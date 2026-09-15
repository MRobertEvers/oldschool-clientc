/* Biohazard -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int
bio_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
bio_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    fprintf(stderr, "PASS biohazard %s trigger=%s %s\n", step, trigger, observable);
    fflush(stderr);
}

static void
bio_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
bio_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
bio_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = bio_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 200 && player->active_script; round++ )
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
bio_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = bio_chatmenu();

    assert(srv);
    assert(player);
    bio_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
bio_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    bio_drain(srv, player, 0);
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
bio_find_loc(int cx, int cz, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int slot;
    int s;

    slot = ToriRSServer_SceneFindLocId(cx, cz, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( dx = -radius; dx <= radius; dx++ )
    {
        for( dz = -radius; dz <= radius; dz++ )
        {
            slot = ToriRSServer_SceneFindLocId(cx + dx, cz + dz, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    }
    for( s = 0;; s++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(s);

        if( !loc )
            break;
        if( !loc->active || loc->level != level || loc->loc_id != loc_id )
            continue;
        if( loc->x + loc->size_x <= cx - radius || loc->x > cx + radius )
            continue;
        if( loc->z + loc->size_z <= cz - radius || loc->z > cz + radius )
            continue;
        return s;
    }
    return -1;
}

static int
bio_spawn_npc(
    struct ToriRSServer* srv,
    int npc_id,
    int x,
    int z,
    int level,
    int* spawned)
{
    int slot;

    assert(srv);
    assert(spawned);
    slot = selftest_find_npc(srv, npc_id);
    if( slot >= 0 )
        return slot;
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_id, x, z, level);
    if( slot >= 0 )
        *spawned = slot;
    return slot;
}

static void
bio_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_id, int slot)
{
    assert(srv);
    assert(player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_id, -1, slot);
}

static void
bio_wear(struct ToriRSServerPlayer* player, int slot, int obj_id)
{
    assert(player);
    player->worn[slot].obj_id = obj_id;
    player->worn[slot].count = 1;
}

static void
bio_clear_worn(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_WORN_SLOTS; i++ )
    {
        player->worn[i].obj_id = -1;
        player->worn[i].count = 0;
    }
}

static void
selftest_quest_biohazard(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_elena;
    int npc_jerico;
    int npc_omart;
    int npc_chemist;
    int npc_hops;
    int npc_chancy;
    int npc_artist;
    int npc_hops2;
    int npc_chancy2;
    int npc_artist2;
    int npc_guidor;
    int npc_lathas;
    int varp_bio;
    int varp_errand;
    int varp_elena;
    int varp_qp;
    int vb_home;
    int vb_omart;
    int obj_feed;
    int obj_pigeons;
    int obj_apple;
    int obj_gown;
    int obj_key;
    int obj_dist;
    int obj_sample;
    int obj_honey;
    int obj_eth;
    int obj_broline;
    int obj_paper;
    int obj_mask;
    int obj_priest_top;
    int obj_priest_bot;
    int loc_tower;
    int loc_tower_op;
    int loc_cauldron;
    int loc_cauldron_op;
    int loc_crate;
    int loc_gate;
    int elena_slot;
    int jerico_slot;
    int omart_slot;
    int chemist_slot;
    int hops_slot;
    int chancy_slot;
    int artist_slot;
    int guidor_slot;
    int lathas_slot;
    int spawned_elena;
    int spawned_jerico;
    int spawned_omart;
    int spawned_chemist;
    int spawned_hops;
    int spawned_chancy;
    int spawned_artist;
    int spawned_hops2;
    int spawned_chancy2;
    int spawned_artist2;
    int spawned_guidor;
    int spawned_lathas;
    int loc_slot;
    int qp_before;
    int x_before;
    int z_before;

    fprintf(stderr, "ToriRSServer selftest: quest_biohazard (Biohazard)\n");
    fflush(stderr);

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_biohazard selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_elena = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elena2");
    npc_jerico = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "jerico");
    npc_omart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "omart");
    npc_chemist = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "chemist");
    npc_hops = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "drunk1");
    npc_chancy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gambler1");
    npc_artist = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "artist1");
    npc_hops2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "drunk2");
    npc_chancy2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gambler2");
    npc_artist2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "artist2");
    npc_guidor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "guidor");
    npc_lathas = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "kinglathas");
    varp_bio = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "biohazard");
    varp_errand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "bioerrand");
    varp_elena = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "elenaquest");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    vb_home = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "plaguecity_elena_at_home");
    vb_omart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "biohazard_met_omart");
    obj_feed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "birdfeed");
    obj_pigeons = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pigeons");
    obj_apple = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rottenapples");
    obj_gown = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "doctor_gown");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mournerkeytw");
    obj_dist = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "distillator");
    obj_sample = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "plaguesample");
    obj_honey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "liquid_honey");
    obj_eth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ethenea");
    obj_broline = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sulphuric_broline");
    obj_paper = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "touch_paper");
    obj_mask = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gasmask");
    obj_priest_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "priest_gown");
    obj_priest_bot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "priest_robe");
    loc_tower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "biowatchtower");
    loc_tower_op = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "biowatchtower_op");
    loc_cauldron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mournercauldron");
    loc_cauldron_op = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mournercauldron_op");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mournercrateup");
    loc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "guidorgatelclosed");

    SELFTEST_CHECK(npc_elena > 0 && npc_jerico > 0 && npc_omart > 0 && npc_chemist > 0 &&
                       npc_guidor > 0 && npc_lathas > 0 && varp_bio > 0 && loc_tower_op > 0 &&
                       loc_cauldron_op > 0 && obj_feed > 0 && obj_sample > 0,
                   "quest_biohazard symbols should all resolve");
    if( npc_elena <= 0 || varp_bio <= 0 || loc_tower_op <= 0 )
    {
        if( owned )
            ToriRSServer_ScriptsFree(srv);
        return;
    }

    spawned_elena = -1;
    spawned_jerico = -1;
    spawned_omart = -1;
    spawned_chemist = -1;
    spawned_hops = -1;
    spawned_chancy = -1;
    spawned_artist = -1;
    spawned_hops2 = -1;
    spawned_chancy2 = -1;
    spawned_artist2 = -1;
    spawned_guidor = -1;
    spawned_lathas = -1;
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);
    selftest_clear_inv(player);
    bio_clear_worn(player);
    player->varps[varp_bio] = 0;
    if( varp_errand >= 0 )
        player->varps[varp_errand] = 0;
    if( varp_elena >= 0 )
        player->varps[varp_elena] = 0;
    bio_release(srv, player);

    /* ---- prerequisite: Plague City incomplete cannot start ---- */
    bio_snap(srv, player, 0, 2592, 3336);
    bio_pass("snap_elena", "SNAP", "tile=2592,3336,0");
    elena_slot = bio_spawn_npc(srv, npc_elena, 2592, 3336, 0, &spawned_elena);
    SELFTEST_CHECK(elena_slot >= 0, "Elena shell should spawn at her house");
    if( elena_slot < 0 )
        goto bio_selftest_done;
    bio_talk(srv, player, srv->npcs[elena_slot].type, elena_slot);
    bio_drain(srv, player, 0);
    bio_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_bio] == 0, "no Plague City should leave biohazard=0, got %d",
                   player->varps[varp_bio]);
    if( player->varps[varp_bio] == 0 )
        bio_pass("prereq_plaguecity", "opnpc1,elena2", "biohazard=0");

    /* ---- eligible refuse / accept ---- */
    if( varp_elena >= 0 )
        player->varps[varp_elena] = 29;
    if( vb_home >= 0 )
        ToriRSServer_VarbitSetOn(srv, player, vb_home, 1);
    bio_talk(srv, player, srv->npcs[elena_slot].type, elena_slot);
    bio_choose(srv, player, 2);
    bio_drain(srv, player, 0);
    bio_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_bio] == 0, "refuse should leave biohazard=0, got %d",
                   player->varps[varp_bio]);
    if( player->varps[varp_bio] == 0 )
        bio_pass("start_refuse", "opnpc1,elena2", "biohazard=0");

    bio_talk(srv, player, srv->npcs[elena_slot].type, elena_slot);
    bio_choose(srv, player, 1);
    bio_drain(srv, player, 0);
    bio_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_bio] == 1, "accept should set biohazard=1, got %d",
                   player->varps[varp_bio]);
    if( player->varps[varp_bio] == 1 )
        bio_pass("start_accept", "opnpc1,elena2", "biohazard=1");

    /* ---- Jerico 1->2 ---- */
    bio_snap(srv, player, 0, 2612, 3324);
    jerico_slot = bio_spawn_npc(srv, npc_jerico, 2612, 3324, 0, &spawned_jerico);
    SELFTEST_CHECK(jerico_slot >= 0, "Jerico should spawn");
    if( jerico_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[jerico_slot].type, jerico_slot);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 2, "Jerico should set biohazard=2, got %d",
                       player->varps[varp_bio]);
        if( player->varps[varp_bio] == 2 )
            bio_pass("talk_jerico", "opnpc1,jerico", "biohazard=2");
    }

    /* ---- watchtower leaf + feed 2->3 ---- */
    bio_snap(srv, player, 0, 2562, 3301);
    loc_slot = bio_find_loc(2562, 3301, 0, loc_tower, 16);
    if( loc_slot >= 0 )
        bio_pass("watchtower_shell", "SceneFindLocId", "biowatchtower");
    loc_slot = bio_find_loc(2562, 3301, 0, loc_tower_op, 16);
    if( loc_slot < 0 )
        loc_slot = bio_find_loc(2562, 3300, 0, loc_tower_op, 16);
    SELFTEST_CHECK(loc_slot >= 0 || loc_tower_op > 0,
                   "biowatchtower_op leaf should be reachable after state 2");
    if( loc_slot >= 0 )
        bio_pass("watchtower_leaf", "SceneFindLocId", "biowatchtower_op");
    if( obj_feed > 0 )
        selftest_give(player, obj_feed, 1);
    player->last_useitem = obj_feed;
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_tower_op, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_tower_op, -1, -1);
    bio_drain(srv, player, 0);
    bio_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_bio] == 3, "feed leaf should set biohazard=3, got %d",
                   player->varps[varp_bio]);
    if( player->varps[varp_bio] == 3 )
        bio_pass("watchtower_feed", "oplocu,biowatchtower_op", "biohazard=3");

    /* ---- pigeons 3->4 ---- */
    if( obj_pigeons > 0 )
        selftest_give(player, obj_pigeons, 1);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_pigeons, -1, -1);
    bio_drain(srv, player, 0);
    bio_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_bio] == 4, "pigeons should set biohazard=4, got %d",
                   player->varps[varp_bio]);
    if( player->varps[varp_bio] == 4 )
        bio_pass("release_pigeons", "opheld1,pigeons", "biohazard=4");

    /* ---- Omart: no mask rejected, then mask crosses 4->5 ---- */
    bio_snap(srv, player, 0, 2559, 3266);
    omart_slot = bio_spawn_npc(srv, npc_omart, 2559, 3266, 0, &spawned_omart);
    SELFTEST_CHECK(omart_slot >= 0, "Omart shell should spawn");
    if( omart_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[omart_slot].type, omart_slot);
        bio_choose(srv, player, 1);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 4, "Omart without mask must stay 4, got %d",
                       player->varps[varp_bio]);
        if( player->varps[varp_bio] == 4 )
            bio_pass("omart_nomask", "opnpc1,omart", "biohazard=4");
        if( obj_mask > 0 )
            bio_wear(player, TORIRSSERVER_WEAR_HEAD, obj_mask);
        bio_talk(srv, player, srv->npcs[omart_slot].type, omart_slot);
        bio_choose(srv, player, 1);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 5, "Omart with mask should set 5, got %d",
                       player->varps[varp_bio]);
        if( player->varps[varp_bio] == 5 )
            bio_pass("omart_cross", "opnpc1,omart", "biohazard=5");
        if( vb_omart >= 0 )
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_omart) == 1, "met_omart should settle");
    }

    /* ---- cauldron leaf 5->6 ---- */
    bio_snap(srv, player, 0, 2547, 3321);
    loc_slot = bio_find_loc(2547, 3321, 0, loc_cauldron, 16);
    if( loc_slot >= 0 )
        bio_pass("cauldron_shell", "SceneFindLocId", "mournercauldron");
    loc_slot = bio_find_loc(2547, 3321, 0, loc_cauldron_op, 16);
    if( loc_slot < 0 )
        loc_slot = bio_find_loc(2551, 3320, 0, loc_cauldron_op, 16);
    if( loc_slot >= 0 )
        bio_pass("cauldron_leaf", "SceneFindLocId", "mournercauldron_op");
    if( obj_apple > 0 )
        selftest_give(player, obj_apple, 1);
    player->last_useitem = obj_apple;
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cauldron_op, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_cauldron_op, -1, -1);
    bio_drain(srv, player, 0);
    bio_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_bio] == 6, "cauldron leaf should set biohazard=6, got %d",
                   player->varps[varp_bio]);
    if( player->varps[varp_bio] == 6 )
        bio_pass("cauldron_poison", "oplocu,mournercauldron_op", "biohazard=6");

    /* ---- mourner key cheat-skip (no boss AI) + crate 6->7 ---- */
    if( obj_gown > 0 )
        bio_wear(player, TORIRSSERVER_WEAR_BODY, obj_gown);
    ToriRSServer_ScriptsRunDebugproc(srv, "biohazard_pass_mourner");
    bio_drain(srv, player, 0);
    bio_release(srv, player);
    if( obj_key > 0 )
        SELFTEST_CHECK(selftest_count(player, obj_key) >= 1, "mourner cheat-skip should grant key");
    bio_pass("mourner_skip", "debugproc,biohazard_pass_mourner",
             "PASS biohazard boss=mournerstew2 CHEAT-SKIP");

    bio_snap(srv, player, 1, 2554, 3327);
    loc_slot = bio_find_loc(2554, 3327, 1, loc_crate, 8);
    if( loc_slot < 0 )
        loc_slot = bio_find_loc(2555, 3327, 1, loc_crate, 8);
    SELFTEST_CHECK(loc_slot >= 0, "mournercrateup should stand upstairs third-from-left");
    if( loc_slot >= 0 )
    {
        bio_pass("crate_shell", "SceneFindLocId", "mournercrateup");
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_crate, -1, loc_slot);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 7, "crate should set biohazard=7, got %d",
                       player->varps[varp_bio]);
        if( obj_dist > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_dist) >= 1, "crate should grant distillator");
        if( player->varps[varp_bio] == 7 )
            bio_pass("search_crate", "oploc1,mournercrateup", "biohazard=7");
    }

    /* ---- Elena analysis 7->10 ---- */
    bio_snap(srv, player, 0, 2592, 3336);
    elena_slot = bio_spawn_npc(srv, npc_elena, 2592, 3336, 0, &spawned_elena);
    if( elena_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[elena_slot].type, elena_slot);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 10, "Elena analysis should set 10, got %d",
                       player->varps[varp_bio]);
        if( obj_sample > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_sample) >= 1, "Elena should grant the sample");
        if( player->varps[varp_bio] == 10 )
            bio_pass("elena_analysis", "opnpc1,elena2", "biohazard=10");
    }

    /* ---- Chemist: no confiscation, 10->12 ---- */
    bio_snap(srv, player, 0, 2933, 3210);
    chemist_slot = bio_spawn_npc(srv, npc_chemist, 2933, 3210, 0, &spawned_chemist);
    if( chemist_slot >= 0 )
    {
        int sample_before = obj_sample > 0 ? selftest_count(player, obj_sample) : 0;

        bio_talk(srv, player, srv->npcs[chemist_slot].type, chemist_slot);
        bio_choose(srv, player, 1);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        if( obj_sample > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_sample) >= sample_before,
                           "chemist must not confiscate the plague sample");
        SELFTEST_CHECK(player->varps[varp_bio] == 12, "chemist should set biohazard=12, got %d",
                       player->varps[varp_bio]);
        if( obj_paper > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_paper) >= 1, "chemist should grant touch paper");
        if( player->varps[varp_bio] == 12 )
            bio_pass("talk_chemist", "opnpc1,chemist", "biohazard=12");
    }

    /* ---- errand boys: correct vials ---- */
    bio_snap(srv, player, 0, 2930, 3220);
    hops_slot = bio_spawn_npc(srv, npc_hops, 2930, 3220, 0, &spawned_hops);
    chancy_slot = bio_spawn_npc(srv, npc_chancy, 2931, 3220, 0, &spawned_chancy);
    artist_slot = bio_spawn_npc(srv, npc_artist, 2932, 3220, 0, &spawned_artist);
    if( hops_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[hops_slot].type, hops_slot);
        bio_choose(srv, player, 3);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        bio_pass("hops_broline", "opnpc1,drunk1", "correct");
    }
    if( chancy_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[chancy_slot].type, chancy_slot);
        bio_choose(srv, player, 2);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        bio_pass("chancy_honey", "opnpc1,gambler1", "correct");
    }
    if( artist_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[artist_slot].type, artist_slot);
        bio_choose(srv, player, 1);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        bio_pass("davinci_ethenea", "opnpc1,artist1", "correct");
    }

    /* ---- Varrock inspection leaf (gate loc) + retrieval ---- */
    bio_snap(srv, player, 0, 3270, 3390);
    loc_slot = bio_find_loc(3270, 3390, 0, loc_gate, 16);
    if( loc_slot >= 0 )
        bio_pass("varrock_gate", "SceneFindLocId", "guidorgatelclosed");
    hops_slot = bio_spawn_npc(srv, npc_hops2, 3270, 3391, 0, &spawned_hops2);
    chancy_slot = bio_spawn_npc(srv, npc_chancy2, 3271, 3391, 0, &spawned_chancy2);
    artist_slot = bio_spawn_npc(srv, npc_artist2, 3272, 3391, 0, &spawned_artist2);
    if( hops_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[hops_slot].type, hops_slot);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        if( obj_broline > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_broline) >= 1, "Hops should return broline");
        bio_pass("hops_collect", "opnpc1,drunk2", "vial_back");
    }
    if( chancy_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[chancy_slot].type, chancy_slot);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        if( obj_honey > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_honey) >= 1, "Chancy should return honey");
        bio_pass("chancy_collect", "opnpc1,gambler2", "vial_back");
    }
    if( artist_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[artist_slot].type, artist_slot);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        if( obj_eth > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_eth) >= 1, "Da Vinci should return ethenea");
        bio_pass("davinci_collect", "opnpc1,artist2", "vial_back");
    }

    /* ---- Guidor 12->14 ---- */
    if( obj_priest_top > 0 )
        bio_wear(player, TORIRSSERVER_WEAR_BODY, obj_priest_top);
    if( obj_priest_bot > 0 )
        bio_wear(player, TORIRSSERVER_WEAR_LEGS, obj_priest_bot);
    bio_snap(srv, player, 0, 3284, 3382);
    guidor_slot = bio_spawn_npc(srv, npc_guidor, 3284, 3382, 0, &spawned_guidor);
    if( guidor_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[guidor_slot].type, guidor_slot);
        bio_choose(srv, player, 1);
        bio_drain(srv, player, 1);
        bio_choose(srv, player, 2);
        bio_drain(srv, player, 1);
        bio_choose(srv, player, 1);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 14, "Guidor should set biohazard=14, got %d",
                       player->varps[varp_bio]);
        if( player->varps[varp_bio] == 14 )
            bio_pass("talk_guidor", "opnpc1,guidor", "biohazard=14");
    }

    /* ---- Elena report 14->15 ---- */
    bio_snap(srv, player, 0, 2592, 3336);
    elena_slot = bio_spawn_npc(srv, npc_elena, 2592, 3336, 0, &spawned_elena);
    if( elena_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[elena_slot].type, elena_slot);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 15, "Elena report should set 15, got %d",
                       player->varps[varp_bio]);
        if( player->varps[varp_bio] == 15 )
            bio_pass("elena_report", "opnpc1,elena2", "biohazard=15");
    }

    /* ---- West Ardougne teleport rejected at 15 ---- */
    x_before = player->x;
    z_before = player->z;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1,
                                   ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "teletab_westardy"),
                                   -1, -1);
    bio_drain(srv, player, 0);
    bio_release(srv, player);
    SELFTEST_CHECK(player->varps[varp_bio] == 15 && player->x == x_before && player->z == z_before,
                   "West Ardougne tablet must not fire at state 15");
    if( player->varps[varp_bio] == 15 && player->x == x_before && player->z == z_before )
        bio_pass("tele_blocked", "opheld1,teletab_westardy", "state=15 blocked");

    /* ---- Lathas complete 15->16 ---- */
    bio_snap(srv, player, 1, 2578, 3293);
    lathas_slot = bio_spawn_npc(srv, npc_lathas, 2578, 3293, 1, &spawned_lathas);
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    if( lathas_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[lathas_slot].type, lathas_slot);
        bio_choose(srv, player, 1);
        bio_drain_rewards(srv, player);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 16, "Lathas should complete to 16, got %d",
                       player->varps[varp_bio]);
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 3, "completion should award 3 QP");
        if( player->varps[varp_bio] == 16 )
            bio_pass("lathas_complete", "opnpc1,kinglathas", "biohazard=16");
    }

    /* ---- postquest Elena ---- */
    bio_snap(srv, player, 0, 2592, 3336);
    elena_slot = bio_spawn_npc(srv, npc_elena, 2592, 3336, 0, &spawned_elena);
    if( elena_slot >= 0 )
    {
        bio_talk(srv, player, srv->npcs[elena_slot].type, elena_slot);
        bio_drain(srv, player, 0);
        bio_release(srv, player);
        SELFTEST_CHECK(player->varps[varp_bio] == 16, "postquest must stay complete");
        if( player->varps[varp_bio] == 16 )
            bio_pass("postquest_elena", "opnpc1,elena2", "biohazard=16");
    }

    bio_pass("cleanup", "WorldNpcFree", "spawns reaped");

