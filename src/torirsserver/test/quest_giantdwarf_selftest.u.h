#ifndef TORIRSSERVER_TEST_QUEST_GIANTDWARF_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_GIANTDWARF_SELFTEST_U_H

/* The Giant Dwarf Gate D C walk. Included from torirs_server_world_selftest.c
 * and invoked immediately before a selftest_reset_world so spawned Keldagrim
 * npcs cannot leak. Real OPNPC / OPLOC / OPHELDU / OPLOCU on the authored
 * path. player->godmode = 1 for the whole walk (no death test). Completion
 * goes through Veldaban's authored ~gdwarf_quest_complete /
 * ~quest_complete_rewards. Additive Giant Dwarf only — do not rewrite
 * Between a Rock boatman postquest, Reldo Two Cats, Thurgo Knight's Sword,
 * Observatory, Tears of Guthix, Zogre, Lost Tribe, Construction, or MTA. */

#define GDWARF_NOT_STARTED 0
#define GDWARF_ARRIVED 1
#define GDWARF_VELDABAN_DONE 2
#define GDWARF_BLASIDAR_DONE 3
#define GDWARF_VERMUNDI_ASKED 4
#define GDWARF_LIBRARIAN_ASKED 5
#define GDWARF_GOT_BOOK 6
#define GDWARF_SHOWED_BOOK 7
#define GDWARF_MACHINE_LOADED 8
#define GDWARF_MACHINE_STARTED 9
#define GDWARF_CLOTHES_DONE 10
#define GDWARF_SARO_ASKED 11
#define GDWARF_DROMUND_ASKED 12
#define GDWARF_LEFT_BOOT 13
#define GDWARF_BOOTS_DONE 14
#define GDWARF_SANTIRI_ASKED 15
#define GDWARF_SAPPHIRES_USED 16
#define GDWARF_IMCANDO_ASKED 17
#define GDWARF_RELDO_TOLD 18
#define GDWARF_AXE_DONE 19
#define GDWARF_ITEMS_GIVEN 20
#define GDWARF_BLASIDAR_AFTER 21
#define GDWARF_ENTERED_CONSORTIUM 22
#define GDWARF_SECRETARY_DONE 23
#define GDWARF_DIRECTOR_DONE 24
#define GDWARF_JOINED_COMPANY 25
#define GDWARF_DIRECTOR_AFTER 26
#define GDWARF_READY_TO_FINISH 28
#define GDWARF_COMPLETE 50

#define GDWARF_REQ_MAGIC 33
#define GDWARF_REQ_FIREMAKING 16
#define GDWARF_REQ_CRAFTING 12
#define GDWARF_REQ_THIEVING 14
#define GDWARF_SAPPHIRES_NEEDED 3
#define GDWARF_ORE_NEEDED 10
#define GDWARF_BAR_NEEDED 10
#define GDWARF_COMPANY_BLUE_OPAL 3
#define GDWARF_MODEL_CLOTHES 1
#define GDWARF_MODEL_BOOTS 3
#define GDWARF_MODEL_AXE 7
#define GDWARF_SQUIRE_GIVEN_PIE 3

#define GDWARF_REWARD_MINING 25000
#define GDWARF_REWARD_SMITHING 25000
#define GDWARF_REWARD_CRAFTING 25000
#define GDWARF_REWARD_MAGIC 15000
#define GDWARF_REWARD_THIEVING 15000
#define GDWARF_REWARD_FIREMAKING 15000

#define GDWARF_BOATMAN_X 2829
#define GDWARF_BOATMAN_Z 10129
#define GDWARF_VELDABAN_X 2827
#define GDWARF_VELDABAN_Z 10210
#define GDWARF_BLASIDAR_X 2907
#define GDWARF_BLASIDAR_Z 10201
#define GDWARF_VERMUNDI_X 2887
#define GDWARF_VERMUNDI_Z 10188
#define GDWARF_LIBRARIAN_X 2861
#define GDWARF_LIBRARIAN_Z 10222
#define GDWARF_BOOKCASE_X 2859
#define GDWARF_BOOKCASE_Z 10224
#define GDWARF_MACHINE_X 2885
#define GDWARF_MACHINE_Z 10189
#define GDWARF_SARO_X 2827
#define GDWARF_SARO_Z 10198
#define GDWARF_DROMUND_X 2838
#define GDWARF_DROMUND_Z 10220
#define GDWARF_SANTIRI_X 2828
#define GDWARF_SANTIRI_Z 10227
#define GDWARF_RELDO_X 3211
#define GDWARF_RELDO_Z 3494
#define GDWARF_THURGO_X 3001
#define GDWARF_THURGO_Z 3144
#define GDWARF_RIKI_X 2904
#define GDWARF_RIKI_Z 10207
#define GDWARF_STAIRS_X 2895
#define GDWARF_STAIRS_Z 10206
#define GDWARF_SECRETARY_X 2877
#define GDWARF_SECRETARY_Z 10201
#define GDWARF_DIRECTOR_X 2879
#define GDWARF_DIRECTOR_Z 10195

static void
gdwarf_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "GDWARF PASS: %s\n", step);
}

static void
gdwarf_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
gdwarf_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
gdwarf_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 48 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
gdwarf_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
gdwarf_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = gdwarf_chatmenu();
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
gdwarf_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = gdwarf_chatmenu();
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
gdwarf_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    gdwarf_god(player);
    selftest_tick(srv);
}

static int
gdwarf_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    gdwarf_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
gdwarf_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
gdwarf_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
gdwarf_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( gdwarf_inv_total(player, obj_id) >= count )
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
gdwarf_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
gdwarf_get_bit(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static int
gdwarf_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    gdwarf_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
gdwarf_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
gdwarf_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    gdwarf_talk(srv, npc_type, slot);
    gdwarf_finish(srv);
}

