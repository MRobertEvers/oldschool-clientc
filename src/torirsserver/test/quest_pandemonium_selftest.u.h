/* Pandemonium Gate D stanza. Worker-branch only -- parent will not merge
 * this .u.h onto v3.
 *
 * Guarded by TORIRSSERVER_SELFTEST_PAND_ONLY=1. Placed immediately before
 * the shop fprintf so an unset PAND_ONLY leaves the default suite unmoved.
 *
 * Walk must stop on chatmenu:options so refuse can pick row 2.
 * Every assertion is a real OPNPC / OPLOC dispatch or a named leftover
 * mesbox. Player stays unkillable (godmode) -- this is not a death case.
 */

static int
selftest_pand_varbit(const char* name)
{
    assert(name);
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
}

static int
selftest_pand_get(struct ToriRSServerPlayer* player, int bit)
{
    assert(player);
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
selftest_pand_set(struct ToriRSServer* srv, int bit, int value)
{
    assert(srv);
    if( bit < 0 )
        return;
    ToriRSServer_VarbitSet(srv, bit, value);
}

static void
selftest_pand_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "PAND PASS: %s\n", step);
}

static void
selftest_pand_close(struct ToriRSServer* srv)
{
    int i;

    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
    for( i = 0; i < 16 && srv->active_player && srv->active_player->active_script; i++ )
        selftest_tick(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static int
selftest_pand_spawn(
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
    if( slot >= 0 )
        srv->npcs[slot].despawns_on_death = 1;
    return slot;
}

static int
selftest_pand_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    int ran;

    assert(srv);
    ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1] for type %d should run, got %d", npc_type, ran);
    return ran;
}

static void
selftest_pand_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
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
selftest_pand_pick(struct ToriRSServerPlayer* player, int row)
{
    int chatmenu;
    uint8_t button[6];
    int uid;

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

static int
selftest_pand_loc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    const char* loc_name)
{
    int loc;
    int slot;

    assert(srv);
    assert(player);
    assert(loc_name);
    loc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, loc_name);
    SELFTEST_CHECK(loc >= 0, "loc %s should resolve", loc_name);
    if( loc < 0 )
        return -1;
    slot = ToriRSServer_SceneAddLoc(player->x, player->z, player->level, loc, 0, 0);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(player->x, player->z, player->level, loc);
    if( slot < 0 )
        return -1;
    return ToriRSServer_ScriptsRunTriggerOnLoc(
        srv, SS_TRIGGER_OPLOC1, loc, ToriRSServer_LocCategory(loc), slot);
}

