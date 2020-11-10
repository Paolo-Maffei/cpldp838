/*
    project        : CPLDP-8, PDP-8 compatible computer with MAXII
    author         : omiokone
    pdp8.v         : CPU Verilog HDL source
*/

//  top module
module pdp8(
  input               clk, reset_n, cont_nc, cont_no,
  output              run,
  inout       [11:0]  data,
  output      [11:0]  mar,              // memory address register
  output      [2:0]   field,
  output      [2:0]   efield,           // extra field for RAM disk
  output              read_n, write_n,
  output      [11:0]  romout,
  input               rxd,              // serial I/O
  output              rts_n,
  output              rbusy,            // 1: receiver busy
  output              txd,
  input               cts_n,
  output              tbusy,            // 1: transmitter busy
  output reg  [11:0]  ac,               // accumulator
  output reg          lr                // L register
  );
  reg                 ctsb_n;
  wire        [8:0]   mc;               // microcode
  wire                reset, out_enable, rom_enable;
  wire                romdo;
  wire        [7:0]   rdata;
  wire                rflag, tflag, kcc, intr, gr2end;
  wire                intc, sin, sout, mem;
  reg         [2:0]   dfr, ifr, ibr;    // memory extension
  reg         [2:0]   sdr, sir;
  reg                 fields;           // 0:instruction 1:data field
  reg                 cif;              // CIF not concluded by JMP/JMS
  wire        [2:0]   dsel, isel;
  reg                 efe;              // extra field enable

//  I/O device code
  assign intc=~mc[8]&~mc[7]&~mc[6]&~mc[5]&~mc[4]&~mc[3];  // interrupt command
  assign sin =~mc[8]&~mc[7]&~mc[6]&~mc[5]& mc[4]& mc[3];  // TTY (serial) in
  assign sout=~mc[8]&~mc[7]&~mc[6]& mc[5]&~mc[4]&~mc[3];  // TTY (serial) out
  assign mem =~mc[8]& mc[7]&~mc[6];                       // memory extension

//  timing_generator
  reg                 ram, preram;      // instruction fetch from 0:ROM 1:RAM
  reg         [4:0]   ir;               // instruction register
  // timing shift registers
  reg         [2:0]   fetchs;           // instruction fetch
  reg                 romcs;            // ROM serial-parallel convert
  reg         [20:0]  roms;
  reg                 ints;             // interrupt
  reg         [1:0]   inds;             // indirect addressing
  reg                 ands;
  reg         [2:0]   tads;
  reg                 iszs;
  reg                 dcas;
  reg         [2:0]   jmss;
  reg                 jmps;
  reg         [1:0]   iots;
  reg         [5:0]   gr1s;             // group1/2 instructions
  reg                 gr2s;
  reg                 mbws;             // memory write of ISZ & DCA

  always@(posedge clk or posedge reset)
  if(reset) begin
    ram<=0; preram<=0;
    fetchs<=3'b001; romcs<=0; roms<=0; ints<=0;
    inds<=0; ands<=0; tads<=0; iszs<=0; dcas<=0;
    jmss<=0; jmps<=0; iots<=0; gr1s<=0; gr2s<=0; mbws<=0;
  end else begin
    if(fetchs[1]) ir<=data[11:7];
    fetchs[2]<=fetchs[1];
    fetchs[1]<=fetchs[0]&ram&~intr | roms[20];
    fetchs[0]<=ands|tads[2]|mbws|jmss[2]|jmps|iots[1]|gr1s[5]|gr2end;
    if(fetchs[0]&~ram) romcs<=1'b1; else
    if(roms[20])       romcs<=1'b0;
    roms<=roms<<1; roms[0]<=fetchs[0]&~ram;
    ints<=fetchs[0]&intr;
    inds<=inds<<1; inds[0]<= ~(ir[4]&ir[3]) & fetchs[2];
                   ands   <= ~ir[4] & ~ir[3] & ~ir[2] & inds[1];
    tads<=tads<<1; tads[0]<= ~ir[4] & ~ir[3] &  ir[2] & inds[1];
                   iszs   <= ~ir[4] &  ir[3] & ~ir[2] & inds[1];
                   dcas   <= ~ir[4] &  ir[3] &  ir[2] & inds[1];
    jmss<=jmss<<1; jmss[0]<=  ir[4] & ~ir[3] & ~ir[2] & inds[1] | ints;
                   jmps   <=  ir[4] & ~ir[3] &  ir[2] & inds[1];
    iots<=iots<<1; iots[0]<=  ir[4] &  ir[3] & ~ir[2] &          fetchs[2];
    gr1s<=gr1s<<1; gr1s[0]<=  ir[4] &  ir[3] &  ir[2] & ~ir[1] & fetchs[2];
                   gr2s   <=  ir[4] &  ir[3] &  ir[2] &  ir[1] & fetchs[2];
    mbws<=iszs|dcas;
    // OSR switches ROM to RAM after next instruction fetch
    if(gr2s&mc[2]) preram<=1'b1;
    if(fetchs[2])  ram<=preram;
  end

