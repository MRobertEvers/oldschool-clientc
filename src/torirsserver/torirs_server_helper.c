#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_ids.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * The helper panel -- interface `helper_generic` (711).
 *
 * Three All Settings > Activities rows hang off this one frame, and all three
 * were unreachable for the same reason the enemy health overlay next door was:
 * the display is finished in the cache and nothing opens it.
 *
 *   163  Agility helper                  %agility_helper_disabled
 *   184  Slayer helper                   %slayer_helper_disabled
 *   275  Clue scroll helper - Infobox    %option_cluehelper_infobox_enabled
 *
 * A fourth row of the same family, 268 (Blast Furnace helper), is deliberately
 * not here. It is a different interface (`blast_furnace_hud`, 474) which builds
 * itself from its own onload, so the whole of that row is "mount it while the
 * player is at the Blast Furnace" -- and where the player is standing is
 * something the minigame's own lane already tracks, tick by tick, in
 * `[softtimer,bf_state_tick]`. Putting it here would mean teaching C the
 * furnace's zone corners, which is the wrong seam by a wide margin.
 *
 * ---- who builds what ----
 *
 * Nothing here draws a row. Each helper is a clientscript with no callers
 * anywhere in the cache -- the signature of an entry point the server is meant
 * to call, the same one the respawn timers (5471/5475/5478) carry:
 *
 *   5170  Agility.  Runs 5171 (zero the lap varcs, seed the target level) then
 *         5182, which names the course out of `enum_3507` keyed on
 *         `%helper_agility_current_course` and arms three or four row updaters
 *         on `var3153` -- the very varp that varbit sits in, so the rows keep
 *         themselves current afterwards without any further help from us.
 *   5317  Slayer.  Runs 5318, which builds Task / Amount / Area / Streak and
 *         re-arms on `var394, var395, var2096, var1077, var1565, var661`.
 *   6631  Clue.    Takes the step's dbrow, parks it in `%var3546`, and 6633
 *         switches on `db_getrowtable` to one of 6634..6644, one per clue kind.
 *
 * So there is no per-course, per-task or per-clue table in this file, and there
 * must never be one: finding yourself writing one means the wrong seam.
 *
 * ---- where it mounts, and it is not the obvious component ----
 *
 * `<gameframe>:helper_content`, not `<gameframe>:helper`. The slot is three
 * layers deep -- `helper` holds `helper_dodger` holds `helper_content` -- and
 * only the innermost is childless, which an if_opensub target has to be. It is
 * also the one the cache addresses by hand: clientscript 4704 resizes
 * `interface_161:12` to whatever the built rows measure, and 4731/4732 ask
 * `if_hassub` of that same component to decide whether the panel is on screen
 * and what has to dodge around it. Mounted one layer out, the panel would exist
 * and never be laid out.
 *
 * ---- the open and the build go out on the SAME tick ----
 *
 * Unlike `hpbar_hud`, which paints from an `if_setonvartransmit` hook and so
 * has to be fed a tick after it is mounted, this panel paints from the
 * RUNCLIENTSCRIPT below. That makes the ordering ours to state rather than
 * something to leave a gap for, and the client honours it: IF_OPENSUB runs on
 * the serial exec pipeline, so the mount is complete before the next packet is
 * popped, while a RUNCLIENTSCRIPT is held until the tick fence -- which is
 * after every packet of the tick has run. Open then build, one tick, no race.
 *
 * ---- the settings are INVERTED, and the cache says which ----
 *
 * Two of the three are `..._disabled` and one is `..._enabled`. That is the
 * sense stated rather than inferred, and reading an inverted row the plain way
 * shows the helper to exactly the players who switched it off -- the failure
 * this whole category of rows is full of.
 *
 * Each row's switch is checked inside its OWN arm below rather than once at the
 * end, so that a player who has turned the clue infobox off still gets the
 * Agility helper. "Not that one" is not "not any".
 */

/** The cache's entry points. Named here because they are the only numbers in
 *  this file that are not resolved through the content pack: a clientscript id
 *  has no symbol table to look it up in. */
#define TORIRSSERVER_SCRIPT_HELPER_AGILITY 5170
#define TORIRSSERVER_SCRIPT_HELPER_SLAYER 5317
#define TORIRSSERVER_SCRIPT_HELPER_CLUE 6631

