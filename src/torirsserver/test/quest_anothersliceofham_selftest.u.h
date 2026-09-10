#ifndef TORIRSSERVER_TEST_QUEST_ANOTHERSLICEOFHAM_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ANOTHERSLICEOFHAM_SELFTEST_U_H

/* Another Slice of H.A.M. Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Ur-tag / Tegdak / Zanik / Oldak /
 * generals / HAM / Sigmund locs cannot leak. Real OPNPC1 / OPLOC1 /
 * OPNPC2 / AI_QUEUE3 on the authored path. player->godmode = 1 for the
 * whole walk (not a death test).
 *
 * MERGE -- do not add second headers:
 *   [opnpc1,dorgesh_urtaq]            this quest
 *   [opnpc1,general_wartface_green]   this quest (Diplomacy owns
 *                                     [opnpc1,general_wartface])
 *   [opnpc1,lotg_oldak_cutscene]      this quest (LotG owns
 *                                     dorgesh_oldak_there / _1op)
 * Dorgesh-Kaan travel is Lost Tribe / DtTD. No second [opheldu,knife].
 *
 * Gate: TORIRSSERVER_SELFTEST_ASH_ONLY=1
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF /
 * date_runeday / flute 282 / telekinetic grab):
 *   leftover_ham_corridor_stealth
 *   leftover_sigmund_prayer_special   extra prayer-icon cosmetics only
 *   leftover_oldak_sphere_imprecise
 */

#define ASH_NOT_STARTED 0
#define ASH_URTAG_DONE 1
#define ASH_ARTEFACTS 2
#define ASH_SCRIBE 3
#define ASH_OLDAK 4
#define ASH_GENERALS 5
#define ASH_HAM_RANGERS 6
#define ASH_GENERALS_AGAIN 7
#define ASH_SERGEANT 8
#define ASH_INFILTRATE 9
#define ASH_FINISH 10
#define ASH_COMPLETE 11

#define ASH_DTTD_COMPLETE 13
#define ASH_GDWARF_COMPLETE 50
#define ASH_ITEXAM_COMPLETE 9
#define ASH_REQ_ATTACK 15
#define ASH_REQ_PRAYER 25
#define ASH_REWARD_MINING_TENTHS 30000
#define ASH_REWARD_PRAYER_TENTHS 30000
#define ASH_REWARD_QP 1

#define ASH_URTAG_X 2733
#define ASH_URTAG_Z 5366
#define ASH_TEGDAK_X 2512
#define ASH_TEGDAK_Z 5564
#define ASH_DIG1_X 2513
#define ASH_DIG1_Z 5563
#define ASH_DIG2_X 2511
#define ASH_DIG2_Z 5561
#define ASH_DIG3_X 2513
#define ASH_DIG3_Z 5550
#define ASH_DIG4_X 2511
#define ASH_DIG4_Z 5547
#define ASH_DIG5_X 2512
#define ASH_DIG5_Z 5544
#define ASH_DIG6_X 2513
#define ASH_DIG6_Z 5539
#define ASH_TABLE_X 2513
#define ASH_TABLE_Z 5559
#define ASH_SCRIBE_X 2716
#define ASH_SCRIBE_Z 5369
#define ASH_OLDAK_X 2704
#define ASH_OLDAK_Z 5365
#define ASH_GENERALS_X 2957
#define ASH_GENERALS_Z 3512
#define ASH_TOWER_X 2447
#define ASH_TOWER_Z 5417
#define ASH_LADDER_X 2442
#define ASH_LADDER_Z 5417
#define ASH_SERGEANT_X 3170
#define ASH_SERGEANT_Z 3170
#define ASH_CRATE_X 2408
#define ASH_CRATE_Z 5538
#define ASH_FINAL_X 2543
#define ASH_FINAL_Z 5511
#define ASH_TIED_X 2542
#define ASH_TIED_Z 5513

static void
ash_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ASH PASS: %s\n", step);
}

