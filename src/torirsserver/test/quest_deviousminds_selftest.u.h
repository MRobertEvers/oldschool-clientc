#ifndef TORIRSSERVER_TEST_QUEST_DEVIOUSMINDS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_DEVIOUSMINDS_SELFTEST_U_H

/* Devious Minds Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned monk /
 * Tiffy / High Priest / whetstone / altar cannot leak. Real OPNPC1 / OPLOC1 /
 * OPLOCU / OPHELDU on the authored path. player->godmode = 1 for the whole
 * walk (not a death test). Completion goes through Tiffy
 * queue(deviousminds_quest_complete) -> ~quest_complete_rewards.
 *
 * Gate: TORIRSSERVER_SELFTEST_DM_ONLY=1
 *
 * ::complete / ::deviousminds are not the walk. Prereqs are genuine
 * varp/varbit writes (Wanted! + Troll Stronghold + Doric's + Enter the Abyss).
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - Exact Abyss / Law Altar traversal is soft-skipped
 *   - Multi-monk / assassin heist is narrated mesbox, not a client cutscene
 *   - Colossal pouch survives / large pouch destroyed per wiki
 */

#define DM_NOT_STARTED 0
#define DM_ACCEPTED 10
#define DM_BOWSWORD_GIVEN 20
#define DM_POUCH_PLACED 30
#define DM_MONK_FOUND_DEAD 40
#define DM_REPORTED_PRIEST 50
#define DM_COMPLETE 60

#define DM_REQ_SMITHING 65
#define DM_REQ_RUNECRAFT 50
#define DM_REQ_FLETCHING 50
#define DM_TROLL_COMPLETE 50
#define DM_DORIC_COMPLETE 100
#define DM_ETA_COMPLETE 4
#define DM_WANTED_COMPLETE 11
#define DM_REWARD_SMITH_TENTHS 65000
#define DM_REWARD_RC_TENTHS 50000
#define DM_REWARD_FLETCH_TENTHS 50000

#define DM_MONK_X 3406
#define DM_MONK_Z 3492
#define DM_WHET_X 2951
#define DM_WHET_Z 3451
#define DM_ALTAR_X 2853
#define DM_ALTAR_Z 3348
#define DM_PRIEST_X 2851
#define DM_PRIEST_Z 3349
#define DM_TIFFY_X 3002
#define DM_TIFFY_Z 3368

static void
dm_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "DM PASS: %s\n", step);
}

static void
dm_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
dm_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
dm_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    /* Drain resume buttons only. Do not WorldCloseModal -- that aborts the
     * active script and can drop Tiffy's queue(deviousminds_quest_complete). */
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 8) <= 0 )
            selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
}

static int
dm_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
dm_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = dm_chatmenu();
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
dm_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = dm_chatmenu();
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
dm_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    dm_god(player);
    selftest_tick(srv);
}

static int
dm_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    dm_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
dm_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
dm_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
dm_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
dm_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static void
dm_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
dm_talk_drain(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    dm_talk(srv, npc_type, slot);
    dm_finish(srv);
}

static void
dm_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    dm_talk(srv, npc_type, slot);
    dm_click_until_menu(srv, 24);
    dm_pick_row(srv, row);
    dm_finish(srv);
}

static void
dm_talk_pick2(
    struct ToriRSServer* srv,
    int npc_type,
    int slot,
    int row1,
    int row2)
{
    assert(srv);
    dm_talk(srv, npc_type, slot);
    dm_click_until_menu(srv, 24);
    dm_pick_row(srv, row1);
    dm_click_until_menu(srv, 24);
    dm_pick_row(srv, row2);
    dm_finish(srv);
}

static void
dm_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
    }
}

static int
dm_find_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            return s;
    }
    return -1;
}

