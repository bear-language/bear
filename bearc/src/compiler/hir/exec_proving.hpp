//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_EXEC_PROVING_HPP
#define BEARC_COMPILER_HIR_EXEC_PROVING_HPP

#include "compiler/hir/context.hpp"
#include "compiler/hir/indexing.hpp"
namespace hir {

/// checks whether two Execs are equivalent
/// - fully checks compt values
/// - returns true only if the two Execs are certainly equal (since their values are fully knowable
/// at compile-time), else false
bool equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2);

/// checks whether two Execs could be equivalent
/// - note: this might be useless (in its current state)
bool possibly_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2);

size_t hash_exec(const Context& ctx, ExecId eid);

} // namespace hir

#endif // !BEARC_COMPILER_HIR_EXEC_PROVING_HPP
