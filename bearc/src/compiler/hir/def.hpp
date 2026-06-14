//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_DEF_H
#define COMPILER_HIR_DEF_H

#include "compiler/hir/indexing.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include <cstdint>
#include <utility>
#include <variant>

namespace hir {

// ------ struct impls -------

struct DefMalformed {};

struct DefModule {
    ScopeId scope;
};

struct DefFunction {

    struct ParamResolResult {
        IdSlice<DefId> params;
        bool poisoned = false;
    };

    IdSlice<DefId> params;
    IdSlice<TypeId> param_types;
    OptId<TypeId> return_type;
    OptId<ExecId> body;
    /// if this function was derived from an original generic function
    OptId<DefId> original;
    // generic args, if any
    OptId<CanonicalGenericArgsId> maybe_generic_args;
    bool discardable = false;
    bool takes_self = false;
    bool posioned = false;
    bool infinite_recursion = false;
    void poison() { posioned = true; }
    bool poisoned() const noexcept { return posioned; }
    bool infinitely_recurses() const noexcept { return infinite_recursion; }
    void poison_infinite_recursion() noexcept {
        posioned = true;
        infinite_recursion = true;
    }
};

struct DefGenericFunction {
    /// maps canonical lists of generics args to concrete instatiations
    CanonicalGenericArgsIdMapId generics_args_to_concrete_defs_map;
};

struct DefFunctionPrototype {
    IdSlice<DefId> params;
    IdSlice<TypeId> param_types;
    OptId<TypeId> return_type;
    bool takes_self;
};

struct DefVariable {
    TypeId type_id;
    OptId<ExecId> compt_value{};
    bool moved = false;
};

struct DefStruct {
    ScopeId scope;
    /// data members/fields, not functions
    IdSlice<DefId> ordered_members;
    IdSlice<DefId> contracts;
    /// if this struct was derived from an original generic struct
    OptId<DefId> orginal;
    // generic args, if any
    OptId<CanonicalGenericArgsId> maybe_generic_args;
};

struct DefGenericStruct {
    /// maps canonical lists of generics args to concrete instatiations
    CanonicalGenericArgsIdMapId generics_args_to_concrete_defs_map;
};

struct DefVariant {
    ScopeId scope;
    /// variant possiblities
    IdSlice<DefId> ordered_members;
    /// if this variant was derived from an original generic variant
    OptId<DefId> orginal;
    // generic args, if any
    OptId<CanonicalGenericArgsId> maybe_generic_args;
};

struct DefGenericVariant {
    /// maps canonical lists of generics args to concrete instatiations
    CanonicalGenericArgsIdMapId generics_args_to_concrete_defs_map;
};

struct DefVariantField {
    /// pointing to DefVariables which containg type and name info
    ScopeId scope;
    IdSlice<DefId> members;
};

struct DefUnion {
    ScopeId scope;
    // members, in order (although order doesn't matter much)
    IdSlice<DefId> ordered_members;
};

struct DefContract {
    IdSlice<DefId> funcs;
    ScopeId scope;
};

struct DefDeftype {
    TypeId type;
};

struct DefScopeWrapper {
    ScopeId scope;
};

struct DefUnevaluated {};

// ^^^^^^ struct impls ^^^^^^^^

/// main exec variant
using DefValue = std::variant<DefModule, DefFunction, DefGenericFunction, DefFunctionPrototype,
                              DefVariable, DefStruct, DefGenericStruct, DefVariant,
                              DefGenericVariant, DefVariantField, DefUnion, DefContract, DefDeftype,
                              DefScopeWrapper, DefUnevaluated, DefMalformed>;

enum class abi_lang : uint8_t {
    native = 0,
    c,
};

/// main exec structure, corresponds to an hir_exec_id_t
struct Def : NodeWithVariantValue<Def> {
    /// represents the resolution state corresponding to an hir::DefId
    enum class resol_state : uint8_t {
        unvisited = 0,
        top_level_visited,
        // can help catch circular defintions
        in_progress,
        resolved,
    };
    static const char* resol_state_to_str(resol_state st) {
        switch (st) {
        case resol_state::unvisited:
            return "unvisited";
        case resol_state::top_level_visited:
            return "top_level_visited";
        case resol_state::in_progress:
            return "in_progress";
        case resol_state::resolved:
            return "resolved";
        }
        std::unreachable();
        return nullptr;
    }

    /// represents the mention state corresponding to an hir::DefId
    enum class mention_state : uint8_t {
        unmentioned = 0,
        mentioned = 1,
        mutated = 2,
    };

    using id_type = DefId;
    /// underlying structure
    DefValue value;
    /// span in src
    const Span span;
    /// id corresponding to the interned identifier
    const SymbolId name;
    /// parent's definition, if any
    OptId<DefId> parent;
    static constexpr HirSize UNORDERED = HIR_SIZE_MAX;
    /// indicates member's order in a struct, equals NOT_ORDERED if unordered
    HirSize member_idx = UNORDERED;
    /// indicates pub (true) or hid (false) visibility
    const bool pub = false;
    /// indicates compt (compile-time)
    const bool compt = false;
    /// indicates static (storage duration)
    const bool statik = false;
    /// indicates generic
    const bool generic = false;
    /// indicates alignment preference, 0 = default alignment
    const uint8_t alignment_preference = 0;
    /// indicates ABI
    const abi_lang abi = abi_lang::native;

    Def(DefValue value, SymbolId name, bool pub, bool compt, bool statik, bool generic, Span span,
        OptId<DefId> parent, enum abi_lang abi = abi_lang::native)
        : value{value}, span{span}, name{name}, parent{parent}, pub{pub}, compt{compt},
          statik{statik}, generic{generic}, abi{abi} {}

    Def(DefValue value, SymbolId name, bool pub, bool compt, bool statik, bool generic, Span span,
        OptId<DefId> parent, uint8_t alignment_preference, enum abi_lang abi = abi_lang::native)
        : value{value}, span{span}, name{name}, parent{parent}, pub{pub}, compt{compt},
          statik{statik}, generic{generic}, alignment_preference{alignment_preference}, abi{abi} {}

    Def(DefValue value, SymbolId name, bool pub, bool compt, bool statik, bool generic, Span span,
        OptId<DefId> parent, uint8_t alignment_preference, HirSize member_idx,
        enum abi_lang abi = abi_lang::native)
        : value{value}, span{span}, name{name}, parent{parent}, member_idx{member_idx}, pub{pub},
          compt{compt}, statik{statik}, generic{generic},
          alignment_preference{alignment_preference}, abi{abi} {
        // ordered statik is malformed
        assert(!(is_ordered() && statik));
    }

    Def(SymbolId name, bool pub, Span span)
        : value{DefUnevaluated{}}, span{span}, name{name}, pub{pub} {}

    void set_value(DefValue value) { this->value = value; }
    bool is_ordered() const noexcept { return member_idx != UNORDERED; };
};

} // namespace hir

#endif
