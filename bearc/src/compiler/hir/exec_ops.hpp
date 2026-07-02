//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_EXEC_OPS_HPP
#define COMPILER_HIR_EXEC_OPS_HPP
#include "compiler/hir/variant_helpers.hpp"
#include "compiler/token.h"
#include <cstdint>
#include <optional>
#include <variant>

namespace hir {
enum class binary_op : uint8_t {
    plus,
    minus,
    multiply,
    divide,
    modulo,
    range_exclusive,
    range_inclusive,
    bit_or,
    bit_and,
    bit_xor,
    greater_than,
    less_than,
    greater_than_or_equal,
    less_than_or_equal,
    left_bitshift,
    right_shift_logical,
    right_shift_arithmetic,
    bool_or,
    bool_and,
    bool_equal,
    bool_not_equal,
};
enum class unary_op : uint8_t { inc, dec, plus, minus, bool_not, bit_not };

enum class assign_op : uint8_t { assign_eq, assign_move };

enum class is_as_op : uint8_t { is, as };

enum class access_op : uint8_t { rarrow, dot };

class InvalidOp {};

const char* binary_op_to_cstr(binary_op op);

const char* binary_op_to_cstr(unary_op op);

const char* is_as_op_to_cstr(is_as_op op);

// converts a token to standard binary ops (arithmetic and comparison only)
std::optional<binary_op> token_to_binary_op(const token_t* tkn);

// converts a token to stand unary ops, boolean and arithmetic ops
std::optional<unary_op> token_to_unary_op(const token_t* tkn);

struct ComptBinaryOp : NodeWithVariantValue<ComptBinaryOp> {
    using Value = std::variant<binary_op, assign_op, is_as_op, access_op, InvalidOp>;
    Value value;
    ComptBinaryOp(const token_t* tkn) noexcept {
        auto maybe_bin = token_to_binary_op(tkn);
        if (maybe_bin.has_value()) {
            value = maybe_bin.value();
            return;
        }
        switch (tkn->type) {
        case TOK_AS:
            value = is_as_op::as;
            break;
        case TOK_IS:
            value = is_as_op::is;
            break;
        case TOK_DOT:
            value = access_op::dot;
            break;
        case TOK_RARROW:
            value = access_op::rarrow;
            break;
        case TOK_ASSIGN_EQ:
        case TOK_ASSIGN_PLUS_EQ:
        case TOK_ASSIGN_MINUS_EQ:
        case TOK_ASSIGN_MULT_EQ:
        case TOK_ASSIGN_DIV_EQ:
        case TOK_ASSIGN_MOD_EQ:
        case TOK_ASSIGN_AND_EQ:
        case TOK_ASSIGN_OR_EQ:
        case TOK_ASSIGN_XOR_EQ:
        case TOK_ASSIGN_LSH_EQ:
        case TOK_ASSIGN_RSHL_EQ:
        case TOK_ASSIGN_RSHA_EQ:
            value = assign_op::assign_eq;
            break;
        case TOK_ASSIGN_MOVE:
            value = assign_op::assign_move;
            break;
        default:
            value = InvalidOp{};
            break;
        }
    }
};

static inline bool bin_op_is_eq_neq(binary_op op) {
    return op == binary_op::bool_equal || op == binary_op::bool_not_equal;
}

} // namespace hir

#endif
