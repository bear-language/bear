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

static int compile_file_impl(const bearc_args_t* args, bool ret_err_cnt = false) {
    hir::Context ctx{*args, hir::Context::instances::one};

    ctx.try_print_info();

    // all diagnostics (including warns, notes, helps since that's
    // more helpful, also this number is used for tests)
    const auto diag_cnt = ctx.diagnostic_count();
    const auto err_cnt = ctx.error_count();

    if (!err_cnt && !args->flags[CLI_FLAG_CHECK] && !args->flags[CLI_FLAG_PARSE_ONLY]) {
        codegen::CodeGen cg{ctx};

        return diag_cnt + static_cast<int>(cg.emit());
    }

    return ret_err_cnt ? err_cnt : diag_cnt;
}

int compile_file_diag_cnt(const bearc_args_t* args) {
    // run on a thread with a large stack, since compt evaluation recurses on the native stack
    int result = 0;
    utils::run_with_compiler_stack([&] { result = compile_file_impl(args); });
    return result;
}

int compile_file(const bearc_args_t* args) {
    int result = 0;
    utils::run_with_compiler_stack([&] { result = compile_file_impl(args, true); });
    return result;
}

} // extern "C"
