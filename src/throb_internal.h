/*
 * fldigi-throb-core internals: submode parameters, tables and pulses shared by the
 * receiver and the transmitter (fldigi 4.2.13 src/throb/throb.cxx, src/include/throb.h).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef THROB_INTERNAL_H
#define THROB_INTERNAL_H

#include "fldigi_throb.h"
#include "fldigi_dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define THROB_SAMPLE_RATE 8000      /* throb.h:34 */
#define THROB_DOWN_SAMPLE 32        /* throb.h:40 */
#define THROB_SYMLEN_1 8192         /* throb.h:42-44 */
#define THROB_MAX_RX_SYMLEN (THROB_SYMLEN_1 / THROB_DOWN_SAMPLE)   /* throb.h:46 */
#define THROB_FILTER_FFT_LEN 8192   /* throb.h:48 */
#define THROB_MAX_TONES 15          /* throb.cxx:40 */
#define THROB_MAX_CHARS 55
/* fldigi's waterfall limits LowFreqCutoff 0 / HighFreqCutoff 3000 (configuration.h:204-209)
 * bound the carrier in modem::set_freq (modem.cxx:319-324). */
#define THROB_LOW_CUTOFF 0.0
#define THROB_HIGH_CUTOFF 3000.0

/* One submode's modem parameters as fldigi's constructor sets them. */
typedef struct throb_params {
    const fthrob_submode *sub;
    int symlen;
    int rxsymlen;
    int num_tones;
    int num_chars;
    int throbx;
    int full_pulse;
    double freqs[THROB_MAX_TONES];
    double bw;                      /* RX low pass, fraction of the sample rate */
    double bandwidth;               /* freqs[num_tones - 1] - freqs[0] */
} throb_params;

int throb_params_init(throb_params *p, const char *submode);
double throb_clamp_freq(const throb_params *p, double freq);
int throb_carrier_ok(const throb_params *p, double freq);
void throb_semi_pulse(double *pulse, int len);
void throb_full_pulse(double *pulse, int len);
/* Tone pair (1-based, as fldigi's tables) of a symbol index. */
void throb_pair(const throb_params *p, int symbol, int *t1, int *t2);
unsigned char throb_char(const throb_params *p, int index);
/* fldigi's idle/space symbol state (throb.cxx:96-130). */
void throb_reset_syms(const throb_params *p, int *idlesym, int *spacesym);
void throb_flip_syms(const throb_params *p, int *idlesym, int *spacesym);

#endif
