#!/usr/bin/env bash

# starting from project root dir
cd bearc || exit
git clone --branch llvmorg-22.1.8 --depth 1 https://github.com/llvm/llvm-project.git

mkdir llvm-build
cd llvm-build || exit

cmake -G Ninja ../llvm-project/llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_ENABLE_RUNTIMES="" \
  -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_BUILD_EXAMPLES=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_TARGETS_TO_BUILD="X86;ARM;AArch64" \
  -DCMAKE_INSTALL_PREFIX=../llvm

ninja install
