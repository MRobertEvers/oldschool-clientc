/*
 * Script container binding and the host command seam.
 *
 * This is the only file that knows both `struct Mock230Server` and the VM. The
 * VM stays engine-agnostic — it holds active entities as void* and never
 * dereferences them — so everything that touches game state funnels through
 * mock230_script_command below.
 *
 * The dispatch contract, which this header used to state the other way round:
 * **a trigger with no script does nothing.** Resolution is the reference's
 * (`ScriptProvider.getByTrigger`) — exact type, then category, then the bare `_`
 * wildcard, and nothing after that — and that wildcard is the only fallback the
 * design has.
 *
 * What survives of the C that used to answer a missed trigger is enumerated in
 * `enum Mock230Fallback` and gated on `mock230_scripts_fallback`, which refuses
 * the two cases `if( !run_trigger(...) )` could not tell apart: a script that
 * *aborted* (a bug in content, not a gap in it) and a server with no script pack
 * at all (in which nothing is a gap, because everything is). The old promise —
 * "a missing script leaves every call site doing exactly what it did before
 * scripts existed" — was right while scripts were an experiment and is a second,
 * silently disagreeing implementation of the game now that they are not.
 */

#include "mock230.h"

#include "mock230_container.h"
#include "mock230_content.h"
#include "mock230_ids.h"
#include "mock230_scene.h"
#include "mock230_session.h"

#include "ss_meta.h"
#include "ss_opcode.h"
#include "ssvm.h"
#include "ssvm_provider.h"

/* Derived from the `case SS_OP_*:` labels in this file and in the VM core.
 * See gen_opcode_coverage.py for why it is generated. */
#include "mock230_opcode_coverage.gen.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Coordinates                                                         */
/* ------------------------------------------------------------------ */

/*
 * RuneScript packs a coord into one int as
 * (level << 28) | ((mx * 64 + lx) << 14) | (mz * 64 + lz).
 *
 * The compiler emits coord literals this way, so the host has to read them the
 * same way or `p_teleport(0_50_50_0_0)` lands somewhere unrelated. Both halves
 * of that agreement live in this repo, which is exactly why it is worth
 * spelling out in one place.
 */

static int32_t
coord_pack(int level, int x, int z)
{
    return (int32_t)(((uint32_t)level << 28) | ((uint32_t)x << 14) | (uint32_t)z);
}

static int
coord_level(int32_t coord)
{
    return (int)(((uint32_t)coord >> 28) & 0x3);
}

static int
coord_x(int32_t coord)
{
    return (int)(((uint32_t)coord >> 14) & 0x3fff);
}

static int
coord_z(int32_t coord)
{
    return (int)((uint32_t)coord & 0x3fff);
}

/* ------------------------------------------------------------------ */
/* Container                                                           */
/* ------------------------------------------------------------------ */

int
mock230_scripts_load(
    struct Mock230Server* srv,
    const char* dir)
{
    struct SSVM_Error err;

    /*
     * Once per world, however many players log in.
     *
     * Both hosts call this from their login path, and reloading on a second
     * login frees the env out from under the *first* player's parked script —
     * a use-after-free whose symptom is a crash several ticks later, in
     * whichever conversation happened to be open. Reloading content while
     * somebody is logged in is not a thing this server supports; the honest
     * spelling of that is to refuse rather than to corrupt.
     */
    if( srv->scripts_ok )
        return 1;

    mock230_scripts_free(srv);

    srv->scripts = (struct SSVM_Provider*)calloc(1, sizeof(struct SSVM_Provider));
    srv->script_env = (struct SSVM_Env*)calloc(1, sizeof(struct SSVM_Env));
    if( !srv->scripts || !srv->script_env )
    {
        mock230_scripts_free(srv);
        return 0;
    }

    SSVM_ErrorClear(&err);
    if( !SSVM_ProviderLoadDir(srv->scripts, dir, &err) )
    {
        /*
         * A banner, not a line, because of what it now means.
         *
         * This used to be a one-line warning about a supported mode: no pack,
         * every trigger falls through to C, the mock still plays. That mode is
         * gone — `mock230_scripts_fallback` refuses every engine fallback when
         * `scripts_ok` is 0 — so a server that reaches here does almost nothing
         * at all. Which is the point: the alternative was a second game running
         * silently beside the one the content tree describes, discoverable only
         * by finding a behaviour where the two disagreed.
         */
        fprintf(stderr,
                "mock230: ============================================================\n"
                "mock230: NO SCRIPT PACK at %s\n"
                "mock230:   %s\n"
                "mock230: The game's behaviour is content, not C. Without the pack the\n"
                "mock230: engine's fallbacks stay OFF and almost nothing will work —\n"
                "mock230: no interactions, no buttons, no dialogue, no drops.\n"
                "mock230: Build it:  make -C src mock230-scripts\n"
                "mock230: ============================================================\n",
                dir, err.message);
        mock230_scripts_free(srv);
        return 0;
    }

    SSVM_EnvInit(srv->script_env, srv->scripts);
    SSVM_EnvBindHost(srv->script_env, srv, mock230_script_command);
    /* Fixed seed so a session replays identically, which every deterministic
     * test downstream depends on. */
    SSVM_EnvSeed(srv->script_env, 0x5eed1234u);

    srv->scripts_ok = 1;
    fprintf(stderr, "mock230: %d scripts loaded from %s\n", srv->scripts->loaded, dir);
    /* Before anything runs: an opcode this tree needs and the engine lacks is a
     * fact about the tree, not about whichever player eventually triggers it. */
    mock230_scripts_report_gaps(srv);
    /* And the second list of the same kind: which behaviours are still answered
     * from C when content binds nothing. It shrinks; that is the schedule. */
    mock230_scripts_report_fallbacks(srv);
    /* And the third: the fallbacks above are C standing in for content that
     * has not arrived. This is the opposite — content that arrived and took a
     * verb the engine still answers. See mock230_scripts_report_shadowed_ops. */
    mock230_scripts_report_shadowed_ops(srv);
    return srv->scripts->loaded;
}

/* ------------------------------------------------------------------ */
/* Coverage                                                            */
/* ------------------------------------------------------------------ */

/** Does anything — the VM core or the host seam — implement this opcode? */
static int
opcode_implemented(int opcode)
{
    int lo = 0;
    int hi = MOCK230_OPCODE_COVERAGE_COUNT - 1;

    while( lo <= hi )
    {
        int mid = lo + ((hi - lo) / 2);
        int value = (int)MOCK230_OPCODE_COVERAGE[mid];

        if( value == opcode )
            return 1;
        if( value < opcode )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/**
 * Report every opcode the loaded content uses that nothing implements.
 *
 * At **load** time, not at call time, and that is the whole value. The VM
 * already complains when an unimplemented opcode is reached — but only if a
 * player happens to trigger that script, which for content behind a quest step
 * or a rare drop may be never. Answering the question up front turns "the
 * server has 155 of 396 opcodes" into the far more useful "this content tree
 * needs these eleven, here is the first script that wants each".
 *
 * That list is the work queue for moving the remaining C behaviour into
 * content: an opcode nothing asks for is not worth implementing, and one that
 * three scripts ask for is blocking three scripts.
 *
 * Returns the number of distinct missing opcodes.
 */
int
mock230_scripts_report_gaps(struct Mock230Server* srv)
{
    /* One bit per opcode, so each is reported once however many scripts want
     * it. opcode values run to 10003 (they are sparse, not dense), so this is
     * sized by the generated value limit rather than by the opcode count. */
    static uint8_t seen[MOCK230_OPCODE_VALUE_LIMIT];
    /* Static, not automatic: 10,004 pointers is 80 KB, which is most of a
     * default thread stack. */
    static const char* first_user[MOCK230_OPCODE_VALUE_LIMIT];
    int missing = 0;

    if( !srv->scripts_ok || !srv->scripts )
        return 0;

    memset(seen, 0, sizeof(seen));
    memset(first_user, 0, sizeof(first_user));

    for( int i = 0; i < srv->scripts->count; i++ )
    {
        const struct SSVM_Script* script = &srv->scripts->scripts[i];

        /* Absent slots are zeroed; op_count marks them. */
        if( script->op_count <= 0 || !script->opcodes )
            continue;

        for( int op = 0; op < script->op_count; op++ )
        {
            int opcode = (int)script->opcodes[op];

            if( opcode < 0 || opcode >= MOCK230_OPCODE_VALUE_LIMIT )
                continue;
            if( seen[opcode] || opcode_implemented(opcode) )
                continue;
            seen[opcode] = 1;
            first_user[opcode] = script->name ? script->name : "?";
            missing++;
        }
    }

    if( missing == 0 )
        return 0;

    fprintf(stderr, "mock230: %d opcode(s) this content uses are not implemented:\n",
            missing);
    for( int opcode = 0; opcode < MOCK230_OPCODE_VALUE_LIMIT; opcode++ )
    {
        if( !seen[opcode] )
            continue;
        fprintf(stderr, "  %-28s first wanted by %s\n", SSVM_OpcodeName(opcode),
                first_user[opcode]);
    }
    return missing;
}

void
mock230_scripts_free(struct Mock230Server* srv)
{
    /*
     * The parked script points into the env that is about to be freed.
     *
     * Nothing noticed while every trigger ran to completion; a conversation
     * that blocks on p_pausebutton is the first content that leaves a state
     * parked across a reload, and the next tick would then walk a freed
     * pointer. The resume buttons go with it — they only mean anything to the
     * script that registered them.
     *
     * Every player's, not the active one's: this runs at load and at shutdown,
     * when there is no "whose turn it is" at all, and with a pool a second
     * player's parked state would outlive the env it points into.
     */
    for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
    {
        srv->players[i].active_script = NULL;
        srv->players[i].resume_button_count = 0;
    }

    if( srv->script_env )
    {
        SSVM_EnvFree(srv->script_env);
        free(srv->script_env);
        srv->script_env = NULL;
    }
    if( srv->scripts )
    {
        SSVM_ProviderFree(srv->scripts);
        free(srv->scripts);
        srv->scripts = NULL;
    }
    srv->scripts_ok = 0;
}

/* Release a state and clear whichever slot was holding it. */
static void
release_parked(struct Mock230Server* srv, struct SSVM_State* state)
{
    /* Searched rather than addressed: a state can be parked on any player, and
     * the one whose turn it is now is not necessarily the one it belongs to —
     * a world-queued script resumes in phase 1, before anybody's turn. */
    for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
    {
        if( srv->players[i].active_script == state )
            srv->players[i].active_script = NULL;
    }
    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active_script == state )
            srv->npcs[i].active_script = NULL;
    }
    SSVM_StateRelease(state);
}

/*
 * `Player.canAccess()` — may the engine hand this player a script right now?
 *
 * The reference is `!this.protect && !(this.delayed || containsModalInterface())`.
 * `protect` has no equivalent here: this engine's one-parked-script rule
 * (`run_or_park`) stands in for it, which is a stated divergence rather than an
 * omission — see osrs230_mockserver.md §3.19.
 *
 * This is what makes a queue a queue. Without it a `[queue]` armed by a dialogue
 * ran *through* the dialogue, and a normal timer fired while the player was
 * `p_delay`ed.
 */
static int
player_can_access(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;

    if( srv->tick < player->delayed_until )
        return 0;
    if( player->mainmodal_group > 0 || player->chatmodal_group > 0 )
        return 0;
    return 1;
}

/**
 * Run a state, and park it wherever its suspend status says it belongs.
 *
 * Returns 1 when a script ran (finished or parked), 0 when it aborted — the
 * caller treats 0 as "nothing happened" and falls back to its C behaviour.
 */
static int
run_or_park(struct Mock230Server* srv, struct SSVM_State* state)
{
    int was_parked = srv->active_player->active_script == state;
    enum SSVM_Exec status = SSVM_Execute(state);

    /*
     * A parked script that finishes closes the chatbox behind it.
     *
     * `Player.executeScript`: when the state that just ended *was* the player's
     * active one, it clears the resume buttons and — if no MAIN modal is up —
     * calls `closeModal(false)`. This engine never did, which was invisible
     * until `canAccess()` existed and then fatal: a conversation that ended on a
     * `~mesbox` left the chat interface mounted, so the player stayed busy and
     * every later queue entry and normal timer was held forever.
     *
     * `false` is the whole reason `close_modal_ex` exists — a script may
     * `weakqueue` on its last line, and the automatic close must not discard it.
     */
    if( was_parked && (status == SSVM_FINISHED || status == SSVM_ABORTED) )
    {
        struct Mock230Player* owner = srv->active_player;

        if( status == SSVM_ABORTED )
        {
            fprintf(stderr, "mock230: %s", SSVM_Backtrace(state));
            fprintf(
                stderr,
                "mock230: abort context host_tag=%d pointers=0x%x active_npc=%d\n",
                (int)state->host_tag,
                (unsigned)state->pointers,
                (state->pointers & SSVM_PTR_ACTIVE_NPC) != 0);
        }
        release_parked(srv, state);
        owner->resume_button_count = 0;
        if( owner->mainmodal_group <= 0 )
            mock230_world_close_modal_ex(srv, 0);
        return status == SSVM_FINISHED;
    }

    switch( status )
    {
    case SSVM_FINISHED:
        release_parked(srv, state);
        return 1;

    case SSVM_ABORTED:
        fprintf(stderr, "mock230: %s", SSVM_Backtrace(state));
        fprintf(
            stderr,
            "mock230: abort context host_tag=%d pointers=0x%x active_npc=%d\n",
            (int)state->host_tag,
            (unsigned)state->pointers,
            (state->pointers & SSVM_PTR_ACTIVE_NPC) != 0);
        release_parked(srv, state);
        return 0;

    case SSVM_SUSPENDED:
    case SSVM_PAUSEBUTTON:
    case SSVM_COUNTDIALOG:
        /* One parked script per player. A second would need somewhere to live
         * and, more importantly, would let two scripts interleave writes to the
         * same player — the reference has the same single slot. */
        if( srv->active_player->active_script && srv->active_player->active_script != state )
        {
            fprintf(stderr,
                    "mock230: dropping %s, which suspended while %s waits\n",
                    state->script ? state->script->name : "?",
                    srv->active_player->active_script->script
                        ? srv->active_player->active_script->script->name
                        : "?");
            SSVM_StateRelease(state);
            return 0;
        }
        srv->active_player->active_script = state;
        return 1;

    case SSVM_NPC_SUSPENDED:
    {
        int slot = (int)state->host_tag - 1;

        if( slot < 0 || slot >= MOCK230_NPC_MAX )
        {
            fprintf(stderr, "mock230: npc_delay with no active npc\n");
            SSVM_StateRelease(state);
            return 0;
        }
        if( srv->npcs[slot].active_script && srv->npcs[slot].active_script != state )
        {
            fprintf(stderr, "mock230: npc %d already has a parked script\n", slot);
            SSVM_StateRelease(state);
            return 0;
        }
        srv->npcs[slot].active_script = state;
        return 1;
    }

    case SSVM_WORLD_SUSPENDED:
    {
        int32_t delay = 0;

        /* world_delay leaves its argument on the stack for the parking code to
         * take, which is what the reference does. Popping it in the command
         * would work equally well, but matching keeps content portable. */
        SSVM_PopInt(state, &delay);
        for( int i = 0; i < MOCK230_WORLD_QUEUE_MAX; i++ )
        {
            if( srv->world_queue[i].active )
                continue;
            srv->world_queue[i].active = 1;
            srv->world_queue[i].state = state;
            srv->world_queue[i].delay = delay + 1;
            return 1;
        }
        fprintf(stderr, "mock230: world queue full, dropping a script\n");
        SSVM_StateRelease(state);
        return 0;
    }

    default:
        SSVM_StateRelease(state);
        return 0;
    }
}

/**
 * Start a script by id, with an optional argument, on behalf of the player.
 *
 * `protect` is `Player.executeScript`'s second argument, and it is not decorative:
 * a SOFT timer runs *without* protected access precisely because it is allowed to
 * run while the player is busy, and giving it the pointer would let it do things a
 * busy player must not be doing. Everything else the engine starts is protected.
 */
static int
run_script_id(
    struct Mock230Server* srv,
    int script_id,
    int32_t arg,
    int has_arg,
    int npc_slot,
    int protect)
{
    const struct SSVM_Script* script = SSVM_ProviderGet(srv->scripts, script_id);
    struct SSVM_State* state;

    if( !script )
        return 0;

    /* `queue` always carries an argument, but a queued script only declares one
     * if it uses it. Bind what the script asked for rather than what the caller
     * happened to have — the reference is equally forgiving, since its
     * setupNewScript pops exactly int_arg_count and leaves any surplus alone. */
    if( script->int_arg_count == 0 )
        has_arg = 0;
    else if( script->int_arg_count > 1 || script->string_arg_count > 0 )
    {
        fprintf(stderr, "mock230: %s declares arguments the engine cannot supply\n",
                script->name);
        return 0;
    }
    else
        has_arg = 1;

    state = SSVM_StateAlloc(srv->script_env, script, has_arg ? &arg : NULL,
                            has_arg ? 1 : 0, NULL, 0);
    if( !state )
        return 0;

    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    if( protect )
        SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot >= 0 && npc_slot < MOCK230_NPC_MAX && srv->npcs[npc_slot].active )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }
    return run_or_park(srv, state);
}

/* ------------------------------------------------------------------ */
/* Resuming                                                            */
/* ------------------------------------------------------------------ */

/*
 * Re-arm the VM's ACTIVE_NPC pointer bit from the slot parked in `host_tag`.
 *
 * LostCity keeps `_activeNpc` on the same ScriptState across p_pausebutton /
 * p_delay; resume just re-executes that state. Here the durable identity is the
 * slot in `host_tag` (a raw pointer would dangle if the npc despawned), and the
 * bit the VM's require table checks is separate. Without this, a conversation
 * that parks on ~chatplayer after a ~p_choice and then resumes into ~chatnpc
 * aborts on NPC_TYPE even though the slot is still valid — the bit was set at
 * trigger entry and is not guaranteed to still be set when the state wakes.
 */
static void
rebind_active_npc(
    struct Mock230Server* srv,
    struct SSVM_State* state)
{
    int slot = (int)state->host_tag - 1;

    if( slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    if( !srv->npcs[slot].active )
        return;
    SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
}

void
mock230_scripts_resume_player(struct Mock230Server* srv)
{
    struct SSVM_State* state = srv->active_player->active_script;

    if( !state || !srv->scripts_ok )
        return;
    /* A resume-button or count-dialog wait is released by client input, not by
     * the clock, so the tick must leave those alone. */
    if( state->execution != SSVM_SUSPENDED )
        return;
    if( srv->tick < srv->active_player->delayed_until )
        return;

    rebind_active_npc(srv, state);
    run_or_park(srv, state);
}

void
mock230_scripts_resume_npc(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc;

    if( !srv->scripts_ok || slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active_script || srv->tick < npc->delayed_until )
        return;
    run_or_park(srv, npc->active_script);
}

void
mock230_scripts_resume_world(struct Mock230Server* srv)
{
    if( !srv->scripts_ok )
        return;

    for( int i = 0; i < MOCK230_WORLD_QUEUE_MAX; i++ )
    {
        struct SSVM_State* state;

        if( !srv->world_queue[i].active )
            continue;
        if( --srv->world_queue[i].delay > 0 )
            continue;

        /* Unlink before running: the script may world_delay again, and it has
         * to be able to claim a free slot — possibly this one. */
        state = srv->world_queue[i].state;
        srv->world_queue[i].active = 0;
        srv->world_queue[i].state = NULL;
        run_or_park(srv, state);
    }
}

/** One pass of the queue, over the entries of one kind-set. */
static void
drain_queue(
    struct Mock230Server* srv,
    int weak)
{
    for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
    {
        struct Mock230Queued* entry = &srv->active_player->queue[i];
        int script_id;
        int32_t arg;

        if( !entry->active )
            continue;
        if( (entry->kind == MOCK230_QUEUE_WEAK) != (weak != 0) )
            continue;

        /*
         * Decrement unconditionally and gate only the *run*, which is what
         * `Player.processQueue` does: `const delay = request.delay--; if
         * (this.canAccess() && delay <= 0)`. The difference is visible — an
         * entry that came due while the player was busy fires on the first tick
         * after access returns, rather than restarting its wait.
         */
        entry->delay--;
        if( entry->delay > 0 )
            continue;
        if( !player_can_access(srv) )
            continue;

        script_id = entry->script_id;
        arg = entry->arg;
        entry->active = 0;
        run_script_id(srv, script_id, arg, 1, -1, 1);
    }
}

void
mock230_scripts_clear_weak_queue(struct Mock230Player* player)
{
    for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
    {
        if( player->queue[i].active && player->queue[i].kind == MOCK230_QUEUE_WEAK )
            player->queue[i].active = 0;
    }
}

void
mock230_scripts_process_queues(struct Mock230Server* srv)
{
    if( !srv->scripts_ok )
        return;

    /*
     * A STRONG entry anywhere in the queue closes whatever modal is up *before*
     * the drain, so that its own entry passes the access check on the tick it is
     * due. That is the kind's entire difference (`Player.processQueues`), and it
     * is why the scan runs even when the strong entry is not yet due.
     */
    for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
    {
        if( srv->active_player->queue[i].active &&
            srv->active_player->queue[i].kind == MOCK230_QUEUE_STRONG )
        {
            mock230_world_close_modal(srv);
            break;
        }
    }

    /* Primary queue, then the weak one — `processQueue()` then
     * `processWeakQueue()`, which is the reference's order and matters when a
     * primary entry closes a modal the weak entries were waiting behind. */
    drain_queue(srv, 0);
    drain_queue(srv, 1);
}

void
mock230_scripts_process_timers(struct Mock230Server* srv)
{
    /*
     * Two passes, NORMAL then SOFT, as `World.processPlayers` calls it.
     *
     * The two differ in both directions and one `case` used to serve both:
     * a SOFT timer runs while the player is busy and *without* protected access;
     * a NORMAL one needs `canAccess()` and runs *with* it. A test that only asked
     * "did a timer fire" would stay green for either half being wrong.
     */
    static const int k_order[2] = { MOCK230_TIMER_NORMAL, MOCK230_TIMER_SOFT };

    if( !srv->scripts_ok )
        return;

    for( int pass = 0; pass < 2; pass++ )
    {
        int type = k_order[pass];

        for( int i = 0; i < MOCK230_TIMER_MAX; i++ )
        {
            struct Mock230Timer* timer = &srv->active_player->timers[i];

            /*
             * No lower bound on the interval, and that is the reference's shape,
             * not an omission: `Player.processTimers` tests only
             * `World.currentTick >= timer.clock + timer.interval`, so an
             * interval of 0 is a timer that fires on *every* tick rather than a
             * stopped one. `cleartimer` is the only thing that stops a timer.
             *
             * It is reachable from real content, not a theoretical case: the
             * reference arms `settimer(agilityarena_pillar,
             * sub(%agilityarena_next_pillar_time, map_clock))`, which is 0 or
             * negative the moment that deadline has passed. An `interval <= 0`
             * guard here left such a timer permanently inert while still holding
             * its slot and still answering `gettimer` — set, and never firing.
             */
            if( !timer->active || timer->type != type )
                continue;
            /* Absolute, not a countdown: `clock` is the tick it was armed or
             * last fired at, which is also what `gettimer` returns. */
            if( srv->tick < timer->clock + timer->interval )
                continue;
            if( type == MOCK230_TIMER_NORMAL && !player_can_access(srv) )
                continue;
            timer->clock = srv->tick;
            run_script_id(srv, timer->script_id, 0, 0, -1, type == MOCK230_TIMER_NORMAL);
        }
    }
}

void
mock230_scripts_process_walktrigger(struct Mock230Server* srv)
{
    struct Mock230Player* player;
    int script_id;

    assert(srv);
    player = srv->active_player;
    assert(player);

    /* LostCity Player.processWalktrigger: fire when armed, not delayed, and not
     * mid protected-access script. Clear before run so the script must re-arm. */
    if( !srv->scripts_ok )
        return;
    if( player->walktrigger < 0 )
        return;
    if( srv->tick < player->delayed_until )
        return;
    if( player->active_script )
        return;

    script_id = player->walktrigger;
    player->walktrigger = -1;
    run_script_id(srv, script_id, 0, 0, -1, 1);
}

int
mock230_scripts_resume_button(
    struct Mock230Server* srv,
    int component_uid)
{
    struct Mock230Player* player = srv->active_player;
    struct SSVM_State* state = player->active_script;

    if( !srv->scripts_ok || !state )
        return 0;
    if( state->execution != SSVM_PAUSEBUTTON )
        return 0;

    /* Only a button the script registered may release it. Anything else is a
     * click on some other interface and must leave the script parked — which is
     * also why the uid has to survive the wire at full width. */
    for( int i = 0; i < player->resume_button_count; i++ )
    {
        if( player->resume_buttons[i] != component_uid )
            continue;
        player->last_com = component_uid;
        player->resume_button_count = 0;
        rebind_active_npc(srv, state);
        return run_or_park(srv, state);
    }
    return 0;
}

int
mock230_scripts_close_dialogue(struct Mock230Server* srv)
{
    struct Mock230Player* player;
    struct SSVM_State* state;

    if( !srv || !srv->active_player )
        return 0;
    player = srv->active_player;
    state = player->active_script;
    if( !state )
        return 0;
    if( state->execution != SSVM_PAUSEBUTTON && state->execution != SSVM_COUNTDIALOG )
        return 0;

    /* The conversation ends here rather than resuming: the script's next
     * statement is whatever followed the ~chatnpc, and running it after the
     * player has walked off would put the *rest* of the dialogue on screen one
     * page at a time with nobody to talk to. */
    release_parked(srv, state);
    player->resume_button_count = 0;
    return 1;
}

int
mock230_scripts_run_script(
    struct Mock230Server* srv,
    int script_id)
{
    if( !srv->scripts_ok )
        return 0;
    return run_script_id(srv, script_id, 0, 0, -1, 1);
}

/*
 * Run a named content proc immediately, with int arguments.
 *
 * This is the seam that lets policy live in content while the engine still owns
 * the tick. Anything the reference expresses as a `[proc,...]` — the experience
 * table, the swing animation, the hit formulas — should be reachable from C
 * through here rather than reimplemented as a C `switch`, which is how those
 * rules drift from the reference silently.
 *
 * Immediate, not queued: a caller mid-swing needs the effect this tick, and a
 * queue entry would land on the next one.
 *
 * Missing script means do nothing and say so under MOCK230_VERBOSE — the same
 * fallback rule the rest of this file follows.
 */
int
mock230_scripts_run_hook_sv(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc)
{
    struct SSVM_State* state;

    /* A NULL script is a no-op — used by the by-name helpers when a name is
     * not in the pack, and by internal callers that may pass a resolved pointer
     * that legitimately came back empty. */
    if( !srv->scripts_ok || !script )
        return 0;

    state = SSVM_StateAlloc(srv->script_env, script, args, argc, strv, strc);
    if( !state )
    {
        fprintf(stderr, "mock230: %s rejected %d argument(s)\n", script->name, argc);
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    return run_or_park(srv, state);
}

/*
 * The by-name form, which is for tests.
 *
 * A test naming the script it tests is stating its subject; the engine naming
 * one is authoring content (§8.6). So the lookup lives here rather than at the
 * call sites, and the engine goes through triggers.
 */
int
mock230_scripts_run_proc_sv(
    struct Mock230Server* srv,
    const char* name,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            printf("mock230: no %s — engine fallback\n", name);
        return 0;
    }
    return mock230_scripts_run_hook_sv(srv, script, args, argc, strv, strc);
}

/*
 * The int-only form, which is most callers.
 *
 * String arguments were reachable all along — `SSVM_StateAlloc` has taken a
 * `strv`/`strc` pair since it was written and this seam passed `NULL, 0` — and
 * that gap quietly decided a design question: content could not be handed a
 * name, so any message mentioning one had to be built in C. The prayer level
 * message ("You need a Prayer level of 31 to use Ultimate Strength.") is the
 * case that surfaced it.
 */
int
mock230_scripts_run_proc(
    struct Mock230Server* srv,
    const char* name,
    const int32_t* args,
    int argc)
{
    return mock230_scripts_run_proc_sv(srv, name, args, argc, NULL, 0);
}

/** The engine's form of the same call: a resolved hook, no name. */
int
mock230_scripts_run_hook(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc)
{
    return mock230_scripts_run_hook_sv(srv, script, args, argc, NULL, 0);
}

/*
 * Run a named content proc and read one int back.
 *
 * The void form above lets content own an *action*; this lets it own an
 * *answer*. The reference states plenty of rules as procs that return a value —
 * `[proc,combat_defend_anim](obj $weapon, obj $shield)(seq)` is the shape: a
 * priority chain (shield, then weapon, then unarmed) with membership
 * conditions, which is a policy no single param read can express and which has
 * no business being a C `if`.
 *
 * The result is the top of the int stack when the proc finishes. Read it BEFORE
 * run_or_park, which releases the state on FINISHED — hence the open-coded
 * execute here rather than reusing that helper. A proc that suspends cannot
 * answer, so anything but FINISHED is a miss and the caller keeps its default.
 *
 * Returns 1 and writes *out when the proc answered; 0 otherwise.
 */
int
mock230_scripts_run_hook_int_sv(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc,
    int32_t* out)
{
    struct SSVM_State* state;
    enum SSVM_Exec status;

    if( !srv->scripts_ok || !out || !script )
        return 0;

    state = SSVM_StateAlloc(srv->script_env, script, args, argc, strv, strc);
    if( !state )
    {
        fprintf(stderr, "mock230: %s rejected %d argument(s)\n", script->name, argc);
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);

    status = SSVM_Execute(state);
    if( status == SSVM_ABORTED )
        fprintf(stderr, "mock230: %s", SSVM_Backtrace(state));
    if( status != SSVM_FINISHED || state->isp < 1 )
    {
        SSVM_StateRelease(state);
        return 0;
    }
    *out = state->int_stack[state->isp - 1];
    SSVM_StateRelease(state);
    return 1;
}

/** By name, for tests. See mock230_scripts_run_proc_sv on why the split. */
int
mock230_scripts_run_proc_int_sv(
    struct Mock230Server* srv,
    const char* name,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc,
    int32_t* out)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            printf("mock230: no %s — engine fallback\n", name);
        return 0;
    }
    return mock230_scripts_run_hook_int_sv(srv, script, args, argc, strv, strc, out);
}

