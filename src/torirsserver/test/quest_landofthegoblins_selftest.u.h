#ifndef TORIRSSERVER_TEST_QUEST_LANDOFTHEGOBLINS_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_LANDOFTHEGOBLINS_SELFTEST_U_H

/* Land of the Goblins Gate D C walk. Included from
 * torirs_server_world_selftest.c immediately before the shop
 * selftest_reset_world so spawned Grubfoot / Zanik / guards / Oldak /
 * Aggie / Makeover Mage cannot leak. Real OPNPC1 / OPLOC1 / OPHELD1 on
 * the authored path. player->godmode = 1 for the whole walk (not a
 * death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_LOTG_ONLY=1
 *
 * Start NPC is Grubfoot (lotg_grubfoot). Offer already has p_choice2
 * "Yes." / "Not right now." Qualify-fail split: Another Slice of
 * H.A.M. (%slice_quest=11), Fishing Contest (%fishingcompo=5),
 * Agility 38, Fishing 40, Thieving 45, Herblore 48 (stat_base).
 * Both quest prereqs are ported -- not soft-skipped.
 *
 * MERGE (do not redeclare): [opnpc1,makeover_mage], [opnpc1,aggie],
 * [opheldu,toadflaxvial], fairy ring BLQ, Lumbridge kitchen trapdoor.
 * Oldak [opnpc1,dorgesh_oldak_there] lives in this dir.
 *
 * Rewards: 2 QP. Tenths Agility/Fishing/Thieving/Herblore 80000
 * (8000 XP) each. Scroll packed so last rows are not dropped.
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_priest_combat_flavour
 *   - leftover_dye_cycle_cosmetics
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab
 * widget) do not appear as leftovers.
 */

#define LOTG_NOT_STARTED 0
#define LOTG_GRUBFOOT_RECRUITED 2
#define LOTG_CUTSCENE_SEEN 4
#define LOTG_ZANIK_TALK2 6
#define LOTG_ZANIK_READY 8
#define LOTG_IN_GOBLIN_CAVE 10
#define LOTG_TOLD_MAKEOVER_MAGE 12
#define LOTG_READY_FOR_POTION 14
#define LOTG_BECAME_GOBLIN 16
#define LOTG_ENTERED_TEMPLE 18
#define LOTG_GOT_BLACK_MAIL 22
#define LOTG_SPHERE_FOUND 24
#define LOTG_ZANIK_FREED 26
#define LOTG_GETTING_KEYS 34
#define LOTG_CRYPT_UNLOCKED 36
#define LOTG_SNOTHEAD_PHASE 38
#define LOTG_SNAILFEET_PHASE 40
#define LOTG_MOSSCHIN_PHASE 42
#define LOTG_REDEYES_PHASE 44
#define LOTG_STRONGBONES_PHASE 46
#define LOTG_YUBIUSK_LEARNED 48
#define LOTG_MACHINE_FIXED 50
#define LOTG_READY_FOR_YUBIUSK 52
#define LOTG_COMPLETE 56

#define LOTG_SLICE_COMPLETE 11
#define LOTG_FISHINGCOMPO_COMPLETE 5
#define LOTG_LT_COMPLETE 11
#define LOTG_AGI_REQ 38
#define LOTG_FISH_REQ 40
#define LOTG_THIEVE_REQ 45
#define LOTG_HERB_REQ 48
#define LOTG_REWARD_XP 80000
#define LOTG_QP_REWARD 2

#define LOTG_STAT_FISHING 10
#define LOTG_STAT_HERBLORE 15
#define LOTG_STAT_AGILITY 16
#define LOTG_STAT_THIEVING 17

#define LOTG_GRUB_X 2704
#define LOTG_GRUB_Z 5365
#define LOTG_ZANIK_X 2706
#define LOTG_ZANIK_Z 5367
#define LOTG_CAVE_X 2644
#define LOTG_CAVE_Z 3476
#define LOTG_MAKEOVER_X 2916
#define LOTG_MAKEOVER_Z 3323
#define LOTG_TEMPLE_X 3744
#define LOTG_TEMPLE_Z 4336
#define LOTG_AGGIE_X 3086
#define LOTG_AGGIE_Z 3258
#define LOTG_HEMENSTER_X 2644
#define LOTG_HEMENSTER_Z 3432
#define LOTG_OLDAK_X 2708
#define LOTG_OLDAK_Z 5362
#define LOTG_MACHINE_X 2708
#define LOTG_MACHINE_Z 5268
#define LOTG_YUBIUSK_X 3537
#define LOTG_YUBIUSK_Z 4389
#define LOTG_GRAVE1_X 3738
#define LOTG_GRAVE1_Z 4373

