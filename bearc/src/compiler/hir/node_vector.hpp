//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_HIR_NODE_VECTOR_HPP
#define COMPILER_HIR_NODE_VECTOR_HPP

#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <type_traits>
#include <utility>
#include <vector>
namespace hir {
template <typename T>
concept Node = hir::IsId<typename T::id_type>;

/// Maps an hir::Id to any type
/// - this should be used for linear indexing where each contiguous Id that exists will correspond
/// to exactly one value..
template <hir::IsId I, typename V, typename VectorType> class IdVecMapBase {
  protected:
    VectorType vec;

  private:
    static constexpr HirId OFFSET = 1;

  public:
    explicit IdVecMapBase(HirSize capacity) : vec{} { vec.reserve(capacity + OFFSET); }
    void reserve(HirSize size) { vec.reserve(size); }
    [[nodiscard]] V& at(I id) {
        assert(id.raw() != HIR_ID_NONE && "[hir::IdVecMap::at] asked for an id of HIR_ID_NONE\n");
        return vec[id.raw() - OFFSET];
    }
    [[nodiscard]] const V& at(I id) const {
        assert(id.raw() != HIR_ID_NONE && "[hir::IdVecMap::cat] asked for an id of HIR_ID_NONE\n");
        return vec[id.raw() - OFFSET];
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
    using iterator = VectorType::iterator;
    using const_iterator = VectorType::const_iterator;
    using reverse_iterator = VectorType::reverse_iterator;
    using const_reverse_iterator = VectorType::const_reverse_iterator;
    using size_type = VectorType::size_type;

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

template <hir::IsId I, typename V> class IdVecMap : public IdVecMapBase<I, V, std::vector<V>> {
  public:
    explicit IdVecMap(HirSize capacity) : IdVecMapBase<I, V, std::vector<V>>(capacity) {}
};

/// Maps an hir::Id to any type, like IdVecMap, but with stable element addresses
/// - slightly (~3-5%) slower than IdVecMap
template <hir::IsId I, typename V> class StableIdVecMap {
    static_assert(alignof(V) <= 8, "DataArena only guarantees 8-byte alignment");
    static constexpr HirId OFFSET = 1;
    static constexpr HirSize CHUNK_SHIFT = 12;
    static constexpr HirSize CHUNK_SIZE = HirSize{1} << CHUNK_SHIFT;
    static constexpr HirSize CHUNK_MASK = CHUNK_SIZE - 1;

    DataArena arena;
    std::vector<V*> chunks;
    HirSize len = 0;

    [[nodiscard]] V& slot(HirSize idx) { return chunks[idx >> CHUNK_SHIFT][idx & CHUNK_MASK]; }
    [[nodiscard]] const V& slot(HirSize idx) const {
        return chunks[idx >> CHUNK_SHIFT][idx & CHUNK_MASK];
    }

  public:
    explicit StableIdVecMap(HirSize) : arena{CHUNK_SIZE * sizeof(V)} {}
    ~StableIdVecMap() {
        if constexpr (!std::is_trivially_destructible_v<V>) {
            for (HirSize i = 0; i < len; i++) {
                slot(i).~V();
            }
        }
    }
    StableIdVecMap(const StableIdVecMap&) = delete;
    StableIdVecMap& operator=(const StableIdVecMap&) = delete;
    StableIdVecMap(StableIdVecMap&&) = delete;
    StableIdVecMap& operator=(StableIdVecMap&&) = delete;

    void reserve(HirSize) {}

    [[nodiscard]] V& at(I id) {
        assert(id.raw() != HIR_ID_NONE
               && "[hir::StableIdVecMap::at] asked for an id of HIR_ID_NONE\n");
        return slot(id.raw() - OFFSET);
    }
    [[nodiscard]] const V& at(I id) const {
        assert(id.raw() != HIR_ID_NONE
               && "[hir::StableIdVecMap::at] asked for an id of HIR_ID_NONE\n");
        return slot(id.raw() - OFFSET);
    }

    template <typename... Args>
    [[nodiscard("Id must be fetched or emplaced node is dead.")]] I
    emplace_and_get_id(Args&&... args) {
        return bump(std::forward<Args>(args)...);
    }
    template <typename... Args> I bump(Args&&... args) {
        if ((len & CHUNK_MASK) == 0) {
            chunks.push_back(arena.alloc_as<V*>(CHUNK_SIZE * sizeof(V)));
        }
        ::new (&slot(len)) V(std::forward<Args>(args)...);
        len++;
        return I{static_cast<HirId>(len - 1 + OFFSET)};
    }

    [[nodiscard]] I begin_id() const { return I{1}; }
    [[nodiscard]] I end_id() const { return I{static_cast<HirId>(len + OFFSET)}; }
    [[nodiscard]] I rbegin_id() const { return I{static_cast<HirId>(len + OFFSET - 1)}; }
    [[nodiscard]] I rend_id() const { return I{0}; }

    using value_type = V;
    using reference = V&;
    using const_reference = const V&;
    using size_type = HirSize;

    [[nodiscard]] bool empty() const noexcept { return len == 0; }
    [[nodiscard]] size_type size() const noexcept { return len; }
};

/// Models a vector of an hir::Node (with stable element addresses)
template <Node T> class NodeVector : public StableIdVecMap<typename T::id_type, T> {
  public:
    explicit NodeVector(HirSize capacity) : StableIdVecMap<typename T::id_type, T>(capacity) {}
};

template <hir::IsId I, typename V>
class SmallIdVecMap : public IdVecMapBase<I, V, llvm::SmallVector<V>> {
  public:
    explicit SmallIdVecMap(HirSize capacity) : IdVecMapBase<I, V, llvm::SmallVector<V>>(capacity) {}
};

/// Models a vector of an hir::Node with an internal small vector
template <Node T> class SmallNodeVector : public SmallIdVecMap<typename T::id_type, T> {
  public:
    explicit SmallNodeVector(HirSize capacity) : SmallIdVecMap<typename T::id_type, T>(capacity) {}
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
