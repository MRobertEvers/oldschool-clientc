/*
 * The App's own session state, as checkpoint domains (app/app_checkpoint.h).
 */
#include "app/app_checkpoint.h"

#include "app.h"
#include "audio/torirs_music.h"
#include "mem/torirs_mem.h"
#include "replay/replay_checkpoint_util.h"
#include "ui/inv_drag.h"
#include "ui/uitree.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_frame.h"
#include "ui/uitree_hovertext.h"
#include "ui/uitree_if_events.h"
#include "ui/uitree_if_store.h"
#include "ui/uitree_interact.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- field lists ---------------------------------------------------------- */

/*
 * Each group is App fields copied by value. A field appears in exactly one
 * group, and what a group leaves out it leaves out on purpose:
 *
 *  - the camera script's oculus keys, its per-frame shake roll and the
 *    "arrived this frame" count are the frame's, not the session's;
 *  - the skybox is restored through App_SetSkybox (the built model is the
 *    scene's), so only its id is a field;
 *  - the idle timer counts frames of input, which a replay has none of.
 */
struct AppCkptField
{
    char const* name;
    size_t offset;
    size_t size;
    /* 0: restored but not compared -- it carries what the world, not the
     * session, decides (the camera events' world tick, the ground-item tiles
     * the last tick's OBJ packets touched). */
    int compared;
};

#define APP_CKPT_FIELD(f) { #f, offsetof(struct App, f), sizeof(((struct App*)0)->f), 1 },
#define APP_CKPT_FIELD_UNCOMPARED(f) { #f, offsetof(struct App, f), sizeof(((struct App*)0)->f), 0 },

static struct AppCkptField const k_fields_stats[] = { APP_CKPT_FIELD(stats) };
static struct AppCkptField const k_fields_social[] = { APP_CKPT_FIELD(social) };
static struct AppCkptField const k_fields_slots[] = { APP_CKPT_FIELD(slots) };
static struct AppCkptField const k_fields_idk[] = { APP_CKPT_FIELD(idk_design) };
/* The held RUNCLIENTSCRIPTs are empty at any moment a checkpoint can be taken
 * (ReplayCheckpoint_CanTake), so only the ring's position is state. */
