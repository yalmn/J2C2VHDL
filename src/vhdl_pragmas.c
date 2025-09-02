#include "vhdl_pragmas.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void ztrim(char *s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n && (s[n-1]=='\r' || s[n-1]=='\n' || s[n-1]==' ' || s[n-1]=='\t')) s[--n]='\0';
  size_t i=0; while (s[i]==' ' || s[i]=='\t') i++;
  if (i) memmove(s, s+i, strlen(s+i)+1);
}

static void lower_ascii(char *s){
  for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
}

static int starts_with(const char *s, const char *prefix){
  size_t n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

static void parse_param_rename_list(VhdlHints *h, char *val){
  // Format: "a=a_in, b=b_in"
  char *p = val;
  while (*p) {
    while (*p==' '||*p=='\t'||*p==',') p++;
    if (!*p) break;
    char left[64]={0}, right[64]={0};
    size_t li=0, ri=0;
    while (*p && *p!='=' && *p!=',' && *p!='\n' && *p!='\r') { if (li+1<sizeof left) left[li++]=*p; p++; }
    left[li]='\0'; ztrim(left);
    if (*p=='=') p++;
    while (*p==' '||*p=='\t') p++;
    while (*p && *p!=',' && *p!='\n' && *p!='\r') { if (ri+1<sizeof right) right[ri++]=*p; p++; }
    right[ri]='\0'; ztrim(right);
    if (left[0] && right[0] && h->param_ren_count < (sizeof h->param_ren/sizeof h->param_ren[0])) {
      strncpy(h->param_ren[h->param_ren_count].from, left, sizeof h->param_ren[0].from - 1);
      strncpy(h->param_ren[h->param_ren_count].to,   right, sizeof h->param_ren[0].to   - 1);
      h->param_ren_count++;
    }
    if (*p==',') p++;
  }
}

void vhdl_pragmas_from_java(const char *path, VhdlHints *out){
  memset(out, 0, sizeof *out);
  out->arith_lib = ARITH_NUMERIC_STD; // default
  // defaults: VHDL-93, reset_in_sensitivity yes (falls reset existiert)
  out->vhdl2008 = false;
  out->reset_in_sensitivity = true;

  FILE *f = fopen(path, "rb");
  if (!f) return;
  char line[1024];
  while (fgets(line, sizeof line, f)) {
    char raw[1024]; strncpy(raw, line, sizeof raw - 1); raw[sizeof raw - 1] = 0;
    ztrim(raw);
    // muss mit '//' beginnen
    if (!starts_with(raw, "//")) continue;
    char *p = raw + 2; ztrim(p);

    // Key extrahieren bis ':'
    char key[128]={0}, val[896]={0};
    const char *colon = strchr(p, ':');
    if (!colon) continue;
    size_t klen = (size_t)(colon - p);
    if (klen >= sizeof key) klen = sizeof key - 1;
    strncpy(key, p, klen); key[klen]='\0';
    char *v = (char*)(colon + 1);
    ztrim(v);

    char key_lc[128]; strncpy(key_lc, key, sizeof key_lc - 1); key_lc[sizeof key_lc - 1]=0;
    lower_ascii(key_lc);

    if (strcmp(key_lc, "vhdl-dialect") == 0) {
      char tmp[64]; strncpy(tmp, v, sizeof tmp - 1); tmp[sizeof tmp - 1]=0; lower_ascii(tmp);
      out->vhdl2008 = (strcmp(tmp,"v2008")==0 || strcmp(tmp,"2008")==0);
    } else if (strcmp(key_lc, "include-use") == 0) {
      if (out->extra_use_count < (sizeof out->extra_use/sizeof out->extra_use[0])) {
        strncpy(out->extra_use[out->extra_use_count++], v, 127);
      }
    } else if (strcmp(key_lc, "entity-name") == 0) {
      strncpy(out->entity_name, v, sizeof out->entity_name - 1);
    } else if (strcmp(key_lc, "output-name") == 0) {
      strncpy(out->output_name, v, sizeof out->output_name - 1);
    } else if (strcmp(key_lc, "clock") == 0) {
      out->has_clock = (v[0] != 0);
      if (out->has_clock) strncpy(out->clock_name, v, sizeof out->clock_name - 1);
    } else if (strcmp(key_lc, "reset") == 0) {
      out->has_reset = (v[0] != 0);
      if (out->has_reset) strncpy(out->reset_name, v, sizeof out->reset_name - 1);
    } else if (strcmp(key_lc, "reset-level") == 0) {
      char tmp[16]; strncpy(tmp, v, sizeof tmp - 1); tmp[sizeof tmp - 1]=0; lower_ascii(tmp);
      out->reset_active_low = (strcmp(tmp,"low")==0 || strcmp(tmp,"0")==0);
    } else if (strcmp(key_lc, "reset-style") == 0) {
      char tmp[16]; strncpy(tmp, v, sizeof tmp - 1); tmp[sizeof tmp - 1]=0; lower_ascii(tmp);
      out->reset_async = (strcmp(tmp,"async")==0);
    } else if (strcmp(key_lc, "reset-in-sensitivity") == 0) {
      char tmp[8]; strncpy(tmp, v, sizeof tmp - 1); tmp[sizeof tmp - 1]=0; lower_ascii(tmp);
      out->reset_in_sensitivity = (strcmp(tmp,"yes")==0 || strcmp(tmp,"true")==0 || strcmp(tmp,"1")==0);
    } else if (strcmp(key_lc, "sensitivity-list") == 0) {
      out->has_sensitivity = (v[0] != 0);
      if (out->has_sensitivity) strncpy(out->sensitivity, v, sizeof out->sensitivity - 1);
    } else if (strcmp(key_lc, "arith-lib") == 0) {
      char tmp[64]; strncpy(tmp, v, sizeof tmp - 1); tmp[sizeof tmp - 1]=0; lower_ascii(tmp);
      out->arith_lib = (strcmp(tmp,"std_logic_arith")==0) ? ARITH_STD_LOGIC_ARITH : ARITH_NUMERIC_STD;
    } else if (strcmp(key_lc, "port-prefix") == 0) {
      strncpy(out->port_prefix, v, sizeof out->port_prefix - 1);
    } else if (strcmp(key_lc, "param-rename") == 0) {
      parse_param_rename_list(out, v);
    } else if (strcmp(key_lc, "with-select") == 0) {
      char tmp[8]; strncpy(tmp, v, sizeof tmp - 1); tmp[sizeof tmp - 1]=0; lower_ascii(tmp);
      out->with_select = (strcmp(tmp,"on")==0 || strcmp(tmp,"true")==0 || strcmp(tmp,"1")==0);
    } else if (strcmp(key_lc, "emit-comments") == 0) {
      char tmp[8]; strncpy(tmp, v, sizeof tmp - 1); tmp[sizeof tmp - 1]=0; lower_ascii(tmp);
      out->emit_comments = (strcmp(tmp,"on")==0 || strcmp(tmp,"true")==0 || strcmp(tmp,"1")==0);
    }
  }
  fclose(f);
}
