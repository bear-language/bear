//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/context.hpp"
#include "cli/args.h"
#include "cli/import_path.h"
#include "compiler/ast/printer.h"
#include "compiler/ast/stmt.h"
#include "compiler/hir/ast_visitor.hpp"
#include "compiler/hir/compt_expr_solver.hpp"
#include "compiler/hir/def.hpp"
#include "compiler/hir/def_visitor.hpp"
#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/file.hpp"
#include "compiler/hir/generic.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/scope.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/parser/token_eaters.h"
#include "compiler/token.h"
#include "utils/ansi_codes.h"
#include "utils/data_arena.hpp"
#include "utils/log.hpp"
#include "llvm/ADT/SmallVector.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stddef.h>
#include <string_view>
#include <thread>
#include <variant>
namespace hir {

static constexpr size_t DEFAULT_ARENA_CAP = 0x10000;
static constexpr size_t DEFAULT_SYMBOL_ARENA_CAP = DEFAULT_ARENA_CAP;
static constexpr size_t DEFAULT_SCOPE_ARENA_CAP = DEFAULT_ARENA_CAP;
static constexpr size_t DEFAULT_CANONICAL_TYPE_ARENA_CAP = DEFAULT_ARENA_CAP;
static constexpr size_t DEFAULT_CANONICAL_GEN_ARGS_ARENA_CAP = DEFAULT_ARENA_CAP;
static constexpr size_t DEFAULT_ID_MAP_ARENA_CAP
    = 0x10000; // increase if any other top level maps need to be mades
static constexpr size_t DEFAULT_TEMP_SCOPE_ARENA_CAP = 0x10000;
static constexpr size_t DEFAULT_SYM_TO_FILE_ID_MAP_CAP = 0x80;
static constexpr size_t DEFAULT_SCOPE_VEC_CAP = 0x80;
static constexpr size_t DEFAULT_FILE_VEC_CAP = 0x80;
static constexpr size_t DEFAULT_FILE_AST_VEC_CAP = DEFAULT_FILE_VEC_CAP;
static constexpr size_t DEFAULT_FILE_ID_VEC_CAP = 0x200;
static constexpr size_t DEFAULT_SYMBOL_VEC_CAP = 0x800;
static constexpr size_t DEFAULT_EXEC_VEC_CAP = 0x800;
static constexpr size_t DEFAULT_DEF_CAP = 0x800;
static constexpr size_t DEFAULT_TYPE_VEC_CAP = 0x400;
static constexpr size_t DEFAULT_CANONICAL_TYPE_VEC_CAP = 0x100;
static constexpr size_t DEFAULT_GENERIC_ARG_VEC_CAP = 0x400;
static constexpr HirSize EXPECTED_HIGH_NUM_IMPORTS = 128;
static constexpr size_t DEFAULT_DIAG_NUM = 0x100;
static constexpr size_t DEFAULT_DEF_SLICE_COUNT = 0x100;
static constexpr size_t DEFAULT_CANONICAL_TT_CAP = 0x1000;
static constexpr size_t DEFAULT_CANONICAL_GEN_ARGS_CAP = 0x1000;
static constexpr size_t DEFAULT_CANONICAL_COMPT_ARGS_CAP = 0x4000;

Context::Context(const bearc_args_t& args) : Context(args, instances::multiple) {}

std::atomic<bool> Context::one_instance_status = true;

Context::Context(const bearc_args_t& args, instances instances)
    : file_ids{DEFAULT_FILE_ID_VEC_CAP}, files{DEFAULT_FILE_VEC_CAP},
      id_map_arena{DEFAULT_ID_MAP_ARENA_CAP},
      symbol_id_to_file_id_map{id_map_arena, DEFAULT_SYM_TO_FILE_ID_MAP_CAP},
      file_asts{DEFAULT_FILE_AST_VEC_CAP}, importer_to_importees{DEFAULT_FILE_VEC_CAP},
      importee_to_importers{DEFAULT_FILE_VEC_CAP}, file_to_diagnostics{EXPECTED_HIGH_NUM_IMPORTS},
      scope_arena{DEFAULT_SCOPE_ARENA_CAP}, scopes{DEFAULT_SCOPE_VEC_CAP},
      temp_scope_arena{std::make_unique<DataArena>(DEFAULT_TEMP_SCOPE_ARENA_CAP)},
      symbol_storage_arena{DEFAULT_SYMBOL_ARENA_CAP}, symbol_map_arena{DEFAULT_SYMBOL_ARENA_CAP},
      str_to_symbol_id_map{symbol_map_arena}, symbol_ids{DEFAULT_SYMBOL_VEC_CAP},
      symbols{DEFAULT_SYMBOL_VEC_CAP}, exec_ids{DEFAULT_EXEC_VEC_CAP}, execs{DEFAULT_EXEC_VEC_CAP},
      def_ids{DEFAULT_DEF_CAP}, defs{DEFAULT_DEF_CAP}, def_resol_states{DEFAULT_DEF_CAP},
      def_ast_nodes(DEFAULT_DEF_CAP), def_mention_states{DEFAULT_DEF_CAP},
      def_to_scope{id_map_arena, DEFAULT_DEF_CAP},
      def_to_scope_for_funcs{id_map_arena, DEFAULT_DEF_CAP}, ordered_def_slices{DEFAULT_DEF_CAP},
      def_to_ordered_def_slice_id{id_map_arena, DEFAULT_DEF_SLICE_COUNT},
      def_to_static_def_slice_id{id_map_arena, DEFAULT_DEF_SLICE_COUNT},
      struct_did_to_contract_set_id_arena{DEFAULT_ARENA_CAP},
      struct_did_to_contract_set_id(struct_did_to_contract_set_id_arena, DEFAULT_DEF_CAP),
      contract_set_arena{DEFAULT_ARENA_CAP}, contract_sets{DEFAULT_DEF_CAP},
      type_ids{DEFAULT_DEF_CAP}, types{DEFAULT_TYPE_VEC_CAP},
      canonical_to_type_id(DEFAULT_CANONICAL_TYPE_VEC_CAP),
      canonical_type_table_arena{DEFAULT_CANONICAL_TYPE_ARENA_CAP},
      canonical_type_table(*this, canonical_type_table_arena, DEFAULT_CANONICAL_TT_CAP),
      generic_arg_ids{DEFAULT_GENERIC_ARG_VEC_CAP}, generic_args{DEFAULT_GENERIC_ARG_VEC_CAP},
      generic_args_arena{DEFAULT_CANONICAL_TYPE_ARENA_CAP},
      canonical_generic_args_id_to_def_id_map{DEFAULT_CANONICAL_GEN_ARGS_CAP},
      compt_args_arena{DEFAULT_ARENA_CAP},
      canonical_compt_args_id_to_exec_id_map{DEFAULT_CANONICAL_COMPT_ARGS_CAP},
      canonical_generic_args_to_first_instance{DEFAULT_CANONICAL_GEN_ARGS_CAP},
      generic_arg_id_slices{DEFAULT_CANONICAL_GEN_ARGS_CAP},
      canonical_generic_args_table_arena{DEFAULT_CANONICAL_GEN_ARGS_ARENA_CAP},
      canonical_compt_args_table{*this, canonical_generic_args_table_arena,
                                 DEFAULT_CANONICAL_GEN_ARGS_CAP},
      generic_param_ids(DEFAULT_CANONICAL_GEN_ARGS_CAP),
      generic_params(DEFAULT_CANONICAL_GEN_ARGS_CAP),
      def_to_gen_args_arena(DEFAULT_CANONICAL_GEN_ARGS_ARENA_CAP),
      def_to_gen_args(def_to_gen_args_arena, DEFAULT_CANONICAL_GEN_ARGS_CAP),
      diagnostics{DEFAULT_DIAG_NUM}, diagnostics_used{DEFAULT_DIAG_NUM},
      only_one_context_instance((instances == instances::one) && one_instance_status), args{args},
      compact_diagnostics(args.flags[CLI_FLAG_COMPACT_DIAGS]), terse{args.flags[CLI_FLAG_TERSE]},
      strict_syntax{args.flags[CLI_FLAG_STRICT_SYNTAX]} {

    one_instance_status = false; // we exist now

    // this may only fail in horribly malfored arguments in test cases
    assert(args.input_file_name);

    // get try to get root file, and allow checking cwd for it

    std::optional<std::filesystem::path> maybe_root_file
        = resolve_on_import_path(args.input_file_name, ".", &args);

    if (!maybe_root_file) {
        return;
    }

    static constexpr size_t STARTING_FUNCTION_CNT_APPROX = 0x1000; // play it safe
    all_function_dids.reserve(STARTING_FUNCTION_CNT_APPROX);
    static constexpr size_t STARTING_STRUCT_STATIC_DID_CNT = 0x1000; // ^^^
    static_struct_member_dids.reserve(STARTING_STRUCT_STATIC_DID_CNT);

    const std::filesystem::path& root_file = maybe_root_file.value();

    FileId root_id = provide_root_file(root_file.c_str());

    // this is actually slower
    // if (args.import_file_cnt) {
    //    register_import_files_parallel(args.import_files, args.import_file_cnt);
    // }

    // search imports to build all asts
    this->explore_imports(root_id);
    // tally parser errors
    for (FileId id = files.rbegin_id(); id != files.rend_id(); --id) {
        File& f = files.at(id);
        const FileAst& ast = file_asts.at(f.ast_id);
        if (!has_flag(CLI_FLAG_PARSE_ONLY)) {
            FileAstVisitor visitor{*this, id};
            visitor.register_top_level_declarations();
        }
        this->note_cnt += ast.diagnostic_count() - ast.error_count();
        this->fatal_error_cnt += ast.error_count();
    }
    if (has_flag(CLI_FLAG_PARSE_ONLY) || (strict_syntax && error_count())) {
        return;
    }

    TopLevelDefVisitor def_vis{*this};
    def_vis.resolve_top_level_definitions();
    // ensure all functions are lowered
    for (const DefId did : all_function_dids) {
        def_vis.visit_as_independent(did);
    }

    // ensure all static struct member dids are lowered, these are predominately generic
    for (const DefId did : static_struct_member_dids) {
        const auto prior_diag_cnt = diagnostic_count();
        def_vis.visit_as_independent(did);
        if (prior_diag_cnt < diagnostic_count()) {
            const OptId<DefId> maybe_gen_parent = try_generic_parent_for_def(did);
            if (maybe_gen_parent.has_value() && def(maybe_gen_parent.as_id()).holds<DefStruct>()) {
                const Def& d = def(maybe_gen_parent.as_id());
                force_link_diagnostic(emplace_diagnostic(
                    d.span, diag_code::in_generic_instantiation_of_type, diag_type::note,
                    DiagnosticTypeAfterMessage{
                        .tid = emplace_type(TypeStruct{.def_id = maybe_gen_parent.as_id(),
                                                       .gen_args_slice = generic_args_for_def(
                                                           maybe_gen_parent.as_id()),
                                                       .generic = true},
                                            Span::generated(), false)},
                    DiagnosticInfoNoPreview{}));
                force_link_diagnostic(emplace_diagnostic_with_message_value(
                    d.span, diag_code::declared_here_as_generic, diag_type::note,
                    DiagnosticSymbolBeforeMessage{.sid = d.name}));
            }
        }
    }
}

int Context::diagnostic_count() const noexcept {
    return static_cast<int>(fatal_error_cnt + normal_error_cnt + note_cnt + warning_cnt + help_cnt);
}

int Context::error_count() const noexcept {
    return static_cast<int>(this->fatal_error_cnt + this->normal_error_cnt);
}

int Context::warning_count() const noexcept { return static_cast<int>(warning_cnt); }
int Context::note_count() const noexcept { return static_cast<int>(note_cnt); }
int Context::help_count() const noexcept { return static_cast<int>(help_cnt); }

bool Context::compact_diagnostics_enabled() const noexcept { return compact_diagnostics; }

bool Context::has_flag(cli_flag_e flag) const noexcept { return args.flags[flag]; }

SymbolId Context::symbol_id(std::string_view sv) { return symbol_id(sv.data(), sv.length()); }
SymbolId Context::symbol_id(const token_t* tkn) { return symbol_id(tkn->start, tkn->len); }
SymbolId Context::symbol_id(const char* start, size_t len) {
    OptId<SymbolId> maybe_symbol = str_to_symbol_id_map.atn(start, len);
    if (maybe_symbol.has_value()) {
        return maybe_symbol.as_id();
    }
    char* sym_data = this->symbol_storage_arena.alloc_as<char*>(len + 1); // +1 for null-term
    memcpy(sym_data, start, len);
    sym_data[len] = '\0'; // null-term

    // make sym
    SymbolId sym_id = this->symbols.emplace_and_get_id(std::string_view{sym_data, len});
    // register sym into map for future fast and cosistent access
    str_to_symbol_id_map.emplace(sym_data, sym_id);
    // give sym_id now that it's fully interned
    return sym_id;
}
SymbolId Context::concat_symbols(SymbolId sid1, SymbolId sid2) {
    std::string buf{};
    buf.reserve(1024); // decently large
    buf += symbol_id_to_cstr(sid1);
    buf += symbol_id_to_cstr(sid2);
    return symbol_id(buf);
}
SymbolId Context::symbol_id_for_identifier_tkn(const token_t* tkn) {
    assert(token_is_builtin_type_or_id(tkn->type));
    return symbol_id(tkn->start, tkn->len);
}

SymbolId Context::symbol_id(Span span) { return symbol_id(span.as_sv(*this)); }
SymbolId Context::symbol_id_for_str_lit_tkn(const token_t* tkn) {
    assert(tkn->type == TOK_STR_LIT);
    return symbol_id(tkn->start + 1, tkn->len - 2); // trims outer quotes
}

FileId Context::provide_root_file(const char* file_name) {
    SymbolId path_symbol = symbol_id(file_name, strlen(file_name));
    FileId file_id = file(path_symbol);
    return file_id;
}

FileId Context::file(SymbolId path_symbol) {
    // check if file has already been requested
    OptId<FileId> maybe_file_id = symbol_id_to_file_id_map.at(path_symbol);
    if (maybe_file_id.has_value()) {
        return maybe_file_id.as_id();
    }
    // ****************** all lexing and parsing done in this one line
    FileAstId ast_id = this->file_asts.emplace_and_get_id(symbol_id_to_cstr(path_symbol));
    // ^^^^^^^^^^^^^^^^^^
    FileId file_id = this->files.emplace_and_get_id(path_symbol, ast_id);
    /// store this mapping for future detection
    this->symbol_id_to_file_id_map.insert(path_symbol, file_id);
    /// bump necessary things that are track id-wise for files
    importee_to_importers.bump();
    importer_to_importees.bump();
    file_to_diagnostics.bump();
    return file_id;
}

FileId Context::file_parallel(SymbolId path_symbol) {
    {
        std::shared_lock read_lock{import_file_mutex};
        // check if file has already been requested
        OptId<FileId> maybe_file_id = symbol_id_to_file_id_map.at(path_symbol);
        if (maybe_file_id.has_value()) {
            return maybe_file_id.as_id();
        }
    }
    // ****************** all lexing and parsing done in this one line
    FileAst file_ast{symbol_id_to_cstr(
        path_symbol)}; // pure parsing, doesn't touch Context, so this is fully parallelizable
    // ^^^^^^^^^^^^^^^^^^

    std::unique_lock write_lock{import_file_mutex};
    // double-check existing before writing
    OptId<FileId> maybe_file_id = symbol_id_to_file_id_map.at(path_symbol);
    if (maybe_file_id.has_value()) {
        return maybe_file_id.as_id();
    }

    FileAstId ast_id = this->file_asts.emplace_and_get_id(std::move(file_ast));
    FileId file_id = this->files.emplace_and_get_id(path_symbol, ast_id);
    /// store this mapping for future detection
    this->symbol_id_to_file_id_map.insert(path_symbol, file_id);
    /// bump necessary things that are track id-wise for files
    importee_to_importers.bump();
    importer_to_importees.bump();
    file_to_diagnostics.bump();
    return file_id;
}

FileId Context::file(std::filesystem::path& path) {
    SymbolId symbol = symbol_id(path.c_str());
    return file(symbol);
}
FileAstId Context::emplace_ast(const char* file_name) {
    return this->file_asts.emplace_and_get_id(file_name);
}

const char* Context::symbol_id_to_cstr(SymbolId id) const {
    return this->symbols.at(id).sv().data();
}

std::string_view Context::symbol(SymbolId id) const { return this->symbols.at(id).sv(); }

/**
 * updates the slice storing the importers for an importee
 */
void Context::register_importer(FileId importee, FileId importer) {
    importee_to_importers.at(importee).emplace_back(importer);
}

void Context::report_cycle(llvm::SmallVectorImpl<FileId>& import_stack,
                           const token_t* import_path_tkn) {
    // gonna be imported in the previous thing, so top of import stack
    FileId imported_in = import_stack[import_stack.size() - 1];
    emplace_diagnostic(Span{imported_in, ast(imported_in).buffer(), import_path_tkn},
                       diag_code::cyclical_import, diag_type::warning,
                       DiagnosticImportStack{freeze_id_vec(import_stack)});
}
void Context::explore_imports(FileId root_id) {
    llvm::SmallVector<FileId> import_stack{};
    import_stack.push_back(root_id);
    explore_imports(root_id, import_stack, nullptr);
}

void Context::explore_imports(FileId importer_file_id, llvm::SmallVectorImpl<FileId>& import_stack,
                              const token_t* import_path_tkn) {
    auto& file = files.at(importer_file_id);

    // angry base case, guard circularity
    if (file.load_state == file_import_state::in_progress) {
        // safe to take id because a circularity is only possible if a prev id exists
        report_cycle(import_stack, import_path_tkn);
        return;
    }
    // happy base case
    if (file.load_state == file_import_state::done) {
        return;
    }

    const FileAst& root_ast = this->file_asts.at(this->files.at(importer_file_id).ast_id);
    const ast_stmt* root = root_ast.root();
    if (!root) {
        ERR("ast.root() == nullptr");
        return;
    }

    // register this file as in progress to track potential circularity
    file.load_state = file_import_state::in_progress;

    // for tracking importees
    llvm::SmallVector<FileId, EXPECTED_HIGH_NUM_IMPORTS> importees;

    for (size_t i = 0; i < root->stmt.file.stmts.len; i++) {
        ast_stmt* curr = root->stmt.file.stmts.start[i];
        if (curr->type == AST_STMT_IMPORT) {
            OptId<FileId> maybe_importee_file_id
                = this->try_file_from_import_statement(importer_file_id, curr);
            if (!maybe_importee_file_id.has_value()) {
                continue;
            }
            FileId importee_file_id = maybe_importee_file_id.as_id();
            // add importee to vec
            importees.emplace_back(importee_file_id);

            // add to reverse dependency list
            this->register_importer(importee_file_id, importer_file_id);

            // recursively traverse
            import_stack.push_back(importer_file_id);
            const token_t* tkn = curr->stmt.import.file_path;
            this->explore_imports(importee_file_id, import_stack, tkn);
            import_stack.pop_back();
        }
    }

    // set forward dependency list
    auto importer_to_importees_slice = freeze_id_vec(importees);

    importer_to_importees.at(importer_file_id) = importer_to_importees_slice;

    file.load_state = file_import_state::done;
}

void Context::try_print_info() {

    // 1. try print out ast-wise information (token tables, pretty-printing)
    for (auto fid = files.begin_id(); fid != files.end_id(); fid++) {
        ast(fid).try_print_info(args);
    }
    // 2. print more info:
    if (has_flag(CLI_FLAG_FILE_GRAPH)) {
        std::cout << ansi_bold_reset() << "all files" << '(' << files.size() << ')' << ":"
                  << ansi_reset() << '\n';
        for (FileId curr = files.begin_id(); curr != files.end_id(); ++curr) {

            const FileAst& ast = file_asts.at(files.at(curr).ast_id);

            std::cout << ansi_bold_reset() << '[' << curr.raw() << "] " << ast.file_name();
            const auto list = importer_to_importees.at(
                symbol_id_to_file_id_map.at(files.at(curr).path).as_id());

            if (list.len() != 0) {
                std::cout << ": ";
            }
            for (auto imp = list.first(); imp != list.end(); ++imp) {
                if (imp.raw() == 0) {
                    continue;
                }
                FileId importee = file_ids.at(imp); // file_ids.cat(imp);
                std::cout << '[' << importee.raw() << "] "
                          << symbol_id_to_cstr(files.at(importee).path);
                // do this check to avoid trailing comma
                if (imp.raw() != list.end().raw() - 1) {
                    std::cout << ", ";
                }
            }
            std::cout << ansi_reset() << "\n";
        }
    }
    // 3. print diagnostics last (so always seen first in terminal)
    if (!has_flag(CLI_FLAG_SILENT)) {
        // go thru each file ast to print info
        for (auto fid = files.begin_id(); fid != files.end_id(); fid++) {
            const FileAst& aast = ast(fid);
            // 1. print parse-time errors (ast-wise errors)
            aast.print_all_errors(compact_diagnostics_enabled());
            // 2. print diagnostics (semantic/non-grammatical errors)
            // OptId<DiagnosticId> prev_diag{};
            for (const auto d : file_to_diagnostics.at(fid)) {
                /*
                bool print_file = true;
                // check if we should bother printing the file name again
                if (prev_diag.has_value()) {
                    auto curr = diagnostics.cat(d);
                    auto prev = diagnostics.cat(prev_diag.as_id());
                    print_file = curr.span.file_id != prev.span.file_id;
                }
                */
                print_diagnostic(d /*, print_file*/);
                // rotate
                // prev_diag = d;
            }
        }
    }
    if (!has_flag(CLI_FLAG_SILENT)) {
        auto errors = error_count();
        if (errors == 1) {
            puts("1 error generated.");
        } else if (errors != 0) {
            printf("%d errors generated.\n", errors);
        }
        auto warnings = warning_count();
        if (warnings == 1) {
            puts("1 warning generated.");
        } else if (warnings != 0) {
            printf("%d warnings generated.\n", warnings);
        }
        auto notes = note_count();
        if (notes == 1) {
            puts("1 note generated.");
        } else if (notes != 0) {
            printf("%d notes generated.\n", notes);
        }
        auto helps = help_count();
        if (helps == 1) {
            puts("1 tip generated.");
        } else if (helps != 0) {
            printf("%d tips generated.\n", helps);
        }
    }
    // std::cout << tables.files.size() << '\n';
    if (this->diagnostic_count() != 0) {
        if (!has_flag(CLI_FLAG_SILENT)) {
            printf("compilation terminated: %s'%s'\n%s", ansi_bold_reset(),
                   symbol_id_to_cstr(files.at(FileId{1}).path), ansi_reset());
        }
    }
    // release printer's internal state
    pretty_printer_reset();
}

const char* Context::file_name(FileId id) const { return symbol_id_to_cstr(files.at(id).path); }

OptId<FileId> Context::try_file_from_import_statement(FileId importer_id,
                                                      const ast_stmt_t* import_statement) {
    assert(import_statement->type == AST_STMT_IMPORT);
    const token_t* path_tkn = import_statement->stmt.import.file_path;
    SymbolId path_symbol_id = symbol_id_for_str_lit_tkn(path_tkn);
    const char* path = symbol_id_to_cstr(path_symbol_id);

    const std::filesystem::path parent = std::filesystem::path(path).parent_path();

    // DNE guard
    auto maybe_path = resolve_on_import_path(path, parent, &this->args);
    if (!maybe_path.has_value()) {
        emplace_diagnostic(Span(importer_id, ast(importer_id).buffer(), path_tkn),
                           diag_code::imported_file_dne, diag_type::error);
        return OptId<FileId>{};
    }
    return file(maybe_path.value());
}

DefId Context::register_top_level_def(SymbolId name, bool pub, bool compt, bool statik,
                                      bool generic, Span span, const ast_stmt_t* stmt,
                                      OptId<DefId> parent) {
    DefId def = defs.emplace_and_get_id(DefUnevaluated{}, name, pub, compt, statik, generic, span,
                                        parent);
    def_resol_states.bump(Def::resol_state::top_level_visited);
    def_ast_nodes.bump(stmt);
    def_mention_states.bump(Def::mention_state::unused);
    return def;
}

DefId Context::register_compt_def(SymbolId name, Span span, OptId<DefId> parent, DefValue value) {
    DefId def = defs.emplace_and_get_id(value, name, true, true, true, false, span, parent);
    def_resol_states.bump(Def::resol_state::resolved);
    def_ast_nodes.bump();
    def_mention_states.bump(Def::mention_state::unused);
    return def;
}

DefId Context::register_def(SymbolId name, Span span, DefId parent, const ast_stmt_t* stmt,
                            DefValue value) {
    DefId def = defs.emplace_and_get_id(value, name, true, false, false, false, span, parent);
    def_resol_states.bump(Def::resol_state::resolved);
    def_ast_nodes.bump(stmt);
    def_mention_states.bump(Def::mention_state::unused);
    return def;
}

void Context::insert_variable(ScopeId scope_id, SymbolId sid, DefId did) {
    scopes.at(scope_id).insert_variable(sid, did);
}

void Context::insert_type(ScopeId scope_id, SymbolId sid, DefId did) {
    scopes.at(scope_id).insert_type(sid, did);
}

[[nodiscard]] IdHashMap<DefId, ScopeId>& Context::defs_to_scopes() { return def_to_scope; }

[[nodiscard]] DefId Context::begin_def_id() const { return defs.begin_id(); }
[[nodiscard]] DefId Context::end_def_id() const { return defs.end_id(); }

ExecId Context::emplace_exec(const ExecValue& value, Span span, bool should_be_compt) {
    return execs.emplace_and_get_id(*this, value, span, should_be_compt);
}

ExecId Context::emplace_compt_exec(const ExecValue& value, Span span) {
    return execs.emplace_and_get_id(*this, value, span, true);
}

GenericArgId Context::emplace_generic_arg(GenericArgValue value) {
    return generic_args.emplace_and_get_id(value);
}

GenericArgIdSliceId Context::emplace_generic_arg_id_slice(IdSlice<GenericArgId> value) {
    return generic_arg_id_slices.emplace_and_get_id(value);
}

GenericParamId Context::emplace_generic_param(Span span, GenericParamValue value, SymbolId name) {
    return generic_params.emplace_and_get_id(value, name, span);
}

FileAst& Context::ast(FileId file_id) { return file_asts.at(files.at(file_id).ast_id); }

const FileAst& Context::ast(FileId file_id) const { return file_asts.at(files.at(file_id).ast_id); }

ScopeId Context::get_or_make_root_scope() {
    if (scopes.size() == 0) {
        return make_scope(std::nullopt);
    }
    // the top-level scope will have to be the first scope!
    return ScopeId{1};
}

ScopeId Context::root_scope() const {
    // the top-level scope will have to be the first scope!
    assert(this->scopes.size() != 0 && "tried to get the root scope before its creation");
    return ScopeId{1};
}

ScopeId Context::make_scope(OptId<ScopeId> parent_scope) {
    return scopes.emplace_and_get_id(parent_scope, scope_arena);
}

ScopeId Context::make_small_scope(OptId<ScopeId> parent_scope) {
    static constexpr size_t CAP = 0x8;
    return make_scope(parent_scope, CAP);
}

ScopeId Context::make_medium_scope(OptId<ScopeId> parent_scope) {
    static constexpr size_t CAP = 0x10;
    return make_scope(parent_scope, CAP);
}

ScopeId Context::make_scope(OptId<ScopeId> parent_scope, HirSize capacity) {
    return scopes.emplace_and_get_id(parent_scope, capacity, scope_arena);
}

ScopeId Context::make_compt_temp_scope(ScopeId parent_scope, HirSize capacity) {
    return scopes.emplace_and_get_id(parent_scope, capacity, *temp_scope_arena,
                                     Scope::storage::variables);
}

DefId Context::register_generated_deftype(ScopeId scope, SymbolId name, TypeId type_id,
                                          DefId parent, Span span) {
    auto did = defs.emplace_and_get_id(DefDeftype{.type = type_id}, name, false, false, false,
                                       false, span, parent);
    this->scope(scope).insert_type(name, did);
    def_resol_states.bump(Def::resol_state::resolved);
    def_ast_nodes.bump();
    def_mention_states.bump(Def::mention_state::unused);
    return did;
}

bool Context::relinquish_temp_scopes() {

    const size_t chunk_size = temp_scope_arena->first_chunk_size();
    const size_t chunk_cap = temp_scope_arena->chunk_cap();

    // try to catch the arena before it has to alloc a second chunk, but don't do it too undersized.
    // estimate the value with 1/2 + 1/4 + 1/8 + 1/16 = 15/16
    if (chunk_size > ((chunk_cap >> 1) + (chunk_cap >> 2) + (chunk_cap >> 3) + (chunk_cap >> 4))) {
        temp_scope_arena
            = std::make_unique<DataArena>(chunk_cap); // frees old arena and gives us another
        return true;
    }
    return false;
}

Scope& Context::scope(ScopeId scope) { return scopes.at(scope); }

void Context::handle_bump_diag_counts(diag_code code, diag_type type) {
    if (type == diag_type::error) {
        switch (code) {
        case diag_code::imported_file_dne:
            fatal_error_cnt++;
            break;
        default:
            normal_error_cnt++;
            break;
        }
    } else if (type == diag_type::note) {
        note_cnt++;
    } else if (type == diag_type::warning) {
        warning_cnt++;
    } else if (type == diag_type::help) {
        help_cnt++;
    }
}

DiagnosticId Context::emplace_diagnostic(Span span, diag_code code, diag_type type,
                                         OptId<DiagnosticId> next) {
    DiagnosticId id = diagnostics.emplace_and_get_id(span, code, type, next);
    diagnostics_used.bump();
    if (!span.is_generated()) {
        file_to_diagnostics.at(span.file_id).emplace_back(id);
    }
    handle_bump_diag_counts(code, type);
    return id;
}

DiagnosticId Context::emplace_diagnostic(Span span, diag_code code, diag_type type,
                                         DiagnosticInfoValue value, OptId<DiagnosticId> next) {
    DiagnosticId id = diagnostics.emplace_and_get_id(span, code, type, value, next);
    diagnostics_used.bump();
    if (!span.is_generated()) {
        file_to_diagnostics.at(span.file_id).emplace_back(id);
    }
    handle_bump_diag_counts(code, type);
    return id;
}

DiagnosticId Context::emplace_diagnostic(Span span, diag_code code, diag_type type,
                                         DiagnosticMessageValue message_value,
                                         DiagnosticInfoValue value, OptId<DiagnosticId> next) {
    DiagnosticId id = diagnostics.emplace_and_get_id(span, code, type, message_value, value, next);
    diagnostics_used.bump();
    if (!span.is_generated()) {
        file_to_diagnostics.at(span.file_id).emplace_back(id);
    }
    handle_bump_diag_counts(code, type);
    return id;
}

DiagnosticId Context::emplace_diagnostic_with_message_value(Span span, diag_code code,
                                                            diag_type type,
                                                            DiagnosticMessageValue message_value,
                                                            OptId<DiagnosticId> next) {
    DiagnosticId id = diagnostics.emplace_and_get_id(span, code, type, message_value,
                                                     DiagnosticNoOtherInfo{}, next);
    diagnostics_used.bump();
    if (!span.is_generated()) {
        file_to_diagnostics.at(span.file_id).emplace_back(id);
    }
    handle_bump_diag_counts(code, type);
    return id;
}

void Context::print_diagnostic(DiagnosticId diag_id, bool print_file) {
    // as to not re-report
    if (diagnostics_used.at(diag_id)) {
        return;
    }
    const Diagnostic& diag = diagnostics.at(diag_id);
    // make sure we always start printing at the start of the chain (even if diagnostics are in
    // different files)
    if (diag.prev.has_value() && !diagnostics_used.at(diag.prev.as_id())) {
        print_diagnostic(diag.prev.as_id());
        return;
    }
    // if terse, only print errors and warnings
    if (!terse || diag.type == diag_type::error || diag.type == diag_type::warning) {
        diag.print(*this, print_file);
    }
    // mark used
    diagnostics_used.at(diag_id) = true;
    // try print next
    if (diag.next.has_value() && !diagnostics_used.at(diag.next.as_id())) {
        // only print next's file if it differs
        Span next_span = diagnostics.at(diag.next.as_id()).span;
        bool print_next_file
            = !next_span.is_generated() && (diag.span.file_id != next_span.file_id);
        print_diagnostic(diag.next.as_id(), print_next_file);
    } else if (!compact_diagnostics_enabled()) {
        std::cout << '\n'; // this makes it so there's a new line between the start and end of
                           // none-contiguous diagnostics, which is more readable
    }
}

void Context::link_diagnostic(DiagnosticId diag, DiagnosticId next) {
    diagnostics.at(diag).set_next(next);
    diagnostics.at(next).set_prev(diag);
}

void Context::force_link_diagnostic(DiagnosticId diag) {
    HirId prev_val = diag.raw() - 1;
    if (prev_val == HIR_ID_NONE) {
        return;
    }
    diagnostics.at(DiagnosticId{prev_val}).set_next(diag);
    diagnostics.at(diag).set_prev(DiagnosticId{prev_val});
}

bool Context::last_diagnostic_chain_contains(diag_code code) {
    if (diagnostics.empty()) {
        return false;
    }
    DiagnosticId curr
        = DiagnosticId{static_cast<HirSize>(diagnostics.size())}; // since diagnostics are 1-indexed
    while (true) {
        const Diagnostic& d = diagnostics.at(curr);
        if (d.code == code) {
            return true;
        }
        if (d.prev.has_value()) {
            curr = d.prev.as_id();
        } else {
            break;
        }
    }
    return false;
}

void Context::try_link_custom_diagnostic(DiagnosticId diag) {
    HirId prev_val = diag.raw() - 1;
    if (prev_val == HIR_ID_NONE) {
        return;
    }
    const DiagnosticId prev{prev_val};
    const Diagnostic& prev_diag = diagnostics.at(prev);
    // if the previous was a failed staic assertion or custom diagnostics, assume they were being
    // chained so the reporting is prettier
    if (std::holds_alternative<DiagnosticCustomComptDiag>(prev_diag.message_value)
        || prev_diag.code == diag_code::static_assertion_failed
        || prev_diag.code == diag_code::static_assertion_failed_colon) {
        diagnostics.at(prev).set_next(diag);
        diagnostics.at(diag).set_prev(prev);
    }
}

Def& Context::def(DefId def_id) { return defs.at(def_id); }

const Def& Context::try_func_def(DefId def_id) const { return def(try_func_did(def_id)); }

DefId Context::try_func_did(DefId def_id) const {
    const Def& def = this->def(def_id);
    if (def.holds<DefVariable>()) {
        DefVariable var = def.as<DefVariable>();
        if (var.compt_value.has_value()) {
            ExecId compt_val = var.compt_value.as_id();
            const Exec& compt_exec = exec(compt_val);
            if (compt_exec.holds<ExecFnPtr>()) {
                return compt_exec.as<ExecFnPtr>().func_def_id;
            }
        }
    }
    return def_id;
}

FileId Context::file_id(IdIdx<FileId> ididx) const { return file_ids.at(ididx); }

void Context::register_ordered_defs(DefId def, llvm::SmallVectorImpl<DefId>& vec) {
    IdSlice<DefId> def_slice = freeze_id_vec(vec);
    OrderedDefSliceId ord_def_slice_id = ordered_def_slices.emplace_and_get_id(def_slice);
    def_to_ordered_def_slice_id.insert(def, ord_def_slice_id);
}

void Context::register_static_defs(DefId def, llvm::SmallVectorImpl<DefId>& vec) {
    IdSlice<DefId> def_slice = freeze_id_vec(vec);
    OrderedDefSliceId ord_def_slice_id = ordered_def_slices.emplace_and_get_id(def_slice);
    def_to_static_def_slice_id.insert(def, ord_def_slice_id);
}

IdSlice<DefId> Context::ordered_defs_for(DefId def_id) {
    auto maybe_odef_slice_id = def_to_ordered_def_slice_id.at(def_id);
    if (!maybe_odef_slice_id.has_value()) {
        // empty slice
        return IdSlice<DefId>{};
    }
    return ordered_def_slices.at(maybe_odef_slice_id.as_id());
}

[[nodiscard]] IdSlice<DefId> Context::static_defs_for(DefId def_id) {
    auto maybe_odef_slice_id = def_to_static_def_slice_id.at(def_id);
    if (!maybe_odef_slice_id.has_value()) {
        // empty slice
        return IdSlice<DefId>{};
    }
    return ordered_def_slices.at(maybe_odef_slice_id.as_id());
}

Def::resol_state Context::resol_state_of(DefId def) const { return def_resol_states.at(def); }

void Context::set_resol_state_of(DefId def, Def::resol_state resol_state) {
    def_resol_states.at(def) = resol_state;
}

Def::mention_state Context::mention_state_of(DefId def) const { return def_mention_states.at(def); }
void Context::promote_mention_state_of(DefId def, Def::mention_state new_mention_state) {

    // make sure used and mutated is set when appropriate
    if ((new_mention_state == Def::mention_state::mutated
         && mention_state_of(def) == Def::mention_state::used)
        || (new_mention_state == Def::mention_state::used
            && mention_state_of(def) == Def::mention_state::mutated)) {
        def_mention_states.at(def) = Def::mention_state::used_and_mutated;
    }

    // only promote (when current is less than new)
    else if (mention_state_of(def) < new_mention_state) {
        def_mention_states.at(def) = new_mention_state;
    }
}

ScopeId Context::scope_for_top_level_def(DefId def_id) const {
    auto hopefully_scope = try_scope_for_top_level_def(def_id);
    if (hopefully_scope.has_value()) {
        return hopefully_scope.as_id();
    }
    ERR(def(def_id).span.as_sv(*this));
    assert(false && "tried to get a scope for a top level def that was incompatible");
    return root_scope();
}

[[nodiscard]] const ast_stmt_t* Context::def_ast_node(DefId def_id) const {
    return def_ast_nodes.at(def_id);
}

[[nodiscard]] bool Context::is_struct_def(DefId def_id) const {
    return def_ast_nodes.at(def_id)->type == AST_STMT_STRUCT_DEF;
}

OptId<ScopeId> Context::try_scope_for_top_level_def(DefId def_id) const {
    const auto& def = defs.at(def_id);
    // no parent means parent scope is root scope
    if (def.holds<DefModule>()) {
        return def.as<DefModule>().scope;
    }
    if (def.holds<DefDeftype>()) {
        const Type& type = this->type(try_decay(def.as<DefDeftype>().type));
        if (type.holds<TypeStruct>()) {
            return def_to_scope.at(type.as<TypeStruct>().def_id);
        }
        if (type.holds<TypeVariant>()) {
            return def_to_scope.at(type.as<TypeVariant>().def_id);
        }
        if (type.holds<TypeUnion>()) {
            return def_to_scope.at(type.as<TypeUnion>().def_id);
        }
    }
    // hopefully found
    return def_to_scope.at(def_id);
}

bool Context::is_top_level_def_with_associated_scope(DefId def_id) const {
    ast_stmt_type_e decl_type = def_ast_nodes.at(def_id)->type;
    return decl_type == AST_STMT_VARIANT_DEF || decl_type == AST_STMT_CONTRACT_DEF
           || decl_type == AST_STMT_STRUCT_DEF || decl_type == AST_STMT_UNION_DEF;
}

FileId Context::def_to_file_id(DefId def) const { return defs.at(def).span.file_id; }

Span Context::make_def_name_span(DefId def, const ast_stmt_t* stmt) const {
    auto fid = def_to_file_id(def);
    auto maybe_name = FileAstVisitor::name_of_ast_decl(stmt);
    if (!maybe_name.has_value()) {
        assert(false && "failed to get name for an AST declaration");
    }
    return Span(fid, ast(fid).buffer(), maybe_name.value());
}

Span Context::make_top_level_def_name_span(DefId def) const {
    return make_def_name_span(def, def_ast_nodes.at(def));
}

const Type& Context::type(IdIdx<TypeId> ididx) const { return type(type_ids.at(ididx)); }
Type& Context::type(IdIdx<TypeId> ididx) { return type(type_ids.at(ididx)); }
const Type& Context::type(TypeId id) const {
    const Type* t = &types.at(id);
    while (t->holds<TypeDeftype>()) {
        t = &types.at(t->as<TypeDeftype>().true_type);
    }
    return *t;
}
Type& Context::type(TypeId id) {
    Type* t = &types.at(id);
    while (t->holds<TypeDeftype>()) {
        t = &types.at(t->as<TypeDeftype>().true_type);
    }
    return *t;
}

TypeId Context::try_decay(TypeId tid) const {
    const Type& type = this->type(tid);
    if (type.holds<TypeRef>()) {
        return type.as<TypeRef>().inner;
    }
    return tid;
}

/// gets the type value without bypassing deftypes
const Type& Context::type_as_mentioned(IdIdx<TypeId> ididx) const {
    return type_as_mentioned(type_ids.at(ididx));
}
/// gets the type value without bypassing deftypes
Type& Context::type_as_mentioned(IdIdx<TypeId> ididx) {
    return type_as_mentioned(type_ids.at(ididx));
}

[[nodiscard]] const Type& Context::type_as_mentioned(TypeId id) const { return types.at(id); }

/// gets the type value without bypassing deftypes
[[nodiscard]] Type& Context::type_as_mentioned(TypeId id) { return types.at(id); }

TypeId Context::type_id(IdIdx<TypeId> tid) const { return type_ids.at(tid); }

CanonicalTypeId Context::emplace_and_get_canonical_type_id(TypeId first_structural_type_id) {
    return canonical_to_type_id.emplace_and_get_id(first_structural_type_id);
}

CanonicalGenericArgsId Context::emplace_and_get_canonical_gen_args_slice_id(
    GenericArgIdSliceId first_structural_slice_id) {
    return canonical_generic_args_to_first_instance.emplace_and_get_id(first_structural_slice_id);
}

GenericArgIdSliceId Context::get_arg_id_for_canonical_arg_id(CanonicalGenericArgsId canon_id) {
    return canonical_generic_args_to_first_instance.at(canon_id);
}

TypeId Context::emplace_type(const TypeValue& value, Span span, bool mut) {
    TypeId tid = types.emplace_and_get_id(value, span, mut);
    // set canonical
    types.at(tid).canonical = canonical_type_table.canonical(tid);
    return tid;
}
[[nodiscard]] const Exec& Context::exec(ExecId id) const { return execs.at(id); }

[[nodiscard]] const Def& Context::def(DefId id) const { return defs.at(id); }

[[nodiscard]] const Exec& Context::exec(IdIdx<ExecId> id) const {
    return execs.at(exec_ids.at(id));
}

[[nodiscard]] GenericArg Context::gen_arg(GenericArgId id) const { return generic_args.at(id); }

[[nodiscard]] GenericArg Context::gen_arg(IdIdx<GenericArgId> id) const {
    return generic_args.at(generic_arg_ids.at(id));
}

[[nodiscard]] GenericArgId Context::gen_arg_id(IdIdx<GenericArgId> id) const {
    return generic_arg_ids.at(id);
}

[[nodiscard]] IdSlice<GenericArgId> Context::gen_arg_id_slice(GenericArgIdSliceId id) const {
    return generic_arg_id_slices.at(id);
}

[[nodiscard]] OptId<DefId> Context::try_def_id_for_type_id(TypeId tid) const {
    const Type& ty = type(tid);
    if (ty.holds<TypeStruct>()) {
        return ty.as<TypeStruct>().def_id;
    }
    if (ty.holds<TypeVariant>()) {
        return ty.as<TypeVariant>().def_id;
    }
    if (ty.holds<TypeUnion>()) {
        return ty.as<TypeUnion>().def_id;
    }
    return {};
}

[[nodiscard]] GenericParam Context::gen_param(GenericParamId id) const {
    return this->generic_params.at(id);
}

[[nodiscard]] GenericParam Context::gen_param(IdIdx<GenericParamId> id) const {
    return this->generic_params.at(this->generic_param_ids.at(id));
}

[[nodiscard]] GenericParamId Context::gen_param_id(IdIdx<GenericParamId> id) const {
    return this->generic_param_ids.at(id);
}

ExecId Context::exec_id(IdIdx<ExecId> id) const { return exec_ids.at(id); }

[[nodiscard]] const Scope& Context::scope(ScopeId sid) const { return scopes.at(sid); }

[[nodiscard]] const Def& Context::def(IdIdx<DefId> id) const { return defs.at(def_ids.at(id)); }

[[nodiscard]] OptId<DefId> Context::try_struct_def(DefId did) const {
    const Def& d = def(did);
    if (d.holds<DefStruct>()) {
        return did;
    }
    if (d.holds<DefDeftype>()) {
        const Type& t = type(d.as<DefDeftype>().type);
        if (t.holds<TypeStruct>()) {
            return t.as<TypeStruct>().def_id;
        }
    }
    return {};
}

[[nodiscard]] OptId<DefId> Context::try_union_def(DefId did) const {
    const Def& d = def(did);
    if (d.holds<DefUnion>()) {
        return did;
    }
    if (d.holds<DefDeftype>()) {
        const Type& t = type(d.as<DefDeftype>().type);
        if (t.holds<TypeUnion>()) {
            return t.as<TypeUnion>().def_id;
        }
    }
    return {};
}

[[nodiscard]] OptId<DefId> Context::try_variant_def(DefId did) const {
    const Def& d = def(did);
    if (d.holds<DefVariant>()) {
        return did;
    }
    if (d.holds<DefDeftype>()) {
        const Type& t = type(d.as<DefDeftype>().type);
        if (t.holds<TypeVariant>()) {
            return t.as<TypeVariant>().def_id;
        }
    }
    return {};
}

[[nodiscard]] DefId Context::def_id(IdIdx<DefId> id) const { return def_ids.at(id); }

OptId<DefId> Context::look_up_variable(ScopeId scope, SymbolId sid) const {
    return Scope::look_up_variable(*this, scope, sid);
}
OptId<DefId> Context::look_up_type(ScopeId scope, SymbolId sid) const {
    return Scope::look_up_type(*this, scope, sid);
}
OptId<DefId> Context::look_up_namespace(ScopeId scope, SymbolId sid) const {
    return Scope::look_up_namespace(*this, scope, sid);
}

OptId<DefId> Context::look_up_local_variable(ScopeId scope, SymbolId sid) const {
    return Scope::look_up_local_variable(*this, scope, sid);
}
OptId<DefId> Context::look_up_local_type(ScopeId scope, SymbolId sid) const {
    return Scope::look_up_local_type(*this, scope, sid);
}
OptId<DefId> Context::look_up_local_namespace(ScopeId scope, SymbolId sid) const {
    return Scope::look_up_local_namespace(*this, scope, sid);
}

OptId<DefId> Context::look_up_scoped(auto on_first, auto on_last, ScopeId scope,
                                     IdSlice<SymbolId> id_slice, Span id_span) {
    ScopeId curr_scope = scope;
    for (IdIdx<SymbolId> sidx = id_slice.begin(); sidx != id_slice.end(); sidx++) {
        SymbolId sid = symbol_ids.at(sidx);
        // base case, last elem should be the variable
        if (sidx == id_slice.last_elem()) {
            OptId<DefId> maybe
                = (sidx == id_slice.begin()) ? on_first(curr_scope, sid) : on_last(curr_scope, sid);
            return maybe.has_value() ? guard_hid(on_first, scope, maybe.as_id(), id_slice, id_span)
                                     : maybe;
        }
        if (auto maybe_mod = (sidx == id_slice.begin()) ? look_up_namespace(curr_scope, sid)
                                                        : look_up_local_namespace(curr_scope, sid);
            maybe_mod.has_value()) {
            curr_scope = def(guard_hid_namespace(
                                 scope, maybe_mod.as_id(),
                                 IdSlice<SymbolId>{id_slice.begin(),
                                                   sidx.raw() + 1 - id_slice.begin().raw()},
                                 id_span))
                             .as<DefModule>()
                             .scope;
        } else if (auto maybe_type = (sidx == id_slice.begin())
                                         ? look_up_type(curr_scope, sid)
                                         : look_up_local_type(curr_scope, sid);
                   maybe_type.has_value()) {
            curr_scope = scope_for_top_level_def(guard_hid_type(
                scope, maybe_type.as_id(),
                IdSlice<SymbolId>{id_slice.begin(), sidx.raw() + 1 - id_slice.begin().raw()},
                id_span));
        } else {
            return {}; // none of those
        }
    }
    // never entered the loop, so not found
    return OptId<DefId>{};
}

OptId<DefId> Context::look_up_scoped_variable(ScopeId scope, IdSlice<SymbolId> id_slice,
                                              Span id_span) {
    return look_up_scoped(
        [this](ScopeId scope, SymbolId sid) { return look_up_variable(scope, sid); },
        [this](ScopeId scope, SymbolId sid) { return look_up_local_variable(scope, sid); }, scope,
        id_slice, id_span);
}

OptId<DefId> Context::look_up_scoped_type(ScopeId scope, IdSlice<SymbolId> id_slice, Span id_span) {
    return look_up_scoped(
        [this](ScopeId scope, SymbolId sid) { return look_up_type(scope, sid); },
        [this](ScopeId scope, SymbolId sid) { return look_up_local_type(scope, sid); }, scope,
        id_slice, id_span);
}
OptId<DefId> Context::look_up_scoped_namespace(ScopeId scope, IdSlice<SymbolId> id_slice,
                                               Span id_span) {
    return look_up_scoped(
        [this](ScopeId scope, SymbolId sid) { return look_up_namespace(scope, sid); },
        [this](ScopeId scope, SymbolId sid) { return look_up_local_namespace(scope, sid); }, scope,
        id_slice, id_span);
}
DefId Context::guard_hid_type(ScopeId scope, DefId did, IdSlice<SymbolId> id_slice, Span id_span) {
    return guard_hid([this](ScopeId scope, SymbolId sid) { return look_up_type(scope, sid); },
                     scope, did, id_slice, id_span);
}

DefId Context::guard_hid_variable(ScopeId scope, DefId did, IdSlice<SymbolId> id_slice,
                                  Span id_span) {
    return guard_hid([this](ScopeId scope, SymbolId sid) { return look_up_variable(scope, sid); },
                     scope, did, id_slice, id_span);
}

DefId Context::guard_hid_namespace(ScopeId scope, DefId did, IdSlice<SymbolId> id_slice,
                                   Span id_span) {
    return guard_hid([this](ScopeId scope, SymbolId sid) { return look_up_namespace(scope, sid); },
                     scope, did, id_slice, id_span);
}

OptId<DefId> Context::look_up_scoped_bypassing_visibility(auto on_first, auto on_last,
                                                          ScopeId scope,
                                                          IdSlice<SymbolId> id_slice) {
    ScopeId curr_scope = scope;
    for (IdIdx<SymbolId> sidx = id_slice.begin(); sidx != id_slice.end(); sidx++) {
        SymbolId sid = symbol_ids.at(sidx);
        // base case, last elem should be the variable
        if (sidx == id_slice.last_elem()) {
            OptId<DefId> maybe
                = (sidx == id_slice.begin()) ? on_first(curr_scope, sid) : on_last(curr_scope, sid);
            return maybe;
        }
        if (auto maybe_mod = (sidx == id_slice.begin()) ? look_up_namespace(curr_scope, sid)
                                                        : look_up_local_namespace(curr_scope, sid);
            maybe_mod.has_value()) {
            curr_scope = def(maybe_mod.as_id()).as<DefModule>().scope;
        } else if (auto maybe_type = (sidx == id_slice.begin())
                                         ? look_up_type(curr_scope, sid)
                                         : look_up_local_type(curr_scope, sid);
                   maybe_type.has_value()) {
            curr_scope = scope_for_top_level_def(maybe_type.as_id());
        } else {
            return {}; // not found
        }
    }
    // not found fallback
    return OptId<DefId>{};
}
OptId<DefId> Context::look_up_scoped_variable_bypassing_visibility(ScopeId scope,
                                                                   IdSlice<SymbolId> id_slice) {
    return look_up_scoped_bypassing_visibility(
        [this](ScopeId scope, SymbolId sid) { return look_up_variable(scope, sid); },
        [this](ScopeId scope, SymbolId sid) { return look_up_local_variable(scope, sid); }, scope,
        id_slice);
}

OptId<DefId> Context::look_up_scoped_type_bypassing_visibility(ScopeId scope,
                                                               IdSlice<SymbolId> id_slice) {
    return look_up_scoped_bypassing_visibility(
        [this](ScopeId scope, SymbolId sid) { return look_up_type(scope, sid); },
        [this](ScopeId scope, SymbolId sid) { return look_up_local_type(scope, sid); }, scope,
        id_slice);
}
OptId<DefId> Context::look_up_scoped_namespace_bypassing_visibility(ScopeId scope,
                                                                    IdSlice<SymbolId> id_slice) {
    return look_up_scoped_bypassing_visibility(
        [this](ScopeId scope, SymbolId sid) { return look_up_namespace(scope, sid); },
        [this](ScopeId scope, SymbolId sid) { return look_up_local_namespace(scope, sid); }, scope,
        id_slice);
}

OptId<DefId> Context::look_up_member_var_guarding_hid(const Def& struct_def, SymbolId symbol_id,
                                                      Span id_span, ScopeId local_scope) {
    assert(struct_def.holds<DefStruct>());
    auto maybe_def
        = Scope::look_up_local_variable(*this, struct_def.as<DefStruct>().scope, symbol_id);
    if (maybe_def.empty()) {
        auto d0 = emplace_diagnostic_with_message_value(
            id_span, diag_code::id_does_not_name_a_member_variable_of, diag_type::error,
            DiagnosticSymbolAfterMessage{.sid = struct_def.name});
        auto d1 = emplace_diagnostic_with_message_value(
            struct_def.span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = struct_def.name});
        link_diagnostic(d0, d1);
        return std::nullopt;
    }
    const Def& def = this->def(maybe_def.as_id());
    if (!def.holds<DefVariable>()) {
        auto d0 = emplace_diagnostic_with_message_value(
            id_span, diag_code::id_does_not_name_a_member_variable_of, diag_type::error,
            DiagnosticSymbolAfterMessage{.sid = struct_def.name});
        auto d1 = emplace_diagnostic_with_message_value(
            def.span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = def.name});
        link_diagnostic(d0, d1);
        return std::nullopt;
    }
    if (!def.is_ordered()) {
        auto d0 = emplace_diagnostic_with_message_value(
            id_span, diag_code::id_names_a_static_mem_thru_dot_for, diag_type::error,
            DiagnosticSymbolAfterMessage{.sid = struct_def.name});
        auto d1 = emplace_diagnostic_with_message_value(
            def.span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = def.name});
        link_diagnostic(d0, d1);
        return std::nullopt;
    }
    if (!def.pub && !scope_has_parent(local_scope, struct_def.as<DefStruct>().scope)) {
        auto d0 = emplace_diagnostic_with_message_value(
            id_span, diag_code::is_declared_hid, diag_type::error,
            DiagnosticSymbolBeforeMessage{.sid = symbol_id});
        auto d1 = emplace_diagnostic_with_message_value(
            def.span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = symbol_id});
        link_diagnostic(d0, d1);
    }
    return maybe_def;
}

