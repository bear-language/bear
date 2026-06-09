//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef AST_EXPRESSIONS_H
#define AST_EXPRESSIONS_H
#include "compiler/ast/stmt_slice.h"
#include "compiler/token.h"
#include "params.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum ast_expr_type {
    // atoms/primary exprs
    AST_EXPR_ID,
    AST_EXPR_LITERAL,
    AST_EXPR_LIST_LITERAL,
    // binary
    AST_EXPR_BINARY, // binary arithmetic expression: +, -, *, /, %, bitwise, comparison, boolean
    // grouping
    AST_EXPR_GROUPING, // (<some_expr>)
    // unary
    AST_EXPR_PRE_UNARY,  // unary expr: -, +, !, ~, ++, --
    AST_EXPR_POST_UNARY, // unary expr: ++, --
    // subscript
    AST_EXPR_SUBSCRIPT,
    // func
    AST_EXPR_FN_CALL, // func(args...), can also be used for methods

    // type expr
    AST_EXPR_TYPE,

    // compt(expr)
    AST_EXPR_COMPT,

    // borrow like & or &mut
    AST_EXPR_BORROW,

    // builtins
    AST_EXPR_SAME_TYPE,
    AST_EXPR_TYPE_TO_STR,
    AST_EXPR_STATIC_ASSERT,
    AST_EXPR_DEFINED,
    AST_EXPR_HAS_CONTRACT,
    AST_EXPR_INFERABLE_AS,

    // structs
    AST_EXPR_STRUCT_INIT,
    AST_EXPR_STRUCT_MEMBER_INIT,

    // closure
    AST_EXPR_CLOSURE,

    // ternary
    AST_EXPR_TERNARY_IF,

    // variants
    AST_EXPR_VARIANT_DECOMP,

    // block expr/match
    AST_EXPR_BLOCK,
    AST_EXPR_MATCH_BRANCH,
    AST_EXPR_MATCH,
    /// has no associated type in the ast_expr_u union
    AST_EXPR_ELSE_MATCH_PATTERN,

    // error
    AST_EXPR_INVALID,

} ast_expr_type_e;

// forward decls
typedef struct ast_expr ast_expr_t;
typedef struct ast_type ast_type_t;

/// slice of ast_expr_t
typedef struct ast_slice_of_exprs {
    ast_expr_t** start;
    size_t len;
} ast_slice_of_exprs_t;

// expr types ~~~~~~~~~~~~

typedef struct ast_expr_id {
    token_ptr_slice_t slice;
} ast_expr_id_t;

typedef struct ast_expr_literal {
    token_t* tkn;
} ast_expr_literal_t;

// resolve through operator token type
typedef struct ast_expr_binary {
    ast_expr_t* lhs;
    token_t* op;
    ast_expr_t* rhs;
} ast_expr_binary_t;

typedef struct ast_expr_subscript {
    ast_expr_t* lhs;
    ast_expr_t* subexpr;
} ast_expr_subscript_t;

typedef struct ast_expr_grouping {
    token_t* left_paren;
    ast_expr_t* expr;
    token_t* right_paren;
} ast_expr_grouping_t;

// pre/postfix must be determined by the ast_expr_type_e inside the wrapping ast_expr_t
typedef struct ast_expr_unary {
    ast_expr_t* expr;
    token_t* op;
} ast_expr_unary_t;

typedef struct ast_expr_borrow {
    ast_expr_t* borrowed;
    token_t* mut;
} ast_expr_borrow_t;

// generics ~~~~~~~~~~~~~~~~~~
typedef union ast_generic_arg_u {
    ast_expr_t* expr;
    ast_type_t* type;
} ast_generic_arg_u;

typedef enum ast_generic_arg_e {
    AST_GENERIC_ARG_TYPE,
    AST_GENERIC_ARG_EXPR,
} ast_generic_arg_e;

typedef struct ast_generic_arg {
    ast_generic_arg_u arg;
    ast_generic_arg_e tag;
    bool valid;
} ast_generic_arg_t;

typedef struct ast_slice_of_generic_args {
    ast_generic_arg_t** start;
    size_t len;
    bool valid;
} ast_slice_of_generic_args_t;
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef struct ast_expr_struct_member_init {
    token_t* id;
    token_t* assign_op;
    ast_expr_t* value;
} ast_expr_struct_member_init_t;

