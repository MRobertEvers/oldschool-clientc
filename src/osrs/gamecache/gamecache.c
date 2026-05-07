#include "gamecache.h"

#include "graphics/dash.h"
#include "osrs/rscache/tables/maps.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define GAMECACHE_EVENTBUFFER_DEFAULT_CAPACITY 1024
#define GAMECACHE_HMAP_INITIAL_CAPACITY 64

/* ---------------------------------------------------------------------------
 * Entry structs -- all store owned GameCache* types.
 * -------------------------------------------------------------------------*/

struct SpriteRefEntry
{
    char name[64]; /* Key must be first field and fixed size for DashMap */
    int uiscene_element_id;
};

struct MapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct GameCacheTerrainChunk* map_terrain;
};

struct FlotypeEntry
{
    int id;
    struct GameCacheOverlay* flotype;
};

struct ConfigLocEntry
{
    int id;
    struct GameCacheLoc* config_loc;
};

struct AnimframeRefEntry
{
    int id;
    int animbaseframes_id;
    int frame_index;
};

struct AnimbaseframesEntry
{
    int id;
    struct GameCacheAnimBaseFrames* animbaseframes;
};

struct SceneryEntry
{
    int id;
    int mapx;
    int mapz;
    struct GameCacheMapLocs* locs;
};

struct ModelEntry
{
    int id;
    struct GameCacheModel* model;
};

struct TextureRefEntry
{
    int id;
};

struct FontRefEntry
{
    char name[GAMECACHE_FONT_NAME_MAX]; /* Key must be first field; zero-padded */
    int uiscene_font_id;
};

struct SequenceEntry
{
    int id;
    struct GameCacheSequence* sequence;
};

struct IdkEntry
{
    int id;
    struct GameCacheIdk* idk;
};

struct ObjEntry
{
    int id;
    struct GameCacheObj* obj;
};

struct SpotAnimEntry
{
    int id;
    struct GameCacheSpotAnim* spotanim;
};

struct IdkModelEntry
{
    int id;
    struct GameCacheModel* model;
};

struct ObjModelEntry
{
    int id;
    struct GameCacheModel* model;
};

struct NpcEntry
{
    int id;
    struct GameCacheNpc* npc;
};

struct NpcModelEntry
{
    int id;
    struct GameCacheModel* model;
};

struct ComponentEntry
{
    int id;
    struct GameCacheComponent* component;
};

struct ComponentSpriteRefEntry
{
    char sprite_name[64]; /* Key must be first field and fixed size for DashMap */
    int uiscene_element_id;
};

/* ---------------------------------------------------------------------------
 * DashMap helpers
 * -------------------------------------------------------------------------*/

static struct DashMap*
gamecache_create_hmap(size_t key_size, size_t entry_size, size_t initial_capacity)
{
    size_t buffer_size = dashmap_buffer_size_for(entry_size, initial_capacity);
    struct DashMapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = key_size,
        .entry_size = entry_size,
    };
    return dashmap_new(&config, 0);
}

static void
gamecache_maybe_grow_hmap(struct DashMap* map)
{
    uint32_t count = dashmap_count(map);
    uint32_t capacity = dashmap_capacity(map);
    if( count * 4 <= capacity * 3 )
        return;

    size_t new_capacity = (size_t)capacity * 2;
    size_t esize = dashmap_entry_size(map);
    size_t new_buffer_size = dashmap_buffer_size_for(esize, new_capacity);
    void* new_buffer = malloc(new_buffer_size);
    void* old_buffer = NULL;
    int rc = dashmap_resize(map, new_buffer, new_buffer_size, new_capacity, &old_buffer);
    assert(rc == DASHMAP_OK);
    (void)rc;
    free(old_buffer);
}

void
gamecache_reserve_hmap(struct DashMap* map, size_t min_count)
{
    /* Ensure the map can hold min_count entries at < 75% load without any incremental resize. */
    size_t need_capacity = (min_count * 4 + 2) / 3 + 1;
    uint32_t capacity = dashmap_capacity(map);
    if( (size_t)capacity >= need_capacity )
        return;

    size_t new_capacity = (size_t)capacity;
    while( new_capacity < need_capacity )
        new_capacity *= 2;

    size_t esize = dashmap_entry_size(map);
    size_t new_buffer_size = dashmap_buffer_size_for(esize, new_capacity);
    void* new_buffer = malloc(new_buffer_size);
    void* old_buffer = NULL;
    int rc = dashmap_resize(map, new_buffer, new_buffer_size, new_capacity, &old_buffer);
    assert(rc == DASHMAP_OK);
    (void)rc;
    free(old_buffer);
}

/* ---------------------------------------------------------------------------
 * Event buffer
 * -------------------------------------------------------------------------*/

static void
gamecache_eventbuffer_push(struct GameCache* gc, struct GameCacheEvent event)
{
    if( !gc || !gc->eventbuffer || gc->eventbuffer_capacity <= 0 )
        return;

    if( gc->eventbuffer_count == gc->eventbuffer_capacity )
    {
        gc->eventbuffer_head = (gc->eventbuffer_head + 1) % gc->eventbuffer_capacity;
        gc->eventbuffer_count--;
    }

    gc->eventbuffer[gc->eventbuffer_tail] = event;
    gc->eventbuffer_tail = (gc->eventbuffer_tail + 1) % gc->eventbuffer_capacity;
    gc->eventbuffer_count++;
}

/* ---------------------------------------------------------------------------
 * Per-type free helpers (own the GameCache* structs)
 * -------------------------------------------------------------------------*/

static void
gamecache_overlay_free(struct GameCacheOverlay* o)
{
    if( !o )
        return;
    free(o);
}

static void
gamecache_maplocs_free(struct GameCacheMapLocs* ml)
{
    if( !ml )
        return;
    free(ml->locs);
    free(ml);
}

static void
gamecache_loc_free(struct GameCacheLoc* loc)
{
    if( !loc )
        return;
    free(loc->name);
    free(loc->desc);
    for( int i = 0; i < 10; i++ )
        free(loc->actions[i]);
    free(loc->recolors_from);
    free(loc->recolors_to);
    free(loc->retextures_from);
    free(loc->retextures_to);
    free(loc->shapes);
    if( loc->models )
    {
        for( int i = 0; i < loc->shapes_and_model_count; i++ )
            free(loc->models[i]);
        free(loc->models);
    }
    free(loc->lengths);
    free(loc);
}

static void
gamecache_animbase_free(struct GameCacheAnimBase* base)
{
    if( !base )
        return;
    free(base->types);
    if( base->labels )
    {
        for( int i = 0; i < base->length; i++ )
            free(base->labels[i]);
        free(base->labels);
    }
    free(base->label_counts);
    free(base);
}

static void
gamecache_animframe_free_contents(struct GameCacheAnimframe* af)
{
    if( !af )
        return;
    gamecache_animbase_free(af->base);
    free(af->groups);
    free(af->x);
    free(af->y);
    free(af->z);
}

static void
gamecache_animbaseframes_free(struct GameCacheAnimBaseFrames* abf)
{
    if( !abf )
        return;
    if( abf->frames )
    {
        for( int i = 0; i < abf->frame_count; i++ )
            gamecache_animframe_free_contents(&abf->frames[i]);
        free(abf->frames);
    }
    free(abf);
}

static void
gamecache_sequence_free(struct GameCacheSequence* seq)
{
    if( !seq )
        return;
    free(seq->frames);
    free(seq->delay);
    free(seq->iframes);
    free(seq);
}

static void
gamecache_idk_free(struct GameCacheIdk* idk)
{
    if( !idk )
        return;
    free(idk->models);
    free(idk);
}

static void
gamecache_obj_free(struct GameCacheObj* obj)
{
    if( !obj )
        return;
    free(obj->name);
    free(obj->recol_s);
    free(obj->recol_d);
    free(obj->countobj);
    free(obj->countco);
    for( int i = 0; i < 5; i++ )
        free(obj->iop[i]);
    free(obj);
}

static void
gamecache_spotanim_free(struct GameCacheSpotAnim* s)
{
    if( !s )
        return;
    free(s);
}

