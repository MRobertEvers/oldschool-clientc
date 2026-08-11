#ifndef TORIRS_OBTORI_H
#define TORIRS_OBTORI_H

/*
 * OB_TORI — a NON-STOCK model container for the imported RS2012 lane.
 *
 * This is not a RuneScape format. No Jagex client reads it, no packed cache
 * contains it, and nothing in the stock content path will ever produce one. It
 * exists because the two things the HD material kernels need are per-FACE and
 * OB3 has nowhere to put them:
 *
 *   1. Which kernel a face wants.
 *      Kernel selection today is a property of the TEXTURE (the record's
 *      alpha/modulate/detail flags), so every face naming a material gets the
 *      same treatment. That is wrong for this lane and measurably so: of the 39
 *      mask materials, 16 read as cutout cards whose empty region must show the
 *      scene behind, 10 as decals lying on a surface that must stay opaque, and
 *      13 are ambiguous from the texture alone (docs/HD_KERNELS.md). Which one a
 *      given face is depends on the GEOMETRY it belongs to, not on the image, so
 *      no per-material flag can express it.
 *
 *   2. A second texture layer, and a per-face strength for it.
 *      OB3 carries exactly one `face_textures[f]` and one
 *      `face_texture_coords[f]`. A surface with a base map and a detail map on
 *      top has no encoding at all.
 *
 * The container is deliberately a wrapper rather than an extension: an OB3 file
 * ends in a trailer that is read backwards from EOF, so appending to one
 * silently breaks every existing reader. OB_TORI puts its magic FIRST, carries
 * the untouched OB3 bytes as a payload, and appends self-describing sections
 * after them. A stock decoder handed one of these fails immediately on the
 * magic instead of mis-reading it, which is the intended behaviour.
 *
 *   offset  size  field
 *        0     8  magic "OB_TORI\0"
 *        8     2  version (OBTORI_VERSION)
 *       10     2  section count
 *       12     4  size of the embedded OB3 payload
 *       16     n  the OB3 bytes, verbatim
 *     16+n        sections, each: u16 kind, u16 flags, u32 size, payload
 *
 * All integers little-endian. Unknown section kinds are skipped by size, so a
 * reader older than a writer degrades to plain OB3 behaviour rather than
 * failing.
 */

#include <stdbool.h>
#include <stdint.h>

#define OBTORI_MAGIC "OB_TORI"
#define OBTORI_MAGIC_SIZE 8
#define OBTORI_VERSION 1
#define OBTORI_HEADER_SIZE 16

/** Which span kernel a face is routed to. See docs/HD_KERNELS.md §1. */
enum ObToriFaceKernel
{
    /** Defer to the texture record's own flags — the stock behaviour. */
    OBTORI_KERNEL_DEFAULT = 0,
    /** Force the colour-key path even if the texture carries alpha. */
    OBTORI_KERNEL_TRANSPARENT = 1,
    /** Per-texel coverage, composited over whatever is behind. A cutout card. */
    OBTORI_KERNEL_ALPHA = 2,
    /** Coverage plus a tint by the face's own chroma. A coloured mask. */
    OBTORI_KERNEL_MODULATE = 3,
    /** Opaque; the texel darkens the colour the face would have had. A decal or
     *  an HD program that is not a surface map at all. */
    OBTORI_KERNEL_DETAIL = 4,
    OBTORI_KERNEL_COUNT
};

enum ObToriSectionKind
{
    /** u8 per face: an ObToriFaceKernel. */
    OBTORI_SECTION_FACE_KERNEL = 1,
    /** u8 per face: 0..255 scaling of the detail kernel's effect, 255 = full. */
    OBTORI_SECTION_FACE_DETAIL_STRENGTH = 2,
    /** int16 per face: a SECOND texture sampled on top of the first, -1 = none.
     *  Defined so the layered case has an encoding; only written when the source
     *  actually carries two layers for a face. */
    OBTORI_SECTION_FACE_DETAIL_TEXTURE = 3
};

/** Decoded sections. Any pointer may be NULL when the file omits that section. */
struct ObToriModel
{
    uint8_t* ob3;
    int ob3_size;

    int face_count;
    uint8_t* face_kernel;
    uint8_t* face_detail_strength;
    int16_t* face_detail_texture;
};

/** True when `data` begins with the OB_TORI magic. Cheap enough to call on
 *  anything before deciding which decoder to use. */
bool
ObTori_IsObTori(const void* data, int size);

/** Decode, or NULL if the magic, version or a section size does not hold up.
 *  `face_count` is what the caller expects from the OB3 payload; a section
 *  whose length disagrees with it is a corrupt file, not a shorter model. */
struct ObToriModel*
ObTori_NewDecode(const void* data, int size, int face_count);

void
ObTori_Free(struct ObToriModel* model);

/** Upper bound for an encode with the given sections present. */
int
ObTori_EncodeBound(int ob3_size, int face_count, int section_count);

/**
 * Encode. Pass NULL for any per-face array to omit its section.
 * Returns bytes written, or 0 if the buffer is too small.
 */
int
ObTori_Encode(
    const void* ob3,
    int ob3_size,
    int face_count,
    const uint8_t* face_kernel,
    const uint8_t* face_detail_strength,
    const int16_t* face_detail_texture,
    void* out,
    int out_capacity);

/** Human-readable kernel name, for tool output and debug logs. */
const char*
ObTori_KernelName(int kernel);

#endif
