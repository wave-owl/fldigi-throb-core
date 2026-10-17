/*
 * fldigi-throb-core transmitter: throb::tx_init, send and tx_process (fldigi 4.2.13
 * src/throb/throb.cxx:47-52, 582-721).
 *
 * Copyright (C) 2026 Gerolf Ziegenhain; ported from fldigi 4.2.13 src/throb/throb.cxx
 * (c) 2006-2010 Dave Freese W1HKJ, ThrobX additions by Joe Veldhuis KD8ATU, adapted from
 * gmfsk (c) 2001-2003 Tomi Manninen OH2BNS.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "throb_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PREAMBLE 4

struct fthrob_tx {
    throb_params p;
    double *txpulse;
    double frequency;
    int reverse;
    int idlesym, spacesym;
};

fthrob_tx *fthrob_tx_create(const fthrob_tx_config *cfg)
{
    /* constructor, throb.cxx:141-222 (pulse of the submode); tx_init, throb.cxx:47-52 */
    fthrob_tx *tx;
    if (!cfg)
        return NULL;
    tx = (fthrob_tx *)calloc(1, sizeof *tx);
    if (!tx)
        return NULL;
    if (throb_params_init(&tx->p, cfg->submode) != 0 || !throb_carrier_ok(&tx->p, cfg->carrier_hz)) {
        free(tx);
        return NULL;
    }
    tx->txpulse = (double *)malloc((size_t)tx->p.symlen * sizeof(double));
    if (!tx->txpulse) {
        free(tx);
        return NULL;
    }
    if (tx->p.full_pulse)
        throb_full_pulse(tx->txpulse, tx->p.symlen);
    else
        throb_semi_pulse(tx->txpulse, tx->p.symlen);
    tx->frequency = cfg->carrier_hz;
    tx->reverse = cfg->reverse != 0;
    throb_reset_syms(&tx->p, &tx->idlesym, &tx->spacesym);
    return tx;
}

void fthrob_tx_destroy(fthrob_tx *tx)
{
    if (!tx)
        return;
    free(tx->txpulse);
    free(tx);
}

size_t fthrob_tx_symlen(const fthrob_tx *tx)
{
    return tx ? (size_t)tx->p.symlen : 0;
}

size_t fthrob_tx_bound(const fthrob_tx *tx)
{
    return tx ? (size_t)PREAMBLE * (size_t)tx->p.symlen : 0;
}

static size_t tx_send(fthrob_tx *tx, int symbol, double *out)
{
    /* send, throb.cxx:582-617 (reverse: Q5; get_txfreq_woffset with TxOffset 0,
     * modem.cxx:353-359; no soft start, Q13) */
    int tone1, tone2, i, n = tx->p.num_tones;
    double w1, w2;
    if (symbol < 0 || symbol >= tx->p.num_chars)
        return 0;
    throb_pair(&tx->p, symbol, &tone1, &tone2);
    tone1 -= 1;
    tone2 -= 1;
    if (tx->reverse) {
        tone1 = (n - 1) - tone1;
        tone2 = (n - 1) - tone2;
    }
    w1 = 2.0 * M_PI * (tx->frequency + tx->p.freqs[tone1]) / THROB_SAMPLE_RATE;
    w2 = 2.0 * M_PI * (tx->frequency + tx->p.freqs[tone2]) / THROB_SAMPLE_RATE;
    for (i = 0; i < tx->p.symlen; i++)
        out[i] = tx->txpulse[i] * (sin(w1 * i) + sin(w2 * i)) / 2.0;
    return (size_t)tx->p.symlen;
}

/* The symbols of one keyboard character, tx_process throb.cxx:648-717 (Q7-Q9, Q11). Returns
 * their number (0..2) and the symbol indices; flips the ThrobX idle/space state when
 * `commit` is set, exactly when fldigi does. */
static int char_symbols(fthrob_tx *tx, unsigned char ch, int sym[2], int commit)
{
    int c = ch, i, s;
    if (!tx->p.throbx) {
        /* the special cases first, if we're doing regular Throb */
        switch (c) {
        case '?':
            sym[0] = 5;     /* shift */
            sym[1] = 20;
            return 2;
        case '@':
            sym[0] = 5;
            sym[1] = 13;
            return 2;
        case '-':
            sym[0] = 5;
            sym[1] = 9;
            return 2;
        case '\r':
            return 0;
        case '\n':
            sym[0] = 5;
            sym[1] = 0;
            return 2;
        default:
            break;
        }
    }
    /* map lower case character to upper case */
    if (islower(c))
        c = toupper(c);
    /* see if the character can be found in our character set (the last match wins) */
    for (s = -1, i = 0; i < tx->p.num_chars; i++)
        if (c == throb_char(&tx->p, i))
            s = i;
    /* send a space for unknown chars */
    if (s == -1)
        c = ' ';
    /* handle spaces for throbx */
    if (c == ' ') {
        s = tx->spacesym;
        if (commit)
            throb_flip_syms(&tx->p, &tx->idlesym, &tx->spacesym);
    }
    sym[0] = s;
    return 1;
}

int fthrob_tx_symbols(const fthrob_tx *tx, unsigned char c)
{
    int sym[2];
    fthrob_tx copy;
    if (!tx)
        return 0;
    copy = *tx;
    return char_symbols(&copy, c, sym, 0);
}

size_t fthrob_tx_begin(fthrob_tx *tx, double *out, size_t max)
{
    /* tx_init (preamble = 4, reset_syms) and the preamble of tx_process, throb.cxx:47-52,
     * 625-630: idle symbols, ThrobX flips after each (Q12) */
    size_t k = 0;
    int i;
    if (!tx || !out || max < fthrob_tx_bound(tx))
        return 0;
    throb_reset_syms(&tx->p, &tx->idlesym, &tx->spacesym);
    for (i = 0; i < PREAMBLE; i++) {
        k += tx_send(tx, tx->idlesym, out + k);
        throb_flip_syms(&tx->p, &tx->idlesym, &tx->spacesym);
    }
    return k;
}

size_t fthrob_tx_put(fthrob_tx *tx, unsigned char c, double *out, size_t max)
{
    /* tx_process for one character, throb.cxx:648-720 */
    int sym[2], n, i;
    size_t k = 0;
    if (!tx || !out || max < 2 * (size_t)tx->p.symlen)
        return 0;
    n = char_symbols(tx, c, sym, 1);
    for (i = 0; i < n; i++)
        k += tx_send(tx, sym[i], out + k);
    return k;
}

size_t fthrob_tx_idle(fthrob_tx *tx, double *out, size_t max)
{
    /* TX buffer empty, throb.cxx:641-646 */
    size_t k;
    if (!tx || !out || max < (size_t)tx->p.symlen)
        return 0;
    k = tx_send(tx, tx->idlesym, out);
    throb_flip_syms(&tx->p, &tx->idlesym, &tx->spacesym);
    return k;
}

size_t fthrob_tx_end(fthrob_tx *tx, double *out, size_t max)
{
    /* end of transmission, throb.cxx:634-639: one idle symbol, no flip */
    if (!tx || !out || max < (size_t)tx->p.symlen)
        return 0;
    return tx_send(tx, tx->idlesym, out);
}
