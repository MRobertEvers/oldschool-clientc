#include "sound_vorbis.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* math.h only promises M_PI under _GNU_SOURCE / POSIX; MSVC never defines it. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * A port of the reference's Vorbis decoder, not a general one.
 *
 * Structure follows the reference class for class: bit reader, codebook, floor
 * (type 1 only), residue, mapping, mode, then the per-packet inverse MDCT and
 * the lapped overlap-add that turns a packet list into PCM. Jagex's encoder
 * emits mono streams with one submap and no channel coupling, and the
 * reference decoder assumes exactly that -- so does this. Anything outside the
 * subset fails the decode instead of guessing, because a Vorbis stream that is
 * *nearly* decoded is white noise rather than a wrong note, and silence is the
 * better failure.
 *
 * Everything allocated by a decode is owned by the setup or the sample; there
 * are no globals. The reference keeps its setup in statics and reparses it
 * whenever a sound effect brings its own, which works only because playback is
 * single-threaded and immediately follows the parse.
 */

#define VORBIS_MAX_CODEBOOKS 256
#define VORBIS_MAX_FLOORS 64
#define VORBIS_MAX_RESIDUES 64
#define VORBIS_MAX_MAPPINGS 64
#define VORBIS_MAX_MODES 64
/** Floor-1 partition classes and the X list they build. Spec caps the list at
 *  65 (two endpoints plus 63 partition values). */
#define VORBIS_MAX_FLOOR_X 128

/* ---- bit reader ---------------------------------------------------------- */

/*
 * LSB-first, byte-at-a-time, exactly the reference's class61.
 *
 * `overrun` replaces the reference's ArrayIndexOutOfBoundsException: a
 * malformed packet must fail the decode rather than walk off the buffer, and
 * failing lazily (keep returning zeroes, check once at the end) keeps the
 * decode loops free of error branches.
 */
struct vorbis_bits
{
    const uint8_t* data;
    int size;
    int byte_pos;
    int bit_pos;
    bool overrun;
};

static void
bits_init(
    struct vorbis_bits* b,
    const uint8_t* data,
    int size)
{
    b->data = data;
    b->size = size;
    b->byte_pos = 0;
    b->bit_pos = 0;
    b->overrun = false;
}

static int
bits_read(
    struct vorbis_bits* b,
    int count)
{
    int value = 0;
    int shift = 0;

    while( count >= 8 - b->bit_pos )
    {
        int avail = 8 - b->bit_pos;
        int mask = (1 << avail) - 1;
        if( b->byte_pos >= b->size )
        {
            b->overrun = true;
            return value;
        }
        value += ((b->data[b->byte_pos] >> b->bit_pos) & mask) << shift;
        b->bit_pos = 0;
        b->byte_pos++;
        shift += avail;
        count -= avail;
    }
    if( count > 0 )
    {
        int mask = (1 << count) - 1;
        if( b->byte_pos >= b->size )
        {
            b->overrun = true;
            return value;
        }
        value += ((b->data[b->byte_pos] >> b->bit_pos) & mask) << shift;
        b->bit_pos += count;
    }
    return value;
}

static int
bits_read1(struct vorbis_bits* b)
{
    int value;

    if( b->byte_pos >= b->size )
    {
        b->overrun = true;
        return 0;
    }
    value = (b->data[b->byte_pos] >> b->bit_pos) & 1;
    b->bit_pos++;
    b->byte_pos += b->bit_pos >> 3;
    b->bit_pos &= 7;
    return value;
}

/** Reference class333.method1821 — bit length of `value` (ilog(0) == 0). */
static int
vorbis_ilog(int value)
{
    int bits = 0;

    if( value < 0 || value >= 65536 )
    {
        value = (int)((uint32_t)value >> 16);
        bits += 16;
    }
    if( value >= 256 )
    {
        value >>= 8;
        bits += 8;
    }
    if( value >= 16 )
    {
        value >>= 4;
        bits += 4;
    }
    if( value >= 4 )
    {
        value >>= 2;
        bits += 2;
    }
    if( value >= 1 )
    {
        value >>= 1;
        bits += 1;
    }
    return value + bits;
}

/** Spec float32_unpack (reference class55.method1047). */
static float
vorbis_float32(int packed)
{
    int mantissa = packed & 0x1FFFFF;
    int sign = packed & (int)0x80000000u;
    int exponent = (packed & 0x7FE00000) >> 21;

    if( sign != 0 )
        mantissa = -mantissa;
    return (float)((double)mantissa * pow(2.0, (double)(exponent - 788)));
}

/* ---- codebook ------------------------------------------------------------ */

struct vorbis_codebook
{
    int dimensions;
    int entries;
    int* lengths;      /* [entries] */
    int* multiplicands;/* lookup table, or NULL */
    float** vectors;   /* [entries][dimensions], or NULL when lookup type 0 */
    float* vector_pool;/* backing store for `vectors` */
    int* huffman;      /* decode tree */
    int huffman_size;
};

/** Reference class53.method1021 — spec lookup1_values. */
static int
codebook_lookup1_values(
    int entries,
    int dimensions)
{
    int guess = (int)pow((double)entries, 1.0 / (double)dimensions) + 1;

    for( ;; )
    {
        int base = guess;
        int exponent = dimensions;
        int accumulator = 1;
        int total;

        while( exponent > 1 )
        {
            if( (exponent & 1) != 0 )
                accumulator = base * accumulator;
            base *= base;
            exponent >>= 1;
        }
        total = exponent == 1 ? base * accumulator : accumulator;
        if( total <= entries )
            return guess;
        guess--;
        if( guess < 1 )
            return 1;
    }
}

/*
 * Build the flat Huffman decode tree (reference class53.method1017).
 *
 * Nodes are indices into `huffman`; a negative entry is `~entry_index` and ends
 * the walk. The reference grows the array by doubling from 8, and the growth is
 * kept rather than sized up front because the final size depends on the
 * codeword assignment, not on the entry count.
 */
static bool
codebook_build_tree(struct vorbis_codebook* book)
{
    int* codewords = NULL;
    int available[33];
    int next_free = 0;

    codewords = calloc((size_t)(book->entries > 0 ? book->entries : 1), sizeof(int));
    if( !codewords )
        return false;
    memset(available, 0, sizeof(available));

    for( int i = 0; i < book->entries; i++ )
    {
        int length = book->lengths[i];
        int bit;
        int candidate;
        int next;

        if( length == 0 )
            continue;
        bit = 1 << (32 - length);
        candidate = available[length];
        codewords[i] = candidate;
        if( (candidate & bit) != 0 )
        {
            next = available[length - 1];
        }
        else
        {
            next = candidate | bit;
            for( int j = length - 1; j >= 1; j-- )
            {
                int held = available[j];
                int mask;
                if( candidate != held )
                    break;
                mask = 1 << (32 - j);
                if( (held & mask) != 0 )
                {
                    available[j] = available[j - 1];
                    break;
                }
                available[j] = held | mask;
            }
        }
        available[length] = next;
        for( int j = length + 1; j <= 32; j++ )
        {
            if( candidate == available[j] )
                available[j] = next;
        }
    }

    book->huffman_size = 8;
    book->huffman = calloc((size_t)book->huffman_size, sizeof(int));
    if( !book->huffman )
    {
        free(codewords);
        return false;
    }

    for( int i = 0; i < book->entries; i++ )
    {
        int length = book->lengths[i];
        int codeword;
        int node = 0;

        if( length == 0 )
            continue;
        codeword = codewords[i];
        for( int bit = 0; bit < length; bit++ )
        {
            int probe = (int)(0x80000000u >> bit);
            if( (codeword & probe) != 0 )
            {
                if( book->huffman[node] == 0 )
                    book->huffman[node] = next_free;
                node = book->huffman[node];
            }
            else
            {
                node++;
            }
            if( node >= book->huffman_size )
            {
                int grown_size = book->huffman_size * 2;
                int* grown;
                while( node >= grown_size )
                    grown_size *= 2;
                grown = realloc(book->huffman, (size_t)grown_size * sizeof(int));
                if( !grown )
                {
                    free(codewords);
                    return false;
                }
                memset(grown + book->huffman_size, 0,
                       (size_t)(grown_size - book->huffman_size) * sizeof(int));
                book->huffman = grown;
                book->huffman_size = grown_size;
            }
        }
        book->huffman[node] = ~i;
        if( node >= next_free )
            next_free = node + 1;
    }

    free(codewords);
    return true;
}

