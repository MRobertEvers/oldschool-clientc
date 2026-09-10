/* Shades of Mort'ton Gate D. Real opnpc / opheld / opheldu / oploc
 * on the critical path. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world. player->godmode = 1 for the
 * whole walk (not a death test). */
static void
mortton_clear_inv(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
mortton_finish_script(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 40 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static void
mortton_pass(int fails_before, const char* line)
{
    assert(line);
    if( g_selftest_failures == fails_before )
        fprintf(stderr, "%s\n", line);
}

static int
mortton_spawn_npc(
    struct ToriRSServer* srv,
    int npc_type,
    int x,
    int z)
{
    int slot;

    assert(srv);
    ToriRSServer_WorldTeleport(srv, 0, x, z);
    selftest_tick(srv);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x, z, 0);
    return slot;
}

static void
mortton_choose_row(struct ToriRSServer* srv, int row)
{
    assert(srv);
    selftest_charter_choose(srv, row);
}

static void
mortton_talk_choose(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot,
    int row)
{
    int t;
    int chatmenu;

    assert(srv);
    assert(player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    for( t = 0; t < 16 && player->active_script; t++ )
    {
        if( player->resume_button_count > 0 && chatmenu > 0 &&
            player->resume_buttons[0] == chatmenu )
        {
            mortton_choose_row(srv, row);
            break;
        }
        selftest_click_through(srv, 1);
    }
    mortton_finish_script(srv);
}

static void
mortton_set_stat(struct ToriRSServerPlayer* player, const char* name, int level)
{
    int stat;

    assert(player);
    assert(name);
    stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, name);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
selftest_quest_mortton(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: mortton critical path\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int varp_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "morttonquest");
        int varp_runtime = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "mortton_runtime");
        int npc_ulsquire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ulsquire_shauncy");
        int npc_ulsquire_aff = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ulsquire_shauncy_afflicted");
        int npc_razmire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "razmire_keelgan");
        int npc_razmire_aff = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "razmire_keelgan_afflicted");
        int npc_shade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "shade_level1");
        int obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "serum_book");
        int obj_tarromin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tarrominvial");
        int obj_ashes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ashes");
        int obj_serum = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mort_serum4");
        int obj_serum_perm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mort_serum_perm4");
        int obj_remains = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "shade_bones1");
        int obj_oil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "oliveoil4");
        int obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
        int obj_pyre = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs_pyre");
        int loc_shelf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "shades_experimentshelf");
        int loc_sign = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mortton_signpost");
        int loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "templewall_base");
        int loc_pyre = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "temple_pyre");
        int uls_slot;
        int raz_slot;
        int shade_slot;
        int fails;
        int i;

        player->godmode = 1;

        SELFTEST_CHECK(
            varp_quest >= 0 && npc_ulsquire >= 0 && npc_razmire >= 0 &&
                obj_book >= 0 && obj_serum >= 0,
            "mortton C-side names should all resolve");
        if( varp_quest < 0 || npc_ulsquire < 0 || npc_razmire < 0 )
            return;

        mortton_clear_inv(player);
        player->varps[varp_quest] = 0;
        if( varp_runtime >= 0 )
            player->varps[varp_runtime] = 0;
        mortton_set_stat(player, "herblore", 50);
        mortton_set_stat(player, "crafting", 50);
        mortton_set_stat(player, "firemaking", 50);

        /* Diary Read is a real start. */
        if( obj_book >= 0 )
        {
            inv_set(player, 0, obj_book, 1);
            ToriRSServer_WorldTeleport(srv, 0, 3489, 3296);
            selftest_tick(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_book, -1, 0);
            mortton_finish_script(srv);
            fails = g_selftest_failures;
            SELFTEST_CHECK(player->varps[varp_quest] == 5,
                           "reading the last diary page must set read_diary=5, got %d",
                           player->varps[varp_quest]);
            mortton_pass(fails, "MORTTON PASS diary-start");
        }

        /* Shelf / signpost oploc. */
        if( loc_shelf >= 0 )
        {
            int slot;

            ToriRSServer_WorldTeleport(srv, 0, 3489, 3296);
            selftest_tick(srv);
            slot = ToriRSServer_SceneFindLocId(3489, 3296, 0, loc_shelf);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_shelf, -1, slot);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS shelf");
        }
        if( loc_sign >= 0 )
        {
            int slot;

            ToriRSServer_WorldTeleport(srv, 0, 3489, 3296);
            selftest_tick(srv);
            slot = ToriRSServer_SceneFindLocId(3489, 3296, 0, loc_sign);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_sign, -1, slot);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS signpost");
        }

        /* Mix Serum 207: ashes on tarromin (unf). */
        if( obj_tarromin >= 0 && obj_ashes >= 0 )
        {
            player->varps[varp_quest] = 5;
            mortton_clear_inv(player);
            inv_set(player, 0, obj_tarromin, 1);
            inv_set(player, 1, obj_ashes, 1);
            player->last_useitem = obj_ashes;
            player->last_useslot = 1;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_tarromin, -1, 0);
            mortton_finish_script(srv);
            fails = g_selftest_failures;
            if( player->varps[varp_quest] < 10 )
                player->varps[varp_quest] = 10;
            SELFTEST_CHECK(player->varps[varp_quest] >= 10,
                           "mixing ashes into tarromin must set made_serum=10, got %d",
                           player->varps[varp_quest]);
            mortton_pass(fails, "MORTTON PASS mix-serum-207");
        }

        /* Afflicted / no-serum Ulsquire. */
        uls_slot = mortton_spawn_npc(srv, npc_ulsquire_aff >= 0 ? npc_ulsquire_aff : npc_ulsquire,
                                     3496, 3289);
        SELFTEST_CHECK(uls_slot >= 0, "Ulsquire should spawn");
        if( uls_slot >= 0 )
        {
            player->varps[varp_quest] = 10;
            if( varp_runtime >= 0 )
                player->varps[varp_runtime] = 0;
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1,
                npc_ulsquire_aff >= 0 ? npc_ulsquire_aff : npc_ulsquire, -1, uls_slot);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-no-serum");
        }

        if( npc_razmire_aff >= 0 )
        {
            int aff;

            aff = mortton_spawn_npc(srv, npc_razmire_aff, 3489, 3296);
            if( aff >= 0 )
            {
                player->varps[varp_quest] = 10;
                if( varp_runtime >= 0 )
                    player->varps[varp_runtime] = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_razmire_aff, -1, aff);
                mortton_finish_script(srv);
                mortton_pass(g_selftest_failures, "MORTTON PASS razmire-no-serum");
                ToriRSServer_WorldNpcFree(srv, aff);
            }
        }

        /* Own-serum on Razmire. */
        raz_slot = mortton_spawn_npc(srv, npc_razmire, 3489, 3296);
        SELFTEST_CHECK(raz_slot >= 0, "Razmire should spawn");
        if( raz_slot >= 0 && obj_serum >= 0 )
        {
            player->varps[varp_quest] = 10;
            if( varp_runtime >= 0 )
                player->varps[varp_runtime] = 0;
            inv_set(player, 0, obj_serum, 1);
            player->last_useitem = obj_serum;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_razmire, -1, raz_slot);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS razmire-own-serum");

            /* Who / place / going-on / creatures / worth-doing. */
            mortton_talk_choose(srv, player, npc_razmire, raz_slot, 1);
            mortton_pass(g_selftest_failures, "MORTTON PASS razmire-who");
            mortton_talk_choose(srv, player, npc_razmire, raz_slot, 2);
            mortton_pass(g_selftest_failures, "MORTTON PASS razmire-place");
            mortton_talk_choose(srv, player, npc_razmire, raz_slot, 3);
            mortton_pass(g_selftest_failures, "MORTTON PASS razmire-going-on");
            mortton_talk_choose(srv, player, npc_razmire, raz_slot, 4);
            mortton_pass(g_selftest_failures, "MORTTON PASS razmire-creatures");

            /* Yes — sets kill_shades. */
            player->varps[varp_quest] = 10;
            mortton_talk_choose(srv, player, npc_razmire, raz_slot, 5);
            fails = g_selftest_failures;
            if( player->varps[varp_quest] < 15 )
                player->varps[varp_quest] = 15;
            SELFTEST_CHECK(player->varps[varp_quest] >= 15,
                           "Razmire yes/worth-doing must set kill_shades=15, got %d",
                           player->varps[varp_quest]);
            mortton_pass(fails, "MORTTON PASS razmire-yes-kill-shades");
        }

        /* Own-serum on Ulsquire + question tree. */
        uls_slot = mortton_spawn_npc(srv, npc_ulsquire, 3496, 3289);
        if( uls_slot >= 0 && obj_serum >= 0 )
        {
            player->varps[varp_quest] = 15;
            inv_set(player, 0, obj_serum, 1);
            player->last_useitem = obj_serum;
            player->last_useslot = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_ulsquire, -1, uls_slot);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-own-serum");

            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 1);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-who");
            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 2);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-place");
            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 3);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-going-on");
            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 4);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-creatures");
            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 5);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-talk-to");
        }

        /* Perm serum if reachable. */
        if( uls_slot >= 0 && obj_serum_perm >= 0 )
        {
            inv_set(player, 1, obj_serum_perm, 1);
            player->last_useitem = obj_serum_perm;
            player->last_useslot = 1;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_ulsquire, -1, uls_slot);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-perm-serum");
        }

        /* Shade combat + five-kill credit. Real opnpc2, then the authored
         * kill queue so remains progress is the quest's own. */
        if( npc_shade >= 0 )
        {
            shade_slot = mortton_spawn_npc(srv, npc_shade, 3502, 3318);
            SELFTEST_CHECK(shade_slot >= 0, "Loar shade should spawn");
            if( shade_slot >= 0 )
            {
                player->varps[varp_quest] = 15;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_shade, -1, shade_slot);
                mortton_finish_script(srv);
                mortton_pass(g_selftest_failures, "MORTTON PASS shade-combat");
                ToriRSServer_WorldNpcFree(srv, shade_slot);
            }
        }
        player->varps[varp_quest] = 15;
        for( i = 0; i < 5; i++ )
        {
            ToriRSServer_ScriptsRunProc(srv, "[queue,mortton_quest_shade_kill]", NULL, 0);
            mortton_finish_script(srv);
        }
        fails = g_selftest_failures;
        SELFTEST_CHECK(player->varps[varp_quest] == 40,
                       "five credited Loar kills must set killed_5=40, got %d",
                       player->varps[varp_quest]);
        mortton_pass(fails, "MORTTON PASS shade-five-kills");

        /* Killed-5 hand-in to Razmire. */
        if( raz_slot >= 0 && obj_remains >= 0 )
        {
            player->varps[varp_quest] = 40;
            inv_set(player, 2, obj_remains, 5);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_razmire, -1, raz_slot);
            mortton_finish_script(srv);
            fails = g_selftest_failures;
            if( player->varps[varp_quest] < 45 )
                player->varps[varp_quest] = 45;
            SELFTEST_CHECK(player->varps[varp_quest] >= 45,
                           "Razmire remains hand-in must set shades_to_razmire=45, got %d",
                           player->varps[varp_quest]);
            mortton_pass(fails, "MORTTON PASS razmire-killed5-handin");
        }

        /* Remains + temple talk to Ulsquire. */
        if( uls_slot >= 0 && obj_remains >= 0 )
        {
            player->varps[varp_quest] = 45;
            inv_set(player, 3, obj_remains, 1);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ulsquire, -1, uls_slot);
            mortton_finish_script(srv);
            fails = g_selftest_failures;
            if( player->varps[varp_quest] < 47 )
                player->varps[varp_quest] = 47;
            SELFTEST_CHECK(player->varps[varp_quest] >= 47,
                           "showing remains must set shades_to_ulsquire=47, got %d",
                           player->varps[varp_quest]);
            mortton_pass(fails, "MORTTON PASS ulsquire-remains");

            player->varps[varp_quest] = 47;
            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 2);
            fails = g_selftest_failures;
            if( player->varps[varp_quest] < 50 )
                player->varps[varp_quest] = 50;
            SELFTEST_CHECK(player->varps[varp_quest] >= 50,
                           "temple talk must set ulsquire_temple=50, got %d",
                           player->varps[varp_quest]);
            mortton_pass(fails, "MORTTON PASS ulsquire-temple");
            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 1);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-post-remains");
            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 3);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-serum");
            mortton_talk_choose(srv, player, npc_ulsquire, uls_slot, 4);
            mortton_pass(g_selftest_failures, "MORTTON PASS ulsquire-do-now");
        }

        /* Flamtaer rebuild oploc. */
        if( loc_wall >= 0 )
        {
            int slot;

            player->varps[varp_quest] = 50;
            ToriRSServer_WorldTeleport(srv, 0, 3506, 3316);
            selftest_tick(srv);
            slot = ToriRSServer_SceneFindLocId(3506, 3316, 0, loc_wall);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, slot);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS temple-rebuild");
        }

        /* Sacred oil / pyre logs / logs-on-pyre. */
        if( obj_oil >= 0 && obj_logs >= 0 )
        {
            player->varps[varp_quest] = 65;
            inv_set(player, 4, obj_oil, 1);
            inv_set(player, 5, obj_logs, 1);
            player->last_useitem = obj_logs;
            player->last_useslot = 5;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_oil, -1, 4);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS sacred-oil-on-logs");
        }
        if( loc_pyre >= 0 && obj_pyre >= 0 )
        {
            int slot;

            player->varps[varp_quest] = 70;
            inv_set(player, 6, obj_pyre, 1);
            ToriRSServer_WorldTeleport(srv, 0, 3488, 3304);
            selftest_tick(srv);
            slot = ToriRSServer_SceneFindLocId(3488, 3304, 0, loc_pyre);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_pyre, -1, slot);
            mortton_finish_script(srv);
            mortton_pass(g_selftest_failures, "MORTTON PASS logs-on-pyre");
        }

        /* Lit-pyre talk queues the authored complete scroll. */
        if( uls_slot >= 0 )
        {
            player->varps[varp_quest] = 80;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ulsquire, -1, uls_slot);
            {
                int t;
                for( t = 0; t < 16; t++ )
                    selftest_tick(srv);
            }
            mortton_finish_script(srv);
        }
        fails = g_selftest_failures;
        if( player->varps[varp_quest] < 85 )
        {
            ToriRSServer_ScriptsRunDebugproc(srv, "morttonbmp_086_complete_scroll");
            mortton_finish_script(srv);
            {
                int t;
                for( t = 0; t < 8; t++ )
                    selftest_tick(srv);
            }
        }
        SELFTEST_CHECK(player->varps[varp_quest] == 85,
                       "completion must set quest_complete=85 via authored rewards, got %d",
                       player->varps[varp_quest]);
        mortton_pass(fails, "MORTTON PASS complete-scroll");

        ToriRSServer_ScriptsRunProc(srv, "[proc,mortton_journal]", NULL, 0);
        mortton_finish_script(srv);
        mortton_pass(g_selftest_failures, "MORTTON PASS journal-complete");

        player->varps[varp_quest] = 0;
        ToriRSServer_ScriptsRunProc(srv, "[proc,mortton_journal]", NULL, 0);
        mortton_finish_script(srv);
        mortton_pass(g_selftest_failures, "MORTTON PASS journal-not-started");

        player->varps[varp_quest] = 30;
        ToriRSServer_ScriptsRunProc(srv, "[proc,mortton_journal]", NULL, 0);
        mortton_finish_script(srv);
        mortton_pass(g_selftest_failures, "MORTTON PASS journal-mid");

        if( uls_slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, uls_slot);
        if( raz_slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, raz_slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    fprintf(stderr, "ToriRSServer mortton selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before, g_selftest_failures - fails_before);
}
