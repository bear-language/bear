//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/parser/parse_type.h"
#include "compiler/ast/expr.h"
#include "compiler/ast/type.h"
#include "compiler/diagnostics/error_codes.h"
#include "compiler/diagnostics/error_list.h"
#include "compiler/parser/parse_expr.h"
#include "compiler/parser/parser.h"
#include "compiler/parser/token_eaters.h"
#include "compiler/token.h"
#include "utils/arena.h"
#include "utils/spill_arr.h"
#include <assert.h>
#include <stdbool.h>

ast_type_t* parser_alloc_type(parser_t* p) { return arena_alloc(p->arena, sizeof(ast_type_t)); }

ast_type_t* parser_sync_type(parser_t* p) {
    token_range_t range = parser_sync(p);
    ast_type_t* dummy_type = parser_alloc_type(p);
    dummy_type->tag = AST_TYPE_INVALID;
    dummy_type->first = range.first;
    dummy_type->last = range.last;
    return dummy_type;
}

static ast_type_t* parser_invalid_type_and_toss_next_tkn(parser_t* p) {
    token_t* tkn = parser_peek(p);
    parser_toss(p);
    ast_type_t* dummy_type = parser_alloc_type(p);
    dummy_type->tag = AST_TYPE_INVALID;
    dummy_type->first = tkn;
    dummy_type->last = tkn;
    return dummy_type;
}

ast_slice_of_params_t parser_freeze_params_spill_arr(parser_t* p, spill_arr_ptr_t* sarr) {
    ast_slice_of_params_t slice = {
        .start = (ast_param_t**)arena_alloc(p->arena, sarr->size * sizeof(ast_param_t*)),
        .len = sarr->size,
    };
    spill_arr_ptr_flat_copy((void**)slice.start, sarr);
    spill_arr_ptr_destroy(sarr);
    return slice;
}

ast_slice_of_params_t parse_slice_of_params(parser_t* p, token_type_e divider,
                                            token_type_e terminator) {
    spill_arr_ptr_t sarr;
    spill_arr_ptr_init(&sarr);

    while (!parser_peek_match(p, terminator) && !parser_eof(p)) {
        spill_arr_ptr_push(&sarr, parse_param(p));
        if (!parser_peek_match(p, terminator)) {
            parser_expect_token(p, divider);
        }
    }

    return parser_freeze_params_spill_arr(p, &sarr);
}

static ast_slice_of_types_t parser_freeze_types_spill_arr(parser_t* p, spill_arr_ptr_t* sarr) {
    ast_slice_of_types_t slice = {
        .start = (ast_type_t**)arena_alloc(p->arena, sarr->size * sizeof(ast_type_t*)),
        .len = sarr->size,
    };
    spill_arr_ptr_flat_copy((void**)slice.start, sarr);
    spill_arr_ptr_destroy(sarr);
    return slice;
}

ast_slice_of_types_t parse_slice_of_types(parser_t* p, token_type_e divider,
                                          token_type_e terminator) {
    spill_arr_ptr_t sarr;
    spill_arr_ptr_init(&sarr);

    while (!parser_peek_match(p, terminator) && !parser_eof(p)) {
        spill_arr_ptr_push(&sarr, parse_type(p));
        if (!parser_peek_match(p, terminator)) {
            parser_expect_token(p, divider);
        }
    }

    return parser_freeze_types_spill_arr(p, &sarr);
}

