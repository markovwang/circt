#include "randomize-arc.h"

#include <cstdio>

int main() {
  PacketSink dut;
  Packet pkt;

  if (!pkt.randomize()) {
    std::fprintf(stderr, "Packet randomize failed\n");
    return 1;
  }

  std::printf("packet: len=%d addr=0x%x\n", pkt.view.len, pkt.view.addr);

  dut.view.len = pkt.view.len;
  dut.view.addr = pkt.view.addr;
  dut.eval();

  if (!dut.view.ok) {
    std::fprintf(stderr, "PacketSink rejected randomized packet\n");
    return 2;
  }

  return 0;
}
