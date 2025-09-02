#include "c_parser.h"
#include "c_lexer.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
  const char *filename;
  CLexer lx;
  CToken tok;
  bool had_error;
  bool unsupported;
} CParser;

static void next(CParser *p){ p->tok = c_lexer_next(&p->lx); }
static const char *kname(CTokenKind k){ return ctoken_kind_name(k); }
static void perr(CParser *p, const char *msg){ diag_error_at(p->filename, p->tok.pos, "%s", msg); p->had_error=true; }
static void expect(CParser *p, CTokenKind k){
  if (p->tok.kind==k){ next(p); return; }
  diag_error_at(p->filename, p->tok.pos, "expected %s, got %s", kname(k), kname(p->tok.kind));
  p->had_error = true;
}

static int64_t parse_int64(const char *s, size_t n){
  int64_t v=0; for(size_t i=0;i<n;i++){ char c=s[i]; if(c<'0'||c>'9') break; v = v*10 + (c-'0'); } return v;
}

static Type parse_type(CParser *p, bool allow_void){
  SourcePos pos = p->tok.pos;
  if (p->tok.kind==CTOK_KW_INT32){ next(p); return ast_type(TY_INT,pos); }
  if (p->tok.kind==CTOK_KW_INT64){ next(p); return ast_type(TY_LONG,pos); }
  if (p->tok.kind==CTOK_KW_BOOL){ next(p); return ast_type(TY_BOOLEAN,pos); }
  if (p->tok.kind==CTOK_KW_VOID){
    if (!allow_void){ diag_error_at(p->filename, pos, "void not allowed here"); p->had_error=true; }
    next(p); return ast_type(TY_VOID,pos);
  }
  perr(p, "type expected");
  return ast_type(TY_INT,pos);
}

static char *take_ident(CParser *p){
  if (p->tok.kind!=CTOK_IDENT){ perr(p, "identifier expected"); return ast_strdup_cstr("<error>"); }
  char *s = ast_strdup_slice(p->tok.lexeme, p->tok.length);
  next(p);
  return s;
}

// fwd decl expr/stmt
static Expr* parse_expression(CParser *p);
static Expr* parse_assignment(CParser *p);
static Expr* parse_or(CParser *p);
static Expr* parse_and(CParser *p);
static Expr* parse_eq(CParser *p);
static Expr* parse_rel(CParser *p);
static Expr* parse_add(CParser *p);
static Expr* parse_mul(CParser *p);
static Expr* parse_un(CParser *p);
static Expr* parse_primary(CParser *p);

static Stmt* parse_block(CParser *p);
static Stmt* parse_statement(CParser *p);
static Stmt* parse_if(CParser *p);
static Stmt* parse_while(CParser *p);
static Stmt* parse_for(CParser *p);
static Stmt* parse_return(CParser *p);
static Stmt* parse_vardecl(CParser *p);

// ---- params ----
static void parse_params(CParser *p, VecParam *out){
  VecParam_init(out);
  if (p->tok.kind==CTOK_RPAREN){ next(p); return; }
  for(;;){
    Type t = parse_type(p, false);
    char *name = take_ident(p);
    Param pr = (Param){ .type=t, .name=name, .pos=t.pos };
    if (!VecParam_push(out, pr)){ perr(p,"oom"); return; }
    if (p->tok.kind==CTOK_RPAREN){ next(p); break; }
    expect(p, CTOK_COMMA);
  }
}

// ---- statements ----
static Stmt* parse_block(CParser *p){
  expect(p, CTOK_LBRACE);
  Stmt *blk = ast_new_block(p->tok.pos);
  while (p->tok.kind!=CTOK_RBRACE && p->tok.kind!=CTOK_EOF){
    Stmt *s = parse_statement(p);
    if (s){ if (!VecStmt_push(&blk->block.stmts, s)){ perr(p,"oom"); break; } }
    else {
      while (p->tok.kind!=CTOK_SEMI && p->tok.kind!=CTOK_RBRACE && p->tok.kind!=CTOK_EOF) next(p);
      if (p->tok.kind==CTOK_SEMI) next(p);
    }
  }
  expect(p, CTOK_RBRACE);
  return blk;
}

static Stmt* parse_if(CParser *p){
  SourcePos pos = p->tok.pos; expect(p, CTOK_KW_IF);
  expect(p, CTOK_LPAREN);
  Expr *cond = parse_expression(p);
  expect(p, CTOK_RPAREN);
  Stmt *then = parse_statement(p);
  Stmt *els = NULL;
  if (p->tok.kind==CTOK_KW_ELSE){ next(p); els = parse_statement(p); }
  return ast_new_if(pos, cond, then, els);
}

