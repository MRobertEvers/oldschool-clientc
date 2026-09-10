#ifndef TORIRSSERVER_TEST_QUEST_MAKINGFRIENDSWITHMYARM_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_MAKINGFRIENDSWITHMYARM_SELFTEST_U_H

/* Making Friends with My Arm Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned Burntmeat / My Arm / Larry / Mother /
 * WOM / Snowflake cannot leak. Real OPNPC1 / OPLOC1 on the authored
 * path. player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_MFWMA_ONLY=1
 *
 * MERGE -- do not add second headers:
 *   [opnpc1,eadgar_troll_chief_cook]  quests/quest_eadgar/scripts/eadgar_troll_chief_cook.rs2
 *   [opnpc1,myarm] / myarm_fixed      this file splices after %myarm >= ^myarm_complete
 *   [opnpc1,peng_larry_rell]          quests/quest_coldwar/scripts/coldwar_larry.rs2
 *   [opnpc1,apothecary]               areas/varrock/scripts/apothecary.rs2
 *
 * Start NPC is Burntmeat. Qualify-fail split: My Arm's Big Adventure
 * FINISHED (%myarm = 320), Construction 35, Firemaking 66, Mining 72,
 * Agility 68. My Arm fail is authored in ~mf_burntmeat_talk /
 * ~mf_show_qualify_fail; OPNPC1 cannot reach it because the dispatcher
 * routes to MABA while %myarm is incomplete -- C-walk invokes the proc
 * for that one gate (same shape as KR Murder Mystery).
 *
 * Roof Yes/No is on My Arm after Burntmeat accept.
 *
 * Reward tenths: Construction 100000 (10000 XP), Firemaking 400000
 * (40000 XP), Mining 500000 (50000 XP), Agility 500000 (50000 XP).
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF /
 * date_runeday / flute 282 / telekinetic grab / My Arm prereq / the
 * four skill gates):
 *   leftover_troll_stronghold_pathing
 *   leftover_larry_boat_instance
 *   leftover_weiss_cliff_rope
 *   leftover_cave_sneak_pathing
 *   leftover_coffin_build_if
 *   leftover_dontknowwhat_mother_fight
 *   leftover_goat_dung_gather
 *   leftover_firepit_unlock_ui
 *   leftover_extra_refuse_trees
 */

#define MF_NOT_STARTED 0
#define MF_ROOF 10
#define MF_LARRY 20
#define MF_LARRY2 25
#define MF_WEISS 35
#define MF_BOULDER 45
#define MF_SNEAK 50
#define MF_CAVES 65
#define MF_MOTHER 75
#define MF_ARM_WEISS 85
#define MF_WOM 110
#define MF_COFFIN 120
#define MF_WOM2 132
#define MF_DELIVER 135
#define MF_PRISON 145
#define MF_AFTER_FIGHT 175
#define MF_WOM_WEISS 178
#define MF_SNOWFLAKE 180
#define MF_DUNG 185
#define MF_NOTES 195
#define MF_FINISH 196
#define MF_COMPLETE 200

#define MF_MYARM_COMPLETE 320
#define MF_COLDWAR_COMPLETE 135
#define MF_REQ_CON 35
#define MF_REQ_FM 66
#define MF_REQ_MINE 72
#define MF_REQ_AGI 68
#define MF_CON_XP 100000
#define MF_FM_XP 400000
#define MF_MINE_XP 500000
#define MF_AGI_XP 500000

#define MF_BURNTMEAT_X 2820
#define MF_BURNTMEAT_Z 10048
#define MF_BURNTMEAT_LEVEL 1
#define MF_ARM_X 2822
#define MF_ARM_Z 3665
#define MF_LARRY_X 2707
#define MF_LARRY_Z 3733
#define MF_WEISS_X 2861
#define MF_WEISS_Z 3908
#define MF_WOM_X 3088
#define MF_WOM_Z 3255
#define MF_APOTH_X 3196
#define MF_APOTH_Z 3403

static void
mf_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "MFWMA PASS: %s\n", step);
}

static void
mf_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
mf_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
mf_drain(struct ToriRSServer* srv)
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
mf_finish(struct ToriRSServer* srv)
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
mf_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
mf_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : mf_chatmenu();
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
mf_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mf_chatmenu();
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
mf_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    mf_god(player);
    selftest_tick(srv);
}

