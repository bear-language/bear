//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "tests/test.h"
#include "cli/args.h"
#include "compiler/token.h"
#include "string.h"
#include "utils/ansi_codes.h"
#include "utils/vector.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

bearc_args_t args;

int main(int argc, char** argv) {

    ansi_init();

    args = parse_cli_args(argc, argv);

    br_test_result_t total = test_total_init();
    vector_t results = vector_create_and_reserve(sizeof(br_test_result_t), 8);

    // add all tests here ----------
    *((br_test_result_t*)vector_emplace_back(&results)) = test_parser();
    *((br_test_result_t*)vector_emplace_back(&results)) = test_hir();
    *((br_test_result_t*)vector_emplace_back(&results)) = test_context_db();
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    printf("%s -----------------------------%s\n", ansi_bold_reset(), ansi_reset());
    printf(" |%s        test results       %s|\n", ansi_bold_blue(), ansi_bold_reset());
    printf("%s -----------------------------%s\n", ansi_bold_reset(), ansi_reset());
    for (size_t i = 0; i < results.size; i++) {
        br_test_result_t* res = (br_test_result_t*)vector_at(&results, i);
        print_result(res);
        test_tally(&total, res);
    }
    printf("%s -----------------------------%s\n", ansi_bold_reset(), ansi_reset());
    print_result(&total);
    vector_destroy(&results);
    token_maps_free(); // after all operations involving token lookups are done
    return (int)(total.cnt_total - total.cnt_success);
}
/*
 * macros to easily test tests in the form tests/00.br
 */

br_test_result_t test_parser(void) {
    TEST_INIT("parser");
    char* parser_args[]
        = {"bearc",
           "--parse-only",
           /*"--pretty-print"*/}; // pretty-print to catch unexpectly null fields and other
                                  // malformation-related errors that could cause segfaults/crashes
                                  // this makes tests sustainally slower, so only enable when
                                  // parsing updates are made to verify correctness
    TEST_SET_ARGS(parser_args);
    ASSERT_EQ_ERR("parser/00", 3);
    ASSERT_EQ_ERR("parser/01", 0);
    ASSERT_EQ_ERR("parser/02", 2);
    ASSERT_EQ_ERR("parser/03", 0);
    ASSERT_EQ_ERR("parser/04", 2);
    ASSERT_EQ_ERR("parser/05", 0);
    ASSERT_EQ_ERR("parser/06", 9);
    ASSERT_EQ_ERR("parser/07", 5);
    ASSERT_EQ_ERR("parser/08", 0);
    ASSERT_EQ_ERR("parser/09", 6);
    ASSERT_EQ_ERR("parser/10", 1);
    ASSERT_EQ_ERR("parser/11", 0);
    ASSERT_EQ_ERR("parser/12", 0);
    ASSERT_EQ_ERR("parser/13", 1);
    ASSERT_EQ_ERR("parser/14", 0);
    ASSERT_EQ_ERR("parser/15", 0);
    ASSERT_EQ_ERR("parser/16", 0);
    ASSERT_EQ_ERR("parser/17", 0);
    ASSERT_EQ_ERR("parser/18", 11);
    ASSERT_EQ_ERR("parser/19", 2);
    ASSERT_EQ_ERR("parser/20", 0);
    ASSERT_EQ_ERR("parser/21", 10);
    ASSERT_EQ_ERR("parser/22", 1);
    ASSERT_EQ_ERR("parser/23", 1);
    ASSERT_EQ_ERR("parser/24", 0);
    ASSERT_EQ_ERR("parser/25", 5);
    ASSERT_EQ_ERR("parser/26", 1);
    ASSERT_EQ_ERR("parser/27", 10);
    ASSERT_EQ_ERR("parser/28", 0);
    ASSERT_EQ_ERR("parser/29", 0);
    ASSERT_EQ_ERR("parser/30", 0);
    ASSERT_EQ_ERR("parser/31", 0);
    ASSERT_EQ_ERR("parser/32", 0);
    ASSERT_EQ_ERR("parser/33", 0);
    ASSERT_EQ_ERR("parser/34", 1);
    ASSERT_EQ_ERR("parser/35", 0);
    ASSERT_EQ_ERR("parser/36", 0);
    ASSERT_EQ_ERR("parser/37", 1);
    ASSERT_EQ_ERR("parser/38", 0);
    ASSERT_EQ_ERR("parser/39", 0);
    ASSERT_EQ_ERR("parser/40", 6);
    ASSERT_EQ_ERR("parser/41", 3);
    ASSERT_EQ_ERR("parser/42", 1);
    ASSERT_EQ_ERR("parser/43", 0);
    ASSERT_EQ_ERR("parser/44", 1);
    ASSERT_EQ_ERR("parser/45", 1);
    ASSERT_EQ_ERR("parser/46", 1);
    ASSERT_EQ_ERR("parser/47", 1);
    ASSERT_EQ_ERR("parser/48", 4);
    ASSERT_EQ_ERR("parser/49", 0);
    ASSERT_EQ_ERR("parser/50", 0);
    ASSERT_EQ_ERR("parser/51", 1);
    ASSERT_EQ_ERR("parser/52", 5);
    ASSERT_EQ_ERR("parser/53", 0);
    ASSERT_EQ_ERR("parser/54", 3);
    ASSERT_EQ_ERR("parser/55", 0);
    ASSERT_EQ_ERR("parser/56", 1);
    ASSERT_EQ_ERR("parser/57", 3);
    ASSERT_EQ_ERR("parser/58", 0);
    ASSERT_EQ_ERR("parser/59", 0);
    ASSERT_EQ_ERR("parser/60", 0);
    ASSERT_EQ_ERR("parser/61", 0);
    ASSERT_EQ_ERR("parser/62", 0);
    ASSERT_EQ_ERR("parser/63", 2);
    ASSERT_EQ_ERR("parser/64", 2);
    ASSERT_EQ_ERR("parser/65", 0);
    ASSERT_EQ_ERR("parser/66", 2);
    ASSERT_EQ_ERR("parser/67", 0);

    return TEST_RESULT;
}