static void
gdwarf_talk_refuse(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    gdwarf_talk(srv, npc_type, slot);
    gdwarf_click_until_menu(srv, 16);
    gdwarf_pick_row(srv, 2);
    gdwarf_finish(srv);
}

static void
gdwarf_oploc_finish(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    gdwarf_finish(srv);
}

static void
gdwarf_use_held(struct ToriRSServer* srv, int held_id, int useitem_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = useitem_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, held_id, -1, -1);
    gdwarf_finish(srv);
    player->last_useitem = -1;
}

static void
gdwarf_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    gdwarf_finish(srv);
    player->last_useitem = -1;
}

static void
gdwarf_set_skills(
    struct ToriRSServerPlayer* player,
    int stat_magic,
    int stat_fm,
    int stat_craft,
    int stat_thief,
    int magic,
    int fm,
    int craft,
    int thief)
{
    assert(player);
    if( stat_magic >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_magic, magic);
    if( stat_fm >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_fm, fm);
    if( stat_craft >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_craft, craft);
    if( stat_thief >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_thief, thief);
}

static void
gdwarf_reset_state(struct ToriRSServer* srv)
{
    int varp_squire;

    assert(srv);
    gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_NOT_STARTED);
    gdwarf_set_bit(srv, "giantdwarf_veldaban_introduced", 0);
    gdwarf_set_bit(srv, "giantdwarf_sculptor_introduced", 0);
    gdwarf_set_bit(srv, "giantdwarf_model_state", 0);
    gdwarf_set_bit(srv, "giantdwarf_original_company", 0);
    gdwarf_set_bit(srv, "giantdwarf_current_company", 0);
    gdwarf_set_bit(srv, "giantdwarf_pie_given", 0);
    gdwarf_set_bit(srv, "giantdwarf_vermundi_givenbook", 0);
    gdwarf_set_bit(srv, "giantdwarf_gotpair", 0);
    gdwarf_set_bit(srv, "forget_quest", 0);
    gdwarf_set_bit(srv, "dwarfrock_gold_boatman_met", 0);
    varp_squire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "squire");
    if( varp_squire >= 0 && srv->active_player )
        srv->active_player->varps[varp_squire] = 0;
}

