// NetTerm: the serial port. 8N1, no flow control; the bit time is div clocks
// (clk / baud: 49.5 MHz / 300 = 165,000 fits the 20 bits). TX from a 64-byte FIFO, RX into a 1 KiB FIFO; ovf is set when a
// byte arrives with the RX FIFO full (the byte is dropped), until ovf_clr.
// rxd must already be synchronised to clk.

`default_nettype none

module nt_uart (
    input  wire        clk,
    input  wire        reset,
    input  wire [19:0] div,

    output logic       txd,
    input  wire        rxd,

    input  wire        push,
    input  wire  [7:0] wdata,
    output wire        full,

    input  wire        pop,
    output wire  [7:0] rdata,
    output wire        avail,
    output wire [10:0] count,
    output logic       ovf,
    input  wire        ovf_clr,
    output logic [15:0] starts,             // start bits seen (diagnostics; wrap)
    output logic [15:0] ferrs               // bytes dropped for a bad stop bit
);

    // ---------------- TX ----------------
    logic [7:0] tq [0:63];
    logic [5:0] tw = '0, tr = '0;
    wire  [6:0] tn = {1'b0, tw} - {1'b0, tr};
    assign full = tw + 6'd1 == tr;
    always_ff @(posedge clk) if (push && !full) tq[tw] <= wdata;

    logic [19:0] tcnt = '0;
    logic [3:0]  tbit = '0;                 // 0 idle, 1 start, 2-9 data, 10 stop
    logic [7:0]  tsh = '0;
    initial txd = 1'b1;
    always_ff @(posedge clk) begin
        if (push && !full) tw <= tw + 6'd1;
        if (tbit == 4'd0) begin
            txd <= 1'b1;
            if (tw != tr) begin
                tsh  <= tq[tr];
                tr   <= tr + 6'd1;
                tbit <= 4'd1;
                tcnt <= div - 20'd1;
                txd  <= 1'b0;
            end
        end else if (tcnt != 0)
            tcnt <= tcnt - 20'd1;
        else begin
            tcnt <= div - 20'd1;
            if (tbit == 4'd10)
                tbit <= 4'd0;
            else begin
                tbit <= tbit + 4'd1;
                if (tbit == 4'd9)
                    txd <= 1'b1;            // stop bit
                else begin
                    txd <= tsh[0];
                    tsh <= {1'b0, tsh[7:1]};
                end
            end
        end
        if (reset) begin
            tw <= '0; tr <= '0; tbit <= '0; txd <= 1'b1;
        end
    end

    // ---------------- RX ----------------
    logic [7:0]  rq [0:1023];
    logic [9:0]  rw = '0, rr = '0;
    assign count = {1'b0, rw} - {1'b0, rr};
    assign avail = rw != rr;
    logic [7:0]  rq_q;
    always_ff @(posedge clk) rq_q <= rq[rr];
    // rdata: the head byte; rq_q follows rr one clock later, which the bus
    // never sees (a read is answered a clock after it starts)
    assign rdata = rq_q;

    logic [19:0] rcnt = '0;
    logic [3:0]  rbit = '0;                 // 0 idle, 1 start, 2-9 data, 10 stop
    logic [7:0]  rsh = '0;
    logic        rwe = 1'b0;
    logic [7:0]  rwd = '0;
    initial ovf = 1'b0;
    initial starts = '0;
    initial ferrs = '0;
    always_ff @(posedge clk) begin
        rwe <= 1'b0;
        if (rbit == 4'd0) begin
            if (!rxd) begin                 // a start bit: sample at its middle
                starts <= starts + 16'd1;
                rbit <= 4'd1;
                rcnt <= {1'b0, div[19:1]};
            end
        end else if (rcnt != 0)
            rcnt <= rcnt - 20'd1;
        else begin
            rcnt <= div - 20'd1;
            if (rbit == 4'd1) begin
                rbit <= rxd ? 4'd0 : 4'd2;  // a glitch, not a start bit
            end else if (rbit <= 4'd9) begin
                rsh  <= {rxd, rsh[7:1]};
                rbit <= rbit + 4'd1;
            end else begin                  // the stop bit's middle
                rbit <= 4'd0;
                if (rxd) begin              // framing errors are dropped
                    rwe <= 1'b1;
                    rwd <= rsh;
                end else
                    ferrs <= ferrs + 16'd1;
            end
        end
        if (rwe) begin
            if (count == 11'd1023)
                ovf <= 1'b1;
            else begin
                rq[rw] <= rwd;
                rw <= rw + 10'd1;
            end
        end
        if (pop && avail) rr <= rr + 10'd1;
        if (ovf_clr) ovf <= 1'b0;
        if (reset) begin
            rbit <= '0; rw <= '0; rr <= '0; ovf <= 1'b0;
        end
    end

endmodule

`default_nettype wire
