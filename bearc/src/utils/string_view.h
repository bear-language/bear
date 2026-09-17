//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef UTILS_STRING_VIEW
#define UTILS_STRING_VIEW
#include <stddef.h>

typedef struct {
    const char* start;
    size_t len;
} string_view_t;

typedef string_view_t sv_t;

#endif // !UTILS_STRING_VIEW
