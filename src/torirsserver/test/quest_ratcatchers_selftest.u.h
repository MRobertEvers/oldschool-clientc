#ifndef TORIRSSERVER_TEST_QUEST_RATCATCHERS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_RATCATCHERS_SELFTEST_U_H

/* Ratcatchers Gate D C walk. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak.
 * Real OPNPC / OPLOC / OPLOCU / OPHELD / OPHELDU on the authored path.
 * player->godmode = 1 for the whole walk (no death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_RATCATCH_ONLY=1
 *
 * ::ratcatchersrun does not exist and is not used here.
 */

#define RATCATCH_NOT_STARTED 0
#define RATCATCH_SEWER_STARTED 5
#define RATCATCH_SEWER_CAUGHT_BASE 6
#define RATCATCH_SEWER_ALL 14
#define RATCATCH_SEWER_REPORTED 15
#define RATCATCH_JIMMY_TALKED 20
#define RATCATCH_JIMMY_DIRECTIONS 22
#define RATCATCH_MANSION_CATCHING 30
#define RATCATCH_MANSION_DONE 35
#define RATCATCH_JIMMY_DONE 40
#define RATCATCH_JACK_NEED_POISON 41
#define RATCATCH_JACK_POISONING 45
#define RATCATCH_JACK_HOLES_DONE 50
#define RATCATCH_JACK_AFTER_CHEESE 55
#define RATCATCH_APOTH_DONE 60
#define RATCATCH_KINGRAT 65
#define RATCATCH_JACK_AFTER_FIGHT 70
#define RATCATCH_JOE_TALKED 75
#define RATCATCH_JOE_SMOKED 80
#define RATCATCH_JOE_AGAIN 85
#define RATCATCH_FELKRASH 90
#define RATCATCH_FACE 95
#define RATCATCH_CHARM 100
#define RATCATCH_TUNE 105
#define RATCATCH_COMPLETE 127

#define RATCATCH_ICS_COMPLETE 26
#define RATCATCH_REWARD_THIEVING 45000
#define RATCATCH_SNAKE_COINS 101
#define RATCATCH_KINGRAT_FISH 8

#define RATCATCH_GERT_X 3151
#define RATCATCH_GERT_Z 3410
#define RATCATCH_PHING_X 3245
#define RATCATCH_PHING_Z 9868
#define RATCATCH_JIMMY_X 2562
#define RATCATCH_JIMMY_Z 3320
#define RATCATCH_JACK_X 3268
#define RATCATCH_JACK_Z 3400
#define RATCATCH_APOTH_X 3195
#define RATCATCH_APOTH_Z 3404
#define RATCATCH_JOE_X 2930
#define RATCATCH_JOE_Z 10213
#define RATCATCH_FACE_X 3019
#define RATCATCH_FACE_Z 3231
#define RATCATCH_FELK_X 2977
#define RATCATCH_FELK_Z 9639
#define RATCATCH_SARIM_X 3019
#define RATCATCH_SARIM_Z 3231
#define RATCATCH_CHARM_X 3354
#define RATCATCH_CHARM_Z 2953

#define RATCATCH_RAT1_X 2832
#define RATCATCH_RAT1_Z 5098
#define RATCATCH_RAT2_X 2861
#define RATCATCH_RAT2_Z 5093
#define RATCATCH_RAT3_X 2858
#define RATCATCH_RAT3_Z 5087
#define RATCATCH_RAT4_X 2863
#define RATCATCH_RAT4_Z 5101
#define RATCATCH_RAT5_X 2857
#define RATCATCH_RAT5_Z 5091
#define RATCATCH_RAT6_X 2863
#define RATCATCH_RAT6_Z 5086

#define RATCATCH_AMULET_SLOT 2

static void
ratcatch_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "RATCATCH PASS: %s\n", step);
}

static void
ratcatch_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ratcatch_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ratcatch_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 64 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static int
ratcatch_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ratcatch_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ratcatch_chatmenu();
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
ratcatch_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ratcatch_chatmenu();
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
ratcatch_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ratcatch_god(player);
    selftest_tick(srv);
}

static int
ratcatch_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    ratcatch_tele(srv, x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
    return slot;
}

static int
ratcatch_spawn_at(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(npc_type > 0);
    ratcatch_tele(srv, x > 0 ? x - 1 : x, z, level);
    slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, x, z, level);
    return slot;
}

