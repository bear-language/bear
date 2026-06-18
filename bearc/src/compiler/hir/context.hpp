//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_TABLES_HPP
#define COMPILER_HIR_TABLES_HPP

#include "cli/args.h"
#include "compiler/ast/stmt.h"
#include "compiler/hir/arena_str_hash_map.hpp"
#include "compiler/hir/ast_visitor.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/exec.hpp"
#include "compiler/hir/file.hpp"
#include "compiler/hir/generic.hpp"
#include "compiler/hir/id_hash_map.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/node_vector.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/token.h"
#include "utils/data_arena.hpp"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <filesystem>
#include <type_traits>

namespace hir {

/**
 * primary data container for hir structures
 * - this model allows for IDs and ID slices with no pointers
 */
class Context {
  public:
    // enum class to initialize Context with additional information that can improve performance
    enum class instances : uint8_t { one, multiple };

    Context(const bearc_args_t& args);
    Context(const bearc_args_t& args, instances instances);
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    int diagnostic_count() const noexcept;
    int error_count() const noexcept;
    int warning_count() const noexcept;
    int note_count() const noexcept;
    int help_count() const noexcept;
    bool compact_diagnostics_enabled() const noexcept;
    bool has_flag(cli_flag_e flag) const noexcept;
    // ----- accessors / emplacers --------
    [[nodiscard]] SymbolId symbol_id(const token_t* tkn);
    [[nodiscard]] SymbolId symbol_id(const char* start, size_t len);
    [[nodiscard]] SymbolId symbol_id(std::string_view sv);
    [[nodiscard]] SymbolId symbol_id_for_identifier_tkn(const token_t* tkn);
    [[nodiscard]] SymbolId symbol_id(Span span);
    /// should be ordered left, right
    [[nodiscard]] SymbolId concat_symbols(SymbolId sid1, SymbolId sid2);
    /// get a symbol, trimming the "" quotes on the outside when interning
    [[nodiscard]] SymbolId symbol_id_for_str_lit_tkn(const token_t* tkn);
    [[nodiscard]] FileId file(SymbolId path);
    [[nodiscard]] FileId file(std::filesystem::path& path);
    [[nodiscard]] const char* file_name(FileId id) const;
    [[nodiscard]] FileAst& ast(FileId file_id);
    [[nodiscard]] const FileAst& ast(FileId file_id) const;

    // ------ scoping -----------
    [[nodiscard]] ScopeId get_or_make_root_scope();
    [[nodiscard]] ScopeId root_scope() const;
    [[nodiscard]] ScopeId make_scope(OptId<ScopeId> parent_scope);
    // makes a named scope with a small capacity
    [[nodiscard]] ScopeId make_small_scope(OptId<ScopeId> parent_scope);
    [[nodiscard]] ScopeId make_medium_scope(OptId<ScopeId> parent_scope);
    [[nodiscard]] ScopeId make_scope(OptId<ScopeId> parent_scope, HirSize capacity);
    [[nodiscard]] ScopeId make_compt_temp_scope(ScopeId parent_scope, HirSize capacity);

    // generate a deftype and insert into the provided scoep
    // - this will forward all references of some identifer to an arbitrary type
    DefId register_generated_deftype(ScopeId scope, SymbolId name, TypeId type_id, DefId parent,
                                     Span span = Span::generated());

    // allow the Context to clean up the memory footprint of temporary scopes
    // only to be called when it is certain that there are no living ScopeIds to temporary scopes
    bool relinquish_temp_scopes();

    [[nodiscard]] Scope& scope(ScopeId scope);
    [[nodiscard]] OptId<DefId> look_up_variable(ScopeId scope, SymbolId sid) const;
    [[nodiscard]] OptId<DefId> look_up_type(ScopeId scope, SymbolId sid) const;
    [[nodiscard]] OptId<DefId> look_up_namespace(ScopeId scope, SymbolId sid) const;

    [[nodiscard]] OptId<DefId> look_up_scoped(auto F, ScopeId scope, IdSlice<SymbolId> id_slice,
                                              Span id_span);

    [[nodiscard]] OptId<DefId> look_up_scoped_generic(auto F, IsDefVisitor auto& def_visitor,
                                                      ScopeId scope, IdSlice<SymbolId> id_slice,
                                                      Span id_span,
                                                      GenericArgIdSliceId gen_args_id);
    [[nodiscard]] OptId<DefId>
    check_to_instatiate_def_on_scoped_lookup(IsDefVisitor auto& def_visitor,
                                             TickableGenArgSlice& targs, DefId orig_did);

