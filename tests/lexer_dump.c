#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"

static char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
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

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <input.java>\n", argv[0]);
    return 2;
  }
  size_t len = 0;
  char *buf = read_file(argv[1], &len);
  if (!buf) {
    fprintf(stderr, "error: cannot read '%s'\n", argv[1]);
    return 2;
  }

  Lexer lx;
  lexer_init(&lx, argv[1], buf, len);

  for (;;) {
    Token t = lexer_next(&lx);
    if (t.kind == TOK_EOF) {
      printf("%u:%u %-10s\n", t.pos.line, t.pos.col, token_kind_name(t.kind));
      break;
    }
    printf("%u:%u %-10s '%.*s'\n",
           t.pos.line, t.pos.col,
           token_kind_name(t.kind),
           (int)t.length, t.lexeme ? t.lexeme : "");
  }

  free(buf);
  return 0;
}
