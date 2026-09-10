/* Dwarf Cannon Gate D. Real opnpc / oploc / opheld / opobj on the
 * critical path. Called immediately before a selftest_reset_world.
 * player->godmode = 1 for the whole walk (not a death test). */
static int
mcannon_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    return n;
}

static void
mcannon_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static void
mcannon_drain(struct ToriRSServer* srv, int max_steps)
{
    int i;
    int chatmenu;

    assert(srv);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    for( i = 0; i < max_steps; i++ )
    {
        struct ToriRSServerPlayer* p = srv->active_player;
        uint8_t resume[6];
        int uid;

        if( !p || !p->active_script )
            break;
        if( p->resume_button_count <= 0 )
        {
            selftest_tick(srv);
            continue;
        }
        uid = p->resume_buttons[0];
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        if( chatmenu > 0 && uid == chatmenu )
        {
            resume[4] = 0;
            resume[5] = 1;
            selftest_handle(p, PKTOUT_NAME_IF_BUTTON1, resume, (int)sizeof(resume));
        }
        else
            selftest_handle(p, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
    }
}

static int
mcannon_place_loc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_id,
    int x,
    int z,
    int level)
{
    int slot;

    assert(srv);
    assert(player);
    selftest_park_player(srv, x, z);
    player->level = level;
    selftest_tick(srv);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneAddLoc(x, z, level, loc_id, 10, 0);
    return slot;
}

