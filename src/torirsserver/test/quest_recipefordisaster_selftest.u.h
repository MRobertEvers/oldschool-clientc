#ifndef TORIRSSERVER_TEST_QUEST_RECIPEFORDISASTER_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_RECIPEFORDISASTER_SELFTEST_U_H

/* Recipe for Disaster Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Cook / Dave / Doris / Traiborn /
 * goblin cook / Kaylee / Rohak / Murphy / Nung / Rantz / WOM /
 * wise monkeys / Agrith / dining locs cannot leak. Real OPNPC1 /
 * OPLOC1 / OPLOCU / OPHELDU / OPNPC4 / AI_OPPLAYER2 / AI_QUEUE3.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Shared triggers are reused, not redeclared: cook (quest_cook.rs2),
 * traiborn, rantz, murphy, Kaylee (belowicemountain), WOM, mm_throne.
 * RFD Amik is hundred_varze_base, not castle sir_amik_varze.
 *
 * Gate: TORIRSSERVER_SELFTEST_RFD_ONLY=1
 */

#define RFD_INTRO_STARTED 1
#define RFD_INTRO_INGREDIENTS 2
#define RFD_INTRO_COMPLETE 3
#define RFD_DAVE_INSPECTED 1
#define RFD_DAVE_TALKED 2
#define RFD_DAVE_DORIS 3
#define RFD_DAVE_STEW 4
#define RFD_DAVE_COMPLETE 5
#define RFD_GUIDE_INSPECTED 1
#define RFD_GUIDE_TRAIBORN 2
#define RFD_GUIDE_CAKE 3
#define RFD_GUIDE_COMPLETE 4
#define RFD_GOBLIN_INSPECTED 1
#define RFD_GOBLIN_CHAR_ASK 2
#define RFD_GOBLIN_CHAR_GIVE 3
#define RFD_GOBLIN_SLOP 4
#define RFD_GOBLIN_COMPLETE 5
#define RFD_DWARF_INSPECTED 1
#define RFD_DWARF_KAYLEE 2
#define RFD_DWARF_ALE 3
#define RFD_DWARF_ROHAK 4
#define RFD_DWARF_INGRED 5
#define RFD_DWARF_COMPLETE 6
#define RFD_PIRATE_INSPECTED 1
#define RFD_PIRATE_COOK 2
#define RFD_PIRATE_MURPHY 3
#define RFD_PIRATE_DIVING 4
#define RFD_PIRATE_HIDE 5
#define RFD_PIRATE_CAKE 6
#define RFD_PIRATE_COMPLETE 7
#define RFD_OGRE_INSPECTED 1
#define RFD_OGRE_RANTZ 2
#define RFD_OGRE_CHOMPY 3
#define RFD_OGRE_COOKED 4
#define RFD_OGRE_COMPLETE 5
#define RFD_AMIK_INSPECTED 1
#define RFD_AMIK_COOK 2
#define RFD_AMIK_WOM 3
#define RFD_AMIK_BRULEE 4
#define RFD_AMIK_COMPLETE 5
#define RFD_MONKEY_INSPECTED 1
#define RFD_MONKEY_AWOW 2
#define RFD_MONKEY_WISE 3
#define RFD_MONKEY_COOKED 4
#define RFD_MONKEY_COMPLETE 5
#define RFD_FINALE_AGRITH 1
#define RFD_FINALE_FLAMBEED 2
#define RFD_FINALE_KARAMEL 3
#define RFD_FINALE_DESSOURT 4
#define RFD_FINALE_MOTHER 5
#define RFD_FINALE_COMPLETE 6
#define RFD_COOK_COMPLETE 2
#define RFD_COOK_REQ 10
#define RFD_FISHING_COMPLETE 5
#define RFD_CREST_COMPLETE 11
#define RFD_WATERFALL_COMPLETE 10
#define RFD_SHILO_COMPLETE 15
#define RFD_HERO_COMPLETE 15
#define RFD_UPASS_COMPLETE 10
#define RFD_ZANARIS_COMPLETE 6
#define RFD_MM_COMPLETE 9
#define RFD_DT_COMPLETE 15
#define RFD_HORROR_COMPLETE 10

#define RFD_COOK_X 3208
#define RFD_COOK_Z 3215
#define RFD_DOOR_X 3206
#define RFD_DOOR_Z 3217
#define RFD_DAVE_X 3080
#define RFD_DAVE_Z 9889
#define RFD_DORIS_X 3080
#define RFD_DORIS_Z 3494
#define RFD_TRAP_X 3078
#define RFD_TRAP_Z 3494
#define RFD_TRAIBORN_X 3112
#define RFD_TRAIBORN_Z 3162
#define RFD_GOBCOOK_X 2959
#define RFD_GOBCOOK_Z 3510
#define RFD_KAYLEE_X 2956
#define RFD_KAYLEE_Z 3370
#define RFD_ROHAK_X 2870
#define RFD_ROHAK_Z 9876
#define RFD_MURPHY_X 2674
#define RFD_MURPHY_Z 3160
#define RFD_NUNG_X 2966
#define RFD_NUNG_Z 9490
#define RFD_KELP_X 2968
#define RFD_KELP_Z 9492
#define RFD_ANCHOR_X 2964
#define RFD_ANCHOR_Z 9488
#define RFD_RANTZ_X 2634
#define RFD_RANTZ_Z 2982
#define RFD_SPIT_X 2632
#define RFD_SPIT_Z 2980
#define RFD_JUBBLY_X 2635
#define RFD_JUBBLY_Z 2965
#define RFD_WOM_X 3094
#define RFD_WOM_Z 3251
#define RFD_SHRINE_X 2460
#define RFD_SHRINE_Z 4376
#define RFD_DRAGON_X 2464
#define RFD_DRAGON_Z 4370
#define RFD_THRONE_X 2800
#define RFD_THRONE_Z 2760
#define RFD_WISE_X 2784
#define RFD_WISE_Z 2760
#define RFD_SNAKE_X 3010
#define RFD_SNAKE_Z 5450
#define RFD_PORTAL_X 3208
#define RFD_PORTAL_Z 3218
#define RFD_ARENA_X 1900
#define RFD_ARENA_Z 5355
#define RFD_ARENA_LEVEL 2

static void
rfd_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "RFD PASS: %s\n", step);
}

static void
rfd_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
rfd_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
rfd_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 8) <= 0 )
            selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
}

static int
rfd_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
rfd_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = rfd_chatmenu();
    if( chatmenu <= 0 )
        return;
    button[0] = (uint8_t)(chatmenu >> 24);
    button[1] = (uint8_t)(chatmenu >> 16);
    button[2] = (uint8_t)(chatmenu >> 8);
    button[3] = (uint8_t)chatmenu;
    button[4] = 0;
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
rfd_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = rfd_chatmenu();
    clicks = 0;
    while( clicks < max_pages && player->active_script )
    {
        if( player->resume_button_count > 0 && chatmenu > 0 &&
            player->resume_buttons[0] == chatmenu )
            return;
        if( selftest_click_through(srv, 1) <= 0 )
            break;
        clicks++;
    }
}

static void
rfd_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    rfd_god(player);
    selftest_tick(srv);
}

static int
rfd_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    rfd_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
rfd_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
rfd_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    return n;
}

static void
rfd_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( rfd_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
        if( player->inv[s].obj_id == obj_id )
        {
            inv_set(player, s, obj_id, player->inv[s].count + count);
            return;
        }
    }
}

static void
rfd_set_progress(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;
    int bit;

    assert(srv);
    assert(name);
    assert(srv->active_player);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        srv->active_player->varps[varp] = value;
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
rfd_get_progress(const struct ToriRSServerPlayer* player, const char* name)
{
    int varp;
    int bit;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        return player->varps[varp];
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        return ToriRSServer_VarbitGet(player, bit);
    return 0;
}

static void
rfd_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
rfd_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
rfd_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    rfd_talk(srv, npc_type, slot);
    rfd_finish(srv);
}