static void
gamecache_npc_free(struct GameCacheNpc* npc)
{
    if( !npc )
        return;
    free(npc->name);
    free(npc->desc);
    free(npc->models);
    free(npc->heads);
    free(npc->recol_s);
    free(npc->recol_d);
    for( int i = 0; i < 5; i++ )
        free(npc->op[i]);
    free(npc);
}

static void
gamecache_component_free(struct GameCacheComponent* c)
{
    if( !c )
        return;
    if( c->scripts )
    {
        for( int i = 0; i < c->scripts_count; i++ )
            free(c->scripts[i]);
        free(c->scripts);
    }
    free(c->scripts_lengths);
    free(c->scriptComparator);
    free(c->scriptOperand);
    free(c->invSlotObjId);
    free(c->invSlotObjCount);
    free(c->children);
    free(c->targetVerb);
    free(c->targetText);
    free(c->text);
    free(c->activeText);
    free(c->invSlotOffsetX);
    free(c->invSlotOffsetY);
    free(c->graphic);
    free(c->activeGraphic);
    if( c->invSlotGraphic )
    {
        for( int i = 0; i < 20; i++ )
            free(c->invSlotGraphic[i]);
        free(c->invSlotGraphic);
    }
    if( c->iop )
    {
        for( int i = 0; i < 5; i++ )
            free(c->iop[i]);
        free(c->iop);
    }
    free(c->childX);
    free(c->childY);
    free(c);
}

/* ---------------------------------------------------------------------------
 * Per-entry free callbacks for dashmap_free_entries
 * -------------------------------------------------------------------------*/

static void
free_flotype_entry(void* e)
{
    gamecache_overlay_free(((struct FlotypeEntry*)e)->flotype);
}
static void
free_scenery_entry(void* e)
{
    gamecache_maplocs_free(((struct SceneryEntry*)e)->locs);
}
static void
free_model_entry(void* e)
{
    gamecache_model_free(((struct ModelEntry*)e)->model);
}
static void
free_config_loc_entry(void* e)
{
    gamecache_loc_free(((struct ConfigLocEntry*)e)->config_loc);
}
static void
free_animbaseframes_entry(void* e)
{
    gamecache_animbaseframes_free(((struct AnimbaseframesEntry*)e)->animbaseframes);
}
static void
free_sequence_entry(void* e)
{
    gamecache_sequence_free(((struct SequenceEntry*)e)->sequence);
}
static void
free_idk_entry(void* e)
{
    gamecache_idk_free(((struct IdkEntry*)e)->idk);
}
static void
free_obj_entry(void* e)
{
    gamecache_obj_free(((struct ObjEntry*)e)->obj);
}
static void
free_spotanim_entry(void* e)
{
    gamecache_spotanim_free(((struct SpotAnimEntry*)e)->spotanim);
}
static void
free_idk_model_entry(void* e)
{
    gamecache_model_free(((struct IdkModelEntry*)e)->model);
}
static void
free_obj_model_entry(void* e)
{
    gamecache_model_free(((struct ObjModelEntry*)e)->model);
}
static void
free_map_terrain_entry(void* e)
{
    free(((struct MapTerrainEntry*)e)->map_terrain);
}
static void
free_npc_entry(void* e)
{
    gamecache_npc_free(((struct NpcEntry*)e)->npc);
}
static void
free_npc_model_entry(void* e)
{
    gamecache_model_free(((struct NpcModelEntry*)e)->model);
}
static void
free_component_entry(void* e)
{
    gamecache_component_free(((struct ComponentEntry*)e)->component);
}

/* ---------------------------------------------------------------------------
 * Map factory helpers
 * -------------------------------------------------------------------------*/

static struct DashMap*
gamecache_new_scenery_hmap(void)
{
    return gamecache_create_hmap(
        sizeof(int), sizeof(struct SceneryEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
}

static struct DashMap*
gamecache_new_map_terrains_hmap(void)
{
    return gamecache_create_hmap(
        sizeof(int), sizeof(struct MapTerrainEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
}

static struct DashMap*
gamecache_new_models_hmap(void)
{
    return gamecache_create_hmap(
        sizeof(int), sizeof(struct ModelEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
}

static void
gamecache_init_reftables_if_needed(struct GameCache* gc)
{
    size_t cap = GAMECACHE_HMAP_INITIAL_CAPACITY;

    if( !gc->textures_reftable )
        gc->textures_reftable =
            gamecache_create_hmap(sizeof(int), sizeof(struct TextureRefEntry), cap);
    if( !gc->fonts_reftable )
        gc->fonts_reftable =
            gamecache_create_hmap(GAMECACHE_FONT_NAME_MAX, sizeof(struct FontRefEntry), cap);
    if( !gc->animframes_reftable )
        gc->animframes_reftable =
            gamecache_create_hmap(sizeof(int), sizeof(struct AnimframeRefEntry), cap);
    if( !gc->component_sprites_reftable )
        gc->component_sprites_reftable =
            gamecache_create_hmap(64, sizeof(struct ComponentSpriteRefEntry), cap);
    if( !gc->sprites_reftable )
        gc->sprites_reftable = gamecache_create_hmap(64, sizeof(struct SpriteRefEntry), cap);
}

static void
gamecache_init_owning_maps(struct GameCache* gc)
{
    size_t cap = GAMECACHE_HMAP_INITIAL_CAPACITY;

    gamecache_init_reftables_if_needed(gc);

    gc->flotype_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct FlotypeEntry), cap);
    gc->scenery_hmap = gamecache_new_scenery_hmap();
    gc->models_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct ModelEntry), cap);
    gc->config_loc_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct ConfigLocEntry), cap);
    gc->animbaseframes_hmap =
        gamecache_create_hmap(sizeof(int), sizeof(struct AnimbaseframesEntry), cap);
    gc->sequences_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct SequenceEntry), cap);
    gc->idk_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct IdkEntry), cap);
    gc->obj_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct ObjEntry), cap);
    gc->spotanim_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct SpotAnimEntry), cap);
    gc->idk_models_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct IdkModelEntry), cap);
    gc->obj_models_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct ObjModelEntry), cap);
    gc->map_terrains_hmap = gamecache_new_map_terrains_hmap();
    gc->npc_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct NpcEntry), cap);
    gc->npc_models_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct NpcModelEntry), cap);
    if( !gc->component_hmap )
        gc->component_hmap =
            gamecache_create_hmap(sizeof(int), sizeof(struct ComponentEntry), cap);
}