static void
ratcatch_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static int
ratcatch_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
ratcatch_give(struct ToriRSServerPlayer* player, int obj_id, int count)
{
    int s;

    assert(player);
    assert(obj_id > 0);
    assert(count > 0);
    if( ratcatch_inv_total(player, obj_id) >= count )
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
ratcatch_set_bit(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ratcatch_get_bit(struct ToriRSServerPlayer* player, const char* name)
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
ratcatch_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
ratcatch_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ratcatch_talk(srv, npc_type, slot);
    ratcatch_finish(srv);
}

static void
ratcatch_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    ratcatch_talk(srv, npc_type, slot);
    ratcatch_click_until_menu(srv, 16);
    ratcatch_pick_row(srv, row);
    ratcatch_finish(srv);
}

static int
ratcatch_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    ratcatch_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
ratcatch_oploc_finish(struct ToriRSServer* srv, int loc_id, int loc_slot)
{
    assert(srv);
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_id, -1, loc_slot);
    ratcatch_finish(srv);
}

static void
ratcatch_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    ratcatch_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
ratcatch_opheld(struct ToriRSServer* srv, int trigger, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, trigger, obj_id, -1, -1);
    ratcatch_finish(srv);
}

static void
ratcatch_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    ratcatch_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
ratcatch_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_NOT_STARTED);
    ratcatch_set_bit(srv, "vc_raton_off1", 0);
    ratcatch_set_bit(srv, "vc_raton_off2", 0);
    ratcatch_set_bit(srv, "vc_raton_off3", 0);
    ratcatch_set_bit(srv, "vc_raton_off4", 0);
    ratcatch_set_bit(srv, "vc_raton_off5", 0);
    ratcatch_set_bit(srv, "vc_raton_off6", 0);
    ratcatch_set_bit(srv, "ratcatch_rathole_1", 0);
    ratcatch_set_bit(srv, "ratcatch_rathole_2", 0);
    ratcatch_set_bit(srv, "ratcatch_rathole_3", 0);
    ratcatch_set_bit(srv, "ratcatch_rathole_4", 0);
    ratcatch_set_bit(srv, "ratcatch_catknowsdrill", 0);
    ratcatch_set_bit(srv, "ics_little_var", 0);
    ratcatch_set_bit(srv, "giantdwarf_quest", 0);
}

static void
ratcatch_set_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    ratcatch_set_bit(srv, "ics_little_var", RATCATCH_ICS_COMPLETE);
    ratcatch_set_bit(srv, "giantdwarf_quest", 1);
}

static void
ratcatch_wear_amulet(struct ToriRSServerPlayer* player, int amulet)
{
    assert(player);
    assert(amulet > 0);
    worn_set(player, RATCATCH_AMULET_SLOT, amulet, 1);
}

static void
ratcatch_kit(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int kitten, int amulet)
{
    assert(srv);
    assert(player);
    ratcatch_clear_inv(player);
    ratcatch_reset_quest(srv);
    ratcatch_set_prereqs(srv);
    if( kitten > 0 )
        ratcatch_give(player, kitten, 1);
    if( amulet > 0 )
        ratcatch_wear_amulet(player, amulet);
    ratcatch_god(player);
}

static void
ratcatch_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,ratcatch_journal]", NULL, 0);
    ratcatch_finish(srv);
    ratcatch_pass(step);
}

