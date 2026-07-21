#ifndef SRC_APP_H
#define SRC_APP_H

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/uitree_anim.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "input/torirs_input.h"
#include "inv/inv_manager.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"
#include "toridraw_scene.h"
#include "ui/uitree.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_host.h"
#include "ui/uitree_interact.h"
#include "varp/varp_manager.h"

#include <stdint.h>

/*
 * Application shell: owns every subsystem and the update loop body, with no
 * platform (SDL) dependency so it compiles headless for tests and the future
 * WASM shell. The platform layer polls input, calls App_RunOnce once per
 * frame, and blits App_Render's pixels.
 */

struct AppConfig
{
    char const* cache_dir;
    char const* config_dir;
    char const* script_dir;
    int interface_id;
};

struct App
{
    struct AppConfig cfg;

    /* Phase 1: task runtime + disk (created first, freed last). */
    struct TaskRunner runner; /* owns queue + io + px */
    struct RSCache_Dat2Disk* disk;

    /* Phase 2: asset pipeline. */
    struct Dat2BuildCache* bc;
    struct CacheProvider* provider;

    /* Phase 3: scene + bridge. */
    struct ToriDraw_Scene* scene;
    struct UITreeSceneBridge bridge;

    /* Phase 4: game state. */
    struct UITree* tree;
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_CS2Host host;

    /* Phase 5: frame state. */
    struct UITreeHost ui_host;
    struct UITreeEmitBuffer emit;
    struct UIInteraction interact;
    struct SeqLoadTracker seq_loads;
    struct InterfaceOpenStats open_stats;
    uint64_t last_logic_ms;
    int hover_com_id;
    int clicked_com_id;
    int need_redraw;
};

/** Construct all subsystems in dependency order. Asserts on failure (parity
 * with the previous bootstrap). */
void
App_Init(struct App* app, struct AppConfig const* cfg);

/** Tear down in strict reverse of App_Init. */
void
App_Shutdown(struct App* app);

/** Open an interface as the tree root (TS WidgetManager.setRootInterface) and
 * drain the load to completion; runs initial animations + emit. */
void
App_OpenRootInterface(struct App* app, int interface_id);

/**
 * One loop-body iteration: pump tasks, run pending 20ms logic ticks
 * (client clock, widget timers, animations), then the per-frame interaction
 * pass and emit rebuild. Returns non-zero when the frame needs re-rendering.
 */
int
App_RunOnce(struct App* app, uint64_t now_ms, struct LibToriRS_Input* input);

/** Rasterize the current emit buffer into pixels (width x height ARGB). */
void
App_Render(struct App* app, int* pixels, int width, int height);

/** Write the current emit buffer to a BMP. Returns 0 on success. */
int
App_WriteBmp(struct App* app, char const* path, int width, int height);

#endif
