/*
 * fldigi-throb-core receiver: throb::rx_process and its helpers (fldigi 4.2.13
 * src/throb/throb.cxx:54-81, 264-536) on fldigi-dsp-core's Hilbert FIR, fftfilt, FIR (Irun),
 * moving average and cmac.
 *
 * Copyright (C) 2026 Gerolf Ziegenhain; ported from fldigi 4.2.13 src/throb/throb.cxx
 * (c) 2006-2010 Dave Freese W1HKJ, ThrobX additions by Joe Veldhuis KD8ATU, adapted from
 * gmfsk (c) 2001-2003 Tomi Manninen OH2BNS.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "throb_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PAIR_RING 1024

struct fthrob_rx {
    throb_params p;
    fdsp_fir hilbert;               /* init_hilbert(37, 1), throb.cxx:226-227 */
    fdsp_fftfilt fftfilter;         /* fftfilt(0, bw, FilterFFTLen), throb.cxx:229 */
    fdsp_fir syncfilt;              /* init(rxsymlen, 1, pulse, NULL), throb.cxx:231-233 */
    fdsp_movavg snfilter;           /* Cmovavg(16), throb.cxx:235 */
    double *syncpulse;              /* the sync filter's taps (kept for reset) */
    fdsp_cmpx *rxtone[THROB_MAX_TONES];
    fdsp_cmpx symbol[THROB_MAX_RX_SYMLEN];
    double syncbuf[THROB_MAX_RX_SYMLEN];
    double dispbuf[THROB_MAX_RX_SYMLEN];
    double phaseacc;
    double frequency;               /* current carrier (AFC moves it) */
    double carrier;                 /* configured carrier */
    double rxcntr;
    double signal, noise, metric, s2n;
    int symptr, deccntr, shift, waitsync;
    int idlesym, spacesym;
    unsigned char lastchar;
    int afc, squelch_on, reverse;
    double squelch;
    /* output of the current rx_process call */
    unsigned char *text;
    size_t max_text, n_text, dropped;
    int pairs[PAIR_RING];
    size_t pair_head, pair_count;
    size_t footprint;
};

static void rx_init(fthrob_rx *rx)
{
    /* rx_init, throb.cxx:54-64 */
    rx->rxcntr = rx->p.rxsymlen;
    rx->waitsync = 1;
    rx->deccntr = 0;
    rx->symptr = 0;
    rx->shift = 0;
    rx->lastchar = '\0';
    throb_reset_syms(&rx->p, &rx->idlesym, &rx->spacesym);
}

static void mk_rxtone(fdsp_cmpx *tone, double freq, const double *pulse, int len)
{
    /* mk_rxtone, throb.cxx:264-279: 32 times down sampled complex prototype tone */
    double x;
    int i;
    for (i = 0; i < len; i += THROB_DOWN_SAMPLE) {
        x = -2.0 * M_PI * freq * i / THROB_SAMPLE_RATE;
        tone[i / THROB_DOWN_SAMPLE].re = pulse[i] * cos(x);
        tone[i / THROB_DOWN_SAMPLE].im = pulse[i] * sin(x);
    }
}

static void rx_free(fthrob_rx *rx)
{
    int i;
    fdsp_fir_free(&rx->hilbert);
    fdsp_fftfilt_free(&rx->fftfilter);
    fdsp_fir_free(&rx->syncfilt);
    free(rx->syncpulse);
    for (i = 0; i < THROB_MAX_TONES; i++)
        free(rx->rxtone[i]);
    free(rx);
}