    /// finds a variable and attempts to resolve definitions on the way to it
    [[nodiscard]] OptId<DefId> look_up_scoped_variable(ScopeId scope, IdSlice<SymbolId> id_slice,
                                                       Span id_span);
    /// finds a type and attempts to resolve definitions on the way to it
    [[nodiscard]] OptId<DefId> look_up_scoped_type(ScopeId scope, IdSlice<SymbolId> id_slice,
                                                   Span id_span);
    [[nodiscard]] OptId<DefId> look_up_scoped_namespace(ScopeId scope, IdSlice<SymbolId> id_slice,
                                                        Span id_span);
    [[nodiscard]] DefId guard_hid(auto F, ScopeId scope, DefId did, IdSlice<SymbolId> id_slice,
                                  Span id_span);
    [[nodiscard]] DefId guard_hid_type(ScopeId scope, DefId did, IdSlice<SymbolId> id_slice,
                                       Span id_span);
    [[nodiscard]] DefId guard_hid_variable(ScopeId scope, DefId did, IdSlice<SymbolId> id_slice,
                                           Span id_span);
    [[nodiscard]] DefId guard_hid_namespace(ScopeId scope, DefId did, IdSlice<SymbolId> id_slice,
                                            Span id_span);

    [[nodiscard]] OptId<DefId> look_up_scoped_bypassing_visibility(auto F, ScopeId scope,
                                                                   IdSlice<SymbolId> id_slice);

    [[nodiscard]] OptId<DefId>
    look_up_scoped_variable_bypassing_visibility(ScopeId scope, IdSlice<SymbolId> id_slice);
    [[nodiscard]] OptId<DefId> look_up_scoped_type_bypassing_visibility(ScopeId scope,
                                                                        IdSlice<SymbolId> id_slice);

    [[nodiscard]] OptId<DefId>
    look_up_scoped_namespace_bypassing_visibility(ScopeId scope, IdSlice<SymbolId> id_slice);

    [[nodiscard]] bool scope_has_parent(ScopeId local_scope, ScopeId possible_parent) const;

    /// simply looks up a local variable inside some scope and issues a diagnostic if necessary
    /// struct_def must correspond to a Def with value of DefStruct
    /// only returns a DefId if it is a variable definition
    [[nodiscard]] OptId<DefId> look_up_member_var_guarding_hid(const Def& struct_def,
                                                               SymbolId symbol_id, Span id_span,
                                                               ScopeId local_scope);
    [[nodiscard]] OptId<DefId> look_up_member_function_guarding_hid(const Def& struct_def,
                                                                    SymbolId symbol_id,
                                                                    Span id_span,
                                                                    ScopeId local_scope);
    [[nodiscard]] OptId<DefId> look_up_member_function_no_diag_except_hid(const Def& struct_def,
                                                                          SymbolId symbol_id,
                                                                          Span id_span,
                                                                          ScopeId local_scope);

    [[nodiscard]] OptId<DefId> look_up_member_var_no_diag_except_hid(const Def& struct_def,
                                                                     SymbolId symbol_id,
                                                                     Span id_span,
                                                                     ScopeId local_scope);

    [[nodiscard]] bool defined_bypassing_visibility(ScopeId scope, IdSlice<SymbolId> id_slice);

    /// checks if a provided SymbolId slice correpsonds to an existent value or defintion
    /// scope - the scope within which to search for the definition
    /// id_slice - the identifer to search for
    /// id_span - span corresponding to the used ID for diagnostic purposes
    /// member - true: identifer is a structured member (e.g. `foo.bar.a`); false: identifer is a
    /// scope identifier (e.g. `foo..Foo..Bar`)
    [[nodiscard]] bool defined(ScopeId scope, IdSlice<SymbolId> id_slice, Span id_span,
                               bool member);

    /// finds the scope containing a definition
    /// TODO needs to handle non-top level stmts too
    [[nodiscard]] ScopeId containing_scope(DefId did) const;

    void register_func_to_scope(DefId did, ScopeId scope_id);

    [[nodiscard]] OptId<ScopeId> func_to_scope(DefId did);

    [[nodiscard]] Span name_span_for_def(DefId did) const;

    /// record ordered definitions to be frozen as an IdSlice<DefId> corresponding to a DefId
    /// (particularly requiring ordered members, like a struct)
    void register_ordered_defs(DefId def, llvm::SmallVectorImpl<DefId>& vec);
    /// gets the ordered def for a structure's def
    [[nodiscard]] IdSlice<DefId> ordered_defs_for(DefId def);
    /// indicates resolution state of a definition
    [[nodiscard]] Def::resol_state resol_state_of(DefId def) const;
    void set_resol_state_of(DefId def, Def::resol_state resol_state);

    [[nodiscard]] Def::mention_state mention_state_of(DefId def) const;
    // will promote unmentioned to mentioned and mentioned to mutated, but never backwards
    void promote_mention_state_of(DefId def, Def::mention_state mention_state);
    IdSlice<SymbolId> symbol_slice(token_ptr_slice_t token_slice);

