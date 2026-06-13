//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef BEARC_UTILS_HPP
#define BEARC_UTILS_HPP

#include <cstddef>

/// mix a size_t for hashing
/// source: https://xorshift.di.unimi.it/splitmix64.c
[[nodiscard]] static inline size_t mix(size_t x) {
    x += 0x9e3779b97f4a7c15;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
    x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
    x ^= x >> 31;
    return x;
};

[[nodiscard]] static inline size_t transform(size_t a, size_t b) {
    // high entropy hash transform
    // https://stackoverflow.com/questions/35985960/c-why-is-boosthash-combine-the-best-way-to-combine-hash-values
    return a ^ (b + 0x9e3779b97f4a7c15uz + (a << 6) + (a >> 2));
}

#endif // !BEARC_COMPILER_UTILS_HPP
