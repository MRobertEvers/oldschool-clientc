/* Clock Tower -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static void
cog_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    fprintf(stderr, "PASS clocktower %s trigger=%s %s\n", step, trigger, observable);
    fflush(stderr);
}

static int
cog_progress(const struct ToriRSServerPlayer* player, int varp_cogquest)
{
    assert(player);
    if( varp_cogquest < 0 || varp_cogquest >= TORIRSSERVER_VARP_COUNT )
        return -1;
    return player->varps[varp_cogquest] & 0xF;
}

static int
cog_find_loc(int cx, int cz, int level, int loc_id, int radius)
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

static void
cog_resume_pages(struct ToriRSServer* srv, int choice_slot, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chat_left;
    int chat_right;
    int messagebox;
    int chatmenu;
    int n;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chat_left = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chat_left:continue");
    chat_right = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chat_right:continue");
    messagebox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    for( n = 0; n < max_pages && player->active_script; n++ )
    {
        if( chatmenu > 0 && player->resume_button_count > 0 &&
            player->resume_buttons[0] == chatmenu )
        {
            player->last_slot = choice_slot;
            ToriRSServer_ScriptsResumeButton(srv, chatmenu);
            continue;
        }
        if( player->active_script &&
            (player->active_script->execution == SSVM_SUSPENDED ||
             player->active_script->execution == SSVM_NPC_SUSPENDED ||
             player->active_script->execution == SSVM_WORLD_SUSPENDED) )
        {
            selftest_tick(srv);
            continue;
        }
        if( chat_left > 0 && ToriRSServer_ScriptsResumeButton(srv, chat_left) )
            continue;
        if( chat_right > 0 && ToriRSServer_ScriptsResumeButton(srv, chat_right) )
            continue;
        if( messagebox > 0 && ToriRSServer_ScriptsResumeButton(srv, messagebox) )
            continue;
        break;
    }
}

static void
cog_drain_rewards(struct ToriRSServer* srv)
{
    int i;

    assert(srv);
    /* Resume pause FIRST -- WorldCloseModal aborts a parked script. */
    cog_resume_pages(srv, 1, 32);
    for( i = 0; i < 40; i++ )
    {
        cog_resume_pages(srv, 1, 8);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
cog_clear_inv(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
cog_take_ground(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int obj_id,
    int x,
    int z,
    int level)
{
    int ground;

    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    selftest_tick(srv);
    ground = ToriRSServer_WorldGroundFind(srv, x, z, level, obj_id);
    if( ground < 0 )
        ground = ToriRSServer_WorldObjAdd(srv, obj_id, 1, x, z, level, -1);
    SELFTEST_CHECK(ground >= 0, "clocktower ground obj %d should exist at %d,%d", obj_id, x, z);
    if( ground < 0 )
        return;
    srv->pending_active_obj = ToriRSServer_WorldObjHandle(srv, ground);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, obj_id, -1, -1);
    srv->pending_active_obj = 0;
    cog_resume_pages(srv, 1, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    selftest_tick(srv);
}

static void
cog_use_on_loc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int loc_id,
    int cx,
    int cz,
    int level,
    int useitem)
{
    int loc_slot;

    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, cx, cz);
    selftest_tick(srv);
    loc_slot = cog_find_loc(cx, cz, level, loc_id, 6);
    SELFTEST_CHECK(loc_slot >= 0, "clocktower loc %d should stand at %d,%d,%d", loc_id, cx, cz,
                   level);
    if( loc_slot < 0 )
        return;
    player->last_useitem = useitem;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    cog_resume_pages(srv, 1, 8);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    player->last_useitem = -1;
    selftest_tick(srv);
}

