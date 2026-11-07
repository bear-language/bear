//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_HIR_VARIANT_HELPERS_HPP
#define COMPILER_HIR_VARIANT_HELPERS_HPP

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

/// visit helper
template <class... Ts> struct Ovld : Ts... {
    using Ts::operator()...;
};
template <class... Ts> Ovld(Ts...) -> Ovld<Ts...>;

/// catch-all for Ovld visitors: any alternative w/o a more specific overload returns a
/// default constructed value of type R
/// usage:
///      const auto vs = Ovld{
///          [](const ExecVariable& e) -> OptId<DefId> { return e.def_id; },
///          OvldDefault<OptId<DefId>>{},
///      };
template <std::default_initializable R> struct OvldDefault {
    template <typename T> constexpr R operator()(const T&) const { return R{}; }
};

namespace priv_impl_variant_helper {
template <typename...> inline constexpr bool always_false_v = false;

// these are only ever instantiated to produce a diagnostic. the compiler prints their template
// arguments in the instantiation context, which names the offending alternative directly
template <typename UnhandledAlternative, std::size_t AlternativeIndex>
struct ovld_visitor_is_missing_an_overload_for {
    static_assert(
        always_false_v<UnhandledAlternative>,
        "visitor does not handle every variant alternative: the unhandled alternative is "
        "the first template argument of `ovld_visitor_is_missing_an_overload_for` in this "
        "error's instantiation context (add an overload for it, or `OvldDefault<R>{}` if "
        "a fallback is intended)");
};

template <typename Alternative, typename ReturnsThis, typename ExpectedThis>
struct ovld_visitor_has_mismatched_return_type_for {
    static_assert(always_false_v<Alternative>,
                  "visitor overloads must all return the same type: in this error's instantiation "
                  "context, the overload for the first template argument of "
                  "`ovld_visitor_has_mismatched_return_type_for` returns the second, but the "
                  "visitor's other overloads return the third");
};

template <typename Variant>
inline constexpr std::size_t alt_count_v = std::variant_size_v<std::remove_cvref_t<Variant>>;

// the argument `std::visit` passes for alternative I as to keep the variant's constness/value
// category
template <typename Variant, std::size_t I>
using alt_arg_t = decltype(std::get<I>(std::declval<Variant>()));

template <typename Visitor, typename Variant, std::size_t I>
inline constexpr bool handles_alt_v = std::is_invocable_v<Visitor, alt_arg_t<Variant, I>>;

// index of the first alternative the visitor handles, or alt_count_v if it handles none
template <typename Visitor, typename Variant, std::size_t... Is>
consteval std::size_t first_handled_alt(std::index_sequence<Is...>) {
    std::size_t first = sizeof...(Is);
    ((first == sizeof...(Is) && handles_alt_v<Visitor, Variant, Is> ? void(first = Is) : void()),
     ...);
    return first;
}

template <typename Visitor, typename Variant, std::size_t I> struct alt_result {
    using type = std::invoke_result_t<Visitor, alt_arg_t<Variant, I>>;
};

// the visit's result type from the first handled alternative (void if none are handled)
// - note: std::conditional_t only names alt_result<..., count>, it never instantiates it
template <typename Visitor, typename Variant,
          std::size_t First
          = first_handled_alt<Visitor, Variant>(std::make_index_sequence<alt_count_v<Variant>>{})>
using visit_result_t
    = std::conditional_t<(First < alt_count_v<Variant>), alt_result<Visitor, Variant, First>,
                         std::type_identity<void>>::type;

// pure checks: these must never instantiate a diagnostic struct, since that would make them
// non-constant
template <typename Visitor, typename Variant, std::size_t I> consteval bool alt_ok() {
    if constexpr (!handles_alt_v<Visitor, Variant, I>) {
        return false;
    } else {
        return std::same_as<typename alt_result<Visitor, Variant, I>::type,
                            visit_result_t<Visitor, Variant>>;
    }
}

template <typename Visitor, typename Variant, std::size_t... Is>
consteval bool visitor_ok(std::index_sequence<Is...>) {
    return (alt_ok<Visitor, Variant, Is>() && ...);
}

// only instantiated once the visitor is known to be bad: one diagnostic per bad alternative
template <typename Visitor, typename Variant, std::size_t I> constexpr void report_alt() {
    using Alt = std::remove_cvref_t<alt_arg_t<Variant, I>>;
    if constexpr (!handles_alt_v<Visitor, Variant, I>) {
        (void)sizeof(ovld_visitor_is_missing_an_overload_for<Alt, I>);
    } else if constexpr (!alt_ok<Visitor, Variant, I>()) {
        (void)sizeof(ovld_visitor_has_mismatched_return_type_for<
                     Alt, typename alt_result<Visitor, Variant, I>::type,
                     visit_result_t<Visitor, Variant>>);
    }
}

template <typename Visitor, typename Variant, std::size_t... Is>
constexpr void report_visitor(std::index_sequence<Is...>) {
    (report_alt<Visitor, Variant, Is>(), ...);
}
} // namespace priv_impl_variant_helper

