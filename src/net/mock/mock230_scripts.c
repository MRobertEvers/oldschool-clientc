/*
 * Script container binding and the host command seam.
 *
 * This is the only file that knows both `struct Mock230Server` and the VM. The
 * VM stays engine-agnostic — it holds active entities as void* and never
 * dereferences them — so everything that touches game state funnels through
 * mock230_script_command below.
 *
 * The fallback contract matters as much as the feature: when no script pack is
 * loaded, or when a trigger has no script, every call site does exactly what it
 * did before scripts existed. A missing or broken toolchain degrades the mock
 * rather than breaking it.
 */

#include "mock230.h"

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
        /* Not fatal, and deliberately not silent: running without content is a
         * supported mode, but doing so by accident is a confusing afternoon. */
        fprintf(stderr, "mock230: no scripts from %s (%s)\n", dir, err.message);
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
    /* Same reasoning, applied to names instead of opcodes — see the header on
     * mock230_scripts_resolve_hooks. */
    mock230_scripts_resolve_hooks(srv);
    return srv->scripts->loaded;
}

/* ------------------------------------------------------------------ */
/* Engine hooks                                                        */
/* ------------------------------------------------------------------ */

/*
 * The scripts the engine starts, resolved at load time rather than at call time.
 *
 * Same argument as `mock230_scripts_report_gaps` above it, one level up: a name
 * this tree does not define is a fact about the tree, and waiting for a player
 * to trigger it is waiting for a silent no-op nobody attributes to a rename.
 * `struct Mock230Hooks` has the rest of the reasoning.
 *
 * Missing is reported and left NULL — the run helpers treat a NULL hook exactly
 * as they treated an absent name, so a tree that does not want a hook still
 * runs. What changes is that it says so once, at boot, with every other
 * unresolved symbol, instead of never.
 */
int
mock230_scripts_resolve_hooks(struct Mock230Server* srv)
{
    /*
     * `offsetof` into the hook table for the same reason `mock230_servercodec.c`
     * uses it: a row is `name -> field`, and writing this as a switch or as ten
     * assignments is how a name comes to be resolved into the wrong field. The
     * struct is all one pointer type, so the offsets are uniform.
     */
    static const struct
    {
        const char* name;
        size_t offset;
    } k_hooks[] = {
#define HOOK(field, script_name) { script_name, offsetof(struct Mock230Hooks, field) }
        HOOK(player_death, "[queue,player_death]"),
        HOOK(combat_defend_anim, "[proc,combat_defend_anim]"),
        HOOK(combat_levelup_message, "[proc,combat_levelup_message]"),
        HOOK(player_melee_swing, "[proc,player_melee_swing]"),
        HOOK(npc_meleeattack, "[proc,npc_meleeattack]"),
        HOOK(combat_weapon_type, "[proc,combat_weapon_type]"),
        HOOK(equip_level_message, "[proc,equip_level_message]"),
        HOOK(equipment_refresh, "[proc,equipment_refresh]"),
        HOOK(equipment_open, "[proc,equipment_open]"),
#undef HOOK
    };
    int missing = 0;

    memset(&srv->hooks, 0, sizeof(srv->hooks));
    if( !srv->scripts_ok )
        return (int)(sizeof(k_hooks) / sizeof(k_hooks[0]));

    for( size_t i = 0; i < sizeof(k_hooks) / sizeof(k_hooks[0]); i++ )
    {
        const struct SSVM_Script* script =
            SSVM_ProviderGetByName(srv->scripts, k_hooks[i].name);

        memcpy((char*)&srv->hooks + k_hooks[i].offset, &script, sizeof(script));
        if( !script )
        {
            fprintf(stderr, "mock230: engine hook %s is not in the pack\n", k_hooks[i].name);
            missing++;
        }
    }
    if( missing )
        fprintf(stderr,
                "mock230: %d engine hook(s) unresolved — the engine will fall back to "
                "doing nothing at each\n",
                missing);
    return missing;
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
     */
    srv->player->active_script = NULL;
    srv->player->resume_button_count = 0;

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
    if( srv->player->active_script == state )
        srv->player->active_script = NULL;
    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active_script == state )
            srv->npcs[i].active_script = NULL;
    }
    SSVM_StateRelease(state);
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
    enum SSVM_Exec status = SSVM_Execute(state);

    switch( status )
    {
    case SSVM_FINISHED:
        release_parked(srv, state);
        return 1;

    case SSVM_ABORTED:
        fprintf(stderr, "mock230: %s", SSVM_Backtrace(state));
        release_parked(srv, state);
        return 0;

    case SSVM_SUSPENDED:
    case SSVM_PAUSEBUTTON:
    case SSVM_COUNTDIALOG:
        /* One parked script per player. A second would need somewhere to live
         * and, more importantly, would let two scripts interleave writes to the
         * same player — the reference has the same single slot. */
        if( srv->player->active_script && srv->player->active_script != state )
        {
            fprintf(stderr, "mock230: dropping a script that suspended while another waits\n");
            SSVM_StateRelease(state);
            return 0;
        }
        srv->player->active_script = state;
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

/** Start a script by id, with an optional argument, on behalf of the player. */
static int
run_script_id(
    struct Mock230Server* srv,
    int script_id,
    int32_t arg,
    int has_arg,
    int npc_slot)
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

    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->player);
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

