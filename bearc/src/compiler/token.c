//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/token.h"
#include "utils/strimap.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/// the values should match their token_type_e counterparts
static unsigned char char_to_token_map[TOKEN_CHAR_TO_TOKEN_MAP_SIZE] = {
    // delimiters
    ['('] = TOK_LPAREN,
    [')'] = TOK_RPAREN,
    ['{'] = TOK_LBRACE,
    ['}'] = TOK_RBRACE,
    ['['] = TOK_LBRACK,
    [']'] = TOK_RBRACK,

    // punctuation
    [';'] = TOK_SEMICOLON,
    [':'] = TOK_COLON,
    ['.'] = TOK_DOT,
    [','] = TOK_COMMA,

    // at invoke
    ['#'] = TOK_HASH_INVOKE,

    // assignment
    ['='] = TOK_ASSIGN_EQ,

    // arithmetic
    ['+'] = TOK_PLUS,
    ['-'] = TOK_MINUS,
    ['*'] = TOK_STAR,
    ['/'] = TOK_DIVIDE,
    ['%'] = TOK_MODULO,

    // bitwise
    ['|'] = TOK_BAR,
    ['&'] = TOK_AMPER,
    ['~'] = TOK_BIT_NOT,
    ['^'] = TOK_BIT_XOR,

    // boolean
    ['!'] = TOK_BOOL_NOT,

    // comparison
    ['>'] = TOK_GT,
    ['<'] = TOK_LT,
};
const unsigned char* get_char_to_token_map(void) { return char_to_token_map; }

