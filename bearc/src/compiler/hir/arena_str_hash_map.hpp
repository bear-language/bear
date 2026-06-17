//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef UTILS_STRIMAP_HPP
#define UTILS_STRIMAP_HPP

#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"
#include "utils/strimap.h"
#include <cstddef>

namespace hir {

template <hir::IsId I> class StrIdHashMap {
    static constexpr size_t DEFAULT_ARENA_STR_HASH_MAP_SIZE = 0x800;

  public:
    StrIdHashMap(DataArena& arena)
        : arena{arena},
          map{strimap_create_from_arena(DEFAULT_ARENA_STR_HASH_MAP_SIZE, arena.arena())} {}
    void emplace(const char* key, I id) { strimap_emplace(&map, key, id.raw()); }
    bool contains(const char* key) { return strimap_contains(&map, key); }
    OptId<I> at(const char* key) {
        auto* id = strimap_at(&map, key);
        if (!id) {
            return OptId<I>{};
        }
        return OptId<I>{static_cast<HirId>(*id)};
    }
    OptId<I> atn(const char* key, size_t len) {
        auto* id = strimap_atn(&map, key, len);
        if (!id) {
            return OptId<I>{};
        }
        return OptId<I>{I{static_cast<HirId>(*id)}};
    }
    void remove(const char* key) { strimap_remove(&map, key); }
    void rehash(size_t size) { strimap_rehash(&map, size); }

  private:
    DataArena& arena;
    strimap_t map;
};

} // namespace hir

#endif
