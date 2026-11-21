/* NetTerm firmware: the main loop.
 *
 * Mode Serial (milestone 1): the serial port is the host. Bytes in go to the
 * terminal; keys go out, and to the terminal too with Local Echo on. The
 * menu's settings (interact.json) are read every frame and applied when they
 * change: the port, the baud rate, the echo, what Enter and Backspace send.
 */
#include "hw.h"
#include "nt.h"

static const uint32_t bauds[] = {300, 1200, 2400, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
#define NBAUD (sizeof bauds / sizeof bauds[0])

static uint32_t cur_lo = 0xFFFFFFFF, cur_hi = 0xFFFFFFFF;
static int echo;
static uint32_t n_tx, n_rx;             /* bytes sent / received, for the status line */

/* Local echo shows what was typed as a typewriter would, not the raw bytes:
   Enter (CR, CR LF or LF) as a new line, Backspace (DEL or BS) as erasing the
   character before the cursor. Escape sequences (the arrows) go to the
   terminal as they are, so they move the cursor. */
static void echo_byte(uint8_t c)
{
    static uint8_t last;
    if (c == 13) { term_putc(13); term_putc(10); }
    else if (c == 10) { if (last != 13) { term_putc(13); term_putc(10); } }
    else if (c == 127 || c == 8) { term_putc(8); term_putc(' '); term_putc(8); }
    else term_putc(c);
    last = c;
}

void host_send(const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        while (IO_USTAT & 2) ;          /* TX full: wait (64 bytes drain in 5.6 ms at 115,200) */
        IO_UDATA = (uint8_t)s[i];
        n_tx++;
        if (echo) echo_byte((uint8_t)s[i]);
    }
}

void host_sends(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    host_send(s, n);
}

static void utoa10(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) *b++ = t[--n];
    *b = 0;
}

void status_draw(void)
{
    uint8_t a = ATTR(15, 1);
    char b[16];
    int port = setting(SET_PORT), inuse = (IO_UCTL >> 23) & 1, bi = setting(SET_BAUD);
    fill_row(STATUS_ROW, 0, COLS, ' ', a);
    text(STATUS_ROW, 1, "NETTERM " VERSION, ATTR(14, 1));
    text(STATUS_ROW, 16, "SERIAL", a);
    text(STATUS_ROW, 24, port == 1 ? "LINK" : port == 2 ? "CART" : inuse ? "AUTO:CART" : "AUTO:LINK", a);
    utoa10(bauds[bi < (int)NBAUD ? bi : 7], b);
    text(STATUS_ROW, 35, b, a);
    text(STATUS_ROW, 35 + 7, "8N1", a);
    text(STATUS_ROW, 47, echo ? "ECHO" : "", a);
    text(STATUS_ROW, 55, osk_is_open() ? "SELECT: CLOSE KEYS" : "SELECT: KEYBOARD", a);
}

/* The line's diagnostics, on the status line's right end each frame:
   T sent, R received, S start bits seen, F bad stop bits, then the levels of
   the link port's SI and the cartridge's pin 31 (1 = high, idle). */
static void hex4(char *b, uint32_t v)
{
    for (int i = 3; i >= 0; i--) { b[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
}

static void diag_draw(void)
{
    char b[40];
    uint32_t d = IO_UDIAG, st = IO_USTAT;
    int n = 0;
    b[n++] = 'T'; hex4(b + n, n_tx); n += 4; b[n++] = ' ';
    b[n++] = 'R'; hex4(b + n, n_rx); n += 4; b[n++] = ' ';
    b[n++] = 'S'; hex4(b + n, d); n += 4; b[n++] = ' ';
    b[n++] = 'F'; hex4(b + n, d >> 16); n += 4; b[n++] = ' ';
    b[n++] = 'L'; b[n++] = (st >> 8 & 1) ? '1' : '0';
    b[n++] = 'C'; b[n++] = (st >> 9 & 1) ? '1' : '0';
    b[n] = 0;
    if (!osk_is_open())
        text(OSK_ROW + 4, 2, b, ATTR(11, 0));
}

static void apply_settings(void)
{
    int bi = setting(SET_BAUD), port = setting(SET_PORT);
    uint32_t baud = bauds[bi < (int)NBAUD ? bi : 7];
    uint32_t div = (CLK_HZ + baud / 2) / baud;
    IO_UCTL = div | ((uint32_t)(port <= 2 ? port : 0) << 21);
    echo = setting(SET_ECHO) != 0;
}

int main(void)
{
    term_reset();
    keys_init();
    apply_settings();
    status_draw();
    {
        const char *m = "\033[1;36mNetTerm " VERSION "\033[0m  a serial terminal for the Analogue Pocket\r\n"
                        "Port, speed and echo: the core's menu.  Select: the on-screen keyboard.\r\n\r\n";
        while (*m) term_putc((uint8_t)*m++);
    }
    uint32_t last_frame = IO_FRAMES, last_inuse = 2;
    for (;;) {
        /* the serial port: up to a screenful a pass, so input stays live */
        for (int n = 0; n < 256 && (IO_USTAT & 1); n++) {
            uint32_t d = IO_UDATA;
            n_rx++;
            term_putc((uint8_t)d);
        }
        keys_poll();
        uint32_t f = IO_FRAMES;
        if (f != last_frame) {
            last_frame = f;
            uint32_t lo = IO_SET_LO, hi = IO_SET_HI, inuse = (IO_UCTL >> 23) & 1;
            if (lo != cur_lo || hi != cur_hi) {
                cur_lo = lo; cur_hi = hi;
                apply_settings();
                status_draw();
            }
            if (inuse != last_inuse) {
                last_inuse = inuse;
                status_draw();
            }
            diag_draw();
        }
    }
}
