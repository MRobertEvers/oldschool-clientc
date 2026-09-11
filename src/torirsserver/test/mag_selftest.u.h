/* Meat and Greet MAG-only walk. Lives on the worker branch.
 * Guarded by TORIRSSERVER_SELFTEST_MAG_ONLY=1. Placed immediately before
 * the shop fprintf so an unset MAG_ONLY leaves the default suite unmoved. */

static int
selftest_mag_varbit(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
}

static int
selftest_mag_get(
    struct ToriRSServerPlayer* player,
    int bit)
{
    assert(player);
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
selftest_mag_set(
    struct ToriRSServer* srv,
    int bit,
    int value)
{
    assert(srv);
    if( bit < 0 )
        return;
    ToriRSServer_VarbitSet(srv, bit, value);
}

static int
selftest_mag_spawn(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    const char* npc_name)
{
    int type;
    int slot;

    assert(srv);
    assert(player);
    assert(npc_name);
    type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, npc_name);
    SELFTEST_CHECK(type > 0, "npc %s should resolve", npc_name);
    if( type <= 0 )
        return -1;
    slot = ToriRSServer_WorldNpcSpawn(srv, type, player->x + 1, player->z, player->level);
    SELFTEST_CHECK(slot >= 0, "npc %s should spawn next to the player", npc_name);
    return slot;
}

static int
selftest_mag_talk(
    struct ToriRSServer* srv,
    int npc_type,
    int slot)
{
    int ran;

    assert(srv);
    ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1] for type %d should run, got %d", npc_type, ran);
    return ran;
}

