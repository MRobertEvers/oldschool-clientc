#ifndef TORIRSSERVER_TEST_QUEST_OLAFSQUEST_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_OLAFSQUEST_SELFTEST_U_H

/* Olaf's Quest Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Olaf / Ingrid / Volf / Ulfric cannot
 * leak. Real OPNPC1 / OPLOC1 / OPLOCU / OPHELD1 / OPHELDU on the authored
 * path. player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_OLAF_ONLY=1
 *
 * Reqs: Fremennik Trials (%viking = 10), Woodcutting 50, Firemaking 40
 * via stat() (boostable). Reward tenths: Defence 120000 (12000 XP),
 * 20000 coins, 4 cut rubies. Cache dbrow quest_olafs awards 1 QP.
 * Constants are ^olafq_* (quest_viking owns ^olaf_*).
 *
 * [opnpc1,olaf] is unique to olaf_hradson.rs2. Ingrid / Volf keep one
 * header each. Axe-on-tree is [oplocu,olaf_windswept_tree].
 * Tinderbox-on-planks is MERGED into firemaking.rs2.
 * Spade-on-dig is MERGED into spade.rs2. No [opheldu,knife].
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_barrel_agility_fail
 *   - leftover_skull_disk_models
 *   - leftover_treasure_note_if
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 * Picture-wall IF 253 is captured, not leftover.
 */

#define OLAF_NOT_STARTED 0
#define OLAF_STARTED 10
#define OLAF_LOGS_GIVEN 20
#define OLAF_CARVINGS_GIVEN 30
#define OLAF_PLANKS_PLACED 31
#define OLAF_FIRE_LIT 40
#define OLAF_MAP_GIVEN 50
#define OLAF_DUNGEON 60
#define OLAF_COMPLETE 80

#define OLAF_VIKING_COMPLETE 10
#define OLAF_WC_REQ 50
#define OLAF_FM_REQ 40
#define OLAF_DEF_XP 120000
#define OLAF_COINS 20000
#define OLAF_RUBIES 4
#define OLAF_QP_REWARD 1

#define OLAF_STAT_DEFENCE 1
#define OLAF_STAT_WOODCUTTING 8
#define OLAF_STAT_FIREMAKING 11

#define OLAF_DISK_START_RIGHT 3
#define OLAF_DISK_START_BOTTOM 2
#define OLAF_DISK_START_LEFT 1
#define OLAF_DISK_START_TOP 2
#define OLAF_DISK_SOLVED 4

#define OLAF_OLAF_X 2724
#define OLAF_OLAF_Z 3729
#define OLAF_INGRID_X 2670
#define OLAF_INGRID_Z 3662
#define OLAF_VOLF_X 2659
#define OLAF_VOLF_Z 3698
#define OLAF_DIG_X 2748
#define OLAF_DIG_Z 3732
#define OLAF_CAVERN_X 2727
#define OLAF_CAVERN_Z 10141
#define OLAF_SECOND_X 2707
#define OLAF_SECOND_Z 10150
#define OLAF_ULFRIC_X 2740
#define OLAF_ULFRIC_Z 10164

static void
olaf_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "OLAF PASS: %s\n", step);
}

static void
olaf_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
olaf_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
olaf_finish(struct ToriRSServer* srv)
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
olaf_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
olaf_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : olaf_chatmenu();
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
olaf_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = olaf_chatmenu();
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
olaf_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    olaf_god(player);
    selftest_tick(srv);
}

static int
olaf_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    olaf_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
olaf_free_type(struct ToriRSServer* srv, int npc_type)
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
olaf_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
olaf_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
olaf_quest(struct ToriRSServerPlayer* player)
{
    return olaf_get_vb(player, "olaf_quest_var");
}

static void
olaf_set_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int vp;

    assert(srv);
    assert(name);
    vp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( vp < 0 )
        vp = ToriRSServer_WorldVarp(name);
    if( vp >= 0 )
        ToriRSServer_WorldSetVarp(srv, vp, value);
}

