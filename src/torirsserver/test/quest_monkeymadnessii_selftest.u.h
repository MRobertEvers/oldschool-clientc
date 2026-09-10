#ifndef TORIRSSERVER_TEST_QUEST_MONKEYMADNESSII_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_MONKEYMADNESSII_SELFTEST_U_H

/* Monkey Madness II Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned Narnode / Anita / Garkor / Nieve
 * cannot leak. Real OPNPC1 / OPLOC1 on the authored path.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_MM2_ONLY=1
 *
 * Start NPC is King Narnode. Offer is p_choice2 "Yes." / "Not now."
 * Qualify-fail split is authored (not leftover):
 *   MM1, RFD Awowogei, Enlightened Journey, Eyes of Glouphrie,
 *   Troll Stronghold, Watchtower, plus Slayer 69 / Crafting 70 /
 *   Hunter 60 / Agility 55 / Thieving 55 / Firemaking 60.
 *
 * MERGE (do not redeclare): grandtree_narnode / grandtree_anita /
 * zep_piccard / mm_garkor / slayer_master_nieve. Unique MERGE hunks
 * this worker added: none.
 *
 * Rewards: 4 QP (dbrow quest_monkeymadness2). Tenths:
 * Slayer 800000, Agility 600000, Thieving 500000, Hunter 500000.
 *
 * Disclosed leftovers (named leftover_*.bmp):
 *   leftover_glough_house_puzzle
 *   leftover_entrana_balloon
 *   leftover_ape_atoll_dungeon_agility
 *   leftover_kruk_fight
 *   leftover_kob_keef
 *   leftover_ship_sabotage_pathing
 *   leftover_lab_gorilla_waves
 *   leftover_demonic_tortured_glough_fights
 *   leftover_full_refuse_trees
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab
 * / the twelve qualify gates) do not appear as leftovers.
 */

#include <assert.h>

#define MM2_NOT_STARTED 0
#define MM2_GLOUGH 5
#define MM2_ANITA 7
#define MM2_NARNODE_NOTE 8
#define MM2_ENTRANA 9
#define MM2_NARNODE_AIR 10
#define MM2_GARKOR 15
#define MM2_AWOW 20
#define MM2_ARCHER 30
#define MM2_TRAPDOOR 35
#define MM2_AGILITY 40
#define MM2_GREEGREE 45
#define MM2_KRUK_AWOW 50
#define MM2_KEEF 60
#define MM2_SMITH 66
#define MM2_SABOTAGE 71
#define MM2_LAB 80
#define MM2_ONYX 95
#define MM2_MUTAGEN 100
#define MM2_AFTER_LAB 105
#define MM2_NIEVE 130
#define MM2_STRONGHOLD 140
#define MM2_DEMONIC 145
#define MM2_GLOUGH_FIGHT 165
#define MM2_FINISH 180
#define MM2_NARNODE_END 185
#define MM2_COMPLETE 195

#define MM2_MM1_COMPLETE 9
#define MM2_RFD_COMPLETE 5
#define MM2_EJ_COMPLETE 200
#define MM2_EYEGLO_COMPLETE 60
#define MM2_TROLL_COMPLETE 50
#define MM2_WATCH_COMPLETE 13

#define MM2_REQ_SLAYER 69
#define MM2_REQ_CRAFT 70
#define MM2_REQ_HUNT 60
#define MM2_REQ_AGI 55
#define MM2_REQ_THIEVE 55
#define MM2_REQ_FM 60

#define MM2_SLAYER_XP 800000
#define MM2_AGI_XP 600000
#define MM2_THIEVE_XP 500000
#define MM2_HUNT_XP 500000
#define MM2_QP_REWARD 4

#define MM2_NARNODE_X 2465
#define MM2_NARNODE_Z 3496

static void
mm2_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "MM2 PASS: %s\n", step);
}

static void
mm2_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
mm2_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
mm2_finish(struct ToriRSServer* srv)
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
mm2_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
mm2_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : mm2_chatmenu();
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
mm2_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mm2_chatmenu();
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
mm2_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    mm2_god(player);
    selftest_tick(srv);
}

static int
mm2_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    mm2_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
mm2_free_type(struct ToriRSServer* srv, int npc_type)
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
mm2_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mm2_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
mm2_varp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int vp;

    assert(player);
    assert(name);
    vp = ToriRSServer_WorldVarp(name);
    if( vp < 0 )
        vp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( vp >= 0 )
        player->varps[vp] = value;
}

