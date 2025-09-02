#include "codegen_vhdl.h"
#include "ast.h"
#include "vhdl_pragmas.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/*
 * VHDL Codegen (MVP) + Pragmas
 * - Entity/Architecture je Methode, standardmäßig kombinational.
 * - Typen: int32→signed(31 downto 0), int64→signed(63 downto 0), bool→std_logic.
 * - if/else, return, Zuweisungen, VarDecl.
 * - for (konstante Grenzen, Schritt ±1) → native VHDL for-loop.
 * - while (Muster: Literal-Init im selben Block; Bedingung i ? C; Body endet mit i=i±1) → for-Loop;
 *   letztes i=i±1 wird im Body unterdrückt.
 * - Division/Modulo im Vektor-Kontext: to_signed(to_integer(L) op to_integer(R), WIDTH).
 * - Peepholes:
 *     (1) 1 % X → to_signed(1, WIDTH)
 *     (2) Unmittelbar nach "v := ... mod ...;" folgendes "if (v < 0) v := v + ...;" wird entfernt.
 *
 * Pragmas (aus vhdl_pragmas.[ch]):
 *   - entity-name, output-name
 *   - vhdl-dialect: v93 | v2008
 *   - sensitivity-list ODER sensitivity_list (Alias akzeptiert)
 * Bibliotheken sind immer:
 *   library ieee;
 *   use ieee.std_logic_1164.all;
 *   use ieee.numeric_std.all;
 */

/* ------------------------------------------------------------------------- */
/* Globale Hints (per main.c gesetzt)                                        */
static VhdlHints g_hints;
static bool g_have_hints = false;

void codegen_vhdl_set_hints(const VhdlHints *h) {
  if (h) { g_hints = *h; g_have_hints = true; }
  else { memset(&g_hints, 0, sizeof g_hints); g_have_hints = false; }
}

/* ------------------------------------------------------------------------- */
/* Emit-Helfer                                                                */
typedef struct { FILE *f; int indent; } Em;
static void em_init(Em *e, FILE *f){ e->f=f; e->indent=0; }
static void em_nl(Em *e){ fputc('\n', e->f); }
static void em_ind(Em *e){ for (int i=0;i<e->indent;i++) fputs("  ", e->f); }
static void em_inc(Em *e){ e->indent++; }
static void em_dec(Em *e){ if (e->indent>0) e->indent--; }

/* ------------------------------------------------------------------------- */
/* Kleine lokale Vektoren (Makro nicht mit ast.h kollidieren lassen)         */
#define VEC_LOCAL_DECL(T, Name) \
  typedef struct { T *items; size_t len; size_t cap; } Name; \
  static void Name##_init(Name *v){ v->items=NULL; v->len=v->cap=0; } \
  static void Name##_free(Name *v){ free(v->items); v->items=NULL; v->len=v->cap=0; } \
  static bool Name##_push(Name *v, T x){ \
    if (v->len==v->cap){ size_t n=v->cap? v->cap*2:4; T* p=(T*)realloc(v->items, n*sizeof(T)); if(!p) return false; v->items=p; v->cap=n; } \
    v->items[v->len++]=x; return true; \
  }

/* ------------------------------------------------------------------------- */
/* Symboltabelle                                                              */
typedef struct { char *name; TypeKind kind; bool is_param; } Sym;
VEC_LOCAL_DECL(Sym,   VecSym)
VEC_LOCAL_DECL(char*, VecStr)

static int width_of(TypeKind k){
  switch(k){
    case TY_INT:     return 32;
    case TY_LONG:    return 64;
    case TY_BOOLEAN: return 1;
    default:         return 0;
  }
}
static const char* vhdl_type(TypeKind k){
  switch (k){
    case TY_INT:     return "signed(31 downto 0)";
    case TY_LONG:    return "signed(63 downto 0)";
    case TY_BOOLEAN: return "std_logic";
    case TY_VOID:    return "void /*unsupported*/";
  }
  return "signed(31 downto 0)";
}
static const char* vhdl_binop(BinOpKind k){
  switch (k){
    case BIN_ADD: return "+";
    case BIN_SUB: return "-";
    case BIN_MUL: return "*";
    case BIN_DIV: return "/";
    case BIN_MOD: return "mod";
    case BIN_EQ:  return "=";
    case BIN_NEQ: return "/=";
    case BIN_LT:  return "<";
    case BIN_LE:  return "<=";
    case BIN_GT:  return ">";
    case BIN_GE:  return ">=";
    case BIN_AND: return "and";
    case BIN_OR:  return "or";
  }
  return "?";
}
static TypeKind sym_lookup(VecSym *sy, const char *name, bool *is_param_out){
  for (size_t i=0;i<sy->len;i++){
    if (sy->items[i].name && name && strcmp(sy->items[i].name,name)==0){
      if (is_param_out) *is_param_out = sy->items[i].is_param;
      return sy->items[i].kind;
    }
  }
  if (is_param_out) *is_param_out = false;
  return TY_INT;
}

