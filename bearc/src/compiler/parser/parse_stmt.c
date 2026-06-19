//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/parser/parse_stmt.h"
#include "compiler/ast/expr.h"
#include "compiler/ast/stmt.h"
#include "compiler/ast/stmt_slice.h"
#include "compiler/ast/type.h"
#include "compiler/diagnostics/error_codes.h"
#include "compiler/diagnostics/error_list.h"
#include "compiler/parser/parse_expr.h"
#include "compiler/parser/parse_type.h"
#include "compiler/parser/parser.h"
#include "compiler/parser/token_eaters.h"
#include "compiler/token.h"
#include "utils/arena.h"
#include "utils/spill_arr.h"
#include <stdbool.h>
#include <string.h>

ast_stmt_t* parse_file(parser_t* p, const char* file_name) {
    ast_stmt_t* file = parser_alloc_stmt(p);
    file->type = AST_STMT_FILE;
    file->stmt.file.file_name = file_name;
    file->stmt.file.stmts = parse_slice_of_stmts_call(p, TOK_EOF, parse_stmt_top_level_decl);
    if (file->stmt.file.stmts.len != 0) {
        file->first = file->stmt.file.stmts.start[0]->first;
        file->last = file->stmt.file.stmts.start[file->stmt.file.stmts.len - 1]->last;
    } else {
        // since eating never runs past eof, this is safe
        file->last = parser_eat(p);
        file->first = parser_eat(p);
    }
    return file;
}

ast_slice_of_stmts_t parse_slice_of_stmts_call(parser_t* p, token_type_e until_tkn,
                                               ast_stmt_t* (*call)(parser_t*)) {
    spill_arr_ptr_t sarr;
    spill_arr_ptr_init(&sarr);

    while (!parser_peek_match(p, until_tkn) && !parser_eof(p) // while !eof (edge-case handling)
    ) {
        spill_arr_ptr_push(&sarr, call(p));
    }

    return parser_freeze_stmt_spill_arr(p, &sarr);
}

ast_slice_of_stmts_t parse_slice_of_stmts(parser_t* p, token_type_e until_tkn) {
    return parse_slice_of_stmts_call(p, until_tkn, &parse_stmt);
}

ast_slice_of_stmts_t parse_slice_of_decls(parser_t* p, token_type_e until_tkn) {
    return parse_slice_of_stmts_call(p, until_tkn, &parse_stmt_decl);
}

ast_slice_of_stmts_t parser_freeze_stmt_spill_arr(parser_t* p, spill_arr_ptr_t* sarr) {
    ast_slice_of_stmts_t slice = {
        .start = (ast_stmt_t**)arena_alloc(p->arena, sarr->size * sizeof(ast_stmt_t*)),
        .len = sarr->size,
    };
    spill_arr_ptr_flat_copy((void**)slice.start, sarr);
    spill_arr_ptr_destroy(sarr);
    return slice;
}

ast_stmt_t* parser_alloc_stmt(parser_t* p) { return arena_alloc(p->arena, sizeof(ast_stmt_t)); }

static ast_stmt_t* parser_sync_stmt(parser_t* p) {
    token_range_t range = parser_sync_call(p, &token_is_syncable_stmt_delim);
    ast_stmt_t* dummy_stmt = parser_alloc_stmt(p);
    dummy_stmt->type = AST_STMT_INVALID;
    dummy_stmt->first = range.first;
    dummy_stmt->last = range.last;
    return dummy_stmt;
}

static ast_stmt_t* parser_invalid_stmt(parser_t* p, token_t* first, token_t* last) {
    ast_stmt_t* dummy_stmt = parser_alloc_stmt(p);
    dummy_stmt->type = AST_STMT_INVALID;
    dummy_stmt->first = first;
    dummy_stmt->last = last;
    return dummy_stmt;
}

ast_stmt_t* parser_sync_stmt_call(parser_t* p, bool (*call)(token_type_e)) {
    token_range_t range = parser_sync_call(p, call);
    ast_stmt_t* dummy_stmt = parser_alloc_stmt(p);
    dummy_stmt->type = AST_STMT_INVALID;
    dummy_stmt->first = range.first;
    dummy_stmt->last = range.last;
    return dummy_stmt;
}

static ast_stmt_t* parser_sync_stmt_until(parser_t* p, token_type_e tok_type) {
    token_range_t range = parser_sync_until(p, tok_type);
    ast_stmt_t* dummy_stmt = parser_alloc_stmt(p);
    dummy_stmt->type = AST_STMT_INVALID;
    dummy_stmt->first = range.first;
    dummy_stmt->last = range.last;
    return dummy_stmt;
}

ast_stmt_t* parse_stmt(parser_t* p) {
    token_t* first_tkn = parser_peek(p);
    token_type_e next_type = first_tkn->type;

    // parse things that lead with a token (easier)
    if (next_type == TOK_LBRACE) {
        return parse_stmt_block(p);
    }

    if (next_type == TOK_IF) {
        return parse_stmt_if(p);
    }

    if (next_type == TOK_WHILE) {
        return parse_stmt_while(p);
    }

    if (next_type == TOK_FOR) {
        return parse_stmt_for(p);
    }

    if (next_type == TOK_BREAK) {
        if (parser_mode(p) != PARSER_MODE_IN_LOOP) {
            compiler_error_list_emplace(p->error_list, first_tkn, ERR_BREAK_STMT_OUTSIDE_OF_LOOP);
            return parser_sync_stmt_until(p, TOK_SEMICOLON);
        }
        return parse_stmt_break(p);
    }

    if (next_type == TOK_CONTINUE) {
        if (parser_mode(p) != PARSER_MODE_IN_LOOP) {
            compiler_error_list_emplace(p->error_list, first_tkn,
                                        ERR_CONTINUE_STMT_OUTSIDE_OF_LOOP);
            return parser_sync_stmt_until(p, TOK_SEMICOLON);
        }
        return parse_stmt_continue(p);
    }

    // handle decls with leading mut or brackets (slices and arrays)
    if (token_is_non_id_type_idicator(next_type)) {
        if (next_type == TOK_MUT) {
            return parse_var_decl_from_id_or_mut(p, NULL, true); // leading_mut = true
        }
        return parse_var_decl_from_id_or_mut(p, NULL, false);
    }
    // handle fn ptr decls
    if (next_type == TOK_STAR && parser_peek_n(p, 1)->type == TOK_FN) {
        return parse_var_decl_from_id_or_mut(p, NULL, false);
    }

    if (next_type == TOK_RETURN) {
        return parse_stmt_return(p);
    }

    if (next_type == TOK_SEMICOLON) {
        return parse_stmt_empty(p);
    }

    if (next_type == TOK_COMPT) {
        return parse_stmt_compt_modifier(p, &parse_var_decl);
    }

    if (next_type == TOK_STATIC) {
        return parse_stmt_static_modifier(p, &parse_var_decl);
    }

    if (next_type == TOK_USE) {
        return parse_stmt_use(p);
    }

    if (next_type == TOK_DEFTYPE) {
        return parse_stmt_deftype(p);
    }

    // allow parsing of (type) name;
    if (next_type == TOK_LPAREN) {
        size_t cnt = 0;
        while (parser_peek_n(p, cnt)->type == TOK_LPAREN) {
            ++cnt;
        }
        if (token_is_non_id_type_idicator(parser_peek_n(p, cnt)->type)) {
            return parse_var_decl(p);
        }
    }

    // vis modifers are illegal on plain statements
    if (token_is_visibility_modifier(next_type)) {
        parser_shed_visibility_qualis_with_error(p);
    }

    ast_expr_t* leading_expr = NULL;
    // parse things with a leading id (varname or type)
    if (token_is_builtin_type_or_id(parser_peek(p)->type)) {

        token_ptr_slice_t leading_id = parse_id_token_slice(p, TOK_SCOPE_RES);
        next_type = parser_peek(p)->type;
        // parse as decl is the id is followed by a var name, or other symbol indicative of a type
        if (token_is_posttype_indicator(next_type)) {
            return parse_var_decl_from_id_or_mut(p, &leading_id, NULL);
        }
        // parse as regular expression
        leading_expr = parse_expr_from_id_slice(p, leading_id);
    } else {
        // parse things that have a leading expr
        leading_expr = parse_expr(p);
    }

    // handle invalid leading expr
    if (leading_expr->type == AST_EXPR_INVALID) {
        return parser_sync_stmt(p);
    }
    // else interpret as expression-statement
    return parse_stmt_expr(p, leading_expr);
}

