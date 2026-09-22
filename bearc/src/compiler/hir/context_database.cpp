//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#include "compiler/hir/context_database.hpp"
#include "compiler/hir/indexing.hpp"
#include <optional>

using namespace hir;

ContextDatabase::DefQueryResult
ContextDatabase::query_canon_def(const std::vector<std::string>& canonical_def_name) {
    return query_def(ctx->root_scope(), canonical_def_name);
}

[[nodiscard]] ContextDatabase::DefQueryResult
ContextDatabase::query_def(Span span, const std::vector<std::string>& canonical_def_name) {
    return query_def(ctx->scope_for_span(span), canonical_def_name);
}

[[nodiscard]] ContextDatabase::DefQueryResult
ContextDatabase::query_def(ScopeId scope, const std::vector<std::string>& canonical_def_name) {
    auto def_ids = query_def_id(scope, canonical_def_name);

    auto maybe_mod = def_ids.mod_id;

    auto maybe_type = def_ids.type_id;

    auto maybe_variable = def_ids.variable_id;

    return DefQueryResult{
        .mod
        = (maybe_mod.has_value() ? std::optional<Def>(ctx->def(maybe_mod.as_id())) : std::nullopt),
        .type = (maybe_type.has_value() ? std::optional<Def>(ctx->def(maybe_type.as_id()))
                                        : std::nullopt),
        .variable
        = (maybe_variable.has_value() ? std::optional<Def>(ctx->def(maybe_variable.as_id()))
                                      : std::nullopt),
    };
}

ContextDatabase::DefIdQueryResult
ContextDatabase::query_canon_def_id(const std::vector<std::string>& canonical_def_name) {
    return query_def_id(ctx->root_scope(), canonical_def_name);
}

[[nodiscard]] ContextDatabase::DefIdQueryResult
ContextDatabase::query_def_id(Span span, const std::vector<std::string>& canonical_def_name) {
    return query_def_id(ctx->scope_for_span(span), canonical_def_name);
}

[[nodiscard]] ContextDatabase::DefIdQueryResult
ContextDatabase::query_def_id(hir::ScopeId scope,
                              const std::vector<std::string>& canonical_def_name) {
    llvm::SmallVector<SymbolId> sid_vec;
    for (const auto& s : canonical_def_name) {
        sid_vec.push_back(ctx->symbol_id(s));
    }
    IdSlice<SymbolId> sid_slice = ctx->freeze_id_vec(sid_vec);
    auto maybe_mod = ctx->look_up_scoped_namespace_bypassing_visibility(scope, sid_slice);

    auto maybe_type = ctx->look_up_scoped_type_bypassing_visibility(scope, sid_slice);

    auto maybe_variable = ctx->look_up_scoped_variable_bypassing_visibility(scope, sid_slice);
    return DefIdQueryResult{
        .mod_id = maybe_mod, .type_id = maybe_type, .variable_id = maybe_variable};
}

int ContextDatabase::diagnostic_count() const noexcept { return ctx->diagnostic_count(); }

const hir::Diagnostic& ContextDatabase::diagnostic(hir::DiagnosticId did) const {
    return ctx->diagnostic(did);
}

const hir::Exec& ContextDatabase::exec(hir::ExecId eid) const { return ctx->exec(eid); }

[[nodiscard]] const llvm::SmallVectorImpl<hir::DiagnosticId>&
ContextDatabase::diagnostics_for_file(hir::FileId fid) const {
    return ctx->diagnostics_for_file(fid);
}

[[nodiscard]] const compiler_error_list_t&
ContextDatabase::parser_diagnostics_for_file(hir::FileId fid) const {
    return ctx->parser_diagnostics_for_file(fid);
}

[[nodiscard]] std::string
ContextDatabase::message_for_parser_diagnostic(const compiler_error_t& err) {
    return Context::message_for_parser_diagnostic(err);
}

std::string ContextDatabase::message_for_diagnostic(hir::DiagnosticId did) {
    return diagnostic(did).message_string(*ctx);
}

namespace {

std::vector<std::pair<std::string, std::string>>
copy_overlays(std::span<const SourceOverlay> overlays) {
    std::vector<std::pair<std::string, std::string>> storage;
    storage.reserve(overlays.size());
    for (const SourceOverlay& overlay : overlays) {
        storage.emplace_back(overlay.path, overlay.src);
    }
    return storage;
}

std::vector<SourceOverlay>
overlay_views(const std::vector<std::pair<std::string, std::string>>& storage) {
    std::vector<SourceOverlay> views;
    views.reserve(storage.size());
    for (const auto& [path, src] : storage) {
        views.push_back(SourceOverlay{.path = path, .src = src.c_str()});
    }
    return views;
}

} // namespace

ContextDatabase::ContextDatabase(std::vector<const char*> args_vec,
                                 std::span<const SourceOverlay> overlays)
    : args{std::make_unique<bearc_args>(parse_cli_args(static_cast<int>(args_vec.size()),
                                                       const_cast<char**>(args_vec.data())))},
      overlay_storage{copy_overlays(overlays)},
      ctx{std::make_unique<Context>(*this->args, Context::instances::multiple,
                                    overlay_views(overlay_storage))} {}

OptId<FileId> ContextDatabase::file_id_for_path(std::string_view path) const {
    return ctx->file_id_for_path(path);
}

const char* ContextDatabase::file_name(FileId fid) const { return ctx->file_name(fid); }

std::string_view ContextDatabase::file_source(FileId fid) const {
    const FileAst& ast = ctx->ast(fid);
    return std::string_view{ast.buffer(), ast.src()->src_len};
}

bool ContextDatabase::file_is_intrinsic(FileId fid) const { return ctx->file_is_intrinsic(fid); }

std::string_view ContextDatabase::symbol(SymbolId sid) const { return ctx->symbol(sid); }
