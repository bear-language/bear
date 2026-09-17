//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "utils/strimap.h"
#include "utils/arena.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STRIMAP_ARENA_CHUNK_SIZE 32768

// expects either a capacity or NULL for default capacity
strimap_t strimap_create(size_t capacity) {
    capacity = (capacity > STRIMAP_MINIMUM_CAPACITY) ? capacity : STRIMAP_MINIMUM_CAPACITY;
    arena_t* arena = malloc(sizeof(arena_t));
    *arena = arena_create(STRIMAP_ARENA_CHUNK_SIZE);
    return strimap_create_from_arena(capacity, arena);
}

strimap_t strimap_create_from_arena(size_t capacity, arena_t* arena) {
    capacity = (capacity > STRIMAP_MINIMUM_CAPACITY) ? capacity : STRIMAP_MINIMUM_CAPACITY;
    strimap_t map;
    map.capacity = capacity;
    map.arena = arena;
    map.buckets = (strimap_entry_t**)arena_alloc(map.arena, capacity * sizeof(strimap_entry_t*));
    // init to NULL
    for (size_t i = 0; i < capacity; i++) {
        map.buckets[i] = NULL;
    }
    map.size = 0;
    return map;
}

void strimap_destroy(strimap_t* map) {
    if (!map || !map->buckets || !map->arena->head) {
        return;
    }
    arena_destroy(map->arena);
    free(map->arena);
    map->buckets = NULL;
    map->size = 0;
    map->capacity = 0;
}

// inserts integer value index at specified key
void strimap_emplace(strimap_t* map, const char* key, int32_t val) {
    if ((double)map->size / (double)map->capacity >= STRIMAP_LOAD_FACTOR) {
        strimap_rehash(map, 2 * map->capacity);
    }
    uint64_t raw_hash = hash_str(key);
    uint64_t bucket_idx = raw_hash % map->capacity;

    strimap_entry_t* curr = map->buckets[bucket_idx];

    while (curr) {
        if (strcmp(key, curr->key) == 0) {
            curr->val = val;
            return; // if key already exists, update val
        }
        curr = curr->next;
    }
    // if we skipped traverse loop because curr bucket is null, just insert straight into the bucket
    strimap_entry_t* entry = arena_alloc(map->arena, sizeof(strimap_entry_t));
    size_t key_size = strlen(key) + 1;
    char* fresh_key = arena_alloc(map->arena, key_size);
    memcpy(fresh_key, key, key_size);
    entry->key = fresh_key;
    entry->val = val;
    entry->len = key_size - 1; // subtract away null term
    entry->next = map->buckets[bucket_idx];
    map->buckets[bucket_idx] = entry;
    ++map->size;
}

// remove an entry from the map
void strimap_remove(strimap_t* map, const char* key) {
    uint64_t raw_hash = hash_str(key);
    uint64_t bucket_idx = raw_hash % map->capacity;

    strimap_entry_t* curr = map->buckets[bucket_idx];
    strimap_entry_t* prev = NULL;
    while (curr) {
        if (strcmp(key, curr->key) == 0) {
            if (prev) {
                prev->next = curr->next; // stitch chain around removed node
            } else {
                map->buckets[bucket_idx] = curr->next; // update head
            }
            --map->size;
            return; // if key already exists remove
        }
        prev = curr;
        curr = curr->next;
    }
    // if key not found, do nothing
}

// gets a mutatable pointer to the value at a specified key
int32_t* strimap_at(strimap_t* map, const char* key) {
    uint64_t raw_hash = hash_str(key);
    uint64_t bucket_idx = raw_hash % map->capacity;

    strimap_entry_t* curr = map->buckets[bucket_idx];

    while (curr) {
        if (strcmp(key, curr->key) == 0) {
            return &curr->val;
        }
        curr = curr->next;
    }
    return NULL; // not found
}

// gets a mutatable pointer to the value at a specified key and finds string by length, so the
// key does not have to be null-terminated
int32_t* strimap_atn(strimap_t* map, const char* key, size_t key_len) {
    uint64_t raw_hash = hash_strn(key, key_len);
    uint64_t bucket_idx = raw_hash % map->capacity;

    strimap_entry_t* curr = map->buckets[bucket_idx];

    while (curr) {
        if (curr->len == key_len && strncmp(key, curr->key, key_len) == 0) {
            return &curr->val;
        }
        curr = curr->next;
    }
    return NULL; // not found
}

