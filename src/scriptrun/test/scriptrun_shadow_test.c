/*
 * The headless client's shadow of the packet model (scriptrun/scriptrun_shadow.h)
 * against a hand-built core holding its own model (scriptrun/scriptrun_model.h):
 * the test plays the core -- the shared apply into its model, then its
 * presentation, or a write by hand where a case needs the two to differ --
 * feeds the same packets through the shadow, and reads the rows back from the
 * file.
 */
#include "game/rs_chat.h"
#include "game/rs_entity_sync.h"
#include "game/rs_event_ring.h"
#include "game/rs_server_state.h"
#include "inv/inv_manager.h"
#include "net/rev/revpacket.h"
#include "scriptrun/scriptrun_core.h"
#include "scriptrun/scriptrun_shadow.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                      \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

enum
{
    MAX_ROWS = 256,
};

struct Row
{
    int tick;
    char family[32];
    char key[64];
    char old_value[320];
    char shared_value[320];
    char packet[32];
};

static char s_path[512];

static char const*
test_pkt_name(int kind)
{
    switch( kind )
    {
    case PKT_NAME_VARP_SMALL:
        return "VARP_SMALL";
    case PKT_NAME_VARP_LONG:
        return "VARP_LONG";
    case PKT_NAME_IF_SETHIDE:
        return "IF_SETHIDE";
    case PKT_NAME_IF_CLOSESUB:
        return "IF_CLOSESUB";
    case PKT_NAME_UPDATE_INV_PARTIAL:
        return "UPDATE_INV_PARTIAL";
    case PKT_NAME_MESSAGE_GAME:
        return "MESSAGE_GAME";
    case PKT_NAME_IF_SETTEXT:
        return "IF_SETTEXT";
    case PKT_NAME_IF_OPENSUB:
        return "IF_OPENSUB";
    case PKT_NAME_SERVER_TICK_END:
        return "SERVER_TICK_END";
    default:
        return NULL;
    }
}

/* A core as ScriptrunCore_New leaves it, without the decoders: what the
 * shadow reads. */
static struct ScriptrunCore*
core_new(void)
{
    struct ScriptrunCore* core = calloc(1, sizeof(*core));

    assert(core);
    core->world = World_New();
    assert(core->world);
    core->esync = calloc(1, sizeof(*core->esync));
    assert(core->esync);
    RS_EntitySync_Init(core->esync);
    core->esync->local_pid = 3;
    ScriptrunModel_Init(&core->model,
        242,
        &core->world->cycle,
        core->world,
        core->esync,
        (struct RS_EventSink){
            .ring = &core->events,
            .entries = core->event_entries,
            .capacity = SCRIPTRUN_EVENTS,
            .full = RS_EVENT_RING_OVERWRITE,
            .cycle = &core->world->cycle,
        });
    ScriptrunZone_Init(&core->zone,
        (struct RS_EventSink){
            .ring = &core->events,
            .entries = core->event_entries,
            .capacity = SCRIPTRUN_EVENTS,
            .full = RS_EVENT_RING_OVERWRITE,
            .cycle = &core->world->cycle,
        });
    core->toplevel = -1;
    return core;
}

static void
core_free(struct ScriptrunCore* core)
{
    ScriptrunModel_Free(&core->model);
    ScriptrunZone_Free(&core->zone);
    RS_EntitySync_Free(core->esync);
    free(core->esync);
    World_Free(core->world);
    free(core);
}

/* The core's half of a packet: the shared apply into its own model, as
 * scriptrun_core.c core_packet does before its presentation. */
static void
core_apply(
    struct ScriptrunCore* core,
    struct RevPacket const* p)
{
    (void)RS_ServerState_Apply(&core->model.state, p);
}

/* One packet through both sides, with nothing presented. */
static void
both(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore* core,
    struct RevPacket const* p)
{
    ScriptrunShadow_Before(sh, core, p);
    core_apply(core, p);
    ScriptrunShadow_After(sh, core, p);
}

static void
core_stamp(
    struct ScriptrunCore* core,
    int kind,
    int a,
    int b)
{
    struct RS_Event ev = { .kind = kind, .cycle = core->world->cycle, .a = a, .b = b };

    RS_EventRing_Push(
        &core->events, core->event_entries, SCRIPTRUN_EVENTS, RS_EVENT_RING_OVERWRITE, &ev);
}

