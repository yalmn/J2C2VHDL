#include "parser.h"
#include "lexer.h"
#include <string.h>
#include <ctype.h>

// -------- Parser state --------
typedef struct {
  const char *filename;
  Lexer lx;
  Token tok;        // current lookahead
  bool had_error;
} Parser;

static void next(Parser *p){ p->tok = lexer_next(&p->lx); }
static const char* kname(TokenKind k){ return token_kind_name(k); }

static void perr_tok(Parser *p, const char *msg){
  diag_error_at(p->filename, p->tok.pos, "%s", msg);
  p->had_error = true;
}

static void perr_expect(Parser *p, TokenKind expected){
  diag_error_at(p->filename, p->tok.pos, "expected %s, got %s",
                kname(expected), kname(p->tok.kind));
  p->had_error = true;
}

static bool match(Parser *p, TokenKind k){
  if (p->tok.kind == k){ next(p); return true; }
  return false;
}
static bool expect(Parser *p, TokenKind k){
  if (match(p,k)) return true;
  perr_expect(p,k);
  return false;
}

static int64_t parse_int_literal(const char *s, size_t n){
  int64_t v = 0;
  for (size_t i=0;i<n;i++){
    char c=s[i];
    if (c<'0'||c>'9') break;
    v = v*10 + (int64_t)(c - '0');
  }
  return v;
}

// Forward decls
static Program*    parse_program(Parser *p);
static ClassDecl*  parse_class(Parser *p);
static Field*      parse_field(Parser *p, Type t, char *name, SourcePos pos);
static Type        parse_type(Parser *p, bool allow_void);
static char*       take_ident(Parser *p, const Token *t);
static Stmt*       parse_block(Parser *p);
static Stmt*       parse_statement(Parser *p);
static Stmt*       parse_if(Parser *p);
static Stmt*       parse_while(Parser *p);
static Stmt*       parse_for(Parser *p);
static Stmt*       parse_return(Parser *p);
static Stmt*       parse_vardecl_stmt(Parser *p);
static Stmt*       parse_expr_stmt(Parser *p);

static Expr* parse_expression(Parser *p);
static Expr* parse_assignment(Parser *p);
static Expr* parse_or(Parser *p);
static Expr* parse_and(Parser *p);
static Expr* parse_equality(Parser *p);
static Expr* parse_rel(Parser *p);
static Expr* parse_add(Parser *p);
static Expr* parse_mul(Parser *p);
static Expr* parse_unary(Parser *p);
static Expr* parse_primary(Parser *p);

// -------- Implementation --------
static char* take_ident(Parser *p, const Token *t){
  if (t->kind != TOK_IDENT){
    perr_expect(p, TOK_IDENT);
  }
  return ast_strdup_slice(t->lexeme, t->length);
}

static Type parse_type(Parser *p, bool allow_void){
  SourcePos pos = p->tok.pos;
  if (p->tok.kind == TOK_KW_INT){    next(p); return ast_type(TY_INT, pos); }
  if (p->tok.kind == TOK_KW_LONG){   next(p); return ast_type(TY_LONG, pos); }
  if (p->tok.kind == TOK_KW_BOOLEAN){next(p); return ast_type(TY_BOOLEAN, pos); }
  if (p->tok.kind == TOK_KW_VOID){
    if (!allow_void){
      diag_error_at(p->filename, pos, "void is not allowed here");
      p->had_error = true;
    }
    next(p); return ast_type(TY_VOID, pos);
  }
  perr_tok(p, "expected a type (int|long|boolean|void)");
  return ast_type(TY_INT, pos);
}

static void parse_param_list(Parser *p, VecParam *out){
  VecParam_init(out);
  if (match(p, TOK_RPAREN)) return; // empty
  for (;;) {
    Type t = parse_type(p, false);
    if (p->tok.kind != TOK_IDENT){ perr_expect(p, TOK_IDENT); break; }
    char *name = take_ident(p, &p->tok); next(p);
    Param par = (Param){ .type=t, .name=name, .pos=t.pos };
    if (!VecParam_push(out, par)){ perr_tok(p, "out of memory"); return; }
    if (match(p, TOK_RPAREN)) break;
    if (!expect(p, TOK_COMMA)) break;
  }
}

