//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/compt_expr_solver.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/type_resolver.hpp"

namespace hir {

template <IsDefVisitor V>
OptId<TypeId> ComptExprSolver<V>::resolve_type(FileId fid, ScopeId scope, const ast_type_t* type) {
    return TypeResolver<V>{context, this->def_visitor}.resolve_type(fid, scope, type);
}
template <IsDefVisitor V>
OptId<TypeId> ComptExprSolver<V>::resolve_type(FileId fid, ScopeId scope, const ast_type_t* type,
                                               bool need_layout_info) {
    return TypeResolver<V>{context, this->def_visitor}.resolve_type(fid, scope, type,
                                                                    need_layout_info);
}

// explicit instants., these should be the only ones necessary
template class ComptExprSolver<InsideBodyDefVisitor>;
template class ComptExprSolver<TopLevelDefVisitor>;

} // namespace hir