/**
 * `%current_helper` as clientscript 4697 reads it.
 *
 * 4697 is the panel's "Reset" op -- 4695 arms op 6 with it -- and it switches
 * on this varbit. Exactly one of its cases is a helper: `case 2` re-runs 5171
 * and 5182, which is the Agility helper rebuilding itself. Nothing in the cache
 * WRITES the varbit, so the value is the server's to publish, and 2 is the only
 * one it can honestly publish: an id invented for the Slayer or clue helper
 * would fall through 4697's switch and make Reset a silent no-op, which is what
 * 0 already does and says so.
 */
#define TORIRSSERVER_CACHE_HELPER_AGILITY 2

/** Read one varp of this player, tolerating an id the cache does not have.
 *
 *  `-1` is not a legal varp and the ids table leaves an unresolved symbol at
 *  it, so a cache that ships without one of these names reads as "no task, no
 *  course, no clue" and the panel simply never opens -- rather than indexing
 *  off the front of the array. */
static int
helper_varp(
    struct ToriRSServerPlayer const* player,
    int varp)
{
    assert(player);
    if( varp < 0 || varp >= TORIRSSERVER_VARP_COUNT )
        return 0;
    return player->varps[varp];
}

int
ToriRSServer_HelperWantedFor(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int* out_arg)
{
    struct ToriRSServerIds const* ids = ToriRSServer_Ids();

    assert(srv);
    assert(player);
    assert(out_arg);

    *out_arg = 0;

    /*
     * Ordered by how momentary each activity is, most momentary first.
     *
     * One frame, three claimants, so the order is a real decision: a state that
     * lasts all session and one that lasts a lap cannot share, and if the
     * long-lived one wins the short-lived one is never seen at all. A Slayer
     * task outlives everything here, so it sits at the bottom; a clue STEP is
     * the single most specific thing a player can be in the middle of, so it
     * sits at the top.
     */

    /*
     * The clue step, and it is the only helper that takes an argument.
     *
     * `%cluehelper_infobox_clue` is the cache's own name for "which clue row
     * the infobox is about", and content is what puts a row in it -- the trail
     * lane already derives the row from the scroll in the backpack
     * (`~trail_row_of_obj`), which is knowledge that has no business being
     * duplicated here.
     */
    if( ids->varp_cluehelper_infobox_clue >= 0 )
    {
        int const row = helper_varp(player, ids->varp_cluehelper_infobox_clue);

        /* This row is the ONE stated the other way round: `..._enabled`, so
         * the plain reading is the right one. */
        if( row > 0 && ToriRSServer_VarbitGet(player, ids->varbit_cluehelper_infobox_on) )
        {
            *out_arg = row;
            return TORIRSSERVER_HELPER_CLUE;
        }
    }

    /*
     * The Agility course.
     *
     * Non-zero is both "which course" and "a course at all", which is not a
     * shortcut: 5182 reads the same varbit for the panel's title and
     * `enum_3507` has no entry 0, so a course the helper could not name is a
     * course the helper cannot show. The lifetime is content's -- the lane
     * arms the varbit as each obstacle is cleared and a softtimer clears it
     * when the player stops -- because "am I still on this course" is a
     * question about obstacles, not about anything visible from here.
     */
    if( ids->varbit_helper_agility_course >= 0 &&
        ToriRSServer_VarbitGet(player, ids->varbit_helper_agility_course) > 0 &&
        !ToriRSServer_VarbitGet(player, ids->varbit_agility_helper_off) )
        return TORIRSSERVER_HELPER_AGILITY;

    /*
     * The Slayer task -- while it is being worked on, not merely while it is
     * held.
     *
     * `%slayer_count` alone would pin the panel open from the moment a master
     * assigns until the last kill, days later, and by the ordering above that
     * would mean nobody ever saw the other two. The extra condition is combat,
     * which is what a task IS: the panel tells you what you are killing and how
     * many are left while you are killing them.
     *
     * Not "combat with the task's own creature", deliberately. That test lives
     * in `skill_slayer/scripts/slayer_kill.rs2`, behind a dbtable lookup, and
     * re-deriving it here would give the server two answers to one question --
     * the failure `varp-two-writers` is named after. A player who breaks off to
     * kill something else keeps the panel for as long as the fight lasts, which
     * is the cheap side of that trade.
     */
    if( ids->varp_slayer_count >= 0 && helper_varp(player, ids->varp_slayer_count) != 0 &&
        player->combat_target >= 0 &&
        !ToriRSServer_VarbitGet(player, ids->varbit_slayer_helper_off) )
        return TORIRSSERVER_HELPER_SLAYER;

    return TORIRSSERVER_HELPER_NONE;
}

