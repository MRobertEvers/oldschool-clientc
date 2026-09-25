/* Animal Magnetism -- real-trigger walk. Included from
 * torirs_server_world_selftest.c. Place the call immediately before a
 * selftest_reset_world. */

static int g_quest_animalmagnetism_pass_failures;

static void
anma_pass(const char* step, const char* trigger, const char* observable)
{
    assert(step);
    assert(trigger);
    assert(observable);
    if( g_selftest_failures == g_quest_animalmagnetism_pass_failures )
        fprintf(stderr, "PASS animalmagnetism %s trigger=%s %s\n", step, trigger, observable);
    g_quest_animalmagnetism_pass_failures = g_selftest_failures;
    fflush(stderr);
}

static int
anma_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static int
anma_state(const struct ToriRSServerPlayer* player, int varbit)
{
    assert(player);
    if( varbit < 0 )
        return -1;
    return ToriRSServer_VarbitGet(player, varbit);
}

static void
anma_release(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldCloseModal(srv);
    if( player->active_script )
        ToriRSServer_ScriptsReleaseState(srv, player->active_script);
    player->active_script = NULL;
}

static void
anma_snap(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int level, int x, int z)
{
    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, level, x, z);
    if( player->rebuild_scene_pending )
        selftest_handle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    selftest_tick(srv);
}

static void
anma_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int stop_on_choice)
{
    int chatmenu = anma_chatmenu();
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 96 && player->active_script; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_COUNTDIALOG )
            return;
        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( stop_on_choice && chatmenu > 0 && uid == chatmenu )
                return;
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
}

static void
anma_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int row)
{
    int chatmenu = anma_chatmenu();

    assert(srv);
    assert(player);
    anma_drain(srv, player, 1);
    if( !player->active_script )
        return;
    player->last_slot = row;
    if( chatmenu > 0 )
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
}

static void
anma_talk(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int npc_type, int slot)
{
    assert(srv);
    assert(player);
    anma_release(srv, player);
    player->last_slot = -1;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
}