ast_stmt_t* parse_stmt_expr(parser_t* p, ast_expr_t* expr) {
    ast_stmt_t* stmt = parser_alloc_stmt(p);
    stmt->stmt.stmt_expr.expr = expr;
    parser_expect_token(p, TOK_SEMICOLON);
    stmt->type = AST_STMT_EXPR;
    stmt->first = stmt->stmt.stmt_expr.expr->first;
    stmt->last = parser_prev(p);
    return stmt;
}

ast_stmt_t* parse_stmt_block(parser_t* p) {
    ast_stmt_t* stmt = parser_alloc_stmt(p);
    stmt->type = AST_STMT_BLOCK;
    token_t* lbrace = parser_expect_token(p, TOK_LBRACE);
    if (!lbrace) {
        return parser_sync_stmt(p);
    }
    stmt->stmt.block.stmts = parse_slice_of_stmts(p, TOK_RBRACE);
    token_t* rbrace = parser_expect_token(p, TOK_RBRACE);
    if (!rbrace) {
        return parser_sync_stmt(p);
    }
    stmt->first = lbrace;
    stmt->last = rbrace;
    return stmt;
}

ast_stmt_t* parse_var_decl(parser_t* p) {
    if (parser_peek_match(p, TOK_STATIC)) {
        return parse_stmt_static_modifier(p, &parse_var_decl);
    }
    if (parser_peek_match(p, TOK_COMPT)) {
        return parse_stmt_compt_modifier(p, &parse_var_decl);
    }
    if (parser_peek_match(p, TOK_ALIGNAS)) {
        return parse_stmt_alignas_modifier(p, &parse_var_decl);
    }
    return parse_var_decl_from_id_or_mut(p, NULL, false);
}

ast_stmt_t* parse_var_decl_from_id_or_mut(parser_t* p, token_ptr_slice_t* opt_id_slice,
                                          bool leading_mut) {
    ast_stmt_t* stmt = parser_alloc_stmt(p);

    ast_type_t* type;
    if (leading_mut || !opt_id_slice) {
        type = parse_type(p);
    } else if (opt_id_slice) {
        type = parse_type_with_leading_id(p, *opt_id_slice);
    } else {
        compiler_error_list_emplace(p->error_list, parser_prev(p), ERR_EXPECTED_STATEMENT);
        return parser_sync_stmt(p);
    }

    token_t* name = parse_var_name(p);
    if (!name) {
        return parser_sync_stmt(p);
    }
    token_t* next_tkn = parser_peek(p);
    token_type_e next_type = next_tkn->type;

    if (token_is_assignment_init(next_type)) {
        stmt->type = AST_STMT_VAR_INIT_DECL;
        stmt->stmt.var_init_decl.type = type;
        stmt->stmt.var_init_decl.name = name;
        // we already know next type is an assignment init token
        stmt->stmt.var_init_decl.assign_op = parser_eat(p);
        stmt->stmt.var_init_decl.rhs = parse_expr(p);
        token_t* term = parser_expect_token(p, TOK_SEMICOLON);
        if (!term) {
            return parser_sync_stmt(p);
        }
    } else if (next_type == TOK_SEMICOLON) {
        stmt->type = AST_STMT_VAR_DECL;
        stmt->stmt.var_decl.type = type;
        stmt->stmt.var_decl.name = name;
        token_t* term = parser_expect_token(p, TOK_SEMICOLON);
        if (!term) {
            return parser_sync_stmt(p);
        }
    } else {
        compiler_error_list_emplace(p->error_list, next_tkn, ERR_INCOMPLETE_VAR_DECLARATION);
        return parser_sync_stmt(p);
    }

    stmt->first = type->first;
    stmt->last = parser_prev(p);
    return stmt;
}