static void
gamecache_init_maps_and_eventbuffer(struct GameCache* gc)
{
    gamecache_init_owning_maps(gc);

    gc->eventbuffer_capacity = GAMECACHE_EVENTBUFFER_DEFAULT_CAPACITY;
    gc->eventbuffer = calloc((size_t)gc->eventbuffer_capacity, sizeof(struct GameCacheEvent));
    gc->eventbuffer_head = 0;
    gc->eventbuffer_tail = 0;
    gc->eventbuffer_count = 0;
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------*/

struct GameCache*
gamecache_new(void)
{
    struct GameCache* gc = malloc(sizeof(struct GameCache));
    memset(gc, 0, sizeof(struct GameCache));
    gamecache_init_maps_and_eventbuffer(gc);
    return gc;
}

static void
dashmap_free_entries(struct DashMap* map, void (*entry_free_fn)(void*))
{
    if( !map )
        return;
    if( entry_free_fn )
    {
        struct DashMapIter* iter = dashmap_iter_new(map);
        void* entry;
        while( (entry = dashmap_iter_next(iter)) )
            entry_free_fn(entry);
        dashmap_iter_free(iter);
    }
    free(dashmap_buffer_ptr(map));
    dashmap_free(map);
}

void
gamecache_free(struct GameCache* gc)
{
    if( !gc )
        return;

    dashmap_free_entries(gc->textures_reftable, NULL);
    dashmap_free_entries(gc->fonts_reftable, NULL);
    dashmap_free_entries(gc->sprites_reftable, NULL);
    dashmap_free_entries(gc->flotype_hmap, free_flotype_entry);
    dashmap_free_entries(gc->scenery_hmap, free_scenery_entry);
    dashmap_free_entries(gc->models_hmap, free_model_entry);
    dashmap_free_entries(gc->config_loc_hmap, free_config_loc_entry);

    dashmap_free_entries(gc->animframes_reftable, NULL);
    dashmap_free_entries(gc->animbaseframes_hmap, free_animbaseframes_entry);

    dashmap_free_entries(gc->sequences_hmap, free_sequence_entry);
    dashmap_free_entries(gc->idk_hmap, free_idk_entry);
    dashmap_free_entries(gc->obj_hmap, free_obj_entry);
    dashmap_free_entries(gc->spotanim_hmap, free_spotanim_entry);
    dashmap_free_entries(gc->idk_models_hmap, free_idk_model_entry);
    dashmap_free_entries(gc->obj_models_hmap, free_obj_model_entry);
    dashmap_free_entries(gc->map_terrains_hmap, free_map_terrain_entry);
    dashmap_free_entries(gc->npc_hmap, free_npc_entry);
    dashmap_free_entries(gc->npc_models_hmap, free_npc_model_entry);
    dashmap_free_entries(gc->component_hmap, free_component_entry);
    dashmap_free_entries(gc->component_sprites_reftable, NULL);

    free(gc->eventbuffer);
    free(gc);
}

static void
gamecache_clear_internal(struct GameCache* gc)
{
    if( !gc )
        return;

    dashmap_free_entries(gc->flotype_hmap, free_flotype_entry);
    gc->flotype_hmap = NULL;
    dashmap_free_entries(gc->scenery_hmap, free_scenery_entry);
    gc->scenery_hmap = NULL;
    dashmap_free_entries(gc->models_hmap, free_model_entry);
    gc->models_hmap = NULL;
    dashmap_free_entries(gc->config_loc_hmap, free_config_loc_entry);
    gc->config_loc_hmap = NULL;

    dashmap_free_entries(gc->animbaseframes_hmap, free_animbaseframes_entry);
    gc->animbaseframes_hmap = NULL;

    dashmap_free_entries(gc->sequences_hmap, free_sequence_entry);
    gc->sequences_hmap = NULL;
    dashmap_free_entries(gc->idk_hmap, free_idk_entry);
    gc->idk_hmap = NULL;
    dashmap_free_entries(gc->obj_hmap, free_obj_entry);
    gc->obj_hmap = NULL;
    dashmap_free_entries(gc->spotanim_hmap, free_spotanim_entry);
    gc->spotanim_hmap = NULL;
    dashmap_free_entries(gc->idk_models_hmap, free_idk_model_entry);
    gc->idk_models_hmap = NULL;
    dashmap_free_entries(gc->obj_models_hmap, free_obj_model_entry);
    gc->obj_models_hmap = NULL;
    dashmap_free_entries(gc->map_terrains_hmap, free_map_terrain_entry);
    gc->map_terrains_hmap = NULL;
    dashmap_free_entries(gc->npc_hmap, free_npc_entry);
    gc->npc_hmap = NULL;
    dashmap_free_entries(gc->npc_models_hmap, free_npc_model_entry);
    gc->npc_models_hmap = NULL;
    /* Keep component_hmap: decoded interfaces are required after this clear (e.g.
     * LuaGame_rebuild_centerzone_end -> gamecache_clear after world load). */

    free(gc->eventbuffer);
    gc->eventbuffer = NULL;

    gamecache_init_owning_maps(gc);

    gc->eventbuffer_capacity = GAMECACHE_EVENTBUFFER_DEFAULT_CAPACITY;
    gc->eventbuffer = calloc((size_t)gc->eventbuffer_capacity, sizeof(struct GameCacheEvent));
    gc->eventbuffer_head = 0;
    gc->eventbuffer_tail = 0;
    gc->eventbuffer_count = 0;
}

void
gamecache_clear(struct GameCache* gc)
{
    gamecache_clear_internal(gc);
}

void
gamecache_reset_uiscene_linked_reftables(struct GameCache* gc)
{
    if( !gc )
        return;
    dashmap_free_entries(gc->component_sprites_reftable, NULL);
    gc->component_sprites_reftable = NULL;
    dashmap_free_entries(gc->sprites_reftable, NULL);
    gc->sprites_reftable = NULL;
    dashmap_free_entries(gc->fonts_reftable, NULL);
    gc->fonts_reftable = NULL;
    gamecache_init_reftables_if_needed(gc);
}

void
gamecache_clear_map_chunks(struct GameCache* gc)
{
    if( !gc )
        return;

    dashmap_free_entries(gc->map_terrains_hmap, free_map_terrain_entry);
    dashmap_free_entries(gc->scenery_hmap, free_scenery_entry);
    gc->map_terrains_hmap = gamecache_new_map_terrains_hmap();
    gc->scenery_hmap = gamecache_new_scenery_hmap();
}

void
gamecache_map_terrain_cache_clear(struct GameCache* gc)
{
    if( !gc || !gc->map_terrains_hmap )
        return;
    dashmap_free_entries(gc->map_terrains_hmap, free_map_terrain_entry);
    gc->map_terrains_hmap = gamecache_new_map_terrains_hmap();
}

void
gamecache_map_scenery_cache_clear(struct GameCache* gc)
{
    if( !gc || !gc->scenery_hmap )
        return;
    dashmap_free_entries(gc->scenery_hmap, free_scenery_entry);
    gc->scenery_hmap = gamecache_new_scenery_hmap();
}

void
gamecache_model_cache_clear(struct GameCache* gc)
{
    if( !gc )
        return;
    if( gc->models_hmap )
    {
        dashmap_free_entries(gc->models_hmap, free_model_entry);
        gc->models_hmap = gamecache_new_models_hmap();
    }
}

void
gamecache_animbaseframes_cache_clear(struct GameCache* gc)
{
    if( !gc )
        return;
    if( gc->animbaseframes_hmap )
    {
        dashmap_free_entries(gc->animbaseframes_hmap, free_animbaseframes_entry);
        gc->animbaseframes_hmap =
            gamecache_create_hmap(sizeof(int), sizeof(struct AnimbaseframesEntry), 64);
    }
    if( gc->animframes_reftable )
    {
        dashmap_free_entries(gc->animframes_reftable, NULL);
        gc->animframes_reftable = NULL;
    }
    gamecache_init_reftables_if_needed(gc);
}

void
gamecache_scenery_config_clear(struct GameCache* gc)
{
    if( !gc )
        return;
    if( gc->config_loc_hmap )
        dashmap_free_entries(gc->config_loc_hmap, free_config_loc_entry);
    gc->config_loc_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct ConfigLocEntry), 64);
}

void
gamecache_sequences_clear(struct GameCache* gc)
{
    if( !gc )
        return;
    if( gc->sequences_hmap )
        dashmap_free_entries(gc->sequences_hmap, free_sequence_entry);
    gc->sequences_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct SequenceEntry), 64);
}

void
gamecache_floortypes_clear(struct GameCache* gc)
{
    if( !gc )
        return;
    if( gc->flotype_hmap )
        dashmap_free_entries(gc->flotype_hmap, free_flotype_entry);
    gc->flotype_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct FlotypeEntry), 64);
}

void
gamecache_objects_clear(struct GameCache* gc)
{
    if( !gc )
        return;
    if( gc->obj_hmap )
        dashmap_free_entries(gc->obj_hmap, free_obj_entry);
    gc->obj_hmap = gamecache_create_hmap(sizeof(int), sizeof(struct ObjEntry), 64);
}

void
gamecache_component_cache_clear(struct GameCache* gc)
{
    if( !gc )
        return;

    dashmap_free_entries(gc->component_hmap, free_component_entry);
    gc->component_hmap = gamecache_create_hmap(
        sizeof(int), sizeof(struct ComponentEntry), GAMECACHE_HMAP_INITIAL_CAPACITY);
}

/* ---------------------------------------------------------------------------
 * Flotype (overlay)
 * -------------------------------------------------------------------------*/

void
gamecache_add_flotype(struct GameCache* gc, int flotype_id, struct GameCacheOverlay* flotype)
{
    struct FlotypeEntry* e =
        (struct FlotypeEntry*)dashmap_search(gc->flotype_hmap, &flotype_id, DASHMAP_INSERT);
    assert(e && "Flotype must be inserted into hmap");
    e->id = flotype_id;
    e->flotype = flotype;
    gamecache_maybe_grow_hmap(gc->flotype_hmap);
}