static struct AppCkptField const k_fields_cs2queue[] = {
    APP_CKPT_FIELD(pending_clientscripts.head) APP_CKPT_FIELD(pending_clientscripts.count)
};
static struct AppCkptField const k_fields_camera[] = {
    APP_CKPT_FIELD(cam_script.scripted) APP_CKPT_FIELD(cam_script.smooth_reset)
    APP_CKPT_FIELD(cam_script.move_lx) APP_CKPT_FIELD(cam_script.move_lz)
    APP_CKPT_FIELD(cam_script.move_height) APP_CKPT_FIELD(cam_script.move_rate)
    APP_CKPT_FIELD(cam_script.move_rate2) APP_CKPT_FIELD(cam_script.look_lx)
    APP_CKPT_FIELD(cam_script.look_lz) APP_CKPT_FIELD(cam_script.look_height)
    APP_CKPT_FIELD(cam_script.look_rate) APP_CKPT_FIELD(cam_script.look_rate2)
    APP_CKPT_FIELD(cam_script.move_cycles) APP_CKPT_FIELD(cam_script.rotate_cycles)
    APP_CKPT_FIELD(cam_script.look_cycles) APP_CKPT_FIELD(cam_script.last_move_height)
    APP_CKPT_FIELD(cam_script.shake) APP_CKPT_FIELD(cam_script.shake_jitter)
    APP_CKPT_FIELD(cam_script.shake_amplitude) APP_CKPT_FIELD(cam_script.shake_speed)
    APP_CKPT_FIELD(cam_script.shake_cycle) APP_CKPT_FIELD(cam_script.cam_mode)
    APP_CKPT_FIELD(cam_script.oculus_sync) APP_CKPT_FIELD(cam_script.oculus_recentre)
    APP_CKPT_FIELD(cam_script.oculus_y_valid) APP_CKPT_FIELD(cam_script.oculus_y)
    APP_CKPT_FIELD(cam_script.oculus_speed) APP_CKPT_FIELD(cam_script.oculus_angle)
    APP_CKPT_FIELD(cam_script.oculus_dy) APP_CKPT_FIELD(cam_script.serial)
    APP_CKPT_FIELD_UNCOMPARED(cam_script.events) APP_CKPT_FIELD(cam_unlock)
};
static struct AppCkptField const k_fields_scalars[] = {
    APP_CKPT_FIELD(hint_arrow) APP_CKPT_FIELD(player_ops) APP_CKPT_FIELD(player_ops_primary)
    APP_CKPT_FIELD(player_attack_option) APP_CKPT_FIELD(npc_attack_option) APP_CKPT_FIELD(multiway)
    APP_CKPT_FIELD(interaction_default_tile) APP_CKPT_FIELD(interaction_default_entity)
    APP_CKPT_FIELD(interaction_tile) APP_CKPT_FIELD(interaction_entity) APP_CKPT_FIELD(server_zbuf)
    APP_CKPT_FIELD(unknown_string) APP_CKPT_FIELD(site_settings) APP_CKPT_FIELD(website_token)
    APP_CKPT_FIELD(skybox_id_plus1) APP_CKPT_FIELD(minimap_state) APP_CKPT_FIELD(welcome)
    APP_CKPT_FIELD(reboot_timer) APP_CKPT_FIELD(pm_message_ids) APP_CKPT_FIELD(pm_message_head)
    APP_CKPT_FIELD(tracking_enabled) APP_CKPT_FIELD(objsel) APP_CKPT_FIELD(targetsel)
    APP_CKPT_FIELD(logout_requested) APP_CKPT_FIELD(logout_wait_cycles)
    APP_CKPT_FIELD(chat_line_saved) APP_CKPT_FIELD(chat_line_prompt) APP_CKPT_FIELD(vm_keyboard_open)
    /* Which top the tree is rooted at: IF_OPENTOP is a no-op for the top the
     * App thinks is open, so a restore across a re-root must put the old one
     * back or the chase never re-roots and every IF_OPENSUB misses. */
    APP_CKPT_FIELD(boot_interface_id) APP_CKPT_FIELD(screen)
    /* What the client triggers (tile highlights, the ground-items overlay)
     * were last fired for. They fire on the EDGE of these changing, and the
     * overlays and highlight groups they build are in the checkpoint, so the
     * edges must be the checkpoint's too or a restore re-fires (or skips) a
     * trigger the recorded session did not. */
    APP_CKPT_FIELD(highlight_last_hover_coord) APP_CKPT_FIELD(highlight_last_route)
    APP_CKPT_FIELD(highlight_last_dest_coord) APP_CKPT_FIELD(highlight_last_mouseover)
    APP_CKPT_FIELD_UNCOMPARED(ground_items_dirty) APP_CKPT_FIELD(ground_items_settings_seen)
    APP_CKPT_FIELD(ground_items_aux_seen) APP_CKPT_FIELD(client_trigger_refire_pending)
};

#undef APP_CKPT_FIELD
#undef APP_CKPT_FIELD_UNCOMPARED

struct AppCkptGroup
{
    char const* name;
    struct AppCkptField const* fields;
    int count;
};

#define APP_CKPT_GROUP(name, table) { name, table, (int)(sizeof(table) / sizeof(table[0])) }
static struct AppCkptGroup const k_groups[APP_CKPT_FIELDS_COUNT] = {
    APP_CKPT_GROUP("stats", k_fields_stats),       APP_CKPT_GROUP("social", k_fields_social),
    APP_CKPT_GROUP("slots", k_fields_slots),       APP_CKPT_GROUP("idk", k_fields_idk),
    APP_CKPT_GROUP("cs2queue", k_fields_cs2queue), APP_CKPT_GROUP("camera", k_fields_camera),
    APP_CKPT_GROUP("scalars", k_fields_scalars),
};
#undef APP_CKPT_GROUP

/* A group's copy: which group, then its fields packed end to end. */
struct AppCkptFields
{
    int which;
    size_t bytes;
    unsigned char data[];
};

static size_t
app_ckpt_group_bytes(struct AppCkptGroup const* group)
{
    size_t bytes = 0;

    for( int i = 0; i < group->count; i++ )
        bytes += group->fields[i].size;
    return bytes;
}

void*
App_CheckpointFieldsTake(
    struct App const* app,
    int which)
{
    struct AppCkptGroup const* group;
    struct AppCkptFields* cp;
    size_t at = 0;
    size_t bytes;

    assert(app);
    assert(which >= 0);
    assert(which < APP_CKPT_FIELDS_COUNT);
    group = &k_groups[which];
    bytes = app_ckpt_group_bytes(group);
    cp = ToriMem_Alloc(ToriMem_Session(), sizeof(*cp) + bytes);
    assert(cp);
    cp->which = which;
    cp->bytes = bytes;
    for( int i = 0; i < group->count; i++ )
    {
        memcpy(cp->data + at, (unsigned char const*)app + group->fields[i].offset, group->fields[i].size);
        at += group->fields[i].size;
    }
    return cp;
}

