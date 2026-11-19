/*
 * The headless client's shadow of the packet model (scriptrun_shadow.h): the
 * shared apply fed beside the core's restated rules, and the comparer.
 */

#include "scriptrun/scriptrun_shadow.h"

#include "game/rs_chat.h"
#include "game/rs_entity_sync.h"
#include "game/rs_event_ring.h"
#include "game/rs_hint_arrow.h"
#include "game/rs_interaction_mode.h"
#include "game/rs_npc_update_origin.h"
#include "game/rs_player_ops.h"
#include "game/rs_player_stats.h"
#include "game/rs_server_state.h"
#include "game/rs_server_tick.h"
#include "game/rs_session_info.h"
#include "game/rs_social.h"
#include "game/rs_ui_slots.h"
#include "inv/inv_manager.h"
#include "mem/torirs_mem.h"
#include "net/rev/revpacket.h"
#include "net/session_end.h"
#include "scriptrun/scriptrun_core.h"
#include "ui/uitree_if_events.h"
#include "ui/uitree_if_store.h"
#include "varp/varp_manager.h"
#include "world/world.h"
#include "world/worldview.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum
{
    /* The shadow's ring: what one packet stamps is read back by the same
     * packet's After, so it only has to hold one packet's events. */
    SHADOW_EVENTS = 1024,
    /* One packet's events or chat lines, per side. A packet that stamps more
     * is compared on its first SHADOW_PER_PACKET and the rest are counted. */
    SHADOW_PER_PACKET = 64,
    SHADOW_VALUE_LEN = 320,
    SHADOW_KEY_LEN = 64,
};

/* The domains a packet's touch is recorded under (shadow_touch). */
enum
{
    TOUCH_VARP = 1,
    TOUCH_ALL_VARPS,
    TOUCH_STAT,
    TOUCH_RUN,
    TOUCH_INV,
    TOUCH_COM,
    TOUCH_GROUP,
    TOUCH_ALL_COMS,
};

/* Open addressing, uint64 keys, 0 is an empty slot. Grows at half full. */
struct ShadowMap
{
    uint64_t* keys;
    uint64_t* a;
    uint32_t* b;
    int cap;
    int count;
};

struct ScriptrunShadow
{
    /* ---- the records the shared apply writes ---- */
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct InvManager invs;
    struct UIIfTextStore if_texts;
    struct UIIfIntStore if_hides;
    struct UIIfIntStore if_colours;
    struct UIIfEventTable if_events;
    struct RS_Chat chat;
    struct RS_Social social;
    struct RS_SessionInfo session_info;
    struct SessionEnd session_end;
    struct RS_ServerTick server_tick;
    struct RS_PlayerOps player_ops;
    struct RS_HintArrow hint_arrow;
    struct RS_NpcUpdateOrigin npc_update_origin;
    struct RS_InteractionMode interaction_mode;
    struct WorldviewRegistry worldviews;
    struct World* world;
    struct RS_EntitySync esync;
    int chat_filter_mode[RS_UI_CHAT_FILTER_COUNT];
    struct RS_Event entries[SHADOW_EVENTS];
    struct RS_EventRing ring;
    struct RS_ServerState state;

    /* ---- the comparer ---- */
    FILE* out;
    ScriptrunShadow_PktNameFn pkt_name;
    uint32_t packet_serial;
    uint32_t core_event_mark;
    uint32_t shadow_event_mark;
    int core_message_mark;
    int shadow_message_mark;
    uint32_t compare_gen;
    /* (domain, id) -> (packet serial << 32 | packet kind) */
    struct ShadowMap touches;
    /* hash(family, key) -> a: hash(old, shared), b: the compare that saw it */
    struct ShadowMap diffs;
    int rows[SCRIPTRUN_SHADOW_FAMILY_COUNT];
    int keys[SCRIPTRUN_SHADOW_FAMILY_COUNT];

    int applied;
    int unchanged;
    int passed;
    int entity_setters;
    int overflow;
};

static char const* const k_family_names[SCRIPTRUN_SHADOW_FAMILY_COUNT] = {
    "varps", "stats", "invs", "messages", "if_texts", "if_hides", "if_events", "events",
};

char const*
ScriptrunShadow_FamilyName(int family)
{
    assert(family >= 0);
    assert(family < SCRIPTRUN_SHADOW_FAMILY_COUNT);
    return k_family_names[family];
}

/* ------------------------------------------------------------------ the map */

static uint64_t
map_mix(uint64_t k)
{
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdull;
    k ^= k >> 33;
    return k;
}

static void
map_free(struct ShadowMap* m)
{
    ToriMem_Free(ToriMem_ServerPlayers(), m->keys);
    ToriMem_Free(ToriMem_ServerPlayers(), m->a);
    ToriMem_Free(ToriMem_ServerPlayers(), m->b);
    memset(m, 0, sizeof(*m));
}

static int
map_find(
    struct ShadowMap const* m,
    uint64_t key)
{
    assert(key != 0);
    if( m->cap == 0 )
        return -1;
    for( int i = (int)(map_mix(key) & (uint64_t)(m->cap - 1));; i = (i + 1) & (m->cap - 1) )
    {
        if( m->keys[i] == key )
            return i;
        if( m->keys[i] == 0 )
            return -1;
    }
}

static int map_put(struct ShadowMap* m, uint64_t key, int* inserted);

