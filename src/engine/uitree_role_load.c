#include "uitree_role_load.h"

#include "revconfig/revconfig.h"
#include "revconfig/revconfig_load.h"
#include "revconfig/revconfig_refs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * The revconfig matcher kinds and the ui ones are deliberately the same set,
 * spelled twice so that neither module has to include the other. This is the
 * one place that may assume they correspond, so it checks rather than trusts.
 */
_Static_assert(
    (int)REVCONFIG_ROLE_MAX_MATCHERS <= (int)UITREE_ROLE_MAX_MATCHERS,
    "a profile must not be able to write a chain the tree cannot hold");

/**
 * Resolve one revconfig matcher into tree terms.
 *
 * Returns 0 when the rung cannot be resolved AT ALL on this revision -- an
 * unknown slot name, an [iface:…] the profile never declared. That is
 * different from a rung that resolves to a lookup which happens to find
 * nothing today: the second is a runtime state the chain handles, the first is
 * a rung that could never work and is reported.
 */
static int
role_matcher_to_tree(
    struct RevConfigRoleMatcher const* src,
    struct RevConfigRefs const* refs,
    char const* role_name,
    struct UITreeRoleMatcher* out)
{
    assert(src);
    assert(refs);
    assert(role_name);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->member = -1;

    switch( src->kind )
    {
    case REVCONFIG_ROLE_MATCH_SLOT:
    {
        int slot = UITree_RoleSlotFromName(src->slot);
        if( slot < 0 )
        {
            TORIRS_LOG("revconfig: [role:%s] slot(%s) names no frame slot\n",
                role_name,
                src->slot);
            return 0;
        }
        out->kind = UITREE_ROLE_MATCH_SLOT;
        out->slot = (int16_t)slot;
        if( src->member[0] != '\0' )
        {
            int member = UITree_RoleSlotMemberFromName(slot, src->member);
            if( member < 0 )
            {
                TORIRS_LOG("revconfig: [role:%s] slot(%s, %s) names no member of that slot\n",
                    role_name,
                    src->slot,
                    src->member);
                return 0;
            }
            out->member = member;
        }
        return 1;
    }

    case REVCONFIG_ROLE_MATCH_ID:
        out->kind = UITREE_ROLE_MATCH_ID;
        out->uid = src->ref.value;
        return 1;

    case REVCONFIG_ROLE_MATCH_IFACE:
    case REVCONFIG_ROLE_MATCH_CC:
    {
        int uid;

        if( src->ref.kind == REVCONFIG_ROLE_MATCH_ID )
            uid = src->ref.value;
        else
        {
            /* -1 means this revision does not have that interface, which for
             * a rung means the rung is unwritable -- not that it should be
             * kept and quietly miss. */
            int group = RevConfigRefs_Get(refs, "iface", src->ref.name);
            if( group < 0 )
            {
                TORIRS_LOG("revconfig: [role:%s] iface(%s) is not declared by this profile\n",
                    role_name,
                    src->ref.name);
                return 0;
            }
            /* The same packing the rest of the client uses for a dat2 uid; a
             * dat1 id is flat and its child is 0, which leaves it unchanged. */
            uid = group > 0xFFFF ? group : (group << 16);
            uid |= src->ref.value & 0xFFFF;
        }

        if( src->kind == REVCONFIG_ROLE_MATCH_CC )
        {
            out->kind = UITREE_ROLE_MATCH_CC;
            out->uid = uid;
            out->value = src->value;
        }
        else
        {
            out->kind = UITREE_ROLE_MATCH_IFACE;
            out->uid = uid;
        }
        return 1;
    }

    case REVCONFIG_ROLE_MATCH_CLIENTCODE:
        out->kind = UITREE_ROLE_MATCH_CLIENTCODE;
        out->value = src->value;
        return 1;

    default:
        return 0;
    }
}

void
UITreeRoleLoad_AddItems(
    struct UITreeRoleTable* table,
    struct RevConfigItemBuffer const* items,
    struct RevConfigRefs const* refs)
{
    assert(table);
    assert(items);
    assert(refs);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];

        /*
         * A component's `role=` interns the name and says a node will carry
         * it. The tag itself is stamped by the bake, which is a different
         * pass over the same items -- but the FLAG has to be set even if this
         * profile's bake never runs (a cache gameframe lane still parses the
         * dat1 ui half), because it is what tells the resolver whether an
         * authored node is worth looking for.
         */
        if( item->kind == RCITEM_UICOMPONENT && item->u.uicomponent.role[0] != '\0' )
        {
            uint16_t id = UITree_RoleIntern(table, item->u.uicomponent.role);
            UITree_RoleMarkAuthored(table, id);
            continue;
        }

        if( item->kind != RCITEM_ROLE || item->u.role.name[0] == '\0' )
            continue;

        uint16_t id = UITree_RoleIntern(table, item->u.role.name);
        for( int m = 0; m < item->u.role.matcher_count; m++ )
        {
            struct UITreeRoleMatcher matcher;
            if( !role_matcher_to_tree(
                    &item->u.role.matchers[m], refs, item->u.role.name, &matcher) )
                continue;
            if( !UITree_RoleAddMatcher(table, id, &matcher) )
                TORIRS_LOG("revconfig: [role:%s] has more than %d match= lines; the rest are dropped\n",
                    item->u.role.name,
                    UITREE_ROLE_MAX_MATCHERS);
        }
    }
}

/** Parse one source into `table`. `prefix` NULL/"" is the unprefixed dialect. */
static void
role_load_one(
    struct UITreeRoleTable* table,
    struct RevConfigRefs const* refs,
    char const* path,
    char const* prefix)
{
    struct RevConfigBuffer* fields;
    struct RevConfigItemBuffer* items;

    assert(table);
    assert(refs);
    if( !path || path[0] == '\0' )
        return;

    fields = revconfig_buffer_new(256);
    assert(fields);
    items = revconfig_item_buffer_new(64);
    assert(items);

    revconfig_load_fields_from_ini_prefixed(path, prefix, fields);
    revconfig_items_build(fields, items);
    UITreeRoleLoad_AddItems(table, items, refs);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
}

void
UITreeRoleLoad_LoadSources(
    struct UITreeRoleTable* table,
    struct RevConfigRefs const* refs,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini)
{
    assert(table);
    assert(refs);

    /* Same order as RevConfigRefs_LoadSources: shared files first, the boot
     * manifest's own inline sections last. */
    role_load_one(table, refs, ui_ini, NULL);
    role_load_one(table, refs, cache_ini, NULL);
    role_load_one(table, refs, inline_ini, "revconfig");
}
