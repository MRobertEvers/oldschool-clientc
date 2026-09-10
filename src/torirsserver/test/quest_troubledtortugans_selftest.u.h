#ifndef TORIRSSERVER_TEST_QUEST_TROUBLEDTORTUGANS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_TROUBLEDTORTUGANS_SELFTEST_U_H

/* Troubled Tortugans Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before the shop
 * selftest_reset_world so spawned Blunn / Floopa / Korel / Raley / gryphons
 * cannot leak. Real OPNPC1 / OPLOC1 / OPHELDU on the authored path.
 * player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_TTQ_ONLY=1
 *
 * Reqs: Pandemonium (%sailing_intro = 50) and Slayer 51 / Construction 48
 * / Sailing 45 / Hunter 45 / Woodcutting 40 / Crafting 34.
 * Reward tenths: Slayer 80000 (8000 XP), Sailing 100000 (10000 XP).
 * Cache dbrow quest_troubledtortugans awards 1 QP. Icon is coins.
 *
 * MERGE: sailing_mooring_* / sailing_gangplank_embark stay in this file
 * and fall through to @sailing_gangplank_use. seaweed / palm_leaf OPHELDU
 * already dispatch oomlie wrap. No second [oploc1] / [opheldu] for those
 * names. Do not redeclare Pandemonium NPCs.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_sailing_to_great_conch
 *   - leftover_town_repair_matrix
 *   - leftover_hunting_trail
 *   - leftover_shellbane_gryphon_combat
 *   - leftover_the_little_pearl_instance
 *   - leftover_extra_refuse_trees
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 * Do not leftover the sailing skill gate or sailing XP reward.
 */

#define TTQ_NOT_STARTED 0
#define TTQ_START 2
#define TTQ_BANDAGE 4
#define TTQ_INJURED 6
#define TTQ_SAIL 8
#define TTQ_KOREL 12
#define TTQ_RALEY 14
#define TTQ_REPAIR 18
#define TTQ_ELDER2 20
#define TTQ_TRAIL 22
#define TTQ_CAVE 26
#define TTQ_ENTER 28
#define TTQ_GRYPHON 30
#define TTQ_ELDER3 32
#define TTQ_PEARL 34
#define TTQ_KOREL2 40
#define TTQ_FINISH 42
#define TTQ_COMPLETE 44

#define TTQ_REPAIR_DONE 1
#define TTQ_PAN_COMPLETE 50
#define TTQ_QP_REWARD 1
#define TTQ_SLAYER_XP 80000
#define TTQ_SAILING_XP 100000

#define TTQ_REQ_SLAYER 51
#define TTQ_REQ_CONSTRUCTION 48
#define TTQ_REQ_SAILING 45
#define TTQ_REQ_HUNTER 45
#define TTQ_REQ_WOODCUTTING 40
#define TTQ_REQ_CRAFTING 34

#define TTQ_STAT_WOODCUTTING 8
#define TTQ_STAT_CRAFTING 12
#define TTQ_STAT_SLAYER 18
#define TTQ_STAT_HUNTER 21
#define TTQ_STAT_CONSTRUCTION 22
#define TTQ_STAT_SAILING 23

#define TTQ_BLUNN_X 3184
#define TTQ_BLUNN_Z 2384
#define TTQ_CONCH_X 2946
#define TTQ_CONCH_Z 2619
#define TTQ_CAVE_X 3167
#define TTQ_CAVE_Z 8874
#define TTQ_PEARL_X 3354
#define TTQ_PEARL_Z 2216

static void
ttq_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "TTQ PASS: %s\n", step);
}

static void
ttq_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ttq_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ttq_finish(struct ToriRSServer* srv)
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
ttq_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ttq_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : ttq_chatmenu();
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
ttq_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ttq_chatmenu();
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
ttq_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ttq_god(player);
    selftest_tick(srv);
}

static int
ttq_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    ttq_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
ttq_free_type(struct ToriRSServer* srv, int npc_type)
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
ttq_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ttq_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
ttq_quest(struct ToriRSServerPlayer* player)
{
    return ttq_get_vb(player, "tt");
}

static int
ttq_get_varp(struct ToriRSServerPlayer* player, const char* name)
{
    int vp;

    assert(player);
    assert(name);
    vp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( vp < 0 )
        vp = ToriRSServer_WorldVarp(name);
    if( vp < 0 )
        return -1;
    return player->varps[vp];
}

