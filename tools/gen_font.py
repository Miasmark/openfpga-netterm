#!/usr/bin/env python3
"""Build the terminal's 256-glyph 8 x 8 font, in code page 437 order, from
Daniel Hepper's public-domain font8x8 (tools/font8x8/).

Writes src/fpga/core/font.hex: 2048 bytes, glyph g's row r at g * 8 + r, bit 7
the leftmost pixel (font8x8 keeps bit 0 leftmost; flipped here). Code page 437
byte -> Unicode -> font8x8 glyph; what font8x8 doesn't have (CP437's 0x01-0x1F
symbols, a few math signs) is left blank or drawn with a near glyph, listed in
MISSING when the script runs.

Usage: python3 tools/gen_font.py
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'font8x8')
OUT = os.path.join(HERE, '..', 'src', 'fpga', 'core', 'font.hex')

# CP437 0x80-0xFF as Unicode
CP437_HI = [
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
]
# Near substitutes for code points font8x8 lacks
NEAR = {0x2219: 0x00B7, 0x25A0: 0x2588}


def load():
    glyphs = {}
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith('.h'):
            continue
        for line in open(os.path.join(SRC, fn)):
            m = re.search(r'\{([^}]*)\}.*U\+([0-9A-Fa-f]{4})', line)
            if m:
                rows = [int(v, 16) for v in m.group(1).replace(' ', '').split(',') if v]
                if len(rows) == 8:
                    glyphs.setdefault(int(m.group(2), 16), rows)
            elif '(less than or equal)' in line or '(greater than or equal)' in line:
                rows = [int(v, 16) for v in re.search(r'\{([^}]*)\}', line).group(1).replace(' ', '').split(',')]
                glyphs.setdefault(0x2264 if 'less' in line else 0x2265, rows)
    return glyphs


def main():
    glyphs = load()
    missing = []
    out = []
    for b in range(256):
        if b < 0x20:
            cp = None                       # CP437's control-range symbols: blank
        elif b < 0x80:
            cp = b
        else:
            cp = CP437_HI[b - 0x80]
        rows = [0] * 8
        if cp is not None:
            g = glyphs.get(cp) or glyphs.get(NEAR.get(cp, -1))
            if g:
                rows = g
            else:
                missing.append('%02X U+%04X' % (b, cp))
        for r in rows:
            out.append('%02X' % int('{:08b}'.format(r)[::-1], 2))   # bit 7 = leftmost
    open(OUT, 'w').write('\n'.join(out) + '\n')
    print(os.path.relpath(OUT), '256 glyphs; missing:', ', '.join(missing) or 'none')


if __name__ == '__main__':
    main()
