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

#include "ss_meta.h"
#include "ss_opcode.h"
#include "ssvm.h"

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
    return srv->scripts->loaded;
}

void
mock230_scripts_free(struct Mock230Server* srv)
{
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
    if( srv->player.active_script == state )
        srv->player.active_script = NULL;
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
        if( srv->player.active_script && srv->player.active_script != state )
        {
            fprintf(stderr, "mock230: dropping a script that suspended while another waits\n");
            SSVM_StateRelease(state);
            return 0;
        }
        srv->player.active_script = state;
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

    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, &srv->player);
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
    struct SSVM_State* state = srv->player.active_script;

    if( !state || !srv->scripts_ok )
        return;
    /* A resume-button or count-dialog wait is released by client input, not by
     * the clock, so the tick must leave those alone. */
    if( state->execution != SSVM_SUSPENDED )
        return;
    if( srv->tick < srv->player.delayed_until )
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
        struct Mock230Queued* entry = &srv->player.queue[i];
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
        struct Mock230Timer* timer = &srv->player.timers[i];

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
    struct Mock230Player* player = &srv->player;
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
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, &srv->player);
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

/** Container by the id scripts name it with (93 backpack, 94 worn). */
static struct Mock230Item*
container_for(
    struct Mock230Server* srv,
    int32_t inv_id,
    int* out_slots)
{
    if( inv_id == MOCK230_INV_BACKPACK )
    {
        *out_slots = MOCK230_INV_SLOTS;
        return srv->player.inv;
    }
    if( inv_id == MOCK230_INV_WORN )
    {
        *out_slots = MOCK230_WORN_SLOTS;
        return srv->player.worn;
    }
    *out_slots = 0;
    return NULL;
}

static void
mark_varp_changed(struct Mock230Player* player, int varp)
{
    for( int i = 0; i < player->varp_changed_count; i++ )
    {
        if( player->varp_changed[i] == varp )
            return;
    }
    if( player->varp_changed_count < MOCK230_VARP_DIRTY_MAX )
        player->varp_changed[player->varp_changed_count++] = varp;
}

int
mock230_script_command(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    struct Mock230Player* player = &srv->player;

    (void)dot;

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
        snprintf(npc->say, sizeof(npc->say), "%s", text);
        npc->masks |= MOCK230_NMASK_SAY;
        /* Face the player while speaking, the way the reference's
         * playerfaceclose mode would. */
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
            if( values[0] == MOCK230_INV_BACKPACK )
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
            if( values[0] == MOCK230_INV_BACKPACK )
                player->inv_dirty |= 1u << i;
            else
                player->worn_dirty |= 1u << i;
        }
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
        if( player->varps[varp] != value )
        {
            player->varps[varp] = value;
            mark_varp_changed(player, varp);
        }
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

    case SS_OP_OC_STACKABLE:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushInt(state, mock230_objinfo(obj_id)->stackable);
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

    default:
        /* Not ours. The VM reports it through the loud stub, which pops and
         * pushes what the signature declares so the script survives. */
        return 0;
    }
}
