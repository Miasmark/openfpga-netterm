// NetTerm: the system on chip (docs/DESIGN.md).
//
// clk_sys (49.5 MHz): a PicoRV32 (RV32IM) with 64 KiB of RAM holding the
// firmware (firmware/, built to firmware.hex), the screen RAM's CPU port, the
// MMIO block and the serial port. clk_pix (12.375 MHz): the text video.
// clk_74a: the APF's controller words and the menu's settings, which reach
// clk_sys through a request / acknowledge copy (the 74a side holds its copy
// still until the sys side has taken it).
//
// Memory map (firmware/hw.h):
//   0x0000_0000  RAM, 64 KiB (code, data, stack; reset vector 0)
//   0x1000_0000  screen RAM: cell n = row * 80 + col at 0x1000_0000 + 4n,
//                bits [7:0] the CP437 character, [11:8] foreground, [15:12]
//                background (CGA colours); 80 x 30 cells
//   0x2000_0000  MMIO (below)
//   0x3000_0000  the save RAM, 2 KiB: the Pocket's save slot (nt_save_ram.sv)

`default_nettype none

module nt_soc #(
    parameter FIRMWARE = "core/firmware.hex",   // relative to src/fpga (Quartus' project directory)
    parameter FONT     = "core/font.hex"
) (
    input  wire        clk_sys,
    input  wire        reset,          // clk_sys, active high
    input  wire        clk_74a,
    input  wire        clk_pix,

    // clk_74a: the controllers and the menu's settings (8 x 8 bits)
    input  wire [31:0] cont1_key, cont2_key, cont3_key, cont4_key,
    input  wire [31:0] cont1_joy, cont2_joy, cont3_joy, cont4_joy,
    input  wire [15:0] cont1_trig, cont2_trig, cont3_trig, cont4_trig,
    input  wire [63:0] settings,

    // the save slot's RAM, bridge side (clk_74a; nt_save_ram.sv)
    input  wire  [8:0] save_addr,
    input  wire [31:0] save_din,
    input  wire        save_wr,
    input  wire        save_rd,
    output wire [31:0] save_dout,

    // the serial port: TX goes out on both ports; RX from the one chosen
    output wire        txd,
    input  wire        rxd_link,
    input  wire        rxd_cart,

    // video (clk_pix)
    output wire [23:0] video_rgb,
    output wire        video_de,
    output wire        video_hs,
    output wire        video_vs
);

    // ------------------------------------------------------------------
    // The controllers and settings: clk_74a -> clk_sys
    // ------------------------------------------------------------------
    localparam int CW = 4 * 32 + 4 * 32 + 4 * 16 + 64;
    wire  [CW-1:0] c_live = {settings, cont4_trig, cont3_trig, cont2_trig, cont1_trig,
                             cont4_joy, cont3_joy, cont2_joy, cont1_joy,
                             cont4_key, cont3_key, cont2_key, cont1_key};
    logic [CW-1:0] c_hold = '0;            // clk_74a, still while req != ack
    logic          c_req = 1'b0;
    logic [2:0]    c_ack_s = '0;           // clk_74a's view of c_ack
    always_ff @(posedge clk_74a) begin
        c_ack_s <= {c_ack_s[1:0], c_ack};
        if (c_req == c_ack_s[2]) begin
            c_hold <= c_live;
            c_req  <= ~c_req;
        end
    end
    logic [CW-1:0] c_sys = '0;             // clk_sys
    logic          c_ack = 1'b0;
    logic [2:0]    c_req_s = '0;
    logic [31:0]   c_count = '0;
    always_ff @(posedge clk_sys) begin
        c_req_s <= {c_req_s[1:0], c_req};
        if (c_req_s[2] != c_ack) begin
            c_sys   <= c_hold;
            c_ack   <= ~c_ack;
            c_count <= c_count + 32'd1;
        end
    end
    wire [31:0] key  [4];
    wire [31:0] joy  [4];
    wire [15:0] trig [4];
    genvar gi;
    generate for (gi = 0; gi < 4; gi++) begin : unpack
        assign key[gi]  = c_sys[32 * gi +: 32];
        assign joy[gi]  = c_sys[128 + 32 * gi +: 32];
        assign trig[gi] = c_sys[256 + 16 * gi +: 16];
    end endgenerate
    wire [63:0] set_sys = c_sys[320 +: 64];

    // ------------------------------------------------------------------
    // The CPU
    // ------------------------------------------------------------------
    wire        mem_valid, mem_instr;
    logic       mem_ready = 1'b0;
    wire [31:0] mem_addr, mem_wdata;
    wire [3:0]  mem_wstrb;
    logic [31:0] mem_rdata = '0;

    picorv32 #(
        .ENABLE_COUNTERS     (1),
        .ENABLE_COUNTERS64   (1),
        .ENABLE_REGS_DUALPORT(1),
        .BARREL_SHIFTER      (1),
        .COMPRESSED_ISA      (0),
        .ENABLE_MUL          (1),
        .ENABLE_DIV          (1),
        .ENABLE_IRQ          (0),
        .CATCH_MISALIGN      (1),
        .CATCH_ILLINSN       (1),
        .PROGADDR_RESET      (32'h0000_0000),
        .STACKADDR           (32'h0001_0000)
    ) cpu (
        .clk        (clk_sys),
        .resetn     (~reset),
        .trap       (),
        .mem_valid  (mem_valid),
        .mem_instr  (mem_instr),
        .mem_ready  (mem_ready),
        .mem_addr   (mem_addr),
        .mem_wdata  (mem_wdata),
        .mem_wstrb  (mem_wstrb),
        .mem_rdata  (mem_rdata),
        .mem_la_read (), .mem_la_write (), .mem_la_addr (), .mem_la_wdata (), .mem_la_wstrb (),
        .pcpi_valid (), .pcpi_insn (), .pcpi_rs1 (), .pcpi_rs2 (),
        .pcpi_wr    (1'b0), .pcpi_rd (32'd0), .pcpi_wait (1'b0), .pcpi_ready (1'b0),
        .irq        (32'd0),
        .eoi        (),
        .trace_valid (), .trace_data ()
    );

    wire sel_ram = mem_addr[31:28] == 4'h0;
    wire sel_scr = mem_addr[31:28] == 4'h1;
    wire sel_io  = mem_addr[31:28] == 4'h2;
    wire sel_sav = mem_addr[31:28] == 4'h3;
    wire go      = mem_valid && !mem_ready;

    // RAM: 16 K words, byte writes (packed bytes: Quartus' byte-enable form)
    logic [3:0][7:0] ram [0:16383];
    initial $readmemh(FIRMWARE, ram);
    logic [31:0] ram_q;
    wire  [13:0] ram_a = mem_addr[15:2];
    always_ff @(posedge clk_sys) begin
        if (go && sel_ram) begin
            if (mem_wstrb[0]) ram[ram_a][0] <= mem_wdata[7:0];
            if (mem_wstrb[1]) ram[ram_a][1] <= mem_wdata[15:8];
            if (mem_wstrb[2]) ram[ram_a][2] <= mem_wdata[23:16];
            if (mem_wstrb[3]) ram[ram_a][3] <= mem_wdata[31:24];
        end
        ram_q <= ram[ram_a];
    end

    // Screen RAM: port A here (clk_sys, read / write), port B the video's
    logic [15:0] scr [0:4095];
    logic [15:0] scr_qa;
    wire  [11:0] scr_aa = mem_addr[13:2];
    always_ff @(posedge clk_sys) begin
        if (go && sel_scr && mem_wstrb[0]) scr[scr_aa] <= mem_wdata[15:0];
        scr_qa <= scr[scr_aa];
    end
    // The save RAM
    wire  [31:0] sav_q;
    nt_save_ram save_ram (
        .clk_a  (clk_sys),
        .addr_a (mem_addr[10:2]),
        .din_a  (mem_wdata),
        .we_a   (go && sel_sav ? mem_wstrb : 4'd0),
        .dout_a (sav_q),
        .clk_b  (clk_74a),
        .addr_b (save_addr),
        .din_b  (save_din),
        .we_b   (save_wr),
        .rd_b   (save_rd),
        .dout_b (save_dout)
    );

    wire  [11:0] scr_ab;
    logic [15:0] scr_qb;
    always_ff @(posedge clk_pix) scr_qb <= scr[scr_ab];

    // ------------------------------------------------------------------
    // MMIO (0x2000_0000)
    // ------------------------------------------------------------------
    localparam [31:0] ID = 32'h4E54_3031;   // "NT01"

    // a millisecond counter
    logic [15:0] ms_pre = '0;
    logic [31:0] ms = '0;
    always_ff @(posedge clk_sys) begin
        if (ms_pre == 16'd49499) begin
            ms_pre <= '0;
            ms <= ms + 32'd1;
        end else
            ms_pre <= ms_pre + 16'd1;
    end

    // frames: the video's vertical blank, counted in clk_sys
    wire        vb_tog;                     // clk_pix: toggles each frame
    logic [2:0] vb_s = '0;
    logic [31:0] frames = '0;
    always_ff @(posedge clk_sys) begin
        vb_s <= {vb_s[1:0], vb_tog};
        if (vb_s[2] != vb_s[1]) frames <= frames + 32'd1;
    end

    // the serial port
    logic [19:0] u_div = 20'd430;           // 49.5 MHz / 115,200
    logic [1:0]  u_mode = 2'd0;             // RX from: 0 auto, 1 the link port, 2 the cartridge, 3 TX itself (a test)
    logic        u_port = 1'b0;             // the port in use: 0 link, 1 cartridge
    logic        u_push = 1'b0, u_pop = 1'b0;
    logic [7:0]  u_wdata = '0;
    wire  [7:0]  u_rdata;
    wire         u_avail, u_full, u_ovf;
    wire  [10:0] u_count;
    wire  [15:0] u_starts, u_ferrs, u_tsent;
    logic        u_ovf_clr = 1'b0;
    logic [2:0]  rl_s = 3'b111, rc_s = 3'b111;
    // auto: the port whose line last fell (a start bit) while the other
    // stayed high; set by hand, the one chosen
    always_ff @(posedge clk_sys) begin
        rl_s <= {rl_s[1:0], rxd_link};
        rc_s <= {rc_s[1:0], rxd_cart};
        unique case (u_mode)
            2'd1:    u_port <= 1'b0;
            2'd2:    u_port <= 1'b1;
            default: if (rl_s[2] && !rl_s[1] && rc_s[1]) u_port <= 1'b0;
                     else if (rc_s[2] && !rc_s[1] && rl_s[1]) u_port <= 1'b1;
        endcase
    end
    nt_uart uart (
        .clk     (clk_sys),
        .reset   (reset),
        .div     (u_div),
        .txd     (txd),
        .rxd     (u_mode == 2'd3 ? txd : u_port ? rc_s[2] : rl_s[2]),
        .push    (u_push),
        .wdata   (u_wdata),
        .full    (u_full),
        .pop     (u_pop),
        .rdata   (u_rdata),
        .avail   (u_avail),
        .count   (u_count),
        .ovf     (u_ovf),
        .ovf_clr (u_ovf_clr),
        .tsent   (u_tsent),
        .starts  (u_starts),
        .ferrs   (u_ferrs)
    );

    // the cursor (read by the video, quasi-static)
    logic [6:0] cur_x = '0;
    logic [4:0] cur_y = '0;
    logic       cur_on = 1'b0;

    logic [31:0] io_q;
    always_comb begin
        unique case (mem_addr[7:2])
            6'h00: io_q = ID;
            6'h01: io_q = frames;
            6'h02: io_q = ms;
            6'h03: io_q = c_count;
            6'h04: io_q = {5'd0, u_count, 4'd0, rc_s[2], rl_s[2], 5'd0, u_ovf, u_full, u_avail};
            6'h05: io_q = {23'd0, u_avail, u_rdata};
            6'h06: io_q = {8'd0, u_port, u_mode, 1'b0, u_div};
            6'h07: io_q = {u_ferrs, u_starts};
            6'h08: io_q = key[0];
            6'h09: io_q = key[1];
            6'h0A: io_q = key[2];
            6'h0B: io_q = key[3];
            6'h0C: io_q = joy[0];
            6'h0D: io_q = joy[1];
            6'h0E: io_q = joy[2];
            6'h0F: io_q = joy[3];
            6'h10: io_q = {16'd0, trig[0]};
            6'h11: io_q = {16'd0, trig[1]};
            6'h12: io_q = {16'd0, trig[2]};
            6'h13: io_q = {16'd0, trig[3]};
            6'h14: io_q = set_sys[31:0];
            6'h15: io_q = set_sys[63:32];
            6'h18: io_q = {15'd0, cur_on, 3'd0, cur_y, 1'b0, cur_x};
            6'h19: io_q = {16'd0, u_tsent};
            default: io_q = 32'd0;
        endcase
    end

    always_ff @(posedge clk_sys) begin
        mem_ready <= 1'b0;
        u_push    <= 1'b0;
        u_pop     <= 1'b0;
        u_ovf_clr <= 1'b0;
        if (go) begin
            mem_ready <= 1'b1;
            if (sel_io && |mem_wstrb) begin
                unique case (mem_addr[7:2])
                    6'h05: begin u_wdata <= mem_wdata[7:0]; u_push <= 1'b1; end
                    6'h06: begin u_div <= mem_wdata[19:0]; u_mode <= mem_wdata[22:21]; end
                    6'h18: begin cur_x <= mem_wdata[6:0]; cur_y <= mem_wdata[12:8]; cur_on <= mem_wdata[16]; end
                    default: ;
                endcase
            end
            if (sel_io && !(|mem_wstrb)) begin
                if (mem_addr[7:2] == 6'h05 && u_avail) u_pop <= 1'b1;
                if (mem_addr[7:2] == 6'h04 && u_ovf) u_ovf_clr <= 1'b1;
            end
        end
        if (reset) begin
            u_div  <= 20'd430;
            u_mode <= 2'd0;
            cur_on <= 1'b0;
        end
    end

    // read data: one clock after the request, with mem_ready
    logic [1:0]  rsel = '0;
    logic [31:0] io_qr = '0;
    always_ff @(posedge clk_sys) begin
        rsel  <= sel_scr ? 2'd1 : sel_io ? 2'd2 : sel_sav ? 2'd3 : 2'd0;
        io_qr <= io_q;
    end
    always_comb begin
        unique case (rsel)
            2'd1:    mem_rdata = {16'd0, scr_qa};
            2'd2:    mem_rdata = io_qr;
            2'd3:    mem_rdata = sav_q;
            default: mem_rdata = ram_q;
        endcase
    end

    // ------------------------------------------------------------------
    // The video (clk_pix)
    // ------------------------------------------------------------------
    nt_video #(.FONT(FONT)) video (
        .clk     (clk_pix),
        .scr_a   (scr_ab),
        .scr_q   (scr_qb),
        .cur_x   (cur_x),
        .cur_y   (cur_y),
        .cur_on  (cur_on),
        .vb_tog  (vb_tog),
        .rgb     (video_rgb),
        .de      (video_de),
        .hs      (video_hs),
        .vs      (video_vs)
    );

endmodule

`default_nettype wire
