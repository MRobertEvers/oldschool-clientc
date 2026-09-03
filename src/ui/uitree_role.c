#include "uitree_role.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* The vocabulary                                                            */
/* ------------------------------------------------------------------------ */

/*
 * The role spelling of enum UITreeFrameSlot.
 *
 * A table and not a switch because it is read in both directions -- a profile
 * writes `slot(chat_buttons, report)` and a diagnostic prints the name back --
 * and because a slot added to the enum with no name here is then a compile-time
 * sized array that no longer matches, which the static assert below catches.
 */
static char const* const ROLE_SLOT_NAME[UITREE_FRAME_SLOT_COUNT] = {
    "viewport", "minimap", "compass", "chat", "sidebar", "main_modal", "chat_buttons", "orbs",
};

_Static_assert(
    sizeof(ROLE_SLOT_NAME) / sizeof(ROLE_SLOT_NAME[0]) == UITREE_FRAME_SLOT_COUNT,
    "every frame slot needs a role name");

/* Chat-button filters, the one role whose members have names. Mirrors
 * revconfig_parse_chat_button_filter; both spell the reference's privacy bar. */
static char const* const ROLE_CHAT_FILTER_NAME[4] = {
    "public",
    "private",
    "trade",
    "report",
};

int
UITree_RoleSlotFromName(char const* name)
{
    assert(name);
    for( int i = 0; i < UITREE_FRAME_SLOT_COUNT; i++ )
    {
        if( strcmp(name, ROLE_SLOT_NAME[i]) == 0 )
            return i;
    }
    return -1;
}

char const*
UITree_RoleSlotName(int slot)
{
    if( slot < 0 || slot >= UITREE_FRAME_SLOT_COUNT )
        return NULL;
    return ROLE_SLOT_NAME[slot];
}

int
UITree_RoleSlotMemberFromName(int slot, char const* name)
{
    char* end;
    long value;

    assert(name);
    if( name[0] == '\0' )
        return -1;

    if( slot == UITREE_FRAME_SLOT_CHAT_BUTTONS )
    {
        for( int i = 0; i < 4; i++ )
        {
            if( strcmp(name, ROLE_CHAT_FILTER_NAME[i]) == 0 )
                return i;
        }
    }

    /* A plain number works for every role: a sidebar mount's tabno has no
     * names, and a filter may be written either way. */
    value = strtol(name, &end, 10);
    if( end != name && *end == '\0' && value >= 0 && value <= 0xFFFF )
        return (int)value;

    return -1;
}

/* ------------------------------------------------------------------------ */
/* The table                                                                 */
/* ------------------------------------------------------------------------ */

uint16_t
UITree_RoleFind(struct UITreeRoleTable const* table, char const* name)
{
    assert(table);
    assert(name);

    for( int i = 0; i < table->count; i++ )
    {
        if( strcmp(table->entries[i].name, name) == 0 )
            return (uint16_t)(i + 1);
    }
    return 0;
}

uint16_t
UITree_RoleIntern(struct UITreeRoleTable* table, char const* name)
{
    struct UITreeRoleEntry* entry;
    uint16_t existing;

    assert(table);
    assert(name);
    if( name[0] == '\0' )
        return 0;

    existing = UITree_RoleFind(table, name);
    if( existing )
        return existing;

    /* Ids are 16 bits on the node and 0 means "none", so the table cannot
     * hold more than 65535. Nothing is near it -- a profile declares a
     * handful -- and a table that got there is a bug, not a state. */
    assert(table->count < 0xFFFF);

    if( table->count == table->capacity )
    {
        int capacity = table->capacity ? table->capacity * 2 : 8;
        struct UITreeRoleEntry* grown =
            realloc(table->entries, (size_t)capacity * sizeof(*grown));
        assert(grown);
        table->entries = grown;
        table->capacity = capacity;
    }

    entry = &table->entries[table->count++];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->memo_node = -1;
    return (uint16_t)table->count;
}

char const*
UITree_RoleName(struct UITreeRoleTable const* table, uint16_t role_id)
{
    assert(table);
    if( role_id == 0 || (int)role_id > table->count )
        return NULL;
    return table->entries[role_id - 1].name;
}

int
UITree_RoleAddMatcher(
    struct UITreeRoleTable* table,
    uint16_t role_id,
    struct UITreeRoleMatcher const* matcher)
{
    struct UITreeRoleEntry* entry;

    assert(table);
    assert(matcher);
    assert(role_id > 0);
    assert((int)role_id <= table->count);

    entry = &table->entries[role_id - 1];
    if( entry->matcher_count >= UITREE_ROLE_MAX_MATCHERS )
        return 0;

    entry->matchers[entry->matcher_count++] = *matcher;
    if( matcher->kind != UITREE_ROLE_MATCH_SLOT )
        entry->id_sensitive = 1;
    /* A chain that grew has to be re-asked even if it was asked a moment ago:
     * the rung just added may resolve where the old ones did not. */
    entry->memo_valid = 0;
    return 1;
}

void
UITree_RoleMarkAuthored(struct UITreeRoleTable* table, uint16_t role_id)
{
    assert(table);
    assert(role_id > 0);
    assert((int)role_id <= table->count);

    table->entries[role_id - 1].authored = 1;
    table->entries[role_id - 1].memo_valid = 0;
}