void
App_CheckpointFieldsRestore(
    struct App* app,
    int which,
    void const* cp_v)
{
    struct AppCkptFields const* cp = cp_v;
    struct AppCkptGroup const* group;
    size_t at = 0;
    int skybox_plus1;

    assert(app);
    assert(cp);
    assert(which == cp->which);
    skybox_plus1 = app->skybox_id_plus1;
    group = &k_groups[which];
    for( int i = 0; i < group->count; i++ )
    {
        memcpy((unsigned char*)app + group->fields[i].offset, cp->data + at, group->fields[i].size);
        at += group->fields[i].size;
    }
    if( which == APP_CKPT_FIELDS_SCALARS && app->skybox_id_plus1 != skybox_plus1 )
    {
        /* The model is built from the id; ask for it the way CAM_SKYBOX does. */
        int const want = app->skybox_id_plus1;

        app->skybox_id_plus1 = skybox_plus1;
        App_SetSkybox(app, want - 1);
    }
}

void
App_CheckpointFieldsFree(void* cp)
{
    ToriMem_Free(ToriMem_Session(), cp);
}

size_t
App_CheckpointFieldsBytes(void const* cp_v)
{
    struct AppCkptFields const* cp = cp_v;

    assert(cp);
    return sizeof(*cp) + cp->bytes;
}

int
App_CheckpointFieldsEqual(
    void const* a_v,
    void const* b_v,
    char* why,
    int why_cap)
{
    struct AppCkptFields const* a = a_v;
    struct AppCkptFields const* b = b_v;
    struct AppCkptGroup const* group;
    size_t at = 0;

    assert(a);
    assert(b);
    assert(a->which == b->which);
    group = &k_groups[a->which];
    for( int i = 0; i < group->count; i++ )
    {
        if( group->fields[i].compared && memcmp(a->data + at, b->data + at, group->fields[i].size) != 0 )
        {
            if( group->fields[i].size == sizeof(int) )
            {
                int va;
                int vb;

                memcpy(&va, a->data + at, sizeof(va));
                memcpy(&vb, b->data + at, sizeof(vb));
                Ckpt_Why(why, why_cap, "%s %d != %d", group->fields[i].name, va, vb);
            }
            else
            {
                size_t first = 0;

                while( a->data[at + first] == b->data[at + first] )
                    first++;
                Ckpt_Why(why, why_cap, "%s (byte %zu of %zu)", group->fields[i].name, first, group->fields[i].size);
            }
            return 0;
        }
        at += group->fields[i].size;
    }
    return 1;
}

/* --- the interface stores ------------------------------------------------- */

struct AppCkptIfStores
{
    struct UIIfTextStore texts;
    struct UIIfIntStore hides;
    struct UIIfIntStore colours;
    struct UIIfEventTable events;
    /* Whether each store had been applied to the tree as it stood. */
    int texts_current;
    int hides_current;
    int colours_current;
    struct AppIfHead* heads;
    int head_count;
    /* Per head / player model: applied to the tree as it stood. */
    unsigned char* head_current;
    struct AppIfPlayerModel* player_models;
    int player_model_count;
    unsigned char* player_model_current;
    struct UITreeInterfaceParent detached_parents[UITREE_INTERFACE_PARENT_MAX];
    int detached_parent_count;
    int modal_host_uid;
};

