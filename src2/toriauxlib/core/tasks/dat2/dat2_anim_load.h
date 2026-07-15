#ifndef TORIAUXLIB_DAT2_ANIM_LOAD_H
#define TORIAUXLIB_DAT2_ANIM_LOAD_H

#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

static inline bool
dat2_anim_classic_frame_archive_loaded(
    struct ToriAuxLibCore* core,
    struct Dat2BuildCache* dat2_bc,
    int archive_id)
{
    return dat2_buildcache_frames_has(dat2_bc, archive_id) ||
           ToriAuxLibCore_AnimationHas(core, archive_id);
}

static inline bool
dat2_anim_skeletal_loaded(
    struct ToriAuxLibCore* core,
    struct Dat2BuildCache* dat2_bc,
    int anim_maya_id)
{
    return dat2_buildcache_skeletal_has(dat2_bc, anim_maya_id) ||
           ToriAuxLibCore_SkeletalAnimHas(core, anim_maya_id);
}

static inline bool
dat2_anim_sequence_assets_missing(
    struct ToriAuxLibCore* core,
    struct Dat2BuildCache* dat2_bc,
    const struct RSCacheDat2A_ConfigSequence* bc_seq,
    const struct ToriAuxLibCore_Sequence* core_seq)
{
    if( bc_seq )
    {
        if( bc_seq->anim_maya_id >= 0 && bc_seq->frame_count == 0 )
            return !dat2_anim_skeletal_loaded(core, dat2_bc, bc_seq->anim_maya_id);

        for( int fi = 0; fi < bc_seq->frame_count; fi++ )
        {
            int aid = (bc_seq->frame_ids[fi] >> 16) & 0xFFFF;
            if( aid < 0 )
                continue;
            if( !dat2_anim_classic_frame_archive_loaded(core, dat2_bc, aid) )
                return true;
        }
        return false;
    }

    if( core_seq )
    {
        if( core_seq->anim_maya_id >= 0 && core_seq->frame_count == 0 )
            return !dat2_anim_skeletal_loaded(core, dat2_bc, core_seq->anim_maya_id);

        for( int fi = 0; fi < core_seq->frame_count; fi++ )
        {
            int aid = (core_seq->frames[fi] >> 16) & 0xFFFF;
            if( aid < 0 )
                continue;
            if( !dat2_anim_classic_frame_archive_loaded(core, dat2_bc, aid) )
                return true;
        }
    }

    return true;
}

struct Dat2AnimArchiveSet
{
    int* ids;
    int count;
    int capacity;
};

static inline void
dat2_anim_archive_set_init(
    struct Dat2AnimArchiveSet* set,
    int initial_capacity)
{
    set->ids = malloc((size_t)initial_capacity * sizeof(int));
    set->count = 0;
    set->capacity = set->ids ? initial_capacity : 0;
}

static inline void
dat2_anim_archive_set_free(struct Dat2AnimArchiveSet* set)
{
    free(set->ids);
    set->ids = NULL;
    set->count = 0;
    set->capacity = 0;
}

static inline bool
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

static inline void
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

struct Dat2AnimSubmitSkeletalCtx
{
    struct ToriAuxLibCache* c;
};

static inline void
dat2_anim_submit_skeletal_cb(
    int anim_maya_id,
    struct RSCacheDat2A_AnimMaya* maya,
    void* user_data)
{
    (void)maya;
    struct Dat2AnimSubmitSkeletalCtx* ctx = user_data;
    ToriAuxLibCache_SubmitSkeletalFromDat2(ctx->c, anim_maya_id);
}

static inline void
dat2_anim_submit_all_skeletal(
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache* dat2_bc)
{
    struct Dat2AnimSubmitSkeletalCtx ctx = { .c = c };
    dat2_buildcache_foreach_skeletal(dat2_bc, dat2_anim_submit_skeletal_cb, &ctx);
}

static inline void
dat2_anim_submit_sequence_skeletal(
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache* dat2_bc,
    const struct RSCacheDat2A_ConfigSequence* seq)
{
    int maya_id;

    assert(c && dat2_bc && seq);

    maya_id = seq->anim_maya_id;
    if( maya_id < 0 || seq->frame_count != 0 )
        return;

    assert(
        dat2_buildcache_skeletal_has(dat2_bc, maya_id) &&
        "skeletal sequence missing animaya in buildcache");
    ToriAuxLibCache_SubmitSkeletalFromDat2(c, maya_id);
}

#endif
