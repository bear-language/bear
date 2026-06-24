//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef AST_STATEMENTS_H
#define AST_STATEMENTS_H
#include "compiler/ast/expr.h"
#include "compiler/ast/params.h"
#include "compiler/ast/stmt_slice.h"
#include "compiler/ast/type.h"
#include "compiler/token.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum ast_statement_type {
    // --------- declaration statements ---------
    // blocks
    AST_STMT_FILE, // top-level flat seq of statements w/ metadata
    AST_STMT_EXTERN_BLOCK,

    // var decls
    AST_STMT_VAR_DECL,      // type foo;
    AST_STMT_VAR_INIT_DECL, // type foo = something;

    // module stuff
    AST_STMT_MODULE,

    // stmt decl modifiers
    AST_STMT_VISIBILITY_MODIFIER,
    AST_STMT_COMPT_MODIFIER,
    AST_STMT_STATIC_MODIFIER,
    AST_STMT_ALIGNAS_MODIFIER,

    // structures
    AST_STMT_STRUCT_DEF,
    AST_STMT_CONTRACT_DEF,
    AST_STMT_UNION_DEF,

    // variants
    AST_STMT_VARIANT_DEF,
    AST_STMT_VARIANT_FIELD_DECL,

    // function declarations
    AST_STMT_FN_DECL,
    AST_STMT_FN_PROTOTYPE,

    AST_STMT_DEFTYPE,

    // --------- directives ---------
    // import
    AST_STMT_IMPORT,

    // use foo;
    AST_STMT_USE,

    // --------- pure statements ---------

    AST_STMT_BLOCK, // {...} sequence of statements

    // statement expr
    AST_STMT_EXPR,

    // statement empty
    AST_STMT_EMPTY,

    // break statement
    AST_STMT_BREAK,

    // control flow
    AST_STMT_IF,
    AST_STMT_ELSE,
    AST_STMT_WHILE,
    AST_STMT_FOR,
    AST_STMT_FOR_IN,
    AST_STMT_RETURN,
    AST_STMT_YIELD,
    AST_STMT_CONTINUE,

    AST_STMT_INVALID,
} ast_stmt_type_e;

// forward decls ~~~~~~~~~~~~~~~~~~
typedef struct ast_stmt ast_stmt_t;
typedef struct ast_stmt_else ast_stmt_else_t;
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

/// generic parmeters

typedef enum ast_generic_parameter_e {
    AST_GENERIC_PARAM_TYPE,
    AST_GENERIC_PARAM_VAR,
    AST_GENERIC_PARAM_INVALID,
} ast_generic_parameter_e;

typedef struct ast_generic_parameter_u {
    ast_param_t* generic_var;
    ast_type_with_contracts_t* generic_type;
} ast_generic_parameter_u;

typedef struct ast_generic_parameter {
    ast_generic_parameter_u param;
    ast_generic_parameter_e tag;
    token_t* first;
    token_t* last;
} ast_generic_parameter_t;

/// this is the <T has(foo, bar), var N> clause
typedef struct ast_slice_of_generic_params {
    ast_generic_parameter_t** start;
    size_t len;
} ast_slice_of_generic_params_t;

// stmt types ~~~~~~~~~~~~~~~~~~~~
/**
 * represents block statements
 * {...series of statements...}
 */
typedef struct ast_stmt_block {
    ast_slice_of_stmts_t stmts;
} ast_stmt_block_t;

/**
 * represents a top level sequence of statements in a file
 * this is the highest-order statement construct
 */
typedef struct ast_stmt_file {
    const char* file_name;
    ast_slice_of_stmts_t stmts;
} ast_stmt_file_t;

/**
 * represents a module decl or insertion in a module
 * mod my_mod {...} // module will enclosed
 */
typedef struct ast_stmt_module {
    token_t* id;
    ast_slice_of_stmts_t decls;
} ast_stmt_module_t;

/**
 * imports a file given my a path as my.path.to.file
 */
typedef struct ast_stmt_import {
    /// NULLable/optional
    token_t* extern_language;
    token_t* file_path;
    /// introduce inside a module
    token_ptr_slice_t into_mod;
    bool has_into_mod;
} ast_stmt_import_t;

/// bring a module into current scope
typedef struct ast_stmt_use {
    token_ptr_slice_t id;
    bool mod;
} ast_stmt_use_t;

/// a statement expr, like `foo();`
typedef struct ast_stmt_expr {
    /// sole-expr of this statement
    ast_expr_t* expr;
} ast_stmt_expr_t;

typedef struct ast_stmt_fn_decl {
    /// fn, mt, or dt
    token_t* kw;
    token_ptr_slice_t name;
    ast_slice_of_generic_params_t generic_params;
    ast_slice_of_params_t params;
    /// NULLable if no return type
    token_t* ret_arrow;
    /// NULLable if no return type
    ast_type_t* return_type;
    ast_stmt_t* block;
    ast_expr_t* expr;
    bool only_expr;
    bool is_generic;
    bool is_mut;
    bool discardable;
} ast_stmt_fn_decl_t;