static void
codebook_release(struct vorbis_codebook* book)
{
    free(book->lengths);
    free(book->multiplicands);
    free(book->vectors);
    free(book->vector_pool);
    free(book->huffman);
    memset(book, 0, sizeof(*book));
}

static bool
codebook_decode(
    struct vorbis_codebook* book,
    struct vorbis_bits* bits)
{
    int lookup_type;

    bits_read(bits, 24); /* sync pattern 0x564342 */
    book->dimensions = bits_read(bits, 16);
    book->entries = bits_read(bits, 24);
    if( book->dimensions <= 0 || book->entries <= 0 || book->entries > (1 << 24) )
        return false;
    book->lengths = calloc((size_t)book->entries, sizeof(int));
    if( !book->lengths )
        return false;

    if( bits_read1(bits) != 0 )
    {
        /* Ordered: run-lengths of equal codeword lengths. */
        int entry = 0;
        int length = bits_read(bits, 5) + 1;
        while( entry < book->entries )
        {
            int run = bits_read(bits, vorbis_ilog(book->entries - entry));
            if( run < 0 || run > book->entries - entry )
                return false;
            for( int i = 0; i < run; i++ )
                book->lengths[entry++] = length;
            length++;
            if( bits->overrun )
                return false;
        }
    }
    else
    {
        bool sparse = bits_read1(bits) != 0;
        for( int i = 0; i < book->entries; i++ )
        {
            if( sparse && bits_read1(bits) == 0 )
                book->lengths[i] = 0;
            else
                book->lengths[i] = bits_read(bits, 5) + 1;
        }
    }
    if( bits->overrun )
        return false;
    if( !codebook_build_tree(book) )
        return false;

    lookup_type = bits_read(bits, 4);
    if( lookup_type > 0 )
    {
        float minimum = vorbis_float32(bits_read(bits, 32));
        float delta = vorbis_float32(bits_read(bits, 32));
        int value_bits = bits_read(bits, 4) + 1;
        bool sequence = bits_read1(bits) != 0;
        int lookup_values;

        if( lookup_type == 1 )
            lookup_values = codebook_lookup1_values(book->entries, book->dimensions);
        else
            lookup_values = book->dimensions * book->entries;
        if( lookup_values <= 0 )
            return false;

        book->multiplicands = calloc((size_t)lookup_values, sizeof(int));
        if( !book->multiplicands )
            return false;
        for( int i = 0; i < lookup_values; i++ )
            book->multiplicands[i] = bits_read(bits, value_bits);
        if( bits->overrun )
            return false;

        book->vector_pool =
            calloc((size_t)book->entries * (size_t)book->dimensions, sizeof(float));
        book->vectors = calloc((size_t)book->entries, sizeof(float*));
        if( !book->vector_pool || !book->vectors )
            return false;
        for( int i = 0; i < book->entries; i++ )
            book->vectors[i] = book->vector_pool + (size_t)i * (size_t)book->dimensions;

        if( lookup_type == 1 )
        {
            for( int entry = 0; entry < book->entries; entry++ )
            {
                float last = 0.0f;
                int index_divisor = 1;
                for( int d = 0; d < book->dimensions; d++ )
                {
                    int offset = entry / index_divisor % lookup_values;
                    float value = (float)book->multiplicands[offset] * delta + minimum + last;
                    book->vectors[entry][d] = value;
                    if( sequence )
                        last = value;
                    index_divisor = lookup_values * index_divisor;
                }
            }
        }
        else
        {
            for( int entry = 0; entry < book->entries; entry++ )
            {
                float last = 0.0f;
                int offset = book->dimensions * entry;
                for( int d = 0; d < book->dimensions; d++ )
                {
                    float value = (float)book->multiplicands[offset] * delta + minimum + last;
                    book->vectors[entry][d] = value;
                    if( sequence )
                        last = value;
                    offset++;
                }
            }
        }
    }
    return !bits->overrun;
}

/** Walk the Huffman tree. Returns -1 when the packet ran out. */
static int
codebook_read_entry(
    const struct vorbis_codebook* book,
    struct vorbis_bits* bits)
{
    int node = 0;

    while( book->huffman[node] >= 0 )
    {
        node = bits_read1(bits) != 0 ? book->huffman[node] : node + 1;
        if( bits->overrun || node < 0 || node >= book->huffman_size )
            return -1;
    }
    return ~book->huffman[node];
}

/* ---- floor (type 1) ------------------------------------------------------ */

static const int VORBIS_FLOOR_RANGES[4] = { 256, 128, 86, 64 };

