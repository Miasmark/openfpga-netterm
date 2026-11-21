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
clk_sys 49.5 MHz:  PicoRV32 ── RAM 128 KiB (firmware) ── MMIO ── UART ── link SO/SI, cart pins 5/31
                        └──── screen RAM port A
clk_pix 12.375 MHz:              screen RAM port B ── nt_video ── 640 x 240 out
```

| Part | What |
|---|---|
| **CPU** | PicoRV32, RV32IM, no interrupts (the firmware polls). Reset vector 0, stack at the top of RAM. |
| **RAM** | 32 K x 32 (128 KiB; 64 KiB until 0.5.0), byte writes, loaded with `firmware.hex` at configuration. |
| **Save RAM** | 2 KiB behind the save slot (the address book): the bridge at `0x2000_0000`, the CPU at `0x3000_0000` (`nt_save_ram.sv`). |
| **List window** | 4 KiB (`nt_save_ram.sv` again): the bridge writes it at `0x4000_0000`, the CPU reads (and clears) it at `0x4000_0000`. |
| **List reads** | The BBS list's slot (0x101) is deferloaded. The firmware sets an offset and a length (at most 4 KiB) and writes GO; a clk_74a state machine raises `target_dataslot_read` (core_bridge_cmd sends target command 0x0180: slot 0x101, that offset, bridge address `0x4000_0000`, that length), waits for `target_dataslot_done` to drop and come back, and hands back `target_dataslot_err` (7 if nothing answers for 3.6 s). The file's size comes from the data table (slot index 1's size word, address 3), which core_top reads between its writes of the save slot's size. |
| **Screen RAM** | 4 K x 16, dual-clock; 80 x 30 cells used. Cell = `{bg[15:12], fg[11:8], char[7:0]}`. |
| **Video** | 640 x 240 visible of 784 x 262 at 12.375 MHz (60.2 Hz); 8 x 8 cells; code page 437 font (`tools/gen_font.py`, from font8x8); CGA's 16 colours; a blinking block cursor in hardware. The scaler shows it at 4:3. |
| **UART** | 8N1; the bit time is a 20-bit divider of 49.5 MHz (300 to 921,600 baud); 64-byte TX FIFO, 1 KiB RX FIFO. TX drives both the link port's SO and cartridge pin 5; RX comes from SI or pin 31: chosen, or Auto (the line that last fell while the other stayed high). |
| **Controllers** | All four controllers' words and the menu's 8 setting bytes cross from clk_74a as one request / acknowledge copy, so the firmware always reads one poll's words together. |

**core.json: `cartridge_adapter` must be 0** (the core uses the cartridge
port), as openFPGA-7800 has it. With -1 (0.1.0-0.1.3) the link port's SO
never drove: the transmitter sent (the line check's X counted) and no start
bit reached SI through a wire loop that the 7800 core's test passed on the
same Pocket. The link port's translators appear to share the cartridge
port's supply, which the Pocket leaves off for a core that says it doesn't
use the cartridge port. 0.1.4, with 0, works: the wire loop, and a PC
terminal both ways (2026-11-21).

**Memory map** (`firmware/hw.h`): RAM at `0x0000_0000`, screen at
`0x1000_0000` (a cell per word), MMIO at `0x2000_0000`: ID, frame and
millisecond counters, the UART's status / data / control, the controllers'
key / joy / trig words, the settings, the cursor, the list reads; the save
slot at `0x3000_0000`; the list window at `0x4000_0000`.

## The firmware (firmware/)

| File | What |
|---|---|
| `crt0.S`, `link.ld`, `lib.c` | Start-up, the memory layout, memset / memcpy (no C library). |
| `term.c` | The terminal: a VT100 / ANSI-BBS subset in rows 0-23 (see its header for the sequences). |
| `keys.c` | The Dock keyboard (HID codes to ASCII and VT sequences, repeat), controller 1, and the on-screen keyboard in rows 25-29. |
| `book.c` | The address book, in the save slot (data.json "Address Book", 2 KiB at 0x3000_0000 for the CPU, 0x2000_0000 for the bridge; `nt_save_ram.sv`). |
| `list.c` | The BBS list: reads a SyncTERM dialing directory (`syncterm.lst`, the Telnet BBS Guide's) 4 KiB at a time, keeps the Telnet entries (48 KiB pool, up to 1,536), browsing, dialling, copying into the book. |
| `scrollback.c` | The 200 lines scrolled off the top (32 KB of the RAM), and the view back through them. |
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
2. **FujiNet Modem mode** (0.2.0): FujiNet's RS232 build hands the bytes that
   come outside FujiBus frames to its modem (lib/bus/rs232/rs232.cpp,
   `_rs232_process_cmd`; lib/device/rs232/modem.cpp). In its command state
   the modem reads each burst (bytes until a 100 ms gap) as one command and
   prints nothing back for it (`cmdOutput` is off while `modemCommand` runs);
   online, the bytes pass to the Telnet connection. So: a local line editor
   until "CONNECT", straight through after, back on "NO CARRIER".
3. **FujiNet N: mode:** the firmware speaks FujiBus (SLIP frames
   at 115,200, as openFPGA-7800's `fujinet_mailbox.sv` does in hardware) to a
   FujiNet's network device: open `N:TELNET://host:port/` or `N:TCP://...`,
   then pass bytes both ways. (The dialling directory came first: the
   address book, 0.4.0, and the BBS list, 0.5.0.)
4. **Comfort:** scrollback (0.3.0); XMODEM / YMODEM to and from the save area; an
   8 x 16 font at 640 x 480 for the Dock; the mouse (pointing at the
   on-screen keyboard, selecting text); a VT220 keypad mode.
5. **Later, if FujiNet's firmware allows:** SSH (`N:SSH://`), a faster link
   for FujiNet mode.
