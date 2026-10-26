#ifndef RSPROT_INFO_H
#define RSPROT_INFO_H

/*
 * rsprot info streams -- PLAYER_INFO, NPC_INFO and WORLDENTITY_INFO, every
 * revision 223-241: the vocabulary every stream shares.
 *
 * ## The structure, as the hand-written readers have it
 *
 * Each revision has an entry point named for the prot and the revision, and it
 * calls the prot's VERSION:
 *
 *   player_info_osrs227(...)            -> player_info_v2(&k_osrs227, ...)
 *   npc_info_small_v5_osrs239(...)      -> npc_info_small_v5(&k_osrs239, ...)
 *   worldentity_info_v7_osrs241(...)    -> worldentity_info_v7(&k_osrs241, ...)
 *
 * A version (player_info_v1.c, npc_info_v5.c, worldentity_info_v7.c) is one
 * straight-line reader with no revision in it: the stream's sections, then
 * what its revision's constants say. PLAYER_INFO, which Jagex does not
 * version, is versioned by format generation -- a version per change in its set
 * of block shapes.
 *
 * What Jagex reshuffles every revision is the CONSTANTS, generated into
 * <stream>_osrs<N>.c by tools/rsprot_gen.py from spec/streams/<stream>.ini:
 *
 *   - each block's flag bit, and the order the blocks are read in: a generated
 *     dispatch, one `if (flag & bit)` per block in wire order;
 *   - each field's byte transform (g2 / g2Alt1 / g2Alt2 / g2Alt3): the block's
 *     encoding struct, one read function per field;
 *   - the order of a record's fields (the npc entering-view record, the world
 *     entity spawn): a generated record reader.
 *
 * Shared, hand-written, and the same at every revision: the bit sections
 * (rsprot_info_player.c, rsprot_info_npc.c), the world-entity loops
 * (rsprot_info_worldentity.c) and each block's shape (rsprot_info_blocks.c) --
 * a block's fields come in one order at every revision, which the generator
 * checks.
 *
 * ## Why these are not codecs
 *
 * Everything under packets/ is a flat layout one function can run in both
 * directions. The info streams are STATE MACHINES: the player stream walks a
 * 2048-slot table the client keeps across packets, and the npc and world
 * entity streams address their entries by position in a list both ends rebuild
 * during the packet. A byte read out of place desynchronises every packet
 * after it.
 *
 * ## Output
 *
 * A reader writes the list the entity-info executor reads (RsprotInfoList):
 * its ops in wire order, its strings and blobs in the list's byte arena. The
 * list's last op is a STOP when the read was not complete.
 */

#include "../src/rsprot_buf.h"

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Reads                                                               */
/* ------------------------------------------------------------------ */

/** One byte-aligned read, by JagByteBuf name. */
typedef enum RsprotInfoPrim
{
    RSPROT_IP_NONE = 0,
    RSPROT_IP_G1,
    RSPROT_IP_G1_ALT1,
    RSPROT_IP_G1_ALT2,
    RSPROT_IP_G1_ALT3,
    RSPROT_IP_G1S,
    RSPROT_IP_G1S_ALT1,
    RSPROT_IP_G1S_ALT2,
    RSPROT_IP_G1S_ALT3,
    RSPROT_IP_G2,
    RSPROT_IP_G2_ALT1,
    RSPROT_IP_G2_ALT2,
    RSPROT_IP_G2_ALT3,
    RSPROT_IP_G2S,
    RSPROT_IP_G2S_ALT1,
    RSPROT_IP_G2S_ALT2,
    RSPROT_IP_G2S_ALT3,
    RSPROT_IP_G4,
    RSPROT_IP_G4_ALT1,
    RSPROT_IP_G4_ALT2,
    RSPROT_IP_G4_ALT3,
    RSPROT_IP_SMART,         /* gSmart1or2 */
    RSPROT_IP_SMARTNULL,     /* gSmart1or2null */
    RSPROT_IP_SMART2OR4NULL, /* gSmart2or4null */
    RSPROT_IP_JSTR,          /* gjstr */
    RSPROT_IP_DATA,          /* gdata and its three orders */
    RSPROT_IP_DATA_ALT1,
    RSPROT_IP_DATA_ALT2,
    RSPROT_IP_DATA_ALT3,
    RSPROT_IP_COUNT
} RsprotInfoPrim;

/* ------------------------------------------------------------------ */
/* Block shapes                                                        */
/* ------------------------------------------------------------------ */

/**
 * An extended-info block's shape. One per decode function shape across
 * 227-241, player and npc together where RSProx's two clients share one.
 */
