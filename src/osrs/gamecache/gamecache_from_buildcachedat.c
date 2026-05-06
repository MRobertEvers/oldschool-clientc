#include "gamecache_from_buildcachedat.h"

#include "gamecache.h"
#include "osrs/buildcachedat.h"

/* rscache includes kept here only -- the exclusive bridge between rscache and GameCache types. */
#include "graphics/dash.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_npc.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_spotanim.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Dash entry layout mirrors (match BuildCacheDat / GameCache entry structs).
 * -------------------------------------------------------------------------*/

struct GCFlotypeEntry
{
    int id;
    struct CacheConfigOverlay* flotype;
};
struct GCMapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapTerrain* map_terrain;
};
struct GCSceneryEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapLocs* locs;
};
struct GCModelEntry
{
    int id;
    struct CacheModel* model;
};
struct GCConfigLocEntry
{
    int id;
    struct CacheConfigLocation* config_loc;
};
struct GCSequenceEntry
{
    int id;
    struct CacheDatSequence* sequence;
};
struct GCAnimbaseframesEntry
{
    int id;
    struct CacheDatAnimBaseFrames* animbaseframes;
};
struct GCAnimframeRefEntry
{
    int id;
    int animbaseframes_id;
    int frame_index;
};
struct GCIdkEntry
{
    int id;
    struct CacheDatConfigIdk* idk;
};
struct GCObjEntry
{
    int id;
    struct CacheDatConfigObj* obj;
};
struct GCNpcEntry
{
    int id;
    struct CacheDatConfigNpc* npc;
};
struct GCSpotAnimEntry
{
    int id;
    struct CacheDatConfigSpotAnim* spotanim;
};
struct GCComponentEntry
{
    int id;
    struct CacheDatConfigComponent* component;
};
struct GCIdkModelEntry
{
    int id;
    struct CacheModel* model;
};
struct GCObjModelEntry
{
    int id;
    struct CacheModel* model;
};
struct GCNpcModelEntry
{
    int id;
    struct CacheModel* model;
};
struct GCTextureRefEntry
{
    int id;
};
struct GCSpriteRefEntry
{
    char name[64];
    int uiscene_element_id;
};
struct GCFontRefEntry
{
    char name[GAMECACHE_FONT_NAME_MAX];
    int uiscene_font_id;
};
struct GCComponentSpriteRefEntry
{
    char sprite_name[64];
    int uiscene_element_id;
};

/* ---------------------------------------------------------------------------
 * dup_* helpers: convert rscache types to trimmed GameCache types.
 * -------------------------------------------------------------------------*/

static struct GameCacheOverlay*
dup_overlay(struct CacheConfigOverlay const* o)
{
    struct GameCacheOverlay* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    d->rgb_color = o->rgb_color;
    d->texture = o->texture;
    d->secondary_rgb_color = o->secondary_rgb_color;
    return d;
}

static struct GameCacheTerrainChunk*
dup_map_terrain(struct CacheMapTerrain const* t)
{
    struct GameCacheTerrainChunk* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    int n = MAP_TERRAIN_X * MAP_TERRAIN_Z * MAP_TERRAIN_LEVELS;
    assert(n == GAMECACHE_TERRAIN_X * GAMECACHE_TERRAIN_Z * GAMECACHE_TERRAIN_LEVELS);
    for( int i = 0; i < n; i++ )
    {
        struct CacheMapFloor const* src = &t->tiles_xyz[i];
        struct GameCacheFloorTile* dst = &d->tiles[i];
        dst->overlay_id = src->overlay_id;
        dst->underlay_id = src->underlay_id;
        dst->height = src->height;
        /* attr_opcode dropped */
        dst->settings = src->settings;
        dst->shape = src->shape;
        dst->rotation = src->rotation;
    }
    return d;
}

static struct GameCacheMapLocs*
dup_map_locs(struct CacheMapLocs const* locs)
{
    if( !locs )
        return NULL;
    struct GameCacheMapLocs* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    /* _chunk_mapx and _chunk_mapz dropped (key is stored in the DashMap). */
    d->locs_count = locs->locs_count;
    d->locs = NULL;
    if( locs->locs_count > 0 && locs->locs )
    {
        d->locs = malloc((size_t)locs->locs_count * sizeof(struct GameCacheMapLoc));
        if( !d->locs )
        {
            free(d);
            return NULL;
        }
        for( int i = 0; i < locs->locs_count; i++ )
        {
            struct CacheMapLoc const* src = &locs->locs[i];
            struct GameCacheMapLoc* dst = &d->locs[i];
            dst->loc_id = src->loc_id;
            dst->shape_select = src->shape_select;
            dst->orientation = src->orientation;
            dst->chunk_pos_x = src->chunk_pos_x;
            dst->chunk_pos_z = src->chunk_pos_z;
            dst->chunk_pos_level = src->chunk_pos_level;
        }
    }
    return d;
}

