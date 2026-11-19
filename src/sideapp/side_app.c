#include "sideapp/side_app.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <string.h>

struct SideAppEntry
{
    char const* name;
    void* self;
    struct SideAppHooks const* hooks;
    struct SideAppAction const* actions;
    int action_count;
};

struct SideApps
{
    struct SideAppBindingTable table;
    /* Per binding: the side-app and action it resolved to, -1 until the
     * side-app that answers its name registers. */
    int binding_app[SIDE_APP_BINDING_MAX];
    int binding_action[SIDE_APP_BINDING_MAX];
    struct SideAppEntry entries[SIDE_APP_MAX];
    int count;
};

/* Private to this unit, declared up front so definition order is free. */
static int
side_apps_answered(
    struct SideApps const* apps,
    char const* action);
static int
side_apps_unresolved_walk(
    struct SideApps const* apps,
    int want,
    struct SideAppUnresolved* out);
static int
side_apps_entry_has_phase(
    struct SideAppEntry const* entry,
    enum SideAppPhase phase);
static void
side_apps_deliver_action(
    struct SideApps* apps,
    int s,
    int a,
    struct SideAppHost* host,
    SideAppKeyFires fires,
    void* ctx);

struct SideApps*
SideApps_New(struct SideAppBindingTable const* bindings)
{
    assert(bindings);
    assert(bindings->binding_count >= 0);
    assert(bindings->binding_count <= SIDE_APP_BINDING_MAX);
    assert(bindings->declared_count >= 0);
    assert(bindings->declared_count <= SIDE_APP_DECLARED_MAX);

    struct SideApps* apps = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*apps));
    apps->table = *bindings;
    for( int i = 0; i < SIDE_APP_BINDING_MAX; i++ )
    {
        apps->binding_app[i] = -1;
        apps->binding_action[i] = -1;
    }
    return apps;
}

void
SideApps_Free(struct SideApps* apps)
{
    if( !apps )
        return;
    for( int i = apps->count - 1; i >= 0; i-- )
        if( apps->entries[i].hooks->free )
            apps->entries[i].hooks->free(apps->entries[i].self);
    ToriMem_Free(ToriMem_Process(), apps);
}

int
SideApps_Register(
    struct SideApps* apps,
    char const* name,
    void* self,
    struct SideAppHooks const* hooks,
    struct SideAppAction const* actions,
    int action_count)
{
    assert(apps);
    assert(name);
    assert(hooks);
    assert(actions);
    assert(action_count > 0);
    assert(apps->count < SIDE_APP_MAX);
    assert(SideApps_IndexOf(apps, name) < 0);

    int const index = apps->count;
    for( int a = 0; a < action_count; a++ )
    {
        assert(actions[a].name);
        assert(actions[a].phase >= 0);
        assert(actions[a].phase < SIDE_APP_PHASE_COUNT);
        /* A delivered action needs somewhere to be delivered to. */
        assert(actions[a].phase == SIDE_APP_PHASE_POLLED || hooks->key);
        /* One name, one answer: a second would be a binding firing two
         * tools. */
        assert(!side_apps_answered(apps, actions[a].name));
        for( int b = 0; b < a; b++ )
            assert(strcmp(actions[a].name, actions[b].name) != 0);
    }

    apps->entries[index].name = name;
    apps->entries[index].self = self;
    apps->entries[index].hooks = hooks;
    apps->entries[index].actions = actions;
    apps->entries[index].action_count = action_count;
    apps->count++;

    for( int i = 0; i < apps->table.binding_count; i++ )
    {
        if( apps->binding_app[i] >= 0 )
            continue;
        for( int a = 0; a < action_count; a++ )
            if( strcmp(apps->table.bindings[i].action, actions[a].name) == 0 )
            {
                apps->binding_app[i] = index;
                apps->binding_action[i] = a;
                break;
            }
    }
    return index;
}

int
SideApps_IndexOf(
    struct SideApps const* apps,
    char const* name)
{
    assert(apps);
    assert(name);
    for( int i = 0; i < apps->count; i++ )
        if( strcmp(apps->entries[i].name, name) == 0 )
            return i;
    return -1;
}

