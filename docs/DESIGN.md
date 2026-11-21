# NetTerm: design

## The idea

A terminal only needs a serial line. NetTerm is that line's far end, on the
Pocket: a screen, a keyboard, and a UART on the link port and the cartridge
slot. What is on the other end is up to the user: a computer, a WiFi modem
dialling Telnet BBSs, or (milestone 2) a FujiNet speaking FujiBus, whose
network device does TCP, Telnet and TLS itself.

It came out of the FujiNet Pocket cartridge's scoping note
(fujinet-hardware-analogue-pocket, `Analogue-Pocket/FujiNet-Pocket-Rev0/docs/NET_CORES_SCOPING.md`,
phase 1). The Dock's keyboard and mouse formats used here were checked on
hardware with openfpga-input-monitor.

## The hardware (src/fpga/core)

```
clk_74a ── controllers, settings ──(req/ack copy)──┐
                                                   v
clk_sys 49.5 MHz:  PicoRV32 ── RAM 64 KiB (firmware) ── MMIO ── UART ── link SO/SI, cart pins 5/31
                        └──── screen RAM port A
clk_pix 12.375 MHz:              screen RAM port B ── nt_video ── 640 x 240 out
```

| Part | What |
|---|---|
| **CPU** | PicoRV32, RV32IM, no interrupts (the firmware polls). Reset vector 0, stack at the top of RAM. |
| **RAM** | 16 K x 32, byte writes, loaded with `firmware.hex` at configuration. |
| **Screen RAM** | 4 K x 16, dual-clock; 80 x 30 cells used. Cell = `{bg[15:12], fg[11:8], char[7:0]}`. |
| **Video** | 640 x 240 visible of 784 x 262 at 12.375 MHz (60.2 Hz); 8 x 8 cells; code page 437 font (`tools/gen_font.py`, from font8x8); CGA's 16 colours; a blinking block cursor in hardware. The scaler shows it at 4:3. |
| **UART** | 8N1; the bit time is a 20-bit divider of 49.5 MHz (300 to 921,600 baud); 64-byte TX FIFO, 1 KiB RX FIFO. TX drives both the link port's SO and cartridge pin 5; RX comes from SI or pin 31: chosen, or Auto (the line that last fell while the other stayed high). |
| **Controllers** | All four controllers' words and the menu's 8 setting bytes cross from clk_74a as one request / acknowledge copy, so the firmware always reads one poll's words together. |

**Memory map** (`firmware/hw.h`): RAM at `0x0000_0000`, screen at
`0x1000_0000` (a cell per word), MMIO at `0x2000_0000`: ID, frame and
millisecond counters, the UART's status / data / control, the controllers'
key / joy / trig words, the settings, the cursor.

## The firmware (firmware/)

| File | What |
|---|---|
| `crt0.S`, `link.ld`, `lib.c` | Start-up, the memory layout, memset / memcpy (no C library). |
| `term.c` | The terminal: a VT100 / ANSI-BBS subset in rows 0-23 (see its header for the sequences). |
| `keys.c` | The Dock keyboard (HID codes to ASCII and VT sequences, repeat), controller 1, and the on-screen keyboard in rows 25-29. |
| `main.c` | The loop: the UART into the terminal, the keys out; the menu's settings applied when they change; the status line (row 24). |

**The Dock's keyboard** (controller 3, type 4): six HID usage codes, keys 1-4
in `joy[31:24]`..`joy[7:0]`, keys 5-6 in `trig[15:8]`, `trig[7:0]`; the
modifier byte in `key[15:8]`.

**The Dock's mouse** (controller 4, type 5; not used yet): a report counter
in `key[15:0]`; X in `joy[15:0]` and Y in `trig[15:0]`, signed, each with its
low byte in bits [15:8]; buttons in `joy[20:16]`; wheel clicks arrive as one
count of X.

## Milestones

1. **Serial terminal** (0.1): this.
2. **FujiNet mode:** a Mode setting. The firmware speaks FujiBus (SLIP frames
   at 115,200, as openFPGA-7800's `fujinet_mailbox.sv` does in hardware) to a
   FujiNet's network device: open `N:TELNET://host:port/` or `N:TCP://...`,
   then pass bytes both ways. A dialling directory, kept in a save file.
3. **Comfort:** scrollback; XMODEM / YMODEM to and from the save area; an
   8 x 16 font at 640 x 480 for the Dock; the mouse (pointing at the
   on-screen keyboard, selecting text); a VT220 keypad mode.
4. **Later, if FujiNet's firmware allows:** SSH (`N:SSH://`), a faster link
   for FujiNet mode.
