#ifndef CS2_HOST_UI_H
#define CS2_HOST_UI_H

#include "cs2vm.h"

struct ToriAuxLibCore;
struct ToriAuxLibCache;
struct ToriAuxLibVM;
struct ToriDraw_Scene;
struct UITree;

/** Optional callback when a varp is written (for onVarpTransmit dispatch). */
typedef void (*CS2HostUIVarpChangeFn)(
    void* ud,
    int varp_id);

/** Resolve obj id to pre-rasterized icon scene/atlas. Return true when icon exists. */
typedef bool (*CS2HostUIResolveObjIconFn)(
    void* ud,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index);

typedef int (*CS2HostUIInvGetObjFn)(
    void* ud,
    int inv_id,
    int slot);

typedef int (*CS2HostUIInvGetNumFn)(
    void* ud,
    int inv_id,
    int slot);

typedef int (*CS2HostUIInvSizeFn)(
    void* ud,
    int inv_id);

typedef int (*CS2HostUIEnumOutputCountFn)(
    void* ud,
    int enum_id);

typedef bool (*CS2HostUIStructParamFn)(
    void* ud,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str);

/** Optional: notify editor when CC_CREATE succeeds (ok=1) or fails (ok=0). */
typedef void (*CS2HostUICcCreateFn)(
    void* ud,
    int parent_component_id,
    int sub_id,
    int ok);

struct CS2HostUIInitArgs
{
    struct ToriAuxLibCore* core;
    struct ToriAuxLibCache* cache;
    struct ToriAuxLibVM* vm;
    struct UITree* tree;
    struct ToriDraw_Scene* scene;
    CS2HostUIVarpChangeFn on_varp_change;
    void* on_varp_change_ud;
    CS2HostUIResolveObjIconFn resolve_obj_icon;
    void* resolve_obj_icon_ud;
    CS2HostUIInvGetObjFn inv_get_obj;
    CS2HostUIInvGetNumFn inv_get_num;
    CS2HostUIInvSizeFn inv_size;
    void* inv_ud;
    CS2HostUIEnumOutputCountFn enum_output_count;
    void* enum_output_count_ud;
    CS2HostUIStructParamFn struct_param;
    void* struct_param_ud;
    CS2HostUICcCreateFn cc_create_notify;
    void* cc_create_notify_ud;
    int viewport_w;
    int viewport_h;
    int client_type;
    /** 1 = fixed, 2 = resizable (GETWINDOWMODE). */
    int window_mode;
};

void
cs2_host_ui_init(
    struct CS2Host* host,
    struct CS2HostUIInitArgs const* args);

void
cs2_host_ui_invoke(
    void* ud,
    struct CS2_InvokeCtx* ctx);

/** Advance CLIENTCLOCK once per game tick (20ms cycle). */
void
cs2_host_ui_tick(struct CS2Host* host);

#endif