ast_stmt_t* parse_fn_decl(parser_t* p) {
    bool cooked = false;
    ast_stmt_t* decl = parser_alloc_stmt(p);
    decl->type = AST_STMT_FN_DECL;

    decl->stmt.fn_decl.kw = parser_eat(p); // fine because we knew to enter this function

    parser_shed_visibility_qualis_with_error(p);

    token_t* mut = parser_match_token(p, TOK_MUT);

    if (mut && decl->stmt.fn_decl.kw->type != TOK_MT) {
        compiler_error_list_emplace(p->error_list, mut, ERR_MUT_QUALIFIER_ON_NON_MT);
    }

    decl->stmt.fn_decl.is_mut = mut; // token ptr into bool

    parser_shed_visibility_qualis_with_error(p);

    token_ptr_slice_t id_slice = parse_id_token_slice(p, TOK_SCOPE_RES);
    decl->stmt.fn_decl.name = id_slice;
    size_t len = id_slice.len;
    // if the Name..method chain is too long OR the function is an fn
    if (len > 2) {
        compiler_error_list_emplace(p->error_list, id_slice.start[0],
                                    ERR_TOO_MANY_QUALIFICATIONS_ON_FUNCTION);
        cooked = true;
    } else if (len == 0) {
        cooked = true;
    }
    decl->stmt.fn_decl.is_generic = false;
    parser_match_token(p, TOK_GENERIC_SEP); // this is fine
    if (parser_match_token(p, TOK_LT)) {
        ast_slice_of_generic_params_t params = parse_generic_params(p);
        decl->stmt.fn_decl.generic_params = params;
        decl->stmt.fn_decl.is_generic = params.len; // true when len != 0
        parser_expect_token(p, TOK_GT);
    }

    token_t* lparen = parser_expect_token(p, TOK_LPAREN);
    if (!lparen) {
        return parser_sync_stmt(p);
    }

    decl->stmt.fn_decl.params = parse_slice_of_params(p, TOK_COMMA, TOK_RPAREN);

    token_t* rparen = parser_expect_token(p, TOK_RPAREN);
    if (!rparen) {
        return parser_sync_stmt(p);
    }

    token_t* rarrow = parser_match_token(p, TOK_DISCARD_RARROW);

    if (!rarrow) {
        rarrow = parser_match_token(p, TOK_RARROW);
    }
    decl->stmt.fn_decl.discardable = false;
    if (rarrow) {
        if (rarrow->type == TOK_DISCARD_RARROW) {
            decl->stmt.fn_decl.discardable = true;
        }
        ast_type_t* return_type = parse_type(p);
        if (return_type->tag == AST_TYPE_INVALID) {
            cooked = true;
        }
        decl->stmt.fn_decl.return_type = return_type;
    } else {
        decl->stmt.fn_decl.ret_arrow = NULL;
        decl->stmt.fn_decl.return_type = NULL;
    }
    bool only_expr = parser_match_token(p, TOK_EQ_ARROW);

    // handle fn foo() -> i32 => expr or {expr}
    if (only_expr) {
        decl->stmt.fn_decl.block = NULL;

        token_t* lbrace = parser_match_token(p, TOK_LBRACE);

        ast_expr_t* expr = parse_expr(p);
        if (expr->type == AST_EXPR_INVALID) {
            cooked = true;
        }
        decl->stmt.fn_decl.expr = expr;

        if (lbrace && !parser_expect_token(p, TOK_RBRACE)) {
            if (parser_prev(p)->type == TOK_SEMICOLON) {
                compiler_error_list_emplace(p->error_list, parser_prev(p),
                                            HELP_REMOVE_SEMICOLON_TO_YIELD_EXPRESSION_VALUE);
            }
            return parser_sync_stmt(p);
        }

        parser_match_token(
            p, TOK_SEMICOLON); // allow a trailing semicolon so we don't later emit a warning for it

    } else {
        ast_stmt_t* block = parse_stmt_block(p);
        if (block->type == AST_STMT_INVALID) {
            cooked = true;
        }
        decl->stmt.fn_decl.block = block;
        decl->stmt.fn_decl.expr = NULL;
    }

    decl->stmt.fn_decl.only_expr = only_expr;

    decl->first = decl->stmt.fn_decl.kw;
    decl->last = parser_prev(p);

    if (cooked) {
        return parser_invalid_stmt(p, decl->first, decl->last);
    }

    return decl;
}

ast_stmt_t* parse_stmt_return(parser_t* p) {
    ast_stmt_t* ret_stmt = parser_alloc_stmt(p);
    ret_stmt->type = AST_STMT_RETURN;
    token_t* first = parser_eat(p); // safe becuz we knew to enter this function
    // check if we should parse an expr
    if (!(parser_peek(p)->type == TOK_SEMICOLON)) {
        ret_stmt->stmt.return_stmt.expr = parse_expr(p);
    } else {
        ret_stmt->stmt.return_stmt.expr = NULL;
    }
    token_t* term = parser_expect_token(p, TOK_SEMICOLON);
    if (!term) {
        return parser_sync_stmt(p);
    }
    ret_stmt->first = first;
    ret_stmt->last = term;
    return ret_stmt;
}

ast_stmt_t* parse_stmt_yield(parser_t* p) {
    ast_stmt_t* ret_stmt = parser_alloc_stmt(p);
    ret_stmt->type = AST_STMT_YIELD;
    token_t* first = parser_peek(p);
    if (!parser_expect_token(p, TOK_YIELD)) {
        return parser_sync_stmt(p);
    }
    ret_stmt->stmt.yield_stmt.expr = parse_expr(p);
    token_t* term = parser_expect_token(p, TOK_SEMICOLON);
    if (!term) {
        return parser_sync_stmt(p);
    }
    ret_stmt->first = first;
    ret_stmt->last = term;
    return ret_stmt;
}

ast_stmt_t* parse_stmt_empty(parser_t* p) {
    ast_stmt_t* empty = parser_alloc_stmt(p);
    empty->type = AST_STMT_EMPTY;
    token_t* term = parser_eat(p); // fine becuz we knew to enter this function
    empty->stmt.empty.terminator = term;
    empty->first = term;
    empty->last = term;
    return empty;
}

ast_param_t* parse_param(parser_t* p) {
    ast_param_t* param = arena_alloc(p->arena, sizeof(ast_param_t));
    // set type
    param->type = parse_type(p);
    // try set name
    token_t* next = parser_peek(p);
    bool match = next->type == TOK_IDENTIFIER || next->type == TOK_SELF_ID;
    if (!match) {
        compiler_error_list_emplace(p->error_list, next, ERR_EXPECTED_PARAMETER_IDENTIFIER);
        param->name = NULL;
    } else {
        param->name = parser_eat(p);
    }
    // set valid
    if (!match || param->type->tag == AST_TYPE_INVALID) {
        param->valid = false;
    } else {
        param->valid = true;
    }
    // set fir/last
    param->first = param->type->first;
    if (next) {
        param->last = next;
    } else {
        param->last = parser_prev(p);
    }
    return param;
}

bool parser_shed_visibility_qualis_with_error(parser_t* p) {
    bool did_shed = false;
    while (parser_match_token_call(p, &token_is_visibility_modifier)) {
        compiler_error_list_emplace(p->error_list, parser_prev(p),
                                    ERR_EXTRANEOUS_VISIBILITY_MODIFIER);
        compiler_error_list_emplace(p->error_list, parser_prev(p), HELP_REMOVE);
        did_shed = true;
    }
    return did_shed;
}

ast_stmt_t* parse_stmt_vis_modifier(parser_t* p, ast_stmt_t* (*call)(parser_t*)) {
    ast_stmt_t* vis = parser_alloc_stmt(p);
    vis->type = AST_STMT_VISIBILITY_MODIFIER;
    token_t* modif = parser_eat(p); // fine becuz we knew to enter this function
    // shed redundant qualifiers in a loop, but don't return invalid statement
    parser_shed_visibility_qualis_with_error(p);
    ast_stmt_t* stmt = call(p); // expect a declaration, namely a function or var decl
    vis->stmt.vis_modifier.stmt = stmt;
    vis->stmt.vis_modifier.modifier = modif;
    vis->first = modif;
    vis->last = stmt->last;
    return vis;
}

bool parser_shed_compt_qualis_with_error(parser_t* p) {
    bool did_shed = false;
    while (parser_match_token(p, TOK_COMPT)) {
        compiler_error_list_emplace(p->error_list, parser_prev(p), ERR_REDUNDANT_COMPT_QUALIFIER);
        compiler_error_list_emplace(p->error_list, parser_prev(p), HELP_REMOVE);
        did_shed = true;
    }
    return did_shed;
}

