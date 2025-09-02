#ifndef C_LEXER_H
#define C_LEXER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "diag.h"

typedef enum {
  CTOK_EOF = 0,

  CTOK_IDENT,
  CTOK_INT_LIT,

  // keywords/types
  CTOK_KW_INT32,   // int32_t
  CTOK_KW_INT64,   // int64_t
  CTOK_KW_BOOL,    // bool
  CTOK_KW_VOID,    // void
  CTOK_KW_IF,      // if
  CTOK_KW_ELSE,    // else
  CTOK_KW_RETURN,  // return
  CTOK_KW_TRUE,    // true
  CTOK_KW_FALSE,   // false
  CTOK_KW_FOR,     // for
  CTOK_KW_WHILE,   // while

  // ops / punct
  CTOK_PLUS, CTOK_MINUS, CTOK_STAR, CTOK_SLASH, CTOK_PERCENT,
  CTOK_EQEQ, CTOK_NEQ, CTOK_LT, CTOK_LE, CTOK_GT, CTOK_GE,
  CTOK_ANDAND, CTOK_OROR, CTOK_NOT,
  CTOK_ASSIGN, CTOK_SEMI, CTOK_COMMA, CTOK_LPAREN, CTOK_RPAREN, CTOK_LBRACE, CTOK_RBRACE,

  CTOK_UNKNOWN = 255
} CTokenKind;

typedef struct {
  CTokenKind  kind;
  const char *lexeme;
  size_t      length;
  SourcePos   pos;
} CToken;

typedef struct {
  const char *filename;
  const char *src;
  size_t length;
  size_t index;
  unsigned line;
  unsigned col;
} CLexer;

void c_lexer_init(CLexer *lx, const char *filename, const char *src, size_t len);
CToken c_lexer_next(CLexer *lx);

const char *ctoken_kind_name(CTokenKind k);

#endif // C_LEXER_H
