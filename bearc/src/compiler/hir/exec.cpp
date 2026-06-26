//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/exec.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
namespace hir {

std::optional<ExecConst> ExecConst::try_safe_convert_to(builtin_type type) const {
    auto maybe_up = try_up_convert_to(type);
    return maybe_up.has_value() ? maybe_up : try_down_convert_to(type);
}

std::optional<ExecConst> ExecConst::try_up_convert_to(builtin_type type) const {
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
        case builtin_type::voidd:
        case builtin_type::str:
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
std::optional<ExecConst> ExecConst::try_down_convert_to(builtin_type type) const {

    using OptConst = std::optional<ExecConst>;

    auto to_optconst = +[](ConstantValue v) -> OptConst { return OptConst{ExecConst{v}}; };

    auto none = +[] { return OptConst{}; };

    auto fits_signed = [](int64_t v, int64_t lo, int64_t hi) { return v >= lo && v <= hi; };

    auto fits_unsigned = [](uint64_t v, uint64_t hi) { return v <= hi; };

    auto fits_float_int = [](double v) { return std::floor(v) == v; };

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
        default:
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

        default:
            return none();
        }
    }

    // f64
    case 11: {
        double v = as<double>();

        switch (type) {

        case builtin_type::u32:
            if (fits_float_int(v) && v >= 0 && v <= UINT32_MAX) {
                return to_optconst(ConstantValue{static_cast<uint32_t>(v)});
            }
            return none();

        case builtin_type::u64:
            if (fits_float_int(v) && v >= 0 && v <= UINT64_MAX) {
                return to_optconst(ConstantValue{static_cast<uint64_t>(v)});
            }
            return none();

        case builtin_type::i32:
            if (fits_float_int(v) && v >= INT32_MIN && v <= INT32_MAX) {
                return to_optconst(ConstantValue{static_cast<int32_t>(v)});
            }
            return none();

        case builtin_type::i64:
            if (fits_float_int(v) && v >= INT64_MIN && v <= INT64_MAX) {
                return to_optconst(ConstantValue{static_cast<int64_t>(v)});
            }
            return none();

        case builtin_type::f32:
            return to_optconst(ConstantValue{static_cast<float>(v)});

        case builtin_type::f64:
            return *this;

        case builtin_type::boolean:
            return to_optconst(ConstantValue{static_cast<bool>(v)});

        default:
            return none();
        }
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
    : value{value}, span{span} {
    bool truely_compt = can_be_compt(ctx);
    if (should_be_compt && !truely_compt) {
        ctx.emplace_diagnostic(span, diag_code::cannot_resolve_value_at_compt, diag_type::error);
    }
    this->compt = truely_compt && should_be_compt;
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

bool Exec::can_be_compt(const Context& ctx) {
    auto get_d = [&](DefId did) { return ctx.def(did); };
    auto get_e = [&](ExecId eid) { return ctx.exec(eid); };
    auto eidx_to_e = [&](IdIdx<ExecId> eid) { return ctx.exec(eid); };
    auto vs = Ovld{
        [&](const ExecBlock&) -> bool { return false; },
        [&](const ExecExprStmt&) -> bool { return false; },
        [&](const ExecBreakStmt&) -> bool { return false; },
        [&](const ExecContinueStmt&) -> bool { return false; },
        [&](const ExecIfStmt&) -> bool { return false; },
        [&](const ExecLoopStmt&) -> bool { return false; },
        [&](const ExecReturnStmt&) -> bool { return false; },
        [&](const ExecYieldStmt&) -> bool { return false; },
        // exprs
        [&](const ExecExprVariable& t) -> bool { return get_d(t.def_id).compt; },
        [&](const ExecConst&) -> bool { return true; },
        [&](const ExecExprListLiteral& t) -> bool {
            // just check each elem
            for (auto eidx = t.elems.begin(); eidx != t.elems.end(); eidx++) {
                if (!eidx_to_e(eidx).compt) {
                    return false;
                }
            }
            return true;
        },
        [&](const ExecExprAssignMove& t) -> bool {
            return get_e(t.lhs).compt && get_e(t.rhs).compt;
        },
        [&](const ExecExprAssignEqual& t) -> bool {
            return get_e(t.lhs).compt && get_e(t.rhs).compt;
        },
        [&](const ExecExprIs& t) -> bool {
            return get_e(t.variant_instance).compt && get_e(t.variant_decomp).compt;
        },
        [&](const ExecExprMemberAccess& t) -> bool {
            return get_e(t.owner).compt && get_e(t.member).compt;
        },
        [&](const ExecExprPointerMemberAccess& t) -> bool {
            return get_e(t.owner).compt && get_e(t.member).compt;
        },
        [&](const ExecExprBinary& t) -> bool { return get_e(t.lhs).compt && get_e(t.rhs).compt; },
        [&](const ExecExprCast& t) -> bool { return get_e(t.expr).compt; },
        [&](const ExecExprPreUnary& t) -> bool { return get_e(t.expr).compt; },
        [&](const ExecExprPostUnary& t) -> bool { return get_e(t.expr).compt; },
        [&](const ExecExprSubscript& t) -> bool {
            return get_e(t.base).compt && get_e(t.index).compt;
        },
        [&](const ExecExprFnCall& t) -> bool {
            const bool callee = get_e(t.callee).compt;
            // check all args, ret false if one isn't compt
            for (auto eidx = t.args.begin(); eidx != t.args.end(); eidx++) {
                if (!eidx_to_e(eidx).compt) {
                    return false;
                }
            }
            return callee;
        },
        [&](const ExecExprBorrow& t) -> bool { return get_e(t.borrowee).compt; },
        [&](const ExecExprDeref& t) -> bool { return get_e(t.expr).compt; },
        [&](const ExecExprStructInit& t) -> bool {
            for (auto eidx = t.member_inits.begin(); eidx != t.member_inits.end(); eidx++) {
                if (!eidx_to_e(eidx).compt) {
                    return false;
                }
            }
            return true;
        },
        [&](const ExecExprStructMemberInit& t) -> bool { return get_e(t.value).compt; },
        // full compt ctrl no for now
        [&](const ExecExprClosure&) -> bool { return false; },
        [&](const ExecExprVariantDecomp&) -> bool { return false; },
        [&](const ExecExprMatch&) -> bool { return false; },
        [&](const ExecExprMatchBranch&) -> bool { return false; },
        [&](const ExecFnPtr&) -> bool { return true; },
        [&](const ExecExprUnionInit&) -> bool { return true; },
        [&](const ExecExprVariantInit&) -> bool { return true; },
        [&](const ExecVariantFieldInit&) -> bool { return true; },
    };
    return visit(vs);
}

SymbolId ExecConst::to_symbol_id(Context& ctx) const {
    std::string str;
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
        return (as<bool>()) ? "true" : "false";
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
        return (as<bool>()) ? "true" : "false";
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

template <typename T> EConst e_plus(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() + r.as<T>())};
}

template <typename T> EConst e_minus(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() - r.as<T>())};
}

