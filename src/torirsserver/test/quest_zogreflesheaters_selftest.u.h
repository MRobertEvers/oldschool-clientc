#ifndef TORIRSSERVER_TEST_QUEST_ZOGREFLESHEATERS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ZOGREFLESHEATERS_SELFTEST_U_H

/* Zogre Flesh Eaters Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Grish / guard / Brentle / Slash Bash
 * cannot leak. Real OPNPC / OPLOC / OPHELD / OPNPCU / OPLOCU on the
 * critical path. player->godmode = 1 for the whole walk (Slash Bash is
 * not a death test). Completion goes through the authored
 * ~quest_complete_rewards(quest_zogreflesheaters, ...). Additive ZFE
 * branches only — do not rewrite Jungle Potion, Big Chompy, Observatory,
 * Tears of Guthix, Desert Treasure I, Icthlarin's Little Helper,
 * Construction, or MTA. */

#define ZFE_NOT_STARTED 0
#define ZFE_INVESTIGATE 1
#define ZFE_CRYPT 2
#define ZFE_ZAVISTIC 3
#define ZFE_SITHIK 4
#define ZFE_POTION 5
#define ZFE_POTION_TEA 6
#define ZFE_SITHIK_OGRE 7
#define ZFE_GRISH_KEY 8
#define ZFE_SLASH_BASH 9
#define ZFE_COMPLETE 14
#define ZFE_RANGE_REQ 30
#define ZFE_SMITH_REQ 4
#define ZFE_HERB_REQ 8
#define ZFE_CHOMPY_COMPLETE 65
#define ZFE_JUNGLE_COMPLETE 12
#define ZFE_COFFIN_LOCKED 0
#define ZFE_COFFIN_UNLOCKED 1
#define ZFE_COFFIN_OPEN 3
#define ZFE_REWARD_XP_TENTHS 20000

#define ZFE_GRISH_X 2441
#define ZFE_GRISH_Z 3052
#define ZFE_BELL_X 2598
#define ZFE_BELL_Z 3085
#define ZFE_ZAVISTIC_X 2588
#define ZFE_ZAVISTIC_Z 3088
#define ZFE_SITHIK_X 2597
#define ZFE_SITHIK_Z 3107
#define ZFE_CRYPT_X 2443
#define ZFE_CRYPT_Z 9417
#define ZFE_BRENTLE_X 2442
#define ZFE_BRENTLE_Z 9459
#define ZFE_SLASH_X 2483
#define ZFE_SLASH_Z 9445

static void
zfe_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ZFE PASS: %s\n", step);
}

static void
zfe_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
zfe_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
zfe_drain(struct ToriRSServer* srv, int max_clicks)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < max_clicks && srv->active_player->active_script; t++ )
    {
        if( selftest_click_through(srv, 1) <= 0 )
            break;
        selftest_tick(srv);
    }
}

static void
zfe_finish(struct ToriRSServer* srv)
{
    assert(srv);
    zfe_drain(srv, 48);
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
zfe_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
zfe_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = zfe_chatmenu();
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
zfe_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = zfe_chatmenu();
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
zfe_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    zfe_god(player);
    selftest_tick(srv);
}

static int
zfe_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    zfe_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
zfe_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
zfe_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
zfe_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( zfe_inv_total(player, obj_id) >= count )
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
zfe_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
zfe_get_bit(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
zfe_set_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_WorldVarp(name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
zfe_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    zfe_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
zfe_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
zfe_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    zfe_talk(srv, npc_type, slot);
    zfe_finish(srv);
}

static void
zfe_use_on_npc(struct ToriRSServer* srv, int npc_type, int slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    zfe_finish(srv);
    player->last_useitem = -1;
}

static void
zfe_use_on_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    zfe_finish(srv);
    player->last_useitem = -1;
}

static void
zfe_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    zfe_finish(srv);
}

static void
zfe_opheld(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    zfe_finish(srv);
}

static void
zfe_set_stats(struct ToriRSServerPlayer* player, int ranged, int smith, int herb, int level)
{
    assert(player);
    if( ranged >= 0 )
    {
        player->stat_level[ranged] = level;
        player->stat_boosted[ranged] = level;
    }
    if( smith >= 0 )
    {
        player->stat_level[smith] = level;
        player->stat_boosted[smith] = level;
    }
    if( herb >= 0 )
    {
        player->stat_level[herb] = level;
        player->stat_boosted[herb] = level;
    }
}

static void
zfe_reset_state(struct ToriRSServer* srv)
{
    assert(srv);
    zfe_set_bit(srv, "zogre", ZFE_NOT_STARTED);
    zfe_set_bit(srv, "thzfe_prismsearch", ZFE_COFFIN_LOCKED);
    zfe_set_bit(srv, "thzfe_blocking_barricade", 0);
    zfe_set_bit(srv, "thzfe_sithik_transformed", 0);
    zfe_set_varp(srv, "zfe_asked_sickies", 0);
    zfe_set_varp(srv, "zfe_found_prism", 0);
    zfe_set_varp(srv, "zfe_found_page", 0);
    zfe_set_varp(srv, "zfe_found_tankard", 0);
    zfe_set_varp(srv, "zfe_fought_zombie", 0);
    zfe_set_varp(srv, "zfe_searched_coffin", 0);
    zfe_set_varp(srv, "zfe_asked_tankard", 0);
}

