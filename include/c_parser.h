#ifndef C_PARSER_H
#define C_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include "ast.h"

// Parse a restricted C99 subset (as produced by our Stage-A C codegen)
// into the shared AST. Returns NULL on error.
Program *parse_c(const char *filename, const char *buffer, size_t len, bool *unsupported);

#endif // C_PARSER_H
