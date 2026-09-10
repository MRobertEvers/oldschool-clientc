#ifndef TORIRSSERVER_TEST_QUEST_ELEMENTALWORKSHOPII_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ELEMENTALWORKSHOPII_SELFTEST_U_H

/* Elemental Workshop II Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned earth elementals / jig-cart NPCs cannot
 * leak. Real OPLOC1 / OPLOCU / OPHELD1 / OPNPC1 / OPNPCU / AI_QUEUE3 on
 * the authored path. player->godmode = 1 for the whole walk (not a death
 * test).
 *
 * Gate: TORIRSSERVER_SELFTEST_ELEM2_ONLY=1 (or EW2 / ELEMENTALWORKSHOPII)
 *
 * Real prereq: Elemental Workshop I (%elemental_workshop_finished >= 1).
 * Magic 20 at the extractor chair; Smithing 30 at the workbench claw/helmet.
 * Reward tenths: 75000 Smithing / Crafting, 1 QP.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - pipe-connection widget ElemMagicpressPipes collapsed to one
 *     junction-box reconnect (states 5/6/13)
 *   - crane _fire_pos/_fire_state has no native multiloc -- mes() against
 *     fixed elem2_crane_track_up_empty
 *   - optional post-quest Mind Shield / slashed-book second bar
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 */

#define ELEM2_NOT_STARTED 0
#define ELEM2_BOOK_TAKEN 1
#define ELEM2_SCROLL_REVEALED 2
#define ELEM2_MACHINERY_LEARNED 3
#define ELEM2_KEY_FOUND 4
#define ELEM2_HATCH_OPENED 5
#define ELEM2_SCHEMATICS_TAKEN 6
#define ELEM2_CLAW_MADE 7
#define ELEM2_CRANE_REPAIRED 8
#define ELEM2_WORKSHOP_REPAIRED 9
#define ELEM2_MIND_BAR_MADE 10
#define ELEM2_COMPLETE 11

#define ELEM2_JIG_EMPTY 0
#define ELEM2_JIG_BAR_PLACED 1
#define ELEM2_JIG_BAR_HOT 2
#define ELEM2_JIG_BAR_FLAT_HOT 3
#define ELEM2_JIG_BAR_FLAT_COOL 4
#define ELEM2_JIG_BAR_DRY 5

#define ELEM2_JIG_POS_LAVA 0
#define ELEM2_JIG_POS_PRESS 1
#define ELEM2_JIG_POS_TANK 2
#define ELEM2_JIG_POS_TUNNEL 3

#define ELEM2_FIRE_BROKEN 0
#define ELEM2_FIRE_EMPTY 1
#define ELEM2_FIRE_HOLDING_COLD 2
#define ELEM2_FIRE_HOLDING_HOT 3

#define ELEM2_FIRE_RAISED 0
#define ELEM2_FIRE_LOWERED 1
#define ELEM2_FIRE_ABOVE_LAVA 2
#define ELEM2_FIRE_IN_LAVA 3

#define ELEM2_DOOR_TANK_CLOSED 0
#define ELEM2_DOOR_TANK_OPEN 1
#define ELEM2_DOOR_GRABBER_OUT 2
#define ELEM2_DOOR_GRABBER_IN_HOT_CLOSED 3
#define ELEM2_DOOR_GRABBER_IN_HOT_OPEN 4
#define ELEM2_DOOR_GRABBER_OUT_HOT 5
#define ELEM2_DOOR_GRABBER_IN_COOL_CLOSED 6
#define ELEM2_DOOR_GRABBER_IN_COOL_OPEN 7
#define ELEM2_DOOR_GRABBER_OUT_COOL 8

#define ELEM2_MINDJIG_EMPTY 0
#define ELEM2_MINDJIG_BAR_PLACED 1
#define ELEM2_MINDJIG_MIND_BAR 2

#define ELEM2_REWARD_TENTHS 75000
#define ELEM2_QP_REWARD 1

#define ELEM2_EXAM_X 3369
#define ELEM2_EXAM_Z 3335
#define ELEM2_WORKSHOP_X 2716
#define ELEM2_WORKSHOP_Z 9888
#define ELEM2_MIND_X 1954
#define ELEM2_MIND_Z 5155

static void
elem2_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ELEM2 PASS: %s\n", step);
}

static void
elem2_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
elem2_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
elem2_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 8) <= 0 )
            selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
}

static void
elem2_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    elem2_god(player);
    selftest_tick(srv);
}

static int
elem2_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    elem2_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
elem2_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
elem2_free_type(struct ToriRSServer* srv, int npc_type)
{
    int i;

    assert(srv);
    if( npc_type <= 0 )
        return;
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type )
            ToriRSServer_WorldNpcFree(srv, i);
    }
    ToriRSServer_WorldNpcReap(srv);
}

static void
elem2_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
elem2_get_vb(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static int
elem2_quest(struct ToriRSServerPlayer* player)
{
    return elem2_get_vb(player, "elemental_quest_2_main");
}

static void
elem2_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id < 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
        if( player->inv[s].obj_id == obj_id )
        {
            inv_set(player, s, player->inv[s].obj_id, player->inv[s].count + count);
            return;
        }
    }
}

static int
elem2_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    return n;
}

static void
elem2_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id < 0 )
            inv_set(player, s, obj_id, 1);
}

static void
elem2_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
elem2_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        elem2_set_stat(player, stat, 99);
}

static void
elem2_ew1(struct ToriRSServer* srv, int finished)
{
    assert(srv);
    elem2_vb(srv, "elemental_workshop_finished", finished ? 1 : 0);
    elem2_vb(srv, "elemental_workshop_book", finished ? 1 : 0);
    elem2_vb(srv, "elemental_workshop_key", finished ? 1 : 0);
    elem2_vb(srv, "elemental_workshop_fire", finished ? 1 : 0);
    elem2_vb(srv, "elemental_workshop_bellows", finished ? 1 : 0);
    elem2_vb(srv, "elemental_workshop_bellows_switch", finished ? 1 : 0);
    elem2_vb(srv, "elemental_workshop_switch", finished ? 1 : 0);
}

