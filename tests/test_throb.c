/*
 * fldigi-throb-core unit tests: API, submodes, loopback in both character sets, chunk
 * invariance, reverse, squelch, and one case per fldigi behaviour Q1-Q13 of the README.
 * Bit-exactness against fldigi itself is tests/test_throb_fldigi.c.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include "test_util.h"
#include "fldigi_dsp.h"

static const char *ALL[6] = {"THROB1", "THROB2", "THROB4", "THRBX1", "THRBX2", "THRBX4"};

/* 1 s of silence, the over, 3 s of silence (the fftfilt delay is 0.5 s). */
static double *padded(const double *x, size_t n, size_t *out_n)
{
    size_t total = 8000 + n + 24000;
    double *y = (double *)calloc(total, sizeof(double));
    memcpy(y + 8000, x, n * sizeof(double));
    *out_n = total;
    return y;
}

static unsigned char *loop(const char *sub, const char *text, int tx_rev, int rx_rev,
                           size_t chunk, size_t *got)
{
    size_t n, np;
    double *x = render_over_rev(sub, text, 0, 0, 1500.0, tx_rev, &n), *y = padded(x, n, &np);
    fthrob_rx *rx = make_rx_rev(sub, 1500.0, 0, rx_rev);
    unsigned char *out = decode(rx, y, np, chunk, got);
    fthrob_rx_destroy(rx);
    free(x);
    free(y);
    return out;
}

static void test_api(void)
{
    int i, abi = fthrob_abi();
    const char *v = fthrob_version();
    CHECK(strstr(v, "contract=throb-c1") && strstr(v, "abi=1") && strstr(v, "tx=1") &&
          strstr(v, "dsp=" FDSP_VERSION_STRING), "version %s", v);
    CHECK(abi == 1, "abi %d", abi);
    CHECK(fthrob_submode_count() == 6, "count");
    CHECK(fthrob_submode_at(-1) == NULL && fthrob_submode_at(6) == NULL, "at bounds");
    CHECK(fthrob_submode_find("thrbx2") == fthrob_submode_at(4), "find case-insensitive");
    CHECK(fthrob_submode_find("THROB8") == NULL && fthrob_submode_find(NULL) == NULL, "find");
    for (i = 0; i < 6; i++) {
        const fthrob_submode *m = fthrob_submode_at(i);
        fthrob_rx_config rc;
        fthrob_tx_config tc;
        fthrob_rx *rx;
        fthrob_tx *tx;
        CHECK(strcmp(m->name, ALL[i]) == 0 && m->fldigi_id == 132 + i, "submode %d", i);
        CHECK(fabs(m->bandwidth - (m->num_tones - 1) * m->tone_spacing) < 1e-9, "bw %d", i);
        /* AC-860.3: receiver and transmitter of every submode */
        memset(&rc, 0, sizeof rc);
        rc.submode = m->name;
        rc.carrier_hz = 1500.0;
        rx = fthrob_rx_create(&rc);
        memset(&tc, 0, sizeof tc);
        tc.submode = m->name;
        tc.carrier_hz = 1500.0;
        tx = fthrob_tx_create(&tc);
        CHECK(rx && tx, "create %s", m->name);
        /* R-861.6: under 1 MB per receiver */
        CHECK(fthrob_rx_footprint(rx) > 0 && fthrob_rx_footprint(rx) < 1024 * 1024,
              "%s footprint %zu", m->name, fthrob_rx_footprint(rx));
        CHECK(fthrob_tx_symlen(tx) == (size_t)m->symlen &&
              fthrob_tx_bound(tx) == 4 * (size_t)m->symlen, "%s symlen", m->name);
        CHECK(fthrob_rx_process(rx, NULL, 10, NULL, 0) == 0, "NULL input");
        fthrob_rx_destroy(rx);
        fthrob_tx_destroy(tx);
        /* carriers modem::set_freq would move are refused */
        rc.carrier_hz = m->bandwidth / 2 - 0.5;
        CHECK(fthrob_rx_create(&rc) == NULL, "%s low carrier", m->name);
        rc.carrier_hz = 3000.0 - m->bandwidth / 2 + 0.5;
        CHECK(fthrob_rx_create(&rc) == NULL, "%s high carrier", m->name);
        tc.carrier_hz = 3000.0;
        CHECK(fthrob_tx_create(&tc) == NULL, "%s tx carrier", m->name);
    }
    {
        fthrob_rx_config rc;
        memset(&rc, 0, sizeof rc);
        rc.submode = "MFSK16";
        rc.carrier_hz = 1500.0;
        CHECK(fthrob_rx_create(&rc) == NULL && fthrob_rx_create(NULL) == NULL, "bad config");
        fthrob_rx_destroy(NULL);
        fthrob_tx_destroy(NULL);
    }
}

