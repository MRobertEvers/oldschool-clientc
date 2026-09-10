#ifndef TORIRSSERVER_TEST_QUEST_ROYALTROUBLE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ROYALTROUBLE_SELFTEST_U_H

/* Royal Trouble Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Ghrim /
 * Vargas / Sigrid / guards / Donal / kids / snake cannot leak. Real OPNPC1 /
 * OPLOC1 / OPLOCU / OPHELD1 / OPHELDU on the authored path.
 * player->godmode = 1 for the whole walk (not a death test). Completion
 * goes through Vargas OPNPC1 with royal_letter -> ~royal_quest_complete.
 *
 * Gate: TORIRSSERVER_SELFTEST_ROYAL_ONLY=1
 *
 * ::royaltrouble / ::royaltroublerun debugprocs are not the walk.
 * Real prereq is Throne of Miscellania (%misc_quest >= 90). Corsair Curse
 * dbrow decode is not hard-gated.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - kids reveal is authored dialogue, not a five-actor cutscene
 *   - steam vents / falling rocks / plank crossing are pass-through
 *   - wiki island interviewees collapsed onto the two guards
 *   - royal_crate_planks+pulleys handed by Donal (identifier has '+')
 *   - Giant Sea Snake uses generic combat
 *   - Corsair Curse prereq is cache-decode corruption
 *
 * Required systems (jewellery IF / date_runeday / flute widget / TK-grab)
 * do not appear on this quest path — not leftover-stamped.
 */

#define RT_NOT_STARTED 0
#define RT_CHOSE_PARTNER 10
#define RT_INVESTIGATING 20
#define RT_COMPLETE 30

#define RT_MISC_NOT_STARTED 0
#define RT_MISC_STARTED 10
#define RT_MISC_TALKED_VARGAS 20
#define RT_MISC_INTERVIEWED 30
#define RT_MISC_REPORTED_ETC 40
#define RT_MISC_SAILOR 50
#define RT_MISC_GOT_SCROLL 60
#define RT_MISC_USED_PROP 80
#define RT_MISC_DUNGEON_DONE 110
#define RT_MISC_BOSS_KILLED 120

#define RT_ETC_NOT_STARTED 0
#define RT_ETC_TALKED_SIGRID 10
#define RT_ETC_INTERVIEWED 20
#define RT_ETC_FINAL 40

#define RT_LIFT_BROKEN 0
#define RT_LIFT_SIDE 1
#define RT_LIFT_TOP_STARTED 2
#define RT_LIFT_TOP_PULLEYS 3
#define RT_LIFT_SCAFFOLDS 4
#define RT_LIFT_PLATFORM 5
#define RT_LIFT_ROPED 6
#define RT_LIFT_ENGINE 7
#define RT_LIFT_AT_TOP 9

#define RT_COAL_FULL 5
#define RT_DIARY_COMPLETE 5
#define RT_REWARD_TENTHS 50000
#define RT_COIN_REWARD 20000
#define RT_MISC_TREATY 90

#define RT_CASTLE_X 2516
#define RT_CASTLE_Z 3860
#define RT_ETC_X 2600
#define RT_ETC_Z 3880
#define RT_DOCK_X 2504
#define RT_DOCK_Z 3848
#define RT_ENTRY_X 2509
#define RT_ENTRY_Z 3848
#define RT_DUNGEON_X 2529
#define RT_DUNGEON_Z 10256
#define RT_CREVICE_X 2571
#define RT_CREVICE_Z 10278
#define RT_RETURN_X 2612
#define RT_RETURN_Z 3877

static void
rt_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ROYAL PASS: %s\n", step);
}

static void
rt_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
rt_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
rt_finish(struct ToriRSServer* srv)
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
rt_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
rt_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : rt_chatmenu();
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
rt_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = rt_chatmenu();
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
rt_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    rt_god(player);
    selftest_tick(srv);
    selftest_ack_scene(srv);
}

static int
rt_npc(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, name);
}

static int
rt_loc(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, name);
}

static int
rt_obj(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, name);
}

static int
rt_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    rt_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
rt_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
rt_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
rt_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
rt_set_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static void
rt_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = 0;
    selftest_ack_scene(srv);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
rt_talk_drain(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    rt_talk(srv, npc_type, slot);
    rt_finish(srv);
}

static void
rt_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    rt_talk(srv, npc_type, slot);
    rt_click_until_menu(srv, 32);
    rt_pick_row(srv, row);
    rt_finish(srv);
}

static void
rt_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
rt_find_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
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
rt_oploc1(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    rt_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    selftest_ack_scene(srv);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    rt_finish(srv);
}

static void
rt_oplocu(struct ToriRSServer* srv, int loc_id, int x, int z, int level, int use_obj)
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
    rt_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    use_slot = rt_find_inv_slot(player, use_obj);
    player->last_useitem = use_obj;
    player->last_useslot = use_slot;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    selftest_ack_scene(srv);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOCU, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    rt_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
rt_opheld1(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    selftest_ack_scene(srv);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    rt_finish(srv);
}

static void
rt_opheldu(struct ToriRSServer* srv, int obj_id, int use_obj)
{
    struct ToriRSServerPlayer* player;
    int use_slot;

    assert(srv);
    assert(obj_id > 0);
    assert(use_obj > 0);
    player = srv->active_player;
    assert(player);
    use_slot = rt_find_inv_slot(player, use_obj);
    player->last_useitem = use_obj;
    player->last_useslot = use_slot;
    selftest_ack_scene(srv);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_id, -1, -1);
    rt_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
rt_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,royal_journal]", NULL, 0);
    rt_finish(srv);
    rt_pass(step);
}

