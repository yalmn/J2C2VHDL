#ifndef DIAG_H
#define DIAG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  unsigned line;
  unsigned col;
} SourcePos;

// Print an error with filename and source position (1-based).
void diag_error_at(const char *filename, SourcePos pos, const char *fmt, ...);

// Print a simple error without position.
void diag_error_simple(const char *filename, const char *fmt, ...);

// Print a system error (errno) for I/O contexts.
void diag_perror(const char *filename, const char *msg);

#endif // DIAG_H