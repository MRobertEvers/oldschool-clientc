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
    /* out->cc_type stays 0 ("no filter") from the memset above unless the
     * _CC case below states a type name; the enum has no zero member. */

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
            if( src->cc_type[0] != '\0' )
            {
                out->cc_type = UITree_RoleCcTypeFromName(src->cc_type);
                if( out->cc_type < 0 )
                {
                    TORIRS_LOG("revconfig: [role:%s] cc(...) names no known type '%s'\n",
                        role_name,
                        src->cc_type);
                    return 0;
                }
            }
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

/**
 * Translate one `match=` line, expanding an `any(v1,…)` in whichever numeric
 * argument carried it into that many separate rungs appended to `id`'s chain
 * -- the same effect as the profile having written that many match= lines,
 * per D10.
 */
static void
role_matcher_expand_add(
    struct UITreeRoleTable* table,
    uint16_t id,
    struct RevConfigRoleMatcher const* src,
    struct RevConfigRefs const* refs,
    char const* role_name)
{
    int total = 1;
    int const* extra = NULL;
    int ref_carries_it = 0;

    assert(table);
    assert(src);
    assert(refs);
    assert(role_name);

    switch( src->kind )
    {
    case REVCONFIG_ROLE_MATCH_ID:
    case REVCONFIG_ROLE_MATCH_IFACE:
        total = 1 + src->ref.any_count;
        extra = src->ref.any_value;
        ref_carries_it = 1;
        break;
    case REVCONFIG_ROLE_MATCH_CLIENTCODE:
    case REVCONFIG_ROLE_MATCH_CC:
        total = 1 + src->any_count;
        extra = src->any_value;
        break;
    default:
        break;
    }

    for( int i = 0; i < total; i++ )
    {
        struct RevConfigRoleMatcher copy = *src;
        struct UITreeRoleMatcher matcher;

        if( i > 0 )
        {
            if( ref_carries_it )
                copy.ref.value = extra[i - 1];
            else
                copy.value = extra[i - 1];
        }

        if( !role_matcher_to_tree(&copy, refs, role_name, &matcher) )
            continue;
        if( !UITree_RoleAddMatcher(table, id, &matcher) )
            TORIRS_LOG("revconfig: [role:%s] has more than %d match= lines; the rest are dropped\n",
                role_name,
                UITREE_ROLE_MAX_MATCHERS);
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

        /* derive= rides UITreeRoleTable.fallback instead of the chain below;
         * a role that states both is data the fallback wins on, since it is
         * consulted first (UITree_RoleNode). */
        if( item->u.role.derive_fact[0] != '\0' )
            UITree_RoleSetDerive(table, id, item->u.role.derive_fact, item->u.role.derive_argument);

        for( int m = 0; m < item->u.role.matcher_count; m++ )
            role_matcher_expand_add(table, id, &item->u.role.matchers[m], refs, item->u.role.name);
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
    char generated_roles[512];

    assert(table);
    assert(refs);

    /* Same order as RevConfigRefs_LoadSources: shared files first, the boot
     * manifest's own inline sections last. */
    role_load_one(table, refs, ui_ini, NULL);
    role_load_one(table, refs, cache_ini, NULL);
    /*
     * tools/revconfig_roles_from_pack.py's generated [role:]/[iface:]
     * companion, a SEPARATE file next to a dat2 lane's cache ini
     * (osrs239_dat2_cache.ini -> osrs239_dat2_roles.gen.ini) so the
     * hand-edited ini is never rewritten. `refs` must already carry this same
     * file's [iface:] sections for its iface(...) rungs to resolve --
     * RevConfigRefs_LoadSources derives and loads the identical path. A lane
     * with no generated sibling (dat1, or a dat2 lane not yet generated)
     * derives nothing and role_load_one's own missing-file handling no-ops.
     */
    if( cache_ini &&
        revconfig_derive_sibling_path(
            cache_ini,
            "_dat2_cache.ini",
            "_dat2_roles.gen.ini",
            generated_roles,
            sizeof(generated_roles)) )
        role_load_one(table, refs, generated_roles, NULL);
    role_load_one(table, refs, inline_ini, "revconfig");
}
