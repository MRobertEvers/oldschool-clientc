#ifndef TORIAUXLIBC_T_RUNESCAPE_DAT2_ANIM_LOAD_H
#define TORIAUXLIBC_T_RUNESCAPE_DAT2_ANIM_LOAD_H

#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/c/toriauxlibc_submit.h"

#include <stdbool.h>
#include <stdlib.h>

struct Dat2AnimArchiveSet
{
    int* ids;
    int count;
    int capacity;
};

static void
dat2_anim_archive_set_init(
    struct Dat2AnimArchiveSet* set,
    int initial_capacity)
{
    set->ids = malloc((size_t)initial_capacity * sizeof(int));
    set->count = 0;
    set->capacity = set->ids ? initial_capacity : 0;
}

static void
dat2_anim_archive_set_free(struct Dat2AnimArchiveSet* set)
{
    free(set->ids);
    set->ids = NULL;
    set->count = 0;
    set->capacity = 0;
}

static bool
dat2_anim_archive_set_add(
    struct Dat2AnimArchiveSet* set,
    int archive_id)
{
    for( int k = 0; k < set->count; k++ )
    {
        if( set->ids[k] == archive_id )
            return true;
    }

    if( set->count >= set->capacity )
    {
        int new_capacity = set->capacity ? set->capacity * 2 : 32;
        int* grow = realloc(set->ids, (size_t)new_capacity * sizeof(int));
        if( !grow )
            return false;
        set->ids = grow;
        set->capacity = new_capacity;
    }

    set->ids[set->count++] = archive_id;
    return true;
}

static void
dat2_anim_set_add_sequence_archives(
    struct Dat2AnimArchiveSet* set,
    const struct RSCacheDat2A_ConfigSequence* seq)
{
    if( !set || !seq )
        return;

    for( int fi = 0; fi < seq->frame_count; fi++ )
    {
        int aid = (seq->frame_ids[fi] >> 16) & 0xFFFF;
        if( aid < 0 )
            continue;
        dat2_anim_archive_set_add(set, aid);
    }
}

static void
dat2_anim_cache_sequence_skeletal(
    struct Dat2BuildCache* dat2_bc,
    struct RSCacheDat2Disk* cache_disk,
    const struct RSCacheDat2A_ConfigSequence* seq)
{
    if( !dat2_bc || !cache_disk || !seq )
        return;

    int maya_id = seq->anim_maya_id;
    if( maya_id < 0 || dat2_buildcache_skeletal_has(dat2_bc, maya_id) )
        return;

    struct RSCacheDat2A_AnimMaya* maya = RSCacheDat2A_AnimMayaNewFromCache(cache_disk, maya_id);
    if( maya )
        dat2_buildcache_skeletal_add(dat2_bc, maya_id, maya);
}

static void
dat2_anim_submit_archive_set(
    struct ToriAuxLibC* c,
    struct Dat2BuildCache* dat2_bc,
    struct RSCacheDat2Disk* cache_disk,
    const struct Dat2AnimArchiveSet* set)
{
    if( !c || !dat2_bc || !cache_disk || !set )
        return;

    for( int i = 0; i < set->count; i++ )
    {
        int aid = set->ids[i];
        if( !dat2_buildcache_frames_has(dat2_bc, aid) )
            dat2_buildcache_frames_init_from_archive(dat2_bc, cache_disk, aid);
        ToriAuxLibC_SubmitAnimationFromDat2(c, aid);
    }
}

struct Dat2AnimSubmitSkeletalCtx
{
    struct ToriAuxLibC* c;
};

static void
dat2_anim_submit_skeletal_cb(
    int anim_maya_id,
    struct RSCacheDat2A_AnimMaya* maya,
    void* user_data)
{
    (void)maya;
    struct Dat2AnimSubmitSkeletalCtx* ctx = user_data;
    ToriAuxLibC_SubmitSkeletalFromDat2(ctx->c, anim_maya_id);
}

static void
dat2_anim_submit_all_skeletal(
    struct ToriAuxLibC* c,
    struct Dat2BuildCache* dat2_bc)
{
    struct Dat2AnimSubmitSkeletalCtx ctx = { .c = c };
    dat2_buildcache_foreach_skeletal(dat2_bc, dat2_anim_submit_skeletal_cb, &ctx);
}

static void
dat2_anim_submit_sequence_skeletal(
    struct ToriAuxLibC* c,
    struct Dat2BuildCache* dat2_bc,
    const struct RSCacheDat2A_ConfigSequence* seq)
{
    if( !c || !dat2_bc || !seq )
        return;

    int maya_id = seq->anim_maya_id;
    if( maya_id >= 0 && dat2_buildcache_skeletal_has(dat2_bc, maya_id) )
        ToriAuxLibC_SubmitSkeletalFromDat2(c, maya_id);
}

#endif