/** The clientscript that builds one helper's rows, or -1. */
static int
helper_builder_script(int helper)
{
    switch( helper )
    {
    case TORIRSSERVER_HELPER_CLUE:
        return TORIRSSERVER_SCRIPT_HELPER_CLUE;
    case TORIRSSERVER_HELPER_AGILITY:
        return TORIRSSERVER_SCRIPT_HELPER_AGILITY;
    case TORIRSSERVER_HELPER_SLAYER:
        return TORIRSSERVER_SCRIPT_HELPER_SLAYER;
    default:
        return -1;
    }
}

/** For a debug line and nothing else. */
static char const*
helper_name(int helper)
{
    switch( helper )
    {
    case TORIRSSERVER_HELPER_CLUE:
        return "clue";
    case TORIRSSERVER_HELPER_AGILITY:
        return "agility";
    case TORIRSSERVER_HELPER_SLAYER:
        return "slayer";
    default:
        return "none";
    }
}

void
ToriRSServer_HelperTick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    struct ToriRSServerIds const* ids = ToriRSServer_Ids();
    int const debug = getenv("TORIRS_HELPER_DEBUG") != NULL;
    int want;
    int arg = 0;
    int script;

    assert(srv);
    assert(player);

    if( ids->iface_helper_generic <= 0 )
        return;

    want = ToriRSServer_HelperWantedFor(srv, player, &arg);

    /*
     * Steady state, and it has to compare the ARGUMENT too.
     *
     * The builders draw once, from the RUNCLIENTSCRIPT, and only the Agility
     * and Slayer ones re-arm themselves on a var afterwards. The clue helper
     * does not: 6633 lays out one clue step and stops. So a player who finishes
     * a step and starts the next would keep the old step's rows forever unless
     * a changed row counts as a change -- which is the whole reason
     * `helper_arg` is remembered beside `helper_open`.
     */
    if( want == player->helper_open && arg == player->helper_arg )
        return;

    if( want == TORIRSSERVER_HELPER_NONE )
    {
        player->helper_open = TORIRSSERVER_HELPER_NONE;
        player->helper_arg = 0;
        if( debug )
            fprintf(stderr, "helper: close\n");
        /* Back to "no helper is showing" before the panel goes, so a Reset op
         * that races the close finds nothing to rebuild rather than rebuilding
         * into a slot that is being emptied. */
        if( ids->varbit_current_helper >= 0 )
            ToriRSServer_VarbitSetOn(srv, player, ids->varbit_current_helper, 0);
        ToriRSServer_SendIfClosesub(player, ToriRSServer_PlayerHelper(player));
        return;
    }

    if( player->helper_open == TORIRSSERVER_HELPER_NONE )
    {
        int const slot = ToriRSServer_PlayerHelper(player);

        if( debug )
            fprintf(stderr,
                    "helper: open iface %d into %d:%d (live gameframe %d) for %s\n",
                    ids->iface_helper_generic,
                    TORIRSSERVER_COM_GROUP(slot),
                    TORIRSSERVER_COM_CHILD(slot),
                    ToriRSServer_PlayerGameframeIface(player),
                    helper_name(want));
        ToriRSServer_SendIfOpensub(
            player,
            TORIRSSERVER_COM_GROUP(slot),
            TORIRSSERVER_COM_CHILD(slot),
            ids->iface_helper_generic,
            1);
    }

    /*
     * Which helper is showing, for the panel's own Reset op. See
     * TORIRSSERVER_CACHE_HELPER_AGILITY: 2 is the only value 4697 acts on, and
     * every other helper honestly has none.
     *
     * Written before the builder rather than after, because 5182 is what arms
     * the row updaters and a Reset that landed between the two would rebuild
     * from a varbit that still said "nothing".
     */
    if( ids->varbit_current_helper >= 0 )
        ToriRSServer_VarbitSetOn(
            srv,
            player,
            ids->varbit_current_helper,
            want == TORIRSSERVER_HELPER_AGILITY ? TORIRSSERVER_CACHE_HELPER_AGILITY : 0);

    script = helper_builder_script(want);
    assert(script > 0);
    if( debug )
        fprintf(stderr, "helper: build %s via clientscript %d (arg %d)\n",
                helper_name(want), script, arg);
    if( want == TORIRSSERVER_HELPER_CLUE )
        ToriRSServer_SendRunClientscript(player, script, &arg, 1);
    else
        ToriRSServer_SendRunClientscript(player, script, NULL, 0);

    player->helper_open = want;
    player->helper_arg = arg;
}