/* AC-860.1: TX -> RX in every submode, with the documented asymmetries. */
static void test_loopback(void)
{
    static const char *throb_in = "cq cq de dg6fl, 599 (73) 'ok'/. ?@-x\nnew\rline";
    static const char *throb_out = "CQ CQ DE DG6FL, 599 (73) 'OK'/. ?@=X\nNEWLINE";
    static const char *x_in = "cq de dg6fl #\"+-;:?!@= 0123456789\nnext\rword";
    static const char *x_out = "CQ DE DG6FL #\"+-;:?!@= 0123456789\nNEXT WORD";
    int i;
    for (i = 0; i < 6; i++) {
        const fthrob_submode *m = fthrob_submode_at(i);
        size_t got;
        unsigned char *out = loop(m->name, m->throbx ? x_in : throb_in, 0, 0, 512, &got);
        CHECK(contains(out, got, m->throbx ? x_out : throb_out), "%s loopback: [%s]", m->name,
              out);
        CHECK(memchr(out, 0, got) == NULL, "%s: NUL in the output (Q6)", m->name);
        free(out);
    }
}

static void test_chunking(void)
{
    int i;
    for (i = 0; i < 6; i += 2) {
        size_t a, b, c;
        unsigned char *x = loop(ALL[i], "CQ CQ DE DG6FL K", 0, 0, 1, &a);
        unsigned char *y = loop(ALL[i], "CQ CQ DE DG6FL K", 0, 0, 512, &b);
        unsigned char *z = loop(ALL[i], "CQ CQ DE DG6FL K", 0, 0, 10007, &c);
        CHECK(a == b && b == c && memcmp(x, y, a) == 0 && memcmp(y, z, b) == 0,
              "%s: chunking changes the output", ALL[i]);
        free(x);
        free(y);
        free(z);
    }
}

/* Q5: reverse mirrors the tone indices on both sides. */
static void test_reverse(void)
{
    int i;
    for (i = 0; i < 6; i++) {
        size_t a, b;
        unsigned char *ok = loop(ALL[i], "CQ DE DG6FL", 1, 1, 512, &a);
        unsigned char *bad = loop(ALL[i], "CQ DE DG6FL", 1, 0, 512, &b);
        CHECK(contains(ok, a, "CQ DE DG6FL"), "%s reverse loopback [%s]", ALL[i], ok);
        CHECK(!contains(bad, b, "CQ DE DG6FL"), "%s reverse TX, normal RX decodes", ALL[i]);
        free(ok);
        free(bad);
    }
}

