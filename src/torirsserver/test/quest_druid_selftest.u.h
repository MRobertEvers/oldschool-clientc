/* Druidic Ritual -- real-trigger selftest (OPNPC1 / OPLOCU / OPHELD1).
 * Included from torirs_server_world_selftest.c after the shared helpers. */

static int g_quest_druid_pass_failures;

static void
quest_druid_pass_begin(void)
{
    g_quest_druid_pass_failures = g_selftest_failures;
}

static void
quest_druid_pass(const char* step)
{
    assert(step);
    if( g_selftest_failures == g_quest_druid_pass_failures )
        fprintf(stderr, "ToriRSServer selftest: quest_druid PASS %s\n", step);
    g_quest_druid_pass_failures = g_selftest_failures;
}

static void
quest_druid_set_combat(
    struct ToriRSServerPlayer* player,
    int level)
{
    int i;

    assert(player);
    assert(level > 0);
    for( i = 0; i < 7; i++ )
    {
        player->stat_level[i] = level;
        player->stat_boosted[i] = level;
    }
    player->stat_level[TORIRSSERVER_STAT_HITPOINTS] = level < 10 ? 10 : level;
    player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS] = player->stat_level[TORIRSSERVER_STAT_HITPOINTS];
    player->hitpoints = player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS];
    player->max_hitpoints = player->hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static void
quest_druid_resume_pages(
    struct ToriRSServer* srv,
    int choice_slot,
    int max_pages)
{
    struct ToriRSServerPlayer* player;
    int chat_left;
    int chat_right;
    int messagebox;
    int chatmenu;
    int n;

    assert(srv);
    player = srv->active_player;
    assert(player);
    chat_left = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chat_left:continue");
    chat_right = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chat_right:continue");
    messagebox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "messagebox:continue");
    chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    for( n = 0; n < max_pages && player->active_script; n++ )
    {
        if( chatmenu > 0 && player->resume_button_count > 0 &&
            player->resume_buttons[0] == chatmenu )
        {
            if( choice_slot <= 0 )
                return;
            player->last_slot = choice_slot;
            ToriRSServer_ScriptsResumeButton(srv, chatmenu);
            continue;
        }
        if( chat_left > 0 && ToriRSServer_ScriptsResumeButton(srv, chat_left) )
            continue;
        if( chat_right > 0 && ToriRSServer_ScriptsResumeButton(srv, chat_right) )
            continue;
        if( messagebox > 0 && ToriRSServer_ScriptsResumeButton(srv, messagebox) )
            continue;
        break;
    }
}

static void
quest_druid_drain_to_choice(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int choice_uid)
{
    assert(srv);
    assert(player);
    (void)choice_uid;
    (void)player;
    quest_druid_resume_pages(srv, 0, 24);
}

static void
quest_druid_pick(
    struct ToriRSServer* srv,
    int row)
{
    assert(srv);
    quest_druid_resume_pages(srv, row, 4);
}

