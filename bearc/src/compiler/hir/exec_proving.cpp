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
        [&other, &ctx](const ExecExprUnionInit& t) -> bool {
            if (!other.holds<ExecExprUnionInit>()) {
                return false;
            }

            if (t.union_def_id != other.as<ExecExprUnionInit>().union_def_id) {
                return false;
            }

            if (t.active_member_idx != other.as<ExecExprUnionInit>().active_member_idx) {
                return false;
            }

            return equivalent_exec(ctx, t.member_init, other.as<ExecExprUnionInit>().member_init);
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
        [](const ExecExprVariable& t) -> bool { return false; },
        [&other](const ExecExprComptConstant& t) -> bool {
            if (!other.holds<ExecExprComptConstant>()) {
                return false;
            }
            const auto o = other.as<ExecExprComptConstant>();
            if (!o.holds_same_variant_type(t)) {
                return false;
            }
            const auto res = ExecConst::equal(t, o);
            return res.has_value() && res.value().as<bool>();
        },
        [&other, &ctx](const ExecExprListLiteral& t) -> bool {
            if (!other.holds<ExecExprListLiteral>()) {
                return false;
            }

            const auto o = other.as<ExecExprListLiteral>();

            if (o.len() != t.len()) {
                return false;
            }

            // make sure both either have an elem type or both don't
            if (o.elem_type_id.has_value() != t.elem_type_id.has_value()) {
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
        [](const ExecExprAssignMove& t) -> bool { return false; },
        [](const ExecExprAssignEqual& t) -> bool { return false; },
        [](const ExecExprIs& t) -> bool { return false; },
        [](const ExecExprMemberAccess& t) -> bool { return false; },
        [](const ExecExprPointerMemberAccess& t) -> bool { return false; },
        [](const ExecExprBinary& t) -> bool { return false; },
        [](const ExecExprCast& t) -> bool { return false; },
        [](const ExecExprPreUnary& t) -> bool { return false; },
        [](const ExecExprPostUnary& t) -> bool { return false; },
        [](const ExecExprSubscript& t) -> bool { return false; },
        [](const ExecExprFnCall& t) -> bool { return false; },
        [](const ExecExprBorrow& t) -> bool { return false; },
        [](const ExecExprDeref& t) -> bool { return false; },
        [](const ExecExprClosure& t) -> bool { return false; },
        [](const ExecExprVariantDecomp& t) -> bool { return false; },
        [](const ExecExprMatch& t) -> bool { return false; },
        [](const ExecExprMatchBranch& t) -> bool { return false; },
        [](const ExecFnPtr& t) -> bool { return false; },
    };

    return ctx.exec(eid1).visit(vs);
}

bool possibly_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2) {
    return equivalent_exec(ctx, eid1, eid2)
           || ctx.exec(eid1).holds_same_variant_type(ctx.exec(eid2));
}

} // namespace hir