typedef enum RsprotInfoKind
{
    RSPROT_IK_NONE = 0,
    /** RSProx throws on the flag (CHAT_OLD, UNUSED_FLAGS): no layout exists. */
    RSPROT_IK_ILLEGAL,
    RSPROT_IK_SAY,
    RSPROT_IK_MOVE_SPEED,
    RSPROT_IK_TEMP_MOVE_SPEED,
    RSPROT_IK_SEQUENCE,
    RSPROT_IK_FACE_PATHING_ENTITY,
    RSPROT_IK_FACE_ANGLE, /* player: angle; npc: angle, instant */
    RSPROT_IK_FACE_COORD, /* npc, 227-231 */
    RSPROT_IK_FACING,     /* 238+: the unified facing block */
    RSPROT_IK_NAME_EXTRAS,
    RSPROT_IK_CHAT,
    RSPROT_IK_EXACT_MOVE,
    RSPROT_IK_SPOTANIM,
    RSPROT_IK_HITS,     /* 227-237: hits and headbars in one block */
    RSPROT_IK_HITMARKS, /* 237+ */
    RSPROT_IK_HEADBARS, /* 237+ */
    RSPROT_IK_TINTING,
    RSPROT_IK_TRANSPARENCY,
    RSPROT_IK_RESET,
    RSPROT_IK_FREEZE,
    RSPROT_IK_APPEARANCE,
    RSPROT_IK_TRANSFORMATION,
    RSPROT_IK_NAME_CHANGE,
    RSPROT_IK_LEVEL_CHANGE,
    RSPROT_IK_ENABLED_OPS,
    RSPROT_IK_BAS_CHANGE,
    /** Body customisation whose recolour and retexture arrays take their
     *  LENGTH from the npc's cache type (no count on the wire): RSProx
     *  BodyCustomisation / V1 / V2. */
    RSPROT_IK_CUSTOM_TYPED,
    /** The same shape for the chathead: HeadCustomisation / V1 / V2. */
    RSPROT_IK_HEAD_CUSTOM_TYPED,
    RSPROT_IK_CUSTOM_V3, /* counted arrays, player composition */
    RSPROT_IK_CUSTOM_V4, /* V3 plus a recolour-all form */
    RSPROT_IK_HEADICON_CUSTOM,
    /** 223-224's second chat block: CHAT without the colour pattern. */
    RSPROT_IK_CHAT_OLD,
    RSPROT_IK_COUNT
} RsprotInfoKind;

/* ------------------------------------------------------------------ */
/* Output: the list the executor reads                                 */
/* ------------------------------------------------------------------ */

/*
 * A reader writes what it decoded as a flat list of ops, in wire order, in the
 * vocabulary the entity-info executor applies -- every stream and every
 * protocol family (the OSRS revisions here, and the client's classic RS2
 * readers) writes this one list, so the executor has one input.
 *
 * Two shapes of movement share it. The OSRS player stream addresses a
 * 2048-slot table: ADD_NEW / SET_LOCAL_PLAYER name the slot, ABS_XZLEVEL its
 * new absolute position, REMOVE_PID its departure. The npc streams and the
 * classic player stream address the list both ends rebuild during the packet:
 * COUNT_RESET, then per old entry ADD_OLD + INFO (and WALKDIR / RUNDIR) or
 * CLEAR_OLD, then ADD_NEW + DELTA_XZ for each newcomer, and SET_EXT selects an
 * extended-info target by its position in that list.
 *
 * Strings and blobs live in the list's byte arena, NUL-terminated and in
 * natural byte order, valid until the list is read into again.
 */

