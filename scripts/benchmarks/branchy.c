#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Point struct, extend with payload if needed
typedef struct
{
    int x, y;
} Point;

// -------- Cycle Counter --------
#if defined(__aarch64__)
static inline uint64_t
rdcycle(void)
{
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
#elif defined(__arm__)
static inline uint64_t
rdcycle(void)
{
    uint32_t pmccntr;
    uint32_t pmuseren;
    uint32_t pmcntenset;
    // Read the user mode perf monitor counter access permissions
    asm volatile("mrc p15, 0, %0, c9, c14, 0" : "=r"(pmuseren));
    if( pmuseren & 1 )
    { // Allowed to access counters
        asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r"(pmcntenset));
        if( pmcntenset & 0x80000000ul )
        { // Is it counting?
            asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(pmccntr));
            return ((uint64_t)pmccntr) * 64; // Adjust for the frequency difference
        }
    }
    return 0; // No counter access
}
#else
static inline uint64_t
rdcycle(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec; // ns pseudo-cycle
}
#endif

// -------- Branchy sorting network (3 elements, by y) --------
static inline void
cmp_swap(Point* a, Point* b)
{
    if( a->y > b->y )
    {
        Point tmp = *a;
        *a = *b;
        *b = tmp;
    }
}
static inline void
sort3_branchy(Point* p)
{
    cmp_swap(&p[0], &p[1]);
    cmp_swap(&p[1], &p[2]);
    cmp_swap(&p[0], &p[1]);
}

// -------- Dedicated 6-way branch (all permutations of 3) --------
static inline void
sort3_dedicated(Point* p)
{
    // Expand into cases: we only branch once to choose correct order
    if( p[0].y <= p[1].y && p[1].y <= p[2].y )
    {
        // already sorted
    }
    else if( p[0].y <= p[2].y && p[2].y <= p[1].y )
    {
        Point t = p[1];
        p[1] = p[2];
        p[2] = t;
    }
    else if( p[1].y <= p[0].y && p[0].y <= p[2].y )
    {
        Point t = p[0];
        p[0] = p[1];
        p[1] = t;
    }
    else if( p[1].y <= p[2].y && p[2].y <= p[0].y )
    {
        Point t0 = p[0], t1 = p[1], t2 = p[2];
        p[0] = t1;
        p[1] = t2;
        p[2] = t0;
    }
    else if( p[2].y <= p[0].y && p[0].y <= p[1].y )
    {
        Point t0 = p[0], t1 = p[1], t2 = p[2];
        p[0] = t2;
        p[1] = t0;
        p[2] = t1;
    }
    else
    { // p2 <= p1 <= p0
        Point t = p[0];
        p[0] = p[2];
        p[2] = t;
    }
}

// -------- Main Benchmark --------
// gcc -O3 -march=native -o branchy branchy.c
// ./branchy 20000000   # 20 million trials

int
main(int argc, char** argv)
{
    long N = (argc > 1) ? atol(argv[1]) : 10000000L;
    Point pts[3];
    uint64_t t0, t1, t2;
    unsigned long long checksum = 0;

    srand(42);

    // --- Branchy ---
    t0 = rdcycle();
    for( long i = 0; i < N; i++ )
    {
        pts[0].x = rand();
        pts[0].y = rand();
        pts[1].x = rand();
        pts[1].y = rand();
        pts[2].x = rand();
        pts[2].y = rand();
        sort3_branchy(pts);
        checksum += pts[0].y; // prevent dead-code elimination
    }
    t1 = rdcycle();

    // --- Dedicated ---
    for( long i = 0; i < N; i++ )
    {
        pts[0].x = rand();
        pts[0].y = rand();
        pts[1].x = rand();
        pts[1].y = rand();
        pts[2].x = rand();
        pts[2].y = rand();
        sort3_dedicated(pts);
        checksum += pts[0].y;
    }
    t2 = rdcycle();

    fprintf(stderr, "checksum=%llu\n", checksum);
    printf("Branchy:   %llu cycles\n", (unsigned long long)(t1 - t0));
    printf("Dedicated: %llu cycles\n", (unsigned long long)(t2 - t1));
    printf("Per iter:  %.2f vs %.2f cycles\n", (double)(t1 - t0) / N, (double)(t2 - t1) / N);

    return 0;
}
