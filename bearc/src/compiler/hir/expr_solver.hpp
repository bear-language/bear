//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the Apache License 2.0. See LICENSE for details.

#ifndef COMPILER_HIR_EXPR_TO_EXEC_HPP
#define COMPILER_HIR_EXPR_TO_EXEC_HPP

#include "compiler/ast/expr.h"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/indexing.hpp"

namespace hir {

template <typename S>
concept IsExprSolver
    = requires(S s, ExecId eid, FileId fid, ScopeId scope, const ast_expr_t* expr) {
          { s.infer_type_from_exec(eid) } -> std::same_as<OptId<TypeId>>;
          { s.solve_expr(fid, scope, expr) } -> std::same_as<OptId<ExecId>>;
          { s.get_context() } -> std::same_as<Context&>;
      };

} // namespace hir

#endif // !COMPILER_HIR_AST_EXPR_TO_EXEC_HPP
