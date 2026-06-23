//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/parser/rules.h"
#include "compiler/token.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_ASSOCIATIVITY 18

static bool right_assoc_map[MAX_ASSOCIATIVITY + 1] = {
    [3] = true,
    [16] = true,
    [18] = true,
};
bool is_right_assoc_from_prec(uint8_t precedence) { return right_assoc_map[precedence]; }

static uint8_t postunary_prec_map[TOK__NUM] = {
    [TOK_INC] = 2,
    [TOK_DEC] = 2,
};

uint8_t prec_postunary(token_type_e type) { return postunary_prec_map[type]; }

static uint8_t preunary_prec_map[TOK__NUM] = {
    [TOK_INC] = 3,          [TOK_DEC] = 3,           [TOK_PLUS] = 3,        [TOK_MINUS] = 3,
    [TOK_STAR] = 3,         [TOK_AMPER] = 3,         [TOK_SIZEOF] = 3,      [TOK_ALIGNOF] = 3,
    [TOK_BOOL_NOT] = 3,     [TOK_TYPEOF] = 3,        [TOK_BIT_NOT] = 3,     [TOK_COMPT] = 3,
    [TOK_SAME_TYPE] = 3,    [TOK_STATIC_ASSERT] = 3, [TOK_TYPE_TO_STR] = 3, [TOK_DEFINED] = 3,
    [TOK_HAS_CONTRACT] = 3, [TOK_INFERABLE_AS] = 3,  [TOK_ERR] = 3,         [TOK_WARN] = 3,
    [TOK_NOTE] = 3,         [TOK_HELP] = 3,

};

uint8_t prec_preunary(token_type_e type) { return preunary_prec_map[type]; }

static uint8_t binary_prec_map[TOK__NUM] = {
    [TOK_GENERIC_SEP] = 1,

    [TOK_DOT] = 2,
    [TOK_RARROW] = 2,

    [TOK_ELLIPSE] = 3,
    [TOK_ELLIPSE_EQ] = 3,
    [TOK_AS] = 3,
    [TOK_IS] = 3,

    [TOK_IF] = 4,

    [TOK_PLUS] = 6,
    [TOK_MINUS] = 6,

    [TOK_STAR] = 5,
    [TOK_DIVIDE] = 5,
    [TOK_MODULO] = 5,

    [TOK_BAR] = 13,
    [TOK_AMPER] = 11,
    [TOK_BIT_NOT] = 3,
    [TOK_BIT_XOR] = 12,

    [TOK_GT] = 9,
    [TOK_LT] = 9,

    [TOK_STREAM] = 7,

    [TOK_LSH] = 7,
    [TOK_RSHL] = 7,
    [TOK_RSHA] = 7,

    [TOK_BOOL_OR] = 15,
    [TOK_BOOL_AND] = 14,

    [TOK_GE] = 9,
    [TOK_LE] = 9,
    [TOK_BOOL_EQ] = 10,
    [TOK_NE] = 10,

    [TOK_ASSIGN_EQ] = 16,
    [TOK_ASSIGN_MOVE] = 16,

    [TOK_ASSIGN_PLUS_EQ] = 16,
    [TOK_ASSIGN_MINUS_EQ] = 16,
    [TOK_ASSIGN_MULT_EQ] = 16,
    [TOK_ASSIGN_DIV_EQ] = 16,
    [TOK_ASSIGN_MOD_EQ] = 16,

    [TOK_ASSIGN_AND_EQ] = 16,
    [TOK_ASSIGN_OR_EQ] = 16,
    [TOK_ASSIGN_XOR_EQ] = 16,
    [TOK_ASSIGN_LSH_EQ] = 16,
    [TOK_ASSIGN_RSHL_EQ] = 16,
    [TOK_ASSIGN_RSHA_EQ] = 16,
};
uint8_t prec_binary(token_type_e type) { return binary_prec_map[type]; }

bool is_binary_op(token_type_e type) { return binary_prec_map[type]; }
bool is_angle_bracket_operator(token_type_e t) {
    return t == TOK_GT || t == TOK_LT || t == TOK_RSHA || t == TOK_RSHL || t == TOK_LSH;
}
bool is_preunary_op(token_type_e type) { return preunary_prec_map[type]; }
bool is_postunary_op(token_type_e type) { return postunary_prec_map[type]; }
