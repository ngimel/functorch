// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include <torch/extension.h>
#include <ATen/WrapDimUtils.h>

#include <functorch/csrc/TensorWrapper.h>
#include <functorch/csrc/DynamicLayer.h>
#include <functorch/csrc/BatchedTensorImpl.h>
#include <functorch/csrc/VmapTransforms.h>
#include <functorch/csrc/BatchedFallback.h>
#include <functorch/csrc/BatchRulesHelper.h>
#include <functorch/csrc/PointwiseOperatorCompileCache.h>
#include <functorch/csrc/CompileCache.h>
#include <functorch/csrc/CustomFunction.h>


namespace at {
namespace functorch {

static bool has_level(const Tensor& self, int64_t level) {
  const auto* batched = maybeGetBatchedImpl(self);
  if (!batched) {
    return false;
  }
  return batched->level() >= level;
}

Tensor _add_batch_dim(const Tensor& self, int64_t batch_dim, int64_t level) {
  return addBatchDim(self, batch_dim, level);
}

static std::pair<Tensor,int64_t> remove_existing_batch_dim(
    const BatchedTensorImpl* batched, int64_t level) {

  TORCH_INTERNAL_ASSERT(batched->level() == level);
  return std::make_pair(batched->value(), batched->bdim());
}

// Poor man's version of np.moveaxis. Moves the dimension at `dst` to `src`
// while preserving the order of other existing dimensions.
// We should probably add np.moveaxis (it is more general) to PyTorch. (#36048)
// When we do, replace the following with it.
static Tensor _movedim(const Tensor& self, int64_t src, int64_t dst) {
  auto logical_dim = self.dim();
  src = maybe_wrap_dim(src, logical_dim);
  dst = maybe_wrap_dim(dst, logical_dim);
  if (src == dst) {
    return self;
  }
  VmapDimVector permutation;
  permutation.reserve(logical_dim);
  for (int64_t dim = 0; dim < logical_dim; dim++) {
    if (dim == src) {
      continue;
    }
    permutation.push_back(dim);
  }
  permutation.insert(permutation.begin() + dst, src);
  return self.permute(permutation);
}

// Removes the batch dim with level `level` from `self`. If this causes the
// last batch dim to be removed from a BatchedTensor, then this returns a
// regular Tensor.
//
// If the `level` of the batch dim to remove does not exist in `self`, then we
// add the batch dim in. This can happen if `self` didn't interact with a tensor
// inside the vmap level, for example,
//     self = torch.randn(3)
//     y = torch.randn(5)
//     out = vmap(lambda x: vmap(lambda y: x)(y))(self)
//     assert out.shape == (3, 5)
// Inside the inner vmap, `x` is a BatchedTensor with a single batch dimension
// corresponding to the *outer* vmap level and it doesn't have any dimensions that
// correspond to the inner vmap level so we need to create one for the user.
//
// `out_dim` controls where we should put the batch dimension in the output tensor.
Tensor _remove_batch_dim(const Tensor& self, int64_t level, int64_t batch_size, int64_t out_dim) {
  if (!has_level(self, level)) {
    auto self_sizes = self.sizes();
    VmapDimVector expanded_sizes(self_sizes.begin(), self_sizes.end());
    expanded_sizes.insert(expanded_sizes.begin() + out_dim, batch_size);
    auto result = self.expand(expanded_sizes);
    return result;
  }

  // Must be batched if has_level(self, /*any_level*/)
  const auto* batched = maybeGetBatchedImpl(self);
  TORCH_INTERNAL_ASSERT(batched != nullptr);

  Tensor self_without_bdim;
  int64_t newly_exposed_logical_dim;
  std::tie(self_without_bdim, newly_exposed_logical_dim) = remove_existing_batch_dim(batched, level);
  auto result = _movedim(self_without_bdim, newly_exposed_logical_dim, out_dim);
  return result;
}

Tensor _wrap_for_grad(const Tensor& self, int64_t level) {
  // NB: different behavior inside??
  // return self;
  // TORCH_INTERNAL_ASSERT(!maybeGetTensorWrapper(self));
  // TORCH_INTERNAL_ASSERT(self.has_storage());
  return makeTensorWrapper(self, level);
}

Tensor _unwrap_for_grad(const Tensor& self, int64_t level) {
  auto* result = maybeGetTensorWrapper(self);
  if (!result) {
    return self;
  }
  TORCH_INTERNAL_ASSERT(result->level().has_value());
  if (result->level() == level) {
    return result->value();
  }
  return self;
}

int64_t dlevel(const Tensor& tensor) {
  auto* wrapped = maybeGetTensorWrapper(tensor);
  if (!wrapped) {
    return 0;
  }
  if (!wrapped->is_alive()) {
    return -1;
  }
  return wrapped->level().value();
}

bool dump_tensor(const Tensor& self) {
  dumpTensorCout(self);
  return true;
}

int64_t _grad_increment_nesting() {
  // See NOTE [grad and vjp interaction with no_grad]
  bool prev_grad_mode = c10::GradMode::is_enabled();
  return initAndPushDynamicLayer(at::DispatchKey::Autograd, nullopt, prev_grad_mode);
}

int64_t _grad_decrement_nesting() {
  auto layer = popDynamicLayerAndDeleteMetadata();
  TORCH_INTERNAL_ASSERT(layer.key() == DispatchKey::Autograd);
  return layer.layerId();
}

int64_t _vmap_increment_nesting(int64_t batch_size) {
  return initAndPushDynamicLayer(kBatchedKey, batch_size);
}

int64_t _vmap_decrement_nesting() {
  auto layer = popDynamicLayerAndDeleteMetadata();
  TORCH_INTERNAL_ASSERT(layer.key() == kBatchedKey);
  return layer.layerId();
}

static bool is_batchedtensor(const Tensor& tensor) {
  auto* batched = maybeGetBatchedImpl(tensor);
  return batched != nullptr;
}

static bool is_gradtrackingtensor(const Tensor& tensor) {
  auto* wrapped = maybeGetTensorWrapper(tensor);
  return wrapped != nullptr;
}

static Tensor get_unwrapped(const Tensor& tensor) {
  auto* batched = maybeGetBatchedImpl(tensor);
  if (batched) {
    return batched->value();
  }
  auto* wrapped = maybeGetTensorWrapper(tensor);
  if (wrapped) {
    return wrapped->value();
  }
  TORCH_CHECK(false, "No wrappers present!");
}

static int64_t maybe_get_level(const Tensor& tensor) {
  auto* batched = maybeGetBatchedImpl(tensor);
  if (batched) {
    return batched->level();
  }
  auto* wrapped = maybeGetTensorWrapper(tensor);
  if (wrapped) {
    if (wrapped->level()) {
      return *wrapped->level();
    }
    // TODO: this is a weird special case...
    return -2;
  }
  return -1;
}

static int64_t maybe_get_bdim(const Tensor& tensor) {
  auto* batched = maybeGetBatchedImpl(tensor);
  if (batched) {
    return batched->bdim();
  }
  return -1;
}

static int64_t currentLevel() {
  auto maybe_layer = maybeCurrentDynamicLayer();
  TORCH_INTERNAL_ASSERT(maybe_layer.has_value());
  int64_t current_level = maybe_layer->layerId();
  return current_level;
}

static std::tuple<Tensor, int64_t> unwrapTensorAtCurrentLevel(const Tensor& tensor) {
  auto maybe_layer = maybeCurrentDynamicLayer();
  TORCH_INTERNAL_ASSERT(maybe_layer.has_value());
  int64_t current_level = maybe_layer->layerId();
  auto result = unwrapTensorAtLevel(tensor, current_level);
  auto value = std::get<0>(result);
  auto bdim = std::get<1>(result);
  value = moveBatchDimToFront(value, bdim);
  return std::make_tuple(value, bdim.has_value() ? 0 : -1);
}

static void tls_set_vmap_excluded(bool excluded) {
  c10::impl::tls_set_dispatch_key_excluded(kBatchedKey, excluded);
}

static bool tls_set_is_included() {
  return c10::impl::tls_is_dispatch_key_included(kDynamicLayerFrontModeKey);
}

static void dump_dls() {
  std::cout << getDynamicLayerStack() << std::endl;
}

static void dump_local_tls() {
  auto tls = c10::impl::tls_local_dispatch_key_set();
  std::cout << "[Local Include] " << tls.included_ << std::endl;
  std::cout << "[Local Exclude] " << tls.excluded_ << std::endl;
}

} // namespace functorch
}