    // diagnostics
    void handle_bump_diag_counts(diag_code code, diag_type type);
    DiagnosticId emplace_diagnostic(Span span, diag_code code, diag_type type,
                                    OptId<DiagnosticId> next = OptId<DiagnosticId>{});
    DiagnosticId emplace_diagnostic(Span span, diag_code code, diag_type type,
                                    DiagnosticInfoValue value,
                                    OptId<DiagnosticId> next = OptId<DiagnosticId>{});
    DiagnosticId emplace_diagnostic(Span span, diag_code code, diag_type type,
                                    DiagnosticMessageValue message_value, DiagnosticInfoValue value,
                                    OptId<DiagnosticId> next = OptId<DiagnosticId>{});
    DiagnosticId emplace_diagnostic_with_message_value(Span span, diag_code code, diag_type type,
                                                       DiagnosticMessageValue message_value,
                                                       OptId<DiagnosticId> next
                                                       = OptId<DiagnosticId>{});
    void link_diagnostic(DiagnosticId diag, DiagnosticId next);

    // only use when this diagnostic is known to follow another diagnostic but the previous
    // diagnostic's id is unavailable
    void force_link_diagnostic(DiagnosticId diag);
    void print_diagnostic(DiagnosticId diag, bool print_file = true);
    // type emplacer
    /// emplaces and gets the id from a new CanonicalTypeId c
    /// orresponding to a TypeId which points
    /// to the first structural representation of the canonical type
    [[nodiscard]] CanonicalTypeId
    emplace_and_get_canonical_type_id(TypeId first_structural_type_id);

    [[nodiscard]] CanonicalGenericArgsId
    emplace_and_get_canonical_gen_args_slice_id(GenericArgIdSliceId first_structural_slice_id);

    [[nodiscard]] GenericArgIdSliceId
    get_arg_id_for_canonical_arg_id(CanonicalGenericArgsId canon_id);

    /// emplaces a type, setting its CanonicalTypeId, and returning its TypeId
    [[nodiscard]] TypeId emplace_type(const TypeValue& value, Span span, bool mut);

    /// accessfor for a def thru a DefId
    Def& def(DefId def_id);

    /// trys to access a direct function def or a compt function ptr
    /// basically, if a value is a know compt variable pointing to some known function, we will get
    /// that function's defintion instead of the defintion of the compt variable (of a function
    /// pointer type)
    const Def& try_func_def(DefId def_id) const;
    DefId try_func_did(DefId def_id) const;
    FileId def_to_file_id(DefId def) const;
    Span make_def_name_span(DefId def, const ast_stmt_t* stmt) const;
    Span make_top_level_def_name_span(DefId def) const;

    bool is_top_level_def_with_associated_scope(DefId def_id) const;
    /// gets the named scope for a top level def
    /// - note: NOT the containing scope for a given def.
    [[nodiscard]] ScopeId scope_for_top_level_def(DefId def) const;
    /// trys to look up the scope for a top level def
    [[nodiscard]] OptId<ScopeId> try_scope_for_top_level_def(DefId def) const;

    /// for registering definitions at the top level before resolution
    DefId register_top_level_def(SymbolId name, bool pub, bool compt, bool statik, bool generic,
                                 Span span, const ast_stmt_t* stmt,
                                 OptId<DefId> parent = OptId<DefId>{});

    DefId register_compt_def(SymbolId name, Span span, OptId<DefId> parent,
                             DefValue value = DefUnevaluated{});

    DefId register_def(SymbolId name, Span span, DefId parent, DefValue value = DefUnevaluated{});

    void insert_variable(ScopeId scope_id, SymbolId sid, DefId did);
    void insert_type(ScopeId scope_id, SymbolId sid, DefId did);

    // should only be used for types
    [[nodiscard]] IdHashMap<DefId, ScopeId>& defs_to_scopes_for_types();

    template <typename... Args> [[nodiscard]] ExecId register_exec(Args&&... args) {
        return execs.emplace_and_get_id(std::forward<Args>(args)...);
    }
    [[nodiscard]] ExecId emplace_exec(const ExecValue& value, Span span, bool should_be_compt);

    [[nodiscard]] ExecId emplace_compt_exec(const ExecValue& value, Span span);

    [[nodiscard]] GenericArgId emplace_generic_arg(GenericArgValue value);

    [[nodiscard]] GenericArgIdSliceId emplace_generic_arg_id_slice(IdSlice<GenericArgId> value);

    [[nodiscard]] GenericParamId emplace_generic_param(Span span, GenericParamValue value,
                                                       SymbolId name);

    // ----- info viewing ------

    /// get the c-string corresponding to a SymbolId
    /// - this is guranteed to be null-terminated
    [[nodiscard]] const char* symbol_id_to_cstr(SymbolId id) const;

    // gets the std::string_view associated with a SymbolId
    /// - this is guranteed to be null-terminated
    [[nodiscard]] std::string_view symbol(SymbolId id) const;

    // ------ transformers -----
    void explore_imports(FileId root_id);
    void explore_imports(FileId importer_file_id, llvm::SmallVectorImpl<FileId>& import_stack,
                         const token_t* import_path_tkn);
    /// prints info based on cli-flags
    void try_print_info();

