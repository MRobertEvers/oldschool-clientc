/* The Forsaken Tower C-walk. Worker branch only — parent will not merge this.
 *
 *   TORIRSSERVER_SELFTEST_FT_ONLY=1 TORIRSSERVER_GOD=1 TORIRS_PLUGINS=0 \
 *       ./<obj>/torirsserver --selftest
 *
 * Player is unkillable unless a step is a death case. This walk has none.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FT_CWALK_NOT_STARTED
#define FT_CWALK_NOT_STARTED 0
#define FT_CWALK_UNDOR 2
#define FT_CWALK_TOWER 3
#define FT_CWALK_PUZZLE 4
#define FT_CWALK_HAMMER 8
#define FT_CWALK_RETURN 10
#define FT_CWALK_COMPLETE 11
#define FT_CWALK_PUZZLE_DONE 4
#define FT_CWALK_ALTAR_DONE 2
#define FT_CWALK_COK_COMPLETE 7
#define FT_CWALK_XMARKS_COMPLETE 8
#define FT_CWALK_COINS 6000
#endif

static int
ft_cwalk_inv_count(const struct ToriRSServerPlayer* player, int obj_id)
{
    int total;
    int slot;

    assert(player);
    assert(obj_id > 0);
    total = 0;
    for( slot = 0; slot < TORIRSSERVER_INV_SLOTS; slot++ )
    {
        if( player->inv[slot].obj_id == obj_id )
            total += player->inv[slot].count;
    }
    return total;
}

static void
ft_cwalk_clear_inv(struct ToriRSServerPlayer* player)
{
    int slot;

    assert(player);
    for( slot = 0; slot < TORIRSSERVER_INV_SLOTS; slot++ )
        inv_set(player, slot, -1, 0);
}

static void
selftest_quest_forsakentower(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    const char* scripts;
    int loaded;
    int lovaquest;
    int furnace;
    int electricity;
    int refinery;
    int altar;
    int foundhammer;
    int reward;
    int veos;
    int cluequest;
    int coins;
    int hammer;
    int page;
    int32_t why;
    int32_t puzzles;

    assert(srv);
    assert(player);

    fprintf(stderr, "ToriRSServer selftest: The Forsaken Tower\n");

    setenv("TORIRSSERVER_GOD", "1", 1);
    setenv("TORIRS_PLUGINS", "0", 1);
    player->godmode = 1;
    ToriRSServer_WorldSetActive(srv, player);

    loaded = srv->scripts_ok;
    if( !loaded )
    {
        scripts = getenv("TORIRSSERVER_SCRIPTS");
        loaded = ToriRSServer_ScriptsLoad(
            srv, scripts ? scripts : selftest_scripts_dir());
        if( !loaded && !scripts )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    }
    SELFTEST_CHECK(loaded && srv->scripts_ok,
                   "Forsaken Tower C-walk needs a compiled script pack");
    if( !loaded || !srv->scripts_ok )
        return;

    lovaquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lovaquest");
    furnace = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lovaquest_furnace");
    electricity = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lovaquest_electricity");
    refinery = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lovaquest_refinery");
    altar = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lovaquest_altar");
    foundhammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lovaquest_foundhammer");
    reward = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "lovaquest_reward");
    veos = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "veos_progress");
    cluequest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "cluequest");
    coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
    hammer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "lovaquest_hammer");
    page = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "veos_memoirs_lova_page");

    assert(lovaquest >= 0);
    assert(furnace >= 0);
    assert(electricity >= 0);
    assert(refinery >= 0);
    assert(altar >= 0);
    assert(foundhammer >= 0);
    assert(reward >= 0);
    assert(veos >= 0);
    assert(cluequest >= 0);
    assert(coins > 0);
    assert(hammer > 0);
    assert(page > 0);

    SELFTEST_CHECK(player->godmode == 1, "player stays unkillable for the FT walk");

    ft_cwalk_clear_inv(player);
    ToriRSServer_VarbitSet(srv, lovaquest, FT_CWALK_NOT_STARTED);
    ToriRSServer_VarbitSet(srv, furnace, 0);
    ToriRSServer_VarbitSet(srv, electricity, 0);
    ToriRSServer_VarbitSet(srv, refinery, 0);
    ToriRSServer_VarbitSet(srv, altar, 0);
    ToriRSServer_VarbitSet(srv, foundhammer, 0);
    ToriRSServer_VarbitSet(srv, reward, 0);
    ToriRSServer_VarbitSet(srv, veos, 0);
    ToriRSServer_VarbitSet(srv, cluequest, 0);

    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,ft_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 1,
                   "missing Client of Kourend is qualify reason 1, got %d", why);

    ToriRSServer_VarbitSet(srv, veos, FT_CWALK_COK_COMPLETE);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,ft_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 2,
                   "missing X Marks the Spot is qualify reason 2, got %d", why);

    ToriRSServer_VarbitSet(srv, cluequest, FT_CWALK_XMARKS_COMPLETE);
    why = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,ft_qualify_fail_reason]", NULL, 0, &why) &&
                       why == 0,
                   "CoK + X Marks qualify with no skill gate, got %d", why);

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,ft_show_qualify_fail_cok]", NULL, 0),
                   "CoK qualify-fail mesbox proc exists");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,ft_show_qualify_fail_xmarks]", NULL, 0),
                   "X Marks qualify-fail mesbox proc exists");

    ToriRSServer_VarbitSet(srv, lovaquest, FT_CWALK_NOT_STARTED);
    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_refuse]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, lovaquest) == FT_CWALK_NOT_STARTED,
                   "refuse must not auto-start, %%lovaquest=%d",
                   ToriRSServer_VarbitGet(player, lovaquest));

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_accept]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, lovaquest) == FT_CWALK_UNDOR,
                   "accept writes %%lovaquest=^ft_undor, got %d",
                   ToriRSServer_VarbitGet(player, lovaquest));

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_undor_send]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, lovaquest) == FT_CWALK_TOWER,
                   "Undor send writes %%lovaquest=^ft_tower, got %d",
                   ToriRSServer_VarbitGet(player, lovaquest));

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_tower_enter]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, lovaquest) == FT_CWALK_PUZZLE,
                   "tower entry writes %%lovaquest=^ft_puzzle, got %d",
                   ToriRSServer_VarbitGet(player, lovaquest));

    puzzles = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,ft_puzzles_done]", NULL, 0, &puzzles) &&
                       puzzles == 0,
                   "puzzles start incomplete, got %d", puzzles);

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_furnace_softskip]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, furnace) == FT_CWALK_PUZZLE_DONE,
                   "furnace soft-skip writes ^ft_puzzle_done, got %d",
                   ToriRSServer_VarbitGet(player, furnace));

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_power_softskip]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, electricity) == FT_CWALK_PUZZLE_DONE,
                   "power soft-skip writes ^ft_puzzle_done, got %d",
                   ToriRSServer_VarbitGet(player, electricity));

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_refinery_softskip]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, refinery) == FT_CWALK_PUZZLE_DONE,
                   "refinery soft-skip writes ^ft_puzzle_done, got %d",
                   ToriRSServer_VarbitGet(player, refinery));

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_pylon_softskip]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, altar) == FT_CWALK_ALTAR_DONE,
                   "pylon soft-skip writes ^ft_altar_done, got %d",
                   ToriRSServer_VarbitGet(player, altar));

    puzzles = -1;
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProcInt(
                       srv, "[proc,ft_puzzles_done]", NULL, 0, &puzzles) &&
                       puzzles == 1,
                   "all four systems restore, got %d", puzzles);

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_display_take_hammer]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, lovaquest) == FT_CWALK_HAMMER,
                   "display case writes %%lovaquest=^ft_hammer, got %d",
                   ToriRSServer_VarbitGet(player, lovaquest));
    SELFTEST_CHECK(ft_cwalk_inv_count(player, hammer) >= 1,
                   "display case gives Dinh's hammer, count=%d",
                   ft_cwalk_inv_count(player, hammer));
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, foundhammer) > 0,
                   "display case stamps %%lovaquest_foundhammer");

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_undor_take_hammer]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, lovaquest) == FT_CWALK_RETURN,
                   "Undor return writes %%lovaquest=^ft_return, got %d",
                   ToriRSServer_VarbitGet(player, lovaquest));
    SELFTEST_CHECK(ft_cwalk_inv_count(player, hammer) == 0,
                   "Undor takes Dinh's hammer, leftover=%d",
                   ft_cwalk_inv_count(player, hammer));

    (void)ToriRSServer_ScriptsRunProc(srv, "[proc,ft_quest_complete]", NULL, 0);
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, lovaquest) == FT_CWALK_COMPLETE,
                   "complete writes %%lovaquest=^ft_complete, got %d",
                   ToriRSServer_VarbitGet(player, lovaquest));
    SELFTEST_CHECK(ToriRSServer_VarbitGet(player, reward) == 1,
                   "complete stamps %%lovaquest_reward");
    SELFTEST_CHECK(ft_cwalk_inv_count(player, coins) >= FT_CWALK_COINS,
                   "complete awards 6000 coins, got %d",
                   ft_cwalk_inv_count(player, coins));
    SELFTEST_CHECK(ft_cwalk_inv_count(player, page) >= 1,
                   "complete awards Jewellery of jubilation page, count=%d",
                   ft_cwalk_inv_count(player, page));

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,forsakentower_journal]", NULL, 0),
                   "journal proc exists and prints QUEST COMPLETE at endstate");

    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,ft_leftover_jug_coolant_puzzle]", NULL, 0),
                   "leftover_jug_coolant_puzzle disclosure exists");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,ft_leftover_power_grid_if]", NULL, 0),
                   "leftover_power_grid_if disclosure exists");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(
                       srv, "[proc,ft_leftover_fluid_refinery_puzzle]", NULL, 0),
                   "leftover_fluid_refinery_puzzle disclosure exists");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,ft_leftover_pylon_altar_puzzle]", NULL, 0),
                   "leftover_pylon_altar_puzzle disclosure exists");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(
                       srv, "[proc,ft_leftover_ignisia_wintertodt_gate]", NULL, 0),
                   "leftover_ignisia_wintertodt_gate disclosure exists");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(srv, "[proc,ft_leftover_full_refuse_trees]", NULL, 0),
                   "leftover_full_refuse_trees disclosure exists");
    SELFTEST_CHECK(ToriRSServer_ScriptsRunProc(
                       srv, "[proc,ft_leftover_graceful_recolour_ui]", NULL, 0),
                   "leftover_graceful_recolour_ui disclosure exists");

    SELFTEST_CHECK(player->godmode == 1, "godmode still on at end of FT walk");
}
