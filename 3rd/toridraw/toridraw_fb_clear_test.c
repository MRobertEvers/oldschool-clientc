/*
 * toridraw_fb_clear_test -- the asm clear must write exactly what the C writes.
 *
 * The non-temporal kernel has three paths the C twin does not: a scalar head
 * that runs until the pointer is 16-byte aligned, a 64-byte block loop, and a
 * 16-byte block loop, plus a scalar tail. Every boundary between them is a
 * place to be off by one pixel, and a clear that is off by one pixel produces
 * a single stale column at the edge of the screen -- the kind of defect that
 * survives a whole session of looking at it.
 *
 * So the test does not check a representative size. It checks every length
 * from 0 to 200 at every one of the four possible pixel alignments, which
 * covers each entry and exit of each loop, and then the real framebuffer size.
 * Guard pixels either side catch a write that runs past the end.
 *
 * Built and run on the host as a 32-bit binary; the correctness of the kernel
 * does not depend on the Pentium 4, only its speed does. -b also times it, but
 * a host timing says nothing about the target -- read that number on the XP
 * box or not at all.
 */

#include "graphics/fb_clear.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(TORIDRAW_FB_CLEAR_ASM) || !TORIDRAW_FB_CLEAR_ASM
#error "build this with -DTORIDRAW_FB_CLEAR_ASM=1; there is nothing to compare"
#endif

#define GUARD 8
#define SLACK 64

static int failures = 0;

static void
check(uint32_t* base, size_t offset_px, size_t count, uint32_t value,
      size_t slots)
{
    /*
     * Two buffers rather than one cleared twice: a kernel that reads what it
     * just wrote would pass the one-buffer version by accident.
     */
    static uint32_t* ref = NULL;
    static size_t ref_slots = 0;
    size_t i;

    if( ref_slots < slots )
    {
        free(ref);
        ref = (uint32_t*)malloc(slots * sizeof(uint32_t));
        assert(ref);
        ref_slots = slots;
    }

    for( i = 0; i < slots; i++ )
    {
        base[i] = 0xDEADBEEFu;
        ref[i] = 0xDEADBEEFu;
    }

    ToriDraw_FbClear32(ref + GUARD + offset_px, count, value);
    toridraw_fb_clear32_nt_xrgb8888_asm(base + GUARD + offset_px, count, value);

    for( i = 0; i < slots; i++ )
    {
        if( base[i] == ref[i] )
            continue;
        printf("MISMATCH len=%u align=%u slot=%u  asm=%08X ref=%08X\n",
               (unsigned)count, (unsigned)offset_px, (unsigned)i,
               (unsigned)base[i], (unsigned)ref[i]);
        failures++;
        if( failures > 12 )
        {
            printf("...stopping after 12\n");
            exit(1);
        }
        return;
    }
}

int
main(int argc, char** argv)
{
    const size_t FB_PX = 765u * 503u;
    size_t slots = FB_PX + 2u * GUARD + SLACK;
    /* One extra cache line so the +align walk stays inside the allocation. */
    uint32_t* raw = (uint32_t*)malloc((slots + 16) * sizeof(uint32_t));
    uint32_t* base;
    size_t len;
    size_t align;
    int bench = (argc > 1 && strcmp(argv[1], "-b") == 0);

    assert(raw);
    /* Align the buffer itself, then offset deliberately, so "align" means what
     * it says instead of whatever malloc happened to return. */
    base = (uint32_t*)((((uintptr_t)raw) + 63u) & ~(uintptr_t)63u);

    for( align = 0; align < 4; align++ )
        for( len = 0; len <= 200; len++ )
            check(base, align, len, 0xFF202428u, slots);

    for( align = 0; align < 4; align++ )
        check(base, align, FB_PX, 0xFF202428u, slots);

    if( failures )
    {
        printf("FAIL: %d mismatches\n", failures);
        return 1;
    }
    printf("PASS: 804 lengths x 4 alignments + the 765x503 framebuffer, "
           "byte-identical\n");

    if( bench )
    {
        /*
         * Host timing only. The whole point of the kernel is a Pentium 4's
         * read-for-ownership cost, which this machine does not have.
         */
        const int REPS = 2000;
        int i;
        clock_t t0;
        double c_ms;
        double asm_ms;

        t0 = clock();
        for( i = 0; i < REPS; i++ )
            ToriDraw_FbClear32(base + GUARD, FB_PX, 0xFF202428u);
        c_ms = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e3 / REPS;

        t0 = clock();
        for( i = 0; i < REPS; i++ )
            toridraw_fb_clear32_nt_xrgb8888_asm(base + GUARD, FB_PX, 0xFF202428u);
        asm_ms = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e3 / REPS;

        printf("host only: C %.3f ms  asm %.3f ms  %.2fx  "
               "(NOT the target; measure on XP)\n",
               c_ms, asm_ms, c_ms / asm_ms);
    }
    return 0;
}