static int
mf_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    mf_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
mf_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
mf_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mf_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
mf_status(struct ToriRSServerPlayer* player)
{
    return mf_get_vb(player, "my2arm_status");
}

static void
mf_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
mf_skills_ok(struct ToriRSServerPlayer* player)
{
    int stat_con;
    int stat_fm;
    int stat_mine;
    int stat_agi;

    assert(player);
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    mf_set_stat(player, stat_con, 99);
    mf_set_stat(player, stat_fm, 99);
    mf_set_stat(player, stat_mine, 99);
    mf_set_stat(player, stat_agi, 99);
}

static void
mf_skills_low(struct ToriRSServerPlayer* player)
{
    int stat_con;
    int stat_fm;
    int stat_mine;
    int stat_agi;

    assert(player);
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    mf_set_stat(player, stat_con, 1);
    mf_set_stat(player, stat_fm, 1);
    mf_set_stat(player, stat_mine, 1);
    mf_set_stat(player, stat_agi, 1);
}

static void
mf_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    mf_vb(srv, "my2arm_status", MF_NOT_STARTED);
    mf_vb(srv, "my2arm_client_coffin", 0);
    mf_vb(srv, "myarm", 0);
    mf_vb(srv, "peng_quest", 0);
}

static void
mf_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    mf_vb(srv, "myarm", MF_MYARM_COMPLETE);
    mf_vb(srv, "peng_quest", MF_COLDWAR_COMPLETE);
}

static void
mf_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;
    int rc;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    mf_drain(srv);
    assert(slot >= 0);
    assert(srv->npcs[slot].active);
    player->last_slot = slot;
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 type %d slot %d should run (rc=%d active=%d)",
                   npc_type, slot, rc, srv->npcs[slot].active);
}

static void
mf_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    mf_talk(srv, npc_type, slot);
    mf_finish(srv);
}

static void
mf_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    mf_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        mf_click_until_menu(srv, 24);
        mf_pick_row(srv, rows[i]);
    }
    mf_finish(srv);
}

static void
mf_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
mf_inv_count(struct ToriRSServerPlayer* player, int obj_id)
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
mf_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    mf_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
mf_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    assert(srv->active_player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    mf_finish(srv);
}

static void
mf_opheld(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    mf_finish(srv);
}

static void
mf_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,makingfriendswithmyarm_journal]", NULL, 0);
    mf_finish(srv);
    mf_pass(step);
}

static void
mf_leftover(struct ToriRSServer* srv, const char* proc, const char* step)
{
    assert(srv);
    assert(proc);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, proc, NULL, 0);
    mf_finish(srv);
    mf_pass(step);
}