static void clear_state(fthrob_rx *rx)
{
    /* constructor state, throb.cxx:240-253, and init, throb.cxx:66-81 */
    int i;
    memset(rx->hilbert.ibuffer, 0, sizeof rx->hilbert.ibuffer);
    memset(rx->hilbert.qbuffer, 0, sizeof rx->hilbert.qbuffer);
    rx->hilbert.pointer = rx->hilbert.length;
    rx->hilbert.counter = 0;
    fdsp_fftfilt_create(&rx->fftfilter, 0.0, rx->p.bw);
    memset(rx->syncfilt.ibuffer, 0, sizeof rx->syncfilt.ibuffer);
    memset(rx->syncfilt.qbuffer, 0, sizeof rx->syncfilt.qbuffer);
    rx->syncfilt.pointer = rx->syncfilt.length;
    rx->syncfilt.counter = 0;
    fdsp_movavg_reset(&rx->snfilter);
    for (i = 0; i < THROB_MAX_RX_SYMLEN; i++) {
        rx->syncbuf[i] = rx->dispbuf[i] = 0.0;
        rx->symbol[i].re = rx->symbol[i].im = 0.0;
    }
    rx->phaseacc = 0.0;
    rx->metric = 0.0;
    rx->s2n = 0.0;
    rx->signal = rx->noise = 0.0;
    rx->frequency = rx->carrier;
    rx_init(rx);
}

fthrob_rx *fthrob_rx_create(const fthrob_rx_config *cfg)
{
    fthrob_rx *rx;
    double *txpulse;
    int i;
    if (!cfg)
        return NULL;
    rx = (fthrob_rx *)calloc(1, sizeof *rx);
    if (!rx)
        return NULL;
    if (throb_params_init(&rx->p, cfg->submode) != 0 || !throb_carrier_ok(&rx->p, cfg->carrier_hz)) {
        free(rx);
        return NULL;
    }
    rx->footprint = sizeof *rx;
    rx->carrier = cfg->carrier_hz;
    rx->afc = cfg->afc != 0;
    rx->squelch_on = cfg->squelch_on != 0;
    rx->squelch = cfg->squelch;
    rx->reverse = cfg->reverse != 0;
    /* constructor, throb.cxx:141-238 */
    txpulse = (double *)malloc((size_t)rx->p.symlen * sizeof(double));
    rx->syncpulse = (double *)malloc((size_t)rx->p.rxsymlen * sizeof(double));
    if (!txpulse || !rx->syncpulse) {
        free(txpulse);
        rx_free(rx);
        return NULL;
    }
    if (rx->p.full_pulse) {
        throb_full_pulse(txpulse, rx->p.symlen);
        throb_full_pulse(rx->syncpulse, rx->p.rxsymlen);
    } else {
        throb_semi_pulse(txpulse, rx->p.symlen);
        throb_semi_pulse(rx->syncpulse, rx->p.rxsymlen);
    }
    if (fdsp_fir_init_hilbert(&rx->hilbert, 37, 1) != 0 ||
        fdsp_fftfilt_init(&rx->fftfilter, 0.0, rx->p.bw, THROB_FILTER_FFT_LEN) != 0 ||
        fdsp_fir_init(&rx->syncfilt, rx->p.rxsymlen, 1, rx->syncpulse, NULL) != 0 ||
        fdsp_movavg_init(&rx->snfilter, 16) != 0) {
        free(txpulse);
        rx_free(rx);
        return NULL;
    }
    for (i = 0; i < rx->p.num_tones; i++) {
        rx->rxtone[i] = (fdsp_cmpx *)malloc((size_t)rx->p.rxsymlen * sizeof(fdsp_cmpx));
        if (!rx->rxtone[i]) {
            free(txpulse);
            rx_free(rx);
            return NULL;
        }
        mk_rxtone(rx->rxtone[i], rx->p.freqs[i], txpulse, rx->p.symlen);
    }
    free(txpulse);
    /* every allocation of create: sync taps, tone prototypes, the FIR taps (I and Q of the
     * Hilbert and the sync filter), fftfilt (4 x len + len/2 points, gfft tables) */
    rx->footprint += (size_t)rx->p.rxsymlen * sizeof(double) +
                     (size_t)rx->p.num_tones * rx->p.rxsymlen * sizeof(fdsp_cmpx) +
                     2 * 37 * sizeof(double) + 2 * (size_t)rx->p.rxsymlen * sizeof(double) +
                     (size_t)(4 * THROB_FILTER_FFT_LEN + THROB_FILTER_FFT_LEN / 2) *
                         sizeof(fdsp_cmpx) +
                     (size_t)(THROB_FILTER_FFT_LEN / 4 + 1) * sizeof(double) +
                     (size_t)(1 << (13 / 2 - 1)) * sizeof(short);
    clear_state(rx);
    return rx;
}