static pthread_once_t string_to_token_map_init_once = PTHREAD_ONCE_INIT;
static strimap_t string_to_token_map;
static void string_to_token_map_init(void) {
    string_to_token_map
        = strimap_create(TOKEN_STRING_TO_TOKEN_MAP_SIZE); // make large to reduce hash conflicts

    // bool literals
    strimap_emplace(&string_to_token_map, "true", TOK_BOOL_LIT_TRUE);
    strimap_emplace(&string_to_token_map, "false", TOK_BOOL_LIT_FALSE);
    strimap_emplace(&string_to_token_map, "null", TOK_NULL_LIT);

    // file
    strimap_emplace(&string_to_token_map, "import", TOK_IMPORT);

    // keywords
    strimap_emplace(&string_to_token_map, "mod", TOK_MODULE);
    strimap_emplace(&string_to_token_map, "use", TOK_USE);
    strimap_emplace(&string_to_token_map, "fn", TOK_FN);
    strimap_emplace(&string_to_token_map, "mt", TOK_MT);
    strimap_emplace(&string_to_token_map, "dt", TOK_DT);

    strimap_emplace(&string_to_token_map, "mut", TOK_MUT);
    strimap_emplace(&string_to_token_map, "contract", TOK_CONTRACT);
    strimap_emplace(&string_to_token_map, "requires", TOK_REQUIRES);

    strimap_emplace(&string_to_token_map, "i8", TOK_I8);
    strimap_emplace(&string_to_token_map, "u8", TOK_U8);

    strimap_emplace(&string_to_token_map, "i16", TOK_I16);
    strimap_emplace(&string_to_token_map, "u16", TOK_U16);

    strimap_emplace(&string_to_token_map, "i32", TOK_I32);
    strimap_emplace(&string_to_token_map, "u32", TOK_U32);

    strimap_emplace(&string_to_token_map, "i64", TOK_I64);
    strimap_emplace(&string_to_token_map, "u64", TOK_U64);

    strimap_emplace(&string_to_token_map, "char", TOK_CHAR);
    strimap_emplace(&string_to_token_map, "f32", TOK_F32);
    strimap_emplace(&string_to_token_map, "f64", TOK_F64);
    strimap_emplace(&string_to_token_map, "str", TOK_STR);
    strimap_emplace(&string_to_token_map, "bool", TOK_BOOL);
    strimap_emplace(&string_to_token_map, "void", TOK_VOID);
    strimap_emplace(&string_to_token_map, "var", TOK_VAR);
    strimap_emplace(&string_to_token_map, "static", TOK_STATIC);
    strimap_emplace(&string_to_token_map, "extern", TOK_EXTERN);
    strimap_emplace(&string_to_token_map, "compt", TOK_COMPT);
    strimap_emplace(&string_to_token_map, "hid", TOK_HID);
    strimap_emplace(&string_to_token_map, "pub", TOK_PUB);
    strimap_emplace(&string_to_token_map, "has", TOK_HAS);
    strimap_emplace(&string_to_token_map, "is", TOK_IS);
    strimap_emplace(&string_to_token_map, "variant", TOK_VARIANT);

    strimap_emplace(&string_to_token_map, "@same_type", TOK_SAME_TYPE);
    strimap_emplace(&string_to_token_map, "@type_to_str", TOK_TYPE_TO_STR);
    strimap_emplace(&string_to_token_map, "@static_assert", TOK_STATIC_ASSERT);
    strimap_emplace(&string_to_token_map, "@defined", TOK_DEFINED);
    strimap_emplace(&string_to_token_map, "@has_contract", TOK_HAS_CONTRACT);
    strimap_emplace(&string_to_token_map, "@inferable_as", TOK_INFERABLE_AS);

    strimap_emplace(&string_to_token_map, "@err", TOK_ERR);
    strimap_emplace(&string_to_token_map, "@warn", TOK_WARN);
    strimap_emplace(&string_to_token_map, "@note", TOK_NOTE);
    strimap_emplace(&string_to_token_map, "@help", TOK_HELP);

    strimap_emplace(&string_to_token_map, "decay", TOK_DECAY);

    strimap_emplace(&string_to_token_map, "deftype", TOK_DEFTYPE);

    // control flow
    strimap_emplace(&string_to_token_map, "if", TOK_IF);
    strimap_emplace(&string_to_token_map, "else", TOK_ELSE);
    strimap_emplace(&string_to_token_map, "while", TOK_WHILE);
    strimap_emplace(&string_to_token_map, "for", TOK_FOR);
    strimap_emplace(&string_to_token_map, "return", TOK_RETURN);
    strimap_emplace(&string_to_token_map, "yield", TOK_YIELD);
    strimap_emplace(&string_to_token_map, "break", TOK_BREAK);
    strimap_emplace(&string_to_token_map, "continue", TOK_CONTINUE);
    strimap_emplace(&string_to_token_map, "match", TOK_MATCH);

    // more operators
    strimap_emplace(&string_to_token_map, "@sizeof", TOK_SIZEOF);
    strimap_emplace(&string_to_token_map, "@alignof", TOK_ALIGNOF);
    strimap_emplace(&string_to_token_map, "alignas", TOK_ALIGNAS);
    strimap_emplace(&string_to_token_map, "typeof", TOK_TYPEOF);
    strimap_emplace(&string_to_token_map, "move", TOK_MOVE);
    strimap_emplace(&string_to_token_map, "as", TOK_AS);

    // structures
    strimap_emplace(&string_to_token_map, "self", TOK_SELF_ID);
    strimap_emplace(&string_to_token_map, "Self", TOK_SELF_TYPE);
    strimap_emplace(&string_to_token_map, "struct", TOK_STRUCT);

    strimap_emplace(&string_to_token_map, "union", TOK_UNION);

    // operators / symbols (multi-char tokens)
    strimap_emplace(&string_to_token_map, "->", TOK_RARROW);
    strimap_emplace(&string_to_token_map, "~>", TOK_DISCARD_RARROW);
    strimap_emplace(&string_to_token_map, "=>", TOK_EQ_ARROW);
    strimap_emplace(&string_to_token_map, "..", TOK_SCOPE_RES);
    strimap_emplace(&string_to_token_map, "::", TOK_GENERIC_SEP);
    strimap_emplace(&string_to_token_map, "<-", TOK_ASSIGN_MOVE);
    strimap_emplace(&string_to_token_map, "<<-", TOK_STREAM);
    strimap_emplace(&string_to_token_map, "++", TOK_INC);
    strimap_emplace(&string_to_token_map, "--", TOK_DEC);
    strimap_emplace(&string_to_token_map, "<<", TOK_LSH);
    strimap_emplace(&string_to_token_map, ">>", TOK_RSHL);
    strimap_emplace(&string_to_token_map, ">>>", TOK_RSHA);
    strimap_emplace(&string_to_token_map, "||", TOK_BOOL_OR);
    strimap_emplace(&string_to_token_map, "&&", TOK_BOOL_AND);
    strimap_emplace(&string_to_token_map, ">=", TOK_GE);
    strimap_emplace(&string_to_token_map, "<=", TOK_LE);
    strimap_emplace(&string_to_token_map, "==", TOK_BOOL_EQ);
    strimap_emplace(&string_to_token_map, "!=", TOK_NE);

    // range stuff
    strimap_emplace(&string_to_token_map, "...", TOK_ELLIPSE);
    strimap_emplace(&string_to_token_map, "...=", TOK_ELLIPSE_EQ);
    strimap_emplace(&string_to_token_map, "in", TOK_IN);

    // compound assignment operators
    strimap_emplace(&string_to_token_map, "+=", TOK_ASSIGN_PLUS_EQ);
    strimap_emplace(&string_to_token_map, "-=", TOK_ASSIGN_MINUS_EQ);
    strimap_emplace(&string_to_token_map, "*=", TOK_ASSIGN_MULT_EQ);
    strimap_emplace(&string_to_token_map, "/=", TOK_ASSIGN_DIV_EQ);
    strimap_emplace(&string_to_token_map, "%=", TOK_ASSIGN_MOD_EQ);
    strimap_emplace(&string_to_token_map, "&=", TOK_ASSIGN_AND_EQ);
    strimap_emplace(&string_to_token_map, "|=", TOK_ASSIGN_OR_EQ);
    strimap_emplace(&string_to_token_map, "^=", TOK_ASSIGN_XOR_EQ);
    strimap_emplace(&string_to_token_map, "<<=", TOK_ASSIGN_LSH_EQ);
    strimap_emplace(&string_to_token_map, ">>=", TOK_ASSIGN_RSHL_EQ);
    strimap_emplace(&string_to_token_map, ">>>=", TOK_ASSIGN_RSHA_EQ);
}

