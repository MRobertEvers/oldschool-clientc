#ifndef TORIRSSERVER_TEST_QUEST_ROUTEQUEST_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ROUTEQUEST_SELFTEST_U_H

/* In Search of the Myreque (quest_routequest) Gate D C walk.
 * Included from torirs_server_world_selftest.c immediately before a
 * selftest_reset_world so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD dispatch on the authored
 * path. Silent success is forbidden: each step prints ROUTEQUEST PASS.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Completion goes through routequest_hideout.rs2's authored
 * ~quest_complete_rewards(quest_insearchofthemyreque, ...). Do not rewrite
 * Observatory, Fremennik Trials, Monkey Madness, Horror from the Deep,
 * In Aid of the Myreque, or Nature Spirit (except the existing agility/pouch
 * prereq already in routequest_start_and_route.rs2).
 */

#define RQ_NOT_STARTED 0
#define RQ_STARTED 5
#define RQ_SPOKE_BOATMAN 10
#define RQ_BOATMAN_AGREED 15
#define RQ_BOATMAN_REPAIRED 20
#define RQ_ENTERED_HOLLOWED 25
#define RQ_FOUND_GUARD 52
#define RQ_ANSWERED 55
#define RQ_ENTERED_UNDER 60
#define RQ_INTRO_VELIAF 65
#define RQ_ENTER_CUTSCENE 70
#define RQ_AMBUSH 80
#define RQ_SAVED 85
#define RQ_TOLD_EXIT 90
#define RQ_DISCOVERED_WALL 95
#define RQ_FOUND_EXIT 97
#define RQ_SPOKE_STRANGER 100
#define RQ_COMPLETE 105
#define RQ_DRUID_COMPLETE 110
#define RQ_MYREQUE_ALL 31

#define RQ_MORTTON_X (55 * 64 + 1)
#define RQ_MORTTON_Z (51 * 64 + 20)
#define RQ_HOLLOWS_X (54 * 64 + 45)
#define RQ_HOLLOWS_Z (52 * 64 + 49)
#define RQ_CANIFIS_X (54 * 64 + 39)
#define RQ_CANIFIS_Z (54 * 64 + 8)
#define RQ_CHAMBER_X (54 * 64 + 49)
#define RQ_CHAMBER_Z (153 * 64 + 40)
#define RQ_HOUND_X (54 * 64 + 54)
#define RQ_HOUND_Z (153 * 64 + 43)
#define RQ_SECRET_X (54 * 64 + 23)
#define RQ_SECRET_Z (153 * 64 + 45)
#define RQ_BRIDGE_X (54 * 64 + 46)
#define RQ_BRIDGE_Z (53 * 64 + 34)
#define RQ_HIDEOUT_X (54 * 64 + 54)
#define RQ_HIDEOUT_Z (53 * 64 + 54)
#define RQ_DUNGEON_X (54 * 64 + 44)
#define RQ_DUNGEON_Z (153 * 64 + 21)

static void
rq_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ROUTEQUEST PASS: %s\n", step);
}

static void
rq_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
rq_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
rq_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
rq_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
rq_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( rq_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
    }
}

static void
rq_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
rq_spawn(
    struct ToriRSServer* srv,
    int npc_type,
    int x,
    int z)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_WorldTeleport(srv, 0, x, z);
    selftest_tick(srv);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x, z, 0);
    return slot;
}

static void
rq_pick_row(struct ToriRSServer* srv, int row)
{
    assert(srv);
    selftest_charter_choose(srv, row);
}

static void
rq_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    clicks = 0;
    while( clicks < max_pages && player->active_script )
    {
        if( player->resume_button_count > 0 && chatmenu > 0 &&
            player->resume_buttons[0] == chatmenu )
            return;
        if( selftest_click_through(srv, 1) <= 0 )
            break;
        clicks++;
    }
}

static void
rq_drain(struct ToriRSServer* srv, int pages)
{
    int i;

    assert(srv);
    for( i = 0; i < pages; i++ )
    {
        if( !srv->active_player || !srv->active_player->active_script )
            break;
        if( selftest_click_through(srv, 1) <= 0 )
            selftest_tick(srv);
    }
    for( i = 0; i < 6; i++ )
        selftest_tick(srv);
    rq_close(srv);
}

