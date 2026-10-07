#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_MAPELEMENT_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_MAPELEMENT_H

#include "../rsbuffer.h"
#include "../rscache_presence.h"

/**
 * Map element config ("mapFunctions", config group 35) — the icon, label and
 * category of one world map element.
 *
 * ## Every opcode is kept
 *
 * This decoder used to keep the four fields the client reads back through the
 * MEC_* script opcodes (sprite, name, text size, category) and consume the rest
 * only to stay aligned, so an encode was a valid record with less in it: every
 * one of osrs239's 1,261 records lost its opcode 8, and 385 their opcode 7 as
 * well. Each operand is now stored and written back. The fields the client is
 * not known to use are named by their opcode (`unknown21`) rather than by a
 * guess at their meaning.
 *
 * The opcode set is the union of the rev-239 reader (`class373.method8661` in
 * the deob: 1-8, 10-19, 21-25, 28-30) and the older readers this decoder already
 * knew (9, 20 and 249, which rev 239 no longer reads). Only 1, 3, 4, 6, 7, 8, 10,
 * 17 and 19 occur in any cache in the tree.
 */

/**
 * The fields a map element stream can state, for `RSCache_MapElement.present`.
 * Keyed by meaning; the five op strings are five separate opcodes and so five
 * fields, `RSCACHE_MAPELEMENT_FIELD_OP1 + i`.
 */
enum RSCache_MapElementField
{
    RSCACHE_MAPELEMENT_FIELD_SPRITE = 0,         /* 1 */
    RSCACHE_MAPELEMENT_FIELD_HOVER_SPRITE,       /* 2 */
    RSCACHE_MAPELEMENT_FIELD_NAME,               /* 3 */
    RSCACHE_MAPELEMENT_FIELD_TEXT_COLOUR,        /* 4 */
    RSCACHE_MAPELEMENT_FIELD_HOVER_TEXT_COLOUR,  /* 5 */
    RSCACHE_MAPELEMENT_FIELD_TEXT_SIZE,          /* 6 */
    RSCACHE_MAPELEMENT_FIELD_FLAGS,              /* 7 */
    RSCACHE_MAPELEMENT_FIELD_RANDOMIZE_POSITION, /* 8 */
    RSCACHE_MAPELEMENT_FIELD_VISIBILITY,         /* 9 */
    RSCACHE_MAPELEMENT_FIELD_OP1,                /* 10 .. */
    RSCACHE_MAPELEMENT_FIELD_OP2,
    RSCACHE_MAPELEMENT_FIELD_OP3,
    RSCACHE_MAPELEMENT_FIELD_OP4,
    RSCACHE_MAPELEMENT_FIELD_OP5,                /* .. 14 */
    RSCACHE_MAPELEMENT_FIELD_POLYGON,            /* 15 */
    RSCACHE_MAPELEMENT_FIELD_UNKNOWN16,          /* 16, no operand */
    RSCACHE_MAPELEMENT_FIELD_TARGET_NAME,        /* 17 */
    RSCACHE_MAPELEMENT_FIELD_UNKNOWN18,          /* 18 */
    RSCACHE_MAPELEMENT_FIELD_CATEGORY,           /* 19 */
    RSCACHE_MAPELEMENT_FIELD_VISIBILITY2,        /* 20 */
    RSCACHE_MAPELEMENT_FIELD_UNKNOWN21,          /* 21 */
    RSCACHE_MAPELEMENT_FIELD_UNKNOWN22,          /* 22 */
    RSCACHE_MAPELEMENT_FIELD_UNKNOWN23,          /* 23 */
    RSCACHE_MAPELEMENT_FIELD_UNKNOWN24,          /* 24 */
    RSCACHE_MAPELEMENT_FIELD_UNKNOWN25,          /* 25 */
    RSCACHE_MAPELEMENT_FIELD_UNKNOWN28,          /* 28 */
    RSCACHE_MAPELEMENT_FIELD_HORIZONTAL_ALIGN,   /* 29 */
    RSCACHE_MAPELEMENT_FIELD_VERTICAL_ALIGN,     /* 30 */
    RSCACHE_MAPELEMENT_FIELD_PARAMS,             /* 249 */
    RSCACHE_MAPELEMENT_FIELD_COUNT
};

#define RSCACHE_MAPELEMENT_OPS 5

/** Opcodes 9 and 20: show the element only while a var reads within a range.
 *  `varbit`/`varp` are -1 for 65535 on the wire. */
struct RSCache_MapElementVisibility
{
    int varbit;
    int varp;
    int min;
    int max;
};

struct RSCache_MapElement
{
    /** Which `RSCache_MapElementField`s the stream stated. The encoder writes
     *  exactly these; the values below hold the defaults for the rest. */
    struct RSCache_Presence present;
    int id;

    /* What the client reads back (MEC_*). */
    char* name;    /* 3 */
    int text_size; /* 6 */
    int category;  /* 19; -1 when absent */
    int sprite_id; /* 1, big smart; -1 when absent */

    /* The rest, verbatim. */
    int hover_sprite_id;                             /* 2, big smart; -1 when absent */
    int text_colour;                                 /* 4, 24-bit */
    int hover_text_colour;                           /* 5, 24-bit */
    int flags;                                       /* 7 */
    int randomize_position;                          /* 8 */
    struct RSCache_MapElementVisibility visibility;  /* 9 */
    struct RSCache_MapElementVisibility visibility2; /* 20 */
    char* ops[RSCACHE_MAPELEMENT_OPS];               /* 10..14 */
    char* target_name;                               /* 17 */

    /** Opcode 15: `polygon_point_count` vertices, each an (x, y) pair of signed
     *  shorts in `polygon_points` and a byte in `polygon_point_flags`; between
     *  them one int and a counted list of ints. */
    int polygon_point_count;
    int* polygon_points;      /* polygon_point_count * 2 */
    int* polygon_point_flags; /* polygon_point_count */
    int polygon_fill;
    int polygon_extra_count;
    int* polygon_extra;

    int unknown18;                /* big smart */
    int unknown21;                /* i32 */
    int unknown22;                /* i32 */
    int unknown23[3];             /* three u8 */
    int unknown24[2];             /* two signed shorts */
    int unknown25;                /* big smart */
    int unknown28;                /* u8 */
    int horizontal_align;         /* 29 */
    int vertical_align;           /* 30 */
    struct RSCache_Params params; /* 249 */

    /** Bytes consumed. Equal to the record size for a fully understood record. */
    int _consumed;
};

/** The defaults, nothing stated, on an already-zeroed record. */
void
RSCache_MapElementInit(struct RSCache_MapElement* entry);

/** Decode one record into a zeroed `entry`. Stops on an opcode it does not know,
 *  leaving `_consumed` short. */
void
RSCache_MapElementDecodeInplace(
    struct RSCache_MapElement* entry,
    const void* data,
    int data_size);

/**
 * Encode exactly the fields `present` names.
 *
 * In the one order that reproduces both shapes the caches use — `3,6,4,19,8,7`
 * (labels) and `19,1,10,8,7,17` (icons) — then the opcodes no cache in the tree
 * carries, ascending. Returns bytes written.
 */
uint32_t
RSCache_MapElementEncode(
    const struct RSCache_MapElement* entry,
    uint8_t* out,
    uint32_t out_capacity);

void
RSCache_MapElementFreeInplace(struct RSCache_MapElement* entry);

/** An upper bound on what `RSCache_MapElementEncode` will write. */
uint32_t
RSCache_MapElementEncodeBound(const struct RSCache_MapElement* entry);

#endif
