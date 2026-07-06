//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/parser/token_eaters.h"
#include "compiler/diagnostics/error_codes.h"
#include "compiler/diagnostics/error_list.h"
#include "compiler/parser/parser.h"
#include "compiler/parser/rules.h"
#include "compiler/token.h"
#include "utils/vector.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ----------- token consumption primitive functions -------

// consume a token
token_t* parser_eat(parser_t* parser) {
    token_t* tkn = vector_at(parser->tokens, parser->pos);
    if (tkn->type != TOK_EOF) {
        parser->pos++;
        parser->prev_discarded = false;
    }
    return tkn;
}

void parser_toss(parser_t* p) {
    parser_eat(p);
    p->prev_discarded = true;
}

// peek current uneaten token without consuming it
token_t* parser_peek(parser_t* parser) {
    token_t* tkn = vector_at(parser->tokens, parser->pos);
    return tkn;
}

token_t* parser_peek_n(parser_t* parser, size_t n) {
    size_t idx = parser->pos + n;
    if (idx >= parser->tokens->size) {
        return parser_peek(parser); // return EOF
    }
    return vector_at(parser->tokens, idx);
}

// see last eaten token
token_t* parser_prev(parser_t* parser) {
    if (parser->pos == 0) {
        return NULL;
    }
    token_t* tkn = vector_at(parser->tokens, parser->pos - 1);
    return tkn;
}

/**
 * undiscards the last tossed token if it matches, think of this like a reverse match, but safe,
 * since it will only fire if the last token was certainly tossed an not used
 */
token_t* parser_match_tossed(parser_t* parser, token_type_e type) {
    if (parser->prev_discarded && parser_prev(parser)->type == type) {
        parser->prev_discarded = false;
        --parser->pos;
        return parser_prev(parser);
    }
    return NULL;
}

token_t* parser_peek_match(parser_t* parser, token_type_e type) {
    token_t* tkn = vector_at(parser->tokens, parser->pos);
    if (tkn->type == type && type != TOK_EOF) {
        return tkn;
    }
    return parser_match_tossed(parser, type); // NULL or good if match
}

/*
 * peek then conditionally eat
 * \return token_t* to consumed token or NULL if not matched
 */
token_t* parser_match_token(parser_t* parser, token_type_e type) {
    token_t* tkn = parser_peek_match(parser, type);
    if (tkn) {
        ++parser->pos;
        parser->prev_discarded = false; // ensure
    }
    return tkn;
}

/**
 * peek then conditionally eat based on a match function call, which takes a token_type_e and
 * returns bool if valid, else false
 * \return token_t* to consumed token or NULL if not matched
 */
token_t* parser_match_token_call(parser_t* parser, bool (*match)(token_type_e)) {
    token_t* tkn = vector_at(parser->tokens, parser->pos);
    if (match(tkn->type)) {
        if (tkn->type != TOK_EOF) {
            parser->pos++;
            parser->prev_discarded = false;
        }
        return tkn;
    }
    return NULL;
}

// eat if current token matches specified type or return NULL and add to error_list
token_t* parser_expect_token(parser_t* parser, token_type_e expected_type) {
    token_t* tkn = vector_at(parser->tokens, parser->pos);
    if (tkn->type == expected_type) {
        parser->pos++;
        parser->prev_discarded = false;
        return tkn;
    }
    if (parser->prev_discarded && parser_prev(parser)->type == expected_type) {
        parser->prev_discarded = false;
        return parser_prev(parser);
    }
    parser_toss(parser);
    compiler_error_list_emplace_expected_token(parser->error_list, tkn, ERR_EXPECTED_TOKEN,
                                               expected_type);
    return NULL;
}

