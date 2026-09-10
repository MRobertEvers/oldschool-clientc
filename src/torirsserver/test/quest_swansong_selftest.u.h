#ifndef TORIRSSERVER_TEST_QUEST_SWANSONG_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_SWANSONG_SELFTEST_U_H

/* Swan Song Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Herman /
 * WOM / Franklin / Arnold / Frumscone / Malignius / Crafter / Queen / locs
 * cannot leak. Real OPNPC1 / OPNPC2 / OPLOC1 / OPLOCU / OPHELDU on the
 * authored path. player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_SWANSONG_ONLY=1
 *
 * Wise Old Man is the shared [opnpc1,wise_old_man] / [opnpc1,wom_multi]
 * trigger. Frumscone is the shared [opnpc1,wizard_frumscone] trigger.
 * Arnold Trade is the existing [opnpc3,swan_arnold] shop. pot_empty OPHELDU
 * is the existing handler in swansong_army.rs2.
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF / daily-reset
 * / flute widget / telekinetic grab):
 *   - west walls 2-5 collapse to wall 1 mesboxes
 *   - Arnold shop is the existing Trade trigger
 *   - siege cutscene deferred to 1-on-1 Queen
 *   - One Small Favour / Garden of Tranquillity prereqs soft-skipped
 *   - world monkfish spots already gated (not rewritten)
 *   - ambient colony flavour unused
 */

#define SSQ_NOT_STARTED 0
#define SSQ_AGREED 10
#define SSQ_WOM_TOLD 20
#define SSQ_RUNES_GIVEN 30
#define SSQ_READY 40
#define SSQ_TROLLS_BEATEN 50
#define SSQ_TOLD_TO_HELP 65
#define SSQ_HELPING 70
#define SSQ_TASKS_DONE 80
#define SSQ_FRUMSCONE 95
#define SSQ_MALIGNIUS 110
#define SSQ_CRAFTER 120
#define SSQ_POT_READY 130
#define SSQ_ARMY 140
#define SSQ_FINAL 160
#define SSQ_QUEEN_FIGHT 170
#define SSQ_QUEEN_DEAD 190
#define SSQ_COMPLETE 200

#define SSQ_FRANKLIN_TALKED 1
#define SSQ_FRANKLIN_LOG 2
#define SSQ_FRANKLIN_LIT 3
#define SSQ_FRANKLIN_DONE 4
#define SSQ_ARNOLD_TALKED 1
#define SSQ_ARNOLD_DONE 6
#define SSQ_TROLLS_NEEDED 3
#define SSQ_BONES_NEEDED 7

#define SSQ_REQ_MAGIC 66
#define SSQ_REQ_COOKING 62
#define SSQ_REQ_FISHING 62
#define SSQ_REQ_SMITHING 45
#define SSQ_REQ_FIREMAKING 42
#define SSQ_REQ_CRAFTING 40
#define SSQ_REWARD_MAGIC_TENTHS 150000
#define SSQ_REWARD_PRAYER_TENTHS 100000
#define SSQ_REWARD_FISHING_TENTHS 500000
#define SSQ_REWARD_COINS 25000
#define SSQ_REWARD_QP 2

#define SSQ_GATE_X 2345
#define SSQ_GATE_Z 3651
#define SSQ_HOLE_X 2344
#define SSQ_HOLE_Z 3651
#define SSQ_AMBUSH_X 2343
#define SSQ_AMBUSH_Z 3657
#define SSQ_HERMAN_X 2354
#define SSQ_HERMAN_Z 3683
#define SSQ_FRANKLIN_X 2344
#define SSQ_FRANKLIN_Z 3667
#define SSQ_FIREBOX_X 2344
#define SSQ_FIREBOX_Z 3676
#define SSQ_PRESS_X 2342
#define SSQ_PRESS_Z 3676
#define SSQ_WALL1_X 2336
#define SSQ_WALL1_Z 3665
#define SSQ_WALL2_X 2336
#define SSQ_WALL2_Z 3667
#define SSQ_WALL3_X 2336
#define SSQ_WALL3_Z 3669
#define SSQ_WALL4_X 2336
#define SSQ_WALL4_Z 3671
#define SSQ_WALL5_X 2336
#define SSQ_WALL5_Z 3673
#define SSQ_ARNOLD_X 2330
#define SSQ_ARNOLD_Z 3691
#define SSQ_FISH_X 2311
#define SSQ_FISH_Z 3696
#define SSQ_STOVE_X 2316
#define SSQ_STOVE_Z 3669
#define SSQ_QUEEN_X 2347
#define SSQ_QUEEN_Z 3704
#define SSQ_WOM_X 3088
#define SSQ_WOM_Z 3255
#define SSQ_FRUMSCONE_X 2588
#define SSQ_FRUMSCONE_Z 9489
#define SSQ_MALIGNIUS_X 2991
#define SSQ_MALIGNIUS_Z 3269
#define SSQ_CRAFTER_X 2937
#define SSQ_CRAFTER_Z 3290
#define SSQ_WOM_OFFICE_X 2353
#define SSQ_WOM_OFFICE_Z 3683

