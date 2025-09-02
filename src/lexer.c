#include "lexer.h"
#include <string.h>
#include <ctype.h>

// ---------- helpers ----------
static int current(const Lexer *lx) {
  if (lx->index >= lx->length) return -1;
  return (unsigned char)lx->src[lx->index];
}

static int lookahead(const Lexer *lx) {
  if (lx->index + 1 >= lx->length) return -1;
  return (unsigned char)lx->src[lx->index + 1];
}

// consume one logical char; CRLF is treated as a single newline
static void consume(Lexer *lx) {
  if (lx->index >= lx->length) return;
  char c = lx->src[lx->index++];
  if (c == '\r') {
    if (lx->index < lx->length && lx->src[lx->index] == '\n') {
      lx->index++; // swallow LF after CR
    }
    lx->line++;
    lx->col = 1;
  } else if (c == '\n') {
    lx->line++;
    lx->col = 1;
  } else {
    lx->col++;
  }
}

static bool is_ident_start(int c) {
  return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_ident_continue(int c) {
  return is_ident_start(c) || (c >= '0' && c <= '9');
}

static void skip_ws_and_comments(Lexer *lx) {
  for (;;) {
    int c = current(lx);
    // whitespace
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
      consume(lx);
      continue;
    }
    // line comment: //
    if (c == '/' && lookahead(lx) == '/') {
      consume(lx); // '/'
      consume(lx); // second '/'
      while (current(lx) != -1) {
        int d = current(lx);
        if (d == '\n' || d == '\r') break;
        consume(lx);
      }
      // next loop iteration will consume newline as whitespace
      continue;
    }
    break;
  }
}

typedef struct { const char *kw; TokenKind kind; } KwEntry;
static TokenKind match_keyword(const char *p, size_t n) {
  // minimal perfect hash not needed—small linear table is fine (MVP)
  static const KwEntry table[] = {
    {"public", TOK_KW_PUBLIC},
    {"class",  TOK_KW_CLASS},
    {"static", TOK_KW_STATIC},
    {"if",     TOK_KW_IF},
    {"else",   TOK_KW_ELSE},
    {"while",  TOK_KW_WHILE},
    {"for",    TOK_KW_FOR},
    {"return", TOK_KW_RETURN},
    {"int",    TOK_KW_INT},
    {"long",   TOK_KW_LONG},
    {"boolean",TOK_KW_BOOLEAN},
    {"void",   TOK_KW_VOID},
    {"true",   TOK_KW_TRUE},
    {"false",  TOK_KW_FALSE},
  };
  for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); ++i) {
    const char *k = table[i].kw;
    size_t kl = strlen(k);
    if (kl == n && memcmp(p, k, n) == 0) return table[i].kind;
  }
  return TOK_IDENT;
}

// ---------- API ----------
void lexer_init(Lexer *lx, const char *filename, const char *src, size_t len) {
  lx->filename = filename ? filename : "";
  lx->src = src ? src : "";
  lx->length = src ? len : 0u;
  lx->index = 0u;
  lx->line = 1u;
  lx->col = 1u;
}

