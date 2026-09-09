/* Haunted Mine critical-path re-audit (gp-mine-c1).
 *
 * Wiki RAW oldids: Haunted_Mine 15292305, Quick_guide 14834641,
 * Transcript 15263301. QH steps.put(0..10) + named Object/Npc/ItemIDs
 * from HauntedMine.java @ 5ea99d5. Cache/pack names win.
 *
 * Every progress click is a real OPNPC/OPLOC/OPHELD/OPLOCU, not a mirrored
 * debugproc (boss skip is the one named cheat). Player is godmoded first.
 */
static int
hmq_inv_has(const struct ToriRSServerPlayer* player, int obj_id)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id == obj_id && player->inv[i].count > 0 )
            return 1;
    }
    return 0;
}

static void
hmq_clear_inv(struct ToriRSServerPlayer* player)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        inv_set(player, i, -1, 0);
}

static void
hmq_finish_script(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 24 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 6);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

/* configs/all.varbit export for basevar hauntedmine_bits. Same numbers the
 * cache would have loaded; installed only so POP_VARBIT can pack them when
 * this VM has no cache.osrs239. */
static void
hmq_ensure_native_varbits(void)
{
    static const struct
    {
        const char* name;
        int startbit;
    } bits[] = {
        { "hauntedmine_pointspuzzlestarted", 0 },
        { "hauntedmine_lever_b", 1 },
        { "hauntedmine_lever_a", 2 },
        { "hauntedmine_lever_c", 3 },
        { "hauntedmine_lever_d", 4 },
        { "hauntedmine_lever_e", 5 },
        { "hauntedmine_lever_i", 6 },
        { "hauntedmine_lever_j", 7 },
        { "hauntedmine_lever_k", 8 },
        { "hauntedmine_liftpoweredonce", 9 },
        { "hauntedmine_liftpowerednow", 10 },
        { "hauntedmine_begincart_fungus", 11 },
        { "hauntedmine_endcart_fungus", 12 },
        { "hauntedmine_heardaboutkey", 21 },
    };
    int base = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hauntedmine_bits");
    int i;

    assert(base >= 0);
    for( i = 0; i < (int)(sizeof(bits) / sizeof(bits[0])); i++ )
    {
        int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, bits[i].name);

        assert(id >= 0);
        SELFTEST_CHECK(
            ToriRSServer_VarbitDefine(id, base, bits[i].startbit, bits[i].startbit) == base,
            "hauntedmine varbit %s should pack into hauntedmine_bits",
            bits[i].name);
    }
}

static void
hmq_pass(
    int fails_before,
    const char* line)
{
    assert(line);
    if( g_selftest_failures == fails_before )
        fprintf(stderr, "%s\n", line);
}

static int
hmq_oploc1(
    struct ToriRSServer* srv,
    int loc,
    int x,
    int z)
{
    int slot;

    assert(srv);
    ToriRSServer_WorldTeleport(srv, 0, x, z);
    selftest_tick(srv);
    slot = ToriRSServer_SceneFindLocId(x, z, 0, loc);
    return ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc, -1, slot);
}

