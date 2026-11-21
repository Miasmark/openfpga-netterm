// NetTerm simulation: the whole SoC (CPU, firmware, video), with the serial
// TX looped back to the link port's RX, a keyboard typing on controller 3
// and controller 1 opening the on-screen keyboard and typing a key from it.
// Writes the last whole frame to sim/work/frame.ppm.
#include "Vnt_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <deque>

static const int W = 640, H = 240;

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Vnt_soc *t = new Vnt_soc;
    double secs = argc > 1 ? atof(argv[1]) : 0.6;
    // ticks of 297 MHz: clk_74a every 4, clk_sys every 6, clk_pix every 24
    const uint64_t ticks = (uint64_t)(secs * 297e6);
    std::vector<uint32_t> fb(W * H), last(W * H);
    int x = 0, y = -1, frames = 0;
    bool in_frame = false;
    const char *ls = getenv("NT_SETTINGS");
    t->settings = ls ? strtoull(ls, 0, 16) : 0x0000000000070000ULL;   // serial, auto port, 115200
    t->cont1_key = 0x10000000;                // a Pocket pad
    t->reset = 1;
    // the keyboard: "hello" then Enter, 60 ms per key (NT_MODEM: "atdt bbs", Enter, "hi")
    const bool feed = getenv("NT_FEED") != nullptr;
    const bool book = getenv("NT_BOOK") != nullptr;     // Start: the address book, then A: dial    // NT_FEED=n: n numbered lines arrive, then R + Left, R + Up x2
    // NT_LIST=file: the BBS list slot holds that file. Start (the book), Right (the list),
    // then Down x3, Y (copy), and with NT_LIST_DIAL, A (dial) at NT_LIST_T ms (default 1500)
    const char *lpath = getenv("NT_LIST");
    std::vector<uint8_t> lfile;
    if (lpath) { FILE *lf = fopen(lpath, "rb"); int c; while (lf && (c = fgetc(lf)) != EOF) lfile.push_back(c); if (lf) fclose(lf); }
    const double lt = getenv("NT_LIST_T") ? atof(getenv("NT_LIST_T")) : 1500;
    t->list_size = lfile.size();
    t->tgt_done = 0; t->tgt_err = 0; t->lwin_wr = 0;
    int tg_st = 0, tg_i = 0, tg_wait = 0, tg_reads = 0; uint8_t tg_prev = 0; uint32_t tg_off = 0, tg_len = 0;
    const bool modem = getenv("NT_MODEM") != nullptr || feed || book || lpath;
    static const uint8_t typed_hello[] = {0x0B, 0x08, 0x0F, 0x0F, 0x12, 0x28, 0x1B, 0x1C, 0x1D};
    static const uint8_t typed_modem[] = {0x04, 0x17, 0x07, 0x17, 0x2C, 0x05, 0x05, 0x16, 0x28, 0x0B, 0x0C};
    // NT_BOOK_EDIT: in the book, Tab (add), "abc", Enter, "xy", Enter (save)
    static const uint8_t typed_edit[] = {0, 0, 0, 0x2B, 0x04, 0x05, 0x06, 0x28, 0x1B, 0x1C, 0x28};
    const bool bedit = getenv("NT_BOOK_EDIT") != nullptr;
    const uint8_t *typed = bedit ? typed_edit : modem ? typed_modem : typed_hello;
    const int ntyped = bedit ? sizeof typed_edit : (feed || book || lpath) ? 0 : modem ? sizeof typed_modem : sizeof typed_hello;
    // the fake FujiNet modem: one command per burst (a 100 ms gap ends it), silent in the
    // command state; ATDT -> "CONNECT 115200" and a greeting, then it echoes like a BBS
    const int BIT = 430 * 6;               // ticks per bit at 115,200
    int rx_state = -1, rx_bit = 0; uint64_t rx_t = 0; uint8_t rx_sh = 0, prev_txd = 1;
    std::string burst; uint64_t burst_t = 0; bool mon = false; int commands = 0;
    std::deque<uint8_t> out; int tx_bit = -1; uint64_t tx_t = 0; uint8_t tx_cur = 0, line = 1;
    if (feed) {
        mon = true;
        for (int i = 1; i <= atoi(getenv("NT_FEED")); i++) {
            char b[32]; snprintf(b, sizeof b, "line %02d of the feed\r\n", i);
            for (char *p = b; *p; p++) out.push_back(*p);
        }
    }
    for (uint64_t k = 0; k < ticks; k++) {
        double ms = k / 297e3;
        if (ms > 0.01) t->reset = 0;
        // controller 3: one key per 60 ms from 100 ms, held 30 ms
        int ki = (int)((ms - 100) / 60);
        uint32_t code = 0;
        if (ms >= 100 && ki < ntyped && (ms - 100 - ki * 60) < 30) code = typed[ki];
        t->cont3_key = 0x40000000;
        t->cont3_joy = code << 24;
        t->cont3_trig = 0;
        // controller 1: Select at 450 ms, Right at 480 and 510, A at 540
        uint32_t b = 0;
        if (lpath) {
            if (ms > 200 && ms < 210) b |= 1u << 15;                      // Start
            if (ms > 300 && ms < 310) b |= 1u << 3;                       // Right: the list
            const int downs = getenv("NT_LIST_DOWNS") ? atoi(getenv("NT_LIST_DOWNS")) : 3;
            for (int i = 0; i < downs && i < 3; i++) if (ms > lt + 40 * i && ms < lt + 40 * i + 10) b |= 1u << 1;   // Down
            if (downs > 3 && ms > lt + 120 && ms < lt + 130) b |= 1u << 3;   // more: Right, a page
            if (getenv("NT_LIST_BACK") && ms > lt + 250 && ms < lt + 260) b |= 1u << 5;   // B: back to the book
            if (ms > lt + 150 && ms < lt + 160) b |= 1u << 7;             // Y: copy
            if (getenv("NT_LIST_DIAL") && ms > lt + 250 && ms < lt + 260) b |= 1u << 4;   // A: dial
        } else if (book) {
            if (ms > 200 && ms < 210) b |= 1u << 15;                      // Start
            if (getenv("NT_BOOK_DIAL") && ms > 300 && ms < 310) b |= 1u << 4;   // A
        } else if (feed) {
            if (ms > 300 && ms < 600) b |= 1u << 9;                       // R held
            if (ms > 320 && ms < 330) b |= 1u << 2;                       // Left: a page back
            if ((ms > 400 && ms < 410) || (ms > 450 && ms < 460)) b |= 1u << 0;   // Up, Up
        } else {
        if (ms > 450 && ms < 460) b = 1u << 14;
        if ((ms > 480 && ms < 490) || (ms > 510 && ms < 520)) b = 1u << 3;
        if (ms > 540 && ms < 550) b = 1u << 4;
        }
        t->cont1_key = 0x10000000 | b;
        if (modem) {
            // receive from the core's TX
            uint8_t txd = t->txd;
            if (rx_state < 0 && prev_txd && !txd) { rx_state = 0; rx_t = k + BIT / 2; rx_bit = 0; rx_sh = 0; }
            prev_txd = txd;
            if (rx_state >= 0 && k >= rx_t) {
                if (rx_bit >= 1 && rx_bit <= 8) rx_sh |= txd << (rx_bit - 1);
                if (rx_bit == 9) {
                    rx_state = -1;
                    if (mon) out.push_back(rx_sh);                // online: the BBS echoes
                    else { burst += (char)rx_sh; burst_t = k; }
                } else { rx_bit++; rx_t += BIT; }
            }
            if (!mon && !burst.empty() && k - burst_t > (uint64_t)(0.1 * 297e6)) {
                commands++;
                if (getenv("NT_MODEM_LOG")) { fprintf(stderr, "burst %d at %.0f ms: [", commands, k / 297e3); for (char c : burst) fprintf(stderr, c >= 32 ? "%c" : "\\x%02x", (unsigned char)c); fprintf(stderr, "]\n"); }
                if (burst.rfind("ATDT", 0) == 0 || burst.rfind("atdt", 0) == 0) {
                    for (const char *p = "CONNECT 115200\r\nWelcome to the test BBS\r\n"; *p; p++) out.push_back(*p);
                    mon = true;
                }
                burst.clear();
            }
            // send to the core's RX
            if (tx_bit < 0 && !out.empty() && ms > 50) { tx_cur = out.front(); out.pop_front(); tx_bit = 0; tx_t = k; line = 0; }   // after the core is up
            if (tx_bit >= 0 && k >= tx_t + BIT) {
                tx_t += BIT; tx_bit++;
                line = tx_bit <= 8 ? (tx_cur >> (tx_bit - 1)) & 1 : 1;
                if (tx_bit == 10) tx_bit = -1;
            }
            t->rxd_link = line;
        } else
            t->rxd_link = getenv("NT_NOLOOP") ? 1 : t->txd;   // loopback
        t->rxd_cart = 1;
        // the Pocket's side of target command 0x0180 (core_bridge_cmd and the APF), on clk_74a's rising edges
        if (k % 4 == 0 && !t->clk_74a) {
            t->lwin_wr = 0;
            if (t->tgt_read && !tg_prev && tg_st == 0) { tg_st = 1; tg_wait = 200; tg_off = t->tgt_offset; tg_len = t->tgt_length; tg_reads++; }
            tg_prev = t->tgt_read;
            if (tg_st == 1 && --tg_wait == 0) { t->tgt_done = 0; tg_st = 2; tg_i = 0; tg_wait = 2000; }
            else if (tg_st == 2 && --tg_wait <= 0) {
                if (tg_off >= lfile.size() || lfile.empty()) { t->tgt_err = 2; t->tgt_done = 1; tg_st = 0; }
                else if (tg_i < (int)tg_len && tg_off + tg_i < lfile.size()) {
                    uint32_t w = 0;
                    for (int j = 0; j < 4; j++) w |= (uint32_t)(tg_off + tg_i + j < lfile.size() ? lfile[tg_off + tg_i + j] : 0) << (24 - 8 * j);
                    t->lwin_addr = (tg_i >> 2) & 1023; t->lwin_din = w; t->lwin_wr = 1;
                    tg_i += 4; tg_wait = 2;
                } else { t->tgt_err = 0; t->tgt_done = 1; tg_st = 0; }
            }
        }
        if (k % 2 == 0) t->clk_74a = !t->clk_74a;
        if (k % 3 == 0) t->clk_sys = !t->clk_sys;
        bool pix_rise = false;
        if (k % 12 == 0) { t->clk_pix = !t->clk_pix; pix_rise = t->clk_pix; }
        t->eval();
        if (pix_rise) {
            if (t->video_vs) {
                if (in_frame && y >= H) { last = fb; frames++; }
                in_frame = true; y = -1;
            }
            if (t->video_hs) { y++; x = 0; }
            if (t->video_de && in_frame) {
                int yy = y - 16;
                if (yy >= 0 && yy < H && x < W) fb[yy * W + x] = t->video_rgb;
                x++;
            }
        }
    }
    FILE *f = fopen("sim/work/frame.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (uint32_t p : last) { fputc(p >> 16, f); fputc(p >> 8 & 0xFF, f); fputc(p & 0xFF, f); }
    fclose(f);
    if (lpath) printf("list reads %d\n", tg_reads);
    printf("%d frames%s\n", frames, modem ? (std::string(", modem commands ") + std::to_string(commands)).c_str() : "");
    delete t;
    return 0;
}