/** The int-only form. See mock230_scripts_run_proc for why the string half
 *  exists at all. */
int
mock230_scripts_run_proc_int(
    struct Mock230Server* srv,
    const char* name,
    const int32_t* args,
    int argc,
    int32_t* out)
{
    return mock230_scripts_run_proc_int_sv(srv, name, args, argc, NULL, 0, out);
}

/** The engine's form: a resolved hook that answers with one int. */
int
mock230_scripts_run_hook_int(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    int32_t* out)
{
    return mock230_scripts_run_hook_int_sv(srv, script, args, argc, NULL, 0, out);
}

/*
 * Put a queue script on the player's queue from the engine side.
 *
 * `queue(...)` is a ServerScript op, so content can already do this; what this
 * adds is the engine being able to *start* the exchange. Auto-retaliate is the
 * case that needs it: the npc's swing happens in C, and the response is
 * content.
 *
 * A NULL script queues nothing — used by the by-name form when a name is
 * absent, and by internal callers whose pointer may legitimately be empty.
 */
int
mock230_scripts_queue_hook(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    int delay,
    int32_t arg)
{
    struct Mock230Player* player;

    if( !srv->scripts_ok || !script )
        return 0;

    player = srv->active_player;
    for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
    {
        if( player->queue[i].active )
            continue;
        player->queue[i].active = 1;
        player->queue[i].script_id = script->id;
        /* +1 for the same reason SS_OP_QUEUE does it: the drain decrements
         * before it fires, so delay 0 has to mean "next tick". */
        player->queue[i].delay = delay + 1;
        player->queue[i].arg = arg;
        player->queue[i].kind = MOCK230_QUEUE_NORMAL;
        player->queue[i].logout_action = 0;
        return 1;
    }
    /*
     * Reported, because it became reachable the day the queue learned to wait.
     *
     * Before the `canAccess()` gate every entry drained on its next tick, so 16
     * slots meant 16 entries in flight. An entry now sits there for as long as
     * the player is busy, and a fight with a level-up message box on screen adds
     * one per hit. The reference's queue is a linked list with no cap at all, so
     * this limit is this engine's and nothing content can see coming — and a
     * dropped `[queue,player_death]` looks exactly like a death that never ends.
     */
    fprintf(stderr, "mock230: %s dropped — the player's queue is full (%d)\n", script->name,
            MOCK230_QUEUE_MAX);
    return 0;
}

/** By name, for tests. See mock230_scripts_run_proc_sv on why the split. */
int
mock230_scripts_queue_named(
    struct Mock230Server* srv,
    const char* name,
    int delay,
    int32_t arg)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            printf("mock230: no %s — nothing queued\n", name);
        return 0;
    }
    return mock230_scripts_queue_hook(srv, script, delay, arg);
}

/* ------------------------------------------------------------------ */
/* Trigger dispatch                                                    */
/* ------------------------------------------------------------------ */

/*
 * Which namespace a trigger's subject id is drawn from.
 *
 * Only so a miss can be *named*: "no trigger for [opnpc2,goblin]" is a sentence
 * somebody can act on and "no trigger for 11/3105" is a puzzle. The reference
 * prints the same line off `type.debugname` (Player.defaultOp, OpHeldHandler);
 * the names here come from the same `pack/` files the compiler resolved the
 * script's own subject through, so a name that prints is a name that could have
 * been bound.
 *
 * The ap/op/ai families are laid out in contiguous runs by subject
 * (ss_trigger.h), so this is ranges rather than 168 rows.
 */
static int
trigger_subject_kind(
    int trigger,
    enum Mock230PackKind* out_kind)
{
    if( (trigger >= SS_TRIGGER_APNPC1 && trigger <= SS_TRIGGER_AI_OPNPC5) ||
        trigger == SS_TRIGGER_AI_TIMER || trigger == SS_TRIGGER_AI_SPAWN ||
        trigger == SS_TRIGGER_AI_DESPAWN || trigger == SS_TRIGGER_AI_WALKTRIGGER ||
        (trigger >= SS_TRIGGER_AI_QUEUE1 && trigger <= SS_TRIGGER_AI_QUEUE20) ||
        (trigger >= SS_TRIGGER_AI_APPLAYER1 && trigger <= SS_TRIGGER_AI_OPPLAYER5) )
    {
        /* The ai_* families are subjects *of an npc*, whichever entity the
         * trigger is about — `[ai_opplayer1,goblin]` names the goblin. */
        *out_kind = MOCK230_PACK_NPC;
        return 1;
    }
    if( (trigger >= SS_TRIGGER_APOBJ1 && trigger <= SS_TRIGGER_AI_OPOBJ5) ||
        (trigger >= SS_TRIGGER_OPHELD1 && trigger <= SS_TRIGGER_OPHELDT) )
    {
        *out_kind = MOCK230_PACK_OBJ;
        return 1;
    }
    if( trigger >= SS_TRIGGER_APLOC1 && trigger <= SS_TRIGGER_AI_OPLOC5 )
    {
        *out_kind = MOCK230_PACK_LOC;
        return 1;
    }
    if( trigger == SS_TRIGGER_IF_BUTTON ||
        (trigger >= SS_TRIGGER_INV_BUTTON1 && trigger <= SS_TRIGGER_INV_BUTTOND) )
    {
        *out_kind = MOCK230_PACK_COMPONENT;
        return 1;
    }
    /* IF_CLOSE / IF_OPEN subjects are bare interface ids (see close_modal's
     * `[if_close,bankmain]` comment) — not packed component uids. */
    if( trigger == SS_TRIGGER_IF_CLOSE || trigger == SS_TRIGGER_IF_OPEN )
    {
        *out_kind = MOCK230_PACK_INTERFACE;
        return 1;
    }
    return 0;
}

/*
 * Did a player ask for this, or did the engine?
 *
 * Only the first kind reports a miss, which is the reference's own division:
 * `Player.defaultOp` and the OpHeld/InvButton/IfButton handlers print
 * "No trigger for [...]" because a click that does nothing is a thing somebody
 * is standing there waiting for. `World.spawnNpc` and `Npc.processTimers` say
 * nothing, because an npc with no `[ai_spawn]` is every npc — 2,197 of them at
 * world init here, which is a wall of text rather than a diagnostic.
 *
 * The player families are contiguous and end exactly where their `ai_` twins
 * begin (ss_trigger.h): npc 3..16, obj 31..44, loc 59..72, player 87..100.
 */
static int
trigger_is_player_initiated(int trigger)
{
    return (trigger >= SS_TRIGGER_APNPC1 && trigger <= SS_TRIGGER_OPNPCT) ||
           (trigger >= SS_TRIGGER_APOBJ1 && trigger <= SS_TRIGGER_OPOBJT) ||
           (trigger >= SS_TRIGGER_APLOC1 && trigger <= SS_TRIGGER_OPLOCT) ||
           (trigger >= SS_TRIGGER_APPLAYER1 && trigger <= SS_TRIGGER_OPPLAYERT) ||
           (trigger >= SS_TRIGGER_OPHELD1 && trigger <= SS_TRIGGER_OPHELDT) ||
           trigger == SS_TRIGGER_IF_BUTTON || trigger == SS_TRIGGER_IF_CLOSE ||
           (trigger >= SS_TRIGGER_INV_BUTTON1 && trigger <= SS_TRIGGER_INV_BUTTOND);
}

/*
 * Triggers whose subject is an npc and whose scripts expect ACTIVE_NPC armed
 * (LostCity ScriptRunner.init(..., targetNpc)). Running without a live slot
 * reaches ~chatnpc and aborts on NPC_TYPE — joe_prequest's first page was the
 * loud case. Refuse at the door instead of starting a doomed script.
 */
static int
trigger_requires_active_npc(int trigger)
{
    return (trigger >= SS_TRIGGER_APNPC1 && trigger <= SS_TRIGGER_OPNPCT) ||
           (trigger >= SS_TRIGGER_AI_APNPC1 && trigger <= SS_TRIGGER_AI_OPNPC5) ||
           trigger == SS_TRIGGER_AI_TIMER || trigger == SS_TRIGGER_AI_SPAWN ||
           trigger == SS_TRIGGER_AI_DESPAWN || trigger == SS_TRIGGER_AI_WALKTRIGGER ||
           (trigger >= SS_TRIGGER_AI_QUEUE1 && trigger <= SS_TRIGGER_AI_QUEUE20) ||
           (trigger >= SS_TRIGGER_AI_APPLAYER1 && trigger <= SS_TRIGGER_AI_OPPLAYER5);
}

/** `[opnpc2,goblin]`, or `[opnpc2,3105]` when the id has no name. */
static const char*
trigger_label(
    int trigger,
    int type,
    char* buffer,
    size_t capacity)
{
    enum Mock230PackKind kind = MOCK230_PACK_COUNT;
    const char* subject = NULL;

    if( type >= 0 && trigger_subject_kind(trigger, &kind) )
        subject = mock230_content_symbol_name(kind, type);

    if( subject )
        snprintf(buffer, capacity, "[%s,%s]", SSVM_TriggerName(trigger), subject);
    else if( type >= 0 )
        snprintf(buffer, capacity, "[%s,%d]", SSVM_TriggerName(trigger), type);
    else
        snprintf(buffer, capacity, "[%s,_]", SSVM_TriggerName(trigger));
    return buffer;
}

/*
 * Start a script the way a trigger does: on the active player, with the npc the
 * trigger is about, and with no arguments.
 *
 * Shared by the keyed and the name-addressed forms so that the two cannot come
 * to disagree about what a trigger's execution context is — which they did in
 * one respect already, the npc, and `[if_button,...]` never had one.
 */
static int
run_trigger_script(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    int npc_slot,
    int loc_slot)
{
    struct SSVM_State* state = SSVM_StateAlloc(srv->script_env, script, NULL, 0, NULL, 0);

    if( !state )
    {
        fprintf(stderr, "mock230: %s expects arguments a trigger cannot supply\n",
                script->name);
        return MOCK230_TRIGGER_FAILED;
    }

    /* Every trigger the server fires is on behalf of whoever's turn it is, and
     * the engine grants protected access because these all arrive as a direct
     * response to player input. */
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);

    if( npc_slot >= 0 && npc_slot < MOCK230_NPC_MAX && srv->npcs[npc_slot].active )
    {
        /* The pointer satisfies the VM's require-an-active-npc check; the slot
         * in host_tag is what actually resolves it. Stored +1 so zero means
         * "no npc" without a separate flag. */
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }

    /* The active ground obj, for `[opobj<n>]` — `Player.getOpTrigger` sets
     * `state.activeObj` on the obj arm the same way it sets `activeNpc` on the
     * npc one. One-shot: consumed here so a script that itself fires a trigger
     * does not inherit it. The value is `mock230_world_obj_handle`'s — a slot
     * *and* the slot's generation, never a pointer, because the ground array is
     * a free list and a suspended script must resume onto its own obj or none. */
    if( srv->pending_active_obj )
    {
        SSVM_SetActive(state, SSVM_ENT_OBJ, SSVM_PRIMARY, (void*)srv->pending_active_obj);
        srv->pending_active_obj = 0;
    }

    /*
     * The loc the trigger is about, on the same footing as the npc.
     *
     * `SSVM_ENT_LOC` had exactly three writers tree-wide before 2026-08-02 — the
     * iterator, `LOC_FIND` and `LOC_ADD` — so **every** `[oploc<n>]` and
     * `[aploc<n>]` script ran with no active loc, and the first `loc_coord`,
     * `loc_angle`, `loc_shape`, `loc_param`, `loc_change` or `loc_del` in it
     * aborted with "the active loc is gone". A door script could bind and could
     * not say anything about the door it was bound to; that, and not the `loc_*`
     * family, was what the `oploc` fallback row was actually waiting on.
     *
     * By *slot*, not by pointer, and encoded `slot + 1` — the convention
     * `mock230_ops_loc.c` and `LOC_FIND` already share, for the reason stated
     * there: a script can suspend between the dispatch and the read, and a scene
     * rebuild reallocates the array underneath it. The pointer's only job is to
     * satisfy the VM's require-an-active-loc check.
     */
    if( loc_slot >= 0 )
    {
        struct Mock230SceneLoc* loc = mock230_scene_loc(loc_slot);

        if( loc && loc->active )
        {
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY,
                           (void*)(intptr_t)(loc_slot + 1));
        }
    }

    /* run_or_park answers 1 for ran-or-parked and 0 for every way a bound script
     * can fail to run — aborted, no parking slot, world queue full. All of those
     * are FAILED here: a script existed and the behaviour did not happen. */
    return run_or_park(srv, state) ? MOCK230_TRIGGER_RAN : MOCK230_TRIGGER_FAILED;
}

/*
 * `chain` selects getByTrigger (type -> category -> `_`) over
 * getByTriggerSpecific (one rung, no fallback). `report` is off only for the
 * keyed half of the if_button pair, which is not a miss until the name-addressed
 * half has also missed.
 */
static int
run_trigger_impl(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    int loc_slot,
    int chain,
    int report)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return MOCK230_TRIGGER_NONE;

    script = chain ? SSVM_ProviderGetByTrigger(srv->scripts, trigger, type, category)
                   : SSVM_ProviderGetByTriggerSpecific(srv->scripts, trigger, type, category);
    if( !script )
    {
        if( report && srv->verbose && trigger_is_player_initiated(trigger) )
        {
            char label[192];

            /* The reference's own wording, from `Player.defaultOp`, and its own
             * condition: a debug build says so, a production one is silent.
             * Nothing else distinguishes a trigger that deliberately does
             * nothing from a packet that never arrived. */
            fprintf(stderr, "mock230: no trigger for %s\n",
                    trigger_label(trigger, type, label, sizeof(label)));
        }
        return MOCK230_TRIGGER_NONE;
    }

    if( trigger_requires_active_npc(trigger) )
    {
        int live = npc_slot >= 0 && npc_slot < MOCK230_NPC_MAX &&
                   srv->npcs[npc_slot].active;

        if( !live )
        {
            char label[192];

            fprintf(
                stderr,
                "mock230: %s refused — no live npc (slot=%d script=%s)\n",
                trigger_label(trigger, type, label, sizeof(label)),
                npc_slot,
                script->name ? script->name : "?");
            return MOCK230_TRIGGER_FAILED;
        }
    }

    return run_trigger_script(srv, script, npc_slot, loc_slot);
}

int
mock230_scripts_run_trigger(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int npc_slot)
{
    return run_trigger_impl(srv, trigger, type, category, npc_slot, -1, 1, 1);
}

/*
 * Trigger dispatch with string arguments — used by friend login/logout
 * notifications that hand the display name to content (`[friendlogin,_]`).
 */
int
mock230_scripts_run_trigger_sv(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    const char* const* strv,
    int strc)
{
    const struct SSVM_Script* script;
    struct SSVM_State* state;

    if( !srv->scripts_ok )
        return MOCK230_TRIGGER_NONE;

    script = SSVM_ProviderGetByTrigger(srv->scripts, trigger, type, category);
    if( !script )
        return MOCK230_TRIGGER_NONE;

    state = SSVM_StateAlloc(srv->script_env, script, NULL, 0, strv, strc);
    if( !state )
    {
        fprintf(stderr, "mock230: %s rejected string argument(s)\n",
                script->name ? script->name : "?");
        return MOCK230_TRIGGER_FAILED;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot >= 0 && npc_slot < MOCK230_NPC_MAX && srv->npcs[npc_slot].active )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }
    return run_or_park(srv, state) ? MOCK230_TRIGGER_RAN : MOCK230_TRIGGER_FAILED;
}

int
mock230_scripts_run_trigger_on_loc(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int loc_slot)
{
    return run_trigger_impl(srv, trigger, type, category, -1, loc_slot, 1, 1);
}

int
mock230_scripts_run_trigger_specific(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int npc_slot)
{
    return run_trigger_impl(srv, trigger, type, category, npc_slot, -1, 0, 1);
}

/*
 * One (trigger, component) attempt: by key, then by name.
 *
 * Both spellings have to be tried for every trigger in this family, because
 * which one a script compiled under is decided by arithmetic rather than by
 * the author. `SSVM_LookupKey` puts the subject at bit 10 of an i32, so a
 * component uid `(interface << 16) | child` only fits for interfaces below 32
 * — everything else compiles name-addressed (`ssc_compile.c`'s
 * `symbol->value >= (1 << 21)` branch). `stats:attack` is 20,971,521 and is in
 * the second class; `chatmenu:options` is in the first.
 */
static int
run_if_button_trigger(
    struct Mock230Server* srv,
    int trigger,
    int uid,
    const char* component)
{
    const struct SSVM_Script* script;
    char name[192];
    int result;

    /* *Specific*: `IfButtonHandler` uses getByTriggerSpecific, so a component
     * with no script of its own does not fall through to some `[if_button,_]`
     * that would then swallow every click in the game. */
    result = run_trigger_impl(srv, trigger, uid, -1, -1, -1, 0, 0);
    if( result != MOCK230_TRIGGER_NONE )
        return result;

    if( !component )
        return MOCK230_TRIGGER_NONE;

    snprintf(name, sizeof(name), "[%s,%s]", SSVM_TriggerName(trigger), component);
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
        return MOCK230_TRIGGER_NONE;
    return run_trigger_script(srv, script, -1, -1);
}

int
mock230_scripts_run_if_button(
    struct Mock230Server* srv,
    int uid,
    int op_num)
{
    const char* component;
    int result;

    if( !srv->scripts_ok )
        return MOCK230_TRIGGER_NONE;

    component = mock230_content_symbol_name(MOCK230_PACK_COMPONENT, uid);

    /*
     * The numbered trigger first, then the unnumbered one.
     *
     * A rev-230 component carries up to ten ops and the packet says which was
     * clicked; `stats:attack` alone has "Toggle Attack XP" on op 1 and "View
     * Attack guide" on op 2, and before IF_BUTTON1..10 existed a script bound
     * to that component could not tell them apart. So content that cares
     * writes `[if_button2,stats:attack]`, and content that does not keeps
     * `[if_button,...]` and answers every op — which is what every script in
     * the tree written before this did, and why the unnumbered form is a
     * fallthrough rather than a replacement.
     *
     * This does not widen the *engine's* fallback list (`enum Mock230Fallback`,
     * osrs230_mockserver.md §3.18): both rungs are content, and the C rung
     * below is the same single one it always was.
     */
    if( op_num >= 1 && op_num <= 10 )
    {
        result = run_if_button_trigger(srv, SS_TRIGGER_IF_BUTTON1 + (op_num - 1), uid, component);
        if( result != MOCK230_TRIGGER_NONE )
            return result;
    }

    result = run_if_button_trigger(srv, SS_TRIGGER_IF_BUTTON, uid, component);
    if( result != MOCK230_TRIGGER_NONE )
        return result;

    if( srv->verbose )
    {
        if( component )
            fprintf(stderr, "mock230: no trigger for [if_button%d,%s] or [if_button,%s]\n",
                    op_num, component, component);
        else
            fprintf(stderr, "mock230: no trigger for [if_button,%d:%d] (no component name)\n",
                    uid >> 16, uid & 0xffff);
    }
    return MOCK230_TRIGGER_NONE;
}

