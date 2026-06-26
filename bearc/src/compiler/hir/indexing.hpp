//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_INDEXING_HPP
#define COMPILER_HIR_INDEXING_HPP

#include <algorithm>
#include <assert.h>
#include <optional>
#include <stdint.h>
#include <string_view>

namespace hir {

// helper trait (useful for enumerating over various possible Ids in concepts)
template <typename T, typename... Ts> constexpr bool is_any_of_v = (std::is_same_v<T, Ts> || ...);

/// defines the underlying size of indices of vectors storing HIR nodes
using HirSize = uint32_t;

constexpr HirSize HIR_SIZE_MAX = UINT32_MAX;

/// represents a null HirId or its derivations, valued 0
inline constexpr HirSize HIR_ID_NONE = 0;

/// abstract typedef id for indexing into vectors of HIR nodes
using HirId = HirSize;

/// indicates that a type is indeed a flavor of HirId
template <typename T>
concept IsId = requires { typename T::id_tag; };

template <typename T> class Id {
    HirId value;

  public:
    using id_tag = T;
    constexpr explicit Id(HirId v) : value(v) {}
    constexpr Id() : value(HIR_ID_NONE) {}
    [[nodiscard]] constexpr HirId raw() const noexcept { return value; }
    friend constexpr bool operator==(Id<T> a, Id<T> b) { return a.value == b.value; }
    constexpr Id operator++() { return Id{++value}; }
    constexpr Id operator--() { return Id{--value}; }
    constexpr Id operator++(int) { return Id{value++}; }
    constexpr Id operator--(int) { return Id{value--}; }
};

class Symbol;
struct File;
class FileAst;
class Scope;
struct Exec;
struct Def;
struct Type;
struct GenericParam;
struct GenericArg;
struct Diagnostic;

/// primary means of tracking interned strings in the hir
using SymbolId = Id<Symbol>;

/// for addressing interned file names
using FileId = Id<File>;

/// for addressing asts
using FileAstId = Id<FileAst>;

/// for addressing scopes
/// - It is possible that a given ScopeId referes to a dead named scope, so precaution must be taken
/// that random or dead ScopeId values (that are not known valid ScopeIds to living scope refereces)
/// are not used
using ScopeId = Id<Scope>;

/// for addressing exec nodes
using ExecId = Id<Exec>;

/// for addressing definition nodes
using DefId = Id<Def>;

/// for addressing type nodes
using TypeId = Id<Type>;

/// for addressing generic argument nodes
using GenericArgId = Id<GenericArg>;

/// for addressing generic param nodes
using GenericParamId = Id<GenericParam>;

using DiagnosticId = Id<Diagnostic>;

/// to be stored in a HirSymbolId -> HirSymbol table
class Symbol {
    /// view into interned string arena for reversing symbols back into strings
    std::string_view string_view;

  public:
    using id_type = SymbolId;
    constexpr explicit Symbol(std::string_view string_view) : string_view(string_view) {}
    [[nodiscard]] constexpr std::string_view sv() const noexcept { return this->string_view; }
};

template <std::size_t N> struct StringLiteral {
    char value[N];

    // constexpr constructor to copy the string literal into the array
    constexpr StringLiteral(const char (&str)[N]) { std::copy_n(str, N, value); }
    constexpr const char* get() const { return value; }
};

/// repsents an index into the storage for a certain Id, to allow for slicing
template <hir::IsId T> class IdIdx {
    using points_to_type = T;
    HirId value;

  public:
    using id_tag = Id<T>;
    using self_type = IdIdx<T>;
    constexpr IdIdx() : value(HIR_ID_NONE) {}
    explicit IdIdx(HirId value) : value(value) {};
    constexpr HirId raw() const noexcept { return value; }
    constexpr self_type operator++() { return IdIdx{++value}; }
    constexpr self_type operator--() { return IdIdx{--value}; }
    constexpr self_type operator++(int) { return IdIdx{value++}; }
    constexpr self_type operator--(int) { return IdIdx{value--}; }
    constexpr self_type at(HirSize offset) { return IdIdx{value + offset}; }
    friend constexpr bool operator==(IdIdx<T> a, IdIdx<T> b) { return a.value == b.value; }
};

struct Opt {
    static constexpr std::nullopt_t none = std::nullopt;
    using NoneId = std::nullopt_t;
};

/// holds an optional HirId or HirIdIdx type
template <hir::IsId T> class OptId {
    T underlying{};

  public:
    using none_type = Opt::NoneId;
    static constexpr none_type none = Opt::none;
    using id_tag = T;
    OptId() = default;
    OptId(T id_value) : underlying(id_value) {}
    // allow conversion from none_type
    constexpr OptId(none_type none) : OptId{} { const auto _ = none; }
    HirId raw() const { return underlying.raw(); }
    constexpr T as_id() const noexcept {
        assert(underlying.raw() != HIR_ID_NONE);
        return underlying;
    }
    [[nodiscard]] bool has_value() const noexcept { return underlying.raw() != HIR_ID_NONE; }
    [[nodiscard]] bool empty() const noexcept { return underlying.raw() == HIR_ID_NONE; }
    void set(T id_value) noexcept { this->underlying = id_value; }
    T get_or(T or_else_this_value) {
        return this->has_value() ? this->as_id() : or_else_this_value;
    };
};

template <hir::IsId T> class IdSlice {
    using points_to_type = T;
    IdIdx<T> first_{};
    HirSize len_{};

  public:
    constexpr IdSlice() : first_(IdIdx<T>{}), len_{0} {}
    constexpr IdSlice(IdIdx<T> first, HirSize len) : first_(first), len_(len) {}
    [[nodiscard]] constexpr IdIdx<T> first() const noexcept { return first_; }
    [[nodiscard]] constexpr IdIdx<T> last_elem() const noexcept {
        return IdIdx<T>{first_.raw() + len_ - 1};
    }
    [[nodiscard]] constexpr IdIdx<T> get(HirSize i) const noexcept {
        return IdIdx<T>{first_.raw() + i};
    }
    [[nodiscard]] constexpr HirSize len() const noexcept { return len_; }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return len_ == 0; }

    [[nodiscard]] constexpr IdSlice sub_slice_n(HirSize count) const noexcept {
        return IdSlice{first_, count};
    }
    [[nodiscard]] constexpr IdSlice sub_slice(HirSize begin_index,
                                              HirSize end_index) const noexcept {
        return IdSlice{first_ + begin_index, end_index};
    }

    [[nodiscard]] constexpr IdIdx<T> begin() const noexcept { return first_; }

    [[nodiscard]] constexpr IdIdx<T> end() const noexcept { return IdIdx<T>{first_.raw() + len_}; }
};

using OrderedDefSliceId = Id<IdSlice<DefId>>;

// id points to first TypeId mention
using CanonicalTypeId = Id<TypeId>;

using GenericArgIdSliceId = Id<IdSlice<GenericArgId>>;

/// for canonicalizing slices of generic args
using CanonicalGenericArgsId = Id<IdSlice<GenericArgId>>;

/// for indexing maps of GenericArgIds to DefIds
using CanonicalGenericArgsIdMapId = Id<IdSlice<CanonicalGenericArgsId>>;

using CanonicalComptArgsIdMapId = Id<IdSlice<CanonicalGenericArgsId>>;

} // namespace hir

#endif