/// the call should correspond to a function that parses the underlying function
ast_stmt_t* parse_stmt_compt_modifier(parser_t* p, ast_stmt_t* (*call)(parser_t* p)) {
    ast_stmt_t* vis = parser_alloc_stmt(p);
    vis->type = AST_STMT_COMPT_MODIFIER;
    token_t* modif = parser_expect_token(p, TOK_COMPT);
    if (!modif) {
        return parser_sync_stmt(p);
    }
    // shed redundant qualifiers
    // this dumb looking structure handles any abitrary nesting of compt|pub|hid, which is malformed
    // past one compt
    while (parser_shed_compt_qualis_with_error(p)) {
        while (parser_shed_visibility_qualis_with_error(p)) {
            ;
        }
    }
    while (parser_shed_visibility_qualis_with_error(p)) {
        while (parser_shed_compt_qualis_with_error(p)) {
            ;
        }
    }
    ast_stmt_t* stmt = call(p);
    vis->stmt.compt_modifier.stmt = stmt;
    vis->first = modif;
    vis->last = stmt->last;
    return vis;
}

ast_stmt_t* parse_stmt_top_level_decl(parser_t* p) {
    if (parser_peek_match(p, TOK_IMPORT)) {
        return parse_stmt_import(p);
    }
    if (parser_peek_match(p, TOK_USE)) {
        compiler_error_list_emplace(p->error_list, parser_peek(p),
                                    WARN_TOP_LEVEL_USE_CAN_POLLUTE_THE_GLOBAL_NAMESPACE);
    }
    return parse_stmt_decl(p);
}

ast_stmt_t* parse_stmt_decl(parser_t* p) {

    token_type_e next_type = parser_peek(p)->type;
    // if prev was discarded, that means that the semicolon was certainly part of an already
    // adressed malformed statement, so consum it and otherwise return an invalid statement w/ an
    // error
    if (next_type == TOK_SEMICOLON) {
        if (!p->prev_discarded) {
            compiler_error_list_emplace(p->error_list, parser_peek(p), NOTE_EXTRANEOUS_SEMICOLON);
            return parser_sync_stmt(p);
        }
        parser_eat(p); // consume lingering semicolon
        next_type = parser_peek(p)->type;
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if (token_is_function_leading_kw(next_type)) {
        return parse_fn_decl(p);
    }
    if (next_type == TOK_MODULE) {
        return parse_module(p);
    }

    if (next_type == TOK_USE) {
        return parse_stmt_use(p);
    }

    if (next_type == TOK_STRUCT) {
        return parse_stmt_struct_decl(p);
    }

    if (next_type == TOK_CONTRACT) {
        return parse_stmt_contract_decl(p);
    }

    if (next_type == TOK_UNION) {
        return parse_stmt_union_decl(p);
    }

    if (next_type == TOK_EXTERN) {
        return parse_stmt_extern_block(p);
    }

    if (next_type == TOK_DEFTYPE) {
        return parse_stmt_deftype(p);
    }

    if (next_type == TOK_VARIANT) {
        return parse_stmt_variant_decl(p);
    }

    if (token_is_visibility_modifier(next_type)) {
        return parse_stmt_vis_modifier(p, &parse_stmt_decl);
    }

    if (next_type == TOK_COMPT) {
        return parse_stmt_compt_modifier(p, &parse_stmt_var_or_fn_decl);
    }

    // handle static variable decls
    if (next_type == TOK_STATIC) {
        return parse_stmt_static_modifier(p, &parse_var_decl);
    }

    if (next_type == TOK_ALIGNAS) {
        return parse_stmt_alignas_modifier(p, &parse_var_decl);
    }

    if (next_type == TOK_IMPORT) {
        compiler_error_list_emplace(p->error_list, parser_peek(p),
                                    ERR_NON_TOP_LEVEL_IMPORT_STATEMENT);
        return parser_sync_stmt(p);
    }

    // allow parsing of (type) name;
    if (next_type == TOK_LPAREN) {
        size_t cnt = 0;
        while (parser_peek_n(p, cnt)->type == TOK_LPAREN) {
            ++cnt;
        }
        if (token_is_non_id_type_idicator(parser_peek_n(p, cnt)->type)) {
            return parse_var_decl(p);
        }
    }

    // guard trailing junk due to eof tok from previously synced stmt
    if (parser_eof(p)) {
        return parser_sync_stmt(p);
    }

    // guard against definitely malformed decls ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if (!(token_is_builtin_type_or_id(next_type) || token_is_non_id_type_idicator(next_type)
          || (next_type == TOK_STAR && parser_peek_n(p, 1)->type == TOK_FN))) {
        // if equal, this means we're almost certainly we're trailing a malformed decl
        if (parser_peek(p)->type != TOK_RBRACE) {
            compiler_error_list_emplace(p->error_list, parser_peek(p), ERR_EXPECTED_DECLARTION);
        }
        return parser_sync_stmt(p);
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    // if malformed guard passes, try to parse as a var declaration
    return parse_var_decl_from_id_or_mut(p, NULL, false); // no leading id, leading mut == false
}

ast_stmt_t* parse_module(parser_t* p) {
    ast_stmt_t* mod = parser_alloc_stmt(p);
    token_t* mod_tkn = parser_expect_token(p, TOK_MODULE);
    if (!mod_tkn) {
        return parser_sync_stmt(p);
    }
    token_t* id = parser_match_token(p, TOK_IDENTIFIER);
    if (!id) {
        compiler_error_list_emplace(p->error_list, parser_peek(p), ERR_INVALID_MODULE_NAME);
        return parser_sync_stmt(p);
    }
    mod->stmt.module.id = id;
    mod->type = AST_STMT_MODULE;

    token_type_e next_type = parser_peek(p)->type;
    if (next_type != TOK_SEMICOLON && next_type != TOK_LBRACE) {
        compiler_error_list_emplace(p->error_list, parser_peek(p),
                                    ERR_EXPECTED_DELIM_IN_MODULE_DECL);
        return parser_sync_stmt(p);
    }

    token_t* semicolon = parser_match_token(p, TOK_SEMICOLON);
    if (semicolon) {
        ast_slice_of_stmts_t decls = parse_slice_of_decls(p, TOK_EOF);
        mod->stmt.module.decls = decls;
    } else {
        parser_expect_token(p, TOK_LBRACE);
        ast_slice_of_stmts_t decls = parse_slice_of_decls(p, TOK_RBRACE);
        parser_expect_token(p, TOK_RBRACE);
        mod->stmt.module.decls = decls;
    }
    mod->first = mod_tkn;
    mod->last = parser_prev(p);
    return mod;
}

void parser_guard_against_trailing_rparens(parser_t* p) {
    if (parser_peek(p)->type == TOK_RPAREN) {
        token_t* rparen;
        while ((rparen = parser_match_token(p, TOK_RPAREN))) {
            compiler_error_list_emplace(p->error_list, parser_prev(p), ERR_MISMATCHED_RPAREN);
        }
    }
}

