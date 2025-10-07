#include <sys/time.h>

#include <immintrin.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_TESTS 10000000
#define MIN_NUMERATOR -3500
#define MAX_NUMERATOR 3500
#define MIN_DENOMINATOR 16
#define MAX_DENOMINATOR 3500

// Test data structure
typedef struct
{
    int numerator;
    int denominator;
    int integer_result;
    float sse_result;
    float error;
    float relative_error;
} DivisionTest;

static DivisionTest tests[NUM_TESTS];

// SSE fast reciprocal division using rcpps (4 samples at once)
static inline void
fast_reciprocal_division_sse_4x(int* numerators, int* denominators, float* results)
{
    // Load 4 numerators and denominators
    __m128 num = _mm_set_ps(
        (float)numerators[3], (float)numerators[2], (float)numerators[1], (float)numerators[0]);
    __m128 den = _mm_set_ps(
        (float)denominators[3],
        (float)denominators[2],
        (float)denominators[1],
        (float)denominators[0]);

    // Use reciprocal approximation (rcpps) for 4 values
    __m128 rcp = _mm_rcp_ps(den);

    // Multiply numerators by reciprocals
    __m128 result = _mm_mul_ps(num, rcp);

    // Store results
    _mm_store_ps(results, result);
}

// Single sample version for remainder processing
static inline float
fast_reciprocal_division_sse(int numerator, int denominator)
{
    __m128 num = _mm_set_ss((float)numerator);
    __m128 den = _mm_set_ss((float)denominator);

    // Use reciprocal approximation (rcpss)
    __m128 rcp = _mm_rcp_ss(den);

    // Multiply numerator by reciprocal
    __m128 result = _mm_mul_ss(num, rcp);

    return _mm_cvtss_f32(result);
}

// SSE fast reciprocal division with Newton-Raphson refinement (4 samples at once)
static inline void
fast_reciprocal_division_sse_refined_4x(int* numerators, int* denominators, float* results)
{
    // Load 4 numerators and denominators
    __m128 num = _mm_set_ps(
        (float)numerators[3], (float)numerators[2], (float)numerators[1], (float)numerators[0]);
    __m128 den = _mm_set_ps(
        (float)denominators[3],
        (float)denominators[2],
        (float)denominators[1],
        (float)denominators[0]);

    // Use reciprocal approximation (rcpps) for 4 values
    __m128 rcp = _mm_rcp_ps(den);

    // Newton-Raphson refinement: rcp = rcp * (2.0 - den * rcp)
    __m128 two = _mm_set1_ps(2.0f);
    __m128 temp = _mm_mul_ps(den, rcp);
    temp = _mm_sub_ps(two, temp);
    rcp = _mm_mul_ps(rcp, temp);

    // Multiply numerators by refined reciprocals
    __m128 result = _mm_mul_ps(num, rcp);

    // Store results
    _mm_store_ps(results, result);
}

// Single sample version for remainder processing
static inline float
fast_reciprocal_division_sse_refined(int numerator, int denominator)
{
    __m128 num = _mm_set_ss((float)numerator);
    __m128 den = _mm_set_ss((float)denominator);

    // Use reciprocal approximation (rcpss)
    __m128 rcp = _mm_rcp_ss(den);

    // Newton-Raphson refinement: rcp = rcp * (2.0 - den * rcp)
    __m128 two = _mm_set_ss(2.0f);
    __m128 temp = _mm_mul_ss(den, rcp);
    temp = _mm_sub_ss(two, temp);
    rcp = _mm_mul_ss(rcp, temp);

    // Multiply numerator by refined reciprocal
    __m128 result = _mm_mul_ss(num, rcp);

    return _mm_cvtss_f32(result);
}

// Regular integer division (for comparison)
static inline int
integer_division(int numerator, int denominator)
{
    return numerator / denominator;
}

// Regular float division (for accuracy reference)
static inline float
float_division(int numerator, int denominator)
{
    return (float)numerator / (float)denominator;
}

// Generate test data
static void
generate_test_data()
{
    printf("Generating %d test cases...\n", NUM_TESTS);
    srand(42); // Fixed seed for reproducible results

    for( int i = 0; i < NUM_TESTS; i++ )
    {
        // Generate random numerator in range [-3500, +3500]
        tests[i].numerator = MIN_NUMERATOR + rand() % (MAX_NUMERATOR - MIN_NUMERATOR + 1);

        // Generate random denominator in range [16, 3500] (avoid small denominators for stability)
        tests[i].denominator = MIN_DENOMINATOR + rand() % (MAX_DENOMINATOR - MIN_DENOMINATOR + 1);

        // Calculate reference results
        tests[i].integer_result = integer_division(tests[i].numerator, tests[i].denominator);
    }
    printf("Generated %d test cases\n", NUM_TESTS);
}

