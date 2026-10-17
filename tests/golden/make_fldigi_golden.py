#!/usr/bin/env python3
"""Golden data from fldigi 4.2.13 itself for fldigi-throb-core (stdlib only).

Drives the headless fldigi bench build (configure --enable-benchmark plus the TX
extension of wave-owl's archived change fix-psk-rtty-vs-fldigi, bench/BUILD.md) for every
Throb submode of fldigi 4.2.13 (src/globals/globals.cxx:218-223):

* TX: fldigi renders the text (bench-tx, txlevel -12 dB, PCM16, modem rate). The FNV-1a 64
  hash of every vector goes to tx_hashes.txt; the CQ overs are kept as raw little-endian
  int16 in tx/<name>.s16.
* RX: fldigi decodes 1 s silence + the TX vector + tail s silence, clean and buried in
  deterministic noise at -5 and -10 dB (2500 Hz reference bandwidth, see noisy()). The raw
  bytes fldigi hands to put_rx_char() go to rx/<name>_<snr>.txt (NUL included). The tail
  is 12 s. Throb is narrow (64-156 Hz), so the noisy steps go down to -20 dB in 2500 Hz.
* AFC: fldigi transmits at 1500 + offset Hz and decodes at 1500 Hz with --benchmark-afc 1.

    FLDIGI_BENCH_BIN=/path/to/src-bench/src/fldigi python3 tests/golden/make_fldigi_golden.py

The C tests (tests/test_throb_fldigi.c) rebuild the same noisy inputs bit for bit.
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations

import array
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
FLDIGI = os.environ.get("FLDIGI_BENCH_BIN", "fldigi")
CARRIER_HZ = 1500
TEXTS = {"cq": "CQ CQ DE DG6FL DG6FL K",
         "pangram": "the quick brown fox jumps over the lazy dog 0123456789 ,.'/()?@-=#\"+;:! "
                    "<x> 73"}

# (stem, fldigi trx_mode id = index into mode_info[], sname)
THROB = [("throb1", 132, "THROB1"), ("throb2", 133, "THROB2"), ("throb4", 134, "THROB4"),
         ("thrbx1", 135, "THRBX1"), ("thrbx2", 136, "THRBX2"), ("thrbx4", 137, "THRBX4")]
TAIL_S = {}
# AFC (--benchmark-afc 1, receiver at 1500 Hz): fldigi transmits at 1500 + offset Hz.
AFC = [("throb2_afc", 133, "THROB2", 3), ("throb4_afc", 134, "THROB4", -6),
       ("thrbx2_afc", 136, "THRBX2", -3), ("thrbx4_afc", 137, "THRBX4", 5)]
STORED = {"throb1", "throb2", "throb4", "thrbx1", "thrbx2", "thrbx4"}
SNRS = ("clean", "m05", "m10", "m15", "m20")
#: 1 / 10^(snr/10) built from sqrt(10) and powers of ten only (correctly rounded in C too)
INV = {"m05": "1/s", "m10": "1/10", "m15": "1/(10s)", "m20": "1/100"}


def cases():
    out = [(stem, mid, sname, "cq") for stem, mid, sname in THROB]
    out += [(stem + "_pangram", mid, sname, "pangram") for stem, mid, sname in THROB]
    return out


def afc_cases():
    return [(stem, mid, sname, "cq", off) for stem, mid, sname, off in AFC]


def tail_s(stem):
    return TAIL_S.get(stem.split("_")[0], 12)


def run_fldigi(mid, sname, args, env_extra=None, freq=CARRIER_HZ):
    cfg = tempfile.mkdtemp(prefix="fldigi-throb-golden-")
    Path(cfg, "fldigi_def.xml").write_text("<FLDIGI_DEFS>\n</FLDIGI_DEFS>\n")
    env = {k: v for k, v in os.environ.items() if not k.startswith("FLDIGI_BENCH_TX")}
    env.update(env_extra or {})
    try:
        p = subprocess.run([FLDIGI, "--config-dir", cfg, "--benchmark-modem", str(mid),
                            "--benchmark-frequency", str(freq), *args],
                           env=env, capture_output=True, text=True, timeout=3600)
    finally:
        shutil.rmtree(cfg, ignore_errors=True)
    if f"mode={sname} " not in p.stderr:
        raise RuntimeError(f"fldigi did not run {sname}: {p.stderr[-400:]}")
    return p.stderr


def read_wav(path):
    with wave.open(str(path)) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
        sr = w.getframerate()
        a = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        a.byteswap()
    return sr, a


def write_wav(path, sr, pcm):
    a = array.array("h", pcm)
    if sys.byteorder == "big":
        a.byteswap()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(a.tobytes())


def fnv1a64(pcm):
    h = 0xCBF29CE484222325
    for v in pcm:
        u = v & 0xFFFF
        for byte in (u & 0xFF, u >> 8):
            h ^= byte
            h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class Lcg:
    """32-bit LCG (Numerical Recipes); the same in tests/test_throb_fldigi.c."""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def uniform(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s / 4294967296.0


def noisy(pcm, sr, snr, seed, tail):
    """1 s + pcm + tail s, plus Irwin-Hall Gaussian noise at -5 ... -20 dB SNR in 2500 Hz.

    Only +, -, *, / and sqrt (all correctly rounded), so C reproduces it bit for bit.
    SNR = mean square of the burst / (noise variance * 2500 / (sr / 2)).
    """
    lead = sr
    padded = [0.0] * lead + [float(v) for v in pcm] + [0.0] * (tail * sr)
    if snr == "clean":
        return [int(v) for v in padded]
    ps = 0.0
    for v in pcm:
        ps += float(v) * float(v)
    ps = ps / len(pcm)
    sq = math.sqrt(10.0)
    inv = {"m05": 1.0 / sq, "m10": 1.0 / 10.0, "m15": 1.0 / (10.0 * sq),
           "m20": 1.0 / 100.0}[snr]
    s2 = ps / inv * (sr / 2.0) / 2500.0
    sd = math.sqrt(s2)
    gain = 3276.7 / math.sqrt(ps * len(pcm) / len(padded) + s2)
    rng = Lcg(seed)
    out = []
    for v in padded:
        z = 0.0
        for _ in range(12):
            z += rng.uniform()
        z -= 6.0
        y = (v + sd * z) * gain
        q = math.floor(y + 0.5)
        out.append(max(-32768, min(32767, int(q))))
    return out


def one(idx, case):
    stem, mid, sname, key = case
    with tempfile.TemporaryDirectory() as td:
        wav = Path(td, stem + ".wav")
        run_fldigi(mid, sname, ["--benchmark-input", "1"],
                   {"FLDIGI_BENCH_TX_TEXT": TEXTS[key], "FLDIGI_BENCH_TX_WAV": str(wav)})
        sr, pcm = read_wav(wav)
        line = (f"{stem} {sname} {key} {idx} {sr} {len(pcm)} {tail_s(stem)} "
                f"{fnv1a64(pcm):016x}")
        if stem in STORED:
            raw = array.array("h", pcm)
            if sys.byteorder == "big":
                raw.byteswap()
            Path(HERE, "tx", stem + ".s16").write_bytes(raw.tobytes())
        for i, snr in enumerate(SNRS):
            inp = Path(td, f"{stem}_{snr}.wav")
            write_wav(inp, sr, noisy(pcm, sr, snr, 1000 * idx + i, tail_s(stem)))
            out = Path(td, f"rx_{snr}.txt")
            run_fldigi(mid, sname, ["--benchmark-input", str(inp),
                                    "--benchmark-output", str(out)])
            text = out.read_bytes() if out.exists() else b""
            Path(HERE, "rx", f"{stem}_{snr}.txt").write_bytes(text)
    print(line, flush=True)
    return line


def one_afc(idx, case):
    """TX at 1500 + offset Hz, RX with AFC at 1500 Hz; noise seed 1000 * idx + step."""
    stem, mid, sname, key, off = case
    with tempfile.TemporaryDirectory() as td:
        wav = Path(td, stem + ".wav")
        run_fldigi(mid, sname, ["--benchmark-input", "1"],
                   {"FLDIGI_BENCH_TX_TEXT": TEXTS[key], "FLDIGI_BENCH_TX_WAV": str(wav)},
                   freq=CARRIER_HZ + off)
        sr, pcm = read_wav(wav)
        line = (f"{stem} {sname} {key} {idx} {sr} {len(pcm)} {tail_s(stem)} {off} "
                f"{fnv1a64(pcm):016x}")
        for i, snr in enumerate(SNRS):
            inp = Path(td, f"{stem}_{snr}.wav")
            write_wav(inp, sr, noisy(pcm, sr, snr, 1000 * idx + i, tail_s(stem)))
            out = Path(td, f"rx_{snr}.txt")
            run_fldigi(mid, sname, ["--benchmark-input", str(inp), "--benchmark-afc", "1",
                                    "--benchmark-output", str(out)])
            text = out.read_bytes() if out.exists() else b""
            Path(HERE, "rx", f"{stem}_{snr}.txt").write_bytes(text)
    print(line, flush=True)
    return line


def main():
    (HERE / "tx").mkdir(exist_ok=True)
    (HERE / "rx").mkdir(exist_ok=True)
    only = set(sys.argv[1:])
    todo = [(i, c) for i, c in enumerate(cases()) if not only or c[0] in only]
    with ThreadPoolExecutor(max_workers=int(os.environ.get("JOBS", "8"))) as ex:
        lines = list(ex.map(lambda a: one(*a), todo))
    base = len(cases())
    todo_afc = [(base + i, c) for i, c in enumerate(afc_cases()) if not only or c[0] in only]
    with ThreadPoolExecutor(max_workers=int(os.environ.get("JOBS", "8"))) as ex:
        afc_lines = list(ex.map(lambda a: one_afc(*a), todo_afc))
    if only:
        return 0
    (HERE / "afc.txt").write_text(
        "# stem fldigi-sname text index samplerate samples tail-s tx-offset-Hz fnv1a64 -- "
        "fldigi TX at 1500 + offset, fldigi RX at 1500 with --benchmark-afc 1\n"
        + "\n".join(afc_lines) + "\n")
    header = ("# stem fldigi-sname text index samplerate samples tail-s fnv1a64(int16 LE)"
              " -- fldigi 4.2.13 bench TX, txlevel -12 dB; noise seed = 1000 * index + snr"
              " step\n")
    (HERE / "tx_hashes.txt").write_text(header + "\n".join(lines) + "\n")
    (HERE / "texts.txt").write_text("".join(f"{k} {v}\n" for k, v in TEXTS.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