void fthrob_rx_destroy(fthrob_rx *rx)
{
    if (rx)
        rx_free(rx);
}

void fthrob_rx_reset(fthrob_rx *rx)
{
    if (!rx)
        return;
    clear_state(rx);
    rx->dropped = 0;
    rx->pair_head = rx->pair_count = 0;
}

static fdsp_cmpx cmul(fdsp_cmpx a, fdsp_cmpx b)
{
    /* std::complex<double> operator* (four products in separate statements, never fused) */
    fdsp_cmpx r;
    double ac = a.re * b.re, bd = a.im * b.im, ad = a.re * b.im, bc = a.im * b.re;
    r.re = ac - bd;
    r.im = ad + bc;
    return r;
}

static double cabs_(fdsp_cmpx z)
{
    /* std::abs(std::complex<double>) = hypot */
    return hypot(z.re, z.im);
}

static fdsp_cmpx mixer(fthrob_rx *rx, fdsp_cmpx in)
{
    /* mixer, throb.cxx:281-295 */
    fdsp_cmpx z;
    double f;
    fdsp_sincos(rx->phaseacc, &z.im, &z.re);
    z = cmul(z, in);
    f = rx->frequency;
    rx->phaseacc -= 2.0 * M_PI * f / THROB_SAMPLE_RATE;
    if (rx->phaseacc < 0)
        rx->phaseacc += 2.0 * M_PI;
    return z;
}

static int findtones(fthrob_rx *rx, const fdsp_cmpx *word, int *tone1, int *tone2)
{
    /* findtones, throb.cxx:297-350 (quirks Q2, Q3) */
    double max1, max2, s2n;
    int maxtone, i, n = rx->p.num_tones;

    max1 = 0;
    *tone1 = 0;
    for (i = 0; i < n; i++) {
        if (cabs_(word[i]) > max1) {
            max1 = cabs_(word[i]);
            *tone1 = i;
        }
    }
    maxtone = *tone1;
    max2 = 0;
    *tone2 = 0;
    for (i = 0; i < n; i++) {
        if (i == *tone1)
            continue;
        if (cabs_(word[i]) > max2) {
            max2 = cabs_(word[i]);
            *tone2 = i;
        }
    }
    /* single-tone symbols, Throb only (Q2) */
    if (!rx->p.throbx && max1 > max2 * 2)
        *tone2 = *tone1;
    if (*tone1 > *tone2) {
        i = *tone1;
        *tone1 = *tone2;
        *tone2 = i;
    }
    rx->signal = rx->noise = 0.0;
    for (i = 0; i < n; i++) {
        if (i == *tone1 || i == *tone2)
            rx->signal += cabs_(word[i]) / 2.0;
        else
            rx->noise += cabs_(word[i]) / (n - 2.0);
    }
    rx->metric = fdsp_movavg_run(&rx->snfilter, rx->signal / (rx->noise + 1e-6));
    s2n = 10.0 * log10(rx->metric) - 3.0;
    rx->s2n = s2n > 100.0 ? 100.0 : (s2n < 0.0 ? 0.0 : s2n);   /* CLAMP, throb.cxx:43 (Q3) */
    return maxtone;
}

static void put_char(fthrob_rx *rx, unsigned char c)
{
    /* put_rx_char: NUL (the Throb idle pair) is not text (Q6) */
    if (c == '\0')
        return;
    if (rx->n_text < rx->max_text)
        rx->text[rx->n_text++] = c;
    else
        rx->dropped++;
}

static void show_char(fthrob_rx *rx, unsigned char c)
{
    /* show_char, throb.cxx:352-355 (rx_lowercase off) */
    if (rx->metric > rx->squelch || !rx->squelch_on)
        put_char(rx, c);
}

