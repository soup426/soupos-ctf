/* soupyc - scripting language for soupOS
 *
 * Syntax:
 *   let x = expr              declare variable (always new binding)
 *   x = expr                  assign existing variable
 *   pour expr                 print value + newline
 *   if expr { } else { }      conditional
 *   while expr { }            loop
 *   for v in a..b { }         range loop (a inclusive, b exclusive)
 *   break                     exit innermost loop
 *   let line = input          read a line from keyboard
 *   fn name(p1, p2) { }       define function
 *   return expr               return from function
 *   name(args)                call function (statement or expression)
 *   hash(expr)                AlphaSOUP-32 hash of value (expression)
 *   beep freq [ms]            play PC speaker tone (statement)
 *   ai(prompt)                ask the host LLM via the COM2 bridge; returns
 *                             the reply as a string (capped at 47 chars)
 *   include "file.sc"         parse another script inline: its functions
 *                             register globally, its top-level statements run
 *                             where the include appears (nests up to 4 deep)
 *
 * Built-in functions (expressions):
 *   len(s)  abs(n)  min(a,b)  max(a,b)  rand()  time()
 *   int(x)  str(x)  chr(n)  ord(s)  substr(s,start,n)  upper(s)  lower(s)
 *   contains(s,sub)  find(s,sub)             - substring test / index (-1)
 *   push(arr,x)  pop(arr)  len(arr)          - array operations
 *   sum(arr)  reverse(arr)  sort(arr)        - fold / in-place reorder
 *
 * Arrays (reference type - sharing a value shares the storage):
 *   let a = [1, 2, 3]         array literal ([] is the empty array)
 *   a[0]                      indexing (expression)
 *   a[1] = 9                  indexed assignment
 *   a[i][j] = x               nested indexed assignment (any depth)
 * String indexing: s[i] yields a 1-character string (read-only).
 *
 * File I/O (via the VFS - works on disk files and /dev nodes):
 *   let f = open("LOG.TXT", "w")   open for write ("w"); omit arg = read
 *   write(f, "text")               write string, returns bytes written
 *   read(f, n)                     read up to n bytes, returns a string
 *   eof(f)                         1 once all bytes have been read
 *   close(f)                       flush (write mode) + release handle
 *   open() returns -1 if the file is missing.
 *
 * Types: integer, string (48 chars max), array
 * Operators: + - * / %   == != < > <= >=   && || !
 * String +: concatenation.  String == !=: comparison.
 */

#include "soupyc.h"
#ifndef NO_CHALLENGE
#include "challenge.h"
#endif
#include "vga.h"
#include "term.h"
#include "keyboard.h"
#include "str.h"
#include "alphasoup.h"
#include "speaker.h"
#include "timer.h"
#include "vfs.h"
#include "fat.h"
#include "heap.h"
#include "ai.h"
#include "soupyc_types.h"
#include "task.h"

/* ============================================================
 * Per-invocation context
 * ------------------------------------------------------------
 * Everything the interpreter mutates used to be a file-scope static, which
 * made soupyc safe to call from exactly one task - the shell's. The
 * preemption audit (docs/preemption-audit.md) called that out as the thing
 * standing between soupyc and a background `spawn`, and this is it: one
 * context per running script, hung off the task that is running it, so two
 * interpreters never share a variable table, a node pool or a lexer position.
 *
 * The accessor macros below mean the ~1500 lines of interpreter underneath
 * did not have to change: `pool`, `tok`, `src` and the rest still read as
 * plain names, and each now resolves through the current task.
 *
 * WHAT DELIBERATELY STAYS GLOBAL:
 *   - sc_state (the array pool). Its layout is load-bearing for the CTF
 *     challenge, which reaches after_hook by a fixed distance from an array's
 *     elements, so it cannot move onto the heap. Instead each context records
 *     which slots it allocated (arr_owned) and releases only those, so two
 *     scripts can hold arrays at the same time without clearing each other's.
 *   - rng_state, which is shared entropy and wants to be.
 * ============================================================ */
typedef struct soupyc_ctx {
    /* node pool */
    node_t   m_pool[POOL_SIZE];
    int      m_pool_used;
    /* string store (see the ownership note on val_t) */
    str_blk_t *m_str_head;
    uint32_t   m_str_used;
    /* symbols */
    struct { char name[32]; val_t val; } m_vars[MAX_VARS];
    int      m_nvar;
    func_t   m_funcs[MAX_FUNCS];
    int      m_nfunc;
    /* call stack */
    int      m_scope_stack[MAX_CALL_DEPTH];
    int      m_call_depth;
    val_t    m_call_args[MAX_CALL_DEPTH][MAX_PARAMS];
    /* control flow and errors */
    int      m_err_flag;
    char     m_err_msg[64];
    int      m_break_flag;
    int      m_return_flag;
    val_t    m_return_val;
    int      m_g_err_line;
    int      m_err_line;
    /* lexer */
    const char *m_src;
    int      m_src_pos;
    int      m_cur_line;
    int      m_inc_depth;
    tok_t    m_tok;
    /* open file handles, closed when the script ends */
    vfs_node_t *m_sc_files[MAX_SC_FILES];
    /* which slots of the global array pool belong to this script */
    uint32_t m_arr_owned;
    /* spawned scripts only: index into m_funcs of the function to run */
    int      m_entry_func;
} soupyc_ctx_t;

static inline soupyc_ctx_t *cur_ctx(void) {
    return (soupyc_ctx_t *)task_current()->soupyc;
}

/* The members carry an m_ prefix so that code holding a context pointer
 * explicitly - the clone below, the child entry point - can say ctx->m_x
 * without the bare name expanding into the member position. */
#define pool         (cur_ctx()->m_pool)
#define pool_used    (cur_ctx()->m_pool_used)
#define str_head     (cur_ctx()->m_str_head)
#define str_used     (cur_ctx()->m_str_used)
#define vars         (cur_ctx()->m_vars)
#define nvar         (cur_ctx()->m_nvar)
#define funcs        (cur_ctx()->m_funcs)
#define nfunc        (cur_ctx()->m_nfunc)
#define scope_stack  (cur_ctx()->m_scope_stack)
#define call_depth   (cur_ctx()->m_call_depth)
#define call_args    (cur_ctx()->m_call_args)
#define err_flag     (cur_ctx()->m_err_flag)
#define err_msg      (cur_ctx()->m_err_msg)
#define break_flag   (cur_ctx()->m_break_flag)
#define return_flag  (cur_ctx()->m_return_flag)
#define return_val   (cur_ctx()->m_return_val)
#define g_err_line   (cur_ctx()->m_g_err_line)
#define err_line     (cur_ctx()->m_err_line)
#define src          (cur_ctx()->m_src)
#define src_pos      (cur_ctx()->m_src_pos)
#define cur_line     (cur_ctx()->m_cur_line)
#define inc_depth    (cur_ctx()->m_inc_depth)
#define tok          (cur_ctx()->m_tok)
#define sc_files     (cur_ctx()->m_sc_files)
#define arr_owned    (cur_ctx()->m_arr_owned)


/* ============================================================
 * Tokens
 * ============================================================ */
enum {
    TK_EOF = 0,
    TK_NUM, TK_STR, TK_IDENT,
    /* keywords */
    TK_LET, TK_IF, TK_ELSE, TK_WHILE, TK_POUR, TK_BREAK, TK_INPUT,
    TK_FN, TK_RETURN, TK_BEEP, TK_FOR, TK_IN, TK_INCLUDE,
    /* arithmetic */
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_MOD,
    /* comparison */
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LEQ, TK_GEQ,
    /* logical */
    TK_AND, TK_OR, TK_NOT,
    /* punctuation */
    TK_ASSIGN, TK_LBRACE, TK_RBRACE, TK_LPAREN, TK_RPAREN, TK_COMMA,
    TK_LBRACKET, TK_RBRACKET, TK_DOTDOT,
    TK_ERR
};

/* ============================================================
 * AST node types
 * ============================================================ */
enum {
    N_NUM, N_STR, N_VAR,
    N_UNARY, N_BINOP,
    N_ASSIGN,       /* ival=1 -> let (always new binding), ival=0 -> plain */
    N_IF, N_WHILE,
    N_POUR, N_BREAK, N_INPUT,
    N_FUNC_DEF,     /* ival = index into funcs[] */
    N_CALL,         /* sval = name, left = first arg (args chained via next) */
    N_RETURN,       /* body = return expression (or NULL) */
    N_BEEP,         /* left = freq expr, right = ms expr (or NULL -> 200ms) */
    N_ARRAY,        /* array literal - elements chained off left via next  */
    N_INDEX,        /* left = array/string expr, right = index expr        */
    N_INDEX_SET,    /* left = array expr, right = index, body = value       */
    N_FOR,          /* sval = loop var, left = start, right = end, body     */
    N_INCLUDE       /* body = statement list parsed from an included file   */
};

/* ============================================================
 * Node pool  (static - reset between script runs)
 * ============================================================ */
/* Identifier and token length. This is NOT a limit on string values any
 * more: those are heap-backed (see val_t). */

static node_t *new_node(int type);   /* forward */

/* ============================================================
 * Values
 * ============================================================ */
#define VAL_INT 0
#define VAL_STR 1
#define VAL_ARR 2          /* ival holds an index into arrays[] */

