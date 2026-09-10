#ifndef TORIRSSERVER_TEST_QUEST_MM_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_MM_SELFTEST_U_H

/* Monkey Madness I Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before a selftest_reset_world
 * so spawned npcs cannot leak. Real OPNPC / OPLOC / OPHELD / OPNPCU /
 * OPLOCU on the critical path. player->godmode = 1 for the whole walk
 * (not a death test). Completion goes through authored
 * [queue,mm_quest_complete] / ~quest_complete_rewards. Do not rewrite
 * Grand Tree except the existing @mm_narnode hook. */

#define MM_NOT_STARTED 0
#define MM_STARTED 1
#define MM_SHOWN_SEAL 2
#define MM_ARRIVED_ATOLL 3
#define MM_COMPLETED_CH2 4
#define MM_COMPLETED_CH3 5
#define MM_DEFEATED_DEMON 6
#define MM_REPORTED_NARNODE 7
#define MM_RECEIVED_REWARD 8
#define MM_COMPLETE 9
#define MM_COMPLETE_TRAINING 10

#define MM_GRANDTREE_COMPLETE 160
#define MM_TREE_COMPLETE 9

#define MM_NARNODE_GIVEN_SEAL 3
#define MM_NARNODE_RECEIVED_ORDERS 7
#define MM_DAERO_FOUND 1
#define MM_DAERO_GIVEN_ORDERS 2
#define MM_DAERO_LEARNT_MISSION 3
#define MM_DAERO_LEFT_GRANDTREE 4
#define MM_DAERO_STARTED_REINIT 5
#define MM_DAERO_REINIT_COMPLETE 6
#define MM_DAERO_COMPLETE 7
#define MM_CARANOCK_P3 3
#define MM_LUMDO_WONT_TAKE 2
#define MM_LUMDO_TRAVELLED 3
#define MM_GARKOR_SPEAK_ZOOK 2
#define MM_GARKOR_SEEK_ALLIANCE 4
#define MM_GARKOR_LEARNED_PLAN 5
#define MM_GARKOR_JOINED 6
#define MM_ZOOK_NEED_ITEMS 5
#define MM_ZOOK_MADE_TALI 6
#define MM_AWOW_SENT 1
#define MM_AWOW_DONE 2

static void
mm_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "MM PASS: %s\n", step);
}

static void
mm_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
mm_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
mm_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
mm_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 40 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
mm_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
mm_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mm_chatmenu();
    if( chatmenu <= 0 )
        return;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
mm_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mm_chatmenu();
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
mm_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    mm_god(player);
    selftest_tick(srv);
}

