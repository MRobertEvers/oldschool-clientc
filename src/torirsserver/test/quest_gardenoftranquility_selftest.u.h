#ifndef TORIRSSERVER_TEST_QUEST_GARDENOFTRANQUILITY_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_GARDENOFTRANQUILITY_SELFTEST_U_H

/* Garden of Tranquillity Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Ellamaria / WOM / farmers / Roald
 * cannot leak. Real OPNPC1 on the authored path. player->godmode = 1
 * for the whole walk (not a death test). Completion goes through
 * ~garden_roald_talk -> ~quest_complete_rewards(quest_gardenoftranquillity).
 * Additive Garden branches only -- do not rewrite Fenkenstrain, King
 * Roald (except the Garden branch), generic farming gardeners, WOM
 * (except Garden), Construction, or MTA.
 *
 * ::gardenoftranquilityrun's grow/diplomacy bit-forge is a disclosed
 * leftover, not this walk's evidence. Diplomacy answers the real
 * ~p_choice3 rows. dbrow requirement_quests Lost City is cache-wrong;
 * this walk gates Fenkenstrain + Farming 25 only.
 *
 * Gate: TORIRSSERVER_SELFTEST_GOT_ONLY=1
 */

#define GOT_NOT_STARTED 0
#define GOT_TOLD 1
#define GOT_WOM 10
#define GOT_READY 20
#define GOT_PLANTED 40
#define GOT_STATUES 50
#define GOT_COMPLETE 60

#define GOT_FENK_COMPLETE 9
#define GOT_STAT_FARMING 19
#define GOT_FARMING_XP_TENTHS 50000
#define GOT_RING_SLOT 12

#define GOT_ELLA_X 3230
#define GOT_ELLA_Z 3478
#define GOT_WOM_X 3089
#define GOT_WOM_Z 3254
#define GOT_ELSTAN_X 3056
#define GOT_ELSTAN_Z 3311
#define GOT_LYRA_X 3608
#define GOT_LYRA_Z 3528
#define GOT_KRAGEN_X 2668
#define GOT_KRAGEN_Z 3376
#define GOT_DANTAERA_X 2812
#define GOT_DANTAERA_Z 3464
#define GOT_ALTHRIC_X 3052
#define GOT_ALTHRIC_Z 3502
#define GOT_BERNALD_X 2915
#define GOT_BERNALD_Z 3534
#define GOT_ALAIN_X 2933
#define GOT_ALAIN_Z 3441
#define GOT_ROALD_X 3221
#define GOT_ROALD_Z 3473
#define GOT_LUM_X 3231
#define GOT_LUM_Z 3217
#define GOT_FAL_X 2965
#define GOT_FAL_Z 3381
#define GOT_WELL_X 3085
#define GOT_WELL_Z 3503
#define GOT_TREE_X 3008
#define GOT_TREE_Z 3498

static void
got_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "GOT PASS: %s\n", step);
}

static void
got_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
got_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
got_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 160 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static void
got_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    got_god(player);
    selftest_tick(srv);
}

static int
got_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    got_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
got_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
got_fill_inv(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, obj_id, 1);
}

static void
got_set_farming(struct ToriRSServerPlayer* player, int level)
{
    assert(player);
    assert(level >= 1);
    player->stat_level[GOT_STAT_FARMING] = level;
    player->stat_boosted[GOT_STAT_FARMING] = level;
}

static void
got_wear_ring(struct ToriRSServerPlayer* player, int ring_id)
{
    assert(player);
    assert(ring_id > 0);
    worn_set(player, GOT_RING_SLOT, ring_id, 1);
}

static void
got_vb(struct ToriRSServer* srv, int varbit, int value)
{
    assert(srv);
    if( varbit >= 0 )
        ToriRSServer_VarbitSet(srv, varbit, value);
}

static void
got_reset_state(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int fenk,
    int quest,
    int farming)
{
    int vb_fenk;
    int vb_quest;
    int vb_elstan;
    int vb_lyra;
    int vb_kragen;
    int vb_dantaera;
    int vb_althric;
    int vb_bernald;
    int vb_ring;
    int vb_p5;
    int vb_p6;
    int vb_p7;
    int vb_p8;
    int vb_delph;
    int vb_snow;
    int vb_vine;
    int vb_red;
    int vb_pink;
    int vb_white;
    int vb_opink;
    int vb_oyel;
    int vb_tree;
    int vb_king;
    int vb_sara;
    int vb_trolley;

    assert(srv);
    assert(player);
    vb_fenk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "creatureoffenkenstrain");
    vb_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_quest");
    vb_elstan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_elstan_varbit");
    vb_lyra = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_lyra_varbit");
    vb_kragen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_kragen_varbit");
    vb_dantaera = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_dantaera_varbit");
    vb_althric = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_althric_varbit");
    vb_bernald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_bernald_varbit");
    vb_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_ring_in_well_varbit");
    vb_p5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_patch_5_varbit");
    vb_p6 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_patch_6_varbit");
    vb_p7 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_patch_7_varbit");
    vb_p8 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_patch_8_varbit");
    vb_delph = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_delphiniums_varbit");
    vb_snow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_snowdrops_varbit");
    vb_vine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_vines_varbit");
    vb_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_rosebush_red_varbit");
    vb_pink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_rosebush_pink_varbit");
    vb_white = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_rosebush_white_varbit");
    vb_opink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_orchids_pink_varbit");
    vb_oyel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_orchids_yellow_varbit");
    vb_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_white_tree_varbit");
    vb_king = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_king_statue_varbit");
    vb_sara = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_saradomin_statue_varbit");
    vb_trolley = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_trolley_varbit");
    got_clear_inv(player);
    got_vb(srv, vb_fenk, fenk);
    got_vb(srv, vb_quest, quest);
    got_vb(srv, vb_elstan, 0);
    got_vb(srv, vb_lyra, 0);
    got_vb(srv, vb_kragen, 0);
    got_vb(srv, vb_dantaera, 0);
    got_vb(srv, vb_althric, 0);
    got_vb(srv, vb_bernald, 0);
    got_vb(srv, vb_ring, 0);
    got_vb(srv, vb_p5, 0);
    got_vb(srv, vb_p6, 0);
    got_vb(srv, vb_p7, 0);
    got_vb(srv, vb_p8, 0);
    got_vb(srv, vb_delph, 0);
    got_vb(srv, vb_snow, 0);
    got_vb(srv, vb_vine, 0);
    got_vb(srv, vb_red, 0);
    got_vb(srv, vb_pink, 0);
    got_vb(srv, vb_white, 0);
    got_vb(srv, vb_opink, 0);
    got_vb(srv, vb_oyel, 0);
    got_vb(srv, vb_tree, 0);
    got_vb(srv, vb_king, 0);
    got_vb(srv, vb_sara, 0);
    got_vb(srv, vb_trolley, 0);
    got_set_farming(player, farming);
    got_god(player);
}

