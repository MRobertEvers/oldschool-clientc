#ifndef TORIRSSERVER_TEST_QUEST_BENEATHCURSEDSANDS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_BENEATHCURSEDSANDS_SELFTEST_U_H

/* Beneath Cursed Sands Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned Jamila / Maisa / Zahur / High Priest /
 * Osman / tomb NPCs cannot leak into the shop stanza. Real OPNPC1 /
 * OPLOC1 / OPHELD1 on the authored path. player->godmode = 1 for the
 * whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_BCS_ONLY=1
 *
 * MERGE -- do not add second headers:
 *   [opnpc1,contact_market_craft] / contact_market_craft_multi  this file
 *   [opnpc1,elid_herbalist]                                    this file
 *   [opnpc1,elid_general_seller]                               this file
 *   [opnpc1,ics_little_hipriest_vis]  shared dispatcher (Contact splices)
 *   [opnpc1,osman]                    areas/alkharid -- BCS uses bcs_osman_vis
 * Do not redeclare Contact / ILH / Gertrude / Prince Ali NPCs.
 *
 * Start NPC is Jamila. Qualify-fail split: Contact! FINISHED
 * (%contact >= 130), Prince Ali Rescue FINISHED (%princequest >= 110),
 * Icthlarin's Little Helper FINISHED (%ics_little_var >= 26),
 * Gertrude's Cat FINISHED (%fluffs >= 6), Agility 62, Crafting 55,
 * Firemaking 55. Offer already has p_choice2 "Yes." / "Not now."
 *
 * Reward tenths: Agility 500000 (50000 XP) plus Keris partisan and
 * Circlet of water.
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF /
 * date_runeday / flute 282 / telekinetic grab / Contact! / Prince Ali
 * Rescue / Icthlarin's Little Helper / Gertrude's Cat / the three skill
 * gates):
 *   leftover_head_guard_fight
 *   leftover_furnace_emblem_pathing
 *   leftover_scarab_mage_fight
 *   leftover_tomb_lever_riddle
 *   leftover_champion_of_scabaras
 *   leftover_chemistry_puzzle_if
 *   leftover_menaphite_akh_fight
 *   leftover_full_refuse_trees
 */

#define BCS_NOT_STARTED 0
#define BCS_MESSAGE 2
#define BCS_MAISA 6
#define BCS_PYRAMID 14
#define BCS_GUARD 18
#define BCS_MAISA2 20
#define BCS_FURNACE 26
#define BCS_EMBLEM 32
#define BCS_MAGES 38
#define BCS_LEVER 44
#define BCS_RIDDLE 46
#define BCS_TOMB 48
#define BCS_KEY 54
#define BCS_CHAMPION 56
#define BCS_PRIEST 62
#define BCS_NARDAH 68
#define BCS_MEAT 72
#define BCS_CHEM 82
#define BCS_CURE 88
#define BCS_SOPH 90
#define BCS_AKH 92
#define BCS_OSMAN 100
#define BCS_FINISH 102
#define BCS_COMPLETE 108

#define BCS_CONTACT_COMPLETE 130
#define BCS_PRINCE_COMPLETE 110
#define BCS_ICS_COMPLETE 26
#define BCS_FLUFFS_COMPLETE 6
#define BCS_REQ_AGILITY 62
#define BCS_REQ_CRAFTING 55
#define BCS_REQ_FIREMAKING 55
#define BCS_AGI_XP 500000

#define BCS_JAMILA_X 3311
#define BCS_JAMILA_Z 2779
#define BCS_JAMILA_LEVEL 0
#define BCS_MAISA_X 3378
#define BCS_MAISA_Z 2792
#define BCS_MAISA_LEVEL 0

static void
bcs_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "BCS PASS: %s\n", step);
}

static void
bcs_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
bcs_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
bcs_drain(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    int t;

    assert(srv);
    player = srv->active_player;
    assert(player);
    for( t = 0; t < 8 && player->active_script; t++ )
    {
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
    ToriRSServer_ScriptsProcessQueues(srv);
}

static void
bcs_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 96 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 1);
        selftest_tick(srv);
    }
    for( t = 0; t < 8; t++ )
        selftest_tick(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    for( t = 0; t < 4; t++ )
        selftest_tick(srv);
}