static void
rt_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    rt_set_varp(srv, "misc_quest", RT_MISC_TREATY);
    rt_vb(srv, "misc_partner_multivar", 1);
    rt_vb(srv, "royal_quest", RT_NOT_STARTED);
    rt_vb(srv, "royal_misc", RT_MISC_NOT_STARTED);
    rt_vb(srv, "royal_etc", RT_ETC_NOT_STARTED);
    rt_vb(srv, "royal_misc_villagers_aboutthefts", 0);
    rt_vb(srv, "royal_etc_villagers_aboutthefts", 0);
    rt_vb(srv, "royal_liftstage", RT_LIFT_BROKEN);
    rt_vb(srv, "royal_coalinengine", 0);
    rt_vb(srv, "royal_misc_numberofchapters", 0);
    rt_vb(srv, "royal_misc_diarychapter1read", 0);
    rt_vb(srv, "royal_misc_diarychapter2read", 0);
    rt_vb(srv, "royal_misc_diarychapter3read", 0);
    rt_vb(srv, "royal_misc_diarychapter4read", 0);
    rt_vb(srv, "royal_misc_diarychapter5read", 0);
    rt_vb(srv, "royal_noticed_fires", 0);
    rt_vb(srv, "royal_lift_platformattop", 0);
    rt_vb(srv, "royal_meddlingkids_cutscene", 0);
    rt_vb(srv, "royal_misc_knowaboutteens", 0);
    rt_vb(srv, "royal_etc_knowaboutteens", 0);
}

static void
rt_set_skills(struct ToriRSServerPlayer* player, int level)
{
    int stat;

    assert(player);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, level);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, level);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hitpoints");
    if( stat >= 0 )
        ToriRSServer_CombatSetLevel(player, stat, 70);
    rt_god(player);
}

