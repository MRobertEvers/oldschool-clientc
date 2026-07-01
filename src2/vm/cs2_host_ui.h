#ifndef CS2_HOST_UI_H
#define CS2_HOST_UI_H

#include "cs2vm.h"

struct ToriAuxLibCore;
struct ToriAuxLibCache;
struct ToriAuxLibVM;
struct UITree;

/** Optional callback when a varp is written (for onVarpTransmit dispatch). */
typedef void (*CS2HostUIVarpChangeFn)(
    void* ud,
    int varp_id);

struct CS2HostUIInitArgs
{
    struct ToriAuxLibCore* core;
    struct ToriAuxLibCache* cache;
    struct ToriAuxLibVM* vm;
    struct UITree* tree;
    CS2HostUIVarpChangeFn on_varp_change;
    void* on_varp_change_ud;
};

void
cs2_host_ui_init(
    struct CS2Host* host,
    struct CS2HostUIInitArgs const* args);

void
cs2_host_ui_invoke(
    void* ud,
    struct CS2_InvokeCtx* ctx);

#endif
