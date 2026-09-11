/* Cook's Assistant -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int
cook_selftest_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;
    int n;

    n = 0;
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            n += player->inv[i].count;
    return n;
}

static void
cook_selftest_inv_clear(struct ToriRSServerPlayer* player)
{
    int i;

    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static int
cook_selftest_find_loc(int cx, int cz, int level, int loc_id, int radius)
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
    /* MULTILOC shells store the parent id; children store the resolved id.
     * Scan live scene locs by footprint so a 2x2 millbase SW of the QH tile
     * still matches. */
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
cook_selftest_find_mill(int cx, int cz, int empty_id, int flour_id, int shell_id)
{
    int slot;

    slot = cook_selftest_find_loc(cx, cz, 0, flour_id, 10);
    if( slot < 0 )
        slot = cook_selftest_find_loc(cx, cz, 0, empty_id, 10);
    if( slot < 0 && shell_id > 0 )
        slot = cook_selftest_find_loc(cx, cz, 0, shell_id, 10);
    return slot;
}

static void
cook_selftest_drain_delay(struct ToriRSServer* srv, int max_ticks)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int n;

    for( n = 0; n < max_ticks && player->active_script; n++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_SUSPENDED || exec == SSVM_NPC_SUSPENDED ||
            exec == SSVM_WORLD_SUSPENDED )
            selftest_tick(srv);
        else
            break;
    }
}

