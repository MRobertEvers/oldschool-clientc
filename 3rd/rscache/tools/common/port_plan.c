#include "port_plan.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char*
tool_strdup(const char* s)
{
    if( !s )
        return NULL;
    size_t n = strlen(s);
    char* d = malloc(n + 1);
    if( d )
        memcpy(d, s, n + 1);
    return d;
}

void
tool_neutral_npc_free(struct Tool_NeutralNpc* n)
{
    if( !n )
        return;
    free(n->name);
    free(n->desc);
    free(n->models);
    free(n->chathead_models);
    free(n->recolor_to_find);
    free(n->recolor_to_replace);
    free(n->retexture_to_find);
    free(n->retexture_to_replace);
    for( int i = 0; i < 5; i++ )
        free(n->actions[i]);
    memset(n, 0, sizeof(*n));
}

static void
set_anim(
    struct Tool_NeutralNpc* n,
    enum Tool_AnimSlot slot,
    int value,
    bool present)
{
    n->anim[slot] = value;
    n->anim_present[slot] = present;
}

int
tool_neutral_npc_from_dat2(
    struct Tool_Dat2Cache* c,
    const struct RSCache_Dat2ConfigNpc* npc,
    int npc_id,
    struct Tool_NeutralNpc* out)
{
    assert(c && npc && out);
    memset(out, 0, sizeof(*out));
    out->source_id = npc_id;
    out->name = tool_strdup(npc->name);
    out->size = npc->size;
    out->combat_level = npc->combat_level;
    out->width_scale = npc->width_scale;
    out->height_scale = npc->height_scale;
    out->ambient = npc->ambient;
    out->contrast = npc->contrast;
    out->is_minimap_visible = npc->is_minimap_visible;
    out->is_interactable = npc->is_interactable;
    out->rotation_flag = npc->rotation_flag;
    out->rotation_speed = npc->rotation_speed;
    out->category = npc->category;
    out->height = npc->height;

    if( npc->models_count > 0 && npc->models )
    {
        out->models = malloc((size_t)npc->models_count * sizeof(int));
        if( out->models )
        {
            memcpy(out->models, npc->models, (size_t)npc->models_count * sizeof(int));
            out->models_count = npc->models_count;
        }
    }
    if( npc->chathead_models_count > 0 && npc->chathead_models )
    {
        out->chathead_models =
            malloc((size_t)npc->chathead_models_count * sizeof(int));
        if( out->chathead_models )
        {
            memcpy(
                out->chathead_models,
                npc->chathead_models,
                (size_t)npc->chathead_models_count * sizeof(int));
            out->chathead_models_count = npc->chathead_models_count;
        }
    }
    if( npc->recolor_count > 0 && npc->recolor_to_find )
    {
        out->recolor_to_find = malloc((size_t)npc->recolor_count * sizeof(short));
        out->recolor_to_replace = malloc((size_t)npc->recolor_count * sizeof(short));
        if( out->recolor_to_find && out->recolor_to_replace )
        {
            memcpy(
                out->recolor_to_find,
                npc->recolor_to_find,
                (size_t)npc->recolor_count * sizeof(short));
            memcpy(
                out->recolor_to_replace,
                npc->recolor_to_replace,
                (size_t)npc->recolor_count * sizeof(short));
            out->recolor_count = npc->recolor_count;
        }
    }
    if( npc->retexture_count > 0 && npc->retexture_to_find )
    {
        out->retexture_to_find = malloc((size_t)npc->retexture_count * sizeof(short));
        out->retexture_to_replace = malloc((size_t)npc->retexture_count * sizeof(short));
        if( out->retexture_to_find && out->retexture_to_replace )
        {
            memcpy(
                out->retexture_to_find,
                npc->retexture_to_find,
                (size_t)npc->retexture_count * sizeof(short));
            memcpy(
                out->retexture_to_replace,
                npc->retexture_to_replace,
                (size_t)npc->retexture_count * sizeof(short));
            out->retexture_count = npc->retexture_count;
        }
    }
    for( int i = 0; i < 5; i++ )
        out->actions[i] = tool_strdup(npc->actions[i]);

