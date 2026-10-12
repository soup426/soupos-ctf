/* highlight.c - neosim's syntax colours (v0.60.217). */
#include "highlight.h"

static const char *const sc_words[] = { "let", "if", "else", "while", "pour", "break", "input", "fn", "return",
                                        "beep", "for", "in", "include", 0 };   /* soupyc.c's own */
static const char *const c_words[] = { "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register",
    "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
    "void", "volatile", "while", 0 };

static int ends(const char *p, const char *e) {
    int a = 0, b = 0;
    while (p[a]) a++;
    while (e[b]) b++;
    if (a < b) return 0;
    for (int i = 0; i < b; i++) { char c = p[a - b + i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != e[i]) return 0; }
    return 1;
}
int hl_kind(const char *path) {
    if (ends(path, ".sc")) return KIND_SOUPYC;
    if (ends(path, ".c") || ends(path, ".h")) return KIND_C;
    return KIND_TEXT;
}
static int word_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static int is_word(const char *const *words, const char *s, int n) {
    for (int w = 0; words[w]; w++) {
        int k = 0;
        while (k < n && words[w][k] == s[k]) k++;
        if (k == n && !words[w][k]) return 1;
    }
    return 0;
}
void hl_line(int kind, const char *s, int n, int *state, unsigned char *cls) {
    for (int i = 0; i < n; i++) cls[i] = HL_PLAIN;
    if (kind == KIND_TEXT) return;
    int i = 0;
    if (kind == KIND_C) {                          /* a # line, whole */
        int f = 0; while (f < n && s[f] == ' ') f++;
        if (!*state && f < n && s[f] == '#') { for (int k = f; k < n; k++) cls[k] = HL_PREPROC; return; }
    }
    while (i < n) {
        if (*state == 1) {                         /* inside a C comment, to its end */
            cls[i] = HL_COMMENT;
            if (s[i] == '*' && i + 1 < n && s[i + 1] == '/') { cls[i + 1] = HL_COMMENT; i += 2; *state = 0; } else i++;
            continue;
        }
        if (*state == 2) {                         /* inside a soupyc """ string */
            cls[i] = HL_STRING;
            if (s[i] == '"' && i + 2 < n && s[i + 1] == '"' && s[i + 2] == '"') { cls[i + 1] = cls[i + 2] = HL_STRING; i += 3; *state = 0; } else i++;
            continue;
        }
        char c = s[i];
        if (kind == KIND_SOUPYC && c == '#') { for (; i < n; i++) cls[i] = HL_COMMENT; break; }
        if (kind == KIND_C && c == '/' && i + 1 < n && s[i + 1] == '/') { for (; i < n; i++) cls[i] = HL_COMMENT; break; }
        if (kind == KIND_C && c == '/' && i + 1 < n && s[i + 1] == '*') { cls[i] = cls[i + 1] = HL_COMMENT; i += 2; *state = 1; continue; }
        if (kind == KIND_SOUPYC && c == '"' && i + 2 < n && s[i + 1] == '"' && s[i + 2] == '"') { cls[i] = cls[i + 1] = cls[i + 2] = HL_STRING; i += 3; *state = 2; continue; }
        if (c == '"' || (kind == KIND_C && c == '\'')) {   /* a string (or a C character), backslashes kept in it */
            char q = c;
            cls[i++] = HL_STRING;
            while (i < n && s[i] != q) { if (s[i] == '\\' && i + 1 < n) cls[i++] = HL_STRING; cls[i++] = HL_STRING; }
            if (i < n) cls[i++] = HL_STRING;
            continue;
        }
        if (c >= '0' && c <= '9' && (i == 0 || !word_char(s[i - 1]))) {
            while (i < n && (word_char(s[i]) || s[i] == '.')) cls[i++] = HL_NUMBER;
            continue;
        }
        if (word_char(c)) {
            int j = i; while (j < n && word_char(s[j])) j++;
            if (is_word(kind == KIND_C ? c_words : sc_words, s + i, j - i)) for (int k = i; k < j; k++) cls[k] = HL_KEYWORD;
            i = j;
            continue;
        }
        i++;
    }
}