static ast_type_t* parse_type_base_impl(parser_t* p, bool pre_mut, token_ptr_slice_t id_slice) {
    ast_type_t* base = parser_alloc_type(p);
    // self-referential for canonical_base, although this shouldn't be an issue
    base->canonical_base = base;
    // handle pre-mut
    token_t* pre_mut_tkn = NULL;
    if (pre_mut) {
        pre_mut_tkn = parser_eat(p);
        // handle redundant leading muts
        while (parser_match_token(p, TOK_MUT)) {
            compiler_error_list_emplace(p->error_list, parser_prev(p), ERR_REDUNDANT_MUT);
            compiler_error_list_emplace(p->error_list, parser_prev(p), HELP_REMOVE);
        }
        // parse base type
        base->type.base.id = parse_id_token_slice(p, TOK_SCOPE_RES);
    } else {
        // base id must already be parsed and passed in
        base->type.base.id = id_slice;
    }
    // handle post-mut
    bool post_mut = parser_match_token(p, TOK_MUT); // optionally consume post-mut as bool

    base->type.base.mut = pre_mut || post_mut;

    // handle tkn ordering due to mut order
    if (!pre_mut) {
        base->first = base->type.base.id.start[0];
    } else if (post_mut && pre_mut) {
        assert(pre_mut_tkn != NULL && "[parse_type_base] mut_tkn must not be NULL");
        base->first = pre_mut_tkn;
        compiler_error_list_emplace(p->error_list, parser_prev(p), ERR_REDUNDANT_MUT);
        compiler_error_list_emplace(p->error_list, parser_prev(p), HELP_REMOVE);
    } else {
        base->first = pre_mut_tkn;
    }
    base->last = parser_prev(p);
    base->tag = AST_TYPE_BASE;
    return base;
}

ast_type_t* parse_type_base_with_leading_id(parser_t* p, token_ptr_slice_t id_slice) {
    return parse_type_base_impl(p, false, id_slice);
}

ast_type_t* parse_type_base_with_leading_mut(parser_t* p) {
    token_ptr_slice_t dummy = {.start = NULL, .len = 0};
    return parse_type_base_impl(p, true, dummy);
}

static void parser_shed_rparens(parser_t* p, int* cnt) {
    // shed right parens
    for (int i = 0; i < *cnt; i++) {
        parser_expect_token(p, TOK_RPAREN);
        (*cnt)--;
    }
}

static ast_type_t* parse_type_impl(parser_t* p, token_ptr_slice_t leading_id, bool has_leading_id) {
    int paren_count = 0;
    while (parser_match_token(p, TOK_LPAREN)) {
        ++paren_count;
    }
    token_t* first_tkn = parser_peek(p);
    token_type_e first_type = first_tkn->type;

    ast_type_t* inner = NULL;

    // parse base types
    if (has_leading_id) {
        inner = parse_type_base_with_leading_id(p, leading_id); // pre-mut = false
    } else if (first_type == TOK_MUT) {
        token_type_e t1 = parser_peek_n(p, 1)->type;
        token_type_e t2 = parser_peek_n(p, 2)->type;
        if (t1 == TOK_LBRACK && t2 == TOK_AMPER) {
            compiler_error_list_emplace(p->error_list, first_tkn, ERR_NO_LEADING_MUT_FOR_SLICES);
            return parser_sync_type(p);
        }
        if (t1 == TOK_LBRACK) {
            compiler_error_list_emplace(p->error_list, first_tkn, ERR_MUT_CANNOT_BIND_TO_ARRAYS);
            return parser_sync_type(p);
        }
        // try function ptr (effectively a special case)
        if (t1 == TOK_STAR && t2 == TOK_FN) {
            inner = parse_type_fn_ptr(p);
        } else {
            inner = parse_type_base_with_leading_mut(p);
        }
    } else if (first_type == TOK_STAR && parser_peek_n(p, 1)->type == TOK_FN) {
        inner = parse_type_fn_ptr(p);
    }

    // parse opening guys
    else if (first_type == TOK_LBRACK && parser_peek_n(p, 1)->type == TOK_AMPER) {
        inner = parse_type_slice(p);
    } else if (first_type == TOK_LBRACK) {
        inner = parse_type_arr(p);
    }

    // parse type modifiers
    else if (token_is_ref_or_ptr(parser_peek(p)->type)) {
        inner = parse_type_ref_ptr(p);
    }

    else if (parser_peek_match(p, TOK_TYPEOF)) {
        inner = parse_type_of(p);
    }

    else if (parser_peek_match(p, TOK_DECAY)) {
        inner = parse_type_decay(p);
    }

    else if (parser_peek_match(p, TOK_BOOL_AND)) {
        compiler_error_list_emplace(p->error_list, parser_peek(p), ERR_MULTILEVEL_REF);
        inner = parser_invalid_type_and_toss_next_tkn(p);
    }

    else if (token_is_builtin_type_or_id(first_type)) {
        leading_id = parse_id_token_slice(p, TOK_SCOPE_RES);
        inner = parse_type_base_with_leading_id(p, leading_id);
    }

    parser_shed_rparens(p, &paren_count);

    if (token_is_generic_opener(parser_peek(p)->type)) {
        inner = parse_type_generic(p, inner);
    }

    parser_shed_rparens(p, &paren_count);

    if (inner) {
        return inner;
    }
    compiler_error_list_emplace(p->error_list, first_tkn, ERR_EXPECTED_TYPE);
    return parser_sync_type(p);
}

