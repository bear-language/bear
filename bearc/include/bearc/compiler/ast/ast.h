//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef AST_AST_H
#define AST_AST_H

#include "compiler/ast/stmt.h"
#include "compiler/diagnostics/error_list.h"
#include "utils/file_io.h"
#include "utils/vector.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct br_ast {
    src_buffer_t src_buffer;
    vector_t tokens;
    arena_t arena;
    /// root ast node
    ast_stmt_t* file_stmt_root_node;
    compiler_error_list_t error_list;
} br_ast_t;

br_ast_t ast_create_from_file(const char* file_name);

br_ast_t ast_create_from_string_literal(const char* name, const char* string_literal);

void ast_destroy(br_ast_t* ast);

#ifdef __cplusplus
}
#endif

#endif
