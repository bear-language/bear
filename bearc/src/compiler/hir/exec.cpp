//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/hir/exec.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stddef.h>
#include <type_traits>
#include <utility>
namespace hir {

template <typename T> static bool integral_fits_in(T val, builtin_type type) {
    switch (type) {
    case builtin_type::i8:
        return std::in_range<int8_t>(val);
    case builtin_type::u8:
        return std::in_range<uint8_t>(val);
    case builtin_type::i16:
        return std::in_range<int16_t>(val);
    case builtin_type::u16:
        return std::in_range<uint16_t>(val);
    case builtin_type::i32:
        return std::in_range<int32_t>(val);
    case builtin_type::u32:
        return std::in_range<uint32_t>(val);
    case builtin_type::i64:
        return std::in_range<int64_t>(val);
    case builtin_type::u64:
        return std::in_range<uint64_t>(val);
    case builtin_type::charr:
        return std::in_range<int8_t>(val);
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return true;
}

std::optional<ExecConst> ExecConst::try_safe_convert_to(Context& ctx, builtin_type type) const {
    if (is_integral()) {
        const bool fits = std::visit(
            [type](auto val) {
                using T = decltype(val);
                if constexpr (std::is_same_v<T, char>) {
                    // bear chars are signed 8 bit integers (std::in_range rejects char itself)
                    return integral_fits_in(static_cast<int8_t>(val), type);
                } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                    return integral_fits_in(val, type);
                }
                return true;
            },
            value);
        if (!fits) {
            return std::nullopt;
        }
    }
    auto maybe_up = try_up_convert_to(ctx, type);
    return maybe_up.has_value() ? maybe_up : try_down_convert_to(type);
}

std::optional<ExecConst> ExecConst::try_cast_to(Context& ctx, builtin_type type) const {
    // allow string casts by converting to symbol id
    if (type == builtin_type::str) {
        return ExecConst{to_symbol_id(ctx)};
    }
    // bool -> numeric/char is explicit only, so it is not part of the up/down conversions (which
    // back implicit conversions). go thru u8 since 0/1 fits in every numeric type
    if (holds<bool>() && type != builtin_type::boolean) {
        return ExecConst{ConstantValue{static_cast<uint8_t>(as<bool>())}}.try_up_convert_to(ctx,
                                                                                            type);
    }
    auto maybe_up = try_up_convert_to(ctx, type);
    return maybe_up.has_value() ? maybe_up : try_down_convert_to(type);
}

std::optional<ExecConst> ExecConst::try_up_convert_to(Context& ctx, builtin_type type) const {
    using OptConst = std::optional<ExecConst>;
    auto to_optconst
        = +[](ConstantValue constval) -> OptConst { return OptConst{ExecConst{constval}}; };
    auto none = +[] { return OptConst{}; };
    switch (value.index()) {
    case 0:
        // builtin_type::str;
        switch (type) {
        case builtin_type::str:
            return *this;
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::u32:
        case builtin_type::i32:
        case builtin_type::u64:
        case builtin_type::i64:
        case builtin_type::charr:
        case builtin_type::f32:
        case builtin_type::f64:
        case builtin_type::voidd:
        case builtin_type::nullpointer:
        case builtin_type::boolean:
            break;
        }
        break;
    case 1: {
        // builtin_type::i8
        const int8_t val = as<int8_t>();
        switch (type) {
        case builtin_type::u8:
            return to_optconst(ConstantValue{static_cast<uint8_t>(val)});
        case builtin_type::i8:
            return *this;
        case builtin_type::u16:
            return to_optconst(ConstantValue{static_cast<uint16_t>(val)});
        case builtin_type::i16:
            return to_optconst(ConstantValue{static_cast<int16_t>(val)});
        case builtin_type::u32:
            return to_optconst(ConstantValue{static_cast<uint32_t>(val)});
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(val)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(val)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(val)});
        case builtin_type::charr:
            return to_optconst(ConstantValue{static_cast<char>(val)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }
        break;
    }
    case 2: {
        // builtin_type::u8;
        const uint8_t val = as<uint8_t>();
        switch (type) {
        case builtin_type::u8:
            return *this;
        case builtin_type::i8:
            return to_optconst(ConstantValue{static_cast<int8_t>(val)});
        case builtin_type::u16:
            return to_optconst(ConstantValue{static_cast<uint16_t>(val)});
        case builtin_type::i16:
            return to_optconst(ConstantValue{static_cast<int16_t>(val)});
        case builtin_type::u32:
            return to_optconst(ConstantValue{static_cast<uint32_t>(val)});
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(val)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(val)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(val)});
        case builtin_type::charr:
            return to_optconst(ConstantValue{static_cast<char>(val)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }
        break;
    }
    case 3: {
        // builtin_type::i16;
        const int16_t val = as<int16_t>();
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::charr:
            return none();
        case builtin_type::u16:
            return to_optconst(ConstantValue{static_cast<uint16_t>(val)});
        case builtin_type::i16:
            return *this;
        case builtin_type::u32:
            return to_optconst(ConstantValue{static_cast<uint32_t>(val)});
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(val)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(val)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(val)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }

        break;
    }
    case 4: {
        // builtin_type::u16;
        const uint16_t val = as<uint16_t>();
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::charr:
            return none();
        case builtin_type::u16:
            return *this;
        case builtin_type::i16:
            return to_optconst(ConstantValue{static_cast<int16_t>(val)});
        case builtin_type::u32:
            return to_optconst(ConstantValue{static_cast<uint32_t>(val)});
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(val)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(val)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(val)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }

        break;
    }
    case 5: {
        // builtin_type::i32;
        const int32_t val = as<int32_t>();
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::charr:
            return none();
        case builtin_type::u32:
            return to_optconst(ConstantValue{static_cast<uint32_t>(val)});
        case builtin_type::i32:
            return *this;
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(val)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(val)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }

        break;
    }
    case 6: {
        // builtin_type::u32;
        const uint32_t val = as<uint32_t>();
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::charr:
            return none();
        case builtin_type::u32:
            return *this;
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(val)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(val)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(val)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }

        break;
    }
    case 7: {
        // builtin_type::i64;
        const int64_t val = as<int64_t>();
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::u32:
        case builtin_type::i32:
        case builtin_type::charr:
            return none();
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(val)});
        case builtin_type::i64:
            return *this;
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }

        break;
    }
    case 8: {
        // builtin_type::u64;
        const uint64_t val = as<uint64_t>();
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::u32:
        case builtin_type::i32:
        case builtin_type::charr:
            return none();
        case builtin_type::u64:
            return *this;
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(val)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }

        break;
    }
    case 9: {
        // builtin_type::charr;
        const char val = as<char>();
        switch (type) {
        case builtin_type::u8:
            return to_optconst(ConstantValue{static_cast<uint8_t>(val)});
        case builtin_type::i8:
            return to_optconst(ConstantValue{static_cast<int8_t>(val)});
        case builtin_type::u16:
            return to_optconst(ConstantValue{static_cast<uint16_t>(val)});
        case builtin_type::i16:
            return to_optconst(ConstantValue{static_cast<int16_t>(val)});
        case builtin_type::u32:
            return to_optconst(ConstantValue{static_cast<uint32_t>(val)});
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(val)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(val)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(val)});
        case builtin_type::charr:
            return to_optconst(ConstantValue{static_cast<char>(val)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(val)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
        case builtin_type::str:
            return ExecConst{ctx.symbol_id(std::string{val})}; // fine cuz sso
        case builtin_type::voidd:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
        }

        break;
    }
    case 10: {
        // builtin_type::f32;
        const float val = as<float>();
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::u32:
        case builtin_type::i32:
        case builtin_type::u64:
        case builtin_type::i64:
        case builtin_type::charr:
            return none();
        case builtin_type::f32:
            return *this;
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(val)});
            break;
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
            break;
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
            break;
        }

        break;
    }
    case 11: {
        // builtin_type::f64;
        const double val = as<double>();
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::u32:
        case builtin_type::i32:
        case builtin_type::u64:
        case builtin_type::i64:
        case builtin_type::charr:
        case builtin_type::f32:
            return none();
        case builtin_type::f64:
            return *this;
            break;
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
            break;
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(val)});
            break;
        }

        break;
    }
    case 12: {
        // builtin_type::nullpointer;
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::u32:
        case builtin_type::i32:
        case builtin_type::u64:
        case builtin_type::i64:
        case builtin_type::charr:
        case builtin_type::f32:
        case builtin_type::f64:
        case builtin_type::voidd:
        case builtin_type::str:
            return none();
        case builtin_type::nullpointer:
            return *this;
            break;
        case builtin_type::boolean:
            return to_optconst(ConstantValue{false});
            break;
        }

        break;
    }
    case 13: {
        // builtin_type::boolean;
        switch (type) {
        case builtin_type::u8:
        case builtin_type::i8:
        case builtin_type::u16:
        case builtin_type::i16:
        case builtin_type::u32:
        case builtin_type::i32:
        case builtin_type::u64:
        case builtin_type::i64:
        case builtin_type::charr:
        case builtin_type::f32:
        case builtin_type::f64:
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        case builtin_type::boolean:
            return *this;
            break;
        }

        break;
    }
    default:
        assert(false && "unconsidered builtin type");
        break;
    }
    return OptConst{};
}

