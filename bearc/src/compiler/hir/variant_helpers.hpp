//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_HIR_VARIANT_HELPERS_HPP
#define COMPILER_HIR_VARIANT_HELPERS_HPP

#include <optional>
#include <variant>
// visit helper
template <class... Ts> struct Ovld : Ts... {
    using Ts::operator()...;
};
template <class... Ts> Ovld(Ts...) -> Ovld<Ts...>;

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

// helper for structs with a subvalue that is a variant (see hir::Exec, hir::Def, etc. for examples)
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
        return std::visit(visitor, self().value);
    }

    // try to get a as variant value type if holding T, else get empty
    template <typename T> std::optional<T> try_as() const noexcept {
        return std::holds_alternative<T>(self().value) ? std::optional<T>{std::get<T>(self().value)}
                                                       : std::nullopt;
    }

    friend V;
};
#endif
