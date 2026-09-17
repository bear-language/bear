//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.
#ifndef COMPILER_AST_STMT_SLICE
#define COMPILER_AST_STMT_SLICE

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ast_stmt ast_stmt_t;

/**
 * slice of statements
 */
typedef struct ast_slice_of_stmts {
    ast_stmt_t** start;
    size_t len;
} ast_slice_of_stmts_t;

#ifdef __cplusplus
} // extern "C"
#endif
#endif