typedef enum RsprotInfoOpKind
{
    RSPROT_OP_NONE = 0,

    /* --- whom the ops after it are about ------------------------------- */
    /** The local player. */
    RSPROT_OP_SET_LOCAL_PLAYER,
    /** `value`: an entity by server slot (a player pid, an npc slot), newly
     *  added or -- in the OSRS player stream -- addressed. */
    RSPROT_OP_ADD_NEW,
    /** `value`: a kept entity, by its position in the previous packet's list. */
    RSPROT_OP_ADD_OLD,
    /** `value`: the extended-info target, BY POSITION in the list rebuilt this
     *  packet -- not by server slot. The consumer's list must have one entry
     *  per entry the reader counted, or this resolves to the wrong entity, and
     *  does so silently. See RS_EntitySync::active_players/active_npcs. */
    RSPROT_OP_SET_EXT,
    /** `value`: an entity dropped, by its position in the previous list. */
    RSPROT_OP_CLEAR_OLD,
    /** `value`: a player dropped by slot (the OSRS slot table). */
    RSPROT_OP_REMOVE_PID,
    /** `value`: how many entries of the previous list this packet walks. */
    RSPROT_OP_COUNT_RESET,
    /** `value`: 1 when the kept entity moved or has extended info. */
    RSPROT_OP_INFO,

    /* --- movement ----------------------------------------------------- */
    RSPROT_OP_WALKDIR,  /* `value`: a 3-bit direction */
    RSPROT_OP_RUNDIR,   /* `value`: a direction (player: 4-bit run; npc: one of two 3-bit) */
    RSPROT_OP_CRAWLDIR, /* `value`: one 3-bit direction at crawl speed (half a walk) */
    RSPROT_OP_NPCTYPE,  /* `value`: the newcomer's npc config id */
    /** `xz_level`: a scene-local position (the classic local player). */
    RSPROT_OP_LOCAL_XZLEVEL,
    /** `xz_level`: an ABSOLUTE position (the OSRS player stream). The reader
     *  cannot convert between the two: it does not know the scene origin. */
    RSPROT_OP_ABS_XZLEVEL,
    /** `delta`: a newcomer's offset from the local player / update origin. */
    RSPROT_OP_DELTA_XZ,
    /** `low_res`: a slot's low-resolution position (the OSRS player stream):
     *  where a slot leaving high resolution now is, or a low-resolution
     *  update. Not for the executor, which keeps no low-resolution players. */
    RSPROT_OP_LOW_RES_POS,

    /* --- extended info -------------------------------------------------- */
    /** `block`: the extended-info block the ops up to the next BLOCK (or the
     *  next target) were read from. Written first in every block, so a block
     *  that writes no op -- an empty hit list -- is still in the list. */
    RSPROT_OP_BLOCK,
    RSPROT_OP_APPEARANCE,
    RSPROT_OP_SEQUENCE,
    RSPROT_OP_FACE_ENTITY,
    RSPROT_OP_SAY,
    RSPROT_OP_DAMAGE,
    RSPROT_OP_DAMAGE2, /* the classic second hitsplat */
    RSPROT_OP_HEADBAR,
    RSPROT_OP_FACE_COORD,
    RSPROT_OP_CHAT,
    RSPROT_OP_SPOTANIM,
    RSPROT_OP_EXACT_MOVE,
    RSPROT_OP_FACE_ANGLE,
    RSPROT_OP_TINT,
    RSPROT_OP_TRANSPARENCY,
    RSPROT_OP_CHANGE_TYPE,
    RSPROT_OP_SPAWN_CYCLE, /* `value` */
    RSPROT_OP_VISIBLE_OPS, /* `value`: the op mask */
    RSPROT_OP_NAME_CHANGE,
    RSPROT_OP_LEVEL_CHANGE, /* `value` */
    RSPROT_OP_BAS_CHANGE,
    RSPROT_OP_HEADICON_CUSTOM,
    RSPROT_OP_BODY_CUSTOM,
    RSPROT_OP_HEAD_CUSTOM,     /* `body`: the chathead's customisation */
    RSPROT_OP_MOVE_SPEED,      /* `value`: the slot's traversal from now (signed) */
    RSPROT_OP_TEMP_MOVE_SPEED, /* `value`: this cycle's traversal (signed) */
    RSPROT_OP_NAME_EXTRAS,
    RSPROT_OP_RESET,  /* `value`: the player's reset block, as read */
    RSPROT_OP_FREEZE, /* `freeze` */

    /* --- not for the executor ----------------------------------------------- */
    /** Last, and only when the read was not complete: why, and where
     *  (`stop`). Everything before it was read whole. */
    RSPROT_OP_STOP,
    /* WORLDENTITY_INFO_V3..V7, which a host converts to its own arm. `who` is
     * the position in the active world's entity list (updates) or the entity
     * index (spawns). */
    RSPROT_OP_WE_COUNT,  /* `value`: the entries the update section walks */
    RSPROT_OP_WE_REMOVE, /* the entity at `who` leaves the list */
    RSPROT_OP_WE_IDLE,   /* `we_move`, no deltas: stays (extended info from V6) */
    RSPROT_OP_WE_MOVE,   /* `we_move` */
    RSPROT_OP_WE_ADD,    /* `we_add`: an entity enters, at the end of the list */
} RsprotInfoOpKind;

/** An entity's traversal (MOVE_SPEED / TEMP_MOVE_SPEED). */
enum
{
    RSPROT_TRAVERSAL_CRAWL = 0,
    RSPROT_TRAVERSAL_WALK = 1,
    RSPROT_TRAVERSAL_RUN = 2,
    RSPROT_TRAVERSAL_SNAP = 127,
};

/** How an OSRS player slot came by its ABS_XZLEVEL: the high-resolution
 *  movement opcode, or ADD for a slot entering high resolution. */
enum
{
    RSPROT_PLAYER_MOVE_ADD = 0,
    RSPROT_PLAYER_MOVE_WALK = 1,
    RSPROT_PLAYER_MOVE_RUN = 2,
    RSPROT_PLAYER_MOVE_TELEPORT = 3,
};

/** The 238+ facing block's target types (FACE_ENTITY `target_type`). */
enum
{
    RSPROT_FACE_TARGET_NONE = 0,
    RSPROT_FACE_TARGET_NPC = 1,
    RSPROT_FACE_TARGET_PLAYER = 2,
    RSPROT_FACE_TARGET_WORLD_ENTITY = 3,
};