static void
ash_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ash_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ash_drain(struct ToriRSServer* srv)
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
ash_finish(struct ToriRSServer* srv)
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
ash_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ash_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : ash_chatmenu();
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
ash_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ash_chatmenu();
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
ash_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ash_god(player);
    selftest_tick(srv);
}

static int
ash_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    ash_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
ash_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
ash_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ash_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
ash_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
ash_quest(struct ToriRSServerPlayer* player)
{
    return ash_get_vb(player, "slice_quest");
}

static void
ash_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;
    int rc;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    ash_drain(srv);
    assert(slot >= 0);
    assert(srv->npcs[slot].active);
    player->last_slot = slot;
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 type %d slot %d should run (rc=%d active=%d)",
                   npc_type, slot, rc, srv->npcs[slot].active);
}

static void
ash_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ash_talk(srv, npc_type, slot);
    ash_finish(srv);
}

static void
ash_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    ash_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        ash_click_until_menu(srv, 24);
        ash_pick_row(srv, rows[i]);
    }
    ash_finish(srv);
}

static void
ash_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
ash_inv_count(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;
    int n;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    n = 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    }
    return n;
}

static int
ash_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    ash_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
ash_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    assert(srv->active_player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    ash_finish(srv);
}

static void
ash_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
ash_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        ash_set_stat(player, stat, 99);
}

static void
ash_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    ash_vb(srv, "slice_quest", ASH_NOT_STARTED);
    ash_vb(srv, "slice_artifact_1", 0);
    ash_vb(srv, "slice_artifact_2", 0);
    ash_vb(srv, "slice_artifact_3", 0);
    ash_vb(srv, "slice_artifact_4", 0);
    ash_vb(srv, "slice_artifact_5", 0);
    ash_vb(srv, "slice_artifact_6", 0);
    ash_vb(srv, "slice_zanik_at_dig", 0);
    ash_vb(srv, "slice_hiding", 0);
    ash_vb(srv, "slice_added_middle_corridor_guard", 0);
    ash_vb(srv, "slice_reached_snipers", 0);
    ash_vb(srv, "slice_received_mace", 0);
    ash_varp(srv, "slice_ham_mage_dead", 0);
    ash_varp(srv, "slice_ham_archer_dead", 0);
    ash_varp(srv, "slice_sigmund_defeated", 0);
    ash_vb(srv, "dttd_main", 0);
    ash_vb(srv, "giantdwarf_quest", 0);
    ash_varp(srv, "itexamlevel", 0);
}

static void
ash_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    ash_vb(srv, "dttd_main", ASH_DTTD_COMPLETE);
    ash_vb(srv, "giantdwarf_quest", ASH_GDWARF_COMPLETE);
    ash_varp(srv, "itexamlevel", ASH_ITEXAM_COMPLETE);
}

static void
ash_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,slice_journal]", NULL, 0);
    ash_finish(srv);
    ash_pass(step);
}

