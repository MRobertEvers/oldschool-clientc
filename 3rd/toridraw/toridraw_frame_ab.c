#include "toridraw_frame_ab.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/*
 * WARMUP is in frames and is deliberately generous. The first frames of a run
 * pay scene load-in, cold caches and first-touch page faults, and they are
 * charged to whichever arm the ABBA cycle hands them -- which is a bias with a
 * sign and no meaning. Nothing before this point is counted.
 *
 * It is a multiple of four so that discarding it does not leave the cycle
 * half-consumed and hand one arm an extra frame.
 */
#define WARMUP_FRAMES 120

struct FrameAb
{
    int enabled;
    int swapped;
    int inited;
    uint32_t frame;    /* frames seen, including warmup */
    double t0;
    double total[2];
    uint32_t count[2];
    double sumsq[2];   /* for the spread, so the delta can be judged */

    /*
     * The PAIRED estimator, which is the one that can actually decide this.
     *
     * The unpaired difference of two grand means is swamped by content: the
     * camera is pinned but the scene animates, and the per-frame spread came
     * out at 8 ms on a 23 ms frame -- so a real 0.7 ms effect sits inside a
     * 2-sigma bound of 0.87 ms and reads as neutral. That spread is almost
     * entirely frames drawing different amounts of geometry, not the machine
     * being noisy, and it is shared by both arms.
     *
     * Differencing INSIDE one ABBA group removes it. Adjacent frames draw
     * nearly the same scene, so
     *
     *     d = (t1 + t2)/2 - (t0 + t3)/2
     *
     * subtracts almost all of the content variance while still measuring
     * B - A. It also cancels linear drift exactly: frames 1 and 2 have the
     * same mean index as frames 0 and 3, so a machine getting steadily slower
     * contributes nothing.
     */
    double cyc[4];
    double dsum;
    double dsumsq;
    uint32_t dcount;
};

static struct FrameAb g_ab;

