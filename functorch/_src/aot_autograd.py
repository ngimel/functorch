from functorch import make_fx
import time
import torch
import torch.nn as nn
from functorch import make_functional_with_buffers, make_fx
from torch.fx.node import map_arg
import torch.fx as fx
from torch.fx.proxy import GraphAppendingTracer
from torch.fx import immutable_collections
import torch.utils._pytree as pytree
import torch.utils.dlpack
from torch.fx.passes import graph_drawer
import operator
import os
import inspect
from functorch._C import CompileCache
from functools import partial
from typing import Callable
from .python_key import pythonkey_decompose
from .decompositions import register_decomposition

pytree._register_pytree_node(immutable_collections.immutable_list, lambda x: (list(x), None), lambda x, c: immutable_collections.immutable_list(x))
pytree._register_pytree_node(immutable_collections.immutable_dict, lambda x: (list(x.values()), list(x.keys())), lambda x, c: immutable_collections.immutable_dict({key: value for key, value in zip(c, x)}))

aten = torch.ops.aten

def draw_graph(traced: torch.fx.GraphModule, fname: str, figname: str = "fx_graph"):
    base, ext = os.path.splitext(fname)
    if not ext:
        ext = ".svg"
    print(f"Writing FX graph to file: {base}{ext}")
    g = graph_drawer.FxGraphDrawer(traced, figname)
    x = g.get_main_dot_graph()
    getattr(x, "write_" + ext.lstrip("."))(f"{base}{ext}")

# todo(chilli): clean this up/make it more understandable
def default_partition(fx_module: fx.GraphModule, _joint_inputs):
    bw_nodes = set()
    saved_nodes = set()
    output_node = None
    for n in fx_module.graph.nodes:
        if n.op == 'placeholder' and 'tangents' in n.target:
            bw_nodes.add(n)
        elif n.op != 'output':
            has_color = False

            def is_colored(a):
                nonlocal has_color
                if a in bw_nodes or a in saved_nodes:
                    has_color = True

            def add_saved(a):
                if a not in bw_nodes:
                    saved_nodes.add(a)
            map_arg(n.args, lambda x: is_colored(x))
            map_arg(n.kwargs, lambda x: is_colored(x))
            if has_color:
                bw_nodes.add(n)
                map_arg(n.args, lambda x: add_saved(x))
                map_arg(n.kwargs, lambda x: add_saved(x))
        elif n.op == 'output':
            output_node = n

    num_fwd_outputs = fx_module._out_spec.children_specs[0].num_leaves
    num_bwd_outputs = fx_module._out_spec.children_specs[1].num_leaves
    bw_outputs = output_node.args[0][num_fwd_outputs:]

    bw_graph = fx.Graph()
    value_remap = {}
    for saved_node in saved_nodes:
        value_remap[saved_node] = bw_graph.placeholder(saved_node.name)

    for node in fx_module.graph.nodes:
        if node in bw_nodes or node in bw_outputs:
            value_remap[node] = bw_graph.node_copy(node, lambda n : value_remap[n])

    assert(num_fwd_outputs + num_bwd_outputs == len(output_node.args[0]))
    bwd_outputs = [value_remap[i] for i in bw_outputs]
    if len(bwd_outputs) == 1:
        bwd_outputs = bwd_outputs[0]
    bw_graph.output(bwd_outputs)
    bw_module = fx.GraphModule(fx_module, bw_graph)

    fw_graph = fx.Graph()
    value_remap = {}
    for node in fx_module.graph.nodes:
        if node not in bw_nodes and node.op != 'output':
            value_remap[node] = fw_graph.node_copy(node, lambda n : value_remap[n])

    fwd_outputs = [value_remap[i] for i in output_node.args[0][:num_fwd_outputs]] + [value_remap[n] for n in saved_nodes]
    if len(fwd_outputs) == 1:
        fwd_outputs = fwd_outputs[0]
    fw_graph.output(fwd_outputs)
    fw_module = fx.GraphModule(fx_module, fw_graph)
    fw_module.graph.lint()
    bw_module.graph.lint()
    return fw_module, bw_module

