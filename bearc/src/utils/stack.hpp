//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef UTILS_STACK_HPP
#define UTILS_STACK_HPP

#include "llvm/Support/thread.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

namespace utils {

/// native stack size for threads doing compiler work
///
/// - compt evaluation recurses on the native stack and debug + UBSan frames are large (~18KB per
/// compt fn call), so neither the 8MB main thread default nor the 512KB secondary thread default on
/// macOS is enough
inline constexpr unsigned COMPILER_THREAD_STACK_BYTES = 64u * 1024u * 1024u;

// llvm::thread needs the stack size as an optional
inline constexpr std::optional<unsigned> COMPILER_THREAD_STACK{COMPILER_THREAD_STACK_BYTES};

/// run `f` on a new thread with a COMPILER_THREAD_STACK_BYTES stack and wait for it to finish
template <typename F> void run_with_compiler_stack(F&& f) {
    llvm::thread t{COMPILER_THREAD_STACK, std::forward<F>(f)};
    t.join();
}

/// approximate num bytes left on the current thread's native stack (stacks grow down on
/// all supported targets).
///
/// - note: returns SIZE_MAX if the stack bounds can't be determined
///
/// - used to guard deep recursion (e.g. compt fn evaluation) whose per-frame cost varies a lot
/// between debug/sanitized and optimized builds, so a fixed frame count alone isn't safe
[[nodiscard]] inline std::size_t remaining_stack_bytes() {
    thread_local std::uintptr_t stack_low = 0; // lowest usable address, 0 => unknown/uncomputed
    thread_local bool computed = false;

    if (!computed) {
        computed = true;
#ifdef __APPLE__
        pthread_t self = pthread_self();
        auto high = reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(self));
        std::size_t size = pthread_get_stacksize_np(self);
        stack_low = high - size;
#elifdef __linux__
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) == 0) {
            void* addr = nullptr;
            std::size_t size = 0;
            if (pthread_attr_getstack(&attr, &addr, &size) == 0) {
                stack_low = reinterpret_cast<std::uintptr_t>(addr);
            }
            pthread_attr_destroy(&attr);
        }
#endif
    }

    if (stack_low == 0) {
        return std::numeric_limits<std::size_t>::max();
    }

    const auto sp = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    return sp > stack_low ? sp - stack_low : 0;
}

} // namespace utils

#endif
