#ifndef TORIRSSERVER_TEST_QUEST_MOUNTAINDAUGHTER_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_MOUNTAINDAUGHTER_SELFTEST_U_H

/* Mountain Daughter Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned npcs / locs cannot leak. Real OPNPC /
 * OPLOC / OPHELD / OPLOCU on the authored path. player->godmode = 1 for
 * the whole walk (Kendal is not a death test). Completion goes through
 * ~mdq_quest_complete -> ~quest_complete_rewards. Additive MD branch on
 * Brundt only — do not rewrite Fremennik Trials / Exiles / Viking. */

#define MDQ_NOT_STARTED 0
#define MDQ_STARTED 10
#define MDQ_SPIRIT_HEARD 20
#define MDQ_READY_FOR_KENDAL 30
#define MDQ_KENDAL_FOUND 40
#define MDQ_KENDAL_KILLED 50
#define MDQ_CORPSE_GIVEN 60
#define MDQ_COMPLETE 70

#define MDQ_REL_ASKED 10
#define MDQ_REL_JOKUL 20
#define MDQ_REL_SVIDI 30
#define MDQ_REL_ROCK 40
#define MDQ_REL_GUARANTEE 50
#define MDQ_REL_GIVEN 60

#define MDQ_FOOD_ASKED 10
#define MDQ_FOOD_FINISHED 20

#define MDQ_HAMAL_X 2811
#define MDQ_HAMAL_Z 3673
#define MDQ_JOKUL_X 2811
#define MDQ_JOKUL_Z 3681
#define MDQ_SVIDI_X 2714
#define MDQ_SVIDI_Z 3672
#define MDQ_RAGNAR_X 2765
#define MDQ_RAGNAR_Z 3676
#define MDQ_BRUNDT_X 2659
#define MDQ_BRUNDT_Z 3669
#define MDQ_CAMP_X 2768
#define MDQ_CAMP_Z 3668
#define MDQ_ISLAND3_X 2781
#define MDQ_ISLAND3_Z 3689
#define MDQ_KENDAL_X 2786
#define MDQ_KENDAL_Z 10081
#define MDQ_KENDAL_LEVEL 0
#define MDQ_HANDS 9

static int mdq_fails_mark;

static void
mdq_begin(void)
{
    mdq_fails_mark = g_selftest_failures;
}

static void
mdq_pass(const char* step)
{
    assert(step);
    if( g_selftest_failures == mdq_fails_mark )
        fprintf(stderr, "MOUNTAINDAUGHTER PASS: %s\n", step);
    mdq_fails_mark = g_selftest_failures;
}

static void
mdq_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
mdq_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
mdq_finish(struct ToriRSServer* srv)
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

static void
mdq_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    mdq_god(player);
    selftest_tick(srv);
}

static int
mdq_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    mdq_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
mdq_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
mdq_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
mdq_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( mdq_inv_total(player, obj_id) >= count )
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
mdq_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mdq_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
mdq_reset_bits(struct ToriRSServer* srv)
{
    assert(srv);
    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_NOT_STARTED);
    mdq_set_bit(srv, "mdaughter_relations_var", 0);
    mdq_set_bit(srv, "mdaughter_food_var", 0);
    mdq_set_bit(srv, "mdaughter_mud_var", 0);
    mdq_set_bit(srv, "mdaughter_hamal_relations_done", 0);
    mdq_set_bit(srv, "mdaughter_hamal_heardofdeath", 0);
    mdq_set_bit(srv, "mdaughter_hamal_heardofburial", 0);
    mdq_set_bit(srv, "mdaughter_hamal_heardofbear", 0);
    mdq_set_bit(srv, "mdaughter_brundt_done", 0);
    mdq_set_bit(srv, "mdaughter_bear_discovery", 0);
    mdq_set_bit(srv, "mdaughter_bear_mayattack", 0);
    mdq_set_bit(srv, "mdaughter_bear_multi_state", 0);
    mdq_set_bit(srv, "mdaughter_ragnar_gavenecklace", 0);
    mdq_set_bit(srv, "mdaughter_burial_state", 0);
    mdq_set_bit(srv, "mdaughter_bearman_autotalk", 0);
}

static int
mdq_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
mdq_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mdq_chatmenu();
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
mdq_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mdq_chatmenu();
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