static const float VORBIS_INVERSE_DB[256] = {
    1.0649863e-7f, 1.1341951e-7f, 1.2079015e-7f, 1.2863978e-7f, 1.369995e-7f,  1.459025e-7f,
    1.5538409e-7f, 1.6548181e-7f, 1.7623574e-7f, 1.8768856e-7f, 1.998856e-7f,  2.128753e-7f,
    2.2670913e-7f, 2.4144197e-7f, 2.5713223e-7f, 2.7384212e-7f, 2.9163792e-7f, 3.1059022e-7f,
    3.307741e-7f,  3.5226967e-7f, 3.7516213e-7f, 3.995423e-7f,  4.255068e-7f,  4.5315863e-7f,
    4.8260745e-7f, 5.1397e-7f,    5.4737063e-7f, 5.829419e-7f,  6.208247e-7f,  6.611694e-7f,
    7.041359e-7f,  7.4989464e-7f, 7.98627e-7f,   8.505263e-7f,  9.057983e-7f,  9.646621e-7f,
    1.0273513e-6f, 1.0941144e-6f, 1.1652161e-6f, 1.2409384e-6f, 1.3215816e-6f, 1.4074654e-6f,
    1.4989305e-6f, 1.5963394e-6f, 1.7000785e-6f, 1.8105592e-6f, 1.9282195e-6f, 2.053526e-6f,
    2.1869757e-6f, 2.3290977e-6f, 2.4804558e-6f, 2.6416496e-6f, 2.813319e-6f,  2.9961443e-6f,
    3.1908505e-6f, 3.39821e-6f,   3.619045e-6f,  3.8542307e-6f, 4.1047006e-6f, 4.371447e-6f,
    4.6555283e-6f, 4.958071e-6f,  5.280274e-6f,  5.623416e-6f,  5.988857e-6f,  6.3780467e-6f,
    6.7925284e-6f, 7.2339453e-6f, 7.704048e-6f,  8.2047e-6f,    8.737888e-6f,  9.305725e-6f,
    9.910464e-6f,  1.0554501e-5f, 1.1240392e-5f, 1.1970856e-5f, 1.2748789e-5f, 1.3577278e-5f,
    1.4459606e-5f, 1.5399271e-5f, 1.6400005e-5f, 1.7465769e-5f, 1.8600793e-5f, 1.9809577e-5f,
    2.1096914e-5f, 2.2467912e-5f, 2.3928002e-5f, 2.5482977e-5f, 2.7139005e-5f, 2.890265e-5f,
    3.078091e-5f,  3.2781227e-5f, 3.4911533e-5f, 3.718028e-5f,  3.9596467e-5f, 4.2169668e-5f,
    4.491009e-5f,  4.7828602e-5f, 5.0936775e-5f, 5.424693e-5f,  5.7772202e-5f, 6.152657e-5f,
    6.552491e-5f,  6.9783084e-5f, 7.4317984e-5f, 7.914758e-5f,  8.429104e-5f,  8.976875e-5f,
    9.560242e-5f,  1.0181521e-4f, 1.0843174e-4f, 1.1547824e-4f, 1.2298267e-4f, 1.3097477e-4f,
    1.3948625e-4f, 1.4855085e-4f, 1.5820454e-4f, 1.6848555e-4f, 1.7943469e-4f, 1.9109536e-4f,
    2.0351382e-4f, 2.167393e-4f,  2.3082423e-4f, 2.4582449e-4f, 2.6179955e-4f, 2.7881275e-4f,
    2.9693157e-4f, 3.1622787e-4f, 3.3677815e-4f, 3.5866388e-4f, 3.8197188e-4f, 4.0679457e-4f,
    4.3323037e-4f, 4.613841e-4f,  4.913675e-4f,  5.2329927e-4f, 5.573062e-4f,  5.935231e-4f,
    6.320936e-4f,  6.731706e-4f,  7.16917e-4f,   7.635063e-4f,  8.1312325e-4f, 8.6596457e-4f,
    9.2223985e-4f, 9.821722e-4f,  0.0010459992f, 0.0011139743f, 0.0011863665f, 0.0012634633f,
    0.0013455702f, 0.0014330129f, 0.0015261382f, 0.0016253153f, 0.0017309374f, 0.0018434235f,
    0.0019632196f, 0.0020908006f, 0.0022266726f, 0.0023713743f, 0.0025254795f, 0.0026895993f,
    0.0028643848f, 0.0030505287f, 0.003248769f,  0.0034598925f, 0.0036847359f, 0.0039241905f,
    0.0041792067f, 0.004450795f,  0.004740033f,  0.005048067f,  0.0053761187f, 0.005725489f,
    0.0060975635f, 0.0064938175f, 0.0069158226f, 0.0073652514f, 0.007843887f,  0.008353627f,
    0.008896492f,  0.009474637f,  0.010090352f,  0.01074608f,   0.011444421f,  0.012188144f,
    0.012980198f,  0.013823725f,  0.014722068f,  0.015678791f,  0.016697686f,  0.017782796f,
    0.018938422f,  0.020169148f,  0.021479854f,  0.022875736f,  0.02436233f,   0.025945531f,
    0.027631618f,  0.029427277f,  0.031339627f,  0.03337625f,   0.035545226f,  0.037855156f,
    0.0403152f,    0.042935107f,  0.045725275f,  0.048696756f,  0.05186135f,   0.05523159f,
    0.05882085f,   0.062643364f,  0.06671428f,   0.07104975f,   0.075666964f,  0.08058423f,
    0.08582105f,   0.09139818f,   0.097337745f,  0.1036633f,    0.11039993f,   0.11757434f,
    0.12521498f,   0.13335215f,   0.14201812f,   0.15124726f,   0.16107617f,   0.1715438f,
    0.18269168f,   0.19456401f,   0.20720787f,   0.22067343f,   0.23501402f,   0.25028655f,
    0.26655158f,   0.28387362f,   0.3023213f,    0.32196787f,   0.34289113f,   0.36517414f,
    0.3889052f,    0.41417846f,   0.44109413f,   0.4697589f,    0.50028646f,   0.53279793f,
    0.5674221f,    0.6042964f,    0.64356697f,   0.6853896f,    0.72993004f,   0.777365f,
    0.8278826f,    0.88168305f,   0.9389798f,    1.0f
};

struct vorbis_floor
{
    int partition_count;
    int* partition_class;   /* [partition_count] */
    int class_count;
    int* class_dimensions;  /* [class_count] */
    int* class_subclasses;  /* [class_count] */
    int* class_masterbook;  /* [class_count] */
    int** class_subbook;    /* [class_count][1 << subclasses] */
    int multiplier;
    int* x_list;            /* [x_count] */
    int x_count;
};

static void
floor_release(struct vorbis_floor* floor)
{
    if( floor->class_subbook )
    {
        for( int i = 0; i < floor->class_count; i++ )
            free(floor->class_subbook[i]);
        free(floor->class_subbook);
    }
    free(floor->partition_class);
    free(floor->class_dimensions);
    free(floor->class_subclasses);
    free(floor->class_masterbook);
    free(floor->x_list);
    memset(floor, 0, sizeof(*floor));
}

static bool
floor_decode(
    struct vorbis_floor* floor,
    struct vorbis_bits* bits)
{
    int class_max = 0;
    int rangebits;
    int x_count = 2;
    int cursor;

    if( bits_read(bits, 16) != 1 )
        return false; /* only floor type 1 exists in this corpus */

    floor->partition_count = bits_read(bits, 5);
    floor->partition_class = calloc((size_t)(floor->partition_count + 1), sizeof(int));
    if( !floor->partition_class )
        return false;
    for( int i = 0; i < floor->partition_count; i++ )
    {
        int cls = bits_read(bits, 4);
        floor->partition_class[i] = cls;
        if( cls >= class_max )
            class_max = cls + 1;
    }
    floor->class_count = class_max;
    if( class_max > 0 )
    {
        floor->class_dimensions = calloc((size_t)class_max, sizeof(int));
        floor->class_subclasses = calloc((size_t)class_max, sizeof(int));
        floor->class_masterbook = calloc((size_t)class_max, sizeof(int));
        floor->class_subbook = calloc((size_t)class_max, sizeof(int*));
        if( !floor->class_dimensions || !floor->class_subclasses || !floor->class_masterbook ||
            !floor->class_subbook )
            return false;
    }
    for( int i = 0; i < class_max; i++ )
    {
        int subclasses;
        int subbook_count;

        floor->class_dimensions[i] = bits_read(bits, 3) + 1;
        subclasses = bits_read(bits, 2);
        floor->class_subclasses[i] = subclasses;
        if( subclasses != 0 )
            floor->class_masterbook[i] = bits_read(bits, 8);
        subbook_count = 1 << subclasses;
        floor->class_subbook[i] = calloc((size_t)subbook_count, sizeof(int));
        if( !floor->class_subbook[i] )
            return false;
        for( int j = 0; j < subbook_count; j++ )
            floor->class_subbook[i][j] = bits_read(bits, 8) - 1;
    }

    floor->multiplier = bits_read(bits, 2) + 1;
    rangebits = bits_read(bits, 4);
    for( int i = 0; i < floor->partition_count; i++ )
        x_count += floor->class_dimensions[floor->partition_class[i]];
    if( x_count < 2 || x_count > VORBIS_MAX_FLOOR_X )
        return false;

    floor->x_count = x_count;
    floor->x_list = calloc((size_t)x_count, sizeof(int));
    if( !floor->x_list )
        return false;
    floor->x_list[0] = 0;
    floor->x_list[1] = 1 << rangebits;
    cursor = 2;
    for( int i = 0; i < floor->partition_count; i++ )
    {
        int cls = floor->partition_class[i];
        for( int j = 0; j < floor->class_dimensions[cls]; j++ )
            floor->x_list[cursor++] = bits_read(bits, rangebits);
    }
    return !bits->overrun;
}

