//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

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
    ASSERT_EQ_ERR("parser/04", 4);
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
    ASSERT_EQ_ERR("parser/19", 5);
    ASSERT_EQ_ERR("parser/20", 0);
    ASSERT_EQ_ERR("parser/21", 10);
    ASSERT_EQ_ERR("parser/22", 1);
    ASSERT_EQ_ERR("parser/23", 1);
    ASSERT_EQ_ERR("parser/24", 1);
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
    ASSERT_EQ_ERR("parser/49", 1);
    ASSERT_EQ_ERR("parser/50", 0);
    ASSERT_EQ_ERR("parser/51", 1);
    ASSERT_EQ_ERR("parser/52", 5);
    ASSERT_EQ_ERR("parser/53", 0);
    ASSERT_EQ_ERR("parser/54", 2);
    ASSERT_EQ_ERR("parser/55", 0);
    ASSERT_EQ_ERR("parser/56", 2);
    ASSERT_EQ_ERR("parser/57", 3);
    ASSERT_EQ_ERR("parser/58", 0);
    ASSERT_EQ_ERR("parser/59", 0);

    return TEST_RESULT;
}

br_test_result_t test_hir(void) {
    TEST_INIT("hir");
    char* args1[] = {"bearc", "07.br", "-I", "tests/hir"};
    ASSERT_EQ_ERR_FROM_ARGS(args1, 0);
    char* args2[] = {"bearc", "tests/hir/07.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args2, 2);
    char* args3[] = {"bearc", "tests/hir/04.br", "--compile", "--import-path", "."};
    ASSERT_EQ_ERR_FROM_ARGS(args3, 10);
    char* args4[] = {"bearc", "tests/hir/00.br", "--compile", "--import-path", "."};
    ASSERT_EQ_ERR_FROM_ARGS(args4, 2);
    char* args5[] = {"bearc", "-I", "tests/projects/00", "-c", "00.br"};
    ASSERT_EQ_ERR_FROM_ARGSN(args5, 3, 2);
    char* args6[] = {"bearc", "-I", "tests/projects/01", "-c", "00.br"};
    ASSERT_EQ_ERR_FROM_ARGSN(args6, 4, 2);
    char* args7[] = {"bearc", "00.br", "-I", "tests/projects/02"};
    ASSERT_EQ_ERR_FROM_ARGSN(args7, 4, 3);
    char* args8[] = {"bearc", "tests/hir/09.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args8, 5);
    char* args9[] = {"bearc", "tests/hir/10.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args9, 2);
    char* args10[] = {"bearc", "tests/hir/11.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args10, 8);
    char* args11[] = {"bearc", "tests/hir/12.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args11, 3);
    char* args12[] = {"bearc", "tests/hir/06.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args12, 8);
    char* args13[] = {"bearc", "tests/hir/13.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args13, 34);
    char* args14[] = {"bearc", "tests/hir/14.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args14, 5);
    char* args15[] = {"bearc", "tests/hir/15.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args15, 14);
    char* args16[] = {"bearc", "tests/hir/16.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args16, 5);
    char* args17[] = {"bearc", "tests/hir/17.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args17, 4);
    char* args18[] = {"bearc", "tests/hir/18.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args18, 5);
    char* args19[] = {"bearc", "tests/hir/19.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args19, 14);
    char* args20[] = {"bearc", "tests/hir/20.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args20, 22);
    char* args21[] = {"bearc", "tests/hir/21.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args21, 4);
    char* args22[] = {"bearc", "tests/hir/22.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args22, 5);
    char* args23[] = {"bearc", "tests/hir/23.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args23, 4);
    char* args24[] = {"bearc", "tests/hir/24.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args24, 2);
    char* args25[] = {"bearc", "tests/hir/25.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args25, 8);
    char* args26[] = {"bearc", "00.br", "-I", "tests/projects/03"};
    ASSERT_EQ_ERR_FROM_ARGSN(args26, 9, 3);
    char* args27[] = {"bearc", "tests/hir/26.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args27, 4);
    char* args28[] = {"bearc", "tests/hir/27.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args28, 21);
    char* args29[] = {"bearc", "tests/hir/28.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args29, 5);
    char* args30[] = {"bearc", "tests/hir/30.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args30, 8);
    char* args31[] = {"bearc", "tests/hir/31.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args31, 0);
    char* args32[] = {"bearc", "tests/hir/32.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args32, 14);
    char* args33[] = {"bearc", "tests/hir/33.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args33, 10);
    char* args34[] = {"bearc", "tests/hir/34.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args34, 4);
    char* args35[] = {"bearc", "tests/hir/35.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args35, 2);
    char* args36[] = {"bearc", "tests/hir/36.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args36, 3);
    char* args37[] = {"bearc", "tests/hir/37.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args37, 1);
    char* args38[] = {"bearc", "tests/hir/38.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args38, 4);
    char* args39[] = {"bearc", "tests/hir/39.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args39, 3);
    char* args40[] = {"bearc", "tests/hir/40.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args40, 3);
    char* args41[] = {"bearc", "tests/hir/41.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args41, 7);
    char* args42[] = {"bearc", "tests/hir/42.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args42, 2);
    char* args43[] = {"bearc", "tests/hir/43.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args43, 2);
    char* args44[] = {"bearc", "tests/hir/44.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args44, 4);
    char* args45[] = {"bearc", "tests/hir/45.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args45, 11);
    char* args46[] = {"bearc", "tests/hir/46.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args46, 1);
    char* args47[] = {"bearc", "tests/hir/47.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args47, 23);
    char* args48[] = {"bearc", "tests/hir/48.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args48, 2);
    char* args49[] = {"bearc", "tests/hir/49.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args49, 14);
    char* args50[] = {"bearc", "tests/hir/50.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args50, 7);
    char* args51[] = {"bearc", "tests/hir/51.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args51, 2);
    char* args52[] = {"bearc", "tests/hir/52.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args52, 4);
    char* args53[] = {"bearc", "tests/hir/53.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args53, 22);
    char* args54[] = {"bearc", "tests/hir/54.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args54, 8);
    char* args55[] = {"bearc", "tests/hir/55.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args55, 13);
    char* args56[] = {"bearc", "tests/hir/56.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args56, 1);
    char* args57[] = {"bearc", "tests/hir/57.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args57, 20);
    char* args58[] = {"bearc", "tests/hir/58.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args58, 33);
    char* args59[] = {"bearc", "tests/hir/59.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args59, 6);
    char* args60[] = {"bearc", "tests/hir/60.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args60, 6);
    char* args61[] = {"bearc", "tests/hir/61.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args61, 5);
    char* args62[] = {"bearc", "tests/hir/62.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args62, 1);
    char* args63[] = {"bearc", "tests/hir/63.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args63, 32);
    char* args64[] = {"bearc", "tests/hir/64.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args64, 26);
    char* args65[] = {"bearc", "tests/hir/65.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args65, 18);
    char* args66[] = {"bearc", "tests/hir/66.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args66, 3);
    char* args67[] = {"bearc", "tests/hir/67.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args67, 0);
    char* args68[] = {"bearc", "tests/hir/68.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args68, 5);
    char* args69[] = {"bearc", "tests/hir/69.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args69, 4);
    char* args70[] = {"bearc", "tests/hir/70.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args70, 2);
    char* args71[] = {"bearc", "tests/hir/71.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args71, 12);
    char* args72[] = {"bearc", "tests/hir/72.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args72, 1);
    char* args73[] = {"bearc", "tests/hir/73.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args73, 14);
    char* args74[] = {"bearc", "tests/hir/74.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args74, 6);
    char* args75[] = {"bearc", "tests/hir/75.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args75, 14);
    char* args76[] = {"bearc", "tests/hir/76.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args76, 2);
    char* args77[] = {"bearc", "tests/hir/77.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args77, 19);
    char* args78[] = {"bearc", "tests/hir/78.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args78, 19);
    char* args79[] = {"bearc", "tests/hir/79.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args79, 1);
    char* args80[] = {"bearc", "tests/hir/80.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args80, 6);
    char* args81[] = {"bearc", "tests/hir/81.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args81, 1);
    char* args82[] = {"bearc", "tests/hir/82.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args82, 3);
    char* args83[] = {"bearc", "tests/hir/83.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args83, 3);
    char* args84[] = {"bearc", "tests/hir/84.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args84, 14);
    char* args85[] = {"bearc", "tests/hir/85.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args85, 38);
    char* args86[] = {"bearc", "tests/hir/86.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args86, 0);
    char* args87[] = {"bearc", "tests/hir/87.br", "-I", "tests/hir"};
    ASSERT_EQ_ERR_FROM_ARGS(args87, 19);
    char* args88[] = {"bearc", "tests/hir/88.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args88, 6);
    char* args89[] = {"bearc", "tests/hir/89.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args89, 4);
    char* args90[] = {"bearc", "tests/hir/90.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args90, 2);
    char* args91[] = {"bearc", "tests/hir/91.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args91, 8);
    char* args92[] = {"bearc", "tests/hir/92.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args92, 9);
    char* args93[] = {"bearc", "tests/hir/93.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args93, 23);
    char* args94[] = {"bearc", "tests/hir/94.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args94, 3);
    char* args95[] = {"bearc", "tests/hir/95.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args95, 2);
    char* args96[] = {"bearc", "tests/hir/96.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args96, 4);
    char* args97[] = {"bearc", "tests/hir/97.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args97, 7);
    char* args98[] = {"bearc", "tests/hir/98.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args98, 11);
    char* args99[] = {"bearc", "tests/hir/99.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args99, 12);
    char* args_a00[] = {"bearc", "tests/hir/a00.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a00, 5);
    char* args_a01[] = {"bearc", "tests/hir/a01.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a01, 9);
    char* args_a02[] = {"bearc", "tests/hir/a02.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a02, 16);
    char* args_a03[] = {"bearc", "tests/hir/a03.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a03, 10);
    char* args_a04[] = {"bearc", "tests/hir/a04.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a04, 10);
    char* args_a05[] = {"bearc", "tests/hir/a05.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a05, 16);
    char* args_a06[] = {"bearc", "tests/hir/a06.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a06, 2);
    char* args_a07[] = {"bearc", "tests/hir/a07.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a07, 13);
    char* args_a08[] = {"bearc", "tests/hir/a08.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a08, 5);
    char* args_a09[] = {"bearc", "tests/hir/a09.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a09, 4);
    char* args_a10[] = {"bearc", "tests/hir/a10.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a10, 8);
    char* args_a11[] = {"bearc", "tests/hir/a11.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a11, 18);
    char* args_a12[] = {"bearc", "tests/hir/a12.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a12, 10);
    char* args_a13[] = {"bearc", "tests/hir/a13.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a13, 7);
    char* args_a14[] = {"bearc", "tests/hir/a14.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a14, 7);
    char* args_a15[] = {"bearc", "tests/hir/a15.br"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a15, 19);
    char* args_a16[] = {"bearc", "tests/hir/a16.br", "-s"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a16, 1);
    char* args_a17[] = {"bearc", "tests/hir/a17.br", "-s"};
    ASSERT_EQ_ERR_FROM_ARGS(args_a17, 1);

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
