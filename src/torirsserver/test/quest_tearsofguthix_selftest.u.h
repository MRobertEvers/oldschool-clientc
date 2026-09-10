#ifndef TORIRSSERVER_TEST_QUEST_TEARSOFGUTHIX_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_TEARSOFGUTHIX_SELFTEST_U_H

/* Tears of Guthix Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Juna
 * dummy / light-creature cannot leak. Real OPLOC / OPHELDU / OPNPCU /
 * OPNPC1 on the critical path. player->godmode = 1 for the whole walk
 * (no death test). Completion goes through the authored
 * ~quest_complete_rewards(quest_tearsofguthix, ...). Additive ToG only —
 * do not rewrite Death to the Dorgeshuun, Observatory, Construction, or
 * MTA. DTtD Zanik branches stay at %dttd_main = 0. */

#define TOG_NOT_STARTED 0
#define TOG_NEED_BOWL 1
#define TOG_COMPLETE 2
#define TOG_QP_REQ 43
#define TOG_FM_REQ 49
#define TOG_CRAFT_REQ 20
#define TOG_MINE_REQ 20
#define TOG_REWARD_XP_TENTHS 10000

#define TOG_JUNA_X (50 * 64 + 50)
#define TOG_JUNA_Z (148 * 64 + 45)
#define TOG_LEVEL 2
#define TOG_NORTH_X (50 * 64 + 28)
#define TOG_NORTH_Z (148 * 64 + 55)
#define TOG_SOUTH_X (50 * 64 + 29)
#define TOG_SOUTH_Z (148 * 64 + 32)
#define TOG_MINE_X (50 * 64 + 29)
#define TOG_MINE_Z (148 * 64 + 25)

static void
tog_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "TOG PASS: %s\n", step);
}

static void
tog_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
tog_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
tog_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 48 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
tog_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
tog_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = tog_chatmenu();
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
tog_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = tog_chatmenu();
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
tog_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    tog_god(player);
    selftest_tick(srv);
}

static int
tog_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    tog_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
tog_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
tog_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
tog_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( tog_inv_total(player, obj_id) >= count )
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
tog_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
tog_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
tog_set_qp(struct ToriRSServer* srv, int qp)
{
    int varp;

    assert(srv);
    varp = ToriRSServer_WorldVarp("qp");
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, qp);
}

static int
tog_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    tog_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
tog_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    assert(loc_id >= 0);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
}

static void
tog_oploc_finish(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    tog_oploc(srv, loc_id, loc_slot);
    tog_finish(srv);
}

static void
tog_use_held(struct ToriRSServer* srv, int held_id, int useitem_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = useitem_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, held_id, -1, -1);
    tog_finish(srv);
    player->last_useitem = -1;
}

static void
tog_use_on_npc(struct ToriRSServer* srv, int npc_type, int slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    tog_finish(srv);
    player->last_useitem = -1;
}

static void
tog_set_skills(
    struct ToriRSServerPlayer* player,
    int stat_fm,
    int stat_craft,
    int stat_mine,
    int fm,
    int craft,
    int mine)
{
    assert(player);
    if( stat_fm >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_fm, fm);
    if( stat_craft >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_craft, craft);
    if( stat_mine >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_mine, mine);
}

static void
tog_reset_state(struct ToriRSServer* srv)
{
    assert(srv);
    tog_set_bit(srv, "tog_juna_bowl", TOG_NOT_STARTED);
    tog_set_bit(srv, "dttd_main", 0);
    tog_set_bit(srv, "lost_tribe_hole_2_dug", 0);
    tog_set_bit(srv, "tog_last_day", 0);
    tog_set_qp(srv, 0);
}

