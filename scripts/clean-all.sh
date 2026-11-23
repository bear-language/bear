#!/usr/bin/env bash
set -euo pipefail

# usage: [Debug|Release|RelWithDebInfo]
config="${1:-Debug}"

case "$config" in
    Debug|Release|RelWithDebInfo) ;;
    *) echo "Invalid config: $config (expected Debug or Release or RelWithDebInfo)" >&2; exit 1;;
esac

rm -rf build
cmake -DCMAKE_BUILD_TYPE="$config" -DBUILD_LIB=ON -B build
cmake --build build -j"$(nproc)"
