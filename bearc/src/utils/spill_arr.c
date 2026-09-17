//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "utils/spill_arr.h"
#include "utils/vector.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void spill_arr_ptr_init(spill_arr_ptr_t* sarr) {
    sarr->size = 0;
    sarr->arr_cap = SPILL_ARR_SIZE_8_T_ARR_CAP;
    // vec uninit until we need it,
    // arr uninit'd
}

void** spill_arr_ptr_emplace(spill_arr_ptr_t* sarr) {
    size_t idx = sarr->size++;

    if (idx < sarr->arr_cap) {
        return sarr->data + idx;
    }

    if (idx == sarr->arr_cap) {
        sarr->vec = vector_create_and_reserve(sizeof(void*), SPILL_ARR_SIZE_8_T_ARR_CAP);
    }

    return (void**)vector_emplace_back(&sarr->vec);
}

void spill_arr_ptr_push(spill_arr_ptr_t* sarr, void* ptr) { *spill_arr_ptr_emplace(sarr) = ptr; }

void** spill_arr_ptr_at(spill_arr_ptr_t* sarr, size_t n) {
    if (n >= sarr->size) {
        return NULL;
    }
    if (n < sarr->arr_cap) {
        return (sarr->data + n);
    }
    // assume internal buff is 64
    // say we ask for idx 64, we actually get 64 - 64 = vec[0]
    assert(sarr->vec.elem_size == sizeof(void*)
           && "[spill_arr_8_at] internal vector elem_size corrupted\n");
    return (void**)vector_at(&sarr->vec, n - sarr->arr_cap);
}

void spill_arr_ptr_destroy(spill_arr_ptr_t* sarr) {
    if (sarr->size > sarr->arr_cap) {
        vector_destroy(&sarr->vec);
    }
}

void spill_copy(void** dest, spill_arr_ptr_t* sarr) {
    memcpy((void*)dest, (void*)sarr->data, sarr->arr_cap * sizeof(void*));
    memcpy((void*)(dest + sarr->arr_cap), sarr->vec.data, sarr->vec.size * sizeof(void*));
}

void spill_arr_ptr_flat_copy(void** dest, spill_arr_ptr_t* sarr) {
    if (sarr->size <= sarr->arr_cap) {
        memcpy((void*)dest, (void*)sarr->data, sarr->size * sizeof(void*));
    } else {
        spill_copy(dest, sarr);
    }
}
