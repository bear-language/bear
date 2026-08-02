//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/layout.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include "type.hpp"
#include <utility>

namespace hir {

LayoutId layout_for_type(Context& context, TypeId tid) {
    const Type& ty = context.type(tid);

    // check for memoized layout (which is recorded per-canonical-type)
    const auto maybe_existing = context.layout_for_canon_type(ty.canonical);
    if (maybe_existing.has_value()) {
        return maybe_existing.as_id();
    }
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
            const auto mems = context.ordered_defs_for(t.def_id);

            const auto align_up = +[](HirSize off, HirSize align) -> HirSize {
                // assumes align is a power of two, which it should be
                return (off + align - 1) & ~(align - 1);
            };

            HirSize offset = 0;
            HirSize struct_align = 1;

            for (auto didx = mems.begin(); didx != mems.end(); ++didx) {
                // all struct ordered members are variable definitions
                const auto& def = context.def(didx);
                const auto tid = def.as<DefVariable>().type_id;

                const LayoutId mem_lid = layout_for_type(context, tid);
                Layout mem_lay = context.layout(mem_lid);
                mem_lay.alignment
                    = std::max(mem_lay.alignment, static_cast<HirSize>(def.alignment_preference));

                // insert padding so this member starts at its required alignment
                offset = align_up(offset, mem_lay.alignment);
                offset += mem_lay.width;
                // TODO store offset info per-DefId in Context

                struct_align = std::max(struct_align, mem_lay.alignment);
            }

            // trailing padding since total size must be a multiple of struct alignment
            HirSize width = align_up(offset, struct_align);

            // width of 0 is not allowed (empty struct)
            if (width == 0) {
                width = 1;
            }

            return Layout{.width = width, .alignment = struct_align};
        },
        [&context](const TypeVariant& t) -> Layout {
            const auto fields = context.ordered_defs_for(t.def_id);

            const auto align_up = +[](HirSize off, HirSize align) -> HirSize {
                // assumes align is a power of two, which it should be
                return (off + align - 1) & ~(align - 1);
            };

            llvm::SmallVector<Layout> field_lays;
            for (auto field_didx = fields.begin(); field_didx != fields.end(); ++field_didx) {
                const auto mems = context.def(field_didx).as<DefVariantField>().members;
                HirSize offset = 0;
                HirSize field_align = 1;
                for (auto didx = mems.begin(); didx != mems.end(); ++didx) {
                    // all variant members are ordered members are variable definitions
                    const auto& def = context.def(didx);
                    const auto tid = def.as<DefVariable>().type_id;

                    const LayoutId mem_lid = layout_for_type(context, tid);
                    Layout mem_lay = context.layout(mem_lid);
                    mem_lay.alignment = std::max(mem_lay.alignment,
                                                 static_cast<HirSize>(def.alignment_preference));

                    // insert padding so this member starts at its required alignment
                    offset = align_up(offset, mem_lay.alignment);
                    offset += mem_lay.width;
                    // TODO store offset info per-DefId in Context

                    field_align = std::max(field_align, mem_lay.alignment);
                }

                HirSize width = align_up(offset, field_align);

                field_lays.push_back(Layout{.width = width, .alignment = field_align});
            }

            HirSize max_width = 0;
            HirSize max_align = 0;
            for (const auto lay : field_lays) {
                max_width = std::max(max_width, lay.width);
                max_align = std::max(max_align, lay.alignment);
            }

            Layout lay{.width = max_width, .alignment = max_align};

            // handle the discriminant and make sure it's aligned (matters particularly when the
            // width of the discriminant > payload)
            const HirSize discrim_bytes
                = context.def(t.def_id).as<DefVariant>().byte_count_for_discriminant();
            auto offset = align_up(lay.width,
                                   discrim_bytes); // discrim_bytes is the align of the discriminant
            lay.width += offset + discrim_bytes;
            lay.alignment = std::max(lay.alignment, discrim_bytes);

            return lay;
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

    const LayoutId lay_id = context.emplace_layout(ty.visit(vs));

    // memoize this guy
    context.put_layout_for_canon_type(ty.canonical, lay_id);

    return lay_id;
}

} // namespace hir