static void
map_grow(struct ShadowMap* m)
{
    struct ShadowMap old = *m;
    int const cap = old.cap ? old.cap * 2 : 256;

    m->keys = ToriMem_Calloc(ToriMem_ServerPlayers(), (size_t)cap, sizeof(*m->keys));
    m->a = ToriMem_Calloc(ToriMem_ServerPlayers(), (size_t)cap, sizeof(*m->a));
    m->b = ToriMem_Calloc(ToriMem_ServerPlayers(), (size_t)cap, sizeof(*m->b));
    assert(m->keys);
    assert(m->a);
    assert(m->b);
    m->cap = cap;
    m->count = 0;
    for( int i = 0; i < old.cap; i++ )
    {
        int inserted;
        int at;

        if( old.keys[i] == 0 )
            continue;
        at = map_put(m, old.keys[i], &inserted);
        m->a[at] = old.a[i];
        m->b[at] = old.b[i];
    }
    map_free(&old);
}

/* The slot for `key`, made (zeroed) when absent. */
static int
map_put(
    struct ShadowMap* m,
    uint64_t key,
    int* inserted)
{
    int i;

    assert(key != 0);
    *inserted = 0;
    if( (m->count + 1) * 2 > m->cap )
        map_grow(m);
    for( i = (int)(map_mix(key) & (uint64_t)(m->cap - 1)); m->keys[i] != 0;
         i = (i + 1) & (m->cap - 1) )
        if( m->keys[i] == key )
            return i;
    m->keys[i] = key;
    m->a[i] = 0;
    m->b[i] = 0;
    m->count++;
    *inserted = 1;
    return i;
}

static uint64_t
fnv(
    uint64_t h,
    char const* s)
{
    for( ; *s; s++ )
    {
        h ^= (unsigned char)*s;
        h *= 0x100000001b3ull;
    }
    h ^= 0xff;
    h *= 0x100000001b3ull;
    return h;
}

/* ---------------------------------------------------------------- touches */

static uint64_t
touch_key(
    int domain,
    int id)
{
    return ((uint64_t)(uint32_t)domain << 32) | (uint32_t)id;
}

static void
shadow_touch(
    struct ScriptrunShadow* sh,
    int domain,
    int id,
    int pkt)
{
    int inserted;
    int at = map_put(&sh->touches, touch_key(domain, id), &inserted);

    sh->touches.a[at] = ((uint64_t)sh->packet_serial << 32) | (uint32_t)pkt;
}

/* The latest of the touches named, as (serial << 32 | kind); 0 for none. */
static uint64_t
shadow_touched(
    struct ScriptrunShadow const* sh,
    int domain,
    int id)
{
    int at = map_find(&sh->touches, touch_key(domain, id));
    return at < 0 ? 0 : sh->touches.a[at];
}

static uint64_t
latest(
    uint64_t x,
    uint64_t y)
{
    return (x >> 32) >= (y >> 32) ? x : y;
}

/* The group a core mount slot holds, or -1 (scriptrun_core.c
 * ScriptrunCore_MountedAt, read off the struct). */
static int
core_group_at(
    struct ScriptrunCore const* core,
    int target_uid)
{
    for( int i = 0; i < core->mount_count; i++ )
        if( core->mounts[i].target_uid == target_uid )
            return core->mounts[i].interface_id;
    return -1;
}

static void
shadow_note_touches(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    struct RevPacket const* p)
{
    int const kind = p->packet_type;

    if( kind == PKT_NAME_VARP_SMALL )
        shadow_touch(sh, TOUCH_VARP, p->_varp_small.variable, kind);
    else if( kind == PKT_NAME_VARP_LARGE )
        shadow_touch(sh, TOUCH_VARP, p->_varp_large.variable, kind);
    else if( kind == PKT_NAME_VARP_LONG )
        shadow_touch(sh, TOUCH_VARP, p->_varp_long.variable, kind);
    else if( kind == PKT_NAME_VARP_SYNC || kind == PKT_NAME_VARP_RESET )
        shadow_touch(sh, TOUCH_ALL_VARPS, 0, kind);
    else if( kind == PKT_NAME_UPDATE_STAT )
        shadow_touch(sh, TOUCH_STAT, p->_update_stat.stat, kind);
    else if( kind == PKT_NAME_UPDATE_RUNENERGY )
        shadow_touch(sh, TOUCH_RUN, 0, kind);
    else if( kind == PKT_NAME_UPDATE_INV_FULL )
    {
        shadow_touch(sh, TOUCH_INV, p->_update_inv_full.inv_id, kind);
        shadow_touch(sh, TOUCH_INV,
            RS_ServerState_InvContainerId(p->_update_inv_full.inv_id, p->_update_inv_full.component_id),
            kind);
    }
    else if( kind == PKT_NAME_UPDATE_INV_PARTIAL )
    {
        shadow_touch(sh, TOUCH_INV, p->_update_inv_partial.inv_id, kind);
        shadow_touch(sh, TOUCH_INV,
            RS_ServerState_InvContainerId(
                p->_update_inv_partial.inv_id, p->_update_inv_partial.component_id),
            kind);
    }
    else if( kind == PKT_NAME_UPDATE_INV_STOP_TRANSMIT )
    {
        shadow_touch(sh, TOUCH_INV, p->_update_inv_stop_transmit.inv_id, kind);
        shadow_touch(sh, TOUCH_INV,
            RS_ServerState_InvContainerId(
                p->_update_inv_stop_transmit.inv_id, p->_update_inv_stop_transmit.component_id),
            kind);
    }
    else if( kind == PKT_NAME_IF_SETTEXT )
        shadow_touch(sh, TOUCH_COM, p->_if_settext.component_id, kind);
    else if( kind == PKT_NAME_IF_SETHIDE )
        shadow_touch(sh, TOUCH_COM, p->_if_sethide.component_id, kind);
    else if( kind == PKT_NAME_IF_SETEVENTS )
        shadow_touch(sh, TOUCH_COM, p->_if_setevents.component_id, kind);
    else if( kind == PKT_NAME_IF_SETCOLOUR )
        shadow_touch(sh, TOUCH_COM, p->_if_setcolour.component_id, kind);
    else if( kind == PKT_NAME_IF_OPENSUB )
    {
        int const outgoing = core_group_at(core, p->_if_opensub.target_uid);

        shadow_touch(sh, TOUCH_GROUP, p->_if_opensub.interface_id, kind);
        if( outgoing >= 0 )
            shadow_touch(sh, TOUCH_GROUP, outgoing, kind);
    }
    else if( kind == PKT_NAME_IF_CLOSESUB )
    {
        int const outgoing = core_group_at(core, p->_if_closesub.target_uid);

        if( outgoing >= 0 )
            shadow_touch(sh, TOUCH_GROUP, outgoing, kind);
    }
    else if( kind == PKT_NAME_IF_MOVESUB )
    {
        int const moved = core_group_at(core, p->_if_movesub.source_uid);
        int const replaced = core_group_at(core, p->_if_movesub.dest_uid);

        if( moved >= 0 )
            shadow_touch(sh, TOUCH_GROUP, moved, kind);
        if( replaced >= 0 )
            shadow_touch(sh, TOUCH_GROUP, replaced, kind);
    }
    else if( kind == PKT_NAME_IF_OPENTOP || kind == PKT_NAME_IF_RESYNC_V2 )
        shadow_touch(sh, TOUCH_ALL_COMS, 0, kind);
}

