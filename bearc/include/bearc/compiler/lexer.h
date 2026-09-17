//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_LEXER
#define COMPILER_LEXER

#include "utils/file_io.h"
#include "utils/vector.h"

#define LEXER_ESTIMATED_CHARS_PER_TOKEN 6

#ifdef __cplusplus
extern "C" {
#endif

/**
 * create a vector_t storing token_t from a specified src_buffer_t
 */
vector_t lexer_tokenize_src_buffer(const src_buffer_t* buf);

#ifdef __cplusplus
}
#endif

#endif // !COMPILER_LEXER
