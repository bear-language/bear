//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_DIAGNOSTICS_SRC_VIEW_H
#define COMPILER_DIAGNOSTICS_SRC_VIEW_H
#include "utils/file_io.h"
#include "utils/string.h"
#include "utils/string_view.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * builds a string view that displays source code
 */
string_view_t get_line_string_view(const src_buffer_t* src_buffer, const char* start);

/**
 * gets a cursor pointing to an error token, should be used with the string_view_t from
 * get_line_string_view
 */
string_t get_cursor_string(string_view_t line_view, size_t len, size_t col, const char* ansi_color);

#ifdef __cplusplus
}
#endif

#endif // !COMPILER_DIAGNOSTICS_SRC_VIEW_H
