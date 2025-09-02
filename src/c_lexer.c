#include "c_lexer.h"
#include <string.h>

static int cur(const CLexer *lx){ return (lx->index>=lx->length)? -1 : (unsigned char)lx->src[lx->index]; }
static int la1(const CLexer *lx){ return (lx->index+1>=lx->length)? -1 : (unsigned char)lx->src[lx->index+1]; }

static void consume(CLexer *lx){
  if (lx->index>=lx->length) return;
  char c = lx->src[lx->index++];
  if (c=='\r'){
    if (lx->index<lx->length && lx->src[lx->index]=='\n') lx->index++;
    lx->line++; lx->col=1;
  } else if (c=='\n'){ lx->line++; lx->col=1; }
  else lx->col++;
}

static bool is_start(int c){ return c=='_' || (c>='A'&&c<='Z') || (c>='a'&&c<='z'); }
static bool is_cont(int c){ return is_start(c) || (c>='0'&&c<='9'); }

static void skip(CLexer *lx){
  for(;;){
    int c = cur(lx);
    if (c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'){ consume(lx); continue; }
    if (c=='/' && la1(lx)=='/'){ consume(lx); consume(lx); while(cur(lx)!=-1 && cur(lx)!='\n' && cur(lx)!='\r') consume(lx); continue; }
    if (c=='#'){ while(cur(lx)!=-1 && cur(lx)!='\n' && cur(lx)!='\r') consume(lx); continue; }
    break;
  }
}

typedef struct { const char *kw; CTokenKind k; } Kw;
static CTokenKind match_kw(const char *p, size_t n){
  static const Kw tab[] = {
    {"int32_t", CTOK_KW_INT32},
    {"int64_t", CTOK_KW_INT64},
    {"bool",    CTOK_KW_BOOL},
    {"void",    CTOK_KW_VOID},
    {"if",      CTOK_KW_IF},
    {"else",    CTOK_KW_ELSE},
    {"return",  CTOK_KW_RETURN},
    {"true",    CTOK_KW_TRUE},
    {"false",   CTOK_KW_FALSE},
    {"for",     CTOK_KW_FOR},
    {"while",   CTOK_KW_WHILE},
  };
  for (size_t i=0;i<sizeof(tab)/sizeof(tab[0]);++i){
    size_t m = strlen(tab[i].kw);
    if (m==n && memcmp(p, tab[i].kw, n)==0) return tab[i].k;
  }
  return CTOK_IDENT;
}

void c_lexer_init(CLexer *lx, const char *filename, const char *src, size_t len){
  lx->filename = filename?filename:"";
  lx->src = src?src:"";
  lx->length = src?len:0;
  lx->index=0; lx->line=1; lx->col=1;
}

CToken c_lexer_next(CLexer *lx){
  skip(lx);
  CToken t; t.lexeme=NULL; t.length=0; t.pos.line=lx->line; t.pos.col=lx->col;

  int c = cur(lx);
  if (c==-1){ t.kind=CTOK_EOF; return t; }

  if (is_start(c)){
    size_t st=lx->index; consume(lx);
    while(is_cont(cur(lx))) consume(lx);
    size_t en=lx->index;
    t.lexeme = lx->src+st; t.length=en-st;
    t.kind = match_kw(t.lexeme, t.length);
    return t;
  }

  if (c>='0' && c<='9'){
    size_t st=lx->index; consume(lx);
    while(cur(lx)>='0' && cur(lx)<='9') consume(lx);
    size_t en=lx->index;
    t.lexeme = lx->src+st; t.length=en-st; t.kind=CTOK_INT_LIT;
    return t;
  }

  #define M2(a,b,Kind) do{ if (cur(lx)==(a) && la1(lx)==(b)){ consume(lx); consume(lx); t.kind=(Kind); return t; } }while(0)
  M2('=','=',CTOK_EQEQ);
  M2('!','=',CTOK_NEQ);
  M2('<','=',CTOK_LE);
  M2('>','=',CTOK_GE);
  M2('&','&',CTOK_ANDAND);
  M2('|','|',CTOK_OROR);
  #undef M2

  switch (c){
    case '+': consume(lx); t.kind=CTOK_PLUS; break;
    case '-': consume(lx); t.kind=CTOK_MINUS; break;
    case '*': consume(lx); t.kind=CTOK_STAR; break;
    case '/': consume(lx); t.kind=CTOK_SLASH; break;
    case '%': consume(lx); t.kind=CTOK_PERCENT; break;
    case '<': consume(lx); t.kind=CTOK_LT; break;
    case '>': consume(lx); t.kind=CTOK_GT; break;
    case '!': consume(lx); t.kind=CTOK_NOT; break;
    case '=': consume(lx); t.kind=CTOK_ASSIGN; break;
    case ';': consume(lx); t.kind=CTOK_SEMI; break;
    case ',': consume(lx); t.kind=CTOK_COMMA; break;
    case '(': consume(lx); t.kind=CTOK_LPAREN; break;
    case ')': consume(lx); t.kind=CTOK_RPAREN; break;
    case '{': consume(lx); t.kind=CTOK_LBRACE; break;
    case '}': consume(lx); t.kind=CTOK_RBRACE; break;
    default:  consume(lx); t.kind=CTOK_UNKNOWN; break;
  }
  if (!t.lexeme){ t.lexeme = lx->src + (lx->index-1); t.length=1; }
  return t;
}

const char *ctoken_kind_name(CTokenKind k){
  switch(k){
    case CTOK_EOF: return "EOF";
    case CTOK_IDENT: return "IDENT";
    case CTOK_INT_LIT: return "INT_LIT";
    case CTOK_KW_INT32: return "KW_INT32";
    case CTOK_KW_INT64: return "KW_INT64";
    case CTOK_KW_BOOL: return "KW_BOOL";
    case CTOK_KW_VOID: return "KW_VOID";
    case CTOK_KW_IF: return "KW_IF";
    case CTOK_KW_ELSE: return "KW_ELSE";
    case CTOK_KW_RETURN: return "KW_RETURN";
    case CTOK_KW_TRUE: return "KW_TRUE";
    case CTOK_KW_FALSE: return "KW_FALSE";
    case CTOK_KW_FOR: return "KW_FOR";
    case CTOK_KW_WHILE: return "KW_WHILE";
    case CTOK_PLUS: return "PLUS"; case CTOK_MINUS: return "MINUS";
    case CTOK_STAR: return "STAR"; case CTOK_SLASH: return "SLASH";
    case CTOK_PERCENT: return "PERCENT";
    case CTOK_EQEQ: return "EQEQ"; case CTOK_NEQ: return "NEQ";
    case CTOK_LT: return "LT"; case CTOK_LE: return "LE";
    case CTOK_GT: return "GT"; case CTOK_GE: return "GE";
    case CTOK_ANDAND: return "ANDAND"; case CTOK_OROR: return "OROR";
    case CTOK_NOT: return "NOT";
    case CTOK_ASSIGN: return "ASSIGN"; case CTOK_SEMI: return "SEMI";
    case CTOK_COMMA: return "COMMA"; case CTOK_LPAREN: return "LPAREN";
    case CTOK_RPAREN: return "RPAREN"; case CTOK_LBRACE: return "LBRACE";
    case CTOK_RBRACE: return "RBRACE";
    case CTOK_UNKNOWN: return "UNKNOWN";
    default: return "???";
  }
}