/*
 * Every op carries what its block SENT: a field the executor wants converted
 * (65535 as -1, half-tiles, a soak hit's two splats) is converted where the
 * conversion keeps the value -- 65535 is still -1 and nothing else -- and kept
 * as sent beside it where it would not.
 */

typedef struct RsprotInfoXZLevel
{
    int16_t x;
    int16_t z;
    uint8_t level;
    bool jump;
    /** The OSRS stream states the traversal apart from the move: the slot's
     *  persistent speed, or this cycle's override. Classic readers leave it
     *  false. */
    bool has_move_speed;
    int8_t move_speed;
    /** An exact move in the same update cancels the move (class109.method3804
     *  flag 0x4000 clears field1582): the coordinate still becomes route[0] --
     *  the exact move's tiles are relative to it -- but the actor neither snaps
     *  nor steps. */
    bool route_only;
    /** The OSRS player stream: RSPROT_PLAYER_MOVE_*. */
    uint8_t move;
} RsprotInfoXZLevel;

typedef struct RsprotInfoDeltaXZ
{
    int16_t dx;
    int16_t dz;
    bool jump;
} RsprotInfoDeltaXZ;

/** The 2048-slot table's low-resolution position: level and the 8-bit region
 *  coordinates (a region is 64 tiles). */
typedef struct RsprotInfoLowRes
{
    uint16_t slot;
    /** 0: the slot left high resolution; 1 a level change, 2 a step to an
     *  adjacent region, 3 a move to any region -- the low-resolution opcode. */
    uint8_t opcode;
    uint8_t level;
    uint8_t x;
    uint8_t z;
} RsprotInfoLowRes;

typedef struct RsprotInfoBlockMark
{
    uint8_t kind; /* RsprotInfoKind */
    uint32_t bit; /* its flag bit */
} RsprotInfoBlockMark;

typedef struct RsprotInfoAppearance
{
    const uint8_t* appearance; /* the list's bytes, natural order */
    int len;
} RsprotInfoAppearance;

typedef struct RsprotInfoSequence
{
    int sequence_id; /* -1 = clear (65535) */
    uint8_t delay;
} RsprotInfoSequence;

/** `entity_id`: an npc index, or 32768 + a player index; -1 clears. */
typedef struct RsprotInfoFaceEntity
{
    int32_t entity_id;
    uint16_t fallback_angle;
    bool has_fallback_angle; /* false: the 238+ block's reset */
    bool instant;            /* as sent: a reset carries one too */
    uint8_t movement_mode;
    bool modern; /* the 238+ facing block */
    /** As sent. The older block: `target_type` 0 and its 24-bit index
     *  (0x10000 + a player). The 238+ block: RSPROT_FACE_TARGET_* and the
     *  index; a world entity is no entity here (`entity_id` -1): the
     *  executor turns to the fallback angle. */
    uint8_t target_type;
    int32_t target_index;
} RsprotInfoFaceEntity;

typedef struct RsprotInfoSay
{
    const char* text; /* the list's bytes */
} RsprotInfoSay;

/* One hit. A soaked hit is ONE op: the executor draws the soak as a second
 * splat on the same delay. */
typedef struct RsprotInfoDamage
{
    int damage_type; /* -1: a delay with no splat (the OSRS 0x7FFE) */
    int damage;      /* -1 with it */
    bool soaked;     /* the OSRS 0x7FFF form: a soak splat follows */
    int soak_type;   /* -1 when not soaked */
    int soak_damage;
    uint8_t health;
    uint8_t total_health;
    uint16_t delay;
    uint16_t slots; /* the 237+ hitmark block's limit; 0 where none is sent */
} RsprotInfoDamage;

/* Actor.method3504's six wire values, kept apart from a hitsplat: a bar can
 * update with no damage at all. `remove` drops this type. A block of several
 * bars writes one op each, `index` its place in the block. */
typedef struct RsprotInfoHeadbar
{
    int type;
    int duration;
    int start_delay;
    uint8_t start_fill;
    uint8_t end_fill;
    bool remove;
    uint8_t index;
} RsprotInfoHeadbar;

/** Half-tile coordinates; 0, 0 is no target. */
typedef struct RsprotInfoFaceCoord
{
    int16_t x;
    int16_t z;
    bool instant; /* as sent, target or not */
    uint8_t movement_mode;
    bool modern;
    /** As sent: the tile (the older block's 0xFFFF is no target) and the
     *  238+ block's size nibbles. */
    int32_t tile_x;
    int32_t tile_z;
    uint8_t size_x;
    uint8_t size_z;
} RsprotInfoFaceCoord;

