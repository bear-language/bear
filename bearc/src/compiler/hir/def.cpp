//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/def.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/indexing.hpp"

namespace hir {
static std::string def_id_str(DefId did) { return "Def#" + std::to_string(did.raw()); }
std::string def_to_string(Context& ctx, ExecId eid) {
    // TODO
}
} // namespace hir