static void
selftest_quest_makingfriendswithmyarm(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_cook;
    int npc_myarm;
    int npc_larry;
    int npc_boulder;
    int npc_mother;
    int npc_wom;
    int npc_apoth;
    int npc_snow;
    int npc_mush;
    int npc_dkw;
    int npc_mother_fight;
    int npc_wom_weiss;
    int loc_boat;
    int loc_cliff;
    int loc_fence;
    int loc_cave;
    int loc_coffin;
    int loc_dung;
    int obj_potion;
    int obj_goat;
    int obj_book;
    int stat_con;
    int stat_fm;
    int stat_mine;
    int stat_agi;
    int varp_qp;
    int slot_cook;
    int slot_myarm;
    int slot_larry;
    int slot_boulder;
    int slot_mother;
    int slot_wom;
    int slot_apoth;
    int slot_snow;
    int slot_mush;
    int slot_dkw;
    int slot_mother_fight;
    int slot_wom_weiss;
    int loc_slot;
    int con_before;
    int fm_before;
    int mine_before;
    int agi_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: making friends with my arm critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer mfwma selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    mf_god(player);
    mf_reset_quest(srv);
    mf_clear_inv(player);
    mf_skills_low(player);

    npc_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "eadgar_troll_chief_cook");
    npc_myarm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "myarm_fixed");
    npc_larry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "peng_larry_rell");
    npc_boulder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "my2arm_sentry_boulder");
    npc_mother = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "my2arm_mother_enthroned");
    npc_wom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wise_old_man");
    npc_apoth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "apothecary");
    npc_snow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "my2arm_snowflake");
    npc_mush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "my2arm_mushroom_dying");
    npc_dkw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "my2arm_dontknowwhat_battle");
    npc_mother_fight = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "my2arm_mother_battle_melee");
    npc_wom_weiss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "my2arm_wom_endquest");
    loc_boat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "peng_boat_rell");
    loc_cliff = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "my2arm_cliff_shortcut_3");
    loc_fence = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "my2arm_town_fence_broken");
    loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "my2arm_cave_squeeze");
    loc_coffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "my2arm_coffin_multi");
    loc_dung = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "my2arm_goatdung");
    obj_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "my2arm_potion");
    obj_goat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "my2arm_goatpoo");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "my2arm_book");
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_makingfriendswithmyarm") > 0,
                   "dbrow quest_makingfriendswithmyarm should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "my2arm_status") >= 0,
                   "varbit my2arm_status should resolve");
    SELFTEST_CHECK(npc_cook > 0, "npc eadgar_troll_chief_cook should resolve");
    if( npc_cook <= 0 )
        return;

    mf_journal(srv, "journal_00_not_started");

    /* My Arm prereq: dispatcher routes to MABA, so invoke the authored
     * qualify-fail proc (KR Murder Mystery shape). */
    ToriRSServer_ScriptsRunProc(srv, "[proc,mf_show_qualify_fail]", NULL, 0);
    mf_finish(srv);
    SELFTEST_CHECK(mf_status(player) == MF_NOT_STARTED,
                   "My Arm qualify-fail must stay not_started");
    mf_pass("qualify_fail_myarm");

    slot_cook = mf_spawn(srv, npc_cook, MF_BURNTMEAT_X, MF_BURNTMEAT_Z, MF_BURNTMEAT_LEVEL);
    SELFTEST_CHECK(slot_cook >= 0, "Burntmeat should spawn");
    if( slot_cook < 0 )
        return;

    mf_prereqs(srv);
    mf_skills_low(player);
    mf_set_stat(player, stat_fm, 99);
    mf_set_stat(player, stat_mine, 99);
    mf_set_stat(player, stat_agi, 99);
    mf_talk_finish(srv, npc_cook, slot_cook);
    SELFTEST_CHECK(mf_status(player) == MF_NOT_STARTED,
                   "Construction qualify-fail must stay not_started");
    mf_pass("opnpc1_qualify_fail_construction");

    mf_set_stat(player, stat_con, 99);
    mf_set_stat(player, stat_fm, 1);
    mf_talk_finish(srv, npc_cook, slot_cook);
    SELFTEST_CHECK(mf_status(player) == MF_NOT_STARTED,
                   "Firemaking qualify-fail must stay not_started");
    mf_pass("opnpc1_qualify_fail_firemaking");

    mf_set_stat(player, stat_fm, 99);
    mf_set_stat(player, stat_mine, 1);
    mf_talk_finish(srv, npc_cook, slot_cook);
    SELFTEST_CHECK(mf_status(player) == MF_NOT_STARTED,
                   "Mining qualify-fail must stay not_started");
    mf_pass("opnpc1_qualify_fail_mining");

    mf_set_stat(player, stat_mine, 99);
    mf_set_stat(player, stat_agi, 1);
    mf_talk_finish(srv, npc_cook, slot_cook);
    SELFTEST_CHECK(mf_status(player) == MF_NOT_STARTED,
                   "Agility qualify-fail must stay not_started");
    mf_pass("opnpc1_qualify_fail_agility");

    mf_skills_ok(player);
    mf_talk_rows(srv, npc_cook, slot_cook, k_refuse, 1);
    SELFTEST_CHECK(mf_status(player) == MF_NOT_STARTED,
                   "Burntmeat refuse must stay not_started");
    mf_pass("opnpc1_burntmeat_refuse");

    mf_talk_rows(srv, npc_cook, slot_cook, k_accept, 1);
    SELFTEST_CHECK(mf_status(player) == MF_ROOF,
                   "Burntmeat accept must set roof (got %d)", mf_status(player));
    mf_pass("opnpc1_burntmeat_accept");

    mf_talk_finish(srv, npc_cook, slot_cook);
    SELFTEST_CHECK(mf_status(player) == MF_ROOF,
                   "Burntmeat mid-roof must stay roof");
    mf_pass("opnpc1_burntmeat_mid_roof");

    mf_journal(srv, "journal_10_roof");

    slot_myarm = -1;
    if( npc_myarm > 0 )
    {
        slot_myarm = mf_spawn(srv, npc_myarm, MF_ARM_X, MF_ARM_Z, 0);
        SELFTEST_CHECK(slot_myarm >= 0, "My Arm should spawn");
        if( slot_myarm >= 0 )
        {
            mf_talk_rows(srv, npc_myarm, slot_myarm, k_refuse, 1);
            SELFTEST_CHECK(mf_status(player) == MF_ROOF,
                           "My Arm refuse must stay roof");
            mf_pass("opnpc1_myarm_roof_refuse");

            mf_talk_rows(srv, npc_myarm, slot_myarm, k_accept, 1);
            SELFTEST_CHECK(mf_status(player) == MF_LARRY,
                           "My Arm accept must set larry (got %d)", mf_status(player));
            mf_pass("opnpc1_myarm_roof_accept");

            mf_talk_finish(srv, npc_myarm, slot_myarm);
            SELFTEST_CHECK(mf_status(player) == MF_LARRY,
                           "My Arm mid-larry must stay larry");
            mf_pass("opnpc1_myarm_mid_larry");
        }
    }

    mf_journal(srv, "journal_20_larry");

    slot_larry = -1;
    if( npc_larry > 0 )
    {
        slot_larry = mf_spawn(srv, npc_larry, MF_LARRY_X, MF_LARRY_Z, 0);
        SELFTEST_CHECK(slot_larry >= 0, "Larry should spawn");
        if( slot_larry >= 0 )
        {
            mf_talk_finish(srv, npc_larry, slot_larry);
            SELFTEST_CHECK(mf_status(player) == MF_LARRY2,
                           "Larry first talk must set larry2 (got %d)", mf_status(player));
            mf_pass("opnpc1_larry_first");

            mf_talk_finish(srv, npc_larry, slot_larry);
            SELFTEST_CHECK(mf_status(player) == MF_WEISS,
                           "Larry sail must set weiss (got %d)", mf_status(player));
            mf_pass("opnpc1_larry_sail");
        }
    }

    if( loc_boat > 0 )
    {
        mf_vb(srv, "my2arm_status", MF_LARRY2);
        loc_slot = mf_place_loc(srv, loc_boat, MF_LARRY_X, MF_LARRY_Z, 0);
        mf_oploc(srv, loc_boat, loc_slot);
        SELFTEST_CHECK(mf_status(player) == MF_WEISS,
                       "boat loc must set weiss (got %d)", mf_status(player));
        mf_pass("oploc1_larry_boat");
    }

    mf_journal(srv, "journal_35_weiss");

    if( loc_cliff > 0 )
    {
        loc_slot = mf_place_loc(srv, loc_cliff, MF_WEISS_X, MF_WEISS_Z, 0);
        mf_oploc(srv, loc_cliff, loc_slot);
        SELFTEST_CHECK(mf_status(player) == MF_BOULDER,
                       "cliff rope must set boulder (got %d)", mf_status(player));
        mf_pass("oploc1_cliff_rope");
    }

    slot_boulder = -1;
    if( npc_boulder > 0 )
    {
        slot_boulder = mf_spawn(srv, npc_boulder, MF_WEISS_X, MF_WEISS_Z, 0);
        SELFTEST_CHECK(slot_boulder >= 0, "boulder sentry should spawn");
        if( slot_boulder >= 0 )
        {
            mf_talk_finish(srv, npc_boulder, slot_boulder);
            SELFTEST_CHECK(mf_status(player) == MF_SNEAK,
                           "boulder sneak must set sneak (got %d)", mf_status(player));
            mf_pass("opnpc1_boulder_sneak");
        }
    }

    if( loc_fence > 0 )
    {
        loc_slot = mf_place_loc(srv, loc_fence, MF_WEISS_X + 2, MF_WEISS_Z, 0);
        mf_oploc(srv, loc_fence, loc_slot);
        SELFTEST_CHECK(mf_status(player) == MF_CAVES,
                       "fence sneak must set caves (got %d)", mf_status(player));
        mf_pass("oploc1_fence_sneak");
        mf_oploc(srv, loc_fence, loc_slot);
        SELFTEST_CHECK(mf_status(player) == MF_MOTHER,
                       "fence deeper must set mother (got %d)", mf_status(player));
        mf_pass("oploc1_cave_deeper");
    }
    else if( loc_cave > 0 )
    {
        mf_vb(srv, "my2arm_status", MF_CAVES);
        loc_slot = mf_place_loc(srv, loc_cave, MF_WEISS_X + 3, MF_WEISS_Z, 0);
        mf_oploc(srv, loc_cave, loc_slot);
        SELFTEST_CHECK(mf_status(player) == MF_MOTHER,
                       "cave pathing must set mother (got %d)", mf_status(player));
        mf_pass("oploc1_cave_pathing");
    }

    mf_journal(srv, "journal_75_mother");

    slot_mother = -1;
    if( npc_mother > 0 )
    {
        mf_vb(srv, "my2arm_status", MF_MOTHER);
        slot_mother = mf_spawn(srv, npc_mother, MF_WEISS_X, MF_WEISS_Z, 0);
        SELFTEST_CHECK(slot_mother >= 0, "Mother should spawn");
        if( slot_mother >= 0 )
        {
            mf_talk_finish(srv, npc_mother, slot_mother);
            SELFTEST_CHECK(mf_status(player) == MF_ARM_WEISS,
                           "Mother intro must set arm_weiss (got %d)", mf_status(player));
            mf_pass("opnpc1_mother_intro");
        }
    }

    if( slot_myarm >= 0 )
    {
        mf_talk_finish(srv, npc_myarm, slot_myarm);
        SELFTEST_CHECK(mf_status(player) == MF_WOM,
                       "My Arm snowflake-trapped must set wom (got %d)", mf_status(player));
        mf_pass("opnpc1_myarm_snowflake_trapped");
    }

    mf_journal(srv, "journal_110_wom");

    slot_wom = -1;
    if( npc_wom > 0 )
    {
        slot_wom = mf_spawn(srv, npc_wom, MF_WOM_X, MF_WOM_Z, 0);
        SELFTEST_CHECK(slot_wom >= 0, "Wise Old Man should spawn");
        if( slot_wom >= 0 )
        {
            mf_talk_finish(srv, npc_wom, slot_wom);
            SELFTEST_CHECK(mf_status(player) == MF_COFFIN,
                           "WOM funeral ruse must set coffin (got %d)", mf_status(player));
            mf_pass("opnpc1_wom_funeral_ruse");
        }
    }

    slot_apoth = -1;
    if( npc_apoth > 0 )
    {
        slot_apoth = mf_spawn(srv, npc_apoth, MF_APOTH_X, MF_APOTH_Z, 0);
        SELFTEST_CHECK(slot_apoth >= 0, "Apothecary should spawn");
        if( slot_apoth >= 0 )
        {
            mf_talk_finish(srv, npc_apoth, slot_apoth);
            SELFTEST_CHECK(mf_status(player) == MF_WOM2,
                           "Apothecary cadava must set wom2 (got %d)", mf_status(player));
            mf_pass("opnpc1_apoth_cadava");
        }
    }

    if( slot_wom >= 0 )
    {
        if( obj_potion > 0 && mf_inv_count(player, obj_potion) < 1 )
            mf_give(player, obj_potion, 1);
        mf_talk_finish(srv, npc_wom, slot_wom);
        SELFTEST_CHECK(mf_status(player) == MF_DELIVER,
                       "WOM load coffin must set deliver (got %d)", mf_status(player));
        mf_pass("opnpc1_wom_load_coffin");
    }

    if( loc_coffin > 0 )
    {
        mf_vb(srv, "my2arm_status", MF_COFFIN);
        loc_slot = mf_place_loc(srv, loc_coffin, MF_WOM_X + 2, MF_WOM_Z, 0);
        mf_oploc(srv, loc_coffin, loc_slot);
        mf_pass("oploc1_coffin_build");
        mf_vb(srv, "my2arm_status", MF_DELIVER);
    }

    mf_journal(srv, "journal_135_deliver");

    if( slot_myarm >= 0 )
    {
        mf_talk_finish(srv, npc_myarm, slot_myarm);
        SELFTEST_CHECK(mf_status(player) == MF_PRISON,
                       "My Arm WOM-ready must set prison (got %d)", mf_status(player));
        mf_pass("opnpc1_myarm_wom_ready");
    }

    slot_mush = -1;
    if( npc_mush > 0 )
    {
        slot_mush = mf_spawn(srv, npc_mush, MF_WEISS_X, MF_WEISS_Z + 2, 0);
        if( slot_mush >= 0 )
        {
            mf_talk_finish(srv, npc_mush, slot_mush);
            mf_pass("opnpc1_mushroom_weak");
        }
    }

    slot_dkw = -1;
    if( npc_dkw > 0 )
    {
        slot_dkw = mf_spawn(srv, npc_dkw, MF_WEISS_X + 4, MF_WEISS_Z, 0);
        if( slot_dkw >= 0 )
        {
            mf_talk_finish(srv, npc_dkw, slot_dkw);
            SELFTEST_CHECK(mf_status(player) == 160,
                           "Don't Know What skip must set 160 (got %d)", mf_status(player));
            mf_pass("opnpc1_dontknowwhat_skip");
        }
    }

    slot_mother_fight = -1;
    if( npc_mother_fight > 0 )
    {
        slot_mother_fight = mf_spawn(srv, npc_mother_fight, MF_WEISS_X + 5, MF_WEISS_Z, 0);
        if( slot_mother_fight >= 0 )
        {
            mf_talk_finish(srv, npc_mother_fight, slot_mother_fight);
            SELFTEST_CHECK(mf_status(player) == MF_AFTER_FIGHT,
                           "Mother fight skip must set after_fight (got %d)",
                           mf_status(player));
            mf_pass("opnpc1_mother_fight_skip");
        }
    }

    mf_journal(srv, "journal_175_after_fight");

    if( slot_myarm >= 0 )
    {
        mf_talk_finish(srv, npc_myarm, slot_myarm);
        SELFTEST_CHECK(mf_status(player) == MF_WOM_WEISS,
                       "My Arm after-fight must set wom_weiss (got %d)",
                       mf_status(player));
        mf_pass("opnpc1_myarm_after_fight");
    }

    slot_wom_weiss = -1;
    if( npc_wom_weiss > 0 )
    {
        slot_wom_weiss = mf_spawn(srv, npc_wom_weiss, MF_WEISS_X + 6, MF_WEISS_Z, 0);
        if( slot_wom_weiss >= 0 )
        {
            mf_talk_finish(srv, npc_wom_weiss, slot_wom_weiss);
            SELFTEST_CHECK(mf_status(player) == MF_SNOWFLAKE,
                           "WOM aftermath must set snowflake (got %d)",
                           mf_status(player));
            mf_pass("opnpc1_wom_aftermath");
        }
    }

    slot_snow = -1;
    if( npc_snow > 0 )
    {
        slot_snow = mf_spawn(srv, npc_snow, MF_WEISS_X + 7, MF_WEISS_Z, 0);
        SELFTEST_CHECK(slot_snow >= 0, "Snowflake should spawn");
        if( slot_snow >= 0 )
        {
            mf_talk_finish(srv, npc_snow, slot_snow);
            SELFTEST_CHECK(mf_status(player) == MF_DUNG,
                           "Snowflake dung-ask must set dung (got %d)",
                           mf_status(player));
            mf_pass("opnpc1_snowflake_dung_ask");

            mf_talk_finish(srv, npc_snow, slot_snow);
            SELFTEST_CHECK(mf_status(player) == MF_DUNG,
                           "Snowflake need-dung must stay dung");
            mf_pass("opnpc1_snowflake_need_dung");
        }
    }

    if( loc_dung > 0 )
    {
        loc_slot = mf_place_loc(srv, loc_dung, MF_WEISS_X + 8, MF_WEISS_Z, 0);
        mf_oploc(srv, loc_dung, loc_slot);
        if( obj_goat > 0 )
            SELFTEST_CHECK(mf_inv_count(player, obj_goat) >= 1,
                           "goat dung scoop should give my2arm_goatpoo");
        mf_pass("oploc1_goat_dung_scoop");
    }
    else if( obj_goat > 0 )
        mf_give(player, obj_goat, 1);

    if( slot_snow >= 0 )
    {
        mf_talk_finish(srv, npc_snow, slot_snow);
        SELFTEST_CHECK(mf_status(player) == MF_NOTES,
                       "Snowflake give-dung must set notes (got %d)",
                       mf_status(player));
        mf_pass("opnpc1_snowflake_give_dung");

        if( obj_book > 0 && mf_inv_count(player, obj_book) < 1 )
            mf_give(player, obj_book, 1);
        mf_opheld(srv, obj_book);
        SELFTEST_CHECK(mf_status(player) == MF_FINISH,
                       "notes read must set finish (got %d)", mf_status(player));
        mf_pass("opheld1_notes_read");
    }

    mf_journal(srv, "journal_196_finish");

    con_before = (stat_con >= 0) ? player->stat_xp_tenths[stat_con] : 0;
    fm_before = (stat_fm >= 0) ? player->stat_xp_tenths[stat_fm] : 0;
    mine_before = (stat_mine >= 0) ? player->stat_xp_tenths[stat_mine] : 0;
    agi_before = (stat_agi >= 0) ? player->stat_xp_tenths[stat_agi] : 0;
    qp_before = 0;
    if( varp_qp >= 0 )
        qp_before = player->varps[varp_qp];

    if( slot_snow >= 0 )
    {
        mf_talk_finish(srv, npc_snow, slot_snow);
        SELFTEST_CHECK(mf_status(player) == MF_COMPLETE,
                       "Snowflake finish must complete (got %d)", mf_status(player));
        mf_pass("opnpc1_snowflake_finish");
    }

    if( stat_con >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_con] >= con_before + MF_CON_XP,
                       "Construction reward tenths 100000 (10000 XP)");
    if( stat_fm >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_fm] >= fm_before + MF_FM_XP,
                       "Firemaking reward tenths 400000 (40000 XP)");
    if( stat_mine >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_mine] >= mine_before + MF_MINE_XP,
                       "Mining reward tenths 500000 (50000 XP)");
    if( stat_agi >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_agi] >= agi_before + MF_AGI_XP,
                       "Agility reward tenths 500000 (50000 XP)");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] > qp_before,
                       "quest complete should award QP");
    mf_pass("complete_rewards");

    mf_journal(srv, "journal_200_complete");

    if( slot_cook >= 0 )
    {
        mf_talk_finish(srv, npc_cook, slot_cook);
        mf_pass("opnpc1_burntmeat_complete");
    }
    if( slot_myarm >= 0 )
    {
        mf_talk_finish(srv, npc_myarm, slot_myarm);
        mf_pass("opnpc1_myarm_complete");
    }
    if( slot_snow >= 0 )
    {
        mf_talk_finish(srv, npc_snow, slot_snow);
        mf_pass("opnpc1_snowflake_thanks");
    }

    mf_leftover(srv, "[proc,mf_leftover_troll_stronghold_pathing]",
                "leftover_troll_stronghold_pathing");
    mf_leftover(srv, "[proc,mf_leftover_larry_boat_instance]",
                "leftover_larry_boat_instance");
    mf_leftover(srv, "[proc,mf_leftover_weiss_cliff_rope]",
                "leftover_weiss_cliff_rope");
    mf_leftover(srv, "[proc,mf_leftover_cave_sneak_pathing]",
                "leftover_cave_sneak_pathing");
    mf_leftover(srv, "[proc,mf_leftover_coffin_build_if]",
                "leftover_coffin_build_if");
    mf_leftover(srv, "[proc,mf_leftover_dontknowwhat_mother_fight]",
                "leftover_dontknowwhat_mother_fight");
    mf_leftover(srv, "[proc,mf_leftover_goat_dung_gather]",
                "leftover_goat_dung_gather");
    mf_leftover(srv, "[proc,mf_leftover_firepit_unlock_ui]",
                "leftover_firepit_unlock_ui");
    mf_leftover(srv, "[proc,mf_leftover_extra_refuse_trees]",
                "leftover_extra_refuse_trees");

    mf_free_npc(srv, slot_cook);
    mf_free_npc(srv, slot_myarm);
    mf_free_npc(srv, slot_larry);
    mf_free_npc(srv, slot_boulder);
    mf_free_npc(srv, slot_mother);
    mf_free_npc(srv, slot_wom);
    mf_free_npc(srv, slot_apoth);
    mf_free_npc(srv, slot_snow);
    mf_free_npc(srv, slot_mush);
    mf_free_npc(srv, slot_dkw);
    mf_free_npc(srv, slot_mother_fight);
    mf_free_npc(srv, slot_wom_weiss);
    mf_clear_inv(player);
    mf_reset_quest(srv);
}

#endif
