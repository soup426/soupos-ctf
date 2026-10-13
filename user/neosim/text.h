/* text.h - neosim's buffer (v0.60.204): an array of lines, each a 256-byte
 * block from a pool (255 characters at most; longer lines are cut at load,
 * and the count is said). */
#pragma once
#define T_MAXL 2048
#define T_COLS 255
typedef struct {
    char *s[T_MAXL];
    int   len[T_MAXL];
    int   n;                 /* lines: always 1 or more */
    int   cut;               /* lines cut at load */
    int   crlf;              /* its lines ended \r\n: written back so (v0.60.285) */
} text_t;
int  text_init(void);                                   /* the pool; 0, or -1 */
void text_clear(text_t *t);                             /* one empty line */
int  text_load(text_t *t, const char *path);            /* 0, -1 no such file (empty), -2 too many lines */
int  text_save(const text_t *t, const char *path, unsigned *bytes);   /* 0, or -1 */
int  text_insert_line(text_t *t, int at);
int  text_room(const text_t *t);                      /* room for a line (v0.60.296) */               /* an empty line at `at`; 0, or -1 full */
void text_delete_line(text_t *t, int at);
int  text_insert_char(text_t *t, int line, int col, char c);          /* 0, or -1 line full */
void text_delete_char(text_t *t, int line, int col);
int  text_split(text_t *t, int line, int col);          /* the rest of the line to a new one below */
void text_join(text_t *t, int line);                    /* line + 1 onto the end of line */
/* (v0.60.231) called after a line is put in (delta 1) or taken out (-1) at
 * 'at'; join_col >= 0 when the line taken out was joined onto the one above
 * at that column. */
extern void (*text_lines_hook)(int at, int delta, int join_col);
