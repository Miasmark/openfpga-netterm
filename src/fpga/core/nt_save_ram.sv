// NetTerm: the save RAM, 2 KiB behind a Pocket save slot (data.json,
// "Address Book", the firmware's address book).
//
// Port A: the CPU (clk_sys), 32-bit words with byte writes; byte n of the
// file is byte n of the CPU's address space here (little-endian lanes).
// Port B: the APF bridge (clk_74a), 32-bit words, big-endian as the bridge
// is (bridge_endian_little = 0): byte n of a word is bits [31-8n -: 8].
// So each byte lane is its own true dual-port, dual-clock RAM, wired to the
// CPU's lane n and the bridge's lane 3-n.
//
// Bridge reads lag a transaction: the host samples dout_b a few cycles into
// a read and then pulses rd_b, and expects the word latched at the previous
// strobe (openFPGA-7800's save_ram_dp, whose HSC saves once came out rotated
// by four bytes without this).

`default_nettype none

module nt_save_ram #(
    parameter int WORD_BITS = 9                 // 512 words, 2 KiB
) (
    input  wire                 clk_a,
    input  wire [WORD_BITS-1:0] addr_a,
    input  wire          [31:0] din_a,
    input  wire           [3:0] we_a,
    output logic         [31:0] dout_a,

    input  wire                 clk_b,
    input  wire [WORD_BITS-1:0] addr_b,
    input  wire          [31:0] din_b,
    input  wire                 we_b,
    input  wire                 rd_b,
    output logic         [31:0] dout_b
);

    logic [31:0] q_b;
    initial dout_b = '0;
    always @(posedge clk_b) if (rd_b) dout_b <= q_b;

    // Plain always blocks, as Quartus's dual-clock true dual-port template has
    // them: two always_ff blocks writing one array fail elaboration
    // ("Can't resolve multiple constant drivers", 0.4.0's build).
    genvar n;
    generate for (n = 0; n < 4; n++) begin : lane
        reg [7:0] m [0:(1 << WORD_BITS) - 1];
        reg [7:0] qa, qb;
        always @(posedge clk_a) begin
            if (we_a[n]) m[addr_a] <= din_a[8 * n +: 8];
            qa <= m[addr_a];
        end
        always @(posedge clk_b) begin
            if (we_b) m[addr_b] <= din_b[8 * (3 - n) +: 8];
            qb <= m[addr_b];
        end
        assign dout_a[8 * n +: 8] = qa;
        assign q_b[8 * (3 - n) +: 8] = qb;
    end endgenerate

endmodule

`default_nettype wire