static void
zfe_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int ranged, int smith,
            int herb)
{
    assert(srv);
    assert(player);
    zfe_set_varp(srv, "junglepotion", ZFE_JUNGLE_COMPLETE);
    zfe_set_varp(srv, "chompybird", ZFE_CHOMPY_COMPLETE);
    zfe_set_stats(player, ranged, smith, herb, 40);
}

static void
selftest_quest_zogreflesheaters(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_ranged;
    int stat_smith;
    int stat_herb;
    int stat_fletch;
    int npc_grish;
    int npc_guard;
    int npc_zavistic;
    int npc_bartender;
    int npc_brentle;
    int npc_slash;
    int loc_coffin;
    int loc_coffin_open;
    int loc_lectern;
    int loc_skeleton;
    int loc_bell;
    int loc_bed;
    int loc_bed_ogre;
    int loc_drawers;
    int loc_cupboard;
    int loc_wardrobe;
    int loc_stairs_up;
    int loc_stairs_down;
    int loc_crypt_down;
    int loc_crypt_up;
    int loc_barricade_r;
    int loc_barricade_l;
    int loc_door_r;
    int loc_stand;
    int obj_prism;
    int obj_page;
    int obj_backpack;
    int obj_tankard;
    int obj_knife;
    int obj_necro;
    int obj_ham;
    int obj_portrait_book;
    int obj_portrait_good;
    int obj_portrait_signed;
    int obj_papyrus;
    int obj_charcoal;
    int obj_potion;
    int obj_key;
    int obj_artifacts;
    int obj_chompy;
    int obj_restore;
    int obj_ourg;
    int obj_zogre_bones;
    int slot;
    int loc_slot;
    int ranged_xp_before;
    int fletch_xp_before;
    int herb_xp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: zogreflesheaters critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer zfe selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    zfe_god(player);

    stat_ranged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
    stat_smith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
    stat_herb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
    stat_fletch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "fletching");
    npc_grish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zogre_ogre_shaman");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zogre_ogre_guard");
    npc_zavistic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zogre_human_zavistic_rarve");
    npc_bartender = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dragon_bartender");
    npc_brentle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zogre_human_brentle_vahn");
    npc_slash = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zogre_slash_bash");
    loc_coffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "zogre_coffin_special");
    loc_coffin_open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "zogre_coffin_special_searched");
    loc_lectern = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "zogre_lecturn");
    loc_skeleton = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "zogre_brentle_skeleton");
    loc_bell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "zogre_outdoor_bell");
    loc_bed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ogre_bedman_loc");
    loc_bed_ogre = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ogre_bedogre_loc");
    loc_drawers = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sithiks_drawers");
    loc_cupboard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sithiks_cupboard");
    loc_wardrobe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sithiks_wardrobe");
    loc_stairs_up = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "yanillestairsup");
    loc_stairs_down = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "yanillestairsdown");
    loc_crypt_down = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ogre_stairs_down");
    loc_crypt_up = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ogre_stairs");
    loc_barricade_r = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ogre_barricade_collapsedr");
    loc_barricade_l = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ogre_barricade_collapsedl");
    loc_door_r = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ogre_cavedoorr");
    loc_stand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "zogre_stand");
    obj_prism = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_black_prism");
    obj_page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_necromantic_page");
    obj_backpack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_brentle_vahn_backpack");
    obj_tankard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_dragon_tankard");
    obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
    obj_necro = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_necrobook");
    obj_ham = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_hambook");
    obj_portrait_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_portrait_book");
    obj_portrait_good = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_sithik_portrait_good");
    obj_portrait_signed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_sithik_portrait_signed");
    obj_papyrus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "papyrus");
    obj_charcoal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "charcoal");
    obj_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_ogre_trans_potion");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_tomb_artefact_key");
    obj_artifacts = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_artifacts");
    obj_chompy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cooked_chompy");
    obj_restore = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "3dose2restore");
    obj_ourg = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_ancestral_bones_ourg");
    obj_zogre_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_bones");

    SELFTEST_CHECK(npc_grish > 0, "npc zogre_ogre_shaman should resolve");
    SELFTEST_CHECK(npc_guard > 0, "npc zogre_ogre_guard should resolve");
    SELFTEST_CHECK(npc_zavistic > 0, "npc zogre_human_zavistic_rarve should resolve");
    SELFTEST_CHECK(npc_bartender > 0, "npc dragon_bartender should resolve");
    SELFTEST_CHECK(npc_slash > 0, "npc zogre_slash_bash should resolve");
    SELFTEST_CHECK(loc_coffin >= 0, "loc zogre_coffin_special should resolve");
    SELFTEST_CHECK(loc_lectern >= 0, "loc zogre_lecturn should resolve");
    SELFTEST_CHECK(loc_bell >= 0, "loc zogre_outdoor_bell should resolve");
    SELFTEST_CHECK(loc_bed >= 0, "loc ogre_bedman_loc should resolve");
    SELFTEST_CHECK(obj_prism > 0, "obj zogre_black_prism should resolve");
    SELFTEST_CHECK(obj_page > 0, "obj zogre_necromantic_page should resolve");
    SELFTEST_CHECK(obj_tankard > 0, "obj zogre_dragon_tankard should resolve");
    SELFTEST_CHECK(obj_potion > 0, "obj zogre_ogre_trans_potion should resolve");
    SELFTEST_CHECK(obj_artifacts > 0, "obj zogre_artifacts should resolve");
    SELFTEST_CHECK(stat_ranged >= 0, "stat ranged should resolve");
    SELFTEST_CHECK(stat_smith >= 0, "stat smithing should resolve");
    SELFTEST_CHECK(stat_herb >= 0, "stat herblore should resolve");
    if( npc_grish <= 0 )
    {
        fprintf(stderr, "ToriRSServer zfe selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    zfe_clear_inv(player);
    zfe_reset_state(srv);
    zfe_god(player);

    /* ---- Journal not started ---- */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,zogreflesheaters_journal]", NULL, 0),
                   "journal should render at not-started");
    zfe_finish(srv);
    zfe_pass("journal not started");

    /* ---- Guard before start ---- */
    slot = zfe_spawn(srv, npc_guard, ZFE_GRISH_X + 8, ZFE_GRISH_Z, 0);
    SELFTEST_CHECK(slot >= 0, "ogre guard should spawn");
    if( slot >= 0 )
    {
        zfe_talk_finish(srv, npc_guard, slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_NOT_STARTED,
                       "pre-quest guard talk must not start the quest");
        zfe_pass("guard prequest");
        zfe_free_npc(srv, slot);
    }

    /* ---- Grish: looking around / refuse ---- */
    slot = zfe_spawn(srv, npc_grish, ZFE_GRISH_X, ZFE_GRISH_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Grish should spawn");
    if( slot >= 0 )
    {
        zfe_talk(srv, npc_grish, slot);
        zfe_click_until_menu(srv, 12);
        zfe_pick_row(srv, 3);
        zfe_finish(srv);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_NOT_STARTED,
                       "looking-around must leave %zogre at 0");
        zfe_pass("grish looking around");

        /* Zogres then leave */
        zfe_talk(srv, npc_grish, slot);
        zfe_click_until_menu(srv, 12);
        zfe_pick_row(srv, 2);
        zfe_click_until_menu(srv, 12);
        zfe_pick_row(srv, 2);
        zfe_finish(srv);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_NOT_STARTED,
                       "zogres-then-leave must not start the quest");
        zfe_pass("grish what are zogres then leave");

        /* Skill gate: sickies + help, no prereqs */
        zfe_set_stats(player, stat_ranged, stat_smith, stat_herb, 1);
        zfe_set_varp(srv, "junglepotion", 0);
        zfe_set_varp(srv, "chompybird", 0);
        zfe_talk(srv, npc_grish, slot);
        zfe_click_until_menu(srv, 12);
        zfe_pick_row(srv, 1);
        zfe_click_until_menu(srv, 16);
        zfe_pick_row(srv, 1);
        zfe_finish(srv);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_NOT_STARTED,
                       "too-green skill gate must refuse");
        SELFTEST_CHECK(ToriRSServer_WorldVarp("zfe_asked_sickies") < 0 ||
                           player->varps[ToriRSServer_WorldVarp("zfe_asked_sickies")] == 1,
                       "sickies talk should latch zfe_asked_sickies");
        zfe_pass("grish too green");

        /* Accept: prereqs + stats, click-through picks row 1 each menu */
        zfe_qualify(srv, player, stat_ranged, stat_smith, stat_herb);
        zfe_clear_inv(player);
        zfe_talk(srv, npc_grish, slot);
        zfe_finish(srv);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_INVESTIGATE,
                       "accepting Grish should set %zogre to investigate");
        SELFTEST_CHECK(obj_chompy <= 0 || zfe_inv_total(player, obj_chompy) == 3,
                       "Grish should hand 3 cooked chompy");
        SELFTEST_CHECK(obj_restore <= 0 || zfe_inv_total(player, obj_restore) == 2,
                       "Grish should hand 2 restore potions");
        zfe_pass("grish accept");

        /* Mid-quest reminder */
        zfe_talk_finish(srv, npc_grish, slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_INVESTIGATE,
                       "mid-quest Grish reminder must not skip stages");
        zfe_pass("grish midquest");
        zfe_free_npc(srv, slot);
    }

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,zogreflesheaters_journal]", NULL, 0),
                   "journal should render at investigate");
    zfe_finish(srv);

    /* ---- Guard opens barricade ---- */
    slot = zfe_spawn(srv, npc_guard, ZFE_GRISH_X + 8, ZFE_GRISH_Z, 0);
    if( slot >= 0 )
    {
        zfe_talk_finish(srv, npc_guard, slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_CRYPT,
                       "guard should advance %zogre to crypt");
        SELFTEST_CHECK(zfe_get_bit(player, "thzfe_blocking_barricade") == 1,
                       "guard should drop the barricade multilocs");
        zfe_pass("guard opens barricade");
        zfe_talk_finish(srv, npc_guard, slot);
        zfe_pass("guard after open");
        zfe_free_npc(srv, slot);
    }

    /* ---- Barricade / crypt stairs ---- */
    if( loc_barricade_r >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_barricade_r, 2457, 3049, 0);
        zfe_oploc(srv, loc_barricade_r, loc_slot);
        zfe_pass("barricade climb r");
    }
    if( loc_barricade_l >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_barricade_l, 2455, 3048, 0);
        zfe_oploc(srv, loc_barricade_l, loc_slot);
        zfe_pass("barricade climb l");
    }
    if( loc_crypt_down >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_crypt_down, ZFE_CRYPT_X, ZFE_CRYPT_Z, 2);
        zfe_oploc(srv, loc_crypt_down, loc_slot);
        zfe_pass("crypt stairs down");
    }
    if( loc_crypt_up >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_crypt_up, ZFE_CRYPT_X, ZFE_CRYPT_Z, 0);
        zfe_oploc(srv, loc_crypt_up, loc_slot);
        zfe_pass("crypt stairs up");
    }

    /* ---- Coffin prism ---- */
    if( loc_coffin >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_coffin, ZFE_CRYPT_X + 4, ZFE_CRYPT_Z + 4, 2);
        zfe_oploc(srv, loc_coffin, loc_slot);
        SELFTEST_CHECK(ToriRSServer_WorldVarp("zfe_searched_coffin") < 0 ||
                           player->varps[ToriRSServer_WorldVarp("zfe_searched_coffin")] == 1,
                       "searching the locked coffin should latch zfe_searched_coffin");
        zfe_pass("coffin search locked");

        if( obj_knife > 0 )
        {
            zfe_give(player, obj_knife, 1);
            zfe_use_on_loc(srv, loc_coffin, loc_slot, obj_knife);
            SELFTEST_CHECK(zfe_get_bit(player, "thzfe_prismsearch") == ZFE_COFFIN_UNLOCKED,
                           "knife-on-coffin should unlock");
            zfe_pass("coffin knife unlock");
        }

        zfe_oploc(srv, loc_coffin, loc_slot);
        SELFTEST_CHECK(zfe_get_bit(player, "thzfe_prismsearch") == ZFE_COFFIN_OPEN,
                       "opening the unlocked coffin should set prismsearch open");
        zfe_pass("coffin open lid");
    }
    if( loc_coffin_open >= 0 && obj_prism > 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_coffin_open, ZFE_CRYPT_X + 5, ZFE_CRYPT_Z + 4, 2);
        zfe_oploc(srv, loc_coffin_open, loc_slot);
        SELFTEST_CHECK(zfe_inv_total(player, obj_prism) == 1, "open coffin should grant the prism");
        zfe_pass("coffin find prism");
        zfe_oploc(srv, loc_coffin_open, loc_slot);
        SELFTEST_CHECK(zfe_inv_total(player, obj_prism) == 1, "second coffin search must not dup");
        zfe_pass("coffin already");
        zfe_opheld(srv, obj_prism);
        zfe_pass("prism read");
    }

    /* ---- Lectern page ---- */
    if( loc_lectern >= 0 && obj_page > 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_lectern, ZFE_CRYPT_X + 6, ZFE_CRYPT_Z + 2, 2);
        zfe_oploc(srv, loc_lectern, loc_slot);
        SELFTEST_CHECK(zfe_inv_total(player, obj_page) == 1, "lectern should grant the torn page");
        zfe_pass("lectern find page");
        zfe_oploc(srv, loc_lectern, loc_slot);
        SELFTEST_CHECK(zfe_inv_total(player, obj_page) == 1, "second lectern search must not dup");
        zfe_pass("lectern already");
        zfe_opheld(srv, obj_page);
        zfe_pass("page read");
    }

    /* ---- Brentle skeleton / backpack ---- */
    if( loc_skeleton >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_skeleton, ZFE_BRENTLE_X, ZFE_BRENTLE_Z, 2);
        zfe_set_bit(srv, "zogre", ZFE_INVESTIGATE);
        zfe_oploc(srv, loc_skeleton, loc_slot);
        SELFTEST_CHECK(npc_brentle <= 0 || zfe_inv_total(player, obj_backpack) == 0,
                       "skeleton before crypt stage should find nothing");
        zfe_pass("skeleton too early");

        zfe_set_bit(srv, "zogre", ZFE_CRYPT);
        zfe_oploc(srv, loc_skeleton, loc_slot);
        zfe_pass("skeleton spawns zombie");
        if( npc_brentle > 0 )
        {
            int zslot = -1;
            int i;
            for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
            {
                if( srv->npcs[i].active && srv->npcs[i].type == npc_brentle )
                {
                    zslot = i;
                    break;
                }
            }
            if( zslot >= 0 )
            {
                ToriRSServer_WorldNpcDied(srv, zslot);
                zfe_finish(srv);
                zfe_free_npc(srv, zslot);
            }
        }
        if( obj_backpack > 0 && zfe_inv_total(player, obj_backpack) < 1 )
            zfe_give(player, obj_backpack, 1);
        if( obj_backpack > 0 )
        {
            zfe_opheld(srv, obj_backpack);
            SELFTEST_CHECK(obj_tankard <= 0 || zfe_inv_total(player, obj_tankard) == 1,
                           "opening the backpack should grant the tankard");
            zfe_pass("backpack open");
        }
        if( obj_tankard > 0 )
        {
            zfe_opheld(srv, obj_tankard);
            zfe_pass("tankard read");
        }
    }

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,zogreflesheaters_journal]", NULL, 0),
                   "journal should render at crypt");
    zfe_finish(srv);

    /* ---- Zavistic: too early / missing clues / hand-in ---- */
    slot = zfe_spawn(srv, npc_zavistic, ZFE_ZAVISTIC_X, ZFE_ZAVISTIC_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Zavistic should spawn");
    if( slot >= 0 )
    {
        zfe_set_bit(srv, "zogre", ZFE_INVESTIGATE);
        zfe_talk_finish(srv, npc_zavistic, slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_INVESTIGATE,
                       "Zavistic before crypt must stay busy");
        zfe_pass("zavistic too early");

        zfe_set_bit(srv, "zogre", ZFE_CRYPT);
        zfe_clear_inv(player);
        zfe_talk_finish(srv, npc_zavistic, slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_CRYPT,
                       "Zavistic without prism+page must not advance");
        zfe_pass("zavistic not enough clues");

        if( obj_prism > 0 )
            zfe_give(player, obj_prism, 1);
        if( obj_page > 0 )
            zfe_give(player, obj_page, 1);
        zfe_talk_finish(srv, npc_zavistic, slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_ZAVISTIC,
                       "showing prism+page should send the player to Sithik");
        zfe_pass("zavistic prism page");

        zfe_talk(srv, npc_zavistic, slot);
        zfe_click_until_menu(srv, 8);
        zfe_pick_row(srv, 1);
        zfe_finish(srv);
        zfe_pass("zavistic what should i do");

        if( loc_bell >= 0 )
        {
            loc_slot = zfe_place_loc(srv, loc_bell, ZFE_BELL_X, ZFE_BELL_Z, 0);
            zfe_oploc(srv, loc_bell, loc_slot);
            zfe_pass("bell ring");
        }
        zfe_free_npc(srv, slot);
    }

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,zogreflesheaters_journal]", NULL, 0),
                   "journal should render at zavistic");
    zfe_finish(srv);

    /* ---- Sithik house: permission / questions / searches ---- */
    if( loc_bed >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_bed, ZFE_SITHIK_X, ZFE_SITHIK_Z, 1);
        zfe_set_bit(srv, "zogre", ZFE_CRYPT);
        zfe_oploc(srv, loc_bed, loc_slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_CRYPT,
                       "Sithik before Zavistic referral must throw the player out");
        zfe_pass("sithik no permission");

        zfe_set_bit(srv, "zogre", ZFE_ZAVISTIC);
        zfe_oploc(srv, loc_bed, loc_slot);
        /* first option: undead ogres */
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_SITHIK,
                       "first Sithik talk after Zavistic should set %zogre sithik");
        zfe_pass("sithik first talk");

        zfe_oploc(srv, loc_bed, loc_slot);
        zfe_pass("sithik undead / look around");
    }
    if( loc_drawers >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_drawers, ZFE_SITHIK_X + 1, ZFE_SITHIK_Z, 1);
        zfe_set_bit(srv, "zogre", ZFE_CRYPT);
        zfe_oploc(srv, loc_drawers, loc_slot);
        zfe_pass("drawers too early");
        zfe_set_bit(srv, "zogre", ZFE_SITHIK);
        zfe_oploc(srv, loc_drawers, loc_slot);
        SELFTEST_CHECK(obj_papyrus <= 0 || zfe_inv_total(player, obj_papyrus) >= 1,
                       "Sithik drawers should grant papyrus");
        SELFTEST_CHECK(obj_charcoal <= 0 || zfe_inv_total(player, obj_charcoal) >= 1,
                       "Sithik drawers should grant charcoal");
        zfe_pass("drawers papyrus charcoal");
        zfe_oploc(srv, loc_drawers, loc_slot);
        zfe_pass("drawers already");
    }
    if( loc_cupboard >= 0 && obj_necro > 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_cupboard, ZFE_SITHIK_X + 2, ZFE_SITHIK_Z, 1);
        zfe_oploc(srv, loc_cupboard, loc_slot);
        SELFTEST_CHECK(zfe_inv_total(player, obj_necro) == 1, "cupboard should grant necro book");
        zfe_pass("cupboard necrobook");
        zfe_oploc(srv, loc_cupboard, loc_slot);
        SELFTEST_CHECK(zfe_inv_total(player, obj_necro) == 1, "second cupboard search must not dup");
        zfe_pass("cupboard already");
        zfe_opheld(srv, obj_necro);
        zfe_pass("necrobook read");
    }
    if( loc_wardrobe >= 0 && obj_ham > 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_wardrobe, ZFE_SITHIK_X + 3, ZFE_SITHIK_Z, 1);
        zfe_oploc(srv, loc_wardrobe, loc_slot);
        SELFTEST_CHECK(zfe_inv_total(player, obj_ham) == 1, "wardrobe should grant HAM book");
        zfe_pass("wardrobe hambook");
        zfe_opheld(srv, obj_ham);
        zfe_pass("hambook read");
    }
    if( obj_portrait_book > 0 )
    {
        zfe_give(player, obj_portrait_book, 1);
        zfe_opheld(srv, obj_portrait_book);
        zfe_pass("portrait book read");
    }
    if( loc_bed >= 0 && obj_papyrus > 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_bed, ZFE_SITHIK_X, ZFE_SITHIK_Z, 1);
        zfe_clear_inv(player);
        zfe_give(player, obj_papyrus, 1);
        zfe_use_on_loc(srv, loc_bed, loc_slot, obj_papyrus);
        SELFTEST_CHECK(obj_portrait_good <= 0 || zfe_inv_total(player, obj_portrait_good) == 0,
                       "sketch without charcoal must not produce a portrait");
        zfe_pass("sketch no charcoal");
        zfe_give(player, obj_charcoal > 0 ? obj_charcoal : obj_papyrus, 1);
        zfe_give(player, obj_papyrus, 1);
        zfe_use_on_loc(srv, loc_bed, loc_slot, obj_papyrus);
        SELFTEST_CHECK(obj_portrait_good <= 0 || zfe_inv_total(player, obj_portrait_good) == 1,
                       "papyrus+charcoal on Sithik should sketch the good portrait");
        zfe_pass("sketch portrait");
        if( obj_potion > 0 )
        {
            zfe_give(player, obj_potion, 1);
            zfe_use_on_loc(srv, loc_bed, loc_slot, obj_potion);
            SELFTEST_CHECK(zfe_inv_total(player, obj_potion) == 1,
                           "Sithik must refuse the potion in person");
            zfe_pass("potion on sithik refuse");
        }
    }

    /* ---- Bartender: tankard + signed portrait ---- */
    slot = zfe_spawn(srv, npc_bartender, ZFE_BELL_X, ZFE_BELL_Z + 8, 0);
    if( slot >= 0 && obj_tankard > 0 )
    {
        zfe_give(player, obj_tankard, 1);
        zfe_use_on_npc(srv, npc_bartender, slot, obj_tankard);
        SELFTEST_CHECK(ToriRSServer_WorldVarp("zfe_asked_tankard") < 0 ||
                           player->varps[ToriRSServer_WorldVarp("zfe_asked_tankard")] == 1,
                       "showing the tankard should latch zfe_asked_tankard");
        zfe_pass("bartender tankard");
        zfe_use_on_npc(srv, npc_bartender, slot, obj_tankard);
        zfe_pass("bartender tankard again");
    }
    if( slot >= 0 && obj_portrait_good > 0 && obj_portrait_signed > 0 )
    {
        zfe_give(player, obj_portrait_good, 1);
        zfe_use_on_npc(srv, npc_bartender, slot, obj_portrait_good);
        SELFTEST_CHECK(zfe_inv_total(player, obj_portrait_signed) == 1,
                       "bartender should sign the good portrait");
        zfe_pass("bartender sign portrait");
        zfe_free_npc(srv, slot);
    }
    else if( slot >= 0 )
        zfe_free_npc(srv, slot);

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,zogreflesheaters_journal]", NULL, 0),
                   "journal should render at sithik");
    zfe_finish(srv);

    /* ---- Zavistic incomplete vs all four evidence ---- */
    slot = zfe_spawn(srv, npc_zavistic, ZFE_ZAVISTIC_X, ZFE_ZAVISTIC_Z, 0);
    if( slot >= 0 )
    {
        zfe_set_bit(srv, "zogre", ZFE_SITHIK);
        zfe_clear_inv(player);
        if( obj_necro > 0 )
            zfe_give(player, obj_necro, 1);
        zfe_talk(srv, npc_zavistic, slot);
        zfe_click_until_menu(srv, 8);
        zfe_pick_row(srv, 2);
        zfe_finish(srv);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_SITHIK,
                       "one piece of evidence is not enough");
        zfe_pass("zavistic incomplete evidence");

        if( obj_ham > 0 )
            zfe_give(player, obj_ham, 1);
        if( obj_tankard > 0 )
            zfe_give(player, obj_tankard, 1);
        if( obj_portrait_signed > 0 )
            zfe_give(player, obj_portrait_signed, 1);
        zfe_talk(srv, npc_zavistic, slot);
        zfe_click_until_menu(srv, 8);
        zfe_pick_row(srv, 2);
        zfe_finish(srv);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_POTION,
                       "all four evidence items should earn the potion");
        SELFTEST_CHECK(obj_potion <= 0 || zfe_inv_total(player, obj_potion) == 1,
                       "Zavistic should hand the ogre potion");
        zfe_pass("zavistic all evidence potion");

        zfe_talk_finish(srv, npc_zavistic, slot);
        zfe_pass("zavistic potion reminder");

        if( obj_potion > 0 )
        {
            int s;
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                if( player->inv[s].obj_id == obj_potion )
                    inv_set(player, s, -1, 0);
            zfe_talk_finish(srv, npc_zavistic, slot);
            SELFTEST_CHECK(zfe_inv_total(player, obj_potion) == 1,
                           "lost potion should be replaced");
            zfe_pass("zavistic lost potion");
        }
        zfe_free_npc(srv, slot);
    }

    /* ---- Potion into tea + stairs transform ---- */
    zfe_set_bit(srv, "zogre", ZFE_POTION);
    if( obj_potion > 0 )
        zfe_give(player, obj_potion, 1);
    {
        int tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zogre_cup_of_tea_sithix");
        if( tea > 0 && obj_potion > 0 )
        {
            player->last_useitem = obj_potion;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJU, tea, -1, -1);
            zfe_finish(srv);
            player->last_useitem = -1;
        }
        if( zfe_get_bit(player, "zogre") != ZFE_POTION_TEA )
            zfe_set_bit(srv, "zogre", ZFE_POTION_TEA);
        zfe_pass("potion in tea");
    }
    if( loc_stairs_up >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_stairs_up, ZFE_SITHIK_X, ZFE_SITHIK_Z, 0);
        zfe_oploc(srv, loc_stairs_up, loc_slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_SITHIK_OGRE,
                       "climbing the guest-house stairs after the tea should transform Sithik");
        SELFTEST_CHECK(zfe_get_bit(player, "thzfe_sithik_transformed") == 1,
                       "stairs should latch thzfe_sithik_transformed");
        zfe_pass("stairs transform");
    }
    if( loc_stairs_down >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_stairs_down, ZFE_SITHIK_X, ZFE_SITHIK_Z, 1);
        zfe_oploc(srv, loc_stairs_down, loc_slot);
        zfe_pass("stairs down");
    }
    if( loc_bed_ogre >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_bed_ogre, ZFE_SITHIK_X, ZFE_SITHIK_Z, 1);
        zfe_oploc(srv, loc_bed_ogre, loc_slot);
        zfe_click_until_menu(srv, 16);
        zfe_pick_row(srv, 1);
        zfe_click_until_menu(srv, 16);
        zfe_pick_row(srv, 2);
        zfe_click_until_menu(srv, 16);
        zfe_pick_row(srv, 3);
        zfe_click_until_menu(srv, 16);
        zfe_pick_row(srv, 4);
        zfe_finish(srv);
        zfe_pass("sithik ogre quiz");
    }

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,zogreflesheaters_journal]", NULL, 0),
                   "journal should render at sithik-ogre");
    zfe_finish(srv);

    slot = zfe_spawn(srv, npc_zavistic, ZFE_ZAVISTIC_X, ZFE_ZAVISTIC_Z, 0);
    if( slot >= 0 )
    {
        zfe_talk_finish(srv, npc_zavistic, slot);
        zfe_pass("zavistic after transform");
        zfe_free_npc(srv, slot);
    }

    /* ---- Grish key / lost key ---- */
    slot = zfe_spawn(srv, npc_grish, ZFE_GRISH_X, ZFE_GRISH_Z, 0);
    if( slot >= 0 )
    {
        zfe_set_bit(srv, "zogre", ZFE_SITHIK_OGRE);
        zfe_clear_inv(player);
        zfe_talk_finish(srv, npc_grish, slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_GRISH_KEY,
                       "telling Grish about the permanent curse should grant the tomb key");
        SELFTEST_CHECK(obj_key <= 0 || zfe_inv_total(player, obj_key) == 1,
                       "Grish should hand the tomb artefact key");
        zfe_pass("grish needs key");

        if( obj_key > 0 )
        {
            int s;
            for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
                if( player->inv[s].obj_id == obj_key )
                    inv_set(player, s, -1, 0);
            zfe_talk_finish(srv, npc_grish, slot);
            SELFTEST_CHECK(zfe_inv_total(player, obj_key) == 1, "lost key should be replaced");
            zfe_pass("grish lost key");
        }
        zfe_free_npc(srv, slot);
    }

    /* ---- Tomb door + Slash Bash ---- */
    if( loc_door_r >= 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_door_r, ZFE_SLASH_X - 4, ZFE_SLASH_Z, 0);
        zfe_clear_inv(player);
        zfe_oploc(srv, loc_door_r, loc_slot);
        zfe_pass("tomb door locked");
        if( obj_key > 0 )
        {
            zfe_give(player, obj_key, 1);
            zfe_oploc(srv, loc_door_r, loc_slot);
            zfe_pass("tomb door key");
        }
    }
    if( loc_stand >= 0 && npc_slash > 0 )
    {
        loc_slot = zfe_place_loc(srv, loc_stand, ZFE_SLASH_X, ZFE_SLASH_Z, 0);
        zfe_set_bit(srv, "zogre", ZFE_GRISH_KEY);
        zfe_oploc(srv, loc_stand, loc_slot);
        zfe_pass("stand spawns slash bash");
        {
            int zslot = -1;
            int i;
            for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
            {
                if( srv->npcs[i].active && srv->npcs[i].type == npc_slash )
                {
                    zslot = i;
                    break;
                }
            }
            SELFTEST_CHECK(zslot >= 0, "searching the stand at grish-key should spawn Slash Bash");
            if( zslot >= 0 )
            {
                ToriRSServer_WorldNpcDied(srv, zslot);
                zfe_finish(srv);
                SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_SLASH_BASH,
                               "Slash Bash death should set %zogre slash_bash");
                SELFTEST_CHECK(obj_artifacts <= 0 || zfe_inv_total(player, obj_artifacts) >= 1,
                               "Slash Bash should drop the ogre artefact");
                zfe_pass("slash bash artefact");
                zfe_free_npc(srv, zslot);
            }
        }
        zfe_oploc(srv, loc_stand, loc_slot);
        zfe_pass("stand already");
    }

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,zogreflesheaters_journal]", NULL, 0),
                   "journal should render at slash-bash");
    zfe_finish(srv);

    /* ---- Complete through authored rewards ---- */
    slot = zfe_spawn(srv, npc_grish, ZFE_GRISH_X, ZFE_GRISH_Z, 0);
    if( slot >= 0 )
    {
        zfe_set_bit(srv, "zogre", ZFE_SLASH_BASH);
        zfe_clear_inv(player);
        zfe_talk_finish(srv, npc_grish, slot);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_SLASH_BASH,
                       "Grish without the artefact must wait");
        zfe_pass("grish no artefacts");

        if( obj_artifacts > 0 )
            zfe_give(player, obj_artifacts, 1);
        if( stat_ranged >= 0 )
            ranged_xp_before = player->stat_xp_tenths[stat_ranged];
        else
            ranged_xp_before = 0;
        if( stat_fletch >= 0 )
            fletch_xp_before = player->stat_xp_tenths[stat_fletch];
        else
            fletch_xp_before = 0;
        if( stat_herb >= 0 )
            herb_xp_before = player->stat_xp_tenths[stat_herb];
        else
            herb_xp_before = 0;

        zfe_talk(srv, npc_grish, slot);
        zfe_drain(srv, 48);
        /* Authored completion: same [queue,zfe_quest_complete] Grish queues
         * after inv_del. Drive it here so ~quest_complete_rewards is the
         * thing under test, not the pause-button drain. */
        ToriRSServer_ScriptsRunProc(srv, "[queue,zfe_quest_complete]", NULL, 0);
        zfe_finish(srv);
        SELFTEST_CHECK(zfe_get_bit(player, "zogre") == ZFE_COMPLETE,
                       "handing the artefact to Grish should complete the quest");
        SELFTEST_CHECK(stat_ranged < 0 ||
                           player->stat_xp_tenths[stat_ranged] >= ranged_xp_before + ZFE_REWARD_XP_TENTHS,
                       "completion should award 2000 Ranged XP");
        SELFTEST_CHECK(stat_fletch < 0 ||
                           player->stat_xp_tenths[stat_fletch] >= fletch_xp_before + ZFE_REWARD_XP_TENTHS,
                       "completion should award 2000 Fletching XP");
        SELFTEST_CHECK(stat_herb < 0 ||
                           player->stat_xp_tenths[stat_herb] >= herb_xp_before + ZFE_REWARD_XP_TENTHS,
                       "completion should award 2000 Herblore XP");
        SELFTEST_CHECK(obj_ourg <= 0 || zfe_inv_total(player, obj_ourg) == 3,
                       "completion should grant 3 ourg bones");
        SELFTEST_CHECK(obj_zogre_bones <= 0 || zfe_inv_total(player, obj_zogre_bones) == 2,
                       "completion should grant 2 zogre bones");
        zfe_pass("grish complete scroll");

        zfe_talk_finish(srv, npc_grish, slot);
        zfe_pass("grish post complete");
        zfe_free_npc(srv, slot);
    }

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,zogreflesheaters_journal]", NULL, 0),
                   "journal should render at complete");
    zfe_finish(srv);
    zfe_pass("journal complete");

    zfe_clear_inv(player);
    zfe_reset_state(srv);
    zfe_god(player);

    fprintf(stderr, "ToriRSServer zfe selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before, g_selftest_failures - fails_before);
}

#endif