// Analyze accuracy of SSE reciprocal division
static void
analyze_accuracy()
{
    printf("\nAnalyzing accuracy of SSE reciprocal division...\n");

    double total_error = 0.0;
    double total_relative_error = 0.0;
    double max_error = 0.0;
    double max_relative_error = 0.0;
    int zero_division_cases = 0;

    for( int i = 0; i < NUM_TESTS; i++ )
    {
        float reference = float_division(tests[i].numerator, tests[i].denominator);
        tests[i].sse_result =
            fast_reciprocal_division_sse(tests[i].numerator, tests[i].denominator);

        tests[i].error = fabsf(tests[i].sse_result - reference);

        if( fabsf(reference) > 1e-6 )
        {
            tests[i].relative_error = tests[i].error / fabsf(reference);
        }
        else
        {
            tests[i].relative_error = 0.0;
            zero_division_cases++;
        }

        total_error += tests[i].error;
        total_relative_error += tests[i].relative_error;

        if( tests[i].error > max_error )
        {
            max_error = tests[i].error;
        }

        if( tests[i].relative_error > max_relative_error )
        {
            max_relative_error = tests[i].relative_error;
        }
    }

    printf("Accuracy Analysis:\n");
    printf("  Average absolute error: %.6f\n", total_error / NUM_TESTS);
    printf("  Maximum absolute error: %.6f\n", max_error);
    printf(
        "  Average relative error: %.6f (%.4f%%)\n",
        total_relative_error / NUM_TESTS,
        (total_relative_error / NUM_TESTS) * 100.0);
    printf(
        "  Maximum relative error: %.6f (%.4f%%)\n",
        max_relative_error,
        max_relative_error * 100.0);
    printf("  Cases near zero: %d\n", zero_division_cases);
}

// Benchmark functions
static double
benchmark_integer_division()
{
    struct timeval start, end;
    gettimeofday(&start, NULL);

    volatile int result = 0;
    for( int i = 0; i < NUM_TESTS; i++ )
    {
        result += integer_division(tests[i].numerator, tests[i].denominator);
    }

    gettimeofday(&end, NULL);

    // Prevent optimization
    if( result == 0xDEADBEEF )
    {
        printf("Unlikely result\n");
    }

    double us = (end.tv_sec - start.tv_sec) * 1000000.0;
    us += (end.tv_usec - start.tv_usec);
    return us;
}

static double
benchmark_sse_reciprocal()
{
    struct timeval start, end;
    gettimeofday(&start, NULL);

    volatile float result = 0.0f;

    // Process 4 samples at a time
    for( int i = 0; i < NUM_TESTS - 3; i += 4 )
    {
        int numerators[4] = { tests[i].numerator,
                              tests[i + 1].numerator,
                              tests[i + 2].numerator,
                              tests[i + 3].numerator };
        int denominators[4] = { tests[i].denominator,
                                tests[i + 1].denominator,
                                tests[i + 2].denominator,
                                tests[i + 3].denominator };
        float results[4] __attribute__((aligned(16)));

        fast_reciprocal_division_sse_4x(numerators, denominators, results);

        result += results[0] + results[1] + results[2] + results[3];
    }

    // Handle remaining samples (if NUM_TESTS is not divisible by 4)
    for( int i = NUM_TESTS - (NUM_TESTS % 4); i < NUM_TESTS; i++ )
    {
        result += fast_reciprocal_division_sse(tests[i].numerator, tests[i].denominator);
    }

    gettimeofday(&end, NULL);

    // Prevent optimization
    if( result == 12345.6789f )
    {
        printf("Unlikely result\n");
    }

    double us = (end.tv_sec - start.tv_sec) * 1000000.0;
    us += (end.tv_usec - start.tv_usec);
    return us;
}