/*
 * Resolve a coordinate-subject trigger to its script, or NULL.
 *
 * Both public entry points go through this so the *name* has one spelling. The
 * spelling is the whole contract: if it disagrees with `ssc_compile.c`'s by one
 * character, every zone script in the tree stops running and nothing says so.
 */
static const struct SSVM_Script*
zone_trigger_script(
    struct Mock230Server* srv,
    int trigger,
    int level,
    int x,
    int z)
{
    const char* trigger_name;
    char name[192];

    if( !srv->scripts_ok )
        return NULL;

    trigger_name = SSVM_TriggerName(trigger);
    if( !trigger_name || !trigger_name[0] )
        return NULL;

    switch( trigger )
    {
    case SS_TRIGGER_ZONE:
    case SS_TRIGGER_ZONEEXIT:
        /*
         * `[zone,<level>_<mx>_<mz>_<lx>_<lz>]`, where the last two are **tile
         * offsets inside the map square**, 0/8/…/56 — not zone indices 0..7, and
         * not zero-padded. `Player.ts`'s own formatter:
         *   `${level}_${x >> 6}_${z >> 6}_${(x & 0x3f) >> 3 << 3}_${…}`
         * Getting a character of this wrong costs nothing at build time and is
         * permanently silent at run time, which is why the selftest asserts a
         * compiled header is retrievable by exactly this string.
         */
        snprintf(name, sizeof(name), "[%s,%d_%d_%d_%d_%d]", trigger_name, level, x >> 6, z >> 6,
                 ((x & 0x3f) >> 3) << 3, ((z & 0x3f) >> 3) << 3);
        break;

    case SS_TRIGGER_MAPZONE:
    case SS_TRIGGER_MAPZONEEXIT:
        /*
         * The map square, and the level is a literal 0 — the reference builds
         * this latch with `CoordGrid.packCoord(0, x, z)`, so climbing a ladder
         * inside one square does not re-enter it. `level` is accepted and
         * ignored on purpose: the caller should not have to know that.
         */
        (void)level;
        snprintf(name, sizeof(name), "[%s,0_%d_%d]", trigger_name, x >> 6, z >> 6);
        break;

    default:
        return NULL;
    }

    /*
     * No keyed rung, unlike `run_if_button`. There, the keyed lookup is a
     * correct fast path; here it is actively wrong — `ssc_lex.c` packs a 5-part
     * coord into 28 bits and the compiled key gives its subject 21, so a zone's
     * key either goes negative (and `ssvm_provider.c` keeps negatives out of the
     * index) or wraps onto a subject that no runtime lookup reproduces. Name is
     * the only address these four have. `ssc_compile.c` now writes -1 for a
     * coord subject so that is true by construction rather than by luck.
     *
     * A miss is silent, and a miss is the overwhelmingly common case: there are
     * tens of thousands of nameable zones and a handful of bound ones. What
     * bounds the cost is not the lookup but the *latch* — this runs once per
     * zone crossing, at most one crossing per four ticks at walking pace, never
     * once per tick. `SSVM_ProviderGetByName` is FNV-1a plus a binary search
     * that stops at the first mismatched hash, so a miss is ~log2(n) compares
     * and no strcmp at all. Measured on this tree's pack (see
     * osrs230_mockserver.md §3.21) rather than asserted.
     */
    return SSVM_ProviderGetByName(srv->scripts, name);
}

int
mock230_scripts_run_trigger_at(
    struct Mock230Server* srv,
    int trigger,
    int level,
    int x,
    int z)
{
    const struct SSVM_Script* script = zone_trigger_script(srv, trigger, level, x, z);

    if( !script )
        return MOCK230_TRIGGER_NONE;
    return run_trigger_script(srv, script, -1, -1);
}

int
mock230_scripts_queue_trigger_at(
    struct Mock230Server* srv,
    int trigger,
    int level,
    int x,
    int z)
{
    const struct SSVM_Script* script = zone_trigger_script(srv, trigger, level, x, z);
    struct Mock230Player* player;

    if( !script )
        return MOCK230_TRIGGER_NONE;

    player = srv->active_player;
    for( int i = 0; i < MOCK230_ENGINE_QUEUE_MAX; i++ )
    {
        if( player->engine_queue[i].active )
            continue;
        player->engine_queue[i].active = 1;
        player->engine_queue[i].script_id = script->id;
        /*
         * Zero, not `delay + 1`. `enqueueScript` overwrites the delay with 0 for
         * ENGINE, and `processEngineQueue` compares the value the counter had
         * *before* its decrement — so the entry is due on the first drain after
         * this one, which is phase 5 of the next tick.
         */
        player->engine_queue[i].delay = 0;
        player->engine_queue[i].arg = 0;
        player->engine_queue[i].kind = MOCK230_QUEUE_ENGINE;
        player->engine_queue[i].logout_action = 0;
        return MOCK230_TRIGGER_RAN;
    }

    /* Same argument as `queue_hook`'s: the reference's list has no cap, so an
     * overflow is this engine's own and content cannot see it coming. */
    fprintf(stderr, "mock230: %s dropped — the engine queue is full (%d)\n", script->name,
            MOCK230_ENGINE_QUEUE_MAX);
    return MOCK230_TRIGGER_NONE;
}

void
mock230_scripts_process_engine_queue(struct Mock230Server* srv)
{
    struct Mock230Player* player;

    if( !srv->scripts_ok )
        return;

    player = srv->active_player;
    for( int i = 0; i < MOCK230_ENGINE_QUEUE_MAX; i++ )
    {
        struct Mock230Queued* entry = &player->engine_queue[i];
        int script_id;
        int due;

        if( !entry->active )
            continue;

        /*
         * `const delay = request.delay--; if (this.canAccess() && delay <= 0)`.
         * The decrement is unconditional and only the run is gated, exactly as
         * the normal queue's is — so an entry held by a dialogue fires on the
         * tick the dialogue closes rather than restarting a wait it never had.
         */
        due = entry->delay-- <= 0;
        if( !due || !player_can_access(srv) )
            continue;

        script_id = entry->script_id;
        entry->active = 0;
        /* Protected, as `processEngineQueue`'s `executeScript(script, true)`. */
        run_script_id(srv, script_id, 0, 0, -1, 1);
    }
}

/*
 * Exchange the clicked item with the dragged one.
 *
 * Both halves move together — item *and* slot — because a script that reads
 * `last_slot` to consume what it just used would otherwise address the wrong
 * backpack cell. The reference swaps them as two destructuring assignments back
 * to back for the same reason.
 */
static void
opheldu_swap(struct Mock230Player* player)
{
    int item = player->last_item;
    int slot = player->last_slot;

    player->last_item = player->last_useitem;
    player->last_useitem = item;
    player->last_slot = player->last_useslot;
    player->last_useslot = slot;
}

int
mock230_scripts_run_opheldu(
    struct Mock230Server* srv,
    int obj_type,
    int obj_category,
    int use_obj_type,
    int use_obj_category)
{
    struct Mock230Player* player = srv->active_player;
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return MOCK230_TRIGGER_NONE;

    /*
     * Four rungs, `getByTriggerSpecific` throughout: `OpHeldUHandler` never asks
     * for `[opheldu,_]`, and the reference tree has none — a wildcard here would
     * swallow every "use A on B" in the game the moment somebody wrote one.
     */

    /* 1 — the item that was clicked. */
    script = SSVM_ProviderGetByTriggerSpecific(srv->scripts, SS_TRIGGER_OPHELDU, obj_type, -1);

    /* 2 — the item that was dragged. The swap is *outside* the null check, in
     * the reference and here: rungs 3 and 4 then run against swapped state, and
     * the category rungs' orientation depends on it. See the header comment. */
    if( !script )
    {
        script =
            SSVM_ProviderGetByTriggerSpecific(srv->scripts, SS_TRIGGER_OPHELDU, use_obj_type, -1);
        opheldu_swap(player);
    }

    /* 3 — the clicked item's category. */
    if( !script && obj_category != -1 )
        script =
            SSVM_ProviderGetByTriggerSpecific(srv->scripts, SS_TRIGGER_OPHELDU, -1, obj_category);

    /* 4 — the dragged item's category, and back the other way. */
    if( !script && use_obj_category != -1 )
    {
        script = SSVM_ProviderGetByTriggerSpecific(srv->scripts, SS_TRIGGER_OPHELDU, -1,
                                                   use_obj_category);
        opheldu_swap(player);
    }

    if( !script )
    {
        if( srv->verbose )
        {
            char label[192];

            /* The *clicked* item names the miss, which is what the reference
             * prints (`No trigger for [opheldu,${objType.debugname}]`) — and by
             * this point the swap may have moved it, so it comes from the
             * argument rather than from the player. */
            fprintf(stderr, "mock230: no trigger for %s\n",
                    trigger_label(SS_TRIGGER_OPHELDU, obj_type, label, sizeof(label)));
        }
        return MOCK230_TRIGGER_NONE;
    }
    return run_trigger_script(srv, script, -1, -1);
}

/* ------------------------------------------------------------------ */
/* Engine fallbacks                                                    */
/* ------------------------------------------------------------------ */

/*
 * An opcode a `blocked_on` string names as the thing that is missing.
 *
 * This exists for the assertion under it, not for the documentation. `ai_queue3`
 * printed "drop tables need npc categories" at every boot for two stages after
 * categories, the category rung and 69 ported drop-table files had all landed.
 * Nothing was wrong with the code — the *reason* had expired, and an expired
 * reason reads exactly like a live one. Prose cannot go stale loudly, so the
 * citation is machine-readable as well as printed: a row that says
 * `OC_WEARPOS` is missing is only true while nothing implements `OC_WEARPOS`,
 * and `fallback_stale_blockers` says so at boot on the day somebody does.
 *
 * `value` is the opcode's number from `ss_opcode.h`, or **-1 for an opcode
 * nothing has even declared yet** — which is a real state here: `LAST_VERB` is
 * a reader two rows are blocked on and there is no `SS_OP_LAST_VERB` at all.
 * `MOCK230_BLOCKER_*` below turns the declaration itself into the switch, so
 * declaring the opcode is enough to put it under the check. Every other row
 * uses `BLOCKING_OP(sym)`, which stringifies the same token it evaluates: the
 * printed name and the checked value cannot be edited apart.
 *
 * The check is deliberately one-directional. It catches "this row cites
 * something that is now present", which is the failure that hides; it cannot
 * catch a blocker that is not an opcode (an unwritten entity binding, a
 * component that is armed but not bound), and those are cited in the text with
 * the grep that settles them instead.
 */
struct FallbackBlockingOp
{
    /** As `ss_opcode.h` spells it, so the text and the check cannot drift. */
    const char* name;
    /** Its value, or -1 when nothing declares it yet. */
    int value;
};

/* Nothing declares a `last_verb` reader. The engine latches the verb
 * (`mock230_world.c`, `player->last_verb = op_num`) and content cannot read it,
 * which is the shared blocker under `inv_button` and `if_button`. Written as an
 * #ifdef rather than a -1 literal so that declaring the opcode automatically
 * hands it to the staleness check — and under a private name so that a stray
 * `case SS_OP_LAST_VERB:` can never pick up a placeholder. */
#ifdef SS_OP_LAST_VERB
#define MOCK230_BLOCKER_LAST_VERB SS_OP_LAST_VERB
#else
#define MOCK230_BLOCKER_LAST_VERB (-1)
#endif

/* Stringify the symbol rather than repeating it, so the printed name and the
 * checked value are the same token and cannot be edited apart. */
#define BLOCKING_OP(sym) { #sym, (sym) }

static const struct FallbackBlockingOp k_blocked_opnpc[] = {
    /* None. `opnpc` is not blocked on an opcode — see its text. */
    { NULL, 0 }
};

/* `k_blocked_oploc` was here. It listed P_OPLOC, LOC_CATEGORY and LC_CATEGORY;
 * all three landed 2026-08-02 and the list emptied in the same commit, because
 * `fallback_stale_blockers` is fatal in the selftest and a row cannot outlive
 * its cited opcodes quietly. The row itself outlived them by a day — see
 * `enum Mock230Fallback` in mock230.h for what the last blocker actually was —
 * and went the same day the content that answers it landed. */

/*
 * `k_blocked_opobj` was here and is gone with its row, in two stages that are
 * worth keeping as the worked example of the order §2.5 asks for.
 *
 * Stage one widened the surface: it cited OBJ_COORD, OBJ_COUNT, OBJ_DEL,
 * OBJ_FIND, OBJ_TAKEITEM and OBJ_TYPE; five landed in mock230_ops_obj.c and
 * OBJ_FIND came *off the list* rather than being implemented, because
 * `[opobj3,_]` does not call it — the dispatch supplies the active obj. The row
 * then stood with an empty `blocked_ops` and a text saying so.
 *
 * Stage two moved the behaviour and deleted the row. What made the deletion
 * checkable rather than hopeful: the two selftest legs were re-pointed at
 * OPOBJ3 *while `interaction_engine_obj` was still present* and unbinding
 * `[opobj3,_]` left them **green**, because the fallback answered. That is the
 * measurement — a leg that stays green under the mutation is measuring the C —
 * and it is why the legs could only become evidence once the C was gone.
 */

/*
 * `k_blocked_opheld` is gone with MOCK230_FALLBACK_OPHELD, 2026-08-02, and the
 * account of what it cited is worth keeping because only five of its seven
 * opcodes were ever true.
 *
 * Landed the stage before the row went: OC_WEARPOS / OC_WEARPOS2 / OC_WEARPOS3
 * (mock230_ops_obj.c) and INV_MOVEFROMSLOT / INV_DROPSLOT (mock230_ops_inv.c).
 * `inv_moveitem` moved into that file at the same time and grew the generic
 * container-to-container arm every equip and unequip path needs — it had been
 * three bank arms and a printf, and `gen_opcode_coverage.py` reported the
 * opcode *covered*, because a `case` label is all it can see.
 *
 * Came OFF the list without being implemented: BUILDAPPEARANCE (2004), because
 * `put_appearance` reads `player->worn` unconditionally and the opcode's job in
 * the reference is to *select* the container the encoder reads — so accepting
 * the argument and raising the mask would be plausible, wrong and quiet
 * (§3.13d); and P_CLEARPENDINGACTION (2070), misfiled, because
 * `mock230_world_clear_pending_action` was never called from `handle_opheld` at
 * all. Both are still true and neither is implemented.
 *
 * And what actually blocked the row was none of those. It was that **the level
 * requirement had no script-readable form**: dispatch is content-first, so
 * binding `[opheld2,_]` stopped `mock230_equipment_may_wear` running and took
 * the gate with it. That is a data-relocation job, and the stage that did it
 * found the thing every prose account of the data had wrong — the requirement
 * is a MERGE, `.obj` overlay plus the cache's own skillrequire/levelrequire
 * params, and the overlay is 59% of it. See
 * skill_combat/scripts/levelrequire.rs2.
 */

static const struct FallbackBlockingOp k_blocked_inv_button[] = {
    { NULL, 0 }
};

static const struct FallbackBlockingOp k_blocked_if_button[] = {
    { NULL, 0 }
};

/*
 * The C that still answers a trigger nothing is bound to, named and counted.
 *
 * `blocked_on` is the whole reason each row is allowed to exist. It is not a
 * comment: the boot prints it, so "why is this behaviour not content?" has an
 * answer at the point where somebody is asking, and a row whose blocker has
 * been cleared is visibly a row to delete.
 *
 * Each string is written to be **checkable in one command** rather than
 * plausible — a symbol as its header spells it, a file:line, a line count, a
 * reference path — because the previous set read plausibly and one of them was
 * simply false. Where a blocker is partly cleared the string says which part;
 * "NOT x" openings are load-bearing, not rhetoric. `blocked_ops` is the machine
 * half of the same job.
 *
 * The rule this table encodes is PORTING_GUIDE §2.5's: widen the ServerScript
 * surface until a script can say it, move the behaviour, delete the row. Adding
 * a row goes the other way and is not a choice a content port gets to make.
 */
static const struct
{
    const char* name;
    const char* blocked_on;
    /** The opcodes `blocked_on` names, NULL-terminated. Never NULL itself. */
    const struct FallbackBlockingOp* blocked_ops;
} k_engine_fallbacks[MOCK230_FALLBACK_COUNT] = {
    [MOCK230_FALLBACK_OPNPC] =
        { "opnpc",
          "NOT an opcode — this is the one row with an empty blocked_ops, and the only "
          "one whose blocker is a volume of C. mock230_combat.c is 1,061 lines "
          "(`wc -l`) and stays engine; the row itself is interaction_engine_npc, "
          "mock230_world.c:2333-2364, whose greeting half is ALREADY content "
          "([proc,npc_default_chat], player/messages.rs2:137) — so what is left in C is "
          "a strcmp against the cache's own Attack verb and the FACE_ENTITY latch "
          "before the proc, and nothing else. [opnpc2,_] now owns Attack for op 2 "
          "and p_opnpc re-dispatches through the interaction path for all ops. "
          "Blocked on the remaining combat_engage callers moving to content",
          k_blocked_opnpc },
    [MOCK230_FALLBACK_INV_BUTTON] =
        { "inv_button",
          "Bank item ops no longer arrive as INV_BUTTON — the client emits "
          "IF_BUTTON1..10 for IF_SETEVENTS-armed component rows, and content binds "
          "[if_button1..8,bankmain:items] / [if_button2..8,bankside:items] "
          "(interface_bank/scripts/bank.rs2, bank_deposit.rs2). This row remains for "
          "any other unbound INV_BUTTON surface (worn tab, shops). The C bank "
          "quantity ladder is deleted; mock230_bank_handle_button is a no-op stub",
          k_blocked_inv_button },
    [MOCK230_FALLBACK_IF_BUTTON] =
        { "if_button",
          "Bank settings and item ops are content on the armed components "
          "(bankmain:{swap_insert,note,quantity*,depositinv,depositworn,items}). "
          "mock230_bank_handle_button is a no-op stub. This row remains for other "
          "unbound IF_BUTTON clicks outside the bank. No last_verb opcode — numbered "
          "[if_buttonN] encodes the op index (skill_guide pattern)",
          k_blocked_if_button },
};

/**
 * How many live rows cite an opcode that is now implemented — 0 is the only
 * acceptable answer, and the selftest pins it there.
 *
 * This is the guard against the way this list actually failed. A row does not
 * usually go wrong by being wrong when it is written; it goes wrong when the
 * thing it is waiting for arrives and nobody comes back to the row, at which
 * point it keeps printing a reason that is no longer a reason. That is not
 * visible by reading — a stale blocker and a live one look identical — so the
 * moment the opcode lands, this says so, at boot, unconditionally, next to the
 * count it invalidates.
 *
 * It is loud rather than fatal on purpose: the opcode landing is *progress*,
 * and a server that refuses to start because somebody implemented OBJ_DEL would
 * teach exactly the wrong lesson. The selftest is where it is an error.
 */
static int
fallback_stale_blockers(void)
{
    int stale = 0;

    for( int i = 0; i < MOCK230_FALLBACK_COUNT; i++ )
    {
        const struct FallbackBlockingOp* op = k_engine_fallbacks[i].blocked_ops;

        for( ; op && op->name; op++ )
        {
            /* -1 is "nothing declares it yet", which is a blocker in its own
             * right and cannot be looked up. */
            if( op->value < 0 || !opcode_implemented(op->value) )
                continue;
            fprintf(stderr,
                    "mock230: STALE BLOCKER — the `%s` engine fallback says it is waiting "
                    "on %s (%d), and %s is implemented now. Either the row can go or its "
                    "reason has to be rewritten to what is still true "
                    "(k_engine_fallbacks[], mock230_scripts.c).\n",
                    k_engine_fallbacks[i].name, op->name, op->value, op->name);
            stale++;
        }
    }
    return stale;
}

int
mock230_scripts_stale_blockers(void)
{
    return fallback_stale_blockers();
}

int
mock230_scripts_report_fallbacks(struct Mock230Server* srv)
{
    /* One line by default and the roll under MOCK230_VERBOSE: the count is what
     * anyone reading a boot log needs (it should be going down), the reasons are
     * what somebody working on one needs. Both matter; only one of them belongs
     * in a log that also prints twenty times during the selftest. */
    fprintf(stderr, "mock230: %d engine fallback(s) still answer triggers content does not bind\n",
            MOCK230_FALLBACK_COUNT);
    /* Not behind `verbose`: this one is an error report, and the reader who
     * needs it is the person who just implemented the opcode — who has no
     * reason to be running with MOCK230_VERBOSE and every reason to be told. */
    fallback_stale_blockers();
    if( srv->verbose )
    {
        for( int i = 0; i < MOCK230_FALLBACK_COUNT; i++ )
            fprintf(stderr, "  %-12s blocked on: %s\n", k_engine_fallbacks[i].name,
                    k_engine_fallbacks[i].blocked_on);
    }
    return MOCK230_FALLBACK_COUNT;
}

/* ------------------------------------------------------------------ */
/* Shadowed engine verbs                                               */
/* ------------------------------------------------------------------ */

/**
 * Which `p_op*` discharges the obligation a shadowing script takes on.
 *
 * Binding `[opnpc2,goblin]` does not *add* to the engine's Attack — it replaces
 * it, because the engine's verb handling only runs when nothing was bound. A
 * script that means to keep the fight has to say so, and the way it says so is
 * to re-issue the op itself.
 */
static int
discharging_opcode(int trigger)
{
    if( trigger >= SS_TRIGGER_OPNPC1 && trigger <= SS_TRIGGER_OPNPC5 )
        return SS_OP_P_OPNPC;
    /* `[oploc<n>] -> SS_OP_P_OPLOC` was here and is unreachable now rather than
     * merely unused: with `interaction_engine_loc` deleted the engine claims no
     * loc verb, so `mock230_world_engine_claimed_verb` never returns one for a
     * loc and the report never asks what would discharge it. Leaving it would be
     * advice to write `[oploc1,_] p_oploc(1)`, which is an infinite recursion —
     * `p_oploc` re-issues the op and the op is this trigger. `P_OPLOC` itself
     * stays implemented (mock230_ops_player.c); it is what a script that means
     * to *resume* an op calls, which is all 43 of the reference's callers. */
    if( trigger >= SS_TRIGGER_OPHELD1 && trigger <= SS_TRIGGER_OPHELD5 )
        return SS_OP_P_OPHELD;
    return -1;
}

static int
script_calls(
    const struct SSVM_Script* script,
    int opcode)
{
    for( int i = 0; i < script->op_count; i++ )
        if( (int)script->opcodes[i] == opcode )
            return 1;
    return 0;
}

