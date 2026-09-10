#ifndef TORIRSSERVER_TEST_QUEST_MOURNINGSENDPARTI_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_MOURNINGSENDPARTI_SELFTEST_U_H

/* Mourning's End Part I Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned npcs cannot leak. Real OPNPC / OPLOC /
 * OPHELDU on the authored path. player->godmode = 1 for the whole walk
 * (no death test). Completion goes through Arianwyn's authored
 * ~mend1_quest_complete / ~quest_complete_rewards(quest_mourningsendpart1).
 * Additive MEP1 only -- do not rewrite Roving Elves, Sheep Herder, Plague
 * City Elena, SoTE, MEP2, Construction, or MTA. */

#define MEP1_NOT_STARTED 0
#define MEP1_BRIEFED 2
#define MEP1_GATHERING 3
#define MEP1_ASSIGNMENT 4
#define MEP1_GNOME_TASK 5
#define MEP1_POISON_TASK 6
#define MEP1_LEARN_SECRET 7
#define MEP1_REPORT 8
#define MEP1_COMPLETE 9

#define MEP1_GNOME_NONE 0
#define MEP1_GNOME_WEAKNESS 3
#define MEP1_GNOME_TORTURED 5
#define MEP1_GNOME_TALKED_ITEM 6
#define MEP1_GNOME_RELEASED 7
#define MEP1_GNOME_REPAIRED 9

#define MEP1_AMMO_NONE 0
#define MEP1_AMMO_RED 1
#define MEP1_AMMO_GREEN 2
#define MEP1_AMMO_BLUE 3
#define MEP1_AMMO_YELLOW 4

#define MEP1_ELENA_NONE 0
#define MEP1_ELENA_SIEVE 4

#define MEP1_REQ_RANGED 60
#define MEP1_REQ_THIEVING 50
#define MEP1_REWARD_THIEVING 400000
#define MEP1_REWARD_HITPOINTS 250000

#define MEP1_ROVING_COMPLETE 60
#define MEP1_CHOMPY_COMPLETE 65
#define MEP1_HERDER_COMPLETE 3
#define MEP1_REGICIDE_COMPLETE 15
#define MEP1_WATERFALL_COMPLETE 10

#define MEP1_ISLWYN_X 2291
#define MEP1_ISLWYN_Z 3147
#define MEP1_ARIANWYN_X 2353
#define MEP1_ARIANWYN_Z 3172
#define MEP1_MOURNER_X 2299
#define MEP1_MOURNER_Z 3328
#define MEP1_ORONWEN_X 2324
#define MEP1_ORONWEN_Z 3179
#define MEP1_ESSYLLT_X 2044
#define MEP1_ESSYLLT_Z 4628
#define MEP1_GNOME_X 2036
#define MEP1_GNOME_Z 4630
#define MEP1_ELENA_X 2592
#define MEP1_ELENA_Z 3336
#define MEP1_DOOR_X 2551
#define MEP1_DOOR_Z 3273

static void
mep1_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "MEP1 PASS: %s\n", step);
}

static void
mep1_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
mep1_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
mep1_finish(struct ToriRSServer* srv)
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
mep1_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
mep1_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mep1_chatmenu();
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
mep1_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = mep1_chatmenu();
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
mep1_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    mep1_god(player);
    selftest_tick(srv);
}

static int
mep1_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    mep1_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static void
mep1_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
mep1_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
mep1_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( mep1_inv_total(player, obj_id) >= count )
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
mep1_set_var(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;
    int bit;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 && srv->active_player )
    {
        srv->active_player->varps[varp] = value;
        return;
    }
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mep1_get_var(struct ToriRSServerPlayer* player, const char* name)
{
    int varp;
    int bit;

    assert(player);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        return player->varps[varp];
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit < 0 )
        return 0;
    return ToriRSServer_VarbitGet(player, bit);
}

static void
mep1_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
mep1_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
mep1_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    mep1_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 0, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    return slot;
}

static void
mep1_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
mep1_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    mep1_talk(srv, npc_type, slot);
    mep1_finish(srv);
}

static void
mep1_talk_refuse(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    mep1_talk(srv, npc_type, slot);
    mep1_click_until_menu(srv, 16);
    mep1_pick_row(srv, 2);
    mep1_finish(srv);
}

static void
mep1_oploc_finish(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    mep1_finish(srv);
}

static void
mep1_use_held(struct ToriRSServer* srv, int held_id, int useitem_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = useitem_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, held_id, -1, -1);
    mep1_finish(srv);
    player->last_useitem = -1;
}

static void
mep1_set_skills(
    struct ToriRSServerPlayer* player,
    int stat_ranged,
    int stat_thief,
    int ranged,
    int thief)
{
    assert(player);
    if( stat_ranged >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_ranged, ranged);
    if( stat_thief >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_thief, thief);
}

