/*
 * The seek check's digests (replay_digest.h). Read-only.
 */
#include "replay/replay_digest.h"

#include "app.h"
#include "game/rs_entity_sync.h"
#include "inv/inv_manager.h"
#include "net/net.h"
#include "net/rev/gameproto_revisions.h"
#include "ui/uitree.h"
#include "varc/varc_manager.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/torirs_log.h"

#define FNV_OFFSET 0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull

static uint64_t
fnv_bytes(
    uint64_t h,
    void const* data,
    size_t len)
{
    unsigned char const* p = data;

    for( size_t i = 0; i < len; i++ )
    {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

static uint64_t
fnv_int(
    uint64_t h,
    int64_t v)
{
    return fnv_bytes(h, &v, sizeof(v));
}

static uint64_t
fnv_str(
    uint64_t h,
    char const* s)
{
    /* NULL and "" differ: a component with no text is not one blanked. */
    if( !s )
        return fnv_int(h, -1);
    return fnv_bytes(fnv_int(h, (int64_t)strlen(s)), s, strlen(s));
}

/* An item's hash spread over 64 bits, so a sum of them is an order-free set
 * hash in which two different items do not cancel. */
static uint64_t
mix(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

/* --- session ------------------------------------------------------------- */

static uint64_t
digest_varps(struct VarPManager const* v)
{
    uint64_t h = FNV_OFFSET;

    h = fnv_int(h, v->varp_count);
    if( v->var )
        h = fnv_bytes(h, v->var, (size_t)v->varp_count * sizeof(v->var[0]));
    if( v->var_serv )
        h = fnv_bytes(h, v->var_serv, (size_t)v->varp_count * sizeof(v->var_serv[0]));
    h = fnv_int(h, v->var_long_count);
    for( int i = 0; i < v->var_long_count; i++ )
    {
        if( !v->var_long_set || !v->var_long_set[i] )
            continue;
        h = fnv_int(h, i);
        h = fnv_int(h, v->var_long[i]);
        h = fnv_int(h, v->var_long_serv ? v->var_long_serv[i] : 0);
    }
    return h;
}

static uint64_t
digest_invs(struct InvManager const* m)
{
    uint64_t sum = 0;

    for( int c = 0; c < m->container_count; c++ )
    {
        struct InvContainer const* inv = &m->containers[c];
        uint64_t h = fnv_int(FNV_OFFSET, inv->inv_id);

        h = fnv_int(h, inv->slot_count);
        for( int s = 0; s < inv->slot_count && inv->slots; s++ )
        {
            h = fnv_int(h, inv->slots[s].obj_id);
            h = fnv_int(h, inv->slots[s].obj_count);
        }
        sum += mix(h);
    }
    return sum;
}

static uint64_t
digest_stats(struct RS_PlayerStats const* s)
{
    uint64_t h = FNV_OFFSET;

    h = fnv_bytes(h, s->base_level, sizeof(s->base_level));
    h = fnv_bytes(h, s->current_level, sizeof(s->current_level));
    h = fnv_bytes(h, s->xp, sizeof(s->xp));
    return h;
}

static uint64_t
digest_cs2(
    struct VarCManager const* v,
    FILE* dump)
{
    uint64_t h = FNV_OFFSET;

    h = fnv_int(h, v->int_count);
    if( v->ints )
        h = fnv_bytes(h, v->ints, (size_t)v->int_count * sizeof(v->ints[0]));
    for( int i = 0; dump && v->ints && i < v->int_count; i++ )
        fprintf(dump, "varc %d %d\n", i, v->ints[i]);
    h = fnv_int(h, v->string_count);
    for( int i = 0; i < v->string_count && v->strings; i++ )
    {
        h = fnv_str(h, v->strings[i] ? v->strings[i] : "");
        if( dump )
            fprintf(dump, "varcstr %d %s\n", i, v->strings[i] ? v->strings[i] : "");
    }
    h = fnv_int(h, v->long_count);
    for( int i = 0; i < v->long_count; i++ )
    {
        if( !v->long_set || !v->long_set[i] )
            continue;
        h = fnv_int(h, i);
        h = fnv_int(h, v->longs[i]);
    }
    return h;
}

/*
 * The interface tree: every live node, by its id, its parent's id and the
 * properties the server and the scripts set. Left out, as presentation: the
 * resolved layout (abs_*, layout_resolved), scene and atlas handles (where a
 * sprite or an icon happened to be baked), an animation's frame and cycle,
 * drag and hover state, the dirty bits and the node's slot in the array.
 */
static uint64_t
digest_ui(
    struct UITree const* tree,
    FILE* dump)
{
    uint64_t sum = 0;

    if( !tree )
        return 0;
    /* tree-walk-exempt: replay seek check, two walks per run under --replay-digest */
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        uint64_t h;
        int parent_id = -1;

        if( c->freed || c->component_id < 0 )
            continue;
        /* Excluded as presentation: a dynamic node under no component (no
         * parent, or a native container with no id) is in no interface. These are the entity overlay's layers (headbars,
         * hitsplats, overhead text), laid out by the emit pass at the
         * entities' projected screen positions (app_entity_overlay_layout):
         * camera and interpolation, rebuilt every drawn frame. */
        if( c->parent >= 0 && (uint32_t)c->parent < tree->component_count )
            parent_id = tree->components[c->parent].component_id;
        if( c->dynamic && parent_id < 0 )
            continue;
        /* Likewise anything of interface 0, which no cache interface is: the
         * client's own native nodes and what hangs under them -- the ground
         * item labels (app_ground_items_tick), the overlay layers above --
         * whose ids are handed out as they are made. */
        if( (c->dynamic ? parent_id : c->component_id) >> 16 == 0 )
            continue;
        /* A dynamic node's id is handed out as it is created, so it counts
         * the creations before it; the node is its parent's child at an
         * index. */
        h = fnv_int(FNV_OFFSET, c->dynamic ? -1 : c->component_id);
        h = fnv_int(h, c->dynamic ? c->dynamic_child_index : -1);
        h = fnv_int(h, parent_id);
        h = fnv_int(h, c->type);
        h = fnv_int(h, c->behavior.hide);
        h = fnv_int(h, c->mount_hidden);
        h = fnv_int(h, c->trans);
        h = fnv_int(h, c->colour);
        h = fnv_int(h, c->fill_colour);
        h = fnv_int(h, c->position.x);
        h = fnv_int(h, c->position.y);
        h = fnv_int(h, c->position.width);
        h = fnv_int(h, c->position.height);
        h = fnv_int(h, c->position.x_mode);
        h = fnv_int(h, c->position.y_mode);
        h = fnv_int(h, c->position.width_mode);
        h = fnv_int(h, c->position.height_mode);
        h = fnv_int(h, c->item_id);
        h = fnv_int(h, c->item_count);
        h = fnv_int(h, c->scroll_x);
        h = fnv_int(h, c->scroll_y);
        h = fnv_str(h, c->data_text);
        if( c->type == UIELEM_RS_TEXT )
        {
            h = fnv_str(h, c->u.rs_text.text);
            h = fnv_int(h, c->u.rs_text.font_id);
            h = fnv_int(h, c->u.rs_text.color);
        }
        else if( c->type == UIELEM_RS_MODEL )
        {
            h = fnv_int(h, c->u.rs_model.gamecache_model_id);
            h = fnv_int(h, c->u.rs_model.active_model_id);
            h = fnv_int(h, c->u.rs_model.zoom);
            h = fnv_int(h, c->u.rs_model.xan);
            h = fnv_int(h, c->u.rs_model.yan);
            h = fnv_int(h, c->u.rs_model.zan);
            h = fnv_int(h, c->u.rs_model.anim_seq_id);
        }
        else if( c->type == UIELEM_RS_RECT )
        {
            h = fnv_int(h, c->u.rs_rect.color);
            h = fnv_int(h, c->u.rs_rect.filled);
        }
        if( dump )
            fprintf(dump,
                    "ui %d dyn=%d parent=%d type=%d hide=%d mh=%d trans=%d col=%d fill=%d "
                    "pos=%d,%d,%d,%d modes=%d,%d,%d,%d item=%d,%d scroll=%d,%d text=%s rstext=%s "
                    "model=%d,%d anim=%d plugin=%llu role=%d hash=%016llx\n",
                    c->component_id, c->dynamic ? c->dynamic_child_index : -1, parent_id,
                    (int)c->type, c->behavior.hide, c->mount_hidden, c->trans, c->colour,
                    c->fill_colour, c->position.x, c->position.y, c->position.width,
                    c->position.height, c->position.x_mode, c->position.y_mode,
                    c->position.width_mode, c->position.height_mode, c->item_id, c->item_count,
                    c->scroll_x, c->scroll_y, c->data_text ? c->data_text : "(null)",
                    c->type == UIELEM_RS_TEXT && c->u.rs_text.text ? c->u.rs_text.text : "-",
                    c->type == UIELEM_RS_MODEL ? c->u.rs_model.gamecache_model_id : -1,
                    c->type == UIELEM_RS_MODEL ? c->u.rs_model.active_model_id : -1,
                    c->type == UIELEM_RS_MODEL ? c->u.rs_model.anim_seq_id : -1,
                    (unsigned long long)c->plugin_owner, (int)c->role_id, (unsigned long long)h);
        sum += mix(h);
    }
    return sum;
}

/* --- world ----------------------------------------------------------------- */

static uint64_t
digest_players(struct App const* app)
{
    struct RS_EntitySync const* es = &app->esync;
    uint64_t sum = 0;

    if( !app->world )
        return 0;
    for( int i = 0; i < es->registry.count; i++ )
    {
        struct World_EntityRecord const* r = &es->registry.records[i];
        struct WorldEntity_Player const* p;
        uint64_t h;

        if( WORLD_ENTITY_KIND_OF(r->entity_id) != WORLD_ENTITY_KIND_PLAYER )
            continue;
        p = World_EntityPoolGet(&app->world->entities.player, r->world_index);
        if( !p )
            continue;
        h = fnv_int(FNV_OFFSET, WORLD_ENTITY_INDEX_OF(r->entity_id));
        /* The wire's tile (ABS_XZLEVEL), not the interpolated one. */
        h = fnv_int(h, p->view_placement.has_abs ? p->view_placement.abs_x : -1);
        h = fnv_int(h, p->view_placement.has_abs ? p->view_placement.abs_z : -1);
        h = fnv_int(h, p->grid_position.level);
        h = fnv_bytes(h, p->appearance.slots, sizeof(p->appearance.slots));
        h = fnv_bytes(h, p->appearance.colors, sizeof(p->appearance.colors));
        h = fnv_str(h, p->name);
        h = fnv_int(h, p->combat_level);
        h = fnv_int(h, p->gender);
        h = fnv_int(h, p->transmog_npc_id);
        sum += mix(h);
    }
    return fnv_int(sum, es->local_pid);
}

static uint64_t
digest_player_table(struct App const* app)
{
    struct GameProtoRevTable const* rev = app->net ? app->net->rev : NULL;
    uint64_t h;
    void* buf;
    size_t size;

    if( !rev || !rev->player_info_state_size || !rev->player_info_state_save )
        return 0;
    size = rev->player_info_state_size();
    buf = malloc(size > 0 ? size : 1);
    assert(buf);
    rev->player_info_state_save(buf);
    h = fnv_bytes(FNV_OFFSET, buf, size);
    free(buf);
    return h;
}

static uint64_t
digest_npcs(struct App const* app)
{
    struct RS_EntitySync const* es = &app->esync;
    uint64_t h = FNV_OFFSET;
    int base_x;
    int base_z;

    if( !app->world )
        return 0;
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;
    h = fnv_int(h, es->active_npc_count);
    for( int i = 0; i < es->active_npc_count; i++ )
    {
        int const slot = es->active_npcs[i];
        int world_idx;
        struct WorldEntity_NPC const* n;

        h = fnv_int(h, slot);
        if( slot < 0 ||
            !RS_EntitySync_FindNpc((struct RS_EntitySync*)es, slot, &world_idx, NULL) )
        {
            h = fnv_int(h, -2);
            continue;
        }
        n = World_EntityPoolGet(&app->world->entities.npc, world_idx);
        if( !n )
        {
            h = fnv_int(h, -3);
            continue;
        }
        h = fnv_int(h, n->npc_id);
        h = fnv_int(h, n->base_npc_id);
        /* route[0]: the tile the server last put it on. */
        h = fnv_int(h, base_x + n->pathing.route_x[0]);
        h = fnv_int(h, base_z + n->pathing.route_z[0]);
        h = fnv_int(h, n->grid_position.level);
    }
    return h;
}

/*
 * The zones as they now stand, not the history that got them there: per
 * (level, x, z, layer), the loc the latest change left against the map's own
 * (the key's oldest record), where they differ; and every ground item.
 */
static uint64_t
digest_zones(
    struct App const* app,
    FILE* dump)
{
    struct World const* world = app->world;
    struct World_EntityPool const* pool;
    uint64_t sum = 0;

    if( !world )
        return 0;
    for( int i = 0; i < world->loc_change_count; i++ )
    {
        struct World_LocChange const* rec = &world->loc_changes[i];
        struct World_LocChange const* latest = rec;
        int first = 1;
        uint64_t h;

        for( int j = 0; j < i && first; j++ )
        {
            struct World_LocChange const* o = &world->loc_changes[j];

            if( o->level == rec->level && o->layer == rec->layer && o->x == rec->x && o->z == rec->z )
                first = 0;
        }
        if( !first )
            continue;
        for( int k = i + 1; k < world->loc_change_count; k++ )
        {
            struct World_LocChange const* o = &world->loc_changes[k];

            if( o->level == rec->level && o->layer == rec->layer && o->x == rec->x && o->z == rec->z )
                latest = o;
        }
        if( rec->old_type == latest->new_type && rec->old_angle == latest->new_angle &&
            (rec->old_type < 0 || rec->old_shape == latest->new_shape) )
            continue;
        h = fnv_int(FNV_OFFSET, rec->level);
        h = fnv_int(h, world->_base_tile_x + rec->x);
        h = fnv_int(h, world->_base_tile_z + rec->z);
        h = fnv_int(h, rec->layer);
        h = fnv_int(h, latest->new_type);
        h = fnv_int(h, latest->new_type >= 0 ? latest->new_angle : 0);
        h = fnv_int(h, latest->new_type >= 0 ? latest->new_shape : 0);
        if( dump )
            fprintf(dump, "loc %d,%d,%d layer %d -> %d angle %d shape %d (map %d)\n",
                    world->_base_tile_x + rec->x, world->_base_tile_z + rec->z, rec->level, rec->layer,
                    latest->new_type, latest->new_angle, latest->new_shape, rec->old_type);
        sum += mix(h);
    }
    pool = &world->entities.obj_stack;
    for( int i = World_EntityPoolHead(pool); i >= 0; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack const* s = World_EntityPoolAt(pool, i);
        uint64_t h;

        if( s->editor_preview )
            continue;
        h = fnv_int(FNV_OFFSET + 1, world->_base_tile_x + s->grid_position.x);
        h = fnv_int(h, world->_base_tile_z + s->grid_position.z);
        h = fnv_int(h, s->grid_position.level);
        h = fnv_int(h, s->obj_id);
        h = fnv_int(h, s->count);
        if( dump )
            fprintf(dump, "obj %d,%d,%d %d x%d\n", world->_base_tile_x + s->grid_position.x,
                    world->_base_tile_z + s->grid_position.z, s->grid_position.level, s->obj_id,
                    s->count);
        sum += mix(h);
    }
    return sum;
}

static uint64_t
digest_scene(struct App const* app)
{
    uint64_t h = FNV_OFFSET;

    h = fnv_int(h, app->world_active);
    h = fnv_int(h, app->world ? app->world->_base_tile_x : -1);
    h = fnv_int(h, app->world ? app->world->_base_tile_z : -1);
    h = fnv_int(h, app->rebuild_zone_x);
    h = fnv_int(h, app->rebuild_zone_z);
    h = fnv_int(h, app->rebuild_instance_active);
    if( app->rebuild_instance_active )
        h = fnv_bytes(h, app->rebuild_instance_zones, sizeof(app->rebuild_instance_zones));
    return h;
}

/* --- the digest ---------------------------------------------------------- */

static void
digest_take(
    struct App const* app,
    struct ReplayDigest* out,
    FILE* dump)
{
    assert(app);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->v[REPLAY_DIGEST_VARPS] = digest_varps(&app->varps);
    out->v[REPLAY_DIGEST_INVS] = digest_invs(&app->invs);
    out->v[REPLAY_DIGEST_STATS] = digest_stats(&app->stats);
    out->v[REPLAY_DIGEST_UI] = digest_ui(app->tree, dump);
    out->v[REPLAY_DIGEST_CS2] = digest_cs2(&app->varcs, dump);
    out->v[REPLAY_DIGEST_PLAYERS] = digest_players(app);
    out->v[REPLAY_DIGEST_PLAYER_TABLE] = digest_player_table(app);
    out->v[REPLAY_DIGEST_NPCS] = digest_npcs(app);
    out->v[REPLAY_DIGEST_ZONES] = digest_zones(app, dump);
    out->v[REPLAY_DIGEST_SCENE] = digest_scene(app);
    out->client_clock = app->host.client_clock;
    out->logic_cycle = (unsigned long long)app->logic_cycle;
}

void
ReplayDigest_Take(
    struct App const* app,
    struct ReplayDigest* out)
{
    digest_take(app, out, NULL);
}

void
ReplayDigest_TakeDumping(
    struct App const* app,
    struct ReplayDigest* out,
    char const* path)
{
    FILE* dump;

    assert(path);
    dump = fopen(path, "w");
    if( !dump )
        TORIRS_ERR("replay: cannot write the digest dump %s\n", path);
    digest_take(app, out, dump);
    if( dump )
        fclose(dump);
}

char const*
ReplayDigest_DomainName(int domain)
{
    static char const* const k_names[REPLAY_DIGEST_COUNT] = {
        "varps", "invs", "stats", "ui", "cs2", "players", "ptable", "npcs", "zones", "scene",
    };

    assert(domain >= 0);
    assert(domain < REPLAY_DIGEST_COUNT);
    return k_names[domain];
}

void
ReplayDigest_Print(
    struct ReplayDigest const* d,
    int tick)
{
    char line[512];
    int n;

    assert(d);
    n = snprintf(line, sizeof(line), "replay: digest tick=%d", tick);
    for( int i = 0; i < REPLAY_DIGEST_COUNT && n < (int)sizeof(line); i++ )
        n += snprintf(line + n, sizeof(line) - (size_t)n, " %s=%016llx", ReplayDigest_DomainName(i),
                      (unsigned long long)d->v[i]);
    TORIRS_REPORT("%s (clientclock %d, logic cycle %llu)\n", line, d->client_clock, d->logic_cycle);
}
