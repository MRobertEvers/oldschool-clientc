/* Gertrude's Cat -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int
fluffs_coord_pack(int level, int x, int z)
{
    return (int)(((unsigned)level << 28) | ((unsigned)x << 14) | (unsigned)z);
}

static int
fluffs_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
fluffs_release_park(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
fluffs_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
fluffs_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = fluffs_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 64 && player->active_script; round++ )
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
fluffs_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = fluffs_chatmenu();

    assert(srv);
    assert(player);
    fluffs_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
fluffs_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    assert(srv);
    assert(player);
    fluffs_release_park(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
fluffs_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    fluffs_drain(srv, player, 0);
    for( i = 0; i < 48; i++ )
    {
        if( player->active_script && player->active_script->execution == SSVM_PAUSEBUTTON &&
            player->resume_button_count > 0 )
            ToriRSServer_ScriptsResumeButton(srv, player->resume_buttons[0]);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static int
fluffs_find_npc_at(const struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int i;

    assert(srv);
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type && srv->npcs[i].x == x &&
            srv->npcs[i].z == z && srv->npcs[i].level == level )
            return i;
    }
    return -1;
}

static int
fluffs_find_loc_at(int x, int z, int level, int loc_id, int radius)
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

static int
fluffs_kitten_total(const struct ToriRSServerPlayer* player, const int* ids, int n)
{
    int i;
    int total;

    assert(player);
    assert(ids);
    total = 0;
    for( i = 0; i < n; i++ )
        if( ids[i] > 0 )
            total += selftest_count(player, ids[i]);
    return total;
}

static void
selftest_quest_fluffs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    static const int crate_xz[6][2] = {
        {3305, 3500}, {3310, 3499}, {3307, 3507},
        {3303, 3506}, {3298, 3514}, {3315, 3515},
    };
    int loaded;
    int npc_gert;
    int npc_shilop;
    int npc_fluffs;
    int npc_crate;
    int loc_crate;
    int loc_fence;
    int varp;
    int varp_crate;
    int qp_varp;
    int cooking;
    int obj_coins;
    int obj_milk;
    int obj_bucket;
    int obj_doogle;
    int obj_sardine;
    int obj_seasoned;
    int obj_lost;
    int obj_cake;
    int obj_stew;
    int kittens[6];
    int g_slot;
    int s_slot;
    int f_slot;
    int spawned_g;
    int spawned_s;
    int spawned_f;
    int qp_before;
    int xp_before;
    int i;
    int crates_ok;
    int loc_hits;

    fprintf(stderr, "ToriRSServer selftest: quest_fluffs (Gertrude's Cat)\n");
    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded && srv->scripts_ok, "quest_fluffs selftest needs the compiled script pack");
    if( !loaded || !srv->scripts_ok )
        return;

    npc_gert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gertrude");
    npc_shilop = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "shilop");
    npc_fluffs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gertrudescat");
    npc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "kittens_mew");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "gertrudeempty_crate");
    loc_fence = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "gertrudefence");
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fluffs");
    varp_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "fluffs_crate");
    qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    cooking = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_milk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_milk");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
    obj_doogle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doogleleaves");
    obj_sardine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_sardine");
    obj_seasoned = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "seasoned_sardine");
    obj_lost = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gertrudekittens");
    obj_cake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chocolate_cake");
    obj_stew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "stew");
    kittens[0] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject");
    kittens[1] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_light");
    kittens[2] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_brown");
    kittens[3] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_black");
    kittens[4] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_browngrey");
    kittens[5] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject_bluegrey");

    SELFTEST_CHECK(npc_gert >= 0 && npc_shilop >= 0 && npc_fluffs >= 0 && npc_crate >= 0 &&
                       varp >= 0 && varp_crate >= 0 && obj_coins >= 0 && obj_milk >= 0 &&
                       obj_doogle >= 0 && obj_sardine >= 0 && obj_seasoned >= 0 &&
                       obj_lost >= 0 && kittens[0] >= 0,
                   "quest_fluffs symbols should resolve");
    if( npc_gert < 0 || npc_shilop < 0 || npc_fluffs < 0 || npc_crate < 0 || varp < 0 ||
        varp_crate < 0 || obj_lost < 0 )
        return;

    srv->members_world = 1;
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    player->varps[varp] = 0;
    player->varps[varp_crate] = 0;
    spawned_g = -1;
    spawned_s = -1;
    spawned_f = -1;

    /* ---- Gertrude refuse ---- */
    fluffs_snap(srv, player, 0, 3151, 3410);
    g_slot = selftest_find_npc(srv, npc_gert);
    if( g_slot < 0 )
    {
        g_slot = ToriRSServer_WorldNpcSpawn(srv, npc_gert, 3151, 3410, 0);
        spawned_g = g_slot;
    }
    SELFTEST_CHECK(g_slot >= 0, "Gertrude should exist at 3151,3410");
    if( g_slot < 0 )
        return;

    fluffs_talk(srv, player, npc_gert, g_slot);
    fluffs_choose(srv, player, 3);
    fluffs_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 0, "refuse must leave fluffs=0, got %d",
                   player->varps[varp]);
    fprintf(stderr, "PASS gertrudescat start_refuse trigger=opnpc1,gertrude observable=fluffs=0\n");

    /* ---- Gertrude accept ---- */
    fluffs_talk(srv, player, npc_gert, g_slot);
    fluffs_choose(srv, player, 1);
    fluffs_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 1, "accept should write fluffs=1, got %d",
                   player->varps[varp]);
    fprintf(stderr, "PASS gertrudescat start_accept trigger=opnpc1,gertrude observable=fluffs=1\n");

    /* ---- Shilop pay ---- */
    fluffs_snap(srv, player, 0, 3221, 3434);
    s_slot = selftest_find_npc(srv, npc_shilop);
    if( s_slot < 0 )
    {
        s_slot = ToriRSServer_WorldNpcSpawn(srv, npc_shilop, 3221, 3434, 0);
        spawned_s = s_slot;
    }
    SELFTEST_CHECK(s_slot >= 0, "Shilop should exist at the market");
    if( s_slot < 0 )
        goto fluffs_done;
    selftest_give(player, obj_coins, 100);
    fluffs_talk(srv, player, npc_shilop, s_slot);
    fluffs_choose(srv, player, 2);
    fluffs_choose(srv, player, 2);
    fluffs_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 2, "paid child should write fluffs=2, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_coins) == 0, "payment should consume 100 coins");
    fprintf(stderr, "PASS gertrudescat children_pay trigger=opnpc1,shilop observable=fluffs=2\n");

    /* ---- Six crate coords: world kittens_mew + SceneFindLocId ---- */
    fluffs_snap(srv, player, 0, 3306, 3506);
    crates_ok = 0;
    loc_hits = 0;
    for( i = 0; i < 6; i++ )
    {
        int nx = crate_xz[i][0];
        int nz = crate_xz[i][1];
        int nslot = fluffs_find_npc_at(srv, npc_crate, nx, nz, 0);
        int lslot = loc_crate > 0 ? fluffs_find_loc_at(nx, nz, 0, loc_crate, 0) : -1;

        if( lslot >= 0 )
            loc_hits++;
        if( nslot >= 0 )
            crates_ok++;
        else
            SELFTEST_CHECK(nslot >= 0, "kittens_mew must stand at %d,%d (roll %d)", nx, nz, i);
    }
    SELFTEST_CHECK(crates_ok == 6, "all six crate rolls must resolve, got %d", crates_ok);
    fprintf(stderr,
            "PASS gertrudescat crate_coords trigger=scene observable=kittens_mew=6 "
            "SceneFindLocId_gertrudeempty_crate=%d\n",
            loc_hits);
    if( loc_fence > 0 )
    {
        int fence_slot = fluffs_find_loc_at(3308, 3492, 0, loc_fence, 8);
        SELFTEST_CHECK(fence_slot >= 0, "gertrudefence should be placed at the lumber yard");
        if( fence_slot >= 0 )
            fprintf(stderr, "PASS gertrudescat fence trigger=SceneFindLocId,gertrudefence\n");
    }

    /* ---- Fluffs milk ---- */
    fluffs_snap(srv, player, 1, 3306, 3512);
    f_slot = fluffs_find_npc_at(srv, npc_fluffs, 3306, 3512, 1);
    if( f_slot < 0 )
        f_slot = selftest_find_npc(srv, npc_fluffs);
    if( f_slot < 0 )
    {
        f_slot = ToriRSServer_WorldNpcSpawn(srv, npc_fluffs, 3306, 3512, 1);
        spawned_f = f_slot;
    }
    SELFTEST_CHECK(f_slot >= 0, "Fluffs should exist upstairs in the lumber yard");
    if( f_slot < 0 )
        goto fluffs_done;
    selftest_give(player, obj_milk, 1);
    fluffs_release_park(srv, player);
    player->last_useitem = obj_milk;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_fluffs, -1, f_slot);
    fluffs_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 3, "milk should write fluffs=3, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(selftest_count(player, obj_milk) == 0, "milk should be consumed");
    if( obj_bucket > 0 )
        SELFTEST_CHECK(selftest_count(player, obj_bucket) == 1, "milk should leave an empty bucket");
    fprintf(stderr, "PASS gertrudescat milk trigger=opnpcu,gertrudescat observable=fluffs=3\n");

    /* ---- Seasoned sardine both directions, then feed ---- */
    selftest_give(player, obj_doogle, 1);
    selftest_give(player, obj_sardine, 1);
    player->last_slot = 0;
    player->last_item = obj_doogle;
    player->last_useitem = obj_sardine;
    player->last_useslot = 1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_doogle, -1, -1);
    fluffs_drain(srv, player, 0);
    SELFTEST_CHECK(selftest_count(player, obj_seasoned) == 1, "doogle-on-sardine should make seasoned sardine");
    fprintf(stderr, "PASS gertrudescat sardine_mix trigger=opheldu,doogleleaves observable=seasoned=1\n");

    fluffs_release_park(srv, player);
    player->last_useitem = obj_seasoned;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_fluffs, -1, f_slot);
    fluffs_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 4, "sardine should write fluffs=4, got %d",
                   player->varps[varp]);
    fprintf(stderr, "PASS gertrudescat sardine trigger=opnpcu,gertrudescat observable=fluffs=4\n");

    /* ---- Force each crate roll; only the matching NPC grants the kitten ---- */
    for( i = 0; i < 6; i++ )
    {
        int nx = crate_xz[i][0];
        int nz = crate_xz[i][1];
        int win = fluffs_find_npc_at(srv, npc_crate, nx, nz, 0);
        int other = fluffs_find_npc_at(srv, npc_crate, crate_xz[(i + 1) % 6][0],
                                      crate_xz[(i + 1) % 6][1], 0);

        player->varps[varp_crate] = fluffs_coord_pack(0, nx, nz);
        selftest_clear_inv(player);
        if( other >= 0 )
        {
            fluffs_release_park(srv, player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crate, -1, other);
            fluffs_drain(srv, player, 0);
            SELFTEST_CHECK(selftest_count(player, obj_lost) == 0,
                           "wrong crate roll %d must not grant the kitten", i);
        }
        SELFTEST_CHECK(win >= 0, "winning crate NPC missing for roll %d at %d,%d", i, nx, nz);
        if( win < 0 )
            continue;
        fluffs_release_park(srv, player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_crate, -1, win);
        fluffs_drain(srv, player, 0);
        SELFTEST_CHECK(selftest_count(player, obj_lost) == 1,
                       "crate roll %d at %d,%d should grant Fluffs' kitten", i, nx, nz);
    }
    fprintf(stderr, "PASS gertrudescat crate_search trigger=opnpc1,kittens_mew observable=all6\n");

    /* ---- Rescue must not delete the shared Fluffs NPC ---- */
    selftest_clear_inv(player);
    selftest_give(player, obj_lost, 1);
    player->varps[varp] = 4;
    fluffs_release_park(srv, player);
    player->last_useitem = obj_lost;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_fluffs, -1, f_slot);
    fluffs_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 5, "rescue should write fluffs=5, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(srv->npcs[f_slot].active, "rescue must not npc_del the shared Fluffs");
    SELFTEST_CHECK(srv->npcs[f_slot].type == npc_fluffs, "Fluffs type must survive rescue");
    fprintf(stderr, "PASS gertrudescat rescue trigger=opnpcu,gertrudescat observable=fluffs=5 npc_kept\n");

    /* ---- Atomic completion ---- */
    fluffs_snap(srv, player, 0, 3151, 3410);
    if( spawned_g < 0 )
        g_slot = selftest_find_npc(srv, npc_gert);
    if( g_slot < 0 )
    {
        g_slot = ToriRSServer_WorldNpcSpawn(srv, npc_gert, 3151, 3410, 0);
        spawned_g = g_slot;
    }
    SELFTEST_CHECK(g_slot >= 0, "Gertrude should exist for hand-in");
    if( g_slot < 0 )
        goto fluffs_done;
    selftest_clear_inv(player);
    qp_before = qp_varp >= 0 ? player->varps[qp_varp] : 0;
    xp_before = cooking >= 0 ? player->stat_xp_tenths[cooking] : 0;
    fluffs_talk(srv, player, npc_gert, g_slot);
    fluffs_drain_rewards(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 6, "completion should commit fluffs=6, got %d",
                   player->varps[varp]);
    SELFTEST_CHECK(fluffs_kitten_total(player, kittens, 6) == 1,
                   "completion should grant exactly one inventory kitten");
    SELFTEST_CHECK(obj_cake < 0 || selftest_count(player, obj_cake) == 1,
                   "completion should grant chocolate cake");
    SELFTEST_CHECK(obj_stew < 0 || selftest_count(player, obj_stew) == 1,
                   "completion should grant stew");
    if( cooking >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[cooking] == xp_before + 15250,
                       "completion should grant 1525 Cooking XP, %d -> %d", xp_before,
                       player->stat_xp_tenths[cooking]);
    if( qp_varp >= 0 )
        SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 1,
                       "completion should award 1 QP, %d -> %d", qp_before,
                       player->varps[qp_varp]);
    fprintf(stderr,
            "PASS gertrudescat complete trigger=opnpc1,gertrude observable=fluffs=6 "
            "kitten+cake+stew xp_delta=15250 qp_delta=1\n");

    /* ---- Post-quest ---- */
    fluffs_talk(srv, player, npc_gert, g_slot);
    fluffs_drain(srv, player, 0);
    SELFTEST_CHECK(player->varps[varp] == 6, "post-quest talk must stay at 6, got %d",
                   player->varps[varp]);
    fprintf(stderr, "PASS gertrudescat postquest trigger=opnpc1,gertrude observable=fluffs=6\n");

fluffs_done:
    if( spawned_g >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_g);
    if( spawned_s >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_s);
    if( spawned_f >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_f);
    ToriRSServer_WorldNpcReap(srv);
    fluffs_release_park(srv, player);
    fprintf(stderr, "PASS gertrudescat start-to-complete via real triggers\n");
}
