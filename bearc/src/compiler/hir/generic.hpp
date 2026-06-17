//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_GENERICS_HPP
#define COMPILER_HIR_GENERICS_HPP

#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include "utils/data_arena.hpp"
#include <variant>
namespace hir {

using GenericArgValue = std::variant<ExecId, TypeId>;
struct GenericArg : NodeWithVariantValue<GenericArg> {
    using id_type = GenericArgId;
    using value_type = GenericArgValue;
    GenericArgValue value;
    explicit GenericArg(GenericArgValue value) : value{value} {}
};

struct GenericParamType {
    IdSlice<DefId> contracts;
};

struct GenericParamVariable {
    TypeId type;
};

using GenericParamValue = std::variant<GenericParamType, GenericParamVariable>;
struct GenericParam : NodeWithVariantValue<GenericParam> {
    using id_type = GenericParamId;
    using value_type = GenericParamValue;
    GenericParamValue value;
    Span span;
    SymbolId name;
    explicit GenericParam(GenericParamValue value, SymbolId name, Span span)
        : value{value}, span{span}, name{name} {}
};

[[nodiscard]] size_t hash_gen_arg(const Context& ctx, GenericArgId gen_arg_id);
[[nodiscard]] bool equivalent_gen_arg(const Context& ctx, GenericArgId gid1, GenericArgId gid2);
[[nodiscard]] size_t hash_gen_arg_id_slice(const Context& ctx, GenericArgIdSliceId gid);
[[nodiscard]] bool equivalent_gen_arg_id_slice(const Context& ctx, GenericArgIdSliceId sid1,
                                               GenericArgIdSliceId sid2);

class CanonicalComptArgsTable {
    struct Entry {
        GenericArgIdSliceId key_id;
        CanonicalGenericArgsId val;
        size_t hash;
        Entry* next;
        Entry(GenericArgIdSliceId key_id, CanonicalGenericArgsId val, size_t hash, Entry* next)
            : key_id(key_id), val(val), hash(hash), next(next) {}
    };
    static constexpr size_t DEFAULT_CAP = 128;
    static constexpr double LOAD_FACTOR = 1.25;
    static constexpr size_t GROWTH_FACTOR = 2;
    Context& context;
    DataArena& arena;

    Entry** buckets;
    size_t count;
    size_t capacity;

    void rehash(size_t new_capacity);
    bool same_structure(GenericArgIdSliceId gid1, GenericArgIdSliceId gid2) const;
    size_t hash(GenericArgIdSliceId id) const;
    static size_t index(size_t hash, size_t cap);
    static void put_new_head_on_chain(Entry** chain, Entry* new_entry);
    // only use after at returns none to avoid duplicate inserts
    void insert(GenericArgIdSliceId id, CanonicalGenericArgsId cid);

  public:
    CanonicalComptArgsTable(Context& context, DataArena& arena, HirSize capacity)
        : context{context}, arena{arena}, capacity{capacity} {}
    OptId<CanonicalGenericArgsId> at(GenericArgIdSliceId id) const;
    [[nodiscard]] CanonicalGenericArgsId canonical(GenericArgIdSliceId id);
};

} // namespace hir

#endif // !COMPILER_HIR_GENERICS_HPP
