//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef ANSI_CODES_H
#define ANSI_CODES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

bool ansi_init(void);

const char* ansi_reset(void);

const char* ansi_bold(void);
const char* ansi_faint(void);
const char* ansi_italic(void);
const char* ansi_underline(void);

const char* ansi_black_fg(void);
const char* ansi_red_fg(void);
const char* ansi_green_fg(void);
const char* ansi_yellow_fg(void);
const char* ansi_blue_fg(void);
const char* ansi_magenta_fg(void);
const char* ansi_cyan_fg(void);
const char* ansi_white_fg(void);

const char* ansi_black_bg(void);
const char* ansi_red_bg(void);
const char* ansi_green_bg(void);
const char* ansi_yellow_bg(void);
const char* ansi_blue_bg(void);
const char* ansi_magenta_bg(void);
const char* ansi_cyan_bg(void);
const char* ansi_white_bg(void);

const char* ansi_bold_green(void);
const char* ansi_bold_magenta(void);
const char* ansi_bold_red(void);
const char* ansi_bold_yellow(void);
const char* ansi_bold_cyan(void);
const char* ansi_bold_blue(void);
const char* ansi_bold_reset(void);

#ifdef __cplusplus
}
#endif

#endif // ANSI_CODES_H