static void parse_arg_list(Parser *p, Expr *call){
  if (match(p, TOK_RPAREN)) return;
  for (;;) {
    Expr *e = parse_expression(p);
    if (!VecExpr_push(&call->call.args, e)){ perr_tok(p, "out of memory"); return; }
    if (match(p, TOK_RPAREN)) break;
    if (!expect(p, TOK_COMMA)) break;
  }
}

static Field* parse_field(Parser *p, Type t, char *name, SourcePos pos){
  Expr *init = NULL;
  if (match(p, TOK_ASSIGN)){
    init = parse_expression(p);
  }
  expect(p, TOK_SEMI);
  Field *f = (Field*)calloc(1,sizeof(Field));
  if (!f){ perr_tok(p, "out of memory"); return NULL; }
  f->type = t; f->name = name; f->init = init; f->pos = pos;
  return f;
}

static ClassDecl* parse_class(Parser *p){
  // "public" "class" Identifier "{" MemberList "}"
  if (!expect(p, TOK_KW_PUBLIC)) return NULL;
  expect(p, TOK_KW_CLASS);
  if (p->tok.kind != TOK_IDENT){ perr_expect(p, TOK_IDENT); return NULL; }
  char *cname = take_ident(p, &p->tok); SourcePos cpos=p->tok.pos; next(p);
  expect(p, TOK_LBRACE);

  ClassDecl *cls = (ClassDecl*)calloc(1,sizeof(ClassDecl));
  if (!cls){ perr_tok(p, "out of memory"); return NULL; }
  cls->name=cname; cls->pos=cpos; VecField_init(&cls->fields); VecMethod_init(&cls->methods);

  while (p->tok.kind != TOK_RBRACE && p->tok.kind != TOK_EOF){
    if (!match(p, TOK_KW_PUBLIC)){ perr_tok(p, "expected 'public'"); break; }
    if (!expect(p, TOK_KW_STATIC)) break;
    Type t = parse_type(p, true);
    if (p->tok.kind != TOK_IDENT){ perr_expect(p, TOK_IDENT); break; }
    char *name = take_ident(p, &p->tok); SourcePos pos = p->tok.pos; next(p);

    if (match(p, TOK_LPAREN)){
      // Method
      VecParam params; VecParam_init(&params);
      parse_param_list(p, &params); // reads until ')'
      Stmt *body = parse_block(p);  // method body is a block
      Method *m = (Method*)calloc(1,sizeof(Method));
      if (!m){ perr_tok(p, "out of memory"); return cls; }
      m->ret_type=t; m->name=name; m->params=params; m->body=body; m->pos=pos;
      if (!VecMethod_push(&cls->methods, m)){ perr_tok(p, "out of memory"); return cls; }
    } else {
      // Field
      Field *f = parse_field(p, t, name, pos);
      if (!f) return cls;
      if (!VecField_push(&cls->fields, f)){ perr_tok(p, "out of memory"); return cls; }
    }
  }
  expect(p, TOK_RBRACE);
  return cls;
}

static Program* parse_program(Parser *p){
  Program *prog = ast_program_new();
  if (!prog){ perr_tok(p, "out of memory"); return NULL; }
  ClassDecl *cls = parse_class(p);
  if (cls) {
    if (!VecClass_push(&prog->classes, cls)){
      perr_tok(p,"out of memory");
      ast_free_program(prog);
      return NULL;
    }
  }
  if (p->had_error){
    ast_free_program(prog);
    return NULL;
  }
  return prog;
}

// -------- Statements --------
static Stmt* parse_block(Parser *p){
  if (!expect(p, TOK_LBRACE)) return ast_new_block(p->tok.pos);
  SourcePos pos = p->tok.pos;
  Stmt *blk = ast_new_block(pos);
  while (p->tok.kind != TOK_RBRACE && p->tok.kind != TOK_EOF){
    Stmt *s = parse_statement(p);
    if (s) { if (!VecStmt_push(&blk->block.stmts, s)) { perr_tok(p, "out of memory"); break; } }
    else {
      while (p->tok.kind!=TOK_SEMI && p->tok.kind!=TOK_RBRACE && p->tok.kind!=TOK_EOF) next(p);
      if (p->tok.kind==TOK_SEMI) next(p);
    }
  }
  expect(p, TOK_RBRACE);
  return blk;
}

