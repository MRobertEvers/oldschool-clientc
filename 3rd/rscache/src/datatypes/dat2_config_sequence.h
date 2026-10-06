#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_SEQUENCE_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_SEQUENCE_H

#include "../filelist.h"
#include "../rscache_presence.h"
#include "../rscache_profile.h"

#include <stdbool.h>

struct RSCache_Dat2ConfigFrameSound
{
    int id;
    int loops;
    int location;
    int retain;
    int weight; // Only used in rev226+
};

struct RSCache_Dat2ConfigFrameSoundMap
{
    int* frames;                          // Frame indices
    struct RSCache_Dat2ConfigFrameSound* sounds; // Sound data
    int count;
    int capacity;
};

/**
 * The fields a sequence stream can state, for `RSCache_Dat2ConfigSequence.present`.
 *
 * Keyed by meaning, not opcode: the era decides the number (frame sounds are 13 or
 * 15 before rev 226 and 14 after; the maya id is 14, then 13), and every era's
 * decoder maps its opcode onto the one field.
 */
enum RSCache_Dat2ConfigSequenceField
{
    RSCACHE_SEQ_FIELD_FRAMES = 0,            /* 1: frame ids + lengths */
    RSCACHE_SEQ_FIELD_FRAME_STEP,            /* 2 */
    RSCACHE_SEQ_FIELD_INTERLEAVE,            /* 3 */
    RSCACHE_SEQ_FIELD_STRETCHES,             /* 4 */
    RSCACHE_SEQ_FIELD_FORCED_PRIORITY,       /* 5 */
    RSCACHE_SEQ_FIELD_LEFT_HAND,             /* 6 */
    RSCACHE_SEQ_FIELD_RIGHT_HAND,            /* 7 */
    RSCACHE_SEQ_FIELD_MAX_LOOPS,             /* 8 */
    RSCACHE_SEQ_FIELD_PRECEDENCE,            /* 9 */
    RSCACHE_SEQ_FIELD_PRIORITY,              /* 10 */
    RSCACHE_SEQ_FIELD_REPLY_MODE,            /* 11 */
    RSCACHE_SEQ_FIELD_CHAT_FRAMES,           /* 12 */
    RSCACHE_SEQ_FIELD_FRAME_SOUNDS,          /* v1/v2 13 or 15, v3 14, rs2 13 */
    RSCACHE_SEQ_FIELD_MAYA_ID,               /* v1/v2 14, v3 13 */
    RSCACHE_SEQ_FIELD_MAYA_RANGE,            /* v1/v2 16, v3 15 */
    RSCACHE_SEQ_FIELD_VERTICAL_OFFSET,       /* v3 16 */
    RSCACHE_SEQ_FIELD_MAYA_MASKS,            /* 17 */
    RSCACHE_SEQ_FIELD_DEBUG_NAME,            /* 18 */
    RSCACHE_SEQ_FIELD_CROSS_WORLD_SOUNDS,    /* v3 19 */
    RSCACHE_SEQ_FIELD_RS2_530_SOUND_FLAG,    /* rs2 14 */
    RSCACHE_SEQ_FIELD_RS2_727_TWEENED,       /* rs2-727 15 */
    RSCACHE_SEQ_FIELD_RS2_727_UNKNOWN16,     /* rs2-727 16 */
    RSCACHE_SEQ_FIELD_RS2_727_VORBIS_SOUNDS, /* rs2-727 18 */
    RSCACHE_SEQ_FIELD_COUNT
};

struct RSCache_Dat2ConfigSequence
{
    /** Which `RSCache_Dat2ConfigSequenceField`s the stream stated. The encoder
     *  writes exactly these; the values below still hold the client defaults
     *  for the rest. */
    struct RSCache_Presence present;
    int id;
    int* frame_ids;
    int* frame_lengths;
    int frame_count;
    int frame_step;
    bool stretches;
    int forced_priority;
    int left_hand_item;
    int right_hand_item;
    int max_loops;
    int precedence_animating;
    int priority;
    int reply_mode;
    int anim_maya_id;
    int anim_maya_start;
    int anim_maya_end;
    int vertical_offset; /* rev226+ opcode 16 */
    int* interleave_leave;
    int* chat_frame_ids;
    /** Entries in `chat_frame_ids`. The array used to be allocated without
     *  recording its length, which left it unencodable and uncomparable. */
    int chat_frame_id_count;
    bool* anim_maya_masks;
    char* debug_name;
    /** V3 opcode 19 — sounds audible across worlds. */
    bool sounds_cross_world_view;
    /** RS2 rev-530 opcode 14. Its client meaning is unknown, but the bare flag
     *  is retained so a decoded record can be represented without pretending
     *  it is the later opcode-14 Maya/sound structure. */
    bool rs2_530_sound_flag;
    /** Late pre-EoC RS2 additions. They are consumed separately because these
     * opcode numbers mean unrelated Maya/sound fields on OldSchool. */
    bool rs2_727_tweened;
    bool rs2_727_unknown16;
    /** RS2 rev-727 opcode 18. The reference AnimationDefinitions.aBool5928
     *  selects MIDIInstrument/index 14 instead of SoundEffect/index 4 for
     *  every frame sound in this sequence. */
    bool rs2_727_vorbis_sounds;
    struct RSCache_Dat2ConfigFrameSoundMap frame_sounds; // Map of frame index to sound data

    /** Bytes consumed by the last decode, set on reaching the terminating opcode 0.
     *  Zero if the decode bailed early. Same diagnostic loc, npc and spotanim
     *  carry. */
    int _consumed;
};