/**
 * Report every trigger content binds over a verb the engine answers itself.
 *
 * This is triage §7.7, which the fallback inversion did not close and could
 * not: inverting the fallback made a *missing* script loud, and this is the
 * opposite failure — a script that is present, runs fine, and quietly takes a
 * verb the engine was going to handle. Nothing fails. The goblin simply says
 * its line and stands there.
 *
 * That is not hypothetical. `skill_combat/combat.rs2` carries the scar in its
 * own header: a goblin's Attack is op 2, `[opnpc2,goblin]` replaced it, and the
 * fix was to add `p_opnpc(2)` back. The file then states the rule for everyone
 * else — *"any other script that binds an op the cache gives a verb to has the
 * same obligation"* — and until now nothing enforced it. §7.7's warning is that
 * this gets much worse on import: the reference binds 634 `[opnpc1]` and 867
 * `[oploc1]` triggers, and `levelrequire/` alone binds 304 `[opheld2]`, which
 * is the verb the engine equips on.
 *
 * At **load**, not at call time, for the reason `mock230_scripts_report_gaps`
 * gives: a script behind a quest step may never be triggered by anyone, and a
 * swallowed verb that nobody clicks this session is still a swallowed verb.
 *
 * Only exact-type bindings are checked. A `[opnpc1,_bandit]` category binding
 * or a bare `_` wildcard names no record, so there is no op list to read and
 * nothing to compare — those are invisible here, and saying so is better than
 * implying the check is total.
 *
 * The discharge test is deliberately generous: *any* `p_op*` of the right
 * family counts, without checking that its argument is the op that was bound.
 * The failure this exists to catch is the script that forgot entirely; one that
 * re-issues the wrong index is a different bug and this cannot see it.
 *
 * **A hit is a review item, not a defect**, and the distinction is not one this
 * can make. The second legitimate way to discharge the obligation is to do the
 * engine's job yourself, and nothing static tells that apart from doing
 * something else. So this prints a list and never fails a load. It is the same
 * posture as `mock230_pack`'s foreign-area spawn-prefix warning (triage §10.2):
 * a prompt to go and look, not a verdict.
 *
 * **The list is empty as of 2026-08-02, and getting it there was an eviction
 * rather than a fix.** It read 1, then 20 when the ladders landed, then 97 when
 * the 78 bank booths did — every entry a script taking a verb
 * `interaction_engine_loc` still answered. Deleting that function took all 97
 * out at once: the engine claims no loc verb now, so there is nothing for a
 * door, a ladder or a booth to shadow. What is left claimable is "Attack" on an
 * npc and "Wear"/"Wield"/"Drop" on a held obj. The 97 were the honest reading —
 * every one of those scripts *was* replacing engine behaviour — and the right
 * response to a review list that long was to delete what it was reviewing.
 *
 * Returns the number of scripts that shadow a verb without re-issuing it.
 */
int
mock230_scripts_report_shadowed_ops(struct Mock230Server* srv)
{
    int shadowed = 0;

    if( !srv->scripts_ok || !srv->scripts )
        return 0;

    for( int i = 0; i < srv->scripts->count; i++ )
    {
        const struct SSVM_Script* script = &srv->scripts->scripts[i];
        const char* verb;
        int trigger;
        int discharge;

        if( script->op_count <= 0 || !script->opcodes )
            continue;
        /* Name-addressed scripts (proc, label, queue, …) carry -1 and bind no
         * trigger at all. */
        if( script->lookup_key < 0 )
            continue;
        /* Bits 8..9 are the subject mode; only an exact type names a record. */
        if( ((script->lookup_key >> 8) & 0x3) != SS_LOOKUP_TYPE )
            continue;

        trigger = SSVM_LookupKeyTrigger(script->lookup_key);
        verb = mock230_world_engine_claimed_verb(trigger, script->lookup_key >> 10);
        if( !verb )
            continue;

        discharge = discharging_opcode(trigger);
        if( discharge >= 0 && script_calls(script, discharge) )
            continue;

        if( shadowed == 0 )
            fprintf(stderr,
                    "mock230: content binds a trigger over a verb the engine answers itself:\n");
        fprintf(stderr, "  %-34s takes \"%s\" without re-issuing it (%s)\n",
                script->name ? script->name : "?", verb,
                discharge >= 0 ? SSVM_OpcodeName(discharge) : "?");
        shadowed++;
    }

    if( shadowed )
        fprintf(stderr,
                "mock230: %d script(s) shadow an engine verb — check each does the engine's "
                "job or means not to; this is a review list, not an error\n",
                shadowed);
    return shadowed;
}

int
mock230_scripts_fallback(
    struct Mock230Server* srv,
    enum Mock230Fallback which,
    int result)
{
    const char* name;

    if( which < 0 || which >= MOCK230_FALLBACK_COUNT )
        return 0;
    name = k_engine_fallbacks[which].name;

    if( result != MOCK230_TRIGGER_NONE )
    {
        /* RAN needs no word — content did its job. FAILED does: the click is
         * about to do nothing at all, and the reason is a script that aborted
         * several lines above this in the log. Saying so is what stops that
         * being read as an engine bug. */
        if( result == MOCK230_TRIGGER_FAILED )
            fprintf(stderr,
                    "mock230: a bound script failed; the `%s` engine fallback is NOT "
                    "running in its place\n",
                    name);
        return 0;
    }

    if( !srv->scripts_ok )
    {
        if( srv->verbose )
            fprintf(stderr, "mock230: no script pack — `%s` does nothing\n", name);
        return 0;
    }

    /* The name, not the blocker. This fires once per unbound click and the
     * blockers are now paragraphs — that is the price of making each one
     * checkable in one command, and the place to pay it is the boot roll, which
     * prints them once. Repeating one of them per click buries the packet log
     * it is supposed to sit beside. */
    if( srv->verbose )
        fprintf(stderr, "mock230: engine fallback `%s` ran (why it is still C: the boot "
                        "roll, MOCK230_VERBOSE)\n",
                name);
    return 1;
}

/* ------------------------------------------------------------------ */
/* ::commands                                                          */
/* ------------------------------------------------------------------ */

/*
 * A `::command`, dispatched to `[debugproc,<name>]`.
 *
 * This is how the reference writes a cheat: `ClientCheatHandler` splits the
 * line, looks the debugproc up by name, and fills its declared parameters from
 * the words that follow. Everything in its
 * `content/scripts/_test/scripts/cheats/` is one of these, and none of it is in
 * the engine.
 *
 * That is the point rather than a detail. A cheat is a *content* entry point —
 * "toggle this prayer", "give me that item" — and every one written in C is a
 * second implementation of something content already does, drifting from the
 * shipped path exactly where it matters. `::pray` was one: it reached a prayer
 * through a C module that knew the prayer table, so it tested that module and
 * not the button.
 *
 * The argument types come from the script itself. LostCity resolves obj, npc,
 * loc and component names here as well; this resolves whatever the content tree
 * names, through the same packs the compiler used, and takes anything else as an
 * int. A word that does not resolve is -1, which every reasonable script tests
 * for anyway.
 */
static int
debugproc_arg_type(
    uint8_t type,
    enum Mock230PackKind* out_kind)
{
    /* ScriptVarType.getTypeChar, the same codes ssc_symbols.c compiles with. */
    switch( type )
    {
    case 105: /* int */
    case 49:  /* boolean */
        return 0;
    case 115: /* string */
        return 1;
    case 111: /* obj */
    case 79:  /* namedobj */
        *out_kind = MOCK230_PACK_OBJ;
        return 2;
    case 110: /* npc */
        *out_kind = MOCK230_PACK_NPC;
        return 2;
    case 108: /* loc */
        *out_kind = MOCK230_PACK_LOC;
        return 2;
    case 73: /* component */
        *out_kind = MOCK230_PACK_COMPONENT;
        return 2;
    case 97: /* interface */
        *out_kind = MOCK230_PACK_INTERFACE;
        return 2;
    case 118: /* inv */
        *out_kind = MOCK230_PACK_INV;
        return 2;
    case 65: /* seq */
        *out_kind = MOCK230_PACK_SEQ;
        return 2;
    case 116: /* spotanim */
        *out_kind = MOCK230_PACK_SPOTANIM;
        return 2;
    case 83: /* stat */
        *out_kind = MOCK230_PACK_STAT;
        return 2;
    default:
        return 0;
    }
}

int
mock230_scripts_run_debugproc(
    struct Mock230Server* srv,
    const char* line)
{
    const struct SSVM_Script* script;
    char name[192];
    char command[64];
    int32_t argv[SS_MAX_PARAM_TYPES];
    const char* strv[SS_MAX_PARAM_TYPES];
    char words[SS_MAX_PARAM_TYPES][64];
    int argc = 0;
    int strc = 0;
    const char* cursor = line;
    int length = 0;

    if( !srv->scripts_ok )
        return 0;

    while( *cursor && *cursor != ' ' && length + 1 < (int)sizeof(command) )
        command[length++] = *cursor++;
    command[length] = '\0';
    if( length == 0 )
        return 0;

    snprintf(name, sizeof(name), "[debugproc,%s]", command);
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
        return 0;

    for( int i = 0; i < script->param_type_count && i < SS_MAX_PARAM_TYPES; i++ )
    {
        enum Mock230PackKind kind = MOCK230_PACK_COUNT;
        int form = debugproc_arg_type(script->param_types[i], &kind);
        char* word = words[i];
        int taken = 0;

        while( *cursor == ' ' )
            cursor++;
        while( *cursor && *cursor != ' ' && taken + 1 < 64 )
            word[taken++] = *cursor++;
        word[taken] = '\0';

        if( form == 1 )
            strv[strc++] = word;
        else if( form == 2 )
            argv[argc++] = taken ? mock230_content_symbol(kind, word) : -1;
        else
            argv[argc++] = taken ? (int32_t)strtol(word, NULL, 10) : 0;
    }

    if( srv->verbose )
        fprintf(stderr, "mock230: %s with %d int and %d string args\n", name, argc, strc);
    return mock230_scripts_run_proc_sv(srv, name, argv, argc, strv, strc);
}

/* ------------------------------------------------------------------ */
/* Host commands                                                       */
/* ------------------------------------------------------------------ */

/*
 * The active loc, resolved through its scene slot.
 *
 * Returns NULL when the slot no longer holds a live loc — which is a real
 * state, not a bug: a script can suspend between `loc_find` and `loc_change`,
 * and by the time it resumes somebody else may have taken the loc. The callers
 * abort on NULL rather than acting on whatever is in the slot now.
 */
static struct Mock230SceneLoc*
script_active_loc(struct SSVM_State* state)
{
    int slot = (int)((intptr_t)SSVM_ActiveSlot(state, SSVM_ENT_LOC, SSVM_PRIMARY)) - 1;
    struct Mock230SceneLoc* loc;

    if( slot < 0 )
        return NULL;
    loc = mock230_scene_loc(slot);
    if( !loc || !loc->active )
        return NULL;
    return loc;
}

/*
 * The active npc, resolved through its slot rather than a stored pointer.
 *
 * A parked script outlives the tick that started it, and an npc can despawn or
 * have its slot reused while the script waits. `host_tag` carries the slot so a
 * resumed script either finds the same npc or finds none — never a different
 * one wearing the same address.
 */
static struct Mock230Npc*
active_npc(struct SSVM_State* state)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    int slot = (int)state->host_tag - 1;

    if( slot < 0 || slot >= MOCK230_NPC_MAX )
        return NULL;
    if( !srv->npcs[slot].active )
        return NULL;
    return &srv->npcs[slot];
}

/*
 * The container the *active* player means by `inv_id`.
 *
 * The registry (mock230_container.h) is what actually resolves this, and it
 * takes the player as an argument rather than reading `srv->active_player` —
 * because a `scope=shared` container has no player to read. What is left here
 * is the ServerScript adaptor: a `.`-less container op acts on the primary
 * player, which is LostCity's `ScriptState.activePlayer` and is the correct
 * source *for this layer*. The `.` dialect is where a secondary player would
 * come from; it is decoded and discarded today (`(void)dot;` below), which is
 * one of the things standing between here and trading.
 */
static struct Mock230Container*
container_row(
    struct Mock230Server* srv,
    int32_t inv_id)
{
    return mock230_container_resolve(srv, srv->active_player, inv_id);
}

static struct Mock230Item*
container_for(
    struct Mock230Server* srv,
    int32_t inv_id,
    int* out_slots)
{
    struct Mock230Container* row = container_row(srv, inv_id);

    *out_slots = row ? row->slots : 0;
    return row ? row->items : NULL;
}

/** Mark a container for this tick's transmit. Which *kind* of dirty that is —
 *  a per-slot mask or a whole-container flag — is the row's business now, not
 *  a branch here: it is decided from the slot count, because
 *  UPDATE_INV_PARTIAL's mask can only address 32 of them. */
static void
container_dirty(
    struct Mock230Server* srv,
    int32_t inv_id,
    int slot)
{
    mock230_container_mark(container_row(srv, inv_id), slot);
}

/* `push_typed_param` moved to mock230_ops_param.c as
 * `mock230_push_typed_param` (declared in mock230.h). It was `static` here
 * and so unreachable from a per-domain ops file, and the family's whole
 * difficulty is that there must be exactly one of it — see its comment. */

