#ifndef RSCACHE_DATATYPES_DAT2_COMPONENT_H
#define RSCACHE_DATATYPES_DAT2_COMPONENT_H

#include "../dat2disk.h"
#include "../filelist.h"
#include "../rscache_profile.h"

#include <stdbool.h>
#include <stdint.h>

struct RSCache_Buffer;

enum RSCache_Dat2ComponentScriptVarType
{
    RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT = 0,
    RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_STRING = 1
};

struct RSCache_Dat2ComponentScriptVar
{
    enum RSCache_Dat2ComponentScriptVarType type;
    union
    {
        int32_t i;
        char* s;
    } value;
};

struct RSCache_Dat2ServerActiveProperties
{
    int32_t events;
    int32_t targetMask;
};

/**
 * IF1 / IF3 interface widget. Decode layout follows InterfaceLoader.java (RuneLite cache).
 */
struct RSCache_Dat2Component
{
    int32_t id;
    int32_t type;
    int32_t layer;

    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t baseX;
    int32_t baseY;
    int32_t baseWidth;
    int32_t baseHeight;
    int8_t xMode;
    int8_t yMode;
    int8_t widthMode;
    int8_t heightMode;

    bool hidden;
    int32_t transparency;
    int32_t color;
    bool fill;
    bool alpha;
    bool tiled;
    int32_t outline;
    int32_t graphic;
    int32_t graphicShadow;
    bool horizontalFlip;
    bool verticalFlip;
    int32_t angle;

    char* text;
    char* pauseText;
    int32_t textFont;
    int32_t textHorizontalAlignment;
    int32_t textVerticalAlignment;
    int32_t textLineHeight;
    bool textShadow;

    /** MODEL widget only. Selects model archive + meaning of modelId:
     *  0=none, 1=widget obj, 2=NPC head, 3=player head, 4=item, 5=local player. */
    int32_t modelType;
    int32_t modelId;
    int32_t modelZoom;
    int32_t modelXAngle;
    int32_t modelYAngle;
    int32_t modelZAngle;
    int32_t modelXOffset;
    int32_t modelYOffset;
    int32_t modelSeqId;
    /** IF3 type-6: first byte after sequence id.
     *  Rev ~233: selects orthographic draw (method5172) vs perspective (method5228).
     *  RuneLite cache tools label this "orthogonal"; do not confuse with aBoolean411 below. */
    bool modelOrthographic;

    int32_t objId;
    int32_t objCount;
    bool showObjCount;
    int32_t* objTypes;
    int32_t* objCounts;
    char** objOps;

    int32_t scrollWidth;
    int32_t scrollHeight;
    int32_t scrollX;
    int32_t scrollY;
    bool noClickThrough;

    /** IF1/IF3 packed click/target bits (InterfaceDefinition.clickMask). */
    int32_t clickMask;
    /** IF3 only; component name after type blocks. */
    char* name;
    /** IF3 type 5: texture id (unsigned short in stream). */
    int32_t textureId;

    char* opBase;
    char* targetVerb;

    int32_t lineWidth;
    bool lineDirection;

    int32_t buttonType;
    // AKA ContentType
    // The client will assign special functionality to these.
    // 1337 = CONTENT_WORLD
    // 1338 = CONTENT_MINIMAP
    // 1339 = CONTENT_COMPASS
    // 1400 = CONTENT_WORLDMAP
    int32_t clientCode;
    int32_t linkedComponentId;
    char* option;
    char* activeText;
    char* targetText;
    int32_t activeGraphic;
    int32_t activeColour;
    int32_t overColour;
    int32_t activeOverColour;
    int32_t activeModelId;
    int32_t activeAnimId;
    int32_t marginX;
    int32_t marginY;
    int32_t* invSlotOffsetX;
    int32_t* invSlotOffsetY;
    int32_t* invSlotGraphicId;

