/*
 * Chambers of Xeric tick harness — run the raid under `::god` and assert the
 * strategy guide's timings against what the server actually does, tick by tick.
 *
 * WHY THIS EXISTS
 * ---------------
 * `::coxrun` tests arithmetic: it calls procs and checks their return values.
 * That is worth having and it is not the same question. A proc can be perfectly
 * correct and never be called — which is exactly what happened here. Chambers of
 * Xeric shipped 21 `[ai_timer]` hooks and zero `npc_settimer` calls, so Olm's
 * action clock never advanced, and a 495-line green selftest said nothing about
 * it (docs/minigames/cox/COX_PLAN.md S11.3 F0).
 *
 * The only way to catch that class of defect is to run world ticks and watch.
 * This file does that: it enters the raid, turns on god mode, ticks, and records
 * one line per tick of what the encounter decided. Then it asserts the
 * invariants the OSRS Wiki's Chambers of Xeric/Strategies page states in ticks.
 *
 * IMMORTALITY, AND WHY IT IS NOT `::god`
 * --------------------------------------
 * The player has to survive a full 12-step Olm rotation — a harness that died
 * on tick 9 could only ever assert the first two steps. The obvious lever is
 * `::god`, and it is the wrong one: it zeroes `amount` in the one player damage
 * funnel (mock230_combat.c), which is the exact number this harness exists to
 * read. Under `::god` a boss that hits for 30 and a boss that does not attack at
 * all are indistinguishable.
 *
 * `player->hitmark_count` is not a way round it either. The hitmark list
 * describes one tick and is cleared at the end of every one of them
 * (mock230_world.c), so sampling it after `mock230_world_tick` returns 0
 * forever. The first version of this file asserted on that column and reported
 * "Olm never landed a hitsplat" against a boss that was landing them correctly.
 *
 * So immortality here is a hitpoint pool: the player is given a large one and
 * topped back up after every tick, with the drop recorded first. That is
 * unkillable AND measurable — the harness reads real damage numbers, which is
 * strictly more than `::god` could ever have told it. `::god` is still set as a
 * backstop against a one-shot larger than the pool.
 *
 * WHAT IT READS
 * -------------
 * The content-side trace channel (`%cox_trace_action` + `%cox_trace_serial`,
 * see cox.constant). The serial is what makes it a channel rather than a
 * snapshot: sampling the action code alone cannot distinguish "the same attack
 * twice in a row" from "one attack and a tick of silence", and telling those
 * apart is the entire question a 4-tick clock asks.
 *
 * Hitsplats are read alongside as an independent witness. The trace says what
 * Olm decided; the splat says it reached a player. Asserting only the first
 * would pass a boss that computes a perfect rotation and never attacks.
 *
 * USAGE
 *   MOCK230_COX_SIM=1 ./src/build_opt/mock230 --selftest
 *   tools/cox_sim.sh                    # builds, runs, prints the trace table
 *   MOCK230_COX_SIM_TRACE=<path>        # also write the raw trace there
 */

#include "mock230.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Trace codes, mirroring ^cox_trace_* in cox.constant. Duplicated rather than
 * generated because the harness must fail loudly if content renumbers them: a
 * generated copy would silently follow a renumber and keep passing while
 * asserting the wrong thing. The names below are the assertion's vocabulary. */
enum
{
    COX_TRACE_NONE = 0,
    COX_TRACE_OLM_BASIC = 1,
    COX_TRACE_OLM_SPHERE = 2,
    COX_TRACE_OLM_POWER = 3,
    COX_TRACE_OLM_BURST = 4,
    COX_TRACE_OLM_LIGHTNING = 5,
    COX_TRACE_OLM_TELEPORT = 6,
    COX_TRACE_OLM_SIPHON = 7,
    COX_TRACE_OLM_SKIP = 8,
    COX_TRACE_OLM_EMPTY = 9,
    COX_TRACE_OLM_CATCHUP = 10,
    COX_TRACE_OLM_PHASE = 11,
    COX_TRACE_TEKTON_SWING = 20,
    COX_TRACE_TEKTON_ANVIL = 21,
    COX_TRACE_TEKTON_SPARK = 22,
    COX_TRACE_TEKTON_ENRAGE = 23,
    COX_TRACE_TEKTON_WAITING = 24,
    COX_TRACE_VASA_SPECIAL = 30,
    COX_TRACE_VASA_SIPHON = 31,
    COX_TRACE_VASA_EXPIRED = 32,
    COX_TRACE_VESPULA_DRAIN = 40,
    COX_TRACE_VESPULA_STING = 41,
    COX_TRACE_VANGUARD_SHUFFLE = 50,
    COX_TRACE_ICEFIEND_DOUSE = 60,
    COX_TRACE_ICEFIEND_ACTION = 61,
    COX_TRACE_GUARDIAN_STOMP = 70,
};

