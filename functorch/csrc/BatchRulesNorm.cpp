// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include <functorch/csrc/BatchRulesHelper.h>
#include <functorch/csrc/PlumbingHelper.h>
#include <functorch/csrc/BatchedFallback.h>
#include <ATen/core/dispatch/Dispatcher.h>

namespace at { namespace functorch {

static optional<Tensor> maybe_flatten(
    const optional<Tensor>& tensor, optional<int64_t> tensor_bdim) {
  if (!tensor.has_value()) {
    return nullopt;
  }
  TORCH_INTERNAL_ASSERT(tensor_bdim.has_value());
  return reshape_dim_into(*tensor_bdim, 0, *tensor);
}

static bool is_empty_tensor(const Tensor& tensor) {
  const auto shape = tensor.sizes();
  return shape.size() == 1 && shape[0] == 0;
}

static optional<int64_t> compute_stat_bdim(
    optional<int64_t> input_bdim,
    const Tensor& stat) {
  // There's a weird case where mean, rstd can both have shape (0,).
  // It's possible that this is a bug on the PyTorch side.
  // When that happens we don't want to return a BatchedTensor.
  if (input_bdim.has_value() && !is_empty_tensor(stat)) {
    return 0;
  }
  return nullopt;
}

std::tuple<Tensor,optional<int64_t>,Tensor,optional<int64_t>,Tensor,optional<int64_t>>
batch_norm_batch_rule(
    const Tensor& input, optional<int64_t> input_bdim,
    const c10::optional<Tensor>& weight_opt, optional<int64_t> weight_bdim,
    const c10::optional<Tensor>& bias_opt, optional<int64_t> bias_bdim,
    const c10::optional<Tensor>& running_mean_opt, optional<int64_t> running_mean_bdim,
    const c10::optional<Tensor>& running_var_opt, optional<int64_t> running_var_bdim,
    bool training, double momentum, double eps) {
  c10::MaybeOwned<Tensor> weight_maybe_owned = at::borrow_from_optional_tensor(weight_opt);
  const Tensor& weight = *weight_maybe_owned;
  c10::MaybeOwned<Tensor> bias_maybe_owned = at::borrow_from_optional_tensor(bias_opt);
  const Tensor& bias = *bias_maybe_owned;

  auto input_ = input;
  if (input_bdim) {
    input_ = reshape_dim_into(*input_bdim, /*channels dim*/1, input);    
  }
  const auto running_mean = maybe_flatten(running_mean_opt, running_mean_bdim);
  const auto running_var = maybe_flatten(running_var_opt, running_var_bdim);
  c10::MaybeOwned<Tensor> running_mean_maybe_owned = at::borrow_from_optional_tensor(running_mean);
  const Tensor& running_mean_ = *running_mean_maybe_owned;
  c10::MaybeOwned<Tensor> running_var_maybe_owned = at::borrow_from_optional_tensor(running_var);
  const Tensor& running_var_ = *running_var_maybe_owned;

  if (running_mean_bdim != running_var_bdim) {
    throw std::runtime_error("Running mean and running var must either both be batched tensor or both be regular tensors");
  }
  if (input_bdim && !running_mean_bdim) {
    throw std::runtime_error("Batch norm got a batched tensor as input while the running_mean and running_var, which will be updated in place, were not batched.");
  }
  if (!input_bdim && running_mean_bdim) {
    input_ = at::native::expand(input_, running_mean_.sizes());
  }

  const auto input_logical_rank = rankWithoutBatchDim(input, input_bdim);
  const auto result = at::native_batch_norm(input_, nullopt, nullopt, running_mean_, running_var_, training, momentum, eps);
  auto result0 = std::get<0>(result);
  const auto mean = std::get<1>(result);
  const auto rstd = std::get<2>(result);
  const auto stats_bdim = compute_stat_bdim(input_bdim, mean);

  if (input_bdim) {
    auto bdim_size = input.size(input_bdim.value());
    result0 = reshape_dim_outof(1, bdim_size, result0);
    result0 = moveBatchDimToFront(result0, 1);
  }

  if (weight.defined()) {
    auto weight_ = moveBatchDimToFront(weight, weight_bdim);
    weight_ = maybePadToLogicalRank(weight_, /*has_bdim*/weight_bdim, input_logical_rank);
    result0 = result0 * weight_;
  }
  if (bias.defined()) {
    const auto result_logical_rank = rankWithoutBatchDim(
        result0,
        input_bdim.has_value() || weight_bdim.has_value() ? optional<int64_t>(0) : optional<int64_t>(nullopt));
    auto bias_ = moveBatchDimToFront(bias, bias_bdim);
    bias_ = maybePadToLogicalRank(bias_, /*has_bdim*/bias_bdim, result_logical_rank);
    result0 = result0 + bias_;
  }
  return std::make_tuple(result0, 0, mean, stats_bdim, rstd, stats_bdim);
}

std::tuple<Tensor,optional<int64_t>,Tensor,optional<int64_t>,Tensor,optional<int64_t>,Tensor,optional<int64_t>>
cudnn_batch_norm_batch_rule(
    const Tensor& input, optional<int64_t> input_bdim,
    const Tensor& weight, optional<int64_t> weight_bdim,
    const c10::optional<Tensor>& bias_opt, optional<int64_t> bias_bdim,
    const c10::optional<Tensor>& running_mean_opt, optional<int64_t> running_mean_bdim,
    const c10::optional<Tensor>& running_var_opt, optional<int64_t> running_var_bdim,
    bool training, double exponential_average_factor, double eps) {
  c10::MaybeOwned<Tensor> bias_maybe_owned = at::borrow_from_optional_tensor(bias_opt);
  const Tensor& bias = *bias_maybe_owned;

  auto input_ = input;
  if (input_bdim) {
    input_ = reshape_dim_into(*input_bdim, /*channels dim*/1, input);    
  }
  const auto running_mean = maybe_flatten(running_mean_opt, running_mean_bdim);
  const auto running_var = maybe_flatten(running_var_opt, running_var_bdim);
  c10::MaybeOwned<Tensor> running_mean_maybe_owned = at::borrow_from_optional_tensor(running_mean);
  const Tensor& running_mean_ = *running_mean_maybe_owned;
  c10::MaybeOwned<Tensor> running_var_maybe_owned = at::borrow_from_optional_tensor(running_var);
  const Tensor& running_var_ = *running_var_maybe_owned;

  if (running_mean_bdim != running_var_bdim) {
    throw std::runtime_error("Running mean and running var must either both be batched tensor or both be regular tensors");
  }
  if (input_bdim && !running_mean_bdim) {
    throw std::runtime_error("Batch norm got a batched tensor as input while the running_mean and running_var, which will be updated in place, were not batched.");
  }
  if (!input_bdim && running_mean_bdim) {
    input_ = at::native::expand(input_, running_mean_.sizes());
  }

  const auto input_logical_rank = rankWithoutBatchDim(input, input_bdim);
  const auto result = at::native_batch_norm(input_, nullopt, nullopt, running_mean_, running_var_, training, momentum, eps);
  auto result0 = std::get<0>(result);
  const auto mean = std::get<1>(result);
  const auto rstd = std::get<2>(result);
  const auto stats_bdim = compute_stat_bdim(input_bdim, mean);

  if (input_bdim) {
    auto bdim_size = input.size(input_bdim.value());
    result0 = reshape_dim_outof(1, bdim_size, result0);
    result0 = moveBatchDimToFront(result0, 1);
  }

  if (weight.defined()) {
    auto weight_ = moveBatchDimToFront(weight, weight_bdim);
    weight_ = maybePadToLogicalRank(weight_, /*has_bdim*/weight_bdim, input_logical_rank);
    TORCH_WARN("shape " + str(weight_.sizes()));
    TORCH_WARN("input logical rank " + str(input_logical_rank));
    result0 = result0 * weight_;
  }
  if (bias.defined()) {
    const auto result_logical_rank = rankWithoutBatchDim(
        result0,
        input_bdim.has_value() || weight_bdim.has_value() ? optional<int64_t>(0) : optional<int64_t>(nullopt));
    auto bias_ = moveBatchDimToFront(bias, bias_bdim);
    bias_ = maybePadToLogicalRank(bias_, /*has_bdim*/bias_bdim, result_logical_rank);
    result0 = result0 + bias_;
  }
  Tensor reserve = at::empty({0}, input.options().dtype(kByte));
  return std::make_tuple(result0, 0, mean, stats_bdim, rstd, stats_bdim, reserve, nullopt);  
}

std::tuple<Tensor,optional<int64_t>,Tensor,optional<int64_t>,Tensor,optional<int64_t>>
miopen_batch_norm_batch_rule(
    const Tensor& input, optional<int64_t> input_bdim,
    const Tensor& weight, optional<int64_t> weight_bdim,
    const c10::optional<Tensor>& bias_opt, optional<int64_t> bias_bdim,
    const c10::optional<Tensor>& running_mean_opt, optional<int64_t> running_mean_bdim,
    const c10::optional<Tensor>& running_var_opt, optional<int64_t> running_var_bdim,
    bool training, double exponential_average_factor, double eps) {
  c10::MaybeOwned<Tensor> bias_maybe_owned = at::borrow_from_optional_tensor(bias_opt);
  const Tensor& bias = *bias_maybe_owned;

  auto input_ = input;
  if (input_bdim) {
    input_ = reshape_dim_into(*input_bdim, /*channels dim*/1, input);    
  }
  const auto running_mean = maybe_flatten(running_mean_opt, running_mean_bdim);
  const auto running_var = maybe_flatten(running_var_opt, running_var_bdim);
  c10::MaybeOwned<Tensor> running_mean_maybe_owned = at::borrow_from_optional_tensor(running_mean);
  const Tensor& running_mean_ = *running_mean_maybe_owned;
  c10::MaybeOwned<Tensor> running_var_maybe_owned = at::borrow_from_optional_tensor(running_var);
  const Tensor& running_var_ = *running_var_maybe_owned;

  if (running_mean_bdim != running_var_bdim) {
    throw std::runtime_error("Running mean and running var must either both be batched tensor or both be regular tensors");
  }
  if (input_bdim && !running_mean_bdim) {
    throw std::runtime_error("Batch norm got a batched tensor as input while the running_mean and running_var, which will be updated in place, were not batched.");
  }
  if (!input_bdim && running_mean_bdim) {
    input_ = at::native::expand(input_, running_mean_.sizes());
  }

  const auto input_logical_rank = rankWithoutBatchDim(input, input_bdim);
  const auto result = at::native_batch_norm(input_, nullopt, nullopt, running_mean_, running_var_, training, momentum, eps);
  auto result0 = std::get<0>(result);
  const auto mean = std::get<1>(result);
  const auto rstd = std::get<2>(result);
  const auto stats_bdim = compute_stat_bdim(input_bdim, mean);

  if (input_bdim) {
    auto bdim_size = input.size(input_bdim.value());
    result0 = reshape_dim_outof(1, bdim_size, result0);
    result0 = moveBatchDimToFront(result0, 1);
  }

  if (weight.defined()) {
    auto weight_ = moveBatchDimToFront(weight, weight_bdim);
    weight_ = maybePadToLogicalRank(weight_, /*has_bdim*/weight_bdim, input_logical_rank);
    TORCH_WARN("shape " + str(weight_.sizes()));
    TORCH_WARN("input logical rank " + str(input_logical_rank));
    result0 = result0 * weight_;
  }
  if (bias.defined()) {
    const auto result_logical_rank = rankWithoutBatchDim(
        result0,
        input_bdim.has_value() || weight_bdim.has_value() ? optional<int64_t>(0) : optional<int64_t>(nullopt));
    auto bias_ = moveBatchDimToFront(bias, bias_bdim);
    bias_ = maybePadToLogicalRank(bias_, /*has_bdim*/bias_bdim, result_logical_rank);
    result0 = result0 + bias_;
  }
  return std::make_tuple(result0, 0, mean, stats_bdim, rstd, stats_bdim);
}

std::tuple<Tensor,int64_t,Tensor,int64_t,Tensor,int64_t>
native_group_norm_input_batch_rule(
    const Tensor & input, int64_t input_bdim,
    const c10::optional<Tensor> & weight,
    const c10::optional<Tensor> & bias, int64_t N, int64_t C,
    int64_t HxW, int64_t group, double eps) {
  auto bdim_size = input.size(input_bdim);
  auto input_ = reshape_dim_into(input_bdim, 0, input);
  auto result = at::native_group_norm(input_, weight, bias, N * bdim_size, C, HxW, group, eps);
  return std::make_tuple(
      reshape_dim_outof(0, bdim_size, std::get<0>(result)), 0,
      reshape_dim_outof(0, bdim_size, std::get<1>(result)), 0,
      reshape_dim_outof(0, bdim_size, std::get<2>(result)), 0);
}

std::tuple<Tensor,Tensor,Tensor> native_group_norm_plumbing(
    const Tensor & input, const c10::optional<Tensor> & weight,
    const c10::optional<Tensor> & bias, int64_t N, int64_t C,
    int64_t HxW, int64_t group, double eps) {
  auto maybe_layer = maybeCurrentDynamicLayer();
  TORCH_INTERNAL_ASSERT(maybe_layer.has_value());
  int64_t cur_level = maybe_layer->layerId();

  Tensor input_value;
  optional<int64_t> input_bdim;
  std::tie(input_value, input_bdim) = unwrapTensorAtLevel(input, cur_level);
  optional<Tensor> weight_value;
  optional<int64_t> weight_bdim;
  if (weight) {
      std::tie(weight_value, weight_bdim) = unwrapTensorAtLevel(weight.value(), cur_level);
  }
  optional<Tensor> bias_value;
  optional<int64_t> bias_bdim;
  if (bias) {
      std::tie(bias_value, bias_bdim) = unwrapTensorAtLevel(bias.value(), cur_level);
  }

  if (input_bdim && !weight_bdim && !bias_bdim) {
    c10::impl::ExcludeDispatchKeyGuard guard(kBatchedKey);
    auto result = native_group_norm_input_batch_rule(
        input_value, *input_bdim, weight_value, bias_value,
        N, C, HxW, group, eps);
    return std::make_tuple(
        makeBatched(std::get<0>(result), std::get<1>(result), cur_level),
        makeBatched(std::get<2>(result), std::get<3>(result), cur_level),
        makeBatched(std::get<4>(result), std::get<5>(result), cur_level));
  }

  static auto op = c10::Dispatcher::singleton()
    .findSchemaOrThrow("aten::native_group_norm", "");
  return slow_fallback<Tensor,Tensor,Tensor>(op, { input, weight, bias, N, C, HxW, group, eps });
}

C10_ALWAYS_INLINE bool has_same_shape(
    const Tensor& tensor, optional<int64_t> tensor_bdim,
    IntArrayRef normalized_shape) {
  if (!tensor.defined()) {
    return true;
  }
  if (rankWithoutBatchDim(tensor, tensor_bdim) != normalized_shape.size()) {
    return false;
  }
  const auto tensor_shape = tensor.sizes();
  for (const auto i : c10::irange(normalized_shape.size())) {
    auto j = i;
    // (0, 1, 2), 1 -> (0, 2, 3)
    if (tensor_bdim.has_value() && (int64_t)i >= tensor_bdim.value()) {
      j = j + 1;
    }
    if (normalized_shape[i] != tensor_shape[j]) {
      return false;
    }
  }
  return true;
}

C10_ALWAYS_INLINE void check_same_shape(
    const Tensor& tensor, optional<int64_t> tensor_bdim,
    IntArrayRef normalized_shape, const std::string& name) {
  TORCH_CHECK(has_same_shape(tensor, tensor_bdim, normalized_shape),
      "Expected ", name, " to be of same shape as normalized_shape, but got ",
      name, " of shape ",
      tensor.sizes(),
      " and normalized_shape = ",
      normalized_shape);
}

// Ugh, hard to deduplicate
C10_ALWAYS_INLINE void _check_layer_norm_inputs(
    IntArrayRef normalized_shape,
    const Tensor& weight, optional<int64_t> weight_bdim,
    const Tensor& bias, optional<int64_t> bias_bdim) {

  const int normalized_ndim = normalized_shape.size();
  TORCH_CHECK(
      normalized_ndim >= 1,
      "Expected normalized_shape to be at least 1-dimensional, i.e., ",
      "containing at least one element, but got normalized_shape = ",
      normalized_shape);
  check_same_shape(weight, weight_bdim, normalized_shape, "weight");
  check_same_shape(bias, bias_bdim, normalized_shape, "weight");
}

std::tuple<Tensor,optional<int64_t>,Tensor,optional<int64_t>,Tensor,optional<int64_t>>
native_layer_norm_batch_rule(
    const Tensor& input, optional<int64_t> input_bdim,
    IntArrayRef normalized_shape,
    const c10::optional<Tensor>& weight_opt, optional<int64_t> weight_bdim,
    const c10::optional<Tensor>& bias_opt, optional<int64_t> bias_bdim,
    double eps) {
  auto input_ = moveBatchDimToFront(input, input_bdim);
  if (!weight_bdim && !bias_bdim) {
    const auto result = at::native_layer_norm(input_, normalized_shape, weight_opt, bias_opt, eps);
    const auto mean = std::get<1>(result);
    const auto rstd = std::get<2>(result);
    const auto stats_bdim = compute_stat_bdim(input_bdim, mean);
    return std::make_tuple(std::get<0>(result), 0, mean, stats_bdim, rstd, stats_bdim);
  }

  // See [Note: hacky wrapper removal for optional tensor]
  c10::MaybeOwned<Tensor> weight_maybe_owned = at::borrow_from_optional_tensor(weight_opt);
  const Tensor& weight = *weight_maybe_owned;
  c10::MaybeOwned<Tensor> bias_maybe_owned = at::borrow_from_optional_tensor(bias_opt);
  const Tensor& bias = *bias_maybe_owned;
  _check_layer_norm_inputs(normalized_shape, weight, weight_bdim, bias, bias_bdim);

  const auto input_logical_rank = rankWithoutBatchDim(input, input_bdim);
  const auto result = at::native_layer_norm(input_, normalized_shape, nullopt, nullopt, eps);
  auto result0 = std::get<0>(result);
  const auto mean = std::get<1>(result);
  const auto rstd = std::get<2>(result);
  const auto stats_bdim = compute_stat_bdim(input_bdim, mean);

  if (weight.defined()) {
    auto weight_ = moveBatchDimToFront(weight, weight_bdim);
    weight_ = maybePadToLogicalRank(weight_, /*has_bdim*/weight_bdim, input_logical_rank);
    result0 = result0 * weight_;
  }
  if (bias.defined()) {
    const auto result_logical_rank = rankWithoutBatchDim(
        result0,
        input_bdim.has_value() || weight_bdim.has_value() ? optional<int64_t>(0) : optional<int64_t>(nullopt));
    auto bias_ = moveBatchDimToFront(bias, bias_bdim);
    bias_ = maybePadToLogicalRank(bias_, /*has_bdim*/bias_bdim, result_logical_rank);
    result0 = result0 + bias_;
  }
  return std::make_tuple(result0, 0, mean, stats_bdim, rstd, stats_bdim);
}

std::tuple<at::Tensor,optional<int64_t>> native_layer_norm_backward_no_weight_bias_batch_rule(
    const at::Tensor & grad_out, optional<int64_t> grad_out_bdim,
    const at::Tensor & input, optional<int64_t> input_bdim,
    at::IntArrayRef normalized_shape,
    const at::Tensor & mean, optional<int64_t> mean_bdim,
    const at::Tensor & rstd, optional<int64_t> rstd_bdim) {

  if (!grad_out_bdim.has_value() && !input_bdim.has_value() &&
      !mean_bdim.has_value() && !rstd_bdim.has_value()) {
    const auto result = at::native_layer_norm_backward(
        grad_out, input, normalized_shape, mean, rstd, nullopt, nullopt, {true, false, false});
    return std::make_tuple(std::get<0>(result), nullopt);
  }

  auto grad_out_ = moveBatchDimToFront(grad_out, grad_out_bdim);
  auto input_ = moveBatchDimToFront(input, input_bdim);
  auto mean_ = moveBatchDimToFront(mean, mean_bdim);
  auto rstd_ = moveBatchDimToFront(rstd, rstd_bdim);

  // ensure grad_out / input have bdim.
  const auto bdim_size = get_bdim_size2(grad_out, grad_out_bdim, input, input_bdim);
  grad_out_ = ensure_has_bdim(grad_out_, grad_out_bdim.has_value(), bdim_size);
  input_ = ensure_has_bdim(input_, input_bdim.has_value(), bdim_size);
  mean_ = ensure_has_bdim(mean_, mean_bdim.has_value(), bdim_size);
  rstd_ = ensure_has_bdim(rstd_, rstd_bdim.has_value(), bdim_size);

  auto result = at::native_layer_norm_backward(
      grad_out_.contiguous(),
      input_.contiguous(),
      normalized_shape,
      mean_.contiguous(),
      rstd_.contiguous(),
      nullopt, nullopt, {true, false, false});

  return std::make_tuple(std::get<0>(result), 0);
}

std::tuple<at::Tensor,at::Tensor,at::Tensor> native_layer_norm_backward_plumbing(
    const at::Tensor & grad_out,
    const at::Tensor & input,
    at::IntArrayRef normalized_shape,
    const at::Tensor & mean,
    const at::Tensor & rstd,
    const c10::optional<at::Tensor> & weight_opt,
    const c10::optional<at::Tensor> & bias_opt,
    std::array<bool,3> output_mask) {
  // See [Note: hacky wrapper removal for optional tensor]
  c10::MaybeOwned<Tensor> weight_maybe_owned = at::borrow_from_optional_tensor(weight_opt);
  const Tensor& weight = *weight_maybe_owned;
  c10::MaybeOwned<Tensor> bias_maybe_owned = at::borrow_from_optional_tensor(bias_opt);
  const Tensor& bias = *bias_maybe_owned;

  // plumbing
  auto maybe_layer = maybeCurrentDynamicLayer();
  TORCH_INTERNAL_ASSERT(maybe_layer.has_value());
  int64_t cur_level = maybe_layer->layerId();
  Tensor grad_out_value;
  optional<int64_t> grad_out_bdim;
  std::tie(grad_out_value, grad_out_bdim) = unwrapTensorAtLevel(grad_out, cur_level);
  Tensor input_value;
  optional<int64_t> input_bdim;
  std::tie(input_value, input_bdim) = unwrapTensorAtLevel(input, cur_level);
  Tensor mean_value;
  optional<int64_t> mean_bdim;
  std::tie(mean_value, mean_bdim) = unwrapTensorAtLevel(mean, cur_level);
  Tensor rstd_value;
  optional<int64_t> rstd_bdim;
  std::tie(rstd_value, rstd_bdim) = unwrapTensorAtLevel(rstd, cur_level);
  optional<Tensor> weight_value;
  optional<int64_t> weight_bdim;
  if (weight.defined()) {
    std::tie(weight_value, weight_bdim) = unwrapTensorAtLevel(weight, cur_level);
  }
  optional<Tensor> bias_value;
  optional<int64_t> bias_bdim;
  if (bias.defined()) {
    std::tie(bias_value, bias_bdim) = unwrapTensorAtLevel(bias, cur_level);
  }

  // results
  Tensor grad_bias;
  Tensor grad_weight;
  Tensor grad_input;

  if (output_mask[2] && bias_value.has_value()) {
    const auto num_front_dims_to_reduce = grad_out.dim() - normalized_shape.size();
    if (num_front_dims_to_reduce == 0) {
      grad_bias = grad_out;
    } else {
      grad_bias = grad_out.sum(range(0, num_front_dims_to_reduce));
    }
  }
  if (output_mask[1] && weight_value.has_value()) {
    // NB: output isn't saved...
    const auto normalized_input = (input - mean) * rstd;
    const auto expanded_grad_weight = normalized_input * grad_out;
    const auto num_front_dims_to_reduce =
        expanded_grad_weight.dim() - normalized_shape.size();
    if (num_front_dims_to_reduce == 0) {
      grad_weight = expanded_grad_weight;
    } else {
      grad_weight = expanded_grad_weight.sum(range(0, num_front_dims_to_reduce));
    }
  }
  if (output_mask[0]) {
    const auto grad_normalized_input = weight.defined() ?
      grad_out * weight : grad_out;
    Tensor grad_normalized_input_value;
    optional<int64_t> grad_normalized_input_bdim;
    std::tie(grad_normalized_input_value, grad_normalized_input_bdim) =
        unwrapTensorAtLevel(grad_normalized_input, cur_level);

    c10::impl::ExcludeDispatchKeyGuard guard(kBatchedKey);
    const auto results = native_layer_norm_backward_no_weight_bias_batch_rule(
        grad_normalized_input_value, grad_normalized_input_bdim,
        input_value, input_bdim,
        normalized_shape,
        mean_value, mean_bdim,
        rstd_value, rstd_bdim);
    grad_input = makeBatched(std::get<0>(results), std::get<1>(results), cur_level);
  }
  return std::make_tuple(grad_input, grad_weight, grad_bias);
}

TORCH_LIBRARY_IMPL(aten, FT_BATCHED_KEY, m) {
  VMAP_SUPPORT("native_batch_norm", batch_norm_batch_rule);
  m.impl("native_group_norm", native_group_norm_plumbing);
  VMAP_SUPPORT("native_layer_norm", native_layer_norm_batch_rule);
  m.impl("native_layer_norm_backward", native_layer_norm_backward_plumbing);
}

}}