static char const*
shadow_kind_name(
    struct ScriptrunShadow const* sh,
    uint64_t touched,
    char* buf,
    size_t cap)
{
    char const* name;

    if( touched == 0 )
        return "-";
    name = sh->pkt_name ? sh->pkt_name((int)(uint32_t)touched) : NULL;
    if( name )
        return name;
    snprintf(buf, cap, "pkt%u", (unsigned)(uint32_t)touched);
    return buf;
}

/* ------------------------------------------------------------------- rows */

/* A value for a TSV cell: tabs and line breaks become spaces. */
static void
cell(
    char* dst,
    size_t cap,
    char const* src)
{
    size_t n = 0;

    for( ; src && *src && n + 1 < cap; src++ )
        dst[n++] = (*src == '\t' || *src == '\n' || *src == '\r') ? ' ' : *src;
    dst[n] = '\0';
}

static void
shadow_row(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    int family,
    char const* key,
    char const* old_value,
    char const* shared_value,
    uint64_t touched)
{
    char o[SHADOW_VALUE_LEN];
    char s[SHADOW_VALUE_LEN];
    char kbuf[16];

    cell(o, sizeof(o), old_value);
    cell(s, sizeof(s), shared_value);
    sh->rows[family]++;
    fprintf(sh->out,
        "%d\t%s\t%s\t%s\t%s\t%s\t%d\n",
        core->tick,
        k_family_names[family],
        key,
        o,
        s,
        shadow_kind_name(sh, touched, kbuf, sizeof(kbuf)),
        core->esync->local_pid);
    /* A row is rare and a reader may look mid-run: each one reaches the
     * file when it is written. */
    fflush(sh->out);
}

/* A state family's key differs at this compare: a row when it did not differ
 * at the previous compare, or differed as another pair. */
static void
shadow_state_diff(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    int family,
    char const* key,
    char const* old_value,
    char const* shared_value,
    uint64_t touched)
{
    uint64_t const k = fnv(fnv(0xcbf29ce484222325ull, k_family_names[family]), key) | 1u;
    uint64_t const pair = fnv(fnv(0xcbf29ce484222325ull, old_value), shared_value);
    int inserted;
    int at = map_put(&sh->diffs, k, &inserted);

    if( inserted )
        sh->keys[family]++;
    if( !inserted && sh->diffs.a[at] == pair && sh->diffs.b[at] + 1 == sh->compare_gen )
    {
        sh->diffs.b[at] = sh->compare_gen;
        return;
    }
    sh->diffs.a[at] = pair;
    sh->diffs.b[at] = sh->compare_gen;
    shadow_row(sh, core, family, key, old_value, shared_value, touched);
}

/* A per-packet family's mismatch: every one is a row; its keys are counted
 * once each. */
static void
shadow_packet_diff(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    int family,
    char const* key,
    char const* old_value,
    char const* shared_value,
    int pkt)
{
    uint64_t const k = fnv(fnv(0xcbf29ce484222325ull, k_family_names[family]), key) | 1u;
    int inserted;

    (void)map_put(&sh->diffs, k, &inserted);
    if( inserted )
        sh->keys[family]++;
    shadow_row(sh,
        core,
        family,
        key,
        old_value,
        shared_value,
        ((uint64_t)sh->packet_serial << 32) | (uint32_t)pkt);
}

/* ------------------------------------------------------------- the shadow */

/* InvManager's change callback, bound as the real client binds it
 * (app/app_ui_host.c app_inv_ui_host_change stamps inv_changed per change). */
static void
shadow_inv_changed(
    void* user,
    int container_id)
{
    struct ScriptrunShadow* sh = (struct ScriptrunShadow*)user;

    RS_EventSink_Stamp(&sh->state.events, RS_EVENT_INV_CHANGED, container_id, 0, 0, 0);
}

static int s_shadow_opened;