static double
benchmark_sse_reciprocal_refined()
{
    struct timeval start, end;
    gettimeofday(&start, NULL);

    volatile float result = 0.0f;

    // Process 4 samples at a time
    for( int i = 0; i < NUM_TESTS - 3; i += 4 )
    {
        int numerators[4] = { tests[i].numerator,
                              tests[i + 1].numerator,
                              tests[i + 2].numerator,
                              tests[i + 3].numerator };
        int denominators[4] = { tests[i].denominator,
                                tests[i + 1].denominator,
                                tests[i + 2].denominator,
                                tests[i + 3].denominator };
        float results[4] __attribute__((aligned(16)));

        fast_reciprocal_division_sse_refined_4x(numerators, denominators, results);

        result += results[0] + results[1] + results[2] + results[3];
    }

    // Handle remaining samples (if NUM_TESTS is not divisible by 4)
    for( int i = NUM_TESTS - (NUM_TESTS % 4); i < NUM_TESTS; i++ )
    {
        result += fast_reciprocal_division_sse_refined(tests[i].numerator, tests[i].denominator);
    }

    gettimeofday(&end, NULL);

    // Prevent optimization
    if( result == 12345.6789f )
    {
        printf("Unlikely result\n");
    }

    double us = (end.tv_sec - start.tv_sec) * 1000000.0;
    us += (end.tv_usec - start.tv_usec);
    return us;
}

static double
benchmark_float_division()
{
    struct timeval start, end;
    gettimeofday(&start, NULL);

    volatile float result = 0.0f;
    for( int i = 0; i < NUM_TESTS; i++ )
    {
        result += float_division(tests[i].numerator, tests[i].denominator);
    }

    gettimeofday(&end, NULL);

    // Prevent optimization
    if( result == 12345.6789f )
    {
        printf("Unlikely result\n");
    }

    double us = (end.tv_sec - start.tv_sec) * 1000000.0;
    us += (end.tv_usec - start.tv_usec);
    return us;
}

// Show comprehensive value comparisons
static void
show_value_comparisons()
{
    printf("\nDetailed Value Comparisons (first 20 cases):\n");
    printf(
        "%-8s %-8s %-8s %-20s %-20s %-20s %-12s %-12s\n",
        "Num",
        "Den",
        "Integer",
        "Float (int)",
        "SSE Fast (int)",
        "SSE Refined (int)",
        "Fast Err",
        "Ref Err");
    printf(
        "%s\n",
        "=========================================================================================="
        "======================"
        "=======");

    for( int i = 0; i < 20 && i < NUM_TESTS; i++ )
    {
        int num = tests[i].numerator;
        int den = tests[i].denominator;

        // Temporary buffers for formatted strings
        char temp_buf1[32], temp_buf2[32], temp_buf3[32];

        int int_result = integer_division(num, den);
        float float_result = float_division(num, den);
        float sse_fast = fast_reciprocal_division_sse(num, den);
        float sse_refined = fast_reciprocal_division_sse_refined(num, den);

        float fast_error = fabsf(sse_fast - float_result);
        float refined_error = fabsf(sse_refined - float_result);

        printf(
            "%-8d %-8d %-8d %-20s %-20s %-20s %-12.6f %-12.6f\n",
            num,
            den,
            int_result,
            // Format float result with cast-to-int in parentheses
            (snprintf(temp_buf1, sizeof(temp_buf1), "%.6f (%d)", float_result, (int)float_result),
             temp_buf1),
            // Format SSE fast result with cast-to-int in parentheses
            (snprintf(temp_buf2, sizeof(temp_buf2), "%.6f (%d)", sse_fast, (int)sse_fast),
             temp_buf2),
            // Format SSE refined result with cast-to-int in parentheses
            (snprintf(temp_buf3, sizeof(temp_buf3), "%.6f (%d)", sse_refined, (int)sse_refined),
             temp_buf3),
            fast_error,
            refined_error);
    }

    // Show some specific interesting cases
    printf("\nSpecific Test Cases:\n");
    printf(
        "%-8s %-8s %-8s %-20s %-20s %-20s %-12s %-12s\n",
        "Num",
        "Den",
        "Integer",
        "Float (int)",
        "SSE Fast (int)",
        "SSE Refined (int)",
        "Fast Err",
        "Ref Err");
    printf(
        "%s\n",
        "=========================================================================================="
        "======================"
        "=======");

    // Test some edge cases
    int test_cases[][2] = {
        { 100,   16   }, // Simple case
        { -100,  16   }, // Negative numerator
        { 1000,  3    }, // Large result
        { 1,     1000 }, // Small result
        { 3500,  16   }, // Max numerator, min denominator
        { -3500, 3500 }, // Max negative / max positive
        { 7,     23   }, // Prime numbers
        { 1024,  32   }, // Powers of 2
        { 99,    100  }, // Close to 1
        { 1,     2    }  // Simple fraction
    };

    for( int i = 0; i < 10; i++ )
    {
        int num = test_cases[i][0];
        int den = test_cases[i][1];

        // Temporary buffers for formatted strings
        char temp_buf1[32], temp_buf2[32], temp_buf3[32];

        int int_result = integer_division(num, den);
        float float_result = float_division(num, den);
        float sse_fast = fast_reciprocal_division_sse(num, den);
        float sse_refined = fast_reciprocal_division_sse_refined(num, den);

        float fast_error = fabsf(sse_fast - float_result);
        float refined_error = fabsf(sse_refined - float_result);

        printf(
            "%-8d %-8d %-8d %-20s %-20s %-20s %-12.6f %-12.6f\n",
            num,
            den,
            int_result,
            // Format float result with cast-to-int in parentheses
            (snprintf(temp_buf1, sizeof(temp_buf1), "%.6f (%d)", float_result, (int)float_result),
             temp_buf1),
            // Format SSE fast result with cast-to-int in parentheses
            (snprintf(temp_buf2, sizeof(temp_buf2), "%.6f (%d)", sse_fast, (int)sse_fast),
             temp_buf2),
            // Format SSE refined result with cast-to-int in parentheses
            (snprintf(temp_buf3, sizeof(temp_buf3), "%.6f (%d)", sse_refined, (int)sse_refined),
             temp_buf3),
            fast_error,
            refined_error);
    }
}

