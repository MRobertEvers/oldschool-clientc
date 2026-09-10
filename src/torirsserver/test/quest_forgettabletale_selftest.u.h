#ifndef TORIRSSERVER_TEST_QUEST_FORGETTABLETALE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_FORGETTABLETALE_SELFTEST_U_H

/* A Forgettable Tale... Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Keldagrim npcs cannot leak. Real OPNPC /
 * OPLOC / OPLOCU / OPHELD1 on the authored path. player->godmode = 1 for
 * the whole walk (no death test). Completion goes through kebab Eat ->
 * ~forget_quest_complete. Additive FT only -- do not rewrite Giant Dwarf,
 * Between a Rock, Fishing Contest, Construction, or MTA.
 *
 * Gate: TORIRSSERVER_SELFTEST_FT_ONLY=1
 */

#define FT_NOT_STARTED 0
#define FT_TOLD 10
#define FT_MET 20
#define FT_KELDA 25
#define FT_SEEDS 30
#define FT_HARVESTED 40
#define FT_YEAST 42
#define FT_WATER 43
#define FT_MALT 44
#define FT_HOPS 45
#define FT_YEAST_IN 46
#define FT_WAITED 47
#define FT_VALVE 48
#define FT_STOUT 50
#define FT_GAVE 60
#define FT_REVEALED 65
#define FT_CONDUCTOR 70
#define FT_DIRECTOR 80
#define FT_ENTERED 100
#define FT_ROOM1 105
#define FT_ROOM2 110
#define FT_ROOM3 118
#define FT_CUTSCENE 119
#define FT_REPORTED 120
#define FT_KEBAB 130
#define FT_COMPLETE 140

#define FT_FARM_WEEDS3 0
#define FT_FARM_WEEDED 3
#define FT_FARM_PLANTED 4
#define FT_FARM_GROWN 8
#define FT_FARM_HARVESTED 9

#define FT_GD_COMPLETE 50
#define FT_FC_COMPLETE 5
#define FT_REQ_COOKING 20
#define FT_REQ_FARMING 17
#define FT_REWARD_COOKING 50000
#define FT_REWARD_FARMING 50000

#define FT_VELDABAN_X 2827
#define FT_VELDABAN_Z 10214
#define FT_DRUNK_X 2913
#define FT_DRUNK_Z 10221
#define FT_ROWDY_X 2914
#define FT_ROWDY_Z 10198
#define FT_KHORVAK_X 2864
#define FT_KHORVAK_Z 9878
#define FT_GAUSS_X 2839
#define FT_GAUSS_Z 10196
#define FT_RIND_X 2854
#define FT_RIND_Z 10196
#define FT_PATCH_X 2854
#define FT_PATCH_Z 10203
#define FT_VAT_X 2918
#define FT_VAT_Z 10195
#define FT_VALVE_X 2918
#define FT_VALVE_Z 10193
#define FT_BARREL_X 2917
#define FT_BARREL_Z 10193
#define FT_CONDUCTOR8_X 2922
#define FT_CONDUCTOR8_Z 10166
#define FT_SECRET_CART_X 2919
#define FT_SECRET_CART_Z 10164
#define FT_BOX_X 1862
#define FT_BOX_Z 4954
#define FT_CTRL_X 1860
#define FT_CTRL_Z 4955
#define FT_KEBAB_X 2915
#define FT_KEBAB_Z 10193

static void
ft_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "FT PASS: %s\n", step);
}

static void
ft_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ft_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ft_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 64 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
ft_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ft_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ft_chatmenu();
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
ft_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ft_chatmenu();
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
ft_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ft_god(player);
    selftest_tick(srv);
}

static int
ft_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    ft_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
ft_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
ft_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
ft_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( ft_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
    }
}

static void
ft_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ft_get_bit(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
ft_set_fc(struct ToriRSServerPlayer* player, int value)
{
    int varp;

    assert(player);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fishingcompo");
    if( varp >= 0 )
        player->varps[varp] = value;
}

static int
ft_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    ft_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
ft_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
ft_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ft_talk(srv, npc_type, slot);
    ft_finish(srv);
}

static void
ft_talk_refuse(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ft_talk(srv, npc_type, slot);
    ft_click_until_menu(srv, 16);
    ft_pick_row(srv, 2);
    ft_finish(srv);
}

static void
ft_oploc_finish(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    ft_finish(srv);
}

static void
ft_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    ft_finish(srv);
    player->last_useitem = -1;
}

