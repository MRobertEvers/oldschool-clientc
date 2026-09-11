/* Tale of the Righteous C-walk. Included immediately before the shop stanza.
 * Gate: TORIRSSERVER_SELFTEST_TOR_ONLY=1. Worker branch only.
 * Player is unkillable unless a death case (none here). */
{
    int loaded;
    int varp_tor;
    int varp_veos;
    int varp_clue;
    int varp_qp;
    int npc_phileas;
    int npc_library;
    int npc_shiro;
    int npc_duffy;
    int npc_gnosi;
    int obj_coins;
    int obj_page;
    int obj_rope;
    int stat_str;
    int stat_mine;
    int why;
    int slot;
    int s;
    int qp_before;
    int ran;

    assert(srv);
    assert(player);

    player->godmode = 1;
    setenv("TORIRSSERVER_GOD", "1", 1);
    setenv("TORIRS_PLUGINS", "0", 1);

    fprintf(stderr, "ToriRSServer selftest: Tale of the Righteous\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Tale of the Righteous loads a compiled script pack");
    if( !loaded )
    {
        fprintf(stderr, "  FAIL  no compiled script pack for Tale of the Righteous\n");
    }
    else
    {
        varp_tor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "shayzienquest_main");
        varp_veos = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "veos_quest");
        varp_clue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "cluequest_main");
        varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        npc_phileas = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "phileas_rimor");
        npc_library = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "raidquest_library_archive_guardian");
        npc_shiro = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "shiro_shayzien");
        npc_duffy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "raids_temple_duffy");
        npc_gnosi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "shayzienquest_gnosi");
        obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
        obj_page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "veos_memoirs_shay_page");
        obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
        stat_str = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "strength");
        stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");

        SELFTEST_CHECK(varp_tor >= 0, "shayzienquest_main should resolve");
        SELFTEST_CHECK(varp_veos >= 0, "veos_quest should resolve");
        SELFTEST_CHECK(varp_clue >= 0, "cluequest_main should resolve");
        SELFTEST_CHECK(varp_qp >= 0, "qp should resolve");
        SELFTEST_CHECK(npc_phileas >= 0, "phileas_rimor should resolve");
        SELFTEST_CHECK(npc_library >= 0, "raidquest_library_archive_guardian should resolve");
        SELFTEST_CHECK(npc_shiro >= 0, "shiro_shayzien should resolve");
        SELFTEST_CHECK(npc_duffy >= 0, "raids_temple_duffy should resolve");
        SELFTEST_CHECK(npc_gnosi >= 0, "shayzienquest_gnosi should resolve");
        SELFTEST_CHECK(obj_coins >= 0, "coins should resolve");
        SELFTEST_CHECK(obj_page >= 0, "veos_memoirs_shay_page should resolve");
        SELFTEST_CHECK(obj_rope >= 0, "rope should resolve");
        SELFTEST_CHECK(stat_str >= 0, "strength should resolve");
        SELFTEST_CHECK(stat_mine >= 0, "mining should resolve");

        if( varp_tor >= 0 && varp_veos >= 0 && varp_clue >= 0 && varp_qp >= 0 &&
            npc_phileas >= 0 && npc_library >= 0 && npc_shiro >= 0 &&
            npc_duffy >= 0 && npc_gnosi >= 0 && obj_coins >= 0 && obj_page >= 0 &&
            obj_rope >= 0 && stat_str >= 0 && stat_mine >= 0 )
        {
            player->godmode = 1;
            selftest_reset_world(srv, player, 402, 402);
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, -1, 0);
            player->varps[varp_tor] = 0;
            player->varps[varp_veos] = 0;
            player->varps[varp_clue] = 0;
            ToriRSServer_CombatSetLevel(player, stat_str, 1);
            ToriRSServer_CombatSetLevel(player, stat_mine, 1);

            why = -1;
            ran = ToriRSServer_ScriptsRunProcInt(srv, "[proc,tor_qualify_fail_reason]", NULL, 0, &why);
            SELFTEST_CHECK(ran, "tor_qualify_fail_reason should run");
            SELFTEST_CHECK(why == 1, "missing Client of Kourend is reason 1, got %d", why);

            player->varps[varp_veos] = 7; /* ^cok_complete */
            player->varps[varp_clue] = 0;
            why = -1;
            ran = ToriRSServer_ScriptsRunProcInt(srv, "[proc,tor_qualify_fail_reason]", NULL, 0, &why);
            SELFTEST_CHECK(ran, "tor_qualify_fail_reason should run after CoK");
            SELFTEST_CHECK(why == 2, "missing X Marks the Spot is reason 2, got %d", why);

            player->varps[varp_clue] = 8; /* ^xmarks_complete */
            ToriRSServer_CombatSetLevel(player, stat_str, 1);
            ToriRSServer_CombatSetLevel(player, stat_mine, 10);
            why = -1;
            ran = ToriRSServer_ScriptsRunProcInt(srv, "[proc,tor_qualify_fail_reason]", NULL, 0, &why);
            SELFTEST_CHECK(ran, "tor_qualify_fail_reason should run for Strength");
            SELFTEST_CHECK(why == 3, "Strength under 16 is reason 3, got %d", why);

            ToriRSServer_CombatSetLevel(player, stat_str, 16);
            ToriRSServer_CombatSetLevel(player, stat_mine, 1);
            why = -1;
            ran = ToriRSServer_ScriptsRunProcInt(srv, "[proc,tor_qualify_fail_reason]", NULL, 0, &why);
            SELFTEST_CHECK(ran, "tor_qualify_fail_reason should run for Mining");
            SELFTEST_CHECK(why == 4, "Mining under 10 is reason 4, got %d", why);

            ToriRSServer_CombatSetLevel(player, stat_str, 16);
            ToriRSServer_CombatSetLevel(player, stat_mine, 10);
            why = -1;
            ran = ToriRSServer_ScriptsRunProcInt(srv, "[proc,tor_qualify_fail_reason]", NULL, 0, &why);
            SELFTEST_CHECK(ran, "tor_qualify_fail_reason should run when ready");
            SELFTEST_CHECK(why == 0, "ready player is reason 0, got %d", why);

            /* Qualify-fail talk must not auto-start. */
            player->varps[varp_tor] = 0;
            player->varps[varp_veos] = 0;
            ToriRSServer_WorldTeleport(srv, 0, 1506, 3550);
            selftest_tick(srv);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_phileas, 1507, 3550, 0);
            SELFTEST_CHECK(slot >= 0, "phileas_rimor should spawn");
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_phileas, -1, slot);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 0,
                               "CoK fail must not start the quest, got %d",
                               player->varps[varp_tor]);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* Offer: refuse must not start. */
            player->varps[varp_veos] = 7;
            player->varps[varp_clue] = 8;
            ToriRSServer_CombatSetLevel(player, stat_str, 16);
            ToriRSServer_CombatSetLevel(player, stat_mine, 10);
            player->varps[varp_tor] = 0;
            ToriRSServer_WorldTeleport(srv, 0, 1506, 3550);
            selftest_tick(srv);
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_phileas, 1507, 3550, 0);
            SELFTEST_CHECK(slot >= 0, "phileas_rimor should spawn for the offer");
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_phileas, -1, slot);
                biohazard_run_dialogue(srv, player,
                    ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options"));
                selftest_charter_choose(srv, 2);
                selftest_click_through(srv, 6);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 0,
                               "Not now must not start the quest, got %d",
                               player->varps[varp_tor]);

                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_phileas, -1, slot);
                biohazard_run_dialogue(srv, player,
                    ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options"));
                selftest_charter_choose(srv, 1);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 1,
                               "Yes. should start at ^tor_puzzle=1, got %d",
                               player->varps[varp_tor]);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* Library talk advances to skeleton. */
            player->varps[varp_tor] = 1;
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_library, player->x + 1, player->z, player->level);
            SELFTEST_CHECK(slot >= 0, "library guardian should spawn");
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_library, -1, slot);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 4,
                               "library talk should reach ^tor_skeleton=4, got %d",
                               player->varps[varp_tor]);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* Phileas report -> Shiro. */
            player->varps[varp_tor] = 4;
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_phileas, player->x + 1, player->z, player->level);
            SELFTEST_CHECK(slot >= 0, "phileas_rimor should spawn for the report");
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_phileas, -1, slot);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 6,
                               "skeleton report should reach ^tor_shiro=6, got %d",
                               player->varps[varp_tor]);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* Shiro -> Duffy. */
            player->varps[varp_tor] = 6;
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_shiro, player->x + 1, player->z, player->level);
            SELFTEST_CHECK(slot >= 0, "shiro_shayzien should spawn");
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_shiro, -1, slot);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 7,
                               "Shiro evidence should reach ^tor_duffy=7, got %d",
                               player->varps[varp_tor]);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* Duffy -> rope. */
            player->varps[varp_tor] = 7;
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_duffy, player->x + 1, player->z, player->level);
            SELFTEST_CHECK(slot >= 0, "raids_temple_duffy should spawn");
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_duffy, -1, slot);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 8,
                               "Duffy talk should reach ^tor_rope=8, got %d",
                               player->varps[varp_tor]);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* Gnosi confirm -> Shiro2. */
            player->varps[varp_tor] = 13;
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_gnosi, player->x + 1, player->z, player->level);
            SELFTEST_CHECK(slot >= 0, "shayzienquest_gnosi should spawn");
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gnosi, -1, slot);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 14,
                               "Gnosi confirm should reach ^tor_shiro2=14, got %d",
                               player->varps[varp_tor]);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* Shiro return -> Phileas3. */
            player->varps[varp_tor] = 14;
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_shiro, player->x + 1, player->z, player->level);
            SELFTEST_CHECK(slot >= 0, "shiro_shayzien should spawn for the return");
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_shiro, -1, slot);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_tor] == 15,
                               "Shiro return should reach ^tor_phileas3=15, got %d",
                               player->varps[varp_tor]);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }

            /* Headless walk + authored rewards. */
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, -1, 0);
            player->varps[varp_tor] = 0;
            player->varps[varp_veos] = 7;
            player->varps[varp_clue] = 8;
            ToriRSServer_CombatSetLevel(player, stat_str, 16);
            ToriRSServer_CombatSetLevel(player, stat_mine, 10);
            qp_before = player->varps[varp_qp];
            player->godmode = 1;
            SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "torrun"),
                           "::torrun should run");
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->varps[varp_tor] == 17,
                           "torrun should complete at ^tor_complete=17, got %d",
                           player->varps[varp_tor]);
            SELFTEST_CHECK(selftest_count_obj(player, obj_page) >= 1,
                           "completion should grant Kharedst's memoirs Shayzien page");
            SELFTEST_CHECK(selftest_count_obj(player, obj_coins) >= 8000,
                           "completion should grant 8000 coins, got %d",
                           selftest_count_obj(player, obj_coins));
            SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1,
                           "completion should award 1 QP, %d -> %d",
                           qp_before, player->varps[varp_qp]);
            SELFTEST_CHECK(player->godmode == 1, "player stays unkillable after torrun");

            ToriRSServer_ScriptsRunProc(srv, "[proc,taleoftherighteous_journal]", NULL, 0);
            ToriRSServer_WorldCloseModal(srv);

            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                inv_set(player, s, -1, 0);
            player->varps[varp_tor] = 0;
        }
        ToriRSServer_ScriptsFree(srv);
    }
}