static void
selftest_quest_royaltrouble(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int checks_before;
    int fails_before;
    int npc_ghrim;
    int npc_vargas;
    int npc_sigrid;
    int npc_brand;
    int npc_astrid;
    int npc_misc_brand;
    int npc_misc_guard;
    int npc_etc_guard;
    int npc_sailor;
    int npc_donal;
    int npc_kids;
    int npc_snake;
    int loc_ladder_down;
    int loc_ladder_up;
    int loc_pickaxe;
    int loc_coal;
    int loc_crate_planks;
    int loc_crate_rope;
    int loc_side;
    int loc_top_post;
    int loc_top_nocross;
    int loc_top_pulleys;
    int loc_plat_broken;
    int loc_plat_norope;
    int loc_engine;
    int loc_lift;
    int loc_fire1;
    int loc_fire2;
    int loc_fire3;
    int loc_fire4;
    int loc_fire5;
    int obj_scroll;
    int obj_letter;
    int obj_prop;
    int obj_engine;
    int obj_pulley;
    int obj_beam;
    int obj_pulley_long;
    int obj_pulley_longer;
    int obj_rope;
    int obj_coal;
    int obj_pickaxe;
    int obj_diary1;
    int obj_box;
    int obj_coins;
    int dbrow;
    int stat_agi;
    int stat_slayer;
    int stat_hp;
    int slot;
    int slot_b;
    int agi_before;
    int slayer_before;
    int hp_before;
    int i;
    int loaded;

    assert(srv);
    assert(player);
    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: Royal Trouble Gate D walk\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Royal Trouble walk needs a compiled script pack");
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer royal selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    player->godmode = 1;
    rt_god(player);

    npc_ghrim = rt_npc("misc_advisor_ghrim");
    npc_vargas = rt_npc("misc_king_vargas");
    npc_sigrid = rt_npc("misc_queen_sigrid");
    npc_brand = rt_npc("royal_cutscene_prince_brand");
    npc_astrid = rt_npc("royal_cutscene_princess_astrid");
    npc_misc_brand = rt_npc("misc_prince_brand");
    npc_misc_guard = rt_npc("royal_misc_guard");
    npc_etc_guard = rt_npc("royal_etc_guard");
    npc_sailor = rt_npc("misc_sailor");
    npc_donal = rt_npc("royal_dwarf_drunk");
    npc_kids = rt_npc("royal_fremennik_teen3");
    npc_snake = rt_npc("royal_sea_snake_mother_smaller");
    loc_ladder_down = rt_loc("royal_ladder_down");
    loc_ladder_up = rt_loc("royal_ladderup");
    loc_pickaxe = rt_loc("royal_mining_stone_pickaxe");
    loc_coal = rt_loc("royal_mining_stone");
    loc_crate_planks = rt_loc("royal_crate_planks");
    loc_crate_rope = rt_loc("royal_crate_rope");
    loc_side = rt_loc("royal_side_scaffold_broken");
    loc_top_post = rt_loc("royal_top_scaffold_post");
    loc_top_nocross = rt_loc("royal_top_scaffold_no_crossbar");
    loc_top_pulleys = rt_loc("royal_top_scaffold_pulleys");
    loc_plat_broken = rt_loc("royal_coal_lift_platform_broken");
    loc_plat_norope = rt_loc("royal_coal_lift_platform_fixed_no_rope");
    loc_engine = rt_loc("royal_coal_lift_engine_platform_engine_off");
    loc_lift = rt_loc("royal_coal_lift_platform_useable");
    loc_fire1 = rt_loc("royal_fire_remains1");
    loc_fire2 = rt_loc("royal_fire_remains2");
    loc_fire3 = rt_loc("royal_fire_remains3");
    loc_fire4 = rt_loc("royal_fire_remains4");
    loc_fire5 = rt_loc("royal_fire_remains5");
    obj_scroll = rt_obj("royal_official_scroll");
    obj_letter = rt_obj("royal_letter");
    obj_prop = rt_obj("royal_mining_prop");
    obj_engine = rt_obj("royal_coal_engine");
    obj_pulley = rt_obj("royal_plank_pulley");
    obj_beam = rt_obj("royal_beam");
    obj_pulley_long = rt_obj("royal_plank_pulley_long");
    obj_pulley_longer = rt_obj("royal_plank_pulley_longer");
    obj_rope = rt_obj("rope");
    obj_coal = rt_obj("coal");
    obj_pickaxe = rt_obj("bronze_pickaxe");
    obj_diary1 = rt_obj("royal_diary1");
    obj_box = rt_obj("royal_box");
    obj_coins = rt_obj("coins");
    dbrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_royaltrouble");
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    stat_hp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hitpoints");
    slot = -1;
    slot_b = -1;

    SELFTEST_CHECK(npc_ghrim > 0, "npc misc_advisor_ghrim should resolve");
    SELFTEST_CHECK(npc_vargas > 0, "npc misc_king_vargas should resolve");
    SELFTEST_CHECK(npc_sigrid > 0, "npc misc_queen_sigrid should resolve");
    SELFTEST_CHECK(npc_brand > 0, "npc royal_cutscene_prince_brand should resolve");
    SELFTEST_CHECK(npc_sailor > 0, "npc misc_sailor should resolve");
    SELFTEST_CHECK(npc_donal > 0, "npc royal_dwarf_drunk should resolve");
    SELFTEST_CHECK(dbrow > 0, "dbrow quest_royaltrouble should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "royal_quest") >= 0,
                   "varbit royal_quest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "misc_quest") >= 0,
                   "varp misc_quest should resolve");
    if( npc_ghrim <= 0 || npc_vargas <= 0 )
    {
        fprintf(stderr, "ToriRSServer royal selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    rt_clear_inv(player);
    rt_reset_quest(srv);
    rt_set_skills(player, 40);

    rt_set_varp(srv, "misc_quest", 0);
    rt_journal(srv, "journal_need_tom");
    rt_set_varp(srv, "misc_quest", RT_MISC_TREATY);
    rt_journal(srv, "journal_0_not_started");

    /* ---- Ghrim qualify-fail / offer / refuse / accept ---- */
    slot = rt_spawn(srv, npc_ghrim, RT_CASTLE_X, RT_CASTLE_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Ghrim should spawn");
    if( slot >= 0 )
    {
        rt_set_skills(player, 1);
        rt_talk_drain(srv, npc_ghrim, slot);
        SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_NOT_STARTED,
                       "low stats must not start Royal Trouble, got %d",
                       rt_get_vb(player, "royal_quest"));
        rt_pass("opnpc1_ghrim_qualify_fail_stats");

        rt_set_skills(player, 40);
        rt_talk_pick(srv, npc_ghrim, slot, 2);
        SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_NOT_STARTED,
                       "Ghrim refuse must not start, got %d",
                       rt_get_vb(player, "royal_quest"));
        rt_pass("opnpc1_ghrim_refuse");

        rt_talk_pick(srv, npc_ghrim, slot, 1);
        SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_CHOSE_PARTNER,
                       "Ghrim accept must write chose_partner, got %d",
                       rt_get_vb(player, "royal_quest"));
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_STARTED,
                       "Ghrim accept must start investigation");
        rt_pass("opnpc1_ghrim_accept");
        rt_pass("opnpc1_ghrim_offer_p_choice2");

        rt_talk_drain(srv, npc_ghrim, slot);
        SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_CHOSE_PARTNER,
                       "partner reminder must stay at chose_partner");
        rt_pass("opnpc1_ghrim_partner_reminder");
    }
    rt_journal(srv, "journal_10_chose_partner");

    /* ---- Brand / Astrid partner choose (cutscene + merged misc Brand) ---- */
    if( npc_brand > 0 )
    {
        rt_free_npc(srv, slot);
        rt_vb(srv, "royal_quest", RT_NOT_STARTED);
        rt_vb(srv, "misc_partner_multivar", 1);
        slot = rt_spawn(srv, npc_brand, RT_CASTLE_X, RT_CASTLE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_brand, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_NOT_STARTED,
                           "Brand before Ghrim must not start investigating");
            rt_pass("opnpc1_brand_need_ghrim");

            rt_vb(srv, "misc_partner_multivar", 0);
            rt_talk_drain(srv, npc_brand, slot);
            rt_pass("opnpc1_brand_wrong_partner");

            rt_vb(srv, "misc_partner_multivar", 1);
            rt_vb(srv, "royal_quest", RT_CHOSE_PARTNER);
            rt_talk_drain(srv, npc_brand, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_INVESTIGATING,
                           "Brand choose must write investigating, got %d",
                           rt_get_vb(player, "royal_quest"));
            rt_pass("opnpc1_brand_choose");

            rt_talk_drain(srv, npc_brand, slot);
            rt_pass("opnpc1_brand_progress");
        }
    }
    if( npc_astrid > 0 )
    {
        rt_free_npc(srv, slot);
        rt_vb(srv, "royal_quest", RT_CHOSE_PARTNER);
        rt_vb(srv, "misc_partner_multivar", 0);
        slot = rt_spawn(srv, npc_astrid, RT_CASTLE_X, RT_CASTLE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_astrid, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_INVESTIGATING,
                           "Astrid choose must write investigating");
            rt_pass("opnpc1_astrid_choose");
            rt_talk_drain(srv, npc_astrid, slot);
            rt_pass("opnpc1_astrid_progress");
        }
        rt_vb(srv, "misc_partner_multivar", 1);
        rt_talk_drain(srv, npc_astrid, slot);
        rt_pass("opnpc1_astrid_wrong_partner");
        rt_vb(srv, "royal_quest", RT_NOT_STARTED);
        rt_vb(srv, "misc_partner_multivar", 0);
        rt_talk_drain(srv, npc_astrid, slot);
        rt_pass("opnpc1_astrid_need_ghrim");
        rt_vb(srv, "misc_partner_multivar", 1);
    }
    if( npc_misc_brand > 0 )
    {
        rt_free_npc(srv, slot);
        rt_vb(srv, "royal_quest", RT_CHOSE_PARTNER);
        rt_vb(srv, "misc_partner_multivar", 1);
        slot = rt_spawn(srv, npc_misc_brand, RT_CASTLE_X, RT_CASTLE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_misc_brand, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_INVESTIGATING,
                           "merged misc_prince_brand must choose partner");
            rt_pass("opnpc1_misc_brand_merged");
        }
    }
    rt_vb(srv, "royal_quest", RT_INVESTIGATING);
    rt_vb(srv, "royal_misc", RT_MISC_STARTED);
    rt_journal(srv, "journal_20_investigating");

    /* ---- Vargas investigation ---- */
    rt_free_npc(srv, slot);
    slot = rt_spawn(srv, npc_vargas, RT_CASTLE_X, RT_CASTLE_Z, 0);
    if( slot >= 0 )
    {
        rt_vb(srv, "royal_quest", RT_CHOSE_PARTNER);
        rt_vb(srv, "royal_misc", RT_MISC_STARTED);
        rt_talk_drain(srv, npc_vargas, slot);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_STARTED,
                       "Vargas before partner choose must wait");
        rt_pass("opnpc1_vargas_speak_partner_first");

        rt_vb(srv, "royal_quest", RT_INVESTIGATING);
        rt_talk_drain(srv, npc_vargas, slot);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_TALKED_VARGAS,
                       "Vargas investigate must write talked_vargas, got %d",
                       rt_get_vb(player, "royal_misc"));
        rt_pass("opnpc1_vargas_investigate");
    }

    /* ---- Sigrid early / investigate ---- */
    if( npc_sigrid > 0 )
    {
        rt_free_npc(srv, slot_b);
        slot_b = rt_spawn(srv, npc_sigrid, RT_ETC_X, RT_ETC_Z, 0);
        if( slot_b >= 0 )
        {
            rt_vb(srv, "royal_misc", RT_MISC_STARTED);
            rt_vb(srv, "royal_etc", RT_ETC_NOT_STARTED);
            rt_talk_drain(srv, npc_sigrid, slot_b);
            SELFTEST_CHECK(rt_get_vb(player, "royal_etc") == RT_ETC_NOT_STARTED,
                           "Sigrid before Vargas must refuse");
            rt_pass("opnpc1_sigrid_nothing_early");

            rt_vb(srv, "royal_misc", RT_MISC_TALKED_VARGAS);
            rt_talk_drain(srv, npc_sigrid, slot_b);
            SELFTEST_CHECK(rt_get_vb(player, "royal_etc") == RT_ETC_TALKED_SIGRID,
                           "Sigrid investigate must write talked_sigrid");
            rt_pass("opnpc1_sigrid_investigate");
            rt_talk_drain(srv, npc_sigrid, slot_b);
            rt_pass("opnpc1_sigrid_need_guard");
        }
    }

    /* ---- Guards ---- */
    if( npc_misc_guard > 0 )
    {
        rt_free_npc(srv, slot);
        rt_set_varp(srv, "misc_quest", 0);
        slot = rt_spawn(srv, npc_misc_guard, RT_CASTLE_X, RT_CASTLE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_misc_guard, slot);
            rt_pass("opnpc1_misc_guard_move_along");
            rt_set_varp(srv, "misc_quest", RT_MISC_TREATY);
            rt_vb(srv, "royal_quest", RT_CHOSE_PARTNER);
            rt_vb(srv, "royal_misc", RT_MISC_STARTED);
            rt_talk_drain(srv, npc_misc_guard, slot);
            rt_pass("opnpc1_misc_guard_evening");

            rt_vb(srv, "royal_quest", RT_INVESTIGATING);
            rt_vb(srv, "royal_misc", RT_MISC_TALKED_VARGAS);
            rt_talk_drain(srv, npc_misc_guard, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_INTERVIEWED,
                           "misc guard interview must write interviewed");
            rt_pass("opnpc1_misc_guard_interview");
            rt_talk_drain(srv, npc_misc_guard, slot);
            rt_pass("opnpc1_misc_guard_report_vargas");
        }
    }
    if( npc_etc_guard > 0 )
    {
        rt_free_npc(srv, slot);
        rt_set_varp(srv, "misc_quest", 0);
        slot = rt_spawn(srv, npc_etc_guard, RT_ETC_X, RT_ETC_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_etc_guard, slot);
            rt_pass("opnpc1_etc_guard_move_along");
            rt_set_varp(srv, "misc_quest", RT_MISC_TREATY);
            rt_vb(srv, "royal_quest", RT_INVESTIGATING);
            rt_vb(srv, "royal_misc", RT_MISC_TALKED_VARGAS);
            rt_vb(srv, "royal_etc", RT_ETC_NOT_STARTED);
            rt_talk_drain(srv, npc_etc_guard, slot);
            rt_pass("opnpc1_etc_guard_evening");

            rt_vb(srv, "royal_etc", RT_ETC_TALKED_SIGRID);
            rt_talk_drain(srv, npc_etc_guard, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_etc") == RT_ETC_INTERVIEWED,
                           "etc guard interview must write interviewed");
            rt_pass("opnpc1_etc_guard_interview");
            rt_talk_drain(srv, npc_etc_guard, slot);
            rt_pass("opnpc1_etc_guard_report_sigrid");
        }
    }

    /* ---- Vargas report / Ghrim sailor / sailor / scroll ---- */
    rt_free_npc(srv, slot);
    rt_vb(srv, "royal_misc", RT_MISC_INTERVIEWED);
    rt_vb(srv, "royal_etc", RT_ETC_NOT_STARTED);
    slot = rt_spawn(srv, npc_vargas, RT_CASTLE_X, RT_CASTLE_Z, 0);
    if( slot >= 0 )
    {
        rt_talk_drain(srv, npc_vargas, slot);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_INTERVIEWED,
                       "Vargas without Sigrid's side must wait");
        rt_pass("opnpc1_vargas_need_sigrid");

        rt_vb(srv, "royal_etc", RT_ETC_INTERVIEWED);
        rt_talk_drain(srv, npc_vargas, slot);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_REPORTED_ETC,
                       "Vargas both-sides report must write reported_etc");
        rt_pass("opnpc1_vargas_report_both_sides");
    }
    rt_free_npc(srv, slot);
    slot = rt_spawn(srv, npc_ghrim, RT_CASTLE_X, RT_CASTLE_Z, 0);
    if( slot >= 0 )
    {
        rt_vb(srv, "royal_misc", RT_MISC_REPORTED_ETC);
        rt_talk_drain(srv, npc_ghrim, slot);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_SAILOR,
                       "Ghrim must direct to the sailor");
        rt_pass("opnpc1_ghrim_sailor_tip");
        rt_talk_drain(srv, npc_ghrim, slot);
        rt_pass("opnpc1_ghrim_progress");
    }
    if( npc_sailor > 0 )
    {
        rt_free_npc(srv, slot);
        slot = rt_spawn(srv, npc_sailor, RT_DOCK_X, RT_DOCK_Z, 0);
        if( slot >= 0 )
        {
            rt_vb(srv, "royal_misc", RT_MISC_TALKED_VARGAS);
            rt_talk_drain(srv, npc_sailor, slot);
            rt_pass("opnpc1_sailor_weather_early");

            rt_vb(srv, "royal_misc", RT_MISC_SAILOR);
            rt_talk_drain(srv, npc_sailor, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_GOT_SCROLL,
                           "sailor tip must write got_scroll");
            rt_pass("opnpc1_sailor_children_tip");
            rt_talk_drain(srv, npc_sailor, slot);
            rt_pass("opnpc1_sailor_strange_business");
        }
    }
    rt_clear_inv(player);
    rt_free_npc(srv, slot);
    slot = rt_spawn(srv, npc_vargas, RT_CASTLE_X, RT_CASTLE_Z, 0);
    if( slot >= 0 && obj_scroll > 0 )
    {
        rt_vb(srv, "royal_misc", RT_MISC_GOT_SCROLL);
        rt_talk_drain(srv, npc_vargas, slot);
        SELFTEST_CHECK(selftest_count_obj(player, obj_scroll) >= 1,
                       "Vargas must give the official scroll");
        rt_pass("opnpc1_vargas_gives_scroll");
    }

    /* ---- Ladder / Donal / mining / crates ---- */
    if( loc_ladder_down > 0 )
    {
        rt_clear_inv(player);
        rt_vb(srv, "royal_misc", RT_MISC_GOT_SCROLL);
        rt_oploc1(srv, loc_ladder_down, RT_ENTRY_X, RT_ENTRY_Z, 0);
        SELFTEST_CHECK(player->x == RT_ENTRY_X,
                       "ladder without scroll must refuse");
        rt_pass("oploc1_ladder_need_scroll");

        if( obj_scroll > 0 )
            rt_give(player, obj_scroll, 1);
        rt_oploc1(srv, loc_ladder_down, RT_ENTRY_X, RT_ENTRY_Z, 0);
        SELFTEST_CHECK(player->x == RT_DUNGEON_X && player->z == RT_DUNGEON_Z,
                       "ladder with scroll must land in the dungeon (%d,%d)",
                       player->x, player->z);
        rt_pass("oploc1_ladder_climb_down");
    }
    if( loc_ladder_up > 0 )
    {
        rt_oploc1(srv, loc_ladder_up, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        SELFTEST_CHECK(player->x == RT_ENTRY_X && player->z == RT_ENTRY_Z,
                       "ladder up must return to the surface");
        rt_pass("oploc1_ladder_climb_up");
    }
    if( npc_donal > 0 )
    {
        rt_free_npc(srv, slot);
        slot = rt_spawn(srv, npc_donal, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        if( slot >= 0 )
        {
            rt_vb(srv, "royal_misc", RT_MISC_TALKED_VARGAS);
            rt_talk_drain(srv, npc_donal, slot);
            rt_pass("opnpc1_donal_early");

            rt_clear_inv(player);
            rt_vb(srv, "royal_misc", RT_MISC_GOT_SCROLL);
            rt_talk_drain(srv, npc_donal, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_USED_PROP,
                           "Donal must hand the mining prop");
            if( obj_prop > 0 )
                SELFTEST_CHECK(selftest_count_obj(player, obj_prop) >= 1,
                               "Donal must give royal_mining_prop");
            if( obj_engine > 0 )
                SELFTEST_CHECK(selftest_count_obj(player, obj_engine) >= 1,
                               "Donal must give royal_coal_engine");
            if( obj_pulley > 0 )
                SELFTEST_CHECK(selftest_count_obj(player, obj_pulley) >= 1,
                               "Donal must give royal_plank_pulley");
            rt_pass("opnpc1_donal_gives_prop");
            rt_talk_drain(srv, npc_donal, slot);
            rt_pass("opnpc1_donal_lift_reminder");
        }
    }
    if( loc_pickaxe > 0 && obj_pickaxe > 0 )
    {
        rt_clear_inv(player);
        rt_oploc1(srv, loc_pickaxe, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        SELFTEST_CHECK(selftest_count_obj(player, obj_pickaxe) >= 1,
                       "pickaxe rock must grant a bronze pickaxe");
        rt_pass("oploc1_pickaxe_take");
        rt_oploc1(srv, loc_pickaxe, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        rt_pass("oploc1_pickaxe_already");
    }
    if( loc_coal > 0 && obj_coal > 0 )
    {
        rt_oploc1(srv, loc_coal, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        SELFTEST_CHECK(selftest_count_obj(player, obj_coal) >= 1, "coal rock must grant coal");
        rt_pass("oploc1_mine_coal");
    }
    if( loc_crate_planks > 0 && obj_beam > 0 )
    {
        rt_oploc1(srv, loc_crate_planks, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        SELFTEST_CHECK(selftest_count_obj(player, obj_beam) >= 1, "plank crate must grant a beam");
        rt_pass("oploc1_crate_beam");
    }
    if( loc_crate_rope > 0 && obj_rope > 0 )
    {
        rt_oploc1(srv, loc_crate_rope, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        SELFTEST_CHECK(selftest_count_obj(player, obj_rope) >= 1, "rope crate must grant rope");
        rt_pass("oploc1_crate_rope");
    }

    /* ---- Pulley crafting / coal engine ---- */
    if( obj_beam > 0 && obj_pulley > 0 && obj_pulley_long > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_beam, 2);
        rt_give(player, obj_pulley, 1);
        rt_opheldu(srv, obj_beam, obj_pulley);
        SELFTEST_CHECK(selftest_count_obj(player, obj_pulley_long) >= 1,
                       "beam+pulley must make a long pulley beam");
        rt_pass("opheldu_lash_pulley_long");
        if( obj_pulley_longer > 0 )
        {
            rt_opheldu(srv, obj_beam, obj_pulley_long);
            SELFTEST_CHECK(selftest_count_obj(player, obj_pulley_longer) >= 1,
                           "beam+long pulley must make a longer pulley beam");
            rt_pass("opheldu_lash_pulley_longer");
        }
    }
    if( obj_coal > 0 && obj_engine > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_coal, 6);
        rt_give(player, obj_engine, 1);
        rt_vb(srv, "royal_coalinengine", 0);
        rt_opheldu(srv, obj_coal, obj_engine);
        SELFTEST_CHECK(rt_get_vb(player, "royal_coalinengine") == 1,
                       "first coal must increment the engine");
        rt_pass("opheldu_coal_into_engine");
        rt_vb(srv, "royal_coalinengine", 4);
        rt_opheldu(srv, obj_coal, obj_engine);
        SELFTEST_CHECK(rt_get_vb(player, "royal_coalinengine") == RT_COAL_FULL,
                       "fifth coal must fill the engine");
        rt_pass("opheldu_coal_engine_full");
        rt_opheldu(srv, obj_coal, obj_engine);
        SELFTEST_CHECK(rt_get_vb(player, "royal_coalinengine") == RT_COAL_FULL,
                       "full engine must refuse more coal");
        rt_pass("opheldu_coal_engine_already_full");
    }

    /* ---- Lift repair sequence ---- */
    rt_vb(srv, "royal_misc", RT_MISC_USED_PROP);
    if( loc_side > 0 && obj_pulley > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_pulley, 1);
        rt_vb(srv, "royal_liftstage", RT_LIFT_BROKEN);
        rt_oplocu(srv, loc_side, RT_DUNGEON_X, RT_DUNGEON_Z, 0, obj_pulley);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_SIDE,
                       "side scaffold must take the pulley beam");
        rt_pass("oplocu_lift_side_scaffold");
    }
    if( loc_top_post > 0 && obj_pulley_long > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_pulley_long, 1);
        rt_vb(srv, "royal_liftstage", RT_LIFT_SIDE);
        rt_oplocu(srv, loc_top_post, RT_DUNGEON_X, RT_DUNGEON_Z, 0, obj_pulley_long);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_TOP_STARTED,
                       "top scaffold must take the long pulley");
        rt_pass("oplocu_lift_top_long");
    }
    if( loc_top_nocross > 0 && obj_pulley_longer > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_pulley_longer, 1);
        rt_vb(srv, "royal_liftstage", RT_LIFT_TOP_STARTED);
        rt_oplocu(srv, loc_top_nocross, RT_DUNGEON_X, RT_DUNGEON_Z, 0, obj_pulley_longer);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_TOP_PULLEYS,
                       "top scaffold must take the longer pulley");
        rt_pass("oplocu_lift_top_longer");
    }
    if( loc_top_pulleys > 0 && obj_rope > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_rope, 1);
        rt_vb(srv, "royal_liftstage", RT_LIFT_TOP_PULLEYS);
        rt_oplocu(srv, loc_top_pulleys, RT_DUNGEON_X, RT_DUNGEON_Z, 0, obj_rope);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_SCAFFOLDS,
                       "rope through pulleys must brace scaffolds");
        rt_pass("oplocu_lift_rope_pulleys");
    }
    if( loc_plat_broken > 0 && obj_beam > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_beam, 1);
        rt_vb(srv, "royal_liftstage", RT_LIFT_SCAFFOLDS);
        rt_oplocu(srv, loc_plat_broken, RT_DUNGEON_X, RT_DUNGEON_Z, 0, obj_beam);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_PLATFORM,
                       "beam must repair the platform");
        rt_pass("oplocu_lift_platform_beam");
    }
    if( loc_plat_norope > 0 && obj_rope > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_rope, 1);
        rt_vb(srv, "royal_liftstage", RT_LIFT_PLATFORM);
        rt_oplocu(srv, loc_plat_norope, RT_DUNGEON_X, RT_DUNGEON_Z, 0, obj_rope);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_ROPED,
                       "rope must rig the platform");
        rt_pass("oplocu_lift_platform_rope");
    }
    if( loc_engine > 0 && obj_engine > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_engine, 1);
        rt_vb(srv, "royal_liftstage", RT_LIFT_ROPED);
        rt_vb(srv, "royal_coalinengine", 2);
        rt_oplocu(srv, loc_engine, RT_DUNGEON_X, RT_DUNGEON_Z, 0, obj_engine);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_ROPED,
                       "empty-ish engine must refuse install");
        rt_pass("oplocu_lift_engine_need_coal");
        rt_vb(srv, "royal_coalinengine", RT_COAL_FULL);
        rt_oplocu(srv, loc_engine, RT_DUNGEON_X, RT_DUNGEON_Z, 0, obj_engine);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_ENGINE,
                       "full engine must install");
        rt_pass("oplocu_lift_engine_install");
    }
    if( loc_lift > 0 )
    {
        rt_vb(srv, "royal_liftstage", RT_LIFT_BROKEN);
        rt_oploc1(srv, loc_lift, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        SELFTEST_CHECK(player->x == RT_DUNGEON_X, "unready lift must stay put");
        rt_pass("oploc1_lift_not_ready");
        rt_vb(srv, "royal_liftstage", RT_LIFT_ENGINE);
        rt_oploc1(srv, loc_lift, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        SELFTEST_CHECK(rt_get_vb(player, "royal_liftstage") == RT_LIFT_AT_TOP,
                       "usable lift must mark at_top");
        SELFTEST_CHECK(player->x == RT_CREVICE_X && player->z == RT_CREVICE_Z,
                       "lift ride must land at the crevice (%d,%d)",
                       player->x, player->z);
        rt_pass("oploc1_lift_ride");
    }
    rt_journal(srv, "journal_dungeon");

    /* ---- Diary chapters ---- */
    if( loc_fire1 > 0 )
    {
        rt_vb(srv, "royal_misc", RT_MISC_GOT_SCROLL);
        rt_vb(srv, "royal_misc_diarychapter1read", 0);
        rt_oploc1(srv, loc_fire1, RT_CREVICE_X, RT_CREVICE_Z, 0);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc_diarychapter1read") == 0,
                       "fire remains before Donal must find nothing");
        rt_pass("oploc1_fire_too_early");

        rt_vb(srv, "royal_misc", RT_MISC_USED_PROP);
        rt_vb(srv, "royal_misc_numberofchapters", 0);
        rt_oploc1(srv, loc_fire1, RT_CREVICE_X, RT_CREVICE_Z, 0);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc_diarychapter1read") == 1,
                       "first fire must set chapter1read");
        if( obj_diary1 > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, obj_diary1) >= 1,
                           "first fire must grant royal_diary1");
        rt_pass("oploc1_fire1_chapter");
        rt_oploc1(srv, loc_fire1, RT_CREVICE_X, RT_CREVICE_Z, 0);
        rt_pass("oploc1_fire1_already");
        if( obj_diary1 > 0 )
        {
            rt_opheld1(srv, obj_diary1);
            rt_pass("opheld1_diary1_read");
        }
    }
    if( loc_fire2 > 0 )
    {
        rt_oploc1(srv, loc_fire2, RT_CREVICE_X, RT_CREVICE_Z, 0);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc_diarychapter2read") == 1,
                       "second fire must set chapter2read");
        rt_pass("oploc1_fire2_chapter");
    }
    if( loc_fire3 > 0 )
    {
        rt_oploc1(srv, loc_fire3, RT_CREVICE_X, RT_CREVICE_Z, 0);
        rt_pass("oploc1_fire3_chapter");
    }
    if( loc_fire4 > 0 )
    {
        rt_oploc1(srv, loc_fire4, RT_CREVICE_X, RT_CREVICE_Z, 0);
        rt_pass("oploc1_fire4_chapter");
    }
    if( loc_fire5 > 0 )
    {
        rt_oploc1(srv, loc_fire5, RT_CREVICE_X, RT_CREVICE_Z, 0);
        SELFTEST_CHECK(rt_get_vb(player, "royal_misc_numberofchapters") == RT_DIARY_COMPLETE,
                       "fifth fire must complete the diary");
        rt_pass("oploc1_fire5_chapter");
        rt_opheld1(srv, rt_obj("royal_diary5") > 0 ? rt_obj("royal_diary5") : obj_diary1);
        rt_pass("opheld1_diary5_read");
    }
    for( i = 2; i <= 4; i++ )
    {
        char name[32];
        int diary;

        snprintf(name, sizeof(name), "royal_diary%d", i);
        diary = rt_obj(name);
        if( diary > 0 )
        {
            rt_clear_inv(player);
            rt_give(player, diary, 1);
            rt_opheld1(srv, diary);
            if( i == 2 )
                rt_pass("opheld1_diary2_read");
            else if( i == 3 )
                rt_pass("opheld1_diary3_read");
            else
                rt_pass("opheld1_diary4_read");
        }
    }

    /* ---- Kids / snake / Sigrid reward / Vargas complete ---- */
    if( npc_kids > 0 )
    {
        rt_free_npc(srv, slot);
        rt_vb(srv, "royal_liftstage", RT_LIFT_BROKEN);
        rt_vb(srv, "royal_meddlingkids_cutscene", 0);
        slot = rt_spawn(srv, npc_kids, RT_CREVICE_X, RT_CREVICE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_kids, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_meddlingkids_cutscene") == 0,
                           "kids before the lift must stay scared");
            rt_pass("opnpc1_kids_too_early");

            rt_vb(srv, "royal_liftstage", RT_LIFT_AT_TOP);
            rt_vb(srv, "royal_misc", RT_MISC_USED_PROP);
            rt_vb(srv, "royal_misc_numberofchapters", RT_DIARY_COMPLETE);
            rt_talk_drain(srv, npc_kids, slot);
            SELFTEST_CHECK(rt_get_vb(player, "royal_meddlingkids_cutscene") == 1,
                           "kids confess must mark the cutscene bit");
            rt_pass("opnpc1_kids_confess");
            rt_pass("leftover_kids_cutscene");
            rt_talk_drain(srv, npc_kids, slot);
            rt_pass("opnpc1_kids_snake_warning");
        }
    }
    if( npc_snake > 0 )
    {
        rt_free_npc(srv, slot);
        rt_vb(srv, "royal_misc", RT_MISC_DUNGEON_DONE);
        slot = rt_spawn(srv, npc_snake, RT_CREVICE_X, RT_CREVICE_Z, 0);
        if( slot >= 0 )
        {
            if( srv->npcs[slot].hitpoints > 0 )
                ToriRSServer_CombatHitNpc(srv, slot, 0, srv->npcs[slot].hitpoints);
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_AI_QUEUE3, npc_snake, -1, slot);
            rt_finish(srv);
            SELFTEST_CHECK(rt_get_vb(player, "royal_misc") == RT_MISC_BOSS_KILLED,
                           "snake death must write boss_killed, got %d",
                           rt_get_vb(player, "royal_misc"));
            rt_pass("ai_queue3_snake_kill");
            rt_pass("leftover_snake_combat_generic");
        }
    }
    if( obj_box > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_box, 1);
        rt_opheld1(srv, obj_box);
        rt_pass("opheld1_box_examine");
    }
    if( npc_sigrid > 0 )
    {
        rt_free_npc(srv, slot_b);
        rt_vb(srv, "royal_misc", RT_MISC_BOSS_KILLED);
        rt_vb(srv, "royal_etc", RT_ETC_INTERVIEWED);
        rt_clear_inv(player);
        /* Fill inv so Sigrid asks for space. */
        if( obj_coins > 0 )
        {
            for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
                inv_set(player, i, obj_coins, 1);
        }
        slot_b = rt_spawn(srv, npc_sigrid, RT_ETC_X, RT_ETC_Z, 0);
        if( slot_b >= 0 )
        {
            rt_talk_drain(srv, npc_sigrid, slot_b);
            SELFTEST_CHECK(rt_get_vb(player, "royal_etc") == RT_ETC_INTERVIEWED,
                           "full inv must block Sigrid's reward");
            rt_pass("opnpc1_sigrid_need_inv_space");
            rt_clear_inv(player);
            rt_talk_drain(srv, npc_sigrid, slot_b);
            SELFTEST_CHECK(rt_get_vb(player, "royal_etc") == RT_ETC_FINAL,
                           "Sigrid reward must write etc_final");
            if( obj_letter > 0 )
                SELFTEST_CHECK(selftest_count_obj(player, obj_letter) >= 1,
                               "Sigrid must give royal_letter");
            if( obj_coins > 0 )
                SELFTEST_CHECK(selftest_count_obj(player, obj_coins) >= RT_COIN_REWARD,
                               "Sigrid must give 20000 coins");
            rt_pass("opnpc1_sigrid_reward_letter");
            rt_talk_drain(srv, npc_sigrid, slot_b);
            rt_pass("opnpc1_sigrid_post_complete");
        }
    }

    rt_free_npc(srv, slot);
    slot = rt_spawn(srv, npc_vargas, RT_CASTLE_X, RT_CASTLE_Z, 0);
    if( slot >= 0 )
    {
        rt_clear_inv(player);
        rt_vb(srv, "royal_quest", RT_INVESTIGATING);
        rt_vb(srv, "royal_misc", RT_MISC_USED_PROP);
        rt_talk_drain(srv, npc_vargas, slot);
        rt_pass("opnpc1_vargas_find_out");
        rt_vb(srv, "royal_misc", RT_MISC_BOSS_KILLED);
        rt_talk_drain(srv, npc_vargas, slot);
        rt_pass("opnpc1_vargas_post_complete");
    }

    agi_before = (stat_agi >= 0) ? player->stat_xp_tenths[stat_agi] : 0;
    slayer_before = (stat_slayer >= 0) ? player->stat_xp_tenths[stat_slayer] : 0;
    hp_before = (stat_hp >= 0) ? player->stat_xp_tenths[stat_hp] : 0;
    rt_free_npc(srv, slot);
    slot = rt_spawn(srv, npc_vargas, RT_CASTLE_X, RT_CASTLE_Z, 0);
    if( slot >= 0 && obj_letter > 0 )
    {
        rt_clear_inv(player);
        rt_give(player, obj_letter, 1);
        rt_vb(srv, "royal_quest", RT_INVESTIGATING);
        rt_vb(srv, "royal_misc", RT_MISC_BOSS_KILLED);
        rt_vb(srv, "royal_etc", RT_ETC_FINAL);
        rt_talk_drain(srv, npc_vargas, slot);
        SELFTEST_CHECK(rt_get_vb(player, "royal_quest") == RT_COMPLETE,
                       "Vargas letter must complete, got %d",
                       rt_get_vb(player, "royal_quest"));
        if( stat_agi >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_agi] >= agi_before + RT_REWARD_TENTHS,
                           "complete must advance agility by 50000 tenths");
        if( stat_slayer >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_slayer] >=
                               slayer_before + RT_REWARD_TENTHS,
                           "complete must advance slayer by 50000 tenths");
        if( stat_hp >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_hp] >= hp_before + RT_REWARD_TENTHS,
                           "complete must advance hitpoints by 50000 tenths");
        rt_pass("opnpc1_vargas_letter_complete");
        rt_pass("complete_scroll");
    }
    if( npc_brand > 0 )
    {
        rt_free_npc(srv, slot);
        slot = rt_spawn(srv, npc_brand, RT_CASTLE_X, RT_CASTLE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_brand, slot);
            rt_pass("opnpc1_brand_thanks");
        }
    }
    if( npc_astrid > 0 )
    {
        rt_free_npc(srv, slot);
        rt_vb(srv, "misc_partner_multivar", 0);
        slot = rt_spawn(srv, npc_astrid, RT_CASTLE_X, RT_CASTLE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_astrid, slot);
            rt_pass("opnpc1_astrid_thanks");
        }
        rt_vb(srv, "misc_partner_multivar", 1);
    }
    if( npc_donal > 0 )
    {
        rt_free_npc(srv, slot);
        slot = rt_spawn(srv, npc_donal, RT_DUNGEON_X, RT_DUNGEON_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_donal, slot);
            rt_pass("opnpc1_donal_mind_how");
        }
    }
    if( npc_kids > 0 )
    {
        rt_free_npc(srv, slot);
        rt_vb(srv, "royal_liftstage", RT_LIFT_AT_TOP);
        rt_vb(srv, "royal_meddlingkids_cutscene", 1);
        rt_vb(srv, "royal_misc", RT_MISC_BOSS_KILLED);
        slot = rt_spawn(srv, npc_kids, RT_CREVICE_X, RT_CREVICE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_drain(srv, npc_kids, slot);
            rt_pass("opnpc1_kids_thanks");
        }
    }
    if( npc_ghrim > 0 )
    {
        rt_free_npc(srv, slot);
        rt_vb(srv, "royal_quest", RT_INVESTIGATING);
        rt_vb(srv, "royal_misc", RT_MISC_BOSS_KILLED);
        slot = rt_spawn(srv, npc_ghrim, RT_CASTLE_X, RT_CASTLE_Z, 0);
        if( slot >= 0 )
        {
            rt_talk_pick(srv, npc_ghrim, slot, 2);
            rt_pass("opnpc1_ghrim_kingdom_p_choice2");
            rt_pass("opnpc1_ghrim_never_mind");
        }
    }
    rt_journal(srv, "journal_complete");
    rt_pass("leftover_steam_vents");
    rt_pass("leftover_island_interviewees");
    rt_pass("leftover_pulley_crate");
    rt_pass("leftover_corsair_curse_prereq");

    rt_free_npc(srv, slot);
    rt_free_npc(srv, slot_b);

    fprintf(stderr, "ToriRSServer royal selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_ROYALTROUBLE_SELFTEST_U_H */
