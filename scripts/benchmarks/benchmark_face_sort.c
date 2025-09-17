#include <sys/time.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_FACES 1770
#define MAX_VERTS 3400
#define MAX_DEPTH 1500
#define MAX_PRIORITY 12
#define MAX_FACES_PER_DEPTH 512 // 2^9
#define MAX_FACES_PER_PRIORITY 2000

// Buffers (simulate buckets)
static short face_depth_buckets[MAX_DEPTH * MAX_FACES_PER_DEPTH];
static short face_depth_bucket_counts[MAX_DEPTH];
static short face_priority_buckets[MAX_PRIORITY * MAX_FACES_PER_PRIORITY];
static short face_priority_bucket_counts[MAX_PRIORITY];

// For valid depth collection
static int valid_depths[MAX_DEPTH];
static int num_valid_depths;

// Vertex + faces
static int vertex_x[MAX_VERTS];
static int vertex_y[MAX_VERTS];
static int vertex_z[MAX_VERTS];

static int face_a[NUM_FACES];
static int face_b[NUM_FACES];
static int face_c[NUM_FACES];
static int face_priorities[NUM_FACES];

// -------------------- Helper --------------------

static int
cmp_desc(const void* a, const void* b)
{
    return (*(int*)b - *(int*)a);
}

// Partition using valid depths
void
partition_faces_by_priority_valid(
    int* valid_depths,
    int num_valid_depths,
    short* face_depth_buckets,
    short* face_depth_bucket_counts,
    short* face_priority_buckets,
    short* face_priority_bucket_counts,
    int* face_priorities)
{
    for( int i = 0; i < num_valid_depths; i++ )
    {
        int depth = valid_depths[i];
        int face_count = face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        short* faces = &face_depth_buckets[depth * MAX_FACES_PER_DEPTH];
        for( int j = 0; j < face_count; j++ )
        {
            int face_idx = faces[j];
            int prio = face_priorities[face_idx];
            if( prio >= 0 && prio < MAX_PRIORITY )
            {
                int priority_face_count = face_priority_bucket_counts[prio];
                if( priority_face_count < MAX_FACES_PER_PRIORITY )
                {
                    face_priority_bucket_counts[prio]++;
                    face_priority_buckets[prio * MAX_FACES_PER_PRIORITY + priority_face_count] =
                        face_idx;
                }
            }
        }
    }
}

// Partition baseline: scan all depths
void
partition_faces_by_priority_fullscan(
    int depth_lower_bound,
    int depth_upper_bound,
    short* face_depth_buckets,
    short* face_depth_bucket_counts,
    short* face_priority_buckets,
    short* face_priority_bucket_counts,
    int* face_priorities)
{
    for( int depth = depth_upper_bound; depth >= depth_lower_bound && depth < MAX_DEPTH; depth-- )
    {
        int face_count = face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        short* faces = &face_depth_buckets[depth * MAX_FACES_PER_DEPTH];
        for( int j = 0; j < face_count; j++ )
        {
            int face_idx = faces[j];
            int prio = face_priorities[face_idx];
            if( prio >= 0 && prio < MAX_PRIORITY )
            {
                int priority_face_count = face_priority_bucket_counts[prio];
                if( priority_face_count < MAX_FACES_PER_PRIORITY )
                {
                    face_priority_bucket_counts[prio]++;
                    face_priority_buckets[prio * MAX_FACES_PER_PRIORITY + priority_face_count] =
                        face_idx;
                }
            }
        }
    }
}

// -------------------- Version 1: Baseline full scan --------------------

void
pipeline_fullscan()
{
    for( int i = 0; i < MAX_DEPTH; i++ )
        face_depth_bucket_counts[i] = 0;
    for( int i = 0; i < MAX_PRIORITY; i++ )
        face_priority_bucket_counts[i] = 0;

    int min_depth = INT_MAX, max_depth = INT_MIN;

    for( int f = 0; f < NUM_FACES; f++ )
    {
        int za = vertex_z[face_a[f]];
        int zb = vertex_z[face_b[f]];
        int zc = vertex_z[face_c[f]];
        int depth_average = (za + zb + zc) / 3 + 500;
        if( depth_average <= 0 || depth_average >= MAX_DEPTH )
            continue;

        int bucket_index = face_depth_bucket_counts[depth_average];
        if( bucket_index < MAX_FACES_PER_DEPTH )
        {
            face_depth_bucket_counts[depth_average]++;
            face_depth_buckets[depth_average * MAX_FACES_PER_DEPTH + bucket_index] = f;

            if( depth_average < min_depth )
                min_depth = depth_average;
            if( depth_average > max_depth )
                max_depth = depth_average;
        }
    }

    if( min_depth == INT_MAX )
        return;
    partition_faces_by_priority_fullscan(
        min_depth,
        max_depth,
        face_depth_buckets,
        face_depth_bucket_counts,
        face_priority_buckets,
        face_priority_bucket_counts,
        face_priorities);
}

