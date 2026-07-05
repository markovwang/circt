class Packet;
  rand int len;
  constraint c { len > 0; }
endclass

function int randomize_packet();
  automatic Packet p = new;
  bit ok;
  ok = p.randomize();
  return ok ? 0 : 1;
endfunction

export "DPI-C" randomize_packet = function randomize_packet;
