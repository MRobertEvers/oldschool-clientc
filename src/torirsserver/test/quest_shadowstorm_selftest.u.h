#ifndef TORIRSSERVER_TEST_QUEST_SHADOWSTORM_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_SHADOWSTORM_SELFTEST_U_H

/* Shadow of the Storm Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Reen / Badden / Dave / Denath / Jennifer /
 * Matthew / Agrith-Naar / golem / kiln locs cannot leak. Real OPNPC1 /
 * OPLOC1 / OPHELDU / OPNPCU on the authored path. player->godmode = 1 for
 * the whole walk (Agrith-Naar is not a player-death test). Completion goes
 * through ~sots_try_complete -> ~quest_complete_rewards(quest_shadowofthestorm)
 * plus stat_advance(hitpoints, 10000). Additive SotS only -- do not rewrite
 * The Golem, Demon Slayer, or Desert Treasure II Elissa.
 *
 * ::shadowstorm / ::shadowstormdye / ::shadowstormritual are not used.
 * Prereqs are set as genuine varp/varbit writes (Demon Slayer complete +
 * The Golem complete). Silverlight is given as an inventory item, the same
 * way a finished Demon Slayer player would carry it.
 *
 * Gate: TORIRSSERVER_SELFTEST_SOTS_ONLY=1
 *
 * Disclosed leftovers (not silently skipped):
 *   - per-player incantation permutation (fixed wiki-transcript answer)
 *   - Agrith-Naar Telekinetic Grab pull (no tree precedent)
 *   - lamp is HP XP, no rub-to-choose UI (tree-wide thosf_reward_lamp gap)
 *   - sigil smelt is soft opheldu (jewellery IF deferred)
 */

#define SOTS_NOT_STARTED 0
#define SOTS_SEE_BADDEN 10
#define SOTS_INFILTRATE 20
#define SOTS_DENATH 30
#define SOTS_SIGIL_TASKS 40
#define SOTS_MATTHEW 50
#define SOTS_GOLEM_ASK 60
#define SOTS_RITUAL 70
#define SOTS_RITUAL_DONE 90
#define SOTS_RECRUIT 100
#define SOTS_SUMMON 110
#define SOTS_FIGHT 120
#define SOTS_UNEQUIP 124
#define SOTS_COMPLETE 125

#define SOTS_DEMON_COMPLETE 30
#define SOTS_GOLEM_COMPLETE 10
#define SOTS_AGRITH_REVIVE_HP 12
#define SOTS_LAMP_XP 10000

#define SOTS_REEN_X 3271
#define SOTS_REEN_Z 3158
#define SOTS_BADDEN_X 3493
#define SOTS_BADDEN_Z 3090
#define SOTS_MUSHROOM_X 3495
#define SOTS_MUSHROOM_Z 3088
#define SOTS_STAIRS_X 3493
#define SOTS_STAIRS_Z 3090
#define SOTS_DAVE_X 2721
#define SOTS_DAVE_Z 4911
#define SOTS_THRONE_X 2720
#define SOTS_THRONE_Z 4912
#define SOTS_THRONE_LEVEL 2
#define SOTS_JENNIFER_X 2723
#define SOTS_JENNIFER_Z 4901
#define SOTS_MATTHEW_X 2727
#define SOTS_MATTHEW_Z 4897
#define SOTS_GOLEM_X 3490
#define SOTS_GOLEM_Z 3088
#define SOTS_KILN_X 3496
#define SOTS_KILN_Z 3092

static void
sots_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "SOTS PASS: %s\n", step);
}

static void
sots_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
sots_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
sots_finish(struct ToriRSServer* srv)
{
    int t;

    assert(srv);
    assert(srv->active_player);
    for( t = 0; t < 160 && srv->active_player->active_script; t++ )
    {
        selftest_click_through(srv, 8);
        selftest_tick(srv);
    }
    if( srv->active_player->active_script )
        ToriRSServer_WorldCloseModal(srv);
}

static void
sots_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    sots_god(player);
    selftest_tick(srv);
}