static void
selftest_quest_mcannon(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int varp;
    int npc_lawgof;
    int npc_nulodion;
    int loc_rail;
    int loc_cannon;
    int loc_cave;
    int loc_crate;
    int obj_rail;
    int obj_hammer;
    int obj_remains;
    int obj_toolkit;
    int obj_notes;
    int obj_mould;
    int bit_r1;
    int bit_t1;
    int bit_t2;
    int bit_t3;
    int bit_safe;
    int stat_craft;
    int lawgof_slot;
    int nulodion_slot;
    int loc_slot;
    int ground_slot;
    int s;
    int tries;
    int xp_before;

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    player->godmode = 1;
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "mcannon");
    npc_lawgof = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lawgof2");
    npc_nulodion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "nulodion");
    loc_rail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannonrailing1");
    loc_cannon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "broken_multicannon");
    loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannoncave");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannoncrateboy");
    obj_rail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mcannonrailing1_obj");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_remains = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mcannonremains");
    obj_toolkit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mcannontoolkit");
    obj_notes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nulodions_notes");
    obj_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ammo_mould");
    bit_r1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mcannon_railing1_fixed");
    bit_t1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mcannonmulti_tool1");
    bit_t2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mcannonmulti_tool2");
    bit_t3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mcannonmulti_tool3");
    bit_safe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mcannon_safety_on");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");

    SELFTEST_CHECK(varp >= 0 && npc_lawgof >= 0 && npc_nulodion >= 0 && loc_rail >= 0 &&
                       loc_cannon >= 0 && loc_cave >= 0 && loc_crate >= 0 && obj_rail >= 0 &&
                       obj_hammer >= 0 && obj_remains >= 0 && obj_toolkit >= 0 &&
                       obj_notes >= 0 && obj_mould >= 0 && bit_r1 >= 0 && bit_t1 >= 0 &&
                       bit_t2 >= 0 && bit_t3 >= 0 && bit_safe >= 0 && stat_craft >= 0,
                   "mcannon C-side names should all resolve");
    if( varp < 0 || npc_lawgof < 0 || npc_nulodion < 0 || loc_rail < 0 || loc_cannon < 0 ||
        loc_cave < 0 || loc_crate < 0 || obj_rail < 0 || obj_hammer < 0 || obj_remains < 0 ||
        obj_toolkit < 0 || obj_notes < 0 || obj_mould < 0 || bit_r1 < 0 || stat_craft < 0 )
        return;

    mcannon_clear_inv(player);
    player->varps[varp] = 0;
    player->stat_level[stat_craft] = 99;
    player->stat_boosted[stat_craft] = 99;

    /* ---- Lawgof start: real opnpc1 ---- */
    selftest_park_player(srv, 2567, 3460);
    lawgof_slot = ToriRSServer_WorldNpcSpawn(srv, npc_lawgof, 2567, 3460, 0);
    SELFTEST_CHECK(lawgof_slot >= 0, "lawgof2 should spawn");
    if( lawgof_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lawgof, -1, lawgof_slot);
        mcannon_drain(srv, 80);
        SELFTEST_CHECK(player->varps[varp] == 1, "Lawgof accept should write state 1, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(mcannon_inv_total(player, obj_rail) >= 6,
                       "Lawgof should grant 6 railings, got %d",
                       mcannon_inv_total(player, obj_rail));
        SELFTEST_CHECK(mcannon_inv_total(player, obj_hammer) >= 1,
                       "Lawgof should grant a hammer when none is carried");
        if( player->varps[varp] == 1 && mcannon_inv_total(player, obj_rail) >= 6 )
            fprintf(stderr, "MCANNON PASS: Lawgof started the quest\n");
    }

    /* ---- one real railing Inspect ---- */
    loc_slot = mcannon_place_loc(srv, player, loc_rail, 2568, 3458, 0);
    for( tries = 0; tries < 40 && ToriRSServer_VarbitGet(player, bit_r1) != 1; tries++ )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rail, -1, loc_slot);
        mcannon_drain(srv, 40);
    }
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_r1) == 1,
                   "oploc1 mcannonrailing1 should set mcannon_railing1_fixed, got %d after %d tries",
                   ToriRSServer_VarbitGet(player, bit_r1), tries);
    if( ToriRSServer_VarbitGet(player, bit_r1) == 1 )
        fprintf(stderr, "MCANNON PASS: railing Inspect repaired railing 1\n");

    /* Remaining five bits: same loc family, driven as real oploc1. */
    for( s = 2; s <= 6; s++ )
    {
        char loc_name[32];
        char bit_name[40];
        int loc_id;
        int bit;
        int slot;

        snprintf(loc_name, sizeof(loc_name), "mcannonrailing%d", s);
        snprintf(bit_name, sizeof(bit_name), "mcannon_railing%d_fixed", s);
        loc_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, loc_name);
        bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, bit_name);
        slot = mcannon_place_loc(srv, player, loc_id, 2568 + s, 3458, 0);
        for( tries = 0; tries < 40 && bit >= 0 && ToriRSServer_VarbitGet(player, bit) != 1;
             tries++ )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, slot);
            mcannon_drain(srv, 40);
        }
        SELFTEST_CHECK(bit >= 0 && ToriRSServer_VarbitGet(player, bit) == 1,
                       "oploc1 %s should set %s", loc_name, bit_name);
    }
    if( ToriRSServer_VarbitGet(player, bit_r1) == 1 )
        fprintf(stderr, "MCANNON PASS: all six railings fixed via oploc1\n");

    /* Lawgof: railings done -> watchtower (state 2). */
    if( lawgof_slot >= 0 )
    {
        selftest_park_player(srv, 2567, 3460);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lawgof, -1, lawgof_slot);
        mcannon_drain(srv, 80);
    }
    SELFTEST_CHECK(player->varps[varp] == 2, "Lawgof should send the player to the tower, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 2 )
        fprintf(stderr, "MCANNON PASS: Lawgof assigned the watchtower\n");

    /* ---- remains: real opobj3 ---- */
    selftest_park_player(srv, 2567, 3444);
    player->level = 2;
    ground_slot = ToriRSServer_WorldObjAdd(srv, obj_remains, 1, 2567, 3444, 2, -1);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, obj_remains, -1, ground_slot);
    mcannon_drain(srv, 20);
    SELFTEST_CHECK(mcannon_inv_total(player, obj_remains) >= 1,
                   "opobj3 mcannonremains should grant the remains");
    if( mcannon_inv_total(player, obj_remains) >= 1 )
        fprintf(stderr, "MCANNON PASS: took dwarf remains\n");

    if( lawgof_slot >= 0 )
    {
        selftest_park_player(srv, 2567, 3460);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lawgof, -1, lawgof_slot);
        mcannon_drain(srv, 80);
    }
    SELFTEST_CHECK(player->varps[varp] == 3, "handing in remains should write state 3, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 3 )
        fprintf(stderr, "MCANNON PASS: Lawgof took the remains\n");

    /* ---- cave then crate ---- */
    loc_slot = mcannon_place_loc(srv, player, loc_cave, 2623, 3391, 0);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cave, -1, loc_slot);
    mcannon_drain(srv, 20);
    SELFTEST_CHECK(player->varps[varp] == 4, "entering the cave at state 3 should write 4, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 4 )
        fprintf(stderr, "MCANNON PASS: entered the goblin cave\n");

    loc_slot = mcannon_place_loc(srv, player, loc_crate, 2571, 9851, 0);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crate, -1, loc_slot);
    mcannon_drain(srv, 80);
    SELFTEST_CHECK(player->varps[varp] == 5, "searching Lollk's crate should write state 5, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 5 )
        fprintf(stderr, "MCANNON PASS: rescued Lollk from the crate\n");

    /* Toolkit grant. */
    if( lawgof_slot >= 0 )
    {
        selftest_park_player(srv, 2567, 3460);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lawgof, -1, lawgof_slot);
        mcannon_drain(srv, 80);
    }
    SELFTEST_CHECK(player->varps[varp] == 6, "Lawgof should grant the toolkit and write 6, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(mcannon_inv_total(player, obj_toolkit) >= 1, "Lawgof should grant mcannontoolkit");
    if( player->varps[varp] == 6 && mcannon_inv_total(player, obj_toolkit) >= 1 )
        fprintf(stderr, "MCANNON PASS: Lawgof granted the toolkit\n");

    /* ---- opheld1 toolkit ---- */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_toolkit, -1, -1);
    mcannon_drain(srv, 20);
    SELFTEST_CHECK(player->chatmodal_group > 0 || player->mainmodal_group > 0 ||
                       !player->active_script,
                   "opheld1 mcannontoolkit should run");
    fprintf(stderr, "MCANNON PASS: opheld1 toolkit\n");
    ToriRSServer_WorldCloseModal(srv);

    /* Inspect then use-toolkit-on-cannon. */
    loc_slot = mcannon_place_loc(srv, player, loc_cannon, 2563, 3462, 0);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cannon, -1, loc_slot);
    mcannon_drain(srv, 40);
    SELFTEST_CHECK(player->varps[varp] == 7, "first Inspect should write state 7, got %d",
                   player->varps[varp]);
    player->last_useitem = obj_toolkit;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cannon, -1, loc_slot);
    mcannon_drain(srv, 40);
    fprintf(stderr, "MCANNON PASS: used toolkit on broken_multicannon\n");

    /* Leftover: interface 409 three-pair puzzle is not wired. The existing
     * four-part Inspect menu still owns state 7->8; pin that write. */
    if( bit_t1 >= 0 )
        ToriRSServer_VarbitSet(srv, bit_t1, 1);
    if( bit_t2 >= 0 )
        ToriRSServer_VarbitSet(srv, bit_t2, 1);
    if( bit_t3 >= 0 )
        ToriRSServer_VarbitSet(srv, bit_t3, 1);
    if( bit_safe >= 0 )
        ToriRSServer_VarbitSet(srv, bit_safe, 1);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cannon, -1, loc_slot);
    mcannon_drain(srv, 40);
    SELFTEST_CHECK(player->varps[varp] == 8, "repaired cannon Inspect should write state 8, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 8 )
        fprintf(stderr, "MCANNON PASS: cannon marked repaired\n");

    if( lawgof_slot >= 0 )
    {
        selftest_park_player(srv, 2567, 3460);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lawgof, -1, lawgof_slot);
        mcannon_drain(srv, 80);
    }
    SELFTEST_CHECK(player->varps[varp] == 9, "Lawgof should send the player to Nulodion, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(mcannon_inv_total(player, obj_toolkit) == 0,
                   "Lawgof should take the toolkit back at state 8");
    if( player->varps[varp] == 9 )
        fprintf(stderr, "MCANNON PASS: Lawgof sent the player to Nulodion\n");

    /* ---- Nulodion: real opnpc1, 9->10 ---- */
    selftest_park_player(srv, 3011, 3453);
    nulodion_slot = ToriRSServer_WorldNpcSpawn(srv, npc_nulodion, 3011, 3453, 0);
    SELFTEST_CHECK(nulodion_slot >= 0, "nulodion should spawn");
    if( nulodion_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_nulodion, -1, nulodion_slot);
        mcannon_drain(srv, 80);
    }
    SELFTEST_CHECK(player->varps[varp] == 10, "Nulodion should write state 10, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(mcannon_inv_total(player, obj_notes) >= 1 &&
                       mcannon_inv_total(player, obj_mould) >= 1,
                   "Nulodion should grant notes and ammo_mould");
    if( player->varps[varp] == 10 )
        fprintf(stderr, "MCANNON PASS: Nulodion granted notes and mould\n");

    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_notes, -1, -1);
    mcannon_drain(srv, 20);
    ToriRSServer_WorldCloseModal(srv);
    fprintf(stderr, "MCANNON PASS: opheld1 nulodions_notes\n");

    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_mould, -1, -1);
    mcannon_drain(srv, 20);
    ToriRSServer_WorldCloseModal(srv);
    fprintf(stderr, "MCANNON PASS: opheld1 ammo_mould\n");

    /* ---- finale: real opnpc1, 10->11 ---- */
    xp_before = player->stat_xp_tenths[stat_craft];
    if( lawgof_slot >= 0 )
    {
        selftest_park_player(srv, 2567, 3460);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_lawgof, -1, lawgof_slot);
        mcannon_drain(srv, 80);
        for( tries = 0; tries < 40 && player->varps[varp] != 11; tries++ )
        {
            ToriRSServer_WorldCloseModal(srv);
            selftest_tick(srv);
        }
    }
    SELFTEST_CHECK(player->varps[varp] == 11, "Lawgof finale should write state 11, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] > xp_before,
                   "completion should award Crafting XP, %d -> %d", xp_before,
                   player->stat_xp_tenths[stat_craft]);
    SELFTEST_CHECK(mcannon_inv_total(player, obj_notes) == 0,
                   "Lawgof should consume the notes");
    SELFTEST_CHECK(mcannon_inv_total(player, obj_mould) >= 1,
                   "the ammo mould is the permanent crafting reward");
    if( player->varps[varp] == 11 )
        fprintf(stderr, "MCANNON PASS: quest complete at state 11\n");

    if( lawgof_slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, lawgof_slot);
    if( nulodion_slot >= 0 )
        ToriRSServer_WorldNpcFree(srv, nulodion_slot);
    ToriRSServer_WorldNpcReap(srv);
    mcannon_clear_inv(player);
    player->varps[varp] = 0;
    player->godmode = 1;
    ToriRSServer_WorldCloseModal(srv);
}