typedef struct RsprotInfoChat
{
    uint16_t colour_effect;
    uint8_t type; /* staff-mod level */
    uint8_t length;
    const uint8_t* data; /* the list's bytes: wordpacked, natural order */
    uint8_t autotyper;
    /** A pattern colour (13..20) sends colour - 12 pattern bytes; 223-224's
     *  older block sends none. */
    uint8_t pattern_count;
    uint8_t pattern[8];
} RsprotInfoChat;

typedef struct RsprotInfoSpotAnim
{
    uint8_t slot;
    int32_t spotanim_id;  /* -1 clears (65535) */
    int32_t height_delay; /* height << 16 | cycle delay */
    bool loop;            /* 240+'s loop byte */
    bool has_loop;        /* the block sends one */
    bool single;          /* 223-224's npc block: one, with no count or slot */
} RsprotInfoSpotAnim;

typedef struct RsprotInfoExactMove
{
    int16_t start_x;
    int16_t start_z;
    int16_t end_x;
    int16_t end_z;
    uint16_t start_cycle_delta; /* + the client loop cycle at apply time */
    uint16_t end_cycle_delta;
    uint16_t facing;
    bool facing_is_yaw;
    bool relative; /* deltas from route[0] in the OSRS stream */
} RsprotInfoExactMove;

typedef struct RsprotInfoFaceAngle
{
    uint16_t angle;
    bool instant;
    bool spawn; /* an npc's spawn facing */
    uint8_t movement_mode;
    bool modern;
} RsprotInfoFaceAngle;

/* The cycles relative to the client cycle the block lands on, then hue /
 * saturation / lightness / weight, each as read -- some revisions send them
 * signed and some unsigned. The executor takes the four as SIGNED bytes: -1
 * leaves that channel, a weight not > 0 draws no tint (class149.method5142). */
typedef struct RsprotInfoTint
{
    uint16_t start_cycle_delta;
    uint16_t end_cycle_delta;
    int32_t hue;
    int32_t saturation;
    int32_t lightness;
    int32_t weight;
} RsprotInfoTint;

/* The cycle deltas from arrival and the alphas, as read (signed or not by
 * revision): the executor takes the deltas as signed shorts and the alphas as
 * bytes (0 opaque .. 255 gone). `use_start`: the fade starts from `from`
 * rather than the alpha shown now (non-zero). */
typedef struct RsprotInfoTransparency
{
    int32_t start_cycle_delta;
    int32_t end_cycle_delta;
    int32_t from;
    int32_t to;
    uint8_t use_start;
} RsprotInfoTransparency;

typedef struct RsprotInfoChangeType
{
    int npc_type; /* -1 = back to the spawned type (65535) */
} RsprotInfoChangeType;

typedef struct RsprotInfoNameChange
{
    const char* name; /* the list's bytes */
} RsprotInfoNameChange;

/* One bit per movement seq: 0 turn left, 1 turn right, 2 walk, 3 walk back,
 * 4 walk left, 5 walk right, 6 run, 7 run back, 8 run left, 9 run right,
 * 10 crawl, 11 crawl back, 12 crawl left, 13 crawl right, 14 ready. -1 where
 * the mask has no bit, or the wire says 65535. */
typedef struct RsprotInfoBasChange
{
    uint32_t mask;
    int readyanim;
    int walkanim;
    int turnanim;
    int turnanim_r;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    int runanim_b;
    int runanim_l;
    int runanim_r;
    int crawlanim;
    int crawlanim_b;
    int crawlanim_l;
    int crawlanim_r;
} RsprotInfoBasChange;

/* The eight overhead slots the npc draws instead of its type's. mask 0 = back
 * to the type's icons; an unset slot of a non-zero mask draws nothing (-1). */
typedef struct RsprotInfoHeadiconCustom
{
    uint8_t mask;
    int32_t group[8];
    int32_t index[8];
} RsprotInfoHeadiconCustom;

/*
 * A body (BODY_CUSTOM) or chathead (HEAD_CUSTOM) customisation. `flag` is the
 * block's byte: 0x1 reset (nothing follows), 0x2 models, 0x4 recolours, 0x8
 * retextures, 0x10 mirror, 0x20 a player composition (the counted forms).
 * `reset` is the executor's reading: back to the type's models, which a block
 * without 0x2 also means. The arrays are the list's; absent ones are empty.
 */
typedef struct RsprotInfoBodyCustom
{
    bool reset;
    uint8_t flag;
    int32_t model_count;
    const int32_t* models; /* -1: no model (the older form's 65535) */
    int32_t recolour_count;
    const int32_t* recolours;
    int32_t retexture_count;
    const int32_t* retextures;
    int32_t mirror;            /* the typed forms' byte (0x10); -1 when not sent */
    bool has_recolour_all;     /* the V4 form sent a recolour-all */
    int32_t recolour_all;      /* 65535 still sends the list */
    int32_t body_type;         /* 0x20; -1 when not sent */
    int32_t identkit_count;
    const int32_t* identkits;
} RsprotInfoBodyCustom;

