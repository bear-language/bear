//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/layout.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include "type.hpp"
#include <utility>

namespace hir {

LayoutId layout_for_type(Context& context, TypeId tid) {
    auto vs = Ovld{
        [&context](const TypeBuiltin& t) -> Layout {
            switch (t.type) {
            case builtin_type::u8:
            case builtin_type::i8:
                return Layout::same_align_width(1);
            case builtin_type::u16:
            case builtin_type::i16:
                return Layout::same_align_width(2);
            case builtin_type::u32:
            case builtin_type::i32:
                return Layout::same_align_width(4);
            case builtin_type::u64:
            case builtin_type::i64:
                return Layout::same_align_width(8);
            case builtin_type::charr:
                return Layout::same_align_width(1);
            case builtin_type::f32:
                return Layout::same_align_width(4);
            case builtin_type::f64:
                return Layout::same_align_width(8);
            case builtin_type::voidd:
                return Layout::same_align_width(1); // hm
            case builtin_type::str:
                return Layout{.width = context.pointer_size_bytes() + context.register_size_bytes(),
                              .alignment = std::max(context.register_size_bytes(),
                                                    context.register_size_bytes())};
            case builtin_type::nullpointer:
                return Layout::same_align_width(context.pointer_size_bytes());
            case builtin_type::boolean:
                return Layout::same_align_width(1);
            }
            std::unreachable();
        },
        [&context](const TypeStruct& t) -> Layout {
            // TODO
        },
        [](const TypeVariant& t) -> Layout {
            // TODO
        },
        [](const TypeUnion& t) -> Layout {
            // TODO
        },
        [&context](const TypeDeftype& t) -> Layout {
            return context.layout(layout_for_type(context, t.true_type));
        },
        [](const TypeArr& t) -> Layout {},
        [&context](const TypeSlice&) -> Layout {
            return Layout{.width = context.pointer_size_bytes() + context.register_size_bytes(),
                          .alignment
                          = std::max(context.register_size_bytes(), context.register_size_bytes())};
        },
        [&context](const TypeRef&) -> Layout {
            return Layout::same_align_width(context.pointer_size_bytes());
        },
        [&context](const TypePtr&) -> Layout {
            return Layout::same_align_width(context.pointer_size_bytes());
        },
        [&context](const TypeFnPtr& t) -> Layout {
            return Layout::same_align_width(context.pointer_size_bytes());
        },
        [](const TypeVar&) -> Layout {
            assert(false
                   && "tried to get hir::Layout for an hir::TypeVar, which is an incomplete type");
            return Layout::same_align_width(1);
        },

    };

    // TODO store this in context for this type's canonical type
    return context.emplace_layout(context.type(tid).visit(vs));
}

LayoutId layout_for_canon_type(Context& context, TypeId tid) {
    // TODO
}

} // namespace hir