    /* Presence: NewDecodeProfile defaults animations to -1, so "present" means
     * >= 0. Zero is a real sequence id that can appear on the wire. */
    set_anim(out, TOOL_ANIM_IDLE, npc->standing_animation, npc->standing_animation >= 0);
    set_anim(out, TOOL_ANIM_WALK, npc->walking_animation, npc->walking_animation >= 0);
    set_anim(
        out,
        TOOL_ANIM_IDLE_ROTATE_LEFT,
        npc->idle_rotate_left_animation,
        npc->idle_rotate_left_animation >= 0);
    set_anim(
        out,
        TOOL_ANIM_IDLE_ROTATE_RIGHT,
        npc->idle_rotate_right_animation,
        npc->idle_rotate_right_animation >= 0);
    set_anim(out, TOOL_ANIM_WALK_BACK, npc->rotate180_animation, npc->rotate180_animation >= 0);
    set_anim(out, TOOL_ANIM_WALK_LEFT, npc->rotate_left_animation, npc->rotate_left_animation >= 0);
    set_anim(
        out, TOOL_ANIM_WALK_RIGHT, npc->rotate_right_animation, npc->rotate_right_animation >= 0);
    set_anim(out, TOOL_ANIM_RUN, npc->run_animation, npc->run_animation >= 0);
    set_anim(
        out,
        TOOL_ANIM_RUN_ROTATE180,
        npc->run_rotate180_animation,
        npc->run_rotate180_animation >= 0);
    set_anim(
        out,
        TOOL_ANIM_RUN_ROTATE_LEFT,
        npc->run_rotate_left_animation,
        npc->run_rotate_left_animation >= 0);
    set_anim(
        out,
        TOOL_ANIM_RUN_ROTATE_RIGHT,
        npc->run_rotate_right_animation,
        npc->run_rotate_right_animation >= 0);
    set_anim(out, TOOL_ANIM_CRAWL, npc->crawl_animation, npc->crawl_animation >= 0);
    set_anim(
        out,
        TOOL_ANIM_CRAWL_ROTATE180,
        npc->crawl_rotate180_animation,
        npc->crawl_rotate180_animation >= 0);
    set_anim(
        out,
        TOOL_ANIM_CRAWL_ROTATE_LEFT,
        npc->crawl_rotate_left_animation,
        npc->crawl_rotate_left_animation >= 0);
    set_anim(
        out,
        TOOL_ANIM_CRAWL_ROTATE_RIGHT,
        npc->crawl_rotate_right_animation,
        npc->crawl_rotate_right_animation >= 0);

    out->bas_type_id = npc->bas_type_id;
    out->bas_present = npc->bas_type_id >= 0;
    if( out->bas_present && RSCache_IsRs2Dat2(&c->profile) )
    {
        struct RSCache_Dat2ConfigBas* bas = tool_dat2_bas_load(c, npc->bas_type_id);
        if( bas )
        {
            if( bas->idle_seq_id > 0 )
                set_anim(out, TOOL_ANIM_IDLE, bas->idle_seq_id, true);
            if( bas->walk_seq_id > 0 )
                set_anim(out, TOOL_ANIM_WALK, bas->walk_seq_id, true);
            if( bas->walk_back_seq_id > 0 )
                set_anim(out, TOOL_ANIM_WALK_BACK, bas->walk_back_seq_id, true);
            if( bas->walk_left_seq_id > 0 )
                set_anim(out, TOOL_ANIM_WALK_LEFT, bas->walk_left_seq_id, true);
            if( bas->walk_right_seq_id > 0 )
                set_anim(out, TOOL_ANIM_WALK_RIGHT, bas->walk_right_seq_id, true);
            RSCache_Dat2ConfigBasFree(bas);
        }
    }
    return 1;
}

