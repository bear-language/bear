//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/parser/parse_expr.h"
#include "compiler/ast/expr.h"
#include "compiler/ast/params.h"
#include "compiler/ast/type.h"
#include "compiler/diagnostics/error_codes.h"
#include "compiler/diagnostics/error_list.h"
#include "compiler/parser/parse_stmt.h"
#include "compiler/parser/parse_type.h"
#include "compiler/parser/parser.h"
#include "compiler/parser/rules.h"
#include "compiler/parser/token_eaters.h"
#include "compiler/token.h"
#include "parse_token_slice.h"
#include "utils/arena.h"
#include "utils/spill_arr.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PREC_INIT UINT8_MAX

ast_slice_of_exprs_t parser_freeze_expr_spill_arr(parser_t* p, spill_arr_ptr_t* sarr) {
    ast_slice_of_exprs_t slice = {
        .start = (ast_expr_t**)arena_alloc(p->arena, sarr->size * sizeof(ast_expr_t*)),
        .len = sarr->size,
    };
    spill_arr_ptr_flat_copy((void**)slice.start, sarr);
    spill_arr_ptr_destroy(sarr);
    return slice;
}

ast_slice_of_exprs_t parse_slice_of_exprs_call(parser_t* p, token_type_e divider,
                                               token_type_e until_tkn,
                                               ast_expr_t* (*call)(parser_t*)) {
    spill_arr_ptr_t sarr;
    spill_arr_ptr_init(&sarr);

    while (!(parser_peek_match(p, until_tkn) || parser_eof(p)) // while !eof (edge-case handling)
    ) {
        spill_arr_ptr_push(&sarr, call(p));
        if (!(parser_peek_match(p, until_tkn) || parser_eof(p))) {
            parser_expect_token(p, divider);
        }
    }

    return parser_freeze_expr_spill_arr(p, &sarr);
}

static ast_slice_of_exprs_t parse_slice_of_exprs_call_call(parser_t* p, token_type_e divider,
                                                           bool (*divider_call)(token_type_e),
                                                           ast_expr_t* (*call)(parser_t*)) {
    spill_arr_ptr_t sarr;
    spill_arr_ptr_init(&sarr);

    while (!divider_call(parser_peek(p)->type) || parser_eof(p)) // while !eof (edge-case handling)
    {
        spill_arr_ptr_push(&sarr, call(p));
        if (!divider_call(parser_peek(p)->type) || parser_eof(p)) {
            parser_expect_token(p, divider);
        }
    }

    return parser_freeze_expr_spill_arr(p, &sarr);
}

ast_expr_t* parser_alloc_expr(parser_t* p) { return arena_alloc(p->arena, sizeof(ast_expr_t)); }