int
mock230_script_command(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    struct Mock230Player* player = srv->active_player;

    (void)dot;

    /* Per-domain handlers first. Each returns 1 when it owns the opcode; see the
     * note on mock230_ops_db in mock230.h for why the split grows this way. */
    if( mock230_ops_db(state, opcode, dot) )
        return 1;
    if( mock230_ops_param(state, opcode, dot) )
        return 1;
    if( mock230_ops_loc(state, opcode, dot) )
        return 1;
    if( mock230_ops_npc(state, opcode, dot) )
        return 1;
    if( mock230_ops_obj(state, opcode, dot) )
        return 1;
    if( mock230_ops_inv(state, opcode, dot) )
        return 1;
    if( mock230_ops_player(state, opcode, dot) )
        return 1;

    switch( opcode )
    {
    /* ---- messaging ------------------------------------------------ */

    case SS_OP_MES:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        mock230_send_message(srv->active_player, text);
        return 1;
    }

    case SS_OP_ERROR:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        fprintf(stderr, "mock230: script error: %s\n", text);
        return 1;
    }

    /* ---- npc ------------------------------------------------------ */

    case SS_OP_NPC_SAY:
    {
        const char* text;
        struct Mock230Npc* npc = active_npc(state);

        if( !SSVM_PopStr(state, &text) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_say with no active npc");
            return 1;
        }
        /*
         * Overhead text does not exist in this client.
         *
         * The NPC_INFO SAY mask decodes fine and reaches World_NpcSetChat — but
         * nothing ever reads that field back, so an npc's speech would render
         * nowhere. Sending the mask anyway would look correct at every level
         * except the one that matters.
         *
         * So npc speech goes to the chatbox as "<name>: <text>", which is
         * visible, keeps npc_say's fire-and-forget semantics (unlike routing it
         * through a modal dialogue), and needs only the speaker's name. Content
         * is unchanged — scripts still write npc_say.
         */
        snprintf(npc->say, sizeof(npc->say), "%s", text);
        {
            char line[192];

            snprintf(line, sizeof(line), "%s: %s", mock230_npcinfo(npc->type)->name, text);
            mock230_send_message(srv->active_player, line);
        }
        /* Facing the player is real and does render, so keep it — by the pid of
         * the player the script is running for, which is the same player the
         * line above was addressed to. */
        if( srv->active_player )
            mock230_npc_face_player(npc, srv->active_player->pid);
        return 1;
    }

    case SS_OP_NPC_COORD:
    {
        struct Mock230Npc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_coord with no active npc");
            return 1;
        }
        SSVM_PushInt(state, coord_pack(npc->level, npc->x, npc->z));
        return 1;
    }

    case SS_OP_NPC_NAME:
    {
        struct Mock230Npc* npc = active_npc(state);

        SSVM_PushStr(state, npc ? mock230_npcinfo(npc->type)->name : "");
        return 1;
    }

    case SS_OP_NPC_TYPE:
    {
        struct Mock230Npc* npc = active_npc(state);

        SSVM_PushInt(state, npc ? npc->type : -1);
        return 1;
    }

    /* ---- player --------------------------------------------------- */

    case SS_OP_COORD:
        SSVM_PushInt(state, coord_pack(player->level, player->x, player->z));
        return 1;

    case SS_OP_WALKTRIGGER:
    {
        int32_t script_id;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        player->walktrigger = (int)script_id;
        return 1;
    }

    case SS_OP_GETWALKTRIGGER:
        SSVM_PushInt(state, player->walktrigger);
        return 1;

    case SS_OP_P_WALK:
    {
        int32_t coord;
        int x;
        int z;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        x = coord_x(coord);
        z = coord_z(coord);
        /* Same-tile destination cancels the route (freeze/stun root). */
        if( x == player->x && z == player->z )
        {
            mock230_world_steps_clear(player);
            player->dest_x = -1;
            player->dest_z = -1;
            player->clear_map_flag = 1;
        }
        else
            mock230_world_walk_to(srv, x, z);
        return 1;
    }

    case SS_OP_P_TELEPORT:
    case SS_OP_P_TELEJUMP:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        {
            int was_level = player->level;

            player->x = coord_x(coord);
            player->z = coord_z(coord);
            player->level = coord_level(coord);
            /* The next PLAYER_INFO has to carry an absolute placement rather
             * than a step direction, and the scene may need re-centring around
             * the new position — both of which the tick handles off
             * place_dirty. */
            player->place_dirty = 1;
            player->waypoint_index = -1;
            /*
             * A plane change is the case place_dirty does not cover. The scene
             * *window* has not moved, so `maybe_rebuild` sees nothing, and the
             * client goes on holding every npc, player and zone from the floor
             * below — which is precisely what a ladder does.
             * `PathingEntity.teleport` is the reference's own seam for this
             * (`if (previousLevel != level)`), and this is the engine half of
             * moving the ladders to content.
             */
            if( player->level != was_level )
                mock230_world_player_level_changed(player);
        }
        return 1;
    }

    /*
     * `map_findsquare(coord, minrange, maxrange, mode)` — a random walkable
     * tile in an annulus around `coord`.
     *
     * The reference's own use is what fixes the shape: an imp picks a tile
     * `map_findsquare(npc_coord, 0, 20, ^map_findsquare_none)` and teleports to
     * it, which is why an imp is never where you left it. Rejection sampling
     * over the box, because the alternative — enumerate every legal tile and
     * pick one — is 1,681 collision reads for a radius of 20 and this runs
     * inside a per-npc timer.
     *
     * `mode` is LostCity MapFindSquareType: 0 lineofwalk, 1 lineofsight,
     * 2 none. LINEOFWALK / LINEOFSIGHT require a clear path from the candidate
     * back to the origin (ServerOps.ts); NONE only needs a standable tile.
     *
     * Failure returns the *source* coord, not -1. The reference's callers
     * assign the result straight into `npc_tele`, so a sentinel would teleport
     * the npc to coordinate -1; standing still is what "no square found" has to
     * look like.
     */
    case SS_OP_MAP_FINDSQUARE:
    {
        int32_t values[4];
        int origin_x;
        int origin_z;
        int level;
        int min_range;
        int max_range;
        int mode;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        level = coord_level(values[0]);
        origin_x = coord_x(values[0]);
        origin_z = coord_z(values[0]);
        min_range = values[1] < 0 ? 0 : values[1];
        max_range = values[2] < min_range ? min_range : values[2];
        mode = values[3];

        {
            int found = values[0];

            for( int attempt = 0; attempt < 64; attempt++ )
            {
                int dx = mock230_random(srv, -max_range, max_range);
                int dz = mock230_random(srv, -max_range, max_range);
                int x = origin_x + dx;
                int z = origin_z + dz;
                int adx = dx < 0 ? -dx : dx;
                int adz = dz < 0 ? -dz : dz;

                if( (adx > adz ? adx : adz) < min_range )
                    continue;
                if( !mock230_scene_contains(x, z) )
                    continue;
                if( mock230_scene_walk_blocked(level, x, z) )
                    continue;
                /* Reachability last — same order as ServerOps.ts (cheap filters
                 * first). Candidate → origin. */
                if( mode == 0 /* LINEOFWALK */ &&
                    !mock230_scene_line_of_walk(level, x, z, origin_x, origin_z, 1, 1, 1,
                                                1, 0) )
                    continue;
                if( mode == 1 /* LINEOFSIGHT */ &&
                    !mock230_scene_line_of_sight(level, x, z, origin_x, origin_z, 1, 1, 1,
                                                 1, 0) )
                    continue;
                found = coord_pack(level, x, z);
                break;
            }
            SSVM_PushInt(state, found);
        }
        return 1;
    }

    /*
     * `npc_getmode` — the standing mode phase 4 is running for this npc.
     *
     * The setter has been here since npc modes landed; the getter had not, and
     * content branches on it (`npc_getmode = opplayer2` gates the imp's
     * teleport sound). Reading a mode the engine cannot report makes the branch
     * always-false, which for a sound is invisible and for a guard is a
     * behaviour that silently never happens.
     */
    case SS_OP_NPC_GETMODE:
    {
        struct Mock230Npc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_getmode with no active npc");
            return 1;
        }
        SSVM_PushInt(state, npc->mode);
        return 1;
    }

    /* ---- coordinates ---------------------------------------------- */

    case SS_OP_COORDX:
    case SS_OP_COORDY:
    case SS_OP_COORDZ:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        /* COORDY is the plane, not the north axis — the same naming trap the
         * world-map port hit. COORDZ is north/south. */
        if( opcode == SS_OP_COORDX )
            SSVM_PushInt(state, coord_x(coord));
        else if( opcode == SS_OP_COORDY )
            SSVM_PushInt(state, coord_level(coord));
        else
            SSVM_PushInt(state, coord_z(coord));
        return 1;
    }

    /*
     * `[command,movecoord](coord $coord, int $x, int $y, int $z)(coord)` —
     * engine.rs2:38. `ServerOps.ts:107` is
     * `packCoord(position.level + y, position.x + x, position.z + z)`: **y is
     * the plane** and z is north/south, the same naming trap COORDY above
     * documents.
     *
     * This added `$z` to the level and `$y` to the north axis until 2026-08-02.
     * Every one of the twelve callers in the tree passes `y = 0` — the shape
     * `movecoord($coord, $dx, 0, $dz)` is idiomatic precisely because the plane
     * rarely moves — so the two wrong terms were `level + $dz` and `z + 0`, i.e.
     * `~move_north($coord, 3)` went up three floors and did not move north.
     * Nothing called those procs yet, which is the only reason it had not been
     * seen. Found by porting the doors, whose `~door_open` is the first caller
     * that ever passes a non-zero z.
     */
    case SS_OP_MOVECOORD:
    {
        int32_t values[4];

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        SSVM_PushInt(state,
                     coord_pack(coord_level(values[0]) + values[2],
                                coord_x(values[0]) + values[1],
                                coord_z(values[0]) + values[3]));
        return 1;
    }

    case SS_OP_DISTANCE:
    {
        int32_t first;
        int32_t second;
        int dx;
        int dz;

        if( !SSVM_PopInt(state, &second) || !SSVM_PopInt(state, &first) )
            return 1;
        dx = coord_x(first) - coord_x(second);
        dz = coord_z(first) - coord_z(second);
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        /* Chebyshev, matching the reference: diagonal movement costs one tile. */
        SSVM_PushInt(state, dx > dz ? dx : dz);
        return 1;
    }

    /* ---- inventory ------------------------------------------------ */

    case SS_OP_INV_ADD:
    {
        int32_t values[3];
        struct Mock230Container* row;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        row = container_row(srv, values[0]);
        if( !row )
        {
            SSVM_Abort(state, "inv_add on unknown container %d", values[0]);
            return 1;
        }
        /*
         * `mock230_container_add` is the whole body now — it merges stacks and
         * spreads unstackables, which this arm did neither of, and it marks the
         * slots it writes through the registry (the inline version this
         * replaced read "backpack, else worn", so a write to the bank marked a
         * *worn* slot and a bank slot past 31 shifted a 32-bit mask by its own
         * width). `assure_full` is off, matching the reference's INV_ADD.
         *
         * What is still not the reference: the overflow. `InvOps.ts:73` puts
         * whatever did not fit on the floor at the player's feet; this says so
         * instead, which is a third different answer from the one
         * `interaction_engine_obj` gives ("inv_no_space_message"). Noted rather
         * than fixed — the floor-drop belongs with the `opheld` row's work.
         */
        if( mock230_container_add(row, values[1], values[2], 0) < values[2] )
            mock230_say(srv, "inv_full_message", NULL);
        return 1;
    }

    case SS_OP_INV_DEL:
    {
        int32_t values[3];
        int slots = 0;
        struct Mock230Item* items;
        int remaining;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        items = container_for(srv, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_del on unknown container %d", values[0]);
            return 1;
        }
        remaining = values[2];
        for( int i = 0; i < slots && remaining > 0; i++ )
        {
            if( items[i].obj_id != values[1] )
                continue;
            if( items[i].count > remaining )
            {
                items[i].count -= remaining;
                remaining = 0;
            }
            else
            {
                remaining -= items[i].count;
                items[i].obj_id = -1;
                items[i].count = 0;
            }
            /*
             * Through the registry. What was here read "backpack, else worn",
             * so `inv_del(bank, …)` marked a *worn* slot — a wrong appearance
             * push, and the bank never re-transmitted — and for a bank slot
             * past 31, `1u << i` shifted a 32-bit mask by more than its width
             * (undefined; `i` reaches 1409). Same defect as the one inv_add's
             * comment already records; this is the second copy of it.
             */
            container_dirty(srv, values[0], i);
        }
        return 1;
    }

    /*
     * inv_delslot empties one cell, whatever is in it.
     *
     * Not a convenience over inv_del: they answer different questions. A script
     * that ate the item the player clicked has to remove *that* stack, and
     * inv_del removes the first matching one — which is a different cell as
     * soon as two slots hold the same obj. The reference's consume path uses
     * delslot for exactly this reason and so does the port.
     */
    case SS_OP_INV_DELSLOT:
    {
        int32_t values[2];
        int slots = 0;
        struct Mock230Item* items;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        items = container_for(srv, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_delslot on unknown container %d", values[0]);
            return 1;
        }
        if( values[1] < 0 || values[1] >= slots )
            return 1;
        items[values[1]].obj_id = -1;
        items[values[1]].count = 0;
        /* Through the registry, for the reason inv_del's comment gives. */
        container_dirty(srv, values[0], (int)values[1]);
        return 1;
    }

    case SS_OP_INV_TOTAL:
    {
        int32_t values[2];
        int slots = 0;
        struct Mock230Item* items;
        int total = 0;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        items = container_for(srv, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_total on unknown container %d", values[0]);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            if( items[i].obj_id == values[1] )
                total += items[i].count;
        }
        SSVM_PushInt(state, total);
        return 1;
    }

    case SS_OP_INV_TOTALCAT:
    {
        int32_t values[2];
        int slots = 0;
        struct Mock230Item* items;
        int total = 0;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /*
         * Category 0 is the decoder's "no category stated", so it matches every
         * uncategorised obj in the game. pack/category.pack says content must
         * not bind to it; counting it here would be the same mistake with a
         * quieter symptom, so refuse instead.
         */
        if( values[1] <= 0 )
        {
            SSVM_Abort(state, "inv_totalcat with category %d (0 means unset)", values[1]);
            return 1;
        }
        items = container_for(srv, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_totalcat on unknown container %d", values[0]);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            if( items[i].obj_id < 0 )
                continue;
            if( mock230_objinfo(items[i].obj_id)->category == values[1] )
                total += items[i].count;
        }
        SSVM_PushInt(state, total);
        return 1;
    }

    case SS_OP_INV_FREESPACE:
    {
        int32_t inv_id;
        int slots = 0;
        struct Mock230Item* items;
        int free_slots = 0;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, inv_id, &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_freespace on unknown container %d", inv_id);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            if( items[i].obj_id < 0 )
                free_slots++;
        }
        SSVM_PushInt(state, free_slots);
        return 1;
    }

    /* ---- variables ------------------------------------------------ */

    case SS_OP_PUSH_VARP:
    {
        int varp = state->script->int_operands[state->pc] & 0xffff;

        if( varp < 0 || varp >= MOCK230_VARP_COUNT )
        {
            SSVM_Abort(state, "varp %d is outside the mock's range", varp);
            return 1;
        }
        SSVM_PushInt(state, player->varps[varp]);
        return 1;
    }

    case SS_OP_POP_VARP:
    {
        int varp = state->script->int_operands[state->pc] & 0xffff;
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        if( varp < 0 || varp >= MOCK230_VARP_COUNT )
        {
            SSVM_Abort(state, "varp %d is outside the mock's range", varp);
            return 1;
        }
        /*
         * Assignment always marks the varp for transmission, even when the
         * value is unchanged.
         *
         * That is the reference's semantics and it is load-bearing:
         * LostCity's content contains `%option_nodef = %option_nodef;` with the
         * comment "resync varp", which only means anything if a write to an
         * equal value still reaches the client. It is also what makes an
         * opening state work at all — [login] setting `%com_mode = 0` on a varp
         * that is already 0 has to *tell* the client 0, because the client has
         * never been told anything.
         *
         * `mock230_world_mark_varp` is idempotent within a tick, so a script
         * writing the same varp repeatedly still produces one packet.
         */
        player->varps[varp] = value;
        mock230_world_mark_varp(player, varp);
        /* And whatever engine state hangs off this varp. Writing the array and
         * marking it for transmission is only *reporting* the change; a varp
         * like `option_run` is where a piece of engine state actually lives,
         * and skipping this is how the run orb came to light up while the
         * player kept walking. */
        mock230_world_varp_written(srv, varp, value);
        return 1;
    }

    /* ---- config --------------------------------------------------- */

    case SS_OP_OC_NAME:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushStr(state, mock230_objinfo(obj_id)->name);
        return 1;
    }

    /*
     * The examine text, and the whole of what a rev-230 "Examine" op is.
     *
     * Op 10 is Examine on nearly every panel the client draws — the backpack,
     * the worn tab, the bank, the Tool Leprechaun's twelve cells — and it had
     * no server-side answer at all, because the obj record's `examine` string
     * was decoded by the cache and dropped by mock230_objinfo. Reading it is
     * mechanism, not policy: the sentence is the cache's, content decides
     * whether and when to print it.
     *
     * The empty string rather than NULL for a record that states none — a
     * placeholder, or an id nothing occupies — so a script that prints the
     * result unconditionally prints a blank line instead of dereferencing one.
     */
    case SS_OP_OC_DESC:
    {
        int32_t obj_id;
        const char* desc;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        desc = mock230_objinfo(obj_id)->desc;
        SSVM_PushStr(state, desc ? desc : "");
        return 1;
    }

    /*
     * Config queries.
     *
     * Every one of these is a read off a table the boot loaders already decoded
     * — `Mock230ObjInfo`, `Mock230NpcInfo`, the content packs — which is the
     * reason this batch is safe to add in bulk. `nc_desc` is still absent —
     * a dat2 npc record has no description at all (it is server-driven at this
     * revision). `oc_cost` / `oc_members` / `oc_tradeable` / `oc_desc` land in
     * mock230_ops_obj.c / the cases above once their fields are kept at load.
     *
     * An opcode that cannot be answered from real data is better left to the
     * VM's loud stub than implemented with a plausible guess: the stub says so,
     * and a guess does not.
     *
     * `oc_param` used to be in that list — `runtime_typed`, "and no decoder here
     * keeps a general per-record param table to answer that from". Both halves
     * of that now exist (`mock230_obj_param`, `mock230_content_param_type`), so
     * it is implemented below, and `nc_param` with it over `mock230_npc_param`.
     * `lc_param` / `struct_param` are the same shape over the loc and struct
     * tables, and are left out of this change rather than done badly at speed:
     * neither table is decoded at runtime at all, where the npc one already was
     * — `read_combat_params` had been walking these very rows and discarding
     * all but fourteen keys.
     */
    case SS_OP_NPC_SETMODE:
    {
        int32_t mode;
        struct Mock230Npc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &mode) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_setmode with no active npc");
            return 1;
        }
        /*
         * The mode is stored, not acted on: phase 4 runs it every tick, which
         * is what makes it a *standing state* rather than a one-shot. That
         * distinction is the whole reason `npc_setmode` is in front of 122
         * LostCity files and could not be faked with a one-shot face or walk.
         *
         * `none` and `null` both mean stop, and 162 of the tree's 253 calls are
         * one of the two.
         */
        npc->mode = mode;
        if( mode == MOCK230_NPCMODE_NONE || mode == MOCK230_NPCMODE_NULL )
            npc->step_dir = -1;
        return 1;
    }

    case SS_OP_NPC_QUEUE:
    {
        int32_t queue;
        int32_t arg;
        int32_t delay;
        struct Mock230Npc* npc = active_npc(state);

        /* `npc_queue(2, $damage, $delay)` — queue number, argument, delay. */
        if( !SSVM_PopInt(state, &delay) )
            return 1;
        if( !SSVM_PopInt(state, &arg) )
            return 1;
        if( !SSVM_PopInt(state, &queue) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_queue with no active npc");
            return 1;
        }
        if( queue < 1 || queue > 20 )
        {
            SSVM_Abort(state, "npc_queue %d is outside [ai_queue1..20]", queue);
            return 1;
        }
        for( int i = 0; i < MOCK230_NPC_QUEUE_MAX; i++ )
        {
            if( npc->queue[i].active )
                continue;
            npc->queue[i].active = 1;
            npc->queue[i].queue = queue;
            /*
             * The raw delay, and the drain compares the value *after* its
             * decrement — `Npc.processQueue`: `request.delay--; if (!delayed &&
             * request.delay <= 0)`. A player's queue compares the value before
             * it, so an npc's delay 0 and delay 1 both land on the next npc
             * phase and a player's do not.
             *
             * This stored `delay + 1` and the selftest asserted that
             * `npc_queue(q, arg, 1)` fires on tick +2. It fires on +1; the
             * assertion encoded the wrong convention and was rewritten.
             */
            npc->queue[i].delay = delay;
            npc->queue[i].arg = arg;
            return 1;
        }
        SSVM_Abort(state, "npc %d's queue is full", npc->type);
        return 1;
    }

    case SS_OP_NPC_SETTIMER:
    {
        int32_t interval;
        struct Mock230Npc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &interval) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_settimer with no active npc");
            return 1;
        }
        /*
         * 0 stops the timer, which content relies on — `npc_settimer(0)` is how
         * a behaviour says "not until the action is complete". The trigger is
         * not stored: `[ai_timer,<npc>]` is resolved when it fires, so an npc
         * whose type changes picks up the new type's timer script.
         */
        npc->timer_interval = interval > 0 ? interval : 0;
        npc->timer_clock = 0;
        return 1;
    }

    /* ---- players by uid, logging, gendered text --------------------- */

    case SS_OP_FINDUID:
    case SS_OP_P_FINDUID:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        /*
         * `uid` is 1 for the one player, matching `SS_OP_UID` above. Content
         * uses this to re-acquire a player it stashed in a varp — the shape is
         * `if (p_finduid(%npc_aggressive_player) = true) { ... }` — so a uid
         * that no longer names anybody has to return false rather than abort.
         * That is the whole point of the call: it is content asking whether the
         * player it remembers is still here.
         *
         * `p_finduid` differs from `finduid` by granting *protected* access, so
         * the ops that follow it may write the player. The reference draws the
         * same distinction and the VM already enforces it through the meta
         * table's require bits.
         */
        if( uid != 1 || !srv->active_player )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_PLAYER);
        if( opcode == SS_OP_P_FINDUID )
            SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_SESSION_LOG:
    {
        const char* text = NULL;
        int32_t level;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        if( !SSVM_PopInt(state, &level) )
            return 1;
        /*
         * The reference writes these to a per-player adventure log the client
         * can read back. There is no such log here, and inventing a file format
         * for one is a lot of machinery for a feature nothing displays — so it
         * goes to stderr under MOCK230_VERBOSE, which is where every other
         * "what did content just do" line goes.
         *
         * Implemented rather than left to the loud stub because it is in front
         * of 73 LostCity files (docs/LOSTCITY_PORT_TRIAGE.md §10.5) and every
         * one of them is a quest that otherwise runs. A log line that goes
         * nowhere costs nothing; a quest that aborts on its last statement costs
         * the quest.
         */
        if( srv->verbose )
            fprintf(stderr, "mock230: session_log(%d) %s\n", level, text ? text : "");
        return 1;
    }

    case SS_OP_GENDER:
        SSVM_PushInt(state, srv->active_player ? srv->active_player->gender : 0);
        return 1;

    case SS_OP_TEXT_GENDER:
    {
        const char* female = NULL;
        const char* male = NULL;

        /* Popped in reverse: `text_gender("sir", "lady")` pushes male first. */
        if( !SSVM_PopStr(state, &female) )
            return 1;
        if( !SSVM_PopStr(state, &male) )
            return 1;
        SSVM_PushStr(state,
                     (srv->active_player && srv->active_player->gender) ? (female ? female : "")
                                                          : (male ? male : ""));
        return 1;
    }

    /* ---- find-all iterators ---------------------------------------- */

    /*
     * `npc_findallany($coord, $distance, $checkvis)` then
     * `while (npc_findnext = true) { ... npc_type ... }` is the shape all three
     * of these have, and the loop body reads the *active* entity — so
     * `*_findnext` has to set it, not merely return an id. Getting that wrong
     * gives a loop that runs the right number of times over the wrong entity.
     */
    case SS_OP_NPC_FINDALL:
    case SS_OP_NPC_FINDALLANY:
    {
        int32_t coord;
        int32_t npc_type = -1;
        int32_t distance;
        int32_t checkvis;

        if( !SSVM_PopInt(state, &checkvis) )
            return 1;
        if( !SSVM_PopInt(state, &distance) )
            return 1;
        if( opcode == SS_OP_NPC_FINDALL && !SSVM_PopInt(state, &npc_type) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        srv->iterator.count = 0;
        srv->iterator.cursor = 0;
        srv->iterator.kind = SSVM_ENT_NPC;
        for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
        {
            struct Mock230Npc* npc = &srv->npcs[slot];
            int dx;
            int dz;

            if( !npc->active || npc->level != coord_level(coord) )
                continue;
            if( opcode == SS_OP_NPC_FINDALL && npc->type != npc_type )
                continue;
            dx = npc->x - coord_x(coord);
            dz = npc->z - coord_z(coord);
            if( dx < 0 )
                dx = -dx;
            if( dz < 0 )
                dz = -dz;
            if( (dx > dz ? dx : dz) > distance )
                continue;
            if( !mock230_scene_checkvis(checkvis, npc->level, coord_x(coord),
                                       coord_z(coord), npc->x, npc->z) )
                continue;
            if( srv->iterator.count <
                (int)(sizeof(srv->iterator.slots) / sizeof(srv->iterator.slots[0])) )
                srv->iterator.slots[srv->iterator.count++] = slot;
        }
        return 1;
    }

    case SS_OP_NPC_FINDNEXT:
    {
        if( srv->iterator.kind != SSVM_ENT_NPC )
        {
            SSVM_Abort(state, "npc_findnext without a preceding npc_findall");
            return 1;
        }
        while( srv->iterator.cursor < srv->iterator.count )
        {
            int slot = srv->iterator.slots[srv->iterator.cursor++];

            /* Re-checked, because the list was built before the loop body ran
             * and the body may have killed one of them. */
            if( !srv->npcs[slot].active )
                continue;
            SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
            state->host_tag = slot + 1;
            SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
            SSVM_PushInt(state, 1);
            return 1;
        }
        SSVM_PushInt(state, 0);
        return 1;
    }

    case SS_OP_LOC_FINDALLZONE:
    {
        int32_t coord;
        int zone_x;
        int zone_z;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        /* A zone is 8x8, and the coord names any tile in it. */
        zone_x = coord_x(coord) & ~7;
        zone_z = coord_z(coord) & ~7;

        srv->iterator.count = 0;
        srv->iterator.cursor = 0;
        srv->iterator.kind = SSVM_ENT_LOC;
        for( int slot = 0;; slot++ )
        {
            struct Mock230SceneLoc* loc = mock230_scene_loc(slot);

            if( !loc )
                break;
            if( !loc->active || loc->level != coord_level(coord) )
                continue;
            if( loc->x < zone_x || loc->x >= zone_x + 8 || loc->z < zone_z ||
                loc->z >= zone_z + 8 )
                continue;
            if( srv->iterator.count <
                (int)(sizeof(srv->iterator.slots) / sizeof(srv->iterator.slots[0])) )
                srv->iterator.slots[srv->iterator.count++] = slot;
        }
        return 1;
    }

    case SS_OP_LOC_FINDNEXT:
    {
        if( srv->iterator.kind != SSVM_ENT_LOC )
        {
            SSVM_Abort(state, "loc_findnext without a preceding loc_findallzone");
            return 1;
        }
        while( srv->iterator.cursor < srv->iterator.count )
        {
            int slot = srv->iterator.slots[srv->iterator.cursor++];
            struct Mock230SceneLoc* loc = mock230_scene_loc(slot);

            /* A `loc_del` in the loop body frees the slot; skip it rather than
             * handing the body a loc that is no longer there. */
            if( !loc || !loc->active )
                continue;
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
            SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_LOC);
            SSVM_PushInt(state, 1);
            return 1;
        }
        SSVM_PushInt(state, 0);
        return 1;
    }

    case SS_OP_HUNTALL:
    {
        int32_t coord;
        int32_t distance;
        int32_t checkvis;
        int dx;
        int dz;

        if( !SSVM_PopInt(state, &checkvis) )
            return 1;
        if( !SSVM_PopInt(state, &distance) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        /*
         * `huntall` collects *players* in range — `[proc,sound_area]` uses it to
         * play a sound to everyone who can hear it. `MOCK230_PLAYER_MAX` is 1
         * (§6.1), so this finds at most one, and it is written as a loop over
         * the pool anyway: the day a second player exists this is already
         * right, where a hardcoded `srv->active_player` would be one more place to find.
         *
         * HuntVis for players casts candidate→source (ScriptIterators.ts), the
         * opposite of the npc finds.
         */
        srv->iterator.count = 0;
        srv->iterator.cursor = 0;
        srv->iterator.kind = SSVM_ENT_PLAYER;
        for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
        {
            struct Mock230Player* other = &srv->players[i];

            /* `player_count` is how many of the pool are live; the mock never
             * leaves a hole, so the first `player_count` entries are the world's
             * players. */
            if( i >= srv->player_count || other->level != coord_level(coord) )
                continue;
            dx = other->x - coord_x(coord);
            dz = other->z - coord_z(coord);
            if( dx < 0 )
                dx = -dx;
            if( dz < 0 )
                dz = -dz;
            if( (dx > dz ? dx : dz) > distance )
                continue;
            if( !mock230_scene_checkvis(checkvis, other->level, other->x, other->z,
                                       coord_x(coord), coord_z(coord)) )
                continue;
            srv->iterator.slots[srv->iterator.count++] = i;
        }
        return 1;
    }

    case SS_OP_HUNTNEXT:
    {
        if( srv->iterator.kind != SSVM_ENT_PLAYER )
        {
            SSVM_Abort(state, "huntnext without a preceding huntall");
            return 1;
        }
        while( srv->iterator.cursor < srv->iterator.count )
        {
            int index = srv->iterator.slots[srv->iterator.cursor++];
            struct Mock230Player* other = &srv->players[index];

            if( index >= srv->player_count )
                continue;
            SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, other);
            SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_PLAYER);
            SSVM_PushInt(state, 1);
            return 1;
        }
        SSVM_PushInt(state, 0);
        return 1;
    }

    /* ---- npcs: addressing, lifecycle and reads --------------------- */

    /*
     * `npc_find` and friends set the active npc, which every `npc_*` opcode
     * with `require = 0x010` then acts on. The slot rides in `host_tag` (+1, so
     * zero means none) for the same reason the loc's does: a script can suspend
     * between finding an npc and acting on it, and a slot either still names
     * the same npc or names none — never a different one wearing the same
     * address.
     */
    case SS_OP_NPC_FIND:
    case SS_OP_NPC_FINDEXACT:
    {
        int32_t coord;
        int32_t npc_type;
        int32_t distance = 0;
        int32_t checkvis = 0;
        int best = -1;
        int best_range = 0;

        if( opcode == SS_OP_NPC_FIND )
        {
            if( !SSVM_PopInt(state, &checkvis) )
                return 1;
            if( !SSVM_PopInt(state, &distance) )
                return 1;
        }
        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;
        /*
         * `checkvis` is LostCity HuntVis (0 off / 1 lineofsight / 2 lineofwalk).
         * Content uses it to avoid addressing an npc through a wall.
         */

        for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
        {
            struct Mock230Npc* npc = &srv->npcs[slot];
            int dx;
            int dz;
            int range;

            if( !npc->active || npc->type != npc_type )
                continue;
            if( npc->level != coord_level(coord) )
                continue;
            dx = npc->x - coord_x(coord);
            dz = npc->z - coord_z(coord);
            if( dx < 0 )
                dx = -dx;
            if( dz < 0 )
                dz = -dz;
            range = dx > dz ? dx : dz;
            if( opcode == SS_OP_NPC_FINDEXACT )
            {
                if( range != 0 )
                    continue;
            }
            else if( range > distance )
            {
                continue;
            }
            if( !mock230_scene_checkvis(checkvis, npc->level, coord_x(coord),
                                       coord_z(coord), npc->x, npc->z) )
                continue;
            /* Nearest wins. The reference does the same, and it is what makes
             * `npc_find(coord, guard, 5, 0)` mean "the guard beside me" rather
             * than "whichever guard the slot array happened to reach first". */
            if( best < 0 || range < best_range )
            {
                best = slot;
                best_range = range;
            }
        }

        if( best < 0 )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[best]);
        state->host_tag = best + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
        SSVM_PushInt(state, 1);
        return 1;
    }

    /*
     * The active npc's uid, which is its slot.
     *
     * The mirror of NPC_FINDUID below, and it carries the same caveat: the
     * reference packs a generation counter beside the index so a uid that
     * outlives its npc fails to resolve, and this server does not. Content
     * holding one across a despawn gets whatever took the slot.
     */
    case SS_OP_NPC_UID:
    {
        int slot = (int)state->host_tag - 1;

        if( slot < 0 )
        {
            SSVM_Abort(state, "npc_uid with no active npc");
            return 1;
        }
        SSVM_PushInt(state, slot);
        return 1;
    }

    case SS_OP_NPC_FINDUID:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        /*
         * An npc uid is its slot here. The reference packs a generation counter
         * beside the index so a reused slot fails the lookup; this server has
         * no such counter, so a uid that outlives its npc resolves to whatever
         * took the slot. Content holding a uid across a despawn is rare and the
         * honest fix is the counter, not a guess — this is the one place the
         * difference is visible, so it is stated here.
         */
        if( uid < 0 || uid >= MOCK230_NPC_MAX || !srv->npcs[uid].active )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[uid]);
        state->host_tag = uid + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_NPC_ADD:
    {
        int32_t coord;
        int32_t npc_type;
        int32_t duration;
        int slot;

        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        slot = mock230_world_npc_spawn(srv, npc_type, coord_x(coord), coord_z(coord),
                                       coord_level(coord));
        if( slot < 0 )
        {
            SSVM_Abort(state, "npc_add %d at %d,%d found no free slot", npc_type,
                       coord_x(coord), coord_z(coord));
            return 1;
        }
        /* 0 is "stays until something removes it", matching the reference and
         * matching every npc the map squares spawn. */
        srv->npcs[slot].despawn_tick = duration > 0 ? srv->tick + duration : -1;
        /* Left active, so the script can act on what it just made. */
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
        state->host_tag = slot + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
        return 1;
    }

    case SS_OP_NPC_DEL:
    {
        struct Mock230Npc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_del with no active npc");
            return 1;
        }
        /* The ordinary NPC_INFO remove path: clearing `active` is what the
         * encoder reads, exactly as a death does. */
        npc->active = 0;
        return 1;
    }

    case SS_OP_NPC_TELE:
    {
        int32_t coord;
        struct Mock230Npc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_tele with no active npc");
            return 1;
        }
        /* The chokepoint, not three assignments: this used to move the npc
         * without moving its collision stamp or telling the clients, so the
         * imp left a blocked tile behind it and every observer kept drawing it
         * at the tile it teleported out of. */
        mock230_world_npc_teleport(npc, coord_x(coord), coord_z(coord), coord_level(coord));
        return 1;
    }

    case SS_OP_NPC_RANGE:
    {
        int32_t coord;
        struct Mock230Npc* npc = active_npc(state);
        int dx;
        int dz;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_range with no active npc");
            return 1;
        }
        if( npc->level != coord_level(coord) )
        {
            /* Different planes are not "far", they are unreachable. The
             * reference returns a large number rather than a real distance and
             * content tests `> n`, so anything big is correct; this is the
             * scene's own diagonal, which cannot be a real range. */
            SSVM_PushInt(state, 0x7fffffff);
            return 1;
        }
        dx = npc->x - coord_x(coord);
        dz = npc->z - coord_z(coord);
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        SSVM_PushInt(state, dx > dz ? dx : dz);
        return 1;
    }

    case SS_OP_NPC_STAT:
    case SS_OP_NPC_BASESTAT:
    {
        int32_t stat;
        struct Mock230Npc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_stat with no active npc");
            return 1;
        }
        /*
         * Hitpoints is the only npc stat that *moves*, so it is the only one
         * where base and current differ — everything else is the content
         * block's authored level either way. That is not a simplification: a
         * rev-230 npc record carries no levels at all (§3.12), so the config is
         * the sole source and nothing drains it.
         */
        if( stat == MOCK230_STAT_HITPOINTS )
        {
            SSVM_PushInt(state, opcode == SS_OP_NPC_STAT ? npc->hitpoints
                                                         : npc->base_hitpoints);
            return 1;
        }
        if( stat == MOCK230_STAT_ATTACK )
            SSVM_PushInt(state, npc->def->attack);
        else if( stat == MOCK230_STAT_STRENGTH )
            SSVM_PushInt(state, npc->def->strength);
        else if( stat == MOCK230_STAT_DEFENCE )
            SSVM_PushInt(state, npc->def->defence);
        else if( stat == MOCK230_STAT_RANGED )
            SSVM_PushInt(state, npc->def->ranged);
        else if( stat == MOCK230_STAT_MAGIC )
            SSVM_PushInt(state, npc->def->magic);
        else
            SSVM_PushInt(state, 0);
        return 1;
    }

    case SS_OP_NPC_STATHEAL:
    {
        int32_t values[3];
        struct Mock230Npc* npc = active_npc(state);
        int healed;
        int i;

        /* Call is npc_statheal(stat, constant, percent) — pop into values[0..2]. */
        for( i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !npc )
        {
            SSVM_Abort(state, "npc_statheal with no active npc");
            return 1;
        }
        /*
         * Hitpoints is the only npc stat that moves (see NPC_STAT above). Heal
         * that; other stats are content-block constants with nothing to write.
         * Formula matches NpcOps.ts NPC_STATHEAL.
         */
        if( values[0] != MOCK230_STAT_HITPOINTS )
            return 1;
        healed = npc->hitpoints + (int)(values[1] + (npc->base_hitpoints * values[2]) / 100);
        if( healed > npc->base_hitpoints )
            healed = npc->base_hitpoints;
        if( healed < 0 )
            healed = 0;
        npc->hitpoints = healed;
        return 1;
    }

    /* ---- locs ------------------------------------------------------ */

    /*
     * The active loc is held by *scene slot*, not by pointer.
     *
     * Same reason the active npc is (see `host_tag`): a script can suspend
     * between `loc_find` and `loc_change`, and a scene rebuild reallocates the
     * loc array underneath it. A stored pointer would dangle; a slot either
     * still names the same loc or names one that has changed, and the opcodes
     * below re-read it every time.
     *
     * The slot rides in the VM's active-entity pointer as `slot + 1`, so a
     * non-NULL pointer means "a loc is active" and zero means none — the same
     * +1 convention `host_tag` uses, and it satisfies the VM's own
     * `SSVM_PTR_ACTIVE_LOC` requirement check without a second field.
     */
    case SS_OP_LOC_FIND:
    {
        int32_t coord;
        int32_t loc_id;
        int slot;

        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        slot = mock230_scene_find_loc(coord_x(coord), coord_z(coord), coord_level(coord),
                                      loc_id);
        if( slot < 0 )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_LOC);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_LOC_COORD:
    case SS_OP_LOC_TYPE:
    case SS_OP_LOC_ANGLE:
    case SS_OP_LOC_SHAPE:
    {
        struct Mock230SceneLoc* loc = script_active_loc(state);

        if( !loc )
        {
            SSVM_Abort(state, "the active loc is gone");
            return 1;
        }
        if( opcode == SS_OP_LOC_COORD )
            SSVM_PushInt(state, coord_pack(loc->level, loc->x, loc->z));
        else if( opcode == SS_OP_LOC_TYPE )
            SSVM_PushInt(state, loc->loc_id);
        else if( opcode == SS_OP_LOC_ANGLE )
            SSVM_PushInt(state, loc->angle);
        else
            SSVM_PushInt(state, loc->shape);
        return 1;
    }

    case SS_OP_LOC_CHANGE:
    {
        int32_t loc_id;
        int32_t duration;
        struct Mock230SceneLoc* loc = script_active_loc(state);
        int was_id;
        int shape;
        int angle;
        int x;
        int z;
        int level;

        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !loc )
        {
            SSVM_Abort(state, "loc_change with no active loc");
            return 1;
        }
        was_id = loc->loc_id;
        shape = loc->shape;
        angle = loc->angle;
        x = loc->x;
        z = loc->z;
        level = loc->level;

        if( !mock230_world_loc_set(srv, x, z, level, shape, loc_id, angle) )
        {
            SSVM_Abort(state, "loc_change to %d, which is not in the cache", loc_id);
            return 1;
        }
        mock230_world_loc_revert_queue(srv, duration, was_id, shape, angle, x, z, level);
        return 1;
    }

    case SS_OP_LOC_ANIM:
    {
        int32_t seq_id;
        struct Mock230SceneLoc* loc = script_active_loc(state);

        if( !SSVM_PopInt(state, &seq_id) )
            return 1;
        if( !loc )
        {
            SSVM_Abort(state, "loc_anim with no active loc");
            return 1;
        }
        mock230_zone_loc_anim(srv, loc->x, loc->z, loc->level, loc->shape, loc->angle,
                              (int)seq_id);
        return 1;
    }

    case SS_OP_LOC_DEL:
    {
        int32_t duration;
        struct Mock230SceneLoc* loc = script_active_loc(state);
        int was_id;
        int shape;
        int angle;
        int x;
        int z;
        int level;

        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !loc )
        {
            SSVM_Abort(state, "loc_del with no active loc");
            return 1;
        }
        was_id = loc->loc_id;
        shape = loc->shape;
        angle = loc->angle;
        x = loc->x;
        z = loc->z;
        level = loc->level;

        if( !mock230_world_loc_set(srv, x, z, level, shape, -1, angle) )
        {
            SSVM_Abort(state, "loc_del on a loc that is already gone");
            return 1;
        }
        mock230_world_loc_revert_queue(srv, duration, was_id, shape, angle, x, z, level);
        return 1;
    }

    /*
     * `[command,loc_add](coord $coord, loc $loc, int $angle, locshape $shape,
     * int $duration)` — engine.rs2:657, and `LocOps.ts:19` destructures exactly
     * that order: `const [coord, type, angle, shape, duration] = popInts(5)`.
     *
     * **angle before shape.** This popped them the other way round until
     * 2026-08-02, i.e. it implemented `loc_add($coord, $loc, $shape, $angle,
     * $duration)`. `ss_meta.gen.h` carries arity and stack class, not argument
     * order, so nothing could catch it at the call — and the two selftest
     * callers were written against the transposed order, one of them with
     * `10, 0` where 10 is a *shape* (`centrepiece_straight`) in an argument the
     * reference calls the angle. Symmetric values hide this completely, which is
     * the same failure `docs/` records for POP_ARRAY_INT: the door port is what
     * exposed it, because a swinging door is the first caller whose angle and
     * shape genuinely differ.
     */
    case SS_OP_LOC_ADD:
    {
        int32_t coord;
        int32_t loc_id;
        int32_t shape;
        int32_t angle;
        int32_t duration;
        int slot;

        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !SSVM_PopInt(state, &shape) )
            return 1;
        if( !SSVM_PopInt(state, &angle) )
            return 1;
        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        if( !mock230_world_loc_set(srv, coord_x(coord), coord_z(coord), coord_level(coord),
                                   shape, loc_id, angle) )
        {
            SSVM_Abort(state, "loc_add %d at %d,%d failed — unknown loc or outside the scene",
                       loc_id, coord_x(coord), coord_z(coord));
            return 1;
        }
        slot = mock230_scene_find_loc_exact(coord_x(coord), coord_z(coord),
                                            coord_level(coord), shape);
        /* -1 says "remove it again" rather than "put something back". */
        mock230_world_loc_revert_queue(srv, duration, -1, shape, angle, coord_x(coord),
                                       coord_z(coord), coord_level(coord));
        /* The reference leaves the added loc active, so the next `loc_change`
         * or `loc_del` in the same script addresses it without a `loc_find`. */
        SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_LOC);
        return 1;
    }

    case SS_OP_OC_PARAM:
    {
        int32_t obj_id;
        int32_t param_id;
        const struct Mock230ObjParam* row;

        if( !SSVM_PopInt(state, &param_id) )
            return 1;
        if( !SSVM_PopInt(state, &obj_id) )
            return 1;

        row = mock230_obj_param(obj_id, param_id);
        mock230_push_typed_param(state, param_id, row ? row->sval : NULL,
                                 row ? row->ival : 0, row != NULL, "obj", obj_id);
        return 1;
    }

    case SS_OP_NC_PARAM:
    {
        int32_t npc_id;
        int32_t param_id;
        const struct Mock230NpcParam* row;

        if( !SSVM_PopInt(state, &param_id) )
            return 1;
        if( !SSVM_PopInt(state, &npc_id) )
            return 1;

        row = mock230_npc_param(npc_id, param_id);
        mock230_push_typed_param(state, param_id, row ? row->sval : NULL,
                                 row ? row->ival : 0, row != NULL, "npc", npc_id);
        return 1;
    }

    case SS_OP_OC_DEBUGNAME:
    {
        int32_t obj_id;
        const char* symbol;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        /* The *content* name (`bronze_scimitar`), not the display name
         * ("Bronze scimitar"). That is what makes it a debug name: it is the
         * symbol a script would have written. */
        symbol = mock230_content_symbol_name(MOCK230_PACK_OBJ, obj_id);
        SSVM_PushStr(state, symbol ? symbol : "null");
        return 1;
    }

    case SS_OP_INV_DEBUGNAME:
    {
        int32_t inv_id;
        const char* symbol;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        symbol = mock230_content_symbol_name(MOCK230_PACK_INV, inv_id);
        SSVM_PushStr(state, symbol ? symbol : "null");
        return 1;
    }

    case SS_OP_NC_NAME:
    {
        int32_t npc_type;

        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        SSVM_PushStr(state, mock230_npcinfo(npc_type)->name);
        return 1;
    }

    case SS_OP_NC_DEBUGNAME:
    {
        int32_t npc_type;
        const char* symbol;

        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        symbol = mock230_content_symbol_name(MOCK230_PACK_NPC, npc_type);
        SSVM_PushStr(state, symbol ? symbol : "null");
        return 1;
    }

    case SS_OP_NC_OP:
    {
        int32_t npc_type;
        int32_t op_num;
        const struct Mock230NpcInfo* info;

        if( !SSVM_PopInt(state, &op_num) || !SSVM_PopInt(state, &npc_type) )
            return 1;
        info = mock230_npcinfo(npc_type);
        /* 1-based, as every other op index on the wire and in content is. An
         * absent op is the empty string rather than an abort: asking whether an
         * npc offers op 4 is a normal thing for content to do. */
        if( op_num < 1 || op_num > 5 || !info->ops[op_num - 1] )
            SSVM_PushStr(state, "");
        else
            SSVM_PushStr(state, info->ops[op_num - 1]);
        return 1;
    }

    case SS_OP_NC_SIZE:
    {
        int32_t npc_type;

        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        SSVM_PushInt(state, mock230_npcinfo(npc_type)->size);
        return 1;
    }

    case SS_OP_NC_VISLEVEL:
    {
        int32_t npc_type;

        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        /* The level the client prints beside the name, which is the record's
         * own `combat_level` — not anything derived from the npc's stats. */
        SSVM_PushInt(state, mock230_npcinfo(npc_type)->combat_level);
        return 1;
    }

    case SS_OP_OC_STACKABLE:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushInt(state, mock230_objinfo(obj_id)->stackable);
        return 1;
    }

    /* The obj record's own category (opcode 94) — the same number the
     * `[opheld<n>,_<category>]` trigger keys on, so content can test it
     * directly for the cases a trigger cannot express. */
    case SS_OP_OC_CATEGORY:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushInt(state, mock230_objinfo(obj_id)->category);
        return 1;
    }

    /* ---- animation and effects ------------------------------------ */

    case SS_OP_ANIM:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* Through the gate, exactly like the engine's own animations — the
         * reference routes `anim` through `playAnimation` too, so a script that
         * plays a low-priority emote cannot cut off a swing already queued this
         * tick. */
        mock230_anim_play_player(player, values[0], values[1]);
        return 1;
    }

    /* Appearance stance seqs — LostCity PlayerOps READYANIM…RUNANIM. Each
     * write dirties APPEARANCE so a BAS change without a worn-container write
     * (agility ~bas_set restore, equip already dirties via worn) still ships. */
    case SS_OP_READYANIM:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        player->readyanim = seq;
        player->masks |= MOCK230_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_TURNANIM:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        player->turnanim = seq;
        player->masks |= MOCK230_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_WALKANIM:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        player->walkanim = seq;
        player->masks |= MOCK230_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_WALKANIM_B:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        player->walkanim_b = seq;
        player->masks |= MOCK230_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_WALKANIM_L:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        player->walkanim_l = seq;
        player->masks |= MOCK230_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_WALKANIM_R:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        player->walkanim_r = seq;
        player->masks |= MOCK230_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_RUNANIM:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        /* LostCity allows -1 (null) to clear runanim. */
        player->runanim = seq;
        player->masks |= MOCK230_PMASK_APPEARANCE;
        return 1;
    }

    case SS_OP_NPC_ANIM:
    {
        int32_t values[2];
        struct Mock230Npc* npc = active_npc(state);

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !npc )
        {
            SSVM_Abort(state, "npc_anim with no active npc");
            return 1;
        }
        mock230_anim_play_npc(npc, values[0], values[1]);
        return 1;
    }

    case SS_OP_SPOTANIM_PL:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        player->spotanim_id = values[0];
        /* Height and delay share one int on the wire: height in the high half,
         * delay in the low. */
        player->spotanim_height_delay = (values[1] << 16) | (values[2] & 0xffff);
        player->masks |= MOCK230_PMASK_SPOTANIM;
        return 1;
    }

    case SS_OP_SPOTANIM_NPC:
    {
        int32_t values[3];
        struct Mock230Npc* npc = active_npc(state);

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !npc )
        {
            SSVM_Abort(state, "spotanim_npc with no active npc");
            return 1;
        }
        npc->spotanim_id = values[0];
        npc->spotanim_height_delay = (values[1] << 16) | (values[2] & 0xffff);
        npc->masks |= MOCK230_NMASK_SPOTANIM;
        return 1;
    }

    case SS_OP_FACESQUARE:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        /* Absolute half-tiles (LostCity faceSquare → fine(x,1)); the client
         * treats faceSquareX/Z as (tile<<1)+1 and 0,0 is the "none" sentinel. */
        player->face_x = mock230_coord_fine(coord_x(coord), 1);
        player->face_z = mock230_coord_fine(coord_z(coord), 1);
        player->masks |= MOCK230_PMASK_FACE_COORD;
        return 1;
    }

    case SS_OP_NPC_FACESQUARE:
    {
        int32_t coord;
        struct Mock230Npc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_facesquare with no active npc");
            return 1;
        }
        npc->face_x = mock230_coord_fine(coord_x(coord), 1);
        npc->face_z = mock230_coord_fine(coord_z(coord), 1);
        npc->masks |= MOCK230_NMASK_FACE_COORD;
        return 1;
    }

    case SS_OP_SAY:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        snprintf(player->say, sizeof(player->say), "%s", text);
        player->masks |= MOCK230_PMASK_SAY;
        return 1;
    }

    case SS_OP_NPC_CHANGETYPE:
    {
        int32_t type;
        struct Mock230Npc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &type) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_changetype with no active npc");
            return 1;
        }
        npc->type = type;
        npc->change_type = type;
        npc->masks |= MOCK230_NMASK_CHANGE_TYPE;
        return 1;
    }

    /* ---- interfaces ----------------------------------------------- */

    case SS_OP_IF_SETTEXT:
    {
        const char* text;
        int32_t uid;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        if( !SSVM_PopInt(state, &uid) )
            return 1;
        mock230_send_if_settext(srv->active_player, uid, text);
        return 1;
    }

    case SS_OP_IF_SETNPCHEAD:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        mock230_send_if_setnpchead(srv->active_player, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_SETPLAYERHEAD:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        mock230_send_if_setplayerhead(srv->active_player, uid);
        return 1;
    }

    case SS_OP_IF_SETANIM:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        mock230_send_if_setanim(srv->active_player, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_SETHIDE:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        mock230_send_if_sethide(srv->active_player, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_OPENCHAT:
    {
        int32_t group;
        int slot = mock230_ids()->com_chatbox_modal;

        if( !SSVM_PopInt(state, &group) )
            return 1;
        /*
         * One packet. `chatbox:chatmodal` ships hidden=1, but unhiding it is
         * the *client's* job, not the server's: mounting a sub-interface into
         * it fires the gameframe's on_sub_change hook, and script908 both
         * unhides the modal and hides `chatbox:chatdisplay` behind it. See
         * Mock230Ids.com_chatbox_modal.
         */
        mock230_send_if_opensub(
            srv->active_player, MOCK230_COM_GROUP(slot), MOCK230_COM_CHILD(slot), group, 0);
        return 1;
    }

    /*
     * if_setevents(component, from, to, events) — the rev-230 command with no
     * LostCity equivalent (src/serverscript/gen_opcode_meta.py EXTRA_OPCODES).
     *
     * At rev 230 a component is inert until the server arms it: the cache says
     * a widget *has* an op, the events mask says whether picking that op is
     * transmitted. Every "clicking it does nothing" in the gameframe is this
     * packet not having been sent — the client resolves the verb, runs the
     * component's own CS2 onop, and then has nothing to tell the server,
     * because bit N of the mask was clear.
     *
     * `from`/`to` are the sub-id range for a grid; a plain component uses 0,0.
     */
    case SS_OP_IF_SETEVENTS:
    {
        int32_t com;
        int32_t from;
        int32_t to;
        int32_t events;

        /* Popped last-argument-first, like every other host command here. */
        if( !SSVM_PopInt(state, &events) || !SSVM_PopInt(state, &to) ||
            !SSVM_PopInt(state, &from) || !SSVM_PopInt(state, &com) )
            return 1;
        mock230_send_if_setevents(srv->active_player, (int)com, (int)from, (int)to, (int)events);
        return 1;
    }

    /*
     * if_opensub(component, interface, type) — the general form of the
     * reference's if_openmain / if_openside / if_openoverlay.
     *
     * Those name one fixed slot each, which is the whole 2004 vocabulary. At
     * rev 230 a panel mounts into an arbitrary component of whatever is already
     * open, and panels nest — the side journal's five tabs all mount into
     * `side_journal:tab_container`. So the slot is an argument.
     */
    case SS_OP_IF_OPENSUB:
    {
        int32_t com;
        int32_t group;
        int32_t type;

        if( !SSVM_PopInt(state, &type) || !SSVM_PopInt(state, &group) ||
            !SSVM_PopInt(state, &com) )
            return 1;
        mock230_send_if_opensub(
            srv->active_player, MOCK230_COM_GROUP(com), MOCK230_COM_CHILD(com), (int)group, (int)type);
        return 1;
    }

    /*
     * if_closesub(component) — the inverse of if_opensub, naming the slot.
     *
     * Distinct from SS_OP_IF_CLOSE below, which is the reference's `if_close`
     * and is specialised here to the chatbox modal because that is what every
     * `[if_close]` caller in the tree means by it. A panel that mounted itself
     * into `toplevel_osrs_stretch:mainmodal` has to name that component again
     * to come out, and the note-the-mount bookkeeping the X and Escape read is
     * done inside the encoder, so a close through here keeps CLOSE_MODAL right
     * for free.
     */
    case SS_OP_IF_CLOSESUB:
    {
        int32_t com;

        if( !SSVM_PopInt(state, &com) )
            return 1;
        mock230_send_if_closesub(srv->active_player, (int)com);
        return 1;
    }

    case SS_OP_IF_OPENTOP:
    {
        int32_t group;

        if( !SSVM_PopInt(state, &group) )
            return 1;
        mock230_gameframe_opentop(srv->active_player, (int)group);
        return 1;
    }

    case SS_OP_IF_MOVESUB:
    {
        int32_t dest;
        int32_t source;

        /* Declaration order is source, dest; stack pops reverse. */
        if( !SSVM_PopInt(state, &dest) || !SSVM_PopInt(state, &source) )
            return 1;
        mock230_send_if_movesub(srv->active_player, (int)source, (int)dest);
        return 1;
    }

    case SS_OP_IF_CLOSE:
        /* Unmounting is the whole message: the same on_sub_change hook that
         * hid `chatbox:chatdisplay` on the way in brings it back when the
         * modal has no sub again (script908's else branch). */
        mock230_send_if_closesub(srv->active_player, mock230_ids()->com_chatbox_modal);
        player->resume_button_count = 0;
        return 1;

    /*
     * if_getmain()(interface) — which interface occupies the gameframe
     * mainmodal slot. See EXTRA_OPCODES in gen_opcode_meta.py.
     *
     * The encoder stores 0 when the slot is empty; content compares against
     * `null` (−1), so map empty to null here rather than inventing a second
     * sentinel content has to learn.
     */
    case SS_OP_IF_GETMAIN:
    {
        int group = player->mainmodal_group;

        SSVM_PushInt(state, group > 0 ? group : -1);
        return 1;
    }

    /*
     * `runclientscript_ss(clientscript, string, string)` — see the opcode's
     * entry in gen_opcode_meta.py for why it exists.
     *
     * Arguments are popped in reverse, as every RuneScript command does, and
     * handed to the encoder in declaration order.
     */
    case SS_OP_RUNCLIENTSCRIPT_SS:
    {
        const char* argv[2];
        int32_t script_id;

        if( !SSVM_PopStr(state, &argv[1]) || !SSVM_PopStr(state, &argv[0]) ||
            !SSVM_PopInt(state, &script_id) )
            return 1;
        mock230_send_run_clientscript_mixed(srv->active_player, (int)script_id, "ss", NULL, argv, 2);
        return 1;
    }

    /*
     * `runclientscript*(clientscript)(args...)` — the general form.
     *
     * The compiler lays a vararg call out as: the declared arguments, then the
     * vararg values, then a type string describing them (`ssc_compile.c`'s
     * vararg block; the same layout the reference's popScriptArgs reads). So
     * this pops the type string first and walks it *backwards*, because the
     * last value pushed is the first one off — which is exactly the order the
     * RUNCLIENTSCRIPT packet writes its arguments in anyway.
     *
     * The type string is copied rather than held: it is a pool pointer, and
     * the loop below pops other pool pointers on top of it.
     */
    case SS_OP_RUNCLIENTSCRIPTVARARG:
    {
        char types[MOCK230_RUNCLIENTSCRIPT_ARG_MAX + 1];
        int intv[MOCK230_RUNCLIENTSCRIPT_ARG_MAX];
        const char* strv[MOCK230_RUNCLIENTSCRIPT_ARG_MAX];
        const char* type_string;
        int32_t script_id;
        int argc;
        int i;

        if( !SSVM_PopStr(state, &type_string) )
            return 1;
        argc = (int)strlen(type_string);
        if( argc > MOCK230_RUNCLIENTSCRIPT_ARG_MAX )
        {
            /* Louder than a truncation: a short packet would run the
             * clientscript with the wrong arguments and look like a content
             * bug in the panel it drew. */
            SSVM_Abort(state, "runclientscript* takes at most %d arguments, given %d",
                       MOCK230_RUNCLIENTSCRIPT_ARG_MAX, argc);
            return 1;
        }
        memcpy(types, type_string, (size_t)argc);
        types[argc] = '\0';

        for( i = argc - 1; i >= 0; i-- )
        {
            intv[i] = 0;
            strv[i] = "";
            if( types[i] == 's' )
            {
                if( !SSVM_PopStr(state, &strv[i]) )
                    return 1;
            }
            else
            {
                int32_t value;

                if( !SSVM_PopInt(state, &value) )
                    return 1;
                intv[i] = (int)value;
            }
        }
        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        mock230_send_run_clientscript_mixed(srv->active_player, (int)script_id, types, intv,
                                            strv, argc);
        return 1;
    }

    case SS_OP_IF_ADDRESUMEBUTTON:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        if( player->resume_button_count < MOCK230_RESUME_BUTTON_MAX )
            player->resume_buttons[player->resume_button_count++] = uid;
        /*
         * Registering the button server-side is only half of it: at rev 230
         * nothing is clickable until the server says so, so the component's
         * events have to be enabled too or the player looks at a live-looking
         * prompt that swallows every click.
         *
         * The slot range covers dynamic children, not just 0. A resume button
         * on a *container* is the multi-choice dialogue: `chatmenu:options` has
         * no rows of its own, and the five the clientscript `cc_create`s carry
         * sub-ids 1..5. Arming 0..0 arms the empty container and none of the
         * rows, which is the same looks-right-does-nothing failure this call
         * exists to prevent. A plain component has no sub-ids, so the wider
         * range costs it nothing.
         */
        mock230_send_if_setevents(srv->active_player, uid, 0, MOCK230_RESUME_SUB_MAX, MOCK230_EVENT_CLICK);
        return 1;
    }

    case SS_OP_P_PAUSEBUTTON:
        /* Waits for client input, not for the clock — so nothing in the tick
         * resumes it. mock230_scripts_resume_button does, on a matching click. */
        SSVM_Suspend(state, SSVM_PAUSEBUTTON);
        return 1;

    case SS_OP_LAST_COM:
        SSVM_PushInt(state, player->last_com);
        return 1;

    /* ---- combat ---------------------------------------------------- */

    case SS_OP_P_OPNPC:
    {
        int32_t op_num;
        int slot = (int)state->host_tag - 1;
        struct Mock230Npc* npc;
        const struct Mock230NpcInfo* info;
        struct Mock230Player* player = srv->active_player;

        if( !SSVM_PopInt(state, &op_num) )
            return 1;
        if( op_num < 1 || op_num > 5 )
        {
            SSVM_Abort(state, "p_opnpc: op %d is not 1..5", (int)op_num);
            return 1;
        }
        if( slot < 0 || slot >= MOCK230_NPC_MAX || !srv->npcs[slot].active )
        {
            SSVM_Abort(state, "p_opnpc with no active npc");
            return 1;
        }
        npc = &srv->npcs[slot];
        info = mock230_npcinfo(npc->type);
        /*
         * LostCity PlayerOps.P_OPNPC: stopAction (clear interaction + walk) then
         * setInteraction — not clearPendingAction's combat_stop. Clearing
         * combat_target here aborted every melee loop the moment content called
         * p_opnpc(2) after a swing.
         */
        if( !info->ops[op_num - 1] )
            return 1;
        mock230_world_interaction_clear(srv);
        mock230_world_steps_clear(player);
        mock230_world_interaction_set(srv, MOCK230_INTERACT_NPC, (int)op_num, slot,
                                      npc->type, npc->x, npc->z, npc->level,
                                      info->size, info->size);
        {
            struct CollisionApproach approach;
            mock230_scene_npc_approach(info->size, &approach);
            mock230_world_walk_to_approach(srv, npc->x, npc->z, &approach);
        }
        /* Attack keeps the engine face/approach latch; other ops do not. */
        if( strcmp(info->ops[op_num - 1], "Attack") == 0 )
            player->combat_target = slot;
        return 1;
    }

    case SS_OP_NPC_DAMAGE:
    {
        int32_t values[2];
        int slot = (int)state->host_tag - 1;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( slot < 0 )
        {
            SSVM_Abort(state, "npc_damage with no active npc");
            return 1;
        }
        mock230_combat_hit_npc(srv, slot, values[0], values[1]);
        return 1;
    }

    case SS_OP_DAMAGE:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* values[0] is the player uid, which the single-player mock ignores. */
        mock230_combat_hit_player(srv, values[1], values[2]);
        return 1;
    }

    /*
     * healenergy(amount) — run energy, in the hundredths-of-a-percent unit the
     * whole energy system is written in, so the reference's `healenergy(10000)`
     * is a full bar.
     *
     * It restored *hitpoints* here, which is a different resource and, worse, a
     * silent one: `[queue,player_death]` calls `healenergy` right after
     * `stat_heal(hitpoints, 99, 100)`, so the wrong op was covered by the right
     * one on the only path that runs it. Content asking for energy got health,
     * and nothing anywhere said so.
     */
    case SS_OP_HEALENERGY:
    {
        int32_t amount;

        if( !SSVM_PopInt(state, &amount) )
            return 1;
        player->run_energy += amount;
        if( player->run_energy > MOCK230_RUN_ENERGY_MAX )
            player->run_energy = MOCK230_RUN_ENERGY_MAX;
        if( player->run_energy < 0 )
            player->run_energy = 0;
        return 1;
    }

    case SS_OP_UID:
        /* One player, so the uid is a constant. Content only ever passes it
         * straight back into `damage`. */
        SSVM_PushInt(state, 1);
        return 1;

    case SS_OP_NPC_FINDHERO:
        /* Whoever has been hitting this npc — always the one player here. */
        SSVM_PushInt(state, 1);
        return 1;

    case SS_OP_NPC_ATTACKRANGE:
    {
        /* The active npc's own reach, off its record — `param=attackrange,N` in
         * a .npc block, defaulting to the melee 1. It answered a C constant
         * before, so a ranged npc's script asked how far it could shoot and was
         * told "one tile" no matter what its config said. */
        struct Mock230Npc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_attackrange with no active npc");
            return 1;
        }
        SSVM_PushInt(state, npc->def ? npc->def->attackrange
                                     : mock230_content_npc_default()->attackrange);
        return 1;
    }

    /* ---- waiting -------------------------------------------------- */

    case SS_OP_P_DELAY:
    {
        int32_t ticks;

        if( !SSVM_PopInt(state, &ticks) )
            return 1;
        /* tick + 1 + n, matching the reference: p_delay(0) still costs the rest
         * of this tick, so a script cannot delay by nothing and keep running. */
        player->delayed_until = srv->tick + 1 + ticks;
        SSVM_Suspend(state, SSVM_SUSPENDED);
        return 1;
    }

    case SS_OP_P_ARRIVEDELAY:
        /* Only waits when the player actually moved this tick; a stationary
         * player runs straight through. */
        if( player->move_count > 0 )
        {
            player->delayed_until = srv->tick + 1;
            SSVM_Suspend(state, SSVM_SUSPENDED);
        }
        return 1;

    case SS_OP_NPC_DELAY:
    {
        int32_t ticks;
        struct Mock230Npc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &ticks) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_delay with no active npc");
            return 1;
        }
        npc->delayed_until = srv->tick + 1 + ticks;
        SSVM_Suspend(state, SSVM_NPC_SUSPENDED);
        return 1;
    }

    case SS_OP_WORLD_DELAY:
        /* The argument stays on the stack; the parking code takes it. */
        SSVM_Suspend(state, SSVM_WORLD_SUSPENDED);
        return 1;

    /* ---- queues and timers ---------------------------------------- */

    /*
     * The four queue commands are one body: they differ only in the *kind* they
     * store, and the kind is read by the drain and by `close_modal`.
     *
     *   queue       (script, delay, arg)                   NORMAL
     *   strongqueue (script, delay, arg)                   STRONG — closes modals
     *   weakqueue   (script, delay, arg)                   WEAK   — dies with them
     *   longqueue   (script, delay, arg, logout_action)    LONG   — survives logout
     *
     * The vararg forms (`queue*` and friends) are a separate declaration the
     * compiler refuses outright (`ssc_compile.c`: "it packs a type string the
     * compiler does not build"), so both halves agree that they do not exist.
     */
    case SS_OP_QUEUE:
    case SS_OP_STRONGQUEUE:
    case SS_OP_WEAKQUEUE:
    case SS_OP_LONGQUEUE:
    {
        int32_t values[4] = { 0, 0, 0, 0 };
        int argc = opcode == SS_OP_LONGQUEUE ? 4 : 3;
        int kind = opcode == SS_OP_STRONGQUEUE ? MOCK230_QUEUE_STRONG
                   : opcode == SS_OP_WEAKQUEUE ? MOCK230_QUEUE_WEAK
                   : opcode == SS_OP_LONGQUEUE ? MOCK230_QUEUE_LONG
                                               : MOCK230_QUEUE_NORMAL;

        for( int i = argc - 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
        {
            if( player->queue[i].active )
                continue;
            player->queue[i].active = 1;
            player->queue[i].script_id = values[0];
            /* +1 so delay 0 means "next tick", not "this one". The drain
             * pre-decrements, so this is the reference's raw store read the
             * other way round — same answer for every delay. */
            player->queue[i].delay = values[1] + 1;
            player->queue[i].arg = values[2];
            player->queue[i].kind = kind;
            player->queue[i].logout_action = values[3];
            return 1;
        }
        SSVM_Abort(state, "the player's queue is full");
        return 1;
    }

    /*
     * `clearqueue` unlinks every copy of one script from the primary *and* the
     * weak queue — `unlinkQueuedScript`'s default branch walks both.
     */
    case SS_OP_CLEARQUEUE:
    {
        int32_t script_id;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
        {
            if( player->queue[i].active && player->queue[i].script_id == script_id )
                player->queue[i].active = 0;
        }
        return 1;
    }

    /* `getqueue` counts and does not clear. Two copies of one script are two
     * entries, which is the whole reason content asks. */
    case SS_OP_GETQUEUE:
    {
        int32_t script_id;
        int count = 0;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
        {
            if( player->queue[i].active && player->queue[i].script_id == script_id )
                count++;
        }
        SSVM_PushInt(state, count);
        return 1;
    }

    case SS_OP_SETTIMER:
    case SS_OP_SOFTTIMER:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* A timer is keyed by its script: setting the same one twice re-arms it
         * rather than stacking a second copy. So look for it first, and only
         * take a free slot if it is not already running. */
        {
            struct Mock230Timer* slot = NULL;

            for( int i = 0; i < MOCK230_TIMER_MAX; i++ )
            {
                if( player->timers[i].active && player->timers[i].script_id == values[0] )
                {
                    slot = &player->timers[i];
                    break;
                }
            }
            for( int i = 0; !slot && i < MOCK230_TIMER_MAX; i++ )
            {
                if( !player->timers[i].active )
                    slot = &player->timers[i];
            }
            if( !slot )
            {
                SSVM_Abort(state, "no free timer slot");
                return 1;
            }
            slot->active = 1;
            slot->script_id = values[0];
            slot->interval = values[1];
            /* The world tick, not zero. `Player.setTimer` stores
             * `World.currentTick` and fires on `currentTick >= clock + interval`
             * — and `gettimer` returns this number, so a countdown here would
             * make that opcode unimplementable rather than merely different. */
            slot->clock = srv->tick;
            slot->type = opcode == SS_OP_SOFTTIMER ? MOCK230_TIMER_SOFT : MOCK230_TIMER_NORMAL;
        }
        return 1;
    }

    /*
     * `cleartimer` and `clearsofttimer` are the same operation on the same
     * table: the reference's `clearTimer(timerId)` is `timers.delete(timerId)`
     * with no type in it at all, and a timer is keyed by its script, so the two
     * commands can never name the same one.
     *
     * The `active` test is new. Without it the loop matched zeroed slots, whose
     * `script_id` is 0 — a no-op in practice, but it made the code say something
     * it did not mean.
     */
    case SS_OP_CLEARTIMER:
    case SS_OP_CLEARSOFTTIMER:
    {
        int32_t script_id;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < MOCK230_TIMER_MAX; i++ )
        {
            if( player->timers[i].active && player->timers[i].script_id == script_id )
                player->timers[i].active = 0;
        }
        return 1;
    }

    /* The absolute tick the timer was last armed or fired at, or -1. */
    case SS_OP_GETTIMER:
    {
        int32_t script_id;
        int32_t answer = -1;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < MOCK230_TIMER_MAX; i++ )
        {
            if( player->timers[i].active && player->timers[i].script_id == script_id )
                answer = player->timers[i].clock;
        }
        SSVM_PushInt(state, answer);
        return 1;
    }

    case SS_OP_MAP_CLOCK:
        SSVM_PushInt(state, srv->tick);
        return 1;

    /*
     * `map_members` gates the members-only branches in LostCity's drop tables,
     * which are ported here verbatim. The mock is a free world, so this is a
     * constant — but it has to *exist*, because the alternative is deleting
     * every `if (map_members = ^true)` from content that is otherwise a
     * character-for-character copy of the reference's.
     */
    case SS_OP_MAP_MEMBERS:
        SSVM_PushInt(state, 0);
        return 1;

    /*
     * A rectangle test, and the argument order is the trap.
     *
     * `inzone(from, to, pos)` pushes three coords, so they pop in reverse: pos
     * first. Getting that backwards makes the test read "is `from` inside the
     * rectangle (to, pos)", which is true often enough to look like it works.
     *
     * The reference tests all three axes including level, and its own comparison
     * assumes `from` is the south-west corner and `to` the north-east. A caller
     * that passes them the other way round gets an empty rectangle rather than a
     * diagnostic, in the reference too.
     */
    case SS_OP_INZONE:
    {
        int32_t corner_sw;
        int32_t corner_ne;
        int32_t pos;
        int inside;

        if( !SSVM_PopInt(state, &pos) || !SSVM_PopInt(state, &corner_ne) ||
            !SSVM_PopInt(state, &corner_sw) )
            return 1;
        inside = coord_x(pos) >= coord_x(corner_sw) && coord_x(pos) <= coord_x(corner_ne) &&
                 coord_z(pos) >= coord_z(corner_sw) && coord_z(pos) <= coord_z(corner_ne) &&
                 coord_level(pos) >= coord_level(corner_sw) &&
                 coord_level(pos) <= coord_level(corner_ne);
        SSVM_PushInt(state, inside);
        return 1;
    }

    /*
     * Can you see / walk a straight line between two tiles?
     *
     * LostCity `ServerOps.ts` LINEOFSIGHT / LINEOFWALK → rsmod
     * `hasLineOfSight` / `hasLineOfWalk(..., 1,1,1,1, 0)`. Different plane →
     * false. Outside the built scene → false (same "unknown means don't" rule
     * as map_blocked). The F2P-zone gate the reference applies is not here —
     * this tree has no free-to-play map mask yet.
     */
    case SS_OP_LINEOFSIGHT:
    case SS_OP_LINEOFWALK:
    {
        int32_t from;
        int32_t to;
        int level;
        int x1;
        int z1;
        int x2;
        int z2;
        int clear;

        if( !SSVM_PopInt(state, &to) || !SSVM_PopInt(state, &from) )
            return 1;
        level = coord_level(from);
        if( level != coord_level(to) )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        x1 = coord_x(from);
        z1 = coord_z(from);
        x2 = coord_x(to);
        z2 = coord_z(to);
        clear = opcode == SS_OP_LINEOFSIGHT
                    ? mock230_scene_line_of_sight(level, x1, z1, x2, z2, 1, 1, 1, 1, 0)
                    : mock230_scene_line_of_walk(level, x1, z1, x2, z2, 1, 1, 1, 1, 0);
        SSVM_PushInt(state, clear ? 1 : 0);
        return 1;
    }

    /*
     * Does this tile block walking?
     *
     * The reference is `isFlagged(x, z, level, CollisionFlag.WALK_BLOCKED)`, and
     * `COLL_FLAG_WALK_BLOCKED` in collision_map.h is that same composite
     * (LOC | FLOOR | ANTIMACRO). Reading it off the CollisionMap the scene
     * already built means the server and the client answer this from one model
     * rather than two that can drift.
     *
     * A tile outside the built scene reports *blocked*. That is the safe
     * direction: content asks this before dropping a fire or picking a wander
     * target, and "unknown" has to mean "don't" or the loop spawns things in
     * unloaded map.
     */
    case SS_OP_MAP_BLOCKED:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        SSVM_PushInt(state, mock230_scene_walk_blocked(coord_level(coord),
                                                       coord_x(coord),
                                                       coord_z(coord)));
        return 1;
    }

    /*
     * How many players stand inside a rectangle.
     *
     * One player for now — but the count, not a boolean, because the content
     * that asks (`~playercount_coord_pair_table`) compares it against a
     * threshold. The reference walks the zones the rectangle covers; this walks
     * the player pool, which is the same answer while the pool is small and is
     * what the zone map (§6.1 step 3) will replace.
     */
    case SS_OP_MAP_PLAYERCOUNT:
    {
        int32_t corner_sw;
        int32_t corner_ne;
        int count = 0;

        if( !SSVM_PopInt(state, &corner_ne) || !SSVM_PopInt(state, &corner_sw) )
            return 1;
        for( int i = 0; i < srv->player_count; i++ )
        {
            const struct Mock230Player* other = &srv->players[i];

            if( other->level < coord_level(corner_sw) ||
                other->level > coord_level(corner_ne) )
                continue;
            if( other->x < coord_x(corner_sw) || other->x > coord_x(corner_ne) )
                continue;
            if( other->z < coord_z(corner_sw) || other->z > coord_z(corner_ne) )
                continue;
            count++;
        }
        SSVM_PushInt(state, count);
        return 1;
    }

    /* ---- enums ----------------------------------------------------- */

    /*
     * `enum(inputtype, outputtype, enum, key)`.
     *
     * The declared output type decides **which stack** the result goes on, so
     * this is one of the few host commands where getting a type wrong does not
     * produce a wrong number — it produces a wrong *stack depth*, and every
     * value the script reads afterwards is somebody else's. That is why the
     * reference validates the declared types against the enum's own and why this
     * aborts rather than guessing.
     *
     * A key with no entry yields the enum's `default=`, not an error: the
     * reference's content relies on that for sparse tables.
     */
    case SS_OP_ENUM:
    {
        int32_t values[4];
        const struct Mock230EnumDef* def;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        def = mock230_content_enum_by_id(values[2]);
        if( !def )
        {
            SSVM_Abort(state, "enum %d is not defined by any .enum config", values[2]);
            return 1;
        }
        for( int i = 0; i < def->count; i++ )
        {
            if( def->values[i].key != values[3] )
                continue;
            if( def->output_is_string )
                SSVM_PushStr(state, def->values[i].text ? def->values[i].text : "");
            else
                SSVM_PushInt(state, def->values[i].value);
            return 1;
        }
        if( def->output_is_string )
            SSVM_PushStr(state, def->default_text ? def->default_text : "null");
        else
            SSVM_PushInt(state, def->default_int);
        return 1;
    }

    case SS_OP_ENUM_GETOUTPUTCOUNT:
    {
        int32_t enum_id;
        const struct Mock230EnumDef* def;

        if( !SSVM_PopInt(state, &enum_id) )
            return 1;
        def = mock230_content_enum_by_id(enum_id);
        if( !def )
        {
            SSVM_Abort(state, "enum_getoutputcount on undefined enum %d", enum_id);
            return 1;
        }
        SSVM_PushInt(state, def->count);
        return 1;
    }

    case SS_OP_RANDOM:
    {
        int32_t bound;

        if( !SSVM_PopInt(state, &bound) )
            return 1;
        /* random(n) is 0..n-1, matching the reference. A zero or negative
         * bound yields 0 rather than aborting: content computing a bound from
         * a table size should not take the server down when the table is
         * empty. */
        SSVM_PushInt(state, bound > 0 ? mock230_random(srv, 0, bound - 1) : 0);
        return 1;
    }

    /* ---- ground objs ---------------------------------------------- */

    /*
     * `obj_add(coord, obj, count, duration)` — the command every drop table in
     * LostCity's content is written against. Duration is in ticks
     * (`^lootdrop_duration` is 200); a non-positive one means a permanent
     * spawn, which is how the map squares' own objs are placed.
     */
    case SS_OP_OBJ_ADD:
    {
        int32_t values[4];

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        mock230_world_obj_add(srv, values[1], values[2], coord_x(values[0]),
                              coord_z(values[0]), coord_level(values[0]),
                              values[3] > 0 ? values[3] : -1);
        /*
         * Official client loot tracker: RUNCLIENTSCRIPT script_7192
         * (LOOTTRACKER_ADD_LOOT) with (npcId, eventId, itemId, qty). Only while
         * combat death credit is armed — not every ground spawn.
         */
        if( srv->loot_credit_armed )
        {
            enum
            {
                MOCK230_SCRIPT_LOOTTRACKER_ADD_LOOT = 7192
            };
            int args[4] = {
                srv->loot_credit_npc_type,
                srv->loot_credit_event_id,
                (int)values[1],
                (int)values[2],
            };

            for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
            {
                if( !srv->loot_credit_players[i] || !srv->players[i].active )
                    continue;
                mock230_send_run_clientscript(
                    &srv->players[i], MOCK230_SCRIPT_LOOTTRACKER_ADD_LOOT, args, 4);
            }
        }
        return 1;
    }

    /* ---- stats ----------------------------------------------------- */

    /*
     * Every stat command aborts on an id outside the table rather than
     * returning zero or doing nothing.
     *
     * A stat is a bare name in RuneScript, and this cache uses three of the 23
     * names for something else as well — `hitpoints` is also a param, `attack`
     * a varp, `fishing` a loc. The compiler now resolves the stat family with a
     * kind hint so those cannot arrive here, and this is the second half of
     * that fix: if one ever does, the number will be in the thousands, and a
     * silent no-op means a script that heals nothing and says nothing. That is
     * how the first version of the food content shipped looking correct.
     */
    case SS_OP_STAT:
    {
        int32_t stat;

        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( stat < 0 || stat >= MOCK230_STAT_COUNT )
        {
            SSVM_Abort(state, "stat %d is not a skill", stat);
            return 1;
        }
        /* `stat` is the *boosted* level in the reference — what a level check
         * in content wants to know is what you can do right now. */
        SSVM_PushInt(state, player->stat_boosted[stat]);
        return 1;
    }

    /* The *base* level — what the stat would be with no boost or drain on it.
     * Content asks for this when a cap is involved: an altar restores prayer to
     * its base, not to whatever a potion left it at. */
    case SS_OP_STAT_BASE:
    {
        int32_t stat;

        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( stat < 0 || stat >= MOCK230_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_base %d is not a skill", stat);
            return 1;
        }
        SSVM_PushInt(state, player->stat_level[stat]);
        return 1;
    }

    /*
     * stat_heal(stat, constant, percent) — the reference's formula, verbatim.
     *
     *   healed = current + (constant + base * percent / 100)
     *   level  = max(min(healed, base), current)
     *
     * The two clamps are what make it a *heal*: it never exceeds the base level
     * and never takes a stat down, so calling it on a stat already at full is a
     * no-op rather than a reset. Food is (n, 0); an altar is (base - current,
     * 0); a percentage restore is (0, n).
     */
    /*
     * stat_boost / stat_drain: the two directions of a temporary level change.
     *
     * Same `(stat, constant, percent)` shape as stat_heal below, and the same
     * `constant + base * percent / 100` arithmetic — that is the reference's
     * formula and the reason a super attack potion is written `(5, 15)` rather
     * than as a number of levels.
     *
     * The difference from stat_heal is which direction the clamp faces. A boost
     * may take the boosted level *above* base and must not be undone by a second
     * boost that computes a smaller target; a drain may take it below and must
     * not go under zero. stat_heal restores toward base and clamps at it.
     *
     * `stat_add` and `stat_sub` are deliberately NOT here despite the identical
     * arity. Their reference semantics — whether they move the base level or the
     * boosted one — is not something this repo pins down, and an opcode
     * implemented from a guess is silent when it is wrong, where the VM's stub
     * is loud.
     */
    case SS_OP_STAT_BOOST:
    /* `stat_sub` and `stat_drain` are the same operation: subtract a flat
     * amount plus a percentage of the base level from the boosted level. The
     * reference has both names because content reads better one way for a
     * poison hit and the other for prayer drain; there is nothing to
     * distinguish in the handler, and giving `stat_sub` its own would be two
     * copies of one rule. */
    case SS_OP_STAT_SUB:
    case SS_OP_STAT_DRAIN:
    {
        int32_t values[3];
        int base;
        int current;
        int target;
        int boosting = opcode == SS_OP_STAT_BOOST;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] < 0 || values[0] >= MOCK230_STAT_COUNT )
        {
            SSVM_Abort(state, "%s %d is not a skill", SSVM_OpcodeName(opcode), values[0]);
            return 1;
        }

        base = player->stat_level[values[0]];
        current = player->stat_boosted[values[0]];
        target = values[1] + base * values[2] / 100;
        target = boosting ? current + target : current - target;
        if( target < 0 )
            target = 0;
        if( boosting && target < current )
            target = current;
        if( !boosting && target > current )
            target = current;
        if( target == current )
            return 1;

        player->stat_boosted[values[0]] = target;
        /* Hitpoints are two views of one number — the stat the skills tab
         * prints and the health orb's `hitpoints`. */
        if( values[0] == MOCK230_STAT_HITPOINTS )
            player->hitpoints = target;
        mock230_combat_stat_mark(player, values[0]);
        return 1;
    }

    case SS_OP_STAT_TOTAL:
    {
        int total = 0;

        /* Base levels, not boosted: the total-level number is what the skills
         * tab prints, and a potion does not change it. */
        for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
            total += player->stat_level[stat];
        SSVM_PushInt(state, total);
        return 1;
    }

    /*
     * p_logout: end the session.
     *
     * Killing the session is the whole of it — the socket loop and the embedded
     * pump both exit on a dead session, and the teardown that follows is what
     * saves the player. Doing anything more here would duplicate that path.
     */
    case SS_OP_P_LOGOUT:
        if( srv->active_player && srv->active_player->session )
            mock230_session_kill(srv->active_player->session);
        return 1;

    case SS_OP_STAT_HEAL:
    {
        int32_t values[3];
        int base;
        int current;
        int healed;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] < 0 || values[0] >= MOCK230_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_heal %d is not a skill", values[0]);
            return 1;
        }
        base = player->stat_level[values[0]];
        current = player->stat_boosted[values[0]];
        healed = current + values[1] + base * values[2] / 100;
        if( healed > base )
            healed = base;
        if( healed < current )
            healed = current;
        if( healed == current )
            return 1;
        player->stat_boosted[values[0]] = healed;
        /* Hitpoints are two views of one number — the stat the skills tab
         * prints and the health orb's `hitpoints`. Writing only the stat would
         * heal a player whose orb never moved. */
        if( values[0] == MOCK230_STAT_HITPOINTS )
            player->hitpoints = healed;
        mock230_combat_stat_mark(player, values[0]);
        return 1;
    }

    /*
     * stat_random(stat, low, high) — the level-interpolated success roll.
     *
     *   value  = low * (99 - level) / 98 + high * (level - 1) / 98 + 1
     *   return value > random(256)
     *
     * `low` is the chance out of 256 at level 1 and `high` the chance at level
     * 99, with everything between them on a straight line. Every skill in
     * OldSchool that can fail rolls this, which is why it is a command and not
     * four lines of `calc()` in content: it is the formula, and the rule this
     * tree keeps is that arithmetic stays in C.
     */
    case SS_OP_STAT_RANDOM:
    {
        int32_t values[3];
        int level;
        int value;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] < 0 || values[0] >= MOCK230_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_random %d is not a skill", values[0]);
            return 1;
        }
        level = player->stat_boosted[values[0]];
        value = values[1] * (99 - level) / 98 + values[2] * (level - 1) / 98 + 1;
        SSVM_PushInt(state, value > mock230_random(srv, 0, 255) ? 1 : 0);
        return 1;
    }

    case SS_OP_STAT_ADVANCE:
    {
        int32_t stat;
        int32_t experience;

        if( !SSVM_PopInt(state, &experience) )
            return 1;
        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( stat < 0 || stat >= MOCK230_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_advance %d is not a skill", stat);
            return 1;
        }
        /* The reference's xp argument is already in tenths. */
        mock230_combat_add_xp(srv, stat, experience);
        return 1;
    }

    /* `npc_param` was here. It is `mock230_ops_npc.c`'s now — and had been
     * unreachable since that domain's hook went in above, because the per-domain
     * handlers are offered the opcode first. Deleted rather than marked dead: it
     * answered one param by spelling `death_drop` in C, which is the hard rule
     * in PORTING_GUIDE §2.4 item 3, and a dead copy of a wrong answer is the
     * thing somebody eventually reads and believes. */

    /* ---- audio ---------------------------------------------------- */

    case SS_OP_SOUND_SYNTH:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* The encoder lands with the rest of the dialogue packets. Accepting
         * the call now keeps content that uses it compiling and running. */
        if( srv->verbose )
            fprintf(stderr, "mock230: sound_synth(%d, %d, %d)\n", values[0], values[1],
                    values[2]);
        return 1;
    }

    /* ---- containers ------------------------------------------------ */

    /*
     * The container commands the bank content needs.
     *
     * These are LostCity's own signatures, so `content/scripts/interface_bank`
     * ports across as text. What differs is the *implementation*: LostCity's
     * inventories are dynamic objects with a transmit list per client, and the
     * mock has three fixed containers and one client, so "transmit" is a flag
     * and "stop transmitting" is clearing it.
     */

    case SS_OP_INV_SIZE:
    {
        int32_t inv_id;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        /*
         * Straight off the cache, not through the registry. `inv_size` is a
         * question about the *type*, which is what LostCity asks
         * (`InvType.get(inv).size`) — a container nobody has touched yet still
         * has a size. Routing it through `container_for` is what made this
         * return 0 for 1,023 of the cache's 1,026 invs while
         * `mock230_bank_inv_size` sat beside it answering every one.
         */
        SSVM_PushInt(state, mock230_bank_inv_size((int)inv_id));
        return 1;
    }

    /*
     * inv_setslot: write one cell, whatever was there.
     *
     * `[command,inv_setslot](inv $inv, int $slot, namedobj $obj, int $count)` in
     * the reference's engine.rs2. The registry's own mutator owns the dirty
     * flag, which is the only reason this is three lines: a per-slot mask for a
     * 28-slot backpack and a whole-container flag for a 500-slot one are the
     * same call here.
     */
    case SS_OP_INV_SETSLOT:
    {
        int32_t values[4];
        struct Mock230Container* row;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        row = container_row(srv, values[0]);
        if( !row )
        {
            SSVM_Abort(state, "inv_setslot on unknown container %d", values[0]);
            return 1;
        }
        if( values[1] < 0 || values[1] >= row->slots )
        {
            SSVM_Abort(state, "inv_setslot slot %d outside container %d (%d slots)",
                       values[1], values[0], row->slots);
            return 1;
        }
        mock230_container_set(row, (int)values[1], (int)values[2], (int)values[3]);
        return 1;
    }

    case SS_OP_INV_GETOBJ:
    case SS_OP_INV_GETNUM:
    {
        int32_t inv_id;
        int32_t slot;
        int slots = 0;
        struct Mock230Item* items;

        if( !SSVM_PopInt(state, &slot) || !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, inv_id, &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_getobj/getnum on unknown container %d", inv_id);
            return 1;
        }
        if( slot < 0 || slot >= slots || items[slot].obj_id < 0 )
        {
            /* `null` is -1 for an obj and 0 for a count, which is what every
             * caller branches on. */
            SSVM_PushInt(state, opcode == SS_OP_INV_GETOBJ ? -1 : 0);
            return 1;
        }
        SSVM_PushInt(state, opcode == SS_OP_INV_GETOBJ ? items[slot].obj_id
                                                       : items[slot].count);
        return 1;
    }

    /*
     * inv_itemspace / inv_itemspace2: "will this fit" and "how much will not".
     *
     * The reference splits them because the *messages* differ — a stack that
     * will not fit and a pile that will not all fit are different sentences —
     * and both need the overflow count, not a boolean.
     */
    case SS_OP_INV_ITEMSPACE:
    case SS_OP_INV_ITEMSPACE2:
    {
        int32_t inv_id;
        int32_t obj_id;
        int32_t count;
        int32_t limit;
        int slots = 0;
        struct Mock230Item* items;
        int space = 0;

        if( !SSVM_PopInt(state, &limit) || !SSVM_PopInt(state, &count) ||
            !SSVM_PopInt(state, &obj_id) || !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, inv_id, &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_itemspace on unknown container %d", inv_id);
            return 1;
        }
        if( limit > 0 && limit < slots )
            slots = (int)limit;
        {
            if( mock230_objinfo((int)obj_id)->stackable )
            {
                int has_stack = 0;

                for( int i = 0; i < slots; i++ )
                    if( items[i].obj_id == obj_id )
                        has_stack = 1;
                    else if( items[i].obj_id < 0 )
                        space++;
                space = (has_stack || space > 0) ? (int)count : 0;
            }
            else
            {
                for( int i = 0; i < slots; i++ )
                    if( items[i].obj_id < 0 )
                        space++;
                if( space > count )
                    space = (int)count;
            }
        }
        if( opcode == SS_OP_INV_ITEMSPACE )
            SSVM_PushInt(state, space >= count ? 1 : 0);
        else
            SSVM_PushInt(state, (int)count - space);
        return 1;
    }

    case SS_OP_INV_MOVETOSLOT:
    {
        int32_t from_inv;
        int32_t to_inv;
        int32_t from_slot;
        int32_t to_slot;
        int from_slots = 0;
        int to_slots = 0;
        struct Mock230Item* from_items;
        struct Mock230Item* to_items;

        if( !SSVM_PopInt(state, &to_slot) || !SSVM_PopInt(state, &from_slot) ||
            !SSVM_PopInt(state, &to_inv) || !SSVM_PopInt(state, &from_inv) )
            return 1;
        from_items = container_for(srv, from_inv, &from_slots);
        to_items = container_for(srv, to_inv, &to_slots);
        if( !from_items || !to_items )
        {
            SSVM_Abort(state, "inv_movetoslot between containers %d and %d", from_inv, to_inv);
            return 1;
        }
        if( from_slot < 0 || from_slot >= from_slots || to_slot < 0 || to_slot >= to_slots )
            return 1;
        {
            struct Mock230Item swap = from_items[from_slot];

            from_items[from_slot] = to_items[to_slot];
            to_items[to_slot] = swap;
        }
        container_dirty(srv, from_inv, (int)from_slot);
        container_dirty(srv, to_inv, (int)to_slot);
        return 1;
    }

    /* `inv_moveitem` and its `_cert` / `_uncert` siblings were here. They are
     * mock230_ops_inv.c's now, with their three bank arms unchanged and a
     * fourth, generic container-to-container arm appended after them — the arm
     * every reference equip and unequip path needs and which used to print
     * "is not modelled" and do nothing. That gap was invisible to
     * gen_opcode_coverage.py because the opcode had a `case` label. */

    case SS_OP_INV_CLEAR:
    {
        int32_t inv_id;
        int slots = 0;
        struct Mock230Item* items = NULL;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, inv_id, &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_clear on unknown container %d", inv_id);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            items[i].obj_id = -1;
            items[i].count = 0;
            container_dirty(srv, inv_id, i);
        }
        return 1;
    }

    /*
     * inv_transmit / inv_stoptransmit: bind a container to a component.
     *
     * The reference keeps a per-client list of (inv, component) bindings and
     * sends a full update when one is added. There is one client here and the
     * bindings are fixed, so the whole of it is "send the container now" —
     * which is the part that matters, because the interface it paints was built
     * before the container existed and its paint hook only runs on a transmit.
     */
    case SS_OP_INV_TRANSMIT:
    {
        int32_t inv_id;
        int32_t component;

        if( !SSVM_PopInt(state, &component) || !SSVM_PopInt(state, &inv_id) )
            return 1;
        /*
         * The bank is deliberately not bound through the registry.
         *
         * Its transmit is gated on the interface being open and re-sends tab
         * bookkeeping with it (mock230_bank_flush / bank_push_settings), which
         * the generic binding does not model — binding it here would put two
         * senders on one container, which is exactly the failure mode the
         * registry exists to remove. Its *row* is in the registry all the same,
         * and that is what stopped `inv_del(bank,…)` dirtying a worn slot.
         * Folding the transmit in means moving `bank.open` into the binding
         * table; that is a real simplification and it is not this stage's.
         */
        if( inv_id == mock230_ids()->inv_bank )
        {
            struct Mock230Container* row = container_row(srv, inv_id);
            int used = 0;

            if( !row )
                return 1;
            /* Only the used prefix: UPDATE_INV_FULL clears everything past the
             * capacity it carries, so 1,398 empty slots cost nothing. */
            for( int i = 0; i < row->slots; i++ )
                if( row->items[i].obj_id >= 0 )
                    used = i + 1;
            srv->active_player->bank.open = 1;
            mock230_container_clean(row);
            mock230_send_inv_full(srv->active_player, (int)component, (int)inv_id, row->items,
                                  used);
            return 1;
        }
        if( !mock230_container_bind(srv, srv->active_player, inv_id, component) )
        {
            SSVM_Abort(state, "inv_transmit on unknown container %d", inv_id);
            return 1;
        }
        return 1;
    }

    case SS_OP_INV_STOPTRANSMIT:
    {
        int32_t component;

        if( !SSVM_PopInt(state, &component) )
            return 1;
        if( MOCK230_COM_GROUP(component) == mock230_ids()->iface_bankmain )
            srv->active_player->bank.open = 0;
        mock230_container_unbind(srv->active_player, component);
        return 1;
    }

    /* ---- objs ------------------------------------------------------ */

    /*
     * oc_cert / oc_uncert: the note form of an obj and back.
     *
     * The cache states only one direction — a note record names the item it
     * stands for — so the forward link is a reverse index mock230_objinfo
     * builds. Both return the input unchanged when there is no other form,
     * which is what the reference does and what every caller tests for.
     */
    case SS_OP_OC_CERT:
    case SS_OP_OC_UNCERT:
    {
        int32_t obj_id;
        const struct Mock230ObjInfo* info;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        info = mock230_objinfo((int)obj_id);
        if( opcode == SS_OP_OC_CERT )
            SSVM_PushInt(state, info->cert_id >= 0 ? info->cert_id : obj_id);
        else
            SSVM_PushInt(state, (info->noted_template >= 0 && info->noted_id >= 0)
                                    ? info->noted_id
                                    : obj_id);
        return 1;
    }

    /* ---- varbits --------------------------------------------------- */

    /*
     * A varbit is a bit range inside a varplayer, and which range is a cache
     * fact — see mock230_bank.c. Writing one as a whole varp would destroy
     * whatever else shares it, which for the bank is always something: the
     * withdraw-as-note flag and the current tab are both in varp 115.
     */
    /*
     * The varbit id is the *operand*, exactly as it is for PUSH_VARP/POP_VARP —
     * `%name` compiles to one instruction carrying the id, with `1 << 16` set
     * for the `.%name` secondary-player form (ssc_compile.c `resolve_variable`).
     *
     * Both of these used to pop the id off the int stack instead. That is not a
     * near miss: PUSH_VARBIT pops nothing per ss_meta.gen.h, so it underflowed;
     * POP_VARBIT popped the *value* and read it as the id, then underflowed
     * looking for a value that was never there. Every `%varbit` in the tree
     * aborted its script at the first mention, and because [login,_] is where
     * opening state is set, one such line took the whole login script with it.
     */
    case SS_OP_PUSH_VARBIT:
    {
        int varbit_id = state->script->int_operands[state->pc] & 0xffff;

        SSVM_PushInt(state, mock230_varbit_get(player, varbit_id));
        return 1;
    }

    case SS_OP_POP_VARBIT:
    {
        int varbit_id = state->script->int_operands[state->pc] & 0xffff;
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        /* A varbit the cache does not place has no varp to write, so the write
         * would vanish. Loud, like every other unresolvable id here. */
        if( mock230_varbit_set(srv, varbit_id, (int)value) < 0 )
            SSVM_Abort(state, "varbit %d is not in the cache", varbit_id);
        return 1;
    }

    /* ---- interfaces ------------------------------------------------ */

    /*
     * if_openmain_side: the two-panel open a bank (or a shop, or a trade) is.
     *
     * The main interface goes into toplevel's `mainmodal` and the side one
     * replaces the whole sidebar through `sidemodal`, which is what puts the
     * bank's inventory panel where the tab strip was. Doing only the first
     * leaves the player's real inventory tab beside a bank that cannot see it.
     */
    case SS_OP_IF_OPENMAIN_SIDE:
    {
        int32_t main_group;
        int32_t side_group;

        if( !SSVM_PopInt(state, &side_group) || !SSVM_PopInt(state, &main_group) )
            return 1;
        if( main_group == mock230_ids()->iface_bankmain )
        {
            /* The bank knows how to open itself — settings, events and both
             * containers — and doing it here rather than leaving the script to
             * push fifteen varbits is what keeps the ported content readable. */
            mock230_bank_open(srv);
            return 1;
        }
        mock230_send_if_opensub(
            srv->active_player,
            mock230_ids()->iface_gameframe,
            MOCK230_COM_CHILD(mock230_ids()->com_gameframe_mainmodal),
            (int)main_group,
            0);
        mock230_send_if_opensub(
            srv->active_player,
            mock230_ids()->iface_gameframe,
            MOCK230_COM_CHILD(mock230_ids()->com_gameframe_sidemodal),
            (int)side_group,
            3);
        return 1;
    }

    case SS_OP_IF_OPENMAIN:
    {
        int32_t group;

        if( !SSVM_PopInt(state, &group) )
            return 1;
        mock230_send_if_opensub(
            srv->active_player,
            mock230_ids()->iface_gameframe,
            MOCK230_COM_CHILD(mock230_ids()->com_gameframe_mainmodal),
            (int)group,
            0);
        return 1;
    }

    /*
     * p_countdialog: ask for a number and wait.
     *
     * The client opens its own "Enter amount" prompt and answers with
     * RESUME_P_COUNTDIALOG. Nothing in the tick releases this — the same shape
     * as p_pausebutton, and for the same reason.
     */
    case SS_OP_P_COUNTDIALOG:
        player->last_int = 0;
        mock230_send_if_opencountdialog(srv->active_player);
        SSVM_Suspend(state, SSVM_COUNTDIALOG);
        return 1;

    /*
     * p_countdialog_noprompt: wait for a number WITHOUT opening the prompt.
     *
     * The wait half of the op above, and nothing else. `resume_countdialog` is
     * an ordinary CS2 opcode at this revision, so an interface already on
     * screen can answer a parked script itself — the cache's bank PIN keypad
     * (213) is exactly that: it collects four clicked digits and sends the
     * assembled number.
     *
     * Waiting for it with `p_countdialog` would work and would still be wrong:
     * the prompt it opens echoes the digits as they are typed, which is the
     * one thing a PIN keypad exists to prevent. There is no reference for the
     * split because there is no rev-230 client in the reference.
     */
    case SS_OP_P_COUNTDIALOG_NOPROMPT:
        player->last_int = 0;
        SSVM_Suspend(state, SSVM_COUNTDIALOG);
        return 1;

    case SS_OP_LAST_INT:
        SSVM_PushInt(state, player->last_int);
        return 1;
    case SS_OP_LAST_SLOT:
        SSVM_PushInt(state, player->last_slot);
        return 1;
    case SS_OP_LAST_TARGETSLOT:
        SSVM_PushInt(state, player->last_targetslot);
        return 1;
    case SS_OP_LAST_ITEM:
        SSVM_PushInt(state, player->last_item);
        return 1;

    /*
     * The other half of a use-on. `last_item` is the trigger's subject and these
     * are the item it was used *with* — for `[oplocu]`/`[opnpcu]`/`[opobju]` the
     * subject is not an item at all, so this is the only way in.
     */
    case SS_OP_LAST_USEITEM:
        SSVM_PushInt(state, player->last_useitem);
        return 1;
    case SS_OP_LAST_USESLOT:
        SSVM_PushInt(state, player->last_useslot);
        return 1;

    case SS_OP_DISPLAYNAME:
        SSVM_PushStr(state, player->display_name[0] ? player->display_name : "Player");
        return 1;

    /*
     * `p_stopaction` ends whatever the player was doing before the script's own
     * effect lands. Combat is the only standing action the mock models — the
     * walk queue is not one, because a click that starts a script has already
     * replaced it — so that is the whole implementation rather than a partial
     * one.
     */
    case SS_OP_P_STOPACTION:
        mock230_combat_stop_player(srv);
        return 1;

    case SS_OP_P_LOCMERGE:
    {
        /* LostCity PlayerOps: pop start, end, se, nw; mergeLoc(loc, player,
         * start, end, se.z, se.x, nw.z, nw.x) → wire offsets from loc tile. */
        int32_t start_cycle;
        int32_t end_cycle;
        int32_t se;
        int32_t nw;
        struct Mock230SceneLoc* loc = script_active_loc(state);
        int east;
        int south;
        int west;
        int north;

        if( !SSVM_PopInt(state, &nw) )
            return 1;
        if( !SSVM_PopInt(state, &se) )
            return 1;
        if( !SSVM_PopInt(state, &end_cycle) )
            return 1;
        if( !SSVM_PopInt(state, &start_cycle) )
            return 1;
        if( !loc )
        {
            SSVM_Abort(state, "p_locmerge with no active loc");
            return 1;
        }
        east = coord_x(se) - loc->x;
        south = coord_z(se) - loc->z;
        west = coord_x(nw) - loc->x;
        north = coord_z(nw) - loc->z;
        mock230_zone_loc_merge(
            srv, loc->x, loc->z, loc->level, loc->shape, loc->angle, loc->loc_id,
            (int)start_cycle, (int)end_cycle, player->pid, east, south, west, north);
        return 1;
    }

    /*
     * The overhead icons, as an int content owns outright.
     *
     * This is the reference's whole prayer surface in its engine: `Player.ts`
     * has a `headicons: number` field, `PlayerOps.ts` has a get and a set, and
     * nothing anywhere in it knows what a prayer is. Which icon a prayer draws,
     * and when, is `~headicon_add`/`~headicon_del` in content.
     *
     * The write marks the appearance block, because that is where the byte
     * rides — turning on Protect from Melee is an appearance change like
     * putting on a helmet, and every client that can see the player learns
     * about it through the PLAYER_INFO they were getting anyway.
     */
    case SS_OP_HEADICONS_GET:
        SSVM_PushInt(state, player->headicons);
        return 1;

    case SS_OP_HEADICONS_SET:
    {
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        if( player->headicons != (int)value )
        {
            player->headicons = (int)value;
            player->masks |= MOCK230_PMASK_APPEARANCE;
        }
        return 1;
    }

    default:
        /* Not ours. The VM reports it through the loud stub, which pops and
         * pushes what the signature declares so the script survives. */
        return 0;
    }
}

