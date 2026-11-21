/* NetTerm: the address book.
 *
 * Kept in the save slot (data.json "Address Book", Saves/netterm/common/
 * addressbook.sav): the Pocket loads it into the save RAM before the core
 * starts, and writes the save RAM back to the file when the core is quit.
 *
 * Layout (2048 bytes): "NTBOOK1" and a NUL at 0, then BOOK_MAX entries of 64
 * bytes from 16: a name of up to 23 characters and an address (host or
 * host:port) of up to 39, each NUL-terminated in its 24 / 40 bytes. An entry
 * with an empty address is free.
 *
 * Start (or Alt+D on the Dock keyboard) opens it over the terminal's rows.
 * In the list: Up / Down choose, Enter (A) dials (sends ATDT address as one
 * burst, as Mode FujiNet Modem does), Space (Y) edits, Tab (L) adds, Delete
 * removes, Right opens the BBS list (list.c), Esc (X) or Backspace (B)
 * closes. Editing: type; Up / Down or Tab
 * move between the name and the address; Enter on the address saves (an empty
 * address removes the entry); Esc cancels.
 */
#include "hw.h"
#include "nt.h"

#define BOOK_MAX    30
#define NAME_LEN    24
#define ADDR_LEN    40
#define ENTRY(i)    (SAVE + 16 + (i) * 64)

static const char MAGIC[8] = "NTBOOK1";

static int open_, editing, sel, top, field, edit_slot;
#define SHOWN       17              /* entries on the screen at once */
static char e_name[NAME_LEN], e_addr[ADDR_LEN];
extern uint16_t sb_live[TROWS][COLS];       /* the terminal's rows, while the book is up (scrollback.c's) */
#define under sb_live

int book_is_open(void) { return open_; }

static void sget(volatile uint8_t *p, char *d, int n)
{
    for (int i = 0; i < n - 1; i++) { d[i] = (char)p[i]; if (!d[i]) return; }
    d[n - 1] = 0;
}

static void sput(volatile uint8_t *p, const char *s, int n)
{
    int i = 0;
    for (; i < n - 1 && s[i]; i++) p[i] = (uint8_t)s[i];
    for (; i < n; i++) p[i] = 0;
}

static int used(int i) { return ENTRY(i)[NAME_LEN] != 0; }

static int count(void)
{
    int n = 0;
    for (int i = 0; i < BOOK_MAX; i++) n += used(i);
    return n;
}

static int nth(int k)                   /* the k-th used entry's slot */
{
    for (int i = 0; i < BOOK_MAX; i++)
        if (used(i) && k-- == 0) return i;
    return -1;
}

static void init_if_blank(void)
{
    for (int i = 0; i < 8; i++)
        if (SAVE[i] != (uint8_t)MAGIC[i]) goto blank;
    return;
blank:
    for (int i = 0; i < SAVE_SIZE; i++) SAVE[i] = 0;
    for (int i = 0; i < 8; i++) SAVE[i] = (uint8_t)MAGIC[i];
    sput(ENTRY(0), "20 For Beers", NAME_LEN);           /* a BBS seen answering (2026-11-21) */
    sput(ENTRY(0) + NAME_LEN, "20forbeers.com:1337", ADDR_LEN);
}

/* ---------------- drawing ---------------- */

static void draw(void)
{
    uint8_t bg = ATTR(7, 1), hi = ATTR(15, 4), title = ATTR(0, 7), dim = ATTR(11, 1);
    for (int r = 0; r < TROWS; r++) fill_row(r, 0, COLS, ' ', bg);
    fill_row(0, 0, COLS, ' ', title);
    text(0, 2, editing ? "ADDRESS BOOK: EDIT" : "ADDRESS BOOK", title);
    if (editing) {
        text(3, 4, "Name", bg);
        text(5, 4, "Address", bg);
        for (int f = 0; f < 2; f++) {
            int row = 3 + 2 * f;
            const char *v = f ? e_addr : e_name;
            int w = f ? ADDR_LEN - 1 : NAME_LEN - 1;
            fill_row(row, 14, 14 + w + 1, ' ', f == field ? hi : ATTR(0, 7));
            text(row, 14, v, f == field ? hi : ATTR(0, 7));
            if (f == field) {
                int n = 0;
                while (v[n]) n++;
                cell(row, 14 + n, '_', hi);
            }
        }
        text(8, 4, "host, or host:port (port 23 if left out)", dim);
        text(20, 2, "Type on the keyboard (Select: the on-screen one).", dim);
        text(21, 2, "Up / Down or Tab: the other field.  Enter on the address: save.", dim);
        text(22, 2, "An empty address removes the entry.  Esc (X): cancel.", dim);
        return;
    }
    int n = count();
    if (!n)
        text(3, 4, "No entries yet: Tab (L) adds one, or Right: the BBS list.", bg);
    if (sel < top) top = sel;
    if (sel >= top + SHOWN) top = sel - SHOWN + 1;
    if (n) {
        char b[16];
        int m = 0;
        b[m++] = '0' + (sel + 1) / 10 % 10; b[m++] = '0' + (sel + 1) % 10;
        b[m++] = '/'; b[m++] = '0' + n / 10 % 10; b[m++] = '0' + n % 10; b[m] = 0;
        text(0, COLS - 8, b, title);
    }
    for (int k = top; k < n && k < top + SHOWN; k++) {
        int i = nth(k);
        char nm[NAME_LEN], ad[ADDR_LEN];
        sget(ENTRY(i), nm, NAME_LEN);
        sget(ENTRY(i) + NAME_LEN, ad, ADDR_LEN);
        uint8_t a = k == sel ? hi : bg;
        fill_row(2 + k - top, 2, COLS - 2, ' ', a);
        text(2 + k - top, 4, nm, a);
        text(2 + k - top, 30, ad, a);
    }
    text(20, 2, "Enter (A): dial   Space (Y): edit   Tab (L): add   Delete: remove", dim);
    text(21, 2, "Right: the BBS list.  Esc (X) or Backspace (B): close.  Start or Alt+D: close.", dim);
    text(22, 2, "Kept in the save slot: written to the SD card when the core is quit.", dim);
}

