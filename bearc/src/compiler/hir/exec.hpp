//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_HIR_EXEC_HPP
#define COMPILER_HIR_EXEC_HPP

#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include <compiler/hir/scope.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace hir {

// ------ struct impls -------

struct Block {
    using id_type = BlockId;
    IdSlice<ExecId> execs;
    IdSlice<DefId> defs;
    LexicalCtx lctx;
};

struct ExecBlock {
    BlockId block_id;
};

enum class jump_spot : uint8_t {
    // jump to a given exec
    start,
    // jump to the end of a given exec (jump to the following exec)
    end
};
struct ExecJump {
    ExecId block;
    jump_spot spot;
};

struct ExecBranch {
    ExecId condition;
    ExecId then_block;
    ExecId else_block;
};

struct ExecReturn {
    OptId<ExecId> return_value;
};

struct ExecYield {
    OptId<ExecId> yield_value;
};

/// models an l-value
struct ExecAssignable {
    DefId def_id;
    TypeId type_id;
};

// SymbolId corresponds to a builtin type
using ConstantValue
    = std::variant</* 0 */ SymbolId, /* 1 */ int8_t, /*2*/ uint8_t, /*3*/ int16_t, /*4*/ uint16_t,
                   /*5*/ int32_t, /*6*/ uint32_t,
                   /*7*/ int64_t, /*8*/ uint64_t, /*9*/ char, /*10*/ float, /*11*/ double,
                   /* 12 */ std::nullptr_t, /* 13 */ bool>;

/// corresponding to a constant builtin type
using i8 = int8_t;
using u8 = uint8_t;
using i16 = int16_t;
using u16 = uint16_t;
using i32 = int32_t;
using u32 = uint32_t;
using i64 = int64_t;
using u64 = uint64_t;
using usize = uint64_t;
using f32 = float;
using f64 = double;

/// represents literals/constant builtin types
struct ExecComptConstant : NodeWithVariantValue<ExecComptConstant> {
    ConstantValue value;
    bool matches_type(builtin_type type) const {
        switch (value.index()) {
        case 0:
            return type == builtin_type::str;
        case 1:
            return type == builtin_type::i8;
        case 2:
            return type == builtin_type::u8;
        case 3:
            return type == builtin_type::i16;
        case 4:
            return type == builtin_type::u16;
        case 5:
            return type == builtin_type::i32;
        case 6:
            return type == builtin_type::u32;
        case 7:
            return type == builtin_type::i64;
        case 8:
            return type == builtin_type::u64;
        case 9:
            return type == builtin_type::charr;
        case 10:
            return type == builtin_type::f32;
        case 11:
            return type == builtin_type::f64;
        case 12:
            return type == builtin_type::nullpointer;
        case 13:
            return type == builtin_type::boolean;
        default:
            assert(false && "unconsidered builtin type");
            return false;
        }
    }
    builtin_type type_builtin() const {
        switch (value.index()) {
        case 0:
            return builtin_type::str;
        case 1:
            return builtin_type::i8;
        case 2:
            return builtin_type::u8;
        case 3:
            return builtin_type::i16;
        case 4:
            return builtin_type::u16;
        case 5:
            return builtin_type::i32;
        case 6:
            return builtin_type::u32;
        case 7:
            return builtin_type::i64;
        case 8:
            return builtin_type::u64;
        case 9:
            return builtin_type::charr;
        case 10:
            return builtin_type::f32;
        case 11:
            return builtin_type::f64;
        case 12:
            return builtin_type::nullpointer;
        case 13:
            return builtin_type::boolean;
        default:
            assert(false && "unconsidered builtin type");
            return builtin_type::voidd;
        }
    }
    [[nodiscard]] size_t to_size() const {
        switch (this->type_builtin()) {
        case builtin_type::u8:
            return as<u8>();
        case builtin_type::i8:
            return as<i8>();
        case builtin_type::u16:
            return as<u16>();
        case builtin_type::i16:
            return as<i16>();
        case builtin_type::u32:
            return as<u32>();
        case builtin_type::i32:
            return as<i32>();
        case builtin_type::u64:
            return as<u64>();
        case builtin_type::i64:
            return as<i64>();
        case builtin_type::charr:
            return as<char>();
        case builtin_type::f32:
            return static_cast<size_t>(std::bit_cast<u32>(as<f32>()));
        case builtin_type::f64:
            return std::bit_cast<size_t>(as<f64>());
        case builtin_type::voidd:
            return 0;
        case builtin_type::str:
            return as<SymbolId>().raw();
        case builtin_type::nullpointer:
            return 0;
        case builtin_type::boolean:
            return as<bool>();
        }
        std::unreachable();
        return 0;
    }
    [[nodiscard]] int64_t to_i64() const {
        switch (this->type_builtin()) {
        case builtin_type::u8:
            return as<u8>();
        case builtin_type::i8:
            return as<i8>();
        case builtin_type::u16:
            return as<u16>();
        case builtin_type::i16:
            return as<i16>();
        case builtin_type::u32:
            return as<u32>();
        case builtin_type::i32:
            return as<i32>();
        case builtin_type::u64:
            return static_cast<int64_t>(as<u64>());
        case builtin_type::i64:
            return as<i64>();
        case builtin_type::charr:
            return as<char>();
        case builtin_type::f32:
            return static_cast<int64_t>(std::bit_cast<int32_t>(as<f32>()));
        case builtin_type::f64:
            return std::bit_cast<int64_t>(as<f64>());
        case builtin_type::voidd:
            return 0;
        case builtin_type::str:
            return as<SymbolId>().raw();
        case builtin_type::nullpointer:
            return 0;
        case builtin_type::boolean:
            return as<bool>();
        }
        std::unreachable();
        return 0;
    }