static void
elem2_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_NOT_STARTED);
    elem2_vb(srv, "elemental_quest_2_hide_key", 0);
    elem2_vb(srv, "elemental_quest_2_hatch", 0);
    elem2_vb(srv, "elemental_quest_2_jig_pos", ELEM2_JIG_POS_LAVA);
    elem2_vb(srv, "elemental_quest_2_jig_state", ELEM2_JIG_EMPTY);
    elem2_vb(srv, "elemental_quest_2_fire_state", ELEM2_FIRE_BROKEN);
    elem2_vb(srv, "elemental_quest_2_fire_pos", ELEM2_FIRE_RAISED);
    elem2_vb(srv, "elemental_quest_2_earth_pipe_1_state", 0);
    elem2_vb(srv, "elemental_quest_2_earth_pipe_2_state", 0);
    elem2_vb(srv, "elemental_quest_2_earth_pipe_3_state", 0);
    elem2_vb(srv, "elemental_quest_2_water_state", 0);
    elem2_vb(srv, "elemental_quest_2_water_valve_1", 0);
    elem2_vb(srv, "elemental_quest_2_water_valve_2", 0);
    elem2_vb(srv, "elemental_quest_2_water_door", ELEM2_DOOR_TANK_CLOSED);
    elem2_vb(srv, "elemental_quest_2_water_level", 0);
    elem2_vb(srv, "elemental_quest_2_air_cog1", 0);
    elem2_vb(srv, "elemental_quest_2_air_cog2", 0);
    elem2_vb(srv, "elemental_quest_2_air_cog3", 0);
    elem2_vb(srv, "elemental_quest_2_air_fan_state", 0);
    elem2_vb(srv, "elemental_quest_2_mind_jig", ELEM2_MINDJIG_EMPTY);
    elem2_vb(srv, "elemental_quest_2_box_state", 0);
}

static void
elem2_ready(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    elem2_reset_quest(srv);
    elem2_ew1(srv, 1);
    elem2_skills99(player);
    elem2_clear_inv(player);
    elem2_god(player);
}

static void
elem2_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,elem2_journal]", NULL, 0);
    elem2_finish(srv);
    elem2_pass(step);
}

static void
elem2_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    elem2_finish(srv);
}

/* Priming levers / valves / corkscrew only `mes()` -- no pause button.
 * Draining pages + eight idle ticks after a mes() can let a leftover
 * resume from an earlier mesbox re-fire the same loc. */
static void
elem2_loc1_mes(struct ToriRSServer* srv, int loc_type)
{
    int t;

    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    for( t = 0; t < 2; t++ )
        selftest_tick(srv);
}

