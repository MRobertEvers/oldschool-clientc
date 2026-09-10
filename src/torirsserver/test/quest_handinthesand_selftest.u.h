#ifndef TORIRSSERVER_TEST_QUEST_HANDINTHESAND_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_HANDINTHESAND_SELFTEST_U_H

/* The Hand in the Sand Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Bert / Guard / Rarve / Sandy / Betty /
 * Mazion / desk / coffee / counter / bell cannot leak. Real OPNPC1 /
 * OPNPC3 / OPNPCU / OPLOC1 / OPLOCU / OPHELD1 / OPHELDU on the authored
 * path. player->godmode = 1 for the whole walk (not a death test).
 *
 * Rarve is reached through the shared Zogre Flesh Eaters bell
 * `[oploc1,zogre_outdoor_bell]` (and once through the existing
 * `[opnpc1,zogre_human_zavistic_rarve]`). Betty is reached through the
 * existing `[opnpc1,betty]`. No second trigger is added.
 *
 * Gate: TORIRSSERVER_SELFTEST_HANDSAND_ONLY=1
 */

#define HS_NOT_STARTED 0
#define HS_HAVE_HAND 10
#define HS_BEER_GIVEN 20
#define HS_HAND_TO_RARVE 30
#define HS_BERT_ROTA 40
#define HS_BOTH_ROTAS 50
#define HS_HAVE_SCROLL 60
#define HS_ORB_GIVEN 70
#define HS_TRUTH_SERUM 80
#define HS_SANDY_DISTRACTED 90
#define HS_SERUM_USED 100
#define HS_ORB_ACTIVATED 110
#define HS_INTERROGATION 120
#define HS_ORB_HANDED_IN 130
#define HS_PIT_ENCHANTED 140
#define HS_WIZARD_HEAD 150
#define HS_COMPLETE 160

#define HS_SERUM_WATER 1
#define HS_SERUM_MADE 5
#define HS_SANDY_NORMAL 0
#define HS_SANDY_LOOKING 1
#define HS_COUNTER_EMPTY 0
#define HS_COUNTER_VIAL 1
#define HS_COUNTER_FOCUSED 2

#define HS_REWARD_CRAFT_TENTHS 90000
#define HS_REWARD_THIEVE_TENTHS 10000
#define HS_REQ_CRAFT 49
#define HS_REQ_THIEVE 17

#define HS_BERT_X 2551
#define HS_BERT_Z 3100
#define HS_GUARD_X 2551
#define HS_GUARD_Z 3078
#define HS_BELL_X 2598
#define HS_BELL_Z 3085
#define HS_RARVE_X 2588
#define HS_RARVE_Z 3087
#define HS_SANDY_X 2788
#define HS_SANDY_Z 3176
#define HS_DESK_X 2789
#define HS_DESK_Z 3176
#define HS_COFFEE_X 2788
#define HS_COFFEE_Z 3175
#define HS_BETTY_X 3014
#define HS_BETTY_Z 3258
#define HS_COUNTER_X 3015
#define HS_COUNTER_Z 3258
#define HS_DOORWAY_X 3016
#define HS_DOORWAY_Z 3259
#define HS_MAZION_X 2818
#define HS_MAZION_Z 3342

static void
hs_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "HS PASS: %s\n", step);
}

static void
hs_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
hs_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
hs_finish(struct ToriRSServer* srv)
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
hs_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
hs_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = hs_chatmenu();
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
hs_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = hs_chatmenu();
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
hs_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    hs_god(player);
    selftest_tick(srv);
}

static int
hs_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    hs_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
hs_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
hs_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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

static int
hs_bank_total(const struct ToriRSServerPlayer* player, int obj_id)
{
    int n = 0;
    int s;

    assert(player);
    if( obj_id <= 0 || !player->bank.slots )
        return 0;
    for( s = 0; s < player->bank.size; s++ )
        if( player->bank.slots[s].obj_id == obj_id )
            n += player->bank.slots[s].count;
    return n;
}

static void
hs_set_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    assert(srv->active_player);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        srv->active_player->varps[varp] = value;
}

static int
hs_get_varp(const struct ToriRSServerPlayer* player, const char* name)
{
    int varp;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp < 0 )
        return 0;
    return player->varps[varp];
}

static void
hs_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( hs_inv_total(player, obj_id) >= count )
        return;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id <= 0 )
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

static void
hs_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
hs_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
hs_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
hs_skills(struct ToriRSServerPlayer* player, int stat_craft, int stat_thieve, int craft, int thieve)
{
    assert(player);
    hs_set_stat(player, stat_craft, craft);
    hs_set_stat(player, stat_thieve, thieve);
}

static void
hs_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
hs_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    hs_talk(srv, npc_type, slot);
    hs_finish(srv);
}

static void
hs_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    hs_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        hs_click_until_menu(srv, 16);
        hs_pick_row(srv, rows[i]);
    }
    hs_finish(srv);
}

static void
hs_opnpc(struct ToriRSServer* srv, int trigger, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, trigger, npc_type, -1, slot);
    hs_finish(srv);
}

