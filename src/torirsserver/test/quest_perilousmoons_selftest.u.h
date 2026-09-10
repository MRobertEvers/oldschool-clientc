#ifndef TORIRSSERVER_TEST_QUEST_PERILOUSMOONS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_PERILOUSMOONS_SELFTEST_U_H

/* Perilous Moons Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned Attala / Jessamine / Zuma / Eyatlalli /
 * nagua / Moon bosses / shop NPCs cannot leak. Real OPNPC1 / OPLOC1 on
 * the authored path. player->godmode = 1 for the whole walk (not a
 * death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_PMOON_ONLY=1
 *
 * MERGE -- do not add second headers:
 *   [opnpc1,pmoon_attala_vis]          this file
 *   [opnpc1,pmoon_jessica_vis]         this file
 *   [opnpc1,pmoon_zuma_vis]            this file
 *   [opnpc1,pmoon_eyatlalli_vis]       this file
 *   [opnpc1,cam_torum_shop_magic]      this file only
 *   [opnpc1,cam_torum_shop_blacksmith] this file only
 * Do not redeclare Children of the Sun NPCs.
 *
 * Start NPC is Attala. Qualify-fail split: Children of the Sun FINISHED
 * (%vmq1 >= 24), Slayer 48, Hunter 20, Fishing 20, Runecraft 20,
 * Construction 10. Offer already has p_choice2 "Yes." / "Not now."
 *
 * Reward tenths: Slayer 400000 (40000 XP), Runecraft 50000 (5000 XP),
 * Hunter 50000 (5000 XP), Fishing 50000 (5000 XP).
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF /
 * date_runeday / flute 282 / telekinetic grab / Children of the Sun /
 * the five skill gates):
 *   leftover_sulphur_nagua_combat
 *   leftover_cam_torum_neypotzli_pathing
 *   leftover_camp_build_construction
 *   leftover_talisman_enchant_infuse
 *   leftover_lizard_fish_grub_gather
 *   leftover_moons_of_peril_boss_trio
 *   leftover_full_refuse_trees
 */

#define PM_NOT_STARTED 0
#define PM_NAGUA 2
#define PM_ATTALA2 4
#define PM_JESS 5
#define PM_ENTER 7
#define PM_NEY 8
#define PM_CAMPS 10
#define PM_JESS2 12
#define PM_NAHTA 15
#define PM_SMITH 16
#define PM_EYA 17
#define PM_JESS3 19
#define PM_ITEMS 23
#define PM_EYA2 27
#define PM_MOONS 28
#define PM_ZUMA 30
#define PM_FINISH 31
#define PM_COMPLETE 36

#define PM_COTS_COMPLETE 24
#define PM_REQ_SLAYER 48
#define PM_REQ_HUNTER 20
#define PM_REQ_FISH 20
#define PM_REQ_RC 20
#define PM_REQ_CON 10
#define PM_SLAYER_XP 400000
#define PM_RC_XP 50000
#define PM_HUNT_XP 50000
#define PM_FISH_XP 50000

#define PM_ATTALA_X 1435
#define PM_ATTALA_Z 3124
#define PM_ATTALA_LEVEL 0
#define PM_NEY_X 1439
#define PM_NEY_Z 9600
#define PM_NEY_LEVEL 1

static void
pm_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "PMOON PASS: %s\n", step);
}

static void
pm_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
pm_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
pm_drain(struct ToriRSServer* srv)
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
pm_finish(struct ToriRSServer* srv)
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
pm_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
pm_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : pm_chatmenu();
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
pm_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = pm_chatmenu();
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
pm_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    pm_god(player);
    selftest_tick(srv);
}

static int
pm_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    pm_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
pm_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
pm_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
pm_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
pm_status(struct ToriRSServerPlayer* player)
{
    return pm_get_vb(player, "pmoon_quest");
}

static void
pm_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
pm_skills_ok(struct ToriRSServerPlayer* player)
{
    int stat_slayer;
    int stat_hunt;
    int stat_fish;
    int stat_rc;
    int stat_con;

    assert(player);
    stat_slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    stat_hunt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    stat_fish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fishing");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    pm_set_stat(player, stat_slayer, 99);
    pm_set_stat(player, stat_hunt, 99);
    pm_set_stat(player, stat_fish, 99);
    pm_set_stat(player, stat_rc, 99);
    pm_set_stat(player, stat_con, 99);
}