//  interrupt
  reg                 inte, preinte;

  assign intr=inte&~cif&~efe&(rflag|(tflag&~ctsb_n));
  always@(posedge clk or posedge reset)
  if(reset) begin
    inte<=1'b0; preinte<=1'b0;
  end else
  if(ints | iots[0]&intc&mc[1]&~mc[0]) begin
    inte<=1'b0; preinte<=1'b0;
  end else begin
    if(iots[0]&intc&~mc[1]&mc[0]) preinte<=1'b1;
    if(fetchs[2]) inte<=preinte;
  end

//  accumulator
  wire        [11:0]  logic_out;
  wire        [7:0]   indata;
  wire        [1:0]   func;
  wire                rar, ral;

  assign func[0]=ands | tads[0] | dcas | iots[1] | gr1s[2];
  assign func[1]=ands | gr1s[1] | gr2s;
  assign logic_out= func==0? data   :
                    func==1?  ac    :
                    func==2? ~ac    :
                             ac&data;

  assign indata[7:6]=iots[1]&sin? rdata[7:6]: 2'b0;
  assign indata[5:3]=iots[1]&sin? rdata[5:3]: ~iots[1]|(mc[4]&mc[3])?isel:field;
  assign indata[2:0]=iots[1]&sin? rdata[2:0]: dsel;

  assign rar=mc[3]&(gr1s[4]|(gr1s[5]&mc[1]));
  assign ral=mc[2]&(gr1s[3]|(gr1s[4]&mc[1]));
  always@(posedge clk)
  if(dcas | kcc | (gr1s[0]|gr2s)&mc[7]) ac<=0;              else
  if(rar)                               ac<={lr, ac[11:1]}; else
  if(ands | tads[2] | iots[1]&sin&mc[2]&~mc[0] | iots[1]&mem&~mc[5]&mc[2] |
     gr1s[1]&mc[5] | gr1s[3] | gr1s[5]&mc[2])
    ac<={logic_out[11:8], logic_out[7:0]|indata};

