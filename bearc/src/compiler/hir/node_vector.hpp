//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_NODE_VECTOR_HPP
#define COMPILER_HIR_NODE_VECTOR_HPP

#include "compiler/hir/indexing.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <utility>
#include <vector>
namespace hir {
template <typename T>
concept Node = hir::IsId<typename T::id_type>;

/// Maps an hir::Id to any type
/// - this should be used for linear indexing where each contiguous Id that exists will correspond
/// to exactly one value..
template <hir::IsId I, typename V> class IdVecMap {
  protected:
    std::vector<V> vec;

  private:
    static constexpr HirId OFFSET = 1;

  public:
    explicit IdVecMap(HirSize capacity) : vec{} { vec.reserve(capacity + OFFSET); }
    void reserve(HirSize size) { vec.reserve(size); }
    [[nodiscard]] V& at(I id) {
        assert(id.val() != HIR_ID_NONE && "[hir::IdVecMap::at] asked for an id of HIR_ID_NONE\n");
        return vec.at(id.val() - OFFSET);
    }
    [[nodiscard]] const V& at(I id) const {
        assert(id.val() != HIR_ID_NONE && "[hir::IdVecMap::cat] asked for an id of HIR_ID_NONE\n");
        return vec.at(id.val() - OFFSET);
    }
    template <typename... Args>
    [[nodiscard("Id must be fetched or emplaced node is dead.")]] I
    emplace_and_get_id(Args&&... args) {
        vec.emplace_back(std::forward<Args>(args)...);
        return I{static_cast<HirId>(vec.size()) - 1
                 + OFFSET}; // so just the size, but this is crucial
    }
    template <typename... Args> I bump(Args&&... args) {
        vec.emplace_back(std::forward<Args>(args)...);
        return I{static_cast<HirId>(vec.size()) - 1
                 + OFFSET}; // so just the size, but this is crucial
    }

    [[nodiscard]] I begin_id() const { return I{1}; }
    [[nodiscard]] I end_id() const { return I{static_cast<HirId>(vec.size() + OFFSET)}; }
    [[nodiscard]] I rbegin_id() const { return I{static_cast<HirId>(vec.size() + OFFSET - 1)}; }
    [[nodiscard]] I rend_id() const { return I{0}; }

    using value_type = V;
    using reference = V&;
    using const_reference = const V&;
    using iterator = typename std::vector<V>::iterator;
    using const_iterator = typename std::vector<V>::const_iterator;
    using reverse_iterator = typename std::vector<V>::reverse_iterator;
    using const_reverse_iterator = typename std::vector<V>::const_reverse_iterator;
    using size_type = typename std::vector<V>::size_type;

    [[nodiscard]] iterator begin() noexcept { return vec.begin(); }
    [[nodiscard]] const_iterator begin() const noexcept { return vec.begin(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return vec.cbegin(); }

    [[nodiscard]] iterator end() noexcept { return vec.end(); }
    [[nodiscard]] const_iterator end() const noexcept { return vec.end(); }
    [[nodiscard]] const_iterator cend() const noexcept { return vec.cend(); }

    [[nodiscard]] reverse_iterator rbegin() noexcept { return vec.rbegin(); }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return vec.rbegin(); }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return vec.crbegin(); }

    [[nodiscard]] reverse_iterator rend() noexcept { return vec.rend(); }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return vec.rend(); }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return vec.crend(); }

    [[nodiscard]] bool empty() const noexcept { return vec.empty(); }
    [[nodiscard]] size_type size() const noexcept { return vec.size(); }
};

/// Models a vector of an hir::Node
template <Node T> class NodeVector : public IdVecMap<typename T::id_type, T> {
  public:
    explicit NodeVector(HirSize capacity) : IdVecMap<typename T::id_type, T>(capacity) {}
};

/// Models a vector of an hir::IdIdx pointing to hir::Id
template <hir::IsId I> class IdVector : public IdVecMap<typename hir::IdIdx<I>, I> {
    static constexpr HirSize OFFSET = 1;

  public:
    explicit IdVector(HirSize capacity) : IdVecMap<typename hir::IdIdx<I>, I>(capacity) {}
    IdSlice<I> freeze_small_vec(const llvm::SmallVectorImpl<I>& svec) {
        // basically we're just allocating a contiguous chunk inside the vector so that we can copy
        // in the small vector into internal, permanent storage
        IdIdx<I> first{static_cast<HirId>(this->vec.size() + OFFSET)};
        for (const auto id : svec) {
            this->vec.emplace_back(id);
        }
        HirSize len = svec.size();
        return IdSlice<I>{first, len};
    }
};

} // namespace hir

#endif