    /*
     * Provenance, so an IF1 type-2 widget can be written back.
     *
     * Each of the 20 slots is introduced by a flag byte, and the decoder marks an
     * absent slot by leaving its graphic id at -1. That is not injective: a slot
     * whose flag was set and whose graphic id is genuinely -1 looks identical to
     * one that was never there, and re-encoding drops its eight bytes. Recording
     * the flag is the same fix EXCEPTIONS.md B8 applies to frames and terrain —
     * keep what the wire said rather than trying to infer it back.
     */
    uint8_t invSlotPresent[20];
    /*
     * Set when `option` was empty on the wire and the decoder substituted the
     * button type's default ("Ok" / "Select" / "Continue").
     *
     * Without it an absent option and one that literally says "Continue" are the
     * same state, and every guess is wrong for somebody: interface 99 file 30
     * carries the word in full in all three OldSchool caches here, while most
     * widgets leave it empty.
     */
    bool optionFromDefault;

    int32_t anInt5907;
    int32_t anInt5921;
    int16_t aShort50;
    int16_t aShort49;
    /** IF3 type-6: byte after aShort49.
     *  Deobfuscator Widget.useFixedZoom / drawModel2DAtZoom vs drawModel2D when IF3.
     *  Engine: model_fixed_zoom → zoom3d = widget zoom (true) or 512 (false). */
    bool aBoolean411;
    int32_t anInt5957;
    /** IF3 aspect ratio numerator/denominator (Kronos field2688/field2662). */
    int32_t aspect_ratio_w;
    int32_t aspect_ratio_h;
    int32_t anInt5920;
    int32_t anInt5930;
    int32_t anInt5890;
    int32_t anInt5909;

    int8_t if3SlotPackedA[10];
    int8_t if3SlotPackedB[10];
    int32_t if3SlotCursorOrKey[10];

    uint8_t dragDeadZone;
    uint8_t dragDeadTime;
    bool dragRender;

    struct RSCache_Dat2ComponentScriptVar* onLoad;
    int32_t onLoadLen;
    struct RSCache_Dat2ComponentScriptVar* onMouseOver;
    int32_t onMouseOverLen;
    struct RSCache_Dat2ComponentScriptVar* onMouseLeave;
    int32_t onMouseLeaveLen;
    struct RSCache_Dat2ComponentScriptVar* onTargetLeave;
    int32_t onTargetLeaveLen;
    struct RSCache_Dat2ComponentScriptVar* onTargetEnter;
    int32_t onTargetEnterLen;
    struct RSCache_Dat2ComponentScriptVar* onVarpTransmit;
    int32_t onVarpTransmitLen;
    struct RSCache_Dat2ComponentScriptVar* onInvTransmit;
    int32_t onInvTransmitLen;
    struct RSCache_Dat2ComponentScriptVar* onStatTransmit;
    int32_t onStatTransmitLen;
    struct RSCache_Dat2ComponentScriptVar* onTimer;
    int32_t onTimerLen;
    struct RSCache_Dat2ComponentScriptVar* onOp;
    int32_t onOpLen;
    struct RSCache_Dat2ComponentScriptVar* onMouseRepeat;
    int32_t onMouseRepeatLen;
    struct RSCache_Dat2ComponentScriptVar* onClick;
    int32_t onClickLen;
    struct RSCache_Dat2ComponentScriptVar* onClickRepeat;
    int32_t onClickRepeatLen;
    struct RSCache_Dat2ComponentScriptVar* onRelease;
    int32_t onReleaseLen;
    struct RSCache_Dat2ComponentScriptVar* onHold;
    int32_t onHoldLen;
    struct RSCache_Dat2ComponentScriptVar* onDrag;
    int32_t onDragLen;
    struct RSCache_Dat2ComponentScriptVar* onDragComplete;
    int32_t onDragCompleteLen;
    struct RSCache_Dat2ComponentScriptVar* onScrollWheel;
    int32_t onScrollWheelLen;
    struct RSCache_Dat2ComponentScriptVar* onVarcTransmit;
    int32_t onVarcTransmitLen;
    struct RSCache_Dat2ComponentScriptVar* onVarcstrTransmit;
    int32_t onVarcstrTransmitLen;

    int32_t* varpTriggers;
    int32_t varpTriggersLen;
    int32_t* inventoryTriggers;
    int32_t inventoryTriggersLen;
    int32_t* statTriggers;
    int32_t statTriggersLen;
    int32_t* varcTriggers;
    int32_t varcTriggersLen;
    int32_t* varcstrTriggers;
    int32_t varcstrTriggersLen;

