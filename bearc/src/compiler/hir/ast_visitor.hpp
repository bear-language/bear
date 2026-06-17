//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_AST_VISITOR_HPP
#define COMPILER_HIR_AST_VISITOR_HPP

#include "compiler/ast/stmt_slice.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
namespace hir {

struct TopLevelInfo {
    token_t* scope_prefix_tkn = nullptr;
    token_t* name_tkn = nullptr;
    std::optional<ast_slice_of_stmts_t> stmts;
    scope_kind kind;
    bool is_orderable_var = false;
    bool is_generic = false;
    bool do_not_insert_in_scope = false;
};

enum class gen_instatiation_state : uint8_t {
    not_instantiating,
    instantiating,
};

enum class gen_visit_state : uint8_t {
    outside_generic,
    inside_generic,
};

struct GenericState {
    gen_instatiation_state inst_state;
    gen_visit_state vis_state;
    [[nodiscard]] bool not_instantiating_but_in_generic() const {
        return inst_state == gen_instatiation_state::not_instantiating
               && vis_state == gen_visit_state::inside_generic;
    }
};

/**
 * class to traverse the entirety of the namespaces of a file, filling in top level declarations
 * into an hir::Context
 *
 */
class FileAstVisitor {
    Context& context;
    FileId file;
    OptId<DefId> register_top_level_stmt(ScopeId scope, ast_stmt_t* stmt, OptId<DefId> parent,
                                         abi_lang abi, GenericState gen_state);
    void register_top_level_stmts(ScopeId scope, ast_slice_of_stmts_t stmts, OptId<DefId> parent,
                                  abi_lang abi, GenericState gen_state);
    void register_top_level_stmts_registering_ordered_members(DefId parent_def, ScopeId scope,
                                                              ast_slice_of_stmts_t stmts,
                                                              OptId<DefId> parent, abi_lang abi,
                                                              GenericState gen_state);
    static TopLevelInfo top_level_info_for(const ast_stmt_t* stmt);

  public:
    FileAstVisitor(Context& context, FileId file) : context(context), file(file) {}
    void register_top_level_declarations();
    static std::optional<const token_t*> name_of_ast_decl(const ast_stmt_t* stmt);
    [[nodiscard]] OptId<DefId> lower_generic_stmt(ScopeId scope, ast_stmt_t* stmt,
                                                  OptId<DefId> parent);
};

} // namespace hir

#endif
