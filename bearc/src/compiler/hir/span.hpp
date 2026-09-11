//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_SPAN_HPP
#define COMPILER_SPAN_HPP

#include "compiler/ast/expr.h"
#include "compiler/ast/stmt_slice.h"
#include "compiler/hir/indexing.hpp"
#include "compiler/token.h"
#include <stdbool.h>
#include <stddef.h>
#include <string_view>

namespace hir {

class Context;

class Span {
    [[nodiscard]] Span(HirSize start, HirSize len, FileId file_id, HirSize line,
                       HirSize col) noexcept
        : start(start), len(len), file_id(file_id), line(line), col(col) {};

  public:
    HirSize start{};
    HirSize len{};
    FileId file_id{};
    HirSize line{};
    HirSize col{};
    /// constructs an hir::Span from an existing FileId and none-owned ptrs to a src buffer and a
    /// token_t
    [[nodiscard]] Span(FileId file_id, const char* src, const token_t* tkn);
    [[nodiscard]] Span(FileId file_id, const char* src, const token_t* first, const token_t* last);
    [[nodiscard]] Span(const Context& ctx, FileId file_id, token_ptr_slice_t token_slice);
    [[nodiscard]] Span(const Context& ctx, FileId file_id, const token_t* first,
                       const token_t* last);
    [[nodiscard]] Span(const Context& ctx, FileId file_id, const ast_expr_t* expr);
    [[nodiscard]] Span(const Context& ctx, FileId file_id, const ast_stmt_t* stmt);
    /// stmts.len should be > 0, default to Span::generated() as a fallback otherwise
    [[nodiscard]] Span(const Context& ctx, FileId file_id, ast_slice_of_stmts_t stmts);
    [[nodiscard]] Span(const Context& ctx, FileId file_id, const token_t* tkn);
    [[nodiscard]] static std::string_view retrieve_from_buffer(const char* data, Span span);
    [[nodiscard]] std::string_view as_sv(const Context& context) const;
    [[nodiscard]] static Span generated();
    [[nodiscard]] bool is_generated() const { return file_id.raw() == HIR_ID_NONE; };
    [[nodiscard]] static Span combine(Span span1, Span span2);
    [[nodiscard]] static Span find_between_tokens(const Context& ctx, FileId fid, const token_t* t1,
                                                  const token_t* t2);
    [[nodiscard]] static Span find_between_spans(const Context& ctx, FileId fid, Span s1, Span s2);
    [[nodiscard]] bool contained_in(Span containing) const {
        return file_id == containing.file_id && containing.start <= start
               && containing.start + containing.len >= start + len;
    };
    // faster version of contained in that doesn't check matching file_ids
    [[nodiscard]] bool within_same_file_contained_in(Span containing) const {
        return containing.start <= start && containing.start + containing.len >= start + len;
    };
};

} // namespace hir

#endif
