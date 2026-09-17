//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef UTILS_STRING
#define UTILS_STRING
#include "utils/vector.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * string type that owns its own resources. string_destory(string_t) before a string goes out of
 * scope.
 * the underlying vector should not be modified manually
 */
typedef struct string {
    vector_t vec; // not meant to be touched
} string_t;

/// ctor
string_t string_create(void);
/// must be a null-terminated string, ideally a string literal
string_t string_from(const char* null_term_string);
string_t string_create_and_reserve(size_t capacity);
/// creates a string of a specified length filled with the specified character
string_t string_create_and_fill(size_t len, char c);

/// dtor
void string_destroy(string_t* string);

/// gets underlying data mutably
char* string_data(const string_t* string);
/// gets the size, in bytes
size_t string_size(const string_t* string);
/// gets the capacity, in bytes
size_t string_capacity(const string_t* string);

/// get the char* at an index witin the underlying char[]
char* string_at_ptr(const string_t* string, size_t idx);
/// get the char at an index within the underlying char[]
char string_at(const string_t* string, size_t idx);
/// get the char at the start of the string
char* string_start(const string_t* string);

/// push a char to the end of the string
bool string_push_char(string_t* string, char c);
/// push a string_t onto a string_t
void string_push_string(string_t* dest, const string_t* src);
/// push a standard C-string onto a string_t
void string_push_cstr(string_t* string, const char* cstr);
/// push a char* with a specified length
void string_push_strn(string_t* string, const char* str, size_t len);
/// pops a char of the back of the string
char string_pop_char(string_t* string);
/// reserves some capacity in the string, does nothing if the new_capacity is less than the size
void string_reserve(string_t* string, size_t new_capacity);

/// removes a specified number of chars from the back of the string
void string_shrink_by(string_t* string, size_t n);

#ifdef __cplusplus
}
#endif

#endif // !UTILS_STRING
