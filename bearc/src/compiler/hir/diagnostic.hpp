//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_HIR_DIAGNOSTIC_HPP
#define COMPILER_HIR_DIAGNOSTIC_HPP

#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/span.hpp"
#include "compiler/hir/type.hpp"
#include "compiler/hir/variant_helpers.hpp"
#include <cstdint>
#include <variant>
namespace hir {

class Context;
enum class diag_code : uint8_t {
    imported_file_dne,
    redefined_symbol,
    previous_def_here,
    no_matching_struct_for_method,
    capitalized_mod,
    lowercase_structure,
    invalid_extern_lang,
    cyclical_import,
    empty_variant,
    empty_union,
    circular_definition,
    circular_definition_passes_thru,
    circular_definition_origin,
    cannot_resolve_value_at_compt,
    cannot_convert_type,
    cannot_resolve_at_compt,
    type_not_defined,
    compt_variable_should_be_immutable,
    must_initialize_global_variable,
    cannot_init_with_non_compt_value,
    declared_here_without_compt,
    not_a_compile_time_constant,
    use_of_undeclared_identifier,
    use_of_undeclared_mod,
    not_declared_in_this_scope,
    replace_with,
    remove,
    invalid_alignas,
    alignas_expr_must_be_a_valid_uint_lit,
    multiple_alignas_on_one_def,
    redundant_compt_qualifier,
    redundant_static_qualifier,
    compt_generic_structs_not_possible,
    this_feature_is_planned,
    is_not_a_struct,
    declared_here,
    struct_field_not_initialized,
    struct_fields_not_initialized,
    cannot_convert_expression_to_type,
    field_initializer_does_not_match_field,
    too_many_initializers_given_for_struct_init,
    compt_values_cannot_be_moved,
    var_cannot_be_part_of_a_scoped_identifier,
    compt_variable_should_have_an_explicit_type,
    cannot_convert_value_of_type,
    cannot_mutate_compt_const,
    cannot_cast_expr_to_type,
    guaranteed_narrowing_of_compt_value,
    invalid_operand_for_binary_expression,
    type_is_not_resolvable_at_compt,
    is_declared_hid,
    a_compt_variable_should_be_explicitly_initialized,
    all_runtime_glob_and_mem_vars_need_compt_init,
    incompatible_types_for_binary_expression,
    incompatible_types_for_binary_operator,
    dividing_by_zero_at_compt_is_illegal,
    invalid_cast,
    parentheses_should_be_used_for_chained_casts,
    operator_not_viable_at_compt,
    immutable_value_is_not_assignable,
    value_is_not_contextually_convertible_to,
    invalid_operand_for_unary_expression,
    mismatched_types_in_list_literal,
    value_is_of_type,
    is_of_type,
    array_cannot_have_size_zero,
    cannot_infer_type_at_compt,
    static_assertion_failed,
    value_not_a_struct,
    value_does_not_refer_to_a_named_mem,
    id_does_not_name_a_member_variable_of,
    id_names_a_static_mem_thru_dot_for,
    scoped_identifer_not_allowed_here,
    condition_is_false,
    compt_mut_methods_are_not_permitted,
    compt_expression_functions_can_only_act_on_immut_vals,
    compt_function_does_not_yield_a_pure_expr,
    declare_using_pure_expression_syntax_replacing_body_with,
    invalid_binary_operator,
    compt_expressions_do_not_have_addrs_so_no_ptrs,
    value_is_not_a_pointer,
    id_does_not_name_a_method_of,
    value_is_not_callable,
    not_a_function,
    expected,
    free_function_called_as_a_method,
    only_message_value_is_meaningful,
    compt_vars_should_not_be_move_initialized,
    compile_time_constant_cannot_be_moved,
    value_is_a_compile_time_constant,
    called_here,
    pointers_are_not_assignable_at_compt,
    compt_values_cannot_be_mut_borrowed,
    cannot_evaluate_non_pure_expr_fn_at_compt,
    cannot_access_a_member_value,
    tried_to_call_property,
    value_is_a_compt_list_holding_type,
    value_is_a_struct_of_type,
    should_have_explicit_type,
    non_compt_function_params_must_have_explicit_types,
    type_deduction_not_legal_here,
    use_a_generic_function_parameter_and_specify_it_as_a_type,
    function_signature_does_not_match_contract,
    not_a_contract,
    invalid_contract,
    has_return_type_but_contracts_function_does_not,
    does_not_have_return_type_but_contracts_function_does,
    declared_in_contract_here,
    invalid_return_type,
    return_type_must_be_an_explicit_type,
    void_is_not_a_valid_return_type,
    to_return_nothing_omit_the_return_type_entirely,
    replace_occurences_of_var_with_an_explicit_type,
    immutable_reference_to_mut_value_is_malformed,
    redundant_mut_reference_to_mut_value,
    struct_already_has_contract,
    should_be_declared_compt,
    should_not_be_declared_compt,
    too_many_initializers_for_union,
    union_initializers_must_only_set_one_field,
    too_few_inits_for_union,
    does_not_name_a_field_of_union,
    compt_union_does_not_hold_field,
    compt_union_holds_field,
    raw_use_of_variant_field,
    variant_fields_cannot_be_used_outside_of_a_variant,
    invalid_variant_init_for_field,
    not_a_variant,
    cannot_use_is_for_non_variant_values,
    value_does_not_name_a_valid_pattern,
    identifer_does_not_name_a_valid_pattern,
    not_a_variant_field,
    has_no_such_variant_field,
    is_a_field_of_variant,
    variant_decomposition_not_allowed_here,
    invalid_pattern,
    else_match_branch_should_come_last,
    duplicate_else_match_branch,
    duplicate_match_pattern,
    match_expression_is_not_exhaustive,
    does_not_consider,
    match_expression_may_not_be_exhaustive,
    add_an_else_branch,
    too_many_decompositions_for_variant,
    cannot_match_type_for_decomposed_variant_from,
    cannot_decompose_multiple_variants_on_the_same_match_arm,
    does_not_take_generic_arguments,
    not_a_generic_type,
    gen_arg_expected_a_value_expression_not_a_type,
    should_a_compt_value_of_type,
    does_not_satisfy_contract,
    gen_arg_expected_a_type_not_a_value_expression,
    generic_argument_expected_value_of_type,

