#ifndef SRC_NET_REV_PKT_PLAYER_APPEARANCE_H
#define SRC_NET_REV_PKT_PLAYER_APPEARANCE_H

/*
 * The player appearance block — the one place this tree defines it.
 *
 * Three things live here because they are the same thing seen from three
 * sides, and keeping them apart is what let them drift:
 *
 *   1. THE SLOT VOCABULARY — the "12 int buffer" every renderer in this client
 *      composites a player from (entity_model_build.c, the chathead build, the
 *      design preview, the uitree scene bridge). One int per wear position,
 *      tagged: empty, a body kit, or a worn obj.
 *
 *   2. EVERY WIRE ENCODING of that vocabulary, decode and the tag half of
 *      encode. A revision does not change what an appearance IS, it changes how
 *      the tag is spelled — `0x200 + obj` classic, `0x800 + obj` at 239 — and a
 *      value written under one spelling and read under the other is not a
 *      decode failure. It is a different, perfectly valid body part. The tags
 *      are therefore stated once, next to each other, where a mismatch is
 *      visible rather than spread over an encoder and a decoder in different
 *      directories.
 *
 *   3. AN ENCODING-INDEPENDENT DECODER. Each wire layout is read into one
 *      command stream, the way pkt_npc_info.c reads the npc bitstream into
 *      `PktNpcInfoOp`. Ops carry canonical values — a slot op names a kit or an
 *      obj, never a tagged word — so a consumer that walks ops cannot tell
 *      which revision produced them, and a new revision is a new reader
 *      function rather than a branch in every consumer.
 *
 * Op payloads are fixed-size by design: unlike the npc/player info streams
 * there is no unbounded field here (the longest is a display name), so there is
 * no heap and therefore no `_ops_free` to forget.
 */

#include <stdint.h>

#define APPEARANCE_SLOT_COUNT 12
#define APPEARANCE_COLOUR_COUNT 5
#define APPEARANCE_ANIM_COUNT 7
/* Long enough for a display name (12) and the 239 name extras with room to
 * spare; a longer string on the wire is truncated, never overrun. */
#define APPEARANCE_TEXT_MAX 32

/* ------------------------------------------------------------------ */
/* 1. The slot vocabulary                                              */
/* ------------------------------------------------------------------ */

enum AppearanceSlotKind
{
    /* Nothing drawn here. Either the wear position is bare, or a worn item
     * covers it (a full helm blanks hair and jaw, a platebody blanks arms). */
    APPEARANCE_SLOT_EMPTY = 0,
    APPEARANCE_SLOT_KIT, /* IdentityKit body part */
    APPEARANCE_SLOT_OBJ  /* worn obj */
};

/*
 * The canonical packing the engine's int[12] carries.
 *
 * Deliberately NOT the classic wire tags, which is what it used to be — and
 * being the same number is exactly why the wire spelling leaked into the
 * renderers in the first place. It is also too narrow to be the engine's:
 * `0x100 + kit` leaves 256 kit ids before it collides with the obj range, and
 * the osrs239 cache ships 307 identity kits, so kit 300 packed the classic way
 * reads back as obj 44 — a valid, wrong item. The canonical ranges are wide
 * enough for every id either wire can carry; the narrow wire tags are applied
 * only at the wire, by Appearance_WirePack.
 *
 * Renderers use the helpers below; only the readers and writers in this module
 * touch the wire.
 */
#define APPEARANCE_PACK_KIT 0x10000
#define APPEARANCE_PACK_OBJ 0x20000

static inline int
Appearance_PackKit(int kit_id)
{
    return APPEARANCE_PACK_KIT + kit_id;
}

static inline int
Appearance_PackObj(int obj_id)
{
    return APPEARANCE_PACK_OBJ + obj_id;
}

/** Empty for anything below the kit range — including the -1 that callers
 *  building a partial slot array (held-item overrides) use for "no model". */
static inline enum AppearanceSlotKind
Appearance_SlotKind(int packed)
{
    if( packed >= APPEARANCE_PACK_OBJ )
        return APPEARANCE_SLOT_OBJ;
    if( packed >= APPEARANCE_PACK_KIT )
        return APPEARANCE_SLOT_KIT;
    return APPEARANCE_SLOT_EMPTY;
}

