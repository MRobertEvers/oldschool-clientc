#ifndef TORIRSSERVER_TEST_QUEST_ONESMALLFAVOUR_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ONESMALLFAVOUR_SELFTEST_U_H

/* One Small Favour Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned npcs cannot leak. Real OPNPC / OPLOC /
 * OPHELD / OPLOCU on the critical path. player->godmode = 1 for the whole
 * walk (Jungle/Slagilith fights are not death tests). Completion goes
 * through Yanni's authored ~quest_complete_rewards. Additive OSF branches
 * only — do not rewrite Eadgar/Sanfew, Chompy/Rantz, RM, Druidic Ritual,
 * Shilo, Construction, MTA, Observatory, Horror, Viking, Mort'ton, MM,
 * Myreque, or Roving Elves. */

#define OSF_NOT_STARTED 0
#define OSF_FORESTER_AXE 5
#define OSF_AXE_TO_BRIAN 10
#define OSF_AGGIE_AGREED 20
#define OSF_JOHANHUS_TOLD 25
#define OSF_FRED_TOLD 45
#define OSF_SETH_TOLD 50
#define OSF_HORVIK_TOLD 55
#define OSF_APOTH_TOLD 60
#define OSF_TASSIE_TOLD 65
#define OSF_HAMMERSPIKE_TOLD 70
#define OSF_SANFEW_TOLD 75
#define OSF_BREWING_TEA 80
#define OSF_BLEEMADGE_WANTS_TRASH 86
#define OSF_ARHEIN_TOLD 88
#define OSF_PHANTUWTI_TOLD 90
#define OSF_WALL_FOUND 95
#define OSF_CROMPERTY_TOLD 100
#define OSF_TINDEL_TOLD 105
#define OSF_RANTZ_TOLD 110
#define OSF_GNORMADIUM_TOLD 115
#define OSF_LIGHTS_FIXED 120
#define OSF_GNORMADIUM_DONE 125
#define OSF_RANTZ_DONE 130
#define OSF_TINDEL_DONE 135
#define OSF_CROMPERTY_DONE 140
#define OSF_SLAGILITH_FIGHT 145
#define OSF_SLAGILITH_DEFEATED 150
#define OSF_PETRA_FREED 152
#define OSF_PHANTUWTI_WEATHER 160
#define OSF_VANE_SEARCH 175
#define OSF_VANE_PARTS_TAKEN 177
#define OSF_VANE_REPAIRED 180
#define OSF_PHANTUWTI_VANE_DONE 185
#define OSF_ARHEIN_DONE 190
#define OSF_BLEEMADGE_DONE 195
#define OSF_SANFEW_DONE 200
#define OSF_HAMMERSPIKE_GANG 205
#define OSF_HAMMERSPIKE_DONE 225
#define OSF_TASSIE_DONE 230
#define OSF_POT_MADE 235
#define OSF_HORVIK_DONE 240
#define OSF_SETH_DONE 250
#define OSF_JOHANHUS_DONE 255
#define OSF_AGGIE_DONE 260
#define OSF_BRIAN_DONE 265
#define OSF_FORESTER_DONE 270
#define OSF_YANNI_DONE 275
#define OSF_COMPLETE 285

#define OSF_RM_COMPLETE 6
#define OSF_DRUID_COMPLETE 4

static void
osf_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "OSF PASS: %s\n", step);
}

static void
osf_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
osf_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
osf_finish(struct ToriRSServer* srv)
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
osf_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
osf_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = osf_chatmenu();
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
osf_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = osf_chatmenu();
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
osf_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    osf_god(player);
    selftest_tick(srv);
}

static int
osf_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    osf_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
osf_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
osf_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
osf_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( osf_inv_total(player, obj_id) >= count )
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
osf_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
osf_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
osf_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    osf_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
osf_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
osf_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    osf_talk(srv, npc_type, slot);
    osf_finish(srv);
}

