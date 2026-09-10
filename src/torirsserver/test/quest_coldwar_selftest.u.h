#ifndef TORIRSSERVER_TEST_QUEST_COLDWAR_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_COLDWAR_SELFTEST_U_H

/* Cold War Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Larry / Thing / Fred / KGP / Noodle /
 * Ping / icelord / hide / clockwork locs cannot leak. Real OPNPC1 /
 * OPLOC1 / OPLOC2 / OPLOCU / OPHELDU on the authored path.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * [opnpc1,peng_larry_zoo] / [opnpc1,peng_multi_larry_lumb] already exist
 * in coldwar_larry.rs2 -- this walk calls that trigger. Do not add a
 * second Larry-zoo header. Fred is reached through the existing
 * [opnpc1,fred_the_farmer] dispatcher. Clockwork suit is
 * [oploc1,poh_clockmaking_3] / _4 in the POH workshop. The Relleka boat
 * is merged into Making Friends' [oploc1,peng_boat_rell].
 *
 * Gate: TORIRSSERVER_SELFTEST_COLDWAR_ONLY=1
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF /
 * daily-reset / flute widget / telekinetic grab / POH clockwork table):
 *   - crush-course per-obstacle pathing narrated at the door
 *   - icelord real combat (narrated kill)
 *   - war-room anti-magic cutscene
 *   - Pescaling Pax speech (no reachable opnpc)
 *   - post-quest Penguin Points / Agility Course reward loop
 */

#define CW_NOT_STARTED 0
#define CW_BIRDHIDE 5
#define CW_EMOTES 10
#define CW_AFTER_EMOTES 15
#define CW_RELLEKA 20
#define CW_CLOCKWORK 25
#define CW_SUIT_ICE 30
#define CW_ZOO_TRUST 35
#define CW_ZOO_REPORT 40
#define CW_LUMB 45
#define CW_ZOO_RETURN 50
#define CW_THING_RETURN 55
#define CW_FRED 60
#define CW_OUTPOST 65
#define CW_ICEBERG_KGP 70
#define CW_NOODLE1 75
#define CW_NOODLE2 80
#define CW_KGP_AGAIN 85
#define CW_DEBRIEF 90
#define CW_AGILITY_READY 95
#define CW_AGILITY_DONE 100
#define CW_ARMY 105
#define CW_PINGPONG 110
#define CW_INSTRUMENTS 115
#define CW_CONTROL 120
#define CW_ICELORDS 125
#define CW_ESCAPE 130
#define CW_COMPLETE 135

#define CW_HIDE_NONE 0
#define CW_HIDE_FRAME 1
#define CW_HIDE_BUILT 2
#define CW_KGP_NONE 0
#define CW_KGP_BONGOS 1
#define CW_KGP_LURED 2

#define CW_REQ_HUNTER 10
#define CW_REQ_AGILITY 30
#define CW_REQ_CRAFTING 30
#define CW_REQ_CONSTRUCTION 34
#define CW_REQ_THIEVING 15

#define CW_REWARD_AGILITY_TENTHS 50000
#define CW_REWARD_CRAFTING_TENTHS 20000
#define CW_REWARD_CONSTRUCTION_TENTHS 15000
#define CW_REWARD_QP 1

#define CW_ZOO_X 2597
#define CW_ZOO_Z 3266
#define CW_HIDE_X 2666
#define CW_HIDE_Z 3991
#define CW_HIDE_LV 1
#define CW_ICE_X 2670
#define CW_ICE_Z 3988
#define CW_ICE_LV 1
#define CW_RELL_X 2707
#define CW_RELL_Z 3732
#define CW_PEN_X 2594
#define CW_PEN_Z 3266
#define CW_ZOOP_X 2596
#define CW_ZOOP_Z 3270
#define CW_THING_X 3201
#define CW_THING_Z 3266
#define CW_FRED_X 3189
#define CW_FRED_Z 3273
#define CW_COW_X 3172
#define CW_COW_Z 3318
#define CW_AVAL_X 2638
#define CW_AVAL_Z 4011
#define CW_AVAL_LV 1
#define CW_KGP_X 2647
#define CW_KGP_Z 10384
#define CW_NOODLE_X 2644
#define CW_NOODLE_Z 4008
#define CW_NOODLE_LV 1
#define CW_AGI_DOOR_X 2633
#define CW_AGI_DOOR_Z 10404
#define CW_INSTR_X 2652
#define CW_INSTR_Z 4035
#define CW_PING_X 2668
#define CW_PING_Z 10396
#define CW_BOOTH_X 2671
#define CW_BOOTH_Z 10418
#define CW_ICELORD_X 2647
#define CW_ICELORD_Z 10425
#define CW_PENDOOR_X 2639
#define CW_PENDOOR_Z 10424
#define CW_CHASM_X 2657
#define CW_CHASM_Z 10423

