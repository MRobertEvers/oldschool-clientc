#ifndef TORIRSSERVER_TEST_QUEST_WHATLIESBELOW_SELFTEST_U_H
#define TORIRSSERVER_TEST_QUEST_WHATLIESBELOW_SELFTEST_U_H

/* What Lies Below Gate D C walk. Included from
 * torirs_server_world_selftest.c and invoked immediately before a
 * selftest_reset_world so spawned Rat / Surok / Zaff / outlaws /
 * surok_king cannot leak. Real OPNPC1 / OPHELDU / OPHELD3 / AI_QUEUE2
 * on the authored path. player->godmode = 1 for the whole walk (not a
 * death test — King Roald is a controlled non-lethal fight).
 *
 * Gate: TORIRSSERVER_SELFTEST_WLB_ONLY=1 (or WHATLIESBELOW)
 *
 * Reqs: Rune Mysteries complete, Runecraft 35 via stat_base (not
 * boostable). Reward tenths: Runecraft 80000, Defence 20000. Cache
 * dbrow quest_whatliesbelow awards 1 QP. No Attack row.
 *
 * Rat is [opnpc1,surok_rat] only. Zaff Talk is MERGED into
 * [opnpc1,zaff]. King letter is MERGED into [opnpc1,king_roald].
 * Chaos altar Use is MERGED into [oplocu,chaos_altar].
 *
 * Disclosed leftovers (named leftover_*.bmp, stamp-ready fixed/done):
 *   - leftover_surok_cutscene — extra palace-library flash / summoning
 *   - leftover_outlaw_camp_flavour — camp dressing not on the 5-paper path
 *
 * Required systems (jewellery IF / date_runeday / flute 282 / TK-grab)
 * do not appear on this quest path -- not leftover-stamped.
 */

#define WLB_NOT_STARTED 0
#define WLB_COLLECT_PAPERS 10
#define WLB_LETTER_TO_SUROK 20
#define WLB_WAND_TASK 30
#define WLB_LETTER_TO_RAT 50
#define WLB_SEE_ZAFF 60
#define WLB_ARREST 70
#define WLB_REPORT_RAT 80
#define WLB_COMPLETE 150

#define WLB_RM_COMPLETE 6
#define WLB_RC_REQ 35
#define WLB_PAGES_NEED 5
#define WLB_CHAOS_RUNES 15
#define WLB_RC_XP 80000
#define WLB_DEF_XP 20000
#define WLB_QP_REWARD 1
#define WLB_KING_WEAKEN_HP 5

#define WLB_STAT_DEFENCE 1
#define WLB_STAT_RUNECRAFT 20

#define WLB_RAT_X 3267
#define WLB_RAT_Z 3333
#define WLB_CAMP_X 3118
#define WLB_CAMP_Z 3474
#define WLB_SUROK_X 3208
#define WLB_SUROK_Z 3496
#define WLB_ZAFF_X 3203
#define WLB_ZAFF_Z 3433
#define WLB_KING_X 3222
#define WLB_KING_Z 3472

static void
wlb_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "WLB PASS: %s\n", step);
}

static void
wlb_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
    for( s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        worn_set(player, s, -1, 0);
}

