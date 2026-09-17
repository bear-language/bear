//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/parser/parse_token_slice.h"
#include "compiler/diagnostics/error_codes.h"
#include "compiler/parser/parser.h"
#include "compiler/parser/token_eaters.h"
#include "compiler/token.h"
#include "utils/spill_arr.h"

token_ptr_slice_t parser_freeze_token_ptr_slice(parser_t* p, spill_arr_ptr_t* sarr) {
    token_ptr_slice_t slice = {
        .start = (token_t**)arena_alloc(p->arena, sarr->size * sizeof(token_t*)),
        .len = sarr->size,
    };
    spill_arr_ptr_flat_copy((void**)slice.start, sarr);
    spill_arr_ptr_destroy(sarr);
    return slice;
}

token_ptr_slice_t parse_id_token_slice(parser_t* p, token_type_e divider) {
    spill_arr_ptr_t sarr;
    spill_arr_ptr_init(&sarr);
    if (!token_is_builtin_type_or_id(parser_peek(p)->type)) {
        compiler_error_list_emplace(p->error_list, parser_peek(p), ERR_EXPECTED_IDENTIFER);
        return parser_freeze_token_ptr_slice(p, &sarr);
    }
    do {
        token_t* next = parser_eat(p);
        if (!token_is_builtin_type_or_id(next->type)) {
            compiler_error_list_emplace(p->error_list, parser_prev(p), ERR_EXPECTED_IDENTIFER);
            break;
        }
        spill_arr_ptr_push(&sarr, next);
    } while (parser_match_token(p, divider));
    return parser_freeze_token_ptr_slice(p, &sarr);
}