static int
mdq_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    mdq_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
mdq_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
selftest_quest_mountaindaughter(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_hamal;
    int npc_jokul;
    int npc_svidi;
    int npc_ragnar;
    int npc_brundt;
    int npc_kendal;
    int npc_fighter;
    int loc_boulder;
    int loc_rockslide;
    int loc_rock;
    int loc_bush;
    int loc_mud;
    int loc_tree;
    int loc_pole;
    int loc_flat1;
    int loc_flat2;
    int loc_spirit;
    int loc_cave_in;
    int loc_cave_out;
    int loc_cairn;
    int obj_rope;
    int obj_pick;
    int obj_axe;
    int obj_gloves;
    int obj_fruit;
    int obj_seed;
    int obj_mud;
    int obj_pole;
    int obj_plank;
    int obj_half;
    int obj_guarantee;
    int obj_corpse;
    int obj_necklace;
    int obj_rocks;
    int obj_bearhead;
    int stat_agility;
    int stat_attack;
    int stat_prayer;
    int hamal_slot;
    int jokul_slot;
    int svidi_slot;
    int ragnar_slot;
    int brundt_slot;
    int kendal_slot;
    int fighter_slot;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: mountaindaughter Gate D walk\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    npc_hamal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mdaughter_hamal");
    npc_jokul = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mdaughter_jokul");
    npc_svidi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mdaughter_svidi");
    npc_ragnar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mdaughter_ragnar");
    npc_brundt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viking_brundt_child");
    npc_kendal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mdaughter_multi_bear");
    npc_fighter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mdaughter_bearman_fighter");
    loc_boulder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_cliff_boulder");
    loc_rockslide = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_rockslide");
    loc_rock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_ancient_rock");
    loc_bush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_white_pearl_bush");
    loc_mud = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_swampbubbles1");
    loc_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_lake_tree");
    loc_pole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_polerocks");
    loc_flat1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_flatstone1");
    loc_flat2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_flatstone2");
    loc_spirit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_sulphar_gas");
    loc_cave_in = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_caveentrance");
    loc_cave_out = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_caveexit");
    loc_cairn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mdaughter_burialmound");
    obj_rope = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rope");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    obj_axe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_axe");
    obj_gloves = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "leather_gloves");
    obj_fruit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_white_pearl_fruit");
    obj_seed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_white_pearl_seed");
    obj_mud = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_mud");
    obj_pole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_stick");
    obj_plank = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "woodplank");
    obj_half = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_half_rock");
    obj_guarantee = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_safety_guarantee");
    obj_corpse = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_daughter_corpse");
    obj_necklace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_necklace");
    obj_rocks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_rock");
    obj_bearhead = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mdaughter_bear_helmet");
    stat_agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");
    stat_attack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "attack");
    stat_prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");

    SELFTEST_CHECK(npc_hamal > 0 && npc_jokul > 0 && npc_svidi > 0 && npc_ragnar > 0 &&
                       npc_brundt > 0 && npc_kendal > 0 && npc_fighter > 0 &&
                       loc_boulder >= 0 && loc_rockslide >= 0 && loc_rock >= 0 &&
                       loc_bush >= 0 && loc_mud >= 0 && loc_tree >= 0 && loc_pole >= 0 &&
                       loc_flat1 >= 0 && loc_flat2 >= 0 && loc_spirit >= 0 &&
                       loc_cave_in >= 0 && loc_cave_out >= 0 && loc_cairn >= 0 &&
                       obj_rope > 0 && obj_pick > 0 && obj_axe > 0 && obj_gloves > 0 &&
                       obj_fruit > 0 && obj_seed > 0 && obj_mud > 0 && obj_pole > 0 &&
                       obj_plank > 0 && obj_half > 0 && obj_guarantee > 0 &&
                       obj_corpse > 0 && obj_necklace > 0 && obj_rocks > 0 &&
                       obj_bearhead > 0 && stat_agility >= 0 && stat_attack >= 0 &&
                       stat_prayer >= 0,
                   "mountaindaughter C-side names should all resolve");
    if( npc_hamal <= 0 || stat_agility < 0 )
        return;

    player->godmode = 1;
    player->dying = 0;
    mdq_begin();
    mdq_clear_inv(player);
    mdq_reset_bits(srv);
    ToriRSServer_CombatSetLevel(player, stat_agility, 1);
    mdq_god(player);

    hamal_slot = mdq_spawn(srv, npc_hamal, MDQ_HAMAL_X, MDQ_HAMAL_Z, 0);
    SELFTEST_CHECK(hamal_slot >= 0, "Hamal should spawn for the offer");
    if( hamal_slot < 0 )
        return;

    /* ---- Hamal offer: agility gate, refuse, accept ---- */
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_NOT_STARTED,
                   "Hamal offer at Agility 1 must not start the quest, got %d",
                   mdq_get_bit(player, "mdaughter_quest_var"));
    mdq_pass("opnpc1_hamal_agility_gate");

    ToriRSServer_CombatSetLevel(player, stat_agility, 20);
    mdq_god(player);
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_click_until_menu(srv, 24);
    mdq_pick_row(srv, 2);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_NOT_STARTED,
                   "refusing Hamal must not write mdaughter_quest_var, got %d",
                   mdq_get_bit(player, "mdaughter_quest_var"));
    mdq_pass("opnpc1_hamal_refuse");

    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_click_until_menu(srv, 24);
    mdq_pick_row(srv, 1);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_STARTED,
                   "accepting Hamal should start the quest at 10, got %d",
                   mdq_get_bit(player, "mdaughter_quest_var"));
    mdq_pass("opnpc1_hamal_accept");

    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_STARTED,
                   "mid-quest Hamal lake hint must not advance past 10, got %d",
                   mdq_get_bit(player, "mdaughter_quest_var"));
    mdq_pass("opnpc1_hamal_find_lake");

    /* ---- Rope / rockslide ---- */
    {
        int slot = mdq_place_loc(srv, loc_boulder, MDQ_CAMP_X - 8, MDQ_CAMP_Z - 8, 0);

        SELFTEST_CHECK(slot >= 0, "cliff boulder should place");
        if( slot >= 0 )
        {
            player->last_useitem = obj_plank;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_boulder, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_STARTED,
                           "wrong-item rope use must not change quest state");
            mdq_pass("oplocu_boulder_wrong_item");

            mdq_give(player, obj_rope, 1);
            player->last_useitem = obj_rope;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_boulder, -1, slot);
            mdq_finish(srv);
            mdq_pass("oplocu_boulder_rope");

            player->last_useitem = obj_rope;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_boulder, -1, slot);
            mdq_finish(srv);
            mdq_pass("oplocu_boulder_already_cleared");
        }
    }
    {
        int slot = mdq_place_loc(srv, loc_rockslide, MDQ_CAMP_X, MDQ_CAMP_Z, 0);

        SELFTEST_CHECK(slot >= 0, "rockslide should place");
        if( slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_rockslide, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(player->x == MDQ_CAMP_X && player->z == MDQ_CAMP_Z,
                           "rockslide climb should land in the camp, got %d,%d",
                           player->x, player->z);
            mdq_pass("oploc1_rockslide");
        }
    }

    /* ---- Lake mud / pole / plank ---- */
    {
        int mud_slot = mdq_place_loc(srv, loc_mud, MDQ_CAMP_X, MDQ_CAMP_Z + 8, 0);
        int tree_slot;
        int pole_slot;
        int flat1_slot;
        int flat2_slot;

        SELFTEST_CHECK(mud_slot >= 0, "mud pool should place");
        if( mud_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_mud, -1, mud_slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_inv_total(player, obj_mud) == 1,
                           "digging the pond should grant mdaughter_mud, got %d",
                           mdq_inv_total(player, obj_mud));
            mdq_pass("oploc1_dig_mud");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_mud, -1, mud_slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_inv_total(player, obj_mud) == 1,
                           "a second dig should not duplicate mud");
            mdq_pass("oploc1_dig_mud_already");
        }

        tree_slot = mdq_place_loc(srv, loc_tree, MDQ_CAMP_X, MDQ_CAMP_Z + 10, 0);
        SELFTEST_CHECK(tree_slot >= 0, "lake tree should place");
        if( tree_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_tree, -1, tree_slot);
            mdq_finish(srv);
            mdq_pass("oploc3_tree_slippery");

            player->last_useitem = obj_mud;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_tree, -1, tree_slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_mud_var") == 1,
                           "rubbing mud on the tree should set mdaughter_mud_var");
            SELFTEST_CHECK(mdq_inv_total(player, obj_mud) == 0,
                           "applying mud should consume it");
            mdq_pass("oplocu_mud_on_tree");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_tree, -1, tree_slot);
            mdq_finish(srv);
            mdq_pass("oploc3_tree_no_pole");

            mdq_give(player, obj_pole, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_tree, -1, tree_slot);
            mdq_finish(srv);
            mdq_pass("oploc3_tree_no_plank");

            mdq_give(player, obj_plank, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_tree, -1, tree_slot);
            mdq_finish(srv);
            SELFTEST_CHECK(player->x == 2772 && player->z == 3683,
                           "climbing the muddied tree should land on island 1, got %d,%d",
                           player->x, player->z);
            mdq_pass("oploc3_tree_climb");
        }

        pole_slot = mdq_place_loc(srv, loc_pole, 2772, 3683, 0);
        if( pole_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_pole, -1, pole_slot);
            mdq_finish(srv);
            mdq_pass("oploc1_polerocks_fail");

            player->last_useitem = obj_pole;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_pole, -1, pole_slot);
            mdq_finish(srv);
            SELFTEST_CHECK(player->x == 2773 && player->z == 3689,
                           "pole vault should land on island 2, got %d,%d",
                           player->x, player->z);
            mdq_pass("oplocu_polerocks");
        }

        flat1_slot = mdq_place_loc(srv, loc_flat1, 2773, 3689, 0);
        if( flat1_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_flat1, -1, flat1_slot);
            mdq_finish(srv);
            mdq_pass("oploc1_flatstone1_fail");

            player->last_useitem = obj_plank;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_flat1, -1, flat1_slot);
            mdq_finish(srv);
            SELFTEST_CHECK(player->x == MDQ_ISLAND3_X && player->z == MDQ_ISLAND3_Z,
                           "plank crossing should land on island 3, got %d,%d",
                           player->x, player->z);
            mdq_pass("oplocu_flatstone1");
        }

        flat2_slot = mdq_place_loc(srv, loc_flat2, MDQ_ISLAND3_X, MDQ_ISLAND3_Z, 0);
        if( flat2_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_flat2, -1, flat2_slot);
            mdq_finish(srv);
            mdq_pass("oploc1_flatstone2_jump");

            player->last_useitem = obj_plank;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_flat2, -1, flat2_slot);
            mdq_finish(srv);
            mdq_pass("oplocu_flatstone2");
        }
    }

    /* ---- Spirit pool ---- */
    {
        int slot = mdq_place_loc(srv, loc_spirit, MDQ_ISLAND3_X, MDQ_ISLAND3_Z, 0);

        SELFTEST_CHECK(slot >= 0, "shining pool should place");
        if( slot >= 0 )
        {
            mdq_set_bit(srv, "mdaughter_quest_var", MDQ_NOT_STARTED);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_spirit, -1, slot);
            mdq_finish(srv);
            mdq_pass("oploc1_spirit_wind");

            mdq_set_bit(srv, "mdaughter_quest_var", MDQ_STARTED);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_spirit, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_SPIRIT_HEARD,
                           "first listen should advance to 20, got %d",
                           mdq_get_bit(player, "mdaughter_quest_var"));
            mdq_pass("oploc1_spirit_first_listen");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_spirit, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_SPIRIT_HEARD,
                           "spirit with unfinished camp work must stay at 20, got %d",
                           mdq_get_bit(player, "mdaughter_quest_var"));
            mdq_pass("oploc1_spirit_no_progress");
        }
    }

    /* ---- Diplomacy: Hamal / Jokul / Svidi / Brundt / rock ---- */
    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_SPIRIT_HEARD);
    mdq_set_bit(srv, "mdaughter_relations_var", 0);
    hamal_slot = mdq_spawn(srv, npc_hamal, MDQ_HAMAL_X, MDQ_HAMAL_Z, 0);
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_relations_var") == MDQ_REL_ASKED,
                   "Hamal diplomacy start should write relations=10, got %d",
                   mdq_get_bit(player, "mdaughter_relations_var"));
    mdq_pass("opnpc1_hamal_diplomacy_start");

    jokul_slot = mdq_spawn(srv, npc_jokul, MDQ_JOKUL_X, MDQ_JOKUL_Z, 0);
    mdq_talk(srv, npc_jokul, jokul_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_relations_var") == MDQ_REL_JOKUL,
                   "Jokul should write relations=20, got %d",
                   mdq_get_bit(player, "mdaughter_relations_var"));
    mdq_pass("opnpc1_jokul_talk");
    mdq_talk(srv, npc_jokul, jokul_slot);
    mdq_finish(srv);
    mdq_pass("opnpc1_jokul_after");
    mdq_free_npc(srv, jokul_slot);

    svidi_slot = mdq_spawn(srv, npc_svidi, MDQ_SVIDI_X, MDQ_SVIDI_Z, 0);
    mdq_talk(srv, npc_svidi, svidi_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_relations_var") == MDQ_REL_SVIDI,
                   "Svidi should write relations=30, got %d",
                   mdq_get_bit(player, "mdaughter_relations_var"));
    mdq_pass("opnpc1_svidi_talk");

    brundt_slot = mdq_spawn(srv, npc_brundt, MDQ_BRUNDT_X, MDQ_BRUNDT_Z, 0);
    SELFTEST_CHECK(brundt_slot >= 0, "Brundt should spawn for the MD branch");
    if( brundt_slot >= 0 )
    {
        mdq_talk(srv, npc_brundt, brundt_slot);
        mdq_finish(srv);
        SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_relations_var") == MDQ_REL_SVIDI,
                       "Brundt rock demand must not skip the mine, got %d",
                       mdq_get_bit(player, "mdaughter_relations_var"));
        mdq_pass("opnpc1_brundt_demand_rock");
    }

    {
        int slot = mdq_place_loc(srv, loc_rock, MDQ_HAMAL_X - 4, MDQ_HAMAL_Z, 0);

        SELFTEST_CHECK(slot >= 0, "ancient rock should place");
        if( slot >= 0 )
        {
            player->last_useitem = obj_pick;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_rock, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_inv_total(player, obj_half) == 0,
                           "mining without a pickaxe in inv must not grant the half rock");
            mdq_pass("oplocu_ancient_rock_no_pick");

            mdq_give(player, obj_pick, 1);
            player->last_useitem = obj_pick;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_rock, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_inv_total(player, obj_half) == 1,
                           "mining the Ancient Rock should grant the half rock");
            SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_relations_var") == MDQ_REL_ROCK,
                           "mining should write relations=40, got %d",
                           mdq_get_bit(player, "mdaughter_relations_var"));
            mdq_pass("oplocu_ancient_rock");
        }
    }

    if( brundt_slot >= 0 )
    {
        mdq_tele(srv, MDQ_BRUNDT_X, MDQ_BRUNDT_Z, 0);
        mdq_talk(srv, npc_brundt, brundt_slot);
        mdq_finish(srv);
        SELFTEST_CHECK(mdq_inv_total(player, obj_guarantee) == 1,
                       "Brundt should hand over the safety guarantee");
        SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_relations_var") == MDQ_REL_GUARANTEE,
                       "Brundt accept should write relations=50, got %d",
                       mdq_get_bit(player, "mdaughter_relations_var"));
        SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_brundt_done") == 1,
                       "Brundt accept should set mdaughter_brundt_done");
        mdq_pass("opnpc1_brundt_accept_rock");
        mdq_free_npc(srv, brundt_slot);
    }

    mdq_tele(srv, MDQ_SVIDI_X, MDQ_SVIDI_Z, 0);
    mdq_talk(srv, npc_svidi, svidi_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_relations_var") == MDQ_REL_GIVEN,
                   "Svidi guarantee hand-in should write relations=60, got %d",
                   mdq_get_bit(player, "mdaughter_relations_var"));
    mdq_pass("opnpc1_svidi_guarantee");
    mdq_free_npc(srv, svidi_slot);

    hamal_slot = mdq_spawn(srv, npc_hamal, MDQ_HAMAL_X, MDQ_HAMAL_Z, 0);
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_click_until_menu(srv, 24);
    if( player->active_script && player->resume_button_count > 0 &&
        mdq_chatmenu() > 0 && player->resume_buttons[0] == mdq_chatmenu() )
        mdq_pick_row(srv, 1);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_hamal_relations_done") == 1,
                   "Hamal diplomacy-done talk should set hamal_relations_done");
    mdq_pass("opnpc1_hamal_diplomacy_done");

    /* ---- Food: bush / eat / Hamal ---- */
    {
        int slot = mdq_place_loc(srv, loc_bush, 2848, 3497, 0);

        SELFTEST_CHECK(slot >= 0, "white pearl bush should place");
        if( slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_bush, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_inv_total(player, obj_fruit) == 0,
                           "picking before food_asked should find nothing");
            mdq_pass("oploc3_pearl_gated");

            mdq_set_bit(srv, "mdaughter_food_var", MDQ_FOOD_ASKED);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_bush, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_inv_total(player, obj_fruit) == 0,
                           "picking without gloves should fail");
            mdq_pass("oploc3_pearl_no_gloves");

            worn_set(player, MDQ_HANDS, obj_gloves, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_bush, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_inv_total(player, obj_fruit) == 1,
                           "picking with gloves should grant the fruit");
            mdq_pass("oploc3_pearl_pick");

            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC3, loc_bush, -1, slot);
            mdq_finish(srv);
            mdq_pass("oploc3_pearl_already");
        }
    }

    player->last_item = obj_fruit;
    player->last_slot = 0;
    player->last_verb = 3;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD3, obj_fruit, -1, -1);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_inv_total(player, obj_seed) == 1 && mdq_inv_total(player, obj_fruit) == 0,
                   "eating the White Pearl should leave its seed");
    mdq_pass("opheld3_eat_pearl");

    mdq_clear_inv(player);
    mdq_set_bit(srv, "mdaughter_food_var", 0);
    hamal_slot = mdq_spawn(srv, npc_hamal, MDQ_HAMAL_X, MDQ_HAMAL_Z, 0);
    mdq_set_bit(srv, "mdaughter_relations_var", MDQ_REL_GIVEN);
    mdq_set_bit(srv, "mdaughter_hamal_relations_done", 1);
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_food_var") == MDQ_FOOD_ASKED,
                   "Hamal food ask should write food=10, got %d",
                   mdq_get_bit(player, "mdaughter_food_var"));
    mdq_pass("opnpc1_hamal_food_ask");

    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    mdq_pass("opnpc1_hamal_food_no_seed");

    mdq_give(player, obj_seed, 1);
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_food_var") == MDQ_FOOD_FINISHED,
                   "seed hand-in should write food=20, got %d",
                   mdq_get_bit(player, "mdaughter_food_var"));
    SELFTEST_CHECK(mdq_inv_total(player, obj_seed) == 0, "Hamal should take the seed");
    mdq_pass("opnpc1_hamal_food_handin");

    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    mdq_pass("opnpc1_hamal_camp_help_done");
    mdq_free_npc(srv, hamal_slot);

    /* ---- Spirit progress / Kendal cave ---- */
    {
        int slot = mdq_place_loc(srv, loc_spirit, MDQ_ISLAND3_X, MDQ_ISLAND3_Z, 0);

        if( slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_spirit, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_READY_FOR_KENDAL,
                           "reporting both camp tasks should write quest=30, got %d",
                           mdq_get_bit(player, "mdaughter_quest_var"));
            mdq_pass("oploc1_spirit_progress");
        }
    }

    {
        int slot = mdq_place_loc(srv, loc_cave_in, 2808, 3703, 0);

        SELFTEST_CHECK(slot >= 0, "cave entrance should place");
        if( slot >= 0 )
        {
            mdq_set_bit(srv, "mdaughter_quest_var", MDQ_SPIRIT_HEARD);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cave_in, -1, slot);
            mdq_finish(srv);
            mdq_pass("oploc1_cave_not_ready");

            mdq_set_bit(srv, "mdaughter_quest_var", MDQ_READY_FOR_KENDAL);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cave_in, -1, slot);
            mdq_finish(srv);
            mdq_pass("oploc1_cave_no_axe");

            mdq_give(player, obj_axe, 1);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cave_in, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(player->x == 2790 && player->z == 10083,
                           "chopping into the cave should land at the arrive coord, got %d,%d",
                           player->x, player->z);
            mdq_pass("oploc1_cave_enter");
        }
    }

    kendal_slot = mdq_spawn(srv, npc_kendal, MDQ_KENDAL_X, MDQ_KENDAL_Z, MDQ_KENDAL_LEVEL);
    SELFTEST_CHECK(kendal_slot >= 0, "Kendal multi-npc should spawn");
    if( kendal_slot >= 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_kendal, -1, kendal_slot);
        mdq_finish(srv);
        mdq_pass("opnpc2_kendal_gated");

        mdq_talk(srv, npc_kendal, kendal_slot);
        mdq_finish(srv);
        SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_KENDAL_FOUND,
                       "Kendal intro should write quest=40, got %d",
                       mdq_get_bit(player, "mdaughter_quest_var"));
        SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_bear_discovery") == 1,
                       "Kendal intro should set bear_discovery");
        SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_bear_mayattack") == 1,
                       "Kendal intro should set bear_mayattack");
        mdq_pass("opnpc1_kendal_intro");

        mdq_talk(srv, npc_kendal, kendal_slot);
        mdq_finish(srv);
        mdq_pass("opnpc1_kendal_nothing_left");
    }

    fighter_slot = selftest_find_npc(srv, npc_fighter);
    if( fighter_slot < 0 )
        fighter_slot = ToriRSServer_WorldNpcSpawn(srv, npc_fighter, MDQ_KENDAL_X, MDQ_KENDAL_Z,
                                                 MDQ_KENDAL_LEVEL);
    SELFTEST_CHECK(fighter_slot >= 0, "Kendal fighter should be in the world");
    if( fighter_slot >= 0 )
    {
        struct ToriRSServerNpc* fighter = &srv->npcs[fighter_slot];
        int t;

        ToriRSServer_WorldNpcSetOwner(fighter, player);
        fighter->combat_target = player->pid;
        mdq_set_bit(srv, "mdaughter_quest_var", MDQ_KENDAL_FOUND);
        mdq_set_bit(srv, "mdq_kendal_active", 1);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_fighter, -1, fighter_slot);
        mdq_finish(srv);
        mdq_pass("opnpc2_kendal_attack");

        ToriRSServer_CombatHitNpc(srv, fighter_slot, 0, fighter->max_hitpoints);
        for( t = 0; t < 16 && mdq_get_bit(player, "mdaughter_quest_var") != MDQ_KENDAL_KILLED; t++ )
            selftest_tick(srv);
        SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_KENDAL_KILLED,
                       "killing Kendal should write quest=50, got %d",
                       mdq_get_bit(player, "mdaughter_quest_var"));
        SELFTEST_CHECK(mdq_inv_total(player, obj_bearhead) == 1 ||
                           mdq_inv_total(player, obj_corpse) == 1,
                       "Kendal's death should drop the bearhead and/or corpse");
        mdq_pass("ai_queue3_kendal_falls");
    }
    mdq_free_npc(srv, kendal_slot);
    mdq_free_npc(srv, fighter_slot);

    {
        int slot = mdq_place_loc(srv, loc_cave_out, 2790, 10083, 0);

        if( slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cave_out, -1, slot);
            mdq_finish(srv);
            mdq_pass("oploc1_cave_exit");
        }
    }

    /* ---- Corpse to Hamal, Ragnar, burial ---- */
    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_KENDAL_KILLED);
    mdq_clear_inv(player);
    hamal_slot = mdq_spawn(srv, npc_hamal, MDQ_HAMAL_X, MDQ_HAMAL_Z, 0);
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_KENDAL_KILLED,
                   "Hamal without the corpse must stay at 50, got %d",
                   mdq_get_bit(player, "mdaughter_quest_var"));
    mdq_pass("opnpc1_hamal_corpse_missing");

    mdq_give(player, obj_corpse, 1);
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_CORPSE_GIVEN,
                   "showing the corpse should write quest=60, got %d",
                   mdq_get_bit(player, "mdaughter_quest_var"));
    mdq_pass("opnpc1_hamal_corpse_handin");
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    mdq_pass("opnpc1_hamal_burial_reminder");
    mdq_free_npc(srv, hamal_slot);

    ragnar_slot = mdq_spawn(srv, npc_ragnar, MDQ_RAGNAR_X, MDQ_RAGNAR_Z, 0);
    mdq_talk(srv, npc_ragnar, ragnar_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_inv_total(player, obj_necklace) == 0,
                   "Ragnar should refuse the necklace until five muddy rocks");
    mdq_pass("opnpc1_ragnar_need_rocks");

    mdq_give(player, obj_rocks, 5);
    mdq_talk(srv, npc_ragnar, ragnar_slot);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_inv_total(player, obj_necklace) == 1,
                   "Ragnar should press Asleif's necklace into the player's hands");
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_ragnar_gavenecklace") == 1,
                   "Ragnar necklace talk should set ragnar_gavenecklace");
    mdq_pass("opnpc1_ragnar_necklace");
    mdq_talk(srv, npc_ragnar, ragnar_slot);
    mdq_finish(srv);
    mdq_pass("opnpc1_ragnar_go_on");

    mdq_give(player, obj_corpse, 1);
    mdq_tele(srv, MDQ_CAMP_X, MDQ_CAMP_Z, 0);
    player->last_item = obj_corpse;
    player->last_slot = 0;
    player->last_verb = 3;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD3, obj_corpse, -1, -1);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_burial_state") == 0,
                   "burying off the island must not set burial_state");
    mdq_pass("opheld3_bury_wrong_place");

    mdq_tele(srv, MDQ_ISLAND3_X, MDQ_ISLAND3_Z, 0);
    player->last_item = obj_corpse;
    player->last_slot = 0;
    player->last_verb = 3;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD3, obj_corpse, -1, -1);
    mdq_finish(srv);
    SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_burial_state") == 1,
                   "burying on island 3 with the necklace should set burial_state");
    SELFTEST_CHECK(mdq_inv_total(player, obj_corpse) == 0, "burial should consume the corpse");
    mdq_pass("opheld3_bury_corpse");

    {
        int slot = mdq_place_loc(srv, loc_cairn, MDQ_ISLAND3_X, MDQ_ISLAND3_Z, 0);
        int xp_atk;
        int xp_pray;
        int drain;

        SELFTEST_CHECK(slot >= 0, "burial mound should place");
        if( slot >= 0 )
        {
            player->last_useitem = obj_plank;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cairn, -1, slot);
            mdq_finish(srv);
            mdq_pass("oplocu_cairn_wrong_item");

            mdq_clear_inv(player);
            mdq_give(player, obj_rocks, 4);
            player->last_useitem = obj_rocks;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cairn, -1, slot);
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_CORPSE_GIVEN,
                           "four rocks must not complete the cairn, got %d",
                           mdq_get_bit(player, "mdaughter_quest_var"));
            mdq_pass("oplocu_cairn_need_rocks");

            mdq_give(player, obj_rocks, 5);
            xp_atk = player->stat_xp_tenths[stat_attack];
            xp_pray = player->stat_xp_tenths[stat_prayer];
            player->last_useitem = obj_rocks;
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cairn, -1, slot);
            for( drain = 0;
                 drain < 40 && mdq_get_bit(player, "mdaughter_quest_var") != MDQ_COMPLETE;
                 drain++ )
            {
                if( player->active_script )
                    selftest_click_through(srv, 8);
                selftest_tick(srv);
            }
            mdq_finish(srv);
            SELFTEST_CHECK(mdq_get_bit(player, "mdaughter_quest_var") == MDQ_COMPLETE,
                           "raising the cairn should complete the quest at 70, got %d",
                           mdq_get_bit(player, "mdaughter_quest_var"));
            SELFTEST_CHECK(player->stat_xp_tenths[stat_attack] > xp_atk,
                           "completion should award Attack XP, %d -> %d",
                           xp_atk, player->stat_xp_tenths[stat_attack]);
            SELFTEST_CHECK(player->stat_xp_tenths[stat_prayer] > xp_pray,
                           "completion should award Prayer XP, %d -> %d",
                           xp_pray, player->stat_xp_tenths[stat_prayer]);
            SELFTEST_CHECK(mdq_inv_total(player, obj_bearhead) == 1,
                           "completion should grant the Bearhead mask");
            mdq_pass("oplocu_cairn_complete_scroll");
        }
    }

    hamal_slot = mdq_spawn(srv, npc_hamal, MDQ_HAMAL_X, MDQ_HAMAL_Z, 0);
    mdq_talk(srv, npc_hamal, hamal_slot);
    mdq_finish(srv);
    mdq_pass("opnpc1_hamal_complete_thanks");
    mdq_free_npc(srv, hamal_slot);

    mdq_talk(srv, npc_ragnar, ragnar_slot);
    mdq_finish(srv);
    mdq_pass("opnpc1_ragnar_complete");
    mdq_free_npc(srv, ragnar_slot);

    /* ---- Journals: not-started / mid / complete ---- */
    mdq_reset_bits(srv);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mountaindaughter_journal]", NULL, 0);
    mdq_finish(srv);
    mdq_pass("journal_not_started");

    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_STARTED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mountaindaughter_journal]", NULL, 0);
    mdq_finish(srv);
    mdq_pass("journal_mid_lake");

    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_SPIRIT_HEARD);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mountaindaughter_journal]", NULL, 0);
    mdq_finish(srv);
    mdq_pass("journal_mid_camp");

    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_READY_FOR_KENDAL);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mountaindaughter_journal]", NULL, 0);
    mdq_finish(srv);
    mdq_pass("journal_mid_cave");

    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_KENDAL_FOUND);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mountaindaughter_journal]", NULL, 0);
    mdq_finish(srv);
    mdq_pass("journal_mid_kendal");

    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_KENDAL_KILLED);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mountaindaughter_journal]", NULL, 0);
    mdq_finish(srv);
    mdq_pass("journal_mid_corpse");

    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_CORPSE_GIVEN);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mountaindaughter_journal]", NULL, 0);
    mdq_finish(srv);
    mdq_pass("journal_mid_burial");

    mdq_set_bit(srv, "mdaughter_quest_var", MDQ_COMPLETE);
    ToriRSServer_ScriptsRunProc(srv, "[proc,mountaindaughter_journal]", NULL, 0);
    mdq_finish(srv);
    mdq_pass("journal_complete");

    {
        int slot = mdq_place_loc(srv, loc_cave_in, 2808, 3703, 0);

        if( slot >= 0 )
        {
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cave_in, -1, slot);
            mdq_finish(srv);
            mdq_pass("oploc1_cave_complete_sealed");
        }
    }

    mdq_clear_inv(player);
    mdq_reset_bits(srv);
    mdq_god(player);
}

#endif
