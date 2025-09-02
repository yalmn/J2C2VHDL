#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "diag.h"
#include "parser.h"        // Java → AST
#include "c_parser.h"      // C    → AST
#include "codegen_c.h"
#include "codegen_vhdl.h"
#include "vhdl_pragmas.h"  // <-- NEU: Java-Kommentar-Pragmas → VHDL-Hints

// Forward-Deklaration; Setter ist in codegen_vhdl.c implementiert.
void codegen_vhdl_set_hints(const VhdlHints *hints);

#define EXIT_OK 0
#define EXIT_PARSE_ERROR 1
#define EXIT_IO_ERROR 2
#define EXIT_UNSUPPORTED 3

typedef enum {
  MODE_NONE = 0,
  MODE_JAVA_TO_C,
  MODE_C_TO_VHDL,
  MODE_FULL
} Mode;

static void print_usage(const char *prog) {
  fprintf(stdout,
    "Usage:\n"
    "  %s --java-to-c <input.java> -o <output.c>\n"
    "  %s --c-to-vhdl <input.c> -o <output.vhd>\n"
    "  %s --full <input.java> -o <output.vhd>\n"
    "  %s --help\n\n"
    "Exit codes: 0=ok, 1=parse error, 2=I/O/usage error, 3=unsupported feature.\n",
    prog, prog, prog, prog);
}

static bool has_ext(const char *path, const char *ext) {
  size_t lp = strlen(path), le = strlen(ext);
  if (lp < le) return false;
  return strcmp(path + (lp - le), ext) == 0;
}

static char *read_file_all(const char *path, size_t *out_len){
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0){ fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0){ fclose(f); return NULL; }
  rewind(f);
  char *buf = (char*)malloc((size_t)n + 1);
  if (!buf){ fclose(f); return NULL; }
  size_t rd = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[rd] = '\0';
  if (out_len) *out_len = rd;
  return buf;
}

int main(int argc, char **argv) {
  const char *prog = (argc > 0 && argv[0]) ? argv[0] : "j2c2vhdl";
  if (argc == 1) { print_usage(prog); return EXIT_IO_ERROR; }

  Mode mode = MODE_NONE;
  const char *in_path = NULL;
  const char *out_path = NULL;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (strcmp(arg, "--help") == 0) { print_usage(prog); return EXIT_OK; }
    else if (strcmp(arg, "--java-to-c") == 0) { if (mode!=MODE_NONE){ diag_error_simple(NULL,"only one mode may be specified"); return EXIT_IO_ERROR; } mode=MODE_JAVA_TO_C; if (i+1<argc && argv[i+1][0]!='-') in_path=argv[++i]; }
    else if (strcmp(arg, "--c-to-vhdl") == 0){ if (mode!=MODE_NONE){ diag_error_simple(NULL,"only one mode may be specified"); return EXIT_IO_ERROR; } mode=MODE_C_TO_VHDL; if (i+1<argc && argv[i+1][0]!='-') in_path=argv[++i]; }
    else if (strcmp(arg, "--full") == 0)     { if (mode!=MODE_NONE){ diag_error_simple(NULL,"only one mode may be specified"); return EXIT_IO_ERROR; } mode=MODE_FULL; if (i+1<argc && argv[i+1][0]!='-') in_path=argv[++i]; }
    else if (strcmp(arg, "-o") == 0){ if (i+1>=argc){ diag_error_simple(NULL,"-o requires a path argument"); return EXIT_IO_ERROR; } out_path=argv[++i]; }
    else if (arg[0] == '-') { diag_error_simple(NULL, "unknown option: %s", arg); return EXIT_IO_ERROR; }
    else { if (!in_path) in_path=arg; else { diag_error_simple(NULL,"unexpected extra argument: %s", arg); return EXIT_IO_ERROR; } }
  }

  if (mode==MODE_NONE){ diag_error_simple(NULL,"no mode specified"); print_usage(prog); return EXIT_IO_ERROR; }
  if (!in_path || !out_path){ diag_error_simple(NULL,"both input path and -o <output> are required"); print_usage(prog); return EXIT_IO_ERROR; }

  if ((mode == MODE_JAVA_TO_C || mode == MODE_FULL) && !has_ext(in_path, ".java"))
    fprintf(stderr, "warning: expected a .java input, got '%s'\n", in_path);
  if (mode == MODE_C_TO_VHDL && !has_ext(in_path, ".c"))
    fprintf(stderr, "warning: expected a .c input, got '%s'\n", in_path);

  if (mode == MODE_JAVA_TO_C) {
    size_t n=0; char *buf = read_file_all(in_path, &n);
    if (!buf){ diag_perror(in_path, "cannot open input for reading"); return EXIT_IO_ERROR; }
    Program *ast = parse_java(in_path, buf, n);
    free(buf);
    if (!ast){ return EXIT_PARSE_ERROR; }
    bool ok = codegen_c(ast, out_path, NULL);
    ast_free_program(ast);
    if (!ok){ diag_error_simple(out_path, "failed to write C output"); return EXIT_IO_ERROR; }
    printf("j2c2vhdl: Java → C done.\n");
    return EXIT_OK;
  }

  if (mode == MODE_C_TO_VHDL) {
    size_t n=0; char *buf = read_file_all(in_path, &n);
    if (!buf){ diag_perror(in_path, "cannot open input for reading"); return EXIT_IO_ERROR; }
    bool unsupported=false;
    Program *ast = parse_c(in_path, buf, n, &unsupported);
    free(buf);
    if (!ast){ return EXIT_PARSE_ERROR; }
    bool ok = codegen_vhdl(ast, out_path);
    ast_free_program(ast);
    if (!ok){ diag_error_simple(out_path, "failed to write VHDL output"); return EXIT_IO_ERROR; }
    if (unsupported){ return EXIT_UNSUPPORTED; } // feature parsed but flagged as unsupported
    printf("j2c2vhdl: C → VHDL done.\n");
    return EXIT_OK;
  }

  if (mode == MODE_FULL) {
    size_t n=0; char *buf = read_file_all(in_path, &n);
    if (!buf){ diag_perror(in_path, "cannot open input for reading"); return EXIT_IO_ERROR; }
    Program *ast = parse_java(in_path, buf, n);
    free(buf);
    if (!ast){ return EXIT_PARSE_ERROR; }

    // >>> PRAGMAS: Java-Kommentare einlesen und dem VHDL-Backend übergeben
    VhdlHints hints;
    vhdl_pragmas_from_java(in_path, &hints);   // liest Datei erneut ein und füllt 'hints' (robust gegen fehlende Pragmas)
    codegen_vhdl_set_hints(&hints);            // an Backend übergeben

    bool ok = codegen_vhdl(ast, out_path);
    ast_free_program(ast);
    if (!ok){ diag_error_simple(out_path, "failed to write VHDL output"); return EXIT_IO_ERROR; }
    printf("j2c2vhdl: Java → VHDL done.\n");
    return EXIT_OK;
  }

  return EXIT_OK;
}
