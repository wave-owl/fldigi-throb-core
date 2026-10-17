/*
 * fldigi-throb-core -- the Throb and ThrobX modem of fldigi 4.2.13 (THROB1, THROB2, THROB4,
 * THRBX1, THRBX2, THRBX4), receiver and transmitter, ported to C99.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain
 * Ported from fldigi 4.2.13 (https://github.com/wave-owl/fldigi, tag v4.2.13, commit
 * e7c3ab3709ad62a7d91829364eb48a05fa325c0d): src/throb/throb.cxx, src/include/throb.h and the
 * carrier clamp of src/trx/modem.cxx; (c) 2006-2010 Dave Freese W1HKJ, ThrobX additions
 * by Joe Veldhuis KD8ATU, adapted from gmfsk (c) 2001-2003 Tomi Manninen OH2BNS. Throb was
 * designed by Lionel Sear G3PPT.
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this
 * program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef FLDIGI_THROB_H
#define FLDIGI_THROB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FTHROB_VERSION_STRING "0.1.1"
#define FTHROB_ABI 1
/* The modem rate of every submode (throb.h:34, THROB_SAMPLE_RATE). */
#define FTHROB_RATE 8000

/* "fldigi-throb-core 0.1.1 contract=throb-c1 abi=1 tx=1 dsp=0.4.1". Static string. The
 * contract tag changes only with an incompatible API change; tx=1 announces the
 * transmitter. */
const char *fthrob_version(void);
int fthrob_abi(void);                   /* FTHROB_ABI */

/* ------------------------------------------------------------------------------------
 * Submodes: every Throb mode of fldigi 4.2.13 (globals/globals.cxx:218-223; parameters
 * throb/throb.cxx:132-256).
 * ------------------------------------------------------------------------------------ */
typedef struct fthrob_submode {
    const char *name;        /* "THROB1" ... "THRBX4" (fldigi's short name) */
    int fldigi_id;           /* trx_mode id of fldigi 4.2.13 (index into mode_info[]) */
    int symlen;              /* samples per symbol at 8000 Hz (8192, 4096 or 2048) */
    int num_tones;           /* 9 (Throb) or 11 (ThrobX) */
    int num_chars;           /* 45 (Throb) or 55 (ThrobX) */
    int throbx;              /* 1: ThrobX character set and idle/space alternation */
    int full_pulse;          /* 1: raised-cosine full pulse (x4), 0: semi pulse */
    double tone_spacing;     /* Hz between adjacent tones */
    double bandwidth;        /* fldigi's span: highest - lowest tone, Hz */
    double rx_filter_bw;     /* fldigi's receive low pass, Hz (36, 72, 47 or 94) */
    double baud;             /* 8000 / symlen */
} fthrob_submode;

int fthrob_submode_count(void);                       /* 6 */
const fthrob_submode *fthrob_submode_at(int index);   /* NULL outside 0 .. count-1 */
/* Case-insensitive lookup by name; NULL when unknown. */
const fthrob_submode *fthrob_submode_find(const char *name);

/* ------------------------------------------------------------------------------------
 * Receiver: throb::rx_process (throb.cxx:505-536) with fldigi's state machine.
 * ------------------------------------------------------------------------------------ */
typedef struct fthrob_rx_config {
    const char *submode;     /* see fthrob_submode_find */
    double carrier_hz;       /* audio centre of the tones (fldigi "frequency"), 1500 */
    int afc;                 /* 1: fldigi's AFC (progStatus.afconoff); 0: off (bench) */
    int squelch_on;          /* 1: fldigi's squelch (progStatus.sqlonoff); 0: off (bench) */
    double squelch;          /* fldigi's squelch level in metric units (default 5.0) */
    int reverse;             /* 1: mirrored tones (fldigi's REV / LSB) */
} fthrob_rx_config;

typedef struct fthrob_rx fthrob_rx;

/* NULL for an unknown submode, a carrier outside fldigi's range (bandwidth/2 ..
 * 3000 Hz - bandwidth/2, modem::set_freq with the default waterfall limits) or out of
 * memory. */