    int32_t** cs1Scripts;
    int32_t* cs1ScriptsLengths;
    int32_t cs1ScriptsLen;
    int32_t* cs1ComparisonOpcodes;
    int32_t* cs1ComparisonOperands;
    int32_t cs1ComparisonLen;

    struct RSCache_Dat2Component** createdComponents;
    int32_t createdComponentsLen;
    int32_t createdComponentId;

    struct RSCache_Dat2ServerActiveProperties serverActiveProperties;
    bool if3;
    bool hasHook;
    char** ops;
    int32_t opsLen;
    int32_t* opCursors;
    int32_t opCursorsLen;
};

/**
 * All decoded widgets from one interfaces archive (idx=iface_id).
 * Dat2 stores interfaces in a single archive. Several components are packed together. The "ID" of a
 * single component is calculated by packing the interface ID and the component index. (interface_id
 * << 16) | (component_index & 0xFFFF)
 * Where the component index is the index of the component in the filelist.
 */
struct RSCache_Dat2ComponentPack
{
    struct RSCache_Dat2Component** components;
    int component_count;
};

/** IF3 field-layout family. The layouts differ only in the type-5 and
 *  type-6 blocks: 643-era files carry a trailing colour int on type 5
 *  (flips ordered H,V) and aShort49+aBoolean411 on type 6 with each size
 *  override short gated on its own mode; OSRS-era files omit all of those,
 *  order the type-5 flips V,H, and carry both type-6 override shorts
 *  whenever either size mode is dynamic. cache, cache.jan2026 and
 *  cache.kronos all verify 100% exact-consumption as OSRS.
 *
 *  The two families cannot be told apart from the index revision alone:
 *  643-era caches number their reference tables in the same small-integer
 *  range OSRS used before it switched to unix-timestamp revisions. */
enum RSCache_Dat2ComponentDecodeEra
{
    RSCACHE_DAT2_COMPONENT_DECODE_ERA_OSRS = 0,
    RSCACHE_DAT2_COMPONENT_DECODE_ERA_643,
};

/** Interfaces reference-table version at which OSRS revision 237 widened the
 *  type-6 model ids from u16 to i32 (if1 reads two, if3 one). RuneLite gates
 *  the same field off the same value — InterfaceLoader.configureForRevision()
 *  fed from Index.getRevision(), commit c2e1eb3 "cache: update 237"
 *  (2026-03-15). Local caches: cache.kronos=997, cache=1193,
 *  cache.jan2026=1767694604 — all below the threshold. */
#define RSCACHE_DAT2_COMPONENT_INDEX_REVISION_237 1773137432

/** Unknown interfaces revision: decode with the pre-237 field widths. */
#define RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN (-1)

/** What the decoder needs to pick field widths and field order: which layout
 *  family the cache belongs to, plus its interfaces reference-table version
 *  (RSCache_ReferenceTable.version) for the revision-gated fields within the
 *  OSRS family. */
struct RSCache_Dat2ComponentDecodeRev
{
    enum RSCache_Dat2ComponentDecodeEra era;
    int32_t index_revision;
    /** Game revision, when the caller knows it — it answers the rev-237 question
     *  directly instead of via the index-revision threshold, which only works
     *  for caches whose interfaces table happens to carry a timestamp. Zero
     *  (RSCACHE_REVISION_UNKNOWN) means "fall back to index_revision", which is
     *  what the designated initialisers below leave it as. */
    int32_t game_revision;
};

/** OSRS-family layout at `index_revision` (pass
 *  RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN when the caller has no
 *  reference table on hand). */
struct RSCache_Dat2ComponentDecodeRev
RSCache_Dat2ComponentDecodeRevOsrs(int32_t index_revision);

/** 643-era layout; index revisions are not meaningful across families. */
struct RSCache_Dat2ComponentDecodeRev
RSCache_Dat2ComponentDecodeRev643(void);

/**
 * Derive the layout from a cache profile — the canonical path.
 *
 * Resolves the one question the index revision cannot: the 643 and OSRS families
 * number their reference tables in the same range, so only the profile's `epoch`
 * can tell them apart. Before this existed, RSCache_Dat2ComponentDecodeRev643
 * had no caller and the 643 layout was unreachable.
 *
 * `index_revision` is still passed in because it is per-table metadata the
 * profile does not carry; supply the interfaces reference table's version, or
 * RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN.
 */