/** Per-packet floor curve state (reference class60). */
struct vorbis_floor_curve
{
    const struct vorbis_floor* floor;
    bool present;
    int x[VORBIS_MAX_FLOOR_X];
    int y[VORBIS_MAX_FLOOR_X];
    bool step2[VORBIS_MAX_FLOOR_X];
};

/** Reference class42.method766 — nearest lower neighbour by X. */
static int
floor_neighbour_lower(
    const int* x,
    int index)
{
    int target = x[index];
    int best = -1;
    int best_value = INT32_MIN;

    for( int i = 0; i < index; i++ )
    {
        if( x[i] < target && x[i] > best_value )
        {
            best = i;
            best_value = x[i];
        }
    }
    return best;
}

/** Reference class42.method767 — nearest higher neighbour by X. */
static int
floor_neighbour_higher(
    const int* x,
    int index)
{
    int target = x[index];
    int best = -1;
    int best_value = INT32_MAX;

    for( int i = 0; i < index; i++ )
    {
        if( x[i] > target && x[i] < best_value )
        {
            best = i;
            best_value = x[i];
        }
    }
    return best;
}

/** Reference class42.method774 — render_point. */
static int
floor_render_point(
    int x0,
    int y0,
    int x1,
    int y1,
    int x)
{
    int dy = y1 - y0;
    int adx = x1 - x0;
    int ady = dy < 0 ? -dy : dy;
    int off;

    if( adx == 0 )
        return y0;
    off = (x - x0) * ady / adx;
    return dy < 0 ? y0 - off : y0 + off;
}

/** Reference class42.method769 — render_line into `out`, capped at `n`. */
static void
floor_render_line(
    int x0,
    int y0,
    int x1,
    int y1,
    float* out,
    int n)
{
    int dy = y1 - y0;
    int adx = x1 - x0;
    int ady = dy < 0 ? -dy : dy;
    int base;
    int y = y0;
    int err = 0;
    int sy;

    if( adx <= 0 || x0 < 0 || x0 >= n )
        return;
    base = dy / adx;
    sy = dy < 0 ? base - 1 : base + 1;
    ady = ady - (base < 0 ? -base : base) * adx;
    out[x0] *= VORBIS_INVERSE_DB[y0 & 255];
    if( x1 > n )
        x1 = n;
    for( int x = x0 + 1; x < x1; x++ )
    {
        err += ady;
        if( err >= adx )
        {
            err -= adx;
            y += sy;
        }
        else
        {
            y += base;
        }
        out[x] *= VORBIS_INVERSE_DB[y & 255];
    }
}

/** Reference class60.method1113 — sort the X list, carrying Y and the flags. */
static void
floor_curve_sort(
    struct vorbis_floor_curve* curve,
    int lo,
    int hi)
{
    int pivot_index;
    int pivot_x;
    int pivot_y;
    bool pivot_flag;

    if( lo >= hi )
        return;
    pivot_index = lo;
    pivot_x = curve->x[lo];
    pivot_y = curve->y[lo];
    pivot_flag = curve->step2[lo];
    for( int i = lo + 1; i <= hi; i++ )
    {
        int value = curve->x[i];
        if( value < pivot_x )
        {
            curve->x[pivot_index] = value;
            curve->y[pivot_index] = curve->y[i];
            curve->step2[pivot_index] = curve->step2[i];
            pivot_index++;
            curve->x[i] = curve->x[pivot_index];
            curve->y[i] = curve->y[pivot_index];
            curve->step2[i] = curve->step2[pivot_index];
        }
    }
    curve->x[pivot_index] = pivot_x;
    curve->y[pivot_index] = pivot_y;
    curve->step2[pivot_index] = pivot_flag;
    floor_curve_sort(curve, lo, pivot_index - 1);
    floor_curve_sort(curve, pivot_index + 1, hi);
}

struct vorbis_setup;

static bool
floor_curve_decode(
    struct vorbis_floor_curve* curve,
    const struct vorbis_floor* floor,
    const struct RSCache_VorbisSetup* setup,
    struct vorbis_bits* bits);

/** Reference class60.method1104 — expand the curve over the spectrum. */
static void
floor_curve_apply(
    struct vorbis_floor_curve* curve,
    float* out,
    int n)
{
    const struct vorbis_floor* floor = curve->floor;
    int count = floor->x_count;
    int range = VORBIS_FLOOR_RANGES[floor->multiplier - 1];
    int last_x;
    int last_y;

    curve->step2[0] = true;
    curve->step2[1] = true;
    for( int i = 2; i < count; i++ )
    {
        int lower = floor_neighbour_lower(curve->x, i);
        int higher = floor_neighbour_higher(curve->x, i);
        int predicted;
        int value;
        int room_high;
        int room;

        if( lower < 0 || higher < 0 )
        {
            curve->step2[i] = false;
            continue;
        }
        predicted = floor_render_point(
            curve->x[lower], curve->y[lower], curve->x[higher], curve->y[higher], curve->x[i]);
        value = curve->y[i];
        room_high = range - predicted;
        room = (room_high < predicted ? room_high : predicted) << 1;
        if( value != 0 )
        {
            curve->step2[lower] = true;
            curve->step2[higher] = true;
            curve->step2[i] = true;
            if( value >= room )
                curve->y[i] = room_high > predicted ? value - predicted + predicted
                                                    : predicted - value + room_high - 1;
            else
                curve->y[i] =
                    (value & 1) != 0 ? predicted - (value + 1) / 2 : value / 2 + predicted;
        }
        else
        {
            curve->step2[i] = false;
            curve->y[i] = predicted;
        }
    }

    floor_curve_sort(curve, 0, count - 1);

    last_x = 0;
    last_y = curve->y[0] * floor->multiplier;
    for( int i = 1; i < count; i++ )
    {
        int x;
        int y;

        if( !curve->step2[i] )
            continue;
        x = curve->x[i];
        y = curve->y[i] * floor->multiplier;
        floor_render_line(last_x, last_y, x, y, out, n);
        if( x >= n )
            return;
        last_x = x;
        last_y = y;
    }
    {
        float tail = VORBIS_INVERSE_DB[last_y & 255];
        for( int x = last_x; x < n; x++ )
            out[x] *= tail;
    }
}

/* ---- residue ------------------------------------------------------------- */

struct vorbis_residue
{
    int type;
    int begin;
    int end;
    int partition_size;
    int classifications;
    int classbook;
    int* books; /* [classifications * 8] */
};

static void
residue_release(struct vorbis_residue* residue)
{
    free(residue->books);
    memset(residue, 0, sizeof(*residue));
}

static bool
residue_decode(
    struct vorbis_residue* residue,
    struct vorbis_bits* bits)
{
    int* cascade;

    residue->type = bits_read(bits, 16);
    residue->begin = bits_read(bits, 24);
    residue->end = bits_read(bits, 24);
    residue->partition_size = bits_read(bits, 24) + 1;
    residue->classifications = bits_read(bits, 6) + 1;
    residue->classbook = bits_read(bits, 8);
    if( residue->partition_size <= 0 || residue->classifications <= 0 )
        return false;

    cascade = calloc((size_t)residue->classifications, sizeof(int));
    if( !cascade )
        return false;
    for( int i = 0; i < residue->classifications; i++ )
    {
        int high = 0;
        int low = bits_read(bits, 3);
        if( bits_read1(bits) != 0 )
            high = bits_read(bits, 5);
        cascade[i] = high << 3 | low;
    }

    residue->books = calloc((size_t)residue->classifications * 8, sizeof(int));
    if( !residue->books )
    {
        free(cascade);
        return false;
    }
    for( int i = 0; i < residue->classifications * 8; i++ )
        residue->books[i] = (cascade[i >> 3] & 1 << (i & 7)) != 0 ? bits_read(bits, 8) : -1;
    free(cascade);
    return !bits->overrun;
}