    // converters
    [[nodiscard]] SymbolId symbol_id(IdIdx<SymbolId> sididx) const;
    template <StringLiteral S> [[nodiscard]] SymbolId symbol_id() {
        // when there's only one instance for the life of the program, we can safely cache the
        // symbol_id in static memory for a given string literal as such:
        if (only_one_context_instance) {
            static const SymbolId sid = symbol_id(S.get()); // cache result
            return sid;
        }
        // otherwise, we much compute it for the current context
        return symbol_id(S.get());
    }
    [[nodiscard]] FileId file_id(IdIdx<FileId> ididx) const;
    /// gets the canonical type value (bypassing deftypes)
    [[nodiscard]] const Type& type(IdIdx<TypeId> ididx) const;
    /// gets the canonical type value (bypassing deftypes)
    [[nodiscard]] Type& type(IdIdx<TypeId> ididx);
    /// gets the canonical type value (bypassing deftypes)
    [[nodiscard]] const Type& type(TypeId id) const;
    /// gets the canonical type value (bypassing deftypes)
    [[nodiscard]] Type& type(TypeId id);

    // gets inner tid if tid corresponds to a TypeRef
    [[nodiscard]] TypeId try_decay_ref(TypeId tid) const;

    /// gets the type value without bypassing deftypes
    [[nodiscard]] const Type& type_as_mentioned(IdIdx<TypeId> ididx) const;
    /// gets the type value without bypassing deftypes
    [[nodiscard]] Type& type_as_mentioned(IdIdx<TypeId> ididx);
    /// gets the type value without bypassing deftypes
    [[nodiscard]] const Type& type_as_mentioned(TypeId id) const;
    /// gets the type value without bypassing deftypes
    [[nodiscard]] Type& type_as_mentioned(TypeId id);

    [[nodiscard]] TypeId type_id(IdIdx<TypeId> tid) const;

    [[nodiscard]] bool equivalent_type(TypeId tid1, TypeId tid2) const;

    [[nodiscard]] bool type_inferable_as(TypeId tid1, TypeId tid2) const;

    [[nodiscard]] bool equivalent_type_slice(IdSlice<TypeId> s1, IdSlice<TypeId> s2) const;

    [[nodiscard]] bool compatible_contract_params(IdSlice<TypeId> s1, IdSlice<TypeId> s2,
                                                  DefId struct_id) const;

    // returns true on an invalid return type
    bool report_invalid_return_type(TypeId return_tid);

    [[nodiscard]] const Def& def(DefId id) const;

    [[nodiscard]] const Def& def(IdIdx<DefId> id) const;
    /// try to get a struct DefId, bypassing deftypes
    [[nodiscard]] OptId<DefId> try_struct_def(DefId did) const;

    /// try to get a union DefId, bypassing deftypes
    [[nodiscard]] OptId<DefId> try_union_def(DefId did) const;

    /// try to get a variant DefId, bypassing deftypes
    [[nodiscard]] OptId<DefId> try_variant_def(DefId did) const;

    [[nodiscard]] DefId def_id(IdIdx<DefId> id) const;

    [[nodiscard]] const Exec& exec(ExecId id) const;

    [[nodiscard]] const Exec& exec(IdIdx<ExecId> id) const;

    [[nodiscard]] GenericArg gen_arg(GenericArgId id) const;

    [[nodiscard]] GenericArg gen_arg(IdIdx<GenericArgId> id) const;

    [[nodiscard]] GenericArgId gen_arg_id(IdIdx<GenericArgId> id) const;

    [[nodiscard]] GenericParam gen_param(GenericParamId id) const;

    [[nodiscard]] GenericParam gen_param(IdIdx<GenericParamId> id) const;

    [[nodiscard]] GenericParamId gen_param_id(IdIdx<GenericParamId> id) const;

    [[nodiscard]] IdSlice<GenericArgId> gen_arg_id_slice(GenericArgIdSliceId id) const;

    [[nodiscard]] ExecId exec_id(IdIdx<ExecId> id) const;

    [[nodiscard]] const Scope& scope(ScopeId sid) const;

    [[nodiscard]] const ast_stmt_t* def_ast_node(DefId def_id) const;

    [[nodiscard]] bool is_struct_def(DefId def_id) const;

    [[nodiscard]] DefId begin_def_id() const;

    [[nodiscard]] DefId end_def_id() const;

    /// checks if a Def is a struct without resolving it
    [[nodiscard]] bool is_struct(DefId did) const;
    /// checks if a Def is a union without resolving it
    [[nodiscard]] bool is_union(DefId did) const;
    /// checks if a Def is a variant without resolving it
    [[nodiscard]] bool is_variant(DefId did) const;
    /// checks if a Def is a variant field without resolving it
    [[nodiscard]] bool is_variant_field(DefId did) const;

    [[nodiscard]] bool is_contract(DefId did) const;

    [[nodiscard]] bool is_function(DefId did) const;

    /// try to go from Foo to Bar..Foo
    [[nodiscard]] IdSlice<SymbolId> try_singly_qualified_name(DefId did);

