#include "dat2_config_spotanim.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * A count-prefixed (from, to) pair list: u8 count, then count × (u16, u16).
 *
 * Returns how many pairs were stored. Pairs past RSCACHE_SPOTANIM_COLOUR_SLOTS are
 * consumed and dropped so the stream stays aligned — the count is a byte and could
 * in principle exceed the six slots the struct carries.
 */
static int
decode_colour_list(
    int* from_slots,
    int* to_slots,
    struct RSCache_Buffer* buffer)
{
    int count = g1(buffer);
    int stored = 0;
    for( int i = 0; i < count; i++ )
    {
        int from = g2(buffer);
        int to = g2(buffer);
        if( i < RSCACHE_SPOTANIM_COLOUR_SLOTS )
        {
            from_slots[i] = from;
            to_slots[i] = to;
            stored = i + 1;
        }
    }
    return stored;
}

static void
init_dat2_spotanim(struct RSCache_Dat2ConfigSpotanim* s)
{
    memset(s, 0, sizeof(*s));
    s->model = 0;
    s->anim = -1;
    s->resizeh = 128;
    s->resizev = 128;
    s->angle = 0;
    s->ambient = 0;
    s->contrast = 0;
    s->terrain_mode = 0;
    s->terrain_height = -1;
}

static void
decode_dat2_spotanim(
    struct RSCache_Dat2ConfigSpotanim* s,
    struct RSCache_Buffer* buffer,
    int codec_version)
{
    bool rs2_727 = codec_version == RSCACHE_CODEC_SPOTANIM_RS2_727;
    while( true )
    {
        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
        {
            s->model = rs2_727 ? gbigsmart(buffer) : g2(buffer);
            s->model_opcode = 1;
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_MODEL);
        }
        else if( opcode == 2 )
        {
            s->anim = rs2_727 ? gbigsmart(buffer) : g2(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_ANIM);
        }
        else if( opcode == 3 )
        {
            s->model = g4(buffer);
            s->model_opcode = 3;
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_MODEL);
        }
        else if( opcode == 4 )
        {
            s->resizeh = g2(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_RESIZEH);
        }
        else if( opcode == 5 )
        {
            s->resizev = g2(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_RESIZEV);
        }
        else if( opcode == 6 )
        {
            s->angle = g2(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_ANGLE);
        }
        else if( opcode == 7 )
        {
            s->ambient = g1(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_AMBIENT);
        }
        else if( opcode == 8 )
        {
            s->contrast = g1(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_CONTRAST);
        }
        /* Opcode 9 is a debug/content name in OldSchool. Present in caches produced by
         * third-party packing tools (cache.osrs230 has "soul_wars" on spotanim 0)
         * and absent from stock Jagex ones (cache.jan2026 does not). It has to be
         * consumed either way or the rest of the record misaligns. Late RS2 instead
         * uses the same opcode as a payload-free terrain-conformance preset. */
        else if( opcode == 9 && !rs2_727 )
        {
            free(s->name);
            s->name = gcstring(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_NAME);
        }
        else if( opcode == 9 && rs2_727 )
        {
            s->terrain_mode = 3;
            s->terrain_height = 8224;
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_TERRAIN);
        }
        else if( opcode == 10 )
        {
            /* Payload-free flag. Not in RuneLite's SpotAnimLoader yet; verified
             * against cache.osrs239 where 39 records are `... 0a 00`. */
            s->unknown10 = true;
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_UNKNOWN10);
        }
        else if( opcode == 11 && rs2_727 )
        {
            s->terrain_mode = 1;
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_TERRAIN);
        }
        else if( opcode == 12 && rs2_727 )
        {
            s->terrain_mode = 4;
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_TERRAIN);
        }
        else if( opcode == 13 && rs2_727 )
        {
            s->terrain_mode = 5;
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_TERRAIN);
        }
        else if( opcode == 14 && rs2_727 )
        {
            s->terrain_mode = 2;
            s->terrain_height = g1(buffer) * 256;
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_TERRAIN);
        }
        else if( opcode == 15 && rs2_727 )
        {
            s->terrain_mode = 3;
            s->terrain_height = g2(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_TERRAIN);
        }
        else if( opcode == 16 && rs2_727 )
        {
            s->terrain_mode = 3;
            s->terrain_height = g4(buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_TERRAIN);
        }
        /* Recolour and retexture lists are **count-prefixed**: a u8 count, then
         * that many (from, to) u16 pairs. This is not the "one opcode per slot"
         * shape the ranges 40..79 suggest, and reading a single u16 here desynced
         * every modern record — the decoder used to bail on the byte that followed
         * with "Unrecognized opcode 210". Verified by exact consumption across
         * every cache in the repo. */
        else if( opcode == 40 )
        {
            s->recol_count = decode_colour_list(s->recol_s, s->recol_d, buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_RECOL);
        }
        else if( opcode == 41 )
        {
            s->retex_count = decode_colour_list(s->retex_s, s->retex_d, buffer);
            RSCache_PresenceSet(&s->present, RSCACHE_SPOTANIM_FIELD_RETEX);
        }
        else
        {
            printf("Unrecognized dat2 spotanim opcode %d\n", opcode);
            return;
        }
    }

    s->_consumed = (int)buffer->position;
}

