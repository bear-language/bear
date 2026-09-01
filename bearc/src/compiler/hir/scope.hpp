//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_SCOPE_HPP
#define COMPILER_HIR_SCOPE_HPP

#include "compiler/hir/id_hash_map.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/span.hpp"
#include "utils/data_arena.hpp"
#include <cstdint>

namespace hir {

// TODO
class MoveMap {
    IdHashMap<DefId, ExecId> defs_to_execs;
    OptId<MoveMapId> parent;

    static constexpr size_t DEFAULT_CAP = 0x10;

  public:
    using id_type = MoveMapId;
    MoveMap(DataArena& arena, OptId<MoveMapId> parent)
        : defs_to_execs{arena, DEFAULT_CAP}, parent{parent} {}
    /// inserts a move for a given def to be moved at a given exec
    void insert(DefId did, ExecId eid) { defs_to_execs.insert(did, eid); }
    [[nodiscard]] OptId<ExecId> local_move_for(DefId did) { return defs_to_execs.at(did); }
    [[nodiscard]] OptId<ExecId> move_for(const Context& ctx, DefId did) {
        const auto maybe_eid = defs_to_execs.at(did);
        if (maybe_eid.has_value()) {
            return maybe_eid.as_id();
        }
        // TODO
    }
};

struct LexicalCtx {
    ScopeId scope;
    MoveMapId map;
};

class Context;

using ScopeIdMap = IdHashMap<SymbolId, DefId>;

enum class scope_kind : uint8_t {
    namespacee,
    variable,
    type,
};

struct DummyCall {
    void operator()();
};

template <typename T> struct Tester : T, DummyCall {};

template <typename C>
concept Callable = std::is_class_v<C> && !requires(Tester<C> t) { &Tester<C>::operator(); };

/**
 * maps SymbolId -> DefId
 * models named blocks/namespaces, such as function bodies or ctrl flow blocks
 */
class Scope {
    DataArena& arena;
    /// module, struct, and variant names
    ScopeIdMap namespaces;
    /// var foo;
    ScopeIdMap variables;
    /// structs, variants, unions, deftypes
    ScopeIdMap types;
    OptId<ScopeId> parent_;
    const bool top_level;
    void insert(SymbolId symbol, DefId def, scope_kind kind);
    static OptId<DefId> look_up_impl(const Context& context, ScopeId local_scope_id,
                                     SymbolId symbol, scope_kind kind);

  public:
    // this may need to be tuned for a balance between cache locality and limited rehashing
    static constexpr size_t DEFAULT_CAP = 0x100;
    using id_type = ScopeId;
    bool is_top_level() const { return top_level; };
    // constructs a non-top-level scope with a parent
    Scope(ScopeId parent, DataArena& arena);
    // constructs a scope with an optional parent
    Scope(OptId<ScopeId> parent, DataArena& arena);
    // constructs a top level scope
    Scope(DataArena& arena);
    // constructs a non-top-level scope with a given capacity parent
    Scope(OptId<ScopeId> parent, size_t capacity, DataArena& arena);
    // constructs a top level scope with a given capacity
    Scope(size_t capacity, DataArena& arena);
    enum class storage : uint8_t {
        variables,
        types,
    };
    // construct a scope with only storage for a specific kind of def
    Scope(ScopeId parent, size_t capacity, DataArena& arena, storage storage);
    static OptId<DefId> look_up_namespace(const Context& context, ScopeId local_scope,
                                          SymbolId symbol);
    static OptId<DefId> look_up_variable(const Context& context, ScopeId local_scope,
                                         SymbolId symbol);
    static OptId<DefId> look_up_type(const Context& context, ScopeId local_scope, SymbolId symbol);

    void insert_namespace(SymbolId symbol, DefId def);
    void insert_variable(SymbolId symbol, DefId def);
    void insert_type(SymbolId symbol, DefId def);

    OptId<DefId> already_defines_variable(SymbolId symbol) const;
    OptId<DefId> already_defines_type(SymbolId symbol) const;
    // looks up a namespace without checking parent scopes for any definitons
    static OptId<DefId> look_up_local_namespace(const Context& context, ScopeId local_scope,
                                                SymbolId symbol);
    static OptId<DefId> look_up_local_type(const Context& context, ScopeId local_scope,
                                           SymbolId symbol);
    static OptId<DefId> look_up_local_variable(const Context& context, ScopeId local_scope,
                                               SymbolId symbol);

    [[nodiscard]] OptId<ScopeId> parent() const noexcept { return parent_; }

    using Entry = ScopeIdMap::Entry;

    /// call some functor F for each locally namespace defined in the scope
    /// note: F must be callable with F(Scope::Entry)
    template <Callable F> void for_each_local_namespace(F f) const {
        for (Entry e : this->namespaces) {
            f(e);
        }
    }

    /// call some functor F for each locally type defined in the scope
    /// note: F must be callable with F(Scope::Entry)
    template <Callable F> void for_each_local_type(F f) const {
        for (Entry e : this->types) {
            f(e);
        }
    }

    /// call some functor F for each locally type defined in the scope
    /// note: F must be callable with F(Scope::Entry)
    template <Callable F> void for_each_local_variable(F f) const {
        for (Entry e : this->variables) {
            f(e);
        }
    }
};

struct SpanScopePair {
    Span span;
    ScopeId scope;
};

} // namespace hir

#endif
