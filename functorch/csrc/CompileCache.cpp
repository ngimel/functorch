// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

///
/// This design stemmed of from the PointwiseOperatorCompileCache with the
/// purpose of making it more generic for AOTAutograd. This is Compile Cache
/// allowing different types of hashing functions, and is agnostic of the
/// compiler.
///
#include <functorch/csrc/CompileCache.h>
#include <torch/csrc/autograd/custom_function.h>
#include <torch/csrc/jit/python/pybind_utils.h>
#include <torch/csrc/jit/tensorexpr/codegen.h>
#include <torch/csrc/utils/pybind.h>

using namespace torch::jit::tensorexpr;

namespace {
/// Record of thread-local state that changes operator behavior.
struct LocalState {
  c10::impl::LocalDispatchKeySet dispatchModifier;
  bool gradModeEnabled;

  at::DispatchKeySet apply(at::DispatchKeySet ks) const {
    return (ks | dispatchModifier.included_) - dispatchModifier.excluded_;
  }

  LocalState()
      : dispatchModifier(c10::impl::tls_local_dispatch_key_set()),
        gradModeEnabled(at::GradMode::is_enabled()) {}
};

/// Helper to pack tensor (dtype, requires grad) into an 8-bit key.
static uint8_t packFlags(const LocalState &state, const at::Tensor &v) {
  static_assert(static_cast<int>(at::ScalarType::NumOptions) < 128,
                "overflow possible");
  at::ScalarType dtype = v.dtype().toScalarType();
  bool requires_grad = state.gradModeEnabled && v.requires_grad();
  return static_cast<uint8_t>(requires_grad) |
         (static_cast<uint8_t>(dtype) << 1);
}

using hash_key_t = std::vector<int64_t>;
/// Per-tensor cache specialization key targetting dynamic shapes. Records
/// dtype, dispatch options, aliasing, and per-dim contiguity/broadcasting
/// information.


enum DimFlags {
  /// A leading dimension implicitly added by broadcasting.
  SIZE_MISSING = 1 << 0,

  /// Size == 1.
  SIZE_ONE = 1 << 1,

  /// Size > 1.
  SIZE_OTHER = 1 << 2,

  /// Stride == 0; broadcasting.
  STRIDE_ZERO = 1 << 3,

  /// Stride == 1; packed contiguously in memory.
  STRIDE_ONE = 1 << 4,

  /// Stride = Stride[i + 1] * Size[i + 1].
  /// Used to collapse dimensions.
  STRIDE_CONTIGUOUS = 1 << 5,

  /// Stride = Stride[i - 1] * Size[i - 1].
  /// Used to collapse dimensions in the other direction.
  STRIDE_TRANSPOSED_CONTIGUOUS = 1 << 6, // stride[i-1] * sizes[i-1]

  /// Stride must be provided as an argument.
  STRIDE_AS_ARG = 1 << 7,
};

std::vector<int> genDimFlags(c10::IntArrayRef sizes, c10::IntArrayRef strides) {
  // Pack all the properties for each dimension into a uint8.
  int nDims = sizes.size();
  std::vector<int> dimflags(nDims);
  for (int64_t dim = 0; dim < nDims; ++dim) {
    uint8_t flag =
        (sizes[dim] == 0 ? SIZE_MISSING
                          : (sizes[dim] == 1 ? SIZE_ONE : SIZE_OTHER));
    if (strides[dim] == 0) {
      flag |= STRIDE_ZERO;
    } else if (strides[dim] == 1) {
      flag |= STRIDE_ONE;
    } else if (dim + 1 < (int64_t)sizes.size() &&
                strides[dim] == strides[dim + 1] * sizes[dim + 1]) {
      flag |= STRIDE_CONTIGUOUS;
    } else if (dim > 0 && strides[dim] == strides[dim - 1] * sizes[dim - 1] &&
                (dimflags[dim - 1] & STRIDE_CONTIGUOUS) == 0) {
      flag |= STRIDE_TRANSPOSED_CONTIGUOUS;
    } else {
      flag |= STRIDE_AS_ARG;
    }
    dimflags[dim] = flag;
  }
  return dimflags;
}

hash_key_t dynamic_hasher(const LocalState &state, const at::Tensor &v) {
    hash_key_t hash = {
      0,
      static_cast<int>(packFlags(state, v)),
      static_cast<int>(state.apply(v.key_set()).raw_repr()),
      static_cast<int>(v.ndimension())};
    auto dimFlags = genDimFlags(v.sizes(), v.strides());
    hash.insert(hash.end(), dimFlags.begin(), dimFlags.end());
    return hash;
}

/// Per-tensor cache specialization key targetting static shapes. Recordsdtype,
/// dispatch options, aliasing, and full shapes and strides.
hash_key_t static_hasher(const LocalState &state, const at::Tensor &v) {
    hash_key_t hash = {
      1,
      static_cast<int>(packFlags(state, v)),
      static_cast<int>(state.apply(v.key_set()).raw_repr()),
      static_cast<int>(v.ndimension())};
    hash.insert(hash.end(), v.sizes().begin(), v.sizes().end());
    hash.insert(hash.end(), v.strides().begin(), v.strides().end());
    return hash;
}

/// ArgCompileCache is a templated class allowing plugging of different types of
/// Hasher/Specialization Keys.
struct CompileCache {
public:
  CompileCache() = default;
  ~CompileCache() = default;