/** The player's name decorations, as sent. */
typedef struct RsprotInfoNameExtras
{
    const char* before_name; /* the list's bytes */
    const char* after_name;
    const char* after_combat_level;
} RsprotInfoNameExtras;

typedef struct RsprotInfoFreeze
{
    int32_t delay;
    int32_t duration;
    int32_t cancel;
} RsprotInfoFreeze;

/** A world entity's extended info (V6+). `flags` is the raw byte. */
typedef struct RsprotInfoWeExt
{
    int32_t flags;
    int32_t has_sequence;
    int32_t sequence_id;
    int32_t sequence_delay;
    int32_t has_ops;
    int32_t ops;
} RsprotInfoWeExt;

/** An update entry: fine-unit and angle deltas off the entity's last state
 *  (each 0 when its 2-bit code is), `teleport` for opcode 3. */
typedef struct RsprotInfoWeMove
{
    int32_t dx;
    int32_t dy;
    int32_t dz;
    int32_t dangle;
    int32_t teleport;
    /** V1-V2 only: the entity's move speed (`dangle` is then its angle, and
     *  `dx` / `dz` whole tiles in fine units). */
    int32_t move_speed;
    RsprotInfoWeExt ext;
} RsprotInfoWeMove;

/** A spawn record. Absent fields: `id` and `priority` -1 (before V5),
 *  `has_level` 0 (V5+), `has_centre` 0 (all but V4). */
typedef struct RsprotInfoWeAdd
{
    int32_t id;
    int32_t size_x; /* tiles */
    int32_t size_z;
    int32_t has_level;
    int32_t level;
    int32_t priority;
    /* Fine units and angle, from the build area's origin. */
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t angle;
    int32_t has_centre;
    int32_t centre_x;
    int32_t centre_z;
    /** V1-V2 only: the record's last g2. */
    int32_t unknown;
    RsprotInfoWeExt ext;
} RsprotInfoWeAdd;

typedef enum RsprotInfoStopReason
{
    RSPROT_STOP_ERROR = 1,    /* the payload ran out, or a bit section was malformed */
    RSPROT_STOP_ILLEGAL_FLAG, /* `flags` has a bit no layout exists for */
    RSPROT_STOP_BLOCK,        /* block `kind` (`block_flag`) did not read to its end */
    RSPROT_STOP_TRAILING,     /* everything read, and bytes are left */
} RsprotInfoStopReason;

typedef struct RsprotInfoStop
{
    int32_t reason; /* RsprotInfoStopReason */
    int32_t rpos;   /* the byte the read had reached */
    int32_t flags;  /* the entity's flag word, for a flag or block stop */
    int32_t kind;   /* RsprotInfoKind, for a block stop */
    int32_t block_flag;
} RsprotInfoStop;

typedef struct RsprotInfoOp
{
    uint8_t kind; /* RsprotInfoOpKind */
    int32_t who;  /* STOP and WE_* only */
    union
    {
        uint64_t value;
        RsprotInfoXZLevel xz_level;
        RsprotInfoDeltaXZ delta_xz;
        RsprotInfoLowRes low_res;
        RsprotInfoBlockMark block;
        RsprotInfoAppearance appearance;
        RsprotInfoSequence sequence;
        RsprotInfoFaceEntity face_entity;
        RsprotInfoSay say;
        RsprotInfoDamage damage;
        RsprotInfoHeadbar headbar;
        RsprotInfoFaceCoord face_coord;
        RsprotInfoChat chat;
        RsprotInfoSpotAnim spotanim;
        RsprotInfoExactMove exact_move;
        RsprotInfoFaceAngle face_angle;
        RsprotInfoTint tint;
        RsprotInfoTransparency transparency;
        RsprotInfoChangeType change_type;
        RsprotInfoNameChange name_change;
        RsprotInfoBasChange bas_change;
        RsprotInfoHeadiconCustom headicon;
        RsprotInfoBodyCustom body;
        RsprotInfoNameExtras name_extras;
        RsprotInfoFreeze freeze;
        RsprotInfoStop stop;
        RsprotInfoWeMove we_move;
        RsprotInfoWeAdd we_add;
    };
} RsprotInfoOp;

/** What a reader writes into: the caller's ops and byte arena. */
typedef struct RsprotInfoList
{
    RsprotInfoOp* ops;
    int cap;
    int count;
    /** Strings and blobs the ops point at: NUL-terminated, natural order. */
    uint8_t* bytes;
    int bytes_cap;
    int bytes_used;
    /** The read did not complete: the last op is a STOP saying why. */
    int stopped;
    /** An op or a byte did not fit; the read carried on so a player table
     *  stayed in step, but the list is incomplete. */
    int overflow;
} RsprotInfoList;

