//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef UTILS_STRIMAP_H
#define UTILS_STRIMAP_H

#include "utils/arena.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STRIMAP_MINIMUM_CAPACITY 8
#define STRIMAP_LOAD_FACTOR .75

#ifdef __cplusplus
extern "C" {
#endif

/// an entry in the string to int map
typedef struct strimap_entry {
    char* key;
    size_t len;
    int32_t val;
    struct strimap_entry* next;
} strimap_entry_t;

/**
 * strimap_t, as in string-to-integer (32-bit) int map
 * - most useful for converting strings into enums
 * - uses an arena internally for allocations and uses a bucket + chains design, like most common
 * impls
 * - uses a relatively simple hashing function, so no cryptographically secure or anything of the
 * sort
 */
typedef struct {
    strimap_entry_t** buckets;
    size_t capacity;
    size_t size;
    arena_t* arena;
} strimap_t;

/// ctor for strimap_t
strimap_t strimap_create(size_t capacity);

/// create a strimap from an arena
strimap_t strimap_create_from_arena(size_t capacity, arena_t* arena);

/// dtor for strimap_t, will leak if not called (frees the internal arena)
void strimap_destroy(strimap_t* map);

/// insert an elem {keu, value} destructively/will override anything at the location
void strimap_emplace(strimap_t* map, const char* key, int32_t val);

/// remove an elem at the provided key
void strimap_remove(strimap_t* map, const char* key);
/// rehash the map with a different capacity, does nothing if the capacity is lower than the current
void strimap_rehash(strimap_t* map, size_t new_capacity);
/// returns a mut int* to the value at a provided key
int32_t* strimap_at(strimap_t* map, const char* key);
/// returns a mut int* to the value at a provided key of some length (does not have to be
/// null-terminated)
int32_t* strimap_atn(strimap_t* map, const char* key, size_t key_len);

/// returns a view/const ptr to the value at a provided key
const int32_t* strimap_view(const strimap_t* map, const char* key);
/// returns a view/const ptr to the value at a provided key of some length
const int32_t* strimap_viewn(const strimap_t* map, const char* key, size_t key_len);
/// check if the map contains the provided key
/// - returns true if the map contains the key, else returns false
bool strimap_contains(const strimap_t* map, const char* key);

/**
 * iterator for strimap_t
 * access current element as `curr`
 */
typedef struct strimap_iter {
    const strimap_t* map;
    size_t bucket_idx;
    strimap_entry_t* curr;
} strimap_iter_t;
/// returns an iter set at the start of the map
strimap_iter_t strimap_iter_begin(const strimap_t* map);
/// increment a strimap_iter_t
strimap_entry_t* strimap_iter_next(strimap_iter_t* iter);

uint64_t hash_str(const char* str);
uint64_t hash_strn(const char* str, size_t len);

#ifdef __cplusplus
}
#endif

#endif // ! UTILS_STRIMAP_H