/// down converts a floating point constant (f32 constants are promoted to double first)
static std::optional<ExecConst> down_convert_float(double v, builtin_type type) {
    using OptConst = std::optional<ExecConst>;
    auto to_optconst = [](ConstantValue cv) -> OptConst { return OptConst{ExecConst{cv}}; };

    // [lo, hi) is half-open so that the 64 bit bounds (2^63, 2^64) are exactly representable
    //  - NaN and +-inf fail the floor check or the range check
    auto whole_in = [v](double lo, double hi) { return std::floor(v) == v && v >= lo && v < hi; };

    switch (type) {
    case builtin_type::i8:
    case builtin_type::charr:
        if (whole_in(INT8_MIN, INT8_MAX + 1.0)) {
            const auto i = static_cast<int8_t>(v);
            return type == builtin_type::i8 ? to_optconst(ConstantValue{i})
                                            : to_optconst(ConstantValue{static_cast<char>(i)});
        }
        return std::nullopt;
    case builtin_type::u8:
        if (whole_in(0, UINT8_MAX + 1.0)) {
            return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
        }
        return std::nullopt;
    case builtin_type::i16:
        if (whole_in(INT16_MIN, INT16_MAX + 1.0)) {
            return to_optconst(ConstantValue{static_cast<int16_t>(v)});
        }
        return std::nullopt;
    case builtin_type::u16:
        if (whole_in(0, UINT16_MAX + 1.0)) {
            return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
        }
        return std::nullopt;
    case builtin_type::i32:
        if (whole_in(INT32_MIN, INT32_MAX + 1.0)) {
            return to_optconst(ConstantValue{static_cast<int32_t>(v)});
        }
        return std::nullopt;
    case builtin_type::u32:
        if (whole_in(0, UINT32_MAX + 1.0)) {
            return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
        }
        return std::nullopt;
    case builtin_type::i64:
        if (whole_in(-0x1p63, 0x1p63)) {
            return to_optconst(ConstantValue{static_cast<int64_t>(v)});
        }
        return std::nullopt;
    case builtin_type::u64:
        if (whole_in(0, 0x1p64)) {
            return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
        }
        return std::nullopt;
    case builtin_type::f32:
        if (std::isfinite(v) && std::fabs(v) > std::numeric_limits<float>::max()) {
            return std::nullopt;
        }
        return to_optconst(ConstantValue{static_cast<float>(v)});
    case builtin_type::f64:
        return to_optconst(ConstantValue{v});
    case builtin_type::boolean:
        return to_optconst(ConstantValue{static_cast<bool>(v)});
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::try_down_convert_to(builtin_type type) const {

    using OptConst = std::optional<ExecConst>;

    auto to_optconst = +[](ConstantValue v) -> OptConst { return OptConst{ExecConst{v}}; };

    auto none = +[] { return OptConst{}; };

    auto fits_signed = [](int64_t v, int64_t lo, int64_t hi) { return v >= lo && v <= hi; };

    auto fits_unsigned = [](uint64_t v, uint64_t hi) { return v <= hi; };

    switch (value.index()) {
        // i8
    case 1: {
        int8_t v = as<int8_t>();

        switch (type) {
        case builtin_type::i8:
            return *this;
        case builtin_type::u8:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
            }
            return none();
        case builtin_type::i16:
            return to_optconst(ConstantValue{static_cast<int16_t>(v)});
        case builtin_type::u16:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
            }
            return none();
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(v)});
        case builtin_type::u32:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
            }
            return none();
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(v)});
        case builtin_type::u64:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
            }
            return none();
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});
        case builtin_type::charr:
            return to_optconst(ConstantValue{static_cast<char>(v)});
        default:
            return none();
        }
    }

    // u8
    case 2: {
        uint8_t v = as<uint8_t>();

        switch (type) {
        case builtin_type::i8:
            if (v <= INT8_MAX) {
                return to_optconst(ConstantValue{static_cast<int8_t>(v)});
            }
            return none();
        case builtin_type::u8:
            return *this;
        case builtin_type::i16:
            return to_optconst(ConstantValue{static_cast<int16_t>(v)});
        case builtin_type::u16:
            return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(v)});
        case builtin_type::u32:
            return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(v)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});
        case builtin_type::charr:
            if (v <= INT8_MAX) {
                return to_optconst(ConstantValue{static_cast<char>(v)});
            }
            return none();
        default:
            return none();
        }
    }

    // i16
    case 3: {
        int16_t v = as<int16_t>();

        switch (type) {
        case builtin_type::i8:
            if (fits_signed(v, INT8_MIN, INT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<int8_t>(v)});
            }
            return none();
        case builtin_type::u8:
            if (fits_signed(v, 0, UINT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
            }
            return none();
        case builtin_type::i16:
            return *this;
        case builtin_type::u16:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
            }
            return none();
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(v)});
        case builtin_type::u32:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
            }
            return none();
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(v)});
        case builtin_type::u64:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
            }
            return none();
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});
        case builtin_type::charr:
            if (fits_signed(v, INT8_MIN, INT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<char>(v)});
            }
            return none();
        default:
            return none();
        }
    }

    // u16
    case 4: {
        uint16_t v = as<uint16_t>();

        switch (type) {
        case builtin_type::i8:
            if (v <= INT8_MAX) {
                return to_optconst(ConstantValue{static_cast<int8_t>(v)});
            }
            return none();
        case builtin_type::u8:
            if (fits_unsigned(v, UINT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
            }
            return none();
        case builtin_type::i16:
            if (v <= INT16_MAX) {
                return to_optconst(ConstantValue{static_cast<int16_t>(v)});
            }
            return none();
        case builtin_type::u16:
            return *this;
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(v)});
        case builtin_type::u32:
            return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(v)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});
        case builtin_type::charr:
            if (v <= INT8_MAX) {
                return to_optconst(ConstantValue{static_cast<char>(v)});
            }
            return none();
        default:
            return none();
        }
    }

    // i32
    case 5: {
        int32_t v = as<int32_t>();

        switch (type) {
        case builtin_type::i8:
            if (fits_signed(v, INT8_MIN, INT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<int8_t>(v)});
            }
            return none();
        case builtin_type::u8:
            if (fits_signed(v, 0, UINT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
            }
            return none();
        case builtin_type::i16:
            if (fits_signed(v, INT16_MIN, INT16_MAX)) {
                return to_optconst(ConstantValue{static_cast<int16_t>(v)});
            }
            return none();
        case builtin_type::u16:
            if (fits_signed(v, 0, UINT16_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
            }
            return none();
        case builtin_type::i32:
            return *this;
        case builtin_type::u32:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
            }
            return none();
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(v)});
        case builtin_type::u64:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
            }
            return none();
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});
        case builtin_type::charr:
            if (fits_signed(v, INT8_MIN, INT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<char>(v)});
            }
            return none();
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        }
    }

    // u32
    case 6: {
        uint32_t v = as<uint32_t>();

        switch (type) {
        case builtin_type::i8:
            if (v <= INT8_MAX) {
                return to_optconst(ConstantValue{static_cast<int8_t>(v)});
            }
            return none();
        case builtin_type::u8:
            if (fits_unsigned(v, UINT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
            }
            return none();
        case builtin_type::i16:
            if (v <= INT16_MAX) {
                return to_optconst(ConstantValue{static_cast<int16_t>(v)});
            }
            return none();
        case builtin_type::u16:
            if (fits_unsigned(v, UINT16_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
            }
            return none();
        case builtin_type::i32:
            if (v <= INT32_MAX) {
                return to_optconst(ConstantValue{static_cast<int32_t>(v)});
            }
            return none();
        case builtin_type::u32:
            return *this;
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(v)});
        case builtin_type::u64:
            return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});
        case builtin_type::charr:
            if (v <= INT8_MAX) {
                return to_optconst(ConstantValue{static_cast<char>(v)});
            }
            return none();
        default:
            return none();
        }
    } // i64
    case 7: {
        int64_t v = as<int64_t>();

        switch (type) {

        case builtin_type::i8:
            if (fits_signed(v, -128, 127)) {
                return to_optconst(ConstantValue{static_cast<int8_t>(v)});
            }
            return none();

        case builtin_type::u8:
            if (fits_signed(v, 0, UINT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
            }
            return none();

        case builtin_type::i16:
            if (fits_signed(v, INT16_MIN, INT16_MAX)) {
                return to_optconst(ConstantValue{static_cast<int16_t>(v)});
            }
            return none();

        case builtin_type::u16:
            if (fits_signed(v, 0, UINT16_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
            }
            return none();

        case builtin_type::i32:
            if (fits_signed(v, INT32_MIN, INT32_MAX)) {
                return to_optconst(ConstantValue{static_cast<int32_t>(v)});
            }
            return none();

        case builtin_type::u32:
            if (fits_signed(v, 0, UINT32_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
            }
            return none();

        case builtin_type::i64:
            return *this;

        case builtin_type::u64:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
            }
            return none();

        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});

        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});

        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});

        case builtin_type::charr:
            if (fits_signed(v, INT8_MIN, INT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<char>(v)});
            }
            return none();

        default:
            return none();
        }
    }

    // u64
    case 8: {
        uint64_t v = as<uint64_t>();

        switch (type) {

        case builtin_type::u8:
            if (fits_unsigned(v, UINT8_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
            }
            return none();

        case builtin_type::i8:
            if (v <= INT8_MAX) {
                return to_optconst(ConstantValue{static_cast<int8_t>(v)});
            }
            return none();

        case builtin_type::u16:
            if (fits_unsigned(v, UINT16_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
            }
            return none();

        case builtin_type::i16:
            if (v <= INT16_MAX) {
                return to_optconst(ConstantValue{static_cast<int16_t>(v)});
            }
            return none();

        case builtin_type::u32:
            if (fits_unsigned(v, UINT32_MAX)) {
                return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
            }
            return none();

        case builtin_type::i32:
            if (v <= INT32_MAX) {
                return to_optconst(ConstantValue{static_cast<int32_t>(v)});
            }
            return none();

        case builtin_type::u64:
            return *this;

        case builtin_type::i64:
            if (v <= INT64_MAX) {
                return to_optconst(ConstantValue{static_cast<int64_t>(v)});
            }
            return none();

        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});

        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});

        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});

        case builtin_type::charr:
            if (v <= INT8_MAX) {
                return to_optconst(ConstantValue{static_cast<char>(v)});
            }
            return none();

        default:
            return none();
        }
    }

    // char (treated as a signed 8 bit integer, see integral_fits_in)
    case 9: {
        const auto v = static_cast<int8_t>(as<char>());

        switch (type) {
        case builtin_type::charr:
            return *this;
        case builtin_type::i8:
            return to_optconst(ConstantValue{v});
        case builtin_type::u8:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint8_t>(v)});
            }
            return none();
        case builtin_type::i16:
            return to_optconst(ConstantValue{static_cast<int16_t>(v)});
        case builtin_type::u16:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint16_t>(v)});
            }
            return none();
        case builtin_type::i32:
            return to_optconst(ConstantValue{static_cast<int32_t>(v)});
        case builtin_type::u32:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
            }
            return none();
        case builtin_type::i64:
            return to_optconst(ConstantValue{static_cast<int64_t>(v)});
        case builtin_type::u64:
            if (v >= 0) {
                return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
            }
            return none();
        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});
        case builtin_type::f64:
            return to_optconst(ConstantValue{static_cast<double>(v)});
        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});
        case builtin_type::voidd:
        case builtin_type::str:
        case builtin_type::nullpointer:
            return none();
        }
        break;
    }

    // f32
    case 10: {
        if (type == builtin_type::f32) {
            return *this;
        }
        return down_convert_float(as<float>(), type);
    }

    // f64
    case 11: {
        if (type == builtin_type::f64) {
            return *this;
        }
        return down_convert_float(as<double>(), type);
    }
    // nullptr
    case 12: {
        if (type == builtin_type::nullpointer) {
            return *this;
        }
        break;
    }
    // bool
    case 13: {
        if (type == builtin_type::boolean) {
            return *this;
        }
        break;
    }

    default:
        break;
    }

    return none();
}

Exec::Exec(Context& ctx, ExecValue value, Span span, bool should_be_compt)
    : value{value}, span{span}, compt{should_be_compt} {
    if (should_be_compt && !can_be_compt()) {
        ctx.emplace_diagnostic(span, diag_code::cannot_resolve_value_at_compt, diag_type::error);
    }
}

bool Exec::is_equivalent(const Context& ctx, ExecId eid1, ExecId eid2) {
    // same ExecId always means equivalent
    if (eid1 == eid2) {
        return true;
    }
    const Exec& e1 = ctx.exec(eid1);
    const Exec& e2 = ctx.exec(eid2);
    const bool both_compt = e1.compt && e2.compt;
    if (!both_compt) {
        return false;
    }
    if (e1.holds_same<ExecConst>(e2)) {
        const ExecConst& lit1 = e1.as<ExecConst>();
        const ExecConst& lit2 = e2.as<ExecConst>();
        return lit1.variant_value_equals(lit2);
    }
    return false;
}

