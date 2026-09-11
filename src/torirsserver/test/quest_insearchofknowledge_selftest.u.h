/* In Search of Knowledge (miniquest) Gate D C-walk.
 *
 * Include immediately before the shop stanza. Gate with
 * TORIRSSERVER_SELFTEST_ISOK_ONLY=1 for a focused run.
 *
 * Player is unkillable: god 1 / TORIRSSERVER_GOD=1 / player->godmode = 1 /
 * TORIRS_PLUGINS=0. Required pointers are assert(); allocation failure is
 * assert.
 */
#ifndef TORIRSSERVER_QUEST_ISOK_SELFTEST_U_H
#define TORIRSSERVER_QUEST_ISOK_SELFTEST_U_H

static void
isok_selftest_clear_modals(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsFree(srv);
}

static void
isok_selftest_click_to_choice(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int chatmenu)
{
    int clicks;

    assert(srv);
    assert(player);
    for( clicks = 0; clicks < 16 && player->active_script; clicks++ )
    {
        int uid;
        uint8_t resume[4];

        if( player->resume_button_count <= 0 )
            break;
        uid = player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            return;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
    }
}

static void
isok_selftest_pick_choice(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    SELFTEST_CHECK(chatmenu > 0, "chatmenu:options should resolve");
    isok_selftest_click_to_choice(srv, player, chatmenu);
    SELFTEST_CHECK(player->active_script != NULL, "Aimeri offer should park on p_choice2");
    if( chatmenu > 0 && player->resume_button_count > 0 )
        SELFTEST_CHECK(player->resume_buttons[0] == chatmenu,
                       "Aimeri offer should arm chatmenu:options");
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_click_through(srv, 8);
    isok_selftest_clear_modals(srv, player);
}

