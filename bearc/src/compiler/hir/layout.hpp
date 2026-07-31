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
    using id_type = LayoutId;
    // width, in bytes
    LayoutSize width;
    // alignment, in bytes
    LayoutSize alignment;

    [[nodiscard]] static Layout same_align_width(HirSize bytes) {
        return Layout{.width = bytes, .alignment = bytes};
    }
};

class Context;

LayoutId layout_for_type(Context& context, TypeId tid);

LayoutId layout_for_canon_type(Context& context, TypeId tid);

} // namespace hir

#endif
