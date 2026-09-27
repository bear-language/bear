//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_EXPR_SOLVER_COMMON_HPP
#define BEARC_COMPILER_HIR_EXPR_SOLVER_COMMON_HPP

#include "compiler/ast/expr.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/indexing.hpp"

namespace hir {

[[nodiscard]] static inline std::optional<hir::ExecConst>
solve_expr_literal(Context& ctx, FileId fid, const ast_expr_t* expr) {
    assert(expr->type == AST_EXPR_LITERAL);
    const token_t* tkn = expr->expr.literal.tkn;
    std::optional<ExecConst> maybe_value{};
    switch (tkn->type) {
    case TOK_CHAR_LIT:
        maybe_value = ExecConst{tkn->val.character};
        break;
    // try as i32 if possible
    case TOK_INT_LIT: {
        maybe_value = ExecConst{tkn->val.signed_integral};
        auto maybe_signed = maybe_value->try_safe_convert_to(builtin_type::i32);
        if (maybe_signed.has_value()) {
            maybe_value = maybe_signed;
        }
    } break;
        // try as i32 and then i64 if possible
    case TOK_UINT_LIT: {
        maybe_value = ExecConst{tkn->val.unsigned_integral};
        auto maybe_signed = maybe_value->try_safe_convert_to(builtin_type::i32);
        if (maybe_signed.has_value()) {
            maybe_value = maybe_signed;
        } else {
            maybe_signed = maybe_value->try_safe_convert_to(builtin_type::i64);
            if (maybe_signed.has_value()) {
                maybe_value = maybe_signed;
            }
        }
        break;
    }
    case TOK_FLOAT_LIT:
        maybe_value = ExecConst{tkn->val.floating};
        break;
    case TOK_STR_LIT:
        maybe_value = ExecConst{ctx.symbol_id_for_str_lit_tkn(tkn, fid)};
        break;
    case TOK_BOOL_LIT_FALSE:
        maybe_value = ExecConst{false};
        break;
    case TOK_BOOL_LIT_TRUE:
        maybe_value = ExecConst{true};
        break;
    case TOK_NULL_LIT:
        maybe_value = ExecConst{nullptr};
        break;
    default:
        std::unreachable();
        break;
    }

    return maybe_value;
}

} // namespace hir

#endif