    count, // this must be last,

};
enum class diag_type : uint8_t { error, warning, note, help };

struct DiagnosticNoOtherInfo {};

struct DiagnosticIdentifierAfterMessage {
    IdSlice<SymbolId> sid_slice;
};

struct DiagnosticIdentifierBeforeMessage {
    IdSlice<SymbolId> sid_slice;
};

struct DiagnosticIdentifierBeforeMessageAndTypeAfter {
    IdSlice<SymbolId> sid_slice;
    TypeId tid;
};

struct DiagnosticTyperBeforeMessageAndSymbolAfter {
    SymbolId sid;
    TypeId tid;
};

struct DiagnosticSymbolBeforeMessage {
    SymbolId sid;
};

struct DiagnosticSymbolAfterMessage {
    SymbolId sid;
};

struct DiagnosticSymbolAfterMessageNoQuotes {
    SymbolId sid;
};

struct DiagnosticTypeAfterMessage {
    TypeId tid;
};

struct DiagnosticTypeBeforeMessage {
    TypeId tid;
};

struct DiagnosticTypeToType {
    TypeId from;
    TypeId to;
};

struct DiagnosticTypeAndType {
    TypeId lhs_tid;
    TypeId rhs_tid;
};

struct DiagnosticTypeAndTypeForBinaryOp {
    TypeId lhs_tid;
    TypeId rhs_tid;
    binary_op op;
};

struct DiagnosticSymButGotSym {
    SymbolId leading;
    SymbolId sid1;
    SymbolId sid2;
};

struct DiagnosticComptStackOverflow {
    SymbolId function_sid;
};

struct DiagnosticIdxOutOfBounds {
    SymbolId idx_sid;
    SymbolId length_sid;
};

struct DiagnosticStructMemberSymBeforeMsg {
    SymbolId mem_sid;
};

struct DiagnosticStructDoesNotDefineBlankForContract {
    SymbolId struct_name;
    SymbolId func_name;
    SymbolId contract_name;
};

struct DiagnosticContractFnExpectedButGotNumParams {
    SymbolId contract_fn_name;
    SymbolId expected_sid;
    SymbolId got_sid;
};

struct DiagnosticContractFnExpectedRetTyButGot {
    SymbolId contract_fn_name;
    TypeId expected_return_tid;
    TypeId got_return_tid;
};

struct DiagnosticTyButGot {
    TypeId expected_tid;
    TypeId got_tid;
};

struct DiagnosticVariantInitExpectedButGotNumArgs {
    SymbolId variant_field_name;
    SymbolId expected_sid;
    SymbolId got_sid;
};

struct DiagnosticGenArgsExpectedButGotNumArgs {
    SymbolId name;
    SymbolId expected_sid;
    SymbolId got_sid;
};

struct DiagnosticSymbolBeforeAndAfterMessage {
    SymbolId before_sid;
    SymbolId after_sid;
};

using DiagnosticMessageValue = std::variant<
    DiagnosticNoOtherInfo, DiagnosticIdentifierAfterMessage, DiagnosticSymbolAfterMessage,
    DiagnosticSymbolAfterMessageNoQuotes, DiagnosticIdentifierBeforeMessage,
    DiagnosticTypeAfterMessage, DiagnosticSymbolBeforeMessage, DiagnosticTypeToType,
    DiagnosticTypeAndType, DiagnosticTypeAndTypeForBinaryOp,
    DiagnosticIdentifierBeforeMessageAndTypeAfter, DiagnosticSymButGotSym,
    DiagnosticComptStackOverflow, DiagnosticIdxOutOfBounds, DiagnosticStructMemberSymBeforeMsg,
    DiagnosticStructDoesNotDefineBlankForContract, DiagnosticContractFnExpectedButGotNumParams,
    DiagnosticContractFnExpectedRetTyButGot, DiagnosticVariantInitExpectedButGotNumArgs,
    DiagnosticSymbolBeforeAndAfterMessage, DiagnosticTypeBeforeMessage,
    DiagnosticGenArgsExpectedButGotNumArgs, DiagnosticTyperBeforeMessageAndSymbolAfter,
    DiagnosticTyButGot>;

struct DiagnosticImportStack {
    IdSlice<FileId> files;
};

struct DiagnosticInfoNoPreview {};

struct DiagnosticInfoDontDisplayFile {};

struct DiagnosticSubCode {
    diag_code sub_code;
};

using DiagnosticInfoValue
    = std::variant<DiagnosticNoOtherInfo, DiagnosticImportStack, DiagnosticTypeToType,
                   DiagnosticSubCode, DiagnosticInfoNoPreview, DiagnosticInfoDontDisplayFile>;

struct Diagnostic : NodeWithVariantValue<Diagnostic> {