/**
 * Release a p_countdialog wait with the number the client sent.
 *
 * Separate from mock230_scripts_resume_button because the two waits are
 * released by different packets and neither may release the other — a click
 * arriving while a count dialog is up must leave the script parked.
 */
int
mock230_scripts_resume_countdialog(
    struct Mock230Server* srv,
    int32_t value)
{
    struct SSVM_State* state = srv->active_player->active_script;

    if( !srv->scripts_ok || !state )
        return 0;
    if( state->execution != SSVM_COUNTDIALOG )
        return 0;
    srv->active_player->last_int = value;
    rebind_active_npc(srv, state);
    return run_or_park(srv, state);
}

/*
 * Say a content-owned message.
 *
 * A thin wrapper over run_proc/run_proc_sv, because the alternative at twenty
 * call sites is twenty four-line blocks declaring an argument array. `name` is
 * the bare proc name — "equip_message", not "[proc,equip_message]" — since
 * every caller of this is naming a message and the brackets are noise.
 *
 * Silent when the script is missing: a server with no content tree says nothing
 * rather than falling back to a second copy of the text in C, which is the
 * arrangement that let the two disagree in the first place.
 */
void
mock230_say(
    struct Mock230Server* srv,
    const char* name,
    const char* arg)
{
    char qualified[128];

    snprintf(qualified, sizeof(qualified), "[proc,%s]", name);
    if( arg )
        mock230_scripts_run_proc_sv(srv, qualified, NULL, 0, &arg, 1);
    else
        mock230_scripts_run_proc(srv, qualified, NULL, 0);
}