    [[nodiscard]] bool def_id_slice_contains_def_id(IdSlice<DefId> def_id_slice,
                                                    DefId def_id) const;

    /// emplaces diagnostics as appropriate
    /// - returns false on issue
    [[nodiscard]] bool check_variant_field_has_parent(DefId variant_field_did, DefId variant_did,
                                                      Span id_span);

    /// checks if a type can be inferred as another
    /// - ordering of the arguments doesn't matter
    [[nodiscard]] bool inferable_as_struct(TypeId tid1, TypeId tid2, DefId struct_did) const;

    /// checks if two functions signatures match
    /// returns true on match, else false (also returns false if either DefId does not correspond to
    /// a function)
    [[nodiscard]] bool func_sigs_match_for_contract(DefId did1, DefId did2);

    /// does nothing if either passed in DefId isn't a DefFunction or DefFunctionPrototype
    /// - emplaces notes and assumes that an initial primary error has already been emplaced
    /// - returns the first DiagnosticId (to be linked) if it exists
    DiagRange report_function_disagreement_with_contract(DefId contract_fn_proto_did,
                                                         DefId function_did);

    /// checks if a (decayed) type matches a struct def
    [[nodiscard]] bool type_matches_struct_def(TypeId tid, DefId did);

    [[nodiscard]] OptId<CanonicalGenericArgsId> generic_args_for_def(DefId did);

    [[nodiscard]] CanonicalGenericArgsIdMapId make_generic_args_map_and_get_id();

    [[nodiscard]] IdHashMap<CanonicalGenericArgsId, DefId>&
    generic_args_map(CanonicalGenericArgsIdMapId id);

    [[nodiscard]] CanonicalGenericArgsId canonical_gen_args(GenericArgIdSliceId slice_id);

    template <IsDefVisitor V>
    [[nodiscard]] OptId<DefId> try_generic_instantiation(V& def_visitor, DefId def_id,
                                                         GenericArgIdSliceId generic_args_id) {
        if (!validate_gen_args_for_def(def_visitor, def_id, generic_args_id)) {
            return {};
        }

        const Def& def = this->def(def_visitor.visit_as_dependent(def_id));
        CanonicalGenericArgsIdMapId map_id{};
        if (def.holds<DefGenericFunction>()) {
            map_id = def.as<DefGenericFunction>().generics_args_to_concrete_defs_map;
        } else if (def.holds<DefGenericStruct>()) {
            map_id = def.as<DefGenericStruct>().generics_args_to_concrete_defs_map;
        } else if (def.holds<DefGenericVariant>()) {
            map_id = def.as<DefGenericVariant>().generics_args_to_concrete_defs_map;
        } else {
            return {};
        }
        IdHashMap<CanonicalGenericArgsId, DefId>& map = this->generic_args_map(map_id);

        const auto canonical_args = canonical_gen_args(generic_args_id);

        const auto maybe_instance = map.at(canonical_args);

        // attempt new instatiation if there's not an existing one
        if (maybe_instance.empty()) {
            const auto new_instance = make_new_generic_instantiation(
                def_visitor, def_id, canonical_args, generic_args_id);
            if (new_instance.empty()) {
                return {};
            }
            map.insert(canonical_args, new_instance.as_id());
            return new_instance.as_id();
        }
        return maybe_instance.as_id();
    }

    [[nodiscard]] Span span_for_gen_args(GenericArgIdSliceId gen_arg_slice) const;

    /// adds a function to the vector acting as a list of all functions
    void record_function_def(DefId def_id);

    void register_gen_args_for_def(DefId did, GenericArgIdSliceId gen_args_id);

    [[nodiscard]] OptId<GenericArgIdSliceId> try_gen_args_for_def(DefId did) const;

    /// recursively searches parents of Defs to find generic args
    [[nodiscard]] OptId<GenericArgIdSliceId> search_for_gen_args_for_def(DefId did) const;

    /// recursively searches parents of Defs to check if any parent on the chain is a generic,
    /// non-concrete def (which can be problematic if this is a raw use of a generic def)
    [[nodiscard]] bool def_or_recursive_parent_of_def_is_unspecialized_generic(DefId did) const;

    /// checks if a struct has a contract
    /// - defaults to returning false if struct_did does not correspond to a struct and same with
    /// contract_did
    [[nodiscard]] bool struct_has_contract(DefId struct_did, DefId contract_did);

    [[nodiscard]] bool type_has_contract(TypeId tid, DefId contract_did);

    [[nodiscard]] OptId<TypeId> self_type_for_fn(ScopeId scope, const ast_stmt_fn_decl_t* fn_decl,
                                                 Def& def);

    // linearly scans that a name to see if it is contained in a slice of defs
    // - return an empty optional on miss
    [[nodiscard]] OptId<DefId> linear_name_match_in_def_slice(IdSlice<DefId> defs,
                                                              SymbolId name) const;