void
SideApps_DeliverKeys(
    struct SideApps* apps,
    enum SideAppPhase phase,
    struct SideAppHost* host,
    SideAppKeyFires fires,
    void* ctx)
{
    int have_rank = 0;
    int rank = 0;

    assert(apps);
    assert(phase >= 0);
    assert(phase < SIDE_APP_PHASE_POLLED);
    assert(host);
    assert(fires);

    /* One pass per rank of the phase, lowest first: each pass finds the next
     * rank above the one delivered and delivers every action of it. */
    for( ;; )
    {
        int found = 0;
        int next = 0;
        for( int s = 0; s < apps->count; s++ )
            for( int a = 0; a < apps->entries[s].action_count; a++ )
            {
                struct SideAppAction const* action = &apps->entries[s].actions[a];
                if( action->phase != phase || (have_rank && action->order <= rank) )
                    continue;
                if( !found || action->order < next )
                    next = action->order;
                found = 1;
            }
        if( !found )
            break;
        rank = next;
        have_rank = 1;
        for( int s = 0; s < apps->count; s++ )
            for( int a = 0; a < apps->entries[s].action_count; a++ )
                if( apps->entries[s].actions[a].phase == phase &&
                    apps->entries[s].actions[a].order == rank )
                    side_apps_deliver_action(apps, s, a, host, fires, ctx);
    }

    for( int s = 0; s < apps->count; s++ )
    {
        struct SideAppEntry const* entry = &apps->entries[s];
        if( entry->hooks->keys_done && side_apps_entry_has_phase(entry, phase) )
            entry->hooks->keys_done(entry->self, host, phase);
    }
}

void
SideApps_ProbeKeys(
    struct SideApps* apps,
    enum SideAppPhase phase,
    struct SideAppHost* host,
    SideAppKeyFires fires,
    void* ctx)
{
    assert(apps);
    assert(phase >= 0);
    assert(phase < SIDE_APP_PHASE_POLLED);
    assert(host);
    assert(fires);

    for( int s = 0; s < apps->count; s++ )
    {
        struct SideAppEntry const* entry = &apps->entries[s];
        int any_down = 0;
        if( !entry->hooks->keys_probe || !side_apps_entry_has_phase(entry, phase) )
            continue;
        for( int i = 0; i < apps->table.binding_count && !any_down; i++ )
        {
            struct SideAppAction const* action;
            if( apps->binding_app[i] != s )
                continue;
            action = &entry->actions[apps->binding_action[i]];
            if( action->phase == phase &&
                fires(ctx, &apps->table.bindings[i], action->gate, SIDE_APP_KEY_PRESS) )
                any_down = 1;
        }
        entry->hooks->keys_probe(entry->self, host, phase, any_down);
    }
}

void
SideApps_Frame(
    struct SideApps* apps,
    enum SideAppPhase phase,
    struct SideAppHost* host,
    struct LibToriRS_Input* input)
{
    assert(apps);
    assert(phase >= 0);
    assert(phase < SIDE_APP_PHASE_POLLED);
    assert(host);
    assert(input);

    for( int s = 0; s < apps->count; s++ )
        if( apps->entries[s].hooks->frame )
            apps->entries[s].hooks->frame(apps->entries[s].self, host, phase, input);
}

void
SideApps_BuildChrome(
    struct SideApps* apps,
    struct SideAppHost* host)
{
    assert(apps);
    assert(host);

    for( int s = 0; s < apps->count; s++ )
        if( apps->entries[s].hooks->build_chrome )
            apps->entries[s].hooks->build_chrome(apps->entries[s].self, host);
}

void
SideApps_DrawWorldOverlay(
    struct SideApps* apps,
    struct SideAppHost* host)
{
    assert(apps);
    assert(host);

    for( int s = 0; s < apps->count; s++ )
        if( apps->entries[s].hooks->world_overlay )
            apps->entries[s].hooks->world_overlay(apps->entries[s].self, host);
}

int
SideApps_AnyInspectsScenery(struct SideApps const* apps)
{
    assert(apps);

    for( int s = 0; s < apps->count; s++ )
        if( apps->entries[s].hooks->inspects_scenery &&
            apps->entries[s].hooks->inspects_scenery(apps->entries[s].self) )
            return 1;
    return 0;
}

int
SideApps_AnyHoldsKeyboard(struct SideApps const* apps)
{
    assert(apps);

    for( int s = 0; s < apps->count; s++ )
        if( apps->entries[s].hooks->holds_keyboard &&
            apps->entries[s].hooks->holds_keyboard(apps->entries[s].self) )
            return 1;
    return 0;
}

void
SideApps_MenuFlags(
    struct SideApps const* apps,
    void* build_ctx)
{
    assert(apps);
    assert(build_ctx);

    for( int s = 0; s < apps->count; s++ )
        if( apps->entries[s].hooks->menu_flags )
            apps->entries[s].hooks->menu_flags(apps->entries[s].self, build_ctx);
}

