class Packet;
  rand int len;
  rand int addr;

  constraint len_c {
    len > 0;
  }

  constraint addr_c {
    addr == 16;
  }
endclass

module PacketSink(input int len, input int addr, output bit ok);
  assign ok = (len > 0) && (addr == 16);
endmodule
