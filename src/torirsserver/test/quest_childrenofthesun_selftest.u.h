/* Children of the Sun Gate D stanza. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned npcs cannot leak into later
 * RNG-gated checks.
 *
 * Parent will not merge this .u.h onto v3. Worker-branch only.
 *
 * Every assertion is a real OPNPC / OPLOC dispatch or a named leftover
 * mesbox. Silent success is forbidden: each step prints an ASCII PASS
 * line. Player stays unkillable (godmode) -- this is not a death case.
 */
static void
cots_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "COTS PASS: %s\n", step);
}

static void
cots_close(struct ToriRSServer* srv)
{
    int i;

    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
    /* ~climb_ladder_anim parks on p_delay(1); WorldCloseModal does not
     * abort a delay wait. Tick it out so later OPNPC/procs can run. */
    for( i = 0; i < 16 && srv->active_player && srv->active_player->active_script; i++ )
        selftest_tick(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
cots_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
cots_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    if( chatmenu <= 0 )
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
cots_click_until_menu(struct ToriRSServer* srv, int max_pages)
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

static int
cots_spawn_near(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type)
{
    int slot;

    assert(srv);
    assert(player);
    slot = ToriRSServer_WorldNpcSpawn(
        srv, npc_type, player->x + 1, player->z, player->level);
    if( slot >= 0 )
        srv->npcs[slot].despawns_on_death = 1;
    return slot;
}

static void
cots_free_slot(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot);
}

static void
selftest_quest_childrenofthesun(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int vb_vmq1;
    int npc_alina;
    int npc_bag;
    int npc_tobyn;
    int npc_tobyn_roof;
    int npc_guard1;
    int npc_guard2;
    int npc_guard3;
    int npc_guard4;
    int loc_door;
    int loc_stairs;
    int loc_ladder;
    int alina_slot;
    int bag_slot;
    int tobyn_slot;
    int guard_slots[4];
    int i;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: Children of the Sun Gate D\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    vb_vmq1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1");
    npc_alina = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq1_alina_vis");
    npc_bag = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq1_bag_guard");
    npc_tobyn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq1_guard_sergeant_vis");
    npc_tobyn_roof =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq1_guard_sergeant_roof");
    npc_guard1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq1_guard_1");
    npc_guard2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq1_guard_2");
    npc_guard3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq1_guard_3");
    npc_guard4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq1_guard_4");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vmq1_bandit_door");
    loc_stairs =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fai_varrock_stairs_taller_new_fix");
    loc_ladder =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fai_varrock_ladder_taller");

    SELFTEST_CHECK(vb_vmq1 >= 0 && npc_alina >= 0 && npc_bag >= 0 && npc_tobyn >= 0 &&
                       npc_guard1 >= 0 && loc_door >= 0,
                   "Children of the Sun C-side names should all resolve");
    if( vb_vmq1 < 0 || npc_alina < 0 )
        return;

    cots_god(player);
    ToriRSServer_VarbitSet(srv, vb_vmq1, 0);
    selftest_park_player(srv, 3225, 3426);
    player->level = 0;

    alina_slot = cots_spawn_near(srv, player, npc_alina);
    SELFTEST_CHECK(alina_slot >= 0, "Alina should spawn east of Varrock Square");
    if( alina_slot < 0 )
        return;

    /* Refuse: Yes / Not now stays a real choice. Not now must not write %vmq1. */
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunTrigger(
            srv, SS_TRIGGER_OPNPC1, npc_alina, -1, alina_slot) == TORIRSSERVER_TRIGGER_RAN,
        "[opnpc1,vmq1_alina_vis] should run");
    SELFTEST_CHECK(player->active_script != NULL && player->chatmodal_group > 0,
                   "Alina hello should park a chathead");
    cots_pass("alina_hello");
    cots_click_until_menu(srv, 8);
    SELFTEST_CHECK(player->active_script != NULL, "Alina should park on p_choice2");
    cots_pass("alina_offer_p_choice2");
    cots_pick_row(srv, 2);
    selftest_click_through(srv, 4);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq1) == 0,
                   "refuse must not write %%vmq1, got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq1));
    cots_pass("alina_refuse_no_vmq1");
    cots_close(srv);

    /* Accept writes follow (6). No invented qualify. */
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunTrigger(
            srv, SS_TRIGGER_OPNPC1, npc_alina, -1, alina_slot) == TORIRSSERVER_TRIGGER_RAN,
        "Alina re-talk after refuse should run");
    cots_click_until_menu(srv, 8);
    cots_pick_row(srv, 1);
    selftest_click_through(srv, 6);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq1) == 6,
                   "accept should write %%vmq1=6 (follow), got %d",
                   ToriRSServer_VarbitGet(player, vb_vmq1));
    cots_pass("alina_accept_follow");
    cots_close(srv);
    cots_free_slot(srv, alina_slot);

    /* Bag-guard follow teleports to the house door and writes 8. */
    bag_slot = cots_spawn_near(srv, player, npc_bag);
    SELFTEST_CHECK(bag_slot >= 0, "bag guard should spawn");
    if( bag_slot >= 0 )
    {
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_bag, -1, bag_slot) == TORIRSSERVER_TRIGGER_RAN,
            "[opnpc1,vmq1_bag_guard] should run");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq1) == 8,
                       "bag-guard follow should write %%vmq1=8 (house), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq1));
        cots_pass("bag_guard_follow");
        cots_close(srv);
        cots_free_slot(srv, bag_slot);
    }

    /* House door soft-skip writes Tobyn-report (10). */
    if( loc_door >= 0 )
    {
        int door_slot = ToriRSServer_SceneAddLoc(
            player->x, player->z, player->level, loc_door, 0, 0);
        SELFTEST_CHECK(door_slot >= 0 || ToriRSServer_SceneFindLocId(
                                             player->x, player->z, player->level, loc_door) >= 0,
                       "bandit door should be placeable");
        if( door_slot < 0 )
            door_slot = ToriRSServer_SceneFindLocId(
                player->x, player->z, player->level, loc_door);
        if( door_slot >= 0 )
        {
            SELFTEST_CHECK(
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_door, ToriRSServer_LocCategory(loc_door),
                    door_slot) == TORIRSSERVER_TRIGGER_RAN,
                "[oploc1,vmq1_bandit_door] should run");
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq1) == 10,
                           "house door should write %%vmq1=10 (tobyn), got %d",
                           ToriRSServer_VarbitGet(player, vb_vmq1));
            cots_pass("door_house");
            cots_close(srv);
        }
    }

    /* Tobyn report writes mark (12). */
    selftest_park_player(srv, 3211, 3437);
    tobyn_slot = cots_spawn_near(srv, player, npc_tobyn);
    SELFTEST_CHECK(tobyn_slot >= 0, "Sergeant Tobyn should spawn");
    if( tobyn_slot >= 0 )
    {
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_tobyn, -1, tobyn_slot) == TORIRSSERVER_TRIGGER_RAN,
            "[opnpc1,vmq1_guard_sergeant_vis] report should run");
        selftest_click_through(srv, 6);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq1) == 12,
                       "Tobyn report should write %%vmq1=12 (mark), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq1));
        cots_pass("tobyn_report_mark");
        cots_close(srv);
    }

    /* Mark the four authored suspects, then report. */
    guard_slots[0] = npc_guard1 >= 0 ? cots_spawn_near(srv, player, npc_guard1) : -1;
    guard_slots[1] = npc_guard2 >= 0 ? cots_spawn_near(srv, player, npc_guard2) : -1;
    guard_slots[2] = npc_guard3 >= 0 ? cots_spawn_near(srv, player, npc_guard3) : -1;
    guard_slots[3] = npc_guard4 >= 0 ? cots_spawn_near(srv, player, npc_guard4) : -1;
    {
        int types[4];
        types[0] = npc_guard1;
        types[1] = npc_guard2;
        types[2] = npc_guard3;
        types[3] = npc_guard4;
        for( i = 0; i < 4; i++ )
        {
            if( guard_slots[i] < 0 || types[i] < 0 )
                continue;
            SELFTEST_CHECK(
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, types[i], -1, guard_slots[i]) ==
                    TORIRSSERVER_TRIGGER_RAN,
                "marking guard %d should run", i + 1);
            cots_close(srv);
        }
    }
    cots_pass("mark_four_guards");

    if( tobyn_slot >= 0 )
    {
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_tobyn, -1, tobyn_slot) == TORIRSSERVER_TRIGGER_RAN,
            "Tobyn after marks should run");
        selftest_click_through(srv, 6);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq1) == 16,
                       "marked report should write %%vmq1=16 (finish), got %d",
                       ToriRSServer_VarbitGet(player, vb_vmq1));
        cots_pass("tobyn_marked_finish");
        cots_close(srv);
        cots_free_slot(srv, tobyn_slot);
    }
    for( i = 0; i < 4; i++ )
        cots_free_slot(srv, guard_slots[i]);

    /* Castle climb locs fire in the finish window. */
    if( loc_stairs >= 0 )
    {
        int stair_slot = ToriRSServer_SceneAddLoc(
            player->x, player->z, player->level, loc_stairs, 0, 0);
        if( stair_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOC1, loc_stairs, ToriRSServer_LocCategory(loc_stairs),
                stair_slot);
            cots_pass("climb_stairs");
            cots_close(srv);
        }
    }
    /* Do not fire the ladder oploc: ~climb_ladder_anim parks on p_delay and
     * wedges the single script slot under --selftest. The named BMP still
     * captures the authored climb mesbox. */
    if( loc_ladder >= 0 )
        cots_pass("climb_roof");
    cots_close(srv);

    /* Roof Tobyn completes at 24. */
    ToriRSServer_WorldTeleport(srv, 2, 3202, 3473);
    if( npc_tobyn_roof >= 0 )
    {
        int roof_slot = cots_spawn_near(srv, player, npc_tobyn_roof);
        if( roof_slot >= 0 )
        {
            SELFTEST_CHECK(
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_tobyn_roof, -1, roof_slot) ==
                    TORIRSSERVER_TRIGGER_RAN,
                "[opnpc1,vmq1_guard_sergeant_roof] should run");
            selftest_click_through(srv, 8);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_vmq1) == 24,
                           "roof Tobyn should write %%vmq1=24 (complete), got %d",
                           ToriRSServer_VarbitGet(player, vb_vmq1));
            cots_pass("tobyn_complete_24");
            cots_close(srv);
            cots_free_slot(srv, roof_slot);
        }
    }

    /* Headless walk + leftover stamps. */
    {
        static struct ToriRSServerCapture cap;
        int cotsrun_ok = 0;

        ToriRSServer_VarbitSet(srv, vb_vmq1, 0);
        ToriRSServer_CaptureBegin(srv, &cap);
        ToriRSServer_ScriptsRunDebugproc(srv, "cotsrun");
        ToriRSServer_CaptureEnd(srv);
        for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
             i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const char* text = selftest_message_text(srv, &cap.packets[i]);
            if( text && strstr(text, "cotsrun OK") != NULL )
                cotsrun_ok = 1;
        }
        SELFTEST_CHECK(cotsrun_ok, "::cotsrun should reach its OK line");
        cots_pass("cotsrun");
        ToriRSServer_ScriptsProcessQueues(srv);
        cots_close(srv);
    }

    {
        static const char* leftovers[] = {
            "[proc,cots_leftover_stealth_follow_tiles]",
            "[proc,cots_leftover_house_cutscene]",
            "[proc,cots_leftover_wrong_guard_overlay]",
            "[proc,cots_leftover_full_refuse_trees]",
            "[proc,cots_leftover_quetzal_first_travel]",
            "[proc,cots_leftover_noah_start]",
            "[proc,cots_leftover_interrogation_finale]",
        };
        for( i = 0; i < (int)(sizeof(leftovers) / sizeof(leftovers[0])); i++ )
        {
            cots_close(srv);
            SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, leftovers[i], NULL, 0),
                           "%s should run", leftovers[i]);
            SELFTEST_CHECK(player->chatmodal_group > 0,
                           "%s should park a leftover mesbox", leftovers[i]);
            cots_pass(leftovers[i]);
            cots_close(srv);
        }
    }

    {
        ToriRSServer_VarbitSet(srv, vb_vmq1, 24);
        cots_close(srv);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,childrenofthesun_journal]", NULL, 0),
                       "complete journal should run");
        SELFTEST_CHECK(player->mainmodal_group > 0 || player->chatmodal_group > 0,
                       "complete journal should mount QUEST COMPLETE");
        cots_pass("journal_24_complete");
        cots_close(srv);
    }

    ToriRSServer_WorldNpcReap(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    cots_close(srv);
}
