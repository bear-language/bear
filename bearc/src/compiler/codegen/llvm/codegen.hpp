//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/context.hpp"

#ifndef BEARC_COMPILER_BACKENDS_LLVM_HPP
#define BEARC_COMPILER_BACKENDS_LLVM_HPP

namespace codegen {
class CodeGen {
    hir::Context& ctx;

  public:
    explicit CodeGen(hir::Context& ctx);
    // returns true on failure, else false
    bool emit();
};
} // namespace codegen

#endif