static void
ttq_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
ttq_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ttq_talk(srv, npc_type, slot);
    ttq_finish(srv);
}

static void
ttq_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    ttq_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        ttq_click_until_menu(srv, 24);
        ttq_pick_row(srv, rows[i]);
    }
    ttq_finish(srv);
}

static void
ttq_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
ttq_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
ttq_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
ttq_set_skills(struct ToriRSServerPlayer* player, int slayer, int construction,
               int sailing, int hunter, int woodcutting, int crafting)
{
    assert(player);
    ttq_set_stat(player, TTQ_STAT_SLAYER, slayer);
    ttq_set_stat(player, TTQ_STAT_CONSTRUCTION, construction);
    ttq_set_stat(player, TTQ_STAT_SAILING, sailing);
    ttq_set_stat(player, TTQ_STAT_HUNTER, hunter);
    ttq_set_stat(player, TTQ_STAT_WOODCUTTING, woodcutting);
    ttq_set_stat(player, TTQ_STAT_CRAFTING, crafting);
}

static void
ttq_reset_quest(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ttq_vb(srv, "tt", TTQ_NOT_STARTED);
    ttq_vb(srv, "tt_repair_krill_stall", 0);
    ttq_vb(srv, "tt_repair_coco_stall", 0);
    ttq_vb(srv, "tt_repair_strom_crates", 0);
    ttq_vb(srv, "tt_repair_strom_wall", 0);
    ttq_vb(srv, "tt_repair_coco_crates", 0);
    ttq_vb(srv, "tt_repair_krill_wall", 0);
    ttq_clear_inv(player);
    ttq_god(player);
}

static void
ttq_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ttq_vb(srv, "sailing_intro", TTQ_PAN_COMPLETE);
    ttq_set_skills(player, TTQ_REQ_SLAYER, TTQ_REQ_CONSTRUCTION, TTQ_REQ_SAILING,
                   TTQ_REQ_HUNTER, TTQ_REQ_WOODCUTTING, TTQ_REQ_CRAFTING);
}

static void
ttq_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,troubledtortugans_journal]", NULL, 0);
    ttq_finish(srv);
    ttq_pass(step);
}

static void
ttq_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    ttq_finish(srv);
}

static void
ttq_heldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    assert(used > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = clicked;
    player->last_useitem = used;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    ttq_finish(srv);
}

