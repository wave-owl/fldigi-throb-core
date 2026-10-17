/*
 * fldigi-throb-core: version, submode table, Throb/ThrobX tables, pulses and the per-submode
 * parameters of fldigi 4.2.13's throb constructor.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain; ported from fldigi 4.2.13 src/throb/throb.cxx
 * (c) 2006-2010 Dave Freese W1HKJ, ThrobX additions by Joe Veldhuis KD8ATU.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "throb_internal.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#define STR2(x) #x
#define STR(x) STR2(x)

const char *fthrob_version(void)
{
    return "fldigi-throb-core " FTHROB_VERSION_STRING " contract=throb-c1 abi=" STR(FTHROB_ABI)
           " tx=1 dsp=" FDSP_VERSION_STRING;
}

int fthrob_abi(void)
{
    return FTHROB_ABI;
}

/* Submodes: globals/globals.cxx:218-223 (trx_mode ids 132-137 of fldigi 4.2.13), symbol
 * lengths, pulses, tone sets and receive filters of the constructor throb.cxx:141-220. */
static const fthrob_submode SUBMODES[6] = {
    {"THROB1", 132, 8192, 9, 45, 0, 0, 8.0, 64.0, 36.0, 8000.0 / 8192},
    {"THROB2", 133, 4096, 9, 45, 0, 0, 8.0, 64.0, 36.0, 8000.0 / 4096},
    {"THROB4", 134, 2048, 9, 45, 0, 1, 16.0, 128.0, 72.0, 8000.0 / 2048},
    {"THRBX1", 135, 8192, 11, 55, 1, 0, 7.8125, 78.125, 47.0, 8000.0 / 8192},
    {"THRBX2", 136, 4096, 11, 55, 1, 0, 7.8125, 78.125, 47.0, 8000.0 / 4096},
    {"THRBX4", 137, 2048, 11, 55, 1, 1, 15.625, 156.25, 94.0, 8000.0 / 2048},
};

int fthrob_submode_count(void)
{
    return 6;
}

const fthrob_submode *fthrob_submode_at(int index)
{
    return index >= 0 && index < 6 ? &SUBMODES[index] : NULL;
}

const fthrob_submode *fthrob_submode_find(const char *name)
{
    int i;
    size_t k;
    if (!name)
        return NULL;
    for (i = 0; i < 6; i++) {
        const char *s = SUBMODES[i].name;
        for (k = 0; s[k] && toupper((unsigned char)name[k]) == s[k]; k++)
            ;
        if (!s[k] && !name[k])
            return &SUBMODES[i];
    }
    return NULL;
}

/* throb.cxx:729-775 ThrobTonePairs (1-based tones; idle {5,5}, shift {4,6}) */
static const int ThrobTonePairs[45][2] = {
    {5, 5}, {4, 5}, {1, 2}, {1, 3}, {1, 4}, {4, 6}, {1, 5}, {1, 6}, {1, 7}, {3, 7},
    {1, 8}, {2, 3}, {2, 4}, {2, 8}, {2, 5}, {5, 6}, {2, 6}, {2, 9}, {3, 4}, {3, 5},
    {1, 9}, {3, 6}, {8, 9}, {3, 8}, {3, 3}, {2, 2}, {1, 1}, {3, 9}, {4, 7}, {4, 8},
    {4, 9}, {5, 7}, {5, 8}, {5, 9}, {6, 7}, {6, 8}, {6, 9}, {7, 8}, {7, 9}, {8, 8},
    {7, 7}, {6, 6}, {4, 4}, {9, 9}, {2, 7}};

/* throb.cxx:777-833 ThrobXTonePairs (idle {6,11} and space {1,6} initially; {1,11} = LF,
 * "COMPLETELY NONSTANDARD" in fldigi) */
static const int ThrobXTonePairs[55][2] = {
    {6, 11}, {1, 6}, {2, 6}, {2, 5}, {2, 7}, {2, 8}, {5, 6}, {2, 9}, {2, 10}, {4, 8},
    {4, 6}, {2, 11}, {3, 4}, {3, 5}, {3, 6}, {6, 9}, {6, 10}, {3, 7}, {3, 8}, {3, 9},
    {6, 8}, {6, 7}, {3, 10}, {3, 11}, {4, 5}, {4, 7}, {4, 9}, {4, 10}, {1, 2}, {1, 3},
    {1, 4}, {1, 5}, {1, 7}, {1, 8}, {1, 9}, {1, 10}, {2, 3}, {2, 4}, {4, 11}, {5, 7},
    {5, 8}, {5, 9}, {5, 10}, {5, 11}, {7, 8}, {7, 9}, {7, 10}, {7, 11}, {8, 9}, {8, 10},
    {8, 11}, {9, 10}, {9, 11}, {10, 11}, {1, 11}};

/* throb.cxx:835-881 ThrobCharSet (index 0 idle, 5 shift: NUL) */
static const unsigned char ThrobCharSet[45] = {
    '\0', 'A', 'B', 'C', 'D', '\0', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
    'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '0', ',', '.', '\'', '/', ')', '(', 'E', ' '};