static void
ssq_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SSQ PASS: %s\n", step);
}

static void
ssq_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ssq_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ssq_finish(struct ToriRSServer* srv)
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

static int
ssq_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ssq_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ssq_chatmenu();
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
ssq_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ssq_chatmenu();
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
ssq_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ssq_god(player);
    selftest_tick(srv);
}

static int
ssq_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    ssq_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
ssq_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
ssq_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ssq_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
ssq_quest(struct ToriRSServerPlayer* player)
{
    return ssq_get_vb(player, "swansong");
}

static void
ssq_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
ssq_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ssq_talk(srv, npc_type, slot);
    ssq_finish(srv);
}

static void
ssq_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    ssq_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        ssq_click_until_menu(srv, 24);
        ssq_pick_row(srv, rows[i]);
    }
    ssq_finish(srv);
}

static void
ssq_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
ssq_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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

static int
ssq_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    ssq_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
ssq_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    ssq_finish(srv);
}

static void
ssq_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
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
    ssq_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
ssq_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    ssq_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
ssq_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
ssq_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        ssq_set_stat(player, stat, 99);
}

static void
ssq_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    ssq_vb(srv, "swansong", SSQ_NOT_STARTED);
    ssq_vb(srv, "swansong_franklin", 0);
    ssq_vb(srv, "swansong_wall_1", 0);
    ssq_vb(srv, "swansong_wall_2", 0);
    ssq_vb(srv, "swansong_wall_3", 0);
    ssq_vb(srv, "swansong_wall_4", 0);
    ssq_vb(srv, "swansong_wall_5", 0);
    ssq_vb(srv, "swansong_arnold", 0);
    ssq_vb(srv, "swansong_trolls", 0);
    ssq_vb(srv, "swansong_bones", 0);
    ssq_vb(srv, "swansong_ambush", 0);
    ssq_vb(srv, "swansong_colony", 0);
}

static void
ssq_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,swansong_journal]", NULL, 0);
    ssq_finish(srv);
    ssq_pass(step);
}

static void
ssq_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    ssq_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    ssq_finish(srv);
}

