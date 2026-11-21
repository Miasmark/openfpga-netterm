#!/bin/bash
# Simulate the SoC with Verilator: builds the firmware, runs [seconds] (default
# 0.6) of made-up input, writes sim/work/frame.png (needs Python's PIL).
set -e
cd "$(dirname "$0")/.."
firmware/build.sh
python3 tools/gen_font.py >/dev/null
mkdir -p sim/work
C=src/fpga/core
verilator --cc --exe --build -O3 -Wno-fatal -Wno-WIDTH -Wno-UNUSED -Wno-PINCONNECTEMPTY -Wno-CASEINCOMPLETE \
  -Wno-UNOPTFLAT -Wno-MULTIDRIVEN -Wno-SYMRSVDWORD -Wno-TIMESCALEMOD \
  --top-module nt_soc -GFIRMWARE='"'$C'/firmware.hex"' -GFONT='"'$C'/font.hex"' \
  $C/third_party/picorv32/picorv32.v $C/nt_uart.sv $C/nt_video.sv $C/nt_save_ram.sv $C/nt_soc.sv "$PWD"/sim/tb.cpp \
  -Mdir sim/work/obj -o tb > sim/work/build.log 2>&1 || { tail -30 sim/work/build.log; exit 1; }
sim/work/obj/tb "$@"
python3 -c "from PIL import Image; Image.open('sim/work/frame.ppm').resize((1280,720), Image.NEAREST).save('sim/work/frame.png')" && echo sim/work/frame.png
