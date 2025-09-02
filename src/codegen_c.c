#include "codegen_c.h"
#include "ast.h"
#include <stdio.h>
#include <string.h>

// ---------- emit helpers ----------
typedef struct {
  FILE *f;
  int indent; // in levels (2 spaces each)
} Em;

static void em_init(Em *e, FILE *f){ e->f=f; e->indent=0; }
static void em_nl(Em *e){ fputc('\n', e->f); }
static void em_ind(Em *e){ for (int i=0;i<e->indent;i++){ fputs("  ", e->f); } }
static void em_inc(Em *e){ e->indent++; }
static void em_dec(Em *e){ if (e->indent>0) e->indent--; }

static const char* c_type(TypeKind k){
  switch (k){
    case TY_INT:     return "int32_t";
    case TY_LONG:    return "int64_t";
    case TY_BOOLEAN: return "bool";
    case TY_VOID:    return "void";
  }
  return "int32_t";
}

// forward
static void emit_stmt(Em *e, Stmt *s);
static void emit_expr_inner(Em *e, Expr *x, bool top_assign);

// ---------- expressions ----------
static const char* binop_str(BinOpKind k){
  switch (k){
    case BIN_ADD: return "+";  case BIN_SUB: return "-";  case BIN_MUL: return "*";
    case BIN_DIV: return "/";  case BIN_MOD: return "%";
    case BIN_EQ:  return "=="; case BIN_NEQ: return "!=";
    case BIN_LT:  return "<";  case BIN_LE:  return "<=";
    case BIN_GT:  return ">";  case BIN_GE:  return ">=";
    case BIN_AND: return "&&"; case BIN_OR:  return "||";
  }
  return "?";
}

static void emit_expr_inner(Em *e, Expr *x, bool top_assign){
  if (!x){ fputs("0", e->f); return; }
  switch (x->kind){
    case EX_INT_LIT:
      fprintf(e->f, "%lld", (long long)x->int_lit.value);
      return;
    case EX_BOOL_LIT:
      fputs(x->bool_lit.value ? "true" : "false", e->f);
      return;
    case EX_VAR:
      fputs(x->var.name ? x->var.name : "_", e->f);
      return;
    case EX_UNOP:
      fputc('(', e->f);
      fputs(x->unop.op==UN_NOT ? "!" : "-", e->f);
      emit_expr_inner(e, x->unop.expr, false);
      fputc(')', e->f);
      return;
    case EX_BINOP:
      fputc('(', e->f);
      emit_expr_inner(e, x->binop.lhs, false);
      fputc(' ', e->f);
      fputs(binop_str(x->binop.op), e->f);
      fputc(' ', e->f);
      emit_expr_inner(e, x->binop.rhs, false);
      fputc(')', e->f);
      return;
    case EX_ASSIGN:
      if (!top_assign) fputc('(', e->f);
      emit_expr_inner(e, x->assign.lhs, false);
      fputs(" = ", e->f);
      emit_expr_inner(e, x->assign.rhs, false);
      if (!top_assign) fputc(')', e->f);
      return;
    case EX_CALL: {
      fputs(x->call.name ? x->call.name : "_", e->f);
      fputc('(', e->f);
      for (size_t i=0;i<x->call.args.len;i++){
        if (i) fputs(", ", e->f);
        emit_expr_inner(e, x->call.args.items[i], false);
      }
      fputc(')', e->f);
      return;
    }
  }
}

// convenience wrappers
static void emit_expr(Em *e, Expr *x){ emit_expr_inner(e, x, false); }
static void emit_assign_like(Em *e, Expr *x){ emit_expr_inner(e, x, true); }

// ---------- statements ----------
static void emit_block(Em *e, Stmt *blk){
  fputs("{", e->f); em_nl(e); em_inc(e);
  for (size_t i=0;i<blk->block.stmts.len;i++){
    emit_stmt(e, blk->block.stmts.items[i]);
  }
  em_dec(e); em_ind(e); fputs("}", e->f); em_nl(e);
}