/** IdentityKit id, or -1 when this slot is not a kit. */
static inline int
Appearance_SlotKit(int packed)
{
    return Appearance_SlotKind(packed) == APPEARANCE_SLOT_KIT ? packed - APPEARANCE_PACK_KIT : -1;
}

/** Obj id, or -1 when this slot is not a worn obj. */
static inline int
Appearance_SlotObj(int packed)
{
    return Appearance_SlotKind(packed) == APPEARANCE_SLOT_OBJ ? packed - APPEARANCE_PACK_OBJ : -1;
}

/* ------------------------------------------------------------------ */
/* 2. The wire encodings                                               */
/* ------------------------------------------------------------------ */

enum AppearanceEncoding
{
    /*
     * lc254 / lc245_2 / xrsps233 / osrs230: gender g1, overhead icons g1, ONE
     * 12-entry slot array (`0x100 + kit`, `0x200 + obj`), 5 colours g1, 7
     * animations g2, name g8 base-37, combat level g1.
     */
    APPEARANCE_ENC_CLASSIC = 0,
    /*
     * osrs239 (RSProt `PlayerAppearanceEncoder`). Not a reordering — a
     * different shape:
     *   - TWO 12-entry arrays: `equipment` (what is drawn) then `identKit` (the
     *     body underneath). A 239 client fed the classic single array consumes
     *     the whole thing as equipment and then reads the colours as an
     *     identKit;
     *   - a worn obj is `0x800 + obj`, which lands inside the classic obj range
     *     and would otherwise draw an unrelated body part;
     *   - skull icon and overhead icon are separate signed bytes;
     *   - the name is a NUL-terminated string, not 8 base-37 bytes;
     *   - and five fields follow the combat level that the classic block has no
     *     room for at all.
     */
    APPEARANCE_ENC_V5 = 1,
    /*
     * lc289 (LostCity's January-2005 build): the classic block plus a two-byte
     * SKILL LEVEL after the combat level, and nothing else changed
     * (webclient ClientPlayer.setAppearance, branch 289 — the `team` field
     * beside it is derived from ObjType and costs no wire bytes).
     *
     * A suffix, so a 254 decoder reading a 289 block gets every field it knows
     * about right and simply stops two bytes early. That is why this went
     * unnoticed: the appearance blob is length-prefixed inside PLAYER_INFO, so
     * the under-read cannot desync the packet -- it just silently drops the
     * one field 289 added.
     */
    APPEARANCE_ENC_CLASSIC_LC289 = 2
};

/* Wire tags, stated together so a mismatch is one line apart rather than one
 * directory apart. */
#define APPEARANCE_WIRE_KIT_TAG 0x100
#define APPEARANCE_WIRE_OBJ_TAG_CLASSIC 0x200
#define APPEARANCE_WIRE_OBJ_TAG_V5 0x800
/* 239 only, and only in the first equipment entry: the rest of the equipment
 * array is replaced by a 2-byte npc id and the player renders as that npc. */
#define APPEARANCE_WIRE_TRANSMOG 0xffff

static inline int
Appearance_WireObjTag(enum AppearanceEncoding encoding)
{
    return encoding == APPEARANCE_ENC_V5 ? APPEARANCE_WIRE_OBJ_TAG_V5
                                         : APPEARANCE_WIRE_OBJ_TAG_CLASSIC;
}

/**
 * Canonical slot -> the word this encoding puts on the wire (0 = empty, and an
 * empty slot costs one byte where anything else costs two).
 *
 * An id the encoding cannot carry comes back as 0 — an empty slot, i.e. a
 * missing body part — rather than as a tagged word that would land in the next
 * range and draw something else entirely. The classic block has room for 256
 * kits and the 239 block for 1792, so this is reachable: the osrs239 cache
 * ships 307 identity kits.
 */