// eat or return NULL and add a specific error to error_list (not just Expected token: __)
// matches based on a specified token_type_e
token_t* parser_expect_token_with_err_code(parser_t* parser, token_type_e expected_type,
                                           error_code_e code) {
    token_t* tkn = vector_at(parser->tokens, parser->pos);
    if (tkn->type == expected_type) {
        parser->pos++;
        parser->prev_discarded = false;
        return tkn;
    }
    if (parser->prev_discarded && parser_prev(parser)->type == expected_type) {
        parser->prev_discarded = false;
        return parser_prev(parser);
    }
    parser_toss(parser);
    compiler_error_list_emplace(parser->error_list, tkn, code);
    return NULL;
}

// eat or return NULL and add a specific error to error_list (not just Expected token: __)
// uses a match call that returns bool based on a token_type_e
token_t* parser_expect_token_call(parser_t* parser, bool (*match)(token_type_e),
                                  error_code_e code) {
    token_t* tkn = vector_at(parser->tokens, parser->pos);
    if (match(tkn->type)) {
        parser->pos++;
        parser->prev_discarded = false;
        return tkn;
    }
    if (parser->prev_discarded && match(parser_prev(parser)->type)) {
        parser->prev_discarded = false;
        return parser_prev(parser);
    }
    parser_toss(parser);
    compiler_error_list_emplace(parser->error_list, tkn, code);
    return NULL;
}

// returns true when parser is at EOF
bool parser_eof(const parser_t* parser) {
    token_t* tkn = vector_at(parser->tokens, parser->pos);
    return tkn->type == TOK_EOF;
}

bool token_is_closing_region_delim(token_type_e t);

token_range_t parser_sync_call(parser_t* p, bool (*match)(token_type_e)) {
    token_t* first_tkn = parser_peek(p);
    token_t* last_tkn = first_tkn; // init in case loop never runs!
    while (!parser_eof(p)) {
        token_t* curr = parser_peek(p);
        if (match(curr->type)) {
            break;
        }
        parser_toss(p);
        last_tkn = parser_prev(p);
    }
    if (first_tkn == last_tkn) {
        parser_toss(p);
    }
    token_range_t range = {
        .first = first_tkn,
        .last = last_tkn,
    };
    return range;
}

token_range_t parser_sync_until(parser_t* p, token_type_e tok_type) {
    token_t* first_tkn = parser_peek(p);
    token_t* last_tkn = first_tkn; // init in case loop never runs!
    while (!parser_eof(p)) {
        if (parser_peek_match(p, tok_type)) {
            break;
        }
        parser_toss(p);
        last_tkn = parser_prev(p);
    }
    if (first_tkn == last_tkn) {
        parser_toss(p);
    }
    token_range_t range = {
        .first = first_tkn,
        .last = last_tkn,
    };
    return range;
}

token_range_t parser_sync(parser_t* p) { return parser_sync_call(p, &token_is_syncable_delim); }

token_t* parser_expect_generic_closing_delim(parser_t* p) {
    token_t* tkn = NULL;
    // this unsigned_integral embedded val is zero initialized. So we're just overloading it here to
    // check how many times we've ticked the tokens >>> and >> to be in place of > > > and > >
    if ((tkn = parser_peek_match(p, TOK_RSHA))) {
        uint32_t ticks = ++(tkn->val.unsigned_integral);
        if (ticks == 3) {
            return parser_eat(p);
        }
        if (ticks < 3) {
            return tkn;
        }
    }
    if ((tkn = parser_peek_match(p, TOK_RSHL))) {
        uint32_t ticks = ++(tkn->val.unsigned_integral);
        if (ticks == 2) {
            return parser_eat(p);
        }
        if (ticks < 2) {
            return tkn;
        }
    }
    // just expect this guy, if this fails it returns NULL here as intended
    return parser_expect_token(p, TOK_GT);
}

token_t* parser_peek_generic_closing_delims(parser_t* p) {
    token_type_e type = parser_peek(p)->type;
    if (type == TOK_GT || type == TOK_RSHA || type == TOK_RSHL) {
        return parser_peek(p);
    }
    return NULL;
}