struct ScriptrunShadow*
ScriptrunShadow_New(
    char const* path,
    int rev_number,
    int const* cycle,
    ScriptrunShadow_PktNameFn pkt_name)
{
    struct ScriptrunShadow* sh;
    struct RS_EventSink sink;

    if( !path || !path[0] )
        return NULL;
    assert(cycle);
    sh = ToriMem_Calloc(ToriMem_ServerPlayers(), 1, sizeof(*sh));
    assert(sh);
    sh->out = fopen(path, s_shadow_opened ? "a" : "w");
    if( !sh->out )
    {
        fprintf(stderr, "scriptrun-shadow: cannot open %s\n", path);
        assert(sh->out);
    }
    if( !s_shadow_opened )
        fprintf(sh->out, "# tick\tfamily\tkey\told\tshared\tlast_packet\tpid\n");
    s_shadow_opened = 1;
    sh->pkt_name = pkt_name;

    VarPManager_Init(&sh->varps);
    RS_PlayerStats_Init(&sh->stats);
    InvManager_Init(&sh->invs);
    RS_Chat_Init(&sh->chat, "");
    RS_Social_Init(&sh->social);
    RS_SessionInfo_Reset(&sh->session_info);
    RS_InteractionMode_Reset(&sh->interaction_mode);
    WorldviewRegistry_Init(&sh->worldviews);
    sh->world = World_New();
    assert(sh->world);
    RS_EntitySync_Init(&sh->esync);

    sink = (struct RS_EventSink){
        .ring = &sh->ring,
        .entries = sh->entries,
        .capacity = SHADOW_EVENTS,
        .full = RS_EVENT_RING_OVERWRITE,
        .cycle = cycle,
    };
    RS_Chat_SetEventSink(&sh->chat, sink);
    sh->state = (struct RS_ServerState){
        .varps = &sh->varps,
        .stats = &sh->stats,
        .invs = &sh->invs,
        .if_texts = &sh->if_texts,
        .if_hides = &sh->if_hides,
        .if_colours = &sh->if_colours,
        .if_events = &sh->if_events,
        .chat = &sh->chat,
        .chat_clock = cycle,
        .social = &sh->social,
        .chat_filter_mode = sh->chat_filter_mode,
        .session_info = &sh->session_info,
        .session_end = &sh->session_end,
        .server_tick = &sh->server_tick,
        .player_ops = &sh->player_ops,
        .hint_arrow = &sh->hint_arrow,
        .npc_update_origin = &sh->npc_update_origin,
        .interaction_mode = &sh->interaction_mode,
        .rev_number = rev_number,
        .worldviews = &sh->worldviews,
        .world = sh->world,
        .esync = &sh->esync,
        .provider = NULL,
        .events = sink,
    };
    InvManager_SetChangeCallback(&sh->invs, shadow_inv_changed, sh);
    return sh;
}

void
ScriptrunShadow_Free(struct ScriptrunShadow* sh)
{
    if( !sh )
        return;
    fprintf(stderr,
        "scriptrun-shadow: pid %d packets %u applied %d unchanged %d pass %d entity_setters %d "
        "overflow %d\n",
        sh->esync.local_pid,
        (unsigned)sh->packet_serial,
        sh->applied,
        sh->unchanged,
        sh->passed,
        sh->entity_setters,
        sh->overflow);
    for( int f = 0; f < SCRIPTRUN_SHADOW_FAMILY_COUNT; f++ )
        fprintf(stderr,
            "scriptrun-shadow: family %s rows %d keys %d\n",
            k_family_names[f],
            sh->rows[f],
            sh->keys[f]);
    fclose(sh->out);
    map_free(&sh->touches);
    map_free(&sh->diffs);
    RS_EntitySync_Free(&sh->esync);
    World_Free(sh->world);
    UIIfTextStore_Free(&sh->if_texts);
    UIIfIntStore_Free(&sh->if_hides);
    UIIfIntStore_Free(&sh->if_colours);
    UIIfEventTable_Free(&sh->if_events);
    RS_Chat_Free(&sh->chat);
    InvManager_Free(&sh->invs);
    VarPManager_Free(&sh->varps);
    ToriMem_Free(ToriMem_ServerPlayers(), sh);
}

int
ScriptrunShadow_Rows(
    struct ScriptrunShadow const* sh,
    int family)
{
    assert(sh);
    assert(family >= 0);
    assert(family < SCRIPTRUN_SHADOW_FAMILY_COUNT);
    return sh->rows[family];
}

int
ScriptrunShadow_Keys(
    struct ScriptrunShadow const* sh,
    int family)
{
    assert(sh);
    assert(family >= 0);
    assert(family < SCRIPTRUN_SHADOW_FAMILY_COUNT);
    return sh->keys[family];
}

void
ScriptrunShadow_Before(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    struct RevPacket const* packet)
{
    int const kind = packet->packet_type;
    enum RS_ServerStateApply applied;

    assert(sh);
    assert(core);
    assert(packet);
    sh->packet_serial++;
    sh->core_event_mark = core->events.newest_serial;
    sh->shadow_event_mark = sh->ring.newest_serial;
    sh->core_message_mark = core->message_serial;
    sh->shadow_message_mark = sh->chat.next_message_serial;
    /* The shadow's own player index follows the core's, for the rows and for
     * ANIM_SPECIFIC's local pid. */
    sh->esync.local_pid = core->esync->local_pid;
    shadow_note_touches(sh, core, packet);

    /* Process-global: the login's uid (net/login/login_osrs.c), which the
     * core's own run must not see change. */
    if( kind == PKT_NAME_UPDATE_UID192 )
        return;
    if( kind == PKT_NAME_ANIM_SPECIFIC || kind == PKT_NAME_NPC_ANIM_SPECIFIC ||
        kind == PKT_NAME_NPC_SPOTANIM_SPECIFIC || kind == PKT_NAME_PLAYER_SPOTANIM_SPECIFIC ||
        kind == PKT_NAME_NPC_HEADICON_SPECIFIC )
        sh->entity_setters++;
    applied = RS_ServerState_Apply(&sh->state, packet);
    if( applied == RS_SERVER_STATE_APPLIED )
        sh->applied++;
    else if( applied == RS_SERVER_STATE_UNCHANGED )
        sh->unchanged++;
    else
        sh->passed++;
}

/* ------------------------------------------------------- per-packet compare */