/* ------------------------------------------------------------------------- */
/* Analyse-Helfer                                                             */
static bool expr_uses_var(Expr *e, const char *name){
  if (!e || !name) return false;
  switch (e->kind){
    case EX_VAR:    return e->var.name && strcmp(e->var.name, name)==0;
    case EX_UNOP:   return expr_uses_var(e->unop.expr, name);
    case EX_BINOP:  return expr_uses_var(e->binop.lhs, name) || expr_uses_var(e->binop.rhs, name);
    case EX_ASSIGN: return expr_uses_var(e->assign.lhs, name) || expr_uses_var(e->assign.rhs, name);
    case EX_CALL:   return true; // konservativ
    default:        return false;
  }
}
static bool stmt_uses_var(Stmt *s, const char *name){
  if (!s) return false;
  switch (s->kind){
    case ST_BLOCK:
      for (size_t i=0;i<s->block.stmts.len;i++)
        if (stmt_uses_var(s->block.stmts.items[i], name)) return true;
      return false;
    case ST_IF:
      return expr_uses_var(s->if_s.cond, name)
          || stmt_uses_var(s->if_s.then_br, name)
          || stmt_uses_var(s->if_s.else_br, name);
    case ST_WHILE:
      return expr_uses_var(s->while_s.cond, name) || stmt_uses_var(s->while_s.body, name);
    case ST_FOR:
      return stmt_uses_var(s->for_s.body, name);
    case ST_RETURN:
      return expr_uses_var(s->ret_s.expr, name);
    case ST_VARDECL:
      return expr_uses_var(s->vardecl.init, name);
    case ST_EXPR:
      return expr_uses_var(s->expr_s.expr, name);
  }
  return false;
}
static bool body_assigns_var_anywhere(Stmt *s, const char *name){
  if (!s) return false;
  switch (s->kind){
    case ST_BLOCK:
      for (size_t i=0;i<s->block.stmts.len;i++)
        if (body_assigns_var_anywhere(s->block.stmts.items[i], name)) return true;
      return false;
    case ST_IF:
      if (body_assigns_var_anywhere(s->if_s.then_br, name)) return true;
      if (body_assigns_var_anywhere(s->if_s.else_br, name)) return true;
      return false;
    case ST_WHILE:
    case ST_FOR:
      return true; // konservativ
    case ST_RETURN: return false;
    case ST_VARDECL:
      return (s->vardecl.name && name && strcmp(s->vardecl.name,name)==0);
    case ST_EXPR:
      if (!s->expr_s.expr) return false;
      if (s->expr_s.expr->kind==EX_ASSIGN && s->expr_s.expr->assign.lhs && s->expr_s.expr->assign.lhs->kind==EX_VAR)
        return strcmp(s->expr_s.expr->assign.lhs->var.name, name)==0;
      return false;
  }
  return false;
}

/* For-Shape / Vergleich-Normalisierung */
typedef struct { long long start; long long bound; int step; int cmp; } ForShape; // cmp:0<,1<=,2>,3>=
static int cmp_from_binop(BinOpKind op){
  switch (op){ case BIN_LT: return 0; case BIN_LE: return 1; case BIN_GT: return 2; case BIN_GE: return 3; default: return -1; }
}

/* Schritt-Erkennung: i=i±1 oder 1+i */
static bool is_post_step_stmt(Stmt *s, const char *vn, int *step_out){
  if (!s || s->kind!=ST_EXPR || !s->expr_s.expr) return false;
  Expr *e = s->expr_s.expr;
  if (e->kind!=EX_ASSIGN) return false;
  if (!e->assign.lhs || e->assign.lhs->kind!=EX_VAR) return false;
  if (strcmp(e->assign.lhs->var.name, vn)!=0) return false;
  if (!e->assign.rhs || e->assign.rhs->kind!=EX_BINOP) return false;

  Expr *L = e->assign.rhs->binop.lhs;
  Expr *R = e->assign.rhs->binop.rhs;
  BinOpKind op = e->assign.rhs->binop.op;

  if (L && L->kind==EX_VAR && strcmp(L->var.name, vn)==0 &&
      R && R->kind==EX_INT_LIT && R->int_lit.value==1){
    if (op==BIN_ADD){ if (step_out) *step_out = +1; return true; }
    if (op==BIN_SUB){ if (step_out) *step_out = -1; return true; }
    return false;
  }
  if (L && L->kind==EX_INT_LIT && L->int_lit.value==1 &&
      R && R->kind==EX_VAR && strcmp(R->var.name, vn)==0){
    if (op==BIN_ADD){ if (step_out) *step_out = +1; return true; }
    return false;
  }
  return false;
}

/* i ? C bzw. C ? i → Normalform "i ? C" */
static bool normalize_var_cmp(Expr *cond, const char **vn_out, long long *bound_out, int *cmp_out){
  if (!cond || cond->kind!=EX_BINOP) return false;
  Expr *L = cond->binop.lhs;
  Expr *R = cond->binop.rhs;
  BinOpKind op = cond->binop.op;

  if (L && L->kind==EX_VAR && R && R->kind==EX_INT_LIT){
    if (vn_out) *vn_out = L->var.name;
    if (bound_out) *bound_out = R->int_lit.value;
    switch (op){ case BIN_LT:*cmp_out=0;return true; case BIN_LE:*cmp_out=1;return true;
                 case BIN_GT:*cmp_out=2;return true; case BIN_GE:*cmp_out=3;return true; default: return false; }
  }
  if (L && L->kind==EX_INT_LIT && R && R->kind==EX_VAR){
    if (vn_out) *vn_out = R->var.name;
    if (bound_out) *bound_out = L->int_lit.value;
    switch (op){
      case BIN_LT: *cmp_out = 2; return true; // C < i  → i > C
      case BIN_LE: *cmp_out = 3; return true; // C <= i → i >= C
      case BIN_GT: *cmp_out = 0; return true; // C > i  → i < C
      case BIN_GE: *cmp_out = 1; return true; // C >= i → i <= C
      default: return false;
    }
  }
  return false;
}