static ast_expr_t* parse_primary_expr_impl(parser_t* p, ast_expr_t* opt_atom) {
    token_t* first_tkn = parser_peek(p);
    token_type_e first_type = first_tkn->type;
    ast_expr_t* lhs = opt_atom;
    // if no atoms, try to parse atoms as lhs ~~~~~~~~~~~~~~~~~~~
    if (!lhs) {
        if (token_is_builtin_type_or_id(first_type)) {

            lhs = parse_id(p);

            ast_slice_of_generic_args_t gen_args;

            // try generic struct init or function call

            if (try_parse_generic_args(p, &gen_args)) {
                // struct init in form Foo{.thing = 1, .bar = 2}
                if (parser_peek_match(p, TOK_LBRACE)
                    && parser_mode(p) != PARSER_MODE_BAN_STRUCT_INIT) {
                    lhs = parse_expr_struct_init(p, lhs, &gen_args);
                }
                // fn call
                else if (parser_peek_match(p, TOK_LPAREN)) {
                    lhs = parse_fn_call(p, lhs, &gen_args);
                } else {
                    lhs = parse_expr_generic_id(p, lhs->expr.id.slice, gen_args);
                }
            } else {
                // struct init
                if (parser_peek_match(p, TOK_LBRACE)
                    && parser_mode(p) != PARSER_MODE_BAN_STRUCT_INIT) {
                    lhs = parse_expr_struct_init(p, lhs, NULL); // no generic args
                }
                // fn call
                else if (parser_peek_match(p, TOK_LPAREN)) {
                    lhs = parse_fn_call(p, lhs, NULL); // no generic args
                }
            }

        } else if (token_is_literal(first_type)) {
            lhs = parse_literal(p);
        } else if (first_type == TOK_LPAREN) {
            lhs = parse_grouping(p);
        } else if (first_type == TOK_MATCH) {
            lhs = parse_expr_match(p);
        } else if (first_type == TOK_LBRACE) {
            lhs = parse_expr_struct_init(p, NULL, NULL);
        } else if (first_type == TOK_BAR || first_type == TOK_MOVE || first_type == TOK_BOOL_OR) {
            lhs = parse_expr_closure(p);
        } else if (first_type == TOK_LBRACK) {
            lhs = parse_expr_list_literal(p);
        }
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    token_type_e next_type = parser_peek(p)->type;
    // if atom, try to parse postunary, since postunary prec > preunary prec
    if (lhs && is_postunary_op(parser_peek(p)->type)) {
        return parse_postunary(p, lhs);
    }
    // try fn call or variant decomp too
    if (lhs && (next_type == TOK_LPAREN)) {
        return parse_fn_call(p, lhs, NULL); // no gen args
    }
    // try subscript
    while (parser_peek_match(p, TOK_LBRACK)) {
        lhs = parse_subscript(p, lhs);
    }
    // try ++x, etc.
    if (!lhs && is_preunary_op(first_type)) {
        return parse_expr_prec(p, NULL, prec_preunary(first_type));
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if (lhs) {
        return lhs;
    }
    // failure case
    compiler_error_list_emplace(p->error_list, first_tkn, ERR_EXPECTED_EXPRESSION);
    return parser_sync_expr(p);
}

ast_expr_t* parse_primary_expr(parser_t* p) { return parse_primary_expr_impl(p, NULL); }

ast_expr_t* parse_expr(parser_t* p) {
    ast_expr_t* lhs = parse_primary_expr_impl(p, NULL);
    return parse_expr_prec(p, lhs, PREC_INIT);
}

ast_expr_t* parse_expr_prec(parser_t* p, ast_expr_t* lhs, uint8_t prec) {
    token_type_e op = parser_peek(p)->type;
    if (!lhs && is_preunary_op(op)) {
        return parse_preunary_expr(p);
    }
    if (is_legal_binary_op(p, parser_peek(p)->type)) {
        return parse_binary(p, lhs, prec);
    }
    if (!lhs) {
        return parse_expr(p);
    }
    assert(lhs != NULL && "[parse_expr.c|parse_expr_prec] lhs is NULL");
    return lhs;
}

ast_expr_t* parse_preunary_expr(parser_t* p) {
    // special preunary cases
    switch (parser_peek(p)->type) {
    case TOK_AMPER:
        return parse_expr_borrow(p);
    case TOK_COMPT:
        return parse_expr_compt(p);
    case TOK_SAME_TYPE:
        return parse_expr_same_type(p);
    case TOK_HAS_CONTRACT:
        return parse_expr_has_contract(p);
    case TOK_STATIC_ASSERT:
        return parse_expr_static_assert(p);
    case TOK_INFERABLE_AS:
        return parse_expr_inferable_as(p);
    case TOK_TYPE_TO_STR:
        return parse_expr_type_to_str(p);
    case TOK_DEFINED:
        return parse_expr_defined(p);
    case TOK_ERR:
    case TOK_WARN:
    case TOK_NOTE:
    case TOK_HELP:
        return parse_expr_diagnostic(p);
    default:
        break;
    }
    token_t* op = parser_eat(p); // already been checked that this token is legit
    ast_expr_t* preunary_expr = parser_alloc_expr(p);
    // set op
    preunary_expr->type = AST_EXPR_PRE_UNARY;
    preunary_expr->first = op;
    preunary_expr->expr.unary.op = op;
    // get and set sub expression
    ast_expr_t* sub_expr = NULL;
    // things like @sizeof(...)
    if (token_is_preunary_op_expecting_type(op->type)) {
        // sizeof(
        //       ^
        token_t* lparen = parser_match_token(p, TOK_LPAREN);
        sub_expr = parse_expr_type(p);
        // sizeof(sub_expr)
        //                ^
        if (lparen) {
            parser_expect_token(p, TOK_RPAREN);
        }
    }
    // all others like ++x, --x
    else {
        sub_expr = parse_primary_expr_impl(p, NULL);
    }
    preunary_expr->expr.unary.expr = sub_expr;
    preunary_expr->last = parser_prev(p);
    return preunary_expr;
}

ast_expr_t* parse_expr_same_type(parser_t* p) {
    ast_expr_t* ex = parser_alloc_expr(p);
    ex->type = AST_EXPR_SAME_TYPE;

    token_t* st_tkn = parser_expect_token(p, TOK_SAME_TYPE);
    if (!st_tkn) {
        return parser_sync_expr(p);
    }

    parser_expect_token(p, TOK_LPAREN);
    ex->expr.same_type.lhs_type = parse_type(p);
    parser_expect_token(p, TOK_COMMA);
    ex->expr.same_type.rhs_type = parse_type(p);
    parser_expect_token(p, TOK_RPAREN);

    ex->first = st_tkn;
    ex->last = parser_prev(p);
    return ex;
}

ast_expr_t* parse_expr_defined(parser_t* p) {
    ast_expr_t* ex = parser_alloc_expr(p);
    ex->type = AST_EXPR_DEFINED;

    token_t* tts_tkn = parser_expect_token(p, TOK_DEFINED);
    if (!tts_tkn) {
        return parser_sync_expr(p);
    }

    token_t* lparen = parser_match_token(p, TOK_LPAREN);

    ex->expr.defined.member = false;

    token_ptr_slice_t id_slice;
    if (parser_peek_n(p, 1)->type == TOK_DOT) {
        id_slice = parse_id_token_slice(p, TOK_DOT);

        ex->expr.defined.member = true;
    } else {
        id_slice = parse_id_token_slice(p, TOK_SCOPE_RES);
    }

    // handle failure
    if (id_slice.len == 0) {
        ex->type = AST_EXPR_INVALID;
        return ex;
    }
    ex->expr.defined.id = id_slice;

    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }

    ex->first = tts_tkn;
    ex->last = parser_prev(p);
    return ex;
}

ast_expr_t* parse_expr_has_contract(parser_t* p) {
    ast_expr_t* ex = parser_alloc_expr(p);
    ex->type = AST_EXPR_HAS_CONTRACT;

    token_t* tts_tkn = parser_expect_token(p, TOK_HAS_CONTRACT);
    if (!tts_tkn) {
        return parser_sync_expr(p);
    }

    token_t* lparen = parser_expect_token(p, TOK_LPAREN);

    ex->expr.has_contract.type = parse_type(p);

    parser_expect_token(p, TOK_COMMA);

    ast_expr_t* id_expr = parse_expr_contract(p);

    if (id_expr->type != AST_EXPR_ID && id_expr->type != AST_EXPR_GENERIC_ID) {
        return parser_sync_expr(p);
    }

    ex->expr.has_contract.contract = id_expr;

    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }

    ex->first = tts_tkn;
    ex->last = parser_prev(p);
    return ex;
}

