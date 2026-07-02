//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/lexer.h"
#include "compiler/token.h"
#include "utils/log.h"
#include "utils/vector.h"
#include <stddef.h>

vector_t lexer_tokenize_src_buffer(const src_buffer_t* buf) {
    vector_t tkn_vec
        = vector_create_and_reserve(sizeof(token_t), buf->size / LEXER_ESTIMATED_CHARS_PER_TOKEN);

    // tkn string view params
    char* start = buf->data;               // start of tkn's view into buf
    size_t len = 0;                        // len of tkn's view into buf
    src_loc_t loc = {.line = 0, .col = 0}; // location in src file
    size_t col = 0;
    token_t tkn; // scratch token

    char* pos = buf->data;
    const char* end_of_buf = buf->data + buf->size;
    char c;  // cached curr char, pos[0]
    char n1; // cached next char for lookaheads, pos[1]

    // maps
    const char* always_one_char_map = get_always_one_char_to_token_map();
    const char* first_char_in_multichar_operator_token_map
        = get_first_char_in_multichar_operator_token_map();

// pushes any previous token and delimits by pushing new token of a known length N in chars
#define LEX_KNOWN_LEN_PUSH(N)                                                                      \
    do {                                                                                           \
        if (len != 0) {                                                                            \
            *((token_t*)vector_emplace_back(&tkn_vec)) = token_build(start, len, &loc);            \
            len = 0;                                                                               \
            loc.col = col;                                                                         \
            start = pos;                                                                           \
        }                                                                                          \
        *((token_t*)vector_emplace_back(&tkn_vec)) = token_build(start, N, &loc);                  \
        pos += (N);                                                                                \
        col += (N);                                                                                \
        loc.col = col;                                                                             \
        start = pos;                                                                               \
        len = 0;                                                                                   \
        goto lex_start;                                                                            \
    } while (0)

#define LEX_IN_LITERAL(D)                                                                          \
    while (true) {                                                                                 \
        c = *(++pos);                                                                              \
        ++len;                                                                                     \
        ++col;                                                                                     \
        if (c == '\n') {                                                                           \
            *((token_t*)vector_emplace_back(&tkn_vec)) = token_build(start, len, &loc);            \
            len = 0;                                                                               \
            col = 0;                                                                               \
            ++loc.line;                                                                            \
            start = pos;                                                                           \
            break;                                                                                 \
        }                                                                                          \
        if (pos - 1 >= buf->data && c == (D) && *(pos - 1) != '\\') {                              \
            ++len;                                                                                 \
            *((token_t*)vector_emplace_back(&tkn_vec)) = token_build(start, len, &loc);            \
            len = 0;                                                                               \
            ++pos;                                                                                 \
            start = pos;                                                                           \
            break;                                                                                 \
        }                                                                                          \
    }                                                                                              \
    do {                                                                                           \
        loc.col = col;                                                                             \
    } while (0);                                                                                   \
    goto lex_start;

lex_start:
    c = *pos;
    if (always_one_char_map[(unsigned char)c]) {
        LEX_KNOWN_LEN_PUSH(1);
    }
    if (c == '/' && (pos + 1 < end_of_buf) && pos[1] == '/') {
        goto lex_inline_comment;
    }
    if (first_char_in_multichar_operator_token_map[(unsigned char)c]) {
        goto lex_multichar_operator;
    };
    if (c == ' ' || c == '\t') {
        goto lex_whitespace;
    }
    if (c == '\n') {
        goto lex_newline;
    }
    if (c == '\'') {
        LEX_IN_LITERAL('\'');
    }
    if (c == '\"') {
        LEX_IN_LITERAL('\"');
    }

    if (pos >= buf->data + buf->src_len) {
        goto lex_end;
    }
    ++pos;
    ++len;
    ++col;
    goto lex_start;

    // add any new maximum-munch operators into here
lex_multichar_operator:
    if (pos + 1 < end_of_buf) {
        n1 = pos[1]; // cache
    } else {
        n1 = '\0'; // makes future n1 bounds check unnecessary
    }
    switch (c) {
    case ('.'): {
        if (pos + 3 < end_of_buf && n1 == '.' && pos[2] == '.' && pos[3] == '=') {
            // ...=
            LEX_KNOWN_LEN_PUSH(4);
        }
        if (pos + 2 < end_of_buf && n1 == '.' && pos[2] == '.') {
            // ...
            LEX_KNOWN_LEN_PUSH(3);
        }
        if (n1 == '.') {
            LEX_KNOWN_LEN_PUSH(2);
        }
        if ((n1) >= '0' && n1 <= '9') {
            // just proceed, this is a float lit
            ++pos;
            ++len;
            ++col;
            goto lex_start;
        }
        // else
        LEX_KNOWN_LEN_PUSH(1);
    }
    // assignment
    case ('='): {
        if (n1 == '=' || n1 == '>') {
            LEX_KNOWN_LEN_PUSH(2);
        }
        LEX_KNOWN_LEN_PUSH(1);
    }

    // arithmetic
    case ('+'): {
        if (n1 == '=' || n1 == '+') {
            // ++ or +=
            LEX_KNOWN_LEN_PUSH(2);
        }
        LEX_KNOWN_LEN_PUSH(1);
    }
    case ('-'): {
        /*
if ((n1) >= '0' && n1 <= '9') {
// just proceed, this is a numerical lit
++pos;
++len;
++col;
goto lex_start;
}
*/
        if (n1 == '=' || n1 == '-' || n1 == '>') {
            // --, ->, or -=
            LEX_KNOWN_LEN_PUSH(2);
        }
        LEX_KNOWN_LEN_PUSH(1);
    }
    case ('|'):
        if (n1 == '|') {
            // ||
            LEX_KNOWN_LEN_PUSH(2);
        }
        if (n1 == '=') {
            // |=
            LEX_KNOWN_LEN_PUSH(2);
        }
        LEX_KNOWN_LEN_PUSH(1);
    case ('&'):
        if (n1 == '&') {
            // ||
            LEX_KNOWN_LEN_PUSH(2);
        }
        if (n1 == '=') {
            // &=
            LEX_KNOWN_LEN_PUSH(2);
        }
        LEX_KNOWN_LEN_PUSH(1);
    case ('~'):
        if (n1 == '>' || n1 == '=') {
            // ~>
            LEX_KNOWN_LEN_PUSH(2);
        }
        LEX_KNOWN_LEN_PUSH(1);
        break;
    // intentional fallthrough ~~~~~~~~~~~~~~~~~~~~
    case ('*'):
    case ('/'):
    case ('%'):
    // bitwise
    case ('^'):
    // boolean
    case ('!'):
        if (n1 == '=') {
            // [sym]=
            LEX_KNOWN_LEN_PUSH(2);
        }
        LEX_KNOWN_LEN_PUSH(1);
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // comparison
    case ('>'): {
        if (pos + 3 < end_of_buf && n1 == '>' && pos[2] == '>' && pos[3] == '=') {
            // >>>=
            LEX_KNOWN_LEN_PUSH(4);
        }
        if (pos + 2 < end_of_buf && n1 == '>' && (pos[2] == '>' || pos[2] == '=')) {
            // >>> or >>=
            LEX_KNOWN_LEN_PUSH(3);
        }
        if (n1 == '>' || n1 == '=') {
            // >> or >=
            LEX_KNOWN_LEN_PUSH(2);
        }
        // >
        LEX_KNOWN_LEN_PUSH(1);
    }
    case ('<'): {
        if (pos + 2 < end_of_buf && n1 == '<' && (pos[2] == '=' || pos[2] == '-')) {
            // <<= or <<-
            LEX_KNOWN_LEN_PUSH(3);
        }
        if (n1 == '<' || n1 == '=' || n1 == '-') {
            // <<, <=, or <-
            LEX_KNOWN_LEN_PUSH(2);
        }
        // // <
        LEX_KNOWN_LEN_PUSH(1);
    }
    case (':'): {
        if (n1 == ':') {
            // ::
            LEX_KNOWN_LEN_PUSH(2);
        }
        // :
        LEX_KNOWN_LEN_PUSH(1);
    }
    default: {
        LOG_ERR("unexpected first character in multichar operator during lexing.");
    }
    }
    goto lex_start;

lex_whitespace:
    if (len != 0) {
        *((token_t*)vector_emplace_back(&tkn_vec)) = token_build(start, len, &loc);
        len = 0;
    }
    ++col;
    loc.col = col;
    ++pos;
    start = pos;
    goto lex_start;

lex_newline:
    // pushes any in-progress token, this part may break
    if (len != 0) {
        *((token_t*)vector_emplace_back(&tkn_vec)) = token_build(start, len, &loc);
        len = 0;
    }
    ++loc.line;
    loc.col = 0;
    col = 0;
    ++pos;
    start = pos;
    goto lex_start;

lex_inline_comment:
    if (len != 0) {
        *((token_t*)vector_emplace_back(&tkn_vec)) = token_build(start, len, &loc);
        len = 0;
        loc.col = col;
        start = pos;
    }
    while (true) {
        c = *pos++;
        if (c == '\n') {
            break;
        }
        if (c == '\0') {
            goto lex_done;
        }
    }
    start = pos;
    len = 0;
    col = 0;
    loc.col = 0;
    ++loc.line;
    goto lex_start;

lex_end:
    if (len != 0) {
        *((token_t*)vector_emplace_back(&tkn_vec)) = token_build(start, len, &loc);
    }
lex_done:;
    // build up eof token manually, we have to do this for pretty error messages
    token_t* prev = (token_t*)vector_last(&tkn_vec);
    if (prev) {
        tkn.start = prev->start; // set to prev's start!
        tkn.len = 1;             // this is safe since we use prev loc
        tkn.type = TOK_EOF;
        tkn.loc = prev->loc; // set loc to prev valid loc!
        ++tkn.loc.col;
    } else {
        tkn = (token_t){.start = buf->data, .len = 0, .loc = loc, .val = {0}, .type = TOK_EOF};
    }
    vector_push_back(&tkn_vec, &tkn);
    return tkn_vec;
}
