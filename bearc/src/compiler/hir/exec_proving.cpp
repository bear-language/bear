//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/hashing.hpp"
#include <cstddef>

namespace hir {

bool equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2) {

    if (eid1 == eid2) {
        return true;
    }

    const Exec& other = ctx.exec(eid2);

    auto vs = Ovld{
        [](const ExecBlock& t) -> bool { return false; },
        [](const ExecExprStmt& t) -> bool { return false; },
        [](const ExecBreakStmt& t) -> bool { return false; },
        [](const ExecContinueStmt& t) -> bool { return false; },
        [](const ExecIfStmt& t) -> bool { return false; },
        [](const ExecLoopStmt& t) -> bool { return false; },
        [](const ExecReturnStmt& t) -> bool { return false; },
        [](const ExecYieldStmt& t) -> bool { return false; },
        [&other, &ctx](const ExecRange t) -> bool {
            if (!other.holds<ExecRange>()) {
                return false;
            }

            return equivalent_exec(ctx, t.start, other.as<ExecRange>().start)
                   && equivalent_exec(ctx, t.end, other.as<ExecRange>().end);
        },
        [&other, &ctx](const ExecUnionInit& t) -> bool {
            if (!other.holds<ExecUnionInit>()) {
                return false;
            }

            if (t.union_def_id != other.as<ExecUnionInit>().union_def_id) {
                return false;
            }

            if (t.active_member_idx != other.as<ExecUnionInit>().active_member_idx) {
                return false;
            }

            return equivalent_exec(ctx, t.member_init, other.as<ExecUnionInit>().member_init);
        },
        [&other, &ctx](const ExecExprVariantInit& t) -> bool {
            if (!other.holds<ExecExprVariantInit>()) {
                return false;
            }
            if (t.variant_def_id != other.as<ExecExprVariantInit>().variant_def_id) {
                return false;
            }

            if (t.active_member_idx != other.as<ExecExprVariantInit>().active_member_idx) {
                return false;
            }

            return equivalent_exec(ctx, t.payload_init,
                                   other.as<ExecExprVariantInit>().payload_init);
        },
        [&other, &ctx](const ExecExprStructInit& t) -> bool {
            if (!other.holds<ExecExprStructInit>()) {
                return false;
            }

            const auto o = other.as<ExecExprStructInit>();

            if (t.struct_def_id != o.struct_def_id) {
                return false;
            }

            // malformed guard
            if (t.member_inits.len() != o.member_inits.len()) {
                return false;
            }

            // compare each member init sequentially and just ret false if a single one disagrees
            for (HirSize i = 0; i < o.member_inits.len(); ++i) {
                if (!equivalent_exec(ctx, ctx.exec_id(o.member_inits.get(i)),
                                     ctx.exec_id(t.member_inits.get(i)))) {
                    return false;
                }
            }

            return true;
        },
        [&other, &ctx](const ExecExprStructMemberInit& t) -> bool {
            if (!other.holds<ExecExprStructMemberInit>()) {
                return false;
            }

            const auto o = other.as<ExecExprStructMemberInit>();

            if (t.field_def != o.field_def) {
                return false;
            }

            return equivalent_exec(ctx, t.value, o.value);
        },
        [](const ExecAssignable& t) -> bool { return false; },
        [&other](const ExecComptConstant& t) -> bool {
            if (!other.holds<ExecComptConstant>()) {
                return false;
            }
            const auto o = other.as<ExecComptConstant>();
            if (o.hash_identity() != t.hash_identity()) {
                return false;
            }
            return t.to_size() == o.to_size();
        },
        [&other, &ctx](const ExecListLiteral& t) -> bool {
            if (!other.holds<ExecListLiteral>()) {
                return false;
            }

            const auto o = other.as<ExecListLiteral>();

            if (o.len() != t.len()) {
                return false;
            }

            if (o.elem_type_id.has_value() && t.elem_type_id.has_value()
                && !ctx.equivalent_type(o.elem_type_id.as_id(), t.elem_type_id.as_id())) {
                return false;
            }

            for (HirSize i = 0; i < o.len(); ++i) {
                if (equivalent_exec(ctx, ctx.exec_id(o.elems.get(i)),
                                    ctx.exec_id(t.elems.get(i)))) {
                    return false;
                }
            }
            return true;
        },
        [&other, &ctx](const ExecVariantFieldInit& t) -> bool {
            if (!other.holds<ExecVariantFieldInit>()) {
                return false;
            }

            const auto o = other.as<ExecVariantFieldInit>();

            if (t.variant_field_def_id != o.variant_field_def_id) {
                return false;
            }

            // malformed guard
            if (t.member_inits.len() != o.member_inits.len()) {
                return false;
            }

            // compare each member init sequentially and just ret false if a single one disagrees
            for (HirSize i = 0; i < o.member_inits.len(); ++i) {
                if (!equivalent_exec(ctx, ctx.exec_id(o.member_inits.get(i)),
                                     ctx.exec_id(t.member_inits.get(i)))) {
                    return false;
                }
            }

            return true;
        },
        [&other, &ctx](const ExecFnPtr& t) -> bool {
            if (!other.holds<ExecFnPtr>()) {
                return false;
            }
            if (other.as<ExecFnPtr>().fn_ptr_tid.has_value() != t.fn_ptr_tid.has_value()) {
                return false;
            }
            // both don't have tids
            if (t.fn_ptr_tid.empty()) {
                return true;
            }
            return ctx.equivalent_type(t.fn_ptr_tid.as_id(),
                                       other.as<ExecFnPtr>().fn_ptr_tid.as_id());
        },
        [](const ExecAssignment& t) -> bool { return false; },
        [](const ExecIs& t) -> bool { return false; },
        [](const ExecMemberAccess& t) -> bool { return false; },
        [](const ExecBinary& t) -> bool { return false; },
        [](const ExecCast& t) -> bool { return false; },
        [](const ExecPreUnary& t) -> bool { return false; },
        [](const ExecPostUnary& t) -> bool { return false; },
        [](const ExecSubscript& t) -> bool { return false; },
        [](const ExecFnCall& t) -> bool { return false; },
        [](const ExecBorrow& t) -> bool { return false; },
        [](const ExecDeref& t) -> bool { return false; },
        [](const ExecExprClosure& t) -> bool { return false; },
        [](const ExecExprVariantDecomp& t) -> bool { return false; },
        [](const ExecExprMatch& t) -> bool { return false; },
        [](const ExecExprMatchBranch& t) -> bool { return false; },
    };

    return ctx.exec(eid1).visit(vs);
}

