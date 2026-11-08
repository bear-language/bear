//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef BEARC_COMPILER_HIR_MATCHING_HPP
#define BEARC_COMPILER_HIR_MATCHING_HPP

#include "compiler/ast/expr.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/expr_solver.hpp"
#include "compiler/hir/id_set.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/data_arena.hpp"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace hir {

/// validates the branches of a match expression for a variant, and emplaces any needed diagnostics
/// - checks for exhaustiveness
/// - checks that each branch is indeed a valid variant field for the provided variant
/// - checks for duplicates
/// - does NOT validate/evaluate individual variant decompositions
/// returns false when match is NOT exhaustive or is not valid due to some other more malformed
/// reason
template <IsExprSolver S>
bool valid_exhaustive_match_for_variant(S& solver, ScopeId containing_scope,
                                        ScopeId matched_variant_scope, FileId fid,
                                        DefId variant_did, const ast_expr_t* match_expr) {

    assert(match_expr->type == AST_EXPR_MATCH);

    Context& context = solver.get_context();

    DataArena arena(0x200); // resonably sized

    Def variant_def = context.def(variant_did);

    assert(variant_def.holds<DefVariant>());

    IdSet<DefId> used_variant_fields{arena, 2 * variant_def.as<DefVariant>().ordered_members.len()};

    const ast_slice_of_exprs_t branches = match_expr->expr.match_expr.branches;

    DiagLinker dl{context};

    const ast_expr_t* else_pattern = nullptr;

    bool valid = true;

    for (size_t i = 0; i < branches.len; ++i) {

        const ast_expr_t* branch = branches.start[i];

        assert(branch->type == AST_EXPR_MATCH_BRANCH);

        const ast_slice_of_exprs_t patterns = branch->expr.match_branch.patterns;

        for (size_t j = 0; j < patterns.len; ++j) {

            const ast_expr_t* pattern = patterns.start[j];

            if (pattern->type == AST_EXPR_ELSE_MATCH_PATTERN) {
                if (else_pattern) {
                    dl.link(context.emplace_diagnostic(Span{context, fid, pattern},
                                                       diag_code::duplicate_else_match_branch,
                                                       diag_type::error));
                    dl.link(context.emplace_diagnostic(Span{context, fid, else_pattern},
                                                       diag_code::previous_def_here,
                                                       diag_type::note));
                    valid = false;
                }
                else_pattern = pattern;
                if (i != branches.len - 1) {
                    dl.link(context.emplace_diagnostic(
                        Span{context, fid, pattern}, diag_code::else_match_branch_should_come_last,
                        diag_type::warning));
                }
                continue;
            }
            std::optional<IdSlice<SymbolId>> maybe_sid_slice{};
            Span id_span{context, fid, pattern};
            if (pattern->type == AST_EXPR_ID) {
                maybe_sid_slice = context.symbol_slice(pattern->expr.id.slice);
            } else if (pattern->type == AST_EXPR_VARIANT_DECOMP) {
                maybe_sid_slice = context.symbol_slice(pattern->expr.variant_decomp.id);
                id_span = Span{context, fid, pattern->expr.variant_decomp.id};
            }
            if (!maybe_sid_slice.has_value()) {
                dl.link(context.emplace_diagnostic(
                    id_span, diag_code::invalid_pattern, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field}));

                valid = false;
                continue;
            }
            const auto sid_slice = maybe_sid_slice.value();
            // try to look up inside the matched variant's scope, and then if it fails, look into
            // the containing scope to try to get more info (different variant, etc)
            auto maybe_variant_field_did
                = context.look_up_scoped_type(matched_variant_scope, sid_slice, id_span);
            if (maybe_variant_field_did.empty()) {
                maybe_variant_field_did
                    = context.look_up_scoped_type(containing_scope, sid_slice, id_span);
            }
            if (maybe_variant_field_did.empty()) {
                maybe_variant_field_did
                    = context.look_up_scoped_variable(containing_scope, sid_slice, id_span);
            }

            if (maybe_variant_field_did.empty()) {
                dl.link(context.emplace_diagnostic(
                    id_span, diag_code::use_of_undeclared_identifier, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_declared_in_this_scope}));

                valid = false;
                continue;
            }

            const auto variant_field_did = maybe_variant_field_did.as_id();

            if (!context.def(variant_field_did).holds<DefVariantField>()) {
                dl.link(context.emplace_diagnostic(
                    id_span, diag_code::invalid_pattern, diag_type::error,
                    DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field}));
                dl.link(context.emplace_diagnostic(
                    context.def(variant_field_did).span, diag_code::declared_here, diag_type::note,
                    DiagnosticSymbolBeforeMessage{.sid = context.def(variant_field_did).name},
                    DiagnosticSubCode{.sub_code = diag_code::not_a_variant_field}));

                valid = false;
                continue;
            }

            if (!context.check_variant_field_has_parent(dl, variant_field_did, variant_did,
                                                        id_span)) {
                valid = false;
                continue;
            }

            // already contained
            if (used_variant_fields.insert(variant_field_did)) {
                dl.link(context.emplace_diagnostic(Span{context, fid, pattern},
                                                   diag_code::duplicate_match_pattern,
                                                   diag_type::error));
                valid = false;
            }
        }
    }

    const auto variant_fields = variant_def.as<DefVariant>().ordered_members;

    if (!else_pattern && !(used_variant_fields.size() == variant_fields.len())) {
        Span span{context, fid, match_expr};
        dl.link(context.emplace_diagnostic(span, diag_code::match_expression_is_not_exhaustive,
                                           diag_type::error));

        valid = false;

        for (auto didx = variant_fields.begin(); didx != variant_fields.end(); ++didx) {
            const auto field_did = context.def_id(didx);
            if (!used_variant_fields.contains(field_did)) {
                dl.link(context.emplace_diagnostic(
                    span, diag_code::does_not_consider, diag_type::note,
                    DiagnosticIdentifierAfterMessage{
                        .sid_slice = context.try_singly_qualified_name(field_did)},
                    DiagnosticInfoNoPreview{}));
            }
        }
    }

    return valid;
}