static inline int
Appearance_WirePack(
    enum AppearanceEncoding encoding,
    int packed)
{
    int const obj_tag = Appearance_WireObjTag(encoding);

    switch( Appearance_SlotKind(packed) )
    {
    case APPEARANCE_SLOT_KIT:
    {
        int kit = APPEARANCE_WIRE_KIT_TAG + Appearance_SlotKit(packed);
        return kit >= obj_tag ? 0 : kit;
    }
    case APPEARANCE_SLOT_OBJ:
    {
        int obj = obj_tag + Appearance_SlotObj(packed);
        return obj >= APPEARANCE_WIRE_TRANSMOG ? 0 : obj;
    }
    case APPEARANCE_SLOT_EMPTY:
    default:
        return 0;
    }
}

/** Wire word -> canonical slot. An obj tag that this encoding does not use
 *  falls through to the kit range, which is the silent failure the single
 *  definition exists to prevent — so callers decode through here, never by
 *  comparing against a literal. */
static inline int
Appearance_WireUnpack(
    enum AppearanceEncoding encoding,
    int wire)
{
    int const obj_tag = Appearance_WireObjTag(encoding);

    if( wire >= obj_tag )
        return Appearance_PackObj(wire - obj_tag);
    if( wire >= APPEARANCE_WIRE_KIT_TAG )
        return Appearance_PackKit(wire - APPEARANCE_WIRE_KIT_TAG);
    return 0;
}

/**
 * The cache speaks this encoding too.
 *
 * A sequence's `replaceheldleft` / `replaceheldright` (SeqType opcodes 6/7) are
 * appearance slots in the classic spelling: an obj-range value swaps the held
 * item, anything lower hides it. They arrive from the cache rather than from a
 * server, so they are converted here, once, instead of being compared against a
 * tag at the two call sites that use them.
 */
static inline int
Appearance_FromCacheValue(int cache_value)
{
    return cache_value < 0 ? 0 : Appearance_WireUnpack(APPEARANCE_ENC_CLASSIC, cache_value);
}

/* ------------------------------------------------------------------ */
/* 3. The encoding-independent command stream                          */
/* ------------------------------------------------------------------ */

/** Which of the two 239 arrays a slot op came from. The classic block has one
 *  array and every slot op is VISIBLE. */
enum AppearanceSlotLayer
{
    APPEARANCE_LAYER_VISIBLE = 0, /* what is drawn (239 `equipment`) */
    APPEARANCE_LAYER_IDENTKIT = 1 /* the body underneath (239 `identKit`) */
};

/** Base animation set, in wire order (all encodings agree on the order). */
enum AppearanceAnim
{
    APPEARANCE_ANIM_READY = 0,
    APPEARANCE_ANIM_TURN,
    APPEARANCE_ANIM_WALK,
    APPEARANCE_ANIM_WALK_BACK,
    APPEARANCE_ANIM_WALK_LEFT,
    APPEARANCE_ANIM_WALK_RIGHT,
    APPEARANCE_ANIM_RUN
};

/** 239 only: decoration rendered around the name/level. */
enum AppearanceNameExtra
{
    APPEARANCE_NAME_BEFORE = 0,
    APPEARANCE_NAME_AFTER,
    APPEARANCE_NAME_AFTER_COMBAT_LEVEL
};

enum PktAppearanceOpKind
{
    PKT_APPEARANCE_OP_NONE = 0,
    PKT_APPEARANCE_OP_GENDER,     /* _value: 0 male, 1 female */
    PKT_APPEARANCE_OP_HEAD_ICON,  /* _value: BITMASK of overhead icons */
    PKT_APPEARANCE_OP_SKULL_ICON, /* _value: index, -1 none */
    PKT_APPEARANCE_OP_SLOT,       /* _slot */
    PKT_APPEARANCE_OP_TRANSMOG,   /* _value: npc id the player renders as */
    PKT_APPEARANCE_OP_COLOUR,     /* _indexed: design palette index per part */
    PKT_APPEARANCE_OP_BAS_ANIM,   /* _indexed: enum AppearanceAnim -> seq, -1 none */
    PKT_APPEARANCE_OP_NAME,       /* _text */
    PKT_APPEARANCE_OP_COMBAT_LEVEL, /* _value */
    PKT_APPEARANCE_OP_SKILL_LEVEL,  /* _value: shown only in some minigames */
    PKT_APPEARANCE_OP_HIDDEN,       /* _value: 1 = do not render this player */
    PKT_APPEARANCE_OP_NAME_EXTRA,   /* _text, index = enum AppearanceNameExtra */
    PKT_APPEARANCE_OP_PRONOUN       /* _value */
};