ast_expr_t* parse_expr_type_to_str(parser_t* p) {
    ast_expr_t* ex = parser_alloc_expr(p);
    ex->type = AST_EXPR_TYPE_TO_STR;

    token_t* tts_tkn = parser_expect_token(p, TOK_TYPE_TO_STR);
    if (!tts_tkn) {
        return parser_sync_expr(p);
    }

    token_t* lparen = parser_match_token(p, TOK_LPAREN);
    ex->expr.type_to_str.type = parse_type(p);
    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }

    ex->first = tts_tkn;
    ex->last = parser_prev(p);
    return ex;
}

ast_expr_t* parse_expr_static_assert(parser_t* p) {
    ast_expr_t* ex = parser_alloc_expr(p);

    ex->type = AST_EXPR_STATIC_ASSERT;

    token_t* stass_tkn = parser_expect_token(p, TOK_STATIC_ASSERT);

    if (!stass_tkn) {
        return parser_sync_expr(p);
    }

    token_t* lparen = parser_match_token(p, TOK_LPAREN);
    ex->expr.static_assert_expr.inner = parse_expr(p);

    ex->expr.static_assert_expr.maybe_string = NULL;

    if (parser_match_token(p, TOK_COMMA)) {
        ex->expr.static_assert_expr.maybe_string = parse_expr(p);
    }

    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }

    ex->first = stass_tkn;
    ex->last = parser_prev(p);
    return ex;
}

ast_expr_t* parse_expr_diagnostic(parser_t* p) {
    ast_expr_t* ex = parser_alloc_expr(p);

    ex->type = AST_EXPR_DIAGNOSTIC;

    token_t* tkn = parser_eat(p); // fine since we knew to call this function

    if (!tkn) {
        return parser_sync_expr(p);
    }

    token_t* lparen = parser_match_token(p, TOK_LPAREN);

    ex->expr.diagnostic.first_expr = parse_expr(p);

    ex->expr.diagnostic.maybe_second_expr = NULL;

    if (parser_match_token(p, TOK_COMMA)) {
        ex->expr.diagnostic.maybe_second_expr = parse_expr(p);
    }

    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }

    error_diag_type_e ty = DIAG_TYPE_ERROR;
    switch (tkn->type) {
    case TOK_ERR:
        ty = DIAG_TYPE_ERROR;
        break;
    case TOK_WARN:
        ty = DIAG_TYPE_WARNING;
        break;
    case TOK_NOTE:
        ty = DIAG_TYPE_NOTE;
        break;
    case TOK_HELP:
        ty = DIAG_TYPE_HELP;
        break;
    default:
        break;
    }

    ex->expr.diagnostic.diag_type = ty;

    ex->first = tkn;
    ex->last = parser_prev(p);
    return ex;
}

ast_expr_t* parse_expr_inferable_as(parser_t* p) {
    ast_expr_t* ex = parser_alloc_expr(p);
    ex->type = AST_EXPR_INFERABLE_AS;

    token_t* kw_tkn = parser_expect_token(p, TOK_INFERABLE_AS);
    if (!kw_tkn) {
        return parser_sync_expr(p);
    }

    parser_expect_token(p, TOK_LPAREN);
    ex->expr.same_type.lhs_type = parse_type(p);
    parser_expect_token(p, TOK_COMMA);
    ex->expr.same_type.rhs_type = parse_type(p);
    parser_expect_token(p, TOK_RPAREN);

    ex->first = kw_tkn;
    ex->last = parser_prev(p);
    return ex;
}