typedef struct ast_stmt_var_decl_init {
    ast_type_t* type;
    token_t* name;
    token_t* assign_op;
    ast_expr_t* rhs;
} ast_stmt_var_decl_init_t;

typedef struct ast_stmt_var_decl {
    ast_type_t* type;
    token_t* name;
} ast_stmt_var_decl_t;

typedef struct ast_stmt_if {
    ast_expr_t* condition;
    ast_stmt_t* body_stmt;
    /// NULLable if there's no else
    ast_stmt_t* else_stmt;
    bool has_else;
    bool compt;
} ast_stmt_if_t;

typedef struct ast_stmt_else {
    ast_stmt_t* body_stmt;
} ast_stmt_else_t;

typedef struct ast_stmt_while {
    ast_expr_t* condition;
    ast_stmt_t* body_stmt;
} ast_stmt_while_t;

/// C-style for <int>; <cond>; <step> {...}
typedef struct ast_stmt_for {
    ast_stmt_t* init;
    ast_expr_t* condition;
    ast_expr_t* step;
    ast_stmt_t* body_stmt;
} ast_stmt_for_t;

/// for x in thing {...}
typedef struct ast_stmt_for_in {
    ast_param_t* each;
    ast_expr_t* iterator;
    ast_stmt_t* body_stmt;
} ast_stmt_for_in_t;

typedef struct ast_stmt_return {
    // optional
    ast_expr_t* expr;
} ast_stmt_return_t;

typedef struct ast_stmt_struct_decl {
    token_t* name;
    ast_slice_of_generic_params_t generic_params;
    /// contracts.len == 0 indicates no contracts
    ast_slice_of_exprs_t contracts;
    ast_slice_of_stmts_t fields;
    bool is_generic;
} ast_stmt_struct_decl_t;

typedef struct ast_stmt_contract_decl {
    ast_slice_of_stmts_t fields;
    ast_slice_of_generic_params_t generic_params;
    token_t* name;
    bool is_generic;
} ast_stmt_contract_decl_t;

typedef struct ast_stmt_empty {
    token_t* terminator;
} ast_stmt_empty_t;

typedef struct ast_stmt_vis_modifier {
    token_t* modifier;
    ast_stmt_t* stmt;
} ast_stmt_vis_modifier_t;

typedef struct ast_stmt_wrapped {
    ast_stmt_t* stmt;
} ast_stmt_wrapped_t;

typedef struct ast_stmt_union_decl {
    token_t* name;
    ast_slice_of_stmts_t fields;
} ast_stmt_union_decl_t;

typedef struct ast_stmt_variant_decl {
    token_t* name;
    ast_slice_of_generic_params_t generic_params;
    ast_slice_of_stmts_t fields;
    bool is_generic;
} ast_stmt_variant_decl_t;

typedef struct ast_stmt_variant_field_decl {
    token_t* name;
    ast_slice_of_params_t params;
} ast_stmt_variant_field_decl_t;

typedef struct ast_stmt_extern_block {
    token_t* extern_language;
    ast_slice_of_stmts_t decls;
} ast_stmt_extern_block_t;

typedef struct ast_stmt_deftype {
    token_t* alias_id;
    /// of type AST_EXPR_TYPE or AST_UNARY where token_t* op = typeof
    ast_expr_t* aliased_type_expr;
} ast_stmt_deftype_t;

typedef struct ast_stmt_alignas {
    ast_expr_t* align_expr;
    ast_stmt_t* inner;
} ast_stmt_alignas_t;

// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

/// union of all stmt types
typedef union ast_stmt_u {
    ast_stmt_block_t block;
    ast_stmt_file_t file;
    ast_stmt_module_t module;
    ast_stmt_import_t import;
    ast_stmt_use_t use;
    ast_stmt_expr_t stmt_expr;
    ast_stmt_fn_decl_t fn_decl;
    ast_stmt_fn_decl_t fn_prototype;
    ast_stmt_var_decl_init_t var_init_decl;
    ast_stmt_var_decl_t var_decl;
    ast_stmt_if_t if_stmt;
    ast_stmt_else_t else_stmt;
    ast_stmt_while_t while_stmt;
    ast_stmt_for_t for_stmt;
    ast_stmt_for_in_t for_in_stmt;
    ast_stmt_return_t return_stmt;
    ast_stmt_struct_decl_t struct_decl;
    ast_stmt_contract_decl_t contract_decl;
    ast_stmt_union_decl_t union_decl;
    ast_stmt_empty_t empty;
    ast_stmt_vis_modifier_t vis_modifier;
    ast_stmt_wrapped_t compt_modifier;
    ast_stmt_variant_decl_t variant_decl;
    ast_stmt_variant_field_decl_t variant_field_decl;
    ast_stmt_return_t yield_stmt;
    ast_stmt_wrapped_t static_modifier;
    ast_stmt_deftype_t deftype;
    ast_stmt_extern_block_t extern_block;
    ast_stmt_alignas_t alignaz;
} ast_stmt_u;

typedef struct ast_stmt {
    ast_stmt_u stmt;
    ast_stmt_type_e type;
    token_t* first;
    token_t* last;
} ast_stmt_t;

#ifdef __cplusplus
} // extern "C"
#endif
#endif // !AST_STATEMENTS