static void
cw_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "CW PASS: %s\n", step);
}

static void
cw_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
cw_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
cw_drain(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    int t;

    assert(srv);
    player = srv->active_player;
    assert(player);
    for( t = 0; t < 8 && player->active_script; t++ )
    {
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
    ToriRSServer_ScriptsProcessQueues(srv);
}

static void
cw_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 1);
        selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    for( t = 0; t < 4; t++ )
        selftest_tick(srv);
}

static int
cw_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
cw_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : cw_chatmenu();
    if( uid <= 0 )
        return;
    button[0] = (uint8_t)(uid >> 24);
    button[1] = (uint8_t)(uid >> 16);
    button[2] = (uint8_t)(uid >> 8);
    button[3] = (uint8_t)uid;
    button[4] = (uint8_t)(row >> 8);
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
cw_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = cw_chatmenu();
    for( clicks = 0; clicks < max_pages && player->active_script; clicks++ )
    {
        int uid;
        uint8_t resume[6];

        if( player->resume_button_count <= 0 )
            break;
        uid = player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            return;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        selftest_tick(srv);
    }
}

static void
cw_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    cw_god(player);
    selftest_tick(srv);
}

static int
cw_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    cw_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
cw_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
cw_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
cw_get_vb(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
cw_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
cw_quest(struct ToriRSServerPlayer* player)
{
    return cw_get_vb(player, "peng_quest");
}

static void
cw_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;
    int rc;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    cw_drain(srv);
    assert(slot >= 0);
    assert(srv->npcs[slot].active);
    player->last_slot = slot;
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 type %d slot %d should run (rc=%d active=%d)",
                   npc_type, slot, rc, srv->npcs[slot].active);
}

static void
cw_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    cw_talk(srv, npc_type, slot);
    cw_finish(srv);
}

static void
cw_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    cw_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        cw_click_until_menu(srv, 24);
        cw_pick_row(srv, rows[i]);
    }
    cw_finish(srv);
}

static int
cw_fresh(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    cw_drain(srv);
    return cw_spawn(srv, npc_type, x, z, level);
}

static int
cw_fresh_larry_zoo(struct ToriRSServer* srv, int npc_type)
{
    assert(srv);
    assert(npc_type > 0);
    return cw_fresh(srv, npc_type, CW_ZOO_X, CW_ZOO_Z, 0);
}

static void
cw_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
            inv_set(player, s, obj_id, player->inv[s].count + count);
            return;
        }
    }
}

static int
cw_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    cw_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
cw_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    cw_finish(srv);
}

static void
cw_oploc2(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC2, loc_id, -1, -1);
    cw_finish(srv);
}

static void
cw_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    cw_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
cw_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    cw_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
cw_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
cw_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        cw_set_stat(player, stat, 99);
}

static int
cw_inv_has(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id && player->inv[s].count > 0 )
            return 1;
    }
    return 0;
}

static void
cw_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    cw_vb(srv, "peng_quest", CW_NOT_STARTED);
    cw_vb(srv, "peng_transmog", 0);
    cw_vb(srv, "peng_doing_greeting", 0);
    cw_vb(srv, "peng_multi_hide", CW_HIDE_NONE);
    cw_vb(srv, "peng_multi_kgp", CW_KGP_NONE);
    cw_vb(srv, "peng_emote_1", 1);
    cw_vb(srv, "peng_emote_2", 2);
    cw_vb(srv, "peng_emote_3", 3);
    cw_vb(srv, "peng_pong_chat", 0);
}

static void
cw_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,coldwar_journal]", NULL, 0);
    cw_finish(srv);
    cw_pass(step);
}