class InvalidNodeBase(object):
    def __repr__(self):
        return "Invalid Node"
InvalidNode = InvalidNodeBase()

import networkx as nx
def _extract_graph_with_inputs_outputs(joint_graph, inputs, outputs):
    """
    Given a graph, extracts out a subgraph that takes the specified nodes as inputs and returns the specified outputs.

    This includes specifying non-placeholder nodes as inputs.

    The general strategy is to initialize all inputs with proxies as we
    encounter them, and trace through the graph, only keeping values which take
    in valid proxies. Then, all dead code is eliminated.
    """
    new_graph = fx.Graph()
    tracer = GraphAppendingTracer(new_graph)
    env = {}

    # Add new placeholder nodes in the order specified by the inputs
    new_inputs = {}
    for node in inputs:
        new_node = new_graph.placeholder(node.name)
        new_inputs[node.name] = new_node

    for node in joint_graph.nodes:
        if node in inputs:
            env[node] = fx.Proxy(new_inputs[node.name], tracer)
        elif node.op == 'placeholder':
            env[node] = InvalidNode
        elif node.op == 'call_function':
            def map_arg_to_proxy(x):
                if isinstance(x, fx.Node):
                    out = env[x]
                    return out
                else:
                    return x
            all_args = pytree.tree_flatten((node.args, node.kwargs))[0]
            all_args = [isinstance(env[x], InvalidNodeBase) for x in all_args if isinstance(x, fx.Node)]
            if any(all_args):
                env[node] = InvalidNode
                continue
            args = pytree.tree_map(map_arg_to_proxy, node.args)
            kwargs = pytree.tree_map(map_arg_to_proxy, node.kwargs)
            out = node.target(*args, **kwargs)
            env[node] = out
        elif node.op == 'get_attr':
            new_node = new_graph.node_copy(node, lambda x: env[x])
            env[node] = fx.Proxy(new_node, tracer)
        elif node.op == 'output':
            pass
    new_graph.output([env[x].node for x in outputs])

    new_graph.eliminate_dead_code()
    new_graph.lint()
    return new_graph

