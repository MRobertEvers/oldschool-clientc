/* Murder Mystery Gate D stanza. Included from torirs_server_world_selftest.c
 * immediately before a selftest_reset_world so spawned npcs cannot leak into
 * later RNG-gated checks.
 *
 * Every assertion is a real OPNPC / OPLOC / OPHELD dispatch on the critical
 * path. Silent success is forbidden: each step prints an ASCII PASS line.
 *
 * Wiki RAW oldids (2026-09-09): article 15315154, quick guide 15078493,
 * transcript 15263270. QH murdermystery steps.put(0) talkToGuard,
 * steps.put(1) investigating; complete is dbrow endstate 2.
 * Culprit is pinned to Anna after the real accept so flour/print/compost
 * stay deterministic (wiki: random 1-of-6 per player).
 */
static void
murder_pass(const char* step)
{
    assert(step);
    fprintf(stderr, "MURDERMYSTERY PASS: %s\n", step);
}

static void
murder_clear_inv(struct ToriRSServerPlayer* player)
{
    int s;

    assert(player);
    for( s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        inv_set(player, s, -1, 0);
}

static void
murder_close(struct ToriRSServer* srv)
{
    assert(srv);
    ToriRSServer_WorldCloseModal(srv);
}

static void
murder_run_dialogue(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int choice_uid)
{
    int round;

    assert(srv);
    assert(player);
    for( round = 0; round < 48 && player->active_script != NULL; round++ )
    {
        int exec = player->active_script->execution;

        if( exec == SSVM_PAUSEBUTTON )
        {
            int uid;

            if( player->resume_button_count <= 0 )
                break;
            uid = player->resume_buttons[0];
            if( choice_uid > 0 && uid == choice_uid )
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
murder_pick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int choice_uid,
    int slot)
{
    assert(srv);
    assert(player);
    murder_run_dialogue(srv, player, choice_uid);
    player->last_slot = slot;
    if( choice_uid > 0 )
        ToriRSServer_ScriptsResumeButton(srv, choice_uid);
    murder_run_dialogue(srv, player, 0);
}

static int
murder_place_loc(
    struct ToriRSServer* srv,
    int x,
    int z,
    int loc_id)
{
    int slot;

    assert(srv);
    assert(loc_id >= 0);
    slot = ToriRSServer_SceneFindLocId(x, z, 0, loc_id);
    if( slot >= 0 )
        return slot;
    return ToriRSServer_WorldLocSet(srv, x, z, 0, 10, loc_id, 0, TORIRSSERVER_LOC_SET_ADD);
}

static void
murder_free_npc(struct ToriRSServer* srv, int slot)
{
    assert(srv);
    if( slot >= 0 )
    {
        ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_WorldNpcReap(srv);
    }
}

static void
selftest_quest_murder(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int loaded;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: ::murdermysteryrun\n");

    loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    {
        int npc_guard = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "murderguard");
        int npc_anna = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "anna_sinclair");
        int npc_salesman = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "poison_salesman");
        int npc_gossip = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "gossipy_man");
        int varp_mq = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "murderquest");
        int varp_sus = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "murdersus");
        int varp_ev = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "murder_evidence");
        int varp_pp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "murder_poisonproof_progress");
        int varp_qp = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "qp");
        int loc_window = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "murderwindow");
        int loc_window_child =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "kr_mansion_window_multi_01");
        int loc_barrel = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "murderbarrela");
        int loc_flour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "flourbarrel");
        int loc_sacks = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "murdersacks");
        int loc_compost = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "murdercompost");
        int obj_thread = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murderthreadg");
        int obj_necklace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murdernecklace");
        int obj_neckdust = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murdernecklacedust");
        int obj_weapon = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murderweapon");
        int obj_weapondust = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murderweapondust");
        int obj_paper = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murderpaper");
        int obj_print1 = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murderfingerprint1");
        int obj_printa = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murderfingerprinta");
        int obj_printfound = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "murderfingerprint");
        int obj_pot_empty = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_empty");
        int obj_pot_flour = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "pot_flour");
        int stat_crafting = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, "crafting");
        int chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
        int db_quest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_DBROW, "quest_murdermystery");

        SELFTEST_CHECK(npc_guard >= 0 && npc_anna >= 0 && npc_salesman >= 0 && npc_gossip >= 0 &&
                           varp_mq >= 0 && varp_sus >= 0 && varp_ev >= 0 && varp_pp >= 0 &&
                           loc_window >= 0 && loc_window_child >= 0 && loc_barrel >= 0 &&
                           loc_flour >= 0 && loc_sacks >= 0 && loc_compost >= 0 &&
                           obj_thread >= 0 && obj_necklace >= 0 && obj_weapon >= 0 &&
                           obj_paper >= 0 && obj_print1 >= 0 && obj_printa >= 0 &&
                           obj_printfound >= 0 && obj_pot_empty >= 0 && obj_pot_flour >= 0 &&
                           stat_crafting >= 0 && db_quest >= 0,
                       "the ::murdermysteryrun C-side names should all resolve");
        if( npc_guard < 0 || varp_mq < 0 || loc_window < 0 || obj_weapon < 0 )
        {
            ToriRSServer_ScriptsFree(srv);
            return;
        }

        /* Player unkillable unless this step is a death test. It is not. */
        player->godmode = 1;
        player->dying = 0;
        if( player->max_hitpoints > 0 )
            player->hitpoints = player->max_hitpoints;
        ToriRSServer_CombatSyncHitpoints(player);
        murder_clear_inv(player);
        player->varps[varp_mq] = 0;
        player->varps[varp_sus] = 0;
        player->varps[varp_ev] = 0;
        player->varps[varp_pp] = 0;
        player->chatmodal_group = 0;
        player->active_script = NULL;

        /* ---- OPNPC1 start: refuse then accept (wiki transcript) ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2741, 3562);
        selftest_tick(srv);
        {
            int guard = ToriRSServer_WorldNpcSpawn(srv, npc_guard, 2741, 3562, 0);

            SELFTEST_CHECK(guard >= 0, "murderguard should spawn at 2741,3562");
            if( guard >= 0 )
            {
                int ran;

                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, guard);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "opnpc1 murderguard at state 0 should dispatch, got %d", ran);
                murder_pick(srv, player, chatmenu, 2);
                murder_close(srv);
                SELFTEST_CHECK(player->varps[varp_mq] == 0,
                               "refusing the guard must not write murderquest, got %d",
                               player->varps[varp_mq]);
                murder_pass("opnpc1_guard_refuse");

                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, guard);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "opnpc1 murderguard accept should dispatch, got %d", ran);
                murder_pick(srv, player, chatmenu, 1);
                murder_run_dialogue(srv, player, 0);
                murder_close(srv);
                SELFTEST_CHECK(player->varps[varp_mq] == 1,
                               "accepting the guard should set murderquest=1, got %d",
                               player->varps[varp_mq]);
                SELFTEST_CHECK(player->varps[varp_sus] >= 1 && player->varps[varp_sus] <= 6,
                               "accept should roll murdersus 1..6, got %d",
                               player->varps[varp_sus]);
                /* Pin Anna so barrel/print/compost stay one path. */
                player->varps[varp_sus] = 1;
                murder_pass("opnpc1_guard_accept");
                murder_free_npc(srv, guard);
            }
        }

        /* ---- OPLOC2 smashed window: thread + evidence bit ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2748, 3577);
        selftest_tick(srv);
        {
            int win = murder_place_loc(srv, 2748, 3577, loc_window);
            int ran;

            SELFTEST_CHECK(win >= 0, "murderwindow should stand at 2748,3577");
            if( win < 0 && loc_window_child >= 0 )
                win = murder_place_loc(srv, 2748, 3577, loc_window_child);
            if( win >= 0 )
            {
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_window, -1,
                                                         win);
                if( ran != TORIRSSERVER_TRIGGER_RAN && loc_window_child >= 0 )
                    ran = ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2,
                                                             loc_window_child, -1, win);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "oploc2 murderwindow should dispatch, got %d", ran);
                murder_run_dialogue(srv, player, 0);
                /* Do not fire a second window op: a parked first click plus a
                 * second dispatch takes the "already have thread" return and
                 * never writes the evidence bit. */
                murder_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_thread) > 0,
                               "window search should grant green thread");
                /* setbit(x, 1) is 1<<1 (ss_vm_test: setbit(0,4)==16). */
                SELFTEST_CHECK((player->varps[varp_ev] & 2) != 0,
                               "window search should set murder_found_thread, ev=%d",
                               player->varps[varp_ev]);
                murder_pass("oploc2_window_thread");
            }
        }

        /* ---- OPLOC2 Anna's barrel ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2733, 3575);
        selftest_tick(srv);
        {
            int barrel = murder_place_loc(srv, 2733, 3575, loc_barrel);
            int ran;

            SELFTEST_CHECK(barrel >= 0, "murderbarrela should stand at 2733,3575");
            if( barrel >= 0 )
            {
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_barrel, -1,
                                                         barrel);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "oploc2 murderbarrela should dispatch, got %d", ran);
                murder_run_dialogue(srv, player, 0);
                murder_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_necklace) > 0,
                               "Anna barrel should grant silver necklace");
                murder_pass("oploc2_barrel_necklace");
            }
        }

        /* ---- OPLOC2 flour barrel (needs empty pot) ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2733, 3582);
        selftest_tick(srv);
        {
            int flour = murder_place_loc(srv, 2733, 3582, loc_flour);
            int ran;

            SELFTEST_CHECK(flour >= 0, "flourbarrel should stand at 2733,3582");
            if( flour >= 0 )
            {
                selftest_give(player, obj_pot_empty, 1);
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_flour, -1,
                                                         flour);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "oploc2 flourbarrel should dispatch, got %d", ran);
                murder_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_pot_flour) > 0,
                               "flour barrel should fill pot_empty");
                murder_pass("oploc2_flour_barrel");
            }
        }

        /* ---- OPLOC2 sacks: take flypaper (choice 1) ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2731, 3582);
        selftest_tick(srv);
        {
            int sacks = murder_place_loc(srv, 2731, 3582, loc_sacks);
            int ran;

            SELFTEST_CHECK(sacks >= 0, "murdersacks should stand at 2731,3582");
            if( sacks >= 0 )
            {
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_sacks, -1,
                                                         sacks);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "oploc2 murdersacks should dispatch, got %d", ran);
                murder_pick(srv, player, chatmenu, 1);
                murder_run_dialogue(srv, player, 0);
                murder_close(srv);
                SELFTEST_CHECK(selftest_count_obj(player, obj_paper) > 0,
                               "sacks should grant flypaper on Yes");
                murder_pass("oploc2_sacks_flypaper");
            }
        }

        /* ---- OPHELDU flour on dagger, flypaper on dust, compare Anna ---- */
        {
            selftest_useon(srv, obj_pot_flour, 1, obj_weapon, 1, NULL, 0);
            SELFTEST_CHECK(selftest_count_obj(player, obj_weapondust) > 0,
                           "opheldu pot_flour on murderweapon should dust it");
            murder_pass("opheldu_flour_dagger");

            selftest_useon(srv, obj_paper, 1, obj_weapondust, 1, NULL, 0);
            SELFTEST_CHECK(selftest_count_obj(player, obj_print1) > 0,
                           "opheldu flypaper on dusted dagger should lift unknown print");
            murder_pass("opheldu_paper_dagger");

            selftest_useon(srv, obj_pot_flour, 1, obj_necklace, 1, NULL, 0);
            SELFTEST_CHECK(selftest_count_obj(player, obj_neckdust) > 0,
                           "opheldu pot_flour on necklace should dust it");
            murder_pass("opheldu_flour_necklace");

            selftest_useon(srv, obj_paper, 1, obj_neckdust, 1, NULL, 0);
            SELFTEST_CHECK(selftest_count_obj(player, obj_printa) > 0,
                           "opheldu flypaper on dusted necklace should lift Anna print");
            murder_pass("opheldu_paper_necklace");

            /* Compare needs both prints in inv; useon plants both. */
            player->varps[varp_sus] = 1;
            selftest_useon(srv, obj_printa, 1, obj_print1, 1, NULL, 0);
            SELFTEST_CHECK((player->varps[varp_ev] & 4) != 0,
                           "matching Anna print should set murder_found_fingerprints, ev=%d",
                           player->varps[varp_ev]);
            SELFTEST_CHECK(selftest_count_obj(player, obj_printfound) > 0,
                           "match should replace unknown print with murderfingerprint");
            murder_pass("opheldu_compare_anna");
        }

        /* ---- OPNPC1 gossip (heard-about-salesman flavour) ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2741, 3557);
        selftest_tick(srv);
        {
            int gossip = ToriRSServer_WorldNpcSpawn(srv, npc_gossip, 2741, 3557, 0);
            int ran;

            SELFTEST_CHECK(gossip >= 0, "gossipy_man should spawn");
            if( gossip >= 0 )
            {
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_gossip, -1, gossip);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "opnpc1 gossipy_man should dispatch, got %d", ran);
                murder_pick(srv, player, chatmenu, 2);
                murder_run_dialogue(srv, player, 0);
                murder_close(srv);
                murder_pass("opnpc1_gossip");
                murder_free_npc(srv, gossip);
            }
        }

        /* ---- OPNPC1 poison salesman: who did you sell to ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2694, 3493);
        selftest_tick(srv);
        {
            int sales = ToriRSServer_WorldNpcSpawn(srv, npc_salesman, 2694, 3493, 0);
            int ran;

            SELFTEST_CHECK(sales >= 0, "poison_salesman should spawn");
            if( sales >= 0 )
            {
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_salesman, -1,
                                                     sales);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "opnpc1 poison_salesman should dispatch, got %d", ran);
                murder_pick(srv, player, chatmenu, 2);
                murder_run_dialogue(srv, player, 0);
                murder_close(srv);
                SELFTEST_CHECK(player->varps[varp_pp] == 1,
                               "salesman customer list should set spoken_salesman, got %d",
                               player->varps[varp_pp]);
                murder_pass("opnpc1_poison_salesman");
                murder_free_npc(srv, sales);
            }
        }

        /* ---- OPNPC1 Anna: why buy poison (option 4) ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2734, 3575);
        selftest_tick(srv);
        {
            int anna = ToriRSServer_WorldNpcSpawn(srv, npc_anna, 2734, 3575, 0);
            int ran;

            SELFTEST_CHECK(anna >= 0, "anna_sinclair should spawn");
            if( anna >= 0 )
            {
                player->varps[varp_sus] = 1;
                player->varps[varp_pp] = 1;
                player->varps[varp_ev] |= 1;
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_anna, -1, anna);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "opnpc1 anna_sinclair should dispatch, got %d", ran);
                murder_pick(srv, player, chatmenu, 4);
                murder_run_dialogue(srv, player, 0);
                murder_close(srv);
                SELFTEST_CHECK(player->varps[varp_pp] == 2,
                               "Anna poison lie should set spoken_murderer, got %d",
                               player->varps[varp_pp]);
                murder_pass("opnpc1_anna_poison");
                murder_free_npc(srv, anna);
            }
        }

        /* ---- OPLOC2 compost (Anna's unused-poison proof) ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2730, 3572);
        selftest_tick(srv);
        {
            int compost = murder_place_loc(srv, 2730, 3572, loc_compost);
            int ran;

            SELFTEST_CHECK(compost >= 0, "murdercompost should stand at 2730,3572");
            if( compost >= 0 )
            {
                player->varps[varp_sus] = 1;
                player->varps[varp_pp] = 2;
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC2, loc_compost, -1,
                                                         compost);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "oploc2 murdercompost should dispatch, got %d", ran);
                murder_run_dialogue(srv, player, 0);
                murder_close(srv);
                SELFTEST_CHECK(player->varps[varp_pp] == 3,
                               "Anna compost should set searched_loc, got %d",
                               player->varps[varp_pp]);
                murder_pass("oploc2_compost_anna");
            }
        }

        /* ---- OPNPC1 guard conclusive proof -> complete queue ---- */
        ToriRSServer_WorldTeleport(srv, 0, 2741, 3562);
        selftest_tick(srv);
        {
            int guard = ToriRSServer_WorldNpcSpawn(srv, npc_guard, 2741, 3562, 0);
            int ran;
                int xp_before;
                int drain;
                int obj_coins;

            SELFTEST_CHECK(guard >= 0, "murderguard should spawn for the accusation");
            if( guard >= 0 )
            {
                player->varps[varp_mq] = 1;
                player->varps[varp_sus] = 1;
                player->varps[varp_pp] = 3;
                player->varps[varp_ev] = 6; /* setbit thread(1) + fingerprints(2) */
                murder_clear_inv(player);
                if( obj_thread >= 0 )
                    selftest_give(player, obj_thread, 1);
                if( obj_printfound >= 0 )
                    selftest_give(player, obj_printfound, 1);
                xp_before = player->stat_xp_tenths[stat_crafting];
                obj_coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
                player->chatmodal_group = 0;
                player->active_script = NULL;
                ran = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, npc_guard, -1, guard);
                SELFTEST_CHECK(ran == TORIRSSERVER_TRIGGER_RAN,
                               "opnpc1 murderguard accusation should dispatch, got %d", ran);
                murder_pick(srv, player, chatmenu, 3);
                murder_run_dialogue(srv, player, 0);
                /* Resume every mesbox/chat first so queue(murder_quest_complete)
                 * is armed. Close-then-tick aborts a parked script (field
                 * guide S5) and would leave murderquest at 1 forever. */
                for( drain = 0; drain < 64 && player->varps[varp_mq] != 2; drain++ )
                {
                    if( player->active_script != NULL )
                        murder_run_dialogue(srv, player, 0);
                    else
                    {
                        murder_close(srv);
                        selftest_tick(srv);
                    }
                }
                SELFTEST_CHECK(player->varps[varp_mq] == 2,
                               "conclusive proof should complete murderquest, got %d",
                               player->varps[varp_mq]);
                SELFTEST_CHECK(player->stat_xp_tenths[stat_crafting] > xp_before,
                               "completion should award Crafting xp, %d -> %d",
                               xp_before, player->stat_xp_tenths[stat_crafting]);
                SELFTEST_CHECK(obj_coins >= 0 && selftest_count_obj(player, obj_coins) >= 2000,
                               "completion should grant 2000 coins");
                murder_pass("opnpc1_guard_complete");
                murder_free_npc(srv, guard);
            }
        }

        /* ---- ::complete twice: first sets endstate, second is a no-op ---- */
        {
            static const uint8_t command[] = "complete quest_murdermystery\n";
            int qp_before;
            int endstate = 2;
            int points = 3;

            if( varp_qp >= 0 && varp_mq >= 0 )
            {
                player->varps[varp_mq] = 0;
                qp_before = player->varps[varp_qp];
                handle_cheat(srv, command, (int)sizeof(command) - 1);
                SELFTEST_CHECK(player->varps[varp_mq] == endstate,
                               "::complete quest_murdermystery should set endstate %d, got %d",
                               endstate, player->varps[varp_mq]);
                SELFTEST_CHECK(player->varps[varp_qp] == qp_before + points,
                               "::complete should pay %d QP (%d -> %d)", points, qp_before,
                               player->varps[varp_qp]);
                handle_cheat(srv, command, (int)sizeof(command) - 1);
                SELFTEST_CHECK(player->varps[varp_qp] == qp_before + points,
                               "second ::complete must not pay again (%d -> %d)",
                               qp_before + points, player->varps[varp_qp]);
                murder_pass("complete_idempotent");
            }
        }

        murder_clear_inv(player);
        player->varps[varp_mq] = 0;
        player->varps[varp_sus] = 0;
        player->varps[varp_ev] = 0;
        player->varps[varp_pp] = 0;
        player->godmode = 1;
        ToriRSServer_ScriptsFree(srv);
    }
}