static void
wlb_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
wlb_finish(struct ToriRSServer* srv)
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
wlb_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
wlb_pick_row(struct ToriRSServer* srv, int row)
{
    struct ToriRSServerPlayer* player;
    int uid;
    uint8_t button[6];

    assert(srv);
    player = srv->active_player;
    assert(player);
    uid = (player->resume_button_count > 0) ? player->resume_buttons[0] : wlb_chatmenu();
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
wlb_click_until_menu(struct ToriRSServer* srv, int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chatmenu;
    int clicks;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chatmenu = wlb_chatmenu();
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
wlb_tele(struct ToriRSServer* srv, int x, int z, int level)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    player->x = x;
    player->z = z;
    player->level = level;
    wlb_god(player);
    selftest_tick(srv);
}

static int
wlb_spawn(struct ToriRSServer* srv, int npc_type, int x, int z, int level)
{
    assert(srv);
    assert(npc_type > 0);
    wlb_tele(srv, x, z, level);
    return ToriRSServer_WorldNpcSpawn(srv, npc_type, x + 1, z, level);
}

static void
wlb_free_type(struct ToriRSServer* srv, int npc_type)
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
wlb_vb(struct ToriRSServer* srv, const char* name, int value)
{
    int bit;

    assert(srv);
    assert(name);
    bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
    if( bit >= 0 )
        ToriRSServer_VarbitSet(srv, bit, value);
}

static int
wlb_get_vb(struct ToriRSServerPlayer* player, const char* name)
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
wlb_quest(struct ToriRSServerPlayer* player)
{
    return wlb_get_vb(player, "surok_quest");
}

static void
wlb_set_varp(struct ToriRSServer* srv, const char* name, int value)
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
wlb_talk(struct ToriRSServer* srv, int npc_type, int slot)
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
wlb_talk_finish(struct ToriRSServer* srv, int npc_type, int slot)
{
    assert(srv);
    wlb_talk(srv, npc_type, slot);
    wlb_finish(srv);
}

static void
wlb_give(struct ToriRSServerPlayer* player, int obj_id, int count)
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
wlb_inv_total(const struct ToriRSServerPlayer* player, int obj_id)
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
wlb_set_stat(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
wlb_reset_quest(struct ToriRSServer* srv)
{
    assert(srv);
    wlb_vb(srv, "surok_quest", WLB_NOT_STARTED);
    wlb_vb(srv, "surok_foldercheck", 0);
    wlb_vb(srv, "surok_spoken", 0);
    wlb_set_varp(srv, "runemysteries", 0);
}

static void
wlb_prereqs(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    wlb_set_varp(srv, "runemysteries", WLB_RM_COMPLETE);
    wlb_set_stat(player, WLB_STAT_RUNECRAFT, WLB_RC_REQ);
}

static void
wlb_journal(struct ToriRSServer* srv, const char* step)
{
    assert(srv);
    assert(step);
    ToriRSServer_ScriptsRunProc(srv, "[proc,whatliesbelow_journal]", NULL, 0);
    wlb_finish(srv);
    wlb_pass(step);
}

static void
wlb_held1(struct ToriRSServer* srv, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_id, -1, -1);
    wlb_finish(srv);
}

static void
wlb_held3(struct ToriRSServer* srv, int obj_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD3, obj_id, -1, -1);
    wlb_finish(srv);
}

static void
wlb_heldu(struct ToriRSServer* srv, int obj_id, int use_id)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    assert(obj_id > 0);
    player = srv->active_player;
    assert(player);
    player->last_item = obj_id;
    player->last_useitem = use_id;
    player->last_useslot = 0;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_id, -1, -1);
    wlb_finish(srv);
    player->last_useitem = -1;
    player->last_useslot = -1;
}

static int
wlb_find_type(struct ToriRSServer* srv, int npc_type)
{
    int i;

    assert(srv);
    if( npc_type <= 0 )
        return -1;
    for( i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type )
            return i;
    }
    return -1;
}

