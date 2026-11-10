# fuzz_parse_msg known findings

Reproducers live here, not in `testsuite/`: seeds are replayed by every run, and the first one
below only makes a run slow. Replay one with `/mayhem/fuzz_parse_msg-standalone <file>`.

## http_short_len_underflow.bin: infinite loop in `http_request` (src/http.c)

- Input: `08 00 08 00 7a ff ff 0e ff 02 00 00 06` (an HTTP proxy frame with a short payload).
- Cause: `len` is an `int`; `len -= 1; len -= 18` goes negative on a short payload. The `len == 0`
  check misses it and `buffer_put(&conn->wb, len)` gets a size near 2^64. `buffer_resize` then
  loops forever, because `new_size <<= 1` overflows to 0.
- Impact: remote hang of the rtty client by whoever controls the server stream.
- Fix: reject the frame when the remaining `len` is below the fixed header size.
- Mayhem does not report this as a defect: a replay that exceeds the Mayhemfile `timeout:` is
  recorded as a `timed_out` testcase with no defect, and a libFuzzer `-timeout` flag in the
  Mayhemfile `cmd` makes run creation fail. With the input shipped as a seed (run 2 at `timeout: 30`
  and run 3 at `timeout: 5`) the target stayed at 0 hang defects, and run 2 took 1364s.
  The target is unguarded, so the hang is only visible when replaying the file.

## term_data_null_zero_offset.bin: null pointer plus zero in `buffer_put` (buffer.c:113)

- Found by Mayhem (run 3, defect 3594907, UBSan `pointer-overflow`).
- Cause: a terminal-data frame with length 0 reaches `write_data_to_tty` (src/term.c), which calls
  `buffer_put(&tty->tty.wb, 0)` on a never-written (NULL) buffer, so `b->tail += len` is `NULL + 0`.
- Impact: undefined behaviour only; no memory is touched. UBSan is left on and halting.