/** Arm `list` over the caller's arrays. A reader empties it before it writes. */
void
rsprot_info_list_init(
    RsprotInfoList* list,
    RsprotInfoOp* ops,
    int cap,
    uint8_t* bytes,
    int bytes_cap);

/** 1 when the last read consumed its payload exactly as its revision says and
 *  everything fitted. */
int
rsprot_info_list_complete(const RsprotInfoList* list);

/* Writing a list -- what every reader does, the client's own included. */

/** Empty `out` for a read. */
void rsprot_info_list_begin(RsprotInfoList* out);

/** The next op, zeroed, with `kind` set and `who` -1. Past `cap` it is a
 *  scratch op nothing reads, so a writer never has to test. */
RsprotInfoOp* rsprot_info_emit(RsprotInfoList* out, RsprotInfoOpKind kind);

/** An op carrying only `value`. */
void rsprot_info_emit_value(RsprotInfoList* out, RsprotInfoOpKind kind, uint64_t value);

/** `len` bytes of string into the arena, NUL-terminated. "" when it does not
 *  fit (the list is marked overflowed). */
const char* rsprot_info_text(RsprotInfoList* out, const char* s, int32_t len);

/** `len` bytes of blob into the arena, in natural order (`order` is the
 *  RsprotInfoPrim DATA* it arrived in). NULL, and overflow, when it does not
 *  fit. */
const uint8_t* rsprot_info_blob(RsprotInfoList* out, const uint8_t* src, int32_t len, int order);

/** `n` zeroed ints in the arena, aligned. NULL for none, and NULL with
 *  overflow when they do not fit. */
int32_t* rsprot_info_ints(RsprotInfoList* out, int32_t n);

/**
 * CUSTOM_TYPED / HEAD_CUSTOM_TYPED carry their recolour and retexture arrays
 * with no count: the length is the npc TYPE's (RSProx reads recoldest /
 * retexdest off the cache). The host answers for the npc at list position
 * `who` (`head` 1 for the head block); returning 0 -- or passing no lookup --
 * ends the tail, since there is no way to step over an array of unknown length.
 */
typedef int (*RsprotNpcTypeLengths)(
    void* ctx,
    int who,
    int head,
    int* recolours,
    int* retextures);

/* ------------------------------------------------------------------ */
/* The appearance blob                                                 */
/* ------------------------------------------------------------------ */

/*
 * PLAYER_INFO's appearance block -- the blob RSPROT_OP_APPEARANCE carries --
 * read whole. Four versions over 223-241, which differ only in a worn slot's
 * obj customisation:
 *
 *   v1 223-225  recolour / retexture: an index byte, then a g2 per index not 15
 *   v2 226-236  v1, then the wear (0x4) and head (0x8) models, g2 each
 *   v3 237-240  v2 with g4 models
 *   v4 241      counted recolour / retexture lists of (g1 index, g2 value), a
 *               colour override (0x10: g1 flag, g2 colour), g4 models
 *
 * Every value is as sent. The strings point into the blob, which holds them
 * NUL-terminated: they live as long as it does.
 */

#define RSPROT_APPEARANCE_SLOTS 12
#define RSPROT_APPEARANCE_REPLACEMENTS_MAX 255

typedef struct RsprotAppearanceReplacement
{
    uint8_t index;
    uint16_t value;
} RsprotAppearanceReplacement;

/** One worn slot's obj customisation. `flag`: 0x1 recolours, 0x2 retextures,
 *  0x4 wear models, 0x8 head models, 0x10 (v4) a colour override. */
typedef struct RsprotAppearanceObj
{
    uint8_t flag;
    /* v1-v3: two indices in a byte (15: none), a value for each index sent;
     * -1 where none was. */
    uint8_t recolour_indices;
    int32_t recolour[2];
    uint8_t retexture_indices;
    int32_t retexture[2];
    /* v4: counted lists. */
    uint8_t recolour_count;
    RsprotAppearanceReplacement recolours[RSPROT_APPEARANCE_REPLACEMENTS_MAX];
    uint8_t retexture_count;
    RsprotAppearanceReplacement retextures[RSPROT_APPEARANCE_REPLACEMENTS_MAX];
    uint8_t override_colour;
    uint16_t colour;
    /* v2+: -1 where not sent. */
    int32_t man_wear;
    int32_t woman_wear;
    int32_t man_head;
    int32_t woman_head;
} RsprotAppearanceObj;

