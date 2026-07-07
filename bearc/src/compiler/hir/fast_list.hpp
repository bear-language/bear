//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

// a vector-backed list that doesn't allow removal
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/node_vector.hpp"
#include <optional>

namespace hir {

template <typename T> class FastList {
    struct Node;
    using NodeId = Id<Node>;
    using NodeVec = SmallIdVecMap<NodeId, Node>;
    struct Node {
        T val;
        OptId<NodeId> prev{};
        OptId<NodeId> next{};
    };
    NodeVec nodes;
    OptId<NodeId> head{};
    OptId<NodeId> tail{};
    HirSize size{};

    static constexpr HirSize DEFAULT_CAP = 128;

  public:
    FastList() : nodes(DEFAULT_CAP) {}
    FastList(HirSize cap) : nodes(cap) {}
    FastList(const FastList&) = delete;
    FastList(FastList&&) = delete;
    auto operator=(const FastList&) = delete;
    auto operator=(FastList&&) = delete;

    /// puts a new head on the list
    void put_head(T val) {
        const NodeId node = nodes.emplace_and_get_id(Node{.val = val, .prev = {}, .next = head});
        if (head.has_value()) {
            nodes.at(head.as_id()).prev = node;
        }
        head = node;
        if (size == 0) {
            tail = node;
        }
        ++size;
    }

    /// puts a new tail on the list
    void put_tail(T val) {
        const NodeId node = nodes.emplace_and_get_id(Node{.val = val, .prev = tail, .next = {}});
        if (tail.has_value()) {
            nodes.at(tail.as_id()).next = node;
        }
        tail = node;
        if (size == 0) {
            head = node;
        }
        ++size;
    }

    class Iter {
        const NodeVec& nodes;
        OptId<NodeId> curr;

      public:
        Iter(const FastList& list) : nodes{list.nodes}, curr{list.head} {}
        Iter(const FastList& list, OptId<NodeId> n) : nodes{list.nodes}, curr{n} {}
        T operator*() const noexcept { return nodes.at(curr.as_id()).val; }

        Iter operator++(int) noexcept {
            const auto tmp = *this;
            if (curr.has_value()) {
                curr = nodes.at(curr.as_id()).next;
            }
            return tmp;
        }

        Iter operator++() noexcept {
            if (curr.has_value()) {
                curr = nodes.at(curr.as_id()).next;
            }
            return *this;
        }

        friend bool operator==(const Iter& a, const Iter& b) noexcept { return a.curr == b.curr; }
        friend bool operator!=(const Iter& a, const Iter& b) noexcept { return !(a == b); }
    };

    Iter begin() noexcept { return Iter{*this, head}; }
    Iter end() noexcept { return Iter{*this, OptId<NodeId>{}}; }

    class ReverseIter {
        const NodeVec& nodes;
        OptId<NodeId> curr;

      public:
        ReverseIter(const FastList& list) : nodes{list.nodes}, curr{list.tail} {}
        ReverseIter(const FastList& list, OptId<NodeId> n) : nodes{list.nodes}, curr{n} {}
        T operator*() const noexcept { return nodes.at(curr.as_id()).val; }

        ReverseIter operator++(int) noexcept {
            const auto tmp = *this;
            if (curr.has_value()) {
                curr = nodes.at(curr.as_id()).prev;
            }
            return tmp;
        }

        ReverseIter operator++() noexcept {
            if (curr.has_value()) {
                curr = nodes.at(curr.as_id()).prev;
            }
            return *this;
        }

        friend bool operator==(const ReverseIter& a, const ReverseIter& b) noexcept {
            return a.curr == b.curr;
        }
        friend bool operator!=(const ReverseIter& a, const ReverseIter& b) noexcept {
            return !(a == b);
        }
    };

    ReverseIter rbegin() noexcept { return ReverseIter{*this, tail}; }
    ReverseIter rend() noexcept { return ReverseIter{*this, OptId<NodeId>{}}; }
};

} // namespace hir