static int
bcs_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
bcs_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : bcs_chatmenu();
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
bcs_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = bcs_chatmenu();
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
bcs_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    bcs_god(player);
    selftest_tick(srv);
}

static int
bcs_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    bcs_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
bcs_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
bcs_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static void
bcs_vp(struct ToriRSServerPlayer* player, const char* name, int value)
{
    int id;

    assert(player);
    assert(name);
    id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( id >= 0 )
        player->varps[id] = value;
}

static int
bcs_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
bcs_status(struct ToriRSServerPlayer* player)
{
    return bcs_get_vb(player, "bcs");
}

static void
bcs_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
bcs_skills_ok(struct ToriRSServerPlayer* player)
{
    int stat_agi;
    int stat_craft;
    int stat_fm;

    assert(player);
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    bcs_set_stat(player, stat_agi, 99);
    bcs_set_stat(player, stat_craft, 99);
    bcs_set_stat(player, stat_fm, 99);
}

static void
bcs_skills_low(struct ToriRSServerPlayer* player)
{
    int stat_agi;
    int stat_craft;
    int stat_fm;

    assert(player);
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    bcs_set_stat(player, stat_agi, 1);
    bcs_set_stat(player, stat_craft, 1);
    bcs_set_stat(player, stat_fm, 1);
}

static void
bcs_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    assert(srv->active_player);
    bcs_vb(srv, "bcs", BCS_NOT_STARTED);
    bcs_vb(srv, "bcs_investigated_entry", 0);
    bcs_vb(srv, "bcs_investigated_citizens", 0);
    bcs_vb(srv, "bcs_found_mould", 0);
    bcs_vb(srv, "bcs_emblem_rotation", 0);
    bcs_vb(srv, "bcs_found_tomb", 0);
    bcs_vb(srv, "bcs_mehhar_returned", 0);
    bcs_vb(srv, "contact", 0);
    bcs_vb(srv, "ics_little_var", 0);
    bcs_vp(srv->active_player, "princequest", 0);
    bcs_vp(srv->active_player, "fluffs", 0);
}

static void
bcs_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    assert(srv->active_player);
    bcs_vb(srv, "contact", BCS_CONTACT_COMPLETE);
    bcs_vb(srv, "ics_little_var", BCS_ICS_COMPLETE);
    bcs_vp(srv->active_player, "princequest", BCS_PRINCE_COMPLETE);
    bcs_vp(srv->active_player, "fluffs", BCS_FLUFFS_COMPLETE);
}

static void
bcs_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;
    int rc;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    bcs_drain(srv);
    assert(slot >= 0);
    assert(srv->npcs[slot].active);
    player->last_slot = slot;
    rc = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    SELFTEST_CHECK(rc == TORIRSSERVER_TRIGGER_RAN,
                   "OPNPC1 type %d slot %d should run (rc=%d active=%d)",
                   npc_type, slot, rc, srv->npcs[slot].active);
}

static void
bcs_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    bcs_talk(srv, npc_type, slot);
    bcs_finish(srv);
}

static void
bcs_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    bcs_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        bcs_click_until_menu(srv, 24);
        bcs_pick_row(srv, rows[i]);
    }
    bcs_finish(srv);
}

static void
bcs_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
bcs_has_obj(struct ToriRSServerPlayer* player, int obj_id)
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

static int
bcs_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    bcs_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
bcs_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    assert(srv->active_player);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    bcs_finish(srv);
}

static void
bcs_opheld(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    bcs_finish(srv);
}

static void
bcs_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,beneathcursedsands_journal]", NULL, 0);
    bcs_finish(srv);
    bcs_pass(step);
}

static void
bcs_leftover(struct ToriRSServer* srv, const char* proc, const char* step)
{
    assert(srv);
    assert(proc);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, proc, NULL, 0);
    bcs_finish(srv);
    bcs_pass(step);
}