struct PktAppearance_Slot
{
    uint8_t index; /* wear position, 0..11 */
    uint8_t layer; /* enum AppearanceSlotLayer */
    uint8_t kind;  /* enum AppearanceSlotKind */
    int id;        /* kit id / obj id; -1 when empty */
    int packed;    /* the canonical int a renderer wants */
};

struct PktAppearance_Indexed
{
    int index;
    int value;
};

struct PktAppearance_Text
{
    int index; /* enum AppearanceNameExtra for NAME_EXTRA; 0 otherwise */
    char text[APPEARANCE_TEXT_MAX];
};

struct PktAppearanceOp
{
    enum PktAppearanceOpKind kind;

    union
    {
        int _value;
        struct PktAppearance_Slot _slot;
        struct PktAppearance_Indexed _indexed;
        struct PktAppearance_Text _text;
    };
};

/* gender + 2 icons + 24 slots + 5 colours + 7 anims + name + combat level +
 * skill level + hidden + 3 name extras + pronoun = 46; rounded up. */
#define PKT_APPEARANCE_OPS_MAX 48

/**
 * Decode one appearance block into the command stream.
 *
 * Returns the op count, or -1 when the block is short, malformed, or carries
 * something this decoder refuses to guess at (239 per-obj customisations, whose
 * blocks are variable-length — skipping them wrongly would corrupt every field
 * after, so the block is rejected whole).
 */
int
pkt_appearance_read(
    enum AppearanceEncoding encoding,
    uint8_t const* data,
    int len,
    struct PktAppearanceOp* ops,
    int ops_capacity);

/* ------------------------------------------------------------------ */
/* The folded form the client applies                                  */
/* ------------------------------------------------------------------ */

/*
 * The op stream folded into one struct — what App_WorldApplyPlayerAppearance
 * and the model builders consume. Kept because the client applies an appearance
 * as a whole (it rebuilds one composite model), not incrementally; the ops are
 * what make that fold encoding-independent.
 */
struct PktPlayerAppearance
{
    int gender;
    int headicon; /* bitmask over the headicons sprite pack */
    int skull_icon; /* -1 none; 239 only, no consumer yet */
    int slots[APPEARANCE_SLOT_COUNT];    /* canonical: empty | kit | obj */
    int identkit[APPEARANCE_SLOT_COUNT]; /* 239's body-underneath array, canonical */
    int colors[APPEARANCE_COLOUR_COUNT];
    int readyanim;
    int turnanim;
    int walkanim;
    int walkanim_b;
    int walkanim_l;
    int walkanim_r;
    int runanim;
    char name[APPEARANCE_TEXT_MAX];
    int combat_level;
    /* 239 extras; zero/empty under every other encoding. */
    int npc_id; /* transmog: render as this npc instead, -1 = none */
    int skill_level;
    int hidden;
    int pronoun;
    char name_extra[3][APPEARANCE_TEXT_MAX]; /* enum AppearanceNameExtra */
};

/** Zero, then set the fields whose "absent" value is not 0 (animations -1,
 *  npc_id -1, skull -1). Always call before ApplyOps. */
void
PktPlayerAppearance_Init(struct PktPlayerAppearance* out);

/** Fold a command stream onto an initialised appearance. Returns 1. */
int
PktPlayerAppearance_ApplyOps(
    struct PktPlayerAppearance* out,
    struct PktAppearanceOp const* ops,
    int op_count);

/** read + fold. Returns 1 on success, 0 on a short/invalid block. */
int
PktPlayerAppearance_DecodeAs(
    struct PktPlayerAppearance* out,
    enum AppearanceEncoding encoding,
    uint8_t const* buf,
    int len);

/** The classic block, i.e. `DecodeAs(APPEARANCE_ENC_CLASSIC)`. Every revision
 *  that leaves `GameProtoRevTable.appearance_decode` NULL means this one. */
int
PktPlayerAppearance_Decode(
    struct PktPlayerAppearance* out,
    uint8_t const* buf,
    int len);

#endif