static void
dm_opheldu(struct ToriRSServer* srv, int obj_type, int use_obj_type)
{
    struct ToriRSServerPlayer* player;
    int slot_a;
    int slot_b;

    assert(srv);
    assert(obj_type > 0);
    assert(use_obj_type > 0);
    player = srv->active_player;
    assert(player);
    slot_a = dm_find_inv_slot(player, obj_type);
    slot_b = dm_find_inv_slot(player, use_obj_type);
    player->last_item = obj_type;
    player->last_slot = slot_a;
    player->last_useitem = use_obj_type;
    player->last_useslot = slot_b;
    ToriRSServer_ScriptsRunOpheldu(srv, obj_type, -1, use_obj_type, -1);
    dm_finish(srv);
    player->last_item = -1;
    player->last_useitem = -1;
    player->last_slot = -1;
    player->last_useslot = -1;
}

static void
dm_oploc1(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    dm_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    dm_finish(srv);
}

static void
dm_oplocu(struct ToriRSServer* srv, int loc_id, int x, int z, int level, int use_obj)
{
    struct ToriRSServerPlayer* player;
    int slot;
    int placed;
    int use_slot;

    assert(srv);
    assert(loc_id > 0);
    assert(use_obj > 0);
    player = srv->active_player;
    assert(player);
    dm_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    use_slot = dm_find_inv_slot(player, use_obj);
    player->last_useitem = use_obj;
    player->last_useslot = use_slot;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    dm_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
dm_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,deviousminds_journal]", NULL, 0);
    dm_finish(srv);
    dm_pass(step);
}

static void
dm_set_skills(struct ToriRSServerPlayer* player, int smith, int rc, int fletch)
{
    int stat_smith;
    int stat_rc;
    int stat_fletch;

    assert(player);
    stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    stat_fletch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fletching");
    if( stat_smith >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_smith, smith);
    if( stat_rc >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_rc, rc);
    if( stat_fletch >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_fletch, fletch);
}

static void
dm_clear_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    dm_varp(srv, "troll_quest", 0);
    dm_varp(srv, "doricquest", 0);
    dm_varp(srv, "abyssal_miniquest", 0);
    dm_vb(srv, "wanted_main", 0);
}

static void
dm_set_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    dm_varp(srv, "troll_quest", DM_TROLL_COMPLETE);
    dm_varp(srv, "doricquest", DM_DORIC_COMPLETE);
    dm_varp(srv, "abyssal_miniquest", DM_ETA_COMPLETE);
    dm_vb(srv, "wanted_main", DM_WANTED_COMPLETE);
}

static void
dm_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    dm_vb(srv, "devious_main", DM_NOT_STARTED);
    dm_vb(srv, "devious_monk_met", 0);
    dm_vb(srv, "devious_monk_orb_given", 0);
    dm_vb(srv, "devious_cutscene", 0);
    dm_vb(srv, "devious_altar", 0);
    dm_vb(srv, "devious_monk", 0);
}

