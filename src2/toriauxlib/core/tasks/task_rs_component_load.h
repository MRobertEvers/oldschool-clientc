#ifndef TASK_RS_COMPONENT_LOAD_H
#define TASK_RS_COMPONENT_LOAD_H

#include "3rd/minipt.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "toriauxlib/c/toriauxlibcache.h"

#include <stdbool.h>

struct InstanceRevConfigContext;
struct ToriDraw_Scene;
struct LibToriRS_IOContext;

#define RS_COMPONENT_INFO_TEXT_MAX 256
#define RS_COMPONENT_INFO_SPRITE_MAX 128

enum RSComponentInfoType
{
    RS_COMPONENT_LAYER = 0,
    RS_COMPONENT_INV,
    RS_COMPONENT_RECT,
    RS_COMPONENT_TEXT,
    RS_COMPONENT_GRAPHIC,
    RS_COMPONENT_MODEL,
    RS_COMPONENT_INV_TEXT,
};

#define RS_COMPONENT_STACK_MAX 256

struct RSComponentInfo
{
    enum RSComponentInfoType type;
    int id;
    int parent_id; /* RS component id of parent layer; -1 for tree root */
    int rel_x;
    int rel_y;
    int width;
    int height;
    char sprite_ref[RS_COMPONENT_INFO_SPRITE_MAX];
    char sprite_active_ref[RS_COMPONENT_INFO_SPRITE_MAX];
    int model_id;
    int color;
    int filled;
    int font_id;
    int center;
    int shadowed;
    char text[RS_COMPONENT_INFO_TEXT_MAX];
    int inv_cols;
    int inv_rows;
    int margin_x;
    int margin_y;
    int child_count;
    int child_ids[32];
    int child_x[32];
    int child_y[32];
};

struct RSComponentLoadCallbacks
{
    void* user;
    void (*on_component)(
        void* user,
        struct RSComponentInfo const* info);
};

struct Task_RSComponentLoad
{
    struct pt thread;
    enum ToriAuxLibCacheMode cache_mode;
    struct ToriAuxLibCache* cache;
    struct ToriDraw_Scene* scene;
    struct InstanceRevConfigContext* rc_ctx;
    int root_component_id;
    struct RSComponentLoadCallbacks callbacks;

    /* explicit work stack (dat1: component ids + parent ids) */
    int stack[RS_COMPONENT_STACK_MAX];
    int stack_x[RS_COMPONENT_STACK_MAX];
    int stack_y[RS_COMPONENT_STACK_MAX];
    int stack_parent_id[RS_COMPONENT_STACK_MAX];
    int stack_count;

    /* dat2 work stack */
    int dat2_iface_id;
    int dat2_file_index;
};

struct Task_RSComponentLoad*
Task_RSComponentLoad_New(
    enum ToriAuxLibCacheMode cache_mode,
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks);

void
Task_RSComponentLoad_Free(struct Task_RSComponentLoad* task);

int
Task_RSComponentLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx);

#endif