static void
rfd_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    rfd_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        rfd_click_until_menu(srv, 24);
        rfd_pick_row(srv, rows[i]);
    }
    rfd_finish(srv);
}

static int
rfd_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    rfd_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
rfd_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    rfd_finish(srv);
}

static void
rfd_oploc2(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_id, -1, loc_slot);
    rfd_finish(srv);
}

static void
rfd_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    rfd_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
rfd_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    rfd_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
rfd_opnpc4(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC4, npc_type, -1, slot);
    rfd_finish(srv);
}

static void
rfd_kill(struct ToriRSServer* srv, int slot)
{
    struct ToriRSServerNpc* npc;
    int t;

    assert(srv);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return;
    ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints > 0 ? npc->hitpoints : 1);
    for( t = 0; t < 16; t++ )
        selftest_tick(srv);
    rfd_finish(srv);
}

static void
rfd_journal(struct ToriRSServer* srv, const char* proc, const char* step)
{
    assert(srv);
    assert(proc);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, proc, NULL, 0);
    rfd_finish(srv);
    rfd_pass(step);
}

static void
rfd_prereqs(struct ToriRSServer* srv, int cooking)
{
    struct ToriRSServerPlayer* player;
    int stat_cook;

    assert(srv);
    player = srv->active_player;
    assert(player);
    rfd_set_progress(srv, "cookquest", RFD_COOK_COMPLETE);
    rfd_set_progress(srv, "fishingcompo", RFD_FISHING_COMPLETE);
    rfd_set_progress(srv, "crestquest", RFD_CREST_COMPLETE);
    rfd_set_progress(srv, "waterfall_quest", RFD_WATERFALL_COMPLETE);
    rfd_set_progress(srv, "zombiequeen", RFD_SHILO_COMPLETE);
    rfd_set_progress(srv, "heroquest", RFD_HERO_COMPLETE);
    rfd_set_progress(srv, "upass", RFD_UPASS_COMPLETE);
    rfd_set_progress(srv, "zanaris", RFD_ZANARIS_COMPLETE);
    rfd_set_progress(srv, "mm_main", RFD_MM_COMPLETE);
    rfd_set_progress(srv, "deserttreasure", RFD_DT_COMPLETE);
    rfd_set_progress(srv, "horrorquest", RFD_HORROR_COMPLETE);
    stat_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "cooking");
    rfd_set_stat(player, stat_cook, cooking);
}

static void
rfd_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    rfd_set_progress(srv, "recipefordisaster", 0);
    rfd_set_progress(srv, "rfd_evildave", 0);
    rfd_set_progress(srv, "rfd_lumbridgeguide", 0);
    rfd_set_progress(srv, "rfd_goblins", 0);
    rfd_set_progress(srv, "rfd_dwarf", 0);
    rfd_set_progress(srv, "rfd_pirate", 0);
    rfd_set_progress(srv, "rfd_ogre", 0);
    rfd_set_progress(srv, "rfd_amikvarze", 0);
    rfd_set_progress(srv, "rfd_monkey", 0);
    rfd_set_progress(srv, "rfd_finale", 0);
    rfd_prereqs(srv, RFD_COOK_REQ);
}

static void
rfd_wear_greegree(struct ToriRSServerPlayer* player)
{
    int greegree;
    int amulet;

    assert(player);
    greegree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_monkey_greegree_for_normal_monkey");
    amulet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_amulet_of_monkey_speak");
    if( greegree > 0 )
        worn_set(player, TORIRSSERVER_WEAR_WEAPON, greegree, 1);
    if( amulet > 0 )
        worn_set(player, TORIRSSERVER_WEAR_AMULET, amulet, 1);
}

