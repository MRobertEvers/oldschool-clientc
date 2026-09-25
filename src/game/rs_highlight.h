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

/**
 * Bytes of a name-keyed subject. A player display name, so the same 32 the
 * world's own player entities carry.
 */
#define RS_HIGHLIGHT_NAME_MAX 32

/**
 * Name-keyed subjects, across every group of the two kinds that have them.
 *
 * PLAYER and OPGROUP are the only kinds keyed by a NAME, and the reference
 * keeps each in its own hash map on the highlight manager rather than in the
 * int-keyed lists. 64 because the cache's own users are the mouse-over
 * highlighter (clientscript 5954), which holds one name at a time, and the
 * developer client-ops (4643..4645), which hold one more.
 */
#define RS_HIGHLIGHT_NAMED_MAX 64

/*
 * The two groups the client itself has to know by number.
 *
 * Both belong to the "Clear your highlighted ..." BUTTON rows in All Settings,
 * which reach clientscript 3969 and find no case there: a button row has no
 * varbit, so the cache's only trace of it is the setting id, and the clearing
 * is the client's. That makes the group id a cache fact this client has to
 * spell out, so it is spelled once, here, with what says so:
 *
 *   TILE 6  clientscript 4763 sets it up (`_7035(6, colour, 2, 50, 90)`), the
 *           "Mark tile" client op (4762) adds to it, and 6686 clears it.
 *   NPC  6  clientscript 6688 -- the "Tag" client op -- adds to it.
 *
 * Group ids are per KIND, which is why these two are different groups and
 * clearing one cannot touch the other.
 */
#define RS_HIGHLIGHT_GROUP_TILE_MARKERS 6
#define RS_HIGHLIGHT_GROUP_NPC_TAGS 6

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
    /**
     * The 7040..7044 block: everything whose right-click NAME matches.
     *
     * The reference's own word for it -- `HighlightManager::AddOpGroupHighlight`
     * / `RemoveOpGroupHighlight` / `IsOpGroupHighlighted`, taking an `OpGroup`,
     * which `OpGroup::Create` builds from a subject's name and its op list. The
     * scripted form can only ever hand it the NAME (its ON pops one string and
     * a group), so a name is the whole key, and it applies across the npc, loc
     * and obj pools rather than to one kind of thing.
     *
     * Appended after TILE so the eight kinds the 7000..7039 blocks number keep
     * the ids their opcodes imply.
     */
    RS_HIGHLIGHT_OPGROUP,
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
 *
 * 512 is the MOUSEOVER groups' bit and nothing else's -- clientscript 5949
 * sets up all six of them as `1 + 512`, or `1 + 16 + 512` for group 4. It is
 * carried and not acted on: bit 1 beside it already says "outline the model",
 * which is what a hover highlight is, and no call site distinguishes them.
 *
 * Bits 1, 2, 4 and 8 are no longer read off call sites at all -- the reference
 * client's four predicates state them outright:
 *
 *     HasModelOutline = (flags & 1) && outline_width != 0
 *     HasTileOutline  = (flags & 2) && outline_width != 0
 *     HasModelFill    = (flags & 4) && opacity != 0
 *     HasTileFill     = (flags & 8) && opacity != 0
 *     IsEnabled       = any of the four
 */
#define RS_HIGHLIGHT_FLAG_MODEL_OUTLINE 1
#define RS_HIGHLIGHT_FLAG_TILE_OUTLINE 2
#define RS_HIGHLIGHT_FLAG_MODEL_FILL 4
#define RS_HIGHLIGHT_FLAG_TILE_FILL 8
#define RS_HIGHLIGHT_FLAG_ALWAYS_ON_TOP 16
#define RS_HIGHLIGHT_FLAG_UNKNOWN_32 32
#define RS_HIGHLIGHT_FLAG_MINIMAP 64
#define RS_HIGHLIGHT_FLAG_SNAP_TO_GRID 128
#define RS_HIGHLIGHT_FLAG_MOUSEOVER 512

/**
 * One group's appearance, exactly as a SETUP opcode stated it.
 *
 * Field meanings are the reference client's, read out of its own decompiled
 * `HighlightManager::ConfigureChannel` and the four `EntityHighlight::Has*`
 * predicates rather than inferred from call sites. Two of them were guessed
 * wrong here before that: see `outline_width` and `opacity`.
 */
