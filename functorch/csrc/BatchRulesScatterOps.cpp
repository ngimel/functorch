// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include <functorch/csrc/BatchRulesHelper.h>
#include <iostream>
#include <ATen/Operators.h>
#include <functorch/csrc/PlumbingHelper.h>
#include <functorch/csrc/BatchedFallback.h>


namespace at { namespace functorch {

std::vector<optional<Tensor>> batchIndices(
  ArrayRef<optional<Tensor>> indices,
  ArrayRef<optional<int64_t>> indices_bdims,
  int64_t batch_size,
  optional<int64_t> self_bdim,
  optional<int64_t> values_bdim = nullopt) {
  // There are 3 main cases:
  // 1. self is batched, indices/values are not batched
  // In this case, we just need to augment indices with a None at the front to
  // basically broadcast the indexing across the batch dimension of self.
  //
  // 2. self is not batched, some indices are batched.
  // In this case, we don't need to do anything - indices will automatically
  // broadcast to work with the unbatched self.
  //
  // 3. self is batched, some indices are batched.
  // In this case, we simply need to add an arange that indexes along the first
  // dimension (i.e. the batch dimension). We also need to make sure this
  // broadcasts with the rest of the indices.
  //
  // There is one more case worth mentioning - boolean tensor indices. If we
  // have "batched" boolean tensor indices, that is unrepresentable, as each
  // batch would result in a tensor with different values.
  std::vector<optional<Tensor>> indices_;
  int64_t minIndexDim = 0;
  for (size_t i = 0; i < indices.size(); i++) {
    auto index = indices[i];
    if (index.has_value()) {
      indices_.push_back(moveBatchDimToFront(index.value(), indices_bdims[i]));
      minIndexDim = std::max(minIndexDim, index.value().dim());
      if (index.value().dtype() == kBool && indices_bdims[i].has_value()) {
        throw std::runtime_error("vmap: We do not support batching operators that can support dynamic shape. Attempting to batch over indexing with a boolean mask.");
      }
    } else {
      indices_.push_back(index);
    }
  }

  bool indices_batched = false;
  for (auto idx : indices_bdims) {
    indices_batched = indices_batched || idx.has_value();
  }
  if (!indices_batched && values_bdim.has_value()) {
    minIndexDim += 1;
  }

  if (!indices_batched && self_bdim.has_value()) {
    indices_.insert(indices_.begin(), nullopt);
  } else if (indices_batched && !self_bdim.has_value()) {
    // do nothing
  } else if (indices_batched && (self_bdim.has_value() || values_bdim.has_value())) {
    auto arange_index = at::arange(0, batch_size);
    while (arange_index.dim() < minIndexDim) {
      arange_index = arange_index.unsqueeze(-1);
    }
    indices_.insert(indices_.begin(), arange_index);
  }
  return indices_;
}

std::tuple<Tensor,optional<int64_t>> index_batch_rule(
    const Tensor& self,
    optional<int64_t> self_bdim,
    ArrayRef<optional<Tensor>> indices,
    ArrayRef<optional<int64_t>> indices_bdims) {

  auto self_ = moveBatchDimToFront(self, self_bdim);
  TORCH_INTERNAL_ASSERT(indices.size() == indices_bdims.size());
  std::vector<optional<Tensor>> indices_ = batchIndices(indices, indices_bdims, self_.size(0), self_bdim);
  return std::make_tuple(at::index(self_, List<optional<Tensor>>(indices_)), 0);
}

// plumbing done since we don't support List<optional<Tensor>> in codegen
Tensor index_plumbing(const Tensor & self, const List<optional<Tensor>> & indices
) {
  c10::impl::ExcludeDispatchKeyGuard guard(kBatchedKey);
  auto maybe_layer = maybeCurrentDynamicLayer();
  TORCH_INTERNAL_ASSERT(maybe_layer.has_value());
  int64_t cur_level = maybe_layer->layerId();
  Tensor self_value;
  optional<int64_t> self_bdim;
  std::tie(self_value, self_bdim) = unwrapTensorAtLevel(self, cur_level);
  std::vector<optional<Tensor>> indices_value;
  std::vector<optional<int64_t>> indices_bdims;
  for (const auto&& indRef : indices) {
      optional<Tensor> ind = indRef;
      optional<Tensor> index;
      optional<int64_t> index_bdim;
      if (ind.has_value()) {
        std::tie(index, index_bdim) = unwrapTensorAtLevel(ind.value(), cur_level);
      }
    indices_value.push_back(index);
    indices_bdims.push_back(index_bdim);
  }
  auto results = index_batch_rule(self_value, self_bdim, indices_value, indices_bdims);
  return makeBatched(std::get<0>(results), std::get<1>(results), cur_level);
}

void index_put__batch_rule(
    Tensor& self,
    optional<int64_t> self_bdim,
    ArrayRef<optional<Tensor>> indices,
    ArrayRef<optional<int64_t>> indices_bdims,
    const Tensor& values,
    optional<int64_t> values_bdim,
    bool accumulate) {
  if (!self_bdim.has_value()) {
    vmapIncompatibleInplaceError("index_put");
  }
  auto self_ = moveBatchDimToFront(self, self_bdim);
  auto values_ = moveBatchDimToFront(values, values_bdim);
  TORCH_INTERNAL_ASSERT(indices.size() == indices_bdims.size());
  std::vector<optional<Tensor>> indices_ = batchIndices(indices, indices_bdims, self_.size(0), self_bdim, values_bdim);
  at::index_put_(self_, List<optional<Tensor>>(indices_), values_, accumulate);
}

// plumbing done since we don't support List<optional<Tensor>> in codegen
Tensor& index_put__plumbing(Tensor & self, const List<optional<Tensor>> & indices
, const Tensor & values, bool accumulate) {
  c10::impl::ExcludeDispatchKeyGuard guard(kBatchedKey);
  auto maybe_layer = maybeCurrentDynamicLayer();
  TORCH_INTERNAL_ASSERT(maybe_layer.has_value());
  int64_t cur_level = maybe_layer->layerId();
  Tensor self_value;
  optional<int64_t> self_bdim;
  std::tie(self_value, self_bdim) = unwrapTensorAtLevel(self, cur_level);
  std::vector<optional<Tensor>> indices_value;
  std::vector<optional<int64_t>> indices_bdims;
  for (const auto&& indRef : indices) {
      optional<Tensor> ind = indRef;
      optional<Tensor> index;
      optional<int64_t> index_bdim;
      if (ind.has_value()) {
        std::tie(index, index_bdim) = unwrapTensorAtLevel(ind.value(), cur_level);
      }
    indices_value.push_back(index);
    indices_bdims.push_back(index_bdim);
  }
  Tensor values_value;
  optional<int64_t> values_bdim;
  std::tie(values_value, values_bdim) = unwrapTensorAtLevel(values, cur_level);
  index_put__batch_rule(self_value, self_bdim, indices_value, indices_bdims, values_value, values_bdim, accumulate);
  return self;
}

void _index_put_impl__batch_rule(
    Tensor& self,
    optional<int64_t> self_bdim,
    ArrayRef<optional<Tensor>> indices,
    ArrayRef<optional<int64_t>> indices_bdims,
    const Tensor& values,
    optional<int64_t> values_bdim,
    bool accumulate,
    bool unsafe) {
  if (!self_bdim.has_value()) {
    vmapIncompatibleInplaceError("_index_put_impl_");
  }
  auto self_ = moveBatchDimToFront(self, self_bdim);
  auto values_ = moveBatchDimToFront(values, values_bdim);
  TORCH_INTERNAL_ASSERT(indices.size() == indices_bdims.size());
  std::vector<optional<Tensor>> indices_ = batchIndices(indices, indices_bdims, self_.size(0), self_bdim, values_bdim);
  at::_index_put_impl_(self_, List<optional<Tensor>>(indices_), values_, accumulate, unsafe);
}

// plumbing done since we don't support List<optional<Tensor>> in codegen
Tensor& _index_put_impl__plumbing(Tensor & self, const List<optional<Tensor>> & indices
, const Tensor & values, bool accumulate, bool unsafe) {
  c10::impl::ExcludeDispatchKeyGuard guard(kBatchedKey);
  auto maybe_layer = maybeCurrentDynamicLayer();
  TORCH_INTERNAL_ASSERT(maybe_layer.has_value());
  int64_t cur_level = maybe_layer->layerId();
  Tensor self_value;
  optional<int64_t> self_bdim;
  std::tie(self_value, self_bdim) = unwrapTensorAtLevel(self, cur_level);
  std::vector<optional<Tensor>> indices_value;
  std::vector<optional<int64_t>> indices_bdims;
  for (const auto&& indRef : indices) {
      optional<Tensor> ind = indRef;
      optional<Tensor> index;
      optional<int64_t> index_bdim;
      if (ind.has_value()) {
        std::tie(index, index_bdim) = unwrapTensorAtLevel(ind.value(), cur_level);
      }
    indices_value.push_back(index);
    indices_bdims.push_back(index_bdim);
  }
  Tensor values_value;
  optional<int64_t> values_bdim;
  std::tie(values_value, values_bdim) = unwrapTensorAtLevel(values, cur_level);
  _index_put_impl__batch_rule(self_value, self_bdim, indices_value, indices_bdims, values_value, values_bdim, accumulate, unsafe);
  return self;
}

namespace {

template<typename Func, typename ...Args>
std::tuple<Tensor,optional<int64_t>> scatter_batch_rule(
    Func f,
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    const Scalar& value, Args... args) {
  auto self_logical_rank = rankWithoutBatchDim(self, self_bdim);
  auto index_logical_rank = rankWithoutBatchDim(index, index_bdim);
  auto batch_size = get_bdim_size2(self, self_bdim, index, index_bdim);

  auto self_ = moveBatchDimToFront(self, self_bdim);
  auto index_ = moveBatchDimToFront(index, index_bdim);

  if (self_logical_rank == 0) {
    self_ = self_.unsqueeze(-1);
  }
  if (index_logical_rank == 0) {
    index_ = index_.unsqueeze(-1);
  }
  self_ = ensure_has_bdim(self_, self_bdim.has_value(), batch_size);
  index_ = ensure_has_bdim(index_, index_bdim.has_value(), batch_size);
  auto physical_dim = getPhysicalDim(self_, /*has_batch_dim*/true, dim);

  auto result = f(self_, physical_dim, index_, value, args...);
  // result should have same shape as self
  if (self_logical_rank == 0) {
    result = result.squeeze(-1);
  }
  return std::make_tuple(result, 0);
}

template <typename Func, typename ...Args>
inline std::tuple<Tensor,optional<int64_t>> scatter_batch_rule(
    Func f,
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    const Tensor& src, optional<int64_t> src_bdim, Args... args) {
  auto self_logical_rank = rankWithoutBatchDim(self, self_bdim);
  auto index_logical_rank = rankWithoutBatchDim(index, index_bdim);
  auto src_logical_rank = rankWithoutBatchDim(src, src_bdim);
  auto batch_size = get_bdim_size3(self, self_bdim, index, index_bdim, src, src_bdim);

  auto self_ = moveBatchDimToFront(self, self_bdim);
  auto index_ = moveBatchDimToFront(index, index_bdim);
  auto src_ = moveBatchDimToFront(src, src_bdim);

  if (self_logical_rank == 0) {
    self_ = self_.unsqueeze(-1);
  }
  if (index_logical_rank == 0) {
    index_ = index_.unsqueeze(-1);
  }
  if (src_logical_rank == 0) {
    src_ = src_.unsqueeze(-1);
  }
  self_ = ensure_has_bdim(self_, self_bdim.has_value(), batch_size);
  index_ = ensure_has_bdim(index_, index_bdim.has_value(), batch_size);
  src_ = ensure_has_bdim(src_, src_bdim.has_value(), batch_size);
  auto physical_dim = getPhysicalDim(self_, /*has_batch_dim*/true, dim);

  auto result = f(self_, physical_dim, index_, src_, args...);
  // result should have same shape as self
  if (self_logical_rank == 0) {
    result = result.squeeze(-1);
  }
  return std::make_tuple(result, 0);
}

} // namespace

std::tuple<Tensor,optional<int64_t>> scatter_value_batch_rule(
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    const Scalar& value) {
  return scatter_batch_rule(ATEN_FN2(scatter, value),
                            self, self_bdim, dim, index, index_bdim, value);
}

std::tuple<Tensor,optional<int64_t>> scatter_src_batch_rule(
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    const Tensor& src, optional<int64_t> src_bdim) {
  return scatter_batch_rule(ATEN_FN2(scatter, src),
                            self, self_bdim, dim, index, index_bdim, src, src_bdim);
}

std::tuple<Tensor,optional<int64_t>> scatter_add_batch_rule(
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    const Tensor& src, optional<int64_t> src_bdim) {
  return scatter_batch_rule(ATEN_FN(scatter_add),
                            self, self_bdim, dim, index, index_bdim, src, src_bdim);
}

std::tuple<Tensor,optional<int64_t>> scatter_reduce_batch_rule(
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    const Tensor& src, optional<int64_t> src_bdim,
    const c10::string_view reduce) {
  return scatter_batch_rule(ATEN_FN2(scatter, reduce),
                            self, self_bdim, dim, index, index_bdim, src, src_bdim, reduce);
}

std::tuple<Tensor,optional<int64_t>> scatter_value_reduce_batch_rule(
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    const Scalar& src,
    const c10::string_view reduce) {
  return scatter_batch_rule(ATEN_FN2(scatter, value_reduce),
                            self, self_bdim, dim, index, index_bdim, src, reduce);
}

std::tuple<Tensor,optional<int64_t>> gather_batch_rule(
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    bool sparse_grad) {
  auto self_logical_rank = rankWithoutBatchDim(self, self_bdim);
  auto index_logical_rank = rankWithoutBatchDim(index, index_bdim);
  auto batch_size = get_bdim_size2(self, self_bdim, index, index_bdim);

  auto self_ = moveBatchDimToFront(self, self_bdim);
  auto index_ = moveBatchDimToFront(index, index_bdim);

  if (self_logical_rank == 0) {
    self_ = self_.unsqueeze(-1);
  }
  if (index_logical_rank == 0) {
    index_ = index_.unsqueeze(-1);
  }
  self_ = ensure_has_bdim(self_, self_bdim.has_value(), batch_size);
  index_ = ensure_has_bdim(index_, index_bdim.has_value(), batch_size);
  auto physical_dim = getPhysicalDim(self_, /*has_batch_dim*/true, dim);

  auto result = at::gather(self_, physical_dim, index_, sparse_grad);
  // result should have same rank as index
  if (index_logical_rank == 0) {
    result = result.squeeze(-1);
  }
  return std::make_tuple(result, 0);
}

std::tuple<Tensor,optional<int64_t>> gather_backward_batch_rule(
    const Tensor& grad, optional<int64_t> grad_bdim,
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    bool sparse_grad) {
  auto batch_size = get_bdim_size3(grad, grad_bdim, self, self_bdim, index, index_bdim);
  auto grad_ = moveBatchDimToFront(grad, grad_bdim);
  auto self_ = moveBatchDimToFront(self, self_bdim);
  auto index_ = moveBatchDimToFront(index, index_bdim);

  auto self_logical_rank = rankWithoutBatchDim(self, self_bdim);
  auto index_logical_rank = rankWithoutBatchDim(index, index_bdim);
  auto grad_logical_rank = rankWithoutBatchDim(grad, grad_bdim);

  if (grad_logical_rank == 0) {
    grad_ = grad_.unsqueeze(-1);
  }
  if (self_logical_rank == 0) {
    self_ = self_.unsqueeze(-1);
  }
  if (index_logical_rank == 0) {
    index_ = index_.unsqueeze(-1);
  }
  grad_ = ensure_has_bdim(grad_, grad_bdim.has_value(), batch_size);
  self_ = ensure_has_bdim(self_, self_bdim.has_value(), batch_size);
  index_ = ensure_has_bdim(index_, index_bdim.has_value(), batch_size);

  auto physical_dim = getPhysicalDim(self_, /*has_batch_dim*/true, dim);
  auto result = at::gather_backward(grad_, self_, physical_dim, index_, sparse_grad);
  // result should has same rank as self
  if (self_logical_rank == 0) {
    result = result.squeeze(-1);
  }
  return std::make_tuple(result, 0);
}

namespace {
Tensor get_expanded_index(const Tensor& index, IntArrayRef self_size, int64_t dim) {
  if (index.dim() == 0) {
    return index.expand(self_size);
  }

  // setup new_index_shape as [BS, 1, ..., idx_size, ..., 1]
  // to reshape index_
  auto idx_size = index.size(0);  // get non-batch size of index tensor
  Tensor index_;
  {
    VmapDimVector new_index_shape(self_size.size(), 1);
    new_index_shape[dim] = idx_size;
    index_ = index.view(new_index_shape);
  }
  // Now apply expand to index_
  {
    VmapDimVector new_index_shape = {self_size.begin(), self_size.end()};
    new_index_shape[dim] = idx_size;
    index_ = index_.expand(new_index_shape);
  }
  return index_;
}
}

Tensor index_select_decomp(const Tensor &self, int64_t dim, const Tensor &index)
{
  Tensor index_ = index;
  if (self.dim() > index.dim()) {
    index_ = get_expanded_index(index, self.sizes(), dim);
  }

  auto result = at::gather(self, dim, index_);

  // output of gather has same dimension as `index` while
  // output of index_select has same dimension as self
  // Eg. t = torch.tensor(1)
  //     idx = torch.tensor([0])
  //     torch.index_select(t, 0, idx) # 0-D
  //     torch.gather(t, 0, idx) # 1-D
  if (self.dim() == 0 && result.dim() != 0) {
    result = result.squeeze(-1);
  }

  return result;
}

Tensor index_copy_decomp(
    const Tensor &self, int64_t dim,
    const Tensor &index, const Tensor &source)
{
  Tensor index_ = index;
  if (self.dim() > index.dim()) {
    index_ = get_expanded_index(index, self.sizes(), dim);
  }

  return at::scatter(self, dim, index_, source);  ;
}

Tensor slice_scatter_decomp(const Tensor &self, const Tensor &src,
                            int64_t dim, c10::optional<int64_t> start,
                            c10::optional<int64_t> end, int64_t step)
{
  auto idx = at::arange(start.value_or(0), end.value_or(self.size(dim)), step, self.options().dtype(kLong));
  idx = get_expanded_index(idx, self.sizes(), dim);
  return at::scatter(self, dim, idx, src);
}

Tensor select_scatter_decomp(
    const Tensor &self, const Tensor &source,
    int64_t dim, int64_t index)
{
  // supports negative index
  index = maybe_wrap_dim(index, self.size(dim));
  auto index_ = at::scalar_tensor(index, self.options().dtype(kLong));

  return at::scatter(self, dim, index_.expand_as(self), source.unsqueeze(dim).expand_as(self));
}

std::tuple<Tensor, optional<int64_t>> diagonal_scatter_batch_rule(
    const Tensor &self, c10::optional<int64_t> self_bdim,
    const Tensor &src, c10::optional<int64_t> src_bdim,
    int64_t offset, int64_t dim1, int64_t dim2)
{
  auto self_ = moveBatchDimToFront(self, self_bdim);
  auto src_ = moveBatchDimToFront(src, src_bdim);

  auto batch_size = get_bdim_size2(self, self_bdim, src, src_bdim);

  self_ = ensure_has_bdim(self_, self_bdim.has_value(), batch_size);
  src_ = ensure_has_bdim(src_, src_bdim.has_value(), batch_size);

  auto self_logical_rank = rankWithoutBatchDim(self, self_bdim);
  dim1 = maybe_wrap_dim(dim1, self_logical_rank) + 1;
  dim2 = maybe_wrap_dim(dim2, self_logical_rank) + 1;

  return std::make_tuple(at::diagonal_scatter(self_, src_, offset, dim1, dim2), 0);
}

std::tuple<Tensor,optional<int64_t>> index_add_batch_rule(
    const Tensor& self, optional<int64_t> self_bdim,
    int64_t dim,
    const Tensor& index, optional<int64_t> index_bdim,
    const Tensor& other, optional<int64_t> other_bdim,
    const Scalar& alpha) {
  if (!index_bdim) {
    // Handle scalar tensors... self, other can be scalar tensors
    const auto self_logical_rank = rankWithoutBatchDim(self, self_bdim);
    const auto other_logical_rank = rankWithoutBatchDim(other, other_bdim);
    auto self_ = moveBatchDimToFront(self, self_bdim);
    if (self_logical_rank == 0) {
      self_ = self_.unsqueeze(-1);
    }
    auto other_ = moveBatchDimToFront(other, other_bdim);
    if (other_logical_rank == 0) {
      other_ = other_.unsqueeze(-1);
    }
    dim = maybe_wrap_dim(dim, self_logical_rank);

    const auto batch_size = get_bdim_size2(self, self_bdim, other, other_bdim);
    self_ = ensure_has_bdim(self_, self_bdim.has_value(), batch_size);
    other_ = ensure_has_bdim(other_, other_bdim.has_value(), batch_size);

    auto result = self_.index_add(dim + 1, index, other_, alpha);
    if (self_logical_rank == 0) {
      result = result.squeeze(-1);
    }
    return std::make_tuple(result, 0);
  }

  // Index is batched. For-loop and stack is the best thing I can come up with
  // right now. We really want generalized index_add kernel in PyTorch
  auto batch_size = get_bdim_size3(self, self_bdim, other, other_bdim, index, index_bdim);
  std::vector<Tensor> results;
  results.reserve(batch_size);
  for (const auto i : c10::irange(0, batch_size)) {
    const auto& self_slice = self_bdim.has_value() ?
      self.select(*self_bdim, i) : self;
    const auto& other_slice = other_bdim.has_value() ?
      other.select(*other_bdim, i) : other;
    const auto& index_slice = index_bdim.has_value() ?
      index.select(*index_bdim, i) : index;
    results.push_back(at::index_add(self_slice, dim, index_slice, other_slice, alpha));
  }
  return std::make_tuple(at::stack(results), 0);
}

TORCH_LIBRARY_IMPL(aten, FT_BATCHED_KEY, m) {
  m.impl("index.Tensor", index_plumbing);
  m.impl("index_put_", index_put__plumbing);
  m.impl("_index_put_impl_", _index_put_impl__plumbing);
  m.impl("slice_scatter", slice_scatter_decomp);
  m.impl("select_scatter", select_scatter_decomp);
  m.impl("index_copy", index_copy_decomp);
  m.impl("index_select", index_select_decomp);
  VMAP_SUPPORT("index_add", index_add_batch_rule);
  VMAP_SUPPORT("diagonal_scatter", diagonal_scatter_batch_rule);
  VMAP_SUPPORT("gather", gather_batch_rule);
  VMAP_SUPPORT("gather_backward", gather_backward_batch_rule);
  VMAP_SUPPORT("scatter.value", scatter_value_batch_rule);
  VMAP_SUPPORT("scatter.src", scatter_src_batch_rule);
  VMAP_SUPPORT("scatter_add", scatter_add_batch_rule);
  VMAP_SUPPORT("scatter.reduce", scatter_reduce_batch_rule);
  VMAP_SUPPORT("scatter.value_reduce", scatter_value_reduce_batch_rule);
}

}}
