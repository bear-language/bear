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
#include "compiler/hir/span.hpp"

class ContextDatabase {
  public:
    ContextDatabase(int arg_count, const char** args)
        : args{std::make_unique<bearc_args>(parse_cli_args(arg_count, const_cast<char**>(args)))},
          ctx{std::make_unique<hir::Context>(*this->args)} {}
    ContextDatabase(std::vector<const char*> args_vec)
        : args{std::make_unique<bearc_args>(parse_cli_args(static_cast<int>(args_vec.size()),
                                                           const_cast<char**>(args_vec.data())))},
          ctx{std::make_unique<hir::Context>(*this->args)} {}

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

    [[nodiscard]] hir::Exec exec(hir::ExecId eid) const;

    [[nodiscard]] int diagnostic_count() const noexcept;

  private:
    std::unique_ptr<const bearc_args> args;
    std::unique_ptr<hir::Context> ctx;
};

#endif // !COMPILER_HIR_CONTEXT_DATA_BASE_HPP