/// `std::visit`, but with readable compile errors when `visitor` doesn't handle every alternative
/// of `variant` (one error per unhandled alternative, naming it) or its overloads disagree on
/// return type. NodeWithVariantValue::visit goes through this
template <typename Visitor, typename Variant>
constexpr priv_impl_variant_helper::visit_result_t<Visitor&&, Variant&&>
visit_exhaustive(Visitor&& visitor, Variant&& variant) {
    namespace impl = priv_impl_variant_helper;
    constexpr auto alts = std::make_index_sequence<impl::alt_count_v<Variant>>{};
    if constexpr (impl::visitor_ok<Visitor&&, Variant&&>(alts)) {
        return std::visit(std::forward<Visitor>(visitor), std::forward<Variant>(variant));
    } else {
        // std::visit is never instantiated here, so its own wall of errors doesn't bury these
        impl::report_visitor<Visitor&&, Variant&&>(alts);
        std::unreachable();
    }
}

namespace priv_impl_variant_helper {
template <typename Variant, std::size_t... Is>
bool variant_equal_impl(const Variant& a, const Variant& b, std::index_sequence<Is...>) {
    using Fn = bool (*)(const Variant&, const Variant&);

    static constexpr Fn table[] = {+[](const Variant& x, const Variant& y) -> bool {
        using T = std::variant_alternative_t<Is, Variant>;
        return std::get<T>(x) == std::get<T>(y);
    }...};

    return table[a.index()](a, b);
}
} // namespace priv_impl_variant_helper
template <typename... Ts>
bool variant_equal(const std::variant<Ts...>& a, const std::variant<Ts...>& b) {
    if (a.index() != b.index()) {
        return false;
    }

    return priv_impl_variant_helper::variant_equal_impl(a, b, std::index_sequence_for<Ts...>{});
}

/// helper for structs with a subvalue that is a variant (see hir::Exec, hir::Def, etc. for
/// examples)
template <typename V> struct NodeWithVariantValue {
  private:
    NodeWithVariantValue() = default;
    // not called as of now and will probably fail on call due to not yet instantiated template
    // param
    template <typename T> static consteval void check_requirements() {
        static_assert(
            requires(T v) {
                typename T::value_type;
                { v.value } -> std::same_as<typename T::value_type&>;
            }, "V must contain `using value_type` and `value`");
    }
    const V& self() const { return static_cast<const V&>(*this); }
    V& self() { return static_cast<V&>(*this); }

  public:
    template <typename T> bool holds() const noexcept {
        return std::holds_alternative<T>(self().value);
    }
    template <typename... Ts> bool holds_any_of() const noexcept {
        return (std::holds_alternative<Ts>(self().value) || ...);
    }
    template <typename T> T& as() noexcept { return std::get<T>(self().value); }
    template <typename T> const T& as() const noexcept { return std::get<T>(self().value); }
    bool variant_value_equals(const V& other) const noexcept {
        return variant_equal(self().value, other.value);
    }
    template <typename T> bool holds_same(const V& other) const noexcept {
        return holds<T>() && other.template holds<T>();
    }
    bool holds_same_variant_type(const V& other) const noexcept {
        return self().value.index() == other.value.index();
    }
    template <typename... Ts> bool hold_same_any_of(const V& other) const noexcept {
        return holds_any_of<Ts...>() && other.template holds_any_of<Ts...>();
    }
    template <typename T> constexpr decltype(auto) visit(T&& visitor) const {
        return visit_exhaustive(visitor, self().value);
    }

    /// try to get a as variant value type if holding T, else get empty
    template <typename T> std::optional<T> try_as() const noexcept {
        return std::holds_alternative<T>(self().value) ? std::optional<T>{std::get<T>(self().value)}
                                                       : std::nullopt;
    }

    friend V;
};
#endif