static void decodechar(fthrob_rx *rx, int tone1, int tone2)
{
    /* decodechar, throb.cxx:357-417 */
    int i, t1, t2;
    if (rx->pair_count < PAIR_RING)
        rx->pair_count++;
    rx->pairs[rx->pair_head] = 16 * tone1 + tone2;
    rx->pair_head = (rx->pair_head + 1) % PAIR_RING;

    if (!rx->p.throbx) {
        if (rx->shift) {
            if (tone1 == 0 && tone2 == 8)
                show_char(rx, '?');
            if (tone1 == 1 && tone2 == 7)
                show_char(rx, '@');
            if (tone1 == 2 && tone2 == 6)
                show_char(rx, '=');
            if (tone1 == 4 && tone2 == 4)
                show_char(rx, '\n');
            rx->shift = 0;
            return;
        }
        if (tone1 == 3 && tone2 == 5) {
            rx->shift = 1;
            return;
        }
        for (i = 0; i < rx->p.num_chars; i++) {
            throb_pair(&rx->p, i, &t1, &t2);
            if (t1 == tone1 + 1 && t2 == tone2 + 1) {
                show_char(rx, throb_char(&rx->p, i));
                break;
            }
        }
        return;
    }
    /* ThrobX: no shift, idle and space alternate (Q10) */
    for (i = 0; i < rx->p.num_chars; i++) {
        throb_pair(&rx->p, i, &t1, &t2);
        if (t1 == tone1 + 1 && t2 == tone2 + 1) {
            if (i == rx->spacesym || i == rx->idlesym) {
                if (rx->lastchar != '\0' && rx->lastchar != ' ') {
                    show_char(rx, throb_char(&rx->p, 1));
                    rx->lastchar = ' ';
                } else {
                    rx->lastchar = '\0';
                }
                throb_flip_syms(&rx->p, &rx->idlesym, &rx->spacesym);
            } else {
                show_char(rx, throb_char(&rx->p, i));
                rx->lastchar = throb_char(&rx->p, i);
            }
        }
    }
}

static void rx_symbol(fthrob_rx *rx, fdsp_cmpx in)
{
    /* rx, throb.cxx:419-464 (AFC: Q4, reverse: Q5) */
    fdsp_cmpx rxword[THROB_MAX_TONES];
    int i, tone1, tone2, maxtone, n = rx->p.num_tones;

    rx->symbol[rx->symptr] = in;
    if (rx->rxcntr > 0.0)
        return;
    /* correlate against all tones */
    for (i = 0; i < n; i++)
        rxword[i] = fdsp_cmac(rx->rxtone[i], rx->symbol, rx->symptr + 1, rx->p.rxsymlen);
    maxtone = findtones(rx, rxword, &tone1, &tone2);
    if (rx->reverse)
        decodechar(rx, n - 1 - tone2, n - 1 - tone1);
    else
        decodechar(rx, tone1, tone2);

    if (rx->afc && (rx->metric >= rx->squelch || !rx->squelch_on)) {
        fdsp_cmpx z1, z2, c;
        double f;
        z1 = rxword[maxtone];
        z2 = fdsp_cmac(rx->rxtone[maxtone], rx->symbol, rx->symptr + 2, rx->p.rxsymlen);
        z1.im = -z1.im;   /* conj */
        c = cmul(z1, z2);
        f = atan2(c.im, c.re) / (2 * THROB_DOWN_SAMPLE * M_PI / THROB_SAMPLE_RATE);
        f -= rx->p.freqs[maxtone];
        rx->frequency = throb_clamp_freq(&rx->p, rx->frequency + f / (n - 1));
    }
    /* done with this symbol, start over */
    rx->rxcntr = rx->p.rxsymlen;
    rx->waitsync = 1;
}

