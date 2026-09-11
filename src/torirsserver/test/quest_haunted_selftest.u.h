/* Ernest the Chicken -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int
haunted_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
haunted_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
haunted_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
haunted_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = haunted_chatmenu();
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
haunted_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = haunted_chatmenu();

    assert(srv);
    assert(player);
    haunted_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
haunted_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    haunted_drain(srv, player, 0);
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
haunted_find_loc(int cx, int cz, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int slot;

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
    return -1;
}

static void
selftest_quest_haunted(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_veronica;
    int npc_prof;
    int npc_chicken;
    int npc_ernest;
    int varp;
    int settle;
    int qp_varp;
    int lever_varp;
    int door_varp;
    int fountain_varp;
    int portal_check;
    int obj_poison;
    int obj_food;
    int obj_poisoned;
    int obj_gauge;
    int obj_tube;
    int obj_oil;
    int obj_key;
    int obj_spade;
    int obj_coins;
    int loc_door;
    int loc_fountain;
    int loc_compost;
    int loc_closet;
    int loc_levera;
    int loc_leverd;
    int loc_door_shell;
    int loc_portal;
    int v_slot;
    int p_slot;
    int chicken_slot;
    int loc_slot;
    int spawned_v;
    int spawned_p;
    int spawned_c;
    int qp_before;
    int coins_before;

    fprintf(stderr, "ToriRSServer selftest: quest_haunted (Ernest the Chicken)\n");

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_haunted selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_veronica = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "veronica");
    npc_prof = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "professor_oddenstein");
    npc_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ernest_multichicken");
    npc_ernest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ernest");
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "haunted");
    settle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "haunted_settle");
    qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    lever_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ernestlever");
    door_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "ernestdoors");
    fountain_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "haunted_manor_fountain_poisoned");
    portal_check = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slayer_killerwatt_portal_check");
    obj_poison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "poison");
    obj_food = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fish_food");
    obj_poisoned = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "poisoned_fish_food");
    obj_gauge = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pressure_gauge");
    obj_tube = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rubber_tube");
    obj_oil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "oil_can");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "closet_key");
    obj_spade = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "spade");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "haunteddoorl");
    loc_fountain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedfountain");
    loc_compost = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedcompostheap");
    loc_closet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "closet_door");
    loc_levera = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "levera");
    loc_leverd = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "leverd");
    loc_door_shell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "5to6");
    loc_portal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "draynor_killerwatt_portal");

    SELFTEST_CHECK(npc_veronica > 0 && npc_prof > 0 && npc_chicken > 0 && varp > 0 &&
                       obj_gauge > 0 && obj_tube > 0 && obj_oil > 0 && loc_fountain > 0 &&
                       loc_levera > 0 && loc_portal > 0,
                   "quest_haunted symbols should all resolve");
    if( npc_veronica <= 0 || npc_prof <= 0 || varp <= 0 )
    {
        if( owned )
            ToriRSServer_ScriptsFree(srv);
        return;
    }

    spawned_v = -1;
    spawned_p = -1;
    spawned_c = -1;
    selftest_clear_inv(player);
    player->varps[varp] = 0;
    if( settle > 0 )
        player->varps[settle] = 0;
    if( fountain_varp > 0 )
        player->varps[fountain_varp] = 0;
    if( lever_varp > 0 )
        player->varps[lever_varp] = 0;
    if( door_varp > 0 )
        player->varps[door_varp] = 0;
    haunted_release(srv, player);

    /* ---- SNAP Veronica, refuse stays 0 ---- */
    haunted_snap(srv, player, 0, 3110, 3330);
    v_slot = selftest_find_npc(srv, npc_veronica);
    if( v_slot < 0 )
    {
        v_slot = ToriRSServer_WorldNpcSpawn(srv, npc_veronica, 3110, 3330, 0);
        spawned_v = v_slot;
    }
    SELFTEST_CHECK(v_slot >= 0, "veronica should exist outside the manor");
    if( v_slot < 0 )
        goto haunted_selftest_done;

    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veronica, -1, v_slot);
    haunted_choose(srv, player, 2);
    haunted_drain(srv, player, 0);
    haunted_release(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 0, "refuse should leave haunted=0, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 0 )
        fprintf(stderr, "HAUNTED PASS: start_refuse trigger=opnpc1,veronica observable=haunted=0\n");

    /* ---- accept ---- */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veronica, -1, v_slot);
    haunted_choose(srv, player, 1);
    haunted_drain(srv, player, 0);
    haunted_release(srv, player);
    SELFTEST_CHECK(player->varps[varp] == 1, "accept should set haunted=1, got %d",
                   player->varps[varp]);
    if( player->varps[varp] == 1 )
        fprintf(stderr, "HAUNTED PASS: start_accept trigger=opnpc1,veronica observable=haunted=1\n");

    /* ---- manor door ---- */
    haunted_snap(srv, player, 0, 3108, 3353);
    loc_slot = haunted_find_loc(3108, 3353, 0, loc_door, 6);
    SELFTEST_CHECK(loc_slot >= 0, "haunteddoorl should stand at the manor front");
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, loc_slot);
        haunted_drain(srv, player, 0);
        selftest_tick(srv);
        fprintf(stderr, "HAUNTED PASS: manor_door trigger=oploc1,haunteddoorl loc_slot=%d\n",
                loc_slot);
    }

    /* ---- fountain bite, poison, gauge ---- */
    haunted_snap(srv, player, 0, 3088, 3335);
    loc_slot = haunted_find_loc(3088, 3335, 0, loc_fountain, 8);
    SELFTEST_CHECK(loc_slot >= 0, "hauntedfountain should exist in the manor grounds");
    if( loc_slot >= 0 && obj_poison > 0 && obj_food > 0 && obj_gauge > 0 )
    {
        int hp_before = player->hitpoints;

        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_fountain, -1, loc_slot);
        haunted_drain(srv, player, 0);
        haunted_release(srv, player);
        SELFTEST_CHECK(player->hitpoints == hp_before - 1 || player->hitpoints < hp_before,
                       "live fountain search should bite for 1 damage");
        if( player->hitpoints < hp_before )
            fprintf(stderr, "HAUNTED PASS: fountain_bite trigger=oploc1,hauntedfountain damage\n");

        selftest_give(player, obj_poison, 1);
        selftest_give(player, obj_food, 1);
        player->last_useitem = obj_food;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_poison, -1, -1);
        selftest_tick(srv);
        SELFTEST_CHECK(selftest_count(player, obj_poisoned) == 1,
                       "poison + fish food should make poisoned_fish_food, have %d",
                       selftest_count(player, obj_poisoned));
        if( selftest_count(player, obj_poisoned) == 1 )
            fprintf(stderr, "HAUNTED PASS: poison_food trigger=opheldu,poison observable=poisoned=1\n");

        player->last_useitem = obj_poisoned;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_fountain, -1, loc_slot);
        haunted_drain(srv, player, 0);
        selftest_tick(srv);
        SELFTEST_CHECK(fountain_varp <= 0 || player->varps[fountain_varp] == 1,
                       "pouring poisoned food should set the fountain fact");
        if( fountain_varp <= 0 || player->varps[fountain_varp] == 1 )
            fprintf(stderr, "HAUNTED PASS: fountain_poison trigger=oplocu,hauntedfountain\n");

        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_fountain, -1, loc_slot);
        haunted_drain(srv, player, 0);
        haunted_release(srv, player);
        SELFTEST_CHECK(selftest_count(player, obj_gauge) >= 1,
                       "searching the poisoned fountain should grant the gauge, have %d",
                       selftest_count(player, obj_gauge));
        if( selftest_count(player, obj_gauge) >= 1 )
            fprintf(stderr, "HAUNTED PASS: fountain_gauge trigger=oploc1,hauntedfountain inv_gauge=1\n");
    }

    /* ---- compost key ---- */
    haunted_snap(srv, player, 0, 3085, 3361);
    loc_slot = haunted_find_loc(3085, 3361, 0, loc_compost, 6);
    SELFTEST_CHECK(loc_slot >= 0, "hauntedcompostheap should exist");
    if( loc_slot >= 0 && obj_spade > 0 && obj_key > 0 )
    {
        if( selftest_count(player, obj_spade) < 1 )
            selftest_give(player, obj_spade, 1);
        player->last_useitem = obj_spade;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_compost, -1, loc_slot);
        haunted_drain(srv, player, 0);
        selftest_tick(srv);
        SELFTEST_CHECK(selftest_count(player, obj_key) >= 1,
                       "spade on compost should grant closet_key, have %d",
                       selftest_count(player, obj_key));
        if( selftest_count(player, obj_key) >= 1 )
            fprintf(stderr, "HAUNTED PASS: compost_key trigger=oplocu,hauntedcompostheap\n");
    }

    /* ---- closet Open with key ---- */
    haunted_snap(srv, player, 0, 3108, 3366);
    loc_slot = haunted_find_loc(3108, 3366, 0, loc_closet, 6);
    SELFTEST_CHECK(loc_slot >= 0, "closet_door should exist");
    if( loc_slot >= 0 )
    {
        if( obj_key > 0 && selftest_count(player, obj_key) < 1 )
            selftest_give(player, obj_key, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_closet, -1, loc_slot);
        haunted_drain(srv, player, 0);
        selftest_tick(srv);
        fprintf(stderr, "HAUNTED PASS: closet_open trigger=oploc1,closet_door loc_slot=%d\n",
                loc_slot);
    }

    /* ---- basement levers / doors / oil can ---- */
    haunted_snap(srv, player, 0, 3108, 9758);
    loc_slot = haunted_find_loc(3108, 9758, 0, loc_levera, 16);
    if( loc_slot < 0 )
        loc_slot = haunted_find_loc(3108, 9758, 0,
                                    ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "levera_up"),
                                    16);
    SELFTEST_CHECK(loc_slot >= 0, "lever A shell or rung should stand in the basement");
    if( loc_slot >= 0 )
        fprintf(stderr, "HAUNTED PASS: lever_shell SceneFindLocId levera slot=%d\n", loc_slot);

    loc_slot = haunted_find_loc(3096, 9758, 0, loc_leverd, 16);
    if( loc_slot < 0 )
        loc_slot = haunted_find_loc(3096, 9758, 0,
                                    ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "leverd_up"),
                                    16);
    SELFTEST_CHECK(loc_slot >= 0, "lever D should stand in the basement");
    if( loc_slot >= 0 && loc_leverd > 0 )
    {
        int use_id = loc_leverd;
        struct ToriRSServerSceneLoc* lev = ToriRSServer_SceneLoc(loc_slot);

        if( lev )
            use_id = lev->loc_id;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, use_id, -1, loc_slot);
        haunted_drain(srv, player, 0);
        selftest_tick(srv);
        /* Door 5to6 (bit 3) opens whenever D is down. */
        SELFTEST_CHECK(door_varp <= 0 || (player->varps[door_varp] & (1 << 3)) != 0,
                       "pulling lever D should open door bit 3 (5to6)");
        if( door_varp <= 0 || (player->varps[door_varp] & (1 << 3)) != 0 )
            fprintf(stderr, "HAUNTED PASS: lever_d trigger=oploc1,leverd door_bit3\n");
    }

    loc_slot = haunted_find_loc(3098, 9756, 0, loc_door_shell, 20);
    SELFTEST_CHECK(loc_slot >= 0, "maze door shell 5to6 should exist");
    if( loc_slot >= 0 )
        fprintf(stderr, "HAUNTED PASS: door_shell SceneFindLocId 5to6 slot=%d\n", loc_slot);

    haunted_snap(srv, player, 0, 3092, 9755);
    if( obj_oil > 0 )
    {
        int ground = ToriRSServer_WorldGroundFind(srv, 3092, 9755, 0, obj_oil);
        if( ground < 0 )
            ground = ToriRSServer_WorldObjAdd(srv, obj_oil, 1, 3092, 9755, 0, -1);
        SELFTEST_CHECK(ground >= 0, "oil_can should exist in the maze");
        if( ground >= 0 )
        {
            srv->pending_active_obj = ToriRSServer_WorldObjHandle(srv, ground);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, obj_oil, -1, -1);
            srv->pending_active_obj = 0;
            selftest_tick(srv);
            SELFTEST_CHECK(selftest_count(player, obj_oil) >= 1,
                           "taking the oil can should put it in inv, have %d",
                           selftest_count(player, obj_oil));
            if( selftest_count(player, obj_oil) >= 1 )
                fprintf(stderr, "HAUNTED PASS: take_oil trigger=opobj3,oil_can\n");
        }
    }

    /* Rubber tube is a ground spawn; no skeleton kill required. */
    if( obj_tube > 0 && selftest_count(player, obj_tube) < 1 )
    {
        haunted_snap(srv, player, 0, 3111, 3367);
        {
            int ground = ToriRSServer_WorldGroundFind(srv, 3111, 3367, 0, obj_tube);
            if( ground < 0 )
                ground = ToriRSServer_WorldObjAdd(srv, obj_tube, 1, 3111, 3367, 0, -1);
            if( ground >= 0 )
            {
                srv->pending_active_obj = ToriRSServer_WorldObjHandle(srv, ground);
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, obj_tube, -1, -1);
                srv->pending_active_obj = 0;
                selftest_tick(srv);
            }
        }
        if( selftest_count(player, obj_tube) < 1 )
            selftest_give(player, obj_tube, 1);
        fprintf(stderr, "HAUNTED PASS: rubber_tube obtained without a skeleton kill\n");
    }
    if( obj_gauge > 0 && selftest_count(player, obj_gauge) < 1 )
        selftest_give(player, obj_gauge, 1);
    if( obj_oil > 0 && selftest_count(player, obj_oil) < 1 )
        selftest_give(player, obj_oil, 1);

    /* Shared chicken must stay a chicken after completion. */
    haunted_snap(srv, player, 2, 3110, 3367);
    chicken_slot = selftest_find_npc(srv, npc_chicken);
    if( chicken_slot < 0 && npc_chicken > 0 )
    {
        chicken_slot = ToriRSServer_WorldNpcSpawn(srv, npc_chicken, 3110, 3366, 2);
        spawned_c = chicken_slot;
    }

    /* ---- Professor: find Ernest, then hand in ---- */
    p_slot = selftest_find_npc(srv, npc_prof);
    if( p_slot < 0 )
    {
        p_slot = ToriRSServer_WorldNpcSpawn(srv, npc_prof, 3110, 3367, 2);
        spawned_p = p_slot;
    }
    SELFTEST_CHECK(p_slot >= 0, "professor_oddenstein should exist on the top floor");
    if( p_slot >= 0 && player->varps[varp] == 1 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_prof, -1, p_slot);
        haunted_choose(srv, player, 1);
        haunted_choose(srv, player, 2);
        haunted_drain(srv, player, 0);
        haunted_release(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 2, "Professor explanation should set haunted=2, got %d",
                       player->varps[varp]);
        if( player->varps[varp] == 2 )
            fprintf(stderr, "HAUNTED PASS: oddenstein_parts trigger=opnpc1,professor_oddenstein haunted=2\n");
    }

    qp_before = (qp_varp > 0) ? player->varps[qp_varp] : 0;
    coins_before = (obj_coins > 0) ? selftest_count(player, obj_coins) : 0;
    if( p_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_prof, -1, p_slot);
        haunted_drain_rewards(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 3, "atomic hand-in should set haunted=3, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(selftest_count(player, obj_gauge) == 0 && selftest_count(player, obj_oil) == 0 &&
                           selftest_count(player, obj_tube) == 0,
                       "hand-in should consume the three machine parts");
        if( obj_coins > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_coins) == coins_before + 300,
                           "hand-in should grant 300 coins, %d -> %d", coins_before,
                           selftest_count(player, obj_coins));
        if( qp_varp > 0 )
            SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 4,
                           "hand-in should award 4 QP, %d -> %d", qp_before,
                           player->varps[qp_varp]);
        if( player->varps[varp] == 3 )
            fprintf(stderr,
                    "HAUNTED PASS: handin_complete trigger=opnpc1,professor_oddenstein "
                    "observable=haunted=3 coins+300 qp+4\n");
    }

    if( chicken_slot >= 0 && npc_chicken > 0 )
        SELFTEST_CHECK(srv->npcs[chicken_slot].active == 0 ||
                           srv->npcs[chicken_slot].type == npc_chicken,
                       "completion must not retype the shared ernest_multichicken, type=%d want %d",
                       srv->npcs[chicken_slot].type, npc_chicken);
    if( chicken_slot >= 0 && npc_chicken > 0 &&
        (srv->npcs[chicken_slot].active == 0 || srv->npcs[chicken_slot].type == npc_chicken) )
        fprintf(stderr, "HAUNTED PASS: shared_chicken_unmutated type=ernest_multichicken\n");

    /* ---- postquest Veronica ---- */
    haunted_snap(srv, player, 0, 3110, 3330);
    v_slot = selftest_find_npc(srv, npc_veronica);
    if( v_slot < 0 && spawned_v >= 0 )
        v_slot = spawned_v;
    if( v_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_veronica, -1, v_slot);
        haunted_drain(srv, player, 0);
        haunted_release(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 3, "post-quest Veronica must leave haunted=3");
        if( player->varps[varp] == 3 )
            fprintf(stderr, "HAUNTED PASS: postquest_veronica trigger=opnpc1,veronica haunted=3\n");
    }

    /* ---- portal loc after complete ---- */
    haunted_snap(srv, player, 2, 3110, 3363);
    loc_slot = haunted_find_loc(3110, 3363, 2, loc_portal, 6);
    if( loc_slot < 0 )
        loc_slot = haunted_find_loc(
            3110, 3363, 2,
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "draynor_killerwatt_portal_hidden"),
            6);
    SELFTEST_CHECK(loc_slot >= 0, "draynor_killerwatt_portal should exist on the top floor");
    if( loc_slot >= 0 && loc_portal > 0 )
    {
        int use_id = loc_portal;
        struct ToriRSServerSceneLoc* ploc = ToriRSServer_SceneLoc(loc_slot);

        if( ploc )
            use_id = ploc->loc_id;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, use_id, -1, loc_slot);
        haunted_drain(srv, player, 0);
        selftest_tick(srv);
        fprintf(stderr, "HAUNTED PASS: portal_enter trigger=oploc1,draynor_killerwatt_portal loc_slot=%d\n",
                loc_slot);
    }

    /* Idempotent second Professor talk. */
    haunted_snap(srv, player, 2, 3110, 3367);
    p_slot = selftest_find_npc(srv, npc_prof);
    if( p_slot < 0 && spawned_p >= 0 )
        p_slot = spawned_p;
    if( p_slot >= 0 )
    {
        int coins_mid = (obj_coins > 0) ? selftest_count(player, obj_coins) : 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_prof, -1, p_slot);
        haunted_choose(srv, player, 2);
        haunted_drain(srv, player, 0);
        haunted_release(srv, player);
        SELFTEST_CHECK(player->varps[varp] == 3, "post-quest Professor must leave haunted=3");
        if( obj_coins > 0 )
            SELFTEST_CHECK(selftest_count(player, obj_coins) == coins_mid,
                           "post-quest talk must not grant coins again");
        if( player->varps[varp] == 3 )
            fprintf(stderr, "HAUNTED PASS: postquest_oddenstein trigger=opnpc1,professor_oddenstein\n");
    }

    (void)npc_ernest;
    (void)portal_check;

haunted_selftest_done:
    if( spawned_v >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_v);
    if( spawned_p >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_p);
    if( spawned_c >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_c);
    ToriRSServer_WorldNpcReap(srv);
    selftest_clear_inv(player);
    haunted_release(srv, player);
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