inline std::string integral_key_to_string(uint64_t key, bool is_signed) {
    return is_signed ? std::to_string(static_cast<int64_t>(key ^ INTEGRAL_KEY_SIGN_BIT))
                     : std::to_string(key);
}

struct IntegralInterval {
    uint64_t lo;
    uint64_t hi;
};

inline std::optional<IntegralInterval> try_integral_interval_for_range(const Context& ctx,
                                                                       ExecId range_eid,
                                                                       bool is_signed) {
    const Exec& range_exec = ctx.exec(range_eid);
    assert(range_exec.holds<ExecRange>());
    const ExecRange range = range_exec.as<ExecRange>();
    const auto start = try_integral_key(ctx.exec(range.start).as<ExecConst>(), is_signed);
    const ExecConst end_val = ctx.exec(range.end).as<ExecConst>();
    const auto end = try_integral_key(end_val, is_signed);
    if (!start.has_value()) {
        return {};
    }
    if (!end.has_value()) {
        if (!range.inclusive && !is_signed && end_val.is_signed_integral()
            && end_val.to_i64() == -1) {
            return IntegralInterval{.lo = 0, .hi = start.value()};
        }
        return {};
    }
    if (range.inclusive) {
        return IntegralInterval{.lo = std::min(start.value(), end.value()),
                                .hi = std::max(start.value(), end.value())};
    }
    if (start.value() < end.value()) {
        return IntegralInterval{.lo = start.value(), .hi = end.value() - 1};
    }
    if (start.value() > end.value()) {
        return IntegralInterval{.lo = end.value() + 1, .hi = start.value()};
    }
    return {};
}

struct IntegralDomain {
    IntegralInterval keys;
    bool is_signed;
};

