# Changelog

All notable changes to fldigi-throb-core. Versions follow semantic versioning; the contract
tag `throb-c1` changes only with an incompatible API change.

## 0.1.0 - 2026-10-17

First release, for wave-owl change `add-fldigi-throb-core` (umbrella
`add-fldigi-native-modes`).

- Receiver and transmitter for all six Throb submodes of fldigi 4.2.13 (THROB1, THROB2,
  THROB4, THRBX1, THRBX2, THRBX4), ported from `throb.cxx`: Hilbert front end, mixer,
  `fftfilt` low pass, decimation by 32, envelope sync, `cmac` correlation, two-tone decision
  with Throb's single-tone rule, Throb shift and ThrobX idle/space alternation, optional AFC,
  squelch, s/n and metric, reverse; fldigi's preamble, character quirks, idles and final
  idle symbol. Capability tag `tx=1`.
- fldigi's behaviours Q1-Q13 kept and tested (README); the Throb idle NUL is not printed.
- Built on fldigi-dsp-core 0.4.0 (`dsp/`, pinned by `dsp.lock`).
- Goldens from the fldigi 4.2.13 bench: 16 TX overs and 80 RX decodes (20 with AFC)
  identical to fldigi on the reference platform, also in the sanitizer build; unit tests;
  CPU and memory benchmark.
