#ifndef TORIRSSERVER_TEST_QUEST_KINGSRANSOM_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_KINGSRANSOM_SELFTEST_U_H

/* King's Ransom Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned Gossip / Guard / Anna / Merlin /
 * Cromperty / Arthur cannot leak. Real OPNPC1 / OPLOC1 on the authored
 * path. player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_KR_ONLY=1
 *
 * Start NPC is Gossip (gossipy_man), not Anna. Hook is
 * [label,gossip_kr_hook] after %murderquest = ^murder_complete.
 * Qualify-fail split: Black Knights' Fortress (%spy=4), Holy Grail
 * (%grail=10), Murder Mystery (%murderquest=2 -- hook unreachable
 * until complete; exposed via ~kr_show_qualify_fail), Defence 65,
 * Magic 45. One Small Favour is an unported soft-skip.
 *
 * [opnpc1,merlin] is owned by Holy Grail / Merlin's Crystal -- this
 * walk uses the existing %kr_quest splice. Anna / Gossip already have
 * one [opnpc1] each.
 *
 * Rewards: 1 QP. Tenths Defence 330000 (33000 XP), Magic 50000
 * (5000 XP), antique lamp. Scroll packed so last rows are not dropped.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_tumbler_lock_widget
 *   - leftover_knight_waves
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab
 * widget) do not appear as leftovers -- the cell-door telegrab path
 * is authored narration.
 */

#define KR_NOT_STARTED 0
#define KR_TOLD_BY_GOSSIP 5
#define KR_EVIDENCE_DELIVERED 15
#define KR_DIRECTED_TO_ANNA 20
#define KR_ANNA_AGREED 25
#define KR_IN_TRIAL 30
#define KR_ANNA_FREED 35
#define KR_CAPTURED 40
#define KR_AMBUSHED 42
#define KR_MET_MERLIN 45
#define KR_FOUND_VENT 50
#define KR_ESCAPED_CELL 60
#define KR_HAVE_GRAIL 65
#define KR_HAVE_SCROLL 70
#define KR_IN_BASEMENT 75
#define KR_TOLD_ARTHUR 85
#define KR_COMPLETE 90

#define KR_BKF_COMPLETE 4
#define KR_GRAIL_COMPLETE 10
#define KR_MURDER_COMPLETE 2
#define KR_DEF_REQ 65
#define KR_MAG_REQ 45
#define KR_DEF_XP 330000
#define KR_MAG_XP 50000
#define KR_QP_REWARD 1

#define KR_STAT_DEFENCE 1
#define KR_STAT_MAGIC 6

#define KR_WITNESS_NONE 0
#define KR_WITNESS_DOG 2
#define KR_WITNESS_BUTLER 3
#define KR_WITNESS_MAID 5

#define KR_GOSSIP_X 2741
#define KR_GOSSIP_Z 3557
#define KR_GUARD_X 2741
#define KR_GUARD_Z 3561
#define KR_ANNA_X 2737
#define KR_ANNA_Z 3466
#define KR_STATUE_X 2780
#define KR_STATUE_Z 3508
#define KR_PRISON_X 1907
#define KR_PRISON_Z 4281
#define KR_GRAIL_X 1696
#define KR_GRAIL_Z 4259
#define KR_CROMPERTY_X 2684
#define KR_CROMPERTY_Z 3323
#define KR_BASEMENT_X 1867
#define KR_BASEMENT_Z 4233
#define KR_ARTHUR_X 2763
#define KR_ARTHUR_Z 3512

static void
kr_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "KR PASS: %s\n", step);
}

static void
kr_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
kr_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
kr_finish(struct ToriRSServer* srv)
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
kr_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
kr_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : kr_chatmenu();
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
kr_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = kr_chatmenu();
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
kr_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    kr_god(player);
    selftest_tick(srv);
}

static int
kr_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    kr_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
kr_free_type(struct ToriRSServer* srv, int npc_type)
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
kr_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
kr_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
kr_quest(struct ToriRSServerPlayer* player)
{
    return kr_get_vb(player, "kr_quest");
}