void
mock230_scripts_resume_player(struct Mock230Server* srv)
{
    struct SSVM_State* state = srv->player->active_script;

    if( !state || !srv->scripts_ok )
        return;
    /* A resume-button or count-dialog wait is released by client input, not by
     * the clock, so the tick must leave those alone. */
    if( state->execution != SSVM_SUSPENDED )
        return;
    if( srv->tick < srv->player->delayed_until )
        return;

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

void
mock230_scripts_process_queues(struct Mock230Server* srv)
{
    if( !srv->scripts_ok )
        return;

    for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
    {
        struct Mock230Queued* entry = &srv->player->queue[i];
        int script_id;
        int32_t arg;

        if( !entry->active )
            continue;
        if( --entry->delay > 0 )
            continue;

        script_id = entry->script_id;
        arg = entry->arg;
        entry->active = 0;
        run_script_id(srv, script_id, arg, 1, -1);
    }
}

void
mock230_scripts_process_timers(struct Mock230Server* srv)
{
    if( !srv->scripts_ok )
        return;

    for( int i = 0; i < MOCK230_TIMER_MAX; i++ )
    {
        struct Mock230Timer* timer = &srv->player->timers[i];

        if( !timer->active || timer->interval <= 0 )
            continue;
        if( ++timer->clock < timer->interval )
            continue;
        timer->clock = 0;
        run_script_id(srv, timer->script_id, 0, 0, -1);
    }
}

void
mock230_scripts_process_npc_timer(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc;

    if( !srv->scripts_ok || slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( npc->timer_script < 0 || npc->timer_interval <= 0 )
        return;
    if( ++npc->timer_clock < npc->timer_interval )
        return;
    npc->timer_clock = 0;
    run_script_id(srv, npc->timer_script, 0, 0, slot);
}

int
mock230_scripts_resume_button(
    struct Mock230Server* srv,
    int component_uid)
{
    struct Mock230Player* player = srv->player;
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
        return run_or_park(srv, state);
    }
    return 0;
}

int
mock230_scripts_close_dialogue(struct Mock230Server* srv)
{
    struct Mock230Player* player;
    struct SSVM_State* state;

    if( !srv || !srv->player )
        return 0;
    player = srv->player;
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
    return run_script_id(srv, script_id, 0, 0, -1);
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

    /* NULL is an unresolved hook, which `mock230_scripts_resolve_hooks` has
     * already named at boot. Doing nothing here is the same fallback an absent
     * name got, minus the silence. */
    if( !srv->scripts_ok || !script )
        return 0;

    state = SSVM_StateAlloc(srv->script_env, script, args, argc, strv, strc);
    if( !state )
    {
        fprintf(stderr, "mock230: %s rejected %d argument(s)\n", script->name, argc);
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    return run_or_park(srv, state);
}

/*
 * The by-name form, which is for tests.
 *
 * A test naming the script it tests is stating its subject; the engine naming
 * one is authoring content (§8.6). So the lookup lives here rather than at the
 * call sites, and the engine goes through `srv->hooks`.
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
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->player);
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
 * An unresolved hook queues nothing. That used to be an *absent name*, silent
 * except under MOCK230_VERBOSE, and it was described here as the documented
 * fallback for this server — which it should not have been. A queue that never
 * fires is not a graceful degradation when the queue is `[queue,player_death]`:
 * it is a player who dies and stays a corpse. The name is resolved at load now
 * (`mock230_scripts_resolve_hooks`), so the miss is reported once, at boot,
 * whether or not anyone dies afterwards.
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

    player = srv->player;
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
        return 1;
    }
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

int
mock230_scripts_run_trigger(
    struct Mock230Server* srv,
    int trigger,
    int type,
    int category,
    int npc_slot)
{
    const struct SSVM_Script* script;
    struct SSVM_State* state;

    if( !srv->scripts_ok )
        return 0;

    script = SSVM_ProviderGetByTrigger(srv->scripts, trigger, type, category);
    if( !script )
        return 0;

    state = SSVM_StateAlloc(srv->script_env, script, NULL, 0, NULL, 0);
    if( !state )
    {
        fprintf(stderr, "mock230: %s expects arguments a trigger cannot supply\n",
                script->name);
        return 0;
    }

    /* Every trigger the mock fires is on behalf of the one player, and the
     * engine grants protected access because these all arrive as a direct
     * response to player input. */
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);

    if( npc_slot >= 0 && npc_slot < MOCK230_NPC_MAX && srv->npcs[npc_slot].active )
    {
        /* The pointer satisfies the VM's require-an-active-npc check; the slot
         * in host_tag is what actually resolves it. Stored +1 so zero means
         * "no npc" without a separate flag. */
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }

    return run_or_park(srv, state);
}

