/* Throne of Miscellania Gate D stanza. Included from
 * torirs_server_world_selftest.c immediately before a selftest_reset_world
 * so spawned npcs cannot leak into later RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPHELDU dispatch on the authored path.
 * Both courtship trees (Brand and Astrid) run. Royal Trouble is not started.
 * player->godmode = 1 for the whole walk. ASCII PASS per step.
 */
static void
quest_misc_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "PASS throneofmiscellania %s\n", step);
}

static int
quest_misc_inv_has(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            return 1;
    return 0;
}

static void
quest_misc_inv_clear(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
quest_misc_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
    if( srv->active_player )
        srv->active_player->active_script = NULL;
}

static void
quest_misc_finish_talk(struct ToriRSServer* srv)
{
    assert(srv);
    selftest_click_through(srv, 48);
    quest_misc_close(srv);
}

static int
quest_misc_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
quest_misc_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = quest_misc_chatmenu();
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
quest_misc_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = quest_misc_chatmenu();
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
quest_misc_talk(struct ToriRSServer* srv, int npc_type, int npc_slot)
{
    int ran;

    assert(srv);
    ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    quest_misc_finish_talk(srv);
    return ran;
}

static int
quest_misc_talk_choose(
    struct ToriRSServer* srv,
    int npc_type,
    int npc_slot,
    int row)
{
    int ran;

    assert(srv);
    ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    quest_misc_click_until_menu(srv, 16);
    quest_misc_pick_row(srv, row);
    quest_misc_finish_talk(srv);
    return ran;
}

static void
quest_misc_use_on_npc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot,
    int obj_id)
{
    assert(srv);
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, npc_slot);
    quest_misc_finish_talk(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
quest_misc_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    player->stat_level[TORIRSSERVER_STAT_HITPOINTS] = 99;
    player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS] = 99;
    player->hitpoints = 99;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
