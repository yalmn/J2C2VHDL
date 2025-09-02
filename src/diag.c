#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>

static void vreport(const char *kind, const char *filename, SourcePos pos, const char *fmt, va_list ap) {
  if (filename && filename[0]) {
    if (pos.line || pos.col) {
      fprintf(stderr, "%s:%u:%u: %s: ", filename, pos.line, pos.col, kind);
    } else {
      fprintf(stderr, "%s: %s: ", filename, kind);
    }
  } else {
    fprintf(stderr, "%s: ", kind);
  }
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void diag_error_at(const char *filename, SourcePos pos, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vreport("error", filename, pos, fmt, ap);
  va_end(ap);
}

void diag_error_simple(const char *filename, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  SourcePos p = {0u, 0u};
  vreport("error", filename, p, fmt, ap);
  va_end(ap);
}

void diag_perror(const char *filename, const char *msg) {
  if (filename && filename[0]) {
    fprintf(stderr, "io: %s: %s: %s\n", filename, msg, strerror(errno));
  } else {
    fprintf(stderr, "io: %s: %s\n", msg, strerror(errno));
  }
}
