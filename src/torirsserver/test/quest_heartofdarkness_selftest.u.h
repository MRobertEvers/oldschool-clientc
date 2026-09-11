/* The Heart of Darkness Gate D stanza. Included from
 * torirs_server_world_selftest.c immediately before the shop fprintf
 * so spawned Itzla / Fides / Amoxliatl cannot leak into later
 * RNG-gated checks.
 *
 * Qualify, refuse, accept, mid-quest talks, complete, and journal are
 * driven on the authored path. Silent success is forbidden: each step
 * prints an ASCII PASS line. The player stays godmoded.
 *
 * Guarded by TORIRSSERVER_SELFTEST_HOD_ONLY=1 (not GOD_ONLY).
 */
static void
hod_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "HOD PASS: %s\n", step);
}

static void
hod_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_WorldCloseModal(srv);
    if( srv->active_player )
        srv->active_player->active_script = NULL;
}

static void
hod_god(struct ToriRSServerPlayer* player)
{
    assert(player);
    player->godmode = 1;
    player->dying = 0;
    if( player->max_hitpoints > 0 )
        player->hitpoints = player->max_hitpoints;
    ToriRSServer_CombatSyncHitpoints(player);
}

static int
hod_chatmenu(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

static void
hod_drain(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int chatmenu;
    int round;

    assert(srv);
    assert(player);
    chatmenu = hod_chatmenu();
    for( round = 0; round < 40 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( chatmenu > 0 && uid == chatmenu )
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
hod_choose(struct ToriRSServer* srv, struct ToriRSServerPlayer* player, int slot)
{
    int chatmenu;

    assert(srv);
    assert(player);
    chatmenu = hod_chatmenu();
    hod_drain(srv, player);
    if( player->active_script != NULL && chatmenu > 0 )
    {
        player->last_slot = slot;
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
        hod_drain(srv, player);
    }
}

static void
hod_set_skills(struct ToriRSServerPlayer* player, int mining_lv, int thieving_lv,
               int slayer_lv, int agility_lv)
{
    int mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");
    int thieving = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "thieving");
    int slayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "slayer");
    int agility = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "agility");

    assert(player);
    if( mining >= 0 )
        ToriRSServer_CombatSetLevel(player, mining, mining_lv);
    if( thieving >= 0 )
        ToriRSServer_CombatSetLevel(player, thieving, thieving_lv);
    if( slayer >= 0 )
        ToriRSServer_CombatSetLevel(player, slayer, slayer_lv);
    if( agility >= 0 )
        ToriRSServer_CombatSetLevel(player, agility, agility_lv);
}

static int
hod_vmq3(struct ToriRSServerPlayer* player, int varbit)
{
    assert(player);
    return ToriRSServer_VarbitGet(player, varbit);
}

static void
hod_talk_npc(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int npc_type,
    int x,
    int z)
{
    int slot;

    assert(srv);
    assert(player);
    ToriRSServer_WorldTeleport(srv, 0, x, z);
    selftest_tick(srv);
    slot = npc_spawn(srv, npc_type, x + 1, z, 0);
    SELFTEST_CHECK(slot >= 0, "hod npc type %d should spawn", npc_type);
    if( slot < 0 )
        return;
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_type, -1, slot);
    hod_drain(srv, player);
}