static int
sots_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    sots_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
sots_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
sots_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
sots_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
sots_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
sots_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
sots_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = sots_chatmenu();
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
sots_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = sots_chatmenu();
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
sots_talk(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    assert(npc_type > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
sots_talk_drain(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    sots_talk(srv, npc_type, slot);
    sots_finish(srv);
}

static void
sots_talk_pick(struct ToriRSServer* srv, int npc_type, int slot, int row)
{
    assert(srv);
    sots_talk(srv, npc_type, slot);
    sots_click_until_menu(srv, 24);
    sots_pick_row(srv, row);
    sots_finish(srv);
}

static void
sots_talk_pick2(
    struct ToriRSServer* srv,
    int npc_type,
    int slot,
    int row1,
    int row2)
{
    assert(srv);
    sots_talk(srv, npc_type, slot);
    sots_click_until_menu(srv, 24);
    sots_pick_row(srv, row1);
    sots_click_until_menu(srv, 24);
    sots_pick_row(srv, row2);
    sots_finish(srv);
}

static void
sots_oploc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;
    int placed;

    assert(srv);
    assert(loc_id > 0);
    sots_tele(srv, x, z, level);
    placed = ToriRSServer_WorldLocSet(
        srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    (void)placed;
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    if( slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_OPLOC1, loc_id, ToriRSServer_LocCategory(loc_id), slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_id, -1, -1);
    sots_finish(srv);
}

static void
sots_opheld(struct ToriRSServer* srv, int obj_id)
{
    assert(srv);
    assert(obj_id > 0);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    sots_finish(srv);
}

static int
sots_find_inv_slot(const struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            return s;
    }
    return -1;
}

static void
sots_opheldu(struct ToriRSServer* srv, int obj_type, int use_obj_type)
{
    struct ToriRSServerPlayer* player;
    int slot_a;
    int slot_b;

    assert(srv);
    assert(obj_type > 0);
    assert(use_obj_type > 0);
    player = srv->active_player;
    assert(player);
    slot_a = sots_find_inv_slot(player, obj_type);
    slot_b = sots_find_inv_slot(player, use_obj_type);
    /* ScriptsRunOpheldu reads last_item/last_useitem off the player; the
     * type args only pick the lookup rung (see seaslug torch chain). */
    player->last_item = obj_type;
    player->last_slot = slot_a;
    player->last_useitem = use_obj_type;
    player->last_useslot = slot_b;
    ToriRSServer_ScriptsRunOpheldu(srv, obj_type, -1, use_obj_type, -1);
    sots_finish(srv);
    player->last_item = -1;
    player->last_useitem = -1;
    player->last_slot = -1;
    player->last_useslot = -1;
}

static void
sots_opnpcu(struct ToriRSServer* srv, int npc_type, int slot, int use_obj)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = use_obj;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_type, -1, slot);
    sots_finish(srv);
    player->last_useitem = -1;
}

static void
sots_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
    }
}

static void
sots_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,shadowstorm_journal]", NULL, 0);
    sots_finish(srv);
    sots_pass(step);
}

static void
sots_set_prereqs(struct ToriRSServer* srv)
{
    assert(srv);
    sots_varp(srv, "demonstart", SOTS_DEMON_COMPLETE);
    sots_vb(srv, "golem_a", SOTS_GOLEM_COMPLETE);
}

static void
sots_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    sots_vb(srv, "agrith_quest", SOTS_NOT_STARTED);
    sots_vb(srv, "agrith_badden_uzer", 0);
    sots_vb(srv, "agrith_reen_uzer", 0);
    sots_vb(srv, "agrith_convinced_dave", 0);
    sots_vb(srv, "agrith_convinced_golem", 0);
    sots_vb(srv, "agrith_kiln", 0);
    sots_vb(srv, "golem_throne_gems", 0);
}

