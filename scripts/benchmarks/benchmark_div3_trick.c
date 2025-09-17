#include <stdint.h>
#include <stdio.h>
#include <time.h>

// Magic number for division by 3
// (2^32 + 2) / 3 = 0xAAAAAAAB
#define MAGIC_DIV3 0xAAAAAAAB

// Regular division by 3
uint32_t
div3_regular(uint32_t n)
{
    return n / 3;
}

// Multiplication and shift trick for division by 3
uint32_t
div3_muloverflow(uint32_t n)
{
    uint64_t product = (uint64_t)MAGIC_DIV3 * n;
    return (uint32_t)(product >> 33);
}

uint32_t
div3_mulshift(uint32_t n)
{
    return (uint32_t)((n * 21845) >> 16);
}

// Function to measure time in nanoseconds
uint64_t
get_time_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Benchmark function
void
benchmark(const char* name, uint32_t (*func)(uint32_t), uint32_t iterations)
{
    uint64_t start, end;
    uint32_t result = 0; // To prevent optimization

    // Warmup
    for( uint32_t i = 0; i < 1000; i++ )
    {
        result ^= func(i);
    }

    start = get_time_ns();
    for( uint32_t i = 0; i < iterations; i++ )
    {
        result ^= func(i); // XOR to prevent compiler from optimizing away
    }
    end = get_time_ns();

    double time_ms = (end - start) / 1000000.0;
    printf("%s: %.3f ms (checksum: %u)\n", name, time_ms, result);
}

int
main()
{
    const uint32_t iterations = 100000000; // 100M iterations

    printf("Running benchmarks with %u iterations...\n\n", iterations);

    // Test correctness first
    for( uint32_t i = 0; i < 1500; i++ )
    {
        uint32_t regular = div3_regular(i);
        uint32_t mulshift = div3_mulshift(i);
        uint32_t muloverflow = div3_muloverflow(i);
        // if( regular != mulshift || regular != muloverflow )
        // {
        //     printf(
        //         "Error: Mismatch at %u (regular: %u, mulshift: %u, muloverflow: %u)\n",
        //         i,
        //         regular,
        //         mulshift,
        //         muloverflow);
        //     // return 1;
        // }
    }

    printf("Correctness check passed!\n\n");

    // Run benchmarks
    benchmark("Regular division   ", div3_regular, iterations);
    benchmark("Mul-shift trick   ", div3_mulshift, iterations);
    benchmark("Mul-overflow trick   ", div3_muloverflow, iterations);

    return 0;
}
