/* NetTerm: input.
 *
 * The Dock's USB keyboard (controller 3, type 4; checked on hardware with the
 * input monitor core): up to six HID usage codes, keys 1-4 in joy[31:24] ..
 * joy[7:0], keys 5-6 in trig[15:8], trig[7:0]; the HID modifier byte in
 * key[15:8]. A code that wasn't there last time is a press; the newest held
 * key repeats (500 ms, then every 33 ms).
 *
 * Controller 1 (the Pocket's own buttons, or a Dock controller):
 *   keyboard closed: D-pad = the arrow keys, A = Enter, B = Backspace,
 *     Y = Space, X = Esc, L = Tab, Select = open the on-screen keyboard
 *   keyboard open:   D-pad = move, A = type the key, B = Backspace,
 *     Y = Space, X = Shift (once), L = Ctrl (once), R = Shift lock,
 *     Select = close
 * The on-screen keyboard fills rows 25-29, below the status line; the
 * terminal's 24 rows stay where they are.
 */
#include "hw.h"
#include "nt.h"

/* key codes past ASCII */
enum {
    K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN, K_INS, K_DEL,
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
    K_ENTER, K_BKSP, K_SHIFT, K_CTRL, K_HIDE,
};

static void key_out(int k)
{
    static const char *const seq[] = {
        "\033[A", "\033[B", "\033[D", "\033[C", "\033[H", "\033[F", "\033[5~", "\033[6~", "\033[2~", "\033[3~",
        "\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~",
        "\033[21~", "\033[23~", "\033[24~",
    };
    if (k == K_ENTER) {
        int e = setting(SET_ENTER);
        user_sends(e == 1 ? "\r\n" : e == 2 ? "\n" : "\r");
    } else if (k == K_BKSP) {
        char b = setting(SET_BKSP) ? 8 : 127;
        user_send(&b, 1);
    } else if (k >= K_UP && k <= K_F12) {
        user_sends(seq[k - K_UP]);
    } else if (k < 0x100) {
        char b = (char)k;
        user_send(&b, 1);
    }
}

/* ---------------- the Dock keyboard ---------------- */