int
main()
{
    printf("SSE Fast Reciprocal Division vs Integer Division Benchmark\n");
    printf("==========================================================\n");
    printf(
        "Test range: Numerator [%d, %d], Denominator [%d, %d]\n",
        MIN_NUMERATOR,
        MAX_NUMERATOR,
        MIN_DENOMINATOR,
        MAX_DENOMINATOR);
    printf("Number of test cases: %d\n\n", NUM_TESTS);

    // Generate test data
    generate_test_data();

    // Analyze accuracy
    analyze_accuracy();

    // Show value comparisons
    show_value_comparisons();

    // Warm up
    printf("\nWarming up...\n");
    for( int i = 0; i < 3; i++ )
    {
        benchmark_integer_division();
        benchmark_float_division();
        benchmark_sse_reciprocal();
        benchmark_sse_reciprocal_refined();
    }

    // Run benchmarks
    printf("Running benchmarks...\n");

    double time_integer = benchmark_integer_division();
    double time_float = benchmark_float_division();
    double time_sse = benchmark_sse_reciprocal();
    double time_sse_refined = benchmark_sse_reciprocal_refined();

    // Results
    printf("\nBenchmark Results:\n");
    printf("==================\n");
    printf(
        "Integer Division:           %.1f μs (%.3f ns per operation)\n",
        time_integer,
        time_integer * 1000.0 / NUM_TESTS);
    printf(
        "Float Division:             %.1f μs (%.3f ns per operation)\n",
        time_float,
        time_float * 1000.0 / NUM_TESTS);
    printf(
        "SSE Fast Reciprocal:        %.1f μs (%.3f ns per operation)\n",
        time_sse,
        time_sse * 1000.0 / NUM_TESTS);
    printf(
        "SSE Fast Reciprocal (Refined): %.1f μs (%.3f ns per operation)\n",
        time_sse_refined,
        time_sse_refined * 1000.0 / NUM_TESTS);

    printf("\nSpeedup Analysis:\n");
    printf("=================\n");
    printf(
        "SSE vs Integer:        %.2fx %s\n",
        time_integer / time_sse,
        (time_sse < time_integer) ? "faster" : "slower");
    printf(
        "SSE vs Float:          %.2fx %s\n",
        time_float / time_sse,
        (time_sse < time_float) ? "faster" : "slower");
    printf(
        "SSE Refined vs Integer: %.2fx %s\n",
        time_integer / time_sse_refined,
        (time_sse_refined < time_integer) ? "faster" : "slower");
    printf(
        "SSE Refined vs Float:   %.2fx %s\n",
        time_float / time_sse_refined,
        (time_sse_refined < time_float) ? "faster" : "slower");

    return 0;
}

// Build & run:
//   cd scripts/benchmarks && clang -O3 -msse benchmark_reciprocal_division.c -o
//   benchmark_reciprocal_division && ./benchmark_reciprocal_division | cat