import math
from torch.fx.passes import shape_prop
def partition_with_recompute_fwd_in_bwd(joint_module: fx.GraphModule, _joint_inputs):
    """
    Partitions the joint graph such that the backward recomputes the forward.
    Recomputing helps in trading off memory bandwidth with computation.

    To create the fwd and bwd graph, we copy the joint graph, manually set the
    outputs to just original forward or backward outputs. And then we run the
    resulting graphs through dead code elimintation.
    """

    # draw_graph(joint_module, "joint.svg")
    shape_prop.ShapeProp(joint_module).run(*pytree.tree_flatten(_joint_inputs)[0])
    def is_primal(node):
        return node.op == "placeholder" and "tangents" not in node.target

    def is_tangent(node):
        return node.op == "placeholder" and "tangents" in node.target
    nodes = joint_module.graph.nodes
    num_fwd_outputs = joint_module._out_spec.children_specs[0].num_leaves
    outputs = pytree.tree_flatten([node.args for node in nodes if node.op == 'output'])[0]
    fwd_outputs = outputs[:num_fwd_outputs]
    bwd_outputs = outputs[num_fwd_outputs:]

    primal_inputs = list(filter(is_primal, joint_module.graph.nodes))
    tangent_inputs = list(filter(is_tangent, joint_module.graph.nodes))
    full_bw_graph = joint_module.graph

    nx_graph = nx.DiGraph()
    tangent_closure = set()
    name_to_node = {}
    for node in full_bw_graph.nodes:
        name_to_node[node.name] = node
        if node.op == 'placeholder' and "tangents" in node.target:
            tangent_closure.add(node)
        if node in tangent_closure:
            for user in node.users:
                tangent_closure.add(user)

    compute_intense_ops = [aten.bmm, aten.addmm, aten.cudnn_convolution_backward, aten.cudnn_convolution, aten.max_pool2d_with_indices]
    view_ops = [aten.expand, aten.clone, aten.transpose, aten.t, aten.view, aten._unsafe_view, aten.permute, aten.transpose, aten.t, aten._reshape_alias, aten.squeeze]
    misc_ops = [aten.cat, aten.stack, aten.select, aten.repeat, aten.unbind, aten.new_zeros, aten.cudnn_batch_norm]
    random_ops = [aten.rand_like]
    buggy_ops = [aten.native_layer_norm]
    not_recomputable_ops = compute_intense_ops + view_ops + misc_ops + random_ops + buggy_ops

    for node in full_bw_graph.nodes:
        if node in tangent_closure:
            nx_graph.add_edge(node.name+"_in", "sink", capacity=math.inf)
            continue
        if node.op == 'placeholder' and "primals" in node.target:
            nx_graph.add_edge("source", node.name+"_in", capacity=math.inf)
        
        if not issubclass(node.meta['type'], torch.Tensor):
            weight = math.inf
        else:
            mem_sz = node.meta['tensor_meta'].nbytes
            if node.op == 'placeholder':
                weight = mem_sz
            else:
                weight = mem_sz * 2

        if node.target in not_recomputable_ops:
            nx_graph.add_edge("source", node.name+"_in", capacity=math.inf)

        nx_graph.add_edge(node.name+"_in", node.name+"_out", capacity=weight)
        for user in node.users:
            nx_graph.add_edge(node.name+"_out", user.name+"_in", capacity=math.inf)

    cut_value, partition = nx.minimum_cut(nx_graph, "source", "sink")
    print("cut_value", cut_value/1e9)
    reachable, non_reachable = partition
    cutset = set()
    for u, nbrs in ((n, nx_graph[n]) for n in reachable):
        cutset.update((u, v) for v in nbrs if v in non_reachable)

    cut_nodes = set()
    for node_in, node_out in cutset:
        assert node_in[:-3] == node_out[:-4]
        node_name = node_in[:-3]
        cut_nodes.add(node_name)
    # print(len(cut_nodes), sorted(list(cut_nodes)))


    saved_values = [name_to_node[node] for node in cut_nodes]

    # print(sorted([node.meta['tensor_meta'].nbytes for node in saved_values]))
    # print(sorted(saved_values, key=lambda x: x.meta['tensor_meta'].nbytes))

    primal_inputs = list(filter(is_primal, joint_module.graph.nodes))
    tangent_inputs = list(filter(is_tangent, joint_module.graph.nodes))
    # Construct the forward module
    fwd_graph = _extract_graph_with_inputs_outputs(joint_module.graph, primal_inputs, fwd_outputs + saved_values)
    bwd_graph = _extract_graph_with_inputs_outputs(joint_module.graph, saved_values + tangent_inputs, bwd_outputs)

    # This is to filter out saved values that don't actually end up being used by the backwards pass
    for node in bwd_graph.nodes:
        if node.op == 'placeholder' and not node.users:
            for saved_value in saved_values:
                if saved_value.name == node.name:
                    saved_values.remove(saved_value)
                    break

    # Now, we re-generate the fwd/bwd graphs.
    # NB: This might increase compilation time, but I doubt it matters  
    fwd_graph = _extract_graph_with_inputs_outputs(joint_module.graph, primal_inputs, fwd_outputs + saved_values)
    bwd_graph = _extract_graph_with_inputs_outputs(joint_module.graph, saved_values + tangent_inputs, bwd_outputs)

    fwd_module = fx.GraphModule(joint_module, fwd_graph)
    bwd_module = fx.GraphModule(joint_module, bwd_graph)

    return fwd_module, bwd_module

def create_joint_forward_backward(fn):
    def joint_forward_backward(primals, tangents):
        out = fn(*primals)
        primals = [p for p in pytree.tree_flatten(primals)[0] if p.requires_grad]
        backward_out = []
        if primals:
            backward_out = torch.autograd.grad(out, primals, grad_outputs=tangents, allow_unused=True)
        return out, backward_out
    return joint_forward_backward

