#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>   // malloc/realloc/free
#include "diag.h"

// ---------- Simple growable vector helpers (header-only, inline) ----------
#define VEC_DECL(T, Name) \
  typedef struct { T *items; size_t len; size_t cap; } Name; \
  static inline void Name##_init(Name *v){ v->items=NULL; v->len=v->cap=0; } \
  static inline bool Name##_push(Name *v, T x){ \
    if (v->len==v->cap){ size_t n=v->cap? v->cap*2:4; \
      T* p=(T*)realloc(v->items, n*sizeof(T)); if(!p) return false; v->items=p; v->cap=n; } \
    v->items[v->len++]=x; return true; } \
  static inline void Name##_free(Name *v){ free(v->items); v->items=NULL; v->len=v->cap=0; }

typedef enum {
  TY_INT,
  TY_LONG,
  TY_BOOLEAN,
  TY_VOID
} TypeKind;

typedef struct {
  TypeKind kind;
  SourcePos pos;   // position where type appeared
} Type;

// Forward decls
struct Expr;
struct Stmt;

typedef enum {
  BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
  BIN_EQ, BIN_NEQ, BIN_LT, BIN_LE, BIN_GT, BIN_GE,
  BIN_AND, BIN_OR
} BinOpKind;

typedef enum { UN_NEG, UN_NOT } UnOpKind;

typedef enum {
  EX_INT_LIT,
  EX_BOOL_LIT,
  EX_VAR,
  EX_BINOP,
  EX_UNOP,
  EX_ASSIGN,
  EX_CALL
} ExprKind;

VEC_DECL(struct Expr*, VecExpr)

typedef struct Expr {
  ExprKind kind;
  SourcePos pos;
  union {
    struct { int64_t value; } int_lit;
    struct { bool value; } bool_lit;
    struct { char *name; } var;
    struct { BinOpKind op; struct Expr *lhs; struct Expr *rhs; } binop;
    struct { UnOpKind op; struct Expr *expr; } unop;
    struct { struct Expr *lhs; struct Expr *rhs; } assign; // lhs should be EX_VAR
    struct { char *name; VecExpr args; } call;
  };
} Expr;

typedef enum {
  ST_BLOCK,
  ST_IF,
  ST_WHILE,
  ST_FOR,
  ST_RETURN,
  ST_VARDECL,
  ST_EXPR
} StmtKind;

VEC_DECL(struct Stmt*, VecStmt)

typedef struct Stmt {
  StmtKind kind;
  SourcePos pos;
  union {
    struct { VecStmt stmts; } block;
    struct { Expr *cond; struct Stmt *then_br; struct Stmt *else_br; } if_s;
    struct { Expr *cond; struct Stmt *body; } while_s;
    struct { Expr *init; Expr *cond; Expr *post; struct Stmt *body; } for_s; // MVP: init is ExprStmt only
    struct { Expr *expr; } ret_s;    // may be NULL for void
    struct { Type type; char *name; Expr *init; } vardecl;
    struct { Expr *expr; } expr_s;
  };
} Stmt;

typedef struct {
  Type type;
  char *name;
  SourcePos pos;
} Param;
VEC_DECL(Param, VecParam)

typedef struct {
  Type ret_type;
  char *name;
  VecParam params;
  Stmt *body;       // block
  SourcePos pos;
} Method;
VEC_DECL(Method*, VecMethod)

typedef struct {
  Type type;
  char *name;
  Expr *init;       // optional
  SourcePos pos;
} Field;
VEC_DECL(Field*, VecField)

typedef struct {
  char *name;
  VecField fields;
  VecMethod methods;
  SourcePos pos;
} ClassDecl;
VEC_DECL(ClassDecl*, VecClass)

typedef struct Program {
  VecClass classes;
} Program;

// Constructors / memory management
Program *ast_program_new(void);
void     ast_free_program(Program *p);

// strdup (portable)
char *ast_strdup_slice(const char *p, size_t n);
char *ast_strdup_cstr(const char *s);

// Node factories (helpers)
Expr *ast_new_int(SourcePos pos, int64_t v);
Expr *ast_new_bool(SourcePos pos, bool v);
Expr *ast_new_var(SourcePos pos, char *name);
Expr *ast_new_bin(SourcePos pos, BinOpKind op, Expr *l, Expr *r);
Expr *ast_new_un(SourcePos pos, UnOpKind op, Expr *e);
Expr *ast_new_assign(SourcePos pos, Expr *l, Expr *r);
Expr *ast_new_call(SourcePos pos, char *name);

Stmt *ast_new_block(SourcePos pos);
Stmt *ast_new_if(SourcePos pos, Expr *cond, Stmt *then_br, Stmt *else_br);
Stmt *ast_new_while(SourcePos pos, Expr *cond, Stmt *body);
Stmt *ast_new_for(SourcePos pos, Expr *init, Expr *cond, Expr *post, Stmt *body);
Stmt *ast_new_return(SourcePos pos, Expr *expr);
Stmt *ast_new_vardecl(SourcePos pos, Type t, char *name, Expr *init);
Stmt *ast_new_exprstmt(SourcePos pos, Expr *expr);

Type  ast_type(TypeKind k, SourcePos pos);

#endif // AST_H
