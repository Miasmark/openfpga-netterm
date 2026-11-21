#!/bin/bash
# Build the firmware with clang / lld (RV32IM) into src/fpga/core/firmware.hex:
# 16,384 32-bit words, the RAM's initial contents ($readmemh).
set -e
cd "$(dirname "$0")"
OUT=../src/fpga/core/firmware.hex
mkdir -p build
CFLAGS="--target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 -Os -ffreestanding -fno-builtin -nostdlib -Wall -Wextra -Wno-unused-parameter -fno-pic -mno-relax"
for f in crt0.S lib.c screen.c term.c keys.c main.c; do
    clang $CFLAGS -c "$f" -o "build/${f%.*}.o"
done
ld.lld -m elf32lriscv -T link.ld --no-relax -o build/netterm.elf build/*.o
llvm-objcopy -O binary build/netterm.elf build/netterm.bin
python3 - build/netterm.bin "$OUT" <<'PY'
import sys
b = open(sys.argv[1], 'rb').read()
assert len(b) <= 60 * 1024, "firmware too large: %d bytes (the stack needs the top 4 KiB)" % len(b)
b += bytes(-len(b) % 4)
w = [int.from_bytes(b[i:i + 4], 'little') for i in range(0, len(b), 4)]
w += [0] * (16384 - len(w))
open(sys.argv[2], 'w').write('\n'.join('%08x' % x for x in w) + '\n')
print('firmware: %d bytes' % len(b))
PY