  /// Array defining groups of aliased tensors.

  /// Cache type mapping specialization keys to compiled kernels.
  class vector_hasher {
  public:
    std::size_t operator()(hash_key_t const& vec) const {
      std::size_t seed = vec.size();
      for(auto& i : vec) {
        seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
      }
      return seed;
    }
  };
  using Cache = std::unordered_map<hash_key_t, py::object, vector_hasher>;

  /// Compute the set of specialization keys based on the inputs to
  /// the kernel.
  hash_key_t computeCacheKey(const std::vector<at::Tensor>& args, int numArgs,
                              const std::string& hasherType, int64_t id) {
    LocalState state;
    hash_key_t cacheKey;
    for (int i = 0; i < numArgs; ++i) {
      if (hasherType == "StaticShapeHasher") {
        auto res = static_hasher(state, args[i]);
        cacheKey.insert(cacheKey.end(), res.begin(), res.end());
      } else if (hasherType == "DynamicShapeHasher") {
        auto res = dynamic_hasher(state, args[i]);
        cacheKey.insert(cacheKey.end(), res.begin(), res.end());
      }
    }
    cacheKey.push_back(id);
    cacheKey.push_back(numArgs);
    return cacheKey;
  }

  std::vector<at::Tensor> parsePythonArgs(int numArgs, PyObject *args) {
    // Convert to Tensor Args
    std::vector<at::Tensor> tensorArgs(numArgs);
    for (int i = 0; i < numArgs; ++i) {
      tensorArgs[i] = THPVariable_Unpack(PyTuple_GET_ITEM(args, i));
    }
    return tensorArgs;
  }

  /// Check if the function has already been compiled.
  // py::object at(int64_t id, int numArgs, PyObject *args, PyObject *kwargs) {
  py::object at(int64_t id, int numArgs, const std::string &hasherType,
                PyObject *args) {
    std::vector<at::Tensor> tensorArgs = parsePythonArgs(numArgs, args);
    hash_key_t cacheKey = computeCacheKey(tensorArgs, numArgs, hasherType, id);

    auto item = cache_.find(cacheKey); // protected by GIL

    if (C10_LIKELY(item != cache_.end())) {
      return item->second;
    }
    return py::none();
  }

  /// Insert a new compiled functions for new tensor properties.
  void insert(int64_t id, int numArgs, const std::string &hasherType,
              const py::object &compileFn, PyObject *args) {
    std::vector<at::Tensor> tensorArgs = parsePythonArgs(numArgs, args);
    LocalState state;
    hash_key_t cacheKey= computeCacheKey(tensorArgs, numArgs, hasherType, id);
    cache_.emplace(cacheKey, compileFn);
  }

  const int64_t size() const { return cache_.size(); }

  /// Clear the cache.
  void clear() { cache_.clear(); }

private:
  /// Compilation cache holding key and the compiled function.
  Cache cache_;
};

static CompileCache *createCompileCache() { return new CompileCache(); }

} // namespace

namespace at {
namespace functorch {

// TODO(anijain) - Add static compilation cache
void initCompileCacheBindings(PyObject *module) {
  py::handle te(module);
  py::class_<CompileCache>(te, "CompileCache")
      .def(py::init(&createCompileCache))
      .def("at",
           [](CompileCache &self, int64_t id, int numArgs,
              const std::string hasherType, py::args args) {
             return self.at(id, numArgs, hasherType, args.ptr());
           })
      .def("insert",
           [](CompileCache &self, int64_t id, int numArgs,
              const std::string hasherType, const py::object &compileFn,
              py::args args, py::kwargs kwargs) {
             self.insert(id, numArgs, hasherType, compileFn, args.ptr());
           })
      .def("clear", [](CompileCache &self) { self.clear(); })
      .def("size", [](CompileCache &self) { return self.size(); });
}

} // namespace functorch
} // namespace at