static void
quest_druid_resume_then_close(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    (void)player;
    /* Resume pause FIRST so a queue() after ~chatnpc / ~mesbox is armed
     * before WorldCloseModal aborts the parked script. */
    quest_druid_resume_pages(srv, 1, 32);
    for( i = 0; i < 40; i++ )
    {
        quest_druid_resume_pages(srv, 1, 8);
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static int
quest_druid_inv_slot(
    struct ToriRSServerPlayer* player,
    int obj_id)
{
    int i;

    assert(player);
    for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        if( player->inv[i].obj_id == obj_id && player->inv[i].count > 0 )
            return i;
    }
    return -1;
}

static void
quest_druid_free_spawns(
    struct ToriRSServer* srv,
    int* slots,
    int slot_count)
{
    int i;

    assert(srv);
    assert(slots);
    for( i = 0; i < slot_count; i++ )
    {
        if( slots[i] >= 0 && srv->npcs[slots[i]].active )
            ToriRSServer_WorldNpcFree(srv, slots[i]);
        slots[i] = -1;
    }
    ToriRSServer_WorldNpcReap(srv);
}

static void
selftest_quest_druid(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;
    int varp_druid;
    int varp_qp;
    int varp_osf;
    int npc_kaq;
    int npc_sanfew;
    int loc_cauldron;
    int obj_raw[4];
    int obj_enc[4];
    int obj_guam_grimy;
    int obj_guam;
    int obj_vial;
    int obj_horn;
    int obj_dust;
    int obj_pestle;
    int rows_uid;
    int kaq_slot = -1;
    int sanfew_slot = -1;
    int suit_slot = -1;
    int loc_slot = -1;
    int herb_stat;
    int qp_before;
    int xp_before;
    int snapped;
    int i;
    static const char* raw_names[4] = {
        "raw_rat_meat", "raw_beef", "raw_bear_meat", "raw_chicken"
    };
    static const char* enc_names[4] = {
        "enchanted_rat_meat", "enchanted_beef", "enchanted_bear_meat",
        "enchanted_chicken"
    };
    int spawn_slots[3];

    assert(srv);
    assert(player);
    quest_druid_pass_begin();
    fprintf(stderr, "ToriRSServer selftest: quest_druid\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }
    selftest_park_player(srv, 3222, 3218);

    varp_druid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "druidquest");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    varp_osf = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "onesmallfavour");
    npc_kaq = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "kaqemeex");
    npc_sanfew = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "sanfew");
    loc_cauldron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "cauldron_of_thunder");
    obj_guam_grimy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unidentified_guam");
    obj_guam = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "guam_leaf");
    obj_vial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "vial_water");
    obj_horn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unicorn_horn");
    obj_dust = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "unicorn_horn_dust");
    obj_pestle = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pestle_and_mortar");
    rows_uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
    herb_stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "herblore");
    if( herb_stat < 0 )
        herb_stat = 15;
    for( i = 0; i < 4; i++ )
    {
        obj_raw[i] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, raw_names[i]);
        obj_enc[i] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, enc_names[i]);
    }

    SELFTEST_CHECK(varp_druid >= 0 && npc_kaq > 0 && npc_sanfew > 0 &&
                       loc_cauldron > 0 && rows_uid > 0,
                   "quest_druid names should resolve");
    if( varp_druid < 0 || npc_kaq <= 0 || npc_sanfew <= 0 || loc_cauldron <= 0 ||
        rows_uid <= 0 )
        return;

    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    player->varps[varp_druid] = 0;
    if( varp_osf >= 0 )
        player->varps[varp_osf] = 0;
    quest_druid_set_combat(player, 1);
    srv->members_world = 1;

    /* Herblore locked before start -- real OPHELD1 (category grimy_herb). */
    {
        const struct ToriRSServerObjInfo* grimy_info;
        int cat_grimy;

        cat_grimy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_CATEGORY, "grimy_herb");
        grimy_info = obj_guam_grimy > 0 ? ToriRSServer_ObjInfo(obj_guam_grimy) : NULL;
        if( grimy_info && grimy_info->category > 0 )
            cat_grimy = grimy_info->category;
        selftest_give(player, obj_guam_grimy, 1);
        player->last_slot = quest_druid_inv_slot(player, obj_guam_grimy);
        SELFTEST_CHECK(player->last_slot >= 0, "grimy guam should occupy a slot");
        /* Packet path first: WorldHandle looks up the obj's category so
         * [opheld1,_grimy_herb] actually fires. ScriptsRunTrigger with
         * category=-1 would miss that rung. */
        selftest_opheld(srv, 1, player->last_slot);
        if( selftest_count(player, obj_guam) == 0 && cat_grimy > 0 )
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_guam_grimy,
                                           cat_grimy, -1);
        SELFTEST_CHECK(selftest_count(player, obj_guam) == 0,
                       "OPHELD1 clean must refuse before state 4");
        quest_druid_pass("herblore-locked-clean");
        selftest_clear_inv(player);
    }

    selftest_give(player, obj_guam, 1);
    selftest_give(player, obj_vial, 1);
    player->last_slot = quest_druid_inv_slot(player, obj_vial);
    player->last_item = obj_vial;
    player->last_useslot = quest_druid_inv_slot(player, obj_guam);
    player->last_useitem = obj_guam;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_vial, -1, -1);
    SELFTEST_CHECK(selftest_count(player, ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "guamvial")) == 0,
                   "OPHELDU brew must refuse before state 4");
    quest_druid_pass("herblore-locked-brew");
    selftest_clear_inv(player);

    if( obj_pestle > 0 && obj_horn > 0 )
    {
        selftest_give(player, obj_pestle, 1);
        selftest_give(player, obj_horn, 1);
        player->last_useitem = obj_horn;
        player->last_useslot = quest_druid_inv_slot(player, obj_horn);
        player->last_slot = quest_druid_inv_slot(player, obj_pestle);
        player->last_item = obj_pestle;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_pestle, -1, -1);
        SELFTEST_CHECK(obj_dust < 0 || selftest_count(player, obj_dust) == 0,
                       "OPHELDU grind must refuse before state 4");
        quest_druid_pass("herblore-locked-grind");
        selftest_clear_inv(player);
    }

    kaq_slot = ToriRSServer_WorldNpcSpawn(srv, npc_kaq, player->x + 1, player->z, player->level);
    sanfew_slot = ToriRSServer_WorldNpcSpawn(srv, npc_sanfew, player->x + 2, player->z, player->level);
    spawn_slots[0] = kaq_slot;
    spawn_slots[1] = sanfew_slot;
    spawn_slots[2] = -1;
    SELFTEST_CHECK(kaq_slot >= 0 && sanfew_slot >= 0, "kaqemeex and sanfew should spawn");
    if( kaq_slot < 0 || sanfew_slot < 0 )
    {
        quest_druid_free_spawns(srv, spawn_slots, 2);
        return;
    }

    /* Refuse. Opening choice row 2 = "I'm in search of a quest."; offer row 2 = No. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kaq, -1,
                                                 kaq_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,kaqemeex] should run");
    quest_druid_drain_to_choice(srv, player, rows_uid);
    quest_druid_pick(srv, 2);
    quest_druid_drain_to_choice(srv, player, rows_uid);
    quest_druid_pick(srv, 2);
    quest_druid_resume_then_close(srv, player);
    SELFTEST_CHECK(player->varps[varp_druid] == 0, "refuse must leave druidquest=0");
    quest_druid_pass("kaqemeex-refuse");

    selftest_clear_pending(srv, player);
    player->varps[varp_druid] = 0;

    /* Accept at combat 3, through the advisory warning. */
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kaq, -1,
                                                 kaq_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,kaqemeex] accept should run");
    quest_druid_drain_to_choice(srv, player, rows_uid);
    quest_druid_pick(srv, 2);
    quest_druid_drain_to_choice(srv, player, rows_uid);
    quest_druid_pick(srv, 1);
    quest_druid_drain_to_choice(srv, player, rows_uid);
    if( player->active_script && player->resume_button_count > 0 &&
        player->resume_buttons[0] == rows_uid )
        quest_druid_pick(srv, 1);
    quest_druid_resume_then_close(srv, player);
    SELFTEST_CHECK(player->varps[varp_druid] == 1, "accept must write druidquest=1");
    snapped = player->varps[varp_druid];
    selftest_tick(srv);
    SELFTEST_CHECK(player->varps[varp_druid] == snapped,
                   "SNAP+tick must keep druidquest=1");
    quest_druid_pass("kaqemeex-accept");

    /* Sanfew assignment. */
    selftest_clear_pending(srv, player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sanfew, -1,
                                                 sanfew_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,sanfew] should run");
    quest_druid_drain_to_choice(srv, player, rows_uid);
    quest_druid_pick(srv, 1);
    quest_druid_drain_to_choice(srv, player, rows_uid);
    quest_druid_pick(srv, 2);
    quest_druid_resume_then_close(srv, player);
    SELFTEST_CHECK(player->varps[varp_druid] == 2, "Sanfew assignment must write druidquest=2");
    quest_druid_pass("sanfew-assign");

    /* Cauldron refuses at state 0, then converts all four at state 2. */
    loc_slot = ToriRSServer_SceneAddLoc(player->x, player->z + 1, player->level, loc_cauldron,
                                       10, 0);
    SELFTEST_CHECK(loc_slot >= 0, "cauldron_of_thunder should place");
    player->varps[varp_druid] = 0;
    selftest_clear_inv(player);
    selftest_give(player, obj_raw[0], 1);
    player->last_useitem = obj_raw[0];
    player->last_useslot = quest_druid_inv_slot(player, obj_raw[0]);
    if( loc_slot >= 0 )
        ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cauldron,
                                            ToriRSServer_LocCategory(loc_cauldron), loc_slot);
    SELFTEST_CHECK(selftest_count(player, obj_enc[0]) == 0,
                   "cauldron must refuse outside state 2");
    quest_druid_pass("cauldron-refuse");

    player->varps[varp_druid] = 2;
    selftest_clear_inv(player);
    for( i = 0; i < 4; i++ )
    {
        selftest_give(player, obj_raw[i], 1);
        player->last_useitem = obj_raw[i];
        player->last_useslot = quest_druid_inv_slot(player, obj_raw[i]);
        if( loc_slot >= 0 )
            ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOCU, loc_cauldron,
                                                ToriRSServer_LocCategory(loc_cauldron), loc_slot);
        SELFTEST_CHECK(selftest_count(player, obj_enc[i]) == 1 &&
                           selftest_count(player, obj_raw[i]) == 0,
                       "cauldron must convert %s one-for-one", raw_names[i]);
    }
    quest_druid_pass("cauldron-four-meats");

    /* Sanfew hand-in. */
    selftest_clear_pending(srv, player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_sanfew, -1,
                                                 sanfew_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,sanfew] hand-in should run");
    quest_druid_resume_then_close(srv, player);
    SELFTEST_CHECK(player->varps[varp_druid] == 3, "hand-in must write druidquest=3");
    for( i = 0; i < 4; i++ )
        SELFTEST_CHECK(selftest_count(player, obj_enc[i]) == 0,
                       "hand-in must consume %s", enc_names[i]);
    quest_druid_pass("sanfew-handin");

    /* Lesson then complete. Resume pause FIRST, then wide close+tick. */
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    xp_before = player->stat_xp_tenths[herb_stat];
    selftest_clear_pending(srv, player);
    SELFTEST_CHECK(player->varps[varp_druid] == 3,
                   "lesson must start at druidquest=3, not 4");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kaq, -1,
                                                 kaq_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "[opnpc1,kaqemeex] lesson should run");
    SELFTEST_CHECK(player->varps[varp_druid] == 3,
                   "lesson must not write complete before the last page");
    quest_druid_resume_then_close(srv, player);
    SELFTEST_CHECK(player->varps[varp_druid] == 4, "completion must follow the lesson");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 4, "completion pays 4 QP");
    SELFTEST_CHECK(player->stat_xp_tenths[herb_stat] >= xp_before + 2500,
                   "completion pays 250 Herblore XP");
    quest_druid_pass("lesson-then-complete");

    /* Duplicate completion is a no-op. */
    qp_before = varp_qp >= 0 ? player->varps[varp_qp] : 0;
    xp_before = player->stat_xp_tenths[herb_stat];
    selftest_clear_pending(srv, player);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_kaq, -1,
                                                 kaq_slot) == TORIRSSERVER_TRIGGER_RAN,
                   "postquest talk should run");
    quest_druid_drain_to_choice(srv, player, rows_uid);
    quest_druid_pick(srv, 1);
    quest_druid_resume_then_close(srv, player);
    SELFTEST_CHECK(player->varps[varp_druid] == 4, "postquest must not rewind state");
    if( varp_qp >= 0 )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before, "duplicate complete must not re-pay QP");
    SELFTEST_CHECK(player->stat_xp_tenths[herb_stat] == xp_before,
                   "duplicate complete must not re-pay XP");
    quest_druid_pass("postquest-idempotent");

    /* Herblore unlocked after -- real OPHELD1. 250 Herblore XP is level 3,
     * which is the guam clean gate; pin the level in case XP has not
     * recalculated yet. */
    {
        const struct ToriRSServerObjInfo* grimy_info;
        int cat_grimy;
        int slot;

        if( herb_stat >= 0 )
        {
            if( player->stat_level[herb_stat] < 3 )
                player->stat_level[herb_stat] = 3;
            if( player->stat_boosted[herb_stat] < 3 )
                player->stat_boosted[herb_stat] = 3;
        }
        cat_grimy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_CATEGORY, "grimy_herb");
        grimy_info = obj_guam_grimy > 0 ? ToriRSServer_ObjInfo(obj_guam_grimy) : NULL;
        if( grimy_info && grimy_info->category > 0 )
            cat_grimy = grimy_info->category;
        selftest_clear_inv(player);
        selftest_give(player, obj_guam_grimy, 1);
        slot = quest_druid_inv_slot(player, obj_guam_grimy);
        player->last_slot = slot;
        SELFTEST_CHECK(slot >= 0, "grimy guam should occupy a slot after complete");
        selftest_opheld(srv, 1, slot);
        if( selftest_count(player, obj_guam) < 1 && cat_grimy > 0 )
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1, obj_guam_grimy,
                                           cat_grimy, -1);
        SELFTEST_CHECK(selftest_count(player, obj_guam) >= 1,
                       "OPHELD1 clean must succeed after state 4");
        quest_druid_pass("herblore-unlocked-clean");
    }

    /* Suit of armour: no loot, owned spawn. Production Attack does not
     * write completion. */
    {
        int npc_suit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "suit_of_armour");
        int obj_bones = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bones");

        if( npc_suit >= 0 )
        {
            suit_slot = ToriRSServer_WorldNpcSpawn(srv, npc_suit, player->x + 3, player->z,
                                                   player->level);
            spawn_slots[2] = suit_slot;
            SELFTEST_CHECK(suit_slot >= 0, "suit_of_armour should spawn");
            if( suit_slot >= 0 )
            {
                int tile_x = srv->npcs[suit_slot].x;
                int tile_z = srv->npcs[suit_slot].z;
                int drops = 0;

                selftest_clear_ground(srv);
                ToriRSServer_WorldNpcDied(srv, suit_slot);
                for( i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
                {
                    if( srv->ground[i].active && srv->ground[i].x == tile_x &&
                        srv->ground[i].z == tile_z )
                        drops++;
                }
                SELFTEST_CHECK(drops == 0,
                               "suit_of_armour should drop nothing (wiki: no Drops)");
                if( obj_bones >= 0 )
                    SELFTEST_CHECK(selftest_count(player, obj_bones) == 0,
                                   "suit kill must not invent bones");
                SELFTEST_CHECK(player->varps[varp_druid] == 4,
                               "suit Attack must not write completion");
                quest_druid_pass("suit-no-loot");
                ToriRSServer_WorldNpcFree(srv, suit_slot);
                suit_slot = -1;
                spawn_slots[2] = -1;
                ToriRSServer_WorldNpcReap(srv);
            }
        }
    }

    quest_druid_free_spawns(srv, spawn_slots, 3);
    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    quest_druid_pass("cleanup");
}