static void
hs_opnpcu(struct ToriRSServer* srv, int npc_type, int slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    hs_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static int
hs_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    hs_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

/* Shared bell needs Rarve in range: ~chatnpc_anim aborts the splice before
 * the varbit write if npc_find fails. */
static int
hs_place_bell(struct ToriRSServer* srv, int loc_bell, int npc_rarve, int* rarve_slot)
{
    int bell_slot;

    assert(srv);
    assert(rarve_slot);
    bell_slot = hs_place_loc(srv, loc_bell, HS_BELL_X, HS_BELL_Z, 0);
    *rarve_slot = -1;
    if( npc_rarve > 0 )
        *rarve_slot = ToriRSServer_WorldNpcSpawn(srv, npc_rarve, HS_BELL_X + 1, HS_BELL_Z, 0);
    return bell_slot;
}

static void
hs_oploc(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    hs_finish(srv);
}

static void
hs_oploc_rows(struct ToriRSServer* srv, int loc_id, int loc_slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    for( i = 0; i < n; i++ )
    {
        hs_click_until_menu(srv, 16);
        hs_pick_row(srv, rows[i]);
    }
    hs_finish(srv);
}

static void
hs_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    hs_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
hs_opheld(struct ToriRSServer* srv, int trigger, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, trigger, obj_id, -1, -1);
    hs_finish(srv);
}

static void
hs_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    hs_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
hs_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    hs_set_bit(srv, "handsand_quest", HS_NOT_STARTED);
    hs_set_bit(srv, "handsand_question1", 0);
    hs_set_bit(srv, "handsand_question2", 0);
    hs_set_bit(srv, "handsand_question3", 0);
    hs_set_bit(srv, "handsand_tele", 0);
    hs_set_bit(srv, "handsand_serum", 0);
    hs_set_bit(srv, "handsand_sandy_multi", HS_SANDY_NORMAL);
    hs_set_bit(srv, "handsand_coffee_multi", 0);
    hs_set_bit(srv, "handsand_counter_multi", HS_COUNTER_EMPTY);
    hs_set_varp(srv, "handsand_sand_day", 0);
}

static void
hs_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,handsand_journal]", NULL, 0);
    hs_finish(srv);
    hs_pass(step);
}

