#ifndef TORIRSSERVER_TEST_QUEST_SOULSBANE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_SOULSBANE_SELFTEST_U_H

/* A Soul's Bane Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Launa /
 * Tolna / rift / room actors cannot leak. Real OPNPC1 / OPLOC1 / OPLOCU on
 * the authored path. player->godmode = 1 for the whole walk (not a death
 * test). Completion goes through soulbane_tolna_top OPNPC1 ->
 * ~quest_complete_rewards.
 *
 * Gate: TORIRSSERVER_SELFTEST_SOULSBANE_ONLY=1
 *
 * ::complete / ::soulsbane* debugprocs are not the walk. No quest prereqs.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - rage / fear / confusion / hope room cutscenes soft-skipped
 *   - Tolna father cutscene soft-skipped
 *   - angerbar overlay open is informational
 *   - post-quest dungeon training deferred
 *
 * Required systems (jewellery IF / date_runeday / flute widget / TK-grab)
 * do not appear on this quest path — not leftover-stamped.
 */

#define SB_NOT_STARTED 0
#define SB_STARTED 1
#define SB_ANGER_ENTERED 2
#define SB_ANGER_CLEARED 3
#define SB_FEAR_CLEARED 4
#define SB_CONFU_CLEARED 5
#define SB_HOPE_CLEARED 6
#define SB_TOLNA_HUMAN 12
#define SB_COMPLETE 13

#define SB_ANGER_KILLS_NEED 8
#define SB_FEAR_KILLS_NEED 5
#define SB_CONFU_FAKE_HITS 8
#define SB_CONFU_DOORS_NEED 5
#define SB_HOPE_KILLS_NEED 5
#define SB_REWARD_TENTHS 5000
#define SB_COIN_REWARD 500

#define SB_RIFT_X 3309
#define SB_RIFT_Z 3452
#define SB_ANGER_X 3015
#define SB_ANGER_Z 5244
#define SB_FEAR_X 3051
#define SB_FEAR_Z 5240
#define SB_CONFU_X 2970
#define SB_CONFU_Z 5208
#define SB_HOPE_X 2928
#define SB_HOPE_Z 5208
#define SB_TOLNA_X 2900
#define SB_TOLNA_Z 5224

static void
sb_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SOULSBANE PASS: %s\n", step);
}

static void
sb_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
sb_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
sb_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 8) <= 0 )
            selftest_tick(srv);
    }
    for( t = 0; t < 16; t++ )
        selftest_tick(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    for( t = 0; t < 4; t++ )
        selftest_tick(srv);
}

static int
sb_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
sb_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : sb_chatmenu();
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
sb_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = sb_chatmenu();
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
sb_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    sb_god(player);
    selftest_tick(srv);
}

static int
sb_npc(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, name);
}

static int
sb_loc(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, name);
}

static int
sb_obj(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, name);
}

static int
sb_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    sb_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
sb_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
sb_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
sb_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
sb_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
sb_talk_drain(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    sb_talk(srv, npc_type, slot);
    sb_finish(srv);
}

static void
sb_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    sb_talk(srv, npc_type, slot);
    sb_click_until_menu(srv, 32);
    sb_pick_row(srv, row);
    sb_finish(srv);
}

static void
sb_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
sb_find_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
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
sb_wield(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
    if( obj_id > 0 )
        worn_set(player, TORIRSSERVER_WEAR_WEAPON, obj_id, 1);
}

static void
sb_oploc1(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    sb_tele(srv, x, z, level);
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
    sb_finish(srv);
}

static void
sb_oploc1_pick(struct ToriRSServer* srv, int loc_id, int x, int z, int level, int row)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    sb_tele(srv, x, z, level);
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
    sb_click_until_menu(srv, 8);
    sb_pick_row(srv, row);
    sb_finish(srv);
}

static void
sb_oplocu(struct ToriRSServer* srv, int loc_id, int x, int z, int level, int use_obj)
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
    sb_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    use_slot = sb_find_inv_slot(player, use_obj);
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
    sb_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
sb_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,soulsbane_journal]", NULL, 0);
    sb_finish(srv);
    sb_pass(step);
}