ast_expr_t* parse_literal(parser_t* p) {
    ast_expr_t* lit_expr = parser_alloc_expr(p);
    token_t* tkn = parser_expect_token_call(p, token_is_literal, ERR_EXPECTED_LITERAL);
    if (!tkn) {
        return parser_sync_expr(p);
    }
    lit_expr->expr.literal.tkn = tkn;
    lit_expr->type = AST_EXPR_LITERAL;
    lit_expr->first = tkn;
    lit_expr->last = tkn;
    return lit_expr;
}

ast_expr_t* parse_id(parser_t* p) {
    ast_expr_t* id_expr = parser_alloc_expr(p);
    token_ptr_slice_t id_slice = parse_id_token_slice(p, TOK_SCOPE_RES);
    // handle failure edge case, this is usually safe if we knew to enter this function
    if (id_slice.len == 0) {
        return parser_sync_expr(p);
    }
    id_expr->expr.id.slice = id_slice;
    id_expr->type = AST_EXPR_ID;
    id_expr->first = id_slice.start[0];
    id_expr->last = id_slice.start[id_slice.len - 1];
    return id_expr;
}

ast_expr_t* parse_expr_from_id_slice(parser_t* p, token_ptr_slice_t id_slice) {
    ast_expr_t* id_expr = parser_alloc_expr(p);
    id_expr->expr.id.slice = id_slice;
    id_expr->type = AST_EXPR_ID;
    id_expr->first = id_expr->expr.id.slice.start[0];
    id_expr->last = id_expr->expr.id.slice.start[id_expr->expr.id.slice.len - 1];
    ast_expr_t* lhs = parse_primary_expr_impl(p, id_expr);
    return parse_expr_prec(p, lhs, PREC_INIT);
}

token_t* parse_var_name(parser_t* p) {
    return parser_expect_token_with_err_code(p, TOK_IDENTIFIER, ERR_EXPECTED_VARIABLE_NAME);
}

static bool binary_bind_right(token_type_e curr_op, token_type_e next_op) {
    uint8_t curr_prec = prec_binary(curr_op);
    uint8_t next_prec = prec_binary(next_op);
    return (next_prec < curr_prec)
           || (next_prec == curr_prec && is_right_assoc_from_prec(curr_prec));
}

ast_expr_t* parse_binary(parser_t* p, ast_expr_t* lhs, uint8_t max_prec) {
    ast_expr_t* binary_expr = parser_alloc_expr(p);
    if (parser_peek_match(p, TOK_IF)) {
        return parse_expr_ternary_if(p, lhs);
    }
    token_t* op_tkn = parser_eat(p); // already verfied legit
    binary_expr->type = AST_EXPR_BINARY;
    binary_expr->expr.binary.lhs = lhs;
    binary_expr->expr.binary.op = op_tkn;
    ast_expr_t* middle_expr = NULL;
    // handle special binary ops
    if (op_tkn->type == TOK_AS) {
        middle_expr = parse_expr_type(p);
    } else if (op_tkn->type == TOK_IS) {
        middle_expr = parse_expr_variant_decomp(p);
    } else {
        middle_expr = parse_primary_expr_impl(p, NULL);
    }
    token_type_e curr_op = op_tkn->type;
    token_type_e next_op = parser_peek(p)->type;

    while (binary_bind_right(curr_op, next_op)) {
        middle_expr = parse_expr_prec(p, middle_expr, prec_binary(curr_op));
        curr_op = next_op;
        next_op = parser_peek(p)->type;
    }
    binary_expr->expr.binary.rhs = middle_expr;
    binary_expr->first = binary_expr->expr.binary.lhs->first;
    binary_expr->last = binary_expr->expr.binary.rhs->last;

    if (is_legal_binary_op(p, parser_peek(p)->type)
        && prec_binary(parser_peek(p)->type) < max_prec) {
        return parse_binary(p, binary_expr, max_prec);
    }
    return binary_expr;
}

ast_expr_t* parse_postunary(parser_t* p, ast_expr_t* lhs) {
    ast_expr_t* postunary_expr = parser_alloc_expr(p);
    token_t* op = parser_eat(p); // already verfied legit
    postunary_expr->type = AST_EXPR_POST_UNARY;
    postunary_expr->expr.unary.expr = lhs;
    postunary_expr->expr.unary.op = op;
    postunary_expr->first = postunary_expr->expr.unary.expr->first;
    postunary_expr->last = op;
    return postunary_expr;
}

