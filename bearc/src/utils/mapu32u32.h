//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef UTILS_MAPU32U32_H
#define UTILS_MAPU32U32_H

#include "utils/arena.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAPU32U32_MINIMUM_CAPACITY 4
#define MAPU32U32_LOAD_FACTOR .75

#ifdef __cplusplus
extern "C" {
#endif

/// an entry in the string to int map
typedef struct mapu32u32_entry {
    uint32_t key;
    uint32_t val;
    struct mapu32u32_entry* next;
} mapu32u32_entry_t;

/**
 * mapu32u32_t, as in uint32_t -> uint32_t map
 * - uses an arena internally for allocations and uses a bucket + chains design, like most common
 * impls
 */
typedef struct {
    mapu32u32_entry_t** buckets;
    uint32_t capacity;
    uint32_t size;
    arena_t* arena;
} mapu32u32_t;

/// create a mapu32u32 from an arena
mapu32u32_t mapu32u32_create_from_arena(size_t capacity, arena_t* arena);

/// dtor for mapu32u32_t, only use when this map owns its internal arena; otherwise, will leak if
/// not called (frees the internal arena)
void mapu32u32_destroy(mapu32u32_t* map);

/// insert an elem {key, value} destructively/will override anything at the location
void mapu32u32_insert(mapu32u32_t* map, uint32_t key, uint32_t val);

/// remove an elem at the provided key
bool mapu32u32_remove(mapu32u32_t* map, uint32_t key);

/// rehash the map with a different capacity, does nothing if the capacity is lower than the current
void mapu32u32_rehash(mapu32u32_t* map, uint32_t new_capacity);

/// returns a mut ptr to the value at a provided key
uint32_t* mapu32u32_at(mapu32u32_t* map, uint32_t key);

/// returns a view/const ptr to the value at a provided key
const uint32_t* mapu32u32_cat(const mapu32u32_t* map, uint32_t key);

/// - returns true if the map contains the key, else returns false
bool mapu32u32_contains(const mapu32u32_t* map, uint32_t key);

/**
 * iterator for mapu32u32_t
 * access current element as `curr`
 */
typedef struct {
    const mapu32u32_t* map;
    size_t bucket_idx;
    mapu32u32_entry_t* curr;
} mapu32u32_iter_t;
/// returns an iter set at the start of the map
mapu32u32_iter_t mapu32u32_iter_begin(const mapu32u32_t* map);
/// increment a mapu32u32_iter_t
mapu32u32_entry_t* mapu32u32_iter_next(mapu32u32_iter_t* iter);
/// indicates that the iter at the end
bool mapu32u32_iter_end(mapu32u32_iter_t* iter);

uint32_t hash_uint32(uint32_t x);
uint32_t unhash_uint32(uint32_t x);

#ifdef __cplusplus
}
#endif

#endif // ! UTILS_mapu32u32_H
