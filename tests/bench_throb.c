/*
 * fldigi-throb-core CPU and memory benchmark: receiver and transmitter speed per submode in
 * multiples of real time on one core at 8000 Hz, the CPU share of one receiver, and the
 * receiver's allocation per instance (wave-owl R-861.6: < 2 % of one core, < 1 MB).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include "test_util.h"

#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(void)
{
    int i, fail = 0;
    printf("%-7s %10s %8s %10s %8s\n", "submode", "RX x real", "RX CPU", "TX x real",
           "RX KiB");
    for (i = 0; i < fthrob_submode_count(); i++) {
        const fthrob_submode *m = fthrob_submode_at(i);
        size_t n, k, len = (size_t)FTHROB_RATE * 60, np;
        double *x, *y, t0, t_tx, t_rx, rx_speed, share;
        fthrob_rx *rx;
        unsigned char *out;
        t0 = now();
        x = render_over(m->name, "CQ CQ DE DG6FL DG6FL K", 0, 0, 1500.0, &n);
        t_tx = now() - t0;
        y = (double *)calloc(len, sizeof(double));
        for (k = 0; k < len; k++)
            y[k] = 0.25 * x[k % n];
        rx = make_rx(m->name, 1500.0, 1);
        t0 = now();
        out = decode(rx, y, len, 512, &np);
        t_rx = now() - t0;
        rx_speed = (double)len / FTHROB_RATE / t_rx;
        share = 100.0 / rx_speed;
        printf("%-7s %10.0f %7.2f%% %10.0f %8.0f\n", m->name, rx_speed, share,
               (double)n / FTHROB_RATE / t_tx, fthrob_rx_footprint(rx) / 1024.0);
        fail |= share >= 2.0 || fthrob_rx_footprint(rx) >= 1024 * 1024;
        free(out);
        free(x);
        free(y);
        fthrob_rx_destroy(rx);
    }
    if (fail)
        fprintf(stderr, "budget exceeded (R-861.6: < 2 %% CPU, < 1 MB per receiver)\n");
    return fail;
}