    // treats signed/unsigned/floats/other as distinct, but not subcategories
    // example: u8 and u32 are the same, but not i8 and u32 unless the signed value is non-negative
    [[nodiscard]] size_t hash_identity() const {
        switch (this->type_builtin()) {
        case builtin_type::u8:
            return 0;
        case builtin_type::i8:
            return (as<i8>() < 0) ? 1 : 0;
        case builtin_type::u16:
            return 0;
        case builtin_type::i16:
            return (as<i16>() < 0) ? 1 : 0;
        case builtin_type::u32:
            return 0;
        case builtin_type::i32:
            return (as<i32>() < 0) ? 1 : 0;
        case builtin_type::u64:
            return 0;
        case builtin_type::i64:
            return (as<i64>() < 0) ? 1 : 0;
        case builtin_type::charr:
            return 2;
        case builtin_type::f32:
        case builtin_type::f64:
            return 3;
        case builtin_type::voidd:
            return 4;
        case builtin_type::str:
            return 5;
        case builtin_type::nullpointer:
            return 6;
        case builtin_type::boolean:
            return 7;
        }
        std::unreachable();
        return 0;
    }

    bool is_integral() const {
        switch (this->type_builtin()) {
        case builtin_type::i8:
        case builtin_type::i16:
        case builtin_type::i32:
        case builtin_type::i64:
        case builtin_type::u8:
        case builtin_type::u16:
        case builtin_type::u32:
        case builtin_type::u64:
            return true;
        case builtin_type::charr:
        case builtin_type::f32:
        case builtin_type::f64:
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
        case builtin_type::boolean:
            break;
        }
        return false;
    }