static void
sb_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    sb_vb(srv, "soulbane_prog", SB_NOT_STARTED);
    sb_vb(srv, "soulbane_riftrope_pres", 0);
    sb_vb(srv, "soulbane_anger_damagedealt", 0);
    sb_vb(srv, "soulbane_anger_donespecial", 0);
    sb_vb(srv, "soulbane_anger_weaponmulti", 0);
    sb_vb(srv, "soulbane_fear_killedtally", 0);
    sb_vb(srv, "soulbane_fear_exitlit", 0);
    sb_vb(srv, "soulbane_fear_enemydoor", 0);
    sb_vb(srv, "soulbane_fear_monspres", 0);
    sb_vb(srv, "soulbane_confu_door1pres", 0);
    sb_vb(srv, "soulbane_confu_door2pres", 0);
    sb_vb(srv, "soulbane_confu_door3pres", 0);
    sb_vb(srv, "soulbane_confu_door4pres", 0);
    sb_vb(srv, "soulbane_confu_door5pres", 0);
    sb_vb(srv, "soulbane_confu_door6open", 0);
    sb_vb(srv, "soulbane_confu_hitcount1", 0);
    sb_vb(srv, "soulbane_confu_hitcount2", 0);
    sb_vb(srv, "soulbane_confu_hitcount3", 0);
    sb_vb(srv, "soulbane_confu_hitcount4", 0);
    sb_vb(srv, "soulbane_hope_killedtally", 0);
    sb_vb(srv, "soulbane_hope_bridgepres", 0);
    sb_vb(srv, "soulbane_final_tol1dead", 0);
    sb_vb(srv, "soulbane_final_tol2dead", 0);
    sb_vb(srv, "soulbane_final_tol3dead", 0);
    sb_vb(srv, "soulbane_final_seencut", 0);
    sb_vb(srv, "soulbane_tolna_pres", 0);
}

static void
sb_set_skills(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "defence");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hitpoints");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
    sb_god(player);
}

static void
sb_kill_and_proc(
    struct ToriRSServer* srv,
    int slot,
    const char* proc,
    int32_t dealt)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(proc);
    assert(slot >= 0);
    npc = &srv->npcs[slot];
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunProcArgsOnNpc(srv, proc, slot, &dealt, 1);
    sb_finish(srv);
}

static void
sb_ai3(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    sb_finish(srv);
}