struct GameCacheOverlay*
gamecache_get_flotype(struct GameCache* gc, int flotype_id)
{
    struct FlotypeEntry* e =
        (struct FlotypeEntry*)dashmap_search(gc->flotype_hmap, &flotype_id, DASHMAP_FIND);
    return e ? e->flotype : NULL;
}

struct DashMapIter*
gamecache_iter_new_flotypes(struct GameCache* gc)
{
    return dashmap_iter_new(gc->flotype_hmap);
}

struct GameCacheOverlay*
gamecache_iter_next_flotype(struct DashMapIter* iter)
{
    struct FlotypeEntry* e = (struct FlotypeEntry*)dashmap_iter_next(iter);
    return e ? e->flotype : NULL;
}

/* ---------------------------------------------------------------------------
 * Texture reftable
 * -------------------------------------------------------------------------*/

void
gamecache_add_texture_ref(struct GameCache* gc, int texture_id)
{
    struct TextureRefEntry* e = (struct TextureRefEntry*)dashmap_search(
        gc->textures_reftable, &texture_id, DASHMAP_INSERT);
    assert(e && "Texture ref must be inserted into reftable");
    e->id = texture_id;
    gamecache_maybe_grow_hmap(gc->textures_reftable);
}

bool
gamecache_has_texture_ref(struct GameCache* gc, int texture_id)
{
    struct TextureRefEntry* e = (struct TextureRefEntry*)dashmap_search(
        gc->textures_reftable, &texture_id, DASHMAP_FIND);
    return e != NULL;
}

/* ---------------------------------------------------------------------------
 * Font reftable
 * -------------------------------------------------------------------------*/

void
gamecache_add_font_ref(struct GameCache* gc, const char* font_name, int uiscene_font_id)
{
    char buffer[GAMECACHE_FONT_NAME_MAX] = { 0 };
    strncpy(buffer, font_name, GAMECACHE_FONT_NAME_MAX);
    struct FontRefEntry* e =
        (struct FontRefEntry*)dashmap_search(gc->fonts_reftable, buffer, DASHMAP_INSERT);
    assert(e && "Font ref must be inserted into reftable");
    memset(e->name, 0, sizeof(e->name));
    strncpy(e->name, font_name, GAMECACHE_FONT_NAME_MAX);
    e->uiscene_font_id = uiscene_font_id;
    gamecache_maybe_grow_hmap(gc->fonts_reftable);
}

int
gamecache_get_font_ref_id(struct GameCache* gc, const char* font_name)
{
    char buffer[GAMECACHE_FONT_NAME_MAX] = { 0 };
    strncpy(buffer, font_name, GAMECACHE_FONT_NAME_MAX);
    struct FontRefEntry* e =
        (struct FontRefEntry*)dashmap_search(gc->fonts_reftable, buffer, DASHMAP_FIND);
    return e ? e->uiscene_font_id : -1;
}

struct DashMapIter*
gamecache_iter_new_fonts_reftable(struct GameCache* gc)
{
    return dashmap_iter_new(gc->fonts_reftable);
}

bool
gamecache_iter_next_font_ref(
    struct DashMapIter* iter,
    char* out_name,
    int out_name_cap,
    int* out_uiscene_font_id)
{
    struct FontRefEntry* e = (struct FontRefEntry*)dashmap_iter_next(iter);
    if( !e )
        return false;
    if( out_name && out_name_cap > 0 )
    {
        int i = 0;
        int lim = (int)sizeof(e->name);
        if( lim > out_name_cap - 1 )
            lim = out_name_cap - 1;
        for( ; i < lim; i++ )
        {
            char c = e->name[i];
            out_name[i] = c;
            if( c == '\0' )
                break;
        }
        out_name[i] = '\0';
    }
    if( out_uiscene_font_id )
        *out_uiscene_font_id = e->uiscene_font_id;
    return true;
}

/* ---------------------------------------------------------------------------
 * Sprite reftable
 * -------------------------------------------------------------------------*/

void
gamecache_add_sprite_ref(struct GameCache* gc, const char* name, int uiscene_element_id)
{
    char buffer[64] = { 0 };
    strncpy(buffer, name, sizeof(buffer));
    struct SpriteRefEntry* e =
        (struct SpriteRefEntry*)dashmap_search(gc->sprites_reftable, buffer, DASHMAP_INSERT);
    assert(e && "Sprite ref must be inserted into reftable");
    e->uiscene_element_id = uiscene_element_id;
    gamecache_maybe_grow_hmap(gc->sprites_reftable);
}

int
gamecache_get_sprite_element_id(struct GameCache* gc, const char* name)
{
    char buffer[64] = { 0 };
    strncpy(buffer, name, sizeof(buffer));
    struct SpriteRefEntry* e =
        (struct SpriteRefEntry*)dashmap_search(gc->sprites_reftable, buffer, DASHMAP_FIND);
    return e ? e->uiscene_element_id : -1;
}

/* ---------------------------------------------------------------------------
 * Event buffer
 * -------------------------------------------------------------------------*/

bool
gamecache_eventbuffer_is_empty(struct GameCache* gc)
{
    return !gc || gc->eventbuffer_count <= 0;
}

bool
gamecache_eventbuffer_pop(struct GameCache* gc, struct GameCacheEvent* out_event)
{
    if( !gc || !out_event || gc->eventbuffer_count <= 0 )
        return false;
    *out_event = gc->eventbuffer[gc->eventbuffer_head];
    gc->eventbuffer_head = (gc->eventbuffer_head + 1) % gc->eventbuffer_capacity;
    gc->eventbuffer_count--;
    return true;
}

void
gamecache_eventbuffer_clear(struct GameCache* gc)
{
    if( !gc )
        return;
    gc->eventbuffer_head = 0;
    gc->eventbuffer_tail = 0;
    gc->eventbuffer_count = 0;
}

/* ---------------------------------------------------------------------------
 * Scenery (map locs)
 * -------------------------------------------------------------------------*/

void
gamecache_add_scenery(struct GameCache* gc, int mapx, int mapz, struct GameCacheMapLocs* locs)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct SceneryEntry* e =
        (struct SceneryEntry*)dashmap_search(gc->scenery_hmap, &mapxz, DASHMAP_INSERT);
    assert(e && "Scenery must be inserted into hmap");
    e->id = mapxz;
    e->mapx = mapx;
    e->mapz = mapz;
    e->locs = locs;
    gamecache_maybe_grow_hmap(gc->scenery_hmap);
}

struct GameCacheMapLocs*
gamecache_get_scenery(struct GameCache* gc, int mapx, int mapz)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct SceneryEntry* e =
        (struct SceneryEntry*)dashmap_search(gc->scenery_hmap, &mapxz, DASHMAP_FIND);
    return e ? e->locs : NULL;
}

struct GameCacheMapLocs*
gamecache_iter_next_scenery(struct DashMapIter* iter)
{
    struct SceneryEntry* e = (struct SceneryEntry*)dashmap_iter_next(iter);
    return e ? e->locs : NULL;
}

struct DashMapIter*
gamecache_iter_new_scenery(struct GameCache* gc)
{
    return dashmap_iter_new(gc->scenery_hmap);
}

/* ---------------------------------------------------------------------------
 * Models
 * -------------------------------------------------------------------------*/

void
gamecache_add_model(struct GameCache* gc, int model_id, struct GameCacheModel* model)
{
    struct ModelEntry* e =
        (struct ModelEntry*)dashmap_search(gc->models_hmap, &model_id, DASHMAP_INSERT);
    assert(e && "Model must be inserted into hmap");
    e->id = model_id;
    e->model = model;
    gamecache_maybe_grow_hmap(gc->models_hmap);
}

struct GameCacheModel*
gamecache_get_model(struct GameCache* gc, int model_id)
{
    struct ModelEntry* e =
        (struct ModelEntry*)dashmap_search(gc->models_hmap, &model_id, DASHMAP_FIND);
    return e ? e->model : NULL;
}

