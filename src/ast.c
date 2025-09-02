#include "ast.h"
#include <string.h>

// ---------- string helpers ----------
char *ast_strdup_slice(const char *p, size_t n) {
  char *s = (char*)malloc(n + 1);
  if (!s) return NULL;
  memcpy(s, p, n);
  s[n] = '\0';
  return s;
}
char *ast_strdup_cstr(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  return ast_strdup_slice(s, n);
}

// ---------- vec free helpers for node graphs ----------
static void free_expr(Expr *e);
static void free_stmt(Stmt *s);

Program *ast_program_new(void) {
  Program *p = (Program*)calloc(1, sizeof(Program));
  if (p) { VecClass_init(&p->classes); }
  return p;
}

static void free_field(Field *f) {
  if (!f) return;
  free(f->name);
  if (f->init) free_expr(f->init);
  free(f);
}
static void free_method(Method *m) {
  if (!m) return;
  free(m->name);
  for (size_t i=0;i<m->params.len;i++) free(m->params.items[i].name);
  VecParam_free(&m->params);
  if (m->body) free_stmt(m->body);
  free(m);
}
static void free_class(ClassDecl *c) {
  if (!c) return;
  free(c->name);
  for (size_t i=0;i<c->fields.len;i++) free_field(c->fields.items[i]);
  for (size_t i=0;i<c->methods.len;i++) free_method(c->methods.items[i]);
  VecField_free(&c->fields);
  VecMethod_free(&c->methods);
  free(c);
}

void ast_free_program(Program *p) {
  if (!p) return;
  for (size_t i=0;i<p->classes.len;i++) free_class(p->classes.items[i]);
  VecClass_free(&p->classes);
  free(p);
}

// ---------- factories ----------
Type ast_type(TypeKind k, SourcePos pos){ Type t; t.kind=k; t.pos=pos; return t; }

Expr *ast_new_int(SourcePos pos, int64_t v){
  Expr *e=(Expr*)calloc(1,sizeof(Expr)); if(!e) return NULL;
  e->kind=EX_INT_LIT; e->pos=pos; e->int_lit.value=v; return e;
}
Expr *ast_new_bool(SourcePos pos, bool v){
  Expr *e=(Expr*)calloc(1,sizeof(Expr)); if(!e) return NULL;
  e->kind=EX_BOOL_LIT; e->pos=pos; e->bool_lit.value=v; return e;
}
Expr *ast_new_var(SourcePos pos, char *name){
  Expr *e=(Expr*)calloc(1,sizeof(Expr)); if(!e) return NULL;
  e->kind=EX_VAR; e->pos=pos; e->var.name=name; return e;
}
Expr *ast_new_bin(SourcePos pos, BinOpKind op, Expr *l, Expr *r){
  Expr *e=(Expr*)calloc(1,sizeof(Expr)); if(!e) return NULL;
  e->kind=EX_BINOP; e->pos=pos; e->binop.op=op; e->binop.lhs=l; e->binop.rhs=r; return e;
}
Expr *ast_new_un(SourcePos pos, UnOpKind op, Expr *x){
  Expr *e=(Expr*)calloc(1,sizeof(Expr)); if(!e) return NULL;
  e->kind=EX_UNOP; e->pos=pos; e->unop.op=op; e->unop.expr=x; return e;
}
Expr *ast_new_assign(SourcePos pos, Expr *l, Expr *r){
  Expr *e=(Expr*)calloc(1,sizeof(Expr)); if(!e) return NULL;
  e->kind=EX_ASSIGN; e->pos=pos; e->assign.lhs=l; e->assign.rhs=r; return e;
}
Expr *ast_new_call(SourcePos pos, char *name){
  Expr *e=(Expr*)calloc(1,sizeof(Expr)); if(!e) return NULL;
  e->kind=EX_CALL; e->pos=pos; e->call.name=name; VecExpr_init(&e->call.args); return e;
}