int
mock230_scripts_run_if_button_named(
    struct Mock230Server* srv,
    int uid)
{
    const struct SSVM_Script* script;
    struct SSVM_State* state;
    const char* component;
    char name[192];

    if( !srv->scripts_ok )
        return 0;

    component = mock230_content_symbol_name(MOCK230_PACK_COMPONENT, uid);
    if( !component )
    {
        if( srv->verbose )
            fprintf(stderr, "mock230: if_button %d:%d has no component name\n", uid >> 16,
                    uid & 0xffff);
        return 0;
    }

    snprintf(name, sizeof(name), "[if_button,%s]", component);
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( srv->verbose )
        fprintf(stderr, "mock230: if_button named lookup `%s` -> %s\n", name,
                script ? "found" : "missing");
    if( !script )
        return 0;

    state = SSVM_StateAlloc(srv->script_env, script, NULL, 0, NULL, 0);
    if( !state )
    {
        fprintf(stderr, "mock230: %s expects arguments a trigger cannot supply\n",
                script->name);
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    return run_or_park(srv, state);
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

/** Container by the id scripts name it with (93 backpack, 94 worn, 95 bank). */
static struct Mock230Item*
container_for(
    struct Mock230Server* srv,
    int32_t inv_id,
    int* out_slots)
{
    if( inv_id == mock230_ids()->inv_backpack )
    {
        *out_slots = MOCK230_INV_SLOTS;
        return srv->player->inv;
    }
    if( inv_id == mock230_ids()->inv_worn )
    {
        *out_slots = MOCK230_WORN_SLOTS;
        return srv->player->worn;
    }
    if( inv_id == mock230_ids()->inv_bank )
    {
        *out_slots = srv->player->bank.size;
        return srv->player->bank.slots;
    }
    *out_slots = 0;
    return NULL;
}

/** Mark a container for this tick's transmit. The backpack and worn set carry
 *  per-slot dirty bits; the bank is re-sent whole. */
static void
container_dirty(
    struct Mock230Server* srv,
    int32_t inv_id,
    int slot)
{
    if( inv_id == mock230_ids()->inv_backpack && slot >= 0 && slot < MOCK230_INV_SLOTS )
        srv->player->inv_dirty |= 1u << slot;
    else if( inv_id == mock230_ids()->inv_worn && slot >= 0 && slot < MOCK230_WORN_SLOTS )
    {
        srv->player->worn_dirty |= 1u << slot;
        srv->player->masks |= MOCK230_PMASK_APPEARANCE;
    }
    else if( inv_id == mock230_ids()->inv_bank )
        srv->player->bank.dirty = 1;
}

/*
 * Push a param onto the stack its *declaration* calls for.
 *
 * Shared by `oc_param` and `nc_param` because the choice of stack is the entire
 * difficulty of the `runtime_typed` family and it does not vary by table — only
 * the lookup does. Duplicating it would mean two places for the declared-vs-
 * stored disagreement to be handled differently, and that disagreement is the
 * one thing here worth being loud about.
 *
 * `sval`/`ival` are the stored value and `present` says whether the record
 * carried the param at all; a caller with no row passes present = 0 and the
 * other two are ignored.
 */
static void
push_typed_param(
    struct SSVM_State* state,
    int param_id,
    const char* sval,
    int32_t ival,
    int present,
    const char* record_kind,
    int record_id)
{
    char declared = mock230_content_param_type(param_id);

    /*
     * Which stack the result goes on is decided by the declaration, because
     * that is what the script was compiled against: a script that wrote
     * `nc_param($npc, some_string_param)` has a string-typed local waiting for
     * it, and pushing an int would leave the two stacks out of step for the
     * rest of the script rather than fail here.
     *
     * 1,517 of cache.osrs239's 2,634 param records declare no type at all — the
     * config's type opcode is optional. For those the *value's own* stored kind
     * is the answer and it is not a guess: the record says whether it wrote
     * four bytes or a NUL-terminated string.
     */
    if( !declared && present )
        declared = sval ? 's' : 'i';

    if( declared == 's' )
    {
        if( present && !sval )
        {
            /* Declared a string, stored as an int. The record is wrong, and
             * which half to believe is not this opcode's call to make. */
            SSVM_Abort(state, "param %d is declared a string but %s %d stores an int",
                       param_id, record_kind, record_id);
            return;
        }
        /* `defaultstr=` (311 declared) is still read by nothing, so an absent
         * string param reports "" where the reference reports the declared
         * string — docs/osrs230_mockserver.md records the gap. */
        SSVM_PushStr(state, present ? sval : "");
        return;
    }

    if( present && sval )
    {
        SSVM_Abort(state, "param %d is declared an int but %s %d stores a string",
                   param_id, record_kind, record_id);
        return;
    }
    /*
     * A record that does not carry the param answers with the param's declared
     * `default=`, never an abort — LostCity's handlers push
     * `paramType.defaultInt` (ObjConfigOps.ts), and content relies on both
     * halves of that: `oc_param($obj, specwep) = ^true` is asked of every
     * weapon and only special-attack weapons carry the row (specwep declares
     * no default, so absent reads 0), while 365 params declare `default=-1`
     * precisely so that absence spells "no id" rather than obj 0.
     */
    SSVM_PushInt(state, present ? ival : mock230_content_param_default(param_id));
}

int
mock230_script_command(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    struct Mock230Player* player = srv->player;

    (void)dot;

    /* Per-domain handlers first. Each returns 1 when it owns the opcode; see the
     * note on mock230_ops_db in mock230.h for why the split grows this way. */
    if( mock230_ops_db(state, opcode, dot) )
        return 1;

    switch( opcode )
    {
    /* ---- messaging ------------------------------------------------ */

    case SS_OP_MES:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        mock230_send_message(srv, text);
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
            mock230_send_message(srv, line);
        }
        /* Facing the player is real and does render, so keep it. */
        npc->face_entity = MOCK230_FACE_LOCAL_PLAYER;
        npc->masks |= MOCK230_NMASK_FACE_ENTITY;
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

    case SS_OP_P_TELEPORT:
    case SS_OP_P_TELEJUMP:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        player->x = coord_x(coord);
        player->z = coord_z(coord);
        player->level = coord_level(coord);
        /* The next PLAYER_INFO has to carry an absolute placement rather than a
         * step direction, and the scene may need re-centring around the new
         * position — both of which the tick handles off place_dirty. */
        player->place_dirty = 1;
        player->step_count = 0;
        player->step_head = 0;
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
     * `mode` is `lineofwalk` / `lineofsight` / `none`, and only `none` is
     * honoured: the other two require a reachability test from the source tile,
     * which this server has no cheap form of. Rather than silently treating
     * them as `none` — a monster teleporting through a wall, occasionally, for
     * no visible reason — an unsupported mode reports and falls back, so the
     * gap is in the log rather than in the world.
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

        if( values[3] != 2 /* ^map_findsquare_none */ && srv->verbose )
            fprintf(stderr,
                    "mock230: map_findsquare mode %d is not implemented — using `none`\n",
                    values[3]);

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

    case SS_OP_MOVECOORD:
    {
        int32_t values[4];

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        SSVM_PushInt(state,
                     coord_pack(coord_level(values[0]) + values[3],
                                coord_x(values[0]) + values[1],
                                coord_z(values[0]) + values[2]));
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
        int slots = 0;
        struct Mock230Item* items;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        items = container_for(srv, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_add on unknown container %d", values[0]);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            if( items[i].obj_id >= 0 )
                continue;
            items[i].obj_id = values[1];
            items[i].count = values[2];
            if( values[0] == mock230_ids()->inv_backpack )
                player->inv_dirty |= 1u << i;
            else
                player->worn_dirty |= 1u << i;
            return 1;
        }
        /* No free slot. The reference drops the item on the ground; the mock
         * has no ground objects yet, so it says so rather than pretending. */
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
            if( values[0] == mock230_ids()->inv_backpack )
                player->inv_dirty |= 1u << i;
            else
                player->worn_dirty |= 1u << i;
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
        if( values[0] == mock230_ids()->inv_backpack )
            player->inv_dirty |= 1u << values[1];
        else
            player->worn_dirty |= 1u << values[1];
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
        for( int i = 0; items && i < slots; i++ )
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
        for( int i = 0; items && i < slots; i++ )
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
        for( int i = 0; items && i < slots; i++ )
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
     * Config queries.
     *
     * Every one of these is a read off a table the boot loaders already decoded
     * — `Mock230ObjInfo`, `Mock230NpcInfo`, the content packs — which is the
     * reason this batch is safe to add in bulk. `oc_cost`, `oc_members`,
     * `oc_tradeable` and `oc_desc`/`nc_desc` are still absent — simply not
     * decoded; the obj record's examine text is read by nothing here, and a
     * dat2 npc record has no description at all (it is server-driven at this
     * revision).
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
            /* +1 so delay 0 means "next tick", matching `queue` and `p_delay`. */
            npc->queue[i].delay = delay + 1;
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
        if( uid != 1 || !srv->player )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->player);
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
        SSVM_PushInt(state, srv->player ? srv->player->gender : 0);
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
                     (srv->player && srv->player->gender) ? (female ? female : "")
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
        (void)checkvis; /* No line of sight here — see npc_find. */

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
        (void)checkvis;

        /*
         * `huntall` collects *players* in range — `[proc,sound_area]` uses it to
         * play a sound to everyone who can hear it. `MOCK230_PLAYER_MAX` is 1
         * (§6.1), so this finds at most one, and it is written as a loop over
         * the pool anyway: the day a second player exists this is already
         * right, where a hardcoded `srv->player` would be one more place to find.
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
         * `checkvis` asks for line of sight. There is no LOS here — the scene
         * has collision but nothing projects a ray through it — so the flag is
         * accepted and ignored rather than refused. Content uses it to avoid
         * addressing an npc through a wall; here it will occasionally find one
         * it should not, which is a wrong answer in the direction of doing
         * something rather than nothing.
         */
        (void)checkvis;

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
        npc->tracked = 0;
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
        npc->x = coord_x(coord);
        npc->z = coord_z(coord);
        npc->level = coord_level(coord);
        /* A teleport is not a step. Leaving `step_dir` set would make the
         * client walk the npc there, which for any distance reads as the npc
         * sliding across the map. */
        npc->step_dir = -1;
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
        int slot;
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
        slot = (int)((intptr_t)SSVM_ActiveSlot(state, SSVM_ENT_LOC, SSVM_PRIMARY)) - 1;
        was_id = loc->loc_id;
        shape = loc->shape;
        angle = loc->angle;
        x = loc->x;
        z = loc->z;
        level = loc->level;

        if( !mock230_scene_replace_loc(slot, loc_id, angle) )
        {
            SSVM_Abort(state, "loc_change to %d, which is not in the cache", loc_id);
            return 1;
        }
        mock230_send_loc_add_change(srv, mock230_send_zone(srv, x, z), shape, angle, loc_id);
        mock230_world_loc_revert_queue(srv, slot, duration, was_id, shape, angle, x, z, level);
        return 1;
    }

    case SS_OP_LOC_DEL:
    {
        int32_t duration;
        struct Mock230SceneLoc* loc = script_active_loc(state);
        int slot;
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
        slot = (int)((intptr_t)SSVM_ActiveSlot(state, SSVM_ENT_LOC, SSVM_PRIMARY)) - 1;
        was_id = loc->loc_id;
        shape = loc->shape;
        angle = loc->angle;
        x = loc->x;
        z = loc->z;
        level = loc->level;

        if( !mock230_scene_remove_loc(slot) )
        {
            SSVM_Abort(state, "loc_del on a loc that is already gone");
            return 1;
        }
        mock230_send_loc_del(srv, mock230_send_zone(srv, x, z), shape, angle);
        mock230_world_loc_revert_queue(srv, slot, duration, was_id, shape, angle, x, z, level);
        return 1;
    }

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
        if( !SSVM_PopInt(state, &angle) )
            return 1;
        if( !SSVM_PopInt(state, &shape) )
            return 1;
        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        slot = mock230_scene_add_loc(coord_x(coord), coord_z(coord), coord_level(coord),
                                     loc_id, shape, angle);
        if( slot < 0 )
        {
            SSVM_Abort(state, "loc_add %d at %d,%d failed — unknown loc or outside the scene",
                       loc_id, coord_x(coord), coord_z(coord));
            return 1;
        }
        mock230_send_loc_add_change(srv, mock230_send_zone(srv, coord_x(coord), coord_z(coord)),
                                    shape, angle, loc_id);
        /* -1 says "remove it again" rather than "put something back". */
        mock230_world_loc_revert_queue(srv, slot, duration, -1, shape, angle, coord_x(coord),
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
        push_typed_param(state, param_id, row ? row->sval : NULL, row ? row->ival : 0,
                         row != NULL, "obj", obj_id);
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
        push_typed_param(state, param_id, row ? row->sval : NULL, row ? row->ival : 0,
                         row != NULL, "npc", npc_id);
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
        player->face_x = coord_x(coord);
        player->face_z = coord_z(coord);
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
        npc->face_x = coord_x(coord);
        npc->face_z = coord_z(coord);
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
        mock230_send_if_settext(srv, uid, text);
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
        mock230_send_if_setnpchead(srv, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_SETPLAYERHEAD:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        mock230_send_if_setplayerhead(srv, uid);
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
        mock230_send_if_setanim(srv, values[0], values[1]);
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
        mock230_send_if_sethide(srv, values[0], values[1]);
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
        mock230_send_if_opensub(srv, MOCK230_COM_GROUP(slot), MOCK230_COM_CHILD(slot),
                                group, 0);
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
        mock230_send_if_setevents(srv, (int)com, (int)from, (int)to, (int)events);
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
        mock230_send_if_opensub(srv, MOCK230_COM_GROUP(com), MOCK230_COM_CHILD(com),
                                (int)group, (int)type);
        return 1;
    }

    case SS_OP_IF_CLOSE:
        /* Unmounting is the whole message: the same on_sub_change hook that
         * hid `chatbox:chatdisplay` on the way in brings it back when the
         * modal has no sub again (script908's else branch). */
        mock230_send_if_closesub(srv, mock230_ids()->com_chatbox_modal);
        player->resume_button_count = 0;
        return 1;

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
        mock230_send_run_clientscript_mixed(srv, (int)script_id, "ss", NULL, argv, 2);
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
        mock230_send_if_setevents(srv, uid, 0, MOCK230_RESUME_SUB_MAX, MOCK230_EVENT_CLICK);
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

        if( !SSVM_PopInt(state, &op_num) )
            return 1;
        if( slot < 0 )
        {
            SSVM_Abort(state, "p_opnpc with no active npc");
            return 1;
        }
        /* Op 2 is Attack on every combat npc in this cache. Anything else is a
         * non-combat interaction the mock has no model for yet, so it walks
         * over and stops rather than pretending. */
        if( op_num == 2 )
            mock230_combat_engage(srv, slot);
        else
            mock230_world_walk_beside(srv, srv->npcs[slot].x, srv->npcs[slot].z);
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

    case SS_OP_QUEUE:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
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
            /* +1 so delay 0 means "next tick", not "this one". */
            player->queue[i].delay = values[1] + 1;
            player->queue[i].arg = values[2];
            return 1;
        }
        SSVM_Abort(state, "the player's queue is full");
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
            slot->clock = 0;
        }
        return 1;
    }

    case SS_OP_CLEARTIMER:
    {
        int32_t script_id;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < MOCK230_TIMER_MAX; i++ )
        {
            if( player->timers[i].script_id == script_id )
                player->timers[i].active = 0;
        }
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
        if( srv->player && srv->player->session )
            mock230_session_kill(srv->player->session);
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

    /*
     * `npc_param(<param>)` reads a param off the active npc's type.
     *
     * Only the combat params exist here, and only because the drop tables need
     * `death_drop`. A param the content tree does not model pushes 0 rather
     * than aborting: the reference's own params default, and content that asks
     * for one this engine has never heard of should degrade rather than stop.
     */
    case SS_OP_NPC_PARAM:
    {
        struct Mock230Npc* npc = active_npc(state);
        int32_t param;

        if( !SSVM_PopInt(state, &param) )
            return 1;
        if( npc && npc->def &&
            param == mock230_content_symbol(MOCK230_PACK_PARAM, "death_drop") )
            SSVM_PushInt(state, npc->def->death_drop);
        else
            SSVM_PushInt(state, 0);
        return 1;
    }

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
        int slots = 0;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        (void)container_for(srv, inv_id, &slots);
        SSVM_PushInt(state, slots);
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
        if( !items || slot < 0 || slot >= slots || items[slot].obj_id < 0 )
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
        if( limit > 0 && limit < slots )
            slots = (int)limit;
        if( items )
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
        if( !from_items || !to_items || from_slot < 0 || from_slot >= from_slots ||
            to_slot < 0 || to_slot >= to_slots )
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

    /*
     * inv_moveitem / _cert / _uncert: the whole of deposit and withdraw.
     *
     * The cert variants are what make a bank hold one stack of an item rather
     * than two: depositing un-notes on the way in, and withdrawing notes on the
     * way out only if the player asked for it. Both directions go through
     * mock230_bank, which owns the space checks and their three messages.
     */
    case SS_OP_INV_MOVEITEM:
    case SS_OP_INV_MOVEITEM_CERT:
    case SS_OP_INV_MOVEITEM_UNCERT:
    {
        int32_t from_inv;
        int32_t to_inv;
        int32_t obj_id;
        int32_t count;

        if( !SSVM_PopInt(state, &count) || !SSVM_PopInt(state, &obj_id) ||
            !SSVM_PopInt(state, &to_inv) || !SSVM_PopInt(state, &from_inv) )
            return 1;
        if( from_inv == mock230_ids()->inv_bank )
        {
            int slot = -1;

            for( int i = 0; i < srv->player->bank.size; i++ )
                if( srv->player->bank.slots[i].obj_id == obj_id )
                    slot = i;
            if( slot < 0 )
                return 1;
            /* The opcode decides the form, not the bank's own note toggle:
             * `inv_moveitem_cert` means "as a note" wherever it is called
             * from. Set the flag, move, put it back. */
            {
                int saved = srv->player->bank.note_mode;

                srv->player->bank.note_mode = opcode == SS_OP_INV_MOVEITEM_CERT;
                mock230_bank_withdraw(srv, slot, (int)count);
                srv->player->bank.note_mode = saved;
            }
            return 1;
        }
        if( to_inv == mock230_ids()->inv_bank && from_inv == mock230_ids()->inv_backpack )
        {
            for( int i = 0; i < MOCK230_INV_SLOTS && count > 0; i++ )
            {
                if( srv->player->inv[i].obj_id != obj_id )
                    continue;
                count -= mock230_bank_deposit(srv, i, (int)count);
            }
            return 1;
        }
        if( to_inv == mock230_ids()->inv_bank && from_inv == mock230_ids()->inv_worn )
        {
            /* Straight off the body and into the bank, which is what the
             * deposit-worn button is. Going via the backpack would need a free
             * slot the player may not have. */
            for( int i = 0; i < MOCK230_WORN_SLOTS && count > 0; i++ )
            {
                if( srv->player->worn[i].obj_id != obj_id )
                    continue;
                count -= mock230_bank_deposit_worn(srv, i, (int)count);
            }
            return 1;
        }
        fprintf(stderr, "mock230: inv_moveitem %d -> %d is not modelled\n", (int)from_inv,
                (int)to_inv);
        return 1;
    }

    case SS_OP_INV_CLEAR:
    {
        int32_t inv_id;
        int slots = 0;
        struct Mock230Item* items = NULL;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, inv_id, &slots);
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
        int slots = 0;
        struct Mock230Item* items;

        if( !SSVM_PopInt(state, &component) || !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, inv_id, &slots);
        if( !items )
            return 1;
        if( inv_id == mock230_ids()->inv_bank )
        {
            /* Only the used prefix: UPDATE_INV_FULL clears everything past the
             * capacity it carries, so 1,208 empty slots cost nothing. */
            int used = 0;

            for( int i = 0; i < slots; i++ )
                if( items[i].obj_id >= 0 )
                    used = i + 1;
            slots = used;
            srv->player->bank.open = 1;
            srv->player->bank.dirty = 0;
        }
        mock230_send_inv_full(srv, (int)component, (int)inv_id, items, slots);
        return 1;
    }

    case SS_OP_INV_STOPTRANSMIT:
    {
        int32_t component;

        if( !SSVM_PopInt(state, &component) )
            return 1;
        if( MOCK230_COM_GROUP(component) == mock230_ids()->iface_bankmain )
            srv->player->bank.open = 0;
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
        mock230_send_if_opensub(srv, mock230_ids()->iface_gameframe,
                                MOCK230_COM_CHILD(mock230_ids()->com_gameframe_mainmodal),
                                (int)main_group, 0);
        mock230_send_if_opensub(srv, mock230_ids()->iface_gameframe,
                                MOCK230_COM_CHILD(mock230_ids()->com_gameframe_sidemodal),
                                (int)side_group, 3);
        return 1;
    }

    case SS_OP_IF_OPENMAIN:
    {
        int32_t group;

        if( !SSVM_PopInt(state, &group) )
            return 1;
        mock230_send_if_opensub(srv, mock230_ids()->iface_gameframe,
                                MOCK230_COM_CHILD(mock230_ids()->com_gameframe_mainmodal),
                                (int)group, 0);
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
        mock230_send_if_opencountdialog(srv);
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
    struct SSVM_State* state = srv->player->active_script;

    if( !srv->scripts_ok || !state )
        return 0;
    if( state->execution != SSVM_COUNTDIALOG )
        return 0;
    srv->player->last_int = value;
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
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->player);
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