bool Exec::can_be_compt() {
    auto vs = Ovld{
        [](const ExecFnPtr&) -> bool { return true; },
        [](const ExecUnionInit&) -> bool { return true; },
        [](const ExecVariantInit&) -> bool { return true; },
        [](const ExecRange&) -> bool { return true; },
        [](const ExecVariantFieldInit&) -> bool { return true; },
        [](const ExecListLiteral&) -> bool { return true; },
        [](const ExecConst&) -> bool { return true; },
        [](const ExecStructInit&) -> bool { return true; },
        [](const ExecBlock&) -> bool { return false; },
        [](const ExecJump&) -> bool { return false; },
        [](const ExecBranch&) -> bool { return false; },
        [](const ExecReturn&) -> bool { return false; },
        [](const ExecYield&) -> bool { return false; },
        // exprs
        [](const ExecVariable&) -> bool { return false; },
        [](const ExecAssignment&) -> bool { return false; },
        [](const ExecMemberAccess&) -> bool { return false; },
        [](const ExecBinary&) -> bool { return false; },
        [](const ExecCast&) -> bool { return false; },
        [](const ExecSubscript&) -> bool { return false; },
        [](const ExecFnCall&) -> bool { return false; },
        [](const ExecBorrow&) -> bool { return false; },
        [](const ExecDeref&) -> bool { return false; },
        [](const ExecAddrOf&) -> bool { return false; },
        [](const ExecMatch&) -> bool { return false; },
        [](const ExecMatchBranch&) -> bool { return false; },
        [](const ExecCompoundAssignment&) -> bool { return false; },
        [](const ExecUnary&) -> bool { return false; },
        [](const ExecBuiltinAccess&) -> bool { return false; },
    };
    return visit(vs);
}

SymbolId ExecConst::to_symbol_id(Context& ctx) const {
    std::string str{};
    str.reserve(64); // pretty beeg
    switch (value.index()) {
        // str
    case 0:
        return as<SymbolId>();
    // i8
    case 1: {
        str += std::to_string(static_cast<int>(as<int8_t>()));
        break;
    }
        // u8
    case 2: {
        str += std::to_string(static_cast<int>(as<uint8_t>()));
        break;
    }
        // i16
    case 3: {
        str += std::to_string(static_cast<int>(as<int16_t>()));
        break;
    }
    // u16
    case 4: {
        str += std::to_string(static_cast<unsigned>(as<uint16_t>()));
        break;
    }
    // i32
    case 5: {
        str += std::to_string(as<int32_t>());
        break;
    }
    // u32
    case 6: {
        str += std::to_string(as<uint32_t>());
        break;
    }
    // i64
    case 7: {
        str += std::to_string(as<int64_t>());
        break;
    }
    // u64
    case 8: {
        str += std::to_string(as<uint64_t>());
        break;
    }
    // char
    case 9: {
        str += as<char>();
        break;
    }
    // f32
    case 10: {
        str += std::to_string(as<float>());
        break;
    }
    // f64
    case 11: {
        str += std::to_string(as<double>());
        break;
    }
    // nullptr
    case 12: {
        str += "null";
        break;
    }
    // bool
    case 13: {
        str += as<bool>() ? "true" : "false";
        break;
    }
    default:
        assert(false && "unconsidered constant type");
        return {};
    }
    return ctx.symbol_id(str);
}

std::string ExecConst::to_string() const {
    switch (type_builtin()) {
    case builtin_type::u8:
        return std::to_string(static_cast<int>(as<uint8_t>()));
    case builtin_type::i8:
        return std::to_string(static_cast<int>(as<int8_t>()));
    case builtin_type::u16:
        return std::to_string(static_cast<int>(as<uint16_t>()));
    case builtin_type::i16:
        return std::to_string(static_cast<int>(as<int16_t>()));
    case builtin_type::u32:
        return std::to_string(as<uint32_t>());
    case builtin_type::i32:
        return std::to_string(as<int32_t>());
    case builtin_type::u64:
        return std::to_string(as<uint64_t>());
    case builtin_type::i64:
        return std::to_string(as<int64_t>());
    case builtin_type::charr:
        return std::to_string(as<char>());
    case builtin_type::f32:
        return std::to_string(as<float>());
    case builtin_type::f64:
        return std::to_string(as<double>());
    case builtin_type::voidd:
        return "void";
    case builtin_type::str:
        return "<string>";
    case builtin_type::nullpointer:
        return "null";
    case builtin_type::boolean:
        return as<bool>() ? "true" : "false";
    }
    std::unreachable();
    return "";
}

std::string ExecConst::to_string(const Context& ctx) const {
    switch (type_builtin()) {
    case builtin_type::u8:
        return std::to_string(static_cast<int>(as<uint8_t>()));
    case builtin_type::i8:
        return std::to_string(static_cast<int>(as<int8_t>()));
    case builtin_type::u16:
        return std::to_string(static_cast<int>(as<uint16_t>()));
    case builtin_type::i16:
        return std::to_string(static_cast<int>(as<int16_t>()));
    case builtin_type::u32:
        return std::to_string(as<uint32_t>());
    case builtin_type::i32:
        return std::to_string(as<int32_t>());
    case builtin_type::u64:
        return std::to_string(as<uint64_t>());
    case builtin_type::i64:
        return std::to_string(as<int64_t>());
    case builtin_type::charr:
        return std::to_string(as<char>());
    case builtin_type::f32:
        return std::to_string(as<float>());
    case builtin_type::f64:
        return std::to_string(as<double>());
    case builtin_type::voidd:
        return "void";
    case builtin_type::str:
        return ctx.symbol_id_to_cstr(as<SymbolId>());
    case builtin_type::nullpointer:
        return "null";
    case builtin_type::boolean:
        return as<bool>() ? "true" : "false";
    }
    std::unreachable();
    return "";
}

using EConst = ExecConst;

[[nodiscard]] bool ExecConst::has_binary_op(binary_op op) const {
    return builtin_type_has_binary_op(type_builtin(), op);
}
[[nodiscard]] bool ExecConst::has_unary_op(unary_op op) const {
    return builtin_type_has_unary_op(type_builtin(), op);
}

// integer ops use the gcc/clang overflow builtins so that compt evaluation never hits UB; they
// always store the result wrapped (mod 2^N), so unsigned overflow is well-defined (wraps), while
// signed overflow emits a diagnostic and returns std::nullopt

static std::nullopt_t e_diag_overflow(Context& ctx, Span span, ExecConst r) {
    ctx.emplace_diagnostic_with_message_value(
        span, diag_code::integer_overflow_at_compt_is_illegal_for_signed_type, diag_type::error,
        DiagnosticSymbolAfterMessage{.sid = ctx.symbol_id(builtin_type_to_cstr(r.type_builtin()))});
    return std::nullopt;
}

template <typename T> std::optional<EConst> e_plus(Context& ctx, Span span, EConst l, EConst r) {
    if constexpr (std::is_integral_v<T>) {
        T res;
        if (__builtin_add_overflow(l.as<T>(), r.as<T>(), &res) && std::is_signed_v<T>) {
            return e_diag_overflow(ctx, span, r);
        }
        return EConst{res};
    } else {
        return EConst{static_cast<T>(l.as<T>() + r.as<T>())};
    }
}