Stmt *ast_new_block(SourcePos pos){
  Stmt *s=(Stmt*)calloc(1,sizeof(Stmt)); if(!s) return NULL;
  s->kind=ST_BLOCK; s->pos=pos; VecStmt_init(&s->block.stmts); return s;
}
Stmt *ast_new_if(SourcePos pos, Expr *cond, Stmt *then_br, Stmt *else_br){
  Stmt *s=(Stmt*)calloc(1,sizeof(Stmt)); if(!s) return NULL;
  s->kind=ST_IF; s->pos=pos; s->if_s.cond=cond; s->if_s.then_br=then_br; s->if_s.else_br=else_br; return s;
}
Stmt *ast_new_while(SourcePos pos, Expr *cond, Stmt *body){
  Stmt *s=(Stmt*)calloc(1,sizeof(Stmt)); if(!s) return NULL;
  s->kind=ST_WHILE; s->pos=pos; s->while_s.cond=cond; s->while_s.body=body; return s;
}
Stmt *ast_new_for(SourcePos pos, Expr *init, Expr *cond, Expr *post, Stmt *body){
  Stmt *s=(Stmt*)calloc(1,sizeof(Stmt)); if(!s) return NULL;
  s->kind=ST_FOR; s->pos=pos; s->for_s.init=init; s->for_s.cond=cond; s->for_s.post=post; s->for_s.body=body; return s;
}
Stmt *ast_new_return(SourcePos pos, Expr *expr){
  Stmt *s=(Stmt*)calloc(1,sizeof(Stmt)); if(!s) return NULL;
  s->kind=ST_RETURN; s->pos=pos; s->ret_s.expr=expr; return s;
}
Stmt *ast_new_vardecl(SourcePos pos, Type t, char *name, Expr *init){
  Stmt *s=(Stmt*)calloc(1,sizeof(Stmt)); if(!s) return NULL;
  s->kind=ST_VARDECL; s->pos=pos; s->vardecl.type=t; s->vardecl.name=name; s->vardecl.init=init; return s;
}
Stmt *ast_new_exprstmt(SourcePos pos, Expr *expr){
  Stmt *s=(Stmt*)calloc(1,sizeof(Stmt)); if(!s) return NULL;
  s->kind=ST_EXPR; s->pos=pos; s->expr_s.expr=expr; return s;
}

// ---------- deep free ----------
static void free_expr(Expr *e){
  if (!e) return;
  switch (e->kind){
    case EX_VAR: free(e->var.name); break;
    case EX_BINOP: free_expr(e->binop.lhs); free_expr(e->binop.rhs); break;
    case EX_UNOP: free_expr(e->unop.expr); break;
    case EX_ASSIGN: free_expr(e->assign.lhs); free_expr(e->assign.rhs); break;
    case EX_CALL:
      for (size_t i=0;i<e->call.args.len;i++) free_expr(e->call.args.items[i]);
      VecExpr_free(&e->call.args);
      free(e->call.name);
      break;
    default: break; // literals have no heap members
  }
  free(e);
}

static void free_stmt(Stmt *s){
  if (!s) return;
  switch (s->kind){
    case ST_BLOCK:
      for (size_t i=0;i<s->block.stmts.len;i++) free_stmt(s->block.stmts.items[i]);
      VecStmt_free(&s->block.stmts);
      break;
    case ST_IF:
      free_expr(s->if_s.cond);
      free_stmt(s->if_s.then_br);
      free_stmt(s->if_s.else_br);
      break;
    case ST_WHILE:
      free_expr(s->while_s.cond);
      free_stmt(s->while_s.body);
      break;
    case ST_FOR:
      free_expr(s->for_s.init);
      free_expr(s->for_s.cond);
      free_expr(s->for_s.post);
      free_stmt(s->for_s.body);
      break;
    case ST_RETURN:
      free_expr(s->ret_s.expr);
      break;
    case ST_VARDECL:
      free(s->vardecl.name);
      free_expr(s->vardecl.init);
      break;
    case ST_EXPR:
      free_expr(s->expr_s.expr);
      break;
  }
  free(s);
}