static int
shadow_event_kind_shared(int kind)
{
    return kind == RS_EVENT_VARP_CHANGED || kind == RS_EVENT_INV_CHANGED ||
           kind == RS_EVENT_INV_PACKET || kind == RS_EVENT_CHAT_MESSAGE ||
           kind == RS_EVENT_SERVER_TICK || kind == RS_EVENT_NPC_SEQ;
}

/* The events of the shared kinds stamped after `mark`, oldest first. */
static int
shadow_collect_events(
    struct ScriptrunShadow* sh,
    struct RS_EventRing const* ring,
    struct RS_Event const* entries,
    int capacity,
    uint32_t mark,
    struct RS_Event* out)
{
    int n = 0;
    uint32_t from = mark + 1;

    if( ring->count == 0 )
        return 0;
    if( from < ring->oldest_serial )
        from = ring->oldest_serial;
    for( uint32_t s = from; s <= ring->newest_serial; s++ )
    {
        struct RS_Event const* ev = RS_EventRing_At(ring, entries, capacity, s);

        if( !shadow_event_kind_shared(ev->kind) )
            continue;
        if( n == SHADOW_PER_PACKET )
        {
            sh->overflow++;
            continue;
        }
        out[n++] = *ev;
    }
    return n;
}

static void
event_cell(
    struct RS_Event const* ev,
    char* buf,
    size_t cap)
{
    if( !ev )
        snprintf(buf, cap, "-");
    else
        snprintf(buf,
            cap,
            "%s %d,%d,%d,%d",
            RS_EventKindName(ev->kind),
            (int)ev->a,
            (int)ev->b,
            (int)ev->c,
            (int)ev->d);
}

/* One step of an alignment: both sides hold the element (match), or only the
 * core does (old), or only the model does (shared). */
enum
{
    ALIGN_MATCH = 0,
    ALIGN_OLD,
    ALIGN_SHARED,
};

struct ShadowAlign
{
    int op;
    int i; /* index into the old side, ALIGN_MATCH / ALIGN_OLD */
    int j; /* index into the shared side, ALIGN_MATCH / ALIGN_SHARED */
};

/* Align the two sides of one packet by their longest common subsequence of
 * element hashes, so one element missing on a side is one row and not a
 * shifted run of them. Returns the step count, at most na + nb. */
static int
shadow_align(
    uint64_t const* a,
    int na,
    uint64_t const* b,
    int nb,
    struct ShadowAlign* out)
{
    uint16_t len[SHADOW_PER_PACKET + 1][SHADOW_PER_PACKET + 1];
    int n = 0;
    int i = 0;
    int j = 0;

    assert(na <= SHADOW_PER_PACKET);
    assert(nb <= SHADOW_PER_PACKET);
    for( int x = na; x >= 0; x-- )
        for( int y = nb; y >= 0; y-- )
            len[x][y] = (x == na || y == nb) ? 0
                        : a[x] == b[y]       ? (uint16_t)(len[x + 1][y + 1] + 1)
                        : len[x + 1][y] >= len[x][y + 1] ? len[x + 1][y]
                                                         : len[x][y + 1];
    while( i < na || j < nb )
    {
        if( i < na && j < nb && a[i] == b[j] )
            out[n++] = (struct ShadowAlign){ ALIGN_MATCH, i++, j++ };
        else if( j == nb || (i < na && len[i + 1][j] >= len[i][j + 1]) )
            out[n++] = (struct ShadowAlign){ ALIGN_OLD, i++, -1 };
        else
            out[n++] = (struct ShadowAlign){ ALIGN_SHARED, -1, j++ };
    }
    return n;
}

/* Walk an alignment and hand each difference to `emit`: an old-only step
 * next to a shared-only step with the same `key` is one row with both
 * values (the same event with another payload), any other unmatched step is
 * a row with "-" on the side that lacks it. */
static void
shadow_emit_alignment(
    struct ShadowAlign const* steps,
    int n,
    int const* old_key,
    int const* shared_key,
    void (*emit)(void* ctx, int i, int j),
    void* ctx)
{
    for( int k = 0; k < n; k++ )
    {
        struct ShadowAlign const* s = &steps[k];
        struct ShadowAlign const* next = k + 1 < n ? &steps[k + 1] : NULL;

        if( s->op == ALIGN_MATCH )
            continue;
        if( next && s->op == ALIGN_OLD && next->op == ALIGN_SHARED &&
            old_key[s->i] == shared_key[next->j] )
        {
            emit(ctx, s->i, next->j);
            k++;
        }
        else if( next && s->op == ALIGN_SHARED && next->op == ALIGN_OLD &&
                 shared_key[s->j] == old_key[next->i] )
        {
            emit(ctx, next->i, s->j);
            k++;
        }
        else
            emit(ctx, s->i, s->j);
    }
}

static uint64_t
event_hash(struct RS_Event const* ev)
{
    uint64_t h = 0xcbf29ce484222325ull;
    int32_t const f[5] = { ev->kind, ev->a, ev->b, ev->c, ev->d };

    for( int i = 0; i < 5; i++ )
        h = map_mix(h ^ (uint64_t)(uint32_t)f[i]) + (uint64_t)i;
    return h;
}

struct EventEmit
{
    struct ScriptrunShadow* sh;
    struct ScriptrunCore const* core;
    int pkt;
    struct RS_Event const* old_ev;
    struct RS_Event const* new_ev;
};

