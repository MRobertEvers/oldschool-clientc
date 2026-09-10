#ifndef TORIRSSERVER_TEST_QUEST_ENLIGHTENEDJOURNEY_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_ENLIGHTENEDJOURNEY_SELFTEST_U_H

/* Enlightened Journey Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Auguste / assistants / basket loc
 * cannot leak. Real OPNPC1 / OPNPC4 / OPLOCU / OPHELDU on the authored
 * path. player->godmode = 1 for the whole walk (not a death test).
 *
 * Gate: TORIRSSERVER_SELFTEST_EJ_ONLY=1
 *       (aliases: ENLIGHTENEDJOURNEY / ZEP)
 *
 * [opnpc1,zep_piccard] is the existing MM2 header in
 * quest_monkeymadnessii/scripts/monkeymadnessii.rs2. This walk calls
 * that trigger (MM2's gated branch stays first and is not taken).
 * Do not add a second Auguste header.
 *
 * Disclosed leftovers (named leftover_*.bmp; not jewellery IF /
 * daily-reset / flute widget / telekinetic grab):
 *   - widget-471 balloon flight collapsed to teleport + finish talk
 *   - flash-mob crowd narrated (no per-NPC op)
 *   - per-city first-flight gate (unlocked all at complete)
 */

#define EJ_NOT_STARTED 0
#define EJ_ACCEPTED 10
#define EJ_PROTOTYPE 20
#define EJ_SECOND_BATCH 40
#define EJ_MOB_DEBRIEFED 60
#define EJ_MATERIALS 70
#define EJ_BASKET 80
#define EJ_READY 90
#define EJ_COMPLETE 200

#define EJ_REQ_QP 20
#define EJ_REQ_FM 20
#define EJ_REQ_FARM 30
#define EJ_REQ_CRAFT 36
#define EJ_REWARD_FM_TENTHS 40000
#define EJ_REWARD_FARM_TENTHS 30000
#define EJ_REWARD_CRAFT_TENTHS 20000
#define EJ_REWARD_WC_TENTHS 15000
#define EJ_REWARD_QP 1

#define EJ_ENTRANA_X 2808
#define EJ_ENTRANA_Z 3355
#define EJ_TAVERLEY_X 2938
#define EJ_TAVERLEY_Z 3422
#define EJ_CASTLE_X 2460
#define EJ_CASTLE_Z 3108
#define EJ_GRAND_X 2480
#define EJ_GRAND_Z 3458
#define EJ_CRAFT_X 2925
#define EJ_CRAFT_Z 3303
#define EJ_VARROCK_X 3298
#define EJ_VARROCK_Z 3483

static void
ej_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "EJ PASS: %s\n", step);
}

static void
ej_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
ej_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
ej_finish(struct ToriRSServer* srv)
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
    ToriRSServer_ScriptsProcessQueues(srv);
    for( t = 0; t < 4; t++ )
        selftest_tick(srv);
}

static int
ej_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
ej_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : ej_chatmenu();
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
ej_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = ej_chatmenu();
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
ej_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    ej_god(player);
    selftest_tick(srv);
}

static int
ej_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    ej_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
ej_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
ej_free_type(struct ToriRSServer* srv, int npc_type)
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
ej_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
ej_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
ej_varp(struct ToriRSServer* srv, const char* name, int value)
{
    int varp;

    assert(srv);
    assert(name);
    varp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, name);
    if( varp >= 0 )
        ToriRSServer_WorldSetVarp(srv, varp, value);
}

static int
ej_quest(struct ToriRSServerPlayer* player)
{
    return ej_get_vb(player, "zep_quest");
}

static void
ej_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
ej_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    ej_talk(srv, npc_type, slot);
    ej_finish(srv);
}

static void
ej_talk_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    ej_talk(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        ej_click_until_menu(srv, 24);
        ej_pick_row(srv, rows[i]);
    }
    ej_finish(srv);
}

static void
ej_fly(struct ToriRSServer* srv, int npc_type, int slot)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(npc_type > 0);
    player = srv->active_player;
    assert(player);
    player->last_slot = slot;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC4, npc_type, -1, slot);
}