static Stmt* parse_while(CParser *p){
  SourcePos pos = p->tok.pos; expect(p, CTOK_KW_WHILE);
  expect(p, CTOK_LPAREN);
  Expr *cond = parse_expression(p);
  expect(p, CTOK_RPAREN);
  Stmt *body = parse_statement(p);
  return ast_new_while(pos, cond, body);
}

static Stmt* parse_for(CParser *p){
  SourcePos pos = p->tok.pos; expect(p, CTOK_KW_FOR);
  expect(p, CTOK_LPAREN);
  Expr *init=NULL, *cond=NULL, *post=NULL;
  if (p->tok.kind!=CTOK_SEMI){ init = parse_expression(p); }
  expect(p, CTOK_SEMI);
  if (p->tok.kind!=CTOK_SEMI){ cond = parse_expression(p); }
  expect(p, CTOK_SEMI);
  if (p->tok.kind!=CTOK_RPAREN){ post = parse_expression(p); }
  expect(p, CTOK_RPAREN);
  Stmt *body = parse_statement(p);
  return ast_new_for(pos, init, cond, post, body);
}

static Stmt* parse_return(CParser *p){
  SourcePos pos = p->tok.pos; expect(p, CTOK_KW_RETURN);
  Expr *e = NULL;
  if (p->tok.kind!=CTOK_SEMI) e = parse_expression(p);
  expect(p, CTOK_SEMI);
  return ast_new_return(pos, e);
}

static Stmt* parse_vardecl(CParser *p){
  Type t = parse_type(p, false);
  char *name = take_ident(p);
  Expr *init = NULL;
  if (p->tok.kind==CTOK_ASSIGN){ next(p); init = parse_expression(p); }
  expect(p, CTOK_SEMI);
  return ast_new_vardecl(p->tok.pos, t, name, init);
}

static Stmt* parse_expr_stmt(CParser *p){
  SourcePos pos = p->tok.pos;
  Expr *e = parse_expression(p);
  expect(p, CTOK_SEMI);
  return ast_new_exprstmt(pos, e);
}

static Stmt* parse_statement(CParser *p){
  switch (p->tok.kind){
    case CTOK_LBRACE:   return parse_block(p);
    case CTOK_KW_IF:    return parse_if(p);
    case CTOK_KW_WHILE: return parse_while(p);
    case CTOK_KW_FOR:   return parse_for(p);
    case CTOK_KW_RETURN:return parse_return(p);
    case CTOK_KW_INT32:
    case CTOK_KW_INT64:
    case CTOK_KW_BOOL:  return parse_vardecl(p);
    default:            return parse_expr_stmt(p);
  }
}