bool is_legal_binary_op(parser_t* p, token_type_e type) {
    return is_binary_op(type)
           && !((p->mode == PARSER_MODE_BAN_ANGLE_BRACKETS_IN_EXPRS)
                && is_angle_bracket_operator(type))
           && !((p->mode == PARSER_MODE_BAN_BAR) && type == TOK_BAR);
}

// map containing look-ups for builtin types
static const bool parser_builtin_type_map[TOK__NUM]
    = {[TOK_CHAR] = true, [TOK_U8] = true,  [TOK_I8] = true,   [TOK_I16] = true,
       [TOK_U16] = true,  [TOK_I32] = true, [TOK_U32] = true,  [TOK_I64] = true,
       [TOK_U64] = true,  [TOK_F32] = true, [TOK_F64] = true,  [TOK_BOOL] = true,
       [TOK_STR] = true,  [TOK_VAR] = true, [TOK_VOID] = true, [TOK_SELF_TYPE] = true};

// match helpers
bool token_is_builtin_type(token_type_e t) { return parser_builtin_type_map[t]; }
bool token_is_builtin_type_or_id(token_type_e t) {
    return t == TOK_IDENTIFIER || parser_builtin_type_map[t] || t == TOK_SELF_ID;
}

bool token_is_literal(token_type_e t) {
    return t == TOK_BOOL_LIT_TRUE || t == TOK_BOOL_LIT_FALSE || t == TOK_INT_LIT
           || t == TOK_FLOAT_LIT || t == TOK_STR_LIT || t == TOK_UINT_LIT || t == TOK_CHAR_LIT
           || t == TOK_NULL_LIT;
}

bool token_is_function_leading_kw(token_type_e t) {
    return t == TOK_FN || t == TOK_MT || t == TOK_DT;
}

bool token_is_mt_or_fn(token_type_e t) { return t == TOK_FN || t == TOK_MT; }

bool token_is_mt_or_dt(token_type_e t) { return t == TOK_MT || t == TOK_DT; }

bool token_is_syncable_delim(token_type_e t) {
    return t == TOK_LBRACE || t == TOK_SEMICOLON || t == TOK_RBRACE || t == TOK_RPAREN
           || token_is_function_leading_kw(t) || t == TOK_COMMA || t == TOK_EQ_ARROW;
}

bool token_is_syncable_stmt_delim(token_type_e t) {
    return t == TOK_LBRACE || t == TOK_SEMICOLON || t == TOK_RBRACE
           || token_is_function_leading_kw(t);
}

bool token_is_closing_region_delim(token_type_e t) { return t == TOK_RBRACE; }

bool token_is_assignment_init(token_type_e t) { return t == TOK_ASSIGN_MOVE || t == TOK_ASSIGN_EQ; }

bool token_is_posttype_indicator(token_type_e t) {
    return t == TOK_IDENTIFIER || t == TOK_MUT || t == TOK_LT || t == TOK_GENERIC_SEP;
}

bool token_is_ref_or_ptr(token_type_e t) { return t == TOK_AMPER || t == TOK_STAR; }

bool token_is_non_id_type_idicator(token_type_e t) {
    return t == TOK_MUT || t == TOK_DECAY || t == TOK_LBRACK || t == TOK_TYPEOF
           || token_is_ref_or_ptr(t);
}

bool token_is_generic_opener(token_type_e t) { return t == TOK_GENERIC_SEP || t == TOK_LT; }

bool token_is_preunary_op_expecting_type(token_type_e t) {
    return t == TOK_SIZEOF || t == TOK_ALIGNOF;
}

bool token_is_visibility_modifier(token_type_e t) { return t == TOK_HID || t == TOK_PUB; }

// ^^^^^^^^^^^^^^^^ token consumption primitive functions ^^^^^^^^^^^^^^^^^^^^^^^^^^^