static void emit_stmt(Em *e, Stmt *s){
  if (!s){ em_ind(e); fputs("/* <null> */", e->f); em_nl(e); return; }
  switch (s->kind){
    case ST_BLOCK:
      em_ind(e); emit_block(e, s);
      return;
    case ST_IF:
      em_ind(e); fputs("if (", e->f); emit_expr(e, s->if_s.cond); fputs(") ", e->f);
      // always brace bodies for determinism
      if (s->if_s.then_br && s->if_s.then_br->kind==ST_BLOCK) {
        emit_block(e, s->if_s.then_br);
      } else {
        fputs("{", e->f); em_nl(e); em_inc(e);
        emit_stmt(e, s->if_s.then_br);
        em_dec(e); em_ind(e); fputs("}", e->f); em_nl(e);
      }
      if (s->if_s.else_br){
        em_ind(e); fputs("else ", e->f);
        if (s->if_s.else_br->kind==ST_BLOCK) {
          emit_block(e, s->if_s.else_br);
        } else {
          fputs("{", e->f); em_nl(e); em_inc(e);
          emit_stmt(e, s->if_s.else_br);
          em_dec(e); em_ind(e); fputs("}", e->f); em_nl(e);
        }
      }
      return;
    case ST_WHILE:
      em_ind(e); fputs("while (", e->f); emit_expr(e, s->while_s.cond); fputs(") ", e->f);
      if (s->while_s.body && s->while_s.body->kind==ST_BLOCK) {
        emit_block(e, s->while_s.body);
      } else {
        fputs("{", e->f); em_nl(e); em_inc(e);
        emit_stmt(e, s->while_s.body);
        em_dec(e); em_ind(e); fputs("}", e->f); em_nl(e);
      }
      return;
    case ST_FOR: {
      em_ind(e); fputs("for (", e->f);
      if (s->for_s.init){ emit_assign_like(e, s->for_s.init); }
      fputs(";", e->f);
      if (s->for_s.cond){ fputc(' ', e->f); emit_expr(e, s->for_s.cond); }
      fputs(";", e->f);
      if (s->for_s.post){ fputc(' ', e->f); emit_assign_like(e, s->for_s.post); }
      fputs(") ", e->f);
      if (s->for_s.body && s->for_s.body->kind==ST_BLOCK) {
        emit_block(e, s->for_s.body);
      } else {
        fputs("{", e->f); em_nl(e); em_inc(e);
        emit_stmt(e, s->for_s.body);
        em_dec(e); em_ind(e); fputs("}", e->f); em_nl(e);
      }
      return;
    }
    case ST_RETURN:
      em_ind(e); fputs("return", e->f);
      if (s->ret_s.expr){ fputc(' ', e->f); emit_expr(e, s->ret_s.expr); }
      fputs(";", e->f); em_nl(e);
      return;
    case ST_VARDECL:
      em_ind(e); fprintf(e->f, "%s %s", c_type(s->vardecl.type.kind), s->vardecl.name);
      if (s->vardecl.init){ fputs(" = ", e->f); emit_expr(e, s->vardecl.init); }
      fputs(";", e->f); em_nl(e);
      return;
    case ST_EXPR:
      em_ind(e); emit_assign_like(e, s->expr_s.expr); fputs(";", e->f); em_nl(e);
      return;
  }
}

// ---------- top-level ----------
static void emit_field(Em *e, Field *f){
  // public static fields -> file-scope variables
  em_ind(e);
  fprintf(e->f, "%s %s", c_type(f->type.kind), f->name);
  if (f->init){
    fputs(" = ", e->f);
    emit_expr(e, f->init);
  }
  fputs(";", e->f); em_nl(e);
}

static void emit_method(Em *e, Method *m){
  // Signature
  em_ind(e);
  fprintf(e->f, "%s %s(", c_type(m->ret_type.kind), m->name);
  for (size_t i=0;i<m->params.len;i++){
    if (i) fputs(", ", e->f);
    fprintf(e->f, "%s %s", c_type(m->params.items[i].type.kind), m->params.items[i].name);
  }
  fputs(") ", e->f);
  // Body (block)
  if (m->body && m->body->kind==ST_BLOCK){
    emit_block(e, m->body);
  } else {
    // Shouldn't happen in MVP, but be robust
    fputs("{", e->f); em_nl(e); em_inc(e);
    if (m->body) emit_stmt(e, m->body);
    em_dec(e); em_ind(e); fputs("}", e->f); em_nl(e);
  }
  em_nl(e);
}

bool codegen_c(Program *prog, const char *out_path, const char *top_class_name){
  (void)top_class_name; // MVP: flat function names, no prefix
  FILE *f = fopen(out_path, "wb");
  if (!f) return false;
  Em em; em_init(&em, f);

  // headers
  fputs("#include <stdint.h>\n", f);
  fputs("#include <stdbool.h>\n", f);
  fputs("\n", f);

  // We expect exactly one public class in MVP
  if (prog->classes.len == 0){
    fputs("/* empty program */\n", f);
    fclose(f);
    return true;
  }
  ClassDecl *c = prog->classes.items[0];

  // fields (file-scope)
  for (size_t i=0;i<c->fields.len;i++){
    emit_field(&em, c->fields.items[i]);
  }
  if (c->fields.len) em_nl(&em);

  // methods
  for (size_t i=0;i<c->methods.len;i++){
    emit_method(&em, c->methods.items[i]);
  }

  fclose(f);
  return true;
}