const strimap_t* get_string_to_token_strimap(void) {
    pthread_once(&string_to_token_map_init_once, &string_to_token_map_init);
    return &string_to_token_map;
}

static void string_to_token_strimap_destroy_impl(void) { strimap_destroy(&string_to_token_map); }

static pthread_once_t string_to_token_map_destroy_once = PTHREAD_ONCE_INIT;

static void string_to_token_strimap_destroy(void) {
    pthread_once(&string_to_token_map_destroy_once, &string_to_token_strimap_destroy_impl);
}

void token_maps_free(void) { string_to_token_strimap_destroy(); }

static const char* token_to_string_map_impl[TOK__NUM] = {
    [TOK_INDETERMINATE] = "INDETER.",

    // mono-char tokens
    [TOK_LPAREN] = "(",
    [TOK_RPAREN] = ")",
    [TOK_LBRACE] = "{",
    [TOK_RBRACE] = "}",
    [TOK_LBRACK] = "[",
    [TOK_RBRACK] = "]",

    // punctuation
    [TOK_SEMICOLON] = ";",
    [TOK_COLON] = ":",
    [TOK_DOT] = ".",
    [TOK_COMMA] = ",",
    [TOK_HASH_INVOKE] = "#",

    // assignment / operators
    [TOK_ASSIGN_EQ] = "=",
    [TOK_PLUS] = "+",
    [TOK_MINUS] = "-",
    [TOK_STAR] = "*",
    [TOK_DIVIDE] = "/",
    [TOK_MODULO] = "%",

    // bitwise
    [TOK_BAR] = "|",
    [TOK_AMPER] = "&",
    [TOK_BIT_NOT] = "~",
    [TOK_BIT_XOR] = "^",

    // boolean
    [TOK_BOOL_NOT] = "!",

    // comparison
    [TOK_GT] = ">",
    [TOK_LT] = "<",

    // file / keywords
    [TOK_IMPORT] = "import",
    [TOK_MODULE] = "mod",
    [TOK_USE] = "use",
    [TOK_FN] = "fn",
    [TOK_MT] = "mt",
    [TOK_DT] = "dt",

    [TOK_MUT] = "mut",
    [TOK_CONTRACT] = "contract",
    [TOK_REQUIRES] = "requires",

    [TOK_I8] = "i8",
    [TOK_U8] = "u8",

    [TOK_I16] = "i16",
    [TOK_U16] = "u16",

    [TOK_I32] = "i32",
    [TOK_U32] = "u32",

    [TOK_I64] = "i64",
    [TOK_U64] = "u64",

    [TOK_CHAR] = "char",

    [TOK_F32] = "f32",
    [TOK_F64] = "f64",

    [TOK_STR] = "str",
    [TOK_BOOL] = "bool",
    [TOK_VOID] = "void",
    [TOK_VAR] = "var",
    [TOK_STATIC] = "static",
    [TOK_EXTERN] = "extern",
    [TOK_COMPT] = "compt",
    [TOK_HID] = "hid",
    [TOK_PUB] = "pub",
    [TOK_HAS] = "has",

    [TOK_IF] = "if",
    [TOK_ELSE] = "else",
    [TOK_WHILE] = "while",
    [TOK_FOR] = "for",
    [TOK_RETURN] = "return",
    [TOK_YIELD] = "yield",
    [TOK_BREAK] = "break",
    [TOK_CONTINUE] = "continue",
    [TOK_MATCH] = "match",

    // more operators
    [TOK_SIZEOF] = "@sizeof",
    [TOK_ALIGNOF] = "@alignof",
    [TOK_ALIGNAS] = "alignas",
    [TOK_TYPEOF] = "typeof",
    [TOK_MOVE] = "move",
    [TOK_AS] = "as",
    [TOK_IS] = "is",
    [TOK_DECAY] = "decay",
    [TOK_STATIC_ASSERT] = "@static_assert",
    [TOK_SAME_TYPE] = "@same_type",
    [TOK_TYPE_TO_STR] = "@type_to_str",
    [TOK_DEFINED] = "@defined",
    [TOK_HAS_CONTRACT] = "@has_contract",
    [TOK_INFERABLE_AS] = "@inferable_as",
    [TOK_ERR] = "@err",
    [TOK_WARN] = "@warn",
    [TOK_NOTE] = "@note",
    [TOK_HELP] = "@help",

    // structures
    [TOK_VARIANT] = "variant",
    [TOK_SELF_ID] = "self",
    [TOK_SELF_TYPE] = "Self",
    [TOK_STRUCT] = "struct",
    [TOK_UNION] = "union",
    [TOK_DEFTYPE] = "deftype",

    // variable / literal types
    [TOK_IDENTIFIER] = "identifier",
    [TOK_CHAR_LIT] = "char literal",
    [TOK_INT_LIT] = "integer literal",
    [TOK_UINT_LIT] = "uint literal",
    [TOK_FLOAT_LIT] = "float literal",
    [TOK_STR_LIT] = "string literal",
    [TOK_BOOL_LIT_TRUE] = "true",
    [TOK_BOOL_LIT_FALSE] = "false",
    [TOK_NULL_LIT] = "null",

    // special punctuation
    [TOK_RARROW] = "->",
    [TOK_DISCARD_RARROW] = "~>",
    [TOK_EQ_ARROW] = "=>",
    [TOK_SCOPE_RES] = "..",
    [TOK_GENERIC_SEP] = "::",

    // range
    [TOK_ELLIPSE] = "...",
    [TOK_ELLIPSE_EQ] = "...=",
    [TOK_IN] = "in",

    // operators
    [TOK_ASSIGN_MOVE] = "<-",
    [TOK_STREAM] = "<<-",
    [TOK_INC] = "++",
    [TOK_DEC] = "--",
    [TOK_LSH] = "<<",
    [TOK_RSHL] = ">>",
    [TOK_RSHA] = ">>>",
    [TOK_BOOL_OR] = "||",
    [TOK_BOOL_AND] = "&&",
    [TOK_GE] = ">=",
    [TOK_LE] = "<=",
    [TOK_BOOL_EQ] = "==",
    [TOK_NE] = "!=",

    // --------------- compound assignment -----------------
    // arithmetic
    [TOK_ASSIGN_PLUS_EQ] = "+=",
    [TOK_ASSIGN_MINUS_EQ] = "-=",
    [TOK_ASSIGN_MULT_EQ] = "*=",
    [TOK_ASSIGN_DIV_EQ] = "/=",
    [TOK_ASSIGN_MOD_EQ] = "%=",

    // bitwise
    [TOK_ASSIGN_AND_EQ] = "&=",
    [TOK_ASSIGN_OR_EQ] = "|=",
    [TOK_ASSIGN_XOR_EQ] = "^=",
    [TOK_ASSIGN_LSH_EQ] = "<<=",
    [TOK_ASSIGN_RSHL_EQ] = ">>=",
    [TOK_ASSIGN_RSHA_EQ] = ">>>=",

    // EOF
    [TOK_EOF] = "eof",
    [TOK_LEX_ERROR_EMPTY_TOKEN] = "err_empty_token",
};

