//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "utils/string.h"
#include "stdbool.h"
#include "utils/vector.h"
#include <stddef.h>
#include <string.h>

#define STRING_DEFAULT_CAP 8

// ctor
string_t string_create(void) {
    string_t string = {.vec = vector_create_and_reserve(sizeof(char), STRING_DEFAULT_CAP)};
    ((char*)string.vec.data)[0] = '\0'; // ensure null-term
    return string;
}
string_t string_create_and_reserve(size_t capacity) {
    string_t string = {.vec = vector_create_and_reserve(sizeof(char), capacity)};
    ((char*)string.vec.data)[0] = '\0'; // ensure null-term
    return string;
}

string_t string_from(const char* null_term_string) {
    if (!null_term_string) {
        return string_create();
    }
    size_t len = strlen(null_term_string);
    string_t new_string = string_create_and_reserve(len);

    char c;
    while ((c = *null_term_string++)) {
        string_push_char(&new_string, c);
    }
    return new_string;
}

string_t string_create_and_fill(size_t len, char c) {
    string_t str = string_create_and_reserve(len);
    for (size_t i = 0; i < len; i++) {
        string_push_char(&str, c);
    }
    return str;
}

// dtor
void string_destroy(string_t* string) { vector_destroy(&string->vec); }

// getters
char* string_data(const string_t* string) { return string->vec.data; }
size_t string_size(const string_t* string) { return string->vec.size; }
size_t string_capacity(const string_t* string) { return string->vec.capacity; }

// idx
char* string_at_ptr(const string_t* string, size_t idx) { return vector_at(&string->vec, idx); }
char string_at(const string_t* string, size_t idx) { return *(char*)vector_at(&string->vec, idx); }
char* string_start(const string_t* string) { return vector_start(&string->vec); }

// modifiers
bool string_push_char(string_t* string, char c) {
    bool resize = vector_push_back(&string->vec, &c);
    char null_term = '\0';
    resize |= vector_push_back(&string->vec,
                               &null_term); // ensure null term, this will resize if necessary
    string->vec.size--; // tamper a bit, should be fine; we're doing this so that the
                        // null-terminator can be overwrittern and overwrittern safely
    // since every push_char will do a double push, there should always be room for the null-term
    // shrink to fit is also not supported due to this shortcut
    return resize;
}

void string_push_string(string_t* dest, const string_t* src) {
    for (size_t i = 0; i < string_size(src); i++) {
        string_push_char(dest, string_at(src, i));
    }
}

void string_push_cstr(string_t* string, const char* cstr) {
    if (!cstr) {
        return;
    }
    for (; *cstr; cstr++) {
        string_push_char(string, *cstr);
    }
}

void string_push_strn(string_t* string, const char* str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        string_push_char(string, str[i]);
    }
}

char string_pop_char(string_t* string) {
    size_t size = string->vec.size;
    char c = ((char*)string->vec.data)[size - 1]; // get last char
    ((char*)string->vec.data)[size - 1] = '\0';   // ensure null-term
    string->vec.size--;                           // dec size accordingly
    return c;
}
void string_reserve(string_t* string, size_t new_capacity) {
    vector_reserve(&string->vec, new_capacity);
}

void string_shrink_by(string_t* string, size_t n) {
    if (string->vec.size < n) {
        return;
    }
    string->vec.size -= n;
    ((char*)string->vec.data)[string->vec.size] = '\0'; // guranteed safe indexing since we shrunk
}