/* Body darf i außer finalem Schritt nicht ändern */
static bool body_assigns_var_except_trailing_step(Stmt *body, const char *vn){
  if (!body) return true;

  if (body->kind==ST_BLOCK){
    size_t n = body->block.stmts.len;
    for (size_t i=0;i<n;i++){
      Stmt *s = body->block.stmts.items[i];
      if (i==n-1){ int st=0; if (is_post_step_stmt(s, vn, &st)) continue; }
      if (body_assigns_var_anywhere(s, vn)) return true;
    }
    return false;
  }

  int st=0;
  if (is_post_step_stmt(body, vn, &st)) return false;
  return body_assigns_var_anywhere(body, vn);
}

/* ------------------------------------------------------------------------- */
/* Ausdruck-Emission                                                          */
static bool emit_stmt(Em *e, Stmt *s, VecSym *sy, TypeKind ret_kind); // fwd
static void emit_expr(Em *e, Expr *x, VecSym *sy, int width_hint, bool bool_ctx);

static void emit_divmod_vec(Em *e, BinOpKind op, Expr *l, Expr *r, VecSym *sy, int width_hint){
  int w = (width_hint==64) ? 64 : 32;

  if (op==BIN_MOD && l && l->kind==EX_INT_LIT && l->int_lit.value==1){
    fprintf(e->f, "to_signed(1, %d)", w);
    return;
  }

  fputs("to_signed(to_integer(", e->f);
  emit_expr(e, l, sy, 0, false);
  fputs(") ", e->f);
  fputs(op==BIN_DIV ? "/" : "mod", e->f);
  fputs(" to_integer(", e->f);
  emit_expr(e, r, sy, 0, false);
  fprintf(e->f, "), %d)", w);
}

static void emit_int_lit(Em *e, long long v, int width_hint){
  if (width_hint==32 || width_hint==64) fprintf(e->f, "to_signed(%lld, %d)", v, width_hint);
  else fprintf(e->f, "to_signed(%lld, 32)", v);
}
static void emit_var_ref(Em *e, const char *name){ fputs(name?name:"_", e->f); }
static void emit_unop(Em *e, UnOpKind op, Expr *sub, VecSym *sy, int width_hint){
  if (op==UN_NOT){ fputs("(not ", e->f); emit_expr(e, sub, sy, 0, true); fputc(')', e->f); }
  else { fputc('(', e->f); fputc('-', e->f); emit_expr(e, sub, sy, width_hint, false); fputc(')', e->f); }
}
static void emit_binop(Em *e, BinOpKind op, Expr *l, Expr *r, VecSym *sy, int width_hint){
  if (op==BIN_DIV || op==BIN_MOD){
    fputc('(', e->f);
    emit_divmod_vec(e, op, l, r, sy, width_hint);
    fputc(')', e->f);
    return;
  }
  fputc('(', e->f);
  emit_expr(e, l, sy, width_hint, (op==BIN_AND||op==BIN_OR));
  fprintf(e->f, " %s ", vhdl_binop(op));
  emit_expr(e, r, sy, width_hint, (op==BIN_AND||op==BIN_OR));
  fputc(')', e->f);
}
static void emit_assign_expr(Em *e, Expr *lhs, Expr *rhs, VecSym *sy){
  if (!lhs || lhs->kind!=EX_VAR){ fputs("-- unsupported LHS in assignment", e->f); return; }
  bool is_param=false; TypeKind k = sym_lookup(sy, lhs->var.name, &is_param);
  if (is_param){ fputs("-- cannot assign to input port", e->f); return; }
  int w = width_of(k);
  emit_var_ref(e, lhs->var.name); fputs(" := ", e->f); emit_expr(e, rhs, sy, w, false);
}
static void emit_expr(Em *e, Expr *x, VecSym *sy, int width_hint, bool bool_ctx){
  (void)bool_ctx;
  if (!x){ fputs("(others => '0')", e->f); return; }
  switch (x->kind){
    case EX_INT_LIT:  emit_int_lit(e, (long long)x->int_lit.value, width_hint); return;
    case EX_BOOL_LIT: fputs(x->bool_lit.value ? "'1'" : "'0'", e->f); return;
    case EX_VAR:      emit_var_ref(e, x->var.name); return;
    case EX_UNOP:     emit_unop(e, x->unop.op, x->unop.expr, sy, width_hint); return;
    case EX_BINOP:    emit_binop(e, x->binop.op, x->binop.lhs, x->binop.rhs, sy, width_hint); return;
    case EX_ASSIGN:   fputc('(', e->f); emit_assign_expr(e, x->assign.lhs, x->assign.rhs, sy); fputc(')', e->f); return;
    case EX_CALL:     fputs("/* call_unsupported */ (others => '0')", e->f); return;
  }
}