/* ---- mapping and mode ---------------------------------------------------- */

struct vorbis_mapping
{
    int submap_count;
    int mux; /* the single channel's submap; mono, so one value */
    int* floor_of_submap;
    int* residue_of_submap;
};

static void
mapping_release(struct vorbis_mapping* mapping)
{
    free(mapping->floor_of_submap);
    free(mapping->residue_of_submap);
    memset(mapping, 0, sizeof(*mapping));
}

static bool
mapping_decode(
    struct vorbis_mapping* mapping,
    struct vorbis_bits* bits)
{
    bits_read(bits, 16);
    mapping->submap_count = bits_read1(bits) != 0 ? bits_read(bits, 4) + 1 : 1;
    if( bits_read1(bits) != 0 )
        bits_read(bits, 8); /* coupling steps: mono, so never present */
    bits_read(bits, 2);
    if( mapping->submap_count > 1 )
        mapping->mux = bits_read(bits, 4);
    if( mapping->submap_count <= 0 || mapping->submap_count > 16 )
        return false;

    mapping->floor_of_submap = calloc((size_t)mapping->submap_count, sizeof(int));
    mapping->residue_of_submap = calloc((size_t)mapping->submap_count, sizeof(int));
    if( !mapping->floor_of_submap || !mapping->residue_of_submap )
        return false;
    for( int i = 0; i < mapping->submap_count; i++ )
    {
        bits_read(bits, 8);
        mapping->floor_of_submap[i] = bits_read(bits, 8);
        mapping->residue_of_submap[i] = bits_read(bits, 8);
    }
    return !bits->overrun;
}

/* ---- setup --------------------------------------------------------------- */

struct vorbis_window_tables
{
    float* twiddle_a; /* [n/2] */
    float* twiddle_b; /* [n/2] */
    float* twiddle_c; /* [n/4] */
    int* bit_reverse; /* [n/8] */
};

struct RSCache_VorbisSetup
{
    int blocksize_short;
    int blocksize_long;
    struct vorbis_window_tables window[2]; /* [0] short, [1] long */

    struct vorbis_codebook codebooks[VORBIS_MAX_CODEBOOKS];
    int codebook_count;
    struct vorbis_floor floors[VORBIS_MAX_FLOORS];
    int floor_count;
    struct vorbis_residue residues[VORBIS_MAX_RESIDUES];
    int residue_count;
    struct vorbis_mapping mappings[VORBIS_MAX_MAPPINGS];
    int mapping_count;

    bool mode_blockflag[VORBIS_MAX_MODES];
    int mode_mapping[VORBIS_MAX_MODES];
    int mode_count;
};

static void
window_tables_release(struct vorbis_window_tables* tables)
{
    free(tables->twiddle_a);
    free(tables->twiddle_b);
    free(tables->twiddle_c);
    free(tables->bit_reverse);
    memset(tables, 0, sizeof(*tables));
}

static bool
window_tables_build(
    struct vorbis_window_tables* tables,
    int n)
{
    int half = n >> 1;
    int quarter = n >> 2;
    int eighth = n >> 3;
    int reverse_bits;

    if( n < 8 )
        return false;
    tables->twiddle_a = calloc((size_t)half, sizeof(float));
    tables->twiddle_b = calloc((size_t)half, sizeof(float));
    tables->twiddle_c = calloc((size_t)quarter, sizeof(float));
    tables->bit_reverse = calloc((size_t)eighth, sizeof(int));
    if( !tables->twiddle_a || !tables->twiddle_b || !tables->twiddle_c || !tables->bit_reverse )
        return false;

    for( int i = 0; i < quarter; i++ )
    {
        tables->twiddle_a[i * 2] = (float)cos((double)(i * 4) * M_PI / (double)n);
        tables->twiddle_a[i * 2 + 1] = (float)-sin((double)(i * 4) * M_PI / (double)n);
    }
    for( int i = 0; i < quarter; i++ )
    {
        tables->twiddle_b[i * 2] = (float)cos((double)(i * 2 + 1) * M_PI / (double)(n * 2));
        tables->twiddle_b[i * 2 + 1] = (float)sin((double)(i * 2 + 1) * M_PI / (double)(n * 2));
    }
    for( int i = 0; i < eighth; i++ )
    {
        tables->twiddle_c[i * 2] = (float)cos((double)(i * 4 + 2) * M_PI / (double)n);
        tables->twiddle_c[i * 2 + 1] = (float)-sin((double)(i * 4 + 2) * M_PI / (double)n);
    }
    reverse_bits = vorbis_ilog(eighth - 1);
    for( int i = 0; i < eighth; i++ )
    {
        int value = i;
        int remaining = reverse_bits;
        int reversed = 0;
        while( remaining > 0 )
        {
            reversed = reversed << 1 | (value & 1);
            value = (int)((uint32_t)value >> 1);
            remaining--;
        }
        tables->bit_reverse[i] = reversed;
    }
    return true;
}

void
RSCache_VorbisSetupFree(struct RSCache_VorbisSetup* setup)
{
    if( !setup )
        return;
    for( int i = 0; i < setup->codebook_count; i++ )
        codebook_release(&setup->codebooks[i]);
    for( int i = 0; i < setup->floor_count; i++ )
        floor_release(&setup->floors[i]);
    for( int i = 0; i < setup->residue_count; i++ )
        residue_release(&setup->residues[i]);
    for( int i = 0; i < setup->mapping_count; i++ )
        mapping_release(&setup->mappings[i]);
    window_tables_release(&setup->window[0]);
    window_tables_release(&setup->window[1]);
    free(setup);
}

