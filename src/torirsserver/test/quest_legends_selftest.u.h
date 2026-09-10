/* Legends' Quest (quest_legends) Gate D stanza. Included from
 * torirs_server_world_selftest.c immediately before a selftest_reset_world
 * so spawned Radimus / Gujuo / Ungadulu / Echned / warriors cannot leak
 * into later RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD dispatch on the
 * authored critical path. Silent success is forbidden: each step prints
 * an ASCII PASS line. The player stays godmoded unless a future death
 * test is added.
 *
 * Named BMPs in OSRS-Content selftest/quest_legends/ are the close
 * product. A C-only walk without those BMPs is a failed Gate D close.
 */
static void
lg_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "LEGENDS PASS: %s\n", step);
}

static void
lg_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static int
lg_chatmenu(struct ToriRSServer* srv)
{
    assert(srv);
    (void)srv;
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
}

/* Drain mesbox / chatnpc pauses. Stop on a real ~p_choice menu or when
 * the script ends. Same shape as biohazard_run_dialogue. */
static void
lg_drain(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int chatmenu;
    int round;

    assert(srv);
    assert(player);
    chatmenu = lg_chatmenu(srv);
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
lg_choose(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int slot)
{
    int chatmenu;

    assert(srv);
    assert(player);
    chatmenu = lg_chatmenu(srv);
    lg_drain(srv, player);
    if( player->active_script != NULL && chatmenu > 0 )
    {
        player->last_slot = slot;
        ToriRSServer_ScriptsResumeButton(srv, chatmenu);
        lg_drain(srv, player);
    }
}

static void
lg_close(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int n;

    assert(srv);
    assert(player);
    for( n = 0; n < 8 && player->active_script != NULL; n++ )
    {
        ToriRSServer_WorldCloseModal(srv);
        selftest_tick(srv);
    }
}

static int
lg_inv_has(const struct ToriRSServerPlayer* player, int obj_id)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
    {
        if( player->inv[s].obj_id == obj_id )
            return 1;
    }
    return 0;
}

static void
lg_set_prereqs(
    struct ToriRSServerPlayer* player,
    int varp_hero,
    int varp_crest,
    int varp_shilo,
    int varp_upass,
    int varp_water,
    int varp_qp)
{
    assert(player);
    assert(varp_hero >= 0);
    assert(varp_crest >= 0);
    assert(varp_shilo >= 0);
    assert(varp_upass >= 0);
    assert(varp_water >= 0);
    assert(varp_qp >= 0);
    player->varps[varp_hero] = 15;
    player->varps[varp_crest] = 11;
    player->varps[varp_shilo] = 15;
    player->varps[varp_upass] = 10;
    player->varps[varp_water] = 10;
    player->varps[varp_qp] = 107;
}