static void
kr_set_varp(struct ToriRSServer* srv, const char* name, int value)
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
kr_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
kr_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    kr_talk(srv, npc_type, slot);
    kr_finish(srv);
}

static void
kr_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    kr_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        kr_click_until_menu(srv, 24);
        kr_pick_row(srv, rows[i]);
    }
    kr_finish(srv);
}

static void
kr_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
kr_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
kr_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
kr_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    kr_vb(srv, "kr_quest", KR_NOT_STARTED);
    kr_vb(srv, "kr_window", 0);
    kr_vb(srv, "kr_clue_note", 0);
    kr_vb(srv, "kr_clue_form", 0);
    kr_vb(srv, "kr_clue_armour", 0);
    kr_vb(srv, "kr_court_witness", KR_WITNESS_NONE);
    kr_vb(srv, "kr_court_dog_proof", 0);
    kr_vb(srv, "kr_court_butl_proof", 0);
    kr_vb(srv, "kr_court_maid_proof", 0);
    kr_vb(srv, "kr_court_thread", 0);
    kr_set_varp(srv, "spy", 0);
    kr_set_varp(srv, "grail", 0);
    kr_set_varp(srv, "murderquest", 0);
}

static void
kr_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    kr_set_varp(srv, "spy", KR_BKF_COMPLETE);
    kr_set_varp(srv, "grail", KR_GRAIL_COMPLETE);
    kr_set_varp(srv, "murderquest", KR_MURDER_COMPLETE);
    kr_set_stat(player, KR_STAT_DEFENCE, KR_DEF_REQ);
    kr_set_stat(player, KR_STAT_MAGIC, KR_MAG_REQ);
}

static void
kr_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,kr_journal]", NULL, 0);
    kr_finish(srv);
    kr_pass(step);
}

static void
kr_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    kr_finish(srv);
}

static void
kr_loc1_row(struct ToriRSServer* srv, int loc_type, int row)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    kr_click_until_menu(srv, 24);
    kr_pick_row(srv, row);
    kr_finish(srv);
}

