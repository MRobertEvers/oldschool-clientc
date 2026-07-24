/*
 * bzip2 compressor.
 *
 * Written to pair with the bunzip decompressor in bzip.c, which is
 * decompress-only. A cache needs this because the dat1 jagfile format mandates
 * bzip2 on both of its paths — whole-archive or per-file — with no "stored"
 * escape hatch, so a jagfile cannot be authored without it.
 *
 * ## Scope and priorities
 *
 * Correctness and clarity over ratio and speed. This runs in offline packing
 * tools, not in the client's frame loop, so the block sort is a straightforward
 * comparison sort rather than bzip2's tuned mainSort/fallbackSort pair. Output is
 * a valid bzip2 stream that any decompressor accepts; it is not required to be
 * byte-identical to what the reference `bzip2` binary would emit, and generally
 * is not, because Huffman table selection and refinement are heuristic.
 *
 * Verified two ways (see 3rd/rscache/test/test_compression.c):
 *   1. Round trip through this tree's own bunzip decoder.
 *   2. Interoperability with the system `bzip2 -d` binary.
 *
 * ## Format, briefly
 *
 * A stream is:  'B' 'Z' 'h' <level '1'..'9'>  then one or more blocks, then the
 * end-of-stream footer. Everything after the 4-byte header is a *bit* stream,
 * MSB-first, and blocks are not byte-aligned — hence the bit writer below.
 *
 * Each block carries, in order:
 *   - 48-bit magic 0x314159265359
 *   - 32-bit block CRC (bzip2's unreflected CRC-32, see RSCache_Crc32Bzip2)
 *   - 1 bit "randomised" (deprecated; always 0 here)
 *   - 24-bit BWT primary index
 *   - the symbol-map: a 16-bit coarse mask plus a 16-bit fine mask per set bit
 *   - 3-bit table count (2..6), 15-bit selector count
 *   - selectors, MTF-encoded, unary
 *   - each table's code lengths as a delta walk
 *   - the payload, Huffman coded in groups of 50 symbols
 *
 * The data pipeline into that is: RLE1 -> BWT -> MTF -> RLE2 (RUNA/RUNB).
 */

#include "bzip.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BZIP_ENCODE_GROUP_SIZE 50
#define BZIP_ENCODE_MAX_TABLES 6
#define BZIP_ENCODE_MIN_TABLES 2
#define BZIP_ENCODE_MAX_SELECTORS 18002
#define BZIP_ENCODE_MAX_CODE_LEN 20
/* bzip2 caps code lengths at 17 bits when *writing* even though the decoder
 * tolerates 20; staying at 17 keeps us inside what every decoder accepts. */
#define BZIP_ENCODE_MAX_WRITE_CODE_LEN 17
#define BZIP_ENCODE_REFINE_PASSES 4

/* Level 1 == 100000 bytes per block. The cache format uses level 1 only. */
#define BZIP_ENCODE_BLOCK_BASE 100000

/* ------------------------------------------------------------------- CRC --- */

static uint32_t bzip_crc_table[256];
static bool bzip_crc_table_ready = false;

uint32_t
bzip_crc32_update(
    uint32_t crc,
    const uint8_t* data,
    uint32_t length)
{
    if( !bzip_crc_table_ready )
    {
        for( uint32_t i = 0; i < 256; i++ )
        {
            uint32_t reg = i << 24;
            for( int bit = 0; bit < 8; bit++ )
                reg = (reg & 0x80000000u) ? ((reg << 1) ^ 0x04C11DB7u) : (reg << 1);
            bzip_crc_table[i] = reg;
        }
        bzip_crc_table_ready = true;
    }

    for( uint32_t i = 0; i < length; i++ )
        crc = (crc << 8) ^ bzip_crc_table[((crc >> 24) ^ data[i]) & 0xffu];
    return crc;
}

struct bit_writer
{
    uint8_t* out;
    uint32_t capacity;
    uint32_t bytes;
    uint32_t live; /* bits currently buffered in `acc` */
    uint32_t acc;  /* left-aligned bit accumulator */
    bool overflow;
};

static void
bw_init(
    struct bit_writer* writer,
    uint8_t* out,
    uint32_t capacity)
{
    writer->out = out;
    writer->capacity = capacity;
    writer->bytes = 0;
    writer->live = 0;
    writer->acc = 0;
    writer->overflow = false;
}