static void
selftest_quest_beneathcursedsands(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_jamila;
    int npc_maisa;
    int npc_citizen;
    int npc_headguard;
    int npc_scarab;
    int npc_champion;
    int npc_scabaras;
    int npc_zahur;
    int npc_store;
    int npc_priest;
    int npc_akh;
    int npc_osman;
    int loc_furnace;
    int loc_emblem;
    int loc_lever;
    int loc_riddle;
    int loc_tomb;
    int loc_key;
    int loc_chem;
    int loc_toa;
    int obj_message;
    int obj_meat;
    int obj_cure;
    int obj_partisan;
    int obj_circlet;
    int stat_agi;
    int stat_craft;
    int stat_fm;
    int varp_qp;
    int slot_jamila;
    int slot_maisa;
    int slot_citizen;
    int slot_headguard;
    int slot_scarab;
    int slot_champion;
    int slot_scabaras;
    int slot_zahur;
    int slot_store;
    int slot_priest;
    int slot_akh;
    int slot_osman;
    int loc_slot;
    int agi_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: beneath cursed sands critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer BCS selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    bcs_god(player);
    bcs_reset_quest(srv);
    bcs_clear_inv(player);
    bcs_skills_low(player);

    npc_jamila = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "contact_market_craft");
    npc_maisa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bcs_maisa_vis");
    npc_citizen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bcs_necropolis_citizen_2");
    npc_headguard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bcs_necropolis_headguard_combat");
    npc_scarab = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bcs_scarab_mage");
    npc_champion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bcs_champion_combat");
    npc_scabaras = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bcs_scabaras_high_priest");
    npc_zahur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_herbalist");
    npc_store = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elid_general_seller");
    npc_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ics_little_hipriest_vis");
    npc_akh = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bcs_menaphite_akh_combat");
    npc_osman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "bcs_osman_vis");
    loc_furnace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bcs_furnace");
    loc_emblem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bcs_emblem_plaque");
    loc_lever = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bcs_tomb_lever_on");
    loc_riddle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bcs_riddle_plaque");
    loc_tomb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bcs_tomb_entrance");
    loc_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bcs_key_urn");
    loc_chem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "bcs_chemisty_table");
    loc_toa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "toa_entrance");
    obj_message = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bcs_maisa_message");
    obj_meat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cooked_meat");
    obj_cure = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bcs_cure_crate");
    obj_partisan = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "keris_partisan");
    obj_circlet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "water_circlet_charged");
    stat_agi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_beneathcursedsands") > 0,
                   "dbrow quest_beneathcursedsands should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "bcs") >= 0,
                   "varbit bcs should resolve");
    SELFTEST_CHECK(npc_jamila > 0, "npc contact_market_craft should resolve");
    if( npc_jamila <= 0 )
        return;

    bcs_journal(srv, "journal_00_not_started");

    slot_jamila = bcs_spawn(srv, npc_jamila, BCS_JAMILA_X, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
    SELFTEST_CHECK(slot_jamila >= 0, "Jamila should spawn");
    if( slot_jamila < 0 )
        return;

    bcs_skills_ok(player);
    bcs_talk_finish(srv, npc_jamila, slot_jamila);
    SELFTEST_CHECK(bcs_status(player) == BCS_NOT_STARTED,
                   "Contact! qualify-fail must stay not_started");
    bcs_pass("opnpc1_qualify_fail_contact");

    bcs_vb(srv, "contact", BCS_CONTACT_COMPLETE);
    bcs_talk_finish(srv, npc_jamila, slot_jamila);
    SELFTEST_CHECK(bcs_status(player) == BCS_NOT_STARTED,
                   "Prince Ali Rescue qualify-fail must stay not_started");
    bcs_pass("opnpc1_qualify_fail_prince");

    bcs_vp(player, "princequest", BCS_PRINCE_COMPLETE);
    bcs_talk_finish(srv, npc_jamila, slot_jamila);
    SELFTEST_CHECK(bcs_status(player) == BCS_NOT_STARTED,
                   "Icthlarin's Little Helper qualify-fail must stay not_started");
    bcs_pass("opnpc1_qualify_fail_ics");

    bcs_vb(srv, "ics_little_var", BCS_ICS_COMPLETE);
    bcs_talk_finish(srv, npc_jamila, slot_jamila);
    SELFTEST_CHECK(bcs_status(player) == BCS_NOT_STARTED,
                   "Gertrude's Cat qualify-fail must stay not_started");
    bcs_pass("opnpc1_qualify_fail_fluffs");

    bcs_prereqs(srv);
    bcs_skills_low(player);
    bcs_set_stat(player, stat_craft, 99);
    bcs_set_stat(player, stat_fm, 99);
    bcs_talk_finish(srv, npc_jamila, slot_jamila);
    SELFTEST_CHECK(bcs_status(player) == BCS_NOT_STARTED,
                   "Agility qualify-fail must stay not_started");
    bcs_pass("opnpc1_qualify_fail_agility");

    bcs_set_stat(player, stat_agi, 99);
    bcs_set_stat(player, stat_craft, 1);
    bcs_talk_finish(srv, npc_jamila, slot_jamila);
    SELFTEST_CHECK(bcs_status(player) == BCS_NOT_STARTED,
                   "Crafting qualify-fail must stay not_started");
    bcs_pass("opnpc1_qualify_fail_crafting");

    bcs_set_stat(player, stat_craft, 99);
    bcs_set_stat(player, stat_fm, 1);
    bcs_talk_finish(srv, npc_jamila, slot_jamila);
    SELFTEST_CHECK(bcs_status(player) == BCS_NOT_STARTED,
                   "Firemaking qualify-fail must stay not_started");
    bcs_pass("opnpc1_qualify_fail_firemaking");

    bcs_skills_ok(player);
    bcs_talk_rows(srv, npc_jamila, slot_jamila, k_refuse, 1);
    SELFTEST_CHECK(bcs_status(player) == BCS_NOT_STARTED,
                   "Jamila refuse must stay not_started");
    bcs_pass("opnpc1_jamila_refuse");

    bcs_talk_rows(srv, npc_jamila, slot_jamila, k_accept, 1);
    SELFTEST_CHECK(bcs_status(player) == BCS_MESSAGE,
                   "Jamila accept must set message (got %d)", bcs_status(player));
    bcs_pass("opnpc1_jamila_accept");

    bcs_talk_finish(srv, npc_jamila, slot_jamila);
    SELFTEST_CHECK(bcs_status(player) == BCS_MESSAGE,
                   "Jamila read reminder must stay message");
    bcs_pass("opnpc1_jamila_read_reminder");

    if( obj_message > 0 )
    {
        if( !bcs_has_obj(player, obj_message) )
            bcs_give(player, obj_message, 1);
        bcs_opheld(srv, obj_message);
        SELFTEST_CHECK(bcs_status(player) == BCS_MAISA,
                       "reading the message must set maisa (got %d)",
                       bcs_status(player));
        bcs_pass("opheld1_message_read");
    }

    bcs_journal(srv, "journal_06_maisa");

    slot_maisa = -1;
    if( npc_maisa > 0 )
    {
        slot_maisa = bcs_spawn(srv, npc_maisa, BCS_MAISA_X, BCS_MAISA_Z, BCS_MAISA_LEVEL);
        SELFTEST_CHECK(slot_maisa >= 0, "Maisa should spawn");
        if( slot_maisa >= 0 )
        {
            bcs_talk_finish(srv, npc_maisa, slot_maisa);
            SELFTEST_CHECK(bcs_status(player) == BCS_PYRAMID,
                           "Maisa pyramid must set pyramid (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_maisa_pyramid");
        }
    }

    slot_citizen = -1;
    if( npc_citizen > 0 )
    {
        slot_citizen = bcs_spawn(srv, npc_citizen, BCS_MAISA_X + 2, BCS_MAISA_Z, BCS_MAISA_LEVEL);
        if( slot_citizen >= 0 )
        {
            bcs_talk_finish(srv, npc_citizen, slot_citizen);
            SELFTEST_CHECK(bcs_status(player) == BCS_GUARD,
                           "citizen challenge must set guard (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_citizen_challenge");
        }
    }

    slot_headguard = -1;
    if( npc_headguard > 0 )
    {
        slot_headguard = bcs_spawn(srv, npc_headguard, BCS_MAISA_X + 3, BCS_MAISA_Z, BCS_MAISA_LEVEL);
        if( slot_headguard >= 0 )
        {
            bcs_talk_finish(srv, npc_headguard, slot_headguard);
            SELFTEST_CHECK(bcs_status(player) == BCS_MAISA2,
                           "headguard skip must set maisa2 (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_headguard_skip");
        }
    }

    if( slot_maisa >= 0 )
    {
        bcs_talk_finish(srv, npc_maisa, slot_maisa);
        SELFTEST_CHECK(bcs_status(player) == BCS_FURNACE,
                       "Maisa cliffs must set furnace (got %d)",
                       bcs_status(player));
        bcs_pass("opnpc1_maisa_cliffs");
    }

    if( loc_furnace > 0 )
    {
        loc_slot = bcs_place_loc(srv, loc_furnace, BCS_JAMILA_X + 2, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        bcs_oploc(srv, loc_furnace, loc_slot);
        SELFTEST_CHECK(bcs_status(player) == BCS_EMBLEM,
                       "furnace skip must set emblem (got %d)",
                       bcs_status(player));
        bcs_pass("oploc1_furnace_skip");
    }

    if( loc_emblem > 0 )
    {
        loc_slot = bcs_place_loc(srv, loc_emblem, BCS_JAMILA_X + 3, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        bcs_oploc(srv, loc_emblem, loc_slot);
        SELFTEST_CHECK(bcs_status(player) == BCS_MAGES,
                       "emblem skip must set mages (got %d)",
                       bcs_status(player));
        bcs_pass("oploc1_emblem_skip");
    }

    slot_scarab = -1;
    if( npc_scarab > 0 )
    {
        slot_scarab = bcs_spawn(srv, npc_scarab, BCS_JAMILA_X + 4, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        if( slot_scarab >= 0 )
        {
            bcs_talk_finish(srv, npc_scarab, slot_scarab);
            SELFTEST_CHECK(bcs_status(player) == BCS_LEVER,
                           "scarab skip must set lever (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_scarab_skip");
        }
    }

    if( loc_lever > 0 )
    {
        loc_slot = bcs_place_loc(srv, loc_lever, BCS_JAMILA_X + 5, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        bcs_oploc(srv, loc_lever, loc_slot);
        SELFTEST_CHECK(bcs_status(player) == BCS_RIDDLE,
                       "lever pull must set riddle (got %d)",
                       bcs_status(player));
        bcs_pass("oploc1_lever_pull");
    }

    if( loc_riddle > 0 )
    {
        loc_slot = bcs_place_loc(srv, loc_riddle, BCS_JAMILA_X + 6, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        bcs_oploc(srv, loc_riddle, loc_slot);
        SELFTEST_CHECK(bcs_status(player) == BCS_TOMB,
                       "riddle skip must set tomb (got %d)",
                       bcs_status(player));
        bcs_pass("oploc1_riddle_skip");
    }

    if( loc_tomb > 0 )
    {
        loc_slot = bcs_place_loc(srv, loc_tomb, BCS_JAMILA_X + 7, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        bcs_oploc(srv, loc_tomb, loc_slot);
        SELFTEST_CHECK(bcs_status(player) == BCS_KEY,
                       "tomb enter must set key (got %d)",
                       bcs_status(player));
        bcs_pass("oploc1_tomb_enter");
    }

    if( loc_key > 0 )
    {
        loc_slot = bcs_place_loc(srv, loc_key, BCS_JAMILA_X + 8, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        bcs_oploc(srv, loc_key, loc_slot);
        SELFTEST_CHECK(bcs_status(player) == BCS_CHAMPION,
                       "key take must set champion (got %d)",
                       bcs_status(player));
        bcs_pass("oploc1_key_take");
    }

    bcs_journal(srv, "journal_56_champion");

    slot_champion = -1;
    if( npc_champion > 0 )
    {
        slot_champion = bcs_spawn(srv, npc_champion, BCS_JAMILA_X + 9, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        if( slot_champion >= 0 )
        {
            bcs_talk_finish(srv, npc_champion, slot_champion);
            SELFTEST_CHECK(bcs_status(player) == BCS_PRIEST,
                           "champion skip must set priest (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_champion_skip");
        }
    }

    slot_scabaras = -1;
    if( npc_scabaras > 0 )
    {
        slot_scabaras = bcs_spawn(srv, npc_scabaras, BCS_JAMILA_X + 10, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        if( slot_scabaras >= 0 )
        {
            bcs_talk_finish(srv, npc_scabaras, slot_scabaras);
            SELFTEST_CHECK(bcs_status(player) == BCS_NARDAH,
                           "Scabaras priest must set nardah (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_scabaras_nardah");
        }
    }

    slot_zahur = -1;
    if( npc_zahur > 0 )
    {
        slot_zahur = bcs_spawn(srv, npc_zahur, BCS_JAMILA_X + 11, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        if( slot_zahur >= 0 )
        {
            bcs_talk_finish(srv, npc_zahur, slot_zahur);
            SELFTEST_CHECK(bcs_status(player) == BCS_MEAT,
                           "Zahur need-meat must set meat (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_zahur_need_meat");
        }
    }

    slot_store = -1;
    if( npc_store > 0 )
    {
        slot_store = bcs_spawn(srv, npc_store, BCS_JAMILA_X + 12, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        if( slot_store >= 0 )
        {
            bcs_talk_finish(srv, npc_store, slot_store);
            SELFTEST_CHECK(obj_meat <= 0 || bcs_has_obj(player, obj_meat),
                           "Nardah store should supply meat");
            bcs_pass("opnpc1_store_buy_meat");
        }
    }

    if( slot_zahur >= 0 )
    {
        if( obj_meat > 0 && !bcs_has_obj(player, obj_meat) )
            bcs_give(player, obj_meat, 1);
        bcs_talk_finish(srv, npc_zahur, slot_zahur);
        SELFTEST_CHECK(bcs_status(player) == BCS_CHEM,
                       "Zahur begin-chem must set chem (got %d)",
                       bcs_status(player));
        bcs_pass("opnpc1_zahur_begin_chem");
    }

    if( loc_chem > 0 )
    {
        loc_slot = bcs_place_loc(srv, loc_chem, BCS_JAMILA_X + 13, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        bcs_oploc(srv, loc_chem, loc_slot);
        SELFTEST_CHECK(bcs_status(player) == BCS_CURE,
                       "chem table must set cure (got %d)",
                       bcs_status(player));
        bcs_pass("oploc1_chem_table_skip");
    }

    slot_priest = -1;
    if( npc_priest > 0 )
    {
        slot_priest = bcs_spawn(srv, npc_priest, BCS_JAMILA_X + 14, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        if( slot_priest >= 0 )
        {
            if( obj_cure > 0 && !bcs_has_obj(player, obj_cure) )
                bcs_give(player, obj_cure, 1);
            bcs_talk_finish(srv, npc_priest, slot_priest);
            SELFTEST_CHECK(bcs_status(player) == BCS_SOPH,
                           "High Priest cure must set soph (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_priest_cure");
        }
    }

    if( slot_maisa >= 0 )
    {
        bcs_talk_finish(srv, npc_maisa, slot_maisa);
        SELFTEST_CHECK(bcs_status(player) == BCS_AKH,
                       "Maisa Akh brief must set akh (got %d)",
                       bcs_status(player));
        bcs_pass("opnpc1_maisa_akh");
    }

    bcs_journal(srv, "journal_92_akh");

    slot_akh = -1;
    if( npc_akh > 0 )
    {
        slot_akh = bcs_spawn(srv, npc_akh, BCS_JAMILA_X + 15, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        if( slot_akh >= 0 )
        {
            bcs_talk_finish(srv, npc_akh, slot_akh);
            SELFTEST_CHECK(bcs_status(player) == BCS_OSMAN,
                           "Akh skip must set osman (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_akh_skip");
        }
    }

    slot_osman = -1;
    if( npc_osman > 0 )
    {
        slot_osman = bcs_spawn(srv, npc_osman, BCS_JAMILA_X + 16, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        if( slot_osman >= 0 )
        {
            bcs_talk_finish(srv, npc_osman, slot_osman);
            SELFTEST_CHECK(bcs_status(player) == BCS_FINISH,
                           "Osman must set finish (got %d)",
                           bcs_status(player));
            bcs_pass("opnpc1_osman_return");
        }
    }

    agi_before = (stat_agi >= 0) ? player->stat_xp_tenths[stat_agi] : 0;
    qp_before = 0;
    if( varp_qp >= 0 )
        qp_before = player->varps[varp_qp];

    if( slot_maisa >= 0 )
    {
        bcs_talk_finish(srv, npc_maisa, slot_maisa);
        SELFTEST_CHECK(bcs_status(player) == BCS_COMPLETE,
                       "Maisa finish must complete (got %d)",
                       bcs_status(player));
        bcs_pass("opnpc1_maisa_finish");
    }

    if( stat_agi >= 0 )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_agi] >= agi_before + BCS_AGI_XP,
                       "Agility reward tenths 500000 (50000 XP)");
    if( obj_partisan > 0 )
        SELFTEST_CHECK(bcs_has_obj(player, obj_partisan),
                       "complete should grant Keris partisan");
    if( obj_circlet > 0 )
        SELFTEST_CHECK(bcs_has_obj(player, obj_circlet),
                       "complete should grant Circlet of water");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] > qp_before,
                       "quest complete should award QP");
    bcs_pass("complete_rewards");

    bcs_journal(srv, "journal_108_complete");

    if( slot_jamila >= 0 )
    {
        bcs_talk_finish(srv, npc_jamila, slot_jamila);
        bcs_pass("opnpc1_jamila_complete");
    }

    if( loc_toa > 0 )
    {
        loc_slot = bcs_place_loc(srv, loc_toa, BCS_JAMILA_X + 17, BCS_JAMILA_Z, BCS_JAMILA_LEVEL);
        bcs_oploc(srv, loc_toa, loc_slot);
        bcs_pass("oploc1_toa_open");
    }

    bcs_leftover(srv, "[proc,bcs_leftover_head_guard_fight]",
                 "leftover_head_guard_fight");
    bcs_leftover(srv, "[proc,bcs_leftover_furnace_emblem_pathing]",
                 "leftover_furnace_emblem_pathing");
    bcs_leftover(srv, "[proc,bcs_leftover_scarab_mage_fight]",
                 "leftover_scarab_mage_fight");
    bcs_leftover(srv, "[proc,bcs_leftover_tomb_lever_riddle]",
                 "leftover_tomb_lever_riddle");
    bcs_leftover(srv, "[proc,bcs_leftover_champion_of_scabaras]",
                 "leftover_champion_of_scabaras");
    bcs_leftover(srv, "[proc,bcs_leftover_chemistry_puzzle_if]",
                 "leftover_chemistry_puzzle_if");
    bcs_leftover(srv, "[proc,bcs_leftover_menaphite_akh_fight]",
                 "leftover_menaphite_akh_fight");
    bcs_leftover(srv, "[proc,bcs_leftover_full_refuse_trees]",
                 "leftover_full_refuse_trees");

    bcs_free_npc(srv, slot_jamila);
    bcs_free_npc(srv, slot_maisa);
    bcs_free_npc(srv, slot_citizen);
    bcs_free_npc(srv, slot_headguard);
    bcs_free_npc(srv, slot_scarab);
    bcs_free_npc(srv, slot_champion);
    bcs_free_npc(srv, slot_scabaras);
    bcs_free_npc(srv, slot_zahur);
    bcs_free_npc(srv, slot_store);
    bcs_free_npc(srv, slot_priest);
    bcs_free_npc(srv, slot_akh);
    bcs_free_npc(srv, slot_osman);
    bcs_clear_inv(player);
    bcs_reset_quest(srv);
}

#endif