static double
now_seconds(void)
{
#if defined(_WIN32)
    LARGE_INTEGER f;
    LARGE_INTEGER c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static void
ab_init(void)
{
    const char* v;

    if( g_ab.inited )
        return;
    g_ab.inited = 1;
    v = getenv("TORIDRAW_FRAME_AB");
    g_ab.enabled = (v && atoi(v) != 0);
    /*
     * TORIDRAW_FRAME_AB_SWAP=1 puts the change in the A slot instead of B.
     *
     * This is the control, and it is the only thing that can distinguish a
     * real effect from a bias belonging to the harness -- a slot that is
     * systematically cheaper because of where it falls in the cycle, in the
     * cache, or against whatever the machine does periodically. A genuine
     * result flips sign under the swap and keeps its magnitude. A harness
     * artefact keeps its sign.
     */
    v = getenv("TORIDRAW_FRAME_AB_SWAP");
    g_ab.swapped = (v && atoi(v) != 0);
}

int
ToriDraw_FrameAbEnabled(void)
{
    ab_init();
    return g_ab.enabled;
}

int
ToriDraw_FrameAbArm(void)
{
    ab_init();
    if( !g_ab.enabled )
        return 0;
    /*
     * ABBA from the frame counter: bit0 XOR bit1 gives 0,1,1,0 across each
     * group of four. See the header for why the cycle is this and not ABAB.
     */
    {
        int arm = (int)(((g_ab.frame >> 1) ^ g_ab.frame) & 1u);
        return g_ab.swapped ? !arm : arm;
    }
}

void
ToriDraw_FrameAbBegin(void)
{
    ab_init();
    if( !g_ab.enabled )
        return;
    g_ab.t0 = now_seconds();
}

void
ToriDraw_FrameAbEnd(void)
{
    double dt;
    int arm;

    ab_init();
    if( !g_ab.enabled )
        return;

    dt = now_seconds() - g_ab.t0;
    arm = ToriDraw_FrameAbArm();

    if( g_ab.frame >= WARMUP_FRAMES )
    {
        g_ab.total[arm] += dt;
        g_ab.sumsq[arm] += dt * dt;
        g_ab.count[arm]++;

        /*
         * WARMUP_FRAMES is a multiple of four, so the first counted frame is
         * also the start of a cycle and no group is ever half-collected.
         */
        g_ab.cyc[g_ab.frame & 3u] = dt;
        if( (g_ab.frame & 3u) == 3u )
        {
            double d = (g_ab.cyc[1] + g_ab.cyc[2]) * 0.5
                       - (g_ab.cyc[0] + g_ab.cyc[3]) * 0.5;
            g_ab.dsum += d;
            g_ab.dsumsq += d * d;
            g_ab.dcount++;
        }
    }
    g_ab.frame++;
}

void
ToriDraw_FrameAbDump(const char* label)
{
    double mean[2];
    double sd[2];
    double sem;
    double delta;
    int i;

    assert(label);
    ab_init();
    if( !g_ab.enabled )
        return;

    if( g_ab.count[0] == 0 || g_ab.count[1] == 0 )
    {
        fprintf(stderr,
                "frame-ab %s: not enough frames past the %d-frame warmup "
                "(A=%u B=%u)\n",
                label, WARMUP_FRAMES, g_ab.count[0], g_ab.count[1]);
        return;
    }

    for( i = 0; i < 2; i++ )
    {
        mean[i] = g_ab.total[i] / (double)g_ab.count[i];
        /* Population sd of the per-frame times; with thousands of frames the
         * n-vs-n-1 distinction is far below what is being decided here. */
        sd[i] = g_ab.sumsq[i] / (double)g_ab.count[i] - mean[i] * mean[i];
        sd[i] = (sd[i] > 0.0) ? sqrt(sd[i]) : 0.0;
    }

    delta = mean[1] - mean[0];
    /* Standard error of the difference of two means. */
    sem = sd[0] * sd[0] / (double)g_ab.count[0]
          + sd[1] * sd[1] / (double)g_ab.count[1];
    sem = (sem > 0.0) ? sqrt(sem) : 0.0;

    fprintf(stderr,
            "frame-ab %s%s: A %.3f ms (n=%u, sd %.3f)  B %.3f ms (n=%u, sd %.3f)\n"
            "  unpaired delta B-A %+.4f ms/frame (%+.2f%%), 2-sigma +-%.4f ms"
            "  --> %s\n",
            label, g_ab.swapped ? " [SWAPPED: change is in A]" : "",
            mean[0] * 1e3, g_ab.count[0], sd[0] * 1e3,
            mean[1] * 1e3, g_ab.count[1], sd[1] * 1e3,
            delta * 1e3, 100.0 * delta / mean[0], 2.0 * sem * 1e3,
            (delta < -2.0 * sem) ? "B IS FASTER"
                                 : (delta > 2.0 * sem ? "B IS SLOWER"
                                                      : "NEUTRAL"));

    /*
     * The paired estimator. This is the verdict; the unpaired line above is
     * printed next to it so a disagreement between the two is visible rather
     * than hidden -- they estimate the same quantity, and if they disagree in
     * SIGN then the pairing assumption has broken and neither should be
     * believed.
     */
    if( g_ab.dcount >= 2u )
    {
        double dmean = g_ab.dsum / (double)g_ab.dcount;
        double dsd = g_ab.dsumsq / (double)g_ab.dcount - dmean * dmean;
        double dsem;

        dsd = (dsd > 0.0) ? sqrt(dsd) : 0.0;
        dsem = dsd / sqrt((double)g_ab.dcount);

        fprintf(stderr,
                "  paired   delta B-A %+.4f ms/frame (%+.2f%%), "
                "2-sigma +-%.4f ms  (%u cycles, sd %.3f)  --> %s\n",
                dmean * 1e3, 100.0 * dmean / mean[0], 2.0 * dsem * 1e3,
                g_ab.dcount, dsd * 1e3,
                (dmean < -2.0 * dsem) ? "B IS FASTER"
                                      : (dmean > 2.0 * dsem ? "B IS SLOWER"
                                                            : "NEUTRAL"));
    }
}
