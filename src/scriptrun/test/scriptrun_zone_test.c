/*
 * The headless client's zone families (scriptrun/scriptrun_zone.h) and the
 * effect log both clients keep (game/rs_effect_log.h): each rule the real
 * client applies, at the records.
 */
#include "game/rs_effect_log.h"
#include "game/rs_event_ring.h"
#include "net/rev/revpacket.h"
#include "scriptrun/scriptrun_zone.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    RING = 256,
};

static struct RS_Event s_entries[RING];
static struct RS_EventRing s_ring;
static int s_cycle;

static struct RS_EventSink
sink(void)
{
    return (struct RS_EventSink){
        .ring = &s_ring,
        .entries = s_entries,
        .capacity = RING,
        .full = RS_EVENT_RING_OVERWRITE,
        .cycle = &s_cycle,
    };
}

/* The root zone at scene tile (bx, bz), walked plane `level`. */
static struct ScriptrunZoneAt
at_root(
    int bx,
    int bz,
    int level)
{
    struct ScriptrunZoneAt at;

    memset(&at, 0, sizeof(at));
    at.view = 0;
    at.origin_x = 3200;
    at.origin_z = 3200;
    at.base_x = bx;
    at.base_z = bz;
    at.level = level;
    at.scene_size = 104;
    at.local_pid = 7;
    return at;
}

static int
pos(
    int x,
    int z)
{
    return ((x & 7) << 4) | (z & 7);
}

static struct ScriptrunZone*
zone_new(void)
{
    static struct ScriptrunZone zone;

    memset(&s_ring, 0, sizeof(s_ring));
    s_cycle = 0;
    ScriptrunZone_Init(&zone, sink());
    ScriptrunZone_Rebuild(&zone, 3200, 3200, 104);
    return &zone;
}

static void
obj_add(
    struct ScriptrunZone* zone,
    struct ScriptrunZoneAt const* at,
    int x,
    int z,
    int obj,
    int count)
{
    struct PktObjAdd p = { .pos = pos(x, z), .obj_id = obj, .count = count };
    ScriptrunZone_Apply(zone, at, 0, s_cycle, PKT_NAME_OBJ_ADD, &p);
}

static int
rows(
    struct ScriptrunZone const* zone,
    struct ScriptrunObjRow* out)
{
    int deck_x[SCRIPTRUN_ZONE_VIEWS] = { 0 };
    int deck_z[SCRIPTRUN_ZONE_VIEWS] = { 0 };
    return ScriptrunZone_Objs(zone, deck_x, deck_z, out, 64);
}

