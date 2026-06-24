//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_PARSER_EXPR_H
#define COMPILER_PARSER_EXPR_H
#include "compiler/ast/expr.h"
#include "compiler/parser/parser.h"
#include "utils/spill_arr.h"
#include <stdint.h>

/// for adding contents of the vector to the arena, and freeing the vector
/// - vector must contain type ast_expr_t
ast_slice_of_exprs_t parser_freeze_expr_spill_arr(parser_t* p, spill_arr_ptr_t* sarr);

/// allocs an ast_expr_t using the parser's internal arena
ast_expr_t* parser_alloc_expr(parser_t* p);

/// parse an expr
ast_expr_t* parse_expr(parser_t* p);

/// lhs is NULLable
ast_expr_t* parse_expr_prec(parser_t* p, ast_expr_t* lhs, uint8_t prec);

/// lhs is not NULLable
ast_expr_t* parse_expr_with_leading_id_expr(parser_t* p, ast_expr_t* lhs);

ast_expr_t* parse_expr_from_id_slice(parser_t* p, token_ptr_slice_t id_slice);

ast_expr_t* parse_primary_expr(parser_t* p);

ast_expr_t* parse_preunary_expr(parser_t* p);

ast_expr_t* parse_expr_same_type(parser_t* p);

ast_expr_t* parse_expr_type_to_str(parser_t* p);

ast_expr_t* parse_expr_inferable_as(parser_t* p);

ast_expr_t* parse_expr_defined(parser_t* p);

ast_expr_t* parse_expr_has_contract(parser_t* p);

ast_expr_t* parse_expr_static_assert(parser_t* p);

ast_expr_t* parse_expr_diagnostic(parser_t* p);

ast_expr_t* parse_literal(parser_t* p);

ast_expr_t* parse_id(parser_t* p);

token_t* parse_var_name(parser_t* p);

ast_expr_t* parse_binary(parser_t* p, ast_expr_t* lhs, uint8_t max_prec);

ast_expr_t* parse_postunary(parser_t* p, ast_expr_t* lhs);

ast_expr_t* parser_sync_expr(parser_t* p);

ast_expr_t* parse_grouping(parser_t* p);

/// parse a function call
/// lhs - called expr
/// gen_args - generic args (pass in NULL if they're are none)
ast_expr_t* parse_fn_call(parser_t* p, ast_expr_t* lhs, ast_slice_of_generic_args_t* gen_args);

ast_expr_t* parse_subscript(parser_t* p, ast_expr_t* lhs);

ast_expr_t* parse_expr_type(parser_t* p);

/// parse a struct-init expr
/// lhs - id expr of the struct (NULLable)
/// gen_args - generic args (pass in NULL if they're are none)
ast_expr_t* parse_expr_struct_init(parser_t* p, ast_expr_t* id_lhs,
                                   ast_slice_of_generic_args_t* gen_args);

ast_expr_t* parse_expr_generic_id(parser_t* p, token_ptr_slice_t id_slice,
                                  ast_slice_of_generic_args_t gen_args);

ast_expr_t* parse_expr_borrow(parser_t* p);

ast_expr_t* parse_expr_variant_decomp(parser_t* p);

ast_expr_t* parse_expr_variant_decomp_with_leading_id(parser_t* p, token_ptr_slice_t id);

ast_expr_t* parse_expr_match_pattern(parser_t* p);

ast_expr_t* parse_expr_allowing_block_exprs_with_yields(parser_t* p);

ast_expr_t* parse_expr_match_branch(parser_t* p);

ast_expr_t* parse_expr_match(parser_t* p);

ast_expr_t* parse_expr_closure(parser_t* p);

ast_expr_t* parse_expr_list_literal(parser_t* p);

/**
 * parses an expression before an opering brace in the form:
 * - `... <expr> { ...`
 * - used for the last expressions in the condtions of while & if statements & also after the
 * iterable expr in for-in statements
 */
ast_expr_t* parse_expr_before_opening_brace(parser_t* p);

ast_expr_t* parse_expr_contract(parser_t* p);

/// parse the clause has(Foo0, Foo1, Foo2)
ast_slice_of_exprs_t parse_has_contracts_clause(parser_t* p);

ast_expr_t* parse_expr_ternary_if(parser_t* p, ast_expr_t* lhs);

ast_expr_t* parse_expr_compt(parser_t* p);

/// sets generic args value
/// - returns true if generic args are found
bool try_parse_generic_args(parser_t* p, ast_slice_of_generic_args_t* args);
#endif