static void
pm_skills_low(struct ToriRSServerPlayer* player)
{
    int stat_slayer;
    int stat_hunt;
    int stat_fish;
    int stat_rc;
    int stat_con;

    assert(player);
    stat_slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    stat_hunt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    stat_fish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fishing");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    pm_set_stat(player, stat_slayer, 1);
    pm_set_stat(player, stat_hunt, 1);
    pm_set_stat(player, stat_fish, 1);
    pm_set_stat(player, stat_rc, 1);
    pm_set_stat(player, stat_con, 1);
}

static void
pm_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    pm_vb(srv, "pmoon_quest", PM_NOT_STARTED);
    pm_vb(srv, "pmoon_camp_1", 0);
    pm_vb(srv, "pmoon_camp_2", 0);
    pm_vb(srv, "pmoon_camp_3", 0);
    pm_vb(srv, "pmoon_boss_blood_dead", 0);
    pm_vb(srv, "pmoon_boss_blue_dead", 0);
    pm_vb(srv, "pmoon_boss_eclipse_dead", 0);
    pm_vb(srv, "pmoon_murals_inspected", 0);
    pm_vb(srv, "vmq1", 0);
}

static void
pm_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    pm_vb(srv, "vmq1", PM_COTS_COMPLETE);
}

static void
pm_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;
    int rc;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    pm_drain(srv);
    assert(slot >= 0);
    assert(srv->npcs[slot].active);
    player->last_slot = slot;
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 type %d slot %d should run (rc=%d active=%d)",
                   npc_type, slot, rc, srv->npcs[slot].active);
}

static void
pm_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    pm_talk(srv, npc_type, slot);
    pm_finish(srv);
}

static void
pm_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    pm_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        pm_click_until_menu(srv, 24);
        pm_pick_row(srv, rows[i]);
    }
    pm_finish(srv);
}

static void
pm_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
pm_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    pm_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
pm_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    assert(srv->active_player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    pm_finish(srv);
}

static void
pm_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,perilousmoons_journal]", NULL, 0);
    pm_finish(srv);
    pm_pass(step);
}

static void
pm_leftover(struct ToriRSServer* srv, const char* proc, const char* step)
{
    assert(srv);
    assert(proc);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, proc, NULL, 0);
    pm_finish(srv);
    pm_pass(step);
}

