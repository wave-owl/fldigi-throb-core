# fldigi-throb-core

[![CI](https://github.com/wave-owl/fldigi-throb-core/actions/workflows/ci.yml/badge.svg)](https://github.com/wave-owl/fldigi-throb-core/actions/workflows/ci.yml)

A C99 library that receives and transmits **Throb** and **ThrobX** exactly the way fldigi
4.2.13 does: audio in, characters out, and characters in, audio out, for all six fldigi
submodes (THROB1, THROB2, THROB4, THRBX1, THRBX2, THRBX4). It is a line-by-line port of
`src/throb/throb.cxx` and `src/include/throb.h` (plus the carrier clamp of
`src/trx/modem.cxx`), built on the shared
[fldigi-dsp-core](https://github.com/wave-owl/fldigi-dsp-core) (Hilbert FIR, fftfilt, FIR
`Irun`, moving average, `cmac`). On the reference build the transmitter reproduces fldigi's
TX audio sample for sample and the receiver prints fldigi's characters byte for byte, also
with AFC (see [Verification](#verification)). It is the only Throb implementation of
[wave-owl](https://github.com/wave-owl/wave-owl).

| | |
|---|---|
| **Is** | A single-channel **receiver** for every Throb submode of fldigi 4.2.13: the characters fldigi prints (without the NUL of the Throb idle pair), fldigi's AFC and squelch (both optional, off as in fldigi's bench), s/n and metric, reverse. |
| **Is** | The matching **transmitter** (capability tag `tx=1`): preamble, keyboard characters with fldigi's character quirks, idles and the final idle symbol, as fldigi's `tx_process`. |
| **Is not** | A skimmer: each instance decodes one submode at one carrier. No RSID, no CW ID, no rate conversion (Throb is defined at 8000 Hz). |
| | An I/O layer, threaded or allocating at run time: no sound card or file access, no threads, all memory allocated in `*_create()`, no global mutable state. |

## Contents

- [Throb in one paragraph](#throb-in-one-paragraph)
- [Submodes](#submodes)
- [Signal chain](#signal-chain)
- [fldigi source map](#fldigi-source-map)
- [fldigi behaviour kept on purpose](#fldigi-behaviour-kept-on-purpose)
- [API reference](#api-reference)
- [Usage example](#usage-example)
- [Contract and versioning](#contract-and-versioning)
- [Verification](#verification)
- [Performance and memory](#performance-and-memory)
- [Building](#building)
- [Maintenance and fldigi updates](#maintenance-and-fldigi-updates)
- [License and credits](#license-and-credits)

## Throb in one paragraph

Throb (Lionel Sear G3PPT, 2000) sends one character per symbol as **two simultaneous tones**
out of 9 (Throb) or 11 (ThrobX), shaped by a raised-cosine pulse; a few characters use a
single tone. Throb has a shift symbol for `?`, `@`, `=` and line feed; ThrobX has a larger
character set and alternates two tone pairs for idle and space so that runs of spaces stay
visible. The receiver correlates each symbol against all tone prototypes, picks the two
strongest, and finds the symbol timing from the envelope.

## Submodes

All at 8000 Hz, decimation 32 in the receiver, `fftfilt` length 8192 (`throb.cxx:132-256`,
`globals.cxx:218-223`):

| Submode | fldigi id | Symbol (samples) | Baud | Pulse | Tones | Spacing | Span | RX low pass |
|---|---|---|---|---|---|---|---|---|
| THROB1 | 132 | 8192 | 0.977 | semi | 9 | 8 Hz | 64 Hz | 36 Hz |
| THROB2 | 133 | 4096 | 1.953 | semi | 9 | 8 Hz | 64 Hz | 36 Hz |
| THROB4 | 134 | 2048 | 3.906 | full | 9 | 16 Hz | 128 Hz | 72 Hz |
| THRBX1 | 135 | 8192 | 0.977 | semi | 11 | 7.8125 Hz | 78.125 Hz | 47 Hz |
| THRBX2 | 136 | 4096 | 1.953 | semi | 11 | 7.8125 Hz | 78.125 Hz | 47 Hz |
| THRBX4 | 137 | 2048 | 3.906 | full | 11 | 15.625 Hz | 156.25 Hz | 94 Hz |

THRBX4 is marked "NONSTANDARD" in fldigi; it is ported as fldigi has it.

## Signal chain

Receiver (`throb::rx_process`): real audio -> `(x, x)` -> Hilbert FIR (37 taps) -> mixer at
the carrier -> `fftfilt` low pass -> every 32nd sample: envelope sync filter (`Irun` with
the symbol pulse) and symbol timing, correlation of the last symbol with every tone
prototype (`cmac`), two strongest tones -> character table -> squelch -> text; AFC from the
phase step of the strongest tone.

Transmitter (`throb::tx_process`): 4 idle symbols, one symbol per character (two for the
Throb shift characters), idle symbols while no text is queued, one final idle symbol. Each
symbol is `pulse(i) * (sin(w1 i) + sin(w2 i)) / 2` with the phase restarting at 0, peak 1.0
before fldigi's TX level.

## fldigi source map

fldigi 4.2.13 (mirror `wave-owl/fldigi`, tag v4.2.13, commit e7c3ab37):

| fldigi lines (`src/throb/throb.cxx`) | Function | Here |
|---|---|---|
| 47-52 | `tx_init` (preamble 4, `reset_syms`) | `throb_tx.c` `fthrob_tx_begin` |
| 54-64, 66-81 | `rx_init`, `init` | `throb_rx.c` `rx_init`, `clear_state` |
| 96-130 | `flip_syms`, `reset_syms` | `throb_common.c` |
| 132-256 | constructor: parameters, filters, pulses | `throb_common.c` `throb_params_init`, `throb_rx.c` `fthrob_rx_create` |
| 264-279 | `mk_rxtone` | `throb_rx.c` |
| 281-295 | `mixer` | `throb_rx.c` |
| 297-350 | `findtones` + metric | `throb_rx.c` |
| 352-417 | `show_char`, `decodechar` | `throb_rx.c` |
| 419-464 | `rx` (`cmac` correlation, AFC) | `throb_rx.c` `rx_symbol` |
| 466-503 | `sync` | `throb_rx.c` `sync_symbol` |
| 505-536 | `rx_process` | `throb_rx.c` `fthrob_rx_process` |
| 542-579 | `mk_semi_pulse`, `mk_full_pulse` | `throb_common.c` |
| 582-617 | `send` | `throb_tx.c` `tx_send` |
| 619-721 | `tx_process` | `throb_tx.c` |
| 729-944 | tone pairs, character sets, tone frequencies | `throb_common.c` |
| `modem.cxx:319-324` | `set_freq` clamp | `throb_common.c` `throb_clamp_freq` |

From fldigi-dsp-core 0.4.1: `fdsp_fir_init_hilbert`/`fdsp_fir_run` (Hilbert), `fdsp_fftfilt_*`,
`fdsp_fir_init`/`fdsp_fir_irun` (sync filter), `fdsp_movavg_*` (s/n filter), `fdsp_cmac`,
`fdsp_sincos`.

## fldigi behaviour kept on purpose

The core reproduces fldigi bit for bit; each behaviour below has a test, so that a later
"fix" is a visible decision:

| # | Behaviour | fldigi | Test |
|---|---|---|---|
| Q1 | The `sync` correction `(maxpos - rxsymlen/2) / (num_tones - 1)` uses C integer division | `throb.cxx:493-494` | fldigi goldens |
| Q2 | Throb (not ThrobX): a single tone counts when `max1 > 2 max2` | `throb.cxx:324-329` | `test_rx_quirks` (Z = {1,1}) |
| Q3 | `s2n = clamp(10 log10(metric) - 3, 0, 100)` | `throb.cxx:347` | `test_rx_quirks` |
| Q4 | AFC `set_freq(f + delta / (num_tones - 1))`, clamped to `[bw/2, 3000 - bw/2]` | `throb.cxx:442-454`, `modem.cxx:319-324` | `test_rx_quirks`, AFC goldens |
| Q5 | Reverse mirrors the tone indices on RX and TX | `throb.cxx:437-440, 604-607` | `test_reverse` |
| Q6 | The Throb idle pair (5,5) decodes to NUL; the core does not print it (fldigi's bench records it) | `throb.cxx:387-393` | `test_loopback`, goldens with NUL stripped |
| Q7 | Throb TX `-` is sent as shift + 9, which RX decodes as `=` | `throb.cxx:666-670, 372-373` | `test_loopback` |
| Q8 | Throb TX ignores CR; LF is shift + idle and decodes as LF; `?` and `@` use shift | `throb.cxx:654-678` | `test_loopback`, `test_tx_quirks` |
| Q9 | ThrobX TX CR becomes a space; LF is the pair {1,11} ("NONSTANDARD") | `throb.cxx:703-717, 832` | `test_loopback`, `test_tx_quirks` |
| Q10 | ThrobX RX prints one space for idle/space only after a non-space, then flips | `throb.cxx:396-413` | `test_throbx_spaces` |
| Q11 | TX upper-cases; unknown characters are sent as space | `throb.cxx:689-715` | `test_tx_quirks` |
| Q12 | Preamble of 4 idle symbols (ThrobX alternating), one final idle symbol | `throb.cxx:47-52, 625-639` | `test_tx_quirks` |
| Q13 | Symbol phase restarts at every symbol; no soft start/stop | `throb.cxx:609-616` | `test_tx_quirks`, TX goldens |

Also as fldigi: on silence or noise with the squelch off the receiver prints whatever pair
wins (for Throb often `Z`, the single lowest tone); fldigi's own decodes in the goldens show
the same characters.

## API reference

All declarations are in `include/fldigi_throb.h`.

| Function | Description |
|---|---|
| `fthrob_version()`, `fthrob_abi()` | `"fldigi-throb-core 0.1.1 contract=throb-c1 abi=1 tx=1 dsp=0.4.1"`, `1`. |
| `fthrob_submode_count/_at/_find` | The six submodes (`fthrob_submode`: name, fldigi id, symlen, tones, characters, ThrobX flag, pulse, spacing, span, RX filter, baud). |
| `fthrob_rx_create(cfg)` | Receiver for `cfg->submode` at `cfg->carrier_hz` with `afc`, `squelch_on`, `squelch` (fldigi metric units, fldigi's default 5.0) and `reverse`. NULL for an unknown submode or a carrier fldigi's `set_freq` would move. |
| `fthrob_rx_process(rx, x, n, text, max)` | 8 kHz audio in (int16 / 32768 scale); the characters fldigi prints (no NUL) out; returns their number; overflow is counted (`fthrob_rx_dropped`). |
| `fthrob_rx_snr_db`, `_metric`, `_offset_hz`, `_carrier_hz` | fldigi's s/n, metric, AFC offset and current carrier. |
| `fthrob_rx_set_carrier/_afc/_squelch/_reverse`, `fthrob_rx_reset` | Runtime changes; reset returns to the state after create. |
| `fthrob_rx_footprint`, `fthrob_rx_take_pairs` | Bytes allocated; test tap of the decided tone pairs. |
| `fthrob_tx_create(cfg)` | Transmitter (`submode`, `carrier_hz`, `reverse`). |
| `fthrob_tx_begin/_put/_idle/_end(tx, out, max)` | One state step of `tx_process`: the preamble (4 symbols), one character (0-2 symbols), one idle symbol, the final idle symbol. Each writes all of its samples or none; `fthrob_tx_bound` is always enough. |
| `fthrob_tx_symlen`, `fthrob_tx_bound`, `fthrob_tx_symbols` | Samples per symbol, the largest step, the symbols a character takes. |

## Usage example

```c
fthrob_tx_config tc = {"THRBX2", 1500.0, 0};
fthrob_tx *tx = fthrob_tx_create(&tc);
double *buf = malloc(fthrob_tx_bound(tx) * sizeof *buf);
size_t n = fthrob_tx_begin(tx, buf, fthrob_tx_bound(tx));          /* preamble */
/* ... play n samples, then for each character: */
n = fthrob_tx_put(tx, 'C', buf, fthrob_tx_bound(tx));
n = fthrob_tx_end(tx, buf, fthrob_tx_bound(tx));                    /* final idle */

fthrob_rx_config rc = {"THRBX2", 1500.0, 0, 0, 5.0, 0};
fthrob_rx *rx = fthrob_rx_create(&rc);
unsigned char text[256];
size_t got = fthrob_rx_process(rx, audio, n_audio, text, sizeof text);
```

## Contract and versioning

The contract tag `throb-c1` and the ABI tag `abi=1` change only with an incompatible API
change; wave-owl refuses a library with other tags. Versions follow semantic versioning.
The `dsp/` submodule is pinned by `dsp.lock` (version, commit and content checksum of the
linked files); `tools/check_dsp.sh` runs before every build and refuses any other
fldigi-dsp-core (wave-owl R-920.3).

## Verification

`make test` runs two programs:

- `tests/test_throb.c` (independent of fldigi): API and submode table, carrier limits,
  loopback in all six submodes with both character sets and the asymmetries Q7-Q11, chunk
  invariance (1, 512, 10007 samples), reverse, squelch, reset, dropped characters, AFC
  pull-in and clamp, footprint under 1 MB, one case per behaviour of the table above.
- `tests/test_throb_fldigi.c`: goldens from the fldigi 4.2.13 bench build
  (`tests/golden/make_fldigi_golden.py`; headless fldigi with the TX extension of wave-owl's
  archived change `fix-psk-rtty-vs-fldigi`): 12 overs (each submode with
  `CQ CQ DE DG6FL DG6FL K` and with a pangram of every character class) and 4 AFC overs
  (fldigi TX 3 .. 6 Hz off the receiver, fldigi RX with `--benchmark-afc 1`). TX: the FNV-1a
  hash of the int16 output equals fldigi's, the stored CQ vectors match. RX: 80 decodes (clean,
  -5, -10, -15, -20 dB in 2500 Hz; deterministic noise rebuilt bit for bit in C) equal
  fldigi's output byte for byte after removing fldigi's NULs.

| | Result (macOS arm64, Apple clang `-O3`, the `-O1` sanitizer build, gcc 16 `-O2`) |
|---|---|
| TX overs identical to fldigi | 16 of 16 |
| RX decodes identical to fldigi | 80 of 80 (incl. 20 with AFC) |

On other platforms (another libm, x86 without FMA) the results need not be bit-identical;
there the clean decodes must stay within 1 + 5 % edit distance of fldigi's and the noisy
ones in total within 20 % + 10 errors. CI runs both programs on Ubuntu and macOS and again
under AddressSanitizer and UndefinedBehaviorSanitizer.

## Performance and memory

`make bench` (Apple M4, one core, 60 s of audio, AFC on), multiples of real time:

| | THROB1 | THROB2 | THROB4 | THRBX1 | THRBX2 | THRBX4 |
|---|---|---|---|---|---|---|
| Receiver | 2100 | 2900 | 3300 | 3000 | 3000 | 2800 |
| Transmitter | 10 000 | 19 000 | 24 000 | 23 000 | 23 000 | 20 000 |

One receiver needs about 0.05 % of one core at 8 kHz and allocates 0.75 MiB (mostly the
8192-point `fftfilt` and the two 4096-sample FIR buffers); `make bench` fails above 2 % or
1 MiB (wave-owl R-861.6).

## Building

```sh
git clone --recursive https://github.com/wave-owl/fldigi-throb-core
cd fldigi-throb-core
make            # build/libfldigi_throb.a and build/libfldigi_throb.{so,dylib}
make test       # unit tests and fldigi goldens
make sanitize   # the same under ASan + UBSan
make bench      # CPU and memory
```

C99, `libm` only. wave-owl builds it with `make build-throb-core` into
`third_party/build/libfldigi_throb.{dylib,so}`.

## Maintenance and fldigi updates

Every ported function cites its fldigi lines. When fldigi changes `throb.cxx`, wave-owl's
`scripts/fldigi_upstream_check.py` reports the affected citations; regenerate the goldens with
`FLDIGI_BENCH_BIN=/path/to/fldigi python3 tests/golden/make_fldigi_golden.py` and port the
change. A change to a behaviour of the quirk table needs a new wave-owl ADR.

## License and credits

GPL-3.0-or-later (see `LICENSE`), as fldigi. Throb was designed by Lionel Sear G3PPT. The
fldigi modem is (c) 2006-2010 Dave Freese W1HKJ, ThrobX additions by Joe Veldhuis KD8ATU,
adapted from gmfsk (c) 2001-2003 Tomi Manninen OH2BNS. Port (c) 2026 Gerolf Ziegenhain.