ast_stmt_t* parse_stmt_if(parser_t* p) {
    ast_stmt_t* if_stmt = parser_alloc_stmt(p);
    if_stmt->type = AST_STMT_IF;
    token_t* if_tkn = parser_expect_token(p, TOK_IF);
    if (!if_tkn) {
        return parser_sync_stmt(p);
    }
    if_stmt->stmt.if_stmt.compt = parser_match_token(p, TOK_COMPT);
    ast_expr_t* cond_expr = parse_expr_before_opening_brace(p);
    if_stmt->stmt.if_stmt.condition = cond_expr;
    // make sure a block will succeed the conditional expression
    parser_guard_against_trailing_rparens(p);
    if (!parser_peek_match(p, TOK_LBRACE)) {
        compiler_error_list_emplace(p->error_list, parser_peek(p),
                                    ERR_BODY_MUST_BE_WRAPPED_IN_BRACES);
        return parser_sync_stmt(p);
    }
    if_stmt->stmt.if_stmt.body_stmt = parse_stmt_block(p);
    if (parser_peek_match(p, TOK_ELSE)) {
        if_stmt->stmt.if_stmt.has_else = true;
        if_stmt->stmt.if_stmt.else_stmt = parse_stmt_else(p);
    } else {
        if_stmt->stmt.if_stmt.has_else = false;
    }
    if_stmt->first = if_tkn;
    if_stmt->last = parser_prev(p);
    return if_stmt;
}

ast_stmt_t* parse_stmt_else(parser_t* p) {
    ast_stmt_t* else_stmt = parser_alloc_stmt(p);
    else_stmt->type = AST_STMT_ELSE;
    token_t* else_tkn = parser_expect_token(p, TOK_ELSE);
    if (!else_tkn) {
        return parser_sync_stmt(p);
    }
    else_stmt->stmt.else_stmt.body_stmt = parse_stmt(p);
    else_stmt->first = else_tkn;
    else_stmt->last = else_stmt->stmt.else_stmt.body_stmt->last;
    return else_stmt;
}

ast_stmt_t* parse_stmt_while(parser_t* p) {
    ast_stmt_t* while_stmt = parser_alloc_stmt(p);
    while_stmt->type = AST_STMT_WHILE;
    token_t* while_tkn = parser_expect_token(p, TOK_WHILE);
    if (!while_tkn) {
        return parser_sync_stmt(p);
    }
    ast_expr_t* cond_expr = parse_expr_before_opening_brace(p);
    while_stmt->stmt.while_stmt.condition = cond_expr;
    parser_guard_against_trailing_rparens(p);
    // make sure a block will succeed the conditional expression
    if (!parser_peek_match(p, TOK_LBRACE)) {
        compiler_error_list_emplace(p->error_list, parser_peek(p),
                                    ERR_BODY_MUST_BE_WRAPPED_IN_BRACES);
        return parser_sync_stmt(p);
    }
    // push mode, parse block, restore ~~~~~~~~~~~~~~~~~~~~~~~~~~~
    parser_mode_e saved = parser_mode(p);
    parser_mode_set(p, PARSER_MODE_IN_LOOP);
    while_stmt->stmt.while_stmt.body_stmt = parse_stmt_block(p);
    parser_mode_set(p, saved);
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    while_stmt->first = while_tkn;
    while_stmt->last = while_stmt->stmt.while_stmt.body_stmt->last;
    return while_stmt;
}

ast_stmt_t* parse_stmt_break(parser_t* p) {
    ast_stmt_t* break_stmt = parser_alloc_stmt(p);
    break_stmt->type = AST_STMT_BREAK;
    token_t* break_tkn = parser_expect_token(p, TOK_BREAK);
    if (!break_tkn) {
        return parser_sync_stmt(p);
    }
    break_stmt->first = break_tkn;
    token_t* term = parser_expect_token(p, TOK_SEMICOLON);
    if (term) {
        break_stmt->last = term;
    } else {
        break_stmt->last = break_tkn;
    }
    return break_stmt;
}

ast_stmt_t* parse_stmt_continue(parser_t* p) {
    ast_stmt_t* cont_stmt = parser_alloc_stmt(p);
    cont_stmt->type = AST_STMT_CONTINUE;
    token_t* cont_tkn = parser_expect_token(p, TOK_CONTINUE);
    if (!cont_tkn) {
        return parser_sync_stmt(p);
    }
    cont_stmt->first = cont_tkn;
    token_t* term = parser_expect_token(p, TOK_SEMICOLON);
    if (term) {
        cont_stmt->last = term;
    } else {
        cont_stmt->last = cont_tkn;
    }
    return cont_stmt;
}

ast_stmt_t* parse_stmt_import(parser_t* p) {
    ast_stmt_t* import_stmt = parser_alloc_stmt(p);
    import_stmt->type = AST_STMT_IMPORT;
    token_t* import_tkn = parser_expect_token(p, TOK_IMPORT);
    if (!import_tkn) {
        return parser_sync_stmt(p);
    }
    import_stmt->stmt.import.extern_language = parser_match_token(p, TOK_IDENTIFIER);
    token_t* path = parser_expect_token(p, TOK_STR_LIT);
    if (!path) {
        return parser_sync_stmt(p);
    }
    import_stmt->stmt.import.file_path = path;
    import_stmt->first = import_tkn;
    import_stmt->stmt.import.has_into_mod = false;
    // not allowing this
    /*
    if (parser_match_token(p, TOK_RARROW)) {
        import_stmt->stmt.import.has_into_mod = true;
        import_stmt->stmt.import.into_mod = parse_id_token_slice(p, TOK_SCOPE_RES);
    }
    */
    parser_expect_token(p, TOK_SEMICOLON);
    import_stmt->last = parser_prev(p);
    return import_stmt;
}

ast_stmt_t* parse_stmt_use(parser_t* p) {
    ast_stmt_t* use_stmt = parser_alloc_stmt(p);
    use_stmt->type = AST_STMT_USE;
    token_t* use_tkn = parser_expect_token(p, TOK_USE);
    if (!use_tkn) {
        return parser_sync_stmt(p);
    }
    use_stmt->stmt.use.mod = parser_match_token(p, TOK_MODULE);
    token_ptr_slice_t id = parse_id_token_slice(p, TOK_SCOPE_RES);
    if (id.len == 0) {
        return parser_sync_stmt(p);
    }
    use_stmt->stmt.use.id = id;
    use_stmt->first = use_tkn;
    token_t* term = parser_expect_token(p, TOK_SEMICOLON);
    if (!term) {
        use_stmt->last = id.start[id.len - 1];
    } else {
        use_stmt->last = term;
    }
    return use_stmt;
}