static void
event_emit(
    void* ctx,
    int i,
    int j)
{
    struct EventEmit const* e = (struct EventEmit const*)ctx;
    struct RS_Event const* o = i >= 0 ? &e->old_ev[i] : NULL;
    struct RS_Event const* s = j >= 0 ? &e->new_ev[j] : NULL;
    char ob[96];
    char sb[96];

    event_cell(o, ob, sizeof(ob));
    event_cell(s, sb, sizeof(sb));
    shadow_packet_diff(e->sh,
        e->core,
        SCRIPTRUN_SHADOW_EVENTS,
        RS_EventKindName(o ? o->kind : s->kind),
        ob,
        sb,
        e->pkt);
}

static void
shadow_compare_events(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    int pkt)
{
    struct RS_Event old_ev[SHADOW_PER_PACKET];
    struct RS_Event new_ev[SHADOW_PER_PACKET];
    uint64_t old_hash[SHADOW_PER_PACKET];
    uint64_t new_hash[SHADOW_PER_PACKET];
    int old_key[SHADOW_PER_PACKET];
    int new_key[SHADOW_PER_PACKET];
    struct ShadowAlign steps[2 * SHADOW_PER_PACKET];
    int const n_old = shadow_collect_events(
        sh, &core->events, core->event_entries, SCRIPTRUN_EVENTS, sh->core_event_mark, old_ev);
    int const n_new =
        shadow_collect_events(sh, &sh->ring, sh->entries, SHADOW_EVENTS, sh->shadow_event_mark, new_ev);
    struct EventEmit e = { sh, core, pkt, old_ev, new_ev };
    int n;

    if( n_old == 0 && n_new == 0 )
        return;
    for( int i = 0; i < n_old; i++ )
    {
        old_hash[i] = event_hash(&old_ev[i]);
        old_key[i] = old_ev[i].kind;
    }
    for( int j = 0; j < n_new; j++ )
    {
        new_hash[j] = event_hash(&new_ev[j]);
        new_key[j] = new_ev[j].kind;
    }
    n = shadow_align(old_hash, n_old, new_hash, n_new, steps);
    shadow_emit_alignment(steps, n, old_key, new_key, event_emit, &e);
}

struct MessageEmit
{
    struct ScriptrunShadow* sh;
    struct ScriptrunCore const* core;
    int pkt;
    struct ScriptrunMessage const* const* old_lines;
    struct RS_ChatMessage const* const* new_lines;
};

static void
message_emit(
    void* ctx,
    int i,
    int j)
{
    struct MessageEmit const* e = (struct MessageEmit const*)ctx;
    struct ScriptrunMessage const* o = i >= 0 ? e->old_lines[i] : NULL;
    struct RS_ChatMessage const* s = j >= 0 ? e->new_lines[j] : NULL;
    char ob[SHADOW_VALUE_LEN];
    char sb[SHADOW_VALUE_LEN];
    char key[SHADOW_KEY_LEN];

    if( o )
        snprintf(ob, sizeof(ob), "%d|%s|%s", o->type, o->name, o->text);
    else
        snprintf(ob, sizeof(ob), "-");
    if( s )
        snprintf(sb, sizeof(sb), "%d|%s|%s", s->type, s->sender, s->text);
    else
        snprintf(sb, sizeof(sb), "-");
    snprintf(key, sizeof(key), "type:%d", o ? o->type : s->type);
    shadow_packet_diff(e->sh, e->core, SCRIPTRUN_SHADOW_MESSAGES, key, ob, sb, e->pkt);
}

static uint64_t
message_hash(
    int type,
    char const* name,
    char const* text)
{
    char t[16];

    snprintf(t, sizeof(t), "%d", type);
    return fnv(fnv(fnv(0xcbf29ce484222325ull, t), name), text);
}

static void
shadow_compare_messages(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    int pkt)
{
    struct ScriptrunMessage const* old_lines[SHADOW_PER_PACKET];
    struct RS_ChatMessage const* new_lines[SHADOW_PER_PACKET];
    uint64_t old_hash[SHADOW_PER_PACKET];
    uint64_t new_hash[SHADOW_PER_PACKET];
    int old_key[SHADOW_PER_PACKET];
    int new_key[SHADOW_PER_PACKET];
    struct ShadowAlign steps[2 * SHADOW_PER_PACKET];
    struct MessageEmit e;
    int n_old = 0;
    int n_new = 0;
    int n;

    for( int serial = sh->core_message_mark + 1; serial <= core->message_serial; serial++ )
    {
        struct ScriptrunMessage const* m = &core->messages[serial % SCRIPTRUN_MESSAGES];
        if( n_old == SHADOW_PER_PACKET || m->serial != serial )
        {
            sh->overflow++;
            continue;
        }
        old_hash[n_old] = message_hash(m->type, m->name, m->text);
        old_key[n_old] = m->type;
        old_lines[n_old++] = m;
    }
    /* messages[0] is the newest: walk back to oldest-first. */
    for( int i = sh->chat.message_count - 1; i >= 0; i-- )
    {
        struct RS_ChatMessage const* m = &sh->chat.messages[i];
        if( m->serial <= sh->shadow_message_mark )
            continue;
        if( n_new == SHADOW_PER_PACKET )
        {
            sh->overflow++;
            continue;
        }
        new_hash[n_new] = message_hash(m->type, m->sender, m->text);
        new_key[n_new] = m->type;
        new_lines[n_new++] = m;
    }
    if( n_old == 0 && n_new == 0 )
        return;
    e = (struct MessageEmit){ sh, core, pkt, old_lines, new_lines };
    n = shadow_align(old_hash, n_old, new_hash, n_new, steps);
    shadow_emit_alignment(steps, n, old_key, new_key, message_emit, &e);
}

/* ---------------------------------------------------------- the tick compare */

