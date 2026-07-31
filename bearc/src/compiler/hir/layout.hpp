//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_LAYOUT_HPP
#define BEARC_COMPILER_HIR_LAYOUT_HPP

#include "compiler/hir/indexing.hpp"
namespace hir {

using LayoutSize = HirSize;

struct Layout {
    LayoutSize width;
    LayoutSize alignment;
};

} // namespace hir

#endif
