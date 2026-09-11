/* A Night at the Theatre Gate D C-walk. Included immediately before the
 * shop fprintf in torirs_server_world_selftest.c. Worker branch only --
 * do not merge this header onto parent v3.
 *
 * Gate: TORIRSSERVER_SELFTEST_NATT_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0
 */
{
    int natt_only = getenv("TORIRSSERVER_SELFTEST_NATT_ONLY") != NULL;
    int loaded;
    int tobquest;
    int myq4;
    int done_tob;
    int npc_stranger;
    int npc_daer;
    int loc_crypt_enter;
    int loc_crypt_door;
    int loc_coffin;
    int loc_cave;
    int loc_skeleton;
    int loc_eggs;
    int loc_hespori;
    int loc_hespori_dead;
    int obj_key;
    int obj_head;
    int obj_note;
    int obj_eggs;
    int obj_bark;
    int obj_lamp;
    int if_gold;
    int if_silver;
    int if_flute;
    int chatmenu;
    int slot;
    int choice_row;
    uint8_t resume[6];
    static struct ToriRSServerCapture natt_cap;
    int natt_ok;
    int i;

    if( !natt_only )
        goto natt_selftest_skip;

    fprintf(stderr, "ToriRSServer selftest: A Night at the Theatre\n");
    assert(srv);
    assert(player);
    player->godmode = 1;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "NATT script pack should load");
    if( !loaded )
    {
        fprintf(stderr, "  FAIL  no compiled script pack for NATT\n");
        fprintf(stderr, "ToriRSServer NATT selftest: %lu checks, %d failures\n",
                g_selftest_checks, g_selftest_failures);
        return g_selftest_failures;
    }

    tobquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "tobquest");
    myq4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "myq4");
    done_tob = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "tobquest_done_tob");
    npc_stranger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tob_stranger");
    npc_daer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sanctuary_dark_wizard");
    loc_crypt_enter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tobquest_crypt_entrance");
    loc_crypt_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tobquest_crypt_inner_door");
    loc_coffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tobquest_crypt_coffin_op");
    loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "araxyte_cave_entry");
    loc_skeleton = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tobquest_skeleton_op");
    loc_eggs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tobquest_egg_sac_full");
    loc_hespori = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tobquest_hespori_ready");
    loc_hespori_dead = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tobquest_hespori_dead");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tobquest_key");
    obj_head = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tobquest_head");
    obj_note = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tobquest_note");
    obj_eggs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tobquest_araxyte_eggs");
    obj_bark = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tobquest_bark");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");
    if_gold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "crafting_gold");
    if_silver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "silver_crafting");
    if_flute = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "ratcatcher_flute");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(tobquest >= 0, "%%tobquest varbit should resolve");
    SELFTEST_CHECK(myq4 >= 0, "%%myq4 varbit should resolve");
    SELFTEST_CHECK(npc_stranger >= 0, "tob_stranger should resolve");
    SELFTEST_CHECK(npc_daer >= 0, "sanctuary_dark_wizard should resolve");
    SELFTEST_CHECK(loc_crypt_enter >= 0, "tobquest_crypt_entrance should resolve");
    SELFTEST_CHECK(loc_crypt_door >= 0, "tobquest_crypt_inner_door should resolve");
    SELFTEST_CHECK(loc_coffin >= 0, "tobquest_crypt_coffin_op should resolve");
    SELFTEST_CHECK(loc_cave >= 0, "araxyte_cave_entry should resolve");
    SELFTEST_CHECK(loc_skeleton >= 0, "tobquest_skeleton_op should resolve");
    SELFTEST_CHECK(loc_eggs >= 0, "tobquest_egg_sac_full should resolve");
    SELFTEST_CHECK(loc_hespori >= 0, "tobquest_hespori_ready should resolve");
    SELFTEST_CHECK(loc_hespori_dead >= 0, "tobquest_hespori_dead should resolve");
    SELFTEST_CHECK(obj_key >= 0, "tobquest_key should resolve");
    SELFTEST_CHECK(obj_head >= 0, "tobquest_head should resolve");
    SELFTEST_CHECK(obj_note >= 0, "tobquest_note should resolve");
    SELFTEST_CHECK(obj_eggs >= 0, "tobquest_araxyte_eggs should resolve");
    SELFTEST_CHECK(obj_bark >= 0, "tobquest_bark should resolve");
    SELFTEST_CHECK(obj_lamp >= 0, "thosf_reward_lamp should resolve");
    SELFTEST_CHECK(if_gold == 446, "crafting_gold jewellery IF should be 446, got %d", if_gold);
    SELFTEST_CHECK(if_silver == 6, "silver_crafting jewellery IF should be 6, got %d", if_silver);
    SELFTEST_CHECK(if_flute == 282, "ratcatcher_flute widget should be 282, got %d", if_flute);
    SELFTEST_CHECK(chatmenu >= 0, "chatmenu:options should resolve");

    ToriRSServer_CaptureBegin(srv, &natt_cap);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_required_systems_probe]", NULL, 0),
                   "natt_required_systems_probe should run");
    ToriRSServer_CaptureEnd(srv);
    {
        int saw_day = 0;
        int saw_grab = 0;

        for( i = ToriRSServer_CaptureFindNamed(&natt_cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
             i = ToriRSServer_CaptureFindNamed(&natt_cap, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const char* text = selftest_message_text(srv, &natt_cap.packets[i]);

            if( !text )
                continue;
            if( strstr(text, "natt date_runeday=") )
                saw_day = 1;
            if( strstr(text, "natt telegrab OK") )
                saw_grab = 1;
        }
        SELFTEST_CHECK(saw_day, "date_runeday() should print from the NATT probe");
        SELFTEST_CHECK(saw_grab, "telekinetic grab spell data should resolve");
    }
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    selftest_reset_world(srv, player, 459, 402);
    player->godmode = 1;
    ToriRSServer_WorldTeleport(srv, 0, 3673, 3223);
    selftest_clear_inv(player);
    if( tobquest >= 0 )
        ToriRSServer_VarbitSet(srv, tobquest, 0);
    if( myq4 >= 0 )
        ToriRSServer_VarbitSet(srv, myq4, 0);
    if( done_tob >= 0 )
        ToriRSServer_VarbitSet(srv, done_tob, 0);

    slot = ToriRSServer_WorldNpcSpawn(srv, npc_stranger, 3674, 3223, 0);
    SELFTEST_CHECK(slot >= 0, "Mysterious Stranger should spawn");

    /* Qualify fail: Taste of Hope incomplete. Do not write %tobquest. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "stranger OPNPC1 should run");
    selftest_click_through(srv, 16);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest < 0 || ToriRSServer_VarbitGet(player, tobquest) == 0,
                   "qualify fail must not start the quest, got %d",
                   tobquest >= 0 ? ToriRSServer_VarbitGet(player, tobquest) : -1);
    SELFTEST_CHECK(selftest_count(player, obj_key) == 0,
                   "qualify fail must not hand a crypt key");

    /* Refuse: Taste of Hope complete, pick Not now. */
    if( myq4 >= 0 )
        ToriRSServer_VarbitSet(srv, myq4, 165);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "stranger offer OPNPC1 should run");
    for( i = 0; i < 24 && player->active_script; i++ )
    {
        int uid;

        if( player->resume_button_count <= 0 )
            break;
        uid = player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            break;
        if( !ToriRSServer_ScriptsResumeButton(srv, uid) )
            break;
    }
    SELFTEST_CHECK(player->active_script != NULL && player->resume_button_count > 0 &&
                       chatmenu > 0 && player->resume_buttons[0] == chatmenu,
                   "offer should park on p_choice2 Yes / Not now");
    choice_row = 2;
    resume[0] = (uint8_t)(chatmenu >> 24);
    resume[1] = (uint8_t)(chatmenu >> 16);
    resume[2] = (uint8_t)(chatmenu >> 8);
    resume[3] = (uint8_t)chatmenu;
    resume[4] = 0;
    resume[5] = (uint8_t)choice_row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, resume, sizeof(resume));
    selftest_click_through(srv, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest < 0 || ToriRSServer_VarbitGet(player, tobquest) == 0,
                   "Not now must refuse the start, got %d",
                   tobquest >= 0 ? ToriRSServer_VarbitGet(player, tobquest) : -1);
    SELFTEST_CHECK(selftest_count(player, obj_key) == 0,
                   "refuse must not hand a crypt key");

    /* Accept: Yes. writes %tobquest = 8 and hands a key. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "stranger accept OPNPC1 should run");
    selftest_click_through(srv, 24);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 8,
                   "Yes should write %%tobquest = ^natt_crypt (8), got %d",
                   tobquest >= 0 ? ToriRSServer_VarbitGet(player, tobquest) : -1);
    SELFTEST_CHECK(selftest_count(player, obj_key) >= 1, "accept should hand a crypt key");

    /* Mid-quest stranger: any progress. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "stranger mid-crypt OPNPC1 should run");
    selftest_click_through(srv, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* Crypt door unlock + coffin head. */
    if( tobquest >= 0 )
        ToriRSServer_VarbitSet(srv, tobquest, 8);
    ToriRSServer_WorldTeleport(srv, 0, 3658, 3409);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crypt_door,
                                        ToriRSServer_LocCategory(loc_crypt_door), -1);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 12,
                   "unlocking the inner door should write ^natt_coffin (12), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));

    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_coffin,
                                        ToriRSServer_LocCategory(loc_coffin), -1);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 14,
                   "coffin should write ^natt_head (14), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));
    SELFTEST_CHECK(selftest_count(player, obj_head) >= 1, "coffin should give Ranis's head");

    /* Deliver head. */
    ToriRSServer_WorldTeleport(srv, 0, 3673, 3223);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "deliver-head OPNPC1 should run");
    selftest_click_through(srv, 12);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 20,
                   "head hand-in should write ^natt_spider (20), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));

    /* Cave + skeleton + Daer + eggs. */
    ToriRSServer_WorldTeleport(srv, 0, 3728, 3300);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cave,
                                        ToriRSServer_LocCategory(loc_cave), -1);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) >= 22,
                   "cave enter should reach skeleton state, got %d",
                   ToriRSServer_VarbitGet(player, tobquest));

    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_skeleton,
                                        ToriRSServer_LocCategory(loc_skeleton), -1);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 26,
                   "skeleton should write ^natt_daer (26), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));
    SELFTEST_CHECK(selftest_count(player, obj_note) >= 1, "skeleton should give a note");

    {
        int daer_slot = ToriRSServer_WorldNpcSpawn(srv, npc_daer, 3729, 3300, 0);

        SELFTEST_CHECK(daer_slot >= 0, "Daer Kand should spawn");
        if( daer_slot >= 0 )
        {
            SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_daer,
                                                         ToriRSServer_NpcCategory(npc_daer),
                                                         daer_slot)
                               == TORIRSSERVER_TRIGGER_RAN,
                           "Daer Kand OPNPC1 should run");
            selftest_click_through(srv, 12);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 28,
                           "Daer should write ^natt_eggs_cave (28), got %d",
                           ToriRSServer_VarbitGet(player, tobquest));
            ToriRSServer_WorldNpcFree(srv, daer_slot);
        }
    }

    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_eggs,
                                        ToriRSServer_LocCategory(loc_eggs), -1);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 30,
                   "egg sac should write ^natt_eggs (30), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));
    SELFTEST_CHECK(selftest_count(player, obj_eggs) >= 1, "egg sac should give araxyte eggs");

    /* Eggs hand-in -> grotto. */
    ToriRSServer_WorldTeleport(srv, 0, 3673, 3223);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "eggs hand-in OPNPC1 should run");
    selftest_click_through(srv, 12);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 38,
                   "eggs hand-in should write ^natt_grotto (38), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));

    /* Hespori soft-skip + bark. */
    if( tobquest >= 0 )
        ToriRSServer_VarbitSet(srv, tobquest, 40);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_hespori,
                                        ToriRSServer_LocCategory(loc_hespori), -1);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 44,
                   "Hespori ready should write ^natt_bark_chop (44), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));

    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_hespori_dead,
                                        ToriRSServer_LocCategory(loc_hespori_dead), -1);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 46,
                   "dead Hespori should write ^natt_bark (46), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));
    SELFTEST_CHECK(selftest_count(player, obj_bark) >= 1, "dead Hespori should give bark");

    /* Bark hand-in -> ToB. */
    ToriRSServer_WorldTeleport(srv, 0, 3673, 3223);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "bark hand-in OPNPC1 should run");
    selftest_click_through(srv, 12);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 54,
                   "bark hand-in should write ^natt_tob (54), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));

    /* Finish talk -> complete + 4 lamps. */
    if( tobquest >= 0 )
        ToriRSServer_VarbitSet(srv, tobquest, 80);
    selftest_clear_inv(player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "finish OPNPC1 should run");
    selftest_click_through(srv, 16);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 86,
                   "finish should write ^natt_complete (86), got %d",
                   ToriRSServer_VarbitGet(player, tobquest));
    SELFTEST_CHECK(selftest_count(player, obj_lamp) >= 4,
                   "complete should grant 4 antique lamps, got %d",
                   selftest_count(player, obj_lamp));
    if( done_tob >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, done_tob) == 1,
                       "complete should set %%tobquest_done_tob");

    /* Postquest line. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_stranger,
                                                 ToriRSServer_NpcCategory(npc_stranger), slot)
                       == TORIRSSERVER_TRIGGER_RAN,
                   "postquest OPNPC1 should run");
    selftest_click_through(srv, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* Journal complete prints QUEST COMPLETE! */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,nightatthetheatre_journal]", NULL, 0),
                   "nightatthetheatre_journal should run");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* Leftover procs exist and run. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_leftover_crypt_key_puzzle]", NULL, 0),
                   "leftover_crypt_key_puzzle");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_leftover_araxyte_cave_pathing]", NULL, 0),
                   "leftover_araxyte_cave_pathing");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_leftover_daer_kand_full_tree]", NULL, 0),
                   "leftover_daer_kand_full_tree");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_leftover_hespori_fight]", NULL, 0),
                   "leftover_hespori_fight");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_leftover_tob_raid]", NULL, 0),
                   "leftover_tob_raid");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_leftover_cutscene_if]", NULL, 0),
                   "leftover_cutscene_if");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_leftover_lamp_rub_ui]", NULL, 0),
                   "leftover_lamp_rub_ui");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,natt_leftover_full_refuse_trees]", NULL, 0),
                   "leftover_full_refuse_trees");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* ::nightatthetheatre reset + ::nattrun headless walk. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "nightatthetheatre") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "::nightatthetheatre should reach content");
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 0,
                   "::nightatthetheatre should reset to not-started, got %d",
                   ToriRSServer_VarbitGet(player, tobquest));

    natt_ok = 0;
    ToriRSServer_CaptureBegin(srv, &natt_cap);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "nattrun") == TORIRSSERVER_TRIGGER_RAN,
                   "::nattrun should reach content");
    ToriRSServer_CaptureEnd(srv);
    for( i = ToriRSServer_CaptureFindNamed(&natt_cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(&natt_cap, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        const char* text = selftest_message_text(srv, &natt_cap.packets[i]);

        if( !text )
            continue;
        if( strstr(text, "nattrun OK") )
            natt_ok = 1;
    }
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(natt_ok, "::nattrun should reach its OK line");
    SELFTEST_CHECK(tobquest >= 0 && ToriRSServer_VarbitGet(player, tobquest) == 86,
                   "::nattrun should complete at 86, got %d",
                   ToriRSServer_VarbitGet(player, tobquest));

    if( slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot);
    ToriRSServer_WorldNpcReap(srv);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    player->godmode = 1;

    fprintf(stderr, "ToriRSServer NATT selftest: %lu checks, %d failures\n",
            g_selftest_checks, g_selftest_failures);
    return g_selftest_failures;

natt_selftest_skip:;
}