ast_expr_t* parse_fn_call(parser_t* p, ast_expr_t* lhs, ast_slice_of_generic_args_t* gen_args) {
    ast_expr_t* call_expr = parser_alloc_expr(p);
    if (gen_args) {
        call_expr->expr.fn_call.is_generic = true;
        call_expr->expr.fn_call.generic_args = *gen_args;
    } else {
        call_expr->expr.fn_call.generic_args
            = (ast_slice_of_generic_args_t){.valid = true, .len = 0, .start = NULL};
        call_expr->expr.fn_call.is_generic = false;
    }
    token_t* lparen = parser_expect_token(p, TOK_LPAREN); // should be verfied legit
    if (!lparen) {
        return parser_sync_expr(p);
    }
    call_expr->type = AST_EXPR_FN_CALL;
    call_expr->expr.fn_call.left_expr = lhs;

    spill_arr_ptr_t args;
    spill_arr_ptr_init(&args);

    // skip the args parsing loop if the struct is foo()
    if (!(parser_peek(p)->type == TOK_RPAREN)) {
        do {
            *((ast_expr_t**)spill_arr_ptr_emplace(&args)) = parse_expr(p);
        } while (parser_match_token(p, TOK_COMMA));
    }

    token_t* rparen = parser_expect_token(p, TOK_RPAREN);
    if (!rparen) {
        return parser_sync_expr(p);
    }
    call_expr->expr.fn_call.args = parser_freeze_expr_spill_arr(p, &args);

    call_expr->first = call_expr->expr.fn_call.left_expr->first;
    call_expr->last = rparen;
    return call_expr;
}

ast_expr_t* parser_sync_expr(parser_t* p) {
    token_range_t range = parser_sync(p);
    ast_expr_t* dummy_expr = parser_alloc_expr(p);
    dummy_expr->type = AST_EXPR_INVALID;
    dummy_expr->first = range.first;
    dummy_expr->last = range.last;
    return dummy_expr;
}

ast_expr_t* parse_grouping(parser_t* p) {
    ast_expr_t* grouping = parser_alloc_expr(p);
    grouping->type = AST_EXPR_GROUPING;
    token_t* lparen = parser_eat(p);
    grouping->expr.grouping.left_paren = lparen;
    ast_expr_t* expr = parse_expr(p);
    if (expr->type == AST_EXPR_INVALID) {
        grouping->type = AST_EXPR_INVALID;
    }
    grouping->expr.grouping.expr = expr;
    token_t* rparen = parser_expect_token(p, TOK_RPAREN);
    if (!rparen) {
        return parser_sync_expr(p);
    }

    grouping->expr.grouping.right_paren = rparen;
    grouping->first = lparen;
    grouping->last = grouping->expr.grouping.right_paren;
    return grouping;
}

ast_expr_t* parse_subscript(parser_t* p, ast_expr_t* lhs) {
    ast_expr_t* s = parser_alloc_expr(p);
    s->type = AST_EXPR_SUBSCRIPT;
    s->expr.subscript.lhs = lhs;
    token_t* lbrack = parser_expect_token(p, TOK_LBRACK);
    if (!lbrack) {
        return parser_sync_expr(p);
    }
    s->expr.subscript.subexpr = parse_expr(p);
    token_t* rbrack = parser_expect_token(p, TOK_RBRACK);
    if (!rbrack) {
        return parser_sync_expr(p);
    }
    s->first = s->expr.subscript.lhs->first;
    s->last = rbrack;
    return s;
}

ast_expr_t* parse_expr_type(parser_t* p) {
    ast_expr_t* s = parser_alloc_expr(p);
    s->type = AST_EXPR_TYPE;
    ast_type_t* type = parse_type(p);
    if (type->tag == AST_TYPE_INVALID) {
        return parser_sync_expr(p);
    }
    s->expr.type_expr.type = type;
    s->first = type->first;
    s->last = type->last;
    return s;
}

ast_expr_t* parse_expr_struct_member_init(parser_t* p) {
    ast_expr_t* s = parser_alloc_expr(p);
    s->type = AST_EXPR_STRUCT_MEMBER_INIT;
    if (!parser_expect_token(p, TOK_DOT)) {
        return parser_sync_expr(p);
    }
    token_t* first = parser_prev(p);
    token_t* name = parser_match_token(p, TOK_IDENTIFIER);
    if (!name) {
        compiler_error_list_emplace(p->error_list, parser_peek(p), ERR_EXPECTED_IDENTIFER);
        return parser_sync_expr(p);
    }
    token_t* assign_op = parser_match_token_call(p, &token_is_assignment_init);
    if (!assign_op) {
        compiler_error_list_emplace(p->error_list, parser_peek(p), ERR_EXPECTED_ASSIGNMENT);
        return parser_sync_expr(p);
    }
    s->expr.struct_member_init.id = name;
    s->expr.struct_member_init.assign_op = assign_op;
    s->expr.struct_member_init.value = parse_expr(p);
    // now we should be returning something in the form .foo = some_expr
    s->first = first;
    s->last = parser_prev(p);
    return s;
}

