//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef UTILS_VALUE_ARENA_HPP
#define UTILS_VALUE_ARENA_HPP

#include "utils/arena.h"
#include <stddef.h>
#include <type_traits>

/// holds pure data in an internal arena, which each chunk of the specified chunk size, can hold any
/// values do that do not rely on non-trivial destructors
class DataArena {
    arena_t arena_;

  public:
    /// arena ctor (init) from a specified standard chunk size.
    explicit DataArena(size_t chunk_size);
    DataArena(const DataArena&) = delete;
    DataArena(DataArena&& other) = delete;
    DataArena& operator=(const DataArena&) = delete;
    DataArena& operator=(DataArena&&) = delete;
    ~DataArena();
    /// gets a ptr to the underlying c-style arena
    arena_t* arena();
    /// get an allocation from the arena of a specified size
    template <typename T> T* alloc_type() {
        return static_cast<T*>(arena_alloc(&this->arena_, sizeof(T)));
    } /// returns the a pointer data already casted to the desired ptr type
    template <typename T>
    T alloc_as(size_t size)
        requires std::is_pointer_v<T>
    {
        return static_cast<T>(arena_alloc(&this->arena_, size));
    } /// returns the chunk size (in bytes)
    size_t chunk_cap() const noexcept;
    size_t first_chunk_size() const noexcept;
    /// for testing purposes
    void log_debug_info();
};

#endif
