//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_ID_MAP_HPP
#define COMPILER_HIR_ID_MAP_HPP

#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"
#include "utils/mapu32u32.h"
#include <assert.h>
#include <cstddef>
namespace hir {

// a hashmap optimized for Id storage, based on an internal arena that is not owned
template <hir::IsId K, hir::IsId V>
// should be the case given the constraints of hir::Id concept, but
// just be extra sure since this will otherwise implode
    requires(sizeof(K) == 4 && sizeof(V) == 4)
class IdHashMap {

    DataArena& arena;
    mapu32u32_t map;

  public:
    IdHashMap(const IdHashMap&) = delete;
    IdHashMap(IdHashMap&&) = default;
    IdHashMap& operator=(const IdHashMap&) = delete;
    IdHashMap& operator=(IdHashMap&&) = default;
    IdHashMap(DataArena& arena, size_t capacity)
        : arena(arena), map(mapu32u32_create_from_arena(capacity, arena.arena())) {}
    void insert(K key, V value) {
        assert((key.raw() != HIR_ID_NONE) && "tried to insert a key with value HIR_ID_NONE");
        mapu32u32_insert(&map, key.raw(), value.raw());
    }
    /// returns false if key is not found
    bool remove(K key) { return mapu32u32_remove(&map, key.raw()); }
    /// returns an optional id by value
    OptId<V> at(K key) const {
        auto* value = mapu32u32_cat(&map, key.raw());
        if (value == nullptr) {
            return OptId<V>{};
        }
        return OptId<V>{V{*value}};
    }
    bool contains(K key) { return mapu32u32_at(&map, key.raw()) != 0; }

    class Entry {
        K key_;
        V val_;

      public:
        K key() const noexcept { return key_; }
        V val() const noexcept { return val_; }
        Entry(K key, V val) : key_(key), val_(val) {}
    };

    struct Iter {
        using iterator_category = std::forward_iterator_tag;

        Iter(const mapu32u32_t* map) noexcept : it{mapu32u32_iter_begin(map)} {}
        Iter(const mapu32u32_iter_t raw_it) noexcept : it{raw_it} {}

        Entry operator*() const noexcept { return Entry{K{it.curr->key}, V{it.curr->val}}; }

        Iter& operator++() noexcept {
            mapu32u32_iter_next(&it);
            return *this;
        }

        Iter operator++(int) noexcept {
            Iter tmp = *this;
            ++(*this);
            return tmp;
        }

        static Iter end() noexcept {
            return Iter{mapu32u32_iter_t{
                .map = nullptr,
                .bucket_idx = 0,
                .curr = nullptr,
            }};
        }

        friend bool operator==(const Iter& a, const Iter& b) noexcept {
            return a.it.curr == b.it.curr;
        }
        friend bool operator!=(const Iter& a, const Iter& b) noexcept { return !(a == b); }

      private:
        mapu32u32_iter_t it;
    };
    Iter iter() { return Iter{&map}; }
    Iter begin() const noexcept { return Iter{&map}; }
    Iter end() const noexcept { return Iter::end(); }
};

} // namespace hir

#endif // !COMPILER_HIR_ID_MAP_HPP