static void
ej_fly_rows(struct ToriRSServer* srv, int npc_type, int slot, const int* rows, int n)
{
    int i;

    assert(srv);
    assert(rows);
    assert(n > 0);
    ej_fly(srv, npc_type, slot);
    for( i = 0; i < n; i++ )
    {
        ej_click_until_menu(srv, 24);
        ej_pick_row(srv, rows[i]);
    }
    ej_finish(srv);
}

static void
ej_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
ej_place_loc(struct ToriRSServer* srv, int loc_id, int x, int z, int level)
{
    int slot;

    assert(srv);
    assert(loc_id > 0);
    ej_tele(srv, x, z, level);
    ToriRSServer_WorldLocSet(srv, x, z, level, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
    slot = ToriRSServer_SceneFindLocId(x, z, level, loc_id);
    if( slot < 0 )
        slot = ToriRSServer_SceneFindLoc(x, z, level, loc_id);
    return slot;
}

static void
ej_use_loc(struct ToriRSServer* srv, int loc_id, int loc_slot, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->last_useitem = obj_id;
    player->last_useslot = 0;
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_id, -1, loc_slot);
    else
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOCU, loc_id, -1, -1);
    ej_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
ej_opheldu(struct ToriRSServer* srv, int clicked, int used)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(clicked > 0);
    player = srv->active_player;
    assert(player);
    player->last_useitem = used;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, clicked, -1, -1);
    ej_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static void
ej_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
ej_skills99(struct ToriRSServerPlayer* player)
{
    int stat;

    assert(player);
    for( stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        ej_set_stat(player, stat, 99);
}

static int
ej_inv_has(struct ToriRSServerPlayer* player, int obj_id)
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
ej_inv_count(struct ToriRSServerPlayer* player, int obj_id)
{
    int s;
    int n;

    assert(player);
    if( obj_id <= 0 )
        return 0;
    n = 0;
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            n += player->inv[s].count;
    }
    return n;
}

static void
ej_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    ej_vb(srv, "zep_quest", EJ_NOT_STARTED);
    ej_vb(srv, "zep_rdye", 0);
    ej_vb(srv, "zep_ydye", 0);
    ej_vb(srv, "zep_bowl", 0);
    ej_vb(srv, "zep_sandbags", 0);
    ej_vb(srv, "zep_silk", 0);
    ej_vb(srv, "zep_logs", 0);
    ej_vb(srv, "zep_multi_basket", 0);
    ej_vb(srv, "zep_multi_piccard", 0);
    ej_vb(srv, "zep_multi_cast", 0);
    ej_vb(srv, "zep_multi_gno", 0);
    ej_vb(srv, "zep_multi_craft", 0);
    ej_vb(srv, "zep_multi_varr", 0);
}

static void
ej_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,ej_journal]", NULL, 0);
    ej_finish(srv);
    ej_pass(step);
}