/* ------------------------------------------------------------------------- */
/* For-Analyse aus for-Header                                                */
static bool build_shape_from_for_header(Stmt *s, const char **vn_out, ForShape *fs){
  if (!s || s->kind!=ST_FOR) return false;
  Expr *init = s->for_s.init, *cond=s->for_s.cond, *post=s->for_s.post;
  if (!init || !cond || !post || init->kind!=EX_ASSIGN || !init->assign.lhs || init->assign.lhs->kind!=EX_VAR) return false;
  const char *vn = init->assign.lhs->var.name;
  if (!(cond->kind==EX_BINOP && cond->binop.lhs && cond->binop.lhs->kind==EX_VAR &&
        strcmp(cond->binop.lhs->var.name, vn)==0 && cond->binop.rhs && cond->binop.rhs->kind==EX_INT_LIT)) return false;
  int cmp = cmp_from_binop(cond->binop.op); if (cmp<0) return false;
  if (!init->assign.rhs || init->assign.rhs->kind!=EX_INT_LIT) return false;

  int step=0;
  if (post->kind==EX_ASSIGN && post->assign.lhs && post->assign.lhs->kind==EX_VAR && strcmp(post->assign.lhs->var.name, vn)==0 &&
      post->assign.rhs && post->assign.rhs->kind==EX_BINOP && post->assign.rhs->binop.lhs && post->assign.rhs->binop.lhs->kind==EX_VAR &&
      strcmp(post->assign.rhs->binop.lhs->var.name, vn)==0 && post->assign.rhs->binop.rhs && post->assign.rhs->binop.rhs->kind==EX_INT_LIT &&
      post->assign.rhs->binop.rhs->int_lit.value==1){
    step = (post->assign.rhs->binop.op==BIN_ADD)? +1 : (post->assign.rhs->binop.op==BIN_SUB)? -1 : 0;
  }
  if (step==0) return false;
  if ((step==+1 && !(cmp==0||cmp==1)) || (step==-1 && !(cmp==2||cmp==3))) return false;

  fs->start = init->assign.rhs->int_lit.value;
  fs->bound = cond->binop.rhs->int_lit.value;
  fs->step  = step;
  fs->cmp   = cmp;
  if (vn_out) *vn_out = vn;
  return true;
}

/* ------------------------------------------------------------------------- */
/* while-Unterstützung: Init-Map                                             */
typedef struct { char *name; Stmt *stmt; } NameInit;
VEC_LOCAL_DECL(NameInit, VecInit)

static Stmt* initmap_get(VecInit *m, const char *name){
  if (!m || !name) return NULL;
  for (size_t i=0;i<m->len;i++)
    if (m->items[i].name && strcmp(m->items[i].name, name)==0) return m->items[i].stmt;
  return NULL;
}
static void initmap_set(VecInit *m, const char *name, Stmt *st){
  if (!m || !name) return;
  for (size_t i=0;i<m->len;i++){
    if (m->items[i].name && strcmp(m->items[i].name, name)==0){ m->items[i].stmt = st; return; }
  }
  NameInit ni; ni.name=(char*)name; ni.stmt=st; VecInit_push(m, ni);
}
static void initmap_clear(VecInit *m, const char *name){
  if (!m || !name) return;
  for (size_t i=0;i<m->len;i++){
    if (m->items[i].name && strcmp(m->items[i].name, name)==0){ m->items[i].stmt = NULL; return; }
  }
}

/* Literal-Init für vn? (VarDecl/Assign) */
static bool stmt_is_literal_init_for(Stmt *st, const char *vn, long long *lit_out){
  if (!st || !vn) return false;
  if (st->kind==ST_VARDECL){
    if (st->vardecl.name && strcmp(st->vardecl.name, vn)==0 &&
        st->vardecl.init && st->vardecl.init->kind==EX_INT_LIT){
      if (lit_out) *lit_out = st->vardecl.init->int_lit.value;
      return true;
    }
    return false;
  }
  if (st->kind==ST_EXPR && st->expr_s.expr && st->expr_s.expr->kind==EX_ASSIGN){
    Expr *as = st->expr_s.expr;
    if (as->assign.lhs && as->assign.lhs->kind==EX_VAR &&
        strcmp(as->assign.lhs->var.name, vn)==0 &&
        as->assign.rhs && as->assign.rhs->kind==EX_INT_LIT){
      if (lit_out) *lit_out = as->assign.rhs->int_lit.value;
      return true;
    }
  }
  return false;
}

/* while-Muster → ForShape */
static bool analyze_while_with_init(Stmt *wh, Stmt *init_stmt, ForShape *fs, char **varname_out){
  if (!wh || wh->kind!=ST_WHILE) return false;
  Expr *cond = wh->while_s.cond;
  Stmt *body = wh->while_s.body;
  const char *vn = NULL;
  long long bound = 0;
  int cmp = -1;

  if (!normalize_var_cmp(cond, &vn, &bound, &cmp)) return false;

  if (!body) return false;
  int step=0;
  if (body->kind==ST_BLOCK){
    if (body->block.stmts.len==0) return false;
    if (!is_post_step_stmt(body->block.stmts.items[body->block.stmts.len-1], vn, &step)) return false;
  } else {
    if (!is_post_step_stmt(body, vn, &step)) return false;
  }
  if (body_assigns_var_except_trailing_step(body, vn)) return false;

  long long start=0;
  if (!stmt_is_literal_init_for(init_stmt, vn, &start)) return false;

  if (step==+1 && !(cmp==0||cmp==1)) return false;
  if (step==-1 && !(cmp==2||cmp==3)) return false;

  fs->start = start;
  fs->bound = bound;
  fs->step  = step;
  fs->cmp   = cmp;
  if (varname_out) *varname_out = (char*)vn;
  return true;
}

/* Body ohne letztes i=i±1 emittieren */
static bool emit_body_without_trailing_step(Em *e, Stmt *body, VecSym *sy, TypeKind ret_kind, const char *vn){
  if (!body) return true;

  if (body->kind==ST_BLOCK){
    size_t n = body->block.stmts.len;
    if (n==0) return true;
    for (size_t i=0;i+1<n;i++){
      if (!emit_stmt(e, body->block.stmts.items[i], sy, ret_kind)) return false;
    }
    int st=0;
    if (!is_post_step_stmt(body->block.stmts.items[n-1], vn, &st))
      return emit_stmt(e, body->block.stmts.items[n-1], sy, ret_kind);
    return true;
  }

  int st=0;
  if (is_post_step_stmt(body, vn, &st)) return true;
  return emit_stmt(e, body, sy, ret_kind);
}