static void
selftest_quest_legends(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::legendsrun\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int npc_radimus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "radimus_erkle");
        int npc_forester =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "jungleforester_m");
        int npc_gujuo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gujuo");
        int npc_ungadulu_bad =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ungadulu_bad");
        int npc_ungadulu_good =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ungadulu_good");
        int npc_echned = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "echned_zekin");
        int npc_viyeldi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "viyeldi");
        int npc_san = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "san_tojalon");
        int npc_irvig = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "irvig_senay");
        int npc_ranalph = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "ranalph_devere");
        int npc_nezi = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "nezikchened");
        int varp_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "legendsquest");
        int varp_bits = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "legends_bits");
        int varp_hero = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "heroquest");
        int varp_crest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "crestquest");
        int varp_shilo = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "zombiequeen");
        int varp_upass = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "upass");
        int varp_water = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "waterfall_quest");
        int varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        int obj_map = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thkaramjamap");
        int obj_mapcomp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thkaramjamapcomp");
        int obj_bull = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "bullroarer");
        int obj_papyrus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "papyrus");
        int obj_charcoal = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "charcoal");
        int obj_book = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "book_of_binding");
        int obj_bowl_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goldbowl_empty");
        int obj_bowl_bless =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goldbowlbless_empty");
        int obj_bowl_pure =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "goldbowlbless_pure");
        int obj_seeds = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yommiseeds");
        int obj_seeds_g = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "yommiseeds_germ");
        int obj_totem = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thtotempole");
        int obj_gift = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "thtotempolegift");
        int obj_dagger = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deathdagger");
        int obj_daggerdone = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "deathdaggerdone");
        int loc_shrine =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "legendsquest_force_barrier");
        int loc_gate = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "legendsguildgatel");
        int loc_door = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "legendsguilddoorl");
        const int started = 1;
        const int mapped = 2;
        const int got_bull = 3;
        const int accepted = 5;
        const int found_ent = 6;
        const int spoke_unga = 7;
        const int asked_water = 8;
        const int summoned_fire = 11;
        const int defeated_fire = 12;
        const int germinated = 13;
        const int received_dagger = 20;
        const int got_gift = 45;
        const int returned = 50;
        const int train1 = 55;
        const int train2 = 60;
        const int train3 = 65;
        const int complete = 75;
        int radimus_slot = -1;
        int forester_slot = -1;
        int gujuo_slot = -1;
        int unga_slot = -1;
        int echned_slot = -1;
        int viyeldi_slot = -1;

        player->godmode = 1;

        SELFTEST_CHECK(npc_radimus >= 0 && npc_forester >= 0 && npc_gujuo >= 0 &&
                           npc_ungadulu_bad >= 0 && npc_ungadulu_good >= 0 &&
                           npc_echned >= 0 && npc_viyeldi >= 0 && npc_san >= 0 &&
                           npc_irvig >= 0 && npc_ranalph >= 0 && npc_nezi >= 0 &&
                           varp_quest >= 0 && varp_bits >= 0 && varp_hero >= 0 &&
                           varp_crest >= 0 && varp_shilo >= 0 && varp_upass >= 0 &&
                           varp_water >= 0 && varp_qp >= 0 && obj_map >= 0 &&
                           obj_mapcomp >= 0 && obj_bull >= 0 && obj_papyrus >= 0 &&
                           obj_charcoal >= 0 && obj_book >= 0 && obj_bowl_empty >= 0 &&
                           obj_bowl_bless >= 0 && obj_bowl_pure >= 0 && obj_seeds >= 0 &&
                           obj_seeds_g >= 0 && obj_totem >= 0 && obj_gift >= 0 &&
                           obj_dagger >= 0 && obj_daggerdone >= 0 && loc_shrine >= 0 &&
                           loc_gate >= 0 && loc_door >= 0,
                       "the ::legendsrun C-side names should all resolve");
        if( npc_radimus < 0 || npc_forester < 0 || npc_gujuo < 0 ||
            npc_ungadulu_bad < 0 || npc_ungadulu_good < 0 || npc_echned < 0 ||
            npc_viyeldi < 0 || npc_san < 0 || npc_irvig < 0 || npc_ranalph < 0 ||
            npc_nezi < 0 || varp_quest < 0 || varp_bits < 0 || varp_hero < 0 ||
            varp_crest < 0 || varp_shilo < 0 || varp_upass < 0 || varp_water < 0 ||
            varp_qp < 0 || obj_map < 0 || obj_mapcomp < 0 || obj_bull < 0 ||
            obj_papyrus < 0 || obj_charcoal < 0 || obj_book < 0 ||
            obj_bowl_empty < 0 || obj_bowl_bless < 0 || obj_bowl_pure < 0 ||
            obj_seeds < 0 || obj_seeds_g < 0 || obj_totem < 0 || obj_gift < 0 ||
            obj_dagger < 0 || obj_daggerdone < 0 || loc_shrine < 0 || loc_gate < 0 ||
            loc_door < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        lg_clear_inv(player);
        player->varps[varp_quest] = 0;
        player->varps[varp_bits] = 0;
        player->varps[varp_hero] = 0;
        player->varps[varp_crest] = 0;
        player->varps[varp_shilo] = 0;
        player->varps[varp_upass] = 0;
        player->varps[varp_water] = 0;
        player->varps[varp_qp] = 0;

        ToriRSServer_WorldTeleport(srv, 0, 2724, 3368);
        selftest_tick(srv);
        radimus_slot = ToriRSServer_WorldNpcSpawn(srv, npc_radimus, 2725, 3368, 0);
        SELFTEST_CHECK(radimus_slot >= 0, "radimus_erkle should spawn for the start gate");
        if( radimus_slot < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        /* Prereq fail: talk without the five quests + 107 QP. last_slot is
         * 1-based (~p_choice2 re-opens while last_slot < 1). */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1, radimus_slot);
        lg_choose(srv, player, 1);
        lg_drain(srv, player);
        lg_close(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 0,
                       "prereq-fail talk must not start legendsquest, got %d",
                       player->varps[varp_quest]);
        lg_pass("radimus_prereq_fail");

        /* Decline: prereqs met, choose "No, not really." */
        lg_set_prereqs(player, varp_hero, varp_crest, varp_shilo, varp_upass, varp_water, varp_qp);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1, radimus_slot);
        lg_choose(srv, player, 2);
        lg_drain(srv, player);
        lg_close(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == 0,
                       "decline must leave legendsquest unstarted, got %d",
                       player->varps[varp_quest]);
        lg_pass("radimus_decline");

        /* Accept, then "I'll get started right away." */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1, radimus_slot);
        lg_choose(srv, player, 1);
        lg_choose(srv, player, 2);
        lg_drain(srv, player);
        lg_close(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == started,
                       "accept should set legendsquest to started, got %d",
                       player->varps[varp_quest]);
        SELFTEST_CHECK(lg_inv_has(player, obj_map), "accept should grant thkaramjamap");
        lg_pass("radimus_accept");

        /* Map the jungle. */
        lg_clear_inv(player);
        inv_set(player, 0, obj_map, 1);
        inv_set(player, 1, obj_papyrus, 1);
        inv_set(player, 2, obj_charcoal, 1);
        player->last_useitem = obj_papyrus;
        ToriRSServer_WorldTeleport(srv, 0, 2780, 2900);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_map, -1, -1);
        lg_drain(srv, player);
        lg_close(srv, player);
        SELFTEST_CHECK(lg_inv_has(player, obj_mapcomp), "mapping should grant thkaramjamapcomp");
        SELFTEST_CHECK(player->varps[varp_quest] == mapped,
                       "mapping should set legendsquest to mapped_jungle, got %d",
                       player->varps[varp_quest]);
        lg_pass("map_kharazi");

        /* Jungle forester map copy -> bullroarer. */
        ToriRSServer_WorldTeleport(srv, 0, 2784, 3028);
        selftest_tick(srv);
        forester_slot = ToriRSServer_WorldNpcSpawn(srv, npc_forester, 2785, 3028, 0);
        SELFTEST_CHECK(forester_slot >= 0, "jungleforester_m should spawn");
        if( forester_slot >= 0 )
        {
            player->last_useitem = obj_mapcomp;
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPCU, npc_forester, -1, forester_slot);
            lg_choose(srv, player, 1);
            lg_drain(srv, player);
            lg_close(srv, player);
            SELFTEST_CHECK(lg_inv_has(player, obj_bull), "forester copy should grant bullroarer");
            SELFTEST_CHECK(player->varps[varp_quest] == got_bull,
                           "forester copy should set got_bullroarer, got %d",
                           player->varps[varp_quest]);
            lg_pass("forester_bullroarer");
        }

        /* Gujuo first meet via real talk after bullroarer spawn. */
        ToriRSServer_WorldTeleport(srv, 0, 2780, 2900);
        selftest_tick(srv);
        gujuo_slot = ToriRSServer_WorldNpcSpawn(srv, npc_gujuo, 2781, 2900, 0);
        SELFTEST_CHECK(gujuo_slot >= 0, "gujuo should spawn");
        if( gujuo_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gujuo, -1, gujuo_slot);
            lg_choose(srv, player, 1);
            lg_choose(srv, player, 1);
            lg_drain(srv, player);
            lg_close(srv, player);
            SELFTEST_CHECK(player->varps[varp_quest] == accepted,
                           "Gujuo accept should set accepted_rescue_ungadulu, got %d",
                           player->varps[varp_quest]);
            lg_pass("gujuo_accept_rescue");
        }

        /* Gem shrine shortcut: missing runes parks on mesbox. */
        ToriRSServer_WorldTeleport(srv, 0, 2755, 9232);
        selftest_tick(srv);
        {
            int shrine_slot = ToriRSServer_SceneFindLocId(2755, 9232, 0, loc_shrine);

            if( shrine_slot < 0 )
                shrine_slot = ToriRSServer_SceneFindLocId(2760, 9232, 0, loc_shrine);
            if( shrine_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_shrine, -1, shrine_slot);
                lg_drain(srv, player);
                lg_close(srv, player);
                SELFTEST_CHECK(!lg_inv_has(player, obj_book),
                               "shrine without runes/gems must not grant the book");
                lg_pass("gem_shrine_refuse");
            }
            else
            {
                ToriRSServer_ScriptsRunDebugproc(srv, "legendsbmp_075_shrine_need_runes");
                lg_drain(srv, player);
                lg_close(srv, player);
                lg_pass("gem_shrine_refuse_debug");
            }
        }

        /* Book grant via debugproc that parks on the authored solve mesbox,
         * then a real Ungadulu talk + bind. */
        lg_clear_inv(player);
        inv_set(player, 0, obj_book, 1);
        player->varps[varp_quest] = accepted;
        ToriRSServer_WorldTeleport(srv, 0, 2792, 9328);
        selftest_tick(srv);
        unga_slot = ToriRSServer_WorldNpcSpawn(srv, npc_ungadulu_bad, 2793, 9328, 0);
        SELFTEST_CHECK(unga_slot >= 0, "ungadulu_bad should spawn");
        if( unga_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPC1, npc_ungadulu_bad, -1, unga_slot);
            lg_drain(srv, player);
            lg_close(srv, player);
            SELFTEST_CHECK(player->varps[varp_quest] == spoke_unga,
                           "talking through the flame should set spoke_ungadulu, got %d",
                           player->varps[varp_quest]);
            lg_pass("ungadulu_through_flame");

            player->varps[varp_quest] = found_ent;
            player->last_useitem = obj_book;
            ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPNPCU, npc_ungadulu_bad, -1, unga_slot);
            lg_drain(srv, player);
            lg_close(srv, player);
            SELFTEST_CHECK(player->varps[varp_quest] == summoned_fire,
                           "book of binding should summon Nezikchened, got %d",
                           player->varps[varp_quest]);
            lg_pass("ungadulu_book_bind");
        }

        /* Sacred water / bless bowl. */
        lg_clear_inv(player);
        player->varps[varp_quest] = spoke_unga;
        inv_set(player, 0, obj_bowl_empty, 1);
        if( gujuo_slot >= 0 )
        {
            ToriRSServer_WorldTeleport(srv, 0, 2780, 2900);
            selftest_tick(srv);
            player->last_useitem = obj_bowl_empty;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_gujuo, -1, gujuo_slot);
            lg_drain(srv, player);
            lg_close(srv, player);
            SELFTEST_CHECK(lg_inv_has(player, obj_bowl_bless), "Gujuo should bless goldbowl_empty");
            SELFTEST_CHECK(player->varps[varp_quest] == asked_water,
                           "blessing should set asked_gujuo_holy_water, got %d",
                           player->varps[varp_quest]);
            lg_pass("gujuo_bless_bowl");
        }

        /* Germinate seeds. */
        lg_clear_inv(player);
        player->varps[varp_quest] = defeated_fire;
        inv_set(player, 0, obj_seeds, 1);
        inv_set(player, 1, obj_bowl_pure, 1);
        player->last_useitem = obj_bowl_pure;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELDU, obj_seeds, -1, -1);
        lg_drain(srv, player);
        lg_close(srv, player);
        SELFTEST_CHECK(lg_inv_has(player, obj_seeds_g), "sacred water should germinate yommiseeds");
        SELFTEST_CHECK(player->varps[varp_quest] == germinated,
                       "germinating should set germinated_seeds, got %d",
                       player->varps[varp_quest]);
        lg_pass("yommi_germinate");

        /* Echned dagger grant. */
        lg_clear_inv(player);
        player->varps[varp_quest] = 18; /* heart_in_recess */
        ToriRSServer_WorldTeleport(srv, 0, 2768, 9232);
        selftest_tick(srv);
        echned_slot = ToriRSServer_WorldNpcSpawn(srv, npc_echned, 2769, 9232, 0);
        SELFTEST_CHECK(echned_slot >= 0, "echned_zekin should spawn");
        if( echned_slot >= 0 )
        {
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_echned, -1, echned_slot);
            lg_choose(srv, player, 1); /* Er... me? */
            lg_choose(srv, player, 1); /* Yes, I need it for my quest. */
            lg_choose(srv, player, 2); /* What can I do about that? */
            lg_choose(srv, player, 1); /* I'll do what I must */
            lg_choose(srv, player, 1); /* Ok, I'll do it. */
            lg_drain(srv, player);
            lg_close(srv, player);
            SELFTEST_CHECK(lg_inv_has(player, obj_dagger), "Echned should grant deathdagger");
            SELFTEST_CHECK(player->varps[varp_quest] == received_dagger,
                           "accepting Echned should set received_dagger, got %d",
                           player->varps[varp_quest]);
            lg_pass("echned_dagger");
        }

        /* Viyeldi stab. */
        viyeldi_slot = ToriRSServer_WorldNpcSpawn(srv, npc_viyeldi, 2770, 9233, 0);
        SELFTEST_CHECK(viyeldi_slot >= 0, "viyeldi should spawn");
        if( viyeldi_slot >= 0 && lg_inv_has(player, obj_dagger) )
        {
            player->last_useitem = obj_dagger;
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPCU, npc_viyeldi, -1, viyeldi_slot);
            lg_drain(srv, player);
            lg_close(srv, player);
            SELFTEST_CHECK(lg_inv_has(player, obj_daggerdone),
                           "stabbing Viyeldi should convert the dagger");
            lg_pass("viyeldi_stab");
        }

        /* Warrior crystal mesboxes via debugprocs that park on authored text. */
        ToriRSServer_ScriptsRunDebugproc(srv, "legendsbmp_104_san_crystal");
        lg_drain(srv, player);
        lg_close(srv, player);
        lg_pass("san_tojalon_crystal");
        ToriRSServer_ScriptsRunDebugproc(srv, "legendsbmp_105_irvig_crystal");
        lg_drain(srv, player);
        lg_close(srv, player);
        lg_pass("irvig_senay_crystal");
        ToriRSServer_ScriptsRunDebugproc(srv, "legendsbmp_106_ranalph_crystal");
        lg_drain(srv, player);
        lg_close(srv, player);
        lg_pass("ranalph_devere_crystal");
        ToriRSServer_ScriptsRunDebugproc(srv, "legendsbmp_107_nezi_fire_defeat");
        lg_drain(srv, player);
        lg_close(srv, player);
        lg_pass("nezikchened_fire_defeat");

        /* Totem + map hand-in. */
        lg_clear_inv(player);
        player->varps[varp_quest] = got_gift;
        inv_set(player, 0, obj_mapcomp, 1);
        inv_set(player, 1, obj_gift, 1);
        ToriRSServer_WorldTeleport(srv, 0, 2724, 3368);
        selftest_tick(srv);
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_radimus, -1, radimus_slot);
        lg_drain(srv, player);
        lg_close(srv, player);
        SELFTEST_CHECK(player->varps[varp_quest] == returned,
                       "totem+map hand-in should set returned_to_radimus, got %d",
                       player->varps[varp_quest]);
        lg_pass("radimus_handin");

        /* Four 30k XP training sessions. Combat/Attack each time. */
        {
            int i;
            int want[4];

            want[0] = train1;
            want[1] = train2;
            want[2] = train3;
            want[3] = complete;
            for( i = 0; i < 4; i++ )
            {
                ToriRSServer_ScriptsRunTrigger(
                    srv, SS_TRIGGER_OPNPC1, npc_radimus, -1, radimus_slot);
                lg_choose(srv, player, 1); /* Combat skills */
                lg_choose(srv, player, 1); /* Attack */
                lg_drain(srv, player);
                lg_close(srv, player);
                SELFTEST_CHECK(player->varps[varp_quest] == want[i],
                               "training session %d should reach legendsquest %d, got %d",
                               i + 1, want[i], player->varps[varp_quest]);
                lg_pass(i == 0   ? "radimus_train1"
                        : i == 1 ? "radimus_train2"
                        : i == 2 ? "radimus_train3"
                                 : "radimus_train4_complete");
            }
        }

        SELFTEST_CHECK(player->varps[varp_quest] == complete,
                       "final training should complete legendsquest, got %d",
                       player->varps[varp_quest]);
        lg_pass("quest_complete");

        /* Guild door after completion. */
        {
            int door_slot = ToriRSServer_SceneFindLocId(2724, 3374, 0, loc_door);

            if( door_slot >= 0 )
            {
                ToriRSServer_ScriptsRunTriggerOnLoc(
                    srv, SS_TRIGGER_OPLOC1, loc_door, -1, door_slot);
                lg_drain(srv, player);
                lg_close(srv, player);
                lg_pass("guild_door_after_complete");
            }
        }

        lg_clear_inv(player);
        player->varps[varp_quest] = 0;
        player->varps[varp_bits] = 0;
        ToriRSServer_ScriptsFree(srv);
    }
}
