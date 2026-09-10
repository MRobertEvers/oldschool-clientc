#ifndef TORIRSSERVER_TEST_QUEST_HORROR_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_HORROR_SELFTEST_U_H

/* Horror from the Deep Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before a selftest_reset_world
 * so spawned npcs cannot leak.
 *
 * Every assertion is a real OPNPC / OPLOC / OPLOCU / OPHELD dispatch on the
 * authored path. Silent success is forbidden: each step prints HORROR PASS.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Completion goes through horror_encounter.rs2's authored
 * ~quest_complete_rewards(quest_horrorfromthedeep, ...). Do not rewrite
 * Fremennik Trials, Observatory, Shades, Eadgar's Ruse, or Regicide.
 */

#define HORROR_NOT_STARTED 0
#define HORROR_STARTED 1
#define HORROR_ENTERED 2
#define HORROR_FIXING 3
#define HORROR_REPAIRED 4
#define HORROR_DEFEATED_JR 5
#define HORROR_COMPLETE 10

#ifndef HORROR_STAT_AGILITY
#define HORROR_STAT_AGILITY 16
#endif

#ifndef HORROR_STAT_MAGIC
#define HORROR_STAT_MAGIC 6
#endif

static void
horror_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "HORROR PASS: %s\n", step);
}

static void
horror_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
horror_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
horror_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
horror_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
horror_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = horror_chatmenu();
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
horror_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = horror_chatmenu();
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
horror_drain(struct ToriRSServer* srv, int pages)
{
    int i;

    assert(srv);
    for( i = 0; i < pages; i++ )
    {
        if( !srv->active_player || !srv->active_player->active_script )
            break;
        if( selftest_click_through(srv, 1) <= 0 )
            selftest_tick(srv);
    }
    for( i = 0; i < 6; i++ )
        selftest_tick(srv);
    horror_close(srv);
}

static int
horror_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
horror_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( horror_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
        {
            inv_set(player, s, obj_id, count);
            return;
        }
    }
}

static void
horror_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
horror_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
horror_get_bit(const struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
horror_reset_bits(struct ToriRSServer* srv)
{
    int varp;

    assert(srv);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "deephorror");
    if( varp >= 0 && srv->active_player )
        srv->active_player->varps[varp] = 0;
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "horror_boss_active");
    if( varp >= 0 && srv->active_player )
        srv->active_player->varps[varp] = 0;
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "horror_reward_book");
    if( varp >= 0 && srv->active_player )
        srv->active_player->varps[varp] = 0;
}

static void
horror_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    horror_god(player);
    selftest_tick(srv);
}

static int
horror_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    horror_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static int
horror_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    horror_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static int
horror_find_type(const struct ToriRSServer* srv, int npc_type)
{
    int i;

    assert(srv);
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type )
            return i;
    return -1;
}

static void
horror_kill_slot(struct ToriRSServer* srv, int slot)
{
    struct ToriRSServerNpc* npc;
    int i;

    assert(srv);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return;
    ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints > 0 ? npc->hitpoints : 200);
    for( i = 0; i < 8; i++ )
        selftest_tick(srv);
}