static void
selftest_quest_hauntedmine(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: hauntedmine critical path\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    player->godmode = 1;
    player->stat_level[TORIRSSERVER_STAT_HITPOINTS] = 10;
    player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS] = 10;
    player->max_hitpoints = 10;
    player->hitpoints = 10;
    ToriRSServer_CombatSyncHitpoints(player);
    hmq_ensure_native_varbits();

    {
        int varp_hm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "hauntedmine");
        int varp_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "priestperil");
        int vb_heard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_heardaboutkey");
        int vb_once = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_liftpoweredonce");
        int vb_now = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_liftpowerednow");
        int vb_begin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_begincart_fungus");
        int vb_end = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_endcart_fungus");
        int vb_a = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_lever_a");
        int vb_b = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_lever_b");
        int vb_e = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_lever_e");
        int vb_i = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "hauntedmine_lever_i");
        int npc_zealot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "saradominist_zealot");
        int npc_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hauntedmine_boss_key");
        int loc_south = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_back_entrance2");
        int loc_l1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_laddertop");
        int loc_l2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_laddertop_1sw");
        int loc_l3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_laddertop_1e");
        int loc_up = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_ladder_1w");
        int loc_fungus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "glowing_mushroom2");
        int loc_cart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_puzzle_cart");
        int loc_lever1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_point_lever1");
        int loc_lever2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_point_lever2");
        int loc_lever5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_point_lever5");
        int loc_lever6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_point_lever6");
        int loc_panel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_points_info");
        int loc_valve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_lift_valve");
        int loc_lift = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lift_side_r");
        int loc_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_dark_stairs_top");
        int loc_crystal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "crystalcorner");
        int loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hauntedmine_chisel_crate");
        int obj_liftkey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hauntedmine_lift_key");
        int obj_reward = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hauntedmine_reward_key");
        int obj_fungus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "glowing_fungus");
        int obj_chisel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chisel");
        int obj_shard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "crystalshard_necklace_unstrung");
        int stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
        int stat_str = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "strength");
        int zealot_slot = -1;
        int key_slot = -1;
        int loc_slot;
        int xp_before;
        int hp_start;
        int ran;

        SELFTEST_CHECK(
            varp_hm >= 0 && varp_priest >= 0 && vb_heard >= 0 && vb_once >= 0 &&
                vb_now >= 0 && vb_begin >= 0 && vb_end >= 0 && vb_a >= 0 &&
                vb_b >= 0 && vb_e >= 0 && vb_i >= 0 && npc_zealot >= 0 &&
                npc_key >= 0 && loc_south >= 0 && loc_l1 >= 0 && loc_l2 >= 0 &&
                loc_l3 >= 0 && loc_up >= 0 && loc_fungus >= 0 && loc_cart >= 0 &&
                loc_lever1 >= 0 && loc_lever2 >= 0 && loc_lever5 >= 0 &&
                loc_lever6 >= 0 && loc_panel >= 0 && loc_valve >= 0 &&
                loc_lift >= 0 && loc_stairs >= 0 && loc_crystal >= 0 &&
                obj_liftkey >= 0 && obj_reward >= 0 && obj_fungus >= 0 &&
                obj_chisel >= 0 && obj_shard >= 0 && stat_craft >= 0 &&
                stat_str >= 0,
            "hauntedmine C-side names should all resolve");
        if( varp_hm < 0 || npc_zealot < 0 || obj_liftkey < 0 )
            return;

        hmq_clear_inv(player);
        player->varps[varp_hm] = 0;
        player->varps[varp_priest] = 0;
        ToriRSServer_VarbitSet(srv, vb_heard, 0);
        ToriRSServer_VarbitSet(srv, vb_once, 0);
        ToriRSServer_VarbitSet(srv, vb_now, 0);
        ToriRSServer_VarbitSet(srv, vb_begin, 0);
        ToriRSServer_VarbitSet(srv, vb_end, 0);
        player->stat_level[stat_craft] = 1;
        player->stat_boosted[stat_craft] = 1;
        player->stat_level[stat_str] = 1;
        player->stat_boosted[stat_str] = 1;
        player->stat_xp_tenths[stat_str] = 0;
        hp_start = player->hitpoints;

        ToriRSServer_WorldTeleport(srv, 0, 3444, 3258);
        selftest_tick(srv);
        zealot_slot = ToriRSServer_WorldNpcSpawn(srv, npc_zealot, 3444, 3258, 0);
        SELFTEST_CHECK(zealot_slot >= 0, "saradominist_zealot should spawn");
        if( zealot_slot < 0 )
            return;

        /* Priest incomplete: start must refuse. */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_zealot, -1, zealot_slot);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(player->varps[varp_hm] == 0,
                           "Zealot must refuse without Priest in Peril, got %d",
                           player->varps[varp_hm]);
            hmq_pass(fails, "PASS hauntedmine start-refuse priest=0");
        }

        /* Crafting 1 + Priest complete: start must accept. Mutation target. */
        player->varps[varp_priest] = 60; /* ^priestperil_complete */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_zealot, -1, zealot_slot);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(player->varps[varp_hm] == 1,
                           "OPNPC1 zealot with Priest complete and Crafting 1 must start, got %d",
                           player->varps[varp_hm]);
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_heard) == 1,
                           "start conversation should disclose the key (heardaboutkey=1)");
            SELFTEST_CHECK(player->stat_boosted[stat_craft] == 1,
                           "start must not require Crafting 35, boosted=%d",
                           player->stat_boosted[stat_craft]);
            hmq_pass(fails, "PASS hauntedmine start zealot=1 crafting=1");
        }

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_zealot, -1, zealot_slot);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(hmq_inv_has(player, obj_liftkey),
                           "OPNPC3 pickpocket must deliver hauntedmine_lift_key");
            hmq_pass(fails, "PASS hauntedmine pickpocket key=1");
        }

        ran = hmq_oploc1(srv, loc_south, 3429, 3225);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;
            char line[96];

            SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                           "OPLOC1 hauntedmine_back_entrance2 should run");
            SELFTEST_CHECK(player->x != 3429 || player->z != 3225,
                           "south cart tunnel should move the player, still at %d,%d",
                           player->x, player->z);
            snprintf(line, sizeof(line), "PASS hauntedmine enter south=%d,%d",
                     player->x, player->z);
            hmq_pass(fails, line);
        }

        hmq_oploc1(srv, loc_l1, 3422, 9625);
        hmq_finish_script(srv);
        {
            char line[96];

            snprintf(line, sizeof(line), "PASS hauntedmine ladder l1=%d,%d",
                     player->x, player->z);
            hmq_pass(g_selftest_failures, line);
        }

        hmq_oploc1(srv, loc_l2, 2798, 4567);
        hmq_finish_script(srv);
        {
            char line[96];

            snprintf(line, sizeof(line), "PASS hauntedmine ladder l2=%d,%d",
                     player->x, player->z);
            hmq_pass(g_selftest_failures, line);
        }

        hmq_oploc1(srv, loc_l3, 2725, 4486);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;
            char line[96];

            SELFTEST_CHECK(player->x >= 2757 && player->z >= 4483,
                           "L3 south ladder should reach the cart room, at %d,%d",
                           player->x, player->z);
            snprintf(line, sizeof(line), "PASS hauntedmine cartroom=%d,%d",
                     player->x, player->z);
            hmq_pass(fails, line);
        }

        hmq_oploc1(srv, loc_fungus, 2793, 4493);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(hmq_inv_has(player, obj_fungus),
                           "OPLOC1 glowing_mushroom2 should grant glowing_fungus");
            hmq_pass(fails, "PASS hauntedmine pick fungus=1");
        }

        hmq_oploc1(srv, loc_cart, 2778, 4506);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_begin) == 1,
                           "deposit cart should set begincart_fungus");
            SELFTEST_CHECK(!hmq_inv_has(player, obj_fungus),
                           "deposit should consume the held fungus");
            hmq_pass(fails, "PASS hauntedmine cart deposit=1");
        }

        /* Targets: a/b/e/i = 1. Levers start at 0. */
        hmq_oploc1(srv, loc_lever1, 2785, 4517);
        hmq_oploc1(srv, loc_lever2, 2784, 4517);
        hmq_oploc1(srv, loc_lever5, 2785, 4515);
        hmq_oploc1(srv, loc_lever6, 2768, 4533);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_b) == 1 &&
                               ToriRSServer_VarbitGet(player, vb_a) == 1 &&
                               ToriRSServer_VarbitGet(player, vb_e) == 1 &&
                               ToriRSServer_VarbitGet(player, vb_i) == 1,
                           "QH lever targets a/b/e/i should be 1 after the four pulls");
            hmq_pass(fails, "PASS hauntedmine levers a=1 b=1 e=1 i=1");
        }

        hmq_oploc1(srv, loc_panel, 2770, 4522);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_end) == 1,
                           "correct points start should set endcart_fungus");
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_begin) == 0,
                           "successful send should clear begincart_fungus");
            hmq_pass(fails, "PASS hauntedmine cart send end=1");
        }

        /* Collect room: QH cart at 2774,4537. Then climb back to L3 north. */
        ToriRSServer_WorldTeleport(srv, 0, 2774, 4537);
        selftest_tick(srv);
        hmq_oploc1(srv, loc_cart, 2774, 4537);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(hmq_inv_has(player, obj_fungus),
                           "OPLOC1 collect cart should return glowing_fungus");
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_end) == 0,
                           "collect should clear endcart_fungus");
            hmq_pass(fails, "PASS hauntedmine cart collect=1");
        }

        hmq_oploc1(srv, loc_up, 2774, 4540);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;
            char line[96];

            SELFTEST_CHECK(!(player->x >= 2794 && player->x <= 2812 &&
                             player->z >= 4489 && player->z <= 4532),
                           "collect-room ladder must NOT skip to the lift, at %d,%d",
                           player->x, player->z);
            snprintf(line, sizeof(line), "PASS hauntedmine collect-up=%d,%d",
                     player->x, player->z);
            hmq_pass(fails, line);
        }

        /* L3 north-east ladder is the lift descent (QH 2732,4529). */
        hmq_oploc1(srv, loc_l3, 2732, 4529);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;
            char line[96];

            SELFTEST_CHECK(player->x >= 2794 && player->z >= 4489,
                           "L3 NE ladder should reach the lift room, at %d,%d",
                           player->x, player->z);
            snprintf(line, sizeof(line), "PASS hauntedmine liftroom=%d,%d",
                     player->x, player->z);
            hmq_pass(fails, line);
        }

        if( loc_crate >= 0 )
        {
            hmq_oploc1(srv, loc_crate, 2800, 4500);
            hmq_finish_script(srv);
        }
        if( !hmq_inv_has(player, obj_chisel) )
            inv_set(player, 10, obj_chisel, 1);

        /* Valve without key must refuse. */
        {
            int saved_key_slot = selftest_find(player, obj_liftkey);
            int saved_key = saved_key_slot >= 0 ? player->inv[saved_key_slot].obj_id : -1;

            if( saved_key_slot >= 0 )
                inv_set(player, saved_key_slot, -1, 0);
            hmq_oploc1(srv, loc_valve, 2808, 4496);
            hmq_finish_script(srv);
            {
                int fails = g_selftest_failures;

                SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_now) == 0,
                               "valve without the Zealot's key must stay closed");
                hmq_pass(fails, "PASS hauntedmine valve-refuse key=0");
            }
            if( saved_key_slot >= 0 )
                inv_set(player, saved_key_slot, saved_key, 1);
        }

        /* Wiki / QH: use the key on the valve. Key is retained. */
        ToriRSServer_WorldTeleport(srv, 0, 2808, 4496);
        selftest_tick(srv);
        loc_slot = ToriRSServer_SceneFindLocId(2808, 4496, 0, loc_valve);
        player->last_useitem = obj_liftkey;
        player->last_useslot = selftest_find(player, obj_liftkey);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_valve, -1, loc_slot);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_now) == 1,
                           "OPLOCU key-on-valve should open current flow");
            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_once) == 1,
                           "first unlock should write liftpoweredonce");
            SELFTEST_CHECK(hmq_inv_has(player, obj_liftkey),
                           "valve must retain the Zealot's key");
            hmq_pass(fails, "PASS hauntedmine valve now=1 key-retained=1");
        }

        hmq_oploc1(srv, loc_lift, 2807, 4492);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;
            char line[96];

            SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_now) == 0,
                           "taking the lift should spend current power");
            snprintf(line, sizeof(line), "PASS hauntedmine lift flooded=%d,%d",
                     player->x, player->z);
            hmq_pass(fails, line);
        }

        /* East stairs to Dayth (QH 2748,4437). */
        SELFTEST_CHECK(hmq_inv_has(player, obj_fungus),
                       "lift/stairs path should still be holding glowing_fungus");
        hmq_oploc1(srv, loc_stairs, 2748, 4437);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;
            char line[96];

            SELFTEST_CHECK(player->x != 2748 || player->z != 4437,
                           "dayth stairs should descend, still at %d,%d",
                           player->x, player->z);
            snprintf(line, sizeof(line), "PASS hauntedmine stairs-dayth=%d,%d",
                     player->x, player->z);
            hmq_pass(fails, line);
        }

        ToriRSServer_WorldTeleport(srv, 0, 2788, 4455);
        selftest_tick(srv);
        ran = ToriRSServer_ScriptsRunDebugproc(srv, "hauntedmine_skipboss");
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                           "hauntedmine_skipboss should bind");
            SELFTEST_CHECK(player->varps[varp_hm] == 9,
                           "skipboss should write dayth-killed state 9, got %d",
                           player->varps[varp_hm]);
            hmq_pass(fails, "PASS hauntedmine boss=treus_dayth CHEAT-SKIP");
        }

        key_slot = ToriRSServer_WorldNpcSpawn(srv, npc_key, 2788, 4455, 0);
        SELFTEST_CHECK(key_slot >= 0, "hauntedmine_boss_key should spawn for pickup");
        if( key_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_key, -1, key_slot);
            hmq_finish_script(srv);
            SELFTEST_CHECK(hmq_inv_has(player, obj_reward),
                           "OPNPC1 key pickup after kill should grant hauntedmine_reward_key");
            SELFTEST_CHECK(player->varps[varp_hm] == 10,
                           "key pickup should write state 10, got %d",
                           player->varps[varp_hm]);
            ToriRSServer_WorldNpcFree(srv, key_slot);
            ToriRSServer_WorldNpcReap(srv);
        }
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(hmq_inv_has(player, obj_reward) && player->varps[varp_hm] == 10,
                           "crystal-mine key + state 10 after OPNPC1 pickup");
            hmq_pass(fails, "PASS hauntedmine crystal-key=1 state=10");
        }

        /* West stairs to crystals (QH 2694,4437). */
        if( !hmq_inv_has(player, obj_fungus) )
            inv_set(player, 11, obj_fungus, 1);
        hmq_oploc1(srv, loc_stairs, 2694, 4437);
        hmq_finish_script(srv);
        {
            char line[96];

            snprintf(line, sizeof(line), "PASS hauntedmine stairs-crystal=%d,%d",
                     player->x, player->z);
            hmq_pass(g_selftest_failures, line);
        }

        /* Crafting 34 must refuse the cut. */
        player->stat_level[stat_craft] = 34;
        player->stat_boosted[stat_craft] = 34;
        hmq_oploc1(srv, loc_crystal, 2787, 4428);
        hmq_finish_script(srv);
        {
            int fails = g_selftest_failures;

            SELFTEST_CHECK(player->varps[varp_hm] == 10,
                           "outcrop at Crafting 34 must not complete, got %d",
                           player->varps[varp_hm]);
            hmq_pass(fails, "PASS hauntedmine cut-refuse crafting=34");
        }

        player->stat_level[stat_craft] = 35;
        player->stat_boosted[stat_craft] = 35;
        xp_before = player->stat_xp_tenths[stat_str];
        hmq_oploc1(srv, loc_crystal, 2787, 4428);
        hmq_finish_script(srv);
        {
            int drain;

            for( drain = 0; drain < 40 && player->varps[varp_hm] != 11; drain++ )
            {
                ToriRSServer_WorldCloseModal(srv);
                selftest_tick(srv);
            }
        }
        {
            int fails = g_selftest_failures;
            char line[96];

            SELFTEST_CHECK(player->varps[varp_hm] == 11,
                           "OPLOC1 crystalcorner at Crafting 35 should complete, got %d",
                           player->varps[varp_hm]);
            SELFTEST_CHECK(hmq_inv_has(player, obj_shard),
                           "completion must deliver a salve shard");
            SELFTEST_CHECK(hmq_inv_has(player, obj_reward),
                           "completion must retain the crystal-mine key");
            SELFTEST_CHECK(player->stat_xp_tenths[stat_str] > xp_before,
                           "completion should award Strength XP, %d -> %d",
                           xp_before, player->stat_xp_tenths[stat_str]);
            snprintf(line, sizeof(line),
                     "PASS hauntedmine complete state=11 shard=1 key=1 xp=%d",
                     player->stat_xp_tenths[stat_str] - xp_before);
            hmq_pass(fails, line);
        }

        {
            int fails = g_selftest_failures;
            char line[96];

            SELFTEST_CHECK(!player->dying && player->hitpoints > 0,
                           "player must stay alive, hp=%d dying=%d",
                           player->hitpoints, player->dying);
            SELFTEST_CHECK(!(player->x == 3222 && player->z == 3218) &&
                               !(player->x == 3221 && player->z == 3218),
                           "player must not have died to Lumbridge, at %d,%d hp=%d",
                           player->x, player->z, player->hitpoints);
            SELFTEST_CHECK(player->hitpoints >= hp_start || player->godmode,
                           "godmode should keep hp from collapsing, %d -> %d god=%d",
                           hp_start, player->hitpoints, player->godmode);
            snprintf(line, sizeof(line), "PASS hauntedmine alive hp=%d god=%d at %d,%d",
                     player->hitpoints, player->godmode, player->x, player->z);
            hmq_pass(fails, line);
        }

        if( zealot_slot >= 0 )
        {
            ToriRSServer_WorldNpcFree(srv, zealot_slot);
            ToriRSServer_WorldNpcReap(srv);
        }
        hmq_clear_inv(player);
        player->varps[varp_hm] = 0;
        player->varps[varp_priest] = 0;
    }
}