static void
bw_put(
    struct bit_writer* writer,
    uint32_t value,
    int count)
{
    /* MSB-first: the top `count` bits of `value` go out in order. */
    for( int i = count - 1; i >= 0; i-- )
    {
        writer->acc = (writer->acc << 1) | ((value >> i) & 1u);
        writer->live++;
        if( writer->live == 8 )
        {
            if( writer->bytes >= writer->capacity )
            {
                writer->overflow = true;
                writer->live = 0;
                writer->acc = 0;
                return;
            }
            writer->out[writer->bytes++] = (uint8_t)(writer->acc & 0xff);
            writer->live = 0;
            writer->acc = 0;
        }
    }
}

static void
bw_flush(struct bit_writer* writer)
{
    /* Pad the final partial byte with zeros on the right. */
    while( writer->live != 0 )
        bw_put(writer, 0, 1);
}

/* ------------------------------------------------------------------ RLE1 --- */

/*
 * bzip2's first RLE: a run of 4..255 identical bytes becomes the four bytes
 * followed by a length byte holding (run - 4). Runs of exactly 4 still get a
 * trailing 0, which is what makes the transform reversible.
 */
static uint32_t
rle1_encode(
    const uint8_t* in,
    uint32_t in_len,
    uint8_t* out,
    uint32_t out_capacity,
    uint32_t* consumed)
{
    uint32_t src = 0;
    uint32_t dst = 0;

    while( src < in_len )
    {
        uint8_t byte = in[src];
        uint32_t run = 1;
        while( src + run < in_len && in[src + run] == byte && run < 255 + 4 )
            run++;

        if( run < 4 )
        {
            if( dst + run > out_capacity )
                break;
            for( uint32_t i = 0; i < run; i++ )
                out[dst++] = byte;
        }
        else
        {
            if( dst + 5 > out_capacity )
                break;
            out[dst++] = byte;
            out[dst++] = byte;
            out[dst++] = byte;
            out[dst++] = byte;
            out[dst++] = (uint8_t)(run - 4);
        }
        src += run;
    }

    *consumed = src;
    return dst;
}

/* ------------------------------------------------------------------- BWT --- */

struct bwt_context
{
    const uint8_t* data;
    uint32_t length;
};

static struct bwt_context bwt_ctx;

/*
 * Compare two rotations of the block.
 *
 * Plain O(n) worst-case comparison. bzip2 uses a quadrant-assisted radix sort to
 * avoid this; here the simplicity is worth more than the speed, because the
 * inputs are jagfile config archives (tens of KB) packed offline.
 */
static int
bwt_compare(
    const void* lhs,
    const void* rhs)
{
    uint32_t a = *(const uint32_t*)lhs;
    uint32_t b = *(const uint32_t*)rhs;
    if( a == b )
        return 0;

    const uint8_t* data = bwt_ctx.data;
    uint32_t length = bwt_ctx.length;

    for( uint32_t i = 0; i < length; i++ )
    {
        uint8_t ca = data[a];
        uint8_t cb = data[b];
        if( ca != cb )
            return ca < cb ? -1 : 1;
        if( ++a == length )
            a = 0;
        if( ++b == length )
            b = 0;
    }
    return 0;
}

/**
 * Burrows-Wheeler transform.
 *
 * Writes the last column to `out` and returns the row index of the original
 * string (bzip2's "origPtr"), or UINT32_MAX on allocation failure.
 */
static uint32_t
bwt_transform(
    const uint8_t* in,
    uint32_t length,
    uint8_t* out)
{
    uint32_t* rotations = malloc((size_t)length * sizeof(uint32_t));
    if( !rotations )
        return UINT32_MAX;

    for( uint32_t i = 0; i < length; i++ )
        rotations[i] = i;

    bwt_ctx.data = in;
    bwt_ctx.length = length;
    qsort(rotations, length, sizeof(uint32_t), bwt_compare);

    uint32_t primary = UINT32_MAX;
    for( uint32_t i = 0; i < length; i++ )
    {
        if( rotations[i] == 0 )
            primary = i;
        /* Last column: the byte preceding this rotation's start. */
        out[i] = in[(rotations[i] + length - 1) % length];
    }

    free(rotations);
    return primary;
}

/* -------------------------------------------------------- MTF + RLE2 ------- */

/* RUNA/RUNB encode a zero run in a bijective base-2 numeral system. */
#define SYM_RUNA 0
#define SYM_RUNB 1

