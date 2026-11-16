//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "utils/data_arena.hpp"
#include "utils/arena.h"
#include <stddef.h>
DataArena::DataArena(size_t chunk_size) : chunk_size{chunk_size} {}

DataArena::~DataArena() {
    if (initialized) {
        arena_destroy(&this->arena_);
    }
}

size_t DataArena::chunk_cap() const noexcept { return this->chunk_size; }

void DataArena::log_debug_info() {
    try_init();
    arena_log_debug_info(&this->arena_);
}

arena_t* DataArena::arena() {
    try_init();
    return &this->arena_;
}

size_t DataArena::first_chunk_size() noexcept {
    try_init();
    return arena_.head->used;
}
