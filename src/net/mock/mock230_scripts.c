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
mock230_scripts_run_script(
    struct Mock230Server* srv,
    int script_id)
{
    if( !srv->scripts_ok )
        return 0;
    return run_script_id(srv, script_id, 0, 0, -1);
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

/* ------------------------------------------------------------------ */
/* Host commands                                                       */
/* ------------------------------------------------------------------ */

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
        npc->face_entity = MOCK230_PLAYER_TERMINATOR;
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
        mock230_send_message(srv, "Your inventory is full.");
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
     * reason this batch is safe to add in bulk. What is *not* here is as
     * deliberate: `oc_param`/`nc_param`/`lc_param` are `runtime_typed`, meaning
     * the param's declared type decides whether the result lands on the int
     * stack or the string stack, and no decoder here keeps a general per-record
     * param table to answer that from. `oc_cost`, `oc_members`, `oc_tradeable`
     * and `oc_desc`/`nc_desc` are simply not decoded — the obj record's examine
     * text is read by nothing here, and a dat2 npc record has no description at
     * all (it is server-driven at this revision).
     *
     * An opcode that cannot be answered from real data is better left to the
     * VM's loud stub than implemented with a plausible guess: the stub says so,
     * and a guess does not.
     */
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
        player->anim_id = values[0];
        player->anim_delay = values[1];
        player->masks |= MOCK230_PMASK_SEQUENCE;
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
        npc->anim_id = values[0];
        npc->anim_delay = values[1];
        npc->masks |= MOCK230_NMASK_ANIM;
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

        if( !SSVM_PopInt(state, &group) )
            return 1;
        /* Two packets, not one. 162:559 ships hidden=1, so mounting into
         * 162:561 alone produces a dialogue that is built correctly and never
         * drawn — which looks exactly like the mount having failed. */
        mock230_send_if_sethide(srv, MOCK230_CHAT_CONTAINER_UID, 0);
        mock230_send_if_opensub(srv, MOCK230_CHAT_SLOT_UID >> 16,
                                MOCK230_CHAT_SLOT_UID & 0xffff, group, 0);
        return 1;
    }

    case SS_OP_IF_CLOSE:
        mock230_send_if_closesub(srv, MOCK230_CHAT_SLOT_UID);
        mock230_send_if_sethide(srv, MOCK230_CHAT_CONTAINER_UID, 1);
        player->resume_button_count = 0;
        return 1;

    case SS_OP_IF_ADDRESUMEBUTTON:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        if( player->resume_button_count < MOCK230_RESUME_BUTTON_MAX )
            player->resume_buttons[player->resume_button_count++] = uid;
        /* Registering the button server-side is only half of it: at rev 230
         * nothing is clickable until the server says so, so the component's
         * events have to be enabled too or the player looks at a live-looking
         * prompt that swallows every click. Slot 0..0 with the click bit is
         * what a plain (non-grid) component needs. */
        mock230_send_if_setevents(srv, uid, 0, 0, MOCK230_EVENT_CLICK);
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

    case SS_OP_HEALENERGY:
    {
        int32_t amount;

        if( !SSVM_PopInt(state, &amount) )
            return 1;
        player->hitpoints += amount;
        if( player->hitpoints > player->max_hitpoints )
            player->hitpoints = player->max_hitpoints;
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
        SSVM_PushInt(state, MOCK230_ATTACK_RANGE);
        return 1;

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
    case SS_OP_PUSH_VARBIT:
    {
        int32_t varbit_id;

        if( !SSVM_PopInt(state, &varbit_id) )
            return 1;
        SSVM_PushInt(state, mock230_bank_get_varbit(srv, (int)varbit_id));
        return 1;
    }

    case SS_OP_POP_VARBIT:
    {
        int32_t varbit_id;
        int32_t value;

        if( !SSVM_PopInt(state, &varbit_id) || !SSVM_PopInt(state, &value) )
            return 1;
        mock230_bank_set_varbit(srv, (int)varbit_id, (int)value);
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