/*
 * Run a named proc with an npc made active.
 *
 * The combat swing needs it: `[proc,player_melee_swing]` calls `npc_stat`,
 * `npc_param` and `npc_damage`, all of which resolve against the active npc, so
 * the engine names the target once here rather than passing a slot number
 * content would then have to carry through four procs.
 *
 * `host_tag` is the slot, stored +1 so zero means "no npc" without a second
 * flag; the entity pointer only satisfies the VM's require-an-active-npc check.
 * Both are needed — see mock230_scripts_run_trigger, which does the same pair.
 */
int
mock230_scripts_run_hook_on_npc(
    struct Mock230Server* srv,
    const struct SSVM_Script* script,
    int npc_slot)
{
    struct SSVM_State* state;

    if( !srv->scripts_ok || !script )
        return 0;
    state = SSVM_StateAlloc(srv->script_env, script, NULL, 0, NULL, 0);
    if( !state )
        return 0;
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot >= 0 && npc_slot < MOCK230_NPC_MAX && srv->npcs[npc_slot].active )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }
    return run_or_park(srv, state);
}

/** By name, for tests. See mock230_scripts_run_proc_sv on why the split. */
int
mock230_scripts_run_proc_on_npc(
    struct Mock230Server* srv,
    const char* name,
    int npc_slot)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            printf("mock230: no %s — engine fallback\n", name);
        return 0;
    }
    return mock230_scripts_run_hook_on_npc(srv, script, npc_slot);
}