static struct GameCacheSequence*
dup_sequence(struct CacheDatSequence const* s)
{
    struct GameCacheSequence* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    d->frame_count = s->frame_count;
    d->frames = NULL;
    d->delay = NULL;
    if( s->frame_count > 0 )
    {
        if( s->frames )
        {
            d->frames = malloc((size_t)s->frame_count * sizeof(int));
            memcpy(d->frames, s->frames, (size_t)s->frame_count * sizeof(int));
        }
        if( s->delay )
        {
            d->delay = malloc((size_t)s->frame_count * sizeof(int));
            memcpy(d->delay, s->delay, (size_t)s->frame_count * sizeof(int));
        }
    }
    d->replaceheldleft = s->replaceheldleft;
    d->replaceheldright = s->replaceheldright;
    /* Client.ts: priority defaults to 5 when not present in seq.dat (struct was zeroed). */
    d->priority = s->priority != 0 ? s->priority : 5;
    d->duplicate_behavior = s->duplicate_behavior;
    /* iframes and walkmerge dropped */
    return d;
}

static struct GameCacheAnimBase*
dup_animbase_to_gc(struct CacheAnimBase const* b)
{
    if( !b )
        return NULL;
    struct GameCacheAnimBase* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    d->length = b->length;
    d->types = malloc((size_t)b->length * sizeof(uint8_t));
    d->labels = malloc((size_t)b->length * sizeof(uint8_t*));
    d->label_counts = malloc((size_t)b->length * sizeof(uint16_t));
    if( !d->types || !d->labels || !d->label_counts )
        goto fail;
    memcpy(d->types, b->types, (size_t)b->length * sizeof(uint8_t));
    memcpy(d->label_counts, b->label_counts, (size_t)b->length * sizeof(uint16_t));
    memset(d->labels, 0, (size_t)b->length * sizeof(uint8_t*));
    for( int i = 0; i < b->length; i++ )
    {
        int c = (int)b->label_counts[i];
        if( b->labels[i] && c > 0 )
        {
            d->labels[i] = malloc((size_t)c * sizeof(uint8_t));
            if( !d->labels[i] )
                goto fail;
            memcpy(d->labels[i], b->labels[i], (size_t)c * sizeof(uint8_t));
        }
        else
            d->labels[i] = NULL;
    }
    return d;

fail:
    if( d->labels )
    {
        for( int i = 0; i < b->length; i++ )
            free(d->labels[i]);
    }
    free(d->labels);
    free(d->label_counts);
    free(d->types);
    free(d);
    return NULL;
}

static struct GameCacheAnimBaseFrames*
dup_animbaseframes(struct CacheDatAnimBaseFrames const* abf)
{
    struct GameCacheAnimBaseFrames* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    d->frame_count = abf->frame_count;
    d->frames = NULL;

    if( abf->frame_count <= 0 || !abf->frames )
        return d;

    d->frames = calloc((size_t)abf->frame_count, sizeof(struct GameCacheAnimframe));
    if( !d->frames )
    {
        free(d);
        return NULL;
    }

    /* Each frame owns its own deep-copied animbase (base field dropped from the container). */
    for( int i = 0; i < abf->frame_count; i++ )
    {
        struct GameCacheAnimframe* df = &d->frames[i];
        struct CacheAnimframe const* sf = &abf->frames[i];
        df->id = sf->id;
        df->base = dup_animbase_to_gc(abf->base);
        df->length = sf->length;
        df->delay = sf->delay;
        df->groups = NULL;
        df->x = NULL;
        df->y = NULL;
        df->z = NULL;
        int n = sf->length;
        if( n > 0 )
        {
            if( sf->groups )
            {
                df->groups = malloc((size_t)n * sizeof(int16_t));
                if( df->groups )
                    memcpy(df->groups, sf->groups, (size_t)n * sizeof(int16_t));
            }
            if( sf->x )
            {
                df->x = malloc((size_t)n * sizeof(int16_t));
                if( df->x )
                    memcpy(df->x, sf->x, (size_t)n * sizeof(int16_t));
            }
            if( sf->y )
            {
                df->y = malloc((size_t)n * sizeof(int16_t));
                if( df->y )
                    memcpy(df->y, sf->y, (size_t)n * sizeof(int16_t));
            }
            if( sf->z )
            {
                df->z = malloc((size_t)n * sizeof(int16_t));
                if( df->z )
                    memcpy(df->z, sf->z, (size_t)n * sizeof(int16_t));
            }
        }
    }
    return d;
}

