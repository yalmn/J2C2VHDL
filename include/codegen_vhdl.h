#ifndef CODEGEN_VHDL_H
#define CODEGEN_VHDL_H
#include <stdbool.h>
#include "ast.h"

// Generate VHDL-2008 source from C/IR into out_path. Returns true on success.
bool codegen_vhdl(Program *ir, const char *out_path);

#endif // CODEGEN_VHDL_H