static void sync_symbol(fthrob_rx *rx, fdsp_cmpx in)
{
    /* sync, throb.cxx:466-503 (Q1: integer division of the correction) */
    double f = 0.0, maxval = 0, mag;
    int i, maxpos = 0, len = rx->p.rxsymlen;

    /* "rectify", filter and store input */
    mag = cabs_(in);
    fdsp_fir_irun(&rx->syncfilt, mag, &f);
    rx->syncbuf[rx->symptr] = f;
    /* check counter if we are waiting for sync */
    if (rx->waitsync == 0 || rx->rxcntr > (len / 2.0))
        return;
    for (i = 0; i < len; i++)
        rx->dispbuf[i] = rx->syncbuf[(i + rx->symptr + 1) % len];
    for (i = 0; i < len; i++) {
        if (rx->dispbuf[i] > maxval) {
            maxpos = i;
            maxval = rx->dispbuf[i];
        }
    }
    /* correct sync */
    rx->rxcntr += (maxpos - len / 2) / (rx->p.num_tones - 1);
    rx->waitsync = 0;
}

size_t fthrob_rx_process(fthrob_rx *rx, const double *x, size_t n, unsigned char *text,
                         size_t max_text)
{
    /* rx_process, throb.cxx:505-536 */
    const fdsp_cmpx *zp;
    fdsp_cmpx z;
    size_t k;
    int i, m;
    if (!rx || !x)
        return 0;
    rx->text = text;
    rx->max_text = text ? max_text : 0;
    rx->n_text = 0;
    for (k = 0; k < n; k++) {
        z.re = z.im = x[k];
        fdsp_fir_run(&rx->hilbert, z, &z);
        z = mixer(rx, z);
        m = fdsp_fftfilt_run(&rx->fftfilter, z, &zp);
        /* DOWN_SAMPLE by 32 and push to the receiver */
        for (i = 0; i < m; i++) {
            if (++rx->deccntr >= THROB_DOWN_SAMPLE) {
                rx->rxcntr -= 1.0;
                sync_symbol(rx, zp[i]);
                rx_symbol(rx, zp[i]);
                rx->symptr = (rx->symptr + 1) % rx->p.rxsymlen;
                rx->deccntr = 0;
            }
        }
    }
    rx->text = NULL;
    return rx->n_text;
}

double fthrob_rx_snr_db(const fthrob_rx *rx)
{
    return rx ? rx->s2n : 0.0;
}

double fthrob_rx_metric(const fthrob_rx *rx)
{
    return rx ? rx->metric : 0.0;
}

double fthrob_rx_offset_hz(const fthrob_rx *rx)
{
    return rx ? rx->frequency - rx->carrier : 0.0;
}

double fthrob_rx_carrier_hz(const fthrob_rx *rx)
{
    return rx ? rx->frequency : 0.0;
}

void fthrob_rx_set_carrier(fthrob_rx *rx, double carrier_hz)
{
    /* set_freq, modem.cxx:319-324 */
    if (!rx || !isfinite(carrier_hz))
        return;
    rx->carrier = rx->frequency = throb_clamp_freq(&rx->p, carrier_hz);
}

void fthrob_rx_set_afc(fthrob_rx *rx, int on)
{
    if (rx)
        rx->afc = on != 0;
}

void fthrob_rx_set_squelch(fthrob_rx *rx, int on, double level)
{
    if (!rx)
        return;
    rx->squelch_on = on != 0;
    rx->squelch = level;
}

void fthrob_rx_set_reverse(fthrob_rx *rx, int on)
{
    if (rx)
        rx->reverse = on != 0;
}

size_t fthrob_rx_dropped(const fthrob_rx *rx)
{
    return rx ? rx->dropped : 0;
}

size_t fthrob_rx_footprint(const fthrob_rx *rx)
{
    return rx ? rx->footprint : 0;
}

size_t fthrob_rx_take_pairs(fthrob_rx *rx, int *out, size_t max)
{
    size_t n, i, start;
    if (!rx || !out)
        return 0;
    n = rx->pair_count < max ? rx->pair_count : max;
    start = (rx->pair_head + PAIR_RING - rx->pair_count) % PAIR_RING;
    /* the oldest n of the stored pairs when max is smaller: drop the rest */
    for (i = 0; i < n; i++)
        out[i] = rx->pairs[(start + i) % PAIR_RING];
    rx->pair_count = 0;
    return n;
}