/* for-Kopf für ForShape emittieren; optional i := __i */
static bool emit_for_loop_shape(Em *e, const char *vn, const ForShape *fs, bool need_i_assign, VecSym *sy){
  long long start = fs->start;
  long long end_incl = (fs->step == 1) ? ((fs->cmp == 0) ? (fs->bound - 1) : (fs->bound))
                                       : ((fs->cmp == 2) ? (fs->bound + 1) : (fs->bound));

  bool dummy=false; int w = width_of(sym_lookup(sy, vn, &dummy));
  if (need_i_assign && (w!=32 && w!=64)) w=32;

  em_ind(e);
  if (fs->step==1) fprintf(e->f, "for __%s in %lld to %lld loop", vn, start, end_incl);
  else             fprintf(e->f, "for __%s in %lld downto %lld loop", vn, start, end_incl);
  em_nl(e); em_inc(e);

  if (need_i_assign){ em_ind(e); fprintf(e->f, "%s := to_signed(__%s, %d);", vn, vn, w); em_nl(e); }
  return true;
}

/* ------------------------------------------------------------------------- */
/* Prepass: Loop-Variablen unterdrücken, wenn unbenutzt                      */
static bool str_in_vec(VecStr *vs, const char *s){
  for (size_t i=0;i<vs->len;i++) if (vs->items[i] && s && strcmp(vs->items[i], s)==0) return true;
  return false;
}
static void collect_suppress_loop_vars(Stmt *s, VecStr *out){
  if (!s) return;
  switch (s->kind){
    case ST_BLOCK:
      for (size_t i=0;i<s->block.stmts.len;i++) collect_suppress_loop_vars(s->block.stmts.items[i], out);
      break;
    case ST_IF:
      collect_suppress_loop_vars(s->if_s.then_br, out);
      collect_suppress_loop_vars(s->if_s.else_br, out);
      break;
    case ST_FOR: {
      Expr *init = s->for_s.init, *cond=s->for_s.cond, *post=s->for_s.post;
      if (init && cond && post && init->kind==EX_ASSIGN && init->assign.lhs && init->assign.lhs->kind==EX_VAR){
        const char *vn = init->assign.lhs->var.name;
        if (!stmt_uses_var(s->for_s.body, vn)) if (!str_in_vec(out, vn)) VecStr_push(out, (char*)vn);
      }
      collect_suppress_loop_vars(s->for_s.body, out);
    } break;
    case ST_WHILE: {
      if (s->while_s.cond && s->while_s.cond->kind==EX_BINOP){
        const char *vn=NULL; long long bd=0; int cmp=-1;
        if (normalize_var_cmp(s->while_s.cond, &vn, &bd, &cmp) && vn){
          if (!stmt_uses_var(s->while_s.body, vn)) if (!str_in_vec(out, vn)) VecStr_push(out, (char*)vn);
        }
      }
      collect_suppress_loop_vars(s->while_s.body, out);
    } break;
    case ST_RETURN:
    case ST_VARDECL:
    case ST_EXPR:
      break;
  }
}

/* ------------------------------------------------------------------------- */
/* Peephole: if (v < 0) { v = v + ...; } nach Modulo-Assign v unterdrücken  */
static bool is_modulus_fixup_if(Stmt *ifstmt, const char *vn){
  if (!ifstmt || ifstmt->kind!=ST_IF || !vn) return false;
  Expr *cond = ifstmt->if_s.cond;
  if (!cond || cond->kind!=EX_BINOP || cond->binop.op!=BIN_LT) return false;
  if (!cond->binop.lhs || cond->binop.lhs->kind!=EX_VAR) return false;
  if (strcmp(cond->binop.lhs->var.name, vn)!=0) return false;
  if (!cond->binop.rhs || cond->binop.rhs->kind!=EX_INT_LIT || cond->binop.rhs->int_lit.value!=0) return false;

  Stmt *tb = ifstmt->if_s.then_br; if (!tb) return false;
  Stmt *s = tb;
  if (tb->kind==ST_BLOCK){
    if (tb->block.stmts.len!=1) return false;
    s = tb->block.stmts.items[0];
  }
  if (s->kind!=ST_EXPR || !s->expr_s.expr || s->expr_s.expr->kind!=EX_ASSIGN) return false;
  Expr *as = s->expr_s.expr;
  if (!as->assign.lhs || as->assign.lhs->kind!=EX_VAR) return false;
  if (strcmp(as->assign.lhs->var.name, vn)!=0) return false;

  Expr *rhs = as->assign.rhs; if (!rhs || rhs->kind!=EX_BINOP) return false;
  if (rhs->binop.op!=BIN_ADD) return false;
  Expr *rl = rhs->binop.lhs, *rr = rhs->binop.rhs;
  bool left_is_vn = rl && rl->kind==EX_VAR && strcmp(rl->var.name, vn)==0;
  bool right_is_vn= rr && rr->kind==EX_VAR && strcmp(rr->var.name, vn)==0;
  return left_is_vn || right_is_vn;
}

/* ------------------------------------------------------------------------- */
/* Helper: Return-Expr aus Statement extrahieren                             */
static bool stmt_extract_return_expr(Stmt *node, Expr **out) {
  if (!node) return false;
  if (node->kind == ST_RETURN) {
    if (out) *out = node->ret_s.expr;
    return true;
  }
  if (node->kind == ST_BLOCK && node->block.stmts.len == 1) {
    Stmt *s0 = node->block.stmts.items[0];
    if (s0 && s0->kind == ST_RETURN) {
      if (out) *out = s0->ret_s.expr;
      return true;
    }
  }
  return false;
}

