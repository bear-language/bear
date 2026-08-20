//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/codegen/llvm/codegen.hpp"
#include "compiler/hir/context.hpp"
namespace codegen {

CodeGen::CodeGen(hir::Context& ctx) : ctx{ctx} {}

bool CodeGen::emit() {
    // TODO
    return ctx.error_count() != 0;
}

} // namespace codegen
