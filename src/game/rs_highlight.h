#ifndef RS_HIGHLIGHT_H
#define RS_HIGHLIGHT_H

/*
 * The HIGHLIGHT_* opcode family (7000..7044), as state.
 *
 * These opcodes are how the CACHE asks this client to mark something. 125
 * clientscripts call one: they read the setting's varbit, read its colour row,
 * and then describe a highlight GROUP -- a colour, a style, an opacity, a flag
 * word -- and put subjects into it.
 *
 *     [clientscript,script5198]                  // "Highlight hovered tile"
 *     def_int $int2 = ~script5329(174);          //   ... its colour row
 *     if (%varbit12977 = 1) {                    //   ... its own varbit
 *         if (%varbit12980 = 1) { $int3 = calc($int3 + 16); }  // always on top
 *         _7035(5, $int2, $int0, $int1, $int3);  // HIGHLIGHT_TILE_SETUP
 *     } else {
 *         _7039(5);                              // HIGHLIGHT_TILE_CLEAR
 *         _7035(5, -1, 0, 0, 0);
 *     }
 *
 * That is the whole Activities category's highlighting half, already written,
 * in the cache. Roughly thirty of its rows are driven this way -- the tile
 * indicators, the tile markers, the npc highlight, Agility obstacles, quest
 * start points, fishing spots, poll booths, the Blast Furnace, the clue scroll
 * helper -- and several of them name subjects (which loc is an Agility
 * obstacle, which npc is a fishing spot) that this client has no table for and
 * would otherwise have had to build.
 *
 * So the state here is deliberately dumb. It records what the scripts said and
 * answers GET truthfully; it decides nothing about appearance and draws
 * nothing. The drawing is the `nxt-highlight` builtin's, through the plugin
 * layer's `highlight_next`.
 *
 * Nothing here includes an engine header, so it links into a test on its own.
 */

#include <stdbool.h>

/**
 * Highlight groups per kind.
 *
 * 32 because the cache uses 32: clientscript 4630's teardown clears groups 2
 * through 31 by hand, which is the widest statement of the range anything
 * makes. Group ids are per KIND -- clientscript 1854 has an objtype group 9
 * and a loc group 11 live at once, and script 6503 a tile group 9 -- so this
 * is not one shared table with kind bits packed into it.
 */
#define RS_HIGHLIGHT_GROUP_MAX 32

/**
 * Subjects one kind may hold across all of its groups.
 *
 * The largest real user is `highlight_loctype_on`, called 109 times in this
 * cache -- every Agility obstacle, quest start and fishing spot -- so the
 * loctype list is the one this is sized against, with headroom for a script
 * that names more. Past it, an add is refused and says so rather than
 * evicting: a highlight that silently stopped covering the last few subjects
 * would be indistinguishable from those subjects not qualifying.
 */
#define RS_HIGHLIGHT_MEMBERS_MAX 512

/** What a highlight group applies to. Each has its own SETUP opcode, and its
 *  own group numbering. */
enum RS_HighlightKind
{
    RS_HIGHLIGHT_NPC = 0,
    RS_HIGHLIGHT_NPCTYPE,
    RS_HIGHLIGHT_LOC,
    RS_HIGHLIGHT_LOCTYPE,
    RS_HIGHLIGHT_OBJ,
    RS_HIGHLIGHT_OBJTYPE,
    RS_HIGHLIGHT_PLAYER,
    RS_HIGHLIGHT_TILE,
    RS_HIGHLIGHT_KIND_COUNT
};

/*
 * The flag bits, read off clientscript 4624 -- the developer client-op script
 * that offers "Highlight normal", "Highlight always-on-top" and "Highlight
 * snap-to-grid" and builds each one's flag word from named locals:
 *
 *     normal        = 1 + 2 + 4 + 8
 *     always-on-top = 1 + 2 + 4 + 8 + 16
 *     snap-to-grid  = 2 + 8 + 16 + 32 + 128
 *
 * Bit 16 is confirmed twice over: script 5198 adds exactly 16 to a tile
 * group's flags when "Highlight hovered tile - Always on top" is set.
 *
 * The rest are read from how they pair up in the real call sites. A TILE group
 * is always 2 and 8 together (`_7035(5, c, 0, 70, 10)`), an entity group
 * always 1, or 1 and 4 together (`_7010(11, 65280, 1, 30, 5)`) -- so 1/4 are
 * the model's outline and wash and 2/8 are the tile's. 64 appears only on the
 * tile-marker group (`_7035(6, c, 2, 50, 90)`), the one feature whose marks
 * also show on the minimap.
 *
 * 32 and 128 are named for completeness and are not acted on: only the
 * developer script sets them, and what it means by "snap to grid" is not
 * stated anywhere this client can read.
 */
#define RS_HIGHLIGHT_FLAG_MODEL_OUTLINE 1
#define RS_HIGHLIGHT_FLAG_TILE_OUTLINE 2
#define RS_HIGHLIGHT_FLAG_MODEL_FILL 4
#define RS_HIGHLIGHT_FLAG_TILE_FILL 8
#define RS_HIGHLIGHT_FLAG_ALWAYS_ON_TOP 16
#define RS_HIGHLIGHT_FLAG_UNKNOWN_32 32
#define RS_HIGHLIGHT_FLAG_MINIMAP 64
#define RS_HIGHLIGHT_FLAG_SNAP_TO_GRID 128

