// NetTerm: the text video. 640 x 240 (80 x 30 cells of 8 x 8) out of
// 784 x 262 at 12.375 MHz: 60.2 Hz. Each cell is {bg[15:12], fg[11:8],
// char[7:0]} in the screen RAM; the font is code page 437 (tools/gen_font.py);
// the colours are CGA's sixteen. The cursor cell is drawn inverted, blinking
// (32 frames on, 32 off).
//
// Three stages from the counters: A the cell's address, B the cell (RAM) and
// the font row's address, C the font row (ROM); the output registers follow.
// hs, vs and de come from the same counters through the same stages.

`default_nettype none

module nt_video #(
    parameter FONT = "core/font.hex"
) (
    input  wire        clk,
    output logic [11:0] scr_a,
    input  wire [15:0] scr_q,
    input  wire  [6:0] cur_x,              // quasi-static, from clk_sys
    input  wire  [4:0] cur_y,
    input  wire        cur_on,
    output logic       vb_tog,             // toggles at each frame's start

    output logic [23:0] rgb,
    output logic       de,
    output logic       hs,
    output logic       vs
);

    localparam int H_TOTAL = 784, H_START = 96, H_ACTIVE = 640;
    localparam int V_TOTAL = 262, V_START = 16, V_ACTIVE = 240;

    logic [9:0] hc = '0;
    logic [8:0] vc = '0;
    logic [5:0] frame = '0;
    initial vb_tog = 1'b0;
    always_ff @(posedge clk) begin
        if (hc == H_TOTAL - 1) begin
            hc <= '0;
            if (vc == V_TOTAL - 1) begin
                vc <= '0;
                frame <= frame + 6'd1;
                vb_tog <= ~vb_tog;
            end else
                vc <= vc + 9'd1;
        end else
            hc <= hc + 10'd1;
    end

    // stage A
    wire        a_act = hc >= H_START && hc < H_START + H_ACTIVE && vc >= V_START && vc < V_START + V_ACTIVE;
    wire  [9:0] ax = hc - 10'(H_START);
    wire  [8:0] ay = vc - 9'(V_START);
    wire  [6:0] a_col = ax[9:3];
    wire  [4:0] a_row = ay[7:3];
    always_comb scr_a = 12'(a_row * 7'd80 + a_col);

    logic       b_act = 1'b0, c_act = 1'b0;
    logic       b_hs = 1'b0, b_vs = 1'b0, c_hs = 1'b0, c_vs = 1'b0;
    logic [2:0] b_px = '0, c_px = '0, b_fy = '0;
    logic       b_cur = 1'b0, c_cur = 1'b0;
    logic [7:0] c_attr = '0;

    // the font ROM
    logic [7:0] font [0:2047];
    initial $readmemh(FONT, font);
    logic [7:0] c_bits = '0;

    always_ff @(posedge clk) begin
        // A -> B
        b_act <= a_act;
        b_hs  <= hc == 10'd3;
        b_vs  <= hc == 10'd0 && vc == 9'd0;
        b_px  <= ax[2:0];
        b_fy  <= ay[2:0];
        b_cur <= cur_on && frame[5] && a_col == cur_x && a_row == cur_y;
        // B -> C: scr_q is the cell now
        c_act  <= b_act;
        c_hs   <= b_hs;
        c_vs   <= b_vs;
        c_px   <= b_px;
        c_cur  <= b_cur;
        c_attr <= scr_q[15:8];
        c_bits <= font[{scr_q[7:0], b_fy}];
    end

    function automatic [23:0] cga(input [3:0] c);
        unique case (c)
            4'h0: cga = 24'h000000;  4'h1: cga = 24'h0000AA;  4'h2: cga = 24'h00AA00;  4'h3: cga = 24'h00AAAA;
            4'h4: cga = 24'hAA0000;  4'h5: cga = 24'hAA00AA;  4'h6: cga = 24'hAA5500;  4'h7: cga = 24'hAAAAAA;
            4'h8: cga = 24'h555555;  4'h9: cga = 24'h5555FF;  4'hA: cga = 24'h55FF55;  4'hB: cga = 24'h55FFFF;
            4'hC: cga = 24'hFF5555;  4'hD: cga = 24'hFF55FF;  4'hE: cga = 24'hFFFF55;  default: cga = 24'hFFFFFF;
        endcase
    endfunction

    // C -> out
    wire ink = c_bits[3'd7 - c_px] ^ c_cur;
    always_ff @(posedge clk) begin
        de  <= c_act;
        hs  <= c_hs;
        vs  <= c_vs;
        rgb <= !c_act ? 24'd0 : ink ? cga(c_attr[3:0]) : cga(c_attr[7:4]);
    end

endmodule

`default_nettype wire