    using id_type = DiagnosticId;
    using value_type = DiagnosticInfoValue;
    DiagnosticMessageValue message_value = DiagnosticNoOtherInfo{};
    DiagnosticInfoValue value = DiagnosticNoOtherInfo{};
    Span span;
    OptId<DiagnosticId> next;
    diag_code code;
    diag_type type;
    void print(Context& context, bool print_file) const;
    Diagnostic(Span span, enum diag_code code, enum diag_type type,
               OptId<DiagnosticId> next = OptId<DiagnosticId>{})
        : value(DiagnosticNoOtherInfo{}), span(span), next(next), code(code), type(type) {}
    Diagnostic(Span span, enum diag_code code, enum diag_type type, DiagnosticInfoValue value,
               OptId<DiagnosticId> next = OptId<DiagnosticId>{})
        : value(value), span(span), next(next), code(code), type(type) {}
    Diagnostic(Span span, enum diag_code code, enum diag_type type,
               DiagnosticMessageValue message_value, DiagnosticInfoValue value,
               OptId<DiagnosticId> next = OptId<DiagnosticId>{})
        : message_value(message_value), value(value), span(span), next(next), code(code),
          type(type) {}
    void set_next(DiagnosticId next) { this->next = next; }

  private:
    bool has_complex_message() const;
    void build_complex_message(Context& ctx, std::string& message_str) const;
    static const char* message_for_code(enum diag_code c);
    static const char* name_for_type(enum diag_type t);
    static const char* accent_color_for_type(enum diag_type t);
    void print_info_value(Context& context, HirSize min_width, bool more_than_one_line) const;
    void print_multiline(Context& context, bool print_file) const;
    void print_line(const auto& printable) const;
    void print_line_with_number(HirSize line, const auto& printable) const;
    [[nodiscard]] std::string line(int min_width) const;
    [[nodiscard]] std::string diag(int min_width) const;
    [[nodiscard]] std::string line_with_number(HirSize line, int min_width) const;
    static int width(HirSize line);
};

struct DiagRange {
    OptId<DiagnosticId> first;
    OptId<DiagnosticId> last;
};

/// helper class that makes linking chains of diagnostics easier by internally tracking the previous
/// diagnostic
class DiagLinker {
    Context& ctx;
    OptId<DiagnosticId> first;
    OptId<DiagnosticId> prev;

  public:
    DiagLinker(Context& ctx) : ctx{ctx} {}
    void link(DiagnosticId d);
    void link(OptId<DiagnosticId> d);
    void link(DiagRange range);
    OptId<DiagnosticId> last_in_chain() const;
    OptId<DiagnosticId> first_in_chain() const;
    DiagRange range() const;
};

} // namespace hir

#endif
