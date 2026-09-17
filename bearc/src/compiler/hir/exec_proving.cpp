//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/hashing.hpp"
#include <cstddef>

namespace hir {

bool equivalent_exec_slice(const Context& ctx, IdSlice<ExecId> s1, IdSlice<ExecId> s2) {
    if (s1.len() != s2.len()) {
        return false;
    }
    for (auto i = 0u; i < s1.len(); ++i) {
        if (!equivalent_exec(ctx, ctx.exec_id(s1.get(i)), ctx.exec_id(s2.get(i)))) {
            return false;
        }
    }
    return true;
}

bool equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2) {

    if (eid1 == eid2) {
        return true;
    }

    const Exec& other = ctx.exec(eid2);

    auto vs = Ovld{
        [&other, &ctx](const ExecBlock& t) -> bool {
            if (!other.holds<ExecBlock>()) {
                return false;
            };
            const Block& this_block = ctx.block(t.block_id);
            const Block& other_block = ctx.block(other.as<ExecBlock>().block_id);
            return equivalent_exec_slice(ctx, this_block.execs, other_block.execs);
        },
        [&other, &ctx](const ExecBranch& t) -> bool {
            if (!other.holds<ExecBranch>()) {
                return false;
            };
            const auto o = other.as<ExecBranch>();
            return equivalent_exec(ctx, t.condition, o.condition)
                   && equivalent_exec(ctx, t.then_block, o.then_block)
                   && equivalent_exec(ctx, t.else_block, o.else_block);
        },
        [&other, &ctx](const ExecJump& t) -> bool {
            if (!other.holds<ExecJump>()) {
                return false;
            };
            const auto o = other.as<ExecJump>();
            return t.spot == o.spot && equivalent_exec(ctx, t.block, o.block);
        },
        [&other, &ctx](const ExecReturn& t) -> bool {
            if (!other.holds<ExecReturn>()) {
                return false;
            };
            const auto o = other.as<ExecReturn>();
            if (t.return_value != o.return_value) {
                return false;
            }
            if (!o.return_value && !t.return_value) {
                return true;
            }
            return equivalent_exec(ctx, t.return_value.as_id(), o.return_value.as_id());
        },
        [&ctx, &other](const ExecYield& t) -> bool {
            if (!other.holds<ExecYield>()) {
                return false;
            };
            const auto o = other.as<ExecYield>();
            if (t.yield_value != o.yield_value) {
                return false;
            }
            if (!o.yield_value && !t.yield_value) {
                return true;
            }
            return equivalent_exec(ctx, t.yield_value.as_id(), o.yield_value.as_id());
        },
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
        [&other, &ctx](const ExecVariantInit& t) -> bool {
            if (!other.holds<ExecVariantInit>()) {
                return false;
            }
            if (t.variant_def_id != other.as<ExecVariantInit>().variant_def_id) {
                return false;
            }

            if (t.active_member_idx != other.as<ExecVariantInit>().active_member_idx) {
                return false;
            }

            return equivalent_exec(ctx, t.payload_init, other.as<ExecVariantInit>().payload_init);
        },
        [&other, &ctx](const ExecStructInit& t) -> bool {
            if (!other.holds<ExecStructInit>()) {
                return false;
            }

            const auto o = other.as<ExecStructInit>();

            if (t.anonymous != o.anonymous) {
                return false;
            }

            if (!t.anonymous && t.struct_def_id != o.struct_def_id) {
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
        [&other](const ExecAssignable& t) -> bool {
            if (!other.holds<ExecAssignable>()) {
                return false;
            }
            const auto o = other.as<ExecAssignable>();

            return t.def_id == o.def_id;
        },
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
        [&ctx, &other](const ExecAssignment& t) -> bool {
            if (!other.holds<ExecAssignment>()) {
                return false;
            }
            const auto o = other.as<ExecAssignment>();
            return equivalent_exec(ctx, t.lhs, o.lhs) && equivalent_exec(ctx, t.rhs, o.rhs);
        },
        [&ctx, &other](const ExecMemberAccess& t) -> bool {
            if (!other.holds<ExecMemberAccess>()) {
                return false;
            }
            const auto o = other.as<ExecMemberAccess>();
            return o.member == t.member && equivalent_exec(ctx, t.owner, o.owner);
        },
        [&ctx, &other](const ExecBinary& t) -> bool {
            if (!other.holds<ExecBinary>()) {
                return false;
            }
            const auto o = other.as<ExecBinary>();
            return t.op == o.op && equivalent_exec(ctx, t.lhs, o.lhs)
                   && equivalent_exec(ctx, o.rhs, t.rhs);
        },
        [&ctx, &other](const ExecCast& t) -> bool {
            if (!other.holds<ExecCast>()) {
                return false;
            }
            const auto o = other.as<ExecCast>();
            return ctx.equivalent_type(o.target_tid, t.target_tid)
                   && equivalent_exec(ctx, o.exec, t.exec);
        },
        [&ctx, &other](const ExecSubscript& t) -> bool {
            if (!other.holds<ExecSubscript>()) {
                return false;
            }
            const auto o = other.as<ExecSubscript>();
            return equivalent_exec(ctx, o.base, t.base) && equivalent_exec(ctx, t.base, o.base);
        },
        [&ctx, &other](const ExecFnCall& t) -> bool {
            if (!other.holds<ExecFnCall>()) {
                return false;
            }
            const auto o = other.as<ExecFnCall>();
            return equivalent_exec(ctx, o.callee, t.callee)
                   && equivalent_exec_slice(ctx, o.args, t.args);
        },
        [&other](const ExecBorrow& t) -> bool {
            if (!other.holds<ExecBorrow>()) {
                return false;
            }
            const auto o = other.as<ExecBorrow>();
            return o.borrowee == t.borrowee;
        },
        [&ctx, &other](const ExecDeref& t) -> bool {
            if (!other.holds<ExecDeref>()) {
                return false;
            }
            const auto o = other.as<ExecDeref>();
            return equivalent_exec(ctx, o.dereferenced, t.dereferenced);
        },
        [&other](const ExecAddrOf& t) -> bool {
            if (!other.holds<ExecAddrOf>()) {
                return false;
            }
            const auto o = other.as<ExecAddrOf>();
            return t.addressed == o.addressed;
        },
        [&ctx, &other](const ExecMatch& t) -> bool {
            if (!other.holds<ExecMatch>()) {
                return false;
            }
            const auto o = other.as<ExecMatch>();
            return equivalent_exec(ctx, t.scrutinee, o.scrutinee)
                   && equivalent_exec_slice(ctx, t.branches, o.branches);
        },
        [&ctx, &other](const ExecMatchBranch& t) -> bool {
            if (!other.holds<ExecMatchBranch>()) {
                return false;
            }
            const auto o = other.as<ExecMatchBranch>();
            return equivalent_exec_slice(ctx, t.patterns, o.patterns)
                   && equivalent_exec(ctx, t.body, o.body);
        },
    };

    return ctx.exec(eid1).visit(vs);
}

size_t hash_exec(const Context& ctx, ExecId eid) {
    // TODO make this good for run-time execs
    auto vs = Ovld{
        [](const ExecBlock&) -> size_t { return mix(1uz); },
        [](const ExecJump&) -> size_t { return mix(3uz); },
        [](const ExecBranch&) -> size_t { return mix(5uz); },
        [](const ExecReturn&) -> size_t { return mix(7uz); },
        [&ctx](const ExecRange& t) -> size_t {
            return transform(hash_exec(ctx, t.start), hash_exec(ctx, t.end));
        },
        [&ctx](const ExecYield& t) -> size_t {
            return mix(8uz ^ t.yield_value.has_value() ? hash_exec(ctx, t.yield_value.as_id()) : 0);
        },
        [&ctx](const ExecUnionInit& t) -> size_t {
            return mix(t.union_def_id.raw() ^ hash_exec(ctx, t.member_init) ^ t.active_member_idx);
        },
        [&ctx](const ExecVariantInit& t) -> size_t {
            return mix(t.variant_def_id.raw() ^ hash_exec(ctx, t.payload_init)
                       ^ t.active_member_idx);
        },
        [&ctx](const ExecStructInit& t) -> size_t {
            size_t h = (t.anonymous) ? 0 : t.struct_def_id.raw();
            for (auto eidx = t.member_inits.begin(); eidx != t.member_inits.end(); ++eidx) {
                h = transform(h, hash_exec(ctx, ctx.exec_id(eidx)));
            }
            return h;
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
        [](const ExecAssignment&) -> size_t { return {}; },
        [](const ExecMemberAccess&) -> size_t { return {}; },
        [](const ExecBinary&) -> size_t { return {}; },
        [](const ExecCast&) -> size_t { return {}; },
        [](const ExecSubscript&) -> size_t { return {}; },
        [](const ExecFnCall&) -> size_t { return {}; },
        [](const ExecBorrow&) -> size_t { return {}; },
        [](const ExecAddrOf&) -> size_t { return {}; },
        [](const ExecDeref&) -> size_t { return {}; },
        [](const ExecMatch&) -> size_t { return {}; },
        [](const ExecMatchBranch&) -> size_t { return {}; },
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

bool possibly_equivalent_exec(const Context& ctx, ExecId eid1, ExecId eid2) {

    if (eid1 == eid2) {
        return true;
    }

    const Exec& e1 = ctx.exec(eid1);
    const Exec& e2 = ctx.exec(eid2);

    auto vs = Ovld{
        [](const ExecBlock&) -> bool { return false; },
        [](const ExecJump&) -> bool { return false; },
        [](const ExecBranch&) -> bool { return false; },
        [](const ExecReturn&) -> bool { return false; },
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
        [&e2](const ExecVariantInit& t) -> bool {
            if (!e2.holds<ExecVariantInit>()) {
                return false;
            }
            if (t.variant_def_id != e2.as<ExecVariantInit>().variant_def_id) {
                return false;
            }

            if (t.active_member_idx != e2.as<ExecVariantInit>().active_member_idx) {
                return false;
            }

            return true;
        },
        [&e2](const ExecStructInit& t) -> bool {
            if (!e2.holds<ExecStructInit>()) {
                return false;
            }

            const auto o = e2.as<ExecStructInit>();

            return t.struct_def_id == o.struct_def_id;
        },
        [](const ExecAssignable&) -> bool { return false; },
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
        [](const ExecAssignment&) -> bool { return false; },
        [](const ExecMemberAccess&) -> bool { return false; },
        [](const ExecBinary&) -> bool { return false; },
        [](const ExecCast&) -> bool { return false; },
        [](const ExecSubscript&) -> bool { return false; },
        [](const ExecFnCall&) -> bool { return false; },
        [](const ExecYield&) -> bool { return false; },
        [](const ExecBorrow&) -> bool { return false; },
        [](const ExecAddrOf&) -> bool { return false; },
        [](const ExecDeref&) -> bool { return false; },
        [](const ExecMatch&) -> bool { return false; },
        [](const ExecMatchBranch&) -> bool { return false; },
        [](const ExecFnPtr&) -> bool { return false; },
    };

    return e1.visit(vs);
}

} // namespace hir
