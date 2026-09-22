//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "tests/test.h"
#include "compiler/hir/context_database.hpp"
#include "compiler/hir/exec.hpp"
#include <cstdint>
#include <string>
#include <vector>

using SymSlice = std::vector<std::string>;
using namespace hir;

extern "C" {

br_test_result_t test_context_db(void) {
    TEST_INIT("context database");

    auto econst
        = [&br_test_result](ContextDatabase::DefQueryResult def, const ContextDatabase& db) {
              TEST_ASSERT(def.variable.has_value());

              TEST_ASSERT(def.variable.value().as<DefVariable>().compt_value.has_value());

              auto exec = db.exec(def.variable.value().as<DefVariable>().compt_value.as_id());

              TEST_ASSERT(exec.holds<ExecConst>());
              return exec;
          };

    auto assert_compt = [&br_test_result, &econst](ContextDatabase& db, const char* name, bool b) {
        auto def = db.query_canon_def({name});
        auto exec = econst(def, db);
        TEST_ASSERT_EQ(b, exec.as<ExecConst>().as<bool>());
    };

    auto assert_no_compt_val = [&br_test_result](ContextDatabase& db, const char* name) {
        auto def = db.query_canon_def({name});
        if (!def.variable.has_value()) {
            TEST_ASSERT(true);
            TEST_ASSERT(true);
            return;
        }
        if (def.variable.value().holds<DefVariable>()) {
            TEST_ASSERT(def.variable.value().holds<DefVariable>());
            TEST_ASSERT(def.variable.value().as<DefVariable>().compt_value.empty());
            return;
        }
        TEST_ASSERT(false);
        TEST_ASSERT(false);
    };

    // TEST 1: BITWISE
    const char* args0[] = {"bearc", "tests/hir/28.br"};
    ContextDatabase db28{sizeof(args0) / sizeof(char*), args0};

    auto def0 = db28.query_canon_def({"e"});
    auto exec0 = econst(def0, db28);
    TEST_ASSERT_EQ(0x10, exec0.as<ExecConst>().as<i32>());

    auto def1 = db28.query_canon_def({"b1"});
    auto exec1 = econst(def1, db28);
    TEST_ASSERT_EQ((0x11 | 0x10), static_cast<uint32_t>(exec1.as<ExecConst>().as<u8>()));

    auto def2 = db28.query_canon_def({"h2"});
    auto exec2 = econst(def2, db28);
    TEST_ASSERT_EQ((0x1111 ^ 0x1001), exec2.as<ExecConst>().as<usize>());

    // TEST 2: BOOLEAN OPS
    const char* args1[] = {"bearc", "tests/hir/30.br"};
    ContextDatabase db30{sizeof(args1) / sizeof(char*), args1};

    assert_compt(db30, "a", true);
    assert_compt(db30, "b", true);
    assert_compt(db30, "c", false);
    assert_compt(db30, "d", true);
    assert_compt(db30, "e", true);
    assert_compt(db30, "f", true);
    assert_compt(db30, "g", true);
    assert_compt(db30, "h", true);
    assert_compt(db30, "i", false);
    assert_compt(db30, "j", true);
    assert_no_compt_val(db30, "k");
    assert_compt(db30, "l", true);
    assert_compt(db30, "l1", false);
    assert_compt(db30, "l2", false);
    assert_compt(db30, "l3", true);
    assert_compt(db30, "m1", true);

    // TEST 3: ternary if
    const char* args2[] = {"bearc", "tests/hir/31.br"};
    ContextDatabase db31{sizeof(args2) / sizeof(char*), args2};

    assert_compt(db31, "a", true);
    assert_compt(db31, "b", true);

    // TEST 4: struct mem access
    const char* args3[] = {"bearc", "tests/hir/44.br"};
    ContextDatabase db44{sizeof(args2) / sizeof(char*), args3};
    auto d3 = db44.query_canon_def_id({"Foo", "a"});
    TEST_ASSERT(d3.variable_id.has_value());

    return TEST_RESULT;
}

} // extern "C"