static void
selftest_quest_perilousmoons(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_attala;
    int npc_jess;
    int npc_zuma;
    int npc_eya;
    int npc_nagua;
    int npc_nahta;
    int npc_smith;
    int npc_blood;
    int npc_blue;
    int npc_eclipse;
    int loc_tele;
    int loc_camp;
    int loc_grub;
    int obj_tail;
    int stat_slayer;
    int stat_hunt;
    int stat_fish;
    int stat_rc;
    int stat_con;
    int varp_qp;
    int slot_attala;
    int slot_jess;
    int slot_zuma;
    int slot_eya;
    int slot_nagua;
    int slot_nahta;
    int slot_smith;
    int slot_blood;
    int slot_blue;
    int slot_eclipse;
    int loc_slot;
    int slayer_before;
    int rc_before;
    int hunt_before;
    int fish_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: perilous moons critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer pmoon selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    pm_god(player);
    pm_reset_quest(srv);
    pm_clear_inv(player);
    pm_skills_low(player);

    npc_attala = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pmoon_attala_vis");
    npc_jess = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pmoon_jessica_vis");
    npc_zuma = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pmoon_zuma_vis");
    npc_eya = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pmoon_eyatlalli_vis");
    npc_nagua = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pmoon_quest_nagua_vis");
    npc_nahta = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "cam_torum_shop_magic");
    npc_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "cam_torum_shop_blacksmith");
    npc_blood = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pmoon_boss_blood_moon_vis");
    npc_blue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pmoon_boss_blue_moon_vis");
    npc_eclipse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pmoon_boss_eclipse_moon_vis");
    loc_tele = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pmoon_telebox");
    loc_camp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pmoon_supply_crate");
    loc_grub = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "pmoon_grub_sapling");
    obj_tail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pmoon_lizard_tail");
    stat_slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    stat_hunt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    stat_fish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fishing");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_perilousmoons") > 0,
                   "dbrow quest_perilousmoons should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "pmoon_quest") >= 0,
                   "varbit pmoon_quest should resolve");
    SELFTEST_CHECK(npc_attala > 0, "npc pmoon_attala_vis should resolve");
    if( npc_attala <= 0 )
        return;

    pm_journal(srv, "journal_00_not_started");

    slot_attala = pm_spawn(srv, npc_attala, PM_ATTALA_X, PM_ATTALA_Z, PM_ATTALA_LEVEL);
    SELFTEST_CHECK(slot_attala >= 0, "Attala should spawn");
    if( slot_attala < 0 )
        return;

    pm_skills_ok(player);
    pm_talk_finish(srv, npc_attala, slot_attala);
    SELFTEST_CHECK(pm_status(player) == PM_NOT_STARTED,
                   "Children of the Sun qualify-fail must stay not_started");
    pm_pass("opnpc1_qualify_fail_cots");

    pm_prereqs(srv);
    pm_skills_low(player);
    pm_set_stat(player, stat_hunt, 99);
    pm_set_stat(player, stat_fish, 99);
    pm_set_stat(player, stat_rc, 99);
    pm_set_stat(player, stat_con, 99);
    pm_talk_finish(srv, npc_attala, slot_attala);
    SELFTEST_CHECK(pm_status(player) == PM_NOT_STARTED,
                   "Slayer qualify-fail must stay not_started");
    pm_pass("opnpc1_qualify_fail_slayer");

    pm_set_stat(player, stat_slayer, 99);
    pm_set_stat(player, stat_hunt, 1);
    pm_talk_finish(srv, npc_attala, slot_attala);
    SELFTEST_CHECK(pm_status(player) == PM_NOT_STARTED,
                   "Hunter qualify-fail must stay not_started");
    pm_pass("opnpc1_qualify_fail_hunter");

    pm_set_stat(player, stat_hunt, 99);
    pm_set_stat(player, stat_fish, 1);
    pm_talk_finish(srv, npc_attala, slot_attala);
    SELFTEST_CHECK(pm_status(player) == PM_NOT_STARTED,
                   "Fishing qualify-fail must stay not_started");
    pm_pass("opnpc1_qualify_fail_fishing");

    pm_set_stat(player, stat_fish, 99);
    pm_set_stat(player, stat_rc, 1);
    pm_talk_finish(srv, npc_attala, slot_attala);
    SELFTEST_CHECK(pm_status(player) == PM_NOT_STARTED,
                   "Runecraft qualify-fail must stay not_started");
    pm_pass("opnpc1_qualify_fail_runecraft");

    pm_set_stat(player, stat_rc, 99);
    pm_set_stat(player, stat_con, 1);
    pm_talk_finish(srv, npc_attala, slot_attala);
    SELFTEST_CHECK(pm_status(player) == PM_NOT_STARTED,
                   "Construction qualify-fail must stay not_started");
    pm_pass("opnpc1_qualify_fail_construction");

    pm_skills_ok(player);
    pm_talk_rows(srv, npc_attala, slot_attala, k_refuse, 1);
    SELFTEST_CHECK(pm_status(player) == PM_NOT_STARTED,
                   "Attala refuse must stay not_started");
    pm_pass("opnpc1_attala_refuse");

    pm_talk_rows(srv, npc_attala, slot_attala, k_accept, 1);
    SELFTEST_CHECK(pm_status(player) == PM_NAGUA,
                   "Attala accept must set nagua (got %d)", pm_status(player));
    pm_pass("opnpc1_attala_accept");

    pm_talk_finish(srv, npc_attala, slot_attala);
    SELFTEST_CHECK(pm_status(player) == PM_NAGUA,
                   "Attala nagua reminder must stay nagua");
    pm_pass("opnpc1_attala_nagua_reminder");

    slot_nagua = -1;
    if( npc_nagua > 0 )
    {
        slot_nagua = pm_spawn(srv, npc_nagua, PM_ATTALA_X + 2, PM_ATTALA_Z, PM_ATTALA_LEVEL);
        SELFTEST_CHECK(slot_nagua >= 0, "sulphur nagua should spawn");
        if( slot_nagua >= 0 )
        {
            pm_talk_finish(srv, npc_nagua, slot_nagua);
            SELFTEST_CHECK(pm_status(player) == PM_ATTALA2,
                           "nagua skip must set attala2 (got %d)", pm_status(player));
            pm_pass("opnpc1_nagua_fight_skip");
        }
    }

    pm_talk_finish(srv, npc_attala, slot_attala);
    SELFTEST_CHECK(pm_status(player) == PM_JESS,
                   "Attala after nagua must set jess (got %d)", pm_status(player));
    pm_pass("opnpc1_attala_after_nagua");

    pm_journal(srv, "journal_05_jess");

    slot_jess = -1;
    if( npc_jess > 0 )
    {
        slot_jess = pm_spawn(srv, npc_jess, PM_ATTALA_X + 3, PM_ATTALA_Z, PM_ATTALA_LEVEL);
        SELFTEST_CHECK(slot_jess >= 0, "Jessamine should spawn");
        if( slot_jess >= 0 )
        {
            pm_talk_finish(srv, npc_jess, slot_jess);
            SELFTEST_CHECK(pm_status(player) == PM_ENTER,
                           "Jess enter must set enter (got %d)", pm_status(player));
            pm_pass("opnpc1_jess_enter_ney");
        }
    }

    if( loc_tele > 0 )
    {
        loc_slot = pm_place_loc(srv, loc_tele, PM_ATTALA_X + 4, PM_ATTALA_Z, PM_ATTALA_LEVEL);
        pm_oploc(srv, loc_tele, loc_slot);
        SELFTEST_CHECK(pm_status(player) == PM_NEY,
                       "tele enter must set ney (got %d)", pm_status(player));
        pm_pass("oploc1_tele_enter_ney");
    }

    pm_journal(srv, "journal_08_ney");

    slot_zuma = -1;
    if( npc_zuma > 0 )
    {
        slot_zuma = pm_spawn(srv, npc_zuma, PM_NEY_X, PM_NEY_Z, PM_NEY_LEVEL);
        SELFTEST_CHECK(slot_zuma >= 0, "Zuma should spawn");
        if( slot_zuma >= 0 )
        {
            pm_talk_finish(srv, npc_zuma, slot_zuma);
            SELFTEST_CHECK(pm_status(player) == PM_NEY,
                           "Zuma murals must stay ney");
            pm_pass("opnpc1_zuma_murals");
        }
    }

    if( slot_jess >= 0 )
    {
        pm_talk_finish(srv, npc_jess, slot_jess);
        SELFTEST_CHECK(pm_status(player) == PM_CAMPS,
                       "Jess establish camps must set camps (got %d)",
                       pm_status(player));
        pm_pass("opnpc1_jess_establish_camps");

        pm_talk_finish(srv, npc_jess, slot_jess);
        SELFTEST_CHECK(pm_status(player) == PM_CAMPS,
                       "Jess need-camps must stay camps");
        pm_pass("opnpc1_jess_need_camps");
    }

    if( loc_camp > 0 )
    {
        loc_slot = pm_place_loc(srv, loc_camp, PM_NEY_X + 2, PM_NEY_Z, PM_NEY_LEVEL);
        pm_oploc(srv, loc_camp, loc_slot);
        SELFTEST_CHECK(pm_get_vb(player, "pmoon_camp_1") == 1,
                       "prison camp should set camp_1");
        pm_pass("oploc1_camp_prison");
        pm_oploc(srv, loc_camp, loc_slot);
        SELFTEST_CHECK(pm_get_vb(player, "pmoon_camp_2") == 1,
                       "earthbound camp should set camp_2");
        pm_pass("oploc1_camp_earthbound");
        pm_oploc(srv, loc_camp, loc_slot);
        SELFTEST_CHECK(pm_status(player) == PM_JESS2,
                       "streambound camp must set jess2 (got %d)",
                       pm_status(player));
        pm_pass("oploc1_camp_streambound");
    }

    pm_journal(srv, "journal_12_jess2");

    if( slot_jess >= 0 )
    {
        pm_talk_finish(srv, npc_jess, slot_jess);
        SELFTEST_CHECK(pm_status(player) == PM_NAHTA,
                       "Jess visit Nahta must set nahta (got %d)",
                       pm_status(player));
        pm_pass("opnpc1_jess_visit_nahta");
    }

    slot_nahta = -1;
    if( npc_nahta > 0 )
    {
        slot_nahta = pm_spawn(srv, npc_nahta, PM_ATTALA_X + 5, PM_ATTALA_Z, PM_ATTALA_LEVEL);
        SELFTEST_CHECK(slot_nahta >= 0, "Nahta should spawn");
        if( slot_nahta >= 0 )
        {
            pm_talk_finish(srv, npc_nahta, slot_nahta);
            SELFTEST_CHECK(pm_status(player) == PM_SMITH,
                           "Nahta talismans must set smith (got %d)",
                           pm_status(player));
            pm_pass("opnpc1_nahta_talismans");
        }
    }

    slot_smith = -1;
    if( npc_smith > 0 )
    {
        slot_smith = pm_spawn(srv, npc_smith, PM_ATTALA_X + 6, PM_ATTALA_Z, PM_ATTALA_LEVEL);
        SELFTEST_CHECK(slot_smith >= 0, "blacksmith should spawn");
        if( slot_smith >= 0 )
        {
            pm_talk_finish(srv, npc_smith, slot_smith);
            SELFTEST_CHECK(pm_status(player) == PM_EYA,
                           "blacksmith weapons must set eya (got %d)",
                           pm_status(player));
            pm_pass("opnpc1_blacksmith_weapons");
        }
    }

    pm_journal(srv, "journal_17_eya");

    slot_eya = -1;
    if( npc_eya > 0 )
    {
        slot_eya = pm_spawn(srv, npc_eya, PM_NEY_X + 3, PM_NEY_Z, PM_NEY_LEVEL);
        SELFTEST_CHECK(slot_eya >= 0, "Eyatlalli should spawn");
        if( slot_eya >= 0 )
        {
            pm_talk_finish(srv, npc_eya, slot_eya);
            SELFTEST_CHECK(pm_status(player) == PM_JESS3,
                           "Eya return-jess must set jess3 (got %d)",
                           pm_status(player));
            pm_pass("opnpc1_eya_return_jess");
        }
    }

    if( slot_jess >= 0 )
    {
        pm_talk_finish(srv, npc_jess, slot_jess);
        SELFTEST_CHECK(pm_status(player) == PM_ITEMS,
                       "Jess ritual list must set items (got %d)",
                       pm_status(player));
        pm_pass("opnpc1_jess_ritual_list");
    }

    if( slot_eya >= 0 )
    {
        pm_talk_finish(srv, npc_eya, slot_eya);
        SELFTEST_CHECK(pm_status(player) == PM_ITEMS,
                       "Eya need-tail must stay items");
        pm_pass("opnpc1_eya_need_tail");
    }

    if( loc_grub > 0 )
    {
        loc_slot = pm_place_loc(srv, loc_grub, PM_NEY_X + 4, PM_NEY_Z, PM_NEY_LEVEL);
        pm_oploc(srv, loc_grub, loc_slot);
        pm_pass("oploc1_grub_paste");
    }

    if( obj_tail > 0 )
        pm_give(player, obj_tail, 1);

    if( slot_eya >= 0 )
    {
        pm_talk_finish(srv, npc_eya, slot_eya);
        SELFTEST_CHECK(pm_status(player) == PM_EYA2,
                       "Eya ritual ready must set eya2 (got %d)",
                       pm_status(player));
        pm_pass("opnpc1_eya_ritual_ready");

        pm_talk_finish(srv, npc_eya, slot_eya);
        SELFTEST_CHECK(pm_status(player) == PM_MOONS,
                       "Eya face moons must set moons (got %d)",
                       pm_status(player));
        pm_pass("opnpc1_eya_face_moons");
    }

    pm_journal(srv, "journal_28_moons");

    if( slot_eya >= 0 )
    {
        pm_talk_finish(srv, npc_eya, slot_eya);
        SELFTEST_CHECK(pm_status(player) == PM_MOONS,
                       "Eya defeat-moons must stay moons");
        pm_pass("opnpc1_eya_defeat_moons");
    }

    slot_blood = -1;
    if( npc_blood > 0 )
    {
        slot_blood = pm_spawn(srv, npc_blood, PM_NEY_X + 5, PM_NEY_Z, PM_NEY_LEVEL);
        if( slot_blood >= 0 )
        {
            pm_talk_finish(srv, npc_blood, slot_blood);
            SELFTEST_CHECK(pm_get_vb(player, "pmoon_boss_blood_dead") == 1,
                           "Blood Moon skip should set blood_dead");
            pm_pass("opnpc1_blood_moon_skip");
        }
    }

    slot_blue = -1;
    if( npc_blue > 0 )
    {
        slot_blue = pm_spawn(srv, npc_blue, PM_NEY_X + 6, PM_NEY_Z, PM_NEY_LEVEL);
        if( slot_blue >= 0 )
        {
            pm_talk_finish(srv, npc_blue, slot_blue);
            SELFTEST_CHECK(pm_get_vb(player, "pmoon_boss_blue_dead") == 1,
                           "Blue Moon skip should set blue_dead");
            pm_pass("opnpc1_blue_moon_skip");
        }
    }

    slot_eclipse = -1;
    if( npc_eclipse > 0 )
    {
        slot_eclipse = pm_spawn(srv, npc_eclipse, PM_NEY_X + 7, PM_NEY_Z, PM_NEY_LEVEL);
        if( slot_eclipse >= 0 )
        {
            pm_talk_finish(srv, npc_eclipse, slot_eclipse);
            SELFTEST_CHECK(pm_status(player) == PM_ZUMA,
                           "Eclipse Moon skip must set zuma (got %d)",
                           pm_status(player));
            pm_pass("opnpc1_eclipse_moon_skip");
        }
    }

    if( slot_zuma >= 0 )
    {
        pm_talk_finish(srv, npc_zuma, slot_zuma);
        SELFTEST_CHECK(pm_status(player) == PM_FINISH,
                       "Zuma moons quiet must set finish (got %d)",
                       pm_status(player));
        pm_pass("opnpc1_zuma_moons_quiet");
    }

    pm_journal(srv, "journal_31_finish");

    slayer_before = (stat_slayer >= 0) ? player->stat_xp_tenths[stat_slayer] : 0;
    rc_before = (stat_rc >= 0) ? player->stat_xp_tenths[stat_rc] : 0;
    hunt_before = (stat_hunt >= 0) ? player->stat_xp_tenths[stat_hunt] : 0;
    fish_before = (stat_fish >= 0) ? player->stat_xp_tenths[stat_fish] : 0;
    qp_before = 0;
    if( varp_qp >= 0 )
        qp_before = player->varps[varp_qp];

    if( slot_eya >= 0 )
    {
        pm_talk_finish(srv, npc_eya, slot_eya);
        SELFTEST_CHECK(pm_status(player) == PM_COMPLETE,
                       "Eyatlalli finish must complete (got %d)",
                       pm_status(player));
        pm_pass("opnpc1_eya_complete");
    }

    if( stat_slayer >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_slayer] >= slayer_before + PM_SLAYER_XP,
                       "Slayer reward tenths 400000 (40000 XP)");
    if( stat_rc >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_rc] >= rc_before + PM_RC_XP,
                       "Runecraft reward tenths 50000 (5000 XP)");
    if( stat_hunt >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_hunt] >= hunt_before + PM_HUNT_XP,
                       "Hunter reward tenths 50000 (5000 XP)");
    if( stat_fish >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_fish] >= fish_before + PM_FISH_XP,
                       "Fishing reward tenths 50000 (5000 XP)");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] > qp_before,
                       "quest complete should award QP");
    pm_pass("complete_rewards");

    pm_journal(srv, "journal_36_complete");

    if( slot_attala >= 0 )
    {
        pm_talk_finish(srv, npc_attala, slot_attala);
        pm_pass("opnpc1_attala_complete");
    }
    if( slot_jess >= 0 )
    {
        pm_talk_finish(srv, npc_jess, slot_jess);
        pm_pass("opnpc1_jess_complete");
    }

    pm_leftover(srv, "[proc,pm_leftover_sulphur_nagua_combat]",
                "leftover_sulphur_nagua_combat");
    pm_leftover(srv, "[proc,pm_leftover_cam_torum_neypotzli_pathing]",
                "leftover_cam_torum_neypotzli_pathing");
    pm_leftover(srv, "[proc,pm_leftover_camp_build_construction]",
                "leftover_camp_build_construction");
    pm_leftover(srv, "[proc,pm_leftover_talisman_enchant_infuse]",
                "leftover_talisman_enchant_infuse");
    pm_leftover(srv, "[proc,pm_leftover_lizard_fish_grub_gather]",
                "leftover_lizard_fish_grub_gather");
    pm_leftover(srv, "[proc,pm_leftover_moons_of_peril_boss_trio]",
                "leftover_moons_of_peril_boss_trio");
    pm_leftover(srv, "[proc,pm_leftover_full_refuse_trees]",
                "leftover_full_refuse_trees");

    pm_free_npc(srv, slot_attala);
    pm_free_npc(srv, slot_jess);
    pm_free_npc(srv, slot_zuma);
    pm_free_npc(srv, slot_eya);
    pm_free_npc(srv, slot_nagua);
    pm_free_npc(srv, slot_nahta);
    pm_free_npc(srv, slot_smith);
    pm_free_npc(srv, slot_blood);
    pm_free_npc(srv, slot_blue);
    pm_free_npc(srv, slot_eclipse);
    pm_clear_inv(player);
    pm_reset_quest(srv);
}

#endif
