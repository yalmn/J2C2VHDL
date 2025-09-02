#ifndef PARSER_H
#define PARSER_H
#include <stddef.h>
#include "ast.h"

// Parse Java source into AST Program (MVP: eine public class pro Datei).
// Rückgabe: NULL bei Fehler (Fehler wurden bereits via diag_* ausgegeben).
Program *parse_java(const char *filename, const char *buffer, size_t len);

#endif // PARSER_H
