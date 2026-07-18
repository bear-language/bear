//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_AST_TYPE
#define COMPILER_AST_TYPE

#include "compiler/ast/expr.h"
#include "compiler/token.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum ast_type_tag {
    AST_TYPE_BASE,
    AST_TYPE_REF_PTR,
    AST_TYPE_ARR,
    AST_TYPE_SLICE,
    AST_TYPE_GENERIC,
    AST_TYPE_FN_PTR,
    AST_TYPE_TYPEOF,
    AST_TYPE_DECAY,
    AST_TYPE_INVALID,
} ast_type_tag_e;

typedef struct ast_type_base {
    token_ptr_slice_t id;
    bool mut;
} ast_type_base_t;

// shared by tags AST_TYPE_REF and AST_TYPE_PTR
typedef struct ast_type_ref {
    ast_type_t* inner;
    token_t* modifier; // & or *
    bool mut;
} ast_type_ref_t;

typedef struct ast_type_arr {
    ast_type_t* inner;
    ast_expr_t* size_expr;
} ast_type_arr_t;

typedef struct ast_type_slice {
    ast_type_t* inner;
    bool mut;
} ast_type_slice_t;

typedef struct ast_type_generic {
    ast_type_t* inner;
    ast_slice_of_generic_args_t generic_args;
} ast_type_generic_t;

typedef struct ast_slice_of_types {
    ast_type_t** start;
    size_t len;
} ast_slice_of_types_t;

typedef struct ast_type_fn_ptr {
    ast_slice_of_types_t param_types;
    /// optional if void
    ast_type_t* return_type;
    bool mut;
} ast_type_fn_ptr_t;

typedef struct ast_type_wrapped {
    ast_type_t* inner;
} ast_type_wrapped_t;

typedef struct ast_type_of {
    ast_expr_t* of_expr;
    bool mut;
} ast_type_of_t;

typedef union ast_type_u {
    ast_type_base_t base;
    ast_type_ref_t ptr_ref;
    ast_type_arr_t arr;
    ast_type_generic_t generic;
    ast_type_slice_t slice;
    ast_type_fn_ptr_t fn_ptr;
    ast_type_of_t type_of;
    ast_type_wrapped_t decay;
} ast_type_u;

typedef struct ast_type {
    ast_type_u type;
    ast_type_tag_e tag;
    ast_type_t* canonical_base;
    token_t* first;
    token_t* last;
} ast_type_t;

typedef struct ast_type_with_contracts {
    token_t* id;
    ast_slice_of_exprs_t contract_ids;
    bool valid;
} ast_type_with_contracts_t;

#endif // !COMPILER_AST_TYPE