OptId<DefId> Context::look_up_member_function_no_diag_except_hid(const Def& struct_def,
                                                                 SymbolId symbol_id, Span id_span,
                                                                 ScopeId local_scope) {
    auto maybe_def
        = Scope::look_up_local_variable(*this, struct_def.as<DefStruct>().scope, symbol_id);
    if (maybe_def.empty()) {
        return std::nullopt;
    }
    const Def& def = this->try_func_def(maybe_def.as_id());
    if (!def.holds<DefFunction>()) {
        return std::nullopt;
    }
    if (!def.pub && !scope_has_parent(local_scope, struct_def.as<DefStruct>().scope)) {
        auto d0 = emplace_diagnostic_with_message_value(
            id_span, diag_code::is_declared_hid, diag_type::error,
            DiagnosticSymbolBeforeMessage{.sid = symbol_id});
        auto d1 = emplace_diagnostic_with_message_value(
            def.span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = symbol_id});
        link_diagnostic(d0, d1);
    }
    return maybe_def;
}

OptId<DefId> Context::look_up_member_var_no_diag_except_hid(const Def& struct_def,
                                                            SymbolId symbol_id, Span id_span,
                                                            ScopeId local_scope) {
    auto maybe_def
        = Scope::look_up_local_variable(*this, struct_def.as<DefStruct>().scope, symbol_id);
    if (maybe_def.empty()) {
        return std::nullopt;
    }
    const Def& def = this->def(maybe_def.as_id());
    if (!def.holds<DefVariable>()) {
        return std::nullopt;
    }
    if (!def.pub && !scope_has_parent(local_scope, struct_def.as<DefStruct>().scope)) {
        auto d0 = emplace_diagnostic_with_message_value(
            id_span, diag_code::is_declared_hid, diag_type::error,
            DiagnosticSymbolBeforeMessage{.sid = symbol_id});
        auto d1 = emplace_diagnostic_with_message_value(
            def.span, diag_code::declared_here, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = symbol_id});
        link_diagnostic(d0, d1);
    }
    return maybe_def;
}

