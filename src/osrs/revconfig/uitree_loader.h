#ifndef UITREE_LOADER_H
#define UITREE_LOADER_H

#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/uitree.h"

#include <stdint.h>

/** Max distinct GameCache component ids reported in one UITREE_ASSET_INTERFACE request. */
#define UITREE_MAX_INTERFACE_REQUESTS 128

/** Max asset requests emitted in one UITREE_LOADER_NEEDS_ASSET pause. */
#define UITREE_LOADER_MAX_PENDING_ASSETS 512

enum UITreeLoaderRSComponentType
{
    UITREE_LOADER_RS_COMPONENT_TYPE_LAYER,
    UITREE_LOADER_RS_COMPONENT_TYPE_INV,
    UITREE_LOADER_RS_COMPONENT_TYPE_RECT,
    UITREE_LOADER_RS_COMPONENT_TYPE_TEXT,
    UITREE_LOADER_RS_COMPONENT_TYPE_GRAPHIC,
    UITREE_LOADER_RS_COMPONENT_TYPE_MODEL,
    UITREE_LOADER_RS_COMPONENT_TYPE_INV_TEXT,
};
/**
 * Status returned by uitree_loader_step().
 */
enum UITreeLoaderStatus
{
    /** Still processing fields; call step() again. */
    UITREE_LOADER_RUNNING,
    /** Paused waiting for an external asset; fill the request and call step() again. */
    UITREE_LOADER_NEEDS_ASSET,
    /** All fields processed; tree is fully built. */
    UITREE_LOADER_DONE,
    /** Unrecoverable error during loading. */
    UITREE_LOADER_ERROR,
};

/**
 * Tagged kind for the asset the loader needs the caller to supply.
 */
enum UITreeLoaderAssetKind
{
    UITREE_ASSET_NONE = 0,
    /** Sprite ref missing from GameCache; load 2D media / register sprites, then retry. */
    UITREE_ASSET_SPRITE,
    /** A GameCacheComponent is absent; load interface archive and convert components. */
    UITREE_ASSET_RS_COMPONENT,
    /** A specific model is missing from the model cache. */
    UITREE_ASSET_MODEL,
    /** Font name missing from GameCache font reftable. */
    UITREE_ASSET_FONT,
};

struct UITreeLoaderAssetRequest_RSComponent
{
    struct
    {
        int id;
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
        int anim;
        int activeAnim;
        /** Client IfType model2Type / model2Id when getIfActive (0 = unset, use
         * modelType/model). */
        int activeModelType;
        int activeModel;
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
    } resolve;
};

enum UITreeLoaderModelType
{
    UITREE_LOADER_MODEL_TYPE_MODEL,
    UITREE_LOADER_MODEL_TYPE_NPC,
    UITREE_LOADER_MODEL_TYPE_LOCAL_PLAYER,
    UITREE_LOADER_MODEL_TYPE_OBJ,
    UITREE_LOADER_MODEL_TYPE_NONE,
};

struct UITreeLoaderAssetRequest_Model
{
    int model_id;
    enum UITreeLoaderModelType model_type;
    int model_zoom;
    int model_xan;
    int model_yan;
    int x;
    int y;
    int width;
    int height;

    struct
    {
        int scene_id;
    } resolve;
};

/**
 * Describes the asset the loader could not find.  Inspect `kind`, then read the
 * matching union member to learn what to load.
 */
struct UITreeLoaderAssetRequest
{
    enum UITreeLoaderAssetKind kind;

    int component_id;
    int parent_idx;

    bool resolved;
    union
    {
        /** UITREE_ASSET_SPRITE: logical sprite name from the INI (informational only). */
        struct
        {
            char name[64];
        } sprite;

        struct UITreeLoaderAssetRequest_RSComponent rs_component;

        struct UITreeLoaderAssetRequest_Model rs_model;
        /** UITREE_ASSET_FONT: font name not yet registered in UIScene. */
        struct
        {
            char name[64];
        } font;
    } u;
};

/**
 * Incremental UITree loader.  Create with uitree_loader_new(ui, revconfig), drive with
 * uitree_loader_step(loader, revconfig), and free with uitree_loader_free() when DONE or ERROR.
 *
 * Lower level: uitree_loader_send_revconfig parses INI fields; when status is
 * UITREE_LOADER_NEEDS_ASSET with satisfied bindings, uitree_loader_send applies them.
 * After each `[component:…]` section, call uitree_loader_send_rs_component (when
 * uitree_loader_rs_component_commit_pending) before further step() ticks.
 * Most callers should use uitree_loader_step() only.
 */
struct UITreeLoader;

struct UITreeLoader*
uitree_loader_new(struct UITree* ui);

enum UITreeLoaderStatus
uitree_loader_send_rs_component(
    struct UITreeLoader* loader,
    int component_id);

enum UITreeLoaderStatus
uitree_loader_send(struct UITreeLoader* loader);

enum UITreeLoaderStatus
uitree_loader_send_revconfig(
    struct UITreeLoader* loader,
    struct RevConfigBuffer* revconfig);

int
uitree_loader_pending_asset_count(const struct UITreeLoader* loader);

const struct UITreeLoaderAssetRequest*
uitree_loader_pending_assets(const struct UITreeLoader* loader);

/** Writable pending slots while status is UITREE_LOADER_NEEDS_ASSET (uitree_loader_game_executor_*
 * writes bindings).
 */
struct UITreeLoaderAssetRequest*
uitree_loader_pending_assets_mut(struct UITreeLoader* loader);

void
uitree_loader_free(struct UITreeLoader* loader);

#endif /* UITREE_LOADER_H */