ast_type_t* parse_type_with_leading_id(parser_t* p, token_ptr_slice_t id_slice) {
    return parse_type_impl(p, id_slice, true);
}

ast_type_t* parse_type(parser_t* p) {
    token_ptr_slice_t dummy = {.start = NULL, .len = 0};
    return parse_type_impl(p, dummy, false); //! has_leading_id
}

ast_type_t* parse_type_ref_ptr(parser_t* p) {
    ast_type_t* outer = parser_alloc_type(p);
    token_t* modifier = parser_eat(p); // definitely fine because we know to be in this func

    outer->type.ptr_ref.modifier = modifier;
    outer->type.ptr_ref.mut = parser_match_token(p, TOK_MUT);

    ast_type_t* inner = parse_type(p);

    outer->canonical_base = inner->canonical_base;
    outer->type.ptr_ref.inner = inner;
    outer->tag = AST_TYPE_REF_PTR;
    outer->first = modifier; // always first since we have *ty or &ty
    outer->last = parser_prev(p);
    if (outer->type.ptr_ref.inner->tag == AST_TYPE_REF_PTR
        && outer->type.ptr_ref.inner->type.ptr_ref.modifier->type == TOK_AMPER) {
        compiler_error_list_emplace(p->error_list, outer->type.ptr_ref.modifier,
                                    ERR_MULTILEVEL_REF);
        outer->type.ptr_ref.inner->tag = AST_TYPE_INVALID;
    }
    return outer;
}

ast_type_t* parse_type_of(parser_t* p) {
    ast_type_t* outer = parser_alloc_type(p);
    token_t* to_tok = parser_expect_token(p, TOK_TYPEOF);
    if (!to_tok) {
        return parser_sync_type(p);
    }
    outer->tag = AST_TYPE_TYPEOF;
    token_t* lparen = parser_match_token(p, TOK_LPAREN);
    ast_expr_t* inner = parse_expr(p);
    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }
    outer->type.type_of.of_expr = inner;
    outer->type.type_of.mut = parser_match_token(p, TOK_MUT);
    outer->first = to_tok;
    outer->last = parser_prev(p);
    return outer;
}

ast_type_t* parse_type_decay(parser_t* p) {
    ast_type_t* outer = parser_alloc_type(p);
    outer->tag = AST_TYPE_DECAY;
    token_t* d_tok = parser_expect_token(p, TOK_DECAY);
    if (!d_tok) {
        return parser_sync_type(p);
    }
    token_t* lparen = parser_match_token(p, TOK_LPAREN);
    ast_type_t* inner = parse_type(p);
    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }
    outer->type.decay.inner = inner;
    bool valid = true;
    if (outer->type.decay.inner->tag == AST_TYPE_INVALID) {
        valid = false;
    }
    outer->canonical_base = inner->canonical_base;
    outer->first = d_tok;
    outer->last = parser_prev(p);
    if (!valid) {
        outer->tag = AST_TYPE_INVALID;
    }
    return outer;
}

ast_type_t* parse_type_arr(parser_t* p) {
    ast_type_t* outer = parser_alloc_type(p);
    outer->tag = AST_TYPE_ARR;

    // handle [size_expr]
    token_t* lbrack = parser_expect_token(p, TOK_LBRACK);
    if (!lbrack) {
        return parser_sync_type(p);
    }
    ast_expr_t* size_expr = parse_expr(p);
    if (size_expr->type == AST_EXPR_INVALID) {
        return parser_sync_type(p);
    }
    outer->type.arr.size_expr = size_expr;
    token_t* rbrack = parser_expect_token(p, TOK_RBRACK);
    if (!rbrack) {
        return parser_sync_type(p);
    }
    ast_type_t* inner = parse_type(p);
    if (inner->tag == AST_TYPE_INVALID) {
        return parser_sync_type(p);
    }
    outer->type.arr.inner = inner;
    // handle canonical base
    outer->canonical_base = inner->canonical_base;
    outer->first = lbrack;
    outer->last = inner->last;
    return outer;
}