/* ---------------- opening and closing ---------------- */

void book_open(void)
{
    if (open_) return;
    if (sb_active()) sb_exit();
    init_if_blank();
    for (int r = 0; r < TROWS; r++)
        for (int c = 0; c < COLS; c++)
            under[r][c] = (uint16_t)cell_get(r, c);
    term_show_cursor(0);
    open_ = 1;
    editing = 0;
    int n = count();
    if (sel >= n) sel = n ? n - 1 : 0;
    draw();
}

void book_close(void)
{
    if (!open_) return;
    open_ = 0;
    while (list_is_open()) list_key(27);
    for (int r = 0; r < TROWS; r++)
        for (int c = 0; c < COLS; c++)
            cell_set_raw(r, c, under[r][c]);
    term_show_cursor(term_cursor_wanted());
}

void book_toggle(void) { if (open_) book_close(); else book_open(); }

/* ---------------- keys ---------------- */

static void start_edit(int slot)
{
    edit_slot = slot;
    if (slot >= 0) {
        sget(ENTRY(slot), e_name, NAME_LEN);
        sget(ENTRY(slot) + NAME_LEN, e_addr, ADDR_LEN);
    } else {
        e_name[0] = e_addr[0] = 0;
    }
    field = 0;
    editing = 1;
}

static void save_edit(void)
{
    int slot = edit_slot;
    if (!e_addr[0]) {                           /* empty: remove */
        if (slot >= 0) ENTRY(slot)[NAME_LEN] = 0, ENTRY(slot)[0] = 0;
    } else {
        if (slot < 0)
            for (int i = 0; i < BOOK_MAX; i++)
                if (!used(i)) { slot = i; break; }
        if (slot >= 0) {
            sput(ENTRY(slot), e_name[0] ? e_name : e_addr, NAME_LEN);
            sput(ENTRY(slot) + NAME_LEN, e_addr, ADDR_LEN);
        }
    }
    editing = 0;
    int n = count();
    if (sel >= n) sel = n ? n - 1 : 0;
}

/* An entry from the BBS list: 1 added, 0 already there (the same address),
   -1 no room */
int book_add(const char *name, const char *addr)
{
    init_if_blank();
    char ad[ADDR_LEN];
    int slot = -1;
    for (int i = 0; i < BOOK_MAX; i++) {
        if (!used(i)) { if (slot < 0) slot = i; continue; }
        sget(ENTRY(i) + NAME_LEN, ad, ADDR_LEN);
        int j = 0;
        while (ad[j] && ad[j] == addr[j]) j++;
        if (ad[j] == addr[j]) return 0;
    }
    if (slot < 0) return -1;
    sput(ENTRY(slot), name, NAME_LEN);
    sput(ENTRY(slot) + NAME_LEN, addr, ADDR_LEN);
    return 1;
}

void book_key(int k)
{
    if (list_is_open()) {
        list_key(k);
        if (!open_) return;             /* dialled: the book closed too */
        if (!list_is_open()) {          /* back to the book */
            int n = count();
            if (sel >= n) sel = n ? n - 1 : 0;
            draw();
        }
        return;
    }
    const int k_up = K_UP, k_down = K_DOWN, k_enter = K_ENTER, k_bksp = K_BKSP, k_del = K_DEL;
    if (editing) {
        char *v = field ? e_addr : e_name;
        int max = field ? ADDR_LEN - 1 : NAME_LEN - 1, n = 0;
        while (v[n]) n++;
        if (k == 27) editing = 0;
        else if (k == k_up || k == k_down || k == '\t') field ^= 1;
        else if (k == k_enter) { if (field == 0) field = 1; else save_edit(); }
        else if (k == k_bksp) { if (n) v[n - 1] = 0; }
        else if (k >= 32 && k < 127 && n < max) { v[n] = (char)k; v[n + 1] = 0; }
        draw();
        return;
    }
    int n = count();
    if (k == 27 || k == k_bksp) { book_close(); return; }
    if (k == k_up && sel > 0) sel--;
    else if (k == k_down && sel + 1 < n) sel++;
    else if (k == K_PGUP) sel = sel > SHOWN ? sel - SHOWN : 0;
    else if (k == K_PGDN) { sel += SHOWN; if (sel >= n) sel = n ? n - 1 : 0; }
    else if (k == K_RIGHT) { list_open(); return; }
    else if (k == ' ' && n) start_edit(nth(sel));
    else if (k == '\t') { if (n < BOOK_MAX) start_edit(-1); }
    else if (k == k_del && n) {
        int i = nth(sel);
        ENTRY(i)[0] = 0;
        ENTRY(i)[NAME_LEN] = 0;
        if (sel >= n - 1 && sel) sel--;
    } else if (k == k_enter && n) {
        char ad[ADDR_LEN];
        sget(ENTRY(nth(sel)) + NAME_LEN, ad, ADDR_LEN);
        book_close();
        modem_dial(ad);
        return;
    }
    draw();
}
