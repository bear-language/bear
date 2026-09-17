//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "utils/ansi_codes.h"
#include <stdbool.h>
#include <unistd.h>

#define ESC "\033"

// reset
#define ANSI_RESET ESC "[0m"

// text styles
#define ANSI_BOLD ESC "[1m"
#define ANSI_FAINT ESC "[2m"
#define ANSI_ITALIC ESC "[3m"
#define ANSI_UNDERLINE ESC "[4m"

// foreground Colors
#define ANSI_BLACK_FG ESC "[30m"
#define ANSI_RED_FG ESC "[31m"
#define ANSI_GREEN_FG ESC "[32m"
#define ANSI_YELLOW_FG ESC "[33m"
#define ANSI_BLUE_FG ESC "[34m"
#define ANSI_MAGENTA_FG ESC "[35m"
#define ANSI_CYAN_FG ESC "[36m"
#define ANSI_WHITE_FG ESC "[37m"

// background colors
#define ANSI_BLACK_BG ESC "[40m"
#define ANSI_RED_BG ESC "[41m"
#define ANSI_GREEN_BG ESC "[42m"
#define ANSI_YELLOW_BG ESC "[43m"
#define ANSI_BLUE_BG ESC "[44m"
#define ANSI_MAGENTA_BG ESC "[45m"
#define ANSI_CYAN_BG ESC "[46m"
#define ANSI_WHITE_BG ESC "[47m"

// more
#define ANSI_BOLD_GREEN ANSI_BOLD ANSI_GREEN_FG
#define ANSI_BOLD_MAGENTA ANSI_BOLD ANSI_MAGENTA_FG
#define ANSI_BOLD_RED ANSI_BOLD ANSI_RED_FG
#define ANSI_BOLD_YELLOW ANSI_BOLD ANSI_YELLOW_FG
#define ANSI_BOLD_CYAN ANSI_BOLD ANSI_CYAN_FG
#define ANSI_BOLD_BLUE ANSI_BOLD ANSI_BLUE_FG
#define ANSI_BOLD_WHITE ANSI_BOLD ANSI_WHITE_FG
#define ANSI_BOLD_RESET ANSI_RESET ANSI_BOLD

static char* blank = "";
static bool tty = false;

bool ansi_init(void) {
    tty = isatty(STDOUT_FILENO);
    return tty;
}

#define ANSI_FN(name, value)                                                                       \
    const char* name(void) { return tty ? (value) : blank; }

/* reset */
ANSI_FN(ansi_reset, ANSI_RESET)

/* styles */
ANSI_FN(ansi_bold, ANSI_BOLD)
ANSI_FN(ansi_faint, ANSI_FAINT)
ANSI_FN(ansi_italic, ANSI_ITALIC)
ANSI_FN(ansi_underline, ANSI_UNDERLINE)

/* foreground */
ANSI_FN(ansi_black_fg, ANSI_BLACK_FG)
ANSI_FN(ansi_red_fg, ANSI_RED_FG)
ANSI_FN(ansi_green_fg, ANSI_GREEN_FG)
ANSI_FN(ansi_yellow_fg, ANSI_YELLOW_FG)
ANSI_FN(ansi_blue_fg, ANSI_BLUE_FG)
ANSI_FN(ansi_magenta_fg, ANSI_MAGENTA_FG)
ANSI_FN(ansi_cyan_fg, ANSI_CYAN_FG)
ANSI_FN(ansi_white_fg, ANSI_WHITE_FG)

/* background */
ANSI_FN(ansi_black_bg, ANSI_BLACK_BG)
ANSI_FN(ansi_red_bg, ANSI_RED_BG)
ANSI_FN(ansi_green_bg, ANSI_GREEN_BG)
ANSI_FN(ansi_yellow_bg, ANSI_YELLOW_BG)
ANSI_FN(ansi_blue_bg, ANSI_BLUE_BG)
ANSI_FN(ansi_magenta_bg, ANSI_MAGENTA_BG)
ANSI_FN(ansi_cyan_bg, ANSI_CYAN_BG)
ANSI_FN(ansi_white_bg, ANSI_WHITE_BG)

/* composites */
ANSI_FN(ansi_bold_green, ANSI_BOLD_GREEN)
ANSI_FN(ansi_bold_magenta, ANSI_BOLD_MAGENTA)
ANSI_FN(ansi_bold_red, ANSI_BOLD_RED)
ANSI_FN(ansi_bold_yellow, ANSI_BOLD_YELLOW)
ANSI_FN(ansi_bold_cyan, ANSI_BOLD_CYAN)
ANSI_FN(ansi_bold_blue, ANSI_BOLD_BLUE)
ANSI_FN(ansi_bold_reset, ANSI_BOLD_RESET)