static void
selftest_quest_coldwar(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_larry_zoo;
    int npc_larry_ice;
    int npc_larry_rell;
    int npc_zoo;
    int npc_thing;
    int npc_fred;
    int npc_kgp;
    int npc_noodle;
    int npc_instructor;
    int npc_ping;
    int npc_icelord;
    int loc_hide;
    int loc_clock;
    int loc_pen;
    int loc_cow;
    int loc_aval;
    int loc_agi;
    int loc_booth;
    int loc_wardoor;
    int loc_pendoor;
    int loc_chasm;
    int loc_ice_boat;
    int loc_rell_boat;
    int obj_oak;
    int obj_nails;
    int obj_hammer;
    int obj_spade;
    int obj_suit;
    int obj_mech;
    int obj_plank;
    int obj_silk;
    int obj_cod;
    int obj_tar;
    int obj_feather;
    int obj_id;
    int obj_report2;
    int obj_cowbell;
    int obj_bongos;
    int obj_mahog;
    int obj_leather;
    int stat_hunter;
    int stat_agi;
    int stat_craft;
    int stat_con;
    int stat_thiev;
    int stat_attack;
    int varp_qp;
    int slot_larry;
    int slot_ice;
    int slot_rell;
    int slot_zoo;
    int slot_thing;
    int slot_fred;
    int slot_kgp;
    int slot_noodle;
    int slot_instr;
    int slot_ping;
    int slot_icelord;
    int loc_slot;
    int agi_before;
    int craft_before;
    int con_before;
    int attack_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };
    static const int k_emote[] = { 1, 2, 3 };
    static const int k_clock_toys[] = { 2, 3 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: cold war critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer coldwar selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    cw_god(player);
    cw_reset_quest(srv);
    cw_clear_inv(player);

    npc_larry_zoo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_larry_zoo");
    npc_larry_ice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_larry_ice");
    npc_larry_rell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_larry_rell");
    npc_zoo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_zoo");
    npc_thing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sheep_shearer_the_thing");
    npc_fred = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fred_the_farmer");
    npc_kgp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_kgp");
    npc_noodle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_noodle");
    npc_instructor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_agility_instructor");
    npc_ping = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_ping");
    npc_icelord = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_icelord_warrior01");
    loc_hide = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_observer_cabin_multiloc");
    loc_clock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "poh_clockmaking_3");
    loc_pen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_ardougne_enclosure_door");
    loc_cow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fat_cow");
    loc_aval = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_aval_l");
    loc_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_base_double_door_mid_agility");
    loc_booth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_base_booth_front");
    loc_wardoor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_base_door");
    loc_pendoor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_icelord_pen_door");
    loc_chasm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_base_chasm");
    loc_ice_boat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_row_boat_clickzone");
    loc_rell_boat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_boat_rell");
    obj_oak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "plank_oak");
    obj_nails = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nails");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
    obj_suit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "peng_suit_unwound");
    obj_mech = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "poh_clockwork_mechanism");
    obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "woodplank");
    obj_silk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silk");
    obj_cod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_cod");
    obj_tar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamp_tar");
    obj_feather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feather");
    obj_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "peng_id");
    obj_report2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "peng_report_2");
    obj_cowbell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "peng_cowbell");
    obj_bongos = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "peng_bongos");
    obj_mahog = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "plank_mahogany");
    obj_leather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "leather");
    stat_hunter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    stat_thiev = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    stat_attack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_coldwar") > 0,
                   "dbrow quest_coldwar should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "peng_quest") >= 0,
                   "varbit peng_quest should resolve");
    SELFTEST_CHECK(npc_larry_zoo > 0, "npc peng_larry_zoo should resolve");
    if( npc_larry_zoo <= 0 )
        return;

    cw_journal(srv, "journal_00_not_started");

    slot_larry = cw_spawn(srv, npc_larry_zoo, CW_ZOO_X, CW_ZOO_Z, 0);
    SELFTEST_CHECK(slot_larry >= 0, "Larry zoo should spawn");
    if( slot_larry < 0 )
        return;

    cw_set_stat(player, stat_hunter, 1);
    cw_set_stat(player, stat_agi, 1);
    cw_set_stat(player, stat_craft, 1);
    cw_set_stat(player, stat_con, 1);
    cw_set_stat(player, stat_thiev, 1);
    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_NOT_STARTED,
                   "Hunter qualify-fail must stay not_started");
    cw_pass("opnpc1_larry_qualify_fail_hunter");

    cw_set_stat(player, stat_hunter, 99);
    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_NOT_STARTED,
                   "Agility qualify-fail must stay not_started");
    cw_pass("opnpc1_larry_qualify_fail_agility");

    cw_set_stat(player, stat_agi, 99);
    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_NOT_STARTED,
                   "Crafting qualify-fail must stay not_started");
    cw_pass("opnpc1_larry_qualify_fail_crafting");

    cw_set_stat(player, stat_craft, 99);
    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_NOT_STARTED,
                   "Construction qualify-fail must stay not_started");
    cw_pass("opnpc1_larry_qualify_fail_construction");

    cw_set_stat(player, stat_con, 99);
    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_NOT_STARTED,
                   "Thieving qualify-fail must stay not_started");
    cw_pass("opnpc1_larry_qualify_fail_thieving");

    cw_skills99(player);
    cw_talk_rows(srv, npc_larry_zoo, slot_larry, k_refuse, 1);
    SELFTEST_CHECK(cw_quest(player) == CW_NOT_STARTED,
                   "Larry refuse must stay not_started");
    cw_pass("opnpc1_larry_refuse");

    cw_talk_rows(srv, npc_larry_zoo, slot_larry, k_accept, 1);
    SELFTEST_CHECK(cw_quest(player) == CW_BIRDHIDE,
                   "Larry accept should write birdhide_setup, got %d",
                   cw_quest(player));
    cw_pass("opnpc1_larry_accept");

    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_BIRDHIDE,
                   "materials reminder must stay at birdhide_setup");
    cw_pass("opnpc1_larry_birdhide_need_items");

    if( obj_oak > 0 )
        cw_give(player, obj_oak, 10);
    if( obj_nails > 0 )
        cw_give(player, obj_nails, 10);
    if( obj_hammer > 0 )
        cw_give(player, obj_hammer, 1);
    if( obj_spade > 0 )
        cw_give(player, obj_spade, 1);
    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    cw_pass("opnpc1_larry_birdhide_has_items");

    if( loc_ice_boat > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_ice_boat, CW_ICE_X, CW_ICE_Z, CW_ICE_LV);
        cw_oploc(srv, loc_ice_boat, loc_slot);
        cw_pass("oploc1_ice_boat_row_relleka");
    }
    if( loc_rell_boat > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_rell_boat, CW_RELL_X, CW_RELL_Z, 0);
        cw_oploc(srv, loc_rell_boat, loc_slot);
        cw_pass("oploc1_rell_boat_to_iceberg");
    }

    if( loc_hide > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_hide, CW_HIDE_X, CW_HIDE_Z, CW_HIDE_LV);
        cw_use_loc(srv, loc_hide, loc_slot, obj_oak);
        SELFTEST_CHECK(cw_get_vb(player, "peng_multi_hide") == CW_HIDE_FRAME,
                       "oak planks should raise hide frame, got %d",
                       cw_get_vb(player, "peng_multi_hide"));
        cw_pass("oplocu_hide_oak_build");
        cw_use_loc(srv, loc_hide, loc_slot, obj_spade);
        SELFTEST_CHECK(cw_get_vb(player, "peng_multi_hide") == CW_HIDE_BUILT,
                       "spade should cover the hide, got %d",
                       cw_get_vb(player, "peng_multi_hide"));
        cw_pass("oplocu_hide_spade_cover");
        cw_oploc(srv, loc_hide, loc_slot);
        cw_pass("oploc1_hide_enter_ready");
    }
    else
    {
        cw_vb(srv, "peng_multi_hide", CW_HIDE_BUILT);
    }

    if( npc_larry_ice > 0 )
    {
        slot_ice = cw_fresh(srv, npc_larry_ice, CW_ICE_X, CW_ICE_Z, CW_ICE_LV);
        SELFTEST_CHECK(slot_ice >= 0, "Larry ice should spawn");
        if( slot_ice >= 0 )
        {
            fprintf(stderr, "CW hide=%d quest=%d before ice watch\n",
                    cw_get_vb(player, "peng_multi_hide"), cw_quest(player));
            cw_talk_finish(srv, npc_larry_ice, slot_ice);
            SELFTEST_CHECK(cw_quest(player) == CW_EMOTES,
                           "watching the hide should learn emotes, got %d hide=%d",
                           cw_quest(player), cw_get_vb(player, "peng_multi_hide"));
            cw_pass("opnpc1_larry_ice_watch_emotes");
            cw_talk_finish(srv, npc_larry_ice, slot_ice);
            SELFTEST_CHECK(cw_quest(player) == CW_AFTER_EMOTES,
                           "That's crazy should advance after_emotes, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_larry_ice_emotes_crazy");
            cw_free_npc(srv, slot_ice);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_AFTER_EMOTES);
    }

    if( npc_larry_rell > 0 )
    {
        slot_rell = cw_fresh(srv, npc_larry_rell, CW_RELL_X, CW_RELL_Z, 0);
        if( slot_rell >= 0 )
        {
            cw_talk_finish(srv, npc_larry_rell, slot_rell);
            SELFTEST_CHECK(cw_quest(player) == CW_CLOCKWORK,
                           "Relleka Larry should send you to the POH table, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_larry_rell_clockwork_plan");
            cw_free_npc(srv, slot_rell);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_CLOCKWORK);
    }

    cw_journal(srv, "journal_25_clockwork");

    if( loc_clock > 0 && obj_mech > 0 && obj_plank > 0 && obj_silk > 0 )
    {
        cw_clear_inv(player);
        cw_give(player, obj_mech, 1);
        cw_give(player, obj_plank, 1);
        cw_give(player, obj_silk, 1);
        loc_slot = cw_place_loc(srv, loc_clock, CW_RELL_X, CW_RELL_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_clock, -1, loc_slot);
        cw_click_until_menu(srv, 8);
        cw_pick_row(srv, k_clock_toys[0]);
        cw_click_until_menu(srv, 8);
        cw_pick_row(srv, k_clock_toys[1]);
        cw_finish(srv);
        SELFTEST_CHECK(cw_inv_has(player, obj_suit),
                       "POH clockmaking_3 should craft peng_suit_unwound");
        cw_pass("oploc1_poh_clockmaking_3_penguin");
    }
    else if( obj_suit > 0 )
    {
        cw_give(player, obj_suit, 1);
    }

    cw_free_npc(srv, slot_larry);
    slot_larry = cw_fresh(srv, npc_larry_zoo, CW_ZOO_X, CW_ZOO_Z, 0);
    if( slot_larry >= 0 )
        cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_SUIT_ICE,
                   "showing the suit at the zoo should send you to the iceberg, got %d",
                   cw_quest(player));
    cw_pass("opnpc1_larry_clockwork_has_suit");

    if( npc_larry_ice > 0 )
    {
        slot_ice = cw_fresh(srv, npc_larry_ice, CW_ICE_X, CW_ICE_Z, CW_ICE_LV);
        if( slot_ice >= 0 )
        {
            cw_talk_rows(srv, npc_larry_ice, slot_ice, k_accept, 1);
            SELFTEST_CHECK(cw_quest(player) == CW_ZOO_TRUST,
                           "iceberg suit show should write zoo_trust, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_larry_ice_suit_show");
            cw_free_npc(srv, slot_ice);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_ZOO_TRUST);
    }

    slot_larry = cw_fresh_larry_zoo(srv, npc_larry_zoo);
    SELFTEST_CHECK(slot_larry >= 0, "Larry zoo should respawn for tuxedo");
    cw_talk_rows(srv, npc_larry_zoo, slot_larry, k_refuse, 1);
    SELFTEST_CHECK(cw_quest(player) == CW_ZOO_TRUST,
                   "tuxedo refuse must stay zoo_trust");
    cw_pass("opnpc1_larry_tuxedo_refuse");
    cw_talk_rows(srv, npc_larry_zoo, slot_larry, k_accept, 1);
    SELFTEST_CHECK(cw_get_vb(player, "peng_transmog") == 1,
                   "tuxedo yes should wear the suit");
    cw_pass("opnpc1_larry_tuxedo_yes");

    if( loc_pen > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_pen, CW_PEN_X, CW_PEN_Z, 0);
        cw_oploc(srv, loc_pen, loc_slot);
        cw_pass("oploc1_enclosure_waddle");
    }

    if( npc_zoo > 0 )
    {
        slot_zoo = cw_fresh(srv, npc_zoo, CW_ZOOP_X, CW_ZOOP_Z, 0);
        if( slot_zoo >= 0 )
        {
            cw_vb(srv, "peng_emote_1", 1);
            cw_vb(srv, "peng_emote_2", 2);
            cw_vb(srv, "peng_emote_3", 3);
            cw_talk_rows(srv, npc_zoo, slot_zoo, k_emote, 3);
            SELFTEST_CHECK(cw_quest(player) == CW_ZOO_REPORT,
                           "correct zoo greeting should write zoo_report, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_zoo_emote_success");
            cw_free_npc(srv, slot_zoo);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_ZOO_REPORT);
    }

    slot_larry = cw_fresh_larry_zoo(srv, npc_larry_zoo);
    SELFTEST_CHECK(slot_larry >= 0, "Larry zoo should respawn for zoo report");
    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_LUMB,
                   "zoo report to Larry should send you to Lumbridge, got %d",
                   cw_quest(player));
    cw_pass("opnpc1_larry_zoo_report");

    slot_larry = cw_fresh_larry_zoo(srv, npc_larry_zoo);
    cw_talk_rows(srv, npc_larry_zoo, slot_larry, k_accept, 1);
    cw_pass("opnpc1_larry_lumb_tuxedo_yes");

    if( npc_thing > 0 )
    {
        slot_thing = cw_fresh(srv, npc_thing, CW_THING_X, CW_THING_Z, 0);
        if( slot_thing >= 0 )
        {
            cw_vb(srv, "peng_emote_1", 1);
            cw_vb(srv, "peng_emote_2", 2);
            cw_vb(srv, "peng_emote_3", 3);
            cw_talk_rows(srv, npc_thing, slot_thing, k_emote, 3);
            SELFTEST_CHECK(cw_quest(player) == CW_ZOO_RETURN,
                           "Thing greeting should ask for the passphrase, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_thing_emote_success");
            cw_free_npc(srv, slot_thing);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_ZOO_RETURN);
    }

    if( npc_zoo > 0 && obj_cod > 0 )
    {
        cw_give(player, obj_cod, 1);
        slot_zoo = cw_fresh(srv, npc_zoo, CW_ZOOP_X, CW_ZOOP_Z, 0);
        if( slot_zoo >= 0 )
        {
            cw_talk_finish(srv, npc_zoo, slot_zoo);
            SELFTEST_CHECK(cw_quest(player) == CW_THING_RETURN,
                           "cod should buy the passphrase, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_zoo_cod_passphrase");
            cw_free_npc(srv, slot_zoo);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_THING_RETURN);
    }

    if( npc_thing > 0 )
    {
        slot_thing = cw_fresh(srv, npc_thing, CW_THING_X, CW_THING_Z, 0);
        if( slot_thing >= 0 )
        {
            cw_talk_finish(srv, npc_thing, slot_thing);
            SELFTEST_CHECK(cw_quest(player) == CW_FRED,
                           "passphrase should unlock Fred, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_thing_passphrase");
            cw_free_npc(srv, slot_thing);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_FRED);
    }

    if( npc_fred > 0 )
    {
        slot_fred = cw_fresh(srv, npc_fred, CW_FRED_X, CW_FRED_Z, 0);
        if( slot_fred >= 0 )
        {
            cw_talk_rows(srv, npc_fred, slot_fred, k_refuse, 1);
            SELFTEST_CHECK(cw_quest(player) == CW_FRED,
                           "Fred never-mind must stay at fred");
            cw_pass("opnpc1_fred_nevermind");
            cw_talk_rows(srv, npc_fred, slot_fred, k_accept, 1);
            SELFTEST_CHECK(cw_quest(player) == CW_OUTPOST,
                           "bullying Fred should write outpost_info, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_fred_bully");
            cw_free_npc(srv, slot_fred);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_OUTPOST);
    }

    if( loc_cow > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_cow, CW_COW_X, CW_COW_Z, 0);
        cw_oploc2(srv, loc_cow, loc_slot);
        SELFTEST_CHECK(obj_cowbell <= 0 || cw_inv_has(player, obj_cowbell),
                       "Steal-cowbell should grant peng_cowbell");
        cw_pass("oploc2_cowbell_steal");
    }
    else if( obj_cowbell > 0 )
    {
        cw_give(player, obj_cowbell, 1);
    }

    if( npc_thing > 0 )
    {
        slot_thing = cw_fresh(srv, npc_thing, CW_THING_X, CW_THING_Z, 0);
        if( slot_thing >= 0 )
        {
            cw_talk_finish(srv, npc_thing, slot_thing);
            cw_pass("opnpc1_thing_outpost_info");
            cw_free_npc(srv, slot_thing);
        }
    }
    if( obj_tar > 0 )
        cw_give(player, obj_tar, 1);
    if( obj_feather > 0 )
        cw_give(player, obj_feather, 5);
    if( obj_report2 > 0 && !cw_inv_has(player, obj_report2) )
        cw_give(player, obj_report2, 1);

    slot_larry = cw_fresh_larry_zoo(srv, npc_larry_zoo);
    SELFTEST_CHECK(slot_larry >= 0, "Larry zoo should respawn for outpost handoff");
    cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    SELFTEST_CHECK(cw_quest(player) == CW_ICEBERG_KGP,
                   "outpost handoff should write iceberg_kgp, got %d",
                   cw_quest(player));
    cw_pass("opnpc1_larry_outpost_handoff");

    cw_vb(srv, "peng_transmog", 1);
    if( loc_aval > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_aval, CW_AVAL_X, CW_AVAL_Z, CW_AVAL_LV);
        cw_oploc(srv, loc_aval, loc_slot);
        cw_pass("oploc1_avalanche_enter");
    }

    if( npc_kgp > 0 )
    {
        slot_kgp = cw_fresh(srv, npc_kgp, CW_KGP_X, CW_KGP_Z, 0);
        if( slot_kgp >= 0 )
        {
            cw_talk_finish(srv, npc_kgp, slot_kgp);
            SELFTEST_CHECK(cw_quest(player) == CW_NOODLE1,
                           "first KGP talk should send you to Noodle, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_kgp_see_noodle");
            cw_free_npc(srv, slot_kgp);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_NOODLE1);
    }

    if( npc_noodle > 0 )
    {
        slot_noodle = cw_fresh(srv, npc_noodle, CW_NOODLE_X, CW_NOODLE_Z, CW_NOODLE_LV);
        if( slot_noodle >= 0 )
        {
            cw_talk_finish(srv, npc_noodle, slot_noodle);
            SELFTEST_CHECK(cw_quest(player) == CW_NOODLE2,
                           "Noodle should ask for tar and feathers, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_noodle_peace");
            if( obj_tar > 0 )
                cw_give(player, obj_tar, 1);
            if( obj_feather > 0 )
                cw_give(player, obj_feather, 5);
            cw_talk_finish(srv, npc_noodle, slot_noodle);
            SELFTEST_CHECK(cw_quest(player) == CW_KGP_AGAIN,
                           "Noodle handoff should write kgp_again, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_noodle_handoff");
            cw_free_npc(srv, slot_noodle);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_KGP_AGAIN);
        if( obj_id > 0 )
            cw_give(player, obj_id, 1);
    }

    if( npc_kgp > 0 )
    {
        if( obj_id > 0 && !cw_inv_has(player, obj_id) )
            cw_give(player, obj_id, 1);
        slot_kgp = cw_fresh(srv, npc_kgp, CW_KGP_X, CW_KGP_Z, 0);
        if( slot_kgp >= 0 )
        {
            cw_talk_finish(srv, npc_kgp, slot_kgp);
            SELFTEST_CHECK(cw_quest(player) == CW_DEBRIEF,
                           "showing the ID should write debrief, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_kgp_show_id");
            cw_free_npc(srv, slot_kgp);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_DEBRIEF);
    }

    if( loc_agi > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_agi, CW_AGI_DOOR_X, CW_AGI_DOOR_Z, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_agi, -1, loc_slot);
        cw_click_until_menu(srv, 8);
        cw_pick_row(srv, 2);
        cw_finish(srv);
        SELFTEST_CHECK(cw_quest(player) == CW_DEBRIEF,
                       "crush-course refuse must stay debrief");
        cw_pass("oploc1_agility_door_refuse");
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_agi, -1, loc_slot);
        cw_click_until_menu(srv, 8);
        cw_pick_row(srv, 1);
        cw_finish(srv);
        SELFTEST_CHECK(cw_quest(player) == CW_AGILITY_READY,
                       "attempting the course should write agility_ready, got %d",
                       cw_quest(player));
        cw_pass("oploc1_agility_door_attempt");
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_AGILITY_READY);
    }

    if( npc_instructor > 0 )
    {
        slot_instr = cw_fresh(srv, npc_instructor, CW_INSTR_X, CW_INSTR_Z, 0);
        if( slot_instr >= 0 )
        {
            cw_talk_finish(srv, npc_instructor, slot_instr);
            SELFTEST_CHECK(cw_quest(player) == CW_AGILITY_DONE,
                           "instructor should write agility_done, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_instructor_pass");
            cw_free_npc(srv, slot_instr);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_AGILITY_DONE);
    }

    if( npc_larry_ice > 0 )
    {
        slot_ice = cw_fresh(srv, npc_larry_ice, CW_ICE_X, CW_ICE_Z, CW_ICE_LV);
        if( slot_ice >= 0 )
        {
            cw_talk_finish(srv, npc_larry_ice, slot_ice);
            SELFTEST_CHECK(cw_quest(player) == CW_ARMY,
                           "crush-course report should write army_report, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_larry_ice_agility_report");
            cw_talk_finish(srv, npc_larry_ice, slot_ice);
            SELFTEST_CHECK(cw_quest(player) == CW_PINGPONG,
                           "army report should send you to Ping/Pong, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_larry_ice_army_report");
            cw_free_npc(srv, slot_ice);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_PINGPONG);
    }

    if( npc_kgp > 0 )
    {
        slot_kgp = cw_fresh(srv, npc_kgp, CW_KGP_X, CW_KGP_Z, 0);
        if( slot_kgp >= 0 )
        {
            cw_talk_finish(srv, npc_kgp, slot_kgp);
            SELFTEST_CHECK(cw_quest(player) == CW_INSTRUMENTS,
                           "KGP should point you at Ping/Pong, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_kgp_pingpong");
            cw_free_npc(srv, slot_kgp);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_INSTRUMENTS);
    }

    cw_vb(srv, "peng_transmog", 0);
    if( obj_mahog > 0 && obj_leather > 0 )
    {
        cw_give(player, obj_mahog, 1);
        cw_give(player, obj_leather, 1);
        cw_opheldu(srv, obj_mahog, obj_leather);
        SELFTEST_CHECK(obj_bongos <= 0 || cw_inv_has(player, obj_bongos),
                       "mahogany+leather should craft penguin bongos");
        cw_pass("opheldu_bongos_craft");
    }
    else if( obj_bongos > 0 )
    {
        cw_give(player, obj_bongos, 1);
    }
    if( obj_cowbell > 0 && !cw_inv_has(player, obj_cowbell) )
        cw_give(player, obj_cowbell, 1);

    if( npc_ping > 0 )
    {
        slot_ping = cw_fresh(srv, npc_ping, CW_PING_X, CW_PING_Z, 0);
        if( slot_ping >= 0 )
        {
            cw_talk_finish(srv, npc_ping, slot_ping);
            SELFTEST_CHECK(cw_quest(player) == CW_CONTROL,
                           "bongos hand-in should lure the guard, got %d",
                           cw_quest(player));
            SELFTEST_CHECK(cw_get_vb(player, "peng_multi_kgp") == CW_KGP_LURED,
                           "peng_multi_kgp should be lure_done");
            cw_pass("opnpc1_pingpong_handin");
            cw_free_npc(srv, slot_ping);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_CONTROL);
        cw_vb(srv, "peng_multi_kgp", CW_KGP_LURED);
    }

    if( loc_booth > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_booth, CW_BOOTH_X, CW_BOOTH_Z, 0);
        cw_oploc(srv, loc_booth, loc_slot);
        cw_pass("oploc1_booth_unlock");
    }
    if( loc_wardoor > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_wardoor, CW_BOOTH_X, CW_BOOTH_Z, 0);
        cw_oploc(srv, loc_wardoor, loc_slot);
        SELFTEST_CHECK(cw_quest(player) == CW_ICELORDS,
                       "war-room door should write icelords, got %d",
                       cw_quest(player));
        cw_pass("oploc1_warroom_reveal");
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_ICELORDS);
    }

    if( npc_icelord > 0 )
    {
        slot_icelord = cw_fresh(srv, npc_icelord, CW_ICELORD_X, CW_ICELORD_Z, 0);
        if( slot_icelord >= 0 )
        {
            cw_talk_finish(srv, npc_icelord, slot_icelord);
            SELFTEST_CHECK(cw_quest(player) == CW_ESCAPE,
                           "narrated icelord kill should write escape, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_icelord_narrated_kill");
            cw_free_npc(srv, slot_icelord);
        }
    }
    else
    {
        cw_vb(srv, "peng_quest", CW_ESCAPE);
    }

    if( loc_pendoor > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_pendoor, CW_PENDOOR_X, CW_PENDOOR_Z, 0);
        cw_oploc(srv, loc_pendoor, loc_slot);
        cw_pass("oploc1_pen_door_exit");
    }
    if( loc_chasm > 0 )
    {
        loc_slot = cw_place_loc(srv, loc_chasm, CW_CHASM_X, CW_CHASM_Z, 0);
        cw_oploc(srv, loc_chasm, loc_slot);
        cw_pass("oploc1_chasm_escape");
    }

    agi_before = (stat_agi >= 0) ? player->stat_xp_tenths[stat_agi] : 0;
    craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
    con_before = (stat_con >= 0) ? player->stat_xp_tenths[stat_con] : 0;
    attack_before = (stat_attack >= 0) ? player->stat_xp_tenths[stat_attack] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;

    if( npc_larry_ice > 0 )
    {
        slot_ice = cw_fresh(srv, npc_larry_ice, CW_ICE_X, CW_ICE_Z, CW_ICE_LV);
        if( slot_ice >= 0 )
        {
            cw_talk_finish(srv, npc_larry_ice, slot_ice);
            SELFTEST_CHECK(cw_quest(player) == CW_COMPLETE,
                           "escape report should complete the quest, got %d",
                           cw_quest(player));
            cw_pass("opnpc1_larry_ice_complete");
            cw_free_npc(srv, slot_ice);
        }
    }

    if( stat_agi >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_agi] >= agi_before + CW_REWARD_AGILITY_TENTHS,
                       "complete should award 5000 Agility XP (50000 tenths)");
    if( stat_craft >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >= craft_before + CW_REWARD_CRAFTING_TENTHS,
                       "complete should award 2000 Crafting XP (20000 tenths)");
    if( stat_con >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_con] >= con_before + CW_REWARD_CONSTRUCTION_TENTHS,
                       "complete should award 1500 Construction XP (15000 tenths)");
    if( stat_attack >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_attack] == attack_before,
                       "dbrow has no Attack row -- cache wins, do not award 40 Attack");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + CW_REWARD_QP,
                       "complete should award 1 quest point");
    cw_pass("complete_rewards");

    cw_journal(srv, "journal_135_complete");
    slot_larry = cw_fresh_larry_zoo(srv, npc_larry_zoo);
    if( slot_larry >= 0 )
        cw_talk_finish(srv, npc_larry_zoo, slot_larry);
    cw_pass("opnpc1_larry_already_complete");

    cw_free_npc(srv, slot_larry);
    cw_reset_quest(srv);
    cw_clear_inv(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_COLDWAR_SELFTEST_U_H */
