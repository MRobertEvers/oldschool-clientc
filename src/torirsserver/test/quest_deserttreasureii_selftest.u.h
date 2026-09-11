/* Desert Treasure II C-walk. Included immediately before the shop stanza.
 * Do not merge this file onto parent v3.
 *
 * Gate: TORIRSSERVER_SELFTEST_DT2_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0
 * DT2_ONLY (not GOD_ONLY) so it does not collide with TORIRSSERVER_GOD=1.
 */
selftest_dt2_only:;
{
    int dt2_only = getenv("TORIRSSERVER_SELFTEST_DT2_ONLY") != NULL;
    int loaded;
    int vb_dt2;
    int vb_dt1;
    int vb_sotn;
    int vb_bcs;
    int vb_mend2;
    int npc_asgarnia;
    int npc_banikan;
    int npc_guardian;
    int npc_azzanadra;
    int loc_vault;
    int loc_plaque;
    int loc_winch;
    int loc_crevice;
    int loc_altar;
    int obj_ring;
    int obj_lamp;
    int obj_med_v;
    int obj_med_w;
    int obj_med_s;
    int obj_med_p;
    int obj_coins;
    int stat_fm;
    int stat_magic;
    int stat_thieve;
    int stat_hunt;
    int stat_mine;
    int if_gold;
    int if_silver;
    int if_flute;
    int spell_telegrab;
    int com_chatmenu;
    int32_t why;
    int slot;
    int s;
    int row;

    fprintf(stderr, "ToriRSServer selftest: Desert Treasure II\n");
    assert(srv);
    assert(player);
    player->godmode = 1;
    ToriRSServer_WorldSetActive(srv, player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Desert Treasure II C-walk needs a compiled script pack");
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        if( dt2_only )
        {
            fprintf(stderr, "ToriRSServer dt2 selftest: %lu checks, %d failures\n",
                    g_selftest_checks, g_selftest_failures);
            selftest_evidence_end("dt2");
            return g_selftest_failures;
        }
        goto dt2_walk_done;
    }

    vb_dt2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "dt2");
    vb_dt1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "deserttreasure");
    vb_sotn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "sotn");
    vb_bcs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bcs");
    vb_mend2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mourning_quest_main");
    npc_asgarnia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dt2_asgarnia_smith_vis");
    npc_banikan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dt2_banikan_vis");
    npc_guardian = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dt2_ancient_guardian");
    npc_azzanadra = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dt2_azzanadra_vis");
    loc_vault = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dt2_desert_vault_door");
    loc_plaque = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dt2_vault_plaque");
    loc_winch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dt2_digsite_winch");
    loc_crevice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dt2_digsite_crevice");
    loc_altar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dt2_war_room_altar");
    obj_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ring_of_shadows");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dt2_reward_lamp");
    obj_med_v = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dt2_medallion_vardorvis");
    obj_med_w = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dt2_medallion_whisperer");
    obj_med_s = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dt2_medallion_sucellus");
    obj_med_p = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dt2_medallion_perseriya");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    stat_thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    stat_hunt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    if_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "crafting_gold");
    if_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "silver_crafting");
    if_flute = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "ratcatcher_flute");
    spell_telegrab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lawrune");
    if( spell_telegrab < 0 )
        spell_telegrab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "airrune");
    com_chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(vb_dt2 >= 0 && npc_asgarnia >= 0 && loc_vault >= 0 &&
                       stat_fm >= 0 && stat_magic >= 0 && stat_thieve >= 0 &&
                       stat_hunt >= 0 && stat_mine >= 0,
                   "Desert Treasure II cache names should resolve");
    SELFTEST_CHECK(if_gold >= 0, "jewellery furnace IF crafting_gold is present");
    SELFTEST_CHECK(if_silver >= 0, "jewellery furnace IF silver_crafting is present");
    SELFTEST_CHECK(if_flute == 282, "full flute widget is interface 282, got %d", if_flute);
    SELFTEST_CHECK(spell_telegrab >= 0, "Telekinetic Grab ingredients resolve");
    {
        int32_t day = -1;
        int date_ok = ToriRSServer_ScriptsRunProcInt(
            srv, "[proc,selftest_date_runeday]", NULL, 0, &day);
        SELFTEST_CHECK(date_ok && day >= 0, "date_runeday() answers, got %d ok=%d", day, date_ok);
    }

    if( vb_dt2 < 0 || npc_asgarnia < 0 || loc_vault < 0 || stat_fm < 0 )
        goto dt2_cleanup;

    selftest_clear_inv(player);
    ToriRSServer_VarbitSet(srv, vb_dt2, 0);
    if( vb_dt1 >= 0 )
        ToriRSServer_VarbitSet(srv, vb_dt1, 0);
    if( vb_sotn >= 0 )
        ToriRSServer_VarbitSet(srv, vb_sotn, 0);
    if( vb_bcs >= 0 )
        ToriRSServer_VarbitSet(srv, vb_bcs, 0);
    if( vb_mend2 >= 0 )
        ToriRSServer_VarbitSet(srv, vb_mend2, 0);
    ToriRSServer_CombatSetLevel(player, stat_fm, 1);
    ToriRSServer_CombatSetLevel(player, stat_magic, 1);
    ToriRSServer_CombatSetLevel(player, stat_thieve, 1);
    ToriRSServer_CombatSetLevel(player, stat_hunt, 1);
    ToriRSServer_CombatSetLevel(player, stat_mine, 1);
    player->stat_boosted[stat_fm] = 99;

    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 1,
                   "missing Desert Treasure I fails qualify first, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    if( vb_dt1 >= 0 )
        ToriRSServer_VarbitSet(srv, vb_dt1, 15);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 2,
                   "missing Secrets of the North fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    if( vb_sotn >= 0 )
        ToriRSServer_VarbitSet(srv, vb_sotn, 90);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 3,
                   "missing Beneath Cursed Sands fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    if( vb_bcs >= 0 )
        ToriRSServer_VarbitSet(srv, vb_bcs, 108);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 4,
                   "missing Mourning's End Part II fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    if( vb_mend2 >= 0 )
        ToriRSServer_VarbitSet(srv, vb_mend2, 60);
    player->stat_boosted[stat_fm] = 99;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 5,
                   "Firemaking 1 (boosted 99) fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    ToriRSServer_CombatSetLevel(player, stat_fm, 75);
    player->stat_boosted[stat_fm] = 75;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 6,
                   "Magic 1 fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    ToriRSServer_CombatSetLevel(player, stat_magic, 75);
    player->stat_boosted[stat_magic] = 75;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 7,
                   "Thieving 1 fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    ToriRSServer_CombatSetLevel(player, stat_thieve, 70);
    player->stat_boosted[stat_thieve] = 70;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 8,
                   "Hunter 1 fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    ToriRSServer_CombatSetLevel(player, stat_hunt, 62);
    player->stat_boosted[stat_hunt] = 62;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 9,
                   "Mining 1 fails qualify, got %d", why);
    ToriRSServer_WorldCloseModal(srv);

    ToriRSServer_CombatSetLevel(player, stat_mine, 60);
    player->stat_boosted[stat_mine] = 60;
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,dt2_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 0,
                   "all direct gates qualify, got %d", why);

    /* Vault door must not start the quest. */
    ToriRSServer_WorldTeleport(srv, 0, 3511, 2971);
    selftest_tick(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_vault, -1, -1);
    selftest_click_through(srv, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 0,
                   "vault door must not start the quest, dt2=%d",
                   ToriRSServer_VarbitGet(player, vb_dt2));

    slot = npc_spawn(srv, npc_asgarnia, player->x + 1, player->z + 1, player->level);
    SELFTEST_CHECK(slot >= 0, "Asgarnia Smith should spawn for the start talk");
    if( slot < 0 )
        goto dt2_cleanup;

    /* Refuse: Yes/Not now must not write %dt2. */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_asgarnia, -1, slot);
    selftest_click_through(srv, 6);
    if( com_chatmenu > 0 && player->active_script && player->resume_button_count > 0 )
    {
        uint8_t resume[6];
        int uid = player->resume_buttons[0];
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        resume[4] = 0;
        resume[5] = 2; /* Not now. */
        selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, resume, sizeof(resume));
    }
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 0,
                   "refusing Asgarnia must leave dt2 at 0, got %d",
                   ToriRSServer_VarbitGet(player, vb_dt2));

    /* Accept writes ^dt2_asgarnia = 4. */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_asgarnia, -1, slot);
    selftest_click_through(srv, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 4,
                   "accepting Asgarnia writes ^dt2_asgarnia=4, got %d",
                   ToriRSServer_VarbitGet(player, vb_dt2));

    /* Mid Asgarnia: inspect nudge writes 6. */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_asgarnia, -1, slot);
    selftest_click_through(srv, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 6,
                   "Asgarnia inspect nudge writes 6, got %d",
                   ToriRSServer_VarbitGet(player, vb_dt2));

    if( loc_plaque >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_plaque, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 8,
                       "inspecting the plaque writes 8, got %d",
                       ToriRSServer_VarbitGet(player, vb_dt2));
    }

    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_asgarnia, -1, slot);
    selftest_click_through(srv, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 10,
                   "Asgarnia sends the player to Balando (10), got %d",
                   ToriRSServer_VarbitGet(player, vb_dt2));

    ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_balando_talk]", NULL, 0);
    selftest_click_through(srv, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 12,
                   "Balando writes ^dt2_banikan=12, got %d",
                   ToriRSServer_VarbitGet(player, vb_dt2));

    if( npc_banikan >= 0 )
    {
        int ban = npc_spawn(srv, npc_banikan, player->x + 2, player->z + 1, player->level);
        if( ban >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_banikan, -1, ban);
            selftest_click_through(srv, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 18,
                           "Banikan pickaxe talk writes 18, got %d",
                           ToriRSServer_VarbitGet(player, vb_dt2));
            if( loc_crevice >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crevice, -1, -1);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                player->active_script = NULL;
                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 20,
                               "clearing the crevice writes 20, got %d",
                               ToriRSServer_VarbitGet(player, vb_dt2));
            }
            if( npc_guardian >= 0 )
            {
                int gslot = npc_spawn(srv, npc_guardian, player->x + 3, player->z + 1,
                                      player->level);
                if( gslot >= 0 )
                {
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guardian, -1, gslot);
                    selftest_click_through(srv, 8);
                    ToriRSServer_WorldCloseModal(srv);
                    player->active_script = NULL;
                    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 24,
                                   "guardian skip writes 24, got %d",
                                   ToriRSServer_VarbitGet(player, vb_dt2));
                    ToriRSServer_WorldNpcFree(srv, gslot);
                }
            }
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_banikan, -1, ban);
            selftest_click_through(srv, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 28,
                           "Banikan altar talk writes 28, got %d",
                           ToriRSServer_VarbitGet(player, vb_dt2));
            ToriRSServer_WorldNpcFree(srv, ban);
        }
    }

    if( loc_altar >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_altar, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 32,
                       "war-room first click writes 32, got %d",
                       ToriRSServer_VarbitGet(player, vb_dt2));
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_altar, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_altar, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_altar, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_altar, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 42,
                       "war-room soft-skip reaches ^dt2_four=42, got %d",
                       ToriRSServer_VarbitGet(player, vb_dt2));
    }

    if( obj_med_v >= 0 && obj_med_w >= 0 && obj_med_s >= 0 && obj_med_p >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_dt2, 42);
        selftest_give(player, obj_med_v, 1);
        selftest_give(player, obj_med_w, 1);
        selftest_give(player, obj_med_s, 1);
        selftest_give(player, obj_med_p, 1);
        ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_try_finish_four]", NULL, 0);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 86,
                       "four medallions write ^dt2_medallions=86, got %d",
                       ToriRSServer_VarbitGet(player, vb_dt2));
    }

    if( npc_azzanadra >= 0 )
    {
        int az = npc_spawn(srv, npc_azzanadra, player->x + 1, player->z + 2, player->level);
        if( az >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_azzanadra, -1, az);
            selftest_click_through(srv, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 88,
                           "return with medallions writes 88, got %d",
                           ToriRSServer_VarbitGet(player, vb_dt2));
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_azzanadra, -1, az);
            selftest_click_through(srv, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_azzanadra, -1, az);
            selftest_click_through(srv, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            if( loc_plaque >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_plaque, -1, -1);
                selftest_click_through(srv, 8);
                ToriRSServer_WorldCloseModal(srv);
                player->active_script = NULL;
            }
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_azzanadra, -1, az);
            selftest_click_through(srv, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            ToriRSServer_VarbitSet(srv, vb_dt2, 100);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_azzanadra, -1, az);
            selftest_click_through(srv, 16);
            for( row = 0; row < 40 && ToriRSServer_VarbitGet(player, vb_dt2) != 118; row++ )
            {
                ToriRSServer_WorldCloseModal(srv);
                selftest_tick(srv);
            }
            player->active_script = NULL;
            ToriRSServer_WorldNpcFree(srv, az);
        }
    }
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 118,
                   "Azzanadra completes at 118, got %d",
                   ToriRSServer_VarbitGet(player, vb_dt2));
    if( obj_ring >= 0 )
        SELFTEST_CHECK(selftest_count(player, obj_ring) >= 1, "completion grants Ring of shadows");
    if( obj_lamp >= 0 )
        SELFTEST_CHECK(selftest_count(player, obj_lamp) >= 3, "completion grants 3 reward lamps");
    SELFTEST_CHECK(obj_coins >= 0, "coins icon for ~quest_complete_rewards resolves");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_digsite_war_room_puzzle]", NULL, 0),
                   "leftover_digsite_war_room_puzzle runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_vardorvis_path]", NULL, 0),
                   "leftover_vardorvis_path runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_whisperer_path]", NULL, 0),
                   "leftover_whisperer_path runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_sucellus_path]", NULL, 0),
                   "leftover_sucellus_path runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_leviathan_path]", NULL, 0),
                   "leftover_leviathan_path runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_cell_escape]", NULL, 0),
                   "leftover_cell_escape runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_stranger_wights]", NULL, 0),
                   "leftover_stranger_wights runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_full_refuse_trees]", NULL, 0),
                   "leftover_full_refuse_trees runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_golem_fight]", NULL, 0),
                   "leftover_golem_fight runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,dt2_leftover_lamp_rub_ui]", NULL, 0),
                   "leftover_lamp_rub_ui runs");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    if( loc_winch >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_dt2, 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_winch, -1, -1);
        selftest_click_through(srv, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 0,
                       "winch before start must not write dt2, got %d",
                       ToriRSServer_VarbitGet(player, vb_dt2));
    }

    {
        static struct ToriRSServerCapture dt2run_capture;
        int said_ok = 0;
        ToriRSServer_VarbitSet(srv, vb_dt2, 0);
        selftest_clear_inv(player);
        ToriRSServer_CaptureBegin(srv, &dt2run_capture);
        ToriRSServer_ScriptsRunDebugproc(srv, "dt2run");
        selftest_click_through(srv, 40);
        for( row = 0; row < 40 && ToriRSServer_VarbitGet(player, vb_dt2) != 118; row++ )
        {
            ToriRSServer_WorldCloseModal(srv);
            selftest_tick(srv);
        }
        ToriRSServer_CaptureEnd(srv);
        for( s = ToriRSServer_CaptureFindNamed(&dt2run_capture, PKT_NAME_MESSAGE_GAME, 0); s >= 0;
             s = ToriRSServer_CaptureFindNamed(&dt2run_capture, PKT_NAME_MESSAGE_GAME, s + 1) )
        {
            const struct ToriRSServerCapturedPacket* packet = &dt2run_capture.packets[s];
            const char* text = selftest_message_text(srv, packet);
            if( text && strstr(text, "dt2run OK") != NULL )
                said_ok = 1;
        }
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_dt2) == 118 || said_ok,
                       "::dt2run should complete, dt2=%d ok=%d",
                       ToriRSServer_VarbitGet(player, vb_dt2), said_ok);
    }

    SELFTEST_CHECK(player->godmode == 1, "player stays unkillable for the whole walk");
    ToriRSServer_WorldNpcFree(srv, slot);
    ToriRSServer_WorldNpcReap(srv);

dt2_cleanup:
    selftest_clear_inv(player);
    if( vb_dt2 >= 0 )
        ToriRSServer_VarbitSet(srv, vb_dt2, 0);
    player->active_script = NULL;
    ToriRSServer_WorldCloseModal(srv);

dt2_walk_done:
    if( dt2_only )
    {
        fprintf(stderr, "ToriRSServer dt2 selftest: %lu checks, %d failures\n",
                g_selftest_checks, g_selftest_failures);
        selftest_evidence_end("dt2");
        return g_selftest_failures;
    }
}