void
UITree_RoleTableFree(struct UITreeRoleTable* table)
{
    if( !table )
        return;
    free(table->entries);
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}

/* ------------------------------------------------------------------------ */
/* Resolution                                                                */
/* ------------------------------------------------------------------------ */

static int
role_node_alive(struct UITree const* tree, int32_t idx)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return 0;
    return !tree->components[idx].freed;
}

/** The lowest-indexed live node carrying `client_code`, or -1. */
static int32_t
role_find_client_code(struct UITree const* tree, int client_code)
{
    int32_t best = -1;

    assert(tree);

    /*
     * Through the live set rather than the whole tree -- it is the same set
     * the clientCode tick walks -- and the LOWEST index of the ones it holds.
     *
     * The set is in registration order, which churns as scripts create and
     * delete; the lowest index is the tie-break UITree_FindByComponentId
     * already uses, and on a baked tree it is the node nearest the front of
     * the DFS. Taking whatever the set happened to list first would move the
     * answer around under a plugin that is doing nothing.
     */
    for( int32_t i = 0; i < tree->client_code.count; i++ )
    {
        int32_t idx = tree->client_code.slots[i];
        if( !role_node_alive(tree, idx) )
            continue;
        if( tree->components[idx].behavior.client_code != client_code )
            continue;
        if( best < 0 || idx < best )
            best = idx;
    }
    return best;
}

/** The lowest-indexed live node stamped with `role_id` at bake, or -1. */
static int32_t
role_find_authored(struct UITree const* tree, uint16_t role_id)
{
    assert(tree);
    assert(role_id);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].freed )
            continue;
        if( tree->components[i].role_id == role_id )
            return (int32_t)i;
    }
    return -1;
}

static int32_t
role_resolve_matcher(
    struct UITree const* tree,
    struct UITreeRoleMatcher const* m)
{
    assert(tree);
    assert(m);

    switch( m->kind )
    {
    case UITREE_ROLE_MATCH_SLOT:
        /* Straight through to the layout system's own lookup: a role that
         * names a region and a layout that places one must never be able to
         * disagree about which node that is. */
        return m->member < 0 ? UITree_FrameSlotNode(tree, m->slot)
                             : UITree_FrameSlotMemberNode(tree, m->slot, m->member);

    case UITREE_ROLE_MATCH_ID:
    case UITREE_ROLE_MATCH_IFACE:
    {
        int32_t idx = UITree_FindByComponentId(tree, m->uid);
        return role_node_alive(tree, idx) ? idx : -1;
    }

    case UITREE_ROLE_MATCH_CLIENTCODE:
        return role_find_client_code(tree, m->value);

    case UITREE_ROLE_MATCH_CC:
    {
        /*
         * The parent has to be found first, and it may be absent for an
         * entirely ordinary reason: the group is not mounted, or the script
         * that builds this subtree has not run yet. Both are "not now", which
         * the chain reads as "try the next rung".
         */
        int32_t parent = UITree_FindByComponentId(tree, m->uid);
        int32_t idx;
        if( !role_node_alive(tree, parent) )
            return -1;
        idx = UITree_FindChildBySubid(tree, parent, m->uid, m->value);
        return role_node_alive(tree, idx) ? idx : -1;
    }

    default:
        return -1;
    }
}

int32_t
UITree_RoleNode(
    struct UITree const* tree,
    struct UITreeRoleTable* table,
    uint16_t role_id)
{
    struct UITreeRoleEntry* entry;
    int32_t node;

    assert(tree);
    assert(table);

    if( role_id == 0 || (int)role_id > table->count )
        return -1;

    entry = &table->entries[role_id - 1];

    /*
     * Both generations, for a role that can be moved by id churn.
     *
     * `generation` moves when the tree is rebuilt, which catches a gameframe
     * remount. It does NOT move when a script deletes a node and creates
     * another in its place, and that is the case a role most needs to be right
     * about -- the uid is recycled, so a memo held across it points at a
     * different component that looks entirely valid. `id_generation` is bumped
     * on exactly those events, which also makes it a busy counter: it moves on
     * every component pushed anywhere in the tree. Keying a pure-region role
     * on it would re-walk the tree every frame to reach the same answer, so
     * those key on `generation` alone.
     */
    if( entry->memo_valid && entry->memo_generation == tree->generation &&
        (!entry->id_sensitive || entry->memo_id_generation == tree->id_generation) )
    {
        /* A remembered node can still have been freed without either counter
         * moving, so the answer is re-checked rather than trusted. */
        if( entry->memo_node < 0 || role_node_alive(tree, entry->memo_node) )
            return entry->memo_node;
    }

    /* The authored tag first: a profile that stamped a node has named it
     * outright, and a chain is what you write when you could not. */
    node = entry->authored ? role_find_authored(tree, role_id) : -1;

    for( int i = 0; node < 0 && i < entry->matcher_count; i++ )
        node = role_resolve_matcher(tree, &entry->matchers[i]);

    entry->memo_node = node;
    entry->memo_generation = tree->generation;
    entry->memo_id_generation = tree->id_generation;
    entry->memo_valid = 1;
    return node;
}

int32_t
UITree_RoleNodeByName(
    struct UITree const* tree,
    struct UITreeRoleTable* table,
    char const* name)
{
    assert(tree);
    assert(table);
    assert(name);
    return UITree_RoleNode(tree, table, UITree_RoleFind(table, name));
}