static void
test_ground(void)
{
    struct ScriptrunZone* zone = zone_new();
    struct ScriptrunZoneAt at = at_root(8, 16, 0);
    struct ScriptrunObjRow r[64];
    int n;

    /* Every add is a row, even of an id the tile already shows; arrival order. */
    obj_add(zone, &at, 1, 1, 995, 10);
    obj_add(zone, &at, 1, 1, 526, 1);
    obj_add(zone, &at, 1, 1, 995, 20);
    n = rows(zone, r);
    CHECK(n == 3, "three adds are three rows");
    CHECK(r[0].obj_id == 995 && r[0].count == 10, "the oldest first");
    CHECK(r[0].x == 3200 + 9 && r[0].z == 3200 + 17 && r[0].level == 0, "absolute tile");
    CHECK(s_ring.newest_serial == 3, "an obj_added per add");
    CHECK(s_entries[0].kind == RS_EVENT_OBJ_ADDED && s_entries[0].a == 995 && s_entries[0].b == 10 &&
              s_entries[0].c == 9 && s_entries[0].d == 17,
        "obj_added a=obj b=count c,d=scene tile");

    /* A count of 0 names the row already there and adds none. */
    obj_add(zone, &at, 1, 1, 995, 0);
    CHECK(rows(zone, r) == 3, "a count-0 add adds no row");

    /* OBJ_DEL takes the FIRST match, the oldest: the 10 pile goes, the 20
     * stays, and the order of the rest is kept. */
    {
        struct PktObjDel p = { .pos = pos(1, 1), .obj_id = 995 };
        ScriptrunZone_Apply(zone, &at, 0, 0, PKT_NAME_OBJ_DEL, &p);
    }
    n = rows(zone, r);
    CHECK(n == 2, "one row removed");
    CHECK(r[0].obj_id == 526 && r[1].obj_id == 995 && r[1].count == 20, "the oldest 995 went");
    CHECK(s_entries[s_ring.newest_serial - 1].kind == RS_EVENT_OBJ_REMOVED, "obj_removed stamped");

    /* OBJ_COUNT: the first row of the id. */
    {
        struct PktObjCount p = { .pos = pos(1, 1), .obj_id = 995, .old_count = 20, .new_count = 25 };
        ScriptrunZone_Apply(zone, &at, 0, 0, PKT_NAME_OBJ_COUNT, &p);
    }
    n = rows(zone, r);
    CHECK(r[1].count == 25, "OBJ_COUNT sets the count");

    /* OBJ_REVEAL: for its receiver only. */
    {
        struct PktObjReveal p = { .pos = pos(2, 2), .obj_id = 314, .count = 3, .receiver = 8 };
        ScriptrunZone_Apply(zone, &at, 0, 0, PKT_NAME_OBJ_REVEAL, &p);
        CHECK(rows(zone, r) == 2, "a reveal for another player adds nothing");
        p.receiver = 7;
        ScriptrunZone_Apply(zone, &at, 0, 0, PKT_NAME_OBJ_REVEAL, &p);
        CHECK(rows(zone, r) == 3, "a reveal for this player adds the row");
    }
    /* OBJ_ENABLED_OPS (the reveal's other form) sets the first row's ops. */
    {
        struct PktObjReveal p = {
            .pos = pos(1, 1), .obj_id = 995, .has_enabled_ops = 1, .enabled_ops = 0x3
        };
        struct World_EntityPool* pool = &zone->world->entities.obj_stack;
        int idx;

        ScriptrunZone_Apply(zone, &at, 0, 0, PKT_NAME_OBJ_REVEAL, &p);
        idx = World_ObjStackFind(zone->world, 9, 17, 0, 995);
        CHECK(idx >= 0, "the 995 row");
        CHECK(((struct WorldEntity_ObjStack*)World_EntityPoolGet(pool, idx))->enabled_ops == 0x3,
            "enabled ops set");
        CHECK(rows(zone, r) == 3, "enabled ops adds no row");
    }

    /* UPDATE_ZONE_FULL_FOLLOWS clears the zone's rows, not its neighbours'. */
    {
        struct ScriptrunZoneAt other = at_root(16, 16, 0);
        obj_add(zone, &other, 0, 0, 1511, 1);
        ScriptrunZone_Apply(zone, &at, 0, 0, PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, NULL);
        n = rows(zone, r);
        CHECK(n == 1 && r[0].obj_id == 1511, "the zone's rows went, the neighbour's stayed");
    }

    /* A rebuild shifts the rows with the scene and drops those it leaves. */
    {
        struct ScriptrunZoneAt far = at_root(96, 96, 0);
        obj_add(zone, &far, 7, 7, 1925, 1); /* scene 103,103 */
        ScriptrunZone_Rebuild(zone, 3200 + 48, 3200 + 48, 104);
        n = rows(zone, r);
        CHECK(n == 1, "the row the new scene does not hold went");
        CHECK(r[0].obj_id == 1925 && r[0].x == 3200 + 103 && r[0].z == 3200 + 103,
            "the kept row keeps its absolute tile");
    }
    ScriptrunZone_Free(zone);
}

