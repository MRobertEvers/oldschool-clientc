/* Demon Slayer -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static void
demon_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    fprintf(stderr, "PASS demonslayer %s trigger=%s %s\n", step, trigger, observable);
    fflush(stderr);
}

static int
demon_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;
    int n;

    assert(player);
    n = 0;
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            n += player->inv[i].count;
    return n;
}

static void
demon_inv_clear(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
demon_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static int
demon_drain_to_choice(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int chatmenu)
{
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 240 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( chatmenu > 0 && uid == chatmenu )
                return 1;
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
    return 0;
}

static void
demon_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;
    int chatmenu;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    /* Resume pause FIRST -- WorldCloseModal aborts a parked script. */
    demon_drain_to_choice(srv, player, chatmenu);
    for( i = 0; i < 40; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
demon_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot,
    const int* choices,
    int choice_count)
{
    int chatmenu;
    int i;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    demon_release(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    for( i = 0; i < choice_count; i++ )
    {
        if( !demon_drain_to_choice(srv, player, chatmenu) )
            break;
        player->last_slot = choices[i];
        if( chatmenu > 0 )
            ToriRSServer_ScriptsResumeButton(srv, chatmenu);
    }
    demon_drain_to_choice(srv, player, chatmenu);
    demon_drain_rewards(srv, player);
}

static int
demon_find_loc(int x, int z, int level, int loc_id, int radius)
{
    int slot;
    int dx;
    int dz;
    int s;

    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( dx = -radius; dx <= radius; dx++ )
        for( dz = -radius; dz <= radius; dz++ )
        {
            slot = ToriRSServer_SceneFindLocId(x + dx, z + dz, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    for( s = 0;; s++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

        if( !loc )
            break;
        if( !loc->active || loc->level != level || loc->loc_id != loc_id )
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
selftest_quest_demon(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_aris;
    int npc_prysin;
    int npc_rovin;
    int npc_traiborn;
    int npc_delrith;
    int obj_coins;
    int obj_bones;
    int obj_bucket;
    int obj_key1;
    int obj_key2;
    int obj_key3;
    int obj_sword;
    int loc_drain;
    int loc_drain_key;
    int loc_sewer;
    int loc_sewer_mud;
    int vb_main;
    int vb_case;
    int vb_drain;
    int varp_bones;
    int varp_started;
    int varp_save;
    int varp_reward;
    int varp_qp;
    int aris_slot;
    int prysin_slot;
    int rovin_slot;
    int traiborn_slot;
    int delrith_slot;
    int spawned_aris;
    int spawned_prysin;
    int spawned_rovin;
    int spawned_traiborn;
    int spawned_delrith;
    int qp_before;
    int loc_slot;

    assert(srv);
    assert(player);
    fprintf(stderr, "ToriRSServer selftest: Demon Slayer real-trigger walk\n");
    fflush(stderr);

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Demon Slayer selftest needs a compiled script pack");
    if( !loaded )
        return;

    npc_aris = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "aris");
    npc_prysin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sir_prysin");
    npc_rovin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "captain_rovin");
    npc_traiborn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "traiborn");
    npc_delrith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "delrith");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bones");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_water");
    obj_key1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silverlight_key_1");
    obj_key2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silverlight_key_2");
    obj_key3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silverlight_key_3");
    obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silverlight");
    loc_drain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "questdrain");
    loc_drain_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "qip_ds_questdrain_key");
    loc_sewer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "qip_ds_sewer_key");
    loc_sewer_mud = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "qip_ds_rustykey_mud");
    vb_main = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "demonslayer_main");
    vb_case = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "delrith_silverlight_case");
    vb_drain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "delrith_drain_key");
    varp_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "demon_bones_given");
    varp_started = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "demon_traiborn_started");
    varp_save = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "demon_save_v2");
    varp_reward = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "demon_reward_done");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(npc_aris >= 0 && npc_prysin >= 0 && npc_rovin >= 0 &&
                       npc_traiborn >= 0 && npc_delrith >= 0 && obj_coins >= 0 &&
                       obj_bones >= 0 && obj_bucket >= 0 && obj_key1 >= 0 &&
                       obj_key2 >= 0 && obj_key3 >= 0 && obj_sword >= 0 &&
                       loc_drain >= 0 && vb_main >= 0 && vb_case >= 0 &&
                       vb_drain >= 0 && varp_bones >= 0 && varp_started >= 0 &&
                       varp_save >= 0 && varp_reward >= 0 && varp_qp >= 0,
                   "Demon Slayer symbols should resolve");
    if( npc_aris < 0 || npc_prysin < 0 || npc_rovin < 0 || npc_traiborn < 0 ||
        npc_delrith < 0 || obj_coins < 0 || obj_bones < 0 || obj_bucket < 0 ||
        obj_key1 < 0 || obj_key2 < 0 || obj_key3 < 0 || obj_sword < 0 ||
        loc_drain < 0 || vb_main < 0 || vb_case < 0 || vb_drain < 0 ||
        varp_bones < 0 || varp_started < 0 || varp_save < 0 || varp_reward < 0 ||
        varp_qp < 0 )
        return;

    player->world = srv;
    player->active = 1;
    selftest_reset_world(srv, player, 402, 402);
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);

    spawned_aris = -1;
    spawned_prysin = -1;
    spawned_rovin = -1;
    spawned_traiborn = -1;
    spawned_delrith = -1;
    demon_inv_clear(player);
    player->varps[varp_save] = 0;
    player->varps[varp_bones] = 0;
    player->varps[varp_started] = 0;
    player->varps[varp_reward] = 0;
    ToriRSServer_VarbitSetOn(srv, player, vb_main, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_case, 0);
    ToriRSServer_VarbitSetOn(srv, player, vb_drain, 0);
    player->varps[varp_save] = 1;

    /* ---- SNAP Aris tent ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3204, 3424);
    selftest_ack_scene(srv);
    selftest_tick(srv);
    fprintf(stderr, "PASS demonslayer snap trigger=SNAP tile=3204,3424,0\n");
    fflush(stderr);

    aris_slot = selftest_find_npc(srv, npc_aris);
    if( aris_slot < 0 )
    {
        aris_slot = npc_spawn(srv, npc_aris, 3204, 3424, 0);
        spawned_aris = aris_slot;
    }
    SELFTEST_CHECK(aris_slot >= 0, "Aris should be talkable in Varrock Square");
    if( aris_slot < 0 )
        goto demon_selftest_done;
    demon_pass("aris-world", "SNAP", "aris present");

    /* refuse stays 0 */
    {
        const int refuse[] = {3};

        inv_set(player, 0, obj_coins, 1);
        demon_talk(srv, player, npc_aris, aris_slot, refuse, 1);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 0,
                       "refuse must leave demonslayer_main=0, got %d",
                       ToriRSServer_VarbitGet(player, vb_main));
        if( ToriRSServer_VarbitGet(player, vb_main) == 0 )
            demon_pass("refuse", "OPNPC1", "demonslayer_main=0");
    }

    /* accept: Ok / fortune / Who's Delrith / fight cities */
    {
        const int accept[] = {1, 1, 1, 1};

        inv_set(player, 0, obj_coins, 1);
        demon_talk(srv, player, npc_aris, aris_slot, accept, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 1,
                       "Talk-to accept should write demonslayer_main=1, got %d",
                       ToriRSServer_VarbitGet(player, vb_main));
        if( ToriRSServer_VarbitGet(player, vb_main) == 1 )
            demon_pass("aris-accept", "OPNPC1", "demonslayer_main=1");
    }

    /* ---- SNAP Prysin ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3204, 3473);
    selftest_tick(srv);
    prysin_slot = selftest_require_npc(srv, npc_prysin, 3204, 3473, 0);
    if( prysin_slot >= 0 && selftest_find_npc(srv, npc_prysin) != prysin_slot )
        spawned_prysin = prysin_slot;
    SELFTEST_CHECK(prysin_slot >= 0, "Sir Prysin should stand in the palace");
    if( prysin_slot < 0 )
        goto demon_selftest_done;
    {
        const int keys[] = {3, 1, 1, 1};

        demon_talk(srv, player, npc_prysin, prysin_slot, keys, 4);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 2,
                       "Prysin key explanation should write demonslayer_main=2, got %d",
                       ToriRSServer_VarbitGet(player, vb_main));
        if( ToriRSServer_VarbitGet(player, vb_main) == 2 )
            demon_pass("prysin-keys", "OPNPC1", "demonslayer_main=2");
    }

    /* ---- Rovin key ---- */
    ToriRSServer_WorldTeleport(srv, 2, 3204, 3496);
    selftest_tick(srv);
    rovin_slot = selftest_require_npc(srv, npc_rovin, 3204, 3496, 2);
    if( rovin_slot >= 0 && srv->npcs[rovin_slot].x == 3204 && srv->npcs[rovin_slot].z == 3496 )
        spawned_rovin = -1;
    else
        spawned_rovin = rovin_slot;
    SELFTEST_CHECK(rovin_slot >= 0, "Captain Rovin should stand in the guardhouse");
    if( rovin_slot >= 0 )
    {
        const int important[] = {3, 1};

        demon_talk(srv, player, npc_rovin, rovin_slot, important, 2);
        SELFTEST_CHECK(demon_inv_total(player, obj_key2) == 1,
                       "Rovin Talk-to should grant silverlight_key_2, have %d",
                       demon_inv_total(player, obj_key2));
        if( demon_inv_total(player, obj_key2) == 1 )
            demon_pass("rovin-key", "OPNPC1", "silverlight_key_2=1");
    }

    /* ---- SNAP drain, pour ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3224, 3495);
    selftest_tick(srv);
    loc_slot = demon_find_loc(3224, 3495, 0, loc_drain, 12);
    if( loc_slot < 0 && loc_drain_key >= 0 )
        loc_slot = demon_find_loc(3224, 3495, 0, loc_drain_key, 12);
    SELFTEST_CHECK(loc_slot >= 0, "questdrain should stand by the palace kitchen");
    if( loc_slot >= 0 )
    {
        int use_id = loc_drain;
        struct ToriRSServerSceneLoc* drain_loc = ToriRSServer_SceneLoc(loc_slot);

        if( drain_loc && drain_loc->loc_id == loc_drain_key && loc_drain_key >= 0 )
            use_id = loc_drain_key;
        inv_set(player, 3, obj_bucket, 1);
        player->last_useitem = obj_bucket;
        demon_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, use_id, -1, loc_slot);
        demon_drain_to_choice(srv, player, -1);
        demon_drain_rewards(srv, player);
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_drain) == 1,
                       "pouring water should write delrith_drain_key=1, got %d",
                       ToriRSServer_VarbitGet(player, vb_drain));
        if( ToriRSServer_VarbitGet(player, vb_drain) == 1 )
            demon_pass("drain-pour", "OPLOCU", "delrith_drain_key=1");
    }

    /* ---- sewer key Take ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3225, 9897);
    selftest_tick(srv);
    loc_slot = -1;
    if( loc_sewer >= 0 )
        loc_slot = demon_find_loc(3225, 9897, 0, loc_sewer, 16);
    if( loc_slot < 0 && loc_sewer_mud >= 0 )
        loc_slot = demon_find_loc(3225, 9897, 0, loc_sewer_mud, 16);
    SELFTEST_CHECK(loc_slot >= 0, "sewer key loc should exist after the pour");
    if( loc_slot >= 0 )
    {
        int use_id = loc_sewer >= 0 ? loc_sewer : loc_sewer_mud;
        struct ToriRSServerSceneLoc* sewer_loc = ToriRSServer_SceneLoc(loc_slot);

        if( sewer_loc )
            use_id = sewer_loc->loc_id;
        demon_release(srv, player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, use_id, -1, loc_slot);
        demon_drain_rewards(srv, player);
        SELFTEST_CHECK(demon_inv_total(player, obj_key3) == 1,
                       "sewer Take should grant silverlight_key_3, have %d",
                       demon_inv_total(player, obj_key3));
        if( demon_inv_total(player, obj_key3) == 1 )
            demon_pass("sewer-key", "OPLOC1", "silverlight_key_3=1");
    }

    /* ---- SNAP Traiborn ---- */
    ToriRSServer_WorldTeleport(srv, 1, 3113, 3163);
    selftest_tick(srv);
    traiborn_slot = selftest_find_npc(srv, npc_traiborn);
    if( traiborn_slot < 0 )
    {
        traiborn_slot = npc_spawn(srv, npc_traiborn, 3113, 3163, 1);
        spawned_traiborn = traiborn_slot;
    }
    SELFTEST_CHECK(traiborn_slot >= 0, "Traiborn should be talkable in the Wizard Tower");
    if( traiborn_slot >= 0 )
    {
        const int ask[] = {3, 3, 2};

        demon_talk(srv, player, npc_traiborn, traiborn_slot, ask, 3);
        SELFTEST_CHECK(player->varps[varp_started] == 1,
                       "Traiborn should start the bone ritual, started=%d",
                       player->varps[varp_started]);
        if( player->varps[varp_started] == 1 )
            demon_pass("traiborn-ask", "OPNPC1", "traiborn_started=1");

        inv_set(player, 4, obj_bones, 25);
        demon_talk(srv, player, npc_traiborn, traiborn_slot, NULL, 0);
        SELFTEST_CHECK(player->varps[varp_bones] >= 25,
                       "25 unnoted bones should fill demon_bones_given, got %d",
                       player->varps[varp_bones]);
        SELFTEST_CHECK(demon_inv_total(player, obj_key1) == 1,
                       "Traiborn ritual should grant silverlight_key_1, have %d",
                       demon_inv_total(player, obj_key1));
        if( demon_inv_total(player, obj_key1) == 1 )
            demon_pass("traiborn-bones", "OPNPC1", "silverlight_key_1=1 bones=25");
    }

    /* ---- Silverlight exchange ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3204, 3473);
    selftest_tick(srv);
    if( prysin_slot < 0 || !srv->npcs[prysin_slot].active )
        prysin_slot = selftest_require_npc(srv, npc_prysin, 3204, 3473, 0);
    if( prysin_slot >= 0 )
    {
        demon_talk(srv, player, npc_prysin, prysin_slot, NULL, 0);
        SELFTEST_CHECK(demon_inv_total(player, obj_sword) == 1,
                       "three-key exchange should grant Silverlight, have %d",
                       demon_inv_total(player, obj_sword));
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_case) == 1,
                       "exchange should write delrith_silverlight_case=1, got %d",
                       ToriRSServer_VarbitGet(player, vb_case));
        SELFTEST_CHECK(demon_inv_total(player, obj_key1) == 0 &&
                           demon_inv_total(player, obj_key2) == 0 &&
                           demon_inv_total(player, obj_key3) == 0,
                       "exchange must consume all three keys");
        if( demon_inv_total(player, obj_sword) == 1 )
            demon_pass("silverlight", "OPNPC1", "silverlight=1 case=1");
    }

    /* ---- SNAP Delrith + god + named cheat ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3228, 3369);
    selftest_tick(srv);
    fprintf(stderr, "PASS demonslayer snap trigger=SNAP tile=3228,3369,0\n");
    fflush(stderr);
    delrith_slot = npc_spawn(srv, npc_delrith, 3228, 3369, 0);
    spawned_delrith = delrith_slot;
    SELFTEST_CHECK(delrith_slot >= 0, "Delrith should spawn in the stone circle");
    player->godmode = 1;
    qp_before = player->varps[varp_qp];
    demon_release(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "demon_skipboss");
    demon_drain_rewards(srv, player);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_main) == 3,
                   "named boss cheat should write demonslayer_main=3, got %d",
                   ToriRSServer_VarbitGet(player, vb_main));
    SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 3,
                   "completion should award +3 QP, %d -> %d",
                   qp_before, player->varps[varp_qp]);
    if( ToriRSServer_VarbitGet(player, vb_main) == 3 )
        fprintf(stderr, "PASS demonslayer boss=delrith CHEAT-SKIP\n");
    fflush(stderr);
    demon_pass("reward-scroll", "demon_skipboss", "main=3 qp+3");

demon_selftest_done:
    if( spawned_aris >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_aris);
    if( spawned_prysin >= 0 && srv->npcs[spawned_prysin].active &&
        srv->npcs[spawned_prysin].type == npc_prysin )
        ToriRSServer_WorldNpcFree(srv, spawned_prysin);
    if( spawned_rovin >= 0 && srv->npcs[spawned_rovin].active &&
        srv->npcs[spawned_rovin].type == npc_rovin )
        ToriRSServer_WorldNpcFree(srv, spawned_rovin);
    if( spawned_traiborn >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_traiborn);
    if( spawned_delrith >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_delrith);
    ToriRSServer_WorldNpcReap(srv);
    demon_inv_clear(player);
    player->godmode = 0;
    demon_pass("cleanup", "WorldNpcFree", "spawns reaped");
}