static void
mep1_reset_state(struct ToriRSServer* srv)
{
    assert(srv);
    mep1_set_var(srv, "mourning_quest", MEP1_NOT_STARTED);
    mep1_set_bit(srv, "mourning_gnome", MEP1_GNOME_NONE);
    mep1_set_bit(srv, "mourning_gun_ammo", MEP1_AMMO_NONE);
    mep1_set_bit(srv, "mourning_elena", MEP1_ELENA_NONE);
    mep1_set_bit(srv, "mourning_sheep_red", 0);
    mep1_set_bit(srv, "mourning_sheep_green", 0);
    mep1_set_bit(srv, "mourning_sheep_yellow", 0);
    mep1_set_bit(srv, "mourning_sheep_blue", 0);
    mep1_set_bit(srv, "mourning_food_poison1", 0);
    mep1_set_bit(srv, "mourning_food_poison2", 0);
    mep1_set_bit(srv, "mourning_food_poison3", 0);
    mep1_set_bit(srv, "mourning_dye_chat", 0);
    mep1_set_bit(srv, "mourning_tegid_chat", 0);
    mep1_set_bit(srv, "mourning_silk_1", 0);
    mep1_set_bit(srv, "mourning_silk_2", 0);
    mep1_set_bit(srv, "mourning_fur", 0);
    mep1_set_bit(srv, "mourning_trousers_chat", 0);
    mep1_set_bit(srv, "mourning_trousers_fixed", 0);
    mep1_set_bit(srv, "mourning_mourner_disguise", 0);
    mep1_set_var(srv, "sote", 0);
}

static void
mep1_set_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    mep1_set_var(srv, "rovingelves_quest", MEP1_ROVING_COMPLETE);
    mep1_set_var(srv, "chompybird", MEP1_CHOMPY_COMPLETE);
    mep1_set_var(srv, "sheepherderquest", MEP1_HERDER_COMPLETE);
    mep1_set_var(srv, "regicide_quest", MEP1_REGICIDE_COMPLETE);
    mep1_set_var(srv, "waterfall_quest", MEP1_WATERFALL_COMPLETE);
    mep1_set_var(srv, "sote", 0);
}

static void
mep1_wear_disguise(struct ToriRSServerPlayer* player,
    int obj_mask, int obj_top, int obj_legs, int obj_cloak, int obj_boots, int obj_gloves)
{
    assert(player);
    if( obj_mask > 0 )
        worn_set(player, TORIRSSERVER_WEAR_HEAD, obj_mask, 1);
    if( obj_cloak > 0 )
        worn_set(player, TORIRSSERVER_WEAR_CAPE, obj_cloak, 1);
    if( obj_top > 0 )
        worn_set(player, TORIRSSERVER_WEAR_BODY, obj_top, 1);
    if( obj_legs > 0 )
        worn_set(player, TORIRSSERVER_WEAR_LEGS, obj_legs, 1);
    if( obj_gloves > 0 )
        worn_set(player, TORIRSSERVER_WEAR_HANDS, obj_gloves, 1);
    if( obj_boots > 0 )
        worn_set(player, TORIRSSERVER_WEAR_FEET, obj_boots, 1);
}

