#pragma once

void shell_run(void);

/* A shell of its own on terminal `t`, logged in as `uid`, on a task of its
 * own (v0.36.0). With exec_line it runs that one line with no prompt and
 * ends; otherwise it is interactive until the terminal hangs up or the cook
 * clocks out. Returns a handle, or NULL. */
struct term;
void *shell_session_start(struct term *t, int uid, const char *exec_line, const char *where);
/* For the pass (v0.55.1): is the console at its login prompt, and clock the
 * console out at its next keystroke (a pass caller left logged in). */
int  shell_console_at_login(void);
void shell_console_hangup(void);
/* 1 once the session has ended, with its last command's status. */
int   shell_session_done(void *s, int *status);
/* The terminal has gone: kill every program the session started. */
void  shell_session_hangup(void *s);
void  shell_session_free(void *s);