Token lexer_next(Lexer *lx) {
  skip_ws_and_comments(lx);

  Token t;
  t.lexeme = NULL;
  t.length = 0u;
  t.pos.line = lx->line;
  t.pos.col  = lx->col;

  int c = current(lx);
  if (c == -1) {
    t.kind = TOK_EOF;
    return t;
  }

  // Identifiers / keywords
  if (is_ident_start(c)) {
    size_t start = lx->index;
    consume(lx);
    while (is_ident_continue(current(lx))) consume(lx);
    size_t end = lx->index;
    t.lexeme = lx->src + start;
    t.length = end - start;
    t.kind = match_keyword(t.lexeme, t.length);
    return t;
  }

  // Integer literals (decimal, MVP: digits only)
  if (c >= '0' && c <= '9') {
    size_t start = lx->index;
    consume(lx);
    while (current(lx) >= '0' && current(lx) <= '9') consume(lx);
    size_t end = lx->index;
    t.lexeme = lx->src + start;
    t.length = end - start;
    t.kind = TOK_INT_LIT;
    return t;
  }

  // Two-char operators first
  #define MATCH2(ch1, ch2, KIND) \
    do { if (current(lx)==(ch1) && lookahead(lx)==(ch2)) { consume(lx); consume(lx); t.kind=(KIND); return t; } } while(0)
  MATCH2('=', '=', TOK_EQEQ);
  MATCH2('!', '=', TOK_NEQ);
  MATCH2('<', '=', TOK_LE);
  MATCH2('>', '=', TOK_GE);
  MATCH2('&', '&', TOK_ANDAND);
  MATCH2('|', '|', TOK_OROR);
  #undef MATCH2

  // Single-char tokens
  switch (c) {
    case '+': consume(lx); t.kind = TOK_PLUS;    break;
    case '-': consume(lx); t.kind = TOK_MINUS;   break;
    case '*': consume(lx); t.kind = TOK_STAR;    break;
    case '/': consume(lx); t.kind = TOK_SLASH;   break;
    case '%': consume(lx); t.kind = TOK_PERCENT; break;
    case '<': consume(lx); t.kind = TOK_LT;      break;
    case '>': consume(lx); t.kind = TOK_GT;      break;
    case '!': consume(lx); t.kind = TOK_NOT;     break;
    case '=': consume(lx); t.kind = TOK_ASSIGN;  break;
    case ';': consume(lx); t.kind = TOK_SEMI;    break;
    case ',': consume(lx); t.kind = TOK_COMMA;   break;
    case '(': consume(lx); t.kind = TOK_LPAREN;  break;
    case ')': consume(lx); t.kind = TOK_RPAREN;  break;
    case '{': consume(lx); t.kind = TOK_LBRACE;  break;
    case '}': consume(lx); t.kind = TOK_RBRACE;  break;
    default:
      // Unknown single character
      consume(lx);
      t.kind = TOK_UNKNOWN;
      break;
  }

  // set lexeme for single-char cases / unknown
  if (!t.lexeme) {
    t.lexeme = lx->src + (lx->index - 1);
    t.length = 1;
  }
  return t;
}

// ---------- debug util ----------
const char *token_kind_name(TokenKind k) {
  switch (k) {
    case TOK_EOF: return "EOF";
    case TOK_IDENT: return "IDENT";
    case TOK_INT_LIT: return "INT_LIT";
    case TOK_KW_PUBLIC: return "KW_PUBLIC";
    case TOK_KW_CLASS: return "KW_CLASS";
    case TOK_KW_STATIC: return "KW_STATIC";
    case TOK_KW_IF: return "KW_IF";
    case TOK_KW_ELSE: return "KW_ELSE";
    case TOK_KW_WHILE: return "KW_WHILE";
    case TOK_KW_FOR: return "KW_FOR";
    case TOK_KW_RETURN: return "KW_RETURN";
    case TOK_KW_INT: return "KW_INT";
    case TOK_KW_LONG: return "KW_LONG";
    case TOK_KW_BOOLEAN: return "KW_BOOLEAN";
    case TOK_KW_VOID: return "KW_VOID";
    case TOK_KW_TRUE: return "KW_TRUE";
    case TOK_KW_FALSE: return "KW_FALSE";
    case TOK_PLUS: return "PLUS";
    case TOK_MINUS: return "MINUS";
    case TOK_STAR: return "STAR";
    case TOK_SLASH: return "SLASH";
    case TOK_PERCENT: return "PERCENT";
    case TOK_EQEQ: return "EQEQ";
    case TOK_NEQ: return "NEQ";
    case TOK_LT: return "LT";
    case TOK_LE: return "LE";
    case TOK_GT: return "GT";
    case TOK_GE: return "GE";
    case TOK_ANDAND: return "ANDAND";
    case TOK_OROR: return "OROR";
    case TOK_NOT: return "NOT";
    case TOK_ASSIGN: return "ASSIGN";
    case TOK_SEMI: return "SEMI";
    case TOK_COMMA: return "COMMA";
    case TOK_LPAREN: return "LPAREN";
    case TOK_RPAREN: return "RPAREN";
    case TOK_LBRACE: return "LBRACE";
    case TOK_RBRACE: return "RBRACE";
    case TOK_UNKNOWN: return "UNKNOWN";
    default: return "???";
  }
}