static void
selftest_quest_hod(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int loaded;
    int vmq1;
    int vmq2;
    int vmq3;
    int npc_itzla;
    int npc_bartender;
    int npc_citizen;
    int npc_shop;
    int npc_tower;
    int npc_recruit;
    int npc_fides;
    int npc_amox;
    int npc_servius;
    int loc_bed;
    int loc_trial;
    int loc_ruins;
    int loc_lever;
    int loc_statue;
    int mining;
    int32_t why;
    int before;
    int xp_before;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::hodrun\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    vmq1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq1");
    vmq2 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq2");
    vmq3 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "vmq3");
    npc_itzla = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq2_itzla_vis");
    npc_bartender = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "quetzacalli_bartender");
    npc_citizen = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq3_itzla_vis_citizen");
    npc_shop = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "quetzacalli_general_store");
    npc_tower = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq3_itzla_vis_cultist");
    npc_recruit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq3_recruit_1_vis");
    npc_fides = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq3_forebearer_fides_vis");
    npc_amox = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "amoxliatl");
    npc_servius = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq3_servius_vis");
    loc_bed = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vmq3_pub_bed");
    loc_trial = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vmq3_tower_chest_book_closed");
    loc_ruins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "tapoyauik_temple_entrance");
    loc_lever = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vmq3_ruins_wall_lever_1");
    loc_statue = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "vmq3_ruins_air_statue_multi");
    mining = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "mining");

    SELFTEST_CHECK(vmq1 >= 0 && vmq2 >= 0 && vmq3 >= 0 && npc_itzla >= 0 &&
                       npc_bartender >= 0 && npc_citizen >= 0 && npc_shop >= 0 &&
                       npc_tower >= 0 && npc_recruit >= 0 && npc_fides >= 0 &&
                       npc_amox >= 0 && npc_servius >= 0 && loc_bed >= 0 &&
                       loc_trial >= 0 && loc_ruins >= 0 && loc_lever >= 0 &&
                       loc_statue >= 0 && mining >= 0,
                   "the ::hodrun C-side names should all resolve");
    if( vmq3 < 0 || npc_itzla < 0 )
    {
        ToriRSServer_ScriptsFree(srv);
        return;
    }

    selftest_reset_world(srv, player, 1454, 3173);
    hod_god(player);

    /* ---- qualify fails: shared ~hod_qualify_fail_reason, no %vmq3 write ---- */
    ToriRSServer_VarbitSet(srv, vmq1, 0);
    ToriRSServer_VarbitSet(srv, vmq2, 50);
    ToriRSServer_VarbitSet(srv, vmq3, 0);
    hod_set_skills(player, 55, 48, 48, 46);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,hod_qualify_fail_reason]", NULL, 0, &why) == 1 &&
                       why == 1,
                   "CotS fail should be reason 1, got %d", why);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 0, "CotS fail must not write %%vmq3");
    hod_pass("qualify fail Children of the Sun");
    hod_close(srv);

    ToriRSServer_VarbitSet(srv, vmq1, 24);
    ToriRSServer_VarbitSet(srv, vmq2, 0);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,hod_qualify_fail_reason]", NULL, 0, &why) == 1 &&
                       why == 2,
                   "Twilight's Promise fail should be reason 2, got %d", why);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 0, "TP fail must not write %%vmq3");
    hod_pass("qualify fail Twilight's Promise");
    hod_close(srv);

    ToriRSServer_VarbitSet(srv, vmq2, 50);
    hod_set_skills(player, 1, 48, 48, 46);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,hod_qualify_fail_reason]", NULL, 0, &why) == 1 &&
                       why == 3,
                   "Mining 55 fail should be reason 3, got %d", why);
    hod_pass("qualify fail Mining 55");

    hod_set_skills(player, 55, 1, 48, 46);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,hod_qualify_fail_reason]", NULL, 0, &why) == 1 &&
                       why == 4,
                   "Thieving 48 fail should be reason 4, got %d", why);
    hod_pass("qualify fail Thieving 48");

    hod_set_skills(player, 55, 48, 1, 46);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,hod_qualify_fail_reason]", NULL, 0, &why) == 1 &&
                       why == 5,
                   "Slayer 48 fail should be reason 5, got %d", why);
    hod_pass("qualify fail Slayer 48");

    hod_set_skills(player, 55, 48, 48, 1);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,hod_qualify_fail_reason]", NULL, 0, &why) == 1 &&
                       why == 6,
                   "Agility 46 fail should be reason 6, got %d", why);
    hod_pass("qualify fail Agility 46");

    /* Ready to start. Live Itzla hook requires TP complete. */
    hod_set_skills(player, 55, 48, 48, 46);
    ToriRSServer_VarbitSet(srv, vmq1, 24);
    ToriRSServer_VarbitSet(srv, vmq2, 50);
    ToriRSServer_VarbitSet(srv, vmq3, 0);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,hod_qualify_fail_reason]", NULL, 0, &why) == 1 &&
                       why == 0,
                   "ready start should qualify, got %d", why);
    hod_pass("qualify ready");

    /* Refuse: Yes. is row 1, Not now. is row 2. Must not write %vmq3. */
    before = hod_vmq3(player, vmq3);
    hod_talk_npc(srv, player, npc_itzla, 1454, 3173);
    hod_choose(srv, player, 2);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == before,
                   "refuse Not now. must leave %%vmq3 at %d, got %d", before,
                   hod_vmq3(player, vmq3));
    hod_pass("refuse Not now. does not write %vmq3");

    /* Accept writes %vmq3 = ^hod_pub (8). */
    hod_talk_npc(srv, player, npc_itzla, 1454, 3173);
    hod_choose(srv, player, 1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 8,
                   "accept Yes. should write %%vmq3=8, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("accept Yes. writes %vmq3 = ^hod_pub");

    /* Mid-quest talks on the authored opnpc/oploc hooks. */
    ToriRSServer_VarbitSet(srv, vmq3, 8);
    hod_talk_npc(srv, player, npc_bartender, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 12, "bartender should write %%vmq3=12, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("bartender basement room");

    ToriRSServer_VarbitSet(srv, vmq3, 12);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_bed, -1, -1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 14, "bed should write %%vmq3=14, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("pub bed rest");

    ToriRSServer_VarbitSet(srv, vmq3, 14);
    hod_talk_npc(srv, player, npc_citizen, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 18, "citizen supplies should write %%vmq3=18, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("citizen supplies");

    ToriRSServer_VarbitSet(srv, vmq3, 18);
    hod_talk_npc(srv, player, npc_shop, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 20, "shop charity should write %%vmq3=20, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("shop charity");

    ToriRSServer_VarbitSet(srv, vmq3, 20);
    hod_talk_npc(srv, player, npc_citizen, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 22, "citizen tower should write %%vmq3=22, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("citizen tower");

    ToriRSServer_VarbitSet(srv, vmq3, 22);
    hod_talk_npc(srv, player, npc_tower, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 24, "tower recruits nudge should write %%vmq3=24, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("tower recruits nudge");

    ToriRSServer_VarbitSet(srv, vmq3, 24);
    hod_talk_npc(srv, player, npc_recruit, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 28, "recruits should write %%vmq3=28, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("recruit introductions");

    ToriRSServer_VarbitSet(srv, vmq3, 28);
    hod_talk_npc(srv, player, npc_tower, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 32, "tower enter should write %%vmq3=32, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("tower enter / trial1 nudge");

    ToriRSServer_VarbitSet(srv, vmq3, 32);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_trial, -1, -1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 34, "trial1 soft should write %%vmq3=34, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("trial1 soft-skip");

    ToriRSServer_VarbitSet(srv, vmq3, 34);
    {
        int npc_melee = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "vmq3_tower_twilight_melee_variant_1a");
        if( npc_melee >= 0 )
            hod_talk_npc(srv, player, npc_melee, 1454, 3173);
        hod_close(srv);
    }
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 38, "trial2 soft should write %%vmq3=38, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("trial2 combat soft-skip");

    ToriRSServer_VarbitSet(srv, vmq3, 38);
    {
        int npc_suspect = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_NPC, "vmq3_tower_twilight_suspect_variant_1");
        if( npc_suspect >= 0 )
            hod_talk_npc(srv, player, npc_suspect, 1454, 3173);
        hod_close(srv);
    }
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 42, "trial3 soft should write %%vmq3=42, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("trial3 investigation soft-skip");

    ToriRSServer_VarbitSet(srv, vmq3, 42);
    {
        int npc_boss = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "vmq3_tower_trial_4_boss");
        if( npc_boss >= 0 )
            hod_talk_npc(srv, player, npc_boss, 1454, 3173);
        hod_close(srv);
    }
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 48, "trial4 soft should write %%vmq3=48, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("trial4 boss soft-skip");

    ToriRSServer_VarbitSet(srv, vmq3, 48);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_trial, -1, -1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 50, "robes soft should write %%vmq3=50, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("emissary robes soft-skip");

    ToriRSServer_VarbitSet(srv, vmq3, 50);
    hod_talk_npc(srv, player, npc_fides, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 52, "fides sermon should write %%vmq3=52, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("fides sermon");

    ToriRSServer_VarbitSet(srv, vmq3, 52);
    hod_talk_npc(srv, player, npc_fides, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 54, "fides after should write %%vmq3=54, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("fides after sermon");

    ToriRSServer_VarbitSet(srv, vmq3, 54);
    hod_talk_npc(srv, player, npc_fides, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 56, "fides ruins should write %%vmq3=56, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("fides enter ruins");

    ToriRSServer_VarbitSet(srv, vmq3, 56);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_ruins, -1, -1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 58, "ruins begin mine should write %%vmq3=58, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("ruins begin mine");

    ToriRSServer_VarbitSet(srv, vmq3, 58);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_ruins, -1, -1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 60, "ruins mine entrance should write %%vmq3=60, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("ruins mine entrance");

    ToriRSServer_VarbitSet(srv, vmq3, 60);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_lever, -1, -1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 62, "levers enter should write %%vmq3=62, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("ruins find levers");

    ToriRSServer_VarbitSet(srv, vmq3, 62);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_lever, -1, -1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 64, "levers pulled should write %%vmq3=64, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("ruins levers pulled");

    ToriRSServer_VarbitSet(srv, vmq3, 64);
    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPLOC1, loc_statue, -1, -1);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 68, "statues soft should write %%vmq3=68, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("ice statues soft-skip");

    ToriRSServer_VarbitSet(srv, vmq3, 68);
    hod_talk_npc(srv, player, npc_amox, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 74, "amox soft should write %%vmq3=74, got %d",
                   hod_vmq3(player, vmq3));
    hod_pass("Amoxliatl soft-skip");

    ToriRSServer_VarbitSet(srv, vmq3, 74);
    xp_before = player->stat_xp_tenths[mining];
    hod_talk_npc(srv, player, npc_servius, 1454, 3173);
    hod_close(srv);
    SELFTEST_CHECK(hod_vmq3(player, vmq3) == 76, "Servius should complete %%vmq3=76, got %d",
                   hod_vmq3(player, vmq3));
    SELFTEST_CHECK(player->stat_xp_tenths[mining] > xp_before,
                   "complete should grant Mining XP tenths, %d -> %d", xp_before,
                   player->stat_xp_tenths[mining]);
    hod_pass("Servius complete 2 QP / 8000 Mining Thieving Slayer Agility");

    ToriRSServer_VarbitSet(srv, vmq3, 76);
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,heartofdarkness_journal]", NULL, 0) == 1,
                   "journal complete should run");
    hod_close(srv);
    hod_pass("journal QUEST COMPLETE!");

    {
        static struct ToriRSServerCapture hodrun_capture;
        int hodrun_said_ok = 0;

        ToriRSServer_VarbitSet(srv, vmq1, 24);
        ToriRSServer_VarbitSet(srv, vmq2, 50);
        ToriRSServer_VarbitSet(srv, vmq3, 0);
        hod_set_skills(player, 55, 48, 48, 46);
        ToriRSServer_CaptureBegin(srv, &hodrun_capture);
        ToriRSServer_ScriptsRunDebugproc(srv, "hodrun");
        ToriRSServer_CaptureEnd(srv);
        for( int i = ToriRSServer_CaptureFindNamed(&hodrun_capture, PKT_NAME_MESSAGE_GAME, 0);
             i >= 0;
             i = ToriRSServer_CaptureFindNamed(&hodrun_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const struct ToriRSServerCapturedPacket* packet = &hodrun_capture.packets[i];
            const char* text = selftest_message_text(srv, packet);

            if( !text )
                continue;
            if( strstr(text, "hodrun OK") != NULL )
                hodrun_said_ok = 1;
        }
        SELFTEST_CHECK(hodrun_said_ok, "::hodrun should reach its OK line");
        hod_close(srv);
        hod_pass("::hodrun OK");
    }

    ToriRSServer_ScriptsFree(srv);
}