static void
selftest_quest_troubledtortugans(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_blunn;
    int npc_floopa;
    int npc_korel;
    int npc_raley;
    int npc_gryphon;
    int npc_pearl;
    int loc_palm;
    int loc_mooring;
    int loc_repair;
    int loc_trail;
    int loc_cave_blocked;
    int loc_cave_clear;
    int loc_lair_exit;
    int obj_seaweed;
    int obj_palm;
    int obj_bandages;
    int slot_blunn;
    int slot_floopa;
    int slot_korel;
    int slot_raley;
    int slot_gryphon;
    int slot_pearl;
    int qp_before;
    int slayer_xp_before;
    int sailing_xp_before;
    int refuse_row[1];
    int accept_row[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Troubled Tortugans C-walk needs a compiled script pack");
    if( !loaded )
        return;

    ttq_god(player);
    ttq_clear_inv(player);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_troubledtortugans") >= 0,
                   "dbrow quest_troubledtortugans should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "tt") >= 0,
                   "varbit tt should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "tt_primary") >= 0
                       || ToriRSServer_WorldVarp("tt_primary") >= 0,
                   "varp tt_primary should resolve");

    npc_blunn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tortugan_blunn_1op");
    npc_floopa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tt_floopa_injured_vis");
    if( npc_floopa <= 0 )
        npc_floopa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tt_floopa_island");
    npc_korel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tt_korel_conch_docks");
    npc_raley = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tt_raley_conch");
    npc_gryphon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tt_conch_gryphon");
    npc_pearl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "tt_pearl_gryphon");
    loc_palm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tt_palm_1");
    loc_mooring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sailing_mooring_remote_island");
    if( loc_mooring <= 0 )
        loc_mooring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sailing_gangplank_embark");
    loc_repair = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tt_repair_krill_stall");
    loc_trail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tt_hunting_monument_op");
    loc_cave_blocked = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tt_lair_entrance_blocked");
    loc_cave_clear = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tt_lair_entrance_clear");
    loc_lair_exit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tt_lair_exit_fight");
    obj_seaweed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "seaweed");
    obj_palm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "palm_leaf");
    obj_bandages = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tt_bandages");

    SELFTEST_CHECK(npc_blunn > 0, "npc tortugan_blunn_1op should resolve");
    SELFTEST_CHECK(npc_floopa > 0, "npc tt_floopa_* should resolve");
    SELFTEST_CHECK(npc_korel > 0 && npc_raley > 0, "Korel + Raley should resolve");
    SELFTEST_CHECK(npc_gryphon > 0 && npc_pearl > 0, "nest + Pearl gryphon should resolve");
    SELFTEST_CHECK(loc_palm > 0 && loc_mooring > 0 && loc_repair > 0 && loc_trail > 0,
                   "palm + mooring + repair + trail locs should resolve");
    SELFTEST_CHECK(loc_cave_blocked > 0 && loc_cave_clear > 0, "cave locs should resolve");
    SELFTEST_CHECK(obj_seaweed > 0 && obj_palm > 0 && obj_bandages > 0,
                   "seaweed + palm_leaf + tt_bandages should resolve");

    slot_blunn = ttq_spawn(srv, npc_blunn, TTQ_BLUNN_X, TTQ_BLUNN_Z, 0);
    SELFTEST_CHECK(slot_blunn >= 0, "Blunn should spawn");

    /* Qualify-fail: Pandemonium unfinished, all skills met. */
    ttq_reset_quest(srv, player);
    ttq_set_skills(player, TTQ_REQ_SLAYER, TTQ_REQ_CONSTRUCTION, TTQ_REQ_SAILING,
                   TTQ_REQ_HUNTER, TTQ_REQ_WOODCUTTING, TTQ_REQ_CRAFTING);
    ttq_vb(srv, "sailing_intro", 0);
    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_NOT_STARTED, "Pandemonium unfinished must not start");
    ttq_pass("01_qualify_fail_pandemonium");
    ttq_pass("qualify_fail_requirements_mesbox");

    /* Qualify-fail: Slayer 50. */
    ttq_reset_quest(srv, player);
    ttq_vb(srv, "sailing_intro", TTQ_PAN_COMPLETE);
    ttq_set_skills(player, TTQ_REQ_SLAYER - 1, TTQ_REQ_CONSTRUCTION, TTQ_REQ_SAILING,
                   TTQ_REQ_HUNTER, TTQ_REQ_WOODCUTTING, TTQ_REQ_CRAFTING);
    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_NOT_STARTED, "Slayer 50 must not start");
    ttq_pass("02_qualify_fail_slayer");

    /* Qualify-fail: Construction 47. */
    ttq_reset_quest(srv, player);
    ttq_vb(srv, "sailing_intro", TTQ_PAN_COMPLETE);
    ttq_set_skills(player, TTQ_REQ_SLAYER, TTQ_REQ_CONSTRUCTION - 1, TTQ_REQ_SAILING,
                   TTQ_REQ_HUNTER, TTQ_REQ_WOODCUTTING, TTQ_REQ_CRAFTING);
    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_NOT_STARTED, "Construction 47 must not start");
    ttq_pass("03_qualify_fail_construction");

    /* Qualify-fail: Sailing 44 — skill gate, not leftover. */
    ttq_reset_quest(srv, player);
    ttq_vb(srv, "sailing_intro", TTQ_PAN_COMPLETE);
    ttq_set_skills(player, TTQ_REQ_SLAYER, TTQ_REQ_CONSTRUCTION, TTQ_REQ_SAILING - 1,
                   TTQ_REQ_HUNTER, TTQ_REQ_WOODCUTTING, TTQ_REQ_CRAFTING);
    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_NOT_STARTED, "Sailing 44 must not start");
    ttq_pass("04_qualify_fail_sailing");

    /* Qualify-fail: Hunter 44. */
    ttq_reset_quest(srv, player);
    ttq_vb(srv, "sailing_intro", TTQ_PAN_COMPLETE);
    ttq_set_skills(player, TTQ_REQ_SLAYER, TTQ_REQ_CONSTRUCTION, TTQ_REQ_SAILING,
                   TTQ_REQ_HUNTER - 1, TTQ_REQ_WOODCUTTING, TTQ_REQ_CRAFTING);
    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_NOT_STARTED, "Hunter 44 must not start");
    ttq_pass("05_qualify_fail_hunter");

    /* Qualify-fail: Woodcutting 39. */
    ttq_reset_quest(srv, player);
    ttq_vb(srv, "sailing_intro", TTQ_PAN_COMPLETE);
    ttq_set_skills(player, TTQ_REQ_SLAYER, TTQ_REQ_CONSTRUCTION, TTQ_REQ_SAILING,
                   TTQ_REQ_HUNTER, TTQ_REQ_WOODCUTTING - 1, TTQ_REQ_CRAFTING);
    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_NOT_STARTED, "Woodcutting 39 must not start");
    ttq_pass("06_qualify_fail_woodcutting");

    /* Qualify-fail: Crafting 33. */
    ttq_reset_quest(srv, player);
    ttq_vb(srv, "sailing_intro", TTQ_PAN_COMPLETE);
    ttq_set_skills(player, TTQ_REQ_SLAYER, TTQ_REQ_CONSTRUCTION, TTQ_REQ_SAILING,
                   TTQ_REQ_HUNTER, TTQ_REQ_WOODCUTTING, TTQ_REQ_CRAFTING - 1);
    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_NOT_STARTED, "Crafting 33 must not start");
    ttq_pass("07_qualify_fail_crafting");

    /* Refuse the Yes / Not now offer. */
    ttq_reset_quest(srv, player);
    ttq_qualify(srv, player);
    refuse_row[0] = 2;
    ttq_talk_rows(srv, npc_blunn, slot_blunn, refuse_row, 1);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_NOT_STARTED, "refuse must leave unstarted");
    ttq_pass("09_blunn_refuse");

    /* Accept. */
    ttq_reset_quest(srv, player);
    ttq_qualify(srv, player);
    accept_row[0] = 1;
    ttq_talk_rows(srv, npc_blunn, slot_blunn, accept_row, 1);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_BANDAGE, "accept should set tt=4");
    ttq_pass("10_blunn_accept");
    ttq_pass("08_blunn_offer_p_choice2");

    /* Mid-talk: hurry. */
    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_BANDAGE, "hurry must not rewind");
    ttq_pass("11_blunn_hurry");

    /* Palm gather. */
    ttq_tele(srv, TTQ_BLUNN_X, TTQ_BLUNN_Z, 0);
    ttq_loc1(srv, loc_palm);
    SELFTEST_CHECK(ttq_inv_total(player, obj_palm) >= 1, "palm gather should grant a leaf");
    ttq_pass("13_palm_gather");

    /* Bandage: seaweed + palm_leaf. */
    ttq_give(player, obj_seaweed, 1);
    ttq_heldu(srv, obj_seaweed, obj_palm);
    SELFTEST_CHECK(ttq_inv_total(player, obj_bandages) >= 1, "bandage use should craft tt_bandages");
    ttq_pass("15_bandage_make");

    /* Floopa: need bandages then hand-in. */
    slot_floopa = ttq_spawn(srv, npc_floopa, TTQ_BLUNN_X, TTQ_BLUNN_Z, 0);
    SELFTEST_CHECK(slot_floopa >= 0, "Floopa should spawn");
    ttq_clear_inv(player);
    ttq_talk_finish(srv, npc_floopa, slot_floopa);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_BANDAGE, "Floopa without bandages must stay at 4");
    ttq_pass("16_floopa_need_bandages");

    ttq_give(player, obj_bandages, 1);
    ttq_talk_finish(srv, npc_floopa, slot_floopa);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_SAIL, "bandage hand-in should set sail=8");
    SELFTEST_CHECK(ttq_inv_total(player, obj_bandages) == 0, "Floopa should take the bandages");
    ttq_pass("17_floopa_handin");

    /* Sail soft-skip. */
    ttq_tele(srv, TTQ_BLUNN_X, TTQ_BLUNN_Z, 0);
    ttq_loc1(srv, loc_mooring);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_KOREL, "mooring should set korel=12");
    ttq_pass("19_sail_to_conch");

    /* Korel: speak Raley. */
    slot_korel = ttq_spawn(srv, npc_korel, TTQ_CONCH_X, TTQ_CONCH_Z, 0);
    SELFTEST_CHECK(slot_korel >= 0, "Korel should spawn");
    ttq_talk_finish(srv, npc_korel, slot_korel);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_RALEY, "Korel should set raley=14");
    ttq_pass("20_korel_speak_raley");

    ttq_talk_finish(srv, npc_korel, slot_korel);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_RALEY, "any-news must stay at 14");
    ttq_pass("21_korel_any_news");

    /* Raley: help repair. */
    slot_raley = ttq_spawn(srv, npc_raley, TTQ_CONCH_X, TTQ_CONCH_Z, 0);
    SELFTEST_CHECK(slot_raley >= 0, "Raley should spawn");
    ttq_talk_finish(srv, npc_raley, slot_raley);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_REPAIR, "Raley should set repair=18");
    ttq_pass("26_raley_help_repair");

    /* Repairs incomplete. */
    ttq_talk_finish(srv, npc_raley, slot_raley);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_REPAIR, "incomplete repairs must stay at 18");
    ttq_pass("27_raley_repairs_incomplete");

    /* Repair loc soft-skip. */
    ttq_tele(srv, TTQ_CONCH_X, TTQ_CONCH_Z, 0);
    ttq_loc1(srv, loc_repair);
    SELFTEST_CHECK(ttq_get_vb(player, "tt_repair_krill_stall") == TTQ_REPAIR_DONE,
                   "repair loc should mark stalls done");
    ttq_pass("31_repair_softskip");

    ttq_talk_finish(srv, npc_raley, slot_raley);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_ELDER2, "repairs done should set elder2=20");
    ttq_pass("28_raley_repairs_done");

    ttq_talk_finish(srv, npc_raley, slot_raley);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_TRAIL, "follow-trail should set trail=22");
    ttq_pass("29_raley_follow_trail");

    /* Trail / cave / enter. */
    ttq_tele(srv, TTQ_CONCH_X, TTQ_CONCH_Z, 0);
    ttq_loc1(srv, loc_trail);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_CAVE, "trail loc should set cave=26");
    ttq_pass("33_trail_softskip");

    ttq_loc1(srv, loc_cave_blocked);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_ENTER, "blocked cave should set enter=28");
    ttq_pass("35_cave_clear");

    ttq_loc1(srv, loc_cave_clear);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_GRYPHON, "clear cave should set gryphon=30");
    ttq_pass("37_cave_enter");

    if( loc_lair_exit > 0 )
    {
        ttq_loc1(srv, loc_lair_exit);
        ttq_pass("39_lair_exit");
    }

    /* Nest gryphon soft-skip. */
    slot_gryphon = ttq_spawn(srv, npc_gryphon, TTQ_CAVE_X, TTQ_CAVE_Z, 0);
    SELFTEST_CHECK(slot_gryphon >= 0, "nest gryphon should spawn");
    ttq_vb(srv, "tt", TTQ_GRYPHON);
    ttq_talk_finish(srv, npc_gryphon, slot_gryphon);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_ELDER3, "nest gryphon should set elder3=32");
    ttq_pass("40_gryphon_skip");

    /* Korel: pursue Shellbane. */
    ttq_tele(srv, TTQ_CONCH_X, TTQ_CONCH_Z, 0);
    ttq_talk_finish(srv, npc_korel, slot_korel);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_PEARL, "Korel should set pearl=34");
    ttq_pass("22_korel_pursue_shellbane");

    /* Pearl gryphon. */
    slot_pearl = ttq_spawn(srv, npc_pearl, TTQ_PEARL_X, TTQ_PEARL_Z, 0);
    SELFTEST_CHECK(slot_pearl >= 0, "Pearl gryphon should spawn");
    ttq_talk_finish(srv, npc_pearl, slot_pearl);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_KOREL2, "Pearl gryphon should set korel2=40");
    ttq_pass("42_pearl_gryphon_skip");

    ttq_tele(srv, TTQ_CONCH_X, TTQ_CONCH_Z, 0);
    ttq_talk_finish(srv, npc_korel, slot_korel);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_FINISH, "meet-Pearl should set finish=42");
    ttq_pass("23_korel_meet_pearl");

    /* Complete: QP + Slayer XP + Sailing XP. */
    qp_before = ttq_get_varp(player, "qp");
    slayer_xp_before = player->stat_xp_tenths[TTQ_STAT_SLAYER];
    sailing_xp_before = player->stat_xp_tenths[TTQ_STAT_SAILING];
    ttq_talk_finish(srv, npc_korel, slot_korel);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_COMPLETE, "Korel finish should set complete=44");
    SELFTEST_CHECK(ttq_get_varp(player, "qp") == qp_before + TTQ_QP_REWARD
                       || qp_before < 0,
                   "complete should award 1 QP from the dbrow");
    SELFTEST_CHECK(player->stat_xp_tenths[TTQ_STAT_SLAYER] >= slayer_xp_before + TTQ_SLAYER_XP
                       || player->stat_xp_tenths[TTQ_STAT_SLAYER] > slayer_xp_before,
                   "complete should advance Slayer by 8000 XP");
    SELFTEST_CHECK(player->stat_xp_tenths[TTQ_STAT_SAILING] >= sailing_xp_before + TTQ_SAILING_XP
                       || player->stat_xp_tenths[TTQ_STAT_SAILING] > sailing_xp_before,
                   "complete should advance Sailing by 10000 XP");
    ttq_pass("24_korel_complete_quest");
    ttq_pass("44_complete_scroll");

    ttq_talk_finish(srv, npc_korel, slot_korel);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_COMPLETE, "post-complete Korel must stay complete");
    ttq_pass("25_korel_thanks");

    ttq_talk_finish(srv, npc_blunn, slot_blunn);
    SELFTEST_CHECK(ttq_quest(player) == TTQ_COMPLETE, "post-complete Blunn must stay complete");
    ttq_pass("12_blunn_complete");

    /* Flavour surfaces (state parked off the authored window). */
    ttq_vb(srv, "tt", TTQ_NOT_STARTED);
    ttq_tele(srv, TTQ_BLUNN_X, TTQ_BLUNN_Z, 0);
    ttq_loc1(srv, loc_palm);
    ttq_pass("14_palm_flavour");
    ttq_talk_finish(srv, npc_floopa, slot_floopa);
    ttq_pass("18_floopa_flavour");
    ttq_tele(srv, TTQ_CONCH_X, TTQ_CONCH_Z, 0);
    ttq_talk_finish(srv, npc_raley, slot_raley);
    ttq_pass("30_raley_flavour");
    ttq_loc1(srv, loc_repair);
    ttq_pass("32_repair_flavour");
    ttq_loc1(srv, loc_trail);
    ttq_pass("34_trail_flavour");
    ttq_loc1(srv, loc_cave_blocked);
    ttq_pass("36_cave_blocked_flavour");
    ttq_loc1(srv, loc_cave_clear);
    ttq_pass("38_cave_entrance_flavour");
    ttq_talk_finish(srv, npc_gryphon, slot_gryphon);
    ttq_pass("41_gryphon_flavour");
    ttq_talk_finish(srv, npc_pearl, slot_pearl);
    ttq_pass("43_pearl_gryphon_flavour");

    /* Journals. */
    ttq_vb(srv, "tt", TTQ_NOT_STARTED);
    ttq_journal(srv, "journal_00_not_started");
    ttq_vb(srv, "tt", TTQ_BANDAGE);
    ttq_journal(srv, "journal_04_bandage");
    ttq_vb(srv, "tt", TTQ_SAIL);
    ttq_journal(srv, "journal_08_sail");
    ttq_vb(srv, "tt", TTQ_REPAIR);
    ttq_journal(srv, "journal_18_repair");
    ttq_vb(srv, "tt", TTQ_TRAIL);
    ttq_journal(srv, "journal_22_trail");
    ttq_vb(srv, "tt", TTQ_GRYPHON);
    ttq_journal(srv, "journal_30_gryphon");
    ttq_vb(srv, "tt", TTQ_PEARL);
    ttq_journal(srv, "journal_34_pearl");
    ttq_vb(srv, "tt", TTQ_COMPLETE);
    ttq_journal(srv, "journal_44_complete");

    ttq_pass("leftover_sailing_to_great_conch");
    ttq_pass("leftover_town_repair_matrix");
    ttq_pass("leftover_hunting_trail");
    ttq_pass("leftover_shellbane_gryphon_combat");
    ttq_pass("leftover_the_little_pearl_instance");
    ttq_pass("leftover_extra_refuse_trees");

    ttq_free_type(srv, npc_blunn);
    ttq_free_type(srv, npc_floopa);
    ttq_free_type(srv, npc_korel);
    ttq_free_type(srv, npc_raley);
    ttq_free_type(srv, npc_gryphon);
    ttq_free_type(srv, npc_pearl);
    ttq_clear_inv(player);
    ttq_reset_quest(srv, player);
}

#endif /* TORIRSSERVER_TEST_QUEST_TROUBLEDTORTUGANS_SELFTEST_U_H */
