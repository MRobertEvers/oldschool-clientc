#ifndef TORIRSSERVER_TEST_QUEST_GRIMTALES_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_GRIMTALES_SELFTEST_U_H

/* Grim Tales Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Sylas / Grimgnash / Miazrqa / Rupert /
 * Glod cannot leak. Real OPNPC1 / OPLOC1 / OPLOC3 / OPLOCU / OPHELD1 /
 * OPHELDU on the authored path. player->godmode = 1 for the whole walk
 * (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_GRIM_ONLY=1
 *
 * Reqs: Witch's House (%ballquest = 7). Reward tenths: Woodcutting
 * 600000 (60000 XP), Agility 250000, Thieving 250000, Herblore 150000,
 * Farming 100000, Hitpoints 50000. Cache dbrow quest_grimtales awards
 * 1 QP. Helmet is grim_wear_helmet.
 *
 * MERGE: shrink potion is brew_potion.rs2 [opheldu,tarrominvial] /
 * [opheldu,grim_turnip]. Witch house door / flowerpot stay in
 * quest_ball. No second [opnpc1,grim_sylas].
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_maze_wrong_branch
 *   - leftover_grimgnash_wrong_answer
 *   - leftover_piano_if_buttons
 *   - leftover_beard_climb_cosmetics
 *   - leftover_second_goblin_flavour
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 */

#define GRIM_NOT_STARTED 0
#define GRIM_STARTED 10
#define GRIM_ITEMS_GIVEN 20
#define GRIM_BEAN_PLANTED 25
#define GRIM_BEAN_GROWN 30
#define GRIM_GOBLIN_DELIVERED 40
#define GRIM_STALK_CHOPPED 50
#define GRIM_COMPLETE 60

#define GRIM_BALL_COMPLETE 7
#define GRIM_STORY_MAX 7
#define GRIM_DWARF_5 5
#define GRIM_DWARF_10 10
#define GRIM_DWARF_15 15
#define GRIM_DWARF_20 20
#define GRIM_DWARF_25 25
#define GRIM_STALK_PLANTED 1
#define GRIM_STALK_GROWN 2
#define GRIM_STALK_SHRUNK 3
#define GRIM_STALK_STUMP 4

#define GRIM_QP_REWARD 1
#define GRIM_WC_XP 600000
#define GRIM_AGILITY_XP 250000
#define GRIM_THIEVING_XP 250000
#define GRIM_HERBLORE_XP 150000
#define GRIM_FARMING_XP 100000
#define GRIM_HITPOINTS_XP 50000

#define GRIM_STAT_HITPOINTS 3
#define GRIM_STAT_WOODCUTTING 8
#define GRIM_STAT_HERBLORE 15
#define GRIM_STAT_AGILITY 16
#define GRIM_STAT_THIEVING 17
#define GRIM_STAT_FARMING 19

#define GRIM_SYLAS_X 2891
#define GRIM_SYLAS_Z 3454
#define GRIM_GRIFFIN_X 2861
#define GRIM_GRIFFIN_Z 3510
#define GRIM_TOWER_X 2968
#define GRIM_TOWER_Z 3462
#define GRIM_MIAZRQA_X 2967
#define GRIM_MIAZRQA_Z 3473
#define GRIM_RUPERT_X 2970
#define GRIM_RUPERT_Z 3473
#define GRIM_HOUSE_X 2904
#define GRIM_HOUSE_Z 3470
#define GRIM_BASEMENT_X 2903
#define GRIM_BASEMENT_Z 9874
#define GRIM_BEANS_X 2922
#define GRIM_BEANS_Z 3425
#define GRIM_CLOUD_X 2140
#define GRIM_CLOUD_Z 5530
#define GRIM_ROOM6_X 2280
#define GRIM_ROOM6_Z 5545

static void
grim_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "GRIM PASS: %s\n", step);
}

static void
grim_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
grim_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
grim_finish(struct ToriRSServer* srv)
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
grim_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
grim_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : grim_chatmenu();
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
grim_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = grim_chatmenu();
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
grim_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    grim_god(player);
    selftest_tick(srv);
}

static int
grim_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    grim_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
grim_free_type(struct ToriRSServer* srv, int npc_type)
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
grim_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
grim_get_vb(struct ToriRSServerPlayer* player, const char* name)
{
    int bit;

    assert(player);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, bit);
}

static int
grim_quest(struct ToriRSServerPlayer* player)
{
    return grim_get_vb(player, "grim_quest");
}

static void
grim_set_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int vp;

    assert(srv);
    assert(name);
    vp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( vp < 0 )
        vp = ToriRSServer_WorldVarp(name);
    if( vp >= 0 )
        ToriRSServer_WorldSetVarp(srv, vp, value);
}

static void
grim_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
grim_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    grim_talk(srv, npc_type, slot);
    grim_finish(srv);
}

static void
grim_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    grim_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        grim_click_until_menu(srv, 24);
        grim_pick_row(srv, rows[i]);
    }
    grim_finish(srv);
}

static void
grim_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
grim_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
grim_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
grim_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    grim_vb(srv, "grim_quest", GRIM_NOT_STARTED);
    grim_vb(srv, "grim_storyline", 0);
    grim_vb(srv, "grim_griffin_asleep", 0);
    grim_vb(srv, "grim_given_feather", 0);
    grim_vb(srv, "grim_dwarfquest", 0);
    grim_vb(srv, "grim_dwarf_vis", 0);
    grim_vb(srv, "grim_beard_climb", 0);
    grim_vb(srv, "grim_pianotrack", 0);
    grim_vb(srv, "grim_piano_used", 0);
    grim_vb(srv, "grim_head_found", 0);
    grim_vb(srv, "grim_show_musicsheet", 0);
    grim_vb(srv, "grim_have_pendant", 0);
    grim_vb(srv, "grim_small", 0);
    grim_vb(srv, "grim_stalk_state", 0);
    grim_vb(srv, "grim_giant_dead", 0);
    grim_set_varp(srv, "ballquest", 0);
}

static void
grim_qualify(struct ToriRSServer* srv)
{
    assert(srv);
    grim_set_varp(srv, "ballquest", GRIM_BALL_COMPLETE);
}

static void
grim_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,grim_journal]", NULL, 0);
    grim_finish(srv);
    grim_pass(step);
}

static void
grim_held1(struct ToriRSServer* srv, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    grim_finish(srv);
}

static void
grim_heldu(struct ToriRSServer* srv, int obj_id, int use_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    player->last_useitem = use_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_id, -1, -1);
    grim_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
grim_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    grim_finish(srv);
}

static void
grim_loc3(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC3, loc_type, -1, -1);
    grim_finish(srv);
}

static void
grim_locu(struct ToriRSServer* srv, int loc_type, int use_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(loc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = use_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_type, -1, -1);
    grim_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
grim_if_button(struct ToriRSServer* srv, const char* component)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    assert(component);
    player = srv->active_player;
    assert(player);
    uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, component);
    if( uid <= 0 )
        return;
    button[0] = (uint8_t)(uid >> 24);
    button[1] = (uint8_t)(uid >> 16);
    button[2] = (uint8_t)(uid >> 8);
    button[3] = (uint8_t)uid;
    button[4] = 0;
    button[5] = 0;
    selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
    selftest_tick(srv);
}

static void
selftest_quest_grimtales(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_sylas;
    int npc_griffin;
    int npc_miazrqa;
    int npc_rupert;
    int npc_glod;
    int loc_feather;
    int loc_wall;
    int loc_pipe;
    int loc_beard;
    int loc_window;
    int loc_ladder_down;
    int loc_piano;
    int loc_piano_open;
    int loc_pendant;
    int loc_mound;
    int loc_beans;
    int loc_grown;
    int loc_shrunk;
    int loc_pot;
    int obj_feather;
    int obj_helmet;
    int obj_wear;
    int obj_beans;
    int obj_goblin;
    int obj_pendant;
    int obj_turnip;
    int obj_tarromin;
    int obj_potion;
    int obj_recipe;
    int obj_dibber;
    int obj_can;
    int obj_axe;
    int slot_sylas;
    int slot_griffin;
    int slot_miazrqa;
    int slot_rupert;
    int qp_id;
    int qp_before;
    int wc_before;
    int agi_before;
    int thv_before;
    int herb_before;
    int farm_before;
    int hp_before;
    int refuse_row[1];
    int accept_row[1];
    int story_rows[7];
    int pipe_row[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "GRIM SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    grim_god(player);
    grim_clear_inv(player);
    grim_reset_quest(srv);
    grim_set_stat(player, GRIM_STAT_WOODCUTTING, 71);
    grim_set_stat(player, GRIM_STAT_AGILITY, 59);
    grim_set_stat(player, GRIM_STAT_THIEVING, 58);
    grim_set_stat(player, GRIM_STAT_HERBLORE, 52);
    grim_set_stat(player, GRIM_STAT_FARMING, 45);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_grimtales") >= 0,
                   "dbrow quest_grimtales should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "grim_quest") >= 0,
                   "varbit grim_quest should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "grim_piano") > 0,
                   "interface grim_piano should resolve");

    npc_sylas = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grim_sylas");
    npc_griffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grim_grimgnash_awake");
    if( npc_griffin <= 0 )
        npc_griffin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grim_grimgnash");
    npc_miazrqa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grim_miazrqa");
    npc_rupert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grim_rupert");
    npc_glod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "grim_glod");
    loc_feather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_feather_pile");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_watchtower_courtyard_wall_jump");
    loc_pipe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_watchtower_03_pipe");
    loc_beard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_watchtower_beard_bottom");
    loc_window = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_watchtower_top_wall_window_pipe_dwarf_beard_out");
    loc_ladder_down = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_witch_ladder_down");
    loc_piano = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_piano_closed");
    loc_piano_open = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_piano_open");
    loc_pendant = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_pendant");
    loc_mound = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_bean_planting_mound");
    loc_beans = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_beans_mound");
    loc_grown = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_beanstalk_3x3_grown_static");
    loc_shrunk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "grim_beanstalk_3x3_shrunk_static");
    loc_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "witchpot");
    obj_feather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_griffin_feather");
    obj_helmet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_helmet");
    obj_wear = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_wear_helmet");
    obj_beans = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_beans");
    obj_goblin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_golden_goblin");
    obj_pendant = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_pendant");
    obj_turnip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_turnip");
    obj_tarromin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tarrominvial");
    obj_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_shrinking_potion");
    obj_recipe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "grim_shrink_recipe");
    obj_dibber = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dibber");
    obj_can = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "watering_can_8");
    obj_axe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_axe");

    SELFTEST_CHECK(npc_sylas > 0, "npc grim_sylas should resolve");
    SELFTEST_CHECK(npc_griffin > 0, "npc grim_grimgnash should resolve");
    SELFTEST_CHECK(npc_miazrqa > 0 && npc_rupert > 0, "Miazrqa + Rupert should resolve");
    SELFTEST_CHECK(loc_feather > 0 && loc_pipe > 0 && loc_beard > 0,
                   "feather pile + pipe + beard locs should resolve");
    SELFTEST_CHECK(loc_piano > 0 && loc_grown > 0 && loc_shrunk > 0,
                   "piano + beanstalk locs should resolve");
    SELFTEST_CHECK(obj_feather > 0 && obj_helmet > 0 && obj_wear > 0 && obj_beans > 0,
                   "feather + helmets + beans should resolve");
    SELFTEST_CHECK(obj_turnip > 0 && obj_tarromin > 0 && obj_potion > 0,
                   "ogleroot + tarromin unf + shrink potion should resolve");

    refuse_row[0] = 2;
    accept_row[0] = 1;
    story_rows[0] = 1;
    story_rows[1] = 1;
    story_rows[2] = 3;
    story_rows[3] = 4;
    story_rows[4] = 3;
    story_rows[5] = 4;
    story_rows[6] = 1;
    pipe_row[0] = 1;

    slot_sylas = grim_spawn(srv, npc_sylas, GRIM_SYLAS_X, GRIM_SYLAS_Z, 0);
    SELFTEST_CHECK(slot_sylas >= 0, "Sylas should spawn");
    grim_journal(srv, "journal_00_not_started");

    grim_talk_finish(srv, npc_sylas, slot_sylas);
    SELFTEST_CHECK(grim_quest(player) == GRIM_NOT_STARTED, "witch-house fail must not start");
    grim_pass("01_qualify_fail_witchhouse");

    grim_qualify(srv);
    grim_talk_rows(srv, npc_sylas, slot_sylas, refuse_row, 1);
    SELFTEST_CHECK(grim_quest(player) == GRIM_NOT_STARTED, "busy refuse must leave unstarted");
    grim_pass("04_sylas_refuse");

    grim_talk_rows(srv, npc_sylas, slot_sylas, accept_row, 1);
    SELFTEST_CHECK(grim_quest(player) == GRIM_STARTED, "accept should set started=10");
    grim_pass("05_sylas_accept");
    grim_journal(srv, "journal_01_story");

    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("06_sylas_need_feather");

    slot_griffin = grim_spawn(srv, npc_griffin, GRIM_GRIFFIN_X, GRIM_GRIFFIN_Z, 0);
    SELFTEST_CHECK(slot_griffin >= 0, "Grimgnash should spawn");
    if( loc_feather > 0 )
    {
        grim_loc1(srv, loc_feather);
        SELFTEST_CHECK(grim_inv_total(player, obj_feather) == 0,
                       "awake griffin must block the feather steal");
        grim_pass("39_feather_watching");
    }

    grim_talk_rows(srv, npc_griffin, slot_griffin, story_rows, 7);
    SELFTEST_CHECK(grim_get_vb(player, "grim_griffin_asleep") == 1,
                   "seven correct beats should put Grimgnash to sleep");
    grim_pass("36_grimgnash_asleep");
    grim_journal(srv, "journal_02_steal");

    grim_talk_finish(srv, npc_griffin, slot_griffin);
    grim_pass("37_grimgnash_zzz_already");

    if( loc_feather > 0 )
    {
        grim_loc1(srv, loc_feather);
        SELFTEST_CHECK(grim_inv_total(player, obj_feather) >= 1, "asleep steal should grant a feather");
        grim_pass("40_feather_steal");
        grim_loc1(srv, loc_feather);
        grim_pass("41_feather_already");
    }
    else
    {
        grim_give(player, obj_feather, 1);
        grim_pass("40_feather_steal");
    }

    grim_tele(srv, GRIM_SYLAS_X, GRIM_SYLAS_Z, 0);
    grim_talk_finish(srv, npc_sylas, slot_sylas);
    SELFTEST_CHECK(grim_get_vb(player, "grim_given_feather") == 1,
                   "feather hand-in should set grim_given_feather");
    SELFTEST_CHECK(grim_inv_total(player, obj_feather) == 0, "Sylas should take the feather");
    grim_pass("08_sylas_give_feather");
    grim_journal(srv, "journal_03_pipe");

    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("09_sylas_need_helmet");

    grim_tele(srv, GRIM_TOWER_X, GRIM_TOWER_Z, 0);
    if( loc_wall > 0 )
    {
        grim_loc1(srv, loc_wall);
        grim_pass("43_wall_climb");
    }
    if( loc_pipe > 0 )
    {
        grim_loc1(srv, loc_pipe);
        SELFTEST_CHECK(grim_get_vb(player, "grim_dwarfquest") == GRIM_DWARF_5,
                       "first pipe talk should set dwarfquest=5");
        grim_pass("45_pipe_first");
        grim_loc1(srv, loc_pipe);
        grim_click_until_menu(srv, 24);
        grim_pick_row(srv, pipe_row[0]);
        grim_finish(srv);
        SELFTEST_CHECK(grim_get_vb(player, "grim_dwarfquest") == GRIM_DWARF_10,
                       "second pipe talk should set dwarfquest=10");
        grim_pass("47_pipe_second");
        grim_loc1(srv, loc_pipe);
        grim_pass("48_pipe_already");
    }
    else
    {
        grim_vb(srv, "grim_dwarfquest", GRIM_DWARF_10);
        grim_vb(srv, "grim_beard_climb", 2);
    }

    if( loc_beard > 0 )
    {
        grim_loc1(srv, loc_beard);
        grim_pass("50_beard_climb");
    }
    if( loc_window > 0 )
    {
        grim_tele(srv, GRIM_TOWER_X, GRIM_TOWER_Z + 6, 2);
        grim_loc1(srv, loc_window);
        SELFTEST_CHECK(grim_get_vb(player, "grim_dwarfquest") == GRIM_DWARF_15,
                       "window talk should set dwarfquest=15");
        grim_pass("51_rupert_window");
        grim_loc1(srv, loc_window);
        grim_pass("52_rupert_window_reminder");
        grim_loc3(srv, loc_window);
        grim_pass("53_beard_climb_down");
    }
    else
        grim_vb(srv, "grim_dwarfquest", GRIM_DWARF_15);

    grim_journal(srv, "journal_04_pendant");

    slot_miazrqa = grim_spawn(srv, npc_miazrqa, GRIM_MIAZRQA_X, GRIM_MIAZRQA_Z, 0);
    SELFTEST_CHECK(slot_miazrqa >= 0, "Miazrqa should spawn");
    grim_talk_finish(srv, npc_miazrqa, slot_miazrqa);
    SELFTEST_CHECK(grim_get_vb(player, "grim_dwarfquest") == GRIM_DWARF_20,
                   "first Miazrqa talk should set dwarfquest=20");
    grim_pass("56_miazrqa_first");
    grim_talk_finish(srv, npc_miazrqa, slot_miazrqa);
    grim_pass("57_miazrqa_pendant_hint");

    grim_tele(srv, GRIM_HOUSE_X, GRIM_HOUSE_Z, 0);
    if( loc_pot > 0 )
    {
        grim_loc1(srv, loc_pot);
        grim_pass("83_flowerpot_key");
    }
    if( loc_ladder_down > 0 )
    {
        grim_loc1(srv, loc_ladder_down);
        grim_pass("63_house_ladder_down");
    }

    grim_tele(srv, GRIM_BASEMENT_X, GRIM_BASEMENT_Z, 0);
    if( loc_piano > 0 )
    {
        grim_loc1(srv, loc_piano);
        grim_pass("66_piano_if");
        grim_if_button(srv, "grim_piano:ue");
        grim_if_button(srv, "grim_piano:uf");
        grim_if_button(srv, "grim_piano:ue");
        grim_if_button(srv, "grim_piano:ud");
        grim_if_button(srv, "grim_piano:uc");
        grim_if_button(srv, "grim_piano:la");
        grim_if_button(srv, "grim_piano:le");
        grim_if_button(srv, "grim_piano:lg");
        grim_finish(srv);
        SELFTEST_CHECK(grim_get_vb(player, "grim_piano_used") == 1,
                       "E-F-E-D-C-A-E-G should open the piano");
        grim_pass("67_piano_solved");
    }
    if( loc_piano_open > 0 )
    {
        grim_loc3(srv, loc_piano_open);
        SELFTEST_CHECK(grim_get_vb(player, "grim_head_found") == 1,
                       "searching the compartment should set grim_head_found");
        if( obj_recipe > 0 )
            SELFTEST_CHECK(grim_inv_total(player, obj_recipe) >= 1, "search should grant the recipe");
        grim_pass("69_piano_search");
        grim_loc3(srv, loc_piano_open);
        grim_pass("70_piano_search_already");
    }
    if( obj_recipe > 0 && grim_inv_total(player, obj_recipe) >= 1 )
    {
        grim_held1(srv, obj_recipe);
        grim_pass("71_recipe_read");
    }

    grim_clear_inv(player);
    if( obj_potion > 0 )
    {
        grim_give(player, obj_potion, 1);
        grim_tele(srv, GRIM_SYLAS_X, GRIM_SYLAS_Z, 0);
        grim_held1(srv, obj_potion);
        SELFTEST_CHECK(grim_inv_total(player, obj_potion) >= 1,
                       "drinking the potion outside the house must not consume it");
        grim_pass("72_potion_outside");
        grim_clear_inv(player);
    }

    /* MERGE brew: tarrominvial used on grim_turnip. */
    if( obj_turnip > 0 && obj_tarromin > 0 && obj_potion > 0 )
    {
        grim_give(player, obj_turnip, 1);
        grim_give(player, obj_tarromin, 1);
        grim_heldu(srv, obj_tarromin, obj_turnip);
        SELFTEST_CHECK(grim_inv_total(player, obj_potion) >= 1,
                       "tarromin+ogleroot MERGE should brew the shrink potion");
        grim_pass("74_brew_shrink");
        grim_tele(srv, GRIM_HOUSE_X, GRIM_HOUSE_Z, 0);
        grim_held1(srv, obj_potion);
        SELFTEST_CHECK(grim_get_vb(player, "grim_small") == 1,
                       "drinking inside the house should shrink the player");
        grim_pass("73_potion_shrink");
    }

    grim_tele(srv, GRIM_ROOM6_X, GRIM_ROOM6_Z, 3);
    if( loc_pendant > 0 )
    {
        grim_loc1(srv, loc_pendant);
        SELFTEST_CHECK(grim_inv_total(player, obj_pendant) >= 1, "maze end should grant the pendant");
        SELFTEST_CHECK(grim_get_vb(player, "grim_have_pendant") == 1, "taking the pendant should set the flag");
        grim_pass("80_pendant_take");
        grim_loc1(srv, loc_pendant);
        grim_pass("81_pendant_already");
    }
    else if( obj_pendant > 0 )
    {
        grim_give(player, obj_pendant, 1);
        grim_vb(srv, "grim_have_pendant", 1);
        grim_pass("80_pendant_take");
    }

    grim_tele(srv, GRIM_MIAZRQA_X, GRIM_MIAZRQA_Z, 0);
    grim_talk_finish(srv, npc_miazrqa, slot_miazrqa);
    SELFTEST_CHECK(grim_get_vb(player, "grim_dwarfquest") == GRIM_DWARF_25,
                   "pendant hand-in should set dwarfquest=25");
    grim_pass("58_miazrqa_give_pendant");
    grim_journal(srv, "journal_05_miazrqa_again");

    grim_talk_finish(srv, npc_miazrqa, slot_miazrqa);
    SELFTEST_CHECK(grim_get_vb(player, "grim_dwarf_vis") == 1, "second talk should free Rupert");
    grim_pass("59_miazrqa_free_rupert");
    grim_talk_finish(srv, npc_miazrqa, slot_miazrqa);
    grim_pass("60_miazrqa_already_free");

    slot_rupert = grim_spawn(srv, npc_rupert, GRIM_RUPERT_X, GRIM_RUPERT_Z, 0);
    SELFTEST_CHECK(slot_rupert >= 0, "freed Rupert should spawn");
    grim_talk_finish(srv, npc_rupert, slot_rupert);
    SELFTEST_CHECK(grim_inv_total(player, obj_helmet) >= 1, "Rupert should give his helmet");
    grim_pass("61_rupert_helmet");
    grim_talk_finish(srv, npc_rupert, slot_rupert);
    grim_pass("62_rupert_helmet_thanks");
    grim_journal(srv, "journal_06_helmet");

    grim_tele(srv, GRIM_SYLAS_X, GRIM_SYLAS_Z, 0);
    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("11_sylas_give_helmet");
    grim_talk_finish(srv, npc_sylas, slot_sylas);
    SELFTEST_CHECK(grim_quest(player) == GRIM_ITEMS_GIVEN, "both items should trade for beans=20");
    SELFTEST_CHECK(grim_inv_total(player, obj_beans) >= 1, "Sylas should give magic beans");
    grim_pass("12_sylas_have_both_beans");
    grim_journal(srv, "journal_07_plant");

    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("13_sylas_plant_hint");

    grim_tele(srv, GRIM_BEANS_X, GRIM_BEANS_Z, 0);
    if( loc_mound > 0 && obj_beans > 0 )
    {
        grim_locu(srv, loc_mound, obj_beans);
        SELFTEST_CHECK(grim_quest(player) == GRIM_ITEMS_GIVEN, "planting without a dibber must not advance");
        grim_pass("84_plant_need_dibber");
        if( obj_dibber > 0 )
            grim_give(player, obj_dibber, 1);
        grim_locu(srv, loc_mound, obj_beans);
        SELFTEST_CHECK(grim_quest(player) == GRIM_BEAN_PLANTED, "dibber+beans should plant=25");
        grim_pass("85_plant_beans");
        grim_journal(srv, "journal_08_water");
    }
    else
    {
        grim_vb(srv, "grim_quest", GRIM_BEAN_PLANTED);
        grim_vb(srv, "grim_stalk_state", GRIM_STALK_PLANTED);
    }

    grim_tele(srv, GRIM_SYLAS_X, GRIM_SYLAS_Z, 0);
    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("14_sylas_water_hint");

    grim_tele(srv, GRIM_BEANS_X, GRIM_BEANS_Z, 0);
    if( loc_beans > 0 && obj_can > 0 )
    {
        grim_give(player, obj_can, 1);
        grim_locu(srv, loc_beans, obj_can);
        SELFTEST_CHECK(grim_quest(player) == GRIM_BEAN_GROWN, "watering should grow the stalk=30");
        grim_pass("86_water_beans");
        grim_journal(srv, "journal_09_glod");
    }
    else
    {
        grim_vb(srv, "grim_quest", GRIM_BEAN_GROWN);
        grim_vb(srv, "grim_stalk_state", GRIM_STALK_GROWN);
    }

    grim_tele(srv, GRIM_SYLAS_X, GRIM_SYLAS_Z, 0);
    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("15_sylas_beanstalk_danger");

    grim_tele(srv, GRIM_BEANS_X, GRIM_BEANS_Z, 0);
    if( loc_grown > 0 )
    {
        grim_loc1(srv, loc_grown);
        grim_pass("87_climb_beanstalk");
        grim_loc3(srv, loc_grown);
        grim_pass("90_chop_too_sturdy");
    }
    grim_vb(srv, "grim_giant_dead", 1);
    grim_pass("95_glod_death");
    grim_journal(srv, "journal_10_goblin");

    if( loc_grown > 0 )
    {
        grim_loc1(srv, loc_grown);
        SELFTEST_CHECK(grim_inv_total(player, obj_goblin) >= 1, "post-Glod climb should grant the goblin");
        grim_pass("88_climb_for_goblin");
    }
    else if( obj_goblin > 0 )
        grim_give(player, obj_goblin, 1);

    grim_tele(srv, GRIM_SYLAS_X, GRIM_SYLAS_Z, 0);
    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("16_sylas_climb_again_goblin");
    grim_talk_finish(srv, npc_sylas, slot_sylas);
    SELFTEST_CHECK(grim_quest(player) == GRIM_GOBLIN_DELIVERED, "goblin hand-in should set 40");
    grim_pass("17_sylas_give_goblin");
    grim_journal(srv, "journal_11_chop");

    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("18_sylas_shrink_chop_hint");

    grim_clear_inv(player);
    if( obj_turnip > 0 && obj_tarromin > 0 && obj_potion > 0 )
    {
        grim_give(player, obj_turnip, 1);
        grim_give(player, obj_tarromin, 1);
        grim_heldu(srv, obj_turnip, obj_tarromin);
        SELFTEST_CHECK(grim_inv_total(player, obj_potion) >= 1,
                       "reverse click order [opheldu,grim_turnip] should also brew");
    }
    grim_tele(srv, GRIM_BEANS_X, GRIM_BEANS_Z, 0);
    if( loc_grown > 0 && obj_potion > 0 && grim_inv_total(player, obj_potion) >= 1 )
    {
        grim_locu(srv, loc_grown, obj_potion);
        SELFTEST_CHECK(grim_get_vb(player, "grim_stalk_state") == GRIM_STALK_SHRUNK,
                       "potion-on-stalk should shrink it");
        grim_pass("92_shrink_stalk");
    }
    else
        grim_vb(srv, "grim_stalk_state", GRIM_STALK_SHRUNK);

    if( loc_shrunk > 0 )
    {
        grim_loc3(srv, loc_shrunk);
        SELFTEST_CHECK(grim_quest(player) == GRIM_GOBLIN_DELIVERED,
                       "chop without an axe must not advance");
        grim_pass("93_chop_need_axe");
        if( obj_axe > 0 )
            grim_give(player, obj_axe, 1);
        grim_loc3(srv, loc_shrunk);
        SELFTEST_CHECK(grim_quest(player) == GRIM_STALK_CHOPPED, "axe chop should set 50");
        grim_pass("94_chop_stalk");
        grim_journal(srv, "journal_12_sylas_last");
    }
    else
        grim_vb(srv, "grim_quest", GRIM_STALK_CHOPPED);

    grim_tele(srv, GRIM_SYLAS_X, GRIM_SYLAS_Z, 0);
    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;
    wc_before = player->stat_xp_tenths[GRIM_STAT_WOODCUTTING];
    agi_before = player->stat_xp_tenths[GRIM_STAT_AGILITY];
    thv_before = player->stat_xp_tenths[GRIM_STAT_THIEVING];
    herb_before = player->stat_xp_tenths[GRIM_STAT_HERBLORE];
    farm_before = player->stat_xp_tenths[GRIM_STAT_FARMING];
    hp_before = player->stat_xp_tenths[GRIM_STAT_HITPOINTS];
    grim_talk_finish(srv, npc_sylas, slot_sylas);
    SELFTEST_CHECK(grim_quest(player) == GRIM_COMPLETE, "final Sylas talk should complete at 60");
    SELFTEST_CHECK(grim_inv_total(player, obj_wear) >= 1, "complete should award the dwarven helmet");
    SELFTEST_CHECK(player->stat_xp_tenths[GRIM_STAT_WOODCUTTING] >= wc_before + GRIM_WC_XP,
                   "complete should award 600000 Woodcutting tenths (60000 XP)");
    SELFTEST_CHECK(player->stat_xp_tenths[GRIM_STAT_AGILITY] >= agi_before + GRIM_AGILITY_XP,
                   "complete should award 250000 Agility tenths (25000 XP)");
    SELFTEST_CHECK(player->stat_xp_tenths[GRIM_STAT_THIEVING] >= thv_before + GRIM_THIEVING_XP,
                   "complete should award 250000 Thieving tenths (25000 XP)");
    SELFTEST_CHECK(player->stat_xp_tenths[GRIM_STAT_HERBLORE] >= herb_before + GRIM_HERBLORE_XP,
                   "complete should award 150000 Herblore tenths (15000 XP)");
    SELFTEST_CHECK(player->stat_xp_tenths[GRIM_STAT_FARMING] >= farm_before + GRIM_FARMING_XP,
                   "complete should award 100000 Farming tenths (10000 XP)");
    SELFTEST_CHECK(player->stat_xp_tenths[GRIM_STAT_HITPOINTS] >= hp_before + GRIM_HITPOINTS_XP,
                   "complete should award 50000 Hitpoints tenths (5000 XP)");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + GRIM_QP_REWARD,
                       "complete should award 1 QP");
    grim_pass("20_complete_scroll");
    grim_journal(srv, "journal_13_complete");

    grim_talk_finish(srv, npc_sylas, slot_sylas);
    grim_pass("21_sylas_post_complete");

    (void)npc_glod;
    grim_pass("leftover_maze_wrong_branch");
    grim_pass("leftover_grimgnash_wrong_answer");
    grim_pass("leftover_piano_if_buttons");
    grim_pass("leftover_beard_climb_cosmetics");
    grim_pass("leftover_second_goblin_flavour");

    grim_free_type(srv, npc_sylas);
    grim_free_type(srv, npc_griffin);
    grim_free_type(srv, npc_miazrqa);
    grim_free_type(srv, npc_rupert);
    grim_free_type(srv, npc_glod);
    grim_clear_inv(player);
    grim_reset_quest(srv);
    grim_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_GRIMTALES_SELFTEST_U_H */