def draw_joint_graph(graph, joint_inputs, file_name="full_graph.png"):
    draw_graph(graph, file_name)
    return default_partition(graph, joint_inputs)

def normalize_as_list(x):
    if isinstance(x, tuple):
        return list(x)
    elif isinstance(x, list):
        return x
    return [x]

def create_compiled_function(flat_fn, fw_compiler, bw_compiler, partition_fn, decompose):

    # putting these decompositions here since they shouldn't always be used
    # Kinda sketchy ... we use torch.sub here to have the correct scalar => tensor promotion logic
    @register_decomposition(aten.rsub)
    def rsub(a, b, alpha=1):
        return -aten.sub(a, b)

    # This is only valid if we're running the graph without autograd, such as if the backward pass has been traced.
    @register_decomposition(aten.detach)
    def detach_decomposition(x):
        return x

    @register_decomposition(aten._reshape_alias)
    def _reshape_alias(x, shape, strides):
        return aten.reshape(x, shape)

    joint_forward_backward = create_joint_forward_backward(flat_fn)

    compiled_fw = None
    compiled_bw = None
    num_outs = None

    class CompiledFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, *flat_args):
            nonlocal compiled_fw, compiled_bw, num_outs
            if compiled_fw is None:
                out = flat_fn(*flat_args)
                if isinstance(out, (list, tuple)):
                    num_outs = len(out)
                else:
                    num_outs = 1

                joint_inputs = (flat_args, out)
                with torch.enable_grad():
                    if decompose:
                        with pythonkey_decompose():
                            fx_g = make_fx(joint_forward_backward)(*joint_inputs)
                    else:
                        fx_g = make_fx(joint_forward_backward)(*joint_inputs)
                # for i in range(1000):
                #     attr = f'_tensor_constant{i}'
                #     if hasattr(fx_g, attr):
                #         setattr(fx_g, attr, getattr(fx_g, attr).cuda())
                #     else:
                #         break
                fw_module, bw_module = partition_fn(fx_g, joint_inputs)
                # print(fw_module.code, bw_module.code)

                compiled_fw = fw_compiler(fw_module, flat_args)
                fw_outs = normalize_as_list(compiled_fw(*flat_args))


                sz = []
                for act in fw_outs[num_outs:]:
                    if isinstance(act, torch.nn.parameter.Parameter):
                        act = act.data
                        continue
                    sz.append(act.storage().nbytes())
                print(f"Saved activation GB: {sum(sz)/1e9}")
                bw_args = fw_outs[num_outs:] + fw_outs[0:num_outs]
                compiled_bw = bw_compiler(bw_module, bw_args)
            fw_outs = normalize_as_list(compiled_fw(*flat_args))
            ctx.save_for_backward(*fw_outs[num_outs:])
            if num_outs == 1:
                return fw_outs[0]
            return tuple(fw_outs[0:num_outs])

        @staticmethod
        def backward(ctx, *flat_args):
            # hmm... this doesn't feel right. todo
            # contiguous_args = [t.contiguous() for t in flat_args]
            contiguous_args = [t for t in flat_args]
            out = normalize_as_list(compiled_bw(*ctx.saved_tensors, *contiguous_args))
            out_iter = iter(out)
            grad_out = [next(out_iter) if p else None for p in ctx.needs_input_grad]
            return tuple(grad_out)

    return CompiledFunction


class _CompileCache(CompileCache):
    pass


# using a C++-based pytree reduces the overhead by about 50%
try:
    import tree
    HAS_TREE = True
except ImportError:
    HAS_TREE = False
compile_cache = None