ast_type_t* parse_type_slice(parser_t* p) {
    ast_type_t* outer = parser_alloc_type(p);
    outer->tag = AST_TYPE_SLICE;
    // handle [&]
    // should be fine because we know to be in this func
    token_t* lbrack = parser_expect_token(p, TOK_LBRACK);
    if (!lbrack) {
        return parser_sync_type(p);
    }
    if (!parser_expect_token(p, TOK_AMPER)) {
        return parser_sync_type(p);
    }
    outer->type.slice.mut = parser_match_token(p, TOK_MUT); // match into bool
    if (!parser_expect_token(p, TOK_RBRACK)) {
        return parser_sync_type(p);
    }
    ast_type_t* inner = parse_type(p);
    if (inner->tag == AST_TYPE_INVALID) {
        return parser_sync_type(p);
    }
    // handle canonical base
    outer->type.slice.inner = inner;
    outer->canonical_base = inner->canonical_base;
    outer->first = lbrack;
    outer->last = inner->last;
    return outer;
}

ast_generic_arg_t* parse_generic_arg(parser_t* p) {
    ast_generic_arg_t* arg = arena_alloc(p->arena, sizeof(ast_generic_arg_t));
    token_type_e next_type = parser_peek(p)->type;
    ast_expr_t* expr = NULL;
    ast_type_t* type = NULL;

    bool is_type = false;
    // parse as type or id ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if (token_is_non_id_type_idicator(parser_peek(p)->type)) {
        is_type = true;
        type = parse_type(p);
    } else if (token_is_builtin_type_or_id(parser_peek(p)->type)) {
        token_ptr_slice_t leading_id = parse_id_token_slice(p, TOK_SCOPE_RES);
        next_type = parser_peek(p)->type;
        if (token_is_posttype_indicator(next_type)) {
            is_type = true;
            type = parse_type_with_leading_id(p, leading_id);
        } else {
            is_type = false;
            expr = parse_expr_from_id_slice(p, leading_id);
        }
    } else {
        // parse things that have a leading expr
        is_type = false;
        expr = parse_expr(p);
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    // set according to is_type
    arg->valid = true;
    if (is_type) {
        if (type->tag == AST_TYPE_INVALID) {
            arg->valid = false;
        }
        arg->tag = AST_GENERIC_ARG_TYPE;
        assert(type != NULL && "[parse_generic_arg] arg type must not be NULL");
        arg->arg.type = type;
    } else {
        // handle invalid leading expr
        if (expr->type == AST_EXPR_INVALID) {
            arg->valid = false;
        }
        arg->tag = AST_GENERIC_ARG_EXPR;
        assert(expr != NULL && "[parse_generic_arg] arg expr must not be NULL");
        arg->arg.expr = expr;
    }
    return arg;
}

ast_slice_of_generic_args_t parser_freeze_generic_arg_spill_arr(parser_t* p,
                                                                spill_arr_ptr_t* sarr) {
    ast_slice_of_generic_args_t slice = {
        .start
        = (ast_generic_arg_t**)arena_alloc(p->arena, sarr->size * sizeof(ast_generic_arg_t*)),
        .len = sarr->size,
    };
    spill_arr_ptr_flat_copy((void**)slice.start, sarr);
    spill_arr_ptr_destroy(sarr);
    return slice;
}

