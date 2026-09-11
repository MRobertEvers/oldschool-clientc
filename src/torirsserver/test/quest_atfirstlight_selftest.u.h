/* At First Light C-walk. Not QOT / COTS / GOD_ONLY.
 *
 * Gate: TORIRSSERVER_SELFTEST_AFL_ONLY=1
 * Placed immediately before the shop selftest_reset_world.
 * Godmode on for the whole walk. Player is unkillable (not a death case).
 */

static int
afl_capture_has(
    struct ToriRSServer* srv,
    const struct ToriRSServerCapture* cap,
    const char* needle)
{
    int i;

    assert(srv);
    assert(cap);
    assert(needle);
    for( i = 0; i < cap->count; i++ )
    {
        const struct ToriRSServerCapturedPacket* pk = &cap->packets[i];
        const char* text;

        if( pk->name == PKT_NAME_IF_SETTEXT )
        {
            text = selftest_settext_text(srv, pk);
            if( text && strstr(text, needle) )
                return 1;
        }
        if( pk->name == PKT_NAME_MESSAGE_GAME )
        {
            text = selftest_message_text(srv, pk);
            if( text && strstr(text, needle) )
                return 1;
        }
    }
    return 0;
}

/* Advance chatnpc / mesbox pages only. Stop on p_choice so the caller picks. */
static int
afl_click_pages(
    struct ToriRSServer* srv,
    int max_pages)
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
        int uid;
        uint8_t resume[4];

        if( player->resume_button_count <= 0 )
            break;
        uid = player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            break;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        clicks++;
    }
    return clicks;
}

static void
afl_close(
    struct ToriRSServer* srv)
{
    assert(srv);
    assert(srv->active_player);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    srv->active_player->active_script = NULL;
}

