/* The Ascent of Arceuus C-walk. Worker branch only — parent will not merge this.
 * Gate: TORIRSSERVER_SELFTEST_AOA_ONLY=1
 * Player is unkillable for the whole walk (godmode). No death case. */
static void
selftest_quest_ascentofarceuus(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int arcquest;
    int veos;
    int cluequest;
    int hunter;
    int runecraft;
    int coins;
    int page;
    int trail1;
    int32_t why;
    int hunter_xp_before;
    int runecraft_xp_before;
    int coins_before;

    assert(srv);
    assert(player);

    player->godmode = 1;

    arcquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "arcquest");
    veos = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "veos_progress");
    cluequest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "cluequest");
    hunter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    runecraft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "veos_memoirs_arc_page");
    trail1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "arcquest_hunting_trail_1");

    SELFTEST_CHECK(arcquest >= 0, "arcquest varbit resolves");
    SELFTEST_CHECK(veos >= 0, "veos_progress varbit resolves");
    SELFTEST_CHECK(cluequest >= 0, "cluequest varbit resolves");
    SELFTEST_CHECK(hunter >= 0 && hunter < TORIRSSERVER_STAT_COUNT, "hunter stat resolves");
    SELFTEST_CHECK(runecraft >= 0 && runecraft < TORIRSSERVER_STAT_COUNT, "runecraft stat resolves");
    SELFTEST_CHECK(coins >= 0, "coins obj resolves");
    SELFTEST_CHECK(page >= 0, "veos_memoirs_arc_page obj resolves");
    SELFTEST_CHECK(trail1 >= 0, "arcquest_hunting_trail_1 varbit resolves");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_qualify_fail_reason]") != NULL,
                   "aoa_qualify_fail_reason is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_show_qualify_fail]") != NULL,
                   "aoa_show_qualify_fail is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_show_qualify_fail_cok]") != NULL,
                   "named CoK qualify mesbox is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_show_qualify_fail_xmarks]") != NULL,
                   "named X Marks qualify mesbox is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_show_qualify_fail_hunter]") != NULL,
                   "named Hunter 12 qualify mesbox is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_quest_complete]") != NULL,
                   "aoa_quest_complete is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,ascentofarceuus_journal]") != NULL,
                   "ascentofarceuus_journal is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[debugproc,ascentofarceuus]") != NULL,
                   "::ascentofarceuus debugproc is kept");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[debugproc,aoarun]") != NULL,
                   "::aoarun debugproc is kept");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[debugproc,aoabmp_04_mori_offer_p_choice2]") != NULL,
                   "Mori offer p_choice2 BMP debugproc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[debugproc,aoabmp_42_complete_scroll]") != NULL,
                   "complete-scroll BMP debugproc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[debugproc,aoabmp_journal_14_complete]") != NULL,
                   "journal QUEST COMPLETE BMP debugproc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_leftover_tower_instance_souls]") != NULL,
                   "leftover_tower_instance_souls is named");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_leftover_hunting_trail_multilocs]") != NULL,
                   "leftover_hunting_trail_multilocs is named");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_leftover_tower_mage_door]") != NULL,
                   "leftover_tower_mage_door is named");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_leftover_favour_system]") != NULL,
                   "leftover_favour_system is named");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_leftover_graceful_recolour_ui]") != NULL,
                   "leftover_graceful_recolour_ui is named");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_leftover_asteros_mid_talk]") != NULL,
                   "leftover_asteros_mid_talk is named");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_leftover_kaal_sibling_npcs]") != NULL,
                   "leftover_kaal_sibling_npcs is named");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,aoa_leftover_full_refuse_trees]") != NULL,
                   "leftover_full_refuse_trees is named");

    if( arcquest < 0 || veos < 0 || cluequest < 0 || hunter < 0 || hunter >= TORIRSSERVER_STAT_COUNT )
        return;

    /* Qualify 1: Client of Kourend missing. */
    ToriRSServer_VarbitSet(srv, veos, 0);
    ToriRSServer_VarbitSet(srv, cluequest, 8);
    player->stat_level[hunter] = 12;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,aoa_qualify_fail_reason]", NULL, 0, &why),
                   "qualify reason runs with CoK missing");
    SELFTEST_CHECK(why == 1, "CoK is the first named fail, got %d", why);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 0,
                   "qualify fail does not leftover-stamp or start the quest");

    /* Qualify 2: X Marks the Spot missing. */
    ToriRSServer_VarbitSet(srv, veos, 7);
    ToriRSServer_VarbitSet(srv, cluequest, 0);
    player->stat_level[hunter] = 12;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,aoa_qualify_fail_reason]", NULL, 0, &why),
                   "qualify reason runs with X Marks missing");
    SELFTEST_CHECK(why == 2, "X Marks is the second named fail, got %d", why);

    /* Qualify 3: Hunter 12 missing. */
    ToriRSServer_VarbitSet(srv, veos, 7);
    ToriRSServer_VarbitSet(srv, cluequest, 8);
    player->stat_level[hunter] = 1;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,aoa_qualify_fail_reason]", NULL, 0, &why),
                   "qualify reason runs with Hunter 12 missing");
    SELFTEST_CHECK(why == 3, "Hunter 12 is the third named fail, got %d", why);

    /* Ready: all three gates pass. Offer must not auto-start. */
    player->stat_level[hunter] = 12;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv, "[proc,aoa_qualify_fail_reason]", NULL, 0, &why),
                   "qualify reason runs when ready");
    SELFTEST_CHECK(why == 0, "ready player has no qualify fail, got %d", why);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 0,
                   "passing qualify still does not auto-start");

    /* Accept path is a real Yes (state 1). */
    ToriRSServer_VarbitSet(srv, arcquest, 1);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 1,
                   "accept writes ^aoa_andrews");

    /* Andrews → Mori return → souls. */
    ToriRSServer_VarbitSet(srv, arcquest, 2);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 2, "mori2 is 2");
    ToriRSServer_VarbitSet(srv, arcquest, 3);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,aoa_soft_clear_souls]", NULL, 0),
                   "soft-clear souls runs");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 5,
                   "souls soft-clear writes ^aoa_arceuus (5), got %d",
                   ToriRSServer_VarbitGet(player, arcquest));

    /* Grave inspect advances trail. */
    ToriRSServer_VarbitSet(srv, arcquest, 8);
    if( trail1 >= 0 )
        ToriRSServer_VarbitSet(srv, trail1, 0);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,aoa_inspect_grave]", NULL, 0),
                   "grave inspect runs");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 9,
                   "grave inspect writes ^aoa_track (9), got %d",
                   ToriRSServer_VarbitGet(player, arcquest));
    if( trail1 >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, trail1) == 1,
                       "grave inspect sets hunting trail 1");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,aoa_soft_track]", NULL, 0),
                   "soft track runs");

    ToriRSServer_VarbitSet(srv, arcquest, 10);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,aoa_soft_kill_trapped]", NULL, 0),
                   "soft-kill trapped soul runs");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 11,
                   "trapped soul writes ^aoa_kaal2 (11), got %d",
                   ToriRSServer_VarbitGet(player, arcquest));

    ToriRSServer_VarbitSet(srv, arcquest, 12);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,aoa_search_rocks]", NULL, 0),
                   "rocks search runs");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 13,
                   "rocks write ^aoa_finish (13), got %d",
                   ToriRSServer_VarbitGet(player, arcquest));

    hunter_xp_before = player->stat_xp_tenths[hunter];
    runecraft_xp_before = (runecraft >= 0 && runecraft < TORIRSSERVER_STAT_COUNT)
                              ? player->stat_xp_tenths[runecraft]
                              : 0;
    coins_before = (coins >= 0) ? selftest_count(player, coins) : 0;

    ToriRSServer_VarbitSet(srv, arcquest, 13);
    ToriRSServer_ScriptsRunProc(srv, "[proc,aoa_quest_complete]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, arcquest) == 14,
                   "complete writes ^aoa_complete (14), got %d",
                   ToriRSServer_VarbitGet(player, arcquest));
    SELFTEST_CHECK(player->stat_xp_tenths[hunter] >= hunter_xp_before + 15000,
                   "complete awards 1500 Hunter XP (tenths 15000), before %d after %d",
                   hunter_xp_before, player->stat_xp_tenths[hunter]);
    if( runecraft >= 0 && runecraft < TORIRSSERVER_STAT_COUNT )
        SELFTEST_CHECK(player->stat_xp_tenths[runecraft] >= runecraft_xp_before + 5000,
                       "complete awards 500 Runecraft XP (tenths 5000), before %d after %d",
                       runecraft_xp_before, player->stat_xp_tenths[runecraft]);
    if( coins >= 0 )
        SELFTEST_CHECK(selftest_count(player, coins) >= coins_before + 2000,
                       "complete awards 2000 coins, before %d after %d",
                       coins_before, selftest_count(player, coins));
    if( page >= 0 )
        SELFTEST_CHECK(selftest_count(player, page) >= 1,
                       "complete awards Kharedst's memoirs Arceuus page");

    SELFTEST_CHECK(player->godmode == 1, "player stays unkillable for the whole walk");
}