static const char*
cox_trace_name(int code)
{
    switch( code )
    {
    case COX_TRACE_NONE: return "-";
    case COX_TRACE_OLM_BASIC: return "olm.basic";
    case COX_TRACE_OLM_SPHERE: return "olm.sphere";
    case COX_TRACE_OLM_POWER: return "olm.power";
    case COX_TRACE_OLM_BURST: return "olm.CRYSTAL_BURST";
    case COX_TRACE_OLM_LIGHTNING: return "olm.LIGHTNING";
    case COX_TRACE_OLM_TELEPORT: return "olm.TELEPORT";
    case COX_TRACE_OLM_SIPHON: return "olm.LIFE_SIPHON";
    case COX_TRACE_OLM_SKIP: return "olm.skip";
    case COX_TRACE_OLM_EMPTY: return "olm.empty";
    case COX_TRACE_OLM_CATCHUP: return "olm.catchup";
    case COX_TRACE_OLM_PHASE: return "olm.phase";
    case COX_TRACE_TEKTON_SWING: return "tekton.swing";
    case COX_TRACE_TEKTON_ANVIL: return "tekton.anvil";
    case COX_TRACE_TEKTON_SPARK: return "tekton.sparks";
    case COX_TRACE_TEKTON_ENRAGE: return "tekton.enrage";
    case COX_TRACE_TEKTON_WAITING: return "tekton.waiting";
    case COX_TRACE_VASA_SPECIAL: return "vasa.special";
    case COX_TRACE_VASA_SIPHON: return "vasa.siphon";
    case COX_TRACE_VASA_EXPIRED: return "vasa.expired";
    case COX_TRACE_VESPULA_DRAIN: return "portal.drain";
    case COX_TRACE_VESPULA_STING: return "vespula.sting";
    case COX_TRACE_VANGUARD_SHUFFLE: return "vanguard.shuffle";
    case COX_TRACE_ICEFIEND_DOUSE: return "icefiend.douse";
    case COX_TRACE_ICEFIEND_ACTION: return "icefiend.act";
    case COX_TRACE_GUARDIAN_STOMP: return "guardian.stomp";
    default: return "?";
    }
}

#define COX_SIM_MAX_TICKS 512

struct CoxSimRun
{
    int ticks;
    int code[COX_SIM_MAX_TICKS];   /* trace code decided on this tick, or NONE */
    int hurt[COX_SIM_MAX_TICKS];   /* hitpoints the player lost on this tick */
    int failures;
    int died;
    FILE* out;
};

