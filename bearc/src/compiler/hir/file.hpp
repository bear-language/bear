//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_FILE_HPP
#define COMPILER_HIR_FILE_HPP
#include "cli/args.h"
#include "compiler/ast/ast.h"
#include "compiler/ast/stmt_slice.h"
#include "compiler/hir/indexing.hpp"
#include "utils/file_io.h"
#include <cstdint>

namespace hir {

enum class file_import_state : uint8_t { unvisited = 0, in_progress, done };

struct File {
    using id_type = FileId;
    SymbolId path;
    FileAstId ast_id;
    file_import_state load_state = file_import_state::unvisited;
    File(SymbolId path, FileAstId ast_id)
        : path{path}, ast_id{ast_id}, load_state(file_import_state::unvisited) {}
};

class FileAst {
    br_ast_t ast;

  public:
    using id_type = FileAstId;
    const src_buffer* src() const noexcept;
    const char* buffer() const noexcept;
    const char* file_name() const noexcept;
    const compiler_error_list_t& error_list() const noexcept;
    const ast_stmt_t* root() const noexcept;
    void pretty_print() const;
    void print_all_errors(bool compact) const;
    void print_token_table() const;
    void emplace_tokenwise_error(token_t* tkn, error_code_e error_code);
    size_t diagnostic_count() const;
    size_t error_count() const;
    FileAst(const char* file_name);
    FileAst(FileAst&& other) noexcept;
    ~FileAst();
    void try_print_info(const bearc_args_t& args) const;
};

} // namespace hir

#endif
