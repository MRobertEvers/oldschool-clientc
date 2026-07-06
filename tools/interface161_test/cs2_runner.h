#ifndef INTERFACE161_CS2_RUNNER_H
#define INTERFACE161_CS2_RUNNER_H

#include "fixture.h"
#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toridraw/toridraw.h"
#include "ui/ui_inv_data_service.h"
#include "ui/uitree.h"

struct CS2VM;
struct CS2Host;

struct Interface161Cs2Context
{
    struct RSCacheDat2Disk* cache;
    struct UITree* tree;
    struct CS2VM* cs2vm;
    struct CS2Host* cs2host;
    struct UIInvDataService inv_data;
    struct ToriDraw_Scene* scene;
    struct Interface161ObjIconCache** obj_icon_cache;
    int root_w;
    int root_h;
    int clientscript_decode_flags;
};

void
interface161_cs2_set_clientscript_decode_flags(int flags);

int
interface161_cs2_clientscript_decode_flags(void);

void
interface161_cs2_context_init(struct Interface161Cs2Context* ctx);

void
interface161_cs2_context_free(struct Interface161Cs2Context* ctx);

int
interface161_cs2_run_interface(
    struct Interface161Cs2Context* ctx,
    RSCacheDat2A_Component* comps,
    int comp_count,
    int const* lay_x,
    int const* lay_y,
    int const* lay_w,
    int const* lay_h,
    int iface_id,
    struct Interface161Fixture const* fixture);

/** Populate an existing UITree from decoded interface components (no CS2 hooks). */
int
interface161_cs2_build_tree(
    struct UITree* tree,
    RSCacheDat2A_Component* comps,
    int comp_count);

/** Build UITree + CS2 host for a decoded interface without running CS2 hooks. */
int
interface161_cs2_prepare_exec_shell(
    struct Interface161Cs2Context* ctx,
    RSCacheDat2A_Component* comps,
    int comp_count,
    int iface_id,
    int root_w,
    int root_h);

#endif
