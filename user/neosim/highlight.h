/* highlight.h - neosim's syntax colours (v0.60.217): a class for each
 * character of a line, by the file's kind; one piece of state carried from
 * line to line (inside a C comment, or a soupyc triple-quoted string). */
#pragma once
enum { HL_PLAIN, HL_KEYWORD, HL_STRING, HL_NUMBER, HL_COMMENT, HL_PREPROC };
enum { KIND_TEXT, KIND_SOUPYC, KIND_C };
int  hl_kind(const char *path);                                  /* by the name's ending */
/* Classes for s[0..n) into cls; *state is carried in and out (0 none). */
void hl_line(int kind, const char *s, int n, int *state, unsigned char *cls);