void*
App_CheckpointIfStoresTake(struct App const* app)
{
    struct AppCkptIfStores* cp;
    struct ToriMem_Group* g = ToriMem_Session();
    uint32_t gen;

    assert(app);
    assert(app->tree);
    gen = app->tree->generation;
    cp = ToriMem_Calloc(g, 1, sizeof(*cp));
    assert(cp);
    UIIfTextStore_CheckpointCopy(&cp->texts, &app->if_texts);
    UIIfIntStore_CheckpointCopy(&cp->hides, &app->if_hides);
    UIIfIntStore_CheckpointCopy(&cp->colours, &app->if_colours);
    UIIfEventTable_CheckpointCopy(&cp->events, &app->if_events);
    cp->texts_current = app->if_texts.applied_generation == gen;
    cp->hides_current = app->if_hides.applied_generation == gen;
    cp->colours_current = app->if_colours.applied_generation == gen;
    cp->head_count = app->if_head_count;
    cp->heads = Ckpt_Dup(g, app->if_heads, (size_t)app->if_head_count * sizeof(app->if_heads[0]));
    cp->head_current = ToriMem_Calloc(g, (size_t)(app->if_head_count + 1), 1);
    assert(cp->head_current);
    for( int i = 0; i < app->if_head_count; i++ )
        cp->head_current[i] = app->if_heads[i].applied_gen == gen;
    cp->player_model_count = app->if_player_model_count;
    cp->player_models = Ckpt_Dup(g, app->if_player_models,
                                 (size_t)app->if_player_model_count * sizeof(app->if_player_models[0]));
    cp->player_model_current = ToriMem_Calloc(g, (size_t)(app->if_player_model_count + 1), 1);
    assert(cp->player_model_current);
    for( int i = 0; i < app->if_player_model_count; i++ )
        cp->player_model_current[i] = app->if_player_models[i].applied_gen == gen;
    memcpy(cp->detached_parents, app->detached_parents, sizeof(cp->detached_parents));
    cp->detached_parent_count = app->detached_parent_count;
    cp->modal_host_uid = app->modal_host_uid;
    return cp;
}

void
App_CheckpointIfStoresRestore(
    struct App* app,
    void const* cp_v)
{
    struct AppCkptIfStores const* cp = cp_v;
    uint32_t gen;

    assert(app);
    assert(app->tree);
    assert(cp);
    gen = app->tree->generation;
    UIIfTextStore_Free(&app->if_texts);
    UIIfIntStore_Free(&app->if_hides);
    UIIfIntStore_Free(&app->if_colours);
    UIIfEventTable_Free(&app->if_events);
    UIIfTextStore_CheckpointCopy(&app->if_texts, &cp->texts);
    UIIfIntStore_CheckpointCopy(&app->if_hides, &cp->hides);
    UIIfIntStore_CheckpointCopy(&app->if_colours, &cp->colours);
    UIIfEventTable_CheckpointCopy(&app->if_events, &cp->events);
    /* Up to date then is up to date now; anything else re-applies. */
    app->if_texts.applied_generation = cp->texts_current ? gen : 0;
    app->if_hides.applied_generation = cp->hides_current ? gen : 0;
    app->if_colours.applied_generation = cp->colours_current ? gen : 0;

    /* The two request tables keep the App's allocation (ToriMem_Process),
     * grown to fit. */
    if( cp->head_count > app->if_head_cap )
    {
        app->if_heads = ToriMem_Realloc(ToriMem_Process(), app->if_heads, (size_t)cp->head_count * sizeof(app->if_heads[0]));
        assert(app->if_heads);
        app->if_head_cap = cp->head_count;
    }
    if( cp->head_count > 0 )
        memcpy(app->if_heads, cp->heads, (size_t)cp->head_count * sizeof(app->if_heads[0]));
    app->if_head_count = cp->head_count;
    for( int i = 0; i < cp->head_count; i++ )
        app->if_heads[i].applied_gen = cp->head_current[i] ? gen : 0;
    if( cp->player_model_count > app->if_player_model_cap )
    {
        app->if_player_models = ToriMem_Realloc(ToriMem_Process(), app->if_player_models,
                                                (size_t)cp->player_model_count * sizeof(app->if_player_models[0]));
        assert(app->if_player_models);
        app->if_player_model_cap = cp->player_model_count;
    }
    /* A slot index is its scene id (UITREE_SCENE_IF_PLAYER_MODEL_BASE + i), so
     * slots past the checkpoint's count are freed rather than dropped. */
    for( int i = cp->player_model_count; i < app->if_player_model_count; i++ )
    {
        app->if_player_models[i].com_id = -1;
        app->if_player_models[i].built = 0;
    }
    if( cp->player_model_count > 0 )
        memcpy(app->if_player_models, cp->player_models,
               (size_t)cp->player_model_count * sizeof(app->if_player_models[0]));
    if( cp->player_model_count > app->if_player_model_count )
        app->if_player_model_count = cp->player_model_count;
    for( int i = 0; i < cp->player_model_count; i++ )
        app->if_player_models[i].applied_gen = cp->player_model_current[i] ? gen : 0;

    memcpy(app->detached_parents, cp->detached_parents, sizeof(app->detached_parents));
    app->detached_parent_count = cp->detached_parent_count;
    app->modal_host_uid = cp->modal_host_uid;
}