inline std::optional<IntegralDomain> try_integral_domain(builtin_type type) {
    auto signed_domain = [](int64_t lo, int64_t hi) {
        return IntegralDomain{.keys = {.lo = static_cast<uint64_t>(lo) ^ INTEGRAL_KEY_SIGN_BIT,
                                       .hi = static_cast<uint64_t>(hi) ^ INTEGRAL_KEY_SIGN_BIT},
                              .is_signed = true};
    };
    auto unsigned_domain = [](uint64_t hi) {
        return IntegralDomain{.keys = {.lo = 0, .hi = hi}, .is_signed = false};
    };
    switch (type) {
    case builtin_type::i8:
        return signed_domain(std::numeric_limits<int8_t>::min(), std::numeric_limits<int8_t>::max());
    case builtin_type::i16:
        return signed_domain(std::numeric_limits<int16_t>::min(),
                             std::numeric_limits<int16_t>::max());
    case builtin_type::i32:
        return signed_domain(std::numeric_limits<int32_t>::min(),
                             std::numeric_limits<int32_t>::max());
    case builtin_type::i64:
        return signed_domain(std::numeric_limits<int64_t>::min(),
                             std::numeric_limits<int64_t>::max());
    case builtin_type::u8:
        return unsigned_domain(std::numeric_limits<uint8_t>::max());
    case builtin_type::u16:
        return unsigned_domain(std::numeric_limits<uint16_t>::max());
    case builtin_type::u32:
        return unsigned_domain(std::numeric_limits<uint32_t>::max());
    case builtin_type::u64:
        return unsigned_domain(std::numeric_limits<uint64_t>::max());
    case builtin_type::charr:
    case builtin_type::f32:
    case builtin_type::f64:
    case builtin_type::voidd:
    case builtin_type::str:
    case builtin_type::nullpointer:
    case builtin_type::boolean:
        break;
    }
    return {};
}

class RangeList {
  public:
    struct Overlap {
        ExecId later;
        ExecId earlier;
        bool duplicate_value;
        HirSize later_order;
    };

  private:
    struct Entry {
        IntegralInterval interval;
        ExecId eid;
        HirSize order;
        bool is_value;
    };
    llvm::SmallVector<Entry, 16> entries;
    bool sorted = false;

  public:
    void put(IntegralInterval interval, ExecId eid, bool is_value) {
        assert(!sorted);
        entries.push_back(Entry{.interval = interval,
                                .eid = eid,
                                .order = static_cast<HirSize>(entries.size()),
                                .is_value = is_value});
    }

    [[nodiscard]] bool empty() const noexcept { return entries.empty(); }

