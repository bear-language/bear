//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_INLINE_ID_MAP_HPP
#define BEARC_COMPILER_HIR_INLINE_ID_MAP_HPP

#include "compiler/hir/id_hash_map.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <optional>
#include <utility>

namespace hir {

/// BUFFER_SIZE defaults to 8 entries if nothing is passed it (the 0 is just a placeholder)
template <IsId K, IsId V, size_t BUFFER_SIZE = 0> class InlineIdMap {
    using Entry = std::pair<K, V>;

    // inline buffer is sized to 64 bytes (one cache line)
    static constexpr std::size_t MAX_PAIRS = (BUFFER_SIZE == 0) ? 64 / sizeof(Entry) : BUFFER_SIZE;
    static constexpr std::size_t HASH_MAP_STARTING_SIZE = MAX_PAIRS * 4;
    static constexpr std::size_t DATA_ARENA_STARTING_SIZE
        = HASH_MAP_STARTING_SIZE * sizeof(Entry) * 16;

    static_assert(MAX_PAIRS > 0, "Entry is too large for the inline buffer");

    llvm::SmallVector<Entry, MAX_PAIRS> pairs{};
    // Declared before maybe_map so the arena is destroyed after the map that uses it.
    std::optional<DataArena> maybe_data_arena;
    std::optional<IdHashMap<K, V>> maybe_map;

    [[nodiscard]] bool hash_map_is_active() const { return maybe_map.has_value(); }

    void check_to_init_hash_map() {
        if (hash_map_is_active() || pairs.size() < MAX_PAIRS) {
            return;
        }
        maybe_data_arena.emplace(DATA_ARENA_STARTING_SIZE);
        maybe_map.emplace(*maybe_data_arena, HASH_MAP_STARTING_SIZE);

        for (const auto& [k, v] : pairs) {
            maybe_map->insert(k, v);
        }
        pairs.clear();
    }

  public:
    InlineIdMap() = default;
    InlineIdMap(const InlineIdMap&) = delete;
    InlineIdMap& operator=(const InlineIdMap&) = delete;
    InlineIdMap(InlineIdMap&&) = delete;
    InlineIdMap& operator=(InlineIdMap&&) = delete;
    ~InlineIdMap() = default;

    /// overwrites on duplicate
    void insert(K key, V value) {
        if (!hash_map_is_active()) {
            for (auto& [k, v] : pairs) {
                if (k == key) {
                    v = value;
                    return;
                }
            }
        }

        check_to_init_hash_map();
        if (hash_map_is_active()) {
            maybe_map->insert(key, value);
        } else {
            pairs.emplace_back(key, value);
        }
    }

    [[nodiscard]] OptId<V> at(K key) const {
        if (hash_map_is_active()) {
            return maybe_map->at(key);
        }
        for (const auto& [k, v] : pairs) {
            if (k == key) {
                return v;
            }
        }
        return {};
    }

    [[nodiscard]] bool contains(K key) const { return at(key).has_value(); }
};

} // namespace hir

#endif // !BEARC_COMPILER_HIR_INLINE_ID_MAP_HPP