static void
selftest_quest_handinthesand(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_bert;
    int npc_guard;
    int npc_rarve;
    int npc_sandy;
    int npc_betty;
    int npc_mazion;
    int loc_bell;
    int loc_desk;
    int loc_coffee;
    int loc_counter;
    int obj_hand;
    int obj_beerhand;
    int obj_beer;
    int obj_rota_bert;
    int obj_rota_sandy;
    int obj_sand;
    int obj_scroll;
    int obj_orb;
    int obj_orb_rec;
    int obj_water;
    int obj_juice;
    int obj_dye;
    int obj_lens;
    int obj_rose;
    int obj_serum;
    int obj_vial;
    int obj_redberries;
    int obj_whiteberries;
    int obj_earth;
    int obj_bucket_sand;
    int obj_head;
    int stat_craft;
    int stat_thieve;
    int slot;
    int rarve_bell;
    int bell_slot;
    int desk_slot;
    int coffee_slot;
    int counter_slot;
    int craft_before;
    int thieve_before;
    static const int k_bert_decline_busy[] = { 2 };
    static const int k_bert_decline_bury[] = { 1, 2 };
    static const int k_bert_qualify[] = { 1, 1 };
    static const int k_bert_decline_accept[] = { 1, 1, 2 };
    static const int k_bert_decline_confirm[] = { 1, 1, 1, 2 };
    static const int k_bert_accept[] = { 1, 1, 1, 1 };
    static const int k_rarve_refuse[] = { 2 };
    static const int k_rarve_yes[] = { 1 };
    static const int k_rarve_tele_yes[] = { 1, 1 };
    static const int k_rarve_tele_no[] = { 1, 2 };
    static const int k_sandy_distract_no[] = { 3 };
    static const int k_sandy_herring[] = { 1 };
    static const int k_sandy_q1[] = { 1 };
    static const int k_sandy_q2[] = { 2 };
    static const int k_sandy_q3[] = { 3 };
    static const int k_sandy_done[] = { 4 };
    static const int k_mazion_refuse[] = { 2 };
    static const int k_mazion_hair[] = { 1, 1, 1 };

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: hand in the sand critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer handsand selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    hs_god(player);
    hs_reset_quest(srv);
    hs_clear_inv(player);
    rarve_bell = -1;

    npc_bert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "handsand_bert");
    npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "handsand_guard_captain");
    npc_rarve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zogre_human_zavistic_rarve");
    npc_sandy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "handsand_sandy");
    npc_betty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "betty");
    npc_mazion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "handsand_naziom");
    loc_bell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "zogre_outdoor_bell");
    loc_desk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "handsand_desk");
    loc_coffee = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "handsand_coffee_multiloc");
    loc_counter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "handsand_counter_multiloc");
    obj_hand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_sandyhand");
    obj_beerhand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_beerhand");
    obj_beer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "beer");
    obj_rota_bert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_rota_bert");
    obj_rota_sandy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_rota_sandy");
    obj_sand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_sand");
    obj_scroll = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_scroll_magic");
    obj_orb = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_orb_storage");
    obj_orb_rec = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_orb_recording");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_bottle_water");
    obj_juice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_redberry_juice");
    obj_dye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_pink_dye");
    obj_lens = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bullseye_lantern_lens");
    obj_rose = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_rose_lens");
    obj_serum = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_truthserum");
    obj_vial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vial_empty");
    obj_redberries = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "redberries");
    obj_whiteberries = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "white_berries");
    obj_earth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "earthrune");
    obj_bucket_sand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_sand");
    obj_head = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "handsand_wizhead");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_thieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "handsand_quest") >= 0,
                   "varbit handsand_quest should resolve");
    SELFTEST_CHECK(npc_bert > 0, "npc handsand_bert should resolve");
    SELFTEST_CHECK(npc_guard > 0, "npc handsand_guard_captain should resolve");
    SELFTEST_CHECK(npc_rarve > 0, "npc zogre_human_zavistic_rarve should resolve");
    SELFTEST_CHECK(npc_sandy > 0, "npc handsand_sandy should resolve");
    SELFTEST_CHECK(npc_betty > 0, "npc betty should resolve");
    SELFTEST_CHECK(npc_mazion > 0, "npc handsand_naziom should resolve");
    SELFTEST_CHECK(loc_bell >= 0, "loc zogre_outdoor_bell should resolve");
    SELFTEST_CHECK(obj_hand > 0, "obj handsand_sandyhand should resolve");
    SELFTEST_CHECK(obj_beer > 0, "obj beer should resolve");

    hs_journal(srv, "journal_00_not_started");

    slot = hs_spawn(srv, npc_bert, HS_BERT_X, HS_BERT_Z, 0);
    if( slot >= 0 )
    {
        hs_skills(player, stat_craft, stat_thieve, 1, 1);
        hs_talk_rows(srv, npc_bert, slot, k_bert_decline_busy, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_NOT_STARTED,
                       "Bert busy decline must stay not_started");
        hs_pass("opnpc1_bert_decline_busy");

        hs_talk_rows(srv, npc_bert, slot, k_bert_decline_bury, 2);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_NOT_STARTED,
                       "Bert bury decline must stay not_started");
        hs_pass("opnpc1_bert_decline_bury");

        hs_skills(player, stat_craft, stat_thieve, HS_REQ_CRAFT - 1, HS_REQ_THIEVE);
        hs_talk_rows(srv, npc_bert, slot, k_bert_qualify, 2);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_NOT_STARTED,
                       "Bert Crafting 48 stat_base must refuse");
        hs_pass("opnpc1_bert_qualify_fail_crafting");

        hs_skills(player, stat_craft, stat_thieve, HS_REQ_CRAFT, HS_REQ_THIEVE - 1);
        hs_talk_rows(srv, npc_bert, slot, k_bert_qualify, 2);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_NOT_STARTED,
                       "Bert Thieving 16 stat_base must refuse");
        hs_pass("opnpc1_bert_qualify_fail_thieving");

        /* Boosted stats must not satisfy the dbrow (stat_base, not boostable). */
        if( stat_craft >= 0 && stat_thieve >= 0 )
        {
            player->stat_level[stat_craft] = 1;
            player->stat_boosted[stat_craft] = 99;
            player->stat_level[stat_thieve] = 1;
            player->stat_boosted[stat_thieve] = 99;
            hs_talk_rows(srv, npc_bert, slot, k_bert_qualify, 2);
            SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_NOT_STARTED,
                           "boosted Crafting/Thieving must not qualify");
            hs_pass("opnpc1_bert_qualify_fail_boosted");
        }

        hs_skills(player, stat_craft, stat_thieve, HS_REQ_CRAFT, HS_REQ_THIEVE);
        hs_talk_rows(srv, npc_bert, slot, k_bert_decline_accept, 3);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_NOT_STARTED,
                       "Bert accept decline must stay not_started");
        hs_pass("opnpc1_bert_decline_accept");

        hs_talk_rows(srv, npc_bert, slot, k_bert_decline_confirm, 4);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_NOT_STARTED,
                       "Bert confirm decline must stay not_started");
        hs_pass("opnpc1_bert_decline_confirm");

        hs_talk_rows(srv, npc_bert, slot, k_bert_accept, 4);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_HAVE_HAND,
                       "Bert accept must write have_hand, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_inv_total(player, obj_hand) == 1, "Bert accept must hand over the hand");
        hs_pass("opnpc1_bert_accept_hand");

        hs_talk_finish(srv, npc_bert, slot);
        hs_pass("opnpc1_bert_have_hand_reminder");
    }
    hs_journal(srv, "journal_10_have_hand");

    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_guard, HS_GUARD_X, HS_GUARD_Z, 0);
    if( slot >= 0 )
    {
        hs_set_bit(srv, "handsand_quest", HS_NOT_STARTED);
        hs_talk_finish(srv, npc_guard, slot);
        hs_pass("opnpc1_guard_too_early");

        hs_set_bit(srv, "handsand_quest", HS_HAVE_HAND);
        hs_clear_inv(player);
        if( obj_hand > 0 )
            hs_give(player, obj_hand, 1);
        hs_talk_finish(srv, npc_guard, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_HAVE_HAND,
                       "Guard without beer must stay have_hand");
        hs_pass("opnpc1_guard_need_beer");

        if( obj_beer > 0 )
        {
            hs_opnpcu(srv, npc_guard, slot, obj_beer);
            SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_HAVE_HAND,
                           "Guard OPNPCU without beer in inv must not advance");
            hs_pass("opnpcu_guard_refuse_no_beer");
        }

        if( obj_beer > 0 )
            hs_give(player, obj_beer, 1);
        hs_talk_finish(srv, npc_guard, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_BEER_GIVEN,
                       "Guard talk-with-beer must write beer_given, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_inv_total(player, obj_beerhand) == 1, "Guard must return the beer-soaked hand");
        hs_pass("opnpc1_guard_talk_beer");

        hs_set_bit(srv, "handsand_quest", HS_HAVE_HAND);
        hs_clear_inv(player);
        if( obj_hand > 0 )
            hs_give(player, obj_hand, 1);
        if( obj_beer > 0 )
            hs_give(player, obj_beer, 1);
        hs_opnpcu(srv, npc_guard, slot, obj_beer);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_BEER_GIVEN,
                       "Guard OPNPCU beer must write beer_given, got %d",
                       hs_get_bit(player, "handsand_quest"));
        hs_pass("opnpcu_guard_beer");

        hs_talk_finish(srv, npc_guard, slot);
        hs_pass("opnpc1_guard_beer_given_reminder");
    }
    hs_journal(srv, "journal_20_beer_given");

    /* Shared bell: not-started falls through to Zogre Flesh Eaters. */
    bell_slot = hs_place_bell(srv, loc_bell, npc_rarve, &rarve_bell);
    hs_set_bit(srv, "handsand_quest", HS_NOT_STARTED);
    if( loc_bell >= 0 && bell_slot >= 0 )
    {
        hs_oploc(srv, loc_bell, bell_slot);
        hs_pass("oploc1_bell_not_started_zogre_fallback");
    }

    hs_set_bit(srv, "handsand_quest", HS_HAVE_HAND);
    if( loc_bell >= 0 && bell_slot >= 0 )
    {
        hs_oploc(srv, loc_bell, bell_slot);
        hs_pass("oploc1_bell_have_hand_rarve");
    }

    hs_set_bit(srv, "handsand_quest", HS_BEER_GIVEN);
    hs_clear_inv(player);
    if( loc_bell >= 0 && bell_slot >= 0 )
    {
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_refuse, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_BEER_GIVEN,
                       "Rarve hand refuse must stay beer_given");
        hs_pass("oploc1_bell_hand_refuse");

        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_BEER_GIVEN,
                       "Rarve without the beer-hand must stay beer_given");
        hs_pass("oploc1_bell_hand_need_hand");

        if( obj_beerhand > 0 )
            hs_give(player, obj_beerhand, 1);
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_HAND_TO_RARVE,
                       "Rarve hand intake must write hand_to_rarve, got %d",
                       hs_get_bit(player, "handsand_quest"));
        hs_pass("oploc1_bell_hand_intake");
    }

    /* Existing Zogre OPNPC1 splice, not a second Handsand trigger. */
    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_rarve, HS_RARVE_X, HS_RARVE_Z, 0);
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_rarve, slot);
        hs_pass("opnpc1_rarve_waiting_hours");
    }
    hs_journal(srv, "journal_30_hand_to_rarve");

    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_bert, HS_BERT_X, HS_BERT_Z, 0);
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_bert, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_BERT_ROTA,
                       "Bert rota exchange must write berts_rota, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_inv_total(player, obj_rota_bert) == 1, "Bert must give his rota");
        hs_pass("opnpc1_bert_rota");

        hs_talk_finish(srv, npc_bert, slot);
        hs_pass("opnpc1_bert_rota_reminder");
    }
    hs_journal(srv, "journal_40_bert_rota");

    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_sandy, HS_SANDY_X, HS_SANDY_Z, 0);
    desk_slot = hs_place_loc(srv, loc_desk, HS_DESK_X, HS_DESK_Z, 0);
    if( slot >= 0 )
    {
        hs_set_bit(srv, "handsand_quest", HS_HAVE_HAND);
        hs_talk_finish(srv, npc_sandy, slot);
        hs_pass("opnpc1_sandy_too_early");

        hs_set_bit(srv, "handsand_quest", HS_BERT_ROTA);
        hs_talk_finish(srv, npc_sandy, slot);
        hs_pass("opnpc1_sandy_busy_accounts");
    }
    if( loc_desk >= 0 && desk_slot >= 0 )
    {
        hs_set_bit(srv, "handsand_quest", HS_HAVE_HAND);
        hs_oploc(srv, loc_desk, desk_slot);
        SELFTEST_CHECK(hs_inv_total(player, obj_rota_sandy) == 0,
                       "desk before Bert's rota must find nothing");
        hs_pass("oploc1_desk_too_early");

        hs_set_bit(srv, "handsand_quest", HS_BERT_ROTA);
        hs_oploc(srv, loc_desk, desk_slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_BOTH_ROTAS,
                       "desk search must write both_rotas, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_inv_total(player, obj_rota_sandy) == 1, "desk must yield Sandy's rota");
        hs_pass("oploc1_desk_sandy_rota");

        hs_oploc(srv, loc_desk, desk_slot);
        hs_pass("oploc1_desk_already");
    }
    hs_journal(srv, "journal_50_both_rotas");

    if( slot >= 0 )
    {
        hs_skills(player, stat_craft, stat_thieve, HS_REQ_CRAFT, HS_REQ_THIEVE - 1);
        hs_opnpc(srv, SS_TRIGGER_OPNPC3, npc_sandy, slot);
        SELFTEST_CHECK(hs_inv_total(player, obj_sand) == 0,
                       "pickpocket below Thieving 17 must fail");
        hs_pass("opnpc3_sandy_pickpocket_need_thieving");

        hs_skills(player, stat_craft, stat_thieve, HS_REQ_CRAFT, HS_REQ_THIEVE);
        hs_opnpc(srv, SS_TRIGGER_OPNPC3, npc_sandy, slot);
        SELFTEST_CHECK(hs_inv_total(player, obj_sand) == 1, "pickpocket must yield handsand_sand");
        hs_pass("opnpc3_sandy_pickpocket");

        hs_talk_finish(srv, npc_sandy, slot);
        hs_pass("opnpc1_sandy_tight_ship");
    }

    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_bert, HS_BERT_X, HS_BERT_Z, 0);
    if( slot >= 0 )
    {
        hs_clear_inv(player);
        if( obj_rota_bert > 0 )
            hs_give(player, obj_rota_bert, 1);
        if( obj_rota_sandy > 0 )
            hs_give(player, obj_rota_sandy, 1);
        hs_talk_finish(srv, npc_bert, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_BOTH_ROTAS,
                       "Bert scroll without sand must stay both_rotas");
        hs_pass("opnpc1_bert_scroll_need_sand");

        if( obj_sand > 0 )
            hs_give(player, obj_sand, 1);
        hs_talk_finish(srv, npc_bert, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_HAVE_SCROLL,
                       "Bert scroll exchange must write have_scroll, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_inv_total(player, obj_scroll) == 1, "Bert must give the magic scroll");
        hs_pass("opnpc1_bert_scroll");
    }
    hs_journal(srv, "journal_60_have_scroll");

    hs_free_npc(srv, rarve_bell);
    bell_slot = hs_place_bell(srv, loc_bell, npc_rarve, &rarve_bell);
    if( loc_bell >= 0 && bell_slot >= 0 )
    {
        hs_clear_inv(player);
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_refuse, 1);
        hs_pass("oploc1_bell_scroll_refuse");

        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_HAVE_SCROLL,
                       "Rarve without the scroll must stay have_scroll");
        hs_pass("oploc1_bell_scroll_need_scroll");

        if( obj_scroll > 0 )
            hs_give(player, obj_scroll, 1);
        if( obj_sand > 0 )
            hs_give(player, obj_sand, 1);
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_tele_no, 2);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_ORB_GIVEN,
                       "Rarve scroll+orb must write orb_given, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_inv_total(player, obj_orb) == 1, "Rarve must give the magical orb");
        SELFTEST_CHECK(hs_get_bit(player, "handsand_tele") == 0, "refusing the tele must leave tele unused");
        hs_pass("oploc1_bell_scroll_orb_tele_refuse");

        hs_set_bit(srv, "handsand_quest", HS_HAVE_SCROLL);
        hs_set_bit(srv, "handsand_tele", 0);
        hs_clear_inv(player);
        if( obj_scroll > 0 )
            hs_give(player, obj_scroll, 1);
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_tele_yes, 2);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_ORB_GIVEN,
                       "Rarve tele accept must still write orb_given");
        SELFTEST_CHECK(hs_get_bit(player, "handsand_tele") == 1, "accepting the tele must set handsand_tele");
        hs_pass("oploc1_bell_scroll_orb_tele");
    }
    hs_journal(srv, "journal_70_orb_given");

    if( loc_bell >= 0 && bell_slot >= 0 )
    {
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_tele") == 1,
                       "orb reminder after tele must not re-arm the one-time tele");
        hs_pass("oploc1_bell_orb_reminder_already_tele");
    }

    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_betty, HS_BETTY_X, HS_BETTY_Z, 0);
    if( slot >= 0 )
    {
        hs_set_bit(srv, "handsand_quest", HS_HAVE_SCROLL);
        hs_talk_finish(srv, npc_betty, slot);
        hs_pass("opnpc1_betty_too_early");

        hs_set_bit(srv, "handsand_quest", HS_ORB_GIVEN);
        hs_set_bit(srv, "handsand_serum", 0);
        hs_clear_inv(player);
        hs_talk_finish(srv, npc_betty, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_serum") == 0,
                       "Betty without vials must not write serum water");
        hs_pass("opnpc1_betty_need_vials");

        if( obj_vial > 0 )
            hs_give(player, obj_vial, 2);
        hs_talk_finish(srv, npc_betty, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_serum") == HS_SERUM_WATER,
                       "Betty must fill bottled water, serum=%d",
                       hs_get_bit(player, "handsand_serum"));
        SELFTEST_CHECK(hs_inv_total(player, obj_water) == 1, "Betty must give bottled water");
        hs_pass("opnpc1_betty_bottled_water");

        hs_talk_finish(srv, npc_betty, slot);
        hs_pass("opnpc1_betty_water_hint");
    }

    if( obj_water > 0 && obj_redberries > 0 )
    {
        hs_clear_inv(player);
        hs_give(player, obj_water, 1);
        hs_give(player, obj_redberries, 1);
        hs_opheldu(srv, obj_water, obj_redberries);
        SELFTEST_CHECK(hs_inv_total(player, obj_juice) == 1,
                       "bottle-water on redberries must make juice");
        hs_pass("opheldu_bottle_water_redberries");
    }
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_betty, slot);
        hs_pass("opnpc1_betty_juice_hint");
    }

    if( obj_juice > 0 && obj_whiteberries > 0 )
    {
        hs_clear_inv(player);
        hs_give(player, obj_juice, 1);
        hs_give(player, obj_whiteberries, 1);
        hs_opheldu(srv, obj_juice, obj_whiteberries);
        SELFTEST_CHECK(hs_inv_total(player, obj_dye) == 1, "juice on white berries must make pink dye");
        hs_pass("opheldu_juice_white_berries");
    }
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_betty, slot);
        hs_pass("opnpc1_betty_dye_hint");
    }

    if( obj_dye > 0 && obj_lens > 0 )
    {
        hs_clear_inv(player);
        hs_give(player, obj_dye, 1);
        hs_give(player, obj_lens, 1);
        hs_opheldu(srv, obj_dye, obj_lens);
        SELFTEST_CHECK(hs_inv_total(player, obj_rose) == 1, "pink dye on lens must make rose lens");
        hs_pass("opheldu_pink_dye_lens");
    }
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_betty, slot);
        hs_pass("opnpc1_betty_lens_hint");
    }

    /* Other click order: existing [opheldu,redberries] / [opheldu,white_berries]. */
    if( obj_water > 0 && obj_redberries > 0 )
    {
        hs_clear_inv(player);
        hs_give(player, obj_water, 1);
        hs_give(player, obj_redberries, 1);
        hs_opheldu(srv, obj_redberries, obj_water);
        SELFTEST_CHECK(hs_inv_total(player, obj_juice) == 1,
                       "redberries on bottled water (shared pies trigger) must make juice");
        hs_pass("opheldu_redberries_bottle_water");
        if( obj_whiteberries > 0 )
        {
            hs_give(player, obj_whiteberries, 1);
            hs_opheldu(srv, obj_whiteberries, obj_juice);
            SELFTEST_CHECK(hs_inv_total(player, obj_dye) == 1,
                           "white berries on juice (shared herblore trigger) must make dye");
            hs_pass("opheldu_white_berries_juice");
        }
        if( obj_lens > 0 )
        {
            hs_give(player, obj_lens, 1);
            hs_opheldu(srv, obj_lens, obj_dye);
            SELFTEST_CHECK(hs_inv_total(player, obj_rose) == 1,
                           "lens on pink dye must make rose lens");
            hs_pass("opheldu_lens_pink_dye");
        }
    }

    counter_slot = hs_place_loc(srv, loc_counter, HS_COUNTER_X, HS_COUNTER_Z, 0);
    if( loc_counter >= 0 && counter_slot >= 0 )
    {
        hs_set_bit(srv, "handsand_counter_multi", HS_COUNTER_EMPTY);
        hs_clear_inv(player);
        if( obj_rose > 0 )
            hs_give(player, obj_rose, 1);
        hs_use_loc(srv, loc_counter, counter_slot, obj_rose);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_counter_multi") == HS_COUNTER_EMPTY,
                       "lens without a vial on the counter must stay empty");
        hs_pass("oplocu_counter_lens_need_vial");

        if( obj_vial > 0 )
            hs_give(player, obj_vial, 1);
        hs_use_loc(srv, loc_counter, counter_slot, obj_vial);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_counter_multi") == HS_COUNTER_VIAL,
                       "empty vial on counter must write vial_placed");
        hs_pass("oplocu_counter_vial");

        /* Far from the doorway: focus must refuse. */
        hs_tele(srv, HS_BERT_X, HS_BERT_Z, 0);
        if( obj_rose > 0 )
            hs_give(player, obj_rose, 1);
        hs_use_loc(srv, loc_counter, counter_slot, obj_rose);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_counter_multi") == HS_COUNTER_VIAL,
                       "lens far from doorway must stay vial_placed");
        hs_pass("oplocu_counter_lens_need_doorway");

        hs_tele(srv, HS_DOORWAY_X, HS_DOORWAY_Z, 0);
        hs_use_loc(srv, loc_counter, counter_slot, obj_rose);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_counter_multi") == HS_COUNTER_FOCUSED,
                       "lens through doorway must write focused");
        hs_pass("oplocu_counter_lens_focus");
    }

    if( slot >= 0 )
    {
        hs_set_bit(srv, "handsand_quest", HS_ORB_GIVEN);
        hs_set_bit(srv, "handsand_counter_multi", HS_COUNTER_FOCUSED);
        hs_clear_inv(player);
        hs_talk_finish(srv, npc_betty, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_ORB_GIVEN,
                       "Betty finish without Sandy sand must stay orb_given");
        hs_pass("opnpc1_betty_need_sand");

        if( obj_sand > 0 )
            hs_give(player, obj_sand, 1);
        hs_talk_finish(srv, npc_betty, slot);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_TRUTH_SERUM,
                       "Betty serum must write truth_serum, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_get_bit(player, "handsand_serum") == HS_SERUM_MADE,
                       "Betty serum must write serum_made");
        SELFTEST_CHECK(hs_inv_total(player, obj_serum) == 1, "Betty must give truth serum");
        hs_pass("opnpc1_betty_serum");

        hs_talk_finish(srv, npc_betty, slot);
        hs_pass("opnpc1_betty_after_serum");
    }
    hs_journal(srv, "journal_80_truth_serum");

    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_sandy, HS_SANDY_X, HS_SANDY_Z, 0);
    coffee_slot = hs_place_loc(srv, loc_coffee, HS_COFFEE_X, HS_COFFEE_Z, 0);
    if( slot >= 0 )
    {
        hs_clear_inv(player);
        hs_talk_finish(srv, npc_sandy, slot);
        hs_pass("opnpc1_sandy_distract_no_serum");

        if( obj_serum > 0 )
            hs_give(player, obj_serum, 1);
        hs_talk_rows(srv, npc_sandy, slot, k_sandy_distract_no, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_TRUTH_SERUM,
                       "Sandy distract refuse must stay truth_serum");
        hs_pass("opnpc1_sandy_distract_refuse");

        hs_talk_rows(srv, npc_sandy, slot, k_sandy_herring, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_SANDY_DISTRACTED,
                       "Sandy herring choice must write distracted, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_get_bit(player, "handsand_sandy_multi") == HS_SANDY_LOOKING,
                       "distract must swap sandy_multi to looking");
        hs_pass("opnpc1_sandy_distract_herring");

        hs_talk_finish(srv, npc_sandy, slot);
        hs_pass("opnpc1_sandy_distracted_reminder");
    }
    hs_journal(srv, "journal_90_sandy_distracted");

    if( loc_coffee >= 0 && coffee_slot >= 0 )
    {
        hs_use_loc(srv, loc_coffee, coffee_slot, obj_serum);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_SERUM_USED,
                       "serum on coffee must write serum_used, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_get_bit(player, "handsand_coffee_multi") == 1,
                       "coffee multi must flip after serum");
        hs_pass("oplocu_coffee_serum");
    }
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_sandy, slot);
        hs_pass("opnpc1_sandy_serum_used");
    }
    hs_journal(srv, "journal_100_serum_used");

    if( obj_orb > 0 )
    {
        hs_clear_inv(player);
        hs_give(player, obj_orb, 1);
        hs_set_bit(srv, "handsand_quest", HS_TRUTH_SERUM);
        hs_opheld(srv, SS_TRIGGER_OPHELD1, obj_orb);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_TRUTH_SERUM,
                       "Activate too early must stay truth_serum");
        hs_pass("opheld1_orb_too_early");

        hs_set_bit(srv, "handsand_quest", HS_SERUM_USED);
        hs_opheld(srv, SS_TRIGGER_OPHELD1, obj_orb);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_ORB_ACTIVATED,
                       "Activate must write orb_activated, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_inv_total(player, obj_orb_rec) == 1, "Activate must swap in the recording orb");
        hs_pass("opheld1_orb_activate");
    }
    hs_journal(srv, "journal_110_orb_activated");

    if( slot >= 0 )
    {
        hs_talk_rows(srv, npc_sandy, slot, k_sandy_q1, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_question1") == 1, "Q1 must set handsand_question1");
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_ORB_ACTIVATED,
                       "one question must not finish interrogation");
        hs_pass("opnpc1_sandy_q1");

        hs_talk_rows(srv, npc_sandy, slot, k_sandy_q2, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_question2") == 1, "Q2 must set handsand_question2");
        hs_pass("opnpc1_sandy_q2");

        hs_talk_rows(srv, npc_sandy, slot, k_sandy_done, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_ORB_ACTIVATED,
                       "That's all with Q3 missing must stay orb_activated");
        hs_pass("opnpc1_sandy_interrogate_partial");

        hs_talk_rows(srv, npc_sandy, slot, k_sandy_q3, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_question3") == 1, "Q3 must set handsand_question3");
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_INTERROGATION,
                       "all three questions must write interrogation_done, got %d",
                       hs_get_bit(player, "handsand_quest"));
        hs_pass("opnpc1_sandy_q3_done");

        hs_talk_finish(srv, npc_sandy, slot);
        hs_pass("opnpc1_sandy_after_interrogation");
    }
    hs_journal(srv, "journal_120_interrogation_done");

    hs_free_npc(srv, rarve_bell);
    bell_slot = hs_place_bell(srv, loc_bell, npc_rarve, &rarve_bell);
    if( loc_bell >= 0 && bell_slot >= 0 )
    {
        hs_clear_inv(player);
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_INTERROGATION,
                       "Rarve without the recording orb must stay interrogation_done");
        hs_pass("oploc1_bell_need_orb");

        if( obj_orb_rec > 0 )
            hs_give(player, obj_orb_rec, 1);
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_ORB_HANDED_IN,
                       "Rarve orb intake must write orb_handed_in, got %d",
                       hs_get_bit(player, "handsand_quest"));
        hs_pass("oploc1_bell_orb_intake");
    }
    hs_journal(srv, "journal_130_orb_handed_in");

    if( loc_bell >= 0 && bell_slot >= 0 )
    {
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_ORB_HANDED_IN,
                       "Rarve without runes/sand must stay orb_handed_in");
        hs_pass("oploc1_bell_need_runes");

        if( obj_earth > 0 )
            hs_give(player, obj_earth, 5);
        if( obj_bucket_sand > 0 )
            hs_give(player, obj_bucket_sand, 1);
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_PIT_ENCHANTED,
                       "earth runes + sand must write pit_enchanted, got %d",
                       hs_get_bit(player, "handsand_quest"));
        hs_pass("oploc1_bell_pit_enchant");

        hs_oploc(srv, loc_bell, bell_slot);
        hs_pass("oploc1_bell_pit_enchanted_reminder");
    }
    hs_journal(srv, "journal_140_pit_enchanted");

    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_mazion, HS_MAZION_X, HS_MAZION_Z, 0);
    if( slot >= 0 )
    {
        hs_set_bit(srv, "handsand_quest", HS_ORB_HANDED_IN);
        hs_talk_finish(srv, npc_mazion, slot);
        hs_pass("opnpc1_mazion_too_early");

        hs_set_bit(srv, "handsand_quest", HS_PIT_ENCHANTED);
        hs_talk_rows(srv, npc_mazion, slot, k_mazion_refuse, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_PIT_ENCHANTED,
                       "Mazion refuse must stay pit_enchanted");
        hs_pass("opnpc1_mazion_refuse");

        hs_talk_rows(srv, npc_mazion, slot, k_mazion_hair, 3);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_WIZARD_HEAD,
                       "Mazion skull handoff must write wizard_head, got %d",
                       hs_get_bit(player, "handsand_quest"));
        SELFTEST_CHECK(hs_inv_total(player, obj_head) == 1, "Mazion must hand over the wizard head");
        hs_pass("opnpc1_mazion_skull_handoff");

        hs_talk_finish(srv, npc_mazion, slot);
        hs_pass("opnpc1_mazion_after");
    }
    hs_journal(srv, "journal_150_wizard_head");

    hs_free_npc(srv, rarve_bell);
    bell_slot = hs_place_bell(srv, loc_bell, npc_rarve, &rarve_bell);
    if( loc_bell >= 0 && bell_slot >= 0 )
    {
        hs_clear_inv(player);
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_WIZARD_HEAD,
                       "Rarve without the head must stay wizard_head");
        hs_pass("oploc1_bell_need_head");

        if( obj_head > 0 )
            hs_give(player, obj_head, 1);
        craft_before = 0;
        thieve_before = 0;
        if( stat_craft >= 0 )
            craft_before = player->stat_xp_tenths[stat_craft];
        if( stat_thieve >= 0 )
            thieve_before = player->stat_xp_tenths[stat_thieve];
        hs_oploc_rows(srv, loc_bell, bell_slot, k_rarve_yes, 1);
        {
            int t;

            for( t = 0; t < 16; t++ )
                selftest_tick(srv);
        }
        SELFTEST_CHECK(hs_get_bit(player, "handsand_quest") == HS_COMPLETE,
                       "Rarve head intake must complete the quest, got %d",
                       hs_get_bit(player, "handsand_quest"));
        if( stat_craft >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >=
                               craft_before + HS_REWARD_CRAFT_TENTHS,
                           "complete must advance Crafting by 90000 tenths");
        if( stat_thieve >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_thieve] >=
                               thieve_before + HS_REWARD_THIEVE_TENTHS,
                           "complete must advance Thieving by 10000 tenths");
        hs_pass("oploc1_bell_head_complete");
    }
    hs_journal(srv, "journal_160_complete");

    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_bert, HS_BERT_X, HS_BERT_Z, 0);
    if( slot >= 0 )
    {
        int sand_before;
        int sand_day;

        hs_set_varp(srv, "handsand_sand_day", 0);
        sand_before = hs_bank_total(player, obj_bucket_sand);
        hs_talk_finish(srv, npc_bert, slot);
        SELFTEST_CHECK(hs_bank_total(player, obj_bucket_sand) ==
                           sand_before + 84,
                       "first-of-day Bert must bank 84 buckets of sand");
        sand_day = hs_get_varp(player, "handsand_sand_day");
        SELFTEST_CHECK(sand_day != 0,
                       "first-of-day must stamp %handsand_sand_day");
        hs_pass("opnpc1_bert_daily_sand_first");

        hs_talk_finish(srv, npc_bert, slot);
        SELFTEST_CHECK(hs_bank_total(player, obj_bucket_sand) ==
                           sand_before + 84,
                       "same-day Bert must not send another 84 buckets");
        SELFTEST_CHECK(hs_get_varp(player, "handsand_sand_day") == sand_day,
                       "same-day refuse must leave handsand_sand_day");
        hs_pass("opnpc1_bert_daily_sand_same_day");
    }
    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_guard, HS_GUARD_X, HS_GUARD_Z, 0);
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_guard, slot);
        hs_pass("opnpc1_guard_post_complete");
    }
    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_sandy, HS_SANDY_X, HS_SANDY_Z, 0);
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_sandy, slot);
        hs_pass("opnpc1_sandy_post_complete");
    }
    hs_free_npc(srv, slot);
    slot = hs_spawn(srv, npc_mazion, HS_MAZION_X, HS_MAZION_Z, 0);
    if( slot >= 0 )
    {
        hs_talk_finish(srv, npc_mazion, slot);
        hs_pass("opnpc1_mazion_post_complete");
    }
    hs_free_npc(srv, slot);

    fprintf(stderr, "ToriRSServer handsand selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_HANDINTHESAND_SELFTEST_U_H */
