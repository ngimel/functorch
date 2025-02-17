# Copyright (c) Facebook, Inc. and its affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.
from torch.testing._internal.common_methods_invocations import op_db

# Generated from codegen/gen_functorch_op_db.py via
# python codegen/gen_functorch_lagging_op_db.py > test/functorch_lagging_op_db.py
#
# People add new OpInfos to PyTorch all the time.
# We want them to be able to add OpInfos without breaking our CI.
# To achieve this, we keep our OpInfo library behind that of Pytorch's and
# we periodically update our OpInfo library by regenerating this file
_functorch_lagging_meta = {
    ('H', ''),
    ('T', ''),
    ('__getitem__', ''),
    ('__radd__', ''),
    ('__rand__', ''),
    ('__rdiv__', ''),
    ('__rmatmul__', ''),
    ('__rmod__', ''),
    ('__rmul__', ''),
    ('__ror__', ''),
    ('__rpow__', ''),
    ('__rsub__', ''),
    ('__rxor__', ''),
    ('_masked.amax', ''),
    ('_masked.amin', ''),
    ('_masked.log_softmax', ''),
    ('_masked.mean', ''),
    ('_masked.norm', ''),
    ('_masked.normalize', ''),
    ('_masked.prod', ''),
    ('_masked.softmax', ''),
    ('_masked.softmin', ''),
    ('_masked.sum', ''),
    ('_masked.var', ''),
    ('abs', ''),
    ('acos', ''),
    ('acosh', ''),
    ('add', ''),
    ('addbmm', ''),
    ('addcdiv', ''),
    ('addcmul', ''),
    ('addmm', ''),
    ('addmm', 'decomposed'),
    ('addmv', ''),
    ('addr', ''),
    ('all', ''),
    ('allclose', ''),
    ('amax', ''),
    ('amin', ''),
    ('aminmax', ''),
    ('angle', ''),
    ('any', ''),
    ('argmax', ''),
    ('argmin', ''),
    ('argsort', ''),
    ('argwhere', ''),
    ('as_strided', ''),
    ('asin', ''),
    ('asinh', ''),
    ('atan', ''),
    ('atan2', ''),
    ('atanh', ''),
    ('atleast_1d', ''),
    ('atleast_2d', ''),
    ('atleast_3d', ''),
    ('baddbmm', ''),
    ('bfloat16', ''),
    ('bfloat16', 'channels_last'),
    ('bincount', ''),
    ('bitwise_and', ''),
    ('bitwise_left_shift', ''),
    ('bitwise_not', ''),
    ('bitwise_or', ''),
    ('bitwise_right_shift', ''),
    ('bitwise_xor', ''),
    ('block_diag', ''),
    ('bmm', ''),
    ('bool', ''),
    ('bool', 'channels_last'),
    ('broadcast_tensors', ''),
    ('broadcast_to', ''),
    ('bucketize', ''),
    ('byte', ''),
    ('byte', 'channels_last'),
    ('cartesian_prod', ''),
    ('cat', ''),
    ('cdist', ''),
    ('ceil', ''),
    ('char', ''),
    ('char', 'channels_last'),
    ('cholesky', ''),
    ('cholesky_inverse', ''),
    ('cholesky_solve', ''),
    ('chunk', ''),
    ('clamp', ''),
    ('clamp', 'scalar'),
    ('clone', ''),
    ('combinations', ''),
    ('complex', ''),
    ('conj', ''),
    ('conj_physical', ''),
    ('contiguous', ''),
    ('copysign', ''),
    ('corrcoef', ''),
    ('cos', ''),
    ('cosh', ''),
    ('count_nonzero', ''),
    ('cov', ''),
    ('cross', ''),
    ('cummax', ''),
    ('cummin', ''),
    ('cumprod', ''),
    ('cumsum', ''),
    ('cumulative_trapezoid', ''),
    ('deg2rad', ''),
    ('diag', ''),
    ('diag_embed', ''),
    ('diagonal', ''),
    ('diagonal_scatter', ''),
    ('diff', ''),
    ('digamma', ''),
    ('dist', ''),
    ('div', 'floor_rounding'),
    ('div', 'no_rounding_mode'),
    ('div', 'trunc_rounding'),
    ('dot', ''),
    ('double', ''),
    ('double', 'channels_last'),
    ('dsplit', ''),
    ('dstack', ''),
    ('eig', ''),
    ('einsum', ''),
    ('empty_like', ''),
    ('eq', ''),
    ('erf', ''),
    ('erfc', ''),
    ('erfinv', ''),
    ('exp', ''),
    ('exp2', ''),
    ('expand', ''),
    ('expand_as', ''),
    ('expm1', ''),
    ('fft.fft', ''),
    ('fft.fft2', ''),
    ('fft.fftn', ''),
    ('fft.fftshift', ''),
    ('fft.hfft', ''),
    ('fft.hfft2', ''),
    ('fft.hfftn', ''),
    ('fft.ifft', ''),
    ('fft.ifft2', ''),
    ('fft.ifftn', ''),
    ('fft.ifftshift', ''),
    ('fft.ihfft', ''),
    ('fft.ihfft2', ''),
    ('fft.ihfftn', ''),
    ('fft.irfft', ''),
    ('fft.irfft2', ''),
    ('fft.irfftn', ''),
    ('fft.rfft', ''),
    ('fft.rfft2', ''),
    ('fft.rfftn', ''),
    ('fill_', ''),
    ('flip', ''),
    ('fliplr', ''),
    ('flipud', ''),
    ('float', ''),
    ('float', 'channels_last'),
    ('float_power', ''),
    ('floor', ''),
    ('floor_divide', ''),
    ('fmax', ''),
    ('fmin', ''),
    ('fmod', ''),
    ('fmod', 'autodiffed'),
    ('frac', ''),
    ('frexp', ''),
    ('full_like', ''),
    ('gather', ''),
    ('gcd', ''),
    ('ge', ''),
    ('geqrf', ''),
    ('gradient', ''),
    ('gt', ''),
    ('half', ''),
    ('half', 'channels_last'),
    ('heaviside', ''),
    ('histc', ''),
    ('histogram', ''),
    ('histogramdd', ''),
    ('hsplit', ''),
    ('hstack', ''),
    ('hypot', ''),
    ('i0', ''),
    ('igamma', ''),
    ('igamma', 'grad_other'),
    ('igammac', ''),
    ('igammac', 'grad_other'),
    ('imag', ''),
    ('index_add', ''),
    ('index_copy', ''),
    ('index_fill', ''),
    ('index_put', ''),
    ('index_select', ''),
    ('inner', ''),
    ('int', ''),
    ('int', 'channels_last'),
    ('inverse', ''),
    ('isclose', ''),
    ('isfinite', ''),
    ('isin', ''),
    ('isinf', ''),
    ('isnan', ''),
    ('isneginf', ''),
    ('isposinf', ''),
    ('isreal', ''),
    ('istft', ''),
    ('kron', ''),
    ('kthvalue', ''),
    ('lcm', ''),
    ('ldexp', ''),
    ('le', ''),
    ('lerp', ''),
    ('lgamma', ''),
    ('linalg.cholesky', ''),
    ('linalg.cholesky_ex', ''),
    ('linalg.cond', ''),
    ('linalg.cross', ''),
    ('linalg.det', ''),
    ('linalg.det', 'singular'),
    ('linalg.eig', ''),
    ('linalg.eigh', ''),
    ('linalg.eigvals', ''),
    ('linalg.eigvalsh', ''),
    ('linalg.householder_product', ''),
    ('linalg.inv', ''),
    ('linalg.inv_ex', ''),
    ('linalg.lstsq', ''),
    ('linalg.lstsq', 'grad_oriented'),
    ('linalg.matrix_norm', ''),
    ('linalg.matrix_power', ''),
    ('linalg.matrix_rank', ''),
    ('linalg.matrix_rank', 'hermitian'),
    ('linalg.multi_dot', ''),
    ('linalg.norm', ''),
    ('linalg.pinv', ''),
    ('linalg.pinv', 'hermitian'),
    ('linalg.pinv', 'singular'),
    ('linalg.qr', ''),
    ('linalg.slogdet', ''),
    ('linalg.solve', ''),
    ('linalg.solve_triangular', ''),
    ('linalg.svd', ''),
    ('linalg.svdvals', ''),
    ('linalg.tensorinv', ''),
    ('linalg.tensorsolve', ''),
    ('linalg.vector_norm', ''),
    ('log', ''),
    ('log10', ''),
    ('log1p', ''),
    ('log2', ''),
    ('log_softmax', ''),
    ('log_softmax', 'dtype'),
    ('logaddexp', ''),
    ('logaddexp2', ''),
    ('logcumsumexp', ''),
    ('logdet', ''),
    ('logical_and', ''),
    ('logical_not', ''),
    ('logical_or', ''),
    ('logical_xor', ''),
    ('logit', ''),
    ('logsumexp', ''),
    ('long', ''),
    ('long', 'channels_last'),
    ('lt', ''),
    ('lu', ''),
    ('lu_solve', ''),
    ('lu_unpack', ''),
    ('mH', ''),
    ('mT', ''),
    ('masked_fill', ''),
    ('masked_scatter', ''),
    ('masked_select', ''),
    ('matmul', ''),
    ('matrix_exp', ''),
    ('max', 'binary'),
    ('max', 'reduction_no_dim'),
    ('max', 'reduction_with_dim'),
    ('maximum', ''),
    ('mean', ''),
    ('median', ''),
    ('meshgrid', 'list_of_tensors'),
    ('meshgrid', 'variadic_tensors'),
    ('min', 'binary'),
    ('min', 'reduction_no_dim'),
    ('min', 'reduction_with_dim'),
    ('minimum', ''),
    ('mm', ''),
    ('mode', ''),
    ('movedim', ''),
    ('msort', ''),
    ('mul', ''),
    ('mv', ''),
    ('mvlgamma', 'mvlgamma_p_1'),
    ('mvlgamma', 'mvlgamma_p_3'),
    ('mvlgamma', 'mvlgamma_p_5'),
    ('nan_to_num', ''),
    ('nanmean', ''),
    ('nanmedian', ''),
    ('nanquantile', ''),
    ('nansum', ''),
    ('narrow', ''),
    ('ne', ''),
    ('neg', ''),
    ('new_empty', ''),
    ('new_full', ''),
    ('new_ones', ''),
    ('new_zeros', ''),
    ('nextafter', ''),
    ('nn.functional.adaptive_avg_pool1d', ''),
    ('nn.functional.adaptive_avg_pool2d', ''),
    ('nn.functional.adaptive_avg_pool3d', ''),
    ('nn.functional.adaptive_max_pool1d', ''),
    ('nn.functional.adaptive_max_pool2d', ''),
    ('nn.functional.adaptive_max_pool3d', ''),
    ('nn.functional.avg_pool1d', ''),
    ('nn.functional.avg_pool2d', ''),
    ('nn.functional.avg_pool3d', ''),
    ('nn.functional.batch_norm', ''),
    ('nn.functional.batch_norm', 'without_cudnn'),
    ('nn.functional.bilinear', ''),
    ('nn.functional.celu', ''),
    ('nn.functional.conv1d', ''),
    ('nn.functional.conv2d', ''),
    ('nn.functional.conv_transpose1d', ''),
    ('nn.functional.conv_transpose2d', ''),
    ('nn.functional.conv_transpose3d', ''),
    ('nn.functional.cosine_embedding_loss', ''),
    ('nn.functional.cosine_similarity', ''),
    ('nn.functional.cross_entropy', ''),
    ('nn.functional.ctc_loss', ''),
    ('nn.functional.dropout', ''),
    ('nn.functional.elu', ''),
    ('nn.functional.embedding', ''),
    ('nn.functional.embedding_bag', ''),
    ('nn.functional.feature_alpha_dropout', ''),
    ('nn.functional.fractional_max_pool2d', ''),
    ('nn.functional.fractional_max_pool3d', ''),
    ('nn.functional.gaussian_nll_loss', ''),
    ('nn.functional.gelu', ''),
    ('nn.functional.glu', ''),
    ('nn.functional.grid_sample', ''),
    ('nn.functional.group_norm', ''),
    ('nn.functional.hardshrink', ''),
    ('nn.functional.hardsigmoid', ''),
    ('nn.functional.hardswish', ''),
    ('nn.functional.hardtanh', ''),
    ('nn.functional.hinge_embedding_loss', ''),
    ('nn.functional.huber_loss', ''),
    ('nn.functional.instance_norm', ''),
    ('nn.functional.interpolate', 'area'),
    ('nn.functional.interpolate', 'bicubic'),
    ('nn.functional.interpolate', 'bilinear'),
    ('nn.functional.interpolate', 'linear'),
    ('nn.functional.interpolate', 'nearest'),
    ('nn.functional.interpolate', 'trilinear'),
    ('nn.functional.layer_norm', ''),
    ('nn.functional.leaky_relu', ''),
    ('nn.functional.linear', ''),
    ('nn.functional.local_response_norm', ''),
    ('nn.functional.logsigmoid', ''),
    ('nn.functional.max_pool1d', ''),
    ('nn.functional.max_pool2d', ''),
    ('nn.functional.max_pool3d', ''),
    ('nn.functional.mish', ''),
    ('nn.functional.mse_loss', ''),
    ('nn.functional.nll_loss', ''),
    ('nn.functional.normalize', ''),
    ('nn.functional.one_hot', ''),
    ('nn.functional.pad', 'circular'),
    ('nn.functional.pad', 'constant'),
    ('nn.functional.pad', 'reflect'),
    ('nn.functional.pad', 'replicate'),
    ('nn.functional.pairwise_distance', ''),
    ('nn.functional.pixel_shuffle', ''),
    ('nn.functional.pixel_unshuffle', ''),
    ('nn.functional.poisson_nll_loss', ''),
    ('nn.functional.prelu', ''),
    ('nn.functional.relu', ''),
    ('nn.functional.relu6', ''),
    ('nn.functional.rrelu', ''),
    ('nn.functional.selu', ''),
    ('nn.functional.silu', ''),
    ('nn.functional.softmin', ''),
    ('nn.functional.softmin', 'with_dtype'),
    ('nn.functional.softplus', ''),
    ('nn.functional.softshrink', ''),
    ('nn.functional.softsign', ''),
    ('nn.functional.tanhshrink', ''),
    ('nn.functional.threshold', ''),
    ('nn.functional.unfold', ''),
    ('nn.functional.upsample_bilinear', ''),
    ('nn.functional.upsample_nearest', ''),
    ('nonzero', ''),
    ('norm', ''),
    ('norm', 'fro'),
    ('norm', 'inf'),
    ('norm', 'nuc'),
    ('ones_like', ''),
    ('ormqr', ''),
    ('outer', ''),
    ('permute', ''),
    ('pinverse', ''),
    ('polar', ''),
    ('polygamma', 'polygamma_n_0'),
    ('polygamma', 'polygamma_n_1'),
    ('polygamma', 'polygamma_n_2'),
    ('polygamma', 'polygamma_n_3'),
    ('polygamma', 'polygamma_n_4'),
    ('positive', ''),
    ('pow', ''),
    ('prod', ''),
    ('put', ''),
    ('qr', ''),
    ('quantile', ''),
    ('rad2deg', ''),
    ('rand_like', ''),
    ('randint_like', ''),
    ('randn_like', ''),
    ('ravel', ''),
    ('real', ''),
    ('reciprocal', ''),
    ('remainder', ''),
    ('remainder', 'autodiffed'),
    ('renorm', ''),
    ('repeat', ''),
    ('repeat_interleave', ''),
    ('reshape', ''),
    ('reshape_as', ''),
    ('resize_', ''),
    ('resize_as_', ''),
    ('resolve_conj', ''),
    ('resolve_neg', ''),
    ('roll', ''),
    ('rot90', ''),
    ('round', ''),
    ('rsqrt', ''),
    ('rsub', 'rsub_scalar'),
    ('rsub', 'rsub_tensor'),
    ('scatter', ''),
    ('scatter_add', ''),
    ('searchsorted', ''),
    ('select', ''),
    ('select_scatter', ''),
    ('sgn', ''),
    ('short', ''),
    ('short', 'channels_last'),
    ('sigmoid', ''),
    ('sign', ''),
    ('signbit', ''),
    ('sin', ''),
    ('sinc', ''),
    ('sinh', ''),
    ('slice_scatter', ''),
    ('softmax', ''),
    ('softmax', 'with_dtype'),
    ('solve', ''),
    ('sort', ''),
    ('special.entr', ''),
    ('special.erfcx', ''),
    ('special.i0e', ''),
    ('special.i1', ''),
    ('special.i1e', ''),
    ('special.ndtr', ''),
    ('special.ndtri', ''),
    ('special.polygamma', 'special_polygamma_n_0'),
    ('special.xlog1py', ''),
    ('special.zeta', ''),
    ('special.zeta', 'grad'),
    ('split', ''),
    ('split', 'list_args'),
    ('split_with_sizes', ''),
    ('sqrt', ''),
    ('square', ''),
    ('squeeze', ''),
    ('stack', ''),
    ('std', ''),
    ('std_mean', ''),
    ('stft', ''),
    ('sub', ''),
    ('sum', ''),
    ('sum_to_size', ''),
    ('svd', ''),
    ('symeig', ''),
    ('t', ''),
    ('take', ''),
    ('take_along_dim', ''),
    ('tan', ''),
    ('tanh', ''),
    ('tensor_split', ''),
    ('tensordot', ''),
    ('tile', ''),
    ('to_sparse', ''),
    ('topk', ''),
    ('trace', ''),
    ('transpose', ''),
    ('trapezoid', ''),
    ('trapz', ''),
    ('triangular_solve', ''),
    ('tril', ''),
    ('triu', ''),
    ('true_divide', ''),
    ('trunc', ''),
    ('unfold', ''),
    ('unique', ''),
    ('unique_consecutive', ''),
    ('unsqueeze', ''),
    ('var', ''),
    ('var_mean', ''),
    ('vdot', ''),
    ('view', ''),
    ('view_as', ''),
    ('view_as_complex', ''),
    ('view_as_real', ''),
    ('vsplit', ''),
    ('vstack', ''),
    ('where', ''),
    ('xlogy', ''),
    ('zero_', ''),
    ('zeros_like', ''),
}


def in_functorch_lagging_op_db(opinfo):
    return (opinfo.name, opinfo.variant_test_name) in _functorch_lagging_meta


functorch_lagging_op_db = [
    opinfo for opinfo in op_db if in_functorch_lagging_op_db(opinfo)
]
