/* NetTerm: the BBS list.
 *
 * A SyncTERM dialing directory (syncterm.lst), as the Telnet BBS Guide's
 * monthly download (telnetbbsguide.com, ibbsMMYY.zip) has it, copied to the
 * SD card's Assets/netterm/common/ (data.json "BBS List", data slot 0x101).
 * The slot is deferloaded: the Pocket opens the file when the core starts and
 * puts its size in the data table, and this reads it 4 KiB at a time through
 * the window at LWIN (target command 0x0180, nt_soc.sv), the first time the
 * list is opened.
 *
 * The file: INI-like, CRLF or LF, Latin-1. Lines before the first [section]
 * set the defaults (Port, ConnectionType); each [Name] section is a BBS, with
 * Address, Port and ConnectionType lines (others are skipped). Only Telnet
 * entries are kept (SSH, RLogin and the rest are counted and left out), as
 * "name\0address\0" in the pool, the address as host or host:port (port 23
 * left off), in the file's order.
 *
 * From the address book, Right opens it. Up / Down, Left / Right or Page Up /
 * Page Down (a page), Home / End; a letter or digit on the keyboard jumps to
 * the next name starting with it. Enter (A) dials, Space (Y) copies the entry
 * into the address book, Esc (X) or Backspace (B) goes back to the book.
 */
#include "hw.h"
#include "nt.h"

#define POOL_SIZE   (48 * 1024)
#define LIST_MAX    1536
#define NAME_MAX    36              /* kept, with the NUL */
#define ADDR_MAX    40              /* the address book's limit, with the NUL */
#define SHOWN       18              /* rows 2-19 */

static char pool[POOL_SIZE];
static uint16_t ent[LIST_MAX];      /* each entry's name in the pool */
static int nent, nother, loaded, load_err, truncated;
static uint32_t loaded_size;
static int open_, sel, top;
static char msg[48];

int list_is_open(void) { return open_; }

/* ---------------- parsing ---------------- */

static int pool_n;
static char line[160];
static int line_n, line_long;
static char s_name[NAME_MAX], s_addr[ADDR_MAX], s_port[8], s_type[12];
static char d_port[8] = "23", d_type[12] = "Telnet";
static int in_section;

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int ieq(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) a++, b++;
    return !*a && !*b;
}

