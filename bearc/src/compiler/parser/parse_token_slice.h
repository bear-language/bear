//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

/// for adding contents of the vector to the arena, and freeing the vector
/// - vector must contain type token_t*

#include "compiler/parser/parser.h"
#include "compiler/token.h"

token_ptr_slice_t parse_id_token_slice(parser_t* p, token_type_e divider);
