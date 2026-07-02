//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// a string buffer, created from a file
typedef struct src_buffer {
    /// non-owning view into filename
    const char* file_name;
    /// owns, freed by destroy_char_buffer_from_file
    char* data;
    // size of char buffer, in bytes/chars
    size_t size;
    /// length of src file
    size_t src_len;
    bool owns_data;
} src_buffer_t;

/// creates an src_buffer_t from a file,
/// - must call src_buffer_destroy(src_buffer_t*) to free resources
src_buffer_t src_buffer_from_file_create(const char* file_name);
/// creates an src_buffer_t from a file (name string with a specifed length)
/// - must call src_buffer_destroy(src_buffer_t*) to free resources
src_buffer_t src_buffer_from_file_createn(const char* file_name, size_t name_len);

src_buffer_t src_buffer_from_string_literal(const char* literal_file_name,
                                            const char* literal_buffer);
/// frees the underlying buffer
void src_buffer_destroy(src_buffer_t* buffer);
/// gets a ptr to the underlying string buffer of the src file
const char* src_buffer_get(src_buffer_t* buffer);
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

bool file_exists(const char* file_name);

#ifdef __cplusplus
}
#endif

#endif // !FILE_IO_H