static void copy(char *d, const char *s, int n)
{
    int i = 0;
    for (; i < n - 1 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

static void section_end(void)
{
    if (!in_section) return;
    in_section = 0;
    const char *type = s_type[0] ? s_type : d_type;
    const char *port = s_port[0] ? s_port : d_port;
    if (!ieq(type, "telnet")) { nother++; return; }
    if (!s_addr[0]) return;
    char ad[ADDR_MAX];
    int n = 0;
    for (const char *p = s_addr; *p && n < ADDR_MAX - 1; p++) ad[n++] = *p;
    if (!ieq(port, "23") && port[0]) {
        int pl = 0;
        while (port[pl]) pl++;
        if (n + 1 + pl > ADDR_MAX - 1) return;      /* won't fit the book: left out */
        ad[n++] = ':';
        for (const char *p = port; *p; p++) ad[n++] = *p;
    }
    ad[n] = 0;
    int nl = 0;
    while (s_name[nl]) nl++;
    if (nent >= LIST_MAX || pool_n + nl + 1 + n + 1 > POOL_SIZE) { truncated = 1; return; }
    ent[nent++] = (uint16_t)pool_n;
    for (int i = 0; i <= nl; i++) pool[pool_n++] = s_name[i];
    for (int i = 0; i <= n; i++) pool[pool_n++] = ad[i];
}

static void do_line(void)
{
    char *l = line;
    line[line_n] = 0;
    while (*l == ' ' || *l == '\t') l++;
    int n = 0;
    while (l[n]) n++;
    while (n && (l[n - 1] == ' ' || l[n - 1] == '\t' || l[n - 1] == '\r')) l[--n] = 0;
    if (!n || *l == ';' || *l == '#') return;
    if (*l == '[') {
        section_end();
        if (n >= 2 && l[n - 1] == ']') l[--n] = 0;
        copy(s_name, l + 1, NAME_MAX);
        s_addr[0] = s_port[0] = s_type[0] = 0;
        in_section = 1;
        return;
    }
    char *eq = l;
    while (*eq && *eq != '=') eq++;
    if (!*eq) return;
    char *v = eq + 1;
    *eq = 0;
    for (char *k = eq - 1; k >= l && (*k == ' ' || *k == '\t'); k--) *k = 0;
    while (*v == ' ' || *v == '\t') v++;
    if (ieq(l, "Address")) { if (in_section) copy(s_addr, v, ADDR_MAX); }
    else if (ieq(l, "Port")) copy(in_section ? s_port : d_port, v, sizeof s_port);
    else if (ieq(l, "ConnectionType")) copy(in_section ? s_type : d_type, v, sizeof s_type);
}

static void feed(const volatile uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        int c = b[i];
        if (c == '\n') { if (!line_long) do_line(); line_n = 0; line_long = 0; continue; }
        if (c >= 128) c = '?';                      /* Latin-1: the font is CP437 */
        else if (c < 32 && c != '\t' && c != '\r') c = ' ';
        if (line_n < (int)sizeof line - 1) line[line_n++] = (char)c;
        else line_long = 1;                         /* no line that long is one we use */
    }
}

static void progress(uint32_t off, uint32_t size)
{
    uint8_t bg = ATTR(7, 1);
    char b[48];
    int n = 0;
    for (const char *p = "Reading the BBS list: "; *p; p++) b[n++] = *p;
    uint32_t k = off / 1024, t = size / 1024;
    char d[12]; int m = 0;
    do { d[m++] = '0' + k % 10; k /= 10; } while (k);
    while (m) b[n++] = d[--m];
    for (const char *p = " of "; *p; p++) b[n++] = *p;
    do { d[m++] = '0' + t % 10; t /= 10; } while (t);
    while (m) b[n++] = d[--m];
    for (const char *p = " KiB"; *p; p++) b[n++] = *p;
    b[n] = 0;
    fill_row(3, 0, COLS, ' ', bg);
    text(3, 4, b, bg);
}

static void load(void)
{
    uint32_t size = IO_LSIZE;
    nent = nother = truncated = load_err = 0;
    pool_n = line_n = line_long = in_section = 0;
    copy(d_port, "23", sizeof d_port);
    copy(d_type, "Telnet", sizeof d_type);
    loaded = 1;
    loaded_size = size;
    if (!size) { load_err = -1; return; }
    for (uint32_t off = 0; off < size; off += LWIN_SIZE) {
        uint32_t len = size - off < LWIN_SIZE ? size - off : LWIN_SIZE;
        progress(off, size);
        IO_LOFF = off;
        IO_LLEN = len;
        IO_LCTL = 1;
        while (IO_LCTL & 1) ;
        int err = (IO_LCTL >> 1) & 7;
        if (err) { load_err = err; break; }
        feed(LWIN, (int)len);
        if (nent >= LIST_MAX || truncated) break;
    }
    if (line_n && !line_long) do_line();
    section_end();
}

/* ---------------- drawing ---------------- */

static const char *e_name(int i) { return pool + ent[i]; }
static const char *e_addr(int i) { const char *p = pool + ent[i]; while (*p) p++; return p + 1; }

static void num(char *b, int *n, uint32_t v)
{
    char d[12]; int m = 0;
    do { d[m++] = '0' + v % 10; v /= 10; } while (v);
    while (m) b[(*n)++] = d[--m];
}

static void draw(void)
{
    uint8_t bg = ATTR(7, 1), hi = ATTR(15, 4), title = ATTR(0, 7), dim = ATTR(11, 1);
    for (int r = 0; r < TROWS; r++) fill_row(r, 0, COLS, ' ', bg);
    fill_row(0, 0, COLS, ' ', title);
    text(0, 2, "ADDRESS BOOK > BBS LIST", title);
    if (!nent) {
        if (load_err == -1) {
            text(3, 4, "No BBS list. Copy syncterm.lst (from the Telnet BBS Guide's", bg);
            text(4, 4, "monthly list, telnetbbsguide.com: Lists, Download) to the SD card's", bg);
            text(5, 4, "Assets/netterm/common/ and start the core again.", bg);
        } else if (load_err) {
            char b[48]; int n = 0;
            for (const char *p = "The Pocket couldn't read the list: error "; *p; p++) b[n++] = *p;
            num(b, &n, (uint32_t)load_err);
            b[n] = 0;
            text(3, 4, b, bg);
        } else
            text(3, 4, "No Telnet entries in the BBS list.", bg);
        text(21, 2, "Esc (X) or Backspace (B): back to the address book.", dim);
        return;
    }
    char b[64]; int n = 0;
    num(b, &n, (uint32_t)sel + 1); b[n++] = '/'; num(b, &n, (uint32_t)nent);
    b[n] = 0;
    text(0, COLS - 2 - n, b, title);
    if (sel < top) top = sel;
    if (sel >= top + SHOWN) top = sel - SHOWN + 1;
    for (int k = top; k < nent && k < top + SHOWN; k++) {
        uint8_t a = k == sel ? hi : bg;
        int r = 2 + k - top;
        fill_row(r, 2, COLS - 2, ' ', a);
        text(r, 4, e_name(k), a);
        text(r, 40, e_addr(k), a);
    }
    n = 0;
    num(b, &n, (uint32_t)nent);
    for (const char *p = " Telnet BBSes"; *p; p++) b[n++] = *p;
    if (nother) {
        for (const char *p = " (and "; *p; p++) b[n++] = *p;
        num(b, &n, (uint32_t)nother);
        for (const char *p = " SSH or other, left out)"; *p; p++) b[n++] = *p;
    }
    if (truncated) for (const char *p = ", the first only"; *p; p++) b[n++] = *p;
    b[n] = 0;
    text(20, 2, msg[0] ? msg : b, msg[0] ? ATTR(14, 1) : dim);
    text(21, 2, "Enter (A): dial   Space (Y): copy to the address book   A-Z, 0-9: jump", dim);
    text(22, 2, "Left / Right: a page.  Esc (X) or Backspace (B): back to the address book.", dim);
}

/* ---------------- keys ---------------- */

void list_open(void)
{
    open_ = 1;
    msg[0] = 0;
    if (!loaded || IO_LSIZE != loaded_size) {   /* a new file chosen in the menu: read it again */
        uint8_t bg = ATTR(7, 1), title = ATTR(0, 7);
        for (int r = 0; r < TROWS; r++) fill_row(r, 0, COLS, ' ', bg);
        fill_row(0, 0, COLS, ' ', title);
        text(0, 2, "ADDRESS BOOK > BBS LIST", title);
        load();
        sel = top = 0;
    }
    if (sel >= nent) sel = nent ? nent - 1 : 0;
    draw();
}

void list_key(int k)
{
    msg[0] = 0;
    if (k == 27 || k == K_BKSP) { open_ = 0; return; }
    if (!nent) { draw(); return; }
    if (k == K_UP) { if (sel > 0) sel--; }
    else if (k == K_DOWN) { if (sel + 1 < nent) sel++; }
    else if (k == K_LEFT || k == K_PGUP) sel = sel > SHOWN ? sel - SHOWN : 0;
    else if (k == K_RIGHT || k == K_PGDN) { sel += SHOWN; if (sel >= nent) sel = nent - 1; }
    else if (k == K_HOME) sel = 0;
    else if (k == K_END) sel = nent - 1;
    else if (k == K_ENTER) {
        char ad[ADDR_MAX];
        copy(ad, e_addr(sel), ADDR_MAX);
        open_ = 0;
        book_close();
        modem_dial(ad);
        return;
    } else if (k == ' ') {
        int r = book_add(e_name(sel), e_addr(sel));
        copy(msg, r > 0 ? "Copied to the address book." : r == 0 ? "Already in the address book."
                        : "The address book is full (30 entries).", sizeof msg);
    } else if ((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) {
        int c = lower(k);
        for (int d = 1; d <= nent; d++) {
            int i = (sel + d) % nent;
            if (lower(e_name(i)[0]) == c) { sel = i; break; }
        }
    }
    draw();
}