/* A string value is a pointer and a length into the per-run string store
 * below, not a fixed buffer. That 47-character cap was the longest-standing
 * limitation in the tree: it is why a soupyc script could not write an ELF.
 *
 * WHY NOTHING IS FREED UNTIL THE RUN ENDS. val_t is copied by value
 * everywhere - returned from eval_node, stored into variables and array slots,
 * passed as arguments - so a string can be referenced from several places at
 * once with no record of how many. Freeing on overwrite would be a
 * use-after-free whenever a temporary outlived its slot, and a collector
 * cannot help: the temporaries live in C locals, invisible to anything
 * scanning the interpreter's own structures.
 *
 * So the store is an arena for the duration of one script, released in full
 * when the script ends, with a byte budget. A runaway loop that concatenates
 * forever gets a clean "out of string memory" error instead of starving the
 * kernel heap that Doom's zone allocator needs. Literals are interned once at
 * parse time and referenced, not copied, so a loop over a constant costs
 * nothing. */

#define STR_BUDGET (2u * 1024u * 1024u)

static const char str_empty[] = "";

static void set_err(const char *m);     /* forward */
static int  soupyc_spawn(int func_index);  /* forward: see the spawn section */

/* A writable, NUL-terminated buffer of `len` bytes in the store. */
static char *str_alloc_in(soupyc_ctx_t *c, uint32_t len) {
    if (c->m_str_used + len + 1 > STR_BUDGET) return 0;
    str_blk_t *b = (str_blk_t *)kmalloc(sizeof(str_blk_t) + len + 1);
    if (!b) return 0;
    b->next = c->m_str_head; c->m_str_head = b;
    c->m_str_used += len + 1;
    b->data[len] = '\0';
    return b->data;
}

static char *str_alloc(uint32_t len) {
    char *b = str_alloc_in(cur_ctx(), len);
    if (!b) set_err("out of string memory");
    return b;
}

static void str_store_reset_in(soupyc_ctx_t *c) {
    str_blk_t *b = c->m_str_head;
    while (b) { str_blk_t *n = b->next; kfree(b); b = n; }
    c->m_str_head = 0;
    c->m_str_used = 0;
}
static void str_store_reset(void) { str_store_reset_in(cur_ctx()); }

static val_t mkival(int v)      { val_t r; r.type=VAL_INT; r.ival=v; r.sval=str_empty; r.slen=0; return r; }
static val_t mkaval(int handle) { val_t r; r.type=VAL_ARR; r.ival=handle; r.sval=str_empty; r.slen=0; return r; }

/* Reference a string already in the store (or a static one): no copy. */
static val_t mksval_ref(const char *s, uint32_t len) {
    val_t r; r.type=VAL_STR; r.ival=0; r.sval=s?s:str_empty; r.slen=len; return r;
}
/* Copy bytes into the store. */
static val_t mksvaln(const char *s, uint32_t len) {
    char *b = str_alloc(len);
    if (!b) return mkival(0);
    memcpy(b, s, len);
    return mksval_ref(b, len);
}
static val_t mksval(const char *s) { return mksvaln(s, (uint32_t)strlen(s)); }

/* ============================================================
 * Arrays
 * ------------------------------------------------------------
 * Arrays are reference types: a val_t of type VAL_ARR carries a
 * handle (index into arrays[]), so copying the value - assignment,
 * passing to a function - shares the same underlying storage. The
 * pool is fixed and reset between script runs; arrays are never
 * freed mid-run, which is fine for soupyc's bounded programs.
 * ============================================================ */

/* Interpreter state that sits immediately below the array pool.
 *
 * after_hook is called once a script finishes, if set. Nothing in the tree
 * assigns it: reaching it is challenge stage 4. Laying it out in one struct
 * with the pool makes the distance from an array's elements to the hook a
 * fixed number rather than a link-order accident.
 *
 * guard exists so a val_t written just below the pool lands its `type` field
 * here and its `ival` field exactly on after_hook. */
static struct sc_state_s {
    uint32_t  guard;                /* +0   */
    void    (*after_hook)(void);    /* +4   */
    uint8_t   pad[40];              /* +8   */
    arr_t     arr_slots[MAX_ARRAYS];/* +48  */
} sc_state;

/* Stage 4 of the challenge reaches after_hook from an array's elements by a
 * fixed distance. Freeze the two offsets that distance is made of, so an edit
 * to this struct fails to build instead of quietly breaking the challenge.
 * (A negative array size rather than _Static_assert: this is gnu99.) */
typedef char sc_state_layout_is_frozen[
    (__builtin_offsetof(struct sc_state_s, after_hook) == 4 &&
     __builtin_offsetof(struct sc_state_s, arr_slots)  == 48) ? 1 : -1];

#define arrays sc_state.arr_slots

/* Reserve an array slot. Returns a handle, or -1 if the pool is full. */
static int arr_alloc(void) {
    for (int i = 0; i < MAX_ARRAYS; i++) {
        if (!arrays[i].used) {
            arr_owned |= (1u << i);   /* released when THIS script ends */
            arrays[i].used = 1;
            arrays[i].len  = 0;
            return i;
        }
    }
    return -1;
}

/* Validate a handle carried by a VAL_ARR value. */
static int arr_valid(int h) {
    return h >= 0 && h < MAX_ARRAYS && arrays[h].used;
}

/* ============================================================
 * Symbol table
 * ============================================================ */

/* Search backwards - most recent binding wins (inner scope first) */
static val_t *var_find(const char *n) {
    for (int i = nvar - 1; i >= 0; i--)
        if (strcmp(vars[i].name, n) == 0) return &vars[i].val;
    return (void *)0;
}

/* Always create a new binding at the current top of the var stack */
static int var_bind(const char *n, val_t v) {
    if (nvar >= MAX_VARS) return -1;
    strncpy(vars[nvar].name, n, 31); vars[nvar].name[31] = '\0';
    vars[nvar].val = v;
    nvar++;
    return 0;
}

/* Update most recent binding for n, or append if not found */
static int var_set(const char *n, val_t v) {
    val_t *p = var_find(n);
    if (p) { *p = v; return 0; }
    return var_bind(n, v);
}

/* ============================================================
 * Function table
 * ============================================================ */

/* ============================================================
 * Call stack (for scope restoration and arg buffering)
 * ============================================================ */
/* Pre-allocated argument slots - one row per call depth level.
 * Keeping these static removes ~448 bytes from every eval_node() stack frame,
 * which is the main cause of stack overflow during deep recursion. */

/* ============================================================
 * Runtime state
 * ============================================================ */

/* Best-known source line for the next error. The lexer keeps it pointed at
 * the current token during parsing; eval_node points it at the running node
 * during execution. set_err() snapshots it into err_line. */

static void set_err(const char *msg) {
    if (!err_flag) {
        strncpy(err_msg, msg, 63);
        err_msg[63] = '\0';
        err_line = g_err_line;
        err_flag = 1;
    }
}

/* ============================================================
 * Lexer
 * ============================================================ */

/* include nesting guard - bounds recursion and the kmalloc'd buffer stack */