ast_stmt_t* parse_stmt_for_in(parser_t* p) {
    token_t* for_tkn = parser_prev(p);
    ast_stmt_t* for_stmt = parser_alloc_stmt(p);
    for_stmt->type = AST_STMT_FOR_IN;
    ast_param_t* param = parse_param(p);
    parser_expect_token(p, TOK_IN);
    ast_expr_t* iter = parse_expr_before_opening_brace(p);
    for_stmt->stmt.for_in_stmt.each = param;
    for_stmt->stmt.for_in_stmt.iterator = iter;

    parser_guard_against_trailing_rparens(p);

    // make sure a block will succeed the conditional expression
    if (!parser_peek_match(p, TOK_LBRACE)) {
        compiler_error_list_emplace(p->error_list, parser_peek(p),
                                    ERR_BODY_MUST_BE_WRAPPED_IN_BRACES);
        return parser_sync_stmt(p);
    }
    // push mode, parse block, restore ~~~~~~~~~~~~~~~~~~~~~~~~~~~
    parser_mode_e saved = parser_mode(p);
    parser_mode_set(p, PARSER_MODE_IN_LOOP);
    for_stmt->stmt.for_in_stmt.body_stmt = parse_stmt_block(p);
    parser_mode_set(p, saved);
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    for_stmt->first = for_tkn;
    for_stmt->last = for_stmt->stmt.for_in_stmt.body_stmt->last;
    return for_stmt;
}

ast_stmt_t* parse_stmt_for(parser_t* p) {
    token_t* for_tkn = parser_expect_token(p, TOK_FOR);
    if (!for_tkn) {
        return parser_sync_stmt(p);
    }
    // try to get lparen to decide how to parse
    token_t* lparen = parser_match_token(p, TOK_LPAREN);
    // if no lparen, parse as a for-in loop
    if (!lparen) {
        return parse_stmt_for_in(p);
    }
    // else try as c-style for loop:
    ast_stmt_t* for_stmt = parser_alloc_stmt(p);
    for_stmt->type = AST_STMT_FOR;

    ast_stmt_t* init = parse_stmt(p); // this will end with a ';'
    ast_expr_t* cond_expr = parse_expr(p);
    parser_expect_token(p, TOK_SEMICOLON); // expected ';' following the expr
    ast_expr_t* step = parse_expr(p);
    for_stmt->stmt.for_stmt.init = init;
    for_stmt->stmt.for_stmt.condition = cond_expr;
    for_stmt->stmt.for_stmt.step = step;

    if (lparen) {
        parser_expect_token(p, TOK_RPAREN);
    }

    parser_guard_against_trailing_rparens(p);

    // make sure a block will succeed the conditional expression
    if (!parser_peek_match(p, TOK_LBRACE)) {
        compiler_error_list_emplace(p->error_list, parser_peek(p),
                                    ERR_BODY_MUST_BE_WRAPPED_IN_BRACES);
        return parser_sync_stmt(p);
    }
    // push mode, parse block, restore ~~~~~~~~~~~~~~~~~~~~~~~~~~~
    parser_mode_e saved = parser_mode(p);
    parser_mode_set(p, PARSER_MODE_IN_LOOP);
    for_stmt->stmt.for_stmt.body_stmt = parse_stmt_block(p);
    parser_mode_set(p, saved);
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    for_stmt->first = for_tkn;
    for_stmt->last = for_stmt->stmt.for_stmt.body_stmt->last;
    return for_stmt;
}

/// parse the form T has(id, id, id, ...)
ast_type_with_contracts_t* parse_id_with_contracts(parser_t* p) {
    ast_type_with_contracts_t* t = arena_alloc(p->arena, sizeof(ast_type_with_contracts_t));
    token_t* id = parser_expect_token(p, TOK_IDENTIFIER);
    t->valid = false;
    if (!id) {
        parser_sync(p);
    } else {
        t->valid = true;
        t->id = id;
        // optionally match has, then if there is, expect the has(id, id, id) structure
        if (parser_peek_match(p, TOK_HAS)) {
            t->contract_ids = parse_has_contracts_clause(p);
        } else {
            ast_slice_of_exprs_t ids = {.start = NULL, .len = 0};
            t->contract_ids = ids;
        }
    }
    return t;
}

ast_generic_parameter_t* parse_generic_param(parser_t* p) {
    ast_generic_parameter_t* gen_param = arena_alloc(p->arena, sizeof(ast_generic_parameter_t));
    token_t* first = parser_peek(p);
    token_type_e t0 = first->type;
    token_type_e t1 = parser_peek_n(p, 1)->type;
    if (t0 == TOK_IDENTIFIER && (t1 == TOK_COMMA || t1 == TOK_GT || t1 == TOK_HAS)) {
        gen_param->tag = AST_GENERIC_PARAM_TYPE;
        gen_param->param.generic_type = parse_id_with_contracts(p);
    } else if (token_is_non_id_type_idicator(t0) || token_is_builtin_type_or_id(t0)) {
        gen_param->tag = AST_GENERIC_PARAM_VAR;
        gen_param->param.generic_var = parse_param(p);
    } else {
        gen_param->tag = AST_GENERIC_PARAM_INVALID;
        compiler_error_list_emplace(p->error_list, first, ERR_INVALID_GENERIC_PARAMETER);
        parser_sync(p);
    }
    gen_param->first = first;
    gen_param->last = parser_prev(p);
    return gen_param;
}

ast_slice_of_generic_params_t parser_freeze_generic_param_spill_arr(parser_t* p,
                                                                    spill_arr_ptr_t* sarr) {
    ast_slice_of_generic_params_t slice = {
        .start = (ast_generic_parameter_t**)arena_alloc(
            p->arena, sarr->size * sizeof(ast_generic_parameter_t*)),
        .len = sarr->size,
    };
    spill_arr_ptr_flat_copy((void**)slice.start, sarr);
    spill_arr_ptr_destroy(sarr);
    return slice;
}

ast_slice_of_generic_params_t parse_generic_params(parser_t* p) {
    spill_arr_ptr_t sarr;
    spill_arr_ptr_init(&sarr);

    parser_mode_e saved = parser_mode(p);
    parser_mode_set(p, PARSER_MODE_BAN_ANGLE_BRACKETS_IN_EXPRS);

    while (!(parser_peek_match(p, TOK_GT) || parser_eof(p)) // while !eof (edge-case handling)
    ) {
        spill_arr_ptr_push(&sarr, parse_generic_param(p));
        if (!parser_peek_match(p, TOK_GT)) {
            parser_expect_token(p, TOK_COMMA);
        }
    }

    if (sarr.size == 0) {
        compiler_error_list_emplace(p->error_list, parser_prev(p),
                                    GENERIC_PARAMETERS_CANNOT_BE_EMPTY);
    }

    parser_mode_set(p, saved);
    return parser_freeze_generic_param_spill_arr(p, &sarr);
}