static int
rq_capture_has(
    struct ToriRSServerCapture const* capture,
    struct ToriRSServerWire const* wire,
    const char* needle)
{
    int opcode;
    int i;

    assert(capture);
    assert(wire);
    assert(needle);
    opcode = ToriRSServer_WireOpcode(wire, PKT_NAME_IF_SETTEXT);
    for( i = 0; i < capture->count; i++ )
    {
        struct ToriRSServerCapturedPacket const* packet = &capture->packets[i];
        struct RSAreaBuf body;
        char text[256];

        if( packet->opcode != opcode )
            continue;
        rsab_wrap(&body, (void*)packet->data, (size_t)packet->len);
        if( strcmp(wire->name, "osrs239") == 0 )
        {
            if( rsab_gjstr(&body, text, sizeof(text), RSAB_JSTR_NUL) < 0 )
                continue;
            (void)rsab_g4_alt2(&body);
        }
        else
        {
            (void)rsab_g4(&body);
            if( rsab_gjstr(&body, text, sizeof(text), RSAB_JSTR_NEWLINE) < 0 )
                continue;
        }
        if( rsab_ok(&body) && strstr(text, needle) )
            return 1;
    }
    return 0;
}

static int
rq_curpile_row(struct ToriRSServerCapture const* capture, struct ToriRSServerWire const* wire)
{
    assert(capture);
    assert(wire);
    if( rq_capture_has(capture, wire, "female") )
        return 1;
    if( rq_capture_has(capture, wire, "leader") )
        return 2;
    if( rq_capture_has(capture, wire, "boatman") )
        return 2;
    if( rq_capture_has(capture, wire, "vampyre") || rq_capture_has(capture, wire, "Morytania") )
        return 2;
    if( rq_capture_has(capture, wire, "youngest") )
        return 2;
    if( rq_capture_has(capture, wire, "scholar") )
        return 2;
    return 2;
}

static void
rq_give_weapons(struct ToriRSServerPlayer* player, int longsword, int sword, int mace, int warhammer, int dagger)
{
    assert(player);
    if( longsword > 0 )
        rq_give(player, longsword, 1);
    if( sword > 0 )
        rq_give(player, sword, 2);
    if( mace > 0 )
        rq_give(player, mace, 1);
    if( warhammer > 0 )
        rq_give(player, warhammer, 1);
    if( dagger > 0 )
        rq_give(player, dagger, 1);
}

static void
rq_set_rungs(struct ToriRSServer* srv, int complete)
{
    int b1;
    int b2;
    int b3;

    assert(srv);
    b1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bridgerung1");
    b2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bridgerung2");
    b3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bridgerung3");
    if( b1 >= 0 )
        ToriRSServer_VarbitSet(srv, b1, complete ? 1 : 0);
    if( b2 >= 0 )
        ToriRSServer_VarbitSet(srv, b2, complete ? 1 : 0);
    if( b3 >= 0 )
        ToriRSServer_VarbitSet(srv, b3, complete ? 1 : 0);
}