int
tool_neutral_npc_from_dat1(
    const struct RSCache_Dat1ConfigNpc* npc,
    int npc_id,
    struct Tool_NeutralNpc* out)
{
    assert(npc && out);
    memset(out, 0, sizeof(*out));
    out->source_id = npc_id;
    out->name = tool_strdup(npc->name);
    out->desc = tool_strdup(npc->desc);
    out->size = npc->size;
    out->combat_level = npc->vislevel;
    out->width_scale = npc->resizeh;
    out->height_scale = npc->resizev;
    out->ambient = npc->ambient;
    out->contrast = npc->contrast;
    out->is_minimap_visible = npc->minimap;
    out->rotation_speed = npc->turnspeed;

    if( npc->models_count > 0 && npc->models )
    {
        out->models = malloc((size_t)npc->models_count * sizeof(int));
        if( out->models )
        {
            memcpy(out->models, npc->models, (size_t)npc->models_count * sizeof(int));
            out->models_count = npc->models_count;
        }
    }
    if( npc->heads_count > 0 && npc->heads )
    {
        out->chathead_models = malloc((size_t)npc->heads_count * sizeof(int));
        if( out->chathead_models )
        {
            memcpy(out->chathead_models, npc->heads, (size_t)npc->heads_count * sizeof(int));
            out->chathead_models_count = npc->heads_count;
        }
    }
    if( npc->recol_count > 0 && npc->recol_s )
    {
        out->recolor_to_find = malloc((size_t)npc->recol_count * sizeof(short));
        out->recolor_to_replace = malloc((size_t)npc->recol_count * sizeof(short));
        if( out->recolor_to_find && out->recolor_to_replace )
        {
            for( int i = 0; i < npc->recol_count; i++ )
            {
                out->recolor_to_find[i] = (short)npc->recol_s[i];
                out->recolor_to_replace[i] = (short)npc->recol_d[i];
            }
            out->recolor_count = npc->recol_count;
        }
    }
    for( int i = 0; i < 5; i++ )
        out->actions[i] = tool_strdup(npc->op[i]);

    if( npc->readyanim >= 0 )
        set_anim(out, TOOL_ANIM_IDLE, npc->readyanim, true);
    if( npc->walkanim >= 0 )
        set_anim(out, TOOL_ANIM_WALK, npc->walkanim, true);
    if( npc->walkanim_b >= 0 )
        set_anim(out, TOOL_ANIM_WALK_BACK, npc->walkanim_b, true);
    if( npc->walkanim_r >= 0 )
        set_anim(out, TOOL_ANIM_WALK_RIGHT, npc->walkanim_r, true);
    if( npc->walkanim_l >= 0 )
        set_anim(out, TOOL_ANIM_WALK_LEFT, npc->walkanim_l, true);
    return 1;
}

static void
add_warning(
    char*** warnings,
    int* warning_count,
    const char* msg)
{
    if( !warnings || !warning_count )
        return;
    char** n = realloc(*warnings, (size_t)(*warning_count + 1) * sizeof(char*));
    if( !n )
        return;
    *warnings = n;
    (*warnings)[*warning_count] = tool_strdup(msg);
    (*warning_count)++;
}

