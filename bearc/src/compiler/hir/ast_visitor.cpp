//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/ast_visitor.hpp"
#include "compiler/ast/expr.h"
#include "compiler/ast/printer.h"
#include "compiler/ast/stmt.h"
#include "compiler/hir/context.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/token.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <iostream>
#include <variant>

namespace hir {

bool is_lower(const token_t* s);
bool is_capital(const token_t* s);
std::optional<abi_lang> abi_for_extern_stmt(const ast_stmt_t* stmt);

void FileAstVisitor::register_top_level_declarations() {
    // registers all the top level stmts of the file using the top level scope
    register_top_level_stmts(context.get_or_make_root_scope(),
                             context.ast(file).root()->stmt.file.stmts, OptId<DefId>{},
                             abi_lang::bear,
                             GenericState{.inst_state = gen_instatiation_state::not_instantiating,
                                          .vis_state = gen_visit_state::outside_generic});
}

FileAstVisitor::RegisteredDefResult
FileAstVisitor::register_top_level_stmt(ScopeId scope, const ast_stmt_t* stmt, OptId<DefId> parent,
                                        abi_lang abi, GenericState gen_state,
                                        bool force_return_did) {
    // get first and last token before adjustments so we get the true full span
    const token_t* first_tkn = stmt->first;
    const token_t* last_tkn = stmt->last;

    // handle prefix wrappers --------
    bool pub = true;
    if (stmt->type == AST_STMT_VISIBILITY_MODIFIER) {
        pub = stmt->stmt.vis_modifier.modifier->type == TOK_PUB; // false when hid
        // make stmt equal to inner
        stmt = stmt->stmt.vis_modifier.stmt;
    }

    bool compt = false;
    bool statik = false;
    uint8_t align_pref = 0;

    while (stmt->type == AST_STMT_COMPT_MODIFIER || stmt->type == AST_STMT_STATIC_MODIFIER
           || stmt->type == AST_STMT_ALIGNAS_MODIFIER) {
        if (stmt->type == AST_STMT_COMPT_MODIFIER) {
            if (compt) {
                const token_t* prefix_tkn = stmt->first;
                Span span = Span(file, context.ast(file).buffer(), prefix_tkn);
                auto did0 = context.emplace_diagnostic(span, diag_code::redundant_compt_qualifier,
                                                       diag_type::error);
                auto did1 = context.emplace_diagnostic(
                    span, diag_code::remove, diag_type::help,
                    DiagnosticSymbolAfterMessage{context.symbol_id(span)}, DiagnosticNoOtherInfo{});
                context.link_diagnostic(did0, did1);
            }
            compt = true;
            // take inner
            stmt = stmt->stmt.compt_modifier.stmt;
        }

        if (stmt->type == AST_STMT_STATIC_MODIFIER) {
            if (statik) {
                const token_t* prefix_tkn = stmt->first;
                Span span = Span(file, context.ast(file).buffer(), prefix_tkn);
                auto did0 = context.emplace_diagnostic(span, diag_code::redundant_static_qualifier,
                                                       diag_type::error);
                auto did1 = context.emplace_diagnostic(
                    span, diag_code::remove, diag_type::help,
                    DiagnosticSymbolAfterMessage{context.symbol_id(span)}, DiagnosticNoOtherInfo{});
                context.link_diagnostic(did0, did1);
            }
            statik = true;
            // take inner
            stmt = stmt->stmt.static_modifier.stmt;
        }

        auto try_align_pref = [&](const ast_expr_t* expr) -> uint8_t {
            const auto* tkn = expr->expr.literal.tkn;
            static constexpr auto MAX_ALIGN = 128u;
            if (tkn->type == TOK_UINT_LIT && tkn->val.unsigned_integral <= MAX_ALIGN
                && tkn->val.unsigned_integral > 0) {
                return tkn->val.unsigned_integral;
            }
            return 0u;
        };

        if (stmt->type == AST_STMT_ALIGNAS_MODIFIER) {
            if (align_pref != 0) {
                const token_t* prefix_tkn_first = stmt->first;
                const token_t* prefix_tkn_last = stmt->stmt.alignaz.align_expr->last;
                Span span
                    = Span(file, context.ast(file).buffer(), prefix_tkn_first, prefix_tkn_last);
                auto did0 = context.emplace_diagnostic(span, diag_code::multiple_alignas_on_one_def,
                                                       diag_type::error);
                auto did1 = context.emplace_diagnostic(
                    span, diag_code::remove, diag_type::help,
                    DiagnosticSymbolAfterMessage{context.symbol_id(span)}, DiagnosticNoOtherInfo{});
                context.link_diagnostic(did0, did1);
            }
            const ast_expr_t* expr = stmt->stmt.alignaz.align_expr;
            if (expr->type == AST_EXPR_LITERAL) {
                align_pref = try_align_pref(expr);
            } else if (expr->type == AST_EXPR_GROUPING) {
                while (expr->type == AST_EXPR_GROUPING) {
                    expr = expr->expr.grouping.expr;
                    // last case, this will try to get a valid value, else fails and align pref is
                    // default
                    if (expr->type == AST_EXPR_LITERAL) {
                        align_pref = try_align_pref(expr);
                    }
                }
            }
            // meaning try_align_pref had to return the default value
            if (align_pref == 0) {
                auto did0 = context.emplace_diagnostic(
                    Span(file, context.ast(file).buffer(), expr->first, expr->last),
                    diag_code::invalid_alignas, diag_type::error);
                auto did1 = context.emplace_diagnostic(
                    Span(file, context.ast(file).buffer(), expr->first, expr->last),
                    diag_code::alignas_expr_must_be_a_valid_uint_lit, diag_type::help);
                context.link_diagnostic(did0, did1);
            }
            stmt = stmt->stmt.alignaz.inner;
        }
    }
    statik |= compt; // all compts are static
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    // special cases (modules and extern blocks)
    // handle module, first search for an existing module to insert into
    if (stmt->type == AST_STMT_MODULE) {
        token_t* name_tkn = stmt->stmt.module.id;
        SymbolId name = context.symbol_id_for_identifier_tkn(name_tkn);

        // look up a LOCAL namespace since we don't want to traverse parents for already defined
        // namespaces, because we want to allow nested namespaces. If we didn't do this, we might
        // have this situation:
        // mod foo { i32 BAR = 42; mod bar { i32 BAR = 42; } }
        // mod foey { mod foo { i32 BAR = 42; } }
        // where mod foo is found in the parent of mod foey and then BAR is added to the outer
        // definition of foo!
        //
        // this would be bad.
        OptId<DefId> existing = Scope::look_up_local_namespace(context, scope, name);

        bool existing_module
            = existing.has_value()
              && std::holds_alternative<DefModule>(context.def(existing.as_id()).value);

        DefId mod_def
            = existing_module
                  ? existing.as_id()
                  : context.register_top_level_def(
                        name, pub, compt, /*not generic*/ false, statik,
                        Span(file, context.ast(file).buffer(), /* just name token! */ name_tkn),
                        stmt,
                        parent); // just make span with name token otherwise it will be too long
        ScopeId mod_scope = existing_module
                                ? get<DefModule>(context.def(existing.as_id()).value).scope
                                : context.make_scope(scope);
        // warn capitalized_mod if the mod is new and capitalized
        if (!existing_module && is_capital(name_tkn)) {
            context.emplace_diagnostic(Span(file, context.ast(file).buffer(), name_tkn),
                                       diag_code::capitalized_mod, diag_type::warning);
        }
        context.def(mod_def).set_value(DefModule{.scope = mod_scope});
        context.scope(scope).insert_namespace(name, mod_def);
        register_top_level_stmts(mod_scope, stmt->stmt.module.decls, mod_def, abi,
                                 gen_state); // pass in this module def as parent

        return {};
    }
    // handle extern block
    if (stmt->type == AST_STMT_EXTERN_BLOCK) {
        auto maybe_abi = abi_for_extern_stmt(stmt);
        // ensure valid specified abi
        if (!maybe_abi.has_value()) {
            Span span{file, context.ast(file).buffer(), stmt->stmt.extern_block.extern_language};
            auto did0
                = context.emplace_diagnostic(span, diag_code::invalid_extern_lang, diag_type::error,
                                             DiagnosticSymbolAfterMessage{context.symbol_id(
                                                 stmt->stmt.extern_block.extern_language)},
                                             DiagnosticNoOtherInfo{});
            auto did1 = context.emplace_diagnostic(
                span, diag_code::replace_with, diag_type::help,
                DiagnosticSymbolAfterMessage{context.symbol_id<"C">()}, DiagnosticNoOtherInfo{});
            auto did2 = context.emplace_diagnostic(
                span, diag_code::remove, diag_type::help,
                DiagnosticSymbolAfterMessage{context.symbol_id(span)}, DiagnosticNoOtherInfo{});
            context.link_diagnostic(did0, did1);
            context.link_diagnostic(did1, did2);

        } else {
            enum abi_lang abi = maybe_abi.value();
            register_top_level_stmts(scope, stmt->stmt.extern_block.decls, parent, abi, gen_state);
        }
        return {};
    }
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    const TopLevelInfo info = top_level_info_for(stmt);
    const scope_kind kind = info.kind;
    const token_t* name_tkn = info.name_tkn;
    const std::optional<ast_slice_of_stmts_t> stmts = info.stmts;
    const bool is_generic = info.is_generic;

    // if this wasn't named definition, then RETURN so we don't try to make a new hir::Def
    if (name_tkn == nullptr) {
        return {};
    }

    // if we're already instantiating and inside a generic, we don't want to be instatiating anymore
    // since we only want to instatiate one generic at a time
    if (is_generic && gen_state.inst_state == gen_instatiation_state::instantiating
        && gen_state.vis_state == gen_visit_state::inside_generic) {
        gen_state.inst_state = gen_instatiation_state::not_instantiating;
    }

    // don't evaulate variables/(functions)/variant fields when we're inside a generic that we're
    // not instatiating so that we don't try to make definitiosn with incomplete/non-instatiated
    // variables/fns/fields
    if (!is_generic
        && (kind == scope_kind::variable || stmt->type == AST_STMT_VARIANT_FIELD_DECL
            || stmt->type == AST_STMT_DEFTYPE || stmt->type == AST_STMT_USE)
        && gen_state.not_instantiating_but_in_generic()) {
        return {};
    }

    // hanlde scope prefix for Foo..bar() functions
    token_t* prefix = info.scope_prefix_tkn;
    if (prefix) {
        OptId<DefId> maybe_type_did = Scope::look_up_type(
            context, scope, context.symbol_id_for_identifier_tkn(info.scope_prefix_tkn));
        bool no_struct = false;
        if (maybe_type_did.has_value()) {
            parent = maybe_type_did.as_id();
            const auto parent_info = top_level_info_for(context.def_ast_node(parent.as_id()));
            if (parent_info.is_generic) {
                Span span{context, file, prefix};
                const auto d0 = context.emplace_diagnostic(
                    span,
                    diag_code::out_of_line_scoped_member_functions_not_allowed_for_generic_structs,
                    diag_type::error);
                const auto d1 = context.emplace_diagnostic_with_message_value(
                    context.def(parent.as_id()).span, diag_code::declared_here_as_generic,
                    diag_type::note,
                    DiagnosticSymbolBeforeMessage{.sid = context.def(parent.as_id()).name});
                const auto d2 = context.emplace_diagnostic(
                    span, diag_code::instead_take_the_generic_struct_as_a_first_argument,
                    diag_type::help);
                context.link_diagnostic(d0, d1);
                context.link_diagnostic(d1, d2);
            }
            if (auto s = context.defs_to_scopes().at(maybe_type_did.as_id()); s.has_value()) {
                scope = s.as_id();
            } else {
                no_struct = true;
            }
        } else {
            no_struct = true;
        }

        if (no_struct) {
            context.emplace_diagnostic(Span(file, context.ast(file).buffer(), prefix),
                                       diag_code::no_matching_struct_for_method, diag_type::error);
        }
    }

    // get symbol from token
    SymbolId name = context.symbol_id_for_identifier_tkn(name_tkn);

    // check for redefintion
    OptId<DefId> already_defined{};
    switch (kind) {
    case scope_kind::variable:
        already_defined = context.scope(scope).already_defines_variable(name);
        break;
    case scope_kind::type:
        already_defined = context.scope(scope).already_defines_type(name);
        break;
    default:
        break;
    }

    bool currently_instatiating_a_specialized_generic = false;

    // redefintion guard, only care if it's not for the same stmt (basically meaning this is a
    // generic instatiation)
    if (already_defined.has_value()) {
        currently_instatiating_a_specialized_generic
            = gen_state.inst_state == gen_instatiation_state::instantiating;
        if (!currently_instatiating_a_specialized_generic
            && context.def_ast_node(already_defined.as_id()) != stmt) {
            // do diagnostics for the redefinition
            auto d1 = context.emplace_diagnostic(Span(file, context.ast(file).buffer(), name_tkn),
                                                 diag_code::redefined_symbol, diag_type::error);
            auto orig_file = context.def(already_defined.as_id()).span.file_id;
            auto* t = top_level_info_for(context.def_ast_node(already_defined.as_id())).name_tkn;
            auto d2
                = context.emplace_diagnostic(Span(orig_file, context.ast(orig_file).buffer(), t),
                                             diag_code::previous_def_here, diag_type::note);
            context.link_diagnostic(d1, d2);
            return {};
        }
    }

    // no issues, so register definition
    DefId did = context.register_top_level_def(
        name, pub, compt, statik, is_generic,
        Span(file, context.ast(file).buffer(), first_tkn, last_tkn), stmt, parent);

    if (stmt->type == AST_STMT_FN_DECL || stmt->type == AST_STMT_FN_PROTOTYPE) {
        context.record_function_def(did); // record this function to ensure it gets lowered
    }

    // register into a scope
    if (!info.do_not_insert_in_scope) {
        switch (kind) {
        case scope_kind::variable:
            // specializations shouldn't map name to def_id since their generic args are how we look
            // them up!
            if (!currently_instatiating_a_specialized_generic) {
                context.scope(scope).insert_variable(name, did);
            }
            if (stmt->type == AST_STMT_FN_DECL || stmt->type == AST_STMT_FN_PROTOTYPE) {
                const bool is_small_scope = !stmts.has_value();
                ScopeId new_scope = (is_small_scope) ? context.make_small_scope(scope)
                                                     : context.make_scope(scope);

                context.defs_to_scopes().insert(did, new_scope);
            }

            break;
        case scope_kind::type: {
            // specializations shouldn't map name to def_id since their generic args are how we look
            // them up!
            if (!currently_instatiating_a_specialized_generic) {
                context.scope(scope).insert_type(name, did);
            }
            // if the type (namely a variant field decl) doesn't have statements then the scope
            // needn't be large
            const bool is_small_scope = !stmts.has_value();
            ScopeId types_scope
                = (is_small_scope) ? context.make_small_scope(scope) : context.make_scope(scope);

            context.defs_to_scopes().insert(did, types_scope);
            // warn on lowercase structure definition
            if (stmt->type != AST_STMT_DEFTYPE && is_lower(name_tkn)) {
                context.emplace_diagnostic(Span(file, context.ast(file).buffer(), name_tkn),
                                           diag_code::lowercase_structure, diag_type::warning);
            }
            // try to parse fields
            if (info.stmts.has_value()) {
                GenericState new_gen_state{.inst_state = gen_state.inst_state,
                                           .vis_state = (is_generic)
                                                            ? gen_visit_state::inside_generic
                                                            : gen_state.vis_state};
                register_top_level_stmts_registering_ordered_members(
                    did, types_scope, info.stmts.value(), did, abi, new_gen_state);
            }
            break;
        }

        default:
            break;
        }
    }
    // return the DefId since this is an orderable definition
    if (info.is_orderable_var && !statik && !compt) {
        return RegisteredDefResult{.maybe_did = did};
    }
    if (parent.has_value()) {
        const auto parent_node_type = context.def_ast_node(parent.as_id())->type;
        // always order contract fields
        if (parent_node_type == AST_STMT_CONTRACT_DEF) {
            return RegisteredDefResult{.maybe_did = did};
        }
        // always return non-orderable defs as "static" sub-defs of structs
        if (parent_node_type == AST_STMT_STRUCT_DEF) {
            return RegisteredDefResult{.maybe_did = did, .statik = true};
        }
    }
    if (force_return_did) {
        return RegisteredDefResult{.maybe_did = did};
    }
    return {};
}

void FileAstVisitor::register_top_level_stmts(ScopeId scope, ast_slice_of_stmts_t stmts,
                                              OptId<DefId> parent, abi_lang abi,
                                              GenericState gen_state) {
    for (size_t i = 0; i < stmts.len; i++) {
        register_top_level_stmt(scope, stmts.start[i], parent, abi, gen_state, false);
    }
}
void FileAstVisitor::register_top_level_stmts_registering_ordered_members(
    DefId parent_def, ScopeId scope, ast_slice_of_stmts_t stmts, OptId<DefId> parent, abi_lang abi,
    GenericState gen_state) {
    llvm::SmallVector<DefId> ordered_def_vec{};
    llvm::SmallVector<DefId> static_def_vec{};
    for (size_t i = 0; i < stmts.len; i++) {
        const auto res
            = register_top_level_stmt(scope, stmts.start[i], parent, abi, gen_state, false);
        if (res.maybe_did.has_value()) {
            if (res.statik) {
                // sets static sub-defs
                static_def_vec.push_back(res.maybe_did.as_id());
            } else {
                // set ordered defs for things like unions, structs, and variants
                context.def(res.maybe_did.as_id()).member_idx
                    = ordered_def_vec.size(); // set member_idx
                ordered_def_vec.push_back(res.maybe_did.as_id());
            }
        }
    }

    // handle no members
    if (ordered_def_vec.empty() && !gen_state.not_instantiating_but_in_generic()) {
        const ast_stmt_t* st = context.def_ast_node(parent_def);
        const ast_stmt_type_e statement_type = st->type;
        switch (statement_type) {
        case AST_STMT_STRUCT_DEF:
            break;
        case AST_STMT_UNION_DEF:
            context.emplace_diagnostic(Span{context, file, top_level_info_for(st).name_tkn},
                                       diag_code::empty_union, diag_type::error);

            break;
        case AST_STMT_VARIANT_DEF:
            context.emplace_diagnostic(Span{context, file, top_level_info_for(st).name_tkn},
                                       diag_code::empty_variant, diag_type::error);
            break;
        case AST_STMT_CONTRACT_DEF:
            // this is actually expected to be empty since it's just method decls
            break; // we're done here
        default:
            pretty_print_stmt(st);
            assert(false && "tried to register ordered defs for an invalid type");
            break;
        }
    } else {
        // register the defs
        context.register_ordered_defs(parent_def, ordered_def_vec);
    }
    // try register static defs
    if (!static_def_vec.empty()) {
        context.register_static_defs(parent_def, static_def_vec);
    }
}
TopLevelInfo FileAstVisitor::top_level_info_for(const ast_stmt_t* stmt) {
    scope_kind kind = scope_kind::variable;
    token_t* scope_prefix_tkn = nullptr;
    token_t* name_tkn = nullptr;
    std::optional<ast_slice_of_stmts_t> stmts{};
    bool is_orderable_field = false;
    bool is_generic = false;
    bool do_not_insert_in_scope = false;
    switch (stmt->type) {
    // internal scopes are deffered -----------------------------------
    case AST_STMT_STRUCT_DEF: {
        name_tkn = stmt->stmt.struct_decl.name;
        stmts = stmt->stmt.struct_decl.fields;
        kind = scope_kind::type;
        is_generic = stmt->stmt.struct_decl.is_generic;
        break;
    }
    case AST_STMT_CONTRACT_DEF: {
        name_tkn = stmt->stmt.contract_decl.name;
        stmts = stmt->stmt.contract_decl.fields;
        is_generic = stmt->stmt.contract_decl.is_generic;
        kind = scope_kind::type;
        break;
    }
    case AST_STMT_UNION_DEF: {
        name_tkn = stmt->stmt.union_decl.name;
        stmts = stmt->stmt.union_decl.fields;
        kind = scope_kind::type;
        break;
    }
    case AST_STMT_VARIANT_DEF: {
        name_tkn = stmt->stmt.variant_decl.name;
        stmts = stmt->stmt.variant_decl.fields;
        kind = scope_kind::type;
        is_generic = stmt->stmt.variant_decl.is_generic;
        break;
    }
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    // !!! orderable fields ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    case AST_STMT_VARIANT_FIELD_DECL: {
        name_tkn = stmt->stmt.variant_field_decl.name;
        kind = scope_kind::type;
        is_orderable_field = true;
        break;
    }
    case AST_STMT_VAR_DECL: {
        name_tkn = stmt->stmt.var_decl.name;
        kind = scope_kind::variable;
        is_orderable_field = true;
        break;
    }
    case AST_STMT_VAR_INIT_DECL: {
        name_tkn = stmt->stmt.var_init_decl.name;
        kind = scope_kind::variable;
        is_orderable_field = true;
        break;
    }
        // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    case AST_STMT_FN_DECL: {
        token_ptr_slice_t name_slice = stmt->stmt.fn_decl.name;
        // struct prefix name resolution deffered to later stages
        if (name_slice.len == 2) {
            scope_prefix_tkn = name_slice.start[0];
            name_tkn = name_slice.start[1];
        } else if (name_slice.len == 1) {
            name_tkn = name_slice.start[0];
        }
        kind = scope_kind::variable;
        is_generic = stmt->stmt.fn_decl.is_generic;
        break;
    }
    case AST_STMT_FN_PROTOTYPE: {
        // guranteed to be just one long
        name_tkn = stmt->stmt.fn_prototype.name.start[0];
        kind = scope_kind::variable;
        is_generic = stmt->stmt.fn_prototype.is_generic;
        break;
    }
    case AST_STMT_DEFTYPE:
        name_tkn = stmt->stmt.deftype.alias_id;
        kind = scope_kind::type;
        break;
    case AST_STMT_USE: {
        token_ptr_slice_t id_slice = stmt->stmt.use.id;
        name_tkn = id_slice.start[id_slice.len - 1];
        kind = (stmt->stmt.use.mod) ? scope_kind::namespacee : scope_kind::type;
        do_not_insert_in_scope = true; // should be deffered
        break;
    }
        // handled by special case ~~~~~~~~~~
    case AST_STMT_FILE:
    case AST_STMT_EXTERN_BLOCK:
    case AST_STMT_MODULE:
    case AST_STMT_VISIBILITY_MODIFIER:
    case AST_STMT_COMPT_MODIFIER:
    case AST_STMT_STATIC_MODIFIER:
    case AST_STMT_ALIGNAS_MODIFIER:
    case AST_STMT_IMPORT:
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    case AST_STMT_BLOCK:
    case AST_STMT_EXPR:
    case AST_STMT_EMPTY:
    case AST_STMT_BREAK:
    case AST_STMT_IF:
    case AST_STMT_ELSE:
    case AST_STMT_WHILE:
    case AST_STMT_FOR:
    case AST_STMT_FOR_IN:
    case AST_STMT_RETURN:
    case AST_STMT_YIELD:
    case AST_STMT_CONTINUE:
    case AST_STMT_INVALID:
        break;
    }
    return TopLevelInfo{.scope_prefix_tkn = scope_prefix_tkn,
                        .name_tkn = name_tkn,
                        .stmts = stmts,
                        .kind = kind,
                        .is_orderable_var = is_orderable_field,
                        .is_generic = is_generic,
                        .do_not_insert_in_scope = do_not_insert_in_scope};
}

