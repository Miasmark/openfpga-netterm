/* NetTerm firmware: what the parts share. */
#ifndef NT_H
#define NT_H
#include <stdint.h>

#define VERSION     "0.1.1"
#define COLS        80
#define TROWS       24              /* the terminal: rows 0-23 (VT100's 80 x 24) */
#define STATUS_ROW  24
#define OSK_ROW     25              /* the on-screen keyboard: rows 25-29 */
#define OSK_ROWS    5

/* attribute byte: [3:0] foreground, [7:4] background (CGA colours) */
#define ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

/* screen.c: cells */
void cell(int row, int col, uint8_t ch, uint8_t attr);
uint32_t cell_get(int row, int col);
void cell_set_raw(int row, int col, uint32_t v);
void text(int row, int col, const char *s, uint8_t attr);
void fill_row(int row, int col0, int col1, uint8_t ch, uint8_t attr);

/* term.c: the VT100 / ANSI terminal in rows 0-23 */
void term_reset(void);
void term_putc(uint8_t c);
void term_show_cursor(int on);

/* main.c: to the host (the serial port), and the local echo */
void host_send(const char *s, int n);
void host_sends(const char *s);

/* keys.c: the Dock keyboard, the controller and the on-screen keyboard */
void keys_init(void);
void keys_poll(void);
int osk_is_open(void);

/* main.c: the status line */
void status_draw(void);

#endif