ast_stmt_t* parse_stmt_struct_decl(parser_t* p) {
    ast_stmt_t* struct_stmt = parser_alloc_stmt(p);
    struct_stmt->type = AST_STMT_STRUCT_DEF;
    token_t* struct_tkn = parser_expect_token(p, TOK_STRUCT);
    if (!struct_tkn) {
        return parser_sync_stmt(p);
    }
    struct_stmt->stmt.struct_decl.name = parser_expect_token(p, TOK_IDENTIFIER);
    struct_stmt->stmt.struct_decl.is_generic = false;
    if (parser_match_token(p, TOK_LT)
        || (parser_match_token(p, TOK_GENERIC_SEP) && parser_match_token(p, TOK_LT))) {
        ast_slice_of_generic_params_t params = parse_generic_params(p);
        struct_stmt->stmt.struct_decl.is_generic = params.len; // true when len != 0
        struct_stmt->stmt.struct_decl.generic_params = params;
        parser_expect_token(p, TOK_GT);
    }

    // check for contracts clause
    if (parser_peek_match(p, TOK_HAS)) {
        struct_stmt->stmt.struct_decl.contracts = parse_has_contracts_clause(p);
    } else {
        struct_stmt->stmt.struct_decl.contracts.start = NULL;
        struct_stmt->stmt.struct_decl.contracts.len = 0;
    }

    parser_expect_token(p, TOK_LBRACE);
    struct_stmt->stmt.struct_decl.fields = parse_slice_of_decls(p, TOK_RBRACE);
    parser_expect_token(p, TOK_RBRACE);
    struct_stmt->first = struct_tkn;
    struct_stmt->last = parser_prev(p);
    return struct_stmt;
}

ast_stmt_t* parse_fn_prototype(parser_t* p) {
    // allow compt
    if (parser_peek_match(p, TOK_COMPT)) {
        return parse_stmt_compt_modifier(p, parse_fn_prototype);
    }
    bool cooked = false;
    ast_stmt_t* decl = parser_alloc_stmt(p);
    decl->type = AST_STMT_FN_PROTOTYPE;

    parser_shed_visibility_qualis_with_error(p);

    token_t* kw = parser_expect_token_call(p, &token_is_mt_or_fn, ERR_EXPECTED_FN_OR_MT);
    decl->stmt.fn_prototype.kw = kw;

    token_t* mut = parser_match_token(p, TOK_MUT);

    if (mut && decl->stmt.fn_prototype.kw->type != TOK_MT) {
        compiler_error_list_emplace(p->error_list, mut, ERR_MUT_QUALIFIER_ON_NON_MT);
    }

    decl->stmt.fn_prototype.is_mut = mut; // token ptr into bool

    parser_shed_visibility_qualis_with_error(p);

    token_ptr_slice_t id_slice = parse_id_token_slice(p, TOK_SCOPE_RES);
    decl->stmt.fn_prototype.name = id_slice;
    if (id_slice.len > 1) {
        compiler_error_list_emplace(p->error_list, id_slice.start[0],
                                    ERR_TOO_MANY_QUALIFICATIONS_ON_FUNCTION);
        cooked = true;
    } else if (id_slice.len == 0) {
        cooked = true;
    }
    parser_match_token(p, TOK_GENERIC_SEP); // this is fine

    decl->stmt.fn_prototype.is_generic = false;
    if (parser_match_token(p, TOK_LT)) {
        ast_slice_of_generic_params_t params = parse_generic_params(p);
        decl->stmt.fn_prototype.is_generic = params.len; // params.len != 0
        decl->stmt.fn_prototype.generic_params = params;
        parser_expect_token(p, TOK_GT);
    }

    token_t* lparen = parser_expect_token(p, TOK_LPAREN);
    if (!lparen) {
        return parser_sync_stmt(p);
    }

    decl->stmt.fn_prototype.params = parse_slice_of_params(p, TOK_COMMA, TOK_RPAREN);

    token_t* rparen = parser_expect_token(p, TOK_RPAREN);
    if (!rparen) {
        return parser_sync_stmt(p);
    }

    token_t* rarrow = parser_match_token(p, TOK_DISCARD_RARROW);
    if (!rarrow) {
        rarrow = parser_match_token(p, TOK_RARROW);
    }
    decl->stmt.fn_prototype.discardable = false;
    if (rarrow) {
        if (rarrow->type == TOK_DISCARD_RARROW) {
            decl->stmt.fn_prototype.discardable = true;
        }
        ast_type_t* return_type = parse_type(p);
        if (return_type->tag == AST_TYPE_INVALID) {
            cooked = true;
        }
        decl->stmt.fn_prototype.return_type = return_type;
    } else {
        decl->stmt.fn_prototype.ret_arrow = NULL;
        decl->stmt.fn_prototype.return_type = NULL;
    }
    decl->stmt.fn_prototype.only_expr = false;
    decl->stmt.fn_prototype.expr = NULL;
    parser_expect_token(p, TOK_SEMICOLON);
    decl->first = decl->stmt.fn_prototype.kw;
    decl->last = parser_prev(p);
    if (cooked) {
        return parser_sync_stmt(p);
    }
    return decl;
}

ast_stmt_t* parse_stmt_contract_decl(parser_t* p) {
    ast_stmt_t* stmt = parser_alloc_stmt(p);
    stmt->type = AST_STMT_CONTRACT_DEF;
    token_t* struct_tkn = parser_expect_token(p, TOK_CONTRACT);
    if (!struct_tkn) {
        return parser_sync_stmt(p);
    }
    token_t* name = parser_expect_token(p, TOK_IDENTIFIER);
    if (!name) {
        return parser_sync_stmt(p);
    }
    stmt->stmt.contract_decl.name = name;
    parser_expect_token(p, TOK_LBRACE);
    stmt->stmt.contract_decl.fields = parse_slice_of_stmts_call(p, TOK_RBRACE, &parse_fn_prototype);
    parser_expect_token(p, TOK_RBRACE);
    stmt->first = struct_tkn;
    stmt->last = parser_prev(p);
    return stmt;
}

ast_stmt_t* parse_stmt_union_decl(parser_t* p) {
    ast_stmt_t* s = parser_alloc_stmt(p);
    s->type = AST_STMT_UNION_DEF;
    token_t* union_tkn = parser_expect_token(p, TOK_UNION);
    if (!union_tkn) {
        return parser_sync_stmt(p);
    }
    token_t* name = parser_expect_token(p, TOK_IDENTIFIER);
    if (!name) {
        return parser_sync_stmt(p);
    }
    s->stmt.contract_decl.name = name;
    parser_expect_token(p, TOK_LBRACE);
    s->stmt.contract_decl.fields = parse_slice_of_stmts_call(p, TOK_RBRACE, &parse_var_decl);
    parser_expect_token(p, TOK_RBRACE);
    s->first = union_tkn;
    s->last = parser_prev(p);
    return s;
}

ast_stmt_t* parse_stmt_variant_field_decl(parser_t* p) {
    ast_stmt_t* s = parser_alloc_stmt(p);
    s->type = AST_STMT_VARIANT_FIELD_DECL;
    token_t* name = parser_expect_token(p, TOK_IDENTIFIER);
    if (!name) {
        return parser_sync_stmt(p);
    }
    s->stmt.variant_field_decl.name = name;
    if (parser_match_token(p, TOK_LPAREN)) {
        s->stmt.variant_field_decl.params = parse_slice_of_params(p, TOK_COMMA, TOK_RPAREN);
        parser_expect_token(p, TOK_RPAREN);
    } else {
        ast_slice_of_params_t params = {.start = NULL, .len = 0};
        s->stmt.variant_field_decl.params = params;
    }
    s->first = name;
    s->last = parser_prev(p);
    // terminate w/ `,` unless last entry that proceeds the closing `}`
    if (!parser_peek_match(p, TOK_RBRACE)) {
        parser_expect_token(p, TOK_COMMA);
    }
    return s;
}