    void sort() {
        std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
            return a.interval.lo != b.interval.lo ? a.interval.lo < b.interval.lo
                                                  : a.order < b.order;
        });
        sorted = true;
    }

    void find_overlaps(llvm::SmallVectorImpl<Overlap>& overlaps) const {
        assert(sorted);
        static constexpr size_t NONE = std::numeric_limits<size_t>::max();
        const size_t n = entries.size();
        llvm::SmallVector<size_t, 16> earlier(n, NONE);
        llvm::SmallVector<bool, 16> duplicate(n, false);

        for (size_t i = 1; i < n; ++i) {
            if (entries[i].is_value && entries[i - 1].is_value
                && entries[i].interval.lo == entries[i - 1].interval.lo) {
                earlier[i] = i - 1;
                duplicate[i] = true;
            }
        }

        llvm::SmallVector<size_t, 16> tree(n + 1, NONE);
        auto sweep = [&](auto&& better, auto&& overlaps_with, bool forward) {
            std::fill(tree.begin(), tree.end(), NONE);
            for (size_t step = 0; step < n; ++step) {
                const size_t i = forward ? step : n - 1 - step;
                if (earlier[i] == NONE) {
                    size_t best = NONE;
                    for (size_t k = entries[i].order; k > 0; k -= k & (~k + 1)) {
                        best = better(best, tree[k]);
                    }
                    if (best != NONE && overlaps_with(best, i)) {
                        earlier[i] = best;
                    }
                }
                for (size_t k = entries[i].order + 1; k <= n; k += k & (~k + 1)) {
                    tree[k] = better(tree[k], i);
                }
            }
        };

        sweep(
            [&](size_t a, size_t b) {
                if (a == NONE || b == NONE) {
                    return a == NONE ? b : a;
                }
                return entries[a].interval.hi >= entries[b].interval.hi ? a : b;
            },
            [&](size_t prior, size_t i) { return entries[prior].interval.hi >= entries[i].interval.lo; },
            true);
        sweep(
            [&](size_t a, size_t b) {
                if (a == NONE || b == NONE) {
                    return a == NONE ? b : a;
                }
                return entries[a].interval.lo <= entries[b].interval.lo ? a : b;
            },
            [&](size_t prior, size_t i) { return entries[prior].interval.lo <= entries[i].interval.hi; },
            false);

        for (size_t i = 0; i < n; ++i) {
            if (earlier[i] != NONE) {
                overlaps.push_back(Overlap{.later = entries[i].eid,
                                           .earlier = entries[earlier[i]].eid,
                                           .duplicate_value = duplicate[i],
                                           .later_order = entries[i].order});
            }
        }
        std::ranges::sort(overlaps, {}, &Overlap::later_order);
    }

    void find_gaps(IntegralInterval domain, llvm::SmallVectorImpl<IntegralInterval>& gaps) const {
        assert(sorted);
        uint64_t next = domain.lo;
        for (const Entry& e : entries) {
            if (e.interval.lo > domain.hi) {
                break;
            }
            if (e.interval.hi < next) {
                continue;
            }
            if (e.interval.lo > next) {
                gaps.push_back(IntegralInterval{.lo = next, .hi = e.interval.lo - 1});
            }
            if (e.interval.hi >= domain.hi) {
                return;
            }
            next = e.interval.hi + 1;
        }
        gaps.push_back(IntegralInterval{.lo = next, .hi = domain.hi});
    }
};

inline bool value_inside_range(const Context& ctx, ExecId range_eid, ExecId val_eid) {
    const Exec& value_exec = ctx.exec(val_eid);
    assert(value_exec.holds<ExecConst>());
    const ExecConst val = value_exec.as<ExecConst>();
    if (!val.is_integral()) {
        return false;
    }
    const bool is_signed = val.is_signed_integral();
    const auto key = try_integral_key(val, is_signed);
    const auto interval = try_integral_interval_for_range(ctx, range_eid, is_signed);
    return key.has_value() && interval.has_value() && interval->lo <= key.value()
           && key.value() <= interval->hi;
}

