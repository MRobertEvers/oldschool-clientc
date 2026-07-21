#ifndef SRC_APP_H
#define SRC_APP_H

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/uitree_anim.h"
#include "engine/uitree_builder/task_interface_open.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs1_host.h"
#include "game/rs_cs2_host.h"
#include "game/rs_player_stats.h"
#include "input/torirs_input.h"
#include "inv/inv_manager.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"
#include "toridraw_scene.h"
#include "ui/uitree.h"
#include "ui/uitree_cross.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_host.h"
#include "ui/uitree_interact.h"
#include "varp/varp_manager.h"
#include "world/world_pickset.h"

#include <stdint.h>

struct World;
struct WorldBuilder;
struct PaintersBuffer;

/*
 * Application shell: owns every subsystem and the update loop body, with no
 * platform (SDL) dependency so it compiles headless for tests and the future
 * WASM shell. The platform layer polls input, calls App_RunOnce once per
 * frame, and blits App_Render's pixels.
 */

/*
 * Component ids for the app-pushed overlay nodes (click cross, minimenu).
 * High interface group 0x7FFE keeps them outside any cache interface id.
 */
enum
{
    APP_COM_ID_CROSS = (0x7FFE << 16) | 0,
    APP_COM_ID_MINIMENU = (0x7FFE << 16) | 1,
    APP_COM_ID_WORLD = (0x7FFE << 16) | 2,
};

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

    /* Phase 4b: world sim + builder (needs provider + scene + varps; the
     * World references assets and scene elements by integer id only). */
    struct World* world;
    struct WorldBuilder* world_builder;
    struct PaintersBuffer* painter_buffer;
    struct ToriDraw_Camera world_camera;
    struct ToriDraw_Position world_camera_pos;
    int world_active; /* 1 once Task_WorldLoad completed */

    /* World picking: the full pickset refreshes as part of every rendered
     * frame (App_Render hittests visible models at world_mouse_x/y); click
     * handlers consume the last rendered set. world_emit_desc caches the
     * WORLD node's emit desc — the gate rect and the exact viewport the
     * render pass draws with. */
    struct World_PickSet world_pickset;
    struct UITreeEmitDesc world_emit_desc;
    int world_view_valid;
    int world_mouse_in_viewport;
    int world_mouse_x; /* last input mouse, canvas coords */
    int world_mouse_y;
    int world_hover_tile_x; /* scene tile, -1 = none */
    int world_hover_tile_z;
    int world_hover_tile_level;

    /* Projectile hotkey latch: first press = src tile, second = dst + fire. */
    int proj_src_tile_x; /* -1 = unarmed */
    int proj_src_tile_z;
    int proj_src_tile_level;

    /* Phase 4: game state. */
    struct UITree* tree;
    struct InvManager invs;
    struct VarPManager varps;
    struct RS_PlayerStats stats;
    struct RS_CS2Host host;
    struct RS_CS1Host cs1_host;

    /* Phase 5: frame state. */
    struct UITreeHost ui_host;
    struct UITreeEmitBuffer emit;
    struct UIInteraction interact;
    struct SeqLoadTracker seq_loads;
    struct InterfaceOpenStats open_stats;
    struct UICross cross;
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