/* HID usage 0x04-0x38 on a US layout: unshifted, shifted */
static const char hid_lo[] = "abcdefghijklmnopqrstuvwxyz1234567890\r\033\b\t -=[]\\#;'`,./";
static const char hid_hi[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\r\033\b\t _+{}|~:\"~<>?";

static int hid_key(uint8_t u, uint8_t mods)
{
    int shift = (mods & 0x22) != 0, ctrl = (mods & 0x11) != 0;
    if (u == 0x28 || u == 0x58) return K_ENTER;
    if (u == 0x2A) return K_BKSP;
    if (u >= 0x04 && u <= 0x38) {
        char c = (shift ? hid_hi : hid_lo)[u - 0x04];
        if (ctrl) {
            if (c >= 'a' && c <= 'z') return c - 'a' + 1;
            if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
            if (c == '[' || c == '{') return 27;
            if (c == '\\' || c == '|') return 28;
            if (c == ']' || c == '}') return 29;
            if (c == '@' || c == '2' || c == ' ') return 0;
        }
        return (uint8_t)c;
    }
    if (u >= 0x3A && u <= 0x45) return K_F1 + (u - 0x3A);
    switch (u) {
    case 0x49: return K_INS;
    case 0x4A: return K_HOME;
    case 0x4B: return K_PGUP;
    case 0x4C: return K_DEL;
    case 0x4D: return K_END;
    case 0x4E: return K_PGDN;
    case 0x4F: return K_RIGHT;
    case 0x50: return K_LEFT;
    case 0x51: return K_DOWN;
    case 0x52: return K_UP;
    case 0x54: return '/';
    case 0x55: return '*';
    case 0x56: return '-';
    case 0x57: return '+';
    case 0x62: return '0';
    case 0x63: return '.';
    default: break;
    }
    if (u >= 0x59 && u <= 0x61) return '1' + (u - 0x59);
    return -1;
}

static uint8_t kprev[6];
static uint8_t krep;                /* the key repeating, 0 none */
static uint32_t krep_t;

static void kbd_poll(uint32_t now)
{
    uint32_t k = IO_KEY(2), j = IO_JOY(2), t = IO_TRIG(2);
    uint8_t cur[6] = {0};
    if ((k >> 28) == 4) {
        cur[0] = j >> 24; cur[1] = j >> 16; cur[2] = j >> 8; cur[3] = j;
        cur[4] = t >> 8;  cur[5] = t;
    }
    uint8_t mods = k >> 8;
    for (int i = 0; i < 6; i++) {
        uint8_t u = cur[i];
        if (u < 4) continue;
        int seen = 0;
        for (int q = 0; q < 6; q++) if (kprev[q] == u) seen = 1;
        if (!seen) {
            int kk = hid_key(u, mods);
            if (kk >= 0) key_out(kk);
            krep = u;
            krep_t = now + 500;
        }
    }
    int held = 0;
    for (int i = 0; i < 6; i++) if (krep && cur[i] == krep) held = 1;
    if (!held) krep = 0;
    else if ((int32_t)(now - krep_t) >= 0) {
        int kk = hid_key(krep, mods);
        if (kk >= 0) key_out(kk);
        krep_t = now + 33;
    }
    for (int i = 0; i < 6; i++) kprev[i] = cur[i];
}

/* ---------------- the on-screen keyboard ---------------- */

typedef struct { const char *label; int lo, hi; uint8_t w; } okey;

#define K(l, a, b) {l, a, b, 3}
static const okey row0[] = { K("`", '`', '~'), K("1", '1', '!'), K("2", '2', '@'), K("3", '3', '#'), K("4", '4', '$'),
    K("5", '5', '%'), K("6", '6', '^'), K("7", '7', '&'), K("8", '8', '*'), K("9", '9', '('), K("0", '0', ')'),
    K("-", '-', '_'), K("=", '=', '+'), {"BKSP", K_BKSP, K_BKSP, 6}, {"DEL", K_DEL, K_DEL, 5}, {0, 0, 0, 0} };
static const okey row1[] = { {"TAB", '\t', '\t', 5}, K("q", 'q', 'Q'), K("w", 'w', 'W'), K("e", 'e', 'E'), K("r", 'r', 'R'),
    K("t", 't', 'T'), K("y", 'y', 'Y'), K("u", 'u', 'U'), K("i", 'i', 'I'), K("o", 'o', 'O'), K("p", 'p', 'P'),
    K("[", '[', '{'), K("]", ']', '}'), K("\\", '\\', '|'), {"PGUP", K_PGUP, K_PGUP, 6}, {0, 0, 0, 0} };
static const okey row2[] = { {"ESC", 27, 27, 5}, K("a", 'a', 'A'), K("s", 's', 'S'), K("d", 'd', 'D'), K("f", 'f', 'F'),
    K("g", 'g', 'G'), K("h", 'h', 'H'), K("j", 'j', 'J'), K("k", 'k', 'K'), K("l", 'l', 'L'), K(";", ';', ':'),
    K("'", '\'', '"'), {"ENTER", K_ENTER, K_ENTER, 7}, {"PGDN", K_PGDN, K_PGDN, 6}, {0, 0, 0, 0} };
static const okey row3[] = { {"SHIFT", K_SHIFT, K_SHIFT, 7}, K("z", 'z', 'Z'), K("x", 'x', 'X'), K("c", 'c', 'C'),
    K("v", 'v', 'V'), K("b", 'b', 'B'), K("n", 'n', 'N'), K("m", 'm', 'M'), K(",", ',', '<'), K(".", '.', '>'),
    K("/", '/', '?'), {"UP", K_UP, K_UP, 4}, {"HOME", K_HOME, K_HOME, 6}, {"END", K_END, K_END, 5}, {0, 0, 0, 0} };
static const okey row4[] = { {"CTRL", K_CTRL, K_CTRL, 6}, {"F1", K_F1, K_F1, 4}, {"F2", K_F2, K_F2, 4},
    {"F3", K_F3, K_F3, 4}, {"F4", K_F4, K_F4, 4}, {"SPACE", ' ', ' ', 13}, {"LEFT", K_LEFT, K_LEFT, 6},
    {"DOWN", K_DOWN, K_DOWN, 6}, {"RIGHT", K_RIGHT, K_RIGHT, 7}, {"HIDE", K_HIDE, K_HIDE, 6}, {0, 0, 0, 0} };
static const okey *const rows[OSK_ROWS] = { row0, row1, row2, row3, row4 };

static int osk_open, osk_r, osk_c, osk_shift, osk_lock, osk_ctrl;

int osk_is_open(void) { return osk_open; }

static int row_len(int r) { int n = 0; while (rows[r][n].label) n++; return n; }

static int key_col(int r, int c)       /* the screen column where key c starts */
{
    int x = 1;
    for (int i = 0; i < c; i++) x += rows[r][i].w + 1;
    return x;
}

static void osk_draw(void)
{
    uint8_t base = ATTR(15, 1), keyc = ATTR(0, 7), sel = ATTR(15, 4), on = ATTR(0, 14);
    for (int r = 0; r < OSK_ROWS; r++) {
        fill_row(OSK_ROW + r, 0, COLS, ' ', osk_open ? base : ATTR(7, 0));
        if (!osk_open) continue;
        int shifted = osk_shift || osk_lock;
        for (int c = 0; rows[r][c].label; c++) {
            const okey *k = &rows[r][c];
            int x = key_col(r, c);
            uint8_t a = (r == osk_r && c == osk_c) ? sel
                      : (k->lo == K_SHIFT && shifted) || (k->lo == K_CTRL && osk_ctrl) ? on : keyc;
            fill_row(OSK_ROW + r, x, x + k->w, ' ', a);
            const char *l = k->label;
            char one[2] = {0, 0};
            if (k->w == 3 && k->lo < 0x80 && k->lo >= ' ') { one[0] = (char)(shifted ? k->hi : k->lo); l = one; }
            int len = 0;
            while (l[len]) len++;
            text(OSK_ROW + r, x + (k->w - len) / 2, l, a);
        }
    }
    if (!osk_open)
        text(OSK_ROW + 2, 2, "SELECT: keys  D-pad: arrows  A: Enter  B: Backspace  Y: Space  X: Esc", ATTR(8, 0));
}

static void osk_type(void)
{
    const okey *k = &rows[osk_r][osk_c];
    int shifted = osk_shift || osk_lock;
    if (k->lo == K_SHIFT) { osk_shift = !osk_shift; return; }
    if (k->lo == K_CTRL) { osk_ctrl = !osk_ctrl; return; }
    if (k->lo == K_HIDE) { osk_open = 0; return; }
    int v = shifted ? k->hi : k->lo;
    if (osk_ctrl && v < 0x80) {
        if (v >= 'a' && v <= 'z') v = v - 'a' + 1;
        else if (v >= 'A' && v <= 'Z') v = v - 'A' + 1;
        else if (v == '[') v = 27;
        osk_ctrl = 0;
    }
    key_out(v);
    osk_shift = 0;
}

static void osk_move(int dr, int dc)
{
    if (dr) {
        int x = key_col(osk_r, osk_c) + rows[osk_r][osk_c].w / 2;
        osk_r = (osk_r + dr + OSK_ROWS) % OSK_ROWS;
        int n = row_len(osk_r), best = 0;
        for (int c = 0; c < n; c++)              /* the key under the same column */
            if (key_col(osk_r, c) <= x) best = c;
        osk_c = best;
    } else {
        int n = row_len(osk_r);
        osk_c = (osk_c + dc + n) % n;
    }
}

/* ---------------- controller 1 ---------------- */

enum { B_UP = 0, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_X, B_Y, B_L, B_R, B_SELECT = 14, B_START = 15 };

static uint32_t pprev;
static int prep;                    /* the button repeating, -1 none */
static uint32_t prep_t;

static void pad_press(int b)
{
    if (osk_open) {
        switch (b) {
        case B_UP: osk_move(-1, 0); break;
        case B_DOWN: osk_move(1, 0); break;
        case B_LEFT: osk_move(0, -1); break;
        case B_RIGHT: osk_move(0, 1); break;
        case B_A: osk_type(); break;
        case B_B: key_out(K_BKSP); break;
        case B_Y: key_out(' '); break;
        case B_X: osk_shift = !osk_shift; break;
        case B_L: osk_ctrl = !osk_ctrl; break;
        case B_R: osk_lock = !osk_lock; break;
        case B_SELECT: osk_open = 0; break;
        default: break;
        }
    } else {
        switch (b) {
        case B_UP: key_out(K_UP); break;
        case B_DOWN: key_out(K_DOWN); break;
        case B_LEFT: key_out(K_LEFT); break;
        case B_RIGHT: key_out(K_RIGHT); break;
        case B_A: key_out(K_ENTER); break;
        case B_B: key_out(K_BKSP); break;
        case B_Y: key_out(' '); break;
        case B_X: key_out(27); break;
        case B_L: key_out('\t'); break;
        case B_SELECT: osk_open = 1; break;
        default: break;
        }
    }
    osk_draw();
    if (b == B_SELECT || !osk_open) status_draw();
}

static void pad_poll(uint32_t now)
{
    uint32_t k = IO_KEY(0);
    uint32_t b = ((k >> 28) == 0 || (k >> 28) >= 4) ? 0 : (k & 0xFFFF);
    uint32_t down = b & ~pprev;
    for (int i = 0; i < 16; i++)
        if (down & (1u << i)) {
            pad_press(i);
            prep = (i <= B_RIGHT || i == B_B) ? i : -1;     /* the D-pad and Backspace repeat */
            prep_t = now + 400;
        }
    if (prep >= 0 && (b & (1u << prep))) {
        if ((int32_t)(now - prep_t) >= 0) {
            pad_press(prep);
            prep_t = now + 80;
        }
    } else
        prep = -1;
    pprev = b;
}

void keys_init(void)
{
    osk_open = 0;
    prep = -1;
    osk_draw();
}

void keys_poll(void)
{
    uint32_t now = ms();
    kbd_poll(now);
    pad_poll(now);
}
