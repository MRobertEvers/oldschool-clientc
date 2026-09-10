#ifndef TORIRSSERVER_TEST_QUEST_ICTHLARIN_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ICTHLARIN_SELFTEST_U_H

/* Icthlarin's Little Helper Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Wanderer / Sphinx / Apparitions /
 * Possessed Priest cannot leak. Real OPNPC / OPLOC / OPLOCU / OPNPC1 on
 * the critical path. player->godmode = 1 for the whole walk (Apparition
 * and Possessed Priest are not death tests). Completion goes through the
 * town High Priest's authored
 * ~quest_complete_rewards(quest_icthlarinslittlehelper, ...).
 * Additive ILH branches only — do not rewrite Contact!, The Feud,
 * generic Sophanem/Menaphos, Observatory, Construction, or MTA. */

#define ICS_NOT_STARTED 0
#define ICS_NEED_SUPPLIES 1
#define ICS_ENTERED_CITY 2
#define ICS_FIRST_MEMORY 3
#define ICS_SPHINX 5
#define ICS_HIGH_PRIEST 6
#define ICS_RETURN_JAR 7
#define ICS_JAR_GUARDIAN 8
#define ICS_JAR_KILLED 11
#define ICS_PLACE_JAR 12
#define ICS_JAR_DONE 14
#define ICS_EMBALM 15
#define ICS_RITUAL 16
#define ICS_PLACE_SYMBOL 17
#define ICS_SYMBOL_PLACED 18
#define ICS_POSSESSED 19
#define ICS_MEET_GOD 24
#define ICS_FINISH_TALK 25
#define ICS_COMPLETE 26

#define ICS_JAR_HET 1
#define ICS_LINEN_COST 30
#define ICS_THIEVE_XP 45000
#define ICS_AGILITY_XP 40000
#define ICS_WC_XP 40000
#define FLUFFS_COMPLETE 6

#define ICS_WANDERER_X 3316
#define ICS_WANDERER_Z 2849
#define ICS_PYRAMID_DOOR_X 3295
#define ICS_PYRAMID_DOOR_Z 2779
#define ICS_PIT_X 3292
#define ICS_PIT_Z 9193
#define ICS_WEST_DOOR_X 3280
#define ICS_WEST_DOOR_Z 9198
#define ICS_SPHINX_X 3301
#define ICS_SPHINX_Z 2785
#define ICS_HIPRIEST_X 3281
#define ICS_HIPRIEST_Z 2772
#define ICS_JAR_X 3286
#define ICS_JAR_Z 9194
#define ICS_EMBALMER_X 3287
#define ICS_EMBALMER_Z 2755
#define ICS_CARPENTER_X 3313
#define ICS_CARPENTER_Z 2770
#define ICS_LINEN_X 3311
#define ICS_LINEN_Z 2787
#define ICS_WATER_X 3286
#define ICS_WATER_Z 2840
#define ICS_SUNTRAP_X 3305
#define ICS_SUNTRAP_Z 2756
#define ICS_EAST_DOOR_X 3306
#define ICS_EAST_DOOR_Z 9199
#define ICS_SARC_X 3312
#define ICS_SARC_Z 9197
#define ICS_EVERGREEN_X 3300
#define ICS_EVERGREEN_Z 2800

static void
ics_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "ICS PASS: %s\n", step);
}

static void
ics_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ics_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ics_finish(struct ToriRSServer* srv)
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
ics_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ics_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ics_chatmenu();
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
ics_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ics_chatmenu();
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
ics_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ics_god(player);
    selftest_tick(srv);
}

static int
ics_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    ics_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
ics_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
ics_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
ics_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( ics_inv_total(player, obj_id) >= count )
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
ics_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ics_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
ics_set_varp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        player->varps[varp] = value;
}

static int
ics_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    ics_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
ics_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
ics_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ics_talk(srv, npc_type, slot);
    ics_finish(srv);
}

static void
ics_talk_choice(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    ics_talk(srv, npc_type, slot);
    ics_click_until_menu(srv, 12);
    ics_pick_row(srv, row);
    ics_finish(srv);
}

static void
ics_oploc(struct ToriRSServer* srv, int trigger, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, trigger, loc_id, -1, loc_slot);
    ics_finish(srv);
}

