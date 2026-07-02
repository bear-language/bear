//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/diagnostics/src_view.h"
#include "utils/ansi_codes.h"
#include "utils/file_io.h"
#include "utils/string.h"
#include "utils/string_view.h"

string_view_t get_line_string_view(const src_buffer_t* src_buffer, const char* start) {
    if (start == NULL) {
        string_view_t sv = {.start = src_buffer->data, .len = 1};
        return sv;
    }
    // find start of line
    ptrdiff_t diff_to_line_start = 0;
    while (start - diff_to_line_start >= src_buffer->data
           && *(start - diff_to_line_start) != '\n') {
        diff_to_line_start++;
    }
    diff_to_line_start--; // backtrack once because we overshot in the final increment

    // find end of line
    ptrdiff_t diff_to_line_end = 0;
    while (*(start + diff_to_line_end) != '\n' && *(start + diff_to_line_end) != '\0'
           && start + diff_to_line_end < src_buffer->data + src_buffer->src_len) {
        diff_to_line_end++;
    }

    string_view_t sv
        = {.start = start - diff_to_line_start, .len = diff_to_line_start + diff_to_line_end};
    return sv;
}

string_t get_cursor_string(string_view_t line_view, size_t len, size_t col,
                           const char* ansi_color) {
    // we're at eof (only possible tok with len zero, so go back)
    if (len == 0 && line_view.len > 0 && col > 0) {
        ++len;
        --col;
    }
    string_t str = string_create_and_reserve(line_view.len + 20); // room for ansi strings
    string_push_cstr(&str, ansi_color);
    for (size_t i = 0; i < line_view.len; i++) {
        if (i >= col && i < col + len) {
            string_push_char(&str, '^');
            continue;
        }
        if (i >= col + len) {
            break;
        }
        string_push_char(&str, ' ');
    }
    string_push_cstr(&str, ansi_reset());
    return str;
}