/* ------------------------------------------------------------------------- */
/* Statement-Emission (inkl. While→For, Peepholes, Return-Folding)          */
static bool emit_stmt_core(Em *e, Stmt *s, VecSym *sy, TypeKind ret_kind); // fwd

static bool emit_stmt(Em *e, Stmt *s, VecSym *sy, TypeKind ret_kind){
  if (!s){ em_ind(e); fputs("-- <null stmt>", e->f); em_nl(e); return true; }

  if (s->kind==ST_BLOCK){
    VecInit initmap; VecInit_init(&initmap);
    const char *recent_mod_var = NULL;

    for (size_t i=0;i<s->block.stmts.len;i++){
      Stmt *st = s->block.stmts.items[i];

      /* Peephole: Modulo-Fixup unmittelbar danach entfernen */
      if (recent_mod_var && st->kind==ST_IF && is_modulus_fixup_if(st, recent_mod_var)){
        recent_mod_var = NULL;
        continue;
      }

      /* Return-Folding: if (...) { return A; } return B; */
      if (st->kind == ST_IF && st->if_s.then_br && st->if_s.else_br == NULL && (i + 1) < s->block.stmts.len) {
        Expr *then_ret = NULL;
        if (stmt_extract_return_expr(st->if_s.then_br, &then_ret)) {
          Stmt *next = s->block.stmts.items[i + 1];
          if (next && next->kind == ST_RETURN) {
            const char *yname = "y";
            if (g_have_hints && g_hints.output_name[0]) yname = g_hints.output_name;

            em_ind(e); fputs("-- FOLDED if/return + return\n", e->f);
            em_ind(e); fputs("if (", e->f); emit_expr(e, st->if_s.cond, sy, 0, true); fputs(") then", e->f); em_nl(e);
            em_inc(e);
            em_ind(e); fputs(yname, e->f); fputs(" <= ", e->f);
            if (ret_kind==TY_BOOLEAN) emit_expr(e, then_ret, sy, 0, true);
            else                      emit_expr(e, then_ret, sy, width_of(ret_kind), false);
            fputs(";", e->f); em_nl(e);
            em_dec(e);

            em_ind(e); fputs("else", e->f); em_nl(e);
            em_inc(e);
            em_ind(e); fputs(yname, e->f); fputs(" <= ", e->f);
            if (ret_kind==TY_BOOLEAN) emit_expr(e, next->ret_s.expr, sy, 0, true);
            else                      emit_expr(e, next->ret_s.expr, sy, width_of(ret_kind), false);
            fputs(";", e->f); em_nl(e);
            em_dec(e);

            em_ind(e); fputs("end if;", e->f); em_nl(e);

            i += 1;                /* nächstes Return ist verarbeitet */
            recent_mod_var = NULL; /* Peephole-Zustand zurücksetzen   */
            continue;
          }
        }
      }

      /* while → for (Init im selben Block vorher) */
      if (st->kind==ST_WHILE){
        Expr *cond = st->while_s.cond;
        const char *vn = NULL; long long dummy_bound=0; int dummy_cmp=-1;
        if (normalize_var_cmp(cond, &vn, &dummy_bound, &dummy_cmp) && vn){
          Stmt *init_stmt = NULL;
          for (size_t j=0;j<i; ++j){
            if (stmt_is_literal_init_for(s->block.stmts.items[j], vn, NULL)){
              init_stmt = s->block.stmts.items[j];
            }
          }
          ForShape fs; char *name_out=NULL;
          if (init_stmt && analyze_while_with_init(st, init_stmt, &fs, &name_out)){
            bool need_i_assign=false;
            if (st->while_s.body){
              if (st->while_s.body->kind==ST_BLOCK){
                size_t n = st->while_s.body->block.stmts.len;
                for (size_t j=0;j<n;j++){
                  if (j==n-1){ int step=0; if (is_post_step_stmt(st->while_s.body->block.stmts.items[j], vn, &step)) break; }
                  if (stmt_uses_var(st->while_s.body->block.stmts.items[j], vn)){ need_i_assign=true; break; }
                }
              } else {
                int stp=0;
                if (!is_post_step_stmt(st->while_s.body, vn, &stp) && stmt_uses_var(st->while_s.body, vn))
                  need_i_assign=true;
              }
            }
            if (!emit_for_loop_shape(e, vn, &fs, need_i_assign, sy)) { VecInit_free(&initmap); return false; }
            if (!emit_body_without_trailing_step(e, st->while_s.body, sy, ret_kind, vn)){ em_dec(e); VecInit_free(&initmap); return false; }
            em_dec(e); em_ind(e); fputs("end loop;", e->f); em_nl(e);
            recent_mod_var = NULL;
            continue;
          }
        }
      }

      /* Normale Emission */
      if (!emit_stmt_core(e, st, sy, ret_kind)){ VecInit_free(&initmap); return false; }

      /* Initmap pflegen für spätere while-Analysen */
      if (st->kind==ST_VARDECL){
        if (st->vardecl.init && st->vardecl.init->kind==EX_INT_LIT)
          initmap_set(&initmap, st->vardecl.name, st);
        else
          initmap_clear(&initmap, st->vardecl.name);
      } else if (st->kind==ST_EXPR && st->expr_s.expr && st->expr_s.expr->kind==EX_ASSIGN){
        Expr *as = st->expr_s.expr;
        if (as->assign.lhs && as->assign.lhs->kind==EX_VAR){
          const char *vn2 = as->assign.lhs->var.name;
          if (as->assign.rhs && as->assign.rhs->kind==EX_INT_LIT)
            initmap_set(&initmap, vn2, st);
          else
            initmap_clear(&initmap, vn2);
        }
      }

      /* Peephole-Track: war dies ein Modulo-Assign? */
      if (st->kind==ST_EXPR && st->expr_s.expr && st->expr_s.expr->kind==EX_ASSIGN){
        Expr *as = st->expr_s.expr;
        if (as->assign.lhs && as->assign.lhs->kind==EX_VAR &&
            as->assign.rhs && as->assign.rhs->kind==EX_BINOP &&
            as->assign.rhs->binop.op==BIN_MOD){
          recent_mod_var = as->assign.lhs->var.name;
        } else {
          recent_mod_var = NULL;
        }
      } else {
        recent_mod_var = NULL;
      }
    }

    VecInit_free(&initmap);
    return true;
  }

  /* Kein Block → Kern */
  return emit_stmt_core(e, s, sy, ret_kind);
}

