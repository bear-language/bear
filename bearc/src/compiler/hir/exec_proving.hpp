//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_EXEC_PROVING_HPP
#define BEARC_COMPILER_HIR_EXEC_PROVING_HPP

#include "compiler/hir/context.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/indexing.hpp"
#include <cstdint>
#include <limits>
#include <optional>
namespace hir {

inline constexpr uint64_t INTEGRAL_KEY_SIGN_BIT = uint64_t{1} << 63;

inline std::optional<uint64_t> try_integral_key(ExecConst c, bool is_signed) {
    if (!c.is_integral()) {
        return {};
    }
    if (is_signed) {
        int64_t v{};
        if (c.is_signed_integral()) {
            v = c.to_i64();
        } else {
            const uint64_t u = c.to_size();
            if (u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return {};
            }
            v = static_cast<int64_t>(u);
        }
        return static_cast<uint64_t>(v) ^ INTEGRAL_KEY_SIGN_BIT;
    }
    if (c.is_signed_integral()) {
        const int64_t v = c.to_i64();
        if (v < 0) {
            return {};
        }
        return static_cast<uint64_t>(v);
    }
    return c.to_size();
}

struct CanonicalIntegralRange {
    uint64_t start;
    uint64_t last;
    bool empty;
    bool is_signed;

    friend bool operator==(const CanonicalIntegralRange&, const CanonicalIntegralRange&) = default;
};

std::optional<CanonicalIntegralRange> try_canonical_integral_range(const Context& ctx,
                                                                   const ExecRange& range);


/// checks whether two Execs are equivalent
/// - fully checks compt values
/// - returns true only if the two Execs are certainly equal (since their values are fully knowable
/// at compile-time), or if run-time execs are structurally equivalent.
/// - implcitly considers compt values like u8 and u64 that has values representable in both forms
/// as equivalent
///
/// note: this is to say that the execs may not be equivalent at different times at run-time, but
/// are equivalent as far as the compiler is concerned. keep this in mind when comparing execs that
/// may refer to values that are mutable and mutation was possible between the order of the execs.
/// for this reason, it would be best to either use this with 1) pure compile-time value or 2) execs
/// on either side of an expression, like notably some boolean expression: `exec || exec`, etc.
bool implicit_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2);

bool implicit_equivalent_exec_slice(const Context& ctx, IdSlice<ExecId> s1, IdSlice<ExecId> s2);

bool explicitly_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2);

bool equivalent_exec_slice(const Context& ctx, IdSlice<ExecId> s1, IdSlice<ExecId> s2);

/// considers things like u8 and u64 values that are representable as both as equivalent
size_t hash_exec_with_implicit_equivalence(const Context& ctx, ExecId eid);

size_t hash_exec_with_explicit_equivalence(const Context& ctx, ExecId eid);

/// if two execs could ever be equivalent (cheap type matching)
bool possibly_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2);

/// checks if some Exec definitely returns (in all control flow paths)
bool definitely_returns(const Context& ctx, ExecId eid);

bool definitely_yields(const Context& ctx, ExecId eid);

} // namespace hir

#endif // !BEARC_COMPILER_HIR_EXEC_PROVING_HPP