std::optional<const token_t*> FileAstVisitor::name_of_ast_decl(const ast_stmt_t* stmt) {
    const token_t* tkn_ptr = top_level_info_for(stmt).name_tkn;
    return (tkn_ptr) ? tkn_ptr : std::optional<const token_t*>{};
}

// some helpers

bool is_lower(const token_t* s) { return !is_capital(s); }
bool is_capital(const token_t* s) { return s->start[0] >= 'A' && s->start[0] <= 'Z'; }
std::optional<abi_lang> abi_for_extern_stmt(const ast_stmt_t* stmt) {
    if (stmt->stmt.extern_block.extern_language == nullptr) {
        return abi_lang::bear;
    }
    return (stmt->stmt.extern_block.extern_language->len != 0
            && stmt->stmt.extern_block.extern_language->start[0] == 'C')
               ? abi_lang::c
               : std::optional<abi_lang>{};
}
[[nodiscard]] OptId<DefId> FileAstVisitor::lower_generic_stmt(ScopeId scope, const ast_stmt_t* stmt,
                                                              OptId<DefId> parent) {
    return register_top_level_stmt(scope, stmt, parent, abi_lang::bear,
                                   GenericState{.inst_state = gen_instatiation_state::instantiating,
                                                .vis_state = gen_visit_state::outside_generic},
                                   true)
        .maybe_did;
}

} // namespace hir