    bool is_signed_integral() const {
        switch (this->type_builtin()) {
        case builtin_type::i8:
        case builtin_type::i16:
        case builtin_type::i32:
        case builtin_type::i64:
            return true;
        case builtin_type::charr:
        case builtin_type::f32:
        case builtin_type::f64:
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
        case builtin_type::boolean:
        case builtin_type::u8:
        case builtin_type::u16:
        case builtin_type::u32:
        case builtin_type::u64:
            break;
        }
        return false;
    }
    bool is_unsigned_integral() const {
        switch (this->type_builtin()) {
        case builtin_type::u8:
        case builtin_type::u16:
        case builtin_type::u32:
        case builtin_type::u64:
            return true;
        case builtin_type::i8:
        case builtin_type::i16:
        case builtin_type::i32:
        case builtin_type::i64:
        case builtin_type::charr:
        case builtin_type::f32:
        case builtin_type::f64:
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
        case builtin_type::boolean:
            break;
        }
        return false;
    }

    bool less_than_signed(ExecComptConstant other) const { return to_i64() < other.to_i64(); }

    bool less_than_unsigned(ExecComptConstant other) const { return to_size() < other.to_size(); }

    bool less_than_or_equal_signed(ExecComptConstant other) const {
        return to_i64() <= other.to_i64();
    }

    bool less_than_or_equal_unsigned(ExecComptConstant other) const {
        return to_size() <= other.to_size();
    }

    bool greater_than_signed(ExecComptConstant other) const { return to_i64() > other.to_i64(); }

    bool greater_than_unsigned(ExecComptConstant other) const {
        return to_size() > other.to_size();
    }

    bool greater_than_or_equal_signed(ExecComptConstant other) const {
        return to_i64() >= other.to_i64();
    }

    bool greater_than_or_equal_unsigned(ExecComptConstant other) const {
        return to_size() >= other.to_size();
    }

    // straight up string converter, use this mostly just for debugging
    std::string to_string() const;

    std::string to_string(const Context& ctx) const;

    // basically a string converter but fancy
    SymbolId to_symbol_id(Context& ctx) const;

    // returns none if conversion fails, diagnostics must be reported outside of this method
    [[nodiscard]] std::optional<ExecComptConstant> try_safe_convert_to(builtin_type type) const;

    ExecComptConstant(ConstantValue constval) : value{constval} {}

    [[nodiscard]] std::optional<ExecComptConstant> try_up_convert_to(builtin_type type) const;
    [[nodiscard]] std::optional<ExecComptConstant> try_down_convert_to(builtin_type type) const;

    [[nodiscard]] bool has_binary_op(binary_op op) const;
    [[nodiscard]] bool has_unary_op(unary_op op) const;

    [[nodiscard]] bool equals_zero() const;

    using ExecConst = ExecComptConstant;