bool possibly_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2) {
    const Exec& e1 = ctx.exec(eid1);
    const Exec& e2 = ctx.exec(eid2);

    auto vs = Ovld{
        [](const ExecBlock& t) -> bool { return false; },
        [](const ExecExprStmt& t) -> bool { return false; },
        [](const ExecBreakStmt& t) -> bool { return false; },
        [](const ExecContinueStmt& t) -> bool { return false; },
        [](const ExecIfStmt& t) -> bool { return false; },
        [](const ExecLoopStmt& t) -> bool { return false; },
        [](const ExecReturnStmt& t) -> bool { return false; },
        [](const ExecYieldStmt& t) -> bool { return false; },
        [&e2, &ctx, eid2](const ExecRange t) -> bool {
            if (e2.holds<ExecConst>()) {
                return possibly_equivalent_exec(ctx, t.start, eid2);
            }
            if (!e2.holds<ExecRange>()) {
                return false;
            }

            return possibly_equivalent_exec(ctx, t.start, e2.as<ExecRange>().start)
                   && possibly_equivalent_exec(ctx, t.end, e2.as<ExecRange>().end);
        },
        [&e2](const ExecUnionInit& t) -> bool {
            if (!e2.holds<ExecUnionInit>()) {
                return false;
            }

            if (t.union_def_id != e2.as<ExecUnionInit>().union_def_id) {
                return false;
            }

            if (t.active_member_idx != e2.as<ExecUnionInit>().active_member_idx) {
                return false;
            }
            return false;
        },
        [&e2](const ExecExprVariantInit& t) -> bool {
            if (!e2.holds<ExecExprVariantInit>()) {
                return false;
            }
            if (t.variant_def_id != e2.as<ExecExprVariantInit>().variant_def_id) {
                return false;
            }

            if (t.active_member_idx != e2.as<ExecExprVariantInit>().active_member_idx) {
                return false;
            }

            return true;
        },
        [&e2](const ExecExprStructInit& t) -> bool {
            if (!e2.holds<ExecExprStructInit>()) {
                return false;
            }

            const auto o = e2.as<ExecExprStructInit>();

            return t.struct_def_id == o.struct_def_id;
        },
        [&e2](const ExecExprStructMemberInit& t) -> bool {
            if (!e2.holds<ExecExprStructMemberInit>()) {
                return false;
            }

            const auto o = e2.as<ExecExprStructMemberInit>();

            return (t.field_def == o.field_def);
        },
        [](const ExecAssignable& t) -> bool { return false; },
        [&e2, &ctx, eid1](const ExecComptConstant& t) -> bool {
            if (e2.holds<ExecRange>()) {
                return possibly_equivalent_exec(ctx, eid1, e2.as<ExecRange>().start);
            }
            if (!e2.holds<ExecComptConstant>()) {
                return false;
            }
            const auto o = e2.as<ExecComptConstant>();
            return ((o.is_signed_integral() && o.is_signed_integral())
                    || o.hash_identity() == t.hash_identity());
        },
        [&e2, &ctx](const ExecListLiteral& t) -> bool {
            if (!e2.holds<ExecListLiteral>()) {
                return false;
            }

            const auto o = e2.as<ExecListLiteral>();

            // this means one is empty (thus being typeless as far as we are concerned, so it can be
            // equivalent to any list lit)
            if (o.elem_type_id.has_value() != t.elem_type_id.has_value()) {
                return true;
            }

            if (o.elem_type_id.has_value() && t.elem_type_id.has_value()
                && !ctx.equivalent_type(o.elem_type_id.as_id(), t.elem_type_id.as_id())) {
                return false;
            }

            return true;
        },
        [&e2](const ExecVariantFieldInit& t) -> bool {
            if (!e2.holds<ExecVariantFieldInit>()) {
                return false;
            }

            const auto o = e2.as<ExecVariantFieldInit>();

            return t.variant_field_def_id == o.variant_field_def_id;
        },
        [](const ExecAssignment& t) -> bool { return false; },
        [](const ExecIs& t) -> bool { return false; },
        [](const ExecMemberAccess& t) -> bool { return false; },
        [](const ExecBinary& t) -> bool { return false; },
        [](const ExecCast& t) -> bool { return false; },
        [](const ExecPreUnary& t) -> bool { return false; },
        [](const ExecPostUnary& t) -> bool { return false; },
        [](const ExecSubscript& t) -> bool { return false; },
        [](const ExecFnCall& t) -> bool { return false; },
        [](const ExecBorrow& t) -> bool { return false; },
        [](const ExecDeref& t) -> bool { return false; },
        [](const ExecExprClosure& t) -> bool { return false; },
        [](const ExecExprVariantDecomp& t) -> bool { return false; },
        [](const ExecExprMatch& t) -> bool { return false; },
        [](const ExecExprMatchBranch& t) -> bool { return false; },
        [](const ExecFnPtr& t) -> bool { return false; },
    };

    return e1.visit(vs);
}

