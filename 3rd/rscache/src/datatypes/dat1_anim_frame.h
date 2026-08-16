#ifndef RSCACHE_DATATYPES_DAT1_ANIM_FRAME_H
#define RSCACHE_DATATYPES_DAT1_ANIM_FRAME_H

#include <stdbool.h>
#include <stdint.h>

struct RSCache_Dat1AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** labels;
    uint16_t* label_counts;
};

struct RSCache_Dat1AnimFrame
{
    int id;
    struct RSCache_Dat1AnimBase* base;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;

    /*
     * Provenance for byte-exact encode (mirrors dat2_frame).
     *
     * Decode transforms the flag stream: it skips zero flags, invents type-0
     * "bridge" entries, and fills absent XYZ components with type defaults.
     * Recording the flag stream and which output entries were synthesized lets
     * encode reproduce the original wire bytes.
     */
    /** Number of bone flag bytes (= group_count from the head). */
    int flag_count;
    /** The flag stream verbatim, `flag_count` bytes. */
    uint8_t* bone_flags;
    /** Per output entry: was it inserted by the decoder rather than read? */
    bool* synthesized;
};

struct RSCache_Dat1AnimBaseFrames
{
    struct RSCache_Dat1AnimBase* base;

    struct RSCache_Dat1AnimFrame* frames;
    int frame_count;
};

/**
 * data := archive from the anim table
 */
struct RSCache_Dat1AnimBaseFrames*
RSCache_Dat1AnimBaseFramesNewDecode(
    char* data,
    int data_size);

struct RSCache_Dat1AnimBase*
RSCache_Dat1AnimBaseNewDecode(
    char* data,
    int data_size);

void
RSCache_Dat1AnimFrameFree(struct RSCache_Dat1AnimFrame* animframe);

void
RSCache_Dat1AnimFrameFreeInplace(struct RSCache_Dat1AnimFrame* frame);

void
RSCache_Dat1AnimBaseFramesFree(struct RSCache_Dat1AnimBaseFrames* abf);

uint32_t
RSCache_Dat1AnimBaseEncodeBound(const struct RSCache_Dat1AnimBase* base);

uint32_t
RSCache_Dat1AnimBaseEncode(
    const struct RSCache_Dat1AnimBase* base,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat1AnimBaseFramesEncodeBound(const struct RSCache_Dat1AnimBaseFrames* abf);

/**
 * Encode a dat1 anim archive:
 *   [u16 total][head: id u16 + group_count u8 per frame]
 *   [tran1 flags][tran2 shortsmarts][del u8 per frame][AnimBase]
 *   [trailer 4×u16 lengths]
 *
 * Trailer head_length excludes the leading u16 total (+2 rule on decode).
 * Without provenance (`bone_flags`/`synthesized`), encode still produces a
 * valid archive that decodes semantically, but may not be byte-exact.
 */
uint32_t
RSCache_Dat1AnimBaseFramesEncode(
    const struct RSCache_Dat1AnimBaseFrames* abf,
    uint8_t* out,
    uint32_t out_capacity);

#endif // RSCACHE_DATATYPES_DAT1_ANIM_FRAME_H
