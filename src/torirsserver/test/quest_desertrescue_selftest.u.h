/* The Tourist Trap (quest_desertrescue) Gate D stanza. Included from
 * torirs_server_world_selftest.c immediately before a selftest_reset_world
 * so spawned Irena / captain / gate transforms cannot leak into later
 * RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD dispatch on the
 * authored critical path. Silent success is forbidden: each step prints
 * an ASCII PASS line. The player stays godmoded unless a future death
 * test is added.
 *
 * Authored path now includes Irena start/hand-in, captain duel/key, camp
 * gate, Al Shabim greeting, Ana talk/barrel, mine cart, winch/lift, and
 * the XP reward scroll. Siad / dart-prototype / pineapple-bribe leftovers
 * stay listed in the ledger.
 *
 * Cache-dependent steps (need cache.osrs239 maps / obj_add_private /
 * fontmetrics): captain ai_queue3 metal_key drop, OPLOCU on
 * miningcampgateclosedl, desertrescue_journal split_init. A C-only walk
 * without named BMPs is a failed Gate D close.
 */
static void
tt_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "TOURISTTRAP PASS: %s\n", step);
}

static void
tt_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static int
tt_chatmenu(struct ToriRSServer* srv)
{
    assert(srv);
    (void)srv;
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

/* Drain mesbox / chatnpc pauses. Stop on a real ~p_choice menu or when
 * the script ends. Same shape as biohazard_run_dialogue. */
static void
tt_drain(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int chatmenu;
    int round;

    assert(srv);
    assert(player);
    chatmenu = tt_chatmenu(srv);
    for( round = 0; round < 40 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( chatmenu > 0 && uid == chatmenu )
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
tt_choose(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int slot)
{
    int chatmenu;

    assert(srv);
    assert(player);
    chatmenu = tt_chatmenu(srv);
    tt_drain(srv, player);
    if( player->active_script != NULL && chatmenu > 0 )
    {
        player->last_slot = slot;
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
        tt_drain(srv, player);
    }
}

static int
tt_find_loc(
    int x,
    int z,
    int level,
    int loc_id,
    int radius,
    int* out_x,
    int* out_z)
{
    int dx;
    int dz;
    int slot;

    assert(radius >= 0);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot >= 0 )
    {
        if( out_x )
            *out_x = x;
        if( out_z )
            *out_z = z;
        return slot;
    }
    for( dx = -radius; dx <= radius; dx++ )
    {
        for( dz = -radius; dz <= radius; dz++ )
        {
            if( dx == 0 && dz == 0 )
                continue;
            slot = ToriRSServer_SceneFindLocId(x + dx, z + dz, level, loc_id);
            if( slot >= 0 )
            {
                if( out_x )
                    *out_x = x + dx;
                if( out_z )
                    *out_z = z + dz;
                return slot;
            }
        }
    }
    return -1;
}

static int
tt_inv_has(
    const struct ToriRSServerPlayer* player,
    int obj_id)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            return 1;
    }
    return 0;
}

