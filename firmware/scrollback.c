/* NetTerm: scrollback.
 *
 * Every line the terminal scrolls off the top of the screen (a scroll whose
 * region starts at row 0) is kept here, the last SB_LINES of them. Looking
 * back: the live rows 0-23 are copied aside, and rows 0-23 show the history
 * and then the live rows, offset lines up from the bottom. While the view is
 * up, main.c leaves the serial input in the UART's FIFO; any key that isn't a
 * scrollback key, or a FIFO getting full, puts the live rows back first.
 */
#include "hw.h"
#include "nt.h"

static uint16_t hist[SB_LINES][COLS];
static int head;                    /* the next line to write */
static int count;                   /* lines kept */
static uint16_t live[TROWS][COLS];
static int offset;                  /* lines back from the live screen; 0 = live */

void sb_push_row(int row)
{
    for (int c = 0; c < COLS; c++)
        hist[head][c] = (uint16_t)cell_get(row, c);
    head = (head + 1) % SB_LINES;
    if (count < SB_LINES) count++;
}

int sb_active(void) { return offset != 0; }

static const uint16_t *line_at(int i)     /* i: 0 = the oldest kept line */
{
    return hist[(head - count + i + SB_LINES) % SB_LINES];
}

static void draw(void)
{
    for (int r = 0; r < TROWS; r++) {
        int i = count - offset + r;
        const uint16_t *src = i < count ? line_at(i) : live[i - count];
        for (int c = 0; c < COLS; c++)
            cell_set_raw(r, c, src[c]);
    }
}

void sb_scroll(int delta)           /* + = back in time */
{
    if (!count) return;
    int was = offset;
    int o = offset + delta;
    if (o < 0) o = 0;
    if (o > count) o = count;
    if (o == was) return;
    if (!was) {                     /* entering: keep the live screen */
        for (int r = 0; r < TROWS; r++)
            for (int c = 0; c < COLS; c++)
                live[r][c] = (uint16_t)cell_get(r, c);
        term_show_cursor(0);
    }
    offset = o;
    draw();
    if (!offset)
        sb_exit();
    status_draw();
}

void sb_top(void) { sb_scroll(SB_LINES); }

void sb_exit(void)
{
    if (offset) {
        offset = 0;
        draw();
    }
    term_show_cursor(term_cursor_wanted());
    status_draw();
}

int sb_offset(void) { return offset; }
int sb_count(void) { return count; }
