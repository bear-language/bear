//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#ifndef COMPILER_TOKEN_H
#define COMPILER_TOKEN_H

#include "utils/strimap.h"
#include <stddef.h>
#include <stdint.h>

#define TOKEN_CHAR_TO_TOKEN_MAP_SIZE 256   // unsigned char max value
#define TOKEN_STRING_TO_TOKEN_MAP_SIZE 277 // abitrary, but large and prime to reduce hash conflicts

/// enum representing a type of token in the lexer

#ifdef __cplusplus
extern "C" {
#endif

typedef enum token_type {
    TOK_NONE = 0,
    TOK_INDETERMINATE = 0,
    // mono-char tokens
    // delim
    // brackets, etc
    TOK_LPAREN = '(',
    TOK_RPAREN = ')', // ()
    TOK_LBRACE = '{',
    TOK_RBRACE = '}', // {}
    TOK_LBRACK = '[',
    TOK_RBRACK = ']', // []

    // punc
    TOK_SEMICOLON = ';',
    TOK_DOT = '.',
    TOK_COMMA = ',',
    TOK_COLON = ':',

    // assign
    TOK_ASSIGN_EQ = '=', // =

    // mark invocation
    TOK_HASH_INVOKE = '#',

    // arith
    TOK_PLUS = '+',   // +
    TOK_MINUS = '-',  // -
    TOK_STAR = '*',   // *
    TOK_DIVIDE = '/', // /
    TOK_MODULO = '%', // %

    // bitwise
    TOK_BAR = '|',     // |
    TOK_AMPER = '&',   // & ------> means bitwise and, also means reference
    TOK_BIT_NOT = '~', // ~
    TOK_BIT_XOR = '^', // ^

    // bool
    TOK_BOOL_NOT = '!', // !

    // comparison
    TOK_GT = '>', // will also be used as closing bracket in future generics
    TOK_LT = '<', // will also be used as opening bracket in future generics

    // file
    TOK_IMPORT = 256,
    // keywords
    // module
    TOK_MODULE, // mod
    TOK_USE,
    // function
    TOK_FN,
    TOK_MT, // method, for implicit this ptr for data-associated behavior in structs
    TOK_DT, // dtor
    // integers
    TOK_I8,
    TOK_U8,
    TOK_I16,
    TOK_U16,
    TOK_I32,
    TOK_U32,
    TOK_I64,
    TOK_U64,
    // char
    TOK_CHAR, // 32 bit unicode character
    // floating
    TOK_F32,
    TOK_F64,
    // special
    TOK_STR, // utf8 str
    TOK_BOOL,
    // more special
    TOK_VOID,  // no type
    TOK_VAR,   // like java var
    TOK_COMPT, // compile-time (like constexpr)
    TOK_HID,   // like private, but with slightly different semantics because BearLang has no
               // inheritance, so this is really just a hidden data member or function/method
    TOK_PUB,
    TOK_MUT, // example: mut i32
    TOK_HAS,
    TOK_DEFTYPE,
    TOK_VARIANT, // scoped/type-checked enums
    // memory location identifiers
    TOK_STATIC,
    TOK_EXTERN,

    // more operators
    TOK_SIZEOF,
    TOK_ALIGNOF,
    TOK_ALIGNAS,
    TOK_TYPEOF,
    TOK_MOVE,
    TOK_DECAY, // decay
    TOK_AS,
    TOK_IS, // for variant decomp

    // @____ style builtins

    TOK_SAME_TYPE,     // @same_type
    TOK_TYPE_TO_STR,   // @type_to_str
    TOK_STATIC_ASSERT, // @static_assert
    TOK_DEFINED,       // @defined
    TOK_HAS_CONTRACT,  // @has_contract
    TOK_INFERABLE_AS,  // @inferable_as
    TOK_ERR,           // @err
    TOK_WARN,          // @warn
    TOK_NOTE,          // @note
    TOK_HELP,          // @help

    // constructs
    TOK_REQUIRES,
    TOK_CONTRACT,
    TOK_UNION,

    // ctrl flow
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_IN, // example: for (int a in myArr) {...}
    TOK_RETURN,
    TOK_YIELD,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_MATCH,

    // structures (incorp after prodcedural is working)
    TOK_SELF_ID,
    TOK_SELF_TYPE,
    TOK_STRUCT,

    // variable or function name
    TOK_IDENTIFIER,

    // built-in types
    TOK_CHAR_LIT,
    TOK_INT_LIT,
    TOK_UINT_LIT,
    TOK_FLOAT_LIT, // currently float literals not suported, so we would need to downcast at
                   // comptime
    TOK_STR_LIT,
    TOK_BOOL_LIT_FALSE,
    TOK_BOOL_LIT_TRUE,
    TOK_NULL_LIT,

    // punc
    TOK_EQ_ARROW,
    TOK_RARROW,         // -> for return types
    TOK_DISCARD_RARROW, // ~> for return types
    TOK_SCOPE_RES,      // ..
    TOK_GENERIC_SEP,    // :: , for example, box::int

    // operators
    // range
    TOK_ELLIPSE,
    TOK_ELLIPSE_EQ,
    // assign
    TOK_ASSIGN_MOVE, // <-
    // stream
    TOK_STREAM, // <<-
    // arith
    TOK_INC, // ++
    TOK_DEC, // --
    // bitwise
    TOK_LSH,  // <<
    TOK_RSHL, // >>
    TOK_RSHA, // >>>
    // bool
    TOK_BOOL_OR,  // ||
    TOK_BOOL_AND, // &&
    // comparison
    TOK_GE,
    TOK_LE,
    TOK_BOOL_EQ,
    TOK_NE,

    // --------------- compound assignment -----------------
    // arithmetic
    TOK_ASSIGN_PLUS_EQ,  // +=
    TOK_ASSIGN_MINUS_EQ, // -=
    TOK_ASSIGN_MULT_EQ,  // *=
    TOK_ASSIGN_DIV_EQ,   // /=
    TOK_ASSIGN_MOD_EQ,   // %=

    // bitwise
    TOK_ASSIGN_AND_EQ,  // &=
    TOK_ASSIGN_OR_EQ,   // |=
    TOK_ASSIGN_XOR_EQ,  // ^=
    TOK_ASSIGN_LSH_EQ,  // <<=
    TOK_ASSIGN_RSHL_EQ, // >>=
    TOK_ASSIGN_RSHA_EQ, // >>>=

    // EOF
    TOK_EOF,

    // error states (represent an error in lexing, should never happen and should not be triggered
    // by syntax errors)
    TOK_LEX_ERROR_EMPTY_TOKEN,

    // num token_type_e
    TOK__NUM,
} token_type_e;

/// stores the value of literals in source code
typedef union token_value {
    char character;
    int64_t signed_integral;
    uint64_t unsigned_integral;
    double floating;
} token_value_u;

/// represent a (line, col) index in a source file, zero-indexed
typedef struct src_loc {
    size_t line, col;
} src_loc_t;

/// represent a token in source code, generated by the lexer
typedef struct token {
    /// NOT NULL-TERMINATED; non-owning view into source buffer
    const char* start;
    /// len in source
    size_t len;
    token_type_e type;
    /// get value using sym, only valid for numeric literals
    token_value_u val;
    /// line & col, for error messages
    src_loc_t loc;
} token_t;

typedef struct token_ptr_slice {
    token_t** start;
    size_t len;
} token_ptr_slice_t;

typedef struct token_range {
    token_t* first;
    token_t* last;
} token_range_t;

/// returns a ptr to a char-to token map, used for mono-char tokens (e.g.: +), indexed by char
/// values!
const unsigned char* get_char_to_token_map(void);
/// returns a ptr to a strimap_t {const char*, token_type_e}
const strimap_t* get_string_to_token_strimap(void);
/// returns a ptr to a map for token_type_e -> const char*, indexed by token_type_e's!
const char* const* token_to_string_map(void);
/// returns a ptr to a map indicated whether a given char is always associated with a mono-char
/// token
const char* get_always_one_char_to_token_map(void);
/// returns a ptr to a map indicating whether a given char is the first char in a multichar operator
/// (e.g.: ==)
const char* get_first_char_in_multichar_operator_token_map(void);

bool is_whitespace(char c);

/**
 * builds token according to a starting ptr and length into src as well as an src_loc_t that
 * indicates row/col number for debugging purposes. This function assumes that the lexer has already
 * correctly determined the string that needs to be tokenized.
 */
token_t token_build(const char* start, size_t length, src_loc_t* loc);

/**
 * destroy global token look-up maps
 * - this operation is thread-safe although should probably only ever be called after all threads
 * are joined and/or do not need to use any token look=up tables
 */
void token_maps_free(void);

#ifdef __cplusplus
}
#endif

#endif // !COMPILER_TOKEN_H
