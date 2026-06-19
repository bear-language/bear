//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/diagnostics/error_codes.h"
#include "compiler/token.h"

static const uint8_t diag_types[ERR__COUNT]
    = {[NOTE_DID_YOU_MEAN_MT] = DIAG_TYPE_NOTE,
       [NOTE_EXTRANEOUS_SEMICOLON] = DIAG_TYPE_WARNING,
       [HELP_REMOVE] = DIAG_TYPE_HELP,
       [WARN_TOP_LEVEL_USE_CAN_POLLUTE_THE_GLOBAL_NAMESPACE] = DIAG_TYPE_WARNING,
       [HELP_REMOVE_SEMICOLON_TO_YIELD_EXPRESSION_VALUE] = DIAG_TYPE_HELP};
bool is_non_error_diagnostic(error_code_e error_code) { return diag_types[error_code]; }
static const char* error_messages[ERR__COUNT] = {
    [ERR_EXPECTED_IDENTIFER] = "expected identifier",
    [ERR_EXPECTED_LITERAL] = "expected literal",
    [ERR_EXPECTED_TOKEN] = "expected token: ",
    [ERR_EXPECTED_VARIABLE_NAME] = "expected variable name",
    [ERR_EXPECTED_PARAMETER_IDENTIFIER] = "expected parameter name",
    [ERR_EXPECTED_TYPE] = "expected type",
    [ERR_EXPECTED_EXPRESSION] = "expected expression",
    [ERR_EXPECTED_STATEMENT] = "expected statement",
    [ERR_INCOMPLETE_VAR_DECLARATION] = "expected ';' or assignment operator",
    [ERR_EXPECT_GENERIC_OPENER] = "expected '::' or '<' in generic type",
    [ERR_EXPECTED_BASE_TYPE_IN_GENERIC] = "generic template can only be applied to base types",
    [ERR_REDUNDANT_MUT] = "redundant 'mut' qualifier",
    [ERR_MUT_CANNOT_BIND_TO_ARRAYS] = "'mut' qualifier cannot bind to arrays",
    [ERR_NO_LEADING_MUT_FOR_SLICES] = "'mut' qualifier should be: [&mut] for slices",
    [ERR_EXPECTED_DECLARTION] = "expected variable or function declaration",
    [ERR_EXPECTED_DELIM_IN_MODULE_DECL] = "expected '{' or ';' in module declaration",
    [ERR_INVALID_MODULE_NAME] = "invalid module name",
    [NOTE_EXTRANEOUS_SEMICOLON] = "extraneous ';'",
    [ERR_EXTRANEOUS_VISIBILITY_MODIFIER] = "extraneous visibility modifier",
    [ERR_BODY_MUST_BE_WRAPPED_IN_BRACES]
    = "expected '{' since statements following condition must be wrapped in braces",
    [ERR_MISMATCHED_RPAREN] = "mismatched ')' without an opening '('",
    [ERR_MISSING_CLOSING_RPAREN] = "missing closing ')'",
    [ERR_BREAK_STMT_OUTSIDE_OF_LOOP] = "break statement outside of loop",
    [ERR_REDUNDANT_COMPT_QUALIFIER] = "redundant 'compt' qualifier",
    [ERR_INVALID_GENERIC_PARAMETER] = "invalid generic parameter",
    [ERR_EXPECTED_FN_OR_MT] = "expected function declaration keyword 'fn' or 'mt'",
    [ERR_EXPECTED_ASSIGNMENT] = "expected assignment operator '=' or '<-'",
    [ERR_INVALID_PATTERN]
    = "invalid patern; expected identifier, literal, or variant decomposition",
    [ERR_MUT_QUALIFIER_ON_NON_MT] = "'mut' qualifier can only be used on 'mt' methods",
    [ERR_TOO_MANY_QUALIFICATIONS_ON_FUNCTION] = "too many qualifications on function declaration",
    [ERR_QUALIFICATION_ON_NON_MT_FN_DECL]
    = "leading structure qualification on an 'fn' function is malformed",
    [NOTE_DID_YOU_MEAN_MT] = "did you mean to declare this as 'mt'?",
    [ERR_MULTILEVEL_REF]
    = "multi-level reference type is malformed; did you mean to declare a multi-level pointer?",
    [ERR_CONTINUE_STMT_OUTSIDE_OF_LOOP] = "continue statement outside of loop",
    [HELP_REMOVE] = "remove",
    [HELP_REMOVE_SEMICOLON_TO_YIELD_EXPRESSION_VALUE] = "remove ';' to yield expression value",
    [ERR_NON_TOP_LEVEL_IMPORT_STATEMENT] = "import statement is not a top-level statement",
    [WARN_TOP_LEVEL_USE_CAN_POLLUTE_THE_GLOBAL_NAMESPACE]
    = "top level `use` statement can pollute the global namespace",
    [GENERIC_PARAMETERS_CANNOT_BE_EMPTY] = "generic parameters cannot be empty",
    [GENERIC_ARGS_CANNOT_BE_EMPTY] = "generic arguments cannot be empty",

};
const char* error_message_for_code(error_code_e error_code) { return error_messages[error_code]; }

error_diag_type_e error_diagnostic_type(error_code_e error_code) { return diag_types[error_code]; }

const char* error_message_context_for(compiler_error_t* error) {
    if (error->expected_token_type != TOK_NONE) {
        return token_to_string_map()[error->expected_token_type];
    }
    // this can be extended to support additional contexts in the future
    return ""; // empty string, no additional context
}