struct RSCache_VorbisSetup*
RSCache_VorbisSetupNewDecode(
    const char* data,
    int data_size)
{
    struct RSCache_VorbisSetup* setup;
    struct vorbis_bits bits;
    int count;

    if( !data || data_size <= 0 )
        return NULL;
    setup = calloc(1, sizeof(*setup));
    if( !setup )
        return NULL;
    bits_init(&bits, (const uint8_t*)data, data_size);

    setup->blocksize_short = 1 << bits_read(&bits, 4);
    setup->blocksize_long = 1 << bits_read(&bits, 4);
    if( setup->blocksize_short < 8 || setup->blocksize_long < setup->blocksize_short ||
        setup->blocksize_long > (1 << 16) )
        goto fail;
    if( !window_tables_build(&setup->window[0], setup->blocksize_short) )
        goto fail;
    if( !window_tables_build(&setup->window[1], setup->blocksize_long) )
        goto fail;

    count = bits_read(&bits, 8) + 1;
    if( count > VORBIS_MAX_CODEBOOKS )
        goto fail;
    for( int i = 0; i < count; i++ )
    {
        setup->codebook_count = i + 1;
        if( !codebook_decode(&setup->codebooks[i], &bits) )
            goto fail;
    }

    count = bits_read(&bits, 6) + 1; /* time domain transforms: placeholders */
    for( int i = 0; i < count; i++ )
        bits_read(&bits, 16);

    count = bits_read(&bits, 6) + 1;
    if( count > VORBIS_MAX_FLOORS )
        goto fail;
    for( int i = 0; i < count; i++ )
    {
        setup->floor_count = i + 1;
        if( !floor_decode(&setup->floors[i], &bits) )
            goto fail;
    }

    count = bits_read(&bits, 6) + 1;
    if( count > VORBIS_MAX_RESIDUES )
        goto fail;
    for( int i = 0; i < count; i++ )
    {
        setup->residue_count = i + 1;
        if( !residue_decode(&setup->residues[i], &bits) )
            goto fail;
    }

    count = bits_read(&bits, 6) + 1;
    if( count > VORBIS_MAX_MAPPINGS )
        goto fail;
    for( int i = 0; i < count; i++ )
    {
        setup->mapping_count = i + 1;
        if( !mapping_decode(&setup->mappings[i], &bits) )
            goto fail;
    }

    count = bits_read(&bits, 6) + 1;
    if( count > VORBIS_MAX_MODES )
        goto fail;
    setup->mode_count = count;
    for( int i = 0; i < count; i++ )
    {
        setup->mode_blockflag[i] = bits_read1(&bits) != 0;
        bits_read(&bits, 16);
        bits_read(&bits, 16);
        setup->mode_mapping[i] = bits_read(&bits, 8);
    }
    if( bits.overrun )
        goto fail;

    /* Cross-check every index the packet decoder will follow, so a malformed
     * setup fails here rather than indexing out of bounds per packet. */
    for( int i = 0; i < setup->mode_count; i++ )
    {
        if( setup->mode_mapping[i] < 0 || setup->mode_mapping[i] >= setup->mapping_count )
            goto fail;
    }
    for( int i = 0; i < setup->mapping_count; i++ )
    {
        struct vorbis_mapping* mapping = &setup->mappings[i];
        if( mapping->mux < 0 || mapping->mux >= mapping->submap_count )
            goto fail;
        for( int j = 0; j < mapping->submap_count; j++ )
        {
            if( mapping->floor_of_submap[j] < 0 ||
                mapping->floor_of_submap[j] >= setup->floor_count )
                goto fail;
            if( mapping->residue_of_submap[j] < 0 ||
                mapping->residue_of_submap[j] >= setup->residue_count )
                goto fail;
        }
    }
    for( int i = 0; i < setup->residue_count; i++ )
    {
        struct vorbis_residue* residue = &setup->residues[i];
        if( residue->classbook < 0 || residue->classbook >= setup->codebook_count )
            goto fail;
        for( int j = 0; j < residue->classifications * 8; j++ )
        {
            if( residue->books[j] >= setup->codebook_count )
                goto fail;
        }
    }
    for( int i = 0; i < setup->floor_count; i++ )
    {
        struct vorbis_floor* floor = &setup->floors[i];
        for( int j = 0; j < floor->class_count; j++ )
        {
            int subbook_count = 1 << floor->class_subclasses[j];
            if( floor->class_subclasses[j] != 0 &&
                (floor->class_masterbook[j] < 0 ||
                 floor->class_masterbook[j] >= setup->codebook_count) )
                goto fail;
            for( int k = 0; k < subbook_count; k++ )
            {
                if( floor->class_subbook[j][k] >= setup->codebook_count )
                    goto fail;
            }
        }
    }
    return setup;

fail:
    RSCache_VorbisSetupFree(setup);
    return NULL;
}

/** Reference class42.method771 — read one packet's floor coefficients. */
static bool
floor_curve_decode(
    struct vorbis_floor_curve* curve,
    const struct vorbis_floor* floor,
    const struct RSCache_VorbisSetup* setup,
    struct vorbis_bits* bits)
{
    int cursor;
    int y_bits;

    curve->floor = floor;
    memset(curve->step2, 0, sizeof(curve->step2));
    curve->present = bits_read1(bits) != 0;
    if( !curve->present )
        return true;

    for( int i = 0; i < floor->x_count; i++ )
        curve->x[i] = floor->x_list[i];

    y_bits = vorbis_ilog(VORBIS_FLOOR_RANGES[floor->multiplier - 1] - 1);
    curve->y[0] = bits_read(bits, y_bits);
    curve->y[1] = bits_read(bits, y_bits);
    cursor = 2;
    for( int i = 0; i < floor->partition_count; i++ )
    {
        int cls = floor->partition_class[i];
        int dimensions = floor->class_dimensions[cls];
        int subclasses = floor->class_subclasses[cls];
        int mask = (1 << subclasses) - 1;
        int selector = 0;

        if( subclasses > 0 )
        {
            selector = codebook_read_entry(&setup->codebooks[floor->class_masterbook[cls]], bits);
            if( selector < 0 )
                return false;
        }
        for( int j = 0; j < dimensions; j++ )
        {
            int book = floor->class_subbook[cls][selector & mask];
            selector = (int)((uint32_t)selector >> subclasses);
            if( cursor >= floor->x_count )
                return false;
            if( book >= 0 )
            {
                int value = codebook_read_entry(&setup->codebooks[book], bits);
                if( value < 0 )
                    return false;
                curve->y[cursor++] = value;
            }
            else
            {
                curve->y[cursor++] = 0;
            }
        }
    }
    return !bits->overrun;
}

/** Reference class47.method866 — accumulate one residue into the spectrum. */
static bool
residue_apply(
    const struct vorbis_residue* residue,
    const struct RSCache_VorbisSetup* setup,
    float* out,
    int n,
    bool skip,
    struct vorbis_bits* bits,
    int* classifications_scratch,
    int scratch_size)
{
    const struct vorbis_codebook* classbook;
    int dimensions;
    int span;
    int partitions;

    for( int i = 0; i < n; i++ )
        out[i] = 0.0f;
    if( skip )
        return true;

    classbook = &setup->codebooks[residue->classbook];
    dimensions = classbook->dimensions;
    span = residue->end - residue->begin;
    if( span <= 0 )
        return true;
    partitions = span / residue->partition_size;
    if( partitions <= 0 )
        return true;
    if( partitions > scratch_size )
        return false;
    /* The reference trusts the stream; a residue whose span runs past the
     * spectrum would write off the end of `out`, so refuse it here. */
    if( residue->begin < 0 || residue->begin + partitions * residue->partition_size > n )
        return false;

    for( int pass = 0; pass < 8; pass++ )
    {
        int partition = 0;
        while( partition < partitions )
        {
            if( pass == 0 )
            {
                int classword = codebook_read_entry(classbook, bits);
                if( classword < 0 )
                    return false;
                for( int i = dimensions - 1; i >= 0; i-- )
                {
                    if( partition + i < partitions )
                        classifications_scratch[partition + i] =
                            classword % residue->classifications;
                    classword /= residue->classifications;
                }
            }
            for( int i = 0; i < dimensions; i++ )
            {
                int cls = classifications_scratch[partition];
                int book_index = residue->books[cls * 8 + pass];
                if( book_index >= 0 )
                {
                    int offset = residue->partition_size * partition + residue->begin;
                    const struct vorbis_codebook* book = &setup->codebooks[book_index];
                    if( !book->vectors )
                        return false;
                    if( residue->type == 0 )
                    {
                        int step = residue->partition_size / book->dimensions;
                        for( int j = 0; j < step; j++ )
                        {
                            int entry = codebook_read_entry(book, bits);
                            const float* vector;
                            if( entry < 0 || entry >= book->entries )
                                return false;
                            vector = book->vectors[entry];
                            for( int k = 0; k < book->dimensions; k++ )
                                out[step * k + offset + j] += vector[k];
                        }
                    }
                    else
                    {
                        int written = 0;
                        while( written < residue->partition_size )
                        {
                            int entry = codebook_read_entry(book, bits);
                            const float* vector;
                            if( entry < 0 || entry >= book->entries )
                                return false;
                            vector = book->vectors[entry];
                            for( int k = 0; k < book->dimensions; k++ )
                            {
                                if( written >= residue->partition_size )
                                    break;
                                out[offset + written] += vector[k];
                                written++;
                            }
                        }
                    }
                }
                partition++;
                if( partition >= partitions )
                    break;
            }
        }
    }
    return !bits->overrun;
}

/* ---- per-packet decode --------------------------------------------------- */