static struct RevPacket
packet(int name)
{
    struct RevPacket p;

    memset(&p, 0, sizeof(p));
    p.packet_type = name;
    return p;
}

/* SERVER_TICK_END through both: the model stamps server_tick on each. */
static void
tick_end(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore* core)
{
    struct RevPacket p = packet(PKT_NAME_SERVER_TICK_END);

    both(sh, core, &p);
    core->tick++;
    core->world->cycle += SCRIPTRUN_CYCLES_PER_TICK;
}

/* The login burst's stats and run energy, through both. */
static void
login(
    struct ScriptrunShadow* sh,
    struct ScriptrunCore* core)
{
    struct RevPacket p;

    for( int stat = 0; stat < RS_PLAYER_STATS_SKILL_COUNT; stat++ )
    {
        p = packet(PKT_NAME_UPDATE_STAT);
        p._update_stat.stat = stat;
        p._update_stat.level = stat == 3 ? 10 : 1;
        p._update_stat.xp = stat == 3 ? 1154 : 0;
        both(sh, core, &p);
    }
    p = packet(PKT_NAME_UPDATE_RUNENERGY);
    p._update_run_energy.run_energy_raw = 10000;
    p._update_run_energy.run_energy = 100;
    both(sh, core, &p);
}

static int
read_rows(struct Row* rows)
{
    FILE* f = fopen(s_path, "r");
    char line[2048];
    int n = 0;

    assert(f);
    while( fgets(line, sizeof(line), f) )
    {
        char* field[7];
        int k = 0;
        char* save = NULL;

        if( line[0] == '#' )
            continue;
        line[strcspn(line, "\n")] = '\0';
        for( char* tok = strtok_r(line, "\t", &save); tok && k < 7;
             tok = strtok_r(NULL, "\t", &save) )
            field[k++] = tok;
        CHECK(k == 7, "a row has seven cells");
        CHECK(n < MAX_ROWS, "the test reads few rows");
        rows[n].tick = atoi(field[0]);
        snprintf(rows[n].family, sizeof(rows[n].family), "%s", field[1]);
        snprintf(rows[n].key, sizeof(rows[n].key), "%s", field[2]);
        snprintf(rows[n].old_value, sizeof(rows[n].old_value), "%s", field[3]);
        snprintf(rows[n].shared_value, sizeof(rows[n].shared_value), "%s", field[4]);
        snprintf(rows[n].packet, sizeof(rows[n].packet), "%s", field[5]);
        CHECK(atoi(field[6]) == 3, "the pid column is the core's local pid");
        n++;
    }
    fclose(f);
    return n;
}

static int
count_family(
    struct Row const* rows,
    int n,
    char const* family)
{
    int c = 0;

    for( int i = 0; i < n; i++ )
        c += strcmp(rows[i].family, family) == 0;
    return c;
}

static struct Row const*
find_row(
    struct Row const* rows,
    int n,
    char const* family,
    char const* key)
{
    for( int i = n - 1; i >= 0; i-- )
        if( strcmp(rows[i].family, family) == 0 && strcmp(rows[i].key, key) == 0 )
            return &rows[i];
    return NULL;
}

