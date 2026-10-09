/* remote.c - the pass: driving soupOS from the network.
 *
 * soupOS already had everything but the carrier. console.c translates incoming
 * bytes into the codes the PS/2 driver produces and mirrors output back, which
 * is how the serial console works; this does the same over TCP. The remote user
 * drives THE SAME SHELL as the local one, exactly as a serial user does, rather
 * than getting a second session - soupOS has one shell, and pretending
 * otherwise would mean a second cell buffer, a second cursor and a second line
 * editor.
 *
 * It runs as a task so the shell keeps running while it pumps. A shell command
 * that sat in a loop reading the socket could not also BE the shell.
 *
 * NO ENCRYPTION AND NO PASSWORD ON THE WIRE. Anyone who can reach the port
 * gets the keyboard, and the login prompt they meet is the ordinary one, typed
 * in clear. This is the local-network convenience that comes before SSH, not a
 * replacement for it, and `pass` says so when it starts.
 */
#include "remote.h"
#include "tcp.h"
#include "net.h"
#include "console.h"
#include "task.h"
#include "vga.h"
#include "klog.h"
#include "shell.h"

static volatile int running;
static volatile int want_stop;
static int           conn = -1;
static uint16_t      port_in_use;
static uint32_t      peer;           /* the caller's address, while connected */

uint32_t remote_peer(void) { return conn >= 0 ? peer : 0; }

/* Output sink: the console hands us every character the screen gets. The
 * expansions match console.c's serial ones, because a terminal on a socket
 * wants exactly what a terminal on a UART wants. */
static void remote_sink(char c) {
    if (conn < 0) return;
    if (c == '\n') {
        tcp_send_on(conn, "\r\n", 2);
    } else if (c == '\b') {
        tcp_send_on(conn, "\b \b", 3);
    } else {
        tcp_send_on(conn, &c, 1);
    }
}

static void remote_task(void *arg) {
    (void)arg;
    task_set_service();
    running = 1;

    while (!want_stop) {
        if (tcp_listen(port_in_use) < 0) break;

        int h = tcp_accept(port_in_use, 100);               /* a second, then re-check stop */
        if (h < 0) continue;

        /* The pass hands over THE console's shell, so it may only hand over
         * a console nobody is logged in at (v0.55.1). Before, a caller got
         * whoever was logged in, the headchef included, with no password:
         * measured on v0.55.0, `whoami` from a stranger said headchef. */
        if (!shell_console_at_login()) {
            static const char busy[] =
                "\r\nThe console is in use. The pass opens only to the login prompt.\r\n";
            tcp_send_on(h, busy, sizeof(busy) - 1);
            tcp_close_on(h);
            klog("[pass] refused a caller: the console is logged in\n");
            continue;
        }
        peer = 0;
        tcp_conn_at(h, 0, &peer, 0, 0);
        conn = h;
        console_set_sink(remote_sink);
        klog("[pass] session open on port %u\n", port_in_use);

        static const char hello[] =
            "\r\nsoupOS over the network. You share the local keyboard.\r\n";
        tcp_send_on(h, hello, sizeof(hello) - 1);
        /* A newline, so the shell redraws its prompt for the new arrival. */
        console_rx_byte('\r');

        while (!want_stop) {
            static uint8_t buf[256];
            int n = tcp_recv_on(h, buf, sizeof(buf), 20);   /* 200 ms */
            if (n < 0) break;                               /* connection gone */
            for (int i = 0; i < n; i++) console_rx_byte(buf[i]);
            if (n == 0 && tcp_peer_done_on(h)) break;       /* they hung up */
        }

        console_set_sink(0);
        conn = -1;
        tcp_close_on(h);
        klog("[pass] session closed\n");
        /* A caller who leaves logged in must not leave the next person at the
         * keyboard their session: clock the console out. Ctrl-C ends any
         * program they left in the foreground; the Enter wakes the shell. */
        if (!shell_console_at_login()) {
            shell_console_hangup();
            console_rx_byte(0x03);
            console_rx_byte('\r');
        }
    }

    console_set_sink(0);
    conn    = -1;
    running = 0;
    tcp_stop_listening(port_in_use);
    klog("[pass] stopped\n");
    task_exit();
}

int remote_start(uint16_t port) {
    if (running) return -1;
    if (!net_ip())  return -2;
    want_stop   = 0;
    port_in_use = port;
    if (!task_spawn("pass", remote_task, 0)) return -3;
    return 0;
}

void remote_stop(void) { want_stop = 1; }
int  remote_running(void) { return running; }
uint16_t remote_port(void) { return port_in_use; }