static void
selftest_quest_onesmallfavour(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int varp;
    int varp_rm;
    int varp_druid;
    int npc_yanni;
    int npc_forester;
    int npc_brian;
    int npc_aggie;
    int npc_johanhus;
    int npc_fred;
    int npc_seth;
    int npc_horvik;
    int npc_apoth;
    int npc_tassie;
    int npc_hammerspike;
    int npc_sanfew;
    int npc_bleemadge;
    int npc_arhein;
    int npc_phantuwti;
    int npc_cromperty;
    int npc_tindel;
    int npc_rantz;
    int npc_gnormadium;
    int npc_petra;
    int npc_gang;
    int loc_wall;
    int loc_vane;
    int obj_axe_blunt;
    int obj_axe_sharp;
    int obj_mahogany;
    int obj_tea;
    int obj_report;
    int obj_oxide;
    int obj_scroll;
    int obj_stodgy;
    int obj_comfy;
    int obj_dir_broken;
    int obj_orn_broken;
    int obj_pil_broken;
    int obj_dir_fixed;
    int obj_orn_fixed;
    int obj_pil_fixed;
    int obj_pot;
    int obj_lid;
    int obj_airtight;
    int obj_salts;
    int obj_tincture;
    int obj_cage;
    int obj_hammer;
    int obj_steel;
    int obj_bronze;
    int obj_iron;
    int obj_keyring;
    int loaded;
    int checks_before;
    int fails_before;
    int yanni_slot = -1;
    int slot;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: onesmallfavour critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer osf selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    osf_god(player);

    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "onesmallfavour");
    varp_rm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "runemysteries");
    varp_druid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "druidquest");
    npc_yanni = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "shiloantiques");
    npc_forester = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "jungleforester_m");
    npc_brian = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brian");
    npc_aggie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "aggie");
    npc_johanhus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "favour_johanhus_ulsbrecht");
    npc_fred = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "fred_the_farmer");
    npc_seth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "favour_seth_groats");
    npc_horvik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "horvik_the_armourer");
    npc_apoth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "apothecary");
    npc_tassie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "favour_tassie_slipcast");
    npc_hammerspike = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "favour_hammerspike_stoutbeard");
    npc_sanfew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sanfew");
    npc_bleemadge = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "pilot_white_wolf_base");
    npc_arhein = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "arhein");
    npc_phantuwti = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "favour_phantuwti_farsight");
    npc_cromperty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "cromperty_pre_diary");
    if( npc_cromperty <= 0 )
        npc_cromperty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ardounge_wizard");
    npc_tindel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tindel_marchant");
    npc_rantz = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rantz");
    npc_gnormadium = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gnormadium_avlafrim");
    npc_petra = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "favour_petra");
    npc_gang = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "favour_gangster_dwarf");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "favour_lady_in_wall");
    loc_vane = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "osf_weathervane");
    obj_axe_blunt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_jungleforesteraxe_blunt");
    obj_axe_sharp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_jungleforesteraxe_sharp");
    obj_mahogany = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_mahogany_log");
    obj_tea = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cup_guthix_rest_3");
    obj_report = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_weather_report");
    obj_oxide = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_iron_oxide");
    obj_scroll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_animate_rock");
    obj_stodgy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_matress_stodgy");
    obj_comfy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_matress_comfy");
    obj_dir_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_directionals_broken");
    obj_orn_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_ornament_broken");
    obj_pil_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_pillar_broken");
    obj_dir_fixed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_directionals_fixed");
    obj_orn_fixed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_ornament_fixed");
    obj_pil_fixed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_pillar_fixed");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    obj_lid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "potlid");
    obj_airtight = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_airtight_pot");
    obj_salts = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_breathing_salts");
    obj_tincture = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_herbal_tincture");
    obj_cage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_chicken_cage");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_steel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "steel_bar");
    obj_bronze = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_bar");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_keyring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "favour_key_ring");

    SELFTEST_CHECK(varp >= 0, "varp onesmallfavour should resolve");
    SELFTEST_CHECK(varp_rm >= 0, "varp runemysteries should resolve");
    SELFTEST_CHECK(varp_druid >= 0, "varp druidquest should resolve");
    SELFTEST_CHECK(npc_yanni > 0, "npc shiloantiques should resolve");
    SELFTEST_CHECK(npc_forester > 0, "npc jungleforester_m should resolve");
    SELFTEST_CHECK(npc_brian > 0, "npc brian should resolve");
    SELFTEST_CHECK(npc_aggie > 0, "npc aggie should resolve");
    SELFTEST_CHECK(npc_johanhus > 0, "npc favour_johanhus_ulsbrecht should resolve");
    SELFTEST_CHECK(npc_tassie > 0, "npc favour_tassie_slipcast should resolve");
    SELFTEST_CHECK(npc_hammerspike > 0, "npc favour_hammerspike_stoutbeard should resolve");
    SELFTEST_CHECK(npc_phantuwti > 0, "npc favour_phantuwti_farsight should resolve");
    SELFTEST_CHECK(npc_gnormadium > 0, "npc gnormadium_avlafrim should resolve");
    SELFTEST_CHECK(loc_wall >= 0, "loc favour_lady_in_wall should resolve");
    SELFTEST_CHECK(loc_vane >= 0, "loc osf_weathervane should resolve");
    if( varp < 0 || npc_yanni <= 0 )
    {
        fprintf(stderr, "ToriRSServer osf selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    osf_clear_inv(player);
    player->varps[varp] = OSF_NOT_STARTED;
    if( varp_rm >= 0 )
        player->varps[varp_rm] = 0;
    if( varp_druid >= 0 )
        player->varps[varp_druid] = 0;
    osf_god(player);

    /* ---- Yanni hard gates: Rune Mysteries, then Druidic Ritual ---- */
    yanni_slot = osf_spawn(srv, npc_yanni, 2836, 2983, 0);
    SELFTEST_CHECK(yanni_slot >= 0, "yanni should spawn");
    if( yanni_slot >= 0 )
    {
        osf_talk_finish(srv, npc_yanni, yanni_slot);
        SELFTEST_CHECK(player->varps[varp] == OSF_NOT_STARTED,
                       "Yanni must refuse without Rune Mysteries, got %d",
                       player->varps[varp]);
        osf_pass("opnpc1_yanni_prereq_rune_mysteries");

        if( varp_rm >= 0 )
            player->varps[varp_rm] = OSF_RM_COMPLETE;
        osf_talk_finish(srv, npc_yanni, yanni_slot);
        SELFTEST_CHECK(player->varps[varp] == OSF_NOT_STARTED,
                       "Yanni must refuse without Druidic Ritual, got %d",
                       player->varps[varp]);
        osf_pass("opnpc1_yanni_prereq_druidic_ritual");

        if( varp_druid >= 0 )
            player->varps[varp_druid] = OSF_DRUID_COMPLETE;
        osf_talk_finish(srv, npc_yanni, yanni_slot);
        SELFTEST_CHECK(player->varps[varp] == OSF_FORESTER_AXE,
                       "accepting Yanni must start OSF at forester_axe, got %d",
                       player->varps[varp]);
        osf_pass("opnpc1_yanni_start");
    }

    /* ---- Jungle forester blunt axe ---- */
    if( npc_forester > 0 )
    {
        slot = osf_spawn(srv, npc_forester, 2861, 2942, 0);
        SELFTEST_CHECK(slot >= 0, "jungle forester should spawn");
        if( slot >= 0 )
        {
            osf_talk_finish(srv, npc_forester, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_AXE_TO_BRIAN,
                           "forester should hand the blunt axe, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_axe_blunt <= 0 || osf_inv_total(player, obj_axe_blunt) > 0,
                           "forester should grant favour_jungleforesteraxe_blunt");
            osf_pass("opnpc1_forester_blunt_axe");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Brian: no-axe refuse, then Aggie ask (journal path) ---- */
    if( npc_brian > 0 )
    {
        int held = (obj_axe_blunt > 0) ? osf_inv_total(player, obj_axe_blunt) : 0;

        slot = osf_spawn(srv, npc_brian, 3027, 3249, 0);
        SELFTEST_CHECK(slot >= 0, "brian should spawn");
        if( slot >= 0 )
        {
            osf_clear_inv(player);
            player->varps[varp] = OSF_AXE_TO_BRIAN;
            osf_talk_finish(srv, npc_brian, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_AXE_TO_BRIAN,
                           "Brian without the axe must refuse, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_brian_no_axe_refuse");

            if( obj_axe_blunt > 0 )
                osf_give(player, obj_axe_blunt, held > 0 ? held : 1);
            osf_talk_finish(srv, npc_brian, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_AGGIE_AGREED,
                           "Brian with the axe should send the player to Aggie, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_brian_axe_to_aggie");
            osf_free_npc(srv, slot);
        }
    }

    /* Isolated Aggie ask at the journal stage (before Brian with-axe). */
    if( npc_aggie > 0 )
    {
        slot = osf_spawn(srv, npc_aggie, 3086, 3258, 0);
        SELFTEST_CHECK(slot >= 0, "aggie should spawn");
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_AXE_TO_BRIAN;
            osf_talk_finish(srv, npc_aggie, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_AGGIE_AGREED,
                           "Aggie should agree to vouch once Jimmy is found, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_aggie_character_witness");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Johanhus: ask, mid-refuse, later chickens ---- */
    if( npc_johanhus > 0 )
    {
        slot = osf_spawn(srv, npc_johanhus, 3171, 9619, 0);
        SELFTEST_CHECK(slot >= 0, "johanhus should spawn");
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_AGGIE_AGREED;
            osf_talk_finish(srv, npc_johanhus, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_JOHANHUS_TOLD,
                           "Johanhus should demand chickens, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_johanhus_jimmy");

            osf_talk_finish(srv, npc_johanhus, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_JOHANHUS_TOLD,
                           "Johanhus mid-relay must refuse without chickens");
            osf_pass("opnpc1_johanhus_no_chickens_refuse");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_fred > 0 )
    {
        slot = osf_spawn(srv, npc_fred, 3190, 3273, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_JOHANHUS_TOLD;
            osf_talk_finish(srv, npc_fred, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_FRED_TOLD,
                           "Fred should point at Seth, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_fred_jimmy");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_seth > 0 )
    {
        slot = osf_spawn(srv, npc_seth, 3228, 3291, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_FRED_TOLD;
            osf_talk_finish(srv, npc_seth, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_SETH_TOLD,
                           "Seth should demand cages, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_seth_chickens");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_horvik > 0 )
    {
        slot = osf_spawn(srv, npc_horvik, 3229, 3437, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_SETH_TOLD;
            osf_talk_finish(srv, npc_horvik, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_HORVIK_TOLD,
                           "Horvik should send the player to the Apothecary, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_horvik_cages");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_apoth > 0 )
    {
        slot = osf_spawn(srv, npc_apoth, 3196, 3404, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_HORVIK_TOLD;
            osf_talk_finish(srv, npc_apoth, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_APOTH_TOLD,
                           "Apothecary should send the player to Tassie, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_apoth_pot");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_tassie > 0 )
    {
        slot = osf_spawn(srv, npc_tassie, 3085, 3409, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_APOTH_TOLD;
            osf_talk_finish(srv, npc_tassie, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_TASSIE_TOLD,
                           "Tassie should send the player to Hammerspike, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_tassie_hammerspike");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_hammerspike > 0 )
    {
        slot = osf_spawn(srv, npc_hammerspike, 2968, 9811, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_TASSIE_TOLD;
            osf_talk_finish(srv, npc_hammerspike, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_HAMMERSPIKE_TOLD,
                           "Hammerspike should send the player to Sanfew, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_hammerspike_sanfew");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_sanfew > 0 )
    {
        slot = osf_spawn(srv, npc_sanfew, 2899, 3429, 1);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_HAMMERSPIKE_TOLD;
            osf_talk_finish(srv, npc_sanfew, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_SANFEW_TOLD,
                           "Sanfew OSF branch should demand a glider, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_sanfew_glider");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Bleemadge: ask, no-tea refuse, tea hand-in ---- */
    if( npc_bleemadge > 0 )
    {
        slot = osf_spawn(srv, npc_bleemadge, 2847, 3498, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_SANFEW_TOLD;
            osf_talk_finish(srv, npc_bleemadge, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_BREWING_TEA,
                           "Bleemadge should demand Guthix rest, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_bleemadge_ask_tea");

            osf_clear_inv(player);
            osf_talk_finish(srv, npc_bleemadge, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_BREWING_TEA,
                           "Bleemadge without tea must refuse");
            osf_pass("opnpc1_bleemadge_no_tea_refuse");

            if( obj_tea > 0 )
                osf_give(player, obj_tea, 1);
            osf_talk_finish(srv, npc_bleemadge, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_BLEEMADGE_WANTS_TRASH,
                           "tea hand-in should demand T.R.A.S.H., got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_tea <= 0 || osf_inv_total(player, obj_tea) == 0,
                           "Bleemadge should consume the tea");
            osf_pass("opnpc1_bleemadge_tea_handin");
            osf_free_npc(srv, slot);
        }
    }

    /* Isolated tea recipe refuse (wrong herbs). */
    {
        int obj_hot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cup_hot_water");
        int obj_guam = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "guam_leaf");

        if( obj_hot > 0 && obj_guam > 0 )
        {
            int held_slot;

            osf_clear_inv(player);
            osf_give(player, obj_hot, 1);
            osf_give(player, obj_guam, 1);
            player->last_useitem = obj_guam;
            for( held_slot = 0; held_slot < TORIRSSERVER_INV_SLOTS; held_slot++ )
                if( player->inv[held_slot].obj_id == obj_hot )
                    break;
            if( held_slot < TORIRSSERVER_INV_SLOTS )
            {
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_hot, -1, held_slot);
                osf_finish(srv);
                SELFTEST_CHECK(osf_inv_total(player, obj_hot) > 0,
                               "wrong tea recipe must leave the cup");
                osf_pass("opheldu_brew_tea_wrong_recipe");
            }
        }
    }

    if( npc_arhein > 0 )
    {
        slot = osf_spawn(srv, npc_arhein, 2804, 3432, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_BLEEMADGE_WANTS_TRASH;
            osf_talk_finish(srv, npc_arhein, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_ARHEIN_TOLD,
                           "Arhein should demand a weather report, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_arhein_trash");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_phantuwti > 0 )
    {
        slot = osf_spawn(srv, npc_phantuwti, 2702, 3473, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_ARHEIN_TOLD;
            osf_talk_finish(srv, npc_phantuwti, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_PHANTUWTI_TOLD,
                           "Phantuwti should send the player after Petra, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_phantuwti_weather");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Sculpture wall ---- */
    if( loc_wall >= 0 )
    {
        int loc_slot = osf_place_loc(srv, loc_wall, 2621, 9835, 0);

        SELFTEST_CHECK(loc_slot >= 0, "favour_lady_in_wall should place");
        player->varps[varp] = OSF_PHANTUWTI_TOLD;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, loc_slot);
        osf_finish(srv);
        SELFTEST_CHECK(player->varps[varp] == OSF_WALL_FOUND,
                       "searching the sculpture should mark the wall found, got %d",
                       player->varps[varp]);
        osf_pass("oploc1_wall_found");

        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_wall, -1, loc_slot);
        osf_finish(srv);
        SELFTEST_CHECK(player->varps[varp] == OSF_WALL_FOUND,
                       "mid-relay wall search must not skip Cromperty");
        osf_pass("oploc1_wall_stares");
    }

    if( npc_cromperty > 0 )
    {
        slot = osf_spawn(srv, npc_cromperty, 2684, 3323, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_WALL_FOUND;
            osf_talk_finish(srv, npc_cromperty, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_CROMPERTY_TOLD,
                           "Cromperty should demand iron oxide, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_cromperty_girl_in_rock");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_tindel > 0 )
    {
        slot = osf_spawn(srv, npc_tindel, 2678, 3153, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_CROMPERTY_TOLD;
            osf_talk_finish(srv, npc_tindel, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_TINDEL_TOLD,
                           "Tindel should demand a mattress, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_tindel_oxide");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_rantz > 0 )
    {
        slot = osf_spawn(srv, npc_rantz, 2631, 2969, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_TINDEL_TOLD;
            osf_talk_finish(srv, npc_rantz, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_RANTZ_TOLD,
                           "Rantz OSF branch should demand Gnormadium's lights, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_stodgy <= 0 || osf_inv_total(player, obj_stodgy) > 0,
                           "Rantz should hand the stodgy mattress");
            osf_pass("opnpc1_rantz_mattress");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Gnormadium: refuse, then collapse-fix the lamps ---- */
    if( npc_gnormadium > 0 )
    {
        slot = osf_spawn(srv, npc_gnormadium, 2542, 2968, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_RANTZ_TOLD;
            osf_talk_finish(srv, npc_gnormadium, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_GNORMADIUM_TOLD,
                           "Gnormadium should ask for the lamps, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_gnormadium_ask");

            osf_talk(srv, npc_gnormadium, slot);
            osf_click_until_menu(srv, 16);
            osf_pick_row(srv, 2);
            osf_finish(srv);
            SELFTEST_CHECK(player->varps[varp] == OSF_GNORMADIUM_TOLD,
                           "refusing the lamps must leave the stage");
            osf_pass("opnpc1_gnormadium_refuse_not_yet");

            osf_talk(srv, npc_gnormadium, slot);
            osf_click_until_menu(srv, 16);
            osf_pick_row(srv, 1);
            osf_finish(srv);
            SELFTEST_CHECK(player->varps[varp] == OSF_GNORMADIUM_DONE,
                           "accepting the lamp collapse should finish Gnormadium, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(osf_get_bit(player, "all_lights_fixed") == 1 ||
                               osf_get_bit(player, "fixedlandinglights") == 255,
                           "landing-light bits must be set after the collapse fix");
            osf_pass("opnpc1_gnormadium_fix_lamps");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Inbound: Rantz / Tindel / Cromperty ---- */
    if( npc_rantz > 0 )
    {
        slot = osf_spawn(srv, npc_rantz, 2631, 2969, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_GNORMADIUM_DONE;
            if( obj_stodgy > 0 )
                osf_give(player, obj_stodgy, 1);
            osf_talk_finish(srv, npc_rantz, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_RANTZ_DONE,
                           "Rantz should stuff the mattress, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_comfy <= 0 || osf_inv_total(player, obj_comfy) > 0,
                           "Rantz should grant the comfy mattress");
            osf_pass("opnpc1_rantz_mattress_handin");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_tindel > 0 )
    {
        slot = osf_spawn(srv, npc_tindel, 2678, 3153, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_RANTZ_DONE;
            if( obj_comfy > 0 )
                osf_give(player, obj_comfy, 1);
            osf_talk_finish(srv, npc_tindel, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_TINDEL_DONE,
                           "Tindel should trade oxide for the mattress, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_oxide <= 0 || osf_inv_total(player, obj_oxide) > 0,
                           "Tindel should grant iron oxide");
            osf_pass("opnpc1_tindel_mattress_handin");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_cromperty > 0 )
    {
        slot = osf_spawn(srv, npc_cromperty, 2684, 3323, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_TINDEL_DONE;
            if( obj_oxide > 0 )
                osf_give(player, obj_oxide, 1);
            osf_talk_finish(srv, npc_cromperty, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_CROMPERTY_DONE,
                           "Cromperty should hand the animate-rock scroll, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_scroll <= 0 || osf_inv_total(player, obj_scroll) > 0,
                           "Cromperty should grant favour_animate_rock");
            osf_pass("opnpc1_cromperty_oxide_handin");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Animate Slagilith, then free Petra ---- */
    if( loc_wall >= 0 && obj_scroll > 0 )
    {
        int loc_slot = osf_place_loc(srv, loc_wall, 2621, 9835, 0);

        player->varps[varp] = OSF_CROMPERTY_DONE;
        osf_give(player, obj_scroll, 1);
        player->last_useitem = obj_scroll;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1, loc_slot);
        osf_finish(srv);
        SELFTEST_CHECK(player->varps[varp] == OSF_SLAGILITH_FIGHT,
                       "reading the scroll should spawn Slagilith, got %d",
                       player->varps[varp]);
        osf_pass("oplocu_wall_animate_slagilith");

        player->varps[varp] = OSF_SLAGILITH_DEFEATED;
        player->last_useitem = obj_scroll;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_wall, -1, loc_slot);
        osf_finish(srv);
        SELFTEST_CHECK(player->varps[varp] == OSF_PETRA_FREED,
                       "second scroll read should free Petra, got %d",
                       player->varps[varp]);
        osf_pass("oplocu_wall_free_petra");
    }

    if( npc_petra > 0 )
    {
        slot = osf_spawn(srv, npc_petra, 2617, 9837, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_PETRA_FREED;
            osf_talk_finish(srv, npc_petra, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_PHANTUWTI_WEATHER,
                           "Petra should send the player back to Phantuwti, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_petra_freed");
        }
    }

    if( npc_phantuwti > 0 )
    {
        slot = osf_spawn(srv, npc_phantuwti, 2702, 3473, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_PHANTUWTI_WEATHER;
            osf_talk_finish(srv, npc_phantuwti, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_VANE_SEARCH,
                           "Phantuwti should send the player to the vane, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_phantuwti_vane_hint");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Weathervane: no hammer, search, slot parts ---- */
    if( loc_vane >= 0 )
    {
        int loc_slot = osf_place_loc(srv, loc_vane, 2702, 3476, 3);

        SELFTEST_CHECK(loc_slot >= 0, "osf_weathervane should place");
        osf_clear_inv(player);
        player->varps[varp] = OSF_VANE_SEARCH;
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_vane, -1, loc_slot);
        osf_finish(srv);
        SELFTEST_CHECK(player->varps[varp] == OSF_VANE_SEARCH,
                       "vane search without a hammer must refuse");
        osf_pass("oploc1_vane_no_hammer");

        if( obj_hammer > 0 )
            osf_give(player, obj_hammer, 1);
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_vane, -1, loc_slot);
        osf_finish(srv);
        SELFTEST_CHECK(player->varps[varp] == OSF_VANE_PARTS_TAKEN,
                       "hammered vane search should grant the three parts, got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(obj_dir_broken <= 0 || osf_inv_total(player, obj_dir_broken) > 0,
                       "vane search should grant broken directionals");
        osf_pass("oploc1_vane_search_parts");

        if( obj_dir_fixed > 0 && obj_orn_fixed > 0 && obj_pil_fixed > 0 )
        {
            osf_clear_inv(player);
            osf_give(player, obj_dir_fixed, 1);
            osf_give(player, obj_orn_fixed, 1);
            osf_give(player, obj_pil_fixed, 1);
            player->varps[varp] = OSF_VANE_PARTS_TAKEN;
            osf_set_bit(srv, "directionalsfixed", 0);
            osf_set_bit(srv, "ornamentfixed", 0);
            osf_set_bit(srv, "rotatingpillarfixed", 0);
            player->last_useitem = obj_dir_fixed;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_vane, -1, loc_slot);
            osf_finish(srv);
            player->last_useitem = obj_orn_fixed;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_vane, -1, loc_slot);
            osf_finish(srv);
            player->last_useitem = obj_pil_fixed;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_vane, -1, loc_slot);
            osf_finish(srv);
            SELFTEST_CHECK(player->varps[varp] == OSF_VANE_REPAIRED,
                           "slotting all three parts should repair the vane, got %d",
                           player->varps[varp]);
            osf_pass("oplocu_vane_slot_parts");
        }
    }

    /* Isolated anvil repair (steel / bronze / iron). */
    if( obj_dir_broken > 0 && obj_steel > 0 && obj_hammer > 0 )
    {
        int loc_anvil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "anvil");
        int loc_slot;

        osf_clear_inv(player);
        osf_give(player, obj_dir_broken, 1);
        osf_give(player, obj_hammer, 1);
        player->varps[varp] = OSF_VANE_PARTS_TAKEN;
        player->last_useitem = obj_dir_broken;
        if( loc_anvil >= 0 )
        {
            loc_slot = osf_place_loc(srv, loc_anvil, 2712, 3495, 0);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_anvil, -1, loc_slot);
            osf_finish(srv);
            SELFTEST_CHECK(osf_inv_total(player, obj_dir_broken) > 0,
                           "anvil without a steel bar must refuse the directionals");
            osf_pass("oplocu_anvil_directionals_no_steel");

            osf_give(player, obj_steel, 1);
            player->last_useitem = obj_dir_broken;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_anvil, -1, loc_slot);
            osf_finish(srv);
            SELFTEST_CHECK(obj_dir_fixed <= 0 || osf_inv_total(player, obj_dir_fixed) > 0,
                           "anvil + steel should repair the directionals");
            osf_pass("oplocu_anvil_repair_directionals");
        }
    }

    if( npc_phantuwti > 0 )
    {
        slot = osf_spawn(srv, npc_phantuwti, 2702, 3473, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_VANE_REPAIRED;
            osf_talk_finish(srv, npc_phantuwti, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_PHANTUWTI_VANE_DONE,
                           "fixed vane should grant the weather report, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_report <= 0 || osf_inv_total(player, obj_report) > 0,
                           "Phantuwti should grant favour_weather_report");
            osf_pass("opnpc1_phantuwti_weather_report");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_arhein > 0 )
    {
        slot = osf_spawn(srv, npc_arhein, 2804, 3432, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_PHANTUWTI_VANE_DONE;
            if( obj_report > 0 )
                osf_give(player, obj_report, 1);
            osf_talk_finish(srv, npc_arhein, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_ARHEIN_DONE,
                           "Arhein should accept the weather report, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_arhein_weather_handin");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_bleemadge > 0 )
    {
        slot = osf_spawn(srv, npc_bleemadge, 2847, 3498, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_ARHEIN_DONE;
            osf_talk_finish(srv, npc_bleemadge, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_BLEEMADGE_DONE,
                           "Bleemadge should confirm the T.R.A.S.H., got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_bleemadge_trash_done");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_sanfew > 0 )
    {
        slot = osf_spawn(srv, npc_sanfew, 2899, 3429, 1);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_BLEEMADGE_DONE;
            osf_talk_finish(srv, npc_sanfew, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_SANFEW_DONE,
                           "Sanfew OSF return should accept the initiate, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_sanfew_glider_done");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_hammerspike > 0 )
    {
        slot = osf_spawn(srv, npc_hammerspike, 2968, 9811, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_SANFEW_DONE;
            osf_talk_finish(srv, npc_hammerspike, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_HAMMERSPIKE_GANG,
                           "Hammerspike should send the gang after Sanfew, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_hammerspike_gang");

            if( npc_gang > 0 )
            {
                int gang = osf_spawn(srv, npc_gang, 2969, 9811, 0);

                if( gang >= 0 )
                {
                    player->varps[varp] = OSF_TASSIE_TOLD;
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_gang, -1, gang);
                    osf_finish(srv);
                    SELFTEST_CHECK(player->varps[varp] == OSF_TASSIE_TOLD,
                                   "gang attack before the ambush must refuse");
                    osf_pass("opnpc2_gang_no_reason");
                    osf_free_npc(srv, gang);
                }
            }
            osf_free_npc(srv, slot);
        }
    }

    if( npc_tassie > 0 )
    {
        slot = osf_spawn(srv, npc_tassie, 3085, 3409, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_HAMMERSPIKE_DONE;
            osf_talk_finish(srv, npc_tassie, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_TASSIE_DONE,
                           "Tassie should teach the pot lid, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_tassie_pot_lid");
            osf_free_npc(srv, slot);
        }
    }

    /* Seal pot + lid (shared Swan Song item, OSF advances pot_made). */
    if( obj_pot > 0 && obj_lid > 0 && obj_airtight > 0 )
    {
        int held_slot;

        osf_clear_inv(player);
        osf_give(player, obj_pot, 1);
        osf_give(player, obj_lid, 1);
        player->varps[varp] = OSF_TASSIE_DONE;
        player->last_useitem = obj_lid;
        for( held_slot = 0; held_slot < TORIRSSERVER_INV_SLOTS; held_slot++ )
            if( player->inv[held_slot].obj_id == obj_pot )
                break;
        if( held_slot < TORIRSSERVER_INV_SLOTS )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_pot, -1, held_slot);
            osf_finish(srv);
            SELFTEST_CHECK(player->varps[varp] == OSF_POT_MADE,
                           "sealing the pot should mark pot_made, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(osf_inv_total(player, obj_airtight) > 0,
                           "sealing should grant favour_airtight_pot");
            osf_pass("opheldu_pot_lid_seal");
        }
    }

    if( npc_apoth > 0 )
    {
        slot = osf_spawn(srv, npc_apoth, 3196, 3404, 0);
        if( slot >= 0 )
        {
            osf_clear_inv(player);
            player->varps[varp] = OSF_POT_MADE;
            osf_talk_finish(srv, npc_apoth, slot);
            SELFTEST_CHECK(osf_inv_total(player, obj_salts) == 0,
                           "Apothecary without the pot must refuse");
            osf_pass("opnpc1_apoth_no_pot_refuse");

            if( obj_airtight > 0 )
                osf_give(player, obj_airtight, 1);
            osf_talk_finish(srv, npc_apoth, slot);
            SELFTEST_CHECK(obj_salts <= 0 || osf_inv_total(player, obj_salts) > 0,
                           "Apothecary should grant breathing salts");
            osf_pass("opnpc1_apoth_pot_handin");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_horvik > 0 )
    {
        slot = osf_spawn(srv, npc_horvik, 3229, 3437, 0);
        if( slot >= 0 )
        {
            osf_clear_inv(player);
            player->varps[varp] = OSF_POT_MADE;
            osf_talk_finish(srv, npc_horvik, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_POT_MADE,
                           "Horvik without salts must wait");
            osf_pass("opnpc1_horvik_waiting_salts");

            if( obj_salts > 0 )
                osf_give(player, obj_salts, 1);
            if( obj_tincture > 0 )
                osf_give(player, obj_tincture, 1);
            osf_talk_finish(srv, npc_horvik, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_HORVIK_DONE,
                           "Horvik should make the cages, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_horvik_cages_handin");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_seth > 0 )
    {
        slot = osf_spawn(srv, npc_seth, 3228, 3291, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_HORVIK_DONE;
            if( obj_cage > 0 )
                osf_give(player, obj_cage, 5);
            osf_talk_finish(srv, npc_seth, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_SETH_DONE,
                           "Seth should take the cages, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_seth_cages_handin");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_johanhus > 0 )
    {
        slot = osf_spawn(srv, npc_johanhus, 3171, 9619, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_SETH_DONE;
            osf_talk_finish(srv, npc_johanhus, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_JOHANHUS_DONE,
                           "Johanhus should free Jimmy, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_johanhus_chickens_handin");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_aggie > 0 )
    {
        slot = osf_spawn(srv, npc_aggie, 3086, 3258, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_JOHANHUS_DONE;
            osf_talk_finish(srv, npc_aggie, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_AGGIE_DONE,
                           "Aggie should vouch for Brian, got %d",
                           player->varps[varp]);
            osf_pass("opnpc1_aggie_jimmy_free");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_brian > 0 )
    {
        slot = osf_spawn(srv, npc_brian, 3027, 3249, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_AGGIE_DONE;
            if( obj_axe_blunt > 0 )
                osf_give(player, obj_axe_blunt, 1);
            osf_talk_finish(srv, npc_brian, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_BRIAN_DONE,
                           "Brian should sharpen the axe, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_axe_sharp <= 0 || osf_inv_total(player, obj_axe_sharp) > 0,
                           "Brian should grant the sharpened axe");
            osf_pass("opnpc1_brian_sharpen_axe");
            osf_free_npc(srv, slot);
        }
    }

    if( npc_forester > 0 )
    {
        slot = osf_spawn(srv, npc_forester, 2861, 2942, 0);
        if( slot >= 0 )
        {
            player->varps[varp] = OSF_BRIAN_DONE;
            if( obj_axe_sharp > 0 )
                osf_give(player, obj_axe_sharp, 1);
            osf_talk_finish(srv, npc_forester, slot);
            SELFTEST_CHECK(player->varps[varp] == OSF_FORESTER_DONE,
                           "forester should fell the mahogany, got %d",
                           player->varps[varp]);
            SELFTEST_CHECK(obj_mahogany <= 0 || osf_inv_total(player, obj_mahogany) > 0,
                           "forester should grant favour_mahogany_log");
            osf_pass("opnpc1_forester_mahogany");
            osf_free_npc(srv, slot);
        }
    }

    /* ---- Authored complete scroll ---- */
    if( yanni_slot >= 0 )
    {
        int drain_tick;
        int com_messagebox =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");

        osf_tele(srv, 2836, 2983, 0);
        player->varps[varp] = OSF_FORESTER_DONE;
        if( obj_mahogany > 0 )
            osf_give(player, obj_mahogany, 1);
        osf_talk(srv, npc_yanni, yanni_slot);
        if( com_messagebox > 0 )
            ToriRSServer_ScriptsResumeButton(srv, com_messagebox);
        for( drain_tick = 0; drain_tick < 48 && player->varps[varp] != OSF_COMPLETE;
             drain_tick++ )
        {
            ToriRSServer_WorldCloseModal(srv);
            selftest_tick(srv);
            osf_finish(srv);
        }
        SELFTEST_CHECK(player->varps[varp] == OSF_COMPLETE,
                       "Yanni mahogany hand-in should complete OSF (285), got %d",
                       player->varps[varp]);
        SELFTEST_CHECK(obj_keyring <= 0 || osf_inv_total(player, obj_keyring) > 0,
                       "completion should grant the steel key ring");
        osf_pass("opnpc1_yanni_complete");
        osf_free_npc(srv, yanni_slot);
    }

    osf_clear_inv(player);
    player->varps[varp] = OSF_NOT_STARTED;
    osf_god(player);

    fprintf(stderr, "ToriRSServer osf selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_ONESMALLFAVOUR_SELFTEST_U_H */