static void
test_rules(void)
{
    struct ScriptrunCore* core = core_new();
    struct ScriptrunShadow* sh =
        ScriptrunShadow_New(s_path, 242, &core->world->cycle, test_pkt_name);
    struct Row rows[MAX_ROWS];
    struct Row const* r;
    struct RevPacket p;
    int n;

    CHECK(sh != NULL, "a path: a shadow");
    core->tick = 10;
    core->world->cycle = 300;
    login(sh, core);

    /* ---- agreement writes nothing: VARP_SMALL applied the same by both ---- */
    p = packet(PKT_NAME_VARP_SMALL);
    p._varp_small.variable = 5;
    p._varp_small.value = 3;
    both(sh, core, &p);
    tick_end(sh, core);
    n = read_rows(rows);
    CHECK(n == 0, "the same varp, the same stamp, the same tick: no row");

    /* ---- a packet the core's model never saw (a feed gap): VARP_LONG to
     * the shadow alone ---- */
    p = packet(PKT_NAME_VARP_LONG);
    p._varp_long.variable = 7;
    p._varp_long.value = 9;
    ScriptrunShadow_Before(sh, core, &p);
    ScriptrunShadow_After(sh, core, &p);
    n = read_rows(rows);
    r = find_row(rows, n, "events", "varp_changed");
    CHECK(r != NULL, "the model's varp_changed is a row at its packet");
    CHECK(strcmp(r->old_value, "-") == 0, "the core stamped none");
    CHECK(strncmp(r->shared_value, "varp_changed 7,", 15) == 0, "the model's names varp 7");
    CHECK(strcmp(r->packet, "VARP_LONG") == 0, "the row names its packet");
    tick_end(sh, core);
    n = read_rows(rows);
    r = find_row(rows, n, "varps", "varp_long:7");
    CHECK(r != NULL, "the long varp differs at the fence");
    CHECK(strcmp(r->old_value, "-") == 0, "the core's model holds no long arm for it");
    CHECK(strcmp(r->shared_value, "9") == 0, "the model holds 9");
    CHECK(strcmp(r->packet, "VARP_LONG") == 0, "the last packet that touched it");
    CHECK(r->tick == 11, "the row's tick is the core's");

    /* ---- a difference that stands is one row, not one per tick ---- */
    tick_end(sh, core);
    tick_end(sh, core);
    n = read_rows(rows);
    CHECK(count_family(rows, n, "varps") == 1, "a standing difference is written once");
    CHECK(ScriptrunShadow_Keys(sh, SCRIPTRUN_SHADOW_VARPS) == 1, "one key differs");

    /* ---- one that resolves and comes back is written again ---- */
    p = packet(PKT_NAME_VARP_SMALL);
    p._varp_small.variable = 5;
    p._varp_small.value = 4;
    ScriptrunShadow_Before(sh, core, &p);
    VarPManager_ApplySmall(&core->model.varps, 5, 8); /* the core's side disagrees */
    core_stamp(core, RS_EVENT_VARP_CHANGED, 5, 8);
    ScriptrunShadow_After(sh, core, &p);
    tick_end(sh, core);
    VarPManager_ApplySmall(&core->model.varps, 5, 4); /* agrees */
    tick_end(sh, core);
    VarPManager_ApplySmall(&core->model.varps, 5, 8); /* and differs again */
    tick_end(sh, core);
    n = read_rows(rows);
    {
        int c = 0;

        for( int i = 0; i < n; i++ )
            c += strcmp(rows[i].key, "varp:5") == 0;
        CHECK(c == 2, "a difference that came back after agreeing is a new row");
    }
    r = find_row(rows, n, "events", "varp_changed");
    CHECK(strcmp(r->old_value, "varp_changed 5,8,0,0") == 0, "the same event, another payload:");
    CHECK(strcmp(r->shared_value, "varp_changed 5,4,0,0") == 0, "  one row with both");

    /* ---- the core's presentation: a close unloads the group's hide and
     * text, which the bare model keeps ---- */
    core->mounts[0] = (struct ScriptrunMount){ .target_uid = (161 << 16) | 3, .interface_id = 233 };
    core->mount_count = 1;
    p = packet(PKT_NAME_IF_SETHIDE);
    p._if_sethide.component_id = (233 << 16) | 36;
    p._if_sethide.hide = 1;
    both(sh, core, &p);
    p = packet(PKT_NAME_IF_SETTEXT);
    p._if_settext.component_id = (233 << 16) | 2;
    p._if_settext.text = (char*)"Congratulations";
    both(sh, core, &p);
    tick_end(sh, core);
    n = read_rows(rows);
    CHECK(find_row(rows, n, "if_hides", "com:233:36") == NULL, "both hold the hide");
    CHECK(find_row(rows, n, "if_texts", "com:233:2") == NULL, "both hold the text");
    /* The same group reopened in its own slot keeps them on both. */
    p = packet(PKT_NAME_IF_OPENSUB);
    p._if_opensub.target_uid = (161 << 16) | 3;
    p._if_opensub.interface_id = 233;
    ScriptrunShadow_Before(sh, core, &p);
    CHECK(RS_ServerState_SlotGroupLeaves(&core->model.state, 233, 233) == 0,
        "a same-group remount unloads nothing");
    ScriptrunShadow_After(sh, core, &p);
    tick_end(sh, core);
    n = read_rows(rows);
    CHECK(find_row(rows, n, "if_hides", "com:233:36") == NULL, "the reopen kept the hide");
    p = packet(PKT_NAME_IF_CLOSESUB);
    p._if_closesub.target_uid = (161 << 16) | 3;
    ScriptrunShadow_Before(sh, core, &p);
    CHECK(RS_ServerState_SlotGroupLeaves(&core->model.state, 233, 0) == 1, "a close unloads");
    core->mount_count = 0;
    ScriptrunShadow_After(sh, core, &p);
    tick_end(sh, core);
    n = read_rows(rows);
    r = find_row(rows, n, "if_hides", "com:233:36");
    CHECK(r != NULL, "the close: the core dropped the hide");
    CHECK(strcmp(r->old_value, "-") == 0, "the core holds none");
    CHECK(strcmp(r->shared_value, "1") == 0, "the bare model still holds it");
    CHECK(strcmp(r->packet, "IF_CLOSESUB") == 0, "the group's close is the last touch");
    r = find_row(rows, n, "if_texts", "com:233:2");
    CHECK(r != NULL && strcmp(r->old_value, "-") == 0, "and the text with it");

    /* ---- the event stream is aligned, not compared index by index ---- */
    {
        int const before = count_family(rows, n, "events");
        struct PktUpdateInvPartialEntry entries[2] = {
            { .slot = 0, .obj_id = 1925, .count = 1 },
            { .slot = 1, .obj_id = 1926, .count = 2 },
        };
        int const slot_ids[2] = { 0, 1 };
        int const objs[2] = { 1925, 1926 };
        int const counts[2] = { 1, 2 };

        p = packet(PKT_NAME_UPDATE_INV_PARTIAL);
        p._update_inv_partial.inv_id = 93;
        p._update_inv_partial.count = 2;
        p._update_inv_partial.entries = entries;
        ScriptrunShadow_Before(sh, core, &p);
        /* The core's side by hand, as the headless client's own rule had it:
         * the container sized by the highest slot, both slots in one write
         * (one inv_changed), no inv_packet. */
        InvManager_EnsureContainer(&core->model.invs, 93, 2, "test");
        InvManager_ApplyPartial(&core->model.invs, 93, slot_ids, objs, counts, 2);
        ScriptrunShadow_After(sh, core, &p);
        n = read_rows(rows);
        /* The model: inv_packet, then inv_changed per slot; the core: one
         * inv_changed. Aligned, that is the missing inv_packet and the one
         * extra inv_changed -- two rows, where index pairing made three. */
        CHECK(count_family(rows, n, "events") - before == 2, "two event rows");
        r = find_row(rows, n, "events", "inv_packet");
        CHECK(r && strcmp(r->old_value, "-") == 0, "the core stamped no inv_packet");
        tick_end(sh, core);
        n = read_rows(rows);
        r = find_row(rows, n, "invs", "inv:93:size");
        CHECK(r != NULL, "the sizes differ: the core's 2, the model's container default");
        CHECK(strcmp(r->old_value, "2") == 0, "the core sized it by the highest slot");
        CHECK(find_row(rows, n, "invs", "inv:93:0") == NULL, "slot 0 agrees");
        CHECK(find_row(rows, n, "invs", "inv:93:1") == NULL, "slot 1 agrees");
    }

    /* ---- chat lines ---- */
    p = packet(PKT_NAME_MESSAGE_GAME);
    p._message_game.type = 0;
    p._message_game.text = (char*)"Welcome to RuneScape.";
    both(sh, core, &p);
    n = read_rows(rows);
    CHECK(count_family(rows, n, "messages") == 0, "the same game line on both");
    CHECK(find_row(rows, n, "events", "chat_message") == NULL, "the same serial on both");
    /* A public line the core's entity stream adds (scriptrun_core.c's
     * RSPROT_OP_CHAT arm), which the bare model never sees, arriving during
     * a packet. */
    p = packet(PKT_NAME_SERVER_TICK_END);
    ScriptrunShadow_Before(sh, core, &p);
    core_apply(core, &p);
    RS_Chat_AddMessage(&core->model.chat, RS_CHAT_TYPE_PUBLIC, "Bob", "Bob", "hi", core->world->cycle);
    ScriptrunShadow_After(sh, core, &p);
    n = read_rows(rows);
    r = find_row(rows, n, "messages", "type:2");
    CHECK(r != NULL, "the bare model has no public line");
    CHECK(strcmp(r->shared_value, "-") == 0, "shared side empty");
    CHECK(strstr(r->old_value, "Bob|hi") != NULL, "the core's line");
    r = find_row(rows, n, "events", "chat_message");
    CHECK(r != NULL && strcmp(r->shared_value, "-") == 0, "and its chat_message stamp");

    CHECK(ScriptrunShadow_Rows(sh, SCRIPTRUN_SHADOW_MESSAGES) == 1, "one messages row");
    CHECK(ScriptrunShadow_Rows(sh, SCRIPTRUN_SHADOW_IF_HIDES) == 1, "one if_hides row");
    CHECK(ScriptrunShadow_Rows(sh, SCRIPTRUN_SHADOW_IF_TEXTS) == 1, "one if_texts row");
    CHECK(ScriptrunShadow_Rows(sh, SCRIPTRUN_SHADOW_STATS) == 0, "the stats agree throughout");

    ScriptrunShadow_Free(sh);
    core_free(core);
}