void
App_CheckpointIfStoresFree(void* cp_v)
{
    struct AppCkptIfStores* cp = cp_v;
    struct ToriMem_Group* g = ToriMem_Session();

    if( !cp )
        return;
    UIIfTextStore_Free(&cp->texts);
    UIIfIntStore_Free(&cp->hides);
    UIIfIntStore_Free(&cp->colours);
    UIIfEventTable_Free(&cp->events);
    ToriMem_Free(g, cp->heads);
    ToriMem_Free(g, cp->head_current);
    ToriMem_Free(g, cp->player_models);
    ToriMem_Free(g, cp->player_model_current);
    ToriMem_Free(g, cp);
}

size_t
App_CheckpointIfStoresBytes(void const* cp_v)
{
    struct AppCkptIfStores const* cp = cp_v;

    assert(cp);
    return sizeof(*cp) + UIIfTextStore_CheckpointBytes(&cp->texts) +
           (size_t)(cp->hides.cap + cp->colours.cap) * sizeof(struct UIIfIntEntry) +
           (size_t)cp->events.cap * sizeof(struct UIIfEventRange) +
           (size_t)cp->head_count * (sizeof(cp->heads[0]) + 1u) +
           (size_t)cp->player_model_count * (sizeof(cp->player_models[0]) + 1u);
}

int
App_CheckpointIfStoresEqual(
    void const* a_v,
    void const* b_v,
    char* why,
    int why_cap)
{
    struct AppCkptIfStores const* a = a_v;
    struct AppCkptIfStores const* b = b_v;

    assert(a);
    assert(b);
    if( !UIIfTextStore_CheckpointEqual(&a->texts, &b->texts, why, why_cap) )
        return (Ckpt_Why(why, why_cap, "if_texts"), 0);
    if( !UIIfIntStore_CheckpointEqual(&a->hides, &b->hides, why, why_cap) )
        return (Ckpt_Why(why, why_cap, "if_hides"), 0);
    if( !UIIfIntStore_CheckpointEqual(&a->colours, &b->colours, why, why_cap) )
        return (Ckpt_Why(why, why_cap, "if_colours"), 0);
    if( !UIIfEventTable_CheckpointEqual(&a->events, &b->events, why, why_cap) )
        return (Ckpt_Why(why, why_cap, "if_events"), 0);
    CKPT_EQ_INT(a, b, head_count, why, why_cap);
    for( int i = 0; i < a->head_count; i++ )
    {
        struct AppIfHead const* ha = &a->heads[i];
        struct AppIfHead const* hb = &b->heads[i];

        if( ha->com_id != hb->com_id || ha->kind != hb->kind || ha->npc_id != hb->npc_id || ha->zoom != hb->zoom ||
            ha->anim_id != hb->anim_id || ha->close_serial != hb->close_serial ||
            a->head_current[i] != b->head_current[i] )
        {
            Ckpt_Why(why, why_cap, "if_heads[%d] (com 0x%08x)", i, (unsigned)ha->com_id);
            return 0;
        }
    }
    CKPT_EQ_INT(a, b, player_model_count, why, why_cap);
    for( int i = 0; i < a->player_model_count; i++ )
    {
        struct AppIfPlayerModel const* ma = &a->player_models[i];
        struct AppIfPlayerModel const* mb = &b->player_models[i];

        if( ma->com_id != mb->com_id || ma->scene_id != mb->scene_id ||
            memcmp(ma->slots, mb->slots, sizeof(ma->slots)) != 0 ||
            memcmp(ma->identkit, mb->identkit, sizeof(ma->identkit)) != 0 ||
            memcmp(ma->colors, mb->colors, sizeof(ma->colors)) != 0 || ma->gender != mb->gender ||
            ma->anim_id != mb->anim_id || ma->built != mb->built || ma->close_serial != mb->close_serial )
        {
            Ckpt_Why(why, why_cap, "if_player_models[%d] (com 0x%08x)", i, (unsigned)ma->com_id);
            return 0;
        }
    }
    CKPT_EQ_INT(a, b, detached_parent_count, why, why_cap);
    for( int i = 0; i < a->detached_parent_count; i++ )
        if( a->detached_parents[i].container_uid != b->detached_parents[i].container_uid ||
            a->detached_parents[i].group_id != b->detached_parents[i].group_id ||
            a->detached_parents[i].type != b->detached_parents[i].type )
        {
            Ckpt_Why(why, why_cap, "detached_parents[%d]", i);
            return 0;
        }
    CKPT_EQ_INT(a, b, modal_host_uid, why, why_cap);
    return 1;
}