struct RS_HighlightStyle
{
    /** 0xRRGGBB, or -1 for "off". The reference converts this to packed HSL on
     *  the way in (`RunetekColour::RGBToHSL`) because its own rasteriser is
     *  palette-based; this client's draw api takes RGB, so it is kept as RGB. */
    int colour;
    /**
     * Outline THICKNESS in pixels. 0, 1 or 2 in this cache.
     *
     * Not a "style" enum, which is what this was called here until the
     * reference settled it: both outline predicates are
     * `(flags & bit) && thickness != 0`, so a group with an outline FLAG and a
     * thickness of zero draws no outline at all. Clientscript 5198's hovered
     * tile is exactly that -- flags 2|8, thickness 0 -- and is a fill with no
     * border, which is what the reference draws and what this drew as a solid
     * outline before.
     */
    int outline_width;
    /**
     * Fill alpha, 0..255, clamped by the opcode handler.
     *
     * NOT a percent, which is what this was called here until the reference
     * settled it -- `jag::math::Clamp(value, 0, 255)`. The call sites (30, 45,
     * 50, 70, 100) are therefore quite transparent washes, and scaling them by
     * 255/100 the way this used to made every one of them 2.55x too opaque.
     */
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

/**
 * One name-keyed subject in one group -- a PLAYER, and nothing else.
 *
 * Separate from RS_HighlightMember because the key is a different kind of
 * thing, not a different value: `highlight_player_on` takes a display name off
 * the STRING stack (`highlight_player_on(_6900, 5)`), and a name is what it
 * stays until something matches it against the players in the scene. The
 * reference does the same and for the same reason -- its player channel is a
 * name-hashed map while every other kind is keyed by an int.
 *
 * No `flags`: every call site passes the group's own, and the reference's ON
 * hands its manager a flag word of 0.
 */
struct RS_HighlightNamedMember
{
    /** RS_HIGHLIGHT_PLAYER or RS_HIGHLIGHT_OPGROUP. One list for both, because
     *  a name is a name; the kind is what says which pool to match it in. */
    int kind;
    int group;
    char name[RS_HIGHLIGHT_NAME_MAX];
};

struct RS_HighlightState
{
    struct RS_HighlightStyle style[RS_HIGHLIGHT_KIND_COUNT][RS_HIGHLIGHT_GROUP_MAX];
    struct RS_HighlightMember member[RS_HIGHLIGHT_KIND_COUNT][RS_HIGHLIGHT_MEMBERS_MAX];
    int member_count[RS_HIGHLIGHT_KIND_COUNT];
    /** The PLAYER kind's subjects. See RS_HighlightNamedMember. */
    struct RS_HighlightNamedMember named[RS_HIGHLIGHT_NAMED_MAX];
    int named_count;
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

/**
 * The two name-keyed kinds' ON / OFF / GET.
 *
 * `name` is compared exactly, which is what the reference's name hash does and
 * what makes the round trip work: the only name the cache ever puts in one of
 * these is `_6900`, this client's own report of a player's name, so the string
 * that goes in is the string that comes back. An OPGROUP name is matched
 * against an entity's own name for the same reason. A name longer than
 * RS_HIGHLIGHT_NAME_MAX-1 is refused rather than truncated -- a truncated key
 * would silently mark a different player.
 *
 * ON is idempotent and OFF tolerates a name that is not there, for the same
 * reason the int-keyed forms do: the cache's mouse-over highlighter calls OFF
 * on five groups every tick on its way to a rebuild.
 */
bool RS_HighlightNameOn(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group,
    char const* name);
void RS_HighlightNameOff(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group,
    char const* name);
bool RS_HighlightNameGet(
    struct RS_HighlightState const* state,
    enum RS_HighlightKind kind,
    int group,
    char const* name);

/** Empties `group` of the kind's subjects. For the two name-keyed kinds that
 *  is the named list, which is where their subjects live. */
void RS_HighlightClear(
    struct RS_HighlightState* state,
    enum RS_HighlightKind kind,
    int group);

/**
 * Is this group drawable at all?
 *
 * The reference's own `HighlightChannelProperties::IsEnabled` -- an outline
 * flag with a non-zero thickness, or a fill flag with a non-zero opacity. See
 * the flag block above for the four predicates it is made of.
 *
 * The two halves are what make the odd-looking groups work: the six mouseover
 * groups run at opacity 0 (an outline has no wash to be opaque) and the
 * hovered tile runs at thickness 0 (a wash with no border). Testing either one
 * alone silently drops one of those families.
 */
bool RS_HighlightGroupLive(
    struct RS_HighlightState const* state,
    enum RS_HighlightKind kind,
    int group);

/**
 * Apply one HIGHLIGHT_* opcode.
 *
 * `args` is in PUSH order, as CS2VM2_Op_Highlight pops them: args[0] is the
 * first int the script pushed. `name` is the subject the PLAYER family's ON /
 * OFF / GET take off the string stack, and NULL for every other form; passing
 * NULL to one that needs it refuses the op rather than acting on an empty
 * name, which would be a highlight on the player called "". `*out_query` is
 * filled for the GET forms and untouched otherwise; the caller pushes it.
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
    char const* name,
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