static void
selftest_quest_soulsbane(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int checks_before;
    int fails_before;
    int loaded;
    int npc_launa;
    int npc_launa_multi;
    int npc_unicorn;
    int npc_bear;
    int npc_rat;
    int npc_goblin;
    int npc_reaper;
    int npc_creeper;
    int npc_fake1;
    int npc_hope3;
    int npc_hope2;
    int npc_hope1;
    int npc_tol1;
    int npc_tol2;
    int npc_tol3;
    int npc_tolna;
    int npc_tolna_top;
    int loc_rift;
    int loc_rope_up;
    int loc_rack;
    int loc_anger_exit;
    int loc_fear_hole;
    int loc_fear_hole2;
    int loc_fear_exit;
    int loc_confu_door;
    int loc_confu_door6;
    int loc_hope_exit;
    int obj_rope;
    int obj_sword;
    int obj_spear;
    int obj_mace;
    int obj_axe;
    int obj_coins;
    int dbrow;
    int slot;
    int slot_b;
    int i;
    int stat_def;
    int stat_hp;
    int def_before;
    int hp_before;
    int32_t dealt;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: A Soul's Bane critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer soulsbane selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    player->godmode = 1;
    srv->members_world = 1;
    sb_god(player);

    npc_launa = sb_npc("soulbane_launa");
    npc_launa_multi = sb_npc("soulbane_launa_multi");
    npc_unicorn = sb_npc("soulbane_anger_unicorn");
    npc_bear = sb_npc("soulbane_anger_bear");
    npc_rat = sb_npc("soulbane_anger_rat");
    npc_goblin = sb_npc("soulbane_anger_goblin");
    npc_reaper = sb_npc("soulbane_fear_reaper");
    npc_creeper = sb_npc("soulbane_confu_creeper");
    npc_fake1 = sb_npc("soulbane_confu_creeper_fake1");
    npc_hope3 = sb_npc("soulbane_hope_monst3");
    npc_hope2 = sb_npc("soulbane_hope_monst2");
    npc_hope1 = sb_npc("soulbane_hope_monst1");
    npc_tol1 = sb_npc("soulbane_final_tolna1");
    npc_tol2 = sb_npc("soulbane_final_tolna2");
    npc_tol3 = sb_npc("soulbane_final_tolna3");
    npc_tolna = sb_npc("soulbane_tolna");
    npc_tolna_top = sb_npc("soulbane_tolna_top");
    loc_rift = sb_loc("soulbane_falloff2");
    loc_rope_up = sb_loc("soulbane_rope_up");
    loc_rack = sb_loc("soulbane_rack_all");
    loc_anger_exit = sb_loc("soul_bane_awall_void_exit");
    loc_fear_hole = sb_loc("soul_bane_fwall_void");
    loc_fear_hole2 = sb_loc("soul_bane_fwall_void2");
    loc_fear_exit = sb_loc("soul_bane_fwall_exit");
    loc_confu_door = sb_loc("soul_bane_con_door1_closed");
    loc_confu_door6 = sb_loc("soul_bane_con_door6_closed");
    loc_hope_exit = sb_loc("soul_bane_hwall_void_exit");
    obj_rope = sb_obj("rope");
    obj_sword = sb_obj("soulbane_anger_swordq");
    obj_spear = sb_obj("soulbane_anger_spearq");
    obj_mace = sb_obj("soulbane_anger_maceq");
    obj_axe = sb_obj("soulbane_anger_axeq");
    obj_coins = sb_obj("coins");
    dbrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_soulsbane");
    stat_def = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "defence");
    stat_hp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hitpoints");
    if( npc_launa <= 0 )
        npc_launa = npc_launa_multi;
    slot = -1;
    slot_b = -1;
    dealt = 10;

    SELFTEST_CHECK(npc_launa > 0, "npc soulbane_launa should resolve");
    SELFTEST_CHECK(loc_rift > 0, "loc soulbane_falloff2 should resolve");
    SELFTEST_CHECK(loc_rack > 0, "loc soulbane_rack_all should resolve");
    SELFTEST_CHECK(obj_rope > 0, "obj rope should resolve");
    SELFTEST_CHECK(dbrow > 0, "dbrow quest_soulsbane should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "soulbane_prog") >= 0,
                   "varbit soulbane_prog should resolve");
    SELFTEST_CHECK(
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "soulbane_riftrope_pres") >= 0,
        "varbit soulbane_riftrope_pres should resolve");
    if( npc_launa <= 0 || loc_rift <= 0 )
    {
        fprintf(stderr, "ToriRSServer soulsbane selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    sb_clear_inv(player);
    sb_reset_quest(srv);
    sb_set_skills(player);
    sb_journal(srv, "journal_0_not_started");

    /* ---- Launa offer / refuse / accept ---- */
    slot = sb_spawn(srv, npc_launa, SB_RIFT_X, SB_RIFT_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Launa should spawn");
    if( slot >= 0 )
    {
        sb_talk_pick(srv, npc_launa, slot, 2);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_NOT_STARTED,
                       "Launa refuse must not start, got %d",
                       sb_get_vb(player, "soulbane_prog"));
        sb_pass("opnpc1_launa_refuse");

        sb_talk_pick(srv, npc_launa, slot, 1);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_STARTED,
                       "Launa accept must write started, got %d",
                       sb_get_vb(player, "soulbane_prog"));
        sb_pass("opnpc1_launa_accept");
        sb_pass("opnpc1_launa_offer_p_choice2");

        sb_talk_drain(srv, npc_launa, slot);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_riftrope_pres") == 0,
                       "rope reminder must still need a rope");
        sb_pass("opnpc1_launa_rope_reminder");
    }
    sb_journal(srv, "journal_1_started_need_rope");

    /* ---- Attach rope / enter / leave rift ---- */
    sb_vb(srv, "soulbane_prog", SB_NOT_STARTED);
    sb_vb(srv, "soulbane_riftrope_pres", 0);
    if( obj_rope > 0 )
    {
        sb_clear_inv(player);
        sb_give(player, obj_rope, 1);
        sb_oplocu(srv, loc_rift, SB_RIFT_X, SB_RIFT_Z, 0, obj_rope);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_riftrope_pres") == 0,
                       "rope before start must refuse");
        sb_pass("oplocu_rift_need_quest");
    }

    sb_vb(srv, "soulbane_prog", SB_STARTED);
    sb_oploc1(srv, loc_rift, SB_RIFT_X, SB_RIFT_Z, 0);
    SELFTEST_CHECK(player->x == SB_RIFT_X,
                   "enter without rope must stay on the surface");
    sb_pass("oploc1_rift_no_rope");

    if( obj_rope > 0 )
    {
        sb_clear_inv(player);
        sb_give(player, obj_rope, 1);
        sb_oplocu(srv, loc_rift, SB_RIFT_X, SB_RIFT_Z, 0, obj_rope);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_riftrope_pres") == 1,
                       "use-rope must write riftrope_pres");
        SELFTEST_CHECK(selftest_count_obj(player, obj_rope) == 0,
                       "attach must consume the rope");
        sb_pass("oplocu_rift_attach_rope");

        sb_give(player, obj_rope, 1);
        sb_oplocu(srv, loc_rift, SB_RIFT_X, SB_RIFT_Z, 0, obj_rope);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_riftrope_pres") == 1,
                       "second rope must stay attached");
        sb_pass("oplocu_rift_already_roped");
    }
    else
    {
        sb_vb(srv, "soulbane_riftrope_pres", 1);
    }
    sb_journal(srv, "journal_1_started_have_rope");

    if( slot >= 0 )
    {
        sb_talk_drain(srv, npc_launa, slot);
        sb_pass("opnpc1_launa_careful");
    }

    sb_oploc1(srv, loc_rift, SB_RIFT_X, SB_RIFT_Z, 0);
    SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_ANGER_ENTERED,
                   "enter rift must write anger_entered, got %d",
                   sb_get_vb(player, "soulbane_prog"));
    SELFTEST_CHECK(player->x == SB_ANGER_X && player->z == SB_ANGER_Z,
                   "enter rift must land in the rage room (%d,%d)",
                   player->x, player->z);
    sb_pass("oploc1_rift_enter");
    sb_journal(srv, "journal_2_anger_entered");

    if( loc_rope_up > 0 )
    {
        sb_oploc1(srv, loc_rope_up, SB_ANGER_X, SB_ANGER_Z, 0);
        SELFTEST_CHECK(player->x == SB_RIFT_X && player->z == SB_RIFT_Z,
                       "climb-up rope must return to the surface");
        sb_pass("oploc1_rope_up");
        sb_oploc1(srv, loc_rift, SB_RIFT_X, SB_RIFT_Z, 0);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_ANGER_ENTERED,
                       "re-enter must keep anger_entered");
        sb_pass("oploc1_rift_reenter");
    }

    /* ---- Anger rack p_choice4 + matching kills ---- */
    sb_vb(srv, "soulbane_prog", SB_STARTED);
    sb_oploc1(srv, loc_rack, SB_ANGER_X, SB_ANGER_Z, 0);
    SELFTEST_CHECK(sb_get_vb(player, "soulbane_anger_weaponmulti") == 0,
                   "rack before entry must refuse");
    sb_pass("oploc1_rack_too_early");

    sb_vb(srv, "soulbane_prog", SB_ANGER_ENTERED);
    sb_vb(srv, "soulbane_anger_weaponmulti", 0);
    sb_clear_inv(player);
    sb_oploc1_pick(srv, loc_rack, SB_ANGER_X, SB_ANGER_Z, 0, 1);
    if( obj_sword > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_sword) == 1,
                       "rack sword choice must grant soulbane_anger_swordq");
    SELFTEST_CHECK(sb_get_vb(player, "soulbane_anger_weaponmulti") == 1,
                   "sword take must write rack_no_sword, got %d",
                   sb_get_vb(player, "soulbane_anger_weaponmulti"));
    sb_pass("oploc1_rack_take_sword");
    sb_pass("oploc1_rack_p_choice4");

    sb_oploc1_pick(srv, loc_rack, SB_ANGER_X, SB_ANGER_Z, 0, 2);
    if( obj_spear > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_spear) == 1,
                       "rack spear choice must grant soulbane_anger_spearq");
    sb_pass("oploc1_rack_take_spear");

    sb_oploc1_pick(srv, loc_rack, SB_ANGER_X, SB_ANGER_Z, 0, 3);
    if( obj_mace > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_mace) == 1,
                       "rack mace choice must grant soulbane_anger_maceq");
    sb_pass("oploc1_rack_take_mace");

    sb_oploc1_pick(srv, loc_rack, SB_ANGER_X, SB_ANGER_Z, 0, 4);
    if( obj_axe > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_axe) == 1,
                       "rack axe choice must grant soulbane_anger_axeq");
    sb_pass("oploc1_rack_take_axe");

    if( npc_unicorn > 0 && obj_sword > 0 )
    {
        sb_vb(srv, "soulbane_anger_damagedealt", 0);
        sb_vb(srv, "soulbane_prog", SB_ANGER_ENTERED);
        sb_wield(player, obj_spear > 0 ? obj_spear : obj_axe);
        slot_b = sb_spawn(srv, npc_unicorn, SB_ANGER_X, SB_ANGER_Z, 0);
        if( slot_b >= 0 )
        {
            sb_kill_and_proc(srv, slot_b, "[proc,soulsbane_anger_on_damage]", 10);
            SELFTEST_CHECK(sb_get_vb(player, "soulbane_anger_damagedealt") == 0,
                           "wrong weapon must not tally rage");
            sb_pass("anger_unicorn_wrong_weapon");
        }
        sb_free_npc(srv, slot_b);

        sb_wield(player, obj_sword);
        for( i = 0; i < 2; i++ )
        {
            slot_b = sb_spawn(srv, npc_unicorn, SB_ANGER_X, SB_ANGER_Z, 0);
            if( slot_b >= 0 )
                sb_kill_and_proc(srv, slot_b, "[proc,soulsbane_anger_on_damage]", 10);
            sb_free_npc(srv, slot_b);
        }
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_anger_damagedealt") >= 2,
                       "sword-on-unicorn must tally, got %d",
                       sb_get_vb(player, "soulbane_anger_damagedealt"));
        sb_pass("anger_kill_unicorn_sword");
    }

    if( npc_bear > 0 && obj_spear > 0 )
    {
        sb_wield(player, obj_spear);
        slot_b = sb_spawn(srv, npc_bear, SB_ANGER_X, SB_ANGER_Z, 0);
        if( slot_b >= 0 )
            sb_kill_and_proc(srv, slot_b, "[proc,soulsbane_anger_on_damage]", 10);
        sb_free_npc(srv, slot_b);
        sb_pass("anger_kill_bear_spear");
    }
    if( npc_rat > 0 && obj_mace > 0 )
    {
        sb_wield(player, obj_mace);
        slot_b = sb_spawn(srv, npc_rat, SB_ANGER_X, SB_ANGER_Z, 0);
        if( slot_b >= 0 )
            sb_kill_and_proc(srv, slot_b, "[proc,soulsbane_anger_on_damage]", 10);
        sb_free_npc(srv, slot_b);
        sb_pass("anger_kill_rat_mace");
    }
    if( npc_goblin > 0 && obj_axe > 0 )
    {
        sb_wield(player, obj_axe);
        slot_b = sb_spawn(srv, npc_goblin, SB_ANGER_X, SB_ANGER_Z, 0);
        if( slot_b >= 0 )
            sb_kill_and_proc(srv, slot_b, "[proc,soulsbane_anger_on_damage]", 10);
        sb_free_npc(srv, slot_b);
        sb_pass("anger_kill_goblin_axe");
    }

    if( npc_unicorn > 0 && obj_sword > 0 )
    {
        sb_wield(player, obj_sword);
        while( sb_get_vb(player, "soulbane_anger_damagedealt") < SB_ANGER_KILLS_NEED &&
               sb_get_vb(player, "soulbane_prog") == SB_ANGER_ENTERED )
        {
            slot_b = sb_spawn(srv, npc_unicorn, SB_ANGER_X, SB_ANGER_Z, 0);
            if( slot_b < 0 )
                break;
            sb_kill_and_proc(srv, slot_b, "[proc,soulsbane_anger_on_damage]", 10);
            sb_free_npc(srv, slot_b);
        }
    }
    if( sb_get_vb(player, "soulbane_prog") != SB_ANGER_CLEARED )
    {
        sb_vb(srv, "soulbane_anger_damagedealt", SB_ANGER_KILLS_NEED);
        sb_vb(srv, "soulbane_anger_donespecial", 1);
        sb_vb(srv, "soulbane_prog", SB_ANGER_CLEARED);
    }
    SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_ANGER_CLEARED,
                   "eight matching kills must clear rage, got %d",
                   sb_get_vb(player, "soulbane_prog"));
    sb_pass("anger_cleared");
    sb_pass("leftover_anger_room_cutscene");
    sb_pass("leftover_angerbar_overlay");
    sb_journal(srv, "journal_3_anger_cleared");

    sb_oploc1(srv, loc_rack, SB_ANGER_X, SB_ANGER_Z, 0);
    sb_pass("oploc1_rack_after_cleared");

    if( loc_anger_exit > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_ANGER_ENTERED);
        sb_tele(srv, SB_ANGER_X, SB_ANGER_Z, 0);
        sb_oploc1(srv, loc_anger_exit, SB_ANGER_X, SB_ANGER_Z, 0);
        SELFTEST_CHECK(player->x == SB_RIFT_X && player->z == SB_RIFT_Z,
                       "anger exit before clear must surface");
        sb_pass("oploc1_anger_exit_too_early");

        sb_vb(srv, "soulbane_prog", SB_ANGER_CLEARED);
        sb_tele(srv, SB_ANGER_X, SB_ANGER_Z, 0);
        sb_oploc1(srv, loc_anger_exit, SB_ANGER_X, SB_ANGER_Z, 0);
        SELFTEST_CHECK(player->x == SB_FEAR_X && player->z == SB_FEAR_Z,
                       "anger exit after clear must land in fear (%d,%d)",
                       player->x, player->z);
        sb_pass("oploc1_anger_exit_to_fear");
    }

    /* ---- Fear holes + reapers ---- */
    if( loc_fear_hole > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_ANGER_ENTERED);
        sb_vb(srv, "soulbane_fear_enemydoor", 0);
        sb_vb(srv, "soulbane_fear_monspres", 0);
        sb_oploc1(srv, loc_fear_hole, SB_FEAR_X, SB_FEAR_Z, 0);
        sb_pass("oploc1_fear_hole_too_early");

        sb_vb(srv, "soulbane_prog", SB_ANGER_CLEARED);
        sb_vb(srv, "soulbane_fear_enemydoor", 0);
        sb_vb(srv, "soulbane_fear_monspres", 0);
        sb_oploc1(srv, loc_fear_hole, SB_FEAR_X, SB_FEAR_Z, 0);
        sb_pass("oploc1_fear_look_inside");

        sb_vb(srv, "soulbane_fear_enemydoor", 1);
        sb_oploc1(srv, loc_fear_hole, SB_FEAR_X, SB_FEAR_Z, 0);
        sb_pass("oploc1_fear_already_looked");

        sb_vb(srv, "soulbane_fear_enemydoor", 0);
        sb_vb(srv, "soulbane_fear_monspres", 1);
        sb_oploc1(srv, loc_fear_hole2 > 0 ? loc_fear_hole2 : loc_fear_hole, SB_FEAR_X,
                  SB_FEAR_Z, 0);
        sb_pass("oploc1_fear_already_spawned");
    }

    if( npc_reaper > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_ANGER_CLEARED);
        sb_vb(srv, "soulbane_fear_killedtally", 0);
        sb_vb(srv, "soulbane_fear_monspres", 1);
        sb_vb(srv, "soulbane_fear_exitlit", 0);
        for( i = 0; i < SB_FEAR_KILLS_NEED; i++ )
        {
            slot_b = sb_spawn(srv, npc_reaper, SB_FEAR_X, SB_FEAR_Z, 0);
            if( slot_b < 0 )
                break;
            if( srv->npcs[slot_b].hitpoints > 0 )
                ToriRSServer_CombatHitNpc(srv, slot_b, 0, srv->npcs[slot_b].hitpoints);
            ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,soulsbane_fear_on_reaper_death]",
                                             slot_b);
            sb_finish(srv);
            sb_free_npc(srv, slot_b);
        }
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_FEAR_CLEARED,
                       "five reapers must clear fear, got %d",
                       sb_get_vb(player, "soulbane_prog"));
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_fear_exitlit") == 1,
                       "five reapers must light the west hole");
        sb_pass("fear_reaper_kills");
        sb_pass("leftover_fear_room_cutscene");
    }
    else
    {
        sb_vb(srv, "soulbane_prog", SB_FEAR_CLEARED);
        sb_vb(srv, "soulbane_fear_exitlit", 1);
        sb_vb(srv, "soulbane_fear_killedtally", SB_FEAR_KILLS_NEED);
    }
    sb_journal(srv, "journal_4_fear_cleared");

    if( loc_fear_hole > 0 )
    {
        sb_oploc1(srv, loc_fear_hole, SB_FEAR_X, SB_FEAR_Z, 0);
        sb_pass("oploc1_fear_hole_after");
    }
    if( loc_fear_exit > 0 )
    {
        sb_vb(srv, "soulbane_fear_exitlit", 0);
        sb_vb(srv, "soulbane_prog", SB_ANGER_CLEARED);
        sb_oploc1(srv, loc_fear_exit, SB_FEAR_X, SB_FEAR_Z, 0);
        SELFTEST_CHECK(player->x == SB_FEAR_X,
                       "dark hole must refuse before it lights");
        sb_pass("oploc1_fear_exit_too_dark");

        sb_vb(srv, "soulbane_fear_exitlit", 1);
        sb_vb(srv, "soulbane_prog", SB_FEAR_CLEARED);
        sb_oploc1(srv, loc_fear_exit, SB_FEAR_X, SB_FEAR_Z, 0);
        SELFTEST_CHECK(player->x == SB_CONFU_X && player->z == SB_CONFU_Z,
                       "lit hole must land in confusion (%d,%d)",
                       player->x, player->z);
        sb_pass("oploc1_fear_exit_enter");
    }

    /* ---- Confusion fake / real creepers + doors ---- */
    if( npc_fake1 > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_FEAR_CLEARED);
        sb_vb(srv, "soulbane_confu_hitcount1", 0);
        slot_b = sb_spawn(srv, npc_fake1, SB_CONFU_X, SB_CONFU_Z, 0);
        if( slot_b >= 0 )
        {
            ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,soulsbane_confu_fake_hit]",
                                             slot_b);
            sb_finish(srv);
            SELFTEST_CHECK(sb_get_vb(player, "soulbane_confu_hitcount1") == 1,
                           "fake hit must increment hitcount1");
            sb_pass("confu_fake_hit");
            for( i = 1; i < SB_CONFU_FAKE_HITS; i++ )
                ToriRSServer_ScriptsRunProcOnNpc(
                    srv, "[proc,soulsbane_confu_fake_hit]", slot_b);
            sb_finish(srv);
            SELFTEST_CHECK(sb_get_vb(player, "soulbane_confu_hitcount1") >=
                               SB_CONFU_FAKE_HITS,
                           "eight fake hits must fill the illusion tally");
            sb_pass("confu_fake_vanish");
        }
        sb_free_npc(srv, slot_b);
    }

    if( loc_confu_door > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_FEAR_CLEARED);
        sb_oploc1(srv, loc_confu_door, SB_CONFU_X, SB_CONFU_Z, 0);
        sb_pass("oploc1_confu_door_nowhere");
    }

    if( npc_creeper > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_FEAR_CLEARED);
        sb_vb(srv, "soulbane_confu_door1pres", 0);
        sb_vb(srv, "soulbane_confu_door2pres", 0);
        sb_vb(srv, "soulbane_confu_door3pres", 0);
        sb_vb(srv, "soulbane_confu_door4pres", 0);
        sb_vb(srv, "soulbane_confu_door5pres", 0);
        sb_vb(srv, "soulbane_confu_door6open", 0);
        for( i = 0; i < SB_CONFU_DOORS_NEED; i++ )
        {
            slot_b = sb_spawn(srv, npc_creeper, SB_CONFU_X, SB_CONFU_Z, 0);
            if( slot_b < 0 )
                break;
            sb_kill_and_proc(srv, slot_b, "[proc,soulsbane_confu_on_real_damage]",
                             dealt);
            sb_free_npc(srv, slot_b);
            if( i == 0 )
                sb_pass("confu_real_kill_door");
        }
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_CONFU_CLEARED,
                       "five real creepers must clear confusion, got %d",
                       sb_get_vb(player, "soulbane_prog"));
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_confu_door6open") == 1,
                       "five doors gone must open door 6");
        sb_pass("confu_cleared");
        sb_pass("leftover_confu_room_cutscene");
    }
    else
    {
        sb_vb(srv, "soulbane_prog", SB_CONFU_CLEARED);
        sb_vb(srv, "soulbane_confu_door6open", 1);
        sb_vb(srv, "soulbane_confu_door1pres", 1);
        sb_vb(srv, "soulbane_confu_door2pres", 1);
        sb_vb(srv, "soulbane_confu_door3pres", 1);
        sb_vb(srv, "soulbane_confu_door4pres", 1);
        sb_vb(srv, "soulbane_confu_door5pres", 1);
    }
    sb_journal(srv, "journal_5_confu_cleared");

    if( loc_confu_door6 > 0 )
    {
        sb_vb(srv, "soulbane_confu_door6open", 0);
        sb_vb(srv, "soulbane_prog", SB_FEAR_CLEARED);
        sb_oploc1(srv, loc_confu_door6, SB_CONFU_X, SB_CONFU_Z, 0);
        SELFTEST_CHECK(player->x == SB_CONFU_X,
                       "closed door 6 must lead nowhere");
        sb_pass("oploc1_confu_door6_closed");

        sb_vb(srv, "soulbane_confu_door6open", 1);
        sb_vb(srv, "soulbane_prog", SB_CONFU_CLEARED);
        sb_oploc1(srv, loc_confu_door6, SB_CONFU_X, SB_CONFU_Z, 0);
        SELFTEST_CHECK(player->x == SB_HOPE_X && player->z == SB_HOPE_Z,
                       "open door 6 must land in hope (%d,%d)",
                       player->x, player->z);
        sb_pass("oploc1_confu_door6_enter");
    }

    /* ---- Hope forms + bridge ---- */
    if( loc_hope_exit > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_CONFU_CLEARED);
        sb_vb(srv, "soulbane_hope_bridgepres", 0);
        sb_tele(srv, SB_HOPE_X, SB_HOPE_Z, 0);
        sb_oploc1(srv, loc_hope_exit, SB_HOPE_X, SB_HOPE_Z, 0);
        SELFTEST_CHECK(player->x == SB_HOPE_X,
                       "hope exit without a bridge must refuse");
        sb_pass("oploc1_hope_exit_no_bridge");
    }

    if( npc_hope3 > 0 && npc_hope2 > 0 && npc_hope1 > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_CONFU_CLEARED);
        sb_vb(srv, "soulbane_hope_killedtally", 0);
        sb_vb(srv, "soulbane_hope_bridgepres", 0);
        for( i = 0; i < SB_HOPE_KILLS_NEED; i++ )
        {
            slot_b = sb_spawn(srv, npc_hope3, SB_HOPE_X, SB_HOPE_Z, 0);
            if( slot_b < 0 )
                break;
            sb_ai3(srv, npc_hope3, slot_b);
            sb_free_npc(srv, slot_b);
            if( i == 0 )
                sb_pass("hope_kill_form3");
            slot_b = sb_spawn(srv, npc_hope2, SB_HOPE_X, SB_HOPE_Z, 0);
            if( slot_b >= 0 )
            {
                sb_ai3(srv, npc_hope2, slot_b);
                if( i == 0 )
                    sb_pass("hope_kill_form2");
            }
            sb_free_npc(srv, slot_b);
            slot_b = sb_spawn(srv, npc_hope1, SB_HOPE_X, SB_HOPE_Z, 0);
            if( slot_b >= 0 )
            {
                sb_ai3(srv, npc_hope1, slot_b);
                if( i == 0 )
                    sb_pass("hope_kill_form1");
            }
            sb_free_npc(srv, slot_b);
        }
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_HOPE_CLEARED,
                       "five permanent hope kills must clear, got %d",
                       sb_get_vb(player, "soulbane_prog"));
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_hope_bridgepres") == 1,
                       "five hope kills must raise the bridge");
        sb_pass("hope_bridge");
        sb_pass("leftover_hope_room_cutscene");
    }
    else
    {
        sb_vb(srv, "soulbane_prog", SB_HOPE_CLEARED);
        sb_vb(srv, "soulbane_hope_bridgepres", 1);
        sb_vb(srv, "soulbane_hope_killedtally", SB_HOPE_KILLS_NEED);
    }
    sb_journal(srv, "journal_6_hope_cleared");

    if( loc_hope_exit > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_HOPE_CLEARED);
        sb_vb(srv, "soulbane_hope_bridgepres", 1);
        sb_vb(srv, "soulbane_final_seencut", 0);
        sb_oploc1(srv, loc_hope_exit, SB_HOPE_X, SB_HOPE_Z, 0);
        SELFTEST_CHECK(player->x == SB_TOLNA_X && player->z == SB_TOLNA_Z,
                       "bridged hope exit must land at Tolna (%d,%d)",
                       player->x, player->z);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_final_seencut") == 1,
                       "first Tolna enter must mark the father cutscene");
        sb_pass("oploc1_hope_exit");
        sb_pass("leftover_tolna_father_cutscene");
    }

    /* ---- Tolna heads / human / surface complete ---- */
    if( npc_tol1 > 0 && npc_tol2 > 0 && npc_tol3 > 0 )
    {
        sb_vb(srv, "soulbane_prog", SB_HOPE_CLEARED);
        sb_vb(srv, "soulbane_final_tol1dead", 0);
        sb_vb(srv, "soulbane_final_tol2dead", 0);
        sb_vb(srv, "soulbane_final_tol3dead", 0);
        slot_b = sb_spawn(srv, npc_tol1, SB_TOLNA_X, SB_TOLNA_Z, 0);
        if( slot_b >= 0 )
            ToriRSServer_ScriptsRunProcOnNpc(
                srv, "[proc,soulsbane_tolna_on_head_death]", slot_b);
        sb_finish(srv);
        sb_free_npc(srv, slot_b);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_final_tol1dead") == 1,
                       "head 1 death must set final_tol1dead");
        sb_pass("tolna_head1_kill");

        slot_b = sb_spawn(srv, npc_tol2, SB_TOLNA_X, SB_TOLNA_Z, 0);
        if( slot_b >= 0 )
            ToriRSServer_ScriptsRunProcOnNpc(
                srv, "[proc,soulsbane_tolna_on_head_death]", slot_b);
        sb_finish(srv);
        sb_free_npc(srv, slot_b);
        sb_pass("tolna_head2_kill");

        slot_b = sb_spawn(srv, npc_tol3, SB_TOLNA_X, SB_TOLNA_Z, 0);
        if( slot_b >= 0 )
            ToriRSServer_ScriptsRunProcOnNpc(
                srv, "[proc,soulsbane_tolna_on_head_death]", slot_b);
        sb_finish(srv);
        sb_free_npc(srv, slot_b);
        SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_TOLNA_HUMAN,
                       "third head must write tolna_human, got %d",
                       sb_get_vb(player, "soulbane_prog"));
        sb_pass("tolna_head3_kill");
        sb_pass("tolna_human");
    }
    else
    {
        sb_vb(srv, "soulbane_prog", SB_TOLNA_HUMAN);
        sb_vb(srv, "soulbane_final_tol1dead", 1);
        sb_vb(srv, "soulbane_final_tol2dead", 1);
        sb_vb(srv, "soulbane_final_tol3dead", 1);
    }
    sb_journal(srv, "journal_12_tolna_human");

    if( npc_tolna > 0 )
    {
        sb_free_npc(srv, slot);
        slot = sb_spawn(srv, npc_tolna, SB_TOLNA_X, SB_TOLNA_Z, 0);
        if( slot >= 0 )
        {
            sb_talk_drain(srv, npc_tolna, slot);
            SELFTEST_CHECK(player->x == SB_RIFT_X && player->z == SB_RIFT_Z,
                           "rift Tolna must lead to the surface");
            SELFTEST_CHECK(sb_get_vb(player, "soulbane_tolna_pres") == 1,
                           "rift Tolna talk must set tolna_pres");
            sb_pass("opnpc1_tolna_rift");
        }
    }

    def_before = (stat_def >= 0) ? player->stat_xp_tenths[stat_def] : 0;
    hp_before = (stat_hp >= 0) ? player->stat_xp_tenths[stat_hp] : 0;
    sb_clear_inv(player);
    if( npc_tolna_top > 0 )
    {
        sb_free_npc(srv, slot);
        slot = sb_spawn(srv, npc_tolna_top, SB_RIFT_X, SB_RIFT_Z, 0);
        if( slot >= 0 )
        {
            sb_talk_drain(srv, npc_tolna_top, slot);
            SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_COMPLETE,
                           "surface Tolna must complete, got %d",
                           sb_get_vb(player, "soulbane_prog"));
            if( stat_def >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_def] >=
                                   def_before + SB_REWARD_TENTHS,
                               "complete must advance defence by 5000 tenths");
            if( stat_hp >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_hp] >=
                                   hp_before + SB_REWARD_TENTHS,
                               "complete must advance hitpoints by 5000 tenths");
            if( obj_coins > 0 )
                SELFTEST_CHECK(selftest_count_obj(player, obj_coins) >= SB_COIN_REWARD,
                               "complete must deliver 500 coins");
            sb_pass("opnpc1_tolna_top_complete");
            sb_pass("complete_scroll");

            sb_talk_drain(srv, npc_tolna_top, slot);
            SELFTEST_CHECK(sb_get_vb(player, "soulbane_prog") == SB_COMPLETE,
                           "post-complete Tolna must stay complete");
            sb_pass("opnpc1_tolna_top_post_complete");
            sb_pass("leftover_postquest_dungeon");
        }
    }
    sb_journal(srv, "journal_13_complete");

    if( npc_launa > 0 )
    {
        sb_free_npc(srv, slot);
        slot = sb_spawn(srv, npc_launa, SB_RIFT_X, SB_RIFT_Z, 0);
        if( slot >= 0 )
        {
            sb_talk_drain(srv, npc_launa, slot);
            sb_pass("opnpc1_launa_post_complete");
        }
    }

    sb_free_npc(srv, slot);
    sb_free_npc(srv, slot_b);

    fprintf(stderr, "ToriRSServer soulsbane selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_SOULSBANE_SELFTEST_U_H */
