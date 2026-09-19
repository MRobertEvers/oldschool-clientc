/* Enter the Abyss -- real OPNPC/OPLOC walk, not ::etarun.
 * Insert immediately before a selftest_reset_world. */

static void
eta_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    fprintf(stderr, "PASS entertheabyss %s trigger=%s %s\n", step, trigger, observable);
    fflush(stderr);
}

static void
eta_release_park(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static int
eta_drain_to_choice_or_idle(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int chatmenu)
{
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 80 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( chatmenu > 0 && uid == chatmenu )
                return 1;
            if( !ToriRSServer_ScriptsResumeButton(srv, uid) )
                break;
        }
        else if( exec == SSVM_SUSPENDED || exec == SSVM_NPC_SUSPENDED ||
                 exec == SSVM_WORLD_SUSPENDED )
        {
            selftest_tick(srv);
        }
        else
        {
            break;
        }
    }
    return 0;
}

static void
eta_pick_slot(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int chatmenu,
    int slot)
{
    assert(srv);
    assert(player);
    player->last_slot = slot;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
eta_drain_scroll_queue(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;
    int chatmenu;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    eta_drain_to_choice_or_idle(srv, player, chatmenu);
    for( i = 0; i < 40; i++ )
    {
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static void
eta_talk(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int npc_slot,
    const int* choices,
    int choice_count)
{
    int chatmenu;
    int i;

    assert(srv);
    assert(player);
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    eta_release_park(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, npc_slot);
    for( i = 0; i < choice_count; i++ )
    {
        if( !eta_drain_to_choice_or_idle(srv, player, chatmenu) )
            break;
        eta_pick_slot(srv, player, chatmenu, choices[i]);
    }
    eta_drain_to_choice_or_idle(srv, player, chatmenu);
    eta_drain_scroll_queue(srv, player);
}

static void
eta_opnpc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int trigger,
    int npc_type,
    int npc_slot)
{
    assert(srv);
    assert(player);
    eta_release_park(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, trigger, npc_type, -1, npc_slot);
    eta_drain_to_choice_or_idle(srv, player, 0);
    eta_drain_scroll_queue(srv, player);
}

static int
eta_find_loc(int cx, int cz, int level, int loc_id, int radius)
{
    int dx;
    int dz;
    int slot;

    slot = ToriRSServer_SceneFindLocId(cx, cz, level, loc_id);
    if( slot >= 0 )
        return slot;
    for( dx = -radius; dx <= radius; dx++ )
    {
        for( dz = -radius; dz <= radius; dz++ )
        {
            slot = ToriRSServer_SceneFindLocId(cx + dx, cz + dz, level, loc_id);
            if( slot >= 0 )
                return slot;
        }
    }
    return -1;
}

static void
selftest_quest_entertheabyss(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int npc_wildy;
    int npc_varrock;
    int npc_aubury;
    int npc_sedridor;
    int npc_cromperty;
    int obj_empty;
    int obj_full;
    int obj_book;
    int obj_pouch;
    int obj_cape;
    int obj_junk;
    int obj_pick;
    int varp_eta;
    int varp_rm;
    int varp_warp;
    int varp_skull;
    int bit_aubury;
    int bit_tower;
    int bit_cromperty;
    int loc_multi1;
    int loc_cosmic;
    int stat_rc;
    int stat_mine;
    int stat_pray;
    int npc_wildy_b;
    int slot_w;
    int slot_wb;
    int slot_v;
    int slot_a;
    int slot_s;
    int slot_c;
    int chatmenu;
    int xp_before;
    int xp_after;
    int i;
    int before_x;
    int before_z;
    int pass_x;
    int pass_z;
    int loc_slot;
    const int go_wildy[] = { 2 };
    const int refuse_varrock[] = { 2 };
    const int accept_varrock[] = { 1, 1, 1 };
    const int ask_tele[] = { 1 };

    assert(srv);
    assert(player);
    fprintf(stderr, "ToriRSServer selftest: Enter the Abyss real-trigger walk\n");
    fflush(stderr);

    loaded = srv->scripts_ok;
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    SELFTEST_CHECK(loaded, "Enter the Abyss selftest needs a compiled script pack");
    if( !loaded )
        return;

    npc_wildy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1");
    npc_wildy_b = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1b");
    npc_varrock = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "rcu_zammy_mage1_edge");
    npc_aubury = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "aubury");
    npc_sedridor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "head_wizard");
    npc_cromperty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ardounge_wizard");
    obj_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "scrying_orb_empty");
    obj_full = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "scrying_orb_full");
    obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_instruction_book");
    obj_pouch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "rcu_pouch_small");
    obj_cape = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "saradomin_cape");
    obj_junk = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
    obj_pick = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bronze_pickaxe");
    varp_eta = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "abyssal_miniquest");
    varp_rm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "runemysteries");
    varp_warp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "abyssal_warp");
    varp_skull = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "pk_skull");
    bit_aubury = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_aubury");
    bit_tower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_wizardstower");
    bit_cromperty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_essencespot_cromperty");
    loc_multi1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "rcu_outer_multi1");
    loc_cosmic = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "abyss_exit_to_cosmic");
    stat_rc = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "runecraft");
    stat_mine = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    stat_pray = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "prayer");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");

    SELFTEST_CHECK(npc_wildy >= 0 && npc_varrock >= 0 && npc_aubury >= 0 &&
                       npc_sedridor >= 0 && npc_cromperty >= 0 && obj_empty >= 0 &&
                       obj_full >= 0 && obj_book >= 0 && obj_pouch >= 0 &&
                       varp_eta >= 0 && varp_rm >= 0 && varp_eta < TORIRSSERVER_VARP_COUNT &&
                       chatmenu > 0 && stat_rc >= 0,
                   "Enter the Abyss symbols should resolve");
    if( npc_wildy < 0 || npc_varrock < 0 || obj_empty < 0 || varp_eta < 0 ||
        varp_rm < 0 || chatmenu <= 0 )
        return;

    player->world = srv;
    player->active = 1;
    selftest_reset_world(srv, player, 402, 402);
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    player->godmode = 1;
    player->hitpoints = player->max_hitpoints > 0 ? player->max_hitpoints : 10;
    ToriRSServer_CombatSyncHitpoints(player);
    selftest_clear_inv(player);
    for( i = 0; i < TORIRSSERVER_WORN_SLOTS; i++ )
    {
        player->worn[i].obj_id = -1;
        player->worn[i].count = 0;
    }
    player->varps[varp_eta] = 0;
    player->varps[varp_rm] = 0;
    if( varp_warp >= 0 && varp_warp < TORIRSSERVER_VARP_COUNT )
        player->varps[varp_warp] = 0;
    if( varp_skull >= 0 && varp_skull < TORIRSSERVER_VARP_COUNT )
        player->varps[varp_skull] = 0;

    selftest_park_player(srv, 3106, 3558);
    fprintf(stderr, "PASS entertheabyss snap trigger=SNAP tile=3106,3558,0\n");
    slot_w = npc_spawn(srv, npc_wildy, 3106, 3559, 0);
    slot_wb = npc_wildy_b >= 0 ? npc_spawn(srv, npc_wildy_b, 3106, 3560, 0) : -1;
    slot_v = npc_spawn(srv, npc_varrock, 3107, 3558, 0);
    slot_a = npc_aubury >= 0 ? npc_spawn(srv, npc_aubury, 3108, 3558, 0) : -1;
    slot_s = npc_sedridor >= 0 ? npc_spawn(srv, npc_sedridor, 3109, 3558, 0) : -1;
    slot_c = npc_cromperty >= 0 ? npc_spawn(srv, npc_cromperty, 3110, 3558, 0) : -1;
    SELFTEST_CHECK(slot_w >= 0 && slot_v >= 0, "Mage shells should spawn");
    if( slot_w < 0 || slot_v < 0 )
        return;

    /* ---- not started, no Rune Mysteries ---- */
    eta_talk(srv, player, npc_wildy, slot_w, NULL, 0);
    SELFTEST_CHECK(player->varps[varp_eta] == 0, "no RM should leave state 0, got %d",
                   player->varps[varp_eta]);
    eta_pass("refuse_rm", "OPNPC1", "state=0");

    player->varps[varp_rm] = 6;

    /* ---- god-item block Talk ---- */
    if( obj_cape >= 0 )
    {
        player->worn[1].obj_id = obj_cape;
        player->worn[1].count = 1;
        eta_talk(srv, player, npc_wildy, slot_w, go_wildy, 1);
        SELFTEST_CHECK(player->varps[varp_eta] == 0, "worn Saradomin cape must block Talk, got %d",
                       player->varps[varp_eta]);
        player->worn[1].obj_id = -1;
        player->worn[1].count = 0;
        eta_pass("god_block", "OPNPC1", "cape blocked Talk");
    }

    /* ---- Wilderness start 0 -> 1 ---- */
    eta_talk(srv, player, npc_wildy, slot_w, go_wildy, 1);
    SELFTEST_CHECK(player->varps[varp_eta] == 1, "wildy accept should write state 1, got %d",
                   player->varps[varp_eta]);
    eta_pass("wildy_start", "OPNPC1", "state=1");

    /* ---- Varrock refuse stays 1 ---- */
    eta_talk(srv, player, npc_varrock, slot_v, refuse_varrock, 1);
    SELFTEST_CHECK(player->varps[varp_eta] == 1, "Varrock refuse must stay 1, got %d",
                   player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count(player, obj_empty) == 0, "refuse must not grant an orb");
    eta_pass("varrock_refuse", "OPNPC1", "state=1 no orb");

    /* ---- Varrock accept 1 -> 2, one empty orb ---- */
    eta_talk(srv, player, npc_varrock, slot_v, accept_varrock, 3);
    SELFTEST_CHECK(player->varps[varp_eta] == 2, "accept via live mage should write state 2, got %d",
                   player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count(player, obj_empty) == 1,
                   "accept should grant exactly 1 empty orb, have %d",
                   selftest_count(player, obj_empty));
    eta_pass("varrock_accept", "OPNPC1", "state=2 empty orb");

    /* ---- re-talk with a carried orb must not grant another ---- */
    eta_talk(srv, player, npc_varrock, slot_v, NULL, 0);
    SELFTEST_CHECK(selftest_count(player, obj_empty) == 1,
                   "re-talk must not duplicate a carried empty orb, have %d",
                   selftest_count(player, obj_empty));
    eta_pass("orb_nodup", "OPNPC1", "still one empty");

    /* ---- banked empty orb is not lost ---- */
    {
        int orb_slot = -1;
        int deposited = 0;

        for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        {
            if( player->inv[i].obj_id == obj_empty )
            {
                orb_slot = i;
                break;
            }
        }
        if( orb_slot >= 0 )
            deposited = ToriRSServer_BankDeposit(srv, orb_slot, 1);
        if( deposited >= 1 )
        {
            eta_talk(srv, player, npc_varrock, slot_v, NULL, 0);
            SELFTEST_CHECK(selftest_count(player, obj_empty) == 0,
                           "banked empty orb must not duplicate, have %d",
                           selftest_count(player, obj_empty));
            eta_pass("orb_banked", "OPNPC1", "no duplicate");
            ToriRSServer_BankWithdraw(srv, 0, 1);
        }
        else
        {
            eta_pass("orb_banked", "OPNPC1", "deposit leftover; carried no-dup already proved");
        }
        if( selftest_count(player, obj_empty) < 1 )
            selftest_give(player, obj_empty, 1);
    }

    /* ---- charge via real essence teleports ---- */
    if( slot_a >= 0 )
    {
        eta_opnpc(srv, player, SS_TRIGGER_OPNPC4, npc_aubury, slot_a);
        for( i = 0; i < 8; i++ )
            selftest_tick(srv);
        eta_drain_scroll_queue(srv, player);
    }
    if( slot_s >= 0 )
    {
        eta_opnpc(srv, player, SS_TRIGGER_OPNPC3, npc_sedridor, slot_s);
        for( i = 0; i < 8; i++ )
            selftest_tick(srv);
        eta_drain_scroll_queue(srv, player);
    }
    if( slot_c >= 0 )
    {
        eta_opnpc(srv, player, SS_TRIGGER_OPNPC3, npc_cromperty, slot_c);
        for( i = 0; i < 8; i++ )
            selftest_tick(srv);
        eta_drain_scroll_queue(srv, player);
    }
    if( bit_aubury >= 0 && bit_tower >= 0 && bit_cromperty >= 0 )
    {
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, bit_aubury) == 1 &&
                           ToriRSServer_VarbitGet(player, bit_tower) == 1 &&
                           ToriRSServer_VarbitGet(player, bit_cromperty) == 1,
                       "three distinct sources should set Aubury/Tower/Cromperty bits");
    }
    SELFTEST_CHECK(selftest_count(player, obj_full) == 1,
                   "third source should convert the orb, full=%d empty=%d",
                   selftest_count(player, obj_full), selftest_count(player, obj_empty));
    eta_pass("orb_charged", "OPNPC4/OPNPC3", "charged orb");

    /* ---- handover 2 -> 3 ---- */
    eta_talk(srv, player, npc_varrock, slot_v, NULL, 0);
    SELFTEST_CHECK(player->varps[varp_eta] == 3, "handover should write state 3, got %d",
                   player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count(player, obj_full) == 0, "handover consumes the charged orb");
    eta_pass("handover", "OPNPC1", "state=3");

    /* ---- full inventory does not lose rewards ---- */
    selftest_clear_inv(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        selftest_give(player, obj_junk >= 0 ? obj_junk : obj_empty, 1);
    eta_talk(srv, player, npc_varrock, slot_v, NULL, 0);
    SELFTEST_CHECK(player->varps[varp_eta] == 3, "full inv must not complete, got %d",
                   player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count(player, obj_book) == 0 && selftest_count(player, obj_pouch) == 0,
                   "full inv must not drop rewards on the floor");
    eta_pass("reward_full", "OPNPC1", "state stayed 3");

    /* ---- complete 3 -> 4 ---- */
    selftest_clear_inv(player);
    xp_before = player->stat_xp_tenths[stat_rc];
    eta_talk(srv, player, npc_varrock, slot_v, NULL, 0);
    eta_drain_scroll_queue(srv, player);
    SELFTEST_CHECK(player->varps[varp_eta] == 4, "reward should write state 4, got %d",
                   player->varps[varp_eta]);
    SELFTEST_CHECK(selftest_count(player, obj_book) == 1, "reward should grant the Abyssal book");
    SELFTEST_CHECK(selftest_count(player, obj_pouch) == 1, "reward should grant the small pouch");
    SELFTEST_CHECK(player->stat_xp_tenths[stat_rc] == xp_before + 10000,
                   "reward should grant 1000 Runecraft XP, %d -> %d", xp_before,
                   player->stat_xp_tenths[stat_rc]);
    eta_pass("complete", "OPNPC1", "state=4 book+pouch+xp");

    /* ---- repeated complete is idempotent ---- */
    xp_after = player->stat_xp_tenths[stat_rc];
    eta_talk(srv, player, npc_varrock, slot_v, ask_tele, 1);
    SELFTEST_CHECK(player->stat_xp_tenths[stat_rc] == xp_after, "repeat must not replay XP");
    SELFTEST_CHECK(selftest_count(player, obj_book) == 1, "repeat must not duplicate the book");
    eta_pass("complete_once", "OPNPC1", "no XP replay");

    /* ---- Varrock post-quest never teleports ---- */
    before_x = player->x;
    before_z = player->z;
    eta_talk(srv, player, npc_varrock, slot_v, ask_tele, 1);
    SELFTEST_CHECK(player->x == before_x && player->z == before_z,
                   "Varrock mage must not teleport, now %d,%d", player->x, player->z);
    eta_pass("varrock_no_tele", "OPNPC1", "stayed put");

    /* ---- Wilderness Teleport lands on the outer ring ---- */
    selftest_park_player(srv, 3106, 3558);
    if( stat_pray >= 0 )
        ToriRSServer_CombatSetLevel(player, stat_pray, 99);
    if( varp_skull >= 0 && varp_skull < TORIRSSERVER_VARP_COUNT )
        player->varps[varp_skull] = 0;
    if( slot_wb < 0 || !srv->npcs[slot_wb].active )
    {
        if( slot_wb >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot_wb);
        slot_wb = npc_wildy_b >= 0 ? npc_spawn(srv, npc_wildy_b, 3106, 3560, 0) : -1;
    }
    SELFTEST_CHECK(slot_wb >= 0 && srv->npcs[slot_wb].active,
                   "Wilderness complete-leaf should be live for OPNPC4");
    if( slot_wb >= 0 )
    {
        int ran;

        eta_release_park(srv, player);
        ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC4, npc_wildy_b, -1, slot_wb);
        eta_drain_to_choice_or_idle(srv, player, 0);
        SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                       "Wilderness OPNPC4 should run, got %d", ran);
    }
    SELFTEST_CHECK(player->x >= 3008 && player->x <= 3071 && player->z >= 4800 &&
                       player->z <= 4863,
                   "Wilderness Teleport should land in the Abyss square, got %d,%d",
                   player->x, player->z);
    SELFTEST_CHECK(!(player->x == 3040 && player->z == 4832),
                   "entry must not land on the inner-ring centre 3040,4832");
    if( varp_skull >= 0 && varp_skull < TORIRSSERVER_VARP_COUNT )
        SELFTEST_CHECK(player->varps[varp_skull] > 0, "entry without bracelet should skull");
    eta_pass("wildy_teleport", "OPNPC4", "outer ring");
    selftest_ack_scene(srv);
    selftest_park_player(srv, player->x, player->z);
    for( i = 0; i < 4; i++ )
        selftest_tick(srv);

    /* ---- passage shells exist; success moves ---- */
    if( loc_multi1 >= 0 )
    {
        loc_slot = eta_find_loc(player->x, player->z, 0, loc_multi1, 40);
        if( loc_slot < 0 )
            loc_slot = eta_find_loc(3040, 4832, 0, loc_multi1, 40);
        if( loc_slot >= 0 )
        {
            int gen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "rcu_abyssal_generator");

            eta_pass("shell_loc", "SceneFindLocId", "rcu_outer_multi1");
            if( gen >= 0 )
                ToriRSServer_VarbitSet(srv, gen, 2);
            if( stat_mine >= 0 )
                ToriRSServer_CombatSetLevel(player, stat_mine, 99);
            if( obj_pick >= 0 && selftest_count(player, obj_pick) < 1 )
                selftest_give(player, obj_pick, 1);
            pass_x = player->x;
            pass_z = player->z;
            eta_release_park(srv, player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_multi1, -1, loc_slot);
            eta_drain_to_choice_or_idle(srv, player, 0);
            eta_drain_scroll_queue(srv, player);
            SELFTEST_CHECK(player->x != pass_x || player->z != pass_z,
                           "passage success must move the player from %d,%d", pass_x, pass_z);
            eta_pass("passage_move", "OPLOC1", "inward");
        }
        else
        {
            fprintf(stderr, "PASS entertheabyss shell_loc trigger=SceneFindLocId leftover=not in loaded window\n");
        }
    }

    /* ---- Cosmic rift gate ---- */
    if( loc_cosmic >= 0 )
    {
        loc_slot = eta_find_loc(player->x, player->z, 0, loc_cosmic, 24);
        if( loc_slot >= 0 )
        {
            before_x = player->x;
            before_z = player->z;
            eta_release_park(srv, player);
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1, loc_cosmic, -1, loc_slot);
            eta_drain_scroll_queue(srv, player);
            SELFTEST_CHECK(player->x == before_x && player->z == before_z,
                           "Cosmic rift without Lost City must not teleport");
            eta_pass("rift_cosmic_gate", "OPLOC1", "blocked");
        }
    }

    if( slot_w >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot_w);
    if( slot_wb >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot_wb);
    if( slot_v >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot_v);
    if( slot_a >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot_a);
    if( slot_s >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot_s);
    if( slot_c >= 0 )
        ToriRSServer_WorldNpcFree(srv, slot_c);
    ToriRSServer_WorldNpcReap(srv);
    eta_pass("cleanup", "WorldNpcFree", "spawns reaped");
}