static void
ft_reset_state(struct ToriRSServer* srv)
{
    assert(srv);
    ft_set_bit(srv, "forget_quest", FT_NOT_STARTED);
    ft_set_bit(srv, "forget_farming", FT_FARM_WEEDS3);
    ft_set_bit(srv, "forget_seed2_given", 0);
    ft_set_bit(srv, "forget_seed3_given", 0);
    ft_set_bit(srv, "forget_seed4_given", 0);
    ft_set_bit(srv, "forget_boarding_removed", 0);
    ft_set_bit(srv, "forget_tunnel_cutscene1", 0);
    ft_set_bit(srv, "forget_tunnel_cutscene2", 0);
    ft_set_bit(srv, "forget_tunnel_cutscene3", 0);
    ft_set_bit(srv, "giantdwarf_quest", FT_GD_COMPLETE);
    if( srv->active_player )
        ft_set_fc(srv->active_player, FT_FC_COMPLETE);
}

static void
selftest_quest_forgettabletale(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_cooking;
    int stat_farming;
    int npc_veldaban;
    int npc_drunk;
    int npc_rowdy;
    int npc_khorvak;
    int npc_gauss;
    int npc_rind;
    int npc_blandebir;
    int npc_conductor8;
    int npc_director;
    int loc_patch;
    int loc_vat;
    int loc_valve;
    int loc_barrel;
    int loc_cart;
    int loc_box;
    int loc_ctrl;
    int obj_coins;
    int obj_beer;
    int obj_seed;
    int obj_stout_dwarf;
    int obj_rake;
    int obj_dibber;
    int obj_hops;
    int obj_water;
    int obj_malt;
    int obj_yeast;
    int obj_glass;
    int obj_kelda_stout;
    int obj_kebab;
    int obj_reward;
    int slot;
    int loc_slot;
    int cooking_xp_before;
    int farming_xp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: forgettabletale critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer forgettabletale selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    ft_god(player);

    stat_cooking = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
    stat_farming = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "farming");
    npc_veldaban = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_black_guard_leader");
    npc_drunk = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_drunken_dwarf");
    npc_rowdy = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_rowdy_dwarf");
    npc_khorvak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfrock_engineer2");
    npc_gauss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarf_city_dwarf_man6");
    npc_rind = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_gardener_dwarf");
    npc_blandebir = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "blandebir");
    npc_conductor8 = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_train_conductor8");
    npc_director = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_director_blue_opal");
    loc_patch = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "farming_hops_patch_keldagrim");
    loc_vat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brewing_vat_1");
    loc_valve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vat_valve_1");
    loc_barrel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brewing_barrel_1");
    loc_cart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "keldagrim_train_cart");
    loc_box = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "keldagrim_track_junction_card_box");
    loc_ctrl = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "keldagrim_track_junction_control_box");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_beer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "beer");
    obj_seed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kelda_hop_seed");
    obj_stout_dwarf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarven_stout");
    obj_rake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rake");
    obj_dibber = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dibber");
    obj_hops = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kelda_hops");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_water");
    obj_malt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "barley_malt");
    obj_yeast = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ale_yeast");
    obj_glass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "beer_glass");
    obj_kelda_stout = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kelda_stout");
    obj_kebab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kebab");
    obj_reward = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mature_dwarven_stout");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "forget_quest") >= 0,
                   "varbit forget_quest should resolve");
    SELFTEST_CHECK(npc_veldaban > 0, "npc dwarf_city_black_guard_leader should resolve");
    SELFTEST_CHECK(npc_drunk > 0, "npc dwarf_city_drunken_dwarf should resolve");
    SELFTEST_CHECK(obj_seed > 0, "obj kelda_hop_seed should resolve");
    SELFTEST_CHECK(obj_kelda_stout > 0, "obj kelda_stout should resolve");
    if( npc_veldaban <= 0 || npc_drunk <= 0 )
    {
        fprintf(stderr, "ToriRSServer forgettabletale selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    if( stat_cooking >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_cooking, FT_REQ_COOKING);
    if( stat_farming >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_farming, FT_REQ_FARMING);

    ft_clear_inv(player);
    ft_reset_state(srv);
    ft_god(player);

    ToriRSServer_ScriptsRunProc(srv, "[proc,forget_journal]", NULL, 0);
    ft_finish(srv);
    ft_pass("journal_not_started");

    /* ---- Veldaban prereq refuses / start / mid ---- */
    slot = ft_spawn(srv, npc_veldaban, FT_VELDABAN_X, FT_VELDABAN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Veldaban should spawn");
    if( slot >= 0 )
    {
        ft_set_bit(srv, "giantdwarf_quest", 0);
        ft_set_fc(player, FT_FC_COMPLETE);
        ft_set_bit(srv, "forget_quest", FT_NOT_STARTED);
        ft_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_NOT_STARTED,
                       "GD-incomplete must not start Forgettable Tale");
        ft_pass("opnpc1_veldaban_gd_incomplete");

        ft_set_bit(srv, "giantdwarf_quest", FT_GD_COMPLETE);
        ft_set_fc(player, 0);
        ft_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_NOT_STARTED,
                       "Fishing Contest incomplete must not start Forgettable Tale");
        ft_pass("opnpc1_veldaban_fc_incomplete");

        ft_set_fc(player, FT_FC_COMPLETE);
        ft_talk_refuse(srv, npc_veldaban, slot);
        SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_NOT_STARTED,
                       "refusing Veldaban must not start Forgettable Tale");
        ft_pass("opnpc1_veldaban_refuse");

        ft_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_TOLD,
                       "accepting Veldaban must write told_of_redaxe, got %d",
                       ft_get_bit(player, "forget_quest"));
        ft_pass("opnpc1_veldaban_accept");

        ft_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_TOLD,
                       "mid Veldaban must stay on the Drunken Dwarf");
        ft_pass("opnpc1_veldaban_mid");
        ft_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,forget_journal]", NULL, 0);
    ft_finish(srv);
    ft_pass("journal_told");

    /* ---- Drunken Dwarf beer / kelda / stout / reveal ---- */
    if( npc_drunk > 0 )
    {
        int drunk = ft_spawn(srv, npc_drunk, FT_DRUNK_X, FT_DRUNK_Z, 0);

        SELFTEST_CHECK(drunk >= 0, "Drunken Dwarf should spawn");
        if( drunk >= 0 )
        {
            ft_set_bit(srv, "forget_quest", FT_NOT_STARTED);
            ft_talk_finish(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_NOT_STARTED,
                           "too-early Drunken Dwarf must not advance");
            ft_pass("opnpc1_drunk_too_early");

            ft_set_bit(srv, "forget_quest", FT_TOLD);
            ft_talk_finish(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_MET,
                           "first Red Axe ask must write met_drunkdwarf, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("opnpc1_drunk_dry_throat");

            ft_talk_finish(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_MET,
                           "beer-poor Drunken Dwarf must stay met");
            ft_pass("opnpc1_drunk_beer_poor");

            if( obj_coins > 0 )
                ft_give(player, obj_coins, 10);
            ft_talk_refuse(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_MET,
                           "refusing the beer buy must stay met");
            ft_pass("opnpc1_drunk_beer_refuse");

            ft_talk_finish(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_KELDA,
                           "beer hand-in must write learned_of_kelda, got %d",
                           ft_get_bit(player, "forget_quest"));
            SELFTEST_CHECK(obj_seed <= 0 || ft_inv_total(player, obj_seed) >= 1,
                           "Drunken Dwarf must hand over a kelda seed");
            ft_pass("opnpc1_drunk_kelda_seed");

            ft_talk_finish(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_KELDA,
                           "kelda-ask reminder must stay learned");
            ft_pass("opnpc1_drunk_kelda_ask");

            ft_set_bit(srv, "forget_quest", FT_STOUT);
            ft_talk_finish(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_STOUT,
                           "stout-missing must stay stout_ready");
            ft_pass("opnpc1_drunk_stout_missing");

            if( obj_kelda_stout > 0 )
                ft_give(player, obj_kelda_stout, 1);
            ft_talk_finish(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_GAVE,
                           "stout hand-in must write gave_stout, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("opnpc1_drunk_stout_handin");

            ft_talk_finish(srv, npc_drunk, drunk);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_REVEALED,
                           "tunnel reveal must write dwarf_revealed_tunnels, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("opnpc1_drunk_tunnel_reveal");
            ft_free_npc(srv, drunk);
        }
    }

    /* ---- Rowdy fail + success ---- */
    if( npc_rowdy > 0 )
    {
        int rowdy = ft_spawn(srv, npc_rowdy, FT_ROWDY_X, FT_ROWDY_Z, 0);

        if( rowdy >= 0 )
        {
            ft_clear_inv(player);
            ft_set_bit(srv, "forget_quest", FT_NOT_STARTED);
            ft_set_bit(srv, "forget_seed2_given", 0);
            ft_talk_finish(srv, npc_rowdy, rowdy);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed2_given") == 0,
                           "too-early Rowdy must not grant a seed");
            ft_pass("opnpc1_rowdy_too_early");

            ft_set_bit(srv, "forget_quest", FT_KELDA);
            ft_talk_refuse(srv, npc_rowdy, rowdy);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed2_given") == 0,
                           "refusing Rowdy must not grant a seed");
            ft_pass("opnpc1_rowdy_refuse");

            ft_talk_finish(srv, npc_rowdy, rowdy);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed2_given") == 0,
                           "Rowdy with no coin must not grant a seed");
            ft_pass("opnpc1_rowdy_no_coin");

            if( obj_coins > 0 )
                ft_give(player, obj_coins, 5);
            ft_talk_finish(srv, npc_rowdy, rowdy);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed2_given") == 1,
                           "Rowdy coin hand-in must set seed2_given");
            SELFTEST_CHECK(obj_seed <= 0 || ft_inv_total(player, obj_seed) >= 1,
                           "Rowdy must hand over a kelda seed");
            ft_pass("opnpc1_rowdy_success");
            ft_free_npc(srv, rowdy);
        }
    }

    /* ---- Khorvak fail + success (additive FT, BAR schematic preserved) ---- */
    if( npc_khorvak > 0 )
    {
        int khorvak = ft_spawn(srv, npc_khorvak, FT_KHORVAK_X, FT_KHORVAK_Z, 0);

        if( khorvak >= 0 )
        {
            ft_clear_inv(player);
            ft_set_bit(srv, "forget_quest", FT_NOT_STARTED);
            ft_set_bit(srv, "forget_seed3_given", 0);
            ft_talk_finish(srv, npc_khorvak, khorvak);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed3_given") == 0,
                           "too-early Khorvak must stay on Between a Rock");
            ft_pass("opnpc1_khorvak_too_early");

            ft_set_bit(srv, "forget_quest", FT_KELDA);
            ft_talk_finish(srv, npc_khorvak, khorvak);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed3_given") == 0,
                           "Khorvak with no stout must not grant a seed");
            ft_pass("opnpc1_khorvak_no_stout");

            ft_clear_inv(player);
            ft_talk_refuse(srv, npc_khorvak, khorvak);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed3_given") == 0,
                           "Khorvak refuse must not grant a seed");
            ft_pass("opnpc1_khorvak_refuse");

            ft_clear_inv(player);
            if( obj_stout_dwarf > 0 )
                ft_give(player, obj_stout_dwarf, 1);
            ft_talk_finish(srv, npc_khorvak, khorvak);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed3_given") == 1,
                           "Khorvak stout hand-in must set seed3_given");
            ft_pass("opnpc1_khorvak_success");
            ft_free_npc(srv, khorvak);
        }
    }

    /* ---- Gauss fail + success ---- */
    if( npc_gauss > 0 )
    {
        int gauss = ft_spawn(srv, npc_gauss, FT_GAUSS_X, FT_GAUSS_Z, 0);

        if( gauss >= 0 )
        {
            ft_clear_inv(player);
            ft_set_bit(srv, "forget_quest", FT_NOT_STARTED);
            ft_set_bit(srv, "forget_seed4_given", 0);
            ft_talk_finish(srv, npc_gauss, gauss);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed4_given") == 0,
                           "too-early Gauss must not grant a seed");
            ft_pass("opnpc1_gauss_too_early");

            ft_set_bit(srv, "forget_quest", FT_KELDA);
            ft_talk_finish(srv, npc_gauss, gauss);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed4_given") == 0,
                           "Gauss with no beer must not grant a seed");
            ft_pass("opnpc1_gauss_no_beer");

            if( obj_coins > 0 )
                ft_give(player, obj_coins, 10);
            ft_talk_refuse(srv, npc_gauss, gauss);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed4_given") == 0,
                           "refusing Gauss must not grant a seed");
            ft_pass("opnpc1_gauss_refuse");

            ft_talk_finish(srv, npc_gauss, gauss);
            SELFTEST_CHECK(ft_get_bit(player, "forget_seed4_given") == 1,
                           "Gauss beer hand-in must set seed4_given");
            ft_pass("opnpc1_gauss_success");
            ft_free_npc(srv, gauss);
        }
    }

    /* ---- Rind ---- */
    if( npc_rind > 0 )
    {
        int rind = ft_spawn(srv, npc_rind, FT_RIND_X, FT_RIND_Z, 0);

        if( rind >= 0 )
        {
            ft_clear_inv(player);
            ft_set_bit(srv, "forget_quest", FT_NOT_STARTED);
            ft_talk_finish(srv, npc_rind, rind);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_NOT_STARTED,
                           "too-early Rind must not plant");
            ft_pass("opnpc1_rind_too_early");

            ft_set_bit(srv, "forget_quest", FT_KELDA);
            ft_talk_finish(srv, npc_rind, rind);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_KELDA,
                           "Rind without four seeds must stay kelda");
            ft_pass("opnpc1_rind_need_seeds");

            if( obj_seed > 0 )
                ft_give(player, obj_seed, 4);
            ft_talk_finish(srv, npc_rind, rind);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_SEEDS,
                           "Rind with four seeds must write seeds_ready, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("opnpc1_rind_all_seeds");
            ft_free_npc(srv, rind);
        }
    }

    /* ---- Patch rake / plant / grow / harvest ---- */
    if( loc_patch >= 0 )
    {
        loc_slot = ft_place_loc(srv, loc_patch, FT_PATCH_X, FT_PATCH_Z, 0);
        SELFTEST_CHECK(loc_slot >= 0, "kelda hops patch should place");
        if( loc_slot >= 0 )
        {
            ft_clear_inv(player);
            ft_set_bit(srv, "forget_quest", FT_KELDA);
            ft_set_bit(srv, "forget_farming", FT_FARM_WEEDS3);
            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_WEEDS3,
                           "too-early patch must stay weedy");
            ft_pass("oploc1_patch_too_early");

            ft_set_bit(srv, "forget_quest", FT_SEEDS);
            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_WEEDS3,
                           "rake-missing must not clear weeds");
            ft_pass("oploc1_patch_no_rake");

            if( obj_rake > 0 )
                ft_give(player, obj_rake, 1);
            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == 1,
                           "first rake must advance farming to 1, got %d",
                           ft_get_bit(player, "forget_farming"));
            ft_oploc_finish(srv, loc_patch, loc_slot);
            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_WEEDED,
                           "three rakes must weed the patch, got %d",
                           ft_get_bit(player, "forget_farming"));
            ft_pass("oploc1_patch_rake");

            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_WEEDED,
                           "weeded patch without seeds/dibber must stay weeded");
            ft_pass("oploc1_patch_need_seeds_or_dibber");

            if( obj_seed > 0 )
                ft_give(player, obj_seed, 4);
            if( obj_dibber > 0 )
                ft_give(player, obj_dibber, 1);
            if( stat_farming >= 0 )
                ToriRSServer_CombatSetLevel(player, stat_farming, FT_REQ_FARMING - 1);
            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_WEEDED,
                           "Farming 16 must not plant kelda seeds");
            ft_pass("oploc1_patch_farm_gate_plant");

            if( stat_farming >= 0 )
                ToriRSServer_CombatSetLevel(player, stat_farming, FT_REQ_FARMING);
            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_PLANTED,
                           "planting must write farming planted, got %d",
                           ft_get_bit(player, "forget_farming"));
            ft_pass("oploc1_patch_plant");

            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_PLANTED,
                           "growing click must stay planted");
            ft_pass("oploc1_patch_growing");

            ToriRSServer_ScriptsRunProc(srv, "[timer,forget_kelda_grow]", NULL, 0);
            ft_finish(srv);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_GROWN,
                           "grow timer must write fullygrown, got %d",
                           ft_get_bit(player, "forget_farming"));
            ft_pass("timer_kelda_grow");

            if( stat_farming >= 0 )
                ToriRSServer_CombatSetLevel(player, stat_farming, FT_REQ_FARMING - 1);
            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_GROWN,
                           "Farming 16 must not harvest");
            ft_pass("oploc1_patch_farm_gate_harvest");

            if( stat_farming >= 0 )
                ToriRSServer_CombatSetLevel(player, stat_farming, FT_REQ_FARMING);
            ft_oploc_finish(srv, loc_patch, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_farming") == FT_FARM_HARVESTED,
                           "harvest must write farming harvested, got %d",
                           ft_get_bit(player, "forget_farming"));
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_HARVESTED,
                           "harvest must write kelda_harvested, got %d",
                           ft_get_bit(player, "forget_quest"));
            SELFTEST_CHECK(obj_hops <= 0 || ft_inv_total(player, obj_hops) >= 1,
                           "harvest must grant kelda hops");
            ft_pass("oploc1_patch_harvest");
        }
    }

    /* ---- Blandebir yeast ---- */
    if( npc_blandebir > 0 )
    {
        int yeast = ft_spawn(srv, npc_blandebir, FT_VAT_X, FT_VAT_Z, 1);

        if( yeast >= 0 )
        {
            ft_clear_inv(player);
            ft_set_bit(srv, "forget_quest", FT_SEEDS);
            ft_talk_finish(srv, npc_blandebir, yeast);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_SEEDS,
                           "too-early Blandebir must not sell yeast");
            ft_pass("opnpc1_blandebir_too_early");

            ft_set_bit(srv, "forget_quest", FT_HARVESTED);
            ft_talk_finish(srv, npc_blandebir, yeast);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_HARVESTED,
                           "Blandebir without 25 coins must stay harvested");
            ft_pass("opnpc1_blandebir_no_coins");

            if( obj_coins > 0 )
                ft_give(player, obj_coins, 25);
            ft_talk_refuse(srv, npc_blandebir, yeast);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_HARVESTED,
                           "refusing yeast must stay harvested");
            ft_pass("opnpc1_blandebir_refuse");

            ft_talk_finish(srv, npc_blandebir, yeast);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_YEAST,
                           "yeast buy must write bought_yeast, got %d",
                           ft_get_bit(player, "forget_quest"));
            SELFTEST_CHECK(obj_yeast <= 0 || ft_inv_total(player, obj_yeast) >= 1,
                           "Blandebir must hand over ale yeast");
            ft_pass("opnpc1_blandebir_yeast");
            ft_free_npc(srv, yeast);
        }
    }

    /* ---- Brew water / malt / hops / yeast / valve / collect ---- */
    if( loc_vat >= 0 && loc_valve >= 0 && loc_barrel >= 0 )
    {
        int vat = ft_place_loc(srv, loc_vat, FT_VAT_X, FT_VAT_Z, 1);
        int valve = ft_place_loc(srv, loc_valve, FT_VALVE_X, FT_VALVE_Z, 1);
        int barrel = ft_place_loc(srv, loc_barrel, FT_BARREL_X, FT_BARREL_Z, 1);

        SELFTEST_CHECK(vat >= 0, "vat should place");
        SELFTEST_CHECK(valve >= 0, "valve should place");
        SELFTEST_CHECK(barrel >= 0, "barrel should place");
        if( vat >= 0 && valve >= 0 && barrel >= 0 )
        {
            ft_set_bit(srv, "forget_quest", FT_YEAST);
            if( obj_water > 0 )
                ft_give(player, obj_water, 1);
            ft_use_loc(srv, loc_vat, vat, obj_water);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_YEAST,
                           "one bucket must not fill the vat");
            ft_pass("oplocu_vat_water_need2");

            if( obj_water > 0 )
                ft_give(player, obj_water, 2);
            ft_use_loc(srv, loc_vat, vat, obj_water);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_WATER,
                           "two buckets must write added_water, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("oplocu_vat_add_water");

            if( obj_malt > 0 )
                ft_give(player, obj_malt, 1);
            ft_use_loc(srv, loc_vat, vat, obj_malt);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_WATER,
                           "one malt must not advance");
            ft_pass("oplocu_vat_malt_need2");

            if( obj_malt > 0 )
                ft_give(player, obj_malt, 2);
            ft_use_loc(srv, loc_vat, vat, obj_malt);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_MALT,
                           "two malts must write added_malt, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("oplocu_vat_add_malt");

            if( obj_hops > 0 )
                ft_give(player, obj_hops, 1);
            ft_use_loc(srv, loc_vat, vat, obj_hops);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_HOPS,
                           "kelda hops must write added_hops, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("oplocu_vat_add_hops");

            if( obj_yeast > 0 )
                ft_give(player, obj_yeast, 1);
            if( stat_cooking >= 0 )
                ToriRSServer_CombatSetLevel(player, stat_cooking, FT_REQ_COOKING - 1);
            ft_use_loc(srv, loc_vat, vat, obj_yeast);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_HOPS,
                           "Cooking 19 must not add yeast");
            ft_pass("oplocu_vat_cook_gate");

            if( stat_cooking >= 0 )
                ToriRSServer_CombatSetLevel(player, stat_cooking, FT_REQ_COOKING);
            if( obj_yeast > 0 )
                ft_give(player, obj_yeast, 1);
            ft_use_loc(srv, loc_vat, vat, obj_yeast);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_YEAST_IN,
                           "yeast must write added_yeast, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("oplocu_vat_add_yeast");

            ft_use_loc(srv, loc_vat, vat, obj_yeast);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_YEAST_IN,
                           "fermenting vat must stay added_yeast");
            ft_pass("oplocu_vat_fermenting");

            ft_oploc_finish(srv, loc_valve, valve);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_YEAST_IN,
                           "valve before wait must stay fermenting");
            ft_pass("oploc1_valve_nothing");

            ToriRSServer_ScriptsRunProc(srv, "[timer,forget_brew_wait]", NULL, 0);
            ft_finish(srv);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_WAITED,
                           "brew timer must write brew_waited, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("timer_brew_wait");

            ft_oploc_finish(srv, loc_valve, valve);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_VALVE,
                           "turning the valve must write turned_valve, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("oploc1_valve_turn");

            ft_oploc_finish(srv, loc_valve, valve);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_VALVE,
                           "already-turned valve must stay turned");
            ft_pass("oploc1_valve_already");

            if( obj_glass > 0 )
                ft_give(player, obj_glass, 1);
            ft_use_loc(srv, loc_barrel, barrel, obj_glass);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_STOUT,
                           "collecting must write kelda_stout_ready, got %d",
                           ft_get_bit(player, "forget_quest"));
            SELFTEST_CHECK(obj_kelda_stout <= 0 ||
                               ft_inv_total(player, obj_kelda_stout) >= 1,
                           "barrel must grant kelda stout");
            ft_pass("oplocu_barrel_collect");
        }
    }

    /* ---- Conductor / director / secret cart ---- */
    if( npc_conductor8 > 0 )
    {
        int cond = ft_spawn(srv, npc_conductor8, FT_CONDUCTOR8_X, FT_CONDUCTOR8_Z, 0);

        if( cond >= 0 )
        {
            ft_set_bit(srv, "forget_quest", FT_GAVE);
            ft_talk_finish(srv, npc_conductor8, cond);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_GAVE,
                           "too-early conductor must stay gave_stout");
            ft_pass("opnpc1_conductor_too_early");

            ft_set_bit(srv, "forget_quest", FT_REVEALED);
            ft_talk_finish(srv, npc_conductor8, cond);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_CONDUCTOR,
                           "conductor must write talked_conductor, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("opnpc1_conductor_ask");
            ft_free_npc(srv, cond);
        }
    }

    if( npc_director > 0 )
    {
        int dir = ft_spawn(srv, npc_director, FT_VELDABAN_X, FT_VELDABAN_Z, 0);

        if( dir >= 0 )
        {
            ft_set_bit(srv, "forget_quest", FT_CONDUCTOR);
            ft_talk_finish(srv, npc_director, dir);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_DIRECTOR,
                           "director boarded-door must write talked_director, got %d",
                           ft_get_bit(player, "forget_quest"));
            SELFTEST_CHECK(ft_get_bit(player, "forget_boarding_removed") == 1,
                           "director must set boarding_removed");
            ft_pass("opnpc1_director_boarded");
            ft_free_npc(srv, dir);
        }
    }

    if( loc_cart >= 0 )
    {
        loc_slot = ft_place_loc(srv, loc_cart, FT_SECRET_CART_X, FT_SECRET_CART_Z, 0);
        if( loc_slot >= 0 )
        {
            ft_set_bit(srv, "forget_quest", FT_CONDUCTOR);
            ft_oploc_finish(srv, loc_cart, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_CONDUCTOR,
                           "boarded secret cart must stay conductor");
            ft_pass("oploc1_secret_cart_boarded");

            ft_set_bit(srv, "forget_quest", FT_DIRECTOR);
            ft_oploc_finish(srv, loc_cart, loc_slot);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_ENTERED,
                           "secret cart must write entered_tunnels, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("oploc1_secret_cart_enter");
        }
    }

    /* ---- Tunnel box + three collapsed rooms + cutscene leftover ---- */
    if( loc_box >= 0 && loc_ctrl >= 0 )
    {
        int box = ft_place_loc(srv, loc_box, FT_BOX_X, FT_BOX_Z, 1);
        int ctrl = ft_place_loc(srv, loc_ctrl, FT_CTRL_X, FT_CTRL_Z, 1);

        if( box >= 0 && ctrl >= 0 )
        {
            ft_set_bit(srv, "forget_quest", FT_DIRECTOR);
            ft_oploc_finish(srv, loc_box, box);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_DIRECTOR,
                           "too-early box must stay director");
            ft_pass("oploc1_box_too_early");

            ft_set_bit(srv, "forget_quest", FT_ENTERED);
            ft_oploc_finish(srv, loc_ctrl, ctrl);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_ENTERED,
                           "machinery before the box must stay entered");
            ft_pass("oploc1_mach_need_box");

            ft_oploc_finish(srv, loc_box, box);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_ROOM1,
                           "box search must write room1_done, got %d",
                           ft_get_bit(player, "forget_quest"));
            ft_pass("oploc1_box_search");

            ft_oploc_finish(srv, loc_ctrl, ctrl);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_ROOM2,
                           "room1 machinery must write room2_done, got %d",
                           ft_get_bit(player, "forget_quest"));
            SELFTEST_CHECK(ft_get_bit(player, "forget_tunnel_cutscene1") == 1,
                           "room1 leftover must set tunnel_cutscene1");
            ft_pass("oploc1_mach_room1");

            ft_oploc_finish(srv, loc_ctrl, ctrl);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_ROOM3,
                           "room2 machinery must write room3_done, got %d",
                           ft_get_bit(player, "forget_quest"));
            SELFTEST_CHECK(ft_get_bit(player, "forget_tunnel_cutscene2") == 1,
                           "room2 leftover must set tunnel_cutscene2");
            ft_pass("oploc1_mach_room2");

            ft_oploc_finish(srv, loc_ctrl, ctrl);
            SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_CUTSCENE,
                           "room3 machinery must write cutscene_done, got %d",
                           ft_get_bit(player, "forget_quest"));
            SELFTEST_CHECK(ft_get_bit(player, "forget_tunnel_cutscene3") == 1,
                           "war-council leftover must set tunnel_cutscene3");
            ft_pass("oploc1_mach_room3_cutscene");
        }
    }

    /* ---- Report + kebab memory wipe + rewards ---- */
    slot = ft_spawn(srv, npc_veldaban, FT_VELDABAN_X, FT_VELDABAN_Z, 0);
    if( slot >= 0 )
    {
        ft_set_bit(srv, "forget_quest", FT_CUTSCENE);
        ft_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_KEBAB,
                       "report must write ready_for_kebab, got %d",
                       ft_get_bit(player, "forget_quest"));
        ft_pass("opnpc1_veldaban_report");

        ft_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_KEBAB,
                       "kebab reminder must stay ready_for_kebab");
        ft_pass("opnpc1_veldaban_kebab_reminder");
        ft_free_npc(srv, slot);
    }

    cooking_xp_before = (stat_cooking >= 0) ? player->stat_xp_tenths[stat_cooking] : 0;
    farming_xp_before = (stat_farming >= 0) ? player->stat_xp_tenths[stat_farming] : 0;
    ft_clear_inv(player);
    ft_set_bit(srv, "forget_quest", FT_KEBAB);
    if( obj_kebab > 0 )
        ft_give(player, obj_kebab, 1);
    if( obj_beer > 0 )
        ft_give(player, obj_beer, 1);
    {
        int kebab_slot = -1;
        int s;

        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        {
            if( obj_kebab > 0 && player->inv[s].obj_id == obj_kebab )
            {
                kebab_slot = s;
                break;
            }
        }
        SELFTEST_CHECK(kebab_slot >= 0, "kebab must be in inv for the wipe");
        if( kebab_slot >= 0 )
        {
            player->last_slot = kebab_slot;
            selftest_opheld(srv, 1, kebab_slot);
            ft_finish(srv);
        }
    }
    SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_COMPLETE,
                   "kebab+beer must write complete 140, got %d",
                   ft_get_bit(player, "forget_quest"));
    SELFTEST_CHECK(stat_cooking < 0 ||
                       player->stat_xp_tenths[stat_cooking] >=
                           cooking_xp_before + FT_REWARD_COOKING,
                   "complete should award 5000 Cooking XP");
    SELFTEST_CHECK(stat_farming < 0 ||
                       player->stat_xp_tenths[stat_farming] >=
                           farming_xp_before + FT_REWARD_FARMING,
                   "complete should award 5000 Farming XP");
    SELFTEST_CHECK(obj_reward <= 0 || ft_inv_total(player, obj_reward) >= 2,
                   "complete should grant 2 mature dwarven stout, got %d",
                   obj_reward > 0 ? ft_inv_total(player, obj_reward) : 0);
    ft_pass("opheld1_kebab_memory_wipe_complete");

    ToriRSServer_ScriptsRunProc(srv, "[proc,forget_journal]", NULL, 0);
    ft_finish(srv);
    ft_pass("journal_complete");

    slot = ft_spawn(srv, npc_veldaban, FT_VELDABAN_X, FT_VELDABAN_Z, 0);
    if( slot >= 0 )
    {
        ft_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(ft_get_bit(player, "forget_quest") == FT_COMPLETE,
                       "post-complete Veldaban must stay at 140");
        ft_pass("opnpc1_veldaban_post_complete");
        ft_free_npc(srv, slot);
    }

    ft_clear_inv(player);
    ft_reset_state(srv);
    ft_god(player);
    ToriRSServer_WorldCloseModal(srv);

    fprintf(stderr, "ToriRSServer forgettabletale selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_FORGETTABLETALE_SELFTEST_U_H */