static void
selftest_quest_cog(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_kojo;
    int varp_cogquest;
    int varp_cog_bits;
    int varp_qp;
    int obj_red;
    int obj_blue;
    int obj_black;
    int obj_white;
    int obj_poison;
    int obj_coins;
    int obj_ice;
    int obj_smiths;
    int loc_pole_red;
    int loc_pole_blue;
    int loc_pole_black;
    int loc_pole_white;
    int loc_ctlevera;
    int loc_ctlevera2;
    int loc_ctfoodtrough;
    int loc_ctratgatec;
    int loc_secretdoor2;
    int loc_prisondooropen;
    int kojo_slot;
    int spawned;
    int loc_slot;
    int qp_before;
    int coins_before;
    int progress;

    fprintf(stderr, "ToriRSServer selftest: quest_cog (Clock Tower)\n");
    fflush(stderr);

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_cog selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_kojo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brother_kojo");
    varp_cogquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "cogquest");
    varp_cog_bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "cog_bits");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    obj_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "redcog");
    obj_blue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bluecog");
    obj_black = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blackcog");
    obj_white = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "whitecog");
    obj_poison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rat_poison");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_ice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ice_gloves");
    obj_smiths = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "smithing_uniform_gloves_ice");
    loc_pole_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brokeclockpole_red");
    loc_pole_blue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brokeclockpole_blue");
    loc_pole_black = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brokeclockpole_black");
    loc_pole_white = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "brokeclockpole_white");
    loc_ctlevera = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ctlevera");
    loc_ctlevera2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ctlevera2");
    loc_ctfoodtrough = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ctfoodtrough");
    loc_ctratgatec = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ctratgatec");
    loc_secretdoor2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "secretdoor2");
    loc_prisondooropen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "prisondooropen");

    SELFTEST_CHECK(npc_kojo > 0 && varp_cogquest > 0 && varp_cog_bits > 0 && obj_red > 0 &&
                       obj_blue > 0 && obj_black > 0 && obj_white > 0 && obj_poison > 0 &&
                       loc_pole_red > 0 && loc_pole_blue > 0 && loc_pole_black > 0 &&
                       loc_pole_white > 0 && loc_ctlevera > 0 && loc_ctfoodtrough > 0 &&
                       loc_ctratgatec > 0 && loc_secretdoor2 > 0 && obj_ice > 0 &&
                       obj_smiths > 0 && varp_cogquest < TORIRSSERVER_VARP_COUNT &&
                       varp_cog_bits < TORIRSSERVER_VARP_COUNT,
                   "Clock Tower symbols should all resolve");
    if( npc_kojo <= 0 || varp_cogquest <= 0 || varp_cog_bits <= 0 || obj_red <= 0 ||
        obj_blue <= 0 || obj_black <= 0 || obj_white <= 0 || obj_poison <= 0 ||
        loc_pole_red <= 0 || loc_pole_blue <= 0 || loc_pole_black <= 0 ||
        loc_pole_white <= 0 || loc_ctlevera <= 0 || loc_ctfoodtrough <= 0 ||
        loc_ctratgatec <= 0 || loc_secretdoor2 <= 0 ||
        varp_cogquest >= TORIRSSERVER_VARP_COUNT || varp_cog_bits >= TORIRSSERVER_VARP_COUNT )
    {
        if( owned )
            ToriRSServer_ScriptsFree(srv);
        return;
    }

    spawned = -1;
    player->world = srv;
    player->active = 1;
    ToriRSServer_WorldSetActive(srv, player);
    cog_clear_inv(player);
    player->varps[varp_cogquest] = 0;
    player->varps[varp_cog_bits] = 0;
    player->last_slot = -1;
    player->godmode = 1;
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* ---- SNAP Kojo, refuse stays 0 ---- */
    ToriRSServer_WorldTeleport(srv, 0, 2569, 3249);
    selftest_tick(srv);
    fprintf(stderr, "PASS clocktower snap trigger=SNAP tile=2569,3249,0\n");
    fflush(stderr);

    kojo_slot = selftest_find_npc(srv, npc_kojo);
    if( kojo_slot < 0 )
    {
        kojo_slot = ToriRSServer_WorldNpcSpawn(srv, npc_kojo, 2570, 3249, 0);
        spawned = kojo_slot;
    }
    SELFTEST_CHECK(kojo_slot >= 0, "Brother Kojo should exist in the Clock Tower");
    if( kojo_slot < 0 )
        goto cog_selftest_done;

    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kojo, -1, kojo_slot);
    cog_resume_pages(srv, 2, 24);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    progress = cog_progress(player, varp_cogquest);
    SELFTEST_CHECK(progress == 0, "refuse should leave cogquest low bits at 0, got %d", progress);
    if( progress == 0 )
        cog_pass("start_refuse", "OPNPC1", "progress=0");

    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kojo, -1, kojo_slot);
    cog_resume_pages(srv, 1, 32);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    progress = cog_progress(player, varp_cogquest);
    SELFTEST_CHECK(progress == 1, "accept should set progress=1, got %d", progress);
    if( progress == 1 )
        cog_pass("start_accept", "OPNPC1 last_slot=1", "progress=1");

    /* ---- red cog take + place ---- */
    cog_take_ground(srv, player, obj_red, 2583, 9613, 0);
    SELFTEST_CHECK(selftest_count_obj(player, obj_red) == 1, "taking redcog should put it in inv");
    if( selftest_count_obj(player, obj_red) == 1 )
        cog_pass("take_red", "OPOBJ3", "inv_red=1");
    cog_use_on_loc(srv, player, loc_pole_red, 2568, 3243, 0, obj_red);
    progress = cog_progress(player, varp_cogquest);
    SELFTEST_CHECK(selftest_count_obj(player, obj_red) == 0, "red spindle should consume redcog");
    SELFTEST_CHECK(progress == 2, "placing red should set progress=2, got %d", progress);
    if( progress == 2 && selftest_count_obj(player, obj_red) == 0 )
        cog_pass("place_red", "OPLOCU", "progress=2");

    /* ---- blue cog take + place ---- */
    cog_take_ground(srv, player, obj_blue, 2574, 9633, 0);
    SELFTEST_CHECK(selftest_count_obj(player, obj_blue) == 1, "taking bluecog should put it in inv");
    if( selftest_count_obj(player, obj_blue) == 1 )
        cog_pass("take_blue", "OPOBJ3", "inv_blue=1");
    loc_slot = cog_find_loc(2572, 9631, 0, loc_secretdoor2, 8);
    SELFTEST_CHECK(loc_slot >= 0, "secretdoor2 shell should stand near the blue cell");
    if( loc_slot >= 0 )
        cog_pass("secret_wall", "SceneFindLocId", "secretdoor2");
    cog_use_on_loc(srv, player, loc_pole_blue, 2569, 3240, 1, obj_blue);
    progress = cog_progress(player, varp_cogquest);
    SELFTEST_CHECK(selftest_count_obj(player, obj_blue) == 0, "blue spindle should consume bluecog");
    SELFTEST_CHECK(progress == 3, "placing blue should set progress=3, got %d", progress);
    if( progress == 3 )
        cog_pass("place_blue", "OPLOCU", "progress=3");

    /* ---- black cog: ice gloves cool+take in one click ---- */
    worn_set(player, TORIRSSERVER_WEAR_HANDS, obj_ice, 1);
    cog_take_ground(srv, player, obj_black, 2613, 9639, 0);
    SELFTEST_CHECK(selftest_count_obj(player, obj_black) == 1,
                   "ice gloves should cool and take blackcog, got %d",
                   selftest_count_obj(player, obj_black));
    if( selftest_count_obj(player, obj_black) == 1 )
        cog_pass("take_black", "OPOBJ3", "ice_gloves cool+take");
    worn_set(player, TORIRSSERVER_WEAR_HANDS, -1, 0);
    cog_use_on_loc(srv, player, loc_pole_black, 2570, 9642, 0, obj_black);
    progress = cog_progress(player, varp_cogquest);
    SELFTEST_CHECK(selftest_count_obj(player, obj_black) == 0,
                   "black spindle should consume blackcog");
    SELFTEST_CHECK(progress == 4, "placing black should set progress=4, got %d", progress);
    if( progress == 4 )
        cog_pass("place_black", "OPLOCU", "progress=4");

    /* Smiths gloves (i) remain a valid cool method on a souvenir spawn. */
    worn_set(player, TORIRSSERVER_WEAR_HANDS, obj_smiths, 1);
    cog_take_ground(srv, player, obj_black, 2613, 9639, 0);
    SELFTEST_CHECK(selftest_count_obj(player, obj_black) == 1,
                   "Smiths gloves (i) should take a cooled/souvenir blackcog");
    if( selftest_count_obj(player, obj_black) == 1 )
        cog_pass("take_black_smiths", "OPOBJ3", "smithing_uniform_gloves_ice");
    worn_set(player, TORIRSSERVER_WEAR_HANDS, -1, 0);
    if( selftest_count_obj(player, obj_black) > 0 )
    {
        int i;
        for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
            if( player->inv[i].obj_id == obj_black )
                inv_set(player, i, -1, 0);
    }

    /* ---- white path: lever/cage, trough, western gate ---- */
    ToriRSServer_WorldTeleport(srv, 0, 2591, 9661);
    selftest_tick(srv);
    loc_slot = cog_find_loc(2591, 9661, 0, loc_ctlevera, 2);
    SELFTEST_CHECK(loc_slot >= 0, "ctlevera should stand at 2591,9661");
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_ctlevera, -1, loc_slot);
        selftest_tick(srv);
        SELFTEST_CHECK(cog_find_loc(2591, 9661, 0, loc_ctlevera2, 2) >= 0,
                       "pulling ctlevera should swap it to ctlevera2");
        if( loc_prisondooropen > 0 )
            SELFTEST_CHECK(cog_find_loc(2596, 9657, 0, loc_prisondooropen, 2) >= 0,
                           "pulling ctlevera should open the rat-cage door");
        if( cog_find_loc(2591, 9661, 0, loc_ctlevera2, 2) >= 0 )
            cog_pass("lever_cage", "OPLOC1", "ctlevera2 + cage open");
    }

    if( selftest_count_obj(player, obj_poison) < 1 )
        inv_set(player, inv_first_free(player), obj_poison, 1);
    cog_use_on_loc(srv, player, loc_ctfoodtrough, 2586, 9654, 0, obj_poison);
    SELFTEST_CHECK((player->varps[varp_cogquest] & (1 << 4)) != 0,
                   "poisoning the trough should set the rat-door bit");
    if( player->varps[varp_cogquest] & (1 << 4) )
        cog_pass("poison_trough", "OPLOCU", "rat_door_bit");

    ToriRSServer_WorldTeleport(srv, 0, 2575, 9651);
    selftest_tick(srv);
    loc_slot = cog_find_loc(2575, 9651, 0, loc_ctratgatec, 8);
    SELFTEST_CHECK(loc_slot >= 0, "ctratgatec western gate should exist near the white cog");
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_ctratgatec, -1, loc_slot);
        cog_resume_pages(srv, 1, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        cog_pass("west_gate", "OPLOC1", "ctratgatec");
    }

    cog_take_ground(srv, player, obj_white, 2578, 9655, 0);
    SELFTEST_CHECK(selftest_count_obj(player, obj_white) == 1, "taking whitecog should put it in inv");
    if( selftest_count_obj(player, obj_white) == 1 )
        cog_pass("take_white", "OPOBJ3", "inv_white=1");
    cog_use_on_loc(srv, player, loc_pole_white, 2567, 3241, 2, obj_white);
    progress = cog_progress(player, varp_cogquest);
    SELFTEST_CHECK(selftest_count_obj(player, obj_white) == 0,
                   "white spindle should consume whitecog");
    SELFTEST_CHECK(progress == 5, "placing white should set progress=5, got %d", progress);
    if( progress == 5 )
        cog_pass("place_white", "OPLOCU", "progress=5");

    /* ---- SNAP Kojo, complete via real talk ---- */
    ToriRSServer_WorldTeleport(srv, 0, 2569, 3249);
    selftest_tick(srv);
    kojo_slot = selftest_find_npc(srv, npc_kojo);
    if( kojo_slot < 0 )
    {
        kojo_slot = ToriRSServer_WorldNpcSpawn(srv, npc_kojo, 2570, 3249, 0);
        spawned = kojo_slot;
    }
    SELFTEST_CHECK(kojo_slot >= 0, "Brother Kojo should exist for the hand-in");
    if( kojo_slot >= 0 )
    {
        qp_before = (varp_qp > 0) ? player->varps[varp_qp] : 0;
        coins_before = selftest_count_obj(player, obj_coins);

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kojo, -1, kojo_slot);
        cog_drain_rewards(srv);

        progress = cog_progress(player, varp_cogquest);
        SELFTEST_CHECK(progress == 8, "Kojo finish should set progress=8, got %d", progress);
        SELFTEST_CHECK(selftest_count_obj(player, obj_coins) >= coins_before + 500,
                       "finish should grant 500 coins, %d -> %d", coins_before,
                       selftest_count_obj(player, obj_coins));
        if( varp_qp > 0 )
            SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1,
                           "finish should award 1 QP, %d -> %d", qp_before,
                           player->varps[varp_qp]);
        if( progress == 8 )
            cog_pass("handin_complete", "OPNPC1", "progress=8 coins+500 qp+1");

        /* Native 6/7 must resume, not stay silent, and must not double-pay. */
        {
            int coins_mid = selftest_count_obj(player, obj_coins);
            int qp_mid = (varp_qp > 0) ? player->varps[varp_qp] : 0;

            player->varps[varp_cogquest] =
                (player->varps[varp_cogquest] & ~0xF) | 6;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kojo, -1, kojo_slot);
            cog_drain_rewards(srv);
            progress = cog_progress(player, varp_cogquest);
            SELFTEST_CHECK(progress == 8, "state 6 resume should repair to 8, got %d", progress);
            SELFTEST_CHECK(selftest_count_obj(player, obj_coins) == coins_mid,
                           "state 6 resume must not grant coins again");
            if( varp_qp > 0 )
                SELFTEST_CHECK(player->varps[varp_qp] == qp_mid,
                               "state 6 resume must not award QP again");
            if( progress == 8 )
                cog_pass("resume_state6", "OPNPC1", "progress=8 no second reward");

            player->varps[varp_cogquest] =
                (player->varps[varp_cogquest] & ~0xF) | 7;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kojo, -1, kojo_slot);
            cog_drain_rewards(srv);
            progress = cog_progress(player, varp_cogquest);
            SELFTEST_CHECK(progress == 8, "state 7 resume should repair to 8, got %d", progress);
            if( progress == 8 )
                cog_pass("resume_state7", "OPNPC1", "progress=8 no second reward");
        }

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kojo, -1, kojo_slot);
        cog_resume_pages(srv, 1, 16);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        progress = cog_progress(player, varp_cogquest);
        SELFTEST_CHECK(progress == 8, "postquest talk must leave progress=8");
        if( progress == 8 )
            cog_pass("postquest_retalk", "OPNPC1", "progress=8");
    }

cog_selftest_done:
    if( spawned >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, spawned);
        ToriRSServer_WorldNpcReap(srv);
    }
    cog_clear_inv(player);
    worn_set(player, TORIRSSERVER_WEAR_HANDS, -1, 0);
    if( varp_cogquest > 0 && varp_cogquest < TORIRSSERVER_VARP_COUNT )
        player->varps[varp_cogquest] = 0;
    if( varp_cog_bits > 0 && varp_cog_bits < TORIRSSERVER_VARP_COUNT )
        player->varps[varp_cog_bits] = 0;
    player->godmode = 0;
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    cog_pass("cleanup", "WorldNpcFree", "spawns reaped");
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
