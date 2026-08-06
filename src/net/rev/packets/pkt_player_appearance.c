#include "pkt_player_appearance.h"

#include "net/jbase37.h"

#include <rsbuffer.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Reading primitives                                                   */
/* ------------------------------------------------------------------ */

/*
 * rsbuffer's getters return 0 at the end of the buffer instead of failing, so a
 * truncated block decodes into a plausible naked player rather than an error.
 * Every stage below asks for its bytes first.
 */
static int
need(
    struct RSCache_Buffer* rsbuf,
    uint32_t bytes)
{
    return RSCache_BufferRemaining(rsbuf) >= bytes;
}

/** A signed byte field: 255 means -1 (no icon / no pronoun). */
static int
g1s(struct RSCache_Buffer* rsbuf)
{
    return (int)(int8_t)(uint8_t)g1(rsbuf);
}

/** An animation id; the wire's "none" is 65535. */
static int
anim_g2(struct RSCache_Buffer* rsbuf)
{
    int value = g2(rsbuf);
    return value == 65535 ? -1 : value;
}

/**
 * A NUL-terminated string, bounded by both the buffer and the destination.
 *
 * Returns 0 when no terminator is found before the end of the block: that is a
 * malformed block, not a long name, and continuing would read every later field
 * at the wrong offset.
 */
static int
read_jstr(
    struct RSCache_Buffer* rsbuf,
    char* out,
    int cap)
{
    int written = 0;

    for( ;; )
    {
        int ch;

        if( !need(rsbuf, 1) )
            return 0;
        ch = g1(rsbuf);
        if( ch == 0 )
            break;
        if( written < cap - 1 )
            out[written++] = (char)ch;
    }
    out[written] = '\0';
    return 1;
}

/* ------------------------------------------------------------------ */
/* Op emission                                                          */
/* ------------------------------------------------------------------ */

struct OpWriter
{
    struct PktAppearanceOp* ops;
    int capacity;
    int count;
    int overflow;
};

static struct PktAppearanceOp*
op_push(
    struct OpWriter* w,
    enum PktAppearanceOpKind kind)
{
    struct PktAppearanceOp* op;

    if( w->count >= w->capacity )
    {
        w->overflow = 1;
        return NULL;
    }
    op = &w->ops[w->count++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    return op;
}

static void
op_value(
    struct OpWriter* w,
    enum PktAppearanceOpKind kind,
    int value)
{
    struct PktAppearanceOp* op = op_push(w, kind);
    if( op )
        op->_value = value;
}

static void
op_indexed(
    struct OpWriter* w,
    enum PktAppearanceOpKind kind,
    int index,
    int value)
{
    struct PktAppearanceOp* op = op_push(w, kind);
    if( op )
    {
        op->_indexed.index = index;
        op->_indexed.value = value;
    }
}

static void
op_text(
    struct OpWriter* w,
    enum PktAppearanceOpKind kind,
    int index,
    char const* text)
{
    struct PktAppearanceOp* op = op_push(w, kind);
    if( op )
    {
        op->_text.index = index;
        snprintf(op->_text.text, sizeof(op->_text.text), "%s", text ? text : "");
    }
}

/** The one place a wire word becomes a slot op — both encodings, both layers. */
static void
op_slot(
    struct OpWriter* w,
    enum AppearanceEncoding encoding,
    enum AppearanceSlotLayer layer,
    int index,
    int wire)
{
    struct PktAppearanceOp* op = op_push(w, PKT_APPEARANCE_OP_SLOT);
    int packed;

    if( !op )
        return;
    packed = Appearance_WireUnpack(encoding, wire);
    op->_slot.index = (uint8_t)index;
    op->_slot.layer = (uint8_t)layer;
    op->_slot.kind = (uint8_t)Appearance_SlotKind(packed);
    op->_slot.packed = packed;
    switch( Appearance_SlotKind(packed) )
    {
    case APPEARANCE_SLOT_KIT:
        op->_slot.id = Appearance_SlotKit(packed);
        break;
    case APPEARANCE_SLOT_OBJ:
        op->_slot.id = Appearance_SlotObj(packed);
        break;
    case APPEARANCE_SLOT_EMPTY:
    default:
        op->_slot.id = -1;
        break;
    }
}

/**
 * One slot entry: a zero first byte is an empty slot and costs one byte,
 * anything else is the high byte of a two-byte word. Shared by both encodings
 * and both 239 arrays — the framing never changed, only the tags did.
 *
 * Returns the wire word, or -1 when the entry runs off the end.
 */
static int
read_slot_word(struct RSCache_Buffer* rsbuf)
{
    int msb;

    if( !need(rsbuf, 1) )
        return -1;
    msb = g1(rsbuf);
    if( msb == 0 )
        return 0;
    if( !need(rsbuf, 1) )
        return -1;
    return (msb << 8) | g1(rsbuf);
}

static void
read_colours_and_anims(
    struct OpWriter* w,
    struct RSCache_Buffer* rsbuf)
{
    for( int i = 0; i < APPEARANCE_COLOUR_COUNT; i++ )
        op_indexed(w, PKT_APPEARANCE_OP_COLOUR, i, g1(rsbuf));
    for( int i = 0; i < APPEARANCE_ANIM_COUNT; i++ )
        op_indexed(w, PKT_APPEARANCE_OP_BAS_ANIM, i, anim_g2(rsbuf));
}

/* ------------------------------------------------------------------ */
/* The classic block                                                    */
/* ------------------------------------------------------------------ */

/*
 * gender g1, overhead icons g1, 12 slots, 5 colours g1, 7 animations g2, name
 * g8 base-37, combat level g1. Minimum 42 bytes, with every slot empty.
 *
 * The icon byte is a BITMASK here, not an index: a real rev-230 appearance
 * carries a prayer index and a skull index, but this tree's classic pairing is
 * the older shape the client renders (app.c plots every set bit, stacked
 * upward). See docs/mock230_player_systems.md §4.
 */
static int
read_classic(
    struct OpWriter* w,
    struct RSCache_Buffer* rsbuf)
{
    if( !need(rsbuf, 2) )
        return 0;
    op_value(w, PKT_APPEARANCE_OP_GENDER, g1(rsbuf));
    op_value(w, PKT_APPEARANCE_OP_HEAD_ICON, g1(rsbuf));

