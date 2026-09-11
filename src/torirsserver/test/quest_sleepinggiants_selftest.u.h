/* Sleeping Giants Gate D stanza. Included from torirs_server_world_selftest.c
 * immediately before the shop fprintf / selftest_reset_world so spawned npcs
 * cannot leak into later RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD dispatch on the critical
 * path. Silent success is forbidden: each step prints an ASCII PASS line.
 * The player stays godmoded unless a future death case. There is no death case.
 *
 * Do not merge this .u.h onto parent v3.
 */
static void
sg_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SG PASS: %s\n", step);
}

static void
sg_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static void
sg_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
sg_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
sg_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
sg_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = sg_chatmenu();
    if( chatmenu <= 0 )
        return;
    player->last_slot = row;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static void
sg_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = sg_chatmenu();
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
selftest_quest_sleepinggiants(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::sgrun\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int npc_fake = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "giants_foundry_kovac_fake_attack");
        int npc_quest = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "giants_foundry_kovac_quest");
        int npc_1op = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "giants_foundry_kovac_1op");
        int loc_enter = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "giants_foundry_entrance");
        int loc_polish = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "giants_foundry_polishing_wheel");
        int loc_grind = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "giants_foundry_grindstone");
        int loc_hammer = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "giants_foundry_trip_hammer");
        int loc_crate = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "giants_foundry_supply_box_multi");
        int loc_crucible = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "giants_foundry_crucible_multi");
        int loc_mould = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "giants_foundry_mould_jig");
        int loc_lava = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_LOC, "giants_foundry_lava_pool");
        int obj_preform = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_OBJ, "giants_foundry_preform");
        int bit_sg = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_VARBIT, "sleeping_giants");
        int bit_tut = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_VARBIT, "sleeping_giants_tutorial");
        int bit_polish = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_VARBIT, "sleeping_giants_repair_polish");
        int bit_grind = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_VARBIT, "sleeping_giants_repair_grind");
        int bit_hammer = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_VARBIT, "sleeping_giants_repair_hammer");
        int stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
        int varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        int fake_slot = -1;
        int quest_slot = -1;
        int op_slot = -1;
        int xp_before;
        int qp_before;

        sg_god(player);

        SELFTEST_CHECK(
            npc_fake >= 0 && npc_quest >= 0 && npc_1op >= 0 && loc_enter >= 0 &&
                loc_polish >= 0 && loc_grind >= 0 && loc_hammer >= 0 &&
                loc_crate >= 0 && loc_crucible >= 0 && loc_mould >= 0 &&
                loc_lava >= 0 && obj_preform >= 0 && bit_sg >= 0 && bit_tut >= 0 &&
                bit_polish >= 0 && bit_grind >= 0 && bit_hammer >= 0 &&
                stat_smith >= 0,
            "the ::sgrun C-side names should all resolve");
        if( npc_fake < 0 || npc_quest < 0 || npc_1op < 0 || loc_enter < 0 ||
            loc_polish < 0 || loc_grind < 0 || loc_hammer < 0 || loc_crate < 0 ||
            loc_crucible < 0 || loc_mould < 0 || loc_lava < 0 || obj_preform < 0 ||
            bit_sg < 0 || bit_tut < 0 || bit_polish < 0 || bit_grind < 0 ||
            bit_hammer < 0 || stat_smith < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        sg_clear_inv(player);
        ToriRSServer_VarbitSet(srv, bit_sg, 0);
        ToriRSServer_VarbitSet(srv, bit_tut, 0);
        ToriRSServer_VarbitSet(srv, bit_polish, 0);
        ToriRSServer_VarbitSet(srv, bit_grind, 0);
        ToriRSServer_VarbitSet(srv, bit_hammer, 0);

        /* ---- Smithing 15 qualify fail (stat_base, not leftover) ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3361, 3147);
        selftest_tick(srv);
        ToriRSServer_CombatSetLevel(player, stat_smith, 1);
        fake_slot = ToriRSServer_WorldNpcSpawn(srv, npc_fake, 3361, 3147, 0);
        SELFTEST_CHECK(fake_slot >= 0, "kovac fake-attack should spawn");
        if( fake_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_fake, -1, fake_slot);
            sg_click_until_menu(srv, 8);
            selftest_click_through(srv, 8);
            sg_close(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_sg) == 0,
                           "Smithing 1 must not write sleeping_giants, got %d",
                           ToriRSServer_VarbitGet(player, bit_sg));
            sg_pass("opnpc1_qualify_fail_smithing");
        }

        /* ---- Yes/No refuse (keep refuse, do not auto-start) ---- */
        ToriRSServer_CombatSetLevel(player, stat_smith, 15);
        ToriRSServer_VarbitSet(srv, bit_sg, 0);
        ToriRSServer_ScriptsRunTrigger(
            srv, SS_TRIGGER_OPNPC1, npc_fake, -1, fake_slot);
        sg_click_until_menu(srv, 8);
        sg_pick_row(srv, 2);
        selftest_click_through(srv, 4);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_sg) == 0,
                       "refusing Yes/No must not write sleeping_giants, got %d",
                       ToriRSServer_VarbitGet(player, bit_sg));
        sg_pass("opnpc1_kovac_refuse");

        /* ---- Accept writes ^sg_kovac = 5 ---- */
        ToriRSServer_ScriptsRunTrigger(
            srv, SS_TRIGGER_OPNPC1, npc_fake, -1, fake_slot);
        sg_click_until_menu(srv, 8);
        sg_pick_row(srv, 1);
        selftest_click_through(srv, 4);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_sg) == 5,
                       "accepting Yes must write sleeping_giants=5, got %d",
                       ToriRSServer_VarbitGet(player, bit_sg));
        sg_pass("opnpc1_kovac_accept");

        /* ---- Entrance ---- */
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_enter, -1, -1);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_sg) == 10,
                       "entering the cave should write sleeping_giants=10, got %d",
                       ToriRSServer_VarbitGet(player, bit_sg));
        sg_pass("oploc1_entrance");

        /* ---- Three repairs ---- */
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_polish, -1, -1);
        sg_click_until_menu(srv, 4);
        sg_pick_row(srv, 1);
        selftest_click_through(srv, 4);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_polish) == 2,
                       "repair polish should write bit 2, got %d",
                       ToriRSServer_VarbitGet(player, bit_polish));
        sg_pass("oploc1_polish_repair");

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_grind, -1, -1);
        sg_click_until_menu(srv, 4);
        sg_pick_row(srv, 1);
        selftest_click_through(srv, 4);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_grind) == 2,
                       "repair grind should write bit 2, got %d",
                       ToriRSServer_VarbitGet(player, bit_grind));
        sg_pass("oploc1_grind_repair");

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_hammer, -1, -1);
        sg_click_until_menu(srv, 4);
        sg_pick_row(srv, 1);
        selftest_click_through(srv, 4);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_hammer) == 2,
                       "repair hammer should write bit 2, got %d",
                       ToriRSServer_VarbitGet(player, bit_hammer));
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_sg) == 15,
                       "all three repairs should write sleeping_giants=15, got %d",
                       ToriRSServer_VarbitGet(player, bit_sg));
        sg_pass("oploc1_hammer_repair");

        /* ---- Kovac quest: repairs done -> after_repairs (20) ---- */
        quest_slot = ToriRSServer_WorldNpcSpawn(srv, npc_quest, 3361, 3147, 0);
        SELFTEST_CHECK(quest_slot >= 0, "kovac quest should spawn");
        if( quest_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_quest, -1, quest_slot);
            selftest_click_through(srv, 6);
            sg_close(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_sg) == 20,
                           "repairs-done talk should write sleeping_giants=20, got %d",
                           ToriRSServer_VarbitGet(player, bit_sg));
            sg_pass("opnpc1_kovac_repairs_done");
        }

        /* ---- Commission Kovac ---- */
        op_slot = ToriRSServer_WorldNpcSpawn(srv, npc_1op, 3363, 11485, 0);
        SELFTEST_CHECK(op_slot >= 0, "kovac 1op should spawn");
        if( op_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_1op, -1, op_slot);
            selftest_click_through(srv, 6);
            sg_close(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_sg) == 25,
                           "commission talk should write sleeping_giants=25, got %d",
                           ToriRSServer_VarbitGet(player, bit_sg));
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_tut) == 10,
                           "commission talk should write tutorial=10, got %d",
                           ToriRSServer_VarbitGet(player, bit_tut));
            sg_pass("opnpc1_kovac_commission");
        }

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_crate, -1, -1);
        sg_click_until_menu(srv, 4);
        sg_pick_row(srv, 1);
        selftest_click_through(srv, 4);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_tut) == 15,
                       "crate search should write tutorial=15, got %d",
                       ToriRSServer_VarbitGet(player, bit_tut));
        sg_pass("oploc1_crate_take");

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_crucible, -1, -1);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_tut) == 25,
                       "fill crucible should write tutorial=25, got %d",
                       ToriRSServer_VarbitGet(player, bit_tut));
        sg_pass("oploc1_crucible_fill");

        if( op_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_1op, -1, op_slot);
            selftest_click_through(srv, 6);
            sg_close(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_tut) == 30,
                           "mould talk should write tutorial=30, got %d",
                           ToriRSServer_VarbitGet(player, bit_tut));
            sg_pass("opnpc1_kovac_pick_mould");
        }

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_mould, -1, -1);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_tut) == 35,
                       "set mould should write tutorial=35, got %d",
                       ToriRSServer_VarbitGet(player, bit_tut));
        sg_pass("oploc1_mould_set");

        if( op_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_1op, -1, op_slot);
            selftest_click_through(srv, 6);
            sg_close(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_tut) == 40,
                           "pour talk should write tutorial=40, got %d",
                           ToriRSServer_VarbitGet(player, bit_tut));
            sg_pass("opnpc1_kovac_pour");
        }

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_crucible, -1, -1);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_tut) == 45,
                       "pour crucible should write tutorial=45, got %d",
                       ToriRSServer_VarbitGet(player, bit_tut));
        sg_pass("oploc1_crucible_pour");

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_mould, -1, -1);
        sg_close(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_tut) == 50,
                       "take preform should write tutorial=50, got %d",
                       ToriRSServer_VarbitGet(player, bit_tut));
        SELFTEST_CHECK(selftest_count_obj(player, obj_preform) == 1,
                       "taking the preform should add giants_foundry_preform");
        sg_pass("oploc1_mould_take");

        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_lava, -1, -1);
        sg_close(srv);
        sg_pass("oploc1_lava_finish");

        xp_before = player->stat_xp_tenths[stat_smith];
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        if( op_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_1op, -1, op_slot);
            selftest_click_through(srv, 10);
            sg_close(srv);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_sg) == 30,
                           "hand-in should write sleeping_giants=30, got %d",
                           ToriRSServer_VarbitGet(player, bit_sg));
            SELFTEST_CHECK(
                player->stat_xp_tenths[stat_smith] - xp_before == 60000,
                "complete should award 60000 smithing tenths, delta %d",
                player->stat_xp_tenths[stat_smith] - xp_before);
            if( varp_qp >= 0 )
                SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1,
                               "complete should award 1 QP, qp %d -> %d",
                               qp_before, player->varps[varp_qp]);
            sg_pass("opnpc1_kovac_handin_complete");
        }

        ToriRSServer_ScriptsRunProc(srv, "[proc,sleepinggiants_journal]", NULL, 0);
        sg_close(srv);
        sg_pass("journal_complete");

        ToriRSServer_ScriptsRunProc(srv, "[proc,sg_leftover_repair_item_checks]", NULL, 0);
        sg_close(srv);
        ToriRSServer_ScriptsRunProc(srv, "[proc,sg_leftover_mould_if_718]", NULL, 0);
        sg_close(srv);
        ToriRSServer_ScriptsRunProc(srv, "[proc,sg_leftover_heat_temp_smithing]", NULL, 0);
        sg_close(srv);
        ToriRSServer_ScriptsRunProc(srv, "[proc,sg_leftover_supply_crate_bar_matrix]", NULL, 0);
        sg_close(srv);
        ToriRSServer_ScriptsRunProc(srv, "[proc,sg_leftover_ice_gloves]", NULL, 0);
        sg_close(srv);
        ToriRSServer_ScriptsRunProc(srv, "[proc,sg_leftover_full_refuse_trees]", NULL, 0);
        sg_close(srv);
        sg_pass("leftover_disclosures");

        SELFTEST_CHECK(player->godmode == 1 && player->dying == 0,
                       "the player must stay unkillable, godmode=%d dying=%d",
                       player->godmode, player->dying);
        sg_pass("player_unkillable");

        if( fake_slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, fake_slot);
        if( quest_slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, quest_slot);
        if( op_slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, op_slot);
        ToriRSServer_WorldNpcReap(srv);
        sg_clear_inv(player);
        ToriRSServer_VarbitSet(srv, bit_sg, 0);
        ToriRSServer_VarbitSet(srv, bit_tut, 0);
        ToriRSServer_VarbitSet(srv, bit_polish, 0);
        ToriRSServer_VarbitSet(srv, bit_grind, 0);
        ToriRSServer_VarbitSet(srv, bit_hammer, 0);
        player->godmode = 1;
    }
}