ast_slice_of_generic_args_t parse_slice_of_generic_args(parser_t* p) {

    spill_arr_ptr_t sarr;
    spill_arr_ptr_init(&sarr);

    token_t* opener
        = parser_expect_token_call(p, &token_is_generic_opener, ERR_EXPECT_GENERIC_OPENER);
    // when just, ::, just expect one thing, like box::thing
    ast_generic_arg_t* arg = NULL;
    bool valid = false;
    if (opener->type == TOK_GENERIC_SEP && !parser_match_token(p, TOK_LT)) {
        arg = parse_generic_arg(p);
        valid |= arg->valid;
        spill_arr_ptr_push(&sarr, arg);
    }
    // otherwise expect foo::<garg1, garg2> or foo<garg1, garg2>
    else {
        // ensure monkey business is banned
        parser_mode_e saved_mode = parser_mode(p);
        parser_mode_set(p, PARSER_MODE_BAN_ANGLE_BRACKETS_IN_EXPRS);

        while (!parser_peek_generic_closing_delims(p) && !parser_eof(p)) {
            arg = parse_generic_arg(p);
            valid |= arg->valid;
            spill_arr_ptr_push(&sarr, arg);
            if (!parser_peek_generic_closing_delims(p) && !parser_peek_match(p, TOK_GENERIC_SEP)) {
                parser_expect_token(p, TOK_COMMA);
            }
        }
        token_t* gt = parser_expect_generic_closing_delim(p);
        valid = gt; // false if gt == NULL

        // restore monkey business
        parser_mode_set(p, saved_mode);
    }
    ast_slice_of_generic_args_t args = parser_freeze_generic_arg_spill_arr(p, &sarr);
    if (!args.len) {
        compiler_error_list_emplace(p->error_list, parser_prev(p), GENERIC_ARGS_CANNOT_BE_EMPTY);
    }
    args.valid = valid;
    return args;
}

ast_type_t* parse_type_generic(parser_t* p, ast_type_t* inner) {
    ast_type_t* outer = parser_alloc_type(p);
    outer->tag = AST_TYPE_GENERIC;

    // handle canonical base
    if (!(inner->tag == AST_TYPE_BASE)) {
        compiler_error_list_emplace(p->error_list, inner->last, ERR_EXPECTED_BASE_TYPE_IN_GENERIC);
        return parser_sync_type(p);
    }
    outer->type.generic.inner = inner;
    outer->canonical_base = inner->canonical_base;
    parser_mode_e saved = parser_mode(p);
    parser_mode_set(
        p, PARSER_MODE_BAN_ANGLE_BRACKETS_IN_EXPRS); // cleaner template parsing from < and > issues
    ast_slice_of_generic_args_t args = parse_slice_of_generic_args(p);
    parser_mode_set(p, saved); // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    if (parser_peek_match(p, TOK_MUT) && inner->tag == AST_TYPE_BASE) {
        inner->type.base.mut = true; // force inner to be mut
    }
    if (!args.valid) {
        return parser_sync_type(p);
    }
    outer->type.generic.generic_args = args;
    outer->first = inner->first;
    outer->last = parser_prev(p);
    return outer;
}

ast_type_t* parse_type_fn_ptr(parser_t* p) {
    ast_type_t* fnp = parser_alloc_type(p);
    fnp->tag = AST_TYPE_FN_PTR;
    token_t* first = parser_peek(p);
    bool leading_mut = parser_match_token(p, TOK_MUT);
    fnp->type.fn_ptr.mut = leading_mut;
    if (!parser_expect_token(p, TOK_STAR)) {
        return parser_sync_type(p);
    }
    if (!parser_expect_token(p, TOK_FN)) {
        return parser_sync_type(p);
    }
    if (!leading_mut) {
        fnp->type.fn_ptr.mut = parser_match_token(p, TOK_MUT);
    }
    // shed redundant muts
    while (parser_match_token(p, TOK_MUT)) {
        compiler_error_list_emplace(p->error_list, parser_prev(p), ERR_REDUNDANT_MUT);
        compiler_error_list_emplace(p->error_list, parser_prev(p), HELP_REMOVE);
    }

    if (!parser_expect_token(p, TOK_LPAREN)) {
        return parser_sync_type(p);
    }
    ast_slice_of_types_t params = parse_slice_of_types(p, TOK_COMMA, TOK_RPAREN);
    if (!parser_expect_token(p, TOK_RPAREN)) {
        return parser_sync_type(p);
    }
    fnp->type.fn_ptr.return_type = NULL;
    if (parser_match_token(p, TOK_RARROW)) {
        token_t* lparen = parser_match_token(p, TOK_LPAREN);
        fnp->type.fn_ptr.return_type = parse_type(p);
        if (lparen) {
            parser_expect_token(p, TOK_RPAREN);
        }
    }
    fnp->type.fn_ptr.param_types = params;
    fnp->canonical_base = fnp;
    fnp->first = first;
    fnp->last = parser_prev(p);
    return fnp;
}
