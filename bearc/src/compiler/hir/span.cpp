//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/span.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/token.h"
#include <stddef.h>
#include <string_view>

namespace hir {
Span::Span(FileId file_id, const char* src, const token_t* tkn)
    : start(tkn->start - src), len(tkn->len), file_id(file_id), line(tkn->loc.line),
      col(tkn->loc.col) {}

Span::Span(FileId file_id, const char* src, const token_t* first, const token_t* last)
    : start(first->start - src), len((last->start + last->len) - first->start), file_id(file_id),
      line(first->loc.line), col(first->loc.col) {}

std::string_view Span::retrieve_from_buffer(const char* data, Span span) {
    return std::string_view(data + span.start, span.len);
}

[[nodiscard]] std::string_view Span::as_sv(const Context& context) const {
    return retrieve_from_buffer(context.ast(file_id).buffer(), *this);
}

Span::Span(const Context& ctx, FileId file_id, const token_t* first, const token_t* last)
    : Span(file_id, ctx.ast(file_id).buffer(), first, last) {}

Span::Span(const Context& ctx, FileId file_id, const ast_expr_t* expr)
    : Span(ctx, file_id, expr->first, expr->last) {}

Span::Span(const Context& ctx, FileId file_id, const token_t* tkn)
    : start(tkn->start - ctx.ast(file_id).buffer()), len(tkn->len), file_id(file_id),
      line(tkn->loc.line), col(tkn->loc.col) {}

Span::Span(const Context& ctx, FileId file_id, token_ptr_slice_t token_slice)
    : Span(ctx, file_id, token_slice.start[0], token_slice.start[token_slice.len - 1]) {}
Span Span::generated() { return Span{0, 0, FileId{HIR_ID_NONE}, 0, 0}; }

Span Span::combine(Span span1, Span span2) {
    if (span1.file_id != span2.file_id) {
        // assert(false && "tried to combine two hir::Spans from different files");
        return span1; // PROBLEM: better than failing, i guess? alternatively, we could return
                      // Span::generated() but that's more lossy, although less deceptive
    }
    return Span(span1.start, span2.start - span1.start + span2.len, span1.file_id, span1.line,
                span1.col);
}

Span Span::find_between_tokens(const Context& ctx, FileId fid, const token_t* t1,
                               const token_t* t2) {
    Span s1{ctx, fid, t1};
    Span s2{ctx, fid, t2};
    return find_between_spans(ctx, fid, s1, s2);
}

Span Span::find_between_spans(const Context& ctx, FileId fid, Span s1, Span s2) {
    assert(s1.file_id == s2.file_id);
    const char* t1_c = retrieve_from_buffer(ctx.ast(fid).buffer(), s1).data();
    char c = *t1_c;
    HirSize len_from_left = 0;
    while (c = *t1_c, t1_c++, !is_whitespace(c)) {
        len_from_left++;
    }
    // undo overshoot
    if (len_from_left != 0) {
        t1_c--;
    }

    const char* t2_c = retrieve_from_buffer(ctx.ast(fid).buffer(), s2).data();

    c = *t2_c;
    HirSize len_from_right = 0;
    while (c = *t2_c, t2_c--, !is_whitespace(c)) {
        len_from_right++;
    }
    static_assert(sizeof(size_t) == sizeof(const char*));
    return Span{(s1.start + len_from_left + 1),
                (s2.start - s1.start - len_from_left - len_from_right - 1), (s1.file_id), (s1.line),
                (s1.col)};
}

} // namespace hir