static void
selftest_quest_shadowstorm(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int checks_before;
    int fails_before;
    int npc_reen;
    int npc_badden;
    int npc_dave;
    int npc_denath;
    int npc_jennifer;
    int npc_matthew;
    int npc_golem;
    int npc_agrith;
    int npc_tanya;
    int npc_ghost;
    int obj_silverlight;
    int obj_dyed;
    int obj_darklight;
    int obj_mushroom;
    int obj_ink;
    int obj_mould;
    int obj_sigil;
    int obj_book;
    int obj_bar;
    int obj_key;
    int obj_shirt;
    int obj_robe;
    int obj_shirt_d;
    int obj_robe_d;
    int obj_sapphire;
    int obj_ruby;
    int obj_emerald;
    int obj_bronze;
    int loc_mush;
    int loc_stairs;
    int loc_stairs_up;
    int loc_portal;
    int loc_portal_out;
    int loc_kiln1;
    int loc_kiln2;
    int loc_kiln3;
    int loc_kiln4;
    int loc_rubble;
    int dbrow;
    int slot;
    int agrith_slot;
    int kiln_correct;
    int kiln_wrong;
    int loc_wrong;
    int loc_right;
    int hp_xp_before;
    int quest;

    assert(srv);
    assert(player);

    checks_before = (int)g_selftest_checks;
    fails_before = g_selftest_failures;
    fprintf(stderr, "ToriRSServer selftest: ::shadowstorm / Shadow of the Storm\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    player->godmode = 1;
    srv->members_world = 1;
    sots_god(player);

    npc_reen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_reen_alkharid");
    if( npc_reen <= 0 )
        npc_reen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_reen");
    npc_badden = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_badden_uzer");
    if( npc_badden <= 0 )
        npc_badden = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_badden");
    npc_dave = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_dave");
    npc_denath = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_denath");
    npc_jennifer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_jennifer");
    npc_matthew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_matthew");
    npc_golem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "golem_golem");
    npc_agrith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_naar");
    npc_tanya = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_tanya_falling");
    npc_ghost = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "agrith_noncombat_ghost");
    obj_silverlight = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silverlight");
    obj_dyed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "agrith_silverlight_dyed");
    obj_darklight = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "darklight");
    obj_mushroom = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_mushroom");
    obj_ink = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_ink");
    obj_mould = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "agrith_sigil_mould");
    obj_sigil = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "agrith_sigil");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "agrith_book");
    obj_bar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silver_bar");
    obj_key = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "golem_golemkey");
    obj_shirt = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "desert_shirt");
    obj_robe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "desert_robe");
    obj_shirt_d = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "agrith_desert_shirt_dyed");
    obj_robe_d = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "agrith_desert_robe_dyed");
    obj_sapphire = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sapphire");
    obj_ruby = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ruby");
    obj_emerald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "emerald");
    obj_bronze = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_sword");
    loc_mush = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_black_mushrooms");
    loc_stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_insidestairs_top");
    loc_stairs_up = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_insidestairs_base");
    loc_portal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "golem_portal");
    loc_portal_out = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "agrith_portal_closing");
    loc_kiln1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "agrith_kiln_1");
    loc_kiln2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "agrith_kiln_2");
    loc_kiln3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "agrith_kiln_3");
    loc_kiln4 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "agrith_kiln_4");
    loc_rubble = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "agrith_wizard_rubble");
    dbrow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_shadowofthestorm");

    SELFTEST_CHECK(npc_reen > 0, "agrith_reen should resolve");
    SELFTEST_CHECK(npc_badden > 0, "agrith_badden should resolve");
    SELFTEST_CHECK(npc_dave > 0, "agrith_dave should resolve");
    SELFTEST_CHECK(npc_denath > 0, "agrith_denath should resolve");
    SELFTEST_CHECK(npc_jennifer > 0, "agrith_jennifer should resolve");
    SELFTEST_CHECK(npc_matthew > 0, "agrith_matthew should resolve");
    SELFTEST_CHECK(npc_golem > 0, "golem_golem should resolve");
    SELFTEST_CHECK(npc_agrith > 0, "agrith_naar should resolve");
    SELFTEST_CHECK(obj_silverlight > 0, "silverlight should resolve");
    SELFTEST_CHECK(obj_dyed > 0, "agrith_silverlight_dyed should resolve");
    SELFTEST_CHECK(obj_darklight > 0, "darklight should resolve");
    SELFTEST_CHECK(obj_mushroom > 0, "golem_mushroom should resolve");
    SELFTEST_CHECK(obj_mould > 0, "agrith_sigil_mould should resolve");
    SELFTEST_CHECK(obj_sigil > 0, "agrith_sigil should resolve");
    SELFTEST_CHECK(obj_book > 0, "agrith_book should resolve");
    SELFTEST_CHECK(dbrow > 0, "dbrow quest_shadowofthestorm should resolve");
    SELFTEST_CHECK(loc_kiln1 > 0, "agrith_kiln_1 should resolve");
    SELFTEST_CHECK(loc_kiln2 > 0, "agrith_kiln_2 should resolve");
    SELFTEST_CHECK(loc_kiln3 > 0, "agrith_kiln_3 should resolve");
    SELFTEST_CHECK(loc_kiln4 > 0, "agrith_kiln_4 should resolve");

    sots_clear_inv(player);
    sots_reset_quest(srv);
    sots_varp(srv, "demonstart", 0);
    sots_vb(srv, "golem_a", 0);
    sots_journal(srv, "journal_not_started");

    /* ---- Reen refuse-reqs (neither prereq) ---- */
    slot = sots_spawn(srv, npc_reen, SOTS_REEN_X, SOTS_REEN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Father Reen should spawn");
    sots_talk_drain(srv, npc_reen, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_NOT_STARTED,
                   "Reen must refuse without Demon Slayer + The Golem");
    sots_pass("reen_refuse_reqs");

    /* DS only still refuses. */
    sots_varp(srv, "demonstart", SOTS_DEMON_COMPLETE);
    sots_talk_drain(srv, npc_reen, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_NOT_STARTED,
                   "Reen must refuse with only Demon Slayer complete");
    sots_pass("reen_refuse_golem_missing");

    /* Golem only still refuses. */
    sots_varp(srv, "demonstart", 0);
    sots_vb(srv, "golem_a", SOTS_GOLEM_COMPLETE);
    sots_talk_drain(srv, npc_reen, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_NOT_STARTED,
                   "Reen must refuse with only The Golem complete");
    sots_pass("reen_refuse_ds_missing");

    /* ---- Reen offer + accept (genuine prereqs + Silverlight) ---- */
    sots_set_prereqs(srv);
    sots_give(player, obj_silverlight, 1);
    sots_talk_drain(srv, npc_reen, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_SEE_BADDEN,
                   "Reen accept must set see-Badden, got %d",
                   sots_get_vb(player, "agrith_quest"));
    SELFTEST_CHECK(selftest_count_obj(player, obj_silverlight) >= 1,
                   "Silverlight stays after Reen start");
    sots_pass("reen_offer_accept");
    sots_journal(srv, "journal_see_badden");

    /* Reen mid reminder. */
    sots_talk_drain(srv, npc_reen, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_SEE_BADDEN,
                   "Reen mid must not skip Badden");
    sots_pass("reen_mid");

    /* Lost Silverlight replacement. */
    sots_clear_inv(player);
    sots_talk_drain(srv, npc_reen, slot);
    SELFTEST_CHECK(selftest_count_obj(player, obj_silverlight) >= 1,
                   "Reen must replace lost Silverlight");
    sots_pass("reen_replace_silverlight");

    /* Reen grants Silverlight when the player has none on first start. */
    sots_clear_inv(player);
    sots_reset_quest(srv);
    sots_set_prereqs(srv);
    sots_talk_drain(srv, npc_reen, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_SEE_BADDEN,
                   "Reen start without sword must still accept");
    SELFTEST_CHECK(selftest_count_obj(player, obj_silverlight) >= 1,
                   "Reen must give Silverlight when the player has none");
    sots_pass("reen_offer_give_silverlight");
    sots_free_npc(srv, slot);

    /* ---- Badden too-early / choice refuse / no sword / accept ---- */
    slot = sots_spawn(srv, npc_badden, SOTS_BADDEN_X, SOTS_BADDEN_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Father Badden should spawn");
    sots_reset_quest(srv);
    sots_set_prereqs(srv);
    sots_talk_drain(srv, npc_badden, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_NOT_STARTED,
                   "Badden before Reen must not start the quest");
    sots_pass("badden_too_early");

    sots_vb(srv, "agrith_quest", SOTS_SEE_BADDEN);
    sots_vb(srv, "agrith_badden_uzer", 1);
    sots_give(player, obj_silverlight, 1);
    sots_talk_pick(srv, npc_badden, slot, 2);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_SEE_BADDEN,
                   "Badden 'just passing through' must not infiltrate");
    sots_pass("badden_choice_refuse");

    sots_clear_inv(player);
    sots_talk_pick(srv, npc_badden, slot, 1);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_SEE_BADDEN,
                   "Badden without Silverlight must not infiltrate");
    sots_pass("badden_no_silverlight");

    sots_give(player, obj_silverlight, 1);
    sots_talk_pick2(srv, npc_badden, slot, 1, 3);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_INFILTRATE,
                   "Badden 'never take part' still briefs infiltrate, got %d",
                   sots_get_vb(player, "agrith_quest"));
    sots_pass("badden_choice_never");

    sots_vb(srv, "agrith_quest", SOTS_SEE_BADDEN);
    sots_talk_pick2(srv, npc_badden, slot, 1, 1);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_INFILTRATE,
                   "Badden 'tell me more' must infiltrate");
    sots_pass("badden_choice_more");

    sots_vb(srv, "agrith_quest", SOTS_SEE_BADDEN);
    sots_talk_pick2(srv, npc_badden, slot, 1, 2);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_INFILTRATE,
                   "Badden accept must set infiltrate");
    sots_pass("badden_infiltrate_accept");
    sots_journal(srv, "journal_infiltrate");

    sots_talk_drain(srv, npc_badden, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_INFILTRATE,
                   "Badden mid-infiltrate must not skip dye");
    sots_pass("badden_mid_infiltrate");
    sots_free_npc(srv, slot);

    /* ---- Dye refuse before infiltrate ---- */
    sots_vb(srv, "agrith_quest", SOTS_SEE_BADDEN);
    sots_clear_inv(player);
    sots_give(player, obj_silverlight, 1);
    sots_give(player, obj_mushroom, 1);
    sots_opheldu(srv, obj_silverlight, obj_mushroom);
    SELFTEST_CHECK(selftest_count_obj(player, obj_dyed) == 0,
                   "dye must refuse before infiltrate");
    sots_pass("dye_too_early");

    /* Pick mushrooms. */
    sots_vb(srv, "agrith_quest", SOTS_INFILTRATE);
    sots_clear_inv(player);
    if( loc_mush > 0 )
    {
        sots_oploc(srv, loc_mush, SOTS_MUSHROOM_X, SOTS_MUSHROOM_Z, 0);
        SELFTEST_CHECK(selftest_count_obj(player, obj_mushroom) >= 1,
                       "picking black mushrooms must grant golem_mushroom");
        sots_pass("pick_black_mushrooms");
    }

    /* Dye missing sword / missing dye / success. */
    sots_clear_inv(player);
    sots_give(player, obj_mushroom, 1);
    sots_opheldu(srv, obj_silverlight, obj_mushroom);
    SELFTEST_CHECK(selftest_count_obj(player, obj_dyed) == 0,
                   "dye without Silverlight must refuse");
    sots_pass("dye_need_silverlight");

    sots_clear_inv(player);
    sots_give(player, obj_silverlight, 1);
    sots_opheldu(srv, obj_silverlight, obj_mushroom);
    SELFTEST_CHECK(selftest_count_obj(player, obj_dyed) == 0,
                   "dye without mushrooms/ink must refuse");
    sots_pass("dye_need_mushrooms");

    sots_clear_inv(player);
    sots_give(player, obj_silverlight, 1);
    sots_give(player, obj_mushroom, 1);
    sots_opheldu(srv, obj_silverlight, obj_mushroom);
    SELFTEST_CHECK(selftest_count_obj(player, obj_dyed) >= 1,
                   "dye Silverlight with mushrooms must grant dyed sword");
    SELFTEST_CHECK(selftest_count_obj(player, obj_silverlight) == 0,
                   "undyed Silverlight is consumed");
    sots_pass("dye_silverlight");

    if( obj_ink > 0 && obj_shirt > 0 && obj_shirt_d > 0 )
    {
        sots_give(player, obj_ink, 1);
        sots_give(player, obj_shirt, 1);
        sots_opheldu(srv, obj_ink, obj_shirt);
        sots_pass("dye_desert_shirt");
    }
    if( obj_ink > 0 && obj_robe > 0 && obj_robe_d > 0 )
    {
        sots_give(player, obj_ink, 1);
        sots_give(player, obj_robe, 1);
        sots_opheldu(srv, obj_ink, obj_robe);
        sots_pass("dye_desert_robe");
    }

    /* Stairs down / Dave refuse undyed / infiltrate. */
    if( loc_stairs > 0 )
    {
        sots_oploc(srv, loc_stairs, SOTS_STAIRS_X, SOTS_STAIRS_Z, 0);
        sots_pass("stairs_down");
    }

    slot = sots_spawn(srv, npc_dave, SOTS_DAVE_X, SOTS_DAVE_Z, 0);
    SELFTEST_CHECK(slot >= 0, "Evil Dave should spawn");
    sots_clear_inv(player);
    sots_give(player, obj_silverlight, 1);
    sots_talk_drain(srv, npc_dave, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_INFILTRATE,
                   "Dave must refuse without dyed Silverlight");
    sots_pass("dave_refuse_undyed");

    sots_clear_inv(player);
    sots_give(player, obj_dyed, 1);
    sots_talk_drain(srv, npc_dave, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_DENATH,
                   "Dave infiltrate must set denath, got %d",
                   sots_get_vb(player, "agrith_quest"));
    sots_pass("dave_infiltrate");
    sots_journal(srv, "journal_denath");
    sots_free_npc(srv, slot);

    if( loc_portal > 0 )
    {
        sots_oploc(srv, loc_portal, SOTS_DAVE_X, SOTS_DAVE_Z, 0);
        sots_pass("portal_enter");
    }

    /* Denath / Jennifer / Matthew. */
    slot = sots_spawn(srv, npc_denath, SOTS_THRONE_X, SOTS_THRONE_Z, SOTS_THRONE_LEVEL);
    SELFTEST_CHECK(slot >= 0, "Denath should spawn");
    sots_talk_drain(srv, npc_denath, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_DENATH,
                   "Denath briefing must stay at denath");
    sots_pass("denath_brief");
    sots_free_npc(srv, slot);

    slot = sots_spawn(srv, npc_jennifer, SOTS_JENNIFER_X, SOTS_JENNIFER_Z, SOTS_THRONE_LEVEL);
    SELFTEST_CHECK(slot >= 0, "Jennifer should spawn");
    sots_talk_drain(srv, npc_jennifer, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_SIGIL_TASKS,
                   "Jennifer mould must set sigil tasks, got %d",
                   sots_get_vb(player, "agrith_quest"));
    SELFTEST_CHECK(selftest_count_obj(player, obj_mould) >= 1,
                   "Jennifer must grant the sigil mould");
    sots_pass("jennifer_mould");
    sots_journal(srv, "journal_sigil_tasks");

    sots_talk_drain(srv, npc_jennifer, slot);
    sots_pass("jennifer_already");
    sots_free_npc(srv, slot);

    sots_give(player, obj_bar, 1);
    sots_opheldu(srv, obj_mould, obj_bar);
    SELFTEST_CHECK(selftest_count_obj(player, obj_sigil) >= 1,
                   "soft furnace must cast a demonic sigil");
    sots_pass("cast_sigil");
    sots_opheldu(srv, obj_mould, obj_bar);
    sots_pass("cast_sigil_already");

    slot = sots_spawn(srv, npc_matthew, SOTS_MATTHEW_X, SOTS_MATTHEW_Z, SOTS_THRONE_LEVEL);
    SELFTEST_CHECK(slot >= 0, "Matthew should spawn");
    sots_talk_drain(srv, npc_matthew, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_MATTHEW,
                   "Matthew kiln tip must set matthew, got %d",
                   sots_get_vb(player, "agrith_quest"));
    sots_pass("matthew_kiln_tip");
    sots_journal(srv, "journal_matthew");

    /* Golem interrogation (lives in quest_golem/; SotS window only). */
    sots_free_npc(srv, slot);
    slot = sots_spawn(srv, npc_golem, SOTS_GOLEM_X, SOTS_GOLEM_Z, 0);
    SELFTEST_CHECK(slot >= 0, "clay golem should spawn");
    sots_talk_drain(srv, npc_golem, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_GOLEM_ASK,
                   "golem interrogation must set golem-ask, got %d",
                   sots_get_vb(player, "agrith_quest"));
    sots_pass("golem_ask");
    sots_journal(srv, "journal_golem_ask");
    sots_talk_drain(srv, npc_golem, slot);
    sots_pass("golem_ask_repeat");

    kiln_correct = sots_get_vb(player, "agrith_kiln");
    if( kiln_correct < 0 )
        kiln_correct = 0;
    kiln_wrong = (kiln_correct + 1) % 4;
    loc_right = loc_kiln1;
    loc_wrong = loc_kiln2;
    if( kiln_correct == 1 )
        loc_right = loc_kiln2;
    else if( kiln_correct == 2 )
        loc_right = loc_kiln3;
    else if( kiln_correct == 3 )
        loc_right = loc_kiln4;
    if( kiln_wrong == 0 )
        loc_wrong = loc_kiln1;
    else if( kiln_wrong == 1 )
        loc_wrong = loc_kiln2;
    else if( kiln_wrong == 2 )
        loc_wrong = loc_kiln3;
    else
        loc_wrong = loc_kiln4;

    /* Kiln too-early was already passed (quest was < 60). Wrong / right / already. */
    sots_oploc(srv, loc_wrong, SOTS_KILN_X, SOTS_KILN_Z, 0);
    SELFTEST_CHECK(selftest_count_obj(player, obj_book) == 0,
                   "wrong kiln must not grant the tome");
    sots_pass("kiln_wrong");

    sots_oploc(srv, loc_right, SOTS_KILN_X + 1, SOTS_KILN_Z, 0);
    SELFTEST_CHECK(selftest_count_obj(player, obj_book) >= 1,
                   "right kiln must grant Josef's tome");
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_RITUAL,
                   "finding the tome must set ritual (70), got %d",
                   sots_get_vb(player, "agrith_quest"));
    sots_pass("kiln_right");
    sots_journal(srv, "journal_ritual_tome");

    sots_opheld(srv, obj_book);
    sots_pass("read_agrith_book");

    sots_oploc(srv, loc_right, SOTS_KILN_X + 1, SOTS_KILN_Z, 0);
    sots_pass("kiln_already");

    /* Hand tome to Matthew: first ritual (scripted Tanya / Eric / Denath flee). */
    sots_free_npc(srv, slot);
    slot = sots_spawn(srv, npc_matthew, SOTS_MATTHEW_X, SOTS_MATTHEW_Z, SOTS_THRONE_LEVEL);
    sots_talk_drain(srv, npc_matthew, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_RITUAL_DONE,
                   "first ritual must set ritual-done, got %d",
                   sots_get_vb(player, "agrith_quest"));
    sots_pass("matthew_first_ritual");
    sots_journal(srv, "journal_ritual_done");

    if( npc_tanya > 0 )
    {
        int tanya = sots_spawn(
            srv, npc_tanya, SOTS_THRONE_X, SOTS_THRONE_Z + 2, SOTS_THRONE_LEVEL);
        sots_talk_drain(srv, npc_tanya, tanya);
        sots_pass("tanya_falling");
        sots_free_npc(srv, tanya);
    }
    if( npc_ghost > 0 )
    {
        int ghost = sots_spawn(
            srv, npc_ghost, SOTS_THRONE_X + 2, SOTS_THRONE_Z, SOTS_THRONE_LEVEL);
        sots_talk_drain(srv, npc_ghost, ghost);
        sots_pass("ritual_ghost");
        sots_free_npc(srv, ghost);
    }
    if( loc_rubble > 0 )
    {
        sots_oploc(srv, loc_rubble, SOTS_THRONE_X + 3, SOTS_THRONE_Z, SOTS_THRONE_LEVEL);
        sots_pass("eric_rubble");
    }
    if( loc_portal_out > 0 )
    {
        sots_oploc(srv, loc_portal_out, SOTS_THRONE_X, SOTS_THRONE_Z, SOTS_THRONE_LEVEL);
        sots_pass("portal_leave");
    }

    /* Recruit Dave / Badden / Reen / golem. */
    sots_free_npc(srv, slot);
    slot = sots_spawn(srv, npc_dave, SOTS_DAVE_X, SOTS_DAVE_Z, 0);
    sots_talk_drain(srv, npc_dave, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_convinced_dave") == 2,
                   "Dave recruit must set convinced=2");
    sots_pass("recruit_dave");
    sots_free_npc(srv, slot);

    slot = sots_spawn(srv, npc_badden, SOTS_BADDEN_X, SOTS_BADDEN_Z, 0);
    sots_talk_drain(srv, npc_badden, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_badden_uzer") == 2,
                   "Badden recruit must set badden_uzer=2");
    sots_pass("recruit_badden");
    sots_talk_drain(srv, npc_badden, slot);
    sots_pass("recruit_badden_already");
    sots_free_npc(srv, slot);

    slot = sots_spawn(srv, npc_reen, SOTS_REEN_X, SOTS_REEN_Z, 0);
    sots_talk_drain(srv, npc_reen, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_reen_uzer") == 2,
                   "Reen recruit must set reen_uzer=2");
    sots_pass("recruit_reen");
    sots_talk_drain(srv, npc_reen, slot);
    sots_pass("recruit_reen_already");
    sots_free_npc(srv, slot);

    slot = sots_spawn(srv, npc_golem, SOTS_GOLEM_X, SOTS_GOLEM_Z, 0);
    sots_talk_drain(srv, npc_golem, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_convinced_golem") == 1,
                   "golem first recruit talk must refuse and ask for reprogram");
    sots_pass("golem_recruit_refuse");
    sots_talk_drain(srv, npc_golem, slot);
    sots_pass("golem_recruit_waiting");
    if( obj_key > 0 )
    {
        sots_give(player, obj_key, 1);
        sots_opnpcu(srv, npc_golem, slot, obj_key);
        SELFTEST_CHECK(sots_get_vb(player, "agrith_convinced_golem") == 2,
                       "implement on golem must reprogram (convinced=2)");
        sots_pass("golem_reprogram");
    }
    sots_talk_drain(srv, npc_golem, slot);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_convinced_golem") == 3,
                   "golem confirm must set convinced=3");
    sots_pass("golem_recruit_join");
    sots_journal(srv, "journal_recruit");
    sots_free_npc(srv, slot);

    /* Second summon: not-yet / fail chant / pass chant. */
    slot = sots_spawn(srv, npc_matthew, SOTS_MATTHEW_X, SOTS_MATTHEW_Z, SOTS_THRONE_LEVEL);
    sots_talk_pick(srv, npc_matthew, slot, 2);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_RITUAL_DONE,
                   "Matthew 'not yet' must not summon");
    sots_pass("summon_not_yet");

    sots_talk_pick2(srv, npc_matthew, slot, 1, 1);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_RITUAL_DONE,
                   "wrong incantation must fail and retry");
    sots_pass("incantation_fail");

    sots_talk_pick2(srv, npc_matthew, slot, 1, 2);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_FIGHT,
                   "true incantation must start the fight, got %d",
                   sots_get_vb(player, "agrith_quest"));
    sots_pass("incantation_pass");
    sots_journal(srv, "journal_fight");

    agrith_slot = selftest_find_npc(srv, npc_agrith);
    if( agrith_slot < 0 )
        agrith_slot = ToriRSServer_WorldNpcSpawn(
            srv, npc_agrith, SOTS_THRONE_X + 1, SOTS_THRONE_Z, SOTS_THRONE_LEVEL);
    SELFTEST_CHECK(agrith_slot >= 0, "Agrith-Naar should be in the throne room");
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC2, npc_agrith, -1, agrith_slot);
    sots_finish(srv);
    sots_pass("agrith_opnpc2");

    /* Wrong-weapon revive (12 HP). */
    sots_clear_inv(player);
    if( obj_bronze > 0 )
        worn_set(player, TORIRSSERVER_WEAR_WEAPON, obj_bronze, 1);
    ToriRSServer_ScriptsRunProcOnNpc(srv, "[ai_queue3,agrith_naar]", agrith_slot);
    sots_finish(srv);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_FIGHT,
                   "wrong weapon must not finish Agrith-Naar");
    SELFTEST_CHECK(srv->npcs[agrith_slot].active,
                   "Agrith-Naar must revive in place without Silverlight");
    sots_pass("agrith_wrong_weapon_revive");

    /* Silverlight finishing blow -> Darklight + unequip gate. */
    worn_set(player, TORIRSSERVER_WEAR_WEAPON, obj_dyed, 1);
    ToriRSServer_ScriptsRunProcOnNpc(srv, "[ai_queue3,agrith_naar]", agrith_slot);
    sots_finish(srv);
    SELFTEST_CHECK(sots_get_vb(player, "agrith_quest") == SOTS_UNEQUIP,
                   "Silverlight blow must set unequip, got %d",
                   sots_get_vb(player, "agrith_quest"));
    SELFTEST_CHECK(selftest_count_obj(player, obj_darklight) >= 1,
                   "Silverlight must become Darklight");
    sots_pass("agrith_silverlight_darklight");
    sots_journal(srv, "journal_unequip");
    sots_free_npc(srv, agrith_slot);
    sots_free_npc(srv, slot);

    /* Unequip gate / authored complete / optional gems / HP lamp. */
    hp_xp_before = player->stat_xp_tenths[TORIRSSERVER_STAT_HITPOINTS];
    sots_vb(srv, "golem_throne_gems", 0);
    slot = sots_spawn(srv, npc_reen, SOTS_REEN_X, SOTS_REEN_Z, 0);
    sots_talk_drain(srv, npc_reen, slot);
    quest = sots_get_vb(player, "agrith_quest");
    SELFTEST_CHECK(quest == SOTS_COMPLETE,
                   "Reen unequip-gate complete must set 125, got %d", quest);
    SELFTEST_CHECK(selftest_count_obj(player, obj_darklight) >= 1,
                   "complete must leave Darklight");
    {
        int hp_delta = player->stat_xp_tenths[TORIRSSERVER_STAT_HITPOINTS] - hp_xp_before;
        SELFTEST_CHECK(hp_delta == SOTS_LAMP_XP || hp_delta == SOTS_LAMP_XP * 10,
                       "lamp is modelled as 10000 Hitpoints XP (delta %d tenths)",
                       hp_delta);
    }
    if( obj_sapphire > 0 && obj_ruby > 0 && obj_emerald > 0 )
    {
        SELFTEST_CHECK(selftest_count_obj(player, obj_sapphire) >= 2,
                       "throne-gem bonus should grant 2 sapphires");
        SELFTEST_CHECK(selftest_count_obj(player, obj_ruby) >= 2,
                       "throne-gem bonus should grant 2 rubies");
        SELFTEST_CHECK(selftest_count_obj(player, obj_emerald) >= 2,
                       "throne-gem bonus should grant 2 emeralds");
    }
    sots_pass("reen_unequip_complete");
    sots_pass("complete_scroll");
    sots_journal(srv, "journal_complete");

    sots_talk_drain(srv, npc_reen, slot);
    sots_pass("reen_post_complete");
    sots_free_npc(srv, slot);

    slot = sots_spawn(srv, npc_badden, SOTS_BADDEN_X, SOTS_BADDEN_Z, 0);
    sots_talk_drain(srv, npc_badden, slot);
    sots_pass("badden_post_complete");
    sots_free_npc(srv, slot);

    if( loc_stairs_up > 0 )
    {
        sots_oploc(srv, loc_stairs_up, SOTS_DAVE_X, SOTS_DAVE_Z, 0);
        sots_pass("stairs_up");
    }

    fprintf(stderr,
            "ToriRSServer shadowstorm selftest: %d checks, %d failures this walk\n",
            (int)g_selftest_checks - checks_before,
            g_selftest_failures - fails_before);
}

#endif /* TORIRSSERVER_TEST_QUEST_SHADOWSTORM_SELFTEST_U_H */
