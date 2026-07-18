//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_PARSER_PARSE_TYPE
#define COMPILER_PARSER_PARSE_TYPE

#include "compiler/ast/expr.h"
#include "compiler/ast/type.h"
#include "compiler/parser/parse_token_slice.h"
#include "compiler/parser/parser.h"

ast_type_t* parse_type_with_leading_id(parser_t* p, token_ptr_slice_t id_slice);

ast_type_t* parse_type(parser_t* p);

ast_param_t* parse_param(parser_t* p);

ast_slice_of_params_t parse_slice_of_params(parser_t* p, token_type_e divider,
                                            token_type_e terminator);

ast_type_t* parse_type_ref_ptr(parser_t* p);

ast_type_t* parse_type_of(parser_t* p);

ast_type_t* parse_type_decay(parser_t* p);

ast_type_t* parse_type_arr(parser_t* p);

ast_type_t* parse_type_slice(parser_t* p);

ast_type_t* parse_type_generic(parser_t* p, ast_type_t* inner);

ast_slice_of_generic_args_t parse_slice_of_generic_args(parser_t* p);

ast_type_t* parse_type_fn_ptr(parser_t* p);

#endif // ! COMPILER_PARSER_PARSE_TYPE