/** One group's appearance, exactly as a SETUP opcode stated it. */
struct RS_HighlightStyle
{
    /** 0xRRGGBB, or -1 for "this group is off". Every disabling call in the
     *  cache passes -1 here, and most pass opacity 0 as well. */
    int colour;
    /** 0, 1 or 2 in this cache. What each means is not stated anywhere a
     *  client can read, so it is carried and not interpreted. */
    int style;
    /** PERCENT, 0..100, not 0..255: the call sites are 30, 45, 50, 70 and 100.
     *  0 is the other way a group says it is off. */
    int opacity;
    /** RS_HIGHLIGHT_FLAG_*. */
    int flags;
};

/** One subject in one group. */
struct RS_HighlightMember
{
    int group;
    /**
     * What was named. Its meaning is the kind's:
     *   NPC        the npc uid the client op / minimenu reported
     *   NPCTYPE    an npc type id
     *   LOC        a loc type id, at `coord`
     *   LOCTYPE    a loc type id, anywhere
     *   OBJ        an obj id, at `coord`
     *   OBJTYPE    an obj id, anywhere
     *   TILE       unused (-1); the tile IS `coord`
     */
    int key;
    /** Packed absolute coord, `plane << 28 | x << 14 | z`, or -1 for a kind
     *  that names no place. Same packing as CS2's `coord`. */
    int coord;
    /** The per-subject flag word the LOC/OBJ/TILE forms carry, or 0. Separate
     *  from the group's: a script may put one loc in a group with flags of its
     *  own. */
    int flags;
};

struct RS_HighlightState
{
    struct RS_HighlightStyle style[RS_HIGHLIGHT_KIND_COUNT][RS_HIGHLIGHT_GROUP_MAX];
    struct RS_HighlightMember member[RS_HIGHLIGHT_KIND_COUNT][RS_HIGHLIGHT_MEMBERS_MAX];
    int member_count[RS_HIGHLIGHT_KIND_COUNT];
    /** Bumped by every change, so a consumer can skip a frame's work when
     *  nothing has been said since the last one. */
    int revision;
    /** Set once, when an add was refused for want of room. Read by whoever
     *  wants to say so; the refusal itself is already logged. */
    bool overflowed;
};

/** All groups off, all lists empty. Every `colour` becomes -1, which is what
 *  "off" is here -- a zeroed struct would mean "black at 0% opacity", which is
 *  also off but by accident rather than by contract. */
void RS_HighlightReset(struct RS_HighlightState* state);

void RS_HighlightSetup(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group,
    int colour,
    int style,
    int opacity,
    int flags);

/** Idempotent: naming a subject already in the group updates its flags rather
 *  than adding it twice. Returns false when the list is full. */
bool RS_HighlightOn(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group,
    int key,
    int coord,
    int flags);

/** Removing something that is not there is not an error -- the cache's
 *  toggles call OFF unconditionally on the way to a rebuild. */
void RS_HighlightOff(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group,
    int key,
    int coord);

bool RS_HighlightGet(
    struct RS_HighlightState const* state,
    enum RS_HighlightKind kind,
    int group,
    int key,
    int coord);

void RS_HighlightClear(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group);

/** Is this group drawable at all -- a colour, and some opacity? */
bool RS_HighlightGroupLive(
    struct RS_HighlightState const* state,
    enum RS_HighlightKind kind,
    int group);

/**
 * Apply one HIGHLIGHT_* opcode.
 *
 * `args` is in PUSH order, as CS2VM2_Op_Highlight pops them: args[0] is the
 * first int the script pushed. `*out_query` is filled for the GET forms and
 * untouched otherwise; the caller pushes it.
 *
 * Returns false for an opcode this does not own, or one whose argument count
 * does not match its form -- a mismatch means the opcode table and this
 * disagree, which is worth failing loudly for rather than acting on the wrong
 * slot.
 */
bool RS_HighlightApply(
    struct RS_HighlightState* state,
    int opcode,
    int const* args,
    int arg_count,
    int* out_query);

/** The kind and group an opcode names, for a caller that wants to log one.
 *  Returns false when the opcode is not in the family. */
bool RS_HighlightOpcodeKind(int opcode, enum RS_HighlightKind* out_kind);

/** "npc", "loctype", "tile" ... for logs. Never NULL. */
char const* RS_HighlightKindName(enum RS_HighlightKind kind);

/* Coord packing, shared with CS2's `coord` (3308): plane << 28 | x << 14 | z,
 * ABSOLUTE tiles. Here rather than in each caller because a highlight member
 * is compared against a world position exactly once, and both halves of that
 * comparison have to agree about the packing. */
#define RS_HIGHLIGHT_COORD(plane, x, z) (((plane) << 28) | ((x) << 14) | (z))
#define RS_HIGHLIGHT_COORD_PLANE(c) (((c) >> 28) & 0x3)
#define RS_HIGHLIGHT_COORD_X(c) (((c) >> 14) & 0x3FFF)
#define RS_HIGHLIGHT_COORD_Z(c) ((c) & 0x3FFF)

#endif /* RS_HIGHLIGHT_H */
