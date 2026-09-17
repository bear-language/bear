//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef TEST_H
#define TEST_H

#include "cli/args.h"
#ifdef __cplusplus
extern "C" {
#endif

#include "compiler/compile.h"
#include "utils/ansi_codes.h"
#include <stddef.h>

typedef struct {
    /// subtest total count
    size_t cnt_total;
    /// subtest success count
    size_t cnt_success;
    /// name of test
    const char* name;
} br_test_result_t;

extern bearc_args_t args;

br_test_result_t test_parser(void);
br_test_result_t test_hir(void);
br_test_result_t test_total_init(void);
br_test_result_t test_context_db(void);

void test_tally(br_test_result_t* total, br_test_result_t* new_test);

void print_result(br_test_result_t* res);
#define TEST_SET_ARGS(_args)                                                                       \
    do {                                                                                           \
        args = parse_cli_args(sizeof(_args) / sizeof((_args)[0]), _args);                          \
    } while (0);
#define TEST_INIT(_name)                                                                           \
    br_test_result_t br_test_result = {.cnt_total = 0, .cnt_success = 0, .name = (_name)};         \
    int true_cnt = 0;

#define ASSERT_EQ_ERR(file_name, err_cnt)                                                          \
    args.input_file_name = "tests/" file_name ".br";                                               \
    true_cnt = compile_file(&args);                                                                \
    if (true_cnt == (err_cnt)) {                                                                   \
        br_test_result.cnt_success++;                                                              \
    } else {                                                                                       \
        printf("%s [!] %sTEST FAILED %s('"                                                         \
               "tests/" file_name ".br"                                                            \
               "'): expected %d errors, got %d %s\n\n",                                            \
               ansi_bold_reset(), ansi_bold_red(), ansi_bold_reset(), err_cnt, true_cnt,           \
               ansi_reset());                                                                      \
    }                                                                                              \
    br_test_result.cnt_total++;
#define ASSERT_EQ_ERR_FROM_ARGS(args_, err_cnt)                                                    \
    do {                                                                                           \
        bearc_args_t arrggss = parse_cli_args(sizeof(args_) / sizeof((args_)[0]), args_);          \
        true_cnt = compile_file(&arrggss);                                                         \
        if (true_cnt == (err_cnt)) {                                                               \
            br_test_result.cnt_success++;                                                          \
        } else {                                                                                   \
            printf("%s [!] %sTEST FAILED %s('"                                                     \
                   "%s"                                                                            \
                   "'): expected %d errors, got %d %s\n\n",                                        \
                   ansi_bold_reset(), ansi_bold_red(), ansi_bold_reset(), (args_)[1], err_cnt,     \
                   true_cnt, ansi_reset());                                                        \
        }                                                                                          \
        br_test_result.cnt_total++;                                                                \
    } while (0);
#define ASSERT_EQ_ERR_FROM_ARGSN(args_, err_cnt, arg_index_indicating_name)                        \
    do {                                                                                           \
        bearc_args_t arrggss = parse_cli_args(sizeof(args_) / sizeof((args_)[0]), args_);          \
        true_cnt = compile_file(&arrggss);                                                         \
        if (true_cnt == (err_cnt)) {                                                               \
            br_test_result.cnt_success++;                                                          \
        } else {                                                                                   \
            printf("%s [!] %sTEST FAILED %s('"                                                     \
                   "%s"                                                                            \
                   "'): expected %d errors, got %d %s\n\n",                                        \
                   ansi_bold_reset(), ansi_bold_red(), ansi_bold_reset(),                          \
                   (args_)[arg_index_indicating_name], err_cnt, true_cnt, ansi_reset());           \
        }                                                                                          \
        br_test_result.cnt_total++;                                                                \
    } while (0);
#define TEST_RESULT br_test_result

#define TEST_ASSERT(x)                                                                             \
    do {                                                                                           \
        br_test_result.cnt_total++;                                                                \
        if (x) {                                                                                   \
            br_test_result.cnt_success++;                                                          \
        } else {                                                                                   \
            printf("%s [!] %sTEST FAILED: %s assertion failed at "                                 \
                   "%s:%d"                                                                         \
                   "%s\n\n",                                                                       \
                   ansi_bold_reset(), ansi_bold_red(), ansi_bold_reset(), __FILE__, __LINE__,      \
                   ansi_reset());                                                                  \
        }                                                                                          \
    } while (0);
#ifdef __cplusplus
} // extern "C"
#endif

#ifdef __cplusplus
#include <iostream>
#define TEST_ASSERT_EQ(expected, got)                                                              \
    do {                                                                                           \
        br_test_result.cnt_total++;                                                                \
        if ((expected) == (got)) {                                                                 \
            br_test_result.cnt_success++;                                                          \
        } else {                                                                                   \
            printf("%s [!] %sTEST FAILED: %s assertion failed at "                                 \
                   "%s:%d"                                                                         \
                   "%s\n\n",                                                                       \
                   ansi_bold_reset(), ansi_bold_red(), ansi_bold_reset(), __FILE__, __LINE__,      \
                   ansi_reset());                                                                  \
            std::cout << ansi_bold_reset() << "--> " << "expected " << ansi_bold_green()           \
                      << (expected) << ansi_bold_reset() << ", but got " << ansi_bold_red()        \
                      << (got) << ansi_reset() << '\n';                                            \
        }                                                                                          \
    } while (0);
#endif

#endif // !TEST_H