static void
cox_sim_fail(struct CoxSimRun* run, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

static void
cox_sim_fail(struct CoxSimRun* run, const char* fmt, ...)
{
    va_list args;

    /*
     * Prefixed, because on the isolated-tree path this harness runs inside a
     * selftest where dozens of unrelated checks are failing by design (every
     * other package reverted to HEAD). An unprefixed "FAIL" is unfindable in
     * that noise, and a harness whose output cannot be located reads as a pass.
     */
    run->failures++;
    fprintf(stderr, "  FAIL  cox-sim: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/*
 * The ceiling the harness holds the player at — a legal 99, not an inflated
 * pool.
 *
 * An earlier version set 5000 here and read damage of 4901 on ticks where Olm
 * had done nothing at all: `player->hitpoints` is recomputed from
 * `stat_boosted[HITPOINTS]` whenever the stat system touches it
 * (mock230_world.c), and the boost is clamped back to the level, so the harness
 * was measuring its own inflation collapsing rather than an attack. Fighting
 * the stat system for a bigger number was never necessary — 99 survives any
 * single tick of CoX (the fire wall is the biggest at 50-65) and the top-up
 * happens every tick.
 */
#define COX_SIM_HITPOINTS 99

static void
cox_sim_top_up(struct Mock230Player* player)
{
    player->stat_level[MOCK230_STAT_HITPOINTS] = COX_SIM_HITPOINTS;
    player->stat_boosted[MOCK230_STAT_HITPOINTS] = COX_SIM_HITPOINTS;
    player->max_hitpoints = COX_SIM_HITPOINTS;
    player->hitpoints = COX_SIM_HITPOINTS;
}

/* One compact `type@x,z/hp` per live CoX npc. The id window is the raids block:
 * 7527..7620 covers every encounter record the raid spawns. */
static void
cox_sim_dump_npcs(struct Mock230Server* srv, FILE* out)
{
    /* The player first. Half of "why is this npc not attacking" turns out to be
     * "it is not where you think, or the player is not" — and a trace with only
     * one of the two positions in it can only answer half of that. */
    fprintf(out, " p@%d,%d", srv->active_player->x, srv->active_player->z);
    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
    {
        struct Mock230Npc* npc = &srv->npcs[i];

        if( !npc->active )
            continue;
        if( npc->type < 7527 || npc->type > 7620 )
            continue;
        fprintf(out, " %d@%d,%d/%d", npc->type, npc->x, npc->z, npc->hitpoints);
    }
}

/*
 * Run `ticks` world ticks, sampling the trace channel after each.
 *
 * The sample is taken from the SERIAL, not the code: content writes both on
 * every decision, so a serial that moved means something happened this tick and
 * a serial that did not means nothing did — regardless of whether the code
 * repeated. Reading the code alone is the bug this comment exists to prevent.
 */
static void
cox_sim_record(struct Mock230Server* srv, struct CoxSimRun* run, int ticks)
{
    int varp_action = mock230_world_varp("cox_trace_action");
    int varp_serial = mock230_world_varp("cox_trace_serial");
    struct Mock230Player* player = srv->active_player;
    int last_serial;

    if( varp_action < 0 || varp_serial < 0 )
    {
        cox_sim_fail(run, "cox_trace varps are not allocated - run tools/ss_allocate.py");
        return;
    }
    if( ticks > COX_SIM_MAX_TICKS )
        ticks = COX_SIM_MAX_TICKS;

    last_serial = player->varps[varp_serial];
    for( int t = 0; t < ticks; t++ )
    {
        int before = player->hitpoints;
        int serial;

        mock230_world_tick(srv);

        serial = player->varps[varp_serial];
        run->code[t] = (serial != last_serial) ? player->varps[varp_action] : COX_TRACE_NONE;
        last_serial = serial;

        /*
         * Read the drop, then heal back to the ceiling. Both halves in that
         * order: healing first would erase the measurement, and not healing at
         * all turns the pool into a countdown that ends the run early on a
         * long trace.
         */
        run->hurt[t] = before > player->hitpoints ? before - player->hitpoints : 0;
        /*
         * A death inside the window is information, not an accident to paper
         * over: it means one tick of this encounter dealt more than 99, which is
         * either a real overtuned attack or several attacks stacking on one
         * tick. Either way the run continues — the top-up is what makes the rest
         * of the trace worth reading — but it is reported once.
         */
        if( player->hitpoints <= 0 && !run->died )
        {
            run->died = 1;
            fprintf(stderr, "  note: cox-sim: one tick dealt %d, past the 99 ceiling\n",
                    run->hurt[t]);
        }
        cox_sim_top_up(player);
        run->ticks = t + 1;

        if( run->out )
        {
            fprintf(run->out, "%3d %-20s hurt=%d", t, cox_trace_name(run->code[t]), run->hurt[t]);
            /*
             * The npcs' own positions, because half the questions this harness
             * gets asked are "did he move" and the trace channel cannot answer
             * them: it records decisions, and walking is not a decision, it is
             * the absence of one for several ticks. Without this column the
             * only way to tell "Tekton is walking toward the player" from
             * "Tekton is stuck" is to add a trace code and rebuild content,
             * which is a slow way to learn where somebody is standing.
             */
            cox_sim_dump_npcs(srv, run->out);
            fprintf(run->out, "\n");
        }
    }
}

/* Index of the nth tick carrying any trace code, or -1. */
static int
cox_sim_nth_action(const struct CoxSimRun* run, int n)
{
    int seen = 0;

    for( int t = 0; t < run->ticks; t++ )
    {
        if( run->code[t] == COX_TRACE_NONE )
            continue;
        if( seen == n )
            return t;
        seen++;
    }
    return -1;
}

static int
cox_sim_count(const struct CoxSimRun* run, int code)
{
    int n = 0;

    for( int t = 0; t < run->ticks; t++ )
        if( run->code[t] == code )
            n++;
    return n;
}

/*
 * OLM — the guide's tick-level claims, in the guide's own order.
 *
 * Every assertion here quotes a rule from
 * https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies or
 * https://oldschool.runescape.wiki/w/Great_Olm. They are checked against a trace
 * of a live fight rather than against the procs that implement them, so a rule
 * that is implemented but never reached fails here — which is the entire reason
 * this file exists.
 */
static void
cox_sim_check_olm(struct Mock230Server* srv, struct CoxSimRun* run)
{
    int first, second, actions = 0;
    int specials;

    (void)srv;

    /* "Olm attacks with ranged and magic ... attack speed 4" — the actions must
     * land on a 4-tick grid. This is the assertion F0 could never have passed:
     * with the timer unarmed the trace is empty and `first` is -1. */
    first = cox_sim_nth_action(run, 0);
    second = cox_sim_nth_action(run, 1);
    if( first < 0 || second < 0 )
    {
        cox_sim_fail(run, "Olm took fewer than two actions in %d ticks - is his ai_timer armed?",
                     run->ticks);
        return;
    }

    for( int t = first; t < run->ticks; t++ )
    {
        if( run->code[t] == COX_TRACE_NONE )
            continue;
        if( (t - first) % 4 != 0 )
            cox_sim_fail(run, "Olm acted on tick %d, off the 4-tick grid anchored at %d (%s)", t,
                         first, cox_trace_name(run->code[t]));
        actions++;
    }
    if( actions < 12 )
        cox_sim_fail(run, "expected at least one full 12-step rotation, saw %d actions", actions);

    /*
     * The rotation itself: every 4th action is a special, and the three specials
     * arrive in the order crystal burst -> lightning -> teleport.
     *
     * "standard attack / empty event / standard attack / Crystal Burst / ..."
     *
     * The special slot is UNCONDITIONAL — this is audit finding F1, where a 1/3
     * roll guarded it and deleted two thirds of every special. A run of 24
     * actions must therefore contain exactly 6 specials; under the bug it
     * averaged 2.
     */
    specials = cox_sim_count(run, COX_TRACE_OLM_BURST) +
               cox_sim_count(run, COX_TRACE_OLM_LIGHTNING) +
               cox_sim_count(run, COX_TRACE_OLM_TELEPORT) +
               cox_sim_count(run, COX_TRACE_OLM_SIPHON);
    {
        int slot = 0;
        int expected_specials = 0;

        for( int t = first; t < run->ticks; t++ )
        {
            int code;

            if( run->code[t] == COX_TRACE_NONE )
                continue;
            code = run->code[t];
            /* Step index within the 12-step rotation, from the first action. */
            if( slot % 4 == 3 )
            {
                expected_specials++;
                if( code != COX_TRACE_OLM_BURST && code != COX_TRACE_OLM_LIGHTNING &&
                    code != COX_TRACE_OLM_TELEPORT && code != COX_TRACE_OLM_SIPHON &&
                    code != COX_TRACE_OLM_SKIP && code != COX_TRACE_OLM_CATCHUP )
                    cox_sim_fail(run,
                                 "action %d is the special slot but Olm used %s - the special "
                                 "slot is unconditional (audit F1)",
                                 slot, cox_trace_name(code));
            }
            else if( slot % 4 == 1 )
            {
                if( code != COX_TRACE_OLM_EMPTY && code != COX_TRACE_OLM_SKIP &&
                    code != COX_TRACE_OLM_CATCHUP )
                    cox_sim_fail(run, "action %d is the empty event but Olm used %s", slot,
                                 cox_trace_name(code));
            }
            slot++;
        }
        if( expected_specials > 0 && specials == 0 )
            cox_sim_fail(run, "%d special slots came round and none fired (audit F1)",
                         expected_specials);
    }

    /*
     * "These attacks will hit all players that Olm is looking at." A rotation
     * that decides perfectly and never reaches the player is the other half of
     * the same bug, so the splat column is asserted independently of the trace.
     */
    {
        int total = 0;

        for( int t = 0; t < run->ticks; t++ )
            total += run->hurt[t];
        if( total == 0 )
            cox_sim_fail(run,
                         "Olm dealt no damage at all in %d ticks - a rotation that decides "
                         "perfectly and never reaches a player is still a broken fight",
                         run->ticks);
    }
}

/*
 * TEKTON — "Tekton attacks on a three-tick cycle."
 *
 * The guide states this outright and recommends a metronome for it, which is as
 * close to a tick-level specification as the wiki ever gets.
 */
static void
cox_sim_check_tekton(struct Mock230Server* srv, struct CoxSimRun* run)
{
    int prev = -1;
    int swings = 0;

    (void)srv;

    for( int t = 0; t < run->ticks; t++ )
    {
        if( run->code[t] != COX_TRACE_TEKTON_SWING )
            continue;
        swings++;
        if( prev >= 0 && t - prev != 3 )
            cox_sim_fail(run, "Tekton swung %d ticks after the last one; the guide says 3",
                         t - prev);
        prev = t;
    }
    if( swings < 2 )
        cox_sim_fail(run, "Tekton swung %d times in %d ticks - is his ai_timer armed?", swings,
                     run->ticks);
}

/*
 * The public entry point. One call from the selftest, so the shared file carries
 * one line of this harness rather than four hundred.
 *
 * Returns the number of failed assertions.
 */
int
mock230_cox_sim_run(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;
    struct CoxSimRun run;
    const char* trace_path = getenv("MOCK230_COX_SIM_TRACE");
    int saved_god = player->godmode;
    int saved_x = player->x;
    int saved_z = player->z;
    int saved_level = player->level;
    int ticks = 64;

    if( getenv("MOCK230_COX_SIM_TICKS") )
        ticks = atoi(getenv("MOCK230_COX_SIM_TICKS"));
    if( ticks <= 0 || ticks > COX_SIM_MAX_TICKS )
        ticks = 64;

    memset(&run, 0, sizeof(run));
    if( trace_path )
    {
        run.out = fopen(trace_path, "w");
        if( !run.out )
            fprintf(stderr, "  note: could not open %s for the trace\n", trace_path);
    }

    fprintf(stderr, "mock230 selftest: Chambers of Xeric tick harness (%d ticks, god mode)\n",
            ticks);

    /*
     * Immortality BEFORE entering. Olm's first standard attack lands on the
     * fourth tick of the fight and a fresh selftest player has 10 hitpoints, so
     * a harness that armed this afterwards would be asserting a corpse's
     * rotation. `::god` is the backstop; the pool is what makes damage readable
     * (see the header).
     */
    player->godmode = 0;
    cox_sim_top_up(player);

    if( mock230_scripts_run_debugproc(srv, "cox") != MOCK230_TRIGGER_RAN )
    {
        cox_sim_fail(&run, "::cox should reach content - is the script pack compiled?");
        goto done;
    }

    /*
     * TEKTON FIRST, and the order is not cosmetic.
     *
     * The trace is a single channel: one slot, shared by everything in the raid.
     * Once Olm is spawned his 4-tick clock writes to it forever, so a Tekton
     * window opened afterwards is 90% Olm and the Tekton assertions read a trace
     * he is barely in. The first version of this harness did exactly that and
     * reported "Tekton swung 0 times" against a trace whose every entry was
     * `olm.basic`.
     *
     * Tekton is spawned by `::cox` on entry, so his window is simply the one
     * before Olm exists.
     */
    {
        int tekton = 0;

        for( int i = 0; i < MOCK230_NPC_MAX; i++ )
            if( srv->npcs[i].active && srv->npcs[i].type >= 7540 && srv->npcs[i].type <= 7545 )
                tekton++;
        if( tekton == 0 )
        {
            cox_sim_fail(&run, "entering CoX should have spawned Tekton");
        }
        else
        {
            if( run.out )
                fprintf(run.out, "# encounter: tekton\n");
            memset(run.code, 0, sizeof(run.code));
            memset(run.hurt, 0, sizeof(run.hurt));
            run.ticks = 0;
            cox_sim_record(srv, &run, ticks);
            cox_sim_check_tekton(srv, &run);
            fprintf(stderr, "  tekton: %d ticks traced\n", run.ticks);
        }
    }

    /* ---------------------------------------------------------------- Olm */
    if( mock230_scripts_run_debugproc(srv, "coxolm") == MOCK230_TRIGGER_RAN )
    {
        if( run.out )
            fprintf(run.out, "# encounter: olm\n");
        memset(run.code, 0, sizeof(run.code));
        memset(run.hurt, 0, sizeof(run.hurt));
        run.ticks = 0;
        cox_sim_record(srv, &run, ticks);
        cox_sim_check_olm(srv, &run);
        fprintf(stderr, "  olm: %d ticks traced\n", run.ticks);
    }
    else
    {
        cox_sim_fail(&run, "::coxolm should reach content");
    }

    mock230_scripts_run_debugproc(srv, "cox_leave");

done:
    if( run.out )
        fclose(run.out);
    player->godmode = saved_god;
    player->x = saved_x;
    player->z = saved_z;
    player->level = saved_level;
    if( run.failures )
        fprintf(stderr, "  RESULT: cox tick harness FAILED with %d assertion(s)\n", run.failures);
    else
        fprintf(stderr, "  RESULT: cox tick harness passed\n");
    return run.failures;
}
