/* While Guthix Sleeps Gate D stanza. Included from
 * torirs_server_world_selftest.c. Call immediately before the shop
 * selftest_reset_world so spawned npcs cannot leak into later RNG-gated
 * checks.
 *
 * Qualify is split into named mesboxes. Start is Ivy Sophista with
 * p_choice2 Yes / Not now. Refuse must not write %wgs. Player is
 * unkillable (godmode) unless a death case -- this walk has none.
 */
static void
wgs_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "WGS PASS: %s\n", step);
}

static void
wgs_close(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
    player->delayed_until = 0;
    player->chatmodal_group = 0;
}

static void
wgs_drain_dialogue(struct ToriRSServer* srv, int max_clicks)
{
    struct ToriRSServerPlayer* player;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    clicks = 0;
    while( clicks < max_clicks && player->active_script )
    {
        if( selftest_click_through(srv, 1) <= 0 )
        {
            selftest_tick(srv);
            if( player->resume_button_count <= 0 )
                break;
        }
        clicks++;
    }
}

static void
wgs_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
wgs_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
wgs_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = wgs_chatmenu();
    if( chatmenu <= 0 || !player->active_script || player->resume_button_count <= 0 )
        return;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
}

static void
wgs_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = wgs_chatmenu();
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

static int
wgs_parked(struct ToriRSServerPlayer* player)
{
    assert(player);
    return player->active_script != NULL || player->chatmodal_group != 0;
}

static void
wgs_run_park(struct ToriRSServer* srv, const char* cheat, const char* step)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(cheat);
    assert(step);
    player = srv->active_player;
    assert(player);
    player->chatmodal_group = 0;
    ToriRSServer_ScriptsRunDebugproc(srv, cheat);
    SELFTEST_CHECK(wgs_parked(player),
                   "%s should park on a mesbox / chathead / p_choice / journal",
                   step);
    wgs_pass(step);
    wgs_close(srv);
}

