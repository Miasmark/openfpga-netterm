/* NetTerm: the terminal, a VT100 / ANSI-BBS subset, in rows 0-23.
 *
 * Controls: BEL, BS, HT, LF / VT / FF, CR. ESC 7 / 8 (save / restore the
 * cursor), ESC D / M / E (index, reverse index, next line), ESC c (reset),
 * ESC ( / ) x (character sets: ignored), ESC ] ... (OSC: skipped to BEL / ST).
 * CSI: A B C D E F G H f d (moves), J K (erase), L M (lines), @ P X (chars),
 * m (SGR: 0 1 2 5 7 22 25 27 30-37 39 40-47 49 90-97 100-107), r (scroll
 * region), s u (save / restore), h l (?25: the cursor; others ignored),
 * n (6: the cursor's position, 5: OK), c (VT100 with no options).
 * Characters are code page 437 bytes, as BBSs send them; the font has them.
 */
#include "hw.h"
#include "nt.h"

static int cx, cy;                  /* the cursor */
static int wrap;                    /* at the right edge: the next character wraps first */
static int top, bot;                /* the scroll region, rows top-bot */
static uint8_t fg, bg, bold, rev;
static int sx, sy;                  /* saved cursor */
static uint8_t sfg, sbg, sbold, srev;

enum { S_NORM, S_ESC, S_CSI, S_OSC, S_OSC_ESC, S_SKIP1 };
static int st;
static int par[16], npar, priv;

static uint8_t attr(void)
{
    uint8_t f = fg | (bold ? 8 : 0), b = bg;
    if (rev) { uint8_t t = f; f = b; b = t & 7; }
    return ATTR(f, b);
}

static void cursor_sync(void)
{
    IO_CURSOR = (uint32_t)(cx < COLS ? cx : COLS - 1) | ((uint32_t)cy << 8) | (1u << 16);
}

void term_show_cursor(int on)
{
    uint32_t v = IO_CURSOR & 0xFFFF;
    IO_CURSOR = v | (on ? (1u << 16) : 0);
}

static void clear_cells(int row, int c0, int c1)
{
    uint8_t a = ATTR(7, bg);
    for (int c = c0; c < c1; c++)
        cell(row, c, ' ', a);
}

static void scroll_up(int t, int b, int n)
{
    if (n > b - t + 1) n = b - t + 1;
    for (int r = t; r + n <= b; r++)
        for (int c = 0; c < COLS; c++)
            cell_set_raw(r, c, cell_get(r + n, c));
    for (int r = b - n + 1; r <= b; r++)
        clear_cells(r, 0, COLS);
}

static void scroll_down(int t, int b, int n)
{
    if (n > b - t + 1) n = b - t + 1;
    for (int r = b; r - n >= t; r--)
        for (int c = 0; c < COLS; c++)
            cell_set_raw(r, c, cell_get(r - n, c));
    for (int r = t; r < t + n; r++)
        clear_cells(r, 0, COLS);
}

static void lf(void)
{
    if (cy == bot) scroll_up(top, bot, 1);
    else if (cy < TROWS - 1) cy++;
}

static void ri(void)
{
    if (cy == top) scroll_down(top, bot, 1);
    else if (cy > 0) cy--;
}

void term_reset(void)
{
    cx = cy = 0; wrap = 0;
    top = 0; bot = TROWS - 1;
    fg = 7; bg = 0; bold = rev = 0;
    sx = sy = 0; sfg = 7; sbg = 0; sbold = srev = 0;
    st = S_NORM;
    for (int r = 0; r < TROWS; r++)
        clear_cells(r, 0, COLS);
    cursor_sync();
}