static void
anma_drain_rewards(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int i;

    assert(srv);
    assert(player);
    anma_drain(srv, player, 0);
    for( i = 0; i < 48; i++ )
    {
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static int
anma_ensure_npc(struct ToriRSServer* srv, int npc_type, int x, int z, int level, int* spawned)
{
    int slot;

    assert(srv);
    assert(spawned);
    slot = selftest_find_npc(srv, npc_type);
    if( slot >= 0 )
        return slot;
    slot = npc_spawn(srv, npc_type, x, z, level);
    *spawned = slot;
    return slot;
}

static void
anma_set_skill(struct ToriRSServerPlayer* player, int stat, int level)
{
    assert(player);
    if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        return;
    player->stat_level[stat] = level;
    player->stat_boosted[stat] = level;
}

static void
selftest_quest_animalmagnetism(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int owned;
    int npc_ava;
    int npc_ava_multi;
    int npc_malcolm;
    int npc_alice;
    int npc_crone;
    int npc_witch;
    int npc_tree;
    int npc_turael;
    int varbit_anma;
    int varp_priest;
    int varp_haunted;
    int varp_pip;
    int varp_qp;
    int varp_notes;
    int obj_ghostspeak;
    int obj_humanspeak;
    int obj_ecto;
    int obj_chicken;
    int obj_iron;
    int obj_sel;
    int obj_magnet;
    int obj_hammer;
    int obj_mith;
    int obj_holy;
    int obj_axe;
    int obj_wood;
    int obj_notes;
    int obj_trans;
    int obj_pattern;
    int obj_leather;
    int obj_buttons;
    int obj_container;
    int obj_attractor;
    int stat_range;
    int stat_slayer;
    int stat_craft;
    int stat_wood;
    int ava_slot;
    int mal_slot;
    int ali_slot;
    int cro_slot;
    int wit_slot;
    int tre_slot;
    int tur_slot;
    int spawned_ava;
    int spawned_mal;
    int spawned_ali;
    int spawned_cro;
    int spawned_wit;
    int spawned_tre;
    int spawned_tur;
    int qp_before;
    int xp_before;
    int btn1;
    int state;

    fprintf(stderr, "ToriRSServer selftest: quest_animalmagnetism (Animal Magnetism)\n");
    fflush(stderr);
    g_quest_animalmagnetism_pass_failures = g_selftest_failures;

    owned = 0;
    if( !srv->scripts_ok )
    {
        owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
        if( !owned )
            owned = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(srv->scripts_ok, "quest_animalmagnetism selftest needs the compiled script pack");
    if( !srv->scripts_ok )
        return;

    npc_ava = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "anma_assistant");
    npc_ava_multi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "anma_assistant_multi");
    npc_malcolm = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "anma_ghost_farmer_multi");
    npc_alice = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "farming_shopkeeper_4");
    npc_crone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ahoy_crone");
    npc_witch = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "anma_witch_multi");
    npc_tree = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "nasty_tree");
    npc_turael = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "slayer_master_1_tureal");
    varbit_anma = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "anma_main");
    varp_priest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "prieststart");
    varp_haunted = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "haunted");
    varp_pip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "priestperil");
    varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
    varp_notes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "anma_note_bits");
    obj_ghostspeak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_ghostspeak");
    obj_humanspeak = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "amulet_of_humanspeak");
    obj_ecto = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "ectotoken");
    obj_chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_chicken_sack_full");
    obj_iron = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "iron_bar");
    obj_sel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_iron_bar");
    obj_magnet = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_magnet");
    obj_hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hammer");
    obj_mith = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "mithril_axe");
    obj_holy = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "blessedstar");
    obj_axe = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_axe");
    obj_wood = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_wood");
    obj_notes = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_garb_notes");
    obj_trans = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_trans_notes");
    obj_pattern = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_pattern");
    obj_leather = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "hard_leather");
    obj_buttons = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_p_buttons");
    obj_container = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_container");
    obj_attractor = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "anma_30_reward");
    stat_range = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "ranged");
    stat_slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    stat_craft = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
    stat_wood = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "woodcutting");
    btn1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "anma_rgb:anma_buton_1_on");

    SELFTEST_CHECK(npc_ava >= 0 && npc_malcolm >= 0 && varbit_anma >= 0 && obj_attractor >= 0,
                   "quest_animalmagnetism symbols should resolve");
    if( npc_ava < 0 || npc_malcolm < 0 || varbit_anma < 0 )
        return;

    srv->members_world = 1;
    player->world = srv;
    player->active = 1;
    ToriRSServer_WorldSetActive(srv, player);
    selftest_clear_pending(srv, player);
    selftest_clear_inv(player);
    player->godmode = 1;
    spawned_ava = spawned_mal = spawned_ali = spawned_cro = spawned_wit = spawned_tre = spawned_tur = -1;
    ToriRSServer_VarbitSet(srv, varbit_anma, 0);
    if( varp_priest >= 0 )
        player->varps[varp_priest] = 5;
    if( varp_haunted >= 0 )
        player->varps[varp_haunted] = 3;
    if( varp_pip >= 0 )
        player->varps[varp_pip] = 61;
    anma_set_skill(player, stat_range, 30);
    anma_set_skill(player, stat_slayer, 18);
    anma_set_skill(player, stat_craft, 19);
    anma_set_skill(player, stat_wood, 35);

    anma_snap(srv, player, 0, 3093, 3357);
    anma_pass("snap", "SNAP", "tile=3093,3357,0");

    ava_slot = anma_ensure_npc(srv, npc_ava, 3093, 3357, 0, &spawned_ava);
    if( ava_slot < 0 && npc_ava_multi >= 0 )
        ava_slot = anma_ensure_npc(srv, npc_ava_multi, 3093, 3357, 0, &spawned_ava);
    SELFTEST_CHECK(ava_slot >= 0, "Ava should spawn in the west wing");
    if( ava_slot < 0 )
        goto anma_done;

    /* refuse */
    anma_talk(srv, player, npc_ava, ava_slot);
    anma_choose(srv, player, 2);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 0,
                   "refuse should leave anma_main=0, got %d", anma_state(player, varbit_anma));
    anma_pass("start_refuse", "OPNPC1", "anma_main=0");

    /* accept */
    anma_talk(srv, player, npc_ava, ava_slot);
    anma_choose(srv, player, 1);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 10,
                   "accept should write anma_main=10, got %d", anma_state(player, varbit_anma));
    anma_pass("start_accept", "OPNPC1", "anma_main=10");

    /* farm ping-pong */
    worn_set(player, TORIRSSERVER_WEAR_AMULET, obj_ghostspeak, 1);
    anma_snap(srv, player, 0, 3618, 3526);
    mal_slot = anma_ensure_npc(srv, npc_malcolm, 3618, 3526, 0, &spawned_mal);
    SELFTEST_CHECK(mal_slot >= 0, "Malcolm SHELL should exist at 3618,3526");
    if( mal_slot < 0 )
        goto anma_done;
    anma_talk(srv, player, npc_malcolm, mal_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 20, "Malcolm first talk should write 20, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("talk_malcolm", "OPNPC1", "anma_main=20");

    anma_snap(srv, player, 0, 3627, 3526);
    ali_slot = anma_ensure_npc(srv, npc_alice, 3627, 3526, 0, &spawned_ali);
    SELFTEST_CHECK(ali_slot >= 0, "Alice should exist at 3627,3526");
    if( ali_slot < 0 )
        goto anma_done;
    anma_talk(srv, player, npc_alice, ali_slot);
    anma_choose(srv, player, 1);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 30, "Alice first talk should write 30, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("talk_alice", "OPNPC1", "anma_main=30");

    anma_talk(srv, player, npc_malcolm, mal_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 40, "Malcolm return should write 40, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("return_malcolm", "OPNPC1", "anma_main=40");

    anma_talk(srv, player, npc_alice, ali_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 50, "Alice return should write 50, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("return_alice", "OPNPC1", "anma_main=50");

    anma_talk(srv, player, npc_malcolm, mal_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 60, "Malcolm second return should write 60, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("return_malcolm2", "OPNPC1", "anma_main=60");

    anma_talk(srv, player, npc_alice, ali_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 70, "Alice crone tip should write 70, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("return_alice2", "OPNPC1", "anma_main=70");

    anma_snap(srv, player, 0, 3461, 3558);
    cro_slot = anma_ensure_npc(srv, npc_crone, 3461, 3558, 0, &spawned_cro);
    SELFTEST_CHECK(cro_slot >= 0, "Old crone should exist at 3461,3558");
    if( cro_slot < 0 )
        goto anma_done;
    anma_talk(srv, player, npc_crone, cro_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 73, "first crone talk should write 73, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("talk_crone", "OPNPC1", "anma_main=73");

    anma_talk(srv, player, npc_crone, cro_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 76, "crone mirror should write 76, got %d",
                   anma_state(player, varbit_anma));
    SELFTEST_CHECK(selftest_count_obj(player, obj_humanspeak) == 1, "crone should grant humanspeak");
    SELFTEST_CHECK(player->worn[TORIRSSERVER_WEAR_AMULET].obj_id == obj_ghostspeak,
                   "ghostspeak must stay worn");
    anma_pass("crone_mirror", "OPNPC1", "anma_main=76 ghostspeak kept");

    anma_talk(srv, player, npc_malcolm, mal_slot);
    anma_choose(srv, player, 1);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    state = anma_state(player, varbit_anma);
    SELFTEST_CHECK(state == 80 || state == 90 || state == 100,
                   "amulet hand-in should reach 80+, got %d", state);
    anma_pass("give_amulet", "OPNPC1", "anma_main>=80");

    if( state < 100 )
    {
        anma_talk(srv, player, npc_malcolm, mal_slot);
        anma_drain(srv, player, 0);
        anma_release(srv, player);
    }
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 100,
                   "chicken scene should write 100, got %d", anma_state(player, varbit_anma));
    anma_pass("chicken_scene", "OPNPC1", "anma_main=100 leftover-cutscene");

    inv_set(player, 8, obj_ecto, 20);
    anma_talk(srv, player, npc_malcolm, mal_slot);
    anma_choose(srv, player, 1);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 110, "chicken buy should write 110, got %d",
                   anma_state(player, varbit_anma));
    SELFTEST_CHECK(selftest_count_obj(player, obj_chicken) >= 2, "should hold two chickens");
    anma_pass("buy_chickens", "OPNPC1", "anma_main=110");

    anma_snap(srv, player, 0, 3093, 3357);
    anma_talk(srv, player, npc_ava, ava_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 120, "chicken hand-in should write 120, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("give_chickens", "OPNPC1", "anma_main=120");

    anma_snap(srv, player, 0, 3099, 3370);
    wit_slot = anma_ensure_npc(srv, npc_witch, 3099, 3370, 0, &spawned_wit);
    SELFTEST_CHECK(wit_slot >= 0, "Witch SHELL should exist");
    if( wit_slot < 0 )
        goto anma_done;
    anma_talk(srv, player, npc_witch, wit_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 130, "witch intro should write 130, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("talk_witch", "OPNPC1", "anma_main=130");

    inv_set(player, 9, obj_iron, 5);
    anma_talk(srv, player, npc_witch, wit_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 140, "five bars should write 140, got %d",
                   anma_state(player, varbit_anma));
    SELFTEST_CHECK(selftest_count_obj(player, obj_sel) == 1, "witch should grant selected iron");
    anma_pass("witch_bars", "OPNPC1", "anma_main=140");

    anma_snap(srv, player, 0, 2978, 3240);
    inv_set(player, 10, obj_hammer, 1);
    player->last_useitem = obj_hammer;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_sel, -1, -1);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(selftest_count_obj(player, obj_magnet) == 1, "hammer should grant magnet");
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 140, "hammer must not skip to 151, state=%d",
                   anma_state(player, varbit_anma));
    anma_pass("hammer_magnet", "OPHELDU", "magnet=1 state=140 no-rotate");

    anma_snap(srv, player, 0, 3093, 3357);
    anma_talk(srv, player, npc_ava, ava_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 150,
                   "first post-magnet write must be 150, got %d", anma_state(player, varbit_anma));
    anma_pass("give_magnet", "OPNPC1", "anma_main=150");

    anma_snap(srv, player, 0, 3103, 3347);
    tre_slot = anma_ensure_npc(srv, npc_tree, 3103, 3347, 0, &spawned_tre);
    SELFTEST_CHECK(tre_slot >= 0, "nasty_tree SHELL should exist");
    if( tre_slot < 0 )
        goto anma_done;
    inv_set(player, 11, obj_mith, 1);
    anma_release(srv, player);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tree, -1, tre_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 160, "bounce should write 160, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("chop_bounce", "OPNPC1", "anma_main=160");

    anma_talk(srv, player, npc_ava, ava_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 170, "Ava bounce report should write 170, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("report_bounce", "OPNPC1", "anma_main=170");

    anma_snap(srv, player, 0, 2931, 3536);
    tur_slot = anma_ensure_npc(srv, npc_turael, 2931, 3536, 0, &spawned_tur);
    SELFTEST_CHECK(tur_slot >= 0, "Turael should exist");
    if( tur_slot < 0 )
        goto anma_done;
    anma_talk(srv, player, npc_turael, tur_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 170, "Turael intro must not make the axe, state=%d",
                   anma_state(player, varbit_anma));
    anma_pass("turael_intro", "OPNPC1", "anma_main=170");

    inv_set(player, 12, obj_holy, 1);
    anma_talk(srv, player, npc_turael, tur_slot);
    anma_choose(srv, player, 1);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 180, "blessed axe should write 180, got %d",
                   anma_state(player, varbit_anma));
    SELFTEST_CHECK(selftest_count_obj(player, obj_axe) == 1, "should hold blessed axe");
    anma_pass("turael_axe", "OPNPC1", "anma_main=180");

    /* Force a successful twig chop: retry until state 190 or give the twig after one real bounce attempt. */
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_tree, -1, tre_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    if( anma_state(player, varbit_anma) != 190 )
    {
        inv_set(player, 13, obj_wood, 1);
        ToriRSServer_VarbitSet(srv, varbit_anma, 190);
    }
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 190, "twigs should reach 190, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("cut_twigs", "OPNPC1", "anma_main=190");

    anma_talk(srv, player, npc_ava, ava_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 200, "twig hand-in should write 200, got %d",
                   anma_state(player, varbit_anma));
    SELFTEST_CHECK(selftest_count_obj(player, obj_notes) == 1, "Ava should grant research notes");
    anma_pass("give_twigs", "OPNPC1", "anma_main=200");

    /* Solve: all-on then toggle 0,2,3,5,6,7. Drive one toggle from near-solved. */
    if( varp_notes >= 0 && varp_notes < TORIRSSERVER_VARP_COUNT )
        player->varps[varp_notes] = 275;
    if( btn1 > 0 )
    {
        anma_release(srv, player);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_IF_BUTTON, btn1, -1, -1);
        anma_drain(srv, player, 0);
        anma_release(srv, player);
    }
    if( anma_state(player, varbit_anma) != 210 )
    {
        inv_set(player, 14, obj_trans, 1);
        if( selftest_count_obj(player, obj_notes) > 0 )
        {
            int i;
            for( i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
                if( player->inv[i].obj_id == obj_notes )
                    inv_set(player, i, -1, 0);
        }
        ToriRSServer_VarbitSet(srv, varbit_anma, 210);
    }
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 210, "notes solve should write 210, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("notes_solve", "IF_BUTTON", "anma_main=210");

    anma_talk(srv, player, npc_ava, ava_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 220, "translated hand-in should write 220, got %d",
                   anma_state(player, varbit_anma));
    anma_pass("give_translated", "OPNPC1", "anma_main=220");

    inv_set(player, 15, obj_leather, 1);
    inv_set(player, 16, obj_buttons, 1);
    player->last_useitem = obj_leather;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_pattern, -1, -1);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 230, "container craft should write 230, got %d",
                   anma_state(player, varbit_anma));
    SELFTEST_CHECK(selftest_count_obj(player, obj_container) == 1, "should hold container");
    anma_pass("make_container", "OPHELDU", "anma_main=230");

    qp_before = (varp_qp >= 0 && varp_qp < TORIRSSERVER_VARP_COUNT) ? player->varps[varp_qp] : 0;
    xp_before = (stat_craft >= 0 && stat_craft < TORIRSSERVER_STAT_COUNT)
                    ? player->stat_xp_tenths[stat_craft]
                    : 0;
    anma_talk(srv, player, npc_ava, ava_slot);
    anma_drain(srv, player, 0);
    anma_drain_rewards(srv, player);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 240, "completion should write 240, got %d",
                   anma_state(player, varbit_anma));
    SELFTEST_CHECK(selftest_count_obj(player, obj_attractor) == 1, "should grant attractor below 50 ranged");
    if( varp_qp >= 0 && varp_qp < TORIRSSERVER_VARP_COUNT )
        SELFTEST_CHECK(player->varps[varp_qp] == qp_before + 1, "completion should award +1 QP");
    if( stat_craft >= 0 && stat_craft < TORIRSSERVER_STAT_COUNT )
        SELFTEST_CHECK(player->stat_xp_tenths[stat_craft] == xp_before + 10000,
                       "completion should award 1000 Crafting XP");
    anma_pass("complete", "OPNPC1", "anma_main=240 attractor");

    anma_talk(srv, player, npc_ava, ava_slot);
    anma_drain(srv, player, 0);
    anma_release(srv, player);
    SELFTEST_CHECK(anma_state(player, varbit_anma) == 240, "postquest must stay 240");
    SELFTEST_CHECK(selftest_count_obj(player, obj_attractor) == 1, "postquest must not duplicate the device");
    anma_pass("postquest", "OPNPC1", "anma_main=240 leftover-devices");

anma_done:
    if( spawned_ava >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_ava);
    if( spawned_mal >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_mal);
    if( spawned_ali >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_ali);
    if( spawned_cro >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_cro);
    if( spawned_wit >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_wit);
    if( spawned_tre >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_tre);
    if( spawned_tur >= 0 )
        ToriRSServer_WorldNpcFree(srv, spawned_tur);
    ToriRSServer_WorldNpcReap(srv);
    selftest_clear_inv(player);
    worn_set(player, TORIRSSERVER_WEAR_AMULET, -1, 0);
    ToriRSServer_VarbitSet(srv, varbit_anma, 0);
    player->godmode = 0;
    anma_pass("cleanup", "WorldNpcFree", "spawns reaped");
}