/** Rolling state across a sample's packets: the lapped window. */
struct vorbis_stream
{
    const struct RSCache_VorbisSetup* setup;
    float* work;     /* current packet's spectrum, blocksize_long floats */
    float* previous; /* previous packet's, same size */
    int previous_size;
    int previous_right;
    bool previous_empty;
    int* classifications;
    int classifications_size;
};

/*
 * Decode one packet into `out` (a caller buffer of at least
 * (previous_size + n) / 4 floats) and return how many samples were written.
 *
 * Returns -1 on a malformed packet. Returns 0 for the first packet of a
 * stream, which has no left half to overlap with and therefore emits nothing --
 * that is the standard Vorbis lapping rule, not an error.
 */
static int
vorbis_decode_packet(
    struct vorbis_stream* stream,
    const uint8_t* packet,
    int packet_size,
    float* out,
    int out_capacity)
{
    const struct RSCache_VorbisSetup* setup = stream->setup;
    struct vorbis_bits bits;
    struct vorbis_floor_curve curve;
    const struct vorbis_mapping* mapping;
    const struct vorbis_window_tables* tables;
    float* spectrum = stream->work;
    int mode;
    bool long_block;
    int n;
    int half;
    int quarter;
    int eighth;
    int left_start;
    int left_end;
    int left_window;
    int right_start;
    int right_end;
    int right_window;
    int produced = 0;
    int reverse_steps;

    bits_init(&bits, packet, packet_size);
    if( bits_read1(&bits) != 0 )
        return -1; /* not an audio packet */
    mode = bits_read(&bits, vorbis_ilog(setup->mode_count - 1));
    if( mode < 0 || mode >= setup->mode_count || bits.overrun )
        return -1;

    long_block = setup->mode_blockflag[mode];
    n = long_block ? setup->blocksize_long : setup->blocksize_short;
    half = n >> 1;
    quarter = n >> 2;
    eighth = n >> 3;

    {
        bool previous_long = false;
        bool next_long = false;
        if( long_block )
        {
            previous_long = bits_read1(&bits) != 0;
            next_long = bits_read1(&bits) != 0;
        }
        if( long_block && !previous_long )
        {
            left_start = quarter - (setup->blocksize_short >> 2);
            left_end = (setup->blocksize_short >> 2) + quarter;
            left_window = setup->blocksize_short >> 1;
        }
        else
        {
            left_start = 0;
            left_end = half;
            left_window = half;
        }
        if( long_block && !next_long )
        {
            right_start = n - quarter - (setup->blocksize_short >> 2);
            right_end = (setup->blocksize_short >> 2) + (n - quarter);
            right_window = setup->blocksize_short >> 1;
        }
        else
        {
            right_start = half;
            right_end = n;
            right_window = half;
        }
    }

    mapping = &setup->mappings[setup->mode_mapping[mode]];
    if( !floor_curve_decode(
            &curve, &setup->floors[mapping->floor_of_submap[mapping->mux]], setup, &bits) )
        return -1;

    for( int i = 0; i < mapping->submap_count; i++ )
    {
        if( !residue_apply(
                &setup->residues[mapping->residue_of_submap[i]],
                setup,
                spectrum,
                half,
                !curve.present,
                &bits,
                stream->classifications,
                stream->classifications_size) )
            return -1;
    }

    if( curve.present )
        floor_curve_apply(&curve, spectrum, half);

    if( !curve.present )
    {
        for( int i = half; i < n; i++ )
            spectrum[i] = 0.0f;
    }
    else
    {
        tables = &setup->window[long_block ? 1 : 0];

        for( int i = 0; i < half; i++ )
            spectrum[i] *= 0.5f;
        for( int i = half; i < n; i++ )
            spectrum[i] = -spectrum[n - i - 1];

        for( int i = 0; i < quarter; i++ )
        {
            float re = spectrum[i * 4] - spectrum[n - i * 4 - 1];
            float im = spectrum[i * 4 + 2] - spectrum[n - i * 4 - 3];
            float wr = tables->twiddle_a[i * 2];
            float wi = tables->twiddle_a[i * 2 + 1];
            spectrum[n - i * 4 - 1] = re * wr - im * wi;
            spectrum[n - i * 4 - 3] = re * wi + im * wr;
        }
        for( int i = 0; i < eighth; i++ )
        {
            float a = spectrum[i * 4 + half + 3];
            float b = spectrum[i * 4 + half + 1];
            float c = spectrum[i * 4 + 3];
            float d = spectrum[i * 4 + 1];
            float wr;
            float wi;
            spectrum[i * 4 + half + 3] = a + c;
            spectrum[i * 4 + half + 1] = b + d;
            wr = tables->twiddle_a[half - 4 - i * 4];
            wi = tables->twiddle_a[half - 3 - i * 4];
            spectrum[i * 4 + 3] = (a - c) * wr - (b - d) * wi;
            spectrum[i * 4 + 1] = (a - c) * wi + (b - d) * wr;
        }

        reverse_steps = vorbis_ilog(n - 1);
        for( int step = 0; step < reverse_steps - 3; step++ )
        {
            int block = n >> (step + 2);
            int stride = 8 << step;
            for( int group = 0; group < 2 << step; group++ )
            {
                int base_a = n - block * 2 * group;
                int base_b = n - (group * 2 + 1) * block;
                for( int i = 0; i < n >> (step + 4); i++ )
                {
                    int offset = i * 4;
                    float a = spectrum[base_a - 1 - offset];
                    float b = spectrum[base_a - 3 - offset];
                    float c = spectrum[base_b - 1 - offset];
                    float d = spectrum[base_b - 3 - offset];
                    float wr = tables->twiddle_a[stride * i];
                    float wi = tables->twiddle_a[stride * i + 1];
                    spectrum[base_a - 1 - offset] = a + c;
                    spectrum[base_a - 3 - offset] = b + d;
                    spectrum[base_b - 1 - offset] = (a - c) * wr - (b - d) * wi;
                    spectrum[base_b - 3 - offset] = (a - c) * wi + (b - d) * wr;
                }
            }
        }

        for( int i = 1; i < eighth - 1; i++ )
        {
            int target = tables->bit_reverse[i];
            if( i < target )
            {
                int a = i * 8;
                int b = target * 8;
                for( int k = 1; k < 8; k += 2 )
                {
                    float swap = spectrum[a + k];
                    spectrum[a + k] = spectrum[b + k];
                    spectrum[b + k] = swap;
                }
            }
        }

        for( int i = 0; i < half; i++ )
            spectrum[i] = spectrum[i * 2 + 1];
        for( int i = 0; i < eighth; i++ )
        {
            spectrum[n - 1 - i * 2] = spectrum[i * 4];
            spectrum[n - 2 - i * 2] = spectrum[i * 4 + 1];
            spectrum[n - quarter - 1 - i * 2] = spectrum[i * 4 + 2];
            spectrum[n - quarter - 2 - i * 2] = spectrum[i * 4 + 3];
        }
        for( int i = 0; i < eighth; i++ )
        {
            float wr = tables->twiddle_c[i * 2];
            float wi = tables->twiddle_c[i * 2 + 1];
            float a = spectrum[i * 2 + half];
            float b = spectrum[i * 2 + half + 1];
            float c = spectrum[n - 2 - i * 2];
            float d = spectrum[n - 1 - i * 2];
            float t0 = (a - c) * wi + (b + d) * wr;
            float t1;
            spectrum[i * 2 + half] = (a + c + t0) * 0.5f;
            spectrum[n - 2 - i * 2] = (a + c - t0) * 0.5f;
            t1 = (b + d) * wi - (a - c) * wr;
            spectrum[i * 2 + half + 1] = (b - d + t1) * 0.5f;
            spectrum[n - 1 - i * 2] = (-b + d + t1) * 0.5f;
        }
        for( int i = 0; i < quarter; i++ )
        {
            spectrum[i] = tables->twiddle_b[i * 2] * spectrum[i * 2 + half] +
                          tables->twiddle_b[i * 2 + 1] * spectrum[i * 2 + 1 + half];
            spectrum[half - 1 - i] = spectrum[i * 2 + half] * tables->twiddle_b[i * 2 + 1] -
                                     tables->twiddle_b[i * 2] * spectrum[i * 2 + 1 + half];
        }
        for( int i = 0; i < quarter; i++ )
            spectrum[n - quarter + i] = -spectrum[i];
        for( int i = 0; i < quarter; i++ )
            spectrum[i] = spectrum[quarter + i];
        for( int i = 0; i < quarter; i++ )
            spectrum[quarter + i] = -spectrum[quarter - i - 1];
        for( int i = 0; i < quarter; i++ )
            spectrum[half + i] = spectrum[n - i - 1];

        for( int i = left_start; i < left_end; i++ )
        {
            float t = (float)sin(((double)(i - left_start) + 0.5) / (double)left_window * 0.5 * M_PI);
            spectrum[i] *= (float)sin((double)t * (M_PI / 2.0) * (double)t);
        }
        for( int i = right_start; i < right_end; i++ )
        {
            float t = (float)sin(
                ((double)(i - right_start) + 0.5) / (double)right_window * 0.5 * M_PI + M_PI / 2.0);
            spectrum[i] *= (float)sin((double)t * (M_PI / 2.0) * (double)t);
        }
    }

    if( stream->previous_size > 0 )
    {
        produced = (stream->previous_size + n) >> 2;
        if( produced > out_capacity )
            return -1;
        for( int i = 0; i < produced; i++ )
            out[i] = 0.0f;
        if( !stream->previous_empty )
        {
            for( int i = 0; i < stream->previous_right; i++ )
            {
                int index = (stream->previous_size >> 1) + i;
                if( i >= produced || index >= stream->previous_size )
                    break;
                out[i] += stream->previous[index];
            }
        }
        if( curve.present )
        {
            for( int i = left_start; i < half; i++ )
            {
                int index = produced - half + i;
                if( index < 0 || index >= produced )
                    continue;
                out[index] += spectrum[i];
            }
        }
    }

    {
        float* swap = stream->previous;
        stream->previous = stream->work;
        stream->work = swap;
    }
    stream->previous_size = n;
    stream->previous_right = right_end - half;
    stream->previous_empty = !curve.present;
    return produced;
}

