#ifndef SRC_UITREE_ROLE_H
#define SRC_UITREE_ROLE_H

/*
 * Semantic roles: naming an interface element by what it IS.
 *
 * The frame slots next door do this for the placeable REGIONS -- the scene,
 * the minimap, the chatbox -- and the argument generalises. A plugin that
 * wants to press the report button or ask whether the logout screen is up has
 * the same problem the layout had: the answer is a different component on
 * every revision, and a number compiled into C is a silent wrong answer on all
 * but one of them.
 *
 * So a role is a NAME, and the profile for a revision says what it is bound
 * to. Two channels, because there are two kinds of node:
 *
 *   AUTHORED -- a node a revconfig profile built. It carries its role
 *   directly, interned into `role_id` at bake time, and costs nothing to find.
 *
 *   FOUND -- a node the profile did not build: a cache component, or one a CS2
 *   script created at runtime. A matcher chain describes where to look, and is
 *   resolved against the live tree on demand.
 *
 * A chain is tried in declaration order and the first rung that resolves wins,
 * which is what lets one profile carry a rung per toplevel: rev-239 mounts one
 * of six, and the five that are not up simply do not match.
 *
 * WHY RESOLUTION IS LIVE, EVERY TIME
 *
 * A CS2 component's uid is not an identity. UITree_AllocateDynamicComponentId
 * hands out child ids from a rotating 0x8000..0xFFFF counter and CcCreate
 * reclaims the slot first, so a CC_DELETEALL and rebuild -- which the chatbox
 * does per message and a boss fight does per tick -- gives the same numbers
 * back to different components. Anything that remembered a uid would be
 * pointing at whatever got built next. What IS stable is the sub id the script
 * chose, which is why cc() addresses a dynamic node as (parent, sub_id) and
 * why CC_GETID answers the sub id rather than the uid.
 *
 * So this resolves on ask and memoises only until the tree says otherwise --
 * `generation` for a rebuild, `id_generation` for the cc churn inside one.
 */

#include "uitree.h"
#include "uitree_frame.h"

#include <stdint.h>

/** How many rungs one role's chain may carry (mirrors REVCONFIG_ROLE_MAX_MATCHERS). */
#define UITREE_ROLE_MAX_MATCHERS 8

/** Longest role name. */
#define UITREE_ROLE_NAME_MAX 64

/**
 * How one rung names its node.
 *
 * Deliberately the same set as enum RevConfigRoleMatchKind, restated rather
 * than included: the tree is not allowed to depend on revconfig, and a
 * headless uitree test links neither it nor the app that translates between
 * them. App_ is the one place that knows both spellings.
 */
enum UITreeRoleMatchKind
{
    UITREE_ROLE_MATCH_NONE = 0,

    /** A frame slot, optionally one member of it. The regions answer here so
     *  that a role and a layout name the same thing through the same lookup --
     *  there is no second table to keep in step. */
    UITREE_ROLE_MATCH_SLOT,

    /** A uid outright. */
    UITREE_ROLE_MATCH_ID,

    /** A uid the app resolved from an [iface:<name>] before handing it over.
     *  Identical to _ID at this layer; kept apart so a diagnostic can say
     *  which spelling the profile used. */
    UITREE_ROLE_MATCH_IFACE,

    /** The first live node carrying this clientCode. */
    UITREE_ROLE_MATCH_CLIENTCODE,

    /** A CS2-created child of `parent_uid`, by the sub id its script chose. */
    UITREE_ROLE_MATCH_CC,
};

/** One rung, in terms this layer can resolve: every name already a number. */
struct UITreeRoleMatcher
{
    uint8_t kind;

    /** _SLOT: an enum UITreeFrameSlot. */
    int16_t slot;
    /** _SLOT: the member within the role (@see UITree_FrameSlotIndex), or -1
     *  for the region itself. */
    int32_t member;