/* Q1-Q4 run bit for bit against fldigi in test_throb_fldigi.c; here the observable parts. */
static void test_rx_quirks(void)
{
    size_t n, np, got, k;
    int pairs[1024];
    double *x = render_over("THROB2", "ZZZ", 0, 0, 1500.0, &n), *y = padded(x, n, &np);
    fthrob_rx *rx = make_rx("THROB2", 1500.0, 0);
    unsigned char *out = decode(rx, y, np, 512, &got);
    /* Q2: Z is the single tone {1,1}; fldigi counts one tone when max1 > 2 max2 */
    CHECK(contains(out, got, "ZZZ"), "Q2 single tone [%s]", out);
    k = fthrob_rx_take_pairs(rx, pairs, 1024);
    CHECK(k > 3, "pairs tap %zu", k);
    {
        size_t j, zeros = 0, idle = 0;
        for (j = 0; j < k; j++) {
            zeros += pairs[j] == 0;          /* Z = tones (0, 0) */
            idle += pairs[j] == 16 * 4 + 4;  /* idle pair {5,5} = tones (4, 4) */
        }
        CHECK(zeros >= 3 && idle >= 4, "pairs: %zu Z, %zu idle", zeros, idle);
    }
    CHECK(fthrob_rx_take_pairs(rx, pairs, 1024) == 0, "pairs taken");
    /* Q3: s/n clamped to 0 .. 100 dB */
    CHECK(fthrob_rx_snr_db(rx) >= 0.0 && fthrob_rx_snr_db(rx) <= 100.0, "Q3 s/n %g",
          fthrob_rx_snr_db(rx));
    /* reset gives the same decode again */
    {
        size_t got2;
        unsigned char *again;
        fthrob_rx_reset(rx);
        again = decode(rx, y, np, 512, &got2);
        CHECK(got2 == got && memcmp(again, out, got) == 0, "reset");
        free(again);
    }
    /* dropped characters are counted */
    {
        unsigned char one[1];
        size_t total = 0, i;
        fthrob_rx_reset(rx);
        for (i = 0; i < np; i += 512)
            total += fthrob_rx_process(rx, y + i, np - i < 512 ? np - i : 512, one, 0);
        CHECK(total == 0 && fthrob_rx_dropped(rx) == got, "dropped %zu of %zu",
              fthrob_rx_dropped(rx), got);
    }
    /* squelch on with a level no signal reaches: nothing printed */
    {
        size_t got3;
        unsigned char *none;
        fthrob_rx_reset(rx);
        fthrob_rx_set_squelch(rx, 1, 1e9);
        none = decode(rx, y, np, 512, &got3);
        CHECK(got3 == 0, "squelch prints %zu", got3);
        free(none);
    }
    fthrob_rx_destroy(rx);
    free(out);
    free(x);
    free(y);

    /* Q4: the AFC follows a carrier 4 Hz off and never leaves fldigi's clamp */
    {
        double *s = render_over("THROB2", "CQ CQ CQ DE DG6FL DG6FL", 0, 0, 1504.0, &n);
        double *t = padded(s, n, &np);
        fthrob_rx *a = make_rx("THROB2", 1500.0, 1);
        unsigned char *o = decode(a, t, np, 512, &got);
        double off = fthrob_rx_offset_hz(a);
        CHECK(off != 0.0 && fabs(fthrob_rx_carrier_hz(a) - (1500.0 + off)) < 1e-9,
              "Q4 AFC offset %g", off);
        fthrob_rx_set_carrier(a, 10.0);
        CHECK(fthrob_rx_carrier_hz(a) == 32.0, "Q4 clamp low %g", fthrob_rx_carrier_hz(a));
        fthrob_rx_set_carrier(a, 2990.0);
        CHECK(fthrob_rx_carrier_hz(a) == 2968.0, "Q4 clamp high %g", fthrob_rx_carrier_hz(a));
        free(o);
        fthrob_rx_destroy(a);
        free(s);
        free(t);
    }
}

/* Q10: ThrobX prints one space for idle/space only after a non-space. */
static void test_throbx_spaces(void)
{
    size_t got;
    unsigned char *out = loop("THRBX2", "AB   CD", 0, 0, 512, &got);
    CHECK(contains(out, got, "AB CD") && !contains(out, got, "AB  "), "Q10 [%s]", out);
    free(out);
}

