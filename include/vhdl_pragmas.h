#ifndef J2C2VHDL_VHDL_PRAGMAS_H
#define J2C2VHDL_VHDL_PRAGMAS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  ARITH_NUMERIC_STD = 0,
  ARITH_STD_LOGIC_ARITH = 1
} ArithLib;

typedef struct {
  bool has_clock;
  char clock_name[64];

  bool has_reset;
  char reset_name[64];
  // reset-level: low|high
  bool reset_active_low; // true = active low ('0'), false = active high ('1')
  // reset-style: async|sync (nur für spätere Clocked-Prozesse; derzeit nur Port + optional Sens.)
  bool reset_async;

  bool has_sensitivity;
  char sensitivity[256]; // "a, b, c"

  // Dialekt: v93 (Default) oder v2008
  bool vhdl2008; // wenn true → process(all) erlaubt; sonst explizite Liste

  // Header/Entity-Overrides
  char entity_name[128];   // optional
  char output_name[128];   // optional (statt "y")

  // zusätzliche use-Klauseln
  char extra_use[8][128];
  size_t extra_use_count;

  // Arithmetik-Bibliothek
  ArithLib arith_lib;

  // Platzhalter für künftige Pragmas (geparst, noch nicht genutzt)
  char port_prefix[32];
  struct {
    char from[64];
    char to[64];
  } param_ren[16];
  size_t param_ren_count;

  // Stil
  bool with_select;   // noch nicht aktiv
  bool emit_comments; // noch nicht aktiv

  // Sensitivity: reset einbeziehen?
  bool reset_in_sensitivity;

} VhdlHints;

// Liest Pragmas aus einer Java-Datei (Zeilen mit Präfix-Varianten):
//   // vhdl-dialect: v93|v2008
//   // include-use: ieee.std_logic_unsigned.all
//   // entity-name: my_entity
//   // output-name: result
//   // clock: clk
//   // reset: rst_n
//   // reset-level: low|high
//   // reset-style: async|sync
//   // reset-in-sensitivity: yes|no
//   // sensitivity-list: a, b, c
//   // arith-lib: numeric_std|std_logic_arith
//   // port-prefix: i_
//   // param-rename: a=a_in,b=b_in
//   // with-select: on|off
//   // emit-comments: on|off
void vhdl_pragmas_from_java(const char *path, VhdlHints *out);

#endif // J2C2VHDL_VHDL_PRAGMAS_H