static struct GameCacheNpc*
dup_npc(struct CacheDatConfigNpc const* n)
{
    struct GameCacheNpc* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    memset(d, 0, sizeof(*d));

    d->name = n->name ? strdup(n->name) : NULL;
    d->desc = n->desc ? strdup(n->desc) : NULL;
    d->size = n->size;
    d->readyanim = n->readyanim;
    d->walkanim = n->walkanim;
    d->walkanim_b = n->walkanim_b;
    d->walkanim_r = n->walkanim_r;
    d->walkanim_l = n->walkanim_l;
    d->minimap = (int)n->minimap;
    d->vislevel = n->vislevel;
    d->models_count = n->models_count;
    d->heads_count = n->heads_count;
    d->recol_count = n->recol_count;

    for( int i = 0; i < 5; i++ )
        d->op[i] = n->op[i] ? strdup(n->op[i]) : NULL;

    if( n->models_count > 0 && n->models )
    {
        d->models = malloc((size_t)n->models_count * sizeof(int));
        memcpy(d->models, n->models, (size_t)n->models_count * sizeof(int));
    }
    if( n->heads_count > 0 && n->heads )
    {
        d->heads = malloc((size_t)n->heads_count * sizeof(int));
        memcpy(d->heads, n->heads, (size_t)n->heads_count * sizeof(int));
    }
    if( n->recol_count > 0 && n->recol_s && n->recol_d )
    {
        d->recol_s = malloc((size_t)n->recol_count * sizeof(int));
        d->recol_d = malloc((size_t)n->recol_count * sizeof(int));
        memcpy(d->recol_s, n->recol_s, (size_t)n->recol_count * sizeof(int));
        memcpy(d->recol_d, n->recol_d, (size_t)n->recol_count * sizeof(int));
    }
    return d;
}

static struct GameCacheObj*
dup_obj(struct CacheDatConfigObj const* o)
{
    struct GameCacheObj* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    memset(d, 0, sizeof(*d));

    d->model = o->model;
    d->name = o->name ? strdup(o->name) : NULL;
    /* desc dropped */
    d->recol_count = o->recol_count;
    d->zoom2d = o->zoom2d;
    d->xan2d = o->xan2d;
    d->yan2d = o->yan2d;
    d->zan2d = o->zan2d;
    d->xof2d = o->xof2d;
    d->yof2d = o->yof2d;
    d->stackable = (int)o->stackable;
    d->cost = o->cost;
    d->manwear = o->manwear;
    d->manwear2 = o->manwear2;
    d->manwear3 = o->manwear3;
    d->manhead = o->manhead;
    d->manhead2 = o->manhead2;
    d->womanhead = o->womanhead;
    d->womanhead2 = o->womanhead2;
    d->countobj_count = o->countobj_count;
    d->ambient = o->ambient;
    d->contrast = o->contrast;

    if( o->recol_count > 0 && o->recol_s && o->recol_d )
    {
        d->recol_s = malloc((size_t)o->recol_count * sizeof(int));
        d->recol_d = malloc((size_t)o->recol_count * sizeof(int));
        memcpy(d->recol_s, o->recol_s, (size_t)o->recol_count * sizeof(int));
        memcpy(d->recol_d, o->recol_d, (size_t)o->recol_count * sizeof(int));
    }
    if( o->countobj_count > 0 && o->countobj && o->countco )
    {
        d->countobj = malloc((size_t)o->countobj_count * sizeof(int));
        d->countco = malloc((size_t)o->countobj_count * sizeof(int));
        memcpy(d->countobj, o->countobj, (size_t)o->countobj_count * sizeof(int));
        memcpy(d->countco, o->countco, (size_t)o->countobj_count * sizeof(int));
    }
    for( int i = 0; i < 5; i++ )
        d->iop[i] = o->iop[i] ? strdup(o->iop[i]) : NULL;
    /* op[5], desc, and non-consumed fields dropped */
    return d;
}

static struct GameCacheIdk*
dup_idk(struct CacheDatConfigIdk const* k)
{
    struct GameCacheIdk* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    memset(d, 0, sizeof(*d));

    d->models_count = k->models_count;
    memcpy(d->recol_s, k->recol_s, sizeof(d->recol_s));
    memcpy(d->recol_d, k->recol_d, sizeof(d->recol_d));
    memcpy(d->heads, k->heads, sizeof(d->heads));

    if( k->models_count > 0 && k->models )
    {
        d->models = malloc((size_t)k->models_count * sizeof(int));
        memcpy(d->models, k->models, (size_t)k->models_count * sizeof(int));
    }
    /* type and disable dropped */
    return d;
}

static struct GameCacheSpotAnim*
dup_spotanim(struct CacheDatConfigSpotAnim const* s)
{
    struct GameCacheSpotAnim* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    /* All 8 fields kept -- same names. */
    d->model = s->model;
    d->seq_id = s->seq_id;
    d->anim_gfx_height = s->anim_gfx_height;
    d->resizeh = s->resizeh;
    d->resizev = s->resizev;
    d->orientation = s->orientation;
    d->ambient = s->ambient;
    d->contrast = s->contrast;
    return d;
}

static char*
strdup_nonempty(char const* s)
{
    if( !s )
        return NULL;
    return strdup(s);
}