static void
selftest_quest_giantdwarf(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_magic;
    int stat_fm;
    int stat_craft;
    int stat_thief;
    int stat_mining;
    int stat_smithing;
    int varp_squire;
    int npc_boatman_pre;
    int npc_boatman;
    int npc_veldaban;
    int npc_blasidar;
    int npc_vermundi;
    int npc_librarian;
    int npc_saro;
    int npc_dromund;
    int npc_santiri;
    int npc_reldo;
    int npc_thurgo;
    int npc_riki;
    int npc_secretary;
    int npc_director;
    int loc_bookcase;
    int loc_machine;
    int loc_stairs_lo;
    int loc_stairs_hi;
    int obj_book;
    int obj_clothes;
    int obj_left_boot;
    int obj_boots;
    int obj_axe_old;
    int obj_axe_saph;
    int obj_axe_new;
    int obj_coal;
    int obj_logs;
    int obj_tinder;
    int obj_coins;
    int obj_law;
    int obj_air;
    int obj_sapphire;
    int obj_pie;
    int obj_iron;
    int obj_copper;
    int obj_bronze;
    int slot;
    int loc_slot;
    int mining_xp_before;
    int smithing_xp_before;
    int crafting_xp_before;
    int magic_xp_before;
    int thieving_xp_before;
    int firemaking_xp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: giantdwarf critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer giantdwarf selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    gdwarf_god(player);

    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_thief = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    stat_mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_smithing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "smithing");
    varp_squire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "squire");
    npc_boatman_pre = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_boatman_mines_prequest");
    npc_boatman = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_boatman_mines");
    npc_veldaban = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_black_guard_leader");
    npc_blasidar = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_shop_sculpture");
    npc_vermundi = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_shop_cloth_poor");
    npc_librarian = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_librarian");
    npc_saro = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_shop_armour");
    npc_dromund = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_excentric_dwarf");
    npc_santiri = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_shop_weapons");
    npc_reldo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "reldo_normal");
    npc_thurgo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "thurgo");
    npc_riki = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_shop_sculpture_model");
    npc_secretary = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_secretary_blue_opal");
    npc_director = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_NPC, "dwarf_city_director_blue_opal");
    loc_bookcase = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "dwarf_keldagrim_bookcase_ladder");
    loc_machine = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "dwarf_keldagrim_spinning_machine");
    loc_stairs_lo = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "dwarf_keldagrim_wide_stairs_lower");
    loc_stairs_hi = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_LOC, "dwarf_keldagrim_wide_stairs_upper");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_library_book");
    obj_clothes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_clothes");
    obj_left_boot = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_OBJ, "dwarf_perfect_left_boot");
    obj_boots = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_OBJ, "dwarf_perfect_pair_of_boots");
    obj_axe_old = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_battleaxe_old");
    obj_axe_saph = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_OBJ, "dwarf_battleaxe_sapphires");
    obj_axe_new = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dwarf_battleaxe_new");
    obj_coal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coal");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_law = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lawrune");
    obj_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "airrune");
    obj_sapphire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sapphire");
    obj_pie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "redberry_pie");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_copper = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "copper_ore");
    obj_bronze = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_bar");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "giantdwarf_quest") >= 0,
                   "varbit giantdwarf_quest should resolve");
    SELFTEST_CHECK(npc_boatman_pre > 0, "npc dwarf_city_boatman_mines_prequest should resolve");
    SELFTEST_CHECK(npc_veldaban > 0, "npc dwarf_city_black_guard_leader should resolve");
    SELFTEST_CHECK(npc_blasidar > 0, "npc dwarf_city_shop_sculpture should resolve");
    SELFTEST_CHECK(obj_clothes > 0, "obj dwarf_clothes should resolve");
    SELFTEST_CHECK(obj_axe_new > 0, "obj dwarf_battleaxe_new should resolve");
    if( npc_boatman_pre <= 0 || npc_veldaban <= 0 )
    {
        fprintf(stderr, "ToriRSServer giantdwarf selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    gdwarf_clear_inv(player);
    gdwarf_reset_state(srv);
    gdwarf_god(player);

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_not_started");

    /* ---- Boatman skill gates / refuse / accept ---- */
    slot = gdwarf_spawn(srv, npc_boatman_pre, GDWARF_BOATMAN_X, GDWARF_BOATMAN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "prequest boatman should spawn");
    if( slot >= 0 )
    {
        gdwarf_set_skills(player, stat_magic, stat_fm, stat_craft, stat_thief,
                          1, GDWARF_REQ_FIREMAKING, GDWARF_REQ_CRAFTING,
                          GDWARF_REQ_THIEVING);
        gdwarf_talk_finish(srv, npc_boatman_pre, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_NOT_STARTED,
                       "low Magic must not start The Giant Dwarf");
        gdwarf_pass("opnpc1_boatman_skillgate_magic");

        gdwarf_set_skills(player, stat_magic, stat_fm, stat_craft, stat_thief,
                          GDWARF_REQ_MAGIC, 1, GDWARF_REQ_CRAFTING,
                          GDWARF_REQ_THIEVING);
        gdwarf_talk_finish(srv, npc_boatman_pre, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_NOT_STARTED,
                       "low Firemaking must not start The Giant Dwarf");
        gdwarf_pass("opnpc1_boatman_skillgate_firemaking");

        gdwarf_set_skills(player, stat_magic, stat_fm, stat_craft, stat_thief,
                          GDWARF_REQ_MAGIC, GDWARF_REQ_FIREMAKING, 1,
                          GDWARF_REQ_THIEVING);
        gdwarf_talk_finish(srv, npc_boatman_pre, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_NOT_STARTED,
                       "low Crafting must not start The Giant Dwarf");
        gdwarf_pass("opnpc1_boatman_skillgate_crafting");

        gdwarf_set_skills(player, stat_magic, stat_fm, stat_craft, stat_thief,
                          GDWARF_REQ_MAGIC, GDWARF_REQ_FIREMAKING,
                          GDWARF_REQ_CRAFTING, 1);
        gdwarf_talk_finish(srv, npc_boatman_pre, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_NOT_STARTED,
                       "low Thieving must not start The Giant Dwarf");
        gdwarf_pass("opnpc1_boatman_skillgate_thieving");

        gdwarf_set_skills(player, stat_magic, stat_fm, stat_craft, stat_thief,
                          GDWARF_REQ_MAGIC, GDWARF_REQ_FIREMAKING,
                          GDWARF_REQ_CRAFTING, GDWARF_REQ_THIEVING);
        gdwarf_talk_refuse(srv, npc_boatman_pre, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_NOT_STARTED,
                       "refusing the boatman must not start the quest");
        gdwarf_pass("opnpc1_boatman_refuse");

        gdwarf_talk_finish(srv, npc_boatman_pre, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_ARRIVED,
                       "accepting the boatman must write arrived, got %d",
                       gdwarf_get_bit(player, "giantdwarf_quest"));
        gdwarf_pass("opnpc1_boatman_accept_start");

        gdwarf_talk_finish(srv, npc_boatman_pre, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_ARRIVED,
                       "post-start prequest boatman must stay arrived");
        gdwarf_pass("opnpc1_boatman_already_started");
        gdwarf_free_npc(srv, slot);
    }

    if( npc_boatman > 0 )
    {
        int mines = gdwarf_spawn(srv, npc_boatman, GDWARF_BOATMAN_X, GDWARF_BOATMAN_Z, 0);

        if( mines >= 0 )
        {
            int before = gdwarf_get_bit(player, "giantdwarf_quest");

            gdwarf_talk_finish(srv, npc_boatman, mines);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == before,
                           "started boatman_mines must stay additive (BAR postquest)");
            gdwarf_pass("opnpc1_boatman_mines_postquest_additive");
            gdwarf_free_npc(srv, mines);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_arrived");

    /* ---- Veldaban offer / refuse / accept ---- */
    slot = gdwarf_spawn(srv, npc_veldaban, GDWARF_VELDABAN_X, GDWARF_VELDABAN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Veldaban should spawn");
    if( slot >= 0 )
    {
        gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_ARRIVED);
        gdwarf_talk_refuse(srv, npc_veldaban, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_ARRIVED,
                       "refusing Veldaban must not advance");
        gdwarf_pass("opnpc1_veldaban_refuse");

        gdwarf_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_VELDABAN_DONE,
                       "accepting Veldaban must write veldaban_done, got %d",
                       gdwarf_get_bit(player, "giantdwarf_quest"));
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_veldaban_introduced") == 1,
                       "Veldaban accept must set veldaban_introduced");
        gdwarf_pass("opnpc1_veldaban_accept");

        gdwarf_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_VELDABAN_DONE,
                       "mid Veldaban must stay on Blasidar");
        gdwarf_pass("opnpc1_veldaban_mid_blasidar");
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_veldaban");

    /* ---- Blasidar ---- */
    if( npc_blasidar > 0 )
    {
        int sculp = gdwarf_spawn(srv, npc_blasidar, GDWARF_BLASIDAR_X, GDWARF_BLASIDAR_Z, 0);

        SELFTEST_CHECK(sculp >= 0, "Blasidar should spawn");
        if( sculp >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_NOT_STARTED);
            gdwarf_talk_finish(srv, npc_blasidar, sculp);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_NOT_STARTED,
                           "Blasidar too-early must stay not-started");
            gdwarf_pass("opnpc1_blasidar_too_early");

            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_VELDABAN_DONE);
            gdwarf_talk_refuse(srv, npc_blasidar, sculp);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_VELDABAN_DONE,
                           "refusing Blasidar must not advance");
            gdwarf_pass("opnpc1_blasidar_refuse");

            gdwarf_talk_finish(srv, npc_blasidar, sculp);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_BLASIDAR_DONE,
                           "accepting Blasidar must write blasidar_done, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_sculptor_introduced") == 1,
                           "Blasidar accept must set sculptor_introduced");
            gdwarf_pass("opnpc1_blasidar_accept");

            gdwarf_talk_finish(srv, npc_blasidar, sculp);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_BLASIDAR_DONE,
                           "mid Blasidar must stay on the three items");
            gdwarf_pass("opnpc1_blasidar_mid_items");
            gdwarf_free_npc(srv, sculp);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_blasidar");

    /* ---- Clothes / librarian / machine ---- */
    if( npc_vermundi > 0 )
    {
        int cloth = gdwarf_spawn(srv, npc_vermundi, GDWARF_VERMUNDI_X, GDWARF_VERMUNDI_Z, 0);

        if( cloth >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_NOT_STARTED);
            gdwarf_talk_refuse(srv, npc_vermundi, cloth);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_NOT_STARTED,
                           "Vermundi shop chat must not start the quest");
            gdwarf_pass("opnpc1_vermundi_shop");

            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_BLASIDAR_DONE);
            gdwarf_talk_finish(srv, npc_vermundi, cloth);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_VERMUNDI_ASKED,
                           "Vermundi must send the player to the librarian, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_vermundi_ask_clothes");
            gdwarf_free_npc(srv, cloth);
        }
    }

    if( npc_librarian > 0 )
    {
        int lib = gdwarf_spawn(srv, npc_librarian, GDWARF_LIBRARIAN_X, GDWARF_LIBRARIAN_Z, 0);

        if( lib >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_NOT_STARTED);
            gdwarf_talk_finish(srv, npc_librarian, lib);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_NOT_STARTED,
                           "librarian too-early must stay shh");
            gdwarf_pass("opnpc1_librarian_shh");

            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_VERMUNDI_ASKED);
            gdwarf_talk_finish(srv, npc_librarian, lib);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_LIBRARIAN_ASKED,
                           "librarian must point at the top shelf, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_librarian_costume_book");

            gdwarf_talk_finish(srv, npc_librarian, lib);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_LIBRARIAN_ASKED,
                           "librarian mid must stay on the bookcase");
            gdwarf_pass("opnpc1_librarian_mid_shelf");
            gdwarf_free_npc(srv, lib);
        }
    }

    if( loc_bookcase >= 0 )
    {
        loc_slot = gdwarf_place_loc(srv, loc_bookcase, GDWARF_BOOKCASE_X, GDWARF_BOOKCASE_Z, 0);
        if( loc_slot >= 0 )
        {
            gdwarf_clear_inv(player);
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_VERMUNDI_ASKED);
            gdwarf_oploc_finish(srv, loc_bookcase, loc_slot);
            SELFTEST_CHECK(gdwarf_inv_total(player, obj_book) == 0,
                           "bookcase too-early must find nothing");
            gdwarf_pass("oploc1_bookcase_nothing");

            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_LIBRARIAN_ASKED);
            gdwarf_oploc_finish(srv, loc_bookcase, loc_slot);
            SELFTEST_CHECK(gdwarf_inv_total(player, obj_book) == 1,
                           "bookcase climb should grant the costume book");
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_GOT_BOOK,
                           "finding the book must write got_book");
            gdwarf_pass("oploc1_bookcase_find_book");

            gdwarf_oploc_finish(srv, loc_bookcase, loc_slot);
            SELFTEST_CHECK(gdwarf_inv_total(player, obj_book) == 1,
                           "second climb must not duplicate the book");
            gdwarf_pass("oploc1_bookcase_already");
        }
    }

    if( npc_vermundi > 0 )
    {
        int cloth = gdwarf_spawn(srv, npc_vermundi, GDWARF_VERMUNDI_X, GDWARF_VERMUNDI_Z, 0);

        if( cloth >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_GOT_BOOK);
            if( obj_book > 0 && gdwarf_inv_total(player, obj_book) < 1 )
                gdwarf_give(player, obj_book, 1);
            gdwarf_talk_finish(srv, npc_vermundi, cloth);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SHOWED_BOOK,
                           "showing the book must write showed_book, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_vermundi_givenbook") == 1,
                           "showing the book must set vermundi_givenbook");
            gdwarf_pass("opnpc1_vermundi_show_book");

            gdwarf_talk_finish(srv, npc_vermundi, cloth);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SHOWED_BOOK,
                           "Vermundi mid must stay on coal and logs");
            gdwarf_pass("opnpc1_vermundi_need_fuel");
            gdwarf_free_npc(srv, cloth);
        }
    }

    if( loc_machine >= 0 )
    {
        loc_slot = gdwarf_place_loc(srv, loc_machine, GDWARF_MACHINE_X, GDWARF_MACHINE_Z, 0);
        if( loc_slot >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_SHOWED_BOOK);
            gdwarf_clear_inv(player);
            if( obj_coal > 0 )
                gdwarf_give(player, obj_coal, 1);
            gdwarf_use_loc(srv, loc_machine, loc_slot, obj_coal);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SHOWED_BOOK,
                           "coal without logs must not load the machine");
            gdwarf_pass("oplocu_machine_need_logs");

            if( obj_logs > 0 )
                gdwarf_give(player, obj_logs, 1);
            gdwarf_use_loc(srv, loc_machine, loc_slot, obj_coal);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_MACHINE_LOADED,
                           "coal+logs should load the machine, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("oplocu_machine_load");

            if( obj_tinder > 0 )
                gdwarf_give(player, obj_tinder, 1);
            gdwarf_use_loc(srv, loc_machine, loc_slot, obj_tinder);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_MACHINE_STARTED,
                           "tinderbox should start the machine, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("oplocu_machine_light");
        }
    }

    if( npc_vermundi > 0 )
    {
        int cloth = gdwarf_spawn(srv, npc_vermundi, GDWARF_VERMUNDI_X, GDWARF_VERMUNDI_Z, 0);

        if( cloth >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_MACHINE_STARTED);
            gdwarf_clear_inv(player);
            gdwarf_talk_finish(srv, npc_vermundi, cloth);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_MACHINE_STARTED,
                           "Vermundi without 200 coins must not grant clothes");
            gdwarf_pass("opnpc1_vermundi_no_coins");

            if( obj_coins > 0 )
                gdwarf_give(player, obj_coins, 200);
            gdwarf_talk_finish(srv, npc_vermundi, cloth);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_CLOTHES_DONE,
                           "paying Vermundi must write clothes_done, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(obj_clothes <= 0 || gdwarf_inv_total(player, obj_clothes) == 1,
                           "Vermundi should grant exquisite clothes");
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_model_state") == GDWARF_MODEL_CLOTHES,
                           "clothes hand-in must set model_state bit0");
            gdwarf_pass("opnpc1_vermundi_pay_clothes");

            gdwarf_talk_finish(srv, npc_vermundi, cloth);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_CLOTHES_DONE,
                           "post-clothes Vermundi must stay done");
            gdwarf_pass("opnpc1_vermundi_after_clothes");
            gdwarf_free_npc(srv, cloth);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_clothes");

    /* ---- Boots / Saro / Dromund ---- */
    if( npc_saro > 0 )
    {
        int armour = gdwarf_spawn(srv, npc_saro, GDWARF_SARO_X, GDWARF_SARO_Z, 0);

        if( armour >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_CLOTHES_DONE);
            gdwarf_talk_finish(srv, npc_saro, armour);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SARO_ASKED,
                           "Saro must point at Dromund, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_saro_ask_boots");

            gdwarf_talk_finish(srv, npc_saro, armour);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SARO_ASKED,
                           "Saro mid must stay on Dromund");
            gdwarf_pass("opnpc1_saro_mid");
            gdwarf_free_npc(srv, armour);
        }
    }

    if( npc_dromund > 0 )
    {
        int ecc = gdwarf_spawn(srv, npc_dromund, GDWARF_DROMUND_X, GDWARF_DROMUND_Z, 0);

        if( ecc >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_SARO_ASKED);
            gdwarf_talk_finish(srv, npc_dromund, ecc);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_DROMUND_ASKED,
                           "Dromund refuse must write dromund_asked, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_dromund_refuse");

            gdwarf_talk_finish(srv, npc_dromund, ecc);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_LEFT_BOOT,
                           "sneaking the left boot must write left_boot, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(obj_left_boot <= 0 || gdwarf_inv_total(player, obj_left_boot) == 1,
                           "Dromund sneak should grant the left boot");
            gdwarf_pass("opnpc1_dromund_steal_left");

            gdwarf_talk_finish(srv, npc_dromund, ecc);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_LEFT_BOOT,
                           "Telekinetic Grab without runes must stay left_boot");
            gdwarf_pass("opnpc1_dromund_need_runes");

            if( obj_law > 0 )
                gdwarf_give(player, obj_law, 1);
            if( obj_air > 0 )
                gdwarf_give(player, obj_air, 1);
            gdwarf_talk_finish(srv, npc_dromund, ecc);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_BOOTS_DONE,
                           "Telekinetic Grab must write boots_done, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(obj_boots <= 0 || gdwarf_inv_total(player, obj_boots) == 1,
                           "combining boots should grant the exquisite pair");
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_gotpair") == 1,
                           "boot pair must set giantdwarf_gotpair");
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_model_state") == GDWARF_MODEL_BOOTS,
                           "boots hand-in must set model_state 3");
            gdwarf_pass("opnpc1_dromund_telegrab_pair");

            gdwarf_talk_finish(srv, npc_dromund, ecc);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_BOOTS_DONE,
                           "post-boots Dromund must stay done");
            gdwarf_pass("opnpc1_dromund_after");
            gdwarf_free_npc(srv, ecc);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_boots");

    /* ---- Axe / Santiri / sapphires / Reldo / Thurgo ---- */
    if( npc_santiri > 0 )
    {
        int weap = gdwarf_spawn(srv, npc_santiri, GDWARF_SANTIRI_X, GDWARF_SANTIRI_Z, 0);

        if( weap >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_BOOTS_DONE);
            gdwarf_talk_finish(srv, npc_santiri, weap);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SANTIRI_ASKED,
                           "Santiri must entrust the old axe, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(obj_axe_old <= 0 || gdwarf_inv_total(player, obj_axe_old) == 1,
                           "Santiri should grant dwarf_battleaxe_old");
            gdwarf_pass("opnpc1_santiri_entrust_axe");

            gdwarf_talk_finish(srv, npc_santiri, weap);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SANTIRI_ASKED,
                           "Santiri mid must stay on the repair");
            gdwarf_pass("opnpc1_santiri_mid");
            gdwarf_free_npc(srv, weap);
        }
    }

    if( obj_axe_old > 0 && obj_sapphire > 0 )
    {
        gdwarf_clear_inv(player);
        gdwarf_give(player, obj_axe_old, 1);
        gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_SANTIRI_ASKED);
        if( obj_coins > 0 )
            gdwarf_use_held(srv, obj_axe_old, obj_coins);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SANTIRI_ASKED,
                       "wrong item on the old axe must refuse");
        gdwarf_pass("opheldu_axe_wrong_item");

        gdwarf_give(player, obj_sapphire, 1);
        gdwarf_use_held(srv, obj_axe_old, obj_sapphire);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SANTIRI_ASKED,
                       "one sapphire must not sharpen the axe");
        gdwarf_pass("opheldu_axe_need_three_sapphires");

        gdwarf_give(player, obj_sapphire, GDWARF_SAPPHIRES_NEEDED);
        gdwarf_use_held(srv, obj_axe_old, obj_sapphire);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SAPPHIRES_USED,
                       "three sapphires must write sapphires_used, got %d",
                       gdwarf_get_bit(player, "giantdwarf_quest"));
        SELFTEST_CHECK(obj_axe_saph <= 0 || gdwarf_inv_total(player, obj_axe_saph) == 1,
                       "setting sapphires should grant dwarf_battleaxe_sapphires");
        gdwarf_pass("opheldu_axe_set_sapphires");
    }

    if( npc_librarian > 0 )
    {
        int lib = gdwarf_spawn(srv, npc_librarian, GDWARF_LIBRARIAN_X, GDWARF_LIBRARIAN_Z, 0);

        if( lib >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_SAPPHIRES_USED);
            gdwarf_talk_finish(srv, npc_librarian, lib);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_IMCANDO_ASKED,
                           "librarian Imcando lead must write imcando_asked, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_librarian_imcando");

            gdwarf_talk_finish(srv, npc_librarian, lib);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_IMCANDO_ASKED,
                           "librarian mid Imcando must stay on Reldo");
            gdwarf_pass("opnpc1_librarian_imcando_mid");
            gdwarf_free_npc(srv, lib);
        }
    }

    if( npc_reldo > 0 )
    {
        int reldo = gdwarf_spawn(srv, npc_reldo, GDWARF_RELDO_X, GDWARF_RELDO_Z, 0);

        if( reldo >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_IMCANDO_ASKED);
            gdwarf_talk_finish(srv, npc_reldo, reldo);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_RELDO_TOLD,
                           "Reldo Giant Dwarf splice must write reldo_told, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_reldo_imcando_additive");
            gdwarf_free_npc(srv, reldo);
        }
    }

    if( npc_thurgo > 0 )
    {
        int smith = gdwarf_spawn(srv, npc_thurgo, GDWARF_THURGO_X, GDWARF_THURGO_Z, 0);

        if( smith >= 0 )
        {
            gdwarf_clear_inv(player);
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_RELDO_TOLD);
            gdwarf_set_bit(srv, "giantdwarf_pie_given", 0);
            if( varp_squire >= 0 )
                player->varps[varp_squire] = 0;
            gdwarf_talk_finish(srv, npc_thurgo, smith);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_RELDO_TOLD,
                           "Thurgo without the sapphire axe must wait");
            gdwarf_pass("opnpc1_thurgo_need_axe");

            if( obj_axe_saph > 0 )
                gdwarf_give(player, obj_axe_saph, 1);
            gdwarf_talk_finish(srv, npc_thurgo, smith);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_pie_given") == 0,
                           "Thurgo without pie (and without Knight's Sword) must ask");
            gdwarf_pass("opnpc1_thurgo_want_pie");

            if( obj_pie > 0 )
                gdwarf_give(player, obj_pie, 1);
            gdwarf_talk_finish(srv, npc_thurgo, smith);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_pie_given") == 1,
                           "feeding Thurgo a pie must set giantdwarf_pie_given");
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_RELDO_TOLD,
                           "pie alone must not finish the axe");
            gdwarf_pass("opnpc1_thurgo_pie_then_iron");

            if( obj_iron > 0 )
                gdwarf_give(player, obj_iron, 1);
            if( obj_axe_saph > 0 && gdwarf_inv_total(player, obj_axe_saph) < 1 )
                gdwarf_give(player, obj_axe_saph, 1);
            gdwarf_talk_finish(srv, npc_thurgo, smith);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_AXE_DONE,
                           "Thurgo + iron bar must write axe_done, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(obj_axe_new <= 0 || gdwarf_inv_total(player, obj_axe_new) == 1,
                           "Thurgo should grant the restored battleaxe");
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_model_state") == GDWARF_MODEL_AXE,
                           "axe repair must set model_state 7");
            gdwarf_pass("opnpc1_thurgo_repair_axe");
            gdwarf_free_npc(srv, smith);
        }
    }

    /* Knight's Sword already-fed-pie shortcut: Giant Dwarf must skip the second pie. */
    if( npc_thurgo > 0 && obj_axe_saph > 0 && obj_iron > 0 )
    {
        int smith = gdwarf_spawn(srv, npc_thurgo, GDWARF_THURGO_X, GDWARF_THURGO_Z, 0);

        if( smith >= 0 )
        {
            gdwarf_clear_inv(player);
            gdwarf_give(player, obj_axe_saph, 1);
            gdwarf_give(player, obj_iron, 1);
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_RELDO_TOLD);
            gdwarf_set_bit(srv, "giantdwarf_pie_given", 0);
            if( varp_squire >= 0 )
                player->varps[varp_squire] = GDWARF_SQUIRE_GIVEN_PIE;
            gdwarf_talk_finish(srv, npc_thurgo, smith);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_AXE_DONE,
                           "Knight's Sword pie shortcut must still repair the axe");
            SELFTEST_CHECK(varp_squire < 0 ||
                               player->varps[varp_squire] == GDWARF_SQUIRE_GIVEN_PIE,
                           "Giant Dwarf must not rewrite Knight's Sword %%squire");
            gdwarf_pass("opnpc1_thurgo_knights_sword_pie_skip");
            if( varp_squire >= 0 )
                player->varps[varp_squire] = 0;
            gdwarf_free_npc(srv, smith);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_axe");

    /* ---- Riki / stairs / consortium / finish ---- */
    if( npc_riki > 0 )
    {
        int model = gdwarf_spawn(srv, npc_riki, GDWARF_RIKI_X, GDWARF_RIKI_Z, 0);

        if( model >= 0 )
        {
            gdwarf_clear_inv(player);
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_BOOTS_DONE);
            gdwarf_talk_finish(srv, npc_riki, model);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_BOOTS_DONE,
                           "Riki too-early must wait for all three items");
            gdwarf_pass("opnpc1_riki_waiting");

            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_AXE_DONE);
            gdwarf_talk_finish(srv, npc_riki, model);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_AXE_DONE,
                           "Riki without the three items must refuse");
            gdwarf_pass("opnpc1_riki_missing_items");

            if( obj_clothes > 0 )
                gdwarf_give(player, obj_clothes, 1);
            if( obj_boots > 0 )
                gdwarf_give(player, obj_boots, 1);
            if( obj_axe_new > 0 )
                gdwarf_give(player, obj_axe_new, 1);
            gdwarf_talk_finish(srv, npc_riki, model);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_ITEMS_GIVEN,
                           "dressing Riki must write items_given, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(obj_clothes <= 0 || gdwarf_inv_total(player, obj_clothes) == 0,
                           "Riki should take the clothes");
            gdwarf_pass("opnpc1_riki_dress_model");
            gdwarf_free_npc(srv, model);
        }
    }

    if( npc_blasidar > 0 )
    {
        int sculp = gdwarf_spawn(srv, npc_blasidar, GDWARF_BLASIDAR_X, GDWARF_BLASIDAR_Z, 0);

        if( sculp >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_ITEMS_GIVEN);
            gdwarf_talk_finish(srv, npc_blasidar, sculp);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_BLASIDAR_AFTER,
                           "reporting to Blasidar must write blasidar_after, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_blasidar_after_items");
            gdwarf_free_npc(srv, sculp);
        }
    }

    if( loc_stairs_lo >= 0 )
    {
        loc_slot = gdwarf_place_loc(srv, loc_stairs_lo, GDWARF_STAIRS_X, GDWARF_STAIRS_Z, 0);
        if( loc_slot >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_AXE_DONE);
            gdwarf_oploc_finish(srv, loc_stairs_lo, loc_slot);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_AXE_DONE,
                           "stairs before the items are given must refuse");
            gdwarf_pass("oploc1_stairs_too_early");

            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_BLASIDAR_AFTER);
            gdwarf_oploc_finish(srv, loc_stairs_lo, loc_slot);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_ENTERED_CONSORTIUM,
                           "climbing the market stairs must write entered_consortium, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("oploc1_stairs_enter_consortium");
        }
    }

    if( npc_secretary > 0 )
    {
        int sec = gdwarf_spawn(
            srv, npc_secretary, GDWARF_SECRETARY_X, GDWARF_SECRETARY_Z, 1);

        if( sec >= 0 )
        {
            gdwarf_clear_inv(player);
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_ENTERED_CONSORTIUM);
            gdwarf_talk_finish(srv, npc_secretary, sec);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_ENTERED_CONSORTIUM,
                           "secretary without copper ore must stay entered");
            gdwarf_pass("opnpc1_secretary_need_ore");

            if( obj_copper > 0 )
                gdwarf_give(player, obj_copper, GDWARF_ORE_NEEDED);
            gdwarf_talk_finish(srv, npc_secretary, sec);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SECRETARY_DONE,
                           "delivering copper ore must write secretary_done, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_secretary_deliver_ore");

            gdwarf_talk_finish(srv, npc_secretary, sec);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SECRETARY_DONE,
                           "secretary after the delivery must point at the director");
            gdwarf_pass("opnpc1_secretary_after");
            gdwarf_free_npc(srv, sec);
        }
    }

    if( npc_director > 0 )
    {
        int dir = gdwarf_spawn(srv, npc_director, GDWARF_DIRECTOR_X, GDWARF_DIRECTOR_Z, 1);

        if( dir >= 0 )
        {
            gdwarf_clear_inv(player);
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_ENTERED_CONSORTIUM);
            gdwarf_talk_finish(srv, npc_director, dir);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_ENTERED_CONSORTIUM,
                           "director before the secretary task must bounce");
            gdwarf_pass("opnpc1_director_talk_secretary");

            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_SECRETARY_DONE);
            gdwarf_talk_finish(srv, npc_director, dir);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_SECRETARY_DONE,
                           "director without bronze bars must stay secretary_done");
            gdwarf_pass("opnpc1_director_need_bars");

            if( obj_bronze > 0 )
                gdwarf_give(player, obj_bronze, GDWARF_BAR_NEEDED);
            gdwarf_talk_finish(srv, npc_director, dir);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_DIRECTOR_DONE,
                           "delivering bronze bars must write director_done, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_director_deliver_bars");

            gdwarf_talk_refuse(srv, npc_director, dir);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_DIRECTOR_DONE,
                           "refusing to join must stay director_done");
            gdwarf_pass("opnpc1_director_join_refuse");

            gdwarf_talk_finish(srv, npc_director, dir);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_JOINED_COMPANY,
                           "joining Blue Opal must write joined_company, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_current_company") ==
                               GDWARF_COMPANY_BLUE_OPAL,
                           "join must set current_company Blue Opal");
            gdwarf_pass("opnpc1_director_join_accept");

            gdwarf_talk_finish(srv, npc_director, dir);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_DIRECTOR_AFTER,
                           "pledging support must write director_after, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("opnpc1_director_support");
            gdwarf_free_npc(srv, dir);
        }
    }

    if( loc_stairs_hi >= 0 )
    {
        loc_slot = gdwarf_place_loc(srv, loc_stairs_hi, GDWARF_SECRETARY_X, GDWARF_SECRETARY_Z, 1);
        if( loc_slot >= 0 )
        {
            gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_DIRECTOR_AFTER);
            gdwarf_oploc_finish(srv, loc_stairs_hi, loc_slot);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_READY_TO_FINISH,
                           "leaving the consortium floor must write ready_to_finish, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            gdwarf_pass("oploc1_stairs_leave_ready");
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_consortium");

    /* ---- Authored complete scroll via Veldaban ---- */
    if( slot < 0 )
        slot = gdwarf_spawn(srv, npc_veldaban, GDWARF_VELDABAN_X, GDWARF_VELDABAN_Z, 0);
    else
        slot = gdwarf_spawn(srv, npc_veldaban, GDWARF_VELDABAN_X, GDWARF_VELDABAN_Z, 0);
    if( slot >= 0 )
    {
        gdwarf_set_skills(player, stat_magic, stat_fm, stat_craft, stat_thief,
                          GDWARF_REQ_MAGIC, GDWARF_REQ_FIREMAKING,
                          GDWARF_REQ_CRAFTING, GDWARF_REQ_THIEVING);
        if( stat_mining >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_mining, 1);
        if( stat_smithing >= 0 )
            ToriRSServer_CombatSetLevel(player, stat_smithing, 1);
        mining_xp_before = stat_mining >= 0 ? player->stat_xp_tenths[stat_mining] : 0;
        smithing_xp_before = stat_smithing >= 0 ? player->stat_xp_tenths[stat_smithing] : 0;
        crafting_xp_before = stat_craft >= 0 ? player->stat_xp_tenths[stat_craft] : 0;
        magic_xp_before = stat_magic >= 0 ? player->stat_xp_tenths[stat_magic] : 0;
        thieving_xp_before = stat_thief >= 0 ? player->stat_xp_tenths[stat_thief] : 0;
        firemaking_xp_before = stat_fm >= 0 ? player->stat_xp_tenths[stat_fm] : 0;

        gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_READY_TO_FINISH);
        ToriRSServer_WorldCloseModal(srv);
        gdwarf_talk(srv, npc_veldaban, slot);
        {
            int t;

            for( t = 0; t < 64 && player->active_script; t++ )
            {
                selftest_click_through(srv, 1);
                selftest_tick(srv);
            }
        }
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_COMPLETE,
                       "Veldaban finale must complete at endstate 50, got %d",
                       gdwarf_get_bit(player, "giantdwarf_quest"));
        SELFTEST_CHECK(stat_mining < 0 ||
                           player->stat_xp_tenths[stat_mining] >=
                               mining_xp_before + GDWARF_REWARD_MINING,
                       "complete should award 2500 Mining XP");
        SELFTEST_CHECK(stat_smithing < 0 ||
                           player->stat_xp_tenths[stat_smithing] >=
                               smithing_xp_before + GDWARF_REWARD_SMITHING,
                       "complete should award 2500 Smithing XP");
        SELFTEST_CHECK(stat_craft < 0 ||
                           player->stat_xp_tenths[stat_craft] >=
                               crafting_xp_before + GDWARF_REWARD_CRAFTING,
                       "complete should award 2500 Crafting XP");
        SELFTEST_CHECK(stat_magic < 0 ||
                           player->stat_xp_tenths[stat_magic] >=
                               magic_xp_before + GDWARF_REWARD_MAGIC,
                       "complete should award 1500 Magic XP");
        SELFTEST_CHECK(stat_thief < 0 ||
                           player->stat_xp_tenths[stat_thief] >=
                               thieving_xp_before + GDWARF_REWARD_THIEVING,
                       "complete should award 1500 Thieving XP");
        SELFTEST_CHECK(stat_fm < 0 ||
                           player->stat_xp_tenths[stat_fm] >=
                               firemaking_xp_before + GDWARF_REWARD_FIREMAKING,
                       "complete should award 1500 Firemaking XP");
        gdwarf_pass("opnpc1_veldaban_complete");

        gdwarf_talk_finish(srv, npc_veldaban, slot);
        SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_COMPLETE,
                       "post-complete Veldaban must stay at endstate 50");
        gdwarf_pass("opnpc1_veldaban_post_complete");
        gdwarf_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,gdwarf_journal]", NULL, 0);
    gdwarf_finish(srv);
    gdwarf_pass("journal_complete");

    /* ::complete twice: first sets endstate, second is a no-op. */
    gdwarf_set_bit(srv, "giantdwarf_quest", GDWARF_READY_TO_FINISH);
    {
        int32_t row = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_giantdwarf");
        int32_t first = 0;
        int32_t second = 0;

        if( row > 0 )
        {
            ToriRSServer_ScriptsRunProcInt(srv, "[proc,quest_cheat_complete]", &row, 1, &first);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_COMPLETE,
                           "::complete quest_giantdwarf should set endstate 50, got %d",
                           gdwarf_get_bit(player, "giantdwarf_quest"));
            ToriRSServer_ScriptsRunProcInt(srv, "[proc,quest_cheat_complete]", &row, 1, &second);
            SELFTEST_CHECK(gdwarf_get_bit(player, "giantdwarf_quest") == GDWARF_COMPLETE,
                           "second ::complete must stay at endstate 50");
            gdwarf_pass("quest_cheat_complete_idempotent");
        }
    }

    gdwarf_clear_inv(player);
    gdwarf_reset_state(srv);
    gdwarf_god(player);
    ToriRSServer_WorldCloseModal(srv);

    fprintf(stderr, "ToriRSServer giantdwarf selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_GIANTDWARF_SELFTEST_U_H */