    // freeze a vector (llvm::SmallVector) into an IdSlice for leaner storage
    template <IsId I>
    [[nodiscard]] IdSlice<I> freeze_id_vec(const llvm::SmallVectorImpl<I>& vec)
        requires is_any_of_v<I, TypeId, ExecId, DefId, GenericArgId, FileId, SymbolId,
                             GenericParamId>
    {
        if constexpr (std::is_same_v<I, TypeId>) {
            return type_ids.freeze_small_vec(vec);
        } else if constexpr (std::is_same_v<I, ExecId>) {
            return exec_ids.freeze_small_vec(vec);
        } else if constexpr (std::is_same_v<I, DefId>) {
            return def_ids.freeze_small_vec(vec);
        } else if constexpr (std::is_same_v<I, GenericArgId>) {
            return generic_arg_ids.freeze_small_vec(vec);
        } else if constexpr (std::is_same_v<I, FileId>) {
            return file_ids.freeze_small_vec(vec);
        } else if constexpr (std::is_same_v<I, SymbolId>) {
            return symbol_ids.freeze_small_vec(vec);
        } else if constexpr (std::is_same_v<I, GenericParamId>) {
            return generic_param_ids.freeze_small_vec(vec);
        } else {
            static_assert(false, "try to freeze a vector of an unconsidered hir::Id type");
        }
    }

  private:
    // containers:
    // ~~~~~~~~~~~~~~~~~ file stuff ~~~~~~~~~~~~~~~~~~~
    IdVector<FileId> file_ids;
    NodeVector<File> files;
    DataArena id_map_arena;
    IdHashMap<SymbolId, FileId> symbol_id_to_file_id_map;
    NodeVector<FileAst> file_asts;

    /// FileId -> IdSlice<FileId> since all importees are always known when lowering a given file
    IdVecMap<FileId, IdSlice<FileId>> importer_to_importees;
    /// FileId -> llvm::SmallVector<FileId> since this will be updated less predictably
    IdVecMap<FileId, llvm::SmallVector<FileId>> importee_to_importers;
    // maps a FileId to a list of diagnostics
    IdVecMap<FileId, llvm::SmallVector<DiagnosticId>> file_to_diagnostics;
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    // for tracking all functions (which are rather important)
    std::vector<DefId> all_function_dids;

    // ~~~~~~~~~~~~~~~~~~~~~ scopes ~~~~~~~~~~~~~~~~~~~~~~~
    DataArena scope_arena;
    NodeVector<Scope> scopes;
    std::unique_ptr<DataArena> temp_scope_arena;
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    /// const char* -> hir::SymbolId
    DataArena symbol_storage_arena;
    DataArena symbol_map_arena;
    StrIdHashMap<SymbolId> str_to_symbol_id_map;
    IdVector<SymbolId> symbol_ids;
    NodeVector<Symbol> symbols;

    IdVector<ExecId> exec_ids;
    NodeVector<Exec> execs;

    // ~~~~ defs ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    IdVector<DefId> def_ids;
    NodeVector<Def> defs;

    /// indicated whether this node is unvisited, visited during top-level resolution, or resolved
    IdVecMap<DefId, Def::resol_state> def_resol_states; // index with DefId
    /// cached dense mapping of DefIds to AST nodes for fast resolution, this mapping should never
    /// be serialized
    IdVecMap<DefId, const ast_stmt_t*> def_ast_nodes;
    /// tracks whether a defintion is used/unused/modified (for tracking dead definitions)
    IdVecMap<DefId, Def::mention_state> def_mention_states;

    /// for tracking DefId -> ScopeId for structs during the top level resolution
    IdHashMap<DefId, ScopeId> def_to_scope_for_types;

    IdHashMap<DefId, ScopeId> def_to_scope_for_funcs;

    // for storing ordered defs (namely ordered member definitions)
    IdVecMap<OrderedDefSliceId, IdSlice<DefId>> ordered_def_slices;
    // for accessing ordered def slices corresponding to a given DefId
    IdHashMap<DefId, OrderedDefSliceId> def_to_ordered_def_slice_id;
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    // types ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    IdVector<TypeId> type_ids;
    NodeVector<Type> types;

    // maps a canonical type back to its first TypeId mention so the type's structure can be rebuilt
    // even if only its canonical value is known
    IdVecMap<CanonicalTypeId, TypeId> canonical_to_type_id;
    DataArena canonical_type_table_arena;
    CanonicalTypeTable canonical_type_table;

    // generics ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    IdVector<GenericArgId> generic_arg_ids;
    NodeVector<GenericArg> generic_args;

    // generic arg canonicalization
    DataArena generic_args_arena;

    // for getting the concrete defs from generic args (routed thru an unspecialized generic defs
    // that has a map to it's child concrete defs)
    IdVecMap<CanonicalGenericArgsIdMapId, IdHashMap<CanonicalGenericArgsId, DefId>>
        canonical_generic_args_id_to_def_id_map;