struct DashMapIter*
gamecache_iter_new_models(struct GameCache* gc)
{
    return dashmap_iter_new(gc->models_hmap);
}

struct GameCacheModel*
gamecache_iter_next_model(struct DashMapIter* iter)
{
    struct ModelEntry* e = (struct ModelEntry*)dashmap_iter_next(iter);
    return e ? e->model : NULL;
}

/* ---------------------------------------------------------------------------
 * IDK / obj / npc models
 * -------------------------------------------------------------------------*/

void
gamecache_add_idk_model(struct GameCache* gc, int idk_id, struct GameCacheModel* model)
{
    struct IdkModelEntry* e =
        (struct IdkModelEntry*)dashmap_search(gc->idk_models_hmap, &idk_id, DASHMAP_INSERT);
    assert(e && "Idk model must be inserted into hmap");
    e->id = idk_id;
    e->model = model;
    gamecache_maybe_grow_hmap(gc->idk_models_hmap);
}

struct GameCacheModel*
gamecache_get_idk_model(struct GameCache* gc, int idk_id)
{
    struct IdkModelEntry* e =
        (struct IdkModelEntry*)dashmap_search(gc->idk_models_hmap, &idk_id, DASHMAP_FIND);
    return e ? e->model : NULL;
}

void
gamecache_add_obj_model(struct GameCache* gc, int obj_id, struct GameCacheModel* model)
{
    struct ObjModelEntry* e =
        (struct ObjModelEntry*)dashmap_search(gc->obj_models_hmap, &obj_id, DASHMAP_INSERT);
    assert(e && "Obj model must be inserted into hmap");
    e->id = obj_id;
    e->model = model;
    gamecache_maybe_grow_hmap(gc->obj_models_hmap);
}

struct GameCacheModel*
gamecache_get_obj_model(struct GameCache* gc, int obj_id)
{
    struct ObjModelEntry* e =
        (struct ObjModelEntry*)dashmap_search(gc->obj_models_hmap, &obj_id, DASHMAP_FIND);
    return e ? e->model : NULL;
}

void
gamecache_add_npc_model(struct GameCache* gc, int npc_id, struct GameCacheModel* model)
{
    struct NpcModelEntry* e =
        (struct NpcModelEntry*)dashmap_search(gc->npc_models_hmap, &npc_id, DASHMAP_INSERT);
    assert(e && "Npc model must be inserted into hmap");
    e->id = npc_id;
    e->model = model;
    gamecache_maybe_grow_hmap(gc->npc_models_hmap);
}

struct GameCacheModel*
gamecache_get_npc_model(struct GameCache* gc, int npc_id)
{
    struct NpcModelEntry* e =
        (struct NpcModelEntry*)dashmap_search(gc->npc_models_hmap, &npc_id, DASHMAP_FIND);
    return e ? e->model : NULL;
}

/* ---------------------------------------------------------------------------
 * NPC config
 * -------------------------------------------------------------------------*/

void
gamecache_add_npc(struct GameCache* gc, int npc_id, struct GameCacheNpc* npc)
{
    struct NpcEntry* e =
        (struct NpcEntry*)dashmap_search(gc->npc_hmap, &npc_id, DASHMAP_INSERT);
    assert(e && "Npc must be inserted into hmap");
    e->id = npc_id;
    e->npc = npc;
    gamecache_maybe_grow_hmap(gc->npc_hmap);
}

struct GameCacheNpc*
gamecache_get_npc(struct GameCache* gc, int npc_id)
{
    struct NpcEntry* e = (struct NpcEntry*)dashmap_search(gc->npc_hmap, &npc_id, DASHMAP_FIND);
    return e ? e->npc : NULL;
}

struct DashMapIter*
gamecache_iter_new_npcs(struct GameCache* gc)
{
    return dashmap_iter_new(gc->npc_hmap);
}

struct GameCacheNpc*
gamecache_iter_next_npc(struct DashMapIter* iter)
{
    struct NpcEntry* e = (struct NpcEntry*)dashmap_iter_next(iter);
    return e ? e->npc : NULL;
}

/* ---------------------------------------------------------------------------
 * Config locs
 * -------------------------------------------------------------------------*/

void
gamecache_add_config_loc(struct GameCache* gc, int config_loc_id, struct GameCacheLoc* config_loc)
{
    struct ConfigLocEntry* e =
        (struct ConfigLocEntry*)dashmap_search(gc->config_loc_hmap, &config_loc_id, DASHMAP_INSERT);
    assert(e && "Config loc must be inserted into hmap");
    e->id = config_loc_id;
    e->config_loc = config_loc;
    gamecache_maybe_grow_hmap(gc->config_loc_hmap);
}

struct GameCacheLoc*
gamecache_iter_next_config_loc(struct DashMapIter* iter)
{
    struct ConfigLocEntry* e = (struct ConfigLocEntry*)dashmap_iter_next(iter);
    return e ? e->config_loc : NULL;
}

struct GameCacheLoc*
gamecache_get_config_loc(struct GameCache* gc, int config_loc_id)
{
    struct ConfigLocEntry* e = (struct ConfigLocEntry*)dashmap_search(
        gc->config_loc_hmap, &config_loc_id, DASHMAP_FIND);
    return e ? e->config_loc : NULL;
}

struct DashMapIter*
gamecache_iter_new_config_locs(struct GameCache* gc)
{
    return dashmap_iter_new(gc->config_loc_hmap);
}

/* ---------------------------------------------------------------------------
 * Animframes / animbaseframes
 * -------------------------------------------------------------------------*/

void
gamecache_add_animframe_ref(
    struct GameCache* gc,
    int animframe_id,
    int animbaseframes_id,
    int frame_index)
{
    struct AnimframeRefEntry* e = (struct AnimframeRefEntry*)dashmap_search(
        gc->animframes_reftable, &animframe_id, DASHMAP_INSERT);
    assert(e && "Animframe ref must be inserted into reftable");
    e->id = animframe_id;
    e->animbaseframes_id = animbaseframes_id;
    e->frame_index = frame_index;
    gamecache_maybe_grow_hmap(gc->animframes_reftable);
}

struct GameCacheAnimframe*
gamecache_get_animframe(struct GameCache* gc, int animframe_id)
{
    struct AnimframeRefEntry* ref = (struct AnimframeRefEntry*)dashmap_search(
        gc->animframes_reftable, &animframe_id, DASHMAP_FIND);
    if( !ref )
        return NULL;
    struct GameCacheAnimBaseFrames* abf = gamecache_get_animbaseframes(gc, ref->animbaseframes_id);
    if( !abf || ref->frame_index < 0 || ref->frame_index >= abf->frame_count )
        return NULL;
    return &abf->frames[ref->frame_index];
}

void
gamecache_add_animbaseframes(
    struct GameCache* gc,
    int animbaseframes_id,
    struct GameCacheAnimBaseFrames* animbaseframes)
{
    struct AnimbaseframesEntry* e = (struct AnimbaseframesEntry*)dashmap_search(
        gc->animbaseframes_hmap, &animbaseframes_id, DASHMAP_INSERT);
    assert(e && "Animbaseframes must be inserted into hmap");
    e->id = animbaseframes_id;
    e->animbaseframes = animbaseframes;
    gamecache_maybe_grow_hmap(gc->animbaseframes_hmap);
}

struct GameCacheAnimBaseFrames*
gamecache_get_animbaseframes(struct GameCache* gc, int animbaseframes_id)
{
    struct AnimbaseframesEntry* e = (struct AnimbaseframesEntry*)dashmap_search(
        gc->animbaseframes_hmap, &animbaseframes_id, DASHMAP_FIND);
    return e ? e->animbaseframes : NULL;
}

struct DashMapIter*
gamecache_iter_new_animbaseframes(struct GameCache* gc)
{
    return dashmap_iter_new(gc->animbaseframes_hmap);
}

struct GameCacheAnimBaseFrames*
gamecache_iter_next_animbaseframes(struct DashMapIter* iter)
{
    struct AnimbaseframesEntry* e = (struct AnimbaseframesEntry*)dashmap_iter_next(iter);
    return e ? e->animbaseframes : NULL;
}