struct RSCache_Dat2ComponentDecodeRev
RSCache_Dat2ComponentDecodeRevFromProfile(
    const struct RSCache* cache,
    int32_t index_revision);

void
RSCache_Dat2ComponentInit(struct RSCache_Dat2Component* c);

void
RSCache_Dat2ComponentFree(struct RSCache_Dat2Component* c);

void
RSCache_Dat2ComponentDecodeIf1(
    struct RSCache_Dat2Component* self,
    struct RSCache_Buffer* buffer,
    struct RSCache_Dat2ComponentDecodeRev rev);

void
RSCache_Dat2ComponentDecodeIf3(
    struct RSCache_Dat2Component* self,
    struct RSCache_Buffer* buffer,
    struct RSCache_Dat2ComponentDecodeRev rev);

void
RSCache_Dat2ComponentSetOp(
    struct RSCache_Dat2Component* self,
    int32_t i,
    const char* op);

/**
 * Encode a component — the exact inverse of the decoder that produced it.
 *
 * Dispatches on the component's own `if3` flag, so a caller does not have to know
 * which era a widget came from: both families are present in the same cache (2,096
 * of osrs239's 26,478 components are IF1, and 11,086 of osrs184's 22,931 are).
 *
 * Three places the decode is not a plain read, and the encode has to undo each:
 *
 *  - **`layer`** is stored relative and read absolute: the decoder adds the
 *    component's own interface id (`id & 0xFFFF0000`) and maps 65535 to -1. So the
 *    encoder subtracts it back, and writes 65535 for -1.
 *  - **`modelId` / `modelSeqId` / `textFont`** each use 65535 as their absent
 *    sentinel and decode to -1.
 *  - **type 5's flag byte** packs `alpha` and `tiled` into bits 1 and 0.
 *
 * The era matters as much as it does on the way in: `rev_has_int_model_ids` widens
 * type 6's model id from u16 to u32 at revision 237, and encoding with the wrong
 * rev shifts every byte after it.
 *
 * The IF1 path is byte-exact except for one thing it cannot know: its type-1 block
 * reads three bytes the decoder discards, so those re-encode as zeros. See the
 * note on `component_encode_if1`.
 *
 * The RS2 (643) layout is decoded by `decode_if3_rs2` and is **not** inverted here;
 * that rev is refused.
 *
 * Returns bytes written, or 0 on failure (RS2 rev, or `out` too small).
 */
uint32_t
RSCache_Dat2ComponentEncodeIf3(
    const struct RSCache_Dat2Component* self,
    struct RSCache_Dat2ComponentDecodeRev rev,
    uint8_t* out,
    uint32_t out_capacity);

/** Safe `out_capacity` for RSCache_Dat2ComponentEncodeIf3. */
uint32_t
RSCache_Dat2ComponentEncodeIf3Bound(const struct RSCache_Dat2Component* self);

void
RSCache_Dat2ComponentPackFree(struct RSCache_Dat2ComponentPack* pack);

struct RSCache_Dat2Component*
RSCache_Dat2ComponentNewDecode(
    uint8_t* data,
    int data_size,
    int packed_id,
    struct RSCache_Dat2ComponentDecodeRev rev);

/** @param file_ids archive file id per filelist slot, or NULL when the archive's
 *  ids are known to be 0..file_count-1. Interface archives can be sparse (Jagex
 *  removes a widget without renumbering the rest), and a component's uid is
 *  (interface_id << 16) | file_id — NOT its position in the list. Using the
 *  position shifts every uid after a gap, which silently reparents widgets and
 *  hands them each other's geometry. */
struct RSCache_Dat2ComponentPack*
RSCache_Dat2ComponentPackNewFromFileList(
    struct RSCache_FileList* filelist,
    int interface_id,
    struct RSCache_Dat2ComponentDecodeRev rev,
    int const* file_ids);

struct RSCache_Dat2ComponentPack*
RSCache_Dat2ComponentPackNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int interface_id,
    struct RSCache_Dat2ComponentDecodeRev rev);

#endif