const char* const* token_to_string_map(void) { return token_to_string_map_impl; }

static char always_one_char_to_token_map[TOKEN_CHAR_TO_TOKEN_MAP_SIZE] = {
    // delimiters
    ['('] = TOK_LPAREN,
    [')'] = TOK_RPAREN,
    ['{'] = TOK_LBRACE,
    ['}'] = TOK_RBRACE,
    ['['] = TOK_LBRACK,
    [']'] = TOK_RBRACK,
    // punctuation
    [';'] = TOK_SEMICOLON,
    [','] = TOK_COMMA,
    ['#'] = TOK_HASH_INVOKE,
};
const char* get_always_one_char_to_token_map(void) { return always_one_char_to_token_map; }

static char first_char_in_mc_op_tok_map[TOKEN_CHAR_TO_TOKEN_MAP_SIZE] = {
    // punctuation
    ['.'] = TOK_DOT,
    [':'] = TOK_COLON,

    // assignment
    ['='] = TOK_ASSIGN_EQ,

    // arithmetic
    ['+'] = TOK_PLUS,
    ['-'] = TOK_MINUS,
    ['*'] = TOK_STAR,
    ['/'] = TOK_DIVIDE,
    ['%'] = TOK_MODULO,

    // bitwise
    ['|'] = TOK_BAR,
    ['&'] = TOK_AMPER,
    ['~'] = TOK_BIT_NOT,
    ['^'] = TOK_BIT_XOR,

    // boolean
    ['!'] = TOK_BOOL_NOT,

    // comparison
    ['>'] = TOK_GT,
    ['<'] = TOK_LT,
};
const char* get_first_char_in_multichar_operator_token_map(void) {

    return first_char_in_mc_op_tok_map; // return the map itself for faster look-up
}