//  memory buffer register
  wire        [11:0]  mbr, adder;
  wire                cin, cout, mbrc,mbrl;

  assign cin=inds[0]&(mar[11:3]==9'h001) | iszs | gr1s[2]&mc[0] |
             gr1s[3]&lr | gr1s[4]&lr | gr2s;
  assign {cout, adder}=mbr+logic_out+cin;
  assign mbrc=fetchs[0] | fetchs[2] | inds[1];
  assign mbrl=fetchs[1] | inds[0] | tads[0] | tads[1] | iszs | dcas | gr1s[2] |
              ral;
  lpm_ff mbrm(.data(adder), .clock(clk), .enable(mbrc|mbrl), .sclr(mbrc),
              .q(mbr));
  defparam
    mbrm.lpm_width =12,
    mbrm.lpm_fftype="DFF";

  always@(posedge clk)
  if(gr1s[0]&mc[6])           lr<=1'b0;    else
  if(gr1s[1]&mc[4])           lr<=~lr;     else
  if(tads[1] | gr1s[2]&mc[0]) lr<=lr^cout; else
  if(rar)                     lr<=ac[0];   else
  if(ral)                     lr<=cout;

//  program counter
  wire        [11:0]  pc;
  wire                pcup;

  assign pcup=fetchs[2] | iszs&cout | jmss[2] | iots[0]&sin&~mc[2]&mc[0]&rflag |
              iots[0]&sout&mc[0]&tflag&~ctsb_n |
              gr2s&~mc[0]&(mc[3]^(mc[6]&ac[11] | mc[5]&cout | mc[4]&lr));

  lpm_counter pcm(.clock(clk), .aclr(reset), .sload(jmss[1]|jmps), .data(mar),
                  .cnt_en(pcup), .q(pc));
  defparam
    pcm.lpm_direction  ="UP",
    pcm.lpm_port_updown="PORT_UNUSED",
    pcm.lpm_width      =12;

//  memory address register
  wire        [11:0]  muxout;
  wire                mal;

  assign muxout[6:0] = fetchs[0]|          jmss[1]? pc[6:0]:  mbr[6:0];
  assign muxout[11:7]= fetchs[0]|fetchs[2]|jmss[1]? pc[11:7]: mbr[11:7];

  assign mal=fetchs[0] | fetchs[2] | ints | inds[1]&ir[1];
  lpm_shiftreg
    marl(.clock(clk), .data(muxout[6:0]),  .shiftin(romdo),  .enable(mal|romcs),
         .load(mal), .q(mar[6:0])),
    marh(.clock(clk), .data(muxout[11:7]), .shiftin(mar[6]), .enable(mal|romcs),
         .load(mal), .q(mar[11:7]), .sclr(fetchs[2]&~ir[0]));
  defparam
    marl.lpm_direction="LEFT", marl.lpm_width=7,
    marh.lpm_direction="LEFT", marh.lpm_width=5;

  assign data  = out_enable? muxout: 12'bz;
  assign romout= rom_enable? mar:    12'bz;
  assign mc[6:0]=mar[6:0];
  assign mc[8:7]=ir[1:0];

//  ROM
  reg                 dshift;

  always@(posedge clk)
  if(~romcs)  dshift<=1'b0; else
  if(roms[9]) dshift<=1'b1;
 
  rom rom(.arclk(~dshift&~roms[9]&~clk), .ardin(mar[8]), .arshft(1'b1),
          .drclk(~clk), .drdin(1'b0), .drshft(dshift), .erase(1'b0),
          .oscena(1'b0), .program(1'b0), .busy(), .drdout(romdo), .osc(),
          .rtpbusy());

//  bus control
  wire                read;

  assign read=fetchs[1]&ram | inds[0] | ands | tads[1] | iszs;
  assign read_n=~(~clk&read);
  assign rom_enable=~clk&fetchs[1]&~read;
  assign out_enable=~clk&~fetchs[1]&~read;
  assign write_n=~(~clk&(ir[1]&inds[1] | mbws | jmss[1]));

//  power on reset
  reg                 reset_ff;
  always@(posedge clk) reset_ff<=reset_n;
  assign reset=~reset_ff;

//  continue switch
  reg                 cont0, cont1, halt;
  wire                cont;

  assign cont=~(mc[1]&~mc[0]) | cont0&~cont1;
  assign gr2end=(gr2s|halt)&cont;
  assign run=~halt;
  always@(posedge clk or posedge reset)
  if(reset) halt<=0;
  else begin
    if(~cont_nc& cont_no) cont0<=0; else
    if( cont_nc&~cont_no) cont0<=1;
    cont1<=cont0;
    halt<=(gr2s|halt)&~cont;
  end

//  memory extension
  assign dsel= iots[0]                      ? mc [5:3]:
               iots[1]&(mc[5] | mc[4]&mc[3])? sdr     :
                                              3'b0;
  assign isel= iots[0]                      ? mc [5:3]:
               iots[1]                      ? sir     :
                                              3'b0;
  always@(posedge clk or posedge reset)
  if(reset) begin
    dfr<=0; ifr<=0; ibr<=0; sdr<=0; sir<=0; cif<=0;
  end else begin
    if(ints | iots[0]&mem&mc[0] | iots[1]&mem&mc[5]&mc[2]) dfr<=dsel;
    if(ints | iots[0]&mem&mc[1] | iots[1]&mem&mc[5]&mc[2]) ibr<=isel;
    if(jmss[0]|jmps) ifr<=ibr;
    if(ints | iots[0]&intc&mc[2]) begin
      sdr<=dfr; sir<=ifr;
    end
    if(iots[0]&mem&mc[1]) cif<=1'b1; else
    if(jmss[0]|jmps)      cif<=1'b0;
  end

  assign field=fields? dfr: ifr;
  always@(posedge clk)
  if(fetchs[0])                            fields<=1'b0; else
  if(inds[1]&~ir[4]&ir[1] | iots[0]&mc[3]) fields<=1'b1;

//  extra field special instructions
//  6004 extra field OFF
//  6007 extra field ON  data field -> save field register
//  this 3bit become extra field
  always@(posedge clk or posedge reset)
  if(reset)              efe<=0;     else
  if(iots[0]&intc&mc[2]) efe<=mc[0];

  assign efield= fields&efe? sdr: 3'b111;

//  serial I/O
  assign rts_n=rflag|rbusy;
  assign kcc=iots[0]&sin&mc[1];
  serial_io serial_io(clk, reset, rxd, rdata, rflag, kcc, rbusy,
                      txd, ac[7:0], tflag, iots[0]&sout&mc[1],
                      iots[0]&sout&mc[2]&~mc[0], tbusy);
  always@(posedge clk) ctsb_n<=cts_n;
endmodule

//  serial - parallel converter
module serial_io(
  input               clk, reset,
  input               rxd,
  output      [7:0]   rdata,
  output reg          rflag,            // 1: receive data ready
  input               rclr,             // clear rflag
  output reg          rbusy,
  output              txd,
  input       [7:0]   tdata,
  output reg          tflag,            // 1: transmit buffer empty
  input               tclr,             // clear tflag
  input               tload,            // load transmit data
  output reg          tbusy
  );

  // 8MHz/52 = 19200*8 clock
  // modified LFSR as 52 divider
  reg         [5:0]   div;
  wire                sclk;

  assign sclk=~div[3]&~div[2]&~div[1]&~div[0];
  always@(posedge clk or posedge reset)
  if(reset) div<=6'b110000;
  else begin
    div[5]  <=div[4];
    div[4]  <=div[3]|sclk;
    div[3:1]<=div[2:0];
    div[0]  <=(div[5]^div[4])|sclk;
  end

  // serial input
  reg                 rxb;
  reg         [2:0]   rdiv;
  wire        [3:0]   rdivt;
  reg         [9:0]   rbuff;

  always@(posedge clk) rxb<=rxd;

  always@(posedge clk or posedge reset)
  if(reset)     rbusy<=1'b0; else
  if(rflag)     rbusy<=1'b0; else
  if(~rxb&sclk) rbusy<=1'b1;

  assign rdivt=rdiv+1'b1;
  always@(posedge clk) 
  if(rbusy) begin
    if(sclk) rdiv<=rdivt[2:0];
  end else
    rdiv<=3'b100;

  assign rdata[7:0]=rbuff[8:1];
  always@(posedge clk)
  if(rbusy) begin
    if(sclk&rdivt[3]) begin
      rbuff<=rbuff>>1; rbuff[9]<=rxb;
    end
  end else
  if(~rflag) rbuff<=10'h200;

  always@(posedge clk or posedge reset)
  if(reset)               rflag<=1'b0; else
  if(rclr)                rflag<=1'b0; else
  if(rbusy&sclk&rdivt[3]) rflag<=rbuff[0];

  // serial output
  reg                 tstart;
  reg         [2:0]   tdiv;
  wire        [3:0]   tdivt;
  reg         [8:0]   tbuff;
  reg         [3:0]   tcount;
  wire                tend;

  assign tend=tcount[3]&tcount[1];      // count 10
  always@(posedge clk or posedge reset)
  if(reset) tstart<=1'b0; else
  if(tbusy) tstart<=1'b0; else
  if(tload) tstart<=1'b1;
  always@(posedge clk or posedge reset)
  if(reset)       tbusy<=1'b0; else
  if(tend)        tbusy<=1'b0; else
  if(tstart&sclk) tbusy<=1'b1;

  assign tdivt=tdiv+1'b1;
  always@(posedge clk)
  if(tbusy) begin
    if(sclk) tdiv<=tdivt[2:0];
  end else
    tdiv<=0;

  assign txd=tbuff[0]|~tbusy;
  always@(posedge clk)
  if(tbusy) begin
    if(sclk&tdivt[3]) begin
      tbuff<=tbuff>>1; tbuff[8]<=1'b1;
    end
  end else
  if(tload) begin
    tbuff[8:1]<=tdata[7:0]; tbuff[0]<=1'b0;
  end

  always@(posedge clk or posedge reset)
  if(reset)         tcount<=0; else
  if(~tbusy)        tcount<=0; else
  if(sclk&tdivt[3]) tcount<=tcount+1'b1;

  always@(posedge clk or posedge reset)
  if(reset) tflag<=1'b0; else
  if(tclr)  tflag<=1'b0; else
  if(tend)  tflag<=1'b1;
endmodule
