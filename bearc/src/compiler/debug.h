//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_DEBUG_H
#define COMPILER_DEBUG_H

#include "utils/file_io.h"
#include "utils/vector.h"
#ifdef __cplusplus
extern "C" {
#endif

void print_out_src_buffer(const src_buffer_t* src_buffer);
void print_out_tkn_table(const vector_t* vec);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