// helper
token_type_e token_determine_token_type_for_fixed_symbols(const char* start, size_t length);
// helper
void token_check_if_valid_literal_and_set_value(token_t* tkn);
// helper
void token_check_if_valid_symbol_and_set_sym(token_t* tkn);

/**
 * Builds token according to a starting ptr and length into src as well as an src_loc_t that
 * indicates row/col number for debugging purposes. This function assumes that the lexer has already
 * correct determined the string that needs to be tokenized.
 */
token_t token_build(const char* start, size_t length, src_loc_t* loc) {
    token_t tkn;
    // init provided values
    tkn.start = start;
    tkn.len = length;
    tkn.loc = *loc;
    tkn.val.unsigned_integral = 0; // zero-init value
    // now determine most compilcated fields:
    // keywords and reserved symbols

    tkn.type = token_determine_token_type_for_fixed_symbols(
        start,
        length); // will be set to INDETERMINATE if the token could not be resolved as a fixed
                 // symbol

    if (tkn.type == TOK_INDETERMINATE) {
        token_check_if_valid_literal_and_set_value(
            &tkn); // will appropriately set literal symbol and values, but will leave sym as
                   // INDETERMINATE if no pattern was matched
    }

    if (tkn.type == TOK_INDETERMINATE) {
        token_check_if_valid_symbol_and_set_sym(&tkn);
    }
    return tkn;
}

/**
 * helper that looks up tokens for fixed symbols (i.e keywords and operators), impls basic logic
 * using different look ups for optimization
 */
token_type_e token_determine_token_type_for_fixed_symbols(const char* start, size_t length) {
    const unsigned char* char_to_token_map = get_char_to_token_map();
    if (length == 1) {
        return char_to_token_map[(
            unsigned char)start[0]]; // return token indexed at the mono-char literal, any
                                     // invalid entry will be automatically '\0' since the
                                     // map was statically initialized and token
                                     // INDETERMINATE = 0
    }
    const strimap_t* string_to_token_map = get_string_to_token_strimap();
    token_type_e* tkn_type_ptr_from_multi_char_map
        = (token_type_e*)strimap_viewn(string_to_token_map, start, length);

    if (tkn_type_ptr_from_multi_char_map != NULL) {
        return *tkn_type_ptr_from_multi_char_map; // derefence and return because it
                                                  // was valid and thus found
    }
    return TOK_INDETERMINATE; // because strimap_viewn return NULL, it was thus not found and
                              // INDETERMINATE
}

