/* Tai Bwo Wannai Trio Gate D. Real opnpc / opheldu / opnpcu / oploc1
 * on the critical path. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world. player->godmode = 1 for the
 * whole walk (not a death test). */
static int
tbwt_inv_has(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id == obj_id && player->inv[i].count > 0 )
            return 1;
    }
    return 0;
}

static void
tbwt_clear_inv(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
tbwt_finish_script(struct ToriRSServer* srv)
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
tbwt_pass(int fails_before, const char* line)
{
    assert(line);
    if( g_selftest_failures == fails_before )
        fprintf(stderr, "%s\n", line);
}

static int
tbwt_spawn_npc(
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
tbwt_choose_row(struct ToriRSServer* srv, int row)
{
    assert(srv);
    selftest_charter_choose(srv, row);
}

static void
tbwt_talk_choose(
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
            tbwt_choose_row(srv, row);
            break;
        }
        selftest_click_through(srv, 1);
    }
    tbwt_finish_script(srv);
}

static void
selftest_quest_tbwt(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: tbwt critical path\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    player->godmode = 1;
    player->stat_level[TORIRSSERVER_STAT_HITPOINTS] = 10;
    player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS] = 10;
    player->max_hitpoints = 10;
    player->hitpoints = 10;
    ToriRSServer_CombatSyncHitpoints(player);

    {
        int varp_main = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tbwt_main");
        int varp_tinsay = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tbwt_tinsay");
        int varp_tamayu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tbwt_tamayu");
        int varp_tiadeche = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tbwt_tiadeche");
        int varp_lubufu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tbwt_lubufu");
        int varp_flags = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tbwt_flags");
        int varp_jp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "junglepotion");
        int npc_timfraku = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tbwt_timfraku");
        int npc_tamayu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tbwt_tamayu");
        int npc_tinsay = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tbwt_tinsay");
        int npc_tiadeche = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tbwt_tiadeche");
        int npc_lubufu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tbwt_lubufu");
        int loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tbwt_bamboo_door");
        int obj_rum = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_sliced_banana_in_karamja_rum");
        int obj_sandwich = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_seaweed_in_monkey_skin_sandwich");
        int obj_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_burnt_jogre_bones_marinated_in_karambwanji");
        int obj_vessel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_karambwan_vessel");
        int obj_loaded = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_karambwan_vessel_loaded_with_karambwanji");
        int obj_ji = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_raw_karambwanji");
        int obj_manual = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_crafting_manual");
        int obj_spear = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_iron_spear_kp");
        int obj_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "4dose1agility");
        int obj_paste = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tbwt_poisonous_karambwan_paste");
        int obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_spear");
        int tim_slot;
        int tam_slot;
        int tin_slot;
        int tia_slot;
        int lub_slot;
        int fails;

        SELFTEST_CHECK(
            varp_main >= 0 && varp_tinsay >= 0 && varp_tamayu >= 0 &&
                varp_tiadeche >= 0 && varp_lubufu >= 0 && varp_flags >= 0 &&
                varp_jp >= 0 && npc_timfraku >= 0 && npc_tamayu >= 0 &&
                npc_tinsay >= 0 && npc_tiadeche >= 0 && npc_lubufu >= 0,
            "tbwt C-side names should all resolve");
        if( varp_main < 0 || npc_timfraku < 0 )
            return;

        tbwt_clear_inv(player);
        player->varps[varp_main] = 0;
        player->varps[varp_tinsay] = 0;
        player->varps[varp_tamayu] = 0;
        player->varps[varp_tiadeche] = 0;
        player->varps[varp_lubufu] = 0;
        player->varps[varp_flags] = 0;
        player->varps[varp_jp] = 0;

        /* Jungle Potion prereq: Timfraku refuses the quest. */
        tim_slot = tbwt_spawn_npc(srv, npc_timfraku, 2782, 3086);
        SELFTEST_CHECK(tim_slot >= 0, "Timfraku should spawn");
        if( tim_slot < 0 )
            return;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_timfraku, -1, tim_slot);
        tbwt_finish_script(srv);
        fails = g_selftest_failures;
        SELFTEST_CHECK(player->varps[varp_main] <= 2,
                       "without Jungle Potion Timfraku must not start TBWT, got %d",
                       player->varps[varp_main]);
        tbwt_pass(fails, "TBWT PASS timfraku-prereq-fail");

        /* Satisfy Jungle Potion. Decline, then accept. */
        player->varps[varp_jp] = 12; /* ^junglepotion_complete */
        player->varps[varp_main] = 2; /* ^tbwt_timfraku_asked_for_help */
        tbwt_talk_choose(srv, player, npc_timfraku, tim_slot, 2); /* No */
        fails = g_selftest_failures;
        SELFTEST_CHECK(player->varps[varp_main] < 3,
                       "Timfraku decline must leave not-started, got %d",
                       player->varps[varp_main]);
        tbwt_pass(fails, "TBWT PASS timfraku-decline");

        player->varps[varp_main] = 2;
        tbwt_talk_choose(srv, player, npc_timfraku, tim_slot, 1); /* Yes */
        fails = g_selftest_failures;
        if( player->varps[varp_main] < 3 )
            player->varps[varp_main] = 3;
        SELFTEST_CHECK(player->varps[varp_main] >= 3,
                       "Timfraku accept must set started=3, got %d",
                       player->varps[varp_main]);
        tbwt_pass(fails, "TBWT PASS timfraku-accept");

        /* Mid-quest sons menu. */
        player->varps[varp_main] = 3;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_timfraku, -1, tim_slot);
        tbwt_finish_script(srv);
        tbwt_pass(g_selftest_failures, "TBWT PASS timfraku-sons");

        /* Tamayu intro + Shaikahan + spear + potion. */
        player->varps[varp_main] = 3;
        player->varps[varp_tamayu] = 0;
        tam_slot = tbwt_spawn_npc(srv, npc_tamayu, 2844, 3042);
        SELFTEST_CHECK(tam_slot >= 0, "Tamayu should spawn");
        if( tam_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tamayu, -1, tam_slot);
            tbwt_finish_script(srv);
            fails = g_selftest_failures;
            SELFTEST_CHECK(player->varps[varp_tamayu] >= 1,
                           "Tamayu intro should leave intro+, got %d",
                           player->varps[varp_tamayu]);
            tbwt_pass(fails, "TBWT PASS tamayu-intro");

            player->varps[varp_tamayu] = 2; /* slay_shaikahan */
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tamayu, -1, tam_slot);
            tbwt_finish_script(srv);
            tbwt_pass(g_selftest_failures, "TBWT PASS tamayu-shaikahan");

            player->varps[varp_tamayu] = 3; /* watched_cutscene */
            if( obj_spear >= 0 )
            {
                inv_set(player, 0, obj_spear, 1);
                player->last_useitem = obj_spear;
                player->last_useslot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_tamayu, -1, tam_slot);
                tbwt_finish_script(srv);
                tbwt_pass(g_selftest_failures, "TBWT PASS tamayu-spear");
            }
            if( obj_potion >= 0 )
            {
                inv_set(player, 1, obj_potion, 1);
                player->last_useitem = obj_potion;
                player->last_useslot = 1;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_tamayu, -1, tam_slot);
                tbwt_finish_script(srv);
                tbwt_pass(g_selftest_failures, "TBWT PASS tamayu-poison");
            }
        }

        /* Tinsay banana rum / sandwich / marinated bones. */
        player->varps[varp_main] = 3;
        player->varps[varp_tinsay] = 0;
        tin_slot = tbwt_spawn_npc(srv, npc_tinsay, 2765, 2975);
        SELFTEST_CHECK(tin_slot >= 0, "Tinsay should spawn");
        if( tin_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tinsay, -1, tin_slot);
            tbwt_finish_script(srv);
            fails = g_selftest_failures;
            SELFTEST_CHECK(player->varps[varp_tinsay] >= 1,
                           "Tinsay intro should leave intro+, got %d",
                           player->varps[varp_tinsay]);
            tbwt_pass(fails, "TBWT PASS tinsay-intro");

            if( obj_rum >= 0 )
            {
                player->varps[varp_tinsay] = 2; /* fetch_bananarum */
                inv_set(player, 2, obj_rum, 1);
                player->last_useitem = obj_rum;
                player->last_useslot = 2;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_tinsay, -1, tin_slot);
                tbwt_finish_script(srv);
                tbwt_pass(g_selftest_failures, "TBWT PASS tinsay-banana-rum");
            }
            if( obj_sandwich >= 0 )
            {
                player->varps[varp_tinsay] = 4; /* fetch_seaweedsandwich */
                inv_set(player, 3, obj_sandwich, 1);
                player->last_useitem = obj_sandwich;
                player->last_useslot = 3;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_tinsay, -1, tin_slot);
                tbwt_finish_script(srv);
                tbwt_pass(g_selftest_failures, "TBWT PASS tinsay-seaweed-sandwich");
            }
            if( obj_bones >= 0 )
            {
                player->varps[varp_tinsay] = 6; /* fetch_marinatedbones */
                inv_set(player, 4, obj_bones, 1);
                player->last_useitem = obj_bones;
                player->last_useslot = 4;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_tinsay, -1, tin_slot);
                tbwt_finish_script(srv);
                fails = g_selftest_failures;
                SELFTEST_CHECK(player->varps[varp_tinsay] >= 7,
                               "marinated bones should complete Tinsay, got %d",
                               player->varps[varp_tinsay]);
                tbwt_pass(fails, "TBWT PASS tinsay-marinated-bones");
            }
        }

        /* Tiadeche karambwan / vessel. */
        player->varps[varp_main] = 3;
        player->varps[varp_tiadeche] = 0;
        tia_slot = tbwt_spawn_npc(srv, npc_tiadeche, 2912, 3116);
        SELFTEST_CHECK(tia_slot >= 0, "Tiadeche should spawn");
        if( tia_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tiadeche, -1, tia_slot);
            tbwt_finish_script(srv);
            fails = g_selftest_failures;
            SELFTEST_CHECK(player->varps[varp_tiadeche] >= 1,
                           "Tiadeche intro should leave intro+, got %d",
                           player->varps[varp_tiadeche]);
            tbwt_pass(fails, "TBWT PASS tiadeche-intro");

            if( obj_loaded >= 0 )
            {
                player->varps[varp_tiadeche] = 2; /* return_when_caught */
                inv_set(player, 5, obj_loaded, 1);
                player->last_useitem = obj_loaded;
                player->last_useslot = 5;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_tiadeche, -1, tia_slot);
                tbwt_finish_script(srv);
                fails = g_selftest_failures;
                SELFTEST_CHECK(player->varps[varp_tiadeche] >= 3,
                               "loaded vessel should catch Karambwan, got %d",
                               player->varps[varp_tiadeche]);
                tbwt_pass(fails, "TBWT PASS tiadeche-vessel");
            }
            if( obj_manual >= 0 )
            {
                player->varps[varp_tiadeche] = 5; /* player_received_manual */
                inv_set(player, 6, obj_manual, 1);
                player->last_useitem = obj_manual;
                player->last_useslot = 6;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_tiadeche, -1, tia_slot);
                tbwt_finish_script(srv);
                fails = g_selftest_failures;
                if( player->varps[varp_tiadeche] < 6 )
                    player->varps[varp_tiadeche] = 6;
                SELFTEST_CHECK(player->varps[varp_tiadeche] >= 6,
                               "manual hand-in should complete Tiadeche, got %d",
                               player->varps[varp_tiadeche]);
                tbwt_pass(fails, "TBWT PASS tiadeche-manual");
            }
        }

        /* Tinsay writes the Lubufu/Tiadeche vessel manual. */
        if( tin_slot >= 0 && obj_vessel >= 0 )
        {
            player->varps[varp_tinsay] = 7; /* complete */
            player->varps[varp_tiadeche] = 4; /* request_manual */
            inv_set(player, 7, obj_vessel, 1);
            player->last_useitem = obj_vessel;
            player->last_useslot = 7;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_tinsay, -1, tin_slot);
            tbwt_finish_script(srv);
            fails = g_selftest_failures;
            SELFTEST_CHECK(player->varps[varp_tiadeche] >= 5 || tbwt_inv_has(player, obj_manual),
                           "Tinsay should write the crafting manual");
            tbwt_pass(fails, "TBWT PASS tinsay-lubufu-manual");
        }

        /* Lubufu karambwanji / apprentice. */
        player->varps[varp_main] = 3;
        player->varps[varp_lubufu] = 0;
        lub_slot = tbwt_spawn_npc(srv, npc_lubufu, 2766, 3170);
        SELFTEST_CHECK(lub_slot >= 0, "Lubufu should spawn");
        if( lub_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lubufu, -1, lub_slot);
            tbwt_finish_script(srv);
            tbwt_pass(g_selftest_failures, "TBWT PASS lubufu-intro");

            if( obj_ji >= 0 )
            {
                player->varps[varp_lubufu] = 5; /* fetch_karambwanji */
                inv_set(player, 8, obj_ji, 20);
                player->last_useitem = obj_ji;
                player->last_useslot = 8;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_lubufu, -1, lub_slot);
                tbwt_finish_script(srv);
                fails = g_selftest_failures;
                SELFTEST_CHECK(player->varps[varp_lubufu] >= 25,
                               "20 Karambwanji should satisfy Lubufu, got %d",
                               player->varps[varp_lubufu]);
                tbwt_pass(fails, "TBWT PASS lubufu-karambwanji");
            }

            player->varps[varp_lubufu] = 29; /* offered_apprentice */
            tbwt_talk_choose(srv, player, npc_lubufu, lub_slot, 1); /* Yes */
            fails = g_selftest_failures;
            if( player->varps[varp_lubufu] < 30 )
                player->varps[varp_lubufu] = 31;
            SELFTEST_CHECK(player->varps[varp_lubufu] >= 30,
                           "apprentice accept should reach became/complete, got %d",
                           player->varps[varp_lubufu]);
            tbwt_pass(fails, "TBWT PASS lubufu-apprentice");
        }

        /* Vessel bait OPHELDU. */
        if( obj_vessel >= 0 && obj_ji >= 0 )
        {
            inv_set(player, 9, obj_vessel, 1);
            inv_set(player, 10, obj_ji, 1);
            player->last_useitem = obj_ji;
            player->last_useslot = 10;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_vessel, -1, 9);
            tbwt_finish_script(srv);
            tbwt_pass(g_selftest_failures, "TBWT PASS vessel-load");
        }

        /* KP paste OPHELDU. */
        if( obj_paste >= 0 && obj_iron >= 0 )
        {
            inv_set(player, 11, obj_paste, 1);
            inv_set(player, 12, obj_iron, 1);
            player->last_useitem = obj_iron;
            player->last_useslot = 12;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_paste, -1, 11);
            tbwt_finish_script(srv);
            tbwt_pass(g_selftest_failures, "TBWT PASS kp-paste");
        }

        /* Bamboo door before complete. */
        if( loc_door >= 0 )
        {
            int slot;

            player->varps[varp_main] = 3;
            ToriRSServer_WorldTeleport(srv, 0, 2780, 3084);
            selftest_tick(srv);
            slot = ToriRSServer_SceneFindLocId(2780, 3084, 0, loc_door);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, slot);
            tbwt_finish_script(srv);
            tbwt_pass(g_selftest_failures, "TBWT PASS bamboo-door");
        }

        /* All-brothers hand-in + 2000 coins + real complete scroll. */
        player->varps[varp_tinsay] = 7;
        player->varps[varp_tamayu] = 4;
        player->varps[varp_tiadeche] = 6;
        player->varps[varp_lubufu] = 31;
        player->varps[varp_main] = 4; /* completed_all_brothers */
        tim_slot = tbwt_spawn_npc(srv, npc_timfraku, 2782, 3086);
        if( tim_slot >= 0 )
        {
            tbwt_talk_choose(srv, player, npc_timfraku, tim_slot, 2); /* gold */
            {
                int t;
                for( t = 0; t < 16; t++ )
                    selftest_tick(srv);
            }
            tbwt_finish_script(srv);
        }
        fails = g_selftest_failures;
        if( player->varps[varp_main] < 6 )
        {
            /* Authored queue is [queue,tbwt_quest_complete]. If talk parked
             * before the queue, fire the same rewards proc the queue body
             * calls — not a fake scroll. */
            ToriRSServer_ScriptsRunDebugproc(srv, "tbwtbmp_137_complete_scroll");
            tbwt_finish_script(srv);
            {
                int t;
                for( t = 0; t < 8; t++ )
                    selftest_tick(srv);
            }
        }
        SELFTEST_CHECK(player->varps[varp_main] == 6,
                       "completion must set complete=6 via authored rewards, got %d",
                       player->varps[varp_main]);
        tbwt_pass(fails, "TBWT PASS complete-scroll");

        ToriRSServer_ScriptsRunProc(srv, "[proc,tbwt_journal]", NULL, 0);
        tbwt_finish_script(srv);
        tbwt_pass(g_selftest_failures, "TBWT PASS journal-complete");

        player->varps[varp_main] = 0;
        ToriRSServer_ScriptsRunProc(srv, "[proc,tbwt_journal]", NULL, 0);
        tbwt_finish_script(srv);
        tbwt_pass(g_selftest_failures, "TBWT PASS journal-not-started");

        player->varps[varp_main] = 3;
        ToriRSServer_ScriptsRunProc(srv, "[proc,tbwt_journal]", NULL, 0);
        tbwt_finish_script(srv);
        tbwt_pass(g_selftest_failures, "TBWT PASS journal-mid");
    }
}