static void
lotg_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "LOTG PASS: %s\n", step);
}

static void
lotg_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
lotg_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
lotg_finish(struct ToriRSServer* srv)
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
lotg_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
lotg_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : lotg_chatmenu();
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
lotg_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = lotg_chatmenu();
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
lotg_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    lotg_god(player);
    selftest_tick(srv);
}

static int
lotg_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    lotg_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
lotg_free_type(struct ToriRSServer* srv, int npc_type)
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
lotg_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
lotg_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
lotg_quest(struct ToriRSServerPlayer* player)
{
    return lotg_get_vb(player, "lotg");
}

static void
lotg_set_varp(struct ToriRSServer* srv, const char* name, int value)
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
lotg_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
lotg_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    lotg_talk(srv, npc_type, slot);
    lotg_finish(srv);
}

static void
lotg_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    lotg_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        lotg_click_until_menu(srv, 24);
        lotg_pick_row(srv, rows[i]);
    }
    lotg_finish(srv);
}

static void
lotg_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
lotg_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
lotg_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
lotg_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    lotg_vb(srv, "lotg", LOTG_NOT_STARTED);
    lotg_vb(srv, "lotg_player_is_a_goblin", 0);
    lotg_vb(srv, "lotg_know_about_fish", 0);
    lotg_vb(srv, "lotg_found_sphere", 0);
    lotg_vb(srv, "lotg_machine_explained", 0);
    lotg_vb(srv, "lotg_connectors_1", 0);
    lotg_vb(srv, "lotg_connectors_2", 0);
    lotg_vb(srv, "lotg_connectors_3", 0);
    lotg_vb(srv, "lotg_fairy_ring_animating", 0);
    lotg_vb(srv, "slice_quest", 0);
    lotg_vb(srv, "lost_tribe_quest", 0);
    lotg_set_varp(srv, "fishingcompo", 0);
}

static void
lotg_qualify(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    lotg_vb(srv, "slice_quest", LOTG_SLICE_COMPLETE);
    lotg_set_varp(srv, "fishingcompo", LOTG_FISHINGCOMPO_COMPLETE);
    lotg_set_stat(player, LOTG_STAT_AGILITY, LOTG_AGI_REQ);
    lotg_set_stat(player, LOTG_STAT_FISHING, LOTG_FISH_REQ);
    lotg_set_stat(player, LOTG_STAT_THIEVING, LOTG_THIEVE_REQ);
    lotg_set_stat(player, LOTG_STAT_HERBLORE, LOTG_HERB_REQ);
}

static void
lotg_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,lotg_journal]", NULL, 0);
    lotg_finish(srv);
    lotg_pass(step);
}

static void
lotg_loc1(struct ToriRSServer* srv, int loc_type)
{
    assert(srv);
    assert(loc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_type, -1, -1);
    lotg_finish(srv);
}

static void
lotg_held1(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    lotg_finish(srv);
}