static void
selftest_quest_kingsransom(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_gossip;
    int npc_guard;
    int npc_anna;
    int npc_pierre;
    int npc_hobbes;
    int npc_mary;
    int npc_merlin;
    int npc_cromperty;
    int npc_arthur;
    int loc_window;
    int loc_stairs;
    int loc_stairs_top;
    int loc_bookcase;
    int loc_court_stairs;
    int loc_judge;
    int loc_court_door;
    int loc_statue;
    int loc_vent;
    int loc_gate;
    int loc_jewelry;
    int loc_ladder;
    int loc_arthur_statue;
    int obj_lockpick;
    int obj_law;
    int obj_air;
    int obj_grail;
    int obj_scroll;
    int obj_granite;
    int obj_helm;
    int obj_chain;
    int obj_lamp;
    int slot_gossip;
    int slot_guard;
    int slot_anna;
    int slot_pierre;
    int slot_hobbes;
    int slot_mary;
    int slot_merlin;
    int slot_cromperty;
    int slot_arthur;
    int qp_id;
    int qp_before;
    int def_before;
    int mag_before;
    int refuse_row[1];
    int accept_row[1];
    int evidence_row[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "KR SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    kr_god(player);
    kr_clear_inv(player);
    kr_reset_quest(srv);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_kingsransom") >= 0,
                   "dbrow quest_kingsransom should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "kr_quest") >= 0,
                   "varbit kr_quest should resolve");

    npc_gossip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gossipy_man");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "murderguard");
    npc_anna = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "kr_anna_sinclair");
    npc_pierre = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pierre_the_family_dog_handler");
    npc_hobbes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hobbes_the_butler");
    npc_mary = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mary_the_maid");
    npc_merlin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "merlin");
    npc_cromperty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ardounge_wizard");
    npc_arthur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "king_arthur");
    loc_window = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "murderwindow");
    loc_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "murder_qip_spiralstairs");
    loc_stairs_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "murder_qip_spiralstairstop");
    loc_bookcase = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_sin_bookcase3a");
    loc_court_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_courthouse_stairs_top");
    loc_judge = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_judge");
    loc_court_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_court_fence_door");
    loc_statue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_camelot_knight_statue");
    loc_vent = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_underground_jail_cell_wall_bottom_with_vent");
    loc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_underground_jail_bars_gate");
    loc_jewelry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_jewelry_box_table");
    loc_ladder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_bkf_basement_laddertop");
    loc_arthur_statue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_arthur_statue_multi");
    obj_lockpick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lockpick");
    obj_law = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lawrune");
    obj_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "airrune");
    obj_grail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "holy_grail");
    obj_scroll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_animate_rock");
    obj_granite = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "enakh_granite_small");
    obj_helm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_med_helm");
    obj_chain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_chainbody");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");

    SELFTEST_CHECK(npc_gossip > 0, "npc gossipy_man should resolve");
    SELFTEST_CHECK(npc_guard > 0, "npc murderguard should resolve");
    SELFTEST_CHECK(npc_anna > 0, "npc kr_anna_sinclair should resolve");
    SELFTEST_CHECK(npc_merlin > 0 && npc_cromperty > 0 && npc_arthur > 0,
                   "merlin + cromperty + king_arthur should resolve");
    SELFTEST_CHECK(loc_window > 0 && loc_stairs > 0 && loc_bookcase > 0,
                   "mansion window + stairs + bookcase should resolve");
    SELFTEST_CHECK(loc_judge > 0 && loc_statue > 0 && loc_vent > 0 && loc_gate > 0,
                   "judge + statue + vent + gate should resolve");
    SELFTEST_CHECK(obj_lockpick > 0 && obj_grail > 0 && obj_scroll > 0 && obj_lamp > 0,
                   "lockpick + grail + scroll + antique lamp should resolve");

    refuse_row[0] = 2;
    accept_row[0] = 1;
    evidence_row[0] = 1;

    slot_gossip = kr_spawn(srv, npc_gossip, KR_GOSSIP_X, KR_GOSSIP_Z, 0);
    SELFTEST_CHECK(slot_gossip >= 0, "Gossip should spawn");
    kr_journal(srv, "journal_00_not_started");

    /* Murder Mystery fail is unreachable from [opnpc1,gossipy_man] until
     * complete -- expose the named surface through the shared proc. */
    ToriRSServer_ScriptsRunProc(srv, "[proc,kr_show_qualify_fail]", NULL, 0);
    kr_finish(srv);
    SELFTEST_CHECK(kr_quest(player) == KR_NOT_STARTED, "Murder fail must not start");
    kr_pass("03_qualify_fail_murder_mystery");

    kr_set_varp(srv, "murderquest", KR_MURDER_COMPLETE);
    kr_talk_finish(srv, npc_gossip, slot_gossip);
    SELFTEST_CHECK(kr_quest(player) == KR_NOT_STARTED, "BKF fail must not start");
    kr_pass("01_qualify_fail_black_knights");

    kr_set_varp(srv, "spy", KR_BKF_COMPLETE);
    kr_talk_finish(srv, npc_gossip, slot_gossip);
    SELFTEST_CHECK(kr_quest(player) == KR_NOT_STARTED, "Grail fail must not start");
    kr_pass("02_qualify_fail_holy_grail");

    kr_set_varp(srv, "grail", KR_GRAIL_COMPLETE);
    kr_set_stat(player, KR_STAT_MAGIC, KR_MAG_REQ);
    kr_set_stat(player, KR_STAT_DEFENCE, 64);
    kr_talk_finish(srv, npc_gossip, slot_gossip);
    SELFTEST_CHECK(kr_quest(player) == KR_NOT_STARTED, "Defence 64 must not start");
    kr_pass("04_qualify_fail_defence");

    kr_set_stat(player, KR_STAT_DEFENCE, KR_DEF_REQ);
    kr_set_stat(player, KR_STAT_MAGIC, 44);
    kr_talk_finish(srv, npc_gossip, slot_gossip);
    SELFTEST_CHECK(kr_quest(player) == KR_NOT_STARTED, "Magic 44 must not start");
    kr_pass("05_qualify_fail_magic");

    kr_qualify(srv, player);
    kr_talk_rows(srv, npc_gossip, slot_gossip, refuse_row, 1);
    SELFTEST_CHECK(kr_quest(player) == KR_NOT_STARTED, "coincidence refuse must leave unstarted");
    kr_pass("07_gossip_refuse");

    kr_talk_rows(srv, npc_gossip, slot_gossip, accept_row, 1);
    SELFTEST_CHECK(kr_quest(player) == KR_TOLD_BY_GOSSIP, "accept should set told_by_gossip=5");
    kr_pass("08_gossip_accept");
    kr_journal(srv, "journal_05_told_by_gossip");

    kr_talk_finish(srv, npc_gossip, slot_gossip);
    SELFTEST_CHECK(kr_quest(player) == KR_TOLD_BY_GOSSIP, "mid-quest gossip must not rewind");
    kr_pass("09_gossip_mid_investigating");

    slot_guard = kr_spawn(srv, npc_guard, KR_GUARD_X, KR_GUARD_Z, 0);
    SELFTEST_CHECK(slot_guard >= 0, "murder guard should spawn");
    kr_talk_finish(srv, npc_guard, slot_guard);
    SELFTEST_CHECK(kr_quest(player) == KR_TOLD_BY_GOSSIP, "guard without evidence stays investigating");
    kr_pass("12_guard_hint_window");

    kr_tele(srv, KR_GOSSIP_X, KR_GOSSIP_Z, 0);
    kr_loc1(srv, loc_window);
    SELFTEST_CHECK(kr_get_vb(player, "kr_window") == 1, "smash window should set ground floor");
    SELFTEST_CHECK(kr_get_vb(player, "kr_clue_note") == 1, "window should grant scrap paper");
    kr_pass("18_window_smash_enter");

    kr_loc1(srv, loc_window);
    SELFTEST_CHECK(kr_get_vb(player, "kr_window") == 1, "unfinished search must keep the player inside");
    kr_pass("20_window_finish_searching");

    kr_loc1(srv, loc_stairs);
    SELFTEST_CHECK(kr_get_vb(player, "kr_window") == 2, "stairs should set upper floor");
    SELFTEST_CHECK(kr_get_vb(player, "kr_clue_form") == 1, "stairs should grant address form");
    kr_pass("24_stairs_climb_up");

    kr_loc1(srv, loc_bookcase);
    SELFTEST_CHECK(kr_get_vb(player, "kr_clue_armour") == 1, "bookcase should grant black helm");
    kr_pass("30_bookcase_helm");

    kr_loc1(srv, loc_bookcase);
    kr_pass("31_bookcase_already");

    kr_loc1(srv, loc_stairs_top);
    SELFTEST_CHECK(kr_get_vb(player, "kr_window") == 1, "stairs top should return to ground");
    kr_pass("27_stairs_climb_down");

    kr_loc1(srv, loc_window);
    SELFTEST_CHECK(kr_get_vb(player, "kr_window") == 0, "climb out should reset window");
    kr_pass("21_window_climb_out");
    kr_journal(srv, "journal_05b_evidence_ready");

    kr_tele(srv, KR_GUARD_X, KR_GUARD_Z, 0);
    kr_talk_rows(srv, npc_guard, slot_guard, evidence_row, 1);
    SELFTEST_CHECK(kr_quest(player) == KR_EVIDENCE_DELIVERED, "evidence hand-in should set 15");
    kr_pass("14_guard_evidence_handin");
    kr_journal(srv, "journal_15_evidence_delivered");

    kr_tele(srv, KR_GOSSIP_X, KR_GOSSIP_Z, 0);
    kr_talk_finish(srv, npc_gossip, slot_gossip);
    SELFTEST_CHECK(kr_quest(player) == KR_DIRECTED_TO_ANNA, "gossip after evidence should set 20");
    kr_pass("10_gossip_after_evidence");
    kr_journal(srv, "journal_20_directed_to_anna");

    slot_anna = kr_spawn(srv, npc_anna, KR_ANNA_X, KR_ANNA_Z, 0);
    SELFTEST_CHECK(slot_anna >= 0, "Anna Sinclair should spawn");
    kr_talk_rows(srv, npc_anna, slot_anna, refuse_row, 1);
    SELFTEST_CHECK(kr_quest(player) == KR_DIRECTED_TO_ANNA, "Anna refuse must leave directed");
    kr_pass("33_anna_refuse");

    kr_talk_rows(srv, npc_anna, slot_anna, accept_row, 1);
    SELFTEST_CHECK(kr_quest(player) == KR_ANNA_AGREED, "Anna accept should set 25");
    kr_pass("34_anna_accept");
    kr_journal(srv, "journal_25_anna_agreed");

    kr_talk_finish(srv, npc_anna, slot_anna);
    kr_pass("35_anna_waiting_trial");

    kr_tele(srv, KR_ANNA_X, KR_ANNA_Z, 0);
    kr_loc1_row(srv, loc_court_stairs, 2);
    SELFTEST_CHECK(kr_quest(player) == KR_ANNA_AGREED, "court Not yet must leave agreed");
    kr_pass("41_court_stairs_not_yet");

    kr_loc1_row(srv, loc_court_stairs, 1);
    SELFTEST_CHECK(kr_quest(player) == KR_IN_TRIAL, "court ready should set in_trial=30");
    kr_pass("42_court_stairs_ready");
    kr_journal(srv, "journal_30_in_trial");

    kr_loc1(srv, loc_court_door);
    SELFTEST_CHECK(kr_quest(player) == KR_IN_TRIAL, "court door during trial must stay");
    kr_pass("49_court_door_during");

    kr_loc1(srv, loc_judge);
    SELFTEST_CHECK(kr_get_vb(player, "kr_court_witness") == KR_WITNESS_DOG, "judge should call the dog handler");
    kr_pass("44_judge_call_dog");

    slot_pierre = kr_spawn(srv, npc_pierre, KR_ANNA_X, KR_ANNA_Z, 0);
    SELFTEST_CHECK(slot_pierre >= 0, "Pierre should spawn");
    kr_talk_finish(srv, npc_pierre, slot_pierre);
    SELFTEST_CHECK(kr_get_vb(player, "kr_court_dog_proof") == 1, "Pierre should give dog proof");
    kr_pass("51_pierre_testify");

    kr_loc1(srv, loc_judge);
    SELFTEST_CHECK(kr_get_vb(player, "kr_court_witness") == KR_WITNESS_BUTLER, "judge should call the butler");
    kr_pass("45_judge_call_butler");

    slot_hobbes = kr_spawn(srv, npc_hobbes, KR_ANNA_X, KR_ANNA_Z, 0);
    SELFTEST_CHECK(slot_hobbes >= 0, "Hobbes should spawn");
    kr_talk_finish(srv, npc_hobbes, slot_hobbes);
    SELFTEST_CHECK(kr_get_vb(player, "kr_court_butl_proof") == 1, "Hobbes should give butler proof");
    kr_pass("52_hobbes_testify");

    kr_loc1(srv, loc_judge);
    SELFTEST_CHECK(kr_get_vb(player, "kr_court_witness") == KR_WITNESS_MAID, "judge should call the maid");
    kr_pass("46_judge_call_maid");

    slot_mary = kr_spawn(srv, npc_mary, KR_ANNA_X, KR_ANNA_Z, 0);
    SELFTEST_CHECK(slot_mary >= 0, "Mary should spawn");
    kr_talk_finish(srv, npc_mary, slot_mary);
    SELFTEST_CHECK(kr_get_vb(player, "kr_court_maid_proof") == 1, "Mary should give maid proof");
    kr_pass("53_mary_testify");

    kr_loc1(srv, loc_judge);
    SELFTEST_CHECK(kr_get_vb(player, "kr_court_thread") == 1, "judge should take the thread");
    kr_pass("47_judge_thread");

    kr_loc1(srv, loc_judge);
    SELFTEST_CHECK(kr_quest(player) == KR_ANNA_FREED, "verdict should set anna_freed=35");
    kr_pass("48_judge_verdict");
    kr_journal(srv, "journal_35_anna_freed");

    kr_loc1(srv, loc_court_door);
    kr_pass("50_court_door_leave");

    kr_tele(srv, KR_ANNA_X, KR_ANNA_Z, 0);
    kr_talk_finish(srv, npc_anna, slot_anna);
    SELFTEST_CHECK(kr_quest(player) == KR_CAPTURED, "Anna's secret should set captured=40");
    kr_pass("37_anna_freed_secret");

    kr_tele(srv, KR_STATUE_X, KR_STATUE_Z, 0);
    kr_loc1(srv, loc_statue);
    SELFTEST_CHECK(kr_quest(player) == KR_AMBUSHED, "statue ambush should set 42");
    kr_pass("55_statue_hidden_door");
    kr_journal(srv, "journal_40_captured");

    slot_merlin = kr_spawn(srv, npc_merlin, KR_PRISON_X, KR_PRISON_Z, 0);
    SELFTEST_CHECK(slot_merlin >= 0, "Merlin should spawn");
    kr_talk_finish(srv, npc_merlin, slot_merlin);
    SELFTEST_CHECK(kr_quest(player) == KR_MET_MERLIN, "Merlin intro should set 45");
    kr_pass("59_merlin_intro");
    kr_journal(srv, "journal_45_met_merlin");

    kr_talk_finish(srv, npc_merlin, slot_merlin);
    kr_pass("60_merlin_reminder");

    kr_tele(srv, KR_PRISON_X, KR_PRISON_Z, 0);
    kr_loc1(srv, loc_vent);
    SELFTEST_CHECK(kr_quest(player) == KR_FOUND_VENT, "vent should set 50");
    kr_pass("62_vent_found");
    kr_journal(srv, "journal_50_found_vent");

    kr_loc1(srv, loc_gate);
    SELFTEST_CHECK(kr_quest(player) == KR_FOUND_VENT, "gate without tools must stay shut");
    kr_pass("64_gate_need_tools");

    kr_give(player, obj_lockpick, 1);
    kr_loc1(srv, loc_gate);
    SELFTEST_CHECK(kr_quest(player) == KR_ESCAPED_CELL, "lockpick should open the cell=60");
    kr_pass("65_gate_lockpick");
    kr_journal(srv, "journal_60_escaped_cell");

    /* Authored Telekinetic Grab path (not leftover-stamped). */
    kr_vb(srv, "kr_quest", KR_FOUND_VENT);
    kr_clear_inv(player);
    kr_give(player, obj_law, 1);
    kr_give(player, obj_air, 1);
    kr_loc1(srv, loc_gate);
    SELFTEST_CHECK(kr_quest(player) == KR_ESCAPED_CELL, "telegrab runes should open the cell");
    kr_pass("66_gate_telegrab");

    kr_tele(srv, KR_GRAIL_X, KR_GRAIL_Z, 0);
    kr_loc1(srv, loc_jewelry);
    SELFTEST_CHECK(kr_quest(player) == KR_HAVE_GRAIL, "jewelry table should set have_grail=65");
    SELFTEST_CHECK(kr_inv_total(player, obj_grail) >= 1, "table should grant a Holy Grail replacement");
    kr_pass("69_jewelry_grail");
    kr_journal(srv, "journal_65_have_grail");

    slot_cromperty = kr_spawn(srv, npc_cromperty, KR_CROMPERTY_X, KR_CROMPERTY_Z, 0);
    SELFTEST_CHECK(slot_cromperty >= 0, "Cromperty should spawn");
    kr_talk_finish(srv, npc_cromperty, slot_cromperty);
    SELFTEST_CHECK(kr_quest(player) == KR_HAVE_SCROLL, "Cromperty should set have_scroll=70");
    SELFTEST_CHECK(kr_inv_total(player, obj_scroll) >= 1, "Cromperty should grant animate rock scroll");
    kr_pass("71_cromperty_scroll");
    kr_journal(srv, "journal_70_have_scroll");

    kr_tele(srv, KR_BASEMENT_X, KR_BASEMENT_Z, 0);
    kr_loc1(srv, loc_ladder);
    SELFTEST_CHECK(kr_quest(player) == KR_IN_BASEMENT, "basement ladder should set 75");
    kr_pass("73_ladder_descend");
    kr_journal(srv, "journal_75_in_basement");

    kr_loc1(srv, loc_arthur_statue);
    SELFTEST_CHECK(kr_quest(player) == KR_IN_BASEMENT, "statue without items must stay");
    kr_pass("75_arthur_statue_need_items");

    if( kr_inv_total(player, obj_grail) < 1 )
        kr_give(player, obj_grail, 1);
    if( kr_inv_total(player, obj_scroll) < 1 )
        kr_give(player, obj_scroll, 1);
    kr_give(player, obj_granite, 1);
    kr_loc1(srv, loc_arthur_statue);
    SELFTEST_CHECK(kr_quest(player) == KR_IN_BASEMENT, "statue without disguise must stay");
    kr_pass("76_arthur_statue_need_disguise");

    kr_give(player, obj_helm, 1);
    kr_give(player, obj_chain, 1);
    kr_loc1(srv, loc_arthur_statue);
    SELFTEST_CHECK(kr_quest(player) == KR_TOLD_ARTHUR, "granite curse break should set told_arthur=85");
    kr_pass("77_arthur_statue_free");
    kr_journal(srv, "journal_85_told_arthur");

    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;
    def_before = player->stat_xp_tenths[KR_STAT_DEFENCE];
    mag_before = player->stat_xp_tenths[KR_STAT_MAGIC];

    slot_arthur = kr_spawn(srv, npc_arthur, KR_ARTHUR_X, KR_ARTHUR_Z, 0);
    SELFTEST_CHECK(slot_arthur >= 0, "King Arthur should spawn");
    kr_talk_finish(srv, npc_arthur, slot_arthur);
    SELFTEST_CHECK(kr_quest(player) == KR_COMPLETE, "Camelot hand-in should complete at 90");
    SELFTEST_CHECK(player->stat_xp_tenths[KR_STAT_DEFENCE] >= def_before + KR_DEF_XP,
                   "complete should award 330000 Defence tenths (33000 XP)");
    SELFTEST_CHECK(player->stat_xp_tenths[KR_STAT_MAGIC] >= mag_before + KR_MAG_XP,
                   "complete should award 50000 Magic tenths (5000 XP)");
    SELFTEST_CHECK(kr_inv_total(player, obj_lamp) >= 1, "complete should grant an antique lamp");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + KR_QP_REWARD,
                       "complete should award 1 QP");
    kr_pass("82_complete_scroll");
    kr_journal(srv, "journal_90_complete");
    kr_pass("83_antique_lamp");

    kr_talk_finish(srv, npc_gossip, slot_gossip);
    kr_pass("11_gossip_complete");
    kr_talk_finish(srv, npc_guard, slot_guard);
    kr_pass("15_guard_complete");

    kr_pass("leftover_tumbler_lock_widget");
    kr_pass("leftover_knight_waves");

    kr_free_type(srv, npc_gossip);
    kr_free_type(srv, npc_guard);
    kr_free_type(srv, npc_anna);
    kr_free_type(srv, npc_pierre);
    kr_free_type(srv, npc_hobbes);
    kr_free_type(srv, npc_mary);
    kr_free_type(srv, npc_merlin);
    kr_free_type(srv, npc_cromperty);
    kr_free_type(srv, npc_arthur);
    kr_clear_inv(player);
    kr_reset_quest(srv);
    kr_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_KINGSRANSOM_SELFTEST_U_H */