static void
selftest_quest_mourningsendparti(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int stat_ranged;
    int stat_thief;
    int stat_hitpoints;
    int npc_islwyn;
    int npc_arianwyn;
    int npc_mourner;
    int npc_oronwen;
    int npc_essyllt;
    int npc_elena;
    int npc_sheep1;
    int npc_sheep2;
    int npc_sheep3;
    int npc_sheep4;
    int loc_basket;
    int loc_door;
    int loc_trap;
    int loc_rack;
    int loc_orchard;
    int loc_press;
    int loc_still;
    int loc_range;
    int loc_food1;
    int loc_food2;
    int obj_boots;
    int obj_gloves;
    int obj_cloak;
    int obj_ripped;
    int obj_mask;
    int obj_letter;
    int obj_bloody;
    int obj_top;
    int obj_legs;
    int obj_soap;
    int obj_water;
    int obj_silk;
    int obj_fur;
    int obj_key;
    int obj_broken;
    int obj_gun;
    int obj_feather;
    int obj_crunch;
    int obj_logs;
    int obj_leather;
    int obj_reddye;
    int obj_greendye;
    int obj_yellowdye;
    int obj_bluedye;
    int obj_bellows;
    int obj_toad_red;
    int obj_toad_green;
    int obj_toad_yellow;
    int obj_toad_blue;
    int obj_apple;
    int obj_sieve;
    int obj_barrel;
    int obj_mash;
    int obj_naphtha;
    int obj_mix;
    int obj_toxic_naphtha;
    int obj_toxin;
    int obj_coal;
    int obj_crystal;
    int slot;
    int loc_slot;
    int thief_xp_before;
    int hp_xp_before;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: mourningsendparti critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer mep1 selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    mep1_god(player);

    stat_ranged = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
    stat_thief = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    stat_hitpoints = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "hitpoints");
    npc_islwyn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "roving_islwyn_2ops");
    npc_arianwyn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mourning_arianwyn");
    npc_mourner = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mourning_overpass_mourner");
    npc_oronwen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mourning_seamstress");
    npc_essyllt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "mourner_hideout_head_mourner");
    npc_elena = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "elena2");
    npc_sheep1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "herder_plaguesheep_1");
    npc_sheep2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "herder_plaguesheep_2");
    npc_sheep3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "herder_plaguesheep_3");
    npc_sheep4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "herder_plaguesheep_4");
    loc_basket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "eadgar_laundry_basket");
    loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mournerstewdoor");
    loc_trap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mourning_hideout_trap_door");
    loc_rack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mourning_gnome_rack");
    loc_orchard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mourning_orchard_applepile");
    loc_press = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mourning_orchard_applebarrel_empty");
    loc_still = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "regicide_fractionalizing_still");
    loc_range = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "carnilleanrange");
    loc_food1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mourning_sack_full1");
    loc_food2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "mourning_sack_full2");
    obj_boots = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_boots");
    obj_gloves = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_gloves");
    obj_cloak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_cloak");
    obj_ripped = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_ripped_mourner_legs");
    obj_mask = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "gasmask");
    obj_letter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_message");
    obj_bloody = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_bloody_mourner_top");
    obj_top = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_top");
    obj_legs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_mourner_legs");
    obj_soap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_soap");
    obj_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_water");
    obj_silk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silk");
    obj_fur = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fur");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_gnome_key");
    obj_broken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_paint_gun_broken");
    obj_gun = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_paint_gun");
    obj_feather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "feather");
    obj_crunch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "toad_crunchies");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "magic_logs");
    obj_leather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "leather");
    obj_reddye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "reddye");
    obj_greendye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "greendye");
    obj_yellowdye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yellowdye");
    obj_bluedye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bluedye");
    obj_bellows = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "empty_ogre_bellows");
    obj_toad_red = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_bloated_toad_red");
    obj_toad_green = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_bloated_toad_green");
    obj_toad_yellow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_bloated_toad_yellow");
    obj_toad_blue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_bloated_toad_blue");
    obj_apple = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rottenapples");
    obj_sieve = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_sieve");
    obj_barrel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "applebarrel_full");
    obj_mash = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_applebarrel_mush");
    obj_naphtha = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "regicide_barrel_naphtha");
    obj_mix = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_applebarrel_naphtha_mush");
    obj_toxic_naphtha = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_toxic_naphtha");
    obj_toxin = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_apple_toxin");
    obj_coal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coal");
    obj_crystal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mourning_teleport_crystal_4");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "mourning_quest") >= 0,
                   "varp mourning_quest should resolve");
    SELFTEST_CHECK(npc_islwyn > 0, "npc roving_islwyn_2ops should resolve");
    SELFTEST_CHECK(npc_arianwyn > 0, "npc mourning_arianwyn should resolve");
    SELFTEST_CHECK(npc_essyllt > 0, "npc mourner_hideout_head_mourner should resolve");
    SELFTEST_CHECK(obj_crystal > 0, "obj mourning_teleport_crystal_4 should resolve");
    if( npc_islwyn <= 0 || npc_arianwyn <= 0 )
    {
        fprintf(stderr, "ToriRSServer mep1 selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    mep1_clear_inv(player);
    mep1_reset_state(srv);
    mep1_set_prereqs(srv);
    mep1_god(player);
    mep1_set_skills(player, stat_ranged, stat_thief, MEP1_REQ_RANGED, MEP1_REQ_THIEVING);

    ToriRSServer_ScriptsRunProc(srv, "[proc,mend1_journal]", NULL, 0);
    mep1_finish(srv);
    mep1_pass("journal_not_started");

    /* ---- Islwyn skill / prereq refuse, later, ready ---- */
    slot = mep1_spawn(srv, npc_islwyn, MEP1_ISLWYN_X, MEP1_ISLWYN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Islwyn should spawn");
    if( slot >= 0 )
    {
        mep1_set_skills(player, stat_ranged, stat_thief, 1, MEP1_REQ_THIEVING);
        mep1_talk_finish(srv, npc_islwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_NOT_STARTED,
                       "low Ranged must not start Mourning's End Part I");
        mep1_pass("opnpc1_islwyn_skillgate_ranged");

        mep1_set_skills(player, stat_ranged, stat_thief, MEP1_REQ_RANGED, 1);
        mep1_talk_finish(srv, npc_islwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_NOT_STARTED,
                       "low Thieving must not start Mourning's End Part I");
        mep1_pass("opnpc1_islwyn_skillgate_thieving");

        mep1_set_skills(player, stat_ranged, stat_thief, MEP1_REQ_RANGED, MEP1_REQ_THIEVING);
        mep1_set_var(srv, "rovingelves_quest", 0);
        {
            int32_t meets = 1;

            SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(srv,
                               "[proc,mend1_meets_requirements]", NULL, 0, &meets) == 1 &&
                               meets == 0,
                           "missing Roving Elves must fail mend1_meets_requirements");
        }
        mep1_pass("opnpc1_islwyn_prereq_rovingelves");
        mep1_set_var(srv, "rovingelves_quest", MEP1_ROVING_COMPLETE);

        mep1_set_var(srv, "chompybird", 0);
        mep1_talk_finish(srv, npc_islwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_NOT_STARTED,
                       "missing Big Chompy must not start MEP1");
        mep1_pass("opnpc1_islwyn_prereq_chompy");
        mep1_set_var(srv, "chompybird", MEP1_CHOMPY_COMPLETE);

        mep1_set_var(srv, "sheepherderquest", 0);
        mep1_talk_finish(srv, npc_islwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_NOT_STARTED,
                       "missing Sheep Herder must not start MEP1");
        mep1_pass("opnpc1_islwyn_prereq_sheepherder");
        mep1_set_var(srv, "sheepherderquest", MEP1_HERDER_COMPLETE);

        mep1_talk_refuse(srv, npc_islwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_NOT_STARTED,
                       "Islwyn later-choice must not start MEP1");
        mep1_pass("opnpc1_islwyn_later");

        mep1_talk_finish(srv, npc_islwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_BRIEFED,
                       "Islwyn ready must write briefed, got %d",
                       mep1_get_var(player, "mourning_quest"));
        mep1_pass("opnpc1_islwyn_ready_send_arianwyn");

        mep1_talk_finish(srv, npc_islwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_BRIEFED,
                       "post-start Islwyn must stay briefed");
        mep1_pass("opnpc1_islwyn_already_briefed");
        mep1_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,mend1_journal]", NULL, 0);
    mep1_finish(srv);
    mep1_pass("journal_briefed");

    /* ---- Arianwyn too-early / briefing / reminders ---- */
    slot = mep1_spawn(srv, npc_arianwyn, MEP1_ARIANWYN_X, MEP1_ARIANWYN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Arianwyn should spawn");
    if( slot >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_NOT_STARTED);
        mep1_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_NOT_STARTED,
                       "Arianwyn too-early must not start gathering");
        mep1_pass("opnpc1_arianwyn_too_early");

        mep1_set_var(srv, "mourning_quest", MEP1_BRIEFED);
        mep1_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_GATHERING,
                       "Arianwyn briefing must write gathering, got %d",
                       mep1_get_var(player, "mourning_quest"));
        mep1_pass("opnpc1_arianwyn_briefing");

        mep1_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_GATHERING,
                       "Arianwyn gathering reminder must stay gathering");
        mep1_pass("opnpc1_arianwyn_gathering_reminder");

        mep1_set_var(srv, "mourning_quest", MEP1_ASSIGNMENT);
        mep1_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_ASSIGNMENT,
                       "Arianwyn mid-assignment must stay assignment");
        mep1_pass("opnpc1_arianwyn_mid_assignment");
        mep1_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,mend1_journal]", NULL, 0);
    mep1_finish(srv);
    mep1_pass("journal_gathering");

    /* ---- Overpass mourner narrated kill ---- */
    mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
    mep1_clear_inv(player);
    slot = mep1_spawn(srv, npc_mourner, MEP1_MOURNER_X, MEP1_MOURNER_Z, 0);
    SELFTEST_CHECK(slot >= 0, "overpass mourner should spawn");
    if( slot >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_NOT_STARTED);
        mep1_talk_finish(srv, npc_mourner, slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_boots) == 0,
                       "too-early mourner must not drop disguise");
        mep1_pass("opnpc1_mourner_too_early");

        mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
        mep1_talk_finish(srv, npc_mourner, slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_bloody) == 1,
                       "narrated mourner kill must grant bloody top");
        SELFTEST_CHECK(mep1_inv_total(player, obj_ripped) == 1,
                       "narrated mourner kill must grant ripped trousers");
        SELFTEST_CHECK(mep1_inv_total(player, obj_letter) == 1,
                       "narrated mourner kill must grant letter");
        mep1_pass("opnpc1_mourner_kill_narrated");

        mep1_talk_finish(srv, npc_mourner, slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_boots) == 1,
                       "second mourner click must not duplicate loot");
        mep1_pass("opnpc1_mourner_already_looted");
        mep1_free_npc(srv, slot);
    }

    /* ---- Tegid soap / clean top ---- */
    if( loc_basket >= 0 && obj_soap > 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_NOT_STARTED);
        loc_slot = mep1_place_loc(srv, loc_basket, 2884, 3412, 0);
        mep1_oploc_finish(srv, loc_basket, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_soap) == 0,
                       "Tegid basket wrong-state must not grant soap");
        mep1_pass("oploc1_tegid_wrong_state");

        mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
        mep1_clear_inv(player);
        mep1_oploc_finish(srv, loc_basket, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_soap) == 0,
                       "Tegid basket without bloody top must not grant soap");
        mep1_pass("oploc1_tegid_no_reason");

        mep1_give(player, obj_bloody, 1);
        mep1_oploc_finish(srv, loc_basket, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_soap) == 1,
                       "Tegid basket must steal soap");
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_tegid_chat") == 1,
                       "Tegid soap must set mourning_tegid_chat");
        mep1_pass("oploc1_tegid_steal_soap");

        mep1_oploc_finish(srv, loc_basket, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_soap) == 1,
                       "Tegid basket already-looted must stay at one soap");
        mep1_pass("oploc1_tegid_already");
    }

    if( obj_soap > 0 && obj_bloody > 0 && obj_water > 0 && obj_top > 0 )
    {
        mep1_clear_inv(player);
        mep1_give(player, obj_soap, 1);
        mep1_give(player, obj_bloody, 1);
        mep1_use_held(srv, obj_soap, obj_bloody);
        SELFTEST_CHECK(mep1_inv_total(player, obj_top) == 0,
                       "cleaning without water must refuse");
        mep1_pass("opheldu_clean_top_need_water");

        mep1_give(player, obj_water, 1);
        mep1_use_held(srv, obj_soap, obj_bloody);
        SELFTEST_CHECK(mep1_inv_total(player, obj_top) == 1,
                       "soap + water must clean the bloody top");
        mep1_pass("opheldu_clean_top");
    }

    /* ---- Oronwen trousers ---- */
    slot = mep1_spawn(srv, npc_oronwen, MEP1_ORONWEN_X, MEP1_ORONWEN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Oronwen should spawn");
    if( slot >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_NOT_STARTED);
        mep1_talk_finish(srv, npc_oronwen, slot);
        mep1_pass("opnpc1_oronwen_wrong_state");

        mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
        mep1_clear_inv(player);
        mep1_talk_finish(srv, npc_oronwen, slot);
        mep1_pass("opnpc1_oronwen_need_trousers");

        mep1_give(player, obj_ripped, 1);
        mep1_talk_finish(srv, npc_oronwen, slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_legs) == 0,
                       "Oronwen without silk/fur must not mend");
        mep1_pass("opnpc1_oronwen_need_silk_fur");

        mep1_give(player, obj_silk, 2);
        mep1_give(player, obj_fur, 1);
        mep1_talk_finish(srv, npc_oronwen, slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_legs) == 1,
                       "Oronwen with silk and fur must mend trousers");
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_trousers_fixed") == 1,
                       "mending must set mourning_trousers_fixed");
        mep1_pass("opnpc1_oronwen_mend");

        mep1_talk_finish(srv, npc_oronwen, slot);
        mep1_pass("opnpc1_oronwen_already");
        mep1_free_npc(srv, slot);
    }

    /* ---- HQ door refuse / enter ---- */
    if( loc_door >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
        mep1_clear_inv(player);
        loc_slot = mep1_place_loc(srv, loc_door, MEP1_DOOR_X, MEP1_DOOR_Z, 0);
        mep1_oploc_finish(srv, loc_door, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_mourner_disguise") == 0,
                       "HQ door without disguise must refuse");
        mep1_pass("oploc1_hq_door_no_disguise");

        mep1_wear_disguise(player, obj_mask, obj_top, obj_legs, obj_cloak, obj_boots, obj_gloves);
        mep1_oploc_finish(srv, loc_door, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_mourner_disguise") == 0,
                       "HQ door without letter must refuse");
        mep1_pass("oploc1_hq_door_no_letter");

        mep1_give(player, obj_letter, 1);
        mep1_oploc_finish(srv, loc_door, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_mourner_disguise") == 1,
                       "HQ door with full disguise and letter must enter");
        mep1_pass("oploc1_hq_door_enter");
    }

    if( loc_trap >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_NOT_STARTED);
        loc_slot = mep1_place_loc(srv, loc_trap, MEP1_ESSYLLT_X, MEP1_ESSYLLT_Z, 0);
        mep1_oploc_finish(srv, loc_trap, loc_slot);
        mep1_pass("oploc1_trapdoor_wrong_state");

        mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
        mep1_oploc_finish(srv, loc_trap, loc_slot);
        mep1_pass("oploc1_trapdoor_climb");
    }

    /* ---- Essyllt assignment / key / device ---- */
    mep1_clear_inv(player);
    mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
    slot = mep1_spawn(srv, npc_essyllt, MEP1_ESSYLLT_X, MEP1_ESSYLLT_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Essyllt should spawn");
    if( slot >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_NOT_STARTED);
        mep1_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_NOT_STARTED,
                       "Essyllt too-early must not assign");
        mep1_pass("opnpc1_essyllt_too_early");

        mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
        mep1_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_ASSIGNMENT,
                       "Essyllt must write assignment and hand key/device, got %d",
                       mep1_get_var(player, "mourning_quest"));
        SELFTEST_CHECK(mep1_inv_total(player, obj_key) == 1,
                       "Essyllt must give gnome key");
        SELFTEST_CHECK(mep1_inv_total(player, obj_broken) == 1,
                       "Essyllt must give broken device");
        mep1_pass("opnpc1_essyllt_assignment_key_device");

        mep1_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_ASSIGNMENT,
                       "Essyllt mid-gnome reminder must stay assignment");
        mep1_pass("opnpc1_essyllt_gnome_reminder");
        mep1_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,mend1_journal]", NULL, 0);
    mep1_finish(srv);
    mep1_pass("journal_assignment");

    /* ---- Gnome rack beats ---- */
    if( loc_rack >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_GATHERING);
        loc_slot = mep1_place_loc(srv, loc_rack, MEP1_GNOME_X, MEP1_GNOME_Z, 0);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_NONE,
                       "gnome rack too-early must stay none");
        mep1_pass("oploc1_gnome_too_early");

        mep1_set_var(srv, "mourning_quest", MEP1_ASSIGNMENT);
        mep1_clear_inv(player);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_NONE,
                       "gnome without key must stay none");
        mep1_pass("oploc1_gnome_need_key");

        mep1_give(player, obj_key, 1);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_NONE,
                       "gnome without crunchies/feather must stay none");
        mep1_pass("oploc1_gnome_need_crunchies");

        mep1_give(player, obj_feather, 1);
        mep1_give(player, obj_crunch, 1);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_WEAKNESS,
                       "gnome first talk must write weakness");
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_GNOME_TASK,
                       "gnome weakness must write gnome_task, got %d",
                       mep1_get_var(player, "mourning_quest"));
        mep1_pass("oploc1_gnome_weakness");

        mep1_clear_inv(player);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_WEAKNESS,
                       "tickle without feather must stay weakness");
        mep1_pass("oploc1_gnome_tickle_need_feather");

        mep1_give(player, obj_feather, 1);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_TORTURED,
                       "tickle must write tortured");
        mep1_pass("oploc1_gnome_tickle");

        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_TORTURED,
                       "talk-again without items must stay tortured");
        mep1_pass("oploc1_gnome_need_items");

        mep1_give(player, obj_crunch, 1);
        mep1_give(player, obj_logs, 1);
        mep1_give(player, obj_leather, 1);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_TALKED_ITEM,
                       "talk-again with items must write talked_item");
        mep1_pass("oploc1_gnome_talk_items");

        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_TALKED_ITEM,
                       "release without key must stay talked_item");
        mep1_pass("oploc1_gnome_release_need_key");

        mep1_give(player, obj_key, 1);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_RELEASED,
                       "release must write released");
        mep1_pass("oploc1_gnome_release");

        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_RELEASED,
                       "give-items without kit must stay released");
        mep1_pass("oploc1_gnome_give_waiting");

        mep1_give(player, obj_crunch, 1);
        mep1_give(player, obj_logs, 1);
        mep1_give(player, obj_leather, 1);
        mep1_give(player, obj_broken, 1);
        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gnome") == MEP1_GNOME_REPAIRED,
                       "give-items must write repaired");
        SELFTEST_CHECK(mep1_inv_total(player, obj_gun) == 1,
                       "gnome must return the repaired device");
        mep1_pass("oploc1_gnome_repaired");

        mep1_oploc_finish(srv, loc_rack, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_dye_chat") == 1,
                       "toad briefing must set mourning_dye_chat");
        mep1_pass("oploc1_gnome_toad_briefing");

        mep1_oploc_finish(srv, loc_rack, loc_slot);
        mep1_pass("oploc1_gnome_nod");
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,mend1_journal]", NULL, 0);
    mep1_finish(srv);
    mep1_pass("journal_gnome");

    /* ---- Dye bellows / toad load / each colour sheep ---- */
    mep1_set_var(srv, "mourning_quest", MEP1_GNOME_TASK);
    mep1_set_bit(srv, "mourning_dye_chat", 1);
    mep1_set_bit(srv, "mourning_gun_ammo", MEP1_AMMO_NONE);
    mep1_clear_inv(player);
    if( obj_bellows > 0 && obj_reddye > 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_ASSIGNMENT);
        mep1_give(player, obj_bellows, 1);
        mep1_give(player, obj_reddye, 1);
        mep1_use_held(srv, obj_bellows, obj_reddye);
        SELFTEST_CHECK(mep1_inv_total(player, obj_toad_red) == 0,
                       "bellows too-early must refuse");
        mep1_pass("opheldu_bellows_too_early");

        mep1_set_var(srv, "mourning_quest", MEP1_GNOME_TASK);
        mep1_use_held(srv, obj_bellows, obj_reddye);
        SELFTEST_CHECK(mep1_inv_total(player, obj_toad_red) == 1,
                       "red dye-on-bellows must inflate a red toad");
        mep1_pass("opheldu_bellows_red");

        mep1_give(player, obj_greendye, 1);
        mep1_use_held(srv, obj_bellows, obj_greendye);
        SELFTEST_CHECK(mep1_inv_total(player, obj_toad_green) == 1,
                       "green dye-on-bellows must inflate a green toad");
        mep1_pass("opheldu_bellows_green");

        mep1_give(player, obj_yellowdye, 1);
        mep1_use_held(srv, obj_bellows, obj_yellowdye);
        SELFTEST_CHECK(mep1_inv_total(player, obj_toad_yellow) == 1,
                       "yellow dye-on-bellows must inflate a yellow toad");
        mep1_pass("opheldu_bellows_yellow");

        mep1_give(player, obj_bluedye, 1);
        mep1_use_held(srv, obj_bellows, obj_bluedye);
        SELFTEST_CHECK(mep1_inv_total(player, obj_toad_blue) == 1,
                       "blue dye-on-bellows must inflate a blue toad");
        mep1_pass("opheldu_bellows_blue");
    }

    if( obj_gun > 0 && obj_toad_red > 0 )
    {
        mep1_give(player, obj_gun, 1);
        mep1_use_held(srv, obj_toad_red, obj_gun);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gun_ammo") == MEP1_AMMO_RED,
                       "loading red toad must write ammo red");
        mep1_pass("opheldu_toad_load_red");

        mep1_use_held(srv, obj_toad_green, obj_gun);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_gun_ammo") == MEP1_AMMO_RED,
                       "already-loaded device must refuse a second toad");
        mep1_pass("opheldu_toad_already_loaded");
    }

    if( npc_sheep1 > 0 && obj_gun > 0 )
    {
        worn_set(player, TORIRSSERVER_WEAR_WEAPON, obj_gun, 1);
        slot = mep1_spawn(srv, npc_sheep1, 2600, 3360, 0);
        if( slot >= 0 )
        {
            mep1_set_bit(srv, "mourning_gun_ammo", MEP1_AMMO_NONE);
            mep1_talk_finish(srv, npc_sheep1, slot);
            SELFTEST_CHECK(mep1_get_bit(player, "mourning_sheep_red") == 0,
                           "red sheep without red ammo must refuse");
            mep1_pass("opnpc1_sheep_red_wrong_ammo");

            mep1_set_bit(srv, "mourning_gun_ammo", MEP1_AMMO_RED);
            mep1_talk_finish(srv, npc_sheep1, slot);
            SELFTEST_CHECK(mep1_get_bit(player, "mourning_sheep_red") == 1,
                           "red sheep fire must mark red");
            mep1_pass("opnpc1_sheep_red");
            mep1_free_npc(srv, slot);
        }
        if( npc_sheep2 > 0 )
        {
            slot = mep1_spawn(srv, npc_sheep2, 2602, 3360, 0);
            if( slot >= 0 )
            {
                mep1_set_bit(srv, "mourning_gun_ammo", MEP1_AMMO_GREEN);
                mep1_talk_finish(srv, npc_sheep2, slot);
                SELFTEST_CHECK(mep1_get_bit(player, "mourning_sheep_green") == 1,
                               "green sheep fire must mark green");
                mep1_pass("opnpc1_sheep_green");
                mep1_free_npc(srv, slot);
            }
        }
        if( npc_sheep4 > 0 )
        {
            slot = mep1_spawn(srv, npc_sheep4, 2604, 3360, 0);
            if( slot >= 0 )
            {
                mep1_set_bit(srv, "mourning_gun_ammo", MEP1_AMMO_YELLOW);
                mep1_talk_finish(srv, npc_sheep4, slot);
                SELFTEST_CHECK(mep1_get_bit(player, "mourning_sheep_yellow") == 1,
                               "yellow sheep fire must mark yellow");
                mep1_pass("opnpc1_sheep_yellow");
                mep1_free_npc(srv, slot);
            }
        }
        if( npc_sheep3 > 0 )
        {
            slot = mep1_spawn(srv, npc_sheep3, 2606, 3360, 0);
            if( slot >= 0 )
            {
                mep1_set_bit(srv, "mourning_gun_ammo", MEP1_AMMO_BLUE);
                mep1_talk_finish(srv, npc_sheep3, slot);
                SELFTEST_CHECK(mep1_get_bit(player, "mourning_sheep_blue") == 1,
                               "blue sheep fire must mark blue");
                mep1_pass("opnpc1_sheep_blue");
                mep1_free_npc(srv, slot);
            }
        }
        worn_set(player, TORIRSSERVER_WEAR_WEAPON, -1, 0);
    }

    /* ---- Essyllt after sheep / Elena / poison chain ---- */
    mep1_set_var(srv, "mourning_quest", MEP1_GNOME_TASK);
    mep1_set_bit(srv, "mourning_sheep_red", 1);
    mep1_set_bit(srv, "mourning_sheep_green", 1);
    mep1_set_bit(srv, "mourning_sheep_yellow", 1);
    mep1_set_bit(srv, "mourning_sheep_blue", 1);
    mep1_clear_inv(player);
    slot = mep1_spawn(srv, npc_essyllt, MEP1_ESSYLLT_X, MEP1_ESSYLLT_Z, 0);
    if( slot >= 0 )
    {
        mep1_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_POISON_TASK,
                       "Essyllt after sheep must write poison_task, got %d",
                       mep1_get_var(player, "mourning_quest"));
        SELFTEST_CHECK(mep1_inv_total(player, obj_apple) == 1,
                       "Essyllt must auto-grant a rotten apple");
        mep1_pass("opnpc1_essyllt_after_sheep");
        mep1_free_npc(srv, slot);
    }

    slot = mep1_spawn(srv, npc_elena, MEP1_ELENA_X, MEP1_ELENA_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Elena should spawn");
    if( slot >= 0 )
    {
        mep1_clear_inv(player);
        mep1_talk_finish(srv, npc_elena, slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_elena") == MEP1_ELENA_NONE,
                       "Elena without apple must refuse");
        mep1_pass("opnpc1_elena_no_apple");

        mep1_give(player, obj_apple, 1);
        mep1_talk_finish(srv, npc_elena, slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_elena") == MEP1_ELENA_SIEVE,
                       "Elena rotten-apple hand-in must write sieve");
        SELFTEST_CHECK(mep1_inv_total(player, obj_sieve) == 1,
                       "Elena must grant the sieve");
        mep1_pass("opnpc1_elena_rotten_apple_sieve");

        mep1_talk_finish(srv, npc_elena, slot);
        mep1_pass("opnpc1_elena_already");
        mep1_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,mend1_journal]", NULL, 0);
    mep1_finish(srv);
    mep1_pass("journal_poison");

    if( loc_orchard >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_ASSIGNMENT);
        loc_slot = mep1_place_loc(srv, loc_orchard, 2540, 3330, 0);
        mep1_oploc_finish(srv, loc_orchard, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_barrel) == 0,
                       "orchard too-early must refuse");
        mep1_pass("oploc1_orchard_too_early");

        mep1_set_var(srv, "mourning_quest", MEP1_POISON_TASK);
        mep1_set_bit(srv, "mourning_elena", MEP1_ELENA_SIEVE);
        mep1_oploc_finish(srv, loc_orchard, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_barrel) == 1,
                       "orchard must auto-grant a filled barrel");
        mep1_pass("oploc1_orchard_fill_barrel");
    }

    if( loc_press >= 0 )
    {
        loc_slot = mep1_place_loc(srv, loc_press, 2542, 3330, 0);
        mep1_clear_inv(player);
        mep1_oploc_finish(srv, loc_press, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_mash) == 0,
                       "press without barrel must refuse");
        mep1_pass("oploc1_press_no_barrel");

        mep1_give(player, obj_barrel, 1);
        mep1_oploc_finish(srv, loc_press, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_mash) == 1,
                       "press must turn apples into mash");
        mep1_pass("oploc1_press");
    }

    if( loc_still >= 0 )
    {
        loc_slot = mep1_place_loc(srv, loc_still, 2930, 3210, 0);
        mep1_clear_inv(player);
        mep1_oploc_finish(srv, loc_still, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_naphtha) == 0,
                       "still without coal must refuse");
        mep1_pass("oploc1_still_need_coal");

        mep1_give(player, obj_coal, 10);
        mep1_oploc_finish(srv, loc_still, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_naphtha) == 1,
                       "still soft-skip must grant naphtha");
        mep1_pass("oploc1_still_naphtha_softskip");
    }

    if( obj_naphtha > 0 && obj_mash > 0 && obj_mix > 0 )
    {
        mep1_clear_inv(player);
        mep1_give(player, obj_naphtha, 1);
        mep1_give(player, obj_mash, 1);
        mep1_use_held(srv, obj_naphtha, obj_mash);
        SELFTEST_CHECK(mep1_inv_total(player, obj_mix) == 1,
                       "naphtha + mash must mix");
        mep1_pass("opheldu_mix_naphtha");
    }

    if( obj_sieve > 0 && obj_mix > 0 && obj_toxic_naphtha > 0 )
    {
        mep1_give(player, obj_sieve, 1);
        mep1_use_held(srv, obj_sieve, obj_mix);
        SELFTEST_CHECK(mep1_inv_total(player, obj_toxic_naphtha) == 1,
                       "sieve must strain toxic naphtha");
        mep1_pass("opheldu_sieve_mix");
    }

    if( loc_range >= 0 && obj_toxic_naphtha > 0 && obj_toxin > 0 )
    {
        loc_slot = mep1_place_loc(srv, loc_range, 2565, 3270, 0);
        mep1_clear_inv(player);
        mep1_oploc_finish(srv, loc_range, loc_slot);
        mep1_pass("oploc1_cook_nothing");

        mep1_give(player, obj_toxic_naphtha, 1);
        mep1_oploc_finish(srv, loc_range, loc_slot);
        SELFTEST_CHECK(mep1_inv_total(player, obj_toxin) == 2,
                       "range must cook two toxic powder");
        mep1_pass("oploc1_cook_toxin");
    }

    if( loc_food1 >= 0 )
    {
        mep1_set_var(srv, "mourning_quest", MEP1_GNOME_TASK);
        loc_slot = mep1_place_loc(srv, loc_food1, 2530, 3280, 0);
        mep1_oploc_finish(srv, loc_food1, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_food_poison1") == 0,
                       "food1 too-early must refuse");
        mep1_pass("oploc1_food1_too_early");

        mep1_set_var(srv, "mourning_quest", MEP1_POISON_TASK);
        mep1_clear_inv(player);
        mep1_set_bit(srv, "mourning_food_poison1", 0);
        mep1_oploc_finish(srv, loc_food1, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_food_poison1") == 0,
                       "food1 without powder must refuse");
        mep1_pass("oploc1_food1_need_powder");

        mep1_give(player, obj_toxin, 1);
        mep1_oploc_finish(srv, loc_food1, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_food_poison1") == 1,
                       "food1 must poison");
        mep1_pass("oploc1_food1_poison");

        mep1_oploc_finish(srv, loc_food1, loc_slot);
        mep1_pass("oploc1_food1_already");
    }

    if( loc_food2 >= 0 )
    {
        loc_slot = mep1_place_loc(srv, loc_food2, 2532, 3280, 0);
        mep1_clear_inv(player);
        mep1_set_bit(srv, "mourning_food_poison2", 0);
        mep1_oploc_finish(srv, loc_food2, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_food_poison2") == 0,
                       "food2 without powder must refuse");
        mep1_pass("oploc1_food2_need_powder");

        mep1_give(player, obj_toxin, 1);
        mep1_oploc_finish(srv, loc_food2, loc_slot);
        SELFTEST_CHECK(mep1_get_bit(player, "mourning_food_poison2") == 1,
                       "food2 must poison");
        mep1_pass("oploc1_food2_poison");
    }

    mep1_set_var(srv, "mourning_quest", MEP1_POISON_TASK);
    mep1_set_bit(srv, "mourning_food_poison1", 1);
    mep1_set_bit(srv, "mourning_food_poison2", 1);
    slot = mep1_spawn(srv, npc_essyllt, MEP1_ESSYLLT_X, MEP1_ESSYLLT_Z, 0);
    if( slot >= 0 )
    {
        mep1_talk_finish(srv, npc_essyllt, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_REPORT,
                       "Essyllt after poison must write report, got %d",
                       mep1_get_var(player, "mourning_quest"));
        mep1_pass("opnpc1_essyllt_after_poison");
        mep1_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,mend1_journal]", NULL, 0);
    mep1_finish(srv);
    mep1_pass("journal_report");

    /* ---- Arianwyn report + authored complete scroll ---- */
    thief_xp_before = (stat_thief >= 0) ? player->stat_xp_tenths[stat_thief] : 0;
    hp_xp_before = (stat_hitpoints >= 0) ? player->stat_xp_tenths[stat_hitpoints] : 0;
    mep1_clear_inv(player);
    slot = mep1_spawn(srv, npc_arianwyn, MEP1_ARIANWYN_X, MEP1_ARIANWYN_Z, 0);
    if( slot >= 0 )
    {
        mep1_talk_finish(srv, npc_arianwyn, slot);
        SELFTEST_CHECK(mep1_get_var(player, "mourning_quest") == MEP1_COMPLETE,
                       "Arianwyn report must complete MEP1, got %d",
                       mep1_get_var(player, "mourning_quest"));
        SELFTEST_CHECK(mep1_inv_total(player, obj_crystal) == 1,
                       "complete must grant elf teleport crystal");
        if( stat_thief >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_thief] >= thief_xp_before + MEP1_REWARD_THIEVING,
                           "complete must award 40000 Thieving XP");
        if( stat_hitpoints >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_hitpoints] >= hp_xp_before + MEP1_REWARD_HITPOINTS,
                           "complete must award 25000 Hitpoints XP");
        mep1_pass("opnpc1_arianwyn_report_complete");
        mep1_free_npc(srv, slot);
    }

    ToriRSServer_ScriptsRunProc(srv, "[proc,mend1_journal]", NULL, 0);
    mep1_finish(srv);
    mep1_pass("journal_complete");

    /* Fence spawned npcs / locs before returning to the host stanza. */
    selftest_reset_world(srv, player, 402, 402);

    fprintf(stderr, "ToriRSServer mep1 selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_MOURNINGSENDPARTI_SELFTEST_U_H */
