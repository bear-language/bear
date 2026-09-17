//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_AST_PARAMS
#define COMPILER_AST_PARAMS
#include "compiler/token.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ast_type ast_type_t;

typedef struct ast_param {
    ast_type_t* type;
    token_t* name;
    token_t* first;
    token_t* last;
    bool valid;
} ast_param_t;

typedef struct ast_slice_of_params_t {
    ast_param_t** start;
    size_t len;
} ast_slice_of_params_t;

#ifdef __cplusplus
} // extern "C"
#endif
#endif // !COMPILER_AST_PARAMS
