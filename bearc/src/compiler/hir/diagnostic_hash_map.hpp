//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_HIR_DIAGNOSTIC_HASH_MAP_HPP
#define COMPILER_HIR_DIAGNOSTIC_HASH_MAP_HPP

#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"
#include <cstring>
#include <type_traits>

namespace hir {

class None;

/// maps structurally equivalent diagnostic chains to a value, or acts as a set when I is Id<None>
template <IsId I = Id<None>> class DiagnosticHashMap {
    static constexpr bool is_set = std::is_same_v<I, Id<None>>;
    struct NoVal {};
    using Val = std::conditional_t<is_set, NoVal, I>;
    struct Entry {
        DiagnosticId key;
        size_t hash;
        Entry* next;
        [[no_unique_address]] Val val;
    };
    static constexpr size_t DEFAULT_CAP = 128;
    static constexpr size_t GROWTH_FACTOR = 2;

    Context& context;
    DataArena& arena;
    Entry** buckets;
    size_t count{0};
    size_t capacity;

    static Entry** alloc_buckets(DataArena& arena, size_t cap) {
        Entry** buckets = arena.alloc_as<Entry**>(cap * sizeof(Entry*));
        memset(static_cast<void*>(buckets), 0, cap * sizeof(Entry*));
        return buckets;
    }

    Entry* find(DiagnosticId did, size_t hash) const {
        for (Entry* curr = buckets[hash % capacity]; curr; curr = curr->next) {
            if (curr->hash == hash && DiagnosticComparator{context}(curr->key, did)) {
                return curr;
            }
        }
        return nullptr;
    }

    void rehash(size_t new_capacity) {
        Entry** new_buckets = alloc_buckets(arena, new_capacity);
        for (size_t i = 0; i < capacity; ++i) {
            for (Entry* curr = buckets[i]; curr;) {
                Entry* next = curr->next;
                Entry** chain = new_buckets + (curr->hash % new_capacity);
                curr->next = *chain;
                *chain = curr;
                curr = next;
            }
        }
        capacity = new_capacity;
        buckets = new_buckets;
    }

    void emplace(DiagnosticId did, Val val, size_t hash) {
        if (count >= capacity) {
            rehash(capacity * GROWTH_FACTOR);
        }
        Entry** chain = buckets + (hash % capacity);
        Entry* entry = arena.alloc_type<Entry>();
        ::new (entry) Entry{.key = did, .hash = hash, .next = *chain, .val = val};
        *chain = entry;
        ++count;
    }

  public:
    DiagnosticHashMap(Context& context, DataArena& arena, size_t capacity = DEFAULT_CAP)
        : context(context), arena(arena),
          buckets(alloc_buckets(arena, capacity > DEFAULT_CAP ? capacity : DEFAULT_CAP)),
          capacity(capacity > DEFAULT_CAP ? capacity : DEFAULT_CAP) {}

    [[nodiscard]] bool contains(DiagnosticId did) const {
        return find(did, DiagnosticHasher{context}(did)) != nullptr;
    }

    [[nodiscard]] OptId<I> at(DiagnosticId did) const
        requires(!is_set)
    {
        const Entry* entry = find(did, DiagnosticHasher{context}(did));
        return entry ? OptId<I>{entry->val} : OptId<I>{};
    }

    /// only use after `at` returns none to avoid duplicate inserts
    void insert(DiagnosticId did, I val)
        requires(!is_set)
    {
        emplace(did, val, DiagnosticHasher{context}(did));
    }

    /// returns true if an equivalent chain was already in the set
    bool insert(DiagnosticId did)
        requires is_set
    {
        const size_t hash = DiagnosticHasher{context}(did);
        if (find(did, hash)) {
            return true;
        }
        emplace(did, NoVal{}, hash);
        return false;
    }
};

using DiagnosticHashSet = DiagnosticHashMap<>;

} // namespace hir

#endif