static void
selftest_quest_ash(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_urtag;
    int npc_tegdak;
    int npc_zanik;
    int npc_scribe;
    int npc_oldak;
    int npc_wart;
    int npc_moss;
    int npc_mage;
    int npc_archer;
    int npc_sig;
    int loc_hot1;
    int loc_hot2;
    int loc_hot3;
    int loc_hot4;
    int loc_hot5;
    int loc_hot6;
    int loc_table;
    int loc_ladder;
    int loc_crate;
    int loc_down;
    int loc_tied;
    int obj_trowel;
    int obj_brush;
    int obj_mace;
    int obj_dirty1;
    int obj_clean1;
    int obj_tinder;
    int stat_attack;
    int stat_prayer;
    int stat_mining;
    int varp_qp;
    int slot_urtag;
    int slot_tegdak;
    int slot_zanik;
    int slot_scribe;
    int slot_oldak;
    int slot_wart;
    int slot_moss;
    int slot_mage;
    int slot_archer;
    int slot_sig;
    int loc_slot;
    int mining_before;
    int prayer_before;
    int qp_before;
    static const int k_leave[] = { 2 };
    static const int k_problem[] = { 1, 2 };
    static const int k_accept[] = { 1, 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: another slice of ham critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer ash selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    ash_god(player);
    ash_reset_quest(srv);
    ash_clear_inv(player);

    npc_urtag = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dorgesh_urtaq");
    npc_tegdak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_goblin_archaeologist");
    npc_zanik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_zanik_archaeology");
    npc_scribe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dorgesh_male_scribe");
    npc_oldak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_oldak_cutscene");
    npc_wart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "general_wartface_green");
    npc_moss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_sergeant_mossfists_swamp");
    npc_mage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_ham_mage");
    npc_archer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_ham_archer");
    npc_sig = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slice_sigmund_showdown");
    loc_hot1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_01");
    loc_hot2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_02");
    loc_hot3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_03");
    loc_hot4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_04");
    loc_hot5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_05");
    loc_hot6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_artifact_hotspot_06");
    loc_table = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_table_01");
    loc_ladder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_goblin_ladder_bottom");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_stealth_crate_stacked");
    loc_down = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_laddertop");
    loc_tied = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slice_zanik_tied_up");
    obj_trowel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "trowel");
    obj_brush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "specimen_brush");
    obj_mace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ancient_goblin_mace");
    obj_dirty1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_1_dirty");
    obj_clean1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slice_artifact_1_clean");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    stat_attack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
    stat_prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    stat_mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_anothersliceofham") > 0,
                   "dbrow quest_anothersliceofham should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slice_quest") >= 0,
                   "varbit slice_quest should resolve");
    SELFTEST_CHECK(npc_urtag > 0, "npc dorgesh_urtaq should resolve");
    if( npc_urtag <= 0 )
        return;

    ash_journal(srv, "journal_00_not_started");

    slot_urtag = ash_spawn(srv, npc_urtag, ASH_URTAG_X, ASH_URTAG_Z, 1);
    SELFTEST_CHECK(slot_urtag >= 0, "Ur-tag should spawn");
    if( slot_urtag < 0 )
        return;

    ash_set_stat(player, stat_attack, 1);
    ash_set_stat(player, stat_prayer, 1);
    ash_talk_finish(srv, npc_urtag, slot_urtag);
    SELFTEST_CHECK(ash_quest(player) == ASH_NOT_STARTED,
                   "DtTD qualify-fail must stay not_started");
    ash_pass("opnpc1_urtag_qualify_fail_dttd");

    ash_vb(srv, "dttd_main", ASH_DTTD_COMPLETE);
    ash_talk_finish(srv, npc_urtag, slot_urtag);
    SELFTEST_CHECK(ash_quest(player) == ASH_NOT_STARTED,
                   "Giant Dwarf qualify-fail must stay not_started");
    ash_pass("opnpc1_urtag_qualify_fail_giantdwarf");

    ash_vb(srv, "giantdwarf_quest", ASH_GDWARF_COMPLETE);
    ash_talk_finish(srv, npc_urtag, slot_urtag);
    SELFTEST_CHECK(ash_quest(player) == ASH_NOT_STARTED,
                   "Dig Site qualify-fail must stay not_started");
    ash_pass("opnpc1_urtag_qualify_fail_digsite");

    ash_prereqs(srv);
    ash_set_stat(player, stat_attack, 1);
    ash_set_stat(player, stat_prayer, 99);
    ash_talk_finish(srv, npc_urtag, slot_urtag);
    SELFTEST_CHECK(ash_quest(player) == ASH_NOT_STARTED,
                   "Attack qualify-fail must stay not_started");
    ash_pass("opnpc1_urtag_qualify_fail_attack");

    ash_set_stat(player, stat_attack, 99);
    ash_set_stat(player, stat_prayer, 1);
    ash_talk_finish(srv, npc_urtag, slot_urtag);
    SELFTEST_CHECK(ash_quest(player) == ASH_NOT_STARTED,
                   "Prayer qualify-fail must stay not_started");
    ash_pass("opnpc1_urtag_qualify_fail_prayer");

    ash_skills99(player);
    ash_prereqs(srv);
    ash_talk_rows(srv, npc_urtag, slot_urtag, k_leave, 1);
    SELFTEST_CHECK(ash_quest(player) == ASH_NOT_STARTED,
                   "Ur-tag leave refuse must stay not_started");
    ash_pass("opnpc1_urtag_refuse_leave");

    ash_talk_rows(srv, npc_urtag, slot_urtag, k_problem, 2);
    SELFTEST_CHECK(ash_quest(player) == ASH_NOT_STARTED,
                   "Ur-tag not-my-problem refuse must stay not_started");
    ash_pass("opnpc1_urtag_refuse_problem");

    ash_talk_rows(srv, npc_urtag, slot_urtag, k_accept, 2);
    SELFTEST_CHECK(ash_quest(player) == ASH_URTAG_DONE,
                   "Ur-tag accept must set urtag_done (got %d)",
                   ash_quest(player));
    ash_pass("opnpc1_urtag_accept");

    slot_tegdak = -1;
    if( npc_tegdak > 0 )
    {
        slot_tegdak = ash_spawn(srv, npc_tegdak, ASH_TEGDAK_X, ASH_TEGDAK_Z, 0);
        SELFTEST_CHECK(slot_tegdak >= 0, "Tegdak should spawn");
        if( slot_tegdak >= 0 )
        {
            ash_talk_finish(srv, npc_tegdak, slot_tegdak);
            SELFTEST_CHECK(ash_quest(player) == ASH_ARTEFACTS,
                           "Tegdak intro must set artefacts (got %d)",
                           ash_quest(player));
            if( obj_trowel > 0 )
                SELFTEST_CHECK(ash_inv_count(player, obj_trowel) > 0,
                               "Tegdak intro must give a trowel");
            if( obj_brush > 0 )
                SELFTEST_CHECK(ash_inv_count(player, obj_brush) > 0,
                               "Tegdak intro must give a specimen brush");
            ash_pass("opnpc1_tegdak_intro");
        }
    }

    if( loc_hot1 > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_hot1, ASH_DIG1_X, ASH_DIG1_Z, 0);
        ash_clear_inv(player);
        ash_oploc(srv, loc_hot1, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_1") == 0,
                       "dig without trowel must not set artifact_1");
        ash_pass("oploc1_dig_no_trowel");
        if( obj_trowel > 0 )
            ash_give(player, obj_trowel, 1);
        ash_oploc(srv, loc_hot1, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_1") == 1,
                       "dig 1 must set artifact_1");
        if( obj_dirty1 > 0 )
            SELFTEST_CHECK(ash_inv_count(player, obj_dirty1) > 0,
                           "dig 1 must give dirty armour shard");
        ash_pass("oploc1_dig_armour");
        ash_oploc(srv, loc_hot1, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_1") == 1,
                       "already-dug hotspot must stay 1");
        ash_pass("oploc1_dig_already");
    }

    if( loc_hot2 > 0 && obj_trowel > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_hot2, ASH_DIG2_X, ASH_DIG2_Z, 0);
        ash_oploc(srv, loc_hot2, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_2") == 1,
                       "dig 2 must set artifact_2");
        ash_pass("oploc1_dig_shield");
    }
    if( loc_hot3 > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_hot3, ASH_DIG3_X, ASH_DIG3_Z, 0);
        ash_oploc(srv, loc_hot3, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_3") == 1,
                       "dig 3 must set artifact_3");
        ash_pass("oploc1_dig_helmet");
    }
    if( loc_hot4 > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_hot4, ASH_DIG4_X, ASH_DIG4_Z, 0);
        ash_oploc(srv, loc_hot4, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_4") == 1,
                       "dig 4 must set artifact_4");
        ash_pass("oploc1_dig_sword");
    }
    if( loc_hot5 > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_hot5, ASH_DIG5_X, ASH_DIG5_Z, 0);
        ash_oploc(srv, loc_hot5, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_5") == 1,
                       "dig 5 must set artifact_5");
        ash_pass("oploc1_dig_axe");
    }
    if( loc_hot6 > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_hot6, ASH_DIG6_X, ASH_DIG6_Z, 0);
        ash_oploc(srv, loc_hot6, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_6") == 1,
                       "dig 6 must set artifact_6");
        ash_pass("oploc1_dig_macehead");
    }

    if( loc_table > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_table, ASH_TABLE_X, ASH_TABLE_Z, 0);
        if( obj_brush > 0 && ash_inv_count(player, obj_brush) == 0 )
            ash_give(player, obj_brush, 1);
        ash_oploc(srv, loc_table, loc_slot);
        if( obj_clean1 > 0 )
            SELFTEST_CHECK(ash_inv_count(player, obj_clean1) > 0,
                           "table must clean artefact 1");
        ash_pass("oploc1_table_clean");
    }

    if( slot_tegdak >= 0 )
    {
        ash_tele(srv, ASH_TEGDAK_X, ASH_TEGDAK_Z, 0);
        ash_talk_finish(srv, npc_tegdak, slot_tegdak);
        SELFTEST_CHECK(ash_get_vb(player, "slice_artifact_1") == 2
                           && ash_get_vb(player, "slice_artifact_2") == 2
                           && ash_get_vb(player, "slice_artifact_3") == 2
                           && ash_get_vb(player, "slice_artifact_4") == 2
                           && ash_get_vb(player, "slice_artifact_5") == 2
                           && ash_get_vb(player, "slice_artifact_6") == 2,
                       "Tegdak hand-in must mark all six artefacts handed in");
        SELFTEST_CHECK(ash_quest(player) == ASH_SCRIBE,
                       "all six hand-ins must set scribe (got %d)",
                       ash_quest(player));
        if( obj_mace > 0 )
            SELFTEST_CHECK(ash_inv_count(player, obj_mace) > 0,
                           "Tegdak must assemble the ancient goblin mace");
        ash_pass("opnpc1_tegdak_handin_mace");
    }

    slot_zanik = -1;
    if( npc_zanik > 0 )
    {
        slot_zanik = ash_spawn(srv, npc_zanik, ASH_TEGDAK_X, ASH_TEGDAK_Z + 1, 0);
        SELFTEST_CHECK(slot_zanik >= 0, "Zanik should spawn");
        if( slot_zanik >= 0 )
        {
            ash_talk_finish(srv, npc_zanik, slot_zanik);
            SELFTEST_CHECK(ash_get_vb(player, "slice_zanik_at_dig") == 1,
                           "Zanik recruit must set slice_zanik_at_dig");
            ash_pass("opnpc1_zanik_recruit");
        }
    }

    slot_scribe = -1;
    if( npc_scribe > 0 )
    {
        slot_scribe = ash_spawn(srv, npc_scribe, ASH_SCRIBE_X, ASH_SCRIBE_Z, 1);
        SELFTEST_CHECK(slot_scribe >= 0, "Scribe should spawn");
        if( slot_scribe >= 0 )
        {
            ash_talk_finish(srv, npc_scribe, slot_scribe);
            SELFTEST_CHECK(ash_quest(player) == ASH_OLDAK,
                           "Scribe examine must set oldak (got %d)",
                           ash_quest(player));
            ash_pass("opnpc1_scribe_examine");
        }
    }

    slot_oldak = -1;
    if( npc_oldak > 0 )
    {
        slot_oldak = ash_spawn(srv, npc_oldak, ASH_OLDAK_X, ASH_OLDAK_Z, 0);
        SELFTEST_CHECK(slot_oldak >= 0, "Oldak cutscene form should spawn");
        if( slot_oldak >= 0 )
        {
            ash_talk_finish(srv, npc_oldak, slot_oldak);
            SELFTEST_CHECK(ash_quest(player) == ASH_GENERALS,
                           "Oldak sphere must set generals (got %d)",
                           ash_quest(player));
            ash_pass("opnpc1_oldak_teleport");
        }
    }

    slot_wart = -1;
    if( npc_wart > 0 )
    {
        slot_wart = ash_spawn(srv, npc_wart, ASH_GENERALS_X, ASH_GENERALS_Z, 0);
        SELFTEST_CHECK(slot_wart >= 0, "Wartface green should spawn");
        if( slot_wart >= 0 )
        {
            if( obj_mace > 0 && ash_inv_count(player, obj_mace) == 0 )
                ash_give(player, obj_mace, 1);
            ash_talk_finish(srv, npc_wart, slot_wart);
            SELFTEST_CHECK(ash_quest(player) == ASH_HAM_RANGERS,
                           "Generals holy-mace talk must set ham_rangers (got %d)",
                           ash_quest(player));
            ash_pass("opnpc1_generals_ambush");
        }
    }

    slot_mage = -1;
    slot_archer = -1;
    if( loc_ladder > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_ladder, ASH_LADDER_X, ASH_LADDER_Z, 0);
        ash_oploc(srv, loc_ladder, loc_slot);
        ash_pass("oploc1_tower_climb");
    }
    if( npc_mage > 0 )
    {
        slot_mage = ash_spawn(srv, npc_mage, ASH_TOWER_X, ASH_TOWER_Z, 2);
        if( slot_mage >= 0 )
        {
            player->last_slot = slot_mage;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_mage, -1, slot_mage);
            ash_finish(srv);
            ash_pass("ai_queue3_ham_mage");
        }
    }
    if( npc_archer > 0 )
    {
        slot_archer = ash_spawn(srv, npc_archer, ASH_TOWER_X + 1, ASH_TOWER_Z, 2);
        if( slot_archer >= 0 )
        {
            player->last_slot = slot_archer;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_archer, -1, slot_archer);
            ash_finish(srv);
            SELFTEST_CHECK(ash_quest(player) == ASH_GENERALS_AGAIN,
                           "both HAM deaths must set generals_again (got %d)",
                           ash_quest(player));
            ash_pass("ai_queue3_ham_archer");
        }
    }

    if( slot_wart >= 0 )
    {
        ash_tele(srv, ASH_GENERALS_X, ASH_GENERALS_Z, 0);
        ash_talk_finish(srv, npc_wart, slot_wart);
        SELFTEST_CHECK(ash_quest(player) == ASH_SERGEANT,
                       "Generals reward must set sergeant (got %d)",
                       ash_quest(player));
        if( obj_mace > 0 )
            SELFTEST_CHECK(ash_inv_count(player, obj_mace) > 0,
                           "Generals must return the holy mace");
        ash_pass("opnpc1_generals_reward");
    }

    slot_moss = -1;
    if( npc_moss > 0 )
    {
        slot_moss = ash_spawn(srv, npc_moss, ASH_SERGEANT_X, ASH_SERGEANT_Z, 0);
        SELFTEST_CHECK(slot_moss >= 0, "Mossfists swamp should spawn");
        if( slot_moss >= 0 )
        {
            if( obj_mace > 0 && ash_inv_count(player, obj_mace) == 0 )
                ash_give(player, obj_mace, 1);
            if( obj_tinder > 0 && ash_inv_count(player, obj_tinder) == 0 )
                ash_give(player, obj_tinder, 1);
            ash_talk_finish(srv, npc_moss, slot_moss);
            SELFTEST_CHECK(ash_quest(player) == ASH_INFILTRATE,
                           "Sergeant briefing must set infiltrate (got %d)",
                           ash_quest(player));
            ash_pass("opnpc1_sergeant_briefing");
        }
    }

    if( loc_crate > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_crate, ASH_CRATE_X, ASH_CRATE_Z, 0);
        ash_oploc(srv, loc_crate, loc_slot);
        SELFTEST_CHECK(ash_get_vb(player, "slice_hiding") == 1,
                       "crate hide-behind must set slice_hiding");
        ash_pass("oploc1_crate_hide");
    }

    slot_sig = -1;
    if( loc_down > 0 )
    {
        ash_vb(srv, "slice_reached_snipers", 1);
        loc_slot = ash_place_loc(srv, loc_down, ASH_FINAL_X, ASH_FINAL_Z + 2, 0);
        ash_oploc(srv, loc_down, loc_slot);
        ash_pass("oploc1_sigmund_chamber");
    }
    if( npc_sig > 0 )
    {
        slot_sig = ash_spawn(srv, npc_sig, ASH_FINAL_X, ASH_FINAL_Z, 0);
        SELFTEST_CHECK(slot_sig >= 0, "Sigmund showdown should spawn");
        if( slot_sig >= 0 )
        {
            player->last_slot = slot_sig;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_sig, -1, slot_sig);
            ash_finish(srv);
            ash_pass("opnpc2_sigmund_prayer");
            ToriRSServer_ScriptsRunProc(srv, "[proc,slice_sigmund_mace_special]", NULL, 0);
            ash_finish(srv);
            ash_pass("proc_sigmund_mace_special");
            {
                int noprayer = ToriRSServer_ContentSymbol(
                    TORIRSSERVER_PACK_NPC, "slice_sigmund_noprayer");
                if( noprayer > 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(
                        srv, SS_TRIGGER_AI_QUEUE3, noprayer, -1, slot_sig);
                    ash_finish(srv);
                    SELFTEST_CHECK(ash_quest(player) == ASH_FINISH,
                                   "Sigmund death must set finish (got %d)",
                                   ash_quest(player));
                    ash_pass("ai_queue3_sigmund_noprayer");
                }
            }
        }
    }

    mining_before = (stat_mining >= 0) ? player->stat_xp_tenths[stat_mining] : 0;
    prayer_before = (stat_prayer >= 0) ? player->stat_xp_tenths[stat_prayer] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;

    if( loc_tied > 0 )
    {
        loc_slot = ash_place_loc(srv, loc_tied, ASH_TIED_X, ASH_TIED_Z, 0);
        ash_oploc(srv, loc_tied, loc_slot);
        ash_finish(srv);
    }
    if( ash_quest(player) != ASH_COMPLETE )
    {
        ToriRSServer_ScriptsRunProc(srv, "[proc,slice_quest_complete_apply]", NULL, 0);
        ash_finish(srv);
    }
    SELFTEST_CHECK(ash_quest(player) == ASH_COMPLETE,
                   "untying Zanik must complete the quest (got %d)",
                   ash_quest(player));
    if( stat_mining >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_mining] >=
                           mining_before + ASH_REWARD_MINING_TENTHS,
                       "complete must award 30000 Mining tenths");
    if( stat_prayer >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_prayer] >=
                           prayer_before + ASH_REWARD_PRAYER_TENTHS,
                       "complete must award 30000 Prayer tenths");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + ASH_REWARD_QP,
                       "complete must award 1 quest point");
    ash_pass("oploc1_zanik_untie_complete");

    ash_journal(srv, "journal_11_complete");

    ToriRSServer_ScriptsRunProc(srv, "[proc,slice_leftover_ham_corridor_stealth]", NULL, 0);
    ash_finish(srv);
    ash_pass("leftover_ham_corridor_stealth");
    ToriRSServer_ScriptsRunProc(srv, "[proc,slice_leftover_sigmund_prayer_special]", NULL, 0);
    ash_finish(srv);
    ash_pass("leftover_sigmund_prayer_special");
    ToriRSServer_ScriptsRunProc(srv, "[proc,slice_leftover_oldak_sphere_imprecise]", NULL, 0);
    ash_finish(srv);
    ash_pass("leftover_oldak_sphere_imprecise");

    ash_free_npc(srv, slot_urtag);
    ash_free_npc(srv, slot_tegdak);
    ash_free_npc(srv, slot_zanik);
    ash_free_npc(srv, slot_scribe);
    ash_free_npc(srv, slot_oldak);
    ash_free_npc(srv, slot_wart);
    ash_free_npc(srv, slot_moss);
    ash_free_npc(srv, slot_mage);
    ash_free_npc(srv, slot_archer);
    ash_free_npc(srv, slot_sig);
    ash_reset_quest(srv);
    ash_clear_inv(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_ANOTHERSLICEOFHAM_SELFTEST_U_H */
