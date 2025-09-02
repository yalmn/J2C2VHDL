#ifndef LEXER_H
#define LEXER_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "diag.h"

// ---- Token kinds (MVP) ----
typedef enum {
  TOK_EOF = 0,

  // Identifiers & literals
  TOK_IDENT,
  TOK_INT_LIT,

  // Keywords
  TOK_KW_PUBLIC,
  TOK_KW_CLASS,
  TOK_KW_STATIC,
  TOK_KW_IF,
  TOK_KW_ELSE,
  TOK_KW_WHILE,
  TOK_KW_FOR,
  TOK_KW_RETURN,
  TOK_KW_INT,
  TOK_KW_LONG,
  TOK_KW_BOOLEAN,
  TOK_KW_VOID,
  TOK_KW_TRUE,
  TOK_KW_FALSE,

  // Operators / symbols
  TOK_PLUS,      // +
  TOK_MINUS,     // -
  TOK_STAR,      // *
  TOK_SLASH,     // /
  TOK_PERCENT,   // %
  TOK_EQEQ,      // ==
  TOK_NEQ,       // !=
  TOK_LT,        // <
  TOK_LE,        // <=
  TOK_GT,        // >
  TOK_GE,        // >=
  TOK_ANDAND,    // &&
  TOK_OROR,      // ||
  TOK_NOT,       // !
  TOK_ASSIGN,    // =
  TOK_SEMI,      // ;
  TOK_COMMA,     // ,
  TOK_LPAREN,    // (
  TOK_RPAREN,    // )
  TOK_LBRACE,    // {
  TOK_RBRACE,    // }

  TOK_UNKNOWN = 255
} TokenKind;

typedef struct {
  TokenKind  kind;
  const char *lexeme;   // pointer into source buffer
  size_t     length;    // number of bytes
  SourcePos  pos;       // 1-based start position
} Token;

typedef struct {
  const char *filename;
  const char *src;
  size_t length;
  size_t index;  // byte index into src
  unsigned line; // 1-based
  unsigned col;  // 1-based
} Lexer;

// API
void  lexer_init(Lexer *lx, const char *filename, const char *src, size_t len);
Token lexer_next(Lexer *lx);

// Utility for debugging/tests
const char *token_kind_name(TokenKind k);

#endif // LEXER_H