    // for structurally reverse engineering canonical args
    IdVecMap<CanonicalGenericArgsId, GenericArgIdSliceId> canonical_generic_args_to_first_instance;

    // for keys in the generic arg map
    IdVecMap<GenericArgIdSliceId, IdSlice<GenericArgId>> generic_arg_id_slices;

    // storage for the generic args table
    DataArena canonical_generic_args_table_arena;

    // main table for mapping a GenericArgIdSliceId to a CanonicalGenericArgsId
    CanonicalComptArgsTable canonical_compt_args_table;

    IdVector<GenericParamId> generic_param_ids;
    NodeVector<GenericParam> generic_params;

    DataArena def_to_gen_args_arena;
    IdHashMap<DefId, GenericArgIdSliceId> def_to_gen_args;

    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    // error tracking ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    NodeVector<Diagnostic> diagnostics;
    IdVecMap<DiagnosticId, uint8_t> diagnostics_used;
    HirSize warning_cnt{};
    HirSize note_cnt{};
    HirSize help_cnt{};
    HirSize normal_error_cnt{};
    HirSize fatal_error_cnt{};

    // for checking and setting the local only_one_context_instance on init
    static std::atomic<bool> one_instance_status;

    const bool only_one_context_instance;

    // args
    const bearc_args_t& args;
    bool compact_diagnostics = false;

    [[nodiscard]] FileId provide_root_file(const char* file_name);
    /// forceably emplaces ast, not checking if it has already been processed. This function is
    /// wrapped by file handling logic and should thus not be used directly anywhere else
    [[nodiscard]] FileAstId emplace_ast(const char* file_name);
    void register_importer(FileId importee, FileId importer);
    void report_cycle(llvm::SmallVectorImpl<FileId>& import_stack, const token_t* import_path_tkn);
    [[nodiscard]] OptId<FileId> try_file_from_import_statement(FileId importer_id,
                                                               const ast_stmt_t* import_statement);
    template <IsDefVisitor V>
    [[nodiscard]] OptId<DefId>
    make_new_generic_instantiation(V& def_visitor, DefId did,
                                   CanonicalGenericArgsId canon_gen_args_id,
                                   GenericArgIdSliceId gen_args_id) {
        auto maybe_instance_did = FileAstVisitor{*this, def(did).span.file_id}.lower_generic_stmt(
            containing_scope(did), def_ast_node(did), def(did).parent);
        if (maybe_instance_did.empty()) {
            return {};
        }

        // record the instance's generic args
        register_gen_args_for_def(maybe_instance_did.as_id(), gen_args_id);
        // set up for resolving the instantiated def
        def(maybe_instance_did.as_id()).generic = false; // "concretifies" the instatiation
        // make sure generic args are locable
        ScopeId instance_scope = scope_for_top_level_def(maybe_instance_did.as_id());
        insert_gen_args_into_scope(did, maybe_instance_did.as_id(), instance_scope, gen_args_id);

        // this resolves the def
        if constexpr (std::same_as<V, TopLevelDefVisitor>) {
            def_visitor.visit_as_dependent(maybe_instance_did.as_id());
        } else {
            TopLevelDefVisitor{*this}.visit_as_dependent(maybe_instance_did.as_id());
        }
        if (maybe_instance_did.empty()) {
            return {};
        }

        return maybe_instance_did;
    }

    // assumes generic args have already been validated
    void insert_gen_args_into_scope(DefId orginal_generic_did, DefId instance_did, ScopeId scope,
                                    GenericArgIdSliceId gen_args_id);

    // true on valid, else false
    template <IsDefVisitor V>
    [[nodiscard]] bool validate_gen_args_for_def(V& def_visitor, DefId did,
                                                 GenericArgIdSliceId gen_args_id) {
        if (!def(did).generic) {
            emplace_diagnostic(span_for_gen_args(gen_args_id),
                               diag_code::does_not_take_generic_arguments, diag_type::error,
                               DiagnosticSymbolBeforeMessage{.sid = this->def(did).name},
                               DiagnosticSubCode{.sub_code = diag_code::not_a_generic_type});
            return false;
        }

        def_visitor.visit_as_dependent(did);
        const auto maybe_gen_params = try_generic_params_for_def(did);
        if (!maybe_gen_params.has_value()) {
            return false; // this shouldn't happen, but we need to return false here to protect
                          // invariance
        }
        const auto param_slice = maybe_gen_params.value();

        const IdSlice<GenericArgId> arg_slice = gen_arg_id_slice(gen_args_id);

        if (param_slice.len() != arg_slice.len()) {
            Span span = span_for_gen_args(gen_args_id);
            const auto d0 = emplace_diagnostic_with_message_value(
                span, diag_code::only_message_value_is_meaningful, diag_type::error,
                DiagnosticGenArgsExpectedButGotNumArgs{
                    .name = def(did).name,
                    .expected_sid = symbol_id(std::to_string(param_slice.len())),
                    .got_sid = symbol_id(std::to_string(arg_slice.len()))});
            const auto d1 = emplace_diagnostic_with_message_value(
                def(did).span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = def(did).name});
            link_diagnostic(d0, d1);
            return {};
        }

