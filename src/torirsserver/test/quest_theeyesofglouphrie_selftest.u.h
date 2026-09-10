#ifndef TORIRSSERVER_TEST_QUEST_THEEYESOFGLOUPHRIE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_THEEYESOFGLOUPHRIE_SELFTEST_U_H

/* The Eyes of Glouphrie Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Brimstail / Hazelmere / Narnode /
 * fluffie spies / bowl / machine / evergreen cannot leak. Real OPNPC1 /
 * OPNPC2 / OPLOC1 / OPLOC3 / OPLOCU / OPHELDU on the authored path.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_EYEGLO_ONLY=1
 *
 * Brimstail is the existing [opnpc1,gnome_brimstail] trigger
 * (areas/area_gnome/scripts/brimstail.rs2) calling ~eyeglo_brimstail_hub.
 * Hazelmere is the existing [opnpc1,grandtree_hazelmere] trigger.
 * Narnode is the existing [opnpc1,grandtree_narnode] trigger.
 * Bucket of sap is the additive [oplocu,evergreen] branch.
 * Mud-rune grind is the additive [opheldu,pestle_and_mortar] branch.
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF / daily-reset
 * / flute widget / telekinetic grab):
 *   - crystal-disc unlock/operate widget collapsed to one click
 *   - goblin-war flashback battle narrated in Hazelmere mind-link
 *   - eyeglo_hazelmeres_book / eyeglo_crystal_book unused flavour
 */

#define EYEGLO_NOT_STARTED 0
#define EYEGLO_INSPECTED 1
#define EYEGLO_TOLD_AGAIN 2
#define EYEGLO_SEEN_HAZELMERE 12
#define EYEGLO_SABOTAGED 15
#define EYEGLO_REPAIRED 25
#define EYEGLO_UNLOCKED 36
#define EYEGLO_DEFEATED 45
#define EYEGLO_COMPLETE 60

#define EYEGLO_MACHINE_LOCKED 0
#define EYEGLO_MACHINE_BROKEN 1
#define EYEGLO_MACHINE_FIXED 2

#define EYEGLO_CREATURE_EVIL 1
#define EYEGLO_CREATURE_KILLED 2

#define EYEGLO_GRANDTREE_COMPLETE 160
#define EYEGLO_REQ_MAGIC 46
#define EYEGLO_REQ_CONSTRUCTION 5
#define EYEGLO_REWARD_MAGIC_TENTHS 120000
#define EYEGLO_REWARD_RC_TENTHS 60000
#define EYEGLO_REWARD_WC_TENTHS 25000
#define EYEGLO_REWARD_CON_TENTHS 2500
#define EYEGLO_REWARD_QP 2

#define EYEGLO_BRIM_X 2409
#define EYEGLO_BRIM_Z 9817
#define EYEGLO_HAZ_X 2678
#define EYEGLO_HAZ_Z 3086
#define EYEGLO_HAZ_LEVEL 1
#define EYEGLO_NAR_X 2466
#define EYEGLO_NAR_Z 3497
#define EYEGLO_BOWL_X 2408
#define EYEGLO_BOWL_Z 9818
#define EYEGLO_MACHINE_X 2407
#define EYEGLO_MACHINE_Z 9816
#define EYEGLO_EVER_X 2410
#define EYEGLO_EVER_Z 3421
#define EYEGLO_SPY1_X 2408
#define EYEGLO_SPY1_Z 9819
#define EYEGLO_SPY2_X 2465
#define EYEGLO_SPY2_Z 3494
#define EYEGLO_SPY3_X 2466
#define EYEGLO_SPY3_Z 3496
#define EYEGLO_SPY3_LEVEL 3
#define EYEGLO_SPY4_X 2422
#define EYEGLO_SPY4_Z 3526
#define EYEGLO_SPY5_X 2461
#define EYEGLO_SPY5_Z 3388
#define EYEGLO_SPY6_X 2462
#define EYEGLO_SPY6_Z 3443

static void
eyeglo_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "EYEGLO PASS: %s\n", step);
}

static void
eyeglo_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
eyeglo_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
eyeglo_finish(struct ToriRSServer* srv)
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
    ToriRSServer_ScriptsProcessQueues(srv);
    for( t = 0; t < 4; t++ )
        selftest_tick(srv);
}

static int
eyeglo_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
eyeglo_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : eyeglo_chatmenu();
    if( uid <= 0 )
        return;
    button[0] = (uint8_t)(uid >> 24);
    button[1] = (uint8_t)(uid >> 16);
    button[2] = (uint8_t)(uid >> 8);
    button[3] = (uint8_t)uid;
    button[4] = (uint8_t)(row >> 8);
    button[5] = (uint8_t)row;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
eyeglo_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = eyeglo_chatmenu();
    for( clicks = 0; clicks < max_pages && player->active_script; clicks++ )
    {
        int uid;
        uint8_t resume[6];

        if( player->resume_button_count <= 0 )
            break;
        uid = player->resume_buttons[0];
        if( chatmenu > 0 && uid == chatmenu )
            return;
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        selftest_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        selftest_tick(srv);
    }
}

static void
eyeglo_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    eyeglo_god(player);
    selftest_tick(srv);
}

static int
eyeglo_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    eyeglo_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
eyeglo_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
eyeglo_free_type(struct ToriRSServer* srv, int npc_type)
{
    int i;

    assert(srv);
    if( npc_type <= 0 )
        return;
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type )
            ToriRSServer_WorldNpcFree(srv, i);
    }
    ToriRSServer_WorldNpcReap(srv);
}

static void
eyeglo_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
eyeglo_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
eyeglo_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
eyeglo_quest(struct ToriRSServerPlayer* player)
{
    return eyeglo_get_vb(player, "eyeglo_quest");
}

static void
eyeglo_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
eyeglo_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    eyeglo_talk(srv, npc_type, slot);
    eyeglo_finish(srv);
}

static void
eyeglo_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    eyeglo_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        eyeglo_click_until_menu(srv, 24);
        eyeglo_pick_row(srv, rows[i]);
    }
    eyeglo_finish(srv);
}

static void
eyeglo_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id < 0 )
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

static int
eyeglo_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    eyeglo_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
eyeglo_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    eyeglo_finish(srv);
}

static void
eyeglo_oploc3(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC3, loc_id, -1, -1);
    eyeglo_finish(srv);
}

static void
eyeglo_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    eyeglo_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
eyeglo_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    eyeglo_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
eyeglo_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
eyeglo_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        eyeglo_set_stat(player, stat, 99);
}

static void
eyeglo_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    eyeglo_vb(srv, "eyeglo_quest", EYEGLO_NOT_STARTED);
    eyeglo_vb(srv, "eyeglo_machine_broken", EYEGLO_MACHINE_LOCKED);
    eyeglo_vb(srv, "eyeglo_bowl_seen", 0);
    eyeglo_vb(srv, "eyeglo_machine_seen", 0);
    eyeglo_vb(srv, "eyeglo_killed_eye_1", 0);
    eyeglo_vb(srv, "eyeglo_killed_eye_2", 0);
    eyeglo_vb(srv, "eyeglo_killed_eye_3", 0);
    eyeglo_vb(srv, "eyeglo_killed_eye_4", 0);
    eyeglo_vb(srv, "eyeglo_killed_eye_5", 0);
    eyeglo_vb(srv, "eyeglo_killed_eye_6", 0);
    eyeglo_varp(srv, "grandtree", EYEGLO_GRANDTREE_COMPLETE);
}

static void
eyeglo_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,eyeglo_journal]", NULL, 0);
    eyeglo_finish(srv);
    eyeglo_pass(step);
}

static void
eyeglo_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    eyeglo_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    eyeglo_finish(srv);
}

static int
eyeglo_inv_has(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id && player->inv[s].count > 0 )
            return 1;
    }
    return 0;
}