static void
selftest_quest_enlightenedjourney(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_aug;
    int npc_tav;
    int npc_cast;
    int npc_gno;
    int npc_craft;
    int npc_varr;
    int loc_sand;
    int loc_basket;
    int obj_papyrus;
    int obj_wool;
    int obj_candle;
    int obj_frame;
    int obj_balloon;
    int obj_potato;
    int obj_reddye;
    int obj_yellowdye;
    int obj_silk;
    int obj_bowl;
    int obj_sack;
    int obj_sandbag;
    int obj_willow;
    int obj_logs;
    int obj_tinder;
    int obj_jacket;
    int obj_cap;
    int stat_fm;
    int stat_farm;
    int stat_craft;
    int stat_wc;
    int varp_qp;
    int slot_aug;
    int slot_tav;
    int slot_cast;
    int slot_gno;
    int slot_craft;
    int slot_varr;
    int loc_slot;
    int fm_before;
    int farm_before;
    int craft_before;
    int wc_before;
    int qp_before;
    static const int k_refuse[] = { 2 };
    static const int k_accept[] = { 1 };
    static const int k_give_refuse[] = { 5 };
    static const int k_give_dye[] = { 1 };
    static const int k_give_sand[] = { 2 };
    static const int k_give_silk[] = { 3 };
    static const int k_give_bowl[] = { 4 };
    static const int k_fly_notyet[] = { 2 };
    static const int k_fly_ready[] = { 1 };
    static const int k_dest_tav[] = { 1 };
    static const int k_dest_cast[] = { 2 };
    static const int k_dest_craft[] = { 3 };
    static const int k_dest_gno[] = { 4 };
    static const int k_dest_varr[] = { 5 };
    static const int k_dest_ent[] = { 1 };

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: enlightened journey critical path\n");

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "ToriRSServer ej selftest: SKIP no compiled script pack\n");
        return;
    }

    ToriRSServer_WorldSetActive(srv, player);
    srv->members_world = 1;
    player->godmode = 1;
    ej_god(player);
    ej_reset_quest(srv);
    ej_clear_inv(player);

    npc_aug = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zep_piccard");
    npc_tav = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zep_assist_tav");
    npc_cast = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zep_assist_cast");
    npc_gno = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zep_assist_gno");
    npc_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zep_assist_craft");
    npc_varr = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zep_assist_varr");
    loc_sand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "sandpit");
    loc_basket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "zep_multi_basket_entrana");
    obj_papyrus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "papyrus");
    obj_wool = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ball_of_wool");
    obj_candle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unlit_candle");
    obj_frame = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zep_test_balloon_struc");
    obj_balloon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zep_test_balloon");
    obj_potato = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sack_potato_10");
    obj_reddye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "reddye");
    obj_yellowdye = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yellowdye");
    obj_silk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "silk");
    obj_bowl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bowl_empty");
    obj_sack = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "sack_empty");
    obj_sandbag = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zep_sandbag");
    obj_willow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "willow_branch");
    obj_logs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "logs");
    obj_tinder = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "tinderbox");
    obj_jacket = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zep_bomber_jacket");
    obj_cap = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "zep_bomber_cap");
    stat_fm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "firemaking");
    stat_farm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "farming");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_wc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_enlightenedjourney") > 0,
                   "dbrow quest_enlightenedjourney should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "zep_quest") >= 0,
                   "varbit zep_quest should resolve");
    SELFTEST_CHECK(npc_aug > 0, "npc zep_piccard should resolve");
    if( npc_aug <= 0 )
        return;

    ej_journal(srv, "journal_00_not_started");

    slot_aug = ej_spawn(srv, npc_aug, EJ_ENTRANA_X, EJ_ENTRANA_Z, 0);
    SELFTEST_CHECK(slot_aug >= 0, "Auguste should spawn");
    if( slot_aug >= 0 )
    {
        ej_varp(srv, "qp", 0);
        ej_set_stat(player, stat_fm, 1);
        ej_set_stat(player, stat_farm, 1);
        ej_set_stat(player, stat_craft, 1);
        ej_talk_finish(srv, npc_aug, slot_aug);
        SELFTEST_CHECK(ej_quest(player) == EJ_NOT_STARTED,
                       "QP qualify-fail must stay not_started");
        ej_pass("opnpc1_auguste_qualify_fail_qp");

        ej_varp(srv, "qp", EJ_REQ_QP);
        ej_talk_finish(srv, npc_aug, slot_aug);
        SELFTEST_CHECK(ej_quest(player) == EJ_NOT_STARTED,
                       "Firemaking qualify-fail must stay not_started");
        ej_pass("opnpc1_auguste_qualify_fail_firemaking");

        ej_set_stat(player, stat_fm, 99);
        ej_talk_finish(srv, npc_aug, slot_aug);
        SELFTEST_CHECK(ej_quest(player) == EJ_NOT_STARTED,
                       "Farming qualify-fail must stay not_started");
        ej_pass("opnpc1_auguste_qualify_fail_farming");

        ej_set_stat(player, stat_farm, 99);
        ej_talk_finish(srv, npc_aug, slot_aug);
        SELFTEST_CHECK(ej_quest(player) == EJ_NOT_STARTED,
                       "Crafting qualify-fail must stay not_started");
        ej_pass("opnpc1_auguste_qualify_fail_crafting");

        ej_skills99(player);
        ej_talk_rows(srv, npc_aug, slot_aug, k_refuse, 1);
        SELFTEST_CHECK(ej_quest(player) == EJ_NOT_STARTED,
                       "Auguste refuse must stay not_started");
        ej_pass("opnpc1_auguste_refuse");

        ej_talk_rows(srv, npc_aug, slot_aug, k_accept, 1);
        SELFTEST_CHECK(ej_quest(player) == EJ_ACCEPTED,
                       "Auguste accept must write accepted, got %d",
                       ej_quest(player));
        ej_pass("opnpc1_auguste_accept");

        ej_talk_finish(srv, npc_aug, slot_aug);
        ej_pass("opnpc1_auguste_accepted_reminder");
    }
    ej_journal(srv, "journal_10_accepted");

    if( obj_papyrus > 0 && obj_wool > 0 )
    {
        ej_clear_inv(player);
        ej_vb(srv, "zep_quest", EJ_NOT_STARTED);
        ej_give(player, obj_papyrus, 1);
        ej_give(player, obj_wool, 1);
        ej_opheldu(srv, obj_papyrus, obj_wool);
        SELFTEST_CHECK(!ej_inv_has(player, obj_frame),
                       "origami too-early must not grant a frame");
        ej_pass("opheldu_origami_too_early");

        ej_vb(srv, "zep_quest", EJ_ACCEPTED);
        ej_clear_inv(player);
        ej_opheldu(srv, obj_papyrus, obj_wool);
        SELFTEST_CHECK(!ej_inv_has(player, obj_frame),
                       "origami without items must not grant a frame");
        ej_pass("opheldu_origami_need_items");

        ej_give(player, obj_papyrus, 1);
        ej_give(player, obj_wool, 1);
        ej_opheldu(srv, obj_papyrus, obj_wool);
        SELFTEST_CHECK(ej_inv_has(player, obj_frame),
                       "papyrus+wool must grant balloon frame");
        ej_pass("opheldu_origami_papyrus_wool");

        ej_give(player, obj_papyrus, 1);
        ej_give(player, obj_wool, 1);
        ej_opheldu(srv, obj_papyrus, obj_wool);
        SELFTEST_CHECK(ej_inv_count(player, obj_frame) == 1,
                       "second frame attempt must keep one frame");
        ej_pass("opheldu_origami_already_have");
    }

    if( obj_frame > 0 && obj_candle > 0 && obj_balloon > 0 )
    {
        ej_clear_inv(player);
        ej_vb(srv, "zep_quest", EJ_ACCEPTED);
        ej_give(player, obj_frame, 1);
        ej_give(player, obj_candle, 1);
        ej_opheldu(srv, obj_frame, obj_candle);
        SELFTEST_CHECK(ej_inv_has(player, obj_balloon),
                       "candle+frame must grant origami balloon");
        ej_pass("opheldu_origami_candle");
    }

    if( slot_aug >= 0 && obj_balloon > 0 )
    {
        ej_tele(srv, EJ_ENTRANA_X, EJ_ENTRANA_Z, 0);
        ej_vb(srv, "zep_quest", EJ_ACCEPTED);
        ej_clear_inv(player);
        ej_give(player, obj_balloon, 1);
        ej_talk_finish(srv, npc_aug, slot_aug);
        SELFTEST_CHECK(ej_quest(player) == EJ_PROTOTYPE,
                       "show prototype must write prototype_shown, got %d",
                       ej_quest(player));
        ej_pass("opnpc1_auguste_show_prototype");

        ej_talk_finish(srv, npc_aug, slot_aug);
        ej_pass("opnpc1_auguste_prototype_reminder");
    }
    ej_journal(srv, "journal_20_prototype");

    if( slot_aug >= 0 && obj_papyrus > 0 && obj_potato > 0 )
    {
        ej_tele(srv, EJ_ENTRANA_X, EJ_ENTRANA_Z, 0);
        ej_vb(srv, "zep_quest", EJ_PROTOTYPE);
        ej_clear_inv(player);
        ej_give(player, obj_papyrus, 2);
        ej_give(player, obj_potato, 1);
        ej_talk_finish(srv, npc_aug, slot_aug);
        SELFTEST_CHECK(ej_quest(player) == EJ_SECOND_BATCH,
                       "flash-mob mishap must write second_batch, got %d",
                       ej_quest(player));
        ej_pass("opnpc1_auguste_flashmob_mishap");

        ej_talk_finish(srv, npc_aug, slot_aug);
        SELFTEST_CHECK(ej_quest(player) == EJ_MOB_DEBRIEFED,
                       "debrief must write mob_debriefed, got %d",
                       ej_quest(player));
        ej_pass("opnpc1_auguste_flashmob_debrief");
    }
    ej_journal(srv, "journal_40_second_batch");
    ej_journal(srv, "journal_60_mob_debriefed");

    if( loc_sand > 0 && obj_sack > 0 && obj_sandbag > 0 )
    {
        loc_slot = ej_place_loc(srv, loc_sand, EJ_ENTRANA_X, EJ_ENTRANA_Z - 8, 0);
        ej_vb(srv, "zep_quest", EJ_ACCEPTED);
        ej_clear_inv(player);
        ej_give(player, obj_sack, 1);
        ej_use_loc(srv, loc_sand, loc_slot, obj_sack);
        SELFTEST_CHECK(!ej_inv_has(player, obj_sandbag),
                       "sandbag too-early must not fill");
        ej_pass("oplocu_sandbag_too_early");

        ej_vb(srv, "zep_quest", EJ_MOB_DEBRIEFED);
        ej_give(player, obj_sack, 1);
        ej_use_loc(srv, loc_sand, loc_slot, obj_sack);
        SELFTEST_CHECK(ej_inv_has(player, obj_sandbag),
                       "sandpit+sack must grant a sandbag");
        ej_pass("oplocu_sandbag_fill");
    }

    if( slot_aug >= 0 )
    {
        ej_tele(srv, EJ_ENTRANA_X, EJ_ENTRANA_Z, 0);
        ej_vb(srv, "zep_quest", EJ_MOB_DEBRIEFED);
        ej_vb(srv, "zep_rdye", 0);
        ej_vb(srv, "zep_ydye", 0);
        ej_vb(srv, "zep_sandbags", 0);
        ej_vb(srv, "zep_silk", 0);
        ej_vb(srv, "zep_bowl", 0);
        ej_clear_inv(player);
        ej_talk_rows(srv, npc_aug, slot_aug, k_give_refuse, 1);
        SELFTEST_CHECK(ej_quest(player) == EJ_MOB_DEBRIEFED,
                       "give-items refuse must stay mob_debriefed");
        ej_pass("opnpc1_give_items_refuse");

        ej_talk_rows(srv, npc_aug, slot_aug, k_give_dye, 1);
        ej_pass("opnpc1_give_dye_none");

        if( obj_reddye > 0 && obj_yellowdye > 0 )
        {
            ej_give(player, obj_reddye, 1);
            ej_give(player, obj_yellowdye, 1);
            ej_talk_rows(srv, npc_aug, slot_aug, k_give_dye, 1);
            SELFTEST_CHECK(ej_get_vb(player, "zep_rdye") == 1,
                           "red dye hand-in must set zep_rdye");
            SELFTEST_CHECK(ej_get_vb(player, "zep_ydye") == 1,
                           "yellow dye hand-in must set zep_ydye");
            ej_pass("opnpc1_give_dye");
        }

        ej_talk_rows(srv, npc_aug, slot_aug, k_give_sand, 1);
        ej_pass("opnpc1_give_sandbags_none");

        if( obj_sandbag > 0 )
        {
            ej_give(player, obj_sandbag, 8);
            ej_talk_rows(srv, npc_aug, slot_aug, k_give_sand, 1);
            SELFTEST_CHECK(ej_get_vb(player, "zep_sandbags") >= 8,
                           "sandbag hand-in must record eight");
            ej_pass("opnpc1_give_sandbags");

            ej_talk_rows(srv, npc_aug, slot_aug, k_give_sand, 1);
            ej_pass("opnpc1_give_sandbags_already");
        }

        ej_talk_rows(srv, npc_aug, slot_aug, k_give_silk, 1);
        ej_pass("opnpc1_give_silk_not_enough");

        if( obj_silk > 0 )
        {
            ej_give(player, obj_silk, 10);
            ej_talk_rows(srv, npc_aug, slot_aug, k_give_silk, 1);
            SELFTEST_CHECK(ej_get_vb(player, "zep_silk") >= 10,
                           "silk hand-in must record ten");
            ej_pass("opnpc1_give_silk");

            ej_talk_rows(srv, npc_aug, slot_aug, k_give_silk, 1);
            ej_pass("opnpc1_give_silk_already");
        }

        ej_talk_rows(srv, npc_aug, slot_aug, k_give_bowl, 1);
        ej_pass("opnpc1_give_bowl_none");

        if( obj_bowl > 0 )
        {
            ej_give(player, obj_bowl, 1);
            ej_talk_rows(srv, npc_aug, slot_aug, k_give_bowl, 1);
            SELFTEST_CHECK(ej_get_vb(player, "zep_bowl") == 1,
                           "bowl hand-in must set zep_bowl");
            SELFTEST_CHECK(ej_quest(player) == EJ_MATERIALS,
                           "all materials must write materials_given, got %d",
                           ej_quest(player));
            ej_pass("opnpc1_give_bowl_materials_complete");
        }

        ej_talk_finish(srv, npc_aug, slot_aug);
        ej_pass("opnpc1_materials_reminder");
    }
    ej_journal(srv, "journal_70_materials");

    if( loc_basket > 0 && obj_willow > 0 )
    {
        loc_slot = ej_place_loc(srv, loc_basket, EJ_ENTRANA_X + 2, EJ_ENTRANA_Z, 0);
        ej_vb(srv, "zep_quest", EJ_MOB_DEBRIEFED);
        ej_clear_inv(player);
        ej_give(player, obj_willow, 12);
        ej_use_loc(srv, loc_basket, loc_slot, obj_willow);
        SELFTEST_CHECK(ej_quest(player) == EJ_MOB_DEBRIEFED,
                       "basket too-early must stay mob_debriefed");
        ej_pass("oplocu_basket_too_early");

        ej_vb(srv, "zep_quest", EJ_MATERIALS);
        ej_clear_inv(player);
        ej_give(player, obj_willow, 4);
        ej_use_loc(srv, loc_basket, loc_slot, obj_willow);
        SELFTEST_CHECK(ej_quest(player) == EJ_MATERIALS,
                       "basket without twelve must stay materials_given");
        ej_pass("oplocu_basket_need_twelve");

        ej_give(player, obj_willow, 12);
        ej_use_loc(srv, loc_basket, loc_slot, obj_willow);
        SELFTEST_CHECK(ej_quest(player) == EJ_BASKET,
                       "twelve branches must write basket_built, got %d",
                       ej_quest(player));
        ej_pass("oplocu_basket_weave");

        ej_give(player, obj_willow, 12);
        ej_use_loc(srv, loc_basket, loc_slot, obj_willow);
        ej_pass("oplocu_basket_already");
    }
    ej_journal(srv, "journal_80_basket");

    if( slot_aug >= 0 )
    {
        ej_tele(srv, EJ_ENTRANA_X, EJ_ENTRANA_Z, 0);
        ej_vb(srv, "zep_quest", EJ_BASKET);
        ej_clear_inv(player);
        ej_talk_finish(srv, npc_aug, slot_aug);
        ej_pass("opnpc1_logs_reminder");

        if( obj_logs > 0 && obj_tinder > 0 )
        {
            ej_give(player, obj_logs, 10);
            ej_give(player, obj_tinder, 1);
            ej_talk_rows(srv, npc_aug, slot_aug, k_fly_notyet, 1);
            SELFTEST_CHECK(ej_quest(player) == EJ_BASKET,
                           "fly not-yet must stay basket_built");
            ej_pass("opnpc1_fly_not_yet");

            ej_talk_rows(srv, npc_aug, slot_aug, k_fly_ready, 1);
            SELFTEST_CHECK(ej_quest(player) == EJ_READY,
                           "maiden flight must write ready_to_fly, got %d",
                           ej_quest(player));
            SELFTEST_CHECK(player->x == EJ_TAVERLEY_X && player->z == EJ_TAVERLEY_Z,
                           "maiden flight must land at Taverley %d,%d got %d,%d",
                           EJ_TAVERLEY_X, EJ_TAVERLEY_Z, player->x, player->z);
            ej_pass("opnpc1_fly_ready_landing");
        }
    }
    ej_journal(srv, "journal_90_ready");

    if( slot_aug >= 0 )
    {
        ej_tele(srv, EJ_TAVERLEY_X, EJ_TAVERLEY_Z, 0);
        ej_vb(srv, "zep_quest", EJ_READY);
        ej_vb(srv, "zep_multi_piccard", 1);
        ej_skills99(player);
        ej_clear_inv(player);
        fm_before = (stat_fm >= 0) ? player->stat_xp_tenths[stat_fm] : 0;
        farm_before = (stat_farm >= 0) ? player->stat_xp_tenths[stat_farm] : 0;
        craft_before = (stat_craft >= 0) ? player->stat_xp_tenths[stat_craft] : 0;
        wc_before = (stat_wc >= 0) ? player->stat_xp_tenths[stat_wc] : 0;
        qp_before = (varp_qp >= 0) ? player->varps[varp_qp] : 0;
        ej_talk_finish(srv, npc_aug, slot_aug);
        SELFTEST_CHECK(ej_quest(player) == EJ_COMPLETE,
                       "Taverley Auguste must complete the quest, got %d",
                       ej_quest(player));
        if( stat_fm >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_fm] >=
                               fm_before + EJ_REWARD_FM_TENTHS,
                           "complete must award 4000 Firemaking XP");
        if( stat_farm >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_farm] >=
                               farm_before + EJ_REWARD_FARM_TENTHS,
                           "complete must award 3000 Farming XP");
        if( stat_craft >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] >=
                               craft_before + EJ_REWARD_CRAFT_TENTHS,
                           "complete must award 2000 Crafting XP");
        if( stat_wc >= 0 )
            SELFTEST_CHECK(player->stat_xp_tenths[stat_wc] >=
                               wc_before + EJ_REWARD_WC_TENTHS,
                           "complete must award 1500 Woodcutting XP");
        if( varp_qp >= 0 )
            SELFTEST_CHECK(player->varps[varp_qp] >= qp_before + EJ_REWARD_QP,
                           "complete must award 1 QP");
        if( obj_jacket > 0 )
            SELFTEST_CHECK(ej_inv_has(player, obj_jacket),
                           "complete must grant bomber jacket");
        if( obj_cap > 0 )
            SELFTEST_CHECK(ej_inv_has(player, obj_cap),
                           "complete must grant bomber cap");
        ej_pass("opnpc1_taverley_complete_scroll");

        ej_talk_finish(srv, npc_aug, slot_aug);
        ej_pass("opnpc1_already_complete");
    }
    ej_journal(srv, "journal_200_complete");

    if( slot_aug >= 0 )
    {
        ej_tele(srv, EJ_ENTRANA_X, EJ_ENTRANA_Z, 0);
        ej_vb(srv, "zep_quest", EJ_ACCEPTED);
        ej_fly(srv, npc_aug, slot_aug);
        ej_finish(srv);
        ej_pass("opnpc4_entrana_fly_early");

        ej_vb(srv, "zep_quest", EJ_COMPLETE);
        ej_vb(srv, "zep_multi_piccard", 2);
        ej_vb(srv, "zep_multi_cast", 1);
        ej_vb(srv, "zep_multi_gno", 1);
        ej_vb(srv, "zep_multi_craft", 1);
        ej_vb(srv, "zep_multi_varr", 1);
        ej_fly_rows(srv, npc_aug, slot_aug, k_dest_tav, 1);
        SELFTEST_CHECK(player->x == EJ_TAVERLEY_X && player->z == EJ_TAVERLEY_Z,
                       "Entrana dest Taverley must land at Taverley");
        ej_pass("opnpc4_entrana_fly_taverley");
    }

    slot_tav = (npc_tav > 0) ? ej_spawn(srv, npc_tav, EJ_TAVERLEY_X, EJ_TAVERLEY_Z, 0) : -1;
    if( slot_tav >= 0 )
    {
        ej_vb(srv, "zep_quest", EJ_COMPLETE);
        ej_vb(srv, "zep_multi_piccard", 2);
        ej_talk_finish(srv, npc_tav, slot_tav);
        ej_pass("opnpc1_tav_talk");
        ej_fly_rows(srv, npc_tav, slot_tav, k_dest_cast, 1);
        SELFTEST_CHECK(player->x == EJ_CASTLE_X && player->z == EJ_CASTLE_Z,
                       "Taverley dest Castle Wars must land at Castle Wars");
        ej_pass("opnpc4_tav_fly_castlewars");
    }

    slot_cast = (npc_cast > 0) ? ej_spawn(srv, npc_cast, EJ_CASTLE_X, EJ_CASTLE_Z, 0) : -1;
    if( slot_cast >= 0 )
    {
        ej_vb(srv, "zep_quest", EJ_COMPLETE);
        ej_vb(srv, "zep_multi_cast", 1);
        ej_talk_finish(srv, npc_cast, slot_cast);
        ej_pass("opnpc1_cast_talk");
        ej_fly_rows(srv, npc_cast, slot_cast, k_dest_craft, 1);
        SELFTEST_CHECK(player->x == EJ_CRAFT_X && player->z == EJ_CRAFT_Z,
                       "Castle Wars dest Crafting Guild must land at Crafting Guild");
        ej_pass("opnpc4_cast_fly_craftingguild");
    }

    slot_gno = (npc_gno > 0) ? ej_spawn(srv, npc_gno, EJ_GRAND_X, EJ_GRAND_Z, 0) : -1;
    if( slot_gno >= 0 )
    {
        ej_vb(srv, "zep_quest", EJ_COMPLETE);
        ej_vb(srv, "zep_multi_gno", 1);
        ej_talk_finish(srv, npc_gno, slot_gno);
        ej_pass("opnpc1_gno_talk");
        ej_fly_rows(srv, npc_gno, slot_gno, k_dest_varr, 1);
        SELFTEST_CHECK(player->x == EJ_VARROCK_X && player->z == EJ_VARROCK_Z,
                       "Grand Tree dest Varrock must land at Varrock");
        ej_pass("opnpc4_gno_fly_varrock");
    }

    slot_craft = (npc_craft > 0) ? ej_spawn(srv, npc_craft, EJ_CRAFT_X, EJ_CRAFT_Z, 0) : -1;
    if( slot_craft >= 0 )
    {
        ej_vb(srv, "zep_quest", EJ_COMPLETE);
        ej_vb(srv, "zep_multi_craft", 1);
        ej_talk_finish(srv, npc_craft, slot_craft);
        ej_pass("opnpc1_craft_talk");
        ej_fly_rows(srv, npc_craft, slot_craft, k_dest_gno, 1);
        SELFTEST_CHECK(player->x == EJ_GRAND_X && player->z == EJ_GRAND_Z,
                       "Crafting Guild dest Grand Tree must land at Grand Tree");
        ej_pass("opnpc4_craft_fly_grandtree");
    }

    slot_varr = (npc_varr > 0) ? ej_spawn(srv, npc_varr, EJ_VARROCK_X, EJ_VARROCK_Z, 0) : -1;
    if( slot_varr >= 0 )
    {
        ej_vb(srv, "zep_quest", EJ_COMPLETE);
        ej_vb(srv, "zep_multi_varr", 1);
        ej_talk_finish(srv, npc_varr, slot_varr);
        ej_pass("opnpc1_varr_talk");
        ej_fly_rows(srv, npc_varr, slot_varr, k_dest_ent, 1);
        SELFTEST_CHECK(player->x == EJ_ENTRANA_X && player->z == EJ_ENTRANA_Z,
                       "Varrock dest Entrana must land at Entrana");
        ej_pass("opnpc4_varr_fly_entrana");
    }

    ej_free_npc(srv, slot_aug);
    ej_free_npc(srv, slot_tav);
    ej_free_npc(srv, slot_cast);
    ej_free_npc(srv, slot_gno);
    ej_free_npc(srv, slot_craft);
    ej_free_npc(srv, slot_varr);
    ej_free_type(srv, npc_aug);
    ej_free_type(srv, npc_tav);
    ej_free_type(srv, npc_cast);
    ej_free_type(srv, npc_gno);
    ej_free_type(srv, npc_craft);
    ej_free_type(srv, npc_varr);

    ej_pass("leftover_widget471_flight");
    ej_pass("leftover_flashmob_crowd");
    ej_pass("leftover_per_city_gate");
}

#endif /* TORIRSSERVER_TEST_QUEST_ENLIGHTENEDJOURNEY_SELFTEST_U_H */