    /** _ID and _IFACE: the uid. _CC: the parent's uid. */
    int32_t uid;
    /** _CLIENTCODE: the code. _CC: the sub id. */
    int32_t value;
    /** _CC only: the resolved child's required enum UITreeComponentType, or
     *  0 for no filter (the plain two-argument cc() form -- and, deliberately,
     *  what a plain `= {0}`/memset construction already gives every existing
     *  and future caller that never heard of this field: the enum has no
     *  zero member). Guards against a reused dynamic sub id landing on the
     *  wrong kind of node after a CC_DELETEALL rebuild built something else
     *  there under the same id -- @see UITree_RoleCcTypeFromName. */
    int32_t cc_type;
};

/** One role: its name, its chain, and where the last answer came from. */
struct UITreeRoleEntry
{
    char name[UITREE_ROLE_NAME_MAX];
    struct UITreeRoleMatcher matchers[UITREE_ROLE_MAX_MATCHERS];
    int matcher_count;

    /** A profile stamped some node with this role. Set by the table's builder,
     *  because the tag and the flag come from the same `role=` line -- and
     *  without it every lookup on every CS2-only role would walk the tree
     *  hunting for an authored node that was never going to be there. */
    uint8_t authored;

    /** This role's answer can be changed by id churn, not just by a rebuild:
     *  it names a uid, a clientCode or a cc sub id. A role built only from
     *  slot() rungs cannot -- a frame slot is matched on builtin type and slot
     *  tag, and CcCreate produces neither -- so it memoises on `generation`
     *  alone, which is what the layout's own role cache has always done. */
    uint8_t id_sensitive;

    /*
     * Memo. `node` is meaningful only while both generations still hold: the
     * tree bumps `generation` when it is rebuilt and `id_generation` on every
     * id assigned or cleared, which is exactly the cc churn that makes a
     * remembered dynamic node the wrong one.
     *
     * -1 is a cached "this revision does not have it", which is worth keeping:
     * a plugin asking every frame for a role no profile declared should not
     * walk the tree every frame to be told so again.
     */
    int32_t memo_node;
    uint32_t memo_generation;
    uint32_t memo_id_generation;
    uint8_t memo_valid;

    /** `derive=<fact>[(<argument>)]` -- answered by UITreeRoleTable.fallback
     *  instead of the chain above. Empty for an ordinary role. `derive_argument`
     *  is -1 when the ini stated no (<expr>). @see UITree_RoleSetDerive. */
    char derive_fact[32];
    int derive_argument;
};

/**
 * Every role this revision declared.
 *
 * Owned by whoever built it (the App), handed to the resolver by pointer. The
 * table is per-revision and the tree is per-boot, so it deliberately does not
 * live on the tree: the same table answers across a gameframe rebuild.
 */
struct UITreeRoleTable
{
    struct UITreeRoleEntry* entries;
    int count;
    int capacity;
    /** Optional authoritative lane adapter after authored tags. -2 declines
     * the role (use matchers); -1 means the native role is currently absent. */
    int32_t (*fallback)(struct UITree const*, struct UITreeRoleTable const*, uint16_t, void*);
    void* fallback_user;
    /** Open-addressed name index: slot -> role id (0 = empty), kept at most
     *  half full and rebuilt by UITree_RoleIntern when it grows. Every
     *  per-frame lookup by name (a plugin's widget request, a frame binder)
     *  used to walk `entries` with strcmp: 7,000 compares a frame. */
    uint16_t* name_index;
    uint32_t name_index_capacity; /* power of two, or 0 before the first intern */
};

/**
 * Intern `name`, returning its role id. Never 0 -- 0 is "no role" -- so an id
 * is `index + 1`.
 *
 * Creates the entry when it is new, so a `role=` tag on a component and a
 * `[role:<name>]` chain for the same name land on one entry whichever is
 * loaded first.
 */
uint16_t
UITree_RoleIntern(struct UITreeRoleTable* table, char const* name);

/** The role id `name` interned to, or 0 when nothing ever declared it. */
uint16_t
UITree_RoleFind(struct UITreeRoleTable const* table, char const* name);