br_test_result_t test_hir(void) {
    TEST_INIT("hir");

    char* hir_args[] = {"bearc", "-I", "tests/lib", "--check", "--lib"};
    TEST_SET_ARGS(hir_args);

    char* args1[] = {"bearc", "07.br", "-I", "tests/hir", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args1, 0);
    ASSERT_EQ_ERR("hir/07", 5);
    char* args3[]
        = {"bearc", "tests/hir/04.br", "--compile", "--import-path", ".", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args3, 10);
    char* args4[] = {"bearc", "tests/hir/00.br",      "--compile", "--import-path",
                     ".",     "--warn-cyclic-import", "--check",   "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args4, 2);
    char* args5[] = {"bearc",   "-I",   "tests/projects/00", "-c", "00.br", "--warn-cyclic-import",
                     "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGSN(args5, 8, 2);
    char* args6[] = {"bearc", "-I", "tests/projects/01", "-c", "00.br", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGSN(args6, 4, 2);
    char* args7[] = {"bearc", "00.br", "-I", "tests/projects/02", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGSN(args7, 12, 3);
    ASSERT_EQ_ERR("hir/09", 2);
    ASSERT_EQ_ERR("hir/10", 2);
    ASSERT_EQ_ERR("hir/11", 8);
    ASSERT_EQ_ERR("hir/12", 3);
    ASSERT_EQ_ERR("hir/06", 8);
    ASSERT_EQ_ERR("hir/13", 53);
    ASSERT_EQ_ERR("hir/14", 7);
    ASSERT_EQ_ERR("hir/15", 25);
    ASSERT_EQ_ERR("hir/16", 9);
    ASSERT_EQ_ERR("hir/17", 5);
    ASSERT_EQ_ERR("hir/18", 5);
    ASSERT_EQ_ERR("hir/19", 14);
    ASSERT_EQ_ERR("hir/20", 20);
    ASSERT_EQ_ERR("hir/21", 4);
    ASSERT_EQ_ERR("hir/22", 5);
    ASSERT_EQ_ERR("hir/23", 4);
    ASSERT_EQ_ERR("hir/24", 2);
    ASSERT_EQ_ERR("hir/25", 10);
    char* args26[] = {"bearc", "00.br", "-I", "tests/projects/03", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGSN(args26, 10, 3);
    ASSERT_EQ_ERR("hir/26", 13);
    ASSERT_EQ_ERR("hir/27", 23);
    ASSERT_EQ_ERR("hir/28", 9);
    ASSERT_EQ_ERR("hir/29", 15);
    ASSERT_EQ_ERR("hir/30", 7);
    ASSERT_EQ_ERR("hir/31", 0);
    ASSERT_EQ_ERR("hir/32", 11);
    ASSERT_EQ_ERR("hir/33", 8);
    ASSERT_EQ_ERR("hir/34", 5);
    ASSERT_EQ_ERR("hir/35", 2);
    ASSERT_EQ_ERR("hir/36", 1);
    ASSERT_EQ_ERR("hir/37", 1);
    ASSERT_EQ_ERR("hir/38", 4);
    ASSERT_EQ_ERR("hir/39", 3);
    ASSERT_EQ_ERR("hir/40", 2);
    ASSERT_EQ_ERR("hir/41", 7);
    ASSERT_EQ_ERR("hir/42", 2);
    ASSERT_EQ_ERR("hir/43", 2);
    ASSERT_EQ_ERR("hir/44", 3);
    ASSERT_EQ_ERR("hir/45", 12);
    ASSERT_EQ_ERR("hir/46", 1);
    ASSERT_EQ_ERR("hir/47", 23);
    ASSERT_EQ_ERR("hir/48", 4);
    ASSERT_EQ_ERR("hir/49", 5);
    ASSERT_EQ_ERR("hir/50", 6);
    ASSERT_EQ_ERR("hir/51", 2);
    ASSERT_EQ_ERR("hir/52", 4);
    ASSERT_EQ_ERR("hir/53", 36);
    ASSERT_EQ_ERR("hir/54", 10);
    ASSERT_EQ_ERR("hir/55", 15);
    ASSERT_EQ_ERR("hir/56", 1);
    ASSERT_EQ_ERR("hir/57", 20);
    ASSERT_EQ_ERR("hir/58", 44);
    ASSERT_EQ_ERR("hir/59", 8);
    ASSERT_EQ_ERR("hir/60", 6);
    ASSERT_EQ_ERR("hir/61", 4);
    ASSERT_EQ_ERR("hir/62", 1);
    ASSERT_EQ_ERR("hir/63", 53);
    ASSERT_EQ_ERR("hir/64", 47);
    ASSERT_EQ_ERR("hir/65", 39);
    ASSERT_EQ_ERR("hir/66", 3);
    ASSERT_EQ_ERR("hir/67", 0);
    ASSERT_EQ_ERR("hir/68", 5);
    ASSERT_EQ_ERR("hir/69", 4);
    ASSERT_EQ_ERR("hir/70", 2);
    ASSERT_EQ_ERR("hir/71", 12);
    ASSERT_EQ_ERR("hir/72", 1);
    ASSERT_EQ_ERR("hir/73", 17);
    ASSERT_EQ_ERR("hir/74", 6);
    ASSERT_EQ_ERR("hir/75", 16);
    ASSERT_EQ_ERR("hir/76", 2);
    ASSERT_EQ_ERR("hir/77", 18);
    ASSERT_EQ_ERR("hir/78", 15);
    ASSERT_EQ_ERR("hir/79", 1);
    ASSERT_EQ_ERR("hir/80", 8);
    ASSERT_EQ_ERR("hir/81", 1);
    ASSERT_EQ_ERR("hir/82", 3);
    ASSERT_EQ_ERR("hir/83", 3);
    ASSERT_EQ_ERR("hir/84", 12);
    ASSERT_EQ_ERR("hir/85", 37);
    ASSERT_EQ_ERR("hir/86", 0);
    char* args87[] = {"bearc", "tests/hir/87.br", "-I", "tests/hir", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args87, 16);
    ASSERT_EQ_ERR("hir/88", 5);
    ASSERT_EQ_ERR("hir/89", 4);
    ASSERT_EQ_ERR("hir/90", 2);
    ASSERT_EQ_ERR("hir/91", 9);
    ASSERT_EQ_ERR("hir/92", 11);
    ASSERT_EQ_ERR("hir/93", 23);
    ASSERT_EQ_ERR("hir/94", 3);
    ASSERT_EQ_ERR("hir/95", 2);
    ASSERT_EQ_ERR("hir/96", 4);
    ASSERT_EQ_ERR("hir/97", 6);
    ASSERT_EQ_ERR("hir/98", 11);
    ASSERT_EQ_ERR("hir/99", 13);
    ASSERT_EQ_ERR("hir/a00", 5);
    ASSERT_EQ_ERR("hir/a01", 28);
    ASSERT_EQ_ERR("hir/a02", 15);
    ASSERT_EQ_ERR("hir/a03", 9);
    ASSERT_EQ_ERR("hir/a04", 10);
    ASSERT_EQ_ERR("hir/a05", 13);
    ASSERT_EQ_ERR("hir/a06", 2);
    ASSERT_EQ_ERR("hir/a07", 14);
    ASSERT_EQ_ERR("hir/a08", 5);
    ASSERT_EQ_ERR("hir/a09", 3);
    ASSERT_EQ_ERR("hir/a10", 7);
    ASSERT_EQ_ERR("hir/a11", 18);
    ASSERT_EQ_ERR("hir/a12", 10);
    ASSERT_EQ_ERR("hir/a13", 10);
    ASSERT_EQ_ERR("hir/a14", 10);
    ASSERT_EQ_ERR("hir/a15", 26);
    char* args_a16[] = {"bearc", "tests/hir/a16.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a16, 1);
    char* args_a17[] = {"bearc", "tests/hir/a17.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a17, 1);
    char* args_a18[] = {"bearc", "tests/hir/a18.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a18, 44);
    char* args_a19[] = {"bearc", "tests/hir/a19.br", "-s", "-I", "tests/lib", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a19, 1);
    char* args_a20[] = {"bearc", "tests/hir/a20.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a20, 14);
    char* args_a21[] = {"bearc", "tests/hir/a21.br", "-s", "-I", "tests/lib", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a21, 9);
    char* args_a22[] = {"bearc", "tests/hir/a22.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a22, 5);
    char* args_a23[] = {"bearc", "tests/hir/a23.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a23, 4);
    char* args_a24[] = {"bearc", "tests/hir/a24.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a24, 21);
    char* args_a25[] = {"bearc", "tests/hir/a25.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a25, 3);
    char* args_a26[] = {"bearc", "tests/hir/a26.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a26, 3);
    char* args_a27[] = {"bearc", "tests/hir/a27.br", "-s", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a27, 16);
    ASSERT_EQ_ERR("hir/a28", 12);
    ASSERT_EQ_ERR("hir/a29", 4);
    ASSERT_EQ_ERR("hir/a30", 2);
    ASSERT_EQ_ERR("hir/a31", 1);
    ASSERT_EQ_ERR("hir/a32", 13);
    ASSERT_EQ_ERR("hir/a33", 2);
    ASSERT_EQ_ERR("hir/a34", 43);
    ASSERT_EQ_ERR("hir/a35", 28);
    ASSERT_EQ_ERR("hir/a36", 5);
    ASSERT_EQ_ERR("hir/a37", 6);
    ASSERT_EQ_ERR("hir/a38", 13);
    ASSERT_EQ_ERR("hir/a39", 12);
    ASSERT_EQ_ERR("hir/a40", 18);
    ASSERT_EQ_ERR("hir/a41", 20);
    ASSERT_EQ_ERR("hir/a42", 26);
    ASSERT_EQ_ERR("hir/a43", 6);
    ASSERT_EQ_ERR("hir/a44", 1);
    ASSERT_EQ_ERR("hir/a45", 2);
    ASSERT_EQ_ERR("hir/a46", 2);
    ASSERT_EQ_ERR("hir/a47", 5);
    ASSERT_EQ_ERR("hir/a48", 2);
    ASSERT_EQ_ERR("hir/a49", 0);
    ASSERT_EQ_ERR("hir/a50", 1);
    ASSERT_EQ_ERR("hir/a51", 0);
    ASSERT_EQ_ERR("hir/a52", 0);
    ASSERT_EQ_ERR("hir/a53", 1);
    ASSERT_EQ_ERR("hir/a54", 3);
    ASSERT_EQ_ERR("hir/a55", 3);
    ASSERT_EQ_ERR("hir/a56", 3);
    ASSERT_EQ_ERR("hir/a57", 1);
    ASSERT_EQ_ERR("hir/a58", 8);
    ASSERT_EQ_ERR("hir/a59", 5);
    ASSERT_EQ_ERR("hir/a60", 21);
    ASSERT_EQ_ERR("hir/a61", 10);
    ASSERT_EQ_ERR("hir/a62", 6);
    ASSERT_EQ_ERR("hir/a63", 1);
    // this test is kinda dodgy and only exists because it accidently  found a spanning issue with
    // function call diagnostics (hir/a64):
    ASSERT_EQ_ERR("hir/a64", 21);
    ASSERT_EQ_ERR("hir/a65", 3);
    ASSERT_EQ_ERR("hir/a66", 12);
    ASSERT_EQ_ERR("hir/a67", 9);
    ASSERT_EQ_ERR("hir/a68", 13);
    ASSERT_EQ_ERR("hir/a69", 2);
    ASSERT_EQ_ERR("hir/a70", 7);
    ASSERT_EQ_ERR("hir/a71", 7);
    ASSERT_EQ_ERR("hir/a72", 9);
    ASSERT_EQ_ERR("hir/a73", 9);
    ASSERT_EQ_ERR("hir/a74", 1);
    ASSERT_EQ_ERR("hir/a75", 2);
    ASSERT_EQ_ERR("hir/a76", 16);
    ASSERT_EQ_ERR("hir/a77", 8);
    ASSERT_EQ_ERR("hir/a78", 6);
    ASSERT_EQ_ERR("hir/a79", 1);
    ASSERT_EQ_ERR("hir/a80", 1);
    ASSERT_EQ_ERR("hir/a81", 604);
    ASSERT_EQ_ERR("hir/a82", 0);
    ASSERT_EQ_ERR("hir/a83", 9);
    char* args04[]
        = {"bearc", "main.br", "-I", "tests/projects/04", "tests/lib", "--check", "--lib"};
    ASSERT_EQ_ERR_FROM_ARGSN(args04, 2, 3);
    ASSERT_EQ_ERR("hir/a84", 10);
    ASSERT_EQ_ERR("hir/a85", 2);
    ASSERT_EQ_ERR("hir/a86", 6);
    ASSERT_EQ_ERR("hir/a87", 6);
    ASSERT_EQ_ERR("hir/a88", 4);
    ASSERT_EQ_ERR("hir/a89", 13);
    ASSERT_EQ_ERR("hir/a90", 5);
    ASSERT_EQ_ERR("hir/a91", 78);
    ASSERT_EQ_ERR("hir/a92", 16);
    ASSERT_EQ_ERR("hir/a93", 10);
    ASSERT_EQ_ERR("hir/a94", 30);
    ASSERT_EQ_ERR("hir/a95", 22);
    ASSERT_EQ_ERR("hir/a96", 0);
    ASSERT_EQ_ERR("hir/a97", 23);
    ASSERT_EQ_ERR("hir/a98", 5);
    ASSERT_EQ_ERR("hir/a99", 6);
    ASSERT_EQ_ERR("hir/b00", 3);
    ASSERT_EQ_ERR("hir/b01", 5);
    ASSERT_EQ_ERR("hir/b02", 2);
    ASSERT_EQ_ERR("hir/b03", 6);
    ASSERT_EQ_ERR("hir/b04", 1);
    ASSERT_EQ_ERR("hir/b05", 2);
    ASSERT_EQ_ERR("hir/b06", 2);
    ASSERT_EQ_ERR("hir/b07", 1);
    ASSERT_EQ_ERR("hir/b08", 63);
    ASSERT_EQ_ERR("hir/b09", 21);
    ASSERT_EQ_ERR("hir/b10", 26);
    ASSERT_EQ_ERR("hir/b11", 13);
    ASSERT_EQ_ERR("hir/b12", 2);
    ASSERT_EQ_ERR("hir/b13", 32);
    ASSERT_EQ_ERR("hir/b14", 27);
    ASSERT_EQ_ERR("hir/b15", 13);
    ASSERT_EQ_ERR("hir/b16", 17);
    ASSERT_EQ_ERR("hir/b17", 8);
    ASSERT_EQ_ERR("hir/b18", 24);
    ASSERT_EQ_ERR("hir/b19", 10);
    ASSERT_EQ_ERR("hir/b20", 17);
    ASSERT_EQ_ERR("hir/b21", 6);
    ASSERT_EQ_ERR("hir/b22", 12);
    ASSERT_EQ_ERR("hir/b23", 7);
    ASSERT_EQ_ERR("hir/b24", 0);
    ASSERT_EQ_ERR("hir/b25", 9);
    ASSERT_EQ_ERR("hir/b26", 21);
    ASSERT_EQ_ERR("hir/b27", 22);
    ASSERT_EQ_ERR("hir/b28", 24);
    ASSERT_EQ_ERR("hir/b29", 22);
    ASSERT_EQ_ERR("hir/b30", 3);
    ASSERT_EQ_ERR("hir/b31", 10);
    ASSERT_EQ_ERR("hir/b32", 10);
    ASSERT_EQ_ERR("hir/b33", 41);
    ASSERT_EQ_ERR("hir/b34", 4);
    ASSERT_EQ_ERR("hir/b35", 8);
    ASSERT_EQ_ERR("hir/b36", 26);
    ASSERT_EQ_ERR("hir/b37", 16);
    ASSERT_EQ_ERR("hir/b38", 7);
    ASSERT_EQ_ERR("hir/b39", 8);
    ASSERT_EQ_ERR("hir/b40", 68);
    ASSERT_EQ_ERR("hir/b41", 21);
    ASSERT_EQ_ERR("hir/b42", 44);
    ASSERT_EQ_ERR("hir/b43", 12);
    ASSERT_EQ_ERR("hir/b44", 15);
    ASSERT_EQ_ERR("hir/b45", 17);
    ASSERT_EQ_ERR("hir/b46", 16);
    ASSERT_EQ_ERR("hir/b47", 31);
    ASSERT_EQ_ERR("hir/b48", 6);
    ASSERT_EQ_ERR("hir/b49", 6);
    ASSERT_EQ_ERR("hir/b50", 6);
    ASSERT_EQ_ERR("hir/b51", 7);
    ASSERT_EQ_ERR("hir/b52", 9);
    ASSERT_EQ_ERR("hir/b53", 10);
    ASSERT_EQ_ERR("hir/b54", 9);
    ASSERT_EQ_ERR("hir/b55", 5);
    ASSERT_EQ_ERR("hir/b56", 5);
    char* args_b57[] = {"bearc", "tests/hir/b57.br", "--check"};
    ASSERT_EQ_ERR_FROM_ARGS(args_b57, 3);
    ASSERT_EQ_ERR("hir/b58", 8);
    ASSERT_EQ_ERR("hir/b59", 15);
    ASSERT_EQ_ERR("hir/b60", 5);
    ASSERT_EQ_ERR("hir/b61", 8);
    ASSERT_EQ_ERR("hir/b62", 6);
    ASSERT_EQ_ERR("hir/b63", 11);
    ASSERT_EQ_ERR("hir/b64", 12);
    ASSERT_EQ_ERR("hir/b65", 11);

    return TEST_RESULT;
}

br_test_result_t test_total_init(void) {
    br_test_result_t res = {.cnt_success = 0, .cnt_total = 0, .name = "total"};
    return res;
}

void test_tally(br_test_result_t* total, br_test_result_t* new_test) {
    total->cnt_total += new_test->cnt_total;
    total->cnt_success += new_test->cnt_success;
}

void print_result(br_test_result_t* res) {
    size_t succ = res->cnt_success;
    size_t tot = res->cnt_total;
    const char* color = ansi_bold_reset();
    if (succ == tot) {
        color = ansi_bold_green();
    } else if (tot != 0 && ((double)succ / (double)tot >= .9)) {
        color = ansi_bold_yellow();
    } else {
        color = ansi_bold_red();
    }
    printf("  %s%-18s   %s%zu/%zu%s\n", ansi_bold(), res->name, color, succ, tot, ansi_reset());
}
