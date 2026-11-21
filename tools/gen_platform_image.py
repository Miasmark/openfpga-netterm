#!/usr/bin/env python3
"""Draw NetTerm's platform image: a generic 1970s-80s video terminal (a CRT
monitor with a separate keyboard), monochrome pixel art, light on black like
openFPGA-7800's.

Writes dist/Platforms/_images/netterm.bin in the Pocket's format (Analogue's
"Graphical Asset Formats"): 521 x 165, 16 bits a pixel with the brightness in
the upper byte (in the file: the brightness byte, then 0), stored rotated
90 degrees counter-clockwise; and docs/platform-image.png, the same picture
upright, to look at.

Usage: python3 tools/gen_platform_image.py   (needs Python's PIL)
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
W, H = 521, 165

# grey levels
BLACK, DARK, MID, LIGHT, BRIGHT, WHITE = 0, 60, 110, 170, 215, 255


def draw():
    im = Image.new('L', (W, H), BLACK)
    d = ImageDraw.Draw(im)
    cx = W // 2

    # ---- the monitor: a deep case seen a little from the left ----
    mx0, mx1, my0, my1 = cx - 92, cx + 92, 6, 112        # the front face
    depth = 22                                           # the side, to the right
    # side and top (in shade)
    d.polygon([(mx1, my0 + 4), (mx1 + depth, my0 + 12), (mx1 + depth, my1 - 6), (mx1, my1)], fill=MID)
    d.polygon([(mx0 + 4, my0), (mx1 - 4, my0), (mx1 + depth - 4, my0 + 8), (mx0 + depth + 6, my0 + 8)], fill=LIGHT)
    d.polygon([(mx0 + 4, my0), (mx1 - 4, my0), (mx1, my0 + 4), (mx1 + depth, my0 + 12), (mx1 + depth - 4, my0 + 8)],
              fill=LIGHT)
    # the front face, rounded
    d.rounded_rectangle([mx0, my0, mx1, my1], radius=8, fill=BRIGHT)
    # vents along the top of the side
    for i in range(6):
        x = mx1 + 5 + i * 3
        d.line([(x, my0 + 16 + i), (x, my0 + 30 + i)], fill=DARK)
    # the bezel and the screen (left of centre, as on the classic terminals)
    sx0, sx1, sy0, sy1 = mx0 + 12, mx1 - 40, my0 + 10, my1 - 14
    d.rounded_rectangle([sx0 - 4, sy0 - 4, sx1 + 4, sy1 + 4], radius=10, fill=MID)
    d.rounded_rectangle([sx0, sy0, sx1, sy1], radius=12, fill=BLACK)
    # a reflection on the glass
    d.arc([sx0 + 4, sy0 + 3, sx0 + 40, sy0 + 30], 190, 260, fill=DARK, width=2)
    # text on the screen
    ty = sy0 + 12
    lines = [26, 40, 18, 33, 0, 12]
    for n, length in enumerate(lines):
        y = ty + n * 9
        if length:
            for k in range(length):
                if (k * 7 + n * 3) % 11 != 0:            # gaps between words
                    d.rectangle([sx0 + 12 + k * 3, y, sx0 + 13 + k * 3, y + 4], fill=WHITE)
    # the cursor
    y = ty + 5 * 9
    d.rectangle([sx0 + 12 + 12 * 3 + 3, y, sx0 + 12 + 12 * 3 + 6, y + 5], fill=WHITE)
    # the controls column, right of the screen
    for i in range(3):
        d.ellipse([mx1 - 28, my0 + 22 + i * 16, mx1 - 20, my0 + 30 + i * 16], fill=MID)
    d.rectangle([mx1 - 30, my1 - 26, mx1 - 14, my1 - 22], fill=MID)  # a badge
    # the plinth under the monitor
    d.polygon([(mx0 + 18, my1), (mx1 - 18, my1), (mx1 - 8, my1 + 8), (mx0 + 8, my1 + 8)], fill=MID)

    # ---- the keyboard, in front, a wedge seen from above ----
    ky0, ky1 = 124, 160
    kl0, kr0 = cx - 128, cx + 128        # back edge
    kl1, kr1 = cx - 146, cx + 146        # front edge
    d.polygon([(kl0, ky0), (kr0, ky0), (kr1, ky1), (kl1, ky1)], fill=BRIGHT)
    d.polygon([(kl1, ky1), (kr1, ky1), (kr1 - 2, ky1 + 3), (kl1 + 2, ky1 + 3)], fill=MID)   # front lip
    # key rows: each row a little wider towards the front
    rows = 5
    for r in range(rows):
        t0 = (r + 0.15) / rows
        t1 = (r + 0.85) / rows
        y0 = round(ky0 + 3 + t0 * (ky1 - ky0 - 6))
        y1 = round(ky0 + 3 + t1 * (ky1 - ky0 - 6))
        l = kl0 + (kl1 - kl0) * (t0 + t1) / 2 + 8
        rr = kr0 + (kr1 - kr0) * (t0 + t1) / 2 - 8
        span = rr - l
        main = span * 0.78                        # the main block, then the keypad
        n = 14 - (r == rows - 1) * 0
        kw = main / n
        for k in range(n):
            x0 = l + k * kw
            if r == rows - 1 and 3 <= k <= 10:   # the space bar
                if k == 3:
                    d.rectangle([round(x0 + 1), y0, round(l + 11 * kw - 2), y1], fill=MID)
                continue
            d.rectangle([round(x0 + 1), y0, round(x0 + kw - 2), y1], fill=MID)
        pad0 = l + main + 6
        pw = (rr - pad0) / 4
        for k in range(4):
            d.rectangle([round(pad0 + k * pw + 1), y0, round(pad0 + (k + 1) * pw - 2), y1], fill=MID)
    return im


def main():
    im = draw()
    os.makedirs(os.path.join(ROOT, 'docs'), exist_ok=True)
    im.resize((W * 2, H * 2), Image.NEAREST).save(os.path.join(ROOT, 'docs', 'platform-image.png'))
    stored = im.rotate(90, expand=True)              # 90 degrees counter-clockwise: 165 x 521
    out = bytearray()
    for v in stored.tobytes():
        out += bytes((v, 0))
    p = os.path.join(ROOT, 'dist', 'Platforms', '_images', 'netterm.bin')
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(out)
    print(os.path.relpath(p), len(out), 'bytes')


if __name__ == '__main__':
    main()
