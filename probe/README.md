# Raw-protocol probes

Small Python tools used to characterise a new model without any vendor
software (see PROVENANCE.md, "MFC-J5720DW"). They speak the protocol in
docs/PROTOCOL.md directly on TCP 54921.

- `adf_probe.py HOST [flatbed|adf] [DPI] [OUT.bin] [X0,Y0,X1,Y1]` — one scan
  job; prints every reply and dumps the ESC X byte stream to OUT.bin.
- `adf_select_probe.py HOST` — sends the feeder-select variants and prints
  the ESC I offer each produces (no ESC X, so no sheet is consumed).
- `fake_j5720.py DUMP.bin` — a fake printer on 127.0.0.1:54921 that replays a
  dump; point `build/brscan-cli --host 127.0.0.1` at it to iterate on the
  library offline.