static void
selftest_quest_whileguthixsleeps(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int vb_wgs;
    int vp_qp;
    int vp_td;
    int npc_ivy;
    int npc_thaerisk;
    int npc_akrisae;
    int npc_idria_done;
    int stat_thieving;
    int stat_attack;
    int stat_strength;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: While Guthix Sleeps\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    vb_wgs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "wgs");
    vp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    vp_td = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "rs2012_wgs_complete");
    npc_ivy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wgs_ivy_sophista");
    npc_thaerisk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wgs_thaerisk_cemphier");
    npc_akrisae = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wgs_akrisae");
    npc_idria_done = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wgs_idria_temple_done");
    stat_thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    stat_attack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
    stat_strength = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "strength");

    SELFTEST_CHECK(vb_wgs >= 0 && npc_ivy >= 0 && npc_thaerisk >= 0 &&
                       npc_akrisae >= 0 && npc_idria_done >= 0,
                   "While Guthix Sleeps C-side names should resolve");
    if( vb_wgs < 0 || npc_ivy < 0 )
    {
        ToriRSServer_ScriptsFree(srv);
        return;
    }

    wgs_god(player);
    ToriRSServer_VarbitSet(srv, vb_wgs, 0);
    if( vp_td >= 0 )
        player->varps[vp_td] = 0;

    /* ---- Named qualify mesboxes (before writing %wgs) ---- */
    wgs_run_park(srv, "wgsbmp_01_qualify_fail_dov", "qualify_fail_dov");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_wgs) == 0,
                   "DoV qualify fail must not write %%wgs, got %d",
                   ToriRSServer_VarbitGet(player, vb_wgs));
    wgs_run_park(srv, "wgsbmp_02_qualify_fail_pog", "qualify_fail_pog");
    wgs_run_park(srv, "wgsbmp_03_qualify_fail_arena", "qualify_fail_arena");
    wgs_run_park(srv, "wgsbmp_04_qualify_fail_dreammentor", "qualify_fail_dreammentor");
    wgs_run_park(srv, "wgsbmp_05_qualify_fail_handsand", "qualify_fail_handsand");
    wgs_run_park(srv, "wgsbmp_06_qualify_fail_wanted", "qualify_fail_wanted");
    wgs_run_park(srv, "wgsbmp_07_qualify_fail_tote", "qualify_fail_tote");
    wgs_run_park(srv, "wgsbmp_08_qualify_fail_tog", "qualify_fail_tog");
    wgs_run_park(srv, "wgsbmp_09_qualify_fail_naturespirit", "qualify_fail_naturespirit");
    wgs_run_park(srv, "wgsbmp_10_qualify_fail_thieving", "qualify_fail_thieving");
    wgs_run_park(srv, "wgsbmp_11_qualify_fail_magic", "qualify_fail_magic");
    wgs_run_park(srv, "wgsbmp_12_qualify_fail_agility", "qualify_fail_agility");
    wgs_run_park(srv, "wgsbmp_13_qualify_fail_herblore", "qualify_fail_herblore");
    wgs_run_park(srv, "wgsbmp_14_qualify_fail_farming", "qualify_fail_farming");
    wgs_run_park(srv, "wgsbmp_15_qualify_fail_hunter", "qualify_fail_hunter");
    wgs_run_park(srv, "wgsbmp_16_qualify_fail_questpoints", "qualify_fail_questpoints");
    wgs_run_park(srv, "wgsbmp_17_qualify_fail_warriorsguild", "qualify_fail_warriorsguild");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_wgs) == 0,
                   "no qualify fail may write %%wgs, got %d",
                   ToriRSServer_VarbitGet(player, vb_wgs));
    wgs_pass("qualify_split_named_mesboxes");

    /* ---- Offer / refuse / accept via real OPNPC1 ---- */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "wgsbmp_ready") != 0,
                   "wgsbmp_ready should grant the authored start gates");
    wgs_close(srv);
    ToriRSServer_VarbitSet(srv, vb_wgs, 0);
    ToriRSServer_WorldTeleport(srv, 0, 2907, 3450);
    selftest_tick(srv);
    {
        int ivy_slot = ToriRSServer_WorldNpcSpawn(srv, npc_ivy, 2908, 3450, 0);

        SELFTEST_CHECK(ivy_slot >= 0, "Ivy Sophista should spawn for the start talk");
        if( ivy_slot >= 0 )
        {
            player->chatmodal_group = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ivy, -1, ivy_slot);
            SELFTEST_CHECK(wgs_parked(player),
                           "opnpc1 Ivy with prereqs should open the offer tree");
            wgs_click_until_menu(srv, 12);
            wgs_pick_row(srv, 2); /* Not now. */
            wgs_drain_dialogue(srv, 12);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_wgs) == 0,
                           "refusing Ivy must not write %%wgs, got %d",
                           ToriRSServer_VarbitGet(player, vb_wgs));
            wgs_close(srv);
            wgs_pass("ivy_refuse_no_wgs");

            player->chatmodal_group = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ivy, -1, ivy_slot);
            wgs_click_until_menu(srv, 12);
            wgs_pick_row(srv, 1); /* Yes. */
            wgs_drain_dialogue(srv, 12);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_wgs) == 2,
                           "accepting Ivy should write %%wgs investigating (2), got %d",
                           ToriRSServer_VarbitGet(player, vb_wgs));
            wgs_pass("ivy_accept_writes_investigating");

            ToriRSServer_WorldNpcFree(srv, ivy_slot);
        }
    }

    /* ---- Mid-quest parks + leftovers + complete ---- */
    wgs_run_park(srv, "wgsbmp_19_ivy_offer_p_choice2", "ivy_offer_p_choice2");
    wgs_run_park(srv, "wgsbmp_27_thaerisk_assassin_ambush", "thaerisk_assassin_ambush");
    wgs_run_park(srv, "wgsbmp_29_thaerisk_broav_brief", "thaerisk_broav_brief");
    wgs_run_park(srv, "wgsbmp_36_table_search", "table_search");
    wgs_run_park(srv, "wgsbmp_39_akrisae_orb_brief", "akrisae_orb_brief");
    wgs_run_park(srv, "wgsbmp_53_idria_serum", "idria_serum");
    wgs_run_park(srv, "wgsbmp_59_cell_true_terror", "cell_true_terror");
    wgs_run_park(srv, "wgsbmp_66_stone_elemental", "stone_elemental");
    wgs_run_park(srv, "wgsbmp_76_complete_scroll", "complete_scroll");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_wgs) == 900,
                   "complete scroll should leave %%wgs at 900, got %d",
                   ToriRSServer_VarbitGet(player, vb_wgs));
    if( vp_td >= 0 )
        SELFTEST_CHECK(player->varps[vp_td] == 1,
                       "complete must set %%rs2012_wgs_complete = 1, got %d",
                       player->varps[vp_td]);
    wgs_pass("complete_900_and_td_gate");

    wgs_run_park(srv, "wgsbmp_journal_00_not_started", "journal_00_not_started");
    wgs_run_park(srv, "wgsbmp_journal_900_complete", "journal_900_complete");
    wgs_run_park(srv, "wgsbmp_leftover_movario_base_puzzles", "leftover_movario_base_puzzles");
    wgs_run_park(srv, "wgsbmp_leftover_broav_hunt", "leftover_broav_hunt");
    wgs_run_park(srv, "wgsbmp_leftover_assassin_ambush", "leftover_assassin_ambush");
    wgs_run_park(srv, "wgsbmp_leftover_hero_recruitment_walk", "leftover_hero_recruitment_walk");
    wgs_run_park(srv, "wgsbmp_leftover_orb_spy_unmask", "leftover_orb_spy_unmask");
    wgs_run_park(srv, "wgsbmp_leftover_balance_elemental", "leftover_balance_elemental");
    wgs_run_park(srv, "wgsbmp_leftover_tormented_demons", "leftover_tormented_demons");
    wgs_run_park(srv, "wgsbmp_leftover_tail_of_two_cats_gate", "leftover_tail_of_two_cats_gate");
    wgs_run_park(srv, "wgsbmp_leftover_sapphire_lantern_fm49", "leftover_sapphire_lantern_fm49");
    wgs_run_park(srv, "wgsbmp_leftover_full_refuse_trees", "leftover_full_refuse_trees");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "wgsrun") != 0,
                   "::wgsrun should reach [debugproc,wgsrun]");
    wgs_close(srv);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "whileguthixsleeps") != 0,
                   "::whileguthixsleeps should alias [debugproc,wgs]");
    wgs_close(srv);
    wgs_pass("debugprocs_wgs_wgsrun_alias");

    (void)vp_qp;
    (void)stat_thieving;
    (void)stat_attack;
    (void)stat_strength;

    ToriRSServer_WorldNpcReap(srv);
    ToriRSServer_VarbitSet(srv, vb_wgs, 0);
    if( vp_td >= 0 )
        player->varps[vp_td] = 0;
    wgs_god(player);
    wgs_pass("wgs_walk_done");
}