/* ---- sample decode ------------------------------------------------------- */

struct RSCache_AudioSample*
RSCache_AudioSampleNew(
    int sample_rate,
    int sample_count)
{
    struct RSCache_AudioSample* sample;

    if( sample_count < 0 )
        return NULL;
    sample = calloc(1, sizeof(*sample));
    if( !sample )
        return NULL;
    sample->sample_rate = sample_rate;
    sample->sample_count = sample_count;
    sample->loop_end = sample_count;
    sample->samples = calloc((size_t)(sample_count > 0 ? sample_count : 1), sizeof(int16_t));
    if( !sample->samples )
    {
        free(sample);
        return NULL;
    }
    return sample;
}

void
RSCache_AudioSampleFree(struct RSCache_AudioSample* sample)
{
    if( !sample )
        return;
    free(sample->samples);
    free(sample);
}

static int
read_be32(
    const uint8_t* data,
    int offset)
{
    return (int)(((uint32_t)data[offset] << 24) | ((uint32_t)data[offset + 1] << 16) |
                 ((uint32_t)data[offset + 2] << 8) | (uint32_t)data[offset + 3]);
}

struct RSCache_AudioSample*
RSCache_VorbisSampleNewDecode(
    const struct RSCache_VorbisSetup* setup,
    const char* data,
    int data_size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    struct RSCache_AudioSample* sample = NULL;
    struct vorbis_stream stream;
    float* out = NULL;
    int out_capacity;
    int cursor = 0;
    int sample_rate;
    int sample_count;
    int loop_start;
    int loop_end;
    bool loop = false;
    int packet_count;
    int written = 0;
    int max_partitions = 0;

    memset(&stream, 0, sizeof(stream));
    if( !setup || !data || data_size < 20 )
        return NULL;

    sample_rate = read_be32(bytes, 0);
    sample_count = read_be32(bytes, 4);
    loop_start = read_be32(bytes, 8);
    loop_end = read_be32(bytes, 12);
    if( loop_end < 0 )
    {
        loop_end = ~loop_end;
        loop = true;
    }
    packet_count = read_be32(bytes, 16);
    cursor = 20;

    if( sample_rate <= 0 || sample_count < 0 || sample_count > (1 << 27) || packet_count < 0 ||
        packet_count > (1 << 22) )
        return NULL;

    sample = RSCache_AudioSampleNew(sample_rate, sample_count);
    if( !sample )
        return NULL;
    sample->loop_start = loop_start;
    sample->loop_end = loop_end;
    sample->loop = loop;

    stream.setup = setup;
    stream.work = calloc((size_t)setup->blocksize_long, sizeof(float));
    stream.previous = calloc((size_t)setup->blocksize_long, sizeof(float));
    for( int i = 0; i < setup->residue_count; i++ )
    {
        const struct vorbis_residue* residue = &setup->residues[i];
        int span = residue->end - residue->begin;
        int partitions = span > 0 ? span / residue->partition_size : 0;
        if( partitions > max_partitions )
            max_partitions = partitions;
    }
    stream.classifications_size = max_partitions > 0 ? max_partitions : 1;
    stream.classifications = calloc((size_t)stream.classifications_size, sizeof(int));
    out_capacity = setup->blocksize_long;
    out = calloc((size_t)out_capacity, sizeof(float));
    if( !stream.work || !stream.previous || !stream.classifications || !out )
        goto fail;

    for( int i = 0; i < packet_count; i++ )
    {
        int length = 0;
        int chunk;
        int produced;

        do
        {
            if( cursor >= data_size )
                goto fail;
            chunk = bytes[cursor++];
            length += chunk;
        } while( chunk >= 255 );
        if( length < 0 || cursor + length > data_size )
            goto fail;

        produced = vorbis_decode_packet(&stream, bytes + cursor, length, out, out_capacity);
        cursor += length;
        if( produced < 0 )
            goto fail;
        for( int j = 0; j < produced && written < sample_count; j++ )
        {
            int value = (int)(out[j] * 32768.0f);
            if( value > 32767 )
                value = 32767;
            if( value < -32768 )
                value = -32768;
            sample->samples[written++] = (int16_t)value;
        }
    }

    free(stream.work);
    free(stream.previous);
    free(stream.classifications);
    free(out);
    return sample;

fail:
    free(stream.work);
    free(stream.previous);
    free(stream.classifications);
    free(out);
    RSCache_AudioSampleFree(sample);
    return NULL;
}

struct RSCache_AudioSample*
RSCache_VorbisEffectSampleNewDecode(
    const char* data,
    int data_size)
{
    struct RSCache_VorbisSetup* setup;
    struct RSCache_AudioSample* sample;
    int setup_size;

    if( !data || data_size < 8 )
        return NULL;
    setup_size = read_be32((const uint8_t*)data, 0);
    if( setup_size <= 0 || setup_size > data_size - 4 )
        return NULL;
    setup = RSCache_VorbisSetupNewDecode(data + 4, setup_size);
    if( !setup )
        return NULL;
    sample =
        RSCache_VorbisSampleNewDecode(setup, data + 4 + setup_size, data_size - 4 - setup_size);
    RSCache_VorbisSetupFree(setup);
    return sample;
}