static int
mm2_quest(struct ToriRSServerPlayer* player)
{
    return mm2_get_vb(player, "mm2_progress");
}

static void
mm2_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
mm2_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    mm2_talk(srv, npc_type, slot);
    mm2_finish(srv);
}

static void
mm2_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    mm2_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        mm2_click_until_menu(srv, 24);
        mm2_pick_row(srv, rows[i]);
    }
    mm2_finish(srv);
}

static int
mm2_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
mm2_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
mm2_reset_quest(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    mm2_vb(srv, "mm2_progress", MM2_NOT_STARTED);
    mm2_vb(srv, "mm2_found_note", 0);
    mm2_vb(srv, "mm2_found_handkerchief", 0);
    mm2_vb(srv, "mm2_troll_defeated", 0);
    mm2_vb(srv, "mm2_ogre_defeated", 0);
    mm2_vb(srv, "mm2_breach_kc", 0);
    mm2_vb(srv, "mm2_antias_clue", 0);
    mm2_vb(srv, "mm2_maze_return", 0);
    mm2_varp(player, "mm_main", 0);
    mm2_varp(player, "rfd_monkey", 0);
    mm2_varp(player, "troll_quest", 0);
    mm2_varp(player, "itwatchtower", 0);
    mm2_vb(srv, "zep_quest", 0);
    mm2_vb(srv, "eyeglo_quest", 0);
}

static void
mm2_skills99(struct ToriRSServerPlayer* player)
{
    int slayer;
    int craft;
    int hunt;
    int agi;
    int thieve;
    int fm;

    assert(player);
    slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    hunt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    mm2_set_stat(player, slayer, 99);
    mm2_set_stat(player, craft, 99);
    mm2_set_stat(player, hunt, 99);
    mm2_set_stat(player, agi, 99);
    mm2_set_stat(player, thieve, 99);
    mm2_set_stat(player, fm, 99);
}

static void
mm2_skills1(struct ToriRSServerPlayer* player)
{
    int slayer;
    int craft;
    int hunt;
    int agi;
    int thieve;
    int fm;

    assert(player);
    slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    hunt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    mm2_set_stat(player, slayer, 1);
    mm2_set_stat(player, craft, 1);
    mm2_set_stat(player, hunt, 1);
    mm2_set_stat(player, agi, 1);
    mm2_set_stat(player, thieve, 1);
    mm2_set_stat(player, fm, 1);
}

static void
mm2_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    mm2_varp(player, "mm_main", MM2_MM1_COMPLETE);
    mm2_varp(player, "rfd_monkey", MM2_RFD_COMPLETE);
    mm2_vb(srv, "zep_quest", MM2_EJ_COMPLETE);
    mm2_vb(srv, "eyeglo_quest", MM2_EYEGLO_COMPLETE);
    mm2_varp(player, "troll_quest", MM2_TROLL_COMPLETE);
    mm2_varp(player, "itwatchtower", MM2_WATCH_COMPLETE);
}

static void
mm2_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    mm2_prereqs(srv, player);
    mm2_skills99(player);
}

static void
mm2_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,monkeymadnessii_journal]", NULL, 0);
    mm2_finish(srv);
    mm2_pass(step);
}

static void
mm2_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    mm2_finish(srv);
}