    [[nodiscard]] static std::optional<ExecConst> plus(Context& ctx, ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> minus(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> multiply(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> divide(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> mod(ExecConst lhs, ExecConst rhs);

    [[nodiscard]] static std::optional<ExecConst> bit_and(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> bit_or(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> bit_xor(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> bit_lsh(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> bit_rsha(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> bit_rshl(ExecConst lhs, ExecConst rhs);

    [[nodiscard]] static std::optional<ExecConst> greater_than(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> less_than(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> greater_than_or_equal(ExecConst lhs,
                                                                        ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> less_than_or_equal(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> equal(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> not_equal(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> bool_and(ExecConst lhs, ExecConst rhs);
    [[nodiscard]] static std::optional<ExecConst> bool_or(ExecConst lhs, ExecConst rhs);

    [[nodiscard]] static std::optional<ExecConst> preunary_plus(ExecConst ec);
    [[nodiscard]] static std::optional<ExecConst> preunary_minus(ExecConst ec);
    [[nodiscard]] static std::optional<ExecConst> preunary_bool_not(ExecConst ec);
    [[nodiscard]] static std::optional<ExecConst> preunary_bit_not(ExecConst ec);
};

using ExecConst = ExecComptConstant;

struct ExecListLiteral {
    IdSlice<ExecId> elems;
    OptId<TypeId> elem_type_id;
    size_t len() const noexcept { return elems.len(); }
};

struct ExecRange {
    ExecId start;
    // non-inclusive end
    ExecId end;
};

struct ExecAssignment {
    ExecId lhs;
    ExecId rhs;
};

struct ExecMemberAccess {
    ExecId owner;
    DefId member;
};

struct ExecBinary {
    ExecId lhs;
    ExecId rhs;
    binary_op op;
};

struct ExecCast {
    ExecId exec;
    TypeId target_tid;
};

struct ExecSubscript {
    ExecId base;
    ExecId index;
};

struct ExecFnCall {
    ExecId callee;
    IdSlice<ExecId> args;
};

struct ExecBorrow {
    DefId borrowee;
};

struct ExecDeref {
    ExecId dereferenced;
};

struct ExecAddrOf {
    ExecId addressed;
};

struct ExecUnionInit {
    ExecId member_init;
    DefId union_def_id;
    HirSize active_member_idx;
    bool move{false};
};

struct ExecVariantInit {
    /// this should be a ExecVariantFieldInit
    ExecId payload_init;
    DefId variant_def_id;
    HirSize active_member_idx;
};

struct ExecVariantFieldInit {
    IdSlice<ExecId> member_inits;
    DefId variant_field_def_id;
};

struct ExecStructInit {
    IdSlice<ExecId> member_inits;
    DefId struct_def_id;
    bool anonymous{false};
};

struct ExecFnPtr {
    DefId func_def_id;
    OptId<TypeId> fn_ptr_tid;
};

struct ExecMatch {
    ExecId scrutinee;
    IdSlice<ExecId> branches;
};

struct ExecMatchBranch {
    IdSlice<ExecId> patterns;
    ExecId body;
};

// ^^^^^^ struct impls ^^^^^^^^

/// main exec variant
using ExecValue = std::variant<
    // blocks / statements
    ExecBlock, ExecBranch, ExecReturn, ExecYield, ExecJump,

    // expressions
    ExecUnionInit, ExecVariantInit, ExecStructInit, ExecAssignable, ExecComptConstant,
    ExecListLiteral, ExecAssignment, ExecMemberAccess, ExecBinary, ExecCast, ExecSubscript,
    ExecFnCall, ExecBorrow, ExecAddrOf, ExecDeref, ExecMatch, ExecMatchBranch, ExecFnPtr,
    ExecVariantFieldInit, ExecRange>;

/// main exec structure, corresponds to an hir::ExecId
struct Exec : NodeWithVariantValue<Exec> {
    using id_type = ExecId;
    using value_type = ExecValue;
    ExecValue value;
    const Span span;
    bool compt;
    Exec(Context& ctx, ExecValue value, Span span, bool should_be_compt);
    static bool is_equivalent(const Context& ctx, ExecId eid1, ExecId eid2);

  private:
    bool can_be_compt();
};

std::string exec_to_string(Context& ctx, ExecId eid);

bool e_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2);

bool e_hash_exec(const Context& ctx, ExecId eid);

bool e_implicit_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2);

bool e_implicit_hash_exec(const Context& ctx, ExecId eid);

enum class equivalence : uint8_t {
    implicit,
    exact,
};

template <IsId T, equivalence equiv> class ExecHashMap {
    struct Entry {
        ExecId key_id;
        T val_id;
        size_t hash;
        Entry* next;
        Entry(ExecId key_id, T val_id, size_t hash, Entry* next)
            : key_id(key_id), val_id{val_id}, hash(hash), next(next) {}
    };
    static constexpr size_t DEFAULT_CAP = 128;
    static constexpr double LOAD_FACTOR = 1.25;
    static constexpr size_t GROWTH_FACTOR = 2;
    // internally, this consider mut recursively:
    using considerer_type = DoConsiderMut;
    Context& context;
    DataArena& arena;

    Entry** buckets;
    size_t count;
    size_t capacity;

    static constexpr auto equivalence_func
        = (equiv == equivalence::implicit) ? e_implicit_equivalent_exec : e_equivalent_exec;
    static constexpr auto hash_func
        = (equiv == equivalence::implicit) ? e_implicit_hash_exec : e_hash_exec;

    void rehash(size_t new_capacity) {
        Entry** new_buckets = arena.alloc_as<Entry**>(sizeof(Entry*) * new_capacity);
        memset(static_cast<void*>(new_buckets), 0, new_capacity * sizeof(Entry*));
        for (size_t i = 0; i < this->capacity; i++) {
            Entry* curr = this->buckets[i];
            while (curr) {
                Entry* next = curr->next;
                // just move curr into the new chain
                put_new_head_on_chain(new_buckets + index(curr->hash, new_capacity), curr);
                curr = next;
            }
        }
        this->capacity = new_capacity;
        this->buckets = new_buckets; // don't delete old buckets since arena will clean up later
    }

    bool same_structure(ExecId eid1, ExecId eid2) const {
        return equivalence_func(context, eid1, eid2);
    }
    size_t hash(ExecId eid) const { return hash_func(context, eid); }

    static size_t index(size_t hash, size_t cap) { return hash % cap; }

    static void put_new_head_on_chain(Entry** chain, Entry* new_entry) {
        assert(chain);
        new_entry->next = *chain;
        *chain = new_entry;
    }

  public:
    ExecHashMap(Context& context, DataArena& arena, HirSize capacity)
        : context(context), arena(arena), count{0} {
        this->capacity = (capacity > DEFAULT_CAP) ? capacity : DEFAULT_CAP;
        buckets = arena.alloc_as<Entry**>(this->capacity * sizeof(Entry*));

        // zero-init buckets
        memset(static_cast<void*>(buckets), 0, this->capacity * sizeof(Entry*));
    }
    // returns an optional ExecId of the existing ExecId inside the map
    OptId<T> at(ExecId eid) const {
        size_t hash_val = hash(eid);
        Entry* curr = this->buckets[index(hash_val, this->capacity)];
        while (curr) {
            if (hash_val == curr->hash && same_structure(curr->key_id, eid)) {
                return curr->val_id;
            }
            curr = curr->next;
        }
        return {};
    }
    // only use after at returns none to avoid duplicate inserts
    void insert(ExecId eid, T val_id) {
        size_t hash_val = hash(eid);
        Entry** chain = this->buckets + index(hash_val, this->capacity);
        Entry* new_entry = arena.alloc_type<Entry>();
        ::new (new_entry) Entry{eid, val_id, hash_val, nullptr};
        put_new_head_on_chain(chain, new_entry);
        ++this->count;
    }

    struct Iter {
        using iterator_category = std::forward_iterator_tag;

        Iter(const ExecHashMap& map, Entry* curr, size_t curr_bucket)
            : map{map}, curr{curr}, curr_bucket{curr_bucket} {}
        Iter(const ExecHashMap& map) : map{map}, curr{nullptr}, curr_bucket{0} {
            for (size_t i = 0; i < map.capacity; ++i) {
                if (map.buckets[i] != nullptr) {
                    curr = map.buckets[i];
                }
            }
        }

        ExecId operator*() const noexcept { return this->curr->key_id; }

        Iter& operator++() noexcept {
            if (!curr) {
                return *this;
            }

            if (curr->next) {
                curr = curr->next;
            }

            while (curr_bucket < map.capacity) {
                if (map.buckets[curr_bucket] != nullptr) {
                    curr = map.buckets[curr_bucket];
                }
                ++curr_bucket;
            }

            if (curr_bucket == map.capacity) {
                this->curr = nullptr; // end of the road
            }

            return *this;
        }

        Iter operator++(int) noexcept {
            Iter tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const Iter& a, const Iter& b) noexcept { return a.curr == b.curr; }
        friend bool operator!=(const Iter& a, const Iter& b) noexcept { return !(a == b); }

      private:
        const ExecHashMap& map;
        Entry* curr{nullptr};
        size_t curr_bucket;
    };
    Iter begin() const noexcept { return Iter{*this}; }
    Iter end() const noexcept { return Iter{*this, nullptr, 0}; }
};

} // namespace hir

#endif