static Stmt* parse_if(Parser *p){
  SourcePos pos = p->tok.pos; expect(p, TOK_KW_IF);
  expect(p, TOK_LPAREN);
  Expr *cond = parse_expression(p);
  expect(p, TOK_RPAREN);
  Stmt *then_br = parse_statement(p);
  Stmt *else_br = NULL;
  if (match(p, TOK_KW_ELSE)) else_br = parse_statement(p);
  return ast_new_if(pos, cond, then_br, else_br);
}

static Stmt* parse_while(Parser *p){
  SourcePos pos = p->tok.pos; expect(p, TOK_KW_WHILE);
  expect(p, TOK_LPAREN);
  Expr *cond = parse_expression(p);
  expect(p, TOK_RPAREN);
  Stmt *body = parse_statement(p);
  return ast_new_while(pos, cond, body);
}

static Stmt* parse_for(Parser *p){
  SourcePos pos = p->tok.pos; expect(p, TOK_KW_FOR);
  expect(p, TOK_LPAREN);
  Expr *init = NULL, *cond = NULL, *post = NULL;
  if (!match(p, TOK_SEMI)){
    init = parse_expression(p);
    expect(p, TOK_SEMI);
  }
  if (!match(p, TOK_SEMI)){
    cond = parse_expression(p);
    expect(p, TOK_SEMI);
  }
  if (!match(p, TOK_RPAREN)){
    post = parse_expression(p);
    expect(p, TOK_RPAREN);
  }
  Stmt *body = parse_statement(p);
  return ast_new_for(pos, init, cond, post, body);
}

static Stmt* parse_return(Parser *p){
  SourcePos pos = p->tok.pos; expect(p, TOK_KW_RETURN);
  Expr *e = NULL;
  if (p->tok.kind != TOK_SEMI) e = parse_expression(p);
  expect(p, TOK_SEMI);
  return ast_new_return(pos, e);
}

static Stmt* parse_vardecl_stmt(Parser *p){
  Type t = parse_type(p, false);
  if (p->tok.kind != TOK_IDENT){ perr_expect(p, TOK_IDENT); return NULL; }
  char *name = take_ident(p, &p->tok); SourcePos pos=p->tok.pos; next(p);
  Expr *init = NULL;
  if (match(p, TOK_ASSIGN)) init = parse_expression(p);
  expect(p, TOK_SEMI);
  return ast_new_vardecl(pos, t, name, init);
}

static Stmt* parse_expr_stmt(Parser *p){
  SourcePos pos = p->tok.pos;
  Expr *e = parse_expression(p);
  expect(p, TOK_SEMI);
  return ast_new_exprstmt(pos, e);
}

static Stmt* parse_statement(Parser *p){
  switch (p->tok.kind){
    case TOK_LBRACE:    return parse_block(p);
    case TOK_KW_IF:     return parse_if(p);
    case TOK_KW_WHILE:  return parse_while(p);
    case TOK_KW_FOR:    return parse_for(p);
    case TOK_KW_RETURN: return parse_return(p);
    case TOK_KW_INT:
    case TOK_KW_LONG:
    case TOK_KW_BOOLEAN:
      return parse_vardecl_stmt(p);
    default:
      return parse_expr_stmt(p);
  }
}

// -------- Expressions --------
static Expr* parse_expression(Parser *p){ return parse_assignment(p); }

