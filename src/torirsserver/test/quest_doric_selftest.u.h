/* Doric's Quest — OPNPC1 + ResumeButton, not ::doricrun. */

static int
doric_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
doric_release_park(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
doric_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = doric_chatmenu();
    int round;

    for( round = 0; round < 48 && player->active_script; round++ )
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
doric_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = doric_chatmenu();

    doric_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
doric_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int doric_type, int slot)
{
    doric_release_park(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, doric_type, -1, slot);
}

static void
doric_drain_complete(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int varp, int want)
{
    int i;

    doric_drain(srv, player, 0);
    for( i = 0; i < 48 && player->varps[varp] != want; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
selftest_quest_doric(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int doric_type;
    int varp;
    int qp_varp;
    int obj_pick;
    int obj_clay;
    int obj_copper;
    int obj_iron;
    int obj_coins;
    int obj_junk;
    int anvil;
    int mining;
    int slot;
    int qp_before;
    int xp_before;
    int coins_before;
    int i;

    fprintf(stderr, "ToriRSServer selftest: Doric's Quest OPNPC1\n");
    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    doric_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "doric");
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "doricquest");
    qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_clay = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "clay");
    obj_copper = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "copper_ore");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_ore");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_junk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    anvil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "dorics_anvil");
    mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    SELFTEST_CHECK(doric_type >= 0 && varp >= 0 && qp_varp >= 0 && obj_pick >= 0 &&
                       obj_clay >= 0 && obj_copper >= 0 && obj_iron >= 0 && obj_coins >= 0 &&
                       mining >= 0,
                   "Doric symbols should resolve");
    if( doric_type < 0 || varp < 0 || obj_pick < 0 )
        return;

    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    player->varps[varp] = 0;
    selftest_park_player(srv, player->x, player->z);
    slot = npc_spawn(srv, doric_type, player->x + 1, player->z, player->level);
    SELFTEST_CHECK(slot >= 0, "Doric should spawn beside the player");
    if( slot < 0 )
        return;

    /* ---- refuse ---- */
    doric_talk(srv, player, doric_type, slot);
    doric_choose(srv, player, 1); /* anvils */
    doric_choose(srv, player, 2); /* No */
    doric_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 0, "refuse must leave doricquest=0, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_pick) == 0, "refuse must not grant a pickaxe");
    fprintf(stderr, "DORIC PASS: refuse left state 0 and no pickaxe\n");

    /* ---- accept (Talk-to), pickaxe observable ---- */
    doric_talk(srv, player, doric_type, slot);
    doric_choose(srv, player, 1); /* anvils */
    doric_choose(srv, player, 1); /* Yes */
    doric_choose(srv, player, 2); /* I'll be right back */
    doric_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 10, "accept via OPNPC1 should write doricquest=10, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_pick) == 1,
                   "Talk-to accept should grant exactly 1 bronze pickaxe, have %d",
                   selftest_count(player, obj_pick));
    fprintf(stderr, "DORIC PASS: accept via OPNPC1 granted bronze pickaxe, state 10\n");

    /* ---- re-talk mid-quest, still missing ores ---- */
    doric_talk(srv, player, doric_type, slot);
    doric_choose(srv, player, 2); /* I'll be right back */
    doric_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 10, "mid-quest re-talk must stay at 10, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_pick) == 1, "re-talk must not grant another pickaxe");
    fprintf(stderr, "DORIC PASS: mid-quest re-talk stayed at 10, no duplicate pickaxe\n");

    /* ---- hand-in ---- */
    selftest_give(player, obj_clay, 6);
    selftest_give(player, obj_copper, 4);
    selftest_give(player, obj_iron, 2);
    qp_before = qp_varp >= 0 ? player->varps[qp_varp] : 0;
    xp_before = player->stat_xp_tenths[mining];
    coins_before = selftest_count(player, obj_coins);
    doric_talk(srv, player, doric_type, slot);
    doric_drain_complete(srv, player, varp, 100);
    SELFTEST_CHECK(player->varps[varp] == 100, "hand-in should commit doricquest=100, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_clay) == 0 && selftest_count(player, obj_copper) == 0 &&
                       selftest_count(player, obj_iron) == 0,
                   "hand-in should consume 6 clay / 4 copper / 2 iron");
    SELFTEST_CHECK(selftest_count(player, obj_coins) == coins_before + 180,
                   "hand-in should grant 180 coins, %d -> %d", coins_before,
                   selftest_count(player, obj_coins));
    SELFTEST_CHECK(player->stat_xp_tenths[mining] == xp_before + 13000,
                   "hand-in should grant 1300 Mining XP, %d -> %d", xp_before,
                   player->stat_xp_tenths[mining]);
    if( qp_varp >= 0 )
        SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 1, "hand-in should award 1 QP, %d -> %d",
                       qp_before, player->varps[qp_varp]);
    fprintf(stderr, "DORIC PASS: hand-in consumed ores, +180 coins, +1300 Mining XP, state 100\n");

    /* ---- post-quest Talk-to ---- */
    doric_talk(srv, player, doric_type, slot);
    doric_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 100, "post-quest talk must stay at 100, got %d",
                   player->varps[varp]);
    fprintf(stderr, "DORIC PASS: post-quest OPNPC1 stayed complete\n");

    /* ---- anvil unlock: real Doric hut loc, or type-bound OPLOC1 ---- */
    if( anvil >= 0 )
    {
        int loc_slot = -1;
        int cat = ToriRSServer_LocCategory(anvil);

        ToriRSServer_WorldTeleport(srv, 0, 2952, 3451);
        if( player->rebuild_scene_pending )
            selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
        selftest_tick(srv);
        for( i = -6; i <= 6 && loc_slot < 0; i++ )
        {
            int j;

            for( j = -6; j <= 6 && loc_slot < 0; j++ )
                loc_slot = ToriRSServer_SceneFindLocId(2952 + i, 3451 + j, 0, anvil);
        }
        doric_release_park(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, anvil, cat, loc_slot);
        doric_drain(srv, player, 0);
        SELFTEST_CHECK(player->varps[varp] == 100,
                       "post-quest anvil click must not rewind Doric, state %d",
                       player->varps[varp]);
        fprintf(stderr, "DORIC PASS: post-quest anvil OPLOC1 left state 100 (loc_slot=%d)\n",
                loc_slot);
    }

    /* ---- immediate-ready accept (ores already held) ---- */
    doric_release_park(srv, player);
    player->varps[varp] = 0;
    selftest_clear_inv(player);
    selftest_give(player, obj_clay, 6);
    selftest_give(player, obj_copper, 4);
    selftest_give(player, obj_iron, 2);
    qp_before = qp_varp >= 0 ? player->varps[qp_varp] : 0;
    xp_before = player->stat_xp_tenths[mining];
    coins_before = selftest_count(player, obj_coins);
    doric_talk(srv, player, doric_type, slot);
    doric_choose(srv, player, 1); /* anvils */
    doric_choose(srv, player, 1); /* Yes — coincidence hand-in, no directions */
    doric_drain_complete(srv, player, varp, 100);
    SELFTEST_CHECK(player->varps[varp] == 100, "ready-now accept should complete in one talk, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_pick) == 1, "ready-now should still grant the pickaxe");
    SELFTEST_CHECK(selftest_count(player, obj_clay) == 0 && selftest_count(player, obj_iron) == 0,
                   "ready-now should consume the ores in the same conversation");
    SELFTEST_CHECK(selftest_count(player, obj_coins) == coins_before + 180,
                   "ready-now should grant 180 coins");
    SELFTEST_CHECK(player->stat_xp_tenths[mining] == xp_before + 13000,
                   "ready-now should grant 1300 Mining XP");
    if( qp_varp >= 0 )
        SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 1, "ready-now should award 1 QP");
    fprintf(stderr, "DORIC PASS: ready-now accept granted pickaxe and completed atomically\n");

    /* ---- full-inventory accept without a pickaxe already held ---- */
    doric_release_park(srv, player);
    player->varps[varp] = 0;
    selftest_clear_inv(player);
    if( obj_junk >= 0 )
    {
        for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        {
            player->inv[i].obj_id = obj_junk;
            player->inv[i].count = 1;
        }
        player->inv_dirty = 0xfffffffu;
    }
    doric_talk(srv, player, doric_type, slot);
    doric_choose(srv, player, 1);
    doric_choose(srv, player, 1);
    doric_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 0,
                   "full-inv accept without a pickaxe must not advance, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_pick) == 0,
                   "full-inv accept must not silently discard a pickaxe grant");
    fprintf(stderr, "DORIC PASS: full inventory blocked accept without losing the pickaxe\n");

    ToriRSServer_WorldNpcFree(srv, slot);
    ToriRSServer_WorldNpcReap(srv);
    selftest_clear_inv(player);
    player->varps[varp] = 0;
    fprintf(stderr, "DORIC PASS: start-to-complete via OPNPC1\n");
}
