//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/parser/parser.h"
#include "utils/arena.h"
#include <stddef.h>
parser_t parser_create(vector_t* tokens, arena_t* arena, compiler_error_list_t* error_list) {
    parser_t parser = {.tokens = tokens,
                       .pos = 0,
                       .arena = arena,
                       .error_list = error_list,
                       .prev_discarded = false,
                       .mode = PARSER_MODE_DEFAULT,
                       .yield_depth = 0,
                       .no_warn = false};
    return parser;
}

void parser_mode_set(parser_t* p, parser_mode_e mode) { p->mode = mode; }

void parser_mode_reset(parser_t* p) { p->mode = PARSER_MODE_DEFAULT; }

void parser_warn(parser_t* p, token_t* token, error_code_e error_code) {
    if (!p->no_warn) {
        compiler_error_list_emplace(p->error_list, token, error_code);
    }
}

parser_mode_e parser_mode(parser_t* p) { return p->mode; }
