//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_PARSER_STMT
#define COMPILER_PARSER_STMT

#include "compiler/ast/stmt.h"
#include "compiler/parser/parser.h"
#include "utils/spill_arr.h"

/// builds up an ast in the form of a file stmt which contains a file_name and a vector of
/// ast_stmt_t's
ast_stmt_t* parse_file(parser_t* parser, const char* file_name);

/// for adding contents of the vector to the arena, and freeing the vector
/// - vector must contain type ast_stmt_t
ast_slice_of_stmts_t parser_freeze_stmt_spill_arr(parser_t* p, spill_arr_ptr_t* sarr);

ast_slice_of_stmts_t parse_slice_of_stmts_call(parser_t* p, token_type_e until_tkn,
                                               ast_stmt_t* (*call)(parser_t*));

/// parse a sequence of any kind of statement
ast_slice_of_stmts_t parse_slice_of_stmts(parser_t* p, token_type_e until);

/// parse a sequence of stmts but only allowing declarations
ast_slice_of_stmts_t parse_slice_of_decls(parser_t* p, token_type_e until_tkn);

/// for adding contents of the vector to the arena, and freeing the vector
/// - vector must contain type ast_param_t
ast_slice_of_params_t parser_freeze_params_vec(parser_t* p, vector_t* vec);

ast_stmt_t* parser_alloc_stmt(parser_t* p);

ast_stmt_t* parse_stmt(parser_t* p);

ast_stmt_t* parse_stmt_expr(parser_t* p, ast_expr_t* expr);

ast_stmt_t* parse_stmt_block(parser_t* p);

/// parse a variable declaration with specified leading quals/identifiers
ast_stmt_t* parse_var_decl_from_id_or_mut(parser_t* p, token_ptr_slice_t* opt_id_slice,
                                          bool leading_mut);

/// parse a variable declaration
ast_stmt_t* parse_var_decl(parser_t* p);

/// parse a statement beginning with a func declarator
ast_stmt_t* parse_fn_decl(parser_t* p);

/// parse a statement beginning with return
ast_stmt_t* parse_stmt_return(parser_t* p);

/// parse a statement that't just ';'
ast_stmt_t* parse_stmt_empty(parser_t* p);

/// parse a statement like "pub i32 x = 1;"
ast_stmt_t* parse_stmt_vis_modifier(parser_t* p, ast_stmt_t* (*call)(parser_t*));

/// shed extraneous pub and hid qualifiers and emplace errors messages, returns true if shed
/// anything
bool parser_shed_visibility_qualis_with_error(parser_t* p);

/// parse next statement expected a variable or function declaration
ast_stmt_t* parse_stmt_decl(parser_t* p);

ast_stmt_t* parse_stmt_top_level_decl(parser_t* p);

ast_stmt_t* parse_module(parser_t* p);

ast_stmt_t* parse_stmt_if(parser_t* p);

ast_stmt_t* parse_stmt_else(parser_t* p);

ast_stmt_t* parse_stmt_while(parser_t* p);

ast_stmt_t* parse_stmt_break(parser_t* p);

ast_stmt_t* parse_stmt_continue(parser_t* p);

ast_stmt_t* parse_stmt_import(parser_t* p);

ast_stmt_t* parse_stmt_use(parser_t* p);

ast_stmt_t* parse_stmt_for(parser_t* p);

ast_slice_of_generic_params_t parse_generic_params(parser_t* p);

ast_stmt_t* parse_stmt_compt_modifier(parser_t* p, ast_stmt_t* (*call)(parser_t* p));

ast_stmt_t* parse_stmt_struct_decl(parser_t* p);

ast_stmt_t* parse_fn_prototype(parser_t* p);

ast_stmt_t* parse_stmt_contract_decl(parser_t* p);

ast_stmt_t* parse_stmt_union_decl(parser_t* p);

ast_stmt_t* parse_stmt_variant_decl(parser_t* p);

ast_stmt_t* parse_stmt_allowing_yield(parser_t* p);

void parser_guard_against_trailing_rparens(parser_t* p);

ast_stmt_t* parse_stmt_static_modifier(parser_t* p, ast_stmt_t* (*call)(parser_t* p));

ast_stmt_t* parse_stmt_extern_block(parser_t* p);

ast_stmt_t* parse_stmt_deftype(parser_t* p);

ast_stmt_t* parse_stmt_var_or_fn_decl(parser_t* p);

ast_stmt_t* parse_stmt_alignas_modifier(parser_t* p, ast_stmt_t* (*call)(parser_t*));

#endif