/**
 * Move-to-front over the bytes present in the block, with zero runs folded into
 * RUNA/RUNB pairs.
 *
 * Emits into `symbols` and returns the symbol count. `alpha_size` receives the
 * alphabet size, which is (distinct bytes) + 2 run symbols + 1 end-of-block.
 */
static uint32_t
mtf_rle2_encode(
    const uint8_t* bwt,
    uint32_t length,
    const uint8_t* present,
    uint16_t* symbols,
    int* alpha_size)
{
    uint8_t mtf[256];
    int used = 0;
    for( int i = 0; i < 256; i++ )
    {
        if( present[i] )
            mtf[used++] = (uint8_t)i;
    }

    /*
     * Alphabet layout, which the decoder derives independently from the symbol
     * map — so getting the size wrong desyncs the code-length walk and the whole
     * block fails:
     *
     *   0            RUNA
     *   1            RUNB
     *   2 .. used    literals (MTF position p maps to symbol p + 1)
     *   used + 1     end of block
     *   alphaSize    = used + 2
     */
    *alpha_size = used + 2;

    uint32_t count = 0;
    uint32_t zeros = 0;

    for( uint32_t i = 0; i < length; i++ )
    {
        uint8_t byte = bwt[i];

        int position = 0;
        while( mtf[position] != byte )
            position++;

        /* Move to front. */
        if( position != 0 )
        {
            memmove(mtf + 1, mtf, (size_t)position);
            mtf[0] = byte;
        }

        if( position == 0 )
        {
            zeros++;
            continue;
        }

        /* Flush the pending zero run before emitting a literal. */
        if( zeros != 0 )
        {
            uint32_t run = zeros;
            zeros = 0;
            /* Bijective base 2: repeatedly take (run+1)'s low bit. */
            run++;
            while( run > 1 )
            {
                symbols[count++] = (run & 1u) ? SYM_RUNB : SYM_RUNA;
                run >>= 1;
            }
        }

        /* Literals occupy symbols 2..used, since 0 and 1 are the run codes. */
        symbols[count++] = (uint16_t)(position + 1);
    }

    if( zeros != 0 )
    {
        uint32_t run = zeros + 1;
        while( run > 1 )
        {
            symbols[count++] = (run & 1u) ? SYM_RUNB : SYM_RUNA;
            run >>= 1;
        }
    }

    /* End of block is the last symbol in the alphabet, not one past it. */
    symbols[count++] = (uint16_t)(*alpha_size - 1);

    return count;
}

/* ---------------------------------------------------------------- Huffman -- */

/**
 * Build code lengths for `freq` limited to `max_len` bits.
 *
 * Package-merge would be exact; this uses bzip2's own approach — build a Huffman
 * tree, and if it exceeds the limit, flatten the frequencies and retry. That
 * terminates because flattening strictly reduces the spread.
 */
static void
huffman_code_lengths(
    const uint32_t* freq,
    int alpha_size,
    int max_len,
    uint8_t* lengths)
{
    /* Weights are packed as (weight << 8) | depth so ties break on depth, and a
     * floor of 1 keeps unused symbols in the tree — bzip2 does the same, because
     * every symbol needs a code even if it never occurs. */
    uint64_t* node_weight = malloc(sizeof(uint64_t) * (size_t)(alpha_size * 2 + 2));
    int* node_parent = malloc(sizeof(int) * (size_t)(alpha_size * 2 + 2));
    if( !node_weight || !node_parent )
    {
        /* Degrade to a flat code rather than emitting nothing: still a valid
         * stream, just larger. */
        for( int i = 0; i < alpha_size; i++ )
            lengths[i] = (uint8_t)(max_len < 8 ? max_len : 8);
        free(node_weight);
        free(node_parent);
        return;
    }

    uint32_t scaled[256 + 2];
    for( int i = 0; i < alpha_size; i++ )
        scaled[i] = freq[i] == 0 ? 1u : freq[i];

    for( ;; )
    {
        int node_count = alpha_size;
        for( int i = 0; i < alpha_size; i++ )
        {
            node_weight[i] = ((uint64_t)scaled[i] << 8);
            node_parent[i] = -1;
        }

        /* Repeatedly merge the two lightest live nodes. O(n^2) over an alphabet
         * of at most 258 symbols, which is immaterial here. */
        int live = alpha_size;
        while( live > 1 )
        {
            int best1 = -1;
            int best2 = -1;
            for( int i = 0; i < node_count; i++ )
            {
                if( node_parent[i] != -1 )
                    continue;
                if( best1 == -1 || node_weight[i] < node_weight[best1] )
                {
                    best2 = best1;
                    best1 = i;
                }
                else if( best2 == -1 || node_weight[i] < node_weight[best2] )
                {
                    best2 = i;
                }
            }

            int merged = node_count++;
            node_weight[merged] =
                ((node_weight[best1] & ~0xffULL) + (node_weight[best2] & ~0xffULL)) |
                (1ULL + ((node_weight[best1] & 0xffULL) > (node_weight[best2] & 0xffULL)
                             ? (node_weight[best1] & 0xffULL)
                             : (node_weight[best2] & 0xffULL)));
            node_parent[merged] = -1;
            node_parent[best1] = merged;
            node_parent[best2] = merged;
            live--;
        }

        /* Depth of each leaf is its distance to the root. */
        int longest = 0;
        for( int i = 0; i < alpha_size; i++ )
        {
            int depth = 0;
            int node = i;
            while( node_parent[node] != -1 )
            {
                node = node_parent[node];
                depth++;
            }
            lengths[i] = (uint8_t)(depth == 0 ? 1 : depth);
            if( lengths[i] > longest )
                longest = lengths[i];
        }

        if( longest <= max_len )
            break;

        /* Too deep: compress the dynamic range and try again. */
        for( int i = 0; i < alpha_size; i++ )
        {
            uint32_t value = (scaled[i] >> 1) + 1;
            scaled[i] = value;
        }
    }

    free(node_weight);
    free(node_parent);
}