struct RSCache_Dat2ConfigNpc*
tool_neutral_npc_to_dat2(
    const struct Tool_NeutralNpc* n,
    const struct RSCache* dest_profile,
    int emit_bas,
    int bas_dest_id,
    char*** warnings,
    int* warning_count)
{
    assert(n && dest_profile);
    struct RSCache_Dat2ConfigNpc* npc = calloc(1, sizeof(*npc));
    if( !npc )
        return NULL;

    npc->name = tool_strdup(n->name);
    npc->size = n->size > 0 ? n->size : 1;
    npc->combat_level = n->combat_level;
    npc->width_scale = n->width_scale > 0 ? n->width_scale : 128;
    npc->height_scale = n->height_scale > 0 ? n->height_scale : 128;
    npc->ambient = n->ambient;
    npc->contrast = n->contrast;
    npc->is_minimap_visible = n->is_minimap_visible;
    npc->is_interactable = n->is_interactable;
    npc->rotation_flag = n->rotation_flag;
    npc->rotation_speed = n->rotation_speed;
    npc->category = n->category;
    npc->height = n->height;
    npc->bas_type_id = -1;

    if( n->models_count > 0 )
    {
        npc->models = malloc((size_t)n->models_count * sizeof(int));
        if( npc->models )
        {
            memcpy(npc->models, n->models, (size_t)n->models_count * sizeof(int));
            npc->models_count = n->models_count;
        }
    }
    if( n->chathead_models_count > 0 )
    {
        npc->chathead_models = malloc((size_t)n->chathead_models_count * sizeof(int));
        if( npc->chathead_models )
        {
            memcpy(
                npc->chathead_models,
                n->chathead_models,
                (size_t)n->chathead_models_count * sizeof(int));
            npc->chathead_models_count = n->chathead_models_count;
        }
    }
    if( n->recolor_count > 0 )
    {
        npc->recolor_to_find = malloc((size_t)n->recolor_count * sizeof(short));
        npc->recolor_to_replace = malloc((size_t)n->recolor_count * sizeof(short));
        if( npc->recolor_to_find && npc->recolor_to_replace )
        {
            memcpy(
                npc->recolor_to_find, n->recolor_to_find, (size_t)n->recolor_count * sizeof(short));
            memcpy(
                npc->recolor_to_replace,
                n->recolor_to_replace,
                (size_t)n->recolor_count * sizeof(short));
            npc->recolor_count = n->recolor_count;
        }
    }
    if( n->retexture_count > 0 )
    {
        npc->retexture_to_find = malloc((size_t)n->retexture_count * sizeof(short));
        npc->retexture_to_replace = malloc((size_t)n->retexture_count * sizeof(short));
        if( npc->retexture_to_find && npc->retexture_to_replace )
        {
            memcpy(
                npc->retexture_to_find,
                n->retexture_to_find,
                (size_t)n->retexture_count * sizeof(short));
            memcpy(
                npc->retexture_to_replace,
                n->retexture_to_replace,
                (size_t)n->retexture_count * sizeof(short));
            npc->retexture_count = n->retexture_count;
            add_warning(
                warnings,
                warning_count,
                "retexture ids are cache-local and may not map across revisions");
        }
    }
    for( int i = 0; i < 5; i++ )
        npc->actions[i] = tool_strdup(n->actions[i]);

    if( emit_bas && RSCache_IsRs2Dat2(dest_profile) )
    {
        npc->bas_type_id = bas_dest_id;
        /* Standing/walking stay 0; BasType carries them. */
    }
    else
    {
        if( n->anim_present[TOOL_ANIM_IDLE] )
            npc->standing_animation = n->anim[TOOL_ANIM_IDLE];
        if( n->anim_present[TOOL_ANIM_WALK] )
            npc->walking_animation = n->anim[TOOL_ANIM_WALK];
        if( n->anim_present[TOOL_ANIM_WALK_BACK] )
            npc->rotate180_animation = n->anim[TOOL_ANIM_WALK_BACK];
        if( n->anim_present[TOOL_ANIM_WALK_LEFT] )
            npc->rotate_left_animation = n->anim[TOOL_ANIM_WALK_LEFT];
        if( n->anim_present[TOOL_ANIM_WALK_RIGHT] )
            npc->rotate_right_animation = n->anim[TOOL_ANIM_WALK_RIGHT];
        if( n->anim_present[TOOL_ANIM_IDLE_ROTATE_LEFT] )
            npc->idle_rotate_left_animation = n->anim[TOOL_ANIM_IDLE_ROTATE_LEFT];
        if( n->anim_present[TOOL_ANIM_IDLE_ROTATE_RIGHT] )
            npc->idle_rotate_right_animation = n->anim[TOOL_ANIM_IDLE_ROTATE_RIGHT];

        if( n->anim_present[TOOL_ANIM_RUN] || n->anim_present[TOOL_ANIM_CRAWL] )
        {
            if( RSCache_IsOsrs(dest_profile) || RSCache_IsRs2Dat2(dest_profile) )
            {
                if( n->anim_present[TOOL_ANIM_RUN] )
                    npc->run_animation = n->anim[TOOL_ANIM_RUN];
                if( n->anim_present[TOOL_ANIM_RUN_ROTATE180] )
                    npc->run_rotate180_animation = n->anim[TOOL_ANIM_RUN_ROTATE180];
                if( n->anim_present[TOOL_ANIM_RUN_ROTATE_LEFT] )
                    npc->run_rotate_left_animation = n->anim[TOOL_ANIM_RUN_ROTATE_LEFT];
                if( n->anim_present[TOOL_ANIM_RUN_ROTATE_RIGHT] )
                    npc->run_rotate_right_animation = n->anim[TOOL_ANIM_RUN_ROTATE_RIGHT];
                if( n->anim_present[TOOL_ANIM_CRAWL] )
                    npc->crawl_animation = n->anim[TOOL_ANIM_CRAWL];
                if( n->anim_present[TOOL_ANIM_CRAWL_ROTATE180] )
                    npc->crawl_rotate180_animation = n->anim[TOOL_ANIM_CRAWL_ROTATE180];
                if( n->anim_present[TOOL_ANIM_CRAWL_ROTATE_LEFT] )
                    npc->crawl_rotate_left_animation = n->anim[TOOL_ANIM_CRAWL_ROTATE_LEFT];
                if( n->anim_present[TOOL_ANIM_CRAWL_ROTATE_RIGHT] )
                    npc->crawl_rotate_right_animation = n->anim[TOOL_ANIM_CRAWL_ROTATE_RIGHT];
            }
            else
            {
                add_warning(
                    warnings,
                    warning_count,
                    "run/crawl animations dropped: destination does not express them");
            }
        }
    }
    return npc;
}

