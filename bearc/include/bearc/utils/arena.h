//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef UTILS_ARENA_H
#define UTILS_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arena_chunk arena_chunk_t;

/// main arena container
typedef struct arena {
    arena_chunk_t* head;
    size_t chunk_size;
} arena_t;

// arena chunk, composes the arena
typedef struct arena_chunk {
    uint8_t* data;
    size_t used;
    size_t cap;
    struct arena_chunk* next;
} arena_chunk_t;

/// arena ctor (init) from a specified standard chunk size.
arena_t arena_create(size_t chunk_cap_bytes);

/// arena dtor (clean up resources)
void arena_destroy(arena_t* arena);

/// get an allocation from the arena of a specified size
/// - internally defaults to an alignment of 8 bytes
void* arena_alloc(arena_t* arena, size_t req_size_bytes);

/// for testing purposes
void arena_log_debug_info(arena_t* arena);

#ifdef __cplusplus
}
#endif

#endif // !UTILS_ARENA_H
