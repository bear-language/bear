//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/diagnostic.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/type.hpp"
#include "utils/ansi_codes.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <utility>
#include <variant>

namespace hir {

void Diagnostic::print(Context& context, bool print_file) const {
    print_multiline(context, print_file);
}

const char* Diagnostic::message_for_code(enum diag_code c) {
    switch (c) {
    case diag_code::count:
        break;
    case diag_code::redefined_symbol:
        return "redefined symbol";
    case diag_code::previous_def_here:
        return "previous definition here";
    case diag_code::no_matching_struct_for_method:
        return "no matching struct for method declaration";
    case diag_code::capitalized_mod:
        return "capitalized module name";
    case diag_code::lowercase_structure:
        return "lowercase structure name";
    case diag_code::invalid_extern_lang:
        return "unkown extern ABI:";
    case diag_code::imported_file_dne:
        return "imported file does not exist";
    case diag_code::cyclical_import:
        return "cyclical file import detected";
    case diag_code::empty_variant:
        return "empty variant not permitted";
    case diag_code::empty_union:
        return "empty union not permitted";
    case diag_code::circular_definition_origin:
        return "circular definition origin here";
    case diag_code::circular_definition:
        return "circular definition; layout cannot be resolved";
    case diag_code::circular_definition_passes_thru:
        return "circular definition passes through this definition";
    case diag_code::cannot_resolve_value_at_compt:
        return "cannot resolve value at compile-time";
    case diag_code::cannot_convert_type:
        return "type conversion not possible";
    case diag_code::cannot_resolve_at_compt:
        return "cannot resolve expression at compile-time";
    case diag_code::type_not_defined:
        return "type not defined";
    case diag_code::must_initialize_global_variable:
        return "global variables must be initialized";
    case diag_code::compt_variable_should_be_immutable:
        return "compt variables must be immutable";
    case diag_code::cannot_init_with_non_compt_value:
        return "initializer must be a compile-time constant";
    case diag_code::declared_here_without_compt:
        return "declared here without 'compt' specifier";
    case diag_code::not_a_compile_time_constant:
        return "not a compile-time constant";
    case diag_code::use_of_undeclared_identifier:
        return "use of undeclared identifier";
    case diag_code::not_declared_in_this_scope:
        return "not declared in this scope";
    case diag_code::replace_with:
        return "replace with";
    case diag_code::remove:
        return "remove";
    case diag_code::invalid_alignas:
        return "invalid alignas alignment width provided";
    case diag_code::alignas_expr_must_be_a_valid_uint_lit:
        return "alignment must be an unsigned integer literal that is a power of 2 and <= 128.\n"
               "      Specified alignments incompatible with your system's architecture will "
               "default\n"
               "      to their standard values. Consult your architecture and LLVM documentation "
               "for\n"
               "      more details.";
    case diag_code::multiple_alignas_on_one_def:
        return "duplicate `alignas` modifiers on one declaration is malformed";
    case diag_code::redundant_compt_qualifier:
        return "redundant `compt` qualifier";
    case diag_code::redundant_static_qualifier:
        return "redundant `static` qualifier";
    case diag_code::compt_generic_structs_not_possible:
        return "compt generic struct initialization is not yet availible";
    case diag_code::this_feature_is_planned:
        return "this feature is planned";
    case diag_code::is_not_a_struct:
        return "is not a struct";
    case diag_code::declared_here:
        return "declared here";
    case diag_code::struct_field_not_initialized:
        return "struct field not initialized";
    case diag_code::struct_fields_not_initialized:
        return "struct fields not initialized";
    case diag_code::cannot_convert_expression_to_type:
        return "cannot convert expression to type";
    case diag_code::field_initializer_does_not_match_field:
        return "field initializer does not match field";
    case diag_code::too_many_initializers_given_for_struct_init:
        return "too many fields provided in initializer for struct";
    case diag_code::compt_values_cannot_be_moved:
        return "compile-time values cannot be moved";
    case diag_code::var_cannot_be_part_of_a_scoped_identifier:
        return "`var` cannot be part of a scoped identifier";
    case diag_code::compt_variable_should_have_an_explicit_type:
        return "`compt` variables should have an explicit type";
    case diag_code::cannot_convert_value_of_type:
        return "cannot convert value of type";
    case diag_code::cannot_mutate_compt_const:
        return "cannot mutate compile-time constant";
    case diag_code::cannot_cast_expr_to_type:
        return "cannot cast expression to type";
    case diag_code::guaranteed_narrowing_of_compt_value:
        return "guaranteed narrowing of compile-time expression with value";
    case diag_code::invalid_operand_for_binary_expression:
        return "invalid operand for binary expression";
    case diag_code::type_is_not_resolvable_at_compt:
        return "type is not resolvable at compile-time";
    case diag_code::is_declared_hid:
        return "is declared `hid`";
    case diag_code::a_compt_variable_should_be_explicitly_initialized:
        return "should be explicitly initialized since it is a `compt` variable";
    case diag_code::all_runtime_glob_and_mem_vars_need_compt_init:
        return "all run-time global and member variable initializers must be compile-time "
               "constants";
    case diag_code::incompatible_types_for_binary_expression:
        return "incompatible types for binary expression:";
    case diag_code::incompatible_types_for_binary_operator:
        return "incompatible types for binary operator";
    case diag_code::dividing_by_zero_at_compt_is_illegal:
        return "dividing by zero at compile-time is illegal";
    case diag_code::invalid_cast:
        return "invalid cast";
    case diag_code::parentheses_should_be_used_for_chained_casts:
        return "parentheses should be used for chained casts";
    case diag_code::use_of_undeclared_mod:
        return "use of undeclared module";
    case diag_code::operator_not_viable_at_compt:
        return "operator is not viable at compile-time";
    case diag_code::immutable_value_is_not_assignable:
        return "immutable value is not assignable";
    case diag_code::value_is_not_contextually_convertible_to:
        return "value is not contextually convertible to";
    case diag_code::invalid_operand_for_unary_expression:
        return "invalid operand for unary expression";
    case diag_code::mismatched_types_in_list_literal:
        return "mismatched types in list literal";
    case diag_code::is_of_type:
        return "is of type";
    case diag_code::value_is_of_type:
        return "value is of type";
    case diag_code::array_cannot_have_size_zero:
        return "array cannot have a size of zero";
    case diag_code::cannot_infer_type_at_compt:
        return "cannot infer type at compile-time";
    case diag_code::static_assertion_failed:
        return "static assertion failed";
    case diag_code::value_not_a_struct:
        return "value does not correspond to a struct";
    case diag_code::value_does_not_refer_to_a_named_mem:
        return "value does not refer to a named member variable or method";
    case diag_code::id_does_not_name_a_member_variable_of:
        return "identifer does not name a member variable of";
    case diag_code::id_names_a_static_mem_thru_dot_for:
        return "identifer name a static member varirable through the `.` operator for";
    case diag_code::scoped_identifer_not_allowed_here:
        return "scoped identifer not allowed here";
    case diag_code::condition_is_false:
        return "condition is false";
    case diag_code::compt_mut_methods_are_not_permitted:
        return "compile-time `mut` methods are not permitted";
    case diag_code::compt_expression_functions_can_only_act_on_immut_vals:
        return "compile-time expression functions can only act on immutable values";
    case diag_code::compt_function_does_not_yield_a_pure_expr:
        return "compile-time function does not yield a pure expression";
    case diag_code::declare_using_pure_expression_syntax_replacing_body_with:
        return "declare using pure expression syntax, replacing the body with: ";
    case diag_code::invalid_binary_operator:
        return "invalid binary operator";
    case diag_code::compt_expressions_do_not_have_addrs_so_no_ptrs:
        return "compile-time expressions do not have addresses, so dereferencing to a member is "
               "not possible";
    case diag_code::value_is_not_a_pointer:
        return "value is not a pointer";
    case diag_code::id_does_not_name_a_method_of:
        return "identifer does not name a method of";
    case diag_code::value_is_not_callable:
        return "value is not callable";
    case diag_code::not_a_function:
        return "not a function";
    case diag_code::expected:
        return "expected";
    case diag_code::free_function_called_as_a_method:
        return "free function declared `fn` called as a method";
    case diag_code::only_message_value_is_meaningful:
        return "";
    case diag_code::compt_vars_should_not_be_move_initialized:
        return "compile-time variables should not be move-initialized";
    case diag_code::compile_time_constant_cannot_be_moved:
        return "compile-time constant value cannot be moved";
    case diag_code::value_is_a_compile_time_constant:
        return "value is a compile-time constant";
    case diag_code::called_here:
        return "called here";
    case diag_code::pointers_are_not_assignable_at_compt:
        return "pointers are not assignable at compile-time";
    case diag_code::compt_values_cannot_be_mut_borrowed:
        return "compile-time values cannot be mutably borrowed";
    case diag_code::cannot_evaluate_non_pure_expr_fn_at_compt:
        return "cannot evaluate non-pure expression function at compile-time";
    case diag_code::cannot_access_a_member_value:
        return "cannot access a member value";
    case diag_code::tried_to_call_property:
        return "tried to call property";
    case diag_code::value_is_a_compt_list_holding_type:
        return "value is a compile-time list holding type";
    case diag_code::value_is_a_struct_of_type:
        return "value is a struct of type";
    case diag_code::should_have_explicit_type:
        return "should have an explicit type";
    case diag_code::non_compt_function_params_must_have_explicit_types:
        return "non-compt function parameters must have explicit types";
    case diag_code::type_deduction_not_legal_here:
        return "type deduction is not legal here";
    case diag_code::use_a_generic_function_parameter_and_specify_it_as_a_type:
        return "use a generic function parameter and use it as this parameter's type";
    case diag_code::function_signature_does_not_match_contract:
        return "function signature does not match contract's function";
    case diag_code::not_a_contract:
        return "not a contract";
    case diag_code::invalid_contract:
        return "invalid contract";
    case diag_code::has_return_type_but_contracts_function_does_not:
        return "has a return type but contract's function does not";
    case diag_code::does_not_have_return_type_but_contracts_function_does:
        return "does not have a return type but contract's function does";
    case diag_code::declared_in_contract_here:
        return "declared in contract here";
    case diag_code::invalid_return_type:
        return "invalid return type";
    case diag_code::return_type_must_be_an_explicit_type:
        return "return type must be an explicit type";
    case diag_code::void_is_not_a_valid_return_type:
        return "void is not a valid return type";
    case diag_code::to_return_nothing_omit_the_return_type_entirely:
        return "omit the return type entirely to return nothing from a function";
    case diag_code::replace_occurences_of_var_with_an_explicit_type:
        return "replace occurences of `var` with an explicit type";
    case diag_code::immutable_reference_to_mut_value_is_malformed:
        return "immutable reference to mutable value type is malformed as written";
    case diag_code::redundant_mut_reference_to_mut_value:
        return "redundant mutable reference to mutable value type";
    case diag_code::struct_already_has_contract:
        return "struct already has contract";
    case diag_code::should_be_declared_compt:
        return "should be declared `compt`";
    case diag_code::should_not_be_declared_compt:
        return "should not be declared `compt`";
    case diag_code::too_many_initializers_for_union:
        return "too many field initializers for union";
    case diag_code::union_initializers_must_only_set_one_field:
        return "union initializers must only set one field";
    case diag_code::too_few_inits_for_union:
        return "too few field initializers for union";
    case diag_code::does_not_name_a_field_of_union:
        return "field initializer does not name a field of union";
    case diag_code::compt_union_does_not_hold_field:
        return "compile-time union does not hold field";
    case diag_code::compt_union_holds_field:
        return "compile-time union holds field";
    case diag_code::raw_use_of_variant_field:
        return "raw use of variant field";
    case diag_code::variant_fields_cannot_be_used_outside_of_a_variant:
        return "variant fields cannot be used outside of their parent variant";
    case diag_code::invalid_variant_init_for_field:
        return "invalid variant initializer for field";
    case diag_code::not_a_variant:
        return "not a variant";
    case diag_code::cannot_use_is_for_non_variant_values:
        return "cannot use `is` for non-variant values";
    case diag_code::value_does_not_name_a_valid_pattern:
        return "value does not name a valid pattern";
    case diag_code::not_a_variant_field:
        return "not a variant field";
    case diag_code::identifer_does_not_name_a_valid_pattern:
        return "identifier does not name a valid pattern";
    case diag_code::has_no_such_variant_field:
        return "has no such variant field";
    case diag_code::is_a_field_of_variant:
        return "is a field of variant";
    case diag_code::variant_decomposition_not_allowed_here:
        return "variant decomposition not allowed here";
    case diag_code::invalid_pattern:
        return "invalid pattern";
    case diag_code::else_match_branch_should_come_last:
        return "fallback `else` match branch should come last";
    case diag_code::duplicate_else_match_branch:
        return "dupliate `else` match branch";
    case diag_code::duplicate_match_pattern:
        return "duplicate match pattern";
    case diag_code::match_expression_is_not_exhaustive:
        return "match expression is not exhaustive";
    case diag_code::does_not_consider:
        return "does not consider";
    case diag_code::match_expression_may_not_be_exhaustive:
        return "match expression may not be exhaustive";
    case diag_code::add_an_else_branch:
        return "add an else branch";
    case diag_code::too_many_decompositions_for_variant:
        return "too many decompositions for variant";
    case diag_code::cannot_match_type_for_decomposed_variant_from:
        return "cannot match type for decomposed variant from";
    case diag_code::cannot_decompose_multiple_variants_on_the_same_match_arm:
        return "cannot decompose multiple variants on the same match arm";
    case diag_code::does_not_take_generic_arguments:
        return "does not take generic arguments";
    case diag_code::not_a_generic_type:
        return "not a generic type";
    case diag_code::gen_arg_expected_a_value_expression_not_a_type:
        return "generic argument expected a value expression, not a type";
    case diag_code::should_a_compt_value_of_type:
        return "should be a compile-time value of type";
    case diag_code::does_not_satisfy_contract:
        return "does not satisfy contract";
    case diag_code::gen_arg_expected_a_type_not_a_value_expression:
        return "generic argument expected a type, not a value expression";
    case diag_code::generic_argument_expected_value_of_type:
        return "generic argument expected value of type";
    }

    std::unreachable();
    return "";
}

const char* Diagnostic::name_for_type(enum diag_type t) {
    switch (t) {
    case diag_type::error:
        return "error";
    case diag_type::warning:
        return "warning";
    case diag_type::note:
        return "note";
    case diag_type::help:
        return "help";
    }
    return "diagnostic";
}
const char* Diagnostic::accent_color_for_type(enum diag_type t) {
    switch (t) {
    case diag_type::error:
        return ansi_bold_red();
    case diag_type::warning:
        return ansi_bold_yellow();
    case diag_type::note:
        return ansi_bold_cyan();
    case diag_type::help:
        return ansi_bold_green();
    }
    return "";
}

void Diagnostic::print_line(const auto& printable) const {
    std::cout << "  " << std::setw(width(span.line)) << "" << "  | " << printable << '\n';
}

void Diagnostic::print_line_with_number(HirSize line, const auto& printable) const {
    std::cout << "  " << line << "  | " << printable << '\n';
}

std::string Diagnostic::line(int min_width) const {
    std::stringstream ss;
    ss << "  " << std::setw(std::max(min_width, width((span.line)))) << "" << "  | ";
    return ss.str();
}

std::string Diagnostic::diag(int min_width) const {
    std::stringstream ss;
    ss << "  " << std::setw(std::max(min_width, width((span.line)))) << "" << "  \\";
    return ss.str();
}

std::string Diagnostic::line_with_number(HirSize line, int min_width) const {
    std::stringstream ss;
    ss << "  " << std::setw(std::max(min_width, width((span.line)))) << line << "  | ";
    return ss.str();
}

int Diagnostic::width(HirSize line) {
    int w = 1;
    while (line >= 10) {
        line /= 10;
        ++w;
    }
    return w;
}

void Diagnostic::print_info_value(Context& context, HirSize min_width,
                                  bool more_than_one_line) const {

    auto import_stack_helper = [&](IdSlice<FileId> files) {
        auto stream_trace = [&](FileId id, int idx) {
            const char* lore = (idx == 0) ? "[!]   cycle origin: " : "|--> which imports: ";
            const char* clr = (idx == 0) ? ansi_bold_cyan() : "";
            return std::stringstream{} << clr << lore << ansi_bold_reset() << context.file_name(id)
                                       << ansi_reset();
        };
        int idx = 0;
        std::cout << '\n';
        for (auto fid = IdIdx<FileId>{files.begin().raw() + 1}; fid != files.end(); ++fid) {
            print_line(stream_trace(context.file_id(fid), idx).str());
            idx++;
        }
        print_line(stream_trace(context.file_id(files.first()), idx).str());
    };

    auto arrow_helper = [this, min_width, more_than_one_line]() {
        if (more_than_one_line) {
            std::cout << '\n'
                      << line(static_cast<int>(min_width)) << ansi_bold_reset() << "--> "
                      << ansi_reset();
        }
    };

    const auto vs = Ovld{
        [&](DiagnosticNoOtherInfo) { std::cout << '\n'; },
        [&](DiagnosticTypeToType t) {
            arrow_helper();
            std::cout << accent_color_for_type(type) << "cannot convert value of type `"
                      << type_to_string_with_akas(context, t.from) << "` to `"
                      << type_to_string_with_akas(context, t.to) << '`' << ansi_reset() << '\n';
        },
        [&](DiagnosticImportStack import_stack) { import_stack_helper(import_stack.files); },
        [&](DiagnosticSubCode sc) {
            arrow_helper();
            std::cout << accent_color_for_type(type) << message_for_code(sc.sub_code)
                      << ansi_reset() << '\n';
        },
        [](DiagnosticInfoNoPreview) {},
        [](DiagnosticInfoDontDisplayFile) { std::cout << '\n'; },
    };
    this->visit(vs);
    if (!holds<DiagnosticNoOtherInfo>() && !context.compact_diagnostics_enabled()) {
        std::cout << line(static_cast<int>(min_width))
                  << '\n'; // extra line for readibility for stacked diags
    }
}

void Diagnostic::print_multiline(Context& context, bool print_file) const {
    const char* file_name = span.is_generated() ? "" : context.file_name(span.file_id);
    auto adjusted_line = span.line + 1;
    auto adjusted_col = span.col + 1;
    const char* accent_color = accent_color_for_type(type);
    std::string complex_message_str{};
    if (has_complex_message()) {
        build_complex_message(context, complex_message_str);
    }
    const char* message
        = has_complex_message() ? complex_message_str.c_str() : message_for_code(code);
    print_file = print_file && !this->holds<DiagnosticInfoNoPreview>()
                 && !this->holds<DiagnosticInfoDontDisplayFile>(); // no preview means file location
                                                                   // is irrelvant
    if (context.compact_diagnostics_enabled()) {
        if (print_file) {
            printf("%s%s:%u:%u: ", ansi_bold_reset(), file_name, adjusted_line, adjusted_col);
        }
        printf("%s%s: %s%s%s\n", accent_color, name_for_type(type), ansi_bold_reset(), message,
               ansi_reset());
    } else {
        printf("%s%s: %s%s \n", accent_color, name_for_type(type), ansi_bold_reset(), message);
        if (print_file) {
            printf(" --> %s:%u:%u %s\n", file_name, adjusted_line, adjusted_col, ansi_reset());
        }
    }

    // if it's help, don't reshow the src buffer!, also if it's generated or NoInfo don't show!
    if (type == diag_type::help || this->holds<DiagnosticInfoNoPreview>()
        || this->span.is_generated()) {
        return;
    }

    const char* span_start = span.as_sv(context).data();
    const char* span_end = span_start + span.len;

    const char* src_buf_span_start = span_start; // start here before adjusting
    size_t src_buf_span_len = span.len;
    const char* src_buf_start = context.ast(span.file_id).buffer();
    // find start of src
    while (src_buf_span_start > src_buf_start && *src_buf_span_start != '\n') {
        --src_buf_span_start;
        ++src_buf_span_len;
    }
    // as to not having a leading newline
    if (src_buf_span_start[0] == '\n') {
        ++src_buf_span_start;
    }

    // as to not have a trailing newline
    if (src_buf_span_start[src_buf_span_len - 1] == '\n') {
        assert(src_buf_span_len != 0);
        --src_buf_span_len;
    }

    // end (keep bumping end till newline or end of buffer)
    while (src_buf_span_start[src_buf_span_len] != '\0'
           && src_buf_span_start[src_buf_span_len] != '\n') {
        ++src_buf_span_len;
    }

    // find max line (for line number sidebar width purposes)
    HirSize max_line = adjusted_line; // start here
    for (size_t i = 0; i < src_buf_span_len; i++) {
        if (src_buf_span_start[i] == '\n') {
            ++max_line;
        }
    }

    // max line width in terms of characters in the line number string
    const int min_width = width(max_line);

    // print body now
    static constexpr size_t BUF_DEF_SIZE = 2048;
    std::string buf;
    buf.reserve(BUF_DEF_SIZE);
    std::string_view full_src_span{src_buf_span_start, src_buf_span_len};

    HirSize curr_line = adjusted_line; // start here
    bool do_line_num = true;
    bool do_faux_line = false;
    static constexpr HirSize MAX_LINE_LEN = 100;

    // for tracking elipsing out:

    HirSize starting_line = curr_line;
    static constexpr HirSize MAX_LINES_BEFORE_ELLIPSING = 9;

    HirSize curr_len = 0;
    HirSize prev_len = 0; // NOLINT

    HirSize issue_line = 0; // NOLINT
                            //^ clang tidy was having issues with false positives here
    HirSize issue_len = 0;
    bool has_faux_lines = false;

    if (!context.compact_diagnostics_enabled()) {
        std::cout << line(min_width) << '\n';
    }

    for (HirSize i = 0; i < full_src_span.size(); i++) {

        auto maybe_accent_color = [&] {
            // if in span
            if (full_src_span.data() + i >= span_start && full_src_span.data() + i < span_end) {
                return accent_color;
            }
            return "";
        };

        // no line num
        auto line_without_num
            = [&] -> std::string { return ansi_reset() + line(min_width) + maybe_accent_color(); };
        // w line num
        auto line_with = [&](HirSize line_num) -> std::string {
            return ansi_reset() + line_with_number(line_num, min_width) + maybe_accent_color();
        };

        const char c = full_src_span[i];

        // handle need to line num
        if (do_line_num) {
            buf += line_with(curr_line);
            do_line_num = false;
        }
        if (c == '\n') {
            do_line_num = true;
            prev_len = curr_len;
            curr_len = 0;
            ++curr_line;
        } else if (curr_len >= MAX_LINE_LEN - 1) {
            do_faux_line = true;
            has_faux_lines = true;
            prev_len = curr_len;
            curr_len = 0;
            ++curr_line;
        }

        // start colored span
        if (full_src_span.data() + i == span_start) {
            buf += accent_color;
            issue_line = curr_line;
            issue_len = curr_len;
        } else if (full_src_span.data() + i == span_end) {
            buf += ansi_reset();
        }

        buf += c;

        if (do_faux_line) {
            // make a full word before line break;
            std::string tmp;
            tmp.reserve(MAX_LINE_LEN);
            while (buf.size() >= 1 && buf.at(buf.size() - 1) != ' ') {
                const char c = buf.at(buf.size() - 1);
                buf.pop_back();
                tmp.push_back(c);
            }
            buf += '\n';
            buf += line_without_num();
            buf += "        "; // 8 spaces
            for (auto iter = tmp.rbegin(); iter != tmp.rend(); iter++) {
                buf += (*iter);
            }
            do_faux_line = false;
        }

        bool single_line_done
            = !has_faux_lines && (adjusted_line == max_line) && (i == full_src_span.size() - 1);
        if (single_line_done) {
            if (c != '\n') {
                buf += '\n';
            }
            buf += line_without_num();
            for (HirSize k = 0; k < issue_len; k++) {
                buf += ' ';
            }
            buf += accent_color;
            for (HirSize k = 0; k <= span.len - 1; k++) {
                buf += '^';
            }
        }

        ++curr_len;
        if (curr_line - starting_line == MAX_LINES_BEFORE_ELLIPSING) {
            buf += line_without_num();
            buf += ansi_bold_reset();
            buf += "...";
            buf += ansi_reset();
            break;
        }
    }
    buf += ansi_reset();

    std::cout << buf;

    // if only one line
    if (curr_line == adjusted_line) {
        if (issue_len > MAX_LINE_LEN - 40) {
            std::string pre_info_buf;
            pre_info_buf.reserve(MAX_LINE_LEN);
            for (HirSize i = 0; i < issue_len; i++) {
                pre_info_buf += ' ';
            }
            std::cout << '\n' << line(min_width) << pre_info_buf;
        } else {
            std::cout << ' ';
        }

        print_info_value(context, min_width, false); // more than one line
    } else {
        std::cout << '\n' << line(min_width);
        print_info_value(context, min_width, true); // more than one line
    }
}

bool Diagnostic::has_complex_message() const {
    return !std::holds_alternative<DiagnosticNoOtherInfo>(message_value);
}

void Diagnostic::build_complex_message(Context& ctx, std::string& str) const {
    str.reserve(64); // decent size

    auto type_helper_color = [&ctx, &str](TypeId tid, const char* color) {
        if (contains_deftype(ctx, tid)) {
            str += ansi_bold_reset();
            str += '`';
            str += color;
            str += type_to_string_as_mentioned(ctx, tid);
            str += ansi_bold_reset();
            str += '`';
            str += " aka ";
        }
        str += '`';
        str += color;
        str += type_to_string(ctx, tid);
        str += ansi_bold_reset();
        str += '`';
    };

    auto type_helper = [this, &type_helper_color](TypeId tid) {
        type_helper_color(tid, accent_color_for_type(type));
    };

    auto sid_helper = [this, &ctx, &str](SymbolId sid) {
        str += '`';
        str += accent_color_for_type(type);
        str += ctx.symbol(sid);
        str += ansi_bold_reset();
        str += '`';
    };

    auto plural_helper
        = [&ctx](SymbolId d) -> const char* { return (ctx.symbol_id<"1">() == d) ? "" : "s"; };

    auto vs = Ovld{[](DiagnosticNoOtherInfo) {},
                   [&](DiagnosticIdentifierAfterMessage d) {
                       str += message_for_code(code);
                       str += " `";
                       str += accent_color_for_type(type);
                       for (auto sidx = d.sid_slice.begin(); sidx != d.sid_slice.end(); sidx++) {
                           str += ctx.symbol_id_to_cstr(ctx.symbol_id(sidx));
                           if (sidx != d.sid_slice.last_elem()) {
                               str += "..";
                           }
                       }
                       str += ansi_bold_reset();
                       str += '`';
                   },
                   [&](DiagnosticTypeAfterMessage d) {
                       str += message_for_code(code);
                       str += " ";
                       type_helper(d.tid);
                   },
                   [&](DiagnosticIdentifierBeforeMessage d) {
                       str += '`';
                       str += accent_color_for_type(type);
                       for (auto sidx = d.sid_slice.begin(); sidx != d.sid_slice.end(); sidx++) {
                           str += ctx.symbol_id_to_cstr(ctx.symbol_id(sidx));
                           if (sidx != d.sid_slice.last_elem()) {
                               str += "..";
                           }
                       }
                       str += ansi_bold_reset();
                       str += "` ";
                       str += message_for_code(code);
                   },
                   [&](DiagnosticIdentifierBeforeMessageAndTypeAfter d) {
                       str += '`';
                       str += accent_color_for_type(type);
                       for (auto sidx = d.sid_slice.begin(); sidx != d.sid_slice.end(); sidx++) {
                           str += ctx.symbol_id_to_cstr(ctx.symbol_id(sidx));
                           if (sidx != d.sid_slice.last_elem()) {
                               str += "..";
                           }
                       }
                       str += ansi_bold_reset();
                       str += "` ";
                       str += message_for_code(code);
                       str += " ";
                       type_helper(d.tid);
                   },
                   [&](DiagnosticSymbolAfterMessage d) {
                       str += message_for_code(code);
                       str += " `";
                       str += accent_color_for_type(type);
                       str += ctx.symbol_id_to_cstr(d.sid);
                       str += ansi_bold_reset();
                       str += '`';
                   },
                   [&](DiagnosticSymbolBeforeMessage d) {
                       str += '`';
                       str += accent_color_for_type(type);
                       str += ctx.symbol_id_to_cstr(d.sid);
                       str += ansi_bold_reset();
                       str += "` ";
                       str += message_for_code(code);
                   },
                   [&](DiagnosticSymbolAfterMessageNoQuotes d) {
                       str += message_for_code(code);
                       str += ' ';
                       str += ctx.symbol_id_to_cstr(d.sid);
                   },
                   [&](DiagnosticTypeToType t) {
                       str += message_for_code(code);
                       str += " ";
                       type_helper(t.from);
                       str += " to ";
                       type_helper(t.to);
                       str += ansi_reset();
                   },
                   [&](DiagnosticTypeAndType t) {
                       str += message_for_code(code);
                       str += " ";
                       type_helper(t.lhs_tid);
                       str += " and ";
                       type_helper(t.rhs_tid);
                       str += ansi_reset();
                   },
                   [&](DiagnosticTypeAndTypeForBinaryOp t) {
                       str += message_for_code(code);
                       str += " ";
                       type_helper(t.lhs_tid);
                       str += " and ";
                       type_helper(t.rhs_tid);
                       str += " for binary operator ";
                       str += '`';
                       str += accent_color_for_type(type);
                       str += binary_op_to_cstr(t.op);
                       str += ansi_bold_reset();
                       str += '`';
                       str += ansi_reset();
                   },
                   [&](DiagnosticSymButGotSym d) {
                       str += "function `";
                       str += ctx.symbol_id_to_cstr(d.leading);
                       str += "` ";
                       str += message_for_code(code);
                       str += " ";
                       str += ansi_bold_green();
                       str += ctx.symbol_id_to_cstr(d.sid1);
                       str += ansi_bold_reset();
                       str += " arguments but got ";
                       str += accent_color_for_type(type);
                       str += ctx.symbol_id_to_cstr(d.sid2);
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticComptStackOverflow d) {
                       str += "maximum call stack size exceeded when calling function `";
                       str += accent_color_for_type(type);
                       str += ctx.symbol_id_to_cstr(d.function_sid);
                       str += ansi_bold_reset();
                       str += "` ";
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticIdxOutOfBounds d) {
                       str += "index with value ";
                       str += accent_color_for_type(type);
                       str += ctx.symbol(d.idx_sid);
                       str += ansi_bold_reset();
                       str += " is out of bounds for ordered value with length ";
                       str += ansi_bold_cyan();
                       str += ctx.symbol_id_to_cstr(d.length_sid);
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticStructMemberSymBeforeMsg d) {
                       str += "struct member `";
                       str += accent_color_for_type(type);
                       str += ctx.symbol(d.mem_sid);
                       str += ansi_bold_reset();
                       str += "` ";
                       str += message_for_code(code);
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticStructDoesNotDefineBlankForContract d) {
                       str += "struct ";
                       sid_helper(d.struct_name);
                       str += " does not provide function ";
                       sid_helper(d.func_name);
                       str += " for contract ";
                       sid_helper(d.contract_name);
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticContractFnExpectedButGotNumParams d) {
                       str += "contract function ";
                       sid_helper(d.contract_fn_name);
                       str += " expected ";
                       str += ansi_bold_green();
                       str += ctx.symbol_id_to_cstr(d.expected_sid);
                       str += ansi_bold_reset();
                       str += " parameter";
                       str += plural_helper(d.expected_sid);
                       str += " but got ";
                       str += ansi_bold_red();
                       str += ctx.symbol_id_to_cstr(d.got_sid);
                       str += ansi_bold_reset();
                       str += " parameter";
                       str += plural_helper(d.got_sid);
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticVariantInitExpectedButGotNumArgs d) {
                       str += "variant initializer for ";
                       sid_helper(d.variant_field_name);
                       str += " expected ";
                       str += ansi_bold_green();
                       str += ctx.symbol_id_to_cstr(d.expected_sid);
                       str += ansi_bold_reset();
                       str += " argument";
                       str += plural_helper(d.expected_sid);
                       str += " but got ";
                       str += ansi_bold_red();
                       str += ctx.symbol_id_to_cstr(d.got_sid);
                       str += ansi_bold_reset();
                       str += " argument";
                       str += plural_helper(d.got_sid);
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticGenArgsExpectedButGotNumArgs d) {
                       str += "generic arguments for ";
                       sid_helper(d.name);
                       str += " expected ";
                       str += ansi_bold_green();
                       str += ctx.symbol_id_to_cstr(d.expected_sid);
                       str += ansi_bold_reset();
                       str += " argument";
                       str += plural_helper(d.expected_sid);
                       str += " but got ";
                       str += ansi_bold_red();
                       str += ctx.symbol_id_to_cstr(d.got_sid);
                       str += ansi_bold_reset();
                       str += " argument";
                       str += plural_helper(d.got_sid);
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticContractFnExpectedRetTyButGot d) {
                       str += "contract function ";
                       sid_helper(d.contract_fn_name);
                       str += " expected return type ";
                       str += ansi_bold_green();
                       type_helper_color(d.expected_return_tid, ansi_bold_green());
                       str += ansi_bold_reset();
                       str += " but got ";
                       str += ansi_bold_red();
                       type_helper_color(d.got_return_tid, ansi_bold_red());
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticTyButGot d) {
                       str += message_for_code(code);
                       str += " ";
                       str += ansi_bold_green();
                       type_helper_color(d.expected_tid, ansi_bold_green());
                       str += ansi_bold_reset();
                       str += " but got ";
                       str += ansi_bold_red();
                       type_helper_color(d.got_tid, ansi_bold_red());
                       str += ansi_bold_reset();
                   },
                   [&](DiagnosticSymbolBeforeAndAfterMessage d) {
                       sid_helper(d.before_sid);
                       str += " ";
                       str += message_for_code(code);
                       str += " ";
                       sid_helper(d.after_sid);
                   },
                   [&](DiagnosticTyperBeforeMessageAndSymbolAfter d) {
                       type_helper(d.tid);
                       str += " ";
                       str += message_for_code(code);
                       str += " ";
                       sid_helper(d.sid);
                   },
                   [&](DiagnosticTypeBeforeMessage d) {
                       type_helper(d.tid);
                       str += " ";
                       str += message_for_code(code);
                   }}; // namespace hir
    std::visit(vs, message_value);
}

void DiagLinker::link(DiagnosticId d) {
    if (prev.has_value()) {
        ctx.link_diagnostic(prev.as_id(), d);
    }
    prev = d;
    if (first.empty()) {
        first = d;
    }
}

void DiagLinker::link(OptId<DiagnosticId> d) {
    if (prev.has_value() && d.has_value()) {
        ctx.link_diagnostic(prev.as_id(), d.as_id());
    }
    if (d.has_value()) {
        prev = d;
    }
    if (d.has_value() && first.empty()) {
        first = d;
    }
}

void DiagLinker::link(DiagRange range) {
    if (range.first.has_value()) {
        link(range.first.as_id());
    }
    if (range.last.has_value()) {
        prev = range.last.as_id();
    }
}

OptId<DiagnosticId> DiagLinker::last_in_chain() const { return prev; }

OptId<DiagnosticId> DiagLinker::first_in_chain() const { return first; }

DiagRange DiagLinker::range() const { return {first_in_chain(), last_in_chain()}; }

} // namespace hir
