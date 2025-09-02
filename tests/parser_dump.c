#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

static char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  if (n < 0) { fclose(f); return NULL; }
  rewind(f);
  char *buf = (char*)malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t rd = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[rd] = '\0';
  if (out_len) *out_len = rd;
  return buf;
}

static const char* tname(TypeKind k){
  switch (k){
    case TY_INT: return "int";
    case TY_LONG: return "long";
    case TY_BOOLEAN: return "boolean";
    case TY_VOID: return "void";
  }
  return "?";
}

static const char* binop(BinOpKind k){
  switch (k){
    case BIN_ADD: return "+"; case BIN_SUB: return "-"; case BIN_MUL: return "*";
    case BIN_DIV: return "/"; case BIN_MOD: return "%";
    case BIN_EQ: return "=="; case BIN_NEQ: return "!="; case BIN_LT: return "<";
    case BIN_LE: return "<="; case BIN_GT: return ">"; case BIN_GE: return ">=";
    case BIN_AND: return "&&"; case BIN_OR: return "||";
  }
  return "?";
}
static const char* unop(UnOpKind k){ return (k==UN_NOT)?"!":"-"; }

static void pad(int n){ while(n--) putchar(' '); }

static void dump_expr(Expr *e, int ind){
  if (!e){ pad(ind); puts("<null-expr>"); return; }
  switch (e->kind){
    case EX_INT_LIT: pad(ind); printf("Int(%lld)\n",(long long)e->int_lit.value); break;
    case EX_BOOL_LIT: pad(ind); printf("Bool(%s)\n", e->bool_lit.value?"true":"false"); break;
    case EX_VAR: pad(ind); printf("Var(%s)\n", e->var.name); break;
    case EX_UNOP:
      pad(ind); printf("UnOp(%s)\n", unop(e->unop.op));
      dump_expr(e->unop.expr, ind+2);
      break;
    case EX_BINOP:
      pad(ind); printf("BinOp(%s)\n", binop(e->binop.op));
      dump_expr(e->binop.lhs, ind+2);
      dump_expr(e->binop.rhs, ind+2);
      break;
    case EX_ASSIGN:
      pad(ind); puts("Assign");
      dump_expr(e->assign.lhs, ind+2);
      dump_expr(e->assign.rhs, ind+2);
      break;
    case EX_CALL:
      pad(ind); printf("Call(%s)\n", e->call.name);
      for (size_t i=0;i<e->call.args.len;i++) dump_expr(e->call.args.items[i], ind+2);
      break;
  }
}

static void dump_stmt(Stmt *s, int ind){
  if (!s){ pad(ind); puts("<null-stmt>"); return; }
  switch (s->kind){
    case ST_BLOCK:
      pad(ind); puts("Block");
      for (size_t i=0;i<s->block.stmts.len;i++) dump_stmt(s->block.stmts.items[i], ind+2);
      break;
    case ST_IF:
      pad(ind); puts("If");
      pad(ind+2); puts("Cond:"); dump_expr(s->if_s.cond, ind+4);
      pad(ind+2); puts("Then:"); dump_stmt(s->if_s.then_br, ind+4);
      pad(ind+2); puts("Else:"); dump_stmt(s->if_s.else_br, ind+4);
      break;
    case ST_WHILE:
      pad(ind); puts("While");
      dump_expr(s->while_s.cond, ind+2);
      dump_stmt(s->while_s.body, ind+2);
      break;
    case ST_FOR:
      pad(ind); puts("For");
      pad(ind+2); puts("Init:"); dump_expr(s->for_s.init, ind+4);
      pad(ind+2); puts("Cond:"); dump_expr(s->for_s.cond, ind+4);
      pad(ind+2); puts("Post:"); dump_expr(s->for_s.post, ind+4);
      pad(ind+2); puts("Body:"); dump_stmt(s->for_s.body, ind+4);
      break;
    case ST_RETURN:
      pad(ind); puts("Return");
      dump_expr(s->ret_s.expr, ind+2);
      break;
    case ST_VARDECL:
      pad(ind); printf("VarDecl(%s %s)\n", tname(s->vardecl.type.kind), s->vardecl.name);
      if (s->vardecl.init){ pad(ind+2); puts("Init:"); dump_expr(s->vardecl.init, ind+4); }
      break;
    case ST_EXPR:
      pad(ind); puts("ExprStmt");
      dump_expr(s->expr_s.expr, ind+2);
      break;
  }
}

int main(int argc, char **argv){
  if (argc!=2){ fprintf(stderr,"usage: %s <input.java>\n", argv[0]); return 2; }
  size_t len=0; char *buf = read_file(argv[1], &len);
  if (!buf){ fprintf(stderr,"error: cannot read '%s'\n", argv[1]); return 2; }

  Program *prog = parse_java(argv[1], buf, len);
  if (!prog){ fprintf(stderr,"parse failed\n"); free(buf); return 1; }

  for (size_t i=0;i<prog->classes.len;i++){
    ClassDecl *c = prog->classes.items[i];
    printf("Class %s\n", c->name);
    for (size_t f=0; f<c->fields.len; f++){
      Field *fd = c->fields.items[f];
      printf("  Field %s %s\n", tname(fd->type.kind), fd->name);
    }
    for (size_t m=0; m<c->methods.len; m++){
      Method *md = c->methods.items[m];
      printf("  Method %s %s(", tname(md->ret_type.kind), md->name);
      for (size_t p=0; p<md->params.len; p++){
        Param pr = md->params.items[p];
        printf("%s %s%s", tname(pr.type.kind), pr.name, (p+1<md->params.len)?", ":"");
      }
      puts(")");
      dump_stmt(md->body, 4);
    }
  }

  ast_free_program(prog);
  free(buf);
  return 0;
}
