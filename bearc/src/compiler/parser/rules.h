//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_PARSER_RULES
#define COMPILER_PARSER_RULES

#include "compiler/token.h"
#include <stdbool.h>
#include <stdint.h>

/// defines the associativity of an operator based on precedence (C++ style)
bool is_right_assoc_from_prec(uint8_t precedence);

bool is_binary_op(token_type_e type);
bool is_preunary_op(token_type_e type);
bool is_postunary_op(token_type_e type);
bool is_angle_bracket_operator(token_type_e t);

// find the precendence if a postunary operator based on token type
uint8_t prec_postunary(token_type_e type);

// find the precendence if a postunary operator based on token type
uint8_t prec_preunary(token_type_e type);

// find the precendence of a binary operator based on token type
uint8_t prec_binary(token_type_e type);

#endif // !COMPILER_PARSER_RULES