typedef struct RsprotAppearance
{
    int8_t gender;
    int8_t skull_icon;
    int8_t overhead_icon;
    /** Each worn slot's kit, (flag byte << 8) + its second byte; 0 for a flag
     *  of 0, which sends no second byte. A first kit of 65535 is a transformed
     *  player: `npc_id` follows and the rest are not sent (0). */
    uint16_t kits[RSPROT_APPEARANCE_SLOTS];
    int32_t npc_id; /* -1: not transformed */
    /** The interface's kits, the same encoding. */
    uint16_t interface_kits[RSPROT_APPEARANCE_SLOTS];
    uint8_t colours[5];
    uint16_t ready_anim;
    uint16_t turn_anim;
    uint16_t walk_anim;
    uint16_t walk_back_anim;
    uint16_t walk_left_anim;
    uint16_t walk_right_anim;
    uint16_t run_anim;
    const char* name;
    uint8_t combat_level;
    uint16_t skill_level;
    uint8_t hidden;
    /** Bit (12 - slot) per customised slot, 0x8000 a forced refresh; no slot
     *  follows when it is 0 or 0x8000 alone. */
    uint16_t customisation;
    RsprotAppearanceObj objs[RSPROT_APPEARANCE_SLOTS];
    const char* before_name;
    const char* after_name;
    const char* after_combat_level;
    int8_t text_gender;
} RsprotAppearance;

typedef int (*RsprotAppearanceReadFn)(
    const uint8_t* data,
    int32_t len,
    RsprotAppearance* out);

/** Each returns 1, or 0 when the blob is short or a string is unterminated. */
int rsprot_appearance_v1(const uint8_t* data, int32_t len, RsprotAppearance* out);
int rsprot_appearance_v2(const uint8_t* data, int32_t len, RsprotAppearance* out);
int rsprot_appearance_v3(const uint8_t* data, int32_t len, RsprotAppearance* out);
int rsprot_appearance_v4(const uint8_t* data, int32_t len, RsprotAppearance* out);

/** The version `revision` sends; NULL outside 223-241. */
RsprotAppearanceReadFn rsprot_appearance_reader(int revision);

/* ------------------------------------------------------------------ */
/* Readers                                                             */
/* ------------------------------------------------------------------ */

#define RSPROT_INFO_PLAYER_SLOTS 2048

/** The player stream's table. Plain data: a host that decodes several
 *  sessions in one process may copy one in and out between them. */
typedef struct RsprotPlayerInfo
{
    int local_index;
    int seeded;
    uint8_t high_res[RSPROT_INFO_PLAYER_SLOTS];
    uint8_t flags[RSPROT_INFO_PLAYER_SLOTS];
    int32_t coord[RSPROT_INFO_PLAYER_SLOTS];
    int32_t low_res_pos[RSPROT_INFO_PLAYER_SLOTS];
    /** Each slot's persistent traversal (the MOVE_SPEED block's; walk until
     *  one arrives): class95.field1344 in the reference. */
    int8_t move_speed[RSPROT_INFO_PLAYER_SLOTS];
} RsprotPlayerInfo;

/** A fresh table, owned by `local_index`. The table is the same at every
 *  revision; only the reader that walks it is per version. */
void
rsprot_player_info_reset(
    RsprotPlayerInfo* pi,
    int local_index);

/** Seed the table from the GPI init block (the front of the login REBUILD, or
 *  RECONNECT_OK's body). */
void
rsprot_player_info_init(
    RsprotPlayerInfo* pi,
    const uint8_t* data,
    int32_t len);


/** Parse a GPI init block: the local player's absolute coordinate and every
 *  other slot's 18-bit rough position (0 for slot 0 and the local slot).
 *  Returns 1, or 0 when the block is short. */
int
rsprot_player_info_gpi(
    int local_index,
    const uint8_t* data,
    int32_t len,
    int32_t* coord,
    int32_t positions[RSPROT_INFO_PLAYER_SLOTS]);

/** Seed the table from a parsed GPI init block (rsprot_player_info_gpi). */
void
rsprot_player_info_seed(
    RsprotPlayerInfo* pi,
    int32_t coord,
    const int32_t positions[RSPROT_INFO_PLAYER_SLOTS]);

/* The entry points' shapes, for a host that picks one at run time. Each
 * returns the op count it wrote into `out`. */
typedef int (*RsprotPlayerInfoReadFn)(
    RsprotPlayerInfo* pi,
    const uint8_t* data,
    int32_t len,
    RsprotInfoList* out);
typedef int (*RsprotNpcInfoReadFn)(
    const uint8_t* data,
    int32_t len,
    RsprotNpcTypeLengths lengths,
    void* ctx,
    RsprotInfoList* out);
typedef int (*RsprotWorldEntityInfoReadFn)(
    const uint8_t* data,
    int32_t len,
    RsprotInfoList* out);

/*
 * The entry points: <prot>_osrs<N>, one per prot per revision, and a lookup by
 * revision for each prot. When the read did not complete the ops before the
 * stop are written, a STOP op says why, and a player table is still advanced,
 * as the reference's is. The npc and world entity readers are stateless; the
 * npc ones take `lengths` / `ctx` to answer CUSTOM_TYPED (NULL ends the tail
 * there).
 */
#include "npc_info.h"
#include "player_info.h"
#include "worldentity_info.h"
#include "worldentity_info_v1.h"

#endif