/* ---------------------------------------------------------------------------
 * Sequences
 * -------------------------------------------------------------------------*/

void
gamecache_add_sequence(struct GameCache* gc, int sequence_id, struct GameCacheSequence* sequence)
{
    struct SequenceEntry* e =
        (struct SequenceEntry*)dashmap_search(gc->sequences_hmap, &sequence_id, DASHMAP_INSERT);
    assert(e && "Sequence must be inserted into hmap");
    e->id = sequence_id;
    e->sequence = sequence;
    gamecache_maybe_grow_hmap(gc->sequences_hmap);
}

struct GameCacheSequence*
gamecache_get_sequence(struct GameCache* gc, int sequence_id)
{
    struct SequenceEntry* e =
        (struct SequenceEntry*)dashmap_search(gc->sequences_hmap, &sequence_id, DASHMAP_FIND);
    return e ? e->sequence : NULL;
}

struct DashMapIter*
gamecache_iter_new_sequences(struct GameCache* gc)
{
    return dashmap_iter_new(gc->sequences_hmap);
}

struct GameCacheSequence*
gamecache_iter_next_sequence(struct DashMapIter* iter)
{
    struct SequenceEntry* e = (struct SequenceEntry*)dashmap_iter_next(iter);
    return e ? e->sequence : NULL;
}

/* ---------------------------------------------------------------------------
 * IDK config
 * -------------------------------------------------------------------------*/

void
gamecache_add_idk(struct GameCache* gc, int idk_id, struct GameCacheIdk* idk)
{
    struct IdkEntry* e =
        (struct IdkEntry*)dashmap_search(gc->idk_hmap, &idk_id, DASHMAP_INSERT);
    assert(e && "Idk must be inserted into hmap");
    e->id = idk_id;
    e->idk = idk;
    gamecache_maybe_grow_hmap(gc->idk_hmap);
}

struct GameCacheIdk*
gamecache_get_idk(struct GameCache* gc, int idk_id)
{
    struct IdkEntry* e = (struct IdkEntry*)dashmap_search(gc->idk_hmap, &idk_id, DASHMAP_FIND);
    return e ? e->idk : NULL;
}

struct GameCacheIdk*
gamecache_iter_next_idk(struct DashMapIter* iter)
{
    struct IdkEntry* e = (struct IdkEntry*)dashmap_iter_next(iter);
    return e ? e->idk : NULL;
}

struct DashMapIter*
gamecache_iter_new_idks(struct GameCache* gc)
{
    return dashmap_iter_new(gc->idk_hmap);
}

/* ---------------------------------------------------------------------------
 * Obj config
 * -------------------------------------------------------------------------*/

void
gamecache_add_obj(struct GameCache* gc, int obj_id, struct GameCacheObj* obj)
{
    struct ObjEntry* existing =
        (struct ObjEntry*)dashmap_search(gc->obj_hmap, &obj_id, DASHMAP_FIND);
    if( existing && existing->obj )
        gamecache_obj_free(existing->obj);

    struct ObjEntry* e =
        (struct ObjEntry*)dashmap_search(gc->obj_hmap, &obj_id, DASHMAP_INSERT);
    assert(e && "Obj must be inserted into hmap");
    e->id = obj_id;
    e->obj = obj;
    gamecache_maybe_grow_hmap(gc->obj_hmap);
}

struct GameCacheObj*
gamecache_get_obj(struct GameCache* gc, int obj_id)
{
    struct ObjEntry* e = (struct ObjEntry*)dashmap_search(gc->obj_hmap, &obj_id, DASHMAP_FIND);
    return e ? e->obj : NULL;
}

struct GameCacheObj*
gamecache_iter_next_obj(struct DashMapIter* iter)
{
    struct ObjEntry* e = (struct ObjEntry*)dashmap_iter_next(iter);
    return e ? e->obj : NULL;
}

struct DashMapIter*
gamecache_iter_new_objs(struct GameCache* gc)
{
    return dashmap_iter_new(gc->obj_hmap);
}

/* ---------------------------------------------------------------------------
 * Map terrain
 * -------------------------------------------------------------------------*/

void
gamecache_add_map_terrain(
    struct GameCache* gc,
    int mapx,
    int mapz,
    struct GameCacheTerrainChunk* map_terrain)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* e =
        (struct MapTerrainEntry*)dashmap_search(gc->map_terrains_hmap, &mapxz, DASHMAP_INSERT);
    assert(e && "Map terrain must be inserted into hmap");
    e->id = mapxz;
    e->mapx = mapx;
    e->mapz = mapz;
    e->map_terrain = map_terrain;
    gamecache_maybe_grow_hmap(gc->map_terrains_hmap);
}

struct GameCacheTerrainChunk*
gamecache_get_map_terrain(struct GameCache* gc, int mapx, int mapz)
{
    int mapxz = MAPREGIONXZ(mapx, mapz);
    struct MapTerrainEntry* e =
        (struct MapTerrainEntry*)dashmap_search(gc->map_terrains_hmap, &mapxz, DASHMAP_FIND);
    return e ? e->map_terrain : NULL;
}

/* ---------------------------------------------------------------------------
 * Components
 * -------------------------------------------------------------------------*/

void
gamecache_add_component(
    struct GameCache* gc,
    int component_id,
    struct GameCacheComponent* component)
{
    struct ComponentEntry* e =
        (struct ComponentEntry*)dashmap_search(gc->component_hmap, &component_id, DASHMAP_INSERT);
    assert(e && "Component must be inserted into hmap");
    e->id = component_id;
    e->component = component;
    gamecache_maybe_grow_hmap(gc->component_hmap);
}

struct GameCacheComponent*
gamecache_get_component(struct GameCache* gc, int component_id)
{
    struct ComponentEntry* e = (struct ComponentEntry*)dashmap_search(
        gc->component_hmap, &component_id, DASHMAP_FIND);
    return e ? e->component : NULL;
}

struct DashMapIter*
gamecache_component_iter_new(struct GameCache* gc)
{
    return dashmap_iter_new(gc->component_hmap);
}

struct GameCacheComponent*
gamecache_component_iter_next(struct DashMapIter* iter, int* id_out)
{
    struct ComponentEntry* e = (struct ComponentEntry*)dashmap_iter_next(iter);
    if( !e )
        return NULL;
    if( id_out )
        *id_out = e->id;
    return e->component;
}

/* ---------------------------------------------------------------------------
 * Component sprite reftable
 * -------------------------------------------------------------------------*/

void
gamecache_add_component_sprite_ref(
    struct GameCache* gc,
    const char* sprite_name,
    int uiscene_element_id)
{
    char buffer[64] = { 0 };
    strncpy(buffer, sprite_name, sizeof(buffer));
    struct ComponentSpriteRefEntry* e = (struct ComponentSpriteRefEntry*)dashmap_search(
        gc->component_sprites_reftable, buffer, DASHMAP_INSERT);
    assert(e && "Component sprite ref must be inserted into reftable");
    e->uiscene_element_id = uiscene_element_id;
    gamecache_maybe_grow_hmap(gc->component_sprites_reftable);
}

int
gamecache_get_component_sprite_element_id(struct GameCache* gc, const char* sprite_name)
{
    char buffer[64] = { 0 };
    strncpy(buffer, sprite_name, sizeof(buffer));
    struct ComponentSpriteRefEntry* e = (struct ComponentSpriteRefEntry*)dashmap_search(
        gc->component_sprites_reftable, buffer, DASHMAP_FIND);
    return e ? e->uiscene_element_id : -1;
}

/* ---------------------------------------------------------------------------
 * SpotAnim
 * -------------------------------------------------------------------------*/

void
gamecache_add_spotanim(struct GameCache* gc, int spotanim_id, struct GameCacheSpotAnim* spotanim)
{
    struct SpotAnimEntry* existing =
        (struct SpotAnimEntry*)dashmap_search(gc->spotanim_hmap, &spotanim_id, DASHMAP_FIND);
    if( existing && existing->spotanim )
        gamecache_spotanim_free(existing->spotanim);

    struct SpotAnimEntry* e =
        (struct SpotAnimEntry*)dashmap_search(gc->spotanim_hmap, &spotanim_id, DASHMAP_INSERT);
    assert(e && "SpotAnim must be insertable into hmap");
    e->id = spotanim_id;
    e->spotanim = spotanim;
    gamecache_maybe_grow_hmap(gc->spotanim_hmap);
}