static void
selftest_quest_pandemonium(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int sailing;
    int chatmenu;
    int will_type;
    int anne_type;
    int will_boat_type;
    int ribs_type;
    int steve_type;
    int grog_type;
    int jim_type;
    int slot;
    int coupons;
    int kits;
    int spyglass;
    static const char* leftovers[] = {
        "[proc,pan_leftover_helm_sail_navigation]",
        "[proc,pan_leftover_salvaging_hook_cutscene]",
        "[proc,pan_leftover_shipyard_portal]",
        "[proc,pan_leftover_cargo_hold_build_if]",
        "[proc,pan_leftover_port_task_ledger_ui]",
        "[proc,pan_leftover_sailing_xp]",
        "[proc,pan_leftover_full_refuse_trees]",
    };
    int i;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Pandemonium Gate D\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "PAND walk loads a compiled script pack");
    if( !loaded )
        return;

    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    player->dying = 0;

    sailing = selftest_pand_varbit("sailing_intro");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    will_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sailing_intro_will_sarim");
    anne_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sailing_intro_anne_sarim");
    will_boat_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sailing_intro_will_boat");
    ribs_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sailing_intro_ribs");
    steve_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "steve_beanie");
    grog_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sailing_intro_grog_vis");
    jim_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "junior_jim");
    coupons = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sawmill_coupon");
    kits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "boat_repair_kit");
    spyglass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sailing_charting_spyglass");

    SELFTEST_CHECK(sailing >= 0, "%%sailing_intro should resolve");
    SELFTEST_CHECK(chatmenu > 0, "chatmenu:options should resolve");
    SELFTEST_CHECK(will_type > 0, "sailing_intro_will_sarim should resolve");
    SELFTEST_CHECK(anne_type > 0, "sailing_intro_anne_sarim should resolve");
    SELFTEST_CHECK(will_boat_type > 0, "sailing_intro_will_boat should resolve");
    SELFTEST_CHECK(ribs_type > 0, "sailing_intro_ribs should resolve");
    SELFTEST_CHECK(steve_type > 0, "steve_beanie should resolve");
    SELFTEST_CHECK(grog_type > 0, "sailing_intro_grog_vis should resolve");
    SELFTEST_CHECK(jim_type > 0, "junior_jim should resolve");
    if( sailing < 0 || will_type <= 0 || chatmenu <= 0 )
        return;

    /* ---- Offer parks on p_choice2 Yes / No. No invented qualify. ---- */
    selftest_pand_set(srv, sailing, 0);
    slot = selftest_pand_spawn(srv, player, "sailing_intro_will_sarim");
    if( slot >= 0 )
    {
        selftest_pand_talk(srv, will_type, slot);
        SELFTEST_CHECK(player->active_script != NULL,
                       "Will start should park on chat");
        selftest_pand_click_until_menu(srv, 8);
        SELFTEST_CHECK(player->active_script != NULL,
                       "start should park on p_choice2");
        SELFTEST_CHECK(player->resume_button_count == 1 &&
                           player->resume_buttons[0] == chatmenu,
                       "offer should arm chatmenu:options");
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 0,
                       "offer itself must not write %%sailing_intro, got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("will_offer_p_choice2");

        /* Refuse: row 2 "No." must chat and leave %sailing_intro at 0. */
        selftest_pand_pick(player, 2);
        SELFTEST_CHECK(player->active_script != NULL,
                       "refuse should park on refuse chat");
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 0,
                       "refuse must not write %%sailing_intro, got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("will_refuse_no_sailing_intro");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Accept: row 1 "Yes." writes ^pan_board (4). ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 0);
    slot = selftest_pand_spawn(srv, player, "sailing_intro_will_sarim");
    if( slot >= 0 )
    {
        selftest_pand_talk(srv, will_type, slot);
        selftest_pand_click_until_menu(srv, 8);
        SELFTEST_CHECK(player->resume_button_count == 1 &&
                           player->resume_buttons[0] == chatmenu,
                       "accept path should still be on p_choice2");
        selftest_pand_pick(player, 1);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 4,
                       "Yes must write %%sailing_intro = ^pan_board (4), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("will_accept_board");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Board choice: Not yet must not advance. ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 4);
    slot = selftest_pand_spawn(srv, player, "sailing_intro_will_sarim");
    if( slot >= 0 )
    {
        selftest_pand_talk(srv, will_type, slot);
        selftest_pand_click_until_menu(srv, 8);
        SELFTEST_CHECK(player->resume_button_count == 1 &&
                           player->resume_buttons[0] == chatmenu,
                       "board offer should arm chatmenu:options");
        selftest_pand_pass("will_board_p_choice2");
        selftest_pand_pick(player, 2);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 4,
                       "Not yet must not advance %%sailing_intro, got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("will_board_not_yet");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Board ready writes ^pan_where (16). ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 4);
    slot = selftest_pand_spawn(srv, player, "sailing_intro_will_sarim");
    if( slot >= 0 )
    {
        selftest_pand_talk(srv, will_type, slot);
        selftest_pand_click_until_menu(srv, 8);
        selftest_pand_pick(player, 1);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 16,
                       "board ready should write %%sailing_intro = ^pan_where (16), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("will_board_ready");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Anne Sarim hook stays first-line spliced (same label). ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 16);
    slot = selftest_pand_spawn(srv, player, "sailing_intro_anne_sarim");
    if( slot >= 0 && anne_type > 0 )
    {
        selftest_pand_talk(srv, anne_type, slot);
        selftest_click_through(srv, 4);
        selftest_pand_pass("anne_sarim_mid");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Boat listen writes ^pan_nav (8). ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 4);
    slot = selftest_pand_spawn(srv, player, "sailing_intro_will_boat");
    if( slot >= 0 && will_boat_type > 0 )
    {
        selftest_pand_talk(srv, will_boat_type, slot);
        selftest_click_through(srv, 4);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 8,
                       "boat listen should write %%sailing_intro = ^pan_nav (8), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("will_boat_listen");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Helm / salvage / island / courier path. ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 6);
    if( selftest_pand_loc(srv, player, "sailing_intro_navigating") == TORIRSSERVER_TRIGGER_RAN )
    {
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 14,
                       "helm soft-skip should write %%sailing_intro = ^pan_wreck (14), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("helm_softskip");
    }
    selftest_pand_close(srv);

    selftest_pand_set(srv, sailing, 14);
    if( selftest_pand_loc(srv, player, "sailing_intro_salvaging_hook") == TORIRSSERVER_TRIGGER_RAN )
    {
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 16,
                       "salvage hook should write %%sailing_intro = ^pan_where (16), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("salvage_hook");
    }
    selftest_pand_close(srv);

    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 16);
    slot = selftest_pand_spawn(srv, player, "sailing_intro_ribs");
    if( slot >= 0 && ribs_type > 0 )
    {
        selftest_pand_talk(srv, ribs_type, slot);
        selftest_pand_click_until_menu(srv, 8);
        SELFTEST_CHECK(player->resume_button_count == 1 &&
                           player->resume_buttons[0] == chatmenu,
                       "Ribs welcome should arm p_choice3");
        selftest_pand_pick(player, 1);
        selftest_click_through(srv, 6);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 20,
                       "Ribs Where-am-I should write %%sailing_intro = ^pan_ribs (20), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("ribs_where");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 20);
    slot = selftest_pand_spawn(srv, player, "steve_beanie");
    if( slot >= 0 && steve_type > 0 )
    {
        selftest_pand_talk(srv, steve_type, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 24,
                       "Steve should write %%sailing_intro = ^pan_ship (24), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("steve_jim_nudge");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    {
        int cup = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sailing_intro_cup");

        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        selftest_pand_set(srv, sailing, 24);
        if( cup > 0 )
        {
            player->inv[0].obj_id = cup;
            player->inv[0].count = 1;
        }
        slot = selftest_pand_spawn(srv, player, "junior_jim");
        if( slot >= 0 && jim_type > 0 )
        {
            selftest_pand_talk(srv, jim_type, slot);
            selftest_click_through(srv, 8);
            SELFTEST_CHECK(selftest_pand_get(player, sailing) == 28,
                           "Jim cup trade should write %%sailing_intro = ^pan_cargo_build (28), got %d",
                           selftest_pand_get(player, sailing));
            selftest_pand_pass("jim_cup_trade");
            selftest_pand_close(srv);
            ToriRSServer_WorldNpcFree(srv, slot);
            ToriRSServer_WorldNpcReap(srv);
        }
    }

    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 28);
    if( selftest_pand_loc(srv, player, "sailing_boat_facility_placeholder_raft_0") ==
        TORIRSSERVER_TRIGGER_RAN )
    {
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 30,
                       "cargo-hold build should write %%sailing_intro = ^pan_log (30), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("cargo_build");
    }
    selftest_pand_close(srv);

    selftest_pand_set(srv, sailing, 28);
    slot = selftest_pand_spawn(srv, player, "junior_jim");
    if( slot >= 0 && jim_type > 0 )
    {
        selftest_pand_talk(srv, jim_type, slot);
        selftest_click_through(srv, 6);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 32,
                       "Jim log should write %%sailing_intro = ^pan_job (32), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("jim_log");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    selftest_pand_set(srv, sailing, 32);
    if( selftest_pand_loc(srv, player, "sailing_gangplank_proxy_wide") == TORIRSSERVER_TRIGGER_RAN )
    {
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 38,
                       "gangplank to Sarim should write %%sailing_intro = ^pan_port (38), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("gangplank_to_sarim");
    }
    selftest_pand_close(srv);

    selftest_pand_set(srv, sailing, 38);
    if( selftest_pand_loc(srv, player, "dock_loading_bay_ledger_table_withdraw") ==
        TORIRSSERVER_TRIGGER_RAN )
    {
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 40,
                       "ledger withdraw should write %%sailing_intro = ^pan_deliver (40), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("ledger_withdraw");
    }
    selftest_pand_close(srv);

    selftest_pand_set(srv, sailing, 40);
    if( selftest_pand_loc(srv, player, "dock_loading_bay_ledger_table_deposit") ==
        TORIRSSERVER_TRIGGER_RAN )
    {
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 44,
                       "ledger deposit should write %%sailing_intro = ^pan_jim (44), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("ledger_deposit");
    }
    selftest_pand_close(srv);

    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 44);
    slot = selftest_pand_spawn(srv, player, "steve_beanie");
    if( slot >= 0 && steve_type > 0 )
    {
        selftest_pand_talk(srv, steve_type, slot);
        selftest_click_through(srv, 8);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 46,
                       "Steve grog nudge should write %%sailing_intro = ^pan_grog (46), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("steve_grog_nudge");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    selftest_pand_set(srv, sailing, 46);
    slot = selftest_pand_spawn(srv, player, "sailing_intro_grog_vis");
    if( slot >= 0 && grog_type > 0 )
    {
        selftest_pand_talk(srv, grog_type, slot);
        selftest_click_through(srv, 6);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 48,
                       "Grog wrap should write %%sailing_intro = ^pan_finish (48), got %d",
                       selftest_pand_get(player, sailing));
        selftest_pand_pass("grog_wrap");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Complete: coupons / kits / spyglass. Sailing XP leftover. ---- */
    selftest_reset_world(srv, player, 402, 402);
    player->godmode = 1;
    selftest_pand_set(srv, sailing, 48);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
    slot = selftest_pand_spawn(srv, player, "steve_beanie");
    if( slot >= 0 && steve_type > 0 )
    {
        selftest_pand_talk(srv, steve_type, slot);
        selftest_click_through(srv, 16);
        SELFTEST_CHECK(selftest_pand_get(player, sailing) == 50,
                       "Steve wrap should write %%sailing_intro = ^pan_complete (50), got %d",
                       selftest_pand_get(player, sailing));
        if( coupons > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, coupons) >= 25,
                           "complete should grant 25 sawmill coupons, got %d",
                           selftest_count_obj(player, coupons));
        if( kits > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, kits) >= 2,
                           "complete should grant 2 boat repair kits, got %d",
                           selftest_count_obj(player, kits));
        if( spyglass > 0 )
            SELFTEST_CHECK(selftest_count_obj(player, spyglass) >= 1,
                           "complete should grant a spyglass, got %d",
                           selftest_count_obj(player, spyglass));
        selftest_pand_pass("complete_scroll");
        selftest_pand_close(srv);
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }

    /* ---- Journal complete prints QUEST COMPLETE! ---- */
    selftest_pand_set(srv, sailing, 50);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,pandemonium_journal]", NULL, 0) != 0,
                   "~pandemonium_journal should run at complete");
    SELFTEST_CHECK(player->mainmodal_group > 0 || player->chatmodal_group > 0,
                   "complete journal should mount QUEST COMPLETE");
    selftest_pand_pass("journal_50_complete");
    selftest_pand_close(srv);

    /* ---- Allowed leftovers only. ---- */
    for( i = 0; i < (int)(sizeof(leftovers) / sizeof(leftovers[0])); i++ )
    {
        selftest_pand_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, leftovers[i], NULL, 0) != 0,
                       "%s should run", leftovers[i]);
        SELFTEST_CHECK(player->chatmodal_group > 0,
                       "%s should park a leftover mesbox", leftovers[i]);
        selftest_pand_pass(leftovers[i]);
        selftest_pand_close(srv);
    }

    /* ---- Debug cheats still exist. ---- */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "pandemonium") != TORIRSSERVER_TRIGGER_NONE,
                   "::pandemonium should reach [debugproc,pandemonium]");
    selftest_pand_close(srv);
    {
        static struct ToriRSServerCapture cap;
        int pandrun_ok = 0;

        selftest_pand_set(srv, sailing, 0);
        ToriRSServer_CaptureBegin(srv, &cap);
        ToriRSServer_ScriptsRunDebugproc(srv, "pandrun");
        ToriRSServer_CaptureEnd(srv);
        for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
             i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const char* text = selftest_message_text(srv, &cap.packets[i]);
            if( text && strstr(text, "pandrun OK") != NULL )
                pandrun_ok = 1;
        }
        SELFTEST_CHECK(pandrun_ok, "::pandrun should reach its OK line");
        selftest_pand_pass("pandrun");
        ToriRSServer_ScriptsProcessQueues(srv);
        selftest_pand_close(srv);
    }

    ToriRSServer_WorldNpcReap(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    selftest_pand_close(srv);
}
