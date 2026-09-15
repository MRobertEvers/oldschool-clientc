/* Dwarf Cannon -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int
mcannon_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
mcannon_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    fprintf(stderr, "PASS dwarfcannon %s trigger=%s %s\n", step, trigger, observable);
    fflush(stderr);
}

static void
mcannon_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
mcannon_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
mcannon_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = mcannon_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 80 && player->active_script; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( stop_on_choice && chatmenu > 0 && uid == chatmenu )
                return;
            if( !ToriRSServer_ScriptsResumeButton(srv, uid) )
                break;
        }
        else if( exec == SSVM_SUSPENDED || exec == SSVM_NPC_SUSPENDED ||
                 exec == SSVM_WORLD_SUSPENDED )
        {
            selftest_tick(srv);
        }
        else
        {
            break;
        }
    }
}

static void
mcannon_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = mcannon_chatmenu();

    assert(srv);
    assert(player);
    mcannon_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
mcannon_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    assert(srv);
    assert(player);
    mcannon_release(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
mcannon_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    mcannon_drain(srv, player, 0);
    for( i = 0; i < 48; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
mcannon_inv_clear(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static int
mcannon_find_loc(int x, int z, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int slot;
    int s;

    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( dx = -radius; dx <= radius; dx++ )
    {
        for( dz = -radius; dz <= radius; dz++ )
        {
            slot = ToriRSServer_SceneFindLocId(x + dx, z + dz, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    }
    for( s = 0;; s++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

        if( !loc )
            break;
        if( !loc->active || loc->level != level )
            continue;
        if( loc_id > 0 && loc->loc_id != loc_id )
            continue;
        if( loc->x + loc->size_x <= x - radius || loc->x > x + radius )
            continue;
        if( loc->z + loc->size_z <= z - radius || loc->z > z + radius )
            continue;
        return s;
    }
    return -1;
}

static void
selftest_quest_mcannon(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_lawgof;
    int npc_nulodion;
    int npc_lollk;
    int varp;
    int qp_varp;
    int crafting;
    int obj_rail;
    int obj_hammer;
    int obj_toolkit;
    int obj_remains;
    int obj_notes;
    int obj_mould;
    int loc_rail1;
    int loc_cave;
    int loc_crate;
    int loc_cannon;
    int loc_cannon_broken;
    int loc_remains_shell;
    int loc_remains_child;
    int vb_spring;
    int vb_safety;
    int com_tool1;
    int com_tool2;
    int com_tool3;
    int com_spring;
    int com_safety;
    int com_gear;
    int law_slot;
    int nul_slot;
    int spawned_law;
    int spawned_nul;
    int loc_slot;
    int qp_before;
    int xp_before;
    int i;

    assert(srv);
    assert(player);
    fprintf(stderr, "ToriRSServer selftest: Dwarf Cannon real-trigger walk\n");
    fflush(stderr);

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Dwarf Cannon selftest needs a compiled script pack");
    if( !loaded )
        return;

    npc_lawgof = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lawgof2");
    npc_nulodion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "nulodion");
    npc_lollk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dwarfchildtw1");
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "mcannon");
    qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    crafting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    obj_rail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mcannonrailing1_obj");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_toolkit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mcannontoolkit");
    obj_remains = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mcannonremains");
    obj_notes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nulodions_notes");
    obj_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ammo_mould");
    loc_rail1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannon_railing1_multiloc");
    loc_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannoncave");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannoncrateboy");
    loc_cannon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannon_cannon_multiloc");
    loc_cannon_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "broken_multicannon");
    loc_remains_shell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannonremains_multiloc");
    loc_remains_child = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mcannonremains_location");
    vb_spring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mcannon_spring_set");
    vb_safety = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "mcannon_safety_on");
    com_tool1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "mcannon_interface:mcannon_tool1");
    com_tool2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "mcannon_interface:mcannon_tool2");
    com_tool3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "mcannon_interface:mcannon_tool3");
    com_spring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "mcannon_interface:mcannon_spring");
    com_safety = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "mcannon_interface:mcannon_safety");
    com_gear = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "mcannon_interface:mcannon_gear");

    SELFTEST_CHECK(npc_lawgof >= 0 && npc_nulodion >= 0 && varp >= 0 && qp_varp >= 0 &&
                       obj_rail >= 0 && obj_hammer >= 0 && obj_toolkit >= 0 && obj_remains >= 0 &&
                       obj_notes >= 0 && obj_mould >= 0 && loc_rail1 >= 0 && loc_cave >= 0 &&
                       loc_crate >= 0,
                   "Dwarf Cannon symbols should resolve");
    if( npc_lawgof < 0 || npc_nulodion < 0 || varp < 0 || qp_varp < 0 || obj_rail < 0 ||
        obj_hammer < 0 || obj_toolkit < 0 || obj_remains < 0 || obj_notes < 0 || obj_mould < 0 ||
        loc_rail1 < 0 || loc_cave < 0 || loc_crate < 0 )
        return;

    player->world = srv;
    player->active = 1;
    selftest_reset_world(srv, player, 402, 402);
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);
    if( crafting >= 0 )
    {
        player->stat_level[crafting] = 99;
        player->stat_boosted[crafting] = 99;
    }

    mcannon_inv_clear(player);
    player->varps[varp] = 0;
    player->last_slot = -1;

    mcannon_snap(srv, player, 0, 2567, 3460);
    mcannon_pass("snap", "SNAP", "tile=2567,3460,0");

    spawned_law = -1;
    spawned_nul = -1;
    law_slot = selftest_find_npc(srv, npc_lawgof);
    if( law_slot < 0 )
    {
        law_slot = npc_spawn(srv, npc_lawgof, player->x + 1, player->z, player->level);
        spawned_law = law_slot;
    }
    SELFTEST_CHECK(law_slot >= 0, "Lawgof should spawn beside the player");
    if( law_slot < 0 )
        return;

    /* ---- decline stays 0 ---- */
    mcannon_talk(srv, player, npc_lawgof, law_slot);
    mcannon_choose(srv, player, 2);
    mcannon_drain(srv, player, 0);
    mcannon_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 0, "decline must leave %%mcannon at 0, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 0 )
        mcannon_pass("decline", "OPNPC1", "mcannon=0");

    /* ---- accept writes 1 and grants rails + hammer ---- */
    mcannon_talk(srv, player, npc_lawgof, law_slot);
    mcannon_choose(srv, player, 1);
    mcannon_drain(srv, player, 0);
    mcannon_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 1, "accept should write mcannon=1, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_rail) == 6,
                   "accept should grant 6 railings, have %d",
                   selftest_count_obj(player, obj_rail));
    SELFTEST_CHECK(selftest_count_obj(player, obj_hammer) >= 1,
                   "accept should grant a hammer when none was owned");
    if( player->varps[varp] == 1 )
        mcannon_pass("accept", "OPNPC1", "mcannon=1 rails=6 hammer");

    /* QH WorldPoints for the six MULTILOC shells (not rungs). */
    {
        static const struct
        {
            const char* shell;
            const char* child;
            const char* bit;
            int x;
            int z;
        } rails[6] = {
            { "mcannon_railing1_multiloc", "mcannonrailing1", "mcannon_railing1_fixed", 2555, 3479 },
            { "mcannon_railing2_multiloc", "mcannonrailing2", "mcannon_railing2_fixed", 2557, 3468 },
            { "mcannon_railing3_multiloc", "mcannonrailing3", "mcannon_railing3_fixed", 2559, 3458 },
            { "mcannon_railing4_multiloc", "mcannonrailing4", "mcannon_railing4_fixed", 2563, 3457 },
            { "mcannon_railing5_multiloc", "mcannonrailing5", "mcannon_railing5_fixed", 2573, 3457 },
            { "mcannon_railing6_multiloc", "mcannonrailing6", "mcannon_railing6_fixed", 2577, 3457 },
        };
        int r;

        for( r = 0; r < 6; r++ )
        {
            int shell_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, rails[r].shell);
            int child_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, rails[r].child);
            int vb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, rails[r].bit);
            int use_id;
            struct ToriRSServerSceneLoc* loc;

            mcannon_snap(srv, player, 0, rails[r].x, rails[r].z);
            loc_slot = -1;
            if( shell_id >= 0 )
                loc_slot = mcannon_find_loc(rails[r].x, rails[r].z, 0, shell_id, 2);
            if( loc_slot < 0 && child_id >= 0 )
                loc_slot = mcannon_find_loc(rails[r].x, rails[r].z, 0, child_id, 2);
            if( r == 0 )
                SELFTEST_CHECK(loc_slot >= 0, "mcannon_railing1_multiloc should stand at 2555,3479");
            if( loc_slot < 0 )
                continue;
            loc = ToriRSServer_SceneLoc(loc_slot);
            use_id = loc ? loc->loc_id : shell_id;
            for( i = 0; i < 12 && vb >= 0 && ToriRSServer_VarbitGet(player, vb) == 0; i++ )
            {
                mcannon_release(srv, player);
                ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, use_id, -1, loc_slot);
                mcannon_drain(srv, player, 0);
                mcannon_drain_rewards(srv, player);
            }
            if( r == 0 )
            {
                SELFTEST_CHECK(vb < 0 || ToriRSServer_VarbitGet(player, vb) == 1,
                               "railing shell click should set mcannon_railing1_fixed");
                if( vb < 0 || ToriRSServer_VarbitGet(player, vb) == 1 )
                    mcannon_pass("railing", "OPLOC1", "shell=mcannon_railing1_multiloc");
            }
        }
    }

    mcannon_snap(srv, player, 0, 2567, 3460);

    mcannon_talk(srv, player, npc_lawgof, law_slot);
    mcannon_drain(srv, player, 0);
    mcannon_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 2, "all-fixed report should write mcannon=2, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 2 )
        mcannon_pass("tower-task", "OPNPC1", "mcannon=2");

    /* ---- remains (tower roof; QH 2567,3444,2) ---- */
    mcannon_snap(srv, player, 2, 2567, 3444);
    loc_slot = -1;
    if( loc_remains_shell >= 0 )
        loc_slot = mcannon_find_loc(2567, 3444, 2, loc_remains_shell, 4);
    if( loc_slot < 0 && loc_remains_child >= 0 )
        loc_slot = mcannon_find_loc(2567, 3444, 2, loc_remains_child, 4);
    if( loc_slot < 0 )
    {
        mcannon_snap(srv, player, 1, 2570, 3443);
        if( loc_remains_shell >= 0 )
            loc_slot = mcannon_find_loc(2567, 3444, 1, loc_remains_shell, 6);
        if( loc_slot < 0 && loc_remains_child >= 0 )
            loc_slot = mcannon_find_loc(2567, 3444, 1, loc_remains_child, 6);
    }
    if( loc_slot < 0 )
    {
        int lv;

        for( lv = 0; lv <= 2 && loc_slot < 0; lv++ )
        {
            mcannon_snap(srv, player, lv, 2567, 3444);
            if( loc_remains_shell >= 0 )
                loc_slot = mcannon_find_loc(2567, 3444, lv, loc_remains_shell, 8);
            if( loc_slot < 0 && loc_remains_child >= 0 )
                loc_slot = mcannon_find_loc(2567, 3444, lv, loc_remains_child, 8);
        }
    }
    SELFTEST_CHECK(loc_slot >= 0, "remains loc should resolve via SceneFindLocId");
    if( loc_slot >= 0 )
    {
        int use_id = loc_remains_shell;
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(loc_slot);

        if( loc && loc_remains_child >= 0 && loc->loc_id == loc_remains_child )
            use_id = loc_remains_child;
        mcannon_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, use_id, -1, loc_slot);
        mcannon_drain(srv, player, 0);
        mcannon_drain_rewards(srv, player);
        SELFTEST_CHECK(selftest_count_obj(player, obj_remains) == 1,
                       "remains Take should grant one mcannonremains, have %d",
                       selftest_count_obj(player, obj_remains));
        if( selftest_count_obj(player, obj_remains) == 1 )
            mcannon_pass("remains", "OPLOC1", "one remains");
    }

    mcannon_snap(srv, player, 0, 2567, 3460);
    mcannon_talk(srv, player, npc_lawgof, law_slot);
    mcannon_drain(srv, player, 0);
    mcannon_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 3, "remains hand-in should write mcannon=3, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_remains) == 0, "hand-in should consume remains");
    if( player->varps[varp] == 3 )
        mcannon_pass("remains-handin", "OPNPC1", "mcannon=3");

    /* ---- cave 3 -> 4 ---- */
    mcannon_snap(srv, player, 0, 2622, 3392);
    loc_slot = mcannon_find_loc(2622, 3392, 0, loc_cave, 4);
    if( loc_slot < 0 )
        loc_slot = mcannon_find_loc(2624, 3391, 0, loc_cave, 4);
    SELFTEST_CHECK(loc_slot >= 0, "mcannoncave should stand at the Goblin Cave mouth");
    if( loc_slot >= 0 )
    {
        mcannon_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cave, -1, loc_slot);
        mcannon_drain(srv, player, 0);
        mcannon_drain_rewards(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 4, "cave enter should write mcannon=4, got %d",
                       player->varps[varp]);
        if( player->varps[varp] == 4 )
            mcannon_pass("cave", "OPLOC1", "mcannon=4");
    }

    /* ---- Lollk crate 4 -> 5, owned spawn ---- */
    mcannon_snap(srv, player, 0, 2571, 9850);
    loc_slot = mcannon_find_loc(2571, 9850, 0, loc_crate, 4);
    if( loc_slot < 0 )
        loc_slot = mcannon_find_loc(2571, 9851, 0, loc_crate, 4);
    SELFTEST_CHECK(loc_slot >= 0, "mcannoncrateboy should stand in the cave");
    if( loc_slot >= 0 )
    {
        int child_slot;

        mcannon_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crate, -1, loc_slot);
        mcannon_drain(srv, player, 0);
        mcannon_drain_rewards(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 5, "untie should write mcannon=5, got %d",
                       player->varps[varp]);
        if( player->varps[varp] == 5 )
            mcannon_pass("lollk", "OPLOC1", "mcannon=5 owned");
        child_slot = selftest_find_npc(srv, npc_lollk);
        if( child_slot >= 0 )
        {
            ToriRSServer_WorldNpcFree(srv, child_slot);
            ToriRSServer_WorldNpcReap(srv);
        }
    }

    /* ---- toolkit 5 -> 6 ---- */
    mcannon_snap(srv, player, 0, 2567, 3460);
    mcannon_talk(srv, player, npc_lawgof, law_slot);
    mcannon_choose(srv, player, 1);
    mcannon_drain(srv, player, 0);
    mcannon_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 6, "toolkit accept should write mcannon=6, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_toolkit) >= 1, "toolkit grant should land");
    if( player->varps[varp] == 6 )
        mcannon_pass("toolkit", "OPNPC1", "mcannon=6");

    /* ---- toolkit on cannon opens repair; three pairings write 8 ---- */
    loc_slot = -1;
    mcannon_snap(srv, player, 0, 2563, 3462);
    if( loc_cannon >= 0 )
        loc_slot = mcannon_find_loc(2563, 3462, 0, loc_cannon, 4);
    if( loc_slot < 0 && loc_cannon_broken >= 0 )
        loc_slot = mcannon_find_loc(2563, 3462, 0, loc_cannon_broken, 4);
    if( loc_slot < 0 && loc_cannon >= 0 )
        loc_slot = mcannon_find_loc(2567, 3460, 0, loc_cannon, 16);
    if( loc_slot < 0 && loc_cannon_broken >= 0 )
        loc_slot = mcannon_find_loc(2567, 3460, 0, loc_cannon_broken, 16);
    SELFTEST_CHECK(loc_slot >= 0, "quest cannon loc should resolve via SceneFindLocId");
    if( loc_slot >= 0 && com_tool1 > 0 && com_gear > 0 )
    {
        int use_id = loc_cannon;
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(loc_slot);

        if( loc && loc_cannon_broken >= 0 && loc->loc_id == loc_cannon_broken )
            use_id = loc_cannon_broken;
        player->last_useitem = obj_toolkit;
        mcannon_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, use_id, -1, loc_slot);
        mcannon_drain(srv, player, 0);
        player->last_useitem = -1;
        SELFTEST_CHECK(player->varps[varp] == 7, "toolkit use should write mcannon=7, got %d",
                       player->varps[varp]);

        ToriRSServer_ScriptsRunIfButton(srv, com_tool3, 1);
        ToriRSServer_ScriptsRunIfButton(srv, com_spring, 1);
        ToriRSServer_ScriptsRunIfButton(srv, com_tool2, 1);
        ToriRSServer_ScriptsRunIfButton(srv, com_safety, 1);
        ToriRSServer_ScriptsRunIfButton(srv, com_tool1, 1);
        ToriRSServer_ScriptsRunIfButton(srv, com_gear, 1);
        mcannon_drain(srv, player, 0);
        mcannon_drain_rewards(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 8, "three pairings should write mcannon=8, got %d",
                       player->varps[varp]);
        if( vb_spring >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_spring) == 1, "spring bit should set");
        if( vb_safety >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_safety) == 1, "safety bit should set");
        if( player->varps[varp] == 8 )
            mcannon_pass("repair", "OPLOCU+IF_BUTTON1", "mcannon=8");
    }

    mcannon_talk(srv, player, npc_lawgof, law_slot);
    mcannon_choose(srv, player, 1);
    mcannon_drain(srv, player, 0);
    mcannon_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 9, "Nulodion errand should write mcannon=9, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 9 )
        mcannon_pass("nulodion-task", "OPNPC1", "mcannon=9");

    /* ---- Nulodion 9 -> 10 ---- */
    mcannon_snap(srv, player, 0, 3011, 3453);
    nul_slot = selftest_find_npc(srv, npc_nulodion);
    if( nul_slot < 0 )
    {
        nul_slot = npc_spawn(srv, npc_nulodion, player->x + 1, player->z, player->level);
        spawned_nul = nul_slot;
    }
    SELFTEST_CHECK(nul_slot >= 0, "Nulodion should spawn");
    if( nul_slot >= 0 )
    {
        mcannon_talk(srv, player, npc_nulodion, nul_slot);
        mcannon_drain(srv, player, 0);
        mcannon_drain_rewards(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 10, "Nulodion grant should write mcannon=10, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(selftest_count_obj(player, obj_notes) >= 1 &&
                           selftest_count_obj(player, obj_mould) >= 1,
                       "Nulodion should grant notes and mould");
        if( player->varps[varp] == 10 )
            mcannon_pass("nulodion", "OPNPC1", "mcannon=10 notes+mould");
    }

    /* ---- finale 10 -> 11 ---- */
    mcannon_snap(srv, player, 0, 2567, 3460);
    qp_before = player->varps[qp_varp];
    xp_before = crafting >= 0 ? player->stat_xp_tenths[crafting] : 0;
    mcannon_talk(srv, player, npc_lawgof, law_slot);
    mcannon_drain(srv, player, 0);
    mcannon_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 11, "finale should write mcannon=11, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count_obj(player, obj_notes) == 0, "finale should consume notes");
    SELFTEST_CHECK(selftest_count_obj(player, obj_mould) == 0, "finale should consume mould");
    SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 1, "finale should award +1 QP, %d -> %d",
                   qp_before, player->varps[qp_varp]);
    if( crafting >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[crafting] == xp_before + 7500,
                       "finale should award 750 Crafting XP, %d -> %d",
                       xp_before, player->stat_xp_tenths[crafting]);
    if( player->varps[varp] == 11 )
        mcannon_pass("complete", "OPNPC1", "mcannon=11 qp+1 xp+750");

    /* ---- exactly-once / postquest ---- */
    {
        int qp = player->varps[qp_varp];
        int xp = crafting >= 0 ? player->stat_xp_tenths[crafting] : 0;

        mcannon_talk(srv, player, npc_lawgof, law_slot);
        mcannon_drain(srv, player, 0);
        mcannon_drain_rewards(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 11, "postquest must keep mcannon=11");
        SELFTEST_CHECK(player->varps[qp_varp] == qp, "postquest must not award a second QP");
        if( crafting >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[crafting] == xp,
                           "postquest must not award Crafting XP again");
        mcannon_pass("postquest", "OPNPC1", "mcannon=11 exactly-once");
    }

    if( spawned_law >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_law);
    if( spawned_nul >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_nul);
    ToriRSServer_WorldNpcReap(srv);
    mcannon_inv_clear(player);
    player->varps[varp] = 0;
    player->godmode = 0;
    mcannon_pass("cleanup", "WorldNpcFree", "spawns reaped");
}