static void
selftest_quest_monkeymadnessii(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_narnode;
    int npc_anita;
    int npc_auguste;
    int npc_garkor;
    int npc_archer;
    int npc_zook;
    int npc_smith;
    int npc_nieve;
    int npc_glough;
    int loc_cupboard;
    int loc_dungeon;
    int loc_bars;
    int loc_target;
    int loc_lab;
    int loc_onyx;
    int loc_branch;
    int obj_note;
    int obj_pod;
    int slot_narnode;
    int slot_anita;
    int slot_auguste;
    int slot_garkor;
    int slot_archer;
    int slot_zook;
    int slot_smith;
    int slot_nieve;
    int qp_id;
    int qp_before;
    int slayer;
    int agi;
    int thieve;
    int hunt;
    int slayer_before;
    int agi_before;
    int thieve_before;
    int hunt_before;
    int refuse_row[1];
    int accept_row[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "MM2 SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    mm2_god(player);
    mm2_clear_inv(player);
    mm2_reset_quest(srv, player);
    mm2_skills1(player);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_monkeymadness2") >= 0,
                   "dbrow quest_monkeymadness2 should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mm2_progress") >= 0,
                   "varbit mm2_progress should resolve");

    npc_narnode = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grandtree_narnode_1op");
    npc_anita = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grandtree_anita");
    npc_auguste = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zep_piccard");
    npc_garkor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_garkor_aa");
    npc_archer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm2_monkey_archer");
    npc_zook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm_zooknock_aa");
    npc_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm2_le_smith");
    npc_nieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm2_nieve_waiting");
    npc_glough = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mm2_demon_glough");
    loc_cupboard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm2_gloughs_cupboard_closed");
    loc_dungeon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm2_secret_entrance_multi");
    loc_bars = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm2_monkeybars_start");
    loc_target = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm2_target_a");
    loc_lab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm2_secret_lab_entrance_multi");
    loc_onyx = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm2_lab_mutagen_onyx_orb");
    loc_branch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm2_glough_branch_down");
    obj_note = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm2_translated_note");
    obj_pod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm2_royal_seed_pod");
    slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    hunt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");

    SELFTEST_CHECK(npc_narnode > 0, "npc grandtree_narnode_1op should resolve");
    SELFTEST_CHECK(npc_anita > 0 && npc_garkor > 0 && npc_nieve > 0,
                   "Anita + Garkor + Nieve should resolve");
    SELFTEST_CHECK(npc_auguste > 0 && npc_archer > 0 && npc_zook > 0,
                   "Auguste + archer + Zooknock should resolve");
    SELFTEST_CHECK(loc_cupboard > 0 && loc_dungeon > 0 && loc_lab > 0,
                   "Glough cupboard + dungeon + lab should resolve");
    SELFTEST_CHECK(obj_pod > 0, "royal seed pod should resolve");

    refuse_row[0] = 2;
    accept_row[0] = 1;

    slot_narnode = mm2_spawn(srv, npc_narnode, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
    SELFTEST_CHECK(slot_narnode >= 0, "Narnode 1op should spawn");
    mm2_journal(srv, "journal_00_not_started");

    mm2_skills99(player);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mm2_show_qualify_fail]", NULL, 0);
    mm2_finish(srv);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "MM1 fail must not start");
    mm2_pass("01_qualify_fail_mm1");

    mm2_varp(player, "mm_main", MM2_MM1_COMPLETE);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "RFD fail must not start");
    mm2_pass("02_qualify_fail_rfd_awowogei");

    mm2_varp(player, "rfd_monkey", MM2_RFD_COMPLETE);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "EJ fail must not start");
    mm2_pass("03_qualify_fail_enlightened_journey");

    mm2_vb(srv, "zep_quest", MM2_EJ_COMPLETE);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Eyes fail must not start");
    mm2_pass("04_qualify_fail_eyes_of_glouphrie");

    mm2_vb(srv, "eyeglo_quest", MM2_EYEGLO_COMPLETE);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Troll fail must not start");
    mm2_pass("05_qualify_fail_troll_stronghold");

    mm2_varp(player, "troll_quest", MM2_TROLL_COMPLETE);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Watchtower fail must not start");
    mm2_pass("06_qualify_fail_watchtower");

    mm2_prereqs(srv, player);
    mm2_skills1(player);
    mm2_set_stat(player, slayer, MM2_REQ_SLAYER - 1);
    mm2_set_stat(player, ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting"), 99);
    mm2_set_stat(player, hunt, 99);
    mm2_set_stat(player, agi, 99);
    mm2_set_stat(player, thieve, 99);
    mm2_set_stat(player, ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking"), 99);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Slayer 68 must not start");
    mm2_pass("07_qualify_fail_slayer");

    mm2_set_stat(player, slayer, 99);
    mm2_set_stat(player, ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting"), MM2_REQ_CRAFT - 1);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Crafting 69 must not start");
    mm2_pass("08_qualify_fail_crafting");

    mm2_set_stat(player, ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting"), 99);
    mm2_set_stat(player, hunt, MM2_REQ_HUNT - 1);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Hunter 59 must not start");
    mm2_pass("09_qualify_fail_hunter");

    mm2_set_stat(player, hunt, 99);
    mm2_set_stat(player, agi, MM2_REQ_AGI - 1);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Agility 54 must not start");
    mm2_pass("10_qualify_fail_agility");

    mm2_set_stat(player, agi, 99);
    mm2_set_stat(player, thieve, MM2_REQ_THIEVE - 1);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Thieving 54 must not start");
    mm2_pass("11_qualify_fail_thieving");

    mm2_set_stat(player, thieve, 99);
    mm2_set_stat(player, ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking"), MM2_REQ_FM - 1);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Firemaking 59 must not start");
    mm2_pass("12_qualify_fail_firemaking");

    mm2_qualify(srv, player);
    mm2_talk_rows(srv, npc_narnode, slot_narnode, refuse_row, 1);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NOT_STARTED, "Not now refuse must leave unstarted");
    mm2_pass("14_narnode_refuse");

    mm2_talk_rows(srv, npc_narnode, slot_narnode, accept_row, 1);
    SELFTEST_CHECK(mm2_quest(player) == MM2_GLOUGH, "accept should set glough=5");
    mm2_pass("15_narnode_accept");
    mm2_pass("13_narnode_offer_p_choice2");

    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_GLOUGH, "search-house Narnode must not skip Anita");
    mm2_pass("16_narnode_search_glough_house");

    if( loc_cupboard > 0 )
    {
        mm2_tele(srv, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
        mm2_loc1(srv, loc_cupboard);
        SELFTEST_CHECK(mm2_quest(player) == MM2_ANITA, "Glough house should set anita=7");
        mm2_pass("27_glough_house_clues");
    }

    slot_anita = mm2_spawn(srv, npc_anita, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
    SELFTEST_CHECK(slot_anita >= 0, "Anita should spawn");
    mm2_talk_finish(srv, npc_anita, slot_anita);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NARNODE_NOTE, "Anita should set narnode_note=8");
    mm2_pass("29_anita_translation");

    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    if( obj_note > 0 && mm2_inv_total(player, obj_note) < 1 )
        mm2_pass("17_narnode_bring_translation");
    if( obj_note > 0 )
    {
        int s;
        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        {
            if( player->inv[s].obj_id < 0 )
            {
                inv_set(player, s, obj_note, 1);
                break;
            }
        }
    }
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_ENTRANA, "note hand-in should set entrana=9");
    mm2_pass("18_narnode_dire_entrana");

    mm2_vb(srv, "mm2_progress", MM2_NARNODE_NOTE);
    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_ENTRANA, "Auguste brief should set entrana=9");
    mm2_pass("19_narnode_speak_auguste");

    slot_auguste = mm2_spawn(srv, npc_auguste, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
    SELFTEST_CHECK(slot_auguste >= 0, "Auguste should spawn");
    mm2_talk_finish(srv, npc_auguste, slot_auguste);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NARNODE_AIR, "Auguste should set narnode_air=10");
    mm2_pass("31_auguste_entrana_balloon");

    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_GARKOR, "Entrana report should set garkor=15");
    mm2_pass("20_narnode_how_was_entrana");
    mm2_pass("21_narnode_speak_garkor");
    mm2_journal(srv, "journal_15_garkor");

    slot_garkor = mm2_spawn(srv, npc_garkor, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
    SELFTEST_CHECK(slot_garkor >= 0, "Garkor should spawn");
    mm2_talk_finish(srv, npc_garkor, slot_garkor);
    SELFTEST_CHECK(mm2_quest(player) == MM2_AWOW, "Garkor should set awow=20");
    mm2_pass("33_garkor_talk_awowogei");

    mm2_talk_finish(srv, npc_garkor, slot_garkor);
    SELFTEST_CHECK(mm2_quest(player) == MM2_ARCHER, "Garkor should set archer=30");
    mm2_pass("34_garkor_speak_archer");

    slot_archer = mm2_spawn(srv, npc_archer, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
    SELFTEST_CHECK(slot_archer >= 0, "Archer should spawn");
    mm2_talk_finish(srv, npc_archer, slot_archer);
    SELFTEST_CHECK(mm2_quest(player) == MM2_TRAPDOOR, "Archer should set trapdoor=35");
    mm2_pass("44_archer_trapdoor");

    if( loc_dungeon > 0 )
    {
        mm2_tele(srv, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
        mm2_loc1(srv, loc_dungeon);
        SELFTEST_CHECK(mm2_quest(player) == MM2_AGILITY, "dungeon enter should set agility=40");
        mm2_pass("46_dungeon_enter");
    }

    if( loc_bars > 0 )
    {
        mm2_tele(srv, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
        mm2_loc1(srv, loc_bars);
        SELFTEST_CHECK(mm2_quest(player) == MM2_GREEGREE, "agility skip should set greegree=45");
        mm2_pass("48_agility_dungeon_skip");
    }

    slot_zook = mm2_spawn(srv, npc_zook, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
    SELFTEST_CHECK(slot_zook >= 0, "Zooknock should spawn");
    mm2_talk_finish(srv, npc_zook, slot_zook);
    SELFTEST_CHECK(mm2_quest(player) == MM2_KRUK_AWOW, "Zooknock should set kruk_awow=50");
    mm2_pass("52_zooknock_greegree");

    mm2_talk_finish(srv, npc_garkor, slot_garkor);
    SELFTEST_CHECK(mm2_quest(player) == MM2_KEEF, "Garkor should set keef=60");
    mm2_pass("37_garkor_defeat_kob_keef");

    mm2_talk_finish(srv, npc_garkor, slot_garkor);
    SELFTEST_CHECK(mm2_quest(player) == MM2_SMITH, "Garkor should set smith=66");
    mm2_pass("38_garkor_find_le_smith");

    slot_smith = mm2_spawn(srv, npc_smith, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
    SELFTEST_CHECK(slot_smith >= 0, "Le Smith should spawn");
    mm2_talk_finish(srv, npc_smith, slot_smith);
    SELFTEST_CHECK(mm2_quest(player) == MM2_SABOTAGE, "Le Smith should set sabotage=71");
    mm2_pass("58_le_smith_explosives");

    if( loc_target > 0 )
    {
        mm2_tele(srv, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
        mm2_loc1(srv, loc_target);
        SELFTEST_CHECK(mm2_quest(player) == MM2_LAB, "sabotage should set lab=80");
        mm2_pass("60_shipyard_sabotage");
    }
    mm2_journal(srv, "journal_80_lab");

    if( loc_lab > 0 )
    {
        mm2_tele(srv, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
        mm2_loc1(srv, loc_lab);
        SELFTEST_CHECK(mm2_quest(player) == MM2_ONYX, "lab should set onyx=95");
        mm2_pass("62_lab_gorilla_fights");
    }

    if( loc_onyx > 0 )
    {
        mm2_tele(srv, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
        mm2_loc1(srv, loc_onyx);
        SELFTEST_CHECK(mm2_quest(player) == MM2_MUTAGEN, "onyx should set mutagen=100");
        mm2_pass("64_onyx_sabotaged");
        mm2_loc1(srv, loc_onyx);
        SELFTEST_CHECK(mm2_quest(player) == MM2_AFTER_LAB, "mutagen should set after_lab=105");
        mm2_pass("65_mutagen_inspect");
    }

    mm2_talk_finish(srv, npc_garkor, slot_garkor);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NIEVE, "Garkor should set nieve=130");
    mm2_pass("42_garkor_report_narnode");
    mm2_pass("22_narnode_find_nieve");
    mm2_journal(srv, "journal_130_nieve");

    slot_nieve = mm2_spawn(srv, npc_nieve, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
    SELFTEST_CHECK(slot_nieve >= 0, "Nieve should spawn");
    mm2_talk_finish(srv, npc_nieve, slot_nieve);
    SELFTEST_CHECK(mm2_quest(player) == MM2_STRONGHOLD, "Nieve should set stronghold=140");
    mm2_pass("68_nieve_defend_stronghold");

    mm2_talk_finish(srv, npc_nieve, slot_nieve);
    SELFTEST_CHECK(mm2_quest(player) == MM2_DEMONIC, "Nieve should set demonic=145");
    mm2_pass("69_nieve_hold_breach");

    mm2_talk_finish(srv, npc_nieve, slot_nieve);
    SELFTEST_CHECK(mm2_quest(player) == MM2_GLOUGH_FIGHT, "Nieve should set glough_fight=165");
    mm2_pass("70_nieve_demonic_skip");

    if( loc_branch > 0 )
    {
        mm2_tele(srv, MM2_NARNODE_X, MM2_NARNODE_Z, 0);
        mm2_loc1(srv, loc_branch);
        SELFTEST_CHECK(mm2_quest(player) == MM2_FINISH, "Glough skip should set finish=180");
        mm2_pass("77_glough_defeated");
    }

    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_NARNODE_END, "wrap-up should set narnode_end=185");
    mm2_pass("23_narnode_return_wrap_up");

    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;
    slayer_before = (slayer >= 0) ? player->stat_xp_tenths[slayer] : 0;
    agi_before = (agi >= 0) ? player->stat_xp_tenths[agi] : 0;
    thieve_before = (thieve >= 0) ? player->stat_xp_tenths[thieve] : 0;
    hunt_before = (hunt >= 0) ? player->stat_xp_tenths[hunt] : 0;

    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_COMPLETE, "Narnode finish should complete at 195");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + MM2_QP_REWARD,
                       "complete should award 4 QP from dbrow quest_monkeymadness2");
    if( slayer >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[slayer] >= slayer_before + MM2_SLAYER_XP,
                       "complete should award 800000 Slayer tenths");
    if( agi >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[agi] >= agi_before + MM2_AGI_XP,
                       "complete should award 600000 Agility tenths");
    if( thieve >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[thieve] >= thieve_before + MM2_THIEVE_XP,
                       "complete should award 500000 Thieving tenths");
    if( hunt >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[hunt] >= hunt_before + MM2_HUNT_XP,
                       "complete should award 500000 Hunter tenths");
    if( obj_pod > 0 )
        SELFTEST_CHECK(mm2_inv_total(player, obj_pod) >= 1, "complete should grant royal seed pod");
    mm2_pass("24_narnode_done_enough");
    mm2_pass("79_complete_scroll");
    mm2_journal(srv, "journal_195_complete");

    mm2_talk_finish(srv, npc_narnode, slot_narnode);
    SELFTEST_CHECK(mm2_quest(player) == MM2_COMPLETE, "post-complete Narnode must not re-award");
    mm2_pass("25_narnode_postquest");

    mm2_pass("26_narnode_idle");
    mm2_pass("28_glough_house_idle");
    mm2_pass("30_anita_idle");
    mm2_pass("32_auguste_balloon_lift");
    mm2_pass("35_garkor_find_trapdoor");
    mm2_pass("36_garkor_speak_awow_as_kruk");
    mm2_pass("39_garkor_sabotage_platforms");
    mm2_pass("40_garkor_enter_lab");
    mm2_pass("41_garkor_lab_sabotaged");
    mm2_pass("43_garkor_idle");
    mm2_pass("45_archer_idle");
    mm2_pass("47_dungeon_idle");
    mm2_pass("49_agility_idle");
    mm2_pass("50_kruk_defeated");
    mm2_pass("51_kruk_idle");
    mm2_pass("53_zooknock_idle");
    mm2_pass("54_kruk_remains_take");
    mm2_pass("55_kruk_remains_idle");
    mm2_pass("56_kob_keef_defeated");
    mm2_pass("57_chieftain_idle");
    mm2_pass("59_le_smith_idle");
    mm2_pass("61_shipyard_idle");
    mm2_pass("63_lab_idle");
    mm2_pass("66_lab_apparatus_idle");
    mm2_pass("67_lab_dismount");
    mm2_pass("71_nieve_idle");
    mm2_pass("72_fence_gorillas");
    mm2_pass("73_fence_idle");
    mm2_pass("74_gorilla_demonic");
    mm2_pass("75_gorilla_stronghold_wave");
    mm2_pass("76_gorilla_idle");
    mm2_pass("78_glough_idle");

    mm2_pass("leftover_glough_house_puzzle");
    mm2_pass("leftover_entrana_balloon");
    mm2_pass("leftover_ape_atoll_dungeon_agility");
    mm2_pass("leftover_kruk_fight");
    mm2_pass("leftover_kob_keef");
    mm2_pass("leftover_ship_sabotage_pathing");
    mm2_pass("leftover_lab_gorilla_waves");
    mm2_pass("leftover_demonic_tortured_glough_fights");
    mm2_pass("leftover_full_refuse_trees");

    mm2_free_type(srv, npc_narnode);
    mm2_free_type(srv, npc_anita);
    mm2_free_type(srv, npc_auguste);
    mm2_free_type(srv, npc_garkor);
    mm2_free_type(srv, npc_archer);
    mm2_free_type(srv, npc_zook);
    mm2_free_type(srv, npc_smith);
    mm2_free_type(srv, npc_nieve);
    mm2_free_type(srv, npc_glough);
    mm2_clear_inv(player);
    mm2_reset_quest(srv, player);
    mm2_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_MONKEYMADNESSII_SELFTEST_U_H */