/** The name behind a role id, or NULL. */
char const*
UITree_RoleName(struct UITreeRoleTable const* table, uint16_t role_id);

/**
 * Append a rung to `role_id`'s chain. 0 when the chain is already full, which
 * is reported by the caller -- silently dropping the rung a profile wrote is
 * how a role ends up resolving to the wrong node.
 */
int
UITree_RoleAddMatcher(
    struct UITreeRoleTable* table,
    uint16_t role_id,
    struct UITreeRoleMatcher const* matcher);

/**
 * The node carrying `role_id` right now, or -1 when this revision has none.
 *
 * -1 is an ANSWER and not a fault, the same as an undeclared [iface:…] id: a
 * caller that gets it turns its feature off rather than guessing.
 *
 * The authored tag is looked at first -- a profile that stamped a node has
 * said so directly -- and the chain after it, in order.
 */
int32_t
UITree_RoleNode(
    struct UITree const* tree,
    struct UITreeRoleTable* table,
    uint16_t role_id);

/**
 * Record that a node somewhere carries `role_id` as an authored tag.
 *
 * Called by whoever bakes the tag, from the same `role=` line that produced
 * it. @see UITreeRoleEntry::authored.
 */
void
UITree_RoleMarkAuthored(struct UITreeRoleTable* table, uint16_t role_id);

/** UITree_RoleNode by name. 0 roles and unknown names answer -1. */
int32_t
UITree_RoleNodeByName(
    struct UITree const* tree,
    struct UITreeRoleTable* table,
    char const* name);

/**
 * Record that `role_id` is answered by `fact`[(`argument`)] rather than by
 * its (empty) matcher chain. Called once per role while loading a `derive=`
 * line, the same way UITree_RoleMarkAuthored is called from a `role=` line.
 */
void
UITree_RoleSetDerive(
    struct UITreeRoleTable* table,
    uint16_t role_id,
    char const* fact,
    int argument);

/**
 * `role_id`'s derive= fact and `*out_argument`, or NULL for an ordinary role.
 * The function installed as UITreeRoleTable.fallback calls this first, to
 * turn a role id back into the fact name it was declared with -- and to
 * decline (return -2) when this role has none.
 */
char const*
UITree_RoleDeriveFact(
    struct UITreeRoleTable const* table,
    uint16_t role_id,
    int* out_argument);

/** Drop every entry. */
void
UITree_RoleTableFree(struct UITreeRoleTable* table);

/**
 * enum UITreeFrameSlot for a slot name (`viewport`, `minimap`, `compass`,
 * `chat`, `sidebar`, `main_modal`, `chat_buttons`), or -1.
 *
 * Here rather than in uitree_frame.c because it is the ROLE vocabulary's
 * spelling of the slot enum: the frame system itself never sees a name.
 */
int
UITree_RoleSlotFromName(char const* name);

/** The other direction: the role spelling of one enum UITreeFrameSlot, or
 *  NULL out of range. What the frame binder builds `frame_<slot>` from. */
char const*
UITree_RoleSlotName(int slot);

/**
 * The member number a slot's member NAME means, or -1.
 *
 * Chat buttons have names for their filters (`public`, `private`, `trade`,
 * `report`); a sidebar mount's tabno does not, and is written as a number. A
 * plain number parses for either.
 */
int
UITree_RoleSlotMemberFromName(int slot, char const* name);

/**
 * enum UITreeComponentType for a `cc(<anchor>, <sub_id>, <type>)` type-filter
 * name ("text", "graphic", "model", "inv", "inv_text", "layer", "rect",
 * "line", "obj", "arc"), or -1 when `name` names none of them.
 *
 * Here rather than in uitree.c for the same reason UITree_RoleSlotFromName
 * is: it is the ROLE grammar's own spelling of the type enum, read only where
 * a match= line is translated into a matcher.
 */
int
UITree_RoleCcTypeFromName(char const* name);

#endif /* SRC_UITREE_ROLE_H */