void token_check_if_valid_literal_and_set_value(token_t* tkn) {
    if (!tkn || tkn->len == 0) {
        if (tkn) {
            tkn->type = TOK_LEX_ERROR_EMPTY_TOKEN;
        }
        return;
    }

    const char* str = tkn->start;
    size_t len = tkn->len;

    // ~~~ CHAR literal: 'a' or escaped like '\n' ~~~
    if (len >= 3 && str[0] == '\'' && str[len - 1] == '\'') {
        char c;
        if (str[1] == '\\' && len >= 4) { // escaped char
            switch (str[2]) {
            case 'n':
                c = '\n';
                break;
            case 't':
                c = '\t';
                break;
            case '\\':
                c = '\\';
                break;
            case '\'':
                c = '\'';
                break;
            case '"':
                c = '\"';
                break;
            case '0':
                c = '\0';
                break;
            default:
                tkn->type = TOK_INDETERMINATE;
                return;
            }
        } else if (len == 3) { // simple char
            c = str[1];
        } else {
            tkn->type = TOK_INDETERMINATE;
            return;
        }
        tkn->type = TOK_CHAR_LIT;
        tkn->val.character = c;
        return;
    }

    // ~~~ STRING literal: "..." ~~~
    if (len >= 2 && str[0] == '"' && str[len - 1] == '"') {
        tkn->type = TOK_STR_LIT;
        // value handling / escape sequences deferred
        return;
    }

    // ~~~ NUMERIC LITERALS ~~~
    // copy to temporary buffer and null-terminate
    char buf[64]; // may need to adjust very long numbers
    if (len >= sizeof(buf)) {
        tkn->type = TOK_INDETERMINATE;
        return;
    }
    memcpy(buf, str, len);
    buf[len] = '\0';

    errno = 0;
    char* endptr = NULL;

    // INT, if needed
    if (str[0] == '-') {

        long long integral_val = strtoll(buf, &endptr, 0);
        if (endptr != buf && errno == 0) {
            while (isspace((unsigned char)*endptr)) {
                endptr++;
            }
            if (*endptr == '\0') {
                tkn->type = TOK_INT_LIT;
                tkn->val.signed_integral = integral_val;
                return;
            }
        }
    }

    // UINT
    errno = 0;
    endptr = NULL;
    unsigned long long uintegral_val = strtoull(buf, &endptr, 0);
    if (endptr != buf && errno == 0) {
        while (isspace((unsigned char)*endptr)) {
            endptr++;
        }
        if (*endptr == '\0') {
            tkn->type = TOK_UINT_LIT;
            tkn->val.unsigned_integral = uintegral_val;
            return;
        }
    }

    // FLOATING, default to double literal
    errno = 0;
    endptr = NULL;
    double floating_val = strtod(buf, &endptr);
    if (endptr != buf && errno == 0) {
        while (isspace((unsigned char)*endptr)) {
            endptr++;
        }
        if (*endptr == '\0') {
            tkn->type = TOK_FLOAT_LIT;
            tkn->val.floating = floating_val;
            return;
        }
    }

    tkn->type = TOK_INDETERMINATE;
}

bool is_whitespace(char c) { return c == ' ' || c == '\n' || c == '\r'; }

/**
 * check if a token is a valid variable/function name (a "symbol")
 */
void token_check_if_valid_symbol_and_set_sym(token_t* tkn) {
    // token should never have size zero
    if (tkn->start[0] >= '0' && tkn->start[0] <= '9') {
        tkn->type = TOK_INDETERMINATE;
        return;
    }
    if (tkn->len <= 0) {
        tkn->type = TOK_LEX_ERROR_EMPTY_TOKEN;
        return;
    }
    tkn->type = TOK_IDENTIFIER;
    for (size_t i = 0; i < tkn->len; i++) {
        if (!isalnum(tkn->start[i]) && tkn->start[i] != '_') {
            tkn->type = TOK_INDETERMINATE;
            return;
        }
    }
}