typedef struct ast_expr_struct_init {
    token_ptr_slice_t id;
    ast_slice_of_generic_args_t generic_args;
    ast_slice_of_exprs_t member_inits;
    bool is_generic;
} ast_expr_struct_init_t;

typedef struct ast_expr_fn_call {
    ast_expr_t* left_expr; // should resolve to a func/func ptr
    ast_slice_of_generic_args_t generic_args;
    ast_slice_of_exprs_t args; // of type ast_expr_t
    bool is_generic;
} ast_expr_fn_call_t;

typedef struct ast_expr_type_expr {
    ast_type_t* type;
} ast_expr_type_t;

typedef struct ast_expr_variant_decomp {
    token_ptr_slice_t id;
    ast_slice_of_params_t vars;
} ast_expr_variant_decomp_t;

typedef struct ast_expr_block {
    ast_slice_of_stmts_t stmts;
} ast_expr_block_t;

typedef struct ast_expr_match_branch {
    ast_slice_of_exprs_t patterns;
    ast_expr_t* value;
} ast_expr_match_branch_t;

typedef struct ast_expr_match {
    ast_expr_t* matched;
    ast_slice_of_exprs_t branches;
} ast_expr_match_t;

typedef struct ast_expr_closure {
    ast_slice_of_params_t params;
    ast_expr_t* body;
    // NULLable if no return_type
    ast_type_t* return_type;
    bool is_move;
    bool has_explicit_return_type;
} ast_expr_closure_t;

typedef struct ast_expr_ternary_if {
    ast_expr_t* happy_expr;
    ast_expr_t* condition;
    ast_expr_t* else_expr;
    bool compt;
} ast_expr_ternary_if_t;

typedef struct ast_expr_list_literal {
    ast_slice_of_exprs_t slice;
} ast_expr_list_literal_t;

typedef struct ast_expr_wrapped {
    ast_expr_t* inner;
} ast_expr_wrapped_t;

typedef struct ast_expr_two_types {
    ast_type_t* lhs_type;
    ast_type_t* rhs_type;
} ast_expr_two_types_t;

typedef struct ast_expr_wrapped_id {
    token_ptr_slice_t id;
} ast_expr_wrapped_id_t;

typedef struct ast_expr_defined {
    token_ptr_slice_t id;
    // indicates that '.' dots were used, not '..'
    bool member;
} ast_expr_defined_t;

typedef struct ast_expr_has_contract {
    ast_type_t* type;
    token_ptr_slice_t contract_id_slice;
} ast_expr_has_contract_t;

// ^^^^^^^^^^^^^^^^^^^^^^^^

typedef union ast_expr_u {
    ast_expr_id_t id;
    ast_expr_literal_t literal;
    ast_expr_binary_t binary;
    ast_expr_grouping_t grouping;
    ast_expr_unary_t unary;
    ast_expr_fn_call_t fn_call;
    ast_expr_subscript_t subscript;
    ast_expr_type_t type_expr;
    ast_expr_struct_init_t struct_init;
    ast_expr_struct_member_init_t struct_member_init;
    ast_expr_borrow_t borrow;
    ast_expr_variant_decomp_t variant_decomp;
    ast_expr_block_t block;
    ast_expr_match_branch_t match_branch;
    ast_expr_match_t match_expr;
    ast_expr_closure_t closure;
    ast_expr_list_literal_t list_literal;
    ast_expr_ternary_if_t ternary_if;
    ast_expr_wrapped_t compt_expr;
    ast_expr_wrapped_t static_assert_expr;
    ast_expr_two_types_t inferable_as_expr;
    ast_expr_type_t type_to_str;
    ast_expr_two_types_t same_type;
    ast_expr_defined_t defined;
    ast_expr_has_contract_t has_contract;
} ast_expr_u;

/// underlying expr is 0-offset aligned so this struct can be safely downcasted
typedef struct ast_expr {
    ast_expr_u expr;
    ast_expr_type_e type;
    token_t* first;
    token_t* last;
} ast_expr_t;

#ifdef __cplusplus
} // extern "C"
#endif
#endif // !AST_EXPRESSIONS_H