static void
selftest_quest_tearsofguthix(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_fm;
    int stat_craft;
    int stat_mine;
    int npc_dummy;
    int npc_creature;
    int loc_juna;
    int loc_rocks;
    int loc_climb;
    int loc_wall;
    int obj_stone;
    int obj_bowl;
    int obj_chisel;
    int obj_sapphire;
    int obj_lantern;
    int obj_lantern_empty;
    int obj_lens;
    int obj_sap_unlit;
    int obj_sap_empty;
    int obj_sap_lit;
    int obj_tinder;
    int obj_coins;
    int dummy;
    int creature;
    int loc_slot;
    int craft_xp_before;
    int s;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: tearsofguthix critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer tearsofguthix selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    tog_god(player);

    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    npc_dummy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tog_juna_dummy");
    npc_creature = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tog_light_creature_op");
    loc_juna = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tog_juna");
    loc_rocks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tog_blue_stone_rocks1");
    loc_climb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tog_climbing_rocks_down");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tog_weepingwall");
    obj_stone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tog_stone");
    obj_bowl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tog_bowl");
    obj_chisel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chisel");
    obj_sapphire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sapphire");
    obj_lantern = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bullseye_lantern_unlit");
    obj_lantern_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bullseye_lantern_empty");
    obj_lens = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bullseye_lantern_lens");
    obj_sap_unlit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tog_sapphire_lantern_unlit");
    obj_sap_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tog_sapphire_lantern_empty");
    obj_sap_lit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tog_sapphire_lantern_lit");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");

    SELFTEST_CHECK(loc_juna >= 0, "loc tog_juna should resolve");
    SELFTEST_CHECK(npc_dummy > 0, "npc tog_juna_dummy should resolve");
    SELFTEST_CHECK(obj_stone > 0, "obj tog_stone should resolve");
    SELFTEST_CHECK(obj_bowl > 0, "obj tog_bowl should resolve");
    SELFTEST_CHECK(obj_chisel > 0, "obj chisel should resolve");
    SELFTEST_CHECK(stat_fm >= 0, "stat firemaking should resolve");
    SELFTEST_CHECK(stat_craft >= 0, "stat crafting should resolve");
    SELFTEST_CHECK(stat_mine >= 0, "stat mining should resolve");
    if( loc_juna < 0 || npc_dummy <= 0 )
    {
        fprintf(stderr, "ToriRSServer tearsofguthix selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    tog_clear_inv(player);
    tog_reset_state(srv);
    tog_god(player);
    dummy = tog_spawn(srv, npc_dummy, TOG_JUNA_X, TOG_JUNA_Z, TOG_LEVEL);
    loc_slot = tog_place_loc(srv, loc_juna, TOG_JUNA_X, TOG_JUNA_Z, TOG_LEVEL);
    SELFTEST_CHECK(dummy >= 0, "tog_juna_dummy should spawn");
    SELFTEST_CHECK(loc_slot >= 0, "tog_juna loc should place");
    SELFTEST_CHECK(tog_get_bit(player, "dttd_main") == 0,
                   "DTtD must stay unstarted for additive ToG");

    /* ---- Juna prereq refuses (QP / FM / Craft / Mine) ---- */
    if( loc_slot >= 0 )
    {
        tog_set_qp(srv, 0);
        tog_set_skills(player, stat_fm, stat_craft, stat_mine,
                       TOG_FM_REQ, TOG_CRAFT_REQ, TOG_MINE_REQ);
        tog_oploc_finish(srv, loc_juna, loc_slot);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NOT_STARTED,
                       "low QP must not start Tears of Guthix, got %d",
                       tog_get_bit(player, "tog_juna_bowl"));
        tog_pass("oploc1_juna_prereq_qp");

        tog_set_qp(srv, TOG_QP_REQ);
        tog_set_skills(player, stat_fm, stat_craft, stat_mine,
                       1, TOG_CRAFT_REQ, TOG_MINE_REQ);
        tog_oploc_finish(srv, loc_juna, loc_slot);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NOT_STARTED,
                       "low Firemaking must not start Tears of Guthix");
        tog_pass("oploc1_juna_prereq_firemaking");

        tog_set_skills(player, stat_fm, stat_craft, stat_mine,
                       TOG_FM_REQ, 1, TOG_MINE_REQ);
        tog_oploc_finish(srv, loc_juna, loc_slot);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NOT_STARTED,
                       "low Crafting must not start Tears of Guthix");
        tog_pass("oploc1_juna_prereq_crafting");

        tog_set_skills(player, stat_fm, stat_craft, stat_mine,
                       TOG_FM_REQ, TOG_CRAFT_REQ, 1);
        tog_oploc_finish(srv, loc_juna, loc_slot);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NOT_STARTED,
                       "low Mining must not start Tears of Guthix");
        tog_pass("oploc1_juna_prereq_mining");
    }

    /* ---- Story start: refuse / what-are-tears / accept ---- */
    tog_set_qp(srv, TOG_QP_REQ);
    tog_set_skills(player, stat_fm, stat_craft, stat_mine,
                   TOG_FM_REQ, TOG_CRAFT_REQ, TOG_MINE_REQ);
    if( loc_slot >= 0 )
    {
        tog_oploc(srv, loc_juna, loc_slot);
        tog_click_until_menu(srv, 12);
        tog_pick_row(srv, 3);
        tog_finish(srv);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NOT_STARTED,
                       "Not now must not start the quest");
        tog_pass("oploc1_juna_refuse_not_now");

        tog_oploc(srv, loc_juna, loc_slot);
        tog_click_until_menu(srv, 12);
        tog_pick_row(srv, 2);
        tog_click_until_menu(srv, 8);
        tog_pick_row(srv, 2);
        tog_finish(srv);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NOT_STARTED,
                       "tears lore then Not now must not start");
        tog_pass("oploc1_juna_what_are_tears_refuse");

        tog_oploc(srv, loc_juna, loc_slot);
        tog_click_until_menu(srv, 12);
        tog_pick_row(srv, 2);
        tog_click_until_menu(srv, 8);
        tog_pick_row(srv, 1);
        tog_finish(srv);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NEED_BOWL,
                       "tears lore then Okay should start, got %d",
                       tog_get_bit(player, "tog_juna_bowl"));
        tog_pass("oploc1_juna_what_are_tears_accept");
    }

    tog_set_bit(srv, "tog_juna_bowl", TOG_NOT_STARTED);
    if( loc_slot >= 0 )
    {
        tog_oploc(srv, loc_juna, loc_slot);
        tog_click_until_menu(srv, 12);
        tog_pick_row(srv, 1);
        tog_finish(srv);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NEED_BOWL,
                       "Okay should start the quest, got %d",
                       tog_get_bit(player, "tog_juna_bowl"));
        tog_pass("oploc1_juna_accept_story");
    }

    /* ---- Mid-quest Juna: cave / tears / not-now without a bowl ---- */
    if( loc_slot >= 0 )
    {
        tog_oploc(srv, loc_juna, loc_slot);
        tog_click_until_menu(srv, 8);
        tog_pick_row(srv, 3);
        tog_finish(srv);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NEED_BOWL,
                       "mid-quest Not now must stay at need-bowl");
        tog_pass("oploc1_juna_mid_not_now");

        tog_oploc(srv, loc_juna, loc_slot);
        tog_click_until_menu(srv, 8);
        tog_pick_row(srv, 2);
        tog_finish(srv);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NEED_BOWL,
                       "mid-quest tears lore must stay at need-bowl");
        tog_pass("oploc1_juna_mid_what_are_tears");

        tog_oploc(srv, loc_juna, loc_slot);
        tog_click_until_menu(srv, 8);
        tog_pick_row(srv, 1);
        tog_finish(srv);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_NEED_BOWL,
                       "light-creature story must stay at need-bowl");
        tog_pass("oploc1_juna_reach_cave");
    }

    /* ---- Mine stone / climb / weeping wall before complete ---- */
    if( loc_rocks >= 0 )
    {
        int rock = tog_place_loc(srv, loc_rocks, TOG_MINE_X, TOG_MINE_Z, TOG_LEVEL);

        if( rock >= 0 )
        {
            tog_clear_inv(player);
            tog_set_bit(srv, "tog_juna_bowl", TOG_NOT_STARTED);
            tog_oploc_finish(srv, loc_rocks, rock);
            SELFTEST_CHECK(tog_inv_total(player, obj_stone) == 0,
                           "mine before the quest must refuse");
            tog_pass("oploc1_mine_no_reason");

            tog_set_bit(srv, "tog_juna_bowl", TOG_NEED_BOWL);
            tog_set_skills(player, stat_fm, stat_craft, stat_mine,
                           TOG_FM_REQ, TOG_CRAFT_REQ, 1);
            tog_oploc_finish(srv, loc_rocks, rock);
            SELFTEST_CHECK(tog_inv_total(player, obj_stone) == 0,
                           "mine below 20 must refuse");
            tog_pass("oploc1_mine_level");

            tog_set_skills(player, stat_fm, stat_craft, stat_mine,
                           TOG_FM_REQ, TOG_CRAFT_REQ, TOG_MINE_REQ);
            if( obj_coins > 0 )
            {
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                    inv_set(player, s, obj_coins, 1);
                tog_oploc_finish(srv, loc_rocks, rock);
                SELFTEST_CHECK(tog_inv_total(player, obj_stone) == 0,
                               "full inventory must not grant stone");
                tog_pass("oploc1_mine_inv_full");
                tog_clear_inv(player);
            }

            tog_oploc_finish(srv, loc_rocks, rock);
            SELFTEST_CHECK(tog_inv_total(player, obj_stone) == 1,
                           "mine should grant tog_stone");
            tog_pass("oploc1_mine_success");

            tog_oploc_finish(srv, loc_rocks, rock);
            SELFTEST_CHECK(tog_inv_total(player, obj_stone) == 1,
                           "second mine must not duplicate stone");
            tog_pass("oploc1_mine_already");
        }
    }

    if( loc_climb >= 0 )
    {
        int climb = tog_place_loc(srv, loc_climb, TOG_SOUTH_X, TOG_SOUTH_Z, TOG_LEVEL);

        if( climb >= 0 )
        {
            tog_oploc_finish(srv, loc_climb, climb);
            tog_pass("oploc1_climb_rocks");
        }
    }

    if( loc_wall >= 0 )
    {
        int wall = tog_place_loc(srv, loc_wall, TOG_SOUTH_X, TOG_SOUTH_Z + 2, TOG_LEVEL);

        if( wall >= 0 )
        {
            tog_set_bit(srv, "tog_juna_bowl", TOG_NEED_BOWL);
            tog_oploc_finish(srv, loc_wall, wall);
            tog_pass("oploc1_wall_no_access");
        }
    }

    /* ---- Light creature / lantern ---- */
    if( npc_creature > 0 )
    {
        creature = tog_spawn(srv, npc_creature, TOG_NORTH_X, TOG_NORTH_Z, TOG_LEVEL);
        SELFTEST_CHECK(creature >= 0, "tog_light_creature_op should spawn");
        if( creature >= 0 )
        {
            tog_clear_inv(player);
            tog_set_bit(srv, "tog_juna_bowl", TOG_NOT_STARTED);
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_creature, -1, creature);
            tog_finish(srv);
            SELFTEST_CHECK(player->z == TOG_NORTH_Z,
                           "attract before the quest must not teleport");
            tog_pass("opnpc1_creature_no_reason");

            tog_set_bit(srv, "tog_juna_bowl", TOG_NEED_BOWL);
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_creature, -1, creature);
            tog_finish(srv);
            SELFTEST_CHECK(player->z == TOG_NORTH_Z,
                           "attract without a lit lantern must stay put");
            tog_pass("opnpc1_creature_need_lantern");

            if( obj_sap_lit > 0 )
            {
                tog_give(player, obj_sap_lit, 1);
                tog_use_on_npc(srv, npc_creature, creature, obj_sap_lit);
                SELFTEST_CHECK(player->z == TOG_SOUTH_Z,
                               "lit lantern should float south, z=%d", player->z);
                tog_pass("opnpcu_creature_attract");
            }
            tog_free_npc(srv, creature);
        }
    }

    if( obj_sapphire > 0 && obj_lantern > 0 && obj_sap_unlit > 0 )
    {
        tog_clear_inv(player);
        tog_give(player, obj_sapphire, 1);
        tog_give(player, obj_lantern, 1);
        tog_use_held(srv, obj_sapphire, obj_lantern);
        SELFTEST_CHECK(tog_inv_total(player, obj_sap_unlit) == 1,
                       "sapphire on bullseye should make unlit sapphire lantern");
        tog_pass("opheldu_lantern_swap_sapphire");

        if( obj_lens > 0 )
        {
            if( tog_inv_total(player, obj_lens) < 1 )
                tog_give(player, obj_lens, 1);
            tog_use_held(srv, obj_sap_unlit, obj_lens);
            SELFTEST_CHECK(tog_inv_total(player, obj_lantern) == 1,
                           "lens should swap the sapphire back");
            tog_pass("opheldu_lantern_swap_back");
        }

        tog_clear_inv(player);
        tog_give(player, obj_sapphire, 1);
        tog_give(player, obj_lantern, 1);
        tog_use_held(srv, obj_sapphire, obj_lantern);
        if( obj_tinder > 0 && obj_sap_lit > 0 )
        {
            tog_give(player, obj_tinder, 1);
            tog_use_held(srv, obj_sap_unlit, obj_tinder);
            SELFTEST_CHECK(tog_inv_total(player, obj_sap_lit) == 1,
                           "tinderbox should light the sapphire lantern");
            tog_pass("opheldu_lantern_light");

            if( obj_lens > 0 )
            {
                tog_give(player, obj_lens, 1);
                tog_use_held(srv, obj_sap_lit, obj_lens);
                SELFTEST_CHECK(tog_inv_total(player, obj_sap_lit) == 1,
                               "hot lantern must refuse the lens swap");
                tog_pass("opheldu_lantern_too_hot");
            }
        }
    }

    if( obj_sapphire > 0 && obj_lantern_empty > 0 && obj_sap_empty > 0 )
    {
        tog_clear_inv(player);
        tog_give(player, obj_sapphire, 1);
        tog_give(player, obj_lantern_empty, 1);
        tog_use_held(srv, obj_sapphire, obj_lantern_empty);
        SELFTEST_CHECK(tog_inv_total(player, obj_sap_empty) == 1,
                       "sapphire on empty bullseye should make empty sapphire lantern");
        tog_pass("opheldu_lantern_empty_swap");
    }

    /* ---- Chisel + stone -> bowl ---- */
    if( obj_stone > 0 && obj_chisel > 0 && obj_bowl > 0 )
    {
        tog_clear_inv(player);
        tog_give(player, obj_stone, 1);
        tog_give(player, obj_chisel, 1);
        tog_set_bit(srv, "tog_juna_bowl", TOG_NOT_STARTED);
        tog_use_held(srv, obj_stone, obj_chisel);
        SELFTEST_CHECK(tog_inv_total(player, obj_bowl) == 0,
                       "chisel before the quest must refuse");
        tog_pass("opheldu_chisel_no_reason");

        if( obj_coins > 0 )
        {
            tog_use_held(srv, obj_stone, obj_coins);
            SELFTEST_CHECK(tog_inv_total(player, obj_bowl) == 0,
                           "wrong item on stone must not craft a bowl");
            tog_pass("opheldu_chisel_wrong_item");
        }

        tog_set_bit(srv, "tog_juna_bowl", TOG_NEED_BOWL);
        tog_use_held(srv, obj_stone, obj_chisel);
        SELFTEST_CHECK(tog_inv_total(player, obj_bowl) == 1,
                       "chisel + stone should make tog_bowl");
        SELFTEST_CHECK(tog_inv_total(player, obj_stone) == 0,
                       "crafting the bowl should consume the stone");
        tog_pass("opheldu_chisel_bowl");
    }

    /* ---- Bowl hand-in / authored complete ---- */
    tog_tele(srv, TOG_JUNA_X, TOG_JUNA_Z, TOG_LEVEL);
    loc_slot = tog_place_loc(srv, loc_juna, TOG_JUNA_X, TOG_JUNA_Z, TOG_LEVEL);
    if( dummy < 0 )
        dummy = tog_spawn(srv, npc_dummy, TOG_JUNA_X, TOG_JUNA_Z, TOG_LEVEL);
    if( loc_slot >= 0 && obj_bowl > 0 )
    {
        tog_clear_inv(player);
        tog_give(player, obj_bowl, 1);
        tog_set_bit(srv, "tog_juna_bowl", TOG_NEED_BOWL);
        tog_set_qp(srv, TOG_QP_REQ);
        tog_set_skills(player, stat_fm, stat_craft, stat_mine,
                       TOG_FM_REQ, TOG_CRAFT_REQ, TOG_MINE_REQ);
        craft_xp_before = stat_craft >= 0 ? player->stat_xp_tenths[stat_craft] : 0;
        tog_oploc_finish(srv, loc_juna, loc_slot);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_COMPLETE,
                       "bowl hand-in should complete Tears of Guthix, got %d",
                       tog_get_bit(player, "tog_juna_bowl"));
        SELFTEST_CHECK(tog_inv_total(player, obj_bowl) == 0,
                       "Juna should keep the bowl");
        SELFTEST_CHECK(stat_craft < 0 ||
                           player->stat_xp_tenths[stat_craft] >=
                               craft_xp_before + TOG_REWARD_XP_TENTHS,
                       "complete should award 1000 Crafting XP");
        SELFTEST_CHECK(tog_get_bit(player, "dttd_main") == 0,
                       "bowl hand-in must not write DTtD state");
        tog_pass("oploc1_juna_bowl_handin_complete");

        tog_oploc(srv, loc_juna, loc_slot);
        tog_click_until_menu(srv, 8);
        tog_finish(srv);
        SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_COMPLETE,
                       "post-complete talk must stay at endstate 2");
        tog_pass("oploc1_juna_post_complete");
    }

    /* ---- Journals at every authored arm ---- */
    tog_set_bit(srv, "tog_juna_bowl", TOG_NOT_STARTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,tearsofguthix_journal]", NULL, 0);
    tog_finish(srv);
    tog_pass("proc_journal_not_started");

    tog_set_bit(srv, "tog_juna_bowl", TOG_NEED_BOWL);
    ToriRSServer_ScriptsRunProc(srv, "[proc,tearsofguthix_journal]", NULL, 0);
    tog_finish(srv);
    tog_pass("proc_journal_need_bowl");

    tog_set_bit(srv, "tog_juna_bowl", TOG_COMPLETE);
    ToriRSServer_ScriptsRunProc(srv, "[proc,tearsofguthix_journal]", NULL, 0);
    tog_finish(srv);
    tog_pass("proc_journal_complete");

    /* ::complete twice: first sets endstate, second is a no-op. */
    tog_set_bit(srv, "tog_juna_bowl", TOG_NEED_BOWL);
    {
        int32_t row = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_tearsofguthix");
        int32_t first = 0;
        int32_t second = 0;

        if( row > 0 )
        {
            ToriRSServer_ScriptsRunProcInt(srv, "[proc,quest_cheat_complete]", &row, 1, &first);
            SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_COMPLETE,
                           "::complete quest_tearsofguthix should set endstate 2, got %d",
                           tog_get_bit(player, "tog_juna_bowl"));
            ToriRSServer_ScriptsRunProcInt(srv, "[proc,quest_cheat_complete]", &row, 1, &second);
            SELFTEST_CHECK(tog_get_bit(player, "tog_juna_bowl") == TOG_COMPLETE,
                           "second ::complete must stay at endstate 2");
            tog_pass("quest_cheat_complete_idempotent");
        }
    }

    tog_free_npc(srv, dummy);
    tog_clear_inv(player);
    tog_reset_state(srv);
    tog_god(player);
    ToriRSServer_WorldCloseModal(srv);

    fprintf(stderr, "ToriRSServer tearsofguthix selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_TEARSOFGUTHIX_SELFTEST_U_H */