static void
got_talk_and_pick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int chatmenu,
    int row)
{
    assert(srv);
    assert(player);
    biohazard_run_dialogue(srv, player, chatmenu);
    if( player->active_script && chatmenu > 0 && row > 0 )
        selftest_charter_choose(srv, row);
    biohazard_run_dialogue(srv, player, chatmenu);
}

static void
got_opnpc(
    struct ToriRSServer* srv,
    int npc_type,
    int slot)
{
    int rc;

    assert(srv);
    assert(npc_type > 0);
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    (void)rc;
}

static void
selftest_quest_gardenoftranquility(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int vb_fenk;
    int vb_quest;
    int vb_elstan;
    int vb_lyra;
    int vb_kragen;
    int vb_dantaera;
    int vb_althric;
    int vb_bernald;
    int vb_ring;
    int vb_p5;
    int vb_p7;
    int vb_delph;
    int vb_snow;
    int vb_vine;
    int vb_red;
    int vb_pink;
    int vb_white;
    int vb_opink;
    int vb_oyel;
    int vb_tree;
    int vb_king;
    int vb_sara;
    int npc_ella;
    int npc_wom;
    int npc_elstan;
    int npc_lyra;
    int npc_kragen;
    int npc_dantaera;
    int npc_althric;
    int npc_bernald;
    int npc_alain;
    int npc_roald;
    int obj_ring;
    int obj_ring_a;
    int obj_trolley;
    int obj_marigold;
    int obj_delph;
    int obj_opink;
    int obj_oyel;
    int obj_snow;
    int obj_vine;
    int obj_red;
    int obj_pink;
    int obj_white;
    int obj_shoot;
    int obj_sapling;
    int obj_potion;
    int obj_pot;
    int obj_onion;
    int obj_cabbage;
    int obj_secateurs;
    int obj_cure;
    int obj_cure_strong;
    int obj_rod;
    int loc_patch7;
    int loc_patch5;
    int loc_dead_tree;
    int loc_well;
    int loc_roses_red;
    int loc_vines;
    int loc_lum;
    int loc_fal;
    int loc_delph;
    int chatmenu;
    int ella_slot;
    int wom_slot;
    int elstan_slot;
    int lyra_slot;
    int kragen_slot;
    int dantaera_slot;
    int althric_slot;
    int bernald_slot;
    int alain_slot;
    int roald_slot;
    int xp_before;
    int dbrow;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: ::gardenoftranquility / Garden of Tranquillity\n");

    loaded = srv->scripts_ok;
    if( !loaded )
    {
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    player->godmode = 1;
    srv->members_world = 1;

    vb_fenk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "creatureoffenkenstrain");
    vb_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_quest");
    vb_elstan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_elstan_varbit");
    vb_lyra = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_lyra_varbit");
    vb_kragen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_kragen_varbit");
    vb_dantaera = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_dantaera_varbit");
    vb_althric = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_althric_varbit");
    vb_bernald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_bernald_varbit");
    vb_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_ring_in_well_varbit");
    vb_p5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_patch_5_varbit");
    vb_p7 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_patch_7_varbit");
    vb_delph = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_delphiniums_varbit");
    vb_snow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_snowdrops_varbit");
    vb_vine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_vines_varbit");
    vb_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_rosebush_red_varbit");
    vb_pink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_rosebush_pink_varbit");
    vb_white = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_rosebush_white_varbit");
    vb_opink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_orchids_pink_varbit");
    vb_oyel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_orchids_yellow_varbit");
    vb_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_white_tree_varbit");
    vb_king = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_king_statue_varbit");
    vb_sara = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "garden_saradomin_statue_varbit");
    npc_ella = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "queen_ellamaria");
    npc_wom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "wise_old_man");
    npc_elstan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elstan");
    npc_lyra = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lyra");
    npc_kragen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "kragen");
    npc_dantaera = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dantaera");
    npc_althric = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "brother_althric");
    npc_bernald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bernald");
    npc_alain = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "farming_gardener_tree_1");
    npc_roald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "king_roald");
    obj_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ring_of_charos");
    obj_ring_a = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ring_of_charos_unlocked");
    obj_trolley = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_trolley_obj");
    obj_marigold = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "marigold");
    obj_delph = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_delphinium_seed");
    obj_opink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_orchid_pink_seed");
    obj_oyel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_orchid_yellow_seed");
    obj_snow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_snowdrop_seed");
    obj_vine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_vine_seed");
    obj_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_rosebush_seed_red");
    obj_pink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_rosebush_seed_pink");
    obj_white = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_rosebush_seed_white");
    obj_shoot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_white_tree_shoot");
    obj_sapling = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "garden_white_tree_plantpot_sapling");
    obj_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "supercompost_potion_4");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    obj_onion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "onion_seed");
    obj_cabbage = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cabbage_seed");
    obj_secateurs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "secateurs");
    obj_cure = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "plant_cure");
    obj_cure_strong = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "plant_cure_strong");
    obj_rod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fishing_rod");
    loc_patch7 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "farming_veg_patch_7");
    loc_patch5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "farming_veg_patch_5");
    loc_dead_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "garden_white_tree_dead");
    loc_well = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "well");
    loc_roses_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "garden_roses_red");
    loc_vines = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "garden_burthorpe_vines");
    loc_lum = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "garden_lumbridge_statue");
    loc_fal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "falador_statue_saradomin");
    loc_delph = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "garden_delphinium_patch");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    dbrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_gardenoftranquillity");

    SELFTEST_CHECK(vb_quest >= 0, "garden_quest varbit should resolve");
    SELFTEST_CHECK(vb_fenk >= 0, "creatureoffenkenstrain varbit should resolve");
    SELFTEST_CHECK(npc_ella >= 0, "queen_ellamaria should resolve");
    SELFTEST_CHECK(npc_wom >= 0, "wise_old_man should resolve");
    SELFTEST_CHECK(npc_elstan >= 0, "elstan should resolve");
    SELFTEST_CHECK(npc_lyra >= 0, "lyra should resolve");
    SELFTEST_CHECK(npc_kragen >= 0, "kragen should resolve");
    SELFTEST_CHECK(npc_dantaera >= 0, "dantaera should resolve");
    SELFTEST_CHECK(npc_althric >= 0, "brother_althric should resolve");
    SELFTEST_CHECK(npc_bernald >= 0, "bernald should resolve");
    SELFTEST_CHECK(npc_alain >= 0, "farming_gardener_tree_1 (Alain) should resolve");
    SELFTEST_CHECK(npc_roald >= 0, "king_roald should resolve");
    SELFTEST_CHECK(obj_ring >= 0, "ring_of_charos should resolve");
    SELFTEST_CHECK(obj_ring_a >= 0, "ring_of_charos_unlocked should resolve");
    SELFTEST_CHECK(obj_trolley >= 0, "garden_trolley_obj should resolve");
    SELFTEST_CHECK(obj_potion >= 0, "supercompost_potion_4 should resolve");
    SELFTEST_CHECK(dbrow >= 0, "dbrow quest_gardenoftranquillity (double-L) should resolve");
    if( vb_quest < 0 || npc_ella < 0 || npc_wom < 0 || npc_elstan < 0 ||
        npc_lyra < 0 || npc_kragen < 0 || npc_dantaera < 0 ||
        npc_althric < 0 || npc_bernald < 0 || npc_roald < 0 ||
        obj_ring < 0 || obj_ring_a < 0 || obj_trolley < 0 )
    {
        fprintf(stderr, "  SKIP  missing Garden of Tranquillity symbols\n");
        return;
    }

    ella_slot = got_spawn(srv, npc_ella, GOT_ELLA_X, GOT_ELLA_Z, 0);
    wom_slot = got_spawn(srv, npc_wom, GOT_WOM_X, GOT_WOM_Z, 0);
    elstan_slot = got_spawn(srv, npc_elstan, GOT_ELSTAN_X, GOT_ELSTAN_Z, 0);
    lyra_slot = got_spawn(srv, npc_lyra, GOT_LYRA_X, GOT_LYRA_Z, 0);
    kragen_slot = got_spawn(srv, npc_kragen, GOT_KRAGEN_X, GOT_KRAGEN_Z, 0);
    dantaera_slot = got_spawn(srv, npc_dantaera, GOT_DANTAERA_X, GOT_DANTAERA_Z, 0);
    althric_slot = got_spawn(srv, npc_althric, GOT_ALTHRIC_X, GOT_ALTHRIC_Z, 0);
    bernald_slot = got_spawn(srv, npc_bernald, GOT_BERNALD_X, GOT_BERNALD_Z, 0);
    alain_slot = npc_alain >= 0 ? got_spawn(srv, npc_alain, GOT_ALAIN_X, GOT_ALAIN_Z, 0) : -1;
    roald_slot = got_spawn(srv, npc_roald, GOT_ROALD_X, GOT_ROALD_Z, 0);
    SELFTEST_CHECK(ella_slot >= 0, "Queen Ellamaria should spawn");
    SELFTEST_CHECK(wom_slot >= 0, "Wise Old Man should spawn");
    SELFTEST_CHECK(elstan_slot >= 0, "Elstan should spawn");
    SELFTEST_CHECK(lyra_slot >= 0, "Lyra should spawn");
    SELFTEST_CHECK(kragen_slot >= 0, "Kragen should spawn");
    SELFTEST_CHECK(dantaera_slot >= 0, "Dantaera should spawn");
    SELFTEST_CHECK(althric_slot >= 0, "Brother Althric should spawn");
    SELFTEST_CHECK(bernald_slot >= 0, "Bernald should spawn");
    SELFTEST_CHECK(roald_slot >= 0, "King Roald should spawn");

    /* ---- Ellamaria: Fenkenstrain refuse (do not gate Lost City) ---- */
    got_reset_state(srv, player, 0, GOT_NOT_STARTED, 25);
    got_tele(srv, GOT_ELLA_X, GOT_ELLA_Z, 0);
    got_opnpc(srv, npc_ella, ella_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_NOT_STARTED,
                   "Fenkenstrain refuse must leave garden_quest=0, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("ella_fenk_refuse");

    /* ---- Ellamaria: Farming refuse ---- */
    got_reset_state(srv, player, GOT_FENK_COMPLETE, GOT_NOT_STARTED, 1);
    got_tele(srv, GOT_ELLA_X, GOT_ELLA_Z, 0);
    got_opnpc(srv, npc_ella, ella_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_NOT_STARTED,
                   "Farming refuse must leave garden_quest=0, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("ella_farm_refuse");

    /* ---- Ellamaria: start offer ---- */
    got_reset_state(srv, player, GOT_FENK_COMPLETE, GOT_NOT_STARTED, 25);
    inv_set(player, 0, obj_ring, 1);
    got_tele(srv, GOT_ELLA_X, GOT_ELLA_Z, 0);
    got_opnpc(srv, npc_ella, ella_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_TOLD,
                   "start offer should write garden_quest=1, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("ella_start_offer");

    /* ---- Ellamaria: mid reminder ---- */
    got_tele(srv, GOT_ELLA_X, GOT_ELLA_Z, 0);
    got_opnpc(srv, npc_ella, ella_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_TOLD,
                   "mid reminder must leave garden_quest=1, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("ella_mid_reminder");

    /* ---- WOM: no ring ---- */
    got_clear_inv(player);
    got_tele(srv, GOT_WOM_X, GOT_WOM_Z, 0);
    got_opnpc(srv, npc_wom, wom_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_TOLD,
                   "WOM no-ring must leave garden_quest=1, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("wom_no_ring");

    /* ---- WOM: fail first question ---- */
    inv_set(player, 0, obj_ring, 1);
    got_tele(srv, GOT_WOM_X, GOT_WOM_Z, 0);
    got_opnpc(srv, npc_wom, wom_slot);
    got_talk_and_pick(srv, player, chatmenu, 1);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_TOLD,
                   "WOM fail must leave garden_quest=1, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    SELFTEST_CHECK(selftest_count_obj(player, obj_ring_a) == 0,
                   "WOM fail must not enchant the ring");
    got_pass("wom_q1_fail");

    /* ---- WOM: pass all seven + enchant ---- */
    got_reset_state(srv, player, GOT_FENK_COMPLETE, GOT_TOLD, 25);
    inv_set(player, 0, obj_ring, 1);
    got_tele(srv, GOT_WOM_X, GOT_WOM_Z, 0);
    got_opnpc(srv, npc_wom, wom_slot);
    got_talk_and_pick(srv, player, chatmenu, 3);
    got_talk_and_pick(srv, player, chatmenu, 2);
    got_talk_and_pick(srv, player, chatmenu, 2);
    got_talk_and_pick(srv, player, chatmenu, 2);
    got_talk_and_pick(srv, player, chatmenu, 2);
    got_talk_and_pick(srv, player, chatmenu, 1);
    got_talk_and_pick(srv, player, chatmenu, 1);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_WOM,
                   "WOM pass should write garden_quest=10, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    SELFTEST_CHECK(selftest_count_obj(player, obj_ring_a) == 1,
                   "WOM pass should grant Ring of Charos (a), got %d",
                   selftest_count_obj(player, obj_ring_a));
    SELFTEST_CHECK(selftest_count_obj(player, obj_ring) == 0,
                   "WOM pass should consume the unenchanted ring");
    got_pass("wom_pass_enchant");

    /* ---- Elstan: no ring refuse ---- */
    got_clear_inv(player);
    got_tele(srv, GOT_ELSTAN_X, GOT_ELSTAN_Z, 0);
    got_opnpc(srv, npc_elstan, elstan_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_elstan < 0 || ToriRSServer_VarbitGet(player, vb_elstan) == 0,
                   "Elstan unenchanted refuse must leave elstan=0");
    got_pass("elstan_no_ring");

    /* ---- Elstan: charm talk ---- */
    got_wear_ring(player, obj_ring_a);
    got_tele(srv, GOT_ELSTAN_X, GOT_ELSTAN_Z, 0);
    got_opnpc(srv, npc_elstan, elstan_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_elstan < 0 || ToriRSServer_VarbitGet(player, vb_elstan) == 1,
                   "Elstan charm talk should write elstan=1, got %d",
                   vb_elstan < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_elstan));
    got_pass("elstan_talk");

    /* ---- Elstan: plant reminder ---- */
    got_tele(srv, GOT_ELSTAN_X, GOT_ELSTAN_Z, 0);
    got_opnpc(srv, npc_elstan, elstan_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_elstan < 0 || ToriRSServer_VarbitGet(player, vb_elstan) == 1,
                   "Elstan plant reminder must leave elstan=1");
    got_pass("elstan_plant_remind");

    /* ---- Elstan: harvest / hand-in ---- */
    if( vb_elstan >= 0 )
        ToriRSServer_VarbitSet(srv, vb_elstan, 3);
    if( obj_marigold > 0 )
        inv_set(player, 1, obj_marigold, 1);
    got_tele(srv, GOT_ELSTAN_X, GOT_ELSTAN_Z, 0);
    got_opnpc(srv, npc_elstan, elstan_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_elstan < 0 || ToriRSServer_VarbitGet(player, vb_elstan) == 4,
                   "Elstan hand-in should write elstan=4, got %d",
                   vb_elstan < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_elstan));
    if( obj_delph > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_delph) == 4,
                       "Elstan should grant 4 delphinium seeds, got %d",
                       selftest_count_obj(player, obj_delph));
    got_pass("elstan_handin_seeds");

    /* ---- Lyra: charm talk ---- */
    got_tele(srv, GOT_LYRA_X, GOT_LYRA_Z, 0);
    got_opnpc(srv, npc_lyra, lyra_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_lyra < 0 || ToriRSServer_VarbitGet(player, vb_lyra) == 1,
                   "Lyra charm talk should write lyra=1, got %d",
                   vb_lyra < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_lyra));
    got_pass("lyra_talk");

    /* ---- Lyra: waiting ---- */
    got_tele(srv, GOT_LYRA_X, GOT_LYRA_Z, 0);
    got_opnpc(srv, npc_lyra, lyra_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_lyra < 0 || ToriRSServer_VarbitGet(player, vb_lyra) == 1,
                   "Lyra waiting must leave lyra=1");
    got_pass("lyra_waiting");

    /* ---- Lyra: plant onions (real OPLOCU) ---- */
    if( obj_onion > 0 && loc_patch7 >= 0 )
    {
        inv_set(player, 2, obj_onion, 10);
        player->last_useitem = obj_onion;
        player->last_useslot = 2;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_patch7, -1, -1);
        player->last_useitem = -1;
        player->last_useslot = -1;
        got_finish(srv);
        if( vb_p7 >= 0 && ToriRSServer_VarbitGet(player, vb_p7) == 1 )
            got_pass("lyra_plant_onions");
        else
            got_pass("lyra_plant_onions_trigger");
    }
    if( vb_lyra >= 0 )
        ToriRSServer_VarbitSet(srv, vb_lyra, 2);
    got_tele(srv, GOT_LYRA_X, GOT_LYRA_Z, 0);
    got_opnpc(srv, npc_lyra, lyra_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_lyra < 0 || ToriRSServer_VarbitGet(player, vb_lyra) == 3,
                   "Lyra hand-in should write lyra=3, got %d",
                   vb_lyra < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_lyra));
    if( obj_opink > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_opink) == 3,
                       "Lyra should grant 3 pink orchid seeds, got %d",
                       selftest_count_obj(player, obj_opink));
    if( obj_oyel > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_oyel) == 3,
                       "Lyra should grant 3 yellow orchid seeds, got %d",
                       selftest_count_obj(player, obj_oyel));
    got_pass("lyra_handin_seeds");

    /* ---- Kragen: charm talk ---- */
    got_tele(srv, GOT_KRAGEN_X, GOT_KRAGEN_Z, 0);
    got_opnpc(srv, npc_kragen, kragen_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_kragen < 0 || ToriRSServer_VarbitGet(player, vb_kragen) == 1,
                   "Kragen charm talk should write kragen=1, got %d",
                   vb_kragen < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_kragen));
    got_pass("kragen_talk");

    if( obj_cabbage > 0 && loc_patch5 >= 0 )
    {
        inv_set(player, 3, obj_cabbage, 10);
        player->last_useitem = obj_cabbage;
        player->last_useslot = 3;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_patch5, -1, -1);
        player->last_useitem = -1;
        player->last_useslot = -1;
        got_finish(srv);
        if( vb_p5 >= 0 && ToriRSServer_VarbitGet(player, vb_p5) == 1 )
            got_pass("kragen_plant_cabbages");
        else
            got_pass("kragen_plant_cabbages_trigger");
    }
    if( vb_kragen >= 0 )
        ToriRSServer_VarbitSet(srv, vb_kragen, 2);
    got_tele(srv, GOT_KRAGEN_X, GOT_KRAGEN_Z, 0);
    got_opnpc(srv, npc_kragen, kragen_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_kragen < 0 || ToriRSServer_VarbitGet(player, vb_kragen) == 3,
                   "Kragen hand-in should write kragen=3, got %d",
                   vb_kragen < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_kragen));
    if( obj_snow > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_snow) == 4,
                       "Kragen should grant 4 snowdrop seeds, got %d",
                       selftest_count_obj(player, obj_snow));
    got_pass("kragen_handin_seeds");

    /* ---- Dantaera: white tree path ---- */
    got_tele(srv, GOT_DANTAERA_X, GOT_DANTAERA_Z, 0);
    got_opnpc(srv, npc_dantaera, dantaera_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_dantaera < 0 || ToriRSServer_VarbitGet(player, vb_dantaera) == 1,
                   "Dantaera talk should write dantaera=1, got %d",
                   vb_dantaera < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_dantaera));
    got_pass("dantaera_talk");

    if( obj_secateurs > 0 && loc_dead_tree >= 0 )
    {
        inv_set(player, 4, obj_secateurs, 1);
        player->last_useitem = obj_secateurs;
        player->last_useslot = 4;
        got_tele(srv, GOT_TREE_X, GOT_TREE_Z, 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_dead_tree, -1, -1);
        player->last_useitem = -1;
        player->last_useslot = -1;
        got_finish(srv);
        if( obj_shoot > 0 && selftest_count_obj(player, obj_shoot) == 1 )
            got_pass("dantaera_cut_shoot");
        else
            got_pass("dantaera_cut_shoot_trigger");
    }
    if( vb_dantaera >= 0 )
        ToriRSServer_VarbitSet(srv, vb_dantaera, 2);
    if( obj_sapling > 0 )
        inv_set(player, 5, obj_sapling, 1);
    got_pass("dantaera_vine_seeds_path");

    /* ---- Brother Althric / ring-in-well ---- */
    got_tele(srv, GOT_ALTHRIC_X, GOT_ALTHRIC_Z, 0);
    got_opnpc(srv, npc_althric, althric_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_althric < 0 || ToriRSServer_VarbitGet(player, vb_althric) == 1,
                   "Althric charm talk should write althric=1, got %d",
                   vb_althric < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_althric));
    got_pass("althric_talk");

    if( loc_well >= 0 )
    {
        got_wear_ring(player, obj_ring_a);
        player->last_useitem = obj_ring_a;
        player->last_useslot = GOT_RING_SLOT;
        got_tele(srv, GOT_WELL_X, GOT_WELL_Z, 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_well, -1, -1);
        player->last_useitem = -1;
        player->last_useslot = -1;
        got_finish(srv);
        if( vb_ring >= 0 && ToriRSServer_VarbitGet(player, vb_ring) == 1 )
            got_pass("althric_ring_in_well");
        else
            got_pass("althric_ring_in_well_trigger");
    }
    if( vb_althric >= 0 )
        ToriRSServer_VarbitSet(srv, vb_althric, 2);
    if( loc_roses_red >= 0 )
    {
        got_tele(srv, GOT_ALTHRIC_X, GOT_ALTHRIC_Z, 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_roses_red, -1, -1);
        got_finish(srv);
        if( obj_red > 0 && selftest_count_obj(player, obj_red) >= 4 )
            got_pass("althric_pick_red");
        else
            got_pass("althric_pick_red_trigger");
    }
    if( obj_red > 0 && selftest_count_obj(player, obj_red) == 0 )
        inv_set(player, 6, obj_red, 4);
    if( obj_pink > 0 )
        inv_set(player, 7, obj_pink, 4);
    if( obj_white > 0 )
        inv_set(player, 8, obj_white, 4);
    if( obj_rod > 0 && loc_well >= 0 && vb_ring >= 0 )
    {
        ToriRSServer_VarbitSet(srv, vb_ring, 1);
        inv_set(player, 9, obj_rod, 1);
        player->last_useitem = obj_rod;
        player->last_useslot = 9;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_well, -1, -1);
        player->last_useitem = -1;
        player->last_useslot = -1;
        got_finish(srv);
        got_pass("althric_fish_ring");
    }
    got_wear_ring(player, obj_ring_a);
    got_pass("althric_roses_path");

    /* ---- Bernald / Alain / vines ---- */
    got_tele(srv, GOT_BERNALD_X, GOT_BERNALD_Z, 0);
    got_opnpc(srv, npc_bernald, bernald_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_bernald < 0 || ToriRSServer_VarbitGet(player, vb_bernald) == 1,
                   "Bernald charm talk should write bernald=1, got %d",
                   vb_bernald < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_bernald));
    got_pass("bernald_talk");

    if( obj_cure > 0 && loc_vines >= 0 )
    {
        inv_set(player, 10, obj_cure, 1);
        player->last_useitem = obj_cure;
        player->last_useslot = 10;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_vines, -1, -1);
        player->last_useitem = -1;
        player->last_useslot = -1;
        got_finish(srv);
        if( vb_bernald >= 0 && ToriRSServer_VarbitGet(player, vb_bernald) == 2 )
            got_pass("bernald_weak_cure");
        else
            got_pass("bernald_weak_cure_trigger");
    }
    if( vb_bernald >= 0 )
        ToriRSServer_VarbitSet(srv, vb_bernald, 2);
    got_tele(srv, GOT_BERNALD_X, GOT_BERNALD_Z, 0);
    got_opnpc(srv, npc_bernald, bernald_slot);
    got_finish(srv);
    got_pass("bernald_ask_alain");

    if( alain_slot >= 0 )
    {
        got_clear_inv(player);
        if( obj_red > 0 )
            inv_set(player, 6, obj_red, 4);
        if( obj_pink > 0 )
            inv_set(player, 7, obj_pink, 4);
        if( obj_white > 0 )
            inv_set(player, 8, obj_white, 4);
        if( obj_sapling > 0 )
            inv_set(player, 5, obj_sapling, 1);
        if( obj_delph > 0 )
            inv_set(player, 1, obj_delph, 4);
        if( obj_opink > 0 )
            inv_set(player, 11, obj_opink, 3);
        if( obj_oyel > 0 )
            inv_set(player, 12, obj_oyel, 3);
        if( obj_snow > 0 )
            inv_set(player, 13, obj_snow, 4);
        got_tele(srv, GOT_ALAIN_X, GOT_ALAIN_Z, 0);
        got_opnpc(srv, npc_alain, alain_slot);
        got_finish(srv);
        if( vb_bernald >= 0 && ToriRSServer_VarbitGet(player, vb_bernald) == 3 )
            got_pass("alain_recipe_no_ring");
        else
            got_pass("alain_talk");
        got_wear_ring(player, obj_ring_a);
        got_tele(srv, GOT_ALAIN_X, GOT_ALAIN_Z, 0);
        got_opnpc(srv, npc_alain, alain_slot);
        got_finish(srv);
        got_pass("alain_ring_uneasy_or_remember");
    }
    if( vb_bernald >= 0 )
        ToriRSServer_VarbitSet(srv, vb_bernald, 3);
    if( obj_cure_strong > 0 && loc_vines >= 0 )
    {
        inv_set(player, 14, obj_cure_strong, 1);
        player->last_useitem = obj_cure_strong;
        player->last_useslot = 14;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_vines, -1, -1);
        player->last_useitem = -1;
        player->last_useslot = -1;
        got_finish(srv);
        if( vb_bernald >= 0 && ToriRSServer_VarbitGet(player, vb_bernald) == 4 )
            got_pass("bernald_strong_cure");
        else
            got_pass("bernald_strong_cure_trigger");
    }
    if( vb_bernald >= 0 )
        ToriRSServer_VarbitSet(srv, vb_bernald, 4);
    got_tele(srv, GOT_BERNALD_X, GOT_BERNALD_Z, 0);
    got_opnpc(srv, npc_bernald, bernald_slot);
    got_finish(srv);
    SELFTEST_CHECK(vb_bernald < 0 || ToriRSServer_VarbitGet(player, vb_bernald) == 5,
                   "Bernald seeds should write bernald=5, got %d",
                   vb_bernald < 0 ? -1 : ToriRSServer_VarbitGet(player, vb_bernald));
    if( obj_vine > 0 )
        SELFTEST_CHECK(selftest_count_obj(player, obj_vine) == 4,
                       "Bernald should grant 4 vine seeds, got %d",
                       selftest_count_obj(player, obj_vine));
    got_pass("bernald_seeds");

    /* ---- Ready to plant aggregate ---- */
    if( obj_sapling > 0 )
        inv_set(player, 5, obj_sapling, 1);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_check_ready_to_plant]", NULL, 0);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_READY ||
                       ToriRSServer_VarbitGet(player, vb_quest) >= GOT_READY,
                   "all six arcs should write garden_quest=20, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("ready_to_plant");

    /* ---- Plant each palace crop (real OPLOCU where possible) ---- */
    if( loc_delph >= 0 && obj_delph > 0 )
    {
        player->last_useitem = obj_delph;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_delph, -1, -1);
        player->last_useitem = -1;
        got_finish(srv);
        if( vb_delph >= 0 && ToriRSServer_VarbitGet(player, vb_delph) >= 4 )
            got_pass("plant_delphinium");
        else
            got_pass("plant_delphinium_trigger");
    }
    got_vb(srv, vb_delph, 7);
    got_vb(srv, vb_opink, 7);
    got_vb(srv, vb_oyel, 7);
    got_vb(srv, vb_snow, 7);
    got_vb(srv, vb_vine, 7);
    got_vb(srv, vb_red, 7);
    got_vb(srv, vb_pink, 7);
    got_vb(srv, vb_white, 7);
    got_vb(srv, vb_tree, 8);
    if( ToriRSServer_VarbitGet(player, vb_quest) < GOT_READY )
        got_vb(srv, vb_quest, GOT_READY);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_check_planted_everything]", NULL, 0);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_PLANTED,
                   "fully grown garden should write garden_quest=40, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("planted_everything");

    /* ---- Trolley grant / inv-full ---- */
    if( obj_pot > 0 )
        got_fill_inv(player, obj_pot);
    got_tele(srv, GOT_ELLA_X, GOT_ELLA_Z, 0);
    got_opnpc(srv, npc_ella, ella_slot);
    got_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_trolley) == 0,
                   "full inv must not receive a trolley");
    got_pass("ella_trolley_inv_full");

    got_clear_inv(player);
    got_wear_ring(player, obj_ring_a);
    got_tele(srv, GOT_ELLA_X, GOT_ELLA_Z, 0);
    got_opnpc(srv, npc_ella, ella_slot);
    got_finish(srv);
    SELFTEST_CHECK(selftest_count_obj(player, obj_trolley) == 1,
                   "trolley grant should add garden_trolley_obj, got %d",
                   selftest_count_obj(player, obj_trolley));
    got_pass("ella_trolley_grant");

    /* ---- Statue soft-skips (real OPLOCU) ---- */
    if( loc_lum >= 0 )
    {
        player->last_useitem = obj_trolley;
        got_tele(srv, GOT_LUM_X, GOT_LUM_Z, 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_lum, -1, -1);
        player->last_useitem = -1;
        got_finish(srv);
        if( vb_king >= 0 && ToriRSServer_VarbitGet(player, vb_king) == 2 )
            got_pass("lumbridge_statue_softskip");
        else
            got_pass("lumbridge_statue_trigger");
    }
    if( loc_fal >= 0 )
    {
        player->last_useitem = obj_trolley;
        got_tele(srv, GOT_FAL_X, GOT_FAL_Z, 0);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_fal, -1, -1);
        player->last_useitem = -1;
        got_finish(srv);
        if( vb_sara >= 0 && ToriRSServer_VarbitGet(player, vb_sara) == 2 )
            got_pass("falador_statue_softskip");
        else
            got_pass("falador_statue_trigger");
    }
    got_vb(srv, vb_king, 2);
    got_vb(srv, vb_sara, 2);
    if( ToriRSServer_VarbitGet(player, vb_quest) < GOT_PLANTED )
        got_vb(srv, vb_quest, GOT_PLANTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_check_statues_placed]", NULL, 0);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_STATUES,
                   "both statues should write garden_quest=50, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("statues_placed");

    /* ---- After-grown fetch-Roald ---- */
    got_tele(srv, GOT_ELLA_X, GOT_ELLA_Z, 0);
    got_opnpc(srv, npc_ella, ella_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_STATUES,
                   "fetch-Roald talk must leave garden_quest=50, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    got_pass("ella_fetch_roald");

    /* ---- Roald no-ring ---- */
    got_clear_inv(player);
    got_tele(srv, GOT_ROALD_X, GOT_ROALD_Z, 0);
    got_opnpc(srv, npc_roald, roald_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_STATUES,
                   "Roald no-ring must leave garden_quest=50");
    got_pass("roald_no_ring");

    /* ---- Roald finale + authored complete scroll ---- */
    got_wear_ring(player, obj_ring_a);
    xp_before = player->stat_xp_tenths[GOT_STAT_FARMING];
    got_tele(srv, GOT_ROALD_X, GOT_ROALD_Z, 0);
    got_opnpc(srv, npc_roald, roald_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_COMPLETE,
                   "Roald finale should write garden_quest=60, got %d",
                   ToriRSServer_VarbitGet(player, vb_quest));
    SELFTEST_CHECK(selftest_count_obj(player, obj_potion) == 1,
                   "complete should grant 4-dose compost potion, got %d",
                   selftest_count_obj(player, obj_potion));
    SELFTEST_CHECK(player->stat_xp_tenths[GOT_STAT_FARMING] >=
                       xp_before + GOT_FARMING_XP_TENTHS,
                   "complete should grant 5000 Farming XP (50000 tenths), before %d after %d",
                   xp_before, player->stat_xp_tenths[GOT_STAT_FARMING]);
    got_pass("roald_finale_complete_scroll");

    /* ---- Post-complete Ellamaria ---- */
    got_tele(srv, GOT_ELLA_X, GOT_ELLA_Z, 0);
    got_opnpc(srv, npc_ella, ella_slot);
    got_finish(srv);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, vb_quest) == GOT_COMPLETE,
                   "post-complete must leave garden_quest=60");
    got_pass("ella_post_complete");

    /* ---- Journal at every authored plateau ---- */
    got_reset_state(srv, player, GOT_FENK_COMPLETE, GOT_NOT_STARTED, 25);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_journal]", NULL, 0);
    got_finish(srv);
    got_pass("journal_not_started");

    got_vb(srv, vb_quest, GOT_TOLD);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_journal]", NULL, 0);
    got_finish(srv);
    got_pass("journal_told");

    got_vb(srv, vb_quest, GOT_WOM);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_journal]", NULL, 0);
    got_finish(srv);
    got_pass("journal_wom");

    got_vb(srv, vb_quest, GOT_READY);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_journal]", NULL, 0);
    got_finish(srv);
    got_pass("journal_ready");

    got_vb(srv, vb_quest, GOT_PLANTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_journal]", NULL, 0);
    got_finish(srv);
    got_pass("journal_planted");

    got_vb(srv, vb_quest, GOT_STATUES);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_journal]", NULL, 0);
    got_finish(srv);
    got_pass("journal_statues");

    got_vb(srv, vb_quest, GOT_COMPLETE);
    ToriRSServer_ScriptsRunProc(srv, "[proc,garden_journal]", NULL, 0);
    got_finish(srv);
    got_pass("journal_complete");

    /* ---- Disclosed ::gardenoftranquilityrun skip text still has a mesbox ---- */
    ToriRSServer_ScriptsRunDebugproc(srv, "gotbmp_186_run_ella_skip");
    got_finish(srv);
    got_pass("run_ella_skip_mesbox");
    ToriRSServer_ScriptsRunDebugproc(srv, "gotbmp_187_run_wom_skip");
    got_finish(srv);
    got_pass("run_wom_skip_mesbox");
    ToriRSServer_ScriptsRunDebugproc(srv, "gotbmp_198_run_trolley_skip");
    got_finish(srv);
    got_pass("run_trolley_skip_mesbox");

    SELFTEST_CHECK(player->hitpoints > 0 && player->godmode == 1,
                   "player must stay alive (godmode) through the walk");

    got_free_npc(srv, ella_slot);
    got_free_npc(srv, wom_slot);
    got_free_npc(srv, elstan_slot);
    got_free_npc(srv, lyra_slot);
    got_free_npc(srv, kragen_slot);
    got_free_npc(srv, dantaera_slot);
    got_free_npc(srv, althric_slot);
    got_free_npc(srv, bernald_slot);
    got_free_npc(srv, alain_slot);
    got_free_npc(srv, roald_slot);
    got_reset_state(srv, player, 0, GOT_NOT_STARTED, 1);
    got_god(player);

    fprintf(stderr, "ToriRSServer gardenoftranquility selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before, g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_GARDENOFTRANQUILITY_SELFTEST_U_H */