static void
selftest_quest_ratcatchers(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_gert;
    int npc_phing;
    int npc_rat;
    int npc_jimmy;
    int npc_party;
    int npc_jack;
    int npc_apoth;
    int npc_joe;
    int npc_face;
    int npc_felk;
    int npc_charm;
    int obj_kitten;
    int obj_amulet;
    int obj_directions;
    int obj_poison;
    int obj_cheese;
    int obj_pcheese;
    int obj_vial;
    int obj_kwuarm;
    int obj_eggs;
    int obj_milk;
    int obj_dust;
    int obj_marrentill;
    int obj_antipoison;
    int obj_anchovy;
    int obj_weeds;
    int obj_pot;
    int obj_weedpot;
    int obj_smokey;
    int obj_tinder;
    int obj_coins;
    int obj_flute;
    int obj_music;
    int obj_pole;
    int loc_trellis;
    int loc_hole1;
    int loc_hole2;
    int loc_hole3;
    int loc_hole4;
    int loc_hole5;
    int loc_wall;
    int loc_bowl;
    int loc_manhole;
    int loc_ladder;
    int stat_thieving;
    int slot;
    int loc_slot;
    int i;
    int thieving_before;
    static const int k_journal_plateaus[] = {
        RATCATCH_NOT_STARTED,
        RATCATCH_SEWER_STARTED,
        RATCATCH_SEWER_CAUGHT_BASE,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        RATCATCH_SEWER_ALL,
        RATCATCH_SEWER_REPORTED,
        RATCATCH_JIMMY_TALKED,
        RATCATCH_JIMMY_DIRECTIONS,
        RATCATCH_MANSION_CATCHING,
        RATCATCH_MANSION_DONE,
        RATCATCH_JIMMY_DONE,
        RATCATCH_JACK_NEED_POISON,
        RATCATCH_JACK_POISONING,
        RATCATCH_JACK_HOLES_DONE,
        RATCATCH_JACK_AFTER_CHEESE,
        RATCATCH_APOTH_DONE,
        RATCATCH_KINGRAT,
        RATCATCH_JACK_AFTER_FIGHT,
        RATCATCH_JOE_TALKED,
        RATCATCH_JOE_SMOKED,
        RATCATCH_JOE_AGAIN,
        RATCATCH_FELKRASH,
        RATCATCH_FACE,
        RATCATCH_CHARM,
        RATCATCH_TUNE,
        RATCATCH_COMPLETE,
    };
    static const struct
    {
        int x;
        int z;
        int level;
        const char* off;
        const char* pass;
    } k_party[] = {
        { RATCATCH_RAT1_X, RATCATCH_RAT1_Z, 1, "vc_raton_off1", "opnpc1_mansion_rat_1" },
        { RATCATCH_RAT2_X, RATCATCH_RAT2_Z, 1, "vc_raton_off2", "opnpc1_mansion_rat_2" },
        { RATCATCH_RAT3_X, RATCATCH_RAT3_Z, 1, "vc_raton_off3", "opnpc1_mansion_rat_3" },
        { RATCATCH_RAT4_X, RATCATCH_RAT4_Z, 0, "vc_raton_off4", "opnpc1_mansion_rat_4" },
        { RATCATCH_RAT5_X, RATCATCH_RAT5_Z, 0, "vc_raton_off5", "opnpc1_mansion_rat_5" },
        { RATCATCH_RAT6_X, RATCATCH_RAT6_Z, 0, "vc_raton_off6", "opnpc1_mansion_rat_6" },
    };
    static const char* k_holes[] = {
        "ratcatchers_rathole1",
        "ratcatchers_rathole2",
        "ratcatchers_rathole3",
        "ratcatchers_rathole4",
    };
    static const char* k_hole_bits[] = {
        "ratcatch_rathole_1",
        "ratcatch_rathole_2",
        "ratcatch_rathole_3",
        "ratcatch_rathole_4",
    };

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: ratcatchers critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer ratcatchers selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    ratcatch_god(player);

    npc_gert = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gertrude_post");
    npc_phing = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vc_phingspet");
    npc_rat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rat");
    npc_jimmy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vc_jimmy_dazzler");
    npc_party = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vc_party_rat");
    npc_jack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vc_hooknosed_jack");
    npc_apoth = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "apothecary");
    npc_joe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vc_smokin_joe");
    npc_face = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vc_face");
    npc_felk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vc_felkrash_the_bard");
    npc_charm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "feud_snakecharmer");
    obj_kitten = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kittenobject");
    obj_amulet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ics_little_amulet_of_catspeak");
    obj_directions = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ratcatchers_party_directions");
    obj_poison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rat_poison");
    obj_cheese = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "cheese");
    obj_pcheese = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ratcatchers_poisonedcheese");
    obj_vial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vial_empty");
    obj_kwuarm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "kwuarm");
    obj_eggs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "red_spiders_eggs");
    obj_milk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bucket_milk");
    obj_dust = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unicorn_horn_dust");
    obj_marrentill = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "marentill");
    obj_antipoison = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ratcatchers_cat_antipoison");
    obj_anchovy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anchovies");
    obj_weeds = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "weeds");
    obj_pot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    obj_weedpot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ratcatchers_weedpot");
    obj_smokey = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ratcatchers_smokey_weedpot");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    obj_flute = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "snake_flute");
    obj_music = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ratcatchers_music");
    obj_pole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vc_rat_pole");
    loc_trellis = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vc_trellis_base");
    loc_hole1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ratcatchers_rathole1");
    loc_hole2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ratcatchers_rathole2");
    loc_hole3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ratcatchers_rathole3");
    loc_hole4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ratcatchers_rathole4");
    loc_hole5 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "ratcatchers_rathole5");
    loc_wall = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vc_blank_walldecor");
    loc_bowl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "feud_money_bowl");
    loc_manhole = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vc_manhole_open");
    loc_ladder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vc_ladder");
    stat_thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "ratcatch_var") >= 0,
                   "varbit ratcatch_var should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_ratcatchers") >= 0,
                   "dbrow quest_ratcatchers should resolve");
    SELFTEST_CHECK(npc_gert > 0, "npc gertrude_post should resolve");
    SELFTEST_CHECK(npc_phing > 0, "npc vc_phingspet should resolve");
    SELFTEST_CHECK(npc_jimmy > 0, "npc vc_jimmy_dazzler should resolve");
    SELFTEST_CHECK(npc_jack > 0, "npc vc_hooknosed_jack should resolve");
    SELFTEST_CHECK(npc_apoth > 0, "npc apothecary should resolve");
    SELFTEST_CHECK(npc_joe > 0, "npc vc_smokin_joe should resolve");
    SELFTEST_CHECK(npc_face > 0, "npc vc_face should resolve");
    SELFTEST_CHECK(npc_felk > 0, "npc vc_felkrash_the_bard should resolve");
    SELFTEST_CHECK(obj_kitten > 0, "obj kittenobject should resolve");
    SELFTEST_CHECK(obj_amulet > 0, "obj ics_little_amulet_of_catspeak should resolve");
    SELFTEST_CHECK(obj_pole > 0, "obj vc_rat_pole should resolve");
    if( npc_gert <= 0 || npc_phing <= 0 || obj_kitten <= 0 )
    {
        fprintf(stderr, "ToriRSServer ratcatchers selftest: %d checks, %d failures\n",
                (int)g_selftest_checks - checks_before,
                g_selftest_failures - fails_before);
        return;
    }

    ratcatch_clear_inv(player);
    ratcatch_reset_quest(srv);
    ratcatch_god(player);
    ratcatch_journal(srv, "journal_not_started");

    /* ---- Gertrude refuse-reqs / no-cat / no-amulet / refuse / accept / mid ---- */
    slot = ratcatch_spawn(srv, npc_gert, RATCATCH_GERT_X, RATCATCH_GERT_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Gertrude should spawn");
    if( slot >= 0 )
    {
        ratcatch_talk_finish(srv, npc_gert, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_NOT_STARTED,
                       "missing ICS + Giant Dwarf must not start Ratcatchers");
        ratcatch_pass("opnpc1_gertrude_refuse_reqs");

        ratcatch_set_prereqs(srv);
        ratcatch_talk_pick(srv, npc_gert, slot, 1);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_NOT_STARTED,
                       "accept without a cat must not start");
        ratcatch_pass("opnpc1_gertrude_no_cat");

        ratcatch_give(player, obj_kitten, 1);
        ratcatch_talk_pick(srv, npc_gert, slot, 1);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_NOT_STARTED,
                       "accept without catspeak equipped must not start");
        ratcatch_pass("opnpc1_gertrude_no_amulet");

        ratcatch_wear_amulet(player, obj_amulet);
        ratcatch_talk_pick(srv, npc_gert, slot, 2);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_NOT_STARTED,
                       "choice refuse must not start Ratcatchers");
        ratcatch_pass("opnpc1_gertrude_choice_refuse");

        ratcatch_talk_pick(srv, npc_gert, slot, 1);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_SEWER_STARTED,
                       "accept with cat + amulet must write sewer_started, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_gertrude_accept");

        ratcatch_talk_finish(srv, npc_gert, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_SEWER_STARTED,
                       "mid Gertrude must stay on sewer_started");
        ratcatch_pass("opnpc1_gertrude_mid");
    }

    /* ---- Phingspet start / mid / 8-rat report + each sewer catch ---- */
    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_phing, RATCATCH_PHING_X, RATCATCH_PHING_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Phingspet should spawn");
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_NOT_STARTED);
        ratcatch_talk_finish(srv, npc_phing, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_NOT_STARTED,
                       "Phingspet too-early must not start the sewer");
        ratcatch_pass("opnpc1_phingspet_too_early");

        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_SEWER_STARTED);
        ratcatch_talk_finish(srv, npc_phing, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_SEWER_CAUGHT_BASE,
                       "Phingspet start must write caught_base, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_phingspet_start");
    }

    if( npc_rat > 0 )
    {
        int rat_slot = ratcatch_spawn(srv, npc_rat, RATCATCH_PHING_X + 2, RATCATCH_PHING_Z, 0);

        SELFTEST_CHECK(rat_slot >= 0, "sewer rat should spawn");
        if( rat_slot >= 0 )
        {
            for( i = 0; i < 8; i++ )
            {
                int before = ratcatch_get_bit(player, "ratcatch_var");

                ratcatch_talk_finish(srv, npc_rat, rat_slot);
                SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == before + 1,
                               "sewer catch %d must increment ratcatch_var, got %d",
                               i + 1,
                               ratcatch_get_bit(player, "ratcatch_var"));
                ratcatch_pass(i == 0   ? "opnpc1_sewer_catch_1"
                              : i == 1 ? "opnpc1_sewer_catch_2"
                              : i == 2 ? "opnpc1_sewer_catch_3"
                              : i == 3 ? "opnpc1_sewer_catch_4"
                              : i == 4 ? "opnpc1_sewer_catch_5"
                              : i == 5 ? "opnpc1_sewer_catch_6"
                              : i == 6 ? "opnpc1_sewer_catch_7"
                                       : "opnpc1_sewer_catch_8");
            }
            SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_SEWER_ALL,
                           "eight sewer catches must reach all_caught");
            ratcatch_free_npc(srv, rat_slot);
        }
    }

    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", 8);
        ratcatch_talk_finish(srv, npc_phing, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == 8,
                       "Phingspet mid must not skip the remaining rats");
        ratcatch_pass("opnpc1_phingspet_mid");

        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_SEWER_ALL);
        ratcatch_talk_finish(srv, npc_phing, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_SEWER_REPORTED,
                       "Phingspet 8-rat report must write sewer_reported, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_phingspet_report");
    }

    /* ---- Jimmy too-early / talk / directions / mansion ---- */
    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_jimmy, RATCATCH_JIMMY_X, RATCATCH_JIMMY_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Jimmy should spawn");
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_SEWER_STARTED);
        ratcatch_talk_finish(srv, npc_jimmy, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_SEWER_STARTED,
                       "Jimmy too-early must not hand out directions");
        ratcatch_pass("opnpc1_jimmy_too_early");

        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_SEWER_REPORTED);
        ratcatch_clear_inv(player);
        ratcatch_give(player, obj_kitten, 1);
        ratcatch_wear_amulet(player, obj_amulet);
        ratcatch_talk_finish(srv, npc_jimmy, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JIMMY_TALKED,
                       "Jimmy talk must write jimmy_talked, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_directions) == 1,
                       "Jimmy must hand party directions");
        ratcatch_pass("opnpc1_jimmy_talk");

        ratcatch_opheld(srv, SS_TRIGGER_OPHELD1, obj_directions);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JIMMY_DIRECTIONS,
                       "reading directions must write jimmy_directions, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opheld1_jimmy_directions");
    }

    if( loc_trellis >= 0 )
    {
        loc_slot = ratcatch_place_loc(srv, loc_trellis, RATCATCH_JIMMY_X + 4, RATCATCH_JIMMY_Z, 0);
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JIMMY_TALKED);
        ratcatch_oploc_finish(srv, loc_trellis, loc_slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JIMMY_TALKED,
                       "trellis before directions must not start mansion catching");
        ratcatch_pass("oploc1_trellis_too_early");

        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JIMMY_DIRECTIONS);
        ratcatch_oploc_finish(srv, loc_trellis, loc_slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_MANSION_CATCHING,
                       "trellis after directions must write mansion_catching, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("oploc1_trellis_climb");
    }

    if( npc_party > 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_MANSION_CATCHING);
        for( i = 0; i < 6; i++ )
        {
            int party = ratcatch_spawn_at(
                srv, npc_party, k_party[i].x, k_party[i].z, k_party[i].level);

            SELFTEST_CHECK(party >= 0, "mansion party rat %d should spawn", i + 1);
            if( party >= 0 )
            {
                ratcatch_talk_finish(srv, npc_party, party);
                SELFTEST_CHECK(ratcatch_get_bit(player, k_party[i].off) == 1,
                               "mansion rat %d must flip %s", i + 1, k_party[i].off);
                ratcatch_pass(k_party[i].pass);
                ratcatch_free_npc(srv, party);
            }
        }
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_MANSION_DONE,
                       "six mansion rats must write mansion_done, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
    }

    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_MANSION_DONE);
        ratcatch_talk_finish(srv, npc_jimmy, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JIMMY_DONE,
                       "Jimmy after mansion must write jimmy_done, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_jimmy_mansion_done");
    }

    /* ---- Jack poison / cheese / holes ---- */
    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_jack, RATCATCH_JACK_X, RATCATCH_JACK_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Hooknosed Jack should spawn");
    if( slot >= 0 )
    {
        ratcatch_kit(srv, player, obj_kitten, obj_amulet);
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JIMMY_DONE);
        ratcatch_talk_finish(srv, npc_jack, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JACK_NEED_POISON,
                       "Jack without ingredients must write need_poison, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_jack_need_poison");

        ratcatch_give(player, obj_vial, 1);
        ratcatch_give(player, obj_kwuarm, 1);
        ratcatch_give(player, obj_eggs, 1);
        ratcatch_talk_finish(srv, npc_jack, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JACK_POISONING,
                       "Jack mix must write poisoning_holes, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_poison) == 1,
                       "Jack mix must hand rat poison");
        ratcatch_pass("opnpc1_jack_mix_poison");

        ratcatch_kit(srv, player, obj_kitten, obj_amulet);
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JIMMY_DONE);
        ratcatch_give(player, obj_poison, 1);
        ratcatch_talk_finish(srv, npc_jack, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JACK_POISONING,
                       "Jack with poison already must write poisoning_holes");
        ratcatch_pass("opnpc1_jack_has_poison");

        ratcatch_give(player, obj_cheese, 1);
        ratcatch_opheldu(srv, obj_poison, obj_cheese);
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_pcheese) == 0,
                       "fewer than four cheeses must not poison");
        ratcatch_pass("opheldu_cheese_need_four");

        ratcatch_give(player, obj_cheese, 4);
        ratcatch_opheldu(srv, obj_poison, obj_cheese);
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_pcheese) == 4,
                       "poison + four cheeses must make poisoned cheese");
        ratcatch_pass("opheldu_cheese_poison");
    }

    if( loc_hole1 >= 0 && loc_hole2 >= 0 && loc_hole3 >= 0 && loc_hole4 >= 0 &&
        obj_pcheese > 0 )
    {
        int hole_ids[4];

        hole_ids[0] = loc_hole1;
        hole_ids[1] = loc_hole2;
        hole_ids[2] = loc_hole3;
        hole_ids[3] = loc_hole4;
        (void)k_holes;
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JACK_POISONING);
        for( i = 0; i < 4; i++ )
        {
            loc_slot = ratcatch_place_loc(
                srv, hole_ids[i], RATCATCH_JACK_X + 2 + i, RATCATCH_JACK_Z, 1);
            ratcatch_use_loc(srv, hole_ids[i], loc_slot, obj_pcheese);
            SELFTEST_CHECK(ratcatch_get_bit(player, k_hole_bits[i]) == 1,
                           "hole %d must set %s", i + 1, k_hole_bits[i]);
            ratcatch_pass(i == 0   ? "oplocu_hole_1"
                          : i == 1 ? "oplocu_hole_2"
                          : i == 2 ? "oplocu_hole_3"
                                   : "oplocu_hole_4");
        }
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JACK_HOLES_DONE,
                       "four holes must write holes_done, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
    }

    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JACK_HOLES_DONE);
        ratcatch_talk_pick(srv, npc_jack, slot, 1);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JACK_HOLES_DONE,
                       "Jack choice 'something else' must not advance");
        ratcatch_pass("opnpc1_jack_holes_choice_else");

        ratcatch_talk_pick(srv, npc_jack, slot, 2);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JACK_AFTER_CHEESE,
                       "Jack quest choice must write after_cheese, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_jack_holes_done");
    }

    /* ---- Apothecary missing / brew ---- */
    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_apoth, RATCATCH_APOTH_X, RATCATCH_APOTH_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Apothecary should spawn");
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JACK_AFTER_CHEESE);
        ratcatch_talk_finish(srv, npc_apoth, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JACK_AFTER_CHEESE,
                       "Apoth without ingredients must not brew");
        ratcatch_pass("opnpc1_apoth_missing");

        ratcatch_give(player, obj_milk, 1);
        ratcatch_give(player, obj_dust, 1);
        ratcatch_give(player, obj_marrentill, 1);
        ratcatch_talk_finish(srv, npc_apoth, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_APOTH_DONE,
                       "Apoth brew must write apoth_done, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_antipoison) == 1,
                       "Apoth brew must hand cat antipoison");
        ratcatch_pass("opnpc1_apoth_brew");
    }

    /* ---- King Rat leftover + Jack after ---- */
    if( loc_wall >= 0 )
    {
        loc_slot = ratcatch_place_loc(srv, loc_wall, RATCATCH_JACK_X + 3, RATCATCH_JACK_Z, 1);
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_APOTH_DONE);
        ratcatch_give(player, obj_kitten, 1);
        ratcatch_give(player, obj_antipoison, 1);
        ratcatch_give(player, obj_anchovy, RATCATCH_KINGRAT_FISH);
        ratcatch_use_loc(srv, loc_wall, loc_slot, obj_kitten);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_KINGRAT,
                       "King Rat leftover oplocu must write kingrat_defeated, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("oplocu_kingrat_leftover");
    }

    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_jack, RATCATCH_JACK_X, RATCATCH_JACK_Z, 0);
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_KINGRAT);
        ratcatch_talk_finish(srv, npc_jack, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JACK_AFTER_FIGHT,
                       "Jack after King Rat must write after_fight, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_jack_after_fight");
    }

    /* ---- Joe / weedpot / smoke ---- */
    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_joe, RATCATCH_JOE_X, RATCATCH_JOE_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Smokin' Joe should spawn");
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JACK_AFTER_FIGHT);
        ratcatch_talk_finish(srv, npc_joe, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JOE_TALKED,
                       "Joe talk must write joe_talked, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_joe_talk");

        ratcatch_give(player, obj_weeds, 1);
        ratcatch_give(player, obj_pot, 1);
        ratcatch_opheldu(srv, obj_pot, obj_weeds);
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_weedpot) == 1,
                       "weeds on empty pot must make a weedpot");
        ratcatch_pass("opheldu_weedpot_make");

        ratcatch_give(player, obj_tinder, 1);
        ratcatch_opheldu(srv, obj_weedpot, obj_tinder);
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_smokey) == 1,
                       "tinderbox on weedpot must light it");
        ratcatch_pass("opheldu_weedpot_light");
    }

    if( loc_hole5 >= 0 && obj_smokey > 0 )
    {
        loc_slot = ratcatch_place_loc(srv, loc_hole5, RATCATCH_JOE_X + 3, RATCATCH_JOE_Z, 0);
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JOE_TALKED);
        ratcatch_set_bit(srv, "ratcatch_catknowsdrill", 0);
        ratcatch_use_loc(srv, loc_hole5, loc_slot, obj_smokey);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_catknowsdrill") == 1,
                       "first smoke must teach the drill");
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JOE_TALKED,
                       "first smoke must not clear the hole");
        ratcatch_pass("oplocu_joe_smoke_fail");

        ratcatch_use_loc(srv, loc_hole5, loc_slot, obj_smokey);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JOE_SMOKED,
                       "second smoke must write joe_smoked, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("oplocu_joe_smoke_ok");
    }

    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JOE_SMOKED);
        ratcatch_talk_finish(srv, npc_joe, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JOE_AGAIN,
                       "Joe again must write joe_again, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_joe_again");
    }

    /* ---- Face / Felkrash ---- */
    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_face, RATCATCH_FACE_X, RATCATCH_FACE_Z, 0);
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JOE_AGAIN);
        ratcatch_talk_finish(srv, npc_face, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_JOE_AGAIN,
                       "Face before Felkrash must only point downstairs");
        ratcatch_pass("opnpc1_face_talk");
    }

    if( loc_manhole >= 0 )
    {
        loc_slot = ratcatch_place_loc(srv, loc_manhole, RATCATCH_FACE_X, RATCATCH_FACE_Z, 0);
        ratcatch_oploc_finish(srv, loc_manhole, loc_slot);
        ratcatch_pass("oploc1_sarim_manhole");
    }

    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_felk, RATCATCH_FELK_X, RATCATCH_FELK_Z, 0);
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_JOE_AGAIN);
        ratcatch_talk_finish(srv, npc_felk, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_FELKRASH,
                       "Felkrash talk must write felkrash_talked, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_felkrash_talk");
    }

    if( loc_ladder >= 0 )
    {
        loc_slot = ratcatch_place_loc(srv, loc_ladder, RATCATCH_FELK_X, RATCATCH_FELK_Z, 0);
        ratcatch_oploc_finish(srv, loc_ladder, loc_slot);
        ratcatch_pass("oploc1_sarim_ladder");
    }

    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_face, RATCATCH_FACE_X, RATCATCH_FACE_Z, 0);
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_FELKRASH);
        ratcatch_talk_pick(srv, npc_face, slot, 2);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_FACE,
                       "Face after Felkrash must write face_talked, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        ratcatch_pass("opnpc1_face_after_felkrash");
    }

    /* ---- Snake charmer pay fail + ok + flute leftover ---- */
    if( loc_bowl >= 0 && obj_coins > 0 )
    {
        int charm_slot = -1;

        loc_slot = ratcatch_place_loc(srv, loc_bowl, RATCATCH_CHARM_X, RATCATCH_CHARM_Z, 0);
        if( npc_charm > 0 )
            charm_slot = ratcatch_spawn_at(
                srv, npc_charm, RATCATCH_CHARM_X, RATCATCH_CHARM_Z, 0);
        SELFTEST_CHECK(charm_slot >= 0, "feud_snakecharmer should spawn at the money bowl");
        (void)charm_slot;
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_FACE);
        ratcatch_clear_inv(player);
        ratcatch_give(player, obj_kitten, 1);
        ratcatch_wear_amulet(player, obj_amulet);
        ratcatch_give(player, obj_coins, 50);
        ratcatch_use_loc(srv, loc_bowl, loc_slot, obj_coins);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_FACE,
                       "fewer than 101 coins must not buy the charm");
        ratcatch_pass("oplocu_charm_pay_fail");

        ratcatch_give(player, obj_coins, RATCATCH_SNAKE_COINS);
        ratcatch_use_loc(srv, loc_bowl, loc_slot, obj_coins);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_CHARM,
                       "101 coins must write charm_obtained, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_flute) == 1,
                       "pay-ok must hand snake_flute");
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_music) == 1,
                       "pay-ok must hand music scroll");
        ratcatch_pass("oplocu_charm_pay_ok");
    }

    ratcatch_tele(srv, RATCATCH_SARIM_X, RATCATCH_SARIM_Z, 0);
    ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_CHARM);
    ratcatch_give(player, obj_flute, 1);
    ratcatch_give(player, obj_music, 1);
    ratcatch_opheld(srv, SS_TRIGGER_OPHELD1, obj_flute);
    SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_TUNE,
                   "flute leftover at Port Sarim must write tune_played, got %d",
                   ratcatch_get_bit(player, "ratcatch_var"));
    ratcatch_pass("opheld1_flute_leftover");

    /* ---- Authored complete scroll via Felkrash ---- */
    ratcatch_free_npc(srv, slot);
    slot = ratcatch_spawn(srv, npc_felk, RATCATCH_FELK_X, RATCATCH_FELK_Z, 0);
    if( slot >= 0 )
    {
        ratcatch_set_bit(srv, "ratcatch_var", RATCATCH_TUNE);
        thieving_before = 0;
        if( stat_thieving >= 0 )
            thieving_before = player->stat_xp_tenths[stat_thieving];
        ratcatch_talk_finish(srv, npc_felk, slot);
        SELFTEST_CHECK(ratcatch_get_bit(player, "ratcatch_var") == RATCATCH_COMPLETE,
                       "Felkrash complete must write 127, got %d",
                       ratcatch_get_bit(player, "ratcatch_var"));
        SELFTEST_CHECK(ratcatch_inv_total(player, obj_pole) == 1,
                       "complete must hand the rat pole");
        if( stat_thieving >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_thieving] >=
                               thieving_before + RATCATCH_REWARD_THIEVING,
                           "complete must advance thieving by 45000 tenths");
        ratcatch_pass("opnpc1_felkrash_complete_scroll");
    }

    for( i = 0; i < (int)(sizeof(k_journal_plateaus) / sizeof(k_journal_plateaus[0])); i++ )
    {
        char label[64];

        ratcatch_set_bit(srv, "ratcatch_var", k_journal_plateaus[i]);
        snprintf(label, sizeof(label), "journal_plateau_%d", k_journal_plateaus[i]);
        ratcatch_journal(srv, label);
    }

    ratcatch_free_npc(srv, slot);
    fprintf(stderr, "ToriRSServer ratcatchers selftest: %d checks, %d failures\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_RATCATCHERS_SELFTEST_U_H */
