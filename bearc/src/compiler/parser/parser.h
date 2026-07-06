//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H
#include "compiler/diagnostics/error_list.h"
#include "utils/arena.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "utils/vector.h"

typedef enum {
    PARSER_MODE_DEFAULT = 0,
    PARSER_MODE_BAN_ANGLE_BRACKETS_IN_EXPRS,
    PARSER_MODE_IN_LOOP,
    PARSER_MODE_BAN_STRUCT_INIT,
    PARSER_MODE_BAN_BAR,
    PARSER_MODE__NUM,
} parser_mode_e;

/**
 * primary parser structure
 * tracks a position ptr along a vector of token_t
 * - does not own anything!
 */
typedef struct {
    vector_t* tokens;
    size_t pos;
    arena_t* arena;
    compiler_error_list_t* error_list;
    parser_mode_e mode;
    bool prev_discarded;
} parser_t;

parser_t parser_create(vector_t* tokens, arena_t* arena, compiler_error_list_t* error_list);

void parser_mode_set(parser_t* p, parser_mode_e mode);

void parser_mode_reset(parser_t* p);

parser_mode_e parser_mode(parser_t* p);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // !COMPILER_PARSER_H
