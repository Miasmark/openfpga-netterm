/* NetTerm: the hardware (src/fpga/core/nt_soc.sv). */
#ifndef HW_H
#define HW_H
#include <stdint.h>

#define REG(o)      (*(volatile uint32_t *)(0x20000000u + (o)))
#define SCR         ((volatile uint32_t *)0x10000000u)   /* cell n = row * 80 + col */

#define IO_ID       REG(0x00)       /* "NT01" */
#define IO_FRAMES   REG(0x04)       /* video frames, 60.2 per second */
#define IO_MS       REG(0x08)       /* milliseconds */
#define IO_CCOUNT   REG(0x0C)       /* controller snapshots taken */
#define IO_USTAT    REG(0x10)       /* [0] RX byte waiting, [1] TX full, [2] RX overflow (cleared by the read),
                                       [8] link SI level, [9] cartridge pin 31 level, [26:16] RX count */
#define IO_UDATA    REG(0x14)       /* read: [7:0] the next RX byte, [8] it was there; write: send [7:0] */
#define IO_UCTL     REG(0x18)       /* [19:0] clocks per bit, [22:21] RX port 0 auto / 1 link / 2 cartridge,
                                       [23] (read) the port in use: 0 link, 1 cartridge */
#define IO_UDIAG    REG(0x1C)       /* [15:0] RX start bits seen, [31:16] RX bytes dropped for a bad stop bit */
#define IO_KEY(n)   REG(0x20 + 4 * (n))     /* controllers 0-3: cont_key */
#define IO_JOY(n)   REG(0x30 + 4 * (n))     /* cont_joy */
#define IO_TRIG(n)  REG(0x40 + 4 * (n))     /* cont_trig */
#define IO_SET_LO   REG(0x50)       /* the menu's settings 0-3, a byte each */
#define IO_SET_HI   REG(0x54)       /* settings 4-7 */
#define IO_CURSOR   REG(0x60)       /* [6:0] column, [12:8] row, [16] shown */

#define CLK_HZ      49500000u

/* The menu's settings (dist/Cores/Miasmark.NetTerm/interact.json) */
enum { SET_MODE, SET_PORT, SET_BAUD, SET_ECHO, SET_ENTER, SET_BKSP, SET_COLOR, SET_7 };

static inline int setting(int n)
{
    uint32_t w = n < 4 ? IO_SET_LO : IO_SET_HI;
    return (w >> (8 * (n & 3))) & 0xFF;
}

static inline uint32_t ms(void) { return IO_MS; }

#endif