size_t hash_exec(const Context& ctx, ExecId eid) {
    auto vs = Ovld{
        [](const ExecBlock& t) -> size_t { return mix(1uz); },
        [](const ExecExprStmt& t) -> size_t { return mix(2uz); },
        [](const ExecBreakStmt& t) -> size_t { return mix(3uz); },
        [](const ExecContinueStmt& t) -> size_t { return mix(4uz); },
        [](const ExecIfStmt& t) -> size_t { return mix(5uz); },
        [](const ExecLoopStmt& t) -> size_t { return mix(6uz); },
        [](const ExecReturnStmt& t) -> size_t { return mix(7uz); },
        [&ctx](const ExecRange& t) -> size_t {
            return transform(hash_exec(ctx, t.start), hash_exec(ctx, t.end));
        },
        [&ctx](const ExecYieldStmt& t) -> size_t {
            return mix(8uz ^ t.yield_value.has_value() ? hash_exec(ctx, t.yield_value.as_id()) : 0);
        },
        [&ctx](const ExecUnionInit& t) -> size_t {
            return mix(t.union_def_id.raw() ^ hash_exec(ctx, t.member_init) ^ t.active_member_idx);
        },
        [&ctx](const ExecExprVariantInit& t) -> size_t {
            return mix(t.variant_def_id.raw() ^ hash_exec(ctx, t.payload_init)
                       ^ t.active_member_idx);
        },
        [&ctx](const ExecExprStructInit& t) -> size_t {
            size_t h = t.struct_def_id.raw();
            for (auto eidx = t.member_inits.begin(); eidx != t.member_inits.end(); ++eidx) {
                h = transform(h, hash_exec(ctx, ctx.exec_id(eidx)));
            }
            return h;
        },
        [&ctx](const ExecExprStructMemberInit& t) -> size_t {
            return transform(t.field_def.raw(), hash_exec(ctx, (t.value)));
        },
        [](const ExecAssignable& t) -> size_t {
            return transform(t.def_id.raw(), t.type_id.raw());
        },
        [](const ExecComptConstant& t) -> size_t {
            return transform(t.hash_identity(), t.to_size());
        },
        [&ctx](const ExecListLiteral& t) -> size_t {
            size_t h = t.elem_type_id.raw();
            h = transform(h, t.len());
            for (auto eidx = t.elems.begin(); eidx != t.elems.end(); ++eidx) {
                h = transform(h, hash_exec(ctx, ctx.exec_id(eidx)));
            }
            return h;
        },
        [](const ExecAssignment& t) -> size_t { return {}; },
        [](const ExecIs& t) -> size_t { return {}; },
        [](const ExecMemberAccess& t) -> size_t { return {}; },
        [](const ExecBinary& t) -> size_t { return {}; },
        [](const ExecCast& t) -> size_t { return {}; },
        [](const ExecPreUnary& t) -> size_t { return {}; },
        [](const ExecPostUnary& t) -> size_t { return {}; },
        [](const ExecSubscript& t) -> size_t { return {}; },
        [](const ExecFnCall& t) -> size_t { return {}; },
        [](const ExecBorrow& t) -> size_t { return {}; },
        [](const ExecDeref& t) -> size_t { return {}; },
        [](const ExecExprClosure& t) -> size_t {
            // todo, add when impl'd
            return {};
        },
        [](const ExecExprVariantDecomp& t) -> size_t { return {}; },
        [](const ExecExprMatch& t) -> size_t { return {}; },
        [](const ExecExprMatchBranch& t) -> size_t { return {}; },
        [](const ExecFnPtr& t) -> size_t { return mix(t.func_def_id.raw()); },
        [&ctx](const ExecVariantFieldInit& t) -> size_t {
            size_t h = t.variant_field_def_id.raw();
            for (auto eidx = t.member_inits.begin(); eidx != t.member_inits.end(); ++eidx) {
                h = transform(h, hash_exec(ctx, ctx.exec_id(eidx)));
            }
            return h;
        },
    };

    return ctx.exec(eid).visit(vs);
}

} // namespace hir