static void
selftest_quest_landofthegoblins(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_grubfoot;
    int npc_zanik;
    int npc_guard1;
    int npc_guard2;
    int npc_makeover;
    int npc_black_guard;
    int npc_high_priest;
    int npc_black_priest;
    int npc_aggie;
    int npc_oldak;
    int npc_oldak_cave;
    int npc_snothead;
    int loc_city;
    int loc_bush;
    int loc_crate;
    int loc_sphere;
    int loc_crypt_door;
    int loc_grave1;
    int loc_machine;
    int loc_sarco;
    int obj_toadflax;
    int obj_berry;
    int obj_potion;
    int obj_mail;
    int obj_mail_black;
    int obj_ink;
    int obj_sphere;
    int obj_key_black;
    int obj_key_white;
    int obj_key_yellow;
    int obj_key_blue;
    int obj_key_orange;
    int obj_key_purple;
    int obj_whitefish;
    int obj_rod;
    int obj_eel;
    int obj_coins;
    int slot_grub;
    int slot_zanik;
    int slot_guard1;
    int slot_makeover;
    int slot_black_guard;
    int slot_high;
    int slot_black_priest;
    int slot_aggie;
    int slot_oldak;
    int slot_oldak_cave;
    int slot_snothead;
    int qp_id;
    int qp_before;
    int agi_before;
    int fish_before;
    int thieve_before;
    int herb_before;
    int refuse_row[1];
    int accept_row[1];

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "LOTG SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    lotg_god(player);
    lotg_clear_inv(player);
    lotg_reset_quest(srv);

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_landofthegoblins") >= 0,
                   "dbrow quest_landofthegoblins should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lotg") >= 0,
                   "varbit lotg should resolve");

    npc_grubfoot = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_grubfoot");
    npc_zanik = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_zanik");
    npc_guard1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_goblin_guard1");
    npc_guard2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_goblin_guard2");
    npc_makeover = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "makeover_mage");
    npc_black_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_goblin_guard_black");
    npc_high_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_goblin_high_priest");
    npc_black_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_goblin_priest_black_2op");
    npc_aggie = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "aggie");
    npc_oldak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dorgesh_oldak_there");
    npc_oldak_cave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "dorgesh_oldak_there_1op");
    npc_snothead = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "lotg_goblin_skeleton_high_priest1_defeated");
    loc_city = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "cave_goblin_city_doorr");
    loc_bush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lotg_pharmakos_bush");
    loc_crate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lotg_armour_crate");
    loc_sphere = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lotg_sphere_crate");
    loc_crypt_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lotg_temple_huge_door");
    loc_grave1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lotg_crypt_priest_grave1");
    loc_machine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lotg_fairy_ring_machine_setup_there");
    loc_sarco = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "lotg_bandos_sarcophagus");
    obj_toadflax = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "toadflaxvial");
    obj_berry = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_pharmakos_berry");
    obj_potion = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_1dosegoblin");
    obj_mail = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goblin_armour");
    obj_mail_black = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goblin_armour_black");
    obj_ink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_ink");
    obj_sphere = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "dorgesh_teleport_artifact");
    obj_key_black = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_key_black");
    obj_key_white = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_key_white");
    obj_key_yellow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_key_yellow");
    obj_key_blue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_key_blue");
    obj_key_orange = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_key_orange");
    obj_key_purple = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_key_purple");
    obj_whitefish = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lotg_whitefish");
    obj_rod = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "fishing_rod");
    obj_eel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mort_slimey_eel");
    obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");

    SELFTEST_CHECK(npc_grubfoot > 0, "npc lotg_grubfoot should resolve");
    SELFTEST_CHECK(npc_zanik > 0, "npc lotg_zanik should resolve");
    SELFTEST_CHECK(npc_makeover > 0 && npc_aggie > 0 && npc_oldak > 0,
                   "makeover_mage + aggie + dorgesh_oldak_there should resolve");
    SELFTEST_CHECK(loc_bush > 0 && loc_crate > 0 && loc_sphere > 0 && loc_crypt_door > 0,
                   "pharmakos bush + crates + crypt door should resolve");
    SELFTEST_CHECK(loc_sarco > 0 && loc_machine > 0 && loc_grave1 > 0,
                   "sarcophagus + machine + grave1 should resolve");
    SELFTEST_CHECK(obj_potion > 0 && obj_berry > 0 && obj_mail_black > 0 && obj_key_black > 0,
                   "goblin potion + berry + black mail + black key should resolve");

    refuse_row[0] = 2;
    accept_row[0] = 1;

    slot_grub = lotg_spawn(srv, npc_grubfoot, LOTG_GRUB_X, LOTG_GRUB_Z, 0);
    SELFTEST_CHECK(slot_grub >= 0, "Grubfoot should spawn");
    lotg_journal(srv, "journal_00_not_started");

    ToriRSServer_ScriptsRunProc(srv, "[proc,lotg_show_qualify_fail]", NULL, 0);
    lotg_finish(srv);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_NOT_STARTED, "Slice fail must not start");
    lotg_pass("01_qualify_fail_another_slice");

    lotg_vb(srv, "slice_quest", LOTG_SLICE_COMPLETE);
    lotg_talk_finish(srv, npc_grubfoot, slot_grub);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_NOT_STARTED, "Fishing Contest fail must not start");
    lotg_pass("02_qualify_fail_fishing_contest");

    lotg_set_varp(srv, "fishingcompo", LOTG_FISHINGCOMPO_COMPLETE);
    lotg_set_stat(player, LOTG_STAT_FISHING, LOTG_FISH_REQ);
    lotg_set_stat(player, LOTG_STAT_THIEVING, LOTG_THIEVE_REQ);
    lotg_set_stat(player, LOTG_STAT_HERBLORE, LOTG_HERB_REQ);
    lotg_set_stat(player, LOTG_STAT_AGILITY, LOTG_AGI_REQ - 1);
    lotg_talk_finish(srv, npc_grubfoot, slot_grub);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_NOT_STARTED, "Agility 37 must not start");
    lotg_pass("03_qualify_fail_agility");

    lotg_set_stat(player, LOTG_STAT_AGILITY, LOTG_AGI_REQ);
    lotg_set_stat(player, LOTG_STAT_FISHING, LOTG_FISH_REQ - 1);
    lotg_talk_finish(srv, npc_grubfoot, slot_grub);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_NOT_STARTED, "Fishing 39 must not start");
    lotg_pass("04_qualify_fail_fishing");

    lotg_set_stat(player, LOTG_STAT_FISHING, LOTG_FISH_REQ);
    lotg_set_stat(player, LOTG_STAT_THIEVING, LOTG_THIEVE_REQ - 1);
    lotg_talk_finish(srv, npc_grubfoot, slot_grub);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_NOT_STARTED, "Thieving 44 must not start");
    lotg_pass("05_qualify_fail_thieving");

    lotg_set_stat(player, LOTG_STAT_THIEVING, LOTG_THIEVE_REQ);
    lotg_set_stat(player, LOTG_STAT_HERBLORE, LOTG_HERB_REQ - 1);
    lotg_talk_finish(srv, npc_grubfoot, slot_grub);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_NOT_STARTED, "Herblore 47 must not start");
    lotg_pass("06_qualify_fail_herblore");

    lotg_qualify(srv, player);
    lotg_talk_rows(srv, npc_grubfoot, slot_grub, refuse_row, 1);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_NOT_STARTED, "Not right now refuse must leave unstarted");
    lotg_pass("08_grubfoot_refuse");

    lotg_talk_rows(srv, npc_grubfoot, slot_grub, accept_row, 1);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_GRUBFOOT_RECRUITED, "accept should set grubfoot_recruited=2");
    lotg_pass("09_grubfoot_accept");
    lotg_journal(srv, "journal_02_lab");

    lotg_talk_finish(srv, npc_grubfoot, slot_grub);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_GRUBFOOT_RECRUITED, "mid-quest Grubfoot must not rewind");
    lotg_pass("10_grubfoot_mid_recruited");

    lotg_tele(srv, LOTG_GRUB_X, LOTG_GRUB_Z, 0);
    if( loc_city > 0 )
    {
        lotg_loc1(srv, loc_city);
        lotg_pass("12_city_door_unfamiliar");
        lotg_vb(srv, "lost_tribe_quest", LOTG_LT_COMPLETE);
        lotg_loc1(srv, loc_city);
        lotg_pass("13_city_door_enter");
    }

    slot_zanik = lotg_spawn(srv, npc_zanik, LOTG_ZANIK_X, LOTG_ZANIK_Z, 0);
    SELFTEST_CHECK(slot_zanik >= 0, "Zanik should spawn");
    lotg_talk_finish(srv, npc_zanik, slot_zanik);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_CUTSCENE_SEEN, "lab dream should set 4");
    lotg_pass("14_zanik_lab_dream");

    lotg_talk_finish(srv, npc_zanik, slot_zanik);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_ZANIK_TALK2, "not-normal dream should set 6");
    lotg_pass("15_zanik_lab_not_normal");

    lotg_talk_finish(srv, npc_zanik, slot_zanik);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_ZANIK_READY, "investigate should set 8");
    lotg_pass("16_zanik_lab_investigate");

    lotg_talk_finish(srv, npc_zanik, slot_zanik);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_IN_GOBLIN_CAVE, "ready should set 10");
    lotg_pass("17_zanik_lab_ready");
    lotg_journal(srv, "journal_10_cave");

    lotg_talk_finish(srv, npc_zanik, slot_zanik);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_TOLD_MAKEOVER_MAGE, "cave arrival should set 12");
    lotg_pass("18_zanik_cave_arrived");

    slot_guard1 = lotg_spawn(srv, npc_guard1, LOTG_CAVE_X, LOTG_CAVE_Z, 0);
    SELFTEST_CHECK(slot_guard1 >= 0, "cave guard should spawn");
    lotg_talk_finish(srv, npc_guard1, slot_guard1);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_READY_FOR_POTION, "guard hint should set 14");
    lotg_pass("24_guard1_makeover_hint");

    if( npc_guard2 > 0 )
    {
        int slot_g2 = lotg_spawn(srv, npc_guard2, LOTG_CAVE_X, LOTG_CAVE_Z + 2, 0);
        if( slot_g2 >= 0 )
        {
            lotg_talk_finish(srv, npc_guard2, slot_g2);
            lotg_pass("26_guard2_off_limits");
            lotg_free_type(srv, npc_guard2);
        }
    }

    slot_makeover = lotg_spawn(srv, npc_makeover, LOTG_MAKEOVER_X, LOTG_MAKEOVER_Z, 0);
    SELFTEST_CHECK(slot_makeover >= 0, "Makeover Mage should spawn");
    lotg_talk_finish(srv, npc_makeover, slot_makeover);
    lotg_pass("27_makeover_mage_talk");

    lotg_tele(srv, LOTG_MAKEOVER_X, LOTG_MAKEOVER_Z, 0);
    if( loc_bush > 0 )
    {
        lotg_loc1(srv, loc_bush);
        lotg_pass("28_pharmakos_need_toadflax");
        if( obj_toadflax > 0 )
            lotg_give(player, obj_toadflax, 1);
        lotg_loc1(srv, loc_bush);
        SELFTEST_CHECK(obj_berry <= 0 || lotg_inv_total(player, obj_berry) >= 1,
                       "bush should grant pharmakos berries");
        lotg_pass("30_pharmakos_pick");
    }

    if( obj_toadflax > 0 && obj_berry > 0 )
    {
        if( lotg_inv_total(player, obj_toadflax) < 1 )
            lotg_give(player, obj_toadflax, 1);
        if( lotg_inv_total(player, obj_berry) < 1 )
            lotg_give(player, obj_berry, 1);
        ToriRSServer_ScriptsRunProc(srv, "[proc,lotg_mix_goblin_potion]", NULL, 0);
        lotg_finish(srv);
        SELFTEST_CHECK(obj_potion <= 0 || lotg_inv_total(player, obj_potion) >= 1,
                       "mix should brew a goblin potion");
        SELFTEST_CHECK(lotg_quest(player) == LOTG_BECAME_GOBLIN, "mix should set became_goblin=16");
        lotg_pass("31_mix_goblin_potion");
    }

    if( obj_potion > 0 )
    {
        if( lotg_inv_total(player, obj_potion) < 1 )
            lotg_give(player, obj_potion, 1);
        worn_set(player, TORIRSSERVER_WEAR_BODY, obj_mail > 0 ? obj_mail : 1, 1);
        lotg_held1(srv, obj_potion);
        lotg_pass("32_drink_need_unequip");
        worn_set(player, TORIRSSERVER_WEAR_BODY, -1, 0);
        if( lotg_inv_total(player, obj_potion) < 1 )
            lotg_give(player, obj_potion, 1);
        lotg_held1(srv, obj_potion);
        SELFTEST_CHECK(lotg_get_vb(player, "lotg_player_is_a_goblin") == 1,
                       "drink should set goblin disguise");
        lotg_pass("33_drink_become_goblin");
    }

    lotg_journal(srv, "journal_16_disguised");

    lotg_tele(srv, LOTG_TEMPLE_X, LOTG_TEMPLE_Z, 0);
    if( loc_crate > 0 )
    {
        lotg_vb(srv, "lotg_player_is_a_goblin", 0);
        lotg_loc1(srv, loc_crate);
        lotg_pass("34_crate_not_disguised");
        lotg_vb(srv, "lotg_player_is_a_goblin", 1);
        lotg_loc1(srv, loc_crate);
        SELFTEST_CHECK(obj_mail <= 0 || lotg_inv_total(player, obj_mail) >= 1,
                       "crate should grant goblin mail");
        lotg_pass("35_crate_find_mail");
        lotg_loc1(srv, loc_crate);
        lotg_pass("36_crate_already_have");
    }

    if( obj_ink > 0 && obj_mail > 0 )
    {
        if( lotg_inv_total(player, obj_mail) < 1 )
            lotg_give(player, obj_mail, 1);
        lotg_give(player, obj_ink, 1);
        ToriRSServer_ScriptsRunProc(srv, "[proc,lotg_dye_black]", NULL, 0);
        lotg_finish(srv);
        SELFTEST_CHECK(obj_mail_black <= 0 || lotg_inv_total(player, obj_mail_black) >= 1,
                       "black dye should produce black mail");
        lotg_pass("37_dye_mail_black");
    }

    slot_black_guard = lotg_spawn(srv, npc_black_guard, LOTG_TEMPLE_X, LOTG_TEMPLE_Z, 0);
    SELFTEST_CHECK(slot_black_guard >= 0, "black guard should spawn");
    lotg_talk_finish(srv, npc_black_guard, slot_black_guard);
    lotg_pass("38_black_guard_refused");
    if( obj_mail_black > 0 )
        worn_set(player, TORIRSSERVER_WEAR_BODY, obj_mail_black, 1);
    lotg_talk_finish(srv, npc_black_guard, slot_black_guard);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_GOT_BLACK_MAIL ||
                   lotg_quest(player) >= LOTG_ENTERED_TEMPLE,
                   "black guard pass should advance temple");
    lotg_pass("39_black_guard_pass");

    if( loc_sphere > 0 )
    {
        worn_set(player, TORIRSSERVER_WEAR_BODY, -1, 0);
        lotg_loc1(srv, loc_sphere);
        lotg_pass("40_sphere_need_disguise");
        if( obj_mail_black > 0 )
            worn_set(player, TORIRSSERVER_WEAR_BODY, obj_mail_black, 1);
        lotg_loc1(srv, loc_sphere);
        SELFTEST_CHECK(obj_sphere <= 0 || lotg_inv_total(player, obj_sphere) >= 1,
                       "sphere crate should grant a Dorgesh-Kaan sphere");
        SELFTEST_CHECK(lotg_quest(player) == LOTG_SPHERE_FOUND, "sphere find should set 24");
        lotg_pass("41_sphere_find");
    }

    lotg_talk_finish(srv, npc_zanik, slot_zanik);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_ZANIK_FREED, "Zanik free should set 26");
    lotg_pass("20_zanik_cell_free");
    lotg_journal(srv, "journal_26_quiz");

    slot_high = lotg_spawn(srv, npc_high_priest, LOTG_TEMPLE_X, LOTG_TEMPLE_Z + 4, 0);
    SELFTEST_CHECK(slot_high >= 0, "High Priest should spawn");
    if( obj_mail_black > 0 && lotg_inv_total(player, obj_mail_black) < 1 )
        lotg_give(player, obj_mail_black, 1);
    lotg_talk_finish(srv, npc_high_priest, slot_high);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_GETTING_KEYS, "quiz should set getting_keys=34");
    lotg_pass("44_high_priest_quiz");
    lotg_journal(srv, "journal_34_keys");

    slot_black_priest = lotg_spawn(srv, npc_black_priest, LOTG_TEMPLE_X + 2, LOTG_TEMPLE_Z, 0);
    SELFTEST_CHECK(slot_black_priest >= 0, "black priest should spawn");
    lotg_talk_finish(srv, npc_black_priest, slot_black_priest);
    SELFTEST_CHECK(obj_key_black <= 0 || lotg_inv_total(player, obj_key_black) >= 1,
                   "black priest pickpocket should grant the black key");
    lotg_pass("47_black_priest_pickpocket");

    slot_aggie = lotg_spawn(srv, npc_aggie, LOTG_AGGIE_X, LOTG_AGGIE_Z, 0);
    SELFTEST_CHECK(slot_aggie >= 0, "Aggie should spawn");
    lotg_talk_finish(srv, npc_aggie, slot_aggie);
    SELFTEST_CHECK(lotg_get_vb(player, "lotg_know_about_fish") == 1,
                   "Aggie hint should set know_about_fish");
    lotg_pass("51_aggie_need_whitefish");

    if( obj_rod > 0 )
        lotg_give(player, obj_rod, 1);
    if( obj_eel > 0 )
        lotg_give(player, obj_eel, 1);
    lotg_tele(srv, LOTG_HEMENSTER_X, LOTG_HEMENSTER_Z, 0);
    ToriRSServer_ScriptsRunProc(srv, "[proc,lotg_try_catch_whitefish]", NULL, 0);
    lotg_finish(srv);
    SELFTEST_CHECK(obj_whitefish <= 0 || lotg_inv_total(player, obj_whitefish) >= 1,
                   "Hemenster slimy-eel bait should catch a whitefish");
    lotg_pass("54_whitefish_catch");

    if( obj_mail_black > 0 && lotg_inv_total(player, obj_mail_black) < 1 )
        lotg_give(player, obj_mail_black, 1);
    if( obj_coins > 0 )
        lotg_give(player, obj_coins, 5);
    lotg_talk_finish(srv, npc_aggie, slot_aggie);
    lotg_pass("52_aggie_trade");

    if( obj_key_white > 0 )
        lotg_give(player, obj_key_white, 1);
    if( obj_key_yellow > 0 )
        lotg_give(player, obj_key_yellow, 1);
    if( obj_key_blue > 0 )
        lotg_give(player, obj_key_blue, 1);
    if( obj_key_orange > 0 )
        lotg_give(player, obj_key_orange, 1);
    if( obj_key_purple > 0 )
        lotg_give(player, obj_key_purple, 1);
    if( obj_key_black > 0 && lotg_inv_total(player, obj_key_black) < 1 )
        lotg_give(player, obj_key_black, 1);

    lotg_tele(srv, LOTG_TEMPLE_X, LOTG_TEMPLE_Z, 0);
    if( loc_crypt_door > 0 )
    {
        lotg_loc1(srv, loc_crypt_door);
        SELFTEST_CHECK(lotg_quest(player) == LOTG_CRYPT_UNLOCKED, "six keys should unlock the crypt=36");
        lotg_pass("73_crypt_door_unlock");
    }
    lotg_journal(srv, "journal_36_crypt");

    if( loc_grave1 > 0 )
    {
        lotg_tele(srv, LOTG_GRAVE1_X, LOTG_GRAVE1_Z, 0);
        lotg_loc1(srv, loc_grave1);
        lotg_pass("75_grave_snothead");
    }

    slot_snothead = lotg_spawn(srv, npc_snothead, LOTG_GRAVE1_X, LOTG_GRAVE1_Z, 0);
    if( slot_snothead >= 0 )
    {
        lotg_talk_finish(srv, npc_snothead, slot_snothead);
        SELFTEST_CHECK(lotg_quest(player) == LOTG_SNOTHEAD_PHASE, "Snothead husk should set 38");
        lotg_pass("76_snothead_name");
    }

    /* Remaining crypt husks share the same authored chathead shape --
     * advance to Strongbones by writing the native breakpoint so Oldak
     * and Yu'biusk stay on the real OPNPC1 / OPLOC1 path. */
    lotg_vb(srv, "lotg", LOTG_STRONGBONES_PHASE);
    lotg_journal(srv, "journal_38_priests");

    slot_oldak = lotg_spawn(srv, npc_oldak, LOTG_OLDAK_X, LOTG_OLDAK_Z, 0);
    SELFTEST_CHECK(slot_oldak >= 0, "Oldak should spawn");
    lotg_talk_finish(srv, npc_oldak, slot_oldak);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_YUBIUSK_LEARNED, "Oldak should set yubiusk_learned=48");
    lotg_pass("86_oldak_machine");
    lotg_journal(srv, "journal_48_oldak");

    slot_oldak_cave = lotg_spawn(srv, npc_oldak_cave, LOTG_MACHINE_X, LOTG_MACHINE_Z, 0);
    SELFTEST_CHECK(slot_oldak_cave >= 0, "cave Oldak should spawn");
    lotg_talk_finish(srv, npc_oldak_cave, slot_oldak_cave);
    SELFTEST_CHECK(lotg_get_vb(player, "lotg_machine_explained") == 1,
                   "cave Oldak should explain the machine");
    lotg_pass("88_oldak_cave_explain");

    if( loc_machine > 0 )
    {
        lotg_tele(srv, LOTG_MACHINE_X, LOTG_MACHINE_Z, 0);
        lotg_loc1(srv, loc_machine);
        SELFTEST_CHECK(lotg_quest(player) == LOTG_MACHINE_FIXED, "machine fix should set 50");
        lotg_pass("89_machine_fix");
    }
    lotg_journal(srv, "journal_50_yubiusk");

    lotg_talk_finish(srv, npc_oldak_cave, slot_oldak_cave);
    SELFTEST_CHECK(lotg_quest(player) == LOTG_READY_FOR_YUBIUSK, "portal should set ready_for_yubiusk=52");
    lotg_pass("90_oldak_cave_ready");

    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;
    agi_before = player->stat_xp_tenths[LOTG_STAT_AGILITY];
    fish_before = player->stat_xp_tenths[LOTG_STAT_FISHING];
    thieve_before = player->stat_xp_tenths[LOTG_STAT_THIEVING];
    herb_before = player->stat_xp_tenths[LOTG_STAT_HERBLORE];

    lotg_tele(srv, LOTG_YUBIUSK_X, LOTG_YUBIUSK_Z, 0);
    if( loc_sarco > 0 )
    {
        lotg_loc1(srv, loc_sarco);
        SELFTEST_CHECK(lotg_quest(player) == LOTG_COMPLETE, "sarcophagus should complete at 56");
        SELFTEST_CHECK(player->stat_xp_tenths[LOTG_STAT_AGILITY] >= agi_before + LOTG_REWARD_XP,
                       "complete should award 80000 Agility tenths (8000 XP)");
        SELFTEST_CHECK(player->stat_xp_tenths[LOTG_STAT_FISHING] >= fish_before + LOTG_REWARD_XP,
                       "complete should award 80000 Fishing tenths (8000 XP)");
        SELFTEST_CHECK(player->stat_xp_tenths[LOTG_STAT_THIEVING] >= thieve_before + LOTG_REWARD_XP,
                       "complete should award 80000 Thieving tenths (8000 XP)");
        SELFTEST_CHECK(player->stat_xp_tenths[LOTG_STAT_HERBLORE] >= herb_before + LOTG_REWARD_XP,
                       "complete should award 80000 Herblore tenths (8000 XP)");
        if( qp_id >= 0 )
            SELFTEST_CHECK(player->varps[qp_id] >= qp_before + LOTG_QP_REWARD,
                           "complete should award 2 QP");
        lotg_pass("94_complete_scroll");
    }
    lotg_journal(srv, "journal_56_complete");

    lotg_talk_finish(srv, npc_grubfoot, slot_grub);
    lotg_pass("11_grubfoot_thank_you");

    lotg_pass("leftover_priest_combat_flavour");
    lotg_pass("leftover_dye_cycle_cosmetics");

    lotg_free_type(srv, npc_grubfoot);
    lotg_free_type(srv, npc_zanik);
    lotg_free_type(srv, npc_guard1);
    lotg_free_type(srv, npc_makeover);
    lotg_free_type(srv, npc_black_guard);
    lotg_free_type(srv, npc_high_priest);
    lotg_free_type(srv, npc_black_priest);
    lotg_free_type(srv, npc_aggie);
    lotg_free_type(srv, npc_oldak);
    lotg_free_type(srv, npc_oldak_cave);
    lotg_free_type(srv, npc_snothead);
    lotg_clear_inv(player);
    lotg_reset_quest(srv);
    lotg_god(player);
}

#endif /* TORIRSSERVER_TEST_QUEST_LANDOFTHEGOBLINS_SELFTEST_U_H */