/* ------------------------------------------------------------------------- */
/* Kern-Emitter für Einzel-Statements                                        */
static bool emit_stmt_core(Em *e, Stmt *s, VecSym *sy, TypeKind ret_kind){
  if (!s){ em_ind(e); fputs("-- <null stmt>", e->f); em_nl(e); return true; }
  switch (s->kind){
    case ST_BLOCK:
      return emit_stmt(e, s, sy, ret_kind);

    case ST_IF:
      em_ind(e); fputs("if (", e->f); emit_expr(e, s->if_s.cond, sy, 0, true); fputs(") then", e->f); em_nl(e);
      em_inc(e); if (!emit_stmt(e, s->if_s.then_br, sy, ret_kind)) { em_dec(e); return false; } em_dec(e);
      if (s->if_s.else_br){ em_ind(e); fputs("else", e->f); em_nl(e); em_inc(e); if (!emit_stmt(e, s->if_s.else_br, sy, ret_kind)) { em_dec(e); return false; } em_dec(e); }
      em_ind(e); fputs("end if;", e->f); em_nl(e);
      return true;

    case ST_WHILE:
      em_ind(e); fputs("-- unsupported: while (use for with constant bounds and step ±1)", e->f); em_nl(e);
      return false;

    case ST_FOR: {
      const char *vn=NULL; ForShape fs;
      if (!build_shape_from_for_header(s, &vn, &fs)){ em_ind(e); fputs("-- unsupported: for", e->f); em_nl(e); return false; }
      bool need_i_assign = stmt_uses_var(s->for_s.body, vn);
      if (!emit_for_loop_shape(e, vn, &fs, need_i_assign, sy)) return false;
      if (!emit_stmt(e, s->for_s.body, sy, ret_kind)) { em_dec(e); return false; }
      em_dec(e); em_ind(e); fputs("end loop;", e->f); em_nl(e);
      return true;
    }

    case ST_RETURN: {
      em_ind(e);
      const char *yname = "y";
      if (g_have_hints && g_hints.output_name[0]) yname = g_hints.output_name;
      fputs(yname, e->f); fputs(" <= ", e->f);
      if (ret_kind==TY_BOOLEAN) emit_expr(e, s->ret_s.expr, sy, 0, true);
      else                      emit_expr(e, s->ret_s.expr, sy, width_of(ret_kind), false);
      fputs(";", e->f); em_nl(e);
      return true;
    }

    case ST_VARDECL:
      if (s->vardecl.init){
        bool dummy=false; TypeKind k = sym_lookup(sy, s->vardecl.name, &dummy);
        em_ind(e); fputs(s->vardecl.name, e->f); fputs(" := ", e->f);
        emit_expr(e, s->vardecl.init, sy, width_of(k), false);
        fputs(";", e->f); em_nl(e);
      }
      return true;

    case ST_EXPR:
      em_ind(e);
      if (s->expr_s.expr && s->expr_s.expr->kind==EX_ASSIGN){
        emit_assign_expr(e, s->expr_s.expr->assign.lhs, s->expr_s.expr->assign.rhs, sy);
        fputs(";", e->f); em_nl(e);
        return true;
      }
      fputs("-- unsupported expr stmt", e->f); em_nl(e);
      return false;
  }
  return false;
}

/* ------------------------------------------------------------------------- */
/* Lokale Variablen einsammeln                                               */
static void collect_locals(Stmt *s, VecSym *locals){
  if (!s) return;
  switch (s->kind){
    case ST_BLOCK:
      for (size_t i=0;i<s->block.stmts.len;i++) collect_locals(s->block.stmts.items[i], locals);
      break;
    case ST_IF:
      collect_locals(s->if_s.then_br, locals);
      collect_locals(s->if_s.else_br, locals);
      break;
    case ST_WHILE:
      collect_locals(s->while_s.body, locals);
      break;
    case ST_FOR:
      collect_locals(s->for_s.body, locals);
      break;
    case ST_RETURN: break;
    case ST_VARDECL: {
      Sym sym; sym.name=s->vardecl.name; sym.kind=s->vardecl.type.kind; sym.is_param=false;
      VecSym_push(locals, sym);
      break;
    }
    case ST_EXPR: break;
  }
}

/* ------------------------------------------------------------------------- */
/* Header & Prozesskopf                                                      */
static void emit_vhdl_header(FILE *f){
  fputs("library ieee;\n", f);
  fputs("use ieee.std_logic_1164.all;\n", f);
  fputs("use ieee.numeric_std.all;\n\n", f);
}

/* Prozesskopf:
 * - v2008 & keine Sensitivität → process(all)
 * - Falls Sensitivität vorhanden (has_sensitivity ODER sensitivity[0]) → diese Liste
 * - Sonst: automatisch aus allen Eingangsparametern
 */