    for( int i = 0; i < APPEARANCE_SLOT_COUNT; i++ )
    {
        int wire = read_slot_word(rsbuf);
        if( wire < 0 )
            return 0;
        op_slot(w, APPEARANCE_ENC_CLASSIC, APPEARANCE_LAYER_VISIBLE, i, wire);
    }

    if( !need(rsbuf, APPEARANCE_COLOUR_COUNT + APPEARANCE_ANIM_COUNT * 2 + 8 + 1) )
        return 0;
    read_colours_and_anims(w, rsbuf);

    {
        char name[APPEARANCE_TEXT_MAX];
        base37tostr((uint64_t)g8(rsbuf), name, (int)sizeof(name));
        op_text(w, PKT_APPEARANCE_OP_NAME, 0, name);
    }
    op_value(w, PKT_APPEARANCE_OP_COMBAT_LEVEL, g1(rsbuf));
    return 1;
}

/* ------------------------------------------------------------------ */
/* The 239 (v5) block                                                   */
/* ------------------------------------------------------------------ */

/*
 * Transcribed against RSProt's `PlayerAppearanceEncoder` (the encoder, not its
 * reference test decoder — the decoder stops after the three name extras and
 * misses the trailing pronoun byte, which shifts the whole block's length).
 */
static int
read_v5(
    struct OpWriter* w,
    struct RSCache_Buffer* rsbuf)
{
    if( !need(rsbuf, 3) )
        return 0;
    op_value(w, PKT_APPEARANCE_OP_GENDER, g1(rsbuf));
    op_value(w, PKT_APPEARANCE_OP_SKULL_ICON, g1s(rsbuf));
    /*
     * 239 carries an overhead-icon INDEX (-1 = none) where the classic block
     * carries a mask. The op is defined as a mask, so the conversion happens
     * here rather than in every consumer — that is the whole point of the op
     * stream. One icon at a time is all the wire can express at this revision.
     */
    {
        int icon = g1s(rsbuf);
        op_value(w, PKT_APPEARANCE_OP_HEAD_ICON, icon < 0 ? 0 : (1 << icon));
    }