static struct GameCacheComponent*
dup_component(struct CacheDatConfigComponent const* c)
{
    struct GameCacheComponent* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    memset(d, 0, sizeof(*d));

    d->id = c->id;
    /* layer dropped */
    d->type = c->type;
    d->buttonType = c->buttonType;
    d->clientCode = c->clientCode;
    d->width = c->width;
    d->height = c->height;
    d->x = c->x;
    d->y = c->y;
    d->alpha = c->alpha;
    d->overlayer = c->overlayer;
    d->script_comparator_count = c->script_comparator_count;
    d->seqFrame = c->seqFrame;
    d->seqCycle = c->seqCycle;
    d->children_count = c->children_count;
    d->scripts_count = c->scripts_count;
    /* activeModelType and activeModel dropped */
    d->anim = c->anim;
    d->activeAnim = c->activeAnim;
    d->zoom = c->zoom;
    d->xan = c->xan;
    d->yan = c->yan;
    d->scroll = c->scroll;
    d->hide = c->hide;
    d->targetMask = c->targetMask;
    d->marginX = c->marginX;
    d->marginY = c->marginY;
    d->colour = c->colour;
    d->activeColour = c->activeColour;
    d->overColour = c->overColour;
    d->activeOverColour = c->activeOverColour;
    d->modelType = c->modelType;
    d->model = c->model;
    d->draggable = c->draggable;
    d->interactable = c->interactable;
    d->usable = c->usable;
    d->swappable = c->swappable;
    d->fill = c->fill;
    d->center = c->center;
    d->shadowed = c->shadowed;
    d->font = c->font;

    if( c->script_comparator_count > 0 && c->scriptComparator && c->scriptOperand )
    {
        int n = c->script_comparator_count;
        d->scriptComparator = malloc((size_t)n * sizeof(int));
        d->scriptOperand = malloc((size_t)n * sizeof(int));
        memcpy(d->scriptComparator, c->scriptComparator, (size_t)n * sizeof(int));
        memcpy(d->scriptOperand, c->scriptOperand, (size_t)n * sizeof(int));
    }

    if( c->scripts_count > 0 && c->scripts && c->scripts_lengths )
    {
        d->scripts = malloc((size_t)c->scripts_count * sizeof(int*));
        memset(d->scripts, 0, (size_t)c->scripts_count * sizeof(int*));
        d->scripts_lengths = malloc((size_t)c->scripts_count * sizeof(int));
        memcpy(d->scripts_lengths, c->scripts_lengths, (size_t)c->scripts_count * sizeof(int));
        for( int i = 0; i < c->scripts_count; i++ )
        {
            int len = c->scripts_lengths[i];
            if( len > 0 && c->scripts[i] )
            {
                d->scripts[i] = malloc((size_t)len * sizeof(int));
                memcpy(d->scripts[i], c->scripts[i], (size_t)len * sizeof(int));
            }
        }
    }

    int wh = c->width * c->height;
    if( wh > 0 && c->invSlotObjId && c->invSlotObjCount )
    {
        d->invSlotObjId = malloc((size_t)wh * sizeof(int));
        d->invSlotObjCount = malloc((size_t)wh * sizeof(int));
        memcpy(d->invSlotObjId, c->invSlotObjId, (size_t)wh * sizeof(int));
        memcpy(d->invSlotObjCount, c->invSlotObjCount, (size_t)wh * sizeof(int));
    }

    if( c->children_count > 0 && c->children && c->childX && c->childY )
    {
        d->children = malloc((size_t)c->children_count * sizeof(int));
        d->childX = malloc((size_t)c->children_count * sizeof(int));
        d->childY = malloc((size_t)c->children_count * sizeof(int));
        memcpy(d->children, c->children, (size_t)c->children_count * sizeof(int));
        memcpy(d->childX, c->childX, (size_t)c->children_count * sizeof(int));
        memcpy(d->childY, c->childY, (size_t)c->children_count * sizeof(int));
    }

    if( c->invSlotOffsetX && c->invSlotOffsetY )
    {
        d->invSlotOffsetX = malloc(20 * sizeof(int));
        d->invSlotOffsetY = malloc(20 * sizeof(int));
        memcpy(d->invSlotOffsetX, c->invSlotOffsetX, 20 * sizeof(int));
        memcpy(d->invSlotOffsetY, c->invSlotOffsetY, 20 * sizeof(int));
    }

    if( c->invSlotGraphic )
    {
        d->invSlotGraphic = malloc(20 * sizeof(char*));
        memset(d->invSlotGraphic, 0, 20 * sizeof(char*));
        for( int i = 0; i < 20; i++ )
        {
            if( c->invSlotGraphic[i] )
                d->invSlotGraphic[i] = strdup(c->invSlotGraphic[i]);
        }
    }

    if( c->iop )
    {
        d->iop = malloc(5 * sizeof(char*));
        memset(d->iop, 0, 5 * sizeof(char*));
        for( int i = 0; i < 5; i++ )
        {
            if( c->iop[i] )
                d->iop[i] = strdup(c->iop[i]);
        }
    }

    d->targetVerb = strdup_nonempty(c->targetVerb);
    d->targetText = strdup_nonempty(c->targetText);
    d->option = c->option ? strdup(c->option) : NULL;
    d->text = strdup_nonempty(c->text);
    d->activeText = strdup_nonempty(c->activeText);
    d->graphic = strdup_nonempty(c->graphic);
    d->activeGraphic = strdup_nonempty(c->activeGraphic);

    return d;
}

