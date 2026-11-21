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
    // the keyboard: "hello" then Enter, 60 ms per key
    const uint8_t typed[] = {0x0B, 0x08, 0x0F, 0x0F, 0x12, 0x28, 0x1B, 0x1C, 0x1D};
    for (uint64_t k = 0; k < ticks; k++) {
        double ms = k / 297e3;
        if (ms > 0.01) t->reset = 0;
        // controller 3: one key per 60 ms from 100 ms, held 30 ms
        int ki = (int)((ms - 100) / 60);
        uint32_t code = 0;
        if (ms >= 100 && ki < (int)sizeof typed && (ms - 100 - ki * 60) < 30) code = typed[ki];
        t->cont3_key = 0x40000000;
        t->cont3_joy = code << 24;
        t->cont3_trig = 0;
        // controller 1: Select at 450 ms, Right at 480 and 510, A at 540
        uint32_t b = 0;
        if (ms > 450 && ms < 460) b = 1u << 14;
        if ((ms > 480 && ms < 490) || (ms > 510 && ms < 520)) b = 1u << 3;
        if (ms > 540 && ms < 550) b = 1u << 4;
        t->cont1_key = 0x10000000 | b;
        t->rxd_link = getenv("NT_NOLOOP") ? 1 : t->txd;   // loopback
        t->rxd_cart = 1;
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
    printf("%d frames\n", frames);
    delete t;
    return 0;
}