struct GameCacheSpotAnim*
gamecache_get_spotanim(struct GameCache* gc, int spotanim_id)
{
    struct SpotAnimEntry* e =
        (struct SpotAnimEntry*)dashmap_search(gc->spotanim_hmap, &spotanim_id, DASHMAP_FIND);
    return e ? e->spotanim : NULL;
}

/* ---------------------------------------------------------------------------
 * GameCacheModel utilities (copy / merge / free)
 * -------------------------------------------------------------------------*/

void
gamecache_model_free(struct GameCacheModel* model)
{
    if( !model )
        return;
    free(model->face_bone_map);
    free(model->vertices_x);
    free(model->vertices_y);
    free(model->vertices_z);
    free(model->vertex_bone_map);
    free(model->face_indices_a);
    free(model->face_indices_b);
    free(model->face_indices_c);
    free(model->face_colors);
    free(model->face_priorities);
    free(model->face_alphas);
    free(model->face_infos);
    free(model->textured_p_coordinate);
    free(model->textured_m_coordinate);
    free(model->textured_n_coordinate);
    free(model->face_texture_coords);
    free(model->textureRenderTypes);
    free(model->face_textures);
    free(model);
}

struct GameCacheModel*
gamecache_model_new_copy(struct GameCacheModel* model)
{
    struct GameCacheModel* copy = (struct GameCacheModel*)malloc(sizeof(struct GameCacheModel));
    memset(copy, 0, sizeof(struct GameCacheModel));

    copy->_id = model->_id;
    copy->_model_type = model->_model_type;
    copy->_flags = model->_flags;

    copy->vertex_count = model->vertex_count;
    copy->vertices_x = (int*)malloc((size_t)model->vertex_count * sizeof(int));
    copy->vertices_y = (int*)malloc((size_t)model->vertex_count * sizeof(int));
    copy->vertices_z = (int*)malloc((size_t)model->vertex_count * sizeof(int));
    memcpy(copy->vertices_x, model->vertices_x, (size_t)model->vertex_count * sizeof(int));
    memcpy(copy->vertices_y, model->vertices_y, (size_t)model->vertex_count * sizeof(int));
    memcpy(copy->vertices_z, model->vertices_z, (size_t)model->vertex_count * sizeof(int));

    if( model->vertex_bone_map )
    {
        copy->vertex_bone_map = (uint8_t*)malloc((size_t)model->vertex_count * sizeof(uint8_t));
        memcpy(copy->vertex_bone_map, model->vertex_bone_map,
               (size_t)model->vertex_count * sizeof(uint8_t));
    }
    if( model->face_bone_map )
    {
        copy->face_bone_map = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(copy->face_bone_map, model->face_bone_map,
               (size_t)model->face_count * sizeof(uint8_t));
    }

    copy->face_count = model->face_count;
    if( model->face_indices_a )
    {
        copy->face_indices_a = (int*)malloc((size_t)model->face_count * sizeof(int));
        memcpy(copy->face_indices_a, model->face_indices_a,
               (size_t)model->face_count * sizeof(int));
    }
    if( model->face_indices_b )
    {
        copy->face_indices_b = (int*)malloc((size_t)model->face_count * sizeof(int));
        memcpy(copy->face_indices_b, model->face_indices_b,
               (size_t)model->face_count * sizeof(int));
    }
    if( model->face_indices_c )
    {
        copy->face_indices_c = (int*)malloc((size_t)model->face_count * sizeof(int));
        memcpy(copy->face_indices_c, model->face_indices_c,
               (size_t)model->face_count * sizeof(int));
    }
    if( model->face_alphas )
    {
        copy->face_alphas = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(copy->face_alphas, model->face_alphas, (size_t)model->face_count * sizeof(uint8_t));
    }
    if( model->face_infos )
    {
        copy->face_infos = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(copy->face_infos, model->face_infos, (size_t)model->face_count * sizeof(uint8_t));
    }
    if( model->face_priorities )
    {
        copy->face_priorities = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(copy->face_priorities, model->face_priorities,
               (size_t)model->face_count * sizeof(uint8_t));
    }
    else if( model->face_count > 0 )
    {
        assert(
            model->model_priority != 255 &&
            "gamecache_model_new_copy: faces exist but no face_priorities and model_priority==255");
        copy->face_priorities = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memset(
            copy->face_priorities,
            model->model_priority,
            (size_t)model->face_count * sizeof(uint8_t));
    }
    if( model->face_colors )
    {
        copy->face_colors = (uint16_t*)malloc((size_t)model->face_count * sizeof(uint16_t));
        memcpy(copy->face_colors, model->face_colors,
               (size_t)model->face_count * sizeof(uint16_t));
    }

    copy->model_priority = model->model_priority;
    copy->textured_face_count = model->textured_face_count;

    if( model->textured_p_coordinate )
    {
        copy->textured_p_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));
        memcpy(copy->textured_p_coordinate, model->textured_p_coordinate,
               (size_t)model->textured_face_count * sizeof(uint16_t));
    }
    if( model->textured_m_coordinate )
    {
        copy->textured_m_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));
        memcpy(copy->textured_m_coordinate, model->textured_m_coordinate,
               (size_t)model->textured_face_count * sizeof(uint16_t));
    }
    if( model->textured_n_coordinate )
    {
        copy->textured_n_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));
        memcpy(copy->textured_n_coordinate, model->textured_n_coordinate,
               (size_t)model->textured_face_count * sizeof(uint16_t));
    }
    if( model->face_textures )
    {
        copy->face_textures = (int16_t*)malloc((size_t)model->face_count * sizeof(int16_t));
        memcpy(copy->face_textures, model->face_textures,
               (size_t)model->face_count * sizeof(int16_t));
    }
    if( model->face_texture_coords )
    {
        copy->face_texture_coords = (int16_t*)malloc((size_t)model->face_count * sizeof(int16_t));
        memcpy(copy->face_texture_coords, model->face_texture_coords,
               (size_t)model->face_count * sizeof(int16_t));
    }
    if( model->textureRenderTypes )
    {
        copy->textureRenderTypes =
            (unsigned char*)malloc((size_t)model->textured_face_count * sizeof(unsigned char));
        memcpy(copy->textureRenderTypes, model->textureRenderTypes,
               (size_t)model->textured_face_count * sizeof(unsigned char));
    }

    return copy;
}

static int
gcmodel_copy_vertex(struct GameCacheModel* dst, struct GameCacheModel* src, int vi)
{
    int vx = src->vertices_x[vi];
    int vy = src->vertices_y[vi];
    int vz = src->vertices_z[vi];

    for( int i = 0; i < dst->vertex_count; i++ )
    {
        if( dst->vertices_x[i] == vx && dst->vertices_y[i] == vy && dst->vertices_z[i] == vz )
            return i;
    }

    int idx = dst->vertex_count;
    dst->vertices_x[idx] = vx;
    dst->vertices_y[idx] = vy;
    dst->vertices_z[idx] = vz;

    if( dst->vertex_bone_map && src->vertex_bone_map )
        dst->vertex_bone_map[idx] = src->vertex_bone_map[vi];

    dst->vertex_count++;
    return idx;
}