static void
selftest_quest_insearchofknowledge(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int vb_quest;
    int vb_feed;
    int vb_sun_pages;
    int vb_moon_pages;
    int vb_temple_pages;
    int vb_sun_ret;
    int vb_moon_ret;
    int vb_temple_ret;
    int obj_lobster;
    int obj_bread;
    int obj_sun_tome;
    int obj_moon_tome;
    int obj_temple_tome;
    int obj_sun_page;
    int obj_moon_page;
    int obj_temple_page;
    int obj_lamp;
    int npc_injured;
    int npc_healed;
    int npc_logosia;
    int loc_temple;
    int loc_sun;
    int loc_moon;
    int injured_slot;
    int healed_slot;
    int logosia_slot;
    int i;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: In Search of Knowledge\n");

    setenv("TORIRSSERVER_GOD", "1", 1);
    setenv("TORIRS_PLUGINS", "0", 1);
    player->godmode = 1;

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    selftest_reset_world(srv, player, 1840, 9926);
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    vb_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosdun_knowledge_search");
    vb_feed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosdun_aimeri_status");
    vb_sun_pages = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosdun_sun_pages");
    vb_moon_pages = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosdun_moon_pages");
    vb_temple_pages = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosdun_temple_pages");
    vb_sun_ret = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosdun_sun_tome_returned");
    vb_moon_ret = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosdun_moon_tome_returned");
    vb_temple_ret =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hosdun_temple_tome_returned");
    obj_lobster = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lobster");
    obj_bread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread");
    obj_sun_tome = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosdun_sun_tome");
    obj_moon_tome = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosdun_moon_tome");
    obj_temple_tome = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosdun_temple_tome");
    obj_sun_page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosdun_sun_page");
    obj_moon_page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosdun_moon_page");
    obj_temple_page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hosdun_temple_page");
    obj_lamp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thosf_reward_lamp");
    npc_injured = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hosdun_aimeri_injured");
    npc_healed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hosdun_aimeri_healed");
    npc_logosia = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "arceuus_library_librarian");
    loc_temple = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosdun_temple_bookcase");
    loc_sun = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosdun_sun_bookcase");
    loc_moon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hosdun_moon_bookcase");

    SELFTEST_CHECK(vb_quest >= 0 && vb_feed >= 0 && obj_lobster >= 0 && obj_bread >= 0 &&
                       obj_sun_tome >= 0 && obj_moon_tome >= 0 && obj_temple_tome >= 0 &&
                       obj_sun_page >= 0 && obj_moon_page >= 0 && obj_temple_page >= 0 &&
                       obj_lamp >= 0 && npc_injured >= 0 && npc_healed >= 0 && npc_logosia >= 0 &&
                       loc_temple >= 0 && loc_sun >= 0 && loc_moon >= 0,
                   "In Search of Knowledge cache names should all resolve");
    if( vb_quest < 0 || vb_feed < 0 || obj_lobster < 0 || npc_injured < 0 || npc_healed < 0 )
        return;

    ToriRSServer_VarbitSet(srv, vb_quest, 0);
    ToriRSServer_VarbitSet(srv, vb_feed, 0);
    if( vb_sun_pages >= 0 )
        ToriRSServer_VarbitSet(srv, vb_sun_pages, 0);
    if( vb_moon_pages >= 0 )
        ToriRSServer_VarbitSet(srv, vb_moon_pages, 0);
    if( vb_temple_pages >= 0 )
        ToriRSServer_VarbitSet(srv, vb_temple_pages, 0);
    if( vb_sun_ret >= 0 )
        ToriRSServer_VarbitSet(srv, vb_sun_ret, 0);
    if( vb_moon_ret >= 0 )
        ToriRSServer_VarbitSet(srv, vb_moon_ret, 0);
    if( vb_temple_ret >= 0 )
        ToriRSServer_VarbitSet(srv, vb_temple_ret, 0);
    selftest_clear_inv(player);
    player->godmode = 1;

    injured_slot = ToriRSServer_WorldNpcSpawn(srv, npc_injured, player->x + 1, player->z, player->level);
    SELFTEST_CHECK(injured_slot >= 0, "injured Brother Aimeri should spawn");
    if( injured_slot < 0 )
        return;

    /* Hungry talk does not start the miniquest. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_injured, -1,
                                                  injured_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,hosdun_aimeri_injured] should run");
    selftest_click_through(srv, 8);
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 0,
                   "hungry Talk-to must not write ^isok_tomes, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));

    /* Wrong food is refused and not consumed. */
    selftest_give(player, obj_bread, 1);
    player->last_useitem = obj_bread;
    player->last_useslot = 0;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_injured, -1,
                                                  injured_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpcu,hosdun_aimeri_injured] should run on bread");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(selftest_count(player, obj_bread) == 1,
                   "bread must stay in the backpack, count %d", selftest_count(player, obj_bread));
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_feed) == 0,
                   "wrong food must not increment %%hosdun_aimeri_status, got %d",
                   ToriRSServer_VarbitGet(player, vb_feed));

    /* Five valid feeds, one at a time. */
    for( i = 1; i <= 5; i++ )
    {
        selftest_clear_inv(player);
        selftest_give(player, obj_lobster, 1);
        player->last_useitem = obj_lobster;
        player->last_useslot = 0;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_injured, -1,
                                                      injured_slot) == TORIRSSERVER_TRIGGER_RAN,
                       "feed %d should run [opnpcu,hosdun_aimeri_injured]", i);
        isok_selftest_clear_modals(srv, player);
        SELFTEST_CHECK(selftest_count(player, obj_lobster) == 0,
                       "feed %d should consume the lobster", i);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_feed) == i,
                       "feed %d should write %%hosdun_aimeri_status=%d, got %d", i, i,
                       ToriRSServer_VarbitGet(player, vb_feed));
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 0,
                       "feeding must not auto-start the miniquest at feed %d, got %d", i,
                       ToriRSServer_VarbitGet(player, vb_quest));
    }

    /* A sixth feed is refused. */
    selftest_clear_inv(player);
    selftest_give(player, obj_lobster, 1);
    player->last_useitem = obj_lobster;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_injured, -1, injured_slot);
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(selftest_count(player, obj_lobster) == 1,
                   "a sixth feed must not consume food, count %d",
                   selftest_count(player, obj_lobster));
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_feed) == 5,
                   "sixth feed must leave status at 5, got %d",
                   ToriRSServer_VarbitGet(player, vb_feed));

    ToriRSServer_WorldNpcFree(srv, injured_slot);
    ToriRSServer_WorldNpcReap(srv);

    healed_slot = ToriRSServer_WorldNpcSpawn(srv, npc_healed, player->x + 1, player->z, player->level);
    SELFTEST_CHECK(healed_slot >= 0, "healed Brother Aimeri should spawn");
    if( healed_slot < 0 )
        return;

    /* Refuse path: p_choice2 row 2 does not write ^isok_tomes. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_healed, -1,
                                                  healed_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,hosdun_aimeri_healed] should run for the offer");
    isok_selftest_pick_choice(srv, player, 2);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 0,
                   "Not now. must leave %%hosdun_knowledge_search at 0, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));

    /* Accept path: p_choice2 row 1 writes ^isok_tomes = 1. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_healed, -1,
                                                  healed_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,hosdun_aimeri_healed] should run again for accept");
    isok_selftest_pick_choice(srv, player, 1);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 1,
                   "Yes. must write ^isok_tomes (1), got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));

    /* Shelves grant each tome only after start. */
    selftest_clear_inv(player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_temple, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[oploc1,hosdun_temple_bookcase] should run");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(selftest_count(player, obj_temple_tome) == 1,
                   "the temple shelf should grant hosdun_temple_tome");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_sun, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[oploc1,hosdun_sun_bookcase] should run");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(selftest_count(player, obj_sun_tome) == 1,
                   "the sun shelf should grant hosdun_sun_tome");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_moon, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[oploc1,hosdun_moon_bookcase] should run");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(selftest_count(player, obj_moon_tome) == 1,
                   "the moon shelf should grant hosdun_moon_tome");

    /* Page repair: matching page increments; wrong pairing consumes nothing. */
    selftest_give(player, obj_sun_page, 1);
    player->last_useitem = obj_sun_page;
    player->last_useslot = 1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_sun_tome, -1, -1) ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "[opheldu,hosdun_sun_tome] should run");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(vb_sun_pages < 0 || ToriRSServer_VarbitGet(player, vb_sun_pages) == 1,
                   "a sun page should write %%hosdun_sun_pages=1, got %d",
                   vb_sun_pages < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_sun_pages));
    SELFTEST_CHECK(selftest_count(player, obj_sun_page) == 0, "the matching sun page is consumed");

    selftest_give(player, obj_moon_page, 1);
    player->last_useitem = obj_moon_page;
    player->last_useslot = 1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_sun_tome, -1, -1);
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(selftest_count(player, obj_moon_page) == 1,
                   "a moon page on the sun tome must stay, count %d",
                   selftest_count(player, obj_moon_page));
    SELFTEST_CHECK(vb_sun_pages < 0 || ToriRSServer_VarbitGet(player, vb_sun_pages) == 1,
                   "wrong pairing must not change sun pages, got %d",
                   vb_sun_pages < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_sun_pages));

    if( vb_sun_pages >= 0 )
        ToriRSServer_VarbitSet(srv, vb_sun_pages, 4);
    if( vb_moon_pages >= 0 )
        ToriRSServer_VarbitSet(srv, vb_moon_pages, 4);
    if( vb_temple_pages >= 0 )
        ToriRSServer_VarbitSet(srv, vb_temple_pages, 4);

    logosia_slot =
        ToriRSServer_WorldNpcSpawn(srv, npc_logosia, player->x + 2, player->z, player->level);
    SELFTEST_CHECK(logosia_slot >= 0, "Logosia should spawn");
    if( logosia_slot >= 0 )
    {
        player->last_useitem = obj_sun_tome;
        player->last_useslot = 0;
        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_logosia, -1,
                                                      logosia_slot) == TORIRSSERVER_TRIGGER_RAN,
                       "[opnpcu,arceuus_library_librarian] should run for the sun tome");
        isok_selftest_clear_modals(srv, player);
        SELFTEST_CHECK(selftest_count(player, obj_sun_tome) == 0,
                       "Logosia should take the Tome of the sun");
        SELFTEST_CHECK(vb_sun_ret < 0 || ToriRSServer_VarbitGet(player, vb_sun_ret) == 1,
                       "sun returned bit should be 1, got %d",
                       vb_sun_ret < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_sun_ret));

        player->last_useitem = obj_moon_tome;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_logosia, -1, logosia_slot);
        isok_selftest_clear_modals(srv, player);
        player->last_useitem = obj_temple_tome;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_logosia, -1, logosia_slot);
        isok_selftest_clear_modals(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 2,
                       "returning all three tomes should write ^isok_logosia (2), got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));

        SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_logosia, -1,
                                                      logosia_slot) == TORIRSSERVER_TRIGGER_RAN,
                       "[opnpc1,arceuus_library_librarian] should settle the reward");
        selftest_click_through(srv, 16);
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 3,
                       "Logosia reward talk should write ^isok_complete (3), got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        SELFTEST_CHECK(selftest_count(player, obj_lamp) >= 1,
                       "completion should grant thosf_reward_lamp, count %d",
                       selftest_count(player, obj_lamp));
        ToriRSServer_WorldNpcFree(srv, logosia_slot);
    }

    ToriRSServer_WorldNpcFree(srv, healed_slot);
    ToriRSServer_WorldNpcReap(srv);
    isok_selftest_clear_modals(srv, player);

    /* Headless walk still reaches isokrun OK. */
    {
        static struct ToriRSServerCapture cap;
        int said_ok = 0;

        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "isokrun") == TORIRSSERVER_TRIGGER_RAN,
                       "::isokrun should reach content");
        selftest_click_through(srv, 16);
        ToriRSServer_ScriptsProcessQueues(srv);
        ToriRSServer_WorldCloseModal(srv);
        ToriRSServer_CaptureEnd(srv);
        for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
             i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const char* text = selftest_message_text(srv, &cap.packets[i]);

            if( !text )
                continue;
            if( strstr(text, "isokrun OK") != NULL )
                said_ok = 1;
        }
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == 3,
                       "::isokrun should settle ^isok_complete (3), got %d",
                       ToriRSServer_VarbitGet(player, vb_quest));
        SELFTEST_CHECK(selftest_count(player, obj_lamp) >= 1,
                       "::isokrun should leave thosf_reward_lamp in the backpack");
        SELFTEST_CHECK(said_ok || ToriRSServer_VarbitGet(player, vb_quest) == 3,
                       "::isokrun should reach its OK line or settle complete");
        if( player->active_script )
            ToriRSServer_ScriptsFree(srv);
    }

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,isok_leftover_forthos_combat_page_drops]",
                                               NULL, 0),
                   "leftover_forthos_combat_page_drops should exist");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,isok_leftover_knife_web_cut]", NULL, 0),
                   "leftover_knife_web_cut should exist");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,isok_leftover_protect_from_magic]", NULL, 0),
                   "leftover_protect_from_magic should exist");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,isok_leftover_lamp_rub_ui]", NULL, 0),
                   "leftover_lamp_rub_ui should exist");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,isok_leftover_full_aimeri_trees]", NULL, 0),
                   "leftover_full_aimeri_trees should exist");
    isok_selftest_clear_modals(srv, player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,isok_leftover_full_refuse_trees]", NULL, 0),
                   "leftover_full_refuse_trees should exist");
    isok_selftest_clear_modals(srv, player);

    player->godmode = 1;
    ToriRSServer_WorldNpcReap(srv);
    selftest_reset_world(srv, player, 402, 402);
}

#endif /* TORIRSSERVER_QUEST_ISOK_SELFTEST_U_H */