static void
selftest_quest_swansong(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_gate;
    int npc_herman;
    int npc_wom;
    int npc_franklin;
    int npc_arnold;
    int npc_frumscone;
    int npc_malignius;
    int npc_crafter;
    int npc_wom_office;
    int npc_troll;
    int npc_queen;
    int loc_hole;
    int loc_firebox;
    int loc_press;
    int loc_wall1;
    int loc_wall2;
    int loc_fish;
    int loc_stove;
    int obj_logs;
    int obj_tinder;
    int obj_iron;
    int obj_hammer;
    int obj_sheet;
    int obj_net;
    int obj_raw;
    int obj_cooked;
    int obj_blood;
    int obj_lava;
    int obj_mist;
    int obj_pot;
    int obj_lid;
    int obj_airtight;
    int obj_army;
    int obj_coins;
    int obj_apron;
    int stat_magic;
    int stat_cook;
    int stat_fish;
    int stat_smith;
    int stat_fm;
    int stat_craft;
    int stat_pray;
    int varp_qp;
    int slot_gate;
    int slot_herman;
    int slot_wom;
    int slot_franklin;
    int slot_arnold;
    int slot_frumscone;
    int slot_malignius;
    int slot_crafter;
    int slot_office;
    int slot_troll;
    int slot_queen;
    int hole_slot;
    int firebox_slot;
    int press_slot;
    int wall1_slot;
    int wall2_slot;
    int fish_slot;
    int stove_slot;
    int magic_before;
    int pray_before;
    int fish_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: swan song critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer swansong selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    ssq_god(player);
    ssq_reset_quest(srv);
    ssq_clear_inv(player);

    npc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "swan_multioutside");
    npc_herman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "swan_herman");
    npc_wom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wise_old_man");
    npc_franklin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "swan_franklin");
    npc_arnold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "swan_arnold");
    npc_frumscone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wizard_frumscone");
    npc_malignius = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elemental_wizard_boss");
    npc_crafter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "master_crafter_3");
    npc_wom_office = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "swan_wom_office");
    npc_troll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "swan_troll_ambush");
    npc_queen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "swan_seatroll_queen");
    loc_hole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swan_hole");
    loc_firebox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swan_firebox");
    loc_press = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swan_press");
    loc_wall1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swan_wall_1");
    loc_wall2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swan_wall_2");
    loc_fish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swan_fish");
    loc_stove = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "swan_stove");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_sheet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_sheet");
    obj_net = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "net");
    obj_raw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swan_raw_monkfish");
    obj_cooked = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swan_monkfish");
    obj_blood = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bloodrune");
    obj_lava = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lavarune");
    obj_mist = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mistrune");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    obj_lid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "potlid");
    obj_airtight = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_airtight_pot");
    obj_army = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swan_army");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_apron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "brown_apron");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    stat_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
    stat_fish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fishing");
    stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_pray = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_swansong") > 0,
                   "dbrow quest_swansong should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "swansong") >= 0,
                   "varbit swansong should resolve");
    SELFTEST_CHECK(npc_gate > 0, "npc swan_multioutside should resolve");
    SELFTEST_CHECK(npc_herman > 0, "npc swan_herman should resolve");
    SELFTEST_CHECK(npc_wom > 0, "npc wise_old_man should resolve");
    SELFTEST_CHECK(npc_franklin > 0, "npc swan_franklin should resolve");
    SELFTEST_CHECK(npc_arnold > 0, "npc swan_arnold should resolve");
    SELFTEST_CHECK(npc_frumscone > 0, "npc wizard_frumscone should resolve");
    SELFTEST_CHECK(npc_malignius > 0, "npc elemental_wizard_boss should resolve");
    SELFTEST_CHECK(npc_crafter > 0, "npc master_crafter_3 should resolve");
    SELFTEST_CHECK(loc_hole > 0, "loc swan_hole should resolve");
    SELFTEST_CHECK(obj_army > 0, "obj swan_army should resolve");
    if( npc_gate <= 0 )
        return;

    ssq_journal(srv, "journal_00_not_started");

    slot_gate = ssq_spawn(srv, npc_gate, SSQ_GATE_X, SSQ_GATE_Z, 0);
    SELFTEST_CHECK(slot_gate >= 0, "colony-gate Herman should spawn");
    if( slot_gate >= 0 )
    {
        ssq_set_stat(player, stat_magic, 1);
        ssq_set_stat(player, stat_cook, 1);
        ssq_set_stat(player, stat_fish, 1);
        ssq_set_stat(player, stat_smith, 1);
        ssq_set_stat(player, stat_fm, 1);
        ssq_set_stat(player, stat_craft, 1);
        ssq_talk_rows(srv, npc_gate, slot_gate, k_refuse, 1);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_NOT_STARTED,
                       "Herman refuse must stay not_started");
        ssq_pass("opnpc1_herman_gate_refuse");

        ssq_talk_rows(srv, npc_gate, slot_gate, k_accept, 1);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_NOT_STARTED,
                       "Herman qualify-fail must stay not_started");
        ssq_pass("opnpc1_herman_gate_qualify_fail");

        ssq_skills99(player);
        ssq_talk_rows(srv, npc_gate, slot_gate, k_accept, 1);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_AGREED,
                       "Herman accept must write agreed_to_help, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_herman_gate_accept");

        ssq_talk_finish(srv, npc_gate, slot_gate);
        ssq_pass("opnpc1_herman_gate_fetch_wom");
    }
    ssq_journal(srv, "journal_10_agreed");

    ssq_free_npc(srv, slot_gate);
    slot_wom = ssq_spawn(srv, npc_wom, SSQ_WOM_X, SSQ_WOM_Z, 0);
    if( slot_wom >= 0 )
    {
        ssq_talk_finish(srv, npc_wom, slot_wom);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_WOM_TOLD,
                       "WOM first talk must write wom_told, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_wom_draynor_first");

        ssq_talk_finish(srv, npc_wom, slot_wom);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_WOM_TOLD,
                       "WOM without runes must stay wom_told");
        ssq_pass("opnpc1_wom_draynor_need_runes");

        if( obj_blood > 0 )
            ssq_give(player, obj_blood, 5);
        if( obj_lava > 0 )
            ssq_give(player, obj_lava, 10);
        if( obj_mist > 0 )
            ssq_give(player, obj_mist, 10);
        ssq_talk_finish(srv, npc_wom, slot_wom);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_RUNES_GIVEN,
                       "WOM rune hand-in must write runes_given, got %d",
                       ssq_quest(player));
        SELFTEST_CHECK(ssq_inv_total(player, obj_blood) == 0,
                       "WOM must consume the blood runes");
        ssq_pass("opnpc1_wom_draynor_give_runes");
    }

    ssq_free_npc(srv, slot_wom);
    slot_gate = ssq_spawn(srv, npc_gate, SSQ_GATE_X, SSQ_GATE_Z, 0);
    if( slot_gate >= 0 )
    {
        ssq_talk_finish(srv, npc_gate, slot_gate);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_READY,
                       "WOM gate greet must write ready_to_fight, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_wom_gate_greet");

        ssq_talk_finish(srv, npc_gate, slot_gate);
        ssq_pass("opnpc1_wom_gate_ready_reminder");
    }

    hole_slot = ssq_place_loc(srv, loc_hole, SSQ_HOLE_X, SSQ_HOLE_Z, 0);
    ssq_vb(srv, "swansong", SSQ_WOM_TOLD);
    ssq_clear_inv(player);
    if( loc_hole > 0 )
    {
        ssq_oploc(srv, loc_hole, hole_slot);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_WOM_TOLD,
                       "hole before ready must stay wom_told");
        ssq_pass("oploc1_hole_too_early");
    }

    ssq_vb(srv, "swansong", SSQ_READY);
    if( loc_hole > 0 )
    {
        ssq_oploc(srv, loc_hole, hole_slot);
        ssq_pass("oploc1_hole_need_logs");

        if( obj_logs > 0 )
            ssq_give(player, obj_logs, 1);
        ssq_oploc(srv, loc_hole, hole_slot);
        ssq_pass("oploc1_hole_need_tinderbox");

        if( obj_tinder > 0 )
            ssq_give(player, obj_tinder, 1);
        ssq_oploc(srv, loc_hole, hole_slot);
        ssq_pass("oploc1_hole_need_iron_bars");

        if( obj_iron > 0 )
            ssq_give(player, obj_iron, 5);
        ssq_oploc(srv, loc_hole, hole_slot);
        ssq_pass("oploc1_hole_need_hammer");

        if( obj_hammer > 0 )
            ssq_give(player, obj_hammer, 1);
        ssq_oploc(srv, loc_hole, hole_slot);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_ambush") == 1,
                       "colony enter must arm the entrance ambush");
        ssq_pass("oploc1_hole_enter_ambush");
    }

    slot_troll = -1;
    if( npc_troll > 0 )
    {
        int i;

        ssq_vb(srv, "swansong", SSQ_READY);
        ssq_vb(srv, "swansong_trolls", 0);
        ssq_vb(srv, "swansong_bones", 0);
        for( i = 0; i < SSQ_TROLLS_NEEDED; i++ )
        {
            slot_troll = ssq_spawn(srv, npc_troll, SSQ_AMBUSH_X, SSQ_AMBUSH_Z, 0);
            ssq_kill(srv, npc_troll, slot_troll);
            ssq_free_npc(srv, slot_troll);
        }
        SELFTEST_CHECK(ssq_quest(player) == SSQ_TROLLS_BEATEN,
                       "three ambush deaths must write trolls_beaten, got %d",
                       ssq_quest(player));
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_trolls") >= SSQ_TROLLS_NEEDED,
                       "ambush must increment swansong_trolls");
        ssq_pass("ai_queue3_trolls_beaten");
    }

    if( slot_gate >= 0 )
    {
        ssq_talk_finish(srv, npc_gate, slot_gate);
        ssq_pass("opnpc1_gate_after_trolls");
    }

    ssq_free_npc(srv, slot_gate);
    slot_herman = ssq_spawn(srv, npc_herman, SSQ_HERMAN_X, SSQ_HERMAN_Z, 0);
    if( slot_herman >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_READY);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        ssq_pass("opnpc1_herman_inside_too_early");

        ssq_vb(srv, "swansong", SSQ_TROLLS_BEATEN);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_HELPING,
                       "Herman building intro must write helping, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_herman_building_intro");

        ssq_talk_finish(srv, npc_herman, slot_herman);
        ssq_pass("opnpc1_herman_helping_neither");
    }
    ssq_journal(srv, "journal_70_helping");

    ssq_free_npc(srv, slot_herman);
    slot_franklin = ssq_spawn(srv, npc_franklin, SSQ_FRANKLIN_X, SSQ_FRANKLIN_Z, 0);
    if( slot_franklin >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_TROLLS_BEATEN);
        ssq_talk_finish(srv, npc_franklin, slot_franklin);
        ssq_pass("opnpc1_franklin_too_early");

        ssq_vb(srv, "swansong", SSQ_HELPING);
        ssq_talk_finish(srv, npc_franklin, slot_franklin);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_franklin") == SSQ_FRANKLIN_TALKED,
                       "Franklin intro must write franklin_talked");
        ssq_pass("opnpc1_franklin_intro");

        ssq_talk_finish(srv, npc_franklin, slot_franklin);
        ssq_pass("opnpc1_franklin_need_logs");
    }

    firebox_slot = ssq_place_loc(srv, loc_firebox, SSQ_FIREBOX_X, SSQ_FIREBOX_Z, 0);
    if( loc_firebox > 0 )
    {
        ssq_vb(srv, "swansong_franklin", 0);
        ssq_use_loc(srv, loc_firebox, firebox_slot, obj_logs);
        ssq_pass("oplocu_firebox_speak_first");

        ssq_vb(srv, "swansong_franklin", SSQ_FRANKLIN_TALKED);
        ssq_clear_inv(player);
        if( obj_hammer > 0 )
            ssq_use_loc(srv, loc_firebox, firebox_slot, obj_hammer);
        ssq_pass("oplocu_firebox_wrong_item");

        if( obj_logs > 0 )
            ssq_give(player, obj_logs, 1);
        ssq_use_loc(srv, loc_firebox, firebox_slot, obj_logs);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_franklin") == SSQ_FRANKLIN_LOG,
                       "logs on firebox must write franklin_log_added");
        ssq_pass("oplocu_firebox_add_logs");

        if( slot_franklin >= 0 )
        {
            ssq_talk_finish(srv, npc_franklin, slot_franklin);
            ssq_pass("opnpc1_franklin_need_light");
        }

        if( obj_tinder > 0 )
            ssq_give(player, obj_tinder, 1);
        ssq_use_loc(srv, loc_firebox, firebox_slot, obj_tinder);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_franklin") == SSQ_FRANKLIN_LIT,
                       "tinderbox on firebox must write franklin_lit");
        ssq_pass("oplocu_firebox_light");

        ssq_use_loc(srv, loc_firebox, firebox_slot, obj_tinder);
        ssq_pass("oplocu_firebox_already_lit");
    }

    if( slot_franklin >= 0 )
    {
        ssq_talk_finish(srv, npc_franklin, slot_franklin);
        ssq_pass("opnpc1_franklin_need_walls");
    }

    press_slot = ssq_place_loc(srv, loc_press, SSQ_PRESS_X, SSQ_PRESS_Z, 0);
    if( loc_press > 0 && obj_iron > 0 )
    {
        ssq_vb(srv, "swansong_franklin", SSQ_FRANKLIN_TALKED);
        ssq_clear_inv(player);
        ssq_give(player, obj_iron, 5);
        ssq_use_loc(srv, loc_press, press_slot, obj_iron);
        ssq_pass("oplocu_press_need_furnace");

        ssq_vb(srv, "swansong_franklin", SSQ_FRANKLIN_LIT);
        ssq_use_loc(srv, loc_press, press_slot, obj_iron);
        SELFTEST_CHECK(ssq_inv_total(player, obj_sheet) >= 1,
                       "press must yield an iron sheet");
        ssq_pass("oplocu_press_make_sheet");
    }

    wall1_slot = ssq_place_loc(srv, loc_wall1, SSQ_WALL1_X, SSQ_WALL1_Z, 0);
    if( loc_wall1 > 0 )
    {
        ssq_clear_inv(player);
        ssq_use_loc(srv, loc_wall1, wall1_slot, obj_sheet);
        ssq_pass("oplocu_wall_need_sheet");

        if( obj_sheet > 0 )
            ssq_give(player, obj_sheet, 1);
        ssq_use_loc(srv, loc_wall1, wall1_slot, obj_sheet);
        ssq_pass("oplocu_wall_need_hammer");

        if( obj_hammer > 0 )
            ssq_give(player, obj_hammer, 1);
        ssq_use_loc(srv, loc_wall1, wall1_slot, obj_sheet);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_wall_1") == 1,
                       "wall 1 repair must flip swansong_wall_1");
        ssq_pass("oplocu_wall_repair");

        ssq_use_loc(srv, loc_wall1, wall1_slot, obj_sheet);
        ssq_pass("oplocu_wall_already_fixed");
    }

    wall2_slot = ssq_place_loc(srv, loc_wall2, SSQ_WALL2_X, SSQ_WALL2_Z, 0);
    if( loc_wall2 > 0 && obj_sheet > 0 && obj_hammer > 0 )
    {
        ssq_clear_inv(player);
        ssq_give(player, obj_sheet, 4);
        ssq_give(player, obj_hammer, 1);
        ssq_vb(srv, "swansong_wall_2", 0);
        ssq_vb(srv, "swansong_wall_3", 0);
        ssq_vb(srv, "swansong_wall_4", 0);
        ssq_vb(srv, "swansong_wall_5", 0);
        ssq_use_loc(srv, loc_wall2, wall2_slot, obj_sheet);
        ssq_vb(srv, "swansong_wall_2", 1);
        ssq_vb(srv, "swansong_wall_3", 1);
        ssq_vb(srv, "swansong_wall_4", 1);
        ssq_vb(srv, "swansong_wall_5", 1);
        ssq_pass("oplocu_walls_2_to_5_collapsed");
    }

    if( slot_franklin >= 0 )
    {
        ssq_talk_finish(srv, npc_franklin, slot_franklin);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_franklin") == SSQ_FRANKLIN_DONE,
                       "Franklin walls-done must write franklin_finished");
        ssq_pass("opnpc1_franklin_walls_done");

        ssq_talk_finish(srv, npc_franklin, slot_franklin);
        ssq_pass("opnpc1_franklin_finished");
    }

    ssq_free_npc(srv, slot_franklin);
    slot_arnold = ssq_spawn(srv, npc_arnold, SSQ_ARNOLD_X, SSQ_ARNOLD_Z, 0);
    if( slot_arnold >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_TROLLS_BEATEN);
        ssq_talk_finish(srv, npc_arnold, slot_arnold);
        ssq_pass("opnpc1_arnold_too_early");

        ssq_vb(srv, "swansong", SSQ_HELPING);
        ssq_clear_inv(player);
        ssq_talk_finish(srv, npc_arnold, slot_arnold);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_arnold") == SSQ_ARNOLD_TALKED,
                       "Arnold intro must write arnold_talked");
        ssq_pass("opnpc1_arnold_intro");

        ssq_talk_finish(srv, npc_arnold, slot_arnold);
        ssq_pass("opnpc1_arnold_need_fish");
    }

    fish_slot = ssq_place_loc(srv, loc_fish, SSQ_FISH_X, SSQ_FISH_Z, 0);
    if( loc_fish > 0 )
    {
        ssq_vb(srv, "swansong_arnold", 0);
        ssq_oploc(srv, loc_fish, fish_slot);
        ssq_pass("oploc1_fish_nothing");

        ssq_vb(srv, "swansong_arnold", SSQ_ARNOLD_TALKED);
        ssq_clear_inv(player);
        ssq_oploc(srv, loc_fish, fish_slot);
        ssq_pass("oploc1_fish_need_net");

        if( obj_net > 0 )
            ssq_give(player, obj_net, 1);
        ssq_vb(srv, "swansong_colony", 0);
        ssq_oploc(srv, loc_fish, fish_slot);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_colony") == 1,
                       "first fish must spawn the colony troll");
        ssq_pass("oploc1_fish_troll_ambush");

        if( npc_troll > 0 )
        {
            slot_troll = ssq_spawn(srv, npc_troll, SSQ_FISH_X, SSQ_FISH_Z, 0);
            ssq_oploc(srv, loc_fish, fish_slot);
            ssq_pass("oploc1_fish_troll_blocking");
            ssq_kill(srv, npc_troll, slot_troll);
            ssq_free_npc(srv, slot_troll);
        }

        ssq_oploc(srv, loc_fish, fish_slot);
        SELFTEST_CHECK(ssq_inv_total(player, obj_raw) >= 1,
                       "safe fish must yield raw monkfish");
        ssq_pass("oploc1_fish_catch");
    }

    stove_slot = ssq_place_loc(srv, loc_stove, SSQ_STOVE_X, SSQ_STOVE_Z, 0);
    if( loc_stove > 0 )
    {
        int raw_had;

        raw_had = ssq_inv_total(player, obj_raw);
        if( raw_had <= 0 )
        {
            ssq_oploc(srv, loc_stove, stove_slot);
            ssq_pass("oploc1_stove_no_raw");
            if( obj_raw > 0 )
                ssq_give(player, obj_raw, 1);
        }
        ssq_oploc(srv, loc_stove, stove_slot);
        SELFTEST_CHECK(ssq_inv_total(player, obj_cooked) >= 1,
                       "stove must cook a monkfish");
        ssq_pass("oploc1_stove_cook");
    }

    if( slot_arnold >= 0 )
    {
        ssq_clear_inv(player);
        if( obj_cooked > 0 )
            ssq_give(player, obj_cooked, 2);
        ssq_talk_finish(srv, npc_arnold, slot_arnold);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_arnold") > SSQ_ARNOLD_TALKED,
                       "partial monkfish hand-in must increment arnold");
        ssq_pass("opnpc1_arnold_handin_partial");

        if( obj_cooked > 0 )
            ssq_give(player, obj_cooked, 5);
        ssq_talk_finish(srv, npc_arnold, slot_arnold);
        SELFTEST_CHECK(ssq_get_vb(player, "swansong_arnold") >= SSQ_ARNOLD_DONE,
                       "five monkfish must finish Arnold, got %d",
                       ssq_get_vb(player, "swansong_arnold"));
        ssq_pass("opnpc1_arnold_handin_done");

        ssq_talk_finish(srv, npc_arnold, slot_arnold);
        ssq_pass("opnpc1_arnold_finished");
    }

    ssq_free_npc(srv, slot_arnold);
    slot_herman = ssq_spawn(srv, npc_herman, SSQ_HERMAN_X, SSQ_HERMAN_Z, 0);
    if( slot_herman >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_HELPING);
        ssq_vb(srv, "swansong_franklin", SSQ_FRANKLIN_DONE);
        ssq_vb(srv, "swansong_arnold", 0);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        ssq_pass("opnpc1_herman_helping_franklin");

        ssq_vb(srv, "swansong_franklin", 0);
        ssq_vb(srv, "swansong_arnold", SSQ_ARNOLD_DONE);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        ssq_pass("opnpc1_herman_helping_arnold");

        ssq_vb(srv, "swansong", SSQ_HELPING);
        ssq_vb(srv, "swansong_franklin", SSQ_FRANKLIN_DONE);
        ssq_vb(srv, "swansong_arnold", SSQ_ARNOLD_DONE);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_TASKS_DONE,
                       "both sub-tasks must write tasks_done, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_herman_helping_both");

        ssq_talk_finish(srv, npc_herman, slot_herman);
        ssq_pass("opnpc1_herman_after_tasks");
    }
    ssq_journal(srv, "journal_80_tasks_done");

    ssq_free_npc(srv, slot_herman);
    slot_frumscone = ssq_spawn(srv, npc_frumscone, SSQ_FRUMSCONE_X, SSQ_FRUMSCONE_Z, 0);
    if( slot_frumscone >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_HELPING);
        ssq_talk_finish(srv, npc_frumscone, slot_frumscone);
        ssq_pass("opnpc1_frumscone_too_early");

        ssq_vb(srv, "swansong", SSQ_TASKS_DONE);
        ssq_talk_finish(srv, npc_frumscone, slot_frumscone);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_FRUMSCONE,
                       "Frumscone must write frumscone_done, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_frumscone_talk");
    }

    ssq_free_npc(srv, slot_frumscone);
    slot_malignius = ssq_spawn(srv, npc_malignius, SSQ_MALIGNIUS_X, SSQ_MALIGNIUS_Z, 0);
    if( slot_malignius >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_TASKS_DONE);
        ssq_talk_finish(srv, npc_malignius, slot_malignius);
        ssq_pass("opnpc1_malignius_too_early");

        ssq_vb(srv, "swansong", SSQ_FRUMSCONE);
        ssq_vb(srv, "swansong_bones", 0);
        ssq_talk_finish(srv, npc_malignius, slot_malignius);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_MALIGNIUS,
                       "Malignius first talk must write malignius_first, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_malignius_first_need_bones");

        ssq_vb(srv, "swansong", SSQ_POT_READY);
        ssq_clear_inv(player);
        ssq_talk_finish(srv, npc_malignius, slot_malignius);
        ssq_pass("opnpc1_malignius_need_pot");

        if( obj_airtight > 0 )
            ssq_give(player, obj_airtight, 1);
        ssq_vb(srv, "swansong_bones", 3);
        ssq_talk_finish(srv, npc_malignius, slot_malignius);
        ssq_pass("opnpc1_malignius_need_bones_with_pot");

        ssq_vb(srv, "swansong_bones", SSQ_BONES_NEEDED);
        if( obj_airtight > 0 && ssq_inv_total(player, obj_airtight) <= 0 )
            ssq_give(player, obj_airtight, 1);
        ssq_talk_finish(srv, npc_malignius, slot_malignius);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_ARMY,
                       "Malignius army must write army_ready, got %d",
                       ssq_quest(player));
        SELFTEST_CHECK(ssq_inv_total(player, obj_army) >= 1,
                       "Malignius must hand over swan_army");
        ssq_pass("opnpc1_malignius_army");

        ssq_talk_finish(srv, npc_malignius, slot_malignius);
        ssq_pass("opnpc1_malignius_after");
    }

    ssq_free_npc(srv, slot_malignius);
    slot_crafter = ssq_spawn(srv, npc_crafter, SSQ_CRAFTER_X, SSQ_CRAFTER_Z, 0);
    if( slot_crafter >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_FRUMSCONE);
        ssq_talk_finish(srv, npc_crafter, slot_crafter);
        ssq_pass("opnpc1_crafter_too_early");

        ssq_vb(srv, "swansong", SSQ_MALIGNIUS);
        ssq_clear_inv(player);
        ssq_talk_finish(srv, npc_crafter, slot_crafter);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_CRAFTER,
                       "Master Crafter must write crafter_done, got %d",
                       ssq_quest(player));
        SELFTEST_CHECK(ssq_inv_total(player, obj_pot) >= 1 &&
                           ssq_inv_total(player, obj_lid) >= 1,
                       "Master Crafter must hand over pot and lid");
        ssq_pass("opnpc1_crafter_talk");

        ssq_talk_finish(srv, npc_crafter, slot_crafter);
        ssq_pass("opnpc1_crafter_use_lid");

        ssq_clear_inv(player);
        ssq_talk_finish(srv, npc_crafter, slot_crafter);
        ssq_pass("opnpc1_crafter_give_again");
    }

    if( obj_pot > 0 && obj_lid > 0 )
    {
        ssq_clear_inv(player);
        ssq_give(player, obj_pot, 1);
        ssq_give(player, obj_lid, 1);
        ssq_vb(srv, "swansong", SSQ_CRAFTER);
        ssq_opheldu(srv, obj_pot, obj_lid);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_POT_READY,
                       "pot+lid must write pot_ready, got %d",
                       ssq_quest(player));
        SELFTEST_CHECK(ssq_inv_total(player, obj_airtight) >= 1,
                       "pot+lid must yield an airtight pot");
        ssq_pass("opheldu_pot_seal");

        ssq_clear_inv(player);
        ssq_give(player, obj_pot, 1);
        if( obj_hammer > 0 )
        {
            ssq_give(player, obj_hammer, 1);
            ssq_opheldu(srv, obj_pot, obj_hammer);
            ssq_pass("opheldu_pot_wrong_item");
        }
    }

    if( slot_crafter >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_POT_READY);
        if( obj_airtight > 0 && ssq_inv_total(player, obj_airtight) <= 0 )
            ssq_give(player, obj_airtight, 1);
        ssq_talk_finish(srv, npc_crafter, slot_crafter);
        ssq_pass("opnpc1_crafter_fine_pot");
    }
    ssq_journal(srv, "journal_140_army");

    ssq_free_npc(srv, slot_crafter);
    slot_herman = ssq_spawn(srv, npc_herman, SSQ_HERMAN_X, SSQ_HERMAN_Z, 0);
    if( slot_herman >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_FRUMSCONE);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        ssq_pass("opnpc1_herman_wizards_wait");

        ssq_vb(srv, "swansong", SSQ_ARMY);
        ssq_clear_inv(player);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_FINAL,
                       "Herman without army pot must still write final_talk, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_herman_army_no_pot");

        ssq_vb(srv, "swansong", SSQ_ARMY);
        if( obj_army > 0 )
            ssq_give(player, obj_army, 1);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_FINAL,
                       "Herman army comedy must write final_talk, got %d",
                       ssq_quest(player));
        SELFTEST_CHECK(ssq_inv_total(player, obj_army) == 0,
                       "Herman army talk must consume swan_army");
        ssq_pass("opnpc1_herman_army_talk");

        ssq_talk_rows(srv, npc_herman, slot_herman, k_refuse, 1);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_FINAL,
                       "Herman ready-refuse must stay final_talk");
        ssq_pass("opnpc1_herman_ready_refuse");

        ssq_talk_rows(srv, npc_herman, slot_herman, k_accept, 1);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_QUEEN_FIGHT,
                       "Herman ready-accept must write queen_fight, got %d",
                       ssq_quest(player));
        ssq_pass("opnpc1_herman_ready_accept");

        ssq_talk_finish(srv, npc_herman, slot_herman);
        ssq_pass("opnpc1_herman_queen_fight");
    }

    if( npc_queen > 0 )
    {
        slot_queen = ssq_spawn(srv, npc_queen, SSQ_QUEEN_X, SSQ_QUEEN_Z, 0);
        ssq_vb(srv, "swansong", SSQ_QUEEN_FIGHT);
        ssq_kill(srv, npc_queen, slot_queen);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_QUEEN_DEAD,
                       "Queen death must write queen_dead, got %d",
                       ssq_quest(player));
        ssq_pass("ai_queue3_queen_death");
        ssq_free_npc(srv, slot_queen);
    }

    if( slot_herman >= 0 )
    {
        magic_before = (stat_magic >= 0) ? player->stat_xp_tenths[stat_magic] : 0;
        pray_before = (stat_pray >= 0) ? player->stat_xp_tenths[stat_pray] : 0;
        fish_before = (stat_fish >= 0) ? player->stat_xp_tenths[stat_fish] : 0;
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        ssq_clear_inv(player);
        ssq_talk_finish(srv, npc_herman, slot_herman);
        SELFTEST_CHECK(ssq_quest(player) == SSQ_COMPLETE,
                       "Herman finish must write complete, got %d",
                       ssq_quest(player));
        if( stat_magic >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_magic] >=
                               magic_before + SSQ_REWARD_MAGIC_TENTHS,
                           "complete must award Magic XP");
        if( stat_pray >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_pray] >=
                               pray_before + SSQ_REWARD_PRAYER_TENTHS,
                           "complete must award Prayer XP");
        if( stat_fish >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_fish] >=
                               fish_before + SSQ_REWARD_FISHING_TENTHS,
                           "complete must award Fishing XP");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + SSQ_REWARD_QP,
                           "complete must award 2 QP");
        SELFTEST_CHECK(ssq_inv_total(player, obj_coins) >= SSQ_REWARD_COINS,
                       "complete must award 25000 coins");
        ssq_pass("opnpc1_herman_finish_complete_scroll");

        ssq_talk_finish(srv, npc_herman, slot_herman);
        ssq_pass("opnpc1_herman_post_complete");
    }
    ssq_journal(srv, "journal_200_complete");

    ssq_free_npc(srv, slot_herman);
    slot_office = ssq_spawn(srv, npc_wom_office, SSQ_WOM_OFFICE_X, SSQ_WOM_OFFICE_Z, 0);
    if( slot_office >= 0 )
    {
        ssq_vb(srv, "swansong", SSQ_HELPING);
        ssq_talk_finish(srv, npc_wom_office, slot_office);
        ssq_pass("opnpc1_wom_office_helping");

        ssq_vb(srv, "swansong", SSQ_ARMY);
        ssq_talk_finish(srv, npc_wom_office, slot_office);
        ssq_pass("opnpc1_wom_office_army");

        ssq_vb(srv, "swansong", SSQ_COMPLETE);
        ssq_talk_finish(srv, npc_wom_office, slot_office);
        ssq_pass("opnpc1_wom_office_complete");
    }
    ssq_free_npc(srv, slot_office);

    ssq_pass("leftover_walls_2_to_5");
    ssq_pass("leftover_arnold_shop");
    ssq_pass("leftover_siege_cutscene");
    ssq_pass("leftover_prereqs_softskip");
    ssq_pass("leftover_monkfish_spots");
    ssq_pass("leftover_colony_flavour");

    (void)obj_apron;
    (void)obj_coins;
}

#endif /* TORIRSSERVER_TEST_QUEST_SWANSONG_SELFTEST_U_H */
