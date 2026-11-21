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

void host_send(const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        while (IO_USTAT & 2) ;          /* TX full: wait (64 bytes drain in 5.6 ms at 115,200) */
        IO_UDATA = (uint8_t)s[i];
        if (echo) term_putc((uint8_t)s[i]);
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
        }
    }
}