struct RSCache_Dat1ConfigNpc*
tool_neutral_npc_to_dat1(
    const struct Tool_NeutralNpc* n,
    char*** warnings,
    int* warning_count)
{
    assert(n);
    struct RSCache_Dat1ConfigNpc* npc = calloc(1, sizeof(*npc));
    if( !npc )
        return NULL;
    npc->name = tool_strdup(n->name);
    npc->desc = tool_strdup(n->desc);
    npc->size = n->size > 0 ? n->size : 1;
    npc->readyanim = -1;
    npc->walkanim = -1;
    npc->walkanim_b = -1;
    npc->walkanim_r = -1;
    npc->walkanim_l = -1;
    npc->minimap = n->is_minimap_visible;
    npc->vislevel = n->combat_level;
    npc->resizeh = n->width_scale > 0 ? n->width_scale : 128;
    npc->resizev = n->height_scale > 0 ? n->height_scale : 128;
    npc->ambient = n->ambient;
    npc->contrast = n->contrast;
    npc->turnspeed = n->rotation_speed > 0 ? n->rotation_speed : 32;
    npc->headicon = -1;
    npc->resizex = -1;
    npc->resizey = -1;
    npc->resizez = -1;

    if( n->models_count > 0 )
    {
        npc->models = malloc((size_t)n->models_count * sizeof(int));
        if( npc->models )
        {
            memcpy(npc->models, n->models, (size_t)n->models_count * sizeof(int));
            npc->models_count = n->models_count;
        }
    }
    if( n->chathead_models_count > 0 )
    {
        npc->heads = malloc((size_t)n->chathead_models_count * sizeof(int));
        if( npc->heads )
        {
            memcpy(
                npc->heads, n->chathead_models, (size_t)n->chathead_models_count * sizeof(int));
            npc->heads_count = n->chathead_models_count;
        }
    }
    if( n->recolor_count > 0 )
    {
        npc->recol_s = malloc((size_t)n->recolor_count * sizeof(int));
        npc->recol_d = malloc((size_t)n->recolor_count * sizeof(int));
        if( npc->recol_s && npc->recol_d )
        {
            for( int i = 0; i < n->recolor_count; i++ )
            {
                npc->recol_s[i] = n->recolor_to_find[i];
                npc->recol_d[i] = n->recolor_to_replace[i];
            }
            npc->recol_count = n->recolor_count;
        }
    }
    for( int i = 0; i < 5; i++ )
        npc->op[i] = tool_strdup(n->actions[i]);

    if( n->anim_present[TOOL_ANIM_IDLE] )
        npc->readyanim = n->anim[TOOL_ANIM_IDLE];
    if( n->anim_present[TOOL_ANIM_WALK] )
        npc->walkanim = n->anim[TOOL_ANIM_WALK];
    if( n->anim_present[TOOL_ANIM_WALK_BACK] )
        npc->walkanim_b = n->anim[TOOL_ANIM_WALK_BACK];
    if( n->anim_present[TOOL_ANIM_WALK_RIGHT] )
        npc->walkanim_r = n->anim[TOOL_ANIM_WALK_RIGHT];
    if( n->anim_present[TOOL_ANIM_WALK_LEFT] )
        npc->walkanim_l = n->anim[TOOL_ANIM_WALK_LEFT];

    if( n->anim_present[TOOL_ANIM_RUN] || n->anim_present[TOOL_ANIM_CRAWL] )
        add_warning(warnings, warning_count, "run/crawl animations dropped for dat1 destination");
    if( n->retexture_count > 0 )
        add_warning(warnings, warning_count, "retexture dropped: dat1 NPC has no retexture opcodes");
    return npc;
}

