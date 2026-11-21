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

    // Quartus won't infer a dual-clock true dual-port RAM from this module's
    // behavioural form ("Can't resolve multiple constant drivers", with
    // always_ff and with plain always alike), so synthesis places altsyncram
    // directly, as openFPGA-7800's save_ram_dp does; Verilator runs the
    // behavioural form.
    genvar n;
    generate for (n = 0; n < 4; n++) begin : lane
        wire [7:0] qa, qb;
`ifdef VERILATOR
        reg [7:0] m [0:(1 << WORD_BITS) - 1];
        reg [7:0] ra, rb;
        always @(posedge clk_a) begin
            if (we_a[n]) m[addr_a] <= din_a[8 * n +: 8];
            ra <= m[addr_a];
        end
        always @(posedge clk_b) begin
            if (we_b) m[addr_b] <= din_b[8 * (3 - n) +: 8];
            rb <= m[addr_b];
        end
        assign qa = ra;
        assign qb = rb;
`else
        altsyncram #(
            .operation_mode                 ("BIDIR_DUAL_PORT"),
            .width_a                        (8),
            .widthad_a                      (WORD_BITS),
            .numwords_a                     (1 << WORD_BITS),
            .width_b                        (8),
            .widthad_b                      (WORD_BITS),
            .numwords_b                     (1 << WORD_BITS),
            .outdata_reg_a                  ("UNREGISTERED"),
            .outdata_reg_b                  ("UNREGISTERED"),
            .address_reg_b                  ("CLOCK1"),
            .indata_reg_b                   ("CLOCK1"),
            .wrcontrol_wraddress_reg_b      ("CLOCK1"),
            .clock_enable_input_a           ("BYPASS"),
            .clock_enable_input_b           ("BYPASS"),
            .clock_enable_output_a          ("BYPASS"),
            .clock_enable_output_b          ("BYPASS"),
            .power_up_uninitialized         ("FALSE"),
            .read_during_write_mode_port_a  ("NEW_DATA_NO_NBE_READ"),
            .read_during_write_mode_port_b  ("NEW_DATA_NO_NBE_READ"),
            .intended_device_family         ("Cyclone V"),
            .lpm_type                       ("altsyncram")
        ) ram (
            .clock0         (clk_a),
            .address_a      (addr_a),
            .data_a         (din_a[8 * n +: 8]),
            .wren_a         (we_a[n]),
            .q_a            (qa),
            .clock1         (clk_b),
            .address_b      (addr_b),
            .data_b         (din_b[8 * (3 - n) +: 8]),
            .wren_b         (we_b),
            .q_b            (qb),
            .aclr0 (1'b0), .aclr1 (1'b0), .addressstall_a (1'b0), .addressstall_b (1'b0),
            .byteena_a (1'b1), .byteena_b (1'b1), .clocken0 (1'b1), .clocken1 (1'b1),
            .clocken2 (1'b1), .clocken3 (1'b1), .eccstatus (), .rden_a (1'b1), .rden_b (1'b1)
        );
`endif
        assign dout_a[8 * n +: 8] = qa;
        assign q_b[8 * (3 - n) +: 8] = qb;
    end endgenerate

endmodule

`default_nettype wire