// ---- expressions ----
static Expr* parse_expression(CParser *p){ return parse_assignment(p); }
static Expr* parse_assignment(CParser *p){
  Expr *l = parse_or(p);
  if (p->tok.kind==CTOK_ASSIGN){ SourcePos pos = p->tok.pos; next(p); Expr *r = parse_assignment(p); return ast_new_assign(pos,l,r); }
  return l;
}
static Expr* parse_or(CParser *p){
  Expr *e = parse_and(p);
  while (p->tok.kind==CTOK_OROR){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_and(p); e=ast_new_bin(pos,BIN_OR,e,r); }
  return e;
}
static Expr* parse_and(CParser *p){
  Expr *e = parse_eq(p);
  while (p->tok.kind==CTOK_ANDAND){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_eq(p); e=ast_new_bin(pos,BIN_AND,e,r); }
  return e;
}
static Expr* parse_eq(CParser *p){
  Expr *e = parse_rel(p);
  for(;;){
    if (p->tok.kind==CTOK_EQEQ){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_rel(p); e=ast_new_bin(pos,BIN_EQ,e,r); }
    else if (p->tok.kind==CTOK_NEQ){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_rel(p); e=ast_new_bin(pos,BIN_NEQ,e,r); }
    else break;
  }
  return e;
}
static Expr* parse_rel(CParser *p){
  Expr *e = parse_add(p);
  for(;;){
    if (p->tok.kind==CTOK_LT){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_add(p); e=ast_new_bin(pos,BIN_LT,e,r); }
    else if (p->tok.kind==CTOK_LE){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_add(p); e=ast_new_bin(pos,BIN_LE,e,r); }
    else if (p->tok.kind==CTOK_GT){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_add(p); e=ast_new_bin(pos,BIN_GT,e,r); }
    else if (p->tok.kind==CTOK_GE){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_add(p); e=ast_new_bin(pos,BIN_GE,e,r); }
    else break;
  }
  return e;
}
static Expr* parse_add(CParser *p){
  Expr *e = parse_mul(p);
  for(;;){
    if (p->tok.kind==CTOK_PLUS){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_mul(p); e=ast_new_bin(pos,BIN_ADD,e,r); }
    else if (p->tok.kind==CTOK_MINUS){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_mul(p); e=ast_new_bin(pos,BIN_SUB,e,r); }
    else break;
  }
  return e;
}
static Expr* parse_mul(CParser *p){
  Expr *e = parse_un(p);
  for(;;){
    if (p->tok.kind==CTOK_STAR){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_un(p); e=ast_new_bin(pos,BIN_MUL,e,r); }
    else if (p->tok.kind==CTOK_SLASH){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_un(p); e=ast_new_bin(pos,BIN_DIV,e,r); }
    else if (p->tok.kind==CTOK_PERCENT){ SourcePos pos=p->tok.pos; next(p); Expr*r=parse_un(p); e=ast_new_bin(pos,BIN_MOD,e,r); }
    else break;
  }
  return e;
}
static Expr* parse_un(CParser *p){
  if (p->tok.kind==CTOK_NOT){ SourcePos pos=p->tok.pos; next(p); Expr*x=parse_un(p); return ast_new_un(pos,UN_NOT,x); }
  if (p->tok.kind==CTOK_MINUS){ SourcePos pos=p->tok.pos; next(p); Expr*x=parse_un(p); return ast_new_un(pos,UN_NEG,x); }
  return parse_primary(p);
}
static Expr* parse_primary(CParser *p){
  CToken t = p->tok;
  switch(t.kind){
    case CTOK_INT_LIT:{ int64_t v=parse_int64(t.lexeme,t.length); next(p); return ast_new_int(t.pos,v); }
    case CTOK_KW_TRUE: next(p); return ast_new_bool(t.pos,true);
    case CTOK_KW_FALSE: next(p); return ast_new_bool(t.pos,false);
    case CTOK_IDENT:{
      char *name = ast_strdup_slice(t.lexeme,t.length);
      next(p);
      if (p->tok.kind==CTOK_LPAREN){
        while(p->tok.kind!=CTOK_RPAREN && p->tok.kind!=CTOK_EOF) next(p);
        if (p->tok.kind==CTOK_RPAREN) next(p);
        diag_error_at(p->filename, t.pos, "function calls in expressions not supported (MVP)");
        p->unsupported = true;
        return ast_new_int(t.pos, 0);
      }
      return ast_new_var(t.pos, name);
    }
    case CTOK_LPAREN:
      next(p); {
        Expr *e = parse_expression(p);
        expect(p, CTOK_RPAREN);
        return e;
      }
    default:
      perr(p, "unexpected token in expression");
      next(p);
      return ast_new_int(t.pos,0);
  }
}

// ---- top-level ----
static Method* parse_function(CParser *p){
  Type ret = parse_type(p, true);
  char *name = take_ident(p);
  expect(p, CTOK_LPAREN);
  VecParam params; parse_params(p, &params);
  Stmt *body = parse_block(p);
  Method *m = (Method*)calloc(1,sizeof(Method));
  if (!m){ perr(p,"oom"); return NULL; }
  m->ret_type=ret; m->name=name; m->params=params; m->body=body; m->pos = ret.pos;
  return m;
}

Program *parse_c(const char *filename, const char *buffer, size_t len, bool *unsupported){
  CParser p = {0};
  p.filename = filename?filename:"";
  c_lexer_init(&p.lx, filename, buffer, len);
  next(&p);

  Program *prog = ast_program_new();
  if (!prog){ diag_error_at(filename, (SourcePos){0,0}, "out of memory"); return NULL; }

  while (p.tok.kind != CTOK_EOF){
    Method *m = parse_function(&p);
    if (!m) { p.had_error=true; break; }
    ClassDecl *cls = NULL;
    if (prog->classes.len==0){
      cls = (ClassDecl*)calloc(1,sizeof(ClassDecl));
      if (!cls){ diag_error_at(filename,(SourcePos){0,0},"oom"); ast_free_program(prog); return NULL; }
      cls->name = ast_strdup_cstr("CModule");
      VecField_init(&cls->fields);
      VecMethod_init(&cls->methods);
      if (!VecClass_push(&prog->classes, cls)){ diag_error_at(filename,(SourcePos){0,0},"oom"); ast_free_program(prog); return NULL; }
    } else {
      cls = prog->classes.items[0];
    }
    if (!VecMethod_push(&cls->methods, m)){ diag_error_at(filename,(SourcePos){0,0},"oom"); ast_free_program(prog); return NULL; }
  }

  if (unsupported) *unsupported = p.unsupported;
  if (p.had_error){ ast_free_program(prog); return NULL; }
  return prog;
}
