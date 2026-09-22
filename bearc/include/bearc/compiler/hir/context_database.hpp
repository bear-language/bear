//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_HIR_CONTEXT_DATABASE_HPP
#define COMPILER_HIR_CONTEXT_DATABASE_HPP

#include "cli/args.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/span.hpp"
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ContextDatabase {
  public:
    ContextDatabase(int arg_count, const char** args)
        : args{std::make_unique<bearc_args>(parse_cli_args(arg_count, const_cast<char**>(args)))},
          ctx{std::make_unique<hir::Context>(*this->args, hir::Context::instances::multiple,
                                             std::span<hir::SourceOverlay>{})} {}
    ContextDatabase(std::vector<const char*> args_vec)
        : args{std::make_unique<bearc_args>(parse_cli_args(static_cast<int>(args_vec.size()),
                                                           const_cast<char**>(args_vec.data())))},
          ctx{std::make_unique<hir::Context>(*this->args, hir::Context::instances::multiple,
                                             std::span<hir::SourceOverlay>{})} {}
    /// reads any file whose canonical path matches an overlay from that overlay instead of from
    /// disk (e.g. unsaved editor buffers)
    /// - overlays are copied, so they don't need to outlive the database
    /// - the strings pointed to by args_vec still need to outlive the database
    ContextDatabase(std::vector<const char*> args_vec,
                    std::span<const hir::SourceOverlay> overlays);

    struct DefQueryResult {
        std::optional<hir::Def> mod, type, variable;
    };

    struct DefIdQueryResult {
        hir::OptId<hir::DefId> mod_id, type_id, variable_id;
    };

    using Span = hir::Span;

    [[nodiscard]] DefQueryResult
    query_canon_def(const std::vector<std::string>& canonical_def_name);

    [[nodiscard]] DefQueryResult query_def(Span span,
                                           const std::vector<std::string>& canonical_def_name);

    [[nodiscard]] DefQueryResult query_def(hir::ScopeId scope,
                                           const std::vector<std::string>& canonical_def_name);

    [[nodiscard]] DefIdQueryResult
    query_canon_def_id(const std::vector<std::string>& canonical_def_name);

    [[nodiscard]] DefIdQueryResult query_def_id(Span span,
                                                const std::vector<std::string>& canonical_def_name);

    [[nodiscard]] DefIdQueryResult query_def_id(hir::ScopeId scope,
                                                const std::vector<std::string>& canonical_def_name);

    [[nodiscard]] const hir::Exec& exec(hir::ExecId eid) const;

    [[nodiscard]] int diagnostic_count() const noexcept;

    [[nodiscard]] const hir::Diagnostic& diagnostic(hir::DiagnosticId did) const;

    /// gets the hir (semantic anaylsis phase) / logical diagnostics for a given file
    [[nodiscard]] const llvm::SmallVectorImpl<hir::DiagnosticId>&
    diagnostics_for_file(hir::FileId fid) const;

    /// gets the parser diagnostics for a file
    [[nodiscard]] const compiler_error_list_t& parser_diagnostics_for_file(hir::FileId fid) const;

    [[nodiscard]] static std::string message_for_parser_diagnostic(const compiler_error_t& err);

    [[nodiscard]] std::string message_for_diagnostic(hir::DiagnosticId did);

    /// gets the FileId for a canonical file path, if that file was loaded into this database
    [[nodiscard]] hir::OptId<hir::FileId> file_id_for_path(std::string_view path) const;

    /// gets the canonical path of a file
    [[nodiscard]] const char* file_name(hir::FileId fid) const;

    /// gets the full source text that a file was parsed from
    [[nodiscard]] std::string_view file_source(hir::FileId fid) const;

    /// checks if a file is an intrinsic pseudo source file, which has no real path
    [[nodiscard]] bool file_is_intrinsic(hir::FileId fid) const;

    /// gets the string of an interned symbol
    [[nodiscard]] std::string_view symbol(hir::SymbolId sid) const;

  private:
    std::unique_ptr<const bearc_args> args;
    /// owned {path, src} copies of the source overlays that ctx reads from
    std::vector<std::pair<std::string, std::string>> overlay_storage;
    std::unique_ptr<hir::Context> ctx;
};

#endif // !COMPILER_HIR_CONTEXT_DATA_BASE_HPP
