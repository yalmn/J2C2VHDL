#ifndef CODEGEN_C_H
#define CODEGEN_C_H
#include <stdbool.h>
#include "ast.h"

// Generate C99 source from AST into out_path. Returns true on success.
bool codegen_c(Program *prog, const char *out_path, const char *top_class_name);

#endif // CODEGEN_C_H
