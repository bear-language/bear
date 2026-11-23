#!/usr/bin/env bash
set -euo pipefail

# usage: ./run-tests.sh [Debug|Release|RelWithDebInfo]
config="${1:-Debug}"

case "$config" in
    Debug|Release|RelWithDebInfo) ;;
    *)
        echo "Invalid config: $config (expected Debug or Release or RelWithDebInfo)" >&2
        exit 1
        ;;
esac

cmake -S . -B build-test -DCMAKE_BUILD_TYPE="$config" -DTEST=ON
cmake --build build-test -j"$(nproc)"
echo "Running tests..."
./build-test/bearc/bearc