/** Convert CacheModel (rscache) to GameCacheModel, dropping _ids[10] and rotated. */
static struct GameCacheModel*
dup_model_from_cache(struct CacheModel const* m)
{
    if( !m )
        return NULL;
    struct GameCacheModel* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    memset(d, 0, sizeof(*d));

    d->_id = m->_id;
    d->_model_type = m->_model_type;
    d->_flags = m->_flags;

    d->vertex_count = m->vertex_count;
    if( m->vertex_count > 0 )
    {
        d->vertices_x = malloc((size_t)m->vertex_count * sizeof(int));
        d->vertices_y = malloc((size_t)m->vertex_count * sizeof(int));
        d->vertices_z = malloc((size_t)m->vertex_count * sizeof(int));
        memcpy(d->vertices_x, m->vertices_x, (size_t)m->vertex_count * sizeof(int));
        memcpy(d->vertices_y, m->vertices_y, (size_t)m->vertex_count * sizeof(int));
        memcpy(d->vertices_z, m->vertices_z, (size_t)m->vertex_count * sizeof(int));
    }
    if( m->vertex_bone_map )
    {
        d->vertex_bone_map = malloc((size_t)m->vertex_count * sizeof(uint8_t));
        memcpy(d->vertex_bone_map, m->vertex_bone_map, (size_t)m->vertex_count * sizeof(uint8_t));
    }
    if( m->face_bone_map )
    {
        d->face_bone_map = malloc((size_t)m->face_count * sizeof(uint8_t));
        memcpy(d->face_bone_map, m->face_bone_map, (size_t)m->face_count * sizeof(uint8_t));
    }

    d->face_count = m->face_count;
    if( m->face_indices_a )
    {
        d->face_indices_a = malloc((size_t)m->face_count * sizeof(int));
        memcpy(d->face_indices_a, m->face_indices_a, (size_t)m->face_count * sizeof(int));
    }
    if( m->face_indices_b )
    {
        d->face_indices_b = malloc((size_t)m->face_count * sizeof(int));
        memcpy(d->face_indices_b, m->face_indices_b, (size_t)m->face_count * sizeof(int));
    }
    if( m->face_indices_c )
    {
        d->face_indices_c = malloc((size_t)m->face_count * sizeof(int));
        memcpy(d->face_indices_c, m->face_indices_c, (size_t)m->face_count * sizeof(int));
    }
    if( m->face_alphas )
    {
        d->face_alphas = malloc((size_t)m->face_count * sizeof(uint8_t));
        memcpy(d->face_alphas, m->face_alphas, (size_t)m->face_count * sizeof(uint8_t));
    }
    if( m->face_infos )
    {
        d->face_infos = malloc((size_t)m->face_count * sizeof(uint8_t));
        memcpy(d->face_infos, m->face_infos, (size_t)m->face_count * sizeof(uint8_t));
    }
    if( m->face_priorities )
    {
        d->face_priorities = malloc((size_t)m->face_count * sizeof(uint8_t));
        memcpy(d->face_priorities, m->face_priorities, (size_t)m->face_count * sizeof(uint8_t));
    }
    if( m->face_colors )
    {
        d->face_colors = malloc((size_t)m->face_count * sizeof(uint16_t));
        memcpy(d->face_colors, m->face_colors, (size_t)m->face_count * sizeof(uint16_t));
    }
    d->model_priority = m->model_priority;

    d->textured_face_count = m->textured_face_count;
    if( m->textured_p_coordinate )
    {
        d->textured_p_coordinate = malloc((size_t)m->textured_face_count * sizeof(uint16_t));
        memcpy(
            d->textured_p_coordinate,
            m->textured_p_coordinate,
            (size_t)m->textured_face_count * sizeof(uint16_t));
    }
    if( m->textured_m_coordinate )
    {
        d->textured_m_coordinate = malloc((size_t)m->textured_face_count * sizeof(uint16_t));
        memcpy(
            d->textured_m_coordinate,
            m->textured_m_coordinate,
            (size_t)m->textured_face_count * sizeof(uint16_t));
    }
    if( m->textured_n_coordinate )
    {
        d->textured_n_coordinate = malloc((size_t)m->textured_face_count * sizeof(uint16_t));
        memcpy(
            d->textured_n_coordinate,
            m->textured_n_coordinate,
            (size_t)m->textured_face_count * sizeof(uint16_t));
    }
    if( m->face_textures )
    {
        d->face_textures = malloc((size_t)m->face_count * sizeof(int16_t));
        memcpy(d->face_textures, m->face_textures, (size_t)m->face_count * sizeof(int16_t));
    }
    if( m->face_texture_coords )
    {
        d->face_texture_coords = malloc((size_t)m->face_count * sizeof(int16_t));
        memcpy(
            d->face_texture_coords,
            m->face_texture_coords,
            (size_t)m->face_count * sizeof(int16_t));
    }
    if( m->textureRenderTypes )
    {
        d->textureRenderTypes = malloc((size_t)m->textured_face_count * sizeof(unsigned char));
        memcpy(
            d->textureRenderTypes,
            m->textureRenderTypes,
            (size_t)m->textured_face_count * sizeof(unsigned char));
    }
    /* _ids[10] and rotated dropped */
    return d;
}