namespace at { namespace functorch {

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  m.def("_add_batch_dim", &at::functorch::_add_batch_dim, "add batch dim");
  m.def("_remove_batch_dim", &at::functorch::_remove_batch_dim, "remove batch dim");
  m.def("_vmap_increment_nesting", &at::functorch::_vmap_increment_nesting, "remove batch dim");
  m.def("_vmap_decrement_nesting", &at::functorch::_vmap_decrement_nesting, "remove batch dim");
  m.def("_grad_increment_nesting", &at::functorch::_grad_increment_nesting, "remove batch dim");
  m.def("_grad_decrement_nesting", &at::functorch::_grad_decrement_nesting, "remove batch dim");
  m.def("_wrap_for_grad", &at::functorch::_wrap_for_grad, "add batch dim");
  m.def("_unwrap_for_grad", &at::functorch::_unwrap_for_grad, "add batch dim");
  m.def("_set_vmap_fallback_warning_enabled", &at::functorch::setVmapFallbackWarningEnabled, "Set vmap fallback warnings");
  m.def("_set_vmap_fallback_enabled", &at::functorch::setVmapFallbackEnabled);
  m.def("_is_vmap_fallback_enabled", &at::functorch::isVmapFallbackEnabled);
  m.def("dlevel", &at::functorch::dlevel, "add batch dim");
  m.def("dump_tensor", &at::functorch::dump_tensor, "add batch dim");
  m.def("reshape_dim_into", &at::functorch::reshape_dim_into);
  m.def("reshape_dim_outof", &at::functorch::reshape_dim_outof);
  m.def("are_transforms_active", &at::functorch::areTransformsActive);
  // various debugging things. Maybe we should offer these as first-class APIs
  // on Tensors?
  m.def("is_batchedtensor", &at::functorch::is_batchedtensor);
  m.def("is_gradtrackingtensor", &at::functorch::is_gradtrackingtensor);
  m.def("get_unwrapped", &at::functorch::get_unwrapped);
  m.def("maybe_get_level", &at::functorch::maybe_get_level);
  m.def("maybe_get_bdim", &at::functorch::maybe_get_bdim);
  m.def("current_level", &at::functorch::currentLevel);
  m.def("unwrap_batchedtensor", &at::functorch::unwrapTensorAtCurrentLevel);
  m.def("tls_set_vmap_excluded", &at::functorch::tls_set_vmap_excluded);
  m.def("tls_set_is_included", &at::functorch::tls_set_is_included);
  m.def("dump_dls", &at::functorch::dump_dls);
  m.def("dump_local_tls", &at::functorch::dump_local_tls);
  at::functorch::initPointwiseOperatorCompileCacheBindings(m.ptr());
  at::functorch::initCompileCacheBindings(m.ptr());
  initDispatchBindings(m.ptr());
}

}}