static void
selftest_quest_recipefordisaster(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_cook;
    int npc_dave;
    int npc_doris;
    int npc_traiborn;
    int npc_gobcook;
    int npc_kaylee;
    int npc_rohak;
    int npc_murphy;
    int npc_nung;
    int npc_rantz;
    int npc_wom;
    int npc_wise;
    int npc_mudskip;
    int npc_crab;
    int npc_jubbly;
    int npc_jubbly_dead;
    int npc_chicken;
    int npc_dragon;
    int npc_snake;
    int npc_agrith;
    int npc_flambeed;
    int npc_karamel;
    int npc_dessourt;
    int npc_mother;
    int npc_cullin;
    int loc_door;
    int loc_dave;
    int loc_guide;
    int loc_goblin;
    int loc_dwarf;
    int loc_pete;
    int loc_skrach;
    int loc_amik;
    int loc_monkey;
    int loc_trap_closed;
    int loc_trap_open;
    int loc_kelp;
    int loc_anchor;
    int loc_spit;
    int loc_shrine;
    int loc_throne;
    int loc_portal;
    int obj_newt;
    int obj_ale;
    int obj_tomato;
    int obj_blast;
    int obj_ashes;
    int obj_dirty;
    int obj_stew;
    int obj_evil_stew;
    int obj_egg;
    int obj_flour;
    int obj_milk;
    int obj_tin;
    int obj_cake;
    int obj_charcoal;
    int obj_bait;
    int obj_spice;
    int obj_orange;
    int obj_dye;
    int obj_bread;
    int obj_water;
    int obj_dyed;
    int obj_maggots;
    int obj_soggy;
    int obj_slop;
    int obj_coins;
    int obj_asg;
    int obj_asgold;
    int obj_bowl;
    int obj_rockcake;
    int obj_helmet;
    int obj_pack;
    int obj_hide;
    int obj_kelp;
    int obj_crab;
    int obj_cod;
    int obj_fishcake;
    int obj_chompy;
    int obj_jubbly_raw;
    int obj_jubbly_cook;
    int obj_chicken;
    int obj_egg_evil;
    int obj_token;
    int obj_cream;
    int obj_brulee;
    int obj_corpse;
    int obj_banana;
    int obj_nuts;
    int obj_rope;
    int obj_knife;
    int obj_pestle;
    int obj_snake;
    int slot;
    int loc_slot;
    static const int k_cook_no[] = { 2 };
    static const int k_cook_yes[] = { 1 };
    static const int k_kaylee_no[] = { 2 };
    static const int k_kaylee_yes[] = { 1 };
    static const int k_murphy_no[] = { 2 };
    static const int k_murphy_yes[] = { 1 };

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: recipe for disaster critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer rfd selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    rfd_god(player);
    rfd_reset_quest(srv);
    rfd_clear_inv(player);

    npc_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "cook");
    npc_dave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_evil_dave");
    npc_doris = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_dave_mum");
    npc_traiborn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "traiborn");
    npc_gobcook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "goblin_cook");
    npc_kaylee = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "risingsun_barmaid2");
    npc_rohak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_dwarf_dad");
    npc_murphy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "murphy");
    npc_nung = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "100_pirate_mogre_nung");
    npc_rantz = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rantz");
    npc_wom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wise_old_man");
    npc_wise = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_ilm_iwazaru");
    npc_mudskip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_pirate_giant_mudskipper");
    npc_crab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_pirate_giant_crab");
    npc_jubbly = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "100_jubbly_bird");
    npc_jubbly_dead = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "100_jubbly_bird_dead");
    npc_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "chickenquest_evil_chicken");
    npc_dragon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "black_dragon");
    npc_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_ilm_snake");
    npc_agrith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_minion1");
    npc_flambeed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_minion2");
    npc_karamel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_minion3");
    npc_dessourt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_minion4");
    npc_mother = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_minion5_air");
    npc_cullin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "hundred_culinaromancer_final");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_lumbridge_door");
    loc_dave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_dave_base");
    loc_guide = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_guide_base");
    loc_goblin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_goblin1_base");
    loc_dwarf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_dwarf_ambassador_base");
    loc_pete = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_pirate_base");
    loc_skrach = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_ogre_base");
    loc_amik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_varze_base");
    loc_monkey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_monkey_base");
    loc_trap_closed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "100_dave_celler_trapdoor_closed");
    loc_trap_open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "100_dave_celler_trapdoor_open");
    loc_kelp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kelp_pickingpoint");
    loc_anchor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "anchor_middle");
    loc_spit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "multi_chompybird_spitroast_entity");
    loc_shrine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "fairy_chicken_shrine");
    loc_throne = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mm_throne");
    loc_portal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "hundred_portal_multi");
    obj_newt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eye_of_newt");
    obj_ale = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "greenmans_ale");
    obj_tomato = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rotten_tomato");
    obj_blast = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fruit_blast");
    obj_ashes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ashes");
    obj_dirty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_fruit_blast");
    obj_stew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "stew");
    obj_evil_stew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_dave_stew");
    obj_egg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "egg");
    obj_flour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_flour");
    obj_milk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_milk");
    obj_tin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cake_tin");
    obj_cake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "100guide_guidecake");
    obj_charcoal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "charcoal");
    obj_bait = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fishing_bait");
    obj_spice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gnome_spice");
    obj_orange = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "orange_slices");
    obj_dye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bluedye");
    obj_bread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bread");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_water");
    obj_dyed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "100goblin_dyed_oranges");
    obj_maggots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "100goblin_spicey_maggots");
    obj_soggy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "100goblin_soggy_bread");
    obj_slop = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "100goblin_compromise_mush");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_asg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "asgarnian_ale");
    obj_asgold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_dwarf_asgarnian_ale");
    obj_bowl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bowl_water");
    obj_rockcake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_dwarf_cool_rockcake");
    obj_helmet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_pirate_diving_helmet");
    obj_pack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_pirate_diving_backpack");
    obj_hide = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_pirate_mudskipper_hide");
    obj_kelp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_pirate_kelp");
    obj_crab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_pirate_giant_crab_meat");
    obj_cod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_cod");
    obj_fishcake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_pirate_fishcake");
    obj_chompy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_chompy");
    obj_jubbly_raw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "100_jubbly_meat_raw");
    obj_jubbly_cook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "100_jubbly_meat_cooked");
    obj_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "raw_chicken");
    obj_egg_evil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chickenquest_evil_chicken_egg");
    obj_token = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chickenquest_dragon_coin");
    obj_cream = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_of_cream");
    obj_brulee = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chickenquest_brulee_mixture_supreme");
    obj_corpse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_ilm_snake_corpse");
    obj_banana = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "banana");
    obj_nuts = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mm_monkey_nuts");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
    obj_pestle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pestle_and_mortar");
    obj_snake = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hundred_ilm_cooked_stuffed_snake");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "recipefordisaster") >= 0,
                   "varp recipefordisaster should resolve");
    SELFTEST_CHECK(npc_cook > 0, "npc cook should resolve");
    SELFTEST_CHECK(loc_door >= 0, "loc hundred_lumbridge_door should resolve");
    SELFTEST_CHECK(npc_agrith > 0, "npc hundred_minion1 Agrith-Na-Na should resolve");

    rfd_journal(srv, "[proc,rfd_intro_journal]", "journal_intro_0");
    rfd_journal(srv, "[proc,rfd_overview_journal]", "journal_overview_todo");

    /* Intro: Cook's Assistant incomplete must not start RFD. */
    rfd_set_progress(srv, "cookquest", 0);
    slot = rfd_spawn(srv, npc_cook, RFD_COOK_X, RFD_COOK_Z, 0);
    if( slot >= 0 )
    {
        rfd_talk_finish(srv, npc_cook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "recipefordisaster") == 0,
                       "Cook without Cook's Assistant must not start RFD");
        rfd_pass("opnpc1_cook_need_cooks_assistant");
    }

    /* Cooking 9 must not start RFD. */
    rfd_set_progress(srv, "cookquest", RFD_COOK_COMPLETE);
    rfd_prereqs(srv, RFD_COOK_REQ - 1);
    if( slot >= 0 )
    {
        rfd_talk_finish(srv, npc_cook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "recipefordisaster") == 0,
                       "Cook with Cooking 9 must not start RFD");
        rfd_pass("opnpc1_cook_need_cooking_10");
    }

    rfd_prereqs(srv, RFD_COOK_REQ);
    if( slot >= 0 )
    {
        rfd_talk_rows(srv, npc_cook, slot, k_cook_no, 1);
        SELFTEST_CHECK(rfd_get_progress(player, "recipefordisaster") == 0,
                       "Cook decline must stay not started");
        rfd_pass("opnpc1_cook_decline");

        rfd_talk_rows(srv, npc_cook, slot, k_cook_yes, 1);
        SELFTEST_CHECK(rfd_get_progress(player, "recipefordisaster") == RFD_INTRO_STARTED,
                       "Cook accept must write intro_started, got %d",
                       rfd_get_progress(player, "recipefordisaster"));
        rfd_pass("opnpc1_cook_accept_started");

        rfd_talk_finish(srv, npc_cook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "recipefordisaster") == RFD_INTRO_STARTED,
                       "Cook without ingredients must stay started");
        rfd_pass("opnpc1_cook_missing_ingredients");

        if( obj_blast > 0 && obj_ashes > 0 )
        {
            rfd_give(player, obj_blast, 1);
            rfd_give(player, obj_ashes, 1);
            rfd_opheldu(srv, obj_blast, obj_ashes);
            SELFTEST_CHECK(rfd_inv_total(player, obj_dirty) == 1, "ashes on fruit blast must make dirty blast");
            rfd_pass("opheldu_dirty_blast");
        }

        if( obj_newt > 0 )
            rfd_give(player, obj_newt, 1);
        if( obj_ale > 0 )
            rfd_give(player, obj_ale, 1);
        if( obj_tomato > 0 )
            rfd_give(player, obj_tomato, 1);
        if( obj_dirty > 0 && rfd_inv_total(player, obj_dirty) == 0 )
            rfd_give(player, obj_dirty, 1);
        rfd_talk_finish(srv, npc_cook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "recipefordisaster") == RFD_INTRO_INGREDIENTS,
                       "Cook with ingredients must write ingredients_given, got %d",
                       rfd_get_progress(player, "recipefordisaster"));
        rfd_pass("opnpc1_cook_give_ingredients");

        rfd_talk_finish(srv, npc_cook, slot);
        rfd_pass("opnpc1_cook_go_see_feast");
    }
    rfd_journal(srv, "[proc,rfd_intro_journal]", "journal_intro_started");

    loc_slot = rfd_place_loc(srv, loc_door, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_door >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "recipefordisaster", RFD_INTRO_STARTED);
        rfd_oploc(srv, loc_door, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "recipefordisaster") == RFD_INTRO_STARTED,
                       "locked dining door must stay started");
        rfd_pass("oploc1_dining_door_locked");

        rfd_set_progress(srv, "recipefordisaster", RFD_INTRO_INGREDIENTS);
        rfd_oploc(srv, loc_door, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "recipefordisaster") == RFD_INTRO_COMPLETE,
                       "dining door must complete intro, got %d",
                       rfd_get_progress(player, "recipefordisaster"));
        rfd_pass("oploc1_dining_door_complete");
    }
    rfd_journal(srv, "[proc,rfd_intro_journal]", "journal_intro_complete");
    rfd_free_npc(srv, slot);

    /* Evil Dave */
    rfd_set_progress(srv, "recipefordisaster", RFD_INTRO_COMPLETE);
    loc_slot = rfd_place_loc(srv, loc_dave, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_dave >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_dave, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_evildave") == RFD_DAVE_INSPECTED,
                       "inspect Dave must write inspected");
        rfd_pass("oploc1_dave_inspect");
        rfd_oploc(srv, loc_dave, loc_slot);
        rfd_pass("oploc1_dave_inspect_reminder");
    }

    slot = rfd_spawn(srv, npc_dave, RFD_DAVE_X, RFD_DAVE_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_evildave", 0);
        rfd_talk_finish(srv, npc_dave, slot);
        rfd_pass("opnpc1_dave_too_early");

        rfd_set_progress(srv, "rfd_evildave", RFD_DAVE_INSPECTED);
        rfd_talk_finish(srv, npc_dave, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_evildave") == RFD_DAVE_TALKED,
                       "Dave talk must write talked, got %d",
                       rfd_get_progress(player, "rfd_evildave"));
        rfd_pass("opnpc1_dave_talk");
        rfd_talk_finish(srv, npc_dave, slot);
        rfd_pass("opnpc1_dave_talked_reminder");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_doris, RFD_DORIS_X, RFD_DORIS_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_evildave", RFD_DAVE_INSPECTED);
        rfd_talk_finish(srv, npc_doris, slot);
        rfd_pass("opnpc1_doris_too_early");

        rfd_set_progress(srv, "rfd_evildave", RFD_DAVE_TALKED);
        rfd_talk_finish(srv, npc_doris, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_evildave") == RFD_DAVE_DORIS,
                       "Doris without stew must write doris_talked");
        rfd_pass("opnpc1_doris_need_stew");

        if( obj_stew > 0 )
            rfd_give(player, obj_stew, 1);
        rfd_talk_finish(srv, npc_doris, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_evildave") == RFD_DAVE_STEW,
                       "Doris with stew must write stew_made, got %d",
                       rfd_get_progress(player, "rfd_evildave"));
        SELFTEST_CHECK(rfd_inv_total(player, obj_evil_stew) == 1, "Doris must hand over evil stew");
        rfd_pass("opnpc1_doris_season_stew");
        rfd_talk_finish(srv, npc_doris, slot);
        rfd_pass("opnpc1_doris_after");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_trap_closed, RFD_TRAP_X, RFD_TRAP_Z, 0);
    if( loc_trap_closed >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_trap_closed, loc_slot);
        rfd_pass("oploc1_dave_trapdoor_open");
    }
    loc_slot = rfd_place_loc(srv, loc_trap_open, RFD_TRAP_X, RFD_TRAP_Z, 0);
    if( loc_trap_open >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_trap_open, loc_slot);
        rfd_pass("oploc1_dave_trapdoor_climb");
        loc_slot = rfd_place_loc(srv, loc_trap_open, RFD_TRAP_X, RFD_TRAP_Z, 0);
        rfd_oploc2(srv, loc_trap_open, loc_slot);
        rfd_pass("oploc2_dave_trapdoor_close");
    }

    loc_slot = rfd_place_loc(srv, loc_dave, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_dave >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_evildave", RFD_DAVE_STEW);
        rfd_use_loc(srv, loc_dave, loc_slot, obj_stew);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_evildave") == RFD_DAVE_STEW,
                       "wrong stew must not free Dave");
        rfd_pass("oplocu_dave_wrong");
        if( obj_evil_stew > 0 )
            rfd_give(player, obj_evil_stew, 1);
        rfd_use_loc(srv, loc_dave, loc_slot, obj_evil_stew);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_evildave") == RFD_DAVE_COMPLETE,
                       "evil stew must complete Dave, got %d",
                       rfd_get_progress(player, "rfd_evildave"));
        rfd_pass("oplocu_dave_stew");
    }
    rfd_journal(srv, "[proc,rfd_evildave_journal]", "journal_dave_complete");

    /* Lumbridge Guide / Traiborn */
    loc_slot = rfd_place_loc(srv, loc_guide, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_guide >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_guide, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_lumbridgeguide") == RFD_GUIDE_INSPECTED,
                       "inspect Guide must write inspected");
        rfd_pass("oploc1_guide_inspect");
        rfd_oploc(srv, loc_guide, loc_slot);
        rfd_pass("oploc1_guide_inspect_reminder");
    }

    slot = rfd_spawn(srv, npc_traiborn, RFD_TRAIBORN_X, RFD_TRAIBORN_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_lumbridgeguide", 0);
        rfd_talk_finish(srv, npc_traiborn, slot);
        rfd_pass("opnpc1_traiborn_not_inspected");

        rfd_set_progress(srv, "rfd_lumbridgeguide", RFD_GUIDE_INSPECTED);
        rfd_talk_finish(srv, npc_traiborn, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_lumbridgeguide") == RFD_GUIDE_TRAIBORN,
                       "Traiborn ask must write traiborn_talked");
        rfd_pass("opnpc1_traiborn_ask_cake");
        rfd_talk_finish(srv, npc_traiborn, slot);
        rfd_pass("opnpc1_traiborn_need_ingredients");

        if( obj_egg > 0 )
            rfd_give(player, obj_egg, 1);
        if( obj_flour > 0 )
            rfd_give(player, obj_flour, 1);
        if( obj_milk > 0 )
            rfd_give(player, obj_milk, 1);
        if( obj_tin > 0 )
            rfd_give(player, obj_tin, 1);
        rfd_talk_finish(srv, npc_traiborn, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_lumbridgeguide") == RFD_GUIDE_CAKE,
                       "Traiborn enchant must write cake_made, got %d",
                       rfd_get_progress(player, "rfd_lumbridgeguide"));
        SELFTEST_CHECK(rfd_inv_total(player, obj_cake) == 1, "Traiborn must hand over Cake of Guidance");
        rfd_pass("opnpc1_traiborn_enchant");
        rfd_talk_finish(srv, npc_traiborn, slot);
        rfd_pass("opnpc1_traiborn_after");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_guide, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_guide >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_lumbridgeguide", RFD_GUIDE_CAKE);
        rfd_use_loc(srv, loc_guide, loc_slot, obj_stew);
        rfd_pass("oplocu_guide_wrong");
        if( obj_cake > 0 )
            rfd_give(player, obj_cake, 1);
        rfd_use_loc(srv, loc_guide, loc_slot, obj_cake);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_lumbridgeguide") == RFD_GUIDE_COMPLETE,
                       "cake must complete Guide, got %d",
                       rfd_get_progress(player, "rfd_lumbridgeguide"));
        rfd_pass("oplocu_guide_cake");
    }
    rfd_journal(srv, "[proc,rfd_lumbridgeguide_journal]", "journal_guide_complete");

    /* Goblins */
    loc_slot = rfd_place_loc(srv, loc_goblin, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_goblin >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_goblin, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_goblins") == RFD_GOBLIN_INSPECTED,
                       "inspect goblins must write inspected");
        rfd_pass("oploc1_goblin_inspect");
        rfd_oploc(srv, loc_goblin, loc_slot);
        rfd_pass("oploc1_goblin_inspect_reminder");
    }

    slot = rfd_spawn(srv, npc_gobcook, RFD_GOBCOOK_X, RFD_GOBCOOK_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_goblins", 0);
        rfd_talk_finish(srv, npc_gobcook, slot);
        rfd_pass("opnpc1_goblincook_too_early");

        rfd_set_progress(srv, "rfd_goblins", RFD_GOBLIN_INSPECTED);
        rfd_talk_finish(srv, npc_gobcook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_goblins") == RFD_GOBLIN_CHAR_ASK,
                       "Goblin Cook ask must write charcoal_asked");
        rfd_pass("opnpc1_goblincook_ask_charcoal");
        rfd_talk_finish(srv, npc_gobcook, slot);
        rfd_pass("opnpc1_goblincook_need_charcoal");

        if( obj_charcoal > 0 )
            rfd_give(player, obj_charcoal, 1);
        rfd_talk_finish(srv, npc_gobcook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_goblins") == RFD_GOBLIN_CHAR_GIVE,
                       "charcoal must write charcoal_given, got %d",
                       rfd_get_progress(player, "rfd_goblins"));
        rfd_pass("opnpc1_goblincook_give_charcoal");
    }

    if( obj_bait > 0 && obj_spice > 0 )
    {
        rfd_give(player, obj_bait, 1);
        rfd_give(player, obj_spice, 1);
        rfd_opheldu(srv, obj_bait, obj_spice);
        SELFTEST_CHECK(rfd_inv_total(player, obj_maggots) == 1, "spice on bait must make spicy maggots");
        rfd_pass("opheldu_spicy_maggots");
    }
    if( obj_orange > 0 && obj_dye > 0 )
    {
        rfd_give(player, obj_orange, 1);
        rfd_give(player, obj_dye, 1);
        rfd_opheldu(srv, obj_orange, obj_dye);
        SELFTEST_CHECK(rfd_inv_total(player, obj_dyed) == 1, "dye on orange slices must make dyed oranges");
        rfd_pass("opheldu_dyed_oranges");
    }
    if( obj_bread > 0 && obj_water > 0 )
    {
        rfd_give(player, obj_bread, 1);
        rfd_give(player, obj_water, 1);
        rfd_opheldu(srv, obj_bread, obj_water);
        SELFTEST_CHECK(rfd_inv_total(player, obj_soggy) == 1, "water on bread must make soggy bread");
        rfd_pass("opheldu_soggy_bread");
    }

    if( slot >= 0 )
    {
        if( obj_dyed > 0 && rfd_inv_total(player, obj_dyed) == 0 )
            rfd_give(player, obj_dyed, 1);
        if( obj_maggots > 0 && rfd_inv_total(player, obj_maggots) == 0 )
            rfd_give(player, obj_maggots, 1);
        if( obj_soggy > 0 && rfd_inv_total(player, obj_soggy) == 0 )
            rfd_give(player, obj_soggy, 1);
        rfd_talk_finish(srv, npc_gobcook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_goblins") == RFD_GOBLIN_SLOP,
                       "ingredients must write slop_made, got %d",
                       rfd_get_progress(player, "rfd_goblins"));
        rfd_pass("opnpc1_goblincook_make_slop");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_goblin, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_goblin >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_goblins", RFD_GOBLIN_SLOP);
        rfd_use_loc(srv, loc_goblin, loc_slot, obj_stew);
        rfd_pass("oplocu_goblin_wrong");
        if( obj_slop > 0 )
            rfd_give(player, obj_slop, 1);
        rfd_use_loc(srv, loc_goblin, loc_slot, obj_slop);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_goblins") == RFD_GOBLIN_COMPLETE,
                       "slop must complete goblins, got %d",
                       rfd_get_progress(player, "rfd_goblins"));
        rfd_pass("oplocu_goblin_slop");
    }
    rfd_journal(srv, "[proc,rfd_goblins_journal]", "journal_goblins_complete");

    /* Mountain Dwarf / Kaylee / Rohak */
    loc_slot = rfd_place_loc(srv, loc_dwarf, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_dwarf >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "fishingcompo", 0);
        rfd_oploc(srv, loc_dwarf, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_dwarf") == 0,
                       "Fishing Contest hard gate must block dwarf inspect");
        rfd_pass("oploc1_dwarf_prereq_fail");
        rfd_set_progress(srv, "fishingcompo", RFD_FISHING_COMPLETE);
        rfd_oploc(srv, loc_dwarf, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_dwarf") == RFD_DWARF_INSPECTED,
                       "inspect dwarf must write inspected");
        rfd_pass("oploc1_dwarf_inspect");
        rfd_oploc(srv, loc_dwarf, loc_slot);
        rfd_pass("oploc1_dwarf_inspect_reminder");
    }

    slot = rfd_spawn(srv, npc_kaylee, RFD_KAYLEE_X, RFD_KAYLEE_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_dwarf", 0);
        rfd_talk_finish(srv, npc_kaylee, slot);
        rfd_pass("opnpc1_kaylee_not_inspected");

        rfd_set_progress(srv, "rfd_dwarf", RFD_DWARF_INSPECTED);
        rfd_talk_rows(srv, npc_kaylee, slot, k_kaylee_no, 1);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_dwarf") == RFD_DWARF_INSPECTED,
                       "Kaylee decline must stay inspected");
        rfd_pass("opnpc1_kaylee_decline");

        rfd_talk_finish(srv, npc_kaylee, slot);
        rfd_pass("opnpc1_kaylee_no_coins");

        if( obj_coins > 0 )
            rfd_give(player, obj_coins, 200);
        rfd_talk_rows(srv, npc_kaylee, slot, k_kaylee_yes, 1);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_dwarf") == RFD_DWARF_KAYLEE,
                       "Kaylee pay must write kaylee_talked, got %d",
                       rfd_get_progress(player, "rfd_dwarf"));
        rfd_pass("opnpc1_kaylee_pay");
    }
    rfd_free_npc(srv, slot);

    if( obj_asg > 0 && obj_coins > 0 )
    {
        rfd_give(player, obj_asg, 1);
        rfd_give(player, obj_coins, 1);
        rfd_opheldu(srv, obj_asg, obj_coins);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_dwarf") == RFD_DWARF_ALE,
                       "coin in ale must write ale_made");
        SELFTEST_CHECK(rfd_inv_total(player, obj_asgold) == 1, "must make Asgoldian ale");
        rfd_pass("opheldu_asgoldian_ale");
    }

    slot = rfd_spawn(srv, npc_rohak, RFD_ROHAK_X, RFD_ROHAK_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_dwarf", RFD_DWARF_INSPECTED);
        rfd_talk_finish(srv, npc_rohak, slot);
        rfd_pass("opnpc1_rohak_too_early");

        rfd_set_progress(srv, "rfd_dwarf", RFD_DWARF_KAYLEE);
        rfd_talk_finish(srv, npc_rohak, slot);
        rfd_pass("opnpc1_rohak_fancy_drink");

        rfd_set_progress(srv, "rfd_dwarf", RFD_DWARF_ALE);
        rfd_talk_finish(srv, npc_rohak, slot);
        rfd_pass("opnpc1_rohak_need_ale");

        if( obj_asgold > 0 )
            rfd_give(player, obj_asgold, 1);
        rfd_talk_finish(srv, npc_rohak, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_dwarf") == RFD_DWARF_ROHAK,
                       "ale must write rohak_talked, got %d",
                       rfd_get_progress(player, "rfd_dwarf"));
        rfd_pass("opnpc1_rohak_give_ale");
        rfd_talk_finish(srv, npc_rohak, slot);
        rfd_pass("opnpc1_rohak_need_ingredients");

        if( obj_milk > 0 )
            rfd_give(player, obj_milk, 1);
        if( obj_flour > 0 )
            rfd_give(player, obj_flour, 1);
        if( obj_egg > 0 )
            rfd_give(player, obj_egg, 1);
        if( obj_bowl > 0 )
            rfd_give(player, obj_bowl, 1);
        rfd_talk_finish(srv, npc_rohak, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_dwarf") == RFD_DWARF_INGRED,
                       "ingredients must write ingredients_given, got %d",
                       rfd_get_progress(player, "rfd_dwarf"));
        rfd_pass("opnpc1_rohak_bake_cake");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_dwarf, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_dwarf >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_dwarf", RFD_DWARF_INGRED);
        rfd_use_loc(srv, loc_dwarf, loc_slot, obj_stew);
        rfd_pass("oplocu_dwarf_wrong");
        if( obj_rockcake > 0 )
            rfd_give(player, obj_rockcake, 1);
        rfd_use_loc(srv, loc_dwarf, loc_slot, obj_rockcake);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_dwarf") == RFD_DWARF_COMPLETE,
                       "rock cake must complete dwarf, got %d",
                       rfd_get_progress(player, "rfd_dwarf"));
        rfd_pass("oplocu_dwarf_cake");
    }
    rfd_journal(srv, "[proc,rfd_dwarf_journal]", "journal_dwarf_complete");

    /* Pirate Pete */
    loc_slot = rfd_place_loc(srv, loc_pete, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_pete >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_pete, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_pirate") == RFD_PIRATE_INSPECTED,
                       "inspect Pete must write inspected");
        rfd_pass("oploc1_pete_inspect");
        rfd_oploc(srv, loc_pete, loc_slot);
        rfd_pass("oploc1_pete_inspect_reminder");
    }

    slot = rfd_spawn(srv, npc_cook, RFD_COOK_X, RFD_COOK_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", 0);
        rfd_set_progress(srv, "rfd_amikvarze", 0);
        rfd_talk_finish(srv, npc_cook, slot);
        rfd_pass("opnpc1_pirate_cook_too_early");

        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_INSPECTED);
        rfd_talk_finish(srv, npc_cook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_pirate") == RFD_PIRATE_COOK,
                       "pirate cook talk must write cook_talked");
        rfd_pass("opnpc1_pirate_cook_talk");
        rfd_talk_finish(srv, npc_cook, slot);
        rfd_pass("opnpc1_pirate_cook_see_murphy");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_murphy, RFD_MURPHY_X, RFD_MURPHY_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_COOK);
        rfd_talk_finish(srv, npc_murphy, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_pirate") == RFD_PIRATE_MURPHY,
                       "Murphy must write murphy_talked and hand gear");
        rfd_pass("opnpc1_murphy_give_gear");
        rfd_clear_inv(player);
        rfd_talk_finish(srv, npc_murphy, slot);
        rfd_pass("opnpc1_murphy_need_gear");

        if( obj_helmet > 0 )
            rfd_give(player, obj_helmet, 1);
        if( obj_pack > 0 )
            rfd_give(player, obj_pack, 1);
        rfd_talk_rows(srv, npc_murphy, slot, k_murphy_no, 1);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_pirate") == RFD_PIRATE_MURPHY,
                       "Murphy dive decline must stay murphy_talked");
        rfd_pass("opnpc1_murphy_dive_decline");

        if( obj_helmet > 0 )
            rfd_give(player, obj_helmet, 1);
        if( obj_pack > 0 )
            rfd_give(player, obj_pack, 1);
        rfd_talk_rows(srv, npc_murphy, slot, k_murphy_yes, 1);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_pirate") == RFD_PIRATE_DIVING,
                       "Murphy dive must write diving, got %d",
                       rfd_get_progress(player, "rfd_pirate"));
        rfd_pass("opnpc1_murphy_dive");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_nung, RFD_NUNG_X, RFD_NUNG_Z, 1);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_COOK);
        rfd_talk_finish(srv, npc_nung, slot);
        rfd_pass("opnpc1_nung_too_early");

        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_DIVING);
        rfd_talk_finish(srv, npc_nung, slot);
        rfd_pass("opnpc1_nung_need_hide");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_mudskip, RFD_NUNG_X + 2, RFD_NUNG_Z, 1);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_DIVING);
        rfd_kill(srv, slot);
        rfd_pass("ai_queue3_mudskipper");
    }
    rfd_free_npc(srv, slot);
    if( obj_hide > 0 )
        rfd_give(player, obj_hide, 1);

    slot = rfd_spawn(srv, npc_nung, RFD_NUNG_X, RFD_NUNG_Z, 1);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_DIVING);
        if( obj_hide > 0 )
            rfd_give(player, obj_hide, 1);
        rfd_talk_finish(srv, npc_nung, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_pirate") == RFD_PIRATE_HIDE,
                       "Nung hide must write hide_given");
        rfd_pass("opnpc1_nung_give_hide");
        rfd_talk_finish(srv, npc_nung, slot);
        rfd_pass("opnpc1_nung_after");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_crab, RFD_NUNG_X + 3, RFD_NUNG_Z, 1);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_HIDE);
        rfd_kill(srv, slot);
        rfd_pass("ai_queue3_crab");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_kelp, RFD_KELP_X, RFD_KELP_Z, 1);
    if( loc_kelp >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_HIDE);
        rfd_oploc(srv, loc_kelp, loc_slot);
        SELFTEST_CHECK(rfd_inv_total(player, obj_kelp) >= 1, "kelp pick must add kelp");
        rfd_pass("oploc1_kelp_pick");
    }
    loc_slot = rfd_place_loc(srv, loc_anchor, RFD_ANCHOR_X, RFD_ANCHOR_Z, 1);
    if( loc_anchor >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_anchor, loc_slot);
        rfd_pass("oploc1_anchor_climb");
    }

    slot = rfd_spawn(srv, npc_cook, RFD_COOK_X, RFD_COOK_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_HIDE);
        rfd_set_progress(srv, "rfd_amikvarze", 0);
        rfd_talk_finish(srv, npc_cook, slot);
        rfd_pass("opnpc1_pirate_cook_need_ingredients");
        if( obj_kelp > 0 )
            rfd_give(player, obj_kelp, 1);
        if( obj_crab > 0 )
            rfd_give(player, obj_crab, 1);
        if( obj_cod > 0 )
            rfd_give(player, obj_cod, 1);
        if( obj_bread > 0 )
            rfd_give(player, obj_bread, 1);
        rfd_talk_finish(srv, npc_cook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_pirate") == RFD_PIRATE_CAKE,
                       "Cook fishcake must write cake_made, got %d",
                       rfd_get_progress(player, "rfd_pirate"));
        rfd_pass("opnpc1_pirate_cook_make_cake");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_pete, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_pete >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_CAKE);
        rfd_use_loc(srv, loc_pete, loc_slot, obj_stew);
        rfd_pass("oplocu_pete_wrong");
        if( obj_fishcake > 0 )
            rfd_give(player, obj_fishcake, 1);
        rfd_use_loc(srv, loc_pete, loc_slot, obj_fishcake);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_pirate") == RFD_PIRATE_COMPLETE,
                       "fishcake must complete Pete, got %d",
                       rfd_get_progress(player, "rfd_pirate"));
        rfd_pass("oplocu_pete_cake");
    }
    rfd_journal(srv, "[proc,rfd_pirate_journal]", "journal_pirate_complete");

    /* Skrach / Rantz / jubbly */
    loc_slot = rfd_place_loc(srv, loc_skrach, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_skrach >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_skrach, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_ogre") == RFD_OGRE_INSPECTED,
                       "inspect Skrach must write inspected");
        rfd_pass("oploc1_skrach_inspect");
        rfd_oploc(srv, loc_skrach, loc_slot);
        rfd_pass("oploc1_skrach_inspect_reminder");
    }

    slot = rfd_spawn(srv, npc_rantz, RFD_RANTZ_X, RFD_RANTZ_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_ogre", RFD_OGRE_INSPECTED);
        rfd_talk_finish(srv, npc_rantz, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_ogre") == RFD_OGRE_RANTZ,
                       "Rantz talk must write rantz_talked");
        rfd_pass("opnpc1_rantz_talk");
        rfd_talk_finish(srv, npc_rantz, slot);
        rfd_pass("opnpc1_rantz_need_chompy");
        if( obj_chompy > 0 )
            rfd_give(player, obj_chompy, 1);
        rfd_talk_finish(srv, npc_rantz, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_ogre") == RFD_OGRE_CHOMPY,
                       "chompy must write chompy_given, got %d",
                       rfd_get_progress(player, "rfd_ogre"));
        rfd_pass("opnpc1_rantz_give_chompy");
        rfd_talk_finish(srv, npc_rantz, slot);
        rfd_pass("opnpc1_rantz_need_meat");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_jubbly, RFD_JUBBLY_X, RFD_JUBBLY_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_ogre", RFD_OGRE_CHOMPY);
        rfd_kill(srv, slot);
        rfd_pass("ai_queue3_jubbly");
    }
    rfd_free_npc(srv, slot);

    if( npc_jubbly_dead > 0 )
    {
        slot = rfd_spawn(srv, npc_jubbly_dead, RFD_JUBBLY_X, RFD_JUBBLY_Z, 0);
        if( slot >= 0 )
        {
            rfd_opnpc4(srv, npc_jubbly_dead, slot);
            rfd_pass("opnpc4_jubbly_pluck");
        }
        rfd_free_npc(srv, slot);
    }

    loc_slot = rfd_place_loc(srv, loc_spit, RFD_SPIT_X, RFD_SPIT_Z, 0);
    if( loc_spit >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_ogre", RFD_OGRE_CHOMPY);
        rfd_oploc(srv, loc_spit, loc_slot);
        rfd_pass("oploc1_spit_no_meat");
        if( obj_jubbly_raw > 0 )
            rfd_give(player, obj_jubbly_raw, 1);
        rfd_oploc(srv, loc_spit, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_ogre") == RFD_OGRE_COOKED,
                       "spit must write jubbly_cooked");
        rfd_pass("oploc1_spit_cook");
    }

    loc_slot = rfd_place_loc(srv, loc_skrach, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_skrach >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_ogre", RFD_OGRE_COOKED);
        rfd_use_loc(srv, loc_skrach, loc_slot, obj_stew);
        rfd_pass("oplocu_skrach_wrong");
        if( obj_jubbly_cook > 0 )
            rfd_give(player, obj_jubbly_cook, 1);
        rfd_use_loc(srv, loc_skrach, loc_slot, obj_jubbly_cook);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_ogre") == RFD_OGRE_COMPLETE,
                       "jubbly must complete Skrach, got %d",
                       rfd_get_progress(player, "rfd_ogre"));
        rfd_pass("oplocu_skrach_jubbly");
    }
    rfd_journal(srv, "[proc,rfd_ogre_journal]", "journal_ogre_complete");

    /* Sir Amik / Cook / WOM / shrine / dragon */
    loc_slot = rfd_place_loc(srv, loc_amik, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_amik >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "crestquest", 0);
        rfd_oploc(srv, loc_amik, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_amikvarze") == 0,
                       "Amik prereq hard gate must block inspect");
        rfd_pass("oploc1_amik_prereq_fail");
        rfd_prereqs(srv, RFD_COOK_REQ);
        rfd_oploc(srv, loc_amik, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_amikvarze") == RFD_AMIK_INSPECTED,
                       "inspect Amik must write inspected");
        rfd_pass("oploc1_amik_inspect");
        rfd_oploc(srv, loc_amik, loc_slot);
        rfd_pass("oploc1_amik_inspect_reminder");
    }

    slot = rfd_spawn(srv, npc_cook, RFD_COOK_X, RFD_COOK_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_COMPLETE);
        rfd_set_progress(srv, "rfd_amikvarze", RFD_AMIK_INSPECTED);
        rfd_talk_finish(srv, npc_cook, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_amikvarze") == RFD_AMIK_COOK,
                       "Amik cook talk must write cook_talked");
        rfd_pass("opnpc1_amik_cook_talk");
        rfd_talk_finish(srv, npc_cook, slot);
        rfd_pass("opnpc1_amik_cook_reminder");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_wom, RFD_WOM_X, RFD_WOM_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_amikvarze", RFD_AMIK_COOK);
        rfd_talk_finish(srv, npc_wom, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_amikvarze") == RFD_AMIK_WOM,
                       "WOM talk must write wom_talked");
        rfd_pass("opnpc1_wom_talk");
        rfd_talk_finish(srv, npc_wom, slot);
        rfd_pass("opnpc1_wom_need_egg_token");
    }

    loc_slot = rfd_place_loc(srv, loc_shrine, RFD_SHRINE_X, RFD_SHRINE_Z, 0);
    if( loc_shrine >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_amikvarze", RFD_AMIK_WOM);
        if( obj_egg_evil > 0 )
            rfd_give(player, obj_egg_evil, 1);
        if( obj_chicken > 0 )
            rfd_give(player, obj_chicken, 1);
        rfd_use_loc(srv, loc_shrine, loc_slot, obj_chicken);
        rfd_pass("oplocu_shrine_already_egg");
        rfd_clear_inv(player);
        if( obj_chicken > 0 )
            rfd_give(player, obj_chicken, 1);
        rfd_use_loc(srv, loc_shrine, loc_slot, obj_chicken);
        rfd_pass("oplocu_shrine_summon");
    }

    slot = rfd_spawn(srv, npc_chicken, RFD_SHRINE_X + 1, RFD_SHRINE_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_amikvarze", RFD_AMIK_WOM);
        rfd_kill(srv, slot);
        rfd_pass("ai_queue3_evil_chicken");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_dragon, RFD_DRAGON_X, RFD_DRAGON_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_amikvarze", RFD_AMIK_WOM);
        rfd_kill(srv, slot);
        rfd_pass("ai_queue3_black_dragon_token");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_wom, RFD_WOM_X, RFD_WOM_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_amikvarze", RFD_AMIK_WOM);
        if( obj_egg_evil > 0 )
            rfd_give(player, obj_egg_evil, 1);
        if( obj_token > 0 )
            rfd_give(player, obj_token, 1);
        rfd_talk_finish(srv, npc_wom, slot);
        rfd_pass("opnpc1_wom_need_milk_cream");
        if( obj_milk > 0 )
            rfd_give(player, obj_milk, 1);
        if( obj_cream > 0 )
            rfd_give(player, obj_cream, 1);
        if( obj_egg_evil > 0 )
            rfd_give(player, obj_egg_evil, 1);
        if( obj_token > 0 )
            rfd_give(player, obj_token, 1);
        rfd_talk_finish(srv, npc_wom, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_amikvarze") == RFD_AMIK_BRULEE,
                       "WOM brulee must write brulee_made, got %d",
                       rfd_get_progress(player, "rfd_amikvarze"));
        rfd_pass("opnpc1_wom_make_brulee");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_amik, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_amik >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_amikvarze", RFD_AMIK_BRULEE);
        rfd_use_loc(srv, loc_amik, loc_slot, obj_stew);
        rfd_pass("oplocu_amik_wrong");
        if( obj_brulee > 0 )
            rfd_give(player, obj_brulee, 1);
        rfd_use_loc(srv, loc_amik, loc_slot, obj_brulee);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_amikvarze") == RFD_AMIK_COMPLETE,
                       "brulee must complete Amik, got %d",
                       rfd_get_progress(player, "rfd_amikvarze"));
        rfd_pass("oplocu_amik_brulee");
    }
    rfd_journal(srv, "[proc,rfd_amikvarze_journal]", "journal_amik_complete");

    /* King Awowogei */
    loc_slot = rfd_place_loc(srv, loc_monkey, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_monkey >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "mm_main", 0);
        rfd_oploc(srv, loc_monkey, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_monkey") == 0,
                       "MM1 hard gate must block monkey inspect");
        rfd_pass("oploc1_monkey_prereq_fail");
        rfd_prereqs(srv, RFD_COOK_REQ);
        rfd_oploc(srv, loc_monkey, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_monkey") == RFD_MONKEY_INSPECTED,
                       "inspect Awowogei must write inspected");
        rfd_pass("oploc1_monkey_inspect");
        rfd_oploc(srv, loc_monkey, loc_slot);
        rfd_pass("oploc1_monkey_inspect_reminder");
    }

    loc_slot = rfd_place_loc(srv, loc_throne, RFD_THRONE_X, RFD_THRONE_Z, 0);
    if( loc_throne >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_monkey", RFD_MONKEY_INSPECTED);
        rfd_oploc(srv, loc_throne, loc_slot);
        rfd_pass("oploc1_awowogei_no_greegree");
        rfd_wear_greegree(player);
        worn_set(player, TORIRSSERVER_WEAR_AMULET, -1, 0);
        rfd_oploc(srv, loc_throne, loc_slot);
        rfd_pass("oploc1_awowogei_no_amulet");
        rfd_wear_greegree(player);
        rfd_oploc(srv, loc_throne, loc_slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_monkey") == RFD_MONKEY_AWOW,
                       "Awowogei talk must write awowogei_talked");
        rfd_pass("oploc1_awowogei_talk");
        rfd_oploc(srv, loc_throne, loc_slot);
        rfd_pass("oploc1_awowogei_reminder");
    }

    slot = rfd_spawn(srv, npc_wise, RFD_WISE_X, RFD_WISE_Z, 0);
    if( slot >= 0 )
    {
        rfd_clear_inv(player);
        rfd_set_progress(srv, "rfd_monkey", RFD_MONKEY_INSPECTED);
        rfd_talk_finish(srv, npc_wise, slot);
        rfd_pass("opnpc1_wisemonkey_too_early");

        rfd_set_progress(srv, "rfd_monkey", RFD_MONKEY_AWOW);
        rfd_talk_finish(srv, npc_wise, slot);
        rfd_pass("opnpc1_wisemonkey_no_greegree");
        rfd_wear_greegree(player);
        worn_set(player, TORIRSSERVER_WEAR_AMULET, -1, 0);
        rfd_talk_finish(srv, npc_wise, slot);
        rfd_pass("opnpc1_wisemonkey_no_amulet");
        rfd_wear_greegree(player);
        rfd_talk_finish(srv, npc_wise, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_monkey") == RFD_MONKEY_WISE,
                       "Wise Monkeys recipe must write wisemonkeys_talked");
        rfd_pass("opnpc1_wisemonkey_recipe");
        rfd_talk_finish(srv, npc_wise, slot);
        rfd_pass("opnpc1_wisemonkey_need_ingredients");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_snake, RFD_SNAKE_X, RFD_SNAKE_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_monkey", RFD_MONKEY_WISE);
        rfd_kill(srv, slot);
        rfd_pass("ai_queue3_big_snake");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_wise, RFD_WISE_X, RFD_WISE_Z, 0);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_monkey", RFD_MONKEY_WISE);
        rfd_wear_greegree(player);
        if( obj_corpse > 0 )
            rfd_give(player, obj_corpse, 1);
        if( obj_banana > 0 )
            rfd_give(player, obj_banana, 1);
        if( obj_nuts > 0 )
            rfd_give(player, obj_nuts, 1);
        if( obj_rope > 0 )
            rfd_give(player, obj_rope, 1);
        if( obj_knife > 0 )
            rfd_give(player, obj_knife, 1);
        if( obj_pestle > 0 )
            rfd_give(player, obj_pestle, 1);
        rfd_talk_finish(srv, npc_wise, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_monkey") == RFD_MONKEY_COOKED,
                       "Wise Monkeys cook must write snake_cooked, got %d",
                       rfd_get_progress(player, "rfd_monkey"));
        rfd_pass("opnpc1_wisemonkey_cook");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_monkey, RFD_DOOR_X, RFD_DOOR_Z, 0);
    if( loc_monkey >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_monkey", RFD_MONKEY_COOKED);
        rfd_use_loc(srv, loc_monkey, loc_slot, obj_stew);
        rfd_pass("oplocu_monkey_wrong");
        if( obj_snake > 0 )
            rfd_give(player, obj_snake, 1);
        rfd_use_loc(srv, loc_monkey, loc_slot, obj_snake);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_monkey") == RFD_MONKEY_COMPLETE,
                       "stuffed snake must complete monkey, got %d",
                       rfd_get_progress(player, "rfd_monkey"));
        rfd_pass("oplocu_monkey_snake");
    }
    rfd_journal(srv, "[proc,rfd_monkey_journal]", "journal_monkey_complete");

    /* Finale + Agrith TK-grab */
    rfd_set_progress(srv, "rfd_evildave", RFD_DAVE_COMPLETE);
    rfd_set_progress(srv, "rfd_lumbridgeguide", RFD_GUIDE_COMPLETE);
    rfd_set_progress(srv, "rfd_goblins", RFD_GOBLIN_COMPLETE);
    rfd_set_progress(srv, "rfd_dwarf", RFD_DWARF_COMPLETE);
    rfd_set_progress(srv, "rfd_pirate", RFD_PIRATE_COMPLETE);
    rfd_set_progress(srv, "rfd_ogre", RFD_OGRE_COMPLETE);
    rfd_set_progress(srv, "rfd_amikvarze", RFD_AMIK_COMPLETE);
    rfd_set_progress(srv, "rfd_monkey", 0);
    loc_slot = rfd_place_loc(srv, loc_portal, RFD_PORTAL_X, RFD_PORTAL_Z, 0);
    if( loc_portal >= 0 && loc_slot >= 0 )
    {
        rfd_oploc(srv, loc_portal, loc_slot);
        rfd_pass("oploc1_portal_need_guests");
        rfd_set_progress(srv, "rfd_monkey", RFD_MONKEY_COMPLETE);
        rfd_set_progress(srv, "deserttreasure", 0);
        rfd_oploc(srv, loc_portal, loc_slot);
        rfd_pass("oploc1_portal_need_prereqs");
        rfd_prereqs(srv, RFD_COOK_REQ);
        rfd_oploc(srv, loc_portal, loc_slot);
        rfd_pass("oploc1_portal_enter");
    }

    slot = rfd_spawn(srv, npc_agrith, RFD_ARENA_X, RFD_ARENA_Z, RFD_ARENA_LEVEL);
    if( slot >= 0 )
    {
        int before_x = player->x;
        int before_z = player->z;
        ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,rfd_agrith_tk_grab]", slot);
        rfd_finish(srv);
        SELFTEST_CHECK(player->x != before_x || player->z != before_z ||
                           player->x == srv->npcs[slot].x,
                       "Agrith TK-grab must queue a player p_teleport");
        rfd_pass("ai_opplayer2_agrith_tk_grab");
        rfd_set_progress(srv, "rfd_finale", 0);
        rfd_kill(srv, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_finale") == RFD_FINALE_AGRITH,
                       "Agrith death must write agrith_dead, got %d",
                       rfd_get_progress(player, "rfd_finale"));
        rfd_pass("ai_queue3_agrith");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_flambeed, RFD_ARENA_X, RFD_ARENA_Z, RFD_ARENA_LEVEL);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_finale", RFD_FINALE_AGRITH);
        rfd_kill(srv, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_finale") == RFD_FINALE_FLAMBEED,
                       "Flambeed death must write flambeed_dead");
        rfd_pass("ai_queue3_flambeed");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_karamel, RFD_ARENA_X, RFD_ARENA_Z, RFD_ARENA_LEVEL);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_finale", RFD_FINALE_FLAMBEED);
        rfd_kill(srv, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_finale") == RFD_FINALE_KARAMEL,
                       "Karamel death must write karamel_dead");
        rfd_pass("ai_queue3_karamel");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_dessourt, RFD_ARENA_X, RFD_ARENA_Z, RFD_ARENA_LEVEL);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_finale", RFD_FINALE_KARAMEL);
        rfd_kill(srv, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_finale") == RFD_FINALE_DESSOURT,
                       "Dessourt death must write dessourt_dead");
        rfd_pass("ai_queue3_dessourt");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_mother, RFD_ARENA_X, RFD_ARENA_Z, RFD_ARENA_LEVEL);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_finale", RFD_FINALE_DESSOURT);
        rfd_kill(srv, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_finale") == RFD_FINALE_MOTHER,
                       "Mother death must write mother_dead");
        rfd_pass("ai_queue3_gelatinnoth");
    }
    rfd_free_npc(srv, slot);

    slot = rfd_spawn(srv, npc_cullin, RFD_ARENA_X, RFD_ARENA_Z, RFD_ARENA_LEVEL);
    if( slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_finale", RFD_FINALE_MOTHER);
        rfd_kill(srv, slot);
        SELFTEST_CHECK(rfd_get_progress(player, "rfd_finale") == RFD_FINALE_COMPLETE,
                       "Culinaromancer death must complete finale, got %d",
                       rfd_get_progress(player, "rfd_finale"));
        rfd_pass("ai_queue3_culinaromancer");
    }
    rfd_free_npc(srv, slot);

    loc_slot = rfd_place_loc(srv, loc_portal, RFD_PORTAL_X, RFD_PORTAL_Z, 0);
    if( loc_portal >= 0 && loc_slot >= 0 )
    {
        rfd_set_progress(srv, "rfd_finale", RFD_FINALE_COMPLETE);
        rfd_oploc(srv, loc_portal, loc_slot);
        rfd_pass("oploc1_portal_quiet");
    }
    rfd_journal(srv, "[proc,rfd_finale_journal]", "journal_finale_complete");
    rfd_journal(srv, "[proc,rfd_overview_journal]", "journal_overview_done");

    rfd_clear_inv(player);
    rfd_reset_quest(srv);
    fprintf(stderr,
            "ToriRSServer rfd selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_RECIPEFORDISASTER_SELFTEST_U_H */