int
RSCache_Dat2ConfigSpotanimCodecVersion(const struct RSCache* cache)
{
    return RSCache_CodecVersionOr(
        cache, RSCACHE_TYPE_SPOTANIM, RSCACHE_CODEC_SPOTANIM_OSRS);
}

uint32_t
RSCache_Dat2ConfigSpotanimEncodeRevision(
    int revision,
    const struct RSCache_Dat2ConfigSpotanim* spotanim,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(spotanim);
    assert(out);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

#define SPOTANIM_HAS(field) RSCache_PresenceHas(&spotanim->present, RSCACHE_SPOTANIM_FIELD_##field)

    if( SPOTANIM_HAS(MODEL) )
    {
        /* The width the record came in. A hand-built record picks: OSRS >= 237
         * packs model ids with opcode 3 even when they fit in a u16. */
        int opcode = spotanim->model_opcode;
        if( opcode == 0 )
            opcode = (revision >= 237 || spotanim->model < 0 || spotanim->model > 0xFFFF) ? 3 : 1;
        if( opcode == 3 )
        {
            p1(&buffer, 3);
            p4(&buffer, spotanim->model);
        }
        else
        {
            p1(&buffer, 1);
            p2(&buffer, spotanim->model);
        }
    }
    if( SPOTANIM_HAS(ANIM) )
    {
        p1(&buffer, 2);
        p2(&buffer, spotanim->anim);
    }
    if( SPOTANIM_HAS(RESIZEH) )
    {
        p1(&buffer, 4);
        p2(&buffer, spotanim->resizeh);
    }
    if( SPOTANIM_HAS(RESIZEV) )
    {
        p1(&buffer, 5);
        p2(&buffer, spotanim->resizev);
    }
    if( SPOTANIM_HAS(ANGLE) )
    {
        p1(&buffer, 6);
        p2(&buffer, spotanim->angle);
    }
    if( SPOTANIM_HAS(AMBIENT) )
    {
        p1(&buffer, 7);
        p1(&buffer, spotanim->ambient);
    }
    if( SPOTANIM_HAS(CONTRAST) )
    {
        p1(&buffer, 8);
        p1(&buffer, spotanim->contrast);
    }
    /* Opcode 9 precedes the colour lists in every record observed carrying it. */
    if( SPOTANIM_HAS(NAME) )
    {
        assert(spotanim->name);
        p1(&buffer, 9);
        pjstr(&buffer, spotanim->name, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    if( SPOTANIM_HAS(UNKNOWN10) )
        p1(&buffer, 10);
    /* RS2 terrain conformance has no OldSchool opcode; a porter consumes and
     * reports it, and nothing writes it back. */
    /* Count-prefixed lists, matching the decoder. */
    if( SPOTANIM_HAS(RECOL) )
    {
        p1(&buffer, 40);
        p1(&buffer, spotanim->recol_count);
        for( int i = 0; i < spotanim->recol_count; i++ )
        {
            p2(&buffer, spotanim->recol_s[i]);
            p2(&buffer, spotanim->recol_d[i]);
        }
    }
    if( SPOTANIM_HAS(RETEX) )
    {
        p1(&buffer, 41);
        p1(&buffer, spotanim->retex_count);
        for( int i = 0; i < spotanim->retex_count; i++ )
        {
            p2(&buffer, spotanim->retex_s[i]);
            p2(&buffer, spotanim->retex_d[i]);
        }
    }

#undef SPOTANIM_HAS

    p1(&buffer, 0);
    return buffer.position;
}

uint32_t
RSCache_Dat2ConfigSpotanimEncode(
    const struct RSCache_Dat2ConfigSpotanim* spotanim,
    uint8_t* out,
    uint32_t out_capacity)
{
    return RSCache_Dat2ConfigSpotanimEncodeRevision(0, spotanim, out, out_capacity);
}

struct RSCache_Dat2ConfigSpotanim*
RSCache_Dat2ConfigSpotanimNewDecode(int revision, char* data, int data_size)
{
    struct RSCache_Dat2ConfigSpotanim* s;
    struct RSCache_Buffer buffer;

    (void)revision;

    s = calloc(1, sizeof(*s));
    assert(s);
    RSCache_Dat2ConfigSpotanimDecodeInplace(s, data, data_size);
    (void)buffer;
    return s;
}

struct RSCache_Dat2ConfigSpotanim*
RSCache_Dat2ConfigSpotanimNewDecodeProfile(
    const struct RSCache* cache,
    char* data,
    int data_size)
{
    struct RSCache_Dat2ConfigSpotanim* s = calloc(1, sizeof(*s));
    assert(s);
    RSCache_Dat2ConfigSpotanimDecodeInplaceProfile(s, cache, data, data_size);
    return s;
}

void
RSCache_Dat2ConfigSpotanimInit(struct RSCache_Dat2ConfigSpotanim* spotanim)
{
    assert(spotanim);
    init_dat2_spotanim(spotanim);
}

void
RSCache_Dat2ConfigSpotanimDecodeInplace(
    struct RSCache_Dat2ConfigSpotanim* spotanim,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buffer;

    assert(spotanim);
    RSCache_Dat2ConfigSpotanimInit(spotanim);
    if( data_size <= 0 )
        return;
    assert(data);
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    decode_dat2_spotanim(spotanim, &buffer, RSCACHE_CODEC_SPOTANIM_OSRS);
}

void
RSCache_Dat2ConfigSpotanimDecodeInplaceProfile(
    struct RSCache_Dat2ConfigSpotanim* spotanim,
    const struct RSCache* cache,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buffer;

    assert(spotanim);
    RSCache_Dat2ConfigSpotanimInit(spotanim);
    if( data_size <= 0 )
        return;
    assert(data);
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    decode_dat2_spotanim(
        spotanim, &buffer, RSCache_Dat2ConfigSpotanimCodecVersion(cache));
}

void
RSCache_Dat2ConfigSpotanimFreeInplace(struct RSCache_Dat2ConfigSpotanim* spotanim)
{
    if( !spotanim )
        return;
    free(spotanim->name);
    spotanim->name = NULL;
}

void
RSCache_Dat2ConfigSpotanimFree(struct RSCache_Dat2ConfigSpotanim* spotanim)
{
    if( !spotanim )
        return;
    RSCache_Dat2ConfigSpotanimFreeInplace(spotanim);
    free(spotanim);
}

uint32_t
RSCache_Dat2ConfigSpotanimEncodeBound(const struct RSCache_Dat2ConfigSpotanim* spotanim)
{
    /* Scalar opcodes plus the recolour/retexture pairs and the one string. */
    uint32_t need = 128u;

    if( !spotanim )
        return need;
    need += 10u * 4u * 2u;
    if( spotanim->name )
        need += (uint32_t)strlen(spotanim->name) + 2u;
    return need;
}