static void
selftest_quest_whatliesbelow(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_rat;
    int npc_surok;
    int npc_zaff;
    int npc_king;
    int npc_roald;
    int npc_outlaw;
    int obj_paper;
    int obj_empty;
    int obj_half;
    int obj_full;
    int obj_letter1;
    int obj_letter2;
    int obj_wand;
    int obj_glow;
    int obj_diary;
    int obj_bowl;
    int obj_chaos;
    int obj_talisman;
    int obj_ring;
    int obj_instr;
    int slot_rat;
    int slot_surok;
    int slot_zaff;
    int slot_king;
    int slot_roald;
    int slot_outlaw;
    int qp_id;
    int qp_before;
    int rc_before;
    int def_before;
    int attack_before;
    int pages;

    assert(srv);
    assert(player);

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "WLB SKIP: no compiled script pack\n");
        return;
    }

    srv->members_world = 1;
    player->godmode = 1;
    wlb_god(player);
    wlb_clear_inv(player);
    wlb_reset_quest(srv);

    npc_rat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "surok_rat");
    npc_surok = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "surok_surok_type1");
    npc_zaff = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "zaff");
    npc_king = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "surok_king");
    npc_roald = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "king_roald");
    npc_outlaw = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "surok_outlaw1");
    obj_paper = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_paper");
    obj_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_rat_emptyfolder");
    obj_half = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_rat_halffolder");
    obj_full = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_rat_fullfolder");
    obj_letter1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_letter1");
    obj_letter2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_letter2");
    obj_wand = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_metalwand");
    obj_glow = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_glowingwand");
    obj_diary = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_diary");
    obj_bowl = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bowl_empty");
    obj_chaos = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chaosrune");
    obj_talisman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "chaos_talisman");
    obj_ring = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_ring");
    obj_instr = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "surok_instructions");

    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_whatliesbelow") >= 0,
                   "dbrow quest_whatliesbelow should resolve");
    SELFTEST_CHECK(ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "surok_quest") >= 0,
                   "varbit surok_quest should resolve");
    SELFTEST_CHECK(npc_rat > 0, "npc surok_rat should resolve");
    SELFTEST_CHECK(npc_surok > 0, "npc surok_surok_type1 should resolve");
    SELFTEST_CHECK(npc_zaff > 0, "npc zaff should resolve");
    SELFTEST_CHECK(npc_king > 0, "npc surok_king should resolve");
    SELFTEST_CHECK(npc_roald > 0, "npc king_roald should resolve");
    SELFTEST_CHECK(npc_outlaw > 0, "npc surok_outlaw1 should resolve");
    SELFTEST_CHECK(obj_paper > 0 && obj_empty > 0 && obj_half > 0 && obj_full > 0,
                   "paper / folders should resolve");
    SELFTEST_CHECK(obj_letter1 > 0 && obj_letter2 > 0 && obj_wand > 0 && obj_glow > 0,
                   "letters / wands should resolve");
    SELFTEST_CHECK(obj_ring > 0 && obj_instr > 0 && obj_bowl > 0 && obj_chaos > 0,
                   "ring / instructions / bowl / chaos should resolve");

    slot_rat = wlb_spawn(srv, npc_rat, WLB_RAT_X, WLB_RAT_Z, 0);
    SELFTEST_CHECK(slot_rat >= 0, "Rat should spawn");

    wlb_set_stat(player, WLB_STAT_RUNECRAFT, WLB_RC_REQ);
    wlb_talk_finish(srv, npc_rat, slot_rat);
    SELFTEST_CHECK(wlb_quest(player) == WLB_NOT_STARTED, "Rune Mysteries fail must not start");
    wlb_pass("01_rat_qualify_fail_runemysteries");
    wlb_journal(srv, "journal_00_not_started");

    wlb_set_varp(srv, "runemysteries", WLB_RM_COMPLETE);
    wlb_set_stat(player, WLB_STAT_RUNECRAFT, 1);
    wlb_talk_finish(srv, npc_rat, slot_rat);
    SELFTEST_CHECK(wlb_quest(player) == WLB_NOT_STARTED, "Runecraft 35 fail must not start");
    wlb_pass("02_rat_qualify_fail_runecraft");

    wlb_prereqs(srv, player);
    wlb_talk(srv, npc_rat, slot_rat);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 2);
    wlb_finish(srv);
    SELFTEST_CHECK(wlb_quest(player) == WLB_NOT_STARTED, "help-refuse must leave unstarted");
    wlb_pass("05_rat_refuse");

    wlb_talk(srv, npc_rat, slot_rat);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 1);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 2);
    wlb_finish(srv);
    SELFTEST_CHECK(wlb_quest(player) == WLB_NOT_STARTED, "start-refuse must leave unstarted");
    wlb_pass("07_rat_start_refuse");

    wlb_talk(srv, npc_rat, slot_rat);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 1);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 1);
    wlb_finish(srv);
    SELFTEST_CHECK(wlb_quest(player) == WLB_COLLECT_PAPERS, "accept should set collect_papers=10");
    SELFTEST_CHECK(wlb_inv_total(player, obj_empty) >= 1, "accept should grant the empty folder");
    wlb_pass("08_rat_accept");
    wlb_journal(srv, "journal_10_collect");

    wlb_held1(srv, obj_empty);
    wlb_pass("13_empty_folder_read");

    wlb_clear_inv(player);
    wlb_give(player, obj_empty, 1);
    wlb_give(player, obj_paper, WLB_PAGES_NEED);
    for( pages = 0; pages < WLB_PAGES_NEED; pages++ )
        wlb_heldu(srv, obj_paper, obj_empty);
    SELFTEST_CHECK(wlb_inv_total(player, obj_full) >= 1, "five papers should fill the folder");
    wlb_pass("20_folder_full");
    wlb_held1(srv, obj_full);
    wlb_pass("15_full_folder_read");

    slot_outlaw = wlb_spawn(srv, npc_outlaw, WLB_CAMP_X, WLB_CAMP_Z, 0);
    SELFTEST_CHECK(slot_outlaw >= 0, "outlaw should spawn");
    if( slot_outlaw >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerLastint(srv, SS_TRIGGER_AI_QUEUE3, npc_outlaw,
                                              -1, slot_outlaw, 0);
        wlb_finish(srv);
    }
    wlb_pass("21_outlaw_paper_drop");
    wlb_free_type(srv, npc_outlaw);

    wlb_tele(srv, WLB_RAT_X, WLB_RAT_Z, 0);
    wlb_talk_finish(srv, npc_rat, slot_rat);
    SELFTEST_CHECK(wlb_quest(player) == WLB_LETTER_TO_SUROK, "folder hand-in should set letter_to_surok=20");
    SELFTEST_CHECK(wlb_inv_total(player, obj_letter1) >= 1, "Rat should grant letter1");
    wlb_pass("22_rat_folder_handin");
    wlb_journal(srv, "journal_20_letter_surok");
    wlb_held1(srv, obj_letter1);
    wlb_pass("25_letter1_read");

    slot_surok = wlb_spawn(srv, npc_surok, WLB_SUROK_X, WLB_SUROK_Z, 0);
    SELFTEST_CHECK(slot_surok >= 0, "Surok should spawn");
    wlb_talk_finish(srv, npc_surok, slot_surok);
    SELFTEST_CHECK(wlb_quest(player) == WLB_WAND_TASK, "letter to Surok should set wand_task=30");
    SELFTEST_CHECK(wlb_inv_total(player, obj_wand) >= 1, "Surok should grant the metal wand");
    SELFTEST_CHECK(wlb_inv_total(player, obj_diary) >= 1, "Surok should grant the diary");
    wlb_pass("26_surok_letter");
    wlb_journal(srv, "journal_30_wand");
    wlb_held1(srv, obj_diary);
    wlb_pass("32_diary_read");

    wlb_give(player, obj_chaos, WLB_CHAOS_RUNES);
    wlb_give(player, obj_talisman, 1);
    ToriRSServer_ScriptsRunProc(srv, "[proc,wlb_infuse_wand]", NULL, 0);
    wlb_finish(srv);
    SELFTEST_CHECK(wlb_inv_total(player, obj_glow) >= 1, "altar should infuse the wand");
    wlb_pass("35_altar_infuse");

    wlb_give(player, obj_bowl, 1);
    wlb_talk_finish(srv, npc_surok, slot_surok);
    SELFTEST_CHECK(wlb_quest(player) == WLB_LETTER_TO_RAT, "wand+bowl should set letter_to_rat=50");
    SELFTEST_CHECK(wlb_inv_total(player, obj_letter2) >= 1, "Surok should grant letter2");
    wlb_pass("30_surok_wand_handin");
    wlb_journal(srv, "journal_50_letter_rat");
    wlb_held1(srv, obj_letter2);
    wlb_pass("31_letter2_read");

    slot_roald = wlb_spawn(srv, npc_roald, WLB_KING_X, WLB_KING_Z, 0);
    SELFTEST_CHECK(slot_roald >= 0, "King Roald should spawn");
    wlb_talk_finish(srv, npc_roald, slot_roald);
    SELFTEST_CHECK(wlb_quest(player) == WLB_LETTER_TO_RAT, "Roald letter talk must not consume the letter");
    wlb_pass("39_king_roald_letter");
    wlb_free_type(srv, npc_roald);

    wlb_tele(srv, WLB_RAT_X, WLB_RAT_Z, 0);
    wlb_talk(srv, npc_rat, slot_rat);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 1);
    wlb_finish(srv);
    SELFTEST_CHECK(wlb_quest(player) == WLB_SEE_ZAFF, "letter2 hand-in should set see_zaff=60");
    wlb_pass("37_rat_reveal");
    wlb_journal(srv, "journal_60_zaff");

    slot_zaff = wlb_spawn(srv, npc_zaff, WLB_ZAFF_X, WLB_ZAFF_Z, 0);
    SELFTEST_CHECK(slot_zaff >= 0, "Zaff should spawn");
    wlb_talk_finish(srv, npc_zaff, slot_zaff);
    SELFTEST_CHECK(wlb_quest(player) == WLB_ARREST, "Zaff brief should set arrest=70");
    SELFTEST_CHECK(wlb_inv_total(player, obj_ring) >= 1, "Zaff should grant the beacon ring");
    SELFTEST_CHECK(wlb_inv_total(player, obj_instr) >= 1, "Zaff should grant instructions");
    wlb_pass("41_zaff_ring");
    wlb_journal(srv, "journal_70_arrest");
    wlb_held1(srv, obj_instr);
    wlb_pass("42_instructions_read");

    wlb_tele(srv, WLB_SUROK_X, WLB_SUROK_Z, 0);
    wlb_talk(srv, npc_surok, slot_surok);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 2);
    wlb_finish(srv);
    SELFTEST_CHECK(wlb_quest(player) == WLB_ARREST, "fight refuse must stay at arrest");
    SELFTEST_CHECK(wlb_find_type(srv, npc_king) < 0, "refuse must not spawn the king");
    wlb_pass("46_surok_arrest_refuse");

    wlb_talk(srv, npc_surok, slot_surok);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 1);
    wlb_finish(srv);
    slot_king = wlb_find_type(srv, npc_king);
    SELFTEST_CHECK(slot_king >= 0, "Bring it on should spawn surok_king");
    wlb_pass("48_king_fight_start");

    if( slot_king >= 0 )
    {
        int hp_before = srv->npcs[slot_king].hitpoints;
        ToriRSServer_ScriptsRunTriggerLastint(srv, SS_TRIGGER_AI_QUEUE2, npc_king,
                                              -1, slot_king, 200);
        wlb_finish(srv);
        SELFTEST_CHECK(srv->npcs[slot_king].active != 0, "king must stay alive");
        SELFTEST_CHECK(srv->npcs[slot_king].hitpoints > 0, "king HP must stay above 0");
        SELFTEST_CHECK(srv->npcs[slot_king].hitpoints <= WLB_KING_WEAKEN_HP,
                       "lethal hit must stop at weaken HP, got %d (was %d)",
                       srv->npcs[slot_king].hitpoints, hp_before);
        SELFTEST_CHECK(wlb_get_vb(player, "surok_spoken") == 1, "weaken should set surok_spoken");
    }
    wlb_pass("49_king_weaken");

    wlb_vb(srv, "surok_spoken", 0);
    wlb_held3(srv, obj_ring);
    SELFTEST_CHECK(wlb_quest(player) == WLB_ARREST, "ring too soon must restore arrest");
    SELFTEST_CHECK(wlb_get_vb(player, "surok_spoken") == 0, "ring too soon must clear spoken");
    wlb_pass("51_ring_too_soon");

    wlb_tele(srv, WLB_SUROK_X, WLB_SUROK_Z, 0);
    wlb_free_type(srv, npc_king);
    slot_king = wlb_spawn(srv, npc_king, WLB_SUROK_X, WLB_SUROK_Z, 0);
    SELFTEST_CHECK(slot_king >= 0, "king restore spawn");
    if( slot_king >= 0 )
    {
        ToriRSServer_ScriptsRunTriggerLastint(srv, SS_TRIGGER_AI_QUEUE2, npc_king,
                                              -1, slot_king, 200);
        wlb_finish(srv);
    }
    SELFTEST_CHECK(wlb_get_vb(player, "surok_spoken") == 1, "second weaken should set spoken");
    wlb_held3(srv, obj_ring);
    SELFTEST_CHECK(wlb_quest(player) == WLB_REPORT_RAT, "ring after weaken should set report_rat=80");
    wlb_pass("52_ring_summon");
    wlb_journal(srv, "journal_80_report");
    wlb_free_type(srv, npc_king);

    wlb_tele(srv, WLB_RAT_X, WLB_RAT_Z, 0);
    wlb_talk(srv, npc_rat, slot_rat);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 2);
    wlb_finish(srv);
    SELFTEST_CHECK(wlb_quest(player) == WLB_REPORT_RAT, "Defence XP refuse must not complete");
    wlb_pass("57_rat_complete_refuse");

    qp_id = ToriRSServer_WorldVarp("qp");
    if( qp_id < 0 )
        qp_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    qp_before = (qp_id >= 0) ? player->varps[qp_id] : 0;
    rc_before = player->stat_xp_tenths[WLB_STAT_RUNECRAFT];
    def_before = player->stat_xp_tenths[WLB_STAT_DEFENCE];
    attack_before = player->stat_xp_tenths[0];

    wlb_talk(srv, npc_rat, slot_rat);
    wlb_click_until_menu(srv, 24);
    wlb_pick_row(srv, 1);
    wlb_finish(srv);
    SELFTEST_CHECK(wlb_quest(player) == WLB_COMPLETE, "accept XP should complete at 150");
    SELFTEST_CHECK(player->stat_xp_tenths[WLB_STAT_RUNECRAFT] >= rc_before + WLB_RC_XP,
                   "complete should award 80000 Runecraft tenths");
    SELFTEST_CHECK(player->stat_xp_tenths[WLB_STAT_DEFENCE] >= def_before + WLB_DEF_XP,
                   "complete should award 20000 Defence tenths");
    SELFTEST_CHECK(player->stat_xp_tenths[0] == attack_before,
                   "complete must not award Attack XP");
    if( qp_id >= 0 )
        SELFTEST_CHECK(player->varps[qp_id] >= qp_before + WLB_QP_REWARD,
                       "complete should award dbrow quest points");
    wlb_pass("58_complete_scroll");
    wlb_journal(srv, "journal_150_complete");

    wlb_talk_finish(srv, npc_rat, slot_rat);
    wlb_pass("59_rat_after");

    ToriRSServer_ScriptsRunProc(srv, "[proc,leftover_surok_cutscene]", NULL, 0);
    wlb_finish(srv);
    wlb_pass("leftover_surok_cutscene");
    ToriRSServer_ScriptsRunProc(srv, "[proc,leftover_outlaw_camp_flavour]", NULL, 0);
    wlb_finish(srv);
    wlb_pass("leftover_outlaw_camp_flavour");

    wlb_free_type(srv, npc_rat);
    wlb_free_type(srv, npc_surok);
    wlb_free_type(srv, npc_zaff);
    wlb_free_type(srv, npc_king);
    wlb_free_type(srv, npc_roald);
    wlb_free_type(srv, npc_outlaw);
    wlb_clear_inv(player);
    wlb_reset_quest(srv);
}

#endif