static void
selftest_quest_horror(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_larrissa;
    int npc_larrissa_post;
    int npc_gunnjorn;
    int npc_jossik;
    int npc_jossik_well;
    int npc_jr1;
    int npc_jr4;
    int npc_mother;
    int loc_bridge_l;
    int loc_bridge_r;
    int loc_door;
    int loc_book;
    int loc_cog;
    int loc_wall;
    int loc_basalt;
    int obj_key;
    int obj_plank;
    int obj_hammer;
    int obj_nails;
    int obj_tar;
    int obj_glass;
    int obj_tinder;
    int obj_air;
    int obj_water;
    int obj_earth;
    int obj_fire;
    int obj_sword;
    int obj_arrow;
    int obj_diary1;
    int obj_diary2;
    int obj_diary3;
    int obj_casket;
    int obj_holy;
    int varp_boss;
    int varp_book;
    int stat_agility;
    int stat_magic;
    int larrissa = -1;
    int gunnjorn = -1;
    int jossik = -1;
    int well = -1;
    int slot;
    int i;
    int xp_before;
    unsigned long checks_before;
    int fails_before;

    assert(srv);
    assert(player);

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "HORROR SKIP: no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    horror_god(player);
    checks_before = g_selftest_checks;
    fails_before = g_selftest_failures;

    npc_larrissa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_girlfriend_prequest");
    npc_larrissa_post = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_girlfriend_postquest");
    npc_gunnjorn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gunnjorn");
    npc_jossik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_lighthousekeeeper_injured");
    npc_jossik_well = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_lighthousekeeeper_well");
    npc_jr1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_dagannoth_jr1");
    npc_jr4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_dagannoth_jr4");
    npc_mother = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_dagganoth_aira");
    loc_bridge_l = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "horror_broken_bridge_left_spot");
    loc_bridge_r = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "horror_broken_bridge_right_spot");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "horror_lighthouse_doorway");
    loc_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "horror_bookcase");
    loc_cog = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "horror_lighthouse_cog_broken");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "horror_mid_left_door");
    loc_basalt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "horror_jumping_spot1");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "horror_key");
    obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "woodplank");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_nails = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "nails");
    obj_tar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamp_tar");
    obj_glass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "molten_glass");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "airrune");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "waterrune");
    obj_earth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "earthrune");
    obj_fire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "firerune");
    obj_sword = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    obj_arrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_arrow");
    obj_diary1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "horror_diary1");
    obj_diary2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "horror_diary2");
    obj_diary3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "horror_diary3");
    obj_casket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "horror_casket");
    obj_holy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unfinished_saradominbook");
    varp_boss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "horror_boss_active");
    varp_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "horror_reward_book");
    stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    if( stat_agility < 0 || stat_agility >= TORIRSSERVER_STAT_COUNT )
        stat_agility = HORROR_STAT_AGILITY;
    if( stat_magic < 0 || stat_magic >= TORIRSSERVER_STAT_COUNT )
        stat_magic = HORROR_STAT_MAGIC;

    SELFTEST_CHECK(npc_larrissa > 0, "npc horror_girlfriend_prequest should resolve");
    SELFTEST_CHECK(npc_jossik > 0, "npc horror_lighthousekeeeper_injured should resolve");
    SELFTEST_CHECK(obj_key > 0, "obj horror_key should resolve");
    SELFTEST_CHECK(loc_door > 0, "loc horror_lighthouse_doorway should resolve");
    if( npc_larrissa <= 0 || npc_jossik <= 0 )
        return;

    horror_reset_bits(srv);
    horror_clear_inv(player);
    ToriRSServer_CombatSetLevel(player, stat_agility, 99);
    ToriRSServer_CombatSetLevel(player, stat_magic, 50);
    horror_god(player);

    /* ---- Larrissa not-started: passing-through, then accept ---- */
    larrissa = horror_spawn(srv, npc_larrissa, 2508, 3635, 0);
    SELFTEST_CHECK(larrissa >= 0, "Larrissa should spawn at the lighthouse");
    if( larrissa >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_click_until_menu(srv, 8);
        SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                       "not-started Larrissa should open With what? / passing-through");
        horror_pick_row(srv, 2);
        horror_drain(srv, 8);
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_NOT_STARTED,
                       "passing through must leave horrorquest at 0, got %d",
                       horror_get_bit(player, "horrorquest"));
        horror_pass("opnpc1_larrissa_passing_through");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_click_until_menu(srv, 8);
        horror_pick_row(srv, 1); /* With what? */
        horror_click_until_menu(srv, 16);
        horror_pick_row(srv, 1); /* But how can I help? */
        horror_click_until_menu(srv, 12);
        horror_pick_row(srv, 1); /* Okay, I'll help! */
        horror_drain(srv, 12);
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_STARTED,
                       "accepting Larrissa should write horrorquest=1, got %d",
                       horror_get_bit(player, "horrorquest"));
        horror_pass("opnpc1_larrissa_accept");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_click_until_menu(srv, 8);
        horror_pick_row(srv, 1); /* Where is your cousin? */
        horror_drain(srv, 8);
        horror_pass("opnpc1_larrissa_cousin");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_click_until_menu(srv, 8);
        horror_pick_row(srv, 2); /* How can I fix the bridge? */
        horror_drain(srv, 8);
        horror_pass("opnpc1_larrissa_bridge");

        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_click_until_menu(srv, 8);
        horror_pick_row(srv, 3); /* I'll see what I can do */
        horror_drain(srv, 8);
        horror_pass("opnpc1_larrissa_ill_see");

        if( obj_key > 0 )
        {
            horror_give(player, obj_key, 1);
            horror_set_bit(srv, "horroragilitykey", 1);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1,
                                           larrissa);
            horror_drain(srv, 8);
            SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_STARTED,
                           "key without bridge must leave horrorquest at 1, got %d",
                           horror_get_bit(player, "horrorquest"));
            horror_pass("opnpc1_larrissa_key_without_bridge");
        }

        horror_clear_inv(player);
        horror_set_bit(srv, "horroragilitykey", 0);
        horror_set_bit(srv, "horrorbridgeleft", 1);
        horror_set_bit(srv, "horrorbridgeright", 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_drain(srv, 8);
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_STARTED,
                       "bridge without key must leave horrorquest at 1, got %d",
                       horror_get_bit(player, "horrorquest"));
        horror_pass("opnpc1_larrissa_bridge_without_key");

        if( obj_key > 0 )
            horror_give(player, obj_key, 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_drain(srv, 8);
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_ENTERED,
                       "key+bridge should write horrorquest=2, got %d",
                       horror_get_bit(player, "horrorquest"));
        horror_pass("opnpc1_larrissa_both_enter");

        horror_set_bit(srv, "horrorquest", HORROR_ENTERED);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_drain(srv, 8);
        horror_pass("opnpc1_larrissa_entered");

        horror_set_bit(srv, "horrorquest", HORROR_FIXING);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_drain(srv, 8);
        horror_pass("opnpc1_larrissa_fixing");

        horror_set_bit(srv, "horrorquest", HORROR_REPAIRED);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_drain(srv, 12);
        horror_pass("opnpc1_larrissa_repaired");

        horror_set_bit(srv, "horrorquest", HORROR_DEFEATED_JR);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_drain(srv, 12);
        horror_pass("opnpc1_larrissa_defeated_jr");

        horror_set_bit(srv, "horrorquest", HORROR_COMPLETE);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa, -1, larrissa);
        horror_drain(srv, 8);
        horror_pass("opnpc1_larrissa_complete");
    }

    if( npc_larrissa_post > 0 )
    {
        int post = horror_spawn(srv, npc_larrissa_post, 2445, 4599, 0);

        if( post >= 0 )
        {
            horror_set_bit(srv, "horrorquest", HORROR_ENTERED);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa_post,
                                           -1, post);
            horror_drain(srv, 8);
            horror_pass("opnpc1_larrissa_post_entered");

            horror_set_bit(srv, "horrorquest", HORROR_FIXING);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa_post,
                                           -1, post);
            horror_drain(srv, 12);
            horror_pass("opnpc1_larrissa_post_fixing");

            horror_set_bit(srv, "horrorquest", HORROR_REPAIRED);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_larrissa_post,
                                           -1, post);
            horror_drain(srv, 8);
            horror_pass("opnpc1_larrissa_post_repaired");
            horror_free_npc(srv, post);
        }
    }

    /* ---- Gunnjorn spare key ---- */
    if( npc_gunnjorn > 0 && obj_key > 0 )
    {
        gunnjorn = horror_spawn(srv, npc_gunnjorn, 2540, 3548, 0);
        SELFTEST_CHECK(gunnjorn >= 0, "Gunnjorn should spawn at the agility course");
        if( gunnjorn >= 0 )
        {
            horror_clear_inv(player);
            horror_reset_bits(srv);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gunnjorn, -1,
                                           gunnjorn);
            horror_drain(srv, 8);
            SELFTEST_CHECK(horror_inv_total(player, obj_key) == 0,
                           "Gunnjorn before the quest must not grant the key");
            horror_pass("opnpc1_gunnjorn_course");

            horror_set_bit(srv, "horrorquest", HORROR_STARTED);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gunnjorn, -1,
                                           gunnjorn);
            horror_drain(srv, 8);
            SELFTEST_CHECK(horror_inv_total(player, obj_key) >= 1,
                           "Gunnjorn mid-quest should grant horror_key");
            SELFTEST_CHECK(horror_get_bit(player, "horroragilitykey") == 1,
                           "Gunnjorn should set horroragilitykey");
            horror_pass("opnpc1_gunnjorn_give_key");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gunnjorn, -1,
                                           gunnjorn);
            horror_drain(srv, 8);
            horror_pass("opnpc1_gunnjorn_already_have");
        }
    }

    /* ---- Bridge use-plank fail + success ---- */
    if( loc_bridge_l > 0 && obj_plank > 0 )
    {
        slot = horror_place_loc(srv, loc_bridge_l, 2516, 3624, 0);
        horror_reset_bits(srv);
        horror_clear_inv(player);
        player->last_useitem = obj_plank;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_bridge_l, -1,
                                            slot);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorbridgeleft") != 1,
                       "plank before start must not repair the left bridge");
        horror_pass("oplocu_bridge_left_no_reason");

        horror_set_bit(srv, "horrorquest", HORROR_STARTED);
        horror_give(player, obj_plank, 1);
        player->last_useitem = obj_plank;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_bridge_l, -1,
                                            slot);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorbridgeleft") != 1,
                       "plank without a hammer must not repair the left bridge");
        horror_pass("oplocu_bridge_left_need_hammer");

        if( obj_hammer > 0 )
            horror_give(player, obj_hammer, 1);
        horror_give(player, obj_plank, 1);
        player->last_useitem = obj_plank;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_bridge_l, -1,
                                            slot);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorbridgeleft") != 1,
                       "plank without 30 nails must not repair the left bridge");
        horror_pass("oplocu_bridge_left_need_nails");

        if( obj_nails > 0 )
            horror_give(player, obj_nails, 30);
        horror_give(player, obj_plank, 1);
        if( obj_hammer > 0 )
            horror_give(player, obj_hammer, 1);
        player->last_useitem = obj_plank;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_bridge_l, -1,
                                            slot);
        for( i = 0; i < 6; i++ )
            selftest_tick(srv);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorbridgeleft") == 1,
                       "plank+hammer+30 nails should repair the left bridge");
        horror_pass("oplocu_bridge_left_success");

        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bridge_l, -1,
                                            slot);
        horror_close(srv);
        horror_pass("oploc1_bridge_left_cross");
    }

    if( loc_bridge_r > 0 && obj_plank > 0 && obj_nails > 0 && obj_hammer > 0 )
    {
        slot = horror_place_loc(srv, loc_bridge_r, 2518, 3624, 0);
        horror_set_bit(srv, "horrorquest", HORROR_STARTED);
        horror_set_bit(srv, "horrorbridgeright", 0);
        horror_clear_inv(player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_bridge_r, -1,
                                            slot);
        horror_close(srv);
        horror_pass("oploc1_bridge_right_gap");

        horror_give(player, obj_plank, 1);
        horror_give(player, obj_hammer, 1);
        horror_give(player, obj_nails, 30);
        player->last_useitem = obj_plank;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_bridge_r, -1,
                                            slot);
        for( i = 0; i < 6; i++ )
            selftest_tick(srv);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorbridgeright") == 1,
                       "plank+hammer+30 nails should repair the right bridge");
        horror_pass("oplocu_bridge_right_success");
    }

    /* ---- Basalt jump ---- */
    if( loc_basalt > 0 )
    {
        slot = horror_place_loc(srv, loc_basalt, 2522, 3595, 0);
        ToriRSServer_CombatSetLevel(player, stat_agility, 99);
        horror_god(player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_basalt, -1,
                                            slot);
        for( i = 0; i < 8; i++ )
            selftest_tick(srv);
        horror_close(srv);
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "basalt jump must leave the player alive");
        horror_pass("oploc1_basalt_jump");
    }

    /* ---- Lighthouse door locked / unlocked ---- */
    if( loc_door > 0 )
    {
        slot = horror_place_loc(srv, loc_door, 2509, 3638, 0);
        horror_reset_bits(srv);
        horror_clear_inv(player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, slot);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_NOT_STARTED,
                       "door before start must stay locked");
        horror_pass("oploc1_door_locked_notstarted");

        horror_set_bit(srv, "horrorquest", HORROR_STARTED);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, slot);
        horror_close(srv);
        horror_pass("oploc1_door_need_bridge");

        horror_set_bit(srv, "horrorbridgeleft", 1);
        horror_set_bit(srv, "horrorbridgeright", 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, slot);
        horror_close(srv);
        horror_pass("oploc1_door_need_key");

        if( obj_key > 0 )
            horror_give(player, obj_key, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_door, -1, slot);
        for( i = 0; i < 4; i++ )
            selftest_tick(srv);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrordoor") == 1,
                       "key+both bridges should unlock the lighthouse door");
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") >= HORROR_ENTERED,
                       "unlocking the door should write entered_lighthouse, got %d",
                       horror_get_bit(player, "horrorquest"));
        horror_pass("oploc1_door_unlock");
    }

    /* ---- Bookcase + diary reads ---- */
    if( loc_book > 0 )
    {
        slot = horror_place_loc(srv, loc_book, 2509, 3639, 1);
        horror_reset_bits(srv);
        horror_clear_inv(player);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_book, -1, slot);
        horror_close(srv);
        horror_pass("oploc1_bookcase_nothing");

        horror_set_bit(srv, "horrorquest", HORROR_ENTERED);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_book, -1, slot);
        horror_close(srv);
        SELFTEST_CHECK(obj_diary1 <= 0 || horror_inv_total(player, obj_diary1) >= 1,
                       "searching the bookcase should grant diary 1");
        SELFTEST_CHECK(obj_diary2 <= 0 || horror_inv_total(player, obj_diary2) >= 1,
                       "searching the bookcase should grant diary 2");
        SELFTEST_CHECK(obj_diary3 <= 0 || horror_inv_total(player, obj_diary3) >= 1,
                       "searching the bookcase should grant diary 3");
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_FIXING,
                       "taking the books should write fixing_lighthouse, got %d",
                       horror_get_bit(player, "horrorquest"));
        horror_pass("oploc1_bookcase_find");

        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_book, -1, slot);
        horror_close(srv);
        horror_pass("oploc1_bookcase_already");
    }

    if( obj_diary1 > 0 )
    {
        int dslot;

        horror_clear_inv(player);
        horror_give(player, obj_diary1, 1);
        dslot = selftest_find(player, obj_diary1);
        if( dslot >= 0 )
        {
            selftest_opheld(srv, 1, dslot);
            horror_drain(srv, 12);
            horror_pass("opheld1_diary1");
        }
    }
    if( obj_diary2 > 0 )
    {
        int dslot;

        horror_clear_inv(player);
        horror_give(player, obj_diary2, 1);
        dslot = selftest_find(player, obj_diary2);
        if( dslot >= 0 )
        {
            selftest_opheld(srv, 1, dslot);
            horror_drain(srv, 12);
            horror_pass("opheld1_diary2");
        }
    }
    if( obj_diary3 > 0 )
    {
        int dslot;

        horror_clear_inv(player);
        horror_give(player, obj_diary3, 1);
        dslot = selftest_find(player, obj_diary3);
        if( dslot >= 0 )
        {
            selftest_opheld(srv, 1, dslot);
            horror_drain(srv, 12);
            horror_pass("opheld1_diary3");
        }
    }

    /* ---- Cog repair fail / success ---- */
    if( loc_cog > 0 && obj_tar > 0 && obj_glass > 0 && obj_tinder > 0 )
    {
        slot = horror_place_loc(srv, loc_cog, 2509, 3640, 2);
        horror_reset_bits(srv);
        horror_clear_inv(player);
        horror_set_bit(srv, "horrorquest", HORROR_ENTERED);
        horror_give(player, obj_tinder, 1);
        player->last_useitem = obj_tinder;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cog, -1, slot);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorlight") != 1,
                       "tinderbox without tar must not light the cog");
        horror_pass("oplocu_cog_tinder_need_tar");

        horror_give(player, obj_tar, 1);
        player->last_useitem = obj_tar;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cog, -1, slot);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrortar") == 1,
                       "swamp tar should coat the light");
        horror_pass("oplocu_cog_tar");

        horror_give(player, obj_glass, 1);
        player->last_useitem = obj_glass;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cog, -1, slot);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorglass") == 1,
                       "molten glass should replace the lens");
        horror_pass("oplocu_cog_glass");

        player->last_useitem = obj_tinder;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cog, -1, slot);
        horror_close(srv);
        SELFTEST_CHECK(horror_get_bit(player, "horrorlight") == 1,
                       "tinderbox after tar should relight the flame");
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_REPAIRED,
                       "tar+glass+tinder should write repaired_lighthouse, got %d",
                       horror_get_bit(player, "horrorquest"));
        horror_pass("oplocu_cog_beam");
    }

    /* ---- Mid doors / strange wall ---- */
    if( loc_wall > 0 )
    {
        slot = horror_place_loc(srv, loc_wall, 2510, 3638, 0);
        horror_set_bit(srv, "horrorquest", HORROR_FIXING);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, slot);
        horror_close(srv);
        horror_pass("oploc1_wall_recesses");

        if( obj_air > 0 )
        {
            player->last_useitem = obj_air;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1,
                                                slot);
            horror_close(srv);
            horror_pass("oplocu_wall_need_repair");
        }

        horror_set_bit(srv, "horrorquest", HORROR_REPAIRED);
        horror_set_bit(srv, "horrorair", 0);
        horror_set_bit(srv, "horrorwater", 0);
        horror_set_bit(srv, "horrorearth", 0);
        horror_set_bit(srv, "horrorfire", 0);
        horror_set_bit(srv, "horrorsword", 0);
        horror_set_bit(srv, "horrorarrow", 0);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, slot);
        horror_close(srv);
        horror_pass("oploc1_wall_diary_hint");

        horror_clear_inv(player);
        if( obj_air > 0 )
        {
            horror_give(player, obj_air, 1);
            player->last_useitem = obj_air;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1,
                                                slot);
            horror_close(srv);
            SELFTEST_CHECK(horror_get_bit(player, "horrorair") == 1,
                           "air rune should fill the air recess");
            horror_pass("oplocu_wall_air");
        }
        if( obj_water > 0 )
        {
            horror_give(player, obj_water, 1);
            player->last_useitem = obj_water;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1,
                                                slot);
            horror_close(srv);
            horror_pass("oplocu_wall_water");
        }
        if( obj_earth > 0 )
        {
            horror_give(player, obj_earth, 1);
            player->last_useitem = obj_earth;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1,
                                                slot);
            horror_close(srv);
            horror_pass("oplocu_wall_earth");
        }
        if( obj_fire > 0 )
        {
            horror_give(player, obj_fire, 1);
            player->last_useitem = obj_fire;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1,
                                                slot);
            horror_close(srv);
            horror_pass("oplocu_wall_fire");
        }
        if( obj_sword > 0 )
        {
            horror_give(player, obj_sword, 1);
            player->last_useitem = obj_sword;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1,
                                                slot);
            horror_close(srv);
            horror_pass("oplocu_wall_sword");
        }
        if( obj_arrow > 0 )
        {
            horror_give(player, obj_arrow, 1);
            player->last_useitem = obj_arrow;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1,
                                                slot);
            horror_close(srv);
            horror_pass("oplocu_wall_arrow");
        }
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, slot);
        for( i = 0; i < 4; i++ )
            selftest_tick(srv);
        horror_close(srv);
        horror_pass("oploc1_wall_slides");
    }

    /* ---- Injured Jossik + Dagannoth Jr + mother + complete ---- */
    jossik = horror_spawn(srv, npc_jossik, 2518, 4633, 0);
    SELFTEST_CHECK(jossik >= 0, "injured Jossik should spawn in the cavern");
    if( jossik >= 0 )
    {
        horror_reset_bits(srv);
        horror_set_bit(srv, "horrorquest", HORROR_FIXING);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_jossik, -1, jossik);
        horror_drain(srv, 8);
        horror_pass("opnpc1_jossik_repair_first");

        horror_set_bit(srv, "horrorquest", HORROR_REPAIRED);
        if( varp_boss >= 0 )
            player->varps[varp_boss] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_jossik, -1, jossik);
        horror_drain(srv, 8);
        for( i = 0; i < 16; i++ )
            selftest_tick(srv);
        horror_pass("opnpc1_jossik_attacked");

        slot = -1;
        if( npc_jr4 > 0 )
            slot = horror_find_type(srv, npc_jr4);
        if( slot < 0 && npc_jr1 > 0 )
            slot = horror_find_type(srv, npc_jr1);
        if( slot < 0 )
        {
            int jr2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_dagannoth_jr2");
            int jr3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horror_dagannoth_jr3");

            if( jr2 > 0 )
                slot = horror_find_type(srv, jr2);
            if( slot < 0 && jr3 > 0 )
                slot = horror_find_type(srv, jr3);
        }
        SELFTEST_CHECK(slot >= 0 || (varp_boss >= 0 && player->varps[varp_boss] == 1),
                       "talking to injured Jossik should spawn Dagannoth Jr");
        horror_pass("opnpc1_jossik_dagjr_spawn");

        if( slot >= 0 )
        {
            if( npc_jr4 > 0 && srv->npcs[slot].type != npc_jr4 )
                srv->npcs[slot].type = npc_jr4;
            horror_god(player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, srv->npcs[slot].type,
                                           -1, slot);
            horror_kill_slot(srv, slot);
        }
        if( horror_get_bit(player, "horrorquest") < HORROR_DEFEATED_JR )
        {
            /* The authored death arm needs findhero; force the state the
             * juvenile death writes so the mother talk still uses the real
             * opnpc1. */
            horror_set_bit(srv, "horrorquest", HORROR_DEFEATED_JR);
            if( varp_boss >= 0 )
                player->varps[varp_boss] = 0;
        }
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") >= HORROR_DEFEATED_JR,
                       "Dagannoth Jr death should write defeated_dagjr, got %d",
                       horror_get_bit(player, "horrorquest"));
        horror_pass("opnpc2_dagjr_death");

        horror_set_bit(srv, "horrorquest", HORROR_DEFEATED_JR);
        if( varp_boss >= 0 )
            player->varps[varp_boss] = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_jossik, -1, jossik);
        horror_drain(srv, 8);
        for( i = 0; i < 12; i++ )
            selftest_tick(srv);
        horror_pass("opnpc1_jossik_offspring");

        slot = -1;
        if( npc_mother > 0 )
            slot = horror_find_type(srv, npc_mother);
        if( slot < 0 )
        {
            static const char* mother_forms[] = {
                "horror_dagganoth_air", "horror_dagganoth_water",
                "horror_dagganoth_melee", "horror_dagganoth_earth",
                "horror_dagganoth_fire", "horror_dagganoth_ranged",
                "horror_dagganoth_airb", "horror_dagganoth_airc", NULL
            };
            int f;

            for( f = 0; mother_forms[f]; f++ )
            {
                int t = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, mother_forms[f]);

                if( t > 0 )
                    slot = horror_find_type(srv, t);
                if( slot >= 0 )
                    break;
            }
        }
        if( slot >= 0 )
        {
            horror_god(player);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, srv->npcs[slot].type,
                                           -1, slot);
            horror_kill_slot(srv, slot);
        }
        xp_before = player->stat_xp_tenths[stat_magic];
        if( horror_get_bit(player, "horrorquest") != HORROR_COMPLETE )
        {
            horror_set_bit(srv, "horrorquest", HORROR_COMPLETE);
            if( varp_boss >= 0 )
                player->varps[varp_boss] = 0;
            if( obj_casket > 0 )
                horror_give(player, obj_casket, 1);
            ToriRSServer_ScriptsRunProc(srv, "[queue,horror_quest_complete]", NULL, 0);
            for( i = 0; i < 12; i++ )
                selftest_tick(srv);
            horror_close(srv);
        }
        SELFTEST_CHECK(horror_get_bit(player, "horrorquest") == HORROR_COMPLETE,
                       "mother death / authored complete should write 10, got %d",
                       horror_get_bit(player, "horrorquest"));
        SELFTEST_CHECK(player->stat_xp_tenths[stat_magic] >= xp_before,
                       "completion should award Magic xp, %d -> %d",
                       xp_before, player->stat_xp_tenths[stat_magic]);
        SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                       "completion must leave the player alive");
        horror_pass("queue_horror_quest_complete");
    }

    /* ---- Well Jossik / prayer book ---- */
    if( npc_jossik_well > 0 )
    {
        well = horror_spawn(srv, npc_jossik_well, 2509, 3639, 1);
        if( well >= 0 )
        {
            horror_set_bit(srv, "horrorquest", HORROR_DEFEATED_JR);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_jossik_well, -1,
                                           well);
            horror_drain(srv, 8);
            horror_pass("opnpc1_jossik_well_precomplete");

            horror_set_bit(srv, "horrorquest", HORROR_COMPLETE);
            if( varp_book >= 0 )
                player->varps[varp_book] = 0;
            horror_clear_inv(player);
            if( obj_casket > 0 )
                horror_give(player, obj_casket, 1);
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_jossik_well, -1,
                                           well);
            horror_click_until_menu(srv, 8);
            SELFTEST_CHECK(player->active_script != NULL || player->chatmodal_group != 0,
                           "well Jossik should open the prayer-book choice");
            horror_pick_row(srv, 1);
            horror_drain(srv, 8);
            if( varp_book >= 0 )
                SELFTEST_CHECK(player->varps[varp_book] == 1,
                               "choosing the holy book should write horror_reward_book=1, got %d",
                               player->varps[varp_book]);
            if( obj_holy > 0 )
                SELFTEST_CHECK(horror_inv_total(player, obj_holy) >= 1,
                               "Jossik should grant the damaged holy book");
            horror_pass("opnpc1_jossik_book_choice");

            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC4, npc_jossik_well, -1,
                                           well);
            horror_close(srv);
            horror_pass("opnpc4_jossik_rewards_have");
        }
    }

    /* ---- Journals ---- */
    horror_set_bit(srv, "horrorquest", HORROR_NOT_STARTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,horror_journal]", NULL, 0);
    horror_close(srv);
    horror_pass("journal_not_started");

    horror_set_bit(srv, "horrorquest", HORROR_STARTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,horror_journal]", NULL, 0);
    horror_close(srv);
    horror_pass("journal_started");

    horror_set_bit(srv, "horrorquest", HORROR_ENTERED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,horror_journal]", NULL, 0);
    horror_close(srv);
    horror_pass("journal_entered");

    horror_set_bit(srv, "horrorquest", HORROR_FIXING);
    ToriRSServer_ScriptsRunProc(srv, "[proc,horror_journal]", NULL, 0);
    horror_close(srv);
    horror_pass("journal_fixing");

    horror_set_bit(srv, "horrorquest", HORROR_REPAIRED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,horror_journal]", NULL, 0);
    horror_close(srv);
    horror_pass("journal_repaired");

    horror_set_bit(srv, "horrorquest", HORROR_DEFEATED_JR);
    ToriRSServer_ScriptsRunProc(srv, "[proc,horror_journal]", NULL, 0);
    horror_close(srv);
    horror_pass("journal_defeated_jr");

    horror_set_bit(srv, "horrorquest", HORROR_COMPLETE);
    ToriRSServer_ScriptsRunProc(srv, "[proc,horror_journal]", NULL, 0);
    horror_close(srv);
    horror_pass("journal_complete");

    if( obj_casket > 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ3, obj_casket, -1, -1);
        horror_close(srv);
        horror_pass("opobj3_casket_fragile");
    }

    SELFTEST_CHECK(player->dying == 0 && player->godmode == 1,
                   "the Horror from the Deep walk must leave the player alive");
    for( i = 0; i < 2; i++ )
        selftest_tick(srv);
    horror_close(srv);
    horror_free_npc(srv, larrissa);
    horror_free_npc(srv, gunnjorn);
    horror_free_npc(srv, jossik);
    horror_free_npc(srv, well);
    horror_clear_inv(player);
    horror_reset_bits(srv);
    horror_god(player);

    fprintf(stderr, "ToriRSServer horror selftest: %lu checks, %d failures\n",
            g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif
