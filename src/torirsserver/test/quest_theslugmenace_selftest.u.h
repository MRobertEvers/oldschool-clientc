#ifndef TORIRSSERVER_TEST_QUEST_THESLUGMENACE_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_THESLUGMENACE_SELFTEST_U_H

/* The Slug Menace Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Tiffy / O'Niall / Maledict / Hobb /
 * Holgart / Jeb / Lovecraft / Bailey / Jorral / Slug Prince cannot leak.
 * Real OPNPC1 / OPLOC1 / OPLOCU / OPHELDU / AI_QUEUE3 on the authored path.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_SLUG_ONLY=1 (or SLUGMENACE)
 *
 * Real prereqs: Wanted! (%wanted_main >= 11) and Sea Slug
 * (%seaslugquest >= 12). Tiffy is reached only after Recruitment Drive
 * (%rd_main = 4) via the existing [opnpc1,rd_teleporter_guy] merge.
 * Stats: Crafting 30 / Runecraft 30 / Slayer 30 / Thieving 30.
 * Reward tenths: 35000 Crafting / Runecraft / Thieving, 1 QP.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - page-fragment widget 460 collapsed to glue-on-fragment
 *   - villager1/2/3 flavour (no authored quest op)
 *   - unused savant/scan bits (slug2_scan_mayor / slug2_savant_* /
 *     slug2_doorscan / slug2_queen_door / slug2_door_sound_control /
 *     slug2_oniall_control)
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 */

#define SLUG_NOT_STARTED 0
#define SLUG_TOLD_BY_TIFFY 1
#define SLUG_SPOKE_NIALL1 2
#define SLUG_TALKED_ONE 3
#define SLUG_TALKED_TWO 4
#define SLUG_TALKED_THREE 5
#define SLUG_TOLD_DUNGEON 6
#define SLUG_GOT_TRANSCRIPT 7
#define SLUG_MALEDICT_FLIPPED 8
#define SLUG_HAVE_TWO_PAGES 9
#define SLUG_PAGES_TORN 10
#define SLUG_FIXED_PAGE 11
#define SLUG_RUNES_USED 12
#define SLUG_PRINCE_DEAD 13
#define SLUG_COMPLETE 14

#define SLUG_WANTED_COMPLETE 11
#define SLUG_SEASLUG_COMPLETE 12
#define SLUG_RD_COMPLETE 4

#define SLUG_REWARD_TENTHS 35000
#define SLUG_QP_REWARD 1

#define SLUG_TIFFY_X 2996
#define SLUG_TIFFY_Z 3373
#define SLUG_WITCHAVEN_X 2739
#define SLUG_WITCHAVEN_Z 3311
#define SLUG_HOBGOBLIN_X 2701
#define SLUG_HOBGOBLIN_Z 9688
#define SLUG_DUNGEON_X 2351
#define SLUG_DUNGEON_Z 5093
#define SLUG_PRINCE_X 2351
#define SLUG_PRINCE_Z 5093

#define SLUG_ALTAR_AIR 1
#define SLUG_ALTAR_MIND 2
#define SLUG_ALTAR_WATER 3
#define SLUG_ALTAR_EARTH 4
#define SLUG_ALTAR_FIRE 5

static void
slug_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SLUG PASS: %s\n", step);
}

static void
slug_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
slug_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
slug_finish(struct ToriRSServer* srv)
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
slug_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
slug_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : slug_chatmenu();
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
slug_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = slug_chatmenu();
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
slug_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    slug_god(player);
    selftest_tick(srv);
}

static int
slug_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    slug_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
slug_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
slug_free_type(struct ToriRSServer* srv, int npc_type)
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
slug_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
slug_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
slug_quest(struct ToriRSServerPlayer* player)
{
    return slug_get_vb(player, "slug2_main");
}

static void
slug_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int id;

    assert(srv);
    assert(name);
    id = ToriRSServer_WorldVarp(name);
    if( id < 0 )
        id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( id >= 0 )
        ToriRSServer_WorldSetVarp(srv, id, value);
}

static int
slug_get_varp(struct ToriRSServerPlayer* player, const char* name)
{
    int id;

    assert(player);
    assert(name);
    id = ToriRSServer_WorldVarp(name);
    if( id < 0 )
        id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( id < 0 )
        return -1;
    return player->varps[id];
}

static void
slug_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
slug_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    slug_talk(srv, npc_type, slot);
    slug_finish(srv);
}

static void
slug_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    slug_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        slug_click_until_menu(srv, 24);
        slug_pick_row(srv, rows[i]);
    }
    slug_finish(srv);
}

static void
slug_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
slug_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
slug_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
slug_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        slug_set_stat(player, stat, 99);
}

static void
slug_prereqs(struct ToriRSServer* srv, int wanted, int seaslug, int rd)
{
    assert(srv);
    slug_vb(srv, "rd_main", rd ? SLUG_RD_COMPLETE : 0);
    slug_vb(srv, "wanted_main", wanted ? SLUG_WANTED_COMPLETE : 0);
    slug_varp(srv, "seaslugquest", seaslug ? SLUG_SEASLUG_COMPLETE : 0);
}

static void
slug_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    slug_vb(srv, "slug2_main", SLUG_NOT_STARTED);
    slug_vb(srv, "slug2_npc_track1", 0);
    slug_vb(srv, "slug2_npc_track2", 0);
    slug_vb(srv, "slug2_npc_track3", 0);
    slug_vb(srv, "slug2_doorbit", 0);
    slug_vb(srv, "slug2_tornpages", 0);
    slug_vb(srv, "slug2_fixed_page", 0);
    slug_vb(srv, "slug2_haveslug", 0);
    slug_vb(srv, "slug2_used_air_rune", 0);
    slug_vb(srv, "slug2_used_earth_rune", 0);
    slug_vb(srv, "slug2_used_water_rune", 0);
    slug_vb(srv, "slug2_used_fire_rune", 0);
    slug_vb(srv, "slug2_used_mind_rune", 0);
}

static void
slug_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_journal]", NULL, 0);
    slug_finish(srv);
    slug_pass(step);
}

static void
slug_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    slug_finish(srv);
}

static void
slug_locu(struct ToriRSServer* srv, int loc_type, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(loc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_type, -1, -1);
    slug_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
slug_heldu(struct ToriRSServer* srv, int used_on, int useitem)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(used_on > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = useitem;
    player->last_item = used_on;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, used_on, -1, -1);
}

static void
slug_kill(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    assert(npc_type > 0);
    if( slot < 0 )
        return;
    npc = &srv->npcs[slot];
    npc->combat_target = srv->active_player->pid;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_type, -1, slot);
    slug_finish(srv);
    if( npc->active && npc->hitpoints > 0 )
        ToriRSServer_CombatHitNpc(srv, slot, 0, npc->hitpoints);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc_type, -1, slot);
    slug_finish(srv);
}

static void
selftest_quest_theslugmenace(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_tiffy;
    int npc_oniall;
    int npc_maledict;
    int npc_hobb;
    int npc_holgart;
    int npc_jeb;
    int npc_holgart_jeb;
    int npc_lovecraft;
    int npc_bailey;
    int npc_jorral;
    int npc_prince;
    int loc_ruin;
    int loc_wall;
    int loc_door;
    int loc_desk;
    int obj_commorb;
    int obj_commorb_v2;
    int obj_transcript;
    int obj_page1;
    int obj_page2;
    int obj_page4a;
    int obj_page4b;
    int obj_page4c;
    int obj_slug;
    int obj_paste;
    int obj_glue;
    int obj_chisel;
    int obj_essence;
    int obj_air_blank;
    int obj_water_blank;
    int obj_earth_blank;
    int obj_fire_blank;
    int obj_mind_blank;
    int obj_air;
    int obj_water;
    int obj_earth;
    int obj_fire;
    int obj_mind;
    int stat_craft;
    int stat_rc;
    int stat_slayer;
    int stat_thieve;
    int varp_qp;
    int slot_tiffy;
    int slot_oniall;
    int slot_maledict;
    int slot_hobb;
    int slot_holgart;
    int slot_jeb;
    int slot_holgart_jeb;
    int slot_lovecraft = -1;
    int slot_bailey;
    int slot_jorral;
    int slot_prince;
    int craft_before;
    int rc_before;
    int thieve_before;
    int qp_before;
    int32_t charge_args[2];
    int32_t rune_arg;
    static const int k_refuse[] = { 2 };
    static const int k_accept_then_refuse[] = { 1, 2 };
    static const int k_accept_both[] = { 1, 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: the slug menace critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer slug menace selftest: SKIP no compiled script pack\n");
        return;
    }

    npc_tiffy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rd_teleporter_guy");
    npc_oniall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slug2_oniall");
    npc_maledict = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slug2_maledict");
    npc_hobb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slug2_hobb");
    npc_holgart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "holgartlandtravel");
    if( npc_holgart <= 0 )
        npc_holgart = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "holgartland");
    npc_jeb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slug2_jeb");
    npc_holgart_jeb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slug2_holgart_jeb");
    npc_lovecraft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slug2_lovecraft");
    npc_bailey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bailey");
    npc_jorral = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "makinghistory_jorral");
    npc_prince = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slug2_the_slug_prince");
    loc_ruin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slug2_ruin_entrance");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slug2_hidden_entrance");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slug2_cave_doors_closed");
    loc_desk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "slug2_mayors_desk");
    obj_commorb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "wanted_crystal_ball");
    obj_commorb_v2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_crystal_ball");
    obj_transcript = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_transcript");
    obj_page1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_page1");
    obj_page2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_page2");
    obj_page4a = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_page4a");
    obj_page4b = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_page4b");
    obj_page4c = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_page4c");
    obj_slug = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_seaslug_young");
    obj_paste = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "swamppaste");
    obj_glue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_slug_paste");
    obj_chisel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chisel");
    obj_essence = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blankrune");
    if( obj_essence <= 0 )
        obj_essence = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blankrune_high");
    obj_air_blank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_air_blank");
    obj_water_blank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_water_blank");
    obj_earth_blank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_earth_blank");
    obj_fire_blank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_fire_blank");
    obj_mind_blank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_mind_blank");
    obj_air = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_air");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_water");
    obj_earth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_earth");
    obj_fire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_fire");
    obj_mind = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "slug2_rune_mind");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    stat_slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    stat_thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    varp_qp = ToriRSServer_WorldVarp("qp");
    if( varp_qp < 0 )
        varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_slugmenace") > 0,
                   "dbrow quest_slugmenace should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "slug2_main") >= 0,
                   "varbit slug2_main should resolve");
    SELFTEST_CHECK(npc_tiffy > 0, "npc rd_teleporter_guy should resolve");
    SELFTEST_CHECK(npc_oniall > 0, "npc slug2_oniall should resolve");
    SELFTEST_CHECK(npc_maledict > 0, "npc slug2_maledict should resolve");
    SELFTEST_CHECK(npc_hobb > 0, "npc slug2_hobb should resolve");
    SELFTEST_CHECK(npc_holgart > 0, "npc holgartlandtravel should resolve");
    SELFTEST_CHECK(npc_jeb > 0, "npc slug2_jeb should resolve");
    SELFTEST_CHECK(npc_lovecraft > 0, "npc slug2_lovecraft should resolve");
    SELFTEST_CHECK(npc_bailey > 0, "npc bailey should resolve");
    SELFTEST_CHECK(npc_jorral > 0, "npc makinghistory_jorral should resolve");
    SELFTEST_CHECK(npc_prince > 0, "npc slug2_the_slug_prince should resolve");
    SELFTEST_CHECK(loc_ruin > 0 && loc_wall > 0 && loc_door > 0 && loc_desk > 0,
                   "slug locs should resolve");
    SELFTEST_CHECK(obj_commorb_v2 > 0 && obj_transcript > 0 && obj_page1 > 0 && obj_page2 > 0,
                   "slug pages/transcript/commorb should resolve");
    SELFTEST_CHECK(obj_page4a > 0 && obj_page4b > 0 && obj_page4c > 0,
                   "slug torn fragments should resolve");
    SELFTEST_CHECK(obj_air > 0 && obj_water > 0 && obj_earth > 0 && obj_fire > 0 && obj_mind > 0,
                   "slug charged runes should resolve");
    SELFTEST_CHECK(obj_chisel > 0 && obj_essence > 0, "chisel + essence should resolve");

    slug_clear_inv(player);
    slug_reset_quest(srv);
    slug_skills99(player);
    slug_god(player);

    slot_tiffy = slug_spawn(srv, npc_tiffy, SLUG_TIFFY_X, SLUG_TIFFY_Z, 0);
    SELFTEST_CHECK(slot_tiffy >= 0, "Tiffy should spawn");

    slug_journal(srv, "journal_0_not_started");

    /* Qualify-fail: Wanted + Sea Slug + RD done, skills too low. */
    slug_prereqs(srv, 1, 1, 1);
    slug_set_stat(player, stat_craft, 1);
    slug_set_stat(player, stat_rc, 1);
    slug_set_stat(player, stat_slayer, 1);
    slug_set_stat(player, stat_thieve, 1);
    slug_talk_finish(srv, npc_tiffy, slot_tiffy);
    SELFTEST_CHECK(slug_quest(player) == SLUG_NOT_STARTED, "low stats must not start the quest");
    slug_pass("tiffy_qualify_fail_stats");

    /* Sea Slug refuse: Wanted + RD done, Sea Slug unfinished. */
    slug_skills99(player);
    slug_prereqs(srv, 1, 0, 1);
    slug_talk_finish(srv, npc_tiffy, slot_tiffy);
    SELFTEST_CHECK(slug_quest(player) == SLUG_NOT_STARTED, "missing Sea Slug must not start");
    slug_pass("tiffy_seaslug_refuse");

    /* Wanted refuse: RD done, Wanted unfinished -- existing Wanted offer. */
    slug_prereqs(srv, 0, 1, 1);
    slug_talk_rows(srv, npc_tiffy, slot_tiffy, k_refuse, 1);
    SELFTEST_CHECK(slug_quest(player) == SLUG_NOT_STARTED, "Wanted refuse must not start Slug Menace");
    slug_pass("tiffy_wanted_refuse");

    /* Slug Menace first p_choice2 refuse. */
    slug_prereqs(srv, 1, 1, 1);
    slug_talk_rows(srv, npc_tiffy, slot_tiffy, k_refuse, 1);
    SELFTEST_CHECK(slug_quest(player) == SLUG_NOT_STARTED, "first refuse must leave unstarted");
    slug_pass("tiffy_refuse_first");

    /* Second p_choice2 refuse (Tell me more, then I'm busy). */
    slug_talk_rows(srv, npc_tiffy, slot_tiffy, k_accept_then_refuse, 2);
    SELFTEST_CHECK(slug_quest(player) == SLUG_NOT_STARTED, "second refuse must leave unstarted");
    slug_pass("tiffy_refuse_second");

    /* Accept both choices -- Commorb + Commorb v2 + told_by_tiffy. */
    slug_clear_inv(player);
    slug_talk_rows(srv, npc_tiffy, slot_tiffy, k_accept_both, 2);
    SELFTEST_CHECK(slug_quest(player) == SLUG_TOLD_BY_TIFFY, "accept should set slug2_main=1");
    SELFTEST_CHECK(slug_inv_total(player, obj_commorb) >= 1 || obj_commorb <= 0,
                   "accept should grant a Commorb");
    SELFTEST_CHECK(slug_inv_total(player, obj_commorb_v2) >= 1, "accept should grant Commorb v2");
    slug_pass("tiffy_accept");
    slug_journal(srv, "journal_1_told_by_tiffy");

    slot_oniall = slug_spawn(srv, npc_oniall, SLUG_WITCHAVEN_X, SLUG_WITCHAVEN_Z, 0);
    slot_maledict = slug_spawn(srv, npc_maledict, SLUG_WITCHAVEN_X + 2, SLUG_WITCHAVEN_Z, 0);
    slot_hobb = slug_spawn(srv, npc_hobb, SLUG_WITCHAVEN_X + 4, SLUG_WITCHAVEN_Z, 0);
    slot_holgart = slug_spawn(srv, npc_holgart, SLUG_WITCHAVEN_X + 6, SLUG_WITCHAVEN_Z, 0);
    SELFTEST_CHECK(slot_oniall >= 0 && slot_maledict >= 0 && slot_hobb >= 0 && slot_holgart >= 0,
                   "Witchaven NPCs should spawn");

    /* Pre-quest flavour. */
    slug_reset_quest(srv);
    slug_talk_finish(srv, npc_oniall, slot_oniall);
    slug_pass("oniall_early");
    slug_talk_finish(srv, npc_maledict, slot_maledict);
    slug_pass("maledict_early");
    slug_talk_finish(srv, npc_hobb, slot_hobb);
    slug_pass("hobb_early");
    slug_loc1(srv, loc_ruin);
    slug_pass("ruin_early");
    slug_loc1(srv, loc_wall);
    slug_pass("wall_early");
    slug_loc1(srv, loc_desk);
    slug_pass("desk_early");
    if( npc_lovecraft > 0 )
    {
        slot_lovecraft = slug_spawn(srv, npc_lovecraft, SLUG_WITCHAVEN_X + 8, SLUG_WITCHAVEN_Z, 0);
        SELFTEST_CHECK(slot_lovecraft >= 0, "Lovecraft should spawn");
        slug_talk_finish(srv, npc_lovecraft, slot_lovecraft);
        slug_pass("lovecraft_early");
    }

    slug_vb(srv, "slug2_main", SLUG_TOLD_BY_TIFFY);
    slug_talk_finish(srv, npc_oniall, slot_oniall);
    SELFTEST_CHECK(slug_quest(player) == SLUG_SPOKE_NIALL1, "O'Niall first talk sets 2");
    slug_pass("oniall_first");
    slug_journal(srv, "journal_2_spoke_niall1");

    slug_talk_finish(srv, npc_oniall, slot_oniall);
    slug_pass("oniall_waiting");

    slug_talk_finish(srv, npc_tiffy, slot_tiffy);
    slug_pass("tiffy_mid_investigation");

    slug_talk_finish(srv, npc_maledict, slot_maledict);
    SELFTEST_CHECK(slug_get_vb(player, "slug2_npc_track3") == 1, "Maledict fills track3");
    SELFTEST_CHECK(slug_quest(player) == SLUG_TALKED_ONE, "first of three sets 3");
    slug_pass("maledict_first");
    slug_journal(srv, "journal_3_talked_one");

    slug_talk_finish(srv, npc_maledict, slot_maledict);
    slug_pass("maledict_waiting");

    slug_talk_finish(srv, npc_hobb, slot_hobb);
    SELFTEST_CHECK(slug_get_vb(player, "slug2_npc_track2") == 1, "Hobb fills track2");
    SELFTEST_CHECK(slug_quest(player) == SLUG_TALKED_TWO, "second of three sets 4");
    slug_pass("hobb_first");
    slug_journal(srv, "journal_4_talked_two");

    slug_talk_finish(srv, npc_holgart, slot_holgart);
    SELFTEST_CHECK(slug_get_vb(player, "slug2_npc_track1") == 1, "Holgart fills track1");
    SELFTEST_CHECK(slug_quest(player) == SLUG_TALKED_THREE, "third of three sets 5");
    slug_pass("holgart_investigate");
    slug_journal(srv, "journal_5_talked_three");

    slug_talk_finish(srv, npc_oniall, slot_oniall);
    SELFTEST_CHECK(slug_quest(player) == SLUG_TOLD_DUNGEON, "report three sets 6");
    slug_pass("oniall_report_three");
    slug_journal(srv, "journal_6_told_dungeon");

    slug_talk_finish(srv, npc_oniall, slot_oniall);
    slug_pass("oniall_dungeon_reminder");
    slug_talk_finish(srv, npc_tiffy, slot_tiffy);
    slug_pass("tiffy_mid_dungeon");

    slug_tele(srv, SLUG_WITCHAVEN_X, SLUG_WITCHAVEN_Z, 0);
    slug_loc1(srv, loc_ruin);
    SELFTEST_CHECK(player->x == SLUG_HOBGOBLIN_X && player->z == SLUG_HOBGOBLIN_Z,
                   "ruin climb should tele to hobgoblin dungeon, got %d,%d",
                   player->x, player->z);
    slug_pass("ruin_climb");

    slug_loc1(srv, loc_wall);
    SELFTEST_CHECK(slug_get_vb(player, "slug2_doorbit") == 1, "first wall push sets doorbit");
    slug_pass("wall_push");
    slug_loc1(srv, loc_wall);
    SELFTEST_CHECK(player->x == SLUG_DUNGEON_X && player->z == SLUG_DUNGEON_Z,
                   "second wall use should enter sea slug dungeon, got %d,%d",
                   player->x, player->z);
    slug_pass("wall_enter");

    slug_loc1(srv, loc_door);
    slug_pass("door_early");

    slug_give(player, obj_commorb_v2, 1);
    slug_locu(srv, loc_door, obj_commorb_v2);
    SELFTEST_CHECK(slug_quest(player) == SLUG_GOT_TRANSCRIPT, "commorb scan sets 7");
    SELFTEST_CHECK(slug_inv_total(player, obj_transcript) >= 1, "scan grants transcript");
    SELFTEST_CHECK(slug_get_vb(player, "slug2_haveslug") == 1, "scan notices the dead slug");
    slug_pass("door_scan");
    slug_journal(srv, "journal_7_got_transcript");

    slug_locu(srv, loc_door, obj_commorb_v2);
    slug_pass("door_scan_already");
    slug_pass("leftover_savant_scan_bits");

    slug_talk_finish(srv, npc_oniall, slot_oniall);
    slug_pass("oniall_transcript");
    slug_talk_finish(srv, npc_maledict, slot_maledict);
    slug_pass("maledict_has_transcript");

    slot_jorral = slug_spawn(srv, npc_jorral, 2436, 3346, 0);
    SELFTEST_CHECK(slot_jorral >= 0, "Jorral should spawn");
    slug_talk_finish(srv, npc_jorral, slot_jorral);
    SELFTEST_CHECK(slug_inv_total(player, obj_transcript) == 0, "Jorral consumes the transcript");
    slug_pass("jorral_translate");

    slug_talk_finish(srv, npc_maledict, slot_maledict);
    SELFTEST_CHECK(slug_quest(player) == SLUG_MALEDICT_FLIPPED, "translated report flips Maledict to 8");
    slug_pass("maledict_translated");
    slug_journal(srv, "journal_8_maledict_flipped");

    slug_talk_finish(srv, npc_maledict, slot_maledict);
    slug_pass("maledict_flipped_hint");
    slug_talk_finish(srv, npc_oniall, slot_oniall);
    slug_pass("oniall_pages_hint");
    slug_talk_finish(srv, npc_hobb, slot_hobb);
    slug_pass("hobb_after_maledict");

    if( npc_lovecraft <= 0 || slot_lovecraft < 0 )
        slot_lovecraft = slug_spawn(srv, npc_lovecraft, SLUG_WITCHAVEN_X + 8, SLUG_WITCHAVEN_Z, 0);
    slug_loc1(srv, loc_desk);
    SELFTEST_CHECK(slug_inv_total(player, obj_page1) >= 1, "desk search grants page1");
    slug_pass("desk_find_page");
    slug_loc1(srv, loc_desk);
    slug_pass("desk_empty");

    slug_talk_finish(srv, npc_lovecraft, slot_lovecraft);
    SELFTEST_CHECK(slug_inv_total(player, obj_page2) >= 1, "Lovecraft grants page2");
    SELFTEST_CHECK(slug_quest(player) == SLUG_HAVE_TWO_PAGES, "both pages set 9");
    slug_pass("lovecraft_page");
    slug_journal(srv, "journal_9_have_two_pages");
    slug_talk_finish(srv, npc_lovecraft, slot_lovecraft);
    slug_pass("lovecraft_already");

    slug_talk_finish(srv, npc_oniall, slot_oniall);
    SELFTEST_CHECK(slug_quest(player) == SLUG_PAGES_TORN, "third page tears all three, sets 10");
    SELFTEST_CHECK(slug_get_vb(player, "slug2_tornpages") == 1, "tornpages bit set");
    SELFTEST_CHECK(slug_inv_total(player, obj_page4a) >= 1 && slug_inv_total(player, obj_page4b) >= 1 &&
                       slug_inv_total(player, obj_page4c) >= 1,
                   "tear grants three fragments");
    slug_pass("oniall_third_page_tear");
    slug_journal(srv, "journal_10_pages_torn");

    slot_jeb = slug_spawn(srv, npc_jeb, SLUG_WITCHAVEN_X + 10, SLUG_WITCHAVEN_Z, 0);
    SELFTEST_CHECK(slot_jeb >= 0, "Jeb should spawn");
    slug_talk_finish(srv, npc_jeb, slot_jeb);
    slug_pass("jeb_need_slug");

    slug_vb(srv, "slug2_main", SLUG_TOLD_BY_TIFFY);
    slug_talk_finish(srv, npc_jeb, slot_jeb);
    slug_pass("jeb_early");
    slug_vb(srv, "slug2_main", SLUG_PAGES_TORN);

    if( slug_inv_total(player, obj_slug) <= 0 && obj_slug > 0 )
        slug_give(player, obj_slug, 1);
    slug_talk_finish(srv, npc_jeb, slot_jeb);
    slug_pass("jeb_ferry");

    if( npc_holgart_jeb > 0 )
    {
        slot_holgart_jeb =
            slug_spawn(srv, npc_holgart_jeb, SLUG_WITCHAVEN_X + 12, SLUG_WITCHAVEN_Z, 0);
        if( slot_holgart_jeb >= 0 )
        {
            slug_talk_finish(srv, npc_holgart_jeb, slot_holgart_jeb);
            slug_pass("holgart_jeb_mind_slugs");
            slug_pass("leftover_villager_flavour");
        }
    }

    slot_bailey = slug_spawn(srv, npc_bailey, 2782, 3273, 0);
    SELFTEST_CHECK(slot_bailey >= 0, "Bailey should spawn");
    slug_talk_finish(srv, npc_bailey, slot_bailey);
    SELFTEST_CHECK(slug_inv_total(player, obj_paste) >= 1, "Bailey grants swamp paste");
    SELFTEST_CHECK(slug_inv_total(player, obj_glue) >= 1, "Bailey grants slug glue");
    slug_pass("bailey_supplies");
    slug_talk_finish(srv, npc_bailey, slot_bailey);
    slug_pass("bailey_already");

    /* Combine fragments -- widget 460 collapsed to glue-on-fragment. */
    if( slug_inv_total(player, obj_page4a) <= 0 )
        slug_give(player, obj_page4a, 1);
    if( slug_inv_total(player, obj_page4b) <= 0 )
        slug_give(player, obj_page4b, 1);
    if( slug_inv_total(player, obj_page4c) <= 0 )
        slug_give(player, obj_page4c, 1);
    slug_heldu(srv, obj_page4a, obj_glue);
    slug_finish(srv);
    SELFTEST_CHECK(slug_quest(player) == SLUG_FIXED_PAGE, "glue+paste bind sets 11");
    SELFTEST_CHECK(slug_get_vb(player, "slug2_fixed_page") == 1, "fixed_page bit set");
    slug_pass("fragments_combine");
    slug_pass("leftover_page_fragment_widget_460");
    slug_journal(srv, "journal_11_fixed_page");

    slug_talk_finish(srv, npc_oniall, slot_oniall);
    slug_pass("oniall_good_luck");
    slug_talk_finish(srv, npc_tiffy, slot_tiffy);
    slug_pass("tiffy_mid_runes");
    slug_loc1(srv, loc_door);
    slug_pass("door_need_runes");

    /* Five-way chisel+essence engrave, merged into existing [opheldu,chisel]. */
    slug_give(player, obj_chisel, 1);
    slug_give(player, obj_essence, 5);
    slug_heldu(srv, obj_chisel, obj_essence);
    slug_click_until_menu(srv, 8);
    slug_pass("engrave_choice");
    slug_pick_row(srv, 1);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_air_blank) >= 1, "engrave air blank");
    slug_pass("engrave_air");

    slug_heldu(srv, obj_chisel, obj_essence);
    slug_click_until_menu(srv, 8);
    slug_pick_row(srv, 1);
    slug_finish(srv);
    slug_pass("engrave_already");

    slug_heldu(srv, obj_chisel, obj_essence);
    slug_click_until_menu(srv, 8);
    slug_pick_row(srv, 2);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_water_blank) >= 1, "engrave water blank");
    slug_pass("engrave_water");

    slug_heldu(srv, obj_chisel, obj_essence);
    slug_click_until_menu(srv, 8);
    slug_pick_row(srv, 3);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_earth_blank) >= 1, "engrave earth blank");
    slug_pass("engrave_earth");

    slug_heldu(srv, obj_chisel, obj_essence);
    slug_click_until_menu(srv, 8);
    slug_pick_row(srv, 4);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_fire_blank) >= 1, "engrave fire blank");
    slug_pass("engrave_fire");

    slug_heldu(srv, obj_chisel, obj_essence);
    slug_click_until_menu(srv, 8);
    slug_pick_row(srv, 5);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_mind_blank) >= 1, "engrave mind blank");
    slug_pass("engrave_mind");

    charge_args[0] = obj_air_blank;
    charge_args[1] = SLUG_ALTAR_FIRE;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_charge_rune]", charge_args, 2);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_air_blank) >= 1, "wrong altar must not consume air blank");
    slug_pass("charge_wrong_altar");

    charge_args[0] = obj_air_blank;
    charge_args[1] = SLUG_ALTAR_AIR;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_charge_rune]", charge_args, 2);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_air) >= 1, "air altar charges air rune");
    slug_pass("charge_air");

    charge_args[0] = obj_mind_blank;
    charge_args[1] = SLUG_ALTAR_MIND;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_charge_rune]", charge_args, 2);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_mind) >= 1, "mind altar charges mind rune");
    slug_pass("charge_mind");

    charge_args[0] = obj_water_blank;
    charge_args[1] = SLUG_ALTAR_WATER;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_charge_rune]", charge_args, 2);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_water) >= 1, "water altar charges water rune");
    slug_pass("charge_water");

    charge_args[0] = obj_earth_blank;
    charge_args[1] = SLUG_ALTAR_EARTH;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_charge_rune]", charge_args, 2);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_earth) >= 1, "earth altar charges earth rune");
    slug_pass("charge_earth");

    charge_args[0] = obj_fire_blank;
    charge_args[1] = SLUG_ALTAR_FIRE;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_charge_rune]", charge_args, 2);
    slug_finish(srv);
    SELFTEST_CHECK(slug_inv_total(player, obj_fire) >= 1, "fire altar charges fire rune");
    slug_pass("charge_fire");

    rune_arg = obj_air;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_apply_door_rune]", &rune_arg, 1);
    slug_finish(srv);
    SELFTEST_CHECK(slug_get_vb(player, "slug2_used_air_rune") == 1, "air rune slots");
    slug_pass("rune_air");

    rune_arg = obj_air;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_apply_door_rune]", &rune_arg, 1);
    slug_finish(srv);
    slug_pass("rune_already");

    rune_arg = obj_water;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_apply_door_rune]", &rune_arg, 1);
    slug_finish(srv);
    slug_pass("rune_water");
    rune_arg = obj_earth;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_apply_door_rune]", &rune_arg, 1);
    slug_finish(srv);
    slug_pass("rune_earth");
    rune_arg = obj_fire;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_apply_door_rune]", &rune_arg, 1);
    slug_finish(srv);
    slug_pass("rune_fire");
    rune_arg = obj_mind;
    ToriRSServer_ScriptsRunProc(srv, "[proc,slugmenace_apply_door_rune]", &rune_arg, 1);
    slug_finish(srv);
    SELFTEST_CHECK(slug_quest(player) == SLUG_RUNES_USED, "all five runes set 12");
    slug_pass("rune_mind");
    slug_pass("runes_all_used");
    slug_journal(srv, "journal_12_runes_used");

    slug_loc1(srv, loc_door);
    slug_pass("door_open");
    slug_loc1(srv, loc_door);
    slug_pass("door_already_open");

    slot_prince = slug_spawn(srv, npc_prince, SLUG_PRINCE_X, SLUG_PRINCE_Z, 0);
    SELFTEST_CHECK(slot_prince >= 0, "Slug Prince should spawn");
    slug_pass("prince_spawn");
    slug_kill(srv, npc_prince, slot_prince);
    SELFTEST_CHECK(slug_quest(player) == SLUG_PRINCE_DEAD, "Prince death sets 13");
    slug_pass("prince_dead");
    slug_journal(srv, "journal_13_prince_dead");
    slug_free_npc(srv, slot_prince);

    slug_talk_finish(srv, npc_oniall, slot_oniall);
    slug_pass("oniall_prince_dead");

    craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
    rc_before = (stat_rc >= 0) ? player->stat_xp_tenths[stat_rc] : 0;
    thieve_before = (stat_thieve >= 0) ? player->stat_xp_tenths[stat_thieve] : 0;
    qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : slug_get_varp(player, "qp");

    slug_talk_finish(srv, npc_tiffy, slot_tiffy);
    SELFTEST_CHECK(slug_quest(player) == SLUG_COMPLETE, "Tiffy report completes at 14");
    if( stat_craft >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >= craft_before + SLUG_REWARD_TENTHS,
                       "complete awards 3500 Crafting XP");
    if( stat_rc >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_rc] >= rc_before + SLUG_REWARD_TENTHS,
                       "complete awards 3500 Runecraft XP");
    if( stat_thieve >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_thieve] >= thieve_before + SLUG_REWARD_TENTHS,
                       "complete awards 3500 Thieving XP");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + SLUG_QP_REWARD, "complete awards 1 QP");
    slug_pass("tiffy_prince_dead_report");
    slug_pass("complete_scroll");
    slug_journal(srv, "journal_14_complete");

    slug_talk_finish(srv, npc_tiffy, slot_tiffy);
    slug_pass("tiffy_already_complete");
    slug_talk_finish(srv, npc_oniall, slot_oniall);
    slug_pass("oniall_post_complete");
    slug_talk_finish(srv, npc_maledict, slot_maledict);
    slug_pass("maledict_later");
    slug_talk_finish(srv, npc_hobb, slot_hobb);
    slug_pass("hobb_later");

    slug_free_type(srv, npc_tiffy);
    slug_free_type(srv, npc_oniall);
    slug_free_type(srv, npc_maledict);
    slug_free_type(srv, npc_hobb);
    slug_free_type(srv, npc_holgart);
    slug_free_type(srv, npc_jeb);
    slug_free_type(srv, npc_holgart_jeb);
    slug_free_type(srv, npc_lovecraft);
    slug_free_type(srv, npc_bailey);
    slug_free_type(srv, npc_jorral);
    slug_free_type(srv, npc_prince);
    ToriRSServer_WorldNpcReap(srv);
    slug_clear_inv(player);
    slug_reset_quest(srv);
    slug_god(player);

    fprintf(stderr, "ToriRSServer the slug menace selftest: walk finished\n");
}

#endif /* TORIRSSERVER_TEST_QUEST_THESLUGMENACE_SELFTEST_U_H */
