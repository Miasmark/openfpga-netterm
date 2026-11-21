//
// NetTerm for Analogue Pocket - APF top level
//
// Instantiated by the real top-level: apf_top. The port list and the APF
// plumbing come from Analogue's core template (by way of openFPGA-7800);
// the terminal itself is nt_soc.sv.
//

`default_nettype none

module core_top (

//
// physical connections
//

///////////////////////////////////////////////////
// clock inputs 74.25mhz. not phase aligned, so treat these domains as asynchronous

input   wire            clk_74a, // mainclk1
input   wire            clk_74b, // mainclk1 

///////////////////////////////////////////////////
// cartridge interface
// switches between 3.3v and 5v mechanically
// output enable for multibit translators controlled by pic32

// GBA AD[15:8]
inout   wire    [7:0]   cart_tran_bank2,
output  wire            cart_tran_bank2_dir,

// GBA AD[7:0]
inout   wire    [7:0]   cart_tran_bank3,
output  wire            cart_tran_bank3_dir,

// GBA A[23:16]
inout   wire    [7:0]   cart_tran_bank1,
output  wire            cart_tran_bank1_dir,

// GBA [7] PHI#
// GBA [6] WR#
// GBA [5] RD#
// GBA [4] CS1#/CS#
//     [3:0] unwired
inout   wire    [7:4]   cart_tran_bank0,
output  wire            cart_tran_bank0_dir,

// GBA CS2#/RES#
inout   wire            cart_tran_pin30,
output  wire            cart_tran_pin30_dir,
// when GBC cart is inserted, this signal when low or weak will pull GBC /RES low with a special circuit
// the goal is that when unconfigured, the FPGA weak pullups won't interfere.
// thus, if GBC cart is inserted, FPGA must drive this high in order to let the level translators
// and general IO drive this pin.
output  wire            cart_pin30_pwroff_reset,

// GBA IRQ/DRQ
inout   wire            cart_tran_pin31,
output  wire            cart_tran_pin31_dir,

// infrared
input   wire            port_ir_rx,
output  wire            port_ir_tx,
output  wire            port_ir_rx_disable, 

// GBA link port
inout   wire            port_tran_si,
output  wire            port_tran_si_dir,
inout   wire            port_tran_so,
output  wire            port_tran_so_dir,
inout   wire            port_tran_sck,
output  wire            port_tran_sck_dir,
inout   wire            port_tran_sd,
output  wire            port_tran_sd_dir,
 
///////////////////////////////////////////////////
// cellular psram 0 and 1, two chips (64mbit x2 dual die per chip)

output  wire    [21:16] cram0_a,
inout   wire    [15:0]  cram0_dq,
input   wire            cram0_wait,
output  wire            cram0_clk,
output  wire            cram0_adv_n,
output  wire            cram0_cre,
output  wire            cram0_ce0_n,
output  wire            cram0_ce1_n,
output  wire            cram0_oe_n,
output  wire            cram0_we_n,
output  wire            cram0_ub_n,
output  wire            cram0_lb_n,

output  wire    [21:16] cram1_a,
inout   wire    [15:0]  cram1_dq,
input   wire            cram1_wait,
output  wire            cram1_clk,
output  wire            cram1_adv_n,
output  wire            cram1_cre,
output  wire            cram1_ce0_n,
output  wire            cram1_ce1_n,
output  wire            cram1_oe_n,
output  wire            cram1_we_n,
output  wire            cram1_ub_n,
output  wire            cram1_lb_n,

///////////////////////////////////////////////////
// sdram, 512mbit 16bit

output  wire    [12:0]  dram_a,
output  wire    [1:0]   dram_ba,
inout   wire    [15:0]  dram_dq,
output  wire    [1:0]   dram_dqm,
output  wire            dram_clk,
output  wire            dram_cke,
output  wire            dram_ras_n,
output  wire            dram_cas_n,
output  wire            dram_we_n,

///////////////////////////////////////////////////
// sram, AS6C2016-55: 2 Mbit as 128K x 16, 55 ns

output  wire    [16:0]  sram_a,
inout   wire    [15:0]  sram_dq,
output  wire            sram_oe_n,
output  wire            sram_we_n,
output  wire            sram_ub_n,
output  wire            sram_lb_n,

///////////////////////////////////////////////////
// vblank driven by dock for sync in a certain mode

input   wire            vblank,

///////////////////////////////////////////////////
// i/o to 6515D breakout usb uart

output  wire            dbg_tx,
input   wire            dbg_rx,

///////////////////////////////////////////////////
// i/o pads near jtag connector user can solder to

output  wire            user1,
input   wire            user2,

///////////////////////////////////////////////////
// RFU internal i2c bus 

inout   wire            aux_sda,
output  wire            aux_scl,

///////////////////////////////////////////////////
// RFU, do not use
output  wire            vpll_feed,


//
// logical connections
//

///////////////////////////////////////////////////
// video, audio output to scaler
output  wire    [23:0]  video_rgb,
output  wire            video_rgb_clock,
output  wire            video_rgb_clock_90,
output  wire            video_de,
output  wire            video_skip,
output  wire            video_vs,
output  wire            video_hs,
    
output  wire            audio_mclk,
input   wire            audio_adc,
output  wire            audio_dac,
output  wire            audio_lrck,

///////////////////////////////////////////////////
// bridge bus connection
// synchronous to clk_74a
output  wire            bridge_endian_little,
input   wire    [31:0]  bridge_addr,
input   wire            bridge_rd,
output  reg     [31:0]  bridge_rd_data,
input   wire            bridge_wr,
input   wire    [31:0]  bridge_wr_data,

///////////////////////////////////////////////////
// controller data
// 
// key bitmap:
//   [0]    dpad_up
//   [1]    dpad_down
//   [2]    dpad_left
//   [3]    dpad_right
//   [4]    face_a
//   [5]    face_b
//   [6]    face_x
//   [7]    face_y
//   [8]    trig_l1
//   [9]    trig_r1
//   [10]   trig_l2
//   [11]   trig_r2
//   [12]   trig_l3
//   [13]   trig_r3
//   [14]   face_select
//   [15]   face_start
//   [31:28] type
// joy values - unsigned
//   [ 7: 0] lstick_x
//   [15: 8] lstick_y
//   [23:16] rstick_x
//   [31:24] rstick_y
// trigger values - unsigned
//   [ 7: 0] ltrig
//   [15: 8] rtrig
//
input   wire    [31:0]  cont1_key,
input   wire    [31:0]  cont2_key,
input   wire    [31:0]  cont3_key,
input   wire    [31:0]  cont4_key,
input   wire    [31:0]  cont1_joy,
input   wire    [31:0]  cont2_joy,
input   wire    [31:0]  cont3_joy,
input   wire    [31:0]  cont4_joy,
input   wire    [15:0]  cont1_trig,
input   wire    [15:0]  cont2_trig,
input   wire    [15:0]  cont3_trig,
input   wire    [15:0]  cont4_trig
    
);

// not using the IR port, so turn off both the LED, and
// disable the receive circuit to save power
assign port_ir_tx = 0;
assign port_ir_rx_disable = 1;

// bridge endianness
assign bridge_endian_little = 0;

// cart is unused, so set all level translators accordingly
// directions are 0:IN, 1:OUT
assign cart_tran_bank3 = 8'hzz;
assign cart_tran_bank3_dir = 1'b0;
assign cart_tran_bank2 = 8'hzz;
assign cart_tran_bank2_dir = 1'b0;
assign cart_tran_bank1 = 8'hzz;
assign cart_tran_bank1_dir = 1'b0;
// Bank 0 is an output, as the template leaves it; bit 4 (cartridge pin 5,
// GBA /CS) is the serial TX. Pin 31 (GBA /REQ) is the serial RX.
assign cart_tran_bank0 = {3'b111, nt_txd};
assign cart_tran_bank0_dir = 1'b1;
assign cart_tran_pin30 = 1'b0;      // reset or cs2, we let the hw control it by itself
assign cart_tran_pin30_dir = 1'bz;
assign cart_pin30_pwroff_reset = 1'b0;  // hardware can control this
assign cart_tran_pin31 = 1'bz;      // input
assign cart_tran_pin31_dir = 1'b0;  // input

// link port: SO (pin 2) the serial TX, SI (pin 3) the RX
assign port_tran_so = nt_txd;
assign port_tran_so_dir = 1'b1;
assign port_tran_si = 1'bz;
assign port_tran_si_dir = 1'b0;     // SI is input only
assign port_tran_sck = 1'bz;
assign port_tran_sck_dir = 1'b0;    // clock direction can change
assign port_tran_sd = 1'bz;
assign port_tran_sd_dir = 1'b0;     // SD is input and not used

// tie off the rest of the pins we are not using
assign cram0_a = 'h0;
assign cram0_dq = {16{1'bZ}};
assign cram0_clk = 0;
assign cram0_adv_n = 1;
assign cram0_cre = 0;
assign cram0_ce0_n = 1;
assign cram0_ce1_n = 1;
assign cram0_oe_n = 1;
assign cram0_we_n = 1;
assign cram0_ub_n = 1;
assign cram0_lb_n = 1;

assign cram1_a = 'h0;
assign cram1_dq = {16{1'bZ}};
assign cram1_clk = 0;
assign cram1_adv_n = 1;
assign cram1_cre = 0;
assign cram1_ce0_n = 1;
assign cram1_ce1_n = 1;
assign cram1_oe_n = 1;
assign cram1_we_n = 1;
assign cram1_ub_n = 1;
assign cram1_lb_n = 1;

assign dram_a = 'h0;
assign dram_ba = 'h0;
assign dram_dq = {16{1'bZ}};
assign dram_dqm = 'h0;
assign dram_clk = 'h0;
assign dram_cke = 'h0;
assign dram_ras_n = 'h1;
assign dram_cas_n = 'h1;
assign dram_we_n = 'h1;

assign sram_a = 'h0;
assign sram_dq = {16{1'bZ}};
assign sram_oe_n  = 1;
assign sram_we_n  = 1;
assign sram_ub_n  = 1;
assign sram_lb_n  = 1;

assign dbg_tx = 1'bZ;
assign user1 = 1'bZ;
assign aux_scl = 1'bZ;
assign vpll_feed = 1'bZ;

// no audio: the I2S lines held low
assign audio_mclk = 1'b0;
assign audio_dac  = 1'b0;
assign audio_lrck = 1'b0;


////////////////////////////////////////////////////////////////////////////////
// Clocks (tools/gen_pll.sh): clk_sys 49.5 MHz (the CPU), clk_pix 12.375 MHz
// and the same at 90 degrees (the video)
////////////////////////////////////////////////////////////////////////////////

    wire    clk_sys;
    wire    clk_pix;
    wire    clk_pix_90;
    wire    pll_core_locked;
    wire    pll_core_locked_s;

pll_core pll (
    .refclk     ( clk_74a ),
    .rst        ( 1'b0 ),
    .outclk_0   ( clk_sys ),
    .outclk_1   ( clk_pix ),
    .outclk_2   ( clk_pix_90 ),
    .locked     ( pll_core_locked )
);

synch_3 s01(pll_core_locked, pll_core_locked_s, clk_74a);


////////////////////////////////////////////////////////////////////////////////
// Bridge
////////////////////////////////////////////////////////////////////////////////

// Bridge address map
//   0x10000100 + 4n  setting n (interact.json), 8 bits each; read by the
//                    firmware (firmware/hw.h, SET_*)
//   0x10000200       Reset: restarts the firmware
//   0x20000000       the save slot, 2 KiB (data.json "Address Book"; nt_save_ram.sv)
//   0x40000000       the BBS list's window, 4 KiB: the Pocket writes what a
//                    target read (0x0180) of data slot 0x101 asks for here
//   0xF8000000       APF command interface

    reg     [63:0]  settings = 64'h0000_0000_0007_0000;   // defaults: interact.json's
    reg     [7:0]   menu_reset_cnt = 8'd0;
always @(posedge clk_74a) begin
    if (menu_reset_cnt != 0) menu_reset_cnt <= menu_reset_cnt - 1'b1;
    if (bridge_wr && bridge_addr[31:12] == 20'h10000) begin
        if (bridge_addr[11:8] == 4'h1 && bridge_addr[7:5] == 3'd0)
            settings[{bridge_addr[4:2], 3'd0} +: 8] <= bridge_wr_data[7:0];
        if (bridge_addr[11:0] == 12'h200)
            menu_reset_cnt <= 8'hFF;
    end
end

    wire    [31:0]  save_rd_latched;
    wire            save_sel = bridge_addr[31:24] == 8'h20;
    wire            lwin_sel = bridge_addr[31:24] == 8'h40;

always @(*) begin
    casex(bridge_addr)
    32'h20xxxxxx: begin
        bridge_rd_data <= save_rd_latched;
    end
    32'hF8xxxxxx: begin
        bridge_rd_data <= cmd_bridge_rd_data;
    end
    default: begin
        bridge_rd_data <= 0;
    end
    endcase
end


//
// host/target command handler
//
    wire            reset_n;                // driven by host commands, can be used as core-wide reset
    wire    [31:0]  cmd_bridge_rd_data;

// bridge host commands
// synchronous to clk_74a
    wire            status_boot_done = pll_core_locked_s;
    wire            status_setup_done = pll_core_locked_s; // rising edge triggers a target command
    wire            status_running = reset_n; // we are running as soon as reset_n goes high

    wire            dataslot_requestread;
    wire    [15:0]  dataslot_requestread_id;
    wire            dataslot_requestread_ack = 1;
    wire            dataslot_requestread_ok = 1;

    wire            dataslot_requestwrite;
    wire    [15:0]  dataslot_requestwrite_id;
    wire    [31:0]  dataslot_requestwrite_size;
    wire            dataslot_requestwrite_ack = 1;
    wire            dataslot_requestwrite_ok = 1;

    wire            dataslot_update;
    wire    [15:0]  dataslot_update_id;
    wire    [31:0]  dataslot_update_size;

    wire            dataslot_allcomplete;

    wire     [31:0] rtc_epoch_seconds;
    wire     [31:0] rtc_date_bcd;
    wire     [31:0] rtc_time_bcd;
    wire            rtc_valid;

    wire            savestate_supported = 1'b0;
    wire    [31:0]  savestate_addr = 32'd0;
    wire    [31:0]  savestate_size = 32'd0;
    wire    [31:0]  savestate_maxloadsize = 32'd0;

    wire            savestate_start;
    wire            savestate_start_ack = 1'b0;
    wire            savestate_start_busy = 1'b0;
    wire            savestate_start_ok = 1'b0;
    wire            savestate_start_err = 1'b0;

    wire            savestate_load;
    wire            savestate_load_ack = 1'b0;
    wire            savestate_load_busy = 1'b0;
    wire            savestate_load_ok = 1'b0;
    wire            savestate_load_err = 1'b0;

    wire            osnotify_inmenu;

// bridge target commands
// synchronous to clk_74a

    wire            target_dataslot_read;      // the BBS list reads (nt_soc)
    reg             target_dataslot_write = 1'b0;
    reg             target_dataslot_getfile = 1'b0;
    reg             target_dataslot_openfile = 1'b0;

    wire            target_dataslot_ack;
    wire            target_dataslot_done;
    wire    [2:0]   target_dataslot_err;

    wire    [15:0]  target_dataslot_id = 16'h0101;         // data.json "BBS List"
    wire    [31:0]  target_dataslot_slotoffset;
    wire    [31:0]  target_dataslot_bridgeaddr = 32'h40000000;
    wire    [31:0]  target_dataslot_length;

    wire    [31:0]  target_buffer_param_struct;
    wire    [31:0]  target_buffer_resp_struct;

// bridge data slot access
// synchronous to clk_74a

// The data table, in eight steps: write the save slot's size (data slot index
// 0, so the Pocket writes the file even before one exists, as openFPGA-7800
// does), then read the BBS list's size (index 1, which the Pocket fills in
// when it opens the file); the table's RAM registers address and output, so
// the word is read four clocks after the address is set
    reg     [9:0]   datatable_addr = 10'd1;
    reg             datatable_wren = 1'b0;
    reg     [31:0]  datatable_data = 32'd2048;
    wire    [31:0]  datatable_q;
    reg     [2:0]   dt_ph = 3'd0;
    reg     [31:0]  list_size = 32'd0;
always @(posedge clk_74a) begin
    dt_ph <= dt_ph + 3'd1;
    case (dt_ph)
    3'd0: begin datatable_addr <= 10'd1; datatable_wren <= 1'b1; datatable_data <= 32'd2048; end
    3'd1: begin datatable_addr <= 10'd3; datatable_wren <= 1'b0; end
    3'd5: list_size <= datatable_q;
    default: ;
    endcase
end

core_bridge_cmd icb (

    .clk                ( clk_74a ),
    .reset_n            ( reset_n ),

    .bridge_endian_little   ( bridge_endian_little ),
    .bridge_addr            ( bridge_addr ),
    .bridge_rd              ( bridge_rd ),
    .bridge_rd_data         ( cmd_bridge_rd_data ),
    .bridge_wr              ( bridge_wr ),
    .bridge_wr_data         ( bridge_wr_data ),

    .status_boot_done       ( status_boot_done ),
    .status_setup_done      ( status_setup_done ),
    .status_running         ( status_running ),

    .dataslot_requestread       ( dataslot_requestread ),
    .dataslot_requestread_id    ( dataslot_requestread_id ),
    .dataslot_requestread_ack   ( dataslot_requestread_ack ),
    .dataslot_requestread_ok    ( dataslot_requestread_ok ),

    .dataslot_requestwrite      ( dataslot_requestwrite ),
    .dataslot_requestwrite_id   ( dataslot_requestwrite_id ),
    .dataslot_requestwrite_size ( dataslot_requestwrite_size ),
    .dataslot_requestwrite_ack  ( dataslot_requestwrite_ack ),
    .dataslot_requestwrite_ok   ( dataslot_requestwrite_ok ),

    .dataslot_update            ( dataslot_update ),
    .dataslot_update_id         ( dataslot_update_id ),
    .dataslot_update_size       ( dataslot_update_size ),

    .dataslot_allcomplete   ( dataslot_allcomplete ),

    .rtc_epoch_seconds      ( rtc_epoch_seconds ),
    .rtc_date_bcd           ( rtc_date_bcd ),
    .rtc_time_bcd           ( rtc_time_bcd ),
    .rtc_valid              ( rtc_valid ),

    .savestate_supported    ( savestate_supported ),
    .savestate_addr         ( savestate_addr ),
    .savestate_size         ( savestate_size ),
    .savestate_maxloadsize  ( savestate_maxloadsize ),

    .savestate_start        ( savestate_start ),
    .savestate_start_ack    ( savestate_start_ack ),
    .savestate_start_busy   ( savestate_start_busy ),
    .savestate_start_ok     ( savestate_start_ok ),
    .savestate_start_err    ( savestate_start_err ),

    .savestate_load         ( savestate_load ),
    .savestate_load_ack     ( savestate_load_ack ),
    .savestate_load_busy    ( savestate_load_busy ),
    .savestate_load_ok      ( savestate_load_ok ),
    .savestate_load_err     ( savestate_load_err ),

    .osnotify_inmenu        ( osnotify_inmenu ),

    .target_dataslot_read       ( target_dataslot_read ),
    .target_dataslot_write      ( target_dataslot_write ),
    .target_dataslot_getfile    ( target_dataslot_getfile ),
    .target_dataslot_openfile   ( target_dataslot_openfile ),

    .target_dataslot_ack        ( target_dataslot_ack ),
    .target_dataslot_done       ( target_dataslot_done ),
    .target_dataslot_err        ( target_dataslot_err ),

    .target_dataslot_id         ( target_dataslot_id ),
    .target_dataslot_slotoffset ( target_dataslot_slotoffset ),
    .target_dataslot_bridgeaddr ( target_dataslot_bridgeaddr ),
    .target_dataslot_length     ( target_dataslot_length ),

    .target_buffer_param_struct ( target_buffer_param_struct ),
    .target_buffer_resp_struct  ( target_buffer_resp_struct ),

    .datatable_addr         ( datatable_addr ),
    .datatable_wren         ( datatable_wren ),
    .datatable_data         ( datatable_data ),
    .datatable_q            ( datatable_q )

);


////////////////////////////////////////////////////////////////////////////////
// The terminal
////////////////////////////////////////////////////////////////////////////////

// clk_sys reset: the host's reset, the PLL, and the menu's Reset
    wire            sys_reset_74a = !reset_n || !pll_core_locked_s || menu_reset_cnt != 0;
    wire            sys_reset;
synch_3 s02(sys_reset_74a, sys_reset, clk_sys);

    wire            nt_txd;
    wire    [23:0]  nt_rgb;
    wire            nt_de, nt_hs, nt_vs;

nt_soc soc (
    .clk_sys    ( clk_sys ),
    .reset      ( sys_reset ),
    .clk_74a    ( clk_74a ),
    .clk_pix    ( clk_pix ),
    .cont1_key  ( cont1_key ),  .cont2_key  ( cont2_key ),  .cont3_key  ( cont3_key ),  .cont4_key  ( cont4_key ),
    .cont1_joy  ( cont1_joy ),  .cont2_joy  ( cont2_joy ),  .cont3_joy  ( cont3_joy ),  .cont4_joy  ( cont4_joy ),
    .cont1_trig ( cont1_trig ), .cont2_trig ( cont2_trig ), .cont3_trig ( cont3_trig ), .cont4_trig ( cont4_trig ),
    .settings   ( settings ),
    .save_addr  ( bridge_addr[10:2] ),
    .save_din   ( bridge_wr_data ),
    .save_wr    ( bridge_wr && save_sel ),
    .save_rd    ( bridge_rd && save_sel ),
    .save_dout  ( save_rd_latched ),
    .lwin_addr  ( bridge_addr[11:2] ),
    .lwin_din   ( bridge_wr_data ),
    .lwin_wr    ( bridge_wr && lwin_sel ),
    .tgt_read   ( target_dataslot_read ),
    .tgt_offset ( target_dataslot_slotoffset ),
    .tgt_length ( target_dataslot_length ),
    .tgt_done   ( target_dataslot_done ),
    .tgt_err    ( target_dataslot_err ),
    .list_size  ( list_size ),
    .txd        ( nt_txd ),
    .rxd_link   ( port_tran_si ),
    .rxd_cart   ( cart_tran_pin31 ),
    .video_rgb  ( nt_rgb ),
    .video_de   ( nt_de ),
    .video_hs   ( nt_hs ),
    .video_vs   ( nt_vs )
);

assign video_rgb_clock    = clk_pix;
assign video_rgb_clock_90 = clk_pix_90;
assign video_rgb          = nt_rgb;
assign video_de           = nt_de;
assign video_skip         = 1'b0;
assign video_vs           = nt_vs;
assign video_hs           = nt_hs;

endmodule