static int p(int i, int d) { return i < npar && par[i] > 0 ? par[i] : d; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void sgr(void)
{
    if (npar == 0) npar = 1, par[0] = 0;
    for (int i = 0; i < npar; i++) {
        int v = par[i];
        static const uint8_t ansi2cga[8] = {0, 4, 2, 6, 1, 5, 3, 7};   /* ANSI's colour order -> CGA's */
        if (v == 0) { fg = 7; bg = 0; bold = rev = 0; }
        else if (v == 1) bold = 1;
        else if (v == 2 || v == 22) bold = 0;
        else if (v == 7) rev = 1;
        else if (v == 27) rev = 0;
        else if (v >= 30 && v <= 37) fg = ansi2cga[v - 30];
        else if (v == 39) fg = 7;
        else if (v >= 40 && v <= 47) bg = ansi2cga[v - 40];
        else if (v == 49) bg = 0;
        else if (v >= 90 && v <= 97) { fg = ansi2cga[v - 90]; bold = 1; }
        else if (v >= 100 && v <= 107) bg = ansi2cga[v - 100] | 8;
        /* 5 / 25 (blink) and the rest: ignored */
    }
}

static void reply_pos(void)
{
    char b[16];
    int n = 0, r = cy + 1, c = (cx < COLS ? cx : COLS - 1) + 1;
    b[n++] = 27; b[n++] = '[';
    if (r >= 10) b[n++] = '0' + r / 10;
    b[n++] = '0' + r % 10; b[n++] = ';';
    if (c >= 10) b[n++] = '0' + c / 10;
    b[n++] = '0' + c % 10; b[n++] = 'R';
    host_send(b, n);
}

static void csi(uint8_t f)
{
    int n;
    switch (f) {
    case 'A': cy = clampi(cy - p(0, 1), cy >= top ? top : 0, TROWS - 1); break;
    case 'B': cy = clampi(cy + p(0, 1), 0, cy <= bot ? bot : TROWS - 1); break;
    case 'C': cx = clampi(cx + p(0, 1), 0, COLS - 1); break;
    case 'D': cx = clampi((cx < COLS ? cx : COLS - 1) - p(0, 1), 0, COLS - 1); break;
    case 'E': cy = clampi(cy + p(0, 1), 0, TROWS - 1); cx = 0; break;
    case 'F': cy = clampi(cy - p(0, 1), 0, TROWS - 1); cx = 0; break;
    case 'G': cx = clampi(p(0, 1) - 1, 0, COLS - 1); break;
    case 'd': cy = clampi(p(0, 1) - 1, 0, TROWS - 1); break;
    case 'H': case 'f':
        cy = clampi(p(0, 1) - 1, 0, TROWS - 1);
        cx = clampi(p(1, 1) - 1, 0, COLS - 1);
        break;
    case 'J':
        n = npar ? par[0] : 0;
        if (n == 0) {
            clear_cells(cy, cx < COLS ? cx : COLS, COLS);
            for (int r = cy + 1; r < TROWS; r++) clear_cells(r, 0, COLS);
        } else if (n == 1) {
            for (int r = 0; r < cy; r++) clear_cells(r, 0, COLS);
            clear_cells(cy, 0, (cx < COLS ? cx : COLS - 1) + 1);
        } else {
            for (int r = 0; r < TROWS; r++) clear_cells(r, 0, COLS);
            if (n == 2) cx = cy = 0;        /* ANSI.SYS homes the cursor; BBS art expects it */
        }
        break;
    case 'K':
        n = npar ? par[0] : 0;
        if (n == 0) clear_cells(cy, cx < COLS ? cx : COLS, COLS);
        else if (n == 1) clear_cells(cy, 0, (cx < COLS ? cx : COLS - 1) + 1);
        else clear_cells(cy, 0, COLS);
        break;
    case 'L': if (cy >= top && cy <= bot) scroll_down(cy, bot, p(0, 1)); break;
    case 'M': if (cy >= top && cy <= bot) scroll_up(cy, bot, p(0, 1)); break;
    case '@':
        n = p(0, 1);
        for (int c = COLS - 1; c >= cx + n; c--) cell_set_raw(cy, c, cell_get(cy, c - n));
        clear_cells(cy, cx, cx + n < COLS ? cx + n : COLS);
        break;
    case 'P':
        n = p(0, 1);
        for (int c = cx; c < COLS; c++) {
            if (c + n < COLS) cell_set_raw(cy, c, cell_get(cy, c + n));
            else clear_cells(cy, c, c + 1);
        }
        break;
    case 'X':
        n = p(0, 1);
        clear_cells(cy, cx, cx + n < COLS ? cx + n : COLS);
        break;
    case 'm': sgr(); break;
    case 'r':
        top = clampi(p(0, 1) - 1, 0, TROWS - 1);
        bot = clampi(p(1, TROWS) - 1, 0, TROWS - 1);
        if (bot <= top) { top = 0; bot = TROWS - 1; }
        cx = 0; cy = 0;
        break;
    case 's': sx = cx; sy = cy; break;
    case 'u': cx = sx; cy = sy; break;
    case 'h': case 'l':
        if (priv && npar && par[0] == 25) term_show_cursor(f == 'h');
        break;
    case 'n':
        if (npar && par[0] == 6) reply_pos();
        else if (npar && par[0] == 5) host_sends("\033[0n");
        break;
    case 'c':
        if (!priv) host_sends("\033[?1;0c");
        break;
    default: break;
    }
    wrap = 0;
}

void term_putc(uint8_t c)
{
    switch (st) {
    case S_ESC:
        st = S_NORM;
        switch (c) {
        case '[': st = S_CSI; npar = 0; priv = 0; par[0] = 0; return;
        case ']': st = S_OSC; return;
        case '(': case ')': st = S_SKIP1; return;
        case '7': sx = cx; sy = cy; sfg = fg; sbg = bg; sbold = bold; srev = rev; break;
        case '8': cx = sx; cy = sy; fg = sfg; bg = sbg; bold = sbold; rev = srev; break;
        case 'D': lf(); break;
        case 'M': ri(); break;
        case 'E': cx = 0; lf(); break;
        case 'c': term_reset(); break;
        default: break;
        }
        wrap = 0;
        cursor_sync();
        return;
    case S_CSI:
        if (c >= '0' && c <= '9') {
            if (npar == 0) npar = 1;
            par[npar - 1] = par[npar - 1] * 10 + (c - '0');
            if (par[npar - 1] > 9999) par[npar - 1] = 9999;
        } else if (c == ';') {
            if (npar == 0) npar = 1;
            if (npar < 16) par[npar++] = 0;
        } else if (c == '?' || c == '>' || c == '=') {
            priv = 1;
        } else if (c >= 0x40 && c <= 0x7E) {
            st = S_NORM;
            csi(c);
            cursor_sync();
        } else if (c == 24 || c == 26) {
            st = S_NORM;            /* CAN / SUB: abandon the sequence */
        }
        return;
    case S_OSC:
        if (c == 7) st = S_NORM;
        else if (c == 27) st = S_OSC_ESC;
        return;
    case S_OSC_ESC:
        st = c == '\\' ? S_NORM : S_OSC;
        return;
    case S_SKIP1:
        st = S_NORM;
        return;
    default:
        break;
    }

    switch (c) {
    case 27: st = S_ESC; return;
    case 7: return;                     /* BEL: nothing to ring */
    case 8: if (cx > 0) cx = (cx < COLS ? cx : COLS - 1) - (wrap ? 0 : 1); wrap = 0; break;
    case 9: cx = (cx < COLS ? cx : COLS - 1); cx = (cx + 8) & ~7; if (cx > COLS - 1) cx = COLS - 1; wrap = 0; break;
    case 10: case 11: case 12: lf(); wrap = 0; break;
    case 13: cx = 0; wrap = 0; break;
    case 0: case 127: return;
    default:
        if (c < 32) return;
        if (wrap) {
            cx = 0;
            lf();
            wrap = 0;
        }
        cell(cy, cx, c, attr());
        if (cx == COLS - 1) wrap = 1;
        else cx++;
        break;
    }
    cursor_sync();
}