static Expr* parse_assignment(Parser *p){
  Expr *l = parse_or(p);
  if (match(p, TOK_ASSIGN)){
    SourcePos pos = p->tok.pos;
    Expr *r = parse_assignment(p);
    return ast_new_assign(pos, l, r);
  }
  return l;
}
static Expr* parse_or(Parser *p){
  Expr *e = parse_and(p);
  while (match(p, TOK_OROR)){
    SourcePos pos = p->tok.pos;
    Expr *r = parse_and(p);
    e = ast_new_bin(pos, BIN_OR, e, r);
  }
  return e;
}
static Expr* parse_and(Parser *p){
  Expr *e = parse_equality(p);
  while (match(p, TOK_ANDAND)){
    SourcePos pos = p->tok.pos;
    Expr *r = parse_equality(p);
    e = ast_new_bin(pos, BIN_AND, e, r);
  }
  return e;
}
static Expr* parse_equality(Parser *p){
  Expr *e = parse_rel(p);
  for (;;) {
    if (match(p, TOK_EQEQ)){ SourcePos pos=p->tok.pos; Expr* r=parse_rel(p); e=ast_new_bin(pos,BIN_EQ,e,r); }
    else if (match(p, TOK_NEQ)){ SourcePos pos=p->tok.pos; Expr* r=parse_rel(p); e=ast_new_bin(pos,BIN_NEQ,e,r); }
    else break;
  }
  return e;
}
static Expr* parse_rel(Parser *p){
  Expr *e = parse_add(p);
  for (;;) {
    if (match(p, TOK_LT)){ SourcePos pos=p->tok.pos; Expr* r=parse_add(p); e=ast_new_bin(pos,BIN_LT,e,r); }
    else if (match(p, TOK_LE)){ SourcePos pos=p->tok.pos; Expr* r=parse_add(p); e=ast_new_bin(pos,BIN_LE,e,r); }
    else if (match(p, TOK_GT)){ SourcePos pos=p->tok.pos; Expr* r=parse_add(p); e=ast_new_bin(pos,BIN_GT,e,r); }
    else if (match(p, TOK_GE)){ SourcePos pos=p->tok.pos; Expr* r=parse_add(p); e=ast_new_bin(pos,BIN_GE,e,r); }
    else break;
  }
  return e;
}
static Expr* parse_add(Parser *p){
  Expr *e = parse_mul(p);
  for (;;) {
    if (match(p, TOK_PLUS)){ SourcePos pos=p->tok.pos; Expr* r=parse_mul(p); e=ast_new_bin(pos,BIN_ADD,e,r); }
    else if (match(p, TOK_MINUS)){ SourcePos pos=p->tok.pos; Expr* r=parse_mul(p); e=ast_new_bin(pos,BIN_SUB,e,r); }
    else break;
  }
  return e;
}
static Expr* parse_mul(Parser *p){
  Expr *e = parse_unary(p);
  for (;;) {
    if (match(p, TOK_STAR)){ SourcePos pos=p->tok.pos; Expr* r=parse_unary(p); e=ast_new_bin(pos,BIN_MUL,e,r); }
    else if (match(p, TOK_SLASH)){ SourcePos pos=p->tok.pos; Expr* r=parse_unary(p); e=ast_new_bin(pos,BIN_DIV,e,r); }
    else if (match(p, TOK_PERCENT)){ SourcePos pos=p->tok.pos; Expr* r=parse_unary(p); e=ast_new_bin(pos,BIN_MOD,e,r); }
    else break;
  }
  return e;
}
static Expr* parse_unary(Parser *p){
  if (match(p, TOK_NOT)){   SourcePos pos=p->tok.pos; Expr *x=parse_unary(p); return ast_new_un(pos, UN_NOT, x); }
  if (match(p, TOK_MINUS)){ SourcePos pos=p->tok.pos; Expr *x=parse_unary(p); return ast_new_un(pos, UN_NEG, x); }
  return parse_primary(p);
}
static Expr* parse_primary(Parser *p){
  Token t = p->tok;
  switch (t.kind){
    case TOK_INT_LIT:{
      int64_t v = parse_int_literal(t.lexeme, t.length);
      next(p);
      return ast_new_int(t.pos, v);
    }
    case TOK_KW_TRUE:  next(p); return ast_new_bool(t.pos, true);
    case TOK_KW_FALSE: next(p); return ast_new_bool(t.pos, false);
    case TOK_IDENT:{
      char *name = ast_strdup_slice(t.lexeme, t.length);
      next(p);
      if (match(p, TOK_LPAREN)){
        Expr *call = ast_new_call(t.pos, name);
        parse_arg_list(p, call);
        return call;
      } else {
        return ast_new_var(t.pos, name);
      }
    }
    case TOK_LPAREN:{
      next(p);
      Expr *e = parse_expression(p);
      expect(p, TOK_RPAREN);
      return e;
    }
    default:
      perr_tok(p, "unexpected token in expression");
      next(p);
      return ast_new_int(t.pos, 0);
  }
}

// -------- Entry --------
Program *parse_java(const char *filename, const char *buffer, size_t len) {
  Parser p = {0};
  p.filename = filename ? filename : "";
  lexer_init(&p.lx, filename, buffer, len);
  next(&p);
  Program *prog = parse_program(&p);
  if (p.had_error){
    if (prog) ast_free_program(prog);
    return NULL;
  }
  return prog;
}
