#ifndef RSCACHE_DATATYPES_DAT1_CONFIG_COMPONENT_H
#define RSCACHE_DATATYPES_DAT1_CONFIG_COMPONENT_H

#include <stdbool.h>

// Interface widgets from the dat1 INTERFACES jagfile (single file "data").
// See Client-TS Component.unpack / v0 dat1a_config_component.

enum RSCache_Dat1ComponentType
{
    RSCACHE_DAT1_COMPONENT_TYPE_LAYER = 0,
    RSCACHE_DAT1_COMPONENT_TYPE_UNUSED = 1,
    RSCACHE_DAT1_COMPONENT_TYPE_INV = 2,
    RSCACHE_DAT1_COMPONENT_TYPE_RECT = 3,
    RSCACHE_DAT1_COMPONENT_TYPE_TEXT = 4,
    RSCACHE_DAT1_COMPONENT_TYPE_GRAPHIC = 5,
    RSCACHE_DAT1_COMPONENT_TYPE_MODEL = 6,
    RSCACHE_DAT1_COMPONENT_TYPE_INV_TEXT = 7,
    RSCACHE_DAT1_COMPONENT_TYPE_LINE = 9,
};

enum RSCache_Dat1ComponentButtonType
{
    RSCACHE_DAT1_COMPONENT_BUTTON_TYPE_OK = 1,
    RSCACHE_DAT1_COMPONENT_BUTTON_TYPE_TARGET = 2,
    RSCACHE_DAT1_COMPONENT_BUTTON_TYPE_CLOSE = 3,
    RSCACHE_DAT1_COMPONENT_BUTTON_TYPE_TOGGLE = 4,
    RSCACHE_DAT1_COMPONENT_BUTTON_TYPE_SELECT = 5,
    RSCACHE_DAT1_COMPONENT_BUTTON_TYPE_CONTINUE = 6,
};

struct RSCache_Dat1ConfigComponent
{
    int id;
    int layer;
    int type;
    int buttonType;
    int clientCode;
    int width;
    int height;
    int x;
    int y;
    int** scripts;
    int scripts_count;
    int* scripts_lengths;
    int alpha;
    int overlayer;
    int* scriptComparator;
    int* scriptOperand;
    /** Sized independently of scripts_count in the cache format. */
    int comparator_count;
    int* invSlotObjId;
    int* invSlotObjCount;
    int seqFrame;
    int seqCycle;
    int* children;
    int children_count;
    int activeModelType;
    int activeModel;
    int anim;
    int activeAnim;
    int zoom;
    int xan;
    int yan;
    int scroll;
    bool hide;
    char* targetVerb;
    char* targetText;
    int targetMask;
    char* option;
    int marginX;
    int marginY;
    int colour;
    int activeColour;
    int overColour;
    int activeOverColour;
    int modelType;
    int model;
    char* text;
    char* activeText;
    bool draggable;
    bool interactable;
    bool usable;
    bool swappable;
    bool fill;
    bool center;
    bool shadowed;
    int* invSlotOffsetX;
    int* invSlotOffsetY;
    // Sprite name refs into the media jagfile (e.g. "miscgraphics,2").
    char* graphic;
    char* activeGraphic;
    char** invSlotGraphic;
    char** iop;
    int* childX;
    int* childY;
    // Title-font slot 0-3 (p11/p12/b12/q8), not an archive id.
    int font;
};

// components[] is indexed by component id; entries may be NULL (filtered or gaps).
struct RSCache_Dat1ConfigComponentList
{
    struct RSCache_Dat1ConfigComponent** components;
    int components_count;
};

struct RSCache_Dat1ConfigComponentList*
RSCache_Dat1ConfigComponentListNewDecode(
    void* data,
    int size);

/**
 * Decode only components whose id is flagged in needed[]; LAYER children of kept
 * components are flagged as encountered (ids referenced before their definition in
 * the stream would be missed — pass a pre-seeded needed[] for full subtrees).
 */
struct RSCache_Dat1ConfigComponentList*
RSCache_Dat1ConfigComponentListNewDecodeFiltered(
    void* data,
    int size,
    bool* needed,
    int needed_count);

int
RSCache_Dat1ConfigComponentListPeekCount(
    void* data,
    int size);

/**
 * Bring a zeroed allocation to this type's defaults.
 *
 * Eight fields are -1, and every one of them has a real zero: component 0 and
 * layer 0 exist, `type` 0 is LAYER, `buttonType` 0 is "not a button" only by
 * accident of it never being written, `anim`/`activeAnim` 0 is sequence 0, and
 * `targetMask` 0 is a valid mask. Zero-initialising would make every component
 * claim to be layer 0's child.
 */
void
RSCache_Dat1ConfigComponentInit(struct RSCache_Dat1ConfigComponent* component);

/**
 * Decode one component — its id preamble and body — reporting bytes consumed.
 *
 * **Not an opcode stream.** A component is a fixed header followed by sections
 * chosen by `type` and `buttonType`, so there is no per-opcode split to make and
 * no terminator: the record ends where its last conditional section ends. That
 * is why this type takes the registry's whole-record `decode` override rather
 * than `decode_op`.
 *
 * One consequence of decoding a record in isolation: the `65535` preamble that
 * sets the *layer* is sticky across a stream, so a record decoded on its own
 * reports the layer only if it carries the preamble itself. Walk the archive
 * with `RSCache_Dat1ConfigComponentListNewDecode` when the layer matters.
 */
int
RSCache_Dat1ConfigComponentDecodeInplace(
    struct RSCache_Dat1ConfigComponent* component,
    const void* data,
    int size);

/** Release what the record owns, leaving the struct itself to the caller. */
void
RSCache_Dat1ConfigComponentFreeInplace(struct RSCache_Dat1ConfigComponent* component);

void
RSCache_Dat1ConfigComponentFree(struct RSCache_Dat1ConfigComponent* component);

void
RSCache_Dat1ConfigComponentListFree(struct RSCache_Dat1ConfigComponentList* list);

#endif
