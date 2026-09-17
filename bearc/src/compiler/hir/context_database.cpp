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
ContextDatabase::query_def(const std::vector<std::string>& def_path) {
    auto def_ids = query_def_id(def_path);

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
ContextDatabase::query_def_id(const std::vector<std::string>& def_path) {
    llvm::SmallVector<SymbolId> sid_vec;
    for (const auto& s : def_path) {
        sid_vec.push_back(ctx->symbol_id(s));
    }
    IdSlice<SymbolId> sid_slice = ctx->freeze_id_vec(sid_vec);
    auto maybe_mod
        = ctx->look_up_scoped_namespace_bypassing_visibility(ctx->root_scope(), sid_slice);

    auto maybe_type = ctx->look_up_scoped_type_bypassing_visibility(ctx->root_scope(), sid_slice);

    auto maybe_variable
        = ctx->look_up_scoped_variable_bypassing_visibility(ctx->root_scope(), sid_slice);
    return DefIdQueryResult{
        .mod_id = maybe_mod, .type_id = maybe_type, .variable_id = maybe_variable};
}

int ContextDatabase::diagnostic_count() const noexcept { return ctx->diagnostic_count(); }

hir::Exec ContextDatabase::exec(hir::ExecId eid) const { return ctx->exec(eid); }
