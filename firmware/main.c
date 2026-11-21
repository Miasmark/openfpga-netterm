/* NetTerm firmware: the main loop.
 *
 * Mode Serial: the serial port is the host. Bytes in go to the terminal; keys
 * go out, and to the terminal too with Local Echo on.
 *
 * Mode FujiNet Modem: the same line, to a FujiNet's RS232 build, whose modem
 * (fujinet-firmware lib/device/rs232/modem.cpp) takes the bytes that come
 * outside FujiBus frames. In its command state it reads each burst of bytes
 * (a gap of 100 ms ends one) as one whole command and answers nothing to it,
 * so here a command is typed into a local line editor and sent in one burst
 * on Enter. When "CONNECT" comes back the modem is online and keys go
 * straight out; "NO CARRIER" puts it back in the command state.
 *
 * The menu's settings (interact.json) are read every frame and applied when
 * they change: the mode, the port, the baud rate, the echo, what Enter and
 * Backspace send.
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

/* ---------------- Mode FujiNet Modem ---------------- */

static int mode, online;
static char line[128];
static int llen;

static void raw_send(const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        while (IO_USTAT & 2) ;
        IO_UDATA = (uint8_t)s[i];
        n_tx++;
    }
}

static void tputs(const char *s) { while (*s) term_putc((uint8_t)*s++); }

void user_send(const char *s, int n)
{
    if (mode != MODE_MODEM || online) { host_send(s, n); return; }
    if (n > 1 && s[0] == 27) return;            /* the arrows and such: nothing to edit with */
    for (int i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c == '\r' || (c == '\n' && !(i > 0 && s[i - 1] == '\r'))) {
            raw_send(line, llen);               /* the whole command, one burst */
            raw_send("\r", 1);
            llen = 0;
            tputs("\r\n");
        } else if (c == 127 || c == 8) {
            if (llen) { llen--; tputs("\b \b"); }
        } else if (c >= 32 && c < 127 && llen < (int)sizeof line - 1) {
            line[llen++] = (char)c;
            term_putc(c);
        }
    }
}

void user_sends(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    user_send(s, n);
}

/* the modem's word, in the bytes coming back */
static int match(const char *w, int *pos, uint8_t c)
{
    if (c == (uint8_t)w[*pos]) {
        if (!w[++*pos]) { *pos = 0; return 1; }
    } else
        *pos = c == (uint8_t)w[0];
    return 0;
}

static void modem_watch(uint8_t c)
{
    static int pc, pn;
    if (mode != MODE_MODEM) return;
    if (match("CONNECT", &pc, c) && !online) { online = 1; llen = 0; status_draw(); }
    if (match("NO CARRIER", &pn, c) && online) { online = 0; llen = 0; status_draw(); }
}

static void mode_banner(void)
{
    if (mode == MODE_MODEM)
        tputs("\r\n\033[1;33mFujiNet modem:\033[0m type ATDT host:port and Enter (port 23 if left out).\r\n"
              "The modem answers nothing until CONNECT; ATH hangs up, from the command state.\r\n\r\n");
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
    text(STATUS_ROW, 16, mode != MODE_MODEM ? "SERIAL" : online ? "MODEM:ON " : "MODEM:CMD", a);
    text(STATUS_ROW, 27, port == 1 ? "LINK" : port == 2 ? "CART" : port == 3 ? "LOOP TEST" : inuse ? "AUTO:CART" : "AUTO:LINK", a);
    utoa10(bauds[bi < (int)NBAUD ? bi : 7], b);
    text(STATUS_ROW, 38, b, a);
    text(STATUS_ROW, 45, "8N1", a);
    text(STATUS_ROW, 50, echo ? "ECHO" : "", a);
    text(STATUS_ROW, 57, osk_is_open() ? "SELECT: CLOSE KEYS" : "SELECT: KEYBOARD", a);
}

/* The line's diagnostics, on the status line's right end each frame:
   T handed to the port, X bytes the transmitter started, R received, S start bits seen, F bad stop bits, then the levels of
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
    b[n++] = 'X'; hex4(b + n, IO_UTXN); n += 4; b[n++] = ' ';
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
    IO_UCTL = div | ((uint32_t)(port <= 3 ? port : 0) << 21);
    echo = setting(SET_ECHO) != 0;
    int m = setting(SET_MODE) == MODE_MODEM ? MODE_MODEM : MODE_SERIAL;
    if (m != mode) {
        mode = m;
        online = 0;
        llen = 0;
        mode_banner();
    }
}

int main(void)
{
    term_reset();
    keys_init();
    tputs("\033[1;36mNetTerm " VERSION "\033[0m  a serial terminal for the Analogue Pocket\r\n"
          "Mode, port, speed and echo: the core's menu.  Select: the on-screen keyboard.\r\n");
    apply_settings();
    status_draw();
    uint32_t last_frame = IO_FRAMES, last_inuse = 2;
    for (;;) {
        /* the serial port: up to a screenful a pass, so input stays live */
        for (int n = 0; n < 256 && (IO_USTAT & 1); n++) {
            uint32_t d = IO_UDATA;
            n_rx++;
            term_putc((uint8_t)d);
            modem_watch((uint8_t)d);
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