bool Context::scope_has_parent(ScopeId local_scope, ScopeId possible_parent) const {
    ScopeId curr_scope = local_scope;
    while (true) {
        if (curr_scope == possible_parent) {
            return true;
        }
        const Scope* curr = &scope(curr_scope);

        if (curr->parent().empty()) {
            break;
        }
        curr_scope = curr->parent().as_id();
    }
    return false;
}

[[nodiscard]] bool Context::defined_bypassing_visibility(ScopeId scope,
                                                         IdSlice<SymbolId> id_slice) {
    auto maybe_type = look_up_scoped_type_bypassing_visibility(scope, id_slice);
    if (maybe_type.has_value()) {
        return true;
    }
    return look_up_scoped_variable_bypassing_visibility(scope, id_slice).has_value();
}

[[nodiscard]] bool Context::defined(ScopeId scope, IdSlice<SymbolId> id_slice, Span id_span,
                                    bool member) {

    if (member) {
        OptId<DefId> curr_did;
        for (HirSize i = 0; i < id_slice.len(); i++) {
            SymbolId sid = symbol_id(id_slice.get(i));
            OptId<DefId> maybe_did;
            if (curr_did.empty()) {
                maybe_did = look_up_variable(scope, sid);
            } else {
                maybe_did = look_up_member_function_no_diag_except_hid(def(curr_did.as_id()), sid,
                                                                       id_span, scope);
                if (maybe_did.empty()) {
                    maybe_did = look_up_member_var_no_diag_except_hid(def(curr_did.as_id()), sid,
                                                                      id_span, scope);
                }
            }

            if (maybe_did.has_value() && i == id_slice.len() - 1) {
                return true;
            }

            // handle thing.len specifically, kinda work-aroundy
            if (i == 0 && maybe_did.has_value() && id_slice.len() == 2
                && symbol_id<"len">() == symbol_id(id_slice.get(1))) {
                const auto did = maybe_did.as_id();
                const Def& d = def(did);
                if (d.holds<DefVariable>() && d.as<DefVariable>().compt_value.has_value()) {
                    TopLevelDefVisitor def_vis{*this};
                    ComptExprSolver<TopLevelDefVisitor> solver{*this, def_vis};
                    const OptId<TypeId> maybe_tid
                        = solver.infer_type_from_exec(d.as<DefVariable>().compt_value.as_id());
                    if (maybe_tid.has_value()) {
                        const auto ty = type(maybe_tid.as_id());
                        // this is a compt list literal
                        if (ty.holds<TypeArr>()) {
                            return true;
                        }
                    }
                }
                return false;
            }

            if (maybe_did.empty()) {
                return false;
            }
            auto did = maybe_did.as_id();
            const Def& def = this->def(did);
            // try get chained structs
            if (def.holds<DefVariable>()) {
                const DefVariable var_def = def.as<DefVariable>();
                const Type& type = this->type(try_decay(def.as<DefVariable>().type_id));
                if (type.holds<TypeStruct>()) {
                    curr_did = type.as<TypeStruct>().def_id;
                } else if (var_def.compt_value.has_value()) {
                    TopLevelDefVisitor def_vis{*this};
                    ComptExprSolver<TopLevelDefVisitor> solver{*this, def_vis};
                    const OptId<TypeId> maybe_tid
                        = solver.infer_type_from_exec(var_def.compt_value.as_id());
                    if (maybe_tid.has_value()
                        && this->type(maybe_tid.as_id()).holds<TypeStruct>()) {
                        curr_did = this->type(maybe_tid.as_id()).as<TypeStruct>().def_id;
                    } else {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }
        return true;
    }

    // bypass vis the second time around to not warn multiple times
    auto maybe_type = look_up_scoped_type(scope, id_slice, id_span);

    auto err_when_hid = [this, id_slice, id_span](DefId did) {
        const Def& deffy = def(did);
        if (!deffy.pub) {
            auto d0 = emplace_diagnostic_with_message_value(
                id_span, diag_code::is_declared_hid, diag_type::error,
                DiagnosticIdentifierBeforeMessage{.sid_slice = id_slice});
            auto d1 = emplace_diagnostic_with_message_value(
                deffy.span, diag_code::declared_here, diag_type::note,
                DiagnosticIdentifierBeforeMessage{.sid_slice = id_slice});
            link_diagnostic(d0, d1);
        }
    };

    if (maybe_type.has_value()) {
        err_when_hid(maybe_type.as_id());
        return true;
    }
    auto maybe_var = look_up_scoped_variable_bypassing_visibility(scope, id_slice);

    if (maybe_var.has_value()) {
        err_when_hid(maybe_var.as_id());
        return true;
    }
    return false;
}

IdSlice<SymbolId> Context::symbol_slice(token_ptr_slice_t token_slice) {
    llvm::SmallVector<SymbolId> vec{};
    for (size_t i = 0; i < token_slice.len; i++) {
        const token_t* tkn = token_slice.start[i];
        vec.push_back(symbol_id(tkn));
    }
    return freeze_id_vec(vec);
}

ScopeId Context::containing_scope(DefId did) const {
    auto maybe_parent = defs.at(did).parent;
    // ----- base cases (parent w/ scope)---------
    if (!maybe_parent.has_value()) {
        return root_scope();
    }
    DefId parent_id = maybe_parent.as_id();
    const Def& par_def = def(parent_id);
    if (par_def.holds<DefModule>()) {
        return par_def.as<DefModule>().scope;
    }
    if (par_def.holds<DefScopeWrapper>()) {
        return par_def.as<DefScopeWrapper>().scope;
    }
    auto maybe_structure = try_scope_for_top_level_def(parent_id);
    if (maybe_structure.has_value()) {
        return maybe_structure.as_id();
    }
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // recursive up to find parent's parent scope, etc.
    return containing_scope(parent_id);
}

void Context::register_func_to_scope(DefId did, ScopeId scope_id) {
    def_to_scope_for_funcs.insert(did, scope_id);
}

OptId<ScopeId> Context::func_to_scope(DefId did) { return def_to_scope_for_funcs.at(did); }

Span Context::name_span_for_def(DefId did) const {
    const Def& def = this->def(did);
    return Span{*this, def.span.file_id,
                FileAstVisitor::name_of_ast_decl(def_ast_node(did)).value()};
}

SymbolId Context::symbol_id(IdIdx<SymbolId> sididx) const { return symbol_ids.at(sididx); }

bool Context::equivalent_type(TypeId tid1, TypeId tid2) const {
    return type(tid1).canonical == type(tid2).canonical;
}

bool Context::type_inferable_as(TypeId tid1, TypeId tid2) {
    return TypeTransformer<TypeInferer<DoConsiderMut>>{*this}(tid1, tid2);
}

bool Context::equivalent_type_slice(IdSlice<TypeId> s1, IdSlice<TypeId> s2) const {
    if (s1.len() != s2.len()) {
        return false;
    }

    for (HirSize i = 0; i < s1.len(); i++) {
        TypeId t1 = type_id(s1.get(i));
        TypeId t2 = type_id(s2.get(i));
        if (!equivalent_type(t1, t2)) {
            return false;
        }
    }

    return true;
}

bool Context::compatible_contract_params(IdSlice<TypeId> s1, IdSlice<TypeId> s2, DefId struct_id) {
    if (s1.len() != s2.len()) {
        return false;
    }

    if (s1.len() == 0) {
        return true;
    }

    for (HirSize i = 0; i < s1.len(); i++) {
        TypeId t1 = type_id(s1.get(i));
        TypeId t2 = type_id(s2.get(i));
        // the inferable_as use lets structs supply themselves as `Self` for contracts, since
        // contract forward a Self as a deftype to var
        if (!inferable_as_struct(t1, t2, struct_id)) {
            return false;
        }
    }

    return true;
}

bool Context::report_invalid_return_type(TypeId return_tid) {
    const Type& ty = type(return_tid);
    const Type& canon_ty = type(try_decay(return_tid));
    DiagLinker dlinker{*this};
    if (TypeTransformer<TypeContainsVar>{*this}(return_tid)) {
        dlinker.link(emplace_diagnostic_with_message_value(
            ty.span, diag_code::invalid_return_type, diag_type::error,
            DiagnosticTypeAfterMessage{.tid = return_tid}));
        dlinker.link(emplace_diagnostic(ty.span, diag_code::return_type_must_be_an_explicit_type,
                                        diag_type::note, DiagnosticInfoNoPreview{}));
        dlinker.link(emplace_diagnostic(
            ty.span, diag_code::replace_occurences_of_var_with_an_explicit_type, diag_type::help));
        return true;
    }
    if (canon_ty.holds<TypeBuiltin>() && canon_ty.as<TypeBuiltin>().type == builtin_type::voidd) {
        dlinker.link(emplace_diagnostic_with_message_value(
            ty.span, diag_code::invalid_return_type, diag_type::error,
            DiagnosticTypeAfterMessage{.tid = return_tid}));
        dlinker.link(emplace_diagnostic(
            ty.span, diag_code::to_return_nothing_omit_the_return_type_entirely, diag_type::help));
        return true;
    }
    return false;
}

/// checks if a Def is a struct without resolving it
bool Context::is_struct(DefId did) const {
    const auto* node = def_ast_node(did);
    if (!node) {
        return false;
    }
    return node->type == AST_STMT_STRUCT_DEF;
}
/// checks if a Def is a struct without resolving it
bool Context::is_union(DefId did) const { return def_ast_node(did)->type == AST_STMT_UNION_DEF; }
/// checks if a Def is a struct without resolving it
bool Context::is_variant(DefId did) const {
    const auto* node = def_ast_node(did);
    if (!node) {
        return false;
    }
    return node->type == AST_STMT_VARIANT_DEF;
}

bool Context::is_function(DefId did) const {
    const auto* node = def_ast_node(did);
    if (!node) {
        return false;
    }
    return node->type == AST_STMT_FN_DECL;
}

bool Context::is_variant_field(DefId did) const {
    const auto* node = def_ast_node(did);
    if (!node) {
        return false;
    }
    return node->type == AST_STMT_VARIANT_FIELD_DECL;
}

bool Context::is_contract(DefId did) const {
    const auto* node = def_ast_node(did);
    if (!node) {
        return false;
    }
    return node->type == AST_STMT_CONTRACT_DEF;
}

bool Context::def_id_slice_contains_def_id(IdSlice<DefId> def_id_slice, DefId def_id) const {
    for (auto didx = def_id_slice.begin(); didx != def_id_slice.end(); didx++) {
        if (this->def_id(didx) == def_id) {
            return true;
        }
    }
    return false;
}

IdSlice<SymbolId> Context::try_singly_qualified_name(DefId did) {
    llvm::SmallVector<SymbolId, 2> sids{};

    const Def& def = this->def(did);

    if (def.parent.has_value()) {
        sids.push_back(this->def(def.parent.as_id()).name);
    }

    sids.push_back(def.name);

    return freeze_id_vec(sids);
}

bool Context::check_variant_field_has_parent(DiagLinker& dl, DefId variant_field_did,
                                             DefId variant_did, Span id_span) {
    if (def(variant_field_did).parent.as_id() != variant_did) {
        dl.link(emplace_diagnostic_with_message_value(
            id_span, diag_code::is_not_a_variation_of_variant, diag_type::error,
            DiagnosticSymbolBeforeAndAfterMessage{.before_sid = def(variant_field_did).name,
                                                  .after_sid = def(variant_did).name}));

        dl.link(
            emplace_diagnostic(id_span, diag_code::is_a_field_of_variant, diag_type::note,
                               DiagnosticSymbolBeforeAndAfterMessage{
                                   .before_sid = def(variant_field_did).name,
                                   .after_sid = def(def(variant_field_did).parent.as_id()).name},
                               DiagnosticInfoNoPreview{}));
        dl.link(emplace_diagnostic_with_message_value(
            def(def(variant_field_did).parent.as_id()).span, diag_code::declared_here,
            diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = def(def(variant_field_did).parent.as_id()).name}));
        return false;
    }
    return true;
}

bool Context::func_sigs_match_for_contract(DefId did1, DefId did2, DefId struct_did) {
    const Def& def1 = def(did1);
    const Def& def2 = def(did2);

    IdSlice<TypeId> param_tids1;
    OptId<TypeId> return_tid1;
    IdSlice<TypeId> param_tids2;
    OptId<TypeId> return_tid2;

    if (def1.holds<DefFunction>()) {
        param_tids1 = def1.as<DefFunction>().param_types;
        return_tid1 = def1.as<DefFunction>().return_type;
    } else if (def1.holds<DefFunctionPrototype>()) {
        param_tids1 = def1.as<DefFunctionPrototype>().param_types;
        return_tid1 = def1.as<DefFunctionPrototype>().return_type;
    } else {
        return false;
    }
    if (def2.holds<DefFunction>()) {
        param_tids2 = def2.as<DefFunction>().param_types;
        return_tid2 = def2.as<DefFunction>().return_type;
    } else if (def2.holds<DefFunctionPrototype>()) {
        param_tids2 = def2.as<DefFunctionPrototype>().param_types;
        return_tid2 = def2.as<DefFunctionPrototype>().return_type;
    } else {
        return false;
    }

    if (def1.compt != def2.compt) {
        return false;
    }

    if (def1.pub != def2.pub) {
        return false;
    }

    if (param_tids1.len() != param_tids2.len()) {
        return false;
    }

    if ((return_tid1.has_value() && return_tid2.empty())
        || (return_tid1.empty() && return_tid2.has_value())) {
        return false;
    }

    if (return_tid1.has_value() && return_tid2.has_value()
        && !inferable_as_struct(return_tid1.as_id(), return_tid2.as_id(), struct_did)) {
        return false;
    }

    return compatible_contract_params(param_tids1, param_tids2, struct_did);
}

DiagRange Context::report_function_disagreement_with_contract(DefId contract_fn_proto_did,
                                                              DefId function_did,
                                                              DefId struct_did) {
    const Def& contracts_def = def(contract_fn_proto_did);
    const Def& fn_def = def(function_did);

    DiagLinker dlinker{*this};

    // contract's stuff
    IdSlice<TypeId> ct_param_tids;
    OptId<TypeId> ct_return_tid;

    // function's stuff
    IdSlice<TypeId> fn_param_tids;
    OptId<TypeId> fn_return_tid;

    if (contracts_def.holds<DefFunction>()) {
        ct_param_tids = contracts_def.as<DefFunction>().param_types;
        ct_return_tid = contracts_def.as<DefFunction>().return_type;
    } else if (contracts_def.holds<DefFunctionPrototype>()) {
        ct_param_tids = contracts_def.as<DefFunctionPrototype>().param_types;
        ct_return_tid = contracts_def.as<DefFunctionPrototype>().return_type;
    } else {
        return {};
    }
    if (fn_def.holds<DefFunction>()) {
        fn_param_tids = fn_def.as<DefFunction>().param_types;
        fn_return_tid = fn_def.as<DefFunction>().return_type;
    } else if (fn_def.holds<DefFunctionPrototype>()) {
        fn_param_tids = fn_def.as<DefFunctionPrototype>().param_types;
        fn_return_tid = fn_def.as<DefFunctionPrototype>().return_type;
    } else {
        return {};
    }

    if (contracts_def.compt && !fn_def.compt) {
        dlinker.link(emplace_diagnostic_with_message_value(
            name_span_for_def(function_did), diag_code::should_be_declared_compt, diag_type::help,
            DiagnosticSymbolBeforeMessage{.sid = fn_def.name}));
    }
    if (!contracts_def.compt && fn_def.compt) {
        dlinker.link(emplace_diagnostic_with_message_value(
            name_span_for_def(function_did), diag_code::should_not_be_declared_compt,
            diag_type::help, DiagnosticSymbolBeforeMessage{.sid = fn_def.name}));
    }

    if (ct_param_tids.len() != fn_param_tids.len()) {
        dlinker.link(emplace_diagnostic_with_message_value(
            name_span_for_def(function_did), diag_code::only_message_value_is_meaningful,
            diag_type::note,
            DiagnosticContractFnExpectedButGotNumParams{
                .contract_fn_name = contracts_def.name,
                .expected_sid = symbol_id(std::to_string(ct_param_tids.len())),
                .got_sid = symbol_id(std::to_string(fn_param_tids.len())),
            }));
    } else {
        for (HirSize i = 0; i < ct_param_tids.len(); ++i) {
            TypeId fn_tid = type_id(fn_param_tids.get(i));
            TypeId ct_tid = type_id(ct_param_tids.get(i));
            if (!inferable_as_struct(fn_tid, ct_tid, struct_did)) {
                dlinker.link(emplace_diagnostic_with_message_value(
                    type(fn_tid).span, diag_code::only_message_value_is_meaningful, diag_type::note,
                    DiagnosticContractFnParamExpectedTyButGot{
                        .expected_tid = ct_tid, .got_tid = fn_tid, .arg_index = i}));
                dlinker.link(emplace_diagnostic_with_message_value(
                    type(fn_tid).span, diag_code::replace_with, diag_type::help,
                    DiagnosticTypeAfterMessageAsMentioned{.tid = ct_tid}));
            }
        }
    }

    if (ct_return_tid.has_value() && fn_return_tid.empty()) {
        dlinker.link(emplace_diagnostic_with_message_value(
            name_span_for_def(function_did),
            diag_code::does_not_have_return_type_but_contracts_function_does, diag_type::note,
            DiagnosticSymbolBeforeMessage{fn_def.name}));
    }

    if (ct_return_tid.empty() && fn_return_tid.has_value()) {
        dlinker.link(emplace_diagnostic_with_message_value(
            type(fn_return_tid.as_id()).span,
            diag_code::has_return_type_but_contracts_function_does_not, diag_type::note,
            DiagnosticSymbolBeforeMessage{fn_def.name}));
    }

    if (contracts_def.pub && !fn_def.pub) {
        dlinker.link(emplace_diagnostic_with_message_value(
            name_span_for_def(function_did), diag_code::should_not_be_declared_hid, diag_type::note,
            DiagnosticSymbolBeforeMessage{.sid = fn_def.name}));
    }

    if (ct_return_tid.has_value() && fn_return_tid.has_value()
        && !inferable_as_struct(ct_return_tid.as_id(), fn_return_tid.as_id(), struct_did)) {
        dlinker.link(emplace_diagnostic_with_message_value(
            type(fn_return_tid.as_id()).span, diag_code::only_message_value_is_meaningful,
            diag_type::note,
            DiagnosticContractFnExpectedRetTyButGot{.contract_fn_name = contracts_def.name,
                                                    .expected_return_tid = ct_return_tid.as_id(),
                                                    .got_return_tid = fn_return_tid.as_id()}));
        dlinker.link(emplace_diagnostic_with_message_value(
            type(fn_return_tid.as_id()).span, diag_code::replace_with, diag_type::help,
            DiagnosticTypeAfterMessage{.tid = ct_return_tid.as_id()}));
    }
    return dlinker.range();
}

OptId<TypeId> Context::self_type_for_fn(ScopeId scope, const ast_stmt_fn_decl_t* fn_decl,
                                        Def& def) {
    OptId<TypeId> maybe_self_type{};
    if (token_is_mt_or_dt(fn_decl->kw->type)) {
        auto maybe_did = look_up_type(scope, symbol_id<"Self">());

        if (maybe_did.has_value() && this->def(maybe_did.as_id()).holds<DefDeftype>()) {
            auto def = this->def(maybe_did.as_id());
            TypeId self_tid = def.as<DefDeftype>().type;
            const auto& ty = type(self_tid);
            self_tid
                = emplace_type(TypeDeftype{.true_type = self_tid, .definition = maybe_did.as_id()},
                               ty.span, false);
            // make a reference
            maybe_self_type = emplace_type(TypeRef{.inner = self_tid}, ty.span, fn_decl->is_mut);
        }
    }

    return maybe_self_type;
}

bool Context::struct_has_contract(DefId struct_did, DefId contract_did) {
    const Def& contract_def = def(contract_did);
    if (!contract_def.holds<DefContract>()) {
        return false;
    }

    if (struct_did_to_contract_set_id.at(struct_did).has_value()) {
        return contract_set_for_struct_did(struct_did).contains(contract_did);
    }
    return false;
}

bool Context::type_has_contract(TypeId tid, DefId contract_did) {
    TypeId canon_tid = try_decay(tid);
    const Type& ty = type(canon_tid);
    if (!ty.holds<TypeStruct>()) {
        return false;
    }
    return struct_has_contract(ty.as<TypeStruct>().def_id, contract_did);
}

IdSet<DefId>& Context::contract_set_for_struct_did(DefId struct_did) {
    const auto maybe_set_id = struct_did_to_contract_set_id.at(struct_did);
    // one doesn't already exist, so make one
    if (maybe_set_id.empty()) {
        const auto set_id
            = contract_sets.emplace_and_get_id(contract_set_arena, 32); // decent capacity
        struct_did_to_contract_set_id.insert(struct_did, set_id);
        return contract_sets.at(set_id);
    }
    return contract_sets.at(maybe_set_id.as_id());
}

bool Context::inferable_as_struct(TypeId tid1, TypeId tid2, DefId struct_did) {
    const Type& t1 = type(tid1);
    const Type& t2 = type(tid2);
    if (t1.mut != t2.mut) {
        return false;
    }

    // guard mismatched references
    if ((t1.holds<TypeRef>() || t2.holds<TypeRef>()) && !t1.holds_same_variant_type(t2)) {
        return false;
    }
    const TypeId tid1_canonical = try_decay(tid1);
    const TypeId tid2_canonical = try_decay(tid2);
    if (t1.holds<TypeVar>()) {
        const Type& t2 = type(tid2_canonical);
        if (!t2.holds<TypeStruct>()) {
            return false;
        }
        if (t2.as<TypeStruct>().def_id != struct_did) {
            return false;
        }
    }
    if (t2.holds<TypeVar>()) {
        const Type& t1 = type(tid1_canonical);
        if (!t1.holds<TypeStruct>()) {
            return false;
        }
        if (t1.as<TypeStruct>().def_id != struct_did) {
            return false;
        }
    }
    return type_inferable_as(tid1, tid2);
}

bool Context::type_matches_struct_def(TypeId tid, DefId did) {
    const TypeId canon_tid = this->try_decay(tid);
    const Type& ty = type(canon_tid);
    if (!ty.holds<TypeStruct>()) {
        return false;
    }
    return ty.as<TypeStruct>().def_id == did; // only matches if did is also a struct, of course
}

OptId<DefId> Context::try_def_for_type(TypeId tid) const {
    const Type& t = type(tid);
    if (t.holds<TypeStruct>()) {
        return t.as<TypeStruct>().def_id;
    }
    if (t.holds<TypeVariant>()) {
        return t.as<TypeVariant>().def_id;
    }
    if (t.holds<TypeUnion>()) {
        return t.as<TypeUnion>().def_id;
    }
    return {};
}

OptId<CanonicalGenericArgsId> Context::generic_args_for_def(DefId did) const {
    const Def& d = def(did);
    if (d.holds<DefFunction>()) {
        return d.as<DefFunction>().maybe_generic_args;
    }
    if (d.holds<DefVariant>()) {
        return d.as<DefVariant>().maybe_generic_args;
    }
    if (d.holds<DefStruct>()) {
        return d.as<DefStruct>().maybe_generic_args;
    }
    if (d.holds<DefContract>()) {
        return d.as<DefContract>().maybe_generic_args;
    }

    return {};
}

CanonicalGenericArgsIdMapId Context::make_generic_args_map_and_get_id() {
    static constexpr auto DEFAULT_CAP = 8uz;
    // this is emplacing a new IdHashMap
    return this->canonical_generic_args_id_to_def_id_map.emplace_and_get_id(
        canonical_generic_args_table_arena, DEFAULT_CAP);
}

IdHashMap<CanonicalGenericArgsId, DefId>&
Context::generic_args_map(CanonicalGenericArgsIdMapId id) {
    return this->canonical_generic_args_id_to_def_id_map.at(id);
}

OptId<DefId> Context::linear_name_match_in_def_slice(IdSlice<DefId> defs, SymbolId name) const {
    for (auto didx = defs.begin(); didx != defs.end(); didx++) {
        if (def(didx).name == name) {
            return def_id(didx);
        }
    }
    return {};
}

CanonicalGenericArgsId Context::canonical_gen_args(GenericArgIdSliceId slice_id) {
    return this->canonical_compt_args_table.canonical(slice_id);
}

[[nodiscard]] std::optional<IdSlice<GenericParamId>>
Context::try_generic_params_for_def(DefId did) const {
    const Def& def = this->def(did);
    if (def.holds<DefGenericStruct>()) {
        return def.as<DefGenericStruct>().generic_params;
    }
    if (def.holds<DefGenericFunction>()) {
        return def.as<DefGenericFunction>().generic_params;
    }
    if (def.holds<DefGenericVariant>()) {
        return def.as<DefGenericVariant>().generic_params;
    }
    if (def.holds<DefGenericContract>()) {
        return def.as<DefGenericContract>().generic_params;
    }

    return {};
}

[[nodiscard]] OptId<GenericArgIdSliceId>
Context::try_generic_args_for_def_recursive(DefId did) const {
    const Def* curr_def = &def(did);
    while (true) {
        auto maybe_generic_args = generic_args_for_def(did);
        if (maybe_generic_args.has_value()) {
            return maybe_generic_args;
        }
        if (curr_def->parent.empty()) {
            return {};
        }
        did = curr_def->parent.as_id();
        curr_def = &def(did);
    }
    return {};
}

[[nodiscard]] OptId<DefId> Context::try_generic_parent_for_def(DefId did) const {
    const Def* curr_def = &def(did);
    while (true) {
        auto maybe_generic_args = generic_args_for_def(did);
        if (maybe_generic_args.has_value()) {
            return did;
        }
        if (curr_def->parent.empty()) {
            return {};
        }
        did = curr_def->parent.as_id();
        curr_def = &def(did);
    }
    return {};
}

[[nodiscard]] HirSize Context::try_num_generic_params_for_def(DefId did) const {
    const ast_stmt_t* st = def_ast_node(did);
    switch (st->type) {
    case AST_STMT_STRUCT_DEF: {
        return st->stmt.struct_decl.generic_params.len;
    }
    case AST_STMT_VARIANT_DEF:
        return st->stmt.variant_decl.generic_params.len;
    case AST_STMT_FN_DECL:
        return st->stmt.fn_decl.generic_params.len;
    case AST_STMT_CONTRACT_DEF:
        return st->stmt.contract_decl.generic_params.len;
    default:
        break;
    }
    return 0;
}

[[nodiscard]] IdHashMap<CanonicalGenericArgsId, ExecId>&
Context::compt_args_map(CanonicalComptArgsIdMapId map_id) {
    return canonical_compt_args_id_to_exec_id_map.at(map_id);
}

[[nodiscard]] CanonicalComptArgsIdMapId Context::make_compt_args_map() {
    return canonical_compt_args_id_to_exec_id_map.emplace_and_get_id(compt_args_arena,
                                                                     64); // decently sized
}

Span Context::span_for_gen_args(GenericArgIdSliceId gen_arg_slice) const {

    auto span = [this](GenericArgId gid) {
        Ovld vs{
            [this](ExecId eid) { return exec(eid).span; },
            [this](TypeId tid) { return type(tid).span; },
        };
        return this->gen_arg(gid).visit(vs);
    };

    const auto slice = gen_arg_id_slice(gen_arg_slice);

    const Span span1 = span(gen_arg_id(slice.begin()));

    const Span span2 = span(gen_arg_id(slice.last_elem()));

    return Span::combine(span1, span2);
}

void Context::record_function_def(DefId def_id) { all_function_dids.push_back(def_id); }

void Context::record_static_struct_member_def(DefId def_id) {
    static_struct_member_dids.push_back(def_id);
}

void Context::register_gen_args_for_def(DefId did, GenericArgIdSliceId gen_args_id) {
    def_to_gen_args.insert(did, gen_args_id);
}

OptId<GenericArgIdSliceId> Context::try_gen_args_for_def(DefId did) const {
    return def_to_gen_args.at(did);
}

[[nodiscard]] OptId<GenericArgIdSliceId> Context::search_for_gen_args_for_def(DefId did) const {
    const auto maybe_args = try_gen_args_for_def(did);
    if (maybe_args.has_value()) {
        return maybe_args;
    }

    const Def& d = def(did);
    if (d.parent.has_value()) {
        return search_for_gen_args_for_def(d.parent.as_id());
    }
    return {};
}

[[nodiscard]] bool
Context::def_or_recursive_parent_of_def_is_unspecialized_generic(DefId did) const {
    const Def& d = def(did);
    // positive base case: current def is generic itself
    if (d.generic) {
        return true;
    }
    if (d.parent.empty()) {
        // negative base case: current def is not generic nor has parent
        return false;
    }

    // try recurse
    return def_or_recursive_parent_of_def_is_unspecialized_generic(d.parent.as_id());
}

bool Context::def_has_unspecialed_generic_parent(DefId did) const {
    const Def& d = def(did);

    // no parent so of course no
    if (d.parent.empty()) {
        return false;
    }

    // just recurse on parent with this algo
    return def_or_recursive_parent_of_def_is_unspecialized_generic(d.parent.as_id());
}

void Context::insert_gen_args_into_scope(DefId orginal_generic_did, DefId instance_did,
                                         ScopeId scope, GenericArgIdSliceId gen_args_id) {
    const auto maybe_gen_params = try_generic_params_for_def(orginal_generic_did);
    if (!maybe_gen_params.has_value()) {
        return;
    }
    const auto param_slice = maybe_gen_params.value();
    const auto args_slice = gen_arg_id_slice(gen_args_id);

    auto emplace = [this, scope, instance_did](const GenericParam& param, GenericArg arg) {
        Ovld vs{[this, scope, &param, arg, instance_did](GenericParamType t) {
                    if (arg.holds<TypeId>()) {
                        register_generated_deftype(scope, param.name, arg.as<TypeId>(),
                                                   instance_did, type(arg.as<TypeId>()).span);
                    }
                },
                [this, scope, instance_did, param, arg](GenericParamVariable v) {
                    if (arg.holds<ExecId>()) {
                        const auto p = register_compt_def(
                            param.name, exec(arg.as<ExecId>()).span, instance_did,
                            DefVariable{.type_id = v.type, .compt_value = arg.as<ExecId>()});
                        insert_variable(scope, param.name, p);
                    }
                }};
        param.visit(vs);
    };

    for (HirSize i = 0; i < param_slice.len(); ++i) {
        const auto param = gen_param(param_slice.get(i));
        const auto arg = gen_arg(args_slice.get(i));
        emplace(param, arg);
    }
}

void Context::register_import_files_parallel(const char* const* file_paths, uint32_t count) {
    llvm::SmallVector<SymbolId, 128> symbol_vec{}; // decent size;
    for (uint32_t i = 0; i < count; ++i) {
        const std::optional<std::filesystem::path> maybe_path
            = resolve_on_import_path(file_paths[i], ".", &args);
        if (maybe_path.has_value()) {
            symbol_vec.push_back(symbol_id(maybe_path.value().c_str()));
        }
    }
    const uint32_t n_threads = std::min(count, std::thread::hardware_concurrency());
    std::atomic<uint32_t> curr_index{0};
    auto worker = [this, &curr_index, &symbol_vec] {
        while (true) {
            uint32_t i = curr_index.fetch_add(1, std::memory_order_relaxed);
            if (i >= symbol_vec.size()) {
                return;
            }
            file_parallel(symbol_vec[i]);
        }
    };

    llvm::SmallVector<std::thread> threads{};
    for (uint32_t i = 0; i < n_threads; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }
}

template <IsDefVisitor V> OptId<TypeId> Context::infer_type_from_exec(V& def_visitor, ExecId eid) {
    return ComptExprSolver<V>{*this, def_visitor}.infer_type_from_exec(eid);
}
// concrete instants
template OptId<TypeId>
Context::infer_type_from_exec<InsideBodyDefVisitor>(InsideBodyDefVisitor& def_visitor, ExecId eid);
template OptId<TypeId>
Context::infer_type_from_exec<TopLevelDefVisitor>(TopLevelDefVisitor& def_visitor, ExecId eid);

} // namespace hir