static void
test_effects(void)
{
    struct ScriptrunZone* zone = zone_new();
    struct ScriptrunZoneAt at = at_root(8, 8, 1);
    struct RS_EffectProjectile shots[16];
    struct RS_EffectMapAnim anims[16];

    /* MAP_ANIM: logged in the view's absolute frame; off its scene: dropped;
     * 65535: the clear sentinel. */
    {
        struct PktMapAnim p = { .pos = pos(3, 4), .id = 1234 };
        struct ScriptrunZoneAt edge = at_root(104, 0, 0);

        ScriptrunZone_Apply(zone, &at, 0, 30, PKT_NAME_MAP_ANIM, &p);
        ScriptrunZone_Apply(zone, &edge, 0, 30, PKT_NAME_MAP_ANIM, &p);
        p.id = 65535;
        ScriptrunZone_Apply(zone, &at, 0, 30, PKT_NAME_MAP_ANIM, &p);
    }
    CHECK(RS_EffectLog_MapAnims(&zone->effects, zone->effect_entries, anims, 16) == 1,
        "one map graphic logged");
    CHECK(anims[0].x == 3211 && anims[0].z == 3212 && anims[0].level == 1 && anims[0].spotanim == 1234 &&
              anims[0].cycle == 30,
        "its tile, plane, id and cycle");

    /* MAP_PROJANIM: offsets, or the rev-239 absolute landing with its plane. */
    {
        struct PktMapProjAnim p = {
            .pos = pos(0, 0), .dx_offset = 2, .dz_offset = -1, .spotanim = 99, .target = 5,
            .start_delay = 10, .end_delay = 40
        };
        ScriptrunZone_Apply(zone, &at, 0, 30, PKT_NAME_MAP_PROJANIM, &p);
        p.dst_abs = 1;
        p.dst_abs_x = 3000;
        p.dst_abs_z = 3001;
        p.dst_abs_level = 2;
        ScriptrunZone_Apply(zone, &at, 0, 30, PKT_NAME_MAP_PROJANIM, &p);
    }
    CHECK(RS_EffectLog_Projectiles(&zone->effects, zone->effect_entries, shots, 16) == 2,
        "two projectiles");
    CHECK(shots[0].src_x == 3208 && shots[0].dst_x == 3210 && shots[0].dst_z == 3207 &&
              shots[0].level == 1 && shots[0].end_delay == 40,
        "the offset landing, the zone's plane");
    CHECK(shots[1].dst_x == 3000 && shots[1].dst_z == 3001 && shots[1].level == 2,
        "the absolute landing and its plane");

    /* SCRIPTEDPROJ_ADD: a slot in flight keeps its projectile. */
    {
        struct PktScriptedProjAdd p = { .pos = pos(1, 1), .slot = 4, .spotanim = 77, .end_delay = 60 };
        ScriptrunZone_Apply(zone, &at, 0, 30, PKT_NAME_SCRIPTEDPROJ_ADD, &p);
        ScriptrunZone_Apply(zone, &at, 0, 30, PKT_NAME_SCRIPTEDPROJ_ADD, &p);
        CHECK(RS_EffectLog_Projectiles(&zone->effects, zone->effect_entries, shots, 16) == 3,
            "the second ADD for a slot in flight did nothing");
        ScriptrunZone_BeginTick(zone, 4, 120);
        ScriptrunZone_Apply(zone, &at, 4, 120, PKT_NAME_SCRIPTEDPROJ_ADD, &p);
        CHECK(RS_EffectLog_Projectiles(&zone->effects, zone->effect_entries, shots, 16) == 4,
            "once its flight is over the slot takes a new one");
    }

    /* The alive rule both clients read the log by. */
    {
        struct RS_Event const* first =
            RS_EventRing_At(&zone->effects, zone->effect_entries, RS_EFFECT_LOG_CAP, zone->effects.oldest_serial);
        CHECK(first->kind == RS_EVENT_MAP_ANIM, "the oldest entry is the map graphic");
        CHECK(RS_EffectLog_Alive(first, NULL, 30 + RS_EFFECT_MAP_ANIM_LIFE_CYCLES, 0),
            "alive at its last cycle");
        CHECK(!RS_EffectLog_Alive(first, NULL, 31 + RS_EFFECT_MAP_ANIM_LIFE_CYCLES, 0),
            "gone a cycle after");
        CHECK(RS_EffectLog_Alive(first, NULL, 31 + RS_EFFECT_MAP_ANIM_LIFE_CYCLES, 60),
            "the real client's slack keeps it");
        CHECK(RS_EffectLog_Live(&zone->effects, zone->effect_entries, RS_EVENT_PROJECTILE_FLIGHT, 70, 0) ==
                  4,
            "at cycle 70 the 40-cycle projectiles from 30 are on their last cycle");
        CHECK(RS_EffectLog_Live(&zone->effects, zone->effect_entries, RS_EVENT_PROJECTILE_FLIGHT, 71, 0) ==
                  2,
            "a cycle later only the 60-cycle ones are");
    }

    /* LOC_ANIM: the root's only, at the loc's cache level, kept its life. */
    {
        struct PktLocAnim p = { .pos = pos(2, 3), .info = (10 << 2) | 1, .seq_id = 555 };
        struct ScriptrunZoneAt deck = at;

        at.loc_climb[2][3] = 1;
        ScriptrunZone_Apply(zone, &at, 5, 150, PKT_NAME_LOC_ANIM, &p);
        deck.view = 3;
        ScriptrunZone_Apply(zone, &deck, 5, 150, PKT_NAME_LOC_ANIM, &p);
        CHECK(zone->loc_anim_count == 1, "a deck's loc animation is not listed");
        CHECK(zone->loc_anims[0].x == 3210 && zone->loc_anims[0].z == 3211 &&
                  zone->loc_anims[0].level == 2 && zone->loc_anims[0].shape == 10 &&
                  zone->loc_anims[0].seq == 555 && zone->loc_anims[0].tick == 5,
            "its tile, the bridge's cache level, shape, seq and tick");
        ScriptrunZone_BeginTick(zone, 5 + RS_EFFECT_LOC_ANIM_LIFE_TICKS, 0);
        CHECK(zone->loc_anim_count == 1, "kept through its life");
        ScriptrunZone_BeginTick(zone, 6 + RS_EFFECT_LOC_ANIM_LIFE_TICKS, 0);
        CHECK(zone->loc_anim_count == 0, "then gone");
    }
    ScriptrunZone_Free(zone);
}