struct GameCacheModel*
gamecache_model_new_merge(struct GameCacheModel** models, int model_count)
{
    struct GameCacheModel* out = (struct GameCacheModel*)malloc(sizeof(struct GameCacheModel));
    memset(out, 0, sizeof(struct GameCacheModel));

    out->_model_type = -1;
    out->_id = -1;

    int vertex_count = 0;
    int face_count = 0;
    int textured_face_count = 0;

    bool has_face_infos = false;
    bool has_face_colors = false;
    bool has_face_alphas = false;
    bool has_face_textures = false;
    bool has_vertex_bones = false;
    bool has_face_bones = false;

    for( int i = 0; i < model_count; i++ )
    {
        out->_flags |= models[i]->_flags;

        vertex_count += models[i]->vertex_count;
        face_count += models[i]->face_count;
        textured_face_count += models[i]->textured_face_count;

        if( models[i]->face_infos )
            has_face_infos = true;
        if( models[i]->face_colors )
            has_face_colors = true;
        if( models[i]->face_alphas )
            has_face_alphas = true;
        if( models[i]->face_textures )
            has_face_textures = true;
        if( models[i]->vertex_bone_map )
            has_vertex_bones = true;
        if( models[i]->face_bone_map )
            has_face_bones = true;
    }

#define GCM_ALLOC_ZERO(ptr, type, n) \
    do { \
        (ptr) = (type*)malloc((size_t)(n) * sizeof(type)); \
        memset((ptr), 0, (size_t)(n) * sizeof(type)); \
    } while( 0 )

    GCM_ALLOC_ZERO(out->vertices_x, int, vertex_count);
    GCM_ALLOC_ZERO(out->vertices_y, int, vertex_count);
    GCM_ALLOC_ZERO(out->vertices_z, int, vertex_count);
    GCM_ALLOC_ZERO(out->face_indices_a, int, face_count);
    GCM_ALLOC_ZERO(out->face_indices_b, int, face_count);
    GCM_ALLOC_ZERO(out->face_indices_c, int, face_count);

    if( has_face_alphas )
        GCM_ALLOC_ZERO(out->face_alphas, uint8_t, face_count);
    if( has_face_infos )
        GCM_ALLOC_ZERO(out->face_infos, uint8_t, face_count);
    if( face_count > 0 )
        GCM_ALLOC_ZERO(out->face_priorities, uint8_t, face_count);
    if( has_face_colors )
        GCM_ALLOC_ZERO(out->face_colors, uint16_t, face_count);
    if( has_face_textures )
    {
        GCM_ALLOC_ZERO(out->textured_p_coordinate, uint16_t, textured_face_count);
        GCM_ALLOC_ZERO(out->textured_m_coordinate, uint16_t, textured_face_count);
        GCM_ALLOC_ZERO(out->textured_n_coordinate, uint16_t, textured_face_count);
        GCM_ALLOC_ZERO(out->face_textures, int16_t, face_count);
        GCM_ALLOC_ZERO(out->face_texture_coords, int16_t, face_count);
        GCM_ALLOC_ZERO(out->textureRenderTypes, unsigned char, textured_face_count);
    }
    if( has_vertex_bones )
        GCM_ALLOC_ZERO(out->vertex_bone_map, uint8_t, vertex_count);
    if( has_face_bones )
        GCM_ALLOC_ZERO(out->face_bone_map, uint8_t, face_count);

#undef GCM_ALLOC_ZERO

    for( int i = 0; i < model_count; i++ )
    {
        int fc = models[i]->face_count;
        for( int j = 0; j < fc; j++ )
        {
            if( out->face_alphas && models[i]->face_alphas )
                out->face_alphas[out->face_count] = models[i]->face_alphas[j];
            if( out->face_infos && models[i]->face_infos )
                out->face_infos[out->face_count] = models[i]->face_infos[j];
            if( out->face_priorities )
            {
                uint8_t fp = 0;
                if( models[i]->face_priorities )
                    fp = models[i]->face_priorities[j];
                else if( models[i]->model_priority != 0 && models[i]->model_priority != 255 )
                    fp = models[i]->model_priority;
                out->face_priorities[out->face_count] = fp;
            }
            if( out->face_colors && models[i]->face_colors )
                out->face_colors[out->face_count] = models[i]->face_colors[j];
            if( out->face_bone_map )
            {
                if( models[i]->face_bone_map )
                    out->face_bone_map[out->face_count] = models[i]->face_bone_map[j];
                else
                    out->face_bone_map[out->face_count] = (uint8_t)255;
            }

            int ia = gcmodel_copy_vertex(out, models[i], models[i]->face_indices_a[j]);
            int ib = gcmodel_copy_vertex(out, models[i], models[i]->face_indices_b[j]);
            int ic = gcmodel_copy_vertex(out, models[i], models[i]->face_indices_c[j]);

            out->face_indices_a[out->face_count] = ia;
            out->face_indices_b[out->face_count] = ib;
            out->face_indices_c[out->face_count] = ic;

            if( has_face_textures )
            {
                out->face_texture_coords[out->face_count] =
                    models[i]->face_texture_coords ? models[i]->face_texture_coords[j] : -1;
                out->face_textures[out->face_count] =
                    models[i]->face_textures ? models[i]->face_textures[j] : -1;
            }

            out->face_count++;
        }

        for( int j = 0; j < models[i]->textured_face_count; j++ )
        {
            int vp = 0, vm = 0, vn = 0;
            if( out->textured_p_coordinate && models[i]->textured_p_coordinate )
                vp = gcmodel_copy_vertex(out, models[i], (int)models[i]->textured_p_coordinate[j]);
            if( out->textured_m_coordinate && models[i]->textured_m_coordinate )
                vm = gcmodel_copy_vertex(out, models[i], (int)models[i]->textured_m_coordinate[j]);
            if( out->textured_n_coordinate && models[i]->textured_n_coordinate )
                vn = gcmodel_copy_vertex(out, models[i], (int)models[i]->textured_n_coordinate[j]);

            if( out->textured_p_coordinate )
                out->textured_p_coordinate[out->textured_face_count] = (uint16_t)vp;
            if( out->textured_m_coordinate )
                out->textured_m_coordinate[out->textured_face_count] = (uint16_t)vm;
            if( out->textured_n_coordinate )
                out->textured_n_coordinate[out->textured_face_count] = (uint16_t)vn;

            if( out->face_texture_coords && models[i]->face_texture_coords )
                out->face_texture_coords[out->textured_face_count] =
                    models[i]->face_texture_coords[j];

            out->textured_face_count++;
        }
    }

    out->_flags &= ~GAMECACHE_MODEL_FLAG_SHARED;
    return out;
}

/* ---------------------------------------------------------------------------
 * GameCacheModel in-place transforms
 * -------------------------------------------------------------------------*/

void
gamecache_model_transform_mirror(struct GameCacheModel* model)
{
    for( int v = 0; v < model->vertex_count; v++ )
        model->vertices_z[v] = -model->vertices_z[v];
    for( int f = 0; f < model->face_count; f++ )
    {
        int tmp = model->face_indices_a[f];
        model->face_indices_a[f] = model->face_indices_c[f];
        model->face_indices_c[f] = tmp;
    }
}

void
gamecache_model_transform_recolor(struct GameCacheModel* model, int color_src, int color_dst)
{
    uint16_t* fc = model->face_colors;
    if( !fc )
        return;
    for( int f = 0; f < model->face_count; f++ )
        if( fc[f] == (uint16_t)color_src )
            fc[f] = (uint16_t)color_dst;
}

void
gamecache_model_transform_retexture(struct GameCacheModel* model, int tex_src, int tex_dst)
{
    int16_t* ft = model->face_textures;
    if( !ft )
        return;
    for( int f = 0; f < model->face_count; f++ )
        if( ft[f] == (int16_t)tex_src )
            ft[f] = (int16_t)tex_dst;
}

void
gamecache_model_transform_scale(struct GameCacheModel* model, int x, int z, int height)
{
    for( int i = 0; i < model->vertex_count; i++ )
    {
        model->vertices_x[i] = model->vertices_x[i] * x / 128;
        model->vertices_y[i] = model->vertices_y[i] * height / 128;
        model->vertices_z[i] = model->vertices_z[i] * z / 128;
    }
}

void
gamecache_model_transform_orient(struct GameCacheModel* model, int orientation)
{
    orientation &= 3;
    while( orientation-- > 0 )
    {
        for( int v = 0; v < model->vertex_count; v++ )
        {
            int tmp = model->vertices_x[v];
            model->vertices_x[v] = model->vertices_z[v];
            model->vertices_z[v] = -tmp;
        }
    }
}

void
gamecache_model_transform_translate(struct GameCacheModel* model, int x, int y, int z)
{
    for( int i = 0; i < model->vertex_count; i++ )
    {
        model->vertices_x[i] += x;
        model->vertices_y[i] += y;
        model->vertices_z[i] += z;
    }
}