    /* `equipment` — what is actually drawn. */
    for( int i = 0; i < APPEARANCE_SLOT_COUNT; i++ )
    {
        int wire = read_slot_word(rsbuf);
        if( wire < 0 )
            return 0;
        /*
         * A first entry of 65535 is not a slot: the player is transmogged, an
         * npc id follows, and the equipment array ENDS there — the remaining
         * eleven entries are never written.
         */
        if( i == 0 && wire == APPEARANCE_WIRE_TRANSMOG )
        {
            if( !need(rsbuf, 2) )
                return 0;
            op_value(w, PKT_APPEARANCE_OP_TRANSMOG, g2(rsbuf));
            break;
        }
        op_slot(w, APPEARANCE_ENC_V5, APPEARANCE_LAYER_VISIBLE, i, wire);
    }

    /* `identKit` — the body underneath, written whether or not it shows. */
    for( int i = 0; i < APPEARANCE_SLOT_COUNT; i++ )
    {
        int wire = read_slot_word(rsbuf);
        if( wire < 0 )
            return 0;
        op_slot(w, APPEARANCE_ENC_V5, APPEARANCE_LAYER_IDENTKIT, i, wire);
    }

    if( !need(rsbuf, APPEARANCE_COLOUR_COUNT + APPEARANCE_ANIM_COUNT * 2) )
        return 0;
    read_colours_and_anims(w, rsbuf);

    {
        char name[APPEARANCE_TEXT_MAX];
        if( !read_jstr(rsbuf, name, (int)sizeof(name)) )
            return 0;
        op_text(w, PKT_APPEARANCE_OP_NAME, 0, name);
    }

    if( !need(rsbuf, 1 + 2 + 1 + 2) )
        return 0;
    op_value(w, PKT_APPEARANCE_OP_COMBAT_LEVEL, g1(rsbuf));
    op_value(w, PKT_APPEARANCE_OP_SKILL_LEVEL, g2(rsbuf));
    op_value(w, PKT_APPEARANCE_OP_HIDDEN, g1(rsbuf) == 1);

    /*
     * The obj-customisation flag.
     *
     * Bit 15 is `forceModelRefresh` and carries no payload. Bits 1..12 (one per
     * wear position, `1 << (12 - wearpos)`) each mean a VARIABLE-LENGTH
     * customisation block follows — recolour/retexture indices and replacement
     * model ids, each field optional behind its own flag byte. Skipping one
     * wrongly does not lose a field, it reads every later field at the wrong
     * offset, so an unrecognised block rejects the whole appearance rather than
     * guessing. The server writes 0 here until they are implemented.
     */
    {
        int flag = g2(rsbuf);
        if( flag & 0x1ffe )
            return 0;
    }

    for( int i = 0; i < 3; i++ )
    {
        char extra[APPEARANCE_TEXT_MAX];
        if( !read_jstr(rsbuf, extra, (int)sizeof(extra)) )
            return 0;
        op_text(w, PKT_APPEARANCE_OP_NAME_EXTRA, i, extra);
    }

