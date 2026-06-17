//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "utils/mapu32u32.h"
#include <stddef.h>
#include <stdint.h>
#define MAPU32U32_ARENA_CHUNK_SIZE 32768

mapu32u32_t mapu32u32_create_from_arena(size_t capacity, arena_t* arena) {
    capacity = (capacity > MAPU32U32_MINIMUM_CAPACITY) ? capacity : MAPU32U32_MINIMUM_CAPACITY;
    mapu32u32_t map;
    map.capacity = capacity;
    map.arena = arena;
    map.buckets
        = (mapu32u32_entry_t**)arena_alloc(map.arena, capacity * sizeof(mapu32u32_entry_t*));
    // init to NULL
    for (size_t i = 0; i < capacity; i++) {
        map.buckets[i] = NULL;
    }
    map.size = 0;
    return map;
}

void mapu32u32_destroy(mapu32u32_t* map) {
    if (!map || !map->buckets || !map->arena->head) {
        return;
    }
    arena_destroy(map->arena);
    map->buckets = NULL;
    map->size = 0;
    map->capacity = 0;
}

void mapu32u32_insert(mapu32u32_t* map, uint32_t key, uint32_t val) {
    if ((double)map->size / (double)map->capacity >= MAPU32U32_LOAD_FACTOR) {
        mapu32u32_rehash(map, 2 * map->capacity);
    }
    uint32_t raw_hash = hash_uint32(key);
    uint32_t bucket_idx = raw_hash % map->capacity;

    mapu32u32_entry_t* curr = map->buckets[bucket_idx];

    while (curr) {
        if (key == curr->key) {
            curr->val = val;
            return; // if key already exists, update val
        }
        curr = curr->next;
    }
    // if we skipped traverse loop because curr bucket is null, just insert straight into the bucket
    mapu32u32_entry_t* entry = arena_alloc(map->arena, sizeof(mapu32u32_entry_t));
    entry->key = key;
    entry->val = val;
    entry->next = map->buckets[bucket_idx];
    map->buckets[bucket_idx] = entry;
    ++map->size;
}

// remove an entry from the map
bool mapu32u32_remove(mapu32u32_t* map, uint32_t key) {
    uint32_t raw_hash = hash_uint32(key);
    uint32_t bucket_idx = raw_hash % map->capacity;

    mapu32u32_entry_t* curr = map->buckets[bucket_idx];
    mapu32u32_entry_t* prev = NULL;
    while (curr) {
        if (key == curr->key) {
            if (prev) {
                prev->next = curr->next; // stitch chain around removed node
            } else {
                map->buckets[bucket_idx] = curr->next; // update head
            }
            --map->size;
            return true; // if key already exists remove
        }
        prev = curr;
        curr = curr->next;
    }
    // key not found
    return false;
}

void mapu32u32_rehash(mapu32u32_t* map, uint32_t new_capacity) {
    if (new_capacity < MAPU32U32_MINIMUM_CAPACITY) {
        return; // guard
    }
    mapu32u32_entry_t** new_buckets
        = (mapu32u32_entry_t**)arena_alloc(map->arena, new_capacity * sizeof(mapu32u32_entry_t*));
    // init to NULL
    for (size_t i = 0; i < new_capacity; i++) {
        new_buckets[i] = NULL;
    }

    for (size_t i = 0; i < map->capacity; i++) {
        mapu32u32_entry_t* curr = map->buckets[i];
        while (curr) {
            mapu32u32_entry_t* next = curr->next;

            uint64_t raw_hash = hash_uint32(curr->key);
            uint64_t bucket_idx = raw_hash % new_capacity;

            // ins node at head of new bucket
            curr->next = new_buckets[bucket_idx];
            new_buckets[bucket_idx] = curr;

            curr = next;
        }
    }
    map->buckets = new_buckets;
    map->capacity = new_capacity;
}

// gets a mutatable pointer to the value at a specified key
uint32_t* mapu32u32_at(mapu32u32_t* map, uint32_t key) {
    uint32_t raw_hash = hash_uint32(key);
    uint32_t bucket_idx = raw_hash % map->capacity;

    mapu32u32_entry_t* curr = map->buckets[bucket_idx];

    while (curr) {
        if (key == curr->key) {
            return &curr->val;
        }
        curr = curr->next;
    }
    return NULL; // not found
}

const uint32_t* mapu32u32_cat(const mapu32u32_t* map, uint32_t key) {
    // wrap in const
    return (const uint32_t*)mapu32u32_at((mapu32u32_t*)map, key);
}

bool mapu32u32_contains(const mapu32u32_t* map, uint32_t key) {
    return mapu32u32_at((mapu32u32_t*)map, key);
}

mapu32u32_iter_t mapu32u32_iter_begin(const mapu32u32_t* map) {
    mapu32u32_iter_t iter = {.map = map, .bucket_idx = 0, .curr = NULL};

    while (iter.bucket_idx < map->capacity && map->buckets[iter.bucket_idx] == NULL) {
        ++iter.bucket_idx;
    }

    if (iter.bucket_idx < map->capacity) {
        iter.curr = map->buckets[iter.bucket_idx];
    }
    return iter;
}

mapu32u32_entry_t* mapu32u32_iter_next(mapu32u32_iter_t* iter) {
    if (!iter->curr) {
        return iter->curr; // end
    }
    if (iter->curr->next) {
        iter->curr = iter->curr->next;
        return iter->curr; // next
    }
    ++iter->bucket_idx;
    iter->curr = NULL;
    while (iter->bucket_idx < iter->map->capacity && iter->map->buckets[iter->bucket_idx] == NULL) {
        ++iter->bucket_idx;
    }
    if (iter->bucket_idx < iter->map->capacity) {
        iter->curr = iter->map->buckets[iter->bucket_idx];
        return iter->curr;
    }
    iter->curr = NULL;
    return iter->curr;
}

bool mapu32u32_iter_end(mapu32u32_iter_t* iter) { return iter->curr == NULL; }

uint32_t hash_uint32(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3bU;
    x = ((x >> 16) ^ x) * 0x45d9f3bU;
    x = (x >> 16) ^ x;
    return x;
}

uint32_t unhash_uint32(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x119de1f3U;
    x = ((x >> 16) ^ x) * 0x119de1f3U;
    x = (x >> 16) ^ x;
    return x;
}
