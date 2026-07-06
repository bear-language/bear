//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_EXEC_HPP
#define COMPILER_HIR_EXEC_HPP

#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace hir {

// ------ struct impls -------

struct ExecBlock {
    ScopeId scope;
    IdSlice<ExecId> execs;
};

struct ExecExprStmt {
    ExecId expr;
};

struct ExecBreakStmt {};

struct ExecContinueStmt {};

struct ExecIfStmt {
    ExecId condition;
    ExecId block;
    OptId<ExecId> else_stmt;
};

struct ExecLoopStmt {
    ExecId block;
};

struct ExecReturnStmt {
    OptId<ExecId> return_value;
};

struct ExecYieldStmt {
    OptId<ExecId> yield_value;
};

struct ExecExprVariable {
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
struct ExecExprComptConstant : NodeWithVariantValue<ExecExprComptConstant> {
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

    // straight up string converter, use this mostly just for debugging
    std::string to_string() const;

    std::string to_string(const Context& ctx) const;

    // basically a string converter but fancy
    SymbolId to_symbol_id(Context& ctx) const;

    // returns none if conversion fails, diagnostics must be reported outside of this method
    [[nodiscard]] std::optional<ExecExprComptConstant> try_safe_convert_to(builtin_type type) const;

    ExecExprComptConstant(ConstantValue constval) : value{constval} {}

    [[nodiscard]] std::optional<ExecExprComptConstant> try_up_convert_to(builtin_type type) const;
    [[nodiscard]] std::optional<ExecExprComptConstant> try_down_convert_to(builtin_type type) const;

    [[nodiscard]] bool has_binary_op(binary_op op) const;
    [[nodiscard]] bool has_unary_op(unary_op op) const;

    [[nodiscard]] bool equals_zero() const;

    using ExecConst = ExecExprComptConstant;

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

using ExecConst = ExecExprComptConstant;

struct ExecExprListLiteral {
    IdSlice<ExecId> elems;
    OptId<TypeId> elem_type_id;
    size_t len() const noexcept { return elems.len(); }
};

struct ExecRange {
    ExecId start;
    // non-inclusive end
    ExecId end;
};

struct ExecExprAssignMove {
    ExecId lhs;
    ExecId rhs;
};

struct ExecExprAssignEqual {
    ExecId lhs;
    ExecId rhs;
};

struct ExecExprIs {
    ExecId variant_instance;
    ExecId variant_decomp;
};

struct ExecExprMemberAccess {
    ExecId owner;
    ExecId member;
};

struct ExecExprPointerMemberAccess {
    ExecId owner;
    ExecId member;
};

struct ExecExprBinary {
    ExecId lhs;
    ExecId rhs;
    binary_op op;
};

struct ExecExprCast {
    ExecId expr;
    TypeId target;
};

struct ExecExprPreUnary {
    ExecId expr;
    unary_op op;
};

struct ExecExprPostUnary {
    ExecId expr;
    unary_op op;
};

struct ExecExprSubscript {
    ExecId base;
    ExecId index;
};

struct ExecExprFnCall {
    ExecId callee;
    IdSlice<ExecId> args;
};

struct ExecExprBorrow {
    ExecId borrowee;
};

struct ExecExprDeref {
    ExecId expr;
};

struct ExecExprUnionInit {
    ExecId member_init;
    DefId union_def_id;
    HirSize active_member_idx;
    bool move;
};

struct ExecExprVariantInit {
    ExecId payload_init;
    DefId variant_def_id;
    HirSize active_member_idx;
};

struct ExecVariantFieldInit {
    IdSlice<ExecId> member_inits;
    DefId variant_field_def_id;
};

struct ExecExprStructInit {
    IdSlice<ExecId> member_inits;
    DefId struct_def_id;
};

struct ExecExprStructMemberInit {
    DefId field_def;
    ExecId value;
    bool move;
};

struct ExecFnPtr {
    DefId func_def_id;
    OptId<TypeId> fn_ptr_tid;
};

struct ExecExprClosure {
    IdSlice<DefId> params;
    IdSlice<DefId> captures;
    TypeId return_type;
    bool move;
};

struct ExecExprVariantDecomp {
    IdSlice<DefId> fields;
    DefId def;
};

struct ExecExprMatch {
    ExecId scrutinee;
    IdSlice<ExecId> branches;
};

struct ExecExprMatchBranch {
    IdSlice<ExecId> patterns;
    ExecId body;
};

// ^^^^^^ struct impls ^^^^^^^^

/// main exec variant
using ExecValue = std::variant<
    // blocks / statements
    ExecBlock, ExecExprStmt, ExecBreakStmt, ExecContinueStmt, ExecIfStmt, ExecLoopStmt,
    ExecReturnStmt, ExecYieldStmt,

    // expressions
    ExecExprUnionInit, ExecExprVariantInit, ExecExprStructInit, ExecExprStructMemberInit,
    ExecExprVariable, ExecExprComptConstant, ExecExprListLiteral, ExecExprAssignMove,
    ExecExprAssignEqual, ExecExprIs, ExecExprMemberAccess, ExecExprPointerMemberAccess,
    ExecExprBinary, ExecExprCast, ExecExprPreUnary, ExecExprPostUnary, ExecExprSubscript,
    ExecExprFnCall, ExecExprBorrow, ExecExprDeref, ExecExprClosure, ExecExprVariantDecomp,
    ExecExprMatch, ExecExprMatchBranch, ExecFnPtr, ExecVariantFieldInit, ExecRange>;

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
    bool can_be_compt(const Context& ctx);
};

std::string exec_to_string(Context& ctx, ExecId eid);

class ExecHashMap {
    struct Entry {
        ExecId key_id;
        size_t hash;
        Entry* next;
        Entry(ExecId key_id, size_t hash, Entry* next) : key_id(key_id), hash(hash), next(next) {}
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

    void rehash(size_t new_capacity);
    bool same_structure(ExecId eid1, ExecId eid2) const;
    size_t hash(ExecId eid) const;
    static size_t index(size_t hash, size_t cap);
    static void put_new_head_on_chain(Entry** chain, Entry* new_entry);

  public:
    ExecHashMap(Context& context, DataArena& arena, HirSize capacity);
    // returns an optional ExecId of the existing ExecId inside the map
    OptId<ExecId> at(ExecId eid) const;
    // only use after at returns none to avoid duplicate inserts
    void insert(ExecId eid);
};

} // namespace hir

#endif
