/* Ethically Acquired Antiquities Gate D C-walk. Worker-branch only.
 * Guarded by TORIRSSERVER_SELFTEST_EAA_ONLY=1 (not GOD_ONLY).
 *
 * Proves authored qualify (CotS / Shield of Arrav complete / Thieving 25),
 * refuse does not write %eaa, accept writes ^eaa_herminius, mid-quest
 * talks already in-tree, and complete awards 6000 Thieving XP + 5000 coins.
 */
static void
eaa_selftest_close(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
}

static void
eaa_selftest_set_thieving(struct ToriRSServerPlayer* player, int thieving, int level)
{
    assert(player);
    assert(thieving >= 0);
    player->stat_level[thieving] = level;
    player->stat_boosted[thieving] = level;
}

static void
selftest_eaa(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int eaa_bit;
    int vmq1_bit;
    int varp_phoenix;
    int varp_blackarm;
    int thieving;
    int obj_coins;
    int s;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::eaarun (EAA_ONLY)\n");

    player->godmode = 1;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "EAA C-walk needs a compiled script pack");
    if( !loaded )
        return;

    eaa_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaa");
    vmq1_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1");
    varp_phoenix = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "phoenixgang");
    varp_blackarm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "blackarmgang");
    thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");

    SELFTEST_CHECK(eaa_bit >= 0 && vmq1_bit >= 0 && varp_phoenix >= 0 &&
                       varp_blackarm >= 0 && thieving >= 0 && obj_coins >= 0,
                   "EAA C-walk symbols should all resolve");
    if( eaa_bit < 0 || vmq1_bit < 0 || varp_phoenix < 0 || varp_blackarm < 0 ||
        thieving < 0 || obj_coins < 0 )
    {
        ToriRSServer_ScriptsFree(srv);
        return;
    }

    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);

    /* ---- CotS fail: %eaa stays 0 ---- */
    ToriRSServer_VarbitSet(srv, eaa_bit, 0);
    ToriRSServer_VarbitSet(srv, vmq1_bit, 0);
    player->varps[varp_phoenix] = 10; /* ^phoenixgang_complete */
    player->varps[varp_blackarm] = 0;
    eaa_selftest_set_thieving(player, thieving, 25);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_01_qualify_fail_cots");
    eaa_selftest_close(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 0,
                   "CotS fail must not write %%eaa, got %d",
                   ToriRSServer_VarbitGet(player, eaa_bit));

    /* ---- Shield of Arrav fail: joined is not enough; both gangs incomplete ---- */
    ToriRSServer_VarbitSet(srv, eaa_bit, 0);
    ToriRSServer_VarbitSet(srv, vmq1_bit, 24); /* ^cots_complete */
    player->varps[varp_phoenix] = 9;           /* ^phoenixgang_joined, not complete */
    player->varps[varp_blackarm] = 3;          /* ^blackarmgang_joined, not complete */
    eaa_selftest_set_thieving(player, thieving, 25);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_02_qualify_fail_shield_of_arrav");
    eaa_selftest_close(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 0,
                   "Shield-of-Arrav fail (joined, not complete) must not write %%eaa, got %d",
                   ToriRSServer_VarbitGet(player, eaa_bit));

    /* Either gang complete should pass Shield. Thieving still fails here. */
    ToriRSServer_VarbitSet(srv, eaa_bit, 0);
    ToriRSServer_VarbitSet(srv, vmq1_bit, 24);
    player->varps[varp_phoenix] = 10;
    player->varps[varp_blackarm] = 0;
    eaa_selftest_set_thieving(player, thieving, 1);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_03_qualify_fail_thieving");
    eaa_selftest_close(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 0,
                   "Thieving 25 fail must not write %%eaa, got %d",
                   ToriRSServer_VarbitGet(player, eaa_bit));

    /* ---- Refuse: chat only, %%eaa stays 0 ---- */
    ToriRSServer_VarbitSet(srv, eaa_bit, 0);
    ToriRSServer_VarbitSet(srv, vmq1_bit, 24);
    player->varps[varp_phoenix] = 10;
    eaa_selftest_set_thieving(player, thieving, 25);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_05_refuse");
    eaa_selftest_close(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 0,
                   "refuse must not write %%eaa, got %d",
                   ToriRSServer_VarbitGet(player, eaa_bit));

    /* ---- Accept: writes ^eaa_herminius = 2 ---- */
    ToriRSServer_VarbitSet(srv, eaa_bit, 0);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_06_accept");
    eaa_selftest_close(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 2,
                   "accept should write %%eaa = ^eaa_herminius (2), got %d",
                   ToriRSServer_VarbitGet(player, eaa_bit));

    /* ---- Mid-quest talks already in-tree ---- */
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_07_herminius_theft");
    eaa_selftest_close(srv, player);
    {
        int npc_herminius = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "fortis_museum_curator");
        int npc_slot;

        SELFTEST_CHECK(npc_herminius >= 0, "fortis_museum_curator should resolve");
        ToriRSServer_VarbitSet(srv, eaa_bit, 2);
        npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_herminius, player->x + 1,
                                              player->z, player->level);
        SELFTEST_CHECK(npc_slot >= 0, "Herminius should spawn for the theft talk");
        if( npc_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_herminius,
                                           -1, npc_slot);
            eaa_selftest_close(srv, player);
            SELFTEST_CHECK(
                ToriRSServer_VarbitGet(player, eaa_bit) == 4,
                "Herminius theft talk should advance %%eaa to ^eaa_investigate (4), got %d",
                ToriRSServer_VarbitGet(player, eaa_bit));
            ToriRSServer_WorldNpcFree(srv, npc_slot);
            ToriRSServer_WorldNpcReap(srv);
        }
    }

    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_09_tools_investigate");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_11_display_scratches");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_13_academic_clue");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_16_tourist_clue");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_19_citizen_clue");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_23_regulus_clue");
    eaa_selftest_close(srv, player);
    ToriRSServer_VarbitSet(srv, eaa_bit, 8);
    ToriRSServer_ScriptsRunProc(srv, "[proc,eaa_try_visitors_done]", NULL, 0);
    eaa_selftest_close(srv, player);

    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_25_crew_favour_offer");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_30_artima_repair");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_33_crew_return_sails");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_37_stan_betty");
    eaa_selftest_close(srv, player);
    {
        int npc_stan = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "sailing_transport_trader_stan");
        int npc_slot;

        ToriRSServer_VarbitSet(srv, eaa_bit, 16);
        npc_slot = ToriRSServer_WorldNpcSpawn(srv, npc_stan, player->x + 1,
                                              player->z, player->level);
        SELFTEST_CHECK(npc_slot >= 0, "Trader Stan should spawn");
        if( npc_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stan, -1,
                                           npc_slot);
            eaa_selftest_close(srv, player);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 18,
                           "Stan should advance %%eaa to ^eaa_betty (18), got %d",
                           ToriRSServer_VarbitGet(player, eaa_bit));
            ToriRSServer_WorldNpcFree(srv, npc_slot);
            ToriRSServer_WorldNpcReap(srv);
        }
    }

    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_39_betty_notes");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_42_read_notes");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_43_haig_deny");
    eaa_selftest_close(srv, player);
    ToriRSServer_VarbitSet(srv, eaa_bit, 22);
    ToriRSServer_ScriptsRunProc(srv, "[proc,eaa_haig_talk]", NULL, 0);
    eaa_selftest_close(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 24,
                   "Haig deny should advance %%eaa to ^eaa_loot (24), got %d",
                   ToriRSServer_VarbitGet(player, eaa_bit));

    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_44_pickpocket_haig");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_50_crate_search");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunProc(srv, "[proc,eaa_search_crate]", NULL, 0);
    eaa_selftest_close(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 28,
                   "crate search should advance %%eaa to ^eaa_shame_talk (28), got %d",
                   ToriRSServer_VarbitGet(player, eaa_bit));

    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_52_haig_found");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_53_haig_shame_options");
    eaa_selftest_close(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_61_herminius_return");
    eaa_selftest_close(srv, player);

    /* ---- Complete: 1 QP path, 6000 Thieving XP, 5000 coins ---- */
    {
        int xp_before;
        int coins_before = 0;
        int coins_after = 0;

        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
            inv_set(player, s, -1, 0);
        ToriRSServer_VarbitSet(srv, eaa_bit, 36); /* ^eaa_return */
        xp_before = player->stat_xp_tenths[thieving];
        ToriRSServer_ScriptsRunProc(srv, "[proc,eaa_quest_complete]", NULL, 0);
        eaa_selftest_close(srv, player);
        {
            int drain_tick;
            for( drain_tick = 0;
                 drain_tick < 40 && ToriRSServer_VarbitGet(player, eaa_bit) != 38;
                 drain_tick++ )
            {
                ToriRSServer_WorldCloseModal(srv);
                selftest_tick(srv);
            }
        }
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 38,
                       "complete should write %%eaa = ^eaa_complete (38), got %d",
                       ToriRSServer_VarbitGet(player, eaa_bit));
        SELFTEST_CHECK(player->stat_xp_tenths[thieving] == xp_before + 60000,
                       "complete should award 60000 Thieving tenths, %d -> %d",
                       xp_before, player->stat_xp_tenths[thieving]);
        for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        {
            if( player->inv[s].obj_id == obj_coins )
                coins_after += player->inv[s].count;
        }
        (void)coins_before;
        SELFTEST_CHECK(coins_after >= 5000,
                       "complete should grant 5000 coins, got %d", coins_after);
    }

    ToriRSServer_ScriptsRunDebugproc(srv, "eaabmp_journal_38_complete");
    eaa_selftest_close(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, eaa_bit) == 38,
                   "journal complete must keep %%eaa at 38, got %d",
                   ToriRSServer_VarbitGet(player, eaa_bit));

    ToriRSServer_VarbitSet(srv, eaa_bit, 0);
    ToriRSServer_ScriptsFree(srv);
}