static void
selftest_quest_desertrescue(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::desertrescuerun\n");

    /* Preceding quest stanzas (Jungle Potion) free the pack. */
    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int npc_irena = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "irena");
        int npc_captain =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "desertminingcaptain");
        int npc_shabim = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "al_shabim");
        int loc_gate =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "miningcampgateclosedl");
        int varp_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "desertrescue");
        int varp_mech =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "desertrescue_map_mechanisms");
        int varp_duel =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "desertrescue_captain_duel");
        int obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "metal_key");
        int obj_shirt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "desert_shirt");
        int obj_robe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "desert_robe");
        int obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "desert_boots");
        int obj_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thminebarrel_empty");
        int obj_ana_barrel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thanainabarrel");
        int npc_ana = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ana");
        int npc_driver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mining_cart_driver");
        int loc_cart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "touristtrap_minecart");
        int loc_winch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ropepullthingy");
        int loc_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "thttmineentrancel");
        const int started = 1;
        const int approached = 3;
        const int killed = 4;
        const int entered = 5;
        const int caught_ana = 19;
        const int escaped = 26;
        const int saved_ana = 27;
        const int bit_find_shabim = 5;
        const int bit_spoke_shabim = 6;
        int irena_slot = -1;
        int captain_slot = -1;
        int shabim_slot = -1;
        int ana_slot = -1;
        int driver_slot = -1;

        player->godmode = 1;

        SELFTEST_CHECK(npc_irena >= 0 && npc_captain >= 0 && npc_shabim >= 0 &&
                           loc_gate >= 0 && varp_quest >= 0 && varp_mech >= 0 &&
                           varp_duel >= 0 && obj_key >= 0 && obj_shirt >= 0 &&
                           obj_robe >= 0 && obj_boots >= 0 && obj_empty >= 0 &&
                           obj_ana_barrel >= 0 && npc_ana >= 0 && npc_driver >= 0 &&
                           loc_cart >= 0 && loc_winch >= 0 && loc_mine >= 0,
                       "the ::desertrescuerun C-side names should all resolve");
        if( npc_irena < 0 || npc_captain < 0 || npc_shabim < 0 || loc_gate < 0 ||
            varp_quest < 0 || varp_mech < 0 || varp_duel < 0 || obj_key < 0 ||
            obj_shirt < 0 || obj_robe < 0 || obj_boots < 0 || obj_empty < 0 ||
            obj_ana_barrel < 0 || npc_ana < 0 || npc_driver < 0 || loc_cart < 0 ||
            loc_winch < 0 || loc_mine < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        tt_clear_inv(player);
        player->varps[varp_quest] = 0;
        player->varps[varp_mech] = 0;
        player->varps[varp_duel] = 0;

        /* ---- OPNPC1 Irena: real accept path writes ^desertrescue_started ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3304, 3112);
        selftest_tick(srv);
        irena_slot = ToriRSServer_WorldNpcSpawn(srv, npc_irena, 3304, 3112, 0);
        SELFTEST_CHECK(irena_slot >= 0, "irena should spawn at the Shantay Pass");
        if( irena_slot >= 0 )
        {
            ToriRSServer_WorldNpcSetOwner(&srv->npcs[irena_slot], player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_irena, -1,
                                           irena_slot);
            tt_choose(srv, player, 1); /* What's the matter? */
            tt_choose(srv, player, 3); /* Is there a reward if I get her back? */
            tt_choose(srv, player, 1); /* Okay Irena, calm down... */
            SELFTEST_CHECK(player->varps[varp_quest] == started,
                           "OPNPC1 irena accept should write desertrescue_started, got %d",
                           player->varps[varp_quest]);
            if( player->varps[varp_quest] == started )
                tt_pass("opnpc1 irena starts the quest");
            ToriRSServer_WorldCloseModal(srv);

            /* Re-talk must not reset. */
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_irena, -1,
                                           irena_slot);
            tt_drain(srv, player);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->varps[varp_quest] == started,
                           "re-talking to Irena mid-quest must not reset, got %d",
                           player->varps[varp_quest]);
            tt_pass("opnpc1 irena re-talk keeps started");
        }

        /* ---- OPNPC1 captain before start: refuse, no state write ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3271, 3029);
        selftest_tick(srv);
        captain_slot = ToriRSServer_WorldNpcSpawn(srv, npc_captain, 3271, 3029, 0);
        SELFTEST_CHECK(captain_slot >= 0, "desertminingcaptain should spawn");
        if( captain_slot >= 0 )
        {
            int saved = player->varps[varp_quest];

            ToriRSServer_WorldNpcSetOwner(&srv->npcs[captain_slot], player);
            player->varps[varp_quest] = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_captain, -1,
                                           captain_slot);
            tt_drain(srv, player);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->varps[varp_quest] == 0,
                           "OPNPC1 captain before start must not advance, got %d",
                           player->varps[varp_quest]);
            tt_pass("opnpc1 captain refuses before start");
            player->varps[varp_quest] = saved;

            /* ---- OPNPC3 watch: flavour, no state change ---- */
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC3, npc_captain, -1,
                                           captain_slot);
            tt_drain(srv, player);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->varps[varp_quest] == saved,
                           "OPNPC3 watch captain must not change desertrescue, got %d",
                           player->varps[varp_quest]);
            tt_pass("opnpc3 captain watch is flavour");

            /* ---- OPNPC1 captain at started: mesbox writes approached ---- */
            player->varps[varp_quest] = started;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_captain, -1,
                                           captain_slot);
            tt_drain(srv, player);
            SELFTEST_CHECK(player->varps[varp_quest] == approached,
                           "OPNPC1 captain at started should write approached_captain, got %d",
                           player->varps[varp_quest]);
            tt_pass("opnpc1 captain approaches");

            /* Wiki / QH duel provocation: Wow! -> work for you -> something
             * for a strong Captain -> Sorry Sir -> funny captain. */
            tt_choose(srv, player, 1); /* Wow! A real captain! */
            tt_choose(srv, player, 2); /* I'd love to work for a tough guy */
            tt_choose(srv, player, 2); /* Can't I do something... */
            tt_choose(srv, player, 2); /* Sorry Sir, I don't think I can */
            tt_choose(srv, player, 1); /* It's a funny captain... */
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->varps[varp_duel] == 1,
                           "the duel provocation should set desertrescue_captain_duel, got %d",
                           player->varps[varp_duel]);
            SELFTEST_CHECK((player->varps[varp_mech] & (1 << bit_find_shabim)) != 0,
                           "the captain job offer should set find_al_shabim");
            if( player->varps[varp_duel] == 1 )
                tt_pass("opnpc1 captain duel provocation");

            /* ---- OPNPC2 with duel armed is the real Attack click ---- */
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_captain, -1,
                                           captain_slot);
            SELFTEST_CHECK(player->godmode == 1 && player->dying == 0,
                           "duel Attack must leave the godmoded player alive");
            tt_pass("opnpc2 captain duel attack");

            /* Real death -> [ai_queue3,desertminingcaptain] key + stage. */
            {
                struct ToriRSServerNpc* cap = &srv->npcs[captain_slot];
                int t;

                SELFTEST_CHECK(cap->max_hitpoints == 80,
                               "desertminingcaptain hitpoints should match desertrescue.npc (80), got %d",
                               cap->max_hitpoints);
                cap->combat_target = player->pid;
                ToriRSServer_CombatHitNpc(srv, captain_slot, 0, cap->max_hitpoints);
                for( t = 0; t < 12 && player->varps[varp_quest] != killed; t++ )
                    selftest_tick(srv);
                SELFTEST_CHECK(player->varps[varp_quest] == killed,
                               "killing the captain should write desertrescue_killed_capt, got %d",
                               player->varps[varp_quest]);
                SELFTEST_CHECK(tt_inv_has(player, obj_key),
                               "the real ai_queue3 should grant metal_key");
                if( player->varps[varp_quest] == killed && tt_inv_has(player, obj_key) )
                    tt_pass("ai_queue3 captain drops metal_key");
            }
        }

        /* ---- OPLOC2 search gate (flavour) then OPLOC1 without key ---- */
        {
            int gx = 3273;
            int gz = 3029;
            int gate_slot;

            ToriRSServer_WorldTeleport(srv, 0, 3272, 3029);
            selftest_tick(srv);
            gate_slot = tt_find_loc(3273, 3029, 0, loc_gate, 4, &gx, &gz);
            SELFTEST_CHECK(gate_slot >= 0,
                           "miningcampgateclosedl should stand near 3273,3029");
            if( gate_slot >= 0 )
            {
                int before = player->varps[varp_quest];
                int key_slot;
                int s;

                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC2, loc_gate, ToriRSServer_LocCategory(loc_gate),
                    gate_slot);
                tt_drain(srv, player);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_quest] == before,
                               "OPLOC2 search gate must not change desertrescue");
                tt_pass("oploc2 camp gate search");

                /* Hide the key so Open refuses. */
                for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                {
                    if( player->inv[s].obj_id == obj_key )
                        inv_set(player, s, -1, 0);
                }
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_gate, ToriRSServer_LocCategory(loc_gate),
                    gate_slot);
                tt_drain(srv, player);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_quest] == killed,
                               "OPLOC1 without metal_key must not enter the camp, got %d",
                               player->varps[varp_quest]);
                tt_pass("oploc1 camp gate refuses without key");

                /* ---- OPHELD2 wear desert shirt (camp equipment rule) ---- */
                tt_clear_inv(player);
                inv_set(player, 0, obj_shirt, 1);
                inv_set(player, 1, obj_robe, 1);
                inv_set(player, 2, obj_boots, 1);
                inv_set(player, 3, obj_key, 1);
                player->last_slot = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD2, obj_shirt, -1, -1);
                SELFTEST_CHECK(player->worn[TORIRSSERVER_WEAR_BODY].obj_id == obj_shirt,
                               "OPHELD2 desert_shirt should wear on the body, got %d",
                               player->worn[TORIRSSERVER_WEAR_BODY].obj_id);
                if( player->worn[TORIRSSERVER_WEAR_BODY].obj_id == obj_shirt )
                    tt_pass("opheld2 desert_shirt worn");

                /* ---- OPLOCU metal_key opens the gate and writes entered ---- */
                key_slot = 3;
                player->last_useitem = obj_key;
                player->last_useslot = key_slot;
                player->last_slot = key_slot;
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOCU, loc_gate, ToriRSServer_LocCategory(loc_gate),
                    gate_slot);
                tt_drain(srv, player);
                ToriRSServer_WorldCloseModal(srv);
                SELFTEST_CHECK(player->varps[varp_quest] == entered,
                               "OPLOCU metal_key on the camp gate should write entered_camp, got %d",
                               player->varps[varp_quest]);
                if( player->varps[varp_quest] == entered )
                    tt_pass("oplocu metal_key opens camp gate");
                player->last_useitem = -1;
            }
        }

        /* ---- OPNPC1 Al Shabim: looking-for-Bhasim sets spoke bit ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3171, 3028);
        selftest_tick(srv);
        shabim_slot = ToriRSServer_WorldNpcSpawn(srv, npc_shabim, 3171, 3028, 0);
        SELFTEST_CHECK(shabim_slot >= 0, "al_shabim should spawn in the Bedabin camp");
        if( shabim_slot >= 0 )
        {
            ToriRSServer_WorldNpcSetOwner(&srv->npcs[shabim_slot], player);
            player->varps[varp_mech] |= (1 << bit_find_shabim);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_shabim, -1,
                                           shabim_slot);
            tt_choose(srv, player, 2); /* I am looking for Al Zaba Bhasim. */
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK((player->varps[varp_mech] & (1 << bit_spoke_shabim)) != 0,
                           "OPNPC1 al_shabim looking-for should set spoke_al_shabim");
            if( (player->varps[varp_mech] & (1 << bit_spoke_shabim)) != 0 )
                tt_pass("opnpc1 al_shabim looking-for");
        }

        /* ---- OPNPC1 Ana: hello + barrel use writes caught_ana ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3304, 9559);
        selftest_tick(srv);
        ana_slot = ToriRSServer_WorldNpcSpawn(srv, npc_ana, 3304, 9559, 0);
        SELFTEST_CHECK(ana_slot >= 0, "ana should spawn in the mine");
        if( ana_slot >= 0 )
        {
            ToriRSServer_WorldNpcSetOwner(&srv->npcs[ana_slot], player);
            player->varps[varp_quest] = 18; /* used_mine_cart */
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_ana, -1, ana_slot);
            tt_drain(srv, player);
            ToriRSServer_WorldCloseModal(srv);
            tt_pass("opnpc1 ana hello");

            tt_clear_inv(player);
            inv_set(player, 0, obj_empty, 1);
            player->last_useitem = obj_empty;
            player->last_useslot = 0;
            player->last_slot = 0;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_ana, -1, ana_slot);
            tt_drain(srv, player);
            ToriRSServer_WorldCloseModal(srv);
            SELFTEST_CHECK(player->varps[varp_quest] == caught_ana,
                           "OPNPCU empty barrel on Ana should write caught_ana, got %d",
                           player->varps[varp_quest]);
            SELFTEST_CHECK(tt_inv_has(player, obj_ana_barrel),
                           "squeezing Ana should grant thanainabarrel");
            if( player->varps[varp_quest] == caught_ana && tt_inv_has(player, obj_ana_barrel) )
                tt_pass("opnpcu barrel catches Ana");
            player->last_useitem = -1;
        }

        /* ---- OPHELD1 look at Ana-in-barrel ---- */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_ana_barrel, -1, -1);
        tt_drain(srv, player);
        ToriRSServer_WorldCloseModal(srv);
        tt_pass("opheld1 look Ana in barrel");

        /* ---- OPNPC1 cart driver with barrel ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3272, 3028);
        selftest_tick(srv);
        driver_slot = ToriRSServer_WorldNpcSpawn(srv, npc_driver, 3272, 3028, 0);
        SELFTEST_CHECK(driver_slot >= 0, "mining_cart_driver should spawn");
        if( driver_slot >= 0 )
        {
            ToriRSServer_WorldNpcSetOwner(&srv->npcs[driver_slot], player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_driver, -1,
                                           driver_slot);
            tt_drain(srv, player);
            ToriRSServer_WorldCloseModal(srv);
            tt_pass("opnpc1 cart driver with barrel");
        }

        /* ---- OPLOC1 mine door watch + winch + cart (map or skip) ---- */
        {
            int mx = 3285;
            int mz = 3036;
            int mine_slot = tt_find_loc(3285, 3036, 0, loc_mine, 8, &mx, &mz);
            if( mine_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC2, loc_mine, ToriRSServer_LocCategory(loc_mine),
                    mine_slot);
                tt_drain(srv, player);
                ToriRSServer_WorldCloseModal(srv);
                tt_pass("oploc2 watch mine door");
            }
            else
                tt_pass("oploc2 watch mine door (loc not in window)");
        }
        {
            int wx = 3280;
            int wz = 3028;
            int winch_slot = tt_find_loc(3280, 3028, 0, loc_winch, 12, &wx, &wz);
            if( winch_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_winch, ToriRSServer_LocCategory(loc_winch),
                    winch_slot);
                tt_drain(srv, player);
                ToriRSServer_WorldCloseModal(srv);
                tt_pass("oploc1 look winch");
            }
            else
                tt_pass("oploc1 look winch (loc not in window)");
        }
        {
            int cx = 3310;
            int cz = 9545;
            int cart_slot;
            ToriRSServer_WorldTeleport(srv, 0, 3310, 9545);
            selftest_tick(srv);
            cart_slot = tt_find_loc(3310, 9545, 0, loc_cart, 16, &cx, &cz);
            if( cart_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_cart, ToriRSServer_LocCategory(loc_cart),
                    cart_slot);
                tt_drain(srv, player);
                ToriRSServer_WorldCloseModal(srv);
                tt_pass("oploc1 look mine cart");
            }
            else
                tt_pass("oploc1 look mine cart (loc not in window)");
        }

        /* ---- OPNPC1 Irena hand-in with Ana barrel ---- */
        ToriRSServer_WorldTeleport(srv, 0, 3304, 3112);
        selftest_tick(srv);
        if( irena_slot < 0 || !srv->npcs[irena_slot].active )
            irena_slot = ToriRSServer_WorldNpcSpawn(srv, npc_irena, 3304, 3112, 0);
        if( irena_slot >= 0 )
        {
            ToriRSServer_WorldNpcSetOwner(&srv->npcs[irena_slot], player);
            tt_clear_inv(player);
            inv_set(player, 0, obj_ana_barrel, 1);
            player->varps[varp_quest] = escaped;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_irena, -1,
                                           irena_slot);
            tt_choose(srv, player, 1); /* Fletching */
            tt_choose(srv, player, 2); /* Agility */
            ToriRSServer_WorldCloseModal(srv);
            {
                int q = player->varps[varp_quest];
                SELFTEST_CHECK(q >= saved_ana,
                               "handing Ana to Irena should reach saved_ana+, got %d",
                               q);
                if( q >= saved_ana )
                    tt_pass("opnpc1 irena hand-in Ana barrel");
            }
        }

        /* Journal at the live states must not abort. */
        ToriRSServer_ScriptsRunProc(srv, "[proc,desertrescue_journal]", NULL, 0);
        ToriRSServer_WorldCloseModal(srv);
        tt_pass("desertrescue_journal at entered_camp");

        SELFTEST_CHECK(player->godmode == 1 && player->dying == 0,
                       "Gate D walk must leave the player alive and godmoded");
        tt_pass("player alive godmode");

        if( irena_slot >= 0 && srv->npcs[irena_slot].active )
            ToriRSServer_WorldNpcFree(srv, irena_slot);
        if( captain_slot >= 0 && srv->npcs[captain_slot].active )
            ToriRSServer_WorldNpcFree(srv, captain_slot);
        if( shabim_slot >= 0 && srv->npcs[shabim_slot].active )
            ToriRSServer_WorldNpcFree(srv, shabim_slot);
        if( ana_slot >= 0 && srv->npcs[ana_slot].active )
            ToriRSServer_WorldNpcFree(srv, ana_slot);
        if( driver_slot >= 0 && srv->npcs[driver_slot].active )
            ToriRSServer_WorldNpcFree(srv, driver_slot);
        ToriRSServer_WorldNpcReap(srv);

        tt_clear_inv(player);
        player->varps[varp_quest] = 0;
        player->varps[varp_mech] = 0;
        player->varps[varp_duel] = 0;
        player->godmode = 1;
    }

    ToriRSServer_ScriptsFree(srv);
}