static void emit_process_header_for_method(FILE *f, Method *m){
  bool has_list = false;
  if (g_have_hints){
    has_list = g_hints.has_sensitivity || (g_hints.sensitivity[0] != '\0'); /* akzeptiert auch Alias */
  }

  if (g_have_hints && g_hints.vhdl2008 && !has_list) {
    fprintf(f, "  process(all) is\n");
    return;
  }
  if (has_list) {
    fprintf(f, "  process(%s) is\n", g_hints.sensitivity);
    return;
  }
  /* Auto: alle Eingangs-Parameter */
  fputs("  process(", f);
  bool first = true;
  for (size_t i=0;i<m->params.len;i++){
    if (!first) fputs(", ", f);
    fputs(m->params.items[i].name, f);
    first=false;
  }
  if (first) fputs("y", f); /* Fallback */
  fputs(") is\n", f);
}

/* ------------------------------------------------------------------------- */
/* Methoden-/Top-Level-Emission                                              */
static bool name_in(const VecStr *vs, const char *n){
  for (size_t i=0;i<vs->len;i++) if (vs->items[i] && n && strcmp(vs->items[i], n)==0) return true;
  return false;
}
static void collect_suppress_loop_vars(Stmt *s, VecStr *out); /* fwd */

static void emit_method(Em *e, Method *m){
  TypeKind rk = m->ret_type.kind;
  if (rk == TY_VOID) return;

  /* Prepass: unterdrückbare Loop-Variablen */
  VecStr suppress; VecStr_init(&suppress);
  if (m->body) collect_suppress_loop_vars(m->body, &suppress);

  /* Symboltabelle: Params + Locals(filtered) */
  VecSym sy; VecSym_init(&sy);
  for (size_t i=0;i<m->params.len;i++){
    Sym s; s.name=m->params.items[i].name; s.kind=m->params.items[i].type.kind; s.is_param=true;
    VecSym_push(&sy,s);
  }
  VecSym locals; VecSym_init(&locals);
  if (m->body && m->body->kind==ST_BLOCK) collect_locals(m->body, &locals);

  VecSym filtered; VecSym_init(&filtered);
  for (size_t i=0;i<locals.len;i++){ if (name_in(&suppress, locals.items[i].name)) continue; VecSym_push(&filtered, locals.items[i]); }
  for (size_t i=0;i<filtered.len;i++) VecSym_push(&sy, filtered.items[i]);

  /* Entity-Name ggf. überschreiben */
  const char *entity_name = m->name;
  if (g_have_hints && g_hints.entity_name[0]) entity_name = g_hints.entity_name;

  fprintf(e->f, "entity %s is\n", entity_name);
  fputs("  port(\n", e->f);
  for (size_t i=0;i<m->params.len;i++){
    fprintf(e->f, "    %s : in %s;\n", m->params.items[i].name, vhdl_type(m->params.items[i].type.kind));
  }
  const char *yname = "y";
  if (g_have_hints && g_hints.output_name[0]) yname = g_hints.output_name;
  fprintf(e->f, "    %s : out %s\n", yname, vhdl_type(rk));
  fputs("  );\n", e->f);
  fputs("end entity;\n", e->f);

  fprintf(e->f, "architecture rtl of %s is\n", entity_name);
  fputs("begin\n", e->f);

  emit_process_header_for_method(e->f, m);

  /* Lokale Variablen-Decls */
  for (size_t i=0;i<filtered.len;i++){
    if (filtered.items[i].kind==TY_BOOLEAN)
      fprintf(e->f, "    variable %s : std_logic;\n", filtered.items[i].name);
    else
      fprintf(e->f, "    variable %s : signed(%d downto 0);\n", filtered.items[i].name, width_of(filtered.items[i].kind)-1);
  }
  fputs("  begin\n", e->f);

  /* Default-Out (wird ggf. überschrieben) */
  if (rk==TY_BOOLEAN) { fputs("    ", e->f); fputs(yname, e->f); fputs(" <= '0';\n", e->f); }
  else                { fputs("    ", e->f); fputs(yname, e->f); fputs(" <= (others => '0');\n", e->f); }

  /* Body */
  if (m->body){
    Em body=*e; body.indent=2;
    if (m->body->kind==ST_BLOCK){
      for (size_t i=0;i<m->body->block.stmts.len;i++){
        Stmt *st = m->body->block.stmts.items[i];
        if (st->kind==ST_VARDECL && name_in(&suppress, st->vardecl.name)) continue;
        if (!emit_stmt(&body, st, &sy, rk)){ em_ind(&body); fputs("-- unsupported statement encountered\n", body.f); break; }
      }
    } else {
      emit_stmt(&body, m->body, &sy, rk);
    }
  }

  fputs("  end process;\n", e->f);
  fputs("end architecture;\n", e->f);
  em_nl(e);

  VecStr_free(&suppress);
  VecSym_free(&sy);
  VecSym_free(&locals);
  VecSym_free(&filtered);
}

/* ------------------------------------------------------------------------- */
/* Top-Level                                                                  */
bool codegen_vhdl(Program *ir, const char *out_path){
  FILE *f = fopen(out_path, "wb");
  if (!f) return false;
  Em e; em_init(&e, f);

  emit_vhdl_header(f);

  if (ir->classes.len==0){ fputs("-- empty program\n", f); fclose(f); return true; }

  ClassDecl *c = ir->classes.items[0];
  for (size_t i=0;i<c->methods.len;i++) emit_method(&e, c->methods.items[i]);

  fclose(f);
  return true;
}