static void
ics_reset_progress(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ics_set_bit(srv, "ics_little_var", ICS_NOT_STARTED);
    ics_set_bit(srv, "ics_givensphinxstatue", 0);
    ics_set_bit(srv, "ics_little_jar_multi", 0);
    ics_set_bit(srv, "ics_metembalmer", 0);
    ics_set_bit(srv, "ics_gotsalt", 0);
    ics_set_bit(srv, "ics_gotsap", 0);
    ics_set_bit(srv, "ics_gotlinen", 0);
    ics_set_bit(srv, "ics_little_carpenter_multi", 0);
    ics_set_bit(srv, "ics_metcarpenter", 0);
    ics_set_bit(srv, "ics_suntrapgotsalt", 0);
    ics_set_varp(player, "fluffs", FLUFFS_COMPLETE);
    ics_clear_inv(player);
    ics_god(player);
}

static int
ics_find_npc(struct ToriRSServer* srv, int npc_type)
{
    int i;

    assert(srv);
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type )
            return i;
    return -1;
}

static void
selftest_quest_icthlarin(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_thieving;
    int stat_agility;
    int stat_wc;
    int npc_wanderer;
    int npc_sphinx;
    int npc_hipriest;
    int npc_hipriest_cer;
    int npc_embalmer;
    int npc_linen;
    int npc_carpenter;
    int npc_het;
    int npc_possessed;
    int loc_rock;
    int loc_door;
    int loc_pit_to;
    int loc_pit_from;
    int loc_west;
    int loc_ladder;
    int loc_pot_liver;
    int loc_water;
    int loc_suntrap;
    int loc_evergreen;
    int loc_east;
    int loc_sarc;
    int obj_kitten;
    int obj_tinderbox;
    int obj_waterskin;
    int obj_jar_liver;
    int obj_token;
    int obj_coins;
    int obj_bucket;
    int obj_saltwater;
    int obj_salt;
    int obj_knife;
    int obj_sap;
    int obj_linen;
    int obj_willow;
    int obj_holy;
    int obj_unholy;
    int obj_amulet;
    int slot;
    int loc_slot;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: icthlarin critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer icthlarin selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    ics_god(player);

    stat_thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_wc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    npc_wanderer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_redheadlady");
    npc_sphinx = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_sphinx");
    npc_hipriest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_hipriest_town");
    npc_hipriest_cer =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_hipriest_ceremony");
    npc_embalmer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_embalmer");
    npc_linen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_linen1");
    npc_carpenter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_carpenter");
    npc_het = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_het");
    npc_possessed =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_possessedpriest");
    loc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "icthal_entrance_open");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "icthalarins_temple_door");
    loc_pit_to = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ics_little_pit_to");
    loc_pit_from = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ics_little_pit_from");
    loc_west =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "icthalarins_ancient_temple_door_1");
    loc_ladder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ics_ladder");
    loc_pot_liver = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ics_little_pot_liver");
    loc_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "icthalarins_waters_edge");
    loc_suntrap =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "icthalarins_suntrap_centre");
    loc_evergreen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "evergreen");
    loc_east =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "icthalarins_ancient_temple_door_2");
    loc_sarc =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "deserttreasure_sarcophigi_wall");
    obj_kitten = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject");
    obj_tinderbox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_waterskin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "water_skin4");
    obj_jar_liver =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_canopic_jar_liver");
    obj_token = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_sphinxstatue");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
    obj_saltwater =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_saltwaterbucket");
    obj_salt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_pileofsalt");
    obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
    obj_sap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_sap_bucket");
    obj_linen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_linen");
    obj_willow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "willow_logs");
    obj_holy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_holy_symbol");
    obj_unholy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_unholy_symbol");
    obj_amulet =
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_amulet_of_catspeak");

    SELFTEST_CHECK(npc_wanderer > 0, "npc ics_little_redheadlady should resolve");
    SELFTEST_CHECK(npc_sphinx > 0, "npc ics_little_sphinx should resolve");
    SELFTEST_CHECK(npc_hipriest > 0, "npc ics_little_hipriest_town should resolve");
    SELFTEST_CHECK(npc_embalmer > 0, "npc ics_little_embalmer should resolve");
    SELFTEST_CHECK(npc_linen > 0, "npc ics_little_linen1 should resolve");
    SELFTEST_CHECK(npc_carpenter > 0, "npc ics_little_carpenter should resolve");
    SELFTEST_CHECK(npc_het > 0, "npc ics_little_het should resolve");
    SELFTEST_CHECK(npc_possessed > 0, "npc ics_little_possessedpriest should resolve");
    SELFTEST_CHECK(loc_door >= 0, "loc icthalarins_temple_door should resolve");
    SELFTEST_CHECK(loc_west >= 0, "loc icthalarins_ancient_temple_door_1 should resolve");
    SELFTEST_CHECK(loc_pot_liver >= 0, "loc ics_little_pot_liver should resolve");
    SELFTEST_CHECK(loc_east >= 0, "loc icthalarins_ancient_temple_door_2 should resolve");
    SELFTEST_CHECK(obj_kitten > 0, "obj kittenobject should resolve");
    SELFTEST_CHECK(obj_tinderbox > 0, "obj tinderbox should resolve");
    SELFTEST_CHECK(obj_waterskin > 0, "obj water_skin4 should resolve");
    SELFTEST_CHECK(obj_amulet > 0, "obj ics_little_amulet_of_catspeak should resolve");
    SELFTEST_CHECK(stat_thieving >= 0, "stat thieving should resolve");
    if( npc_wanderer <= 0 )
    {
        fprintf(stderr, "ToriRSServer icthlarin selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    ics_reset_progress(srv, player);

    /* ---- Wanderer: no-cat greeting, refuse, need-supplies, hypnosis ---- */
    slot = ics_spawn(srv, npc_wanderer, ICS_WANDERER_X, ICS_WANDERER_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Wanderer should spawn");
    if( slot >= 0 )
    {
        ics_talk_finish(srv, npc_wanderer, slot);
        SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_NOT_STARTED,
                       "no-cat greeting must not start the quest, got %d",
                       ics_get_bit(player, "ics_little_var"));
        ics_pass("opnpc1_wanderer_no_cat");

        ics_give(player, obj_kitten, 1);
        ics_talk(srv, npc_wanderer, slot);
        ics_click_until_menu(srv, 8);
        ics_pick_row(srv, 3);
        ics_finish(srv);
        SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_NOT_STARTED,
                       "Never mind must leave the quest unstarted");
        ics_pass("opnpc1_wanderer_never_mind");

        ics_talk(srv, npc_wanderer, slot);
        ics_click_until_menu(srv, 8);
        ics_pick_row(srv, 2);
        ics_finish(srv);
        SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_NOT_STARTED,
                       "I'll put it away must leave the quest unstarted");
        ics_pass("opnpc1_wanderer_put_away");

        ics_talk(srv, npc_wanderer, slot);
        ics_click_until_menu(srv, 8);
        ics_pick_row(srv, 1);
        ics_finish(srv);
        SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_NEED_SUPPLIES,
                       "accept without kit should ask for supplies, got %d",
                       ics_get_bit(player, "ics_little_var"));
        ics_pass("opnpc1_wanderer_need_supplies");

        ics_talk_finish(srv, npc_wanderer, slot);
        SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_NEED_SUPPLIES,
                       "reminder must stay on need_supplies");
        ics_pass("opnpc1_wanderer_supplies_reminder");

        ics_give(player, obj_tinderbox, 1);
        ics_give(player, obj_waterskin, 1);
        ics_talk(srv, npc_wanderer, slot);
        ics_click_until_menu(srv, 8);
        ics_pick_row(srv, 1);
        ics_finish(srv);
        SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_ENTERED_CITY,
                       "hypnosis should enter Sophanem, got %d",
                       ics_get_bit(player, "ics_little_var"));
        SELFTEST_CHECK(obj_jar_liver <= 0 || ics_inv_total(player, obj_jar_liver) > 0,
                       "hypnosis should grant the Het liver jar");
        SELFTEST_CHECK(ics_inv_total(player, obj_tinderbox) == 0,
                       "hypnosis consumes the tinderbox");
        SELFTEST_CHECK(ics_inv_total(player, obj_waterskin) == 0,
                       "hypnosis consumes the waterskin");
        ics_pass("opnpc1_wanderer_hypnosis");

        ics_talk_finish(srv, npc_wanderer, slot);
        SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_ENTERED_CITY,
                       "post-hypnosis Wanderer should dismiss the player");
        ics_pass("opnpc1_wanderer_go_away");
        ics_free_npc(srv, slot);
    }

    if( loc_rock >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_rock, ICS_WANDERER_X, ICS_WANDERER_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_rock, loc_slot);
            ics_pass("oploc1_rock_entrance");
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,icthlarin_journal]", NULL, 0);
    ics_finish(srv);
    ics_pass("proc_journal_entered_city");

    /* ---- Pyramid first memory: door, pit, west door soft puzzle ---- */
    if( loc_door >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_door, ICS_PYRAMID_DOOR_X, ICS_PYRAMID_DOOR_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_NOT_STARTED);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_door, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_NOT_STARTED,
                           "temple door before start must refuse");
            ics_pass("oploc1_temple_door_closed");

            ics_set_bit(srv, "ics_little_var", ICS_ENTERED_CITY);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_door, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_FIRST_MEMORY,
                           "opening the temple door starts the first memory, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("oploc1_temple_door_enter");
        }
    }

    if( loc_pit_to >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_pit_to, ICS_PIT_X, ICS_PIT_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_oploc(srv, SS_TRIGGER_OPLOC2, loc_pit_to, loc_slot);
            ics_pass("oploc2_pit_jump_north");
        }
    }
    if( loc_pit_from >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_pit_from, ICS_PIT_X, ICS_PIT_Z + 3, 0);
        if( loc_slot >= 0 )
        {
            ics_oploc(srv, SS_TRIGGER_OPLOC2, loc_pit_from, loc_slot);
            ics_pass("oploc2_pit_jump_south");
        }
    }

    if( loc_west >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_west, ICS_WEST_DOOR_X, ICS_WEST_DOOR_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_NOT_STARTED);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_west, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_NOT_STARTED,
                           "west door before first memory must refuse");
            ics_pass("oploc1_west_door_closed");

            ics_set_bit(srv, "ics_little_var", ICS_FIRST_MEMORY);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_west, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_SPHINX,
                           "west door soft puzzle should wake the player at the Sphinx, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("oploc1_west_door_puzzle");
        }
    }

    if( loc_ladder >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_ladder, ICS_PYRAMID_DOOR_X, ICS_PYRAMID_DOOR_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_ladder, loc_slot);
            ics_pass("oploc1_pyramid_ladder");
        }
    }

    /* ---- Sphinx: no-cat, wrong answer + reconsider, correct 9 ---- */
    if( npc_sphinx > 0 )
    {
        slot = ics_spawn(srv, npc_sphinx, ICS_SPHINX_X, ICS_SPHINX_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_NOT_STARTED);
            ics_talk_finish(srv, npc_sphinx, slot);
            ics_pass("opnpc1_sphinx_no_interest");

            ics_set_bit(srv, "ics_little_var", ICS_SPHINX);
            ics_clear_inv(player);
            ics_talk_finish(srv, npc_sphinx, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_SPHINX,
                           "Sphinx without a cat must refuse");
            ics_pass("opnpc1_sphinx_no_cat");

            ics_give(player, obj_kitten, 1);
            ics_talk(srv, npc_sphinx, slot);
            ics_click_until_menu(srv, 12);
            ics_pick_row(srv, 5);
            ics_finish(srv);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_SPHINX,
                           "I don't know must leave the riddle open");
            ics_pass("opnpc1_sphinx_dont_know");

            ics_talk(srv, npc_sphinx, slot);
            ics_click_until_menu(srv, 12);
            ics_pick_row(srv, 1);
            ics_click_until_menu(srv, 8);
            ics_pick_row(srv, 2);
            ics_finish(srv);
            SELFTEST_CHECK(ics_inv_total(player, obj_kitten) > 0,
                           "reconsider must keep the cat");
            ics_pass("opnpc1_sphinx_reconsider");

            ics_talk(srv, npc_sphinx, slot);
            ics_click_until_menu(srv, 12);
            ics_pick_row(srv, 1);
            ics_click_until_menu(srv, 8);
            ics_pick_row(srv, 1);
            ics_finish(srv);
            SELFTEST_CHECK(ics_inv_total(player, obj_kitten) == 0,
                           "confirmed wrong answer should take the cat");
            ics_pass("opnpc1_sphinx_take_cat");

            ics_give(player, obj_kitten, 1);
            ics_talk(srv, npc_sphinx, slot);
            ics_click_until_menu(srv, 12);
            ics_pick_row(srv, 2);
            ics_finish(srv);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_HIGH_PRIEST,
                           "answer 9 should grant the token, got %d",
                           ics_get_bit(player, "ics_little_var"));
            SELFTEST_CHECK(obj_token <= 0 || ics_inv_total(player, obj_token) > 0,
                           "Sphinx should grant ics_little_sphinxstatue");
            ics_pass("opnpc1_sphinx_correct_nine");

            ics_talk_finish(srv, npc_sphinx, slot);
            ics_pass("opnpc1_sphinx_already_token");
            ics_free_npc(srv, slot);
        }
    }

    /* ---- Town High Priest: no token, token handoff, later rungs ---- */
    if( npc_hipriest > 0 )
    {
        slot = ics_spawn(srv, npc_hipriest, ICS_HIPRIEST_X, ICS_HIPRIEST_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_NOT_STARTED);
            ics_talk_finish(srv, npc_hipriest, slot);
            ics_pass("opnpc1_hipriest_plague_busy");

            ics_set_bit(srv, "ics_little_var", ICS_HIGH_PRIEST);
            ics_set_bit(srv, "ics_givensphinxstatue", 0);
            ics_clear_inv(player);
            ics_talk_finish(srv, npc_hipriest, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_HIGH_PRIEST,
                           "High Priest without the token must wait");
            ics_pass("opnpc1_hipriest_bring_token");

            if( obj_token > 0 )
                ics_give(player, obj_token, 1);
            ics_talk_finish(srv, npc_hipriest, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_RETURN_JAR,
                           "token handoff should send the player for the jar, got %d",
                           ics_get_bit(player, "ics_little_var"));
            SELFTEST_CHECK(ics_get_bit(player, "ics_givensphinxstatue") == 1,
                           "token handoff should set ics_givensphinxstatue");
            ics_pass("opnpc1_hipriest_token_handoff");

            ics_talk_finish(srv, npc_hipriest, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_RETURN_JAR,
                           "mid-jar High Priest should wait for the jar");
            ics_pass("opnpc1_hipriest_waiting_jar");
            ics_free_npc(srv, slot);
        }
    }

    /* ---- Canopic jar: refuse, Apparition, take, wrong pot, place ---- */
    if( loc_pot_liver >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_pot_liver, ICS_JAR_X, ICS_JAR_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_HIGH_PRIEST);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_pot_liver, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_HIGH_PRIEST,
                           "pot before return_jar must refuse");
            ics_pass("oploc1_pot_too_soon");

            ics_set_bit(srv, "ics_little_var", ICS_RETURN_JAR);
            ics_set_bit(srv, "ics_little_jar_multi", 0);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_pot_liver, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_JAR_GUARDIAN,
                           "first Take should spawn the Apparition, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("oploc1_pot_apparition");

            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_pot_liver, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_JAR_GUARDIAN,
                           "taking again before the kill must refuse");
            ics_pass("oploc1_pot_defeat_first");

            if( npc_het > 0 )
            {
                int het = ics_find_npc(srv, npc_het);
                if( het < 0 )
                    het = ToriRSServer_WorldNpcSpawn(
                        srv, npc_het, ICS_JAR_X + 1, ICS_JAR_Z, 0);
                if( het >= 0 )
                {
                    struct ToriRSServerNpc* npc = &srv->npcs[het];
                    int waited;

                    ics_god(player);
                    ToriRSServer_CombatHitNpc(srv, het, 0, npc->hitpoints);
                    for( waited = 0; waited < 40 &&
                         ics_get_bit(player, "ics_little_var") != ICS_JAR_KILLED;
                         waited++ )
                        selftest_tick(srv);
                    SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_JAR_KILLED,
                                   "killing the Apparition should fade it, got %d",
                                   ics_get_bit(player, "ics_little_var"));
                    ics_pass("ai_queue3_apparition_het");
                    ics_free_npc(srv, het);
                }
            }

            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_pot_liver, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_PLACE_JAR,
                           "second Take should grant the jar, got %d",
                           ics_get_bit(player, "ics_little_var"));
            SELFTEST_CHECK(obj_jar_liver <= 0 || ics_inv_total(player, obj_jar_liver) > 0,
                           "Take after the kill should add the liver jar");
            ics_pass("oploc1_pot_take_jar");

            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_pot_liver, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_JAR_DONE,
                           "placing the matching jar should finish the chamber, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("oploc1_pot_place_jar");

            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_pot_liver, loc_slot);
            ics_pass("oploc1_pot_already_placed");
        }
    }

    if( npc_hipriest > 0 )
    {
        slot = ics_spawn(srv, npc_hipriest, ICS_HIPRIEST_X, ICS_HIPRIEST_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_JAR_DONE);
            ics_talk_finish(srv, npc_hipriest, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_EMBALM,
                           "returned-jar report should start embalming, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("opnpc1_hipriest_jar_done");

            ics_talk_finish(srv, npc_hipriest, slot);
            ics_pass("opnpc1_hipriest_still_need_help");
            ics_free_npc(srv, slot);
        }
    }

    /* ---- Embalmer / Raetul / salt / sap / Carpenter ---- */
    if( npc_embalmer > 0 )
    {
        slot = ics_spawn(srv, npc_embalmer, ICS_EMBALMER_X, ICS_EMBALMER_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_NOT_STARTED);
            ics_talk_finish(srv, npc_embalmer, slot);
            ics_pass("opnpc1_embalmer_busy");

            ics_set_bit(srv, "ics_little_var", ICS_EMBALM);
            ics_set_bit(srv, "ics_metembalmer", 0);
            ics_set_bit(srv, "ics_gotsalt", 0);
            ics_set_bit(srv, "ics_gotsap", 0);
            ics_set_bit(srv, "ics_gotlinen", 0);
            ics_talk_finish(srv, npc_embalmer, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_metembalmer") == 1,
                           "first Embalmer talk should introduce the materials");
            ics_pass("opnpc1_embalmer_need_materials");
            ics_free_npc(srv, slot);
        }
    }

    if( npc_linen > 0 )
    {
        slot = ics_spawn(srv, npc_linen, ICS_LINEN_X, ICS_LINEN_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_EMBALM);
            ics_clear_inv(player);
            ics_talk_finish(srv, npc_linen, slot);
            ics_pass("opnpc1_raetul_price");

            ics_talk(srv, npc_linen, slot);
            ics_click_until_menu(srv, 8);
            ics_pick_row(srv, 2);
            ics_finish(srv);
            SELFTEST_CHECK(obj_linen <= 0 || ics_inv_total(player, obj_linen) == 0,
                           "declining linen must not grant a piece");
            ics_pass("opnpc1_raetul_no_thanks");

            ics_talk(srv, npc_linen, slot);
            ics_click_until_menu(srv, 8);
            ics_pick_row(srv, 1);
            ics_finish(srv);
            SELFTEST_CHECK(obj_linen <= 0 || ics_inv_total(player, obj_linen) == 0,
                           "linen without coins must refuse");
            ics_pass("opnpc1_raetul_no_coins");

            if( obj_coins > 0 )
                ics_give(player, obj_coins, ICS_LINEN_COST);
            ics_talk(srv, npc_linen, slot);
            ics_click_until_menu(srv, 8);
            ics_pick_row(srv, 1);
            ics_finish(srv);
            SELFTEST_CHECK(obj_linen <= 0 || ics_inv_total(player, obj_linen) > 0,
                           "buying linen should grant ics_little_linen");
            ics_pass("opnpc1_raetul_buy_linen");
            ics_free_npc(srv, slot);
        }
    }

    if( loc_water >= 0 && obj_bucket > 0 && obj_saltwater > 0 )
    {
        loc_slot = ics_place_loc(srv, loc_water, ICS_WATER_X, ICS_WATER_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_clear_inv(player);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_water, loc_slot);
            SELFTEST_CHECK(ics_inv_total(player, obj_saltwater) == 0,
                           "lake without a bucket must refuse");
            ics_pass("oploc1_water_need_bucket");

            ics_give(player, obj_bucket, 1);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_water, loc_slot);
            SELFTEST_CHECK(ics_inv_total(player, obj_saltwater) > 0,
                           "Collect should fill ics_little_saltwaterbucket");
            ics_pass("oploc1_water_fill_saltwater");
        }
    }

    if( loc_suntrap >= 0 && obj_saltwater > 0 && obj_salt > 0 )
    {
        loc_slot = ics_place_loc(srv, loc_suntrap, ICS_SUNTRAP_X, ICS_SUNTRAP_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_suntrap, loc_slot);
            ics_pass("oploc1_suntrap_prompt");

            if( ics_inv_total(player, obj_saltwater) == 0 )
                ics_give(player, obj_saltwater, 1);
            player->last_useitem = obj_saltwater;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_suntrap, -1, loc_slot);
            ics_finish(srv);
            player->last_useitem = -1;
            SELFTEST_CHECK(ics_inv_total(player, obj_salt) > 0,
                           "suntrap should leave a pile of salt");
            ics_pass("oplocu_suntrap_evaporate");
        }
    }

    if( loc_evergreen >= 0 && obj_knife > 0 && obj_bucket > 0 && obj_sap > 0 )
    {
        loc_slot = ics_place_loc(srv, loc_evergreen, ICS_EVERGREEN_X, ICS_EVERGREEN_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_EMBALM);
            ics_clear_inv(player);
            ics_give(player, obj_knife, 1);
            player->last_useitem = obj_knife;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_evergreen, -1, loc_slot);
            ics_finish(srv);
            player->last_useitem = -1;
            SELFTEST_CHECK(ics_inv_total(player, obj_sap) == 0,
                           "evergreen without a bucket must refuse");
            ics_pass("oplocu_evergreen_need_bucket");

            ics_give(player, obj_bucket, 1);
            player->last_useitem = obj_knife;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_evergreen, -1, loc_slot);
            ics_finish(srv);
            player->last_useitem = -1;
            SELFTEST_CHECK(ics_inv_total(player, obj_sap) > 0,
                           "knife+bucket on evergreen should fill sap");
            ics_pass("oplocu_evergreen_tap_sap");
        }
    }

    if( npc_embalmer > 0 )
    {
        slot = ics_spawn(srv, npc_embalmer, ICS_EMBALMER_X, ICS_EMBALMER_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_EMBALM);
            ics_set_bit(srv, "ics_gotsalt", 0);
            ics_set_bit(srv, "ics_gotsap", 0);
            ics_set_bit(srv, "ics_gotlinen", 0);
            if( obj_salt > 0 )
                ics_give(player, obj_salt, 1);
            if( obj_sap > 0 )
                ics_give(player, obj_sap, 1);
            if( obj_linen > 0 )
                ics_give(player, obj_linen, 1);
            ics_talk_finish(srv, npc_embalmer, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_gotsalt") == 1, "Embalmer should take salt");
            SELFTEST_CHECK(ics_get_bit(player, "ics_gotsap") == 1, "Embalmer should take sap");
            SELFTEST_CHECK(ics_get_bit(player, "ics_gotlinen") == 1, "Embalmer should take linen");
            ics_pass("opnpc1_embalmer_handin_all");
            ics_free_npc(srv, slot);
        }
    }

    if( npc_carpenter > 0 )
    {
        slot = ics_spawn(srv, npc_carpenter, ICS_CARPENTER_X, ICS_CARPENTER_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_NOT_STARTED);
            ics_talk_finish(srv, npc_carpenter, slot);
            ics_pass("opnpc1_carpenter_plague");

            ics_set_bit(srv, "ics_little_var", ICS_EMBALM);
            ics_set_bit(srv, "ics_little_carpenter_multi", 0);
            ics_clear_inv(player);
            ics_talk_finish(srv, npc_carpenter, slot);
            ics_pass("opnpc1_carpenter_need_willow");

            if( obj_willow > 0 )
                ics_give(player, obj_willow, 1);
            ics_talk(srv, npc_carpenter, slot);
            ics_click_until_menu(srv, 8);
            ics_pick_row(srv, 2);
            ics_finish(srv);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_carpenter_multi") == 0,
                           "I'll get some must not consume the logs");
            ics_pass("opnpc1_carpenter_ill_get_some");

            ics_talk(srv, npc_carpenter, slot);
            ics_click_until_menu(srv, 8);
            ics_pick_row(srv, 1);
            ics_finish(srv);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_carpenter_multi") == 1,
                           "handing willow logs should carve the symbol");
            SELFTEST_CHECK(obj_holy <= 0 || ics_inv_total(player, obj_holy) > 0,
                           "Carpenter should grant ics_little_holy_symbol");
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_RITUAL,
                           "salt+sap+linen+symbol should advance to ritual, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("opnpc1_carpenter_holy_symbol");

            ics_talk_finish(srv, npc_carpenter, slot);
            ics_pass("opnpc1_carpenter_take_into_pyramid");
            ics_free_npc(srv, slot);
        }
    }

    if( npc_hipriest > 0 )
    {
        slot = ics_spawn(srv, npc_hipriest, ICS_HIPRIEST_X, ICS_HIPRIEST_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_RITUAL);
            ics_talk_finish(srv, npc_hipriest, slot);
            ics_pass("opnpc1_hipriest_take_symbol");
            ics_free_npc(srv, slot);
        }
    }

    /* ---- Ceremony: east door, sarcophagus, possessed priest, finale ---- */
    if( loc_east >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_east, ICS_EAST_DOOR_X, ICS_EAST_DOOR_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_EMBALM);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_east, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_EMBALM,
                           "east door before ritual must refuse");
            ics_pass("oploc1_east_door_closed");

            ics_set_bit(srv, "ics_little_var", ICS_RITUAL);
            ics_clear_inv(player);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_east, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_RITUAL,
                           "east door without the symbol must refuse");
            ics_pass("oploc1_east_door_need_symbol");

            if( obj_holy > 0 )
                ics_give(player, obj_holy, 1);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_east, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_PLACE_SYMBOL,
                           "east door with the holy symbol should enter, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("oploc1_east_door_enter");
        }
    }

    if( loc_sarc >= 0 && obj_holy > 0 && obj_unholy > 0 )
    {
        loc_slot = ics_place_loc(srv, loc_sarc, ICS_SARC_X, ICS_SARC_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_PLACE_SYMBOL);
            if( ics_inv_total(player, obj_holy) == 0 )
                ics_give(player, obj_holy, 1);
            player->last_useitem = obj_holy;
            ToriRSServer_ScriptsRunTriggerOnLoc(
                srv, SS_TRIGGER_OPLOCU, loc_sarc, -1, loc_slot);
            ics_finish(srv);
            player->last_useitem = -1;
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_SYMBOL_PLACED,
                           "using the holy symbol should twist it, got %d",
                           ics_get_bit(player, "ics_little_var"));
            SELFTEST_CHECK(ics_inv_total(player, obj_unholy) > 0,
                           "sarcophagus should replace holy with unholy");
            ics_pass("oplocu_sarc_holy_to_unholy");
        }
    }

    if( loc_east >= 0 )
    {
        loc_slot = ics_place_loc(srv, loc_east, ICS_EAST_DOOR_X, ICS_EAST_DOOR_Z, 0);
        if( loc_slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_SYMBOL_PLACED);
            ics_oploc(srv, SS_TRIGGER_OPLOC1, loc_east, loc_slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_POSSESSED,
                           "re-entering should spawn the possessed priest, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("oploc1_east_door_possessed");
        }
    }

    if( npc_possessed > 0 )
    {
        int poss = ics_find_npc(srv, npc_possessed);
        if( poss < 0 )
            poss = ToriRSServer_WorldNpcSpawn(
                srv, npc_possessed, ICS_EAST_DOOR_X + 1, ICS_EAST_DOOR_Z, 0);
        if( poss >= 0 )
        {
            struct ToriRSServerNpc* npc = &srv->npcs[poss];
            int waited;

            ics_god(player);
            ics_set_bit(srv, "ics_little_var", ICS_POSSESSED);
            ToriRSServer_CombatHitNpc(srv, poss, 0, npc->hitpoints);
            for( waited = 0; waited < 40 &&
                 ics_get_bit(player, "ics_little_var") != ICS_MEET_GOD;
                 waited++ )
                selftest_tick(srv);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_MEET_GOD,
                           "defeating the possessed priest should open the ceremony, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("ai_queue3_possessed_priest");
            ics_free_npc(srv, poss);
        }
    }

    if( npc_hipriest_cer > 0 )
    {
        slot = ics_spawn(srv, npc_hipriest_cer, ICS_EAST_DOOR_X, ICS_EAST_DOOR_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_RITUAL);
            ics_talk_finish(srv, npc_hipriest_cer, slot);
            ics_pass("opnpc1_ceremony_hp_not_ready");

            ics_set_bit(srv, "ics_little_var", ICS_MEET_GOD);
            ics_talk_finish(srv, npc_hipriest_cer, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_FINISH_TALK,
                           "ceremony High Priest should send the player outside, got %d",
                           ics_get_bit(player, "ics_little_var"));
            ics_pass("opnpc1_ceremony_hp_saved_ritual");
            ics_free_npc(srv, slot);
        }
    }

    /* ---- Authored complete scroll at the town High Priest ---- */
    if( npc_hipriest > 0 )
    {
        int xp_thieve = 0;
        int xp_agility = 0;
        int xp_wc = 0;

        slot = ics_spawn(srv, npc_hipriest, ICS_HIPRIEST_X, ICS_HIPRIEST_Z, 0);
        if( slot >= 0 )
        {
            ics_set_bit(srv, "ics_little_var", ICS_FINISH_TALK);
            ics_clear_inv(player);
            if( stat_thieving >= 0 )
                xp_thieve = player->stat_xp_tenths[stat_thieving];
            if( stat_agility >= 0 )
                xp_agility = player->stat_xp_tenths[stat_agility];
            if( stat_wc >= 0 )
                xp_wc = player->stat_xp_tenths[stat_wc];
            ics_talk_finish(srv, npc_hipriest, slot);
            SELFTEST_CHECK(ics_get_bit(player, "ics_little_var") == ICS_COMPLETE,
                           "town High Priest finish must run quest_icthlarinslittlehelper rewards, got %d",
                           ics_get_bit(player, "ics_little_var"));
            SELFTEST_CHECK(obj_amulet <= 0 || ics_inv_total(player, obj_amulet) > 0,
                           "completion should grant ics_little_amulet_of_catspeak");
            if( stat_thieving >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_thieving] >= xp_thieve + ICS_THIEVE_XP,
                               "completion should award 4500 Thieving XP, %d -> %d",
                               xp_thieve, player->stat_xp_tenths[stat_thieving]);
            if( stat_agility >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_agility] >= xp_agility + ICS_AGILITY_XP,
                               "completion should award 4000 Agility XP");
            if( stat_wc >= 0 )
                SELFTEST_CHECK(player->stat_xp_tenths[stat_wc] >= xp_wc + ICS_WC_XP,
                               "completion should award 4000 Woodcutting XP");
            ics_pass("opnpc1_hipriest_complete");
            ics_free_npc(srv, slot);
        }
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,icthlarin_journal]", NULL, 0);
    ics_finish(srv);
    ics_pass("proc_journal_complete");

    ics_reset_progress(srv, player);
    ics_god(player);
    fprintf(stderr, "ToriRSServer icthlarin selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_ICTHLARIN_SELFTEST_U_H */