/** Canonical codes for a set of lengths, in bzip2's order (shorter first). */
static void
huffman_assign_codes(
    const uint8_t* lengths,
    int alpha_size,
    uint32_t* codes)
{
    uint32_t code = 0;
    for( int length = 1; length <= BZIP_ENCODE_MAX_CODE_LEN; length++ )
    {
        for( int i = 0; i < alpha_size; i++ )
        {
            if( lengths[i] == length )
                codes[i] = code++;
        }
        code <<= 1;
    }
}

/* ----------------------------------------------------------------- block --- */

struct block_encoder
{
    uint8_t* rle1;      /* RLE1 output, the BWT input */
    uint8_t* bwt;       /* BWT last column */
    uint16_t* symbols;  /* MTF + RLE2 symbol stream */
    uint32_t capacity;
};

/**
 * Choose Huffman tables and encode one block's symbol stream.
 *
 * bzip2 allows 2..6 tables, each group of 50 symbols selecting one. Tables are
 * seeded by splitting the symbol stream into frequency-balanced spans, then
 * refined: assign each group to whichever table codes it cheapest, recompute the
 * tables from their assigned groups, repeat.
 */
static void
encode_block_payload(
    struct bit_writer* writer,
    const uint16_t* symbols,
    uint32_t symbol_count,
    int alpha_size)
{
    uint32_t group_count = (symbol_count + BZIP_ENCODE_GROUP_SIZE - 1) / BZIP_ENCODE_GROUP_SIZE;

    /* bzip2 picks the table count from the symbol volume. */
    int table_count;
    if( symbol_count < 200 )
        table_count = 2;
    else if( symbol_count < 600 )
        table_count = 3;
    else if( symbol_count < 1200 )
        table_count = 4;
    else if( symbol_count < 2400 )
        table_count = 5;
    else
        table_count = 6;

    static uint8_t lengths[BZIP_ENCODE_MAX_TABLES][258];
    static uint32_t codes[BZIP_ENCODE_MAX_TABLES][258];
    static uint32_t freq[BZIP_ENCODE_MAX_TABLES][258];
    uint8_t* selectors = malloc(group_count ? group_count : 1);
    if( !selectors )
        return;

    /* Seed: split the alphabet's total frequency into `table_count` equal shares
     * and give each table a contiguous span of symbols, which is bzip2's own
     * initialisation. */
    {
        uint32_t total_freq[258];
        memset(total_freq, 0, sizeof(uint32_t) * (size_t)alpha_size);
        for( uint32_t i = 0; i < symbol_count; i++ )
            total_freq[symbols[i]]++;

        uint32_t total = 0;
        for( int i = 0; i < alpha_size; i++ )
            total += total_freq[i];

        int symbol = 0;
        uint32_t remaining = total;
        for( int table = 0; table < table_count; table++ )
        {
            uint32_t target = remaining / (uint32_t)(table_count - table);
            uint32_t accumulated = 0;
            int span_start = symbol;
            while( symbol < alpha_size && (accumulated < target || symbol == span_start) )
            {
                accumulated += total_freq[symbol];
                symbol++;
            }
            remaining -= accumulated;

            for( int i = 0; i < alpha_size; i++ )
                lengths[table][i] = (i >= span_start && i < symbol) ? 1 : 15;
        }
        /* Any tail the loop did not reach belongs to the last table. */
        for( int i = symbol; i < alpha_size; i++ )
            lengths[table_count - 1][i] = 1;
    }

    for( int pass = 0; pass < BZIP_ENCODE_REFINE_PASSES; pass++ )
    {
        for( int table = 0; table < table_count; table++ )
            memset(freq[table], 0, sizeof(uint32_t) * (size_t)alpha_size);

        /* Assign each group to its cheapest table under the current lengths. */
        for( uint32_t group = 0; group < group_count; group++ )
        {
            uint32_t start = group * BZIP_ENCODE_GROUP_SIZE;
            uint32_t end = start + BZIP_ENCODE_GROUP_SIZE;
            if( end > symbol_count )
                end = symbol_count;

            int best_table = 0;
            uint32_t best_cost = UINT32_MAX;
            for( int table = 0; table < table_count; table++ )
            {
                uint32_t cost = 0;
                for( uint32_t i = start; i < end; i++ )
                    cost += lengths[table][symbols[i]];
                if( cost < best_cost )
                {
                    best_cost = cost;
                    best_table = table;
                }
            }

            selectors[group] = (uint8_t)best_table;
            for( uint32_t i = start; i < end; i++ )
                freq[best_table][symbols[i]]++;
        }

        /* Recompute each table from the groups that chose it. */
        for( int table = 0; table < table_count; table++ )
            huffman_code_lengths(
                freq[table], alpha_size, BZIP_ENCODE_MAX_WRITE_CODE_LEN, lengths[table]);
    }

    for( int table = 0; table < table_count; table++ )
        huffman_assign_codes(lengths[table], alpha_size, codes[table]);

    bw_put(writer, (uint32_t)table_count, 3);
    bw_put(writer, group_count, 15);

    /* Selectors, MTF-encoded over table indices then written in unary. */
    {
        uint8_t table_mtf[BZIP_ENCODE_MAX_TABLES];
        for( int i = 0; i < table_count; i++ )
            table_mtf[i] = (uint8_t)i;

        for( uint32_t group = 0; group < group_count; group++ )
        {
            uint8_t wanted = selectors[group];
            int position = 0;
            while( table_mtf[position] != wanted )
                position++;
            if( position != 0 )
            {
                memmove(table_mtf + 1, table_mtf, (size_t)position);
                table_mtf[0] = wanted;
            }
            for( int i = 0; i < position; i++ )
                bw_put(writer, 1, 1);
            bw_put(writer, 0, 1);
        }
    }

    /* Code lengths as a delta walk: start at the first length, then +1/-1 steps
     * (2 bits each) to reach the next, terminated by a 0 bit. */
    for( int table = 0; table < table_count; table++ )
    {
        int current = lengths[table][0];
        bw_put(writer, (uint32_t)current, 5);
        for( int i = 0; i < alpha_size; i++ )
        {
            int target = lengths[table][i];
            while( current < target )
            {
                bw_put(writer, 2, 2); /* 10 = increment */
                current++;
            }
            while( current > target )
            {
                bw_put(writer, 3, 2); /* 11 = decrement */
                current--;
            }
            bw_put(writer, 0, 1);
        }
    }

    /* Payload. */
    for( uint32_t group = 0; group < group_count; group++ )
    {
        uint32_t start = group * BZIP_ENCODE_GROUP_SIZE;
        uint32_t end = start + BZIP_ENCODE_GROUP_SIZE;
        if( end > symbol_count )
            end = symbol_count;

        int table = selectors[group];
        for( uint32_t i = start; i < end; i++ )
            bw_put(writer, codes[table][symbols[i]], lengths[table][symbols[i]]);
    }

    free(selectors);
}