/*
 * Codec versions.
 *
 * Sequences are the clearest case of a difference too large for a flag: the
 * frame-sound record changes shape outright between eras, so each gets its own
 * codec rather than a branch inside one.
 *
 *  v1  pre-220  — frame sound is a single g3 packing id/loops/location.
 *  v2  220..226 — id/loops/location split into separate fields.
 *  v3  226+     — adds a per-sound weight byte, plus opcode 16 vertical offset.
 *
 * The reference client (RuneLite's SequenceLoader.configureForRevision) gates
 * these on the sequence group's own JS5 archive revision, which is why the
 * thresholds below are archive revisions rather than game revisions.
 */
#define RSCACHE_CODEC_SEQUENCE_V1 1
#define RSCACHE_CODEC_SEQUENCE_V2 2
#define RSCACHE_CODEC_SEQUENCE_V3 3
/** RS2 rev 530: opcode 13 is a u16-count nested sound table and opcode 14 is
 *  a payload-free flag. This predates, but is not wire-compatible with, V1. */
#define RSCACHE_CODEC_SEQUENCE_RS2_530 4
/** Late pre-EoC RS2 retains 530's nested opcode-13 sound table and adds the
 * payload-free 15/16/18 flags plus opcodes 19/20 sound modifiers and params. */
#define RSCACHE_CODEC_SEQUENCE_RS2_727 5

/** Archive revision at which the frame-sound record split (game rev 220). */
#define RSCACHE_SEQUENCE_ARCHIVE_REV_220 1141
/** Archive revision at which the weight byte appeared (game rev 226). */
#define RSCACHE_SEQUENCE_ARCHIVE_REV_226 1268

/** RuneLite SequenceDefinition field defaults (applied before opcodes). */
void
RSCache_Dat2ConfigSequenceSetDefaults(struct RSCache_Dat2ConfigSequence* sequence);

/** Which sequence codec this cache needs. */
int
RSCache_Dat2ConfigSequenceCodecVersion(const struct RSCache* cache);

/**
 * Encode a sequence record with the codec the profile selects.
 *
 * The codec version is not optional here: the opcode *numbers* move between eras,
 * not just the record shapes. Per RuneLite's SequenceLoader, which this mirrors:
 *
 *   opcode   v1 / v2                   v3 (rev226+)
 *   13       frame sounds              anim_maya_id
 *   14       anim_maya_id              frame sounds (explicit frame per entry)
 *   15       frame sounds (framed)     maya start/end
 *   16       maya start/end            vertical offset
 *   100      -                         per-frame blend table
 *
 * Encoding with the wrong version therefore produces a record whose fields land in
 * entirely different slots, not merely a mis-sized one.
 *
 * Fields that cannot be reproduced:
 *   - v1 frame sounds pack id/loops/location into 24 bits, leaving no room for
 *     `retain` or `weight`;
 *   - v1 and v2 index frame sounds by *position*, so the list is written dense with
 *     zero-filled holes, which the decoder's own `id >= 1` filter drops again;
 *   - opcode 100's blend table, which the decoder consumes without storing.
 */
uint32_t
RSCache_Dat2ConfigSequenceEncode(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigSequence* def,
    uint8_t* out,
    uint32_t out_capacity);

/** As RSCache_Dat2ConfigSequenceEncode, for callers that already know the codec
 *  version (RSCACHE_CODEC_SEQUENCE_V1..V3 or RS2_530). */
uint32_t
RSCache_Dat2ConfigSequenceEncodeCodec(
    const struct RSCache_Dat2ConfigSequence* def,
    int codec_version,
    uint8_t* out,
    uint32_t out_capacity);

/** An upper bound on what `RSCache_Dat2ConfigSequenceEncode` will write. */
uint32_t
RSCache_Dat2ConfigSequenceEncodeBound(const struct RSCache_Dat2ConfigSequence* def);

/** Decode using the codec version the profile selects. */
void
RSCache_Dat2ConfigSequenceDecodeProfile(
    struct RSCache_Dat2ConfigSequence* sequence,
    const struct RSCache* cache,
    char* data,
    int buffer_size);

struct RSCache_Dat2ConfigSequence*
RSCache_Dat2ConfigSequenceNewDecodeProfile(
    const struct RSCache* cache,
    char* data,
    int data_size);

struct RSCache_Dat1ConfigSequence
{
    // frameCount: number = 0;
    // frames: Int16Array | null = null;
    // iframes: Int16Array | null = null;
    // delay: Int16Array | null = null;
    // loops: number = -1;
    // walkmerge: Int32Array | null = null;
    // stretches: boolean = false;
    // priority: number = 5;
    // replaceheldleft: number = -1;
    // replaceheldright: number = -1;
    // maxloops: number = 99;
    // preanim_move: number = -1;
    // postanim_move: number = -1;
    // duplicatebehavior: number = -1;

    int frame_count;
    int* frames;
    int* iframes;
    int* delay;
    int loops;
    int* walkmerge;
    bool stretches;
    int priority;
    int replaceheldleft;
    int replaceheldright;
    int maxloops;
    int preanim_move;
    int postanim_move;
    int duplicate_behavior;
};

struct RSCache_Dat1ConfigSequence*
RSCache_Dat1ConfigSequenceNewDecode(
    char* buffer,
    int buffer_size);

int
RSCache_Dat1ConfigSequenceDecodeInplace(
    struct RSCache_Dat1ConfigSequence* sequence,
    char* buffer,
    int buffer_size);

void
RSCache_Dat1ConfigSequenceFree(struct RSCache_Dat1ConfigSequence* seq);

void
RSCache_Dat2ConfigSequenceFree(struct RSCache_Dat2ConfigSequence* sequence);
void
RSCache_Dat2ConfigSequenceFreeInplace(struct RSCache_Dat2ConfigSequence* sequence);

#endif // RSCACHE_DATATYPES_DAT2_CONFIG_SEQUENCE_H
