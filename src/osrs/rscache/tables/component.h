#ifndef OSRS_RSCACHE_TABLES_COMPONENT_H
#define OSRS_RSCACHE_TABLES_COMPONENT_H

#include <stdbool.h>
#include <stdint.h>

struct RSBuffer;

typedef struct Model Model;
typedef struct SeqType SeqType;
typedef struct PlayerAppearance PlayerAppearance;

typedef enum
{
    SCRIPT_VAR_INT,
    SCRIPT_VAR_STRING
} ComponentScriptVarType;

typedef struct
{
    ComponentScriptVarType type;
    union
    {
        int32_t i;
        char* s;
    } value;
} ComponentScriptVar;

typedef struct
{
    int32_t events;
    int32_t targetMask;
} ServerActiveProperties;

/**
 * IF1 / IF3 interface widget. Decode layout follows InterfaceLoader.java (RuneLite cache).
 */
typedef struct Component
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

    int32_t modelType;
    int32_t modelId;
    int32_t modelZoom;
    int32_t modelXAngle;
    int32_t modelYAngle;
    int32_t modelZAngle;
    int32_t modelXOffset;
    int32_t modelYOffset;
    int32_t modelSeqId;
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
    bool aBoolean411;
    int32_t anInt5957;
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

    ComponentScriptVar* onLoad;
    int32_t onLoadLen;
    ComponentScriptVar* onMouseOver;
    int32_t onMouseOverLen;
    ComponentScriptVar* onMouseLeave;
    int32_t onMouseLeaveLen;
    ComponentScriptVar* onTargetLeave;
    int32_t onTargetLeaveLen;
    ComponentScriptVar* onTargetEnter;
    int32_t onTargetEnterLen;
    ComponentScriptVar* onVarpTransmit;
    int32_t onVarpTransmitLen;
    ComponentScriptVar* onInvTransmit;
    int32_t onInvTransmitLen;
    ComponentScriptVar* onStatTransmit;
    int32_t onStatTransmitLen;
    ComponentScriptVar* onTimer;
    int32_t onTimerLen;
    ComponentScriptVar* onOp;
    int32_t onOpLen;
    ComponentScriptVar* onMouseRepeat;
    int32_t onMouseRepeatLen;
    ComponentScriptVar* onClick;
    int32_t onClickLen;
    ComponentScriptVar* onClickRepeat;
    int32_t onClickRepeatLen;
    ComponentScriptVar* onRelease;
    int32_t onReleaseLen;
    ComponentScriptVar* onHold;
    int32_t onHoldLen;
    ComponentScriptVar* onDrag;
    int32_t onDragLen;
    ComponentScriptVar* onDragComplete;
    int32_t onDragCompleteLen;
    ComponentScriptVar* onScrollWheel;
    int32_t onScrollWheelLen;
    ComponentScriptVar* onVarcTransmit;
    int32_t onVarcTransmitLen;
    ComponentScriptVar* onVarcstrTransmit;
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
    int32_t cs1ScriptsLen;
    int32_t* cs1ComparisonOpcodes;
    int32_t* cs1ComparisonOperands;
    int32_t cs1ComparisonLen;

    struct Component** createdComponents;
    int32_t createdComponentsLen;
    int32_t createdComponentId;

    ServerActiveProperties serverActiveProperties;
    bool if3;
    bool hasHook;
    char** ops;
    int32_t opsLen;
    int32_t* opCursors;
    int32_t opCursorsLen;
} Component;

void
Component_init(Component* c);

void
Component_free(Component* c);

void
Component_clearStaticCaches(void);

void
Component_decodeIf1(
    Component* self,
    struct RSBuffer* buffer);

void
Component_decodeIf3(
    Component* self,
    struct RSBuffer* buffer);

void
Component_setOp(
    Component* self,
    int32_t i,
    const char* op);

Model*
Component_getModel(
    Component* self,
    SeqType* seq,
    PlayerAppearance* appearance,
    int32_t frame,
    int32_t arg3,
    int32_t arg4,
    bool arg5);

#endif