static int
afl_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type)
{
    int slot;

    assert(srv);
    assert(player);
    slot = selftest_require_npc(srv, npc_type, player->x + 1, player->z, player->level);
    if( slot < 0 )
        return TORIRSSERVER_TRIGGER_FAILED;
    player->x = srv->npcs[slot].x + 1;
    player->z = srv->npcs[slot].z;
    player->level = srv->npcs[slot].level;
    player->active_script = NULL;
    return ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
afl_ready(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int vmq1_bit,
    int eagle_bit,
    int afl_bit,
    int hunter,
    int herblore,
    int construction)
{
    assert(srv);
    assert(player);
    assert(vmq1_bit >= 0);
    assert(eagle_bit >= 0);
    assert(afl_bit >= 0);
    assert(hunter >= 0);
    assert(herblore >= 0);
    assert(construction >= 0);
    ToriRSServer_VarbitSet(srv, vmq1_bit, 24);
    ToriRSServer_VarbitSet(srv, eagle_bit, 40);
    ToriRSServer_VarbitSet(srv, afl_bit, 0);
    ToriRSServer_CombatSetLevel(player, hunter, 46);
    ToriRSServer_CombatSetLevel(player, herblore, 30);
    ToriRSServer_CombatSetLevel(player, construction, 27);
}

static void
selftest_quest_atfirstlight(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int vmq1_bit;
    int eagle_bit;
    int afl_bit;
    int hunter;
    int herblore;
    int construction;
    int apatura;
    int verity;
    int wolf;
    int fox;
    int atza;
    int report;
    int fur;
    int fur_prepped;
    int poultice;
    int afl_complete;
    int afl_hunter_xp;
    int afl_con_xp;
    int afl_herb_xp;
    static struct ToriRSServerCapture cap;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: At First Light\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "At First Light loads a compiled script pack");
    if( !loaded )
        return;

    player->godmode = 1;

    vmq1_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1");
    eagle_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eaglepeak_quest");
    afl_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "afl");
    hunter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hunter");
    herblore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
    construction = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    apatura = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hg_apatura");
    verity = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hg_verity");
    wolf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hg_wolf");
    fox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "afl_hunter_fox");
    atza = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "afl_atza");
    report = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "afl_report");
    fur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "afl_fur");
    fur_prepped = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "afl_fur_prepped");
    poultice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "afl_poultice");
    afl_complete = ToriRSServer_ContentConstantInt("afl_complete", -1);
    afl_hunter_xp = ToriRSServer_ContentConstantInt("afl_hunter_xp", -1);
    afl_con_xp = ToriRSServer_ContentConstantInt("afl_con_xp", -1);
    afl_herb_xp = ToriRSServer_ContentConstantInt("afl_herb_xp", -1);

    SELFTEST_CHECK(vmq1_bit >= 0, "vmq1 varbit resolves");
    SELFTEST_CHECK(eagle_bit >= 0, "eaglepeak_quest varbit resolves");
    SELFTEST_CHECK(afl_bit >= 0, "afl varbit resolves");
    SELFTEST_CHECK(hunter == 21, "hunter should be stat 21, got %d", hunter);
    SELFTEST_CHECK(herblore == 15, "herblore should be stat 15, got %d", herblore);
    SELFTEST_CHECK(construction == 22, "construction should be stat 22, got %d", construction);
    SELFTEST_CHECK(apatura > 0, "hg_apatura resolves");
    SELFTEST_CHECK(verity > 0, "hg_verity resolves");
    SELFTEST_CHECK(wolf > 0, "hg_wolf resolves");
    SELFTEST_CHECK(fox > 0, "afl_hunter_fox resolves");
    SELFTEST_CHECK(atza > 0, "afl_atza resolves");
    SELFTEST_CHECK(report > 0, "afl_report resolves");
    SELFTEST_CHECK(fur > 0, "afl_fur resolves");
    SELFTEST_CHECK(fur_prepped > 0, "afl_fur_prepped resolves");
    SELFTEST_CHECK(poultice > 0, "afl_poultice resolves");
    SELFTEST_CHECK(afl_complete == 12, "^afl_complete is 12, got %d", afl_complete);
    SELFTEST_CHECK(afl_hunter_xp == 45000, "^afl_hunter_xp is 45000 tenths, got %d",
                   afl_hunter_xp);
    SELFTEST_CHECK(afl_con_xp == 8000, "^afl_con_xp is 8000 tenths, got %d", afl_con_xp);
    SELFTEST_CHECK(afl_herb_xp == 5000, "^afl_herb_xp is 5000 tenths, got %d", afl_herb_xp);
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,afl_show_qualify_fail]") != NULL,
                   "split qualify proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,afl_leftover_cat_toy_wind_animation]") !=
                       NULL,
                   "leftover_cat_toy_wind_animation proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,afl_leftover_jerboa_catch_rolls]") !=
                       NULL,
                   "leftover_jerboa_catch_rolls proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,afl_leftover_equipment_pile_if]") !=
                       NULL,
                   "leftover_equipment_pile_if proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,afl_leftover_guild_stairs_multilocs]") !=
                       NULL,
                   "leftover_guild_stairs_multilocs proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,afl_leftover_master_rumours]") != NULL,
                   "leftover_master_rumours proc is in the pack");
    SELFTEST_CHECK(SSVM_ProviderGetByName(srv->scripts, "[proc,afl_leftover_full_refuse_trees]") !=
                       NULL,
                   "leftover_full_refuse_trees proc is in the pack");
    if( vmq1_bit < 0 || eagle_bit < 0 || afl_bit < 0 || hunter < 0 || herblore < 0 ||
        construction < 0 || apatura <= 0 )
        return;

    /* --- Qualify fail: Children of the Sun --- */
    {
        ToriRSServer_VarbitSet(srv, vmq1_bit, 0);
        ToriRSServer_VarbitSet(srv, eagle_bit, 40);
        ToriRSServer_VarbitSet(srv, afl_bit, 0);
        ToriRSServer_CombatSetLevel(player, hunter, 46);
        ToriRSServer_CombatSetLevel(player, herblore, 30);
        ToriRSServer_CombatSetLevel(player, construction, 27);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(afl_talk(srv, player, apatura) == TORIRSSERVER_TRIGGER_RAN,
                       "Apatura OPNPC1 runs for CotS fail");
        afl_click_pages(srv, 2);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "Children of the Sun"),
                       "CotS qualify fail names Children of the Sun");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 0,
                       "CotS fail does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Qualify fail: Eagles' Peak --- */
    {
        ToriRSServer_VarbitSet(srv, vmq1_bit, 24);
        ToriRSServer_VarbitSet(srv, eagle_bit, 0);
        ToriRSServer_VarbitSet(srv, afl_bit, 0);
        ToriRSServer_CombatSetLevel(player, hunter, 46);
        ToriRSServer_CombatSetLevel(player, herblore, 30);
        ToriRSServer_CombatSetLevel(player, construction, 27);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(afl_talk(srv, player, apatura) == TORIRSSERVER_TRIGGER_RAN,
                       "Apatura OPNPC1 runs for Eagles' Peak fail");
        afl_click_pages(srv, 2);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "Eagles' Peak"),
                       "Eagles' Peak qualify fail names Eagles' Peak");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 0,
                       "Eagles' Peak fail does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Qualify fail: Hunter 46 --- */
    {
        ToriRSServer_VarbitSet(srv, vmq1_bit, 24);
        ToriRSServer_VarbitSet(srv, eagle_bit, 40);
        ToriRSServer_VarbitSet(srv, afl_bit, 0);
        ToriRSServer_CombatSetLevel(player, hunter, 1);
        ToriRSServer_CombatSetLevel(player, herblore, 30);
        ToriRSServer_CombatSetLevel(player, construction, 27);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(afl_talk(srv, player, apatura) == TORIRSSERVER_TRIGGER_RAN,
                       "Apatura OPNPC1 runs for Hunter fail");
        afl_click_pages(srv, 2);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "Hunter level of 46"),
                       "Hunter qualify fail names Hunter level of 46");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 0,
                       "Hunter fail does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Qualify fail: Herblore 30 --- */
    {
        ToriRSServer_VarbitSet(srv, vmq1_bit, 24);
        ToriRSServer_VarbitSet(srv, eagle_bit, 40);
        ToriRSServer_VarbitSet(srv, afl_bit, 0);
        ToriRSServer_CombatSetLevel(player, hunter, 46);
        ToriRSServer_CombatSetLevel(player, herblore, 1);
        ToriRSServer_CombatSetLevel(player, construction, 27);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(afl_talk(srv, player, apatura) == TORIRSSERVER_TRIGGER_RAN,
                       "Apatura OPNPC1 runs for Herblore fail");
        afl_click_pages(srv, 2);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "Herblore level of 30"),
                       "Herblore qualify fail names Herblore level of 30");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 0,
                       "Herblore fail does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Qualify fail: Construction 27 --- */
    {
        ToriRSServer_VarbitSet(srv, vmq1_bit, 24);
        ToriRSServer_VarbitSet(srv, eagle_bit, 40);
        ToriRSServer_VarbitSet(srv, afl_bit, 0);
        ToriRSServer_CombatSetLevel(player, hunter, 46);
        ToriRSServer_CombatSetLevel(player, herblore, 30);
        ToriRSServer_CombatSetLevel(player, construction, 1);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(afl_talk(srv, player, apatura) == TORIRSSERVER_TRIGGER_RAN,
                       "Apatura OPNPC1 runs for Construction fail");
        afl_click_pages(srv, 2);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "Construction level of 27"),
                       "Construction qualify fail names Construction level of 27");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 0,
                       "Construction fail does not start the quest, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Offer refuse: must not write %afl --- */
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(afl_talk(srv, player, apatura) == TORIRSSERVER_TRIGGER_RAN,
                       "Apatura OPNPC1 runs for refuse");
        afl_click_pages(srv, 6);
        selftest_charter_choose(srv, 2);
        afl_click_pages(srv, 4);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "I'm not sure I have the time for that right now."),
                       "refuse shows the refuse chathead");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 0,
                       "refuse does not write %afl, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Offer accept: Yes writes ^afl_verity=1 --- */
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(afl_talk(srv, player, apatura) == TORIRSSERVER_TRIGGER_RAN,
                       "Apatura OPNPC1 runs for accept");
        afl_click_pages(srv, 6);
        selftest_charter_choose(srv, 1);
        afl_click_pages(srv, 4);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "Great! Point me in her direction"),
                       "accept shows the Yes chathead");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 1,
                       "accept writes ^afl_verity=1, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Verity sends the player to Wolf --- */
    if( verity > 0 )
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 1);
        SELFTEST_CHECK(afl_talk(srv, player, verity) == TORIRSSERVER_TRIGGER_RAN,
                       "Verity OPNPC1 runs at ^afl_verity");
        afl_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 2,
                       "Verity writes ^afl_wolf=2, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Wolf hands over the toy mouse --- */
    if( wolf > 0 )
    {
        int mouse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "poh_toy_mouse_unwound");

        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 2);
        selftest_clear_inv(player);
        SELFTEST_CHECK(afl_talk(srv, player, wolf) == TORIRSSERVER_TRIGGER_RAN,
                       "Wolf OPNPC1 runs at ^afl_wolf");
        afl_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 3,
                       "Wolf writes ^afl_cat=3, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        if( mouse > 0 )
            SELFTEST_CHECK(selftest_count(player, mouse) == 1, "Wolf grants the unwound toy mouse");
        afl_close(srv);
    }

    /* --- Fox asks for a poultice --- */
    if( fox > 0 )
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 4);
        SELFTEST_CHECK(afl_talk(srv, player, fox) == TORIRSSERVER_TRIGGER_RAN,
                       "Fox OPNPC1 runs at ^afl_fox");
        afl_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 5,
                       "Fox writes ^afl_poultice=5, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Fox takes the poultice and sends the player to Atza --- */
    if( fox > 0 && poultice > 0 )
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 5);
        selftest_clear_inv(player);
        selftest_give(player, poultice, 1);
        SELFTEST_CHECK(afl_talk(srv, player, fox) == TORIRSSERVER_TRIGGER_RAN,
                       "Fox OPNPC1 runs with poultice");
        afl_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 7,
                       "poultice hand-in writes ^afl_atza=7, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        SELFTEST_CHECK(selftest_count(player, poultice) == 0, "poultice is consumed");
        if( fur > 0 )
            SELFTEST_CHECK(selftest_count(player, fur) == 1, "Fox grants a fur sample");
        afl_close(srv);
    }

    /* --- Atza asks for a hammer repair --- */
    if( atza > 0 && fur > 0 )
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 7);
        selftest_clear_inv(player);
        selftest_give(player, fur, 1);
        SELFTEST_CHECK(afl_talk(srv, player, atza) == TORIRSSERVER_TRIGGER_RAN,
                       "Atza OPNPC1 runs with fur");
        afl_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 8,
                       "Atza writes ^afl_repair=8, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    /* --- Atza soft-skip repair + trim --- */
    if( atza > 0 )
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 8);
        selftest_clear_inv(player);
        if( fur > 0 )
            selftest_give(player, fur, 1);
        SELFTEST_CHECK(afl_talk(srv, player, atza) == TORIRSSERVER_TRIGGER_RAN,
                       "Atza OPNPC1 runs at repair");
        afl_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 9,
                       "repair writes ^afl_trim=9, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        if( fur_prepped > 0 )
            SELFTEST_CHECK(selftest_count(player, fur_prepped) == 1, "Atza grants trimmed fur");
        afl_close(srv);
    }

    /* --- Fox gives the report --- */
    if( fox > 0 && fur_prepped > 0 && report > 0 )
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 9);
        selftest_clear_inv(player);
        selftest_give(player, fur_prepped, 1);
        SELFTEST_CHECK(afl_talk(srv, player, fox) == TORIRSSERVER_TRIGGER_RAN,
                       "Fox OPNPC1 runs with trimmed fur");
        afl_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 10,
                       "Fox writes ^afl_report=10, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        SELFTEST_CHECK(selftest_count(player, report) == 1, "Fox grants afl_report");
        afl_close(srv);
    }

    /* --- Verity takes the report --- */
    if( verity > 0 && report > 0 )
    {
        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 10);
        selftest_clear_inv(player);
        selftest_give(player, report, 1);
        SELFTEST_CHECK(afl_talk(srv, player, verity) == TORIRSSERVER_TRIGGER_RAN,
                       "Verity OPNPC1 runs with report");
        afl_click_pages(srv, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 11,
                       "Verity writes ^afl_finish=11, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        SELFTEST_CHECK(selftest_count(player, report) == 0, "report is consumed");
        afl_close(srv);
    }

    /* --- Apatura complete + authored rewards --- */
    {
        int xp_h;
        int xp_c;
        int xp_b;

        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        ToriRSServer_VarbitSet(srv, afl_bit, 11);
        xp_h = player->stat_xp_tenths[hunter];
        xp_c = player->stat_xp_tenths[construction];
        xp_b = player->stat_xp_tenths[herblore];
        SELFTEST_CHECK(afl_talk(srv, player, apatura) == TORIRSSERVER_TRIGGER_RAN,
                       "Apatura OPNPC1 runs at finish");
        afl_click_pages(srv, 8);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) == 12,
                       "finish writes ^afl_complete=12, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        SELFTEST_CHECK(player->stat_xp_tenths[hunter] >= xp_h + 45000,
                       "complete awards 45000 Hunter tenths (%d -> %d)",
                       xp_h, player->stat_xp_tenths[hunter]);
        SELFTEST_CHECK(player->stat_xp_tenths[construction] >= xp_c + 8000,
                       "complete awards 8000 Construction tenths (%d -> %d)",
                       xp_c, player->stat_xp_tenths[construction]);
        SELFTEST_CHECK(player->stat_xp_tenths[herblore] >= xp_b + 5000,
                       "complete awards 5000 Herblore tenths (%d -> %d)",
                       xp_b, player->stat_xp_tenths[herblore]);
        afl_close(srv);
    }

    /* --- Journal QUEST COMPLETE! --- */
    {
        ToriRSServer_VarbitSet(srv, afl_bit, 12);
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,atfirstlight_journal]", NULL, 0),
                       "atfirstlight_journal runs");
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "QUEST COMPLETE!"),
                       "complete journal paints QUEST COMPLETE!");
        afl_close(srv);
    }

    /* --- leftover procs paint their named mesboxes --- */
    {
        ToriRSServer_CaptureBegin(srv, &cap);
        SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,afl_leftover_full_refuse_trees]", NULL, 0),
                       "leftover_full_refuse_trees runs");
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(afl_capture_has(srv, &cap, "p_choice2 Yes / No"),
                       "leftover_full_refuse_trees names the Yes/No offer");
        afl_close(srv);
    }

    /* --- ::aflrun headless walk --- */
    {
        int aflrun_ok = 0;

        afl_ready(srv, player, vmq1_bit, eagle_bit, afl_bit, hunter, herblore, construction);
        selftest_clear_inv(player);
        ToriRSServer_CaptureBegin(srv, &cap);
        ToriRSServer_ScriptsRunDebugproc(srv, "aflrun");
        ToriRSServer_CaptureEnd(srv);
        {
            int i;

            for( i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
                 i = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_GAME, i + 1) )
            {
                const char* text = selftest_message_text(srv, &cap.packets[i]);

                if( text && strstr(text, "aflrun OK") )
                    aflrun_ok = 1;
            }
        }
        SELFTEST_CHECK(aflrun_ok, "::aflrun should reach its OK line");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, afl_bit) >= 12,
                       "::aflrun leaves ^afl_complete, got %d",
                       ToriRSServer_VarbitGet(player, afl_bit));
        afl_close(srv);
    }

    player->godmode = 1;
}