/* The zone families: both zones fed the same is no row; a ground row and a
 * map graphic one side lacks are each a row, a standing one written once. */
static void
test_zone_families(void)
{
    struct ScriptrunCore* core = core_new();
    struct ScriptrunShadow* sh =
        ScriptrunShadow_New(s_path, 242, &core->world->cycle, test_pkt_name);
    struct ScriptrunZone* shadow_zone = ScriptrunShadow_Zone(sh);
    struct ScriptrunZoneAt at;
    struct PktObjAdd add = { .pos = (1 << 4) | 2, .obj_id = 995, .count = 5 };
    struct PktMapAnim anim = { .pos = 0, .id = 1234 };
    struct Row rows[MAX_ROWS];
    struct Row const* r;
    int n;

    CHECK(sh != NULL, "a shadow");
    /* The test writes the two sides apart on purpose. */
    ScriptrunShadow_SetStrictWorld(sh, 0);
    memset(&at, 0, sizeof(at));
    at.origin_x = 3200;
    at.origin_z = 3200;
    at.base_x = 8;
    at.base_z = 8;
    at.scene_size = 104;
    ScriptrunZone_Rebuild(&core->zone, 3200, 3200, 104);
    ScriptrunZone_Rebuild(shadow_zone, 3200, 3200, 104);

    ScriptrunZone_Apply(&core->zone, &at, 0, 0, PKT_NAME_OBJ_ADD, &add);
    ScriptrunZone_Apply(shadow_zone, &at, 0, 0, PKT_NAME_OBJ_ADD, &add);
    tick_end(sh, core);
    n = read_rows(rows);
    CHECK(count_family(rows, n, "ground") == 0, "the same row on both: no row");

    /* The shadow's zone alone takes a second pile and a map graphic. */
    add.count = 7;
    ScriptrunZone_Apply(shadow_zone, &at, 0, (int)core->world->cycle, PKT_NAME_OBJ_ADD, &add);
    ScriptrunZone_Apply(shadow_zone, &at, 0, (int)core->world->cycle, PKT_NAME_MAP_ANIM, &anim);
    tick_end(sh, core);
    tick_end(sh, core);
    n = read_rows(rows);
    r = find_row(rows, n, "ground", "3209,3210,0");
    CHECK(r != NULL, "the tile's rows differ");
    CHECK(strcmp(r->old_value, "995x5") == 0, "the core's one pile");
    CHECK(strcmp(r->shared_value, "995x5 995x7") == 0, "the shadow's two, in arrival order");
    CHECK(count_family(rows, n, "ground") == 1, "a standing difference is written once");
    CHECK(count_family(rows, n, "map_anims") == 1, "the map graphic only one side holds");
    CHECK(ScriptrunShadow_Rows(sh, SCRIPTRUN_SHADOW_PROJECTILES) == 0, "no projectile on either");

    ScriptrunShadow_Free(sh);
    core_free(core);
}

int
main(void)
{
    char const* tmp = getenv("TMPDIR");

    snprintf(s_path,
        sizeof(s_path),
        "%s/scriptrun_shadow_test_%d.tsv",
        tmp && tmp[0] ? tmp : "/tmp",
        (int)getpid());
    test_rules();
    remove(s_path);
    test_zone_families();
    remove(s_path);
    printf("scriptrun_shadow_test: all passed\n");
    return 0;
}
