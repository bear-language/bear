//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "utils/vector.h"
#include "utils/log.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// returns a new vector by value, which will eventually need to be destructed by vector_destroy
vector_t vector_create(size_t elem_size) {
    vector_t vec = {.size = 0, .capacity = 2, .elem_size = elem_size};
    vec.data = malloc(elem_size * vec.capacity);
    if (!vec.data) {
        // if malloc fails
        vec.capacity = 0;
    }
    return vec;
}

// returns a new vector with specified capacity
vector_t vector_create_and_reserve(size_t elem_size, size_t capacity) {
    vector_t vec = {.size = 0, .capacity = capacity, .elem_size = elem_size};
    vec.data = malloc(elem_size * vec.capacity);
    if (!vec.data) {
        // if malloc fails
        vec.capacity = 0;
    }
    return vec;
}

// returns a vector with specified number of zero-initialized elements
vector_t vector_create_and_init(size_t elem_size, size_t size) {
    vector_t vec = {.size = 0, .capacity = size, .elem_size = elem_size};
    vec.data = calloc(vec.capacity, elem_size); // zero-init
    if (!vec.data) {
        vec.capacity = 0;
        vec.size = 0;
    }
    return vec;
}

void vector_destroy(vector_t* vector) {
    free(vector->data);
    vector->data = NULL;
    vector->size = 0;
    vector->capacity = 0;
}

// gets ptr to underlying data
void* vector_data(const vector_t* vector) { return vector->data; }
// gets size of vector
size_t vector_size(const vector_t* vector) { return vector->size; }
// gets capacity of vector
size_t vector_capacity(const vector_t* vector) { return vector->capacity; }
// gets element size of vector
size_t vector_elem_size(const vector_t* vector) { return vector->elem_size; }

// gets value at specified index
void* vector_at(const vector_t* vector, size_t idx) {
    if (idx >= vector->size) {
        LOG_ERR("[ERROR|vector_at] out of range")
        return NULL; // out of bounds
    }
    return (void*)((unsigned char*)vector->data + (idx * vector->elem_size));
}
// gets ptr to start of vector
void* vector_start(const vector_t* vector) { return vector->data; }
// gets ptr to end of vector
void* vector_end(const vector_t* vector) {
    return (void*)((char*)vector->data + ((vector->size) * vector->elem_size));
}
// gets ptr to last element of vector (size - 1)
void* vector_last(const vector_t* vector) {
    if (vector->size == 0) {
        return NULL;
    }
    return (void*)((char*)vector->data + ((vector->size - 1) * vector->elem_size));
}

// modifiers

// pushes element to end of vector; invalidates iterators on resize
// - returns true on resize and false otherwise
bool vector_push_back(vector_t* vector, const void* elem) {
    bool resize = false;
    if (vector->size == vector->capacity) {
        size_t new_capacity = (vector->capacity == 0) ? 1 : vector->capacity * 2;
        void* temp = realloc(vector->data, new_capacity * vector->elem_size);
        if (!temp) {
            LOG_ERR("[ERROR|vector_push_back] reallocation failed when increasing capacity");
            return false; // vector still valid, but push back fails w/ msg
        }
        vector->data = temp;
        vector->capacity = new_capacity;
        resize = true;
    }
    memcpy((unsigned char*)vector->data + (vector->size * vector->elem_size), elem,
           vector->elem_size);
    ++vector->size;
    return resize;
}

void* vector_emplace_back(vector_t* vector) {
    if (vector->size == vector->capacity) {
        size_t new_capacity = (vector->capacity == 0) ? 1 : vector->capacity * 2;
        void* temp = realloc(vector->data, new_capacity * vector->elem_size);
        if (!temp) {
            LOG_ERR("[ERROR|vector_emplace_back] reallocation failed when increasing capacity");
            return NULL; // vector still valid, but push back fails w/ msg
        }
        vector->data = temp;
        vector->capacity = new_capacity;
    }
    ++vector->size;
    return vector_at(vector, vector->size - 1);
}

// removes last element of vector
void vector_remove_back(vector_t* vector) {
    if (vector->size == 0) {
        return; // fail silently cuz empty
    }
    --vector->size;
}

// reserves a specified capacity
void vector_reserve(vector_t* vector, size_t new_capacity) {
    if (new_capacity <= vector->capacity) {
        return; // we fine
    }
    void* temp = realloc(vector->data, new_capacity * vector->elem_size);
    if (!temp) {
        LOG_ERR("[ERROR|vector_reserve] reallocation failed");
        return; // vector still valid, but fails w/ msg
    }
    vector->data = temp;
    vector->capacity = new_capacity;
}

// shrinks capacity to match size; invalidates iterators on resize
// - returns true on resize and false otherwise
bool vector_shrink_to_fit(vector_t* vector) {
    if (vector->size == vector->capacity) {
        return false;
    }
    void* temp = realloc(vector->data, vector->size * vector->elem_size);
    if (!temp) {
        LOG_ERR("[ERROR|vector_shrink_to_fit] vector_t: reallocation failed");
        return false;
    }
    vector->data = temp;
    vector->capacity = vector->size;
    return true;
}