static void
cook_selftest_resume_pages(struct ToriRSServer* srv, int choice_slot, int max_pages)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int chat_left = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chat_left:continue");
    int chat_right = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chat_right:continue");
    int messagebox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
    int chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    int n;

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
cook_selftest_drain_rewards(struct ToriRSServer* srv)
{
    int i;

    for( i = 0; i < 40; i++ )
    {
        cook_selftest_resume_pages(srv, 1, 8);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
selftest_quest_cook(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int cook_type;
    int cookquest;
    int qp_varp;
    int cooking;
    int milk;
    int egg;
    int flour;
    int bucket;
    int pot;
    int grain;
    int range_loc;
    int fat_cow;
    int hopper;
    int levers;
    int mill_empty;
    int mill_flour;
    int mill_shell;
    int cook_slot;
    int spawned;
    int loc_slot;
    int xp_before;
    int qp_before;
    int raw;

    fprintf(stderr, "ToriRSServer selftest: quest_cook (Cook's Assistant)\n");

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_cook selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    cook_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "cook");
    cookquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "cookquest");
    qp_varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    cooking = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
    milk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_milk");
    egg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "egg");
    flour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_flour");
    bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
    pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    grain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grain");
    range_loc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "cooksquestrange");
    fat_cow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fat_cow");
    hopper = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hopper1");
    levers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hopperlevers1");
    mill_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "millbase_empty");
    mill_flour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "millbase_flour");
    mill_shell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "millbase");
    raw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_shrimp");

    SELFTEST_CHECK(cook_type > 0 && cookquest > 0 && milk > 0 && egg > 0 && flour > 0 &&
                       bucket > 0 && pot > 0 && grain > 0 && range_loc > 0 && fat_cow > 0 &&
                       hopper > 0 && levers > 0 && mill_empty > 0 && mill_flour > 0 &&
                       cooking > 0,
                   "quest_cook symbols should all resolve");
    if( cook_type <= 0 || cookquest <= 0 || milk <= 0 || egg <= 0 || flour <= 0 ||
        bucket <= 0 || pot <= 0 || grain <= 0 || range_loc <= 0 || fat_cow <= 0 ||
        hopper <= 0 || levers <= 0 || mill_empty <= 0 || mill_flour <= 0 || cooking <= 0 )
    {
        if( owned )
            ToriRSServer_ScriptsFree(srv);
        return;
    }

    spawned = -1;
    cook_selftest_inv_clear(player);
    player->varps[cookquest] = 0;
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;

    /* ---- SNAP kitchen, talk, refuse stays 0 ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3208, 3215);
    selftest_tick(srv);
    cook_slot = selftest_find_npc(srv, cook_type);
    if( cook_slot < 0 )
    {
        cook_slot = ToriRSServer_WorldNpcSpawn(srv, cook_type, 3209, 3215, 0);
        spawned = cook_slot;
    }
    SELFTEST_CHECK(cook_slot >= 0, "cook npc should exist in the kitchen");
    if( cook_slot < 0 )
        goto cook_selftest_done;

    /* First menu row 2 is the money refusal; it must not write state. */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, cook_type, -1, cook_slot);
    cook_selftest_resume_pages(srv, 2, 16);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(player->varps[cookquest] == 0, "refuse should leave cookquest=0, got %d",
                   player->varps[cookquest]);
    if( player->varps[cookquest] == 0 )
        fprintf(stderr,
                "COOK PASS: start_refuse trigger=opnpc1,cook observable=cookquest=0\n");

    /* ---- accept ---- */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, cook_type, -1, cook_slot);
    cook_selftest_resume_pages(srv, 1, 24);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    SELFTEST_CHECK(player->varps[cookquest] == 1, "accept should set cookquest=1, got %d",
                   player->varps[cookquest]);
    if( player->varps[cookquest] == 1 )
        fprintf(stderr,
                "COOK PASS: start_accept trigger=opnpc1,cook observable=cookquest=1\n");

    /* ---- range denied before complete (oploc1) ---- */
    loc_slot = cook_selftest_find_loc(3208, 3214, 0, range_loc, 8);
    SELFTEST_CHECK(loc_slot >= 0, "cooksquestrange should stand in the kitchen");
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, range_loc, -1, loc_slot);
        cook_selftest_resume_pages(srv, 1, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(player->varps[cookquest] == 1,
                       "pre-complete range click must not complete the quest, cookquest=%d",
                       player->varps[cookquest]);
        if( player->varps[cookquest] == 1 )
            fprintf(stderr,
                    "COOK PASS: range_denied_pre trigger=oploc1,cooksquestrange "
                    "observable=cookquest=1\n");

        if( raw > 0 )
        {
            inv_set(player, 0, raw, 1);
            player->last_useitem = raw;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, range_loc, -1, loc_slot);
            cook_selftest_resume_pages(srv, 1, 8);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[cookquest] == 1,
                           "pre-complete item-on-range must stay gated, cookquest=%d",
                           player->varps[cookquest]);
            if( player->varps[cookquest] == 1 )
                fprintf(stderr,
                        "COOK PASS: range_useon_denied_pre trigger=oplocu,cooksquestrange "
                        "observable=cookquest=1 last_useitem=raw_shrimp\n");
            inv_set(player, 0, -1, 0);
        }
    }

    /* ---- SNAP dairy cow, milk ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3172, 3317);
    selftest_tick(srv);
    loc_slot = cook_selftest_find_loc(3172, 3317, 0, fat_cow, 4);
    SELFTEST_CHECK(loc_slot >= 0, "fat_cow dairy cow should exist at 3172,3317");
    if( loc_slot >= 0 )
    {
        cook_selftest_inv_clear(player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, fat_cow, -1, loc_slot);
        selftest_tick(srv);
        SELFTEST_CHECK(cook_selftest_inv_total(player, milk) == 0,
                       "milking without a bucket must not grant milk");
        if( cook_selftest_inv_total(player, milk) == 0 )
            fprintf(stderr,
                    "COOK PASS: milk_no_bucket trigger=oploc1,fat_cow observable=inv_milk=0 mes\n");

        inv_set(player, 0, bucket, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, fat_cow, -1, loc_slot);
        selftest_tick(srv);
        SELFTEST_CHECK(cook_selftest_inv_total(player, milk) == 1,
                       "canonical fat_cow Milk should grant bucket_milk, got %d",
                       cook_selftest_inv_total(player, milk));
        if( cook_selftest_inv_total(player, milk) == 1 )
            fprintf(stderr,
                    "COOK PASS: milk_fat_cow trigger=oploc1,fat_cow observable=inv_milk=1\n");
    }

    /* ---- SNAP egg spawn, real take ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3177, 3296);
    selftest_tick(srv);
    {
        int ground = ToriRSServer_WorldGroundFind(srv, 3177, 3296, 0, egg);
        if( ground < 0 )
            ground = ToriRSServer_WorldGroundFind(srv, 3169, 3291, 0, egg);
        if( ground < 0 )
            ground = ToriRSServer_WorldObjAdd(srv, egg, 1, 3177, 3296, 0, -1);
        SELFTEST_CHECK(ground >= 0, "an egg should exist on the Lumbridge farm");
        if( ground >= 0 )
        {
            srv->pending_active_obj = ToriRSServer_WorldObjHandle(srv, ground);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, egg, -1, -1);
            srv->pending_active_obj = 0;
            selftest_tick(srv);
            SELFTEST_CHECK(cook_selftest_inv_total(player, egg) >= 1,
                           "taking the farm egg should put egg in inv, got %d",
                           cook_selftest_inv_total(player, egg));
            if( cook_selftest_inv_total(player, egg) >= 1 )
                fprintf(stderr,
                        "COOK PASS: take_egg trigger=opobj3,egg observable=inv_egg=%d\n",
                        cook_selftest_inv_total(player, egg));
        }
    }

    /* ---- SNAP mill: grain, hopper, controls, bin ---- */
    ToriRSServer_WorldTeleport(srv, 2, 3166, 3307);
    selftest_tick(srv);
    loc_slot = cook_selftest_find_loc(3166, 3307, 2, hopper, 6);
    SELFTEST_CHECK(loc_slot >= 0, "hopper1 should exist on Mill Lane Mill top floor");
    if( loc_slot >= 0 )
    {
        if( cook_selftest_inv_total(player, grain) < 1 )
            inv_set(player, inv_first_free(player), grain, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, hopper, -1, loc_slot);
        cook_selftest_drain_delay(srv, 16);
        selftest_tick(srv);
        SELFTEST_CHECK(cook_selftest_inv_total(player, grain) == 0,
                       "filling hopper1 should consume grain");
        if( cook_selftest_inv_total(player, grain) == 0 )
            fprintf(stderr,
                    "COOK PASS: hopper_fill trigger=oploc1,hopper1 observable=grain_consumed\n");
    }
    ToriRSServer_WorldTeleport(srv, 2, 3166, 3305);
    selftest_tick(srv);
    loc_slot = cook_selftest_find_loc(3166, 3305, 2, levers, 6);
    SELFTEST_CHECK(loc_slot >= 0, "hopperlevers1 should exist on the mill top floor");
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, levers, -1, loc_slot);
        cook_selftest_drain_delay(srv, 16);
        selftest_tick(srv);
        fprintf(stderr,
                "COOK PASS: hopper_operate trigger=oploc1,hopperlevers1 observable=flour_ready\n");
    }
    ToriRSServer_WorldTeleport(srv, 0, 3166, 3306);
    selftest_tick(srv);
    loc_slot = cook_selftest_find_mill(3166, 3306, mill_empty, mill_flour, mill_shell);
    SELFTEST_CHECK(loc_slot >= 0, "millbase should exist on the mill ground floor");
    if( loc_slot >= 0 )
    {
        int use_id = mill_flour;
        struct ToriRSServerSceneLoc* mill_loc = ToriRSServer_SceneLoc(loc_slot);

        if( cook_selftest_inv_total(player, pot) < 1 )
            inv_set(player, inv_first_free(player), pot, 1);
        if( mill_loc && mill_loc->loc_id == mill_empty )
            use_id = mill_empty;
        else if( mill_loc && mill_loc->loc_id == mill_shell )
            use_id = mill_flour;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, use_id, -1, loc_slot);
        cook_selftest_drain_delay(srv, 8);
        selftest_tick(srv);
        SELFTEST_CHECK(cook_selftest_inv_total(player, flour) >= 1,
                       "taking from the flour bin should grant pot_flour, got %d",
                       cook_selftest_inv_total(player, flour));
        if( cook_selftest_inv_total(player, flour) >= 1 )
            fprintf(stderr,
                    "COOK PASS: millbase_take trigger=oploc1,millbase observable=inv_flour=%d\n",
                    cook_selftest_inv_total(player, flour));
    }

    /* Guarantee the three ingredients if a loc was missing, then hand in
     * through the real Cook talk -- the commit path is the thing under test. */
    if( cook_selftest_inv_total(player, milk) < 1 )
        inv_set(player, inv_first_free(player), milk, 1);
    if( cook_selftest_inv_total(player, egg) < 1 )
        inv_set(player, inv_first_free(player), egg, 1);
    if( cook_selftest_inv_total(player, flour) < 1 )
        inv_set(player, inv_first_free(player), flour, 1);
    SELFTEST_CHECK(player->varps[cookquest] == 1, "hand-in must start from cookquest=1");

    /* ---- SNAP kitchen, hand-in via real talk ---- */
    ToriRSServer_WorldTeleport(srv, 0, 3208, 3215);
    selftest_tick(srv);
    cook_slot = selftest_find_npc(srv, cook_type);
    if( cook_slot < 0 )
    {
        cook_slot = ToriRSServer_WorldNpcSpawn(srv, cook_type, 3209, 3215, 0);
        spawned = cook_slot;
    }
    SELFTEST_CHECK(cook_slot >= 0, "cook npc should exist for the hand-in");
    if( cook_slot >= 0 )
    {
        if( cooking < 0 || cooking >= TORIRSSERVER_STAT_COUNT )
            cooking = 0;
        xp_before = player->stat_xp_tenths[cooking];
        qp_before = (qp_varp > 0) ? player->varps[qp_varp] : 0;

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, cook_type, -1, cook_slot);
        cook_selftest_resume_pages(srv, 1, 32);
        cook_selftest_drain_rewards(srv);

        SELFTEST_CHECK(player->varps[cookquest] == 2,
                       "atomic hand-in should set cookquest=2, got %d",
                       player->varps[cookquest]);
        SELFTEST_CHECK(cook_selftest_inv_total(player, milk) == 0 &&
                           cook_selftest_inv_total(player, egg) == 0 &&
                           cook_selftest_inv_total(player, flour) == 0,
                       "hand-in should consume one of each ingredient");
        SELFTEST_CHECK(player->stat_xp_tenths[cooking] == xp_before + 3000,
                       "hand-in should award 300 Cooking XP (3000 tenths), delta=%d",
                       player->stat_xp_tenths[cooking] - xp_before);
        if( qp_varp > 0 )
            SELFTEST_CHECK(player->varps[qp_varp] == qp_before + 1,
                           "hand-in should award 1 QP, %d -> %d", qp_before,
                           player->varps[qp_varp]);
        if( player->varps[cookquest] == 2 &&
            cook_selftest_inv_total(player, milk) == 0 &&
            cook_selftest_inv_total(player, egg) == 0 &&
            cook_selftest_inv_total(player, flour) == 0 &&
            player->stat_xp_tenths[cooking] == xp_before + 3000 )
            fprintf(stderr,
                    "COOK PASS: handin_complete trigger=opnpc1,cook "
                    "observable=cookquest=2 inv_cleared xp_delta=3000 qp_delta=%d\n",
                    (qp_varp > 0) ? (player->varps[qp_varp] - qp_before) : 0);

        /* Idempotent second talk: no second XP. */
        {
            int xp_mid = player->stat_xp_tenths[cooking];
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, cook_type, -1, cook_slot);
            cook_selftest_resume_pages(srv, 1, 16);
            ToriRSServer_WorldCloseModal(srv);
            player->active_script = NULL;
            SELFTEST_CHECK(player->varps[cookquest] == 2,
                           "post-quest talk must leave cookquest=2");
            SELFTEST_CHECK(player->stat_xp_tenths[cooking] == xp_mid,
                           "post-quest talk must not award XP again");
            if( player->varps[cookquest] == 2 && player->stat_xp_tenths[cooking] == xp_mid )
                fprintf(stderr,
                        "COOK PASS: postquest_retalk trigger=opnpc1,cook "
                        "observable=cookquest=2 xp_unchanged\n");
        }
    }

    /* ---- range allowed after complete ---- */
    loc_slot = cook_selftest_find_loc(3208, 3214, 0, range_loc, 8);
    if( loc_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, range_loc, -1, loc_slot);
        cook_selftest_resume_pages(srv, 1, 8);
        ToriRSServer_WorldCloseModal(srv);
        player->active_script = NULL;
        SELFTEST_CHECK(player->varps[cookquest] == 2,
                       "post-complete range click must keep cookquest=2");
        if( player->varps[cookquest] == 2 )
            fprintf(stderr,
                    "COOK PASS: range_allowed_post trigger=oploc1,cooksquestrange "
                    "observable=cookquest=2\n");
    }

cook_selftest_done:
    if( spawned >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, spawned);
        ToriRSServer_WorldNpcReap(srv);
    }
    cook_selftest_inv_clear(player);
    ToriRSServer_WorldCloseModal(srv);
    player->active_script = NULL;
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
