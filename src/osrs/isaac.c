/*
 * Matches Client-TS/src/io/Isaac.ts (same sequence as nextInt / isaac_next).
 */

#include "isaac.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Isaac
{
    int count;

    uint32_t rsl[256];
    uint32_t mem[256];

    uint32_t a;
    uint32_t b;
    uint32_t c;
};

static void
isaac_scramble(struct Isaac* isaac)
{
    isaac->c++;
    isaac->b += isaac->c;

    for( int i = 0; i < 256; i++ )
    {
        uint32_t x = isaac->mem[i];

        switch( i & 3 )
        {
        case 0:
            isaac->a ^= isaac->a << 13;
            break;
        case 1:
            isaac->a ^= (unsigned int)isaac->a >> 6;
            break;
        case 2:
            isaac->a ^= isaac->a << 2;
            break;
        case 3:
            isaac->a ^= (unsigned int)isaac->a >> 16;
            break;
        default:
            break;
        }

        isaac->a += isaac->mem[(i + 128) & 0xff];

        uint32_t y;
        isaac->mem[i] = y = isaac->mem[((unsigned int)x >> 2) & 0xff] + isaac->a + isaac->b;
        isaac->rsl[i] = isaac->b = isaac->mem[(((unsigned int)y >> 8) >> 2) & 0xff] + x;
    }
}

static void
isaac_init(struct Isaac* isaac)
{
    uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = 0x9e3779b9, d = 0x9e3779b9, e = 0x9e3779b9,
             f = 0x9e3779b9, g = 0x9e3779b9, h = 0x9e3779b9;

    for( int i = 0; i < 4; i++ )
    {
        a ^= b << 11;
        d += a;
        b += c;
        b ^= (unsigned int)c >> 2;
        e += b;
        c += d;
        c ^= d << 8;
        f += c;
        d += e;
        d ^= (unsigned int)e >> 16;
        g += d;
        e += f;
        e ^= f << 10;
        h += e;
        f += g;
        f ^= (unsigned int)g >> 4;
        a += f;
        g += h;
        g ^= h << 8;
        b += g;
        h += a;
        h ^= (unsigned int)a >> 9;
        c += h;
        a += b;
    }

    for( int i = 0; i < 256; i += 8 )
    {
        a += isaac->rsl[i];
        b += isaac->rsl[i + 1];
        c += isaac->rsl[i + 2];
        d += isaac->rsl[i + 3];
        e += isaac->rsl[i + 4];
        f += isaac->rsl[i + 5];
        g += isaac->rsl[i + 6];
        h += isaac->rsl[i + 7];

        a ^= b << 11;
        d += a;
        b += c;
        b ^= (unsigned int)c >> 2;
        e += b;
        c += d;
        c ^= d << 8;
        f += c;
        d += e;
        d ^= (unsigned int)e >> 16;
        g += d;
        e += f;
        e ^= f << 10;
        h += e;
        f += g;
        f ^= (unsigned int)g >> 4;
        a += f;
        g += h;
        g ^= h << 8;
        b += g;
        h += a;
        h ^= (unsigned int)a >> 9;
        c += h;
        a += b;

        isaac->mem[i] = a;
        isaac->mem[i + 1] = b;
        isaac->mem[i + 2] = c;
        isaac->mem[i + 3] = d;
        isaac->mem[i + 4] = e;
        isaac->mem[i + 5] = f;
        isaac->mem[i + 6] = g;
        isaac->mem[i + 7] = h;
    }

    for( int i = 0; i < 256; i += 8 )
    {
        a += isaac->mem[i];
        b += isaac->mem[i + 1];
        c += isaac->mem[i + 2];
        d += isaac->mem[i + 3];
        e += isaac->mem[i + 4];
        f += isaac->mem[i + 5];
        g += isaac->mem[i + 6];
        h += isaac->mem[i + 7];

        a ^= b << 11;
        d += a;
        b += c;
        b ^= (unsigned int)c >> 2;
        e += b;
        c += d;
        c ^= d << 8;
        f += c;
        d += e;
        d ^= (unsigned int)e >> 16;
        g += d;
        e += f;
        e ^= f << 10;
        h += e;
        f += g;
        f ^= (unsigned int)g >> 4;
        a += f;
        g += h;
        g ^= h << 8;
        b += g;
        h += a;
        h ^= (unsigned int)a >> 9;
        c += h;
        a += b;

        isaac->mem[i] = a;
        isaac->mem[i + 1] = b;
        isaac->mem[i + 2] = c;
        isaac->mem[i + 3] = d;
        isaac->mem[i + 4] = e;
        isaac->mem[i + 5] = f;
        isaac->mem[i + 6] = g;
        isaac->mem[i + 7] = h;
    }

    isaac_scramble(isaac);
    isaac->count = 256;
}

struct Isaac*
isaac_new(
    int* seed,
    int seed_length)
{
    struct Isaac* isaac = malloc(sizeof(struct Isaac));

    isaac_seed(isaac, seed, seed_length);

    return isaac;
}

void
isaac_seed(
    struct Isaac* isaac,
    int* seed,
    int seed_length)
{
    memset(isaac, 0, sizeof(struct Isaac));

    for( int i = 0; i < seed_length; i++ )
    {
        isaac->rsl[i] = seed[i];
    }

    isaac_init(isaac);
}

void
isaac_free(struct Isaac* isaac)
{
    free(isaac);
}

int
isaac_next(struct Isaac* isaac)
{
    assert(isaac);
    if( isaac->count-- == 0 )
    {
        isaac_scramble(isaac);
        isaac->count = 255;
    }

    int value = isaac->rsl[isaac->count];
    return value;
}

size_t
isaac_state_size(void)
{
    return sizeof(struct Isaac);
}

void
isaac_get_state(
    struct Isaac* isaac,
    void* buf)
{
    memcpy(buf, isaac, sizeof(struct Isaac));
}

struct Isaac*
isaac_from_state(const void* buf)
{
    struct Isaac* isaac = malloc(sizeof(struct Isaac));
    if( isaac )
        memcpy(isaac, buf, sizeof(struct Isaac));
    return isaac;
}

void
isaac_debug_print(struct Isaac const* isaac_c)
{
    struct Isaac const* i = isaac_c;
    if( !i )
    {
        fputs("[isaac_debug] (null)\n", stdout);
        return;
    }

    printf(
        "[isaac_debug] count=%d a=%" PRIx32 " b=%" PRIx32 " c=%" PRIx32
        " (isaac_next returns rsl[count] after count--)\n",
        i->count,
        i->a,
        i->b,
        i->c);

    fputs("[isaac_debug] rsl[0..31]:", stdout);
    for( int k = 0; k < 32; k++ )
        printf(" %" PRIx32, i->rsl[k]);
    fputc('\n', stdout);

    fputs("[isaac_debug] rsl[224..255]:", stdout);
    for( int k = 224; k < 256; k++ )
        printf(" %" PRIx32, i->rsl[k]);
    fputc('\n', stdout);

    fputs("[isaac_debug] mem[0..31]:", stdout);
    for( int k = 0; k < 32; k++ )
        printf(" %" PRIx32, i->mem[k]);
    fputc('\n', stdout);
}