template <typename T> std::optional<EConst> e_minus(Context& ctx, Span span, EConst l, EConst r) {
    if constexpr (std::is_integral_v<T>) {
        T res;
        if (__builtin_sub_overflow(l.as<T>(), r.as<T>(), &res) && std::is_signed_v<T>) {
            return e_diag_overflow(ctx, span, r);
        }
        return EConst{res};
    } else {
        return EConst{static_cast<T>(l.as<T>() - r.as<T>())};
    }
}

template <typename T>
std::optional<EConst> e_multiply(Context& ctx, Span span, EConst l, EConst r) {
    if constexpr (std::is_integral_v<T>) {
        T res;
        if (__builtin_mul_overflow(l.as<T>(), r.as<T>(), &res) && std::is_signed_v<T>) {
            return e_diag_overflow(ctx, span, r);
        }
        return EConst{res};
    } else {
        return EConst{static_cast<T>(l.as<T>() * r.as<T>())};
    }
}

// MIN / -1 and MIN % -1 (signed only); div by zero is guarded by the caller
template <typename T> bool e_is_min_by_neg_one(EConst l, EConst r) {
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        return l.as<T>() == std::numeric_limits<T>::min() && r.as<T>() == -1;
    }
    return false;
}

template <typename T> std::optional<EConst> e_divide(Context& ctx, Span span, EConst l, EConst r) {
    if (e_is_min_by_neg_one<T>(l, r)) {
        return e_diag_overflow(ctx, span, r);
    }
    return EConst{static_cast<T>(l.as<T>() / r.as<T>())};
}

template <typename T> EConst e_mod(EConst l, EConst r) {
    if (e_is_min_by_neg_one<T>(l, r)) {
        return EConst{T{0}}; // mathematically 0, but UB in C++
    }
    return EConst{static_cast<T>(l.as<T>() % r.as<T>())};
}

template <typename T> EConst e_bit_or(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() | r.as<T>())};
}

template <typename T> EConst e_bit_and(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() & r.as<T>())};
}

template <typename T> EConst e_bit_xor(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() ^ r.as<T>())};
}

// shifting by a negative amount or by >= the bit width of the type is UB
template <typename T> bool e_guard_shift_amount(Context& ctx, Span span, EConst r) {
    const T amount = r.as<T>();
    if (std::cmp_less(amount, 0) || std::cmp_greater_equal(amount, sizeof(T) * 8)) {
        ctx.emplace_diagnostic_with_message_value(
            span, diag_code::shift_amount_out_of_range_at_compt_is_illegal_for_type,
            diag_type::error,
            DiagnosticSymbolAfterMessage{.sid
                                         = ctx.symbol_id(builtin_type_to_cstr(r.type_builtin()))});
        return true;
    }
    return false;
}

template <typename T> std::optional<EConst> e_lsh(Context& ctx, Span span, EConst l, EConst r) {
    if (e_guard_shift_amount<T>(ctx, span, r)) {
        return std::nullopt;
    }
    return EConst{static_cast<T>(l.as<T>() << r.as<T>())};
}

template <typename T> std::optional<EConst> e_rshl(Context& ctx, Span span, EConst l, EConst r) {
    if (e_guard_shift_amount<T>(ctx, span, r)) {
        return std::nullopt;
    }
    return EConst{static_cast<T>(l.as<T>() >> r.as<T>())};
}

template <typename T> std::optional<EConst> e_rsha(Context& ctx, Span span, EConst l, EConst r) {
    if (e_guard_shift_amount<T>(ctx, span, r)) {
        return std::nullopt;
    }
    return EConst{static_cast<T>(l.as<T>() >> r.as<T>())};
}

template <typename T> EConst e_greater_than(EConst l, EConst r) {
    return EConst{static_cast<bool>(l.as<T>() > r.as<T>())};
}
template <typename T> EConst e_less_than(EConst l, EConst r) {
    return EConst{static_cast<bool>(l.as<T>() < r.as<T>())};
}
template <typename T> EConst e_greater_than_or_equal(EConst l, EConst r) {
    return EConst{static_cast<bool>(l.as<T>() >= r.as<T>())};
}
template <typename T> EConst e_less_than_or_equal(EConst l, EConst r) {
    return EConst{static_cast<bool>(l.as<T>() <= r.as<T>())};
}
template <typename T> EConst e_equal(EConst l, EConst r) {
    return EConst{static_cast<bool>(l.as<T>() == r.as<T>())};
}
template <typename T> EConst e_not_equal(EConst l, EConst r) {
    return EConst{static_cast<bool>(l.as<T>() != r.as<T>())};
}
template <typename T> EConst e_and(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() && r.as<T>())};
}
template <typename T> EConst e_or(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() || r.as<T>())};
}

template <typename T> EConst e_preun_plus(ExecConst e) {
    return EConst{static_cast<T>(+e.as<T>())};
}

template <typename T> std::optional<EConst> e_preun_minus(Context& ctx, Span span, ExecConst e) {
    if constexpr (std::is_integral_v<T>) {
        T res;
        if (__builtin_sub_overflow(T{0}, e.as<T>(), &res) && std::is_signed_v<T>) {
            return e_diag_overflow(ctx, span, e);
        }
        return EConst{res};
    } else {
        return EConst{static_cast<T>(-e.as<T>())};
    }
}

template <typename T> EConst e_bit_not(ExecConst e) { return EConst{static_cast<T>(~e.as<T>())}; }

template <typename T> EConst e_bool_not(ExecConst e) { return EConst{static_cast<T>(!e.as<T>())}; }