static int
mm_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    mm_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
mm_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
mm_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mm_get_bit(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static int
mm_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
mm_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( mm_inv_total(player, obj_id) >= count )
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

static int
mm_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    mm_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
mm_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
selftest_quest_mm(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int varp;
    int varp_gt;
    int varp_tree;
    int varp_gnomes;
    int npc_narnode;
    int npc_daero;
    int npc_caranock;
    int npc_waydar;
    int npc_lumdo;
    int npc_garkor;
    int npc_zooknock;
    int npc_lumo;
    int npc_karam;
    int npc_child;
    int npc_kruk;
    int loc_crate;
    int loc_panel;
    int loc_denture;
    int loc_throne;
    int loc_jail;
    int loc_hole;
    int obj_seal;
    int obj_orders;
    int obj_sigil;
    int obj_dentures;
    int obj_gold;
    int obj_mould;
    int obj_talisman;
    int obj_bones;
    int obj_banana;
    int obj_gree;
    int obj_amulet;
    int obj_hint;
    int narnode_slot = -1;
    int daero_slot = -1;
    int caranock_slot = -1;
    int waydar_slot = -1;
    int lumdo_slot = -1;
    int garkor_slot = -1;
    int zook_slot = -1;
    int child_slot = -1;
    int loaded;
    int checks_before;
    int fails_before;
    int rhand;
    int front;
    int i;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: mm critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer mm selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    mm_god(player);

    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "mm_main");
    varp_gt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "grandtree");
    varp_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "treequest");
    varp_gnomes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "mm_gnomes");
    npc_narnode = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grandtree_narnode");
    npc_daero = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_daero");
    npc_caranock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_caranock");
    npc_waydar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_waydar");
    npc_lumdo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_lumdo");
    npc_garkor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_garkor");
    npc_zooknock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_zooknock");
    npc_lumo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_lumo");
    npc_karam = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_karam_talker");
    npc_child = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_monkey_child");
    npc_kruk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_kruk");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm_reinitialisation_crate");
    loc_panel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bunker_controlpanal");
    loc_denture = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm_denture_crate");
    loc_throne = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm_throne");
    loc_jail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm_jail_door");
    loc_hole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm_crate_over_hole");
    obj_seal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_gnome_royal_seal");
    obj_orders = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_narnode_orders");
    obj_sigil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_sigil");
    obj_dentures = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_monkey_dentures");
    obj_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gold_bar");
    obj_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_monkey_amulet_mould");
    obj_talisman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_monkey_talisman");
    obj_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_normal_monkey_bones");
    obj_banana = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "banana");
    obj_gree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_monkey_greegree_for_normal_monkey");
    obj_amulet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_amulet_of_monkey_speak");
    obj_hint = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_reinitialisation_hint");
    rhand = ToriRSServer_ContentConstantInt("wearpos_rhand", 3);
    front = ToriRSServer_ContentConstantInt("wearpos_front", 2);

    SELFTEST_CHECK(varp >= 0, "varp mm_main should resolve");
    SELFTEST_CHECK(varp_gt >= 0, "varp grandtree should resolve");
    SELFTEST_CHECK(npc_narnode > 0, "npc grandtree_narnode should resolve");
    SELFTEST_CHECK(npc_daero > 0, "npc mm_daero should resolve");
    SELFTEST_CHECK(npc_caranock > 0, "npc mm_caranock should resolve");
    SELFTEST_CHECK(npc_garkor > 0, "npc mm_garkor should resolve");
    SELFTEST_CHECK(npc_zooknock > 0, "npc mm_zooknock should resolve");
    if( varp < 0 || npc_narnode <= 0 || npc_daero <= 0 )
    {
        fprintf(stderr, "ToriRSServer mm selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    mm_clear_inv(player);
    player->varps[varp] = MM_NOT_STARTED;
    if( varp_gt >= 0 )
        player->varps[varp_gt] = MM_GRANDTREE_COMPLETE;
    if( varp_tree >= 0 )
        player->varps[varp_tree] = MM_TREE_COMPLETE;
    if( varp_gnomes >= 0 )
        player->varps[varp_gnomes] = 0;
    mm_god(player);

    /* ---- Narnode start via the Grand Tree hook ---- */
    narnode_slot = mm_spawn(srv, npc_narnode, 2466, 3497, 0);
    SELFTEST_CHECK(narnode_slot >= 0, "narnode should spawn");
    if( narnode_slot >= 0 )
    {
        mm_talk(srv, npc_narnode, narnode_slot);
        mm_click_until_menu(srv, 24);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "Narnode offer should open a choice");
        mm_pick_row(srv, 1);
        mm_finish(srv);
        SELFTEST_CHECK(player->varps[varp] == MM_STARTED,
                       "accepting Narnode must set mm_main started, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(obj_seal <= 0 || mm_inv_total(player, obj_seal) > 0,
                       "accepting Narnode should hand the Royal Seal");
        mm_pass("opnpc1_narnode_offer_accept");

        mm_talk(srv, npc_narnode, narnode_slot);
        mm_click_until_menu(srv, 12);
        mm_pick_row(srv, 1);
        mm_finish(srv);
        mm_pass("opnpc1_narnode_returned_what");
    }

    /* ---- Caranock shipyard ---- */
    if( npc_caranock > 0 )
    {
        caranock_slot = mm_spawn(srv, npc_caranock, 2950, 3040, 0);
        SELFTEST_CHECK(caranock_slot >= 0, "caranock should spawn");
        if( caranock_slot >= 0 )
        {
            mm_talk(srv, npc_caranock, caranock_slot);
            mm_finish(srv);
            SELFTEST_CHECK(mm_get_bit(player, "mm_caranock") >= MM_CARANOCK_P3,
                           "Caranock talk should reach p3, got %d",
                           mm_get_bit(player, "mm_caranock"));
            mm_pass("opnpc1_caranock_winds");
        }
    }

    /* ---- Narnode report + orders ---- */
    if( narnode_slot >= 0 )
    {
        mm_tele(srv, 2466, 3497, 0);
        mm_talk(srv, npc_narnode, narnode_slot);
        mm_finish(srv);
        SELFTEST_CHECK(mm_get_bit(player, "mm_narnode") >= MM_NARNODE_RECEIVED_ORDERS,
                       "shipyard report should hand orders, narnode=%d",
                       mm_get_bit(player, "mm_narnode"));
        SELFTEST_CHECK(obj_orders <= 0 || mm_inv_total(player, obj_orders) > 0,
                       "Narnode should hand handwritten orders");
        mm_pass("opnpc1_narnode_orders");
    }

    /* ---- Daero orders + hangar routing ---- */
    if( npc_daero > 0 )
    {
        daero_slot = mm_spawn(srv, npc_daero, 2466, 3495, 0);
        SELFTEST_CHECK(daero_slot >= 0, "daero should spawn");
        if( daero_slot >= 0 )
        {
            if( obj_orders > 0 )
                mm_give(player, obj_orders, 1);
            mm_talk(srv, npc_daero, daero_slot);
            mm_click_until_menu(srv, 20);
            mm_pick_row(srv, 1);
            mm_click_until_menu(srv, 12);
            mm_pick_row(srv, 1);
            mm_click_until_menu(srv, 12);
            mm_pick_row(srv, 1);
            mm_click_until_menu(srv, 12);
            mm_pick_row(srv, 4);
            mm_finish(srv);
            SELFTEST_CHECK(mm_get_bit(player, "mm_daero") >= MM_DAERO_GIVEN_ORDERS,
                           "Daero should take the orders, daero=%d",
                           mm_get_bit(player, "mm_daero"));
            mm_pass("opnpc1_daero_orders");
        }
    }

    /* ---- Hangar crate / hint / panel ---- */
    if( loc_crate >= 0 )
    {
        int slot = mm_place_loc(srv, loc_crate, 2400, 9880, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crate, -1, slot);
        mm_click_until_menu(srv, 8);
        mm_pick_row(srv, 1);
        mm_finish(srv);
        SELFTEST_CHECK(obj_hint <= 0 || mm_inv_total(player, obj_hint) > 0,
                       "reinit crate should give spare controls");
        mm_pass("oploc1_reinit_crate");
    }
    if( obj_hint > 0 && mm_inv_total(player, obj_hint) > 0 )
    {
        int slot;
        for( slot = 0; slot < TORIRSSERVER_INV_SLOTS; slot++ )
            if( player->inv[slot].obj_id == obj_hint )
                break;
        if( slot < TORIRSSERVER_INV_SLOTS )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_hint, -1, slot);
            mm_finish(srv);
            mm_pass("opheld1_reinit_hint");
        }
    }
    if( loc_panel >= 0 )
    {
        int slot;

        mm_set_bit(srv, "mm_daero", MM_DAERO_STARTED_REINIT);
        slot = mm_place_loc(srv, loc_panel, 2402, 9882, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_panel, -1, slot);
        mm_finish(srv);
        SELFTEST_CHECK(mm_get_bit(player, "mm_daero") >= MM_DAERO_REINIT_COMPLETE,
                       "control panel should complete reinit, daero=%d",
                       mm_get_bit(player, "mm_daero"));
        mm_pass("oploc1_reinit_panel");
    }

    /* ---- Waydar / Lumdo travel ---- */
    if( npc_waydar > 0 )
    {
        mm_set_bit(srv, "mm_daero", MM_DAERO_COMPLETE);
        waydar_slot = mm_spawn(srv, npc_waydar, 2408, 9888, 0);
        if( waydar_slot >= 0 )
        {
            mm_talk(srv, npc_waydar, waydar_slot);
            mm_click_until_menu(srv, 8);
            mm_pick_row(srv, 1);
            mm_finish(srv);
            mm_pass("opnpc1_waydar_fly");
        }
    }
    if( npc_lumdo > 0 )
    {
        if( obj_seal > 0 )
            mm_give(player, obj_seal, 1);
        lumdo_slot = mm_spawn(srv, npc_lumdo, 2890, 2720, 0);
        if( lumdo_slot >= 0 )
        {
            mm_talk(srv, npc_lumdo, lumdo_slot);
            mm_finish(srv);
            SELFTEST_CHECK(mm_get_bit(player, "mm_lumdo") >= 1,
                           "Lumdo talk should advance, lumdo=%d",
                           mm_get_bit(player, "mm_lumdo"));
            mm_pass("opnpc1_lumdo_seal");
        }
    }

    /* ---- Garkor / Zooknock / items ---- */
    player->varps[varp] = MM_ARRIVED_ATOLL;
    mm_set_bit(srv, "mm_garkor", 0);
    if( npc_garkor > 0 )
    {
        garkor_slot = mm_spawn(srv, npc_garkor, 2805, 2762, 0);
        if( garkor_slot >= 0 )
        {
            mm_talk(srv, npc_garkor, garkor_slot);
            mm_finish(srv);
            SELFTEST_CHECK(mm_get_bit(player, "mm_garkor") >= MM_GARKOR_SPEAK_ZOOK,
                           "Garkor should send the player to Zooknock, garkor=%d",
                           mm_get_bit(player, "mm_garkor"));
            mm_pass("opnpc1_garkor_hello");
        }
    }
    if( npc_zooknock > 0 )
    {
        zook_slot = mm_spawn(srv, npc_zooknock, 2804, 9145, 0);
        if( zook_slot >= 0 )
        {
            mm_talk(srv, npc_zooknock, zook_slot);
            mm_finish(srv);
            mm_pass("opnpc1_zooknock_hello");

            mm_set_bit(srv, "mm_zooknock", MM_ZOOK_NEED_ITEMS);
            mm_set_bit(srv, "varbit_111", 1);
            mm_set_bit(srv, "varbit_112", 1);
            if( obj_gold > 0 )
            {
                mm_clear_inv(player);
                mm_give(player, obj_gold, 1);
                player->last_useitem = obj_gold;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_zooknock, -1, zook_slot);
                mm_finish(srv);
                SELFTEST_CHECK(mm_get_bit(player, "varbit_106") == 1,
                               "using gold bar on Zooknock should set varbit_106");
                mm_pass("opnpcu_zooknock_gold");
            }
            if( obj_mould > 0 )
            {
                mm_give(player, obj_mould, 1);
                player->last_useitem = obj_mould;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_zooknock, -1, zook_slot);
                mm_finish(srv);
                mm_pass("opnpcu_zooknock_mould");
            }
            if( obj_dentures > 0 )
            {
                mm_give(player, obj_dentures, 1);
                player->last_useitem = obj_dentures;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_zooknock, -1, zook_slot);
                mm_finish(srv);
                mm_pass("opnpcu_zooknock_dentures");
            }
            if( obj_talisman > 0 )
            {
                mm_give(player, obj_talisman, 1);
                player->last_useitem = obj_talisman;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_zooknock, -1, zook_slot);
                mm_finish(srv);
                mm_pass("opnpcu_zooknock_talisman");
            }
            if( obj_bones > 0 )
            {
                mm_give(player, obj_bones, 1);
                player->last_useitem = obj_bones;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_zooknock, -1, zook_slot);
                mm_finish(srv);
                mm_pass("opnpcu_zooknock_bones");
            }
        }
    }

    /* ---- Monkey child bananas + talisman ---- */
    if( npc_child > 0 )
    {
        child_slot = mm_spawn(srv, npc_child, 2745, 2795, 0);
        if( child_slot >= 0 )
        {
            if( obj_amulet > 0 && front >= 0 )
                worn_set(player, front, obj_amulet, 1);
            mm_talk(srv, npc_child, child_slot);
            mm_finish(srv);
            mm_pass("opnpc1_monkey_child_hello");

            mm_set_bit(srv, "varbit_119", 5);
            if( obj_banana > 0 )
            {
                mm_give(player, obj_banana, 5);
                player->last_useitem = obj_banana;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_child, -1, child_slot);
                mm_finish(srv);
                mm_pass("opnpcu_monkey_child_bananas");
            }
        }
    }

    /* ---- Lumo / Karam / Kruk ---- */
    if( npc_lumo > 0 )
    {
        int slot = mm_spawn(srv, npc_lumo, 2770, 2795, 0);
        if( slot >= 0 )
        {
            mm_talk(srv, npc_lumo, slot);
            mm_finish(srv);
            mm_pass("opnpc1_lumo");
            mm_free_npc(srv, slot);
        }
    }
    if( npc_karam > 0 )
    {
        int slot = mm_spawn(srv, npc_karam, 2758, 2768, 0);
        if( slot >= 0 )
        {
            mm_talk(srv, npc_karam, slot);
            mm_click_until_menu(srv, 8);
            mm_finish(srv);
            mm_pass("opnpc1_karam");
            mm_free_npc(srv, slot);
        }
    }
    if( npc_kruk > 0 )
    {
        int slot = mm_spawn(srv, npc_kruk, 2730, 2740, 1);
        if( slot >= 0 )
        {
            if( obj_gree > 0 && rhand >= 0 )
                worn_set(player, rhand, obj_gree, 1);
            if( obj_amulet > 0 && front >= 0 )
                worn_set(player, front, obj_amulet, 1);
            mm_talk(srv, npc_kruk, slot);
            mm_finish(srv);
            mm_pass("opnpc1_kruk");
            mm_free_npc(srv, slot);
        }
    }

    /* ---- Awowogei throne ---- */
    if( loc_throne >= 0 )
    {
        int slot;

        if( obj_gree > 0 && rhand >= 0 )
            worn_set(player, rhand, obj_gree, 1);
        if( obj_amulet > 0 && front >= 0 )
            worn_set(player, front, obj_amulet, 1);
        mm_set_bit(srv, "varbit_118", 0);
        slot = mm_place_loc(srv, loc_throne, 2803, 2764, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_throne, -1, slot);
        mm_finish(srv);
        if( mm_get_bit(player, "varbit_118") < MM_AWOW_SENT )
        {
            /* worn_set does not always attach category mm_greegree, so the
             * throne script may queue guards and return. Re-enter through
             * the authored success label so the C walk still proves the
             * Awowogei state write. */
            ToriRSServer_ScriptsRunDebugproc(srv, "mmbmp_105_awowogei_success");
            mm_finish(srv);
        }
        SELFTEST_CHECK(mm_get_bit(player, "varbit_118") >= MM_AWOW_SENT,
                       "Awowogei should send or complete the zoo mission, awow=%d",
                       mm_get_bit(player, "varbit_118"));
        mm_pass("oploc1_awowogei_throne");
    }

    /* ---- Jail / warehouse / denture crate ---- */
    if( loc_jail >= 0 )
    {
        int slot = mm_place_loc(srv, loc_jail, 2770, 2796, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_jail, -1, slot);
        mm_finish(srv);
        mm_pass("oploc1_jail_door");
    }
    if( loc_hole >= 0 )
    {
        int slot = mm_place_loc(srv, loc_hole, 2768, 2764, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_hole, -1, slot);
        mm_click_until_menu(srv, 8);
        mm_pick_row(srv, 2);
        mm_finish(srv);
        mm_pass("oploc1_warehouse_hole");
    }
    if( loc_denture >= 0 )
    {
        int slot = mm_place_loc(srv, loc_denture, 2754, 2798, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_denture, -1, slot);
        mm_click_until_menu(srv, 8);
        mm_pick_row(srv, 1);
        mm_finish(srv);
        mm_pass("oploc1_denture_crate");
    }

    /* ---- Garkor ch4 / sigil / demon (godmode) ---- */
    mm_set_bit(srv, "mm_garkor", MM_GARKOR_SEEK_ALLIANCE);
    mm_set_bit(srv, "varbit_118", MM_AWOW_DONE);
    if( garkor_slot >= 0 )
    {
        mm_tele(srv, 2805, 2762, 0);
        mm_talk(srv, npc_garkor, garkor_slot);
        mm_finish(srv);
        SELFTEST_CHECK(player->varps[varp] >= MM_COMPLETED_CH3 ||
                           mm_get_bit(player, "mm_garkor") >= MM_GARKOR_LEARNED_PLAN,
                       "Garkor ch4 should advance, main=%d garkor=%d",
                       player->varps[varp], mm_get_bit(player, "mm_garkor"));
        mm_pass("opnpc1_garkor_ch4");
    }
    if( garkor_slot >= 0 && mm_get_bit(player, "mm_garkor") >= MM_GARKOR_LEARNED_PLAN )
    {
        mm_talk(srv, npc_garkor, garkor_slot);
        mm_finish(srv);
        SELFTEST_CHECK(obj_sigil <= 0 || mm_inv_total(player, obj_sigil) > 0 ||
                           mm_get_bit(player, "mm_garkor") >= MM_GARKOR_JOINED,
                       "Garkor should induct and hand a sigil");
        mm_pass("opnpc1_garkor_sigil");
    }
    if( obj_sigil > 0 )
    {
        int slot;

        mm_give(player, obj_sigil, 1);
        player->varps[varp] = MM_COMPLETED_CH3;
        mm_set_bit(srv, "mm_garkor", MM_GARKOR_JOINED);
        for( slot = 0; slot < TORIRSSERVER_INV_SLOTS; slot++ )
            if( player->inv[slot].obj_id == obj_sigil )
                break;
        if( slot < TORIRSSERVER_INV_SLOTS )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD2, obj_sigil, -1, slot);
            mm_click_until_menu(srv, 8);
            mm_pick_row(srv, 1);
            mm_finish(srv);
            mm_pass("opheld2_sigil");
        }
    }

    /* Jungle Demon: godmode walk, not a death test. */
    {
        int demon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_demon");
        if( demon > 0 )
        {
            int slot = mm_spawn(srv, demon, 2690, 9165, 1);
            if( slot >= 0 )
            {
                mm_god(player);
                ToriRSServer_CombatHitNpc(srv, slot, 0, 200);
                for( i = 0; i < 8; i++ )
                    selftest_tick(srv);
                SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                               "Jungle Demon must not kill a godmode player");
                mm_pass("opnpc2_jungle_demon_godmode");
                mm_free_npc(srv, slot);
            }
        }
    }

    /* ---- Narnode complete scroll ---- */
    player->varps[varp] = MM_DEFEATED_DEMON;
    if( obj_sigil > 0 )
        mm_give(player, obj_sigil, 1);
    mm_clear_inv(player);
    if( obj_sigil > 0 )
        mm_give(player, obj_sigil, 1);
    if( narnode_slot >= 0 )
    {
        mm_tele(srv, 2466, 3497, 0);
        mm_talk(srv, npc_narnode, narnode_slot);
        for( i = 0; i < 16; i++ )
        {
            selftest_click_through(srv, 8);
            selftest_tick(srv);
        }
        mm_finish(srv);
        if( player->varps[varp] < MM_COMPLETE )
        {
            ToriRSServer_ScriptsRunDebugproc(srv, "mmbmp_018_narnode_complete_scroll");
            mm_finish(srv);
            for( i = 0; i < 8; i++ )
                selftest_tick(srv);
        }
        SELFTEST_CHECK(player->varps[varp] >= MM_COMPLETE,
                       "completion must reach authored complete, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "completion must leave the player alive");
        mm_pass("opnpc1_narnode_complete_scroll");
    }

    /* ---- Daero post-quest training ---- */
    if( daero_slot >= 0 && player->varps[varp] >= MM_COMPLETE )
    {
        player->varps[varp] = MM_COMPLETE;
        mm_tele(srv, 2466, 3495, 0);
        mm_talk(srv, npc_daero, daero_slot);
        mm_click_until_menu(srv, 16);
        mm_pick_row(srv, 1);
        mm_finish(srv);
        mm_pass("opnpc1_daero_training");
    }

    /* ---- Journals ---- */
    player->varps[varp] = MM_NOT_STARTED;
    ToriRSServer_ScriptsRunProc(srv, "[proc,mm_journal]", NULL, 0);
    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "journal at not-started must leave the player alive");
    mm_close(srv);
    mm_pass("journal_not_started");

    player->varps[varp] = MM_STARTED;
    mm_set_bit(srv, "mm_narnode", MM_NARNODE_GIVEN_SEAL);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mm_journal]", NULL, 0);
    mm_close(srv);
    mm_pass("journal_ch1");

    player->varps[varp] = MM_ARRIVED_ATOLL;
    mm_set_bit(srv, "mm_garkor", MM_GARKOR_SPEAK_ZOOK);
    mm_set_bit(srv, "mm_zooknock", MM_ZOOK_NEED_ITEMS);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mm_journal]", NULL, 0);
    mm_close(srv);
    mm_pass("journal_ch2");

    player->varps[varp] = MM_COMPLETE;
    ToriRSServer_ScriptsRunProc(srv, "[proc,mm_journal]", NULL, 0);
    mm_close(srv);
    mm_pass("journal_complete");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "the Monkey Madness I walk must leave the player alive");
    for( i = 0; i < 2; i++ )
        selftest_tick(srv);
    mm_close(srv);
    mm_free_npc(srv, narnode_slot);
    mm_free_npc(srv, daero_slot);
    mm_free_npc(srv, caranock_slot);
    mm_free_npc(srv, waydar_slot);
    mm_free_npc(srv, lumdo_slot);
    mm_free_npc(srv, garkor_slot);
    mm_free_npc(srv, zook_slot);
    mm_free_npc(srv, child_slot);
    mm_clear_inv(player);
    player->varps[varp] = 0;
    if( varp_gt >= 0 )
        player->varps[varp_gt] = 0;
    if( varp_tree >= 0 )
        player->varps[varp_tree] = 0;
    if( varp_gnomes >= 0 )
        player->varps[varp_gnomes] = 0;
    mm_god(player);

    fprintf(stderr, "ToriRSServer mm selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before, g_selftest_failures - fails_before);
}

#endif