static int symbols_of(const char *sub, unsigned char c)
{
    fthrob_tx_config tc;
    fthrob_tx *tx;
    int n;
    memset(&tc, 0, sizeof tc);
    tc.submode = sub;
    tc.carrier_hz = 1500.0;
    tx = fthrob_tx_create(&tc);
    n = fthrob_tx_symbols(tx, c);
    fthrob_tx_destroy(tx);
    return n;
}

static void test_tx_quirks(void)
{
    fthrob_tx_config tc;
    fthrob_tx *tx;
    double *buf;
    size_t bound, k, s;
    int i;
    /* Q7, Q8: Throb shift characters, CR ignored; Q9: ThrobX CR is a space, LF one symbol */
    CHECK(symbols_of("THROB1", '?') == 2 && symbols_of("THROB1", '@') == 2 &&
          symbols_of("THROB1", '-') == 2 && symbols_of("THROB1", '\n') == 2 &&
          symbols_of("THROB1", '\r') == 0 && symbols_of("THROB1", 'a') == 1,
          "Q7/Q8 symbol counts");
    CHECK(symbols_of("THRBX1", '?') == 1 && symbols_of("THRBX1", '\r') == 1 &&
          symbols_of("THRBX1", '\n') == 1, "Q9 symbol counts");
    /* Q11: unknown characters are sent as space; lower case as upper case */
    for (i = 0; i < 6; i++) {
        size_t a, b;
        unsigned char *x = loop(ALL[i], "A~b", 0, 0, 512, &a);
        unsigned char *y = loop(ALL[i], "a B", 0, 0, 512, &b);
        CHECK(a == b && memcmp(x, y, a) == 0, "Q11 %s", ALL[i]);
        free(x);
        free(y);
    }
    /* Q12: 4 preamble symbols, one final idle symbol; ThrobX alternates idle and space in
     * the preamble; Q13: every symbol starts at phase 0 (sample 0 is exactly 0) */
    for (i = 0; i < 6; i++) {
        const fthrob_submode *m = fthrob_submode_at(i);
        memset(&tc, 0, sizeof tc);
        tc.submode = m->name;
        tc.carrier_hz = 1500.0;
        tx = fthrob_tx_create(&tc);
        bound = fthrob_tx_bound(tx);
        s = fthrob_tx_symlen(tx);
        buf = (double *)malloc(bound * sizeof(double));
        CHECK(fthrob_tx_begin(tx, buf, bound - 1) == 0, "begin needs the bound");
        k = fthrob_tx_begin(tx, buf, bound);
        CHECK(k == 4 * s, "%s preamble %zu", m->name, k);
        CHECK(buf[0] == 0.0 && buf[s] == 0.0 && buf[3 * s] == 0.0, "%s Q13", m->name);
        if (m->throbx)
            CHECK(memcmp(buf, buf + s, s * sizeof(double)) != 0 &&
                  memcmp(buf, buf + 2 * s, s * sizeof(double)) == 0, "%s Q12 alternation",
                  m->name);
        else
            CHECK(memcmp(buf, buf + s, s * sizeof(double)) == 0, "%s Q12 idle", m->name);
        /* peak 1.0 at most (two tones of amplitude 1/2 each) */
        for (k = 0; k < 4 * s; k++)
            if (fabs(buf[k]) > 1.0)
                break;
        CHECK(k == 4 * s, "%s amplitude", m->name);
        k = fthrob_tx_put(tx, 'E', buf, bound);
        CHECK(k == s, "%s one symbol", m->name);
        CHECK(fthrob_tx_put(tx, 'E', buf, s) == 0, "put needs room for 2 symbols");
        k = fthrob_tx_end(tx, buf, bound);
        CHECK(k == s, "%s end", m->name);
        free(buf);
        fthrob_tx_destroy(tx);
    }
}

int main(void)
{
    test_api();
    test_loopback();
    test_chunking();
    test_reverse();
    test_rx_quirks();
    test_throbx_spaces();
    test_tx_quirks();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("fldigi-throb-core: all unit tests passed (%s)\n", fthrob_version());
    return 0;
}