static void
selftest_mag_pick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int row)
{
    int chatmenu;
    uint8_t button[6];
    int uid;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    SELFTEST_CHECK(chatmenu > 0, "chatmenu:options should resolve");
    if( player->resume_button_count <= 0 )
        return;
    uid = player->resume_buttons[0];
    SELFTEST_CHECK(uid == chatmenu, "choice should arm chatmenu:options, got %d", uid);
    button[0] = (uint8_t)(uid >> 24);
    button[1] = (uint8_t)(uid >> 16);
    button[2] = (uint8_t)(uid >> 8);
    button[3] = (uint8_t)uid;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static void
selftest_mag(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int mag;
    int vmq1;
    int spice;
    int meat;
    int chatmenu;
    int cooking;
    int qp;
    int emelio_type;
    int spice_type;
    int alba_type;
    int wolf_type;
    int renata_type;
    int lelia_type;
    int mino_type;
    int slot;
    int qp_before;
    int cook_before;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Meat and Greet MAG walk\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "MAG walk loads a compiled script pack");
    if( !loaded )
        return;

    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;

    mag = selftest_mag_varbit("mag");
    vmq1 = selftest_mag_varbit("vmq1");
    spice = selftest_mag_varbit("mag_spice");
    meat = selftest_mag_varbit("mag_meat");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    cooking = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
    qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    emelio_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mag_emelio_1op");
    spice_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fortis_shop_spices");
    alba_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mag_alba");
    wolf_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mag_direwolf");
    renata_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mag_renata_vis");
    lelia_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mag_lelia");
    mino_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mag_minotaur");

    SELFTEST_CHECK(mag > 0, "%%mag should resolve");
    SELFTEST_CHECK(vmq1 > 0, "%%vmq1 should resolve");
    SELFTEST_CHECK(spice > 0 && meat > 0, "%%mag_spice / %%mag_meat should resolve");
    SELFTEST_CHECK(chatmenu > 0, "chatmenu:options should resolve");
    SELFTEST_CHECK(emelio_type > 0, "mag_emelio_1op should resolve");
    SELFTEST_CHECK(spice_type > 0, "fortis_shop_spices should resolve");
    SELFTEST_CHECK(alba_type > 0, "mag_alba should resolve");
    SELFTEST_CHECK(wolf_type > 0, "mag_direwolf should resolve");
    SELFTEST_CHECK(renata_type > 0, "mag_renata_vis should resolve");
    SELFTEST_CHECK(lelia_type > 0, "mag_lelia should resolve");
    SELFTEST_CHECK(mino_type > 0, "mag_minotaur should resolve");
    if( mag <= 0 || vmq1 <= 0 || emelio_type <= 0 )
        return;

    /* ---- CotS qualify fail: named mesbox, %mag unchanged ---- */
    selftest_mag_set(srv, mag, 0);
    selftest_mag_set(srv, vmq1, 0);
    slot = selftest_mag_spawn(srv, player, "mag_emelio_1op");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, emelio_type, slot);
        SELFTEST_CHECK(player->active_script != NULL,
                       "qualify-fail should park on the CotS mesbox");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_mag_get(player, mag) == 0,
                       "qualify-fail must not write %%mag, got %d",
                       selftest_mag_get(player, mag));
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Offer parks on p_choice2 Yes / No ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_mag_set(srv, mag, 0);
    selftest_mag_set(srv, spice, 0);
    selftest_mag_set(srv, meat, 0);
    selftest_mag_set(srv, vmq1, 24);
    slot = selftest_mag_spawn(srv, player, "mag_emelio_1op");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, emelio_type, slot);
        SELFTEST_CHECK(player->active_script != NULL,
                       "Emelio start should park on chat");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(player->active_script != NULL,
                       "qualified start should park on p_choice2");
        SELFTEST_CHECK(player->resume_button_count == 1 &&
                           player->resume_buttons[0] == chatmenu,
                       "offer should arm chatmenu:options");
        SELFTEST_CHECK(selftest_mag_get(player, mag) == 0,
                       "offer itself must not write %%mag, got %d",
                       selftest_mag_get(player, mag));

        /* Refuse: row 2 "No." must chat and leave %mag at 0. */
        selftest_mag_pick(srv, player, 2);
        SELFTEST_CHECK(player->active_script != NULL,
                       "refuse should park on refuse chat");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_mag_get(player, mag) == 0,
                       "refuse must not write %%mag, got %d",
                       selftest_mag_get(player, mag));
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Accept: row 1 "Yes." writes %mag = ^mg_supply (4) ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_mag_set(srv, mag, 0);
    selftest_mag_set(srv, spice, 0);
    selftest_mag_set(srv, meat, 0);
    selftest_mag_set(srv, vmq1, 24);
    slot = selftest_mag_spawn(srv, player, "mag_emelio_1op");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, emelio_type, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(player->resume_button_count == 1 &&
                           player->resume_buttons[0] == chatmenu,
                       "accept path should still be on p_choice2");
        selftest_mag_pick(srv, player, 1);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_mag_get(player, mag) == 4,
                       "Yes must write %%mag = ^mg_supply (4), got %d",
                       selftest_mag_get(player, mag));
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Spice merchant mid-quest hook kept ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_mag_set(srv, mag, 4);
    selftest_mag_set(srv, spice, 0);
    selftest_mag_set(srv, meat, 0);
    selftest_mag_set(srv, vmq1, 24);
    slot = selftest_mag_spawn(srv, player, "fortis_shop_spices");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, spice_type, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_mag_get(player, spice) == 4,
                       "spice shop hook should set %%mag_spice done, got %d",
                       selftest_mag_get(player, spice));
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Alba first talk ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_mag_set(srv, mag, 4);
    selftest_mag_set(srv, spice, 4);
    selftest_mag_set(srv, meat, 0);
    selftest_mag_set(srv, vmq1, 24);
    slot = selftest_mag_spawn(srv, player, "mag_alba");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, alba_type, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_mag_get(player, meat) == 2,
                       "Alba first talk should set %%mag_meat talk (2), got %d",
                       selftest_mag_get(player, meat));
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Direwolf alpha soft-skip ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_mag_set(srv, mag, 4);
    selftest_mag_set(srv, meat, 2);
    selftest_mag_set(srv, vmq1, 24);
    slot = selftest_mag_spawn(srv, player, "mag_direwolf");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, wolf_type, slot);
        selftest_click_through(srv, 4);
        SELFTEST_CHECK(selftest_mag_get(player, meat) == 3,
                       "direwolf hook should set %%mag_meat kill (3), got %d",
                       selftest_mag_get(player, meat));
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Alba after kill ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_mag_set(srv, mag, 4);
    selftest_mag_set(srv, spice, 4);
    selftest_mag_set(srv, meat, 3);
    selftest_mag_set(srv, vmq1, 24);
    slot = selftest_mag_spawn(srv, player, "mag_alba");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, alba_type, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_mag_get(player, meat) == 4,
                       "Alba after kill should set %%mag_meat done (4), got %d",
                       selftest_mag_get(player, meat));
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Emelio after both supplies ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_mag_set(srv, mag, 4);
    selftest_mag_set(srv, spice, 4);
    selftest_mag_set(srv, meat, 4);
    selftest_mag_set(srv, vmq1, 24);
    slot = selftest_mag_spawn(srv, player, "mag_emelio_1op");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, emelio_type, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_mag_get(player, mag) == 6,
                       "supply-done Emelio should write %%mag = ^mg_emelio2 (6), got %d",
                       selftest_mag_get(player, mag));
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Renata with test kebab ---- */
    {
        int kebab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mag_test_kebab");

        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        selftest_mag_set(srv, mag, 8);
        selftest_mag_set(srv, vmq1, 24);
        SELFTEST_CHECK(kebab > 0, "mag_test_kebab should resolve");
        if( kebab > 0 )
        {
            player->inv[0].obj_id = kebab;
            player->inv[0].count = 1;
        }
        slot = selftest_mag_spawn(srv, player, "mag_renata_vis");
        if( slot >= 0 )
        {
            selftest_mag_talk(srv, renata_type, slot);
            selftest_click_through(srv, 8);
            SELFTEST_CHECK(selftest_mag_get(player, mag) == 10,
                           "Renata taste should write %%mag = ^mg_success (10), got %d",
                           selftest_mag_get(player, mag));
            ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }
    }

    /* ---- Lelia kebab hand-in ---- */
    {
        int col_kebab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mag_colosseum_kebab");

        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        selftest_mag_set(srv, mag, 14);
        selftest_mag_set(srv, vmq1, 24);
        if( col_kebab > 0 )
        {
            player->inv[0].obj_id = col_kebab;
            player->inv[0].count = 1;
        }
        slot = selftest_mag_spawn(srv, player, "mag_lelia");
        if( slot >= 0 )
        {
            selftest_mag_talk(srv, lelia_type, slot);
            selftest_click_through(srv, 8);
            SELFTEST_CHECK(selftest_mag_get(player, mag) == 16,
                           "Lelia hand-in should write %%mag = ^mg_lelia2 (16), got %d",
                           selftest_mag_get(player, mag));
            ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }
    }

    /* ---- Minotaur soft-skip ---- */
    {
        int kebab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mag_test_kebab");

        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        selftest_mag_set(srv, mag, 18);
        selftest_mag_set(srv, vmq1, 24);
        if( kebab > 0 )
        {
            player->inv[0].obj_id = kebab;
            player->inv[0].count = 1;
        }
        slot = selftest_mag_spawn(srv, player, "mag_minotaur");
        if( slot >= 0 )
        {
            selftest_mag_talk(srv, mino_type, slot);
            selftest_click_through(srv, 4);
            SELFTEST_CHECK(selftest_mag_get(player, mag) == 22,
                           "minotaur hook should write %%mag = ^mg_lelia3 (22), got %d",
                           selftest_mag_get(player, mag));
            ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }
    }

    /* ---- Complete: 1 QP + 8000 Cooking XP (tenths 80000) ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_mag_set(srv, mag, 24);
    selftest_mag_set(srv, vmq1, 24);
    qp_before = (qp > 0) ? player->varps[qp] : 0;
    cook_before = (cooking >= 0) ? player->stat_xp_tenths[cooking] : 0;
    slot = selftest_mag_spawn(srv, player, "mag_emelio_1op");
    if( slot >= 0 )
    {
        selftest_mag_talk(srv, emelio_type, slot);
        selftest_click_through(srv, 12);
        SELFTEST_CHECK(selftest_mag_get(player, mag) == 26,
                       "finish talk should write %%mag = ^mg_complete (26), got %d",
                       selftest_mag_get(player, mag));
        if( qp > 0 )
            SELFTEST_CHECK(player->varps[qp] == qp_before + 1,
                           "complete should award 1 QP (%d -> %d)",
                           qp_before, player->varps[qp]);
        if( cooking >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[cooking] == cook_before + 80000,
                           "complete should award 80000 cooking tenths (%d -> %d)",
                           cook_before, player->stat_xp_tenths[cooking]);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Journal complete prints QUEST COMPLETE! ---- */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,meatandgreet_journal]", NULL, 0) != 0,
                   "~meatandgreet_journal should run at complete");

    /* ---- Leftover mesboxes (allowed names only) ---- */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,mg_leftover_spice_pinpad_if]", NULL, 0) != 0,
                   "leftover_spice_pinpad_if should run");
    selftest_click_through(srv, 4);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,mg_leftover_direwolf_den_instance]", NULL, 0) != 0,
                   "leftover_direwolf_den_instance should run");
    selftest_click_through(srv, 4);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,mg_leftover_recipe_connoisseur_matrix]", NULL, 0) != 0,
                   "leftover_recipe_connoisseur_matrix should run");
    selftest_click_through(srv, 4);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,mg_leftover_colosseum_minotaur_combat]", NULL, 0) != 0,
                   "leftover_colosseum_minotaur_combat should run");
    selftest_click_through(srv, 4);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,mg_leftover_emelio_shop_unlock_ui]", NULL, 0) != 0,
                   "leftover_emelio_shop_unlock_ui should run");
    selftest_click_through(srv, 4);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,mg_leftover_full_refuse_trees]", NULL, 0) != 0,
                   "leftover_full_refuse_trees should run");
    selftest_click_through(srv, 4);

    /* ---- Debug cheats still exist ---- */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "meatandgreet") != TORIRSSERVER_TRIGGER_NONE,
                   "::meatandgreet should reach [debugproc,meatandgreet]");
    selftest_click_through(srv, 4);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "mgrun") != TORIRSSERVER_TRIGGER_NONE,
                   "::mgrun should reach [debugproc,mgrun]");
    selftest_click_through(srv, 16);

    ToriRSServer_WorldNpcReap(srv);
}