uint32_t
bzip_compress(
    uint8_t* out,
    uint32_t out_capacity,
    const uint8_t* data,
    uint32_t data_length,
    int level)
{
    if( !out || (!data && data_length != 0) )
        return 0;
    if( level < 1 )
        level = 1;
    if( level > 9 )
        level = 9;

    uint32_t block_limit = (uint32_t)level * BZIP_ENCODE_BLOCK_BASE;
    /* bzip2 reserves 19 bytes of slack at the end of a block so RLE1 never
     * straddles the limit mid-run. */
    if( block_limit > 19 )
        block_limit -= 19;

    struct bit_writer writer;
    bw_init(&writer, out, out_capacity);

    bw_put(&writer, 'B', 8);
    bw_put(&writer, 'Z', 8);
    bw_put(&writer, 'h', 8);
    bw_put(&writer, (uint32_t)('0' + level), 8);

    uint8_t* rle1 = malloc(block_limit + 64);
    uint8_t* bwt = malloc(block_limit + 64);
    /* Each input byte can produce at most one symbol, plus the run codes and the
     * end-of-block marker. */
    uint16_t* symbols = malloc(sizeof(uint16_t) * (size_t)(block_limit + 64) * 2);
    if( !rle1 || !bwt || !symbols )
    {
        free(rle1);
        free(bwt);
        free(symbols);
        return 0;
    }

    uint32_t stream_crc = 0;
    uint32_t offset = 0;
    bool wrote_block = false;

    while( offset < data_length || !wrote_block )
    {
        uint32_t available = data_length - offset;
        uint32_t take = available;

        uint32_t consumed = 0;
        uint32_t rle1_len = 0;
        if( take > 0 )
        {
            /* RLE1 decides how much input actually fits, so feed it a generous
             * slice and let it report what it consumed. */
            rle1_len = rle1_encode(data + offset, take, rle1, block_limit, &consumed);
        }

        /* An empty input still needs one... no: bzip2 emits zero blocks for empty
         * input, only the header and footer. */
        if( rle1_len == 0 )
            break;

        uint32_t block_crc = RSCACHE_BZIP_CRC_INIT;
        block_crc = bzip_crc32_update(block_crc, data + offset, consumed);
        block_crc = ~block_crc;

        stream_crc = (stream_crc << 1) | (stream_crc >> 31);
        stream_crc ^= block_crc;

        uint32_t primary = bwt_transform(rle1, rle1_len, bwt);
        if( primary == UINT32_MAX )
            break;

        uint8_t present[256] = { 0 };
        for( uint32_t i = 0; i < rle1_len; i++ )
            present[bwt[i]] = 1;

        int alpha_size = 0;
        uint32_t symbol_count = mtf_rle2_encode(bwt, rle1_len, present, symbols, &alpha_size);

        /* Block header. */
        bw_put(&writer, 0x314159u, 24);
        bw_put(&writer, 0x265359u, 24);
        bw_put(&writer, block_crc, 32);
        bw_put(&writer, 0, 1); /* not randomised */
        bw_put(&writer, primary, 24);

        /* Symbol map: coarse 16-bit mask of which 16-byte ranges are used, then a
         * 16-bit fine mask for each used range. */
        {
            uint16_t coarse = 0;
            for( int high = 0; high < 16; high++ )
            {
                for( int low = 0; low < 16; low++ )
                {
                    if( present[high * 16 + low] )
                    {
                        coarse |= (uint16_t)(1u << (15 - high));
                        break;
                    }
                }
            }
            bw_put(&writer, coarse, 16);

            for( int high = 0; high < 16; high++ )
            {
                if( !(coarse & (1u << (15 - high))) )
                    continue;
                uint16_t fine = 0;
                for( int low = 0; low < 16; low++ )
                {
                    if( present[high * 16 + low] )
                        fine |= (uint16_t)(1u << (15 - low));
                }
                bw_put(&writer, fine, 16);
            }
        }

        encode_block_payload(&writer, symbols, symbol_count, alpha_size);

        offset += consumed;
        wrote_block = true;

        if( writer.overflow )
            break;
        if( consumed == 0 )
            break; /* no forward progress; refuse to spin */
    }

    /* End-of-stream footer: magic, combined CRC, then pad to a byte. */
    bw_put(&writer, 0x177245u, 24);
    bw_put(&writer, 0x385090u, 24);
    bw_put(&writer, stream_crc, 32);
    bw_flush(&writer);

    free(rle1);
    free(bwt);
    free(symbols);

    if( writer.overflow )
        return 0;
    return writer.bytes;
}

uint32_t
bzip_compress_bound(uint32_t data_length)
{
    /* Worst case for bzip2 is a small expansion; the generous margin here covers
     * per-block headers, tables and the RLE1 blow-up on pathological input. */
    uint32_t blocks = data_length / BZIP_ENCODE_BLOCK_BASE + 1;
    return data_length + data_length / 50 + 1024 * blocks + 1024;
}