ast_expr_t* parse_expr_struct_init(parser_t* p, ast_expr_t* opt_id_lhs,
                                   ast_slice_of_generic_args_t* gen_args) {
    ast_expr_t* s = parser_alloc_expr(p);
    s->type = AST_EXPR_STRUCT_INIT;
    token_t* first = parser_peek(p);

    if (gen_args) {
        s->expr.struct_init.is_generic = true;
        s->expr.struct_init.generic_args = *gen_args;
    } else {
        s->expr.struct_init.generic_args
            = (ast_slice_of_generic_args_t){.valid = true, .len = 0, .start = NULL};
        s->expr.struct_init.is_generic = false;
    }
    token_ptr_slice_t id = {.start = NULL, .len = 0};
    // if the optional id lhs is NULL, parse an id
    if (!opt_id_lhs) {
        if (parser_peek_match(p, TOK_IDENTIFIER)) {
            opt_id_lhs = parse_id(p);
            // verfify it's 100% an id (matters if passed in)
            if (opt_id_lhs->type != AST_EXPR_ID) {
                compiler_error_list_emplace(p->error_list, parser_prev(p), ERR_EXPECTED_IDENTIFER);
                return parser_sync_expr(p);
            }
        }
    }
    if (opt_id_lhs) {
        id = opt_id_lhs->expr.id.slice;
        s->first = opt_id_lhs->first;
    } else {
        s->first = first;
    }
    // extract the id slice from the id, this should be safe given the above check
    s->expr.struct_init.id = id;
    parser_expect_token(p, TOK_LBRACE);
    s->expr.struct_init.member_inits
        = parse_slice_of_exprs_call(p, TOK_COMMA, TOK_RBRACE, &parse_expr_struct_member_init);
    parser_expect_token(p, TOK_RBRACE);
    s->last = parser_prev(p);
    return s;
}

ast_expr_t* parse_expr_generic_id(parser_t* p, token_ptr_slice_t id_slice,
                                  ast_slice_of_generic_args_t gen_args) {
    ast_expr_t* s = parser_alloc_expr(p);
    if (!id_slice.len) {
        s->type = AST_EXPR_INVALID;
        return s;
    }
    s->type = AST_EXPR_GENERIC_ID;
    s->expr.generic_id.slice = id_slice;
    s->expr.generic_id.args = gen_args;
    s->first = id_slice.start[0]; // fine cuz we previously validated id_slice.len
    s->last = parser_prev(p);
    return s;
}

ast_expr_t* parse_expr_borrow(parser_t* p) {
    ast_expr_t* s = parser_alloc_expr(p);
    s->type = AST_EXPR_BORROW;
    token_t* amper = parser_expect_token(p, TOK_AMPER);
    if (!amper) {
        return parser_sync_expr(p);
    }
    s->expr.borrow.mut = parser_match_token(p, TOK_MUT);
    s->expr.borrow.borrowed = parse_expr(p);
    s->first = amper;
    s->last = parser_prev(p);
    return s;
}

ast_expr_t* parse_expr_variant_decomp(parser_t* p) {
    ast_expr_t* s = parser_alloc_expr(p);
    s->type = AST_EXPR_VARIANT_DECOMP;
    token_t* first = parser_peek(p);
    s->expr.variant_decomp.id = parse_id_token_slice(p, TOK_SCOPE_RES);
    if (parser_match_token(p, TOK_LPAREN)) {
        s->expr.variant_decomp.vars = parse_slice_of_params(p, TOK_COMMA, TOK_RPAREN);
        parser_expect_token(p, TOK_RPAREN);
    } else {
        ast_slice_of_params_t vars = {.start = NULL, .len = 0};
        s->expr.variant_decomp.vars = vars;
    }
    s->first = first;
    s->last = parser_prev(p);
    return s;
}

ast_expr_t* parse_expr_variant_decomp_with_leading_id(parser_t* p, token_ptr_slice_t id) {
    ast_expr_t* e = parser_alloc_expr(p);
    e->type = AST_EXPR_VARIANT_DECOMP;
    token_t* first = parser_peek(p);
    e->expr.variant_decomp.id = id;
    if (parser_match_token(p, TOK_LPAREN)) {
        e->expr.variant_decomp.vars = parse_slice_of_params(p, TOK_COMMA, TOK_RPAREN);
        parser_expect_token(p, TOK_RPAREN);
    } else {
        ast_slice_of_params_t vars = {.start = NULL, .len = 0};
        e->expr.variant_decomp.vars = vars;
    }
    e->first = first;
    e->last = parser_prev(p);
    return e;
}

ast_expr_t* parse_expr_match_pattern(parser_t* p) {
    token_t* el = parser_match_token(p, TOK_ELSE);
    if (el) {
        ast_expr_t* default_expr = parser_alloc_expr(p);
        // nothing to set in the expr union here
        default_expr->type = AST_EXPR_ELSE_MATCH_PATTERN;
        default_expr->first = el;
        default_expr->last = el;
        return default_expr;
    }
    // handle identifier or variant decomp
    token_type_e next_type = parser_peek(p)->type;
    if (next_type == TOK_IDENTIFIER) {
        ast_expr_t* id = parse_id(p);

        next_type = parser_peek(p)->type;

        // parse Foo..Bar(var foo)
        if (next_type == TOK_LPAREN && id->type == AST_EXPR_ID) {
            return parse_expr_variant_decomp_with_leading_id(p, id->expr.id.slice);
        }

        // ban TOK_BAR, `|` since that is used to seperate chained patterns
        parser_mode_e saved = parser_mode(p);
        parser_mode_set(p, PARSER_MODE_BAN_BAR);
        ast_expr_t* expr = parse_expr_prec(p, id, PREC_INIT);
        parser_mode_set(p, saved); // restore
        return expr;
    }

    // ^^^
    parser_mode_e saved = parser_mode(p);
    parser_mode_set(p, PARSER_MODE_BAN_BAR);
    ast_expr_t* expr = parse_expr(p);
    parser_mode_set(p, saved); // restore
    return expr;
}