/** Convert CacheConfigLocation (rscache) to GameCacheLoc with trimmed fields. */
static struct GameCacheLoc*
dup_loc(struct CacheConfigLocation const* src)
{
    if( !src )
        return NULL;
    struct GameCacheLoc* d = malloc(sizeof(*d));
    if( !d )
        return NULL;
    memset(d, 0, sizeof(*d));

    d->name = src->name ? strdup(src->name) : NULL;
    d->desc = src->desc ? strdup(src->desc) : NULL;
    d->size_x = src->size_x;
    d->size_z = src->size_z;
    d->blocks_walk = src->blocks_walk;
    d->blocks_projectiles = src->blocks_projectiles;
    d->wall_width = src->wall_width;
    d->is_interactive = src->is_interactive;
    d->contour_ground_type = src->contour_ground_type;
    d->contour_ground_param = src->contour_ground_param;
    d->sharelight = src->sharelight;
    d->seq_id = src->seq_id;
    d->ambient = src->ambient;
    d->contrast = src->contrast;
    for( int i = 0; i < 10; i++ )
        d->actions[i] = src->actions[i] ? strdup(src->actions[i]) : NULL;
    d->recolor_count = src->recolor_count;
    if( src->recolor_count > 0 && src->recolors_from && src->recolors_to )
    {
        d->recolors_from = malloc((size_t)src->recolor_count * sizeof(int));
        d->recolors_to = malloc((size_t)src->recolor_count * sizeof(int));
        memcpy(d->recolors_from, src->recolors_from, (size_t)src->recolor_count * sizeof(int));
        memcpy(d->recolors_to, src->recolors_to, (size_t)src->recolor_count * sizeof(int));
    }
    d->retexture_count = src->retexture_count;
    if( src->retexture_count > 0 && src->retextures_from && src->retextures_to )
    {
        d->retextures_from = malloc((size_t)src->retexture_count * sizeof(int));
        d->retextures_to = malloc((size_t)src->retexture_count * sizeof(int));
        memcpy(
            d->retextures_from, src->retextures_from, (size_t)src->retexture_count * sizeof(int));
        memcpy(d->retextures_to, src->retextures_to, (size_t)src->retexture_count * sizeof(int));
    }
    d->mirrored = src->mirrored;
    d->shadowed = src->shadowed;
    d->resize_x = src->resize_x;
    d->resize_height = src->resize_height;
    d->resize_z = src->resize_z;
    d->map_scene_id = src->map_scene_id;
    d->offset_x = src->offset_x;
    d->offset_y = src->offset_y;
    d->offset_z = src->offset_z;
    d->shapes_and_model_count = src->shapes_and_model_count;
    if( src->shapes_and_model_count > 0 )
    {
        if( src->shapes )
        {
            d->shapes = malloc((size_t)src->shapes_and_model_count * sizeof(int));
            memcpy(d->shapes, src->shapes, (size_t)src->shapes_and_model_count * sizeof(int));
        }
        if( src->lengths )
        {
            d->lengths = malloc((size_t)src->shapes_and_model_count * sizeof(int));
            memcpy(d->lengths, src->lengths, (size_t)src->shapes_and_model_count * sizeof(int));
        }
        if( src->models )
        {
            d->models = malloc((size_t)src->shapes_and_model_count * sizeof(int*));
            memset(d->models, 0, (size_t)src->shapes_and_model_count * sizeof(int*));
            for( int i = 0; i < src->shapes_and_model_count; i++ )
            {
                int len = src->lengths ? src->lengths[i] : 0;
                if( len > 0 && src->models[i] )
                {
                    d->models[i] = malloc((size_t)len * sizeof(int));
                    memcpy(d->models[i], src->models[i], (size_t)len * sizeof(int));
                }
            }
        }
    }
    return d;
}

/* ---------------------------------------------------------------------------
 * Public convert functions
 * -------------------------------------------------------------------------*/