// -------------------- Version 2: qsort valid depths --------------------

void
pipeline_qsort()
{
    for( int i = 0; i < MAX_DEPTH; i++ )
        face_depth_bucket_counts[i] = 0;
    for( int i = 0; i < MAX_PRIORITY; i++ )
        face_priority_bucket_counts[i] = 0;
    num_valid_depths = 0;

    for( int f = 0; f < NUM_FACES; f++ )
    {
        int za = vertex_z[face_a[f]];
        int zb = vertex_z[face_b[f]];
        int zc = vertex_z[face_c[f]];
        int depth_average = (za + zb + zc) / 3 + 500;
        if( depth_average <= 0 || depth_average >= MAX_DEPTH )
            continue;

        if( face_depth_bucket_counts[depth_average] == 0 )
        {
            valid_depths[num_valid_depths++] = depth_average;
        }

        int bucket_index = face_depth_bucket_counts[depth_average];
        if( bucket_index < MAX_FACES_PER_DEPTH )
        {
            face_depth_bucket_counts[depth_average]++;
            face_depth_buckets[depth_average * MAX_FACES_PER_DEPTH + bucket_index] = f;
        }
    }

    qsort(valid_depths, num_valid_depths, sizeof(int), cmp_desc);

    partition_faces_by_priority_valid(
        valid_depths,
        num_valid_depths,
        face_depth_buckets,
        face_depth_bucket_counts,
        face_priority_buckets,
        face_priority_bucket_counts,
        face_priorities);
}

// -------------------- Version 3: insert sorted valid depths --------------------

void
pipeline_insert()
{
    for( int i = 0; i < MAX_DEPTH; i++ )
        face_depth_bucket_counts[i] = 0;
    for( int i = 0; i < MAX_PRIORITY; i++ )
        face_priority_bucket_counts[i] = 0;
    num_valid_depths = 0;

    for( int f = 0; f < NUM_FACES; f++ )
    {
        int za = vertex_z[face_a[f]];
        int zb = vertex_z[face_b[f]];
        int zc = vertex_z[face_c[f]];
        int depth_average = (za + zb + zc) / 3 + 500;
        if( depth_average <= 0 || depth_average >= MAX_DEPTH )
            continue;

        if( face_depth_bucket_counts[depth_average] == 0 )
        {
            int k = num_valid_depths;
            while( k > 0 && valid_depths[k - 1] < depth_average )
            {
                valid_depths[k] = valid_depths[k - 1];
                k--;
            }
            valid_depths[k] = depth_average;
            num_valid_depths++;
        }

        int bucket_index = face_depth_bucket_counts[depth_average];
        if( bucket_index < MAX_FACES_PER_DEPTH )
        {
            face_depth_bucket_counts[depth_average]++;
            face_depth_buckets[depth_average * MAX_FACES_PER_DEPTH + bucket_index] = f;
        }
    }

    partition_faces_by_priority_valid(
        valid_depths,
        num_valid_depths,
        face_depth_buckets,
        face_depth_bucket_counts,
        face_priority_buckets,
        face_priority_bucket_counts,
        face_priorities);
}

// -------------------- Timing Helper --------------------

double
time_func(void (*func)())
{
    struct timeval start, end;
    gettimeofday(&start, NULL);
    func();
    gettimeofday(&end, NULL);

    // Calculate microseconds
    double microseconds = (end.tv_sec - start.tv_sec) * 1000000.0; // Convert seconds to us
    microseconds += (end.tv_usec - start.tv_usec);                 // Add microseconds
    return microseconds;
}

// -------------------- Main --------------------

// clang -O3 ./benchmark_face_sort.c -o benchmark_face_sort && ./benchmark_face_sort
int
main()
{
    srand(42);

    // Populate vertices
    for( int i = 0; i < MAX_VERTS; i++ )
    {
        vertex_x[i] = rand() % 2000 - 1000;
        vertex_y[i] = rand() % 2000 - 1000;
        vertex_z[i] = rand() % 2000 - 1000;
    }

    // Populate faces
    for( int i = 0; i < NUM_FACES; i++ )
    {
        face_a[i] = rand() % MAX_VERTS;
        face_b[i] = rand() % MAX_VERTS;
        face_c[i] = rand() % MAX_VERTS;
        face_priorities[i] = rand() % MAX_PRIORITY;
    }

    // Warmup
    pipeline_fullscan();
    pipeline_qsort();
    pipeline_insert();

    // Benchmark
    double t1 = time_func(pipeline_fullscan);
    double t2 = time_func(pipeline_qsort);
    double t3 = time_func(pipeline_insert);

    printf("Pipeline Fullscan: %.1f us\n", t1);
    printf("Pipeline Qsort:    %.1f us\n", t2);
    printf("Pipeline Insert:   %.1f us\n", t3);

    return 0;
}