// gets an immutatable pointer to the value at a specified key
const int32_t* strimap_view(const strimap_t* map, const char* key) {
    uint64_t raw_hash = hash_str(key);
    uint64_t bucket_idx = raw_hash % map->capacity;

    const strimap_entry_t* curr = map->buckets[bucket_idx];

    while (curr) {
        if (strcmp(key, curr->key) == 0) {
            return &curr->val;
        }
        curr = curr->next;
    }
    return NULL; // not found
}

// gets an immutatable pointer to the value at a specified key and finds string by length, so the
// key does not have to be null-terminated
const int32_t* strimap_viewn(const strimap_t* map, const char* key, size_t key_len) {
    uint64_t raw_hash = hash_strn(key, key_len);
    uint64_t bucket_idx = raw_hash % map->capacity;

    const strimap_entry_t* curr = map->buckets[bucket_idx];

    while (curr) {
        if (curr->len == key_len && strncmp(key, curr->key, key_len) == 0) {
            return &curr->val;
        }
        curr = curr->next;
    }
    return NULL; // not found
}

// rehash the map to a specified new number of buckets, will fail if new_capacity <
// STRIMAP_MINIMUM_CAPACITY
void strimap_rehash(strimap_t* map, size_t new_capacity) {
    if (new_capacity < STRIMAP_MINIMUM_CAPACITY) {
        return; // guard
    }
    strimap_entry_t** new_buckets
        = (strimap_entry_t**)arena_alloc(map->arena, new_capacity * sizeof(strimap_entry_t*));
    // init to NULL
    for (size_t i = 0; i < new_capacity; i++) {
        new_buckets[i] = NULL;
    }

    for (size_t i = 0; i < map->capacity; i++) {
        strimap_entry_t* curr = map->buckets[i];
        while (curr) {
            strimap_entry_t* next = curr->next;

            uint64_t raw_hash = hash_str(curr->key);
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

// returns true if the map contains an entry at the specified key
bool strimap_contains(const strimap_t* map, const char* key) {
    return strimap_view(map, key) != NULL;
}

// iterators

/*
 * Builds an iterator by value and initializes it to the first entry.
 *
 * Notes:
 * - This iterator does not need to be destructed in any way.
 * - `curr == NULL` means failure (empty map).
 *
 * Usage:
 *   for (strimap_iter_t it = strimap_iter_begin(&map);
 *        it.curr;
 *        strimap_iter_next(&it)) {
 *       printf("%s => %d\n", it.curr->key, it.curr->val);
 *   }
 */

strimap_iter_t strimap_iter_begin(const strimap_t* map) {
    strimap_iter_t iter = {.map = map, .bucket_idx = 0, .curr = NULL};

    while (iter.bucket_idx < map->capacity && map->buckets[iter.bucket_idx] == NULL) {
        ++iter.bucket_idx;
    }

    if (iter.bucket_idx < map->capacity) {
        iter.curr = map->buckets[iter.bucket_idx];
    }
    return iter;
}
/*
 * Increments an existing iterator
 *
 * Notes:
 * - `curr == NULL` means the end has been reached.
 *
 * Usage:
 *   for (strimap_iter_t it = strimap_iter_begin(&map);
 *        it.curr;
 *        strimap_iter_next(&it)) {
 *       printf("%s => %d\n", it.curr->key, it.curr->val);
 *   }
 */
strimap_entry_t* strimap_iter_next(strimap_iter_t* iter) {
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

// helper

// null-term str hash
uint64_t hash_str(const char* str) {
    uint64_t hash = 5381;
    int curr_ch;

    while ((curr_ch = (unsigned char)*str++)) { // iter
        // hash = hash * 33 + c (DJB2 algorithm)
        hash = ((hash << 5) + hash) + curr_ch;
    }

    return hash;
}

// non-null-term str hash
uint64_t hash_strn(const char* str, size_t len) {
    uint64_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        unsigned char curr_ch = (unsigned char)str[i];
        hash = ((hash << 5) + hash) + curr_ch; // hash * 33 + curr_ch
    }
    return hash;
}