ast_stmt_t* parse_stmt_variant_decl(parser_t* p) {
    ast_stmt_t* s = parser_alloc_stmt(p);
    s->type = AST_STMT_VARIANT_DEF;
    token_t* vari_tkn = parser_expect_token(p, TOK_VARIANT);
    if (!vari_tkn) {
        return parser_sync_stmt(p);
    }
    token_t* name = parser_expect_token(p, TOK_IDENTIFIER);
    if (!name) {
        return parser_sync_stmt(p);
    }
    s->stmt.variant_decl.name = name;
    s->stmt.variant_decl.is_generic = false;
    if (parser_match_token(p, TOK_LT)) {
        ast_slice_of_generic_params_t params = parse_generic_params(p);
        s->stmt.variant_decl.generic_params = params;
        s->stmt.variant_decl.is_generic = params.len;
        parser_expect_token(p, TOK_GT);
    }
    parser_expect_token(p, TOK_LBRACE);
    s->stmt.variant_decl.fields
        = parse_slice_of_stmts_call(p, TOK_RBRACE, &parse_stmt_variant_field_decl);
    parser_expect_token(p, TOK_RBRACE);
    s->first = vari_tkn;
    s->last = parser_prev(p);
    return s;
}

ast_stmt_t* parse_stmt_allowing_yield(parser_t* p) {
    if (parser_peek_match(p, TOK_YIELD)) {
        return parse_stmt_yield(p);
    }
    return parse_stmt(p);
}

ast_stmt_t* parse_stmt_static_modifier(parser_t* p, ast_stmt_t* (*call)(parser_t* p)) {
    ast_stmt_t* vis = parser_alloc_stmt(p);
    vis->type = AST_STMT_STATIC_MODIFIER;
    token_t* modif = parser_expect_token(p, TOK_STATIC);
    if (!modif) {
        return parser_sync_stmt(p);
    }
    ast_stmt_t* stmt = call(p);
    vis->stmt.static_modifier.stmt = stmt;
    vis->first = modif;
    vis->last = stmt->last;
    return vis;
}

ast_stmt_t* parse_stmt_extern_block(parser_t* p) {
    ast_stmt_t* stmt = parser_alloc_stmt(p);
    stmt->type = AST_STMT_EXTERN_BLOCK;
    token_t* first = parser_expect_token(p, TOK_EXTERN);
    if (!first) {
        return parser_sync_stmt(p);
    }
    stmt->stmt.extern_block.extern_language = parser_match_token(p, TOK_IDENTIFIER);
    token_t* lbrace = parser_expect_token(p, TOK_LBRACE);
    if (!lbrace) {
        return parser_sync_stmt(p);
    }
    // only parse fn declarations in extern blocks
    stmt->stmt.extern_block.decls = parse_slice_of_stmts_call(p, TOK_RBRACE, &parse_fn_decl);
    token_t* rbrace = parser_expect_token(p, TOK_RBRACE);
    if (!rbrace) {
        return parser_sync_stmt(p);
    }
    stmt->first = first;
    stmt->last = rbrace;
    return stmt;
}

ast_stmt_t* parse_stmt_deftype(parser_t* p) {
    ast_stmt_t* s = parser_alloc_stmt(p);
    s->type = AST_STMT_DEFTYPE;
    token_t* first = parser_expect_token(p, TOK_DEFTYPE);
    if (!first) {
        return parser_sync_stmt(p);
    }
    token_t* alias = parser_expect_token(p, TOK_IDENTIFIER);
    if (!alias) {
        return parser_sync_stmt(p);
    }
    s->stmt.deftype.alias_id = alias;
    token_t* eq = parser_expect_token(p, TOK_ASSIGN_EQ);
    if (!eq) {
        return parser_sync_stmt(p);
    }
    // either a type expression or typeof(expr)
    if (parser_peek_match(p, TOK_TYPEOF)) {
        s->stmt.deftype.aliased_type_expr = parse_expr(p);
    } else {
        s->stmt.deftype.aliased_type_expr = parse_expr_type(p);
    }
    parser_expect_token(p, TOK_SEMICOLON);
    s->first = first;
    s->last = parser_prev(p);
    return s;
}

ast_stmt_t* parse_stmt_var_or_fn_decl(parser_t* p) {

    token_type_e next_type = parser_peek(p)->type;
    // if prev was discarded, that means that the semicolon was certainly part of an already
    // adressed malformed statement, so consum it and otherwise return an invalid statement w/ an
    // error
    if (next_type == TOK_SEMICOLON) {
        if (!p->prev_discarded) {
            compiler_error_list_emplace(p->error_list, parser_peek(p), NOTE_EXTRANEOUS_SEMICOLON);
            return parser_sync_stmt(p);
        }
        parser_eat(p); // consume lingering semicolon
        next_type = parser_peek(p)->type;
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if (token_is_function_leading_kw(next_type)) {
        return parse_fn_decl(p);
    }

    // handle static variable decls
    if (next_type == TOK_STATIC) {
        return parse_stmt_static_modifier(p, &parse_var_decl);
    }

    // guard trailing junk due to eof tok from previously synced stmt
    if (parser_eof(p)) {
        return parser_sync_stmt(p);
    }

    // guard against definitely malformed decls ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    if (!(token_is_builtin_type_or_id(next_type) || token_is_non_id_type_idicator(next_type)
          || (next_type == TOK_STAR && parser_peek_n(p, 1)->type == TOK_FN))) {
        // if equal, this means we're almost certainly we're trailing a malformed decl
        if (parser_peek(p)->type != TOK_RBRACE) {
            compiler_error_list_emplace(p->error_list, parser_peek(p), ERR_EXPECTED_DECLARTION);
        }
        return parser_sync_stmt(p);
    }
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    // if malformed guard passes, try to parse as a var declaration
    return parse_var_decl_from_id_or_mut(p, NULL, false); // no leading id, leading mut == false
}

ast_stmt_t* parse_stmt_alignas_modifier(parser_t* p, ast_stmt_t* (*call)(parser_t*)) {
    ast_stmt_t* alignaz = parser_alloc_stmt(p);
    alignaz->type = AST_STMT_ALIGNAS_MODIFIER;
    token_t* modif = parser_expect_token(p, TOK_ALIGNAS);
    if (!modif) {
        return parser_sync_stmt(p);
    }
    ast_expr_t* expr = parse_expr(p);

    while (parser_shed_visibility_qualis_with_error(p)) {
        ;
    }

    ast_stmt_t* stmt = call(p);
    alignaz->stmt.alignaz.align_expr = expr;
    alignaz->stmt.alignaz.inner = stmt;
    alignaz->first = modif;
    alignaz->last = stmt->last;
    return alignaz;
}