static void
shadow_int_diff(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    int family,
    char const* key,
    int have_old,
    long long old_value,
    int have_shared,
    long long shared_value,
    uint64_t touched)
{
    char o[32];
    char s[32];

    if( have_old == have_shared && (!have_old || old_value == shared_value) )
        return;
    if( have_old )
        snprintf(o, sizeof(o), "%lld", old_value);
    else
        snprintf(o, sizeof(o), "-");
    if( have_shared )
        snprintf(s, sizeof(s), "%lld", shared_value);
    else
        snprintf(s, sizeof(s), "-");
    shadow_state_diff(sh, core, family, key, o, s, touched);
}

static void
shadow_compare_varps(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core)
{
    uint64_t const all = shadow_touched(sh, TOUCH_ALL_VARPS, 0);
    int const n =
        sh->varps.varp_count > SCRIPTRUN_VARPS ? sh->varps.varp_count : SCRIPTRUN_VARPS;
    char key[SHADOW_KEY_LEN];

    for( int id = 0; id < n; id++ )
    {
        int const have_old = id < SCRIPTRUN_VARPS;
        int const old_value = have_old ? core->varps[id] : 0;
        int const shared_value = VarPManager_GetVarp(&sh->varps, id);

        if( have_old ? old_value == shared_value : shared_value == 0 )
            continue;
        snprintf(key, sizeof(key), "varp:%d", id);
        shadow_int_diff(sh,
            core,
            SCRIPTRUN_SHADOW_VARPS,
            key,
            have_old,
            old_value,
            1,
            shared_value,
            latest(shadow_touched(sh, TOUCH_VARP, id), all));
    }
    /* The long arm (VARP_LONG): the core keeps none. */
    for( int id = 0; id < sh->varps.var_long_count; id++ )
    {
        if( !sh->varps.var_long_set[id] )
            continue;
        snprintf(key, sizeof(key), "varp_long:%d", id);
        shadow_int_diff(sh,
            core,
            SCRIPTRUN_SHADOW_VARPS,
            key,
            0,
            0,
            1,
            (long long)sh->varps.var_long[id],
            latest(shadow_touched(sh, TOUCH_VARP, id), all));
    }
}

static void
shadow_compare_stats(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core)
{
    char key[SHADOW_KEY_LEN];

    for( int i = 0; i < SCRIPTRUN_STATS && i < RS_PLAYER_STATS_SKILL_COUNT; i++ )
    {
        uint64_t const touched = shadow_touched(sh, TOUCH_STAT, i);

        snprintf(key, sizeof(key), "stat:%d:level", i);
        shadow_int_diff(sh,
            core,
            SCRIPTRUN_SHADOW_STATS,
            key,
            1,
            core->stat_level[i],
            1,
            sh->stats.current_level[i],
            touched);
        snprintf(key, sizeof(key), "stat:%d:xp", i);
        shadow_int_diff(
            sh, core, SCRIPTRUN_SHADOW_STATS, key, 1, core->stat_xp[i], 1, sh->stats.xp[i], touched);
    }
    shadow_int_diff(sh,
        core,
        SCRIPTRUN_SHADOW_STATS,
        "run_energy_raw",
        1,
        core->run_energy,
        1,
        sh->stats.run_energy_raw,
        shadow_touched(sh, TOUCH_RUN, 0));
}

static void
slot_cell(
    int obj,
    int count,
    char* buf,
    size_t cap)
{
    if( obj < 0 )
        snprintf(buf, cap, "-");
    else
        snprintf(buf, cap, "%d x%d", obj, count);
}

static void
shadow_compare_inv(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    int inv_id)
{
    struct ScriptrunInv const* old_inv = NULL;
    struct InvContainer const* new_inv = InvManager_FindContainer(&sh->invs, inv_id);
    uint64_t const touched = shadow_touched(sh, TOUCH_INV, inv_id);
    char key[SHADOW_KEY_LEN];
    int slots;

    for( int i = 0; i < SCRIPTRUN_INVS; i++ )
        if( core->invs[i].inv_id == inv_id )
            old_inv = &core->invs[i];
    snprintf(key, sizeof(key), "inv:%d:size", inv_id);
    shadow_int_diff(sh,
        core,
        SCRIPTRUN_SHADOW_INVS,
        key,
        old_inv != NULL,
        old_inv ? old_inv->size : 0,
        new_inv != NULL,
        new_inv ? new_inv->slot_count : 0,
        touched);
    slots = old_inv ? SCRIPTRUN_INV_SLOTS : 0;
    if( new_inv && new_inv->slot_count > slots )
        slots = new_inv->slot_count;
    for( int s = 0; s < slots; s++ )
    {
        int const old_obj = old_inv && s < SCRIPTRUN_INV_SLOTS ? old_inv->obj[s] : -1;
        int const old_count = old_inv && s < SCRIPTRUN_INV_SLOTS ? old_inv->count[s] : 0;
        int const new_obj = new_inv && s < new_inv->slot_count ? new_inv->slots[s].obj_id : -1;
        int const new_count = new_inv && s < new_inv->slot_count ? new_inv->slots[s].obj_count : 0;
        char ob[32];
        char nb[32];

        if( old_obj < 0 && new_obj < 0 )
            continue;
        if( old_obj == new_obj && old_count == new_count )
            continue;
        slot_cell(old_obj, old_count, ob, sizeof(ob));
        slot_cell(new_obj, new_count, nb, sizeof(nb));
        snprintf(key, sizeof(key), "inv:%d:%d", inv_id, s);
        shadow_state_diff(sh, core, SCRIPTRUN_SHADOW_INVS, key, ob, nb, touched);
    }
}

static void
shadow_compare_invs(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core)
{
    for( int i = 0; i < SCRIPTRUN_INVS; i++ )
        if( core->invs[i].inv_id >= 0 )
            shadow_compare_inv(sh, core, core->invs[i].inv_id);
    for( int i = 0; i < sh->invs.container_count; i++ )
    {
        int const id = sh->invs.containers[i].inv_id;
        int in_core = 0;

        for( int c = 0; c < SCRIPTRUN_INVS; c++ )
            in_core |= core->invs[c].inv_id == id;
        if( !in_core )
            shadow_compare_inv(sh, core, id);
    }
}

