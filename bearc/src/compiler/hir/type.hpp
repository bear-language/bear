//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_TYPE_HPP
#define COMPILER_HIR_TYPE_HPP

#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include "utils/data_arena.hpp"
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <variant>
namespace hir {

class Context;

// ------ struct impls -------

enum class builtin_type : uint8_t {
    u8,
    i8,
    u16,
    i16,
    u32,
    i32,
    u64,
    i64,
    usize,
    charr,
    f32,
    f64,
    voidd,
    str,
    nullpointer,
    boolean,
};

const char* builtin_type_to_cstr(builtin_type t);
std::optional<builtin_type> id_tkn_slice_to_maybe_builtin(token_ptr_slice_t tkn_slice);

struct TypeVar {};

struct TypeBuiltin {
    builtin_type type;
};

struct TypeStruct {
    DefId def_id;
    // can be empty even if generic if this type was generated
    OptId<GenericArgIdSliceId> gen_args_slice;
    // use this to determine if generic
    bool generic = false;
};

struct TypeUnion {
    DefId def_id;
};

struct TypeVariant {
    DefId def_id;
    // can be empty even if generic if this type was generated
    OptId<GenericArgIdSliceId> gen_args_slice;
    // use this to determine if generic
    bool generic = false;
};

struct TypeDeftype {
    TypeId true_type;
    DefId definition;
};

struct TypeArr {
    TypeId inner;
    OptId<ExecId> compt_size_expr;
    size_t canonical_size;
};

struct TypeSlice {
    TypeId inner;
};

struct TypeRef {
    TypeId inner;
};

struct TypePtr {
    TypeId inner;
};

struct TypeFnPtr {
    IdSlice<TypeId> param_types;
    OptId<TypeId> return_type;
};

struct TypeVariadic {
    TypeId inner;
};

// ^^^^^^ struct impls ^^^^^^^^

/// main exec union
using TypeValue = std::variant<TypeBuiltin, TypeStruct, TypeDeftype, TypeArr, TypeSlice, TypeRef,
                               TypePtr, TypeFnPtr, TypeVariadic, TypeVar, TypeVariant, TypeUnion>;

struct Type : NodeWithVariantValue<Type> {
    using id_type = TypeId;
    using value_type = TypeValue;
    TypeValue value;
    Span span;
    CanonicalTypeId canonical;
    bool mut;
    void set_value(TypeValue value) { this->value = value; }
    // compares identical types (including mut)
    static bool is_same(const Context& ctx, TypeId tid1, TypeId tid2);
    // canonical should get immediately set by context
    Type(const TypeValue& value, Span span, bool mut)
        : value{value}, span{span}, canonical{HIR_ID_NONE}, mut{mut} {}
};

template <class F>
concept TypeTransformerFunctor = requires(F f, const Context& context, const Type& t1,
                                          const Type& t2) {
    typename F::value_type;
    // constructor must take a const ref to a context
    { F{context} };
    // single invocation
    { f(t1) } -> std::convertible_to<typename F::value_type>;
    // double invocation
    { f(t1, t2) } -> std::convertible_to<typename F::value_type>;
    {
        f.transform(std::declval<typename F::value_type>(), std::declval<typename F::value_type>())
    } -> std::convertible_to<typename F::value_type>;
};

template <TypeTransformerFunctor F> class TypeTransformer {
    const Context& context;
    OptId<TypeId> try_inner(const Type& type);
    Type get_type(TypeId tid) const noexcept;
    Type get_type_as_mentioned(TypeId tid) const noexcept;

    typename F::value_type invoke(TypeId tid, auto get_type_functor);
    typename F::value_type invoke(TypeId tid1, TypeId tid2, auto get_type_functor);

  public:
    TypeTransformer(const Context& context) : context(context) {}
    typename F::value_type operator()(TypeId tid1, TypeId tid2);
    typename F::value_type operator()(TypeId tid);
    typename F::value_type invoke_as_mentioned(TypeId tid);
    typename F::value_type invoke_as_mentioned(TypeId tid1, TypeId tid2);
};

template <class C>
concept ConsiderMut = requires(C c) {
    { c.considers_mut() } -> std::convertible_to<bool>;
};

struct DoConsiderMut {
    static consteval bool considers_mut() { return true; }
};
// using this means mut-ness will need to be considered effectively lazily, consider the outermost
// mut at any given time
struct DoNotConsiderMut {
    static consteval bool considers_mut() { return false; }
};

template <ConsiderMut C> class TypeComparator {
    const Context& context;

  public:
    using value_type = bool;
    TypeComparator(const Context& context) : context(context) {}
    bool operator()(const Type& t1, const Type& t2) const;
    // single invocation -> mismatch => false
    bool operator()(const Type& t1) const { return false; } // NOLINT, intentionally taking t1
    static bool transform(bool res1, bool res2) { return res1 && res2; }
    static consteval bool considers_mut() { return C::considers_mut(); }
};

template <ConsiderMut C> class TypeInferer {
    const Context& context;

  public:
    using value_type = bool;
    TypeInferer(const Context& context) : context(context) {}
    bool operator()(const Type& t1, const Type& t2) const;
    // single invocation -> doesn't matter => true
    bool operator()(const Type& t1) const { return true; } // NOLINT, intentionally taking t1
    static bool transform(bool res1, bool res2) { return res1 && res2; }
    static consteval bool considers_mut() { return C::considers_mut(); }
};

class TypeContainsMut {
    const Context& context;