template <IsExprSolver S>
bool valid_exhaustive_match_for_non_variant(S& solver, ScopeId scope, FileId fid,
                                            const ast_expr_t* match_expr) {
    assert(match_expr->type == AST_EXPR_MATCH);

    Context& context = solver.get_context();

    const ast_slice_of_exprs_t branches = match_expr->expr.match_expr.branches;

    const OptId<ExecId> maybe_matched_eid
        = solver.solve_expr(fid, scope, match_expr->expr.match_expr.matched);

    if (maybe_matched_eid.empty()) {
        return false;
    }

    const auto matched_eid = maybe_matched_eid.as_id();

    {
        const Exec& matched_exec = context.exec(matched_eid);
        if (matched_exec.holds<ExecConst>()
            && (matched_exec.as<ExecConst>().holds<f32>()
                || matched_exec.as<ExecConst>().holds<f64>())) {

            const auto maybe_matched_tid = solver.infer_type_from_exec(matched_eid);

            if (maybe_matched_tid.empty()) {
                return false;
            }

            const auto matched_tid = maybe_matched_tid.as_id();

            auto d0 = context.emplace_diagnostic(
                matched_exec.span, diag_code::matching_on_floats_may_cause_unintended_behavior,
                diag_type::warning);
            auto d1 = context.emplace_diagnostic(
                matched_exec.span, diag_code::value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = matched_tid}, DiagnosticInfoNoPreview{});
            context.link_diagnostic(d0, d1);
        }
    }
    DiagLinker dl{context};

    const ast_expr_t* else_pattern = nullptr;

    bool valid = true;

    // decently sized
    DataArena arena{0x400};

    ExecHashMap<ExecId, equivalence::implicit> exec_map{context, arena, 0x200};

    std::optional<IntegralDomain> domain{};
    if (const OptId<TypeId> maybe_tid = solver.infer_type_from_exec(matched_eid);
        maybe_tid.has_value()) {
        const Type& matched_ty = context.type(context.try_decay(maybe_tid.as_id()));
        if (matched_ty.holds<TypeBuiltin>()) {
            domain = try_integral_domain(matched_ty.as<TypeBuiltin>().type);
        }
    }

    RangeList ranges{};

    auto report_pattern_type_mismatch = [&](ExecId pattern_eid, diag_code code) {
        const Span pattern_span = context.exec(pattern_eid).span;
        const Span matched_span = context.exec(matched_eid).span;
        dl.link(context.emplace_diagnostic(pattern_span, code, diag_type::error));
        if (const OptId<TypeId> maybe_tid = solver.infer_type_from_exec(pattern_eid);
            maybe_tid.has_value()) {
            dl.link(context.emplace_diagnostic_with_message_value(
                pattern_span, diag_code::pattern_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = maybe_tid.as_id()}));
        }
        if (const OptId<TypeId> maybe_tid = solver.infer_type_from_exec(matched_eid);
            maybe_tid.has_value()) {
            dl.link(context.emplace_diagnostic_with_message_value(
                matched_span, diag_code::matched_value_is_of_type, diag_type::note,
                DiagnosticTypeAfterMessage{.tid = maybe_tid.as_id()}));
        }
    };

    for (size_t i = 0; i < branches.len; ++i) {

        const ast_expr_t* branch = branches.start[i];

        assert(branch->type == AST_EXPR_MATCH_BRANCH);

        const ast_slice_of_exprs_t patterns = branch->expr.match_branch.patterns;

        for (size_t j = 0; j < patterns.len; ++j) {

            const ast_expr_t* pattern = patterns.start[j];

            if (pattern->type == AST_EXPR_ELSE_MATCH_PATTERN) {
                if (else_pattern) {
                    dl.link(context.emplace_diagnostic(Span{context, fid, pattern},
                                                       diag_code::duplicate_else_match_branch,
                                                       diag_type::error));
                    dl.link(context.emplace_diagnostic(Span{context, fid, else_pattern},
                                                       diag_code::previous_def_here,
                                                       diag_type::note));
                    valid = false;
                }
                else_pattern = pattern;
                if (i != branches.len - 1) {
                    dl.link(context.emplace_diagnostic(
                        Span{context, fid, pattern}, diag_code::else_match_branch_should_come_last,
                        diag_type::warning));
                }
                continue;
            }

            const OptId<ExecId> maybe_pattern_eid = solver.solve_expr(fid, scope, pattern);
            if (maybe_pattern_eid.empty()) {
                continue;
            }
            const ExecId pattern_eid = maybe_pattern_eid.as_id();
            if (!possibly_equivalent_exec(context, matched_eid, pattern_eid)) {
                report_pattern_type_mismatch(pattern_eid,
                                             diag_code::pattern_can_never_match_matched_value);
                continue;
            }
            const Exec& pattern_exec = context.exec(pattern_eid);
            const bool is_range = pattern_exec.holds<ExecRange>();
            const bool is_value
                = pattern_exec.holds<ExecConst>() && pattern_exec.as<ExecConst>().is_integral();
            if (is_range) {
                if (const auto canon
                    = try_canonical_integral_range(context, pattern_exec.as<ExecRange>());
                    canon.has_value() && canon->empty) {
                    dl.link(context.emplace_diagnostic(
                        pattern_exec.span, diag_code::empty_range_pattern, diag_type::warning));
                    continue;
                }
            }
            if (is_range || is_value) {
                const bool key_signed = domain.has_value() ? domain->is_signed : true;
                std::optional<IntegralInterval> maybe_interval{};
                if (is_range) {
                    maybe_interval
                        = try_integral_interval_for_range(context, pattern_eid, key_signed);
                } else if (const auto key
                           = try_integral_key(pattern_exec.as<ExecConst>(), key_signed);
                           key.has_value()) {
                    maybe_interval = IntegralInterval{.lo = key.value(), .hi = key.value()};
                }
                if (domain.has_value()
                    && (!maybe_interval.has_value() || maybe_interval->lo < domain->keys.lo
                        || maybe_interval->hi > domain->keys.hi)) {
                    report_pattern_type_mismatch(pattern_eid,
                                                 diag_code::pattern_exceeds_range_of_matched_type);
                    continue;
                }
                if (maybe_interval.has_value()) {
                    ranges.put(maybe_interval.value(), pattern_eid, is_value);
                    continue;
                }
                if (is_range) {
                    continue;
                }
            }
            const OptId<ExecId> already_eid = exec_map.at(pattern_eid);
            if (already_eid.has_value()) {
                dl.link(context.emplace_diagnostic(context.exec(pattern_eid).span,
                                                   diag_code::duplicate_match_pattern,
                                                   diag_type::error));
                dl.link(context.emplace_diagnostic(context.exec(already_eid.as_id()).span,
                                                   diag_code::existing_pattern_of_same_value_here,
                                                   diag_type::note));
                continue;
            }
            exec_map.insert(
                pattern_eid,
                pattern_eid); // point the exec back to itself so we can find the existing later
        }
    }

    ranges.sort();

    llvm::SmallVector<RangeList::Overlap> overlaps{};
    ranges.find_overlaps(overlaps);
    for (const RangeList::Overlap& overlap : overlaps) {
        dl.link(context.emplace_diagnostic(context.exec(overlap.later).span,
                                           overlap.duplicate_value
                                               ? diag_code::duplicate_match_pattern
                                               : diag_code::overlapping_range_patern,
                                           diag_type::error));
        dl.link(context.emplace_diagnostic(
            context.exec(overlap.earlier).span,
            overlap.duplicate_value ? diag_code::existing_pattern_of_same_value_here
                                    : diag_code::existing_pattern_with_overlapping_range_here,
            diag_type::note));
    }

    if (else_pattern) {
        return valid;
    }

    Span span{context, fid, match_expr};

    if (domain.has_value() && !ranges.empty()) {
        llvm::SmallVector<IntegralInterval> gaps{};
        ranges.find_gaps(domain->keys, gaps);
        if (gaps.empty()) {
            return valid;
        }
        valid = false;
        dl.link(context.emplace_diagnostic(span, diag_code::match_expression_is_not_exhaustive,
                                           diag_type::error));
        for (const IntegralInterval& gap : gaps) {
            std::string text = integral_key_to_string(gap.lo, domain->is_signed);
            if (gap.hi != gap.lo) {
                text += "...=" + integral_key_to_string(gap.hi, domain->is_signed);
            }
            dl.link(context.emplace_diagnostic(
                span, diag_code::does_not_consider, diag_type::note,
                DiagnosticSymbolAfterMessage{.sid = context.symbol_id(text)},
                DiagnosticInfoNoPreview{}));
        }
        dl.link(
            context.emplace_diagnostic(span, diag_code::add_an_else_branch, diag_type::help));
        return valid;
    }

    valid = false;
    const auto d0 = context.emplace_diagnostic(
        span, diag_code::match_expression_may_not_be_exhaustive, diag_type::error);
    const auto d1
        = context.emplace_diagnostic(span, diag_code::add_an_else_branch, diag_type::help);
    context.link_diagnostic(d0, d1);

    return valid;
}

// returns true on issue, else false
bool check_for_multi_decomps_on_match_branch(Context& ctx, FileId fid,
                                             ast_expr_match_branch_t match_branch);

} // namespace hir

#endif // !BEARC_COMPILER_HIR_MATCHING_HPP