static void
test_loc_records(void)
{
    struct ScriptrunZone* zone = zone_new();
    uint8_t climb[8][8];
    struct World_LocChange const* rec;

    memset(climb, 0, sizeof(climb));
    /* A change keeps the map's loc as its old one, and a later change to the
     * key only moves its new one. */
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };

    ScriptrunZone_LocChange(zone, 10, 20, 0, 0, 1, 1530, 1529, 0, 1, 30, 0x1f, none);
    ScriptrunZone_LocChange(zone, 10, 20, 0, 0, 2, 1531, 1530, 0, 1, 60, 0x1f, none);
    CHECK(zone->world->loc_change_count == 1, "one record per key");
    rec = &zone->world->loc_changes[0];
    CHECK(rec->old_type == 1529 && rec->new_type == 1531 && rec->new_angle == 2, "old kept, new moved");
    /* A rebuild keeps the record and moves it with the scene. */
    ScriptrunZone_Rebuild(zone, 3208, 3200, 104);
    CHECK(zone->world->loc_change_count == 1 && rec->x == 2, "the record moved with the scene");
    /* A full zone resend: the map's loc stands again. */
    ScriptrunZone_LocZoneReset(zone, 0, 16, 0, climb);
    CHECK(rec->new_type == 1529 && rec->new_angle == 1, "the key stands as the map's");
    ScriptrunZone_Free(zone);
}

int
main(void)
{
    test_ground();
    test_effects();
    test_loc_records();
    printf("scriptrun_zone_test: all passed\n");
    return 0;
}