def compiled_function(
    fn, fw_compiler, bw_compiler, partition_fn=default_partition, decompose=False, hasher_type="StaticShapeHasher"
):
    global compile_cache
    if compile_cache is None:
        compile_cache = CompileCache()
    cached_fn = None

    fn_id = id(fn)

    def returned_function(*args, **kwargs):
        global compile_cache
        nonlocal cached_fn
        if HAS_TREE:
            flattened_args = tree.flatten((args, kwargs))
        else:
            flattened_args, _ = pytree.tree_flatten((args, kwargs))
        num_args = len(flattened_args)
        # Check if the fn is already compiled
        cached_fn = compile_cache.at(fn_id, num_args, hasher_type, *flattened_args)

        # Compile the function and save it in the cache
        if cached_fn is None:
            # Compile a new function
            flattened_args, args_spec = pytree.tree_flatten((args, kwargs))
            def flat_fn(*args):
                args, kwargs = pytree.tree_unflatten(args, args_spec)
                return fn(*args, **kwargs)

            cached_fn = create_compiled_function(
                flat_fn, fw_compiler, bw_compiler, partition_fn, decompose
            ).apply

            # Save the compiled_fn in the cache
            compile_cache.insert(
                fn_id, num_args, hasher_type, cached_fn, *flattened_args
            )

        return cached_fn(*flattened_args)

    return returned_function


def num_of_recompilations():
    global compile_cache
    if compile_cache is None:
        return 0
    return compile_cache.size()


def clear_compile_cache():
    global compile_cache
    if compile_cache is not None:
        compile_cache.clear()
        compile_cache = None

def tvm_compile(fx_module, example_inputs, name = None):
    import tvm
    from tvm import relay, auto_scheduler
    from tvm.contrib import graph_executor
    import os

    jit_mod = torch.jit.script(fx_module)
    # jit_mod = torch.jit.trace(fx_module, example_inputs)

    shape_list = [(f"inp_{idx}", i.shape) for idx, i in enumerate(example_inputs)]
    mod, params = relay.frontend.from_pytorch(jit_mod, shape_list)
    target = tvm.target.Target("llvm -mcpu=core-avx2")
    tasks, task_weights = auto_scheduler.extract_tasks(mod['main'], params, target)
    for task in tasks:
        print(task.compute_dag)
    if name is None:
        log_file = f'{time.time()}.json'
    else:
        log_file = f'{name}.json'
    if len(tasks) != 0:
        tuner = auto_scheduler.TaskScheduler(tasks, task_weights)
        if not os.path.exists(log_file):
            tune_option = auto_scheduler.TuningOptions(
                num_measure_trials=10000,  # change this to 20000 to achieve the best performance
                measure_callbacks=[auto_scheduler.RecordToFile(log_file)],
                # early_stopping=1000,
                # verbose=2,
            )
            tuner.tune(tune_option)

    dev = tvm.cpu(0)
    with auto_scheduler.ApplyHistoryBest(log_file):
        with tvm.transform.PassContext(opt_level=3, config={"relay.backend.use_auto_scheduler": True}):
            lib = relay.build(mod, target=target, params=params)
    dtype = "float32"
    m = graph_executor.GraphModule(lib["default"](dev))
    def exec_tvm(*args):
        for idx, arg in enumerate(args, 0):
            if arg.dim() != 0:

                m.set_input(f"inp_{idx}", tvm.nd.from_dlpack(torch.utils.dlpack.to_dlpack(arg)))
        m.run()
        outs = [torch.utils.dlpack.from_dlpack(m.get_output(i).to_dlpack()) for i in range(m.get_num_outputs())]
        return outs
    return exec_tvm

def tvm_function(fn, name):
    return compiled_function(fn, partial(tvm_compile, name=f'fw_{name}'), partial(tvm_compile, name=f'bw_{name}'))

def compiled_module(mod, *args, **kwargs):
    func_mod, params, buffers = make_functional_with_buffers(mod)
    compiled_f = compiled_function(func_mod, *args, **kwargs)

    class CompiledModule(nn.Module):
        def __init__(self):
            super(CompiledModule, self).__init__()
            self.orig_module = mod

        def forward(self, *args, **kwargs):
            return compiled_f(
                tuple(self.parameters()),
                tuple(self.buffers()),
                *args,
                **kwargs
            )

    return CompiledModule()

aot_function = compiled_function
aot_module = compiled_module