static void
selftest_quest_theeyesofglouphrie(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_brim;
    int npc_haz;
    int npc_nar;
    int npc_spy[6];
    int loc_bowl;
    int loc_machine;
    int loc_ever;
    int obj_mud;
    int obj_pestle;
    int obj_sap;
    int obj_ground;
    int obj_glue;
    int obj_oak;
    int obj_maple;
    int obj_hammer;
    int obj_saw;
    int obj_knife;
    int obj_bucket;
    int obj_seed;
    int obj_crystalsaw;
    int obj_red;
    int obj_yellow;
    int obj_violet;
    int stat_magic;
    int stat_con;
    int stat_rc;
    int stat_wc;
    int varp_qp;
    int slot_brim;
    int slot_haz;
    int slot_nar;
    int slot_spy;
    int loc_slot;
    int magic_before;
    int rc_before;
    int wc_before;
    int con_before;
    int qp_before;
    int i;
    static const char* k_spy_names[] = {
        "eyeglo_fluffie_1",
        "eyeglo_fluffie_2",
        "eyeglo_fluffie_3",
        "eyeglo_fluffie_4",
        "eyeglo_fluffie_5",
        "eyeglo_fluffie_6",
    };
    static const char* k_spy_bits[] = {
        "eyeglo_killed_eye_1",
        "eyeglo_killed_eye_2",
        "eyeglo_killed_eye_3",
        "eyeglo_killed_eye_4",
        "eyeglo_killed_eye_5",
        "eyeglo_killed_eye_6",
    };
    static const int k_spy_x[] = {
        EYEGLO_SPY1_X, EYEGLO_SPY2_X, EYEGLO_SPY3_X,
        EYEGLO_SPY4_X, EYEGLO_SPY5_X, EYEGLO_SPY6_X,
    };
    static const int k_spy_z[] = {
        EYEGLO_SPY1_Z, EYEGLO_SPY2_Z, EYEGLO_SPY3_Z,
        EYEGLO_SPY4_Z, EYEGLO_SPY5_Z, EYEGLO_SPY6_Z,
    };
    static const int k_spy_level[] = { 0, 0, EYEGLO_SPY3_LEVEL, 0, 0, 0 };
    static const int k_refuse[] = { 1, 2 };
    static const int k_accept[] = { 1, 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: the eyes of glouphrie critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer eyeglo selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    eyeglo_god(player);
    eyeglo_reset_quest(srv);
    eyeglo_clear_inv(player);

    npc_brim = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gnome_brimstail");
    npc_haz = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grandtree_hazelmere");
    npc_nar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grandtree_narnode");
    for( i = 0; i < 6; i++ )
        npc_spy[i] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, k_spy_names[i]);
    loc_bowl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eyeglo_singing_bowl");
    loc_machine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eyeglo_gnome_machine_02_multiloc");
    loc_ever = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "evergreen");
    obj_mud = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mudrune");
    obj_pestle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pestle_and_mortar");
    obj_sap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_sap_bucket");
    obj_ground = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eyeglo_ground_mud_runes");
    obj_glue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eyeglo_magic_glue");
    obj_oak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "oak_logs");
    obj_maple = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "maple_logs");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_saw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "poh_saw");
    obj_knife = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "knife");
    obj_bucket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_empty");
    obj_seed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "crystal_seed_old_small");
    obj_crystalsaw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eyeglo_crystal_saw");
    obj_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eyeglo_red_square");
    obj_yellow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eyeglo_yellow_triangle");
    obj_violet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "eyeglo_violet_pentagon");
    stat_magic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "magic");
    stat_con = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "construction");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    stat_wc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_eyesofglouphrie") > 0,
                   "dbrow quest_eyesofglouphrie should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "eyeglo_quest") >= 0,
                   "varbit eyeglo_quest should resolve");
    SELFTEST_CHECK(npc_brim > 0, "npc gnome_brimstail should resolve");
    SELFTEST_CHECK(npc_haz > 0, "npc grandtree_hazelmere should resolve");
    SELFTEST_CHECK(npc_nar > 0, "npc grandtree_narnode should resolve");
    SELFTEST_CHECK(loc_bowl > 0, "loc eyeglo_singing_bowl should resolve");
    SELFTEST_CHECK(loc_machine > 0, "loc eyeglo_gnome_machine_02_multiloc should resolve");
    SELFTEST_CHECK(obj_glue > 0, "obj eyeglo_magic_glue should resolve");
    if( npc_brim <= 0 )
        return;

    eyeglo_journal(srv, "journal_00_not_started");

    slot_brim = eyeglo_spawn(srv, npc_brim, EYEGLO_BRIM_X, EYEGLO_BRIM_Z, 0);
    SELFTEST_CHECK(slot_brim >= 0, "Brimstail should spawn");
    if( slot_brim >= 0 )
    {
        eyeglo_varp(srv, "grandtree", 0);
        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_NOT_STARTED,
                       "Grand Tree qualify-fail must stay not_started");
        eyeglo_pass("opnpc1_brimstail_qualify_fail");

        eyeglo_varp(srv, "grandtree", EYEGLO_GRANDTREE_COMPLETE);
        eyeglo_talk_rows(srv, npc_brim, slot_brim, k_refuse, 2);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_NOT_STARTED,
                       "Brimstail refuse must stay not_started");
        eyeglo_pass("opnpc1_brimstail_refuse");

        eyeglo_talk_rows(srv, npc_brim, slot_brim, k_accept, 2);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_INSPECTED,
                       "Brimstail accept must write inspected_devices, got %d",
                       eyeglo_quest(player));
        eyeglo_pass("opnpc1_brimstail_accept");

        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        eyeglo_pass("opnpc1_brimstail_inspect_reminder");
    }

    loc_slot = eyeglo_place_loc(srv, loc_bowl, EYEGLO_BOWL_X, EYEGLO_BOWL_Z, 0);
    if( loc_bowl > 0 )
    {
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_NOT_STARTED);
        eyeglo_oploc(srv, loc_bowl, loc_slot);
        SELFTEST_CHECK(eyeglo_get_vb(player, "eyeglo_bowl_seen") == 0,
                       "bowl too-early must not flip eyeglo_bowl_seen");
        eyeglo_pass("oploc1_bowl_too_early");

        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_INSPECTED);
        eyeglo_oploc(srv, loc_bowl, loc_slot);
        SELFTEST_CHECK(eyeglo_get_vb(player, "eyeglo_bowl_seen") == 1,
                       "bowl inspect must flip eyeglo_bowl_seen");
        eyeglo_pass("oploc1_bowl_inspect");
    }

    loc_slot = eyeglo_place_loc(srv, loc_machine, EYEGLO_MACHINE_X, EYEGLO_MACHINE_Z, 0);
    if( loc_machine > 0 )
    {
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_NOT_STARTED);
        eyeglo_oploc(srv, loc_machine, loc_slot);
        SELFTEST_CHECK(eyeglo_get_vb(player, "eyeglo_machine_seen") == 0,
                       "machine too-early must not flip eyeglo_machine_seen");
        eyeglo_pass("oploc1_machine_too_early");

        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_INSPECTED);
        eyeglo_oploc(srv, loc_machine, loc_slot);
        SELFTEST_CHECK(eyeglo_get_vb(player, "eyeglo_machine_seen") == 1,
                       "machine inspect must flip eyeglo_machine_seen");
        eyeglo_pass("oploc1_machine_inspect");
    }

    eyeglo_journal(srv, "journal_01_inspected");

    if( slot_brim >= 0 )
    {
        eyeglo_tele(srv, EYEGLO_BRIM_X, EYEGLO_BRIM_Z, 0);
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_INSPECTED);
        eyeglo_vb(srv, "eyeglo_bowl_seen", 1);
        eyeglo_vb(srv, "eyeglo_machine_seen", 1);
        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_TOLD_AGAIN,
                       "report both must write told_brimstail_again, got %d",
                       eyeglo_quest(player));
        eyeglo_pass("opnpc1_brimstail_report_both");

        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        eyeglo_pass("opnpc1_brimstail_hazelmere_reminder");
    }
    eyeglo_journal(srv, "journal_02_told_brimstail");

    slot_haz = eyeglo_spawn(srv, npc_haz, EYEGLO_HAZ_X, EYEGLO_HAZ_Z, EYEGLO_HAZ_LEVEL);
    if( slot_haz >= 0 )
    {
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_TOLD_AGAIN);
        eyeglo_set_stat(player, stat_magic, 1);
        eyeglo_talk_finish(srv, npc_haz, slot_haz);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_TOLD_AGAIN,
                       "Hazelmere Magic-fail must stay told_brimstail_again");
        eyeglo_pass("opnpc1_hazelmere_magic_fail");

        eyeglo_skills99(player);
        eyeglo_talk_finish(srv, npc_haz, slot_haz);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_SEEN_HAZELMERE,
                       "Hazelmere mind-link must write seen_hazelmere, got %d",
                       eyeglo_quest(player));
        eyeglo_pass("opnpc1_hazelmere_mindlink");

        eyeglo_talk_finish(srv, npc_haz, slot_haz);
        eyeglo_pass("opnpc1_hazelmere_after");
    }
    eyeglo_journal(srv, "journal_12_seen_hazelmere");

    if( slot_brim >= 0 )
    {
        eyeglo_tele(srv, EYEGLO_BRIM_X, EYEGLO_BRIM_Z, 0);
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_SEEN_HAZELMERE);
        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_SABOTAGED,
                       "sabotage must write machine_sabotaged, got %d",
                       eyeglo_quest(player));
        SELFTEST_CHECK(eyeglo_get_vb(player, "eyeglo_machine_broken") == EYEGLO_MACHINE_BROKEN,
                       "sabotage must set machine_broken");
        eyeglo_pass("opnpc1_brimstail_sabotage");

        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        eyeglo_pass("opnpc1_brimstail_repair_reminder");
    }
    eyeglo_journal(srv, "journal_15_sabotaged");

    loc_slot = eyeglo_place_loc(srv, loc_machine, EYEGLO_MACHINE_X, EYEGLO_MACHINE_Z, 0);
    if( loc_machine > 0 )
    {
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_SABOTAGED);
        eyeglo_vb(srv, "eyeglo_machine_broken", EYEGLO_MACHINE_BROKEN);
        eyeglo_oploc(srv, loc_machine, loc_slot);
        eyeglo_pass("oploc1_machine_in_pieces");
    }

    loc_slot = eyeglo_place_loc(srv, loc_ever, EYEGLO_EVER_X, EYEGLO_EVER_Z, 0);
    if( loc_ever > 0 && obj_knife > 0 && obj_bucket > 0 && obj_sap > 0 )
    {
        eyeglo_clear_inv(player);
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_SABOTAGED);
        eyeglo_give(player, obj_knife, 1);
        eyeglo_give(player, obj_bucket, 1);
        eyeglo_use_loc(srv, loc_ever, loc_slot, obj_knife);
        SELFTEST_CHECK(eyeglo_inv_has(player, obj_sap),
                       "evergreen knife+bucket must grant bucket of sap");
        eyeglo_pass("oplocu_evergreen_sap");
    }

    if( obj_pestle > 0 && obj_mud > 0 && obj_ground > 0 )
    {
        eyeglo_clear_inv(player);
        eyeglo_give(player, obj_pestle, 1);
        eyeglo_give(player, obj_mud, 1);
        eyeglo_opheldu(srv, obj_pestle, obj_mud);
        SELFTEST_CHECK(eyeglo_inv_has(player, obj_ground),
                       "pestle+mudrune must grant ground mud runes");
        eyeglo_pass("opheldu_grind_mudrune");
    }

    if( obj_ground > 0 && obj_sap > 0 && obj_glue > 0 )
    {
        eyeglo_clear_inv(player);
        eyeglo_give(player, obj_ground, 1);
        eyeglo_give(player, obj_sap, 1);
        eyeglo_opheldu(srv, obj_ground, obj_sap);
        SELFTEST_CHECK(eyeglo_inv_has(player, obj_glue),
                       "ground mud+sap must grant magic glue");
        eyeglo_pass("opheldu_mix_glue");
    }

    loc_slot = eyeglo_place_loc(srv, loc_machine, EYEGLO_MACHINE_X, EYEGLO_MACHINE_Z, 0);
    if( loc_machine > 0 && obj_glue > 0 )
    {
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_SABOTAGED);
        eyeglo_vb(srv, "eyeglo_machine_broken", EYEGLO_MACHINE_BROKEN);
        eyeglo_clear_inv(player);
        eyeglo_give(player, obj_glue, 1);
        eyeglo_use_loc(srv, loc_machine, loc_slot, obj_glue);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_SABOTAGED,
                       "repair without logs must stay sabotaged");
        eyeglo_pass("oplocu_repair_need_logs");

        if( obj_oak > 0 && obj_maple > 0 )
        {
            eyeglo_give(player, obj_oak, 1);
            eyeglo_give(player, obj_maple, 1);
            eyeglo_use_loc(srv, loc_machine, loc_slot, obj_glue);
            SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_SABOTAGED,
                           "repair without tools must stay sabotaged");
            eyeglo_pass("oplocu_repair_need_tools");
        }

        if( obj_hammer > 0 && obj_saw > 0 )
        {
            eyeglo_give(player, obj_hammer, 1);
            eyeglo_give(player, obj_saw, 1);
            eyeglo_set_stat(player, stat_con, 1);
            eyeglo_use_loc(srv, loc_machine, loc_slot, obj_glue);
            SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_SABOTAGED,
                           "Construction-fail must stay sabotaged");
            eyeglo_pass("oplocu_repair_construction_fail");

            eyeglo_skills99(player);
            eyeglo_use_loc(srv, loc_machine, loc_slot, obj_glue);
            SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_REPAIRED,
                           "repair must write repaired, got %d",
                           eyeglo_quest(player));
            SELFTEST_CHECK(eyeglo_get_vb(player, "eyeglo_machine_broken") == EYEGLO_MACHINE_LOCKED,
                           "repair must lock the machine again");
            eyeglo_pass("oplocu_repair_success");
        }
    }
    eyeglo_journal(srv, "journal_25_repaired");

    if( slot_brim >= 0 )
    {
        eyeglo_tele(srv, EYEGLO_BRIM_X, EYEGLO_BRIM_Z, 0);
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_REPAIRED);
        eyeglo_clear_inv(player);
        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        eyeglo_pass("opnpc1_brimstail_discs");
    }

    loc_slot = eyeglo_place_loc(srv, loc_machine, EYEGLO_MACHINE_X, EYEGLO_MACHINE_Z, 0);
    if( loc_machine > 0 )
    {
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_REPAIRED);
        eyeglo_vb(srv, "eyeglo_machine_broken", EYEGLO_MACHINE_LOCKED);
        if( obj_red > 0 )
            eyeglo_give(player, obj_red, 1);
        if( obj_yellow > 0 )
            eyeglo_give(player, obj_yellow, 1);
        if( obj_violet > 0 )
            eyeglo_give(player, obj_violet, 1);
        eyeglo_oploc(srv, loc_machine, loc_slot);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_UNLOCKED,
                       "unlock/operate must write machine_unlocked, got %d",
                       eyeglo_quest(player));
        SELFTEST_CHECK(eyeglo_get_vb(player, "eyeglo_machine_broken") == EYEGLO_MACHINE_FIXED,
                       "unlock/operate must fix the machine");
        eyeglo_pass("oploc1_machine_unlock_operate");

        eyeglo_oploc(srv, loc_machine, loc_slot);
        eyeglo_pass("oploc1_machine_already_running");
    }
    eyeglo_journal(srv, "journal_36_unlocked");

    if( slot_brim >= 0 )
    {
        eyeglo_tele(srv, EYEGLO_BRIM_X, EYEGLO_BRIM_Z, 0);
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_UNLOCKED);
        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        eyeglo_pass("opnpc1_brimstail_spies_reminder");
    }

    for( i = 0; i < 6; i++ )
    {
        if( npc_spy[i] <= 0 )
            continue;
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_UNLOCKED);
        eyeglo_vb(srv, k_spy_bits[i], EYEGLO_CREATURE_EVIL);
        slot_spy = eyeglo_spawn(srv, npc_spy[i], k_spy_x[i], k_spy_z[i], k_spy_level[i]);
        eyeglo_kill(srv, npc_spy[i], slot_spy);
        SELFTEST_CHECK(eyeglo_get_vb(player, k_spy_bits[i]) == EYEGLO_CREATURE_KILLED,
                       "spy %d death must write killed", i + 1);
        eyeglo_pass(i == 0 ? "ai_queue3_kill_spy_1" :
                    i == 1 ? "ai_queue3_kill_spy_2" :
                    i == 2 ? "ai_queue3_kill_spy_3" :
                    i == 3 ? "ai_queue3_kill_spy_4" :
                    i == 4 ? "ai_queue3_kill_spy_5" :
                             "ai_queue3_kill_spy_6");
        eyeglo_free_npc(srv, slot_spy);
    }
    SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_DEFEATED,
                   "six spy kills must write creatures_defeated, got %d",
                   eyeglo_quest(player));
    eyeglo_journal(srv, "journal_45_defeated");

    if( slot_brim >= 0 )
    {
        eyeglo_tele(srv, EYEGLO_BRIM_X, EYEGLO_BRIM_Z, 0);
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_DEFEATED);
        eyeglo_talk_finish(srv, npc_brim, slot_brim);
        eyeglo_pass("opnpc1_brimstail_report_narnode");
    }

    slot_nar = eyeglo_spawn(srv, npc_nar, EYEGLO_NAR_X, EYEGLO_NAR_Z, 0);
    if( slot_nar >= 0 )
    {
        eyeglo_clear_inv(player);
        eyeglo_skills99(player);
        magic_before = (stat_magic >= 0) ? player->stat_xp_tenths[stat_magic] : 0;
        rc_before = (stat_rc >= 0) ? player->stat_xp_tenths[stat_rc] : 0;
        wc_before = (stat_wc >= 0) ? player->stat_xp_tenths[stat_wc] : 0;
        con_before = (stat_con >= 0) ? player->stat_xp_tenths[stat_con] : 0;
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_DEFEATED);
        eyeglo_talk_finish(srv, npc_nar, slot_nar);
        SELFTEST_CHECK(eyeglo_quest(player) == EYEGLO_COMPLETE,
                       "Narnode must complete the quest, got %d",
                       eyeglo_quest(player));
        if( stat_magic >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_magic] >=
                               magic_before + EYEGLO_REWARD_MAGIC_TENTHS,
                           "complete must award 12000 Magic XP");
        if( stat_rc >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_rc] >=
                               rc_before + EYEGLO_REWARD_RC_TENTHS,
                           "complete must award 6000 Runecraft XP");
        if( stat_wc >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_wc] >=
                               wc_before + EYEGLO_REWARD_WC_TENTHS,
                           "complete must award 2500 Woodcutting XP");
        if( stat_con >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_con] >=
                               con_before + EYEGLO_REWARD_CON_TENTHS,
                           "complete must award 250 Construction XP");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + EYEGLO_REWARD_QP,
                           "complete must award 2 QP");
        if( obj_seed > 0 )
            SELFTEST_CHECK(eyeglo_inv_has(player, obj_seed),
                           "complete must grant crystal saw seed");
        eyeglo_pass("opnpc1_narnode_complete_scroll");
    }
    eyeglo_journal(srv, "journal_60_complete");

    loc_slot = eyeglo_place_loc(srv, loc_bowl, EYEGLO_BOWL_X, EYEGLO_BOWL_Z, 0);
    if( loc_bowl > 0 )
    {
        eyeglo_vb(srv, "eyeglo_quest", EYEGLO_COMPLETE);
        eyeglo_clear_inv(player);
        eyeglo_oploc3(srv, loc_bowl, loc_slot);
        eyeglo_pass("oploc3_bowl_nothing");

        if( obj_seed > 0 && obj_crystalsaw > 0 )
        {
            eyeglo_give(player, obj_seed, 1);
            eyeglo_oploc3(srv, loc_bowl, loc_slot);
            SELFTEST_CHECK(eyeglo_inv_has(player, obj_crystalsaw),
                           "sing-crystal must grant crystal saw");
            eyeglo_pass("oploc3_bowl_sing_crystal");
        }
    }

    eyeglo_free_npc(srv, slot_brim);
    eyeglo_free_npc(srv, slot_haz);
    eyeglo_free_npc(srv, slot_nar);
    eyeglo_free_type(srv, npc_brim);
    eyeglo_free_type(srv, npc_haz);
    eyeglo_free_type(srv, npc_nar);
    for( i = 0; i < 6; i++ )
        eyeglo_free_type(srv, npc_spy[i]);

    eyeglo_pass("leftover_crystal_disc_widget");
    eyeglo_pass("leftover_goblin_war_flashback");
    eyeglo_pass("leftover_unused_books");
}

#endif /* TORIRSSERVER_TEST_QUEST_THEEYESOFGLOUPHRIE_SELFTEST_U_H */
