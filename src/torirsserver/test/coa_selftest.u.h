/* The Curse of Arrav focused walk. Guarded by TORIRSSERVER_SELFTEST_COA_ONLY.
 * Included from torirs_server_world_selftest.c immediately before the shop
 * fprintf. Stays on the worker branch (.u.h is not for v3/main). */

static int
selftest_coa_text_has(
    struct ToriRSServer* srv,
    const struct ToriRSServerCapture* cap,
    const char* needle)
{
    int i;

    assert(srv);
    assert(cap);
    assert(needle);
    for( i = ToriRSServer_CaptureFindNamed(cap, PKT_NAME_IF_SETTEXT, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(cap, PKT_NAME_IF_SETTEXT, i + 1) )
    {
        const char* text = selftest_settext_text(srv, &cap->packets[i]);
        if( text && strstr(text, needle) )
            return 1;
    }
    for( i = ToriRSServer_CaptureFindNamed(cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(cap, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const char* text = selftest_message_text(srv, &cap->packets[i]);
        if( text && strstr(text, needle) )
            return 1;
    }
    return 0;
}

static int
selftest_coa_drain_to_choice(
    struct ToriRSServer* srv,
    int max_pages)
{
    int chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    int clicks = 0;

    assert(srv);
    assert(srv->active_player);
    while( clicks < max_pages && srv->active_player->active_script )
    {
        int uid;
        uint8_t resume[4];

        if( srv->active_player->resume_button_count <= 0 )
            break;
        uid = srv->active_player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            return 1;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(srv->active_player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        clicks++;
    }
    return 0;
}

static void
selftest_coa_pick_row(
    struct ToriRSServer* srv,
    int row)
{
    int chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    uint8_t resume[6];

    assert(srv);
    assert(srv->active_player);
    assert(chatmenu > 0);
    resume[0] = (uint8_t)(chatmenu >> 24);
    resume[1] = (uint8_t)(chatmenu >> 16);
    resume[2] = (uint8_t)(chatmenu >> 8);
    resume[3] = (uint8_t)chatmenu;
    resume[4] = 0;
    resume[5] = (uint8_t)row;
    selftest_handle(srv->active_player, PKTOUT_NAME_IF_BUTTON1, resume, sizeof(resume));
}

static void
selftest_coa_run(
    struct ToriRSServer* srv,
    const char* proc)
{
    assert(srv);
    assert(proc);
    ToriRSServer_WorldCloseModal(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, proc) == TORIRSSERVER_TRIGGER_RAN,
                   "::%s should reach content", proc);
}

static void
selftest_coa(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    static struct ToriRSServerCapture cap;
    int loaded;
    int vb_coa;
    int vb_dov;
    int varp_troll;
    int mining;
    int thieving;
    int ranged;
    int agility;
    int strength;
    int slayer;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: The Curse of Arrav\n");
    player->godmode = 1;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "COA walk loads a compiled script pack");
    if( !loaded )
        return;

    vb_coa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "coa");
    vb_dov = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "dov");
    varp_troll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "troll_love");
    mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    ranged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
    agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    strength = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "strength");
    slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    SELFTEST_CHECK(vb_coa >= 0 && vb_dov >= 0 && varp_troll >= 0 && mining >= 0 &&
                       thieving >= 0 && ranged >= 0 && agility >= 0 && strength >= 0 &&
                       slayer >= 0,
                   "COA symbols resolve (coa=%d dov=%d troll=%d)", vb_coa, vb_dov,
                   varp_troll);
    if( vb_coa < 0 )
        return;

    ToriRSServer_CombatSetLevel(player, mining, 1);
    ToriRSServer_CombatSetLevel(player, thieving, 1);
    ToriRSServer_CombatSetLevel(player, ranged, 1);
    ToriRSServer_CombatSetLevel(player, agility, 1);
    ToriRSServer_CombatSetLevel(player, strength, 1);
    ToriRSServer_CombatSetLevel(player, slayer, 1);
    ToriRSServer_VarbitSet(srv, vb_coa, 0);
    ToriRSServer_VarbitSet(srv, vb_dov, 0);
    if( varp_troll >= 0 )
        player->varps[varp_troll] = 0;

    /* Eight qualify fails: named mesbox, %coa stays 0. */
    {
        static const struct
        {
            const char* proc;
            const char* needle;
        } fails[] = {
            { "coa_bmp_qualify_dov", "Defender of Varrock" },
            { "coa_bmp_qualify_trollromance", "Troll Romance" },
            { "coa_bmp_qualify_mining", "Mining level of 64" },
            { "coa_bmp_qualify_thieving", "Thieving level of 62" },
            { "coa_bmp_qualify_ranged", "Ranged level of 62" },
            { "coa_bmp_qualify_agility", "Agility level of 61" },
            { "coa_bmp_qualify_strength", "Strength level of 58" },
            { "coa_bmp_qualify_slayer", "Slayer level of 37" },
        };
        int i;

        for( i = 0; i < (int)(sizeof(fails) / sizeof(fails[0])); i++ )
        {
            ToriRSServer_VarbitSet(srv, vb_coa, 0);
            ToriRSServer_CaptureBegin(srv, &cap);
            selftest_coa_run(srv, fails[i].proc);
            ToriRSServer_CaptureEnd(srv);
            SELFTEST_CHECK(selftest_coa_text_has(srv, &cap, fails[i].needle),
                           "%s should paint '%s'", fails[i].proc, fails[i].needle);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_coa) == 0,
                           "%s must not write %%coa, got %d", fails[i].proc,
                           ToriRSServer_VarbitGet(player, vb_coa));
            ToriRSServer_WorldCloseModal(srv);
        }
    }

    /* Refuse: chat, %coa unchanged. */
    ToriRSServer_VarbitSet(srv, vb_coa, 0);
    ToriRSServer_CaptureBegin(srv, &cap);
    selftest_coa_run(srv, "coa_bmp_refuse");
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(selftest_coa_text_has(srv, &cap, "I'll have to pass right now"),
                   "refuse should chat the wiki No line");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_coa) == 0,
                   "refuse must not write %%coa, got %d",
                   ToriRSServer_VarbitGet(player, vb_coa));
    ToriRSServer_WorldCloseModal(srv);

    /* Offer p_choice2: drain to Yes/No, still not started, No keeps 0. */
    ToriRSServer_VarbitSet(srv, vb_coa, 0);
    ToriRSServer_CombatSetLevel(player, mining, 64);
    ToriRSServer_CombatSetLevel(player, thieving, 62);
    ToriRSServer_CombatSetLevel(player, ranged, 62);
    ToriRSServer_CombatSetLevel(player, agility, 61);
    ToriRSServer_CombatSetLevel(player, strength, 58);
    ToriRSServer_CombatSetLevel(player, slayer, 37);
    ToriRSServer_VarbitSet(srv, vb_dov, 56);
    if( varp_troll >= 0 )
        player->varps[varp_troll] = 45;
    ToriRSServer_CaptureBegin(srv, &cap);
    selftest_coa_run(srv, "coa_bmp_offer");
    SELFTEST_CHECK(selftest_coa_drain_to_choice(srv, 24),
                   "start path should park on p_choice2 Yes/No");
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(selftest_coa_text_has(srv, &cap, "Yes.") ||
                       selftest_coa_text_has(srv, &cap, "mastaba"),
                   "offer should show the Yes/No start or the mastaba briefing");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_coa) == 0,
                   "p_choice2 must not write %%coa before Yes, got %d",
                   ToriRSServer_VarbitGet(player, vb_coa));
    selftest_coa_pick_row(srv, 2);
    selftest_click_through(srv, 8);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_coa) == 0,
                   "picking No must not write %%coa, got %d",
                   ToriRSServer_VarbitGet(player, vb_coa));
    ToriRSServer_WorldCloseModal(srv);

    /* Accept writes first started state ^coa_tomb = 4. */
    ToriRSServer_VarbitSet(srv, vb_coa, 0);
    ToriRSServer_CaptureBegin(srv, &cap);
    selftest_coa_run(srv, "coa_bmp_accept");
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(selftest_coa_text_has(srv, &cap, "Sounds like a job for me"),
                   "accept should chat the wiki Yes line");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_coa) == 4,
                   "accept should write %%coa=^coa_tomb (4), got %d",
                   ToriRSServer_VarbitGet(player, vb_coa));
    ToriRSServer_WorldCloseModal(srv);

    /* Mid-quest talks / loc beats already in curseofarrav.rs2. */
    {
        static const struct
        {
            const char* proc;
            const char* needle;
        } mid[] = {
            { "coa_bmp_elias_tomb", "Any progress?" },
            { "coa_bmp_elias_canopic", "canopic jar" },
            { "coa_bmp_elias_trollweiss", "Trollweiss" },
            { "coa_bmp_elias_plans", "Interpret the plans" },
            { "coa_bmp_elias_heist", "Any progress?" },
            { "coa_bmp_elias_victory", "Return when you are ready" },
            { "coa_bmp_elias_complete", "Arrav walks free" },
            { "coa_bmp_mastaba_entry", "enter the mastaba" },
            { "coa_bmp_golem", "defeat the golem" },
            { "coa_bmp_tile_puzzle", "tile puzzle" },
            { "coa_bmp_canopic", "canopic jar" },
            { "coa_bmp_mastaba_doors", "imposing doors" },
            { "coa_bmp_trollweiss", "Trollweiss caves" },
            { "coa_bmp_fort", "Zemouregal's fort" },
            { "coa_bmp_arrav_key", "Steal the base key" },
            { "coa_bmp_arrav_confront", "confrontation with Arrav" },
            { "coa_bmp_arrav_idle", "..." },
            { "coa_bmp_base_heist", "base doors" },
            { "coa_bmp_heart", "steal the heart" },
        };
        int i;

        for( i = 0; i < (int)(sizeof(mid) / sizeof(mid[0])); i++ )
        {
            ToriRSServer_CaptureBegin(srv, &cap);
            selftest_coa_run(srv, mid[i].proc);
            ToriRSServer_CaptureEnd(srv);
            SELFTEST_CHECK(selftest_coa_text_has(srv, &cap, mid[i].needle),
                           "%s should paint '%s'", mid[i].proc, mid[i].needle);
            ToriRSServer_WorldCloseModal(srv);
        }
    }

    /* Complete scroll: 2 QP, 40000 Mining / Thieving / Agility, fort access. */
    {
        int mine_before = player->stat_xp_tenths[mining];
        int thieve_before = player->stat_xp_tenths[thieving];
        int agil_before = player->stat_xp_tenths[agility];

        ToriRSServer_VarbitSet(srv, vb_coa, 58);
        ToriRSServer_CaptureBegin(srv, &cap);
        selftest_coa_run(srv, "coa_bmp_complete");
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_coa) == 60,
                       "complete should write %%coa=60, got %d",
                       ToriRSServer_VarbitGet(player, vb_coa));
        SELFTEST_CHECK(player->stat_xp_tenths[mining] >= mine_before + 400000,
                       "complete should award 40000 Mining XP tenths");
        SELFTEST_CHECK(player->stat_xp_tenths[thieving] >= thieve_before + 400000,
                       "complete should award 40000 Thieving XP tenths");
        SELFTEST_CHECK(player->stat_xp_tenths[agility] >= agil_before + 400000,
                       "complete should award 40000 Agility XP tenths");
        SELFTEST_CHECK(selftest_coa_text_has(srv, &cap, "40000") ||
                           selftest_coa_text_has(srv, &cap, "Mining") ||
                           selftest_coa_text_has(srv, &cap, "Zemouregal"),
                       "complete scroll should name the XP or fort reward");
        ToriRSServer_WorldCloseModal(srv);
    }

    /* Journal ladder, including QUEST COMPLETE! */
    {
        static const struct
        {
            const char* proc;
            const char* needle;
        } journals[] = {
            { "coa_bmp_journal_not_started", "Elias" },
            { "coa_bmp_journal_tomb", "mastaba" },
            { "coa_bmp_journal_fort", "Trollweiss" },
            { "coa_bmp_journal_heist", "heist" },
            { "coa_bmp_journal_complete", "QUEST COMPLETE!" },
        };
        int i;

        for( i = 0; i < (int)(sizeof(journals) / sizeof(journals[0])); i++ )
        {
            ToriRSServer_CaptureBegin(srv, &cap);
            selftest_coa_run(srv, journals[i].proc);
            ToriRSServer_CaptureEnd(srv);
            SELFTEST_CHECK(selftest_coa_text_has(srv, &cap, journals[i].needle),
                           "%s should paint '%s'", journals[i].proc, journals[i].needle);
            ToriRSServer_WorldCloseModal(srv);
        }
    }

    /* Allowed leftover stamps. */
    {
        static const struct
        {
            const char* proc;
            const char* needle;
        } leftovers[] = {
            { "coa_bmp_leftover_mastaba_levers_spear_traps", "levers and spear traps" },
            { "coa_bmp_leftover_golem_combat", "golem combat" },
            { "coa_bmp_leftover_tile_puzzle", "tile puzzle" },
            { "coa_bmp_leftover_canopic_fill_reagents", "canopic jar fill" },
            { "coa_bmp_leftover_trollweiss_cave_pathing", "Trollweiss cave pathing" },
            { "coa_bmp_leftover_fort_heist", "fort heist" },
            { "coa_bmp_leftover_base_metal_door", "metal door" },
            { "coa_bmp_leftover_arrav_fight", "Arrav fight" },
            { "coa_bmp_leftover_full_refuse_trees", "refuse trees" },
        };
        int i;

        for( i = 0; i < (int)(sizeof(leftovers) / sizeof(leftovers[0])); i++ )
        {
            ToriRSServer_CaptureBegin(srv, &cap);
            selftest_coa_run(srv, leftovers[i].proc);
            ToriRSServer_CaptureEnd(srv);
            SELFTEST_CHECK(selftest_coa_text_has(srv, &cap, leftovers[i].needle),
                           "%s should paint leftover '%s'", leftovers[i].proc,
                           leftovers[i].needle);
            ToriRSServer_WorldCloseModal(srv);
        }
    }

    /* Existing headless walk still completes. */
    {
        int said_ok = 0;

        ToriRSServer_VarbitSet(srv, vb_coa, 0);
        ToriRSServer_CaptureBegin(srv, &cap);
        selftest_coa_run(srv, "coarun");
        ToriRSServer_CaptureEnd(srv);
        said_ok = selftest_coa_text_has(srv, &cap, "coarun OK");
        SELFTEST_CHECK(said_ok || ToriRSServer_VarbitGet(player, vb_coa) == 60,
                       "::coarun should complete, %%coa=%d",
                       ToriRSServer_VarbitGet(player, vb_coa));
        ToriRSServer_WorldCloseModal(srv);
    }

    /* DoV splice: %coa==0 must not steal elias_white_vis. */
    {
        int npc_elias = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elias_white_vis");
        int slot;

        ToriRSServer_VarbitSet(srv, vb_coa, 0);
        ToriRSServer_VarbitSet(srv, vb_dov, 56);
        if( npc_elias >= 0 )
        {
            slot = ToriRSServer_WorldNpcSpawn(srv, npc_elias, 3222, 3218, 0);
            if( slot >= 0 )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_elias, -1, slot);
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_coa) == 0,
                               "elias_white_vis at not-started must not write %%coa");
                ToriRSServer_WorldCloseModal(srv);
                ToriRSServer_WorldNpcFree(srv, slot);
                ToriRSServer_WorldNpcReap(srv);
            }
        }
    }

    ToriRSServer_ScriptsFree(srv);
}