static uint64_t
com_touched(
    struct ScriptrunShadow const* sh,
    int com)
{
    return latest(latest(shadow_touched(sh, TOUCH_COM, com),
                      shadow_touched(sh, TOUCH_GROUP, (com >> 16) & 0xffff)),
        shadow_touched(sh, TOUCH_ALL_COMS, 0));
}

static void
com_key(
    char* buf,
    size_t cap,
    int com)
{
    snprintf(buf, cap, "com:%d:%d", (com >> 16) & 0xffff, com & 0xffff);
}

static void
shadow_compare_texts(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core)
{
    char key[SHADOW_KEY_LEN];

    for( int i = 0; i < core->settext_count; i++ )
    {
        int const com = core->settexts[i].component;
        char const* shared = UIIfTextStore_Get(&sh->if_texts, com);

        if( shared && strcmp(shared, core->settexts[i].text) == 0 )
            continue;
        com_key(key, sizeof(key), com);
        shadow_state_diff(sh,
            core,
            SCRIPTRUN_SHADOW_IF_TEXTS,
            key,
            core->settexts[i].text,
            shared ? shared : "-",
            com_touched(sh, com));
    }
    for( int i = 0; i < sh->if_texts.count; i++ )
    {
        int const com = sh->if_texts.entries[i].com_id;
        int in_core = 0;

        for( int c = 0; c < core->settext_count && !in_core; c++ )
            in_core = core->settexts[c].component == com;
        if( in_core )
            continue;
        com_key(key, sizeof(key), com);
        shadow_state_diff(sh,
            core,
            SCRIPTRUN_SHADOW_IF_TEXTS,
            key,
            "-",
            sh->if_texts.entries[i].text,
            com_touched(sh, com));
    }
}

static void
shadow_compare_hides(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core)
{
    char key[SHADOW_KEY_LEN];

    for( int i = 0; i < core->sethide_count; i++ )
    {
        int const com = core->sethides[i].component;
        int const shared = UIIfIntStore_Get(&sh->if_hides, com, -1);

        com_key(key, sizeof(key), com);
        shadow_int_diff(sh,
            core,
            SCRIPTRUN_SHADOW_IF_HIDES,
            key,
            1,
            core->sethides[i].hidden,
            shared >= 0,
            shared,
            com_touched(sh, com));
    }
    for( int i = 0; i < sh->if_hides.count; i++ )
    {
        int const com = sh->if_hides.entries[i].com_id;
        int in_core = 0;

        for( int c = 0; c < core->sethide_count && !in_core; c++ )
            in_core = core->sethides[c].component == com;
        if( in_core )
            continue;
        com_key(key, sizeof(key), com);
        shadow_int_diff(sh,
            core,
            SCRIPTRUN_SHADOW_IF_HIDES,
            key,
            0,
            0,
            1,
            sh->if_hides.entries[i].value,
            com_touched(sh, com));
    }
}

static struct UIIfEventRange const*
range_find(
    struct UIIfEventTable const* table,
    struct UIIfEventRange const* r)
{
    for( int i = 0; i < table->count; i++ )
        if( table->ranges[i].com_id == r->com_id && table->ranges[i].from == r->from &&
            table->ranges[i].to == r->to )
            return &table->ranges[i];
    return NULL;
}

static void
shadow_range_diff(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    struct UIIfEventRange const* r,
    struct UIIfEventRange const* old_r,
    struct UIIfEventRange const* new_r)
{
    char key[SHADOW_KEY_LEN];
    char ob[16];
    char nb[16];

    snprintf(key,
        sizeof(key),
        "com:%d:%d[%d..%d]",
        (r->com_id >> 16) & 0xffff,
        r->com_id & 0xffff,
        r->from,
        r->to);
    if( old_r )
        snprintf(ob, sizeof(ob), "0x%x", (unsigned)old_r->events);
    else
        snprintf(ob, sizeof(ob), "-");
    if( new_r )
        snprintf(nb, sizeof(nb), "0x%x", (unsigned)new_r->events);
    else
        snprintf(nb, sizeof(nb), "-");
    shadow_state_diff(sh, core, SCRIPTRUN_SHADOW_IF_EVENTS, key, ob, nb, com_touched(sh, r->com_id));
}

static void
shadow_compare_if_events(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core)
{
    for( int i = 0; i < core->if_events.count; i++ )
    {
        struct UIIfEventRange const* o = &core->if_events.ranges[i];
        struct UIIfEventRange const* s = range_find(&sh->if_events, o);

        if( s && s->events == o->events )
            continue;
        shadow_range_diff(sh, core, o, o, s);
    }
    for( int i = 0; i < sh->if_events.count; i++ )
    {
        struct UIIfEventRange const* s = &sh->if_events.ranges[i];

        if( !range_find(&core->if_events, s) )
            shadow_range_diff(sh, core, s, NULL, s);
    }
}

void
ScriptrunShadow_After(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore const* core,
    struct RevPacket const* packet)
{
    int const kind = packet->packet_type;

    assert(sh);
    assert(core);
    assert(packet);
    shadow_compare_messages(sh, core, kind);
    shadow_compare_events(sh, core, kind);
    if( kind != PKT_NAME_SERVER_TICK_END )
        return;
    sh->compare_gen++;
    shadow_compare_varps(sh, core);
    shadow_compare_stats(sh, core);
    shadow_compare_invs(sh, core);
    shadow_compare_texts(sh, core);
    shadow_compare_hides(sh, core);
    shadow_compare_if_events(sh, core);
}
