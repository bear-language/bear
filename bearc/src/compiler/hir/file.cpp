//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/file.hpp"
#include "cli/args.h"
#include "compiler/ast/ast.h"
#include "compiler/ast/printer.h"
#include "compiler/debug.h"
#include "compiler/diagnostics/error_codes.h"
namespace hir {

FileAst::FileAst(const char* file_name) : ast(ast_create_from_file(file_name)) {}
FileAst::FileAst(const char* file_name, const char* src_literal)
    : ast(ast_create_from_string_literal(file_name, src_literal)) {}
FileAst::~FileAst() {
    if (ast.file_stmt_root_node) {
        ast_destroy(&this->ast);
    }
}
FileAst::FileAst(FileAst&& other) noexcept : ast{other.ast} {
    other.ast.file_stmt_root_node = nullptr;
}
const src_buffer* FileAst::src() const noexcept { return &this->ast.src_buffer; }
const compiler_error_list_t& FileAst::error_list() const noexcept { return this->ast.error_list; }
const ast_stmt_t* FileAst::root() const noexcept { return this->ast.file_stmt_root_node; }
void FileAst::pretty_print() const { pretty_print_stmt(this->root()); }
void FileAst::print_all_errors(bool compact) const {
    compiler_error_list_print_all(&this->ast.error_list, compact);
}
size_t FileAst::diagnostic_count() const {
    return compiler_error_list_diagnostic_count(&this->ast.error_list);
}
size_t FileAst::error_count() const {
    return compiler_error_list_error_count(&this->ast.error_list);
}
const char* FileAst::file_name() const noexcept { return this->ast.src_buffer.file_name; }
void FileAst::print_token_table() const {
    print_out_src_buffer(&ast.src_buffer);
    print_out_tkn_table(&ast.tokens);
}
void FileAst::try_print_info(const bearc_args_t& args) const {
    if (!this->root()) {
        return;
    }
    // --token-table
    if (args.flags[CLI_FLAG_TOKEN_TABLE]) {
        this->print_token_table();
    }

    // --pretty-print
    if (args.flags[CLI_FLAG_PRETTY_PRINT]) {
        this->pretty_print();
    }
}

void FileAst::emplace_tokenwise_error(token_t* tkn, error_code_e error_code) {
    compiler_error_list_emplace(&this->ast.error_list, tkn, error_code);
}

const char* FileAst::buffer() const noexcept { return this->ast.src_buffer.data; }

} // namespace hir