static void
selftest_quest_deviousminds(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_monk;
    int npc_dead;
    int npc_priest;
    int npc_tiffy;
    int loc_whet;
    int loc_altar;
    int obj_mith;
    int obj_blade;
    int obj_bowsword;
    int obj_orb;
    int obj_string;
    int obj_junk;
    int obj_small;
    int obj_med;
    int obj_giant;
    int obj_large;
    int obj_colo;
    int obj_large_d;
    int obj_colo_d;
    int obj_sealed;
    int obj_sealed_c;
    int dbrow;
    int slot;
    int stat_smith;
    int stat_rc;
    int stat_fletch;
    int smith_before;
    int rc_before;
    int fletch_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: Devious Minds critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer deviousminds selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    player->godmode = 1;
    srv->members_world = 1;
    dm_god(player);

    npc_monk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "devious_monk_hooded");
    npc_dead = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "devious_monk_dead");
    npc_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "high_priest_of_entrana");
    npc_tiffy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ds2_meeting_sir_tiffy_cashien");
    loc_whet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "devious_whetstone");
    loc_altar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "devious_altar");
    obj_mith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mithril_2h_sword");
    obj_blade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "devious_slenderblade");
    obj_bowsword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "devious_bowsword");
    obj_orb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "devious_glowingorb");
    obj_string = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bow_string");
    obj_junk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_bar");
    obj_small = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_small");
    obj_med = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_medium");
    obj_giant = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_giant");
    obj_large = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_large");
    obj_colo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_colossal");
    obj_large_d = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_large_degrade");
    obj_colo_d = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_colossal_degrade");
    obj_sealed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "devious_glowingpouch");
    obj_sealed_c = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "devious_glowingpouch_colossal");
    dbrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_deviousminds");
    stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    stat_fletch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fletching");

    SELFTEST_CHECK(npc_monk > 0, "npc devious_monk_hooded should resolve");
    SELFTEST_CHECK(npc_dead > 0, "npc devious_monk_dead should resolve");
    SELFTEST_CHECK(npc_priest > 0, "npc high_priest_of_entrana should resolve");
    SELFTEST_CHECK(npc_tiffy > 0, "npc ds2_meeting_sir_tiffy_cashien should resolve");
    SELFTEST_CHECK(loc_whet > 0, "loc devious_whetstone should resolve");
    SELFTEST_CHECK(loc_altar > 0, "loc devious_altar should resolve");
    SELFTEST_CHECK(obj_mith > 0, "obj mithril_2h_sword should resolve");
    SELFTEST_CHECK(obj_blade > 0, "obj devious_slenderblade should resolve");
    SELFTEST_CHECK(obj_bowsword > 0, "obj devious_bowsword should resolve");
    SELFTEST_CHECK(obj_orb > 0, "obj devious_glowingorb should resolve");
    SELFTEST_CHECK(obj_string > 0, "obj bow_string should resolve");
    SELFTEST_CHECK(obj_large > 0, "obj rcu_pouch_large should resolve");
    SELFTEST_CHECK(obj_colo > 0, "obj rcu_pouch_colossal should resolve");
    SELFTEST_CHECK(dbrow > 0, "dbrow quest_deviousminds should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "devious_main") >= 0,
                   "varbit devious_main should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "wanted_main") >= 0,
                   "varbit wanted_main should resolve");
    if( npc_monk <= 0 || loc_whet <= 0 || obj_bowsword <= 0 )
    {
        fprintf(stderr, "ToriRSServer deviousminds selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    dm_clear_inv(player);
    dm_reset_quest(srv);
    dm_clear_prereqs(srv);
    dm_set_skills(player, 1, 1, 1);
    dm_journal(srv, "journal_not_started");

    /* ---- Monk offer: refuse / qualify-fail / accept ---- */
    slot = dm_spawn(srv, npc_monk, DM_MONK_X, DM_MONK_Z, 0);
    SELFTEST_CHECK(slot >= 0, "hooded monk should spawn");
    if( slot >= 0 )
    {
        dm_talk_pick(srv, npc_monk, slot, 2);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_NOT_STARTED,
                       "That's nice. I have to go. must not start");
        dm_pass("opnpc1_monk_refuse_thats_nice");

        dm_set_skills(player, 1, 1, 1);
        dm_set_prereqs(srv);
        dm_talk_pick(srv, npc_monk, slot, 1);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_NOT_STARTED,
                       "low stats must fail ~deviousminds_qualifies");
        dm_pass("opnpc1_monk_qualify_fail_stats");

        dm_set_skills(player, DM_REQ_SMITHING, DM_REQ_RUNECRAFT, DM_REQ_FLETCHING);
        dm_clear_prereqs(srv);
        dm_talk_pick(srv, npc_monk, slot, 1);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_NOT_STARTED,
                       "missing Wanted! / Troll / Doric / Abyss must fail qualify");
        dm_pass("opnpc1_monk_qualify_fail_prereqs");

        /* Wanted! is done on this queue -- keep the gate. Drop only Wanted. */
        dm_varp(srv, "troll_quest", DM_TROLL_COMPLETE);
        dm_varp(srv, "doricquest", DM_DORIC_COMPLETE);
        dm_varp(srv, "abyssal_miniquest", DM_ETA_COMPLETE);
        dm_vb(srv, "wanted_main", 0);
        dm_talk_pick(srv, npc_monk, slot, 1);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_NOT_STARTED,
                       "Wanted! gate must stay; missing wanted_main must refuse");
        dm_pass("opnpc1_monk_qualify_fail_wanted");

        dm_set_prereqs(srv);
        dm_talk_pick2(srv, npc_monk, slot, 1, 1);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_ACCEPTED,
                       "I'd be glad to help. must write accepted, got %d",
                       dm_get_vb(player, "devious_main"));
        SELFTEST_CHECK(dm_get_vb(player, "devious_monk_met") == 1,
                       "accept must set %%devious_monk_met");
        dm_pass("opnpc1_monk_accept_glad_to_help");

        dm_talk_drain(srv, npc_monk, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_ACCEPTED,
                       "mid reminder without bow-sword must stay accepted");
        dm_pass("opnpc1_monk_mid_reminder_no_bowsword");
    }
    dm_journal(srv, "journal_accepted");

    /* ---- Whetstone too-early / wrong item / need mithril 2h / success ---- */
    dm_reset_quest(srv);
    dm_set_prereqs(srv);
    dm_set_skills(player, DM_REQ_SMITHING, DM_REQ_RUNECRAFT, DM_REQ_FLETCHING);
    dm_clear_inv(player);
    if( obj_mith > 0 )
        dm_give(player, obj_mith, 1);
    if( loc_whet > 0 )
    {
        dm_oploc1(srv, loc_whet, DM_WHET_X, DM_WHET_Z, 0);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_NOT_STARTED,
                       "whetstone oploc1 before accept must not start the quest");
        SELFTEST_CHECK(selftest_count_obj(player, obj_blade) == 0,
                       "too-early whetstone must not grind a slender blade");
        dm_pass("oploc1_whetstone_too_early");

        if( obj_mith > 0 )
        {
            dm_oplocu(srv, loc_whet, DM_WHET_X, DM_WHET_Z, 0, obj_mith);
            SELFTEST_CHECK(selftest_count_obj(player, obj_blade) == 0,
                           "oplocu mithril 2h before accept must refuse");
            dm_pass("oplocu_whetstone_too_early_mithril");
        }
    }

    dm_vb(srv, "devious_main", DM_ACCEPTED);
    dm_clear_inv(player);
    if( loc_whet > 0 )
    {
        dm_oploc1(srv, loc_whet, DM_WHET_X, DM_WHET_Z, 0);
        dm_pass("oploc1_whetstone_need_mithril_2h");

        if( obj_junk > 0 )
        {
            dm_give(player, obj_junk, 1);
            dm_oplocu(srv, loc_whet, DM_WHET_X, DM_WHET_Z, 0, obj_junk);
            SELFTEST_CHECK(selftest_count_obj(player, obj_blade) == 0,
                           "wrong item on whetstone must not grind");
            dm_pass("oplocu_whetstone_wrong_item");
        }

        dm_clear_inv(player);
        if( obj_mith > 0 )
        {
            dm_give(player, obj_mith, 1);
            dm_oplocu(srv, loc_whet, DM_WHET_X, DM_WHET_Z, 0, obj_mith);
            SELFTEST_CHECK(selftest_count_obj(player, obj_blade) == 1,
                           "mithril 2h on whetstone must create devious_slenderblade");
            SELFTEST_CHECK(selftest_count_obj(player, obj_mith) == 0,
                           "successful grind must consume the mithril 2h");
            dm_pass("oplocu_whetstone_success_slenderblade");
        }
    }

    /* ---- String blade: fail (no string) + success ---- */
    dm_clear_inv(player);
    if( obj_blade > 0 )
        dm_give(player, obj_blade, 1);
    if( obj_blade > 0 && obj_junk > 0 )
    {
        dm_give(player, obj_junk, 1);
        dm_opheldu(srv, obj_blade, obj_junk);
        SELFTEST_CHECK(selftest_count_obj(player, obj_bowsword) == 0,
                       "string fail (no bow_string) must not create the bow-sword");
        SELFTEST_CHECK(selftest_count_obj(player, obj_blade) == 1,
                       "string fail must leave the slender blade");
        dm_pass("opheldu_string_fail_no_string");
    }

    dm_clear_inv(player);
    if( obj_blade > 0 )
        dm_give(player, obj_blade, 1);
    if( obj_string > 0 )
        dm_give(player, obj_string, 1);
    if( obj_blade > 0 && obj_string > 0 )
    {
        dm_opheldu(srv, obj_blade, obj_string);
        SELFTEST_CHECK(selftest_count_obj(player, obj_bowsword) == 1,
                       "string success must create devious_bowsword");
        SELFTEST_CHECK(selftest_count_obj(player, obj_blade) == 0,
                       "string success must consume the slender blade");
        dm_pass("opheldu_string_success_bowsword");
    }

    /* Other click order: bows.rs2 case devious_slenderblade. */
    dm_clear_inv(player);
    if( obj_blade > 0 )
        dm_give(player, obj_blade, 1);
    if( obj_string > 0 )
        dm_give(player, obj_string, 1);
    if( obj_blade > 0 && obj_string > 0 )
    {
        dm_opheldu(srv, obj_string, obj_blade);
        SELFTEST_CHECK(selftest_count_obj(player, obj_bowsword) == 1,
                       "bow_string on slenderblade must also create the bow-sword");
        dm_pass("opheldu_bow_string_on_slenderblade");
    }

    /* ---- Monk receive bow-sword + legal p_choice2 + accept orb ---- */
    dm_clear_inv(player);
    if( obj_bowsword > 0 )
        dm_give(player, obj_bowsword, 1);
    dm_vb(srv, "devious_main", DM_ACCEPTED);
    if( slot >= 0 )
    {
        dm_talk_pick(srv, npc_monk, slot, 1);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_BOWSWORD_GIVEN,
                       "legal smuggling choice must still hand the orb, got %d",
                       dm_get_vb(player, "devious_main"));
        SELFTEST_CHECK(selftest_count_obj(player, obj_orb) == 1,
                       "monk must give devious_glowingorb");
        SELFTEST_CHECK(selftest_count_obj(player, obj_bowsword) == 0,
                       "monk must take the bow-sword");
        SELFTEST_CHECK(dm_get_vb(player, "devious_monk_orb_given") == 1,
                       "handoff must set %%devious_monk_orb_given");
        dm_pass("opnpc1_monk_receive_legal_smuggling");
    }
    dm_journal(srv, "journal_bowsword_given");

    dm_talk_drain(srv, npc_monk, slot);
    SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_BOWSWORD_GIVEN,
                   "orb reminder must stay on bowsword_given");
    dm_pass("opnpc1_monk_orb_reminder");

    /* Accept-orb branch (I'll take it there) from a fresh handoff. */
    dm_reset_quest(srv);
    dm_set_prereqs(srv);
    dm_vb(srv, "devious_main", DM_ACCEPTED);
    dm_clear_inv(player);
    if( obj_bowsword > 0 )
        dm_give(player, obj_bowsword, 1);
    if( slot >= 0 )
    {
        dm_talk_pick(srv, npc_monk, slot, 2);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_BOWSWORD_GIVEN,
                       "I'll take it there must hand the orb, got %d",
                       dm_get_vb(player, "devious_main"));
        SELFTEST_CHECK(selftest_count_obj(player, obj_orb) == 1,
                       "accept-orb branch must give the glowing orb");
        dm_pass("opnpc1_monk_accept_orb");
    }

    /* ---- Orb+pouch rejects + large/colossal success ---- */
    dm_vb(srv, "devious_main", DM_BOWSWORD_GIVEN);
    if( obj_orb > 0 && obj_small > 0 )
    {
        dm_clear_inv(player);
        dm_give(player, obj_orb, 1);
        dm_give(player, obj_small, 1);
        dm_opheldu(srv, obj_orb, obj_small);
        SELFTEST_CHECK(selftest_count_obj(player, obj_sealed) == 0,
                       "small pouch must reject the orb");
        SELFTEST_CHECK(selftest_count_obj(player, obj_orb) == 1, "reject must keep the orb");
        dm_pass("opheldu_orb_reject_small");
    }
    if( obj_orb > 0 && obj_med > 0 )
    {
        dm_clear_inv(player);
        dm_give(player, obj_orb, 1);
        dm_give(player, obj_med, 1);
        dm_opheldu(srv, obj_orb, obj_med);
        SELFTEST_CHECK(selftest_count_obj(player, obj_sealed) == 0,
                       "medium pouch must reject the orb");
        dm_pass("opheldu_orb_reject_medium");
    }
    if( obj_orb > 0 && obj_giant > 0 )
    {
        dm_clear_inv(player);
        dm_give(player, obj_orb, 1);
        dm_give(player, obj_giant, 1);
        dm_opheldu(srv, obj_orb, obj_giant);
        SELFTEST_CHECK(selftest_count_obj(player, obj_sealed) == 0,
                       "giant pouch must reject the orb");
        dm_pass("opheldu_orb_reject_giant");
    }
    if( obj_orb > 0 && obj_large_d > 0 )
    {
        dm_clear_inv(player);
        dm_give(player, obj_orb, 1);
        dm_give(player, obj_large_d, 1);
        dm_opheldu(srv, obj_orb, obj_large_d);
        SELFTEST_CHECK(selftest_count_obj(player, obj_sealed) == 0,
                       "degraded large pouch must reject the orb");
        dm_pass("opheldu_orb_reject_degraded_large");
    }
    if( obj_orb > 0 && obj_colo_d > 0 )
    {
        dm_clear_inv(player);
        dm_give(player, obj_orb, 1);
        dm_give(player, obj_colo_d, 1);
        dm_opheldu(srv, obj_orb, obj_colo_d);
        SELFTEST_CHECK(selftest_count_obj(player, obj_sealed_c) == 0,
                       "degraded colossal pouch must reject the orb");
        dm_pass("opheldu_orb_reject_degraded_colossal");
    }
    if( obj_orb > 0 && obj_large > 0 && obj_sealed > 0 )
    {
        dm_clear_inv(player);
        dm_give(player, obj_orb, 1);
        dm_give(player, obj_large, 1);
        dm_opheldu(srv, obj_orb, obj_large);
        SELFTEST_CHECK(selftest_count_obj(player, obj_sealed) == 1,
                       "large pouch must seal the orb");
        dm_pass("opheldu_orb_success_large");
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD3, obj_sealed, -1, -1);
        dm_finish(srv);
        dm_pass("opheld3_peek_large_pouch");
    }
    if( obj_orb > 0 && obj_colo > 0 && obj_sealed_c > 0 )
    {
        dm_clear_inv(player);
        dm_give(player, obj_orb, 1);
        dm_give(player, obj_colo, 1);
        dm_opheldu(srv, obj_orb, obj_colo);
        SELFTEST_CHECK(selftest_count_obj(player, obj_sealed_c) == 1,
                       "colossal pouch must seal the orb");
        dm_pass("opheldu_orb_success_colossal");
    }

    /* ---- Altar place + heist narration (large destroyed, colossal survives) ---- */
    if( loc_altar > 0 && obj_sealed > 0 )
    {
        dm_vb(srv, "devious_main", DM_BOWSWORD_GIVEN);
        dm_vb(srv, "devious_altar", 0);
        dm_vb(srv, "devious_cutscene", 0);
        dm_vb(srv, "devious_monk", 0);
        dm_clear_inv(player);
        if( obj_orb > 0 )
        {
            dm_give(player, obj_orb, 1);
            dm_oplocu(srv, loc_altar, DM_ALTAR_X, DM_ALTAR_Z, 0, obj_orb);
            SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_BOWSWORD_GIVEN,
                           "bare orb on the altar must refuse");
            dm_pass("oplocu_altar_bare_orb_reject");
        }
        dm_clear_inv(player);
        dm_give(player, obj_sealed, 1);
        dm_oplocu(srv, loc_altar, DM_ALTAR_X, DM_ALTAR_Z, 0, obj_sealed);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_POUCH_PLACED,
                       "large sealed pouch on altar must place, got %d",
                       dm_get_vb(player, "devious_main"));
        SELFTEST_CHECK(dm_get_vb(player, "devious_cutscene") == 1,
                       "heist narration must set %%devious_cutscene");
        SELFTEST_CHECK(dm_get_vb(player, "devious_monk") == 1,
                       "heist must flip %%devious_monk to the dead shell");
        SELFTEST_CHECK(dm_get_vb(player, "devious_altar") == 2,
                       "altar must scorched after the blast");
        SELFTEST_CHECK(selftest_count_obj(player, obj_sealed) == 0,
                       "large pouch is destroyed in the blast");
        SELFTEST_CHECK(selftest_count_obj(player, obj_large) == 0,
                       "destroyed large pouch must not return");
        dm_pass("oplocu_altar_large_destroyed");
    }
    dm_journal(srv, "journal_pouch_placed");

    if( loc_altar > 0 && obj_sealed_c > 0 && obj_colo > 0 )
    {
        dm_vb(srv, "devious_main", DM_BOWSWORD_GIVEN);
        dm_vb(srv, "devious_altar", 0);
        dm_vb(srv, "devious_cutscene", 0);
        dm_vb(srv, "devious_monk", 0);
        dm_clear_inv(player);
        dm_give(player, obj_sealed_c, 1);
        dm_oplocu(srv, loc_altar, DM_ALTAR_X, DM_ALTAR_Z, 0, obj_sealed_c);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_POUCH_PLACED,
                       "colossal sealed pouch on altar must place");
        SELFTEST_CHECK(selftest_count_obj(player, obj_colo) == 1,
                       "colossal pouch survives the blast");
        dm_pass("oplocu_altar_colossal_survives");
    }

    /* ---- High Priest after pouch / dead-monk search / report ---- */
    dm_free_npc(srv, slot);
    dm_vb(srv, "devious_main", DM_POUCH_PLACED);
    slot = dm_spawn(srv, npc_priest, DM_PRIEST_X, DM_PRIEST_Z, 0);
    SELFTEST_CHECK(slot >= 0, "High Priest should spawn");
    if( slot >= 0 )
    {
        dm_talk_drain(srv, npc_priest, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_POUCH_PLACED,
                       "High Priest after pouch must send to Paterdomus, not skip");
        dm_pass("opnpc1_high_priest_after_pouch");
    }

    dm_free_npc(srv, slot);
    slot = dm_spawn(srv, npc_dead, DM_MONK_X, DM_MONK_Z, 0);
    SELFTEST_CHECK(slot >= 0, "dead monk should spawn");
    if( slot >= 0 )
    {
        dm_vb(srv, "devious_main", DM_ACCEPTED);
        dm_talk_drain(srv, npc_dead, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_ACCEPTED,
                       "dead-monk search before the heist must refuse");
        dm_pass("opnpc1_dead_monk_too_early");

        dm_vb(srv, "devious_main", DM_POUCH_PLACED);
        dm_talk_drain(srv, npc_dead, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_MONK_FOUND_DEAD,
                       "searching the body must write monk_found_dead, got %d",
                       dm_get_vb(player, "devious_main"));
        dm_pass("opnpc1_dead_monk_search");

        dm_talk_drain(srv, npc_dead, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_MONK_FOUND_DEAD,
                       "already-searched body must stay monk_found_dead");
        dm_pass("opnpc1_dead_monk_already_searched");
    }
    dm_journal(srv, "journal_monk_found_dead");

    dm_free_npc(srv, slot);
    slot = dm_spawn(srv, npc_priest, DM_PRIEST_X, DM_PRIEST_Z, 0);
    if( slot >= 0 )
    {
        dm_vb(srv, "devious_main", DM_MONK_FOUND_DEAD);
        dm_talk_drain(srv, npc_priest, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_REPORTED_PRIEST,
                       "report to High Priest must write reported_priest, got %d",
                       dm_get_vb(player, "devious_main"));
        dm_pass("opnpc1_high_priest_after_dead_monk");

        dm_talk_drain(srv, npc_priest, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_REPORTED_PRIEST,
                       "High Priest after report must stay reported_priest");
        dm_pass("opnpc1_high_priest_after_report");
    }
    dm_journal(srv, "journal_reported_priest");

    /* ---- Tiffy too-early / Something else / Devious Minds complete ---- */
    dm_free_npc(srv, slot);
    slot = dm_spawn(srv, npc_tiffy, DM_TIFFY_X, DM_TIFFY_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Sir Tiffy stand-in should spawn");
    if( slot >= 0 )
    {
        dm_vb(srv, "devious_main", DM_MONK_FOUND_DEAD);
        dm_talk_drain(srv, npc_tiffy, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_MONK_FOUND_DEAD,
                       "Tiffy too-early must not complete");
        dm_pass("opnpc1_tiffy_too_early");

        dm_vb(srv, "devious_main", DM_REPORTED_PRIEST);
        dm_talk_pick(srv, npc_tiffy, slot, 2);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_REPORTED_PRIEST,
                       "Something else. must not complete");
        dm_pass("opnpc1_tiffy_something_else");

        smith_before = (stat_smith >= 0) ? player->stat_xp_tenths[stat_smith] : 0;
        rc_before = (stat_rc >= 0) ? player->stat_xp_tenths[stat_rc] : 0;
        fletch_before = (stat_fletch >= 0) ? player->stat_xp_tenths[stat_fletch] : 0;
        dm_talk_pick(srv, npc_tiffy, slot, 1);
        {
            int t;

            for( t = 0; t < 12; t++ )
                selftest_tick(srv);
        }
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_COMPLETE,
                       "Devious Minds topic must complete, got %d",
                       dm_get_vb(player, "devious_main"));
        if( stat_smith >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_smith] >=
                               smith_before + DM_REWARD_SMITH_TENTHS,
                           "complete must advance smithing by 65000 tenths");
        if( stat_rc >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_rc] >= rc_before + DM_REWARD_RC_TENTHS,
                           "complete must advance runecraft by 50000 tenths");
        if( stat_fletch >= 0 )
            SELFTEST_CHECK(
                player->stat_xp_tenths[stat_fletch] >= fletch_before + DM_REWARD_FLETCH_TENTHS,
                "complete must advance fletching by 50000 tenths");
        dm_pass("opnpc1_tiffy_devious_minds_complete");

        dm_talk_drain(srv, npc_tiffy, slot);
        SELFTEST_CHECK(dm_get_vb(player, "devious_main") == DM_COMPLETE,
                       "post-complete Tiffy must stay complete");
        dm_pass("opnpc1_tiffy_post_complete");
    }
    dm_journal(srv, "journal_complete");

    dm_free_npc(srv, slot);

    fprintf(stderr, "ToriRSServer deviousminds selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_DEVIOUSMINDS_SELFTEST_U_H */