std::optional<ExecConst> ExecConst::plus(Context& ctx, Span span, ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    // std::cout << lhs.to_string() << " + " << rhs.to_string() << '\n'; // debug
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_plus<u8>(ctx, span, lhs, rhs);
    case builtin_type::i8:
        return e_plus<i8>(ctx, span, lhs, rhs);
    case builtin_type::u16:
        return e_plus<u16>(ctx, span, lhs, rhs);
    case builtin_type::i16:
        return e_plus<i16>(ctx, span, lhs, rhs);
    case builtin_type::u32:
        return e_plus<u32>(ctx, span, lhs, rhs);
    case builtin_type::i32:
        return e_plus<i32>(ctx, span, lhs, rhs);
    case builtin_type::u64:
        return e_plus<u64>(ctx, span, lhs, rhs);
    case builtin_type::i64:
        return e_plus<i64>(ctx, span, lhs, rhs);
    case builtin_type::f32:
        return e_plus<f32>(ctx, span, lhs, rhs);
    case builtin_type::f64:
        return e_plus<f64>(ctx, span, lhs, rhs);
    case builtin_type::str:
        return ExecConst{ctx.concat_symbols(lhs.as<SymbolId>(), rhs.as<SymbolId>())};
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::minus(Context& ctx, Span span, ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_minus<u8>(ctx, span, lhs, rhs);
    case builtin_type::i8:
        return e_minus<i8>(ctx, span, lhs, rhs);
    case builtin_type::u16:
        return e_minus<u16>(ctx, span, lhs, rhs);
    case builtin_type::i16:
        return e_minus<i16>(ctx, span, lhs, rhs);
    case builtin_type::u32:
        return e_minus<u32>(ctx, span, lhs, rhs);
    case builtin_type::i32:
        return e_minus<i32>(ctx, span, lhs, rhs);
    case builtin_type::u64:
        return e_minus<u64>(ctx, span, lhs, rhs);
    case builtin_type::i64:
        return e_minus<i64>(ctx, span, lhs, rhs);
    case builtin_type::f32:
        return e_minus<f32>(ctx, span, lhs, rhs);
    case builtin_type::f64:
        return e_minus<f64>(ctx, span, lhs, rhs);
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::multiply(Context& ctx, Span span, ExecConst lhs,
                                             ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_multiply<u8>(ctx, span, lhs, rhs);
    case builtin_type::i8:
        return e_multiply<i8>(ctx, span, lhs, rhs);
    case builtin_type::u16:
        return e_multiply<u16>(ctx, span, lhs, rhs);
    case builtin_type::i16:
        return e_multiply<i16>(ctx, span, lhs, rhs);
    case builtin_type::u32:
        return e_multiply<u32>(ctx, span, lhs, rhs);
    case builtin_type::i32:
        return e_multiply<i32>(ctx, span, lhs, rhs);
    case builtin_type::u64:
        return e_multiply<u64>(ctx, span, lhs, rhs);
    case builtin_type::i64:
        return e_multiply<i64>(ctx, span, lhs, rhs);
    case builtin_type::f32:
        return e_multiply<f32>(ctx, span, lhs, rhs);
    case builtin_type::f64:
        return e_multiply<f64>(ctx, span, lhs, rhs);
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::divide(Context& ctx, Span span, ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_divide<u8>(ctx, span, lhs, rhs);
    case builtin_type::i8:
        return e_divide<i8>(ctx, span, lhs, rhs);
    case builtin_type::u16:
        return e_divide<u16>(ctx, span, lhs, rhs);
    case builtin_type::i16:
        return e_divide<i16>(ctx, span, lhs, rhs);
    case builtin_type::u32:
        return e_divide<u32>(ctx, span, lhs, rhs);
    case builtin_type::i32:
        return e_divide<i32>(ctx, span, lhs, rhs);
    case builtin_type::u64:
        return e_divide<u64>(ctx, span, lhs, rhs);
    case builtin_type::i64:
        return e_divide<i64>(ctx, span, lhs, rhs);
    case builtin_type::f32:
        return e_divide<f32>(ctx, span, lhs, rhs);
    case builtin_type::f64:
        return e_divide<f64>(ctx, span, lhs, rhs);
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::mod(ExecConst lhs, ExecConst rhs) {

    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_mod<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_mod<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_mod<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_mod<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_mod<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_mod<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_mod<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_mod<i64>(lhs, rhs);
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

bool ExecConst::equals_zero() const {
    switch (type_builtin()) {
    case builtin_type::u8:
        return as<u8>() == 0;
    case builtin_type::i8:
        return as<i8>() == 0;
    case builtin_type::u16:
        return as<u16>() == 0;
    case builtin_type::i16:
        return as<i16>() == 0;
    case builtin_type::u32:
        return as<u32>() == 0;
    case builtin_type::i32:
        return as<i32>() == 0;
    case builtin_type::u64:
        return as<u64>() == 0;
    case builtin_type::i64:
        return as<i64>() == 0;
    case builtin_type::charr:
        return as<char>() == '\0';
    case builtin_type::f32:
        return as<f32>() == 0.0f;
    case builtin_type::f64:
        return as<f64>() == 0.0f;
    case builtin_type::voidd:
    case builtin_type::str:
        return false;
    case builtin_type::nullpointer:
        return true;
    case builtin_type::boolean:
        return !as<bool>();
    }
    return false;
}

std::optional<ExecConst> ExecConst::bit_and(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_bit_and<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_bit_and<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_bit_and<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_bit_and<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_bit_and<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_bit_and<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_bit_and<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_bit_and<i64>(lhs, rhs);
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::bit_or(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_bit_or<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_bit_or<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_bit_or<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_bit_or<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_bit_or<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_bit_or<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_bit_or<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_bit_or<i64>(lhs, rhs);
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::bit_xor(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_bit_xor<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_bit_xor<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_bit_xor<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_bit_xor<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_bit_xor<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_bit_xor<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_bit_xor<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_bit_xor<i64>(lhs, rhs);
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::bit_lsh(Context& ctx, Span span, ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_lsh<u8>(ctx, span, lhs, rhs);
    case builtin_type::i8:
        return e_lsh<i8>(ctx, span, lhs, rhs);
    case builtin_type::u16:
        return e_lsh<u16>(ctx, span, lhs, rhs);
    case builtin_type::i16:
        return e_lsh<i16>(ctx, span, lhs, rhs);
    case builtin_type::u32:
        return e_lsh<u32>(ctx, span, lhs, rhs);
    case builtin_type::i32:
        return e_lsh<i32>(ctx, span, lhs, rhs);
    case builtin_type::u64:
        return e_lsh<u64>(ctx, span, lhs, rhs);
    case builtin_type::i64:
        return e_lsh<i64>(ctx, span, lhs, rhs);
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::bit_rsha(Context& ctx, Span span, ExecConst lhs,
                                             ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::i8:
        return e_rsha<i8>(ctx, span, lhs, rhs);
    case builtin_type::i16:
        return e_rsha<i16>(ctx, span, lhs, rhs);
    case builtin_type::i32:
        return e_rsha<i32>(ctx, span, lhs, rhs);
    case builtin_type::i64:
        return e_rsha<i64>(ctx, span, lhs, rhs);
    case builtin_type::u8:
    case builtin_type::u16:
    case builtin_type::u32:
    case builtin_type::u64:
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::bit_rshl(Context& ctx, Span span, ExecConst lhs,
                                             ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_rshl<u8>(ctx, span, lhs, rhs);
    case builtin_type::u16:
        return e_rshl<u16>(ctx, span, lhs, rhs);
    case builtin_type::u32:
        return e_rshl<u32>(ctx, span, lhs, rhs);
    case builtin_type::u64:
        return e_rshl<u64>(ctx, span, lhs, rhs);
    case builtin_type::i8:
    case builtin_type::i16:
    case builtin_type::i32:
    case builtin_type::i64:
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::greater_than(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_greater_than<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_greater_than<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_greater_than<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_greater_than<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_greater_than<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_greater_than<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_greater_than<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_greater_than<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_greater_than<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_greater_than<f64>(lhs, rhs);
    case builtin_type::charr:
        return e_greater_than<char>(lhs, rhs);
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::less_than(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_less_than<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_less_than<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_less_than<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_less_than<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_less_than<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_less_than<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_less_than<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_less_than<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_less_than<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_less_than<f64>(lhs, rhs);
    case builtin_type::charr:
        return e_less_than<char>(lhs, rhs);
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::greater_than_or_equal(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_greater_than_or_equal<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_greater_than_or_equal<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_greater_than_or_equal<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_greater_than_or_equal<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_greater_than_or_equal<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_greater_than_or_equal<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_greater_than_or_equal<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_greater_than_or_equal<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_greater_than_or_equal<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_greater_than_or_equal<f64>(lhs, rhs);
    case builtin_type::charr:
        return e_greater_than_or_equal<char>(lhs, rhs);
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::less_than_or_equal(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_less_than_or_equal<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_less_than_or_equal<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_less_than_or_equal<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_less_than_or_equal<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_less_than_or_equal<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_less_than_or_equal<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_less_than_or_equal<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_less_than_or_equal<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_less_than_or_equal<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_less_than_or_equal<f64>(lhs, rhs);
    case builtin_type::charr:
        return e_less_than_or_equal<char>(lhs, rhs);
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::equal(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_equal<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_equal<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_equal<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_equal<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_equal<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_equal<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_equal<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_equal<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_equal<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_equal<f64>(lhs, rhs);
    case builtin_type::charr:
        return e_equal<char>(lhs, rhs);
    case builtin_type::str:
        return e_equal<SymbolId>(lhs, rhs);
    case builtin_type::nullpointer:
        return std::optional<ExecConst>{true}; // both nullpointer, so they are equal => true
    case builtin_type::boolean:
        return e_equal<bool>(lhs, rhs);
        break;
    case builtin_type::voidd:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::not_equal(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_not_equal<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_not_equal<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_not_equal<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_not_equal<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_not_equal<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_not_equal<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_not_equal<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_not_equal<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_not_equal<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_not_equal<f64>(lhs, rhs);
    case builtin_type::charr:
        return e_not_equal<char>(lhs, rhs);
    case builtin_type::str:
        return e_not_equal<SymbolId>(lhs, rhs);
    case builtin_type::nullpointer:
        return std::optional<ExecConst>{true}; // noth nullpointer, so true
    case builtin_type::boolean:
        return e_not_equal<bool>(lhs, rhs);
    case builtin_type::voidd:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::bool_and(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::boolean:
        return e_and<bool>(lhs, rhs);
    case builtin_type::u8:
    case builtin_type::u16:
    case builtin_type::u32:
    case builtin_type::u64:
    case builtin_type::i8:
    case builtin_type::i16:
    case builtin_type::i32:
    case builtin_type::i64:
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::bool_or(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::boolean:
        return e_or<bool>(lhs, rhs);
    case builtin_type::u8:
    case builtin_type::u16:
    case builtin_type::u32:
    case builtin_type::u64:
    case builtin_type::i8:
    case builtin_type::i16:
    case builtin_type::i32:
    case builtin_type::i64:
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::preunary_plus(ExecConst ec) {
    assert(ec.holds_same_variant_type(ec));
    switch (ec.type_builtin()) {
    case builtin_type::u8:
        return e_preun_plus<u8>(ec);
    case builtin_type::i8:
        return e_preun_plus<i8>(ec);
    case builtin_type::u16:
        return e_preun_plus<u16>(ec);
    case builtin_type::i16:
        return e_preun_plus<i16>(ec);
    case builtin_type::u32:
        return e_preun_plus<u32>(ec);
    case builtin_type::i32:
        return e_preun_plus<i32>(ec);
    case builtin_type::u64:
        return e_preun_plus<u64>(ec);
    case builtin_type::i64:
        return e_preun_plus<i64>(ec);
    case builtin_type::f32:
        return e_preun_plus<f32>(ec);
    case builtin_type::f64:
        return e_preun_plus<f64>(ec);
    case builtin_type::charr:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
    case builtin_type::voidd:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::preunary_minus(Context& ctx, Span span, ExecConst ec) {
    assert(ec.holds_same_variant_type(ec));
    switch (ec.type_builtin()) {
    case builtin_type::u8:
        return e_preun_minus<u8>(ctx, span, ec);
    case builtin_type::i8:
        return e_preun_minus<i8>(ctx, span, ec);
    case builtin_type::u16:
        return e_preun_minus<u16>(ctx, span, ec);
    case builtin_type::i16:
        return e_preun_minus<i16>(ctx, span, ec);
    case builtin_type::u32:
        return e_preun_minus<u32>(ctx, span, ec);
    case builtin_type::i32:
        return e_preun_minus<i32>(ctx, span, ec);
    case builtin_type::u64:
        return e_preun_minus<u64>(ctx, span, ec);
    case builtin_type::i64:
        return e_preun_minus<i64>(ctx, span, ec);
    case builtin_type::f32:
        return e_preun_minus<f32>(ctx, span, ec);
    case builtin_type::f64:
        return e_preun_minus<f64>(ctx, span, ec);
    case builtin_type::charr:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
    case builtin_type::voidd:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::preunary_bool_not(ExecConst ec) {
    assert(ec.holds_same_variant_type(ec));
    switch (ec.type_builtin()) {
    case builtin_type::boolean:
        return e_bool_not<bool>(ec);
    case builtin_type::u8:
    case builtin_type::i8:
    case builtin_type::u16:
    case builtin_type::i16:
    case builtin_type::u32:
    case builtin_type::i32:
    case builtin_type::u64:
    case builtin_type::i64:
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::charr:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::voidd:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::preunary_bit_not(ExecConst ec) {
    assert(ec.holds_same_variant_type(ec));
    switch (ec.type_builtin()) {
    case builtin_type::u8:
        return e_bit_not<u8>(ec);
    case builtin_type::i8:
        return e_bit_not<i8>(ec);
    case builtin_type::u16:
        return e_bit_not<u16>(ec);
    case builtin_type::i16:
        return e_bit_not<i16>(ec);
    case builtin_type::u32:
        return e_bit_not<u32>(ec);
    case builtin_type::i32:
        return e_bit_not<i32>(ec);
    case builtin_type::u64:
        return e_bit_not<u64>(ec);
    case builtin_type::i64:
        return e_bit_not<i64>(ec);
    case builtin_type::charr:
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
    case builtin_type::voidd:
        break;
    }
    return std::nullopt;
}

static std::string exec_id_str(ExecId eid) {
    return std::string{"Exec#"} + std::to_string(eid.raw());
}

static std::string slice_of_execs_to_str(Context& ctx, IdSlice<ExecId> elems) {
    std::string str{};
    for (auto eidx = elems.begin(); eidx != elems.end(); ++eidx) {
        str += exec_to_string(ctx, ctx.exec_id(eidx));
        if (eidx != elems.last_elem()) {
            str += ", ";
        }
    }
    return str;
}

static std::string slice_of_pattern_execs_to_str(Context& ctx, IdSlice<ExecId> elems) {
    std::string str{};
    for (auto eidx = elems.begin(); eidx != elems.end(); ++eidx) {
        str += exec_to_string(ctx, ctx.exec_id(eidx));
        if (eidx != elems.last_elem()) {
            str += "| ";
        }
    }
    return str;
}

std::string exec_to_string(Context& ctx, ExecId eid) {
    static constexpr std::string TAB = "    ";
    auto vs = Ovld{
        [&ctx](const ExecBlock& t) -> std::string {
            std::string str{};
            str += "{\n";

            const Block& block = ctx.block(t.block_id);

            for (const auto didx : block.defs) {
                str += TAB;
                str += def_to_string(ctx, ctx.def_id(didx));
                str += '\n';
            }

            for (const auto eidx : block.execs) {
                str += TAB;
                str += exec_to_string(ctx, ctx.exec_id(eidx));
                str += '\n';
            }

            str += '}';
            return str;
        },
        [](const ExecJump& t) -> std::string {
            std::string str{};
            str += "jump to ";
            str += (t.spot == jump_spot::start) ? "start of " : "end of ";
            str += exec_id_str(t.block);
            return str;
        },
        [&ctx](const ExecBranch& t) -> std::string {
            std::string str{};
            str += "branch if ";
            str += exec_to_string(ctx, t.condition);
            str += " then ";
            str += exec_to_string(ctx, t.then_block);
            str += " else ";
            str += exec_to_string(ctx, t.else_block);
            return str;
        },
        [&ctx](const ExecReturn& t) -> std::string {
            std::string str{};
            str += "return";
            if (t.return_value.has_value()) {
                str += ' ';
                str += exec_to_string(ctx, t.return_value.as_id());
            }
            str += ';';
            return str;
        },
        [&ctx](const ExecYield& t) -> std::string {
            std::string str{};
            str += "yield";
            if (t.yield_value.has_value()) {
                str += ' ';
                str += exec_to_string(ctx, t.yield_value.as_id());
            }
            return str;
        },
        [&ctx](const ExecUnionInit& t) -> std::string {
            return std::string(ctx.symbol_id_to_cstr(ctx.def(t.union_def_id).name)) + "{."
                   + ctx.symbol_id_to_cstr(ctx.def(ctx.def(t.union_def_id)
                                                       .as<DefUnion>()
                                                       .ordered_members.get(t.active_member_idx))
                                               .name)
                   + exec_to_string(ctx, t.member_init) + "}";
        },
        [&ctx](const ExecVariantInit& t) -> std::string {
            std::string str{};

            str += ctx.symbol_id_to_cstr(ctx.def(t.variant_def_id).name);

            str += "..";

            str += exec_to_string(ctx, t.payload_init);

            return str;
        },
        [&ctx](const ExecStructInit& t) -> std::string {
            std::string str{};

            if (!t.anonymous) {
                str += ctx.symbol_id_to_cstr(ctx.def(t.struct_def_id).name);

                str += '{';
            } else {
                str += '(';
            }

            for (auto i = 0u; i < t.member_inits.len(); ++i) {
                const auto eidx = t.member_inits.get(i);
                if (!t.anonymous) {
                    str += '.';
                    str += ctx.symbol_id_to_cstr(
                        ctx.def(ctx.def(t.struct_def_id).as<DefStruct>().ordered_members.get(i))
                            .name);
                    str += " = ";
                }
                str += exec_to_string(ctx, ctx.exec_id(eidx));
                if (eidx != t.member_inits.last_elem() || t.anonymous) {
                    str += ", ";
                }
            }

            if (!t.anonymous) {
                str += '}';
            } else {
                str += ')';
            }

            return str;
        },
        [&ctx](const ExecVariable& t) -> std::string {
            return ctx.symbol_id_to_cstr(ctx.def(t.def_id).name);
        },
        [&ctx](const ExecComptConstant& t) -> std::string { return t.to_string(ctx); },
        [&ctx](const ExecListLiteral& t) -> std::string {
            std::string str{};
            str += '[';
            str += slice_of_execs_to_str(ctx, t.elems);
            str += ']';
            return str;
        },
        [&ctx](const ExecAssignment& e) -> std::string {
            return exec_to_string(ctx, e.lhs) + " = " + exec_to_string(ctx, e.rhs);
        },
        [&ctx](const ExecMemberAccess& e) -> std::string {
            return exec_to_string(ctx, e.owner) + "."
                   + ctx.symbol_id_to_cstr(ctx.def(e.member).name);
        },
        [&ctx](const ExecBinary& e) -> std::string {
            return exec_to_string(ctx, e.lhs) + binary_op_to_cstr(e.op)
                   + exec_to_string(ctx, e.rhs);
        },
        [&ctx](const ExecCast& e) -> std::string {
            return exec_to_string(ctx, e.exec) + " as ("
                   + type_to_string_with_akas(ctx, e.target_tid) + ")";
        },
        [&ctx](const ExecSubscript& t) -> std::string {
            std::string str{};
            str += exec_to_string(ctx, t.base);
            str += '[';
            str += exec_to_string(ctx, t.index);
            str += ']';
            return str;
            return {};
        },
        [&ctx](const ExecFnCall& t) -> std::string {
            std::string str{};
            str += exec_to_string(ctx, t.callee);
            str += '(';
            str += slice_of_execs_to_str(ctx, t.args);
            str += ')';
            return str;
        },
        [&ctx](const ExecBorrow& t) -> std::string {
            std::string str{};
            str += '&';
            str += exec_to_string(ctx, t.borrowee);
            return str;
        },
        [&ctx](const ExecDeref& t) -> std::string {
            std::string str{};
            str += '*';
            str += exec_to_string(ctx, t.dereferenced);
            return str;
        },
        [&ctx](const ExecMatch& t) -> std::string {
            std::string str{};
            str += "match ";
            str += exec_to_string(ctx, t.scrutinee);
            str += " {";
            str += slice_of_execs_to_str(ctx, t.branches);
            str += '}';
            return str;
        },
        [&ctx](const ExecMatchBranch& t) -> std::string {
            std::string str{};
            str += slice_of_pattern_execs_to_str(ctx, t.patterns);
            str += " => ";
            str += exec_to_string(ctx, t.body);
            return str;
        },
        [&ctx](const ExecAddrOf& t) -> std::string {
            std::string str{};
            str += '^';
            str += exec_to_string(ctx, t.addressed);
            return str;
        },
        [&ctx](const ExecFnPtr& t) -> std::string {
            return ctx.symbol_id_to_cstr(ctx.def(t.func_def_id).name);
        },
        [&ctx](const ExecVariantFieldInit& t) -> std::string {
            std::string str{};

            str += ctx.symbol_id_to_cstr(ctx.def(t.variant_field_def_id).name);

            str += '(';

            for (auto eidx = t.member_inits.begin(); eidx != t.member_inits.end(); ++eidx) {
                str += exec_to_string(ctx, ctx.exec_id(eidx));
                if (eidx != t.member_inits.last_elem()) {
                    str += ", ";
                }
            }
            str += ')';

            return str;
        },
        [&ctx](const ExecRange t) -> std::string {
            return exec_to_string(ctx, t.start) + (t.inclusive ? "...=" : "...")
                   + exec_to_string(ctx, t.end);
        },
        [&ctx](const ExecCompoundAssignment& t) -> std::string {
            std::string str;
            str += exec_to_string(ctx, t.lhs);
            str += ' ';
            str += binary_op_to_cstr(t.op);
            str += ' ';
            str += exec_to_string(ctx, t.rhs);
            return str;
        },
        [&ctx](const ExecUnary& t) -> std::string {
            return unary_op_to_cstr(t.op) + exec_to_string(ctx, t.operand);
        },
        [&ctx](const ExecBuiltinAccess& t) -> std::string {
            std::string str = exec_to_string(ctx, t.from);
            switch (t.kind) {
            case builtin_access::str_len:
            case builtin_access::slice_len:
                str += ".len";
                break;
            case builtin_access::slice_ptr:
            case builtin_access::str_ptr:
                str += ".@ptr";
                break;
            case builtin_access::variant_discriminant:
                str += ".@discriminant";
                break;
            }
            return str;
        },

    };

    return ctx.exec(eid).visit(vs);
}

bool e_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2) {
    return explicitly_equivalent_exec(ctx, eid1, eid2);
}

size_t e_hash_exec(const Context& ctx, ExecId eid) {
    return hash_exec_with_explicit_equivalence(ctx, eid);
}

bool e_implicit_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2) {
    return implicit_equivalent_exec(ctx, eid1, eid2);
}

size_t e_implicit_hash_exec(const Context& ctx, ExecId eid) {
    return hash_exec_with_implicit_equivalence(ctx, eid);
}

} // namespace hir
