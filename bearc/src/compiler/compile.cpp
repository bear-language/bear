//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/compile.h"
#include "cli/args.h"
#include "compiler/codegen/llvm/codegen.hpp"
#include "compiler/hir/context.hpp"
#include "utils/stack.hpp"
#include <stddef.h>

extern "C" {

static int compile_file_impl(const bearc_args_t* args) {
    hir::Context ctx{*args, hir::Context::instances::one};
    ctx.try_print_info();
    const auto diag_cnt = ctx.diagnostic_count();

    if (!diag_cnt) {
        codegen::CodeGen cg{ctx};
        return cg.emit();
    }

    return diag_cnt;
}

int compile_file(const bearc_args_t* args) {
    // run on a thread with a large stack, since compt evaluation recurses on the native stack
    int result = 0;
    utils::run_with_compiler_stack([&] { result = compile_file_impl(args); });
    return result;
}

} // extern "C"