/* throb.cxx:883-939 ThrobXCharSet (index 0 idle, 1 space, 54 LF) */
static const unsigned char ThrobXCharSet[55] = {
    '\0', ' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '1', '2',
    '3', '4', '5', '6', '7', '8', '9', '0', ',', '.', '\'', '/', ')', '(', '#',
    '"', '+', '-', ';', ':', '?', '!', '@', '=', '\n'};

/* throb.cxx:941-944 tone frequencies relative to the carrier, Hz */
static const double ThrobToneFreqsNar[9] = {-32, -24, -16, -8, 0, 8, 16, 24, 32};
static const double ThrobToneFreqsWid[9] = {-64, -48, -32, -16, 0, 16, 32, 48, 64};
static const double ThrobXToneFreqsNar[11] = {-39.0625, -31.25, -23.4375, -15.625, -7.8125, 0,
                                              7.8125, 15.625, 23.4375, 31.25, 39.0625};
static const double ThrobXToneFreqsWid[11] = {-78.125, -62.5, -46.875, -31.25, -15.625, 0,
                                              15.625, 31.25, 46.875, 62.5, 78.125};

int throb_params_init(throb_params *p, const char *submode)
{
    /* constructor, throb.cxx:132-256: per-submode symbol length, pulse, tone set and
     * receive filter; rxsymlen = symlen / DOWN_SAMPLE; bandwidth = span of the tones */
    const fthrob_submode *s = fthrob_submode_find(submode);
    const double *f;
    int i;
    if (!s)
        return -1;
    memset(p, 0, sizeof *p);
    p->sub = s;
    p->symlen = s->symlen;
    p->rxsymlen = s->symlen / THROB_DOWN_SAMPLE;
    p->num_tones = s->num_tones;
    p->num_chars = s->num_chars;
    p->throbx = s->throbx;
    p->full_pulse = s->full_pulse;
    if (s->throbx)
        f = s->full_pulse ? ThrobXToneFreqsWid : ThrobXToneFreqsNar;
    else
        f = s->full_pulse ? ThrobToneFreqsWid : ThrobToneFreqsNar;
    for (i = 0; i < p->num_tones; i++)
        p->freqs[i] = f[i];
    p->bw = s->rx_filter_bw / THROB_SAMPLE_RATE;
    p->bandwidth = p->freqs[p->num_tones - 1] - p->freqs[0];
    return 0;
}

double throb_clamp_freq(const throb_params *p, double freq)
{
    /* modem::set_freq, modem.cxx:319-324: CLAMP(freq, Low + bw/2, High - bw/2) */
    double lo = THROB_LOW_CUTOFF + p->bandwidth / 2, hi = THROB_HIGH_CUTOFF - p->bandwidth / 2;
    return freq > hi ? hi : (freq < lo ? lo : freq);
}

int throb_carrier_ok(const throb_params *p, double freq)
{
    /* the carriers modem::set_freq (modem.cxx:319-324) leaves unchanged */
    return isfinite(freq) && throb_clamp_freq(p, freq) == freq;
}

void throb_semi_pulse(double *pulse, int len)
{
    /* mk_semi_pulse, throb.cxx:542-566: raised-cosine edges over len/5 samples */
    double x;
    int i, j;
    for (i = 0; i < len; i++) {
        if (i < len / 5) {
            x = M_PI * i / (len / 5.0);
            pulse[i] = 0.5 * (1 - cos(x));
        }
        if (i >= len / 5 && i < len * 4 / 5)
            pulse[i] = 1.0;
        if (i >= len * 4 / 5) {
            j = i - len * 4 / 5;
            x = M_PI * j / (len / 5.0);
            pulse[i] = 0.5 * (1 + cos(x));
        }
    }
}

void throb_full_pulse(double *pulse, int len)
{
    /* mk_full_pulse, throb.cxx:568-579 */
    int i;
    for (i = 0; i < len; i++)
        pulse[i] = 0.5 * (1 - cos(2 * M_PI * i / len));
}

void throb_pair(const throb_params *p, int symbol, int *t1, int *t2)
{
    /* the table lookup of send, throb.cxx:591-602 */
    const int (*pairs)[2] = p->throbx ? ThrobXTonePairs : ThrobTonePairs;
    *t1 = pairs[symbol][0];
    *t2 = pairs[symbol][1];
}

unsigned char throb_char(const throb_params *p, int index)
{
    /* ThrobCharSet / ThrobXCharSet, throb.cxx:835-939 */
    return p->throbx ? ThrobXCharSet[index] : ThrobCharSet[index];
}

void throb_reset_syms(const throb_params *p, int *idlesym, int *spacesym)
{
    /* reset_syms, throb.cxx:116-130 */
    *idlesym = 0;
    *spacesym = p->throbx ? 1 : 44;
}

void throb_flip_syms(const throb_params *p, int *idlesym, int *spacesym)
{
    /* flip_syms, throb.cxx:96-114: ThrobX alternates idle and space; Throb does nothing */
    if (!p->throbx)
        return;
    if (*idlesym == 0) {
        *idlesym = 1;
        *spacesym = 0;
    } else {
        *idlesym = 0;
        *spacesym = 1;
    }
}
