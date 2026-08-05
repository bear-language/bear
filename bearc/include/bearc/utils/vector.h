//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef UTILS_VECTOR_H
#define UTILS_VECTOR_H
#include "utils/log.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// generic vector, based on void* and tracks indices via an elem_size parameter
/// - stores types of ONE size (logic breaks if you try to do heterogenous storage)
/// - upon contruction, based in sizeof(some_type_t) for proper usage
/// - remember to document/track what type you're stroing internally. Ideally use naming like:
/// ```
/// vector_t vec_int = vector_create(sizeof(int)); // ctor call
/// ```
typedef struct vector {
    void* data;
    size_t size;
    size_t capacity;
    size_t elem_size;
} vector_t;

/// ctor
vector_t vector_create(size_t elem_size);
/// ctor, w/ initial capacity
vector_t vector_create_and_reserve(size_t elem_size, size_t capacity);
/// ctor, w/ initial size and zero-initialization
vector_t vector_create_and_init(size_t elem_size, size_t size);
/// dtor, use or you will get leaks (frees internal arr)
void vector_destroy(vector_t* vector);

/// gets the underlying data ptr
void* vector_data(const vector_t* vector);
/// gets size
size_t vector_size(const vector_t* vector);
/// gets capacity
size_t vector_capacity(const vector_t* vector);
/// gets element size
size_t vector_elem_size(const vector_t* vector);

/// get void* at some idx, cast this!
static inline void* vector_at(const vector_t* vector, size_t idx) {
    if (idx >= vector->size) {
        LOG_ERR("[ERROR|vector_at] out of range")
        return NULL; // out of bounds
    }
    return (void*)((unsigned char*)vector->data + (idx * vector->elem_size));
}

/// get void* at 0 idx, cast this!
void* vector_start(const vector_t* vector);
/// get void* at vec[size], cast this!
void* vector_end(const vector_t* vector);
/// get void* at vec[size - 1], cast this!
void* vector_last(const vector_t* vector);

/// push back an element onto the vector, copies by value, but you still need to pass in a pointer
bool vector_push_back(vector_t* vector, const void* elem);
/// grows the vector by one element and returns a pointer to the uninitialized slot at the back.
/// caller writes the element directly.
void* vector_emplace_back(vector_t* vector);

/// removes the last element
void vector_remove_back(vector_t* vector);
/// reserve some internal capacity, will either realloc or fail if new_capacity < current
void vector_reserve(vector_t* vector, size_t new_capacity);
// reallocs so that size = capacity
bool vector_shrink_to_fit(vector_t* vector);

#ifdef __cplusplus
}
#endif

#endif // !UTILS_VECTOR_H