void
tool_id_map_init(struct Tool_IdMap* m)
{
    memset(m, 0, sizeof(*m));
}

void
tool_id_map_free(struct Tool_IdMap* m)
{
    if( !m )
        return;
    free(m->entries);
    memset(m, 0, sizeof(*m));
}

int
tool_id_map_put(
    struct Tool_IdMap* m,
    int source_id,
    int dest_id)
{
    for( int i = 0; i < m->count; i++ )
    {
        if( m->entries[i].source_id == source_id )
        {
            m->entries[i].dest_id = dest_id;
            return 1;
        }
    }
    if( m->count >= m->capacity )
    {
        int cap = m->capacity == 0 ? 16 : m->capacity * 2;
        struct Tool_IdMapEntry* n =
            realloc(m->entries, (size_t)cap * sizeof(*n));
        if( !n )
            return 0;
        m->entries = n;
        m->capacity = cap;
    }
    m->entries[m->count].source_id = source_id;
    m->entries[m->count].dest_id = dest_id;
    m->count++;
    return 1;
}

int
tool_id_map_lookup(
    const struct Tool_IdMap* m,
    int source_id,
    int* dest_id)
{
    for( int i = 0; i < m->count; i++ )
    {
        if( m->entries[i].source_id == source_id )
        {
            *dest_id = m->entries[i].dest_id;
            return 1;
        }
    }
    return 0;
}

int
tool_alloc_id(
    int source_id,
    int (*is_free)(int id, void* userdata),
    void* userdata)
{
    assert(is_free);
    if( is_free(source_id, userdata) )
        return source_id;
    for( int id = source_id + 1; id < source_id + 1000000; id++ )
    {
        if( is_free(id, userdata) )
            return id;
    }
    return -1;
}

void
tool_port_plan_free(struct Tool_PortPlan* p)
{
    if( !p )
        return;
    tool_neutral_npc_free(&p->npc);
    free(p->related_seq_ids);
    tool_id_map_free(&p->model_map);
    tool_id_map_free(&p->chathead_map);
    tool_id_map_free(&p->seq_map);
    tool_id_map_free(&p->framemap_map);
    tool_id_map_free(&p->frame_archive_map);
    tool_id_map_free(&p->frame_id_map);
    tool_id_map_free(&p->bas_map);
    for( int i = 0; i < p->warning_count; i++ )
        free(p->warnings[i]);
    free(p->warnings);
    memset(p, 0, sizeof(*p));
}

void
tool_port_plan_add_warning(
    struct Tool_PortPlan* p,
    const char* fmt,
    ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    add_warning(&p->warnings, &p->warning_count, buf);
}

static const char*
slot_name(enum Tool_AnimSlot s)
{
    static const char* names[] = {
        "idle",
        "walk",
        "walk_back",
        "walk_left",
        "walk_right",
        "idle_rotate_left",
        "idle_rotate_right",
        "run",
        "run_rotate180",
        "run_rotate_left",
        "run_rotate_right",
        "crawl",
        "crawl_rotate180",
        "crawl_rotate_left",
        "crawl_rotate_right",
    };
    if( s < 0 || s >= TOOL_ANIM_SLOT_COUNT )
        return "?";
    return names[s];
}

