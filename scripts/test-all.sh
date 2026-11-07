#!/usr/bin/env bash

./scripts/test.sh
TEST_RES=$?
python3 scripts/fuzz.py
echo "(unit test) results: $TEST_RES failure(s) (scroll up for details)"
