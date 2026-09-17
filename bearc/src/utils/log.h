//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef LOG_H
#define LOG_H

#ifdef DEBUG_BUILD
#include <stdio.h>
#define LOG_STR(x) printf("%s\n", x);
#define LOG_STR_NNL(x)                                                                             \
    printf("[DEBUG] "                                                                              \
           "%s",                                                                                   \
           x);
#define LOG_CHAR(x) printf("%c\n", x);
#define LOG_CHAR_NNL(x) printf("%c", x);
#define LOG_ERR(x) puts("[ERROR] " x);
#else
#define LOG_STR(x)
#define LOG_STR_NNL(x)
#define LOG_CHAR(x)
#define LOG_CHAR_NNL(x)
#define LOG_ERR(x)
#endif

#endif // !LOG_H