ast_expr_t* parse_expr_block_call(parser_t* p, ast_stmt_t* (*call)(parser_t*)) {
    ast_expr_t* blk = parser_alloc_expr(p);
    blk->type = AST_EXPR_BLOCK;
    token_t* lbrace = parser_expect_token(p, TOK_LBRACE);
    if (!lbrace) {
        return parser_sync_expr(p);
    }
    blk->expr.block.stmts = parse_slice_of_stmts_call(p, TOK_RBRACE, call);
    parser_expect_token(p, TOK_RBRACE);
    blk->first = lbrace;
    blk->last = parser_prev(p);
    return blk;
}

ast_expr_t* parse_expr_allowing_block_exprs_with_yields(parser_t* p) {
    if (parser_peek_match(p, TOK_LBRACE)) {
        return parse_expr_block_call(p, &parse_stmt_allowing_yield);
    }
    return parse_expr(p);
}

ast_expr_t* parse_expr_allowing_block_exprs_without_yields(parser_t* p) {
    if (parser_peek_match(p, TOK_LBRACE)) {
        return parse_expr_block_call(p, &parse_stmt);
    }
    return parse_expr(p);
}

ast_expr_t* parse_expr_match_branch(parser_t* p) {
    ast_expr_t* branch = parser_alloc_expr(p);
    branch->type = AST_EXPR_MATCH_BRANCH;
    token_t* first = parser_peek(p);
    branch->expr.match_branch.patterns
        = parse_slice_of_exprs_call_call(p, TOK_BAR, token_is_arrow, &parse_expr_match_pattern);
    if (parser_match_token(p, TOK_RARROW)) {
        compiler_error_list_emplace(p->error_list, parser_prev(p),
                                    ERR_RETURN_TYPE_ARROW_NOT_ALLOWED_HERE);
        compiler_error_list_emplace(p->error_list, parser_prev(p), HELP_REPLACE_WITH_EQ_ARROW);
    } else {
        parser_expect_token(p, TOK_EQ_ARROW);
    }
    branch->expr.match_branch.value = parse_expr_allowing_block_exprs_with_yields(p);
    branch->first = first;
    branch->last = parser_prev(p);
    return branch;
}

ast_expr_t* parse_expr_match(parser_t* p) {
    ast_expr_t* sw = parser_alloc_expr(p);
    sw->type = AST_EXPR_MATCH;
    token_t* first = parser_peek(p);
    if (!parser_expect_token(p, TOK_MATCH)) {
        return parser_sync_expr(p);
    }
    // match (<matched_expr>)
    parser_expect_token(p, TOK_LPAREN);
    sw->expr.match_expr.matched = parse_expr(p);
    parser_expect_token(p, TOK_RPAREN);
    // guard safely again swtich(<expr>))))))...
    parser_guard_against_trailing_rparens(p);

    // {....}
    parser_expect_token(p, TOK_LBRACE);
    sw->expr.match_expr.branches
        = parse_slice_of_exprs_call(p, TOK_COMMA, TOK_RBRACE, &parse_expr_match_branch);
    parser_expect_token(p, TOK_RBRACE);
    sw->first = first;
    sw->last = parser_prev(p);
    return sw;
}

ast_expr_t* parse_expr_closure(parser_t* p) {
    ast_expr_t* cl = parser_alloc_expr(p);
    cl->type = AST_EXPR_CLOSURE;
    token_t* first = parser_peek(p);
    // move |...|{...}
    cl->expr.closure.is_move = parser_match_token(p, TOK_MOVE); // match into bool
    if (!parser_match_token(p, TOK_BOOL_OR)) {
        if (!parser_expect_token(p, TOK_BAR)) {
            return parser_sync_expr(p);
        }
        cl->expr.closure.params = parse_slice_of_params(p, TOK_COMMA, TOK_BAR);
        parser_expect_token(p, TOK_BAR);
    } else {
        // we just have ||
        cl->expr.closure.params = (ast_slice_of_params_t){.start = NULL, .len = 0};
    }
    cl->expr.closure.return_type = NULL;
    cl->expr.closure.has_explicit_return_type = false;
    // allow explicit return type
    if (parser_match_token(p, TOK_RARROW)) {
        ast_type_t* type = parse_type(p);
        if (type->tag != AST_TYPE_INVALID) {
            cl->expr.closure.return_type = type;
            cl->expr.closure.has_explicit_return_type = true;
        }
    }
    // allow '|thingie| => thing' syntax
    if (parser_match_token(p, TOK_EQ_ARROW)) {
        cl->expr.closure.body = parse_expr_allowing_block_exprs_without_yields(p);
    } else {
        // {....}
        cl->expr.closure.body = parse_expr_block_call(p, &parse_stmt);
    }
    cl->first = first;
    cl->last = parser_prev(p);
    return cl;
}