int
SideApps_MenuAction(
    struct SideApps* apps,
    struct SideAppHost* host,
    int action,
    void const* option)
{
    assert(apps);
    assert(host);
    assert(option);

    for( int s = 0; s < apps->count; s++ )
        if( apps->entries[s].hooks->menu_action &&
            apps->entries[s].hooks->menu_action(apps->entries[s].self, host, action, option) )
            return 1;
    return 0;
}

/* Deliver one action: the first of its bindings that fires calls the key
 * hook, once. */
static void
side_apps_deliver_action(
    struct SideApps* apps,
    int s,
    int a,
    struct SideAppHost* host,
    SideAppKeyFires fires,
    void* ctx)
{
    struct SideAppEntry const* entry = &apps->entries[s];
    struct SideAppAction const* action = &entry->actions[a];

    for( int i = 0; i < apps->table.binding_count; i++ )
    {
        if( apps->binding_app[i] != s || apps->binding_action[i] != a )
            continue;
        if( !fires(ctx, &apps->table.bindings[i], action->gate, action->mode) )
            continue;
        entry->hooks->key(entry->self, host, a, apps->table.bindings[i].args);
        break;
    }
}

/* Does the side-app answer an action of `phase`? */
static int
side_apps_entry_has_phase(
    struct SideAppEntry const* entry,
    enum SideAppPhase phase)
{
    for( int a = 0; a < entry->action_count; a++ )
        if( entry->actions[a].phase == phase )
            return 1;
    return 0;
}

int
SideApps_BindingCount(struct SideApps const* apps)
{
    assert(apps);
    return apps->table.binding_count;
}

struct SideAppBinding const*
SideApps_BindingAt(
    struct SideApps const* apps,
    int index)
{
    assert(apps);
    assert(index >= 0);
    assert(index < apps->table.binding_count);
    return &apps->table.bindings[index];
}

int
SideApps_BindingAnswers(
    struct SideApps const* apps,
    int index,
    int app,
    int action)
{
    assert(apps);
    assert(index >= 0);
    assert(index < apps->table.binding_count);
    return apps->binding_app[index] == app && apps->binding_action[index] == action;
}

int
SideApps_UnresolvedCount(struct SideApps const* apps)
{
    assert(apps);
    return side_apps_unresolved_walk(apps, -1, NULL);
}

struct SideAppUnresolved
SideApps_UnresolvedAt(
    struct SideApps const* apps,
    int index)
{
    struct SideAppUnresolved out = { 0 };

    assert(apps);
    assert(index >= 0);
    assert(index < SideApps_UnresolvedCount(apps));
    side_apps_unresolved_walk(apps, index, &out);
    return out;
}

/* Does any registered side-app answer `action`? */
static int
side_apps_answered(
    struct SideApps const* apps,
    char const* action)
{
    for( int s = 0; s < apps->count; s++ )
        for( int a = 0; a < apps->entries[s].action_count; a++ )
            if( strcmp(apps->entries[s].actions[a].name, action) == 0 )
                return 1;
    return 0;
}

/* Walk the unresolved names in their reported order; fill `out` with the
 * `want`-th one. Returns how many there are. Cold: the boot asks once. */
static int
side_apps_unresolved_walk(
    struct SideApps const* apps,
    int want,
    struct SideAppUnresolved* out)
{
    int n = 0;

    for( int d = 0; d < apps->table.declared_count; d++ )
    {
        struct SideAppDeclaredAction const* decl = &apps->table.declared[d];
        if( side_apps_answered(apps, decl->action) )
            continue;
        if( n == want )
        {
            out->action = decl->action;
            out->source = decl->source;
        }
        n++;
    }
    for( int i = 0; i < apps->table.binding_count; i++ )
    {
        char const* action = apps->table.bindings[i].action;
        int seen = 0;
        if( apps->binding_app[i] >= 0 )
            continue;
        for( int d = 0; d < apps->table.declared_count && !seen; d++ )
            if( strcmp(apps->table.declared[d].action, action) == 0 )
                seen = 1;
        for( int j = 0; j < i && !seen; j++ )
            if( strcmp(apps->table.bindings[j].action, action) == 0 )
                seen = 1;
        if( seen )
            continue;
        if( n == want )
        {
            out->action = action;
            out->source = "";
        }
        n++;
    }
    return n;
}