  public:
    using value_type = bool;
    TypeContainsMut(const Context& context) : context(context) {}
    bool
    operator()(const Type& t1,         // NOLINT
               const Type& t2) const { // NOLINT intentionally taking t1 and t2 here for the concept
        assert(false
               && "double invocation should not be called when checking if a type contains mut");
        return 0;
    }
    bool operator()(const Type& t1) const;
    static bool transform(bool res1, bool res2) { return res1 || res2; }
};

template <typename T> class TypeContainsSome {
    const Context& context;

  public:
    using value_type = bool;
    TypeContainsSome(const Context& context) : context(context) {}
    bool operator()(const Type& t1, const Type& t2) const { // NOLINT
        assert(
            false
            && "double invocation should not be called when checking if a type contains a deftype");
        return 0;
    }
    bool operator()(const Type& t1) const { return t1.holds<T>(); }
    static bool transform(bool res1, bool res2) { return res1 || res2; }
};

using TypeContainsVar = TypeContainsSome<TypeVar>;

using TypeContainsDeftype = TypeContainsSome<TypeDeftype>;

template <ConsiderMut C> class TypeHasher {
    const Context& context;

  public:
    using value_type = HirSize;
    TypeHasher(const Context& context) : context(context) {}
    // probably not needed for the hasher
    size_t operator()(const Type& t1, const Type& t2) const { // NOLINT
        assert(false && "double invocation should not be called when hashing");
        return 0;
    }
    size_t operator()(const Type& t1) const;
    static size_t transform(size_t res1, size_t res2);
    static consteval bool considers_mut() { return C::considers_mut(); }
};

struct TypeToStringValue {
    std::string str;
};

template <ConsiderMut C> class TypeToString {
    const Context& context;

  public:
    using value_type = TypeToStringValue;
    TypeToString(const Context& context) : context(context) {}
    // probably not needed for the hasher
    TypeToStringValue operator()(const Type& t1, const Type& t2) const { // NOLINT
        assert(false && "double invocation should not be called when using ToString");
        return {};
    }
    TypeToStringValue operator()(const Type& t1) const;
    static TypeToStringValue transform(TypeToStringValue res1, TypeToStringValue res2);
    static consteval bool considers_mut() { return C::considers_mut(); }
};

class CanonicalTypeTable {
    struct Entry {
        TypeId key_id;
        CanonicalTypeId val;
        size_t hash;
        Entry* next;
        Entry(TypeId key_id, CanonicalTypeId val, size_t hash, Entry* next)
            : key_id(key_id), val(val), hash(hash), next(next) {}
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
    bool same_structure(TypeId tid1, TypeId tid2) const;
    size_t hash(TypeId type) const;
    static size_t index(size_t hash, size_t cap);
    static void put_new_head_on_chain(Entry** chain, Entry* new_entry);
    // only use after at returns none to avoid duplicate inserts
    void insert(TypeId tid, CanonicalTypeId cid);

  public:
    CanonicalTypeTable(Context& context, DataArena& arena, HirSize capacity);
    OptId<CanonicalTypeId> at(TypeId tid) const;
    [[nodiscard]] CanonicalTypeId canonical(TypeId tid);
};

// function to determine whether a type contains mut
bool contains_mut(const Context& ctx, TypeId tid);

bool contains_deftype(const Context& ctx, TypeId tid);

// converts a TypeId to a string
std::string type_to_string_with_akas(const Context& ctx, TypeId tid);
// converts a TypeId to a string without any muts
std::string type_to_string_with_akas_without_muts(const Context& ctx, TypeId tid);

// converts a TypeId to a string
std::string type_to_string(const Context& ctx, TypeId tid);
// converts a TypeId to a string without any muts
std::string type_to_string_without_muts(const Context& ctx, TypeId tid);

// converts a TypeId to a string
std::string type_to_string_as_mentioned(const Context& ctx, TypeId tid);
// converts a TypeId to a string without any muts
std::string type_to_string_as_mentioned_without_muts(const Context& ctx, TypeId tid);

bool builtin_type_has_binary_op(builtin_type type, binary_op op);

bool builtin_type_has_unary_op(builtin_type type, unary_op op);

} // namespace hir

#endif