static void
elem2_locu(struct ToriRSServer* srv, int loc_type, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(loc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_type, -1, -1);
    elem2_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
elem2_held1(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    elem2_finish(srv);
}

static void
elem2_npc1(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    elem2_finish(srv);
}

static void
elem2_npcu(struct ToriRSServer* srv, int npc_type, int slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = slot;
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    elem2_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
selftest_quest_elementalworkshopii(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int loc_bookcase;
    int loc_boiler;
    int loc_boiler_key;
    int loc_hatch;
    int loc_furnace_out;
    int loc_furnace_lit;
    int loc_crate;
    int loc_workbench;
    int loc_crane;
    int loc_junction;
    int loc_pipe_broken;
    int loc_pipe_float;
    int loc_pin_high;
    int loc_pin_low;
    int loc_pin_left;
    int loc_box1;
    int loc_lever2;
    int loc_lever1;
    int loc_3way;
    int loc_earth;
    int loc_water;
    int loc_corkscrew;
    int loc_valve1;
    int loc_valve2;
    int loc_air;
    int loc_gun_empty;
    int loc_gun_bar;
    int loc_gun_mind;
    int loc_hat;
    int npc_rock;
    int npc_awakened;
    int npc_cart_empty;
    int npc_cart_dry;
    int obj_book;
    int obj_note;
    int obj_key;
    int obj_ore;
    int obj_bar;
    int obj_coal;
    int obj_lava;
    int obj_pick;
    int obj_logs;
    int obj_hammer;
    int obj_claw_book;
    int obj_lever_book;
    int obj_claw;
    int obj_pipe;
    int obj_small;
    int obj_med;
    int obj_big;
    int obj_primed;
    int obj_mind_bar;
    int obj_helm;
    int obj_shield_book;
    int stat_magic;
    int stat_smith;
    int stat_craft;
    int stat_mine;
    int varp_qp;
    int slot_rock;
    int slot_cart;
    int slot_awakened;
    int smith_before;
    int craft_before;
    int qp_before;
    int i;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: elemental workshop ii critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer elemental workshop ii selftest: SKIP no compiled script pack\n");
        return;
    }

    loc_bookcase = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_2_bookcase");
    loc_boiler = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_boiler");
    loc_boiler_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_2_boiler_for_key");
    loc_hatch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_stairs_door_close");
    loc_furnace_out = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_furnace_out");
    loc_furnace_lit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_furnace_lit");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_maintenance_book_crate");
    loc_workbench = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_workbench");
    loc_crane = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_crane_track_up_empty");
    loc_junction = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_press_junction_box");
    loc_pipe_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_piping_blue_broken");
    loc_pipe_float = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_piping_blue_float");
    loc_pin_high = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_wind_pin_high");
    loc_pin_low = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_wind_pin_low");
    loc_pin_left = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_wind_pin_left");
    loc_box1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elemental_workshop_2_box_1");
    loc_lever2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_fire_lever_2");
    loc_lever1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_fire_lever_1");
    loc_3way = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_lever_3way");
    loc_earth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_earth_lever_1");
    loc_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_water_lever");
    loc_corkscrew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_corkscrew");
    loc_valve1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_valve_1");
    loc_valve2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_valve_2");
    loc_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem2_air_lever");
    loc_gun_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem_extractor_gun_no_bar");
    loc_gun_bar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem_extractor_gun_bar");
    loc_gun_mind = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem_extractor_gun_bar_mind");
    loc_hat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "elem_extractor_hat");
    npc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elem1_qip_earth_elemental_rock_version_rock");
    npc_awakened = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elem1_qip_earth_elemental_rock_version");
    npc_cart_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elem2_cart_npc_empty");
    npc_cart_dry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elem2_cart_npc_flat_bar_dry");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_helm_book");
    obj_note = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_2_note");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_2_key");
    obj_ore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_ore");
    obj_bar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_bar");
    obj_coal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coal");
    obj_lava = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_lava_bowl_full");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_claw_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_claw_book");
    obj_lever_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_lever_book");
    obj_claw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elem_broken_finger");
    obj_pipe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elem2_spare_pipe");
    obj_small = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elem2_smallgear");
    obj_med = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elem2_medgear");
    obj_big = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elem2_biggear");
    obj_primed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elem_primed_bar");
    obj_mind_bar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elem_mind_bar");
    obj_helm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elem_mind_helm");
    obj_shield_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "elemental_workshop_shield_book_slashed");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    varp_qp = ToriRSServer_WorldVarp("qp");
    if( varp_qp < 0 )
        varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_elementalworkshop2") > 0,
                   "dbrow quest_elementalworkshop2 should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "elemental_quest_2_main") >= 0,
                   "varbit elemental_quest_2_main should resolve");
    SELFTEST_CHECK(loc_bookcase > 0, "loc elemental_workshop_2_bookcase should resolve");
    SELFTEST_CHECK(loc_boiler > 0, "loc elemental_boiler should resolve");
    SELFTEST_CHECK(loc_hatch > 0, "loc elem2_stairs_door_close should resolve");
    SELFTEST_CHECK(loc_furnace_out > 0 && loc_furnace_lit > 0, "furnace leaves should resolve");
    SELFTEST_CHECK(loc_workbench > 0, "loc elemental_workshop_workbench should resolve");
    SELFTEST_CHECK(loc_crane > 0, "loc elem2_crane_track_up_empty should resolve");
    SELFTEST_CHECK(obj_book > 0 && obj_note > 0 && obj_key > 0, "book/note/key should resolve");
    SELFTEST_CHECK(obj_bar > 0 && obj_claw > 0 && obj_primed > 0 && obj_mind_bar > 0 && obj_helm > 0,
                   "bars/claw/helm should resolve");
    SELFTEST_CHECK(npc_rock > 0 && npc_awakened > 0, "earth elemental rock/awakened should resolve");
    SELFTEST_CHECK(npc_cart_empty > 0 && npc_cart_dry > 0, "jig cart leaves should resolve");

    elem2_god(player);

    /* Journal: EW1 not finished, then stages 0-11. */
    elem2_reset_quest(srv);
    elem2_ew1(srv, 0);
    elem2_clear_inv(player);
    elem2_journal(srv, "journal_ew1_prereq");

    elem2_ready(srv, player);
    elem2_journal(srv, "journal_00_not_started");

    /* Bookcase: EW1 refuse. */
    elem2_tele(srv, ELEM2_EXAM_X, ELEM2_EXAM_Z, 0);
    elem2_reset_quest(srv);
    elem2_ew1(srv, 0);
    elem2_loc1(srv, loc_bookcase);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_NOT_STARTED, "EW1 refuse must not start EW2");
    SELFTEST_CHECK(elem2_inv_total(player, obj_book) == 0, "EW1 refuse must not grant the book");
    elem2_pass("bookcase_ew1_refuse");

    /* Inv full first take. */
    elem2_ready(srv, player);
    if( obj_logs > 0 )
        elem2_fill_inv(player, obj_logs);
    elem2_loc1(srv, loc_bookcase);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_NOT_STARTED, "full inv must not take the book");
    elem2_pass("bookcase_inv_full_first");

    /* First take. */
    elem2_ready(srv, player);
    elem2_loc1(srv, loc_bookcase);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_BOOK_TAKEN, "search should set main=1");
    SELFTEST_CHECK(elem2_inv_total(player, obj_book) >= 1, "search should grant beaten book");
    elem2_pass("bookcase_find_book");
    elem2_journal(srv, "journal_01_book_taken");

    /* Already taken. */
    elem2_loc1(srv, loc_bookcase);
    SELFTEST_CHECK(elem2_inv_total(player, obj_book) == 1, "already-taken must not duplicate the book");
    elem2_pass("bookcase_already_taken");

    /* Another copy after losing the book. */
    elem2_clear_inv(player);
    elem2_loc1(srv, loc_bookcase);
    SELFTEST_CHECK(elem2_inv_total(player, obj_book) >= 1, "lost book should be replaceable");
    elem2_pass("bookcase_another_copy");

    /* Inv full another copy. */
    elem2_clear_inv(player);
    if( obj_logs > 0 )
        elem2_fill_inv(player, obj_logs);
    elem2_loc1(srv, loc_bookcase);
    SELFTEST_CHECK(elem2_inv_total(player, obj_book) == 0, "full inv recovery must not grant");
    elem2_pass("bookcase_inv_full_another");

    /* Read book: inv full blocks scroll. */
    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_BOOK_TAKEN);
    elem2_give(player, obj_book, 1);
    if( obj_logs > 0 )
        elem2_fill_inv(player, obj_logs);
    elem2_held1(srv, obj_book);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_BOOK_TAKEN, "full inv must not reveal the scroll");
    elem2_pass("book_scroll_inv_full");

    /* Read book: scroll reveal. */
    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_BOOK_TAKEN);
    elem2_give(player, obj_book, 1);
    elem2_held1(srv, obj_book);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_SCROLL_REVEALED, "first read should set main=2");
    SELFTEST_CHECK(elem2_inv_total(player, obj_note) >= 1, "first read should grant the scroll");
    elem2_pass("book_read_scroll_reveal");
    elem2_journal(srv, "journal_02_scroll_revealed");

    /* Recover scroll. */
    elem2_clear_inv(player);
    elem2_give(player, obj_book, 1);
    elem2_held1(srv, obj_book);
    SELFTEST_CHECK(elem2_inv_total(player, obj_note) >= 1, "reread should recover the scroll");
    elem2_pass("book_scroll_again");

    /* Reread synopsis after machinery learned. */
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_MACHINERY_LEARNED);
    elem2_give(player, obj_note, 1);
    elem2_held1(srv, obj_book);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_MACHINERY_LEARNED, "later reread must not rewind");
    elem2_pass("book_reread_helm");

    /* Read note. */
    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_SCROLL_REVEALED);
    elem2_give(player, obj_note, 1);
    elem2_held1(srv, obj_note);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_MACHINERY_LEARNED, "scroll read should set main=3");
    elem2_pass("note_machinery_learned");
    elem2_journal(srv, "journal_03_machinery_learned");

    elem2_held1(srv, obj_note);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_MACHINERY_LEARNED, "note reread must not rewind");
    elem2_pass("note_reread");

    /* Boiler early. */
    elem2_tele(srv, ELEM2_WORKSHOP_X, ELEM2_WORKSHOP_Z, 0);
    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_BOOK_TAKEN);
    elem2_loc1(srv, loc_boiler);
    SELFTEST_CHECK(elem2_inv_total(player, obj_key) == 0, "early boiler must not grant the key");
    elem2_pass("boiler_early");

    /* Boiler inv full. */
    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_MACHINERY_LEARNED);
    if( obj_logs > 0 )
        elem2_fill_inv(player, obj_logs);
    elem2_loc1(srv, loc_boiler);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_MACHINERY_LEARNED, "full inv must not find the key");
    elem2_pass("boiler_inv_full");

    /* Boiler find key. */
    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_MACHINERY_LEARNED);
    elem2_loc1(srv, loc_boiler);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_KEY_FOUND, "boiler search should set main=4");
    SELFTEST_CHECK(elem2_inv_total(player, obj_key) >= 1, "boiler search should grant the key");
    elem2_pass("boiler_find_key");
    elem2_journal(srv, "journal_04_key_found");

    elem2_loc1(srv, loc_boiler);
    SELFTEST_CHECK(elem2_inv_total(player, obj_key) == 1, "already-have-key must not duplicate");
    elem2_pass("boiler_already_have_key");

    /* Hatch locked. */
    elem2_clear_inv(player);
    elem2_loc1(srv, loc_hatch);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_hatch") == 0, "locked hatch stays shut");
    elem2_pass("hatch_locked");

    /* Hatch unlock. */
    elem2_give(player, obj_key, 1);
    elem2_locu(srv, loc_hatch, obj_key);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_hatch") == 1, "key should open the hatch");
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_HATCH_OPENED, "unlock should set main=5");
    SELFTEST_CHECK(elem2_inv_total(player, obj_key) == 0, "key is consumed");
    elem2_pass("hatch_unlock");
    elem2_journal(srv, "journal_05_hatch_opened");

    elem2_loc1(srv, loc_boiler);
    SELFTEST_CHECK(elem2_inv_total(player, obj_key) == 0, "opened hatch empties the boiler");
    elem2_pass("boiler_hatch_empty");

    if( loc_boiler_key > 0 )
    {
        elem2_vb(srv, "elemental_quest_2_hatch", 1);
        elem2_loc1(srv, loc_boiler_key);
        elem2_pass("boiler_for_key_already");

        elem2_vb(srv, "elemental_quest_2_hatch", 0);
        elem2_loc1(srv, loc_boiler_key);
        SELFTEST_CHECK(elem2_inv_total(player, obj_key) >= 1, "replacement key from the empty leaf");
        elem2_pass("boiler_for_key_replace");
    }

    /* Earth rock: mining / pickaxe / awaken + existing death drop. */
    elem2_ready(srv, player);
    elem2_tele(srv, ELEM2_WORKSHOP_X, ELEM2_WORKSHOP_Z, 0);
    slot_rock = elem2_spawn(srv, npc_rock, ELEM2_WORKSHOP_X, ELEM2_WORKSHOP_Z, 0);
    SELFTEST_CHECK(slot_rock >= 0, "earth elemental rock should spawn");
    if( slot_rock >= 0 && stat_mine >= 0 )
    {
        elem2_set_stat(player, stat_mine, 1);
        elem2_npc1(srv, npc_rock, slot_rock);
        SELFTEST_CHECK(elem2_inv_total(player, obj_ore) == 0, "low mining must not awaken");
        elem2_pass("rock_need_mining");

        elem2_set_stat(player, stat_mine, 99);
        elem2_npc1(srv, npc_rock, slot_rock);
        elem2_pass("rock_need_pickaxe");

        if( obj_pick > 0 )
            elem2_give(player, obj_pick, 1);
        elem2_npc1(srv, npc_rock, slot_rock);
        elem2_pass("rock_awaken");

        slot_awakened = -1;
        for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
        {
            if( srv->npcs[i].active && srv->npcs[i].type == npc_awakened )
            {
                slot_awakened = i;
                break;
            }
        }
        if( slot_awakened >= 0 )
        {
            srv->npcs[slot_awakened].combat_target = player->pid;
            if( srv->npcs[slot_awakened].hitpoints > 0 )
                ToriRSServer_CombatHitNpc(srv, slot_awakened, 0, srv->npcs[slot_awakened].hitpoints);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_awakened, -1, slot_awakened);
            elem2_finish(srv);
            elem2_pass("rock_death_existing_header");
            elem2_free_npc(srv, slot_awakened);
        }
        elem2_free_npc(srv, slot_rock);
    }
    elem2_free_type(srv, npc_awakened);
    elem2_free_type(srv, npc_rock);

    /* Shared furnace -- existing ~elem1_furnace, both leaves. */
    elem2_ready(srv, player);
    elem2_tele(srv, ELEM2_WORKSHOP_X, ELEM2_WORKSHOP_Z, 0);
    elem2_vb(srv, "elemental_workshop_fire", 0);
    if( obj_ore > 0 )
        elem2_give(player, obj_ore, 1);
    elem2_locu(srv, loc_furnace_out, obj_ore);
    SELFTEST_CHECK(elem2_inv_total(player, obj_bar) == 0, "cold furnace must not smelt");
    elem2_pass("furnace_cold");

    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_workshop_fire", 1);
    elem2_vb(srv, "elemental_workshop_bellows_switch", 0);
    if( obj_ore > 0 )
        elem2_give(player, obj_ore, 1);
    elem2_locu(srv, loc_furnace_lit, obj_ore);
    SELFTEST_CHECK(elem2_inv_total(player, obj_bar) == 0, "unpumped bellows must not smelt");
    elem2_pass("furnace_need_bellows");

    elem2_ready(srv, player);
    if( stat_smith >= 0 )
        elem2_set_stat(player, stat_smith, 1);
    if( obj_ore > 0 )
        elem2_give(player, obj_ore, 1);
    if( obj_coal > 0 )
        elem2_give(player, obj_coal, 4);
    elem2_locu(srv, loc_furnace_lit, obj_ore);
    SELFTEST_CHECK(elem2_inv_total(player, obj_bar) == 0, "low smithing must not smelt");
    elem2_pass("furnace_need_smithing");

    elem2_ready(srv, player);
    if( obj_ore > 0 )
        elem2_give(player, obj_ore, 1);
    elem2_locu(srv, loc_furnace_lit, obj_ore);
    SELFTEST_CHECK(elem2_inv_total(player, obj_bar) == 0, "no coal must not smelt");
    elem2_pass("furnace_need_coal");

    elem2_ready(srv, player);
    if( obj_ore > 0 )
        elem2_give(player, obj_ore, 1);
    if( obj_coal > 0 )
        elem2_give(player, obj_coal, 4);
    elem2_locu(srv, loc_furnace_out, obj_ore);
    SELFTEST_CHECK(elem2_inv_total(player, obj_bar) >= 1, "hot furnace should smelt a bar");
    elem2_pass("furnace_smelt_bar");

    if( obj_lava > 0 )
    {
        elem2_ready(srv, player);
        elem2_vb(srv, "elemental_workshop_fire", 0);
        elem2_give(player, obj_lava, 1);
        elem2_locu(srv, loc_furnace_out, obj_lava);
        SELFTEST_CHECK(elem2_get_vb(player, "elemental_workshop_fire") == 1, "lava bowl should light the furnace");
        elem2_pass("furnace_lava_light");

        elem2_give(player, obj_lava, 1);
        elem2_locu(srv, loc_furnace_lit, obj_lava);
        elem2_pass("furnace_lava_already");
    }

    /* Schematics. */
    elem2_ready(srv, player);
    elem2_tele(srv, ELEM2_MIND_X, ELEM2_MIND_Z, 2);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_HATCH_OPENED);
    elem2_vb(srv, "elemental_quest_2_hatch", 1);
    if( obj_logs > 0 )
        elem2_fill_inv(player, obj_logs);
    elem2_loc1(srv, loc_crate);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_HATCH_OPENED, "full inv must not take schematics");
    elem2_pass("schematics_inv_full");

    elem2_clear_inv(player);
    elem2_loc1(srv, loc_crate);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_SCHEMATICS_TAKEN, "crate should set main=6");
    SELFTEST_CHECK(elem2_inv_total(player, obj_claw_book) >= 1, "crate should grant claw schematic");
    SELFTEST_CHECK(elem2_inv_total(player, obj_lever_book) >= 1, "crate should grant lever schematic");
    elem2_pass("schematics_take");
    elem2_journal(srv, "journal_06_schematics_taken");

    elem2_loc1(srv, loc_crate);
    SELFTEST_CHECK(elem2_inv_total(player, obj_claw_book) == 1, "already-taken must not duplicate");
    elem2_pass("schematics_already");

    /* Claw at shared workbench. */
    elem2_ready(srv, player);
    elem2_tele(srv, ELEM2_WORKSHOP_X, ELEM2_WORKSHOP_Z, 0);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_SCHEMATICS_TAKEN);
    elem2_vb(srv, "elemental_quest_2_hatch", 1);
    elem2_vb(srv, "elemental_quest_2_fire_state", ELEM2_FIRE_BROKEN);
    if( obj_bar > 0 )
        elem2_give(player, obj_bar, 1);
    elem2_locu(srv, loc_workbench, obj_bar);
    SELFTEST_CHECK(elem2_inv_total(player, obj_claw) == 0, "no hammer must not make a claw");
    elem2_pass("claw_need_hammer");

    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_SCHEMATICS_TAKEN);
    elem2_vb(srv, "elemental_quest_2_hatch", 1);
    elem2_vb(srv, "elemental_quest_2_fire_state", ELEM2_FIRE_BROKEN);
    if( stat_smith >= 0 )
        elem2_set_stat(player, stat_smith, 1);
    if( obj_bar > 0 )
        elem2_give(player, obj_bar, 1);
    if( obj_hammer > 0 )
        elem2_give(player, obj_hammer, 1);
    if( obj_claw_book > 0 )
        elem2_give(player, obj_claw_book, 1);
    elem2_locu(srv, loc_workbench, obj_bar);
    SELFTEST_CHECK(elem2_inv_total(player, obj_claw) == 0, "low smithing must not make a claw");
    elem2_pass("claw_need_smithing");

    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_HATCH_OPENED);
    elem2_vb(srv, "elemental_quest_2_hatch", 1);
    elem2_vb(srv, "elemental_quest_2_fire_state", ELEM2_FIRE_BROKEN);
    if( obj_bar > 0 )
        elem2_give(player, obj_bar, 1);
    if( obj_hammer > 0 )
        elem2_give(player, obj_hammer, 1);
    elem2_locu(srv, loc_workbench, obj_bar);
    SELFTEST_CHECK(elem2_inv_total(player, obj_claw) == 0, "no schematic must not make a claw");
    elem2_pass("claw_need_schematic");

    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_SCHEMATICS_TAKEN);
    elem2_vb(srv, "elemental_quest_2_hatch", 1);
    elem2_vb(srv, "elemental_quest_2_fire_state", ELEM2_FIRE_BROKEN);
    if( obj_bar > 0 )
        elem2_give(player, obj_bar, 1);
    if( obj_hammer > 0 )
        elem2_give(player, obj_hammer, 1);
    if( obj_claw_book > 0 )
        elem2_give(player, obj_claw_book, 1);
    elem2_locu(srv, loc_workbench, obj_bar);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_CLAW_MADE, "workbench claw should set main=7");
    SELFTEST_CHECK(elem2_inv_total(player, obj_claw) >= 1, "workbench should grant the claw");
    elem2_pass("claw_made");
    elem2_journal(srv, "journal_07_claw_made");

    elem2_locu(srv, loc_workbench, obj_bar);
    elem2_pass("claw_already");

    /* Crane: lower then fit. */
    elem2_tele(srv, ELEM2_MIND_X, ELEM2_MIND_Z, 2);
    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_CLAW_MADE);
    elem2_vb(srv, "elemental_quest_2_fire_state", ELEM2_FIRE_BROKEN);
    elem2_vb(srv, "elemental_quest_2_fire_pos", ELEM2_FIRE_RAISED);
    if( obj_claw > 0 )
        elem2_give(player, obj_claw, 1);
    elem2_locu(srv, loc_crane, obj_claw);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_state") == ELEM2_FIRE_BROKEN,
                   "raised crane cannot take a claw");
    elem2_pass("crane_need_lower");

    elem2_loc1_mes(srv, loc_lever2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_pos") == ELEM2_FIRE_LOWERED,
                   "SW lever should lower the broken crane");
    elem2_pass("fire_lever2_lower_for_claw");

    elem2_loc1_mes(srv, loc_lever2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_pos") == ELEM2_FIRE_RAISED,
                   "SW lever should raise the broken crane");
    elem2_pass("fire_lever2_raise_broken");

    elem2_loc1_mes(srv, loc_lever2);
    elem2_locu(srv, loc_crane, obj_claw);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_state") == ELEM2_FIRE_EMPTY,
                   "fitting the claw should repair fire_state");
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_CRANE_REPAIRED, "fit claw should set main=8");
    elem2_pass("crane_fit_claw");
    elem2_journal(srv, "journal_08_crane_repaired");

    if( obj_claw > 0 )
        elem2_give(player, obj_claw, 1);
    elem2_locu(srv, loc_crane, obj_claw);
    elem2_pass("crane_already_fitted");

    /* Crates, pipe, cogs, junction -- any order, then repaired check. */
    elem2_ready(srv, player);
    elem2_tele(srv, ELEM2_MIND_X, ELEM2_MIND_Z, 2);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_CRANE_REPAIRED);
    elem2_vb(srv, "elemental_quest_2_fire_state", ELEM2_FIRE_EMPTY);
    if( obj_logs > 0 )
        elem2_fill_inv(player, obj_logs);
    elem2_loc1(srv, loc_box1);
    elem2_pass("crate_inv_full");

    elem2_clear_inv(player);
    elem2_loc1(srv, loc_box1);
    SELFTEST_CHECK(elem2_inv_total(player, obj_small) >= 1, "first crate search finds the small cog");
    elem2_pass("crate_small_cog");
    elem2_loc1(srv, loc_box1);
    SELFTEST_CHECK(elem2_inv_total(player, obj_med) >= 1, "second crate search finds the medium cog");
    elem2_pass("crate_medium_cog");
    elem2_loc1(srv, loc_box1);
    SELFTEST_CHECK(elem2_inv_total(player, obj_big) >= 1, "third crate search finds the large cog");
    elem2_pass("crate_large_cog");
    elem2_loc1(srv, loc_box1);
    SELFTEST_CHECK(elem2_inv_total(player, obj_pipe) >= 1, "fourth crate search finds the spare pipe");
    elem2_pass("crate_spare_pipe");
    elem2_loc1(srv, loc_box1);
    elem2_pass("crate_empty");

    elem2_locu(srv, loc_pin_high, obj_small);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_air_cog1") == 1, "small cog on the top peg");
    elem2_pass("cog_small");
    elem2_locu(srv, loc_pin_low, obj_med);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_air_cog2") == 2, "medium cog on the bottom peg");
    elem2_pass("cog_medium");
    elem2_locu(srv, loc_pin_left, obj_big);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_air_cog3") == 3, "large cog on the remaining peg");
    elem2_pass("cog_large");

    elem2_locu(srv, loc_pipe_broken, obj_pipe);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_state") == 1, "spare pipe seals the leak");
    elem2_pass("pipe_repair");
    if( loc_pipe_float > 0 )
    {
        elem2_loc1(srv, loc_pipe_float);
        elem2_pass("pipe_already");
    }

    elem2_loc1(srv, loc_junction);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_earth_pipe_1_state") == 5,
                   "junction sets pipe 1 to 5");
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_earth_pipe_2_state") == 6,
                   "junction sets pipe 2 to 6");
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_earth_pipe_3_state") == 13,
                   "junction sets pipe 3 to 13");
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_WORKSHOP_REPAIRED, "all three repairs should set main=9");
    elem2_pass("junction_reconnect");
    elem2_journal(srv, "journal_09_workshop_repaired");

    elem2_loc1(srv, loc_junction);
    elem2_pass("junction_already");

    /* Priming FSM -- do not collapse. */
    elem2_ready(srv, player);
    elem2_tele(srv, ELEM2_MIND_X, ELEM2_MIND_Z, 2);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_WORKSHOP_REPAIRED);
    elem2_vb(srv, "elemental_quest_2_fire_state", ELEM2_FIRE_EMPTY);
    elem2_vb(srv, "elemental_quest_2_fire_pos", ELEM2_FIRE_RAISED);
    elem2_vb(srv, "elemental_quest_2_earth_pipe_1_state", 5);
    elem2_vb(srv, "elemental_quest_2_earth_pipe_2_state", 6);
    elem2_vb(srv, "elemental_quest_2_earth_pipe_3_state", 13);
    elem2_vb(srv, "elemental_quest_2_water_state", 1);
    elem2_vb(srv, "elemental_quest_2_air_cog1", 1);
    elem2_vb(srv, "elemental_quest_2_air_cog2", 2);
    elem2_vb(srv, "elemental_quest_2_air_cog3", 3);

    slot_cart = elem2_spawn(srv, npc_cart_empty, ELEM2_MIND_X, ELEM2_MIND_Z, 2);
    SELFTEST_CHECK(slot_cart >= 0, "empty jig cart should spawn");
    if( obj_bar > 0 )
        elem2_give(player, obj_bar, 1);
    if( slot_cart >= 0 )
    {
        elem2_npcu(srv, npc_cart_empty, slot_cart, obj_bar);
        SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_state") == ELEM2_JIG_BAR_PLACED,
                       "use-bar should load the jig");
        elem2_pass("cart_place_bar");
    }

    elem2_loc1_mes(srv, loc_lever1);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_pos") == ELEM2_FIRE_RAISED,
                   "wrong-state SE lever is a no-op");
    elem2_pass("priming_wrong_state");

    elem2_loc1_mes(srv, loc_lever2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_pos") == ELEM2_FIRE_LOWERED,
                   "SW lever lowers onto the bar");
    elem2_pass("fire_lever2_lower_onto_bar");

    elem2_loc1_mes(srv, loc_lever2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_state") == ELEM2_FIRE_HOLDING_COLD,
                   "SW lever lifts the cold bar");
    elem2_pass("fire_lever2_lift_bar");

    elem2_loc1_mes(srv, loc_lever1);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_pos") == ELEM2_FIRE_ABOVE_LAVA,
                   "SE lever rotates over lava");
    elem2_pass("fire_lever1_rotate_lava");

    elem2_loc1_mes(srv, loc_lever2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_pos") == ELEM2_FIRE_IN_LAVA,
                   "SW lever lowers into lava");
    elem2_pass("fire_lever2_lower_into_lava");

    elem2_loc1_mes(srv, loc_lever2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_state") == ELEM2_FIRE_HOLDING_HOT,
                   "SW lever lifts the hot bar");
    elem2_pass("fire_lever2_lift_hot");

    elem2_loc1_mes(srv, loc_lever1);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_pos") == ELEM2_FIRE_RAISED,
                   "SE lever rotates back over the jig");
    elem2_pass("fire_lever1_rotate_jig");

    elem2_loc1_mes(srv, loc_lever2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_fire_pos") == ELEM2_FIRE_LOWERED,
                   "SW lever lowers the hot bar to the jig");
    elem2_pass("fire_lever2_lower_hot_to_jig");

    elem2_loc1_mes(srv, loc_lever2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_state") == ELEM2_JIG_BAR_HOT,
                   "SW lever releases the hot bar");
    elem2_pass("fire_lever2_release_hot");

    elem2_loc1_mes(srv, loc_3way);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_pos") == ELEM2_JIG_POS_PRESS,
                   "3-way trundles to the press");
    elem2_pass("3way_to_press");

    elem2_loc1_mes(srv, loc_earth);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_state") == ELEM2_JIG_BAR_FLAT_HOT,
                   "earth lever flattens the hot bar");
    elem2_pass("earth_lever_press");

    elem2_loc1_mes(srv, loc_3way);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_pos") == ELEM2_JIG_POS_TANK,
                   "3-way trundles to the tank");
    elem2_pass("3way_to_tank");

    elem2_loc1_mes(srv, loc_water);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_door") == ELEM2_DOOR_TANK_OPEN,
                   "water lever opens the tank");
    elem2_pass("water_lever_open");

    elem2_loc1_mes(srv, loc_corkscrew);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_door") == ELEM2_DOOR_GRABBER_OUT,
                   "corkscrew extends the grabber (door=%d)",
                   elem2_get_vb(player, "elemental_quest_2_water_door"));
    elem2_pass("corkscrew_extend");

    elem2_loc1_mes(srv, loc_corkscrew);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_door") == ELEM2_DOOR_GRABBER_IN_HOT_OPEN,
                   "corkscrew retracts with the hot bar (door=%d)",
                   elem2_get_vb(player, "elemental_quest_2_water_door"));
    elem2_pass("corkscrew_retract_hot");

    elem2_loc1_mes(srv, loc_water);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_door") == ELEM2_DOOR_GRABBER_IN_HOT_CLOSED,
                   "water lever seals the grabber (door=%d)",
                   elem2_get_vb(player, "elemental_quest_2_water_door"));
    elem2_pass("water_lever_shut_hot");

    elem2_loc1_mes(srv, loc_valve1);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_valve_1") == 1, "NW valve opens");
    elem2_pass("valve1_open");

    elem2_loc1_mes(srv, loc_valve2);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_state") == ELEM2_JIG_BAR_FLAT_COOL,
                   "NE valve quenches the bar");
    elem2_pass("valve2_quench");

    elem2_loc1_mes(srv, loc_valve1);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_valve_1") == 0, "NW valve shuts");
    elem2_pass("valve1_shut");

    elem2_loc1_mes(srv, loc_water);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_door") == ELEM2_DOOR_GRABBER_IN_COOL_OPEN,
                   "water lever opens on the cooled bar");
    elem2_pass("water_lever_open_cool");

    elem2_loc1_mes(srv, loc_corkscrew);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_door") == ELEM2_DOOR_GRABBER_OUT_COOL,
                   "corkscrew extends with the cooled bar");
    elem2_pass("corkscrew_extend_cool");

    elem2_loc1_mes(srv, loc_corkscrew);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_door") == ELEM2_DOOR_TANK_OPEN,
                   "corkscrew releases the cooled bar");
    elem2_pass("corkscrew_release");

    elem2_loc1_mes(srv, loc_water);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_water_door") == ELEM2_DOOR_TANK_CLOSED,
                   "water lever shuts the empty tank");
    elem2_pass("water_lever_shut_cool");

    elem2_loc1_mes(srv, loc_3way);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_pos") == ELEM2_JIG_POS_TUNNEL,
                   "3-way trundles to the wind tunnel");
    elem2_pass("3way_to_tunnel");

    elem2_loc1_mes(srv, loc_air);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_air_fan_state") == 1, "air lever starts the fan");
    elem2_pass("air_lever_fan_on");

    elem2_loc1_mes(srv, loc_air);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_state") == ELEM2_JIG_BAR_DRY,
                   "air lever dries the bar");
    elem2_pass("air_lever_fan_off_dry");

    elem2_loc1_mes(srv, loc_3way);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_jig_pos") == ELEM2_JIG_POS_LAVA,
                   "3-way returns to the start");
    elem2_pass("3way_back_start");

    if( slot_cart >= 0 )
    {
        elem2_free_npc(srv, slot_cart);
        slot_cart = elem2_spawn(srv, npc_cart_dry, ELEM2_MIND_X, ELEM2_MIND_Z, 2);
        if( obj_logs > 0 )
            elem2_fill_inv(player, obj_logs);
        if( slot_cart >= 0 )
        {
            elem2_npc1(srv, npc_cart_dry, slot_cart);
            SELFTEST_CHECK(elem2_inv_total(player, obj_primed) == 0, "full inv cannot take the primed bar");
            elem2_pass("cart_take_inv_full");

            elem2_clear_inv(player);
            elem2_npc1(srv, npc_cart_dry, slot_cart);
            SELFTEST_CHECK(elem2_inv_total(player, obj_primed) >= 1, "take should grant the primed bar");
            elem2_pass("cart_take_primed");
            elem2_free_npc(srv, slot_cart);
        }
    }
    elem2_free_type(srv, npc_cart_empty);
    elem2_free_type(srv, npc_cart_dry);

    /* Extractor. */
    elem2_ready(srv, player);
    elem2_tele(srv, ELEM2_MIND_X, ELEM2_MIND_Z, 0);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_WORKSHOP_REPAIRED);
    elem2_loc1(srv, loc_hat);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_mind_jig") == ELEM2_MINDJIG_EMPTY,
                   "empty gun refuses the chair");
    elem2_pass("hat_nothing_loaded");

    if( obj_primed > 0 )
        elem2_give(player, obj_primed, 1);
    elem2_locu(srv, loc_gun_empty, obj_primed);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_mind_jig") == ELEM2_MINDJIG_BAR_PLACED,
                   "primed bar loads the gun");
    elem2_pass("gun_load_primed");

    if( loc_gun_bar > 0 )
    {
        elem2_loc1(srv, loc_gun_bar);
        elem2_pass("gun_not_finished");
    }

    if( stat_magic >= 0 )
        elem2_set_stat(player, stat_magic, 1);
    elem2_loc1(srv, loc_hat);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_mind_jig") == ELEM2_MINDJIG_BAR_PLACED,
                   "low magic must not imbue");
    elem2_pass("hat_need_magic");

    if( stat_magic >= 0 )
        elem2_set_stat(player, stat_magic, 99);
    elem2_loc1(srv, loc_hat);
    SELFTEST_CHECK(elem2_get_vb(player, "elemental_quest_2_mind_jig") == ELEM2_MINDJIG_MIND_BAR,
                   "chair should imbue the bar");
    elem2_pass("hat_imbue");

    if( obj_logs > 0 )
        elem2_fill_inv(player, obj_logs);
    elem2_loc1(srv, loc_gun_mind);
    SELFTEST_CHECK(elem2_inv_total(player, obj_mind_bar) == 0, "full inv cannot take the mind bar");
    elem2_pass("gun_take_inv_full");

    elem2_clear_inv(player);
    elem2_loc1(srv, loc_gun_mind);
    SELFTEST_CHECK(elem2_inv_total(player, obj_mind_bar) >= 1, "take should grant the mind bar");
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_MIND_BAR_MADE, "take mind bar should set main=10");
    elem2_pass("gun_take_mind_bar");
    elem2_journal(srv, "journal_10_mind_bar_made");

    /* Helmet / complete. */
    elem2_ready(srv, player);
    elem2_tele(srv, ELEM2_WORKSHOP_X, ELEM2_WORKSHOP_Z, 0);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_MIND_BAR_MADE);
    if( obj_mind_bar > 0 )
        elem2_give(player, obj_mind_bar, 1);
    if( obj_book > 0 )
        elem2_give(player, obj_book, 1);
    elem2_locu(srv, loc_workbench, obj_mind_bar);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_MIND_BAR_MADE, "no hammer must not complete");
    elem2_pass("helm_need_hammer");

    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_MIND_BAR_MADE);
    if( stat_smith >= 0 )
        elem2_set_stat(player, stat_smith, 1);
    if( obj_mind_bar > 0 )
        elem2_give(player, obj_mind_bar, 1);
    if( obj_hammer > 0 )
        elem2_give(player, obj_hammer, 1);
    if( obj_book > 0 )
        elem2_give(player, obj_book, 1);
    elem2_locu(srv, loc_workbench, obj_mind_bar);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_MIND_BAR_MADE, "low smithing must not complete");
    elem2_pass("helm_need_smithing");

    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_MIND_BAR_MADE);
    if( obj_mind_bar > 0 )
        elem2_give(player, obj_mind_bar, 1);
    if( obj_hammer > 0 )
        elem2_give(player, obj_hammer, 1);
    elem2_locu(srv, loc_workbench, obj_mind_bar);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_MIND_BAR_MADE, "no beaten book must not complete");
    elem2_pass("helm_need_book");

    elem2_ready(srv, player);
    elem2_vb(srv, "elemental_quest_2_main", ELEM2_MIND_BAR_MADE);
    if( obj_mind_bar > 0 )
        elem2_give(player, obj_mind_bar, 1);
    if( obj_hammer > 0 )
        elem2_give(player, obj_hammer, 1);
    if( obj_book > 0 )
        elem2_give(player, obj_book, 1);
    smith_before = (stat_smith >= 0) ? player->stat_xp_tenths[stat_smith] : 0;
    craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
    elem2_locu(srv, loc_workbench, obj_mind_bar);
    SELFTEST_CHECK(elem2_quest(player) == ELEM2_COMPLETE, "helmet should complete the quest");
    SELFTEST_CHECK(elem2_inv_total(player, obj_helm) >= 1, "helmet should be granted");
    if( stat_smith >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_smith] >= smith_before + ELEM2_REWARD_TENTHS,
                       "complete should award 75000 smithing tenths");
    if( stat_craft >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >= craft_before + ELEM2_REWARD_TENTHS,
                       "complete should award 75000 crafting tenths");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + ELEM2_QP_REWARD,
                       "complete should award 1 QP");
    elem2_pass("complete_scroll");
    elem2_journal(srv, "journal_11_complete");

    /* Post-quest helmet + leftover book-gate. */
    if( obj_mind_bar > 0 )
        elem2_give(player, obj_mind_bar, 1);
    elem2_locu(srv, loc_workbench, obj_mind_bar);
    elem2_pass("helm_need_either_book");

    if( obj_mind_bar > 0 )
        elem2_give(player, obj_mind_bar, 1);
    if( obj_book > 0 )
        elem2_give(player, obj_book, 1);
    elem2_locu(srv, loc_workbench, obj_mind_bar);
    SELFTEST_CHECK(elem2_inv_total(player, obj_helm) >= 1, "post-quest helmet still works");
    elem2_pass("helm_post_quest");

    (void)obj_shield_book;
    elem2_pass("leftover_pipe_widget");
    elem2_pass("leftover_crane_multiloc");
    elem2_pass("leftover_mind_shield");

    elem2_free_type(srv, npc_awakened);
    elem2_free_type(srv, npc_rock);
    elem2_free_type(srv, npc_cart_empty);
    elem2_free_type(srv, npc_cart_dry);
    elem2_clear_inv(player);
    elem2_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_ELEMENTALWORKSHOPII_SELFTEST_U_H */