    if( !need(rsbuf, 1) )
        return 0;
    op_value(w, PKT_APPEARANCE_OP_PRONOUN, g1s(rsbuf));
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                  */
/* ------------------------------------------------------------------ */

int
pkt_appearance_read(
    enum AppearanceEncoding encoding,
    uint8_t const* data,
    int len,
    struct PktAppearanceOp* ops,
    int ops_capacity)
{
    struct RSCache_Buffer rsbuf;
    struct OpWriter w;
    int ok;

    if( !data || !ops || len <= 0 || ops_capacity <= 0 )
        return -1;

    memset(&w, 0, sizeof(w));
    w.ops = ops;
    w.capacity = ops_capacity;
    RSCache_BufferInit(&rsbuf, (uint8_t*)data, (uint32_t)len);

    ok = encoding == APPEARANCE_ENC_V5 ? read_v5(&w, &rsbuf) : read_classic(&w, &rsbuf);
    /* A stream that did not fit is a silently wrong appearance, not a short
     * one — the dropped ops are whichever fields came last. */
    if( !ok || w.overflow )
        return -1;
    return w.count;
}

void
PktPlayerAppearance_Init(struct PktPlayerAppearance* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    out->skull_icon = -1;
    out->npc_id = -1;
    out->readyanim = -1;
    out->turnanim = -1;
    out->walkanim = -1;
    out->walkanim_b = -1;
    out->walkanim_l = -1;
    out->walkanim_r = -1;
    out->runanim = -1;
}

static void
apply_anim(
    struct PktPlayerAppearance* out,
    int which,
    int seq_id)
{
    switch( which )
    {
    case APPEARANCE_ANIM_READY:
        out->readyanim = seq_id;
        break;
    case APPEARANCE_ANIM_TURN:
        out->turnanim = seq_id;
        break;
    case APPEARANCE_ANIM_WALK:
        out->walkanim = seq_id;
        break;
    case APPEARANCE_ANIM_WALK_BACK:
        out->walkanim_b = seq_id;
        break;
    case APPEARANCE_ANIM_WALK_LEFT:
        out->walkanim_l = seq_id;
        break;
    case APPEARANCE_ANIM_WALK_RIGHT:
        out->walkanim_r = seq_id;
        break;
    case APPEARANCE_ANIM_RUN:
        out->runanim = seq_id;
        break;
    default:
        break;
    }
}

int
PktPlayerAppearance_ApplyOps(
    struct PktPlayerAppearance* out,
    struct PktAppearanceOp const* ops,
    int op_count)
{
    if( !out || !ops )
        return 0;

    for( int i = 0; i < op_count; i++ )
    {
        struct PktAppearanceOp const* op = &ops[i];

        switch( op->kind )
        {
        case PKT_APPEARANCE_OP_GENDER:
            out->gender = op->_value;
            break;
        case PKT_APPEARANCE_OP_HEAD_ICON:
            out->headicon = op->_value;
            break;
        case PKT_APPEARANCE_OP_SKULL_ICON:
            out->skull_icon = op->_value;
            break;
        case PKT_APPEARANCE_OP_SLOT:
            if( op->_slot.index < APPEARANCE_SLOT_COUNT )
            {
                if( op->_slot.layer == APPEARANCE_LAYER_IDENTKIT )
                    out->identkit[op->_slot.index] = op->_slot.packed;
                else
                    out->slots[op->_slot.index] = op->_slot.packed;
            }
            break;
        case PKT_APPEARANCE_OP_TRANSMOG:
            out->npc_id = op->_value;
            break;
        case PKT_APPEARANCE_OP_COLOUR:
            if( op->_indexed.index >= 0 && op->_indexed.index < APPEARANCE_COLOUR_COUNT )
                out->colors[op->_indexed.index] = op->_indexed.value;
            break;
        case PKT_APPEARANCE_OP_BAS_ANIM:
            apply_anim(out, op->_indexed.index, op->_indexed.value);
            break;
        case PKT_APPEARANCE_OP_NAME:
            snprintf(out->name, sizeof(out->name), "%s", op->_text.text);
            break;
        case PKT_APPEARANCE_OP_COMBAT_LEVEL:
            out->combat_level = op->_value;
            break;
        case PKT_APPEARANCE_OP_SKILL_LEVEL:
            out->skill_level = op->_value;
            break;
        case PKT_APPEARANCE_OP_HIDDEN:
            out->hidden = op->_value;
            break;
        case PKT_APPEARANCE_OP_NAME_EXTRA:
            if( op->_text.index >= 0 && op->_text.index < 3 )
                snprintf(
                    out->name_extra[op->_text.index],
                    sizeof(out->name_extra[op->_text.index]),
                    "%s",
                    op->_text.text);
            break;
        case PKT_APPEARANCE_OP_PRONOUN:
            out->pronoun = op->_value;
            break;
        case PKT_APPEARANCE_OP_NONE:
        default:
            break;
        }
    }
    return 1;
}

int
PktPlayerAppearance_DecodeAs(
    struct PktPlayerAppearance* out,
    enum AppearanceEncoding encoding,
    uint8_t const* buf,
    int len)
{
    struct PktAppearanceOp ops[PKT_APPEARANCE_OPS_MAX];
    int op_count;

    if( !out )
        return 0;

    op_count = pkt_appearance_read(encoding, buf, len, ops, PKT_APPEARANCE_OPS_MAX);
    if( op_count < 0 )
        return 0;

    PktPlayerAppearance_Init(out);
    return PktPlayerAppearance_ApplyOps(out, ops, op_count);
}

int
PktPlayerAppearance_Decode(
    struct PktPlayerAppearance* out,
    uint8_t const* buf,
    int len)
{
    return PktPlayerAppearance_DecodeAs(out, APPEARANCE_ENC_CLASSIC, buf, len);
}