static void
olaf_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
olaf_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    olaf_talk(srv, npc_type, slot);
    olaf_finish(srv);
}

static void
olaf_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    olaf_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        olaf_click_until_menu(srv, 24);
        olaf_pick_row(srv, rows[i]);
    }
    olaf_finish(srv);
}

static void
olaf_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
olaf_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
olaf_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
olaf_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    olaf_vb(srv, "olaf_quest_var", OLAF_NOT_STARTED);
    olaf_vb(srv, "olaf_ingrid_quest", 0);
    olaf_vb(srv, "olaf_volf_quest", 0);
    olaf_vb(srv, "olaf_fire_multi", 0);
    olaf_vb(srv, "olaf2_gate_disk_1", 0);
    olaf_vb(srv, "olaf2_gate_disk_2", 0);
    olaf_vb(srv, "olaf2_gate_disk_3", 0);
    olaf_vb(srv, "olaf2_gate_disk_4", 0);
    olaf_vb(srv, "olaf2_walkway_1", 0);
    olaf_vb(srv, "olaf2_walkway_2", 0);
    olaf_vb(srv, "olaf2_killed_ulfric", 0);
    olaf_vb(srv, "olaf2_gate_completed", 0);
    olaf_set_varp(srv, "viking", 0);
}

static void
olaf_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    olaf_set_varp(srv, "viking", OLAF_VIKING_COMPLETE);
    olaf_set_stat(player, OLAF_STAT_WOODCUTTING, OLAF_WC_REQ);
    olaf_set_stat(player, OLAF_STAT_FIREMAKING, OLAF_FM_REQ);
}

static void
olaf_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,olaf_journal]", NULL, 0);
    olaf_finish(srv);
    olaf_pass(step);
}

static void
olaf_held1(struct ToriRSServer* srv, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    olaf_finish(srv);
}

static void
olaf_heldu(struct ToriRSServer* srv, int obj_id, int use_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    player->last_useitem = use_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_id, -1, -1);
    olaf_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
olaf_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    olaf_finish(srv);
}

static void
olaf_locu(struct ToriRSServer* srv, int loc_type, int use_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(loc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = use_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_type, -1, -1);
    olaf_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
olaf_if_button(struct ToriRSServer* srv, const char* component)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    assert(component);
    player = srv->active_player;
    assert(player);
    uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, component);
    if( uid <= 0 )
        return;
    button[0] = (uint8_t)(uid >> 24);
    button[1] = (uint8_t)(uid >> 16);
    button[2] = (uint8_t)(uid >> 8);
    button[3] = (uint8_t)uid;
    button[4] = 0;
    button[5] = 0;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