static int is_digit(char c) { return c>='0' && c<='9'; }
static int is_alpha(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static int is_alnum(char c) { return is_alpha(c)||is_digit(c); }

static void skip_ws(void) {
    for (;;) {
        char c = src[src_pos];
        if (c==' '||c=='\t'||c=='\r') { src_pos++; continue; }
        if (c=='\n') { src_pos++; cur_line++; continue; }
        if (c=='#') { while (src[src_pos] && src[src_pos]!='\n') src_pos++; continue; }
        break;
    }
}

static void next_tok(void) {
    skip_ws();
    tok.ival = 0; tok.sval[0] = '\0'; tok.str = str_empty; tok.slen = 0;
    tok.line = cur_line;          /* line where this token starts */
    g_err_line = cur_line;        /* keep error line current during parsing */
    char c = src[src_pos];

    if (!c) { tok.type = TK_EOF; return; }

    /* Integer */
    if (is_digit(c)) {
        tok.type = TK_NUM;
        while (is_digit(src[src_pos]))
            tok.ival = tok.ival * 10 + (src[src_pos++] - '0');
        return;
    }

    /* String literal. A triple quote (""") opens a multi-line string: it runs
     * verbatim (newlines kept, no \-escapes) until the closing """. A single
     * quote is the usual one-line form with \n and \t escapes. Neither has a
     * length limit: the literal is interned into the string store. */
    if (c == '"') {
        tok.type = TK_STR;
        int triple = (src[src_pos+1]=='"' && src[src_pos+2]=='"');
        src_pos += triple ? 3 : 1;
        /* Measure the raw span first so the exact size can be allocated.
         * Escapes only ever shrink the result, so this is a safe bound. */
        uint32_t span = 0;
        for (int p = src_pos;; p++) {
            char d = src[p];
            if (!d) break;
            if (triple) { if (d=='"' && src[p+1]=='"' && src[p+2]=='"') break; }
            else        { if (d=='"' || d=='\n') break; }
            span++;
        }
        char *buf = str_alloc(span);
        if (!buf) return;                                  /* err already set */

        uint32_t i = 0;
        for (;;) {
            char d = src[src_pos];
            if (!d) break;                                 /* unterminated */
            if (triple) {
                if (d=='"' && src[src_pos+1]=='"' && src[src_pos+2]=='"')
                    { src_pos += 3; break; }
                if (d=='\n') cur_line++;
                buf[i++] = d;
                src_pos++;
            } else {
                if (d=='"') { src_pos++; break; }
                if (d=='\n') break;                        /* no newline in "" */
                char out;
                if (d=='\\' && src[src_pos+1]=='n') { out='\n'; src_pos+=2; }
                else if (d=='\\' && src[src_pos+1]=='t') { out='\t'; src_pos+=2; }
                else { out=d; src_pos++; }
                buf[i++] = out;
            }
        }
        buf[i] = '\0';
        tok.str  = buf;
        tok.slen = i;
        return;
    }

    /* Identifiers / keywords */
    if (is_alpha(c)) {
        int i = 0;
        while (is_alnum(src[src_pos]) && i < SVAL_LEN-1)
            tok.sval[i++] = src[src_pos++];
        tok.sval[i] = '\0';
        if (!strcmp(tok.sval,"let"))    { tok.type=TK_LET;    return; }
        if (!strcmp(tok.sval,"if"))     { tok.type=TK_IF;     return; }
        if (!strcmp(tok.sval,"else"))   { tok.type=TK_ELSE;   return; }
        if (!strcmp(tok.sval,"while"))  { tok.type=TK_WHILE;  return; }
        if (!strcmp(tok.sval,"pour"))   { tok.type=TK_POUR;   return; }
        if (!strcmp(tok.sval,"break"))  { tok.type=TK_BREAK;  return; }
        if (!strcmp(tok.sval,"input"))  { tok.type=TK_INPUT;  return; }
        if (!strcmp(tok.sval,"fn"))     { tok.type=TK_FN;     return; }
        if (!strcmp(tok.sval,"return")) { tok.type=TK_RETURN; return; }
        if (!strcmp(tok.sval,"beep"))   { tok.type=TK_BEEP;   return; }
        if (!strcmp(tok.sval,"for"))    { tok.type=TK_FOR;    return; }
        if (!strcmp(tok.sval,"in"))     { tok.type=TK_IN;     return; }
        if (!strcmp(tok.sval,"include")){ tok.type=TK_INCLUDE;return; }
        tok.type = TK_IDENT; return;
    }

    src_pos++;
    switch (c) {
        case '+': tok.type=TK_PLUS;   return;
        case '-': tok.type=TK_MINUS;  return;
        case '*': tok.type=TK_STAR;   return;
        case '/': tok.type=TK_SLASH;  return;
        case '%': tok.type=TK_MOD;    return;
        case '(': tok.type=TK_LPAREN; return;
        case ')': tok.type=TK_RPAREN; return;
        case '{': tok.type=TK_LBRACE;   return;
        case '}': tok.type=TK_RBRACE;   return;
        case '[': tok.type=TK_LBRACKET; return;
        case ']': tok.type=TK_RBRACKET; return;
        case ',': tok.type=TK_COMMA;    return;
        case '.':
            if (src[src_pos]=='.') { src_pos++; tok.type=TK_DOTDOT; return; }
            break;
        case '=':
            if (src[src_pos]=='=') { src_pos++; tok.type=TK_EQ;  return; }
            tok.type=TK_ASSIGN; return;
        case '!':
            if (src[src_pos]=='=') { src_pos++; tok.type=TK_NEQ; return; }
            tok.type=TK_NOT; return;
        case '<':
            if (src[src_pos]=='=') { src_pos++; tok.type=TK_LEQ; return; }
            tok.type=TK_LT; return;
        case '>':
            if (src[src_pos]=='=') { src_pos++; tok.type=TK_GEQ; return; }
            tok.type=TK_GT; return;
        case '&':
            if (src[src_pos]=='&') { src_pos++; tok.type=TK_AND; return; }
            break;
        case '|':
            if (src[src_pos]=='|') { src_pos++; tok.type=TK_OR;  return; }
            break;
    }
    tok.type = TK_ERR;
}

static void consume(void) { next_tok(); }

static int expect(int type, const char *what) {
    if (tok.type != type) { set_err(what); return 0; }
    consume(); return 1;
}

/* ============================================================
 * Node allocation
 * ============================================================ */
static node_t *new_node(int type) {
    if (pool_used >= POOL_SIZE) { set_err("script too large"); return (void *)0; }
    node_t *n = &pool[pool_used++];
    memset(n, 0, sizeof *n);
    n->type = type;
    n->line = tok.line;        /* line of the token current at allocation */
    return n;
}

/* ============================================================
 * Parser
 * ============================================================ */
static node_t *parse_expr(void);
static node_t *parse_stmt(void);

static node_t *parse_block(void) {
    if (!expect(TK_LBRACE, "expected '{'")) return (void *)0;
    node_t head; memset(&head, 0, sizeof head);
    node_t *tail = &head;
    while (tok.type != TK_RBRACE && tok.type != TK_EOF && !err_flag) {
        node_t *s = parse_stmt();
        if (!s || err_flag) return (void *)0;
        tail->next = s; tail = s;
    }
    if (!expect(TK_RBRACE, "expected '}'")) return (void *)0;
    return head.next;
}

/* Parse a comma-separated argument list, returns first arg (chained via next) */
static node_t *parse_args(void) {
    if (tok.type == TK_RPAREN) return (void *)0;
    node_t *first = parse_expr(); if (!first || err_flag) return (void *)0;
    node_t *last = first;
    while (tok.type == TK_COMMA) {
        consume();
        node_t *arg = parse_expr(); if (!arg || err_flag) return (void *)0;
        last->next = arg; last = arg;
    }
    return first;
}

/* parse_atom - a single primary term, without trailing [] indexing. */
static node_t *parse_atom(void) {
    if (tok.type == TK_NUM) {
        node_t *n = new_node(N_NUM); if (!n) return (void *)0;
        n->ival = tok.ival; consume(); return n;
    }
    /* Array literal: [] or [e1, e2, ...] */
    if (tok.type == TK_LBRACKET) {
        consume();
        node_t *n = new_node(N_ARRAY); if (!n) return (void *)0;
        if (tok.type != TK_RBRACKET) {
            node_t *first = parse_expr(); if (!first || err_flag) return (void *)0;
            n->left = first;
            node_t *last = first;
            while (tok.type == TK_COMMA) {
                consume();
                node_t *e = parse_expr(); if (!e || err_flag) return (void *)0;
                last->next = e; last = e;
            }
        }
        if (!expect(TK_RBRACKET, "expected ']'")) return (void *)0;
        return n;
    }
    if (tok.type == TK_STR) {
        node_t *n = new_node(N_STR); if (!n) return (void *)0;
        n->lit = tok.str; n->litlen = tok.slen; consume(); return n;
    }
    if (tok.type == TK_IDENT) {
        char name[SVAL_LEN];
        strncpy(name, tok.sval, SVAL_LEN-1); name[SVAL_LEN-1] = '\0';
        int id_line = tok.line;          /* line of the identifier itself */
        consume();
        /* Function call in expression context: name(args) */
        if (tok.type == TK_LPAREN) {
            consume();
            node_t *n = new_node(N_CALL); if (!n) return (void *)0;
            n->line = id_line;
            strncpy(n->sval, name, SVAL_LEN-1);
            n->left = parse_args(); if (err_flag) return (void *)0;
            if (!expect(TK_RPAREN, "expected ')'")) return (void *)0;
            return n;
        }
        /* Variable reference */
        node_t *n = new_node(N_VAR); if (!n) return (void *)0;
        n->line = id_line;
        strncpy(n->sval, name, SVAL_LEN-1); return n;
    }
    if (tok.type == TK_INPUT) {
        consume(); return new_node(N_INPUT);
    }
    if (tok.type == TK_LPAREN) {
        consume();
        node_t *e = parse_expr(); if (err_flag) return (void *)0;
        if (!expect(TK_RPAREN, "expected ')'")) return (void *)0;
        return e;
    }
    set_err("expected expression");
    return (void *)0;
}

/* parse_primary - an atom followed by zero or more [] index operations,
 * so `arr[i]`, `arr[i][j]` and `f()[k]` all parse. */
static node_t *parse_primary(void) {
    node_t *base = parse_atom();
    if (!base || err_flag) return base;
    while (tok.type == TK_LBRACKET) {
        consume();
        node_t *idx = parse_expr(); if (!idx || err_flag) return (void *)0;
        if (!expect(TK_RBRACKET, "expected ']'")) return (void *)0;
        node_t *ix = new_node(N_INDEX); if (!ix) return (void *)0;
        ix->left = base; ix->right = idx; ix->line = base->line;
        base = ix;
    }
    return base;
}

static node_t *parse_unary(void) {
    if (tok.type == TK_MINUS || tok.type == TK_NOT) {
        int op = tok.type; consume();
        node_t *n = new_node(N_UNARY); if (!n) return (void *)0;
        n->op   = op;
        n->body = parse_unary(); if (err_flag) return (void *)0;
        return n;
    }
    return parse_primary();
}

static node_t *parse_mul(void) {
    node_t *l = parse_unary(); if (!l || err_flag) return l;
    while (tok.type==TK_STAR || tok.type==TK_SLASH || tok.type==TK_MOD) {
        int op = tok.type; consume();
        node_t *r = parse_unary(); if (!r || err_flag) return (void *)0;
        node_t *n = new_node(N_BINOP); if (!n) return (void *)0;
        n->op=op; n->left=l; n->right=r; n->line=l->line; l=n;
    }
    return l;
}

static node_t *parse_add(void) {
    node_t *l = parse_mul(); if (!l || err_flag) return l;
    while (tok.type==TK_PLUS || tok.type==TK_MINUS) {
        int op = tok.type; consume();
        node_t *r = parse_mul(); if (!r || err_flag) return (void *)0;
        node_t *n = new_node(N_BINOP); if (!n) return (void *)0;
        n->op=op; n->left=l; n->right=r; n->line=l->line; l=n;
    }
    return l;
}

static node_t *parse_cmp(void) {
    node_t *l = parse_add(); if (!l || err_flag) return l;
    int op = tok.type;
    if (op==TK_EQ||op==TK_NEQ||op==TK_LT||op==TK_GT||op==TK_LEQ||op==TK_GEQ) {
        consume();
        node_t *r = parse_add(); if (!r || err_flag) return (void *)0;
        node_t *n = new_node(N_BINOP); if (!n) return (void *)0;
        n->op=op; n->left=l; n->right=r; n->line=l->line; return n;
    }
    return l;
}

static node_t *parse_logical(void) {
    node_t *l = parse_cmp(); if (!l || err_flag) return l;
    while (tok.type==TK_AND || tok.type==TK_OR) {
        int op = tok.type; consume();
        node_t *r = parse_cmp(); if (!r || err_flag) return (void *)0;
        node_t *n = new_node(N_BINOP); if (!n) return (void *)0;
        n->op=op; n->left=l; n->right=r; n->line=l->line; l=n;
    }
    return l;
}

static node_t *parse_expr(void) { return parse_logical(); }

static node_t *parse_stmt(void) {
    /* include "file.sc" - parse another script's statements inline. Function
     * definitions in the included file register into the global table; its
     * top-level statements run where the include appears. */
    if (tok.type == TK_INCLUDE) {
        consume();
        if (tok.type != TK_STR) { set_err("expected \"filename\" after include"); return (void *)0; }
        char fname[SVAL_LEN];
        strncpy(fname, tok.sval, SVAL_LEN-1); fname[SVAL_LEN-1] = '\0';
        consume();                    /* advance the outer stream past the name */

        if (inc_depth >= MAX_INCLUDE_DEPTH) { set_err("include nested too deep"); return (void *)0; }

        vfs_node_t *nd = vfs_open(fname, VFS_RDONLY);
        if (!nd) { set_err("include: file not found"); return (void *)0; }
        uint32_t sz = vfs_size(nd);
        char *buf = (char *)kmalloc(sz + 1);
        if (!buf) { vfs_close(nd); set_err("include: out of memory"); return (void *)0; }
        int got = vfs_read(nd, buf, sz);
        vfs_close(nd);
        if (got < 0) got = 0;
        buf[got] = '\0';

        /* Save the outer lexer state, including the already-lexed next token. */
        const char *s_src = src; int s_pos = src_pos, s_line = cur_line;
        tok_t s_tok = tok;

        src = buf; src_pos = 0; cur_line = 1; inc_depth++;
        next_tok();
        node_t head; memset(&head, 0, sizeof head);
        node_t *tail = &head;
        while (tok.type != TK_EOF && !err_flag) {
            node_t *st = parse_stmt();
            if (!st || err_flag) break;
            tail->next = st; tail = st;
        }
        inc_depth--;

        /* Restore the outer stream. The AST copied every string/name it needs
         * into the node pool, so the include buffer can be freed now. */
        src = s_src; src_pos = s_pos; cur_line = s_line; tok = s_tok;
        kfree(buf);
        if (err_flag) return (void *)0;

        node_t *n = new_node(N_INCLUDE); if (!n) return (void *)0;
        n->body = head.next;
        return n;
    }

    /* fn name(params) { body } */
    if (tok.type == TK_FN) {
        consume();
        if (tok.type != TK_IDENT) { set_err("expected function name"); return (void *)0; }
        if (nfunc >= MAX_FUNCS)    { set_err("too many functions");    return (void *)0; }
        func_t *f = &funcs[nfunc];
        strncpy(f->name, tok.sval, 31); f->name[31] = '\0';
        consume();
        if (!expect(TK_LPAREN, "expected '('")) return (void *)0;
        f->nparam = 0;
        while (tok.type != TK_RPAREN && tok.type != TK_EOF && !err_flag) {
            if (tok.type != TK_IDENT) { set_err("expected parameter name"); return (void *)0; }
            if (f->nparam >= MAX_PARAMS) { set_err("too many parameters"); return (void *)0; }
            strncpy(f->params[f->nparam], tok.sval, 31);
            f->params[f->nparam][31] = '\0';
            f->nparam++;
            consume();
            if (tok.type == TK_COMMA) consume();
        }
        if (!expect(TK_RPAREN, "expected ')'")) return (void *)0;
        f->body = parse_block(); if (err_flag) return (void *)0;
        nfunc++;
        node_t *n = new_node(N_FUNC_DEF); if (!n) return (void *)0;
        n->ival = nfunc - 1;
        return n;
    }

    /* return [expr] */
    if (tok.type == TK_RETURN) {
        consume();
        node_t *n = new_node(N_RETURN); if (!n) return (void *)0;
        /* Only parse expr if we're not at end-of-block */
        if (tok.type != TK_RBRACE && tok.type != TK_EOF) {
            n->body = parse_expr(); if (err_flag) return (void *)0;
        }
        return n;
    }

    /* beep freq [ms] */
    if (tok.type == TK_BEEP) {
        consume();
        node_t *n = new_node(N_BEEP); if (!n) return (void *)0;
        n->left = parse_expr(); if (err_flag) return (void *)0;
        /* Optional ms argument - present if next token starts an expression */
        if (tok.type==TK_NUM || tok.type==TK_IDENT || tok.type==TK_LPAREN ||
            tok.type==TK_MINUS) {
            n->right = parse_expr(); if (err_flag) return (void *)0;
        }
        return n;
    }

    /* let x = expr  |  x = expr  |  name(args) */
    if (tok.type == TK_LET || tok.type == TK_IDENT) {
        int is_let = (tok.type == TK_LET);
        if (is_let) consume();
        if (tok.type != TK_IDENT) { set_err("expected identifier"); return (void *)0; }
        char name[SVAL_LEN];
        strncpy(name, tok.sval, SVAL_LEN-1); name[SVAL_LEN-1] = '\0';
        consume();

        /* Function call as statement: name(args) */
        if (!is_let && tok.type == TK_LPAREN) {
            consume();
            node_t *n = new_node(N_CALL); if (!n) return (void *)0;
            strncpy(n->sval, name, SVAL_LEN-1);
            n->left = parse_args(); if (err_flag) return (void *)0;
            if (!expect(TK_RPAREN, "expected ')'")) return (void *)0;
            return n;
        }

        /* Indexed assignment: name[i] = expr, name[i][j] = expr, ...
         * (not valid with `let`). Every bracket but the last folds into the
         * base via N_INDEX; since arrays are reference types, indexing the
         * base yields the inner array to mutate. */
        if (!is_let && tok.type == TK_LBRACKET) {
            node_t *base = new_node(N_VAR); if (!base) return (void *)0;
            strncpy(base->sval, name, SVAL_LEN-1); base->sval[SVAL_LEN-1] = '\0';
            consume();
            node_t *idx = parse_expr(); if (!idx || err_flag) return (void *)0;
            if (!expect(TK_RBRACKET, "expected ']'")) return (void *)0;
            while (tok.type == TK_LBRACKET) {
                node_t *ix = new_node(N_INDEX); if (!ix) return (void *)0;
                ix->left = base; ix->right = idx; base = ix;
                consume();
                idx = parse_expr(); if (!idx || err_flag) return (void *)0;
                if (!expect(TK_RBRACKET, "expected ']'")) return (void *)0;
            }
            if (!expect(TK_ASSIGN, "expected '='")) return (void *)0;
            node_t *val = parse_expr(); if (!val || err_flag) return (void *)0;
            node_t *n = new_node(N_INDEX_SET); if (!n) return (void *)0;
            n->left = base; n->right = idx; n->body = val;
            return n;
        }

        /* Assignment */
        if (!expect(TK_ASSIGN, "expected '='")) return (void *)0;
        node_t *e = parse_expr(); if (!e || err_flag) return (void *)0;
        node_t *n = new_node(N_ASSIGN); if (!n) return (void *)0;
        strncpy(n->sval, name, SVAL_LEN-1);
        n->ival = is_let;   /* 1 = always new binding, 0 = update or bind */
        n->body = e;
        return n;
    }

    /* pour */
    if (tok.type == TK_POUR) {
        consume();
        node_t *e = parse_expr(); if (!e || err_flag) return (void *)0;
        node_t *n = new_node(N_POUR); if (!n) return (void *)0;
        n->body = e; return n;
    }

    /* if */
    if (tok.type == TK_IF) {
        consume();
        node_t *cond = parse_expr(); if (!cond || err_flag) return (void *)0;
        node_t *then_ = parse_block(); if (err_flag) return (void *)0;
        node_t *else_ = (void *)0;
        if (tok.type == TK_ELSE) {
            consume(); else_ = parse_block(); if (err_flag) return (void *)0;
        }
        node_t *n = new_node(N_IF); if (!n) return (void *)0;
        n->left=cond; n->body=then_; n->else_=else_; return n;
    }

    /* while */
    if (tok.type == TK_WHILE) {
        consume();
        node_t *cond = parse_expr(); if (!cond || err_flag) return (void *)0;
        node_t *body = parse_block(); if (err_flag) return (void *)0;
        node_t *n = new_node(N_WHILE); if (!n) return (void *)0;
        n->left=cond; n->body=body; return n;
    }

    /* for <var> in <start>..<end> { body }  - end is exclusive */
    if (tok.type == TK_FOR) {
        consume();
        if (tok.type != TK_IDENT) { set_err("expected loop variable"); return (void *)0; }
        char vname[SVAL_LEN];
        strncpy(vname, tok.sval, SVAL_LEN-1); vname[SVAL_LEN-1] = '\0';
        consume();
        if (!expect(TK_IN, "expected 'in'")) return (void *)0;
        node_t *start = parse_expr(); if (!start || err_flag) return (void *)0;
        if (!expect(TK_DOTDOT, "expected '..'")) return (void *)0;
        node_t *end = parse_expr(); if (!end || err_flag) return (void *)0;
        node_t *body = parse_block(); if (err_flag) return (void *)0;
        node_t *n = new_node(N_FOR); if (!n) return (void *)0;
        strncpy(n->sval, vname, SVAL_LEN-1); n->sval[SVAL_LEN-1] = '\0';
        n->left = start; n->right = end; n->body = body;
        return n;
    }

    /* break */
    if (tok.type == TK_BREAK) {
        consume(); return new_node(N_BREAK);
    }

    set_err("expected statement");
    return (void *)0;
}

/* ============================================================
 * Evaluator helpers
 * ============================================================ */
static val_t eval_node(node_t *n);

/* Built-in functions (len, abs, rand, ...). Defined after eval_node
 * since they evaluate their own arguments. Returns 1 and writes *out
 * when `n` names a builtin; returns 0 to fall through to user funcs. */
static int call_builtin(node_t *n, val_t *out);

/* Print a value - recurses into arrays as [e1, e2, ...]. */
static void print_val(val_t v) {
    if (v.type == VAL_STR) { term_puts(term_current(), v.sval); return; }
    if (v.type == VAL_ARR) {
        if (!arr_valid(v.ival)) { term_puts(term_current(), "[?]"); return; }
        arr_t *a = &arrays[v.ival];
        term_putc(term_current(), '[');
        for (int i = 0; i < a->len; i++) {
            if (i) term_puts(term_current(), ", ");
            print_val(a->elems[i]);
        }
        term_putc(term_current(), ']');
        return;
    }
    term_printf(term_current(), "%d", v.ival);
}

static void int_to_str(int v, char *buf, int cap) {
    if (cap <= 1) { if (cap==1) buf[0]='\0'; return; }
    if (v == 0) { buf[0]='0'; buf[1]='\0'; return; }
    int neg = (v < 0);
    if (neg && v == (int)0x80000000u) { strncpy(buf,"-2147483648",cap); buf[cap-1]='\0'; return; }
    unsigned uv = neg ? (unsigned)(-v) : (unsigned)v;
    char tmp[12]; int len = 0;
    while (uv) { tmp[len++]='0'+(int)(uv%10); uv/=10; }
    int i=0;
    if (neg && i < cap-1) buf[i++]='-';
    for (int j=len-1; j>=0 && i<cap-1; j--) buf[i++]=tmp[j];
    buf[i]='\0';
}

static int as_int(val_t v) {
    if (v.type == VAL_INT) return v.ival;
    const char *s = v.sval;
    int i = 0;
    while (s[i]==' ' || s[i]=='\t') i++;
    int neg = 0;
    if      (s[i]=='-') { neg = 1; i++; }
    else if (s[i]=='+') {           i++; }
    int r = 0;
    while (s[i]>='0' && s[i]<='9') r = r*10 + (s[i++]-'0');
    return neg ? -r : r;
}

static val_t eval_block(node_t *n) {
    val_t last = mkival(0);
    while (n && !err_flag && !break_flag && !return_flag)
        { last = eval_node(n); n = n->next; }
    return last;
}

/* ============================================================
 * Evaluator
 * ============================================================ */
static val_t eval_node(node_t *n) {
    if (!n || err_flag) return mkival(0);
    if (n->line) g_err_line = n->line;   /* locate runtime errors */

    switch (n->type) {

        case N_NUM: return mkival(n->ival);
        /* Already in the store from parse time: reference it, do not copy,
         * so a literal inside a loop costs nothing. */
        case N_STR: return mksval_ref(n->lit, n->litlen);

        case N_VAR: {
            val_t *v = var_find(n->sval);
            if (!v) { set_err("undefined variable"); return mkival(0); }
            return *v;
        }

        case N_UNARY: {
            val_t v = eval_node(n->body);
            if (n->op == TK_MINUS) return mkival(-as_int(v));
            if (n->op == TK_NOT)   return mkival(!as_int(v));
            return mkival(0);
        }

        case N_BINOP: {
            val_t l = eval_node(n->left);

            /* Short-circuit && and ||: skip RHS eval when outcome is known.
             * Matters for guards like `if x != 0 && 100/x > 1`. */
            if (n->op == TK_AND && !as_int(l)) return mkival(0);
            if (n->op == TK_OR  &&  as_int(l)) return mkival(1);

            val_t r = eval_node(n->right);

            /* String concatenation */
            if (n->op == TK_PLUS && (l.type==VAL_STR || r.type==VAL_STR)) {
                char ln[24], rn[24];
                const char *ls = l.sval; uint32_t ll = l.slen;
                const char *rs = r.sval; uint32_t rl = r.slen;
                if (l.type != VAL_STR) { int_to_str(l.ival, ln, sizeof ln); ls = ln; ll = (uint32_t)strlen(ln); }
                if (r.type != VAL_STR) { int_to_str(r.ival, rn, sizeof rn); rs = rn; rl = (uint32_t)strlen(rn); }

                char *b = str_alloc(ll + rl);
                if (!b) return mkival(0);
                memcpy(b, ls, ll);
                memcpy(b + ll, rs, rl);
                return mksval_ref(b, ll + rl);
            }
            /* String equality */
            if ((n->op==TK_EQ||n->op==TK_NEQ) && l.type==VAL_STR && r.type==VAL_STR) {
                int eq = (l.slen == r.slen) && !memcmp(l.sval, r.sval, l.slen);
                return mkival(n->op==TK_EQ ? eq : !eq);
            }

            int li = as_int(l), ri = as_int(r);
            switch (n->op) {
                case TK_PLUS:  return mkival(li + ri);
                case TK_MINUS: return mkival(li - ri);
                case TK_STAR:  return mkival(li * ri);
                case TK_SLASH: return mkival(ri ? li / ri : 0);
                case TK_MOD:   return mkival(ri ? li % ri : 0);
                case TK_EQ:    return mkival(li == ri);
                case TK_NEQ:   return mkival(li != ri);
                case TK_LT:    return mkival(li <  ri);
                case TK_GT:    return mkival(li >  ri);
                case TK_LEQ:   return mkival(li <= ri);
                case TK_GEQ:   return mkival(li >= ri);
                case TK_AND:   return mkival(li && ri);
                case TK_OR:    return mkival(li || ri);
            }
            return mkival(0);
        }

        case N_ASSIGN: {
            val_t v = eval_node(n->body);
            if (n->ival) {
                /* let - always create a new binding */
                if (var_bind(n->sval, v) < 0) set_err("too many variables");
            } else {
                /* plain assignment - update most recent or create */
                if (var_set(n->sval, v) < 0) set_err("too many variables");
            }
            return v;
        }

        case N_IF: {
            val_t c = eval_node(n->left);
            if (as_int(c)) eval_block(n->body);
            else if (n->else_) eval_block(n->else_);
            return mkival(0);
        }

        case N_WHILE: {
            /* Bindings the body makes are per-iteration, exactly as in N_FOR
             * below. Without this a `let` inside the body accumulated a new
             * binding every pass and the loop died at MAX_VARS with "too many
             * variables" - at 64 iterations, which is not many. */
            int saved_nvar = nvar;
            while (!err_flag && !return_flag) {
                val_t c = eval_node(n->left);
                if (!as_int(c)) break;
                eval_block(n->body);
                if (break_flag) { break_flag = 0; break; }
                nvar = saved_nvar;
            }
            nvar = saved_nvar;
            return mkival(0);
        }

        case N_FOR: {
            val_t sv = eval_node(n->left);
            val_t ev = eval_node(n->right);
            if (err_flag) return mkival(0);
            int start = as_int(sv), end = as_int(ev);
            int saved_nvar = nvar;
            if (var_bind(n->sval, mkival(start)) < 0) {
                set_err("too many variables"); return mkival(0);
            }
            int slot = nvar - 1;            /* loop-variable slot */
            for (int i = start; i < end && !err_flag && !return_flag; i++) {
                vars[slot].val = mkival(i);
                eval_block(n->body);
                if (break_flag) { break_flag = 0; break; }
                nvar = slot + 1;            /* drop lets the body bound */
            }
            nvar = saved_nvar;              /* drop the loop variable */
            return mkival(0);
        }

        case N_POUR: {
            val_t v = eval_node(n->body);
            if (err_flag) return v;
            print_val(v);
            term_putc(term_current(), '\n');
            return v;
        }

        case N_BREAK:
            break_flag = 1;
            return mkival(0);

        case N_INPUT: {
            #define INPUT_MAX 1024u
            static char ibuf[INPUT_MAX + 1];
            uint32_t i = 0; int c;
            /* -1 is a terminal that has gone away (an SSH session hung up):
             * end the line rather than spin on it. */
            while ((c = term_current()->getc(term_current())) != '\n' && c >= 0) {
                if (c == '\b') {
                    if (i > 0) { i--; term_putc(term_current(), '\b'); }
                } else if (c >= ' ' && c < 127 && i < INPUT_MAX) {
                    ibuf[i++] = (char)c;
                    term_putc(term_current(), (char)c);
                }
            }
            ibuf[i] = '\0';
            term_putc(term_current(), '\n');
            return mksvaln(ibuf, i);
        }

        case N_FUNC_DEF:
            /* Already registered during parse phase - nothing to do at runtime */
            return mkival(0);

        case N_INCLUDE:
            /* Run the included file's top-level statements in place. */
            eval_block(n->body);
            return mkival(0);

        case N_CALL: {
            /* Built-in functions (hash, len, abs, ...) take precedence
             * over user-defined functions of the same name. */
            val_t bres;
            if (call_builtin(n, &bres))
                return err_flag ? mkival(0) : bres;

            /* Look up user-defined function */
            func_t *f = (void *)0;
            for (int i = 0; i < nfunc; i++) {
                if (strcmp(funcs[i].name, n->sval) == 0) { f = &funcs[i]; break; }
            }
            if (!f) { set_err("undefined function"); return mkival(0); }

            /* Check depth before using the pre-allocated args slot */
            if (call_depth >= MAX_CALL_DEPTH) { set_err("call stack overflow"); return mkival(0); }

            /* Evaluate arguments into the static slot for this depth level.
             * Must happen BEFORE incrementing call_depth (the slot belongs to
             * the level we're about to enter, but args are evaluated in the
             * caller's context). */
            val_t *args = call_args[call_depth];
            int narg = 0;
            node_t *a = n->left;
            while (a && narg < MAX_PARAMS && !err_flag) {
                args[narg++] = eval_node(a);
                a = a->next;
            }
            if (err_flag) return mkival(0);
            if (narg != f->nparam) { set_err("wrong argument count"); return mkival(0); }

            /* Push scope */
            scope_stack[call_depth++] = nvar;

            /* Bind parameters as new local bindings */
            for (int i = 0; i < f->nparam; i++) {
                if (var_bind(f->params[i], args[i]) < 0) {
                    nvar = scope_stack[--call_depth];
                    set_err("too many variables");
                    return mkival(0);
                }
            }

            /* Execute function body */
            eval_block(f->body);

            /* Collect return value */
            val_t ret = return_flag ? return_val : mkival(0);
            return_flag = 0;

            /* Pop scope - restores all locals including parameters */
            nvar = scope_stack[--call_depth];

            return ret;
        }

        case N_RETURN: {
            return_val  = n->body ? eval_node(n->body) : mkival(0);
            return_flag = 1;
            return return_val;
        }

        case N_BEEP: {
            val_t fv = eval_node(n->left);
            uint32_t freq = (uint32_t)as_int(fv);
            uint32_t ms   = 200;   /* default 200 ms */
            if (n->right) {
                val_t mv = eval_node(n->right);
                ms = (uint32_t)as_int(mv);
            }
            speaker_beep(freq, ms);
            return mkival(0);
        }

        case N_ARRAY: {
            int h = arr_alloc();
            if (h < 0) { set_err("too many arrays"); return mkival(0); }
            for (node_t *e = n->left; e && !err_flag; e = e->next) {
                if (arrays[h].len >= ARR_CAP) {
                    set_err("array literal too large"); return mkival(0);
                }
                val_t ev = eval_node(e);
                if (err_flag) return mkival(0);
                arrays[h].elems[arrays[h].len++] = ev;
            }
            return mkaval(h);
        }

        case N_INDEX: {
            val_t base = eval_node(n->left);
            val_t idx  = eval_node(n->right);
            if (err_flag) return mkival(0);
            int i = as_int(idx);
            /* String index -> 1-character string. */
            if (base.type == VAL_STR) {
                int slen = (int)base.slen;
                if (i < 0 || i >= slen) {
                    set_err("string index out of range"); return mkival(0);
                }
                char b[2]; b[0] = base.sval[i]; b[1] = '\0';
                return mksval(b);
            }
            if (base.type == VAL_ARR && arr_valid(base.ival)) {
                arr_t *a = &arrays[base.ival];
                if (i < 0 || i >= a->len) {
                    set_err("array index out of range"); return mkival(0);
                }
                return a->elems[i];
            }
            set_err("indexing a non-array/string");
            return mkival(0);
        }

        case N_INDEX_SET: {
            val_t base = eval_node(n->left);
            val_t idx  = eval_node(n->right);
            val_t v    = eval_node(n->body);
            if (err_flag) return mkival(0);
            if (base.type != VAL_ARR || !arr_valid(base.ival)) {
                set_err("indexing a non-array"); return mkival(0);
            }
            arr_t *a = &arrays[base.ival];
            int i = as_int(idx);
#ifdef NO_CHALLENGE
            if (i < 0 || i >= a->len) {
#else
            /* CHALLENGE=1: upper bound only, so a negative index writes below
             * the pool. soupyc runs in ring 0, so this is a kernel write from
             * a script. Stage 4 of the CTF chain. */
            if (i >= a->len) {
#endif
                set_err("array index out of range"); return mkival(0);
            }
            a->elems[i] = v;
            return v;
        }
    }
    return mkival(0);
}

/* ============================================================
 * Built-in functions
 * ============================================================ */

/* LCG PRNG for rand(); lazily seeded from the PIT tick count. */
static uint32_t rng_state;
static int soupyc_rand(void) {
    if (rng_state == 0) rng_state = timer_get_ticks() | 1u;
    rng_state = rng_state * 1103515245u + 12345u;
    return (int)((rng_state >> 16) & 0x7FFF);   /* 0..32767 */
}

/* Evaluate a call's argument list into out[], up to `max` args.
 * Returns the count evaluated. Unused slots are left as integer 0. */
static int eval_args(node_t *call, val_t *out, int max) {
    for (int i = 0; i < max; i++) out[i] = mkival(0);
    int k = 0;
    node_t *a = call->left;
    while (a && k < max && !err_flag) {
        out[k++] = eval_node(a);
        a = a->next;
    }
    return k;
}

static int count_args(node_t *call) {
    int k = 0;
    for (node_t *a = call->left; a; a = a->next) k++;
    return k;
}

/* Textual form of a value - returns v->sval, or writes the integer into
 * scratch and returns that. */
static const char *val_str(const val_t *v, char *scratch, int cap) {
    if (v->type == VAL_STR) return v->sval;
    int_to_str(v->ival, scratch, cap);
    return scratch;
}

/* ---- file I/O handle table (soupyc handle int -> VFS node) ----
 * open() returns a small integer handle; the table is closed and
 * cleared between script runs so a script can't leak VFS nodes. */

static void sc_files_reset(void) {
    for (int i = 0; i < MAX_SC_FILES; i++) {
        if (sc_files[i]) { vfs_close(sc_files[i]); sc_files[i] = 0; }
    }
}

/* Resolve a soupyc handle value to an open VFS node, or NULL. */
static vfs_node_t *sc_file_get(val_t h) {
    int i = h.ival;
    if (h.type != VAL_INT || i < 0 || i >= MAX_SC_FILES) return 0;
    return sc_files[i];
}

static int call_builtin(node_t *n, val_t *out) {
    const char *name = n->sval;
    val_t a[3];
    char  scratch[SVAL_LEN];
    *out = mkival(0);

    if (strcmp(name, "hash") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        const char *s = val_str(&a[0], scratch, SVAL_LEN);
        *out = mkival((int)alphasoup_hash(s, (uint32_t)strlen(s)));
        return 1;
    }
    if (strcmp(name, "len") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        if (a[0].type == VAL_ARR) {
            if (!arr_valid(a[0].ival)) { set_err("len: bad array"); return 1; }
            *out = mkival(arrays[a[0].ival].len);
            return 1;
        }
        const char *s = val_str(&a[0], scratch, SVAL_LEN);
        *out = mkival((int)strlen(s));
        return 1;
    }
    if (strcmp(name, "push") == 0) {
        if (count_args(n) != 2) { set_err("push expects 2 args"); return 1; }
        eval_args(n, a, 2);
        if (err_flag) return 1;
        if (a[0].type != VAL_ARR || !arr_valid(a[0].ival)) {
            set_err("push: first arg is not an array"); return 1;
        }
        arr_t *arr = &arrays[a[0].ival];
        if (arr->len >= ARR_CAP) { set_err("push: array is full"); return 1; }
        arr->elems[arr->len++] = a[1];
        *out = a[1];
        return 1;
    }
    if (strcmp(name, "pop") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        if (a[0].type != VAL_ARR || !arr_valid(a[0].ival)) {
            set_err("pop: arg is not an array"); return 1;
        }
        arr_t *arr = &arrays[a[0].ival];
        if (arr->len == 0) { set_err("pop: array is empty"); return 1; }
        *out = arr->elems[--arr->len];
        return 1;
    }
    if (strcmp(name, "abs") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        int v = as_int(a[0]);
        *out = mkival(v < 0 ? -v : v);
        return 1;
    }
    if (strcmp(name, "min") == 0 || strcmp(name, "max") == 0) {
        if (count_args(n) != 2) { set_err("min/max expects 2 args"); return 1; }
        eval_args(n, a, 2);
        if (err_flag) return 1;
        int x = as_int(a[0]), y = as_int(a[1]);
        int want_max = (name[1] == 'a');   /* "max" vs "min" */
        *out = mkival(want_max ? (x > y ? x : y) : (x < y ? x : y));
        return 1;
    }
    if (strcmp(name, "rand") == 0) {
        *out = mkival(soupyc_rand());
        return 1;
    }
    if (strcmp(name, "time") == 0) {
        *out = mkival((int)timer_get_ticks());
        return 1;
    }
    if (strcmp(name, "int") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        *out = mkival(as_int(a[0]));
        return 1;
    }
    if (strcmp(name, "str") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        if (a[0].type == VAL_STR) { *out = a[0]; }
        else { int_to_str(a[0].ival, scratch, SVAL_LEN); *out = mksval(scratch); }
        return 1;
    }
    if (strcmp(name, "chr") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        char b[2];
        b[0] = (char)(as_int(a[0]) & 0xFF);
        b[1] = '\0';
        *out = mksval(b);
        return 1;
    }
    if (strcmp(name, "ord") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        const char *s = val_str(&a[0], scratch, SVAL_LEN);
        *out = mkival((int)(unsigned char)s[0]);
        return 1;
    }
    if (strcmp(name, "upper") == 0 || strcmp(name, "lower") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        const char *s  = val_str(&a[0], scratch, SVAL_LEN);
        int    want_up = (name[0] == 'u');
        uint32_t len = (uint32_t)strlen(s);
        char *b = str_alloc(len);
        if (!b) return 1;
        for (uint32_t i = 0; i < len; i++) {
            char c = s[i];
            if ( want_up && c >= 'a' && c <= 'z') c -= 32;
            if (!want_up && c >= 'A' && c <= 'Z') c += 32;
            b[i] = c;
        }
        *out = mksval_ref(b, len);
        return 1;
    }
    if (strcmp(name, "substr") == 0) {
        if (count_args(n) != 3) { set_err("substr expects 3 args"); return 1; }
        eval_args(n, a, 3);
        if (err_flag) return 1;
        const char *s     = val_str(&a[0], scratch, SVAL_LEN);
        int         slen  = (int)strlen(s);
        int         start = as_int(a[1]);
        int         cnt   = as_int(a[2]);
        if (start < 0)            start = 0;
        if (start > slen)         start = slen;
        if (cnt < 0)              cnt = 0;
        if (start + cnt > slen)   cnt = slen - start;
        char *b = str_alloc((uint32_t)cnt);
        if (!b) return 1;
        memcpy(b, s + start, (uint32_t)cnt);
        *out = mksval_ref(b, (uint32_t)cnt);
        return 1;
    }

    if (strcmp(name, "sum") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        if (a[0].type != VAL_ARR || !arr_valid(a[0].ival)) { set_err("sum: not an array"); return 1; }
        arr_t *arr = &arrays[a[0].ival];
        int s = 0;
        for (int i = 0; i < arr->len; i++) s += as_int(arr->elems[i]);
        *out = mkival(s);
        return 1;
    }
    if (strcmp(name, "reverse") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        if (a[0].type != VAL_ARR || !arr_valid(a[0].ival)) { set_err("reverse: not an array"); return 1; }
        arr_t *arr = &arrays[a[0].ival];
        for (int i = 0, j = arr->len - 1; i < j; i++, j--) {
            val_t t = arr->elems[i]; arr->elems[i] = arr->elems[j]; arr->elems[j] = t;
        }
        *out = a[0];   /* return the (reference) array */
        return 1;
    }
    if (strcmp(name, "sort") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        if (a[0].type != VAL_ARR || !arr_valid(a[0].ival)) { set_err("sort: not an array"); return 1; }
        arr_t *arr = &arrays[a[0].ival];
        for (int i = 1; i < arr->len; i++) {        /* insertion sort, ascending by int value */
            val_t key = arr->elems[i]; int ki = as_int(key); int j = i - 1;
            while (j >= 0 && as_int(arr->elems[j]) > ki) { arr->elems[j + 1] = arr->elems[j]; j--; }
            arr->elems[j + 1] = key;
        }
        *out = a[0];
        return 1;
    }
    if (strcmp(name, "contains") == 0 || strcmp(name, "find") == 0) {
        if (count_args(n) != 2) { set_err("contains/find expects 2 args"); return 1; }
        eval_args(n, a, 2);
        if (err_flag) return 1;
        char sc2[SVAL_LEN];
        const char *hay = val_str(&a[0], scratch, SVAL_LEN);
        const char *nee = val_str(&a[1], sc2,     SVAL_LEN);
        int hl = (int)strlen(hay), nl = (int)strlen(nee), at = -1;
        if (nl == 0) at = 0;
        else for (int i = 0; i + nl <= hl; i++) {
            int k = 0; while (k < nl && hay[i + k] == nee[k]) k++;
            if (k == nl) { at = i; break; }
        }
        *out = mkival(name[0] == 'f' ? at : (at >= 0));   /* find->index, contains->bool */
        return 1;
    }

    /* remove(path) - delete a file. The language could create files but not
     * get rid of them, which made a script that fills the disk impossible to
     * clean up after. Returns 1 on success, 0 on failure. */
    if (strcmp(name, "remove") == 0) {
        if (count_args(n) != 1) { set_err("remove expects 1 arg"); return 1; }
        eval_args(n, a, 1);
        if (err_flag) return 1;
        const char *path = val_str(&a[0], scratch, SVAL_LEN);
        *out = mkival(fat_delete(path) >= 0 ? 1 : 0);
        return 1;
    }

    /* sleep(ms) - yield the CPU. A spawned script that spins would still work,
     * since the scheduler preempts, but it would burn the machine to do it. */
    if (strcmp(name, "sleep") == 0) {
        if (count_args(n) != 1) { set_err("sleep expects 1 arg"); return 1; }
        eval_args(n, a, 1);
        if (err_flag) return 1;
        int ms = as_int(a[0]);
        if (ms < 0)     ms = 0;
        if (ms > 10000) ms = 10000;   /* a typo should not park a task for an hour */
        task_sleep((uint32_t)ms);
        *out = mkival(0);
        return 1;
    }

    /* spawn("worker") - run a no-argument function as a background task.
     * Returns its task id, which `ps` lists and `kill` accepts. */
    if (strcmp(name, "spawn") == 0) {
        if (count_args(n) != 1) { set_err("spawn expects 1 arg"); return 1; }
        eval_args(n, a, 1);
        if (err_flag) return 1;
        const char *fname = val_str(&a[0], scratch, SVAL_LEN);
        int idx = -1;
        for (int i = 0; i < nfunc; i++)
            if (strcmp(funcs[i].name, fname) == 0) { idx = i; break; }
        if (idx < 0)                      { set_err("spawn: no such function"); return 1; }
        if (funcs[idx].nparam != 0)       { set_err("spawn: function must take no arguments"); return 1; }
        int id = soupyc_spawn(idx);
        if (id < 0)                       { set_err("spawn: could not start a task"); return 1; }
        *out = mkival(id);
        return 1;
    }

    if (strcmp(name, "ai") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        const char *p = val_str(&a[0], scratch, SVAL_LEN);
        if (!ai_available()) { *out = mksval("(ai bridge off)"); return 1; }
        ai_send(p);
        /* The store cannot resize a block, so take a line's worth and report
         * the length actually received. */
        #define AI_REPLY_MAX 1024u
        char *buf = str_alloc(AI_REPLY_MAX);
        if (!buf) return 1;
        uint32_t i = 0; int got = 0;
        for (;;) {
            int c = ai_getc(got ? 600 : 1500);
            if (c < 0 || c == AI_EOT) break;
            if (c == '\r' || c == '\n') continue;       /* keep it a single line */
            if (i < AI_REPLY_MAX) buf[i++] = (char)c;
            got = 1;
        }
        buf[i] = '\0';
        *out = mksval_ref(buf, i);
        return 1;
    }

    /* ---- file I/O (backed by the VFS) ---- */

    /* lines(path) - the file as an array of strings, one per line, without
     * the newlines (a trailing \r goes too, so a file written on a PC reads
     * the same). A missing file is an empty array. More lines than an array
     * holds is an error rather than a silent cut. */
    if (strcmp(name, "lines") == 0) {
        if (count_args(n) != 1) { set_err("lines expects 1 arg"); return 1; }
        eval_args(n, a, 1);
        if (err_flag) return 1;
        const char *path = val_str(&a[0], scratch, SVAL_LEN);
        int h = arr_alloc();
        if (h < 0) { set_err("lines: no free array"); return 1; }
        vfs_node_t *nd = vfs_open(path, VFS_RDONLY);
        if (!nd) { *out = mkaval(h); return 1; }
        uint32_t size = vfs_size(nd);
        char *buf = str_alloc(size);
        if (!buf) { vfs_close(nd); return 1; }
        uint32_t got = 0;
        while (got < size) {
            int r = vfs_read(nd, buf + got, size - got);
            if (r <= 0) break;
            got += (uint32_t)r;
        }
        vfs_close(nd);
        arr_t *arr = &arrays[h];
        uint32_t i = 0;
        while (i < got) {
            uint32_t j = i;
            while (j < got && buf[j] != '\n') j++;
            uint32_t len = j - i;
            if (len && buf[i + len - 1] == '\r') len--;
            if (arr->len >= ARR_CAP) { set_err("lines: file has more lines than an array holds"); return 1; }
            char *line = str_alloc(len);
            if (!line) return 1;
            memcpy(line, buf + i, len);
            line[len] = '\0';
            arr->elems[arr->len++] = mksval_ref(line, len);
            i = j + 1;
        }
        *out = mkaval(h);
        return 1;
    }

    /* write_lines(path, array) - the array as a file, a newline after every
     * element, replacing whatever was there. Returns the number of lines
     * written, or -1 if the file could not be opened. */
    if (strcmp(name, "write_lines") == 0) {
        if (count_args(n) != 2) { set_err("write_lines expects 2 args"); return 1; }
        eval_args(n, a, 2);
        if (err_flag) return 1;
        const char *path = val_str(&a[0], scratch, SVAL_LEN);
        if (a[1].type != VAL_ARR || !arr_valid(a[1].ival)) { set_err("write_lines: second arg is not an array"); return 1; }
        vfs_node_t *nd = vfs_open(path, VFS_WRONLY | VFS_CREATE);
        if (!nd) { *out = mkival(-1); return 1; }
        arr_t *arr = &arrays[a[1].ival];
        int written = 0;
        for (int k = 0; k < arr->len; k++) {
            const char *line = val_str(&arr->elems[k], scratch, SVAL_LEN);
            uint32_t len = (uint32_t)strlen(line);
            if (len && vfs_write(nd, line, len) != (int)len) break;
            if (vfs_write(nd, "\n", 1) != 1) break;
            written++;
        }
        vfs_close(nd);
        *out = mkival(written);
        return 1;
    }

    if (strcmp(name, "open") == 0) {
        int nargs = count_args(n);
        if (nargs < 1 || nargs > 2) { set_err("open expects 1 or 2 args"); return 1; }
        eval_args(n, a, 2);
        if (err_flag) return 1;
        if (a[0].type != VAL_STR) { set_err("open: filename must be a string"); return 1; }
        int flags = VFS_RDONLY;
        if (nargs == 2 && a[1].type == VAL_STR &&
            (a[1].sval[0] == 'w' || a[1].sval[0] == 'W'))
            flags = VFS_WRONLY | VFS_CREATE;
        int slot = -1;
        for (int i = 0; i < MAX_SC_FILES; i++)
            if (!sc_files[i]) { slot = i; break; }
        if (slot < 0) { set_err("open: too many open files"); return 1; }
        vfs_node_t *nd = vfs_open(a[0].sval, flags);
        if (!nd) { *out = mkival(-1); return 1; }   /* missing file -> -1 */
        sc_files[slot] = nd;
        *out = mkival(slot);
        return 1;
    }
    if (strcmp(name, "read") == 0) {
        if (count_args(n) != 2) { set_err("read expects 2 args"); return 1; }
        eval_args(n, a, 2);
        if (err_flag) return 1;
        vfs_node_t *nd = sc_file_get(a[0]);
        if (!nd) { set_err("read: bad file handle"); return 1; }
        int want = as_int(a[1]);
        if (want < 0) want = 0;
        char *buf = str_alloc((uint32_t)want);
        if (!buf) return 1;
        int got = vfs_read(nd, buf, (uint32_t)want);
        if (got < 0) got = 0;
        buf[got] = '\0';
        *out = mksval_ref(buf, (uint32_t)got);
        return 1;
    }
    if (strcmp(name, "write") == 0) {
        if (count_args(n) != 2) { set_err("write expects 2 args"); return 1; }
        eval_args(n, a, 2);
        if (err_flag) return 1;
        vfs_node_t *nd = sc_file_get(a[0]);
        if (!nd) { set_err("write: bad file handle"); return 1; }
        const char *s = val_str(&a[1], scratch, SVAL_LEN);
        int wr = vfs_write(nd, s, (uint32_t)strlen(s));
        if (wr < 0) wr = 0;
        *out = mkival(wr);
        return 1;
    }
    if (strcmp(name, "close") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        int i = a[0].ival;
        if (a[0].type != VAL_INT || i < 0 || i >= MAX_SC_FILES || !sc_files[i]) {
            set_err("close: bad file handle"); return 1;
        }
        vfs_close(sc_files[i]);
        sc_files[i] = 0;
        *out = mkival(0);
        return 1;
    }
    if (strcmp(name, "eof") == 0) {
        eval_args(n, a, 1);
        if (err_flag) return 1;
        vfs_node_t *nd = sc_file_get(a[0]);
        if (!nd) { set_err("eof: bad file handle"); return 1; }
        *out = mkival(vfs_eof(nd));
        return 1;
    }

    return 0;   /* not a builtin - fall through to user functions */
}

/* ============================================================
 * Public API
 * ============================================================ */

static int soupyc_exec(const char *source) {
    /* Reset all state */
    pool_used   = 0;
    nvar        = 0;
    nfunc       = 0;
    call_depth  = 0;
    inc_depth   = 0;
    err_flag    = 0;
    break_flag  = 0;
    return_flag = 0;
    err_msg[0]  = '\0';
    err_line    = 0;
    g_err_line  = 0;
    /* Drop the previous run's strings before anything can reference them.
     * Every val_t still sitting in vars[], arrays[] or return_val points into
     * that storage, which is why this happens here, with all of them reset in
     * the same breath. */
    str_store_reset();
    return_val  = mkival(0);
    /* No blanket clear of the array pool here. The pool is shared (its layout
     * is fixed for the challenge), so wiping it would pull the rug from under
     * a script running on another task. Each script releases its own slots
     * when it finishes - see soupyc_run below. */

    src      = source;
    src_pos  = 0;
    cur_line = 1;

    /* Lex first token */
    next_tok();

    /* Parse top-level statement list */
    node_t head; memset(&head, 0, sizeof head);
    node_t *tail = &head;
    while (tok.type != TK_EOF && !err_flag) {
        node_t *s = parse_stmt();
        if (!s || err_flag) break;
        tail->next = s;
        tail = s;
    }

    if (err_flag) {
        term_color(term_current(), VGA_LIGHT_RED, VGA_BLACK);
        term_printf(term_current(), "soupyc: parse error (line %d): %s\n", err_line, err_msg);
        term_color(term_current(), VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }

    /* Execute */
    eval_block(head.next);

    if (err_flag) {
        term_color(term_current(), VGA_LIGHT_RED, VGA_BLACK);
        term_printf(term_current(), "soupyc: runtime error (line %d): %s\n", err_line, err_msg);
        term_color(term_current(), VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }

    /* Post-run hook. Never set by anything in the tree. */
#ifndef NO_CHALLENGE
    if (sc_state.after_hook) sc_state.after_hook();
#endif

    return 0;
}

/* ============================================================
 * spawn: a script function as a background task
 * ------------------------------------------------------------
 * The child gets a context of its own, and - this is the whole design - an
 * INDEPENDENT COPY of the code it needs. Borrowing the parent's node pool
 * would have been cheaper, but then the child could not outlive the `soup`
 * command that spawned it: soupyc_run frees the parent context on the way
 * out, and the child would be walking freed nodes. Copying means the shell
 * gets its prompt back while the child keeps running, which is the point.
 *
 * Node pointers all point inside the pool, which is one contiguous array, so
 * copying the pool and adding a fixed delta to every link relocates the whole
 * tree. String literals are the exception - they live in the string store, not
 * the pool - so each one is re-interned into the child's own store.
 * ============================================================ */
static void clone_code(soupyc_ctx_t *dst, soupyc_ctx_t *src_ctx) {
    int used = src_ctx->m_pool_used;
    memcpy(dst->m_pool, src_ctx->m_pool, (uint32_t)used * sizeof(node_t));
    dst->m_pool_used = used;

    long delta = (char *)dst->m_pool - (char *)src_ctx->m_pool;
    for (int i = 0; i < used; i++) {
        node_t *nd = &dst->m_pool[i];
        if (nd->next)  nd->next  = (node_t *)((char *)nd->next  + delta);
        if (nd->left)  nd->left  = (node_t *)((char *)nd->left  + delta);
        if (nd->right) nd->right = (node_t *)((char *)nd->right + delta);
        if (nd->body)  nd->body  = (node_t *)((char *)nd->body  + delta);
        if (nd->else_) nd->else_ = (node_t *)((char *)nd->else_ + delta);
        if (nd->lit) {
            char *b = str_alloc_in(dst, nd->litlen);
            if (b) { memcpy(b, nd->lit, nd->litlen); nd->lit = b; }
            else   { nd->lit = str_empty; nd->litlen = 0; }
        }
    }

    memcpy(dst->m_funcs, src_ctx->m_funcs, sizeof dst->m_funcs);
    dst->m_nfunc = src_ctx->m_nfunc;
    for (int i = 0; i < dst->m_nfunc; i++)
        if (dst->m_funcs[i].body)
            dst->m_funcs[i].body = (node_t *)((char *)dst->m_funcs[i].body + delta);
}

static void release_ctx(soupyc_ctx_t *c) {
    str_store_reset_in(c);
    for (int i = 0; i < MAX_SC_FILES; i++)
        if (c->m_sc_files[i]) { vfs_close(c->m_sc_files[i]); c->m_sc_files[i] = 0; }
    for (int i = 0; i < MAX_ARRAYS; i++)
        if (c->m_arr_owned & (1u << i)) arrays[i].used = 0;
}

static void soupyc_child(void *arg) {
    soupyc_ctx_t *ctx = (soupyc_ctx_t *)arg;
    task_current()->soupyc = ctx;

    eval_block(funcs[ctx->m_entry_func].body);

    if (err_flag) {
        term_color(term_current(), VGA_LIGHT_RED, VGA_BLACK);
        term_printf(term_current(), "\nsoupyc: spawned %s failed (line %d): %s\n",
                   funcs[ctx->m_entry_func].name, err_line, err_msg);
        term_color(term_current(), VGA_LIGHT_GREY, VGA_BLACK);
    }

    release_ctx(ctx);
    task_current()->soupyc = 0;
    kfree(ctx);
    task_exit();
}

/* Returns the new task id, or -1. */
static int soupyc_spawn(int func_index) {
    soupyc_ctx_t *parent = cur_ctx();
    soupyc_ctx_t *child  = (soupyc_ctx_t *)kmalloc(sizeof *child);
    if (!child) return -1;
    memset(child, 0, sizeof *child);

    clone_code(child, parent);
    child->m_entry_func = func_index;

    char tname[16];
    tname[0] = 's'; tname[1] = 'o'; tname[2] = 'u'; tname[3] = 'p'; tname[4] = ':';
    int k = 5;
    const char *fn = parent->m_funcs[func_index].name;
    for (int i = 0; fn[i] && k < 15; i++) tname[k++] = fn[i];
    tname[k] = '\0';

    task_t *t = task_spawn(tname, soupyc_child, child);
    if (!t) { release_ctx(child); kfree(child); return -1; }
    return (int)t->id;
}

/* ============================================================
 * Entry point
 * ------------------------------------------------------------
 * One context per running script, allocated here and hung off the task that
 * is interpreting, which is what lets two scripts run at once. The previous
 * value is saved and restored so a nested call (a script run from inside
 * another, should that ever exist) does not lose its parent's state.
 * ============================================================ */
int soupyc_run(const char *source) {
    task_t *t = task_current();
    soupyc_ctx_t *prev = (soupyc_ctx_t *)t->soupyc;

    soupyc_ctx_t *ctx = (soupyc_ctx_t *)kmalloc(sizeof *ctx);
    if (!ctx) {
        term_color(term_current(), VGA_LIGHT_RED, VGA_BLACK);
        term_puts(term_current(), "soupyc: not enough memory for an interpreter context\n");
        term_color(term_current(), VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }
    memset(ctx, 0, sizeof *ctx);
    t->soupyc = ctx;

    int rc = soupyc_exec(source);

    /* Everything this script owns goes back now: its strings, its open files,
     * and only the array slots it allocated itself. */
    str_store_reset();
    sc_files_reset();
    /* Through the accessor, not ctx->, because the names below are macros
     * that resolve through the current task - which is still this context. */
    for (int i = 0; i < MAX_ARRAYS; i++)
        if (arr_owned & (1u << i)) arrays[i].used = 0;

    t->soupyc = prev;
    kfree(ctx);
    return rc;
}