void
gamecache_convert_reftables_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd )
        return;

    struct DashMapIter* it;

    if( bcd->textures_reftable )
    {
        it = dashmap_iter_new(bcd->textures_reftable);
        void* ent;
        while( (ent = dashmap_iter_next(it)) )
        {
            struct GCTextureRefEntry* e = (struct GCTextureRefEntry*)ent;
            gamecache_add_texture_ref(gc, e->id);
        }
        dashmap_iter_free(it);
    }

    if( bcd->fonts_reftable )
    {
        char name[GAMECACHE_FONT_NAME_MAX + 1];
        it = dashmap_iter_new(bcd->fonts_reftable);
        struct GCFontRefEntry* fe;
        while( (fe = (struct GCFontRefEntry*)dashmap_iter_next(it)) )
        {
            int lim = (int)sizeof(fe->name);
            int i = 0;
            for( ; i < lim && fe->name[i]; i++ )
                name[i] = fe->name[i];
            name[i] = '\0';
            gamecache_add_font_ref(gc, name, fe->uiscene_font_id);
        }
        dashmap_iter_free(it);
    }

    if( bcd->sprites_reftable )
    {
        it = dashmap_iter_new(bcd->sprites_reftable);
        struct GCSpriteRefEntry* se;
        while( (se = (struct GCSpriteRefEntry*)dashmap_iter_next(it)) )
            gamecache_add_sprite_ref(gc, se->name, se->uiscene_element_id);
        dashmap_iter_free(it);
    }

    if( bcd->component_sprites_reftable )
    {
        it = dashmap_iter_new(bcd->component_sprites_reftable);
        struct GCComponentSpriteRefEntry* ce;
        while( (ce = (struct GCComponentSpriteRefEntry*)dashmap_iter_next(it)) )
            gamecache_add_component_sprite_ref(gc, ce->sprite_name, ce->uiscene_element_id);
        dashmap_iter_free(it);
    }

    if( bcd->animframes_reftable )
    {
        it = dashmap_iter_new(bcd->animframes_reftable);
        struct GCAnimframeRefEntry* ae;
        while( (ae = (struct GCAnimframeRefEntry*)dashmap_iter_next(it)) )
            gamecache_add_animframe_ref(gc, ae->id, ae->animbaseframes_id, ae->frame_index);
        dashmap_iter_free(it);
    }
}

