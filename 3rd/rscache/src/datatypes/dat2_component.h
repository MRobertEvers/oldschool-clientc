#ifndef RSCACHE_DATATYPES_DAT2_COMPONENT_H
#define RSCACHE_DATATYPES_DAT2_COMPONENT_H

#include "../dat2disk.h"
#include "../filelist.h"

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
};

/** OSRS-family layout at `index_revision` (pass
 *  RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN when the caller has no
 *  reference table on hand). */
struct RSCache_Dat2ComponentDecodeRev
RSCache_Dat2ComponentDecodeRevOsrs(int32_t index_revision);

/** 643-era layout; index revisions are not meaningful across families. */
struct RSCache_Dat2ComponentDecodeRev
RSCache_Dat2ComponentDecodeRev643(void);

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