bio_selftest_done:
    if( spawned_elena >= 0 && srv->npcs[spawned_elena].active )
        ToriRSServer_WorldNpcFree(srv, spawned_elena);
    if( spawned_jerico >= 0 && srv->npcs[spawned_jerico].active )
        ToriRSServer_WorldNpcFree(srv, spawned_jerico);
    if( spawned_omart >= 0 && srv->npcs[spawned_omart].active )
        ToriRSServer_WorldNpcFree(srv, spawned_omart);
    if( spawned_chemist >= 0 && srv->npcs[spawned_chemist].active )
        ToriRSServer_WorldNpcFree(srv, spawned_chemist);
    if( spawned_hops >= 0 && srv->npcs[spawned_hops].active )
        ToriRSServer_WorldNpcFree(srv, spawned_hops);
    if( spawned_chancy >= 0 && srv->npcs[spawned_chancy].active )
        ToriRSServer_WorldNpcFree(srv, spawned_chancy);
    if( spawned_artist >= 0 && srv->npcs[spawned_artist].active )
        ToriRSServer_WorldNpcFree(srv, spawned_artist);
    if( spawned_hops2 >= 0 && srv->npcs[spawned_hops2].active )
        ToriRSServer_WorldNpcFree(srv, spawned_hops2);
    if( spawned_chancy2 >= 0 && srv->npcs[spawned_chancy2].active )
        ToriRSServer_WorldNpcFree(srv, spawned_chancy2);
    if( spawned_artist2 >= 0 && srv->npcs[spawned_artist2].active )
        ToriRSServer_WorldNpcFree(srv, spawned_artist2);
    if( spawned_guidor >= 0 && srv->npcs[spawned_guidor].active )
        ToriRSServer_WorldNpcFree(srv, spawned_guidor);
    if( spawned_lathas >= 0 && srv->npcs[spawned_lathas].active )
        ToriRSServer_WorldNpcFree(srv, spawned_lathas);
    ToriRSServer_WorldNpcReap(srv);
    bio_release(srv, player);
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