ast_expr_t* parse_expr_list_literal(parser_t* p) {
    ast_expr_t* cl = parser_alloc_expr(p);
    cl->type = AST_EXPR_LIST_LITERAL;
    token_t* first = parser_peek(p);
    if (!parser_expect_token(p, TOK_LBRACK)) {
        return parser_sync_expr(p);
    }
    cl->expr.list_literal.slice = parse_slice_of_exprs_call(p, TOK_COMMA, TOK_RBRACK, &parse_expr);
    parser_expect_token(p, TOK_RBRACK);
    cl->first = first;
    cl->last = parser_prev(p);
    return cl;
}

ast_expr_t* parse_expr_before_opening_brace(parser_t* p) {
    parser_mode_e saved = parser_mode(p);
    parser_mode_set(p, PARSER_MODE_BAN_STRUCT_INIT);
    ast_expr_t* expr = parse_expr(p);
    parser_mode_set(p, saved);
    return expr;
}

ast_expr_t* parse_expr_contract(parser_t* p) {
    ast_expr_t* id = parse_id(p);
    if (id->type == AST_EXPR_INVALID) {
        return id;
    }

    if (parser_peek_match(p, TOK_GENERIC_SEP) || parser_peek_match(p, TOK_LT)) {
        parser_mode_e saved = parser_mode(p);
        parser_mode_set(p, PARSER_MODE_BAN_ANGLE_BRACKETS_IN_EXPRS);
        ast_slice_of_generic_args_t args = parse_slice_of_generic_args(p);
        parser_mode_set(p, saved);
        return parse_expr_generic_id(p, id->expr.id.slice, args);
    }
    return id;
}

ast_slice_of_exprs_t parse_has_contracts_clause(parser_t* p) {
    spill_arr_ptr_t ids;
    spill_arr_ptr_init(&ids);
    parser_expect_token(p, TOK_HAS);
    token_t* l_paren = parser_match_token(p, TOK_LPAREN);
    if (!parser_peek_match(p, TOK_RPAREN)) {
        do {
            *((ast_expr_t**)spill_arr_ptr_emplace(&ids)) = parse_expr_contract(p);
        } while (parser_match_token(p, TOK_PLUS));
    }
    if (l_paren) {
        parser_expect_token(p, TOK_RPAREN);
    }
    return parser_freeze_expr_spill_arr(p, &ids);
}

ast_expr_t* parse_expr_ternary_if(parser_t* p, ast_expr_t* lhs) {
    // we're looking for <expr> if compt? <condition> else <expr>
    ast_expr_t* tif = parser_alloc_expr(p);
    tif->type = AST_EXPR_TERNARY_IF;
    token_t* first = lhs->first;
    tif->expr.ternary_if.happy_expr = lhs;
    if (!parser_expect_token(p, TOK_IF)) {
        return parser_sync_expr(p);
    }
    tif->expr.ternary_if.compt = parser_match_token(p, TOK_COMPT);
    ast_expr_t* cond = parse_expr(p);
    if (cond->type == AST_EXPR_INVALID) {
        return parser_sync_expr(p);
    }
    tif->expr.ternary_if.condition = cond;
    if (!parser_expect_token(p, TOK_ELSE)) {
        return parser_sync_expr(p);
    }
    tif->expr.ternary_if.else_expr = parse_expr(p);
    tif->first = first;
    tif->last = parser_prev(p);
    return tif;
}

ast_expr_t* parse_expr_compt(parser_t* p) {
    ast_expr_t* ex = parser_alloc_expr(p);

    ex->type = AST_EXPR_COMPT;

    token_t* compt_tkn = parser_expect_token(p, TOK_COMPT);

    if (!compt_tkn) {
        return parser_sync_expr(p);
    }

    token_t* lparen = parser_match_token(p, TOK_LPAREN);
    ex->expr.compt_expr.inner = parse_expr(p);

    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }

    ex->first = compt_tkn;
    ex->last = parser_prev(p);
    return ex;
}

bool try_parse_generic_args(parser_t* p, ast_slice_of_generic_args_t* args) {
    if (parser_peek_match(p, TOK_GENERIC_SEP)) {
        parser_mode_e saved = parser_mode(p);
        parser_mode_set(p, PARSER_MODE_BAN_ANGLE_BRACKETS_IN_EXPRS);
        *args = parse_slice_of_generic_args(p);
        parser_mode_set(p, saved);
        return true;
    }
    return false;
}