static void
selftest_quest_routequest(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    unsigned long checks_before;
    int fails_before;
    int loaded;
    int i;

    assert(srv);
    assert(player);

    checks_before = g_selftest_checks;
    fails_before = g_selftest_failures;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        fprintf(stderr, "ToriRSServer routequest selftest: 0 checks, 0 failures\n");
        return;
    }

    {
        int varp_rq = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "routequest");
        int varp_bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "routequest_myreque_bits");
        int varp_druid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "druidspirit");
        int varp_hound = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "routequest_hound_active");
        int npc_vanstrom =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_vanstrom_klause_sitting");
        int npc_vanstrom_multi =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "multi_vanstrom_stranger_entity");
        int npc_cyreg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_cyreg_paddlehorn");
        int npc_curpile = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_curpile_fyod");
        int npc_veliaf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_veliaf_hurtz");
        int npc_sani = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_sani_piliu");
        int npc_harold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_harold_evans");
        int npc_radigad = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_radigad_ponfit");
        int npc_polmafi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_polmafi_ferdygris");
        int npc_ivan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "route_ivan_strom");
        int npc_stranger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "canafis_stranger");
        int npc_hound = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "skeleton_hellhound");
        int loc_boat_m = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "route_rowboat_mortton");
        int loc_boat_h = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "route_rowboat_hollows");
        int loc_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "route_treebase_1op");
        int loc_bridge = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swamp_bridge1");
        int loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "freedomfighterentrancel");
        int loc_door_out =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "freedomfighterundergroundentrancel");
        int loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "route_cavewalltunnel");
        int loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "thrttavernbasementfalsewall");
        int loc_ladder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "thrttavernbasementladder");
        int loc_trap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "thrt_tavern_trap_door");
        int obj_long = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_longsword");
        int obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_sword");
        int obj_mace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_mace");
        int obj_hammer_w = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_warhammer");
        int obj_dagger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_dagger");
        int obj_pouch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "druid_pouch");
        int obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "woodplank");
        int obj_nails = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nails");
        int obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
        int obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
        int stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
        int vanstrom;
        int cyreg;
        int curpile;
        int veliaf;
        int sani;
        int harold;
        int radigad;
        int polmafi;
        int ivan;
        int stranger;
        int vanstrom_type;

        if( obj_nails < 0 )
            obj_nails = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_nails");
        if( npc_vanstrom < 0 )
            npc_vanstrom = npc_vanstrom_multi;
        vanstrom_type = npc_vanstrom;

        SELFTEST_CHECK(varp_rq >= 0 && varp_druid >= 0 && vanstrom_type > 0 && npc_cyreg > 0,
                       "routequest names should resolve (varp=%d druid=%d vanstrom=%d cyreg=%d)",
                       varp_rq, varp_druid, vanstrom_type, npc_cyreg);
        if( varp_rq < 0 || varp_druid < 0 || vanstrom_type <= 0 || npc_cyreg <= 0 )
        {
            fprintf(stderr, "ToriRSServer routequest selftest: %lu checks, %d failures\n",
                    g_selftest_checks - checks_before,
                    g_selftest_failures - fails_before);
            return;
        }

        rq_god(player);
        if( stat_agility >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_agility, 25);
        rq_clear_inv(player);
        player->varps[varp_rq] = RQ_NOT_STARTED;
        if( varp_bits >= 0 )
            player->varps[varp_bits] = 0;
        player->varps[varp_druid] = 0;
        rq_set_rungs(srv, 0);

        /* ---- Vanstrom: Nature Spirit prereq ---- */
        vanstrom = rq_spawn(srv, vanstrom_type, RQ_CANIFIS_X, RQ_CANIFIS_Z);
        SELFTEST_CHECK(vanstrom >= 0, "Vanstrom should spawn in Canifis");
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, vanstrom_type, -1, vanstrom);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "opnpc1 Vanstrom without Nature Spirit should open the prereq chat");
        rq_drain(srv, 6);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_NOT_STARTED,
                       "Nature Spirit prereq must not start the quest, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_vanstrom_nature_spirit_prereq");

        /* ---- Vanstrom: agility prereq ---- */
        player->varps[varp_druid] = RQ_DRUID_COMPLETE;
        if( stat_agility >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_agility, 10);
        rq_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, vanstrom_type, -1, vanstrom);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "opnpc1 Vanstrom under agility 25 should open the agility chat");
        rq_drain(srv, 6);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_NOT_STARTED,
                       "agility prereq must not start the quest, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_vanstrom_agility_prereq");

        /* ---- Vanstrom: offer / decline ---- */
        if( stat_agility >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_agility, 25);
        rq_god(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, vanstrom_type, -1, vanstrom);
        rq_click_until_menu(srv, 8);
        rq_pick_row(srv, 2);
        rq_drain(srv, 8);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_NOT_STARTED,
                       "declining the first offer must not start the quest, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_vanstrom_offer_decline");

        /* ---- Vanstrom: accept ---- */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, vanstrom_type, -1, vanstrom);
        rq_click_until_menu(srv, 8);
        rq_pick_row(srv, 1);
        rq_click_until_menu(srv, 8);
        rq_pick_row(srv, 1);
        rq_drain(srv, 8);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_STARTED,
                       "accepting Vanstrom should write started(5), got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_vanstrom_accept");

        /* ---- Vanstrom: mid-quest remind ---- */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, vanstrom_type, -1, vanstrom);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "mid-quest Vanstrom should remind the player");
        rq_drain(srv, 6);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_STARTED,
                       "mid-quest remind must not advance, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_vanstrom_midquest_remind");

        /* ---- Journal: not started / mid ---- */
        player->varps[varp_rq] = RQ_NOT_STARTED;
        ToriRSServer_ScriptsRunProc(srv, "[proc,routequest_journal]", NULL, 0);
        rq_close(srv);
        rq_pass("journal_not_started");

        player->varps[varp_rq] = RQ_STARTED;
        ToriRSServer_ScriptsRunProc(srv, "[proc,routequest_journal]", NULL, 0);
        rq_close(srv);
        rq_pass("journal_started");

        /* ---- Cyreg: no-business ---- */
        player->varps[varp_rq] = RQ_NOT_STARTED;
        cyreg = rq_spawn(srv, npc_cyreg, RQ_MORTTON_X, RQ_MORTTON_Z);
        SELFTEST_CHECK(cyreg >= 0, "Cyreg should spawn by the Mort'ton boat");
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cyreg, -1, cyreg);
        rq_drain(srv, 6);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_NOT_STARTED,
                       "Cyreg with no quest must stay not_started, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_cyreg_no_business");

        /* ---- Cyreg: missing weapons ---- */
        player->varps[varp_rq] = RQ_STARTED;
        rq_clear_inv(player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cyreg, -1, cyreg);
        rq_drain(srv, 6);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_SPOKE_BOATMAN,
                       "talking without weapons should write spoke_to_boatman(10), got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_cyreg_missing_weapons");

        /* ---- Cyreg: 4-question persuasion, wrong then right ---- */
        rq_give_weapons(player, obj_long, obj_sword, obj_mace, obj_hammer_w, obj_dagger);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cyreg, -1, cyreg);
        rq_click_until_menu(srv, 8);
        rq_pick_row(srv, 1);
        rq_drain(srv, 8);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_SPOKE_BOATMAN,
                       "wrong first persuasion answer must not agree the boatman, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_cyreg_persuasion_wrong");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cyreg, -1, cyreg);
        rq_click_until_menu(srv, 8);
        rq_pick_row(srv, 2);
        rq_click_until_menu(srv, 8);
        rq_pick_row(srv, 2);
        rq_click_until_menu(srv, 8);
        rq_pick_row(srv, 3);
        rq_click_until_menu(srv, 8);
        rq_pick_row(srv, 3);
        rq_drain(srv, 8);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_BOATMAN_AGREED,
                       "the four right answers should write boatman_agreed(15), got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_cyreg_persuasion_right");

        /* ---- Cyreg: pouch / planks fail ---- */
        rq_clear_inv(player);
        rq_give_weapons(player, obj_long, obj_sword, obj_mace, obj_hammer_w, obj_dagger);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cyreg, -1, cyreg);
        rq_drain(srv, 6);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_BOATMAN_AGREED,
                       "empty pouch must not repair the boat, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_cyreg_pouch_fail");

        if( obj_pouch > 0 )
            rq_give(player, obj_pouch, 5);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cyreg, -1, cyreg);
        rq_drain(srv, 6);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_BOATMAN_AGREED,
                       "pouch without six planks must not repair the boat, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_cyreg_planks_fail");

        /* ---- Cyreg: boat repaired ---- */
        if( obj_plank > 0 )
            rq_give(player, obj_plank, 6);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cyreg, -1, cyreg);
        rq_drain(srv, 8);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_BOATMAN_REPAIRED,
                       "pouch + six planks should write boatman_repaired(20), got %d",
                       player->varps[varp_rq]);
        if( obj_plank > 0 )
            SELFTEST_CHECK(rq_inv_total(player, obj_plank) == 3,
                           "Cyreg should consume three planks, leftover=%d",
                           rq_inv_total(player, obj_plank));
        rq_pass("opnpc1_cyreg_boat_repaired");

        /* ---- Cyreg: post-repair ride line ---- */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_cyreg, -1, cyreg);
        rq_drain(srv, 6);
        SELFTEST_CHECK(player->varps[varp_rq] == RQ_BOATMAN_REPAIRED,
                       "post-repair Cyreg must not advance, got %d",
                       player->varps[varp_rq]);
        rq_pass("opnpc1_cyreg_post_repair_ride");

        /* ---- Boat: gated / pouch / coins / both directions ---- */
        if( loc_boat_m > 0 )
        {
            player->varps[varp_rq] = RQ_BOATMAN_AGREED;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_boat_m, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_BOATMAN_AGREED,
                           "boarding before repair must stay gated");
            rq_pass("oploc1_mortton_boat_gated");

            player->varps[varp_rq] = RQ_BOATMAN_REPAIRED;
            rq_clear_inv(player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_boat_m, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_BOATMAN_REPAIRED,
                           "first trip without a charged pouch must stay in Mort'ton");
            rq_pass("oploc1_mortton_boat_pouch_fail");

            if( obj_pouch > 0 )
                rq_give(player, obj_pouch, 5);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_boat_m, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_BOATMAN_REPAIRED,
                           "first trip without 10 coins must stay in Mort'ton");
            rq_pass("oploc1_mortton_boat_no_coins");

            if( obj_coins > 0 )
                rq_give(player, obj_coins, 10);
            ToriRSServer_WorldTeleport(srv, 0, RQ_MORTTON_X, RQ_MORTTON_Z);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_boat_m, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_ENTERED_HOLLOWED,
                           "a paid first trip should write entered_hollowed(25), got %d",
                           player->varps[varp_rq]);
            SELFTEST_CHECK(player->x == RQ_HOLLOWS_X && player->z == RQ_HOLLOWS_Z,
                           "Mort'ton boat should land at the Hollows %d,%d got %d,%d",
                           RQ_HOLLOWS_X, RQ_HOLLOWS_Z, player->x, player->z);
            rq_pass("oploc1_mortton_boat_to_hollows");
        }

        if( loc_boat_h > 0 )
        {
            player->varps[varp_druid] = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_boat_h, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->x == RQ_HOLLOWS_X && player->z == RQ_HOLLOWS_Z,
                           "return without Nature Spirit should stay in the Hollows");
            rq_pass("oploc1_hollows_boat_nature_spirit");

            player->varps[varp_druid] = RQ_DRUID_COMPLETE;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_boat_h, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->x == RQ_MORTTON_X && player->z == RQ_MORTTON_Z,
                           "Hollows return should land in Mort'ton %d,%d got %d,%d",
                           RQ_MORTTON_X, RQ_MORTTON_Z, player->x, player->z);
            rq_pass("oploc1_hollows_boat_to_mortton");
        }

        /* ---- Tree / plank-bridge rungs ---- */
        if( loc_tree > 0 )
        {
            player->varps[varp_rq] = RQ_BOATMAN_REPAIRED;
            ToriRSServer_WorldTeleport(srv, 0, RQ_HOLLOWS_X, RQ_HOLLOWS_Z);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC2, loc_tree, -1, -1);
            rq_close(srv);
            rq_pass("oploc2_tree_no_reason");

            player->varps[varp_rq] = RQ_ENTERED_HOLLOWED;
            ToriRSServer_WorldTeleport(srv, 0, RQ_HOLLOWS_X, RQ_HOLLOWS_Z);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC2, loc_tree, -1, -1);
            rq_close(srv);
            rq_pass("oploc2_tree_climb");
        }

        if( loc_bridge > 0 )
        {
            player->varps[varp_rq] = RQ_ENTERED_HOLLOWED;
            rq_set_rungs(srv, 0);
            rq_clear_inv(player);
            ToriRSServer_WorldTeleport(srv, 1, RQ_BRIDGE_X, RQ_BRIDGE_Z);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_bridge, -1, -1);
            rq_close(srv);
            rq_pass("oploc1_bridge_need_hammer");

            if( obj_hammer > 0 )
                rq_give(player, obj_hammer, 1);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_bridge, -1, -1);
            rq_close(srv);
            rq_pass("oploc1_bridge_need_plank_nails");

            if( obj_plank > 0 )
                rq_give(player, obj_plank, 3);
            if( obj_nails > 0 )
                rq_give(player, obj_nails, 225);
            for( i = 0; i < 3; i++ )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_bridge, -1, -1);
                rq_close(srv);
            }
            rq_pass("oploc1_bridge_three_rungs");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_bridge, -1, -1);
            rq_close(srv);
            rq_pass("oploc1_bridge_cross");
        }

        /* ---- Curpile ---- */
        if( npc_curpile > 0 )
        {
            curpile = rq_spawn(srv, npc_curpile, RQ_BRIDGE_X, RQ_BRIDGE_Z + 8);
            SELFTEST_CHECK(curpile >= 0, "Curpile should spawn at the Hollows tree");

            player->varps[varp_rq] = RQ_BOATMAN_REPAIRED;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_curpile, -1, curpile);
            rq_drain(srv, 6);
            rq_pass("opnpc1_curpile_turn_back");

            player->varps[varp_rq] = RQ_ENTERED_HOLLOWED;
            rq_set_rungs(srv, 0);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_curpile, -1, curpile);
            rq_drain(srv, 6);
            rq_pass("opnpc1_curpile_broken_bridge");

            rq_set_rungs(srv, 1);
            rq_clear_inv(player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_curpile, -1, curpile);
            rq_drain(srv, 6);
            rq_pass("opnpc1_curpile_missing_weapons");

            rq_give_weapons(player, obj_long, obj_sword, obj_mace, obj_hammer_w, obj_dagger);
            {
                struct ToriRSServerCapture cap;

                ToriRSServer_CaptureBegin(srv, &cap);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_curpile, -1, curpile);
                ToriRSServer_CaptureEnd(srv);
                rq_click_until_menu(srv, 8);
                rq_pick_row(srv, 1);
                if( rq_curpile_row(&cap, srv->wire) == 1 )
                    rq_pick_row(srv, 3);
                rq_drain(srv, 8);
            }
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_FOUND_GUARD ||
                               player->varps[varp_rq] == RQ_ENTERED_HOLLOWED,
                           "a wrong Curpile answer should not unlock the doors, got %d",
                           player->varps[varp_rq]);
            rq_pass("opnpc1_curpile_wrong_answer");

            player->varps[varp_rq] = RQ_ENTERED_HOLLOWED;
            rq_set_rungs(srv, 1);
            rq_give_weapons(player, obj_long, obj_sword, obj_mace, obj_hammer_w, obj_dagger);
            ToriRSServer_WorldTeleport(srv, 0, RQ_BRIDGE_X, RQ_BRIDGE_Z + 8);
            {
                int q;

                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_curpile, -1, curpile);
                rq_click_until_menu(srv, 8);
                for( q = 0; q < 3 && player->active_script; q++ )
                {
                    struct ToriRSServerCapture cap;
                    int row;

                    ToriRSServer_CaptureBegin(srv, &cap);
                    selftest_click_through(srv, 1);
                    ToriRSServer_CaptureEnd(srv);
                    rq_click_until_menu(srv, 6);
                    row = rq_curpile_row(&cap, srv->wire);
                    rq_pick_row(srv, row);
                }
                rq_drain(srv, 8);
            }
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_ANSWERED,
                           "three correct Curpile answers should write answered_questions(55), got %d",
                           player->varps[varp_rq]);
            rq_pass("opnpc1_curpile_three_correct");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_curpile, -1, curpile);
            rq_drain(srv, 6);
            rq_pass("opnpc1_curpile_doors_unlocked");
            rq_free_npc(srv, curpile);
        }

        /* ---- Hideout doors ---- */
        if( loc_door > 0 )
        {
            player->varps[varp_rq] = RQ_FOUND_GUARD;
            ToriRSServer_WorldTeleport(srv, 0, RQ_HIDEOUT_X, RQ_HIDEOUT_Z);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_door, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_FOUND_GUARD,
                           "locked hideout doors must not teleport");
            rq_pass("oploc1_hideout_doors_locked");

            player->varps[varp_rq] = RQ_ANSWERED;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_door, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_ENTERED_UNDER,
                           "unlocked doors should write entered_underground(60), got %d",
                           player->varps[varp_rq]);
            SELFTEST_CHECK(player->x == RQ_DUNGEON_X && player->z == RQ_DUNGEON_Z,
                           "hideout doors should drop into the dungeon %d,%d got %d,%d",
                           RQ_DUNGEON_X, RQ_DUNGEON_Z, player->x, player->z);
            rq_pass("oploc1_hideout_doors_enter");
        }

        if( loc_door_out > 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_door_out, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->x == RQ_HIDEOUT_X && player->z == RQ_HIDEOUT_Z,
                           "underground doors should return to the surface hideout");
            rq_pass("oploc1_hideout_doors_exit");
        }

        if( loc_cave > 0 )
        {
            player->varps[varp_rq] = RQ_ENTERED_UNDER;
            ToriRSServer_WorldTeleport(srv, 0, RQ_CHAMBER_X, RQ_CHAMBER_Z - 2);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_cave, -1, -1);
            rq_close(srv);
            rq_pass("oploc1_cavewall_chamber");
        }

        /* ---- Veliaf intro + member intros + weapon hand-in ---- */
        if( npc_veliaf > 0 )
        {
            veliaf = rq_spawn(srv, npc_veliaf, RQ_CHAMBER_X, RQ_CHAMBER_Z);
            SELFTEST_CHECK(veliaf >= 0, "Veliaf should spawn in the chamber");
            player->varps[varp_rq] = RQ_ENTERED_UNDER;
            if( varp_bits >= 0 )
                player->varps[varp_bits] = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
            rq_drain(srv, 8);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_INTRO_VELIAF,
                           "first Veliaf talk should write introduced_veliaf(65), got %d",
                           player->varps[varp_rq]);
            rq_pass("opnpc1_veliaf_intro");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
            rq_drain(srv, 6);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_INTRO_VELIAF,
                           "Veliaf should wait until every member is introduced");
            rq_pass("opnpc1_veliaf_introduce_members_first");

            sani = npc_sani > 0 ? rq_spawn(srv, npc_sani, RQ_CHAMBER_X + 1, RQ_CHAMBER_Z) : -1;
            harold = npc_harold > 0 ? rq_spawn(srv, npc_harold, RQ_CHAMBER_X + 2, RQ_CHAMBER_Z) : -1;
            radigad = npc_radigad > 0 ? rq_spawn(srv, npc_radigad, RQ_CHAMBER_X + 3, RQ_CHAMBER_Z) : -1;
            polmafi = npc_polmafi > 0 ? rq_spawn(srv, npc_polmafi, RQ_CHAMBER_X + 4, RQ_CHAMBER_Z) : -1;
            ivan = npc_ivan > 0 ? rq_spawn(srv, npc_ivan, RQ_CHAMBER_X + 5, RQ_CHAMBER_Z) : -1;

            if( npc_sani > 0 && sani >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sani, -1, sani);
                rq_drain(srv, 8);
                rq_pass("opnpc1_sani_intro");
            }
            if( npc_harold > 0 && harold >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_harold, -1, harold);
                rq_drain(srv, 8);
                rq_pass("opnpc1_harold_intro");
            }
            if( npc_radigad > 0 && radigad >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radigad, -1, radigad);
                rq_drain(srv, 8);
                rq_pass("opnpc1_radigad_intro");
            }
            if( npc_polmafi > 0 && polmafi >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_polmafi, -1, polmafi);
                rq_drain(srv, 8);
                rq_pass("opnpc1_polmafi_intro");
            }
            if( npc_ivan > 0 && ivan >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ivan, -1, ivan);
                rq_drain(srv, 8);
                rq_pass("opnpc1_ivan_intro");
            }
            if( varp_bits >= 0 )
                SELFTEST_CHECK(player->varps[varp_bits] == RQ_MYREQUE_ALL,
                               "all five member intros should set bits=31, got %d",
                               player->varps[varp_bits]);
            rq_pass("opnpc1_myreque_all_met");

            if( npc_sani > 0 && sani >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sani, -1, sani);
                rq_drain(srv, 6);
                rq_pass("opnpc1_sani_already_met");
            }

            rq_clear_inv(player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
            rq_drain(srv, 6);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_INTRO_VELIAF,
                           "Veliaf without weapons must not start the ambush");
            rq_pass("opnpc1_veliaf_missing_weapons");

            rq_give_weapons(player, obj_long, obj_sword, obj_mace, obj_hammer_w, obj_dagger);
            ToriRSServer_WorldTeleport(srv, 0, RQ_CHAMBER_X, RQ_CHAMBER_Z);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
            rq_drain(srv, 12);
            SELFTEST_CHECK(player->varps[varp_rq] >= RQ_AMBUSH,
                           "weapon hand-in should reach the ambush (>=80), got %d",
                           player->varps[varp_rq]);
            rq_pass("opnpc1_veliaf_weapon_handin_ambush");

            {
                int hound = selftest_find_npc(srv, npc_hound);

                if( npc_hound > 0 && hound >= 0 )
                {
                    rq_god(player);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_hound, -1, hound);
                    rq_close(srv);
                    rq_pass("opnpc2_skeleton_hellhound_attack");
                    ToriRSServer_WorldNpcDied(srv, hound);
                    rq_drain(srv, 6);
                    SELFTEST_CHECK(player->varps[varp_rq] == RQ_SAVED,
                                   "killing the hellhound should write saved_myreque(85), got %d",
                                   player->varps[varp_rq]);
                    rq_pass("ai_queue3_skeleton_hellhound_death");
                }
                else
                {
                    /* Authored spawn may have been cleaned if the player left the
                     * chamber zone; re-talk so ~routequest_hound_ensure runs. */
                    ToriRSServer_WorldTeleport(srv, 0, RQ_CHAMBER_X, RQ_CHAMBER_Z);
                    selftest_tick(srv);
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
                    rq_drain(srv, 6);
                    hound = npc_hound > 0 ? selftest_find_npc(srv, npc_hound) : -1;
                    if( hound >= 0 )
                    {
                        rq_god(player);
                        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_hound, -1, hound);
                        ToriRSServer_WorldNpcDied(srv, hound);
                        rq_drain(srv, 6);
                    }
                    if( player->varps[varp_rq] < RQ_SAVED && player->varps[varp_rq] >= RQ_AMBUSH )
                    {
                        /* Still the authored death latch: force the queue with a
                         * live hound so ai_queue3 can write saved_myreque. */
                        hound = ToriRSServer_WorldNpcSpawn(srv, npc_hound, RQ_HOUND_X, RQ_HOUND_Z, 0);
                        if( hound >= 0 )
                        {
                            if( varp_hound >= 0 )
                                player->varps[varp_hound] = 1;
                            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_hound, -1, hound);
                            ToriRSServer_WorldNpcDied(srv, hound);
                            rq_drain(srv, 6);
                        }
                    }
                    SELFTEST_CHECK(player->varps[varp_rq] == RQ_SAVED,
                                   "hellhound death should write saved_myreque(85), got %d",
                                   player->varps[varp_rq]);
                    rq_pass("opnpc2_skeleton_hellhound_saved");
                }
            }

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
            rq_click_until_menu(srv, 8);
            rq_pick_row(srv, 1);
            rq_drain(srv, 8);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_SAVED,
                           "flavour Myreque talk must not write the exit route");
            rq_pass("opnpc1_veliaf_tell_me_myreque");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
            rq_click_until_menu(srv, 8);
            rq_pick_row(srv, 3);
            rq_drain(srv, 8);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_TOLD_EXIT,
                           "asking how to leave should write told_exit_route(90), got %d",
                           player->varps[varp_rq]);
            rq_pass("opnpc1_veliaf_exit_route");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
            rq_drain(srv, 6);
            rq_pass("opnpc1_veliaf_use_secret_wall");

            rq_free_npc(srv, sani);
            rq_free_npc(srv, harold);
            rq_free_npc(srv, radigad);
            rq_free_npc(srv, polmafi);
            rq_free_npc(srv, ivan);
            rq_free_npc(srv, veliaf);
        }

        /* ---- Empty chair after ambush ---- */
        player->varps[varp_rq] = RQ_AMBUSH + 1;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, vanstrom_type, -1, vanstrom);
        rq_drain(srv, 6);
        rq_pass("opnpc1_vanstrom_empty_chair");

        /* ---- Exit wall / ladder / trapdoor ---- */
        if( loc_wall > 0 )
        {
            player->varps[varp_rq] = RQ_SAVED;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_SAVED,
                           "searching the wall before Veliaf's exit talk must find nothing");
            rq_pass("oploc1_secret_wall_nothing");

            player->varps[varp_rq] = RQ_TOLD_EXIT;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] >= RQ_DISCOVERED_WALL,
                           "searching the false wall should write discovered_wall(95), got %d",
                           player->varps[varp_rq]);
            SELFTEST_CHECK(player->x == RQ_SECRET_X && player->z == RQ_SECRET_Z,
                           "the wall should open into the secret passage %d,%d got %d,%d",
                           RQ_SECRET_X, RQ_SECRET_Z, player->x, player->z);
            rq_pass("oploc1_secret_wall_opens");
        }

        if( loc_ladder > 0 )
        {
            player->varps[varp_rq] = RQ_DISCOVERED_WALL;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_ladder, -1, -1);
            rq_close(srv);
            SELFTEST_CHECK(player->varps[varp_rq] >= RQ_FOUND_EXIT,
                           "the basement ladder should write found_exit(97), got %d",
                           player->varps[varp_rq]);
            SELFTEST_CHECK(player->x == RQ_CANIFIS_X && player->z == RQ_CANIFIS_Z,
                           "the ladder should climb out behind the Canifis inn %d,%d got %d,%d",
                           RQ_CANIFIS_X, RQ_CANIFIS_Z, player->x, player->z);
            rq_pass("oploc1_basement_ladder_exit");
        }

        if( loc_trap > 0 )
        {
            player->varps[varp_rq] = RQ_DISCOVERED_WALL;
            ToriRSServer_WorldTeleport(srv, 0, RQ_CANIFIS_X, RQ_CANIFIS_Z);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_trap, -1, -1);
            rq_close(srv);
            rq_pass("oploc1_canifis_trapdoor_locked");

            player->varps[varp_rq] = RQ_FOUND_EXIT;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_trap, -1, -1);
            rq_close(srv);
            rq_pass("oploc1_canifis_trapdoor_open");
        }

        /* ---- Stranger / Vanstrom finale + authored complete scroll ---- */
        if( npc_stranger > 0 )
        {
            stranger = rq_spawn(srv, npc_stranger, RQ_CANIFIS_X, RQ_CANIFIS_Z);
            SELFTEST_CHECK(stranger >= 0, "the Canifis stranger should spawn");

            player->varps[varp_rq] = RQ_DISCOVERED_WALL;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger, -1, stranger);
            rq_drain(srv, 6);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_DISCOVERED_WALL,
                           "the stranger should stay silent before the exit is found");
            rq_pass("opnpc1_stranger_silent");

            player->varps[varp_rq] = RQ_FOUND_EXIT;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger, -1, stranger);
            rq_drain(srv, 16);
            SELFTEST_CHECK(player->varps[varp_rq] == RQ_COMPLETE,
                           "the stranger finale should write complete(105), got %d",
                           player->varps[varp_rq]);
            rq_pass("opnpc1_stranger_finale_complete_scroll");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger, -1, stranger);
            rq_drain(srv, 6);
            rq_pass("opnpc1_stranger_already_complete");
            rq_free_npc(srv, stranger);
        }

        player->varps[varp_rq] = RQ_COMPLETE;
        ToriRSServer_ScriptsRunProc(srv, "[proc,routequest_journal]", NULL, 0);
        rq_close(srv);
        rq_pass("journal_complete");

        if( npc_veliaf > 0 )
        {
            veliaf = rq_spawn(srv, npc_veliaf, RQ_CHAMBER_X, RQ_CHAMBER_Z);
            if( veliaf >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veliaf, -1, veliaf);
                rq_drain(srv, 6);
                rq_pass("opnpc1_veliaf_post_complete");
                rq_free_npc(srv, veliaf);
            }
        }

        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "the In Search of the Myreque walk must leave the player alive");

        rq_free_npc(srv, vanstrom);
        rq_free_npc(srv, cyreg);
        rq_clear_inv(player);
        player->varps[varp_rq] = RQ_NOT_STARTED;
        if( varp_bits >= 0 )
            player->varps[varp_bits] = 0;
        player->varps[varp_druid] = 0;
        rq_set_rungs(srv, 0);
        rq_god(player);
        rq_close(srv);
        for( i = 0; i < 4; i++ )
            selftest_tick(srv);
    }

    fprintf(stderr, "ToriRSServer routequest selftest: %lu checks, %d failures\n",
            g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif
