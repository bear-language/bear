//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_ID_SET_HPP
#define COMPILER_HIR_ID_SET_HPP

#include "compiler/hir/id_hash_map.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"

namespace hir {

template <IsId T> class IdSet {
  public:
    IdSet(DataArena& arena, HirSize capacity) : map{arena, capacity} {}
    [[nodiscard]] HirSize size() { return size_; }
    /// returns true if the set already contained the value
    bool insert(T elt) {
        const bool already_has = map.contains(elt);
        map.insert(elt, DUMMY);
        if (!already_has) {
            ++size_;
        }
        return already_has;
    }
    [[nodiscard]] bool contains(T elt) { return map.contains(elt); }
    /// returns false if elt is not found
    bool remove(T elt) {
        const bool found = map.remove(elt);
        if (found) {
            --size_;
        }
        return found;
    }

  private:
    hir::IdHashMap<T, T> map;
    HirSize size_ = 0;
    static constexpr T DUMMY = T{HIR_ID_NONE};
};

} // namespace hir

#endif // !COMPILER_HIR_ID_SET_HPP