/* --- music ---------------------------------------------------------------- */

struct AppCkptMusic
{
    /* The track the server asked for, -1 for none, and its held variant. */
    int track;
    int secondary;
};

/* The track a jingle or a pending request stands in front of is still the
 * track: a jingle is transient (the pre-roll plays it), the track is not. */
static int
app_ckpt_music_track(struct ToriRS_MusicPlayer const* m)
{
    if( m->has_request )
        return m->request_source == TORIRS_MUSIC_SOURCE_TRACK ? m->request_song : m->resume_song;
    if( m->stop_pending )
        return -1;
    if( m->current_source == TORIRS_MUSIC_SOURCE_JINGLE )
        return m->resume_song;
    return m->current_song;
}

void*
App_CheckpointMusicTake(struct App const* app)
{
    struct AppCkptMusic* cp;

    assert(app);
    cp = ToriMem_Calloc(ToriMem_Session(), 1, sizeof(*cp));
    assert(cp);
    cp->track = app_ckpt_music_track(&app->audio.music);
    cp->secondary = app->audio.music.secondary_song;
    return cp;
}

void
App_CheckpointMusicRestore(
    struct App* app,
    void const* cp_v)
{
    struct AppCkptMusic const* cp = cp_v;

    assert(app);
    assert(cp);
    if( app_ckpt_music_track(&app->audio.music) != cp->track )
    {
        if( cp->track < 0 )
            ToriRS_Music_Stop(&app->audio.music, 0);
        else
            ToriRS_Music_Request(&app->audio.music, cp->track, TORIRS_MUSIC_SOURCE_TRACK, true, 0, 0);
    }
    ToriRS_Music_SetSecondary(&app->audio.music, cp->secondary);
}

void
App_CheckpointMusicFree(void* cp)
{
    ToriMem_Free(ToriMem_Session(), cp);
}

size_t
App_CheckpointMusicBytes(void const* cp)
{
    assert(cp);
    return sizeof(*(struct AppCkptMusic const*)cp);
}

int
App_CheckpointMusicEqual(
    void const* a_v,
    void const* b_v,
    char* why,
    int why_cap)
{
    struct AppCkptMusic const* a = a_v;
    struct AppCkptMusic const* b = b_v;

    assert(a);
    assert(b);
    CKPT_EQ_INT(a, b, track, why, why_cap);
    CKPT_EQ_INT(a, b, secondary, why, why_cap);
    return 1;
}

/* --- after a restore -------------------------------------------------------- */

void
App_CheckpointAfterRestore(struct App* app)
{
    uint64_t client_cycle;

    assert(app);
    /* The emit list's descs point at node strings the restore freed, and the
     * retained-frame gate trusts the list it last published. */
    UITree_EmitBufferFree(&app->emit);
    UITree_EmitBufferInit(&app->emit);
    memset(&app->emit_gate, 0, sizeof(app->emit_gate));
    app->render_skip.catch_up_ok = 0;
    app->render_skip.fresh = 0;
    app->ui_retained_frame = 0;
    app->damage.valid = 0;

    /* Pointer, focus and drag state names nodes of the tree that was. */
    client_cycle = app->interact.client_cycle;
    UIInteraction_Init(&app->interact);
    app->interact.client_cycle = client_cycle;
    UIInvDrag_Reset(&app->inv_drag);
    UIHoverText_Reset(&app->hover_text);
    app->hover_com_id = -1;
    app->clicked_com_id = -1;

    /* The frame binder's and the plugin layer's memory of the tree. The tree's
     * generation moved, so each of these would miss anyway; dropping them says
     * so rather than relying on it. */
    app->plugin_frame_bound_valid = 0;
    app->plugin_frame_bound_tree = NULL;
    app->plugin_facet_ids.sidetab_valid = 0;
    app->plugin_slot_node_gen = 0;
    UITree_FrameForget(app->tree);

    /* Everything that reads the session: a relayout (the tree did it), the
     * CS1 conditions, and a frame. */
    app->cs1_host.eval_dirty = true;
    app->pending_tree_refresh = 1;
    app->need_redraw = 1;
}
