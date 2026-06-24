//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_PARSER_TOKEN_EATERS_H
#define COMPILER_PARSER_TOKEN_EATERS_H
#include "compiler/diagnostics/error_codes.h"
#include "compiler/parser/parser.h"
#include "compiler/token.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/// consume a token
token_t* parser_eat(parser_t* parser);

/// discard/toss out a token
void parser_toss(parser_t* p);

/// peek current uneaten token without consuming it
token_t* parser_peek(parser_t* parser);

/// peek current uneaten token only if it matches the specified type
token_t* parser_peek_match(parser_t* parser, token_type_e type);

/// peek a specified number of tokens ahead
token_t* parser_peek_n(parser_t* parser, size_t n);

/// see last eaten token
token_t* parser_prev(parser_t* parser);
/**
 * peek then conditionally eat
 * \return token_t* to consumed token or NULL if not matched
 */
token_t* parser_match_token(parser_t* parser, token_type_e type);

/// match prev token only if it was tossed
token_t* parser_match_tossed(parser_t* parser, token_type_e type);

/**
 * peek then conditionally eat based on a match function call, which takes a token_type_e and
 * returns bool if valid, else false
 * \return token_t* to consumed token or NULL if not matched
 */
token_t* parser_match_token_call(parser_t* parser, bool (*match)(token_type_e));

/// eat if current token matches specified type or return NULL and add to error_list
token_t* parser_expect_token(parser_t* parser, token_type_e expected_type);

/// eat or return NULL and add a specific error to error_list (not just Expected token: __)
/// - matches based on a specified token_type_e
token_t* parser_expect_token_with_err_code(parser_t* parser, token_type_e expected_type,
                                           error_code_e code);

/// eat or return NULL and add a specific error to error_list (not just Expected token: __)
/// uses a match call that returns bool based on a token_type_e
token_t* parser_expect_token_call(parser_t* parser, bool (*match)(token_type_e), error_code_e code);

token_range_t parser_sync_call(parser_t* p, bool (*match)(token_type_e));

// returns true when parser is at EOF
bool parser_eof(const parser_t* parser);

// sync the parser and returns a range of tokens consumed while syncing
token_range_t parser_sync(parser_t* p);

// sync the parser until a given type
token_range_t parser_sync_until(parser_t* p, token_type_e tok_type);

/// ensure binary op is legal
bool is_legal_binary_op(parser_t* p, token_type_e type);

//~~~ match helpers ~~~~

/// This resolves issues surrounding
token_t* parser_expect_generic_closing_delim(parser_t* p);

token_t* parser_peek_generic_closing_delims(parser_t* p);

/// returns true when tkn type is builtin
bool token_is_builtin_type(token_type_e t);
/// returns true when tkn type is builtin or an id
bool token_is_builtin_type_or_id(token_type_e t);
/// returns true when tkn type is builtin type mod (box, bag, etc)
bool token_is_builtin_type_mod(token_type_e t);
/// returns true when tkn type is a literal
bool token_is_literal(token_type_e t);
/// returns true when tkn type is a delimiter which the parser can sync to, like a parenthesis or
/// brace
bool token_is_syncable_delim(token_type_e t);

bool token_is_syncable_stmt_delim(token_type_e t);

bool token_is_assignment_init(token_type_e t);

bool token_is_posttype_indicator(token_type_e t);

bool token_is_ref_or_ptr(token_type_e t);

bool token_is_function_leading_kw(token_type_e t);

bool token_is_mt_or_fn(token_type_e t);

bool token_is_mt_or_dt(token_type_e t);

bool token_is_non_id_type_idicator(token_type_e t);

bool token_is_generic_opener(token_type_e t);

bool token_is_preunary_op_expecting_type(token_type_e t);

bool token_is_visibility_modifier(token_type_e t);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
