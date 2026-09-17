//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILE_H
#define COMPILE_H
#include "cli/args.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * compiles a source file given the file name
 *
 * returns the count of compiler errors
 */
int compile_file(const bearc_args_t* args);

#ifdef __cplusplus
}
#endif

#endif // !COMPILE_H