        DiagLinker dl{*this};
        auto validate_arg
            = [this, &dl, &def_visitor](GenericArgId aid, GenericParamId pid) -> bool {
            Ovld vs{
                [this, pid, &dl](TypeId tid) -> bool {
                    GenericParam param = gen_param(pid);
                    if (param.holds<GenericParamVariable>()) {
                        dl.link(emplace_diagnostic(
                            type(tid).span,
                            diag_code::gen_arg_expected_a_value_expression_not_a_type,
                            diag_type::error));
                        dl.link(emplace_diagnostic_with_message_value(
                            type(tid).span, diag_code::should_a_compt_value_of_type,
                            diag_type::note,
                            DiagnosticTypeAfterMessage{.tid
                                                       = param.as<GenericParamVariable>().type}));
                        dl.link(emplace_diagnostic_with_message_value(
                            param.span, diag_code::declared_here, diag_type::note,
                            DiagnosticSymbolBeforeMessage{.sid = param.name}));
                        return false;
                    }
                    if (param.holds<GenericParamType>()) {
                        const auto contracts = param.as<GenericParamType>().contracts;
                        bool cooked = false;
                        for (auto didx = contracts.begin(); didx != contracts.end(); ++didx) {
                            DefId contract_did = def_id(didx);
                            if (!type_has_contract(tid, contract_did)) {
                                dl.link(emplace_diagnostic_with_message_value(
                                    type(tid).span, diag_code::does_not_satisfy_contract,
                                    diag_type::error,
                                    DiagnosticTyperBeforeMessageAndSymbolAfter{
                                        .sid = def(contract_did).name, .tid = tid}));
                                cooked = true;
                            }
                        }
                        if (cooked) {
                            dl.link(emplace_diagnostic_with_message_value(
                                param.span, diag_code::declared_here, diag_type::note,
                                DiagnosticSymbolBeforeMessage{.sid = param.name}));
                        }
                        return !cooked;
                    }
                    return false; // fallback (unreachable)
                },
                [this, pid, &def_visitor, &dl](ExecId eid) -> bool {
                    GenericParam param = gen_param(pid);
                    if (param.holds<GenericParamVariable>()) {
                        OptId<TypeId> maybe_tid = infer_type_from_exec(def_visitor, eid);

                        const auto expected_tid = param.as<GenericParamVariable>().type;

                        if (maybe_tid.empty()) {
                            dl.link(emplace_diagnostic_with_message_value(
                                exec(eid).span, diag_code::generic_argument_expected_value_of_type,
                                diag_type::error, DiagnosticTypeAfterMessage{.tid = expected_tid}));
                            dl.link(emplace_diagnostic_with_message_value(
                                param.span, diag_code::declared_here, diag_type::note,
                                DiagnosticSymbolBeforeMessage{.sid = param.name}));
                            return false;
                        }
                        if (!equivalent_type(maybe_tid.as_id(), expected_tid)) {
                            dl.link(emplace_diagnostic_with_message_value(
                                exec(eid).span, diag_code::generic_argument_expected_value_of_type,
                                diag_type::error,
                                DiagnosticTyButGot{.expected_tid = expected_tid,
                                                   .got_tid = maybe_tid.as_id()}));
                            dl.link(emplace_diagnostic_with_message_value(
                                param.span, diag_code::declared_here, diag_type::note,
                                DiagnosticSymbolBeforeMessage{.sid = param.name}));

                            return false;
                        }
                        return true;
                    }
                    if (param.holds<GenericParamType>()) {
                        dl.link(emplace_diagnostic(
                            exec(eid).span,
                            diag_code::gen_arg_expected_a_type_not_a_value_expression,
                            diag_type::error));
                        dl.link(emplace_diagnostic_with_message_value(
                            param.span, diag_code::declared_here, diag_type::note,
                            DiagnosticSymbolBeforeMessage{.sid = param.name}));

                        return false;
                    }
                    return false; // fallback (unreachable)
                }};
            return gen_arg(aid).visit(vs);
        };

        bool cooked = false;
        for (HirSize i = 0; i < arg_slice.len(); ++i) {
            cooked |= !validate_arg(gen_arg_id(arg_slice.get(i)), gen_param_id(param_slice.get(i)));
        }
        if (cooked) {
            dl.link(emplace_diagnostic_with_message_value(
                def(did).span, diag_code::declared_here, diag_type::note,
                DiagnosticSymbolBeforeMessage{.sid = def(did).name}));
        }
        return true;
    }

    [[nodiscard]] std::optional<IdSlice<GenericParamId>>
    try_generic_params_for_def(DefId did) const;

    template <IsDefVisitor V>
    [[nodiscard]] OptId<TypeId> infer_type_from_exec(V& def_visitor, ExecId eid);
};

} // namespace hir

#endif