selftest_quest_misc(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int varp_misc;
    int varp_hero;
    int varp_viking;
    int bit_told;
    int bit_partner;
    int bit_accepted;
    int bit_approval;
    int bit_affection;
    int bit_audience;
    int bit_royal;
    int bit_coffers;
    int npc_ghrim;
    int npc_vargas;
    int npc_brand;
    int npc_astrid;
    int npc_sigrid;
    int npc_smithy;
    int npc_guard;
    int obj_flowers;
    int obj_cake;
    int obj_ring;
    int obj_bow;
    int obj_awful;
    int obj_good;
    int obj_treaty;
    int obj_nib;
    int obj_pen;
    int obj_iron;
    int obj_logs;
    int obj_rake;
    int slot_ghrim;
    int slot_vargas;
    int slot_brand;
    int slot_astrid;
    int slot_sigrid;
    int slot_smithy;
    int slot_guard;
    int drain;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: throneofmiscellania\n");

    if( !player->active || !player->world )
    {
        player = ToriRSServer_WorldAddPlayer(srv, NULL);
        assert(player);
        assert(player->world);
    }
    selftest_reset_world(srv, player, 402, 402);
    player = srv->active_player;
    assert(player);
    assert(player->world);

    quest_misc_god(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  throneofmiscellania no compiled script pack\n");
        return;
    }

    varp_misc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "misc_quest");
    varp_hero = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "heroquest");
    varp_viking = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "viking");
    bit_told = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "misc_toldking");
    bit_partner = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "misc_partner_multivar");
    bit_accepted = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "misc_acceptedtorule");
    bit_approval = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "misc_approval");
    bit_affection = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "misc_affection");
    bit_audience = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "misc_grantedaudience");
    bit_royal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "royal_quest");
    bit_coffers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "misc_coffers");
    npc_ghrim = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "misc_advisor_ghrim");
    npc_vargas = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "misc_king_vargas");
    npc_brand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "misc_prince_brand");
    npc_astrid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "misc_princess_astrid");
    npc_sigrid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "misc_queen_sigrid");
    npc_smithy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "misc_smithy");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "misc_ulby_doorguard");
    obj_flowers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "flowers_waterfall_quest");
    obj_cake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cake");
    obj_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gold_ring");
    obj_bow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "shortbow");
    obj_awful = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "misc_awful_anthem");
    obj_good = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "misc_good_anthem");
    obj_treaty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "misc_treaty");
    obj_nib = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "misc_giant_nib");
    obj_pen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "misc_giant_pen");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_rake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rake");

    SELFTEST_CHECK(varp_misc >= 0, "misc_quest should resolve");
    SELFTEST_CHECK(varp_hero >= 0, "heroquest should resolve");
    SELFTEST_CHECK(bit_told >= 0, "misc_toldking should resolve");
    SELFTEST_CHECK(bit_partner >= 0, "misc_partner_multivar should resolve");
    SELFTEST_CHECK(bit_accepted >= 0, "misc_acceptedtorule should resolve");
    SELFTEST_CHECK(bit_approval >= 0, "misc_approval should resolve");
    SELFTEST_CHECK(bit_affection >= 0, "misc_affection should resolve");
    SELFTEST_CHECK(bit_audience >= 0, "misc_grantedaudience should resolve");
    SELFTEST_CHECK(npc_ghrim >= 0, "misc_advisor_ghrim should resolve");
    SELFTEST_CHECK(npc_vargas >= 0, "misc_king_vargas should resolve");
    SELFTEST_CHECK(npc_brand >= 0, "misc_prince_brand should resolve");
    SELFTEST_CHECK(npc_astrid >= 0, "misc_princess_astrid should resolve");
    SELFTEST_CHECK(npc_sigrid >= 0, "misc_queen_sigrid should resolve");
    SELFTEST_CHECK(npc_smithy >= 0, "misc_smithy should resolve");
    SELFTEST_CHECK(npc_guard >= 0, "misc_ulby_doorguard should resolve");
    SELFTEST_CHECK(obj_flowers >= 0, "flowers_waterfall_quest should resolve");
    SELFTEST_CHECK(obj_cake >= 0, "cake should resolve");
    SELFTEST_CHECK(obj_ring >= 0, "gold_ring should resolve");
    SELFTEST_CHECK(obj_bow >= 0, "shortbow should resolve");
    SELFTEST_CHECK(obj_awful >= 0, "misc_awful_anthem should resolve");
    SELFTEST_CHECK(obj_good >= 0, "misc_good_anthem should resolve");
    SELFTEST_CHECK(obj_treaty >= 0, "misc_treaty should resolve");
    SELFTEST_CHECK(obj_nib >= 0, "misc_giant_nib should resolve");
    SELFTEST_CHECK(obj_pen >= 0, "misc_giant_pen should resolve");
    SELFTEST_CHECK(obj_iron >= 0, "iron_bar should resolve");
    SELFTEST_CHECK(obj_logs >= 0, "logs should resolve");
    SELFTEST_CHECK(obj_rake >= 0, "rake should resolve");
    if( varp_misc < 0 || npc_ghrim < 0 || npc_vargas < 0 || npc_brand < 0 ||
        npc_astrid < 0 || npc_sigrid < 0 || npc_smithy < 0 || npc_guard < 0 )
        return;

    quest_misc_inv_clear(player);
    player->varps[varp_misc] = 0;
    if( varp_hero >= 0 )
        player->varps[varp_hero] = 0;
    if( varp_viking >= 0 )
        player->varps[varp_viking] = 0;
    if( bit_told >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, bit_told, 0);
    if( bit_partner >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, bit_partner, 0);
    if( bit_accepted >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, bit_accepted, 0);
    if( bit_approval >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, bit_approval, 0);
    if( bit_affection >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, bit_affection, 0);
    if( bit_audience >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, bit_audience, 0);
    if( bit_royal >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, bit_royal, 0);

    ToriRSServer_WorldTeleport(srv, 1, 2501, 3858);
    selftest_tick(srv);

    slot_ghrim = ToriRSServer_WorldNpcSpawn(srv, npc_ghrim, 2499, 3857, 1);
    slot_vargas = ToriRSServer_WorldNpcSpawn(srv, npc_vargas, 2501, 3859, 1);
    slot_brand = ToriRSServer_WorldNpcSpawn(srv, npc_brand, 2502, 3852, 1);
    slot_astrid = ToriRSServer_WorldNpcSpawn(srv, npc_astrid, 2502, 3868, 1);
    slot_sigrid = ToriRSServer_WorldNpcSpawn(srv, npc_sigrid, 2612, 3877, 1);
    slot_smithy = ToriRSServer_WorldNpcSpawn(srv, npc_smithy, 2551, 3897, 0);
    slot_guard = ToriRSServer_WorldNpcSpawn(srv, npc_guard, 2505, 3856, 1);
    SELFTEST_CHECK(slot_ghrim >= 0, "ghrim should spawn");
    SELFTEST_CHECK(slot_vargas >= 0, "vargas should spawn");
    SELFTEST_CHECK(slot_brand >= 0, "brand should spawn");
    SELFTEST_CHECK(slot_astrid >= 0, "astrid should spawn");
    SELFTEST_CHECK(slot_sigrid >= 0, "sigrid should spawn");
    SELFTEST_CHECK(slot_smithy >= 0, "smithy should spawn");
    SELFTEST_CHECK(slot_guard >= 0, "door guard should spawn");
    if( slot_ghrim < 0 || slot_vargas < 0 || slot_brand < 0 || slot_astrid < 0 ||
        slot_sigrid < 0 || slot_smithy < 0 || slot_guard < 0 )
        return;

    /* Journal not-started -- debugproc mounts questjournal. */
    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunDebugproc(srv, "miscjournal") == TORIRSSERVER_TRIGGER_RAN,
        "journal not-started debugproc should run");
    quest_misc_close(srv);
    quest_misc_pass("journal_not_started");

    /* Door guard: Heroes' Quest not complete -- no audience. */
    quest_misc_talk(srv, npc_guard, slot_guard);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_audience) == 0,
                   "guard without Heroes' Quest must not grant audience, got %d",
                   ToriRSServer_VarbitGet(player, bit_audience));
    quest_misc_pass("talk_door_guard_heroes_gate");

    /* Door guard: Heroes' Quest complete -- admit. */
    player->varps[varp_hero] = 15;
    quest_misc_talk(srv, npc_guard, slot_guard);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_audience) == 1,
                   "door guard should grant audience, got %d",
                   ToriRSServer_VarbitGet(player, bit_audience));
    quest_misc_pass("talk_door_guard_admit");

    /* Pre-quest Ghrim kingdom status -- must not start ToM or RT. */
    quest_misc_talk(srv, npc_ghrim, slot_ghrim);
    SELFTEST_CHECK(player->varps[varp_misc] == 0,
                   "pre-quest Ghrim talk must not start the quest, got %d",
                   player->varps[varp_misc]);
    if( bit_royal >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_royal) == 0,
                       "pre-quest Ghrim must not start Royal Trouble, royal_quest=%d",
                       ToriRSServer_VarbitGet(player, bit_royal));
    quest_misc_pass("talk_ghrim_kingdom_status");

    /* Vargas decline (choice row 3). */
    quest_misc_talk_choose(srv, npc_vargas, slot_vargas, 3);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_told) == 0,
                   "Vargas decline must leave misc_toldking at 0, got %d",
                   ToriRSServer_VarbitGet(player, bit_told));
    quest_misc_pass("talk_vargas_decline");

    /* Vargas accept Brand (choice row 1). */
    quest_misc_talk_choose(srv, npc_vargas, slot_vargas, 1);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_told) == 1,
                   "Vargas Brand offer should set misc_toldking, got %d",
                   ToriRSServer_VarbitGet(player, bit_told));
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_partner) == 1,
                   "choice row 1 should court Brand, partner=%d",
                   ToriRSServer_VarbitGet(player, bit_partner));
    quest_misc_pass("talk_vargas_accept_brand");

    /* Brand courtship: talk / flowers / talk / cake / talk / ring. */
    quest_misc_talk(srv, npc_brand, slot_brand);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 10,
                   "Brand talk1 should set affection s1_step0 (10), got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_brand_talk1");

    quest_misc_talk(srv, npc_brand, slot_brand);
    quest_misc_pass("court_brand_need_flowers");

    inv_set(player, 0, obj_flowers, 1);
    quest_misc_use_on_npc(srv, player, npc_brand, slot_brand, obj_flowers);
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_flowers),
                   "Brand should accept flowers");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 15,
                   "flowers should set affection s1_step5 (15), got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_brand_flowers");

    quest_misc_talk(srv, npc_brand, slot_brand);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 20,
                   "Brand talk2 should set affection s2_step0 (20), got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_brand_talk2");

    inv_set(player, 0, obj_cake, 1);
    quest_misc_use_on_npc(srv, player, npc_brand, slot_brand, obj_cake);
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_cake), "Brand should accept cake");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 24,
                   "cake should set affection s2_step4 (24), got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_brand_cake");

    quest_misc_talk(srv, npc_brand, slot_brand);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 30,
                   "Brand talk3/song should set affection s3_step0 (30), got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_brand_songs");

    inv_set(player, 0, obj_ring, 1);
    quest_misc_use_on_npc(srv, player, npc_brand, slot_brand, obj_ring);
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_ring), "Brand should accept the ring");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_accepted) == 1,
                   "Brand ring should set misc_acceptedtorule");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 40,
                   "Brand ring should complete affection (40), got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_brand_ring");

    quest_misc_talk(srv, npc_brand, slot_brand);
    quest_misc_pass("court_brand_married");

    /* Reset courtship and walk Princess Astrid's full tree. */
    ToriRSServer_VarbitSetOn(srv, player, bit_accepted, 0);
    ToriRSServer_VarbitSetOn(srv, player, bit_affection, 0);
    ToriRSServer_VarbitSetOn(srv, player, bit_partner, 0);
    ToriRSServer_VarbitSetOn(srv, player, bit_told, 1);
    quest_misc_inv_clear(player);

    quest_misc_talk(srv, npc_astrid, slot_astrid);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 10,
                   "Astrid talk1 should set affection s1_step0 (10), got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_astrid_talk1");

    quest_misc_talk(srv, npc_astrid, slot_astrid);
    quest_misc_pass("court_astrid_need_flowers");

    inv_set(player, 0, obj_flowers, 1);
    quest_misc_use_on_npc(srv, player, npc_astrid, slot_astrid, obj_flowers);
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_flowers),
                   "Astrid should accept flowers");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 15,
                   "Astrid flowers should set affection 15, got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_astrid_flowers");

    quest_misc_talk(srv, npc_astrid, slot_astrid);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 20,
                   "Astrid talk2 should set affection 20, got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_astrid_talk2");

    inv_set(player, 0, obj_bow, 1);
    quest_misc_use_on_npc(srv, player, npc_astrid, slot_astrid, obj_bow);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 24,
                   "Astrid bow should set affection 24, got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_astrid_bow");

    quest_misc_talk(srv, npc_astrid, slot_astrid);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 30,
                   "Astrid talk3 should set affection 30, got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_astrid_kiss");

    inv_set(player, 0, obj_ring, 1);
    quest_misc_use_on_npc(srv, player, npc_astrid, slot_astrid, obj_ring);
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_ring), "Astrid should accept the ring");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_accepted) == 1,
                   "Astrid ring should set misc_acceptedtorule");
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_affection) == 40,
                   "Astrid ring should complete affection (40), got %d",
                   ToriRSServer_VarbitGet(player, bit_affection));
    quest_misc_pass("court_astrid_ring");

    /* Vargas after courting -> 10. */
    quest_misc_talk(srv, npc_vargas, slot_vargas);
    SELFTEST_CHECK(player->varps[varp_misc] == 10,
                   "Vargas after courting should set misc_quest=10, got %d",
                   player->varps[varp_misc]);
    if( bit_royal >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_royal) == 0,
                       "mid-ToM Vargas must not start Royal Trouble");
    quest_misc_pass("talk_vargas_after_court");

    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunDebugproc(srv, "miscjournal") == TORIRSSERVER_TRIGGER_RAN,
        "journal stage 10 debugproc should run");
    quest_misc_close(srv);
    quest_misc_pass("journal_stage_10");

    /* Sigrid demand -> 20. */
    ToriRSServer_WorldTeleport(srv, 1, 2612, 3877);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_sigrid, slot_sigrid);
    SELFTEST_CHECK(player->varps[varp_misc] == 20,
                   "Sigrid demand should set misc_quest=20, got %d",
                   player->varps[varp_misc]);
    quest_misc_pass("talk_sigrid_demand");

    /* Vargas anthem condition -> 30. */
    ToriRSServer_WorldTeleport(srv, 1, 2501, 3859);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_vargas, slot_vargas);
    SELFTEST_CHECK(player->varps[varp_misc] == 30,
                   "Vargas anthem condition should set misc_quest=30, got %d",
                   player->varps[varp_misc]);
    quest_misc_pass("talk_vargas_anthem");

    /* Sigrid told of anthem -> 40. */
    ToriRSServer_WorldTeleport(srv, 1, 2612, 3877);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_sigrid, slot_sigrid);
    SELFTEST_CHECK(player->varps[varp_misc] == 40,
                   "Sigrid anthem accept should set misc_quest=40, got %d",
                   player->varps[varp_misc]);
    quest_misc_pass("talk_sigrid_anthem");

    /* Brand composes awful anthem -> 50. */
    ToriRSServer_WorldTeleport(srv, 1, 2502, 3852);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_brand, slot_brand);
    SELFTEST_CHECK(player->varps[varp_misc] == 50,
                   "Brand anthem should set misc_quest=50, got %d",
                   player->varps[varp_misc]);
    SELFTEST_CHECK(quest_misc_inv_has(player, obj_awful),
                   "Brand should grant misc_awful_anthem");
    quest_misc_pass("talk_brand_anthem");

    /* Ghrim corrects anthem -> 60. */
    ToriRSServer_WorldTeleport(srv, 1, 2499, 3857);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_ghrim, slot_ghrim);
    SELFTEST_CHECK(player->varps[varp_misc] == 60,
                   "Ghrim correction should set misc_quest=60, got %d",
                   player->varps[varp_misc]);
    SELFTEST_CHECK(quest_misc_inv_has(player, obj_good),
                   "Ghrim should grant misc_good_anthem");
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_awful),
                   "Ghrim should consume misc_awful_anthem");
    quest_misc_pass("talk_ghrim_anthem");

    /* Sigrid takes anthem, grants treaty -> 70. */
    ToriRSServer_WorldTeleport(srv, 1, 2612, 3877);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_sigrid, slot_sigrid);
    SELFTEST_CHECK(player->varps[varp_misc] == 70,
                   "Sigrid treaty should set misc_quest=70, got %d",
                   player->varps[varp_misc]);
    SELFTEST_CHECK(quest_misc_inv_has(player, obj_treaty),
                   "Sigrid should grant misc_treaty");
    quest_misc_pass("talk_sigrid_treaty");

    /* Vargas receives treaty -> 80. */
    ToriRSServer_WorldTeleport(srv, 1, 2501, 3859);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_vargas, slot_vargas);
    SELFTEST_CHECK(player->varps[varp_misc] == 80,
                   "Vargas receive treaty should set misc_quest=80, got %d",
                   player->varps[varp_misc]);
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_treaty),
                   "Vargas should consume misc_treaty");
    quest_misc_pass("talk_vargas_treaty");

    /* Derrik without iron bar -- real opnpc1, no nib yet. */
    ToriRSServer_WorldTeleport(srv, 0, 2551, 3897);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_smithy, slot_smithy);
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_nib),
                   "Derrik without an iron bar must not grant a nib");
    quest_misc_pass("talk_derrik_need_iron");

    /* Derrik forges nib from iron bar. */
    inv_set(player, 0, obj_iron, 1);
    quest_misc_talk(srv, npc_smithy, slot_smithy);
    SELFTEST_CHECK(quest_misc_inv_has(player, obj_nib),
                   "Derrik should grant misc_giant_nib");
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_iron),
                   "Derrik should consume the iron bar");
    quest_misc_pass("talk_derrik_nib");

    /* Use nib on logs -- real opheldu. */
    inv_set(player, 1, obj_logs, 1);
    player->last_useitem = obj_logs;
    player->last_useslot = 1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_nib, -1, 0);
    quest_misc_finish_talk(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
    SELFTEST_CHECK(quest_misc_inv_has(player, obj_pen),
                   "opheldu nib+logs should grant misc_giant_pen");
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_nib),
                   "opheldu should consume misc_giant_nib");
    quest_misc_pass("opheldu_giant_pen");

    /* Vargas signs with the pen -> 90. */
    ToriRSServer_WorldTeleport(srv, 1, 2501, 3859);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_vargas, slot_vargas);
    SELFTEST_CHECK(player->varps[varp_misc] == 90,
                   "Vargas sign should set misc_quest=90, got %d",
                   player->varps[varp_misc]);
    SELFTEST_CHECK(!quest_misc_inv_has(player, obj_pen),
                   "Vargas should consume misc_giant_pen");
    if( bit_royal >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_royal) == 0,
                       "signing the treaty must not start Royal Trouble");
    quest_misc_pass("talk_vargas_sign");

    /* Ghrim 75% support without a tool -- narration of the missing item. */
    quest_misc_inv_clear(player);
    ToriRSServer_WorldTeleport(srv, 1, 2499, 3857);
    selftest_tick(srv);
    quest_misc_talk(srv, npc_ghrim, slot_ghrim);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_approval) < 96,
                   "Ghrim without a tool must not set 75%% support, got %d",
                   ToriRSServer_VarbitGet(player, bit_approval));
    SELFTEST_CHECK(player->varps[varp_misc] == 90,
                   "no-tool support talk must leave misc_quest at 90, got %d",
                   player->varps[varp_misc]);
    quest_misc_pass("talk_ghrim_support_no_tool");

    /* Ghrim 75% support -- narrated kingdom work with a rake. */
    inv_set(player, 0, obj_rake, 1);
    quest_misc_talk(srv, npc_ghrim, slot_ghrim);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_approval) >= 96,
                   "Ghrim support should set approval >= 96, got %d",
                   ToriRSServer_VarbitGet(player, bit_approval));
    SELFTEST_CHECK(player->varps[varp_misc] == 90,
                   "support talk must leave misc_quest at 90, got %d",
                   player->varps[varp_misc]);
    if( bit_royal >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_royal) == 0,
                       "Ghrim support must not start Royal Trouble");
    quest_misc_pass("talk_ghrim_support");

    /* Vargas completion -- real opnpc1, writes 100 and queues the scroll. */
    ToriRSServer_WorldTeleport(srv, 1, 2501, 3859);
    selftest_tick(srv);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_vargas, -1, slot_vargas);
    selftest_click_through(srv, 48);
    for( drain = 0; drain < 48 && player->varps[varp_misc] != 100; drain++ )
    {
        if( player->active_script && player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
    SELFTEST_CHECK(player->varps[varp_misc] == 100,
                   "Vargas finish should set misc_quest=100, got %d",
                   player->varps[varp_misc]);
    if( bit_coffers >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_coffers) >= 10000,
                       "completion should add 10000 to coffers, got %d",
                       ToriRSServer_VarbitGet(player, bit_coffers));
    if( bit_royal >= 0 )
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_royal) == 0,
                       "ToM finish must not start Royal Trouble");
    quest_misc_pass("talk_vargas_complete");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunDebugproc(srv, "miscbmp_128_quest_complete_scroll") ==
                       TORIRSSERVER_TRIGGER_RAN,
                   "quest complete scroll debugproc should run");
    quest_misc_close(srv);
    quest_misc_pass("quest_complete_scroll");

    SELFTEST_CHECK(
        ToriRSServer_ScriptsRunDebugproc(srv, "miscjournal") == TORIRSSERVER_TRIGGER_RAN,
        "journal complete debugproc should run");
    quest_misc_close(srv);
    quest_misc_pass("journal_complete");

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "ToM walk must leave the player unkillable and alive");
    SELFTEST_CHECK(player->stat_level[TORIRSSERVER_STAT_HITPOINTS] > 0,
                   "player must be alive at the end of the ToM walk");
    quest_misc_pass("player_alive");

    ToriRSServer_WorldNpcFree(srv, slot_ghrim);
    ToriRSServer_WorldNpcFree(srv, slot_vargas);
    ToriRSServer_WorldNpcFree(srv, slot_brand);
    ToriRSServer_WorldNpcFree(srv, slot_astrid);
    ToriRSServer_WorldNpcFree(srv, slot_sigrid);
    ToriRSServer_WorldNpcFree(srv, slot_smithy);
    ToriRSServer_WorldNpcFree(srv, slot_guard);
    ToriRSServer_WorldNpcReap(srv);
    quest_misc_inv_clear(player);
}