fthrob_rx *fthrob_rx_create(const fthrob_rx_config *cfg);
void fthrob_rx_destroy(fthrob_rx *rx);
/* Back to the state after create (configured carrier, filters cleared). */
void fthrob_rx_reset(fthrob_rx *rx);
/* Feed n samples at 8000 Hz (fldigi's sound card scale: int16 / 32768). The characters
 * fldigi hands to put_rx_char() go to text, except NUL (the Throb idle pair, quirk Q6);
 * returns their number. Bytes beyond max_text are dropped and counted
 * (fthrob_rx_dropped). text may be NULL with max_text 0. */
size_t fthrob_rx_process(fthrob_rx *rx, const double *x, size_t n, unsigned char *text,
                         size_t max_text);
/* fldigi's status s/n: clamp(10 log10(metric) - 3, 0, 100) dB (throb.cxx:347). */
double fthrob_rx_snr_db(const fthrob_rx *rx);
double fthrob_rx_metric(const fthrob_rx *rx);       /* moving average of signal/noise */
/* Current carrier (moves with AFC) minus the configured carrier, Hz. */
double fthrob_rx_offset_hz(const fthrob_rx *rx);
double fthrob_rx_carrier_hz(const fthrob_rx *rx);
/* Retune (clamped like fldigi's set_freq); becomes the configured carrier. */
void fthrob_rx_set_carrier(fthrob_rx *rx, double carrier_hz);
void fthrob_rx_set_afc(fthrob_rx *rx, int on);
void fthrob_rx_set_squelch(fthrob_rx *rx, int on, double level);
void fthrob_rx_set_reverse(fthrob_rx *rx, int on);
size_t fthrob_rx_dropped(const fthrob_rx *rx);
/* Bytes the receiver allocated (all at create). */
size_t fthrob_rx_footprint(const fthrob_rx *rx);
/* Test tap: the tone pair of every symbol decided since the last call, as handed to
 * decodechar (after reverse), encoded 16 * tone1 + tone2 (tone1 <= tone2, 0-based), oldest
 * first, at most the last 1024. Returns the number copied. */
size_t fthrob_rx_take_pairs(fthrob_rx *rx, int *out, size_t max);

/* ------------------------------------------------------------------------------------
 * Transmitter: throb::tx_process (throb.cxx:619-721), one call per state step. Output: the
 * samples fldigi hands to modem::ModulateXmtr (throb.cxx:582-617, peak 1.0), before the TX
 * level (multiply by 10^(txlevel/20), fldigi's -12 dB, to reproduce fldigi's WAV). fldigi
 * sets no soft start or stop for Throb (quirk Q13).
 * ------------------------------------------------------------------------------------ */
typedef struct fthrob_tx_config {
    const char *submode;
    double carrier_hz;       /* audio centre (fldigi's tx frequency), clamped as RX */
    int reverse;             /* 1: mirrored tones */
} fthrob_tx_config;

typedef struct fthrob_tx fthrob_tx;

fthrob_tx *fthrob_tx_create(const fthrob_tx_config *cfg);   /* NULL as rx_create */
void fthrob_tx_destroy(fthrob_tx *tx);
/* Samples of one symbol, and the largest output of one call (4 symbols, fthrob_tx_begin). */
size_t fthrob_tx_symlen(const fthrob_tx *tx);
size_t fthrob_tx_bound(const fthrob_tx *tx);
/* Start of an over (tx_init and the preamble, throb.cxx:47-52, 625-630): 4 idle symbols
 * (ThrobX: idle/space alternate). Writes at most max samples, returns the number written
 * (all or nothing: 0 when max is too small). */
size_t fthrob_tx_begin(fthrob_tx *tx, double *out, size_t max);
/* One keyboard character (throb.cxx:648-720): 0 symbols (Throb CR), 1 symbol, or 2 symbols
 * (Throb shift + ? @ - and LF); lower case is sent upper case, unknown characters as space. */
size_t fthrob_tx_put(fthrob_tx *tx, unsigned char c, double *out, size_t max);
/* One idle symbol, as fldigi with an empty TX buffer (throb.cxx:641-646). */
size_t fthrob_tx_idle(fthrob_tx *tx, double *out, size_t max);
/* End of an over: the final idle symbol (throb.cxx:634-639). */
size_t fthrob_tx_end(fthrob_tx *tx, double *out, size_t max);
/* Symbols a character takes (0, 1 or 2) without sending it. */
int fthrob_tx_symbols(const fthrob_tx *tx, unsigned char c);

#ifdef __cplusplus
}
#endif

#endif