static void
print_id_map(
    const char* label,
    const struct Tool_IdMap* m)
{
    printf("  %s (%d):\n", label, m->count);
    for( int i = 0; i < m->count; i++ )
        printf("    %d -> %d\n", m->entries[i].source_id, m->entries[i].dest_id);
}

void
tool_port_plan_print(const struct Tool_PortPlan* p)
{
    assert(p);
    printf("=== port plan npc source_id=%d name=%s ===\n", p->npc.source_id, p->npc.name ? p->npc.name : "(null)");
    printf("  models: %d  chatheads: %d\n", p->npc.models_count, p->npc.chathead_models_count);
    printf("  animation slots:\n");
    for( int i = 0; i < TOOL_ANIM_SLOT_COUNT; i++ )
    {
        if( !p->npc.anim_present[i] )
            continue;
        printf("    %s = %d\n", slot_name((enum Tool_AnimSlot)i), p->npc.anim[i]);
    }
    if( p->npc.bas_present )
        printf("  source bas_type_id = %d\n", p->npc.bas_type_id);
    if( p->related_seq_count > 0 )
    {
        printf("  related sequences (%d):", p->related_seq_count);
        for( int i = 0; i < p->related_seq_count; i++ )
            printf(" %d", p->related_seq_ids[i]);
        printf("\n");
    }
    print_id_map("models", &p->model_map);
    print_id_map("chatheads", &p->chathead_map);
    print_id_map("sequences", &p->seq_map);
    print_id_map("framemaps", &p->framemap_map);
    print_id_map("frame_archives", &p->frame_archive_map);
    print_id_map("frame_ids", &p->frame_id_map);
    print_id_map("bas", &p->bas_map);
    if( p->warning_count > 0 )
    {
        printf("  warnings (%d):\n", p->warning_count);
        for( int i = 0; i < p->warning_count; i++ )
            printf("    - %s\n", p->warnings[i]);
    }
}

int
tool_port_plan_write_json(
    const struct Tool_PortPlan* p,
    const char* path)
{
    assert(p && path);
    FILE* f = fopen(path, "w");
    if( !f )
        return 0;
    fprintf(f, "{\n  \"source_npc_id\": %d,\n  \"name\": \"%s\",\n", p->npc.source_id, p->npc.name ? p->npc.name : "");
    fprintf(f, "  \"id_maps\": {\n");
    fprintf(f, "    \"models\": [");
    for( int i = 0; i < p->model_map.count; i++ )
        fprintf(
            f,
            "%s{\"src\":%d,\"dst\":%d}",
            i ? "," : "",
            p->model_map.entries[i].source_id,
            p->model_map.entries[i].dest_id);
    fprintf(f, "],\n    \"sequences\": [");
    for( int i = 0; i < p->seq_map.count; i++ )
        fprintf(
            f,
            "%s{\"src\":%d,\"dst\":%d}",
            i ? "," : "",
            p->seq_map.entries[i].source_id,
            p->seq_map.entries[i].dest_id);
    fprintf(f, "],\n    \"framemaps\": [");
    for( int i = 0; i < p->framemap_map.count; i++ )
        fprintf(
            f,
            "%s{\"src\":%d,\"dst\":%d}",
            i ? "," : "",
            p->framemap_map.entries[i].source_id,
            p->framemap_map.entries[i].dest_id);
    fprintf(f, "],\n    \"frame_ids\": [");
    for( int i = 0; i < p->frame_id_map.count; i++ )
        fprintf(
            f,
            "%s{\"src\":%d,\"dst\":%d}",
            i ? "," : "",
            p->frame_id_map.entries[i].source_id,
            p->frame_id_map.entries[i].dest_id);
    fprintf(f, "]\n  },\n  \"warnings\": [");
    for( int i = 0; i < p->warning_count; i++ )
        fprintf(f, "%s\"%s\"", i ? "," : "", p->warnings[i]);
    fprintf(f, "]\n}\n");
    fclose(f);
    return 1;
}
