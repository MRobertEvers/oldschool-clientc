#ifndef RSCACHE_RSCACHEDAT1A_CONFIGCOMPONENT_H
#define RSCACHE_RSCACHEDAT1A_CONFIGCOMPONENT_H

#include <stdbool.h>
#include <stdint.h>

enum RSCacheDat1A_ConfigComponentType
{
    COMPONENT_TYPE_LAYER = 0,
    COMPONENT_TYPE_UNUSED = 1, // TODO
    COMPONENT_TYPE_INV = 2,
    COMPONENT_TYPE_RECT = 3,
    COMPONENT_TYPE_TEXT = 4,
    COMPONENT_TYPE_GRAPHIC = 5,
    COMPONENT_TYPE_MODEL = 6,
    COMPONENT_TYPE_INV_TEXT = 7,
};

enum RSCacheDat1A_ConfigComponentButtonType
{
    COMPONENT_BUTTON_TYPE_OK = 1,
    COMPONENT_BUTTON_TYPE_TARGET = 2,
    COMPONENT_BUTTON_TYPE_CLOSE = 3,
    COMPONENT_BUTTON_TYPE_TOGGLE = 4,
    COMPONENT_BUTTON_TYPE_SELECT = 5,
    COMPONENT_BUTTON_TYPE_CONTINUE = 6,
};

struct RSCacheDat1A_ConfigComponent
{
    // invSlotObjId: Int32Array | null = null;
    // invSlotObjCount: Int32Array | null = null;
    // seqFrame: number = 0;
    // seqCycle: number = 0;
    // id: number = -1;
    // layer: number = -1;
    // type: number = -1;
    // buttonType: number = -1;
    // clientCode: number = 0;
    // width: number = 0;
    // height: number = 0;
    // alpha: number = 0;
    // x: number = 0;
    // y: number = 0;
    // scripts: (Uint16Array | null)[] | null = null;
    // scriptComparator: Uint8Array | null = null;
    // scriptOperand: Uint16Array | null = null;
    // overlayer: number = -1;
    // scroll: number = 0;
    // scrollPosition: number = 0;
    // hide: boolean = false;
    // children: number[] | null = null;
    // activeModelType: number = 0;
    // activeModel: number = 0;
    // anim: number = -1;
    // activeAnim: number = -1;
    // zoom: number = 0;
    // xan: number = 0;
    // yan: number = 0;
    // targetVerb: string | null = null;
    // targetText: string | null = null;
    // targetMask: number = -1;
    // option: string | null = null;
    // static modelCache: LruCache = new LruCache(30);
    // static imageCache: LruCache | null = null;
    // marginX: number = 0;
    // marginY: number = 0;
    // colour: number = 0;
    // activeColour: number = 0;
    // overColour: number = 0;
    // activeOverColour: number = 0;
    // modelType: number = 0;
    // model: number = 0;
    // graphic: Pix32 | null = null;
    // activeGraphic: Pix32 | null = null;
    // font: PixFont | null = null;
    // text: string | null = null;
    // activeText: string | null = null;
    // draggable: boolean = false;
    // interactable: boolean = false;
    // usable: boolean = false;
    // swappable: boolean = false;
    // fill: boolean = false;
    // center: boolean = false;
    // shadowed: boolean = false;
    // invSlotOffsetX: Int16Array | null = null;
    // invSlotOffsetY: Int16Array | null = null;
    // childX: number[] | null = null;
    // childY: number[] | null = null;
    // invSlotGraphic: (Pix32 | null)[] | null = null;
    // iop: (string | null)[] | null = null;

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
    char const* option;
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
    char* graphic;
    char* activeGraphic;
    char** invSlotGraphic;
    char** iop;
    int* childX;
    int* childY;
    int font;
};

struct RSCacheDat1A_ConfigComponentList
{
    struct RSCacheDat1A_ConfigComponent** components;
    int components_count;
};

struct RSCacheDat1A_ConfigComponentList*
RSCacheDat1A_ConfigComponentListNewDecode(
    void* data,
    int size);

struct RSCacheDat1A_ConfigComponentList*
RSCacheDat1A_ConfigComponentListNewDecodeFiltered(
    void* data,
    int size,
    bool* needed,
    int needed_count);

int
RSCacheDat1A_ConfigComponentListPeekCount(
    void* data,
    int size);

void
RSCacheDat1A_ConfigComponentFree(struct RSCacheDat1A_ConfigComponent* component);

#endif