#
# NetTerm constraints (loaded by apf/apf_constraints.sdc)
#
# The PLL (ic|pll, tools/gen_pll.sh): counter 0 clk_sys (49.5 MHz, the CPU),
# counter 1 clk_pix (12.375 MHz, the video), counter 2 clk_pix at 90 degrees.
# clk_sys -> clk_pix: the screen RAM (a dual-clock block RAM) and the cursor
# registers, which only change between reads and are quasi-static to the
# video; clk_pix -> clk_sys: the frame toggle, through three flip-flops.
# clk_74a -> clk_sys: the controllers and settings, a request / acknowledge
# copy (nt_soc.sv). So every domain is its own asynchronous group.

set clk_sys {ic|pll|altera_pll_i|*|counter[0].output_counter|divclk}
set clk_pix {ic|pll|altera_pll_i|*|counter[1].output_counter|divclk ic|pll|altera_pll_i|*|counter[2].output_counter|divclk}

set_clock_groups -asynchronous \
 -group { bridge_spiclk } \
 -group { clk_74a } \
 -group { clk_74b } \
 -group [get_clocks $clk_sys] \
 -group [get_clocks $clk_pix]