template <typename T> EConst e_multiply(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() * r.as<T>())};
}

template <typename T> EConst e_divide(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() / r.as<T>())};
}

template <typename T> EConst e_mod(EConst l, EConst r) {
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

template <typename T> EConst e_lsh(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() << r.as<T>())};
}

template <typename T> EConst e_rshl(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() >> r.as<T>())};
}

template <typename T> EConst e_rsha(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() >> r.as<T>())};
}

template <typename T> EConst e_greater_than(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() > r.as<T>())};
}
template <typename T> EConst e_less_than(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() < r.as<T>())};
}
template <typename T> EConst e_greater_than_or_equal(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() >= r.as<T>())};
}
template <typename T> EConst e_less_than_or_equal(EConst l, EConst r) {
    return EConst{static_cast<T>(l.as<T>() <= r.as<T>())};
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

template <typename T> EConst e_preun_minus(ExecConst e) {
    return EConst{static_cast<T>(-(e.as<T>()))};
}

template <typename T> EConst e_bit_not(ExecConst e) { return EConst{static_cast<T>(~e.as<T>())}; }

template <typename T> EConst e_bool_not(ExecConst e) { return EConst{static_cast<T>(!e.as<T>())}; }

std::optional<ExecConst> ExecConst::plus(Context& ctx, ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    // std::cout << lhs.to_string() << " + " << rhs.to_string() << '\n'; // debug
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_plus<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_plus<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_plus<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_plus<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_plus<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_plus<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_plus<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_plus<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_plus<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_plus<f64>(lhs, rhs);
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

std::optional<ExecConst> ExecConst::minus(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_minus<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_minus<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_minus<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_minus<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_minus<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_minus<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_minus<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_minus<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_minus<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_minus<f64>(lhs, rhs);
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}
std::optional<ExecConst> ExecConst::multiply(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_multiply<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_multiply<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_multiply<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_multiply<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_multiply<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_multiply<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_multiply<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_multiply<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_multiply<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_multiply<f64>(lhs, rhs);
    case builtin_type::charr:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return std::nullopt;
}

std::optional<ExecConst> ExecConst::divide(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_divide<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_divide<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_divide<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_divide<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_divide<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_divide<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_divide<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_divide<i64>(lhs, rhs);
    case builtin_type::f32:
        return e_divide<f32>(lhs, rhs);
    case builtin_type::f64:
        return e_divide<f64>(lhs, rhs);
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

std::optional<ExecConst> ExecConst::bit_lsh(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_lsh<u8>(lhs, rhs);
    case builtin_type::i8:
        return e_lsh<i8>(lhs, rhs);
    case builtin_type::u16:
        return e_lsh<u16>(lhs, rhs);
    case builtin_type::i16:
        return e_lsh<i16>(lhs, rhs);
    case builtin_type::u32:
        return e_lsh<u32>(lhs, rhs);
    case builtin_type::i32:
        return e_lsh<i32>(lhs, rhs);
    case builtin_type::u64:
        return e_lsh<u64>(lhs, rhs);
    case builtin_type::i64:
        return e_lsh<i64>(lhs, rhs);
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
std::optional<ExecConst> ExecConst::bit_rsha(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::i8:
        return e_rsha<i8>(lhs, rhs);
    case builtin_type::i16:
        return e_rsha<i16>(lhs, rhs);
    case builtin_type::i32:
        return e_rsha<i32>(lhs, rhs);
    case builtin_type::i64:
        return e_rsha<i64>(lhs, rhs);
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
std::optional<ExecConst> ExecConst::bit_rshl(ExecConst lhs, ExecConst rhs) {
    assert(lhs.holds_same_variant_type(rhs));
    switch (lhs.type_builtin()) {
    case builtin_type::u8:
        return e_rshl<u8>(lhs, rhs);
    case builtin_type::u16:
        return e_rshl<u16>(lhs, rhs);
    case builtin_type::u32:
        return e_rshl<u32>(lhs, rhs);
    case builtin_type::u64:
        return e_rshl<u64>(lhs, rhs);
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
std::optional<ExecConst> ExecConst::preunary_minus(ExecConst ec) {
    assert(ec.holds_same_variant_type(ec));
    switch (ec.type_builtin()) {
    case builtin_type::u8:
        return e_preun_minus<u8>(ec);
    case builtin_type::i8:
        return e_preun_minus<i8>(ec);
    case builtin_type::u16:
        return e_preun_minus<u16>(ec);
    case builtin_type::i16:
        return e_preun_minus<i16>(ec);
    case builtin_type::u32:
        return e_preun_minus<u32>(ec);
    case builtin_type::i32:
        return e_preun_minus<i32>(ec);
    case builtin_type::u64:
        return e_preun_minus<u64>(ec);
    case builtin_type::i64:
        return e_preun_minus<i64>(ec);
    case builtin_type::f32:
        return e_preun_minus<f32>(ec);
    case builtin_type::f64:
        return e_preun_minus<f64>(ec);
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

std::string exec_to_string(Context& ctx, ExecId eid) {
    auto vs = Ovld{
        [](const ExecBlock& t) -> std::string { return "{...}"; },
        [](const ExecExprStmt& t) -> std::string { return "(...)"; },
        [](const ExecBreakStmt& t) -> std::string { return "break"; },
        [](const ExecContinueStmt& t) -> std::string { return "continue"; },
        [](const ExecIfStmt& t) -> std::string { return "if (...) {...} ..."; },
        [](const ExecLoopStmt& t) -> std::string { return "loop {...}"; },
        [](const ExecReturnStmt& t) -> std::string { return "return"; },
        [](const ExecYieldStmt& t) -> std::string { return "yield"; },
        [&ctx](const ExecExprUnionInit& t) -> std::string {
            return std::string(ctx.symbol_id_to_cstr(ctx.def(t.union_def_id).name)) + "{...}";
        },
        [&ctx](const ExecExprVariantInit& t) -> std::string {
            std::string str{};

            str.reserve(128); // decent amount

            str += ctx.symbol_id_to_cstr(ctx.def(t.variant_def_id).name);

            str += "..";

            str += exec_to_string(ctx, t.payload_init);

            return str;
        },
        [&ctx](const ExecExprStructInit& t) -> std::string {
            std::string str{};

            str.reserve(256); // decent amount

            str += ctx.symbol_id_to_cstr(ctx.def(t.struct_def_id).name);

            str += "{";

            for (auto eidx = t.member_inits.begin(); eidx != t.member_inits.end(); ++eidx) {
                str += exec_to_string(ctx, ctx.exec_id(eidx));
                if (eidx != t.member_inits.last_elem()) {
                    str += ", ";
                }
            }

            str += "}";

            return str;
        },
        [&ctx](const ExecExprStructMemberInit& t) -> std::string {
            return "." + std::string(ctx.symbol_id_to_cstr(ctx.def(t.field_def).name))
                   + ((t.move) ? " <- " : " = ") + exec_to_string(ctx, t.value);
        },
        [&ctx](const ExecExprVariable& t) -> std::string {
            return ctx.symbol_id_to_cstr(ctx.def(t.def_id).name);
        },
        [&ctx](const ExecExprComptConstant& t) -> std::string { return t.to_string(ctx); },
        [&ctx](const ExecExprListLiteral& t) -> std::string {
            std::string str{};

            str.reserve(512); // decent amount

            str += "[";

            for (auto eidx = t.elems.begin(); eidx != t.elems.end(); ++eidx) {
                str += exec_to_string(ctx, ctx.exec_id(eidx));
                if (eidx != t.elems.last_elem()) {
                    str += ", ";
                }
            }

            str += "]";

            return str;
        },
        [](const ExecExprAssignMove& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprAssignEqual& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprIs& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprMemberAccess& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprPointerMemberAccess& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprBinary& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprCast& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprPreUnary& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprPostUnary& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprSubscript& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprFnCall& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprBorrow& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprDeref& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprClosure& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprVariantDecomp& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprMatch& t) -> std::string {
            // todo
            return {};
        },
        [](const ExecExprMatchBranch& t) -> std::string {
            // todo
            return {};
        },
        [&ctx](const ExecFnPtr& t) -> std::string {
            // todo
            return ctx.symbol_id_to_cstr(ctx.def(t.func_def_id).name);
        },
        [&ctx](const ExecVariantFieldInit& t) -> std::string {
            std::string str{};

            str.reserve(256); // decent amount

            str += ctx.symbol_id_to_cstr(ctx.def(t.variant_field_def_id).name);

            str += "(";

            for (auto eidx = t.member_inits.begin(); eidx != t.member_inits.end(); ++eidx) {
                str += exec_to_string(ctx, ctx.exec_id(eidx));
                if (eidx != t.member_inits.last_elem()) {
                    str += ", ";
                }
            }
            str += ")";

            return str;
        },
    };

    return ctx.exec(eid).visit(vs);
}
} // namespace hir