void
gamecache_convert_floortypes_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->flotype_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->flotype_hmap);
    struct GCFlotypeEntry* e;
    while( (e = (struct GCFlotypeEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheOverlay* o = dup_overlay(e->flotype);
        if( o )
            gamecache_add_flotype(gc, e->id, o);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_sequences_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->sequences_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->sequences_hmap);
    struct GCSequenceEntry* e;
    while( (e = (struct GCSequenceEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheSequence* s = dup_sequence(e->sequence);
        if( s )
            gamecache_add_sequence(gc, e->id, s);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_animbaseframes_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->animbaseframes_hmap )
        return;

    /* Convert animbaseframes data. */
    struct DashMapIter* it = dashmap_iter_new(bcd->animbaseframes_hmap);
    struct GCAnimbaseframesEntry* e;
    while( (e = (struct GCAnimbaseframesEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheAnimBaseFrames* ab = dup_animbaseframes(e->animbaseframes);
        if( ab )
            gamecache_add_animbaseframes(gc, e->id, ab);
    }
    dashmap_iter_free(it);

    /* Also sync the animframe reftable: it is populated alongside the animbaseframes
     * archives (in buildcachedat_animbaseframes_cache_add) but was not yet present
     * when gamecache_convert_reftables_from_buildcachedat ran during texture load.
     * gamecache_add_animframe_ref uses INSERT (idempotent -- safe to call again). */
    if( bcd->animframes_reftable )
    {
        it = dashmap_iter_new(bcd->animframes_reftable);
        struct GCAnimframeRefEntry* ae;
        while( (ae = (struct GCAnimframeRefEntry*)dashmap_iter_next(it)) )
            gamecache_add_animframe_ref(gc, ae->id, ae->animbaseframes_id, ae->frame_index);
        dashmap_iter_free(it);
    }
}

void
gamecache_convert_npcs_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->npc_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->npc_hmap);
    struct GCNpcEntry* e;
    while( (e = (struct GCNpcEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheNpc* n = dup_npc(e->npc);
        if( n )
            gamecache_add_npc(gc, e->id, n);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_objs_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->obj_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->obj_hmap);
    struct GCObjEntry* e;
    while( (e = (struct GCObjEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheObj* o = dup_obj(e->obj);
        if( o )
            gamecache_add_obj(gc, e->id, o);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_idks_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->idk_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->idk_hmap);
    struct GCIdkEntry* e;
    while( (e = (struct GCIdkEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheIdk* k = dup_idk(e->idk);
        if( k )
            gamecache_add_idk(gc, e->id, k);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_spotanims_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->spotanim_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->spotanim_hmap);
    struct GCSpotAnimEntry* e;
    while( (e = (struct GCSpotAnimEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheSpotAnim* s = dup_spotanim(e->spotanim);
        if( s )
            gamecache_add_spotanim(gc, e->id, s);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_components_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->component_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->component_hmap);
    struct GCComponentEntry* e;
    while( (e = (struct GCComponentEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheComponent* c = dup_component(e->component);
        if( c )
            gamecache_add_component(gc, e->id, c);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_map_terrain_chunk_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    int mapx,
    int mapz)
{
    if( !gc || !bcd )
        return;
    struct CacheMapTerrain* t = buildcachedat_get_map_terrain(bcd, mapx, mapz);
    if( !t )
        return;
    struct GameCacheTerrainChunk* d = dup_map_terrain(t);
    if( d )
        gamecache_add_map_terrain(gc, mapx, mapz, d);
}

void
gamecache_convert_scenery_chunk_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    int mapx,
    int mapz)
{
    if( !gc || !bcd )
        return;
    struct CacheMapLocs* locs = buildcachedat_get_scenery(bcd, mapx, mapz);
    if( !locs )
        return;
    struct GameCacheMapLocs* d = dup_map_locs(locs);
    if( d )
        gamecache_add_scenery(gc, mapx, mapz, d);
}

void
gamecache_convert_loc_configs_chunk_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    int mapx,
    int mapz)
{
    if( !gc || !bcd )
        return;
    struct CacheMapLocs* locs = buildcachedat_get_scenery(bcd, mapx, mapz);
    if( !locs || !locs->locs )
        return;

    for( int i = 0; i < locs->locs_count; i++ )
    {
        int loc_id = locs->locs[i].loc_id;
        struct CacheConfigLocation* src = buildcachedat_get_config_loc(bcd, loc_id);
        if( !src )
            continue;
        struct GameCacheLoc* d = dup_loc(src);
        if( d )
            gamecache_add_config_loc(gc, loc_id, d);
    }
}

void
gamecache_convert_models_chunk_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd,
    int mapx,
    int mapz)
{
    (void)mapx;
    (void)mapz;
    if( !gc || !bcd || !bcd->models_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->models_hmap);
    struct GCModelEntry* e;
    while( (e = (struct GCModelEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheModel* m = dup_model_from_cache(e->model);
        if( m )
            gamecache_add_model(gc, e->id, m);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_npc_models_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->npc_models_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->npc_models_hmap);
    struct GCNpcModelEntry* e;
    while( (e = (struct GCNpcModelEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheModel* m = dup_model_from_cache(e->model);
        if( m )
            gamecache_add_npc_model(gc, e->id, m);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_obj_models_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->obj_models_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->obj_models_hmap);
    struct GCObjModelEntry* e;
    while( (e = (struct GCObjModelEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheModel* m = dup_model_from_cache(e->model);
        if( m )
            gamecache_add_obj_model(gc, e->id, m);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_idk_models_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd || !bcd->idk_models_hmap )
        return;
    struct DashMapIter* it = dashmap_iter_new(bcd->idk_models_hmap);
    struct GCIdkModelEntry* e;
    while( (e = (struct GCIdkModelEntry*)dashmap_iter_next(it)) )
    {
        struct GameCacheModel* m = dup_model_from_cache(e->model);
        if( m )
            gamecache_add_idk_model(gc, e->id, m);
    }
    dashmap_iter_free(it);
}

void
gamecache_convert_all_from_buildcachedat(
    struct GameCache* gc,
    struct BuildCacheDat* bcd)
{
    if( !gc || !bcd )
        return;

    gamecache_convert_animbaseframes_from_buildcachedat(gc, bcd);
    gamecache_convert_reftables_from_buildcachedat(gc, bcd);
    gamecache_convert_floortypes_from_buildcachedat(gc, bcd);
    gamecache_convert_sequences_from_buildcachedat(gc, bcd);

    if( bcd->map_terrains_hmap )
    {
        struct DashMapIter* it = dashmap_iter_new(bcd->map_terrains_hmap);
        struct GCMapTerrainEntry* e;
        while( (e = (struct GCMapTerrainEntry*)dashmap_iter_next(it)) )
        {
            struct GameCacheTerrainChunk* d = dup_map_terrain(e->map_terrain);
            if( d )
                gamecache_add_map_terrain(gc, e->mapx, e->mapz, d);
        }
        dashmap_iter_free(it);
    }

    if( bcd->scenery_hmap )
    {
        struct DashMapIter* it = dashmap_iter_new(bcd->scenery_hmap);
        struct GCSceneryEntry* e;
        while( (e = (struct GCSceneryEntry*)dashmap_iter_next(it)) )
        {
            struct GameCacheMapLocs* d = dup_map_locs(e->locs);
            if( d )
                gamecache_add_scenery(gc, e->mapx, e->mapz, d);
        }
        dashmap_iter_free(it);
    }

    gamecache_convert_models_chunk_from_buildcachedat(gc, bcd, 0, 0);

    if( bcd->config_loc_hmap )
    {
        struct DashMapIter* it = dashmap_iter_new(bcd->config_loc_hmap);
        struct GCConfigLocEntry* e;
        while( (e = (struct GCConfigLocEntry*)dashmap_iter_next(it)) )
        {
            struct GameCacheLoc* d = dup_loc(e->config_loc);
            if( d )
                gamecache_add_config_loc(gc, e->id, d);
        }
        dashmap_iter_free(it);
    }

    gamecache_convert_npcs_from_buildcachedat(gc, bcd);
    gamecache_convert_objs_from_buildcachedat(gc, bcd);
    gamecache_convert_idks_from_buildcachedat(gc, bcd);
    gamecache_convert_spotanims_from_buildcachedat(gc, bcd);
    gamecache_convert_components_from_buildcachedat(gc, bcd);

    gamecache_convert_npc_models_from_buildcachedat(gc, bcd);
    gamecache_convert_obj_models_from_buildcachedat(gc, bcd);
    gamecache_convert_idk_models_from_buildcachedat(gc, bcd);
}
