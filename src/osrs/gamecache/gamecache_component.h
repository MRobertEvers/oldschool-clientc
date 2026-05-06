#ifndef GAMECACHE_COMPONENT_H
#define GAMECACHE_COMPONENT_H

#include <stdbool.h>

/** Standalone GameCache component type -- all fields except layer, activeModelType, activeModel. */
struct GameCacheComponent
{
    int id;
    /* layer dropped -- not consumed */
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
    int script_comparator_count;
    int* invSlotObjId;
    int* invSlotObjCount;
    int seqFrame;
    int seqCycle;
    int* children;
    int children_count;
    /* activeModelType dropped */
    /* activeModel dropped */
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

#endif
