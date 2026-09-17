//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef UTILS_SPILL_ARR
#define UTILS_SPILL_ARR
#include "utils/vector.h"
#include <stddef.h>
#include <stdint.h>

#define SPILL_ARR_SIZE_8_T_ARR_CAP 128

#ifdef __cplusplus
extern "C" {
#endif

/// dynamic spill array with a small stack buffer extended by the heap once size grows large enough
typedef struct spill_arr_ptr {
    vector_t vec;
    size_t size;
    size_t arr_cap;
    void* data[SPILL_ARR_SIZE_8_T_ARR_CAP];
} spill_arr_ptr_t;

void spill_arr_ptr_init(spill_arr_ptr_t* sarr);

void** spill_arr_ptr_emplace(spill_arr_ptr_t* sarr);

void spill_arr_ptr_push(spill_arr_ptr_t* sarr, void* ptr);

void** spill_arr_ptr_at(spill_arr_ptr_t* sarr, size_t n);

void spill_arr_ptr_destroy(spill_arr_ptr_t* sarr);

void spill_arr_ptr_flat_copy(void** dest, spill_arr_ptr_t* sarr);

#ifdef __cplusplus
}
#endif

#endif // !UTILS_SPILL_ARR