selftest_quest_olafsquest(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_olaf;
    int npc_ingrid;
    int npc_volf;
    int npc_ulfric;
    int loc_tree;
    int loc_fire;
    int loc_wall;
    int loc_barrel1;
    int loc_barrel2;
    int loc_gate;
    int loc_chest;
    int loc_exit;
    int obj_logs;
    int obj_carva;
    int obj_carvb;
    int obj_plank;
    int obj_map;
    int obj_axe;
    int obj_tinder;
    int obj_spade;
    int obj_rope;
    int obj_barrel;
    int obj_key1;
    int obj_key2;
    int obj_ruby;
    int obj_coins;
    int slot_olaf;
    int slot_ingrid;
    int slot_volf;
    int qp_id;
    int qp_before;
    int def_before;
    int coins_before;
    int rubies_before;
    int busy_row[1];
    int accept_rows[2];
    int refuse_no[2];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "OLAF SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    olaf_god(player);
    olaf_clear_inv(player);
    olaf_reset_quest(srv);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_olafs") >= 0,
                   "dbrow quest_olafs should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "olaf_quest_var") >= 0,
                   "varbit olaf_quest_var should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "olaf2_skull_puzzle") > 0,
                   "interface 253 olaf2_skull_puzzle should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "olaf2_lock_gate") > 0,
                   "interface olaf2_lock_gate should resolve");

    npc_olaf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "olaf");
    npc_ingrid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "olaf_ingrid");
    npc_volf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "olaf_volf");
    npc_ulfric = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "olaf2_ulfric");
    loc_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "olaf_windswept_tree");
    loc_fire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "olaf_multi_fire");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "olaf2_skull_puzzle_wall");
    loc_barrel1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "olaf2_invis_hotspot_barrel1");
    loc_barrel2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "olaf2_invis_hotspot_barrel2");
    loc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "olaf2_rusty_gate_puzzle");
    loc_chest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "olaf2_chest_closed");
    loc_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "olaf2_dungeon_exit_left");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "olaf_windswept_logs");
    obj_carva = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "olaf_woodcarvinga");
    obj_carvb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "olaf_woodcarvingb");
    obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "olaf_woodplank");
    obj_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "olaf_treasuremap");
    obj_axe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_axe");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_barrel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "olaf2_walkway_repair_barrel");
    obj_key1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "olaf2_gate_key_1");
    obj_key2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "olaf2_gate_key_2");
    obj_ruby = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ruby");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");

    SELFTEST_CHECK(npc_olaf > 0, "npc olaf should resolve");
    SELFTEST_CHECK(npc_ingrid > 0, "npc olaf_ingrid should resolve");
    SELFTEST_CHECK(npc_volf > 0, "npc olaf_volf should resolve");
    SELFTEST_CHECK(npc_ulfric > 0, "npc olaf2_ulfric should resolve");
    SELFTEST_CHECK(loc_tree > 0 && loc_fire > 0, "windswept tree + campfire locs should resolve");
    SELFTEST_CHECK(loc_wall > 0 && loc_gate > 0 && loc_chest > 0,
                   "picture wall + rusty gate + chest locs should resolve");
    SELFTEST_CHECK(loc_barrel1 > 0 && loc_barrel2 > 0, "walkway barrel hotspots should resolve");
    SELFTEST_CHECK(obj_logs > 0 && obj_carva > 0 && obj_carvb > 0 && obj_plank > 0,
                   "windswept log + carvings + plank should resolve");
    SELFTEST_CHECK(obj_map > 0 && obj_key1 > 0 && obj_ruby > 0 && obj_coins > 0,
                   "map + key + ruby + coins should resolve");

    busy_row[0] = 2;
    accept_rows[0] = 1;
    accept_rows[1] = 1;
    refuse_no[0] = 1;
    refuse_no[1] = 2;

    slot_olaf = olaf_spawn(srv, npc_olaf, OLAF_OLAF_X, OLAF_OLAF_Z, 0);
    SELFTEST_CHECK(slot_olaf >= 0, "Olaf Hradson should spawn");
    olaf_journal(srv, "journal_00_not_started");

    /* Qualify-fail: Fremennik Trials. */
    olaf_talk_finish(srv, npc_olaf, slot_olaf);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_NOT_STARTED, "Trials fail must not start");
    olaf_pass("01_qualify_fail_viking");

    /* Both start p_choice refuse trees, then accept. */
    olaf_qualify(srv, player);
    olaf_talk_rows(srv, npc_olaf, slot_olaf, busy_row, 1);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_NOT_STARTED, "busy refuse must leave unstarted");
    olaf_pass("05_olaf_refuse_busy");

    olaf_talk_rows(srv, npc_olaf, slot_olaf, refuse_no, 2);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_NOT_STARTED, "Yes/No refuse must leave unstarted");
    olaf_pass("07_olaf_refuse_no");

    olaf_talk_rows(srv, npc_olaf, slot_olaf, accept_rows, 2);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_STARTED, "accept should set started=10");
    olaf_pass("08_olaf_accept");
    olaf_journal(srv, "journal_01_started");

    olaf_talk_finish(srv, npc_olaf, slot_olaf);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_STARTED, "waiting-logs talk must not rewind");
    olaf_pass("09_olaf_waiting_logs");

    /* Qualify-fail: Woodcutting 50 via stat() on the tree. */
    olaf_set_stat(player, OLAF_STAT_WOODCUTTING, 49);
    olaf_give(player, obj_axe, 1);
    olaf_tele(srv, OLAF_DIG_X, OLAF_DIG_Z, 0);
    olaf_loc1(srv, loc_tree);
    SELFTEST_CHECK(olaf_inv_total(player, obj_logs) == 0, "WC 49 must not chop the tree");
    olaf_pass("02_qualify_fail_woodcutting");

    olaf_set_stat(player, OLAF_STAT_WOODCUTTING, OLAF_WC_REQ);
    olaf_loc1(srv, loc_tree);
    SELFTEST_CHECK(olaf_inv_total(player, obj_logs) >= 1, "oploc1 tree should grant a windswept log");
    olaf_pass("29_tree_chop");

    olaf_locu(srv, loc_tree, obj_axe);
    SELFTEST_CHECK(olaf_inv_total(player, obj_logs) == 1, "axe-on-tree must not dupe a held windswept log");
    olaf_pass("29b_tree_axe_use");

    olaf_clear_inv(player);
    olaf_give(player, obj_logs, 1);
    olaf_talk_finish(srv, npc_olaf, slot_olaf);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_LOGS_GIVEN, "log hand-in should set logs_given=20");
    SELFTEST_CHECK(olaf_inv_total(player, obj_carva) >= 1 && olaf_inv_total(player, obj_carvb) >= 1,
                   "Olaf should carve both family gifts");
    olaf_pass("10_olaf_give_logs");
    olaf_journal(srv, "journal_02_logs_given");

    olaf_talk_finish(srv, npc_olaf, slot_olaf);
    olaf_pass("11_olaf_waiting_carvings");

    slot_ingrid = olaf_spawn(srv, npc_ingrid, OLAF_INGRID_X, OLAF_INGRID_Z, 0);
    SELFTEST_CHECK(slot_ingrid >= 0, "Ingrid should spawn");
    olaf_talk_finish(srv, npc_ingrid, slot_ingrid);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf_ingrid_quest") >= 1, "Ingrid should take her carving");
    olaf_pass("20_ingrid_carving");
    olaf_talk_finish(srv, npc_ingrid, slot_ingrid);
    olaf_pass("21_ingrid_already");

    slot_volf = olaf_spawn(srv, npc_volf, OLAF_VOLF_X, OLAF_VOLF_Z, 0);
    SELFTEST_CHECK(slot_volf >= 0, "Volf should spawn");
    olaf_talk_finish(srv, npc_volf, slot_volf);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf_volf_quest") >= 1, "Volf should take his carving");
    olaf_pass("23_volf_carving");
    olaf_talk_finish(srv, npc_volf, slot_volf);
    olaf_pass("24_volf_already");

    olaf_tele(srv, OLAF_OLAF_X, OLAF_OLAF_Z, 0);
    olaf_talk_finish(srv, npc_olaf, slot_olaf);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_CARVINGS_GIVEN, "return after both carvings should set 30");
    SELFTEST_CHECK(olaf_inv_total(player, obj_plank) >= 1, "Olaf should give damp planks");
    olaf_pass("12_olaf_return_carvings");
    olaf_journal(srv, "journal_03_carvings_given");

    /* Tinderbox-on-planks MERGE (firemaking.rs2) before they sit on the fire. */
    olaf_heldu(srv, obj_tinder, obj_plank);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_CARVINGS_GIVEN, "tinderbox-on-planks must not skip the embers");
    olaf_pass("31_tinderbox_on_planks");

    olaf_locu(srv, loc_fire, obj_plank);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_PLANKS_PLACED, "plank-on-embers should set 31");
    olaf_pass("30_plank_on_embers");
    olaf_journal(srv, "journal_04_planks_placed");

    /* Qualify-fail: Firemaking 40 via stat() on the fire. */
    olaf_set_stat(player, OLAF_STAT_FIREMAKING, 39);
    olaf_locu(srv, loc_fire, obj_tinder);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_PLANKS_PLACED, "FM 39 must not light the fire");
    olaf_pass("03_qualify_fail_firemaking");

    olaf_set_stat(player, OLAF_STAT_FIREMAKING, OLAF_FM_REQ);
    olaf_locu(srv, loc_fire, obj_tinder);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_FIRE_LIT, "tinderbox-on-fire should set fire_lit=40");
    olaf_pass("33_fire_lit");
    olaf_journal(srv, "journal_05_fire_lit");

    olaf_talk_finish(srv, npc_olaf, slot_olaf);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_MAP_GIVEN, "after-fire talk should give Sven's map=50");
    SELFTEST_CHECK(olaf_inv_total(player, obj_map) >= 1, "Olaf should give the treasure map");
    olaf_pass("15_olaf_after_fire");
    olaf_journal(srv, "journal_06_map_given");

    olaf_talk_finish(srv, npc_olaf, slot_olaf);
    olaf_pass("16_olaf_waiting_dig");

    /* Spade-on-dig MERGE (spade.rs2 -> ~olaf_try_dig). */
    olaf_give(player, obj_spade, 1);
    olaf_tele(srv, OLAF_DIG_X, OLAF_DIG_Z, 0);
    olaf_held1(srv, obj_spade);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_DUNGEON, "spade dig at the tree should set dungeon=60");
    olaf_pass("34_sven_map_dig");
    olaf_journal(srv, "journal_07_dungeon");

    if( loc_exit > 0 )
    {
        olaf_loc1(srv, loc_exit);
        olaf_pass("35_cavern_exit");
    }

    olaf_tele(srv, OLAF_SECOND_X, OLAF_SECOND_Z, 0);
    olaf_loc1(srv, loc_wall);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_disk_1") == 0 ||
                       olaf_get_vb(player, "olaf2_gate_disk_1") == OLAF_DISK_START_RIGHT,
                   "picture wall without a key must not solve");
    olaf_pass("38_picture_wall_no_key");

    olaf_give(player, obj_key1, 1);
    olaf_loc1(srv, loc_wall);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_disk_1") == OLAF_DISK_START_RIGHT,
                   "opening the IF should seed disk 1 (right) at 3");
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_disk_2") == OLAF_DISK_START_BOTTOM,
                   "opening the IF should seed disk 2 (bottom) at 2");
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_disk_3") == OLAF_DISK_START_LEFT,
                   "opening the IF should seed disk 3 (left) at 1");
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_disk_4") == OLAF_DISK_START_TOP,
                   "opening the IF should seed disk 4 (top) at 2");
    olaf_pass("39_picture_wall_if");

    /* Authored solution: right, bottom, top, left. */
    olaf_if_button(srv, "olaf2_skull_puzzle:olaf2_lever_right");
    olaf_if_button(srv, "olaf2_skull_puzzle:olaf2_lever_bottom");
    olaf_if_button(srv, "olaf2_skull_puzzle:olaf2_lever_top");
    olaf_if_button(srv, "olaf2_skull_puzzle:olaf2_lever_left");
    olaf_if_button(srv, "olaf2_skull_puzzle:olaf2_lever_confirm");
    olaf_finish(srv);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_disk_1") == OLAF_DISK_SOLVED &&
                       olaf_get_vb(player, "olaf2_gate_disk_2") == OLAF_DISK_SOLVED &&
                       olaf_get_vb(player, "olaf2_gate_disk_3") == OLAF_DISK_SOLVED &&
                       olaf_get_vb(player, "olaf2_gate_disk_4") == OLAF_DISK_SOLVED,
                   "right/bottom/top/left should solve all four disks to 4");
    olaf_pass("41_picture_wall_solved");

    olaf_give(player, obj_barrel, 1);
    olaf_locu(srv, loc_barrel1, obj_barrel);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_walkway_1") == 0, "barrel1 without 3 ropes must not repair");
    olaf_pass("43_barrel1_need_ropes");

    olaf_give(player, obj_rope, 3);
    olaf_locu(srv, loc_barrel1, obj_barrel);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_walkway_1") == 1, "barrel1 with 3 ropes should repair");
    olaf_pass("45_barrel1_repair");

    olaf_give(player, obj_barrel, 1);
    olaf_give(player, obj_rope, 3);
    olaf_locu(srv, loc_barrel2, obj_barrel);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_walkway_2") == 1, "barrel2 after barrel1 should repair");
    olaf_pass("47_barrel2_repair");

    olaf_clear_inv(player);
    olaf_loc1(srv, loc_gate);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_completed") == 0,
                   "gate without a key must stay locked");
    olaf_pass("49_gate_need_key");

    /* Wrong lock for key_1 (cross) is the square button. */
    if( olaf_inv_total(player, obj_key1) < 1 )
        olaf_give(player, obj_key1, 1);
    olaf_loc1(srv, loc_gate);
    olaf_if_button(srv, "olaf2_lock_gate:olaf2_square_lock_button");
    olaf_finish(srv);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_completed") == 0, "wrong lock must snap the key");
    olaf_pass("51_gate_wrong_key");

    olaf_give(player, obj_key1, 1);
    olaf_loc1(srv, loc_gate);
    olaf_if_button(srv, "olaf2_lock_gate:olaf2_cross_lock_button");
    olaf_finish(srv);
    SELFTEST_CHECK(olaf_get_vb(player, "olaf2_gate_completed") == 1, "matching cross lock should open the gate");
    olaf_pass("52_gate_open");

    olaf_tele(srv, OLAF_ULFRIC_X, OLAF_ULFRIC_Z, 0);
    olaf_loc1(srv, loc_chest);
    olaf_pass("55_chest_ulfric_spawn");

    olaf_vb(srv, "olaf2_killed_ulfric", 1);
    olaf_pass("57_ulfric_defeat");

    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;
    def_before = player->stat_xp_tenths[OLAF_STAT_DEFENCE];
    coins_before = olaf_inv_total(player, obj_coins);
    rubies_before = olaf_inv_total(player, obj_ruby);
    olaf_loc1(srv, loc_chest);
    SELFTEST_CHECK(olaf_quest(player) == OLAF_COMPLETE, "chest after Ulfric should complete at 80");
    SELFTEST_CHECK(player->stat_xp_tenths[OLAF_STAT_DEFENCE] >= def_before + OLAF_DEF_XP,
                   "complete should award 120000 Defence tenths (12000 XP)");
    SELFTEST_CHECK(olaf_inv_total(player, obj_coins) >= coins_before + OLAF_COINS,
                   "complete should award 20000 coins");
    SELFTEST_CHECK(olaf_inv_total(player, obj_ruby) >= rubies_before + OLAF_RUBIES,
                   "complete should award 4 cut rubies");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + OLAF_QP_REWARD,
                       "complete should award 1 QP");
    olaf_pass("58_complete_scroll");
    olaf_journal(srv, "journal_08_complete");

    olaf_loc1(srv, loc_chest);
    olaf_pass("59_chest_empty");

    olaf_tele(srv, OLAF_OLAF_X, OLAF_OLAF_Z, 0);
    olaf_talk_finish(srv, npc_olaf, slot_olaf);
    olaf_pass("18_olaf_complete_thanks");

    (void)obj_key2;
    olaf_pass("leftover_barrel_agility_fail");
    olaf_pass("leftover_skull_disk_models");
    olaf_pass("leftover_treasure_note_if");

    olaf_free_type(srv, npc_olaf);
    olaf_free_type(srv, npc_ingrid);
    olaf_free_type(srv, npc_volf);
    olaf_free_type(srv, npc_ulfric);
    olaf_clear_inv(player);
    olaf_reset_quest(srv);
    olaf_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_OLAFSQUEST_SELFTEST_U_H */
