#include "app.h"
#include "bootmanifest/bootmanifest.h"
#include "executor_config.h"
#include "engine/uitree_scene_bridge.h"
#include "engine/world_builder/world_builder.h"
#include "cmd/cmdbus.h"
#include "game/rs_chat.h"
#include "game/rs_cs2_dispatch.h"
#include "game/cs2_harness.h"
#include "game/rs_ui_slots.h"
#include "input/torirs_input.h"
#include "input/torirs_keymap.h"
#include "net/net.h"
#include "net/net_out.h"
#include "perf/torirs_perf.h"
#include "platform/net_transport.h"
#include "platform/platform_audio.h"
#include "platform/platform_sdl2.h"
#if !defined(TORIRS_PLATFORM_WEB) || defined(TORIRS_WEB_CACHE_IDB)
#include "platform/platform_x_io_js5.h"
#include "platform/platform_x_io_js5_cache.h"
#endif
#if defined(TORIRS_WEB_CACHE_IDB)
#include "platform/web_cache_boot.h"
#endif
#if defined(TORIRS_HAVE_GL3)
/* The GPU renderer. Desktop GL 3.2 natively, WebGL1 in the browser — one file,
 * see TORIRS_GL_ES2 in platform_sdl2_renderer_gl3.c. */
#include "platform/platform_sdl2_renderer_gl3.h"
#else
/* Software-only builds (e.g. the Win32/GDI backend) never include the GL header,
 * so struct ToriRS_GL3 needs a file-scope forward declaration -- otherwise the
 * opaque `gl3` pointer in interactive_render_present() and the file-scope `gl3`
 * are two distinct incomplete types and the call is a type error. */
struct ToriRS_GL3;
#endif
#if defined(TORIRS_HAVE_D3D9)
#include "platform/platform_win32_renderer_d3d9.h"
#else
struct ToriRS_D3D9;
#endif
/* GL/WebGL remains opt-in. The XP lane instead defaults to classic fixed-
 * function D3D9; --soft3d explicitly selects its GDI fallback. */
#define TORIRS_GPU_DEFAULT 0
#if defined(TORIRS_HAVE_D3D9)
#define TORIRS_D3D9_DEFAULT 1
#else
#define TORIRS_D3D9_DEFAULT 0
#endif
#include "render/torirs_frame.h"
#include "toridraw_math.h"
#include "ui/uitree_hover.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <bmp.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TORIRS_PLATFORM_WEB)
#include "platform/platform_x_io_web.h"
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

#endif

/* Repo-relative defaults (run from the repo root); pass an explicit cache dir
 * as argv[1] from anywhere else. The default boot is the 254-era dat1 cache
 * driven by the rev_245_2 RevConfig; --dat2 switches to the js5 cache, where
 * an interface id is opened directly instead. */
#define DAT1_CACHE_DIR "cache254"
#define DAT2_CACHE_DIR "cache.jan2026"

/* Render one frame into a BMP for the CS2 harness. Same path TORIRS_EXIT_BMP
 * uses, so a harness frame and an exit frame are the same picture. */
static void
harness_shot(void* user, char const* path)
{
    struct App* app = (struct App*)user;
    int* pixels = (int*)calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
    assert(pixels);
    App_Render(app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    bmp_write_file(path, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    free(pixels);
}

#define DEFAULT_REVCONFIG_UI "v0/osrs/revconfig/configs/rev_245_2/rev_245_2_dat1_ui.ini"
#define DEFAULT_REVCONFIG_CACHE "v0/osrs/revconfig/configs/rev_245_2/rev_245_2_dat1_cache.ini"
#define CONFIG_DIR "config"
#define SCRIPT_DIR "script"
#define DEFAULT_INTERFACE_ID 84

/* TORIRS_DUMP_TREE=1: print the widget tree in the reference client's
 * widgetTreeDump.ts format (interface editor parity diffing). */

static int
dump_widget_type(struct UITreeComponent const* c)
{
    switch( c->type )
    {
    case UIELEM_RS_LAYER:
        return 0;
    case UIELEM_RS_INV:
        return 2;
    case UIELEM_RS_RECT:
        return 3;
    case UIELEM_RS_TEXT:
        return 4;
    case UIELEM_RS_GRAPHIC:
        return 5;
    case UIELEM_RS_MODEL:
        return 6;
    case UIELEM_RS_LINE:
        return 9;
    case UIELEM_RS_INV_TEXT:
        return 8;
    default:
        return -(int)c->type;
    }
}

static char const*
dump_kind(struct UITreeComponent const* c)
{
    switch( c->type )
    {
    case UIELEM_RS_LAYER:
        return "layer";
    case UIELEM_RS_INV:
        return "inventory";
    case UIELEM_RS_RECT:
        return "rectangle";
    case UIELEM_RS_TEXT:
        return "text";
    case UIELEM_RS_GRAPHIC:
        return "graphic";
    case UIELEM_RS_MODEL:
        return "model";
    case UIELEM_RS_LINE:
        return "line";
    default:
        return UITree_ComponentTypeStr(c->type);
    }
}

static int
dump_file_id(struct UITreeComponent const* c)
{
    return c->dynamic ? -1 : (c->component_id & 0xFFFF);
}

static int
dump_index(struct UITreeComponent const* c)
{
    return c->dynamic ? c->dynamic_child_index : (c->component_id & 0xFFFF);
}

struct DumpChildRef
{
    int32_t idx;
    int file_id;
    int child_index;
};

static int
dump_child_cmp(
    void const* va,
    void const* vb)
{
    struct DumpChildRef const* a = (struct DumpChildRef const*)va;
    struct DumpChildRef const* b = (struct DumpChildRef const*)vb;
    if( a->file_id != b->file_id )
        return a->file_id - b->file_id;
    return a->child_index - b->child_index;
}

static int
dump_node_hidden(
    struct UITree const* tree,
    int32_t idx)
{
    while( idx >= 0 )
    {
        if( tree->components[idx].behavior.hide )
            return 1;
        idx = tree->components[idx].parent;
    }
    return 0;
}

static void
dump_tree_node(
    struct App* app,
    int32_t idx,
    int depth)
{
    struct UITree const* tree = app->tree;
    struct UITreeComponent const* c = &tree->components[idx];
    char const* kind = dump_kind(c);
    int i;

    for( i = 0; i < depth; i++ )
        printf("  ");
    printf(
        "[%d] kind=%s widget_type=%d trans=%d fill_mode=0 user_id=0x%08x (%d<<16|%d) %s",
        dump_index(c),
        kind,
        dump_widget_type(c),
        c->trans,
        (unsigned)c->component_id,
        (c->component_id >> 16) & 0xFFFF,
        c->component_id & 0xFFFF,
        c->dynamic ? "dynamic" : "static");

    if( c->type == UIELEM_RS_GRAPHIC )
        printf(
            " graphic=%d",
            UITreeSceneBridge_SpriteCacheIdForScene(&app->bridge, c->u.rs_graphic.scene_id));
    else if( c->type == UIELEM_RS_TEXT )
        printf(
            " font=%d color=0x%x text=\"%s\"",
            c->u.rs_text.font_id,
            (unsigned)c->u.rs_text.color,
            c->u.rs_text.text ? c->u.rs_text.text : "");
    else if( c->type == UIELEM_RS_LINE )
        printf(
            " color=0x%x width=%d dir=%d",
            (unsigned)c->u.rs_line.color,
            c->u.rs_line.line_width,
            c->u.rs_line.horizontal ? 1 : 0);

    if( c->type != UIELEM_RS_LAYER )
        printf(
            " abs=%d,%d %dx%d hidden=%d ownhide=%d",
            c->position.abs_x,
            c->position.abs_y,
            c->position.abs_w,
            c->position.abs_h,
            dump_node_hidden(tree, idx),
            c->behavior.hide);
    printf("\n");

    {
        struct DumpChildRef refs[512];
        int count = 0;
        int32_t child = c->first_child;
        while( child >= 0 && count < 512 )
        {
            struct UITreeComponent const* cc = &tree->components[child];
            if( !cc->freed )
            {
                refs[count].idx = child;
                refs[count].file_id = dump_file_id(cc);
                refs[count].child_index = cc->dynamic ? cc->dynamic_child_index : 0;
                count++;
            }
            child = cc->next_sibling;
        }
        qsort(refs, (size_t)count, sizeof(refs[0]), dump_child_cmp);
        for( i = 0; i < count; i++ )
            dump_tree_node(app, refs[i].idx, depth + 1);
    }
}

/* Runtime hook script ids per component. Callable both at boot and after the
 * frame loop: server-mounted sub-interfaces (prayer tab 541, …) only carry
 * their hooks once IF_OPENSUB has landed, so the boot-time dump shows none. */
static void
dump_hooks(struct App* app)
{
    uint32_t i;

    /* Positional walk over the slots. The names and the range check belong to
     * the slot type (ui/uitree_hook.h) rather than to a table kept here — the
     * copy that used to live in this function drifted out of step with the
     * struct and silently relabelled every hook past on_mouse_repeat. */
    for( i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent* c = &app->tree->components[i];
        struct UITreeRuntimeHooks const* hooks;
        int h;
        if( c->freed )
            continue;
        hooks = UITree_Hooks(c);
        for( h = 0; h < UITree_HooksSlotCount(); h++ )
        {
            struct UITreeRuntimeScriptHook const* slot = UITree_HooksSlotAtConst(hooks, h);
            if( !UITree_HookIsSet(slot) )
                continue;
            fprintf(
                stderr,
                "HOOKDUMP com=0x%08x (%d|%d) %s script=%d argc=%d\n",
                c->component_id,
                (c->component_id >> 16) & 0xFFFF,
                c->component_id & 0xFFFF,
                UITree_HooksSlotName(h),
                slot->script_id,
                slot->argc);
        }
    }
}

/*
 * Find `group_id`'s top-level nodes in a sibling list, descending through mount
 * owners. A `type=rs_iface` node from the boot manifest's RevConfig carries no
 * uid of its own (component_id -1) and the group's pack hangs beneath it, so a
 * root-siblings-only scan dumps nothing on a config-built tree.
 */
static void
dump_tree_group_in(
    struct App* app,
    int32_t first,
    int group_id)
{
    for( int32_t i = first; i >= 0; i = app->tree->components[i].next_sibling )
    {
        struct UITreeComponent const* c = &app->tree->components[i];
        if( c->freed )
            continue;
        if( c->component_id >= 0 )
        {
            if( ((c->component_id >> 16) & 0xFFFF) == group_id )
                dump_tree_node(app, i, 0);
            continue;
        }
        dump_tree_group_in(app, c->first_child, group_id);
    }
}

static void
dump_tree(
    struct App* app,
    int group_id)
{
    if( !app->tree )
        return;
    dump_tree_group_in(app, app->tree->root_index, group_id);
}

/*
 * TORIRS_DUMP_ROOTS=1: the root sibling list in paint order.
 *
 * Root order is the boot manifest's RevConfig layout order, and it has to stay
 * that way for the life of the tree — a debug overlay declared after the
 * gameframe must still paint after it once the CS2 scripts have finished
 * rearranging the frame. That only holds because a cache pack is baked *under*
 * its `rs_iface` owner, so this list is the direct check on it: it should read
 * back exactly as the layout section declared, however many mounts and
 * reparents happened in between. Kept out of dump_tree so that stays
 * byte-comparable with the reference client's widgetTreeDump.
 */
static void
dump_roots(struct App* app)
{
    int n = 0;
    if( !app->tree )
        return;
    for( int32_t i = app->tree->root_index; i >= 0;
         i = app->tree->components[i].next_sibling )
    {
        struct UITreeComponent const* c = &app->tree->components[i];
        int children = 0;
        for( int32_t k = c->first_child; k >= 0; k = app->tree->components[k].next_sibling )
            children++;
        printf(
            "ROOT[%d] index=%d type=%d com=0x%08x (%d|%d) hide=%d children=%d%s\n",
            n++,
            (int)i,
            (int)c->type,
            (unsigned)c->component_id,
            (c->component_id >> 16) & 0xFFFF,
            c->component_id & 0xFFFF,
            c->behavior.hide,
            children,
            c->freed ? " freed" : "");
    }
}

static void
update_window_title(
    struct PlatformSDL2* sdl,
    struct App const* app,
    int interface_id)
{
    char title[160];

    snprintf(
        title,
        sizeof(title),
        "torirs iface=%d hover=%d clicked=%d",
        interface_id,
        app->hover_com_id,
        app->clicked_com_id);
    PlatformSDL2_SetTitle(sdl, title);
}

/* Headless sim frames must render like the real loop does: the world pickset
 * and hover tile refresh inside App_Render, so every RunOnce that reports a
 * redraw is followed by a render into a scratch canvas. */
static void
sim_render_frame(struct App* app)
{
    static int* sim_pixels = NULL;
    static size_t sim_pixel_count = 0;
    size_t const want = (size_t)UITREE_LAYOUT_ROOT_W * (size_t)UITREE_LAYOUT_ROOT_H;

    /* Reallocated on growth: the canvas is no longer fixed for the life of the
     * process, and App_Render writes exactly want ints. */
    if( !sim_pixels || sim_pixel_count < want )
    {
        int* grown = realloc(sim_pixels, want * sizeof(int));
        assert(grown);
        sim_pixels = grown;
        sim_pixel_count = want;
    }
    App_Render(app, sim_pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
}

/** Interactive present: Soft3D writes pixels then blits; GPU backends drain the
 * same retained frame and present it. Headless/BMP paths keep using App_Render. */
static void
interactive_render_present(
    struct App* app,
    struct PlatformSDL2* sdl,
    struct ToriRS_GL3* gl3,
    struct ToriRS_D3D9* d3d9)
{
    int const interface_scale_mode = RS_CS2Host_UiScaleMode(&app->host);

    /* Device option 15 is presentation state, just like option 27's canvas
     * size. Apply it immediately after the click that changed it and to every
     * renderer lane; each setter is a no-op while the value is unchanged. */
    PlatformSDL2_SetInterfaceScaleMode(sdl, interface_scale_mode);
#if defined(TORIRS_HAVE_D3D9)
    if( d3d9 )
    {
        struct ToriRS_Frame frame;
        int progress = 0;
        int pick_armed = 0;

        ToriRS_D3D9_SetInterfaceScaleMode(d3d9, interface_scale_mode);

        if( App_IsBooting(app, &progress) )
        {
            ToriRS_D3D9_DrawBootBar(d3d9, progress);
        }
        else if( App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
        {
            if( app->world_mouse_in_viewport )
            {
                ToriRS_D3D9_SetPick(d3d9, app->world_mouse_x, app->world_mouse_y);
                pick_armed = 1;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
            {
                ToriRS_D3D9_RenderFrame(d3d9, &frame);
            }
            if( getenv("TORIRS_FRAME_DEBUG") )
                fprintf(
                    stderr,
                    "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
                    frame.dbg_emit_element,
                    frame.dbg_emit_terrain,
                    frame.dbg_drop_not_live,
                    frame.dbg_drop_no_model);
            if( pick_armed )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PICK_FINISH)
                {
                    App_PickFinish(app, ToriRS_D3D9_PickHits(d3d9));
                }
            }
        }
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
            ToriRS_D3D9_Present(d3d9);
        }
        return;
    }
#else
    (void)d3d9;
#endif

#if defined(TORIRS_HAVE_GL3)
    if( gl3 )
    {
        struct ToriRS_Frame frame;
        int progress = 0;
        int pick_armed = 0;

        ToriRS_GL3_SetInterfaceScaleMode(gl3, interface_scale_mode);

        if( App_IsBooting(app, &progress) )
        {
            ToriRS_GL3_DrawBootBar(gl3, progress);
        }
        else if( App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
        {
            if( app->world_mouse_in_viewport )
            {
                ToriRS_GL3_SetPick(gl3, app->world_mouse_x, app->world_mouse_y);
                pick_armed = 1;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
            {
                ToriRS_GL3_RenderFrame(gl3, &frame);
            }
            if( getenv("TORIRS_FRAME_DEBUG") )
                fprintf(
                    stderr,
                    "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
                    frame.dbg_emit_element,
                    frame.dbg_emit_terrain,
                    frame.dbg_drop_not_live,
                    frame.dbg_drop_no_model);
            if( pick_armed )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PICK_FINISH)
                {
                    App_PickFinish(app, ToriRS_GL3_PickHits(gl3));
                }
            }
        }
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
            PlatformSDL2_PresentGL(sdl);
        }
        return;
    }
#else
    (void)gl3;
#endif

    App_Render(app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
    {
        PlatformSDL2_Present(sdl);
    }
}

/* App_RunOnce returned no frame commit.  The software surface can safely
 * re-upload its retained pixels (useful after a window expose), but swapping an
 * undrawn GL backbuffer can alternate to older contents and a D3D9 DISCARD
 * swapchain explicitly does not preserve its backbuffer.  Leave GPU front
 * buffers alone; the window/compositor retains the last committed frame. */
static void
interactive_present_retained(
    struct PlatformSDL2* sdl,
    struct ToriRS_GL3* gl3,
    struct ToriRS_D3D9* d3d9)
{
#if defined(TORIRS_HAVE_D3D9)
    if( d3d9 )
        return;
#else
    (void)d3d9;
#endif
#if defined(TORIRS_HAVE_GL3)
    if( gl3 )
        return;
#else
    (void)gl3;
#endif
    PlatformSDL2_Present(sdl);
}


/* --- the interactive frame loop -----------------------------------------
 *
 * The loop's state lives at file scope rather than in main's frame because the
 * browser host cannot keep it there: emscripten_set_main_loop unwinds the C
 * stack and then calls back one animation frame at a time, so anything main
 * held as a local would be gone by the first tick. There is exactly one frame
 * loop per process, so file scope costs nothing and says as much.
 *
 * frame_loop_step() is one iteration of what used to be the while body,
 * verbatim; it returns 0 when the loop should stop. Native spins it; the
 * browser hands it to requestAnimationFrame.
 */
#if defined(TORIRS_PLATFORM_WEB)
/* Is the page hidden? EM_JS rather than EM_ASM: the latter is rejected in
 * `-std=c*` modes, and this file is built as C11. */
EM_JS(int, web_document_hidden, (void), {
    return (typeof document !== 'undefined' && document.hidden) ? 1 : 0;
});

/* TORIRS_PERF=1 only (see torirs_perf.h): drops a User Timing mark plus a
 * console.warn at `label`, so a captured DevTools trace shows *why* a frame
 * ran long without the manual cross-referencing (WebSocketReceive bursts
 * against AnimationFrame durations, sample by sample) that a periodic
 * camera stutter took to trace back to frame_loop_step's own raf<->
 * settimeout(0) pacing flip, tripped by the 600ms server tick's burst of
 * small packets. All formatting happens in C; this just posts the string. */
EM_JS(void, web_mark_frame_event, (const char* label), {
    var s = UTF8ToString(label);
    if( typeof performance !== 'undefined' && performance.mark )
        performance.mark(s);
    console.warn('[torirs] ' + s);
});
#endif

static struct App app;
static struct ToriRS_ExecutorConfig executor_cfg;
static struct AppConfig cfg = {
    .cache_dir = NULL, /* resolved from cache_kind below */
    .config_dir = CONFIG_DIR,
    .script_dir = SCRIPT_DIR,
    .interface_id = DEFAULT_INTERFACE_ID,
    .cache_kind = APP_CACHE_DAT1,
    /* -1 = no manifest spawn; app_world_load_begin falls back to the client default. */
    .spawn_x = -1,
    .spawn_z = -1,
};

static struct PlatformSDL2* sdl;
static struct LibToriRS_Input input_storage;
static struct LibToriRS_Input* input;
/* The ring is 128KB and there is exactly one bus per process. */
static struct ToriRS_CmdBus bus;
static FILE* replay;
static uint64_t replay_now;
/* NULL unless the desktop-GL renderer was built AND --opengl3 was passed. */
static struct ToriRS_GL3* gl3;
/* NULL unless the Win32 fixed-function D3D9 renderer was selected. */
static struct ToriRS_D3D9* d3d9;
static struct PlatformAudio* audio;
static struct ToriRS_AudioCommand audio_commands[TORIRS_AUDIO_QUEUE_MAX];
static int sim_sound_id = -1;
static int sim_song_id = -1;
static int sim_jingle_id = -1;
static int sim_music_done;
static int sim_sound_loops = 1;
static int sim_sound_every;
static long sim_sound_next;
static long max_frames;
static long frame_count;
static struct NetTransport* sock;
static int sim_openmain = -1;
static int sim_openmain_done;
static int sim_openside = -1;
static int sim_openside_done;
static int sim_openchat = -1;
static int sim_openchat_done;
static int boot_stats;
static uint64_t boot_start_ms;
static int boot_reported;
static char const* sim_sethide;
static int sim_sethide_done;
static char const* sim_setvarp;
static char const* sim_settab;
static int sim_settab_done;
static int uncapped;
/* TORIRS_PACE_SPIN=1: burn the pacing wait instead of sleeping it. Profiling
 * aid for isolating wake-up cost from render cost; see docs/PERF_HARNESS.md. */
static int pace_spin;
/* Frame start of the previous loop iteration, for the `period` stage. */
static uint64_t prev_frame_start_us;
#if defined(TORIRS_PLATFORM_WEB)
/* Shortest raf-paced `period` seen -- a self-calibrating stand-in for "one
 * vsync" on web, where Emscripten never tells C the display's actual
 * refresh rate. See the TORIRS_PERF raf-miss check in frame_loop_step.
 * Only raf-mode periods may feed it: a settimeout(0) boot iteration runs
 * ~1ms after its predecessor, and one such sample as the baseline would
 * flag every normal 8ms frame after boot as a miss. */
static uint64_t raf_baseline_us;
/* The (mode, value) pair currently installed via
 * emscripten_set_main_loop_timing; -1 until the first install. File scope
 * so the period instrumentation above can tell raf frames from timer
 * frames -- there is exactly one frame loop per process. */
static int paced_mode = -1;
static int paced_value = -1;
#endif
/* Retain gesture/key one-shots while App_RunOnce is holding the last committed
 * visual frame. They are cleared only after a stable tree reaches interaction. */
static int input_frame_pending;

/** One iteration of the frame loop. Returns 0 when the client should stop. */
static int
frame_loop_step(void)
{
#if defined(TORIRS_PLATFORM_WEB)
    /* Carry last frame's queued cache reads to the IO server and take delivery
     * of whatever came back. Nothing else in the process runs every frame, and
     * a request nobody carries parks the task queue forever. */
    PlatformXIO_Web_Pump();

    /*
     * Let the boot block on its reads, and never let the live client.
     *
     * A blocking read returns inside the frame that asked for it, which is what
     * keeps the boot's serial chain of archives from costing an event-loop turn
     * apiece. But it freezes the main thread for longer than the request takes,
     * and past APP_STATE_READY the reads that remain are precisely the ones
     * that coincide with something new on screen — the first play of an npc's
     * hit sound is a fetch on the frame its hitsplat is drawn, and that reads
     * as the hitsplat being slow. After READY the read is queued instead and
     * the pacing below drains it at event-loop rate.
     */
    PlatformXIO_Web_SetBlockingReads(app.app_state != APP_STATE_READY);

    /* Pace the loop by what it is waiting for.
     *
     * A task pipeline is serial: it issues one read, parks, and cannot resume
     * until the answer lands, so a frame consumes at most one round trip per
     * pipeline. At display rate that caps the client at ~120 archives a second
     * while its 20ms logic ticks keep queueing more work — and on a boot that
     * reads several hundred archives the queue grows faster than it drains.
     *
     * Logic ticks are driven by the wall clock, not by the loop, so running the
     * loop from the event loop instead of the display drains the backlog
     * without producing more of it. Back to requestAnimationFrame the moment
     * nothing is outstanding, so a settled client renders on frame boundaries
     * like any other page. */
    {
        /*
         * Three regimes share one setting, so they are decided together:
         *
         *   boot backlog settimeout(0)  — drain as fast as the event loop will
         *   hidden tab   settimeout(50) — keep the socket drained, don't draw
         *   visible play raf(1)         — a normal page on frame boundaries
         *
         * The backlog arm is gated to pre-READY on purpose, and the gate is a
         * jank fix, not thrift. Past READY the reads that remain are one or
         * two chain links deep — a server tick reveals an npc whose model is
         * not resident — and an async response only needs the event loop to
         * turn, which every animation-frame boundary already does; the read
         * resolves a frame later either way. Leaving raf for it cost two
         * callbacks run off vsync plus a raf re-registration that landed
         * mid-cycle: a 13-16ms presentation interval against an 8ms cadence,
         * once per 600ms server tick, visible as a periodic stutter whenever
         * the camera was moving. The backlog this arm exists for — hundreds
         * of serially-discovered boot archives — cannot recur once READY:
         * app_state regresses only on a full gameframe re-root, which gets
         * the fast drain back along with its loading screen, and world/region
         * streaming (Task_WorldLoad) stays READY and rides raf. If a region
         * load's settle rate ever matters, drain it from the IO response
         * callback instead of re-pacing the frame loop.
         *
         * The hidden case is the other one worth explaining. A browser stops
         * calling requestAnimationFrame for a hidden tab, so the client stops
         * draining a socket the server keeps writing to; minutes later the
         * tab comes back to a backlog it can only fast-forward through.
         * Timers keep firing where animation frames do not — clamped to
         * about 1Hz in the background, which is still several times the
         * 600ms server tick, so a hidden tab keeps up instead of falling
         * behind. Asking for 50ms costs nothing when the clamp is the thing
         * that decides.
         *
         * This is not a guarantee: a browser that freezes the page entirely,
         * or an OS that suspends it, stops timers too. That case is what
         * app_net_link_watch is for — it notices the gap and drops the
         * session rather than replaying it.
         */
        int waiting = app.app_state != APP_STATE_READY
                      && PlatformXIO_Web_PendingTotal() > 0;
        int hidden = !waiting && web_document_hidden();
        int mode = (waiting || hidden) ? EM_TIMING_SETTIMEOUT : EM_TIMING_RAF;
        int value = waiting ? 0 : (hidden ? 50 : 1);

        if( mode != paced_mode || value != paced_value )
        {
            if( g_torirs_perf_enabled )
            {
                char label[96];
                snprintf(
                    label,
                    sizeof label,
                    "torirs-pace %s(%d)->%s(%d) frame=%ld",
                    paced_mode < 0 ? "init"
                    : paced_mode == EM_TIMING_RAF ? "raf"
                                                   : "settimeout",
                    paced_value,
                    mode == EM_TIMING_RAF ? "raf" : "settimeout",
                    value,
                    frame_count);
                web_mark_frame_event(label);
            }
            paced_mode = mode;
            paced_value = value;
            emscripten_set_main_loop_timing(mode, value);
        }
    }
#endif
    if( PlatformSDL2_QuitRequested(sdl) )
        return 0;

    uint64_t now;
    int app_redraw;
    uint64_t frame_start_us;
#if !defined(__EMSCRIPTEN__)
    uint64_t frame_start_ms;
#endif

    if( max_frames > 0 && frame_count++ >= max_frames )
        return 0;

#if !defined(__EMSCRIPTEN__)
    /* The pacing budget starts before any frame work. The input timestamp
     * below is intentionally separate: using it as the origin omitted the
     * pre-poll work and made nominal 20 ms frames longer than 20 ms. */
    frame_start_ms = PlatformSDL2_Ticks64();
#endif
    /* The developer overlay's readout (App_NoteFrameTime), which measures the
     * same interval the perf harness calls a frame — and, like it, is closed
     * before the pacing sleep so the number is work and not the cap. Sampled
     * on every platform: the browser lane has no sleep to exclude but has the
     * same question to answer. */
    frame_start_us = PlatformSDL2_TicksUs();
    /* Carry the wall gap since the previous frame start, then open the frame:
     * FRAME_BEGIN moves the carry into this frame's bucket. Work and pace each
     * miss part of the loop, so only this is the period the player sees. */
    if( prev_frame_start_us != 0 && frame_start_us > prev_frame_start_us )
    {
        uint64_t period_us = frame_start_us - prev_frame_start_us;
        TORIRS_PERF_CARRY(TORIRS_PERF_STAGE_PERIOD, period_us * 1000u);
#if defined(TORIRS_PLATFORM_WEB)
        /* raf_baseline_us tracks the shortest raf-paced period seen as a
         * proxy for "one vsync". A period 50% past that baseline means at
         * least one requestAnimationFrame callback was skipped -- mark it
         * rather than leaving the next investigation to reconstruct it from
         * a raw trace. Timer-paced frames (boot drain, hidden tab) are
         * excluded from both sides of the check, and the 4ms floor (240Hz)
         * keeps a scheduling fluke on a raf frame from becoming a baseline
         * every honest frame would then appear to miss. A period spanning a
         * mode flip is judged by the mode just installed above -- close
         * enough for instrumentation, and the flip logs its own marker. */
        if( g_torirs_perf_enabled && paced_mode == EM_TIMING_RAF )
        {
            if( period_us >= 4000
                && (raf_baseline_us == 0 || period_us < raf_baseline_us) )
                raf_baseline_us = period_us;
            else if( raf_baseline_us != 0
                     && period_us > raf_baseline_us + raf_baseline_us / 2 )
            {
                char label[96];
                snprintf(
                    label,
                    sizeof label,
                    "torirs-raf-miss period=%.1fms baseline=%.1fms frame=%ld",
                    (double)period_us / 1000.0,
                    (double)raf_baseline_us / 1000.0,
                    frame_count);
                web_mark_frame_event(label);
            }
        }
#endif
    }
    prev_frame_start_us = frame_start_us;
    TORIRS_PERF_FRAME_BEGIN();

    /* TORIRS_BMP_SERIES=dir,start,step,count: write a numbered App_Render frame
     * every `step` loop iterations from `start` on — a film strip of a live
     * sequence. Single frames (TORIRS_EXIT_BMP) cannot catch a two-tick
     * animation whose start jitters with login time; a strip through the whole
     * window can. */
    {
        static char series_dir[512];
        static long series_start = -1, series_step = 1, series_count = 0, series_written = 0;
        static int series_parsed = 0;
        if( !series_parsed )
        {
            char const* env = getenv("TORIRS_BMP_SERIES");
            series_parsed = 1;
            if( env )
                sscanf(
                    env,
                    "%511[^,],%ld,%ld,%ld",
                    series_dir,
                    &series_start,
                    &series_step,
                    &series_count);
        }
        if( App_FrameSettled(&app) && series_start >= 0 && series_written < series_count &&
            frame_count >= series_start &&
            (frame_count - series_start) % (series_step > 0 ? series_step : 1) == 0 )
        {
            int* pixels = calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
            assert(pixels);
            char path[600];
            if( getenv("TORIRS_ANIM_DEBUG") )
                fprintf(stderr, "bmp_series: frame_count=%ld\n", frame_count);
            extern int g_torirs_painter_force;
            /* TORIRS_PAINTER_ALT=1: write frame_N.bmp painted by world3d AND
             * frame_N_bucket.bmp painted by the bucket painter, from the SAME
             * frame — same scene, same camera, same animation phase, so a
             * pixel diff is the painter alone. Without it, frame_N.bmp is the
             * default painter. */
            int alt = getenv("TORIRS_PAINTER_ALT") != NULL;
            g_torirs_painter_force = alt ? 1 : 0;
            App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            snprintf(path, sizeof(path), "%s/frame_%05ld.bmp", series_dir, frame_count);
            bmp_write_file(path, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            if( alt )
            {
                char path_b[600];
                g_torirs_painter_force = 2;
                App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
                snprintf(path_b, sizeof(path_b), "%s/frame_%05ld_bucket.bmp", series_dir, frame_count);
                bmp_write_file(path_b, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            }
            g_torirs_painter_force = 0;
            free(pixels);
            series_written++;
        }
    }

    if( boot_stats && !boot_reported && app.app_state == APP_STATE_READY )
    {
        boot_reported = 1;
        fprintf(
            stderr,
            "boot: %llums  frames=%d steps=%ld capped=%d\n",
            (unsigned long long)(PlatformSDL2_Ticks64() - boot_start_ms),
            app.boot_frames,
            app.boot_steps,
            app.boot_frames_budget_capped);
    }
    if( boot_stats && frame_count == max_frames - 1 )
        fprintf(
            stderr,
            "post-boot: busy_frames=%d busy_steps=%ld (frames that used the "
            "whole budget with work still queued)\n",
            app.busy_frames,
            app.busy_steps);

    if( sim_openmain > 0 && !sim_openmain_done && app.app_state == APP_STATE_READY )
    {
        fprintf(stderr, "sim_openmain: opening main modal iface=%d\n", sim_openmain);
        RS_UISlots_OpenMain(&app, sim_openmain);
        sim_openmain_done = 1;
    }

    if( sim_openside > 0 && !sim_openside_done && app.app_state == APP_STATE_READY )
    {
        fprintf(stderr, "sim_openside: opening side panel iface=%d\n", sim_openside);
        RS_UISlots_OpenSide(&app, sim_openside);
        sim_openside_done = 1;
    }

    if( sim_openchat > 0 && !sim_openchat_done && app.app_state == APP_STATE_READY )
    {
        fprintf(stderr, "sim_openchat: opening chat dialog iface=%d\n", sim_openchat);
        RS_UISlots_OpenChat(&app, sim_openchat);
        sim_openchat_done = 1;
    }

    if( sim_settab && !sim_settab_done && app.app_state == APP_STATE_READY )
    {
        char* tab_sep = NULL;
        int tabno = (int)strtol(sim_settab, &tab_sep, 0);
        int tab_iface =
            tab_sep && *tab_sep == ':' ? (int)strtol(tab_sep + 1, NULL, 0) : -1;
        fprintf(stderr, "sim_settab: tab=%d iface=%d\n", tabno, tab_iface);
        RS_UISlots_SetTab(&app, tabno, tab_iface);
        RS_UISlots_SetSideTab(&app, tabno);
        sim_settab_done = 1;
    }

    if( sim_setvarp && app.app_state == APP_STATE_READY )
    {
        char const* cur = sim_setvarp;
        while( *cur )
        {
            char* sep = NULL;
            long varp = strtol(cur, &sep, 0);
            long value = sep && *sep == ':' ? strtol(sep + 1, &sep, 0) : 0;
            VarPManager_SetVarpOptimistic(&app.varps, (int)varp, (int)value);
            while( sep && *sep && *sep != ',' )
                sep++;
            cur = sep && *sep == ',' ? sep + 1 : "";
        }
    }

    /* TORIRS_SIM_SETHIDE="com:0|1,...": replay IF_SETHIDE offline. The
     * chat dialogs ship both a narrow and a wide decoration layer and
     * the server picks one, so without this there is no way to see the
     * unhidden variant without a live session. */
    if( sim_sethide && !sim_sethide_done && app.app_state == APP_STATE_READY && app.tree &&
        (sim_openchat <= 0 || sim_openchat_done) && (!sim_settab || sim_settab_done) )
    {
        char const* cur = sim_sethide;
        while( *cur )
        {
            char* sep = NULL;
            long com = strtol(cur, &sep, 0);
            int hide = sep && *sep == ':' ? (int)strtol(sep + 1, &sep, 0) : 1;
            fprintf(stderr, "sim_sethide: com=%ld hide=%d\n", com, hide);
            App_IfHideSet(&app, (int)com, hide);
            while( sep && *sep && *sep != ',' )
                sep++;
            cur = sep && *sep == ',' ? sep + 1 : "";
        }
        App_RefreshAfterTreeMutation(&app);
        sim_sethide_done = 1;
    }

    if( (sim_song_id >= 0 || sim_jingle_id >= 0) && app.app_state == APP_STATE_READY &&
        !sim_music_done )
    {
        if( sim_song_id >= 0 )
        {
            fprintf(stderr, "sim_music: playing track %d\n", sim_song_id);
            App_PlaySong(&app, sim_song_id, true, 0, 0);
        }
        if( sim_jingle_id >= 0 )
        {
            fprintf(stderr, "sim_music: playing jingle %d\n", sim_jingle_id);
            App_PlayJingle(&app, sim_jingle_id, 0);
        }
        sim_music_done = 1;
    }

    /* TORIRS_SIM_SOUND=id[,loops[,every_ticks]]: queue a sound effect
     * once the client is up, and again every `every_ticks` ticks. The
     * only way to hear the audio path without a server, so it is the
     * check that "the sound plays" means a speaker and not a counter. */
    if( sim_sound_id >= 0 && app.app_state == APP_STATE_READY )
    {
        if( sim_sound_next == 0 || (sim_sound_every > 0 && frame_count >= sim_sound_next) )
        {
            fprintf(
                stderr,
                "sim_sound: queueing effect %d loops=%d\n",
                sim_sound_id,
                sim_sound_loops);
            App_PlaySound(&app, sim_sound_id, sim_sound_loops, 0);
            sim_sound_next = sim_sound_every > 0 ? frame_count + sim_sound_every : -1;
        }
    }

    if( replay )
    {
        if( !CmdReplay_PumpFrame(replay, &bus, &replay_now) )
            return 0; /* recording exhausted */
        now = replay_now;
    }
    else
    {
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_INPUT_PREP)
        {
            now = PlatformSDL2_Ticks64();
            CmdBus_PushFrame(&bus, now);
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PLATFORM_POLL)
            {
                PlatformSDL2_PollCommands(sdl, &bus);
                if( sock )
                    NetTransport_Poll(sock, app.net, &bus);
            }

        /* TORIRS_SIM_DRAG="frame,x0,y0,x1,y1[,repeats[,button]]": press at
         * (x0,y0), move to (x1,y1) over 20 frames, release, and repeat
         * `repeats` times (default 1). The only way to exercise a drag
         * headlessly — SIM_CLICK_AT presses and releases in the same
         * place, which no drag handler reacts to — and the repeat is
         * what keeps a pan going long enough to show what a client does
         * when the view never settles. `button` is the LibToriRS_MouseButton
         * code (1 left, 2 middle, 3 right) and defaults to left; middle is
         * what drives the viewport's camera rotate. */
        {
            static long drag_frame = -2;
            static long drag_x0, drag_y0, drag_x1, drag_y1;
            static long drag_repeats = 1;
            static long drag_button = 1;
            if( drag_frame == -2 )
            {
                char const* spec = getenv("TORIRS_SIM_DRAG");
                drag_frame = -1;
                if( spec && *spec )
                {
                    char* end = NULL;
                    long values[7];
                    int count = 0;
                    values[count++] = strtol(spec, &end, 0);
                    while( count < 7 && end && *end == ',' )
                        values[count++] = strtol(end + 1, &end, 0);
                    if( count >= 5 )
                    {
                        drag_frame = values[0];
                        drag_x0 = values[1];
                        drag_y0 = values[2];
                        drag_x1 = values[3];
                        drag_y1 = values[4];
                        drag_repeats = count > 5 && values[5] > 0 ? values[5] : 1;
                        drag_button = count > 6 && values[6] > 0 ? values[6] : 1;
                    }
                }
            }
            if( drag_frame >= 0 && frame_count >= drag_frame )
            {
                long step = frame_count - drag_frame;
                long const steps = 20;
                if( step == 0 )
                    CmdBus_PushMouseMove(&bus, (int)drag_x0, (int)drag_y0);
                else if( step == 2 )
                    CmdBus_PushMouseButton(
                        &bus,
                        TORIRS_CMD_INPUT_MOUSE_DOWN,
                        (uint8_t)drag_button,
                        (int)drag_x0,
                        (int)drag_y0);
                else if( step > 2 && step <= 2 + steps )
                {
                    long i = step - 2;
                    int x = (int)(drag_x0 + (drag_x1 - drag_x0) * i / steps);
                    int y = (int)(drag_y0 + (drag_y1 - drag_y0) * i / steps);
                    CmdBus_PushMouseMove(&bus, x, y);
                }
                else if( step == 3 + steps )
                {
                    CmdBus_PushMouseButton(
                        &bus,
                        TORIRS_CMD_INPUT_MOUSE_UP,
                        (uint8_t)drag_button,
                        (int)drag_x1,
                        (int)drag_y1);
                    fprintf(
                        stderr,
                        "sim_drag: %ld,%ld -> %ld,%ld button=%ld (%ld left)\n",
                        drag_x0, drag_y0, drag_x1, drag_y1, drag_button, drag_repeats - 1);
                    if( --drag_repeats > 0 )
                    {
                        /* Alternate direction each repeat: panning one
                         * way clamps at the area edge after a couple of
                         * drags and the view stops moving, which is not
                         * the "never settles" case worth testing. */
                        long swap_x = drag_x0;
                        long swap_y = drag_y0;
                        drag_x0 = drag_x1;
                        drag_y0 = drag_y1;
                        drag_x1 = swap_x;
                        drag_y1 = swap_y;
                        drag_frame = frame_count + 2;
                    }
                    else
                        drag_frame = -1;
                }
            }
        }

        /* TORIRS_SIM_WHEEL="frame,x,y,notches[,repeats]": park the pointer
         * at (x,y) and turn the wheel `notches` (positive = up / toward the
         * screen, which zooms the viewport in) once per frame for `repeats`
         * frames. Wheel events carry no position of their own, so the move
         * has to land first — same reason SIM_CLICK_AT moves ahead of its
         * press. */
        {
            static long wheel_frame = -2;
            static long wheel_x, wheel_y, wheel_notches;
            static long wheel_repeats = 1;
            if( wheel_frame == -2 )
            {
                char const* spec = getenv("TORIRS_SIM_WHEEL");
                wheel_frame = -1;
                if( spec && *spec )
                {
                    char* end = NULL;
                    long values[5];
                    int count = 0;
                    values[count++] = strtol(spec, &end, 0);
                    while( count < 5 && end && *end == ',' )
                        values[count++] = strtol(end + 1, &end, 0);
                    if( count >= 4 )
                    {
                        wheel_frame = values[0];
                        wheel_x = values[1];
                        wheel_y = values[2];
                        wheel_notches = values[3];
                        wheel_repeats = count > 4 && values[4] > 0 ? values[4] : 1;
                    }
                }
            }
            if( wheel_frame >= 0 && frame_count >= wheel_frame )
            {
                long step = frame_count - wheel_frame;
                if( step == 0 )
                    CmdBus_PushMouseMove(&bus, (int)wheel_x, (int)wheel_y);
                else if( step <= 2 + wheel_repeats && step > 2 )
                {
                    CmdBus_PushMouseWheel(&bus, (int16_t)wheel_notches);
                    if( step == 2 + wheel_repeats )
                    {
                        fprintf(
                            stderr,
                            "sim_wheel: %ld,%ld notches=%ld x%ld\n",
                            wheel_x, wheel_y, wheel_notches, wheel_repeats);
                        wheel_frame = -1;
                    }
                }
            }
        }

        /* TORIRS_SIM_HOOK="frame,com[;frame,com...]": dispatch a component's
         * onop (falling back to onclick) hook at that main-loop frame.
         *
         * The in-loop twin of the pre-loop TORIRS_SIM_CLICK, and it exists for
         * the same reason TORIRS_SIM_CLICK_AT does: the pre-loop block runs
         * before login completes, so it cannot reach anything the *server*
         * mounted — which at rev 230 is every side panel. Unlike SIM_CLICK_AT
         * this needs no coordinates and no visibility, so it can drive a
         * button on a panel whose tab is not selected. */
        {
            static char const* hook_cursor = NULL;
            static int hook_init = 0;
            static long hook_frame = -1;
            static long hook_com = 0;
            if( !hook_init )
            {
                hook_init = 1;
                hook_cursor = getenv("TORIRS_SIM_HOOK");
            }
            if( hook_frame < 0 && hook_cursor && *hook_cursor )
            {
                char* end = NULL;
                hook_frame = strtol(hook_cursor, &end, 0);
                if( end && *end == ',' )
                {
                    hook_com = strtol(end + 1, &end, 0);
                    hook_cursor = (end && *end == ';') ? end + 1 : NULL;
                }
                else
                {
                    hook_cursor = NULL;
                    hook_frame = -1;
                }
            }
            if( hook_frame >= 0 && frame_count >= hook_frame && app.tree )
            {
                int32_t idx = UITree_FindByComponentId(app.tree, (int)hook_com);

                if( idx >= 0 )
                {
                    struct UITreeRuntimeScriptHook hook =
                        UITree_Hooks(&app.tree->components[idx])->on_op;
                    if( hook.script_id <= 0 )
                        hook = UITree_Hooks(&app.tree->components[idx])->on_click;
                    fprintf(stderr, "sim_hook: com=0x%lx script=%d\n", hook_com, hook.script_id);
                    /* A real op click latches which op it was; an onop script
                     * that switches on event_opindex (every list row does) is
                     * a no-op without it. 1 = the primary left-click op. */
                    app.host.event_op_index = 1;
                    RS_CS2_DispatchHook(&app.host, &app.runner, (int)hook_com, &hook);
                }
                else
                    fprintf(stderr, "sim_hook: component 0x%lx not found\n", hook_com);
                hook_frame = -1;
            }
        }

        /* TORIRS_SIM_OPLOC="frame,op,x,z,loc": send one normal object-menu
         * operation once the mock session is live. Unlike a server diagnostic,
         * this traverses the client's net_out_oploc encoder and the server's
         * regular OPLOC route. All values stay in the invoking test, not C. */
        {
            static int sim_oploc_init = 0;
            static long sim_oploc_frame = -1;
            static long sim_oploc_op;
            static long sim_oploc_x;
            static long sim_oploc_z;
            static long sim_oploc_id;
            if( !sim_oploc_init )
            {
                char const* spec = getenv("TORIRS_SIM_OPLOC");
                char* end = NULL;
                sim_oploc_init = 1;
                if( spec && *spec )
                {
                    sim_oploc_frame = strtol(spec, &end, 0);
                    if( end && *end == ',' )
                        sim_oploc_op = strtol(end + 1, &end, 0);
                    if( end && *end == ',' )
                        sim_oploc_x = strtol(end + 1, &end, 0);
                    if( end && *end == ',' )
                        sim_oploc_z = strtol(end + 1, &end, 0);
                    if( end && *end == ',' )
                        sim_oploc_id = strtol(end + 1, &end, 0);
                    else
                        sim_oploc_frame = -1;
                }
            }
            if( sim_oploc_frame >= 0 && frame_count >= sim_oploc_frame )
            {
                fprintf(
                    stderr,
                    "sim_oploc: op=%ld tile=%ld,%ld loc=%ld\n",
                    sim_oploc_op,
                    sim_oploc_x,
                    sim_oploc_z,
                    sim_oploc_id);
                App_SimulateLocOp(
                    &app,
                    (int)sim_oploc_op,
                    (int)sim_oploc_x,
                    (int)sim_oploc_z,
                    (int)sim_oploc_id);
                sim_oploc_frame = -1;
            }
        }

        /* TORIRS_SIM_OPNPC="frame,op,npc": the npc counterpart of the above.
         * The npc is named by cache type, not by server slot — see
         * App_SimulateNpcOp for why that is the only stable handle a test has.
         * Same route as a world click: net_out_opnpc, then the server's
         * ordinary OPNPC trigger dispatch. */
        {
            static int sim_opnpc_init = 0;
            static long sim_opnpc_frame = -1;
            static long sim_opnpc_op;
            static long sim_opnpc_npc;
            if( !sim_opnpc_init )
            {
                char const* spec = getenv("TORIRS_SIM_OPNPC");
                char* end = NULL;
                sim_opnpc_init = 1;
                if( spec && *spec )
                {
                    sim_opnpc_frame = strtol(spec, &end, 0);
                    if( end && *end == ',' )
                        sim_opnpc_op = strtol(end + 1, &end, 0);
                    if( end && *end == ',' )
                        sim_opnpc_npc = strtol(end + 1, &end, 0);
                    else
                        sim_opnpc_frame = -1;
                }
            }
            if( sim_opnpc_frame >= 0 && frame_count >= sim_opnpc_frame )
            {
                int slot = App_SimulateNpcOp(&app, (int)sim_opnpc_op, (int)sim_opnpc_npc);

                fprintf(
                    stderr,
                    "sim_opnpc: op=%ld npc=%ld slot=%d\n",
                    sim_opnpc_op,
                    sim_opnpc_npc,
                    slot);
                /* Retry on the next frame while the npc has not arrived yet:
                 * the caller picks a frame, the server picks the tick its spawn
                 * lands on, and a one-shot would race that.
                 *
                 * TORIRS_SIM_OPNPC_EVERY=N re-issues the op every N frames
                 * after it first lands — a target that walks away from the
                 * player (a Nylocas Matomenos heading for the Maiden) is never
                 * reached by a single click, and "the player chases it and
                 * kills it" is the scenario a death-animation probe needs. */
                if( slot >= 0 )
                {
                    static long every = -1;
                    if( every < 0 )
                    {
                        char const* e = getenv("TORIRS_SIM_OPNPC_EVERY");
                        every = (e && *e) ? strtol(e, NULL, 0) : 0;
                    }
                    sim_opnpc_frame = every > 0 ? frame_count + every : -1;
                }
            }
        }

        /* TORIRS_SIM_RUNSCRIPT="frame,script[,arg0[,arg1...]][;frame,...]":
         * run a clientscript by id at that main-loop frame, with up to four
         * int args.
         *
         * TORIRS_SIM_HOOK covers "click this component", which is the right
         * harness whenever the component exists and its binding is in the
         * tree. This covers the rest: a script reached through a dropdown or a
         * menu whose component is a chore to address, and a script with no
         * binder at all. Same shape as the RUNCLIENTSCRIPT packet path, which
         * is also "run this id with these ints, no component".
         *
         * `3967,12,<mode>` is [clientscript,settings_set_dropdown] on the
         * Display panel's layout row — the case-12 arm that calls 3998 and so
         * the whole Fixed/Classic/Modern remount, from the content's own entry
         * point rather than from 3998 forced by hand. It only reaches 3998 in a
         * cache baked from the tree (docs/gameframe_layout_resize.md §8.3);
         * pristine cache.osrs239 has the arm missing and nothing happens. */
        {
            static char const* rs_cursor = NULL;
            static int rs_init = 0;
            static long rs_frame = -1;
            static long rs_script = 0;
            static int rs_argc = 0;
            static int rs_argv[4];
            if( !rs_init )
            {
                rs_init = 1;
                rs_cursor = getenv("TORIRS_SIM_RUNSCRIPT");
            }
            if( rs_frame < 0 && rs_cursor && *rs_cursor )
            {
                char* end = NULL;
                rs_frame = strtol(rs_cursor, &end, 0);
                rs_argc = 0;
                if( end && *end == ',' )
                {
                    rs_script = strtol(end + 1, &end, 0);
                    while( rs_argc < 4 && end && *end == ',' )
                        rs_argv[rs_argc++] = (int)strtol(end + 1, &end, 0);
                    rs_cursor = (end && *end == ';') ? end + 1 : NULL;
                }
                else
                {
                    rs_cursor = NULL;
                    rs_frame = -1;
                }
            }
            /* TORIRS_CS2_HARNESS=<cases.json>: run the cross-client case list
             * once the client is far enough in to have a cache, a host and a
             * runner, then leave. TORIRS_CS2_HARNESS_FRAME picks how far in;
             * the default is late enough for login to have completed against
             * mock230, because a case that reads a varp needs the varps.
             * The run ends the way every other headless run here ends, with
             * TORIRS_MAX_FRAMES — the harness does not invent a second exit
             * path. */
            {
                static int harness_done = 0;
                char const* harness_cases = getenv("TORIRS_CS2_HARNESS");
                if( !harness_done && harness_cases && *harness_cases )
                {
                    char const* at = getenv("TORIRS_CS2_HARNESS_FRAME");
                    long harness_frame = at ? strtol(at, NULL, 0) : 400;
                    if( frame_count >= harness_frame )
                    {
                        char const* out = getenv("TORIRS_CS2_HARNESS_OUT");
                        harness_done = 1;
                        CS2Harness_Run(
                            &app.host,
                            &app.runner,
                            harness_cases,
                            out && *out ? out : "/tmp/cs2_harness_c",
                            harness_shot,
                            &app);
                    }
                }
            }

            if( rs_frame >= 0 && frame_count >= rs_frame )
            {
                fprintf(
                    stderr, "sim_runscript: script=%ld argc=%d\n", rs_script, rs_argc);
                RS_CS2_RunScript(
                    &app.host,
                    &app.runner,
                    (int)rs_script,
                    rs_argc > 0 ? rs_argv : NULL,
                    rs_argc,
                    0,
                    NULL,
                    0);
                rs_frame = -1;
            }
        }

        /* TORIRS_SIM_TYPE="frame,c97,c108,k84": push key events at consecutive
         * main-loop frames starting at `frame`. Same grammar as the pre-loop
         * TORIRS_SIM_KEYS (c<character>, k<OSRS key code>), in-loop for the
         * same reason as TORIRS_SIM_HOOK above — a text prompt a *script*
         * opened does not exist until the panel that opens it is mounted. */
        {
            static char const* type_cursor = NULL;
            static int type_init = 0;
            static long type_frame = -1;
            if( !type_init )
            {
                type_init = 1;
                type_cursor = getenv("TORIRS_SIM_TYPE");
            }
            /* Start (or restart, at a ';') a burst: read its frame number. */
            if( type_frame < 0 && type_cursor && *type_cursor )
            {
                char* end = NULL;
                type_frame = strtol(type_cursor, &end, 0);
                type_cursor = (end && *end == ',') ? end + 1 : NULL;
                if( !type_cursor )
                    type_frame = -1;
            }
            if( type_frame >= 0 && type_cursor && *type_cursor && frame_count >= type_frame )
            {
                char kind = *type_cursor++;
                char* end = NULL;
                long val = strtol(type_cursor, &end, 0);

                if( kind == 'c' )
                    CmdBus_PushKeyEvent(&bus, -1, (int32_t)val, 0);
                else
                    CmdBus_PushKeyEvent(&bus, (int32_t)val, 0, 0);
                fprintf(stderr, "sim_type: %c%ld at frame %ld\n", kind, val, frame_count);

                if( end && *end == ',' )
                {
                    type_cursor = end + 1;
                    type_frame = frame_count + 1;
                }
                else if( end && *end == ';' )
                {
                    type_cursor = end + 1;
                    type_frame = -1; /* next burst names its own frame */
                }
                else
                {
                    type_cursor = NULL;
                    type_frame = -1;
                }
            }
        }

        /* TORIRS_SIM_HOTKEY="frame,<key>[;frame,<key>...]": press a named key
         * at that main-loop frame and release it two frames later. Key names
         * are the revconfig [hotkey:…] spelling (f1, 3, escape — see
         * LibToriRS_OsrsKeyFromName).
         *
         * Separate from the pre-loop TORIRS_SIM_KEYS block because that one
         * runs before the frame loop and behind App_BootWait; a binding is only
         * live once the tree is baked and App_Interact is running. Drives the
         * OSRS-coded key arrays, which is what both revconfig hotkeys and CS2
         * KEYPRESSED read. */
        {
            static char const* hk_cursor = NULL;
            static int hk_init = 0;
            static long hk_frame = -1;
            static int hk_key = -1;
            /* Platform-neutral code for the same key when it has one (letters
             * and digits). A real press fills both arrays, and the debug world
             * hotkeys read this one — pressing only the OSRS side would make a
             * hotkey/spawn-key collision untestable. */
            static enum LibToriRS_KeyCode hk_plain = TORIRSK_UNKNOWN;
            if( !hk_init )
            {
                hk_init = 1;
                hk_cursor = getenv("TORIRS_SIM_HOTKEY");
            }
            if( hk_frame < 0 && hk_cursor && *hk_cursor )
            {
                char* end = NULL;
                char name[64] = { 0 };
                long at = strtol(hk_cursor, &end, 0);
                if( end && *end == ',' )
                {
                    char const* start = end + 1;
                    size_t len = 0;
                    while( start[len] && start[len] != ';' && len < sizeof(name) - 1 )
                        len++;
                    memcpy(name, start, len);
                    hk_cursor = start[len] == ';' ? start + len + 1 : NULL;
                    hk_frame = at;
                    hk_key = LibToriRS_OsrsKeyFromName(name);
                    hk_plain = TORIRSK_UNKNOWN;
                    if( name[0] && !name[1] )
                    {
                        if( name[0] >= 'a' && name[0] <= 'z' )
                            hk_plain = (enum LibToriRS_KeyCode)(TORIRSK_A + (name[0] - 'a'));
                        else if( name[0] >= '0' && name[0] <= '9' )
                            hk_plain = (enum LibToriRS_KeyCode)(TORIRSK_0 + (name[0] - '0'));
                    }
                    fprintf(stderr, "sim_hotkey: '%s' -> osrs_key=%d at frame %ld\n",
                            name, hk_key, hk_frame);
                }
                else
                    hk_cursor = NULL;
            }
            if( hk_frame >= 0 && hk_key >= 0 && frame_count >= hk_frame )
            {
                if( frame_count == hk_frame )
                {
                    CmdBus_PushOsrsKey(&bus, (int16_t)hk_key, 1, 1);
                    if( hk_plain != TORIRSK_UNKNOWN )
                        CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_DOWN, (uint8_t)hk_plain);
                }
                else if( frame_count >= hk_frame + 2 )
                {
                    CmdBus_PushOsrsKey(&bus, (int16_t)hk_key, 0, 0);
                    if( hk_plain != TORIRSK_UNKNOWN )
                        CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_UP, (uint8_t)hk_plain);
                    hk_frame = -1;
                    hk_key = -1;
                    hk_plain = TORIRSK_UNKNOWN;
                }
            }
        }

        /* TORIRS_SIM_CMD="frame,text[;frame,text...]": send a `::` command at
         * the given main-loop frame.
         *
         * A content lane's debug procs are the only entry to an encounter that
         * no click can reach — the QBD arena is behind `[debugproc,rs2012qbd]`
         * — and a headless run has no chatbox to type into. The frame number
         * matters: the command is a server script call, so it has to land after
         * login, which SIM_CLICK_AT's own comment explains at length. */
        {
            static char const* cmd_cursor = NULL;
            static int cmd_init = 0;
            if( !cmd_init )
            {
                cmd_init = 1;
                cmd_cursor = getenv("TORIRS_SIM_CMD");
            }
            while( cmd_cursor && *cmd_cursor )
            {
                char* end = NULL;
                long const at = strtol(cmd_cursor, &end, 0);
                char const* body;
                size_t len;

                if( !end || *end != ',' )
                {
                    cmd_cursor = NULL;
                    break;
                }
                if( frame_count < at )
                    break; /* not yet; re-checked next frame */

                body = end + 1;
                len = strcspn(body, ";");
                {
                    char text[128];
                    if( len >= sizeof(text) )
                        len = sizeof(text) - 1;
                    memcpy(text, body, len);
                    text[len] = '\0';
                    App_SendCommand(&app, text);
                    fprintf(stderr, "sim_cmd: frame %ld sent ::%s\n", (long)frame_count, text);
                }
                cmd_cursor = body[len] == ';' ? body + len + 1 : NULL;
            }
        }

        /* TORIRS_SIM_CLICK_AT="frame,x,y[,right][;frame,x,y...]":
         * inject a mouse click at the given main-loop frame — the
         * live-server harness (the pre-loop SIM_MOUSE_CLICK path runs
         * before login completes, too early to test the world). The
         * move lands 3 frames before the press so the hover pick set
         * (built during render) covers the click position. */
        {
            static char const* sim_at_cursor = NULL;
            static int sim_at_init = 0;
            static long pend_frame = -1, pend_x, pend_y, pend_right;
            if( !sim_at_init )
            {
                sim_at_init = 1;
                sim_at_cursor = getenv("TORIRS_SIM_CLICK_AT");
            }
            if( pend_frame < 0 && sim_at_cursor && *sim_at_cursor )
            {
                char* end = NULL;
                pend_frame = strtol(sim_at_cursor, &end, 0);
                if( end && *end == ',' )
                {
                    pend_x = strtol(end + 1, &end, 0);
                    pend_y = (end && *end == ',') ? strtol(end + 1, &end, 0) : 0;
                    pend_right = 0;
                    if( end && *end == ',' )
                        pend_right = strtol(end + 1, &end, 0);
                    sim_at_cursor = (end && *end == ';') ? end + 1 : NULL;
                }
                else
                {
                    sim_at_cursor = NULL;
                    pend_frame = -1;
                }
            }
            if( pend_frame >= 0 && frame_count >= pend_frame )
            {
                long step = frame_count - pend_frame;
                uint8_t btn = pend_right ? 3 : 1;
                if( step == 0 )
                {
                    CmdBus_PushMouseMove(&bus, (int)pend_x, (int)pend_y);
                    fprintf(
                        stderr,
                        "sim_click_at: frame=%ld move %ld,%ld right=%ld\n",
                        pend_frame,
                        pend_x,
                        pend_y,
                        pend_right);
                }
                else if( step == 3 )
                {
                    CmdBus_PushMouseButton(
                        &bus, TORIRS_CMD_INPUT_MOUSE_DOWN, btn, (int)pend_x, (int)pend_y);
                }
                else if( step >= 4 )
                {
                    CmdBus_PushMouseButton(
                        &bus, TORIRS_CMD_INPUT_MOUSE_UP, btn, (int)pend_x, (int)pend_y);
                    fprintf(stderr, "sim_click_at: released %ld,%ld\n", pend_x, pend_y);
                    pend_frame = -1;
                }
            }
        }

        /* TORIRS_SIM_RESIZE="frame,WxH[;frame,WxH...]": inject a window
         * resize at the given main-loop frame. The only way to exercise
         * the resize path headlessly — SDL_VIDEODRIVER=dummy never
         * delivers a real SDL_WINDOWEVENT_SIZE_CHANGED, and the whole
         * point of the path is what the gameframe's onResize scripts do
         * after it, which is not observable from the window at all. */
        {
            static char const* sim_resize_cursor = NULL;
            static int sim_resize_init = 0;
            static long rz_frame = -1, rz_w, rz_h;
            if( !sim_resize_init )
            {
                sim_resize_init = 1;
                sim_resize_cursor = getenv("TORIRS_SIM_RESIZE");
            }
            if( rz_frame < 0 && sim_resize_cursor && *sim_resize_cursor )
            {
                char* end = NULL;
                rz_frame = strtol(sim_resize_cursor, &end, 0);
                if( end && *end == ',' )
                {
                    rz_w = strtol(end + 1, &end, 0);
                    rz_h = (end && *end) ? strtol(end + 1, &end, 0) : 0;
                    sim_resize_cursor = (end && *end == ';') ? end + 1 : NULL;
                }
                else
                {
                    sim_resize_cursor = NULL;
                    rz_frame = -1;
                }
                if( rz_w <= 0 || rz_h <= 0 )
                    rz_frame = -1;
            }
            if( rz_frame >= 0 && frame_count >= rz_frame )
            {
                fprintf(stderr, "sim_resize: frame=%ld %ldx%ld\n", rz_frame, rz_w, rz_h);
                CmdBus_PushWindowResize(&bus, (int32_t)rz_w, (int32_t)rz_h);
                rz_frame = -1;
            }
        }

        /* TORIRS_SIM_WINDOW="frame,WxH[;frame,WxH...]": drag the WINDOW's
         * corner, rather than pushing a canvas resize onto the bus.
         *
         * The difference from TORIRS_SIM_RESIZE above is the whole
         * fixed-vs-resizable question: this touches only the OS window, so
         * whether the client relayouts or keeps letterboxing a 765x503 canvas
         * is decided by the follow gate exactly as it is for a real user drag.
         * TORIRS_SIM_RESIZE walks straight past that gate and therefore cannot
         * tell the two modes apart. */
        {
            static char const* sim_window_cursor = NULL;
            static int sim_window_init = 0;
            static long wz_frame = -1, wz_w, wz_h;
            if( !sim_window_init )
            {
                sim_window_init = 1;
                sim_window_cursor = getenv("TORIRS_SIM_WINDOW");
            }
            if( wz_frame < 0 && sim_window_cursor && *sim_window_cursor )
            {
                char* end = NULL;
                wz_frame = strtol(sim_window_cursor, &end, 0);
                if( end && *end == ',' )
                {
                    wz_w = strtol(end + 1, &end, 0);
                    wz_h = (end && *end) ? strtol(end + 1, &end, 0) : 0;
                    sim_window_cursor = (end && *end == ';') ? end + 1 : NULL;
                }
                else
                {
                    sim_window_cursor = NULL;
                    wz_frame = -1;
                }
                if( wz_w <= 0 || wz_h <= 0 )
                    wz_frame = -1;
            }
            if( wz_frame >= 0 && frame_count >= wz_frame )
            {
                fprintf(stderr, "sim_window: frame=%ld %ldx%ld\n", wz_frame, wz_w, wz_h);
                PlatformSDL2_SetWindowSize(sdl, (int)wz_w, (int)wz_h);
                wz_frame = -1;
            }
        }
        }
    }

#if !defined(TORIRS_PLATFORM_WEB)
    if( executor_cfg.js5_enabled &&
        PlatformXIO_Js5Pump(app.runner.px, PlatformSDL2_Ticks64()) < 0 )
    {
        fprintf(
            stderr,
            "torirs: JS5 cache producer stopped (error=%d)\n",
            (int)PlatformXIO_Js5LastError(app.runner.px));
        return 0;
    }
#endif

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_COMMAND_DRAIN)
    {
        if( input_frame_pending )
            LibToriRS_Input_Continue(input, now);
        else
            LibToriRS_Input_Begin(input, now);
        App_DrainCommands(&app, &bus, input);
        LibToriRS_Input_End(input);
    }

    /* Reconcile the presentation surfaces with the canvas the drain just
     * settled on. The canvas is the authority (App_SetCanvasSize clamps it to a
     * floor the window does not respect), so the backbuffer is sized from it
     * and never from the raw window — App_Render writes exactly
     * UITREE_LAYOUT_ROOT_W x _H ints, so any disagreement here is a buffer
     * overrun rather than a cosmetic bug. Below the floor the window letterboxes
     * the clamped canvas, which is also what fixed mode does. */
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_SURFACE_SYNC)
    {
        /* Cheap and unconditional: a window dragged from a Retina display to
         * an ordinary one changes density with no event that says so, and
         * App_SetChromeScale returns immediately when nothing moved. */
        if( !getenv("TORIRS_CHROME_SCALE") )
            App_SetChromeScale(&app, PlatformSDL2_PixelDensity(sdl));
        PlatformSDL2_Resize(sdl, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#if defined(TORIRS_HAVE_D3D9)
        if( d3d9 )
            ToriRS_D3D9_SetViewport(d3d9, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
#if defined(TORIRS_HAVE_GL3)
        if( gl3 )
            ToriRS_GL3_SetViewport(gl3, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
    }

    app_redraw = 0;
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_APP_RUN)
    {
        app_redraw = App_RunOnce(&app, now, input);
    }
    input_frame_pending =
        app.app_state == APP_STATE_READY && !App_InputFrameConsumed(&app);
    if( app_redraw )
    {
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_DISPLAY)
        {
            interactive_render_present(&app, sdl, gl3, d3d9);
        }
    }
    else
    {
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
            interactive_present_retained(sdl, gl3, d3d9);
        }
    }

    /* Fixed mode: script 5355 carves the popout strip from the canvas. Grow the
     * canvas by the measured strip so the classic frame stays APP_CANVAS_MIN_W
     * and the strip sits outside it. Must run after App_RunOnce so open/close
     * layout (5354) has already widened/collapsed the strip. The next frame's
     * drain/resize/present picks up the new size. */
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_WINDOW_SYNC)
    {
        /* "Interface scaling" (device option 27) shrinks the canvas the window
         * is letterboxed from, so it is a canvas change and nothing else — no
         * window call, and no bus round trip, because the click that caused it
         * is already in the recorded stream and the canvas is a pure function
         * of it and the window size. The surface reconcile at the top of the
         * next frame picks up the new backbuffer size. */
        App_SyncUiScale(&app);

        if( App_WindowMode(&app) == CS2VM_WINDOW_MODE_FIXED &&
            App_SyncFixedChromeInset(&app) )
        {
            int const fw = UITREE_LAYOUT_ROOT_W;
            int const fh = UITREE_LAYOUT_ROOT_H;
            PlatformSDL2_SetWindowSize(sdl, fw, fh);
            PlatformSDL2_SetCanvasFollowsWindow(sdl, &bus, false, fw, fh);
            if( getenv("TORIRS_RESIZE_DEBUG") )
                fprintf(stderr, "fixed-chrome: canvas %dx%d (strip inset)\n", fw, fh);
        }

    /*
     * A clientscript changed the window mode (the Display panel's client-mode
     * dropdown is [clientscript,settings_client_mode], and its whole body is
     * setwindowmode + setdefaultwindowmode). The App cannot act on it — it has
     * no window — so the shell does:
     *
     *   resizable -> the canvas tracks the window from now on, starting with
     *                the size the window already is
     *   fixed     -> stop tracking and pin the canvas back to the fixed frame,
     *                which the window then letterboxes
     *
     * Both go out as TORIRS_CMD_WINDOW_RESIZE rather than a direct call, so a
     * mode flip is in the recorded stream and replays at the frame it happened.
     */
    {
        int new_mode = 0;
        if( App_TakeWindowModeChange(&app, &new_mode) )
        {
            bool const resizable = new_mode == CS2VM_WINDOW_MODE_RESIZABLE;
            fprintf(
                stderr, "windowmode: %s\n", resizable ? "resizable" : "fixed");
            PlatformSDL2_SetCanvasFollowsWindow(
                sdl, &bus, resizable, APP_CANVAS_MIN_W, APP_CANVAS_MIN_H);
            if( !resizable )
                CmdBus_PushWindowResize(&bus, APP_CANVAS_MIN_W, APP_CANVAS_MIN_H);
            /* Strip inset is applied next frame once layout has measured it. */
        }
        {
            int layout_mode = 0;
            if( App_TakeClientLayoutChange(&app, &layout_mode) )
            {
                fprintf(stderr, "client_layout: mode=%d\n", layout_mode);
                if( app.net && app.net->state == TORIRS_NET_GAME )
                {
                    uint8_t nsbuf[32];
                    int nslen = net_out_window_status(
                        app.net->rev,
                        app.net->random_out,
                        nsbuf,
                        (int)sizeof(nsbuf),
                        layout_mode,
                        UITREE_LAYOUT_ROOT_W,
                        UITREE_LAYOUT_ROOT_H);
                    if( nslen > 0 )
                        ToriRS_Network_SendRaw(app.net, nsbuf, nslen);
                }
            }
        }
    }
    }

    /*
     * The game asked; the platform plays. Once per frame, after the tick that
     * queued the requests -- a command's PCM is borrowed for exactly the
     * duration of the submit, which the backend copies inside.
     *
     * Then Update mixes and feeds the device, and Feedback reports how far
     * ahead the music stream is so the *next* tick knows how much to
     * synthesise. That ordering is the whole contract; see
     * platform/platform_audio.h.
     */
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_FRAME_POST)
    {
        struct ToriRS_AudioFeedback audio_feedback;

        PlatformAudio_SubmitAll(
            audio,
            audio_commands,
            App_DrainAudio(&app, audio_commands, TORIRS_AUDIO_QUEUE_MAX));
        PlatformAudio_Update(audio);
        PlatformAudio_Feedback(audio, &audio_feedback);
        App_SetAudioFeedback(&app, &audio_feedback);
        update_window_title(sdl, &app, cfg.interface_id);
    }
    /* Close the work timer before pacing sleeps — otherwise capped runs always
     * report ~20 ms (sleep fills the residual) and uncapped Delay(1) adds a
     * flat 1 ms that hides real drift. */
    TORIRS_PERF_FRAME_END();
    /* Whatever this frame had to say leaves as one write, after the work timer
     * closed and before the pacing wait absorbs it. Free on a frame that logged
     * nothing: fflush on an empty buffer writes nothing. See the setvbuf in
     * main(). */
    fflush(stderr);
    App_NoteFrameTime(&app, PlatformSDL2_TicksUs() - frame_start_us);
    /* The browser paces us: emscripten_set_main_loop is backed by
     * requestAnimationFrame, and a blocking sleep here would stall the page's
     * whole main thread rather than yield it. */
#if !defined(__EMSCRIPTEN__)
    /* 50 fps cap: wait to an absolute deadline so early wakeups are retried
     * and the frame's complete workload counts against its 20 ms budget.
     * --uncapped is a true profiling mode and performs no artificial wait. */
    if( !replay && !uncapped )
    {
        uint64_t pace_begin_us = PlatformSDL2_TicksUs();

        if( pace_spin )
        {
            /* TORIRS_PACE_SPIN=1 holds the core busy across the wait instead of
             * sleeping. Diagnostic only — it pins a core at 100% — but it is the
             * only way to separate the cap's own cost from the cost of resuming
             * a CPU that Windows parked during the sleep. */
            while( PlatformSDL2_Ticks64() < frame_start_ms + 20 )
                ;
        }
        else
            PlatformSDL2_SleepUntil(frame_start_ms + 20);

        TORIRS_PERF_CARRY(
            TORIRS_PERF_STAGE_PACE, (PlatformSDL2_TicksUs() - pace_begin_us) * 1000u);
    }
#endif
    return 1;
}

/** Everything after the loop: final dumps, then release the platform. */
static void
frame_loop_teardown(void)
{

    /* TORIRS_EXIT_BMP=path: dump the final frame on exit (live-server
     * smoke runs under TORIRS_MAX_FRAMES + SDL dummy driver). */
    if( getenv("TORIRS_EXIT_BMP") )
    {
        /* TORIRS_SIM_HOVER=x,y: park the pointer there for a few real
         * interact frames FIRST, so both the dumps below and the BMP capture
         * hover-dependent chrome (IF1 overlayer tooltips, over-colour swaps,
         * CS2 onmouserepeat tooltip layers) instead of whatever the last
         * main-loop event left behind. */
        if( getenv("TORIRS_SIM_HOVER") )
        {
            struct LibToriRS_Input hov_storage;
            struct LibToriRS_Input* hov_input = LibToriRS_Input_Init(&hov_storage, 0);
            char* hov_sep = NULL;
            int hov_x = (int)strtol(getenv("TORIRS_SIM_HOVER"), &hov_sep, 0);
            int hov_y = hov_sep && *hov_sep == ',' ? (int)strtol(hov_sep + 1, NULL, 0) : 0;
            for( int t = 0; t < 4; t++ )
            {
                LibToriRS_Input_Begin(hov_input, (uint64_t)(t + 1) * 20);
                LibToriRS_Input_PushMouseMove(hov_input, hov_x, hov_y);
                LibToriRS_Input_End(hov_input);
                App_RunOnce(&app, (uint64_t)(t + 1) * 20, hov_input);
            }
            fprintf(
                stderr,
                "sim_hover: parked at %d,%d hover_com_id=%d\n",
                hov_x,
                hov_y,
                app.hover_com_id);
        }
        /* Post-mount snapshot: unlike the boot-time TORIRS_DUMP_TREE (which
         * runs before any server IF_OPENSUB lands), this dumps after the
         * frame loop so server-driven interface mounts are visible. */
        if( getenv("TORIRS_DUMP_TREE_EXIT") && app.tree )
            dump_tree(&app, cfg.interface_id);
        if( getenv("TORIRS_DUMP_ROOTS") && app.tree )
            dump_roots(&app);
        if( getenv("TORIRS_DUMP_HOOKS_EXIT") && app.tree )
            dump_hooks(&app);
        /* Post-network emit dump: the actual draw list for the last frame,
         * to find what paints over the world viewport (0,0..723,503). */
        /* TORIRS_DUMP_BOUNDS=<group>: post-net resolved geometry for one
         * interface. dump_tree deliberately prints no box for RS_LAYERs (it
         * stays byte-comparable with the reference widgetTreeDump), and
         * TORIRS_DUMP_LAYOUT runs at boot before anything is mounted — so
         * this is the only view of a mounted container's resolved box, its
         * size modes, and its scroll extents. A layer whose box is taller
         * than the cache says is why its children escape the clip. */
        if( getenv("TORIRS_DUMP_BOUNDS") && app.tree )
        {
            int want = (int)strtol(getenv("TORIRS_DUMP_BOUNDS"), NULL, 0);
            for( uint32_t i = 0; i < app.tree->component_count; i++ )
            {
                struct UITreeComponent const* c = &app.tree->components[i];
                if( c->freed || ((c->component_id >> 16) & 0xFFFF) != want )
                    continue;
                fprintf(
                    stderr,
                    "BOUNDS com=0x%08x (%d|%d) type=%d graphic=%d hidden=%d "
                    "abs=%d,%d %dx%d "
                    "wh=%d,%d modes=w%d,h%d,x%d,y%d scroll=%dx%d off=%d,%d\n",
                    (unsigned)c->component_id,
                    (c->component_id >> 16) & 0xFFFF,
                    c->component_id & 0xFFFF,
                    (int)c->type,
                    /* Which sprite a graphic node resolved to. A settings
                     * slider bobble is 2860 when green and 4894 when grey, and
                     * that pair is the only way to tell the two apart from
                     * outside the renderer. */
                    c->type == UIELEM_RS_GRAPHIC
                        ? UITreeSceneBridge_SpriteCacheIdForScene(
                              &app.bridge, c->u.rs_graphic.scene_id)
                        : -1,
                    dump_node_hidden(app.tree, (int32_t)i),
                    c->position.abs_x, c->position.abs_y,
                    c->position.abs_w, c->position.abs_h,
                    c->position.width, c->position.height,
                    (int)c->position.width_mode, (int)c->position.height_mode,
                    (int)c->position.x_mode, (int)c->position.y_mode,
                    c->type == UIELEM_RS_LAYER ? c->u.rs_layer.scroll_width : -1,
                    c->type == UIELEM_RS_LAYER ? c->u.rs_layer.scroll_height : -1,
                    c->scroll_x, c->scroll_y);
            }
        }

        /* TORIRS_DUMP_EMIT_EXIT: post-net draw list (the boot-time
         * TORIRS_DUMP_EMIT fires before any server interface has mounted, so
         * it never shows sub-interface content). Value selects the filter:
         *   "cover"        -> only viewport-covering rects (the original
         *                     use: finding an interface painted over the world)
         *   <group id>     -> only that interface group's components
         *   anything else  -> every command
         * The clip is included because a drawable overflowing its container is
         * a clip bug, and the clip is the only way to tell which. */
        {
            char const* emit_filter = getenv("TORIRS_DUMP_EMIT_EXIT");
            int filter_group = -1;
            int cover_only = 0;
            if( emit_filter )
            {
                if( strcmp(emit_filter, "cover") == 0 )
                    cover_only = 1;
                else if( emit_filter[0] >= '1' && emit_filter[0] <= '9' )
                    filter_group = (int)strtol(emit_filter, NULL, 0);
            }
            for( int i = 0; emit_filter && i < app.emit.count; i++ )
            {
                struct UITreeEmitDesc* d = &app.emit.cmds[i];
                int group = (d->component_id >> 16) & 0xFFFF;
                if( cover_only && !(d->w >= 300 && d->h >= 200 && d->x < 480) )
                    continue;
                if( filter_group >= 0 && group != filter_group )
                    continue;
                fprintf(
                    stderr,
                    "EMIT_EXIT[%d] kind=%d com=0x%08x (%d|%d) x=%d y=%d w=%d h=%d scene=%d model=%d "
                    "color=0x%06x filled=%d trans=%d tiled=%d clip=%d,%d %dx%d\n",
                    i, (int)d->kind, d->component_id, group, d->component_id & 0xFFFF,
                    d->x, d->y, d->w, d->h, d->scene_id, d->model_id, d->color, d->filled, d->trans,
                    d->tiled, d->clip.x, d->clip.y, d->clip.w, d->clip.h);
            }
        }
        if( getenv("TORIRS_NET_DEBUG") && app.tree )
        {
            for( int t = 0; t < 14; t++ )
                fprintf(
                    stderr,
                    "exit: tab %d overlay=%d owner=%d\n",
                    t,
                    app.slots.side_overlay_id[t],
                    app.slots.side_owner_index[t]);
            for( uint32_t i = 0; i < app.tree->component_count; i++ )
            {
                struct UITreeComponent const* c = &app.tree->components[i];
                if( c->type == UIELEM_BUILTIN_TAB_ICONS )
                    fprintf(
                        stderr,
                        "exit: tab_icon idx=%u tab=%d freed=%d hide=%d scene=%d x=%d y=%d\n",
                        i,
                        c->u.tab_icon.tabno,
                        (int)c->freed,
                        (int)c->behavior.hide,
                        c->u.tab_icon.scene_id,
                        c->position.abs_x,
                        c->position.abs_y);
            }
            for( int f = 0; f < 6; f++ )
                fprintf(
                    stderr,
                    "exit: scene_font %d has=%d\n",
                    f,
                    (int)ToriDraw_SceneFontHas(app.scene, f));
            fprintf(stderr, "exit: hover_com_id=%d\n", app.hover_com_id);
            fprintf(
                stderr,
                "exit: minimap_view valid=%d box=%d,%d %dx%d com=%d\n",
                app.minimap_view_valid,
                app.minimap_emit_desc.x,
                app.minimap_emit_desc.y,
                app.minimap_emit_desc.w,
                app.minimap_emit_desc.h,
                app.minimap_emit_desc.component_id);
            /* TORIRS_DUMP_COM=id: dump every live node carrying that
             * component id (duplicate-id / ApplyText-target debugging). */
            if( getenv("TORIRS_DUMP_COM") )
            {
                int want = atoi(getenv("TORIRS_DUMP_COM"));
                for( uint32_t i = 0; i < app.tree->component_count; i++ )
                {
                    struct UITreeComponent const* c = &app.tree->components[i];
                    if( c->component_id != want )
                        continue;
                    fprintf(
                        stderr,
                        "exit: com=%d idx=%u type=%d freed=%d hide=%d text='%s' "
                        "abs=%d,%d wh=%dx%d font=%d color=0x%x "
                        "textalign=%d,%d lineheight=%d parent=%d\n",
                        want,
                        i,
                        (int)c->type,
                        (int)c->freed,
                        (int)c->behavior.hide,
                        c->type == UIELEM_RS_TEXT && c->u.rs_text.text ? c->u.rs_text.text
                                                                      : "",
                        c->position.abs_x,
                        c->position.abs_y,
                        c->position.abs_w,
                        c->position.abs_h,
                        c->type == UIELEM_RS_TEXT ? c->u.rs_text.font_id : -1,
                        c->type == UIELEM_RS_TEXT ? c->u.rs_text.color : 0,
                        c->type == UIELEM_RS_TEXT ? c->u.rs_text.center : -1,
                        c->type == UIELEM_RS_TEXT ? c->u.rs_text.y_align : -1,
                        c->type == UIELEM_RS_TEXT ? c->u.rs_text.line_height : -1,
                        c->parent);
                }
            }
        }
        int* pixels = calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
        assert(pixels);
        App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        bmp_write_file(
            getenv("TORIRS_EXIT_BMP"), pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        printf("wrote %s\n", getenv("TORIRS_EXIT_BMP"));
        free(pixels);
    }

    if( replay )
    {
        fclose(replay);
        /* TORIRS_REPLAY_BMP=path: dump the final replayed frame for golden
         * comparison against the recorded session. */
        if( getenv("TORIRS_REPLAY_BMP") )
        {
            int* pixels =
                calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
            assert(pixels);
            App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            bmp_write_file(
                getenv("TORIRS_REPLAY_BMP"),
                pixels,
                UITREE_LAYOUT_ROOT_W,
                UITREE_LAYOUT_ROOT_H);
            printf("wrote %s\n", getenv("TORIRS_REPLAY_BMP"));
            free(pixels);
        }
    }
    CmdBus_RecordClose(&bus);

    NetTransport_Free(sock);
    /*
     * The audio ledger, and the leak check the retained API makes possible.
     *
     * `assets_live` at exit is how many clips the backend still holds. A
     * session that played sounds ends with a few, because the scene keeps them
     * until it is torn down and that happens after this line -- so what this
     * catches is not "> 0" but a count that keeps *climbing* across a run, which
     * is the signature of ids being reloaded rather than reused. Everything else
     * here is the one place to see whether the mixer was starved, saturated or
     * muted.
     */
    if( ToriRS_AudioTraceEnabled() )
    {
        struct PlatformAudioStats stats = PlatformAudio_Stats(audio);
        fprintf(
            stderr,
            "audio: %d commands, %d voices started (%d stolen, %d rejected), "
            "%d frames played, stream %d dropped / %d starved, %d assets still live\n",
            stats.commands,
            stats.voices_started,
            stats.voices_stolen,
            stats.voices_rejected,
            stats.frames_played,
            stats.stream_dropped_frames,
            stats.stream_starved_frames,
            stats.assets_live);
        fprintf(
            stderr,
            "audio: bus gains effects/music/area %d/%d/%d\n",
            stats.bus_volume[TORIRS_AUDIO_BUS_EFFECTS],
            stats.bus_volume[TORIRS_AUDIO_BUS_MUSIC],
            stats.bus_volume[TORIRS_AUDIO_BUS_AREA]);
        fprintf(
            stderr,
            "audio: %d callbacks, %d underruns, period %.2f ms, "
            "interval %.2f/%.2f/%.2f ms, jitter peak %.2f ms, render peak %.2f ms\n",
            stats.updates,
            stats.underruns,
            stats.callback_period_ms,
            stats.update_interval_min_ms,
            stats.update_interval_mean_ms,
            stats.update_interval_max_ms,
            stats.callback_jitter_max_ms,
            stats.render_max_ms);
        fprintf(
            stderr,
            "audio: stream ring %d/%.1f/%d frames (now %d), capture dropped %d frames\n",
            stats.queue_min_frames,
            stats.queue_mean_frames,
            stats.queue_max_frames,
            stats.queue_current_frames,
            stats.capture_dropped_frames);
    }
    PlatformAudio_Free(audio);
#if defined(TORIRS_HAVE_D3D9)
    ToriRS_D3D9_Free(d3d9);
#endif
#if defined(TORIRS_HAVE_GL3)
    ToriRS_GL3_Free(gl3);
#endif
    PlatformSDL2_Free(sdl);
}

#if defined(__EMSCRIPTEN__)
/* The browser owns the frame clock, so the loop is inverted: instead of the
 * client calling the platform once per iteration, the platform calls the
 * client. Same step function either way. */
static void
frame_loop_tick(void)
{
    if( frame_loop_step() )
        return;
    emscripten_cancel_main_loop();
    /* Close the final CPU calibration interval before capture/destruction. */
    TorirsPerf_Shutdown();
    frame_loop_teardown();
    App_Shutdown(&app);
}
#endif

static int
parse_executor_cli_int(
    char const* flag,
    char const* value,
    int min,
    int max,
    int* out)
{
    char* end = NULL;
    long parsed = strtol(value, &end, 10);

    if( end == value || *end != '\0' || parsed < min || parsed > max )
    {
        fprintf(stderr, "torirs: %s takes an integer in %d..%d\n", flag, min, max);
        return -1;
    }
    *out = (int)parsed;
    return 0;
}

static int
set_executor_js5_host(char const* value)
{
    size_t len = strlen(value);
    if( len == 0 || len >= sizeof(executor_cfg.js5_host) )
    {
        fprintf(
            stderr,
            "torirs: --js5-host must contain 1..%zu characters\n",
            sizeof(executor_cfg.js5_host) - 1);
        return -1;
    }
    memcpy(executor_cfg.js5_host, value, len + 1);
    return 0;
}

#if !defined(TORIRS_PLATFORM_WEB)
static void
executor_js5_config(struct Js5Config* js5)
{
    Js5ConfigInit(js5);
    js5->host = executor_cfg.js5_host;
    js5->primary_port = (uint16_t)executor_cfg.js5_port;
    js5->fallback_port = (uint16_t)executor_cfg.js5_fallback_port;
    js5->revision = (uint32_t)executor_cfg.js5_revision;
}

/*
 * Bring every reference table to a server-validated state on disk.
 *
 * This has to happen before App_Init, not with the attached producer after it.
 * App_Init decodes reference tables itself, and a decode is not a tolerant
 * read: a torn or corrupt 255/N container reaches bzip as a short buffer and
 * takes the process down ("bzip error: Unexpected input EOF") before the
 * producer that exists to repair it has been attached. An absent table is
 * survivable and a corrupt one is not, which is the wrong way round for a cache
 * whose first boot writes all 23 of them.
 *
 * The client attached after App_Init then re-validates the same tables against
 * the same master index. That second pass is a local CRC check, not a second
 * download -- measured at 208 bytes against a warm cache -- so the ordering
 * costs one extra connection and nothing else.
 */
static int
executor_prime_js5_reference_tables(struct RSCache_Dat2Disk* sparse)
{
    struct PlatformXIOJs5Cache* prime;
    struct Js5Config js5;
    int status = 0;

    executor_js5_config(&js5);
    prime = PlatformXIOJs5Cache_New(sparse, &js5);
    if( !prime )
    {
        fprintf(stderr, "torirs: failed to attach JS5 reference-table primer\n");
        return -1;
    }

    while( status == 0 )
    {
        if( PlatformXIOJs5Cache_Tick(prime, PlatformSDL2_Ticks64()) < 0 )
            status = -1;
        else if( PlatformXIOJs5Cache_MetadataReady(prime) )
            status = 1;
        else
            PlatformSDL2_SleepUntil(PlatformSDL2_Ticks64() + 1u);
    }

    {
        struct Js5Progress progress;
        PlatformXIOJs5Cache_GetProgress(prime, &progress);
        if( status < 0 )
            fprintf(
                stderr,
                "torirs: JS5 reference-table prime failed (error=%d state=%d "
                "status=%u port=%u)\n",
                (int)progress.last_error,
                (int)progress.state,
                (unsigned)progress.handshake_status,
                (unsigned)progress.current_port);
        else
            /* Report here as well as after App_Init: this pass is the one that
             * actually downloads on a cold cache, so without it a first boot
             * reports the second pass's local-validation cost and looks free. */
            fprintf(
                stderr,
                "torirs: JS5 reference tables primed (%u references, %llu network bytes)\n",
                (unsigned)progress.references_ready,
                (unsigned long long)progress.bytes_received);
    }
    PlatformXIOJs5Cache_Free(prime);
    return status < 0 ? -1 : 0;
}

static int
executor_prepare_js5_cache(void)
{
    struct RSCache_Dat2Disk* sparse;
    int status;

    if( !executor_cfg.js5_enabled )
        return 0;
    sparse = RSCache_Dat2DiskNewSparseFromDirectory(cfg.cache_dir);
    if( !sparse )
    {
        fprintf(
            stderr,
            "torirs: cannot create/open incremental dat2 cache at %s "
            "(the directory must already exist)\n",
            cfg.cache_dir);
        return -1;
    }
    status = executor_prime_js5_reference_tables(sparse);
    RSCache_Dat2DiskFree(sparse);
    return status;
}

static int
executor_attach_and_prime_js5(void)
{
    struct Js5Config js5;
    int status;

    if( !executor_cfg.js5_enabled )
        return 0;

    executor_js5_config(&js5);
    if( PlatformXIO_Js5Enable(app.runner.px, &js5) != 0 )
    {
        fprintf(stderr, "torirs: failed to attach JS5 cache producer\n");
        return -1;
    }

    /*
     * The master index is always requested from the server. Valid local
     * reference tables may satisfy the subsequent checks, but no core task is
     * stepped until all server-authoritative metadata is installed.
     */
    while( (status = PlatformXIO_Js5Pump(app.runner.px, PlatformSDL2_Ticks64())) == 0 )
        PlatformSDL2_SleepUntil(PlatformSDL2_Ticks64() + 1u);
    if( status < 0 )
    {
        struct Js5Progress progress;
        PlatformXIO_Js5GetProgress(app.runner.px, &progress);
        fprintf(
            stderr,
            "torirs: JS5 metadata prime failed (error=%d state=%d status=%u port=%u)\n",
            (int)progress.last_error,
            (int)progress.state,
            (unsigned)progress.handshake_status,
            (unsigned)progress.current_port);
        return -1;
    }

    {
        struct Js5Progress progress;
        PlatformXIO_Js5GetProgress(app.runner.px, &progress);
        fprintf(
            stderr,
            "torirs: JS5 metadata ready (%u references, %llu network bytes)\n",
            (unsigned)progress.references_ready,
            (unsigned long long)progress.bytes_received);
    }
    return 0;
}
#endif

#if defined(TORIRS_WEB_CACHE_IDB)
/*
 * The browser's half of the same two steps.
 *
 * The prime that precedes App_Init already happened: the page ran it before
 * main() through the exported torirs_web_cache_prime_* functions, because a
 * WebSocket cannot deliver a byte to a thread that is spinning on it (see
 * platform/web_cache_boot.c). All that is left here is the attach.
 *
 * And the attach does not wait. On the desktop the second pass blocks, but
 * blocking is the one thing this host cannot do, and it does not need to: the
 * reference tables are already installed, so what the attached client re-runs
 * is a local CRC check. A group read that arrives before it finishes parks in
 * the ordinary way — PlatformX_IO_Pending is what tells TaskRunner_Step not to
 * resume a task whose slot is unfilled, and it makes no distinction between
 * waiting on a download and waiting on this.
 */
static int
executor_attach_js5_web(void)
{
    struct Js5Config js5;

    if( !WebCacheBoot_Ready() )
    {
        fprintf(stderr, "torirs: JS5 metadata was never primed; cache reads will fail\n");
        return -1;
    }

    Js5ConfigInit(&js5);
    js5.host = WebCacheBoot_Js5Host();
    js5.primary_port = (uint16_t)WebCacheBoot_Js5Port();
    js5.fallback_port = 0; /* a browser has one endpoint; 443 is not a second */
    js5.revision = (uint32_t)WebCacheBoot_Js5Revision();
    /* Demand-only. A tab must not quietly pull a couple of hundred megabytes,
     * and the reads the boot is blocked on should not queue behind a mirror
     * nobody asked for. The cache converges on the working set instead. */
    js5.background_fill = false;
    if( PlatformXIO_Js5Enable(app.runner.px, &js5) != 0 )
    {
        fprintf(stderr, "torirs: failed to attach JS5 cache producer\n");
        return -1;
    }
    fprintf(
        stderr,
        "torirs: JS5 attached (ws://%s:%d, rev %d) — filling on demand\n",
        WebCacheBoot_Js5Host(),
        WebCacheBoot_Js5Port(),
        WebCacheBoot_Js5Revision());
    return 0;
}
#endif

struct MainArgState
{
    int write_bmp;
    int use_opengl3;
    int use_d3d9;
    int d3d9_zbuffer;
    /* Depth-buffered world pass for the GL backends — the peer of
     * --d3d9-zbuffer. Selected by --opengl3-zbuffer / --webgl1-zbuffer. */
    int gl3_zbuffer;
};

static void
main_print_usage(char const* program)
{
    fprintf(
        stderr,
        "usage: %s [cache_dir] [interface_id] [--manifest <boot.ini>] "
        "[--dat1|--dat2] [--revconfig <ui.ini>] [--revconfig-cache <cache.ini>] "
        "[--bmp] [--connect host[:port]] [--port N] [--offline] [--user U] "
        "[--pass P] [--rev lc254|lc245_2|xrsps233] "
        "[--js5|--no-js5] [--js5-host H] [--js5-port N] "
        "[--js5-fallback-port N] [--js5-revision N] [--uncapped] "
        "[--windowmode fixed|resizable] [--window WxH] "
        "[--opengl3|--opengl3-zbuffer|--webgl1|--webgl1-zbuffer|--d3d9|"
        "--d3d9-zbuffer|--soft3d]\n",
        program);
}

/* Apply one argv layer. Manifest-provided tokens are applied first and the
 * process argv second; every layer gets fresh positional slots, so an explicit
 * cache/interface positional replaces rather than follows a manifest one. */
static int
main_parse_argument_layer(
    int argc,
    char* const* argv,
    int first,
    int from_manifest,
    char const* program,
    struct MainArgState* state)
{
    int argi;
    int positional = 0;
    int saw_offline = 0;
    int saw_connect = 0;
    int saw_js5_enable = 0;

    for( argi = first; argi < argc; argi++ )
    {
        if( strcmp(argv[argi], "--manifest") == 0 )
        {
            if( from_manifest )
            {
                fprintf(stderr, "torirs: [client:args] cannot contain --manifest\n");
                return 0;
            }
            if( argi + 1 >= argc )
                goto invalid;
            argi++; /* consumed by main's manifest pre-scan */
            continue;
        }
        if( strcmp(argv[argi], "--offline") == 0 )
        {
            saw_offline = 1;
            continue;
        }
        if( strcmp(argv[argi], "--port") == 0 && argi + 1 < argc )
        {
            cfg.connect_port = atoi(argv[++argi]);
            continue;
        }
        if( strcmp(argv[argi], "--js5") == 0 )
        {
            executor_cfg.js5_enabled = 1;
            saw_js5_enable = 1;
            continue;
        }
        if( strcmp(argv[argi], "--no-js5") == 0 )
        {
            executor_cfg.js5_enabled = 0;
            continue;
        }
        if( strcmp(argv[argi], "--js5-host") == 0 && argi + 1 < argc )
        {
            if( set_executor_js5_host(argv[++argi]) != 0 )
                return 0;
            continue;
        }
        if( strcmp(argv[argi], "--js5-port") == 0 && argi + 1 < argc )
        {
            if( parse_executor_cli_int(
                    "--js5-port", argv[++argi], 1, 65535, &executor_cfg.js5_port) != 0 )
                return 0;
            continue;
        }
        if( strcmp(argv[argi], "--js5-fallback-port") == 0 && argi + 1 < argc )
        {
            if( parse_executor_cli_int(
                    "--js5-fallback-port",
                    argv[++argi],
                    0,
                    65535,
                    &executor_cfg.js5_fallback_port) != 0 )
                return 0;
            executor_cfg.js5_fallback_port_set = 1;
            continue;
        }
        if( strcmp(argv[argi], "--js5-revision") == 0 && argi + 1 < argc )
        {
            if( parse_executor_cli_int(
                    "--js5-revision",
                    argv[++argi],
                    1,
                    2147483647,
                    &executor_cfg.js5_revision) != 0 )
                return 0;
            executor_cfg.js5_revision_explicit = 1;
            continue;
        }
        if( strcmp(argv[argi], "--bmp") == 0 )
        {
            state->write_bmp = 1;
            continue;
        }
        if( strcmp(argv[argi], "--dat1") == 0 )
        {
            cfg.cache_kind = APP_CACHE_DAT1;
            cfg.cache_epoch = 1; /* RSCACHE_EPOCH_DAT1: keep identity coherent */
            continue;
        }
        if( strcmp(argv[argi], "--dat2") == 0 )
        {
            cfg.cache_kind = APP_CACHE_DAT2;
            cfg.cache_epoch = 2; /* RSCACHE_EPOCH_DAT2 */
            continue;
        }
        if( strcmp(argv[argi], "--revconfig") == 0 && argi + 1 < argc )
        {
            cfg.revconfig_ui_ini = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--revconfig-cache") == 0 && argi + 1 < argc )
        {
            cfg.revconfig_cache_ini = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--connect") == 0 && argi + 1 < argc )
        {
            cfg.connect_target = argv[++argi];
            saw_connect = 1;
            continue;
        }
        if( strcmp(argv[argi], "--user") == 0 && argi + 1 < argc )
        {
            cfg.connect_user = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--pass") == 0 && argi + 1 < argc )
        {
            cfg.connect_pass = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--rev") == 0 && argi + 1 < argc )
        {
            cfg.rev_name = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--uncapped") == 0 )
        {
            uncapped = 1;
            continue;
        }
        if( strcmp(argv[argi], "--windowmode") == 0 && argi + 1 < argc )
        {
            cfg.window_mode = CS2VM_WindowModeFromName(argv[++argi]);
            if( !cfg.window_mode )
            {
                fprintf(stderr, "torirs: --windowmode takes fixed|resizable\n");
                return 0;
            }
            continue;
        }
        if( strcmp(argv[argi], "--window") == 0 && argi + 1 < argc )
        {
            char* sep = NULL;
            long w = strtol(argv[++argi], &sep, 10);
            long h = (sep && *sep) ? strtol(sep + 1, NULL, 10) : 0;
            if( w <= 0 || h <= 0 )
            {
                fprintf(stderr, "torirs: --window takes WxH\n");
                return 0;
            }
            cfg.window_w = (int)w;
            cfg.window_h = (int)h;
            continue;
        }
        if( strcmp(argv[argi], "--opengl3") == 0 )
        {
#if defined(TORIRS_HAVE_GL3) && !defined(TORIRS_GL_ES2)
            state->use_opengl3 = 1;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->gl3_zbuffer = 0;
            continue;
#elif defined(TORIRS_GL_ES2)
            fprintf(stderr, "torirs: this build renders through WebGL1 — use --webgl1\n");
            return 0;
#else
            fprintf(stderr, "torirs: --opengl3 is not available in this build\n");
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--webgl1") == 0 )
        {
#if defined(TORIRS_GL_ES2)
            state->use_opengl3 = 1;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->gl3_zbuffer = 0;
            continue;
#else
            fprintf(stderr, "torirs: --webgl1 is the browser build's flag — use --opengl3\n");
            return 0;
#endif
        }
        /* The depth-buffered spelling of the two above. Each build accepts only
         * the name it can honour, and names the other, so a flag is never
         * silently ignored. */
        if( strcmp(argv[argi], "--opengl3-zbuffer") == 0 )
        {
#if defined(TORIRS_HAVE_GL3) && !defined(TORIRS_GL_ES2)
            state->use_opengl3 = 1;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->gl3_zbuffer = 1;
            continue;
#elif defined(TORIRS_GL_ES2)
            fprintf(
                stderr,
                "torirs: this build renders through WebGL1 — use --webgl1-zbuffer\n");
            return 0;
#else
            fprintf(stderr, "torirs: --opengl3-zbuffer is not available in this build\n");
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--webgl1-zbuffer") == 0 )
        {
#if defined(TORIRS_GL_ES2)
            state->use_opengl3 = 1;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->gl3_zbuffer = 1;
            continue;
#else
            fprintf(
                stderr,
                "torirs: --webgl1-zbuffer is the browser build's flag — "
                "use --opengl3-zbuffer\n");
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--d3d9") == 0 )
        {
#if defined(TORIRS_HAVE_D3D9)
            state->use_d3d9 = 1;
            state->use_opengl3 = 0;
            state->d3d9_zbuffer = 0;
            continue;
#else
            fprintf(stderr, "torirs: --d3d9 is not available in this build\n");
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--d3d9-zbuffer") == 0 )
        {
#if defined(TORIRS_HAVE_D3D9)
            state->use_d3d9 = 1;
            state->use_opengl3 = 0;
            state->d3d9_zbuffer = 1;
            continue;
#else
            fprintf(stderr, "torirs: --d3d9-zbuffer is not available in this build\n");
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--soft3d") == 0 )
        {
            state->use_opengl3 = 0;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->gl3_zbuffer = 0;
            continue;
        }
        if( positional == 0 && argv[argi][0] != '-' )
        {
            cfg.cache_dir = argv[argi];
            positional++;
            continue;
        }
        if( positional == 1 && argv[argi][0] != '-' )
        {
            cfg.interface_id = atoi(argv[argi]);
            if( cfg.interface_id <= 0 )
            {
                fprintf(stderr, "invalid interface id: %s\n", argv[argi]);
                return 0;
            }
            positional++;
            continue;
        }

    invalid:
        fprintf(
            stderr,
            "torirs: invalid %s argument '%s'\n",
            from_manifest ? "[client:args]" : "command-line",
            argv[argi]);
        main_print_usage(program);
        return 0;
    }

    /* Resolve connectivity per layer. This lets a real `--offline` clear a
     * manifest-provided --connect, while a real --connect overrides manifest
     * offline. Within one layer, connect wins just as it did before. */
    if( saw_offline && !saw_connect )
        cfg.connect_target = NULL;
    /* Offline also suppresses an inherited JS5 producer. An explicit --js5 in
     * this same layer opts back into cache networking without reconnecting the
     * game transport. Later layers can override either result. */
    if( saw_offline && !saw_js5_enable )
        executor_cfg.js5_enabled = 0;
    return 1;
}

/* Used only while locating the boot manifest. It must understand which
 * options consume a value so a literal value equal to `--manifest` is not
 * mistaken for the bootstrap option before the real parser sees it. */
static int
main_argument_takes_value(char const* argument)
{
    return strcmp(argument, "--manifest") == 0 ||
           strcmp(argument, "--port") == 0 ||
           strcmp(argument, "--revconfig") == 0 ||
           strcmp(argument, "--revconfig-cache") == 0 ||
           strcmp(argument, "--connect") == 0 ||
           strcmp(argument, "--user") == 0 ||
           strcmp(argument, "--pass") == 0 ||
           strcmp(argument, "--rev") == 0 ||
           strcmp(argument, "--js5-host") == 0 ||
           strcmp(argument, "--js5-port") == 0 ||
           strcmp(argument, "--js5-fallback-port") == 0 ||
           strcmp(argument, "--js5-revision") == 0 ||
           strcmp(argument, "--windowmode") == 0 ||
           strcmp(argument, "--window") == 0;
}

int
main(
    int argc,
    char** argv)
{
    static char derived_cache_ini[512];
    static struct BootManifest boot_manifest; /* must outlive app: cfg points into it */
    char* manifest_argv[BOOTMANIFEST_CLIENT_ARG_MAX];
    struct MainArgState arg_state = {
        .write_bmp = 0,
        .use_opengl3 = TORIRS_GPU_DEFAULT,
        .use_d3d9 = TORIRS_D3D9_DEFAULT,
        .d3d9_zbuffer = 0,
        .gl3_zbuffer = 0,
    };
    int argi;
    int i;

    /*
     * Buffer the diagnostic stream, and flush it once per frame (see
     * frame_loop_step).
     *
     * stderr is unbuffered by definition, and on Windows one write costs about
     * 6 ms whether it goes to a console or to a redirected file — the cost is
     * per write, not per byte. That is not a logging annoyance, it is the
     * single largest source of frame stutter this client has: the embedded
     * server runs on this thread, and one first-time content complaint per
     * varp turned a swing that touches thirty of them into a 117 ms tick. Each
     * of those reports is worth printing exactly once; none is worth a dropped
     * frame.
     *
     * Buffering turns nine writes in a tick into one, and the flush is parked
     * after the frame's work timer closes, so on a capped run it is paid out of
     * the pacing slack rather than the 20 ms budget. The exposure is the same
     * one every buffered log has — a hard crash can lose up to a frame of
     * output — which is why the flush is per frame rather than per exit.
     * TORIRS_STDERR_UNBUFFERED=1 restores the old behaviour when debugging a
     * crash is worth more than the frame time.
     */
    {
        char const* raw = getenv("TORIRS_STDERR_UNBUFFERED");

        if( !(raw && raw[0] && raw[0] != '0') )
            setvbuf(stderr, NULL, _IOFBF, 65536);
    }

    ToriRS_ExecutorConfig_Init(&executor_cfg);

    /* Pre-scan for --manifest so its values seed cfg before the flag loop;
     * explicit CLI flags below then override (precedence CLI > manifest). */
    for( argi = 1; argi < argc; argi++ )
    {
        if( strcmp(argv[argi], "--manifest") == 0 && argi + 1 < argc )
        {
            if( BootManifest_LoadFile(&boot_manifest, argv[argi + 1]) != 0 )
                return 1;
            BootManifest_ApplyToConfig(&boot_manifest, &cfg);
            BootManifest_ApplyToExecutorConfig(&boot_manifest, &executor_cfg);
#if defined(TORIRS_PLATFORM_WEB)
            /* This host's sockets are WebSockets (emscripten maps connect() to
             * ws://host:port), so the manifest's tcp host:port is the wrong
             * endpoint whenever the server keeps its WebSocket somewhere else —
             * LostCity serves the game on 43594/tcp and upgrades / on its web
             * port. Still before the flag loop, so --connect/--port win. */
            BootManifest_ApplyWebEndpoint(&boot_manifest, &cfg);
#endif
            break;
        }
        if( main_argument_takes_value(argv[argi]) && argi + 1 < argc )
            argi++;
    }

    for( i = 0; i < boot_manifest.client_arg_count; i++ )
        manifest_argv[i] = boot_manifest.client_args[i];
    if( !main_parse_argument_layer(
            boot_manifest.client_arg_count,
            manifest_argv,
            0,
            1,
            argv[0],
            &arg_state) )
        return 1;
    if( !main_parse_argument_layer(argc, argv, 1, 0, argv[0], &arg_state) )
        return 1;

    int const write_bmp = arg_state.write_bmp;
    int const use_opengl3 = arg_state.use_opengl3;
    int const use_d3d9 = arg_state.use_d3d9;
    int const d3d9_zbuffer = arg_state.d3d9_zbuffer;
    int const gl3_zbuffer = arg_state.gl3_zbuffer;

    /* Cache identity is required. Prefer the manifest; otherwise resolve --rev
     * through the named-profile registry. Bare --dat1/--dat2 is not enough. */
    if( !cfg.cache_identity_set )
    {
        char const* rev = cfg.rev_name;
        if( !rev || !rev[0] )
            rev = getenv("TORIRS_REV");
        struct RSCache named;
        if( rev && rev[0] && RSCache_ProfileByName(rev, &named) )
        {
            cfg.cache_game = named.game;
            cfg.cache_epoch = named.epoch;
            cfg.cache_revision = named.revision;
            cfg.cache_quirks = named.quirks;
            cfg.cache_identity_set = 1;
            cfg.cache_kind =
                named.epoch == 1 /* DAT1 */ ? APP_CACHE_DAT1 : APP_CACHE_DAT2;
        }
        else
        {
            fprintf(
                stderr,
                "torirs: cache identity unset — pass --manifest <boot.ini> (with "
                "epoch/game/revision/quirks) or --rev <name>\n");
            return 1;
        }
    }

    /* Kind-specific defaults, applied only where the command line was silent.
     * A dat1 cache has no gameframe interface to open, so it always needs a
     * RevConfig; dat2 keeps opening interface_id unless one is given. */
    if( !cfg.cache_dir )
        cfg.cache_dir = cfg.cache_kind == APP_CACHE_DAT1 ? DAT1_CACHE_DIR : DAT2_CACHE_DIR;

    {
        char error[192];
        if( ToriRS_ExecutorConfig_ResolveJs5(&executor_cfg, &cfg, error, sizeof(error)) != 0 )
        {
            fprintf(stderr, "torirs: %s\n", error);
            return 1;
        }
        if( executor_cfg.js5_enabled )
            fprintf(
                stderr,
                "torirs: js5 host=%s port=%d fallback=%d revision=%d cache=%s\n",
                executor_cfg.js5_host,
                executor_cfg.js5_port,
                executor_cfg.js5_fallback_port,
                executor_cfg.js5_revision,
                cfg.cache_dir);
    }
#if defined(TORIRS_PLATFORM_WEB)
    if( executor_cfg.js5_enabled )
    {
        fprintf(stderr, "torirs: JS5 incremental cache loading is native-only\n");
        return 1;
    }
#endif
    if( !cfg.revconfig_ui_ini && cfg.cache_kind == APP_CACHE_DAT1 )
    {
        cfg.revconfig_ui_ini = DEFAULT_REVCONFIG_UI;
        if( !cfg.revconfig_cache_ini )
            cfg.revconfig_cache_ini = DEFAULT_REVCONFIG_CACHE;
    }
    /* An explicit --revconfig usually has a sibling sprite/font INI named by
     * the same stem. Derive it, but only adopt it if it actually exists —
     * UITreeBuilder treats an empty path as "no cache INI". */
    if( cfg.revconfig_ui_ini && !cfg.revconfig_cache_ini )
    {
        size_t len = strlen(cfg.revconfig_ui_ini);
        char const* suffix = "_ui.ini";
        size_t suffix_len = strlen(suffix);
        if( len > suffix_len && strcmp(cfg.revconfig_ui_ini + len - suffix_len, suffix) == 0 &&
            len - suffix_len + strlen("_cache.ini") < sizeof(derived_cache_ini) )
        {
            FILE* probe;
            snprintf(
                derived_cache_ini,
                sizeof(derived_cache_ini),
                "%.*s_cache.ini",
                (int)(len - suffix_len),
                cfg.revconfig_ui_ini);
            probe = fopen(derived_cache_ini, "rb");
            if( probe )
            {
                fclose(probe);
                cfg.revconfig_cache_ini = derived_cache_ini;
            }
        }
    }

    if( cfg.revconfig_ui_ini )
        fprintf(
            stderr,
            "torirs: %s cache=%s revconfig=%s cache_ini=%s\n",
            cfg.cache_kind == APP_CACHE_DAT1 ? "dat1" : "dat2",
            cfg.cache_dir,
            cfg.revconfig_ui_ini,
            cfg.revconfig_cache_ini ? cfg.revconfig_cache_ini : "(none)");
    else
        fprintf(
            stderr,
            "torirs: %s cache=%s iface=%d\n",
            cfg.cache_kind == APP_CACHE_DAT1 ? "dat1" : "dat2",
            cfg.cache_dir,
            cfg.interface_id);

    /* TORIRS_ROOT_SIZE=WxH: host the interface at the gameframe slot the client
     * would give it instead of the full canvas. Interfaces size themselves from
     * if_getheight() on their own root, so e.g. bank 12's settings page only
     * lays out correctly at the fixed-mode modal slot (~334 tall) — at the full
     * 503 canvas its centred rows slide down onto its absolute-positioned
     * buttons. Must be set before App_Init: the open path lays out immediately. */
    if( getenv("TORIRS_ROOT_SIZE") )
    {
        char* root_size_sep = NULL;
        long root_w = strtol(getenv("TORIRS_ROOT_SIZE"), &root_size_sep, 10);
        long root_h = root_size_sep && *root_size_sep ? strtol(root_size_sep + 1, NULL, 10) : 0;
        UITree_LayoutSetRootSize((int)root_w, (int)root_h);
        fprintf(stderr, "root_size: %dx%d\n", UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    }
    /* `[ui:boot] window` / --window: the stated boot size. Same slot as the
     * debug knob above and deliberately after it, so TORIRS_ROOT_SIZE keeps
     * winning; the window is created from the layout root a few hundred lines
     * down, so setting it here is what makes the WINDOW that size too.
     *
     * The test is "not fixed", not "== resizable": `cfg.window_mode` is 0 when
     * nobody stated a mode, and unstated means the host's own default, which is
     * resizable. Testing for resizable here made a plain `--window 1440x900`
     * boot silently at 765x503 — the mode it would run in and the mode it was
     * checked against were not the same value. Fixed mode ignores the size
     * either way: the canvas is pinned back to the fixed frame when the mode is
     * applied to the platform. */
    else if( cfg.window_w > 0 && cfg.window_h > 0 &&
             cfg.window_mode != CS2VM_WINDOW_MODE_FIXED )
    {
        UITree_LayoutSetRootSize(cfg.window_w, cfg.window_h);
    }

#if !defined(TORIRS_PLATFORM_WEB)
    if( executor_prepare_js5_cache() != 0 )
        return 1;
#endif
    App_Init(&app, &cfg);
#if defined(TORIRS_WEB_CACHE_IDB)
    if( executor_attach_js5_web() != 0 )
    {
        App_Shutdown(&app);
        return 1;
    }
#elif !defined(TORIRS_PLATFORM_WEB)
    if( executor_attach_and_prime_js5() != 0 )
    {
        App_Shutdown(&app);
        return 1;
    }
#endif
    TorirsPerf_Init(0);
    /* Before anything can read it: App_Init has already run RS_CS2Host_Init,
     * whose default the manifest is entitled to override, and the root
     * interface's own scripts (opened on the next line) call getwindowmode. */
    App_SetBootWindowMode(&app, cfg.window_mode);
    App_OpenRootInterface(&app, cfg.interface_id);

    /* Boot is fully async (App_RunOnce pumps it; App_Render shows a loading
     * bar). The headless harness/debug paths below inspect the freshly built
     * tree synchronously, so pump the boot to completion for them; the plain
     * interactive run skips this and renders the loading state instead. */
    if( write_bmp || getenv("TORIRS_WORLD_NODE_DEBUG") || getenv("TORIRS_SIM_CLICK") ||
        getenv("TORIRS_SIM_KEYS") || getenv("TORIRS_SIM_WORLD_KEY") ||
        getenv("TORIRS_SIM_MOUSE_CLICK") || getenv("TORIRS_DUMP_EMIT") ||
        getenv("TORIRS_DUMP_TREE") || getenv("TORIRS_WORLD_BMP") ||
        getenv("TORIRS_CMD_REPLAY") )
        App_BootWait(&app);

    /* TORIRS_WORLD_NODE_DEBUG=1: world viewport node state + root sibling
     * chain (the emit walk draws the chain in order). idx=-1 means the opened
     * interface has no viewport, so the world is intentionally not loaded;
     * client_code=1337 confirms a cache CONTENT_WORLD layer was the source. */
    if( getenv("TORIRS_WORLD_NODE_DEBUG") )
    {
        int32_t widx = App_WorldNodeIndex(&app);
        fprintf(stderr, "world node idx=%d\n", widx);
        if( widx >= 0 )
        {
            struct UITreeComponent const* wc = &app.tree->components[widx];
            fprintf(
                stderr,
                "world node: com=0x%08x client_code=%d type=%d hide=%d trans=%d freed=%d "
                "parent=%d next_sib=%d\n",
                wc->component_id,
                wc->behavior.client_code,
                (int)wc->type,
                (int)wc->behavior.hide,
                (int)wc->trans,
                (int)wc->freed,
                wc->parent,
                wc->next_sibling);
        }
        fprintf(stderr, "root chain:");
        for( int32_t r = app.tree->root_index; r >= 0; r = app.tree->components[r].next_sibling )
            fprintf(stderr, " 0x%08x", app.tree->components[r].component_id);
        fprintf(stderr, "\n");
    }

    /* TORIRS_SIM_CLICK=<component_id>: dispatch that component's on_click hook
     * right after open — headless repro for click-triggered scripts. */
    char const* sim_click_cursor = getenv("TORIRS_SIM_CLICK");
    while( sim_click_cursor && *sim_click_cursor && app.tree )
    {
        char* sim_click_end = NULL;
        int com_id = (int)strtol(sim_click_cursor, &sim_click_end, 0);
        int32_t idx;
        sim_click_cursor = (sim_click_end && *sim_click_end == ',') ? sim_click_end + 1 : NULL;
        idx = UITree_FindByComponentId(app.tree, com_id);
        if( idx >= 0 )
        {
            struct UITreeRuntimeScriptHook hook =
                UITree_Hooks(&app.tree->components[idx])->on_click;
            if( hook.script_id <= 0 )
                hook = UITree_Hooks(&app.tree->components[idx])->on_op;
            fprintf(stderr, "sim_click: com=0x%x script=%d\n", com_id, hook.script_id);
            RS_CS2_DispatchHook(&app.host, &app.runner, com_id, &hook);
        }
        else
            fprintf(stderr, "sim_click: component 0x%x not found\n", com_id);

        /* Post-click processing mirrors App_RunOnce: transmit pump + logic
         * ticks, where scripts queued by the click actually run. */
        {
            struct LibToriRS_Input sim_input_storage;
            struct LibToriRS_Input* sim_input = LibToriRS_Input_Init(&sim_input_storage, 0);
            uint64_t sim_ms = 1;
            int sim_ticks =
                getenv("TORIRS_SIM_TICKS") ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0) : 25;
            for( int t = 0; t < sim_ticks; t++ )
            {
                LibToriRS_Input_Begin(sim_input, sim_ms);
                if( getenv("TORIRS_SIM_MOUSE") )
                {
                    char* mouse_sep = NULL;
                    long mx = strtol(getenv("TORIRS_SIM_MOUSE"), &mouse_sep, 0);
                    long my = mouse_sep && *mouse_sep == ',' ? strtol(mouse_sep + 1, NULL, 0) : 0;
                    LibToriRS_Input_PushMouseMove(sim_input, (int)mx, (int)my);
                }
                LibToriRS_Input_End(sim_input);
                if( App_RunOnce(&app, sim_ms, sim_input) )
                    sim_render_frame(&app);
                sim_ms += 20;
            }
            fprintf(stderr, "sim_click: post-click ticks done\n");
        }
    }

    /* TORIRS_SIM_MOUSE_CLICK=x,y[,right]: press+release a real mouse button
     * through the input layer and run App_RunOnce ticks — headless repro for
     * pointer-driven paths (click cross, minimenu open/select). Repeatable:
     * "x,y;x2,y2,right" runs each click in order. */
    {
        char const* sim_mc = getenv("TORIRS_SIM_MOUSE_CLICK");
        struct LibToriRS_Input mc_input_storage;
        struct LibToriRS_Input* mc_input = NULL;
        uint64_t mc_ms = 1;
        while( sim_mc && *sim_mc && app.tree )
        {
            char* sep = NULL;
            int mcx = (int)strtol(sim_mc, &sep, 0);
            int mcy = sep && *sep == ',' ? (int)strtol(sep + 1, &sep, 0) : 0;
            enum LibToriRS_MouseButton button = TORIRSM_LEFT;
            if( sep && *sep == ',' && strncmp(sep + 1, "right", 5) == 0 )
            {
                button = TORIRSM_RIGHT;
                sep += 1 + 5;
            }
            sim_mc = (sep && *sep == ';') ? sep + 1 : NULL;

            if( !mc_input )
                mc_input = LibToriRS_Input_Init(&mc_input_storage, 0);

            fprintf(
                stderr,
                "sim_mouse_click: %s at %d,%d\n",
                button == TORIRSM_RIGHT ? "right" : "left",
                mcx,
                mcy);
            LibToriRS_Input_Begin(mc_input, mc_ms);
            LibToriRS_Input_PushMouseMove(mc_input, mcx, mcy);
            LibToriRS_Input_PushMouseDown(mc_input, button, mcx, mcy);
            LibToriRS_Input_End(mc_input);
            if( App_RunOnce(&app, mc_ms, mc_input) )
                sim_render_frame(&app);
            mc_ms += 20;

            LibToriRS_Input_Begin(mc_input, mc_ms);
            LibToriRS_Input_PushMouseUp(mc_input, button, mcx, mcy);
            LibToriRS_Input_End(mc_input);
            if( App_RunOnce(&app, mc_ms, mc_input) )
                sim_render_frame(&app);
            mc_ms += 20;

            {
                int mc_ticks = getenv("TORIRS_SIM_TICKS")
                                   ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0)
                                   : 5;
                for( int t = 0; t < mc_ticks; t++ )
                {
                    LibToriRS_Input_Begin(mc_input, mc_ms);
                    LibToriRS_Input_End(mc_input);
                    if( App_RunOnce(&app, mc_ms, mc_input) )
                        sim_render_frame(&app);
                    mc_ms += 20;
                }
            }
        }
    }

    /*
     * TORIRS_PICK_SWEEP="x0,y0,x1,y1[,step]": the world analogue of
     * TORIRS_HOVER_PROBE below. That one measures UI hitboxes; this moves the
     * pointer over a grid and renders once per point so the raster reports what
     * world geometry actually covers each pixel (pair with TORIRS_PICK_DEBUG).
     *
     * This is the only way to ask "is this loc drawn over its own tile" without
     * eyeballing a screenshot: every other diagnostic reports what the BUILD
     * decided, and a loc placed correctly but drawn wrong is indistinguishable
     * from one placed wrong until you compare a loc's pick region against the
     * terrain picks at the same pixels.
     *
     * One render per point, so a 50x40 grid is 2000 frames — start coarse.
     */
    if( getenv("TORIRS_PICK_SWEEP") )
    {
        char* ps_sep = NULL;
        char const* ps = getenv("TORIRS_PICK_SWEEP");
        int px0 = (int)strtol(ps, &ps_sep, 0);
        int py0 = ps_sep && *ps_sep == ',' ? (int)strtol(ps_sep + 1, &ps_sep, 0) : 0;
        int px1 = ps_sep && *ps_sep == ',' ? (int)strtol(ps_sep + 1, &ps_sep, 0) : px0;
        int py1 = ps_sep && *ps_sep == ',' ? (int)strtol(ps_sep + 1, &ps_sep, 0) : py0;
        int pstep = ps_sep && *ps_sep == ',' ? (int)strtol(ps_sep + 1, &ps_sep, 0) : 8;
        struct LibToriRS_Input ps_storage;
        struct LibToriRS_Input* ps_input = LibToriRS_Input_Init(&ps_storage, 0);
        uint64_t ps_ms = 1;

        if( pstep < 1 )
            pstep = 1;
        for( int py = py0; py <= py1; py += pstep )
        {
            for( int pxi = px0; pxi <= px1; pxi += pstep )
            {
                fprintf(stderr, "pick_sweep: %d,%d\n", pxi, py);
                LibToriRS_Input_Begin(ps_input, ps_ms);
                LibToriRS_Input_PushMouseMove(ps_input, pxi, py);
                LibToriRS_Input_End(ps_input);
                App_RunOnce(&app, ps_ms, ps_input);
                sim_render_frame(&app);
                ps_ms += 20;
            }
        }
    }

    /* TORIRS_HOVER_PROBE="x0,y0,x1,y1[,step]": sweep the rect and print the
     * component id the hover walk resolves at each point (the IF1 overlayer
     * redirect included). Pair with TORIRS_SIM_MOUSE_CLICK to open the tab
     * first — this is how you measure a hitbox instead of eyeballing it. */
    if( getenv("TORIRS_HOVER_PROBE") && app.tree )
    {
        char* hp_sep = NULL;
        char const* hp = getenv("TORIRS_HOVER_PROBE");
        int hx0 = (int)strtol(hp, &hp_sep, 0);
        int hy0 = hp_sep && *hp_sep == ',' ? (int)strtol(hp_sep + 1, &hp_sep, 0) : 0;
        int hx1 = hp_sep && *hp_sep == ',' ? (int)strtol(hp_sep + 1, &hp_sep, 0) : hx0;
        int hy1 = hp_sep && *hp_sep == ',' ? (int)strtol(hp_sep + 1, &hp_sep, 0) : hy0;
        int hstep = hp_sep && *hp_sep == ',' ? (int)strtol(hp_sep + 1, &hp_sep, 0) : 1;
        if( hstep < 1 )
            hstep = 1;
        for( int hy = hy0; hy <= hy1; hy += hstep )
        {
            fprintf(stderr, "hover_probe y=%3d:", hy);
            for( int hx = hx0; hx <= hx1; hx += hstep )
                fprintf(
                    stderr,
                    " %d",
                    UITree_FindHoveredComponentIdForRegion(
                        app.tree, &app.ui_host, -1, hx, hy, 0, 0, UITREE_LAYOUT_ROOT_W,
                        UITREE_LAYOUT_ROOT_H));
            fprintf(stderr, "\n");
        }
    }

    /* TORIRS_SIM_KEYS=c49,c50,k85: feed one key event per simulated tick through
     * the real InteractFrame -> onKey broadcast path. Tokens are `k<n>` for an
     * OSRS internal key code (so output lines up with script sources and
     * TORIRS_DUMP_HOOKS) and `c<n>` for a character code — see
     * struct LibToriRS_KeyEvent for why those are two distinct event shapes.
     * Headless repro for onKey handlers; pair with TORIRS_DUMP_HOOKS=1 to find
     * components carrying one. */
    if( getenv("TORIRS_SIM_KEYS") && app.tree )
    {
        char const* sk_cursor = getenv("TORIRS_SIM_KEYS");
        struct LibToriRS_Input sk_storage;
        struct LibToriRS_Input* sk_input = LibToriRS_Input_Init(&sk_storage, 0);
        uint64_t sk_ms = 1;
        /* onKey scripts commonly re-register hooks and queue transmits that only
         * settle on later logic ticks, so keep ticking after the last key. */
        int sk_tail_ticks =
            getenv("TORIRS_SIM_TICKS") ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0) : 10;

        /* An OSRS-coded key pressed on the previous iteration, still to be
         * released. A real keydown/keyup pair drives both the event queue and
         * the held/pressed arrays (see the SDL handler); without the release
         * the key would read as held forever. */
        int sk_held_key = -1;

        for( ;; )
        {
            LibToriRS_Input_Begin(sk_input, sk_ms);
            if( sk_held_key >= 0 )
            {
                LibToriRS_Input_SetOsrsKeyState(sk_input, sk_held_key, 0, 0);
                sk_held_key = -1;
            }
            if( sk_cursor && *sk_cursor )
            {
                char kind = *sk_cursor++;
                char* sk_end = NULL;
                long val = strtol(sk_cursor, &sk_end, 0);
                sk_cursor = (sk_end && *sk_end == ',') ? sk_end + 1 : NULL;
                if( kind == 'c' )
                    LibToriRS_Input_PushKeyEvent(sk_input, -1, (int)val, 0);
                else
                {
                    LibToriRS_Input_PushKeyEvent(sk_input, (int)val, 0, 0);
                    /* k<n> already IS an OSRS code, so it can drive KEYHELD /
                     * KEYPRESSED and the revconfig hotkey bindings too — both
                     * read the same arrays a real press fills. */
                    LibToriRS_Input_SetOsrsKeyState(sk_input, (int)val, 1, 1);
                    sk_held_key = (int)val;
                }
                fprintf(stderr, "sim_keys: %c%ld\n", kind, val);
            }
            else if( sk_tail_ticks-- <= 0 )
                break;
            LibToriRS_Input_End(sk_input);
            (void)App_RunOnce(&app, sk_ms, sk_input);
            sk_ms += 20;
        }
        fprintf(stderr, "sim_keys: done\n");
    }

    /* TORIRS_SIM_WORLD_KEY=x,y,<char>[;...]: move the mouse to (x,y), run a
     * couple frames so the world hover pick latches the tile, then press the
     * key (letters/digits) through the real input layer. '!' right-clicks and
     * '.' left-clicks instead of pressing a key, so one run can spawn (9/8/0)
     * and then open/use the world minimenu on the result. Headless driver for
     * the hover-gated world hotkeys. */
    {
        char const* swk = getenv("TORIRS_SIM_WORLD_KEY");
        struct LibToriRS_Input swk_storage;
        struct LibToriRS_Input* swk_input = NULL;
        uint64_t swk_ms = 1;
        while( swk && *swk && app.tree )
        {
            char* sep = NULL;
            int wkx = (int)strtol(swk, &sep, 0);
            int wky = sep && *sep == ',' ? (int)strtol(sep + 1, &sep, 0) : 0;
            char key_char = (sep && *sep == ',') ? sep[1] : '\0';
            enum LibToriRS_KeyCode key = TORIRSK_UNKNOWN;
            if( key_char >= 'a' && key_char <= 'z' )
                key = (enum LibToriRS_KeyCode)(TORIRSK_A + (key_char - 'a'));
            else if( key_char >= '0' && key_char <= '9' )
                key = (enum LibToriRS_KeyCode)(TORIRSK_0 + (key_char - '0'));
            sep = key_char ? sep + 2 : sep;
            swk = (sep && *sep == ';') ? sep + 1 : NULL;

            if( !swk_input )
                swk_input = LibToriRS_Input_Init(&swk_storage, 0);

            fprintf(stderr, "sim_world_key: '%c' at %d,%d\n", key_char ? key_char : '?', wkx, wky);
            for( int t = 0; t < 2; t++ )
            {
                LibToriRS_Input_Begin(swk_input, swk_ms);
                LibToriRS_Input_PushMouseMove(swk_input, wkx, wky);
                LibToriRS_Input_End(swk_input);
                if( App_RunOnce(&app, swk_ms, swk_input) )
                    sim_render_frame(&app);
                swk_ms += 20;
            }
            {
                enum LibToriRS_MouseButton btn = key_char == '!'   ? TORIRSM_RIGHT
                                                 : key_char == '.' ? TORIRSM_LEFT
                                                                   : TORIRSM_UNKNOWN;
                /* A real keydown fills the OSRS-coded arrays as well as the
                 * platform-neutral ones (see the SDL handler), and the digit
                 * row is bound to sidebar tabs in rev 254 — without this the
                 * simulated press could only ever reach the debug spawn keys,
                 * never the hotkey that shadows them. */
                char osrs_name[2] = { key_char, '\0' };
                int osrs_key = LibToriRS_OsrsKeyFromName(osrs_name);

                LibToriRS_Input_Begin(swk_input, swk_ms);
                if( key != TORIRSK_UNKNOWN )
                    LibToriRS_Input_PushKeyDown(swk_input, key);
                if( key != TORIRSK_UNKNOWN && osrs_key >= 0 )
                    LibToriRS_Input_SetOsrsKeyState(swk_input, osrs_key, 1, 1);
                if( btn != TORIRSM_UNKNOWN )
                    LibToriRS_Input_PushMouseDown(swk_input, btn, wkx, wky);
                LibToriRS_Input_End(swk_input);
                if( App_RunOnce(&app, swk_ms, swk_input) )
                    sim_render_frame(&app);
                swk_ms += 20;
                LibToriRS_Input_Begin(swk_input, swk_ms);
                if( key != TORIRSK_UNKNOWN )
                    LibToriRS_Input_PushKeyUp(swk_input, key);
                if( key != TORIRSK_UNKNOWN && osrs_key >= 0 )
                    LibToriRS_Input_SetOsrsKeyState(swk_input, osrs_key, 0, 0);
                if( btn != TORIRSM_UNKNOWN )
                    LibToriRS_Input_PushMouseUp(swk_input, btn, wkx, wky);
                LibToriRS_Input_End(swk_input);
                if( App_RunOnce(&app, swk_ms, swk_input) )
                    sim_render_frame(&app);
                swk_ms += 20;
            }
            {
                int wk_ticks = getenv("TORIRS_SIM_TICKS")
                                   ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0)
                                   : 5;
                for( int t = 0; t < wk_ticks; t++ )
                {
                    LibToriRS_Input_Begin(swk_input, swk_ms);
                    LibToriRS_Input_PushMouseMove(swk_input, wkx, wky);
                    LibToriRS_Input_End(swk_input);
                    if( App_RunOnce(&app, swk_ms, swk_input) )
                        sim_render_frame(&app);
                    swk_ms += 20;
                }
            }
        }
    }

    /* TORIRS_SIM_CAMERA_YAW=<0..2047>: park the camera at a yaw and run a frame,
     * so the compass/minimap can be screenshotted at known angles. The in-app
     * yaw keys are the arrows, which the key sim above cannot send. */
    if( getenv("TORIRS_SIM_CAMERA_YAW") )
    {
        struct LibToriRS_Input yaw_storage;
        struct LibToriRS_Input* yaw_input = LibToriRS_Input_Init(&yaw_storage, 0);
        uint64_t yaw_ms = 1;
        /* First frame lands the lazy world load, which resets the camera; only
         * then is it worth parking the yaw. */
        for( int frame = 0; frame < 2; frame++ )
        {
            if( frame == 1 )
            {
                app.world_camera.yaw =
                    ToriDraw_NormalizeAngle((int)strtol(getenv("TORIRS_SIM_CAMERA_YAW"), NULL, 0));
                fprintf(stderr, "sim_camera_yaw: %d\n", app.world_camera.yaw);
            }
            LibToriRS_Input_Begin(yaw_input, yaw_ms);
            LibToriRS_Input_End(yaw_input);
            if( App_RunOnce(&app, yaw_ms, yaw_input) )
                sim_render_frame(&app);
            yaw_ms += 20;
        }
    }

    /* TORIRS_DUMP_OPKEYS=1: print every op-key binding CS2 installed, so a
     * keyboard shortcut that does not fire can be traced to either a missing
     * binding or a missing match. */
    if( getenv("TORIRS_DUMP_OPKEYS") && app.tree )
    {
        for( uint32_t ki = 0; ki < app.tree->component_count; ki++ )
        {
            struct UITreeComponent const* c = &app.tree->components[ki];
            if( c->freed || !UITree_OpKeys(c)->has_bindings )
                continue;
            for( int slot = 0; slot < UITREE_OPKEY_SLOTS; slot++ )
            {
                struct UITreeOpKeyBinding const* b = &UITree_OpKeys(c)->slots[slot];
                if( !b->bound )
                    continue;
                fprintf(
                    stderr,
                    "OPKEYDUMP com=0x%08x op=%d pairs=%d key0=(char=%d,code=%d) "
                    "rate=%d/%d ignore_held=%d on_op=%d\n",
                    c->component_id,
                    slot + 1,
                    b->pair_count,
                    b->key_chars[0],
                    b->key_codes[0],
                    b->rate,
                    b->rate_enabled,
                    b->ignore_held,
                    UITree_Hooks(c)->on_op.script_id);
            }
        }
    }

    /* TORIRS_DUMP_OPS=1: print every node carrying menu option/op strings —
     * verifies cache-config option threading onto the tree. */
    if( getenv("TORIRS_DUMP_OPS") && app.tree )
    {
        for( uint32_t oi = 0; oi < app.tree->component_count; oi++ )
        {
            struct UITreeComponent const* c = &app.tree->components[oi];
            struct UITreeMenuOptions const* mo = UITree_MenuOptions(c);
            int has_ops = mo->option[0] != '\0';
            for( int s = 0; s < UITREE_MENU_OPTION_SLOTS; s++ )
                if( mo->ops[s][0] != '\0' )
                    has_ops = 1;
            if( c->freed || !has_ops )
                continue;
            fprintf(
                stderr,
                "OPSDUMP com=0x%08x option=\"%s\" ops=[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]\n",
                c->component_id,
                mo->option,
                mo->ops[0],
                mo->ops[1],
                mo->ops[2],
                mo->ops[3],
                mo->ops[4]);
        }
    }

    if( getenv("TORIRS_DUMP_TREE") && app.tree )
        dump_tree(&app, cfg.interface_id);

    /* TEMP DEBUG: dump runtime hook script ids (TORIRS_DUMP_HOOKS=1) */
    if( getenv("TORIRS_DUMP_HOOKS") && app.tree )
        dump_hooks(&app);

    /* TORIRS_EMIT_SKIP=<component_id>: drop that component's draw commands from
     * the frame before rasterizing — diffing the two BMPs shows exactly which
     * pixels it owns (or that it is fully overdrawn). */
    if( getenv("TORIRS_EMIT_SKIP") )
    {
        int skip_com = (int)strtol(getenv("TORIRS_EMIT_SKIP"), NULL, 0);
        int kept = 0;
        for( int i = 0; i < app.emit.count; i++ )
        {
            if( app.emit.cmds[i].component_id == skip_com )
                continue;
            app.emit.cmds[kept++] = app.emit.cmds[i];
        }
        fprintf(stderr, "emit_skip: com=0x%x dropped %d cmds\n", skip_com, app.emit.count - kept);
        app.emit.count = kept;
    }

    if( write_bmp )
    {
        char path[256];
        /* A RevConfig run has no single interface id to name the file after. */
        if( cfg.revconfig_ui_ini )
            snprintf(path, sizeof(path), "build/revconfig.bmp");
        else
            snprintf(path, sizeof(path), "build/interface_%d.bmp", cfg.interface_id);
        if( App_WriteBmp(&app, path, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) == 0 )
            printf("wrote %s (%d cmds)\n", path, app.emit.count);
        else
            fprintf(stderr, "failed to write %s\n", path);
    }

    /* TORIRS_DUMP_LAYOUT=1: raw layout inputs + resolved box per component.
     * Neither our dump_tree nor the reference widgetTreeDump prints abs= for
     * layers, so this is the only way to see where a container's resolved box
     * (and therefore every mode-!=0 child under it) goes wrong. Deliberately a
     * separate format so dump_tree stays byte-comparable with the reference. */
    if( getenv("TORIRS_DUMP_LAYOUT") && app.tree )
    {
        for( uint32_t li = 0; li < app.tree->component_count; li++ )
        {
            struct UITreeComponent const* c = &app.tree->components[li];
            struct UITreeElemPosition const* pos = &c->position;
            int parent_w = UITREE_LAYOUT_ROOT_W;
            int parent_h = UITREE_LAYOUT_ROOT_H;
            int parent_id = -1;
            if( c->freed )
                continue;
            if( c->parent >= 0 && (uint32_t)c->parent < app.tree->component_count )
            {
                struct UITreeComponent const* parent = &app.tree->components[c->parent];
                parent_id = parent->component_id;
                parent_w = parent->position.abs_w;
                parent_h = parent->position.abs_h;
                /* Same scroll-content substitution UITree_LayoutResolve applies. */
                if( parent->type == UIELEM_RS_LAYER )
                {
                    if( parent->u.rs_layer.scroll_width > 0 )
                        parent_w = parent->u.rs_layer.scroll_width;
                    if( parent->u.rs_layer.scroll_height > 0 )
                        parent_h = parent->u.rs_layer.scroll_height;
                }
            }
            fprintf(
                stderr,
                "LAYOUT com=0x%08x type=%d if3=%d parent=0x%08x pwh=%dx%d "
                "raw=%d,%d %dx%d modes=x%d,y%d,w%d,h%d abs=%d,%d %dx%d\n",
                c->component_id,
                (int)c->type,
                (int)c->if3,
                parent_id,
                parent_w,
                parent_h,
                pos->x,
                pos->y,
                pos->width,
                pos->height,
                (int)pos->x_mode,
                (int)pos->y_mode,
                (int)pos->width_mode,
                (int)pos->height_mode,
                pos->abs_x,
                pos->abs_y,
                pos->abs_w,
                pos->abs_h);
        }
    }

    /* TORIRS_DUMP_ORDER=1: walk every parent's child list in LINK order (which is
     * what emit/draw uses) and flag where dynamic_child_index goes backwards —
     * those are the places creation order and OSRS childIndex order disagree. */
    if( getenv("TORIRS_DUMP_ORDER") && app.tree )
    {
        for( uint32_t p = 0; p < app.tree->component_count; p++ )
        {
            struct UITreeComponent const* parent = &app.tree->components[p];
            int32_t child;
            int prev_sub = -1;
            int inverted = 0;
            if( parent->freed || parent->first_child < 0 )
                continue;
            for( child = parent->first_child; child >= 0;
                 child = app.tree->components[child].next_sibling )
            {
                struct UITreeComponent const* cc = &app.tree->components[child];
                if( !cc->dynamic )
                    continue;
                if( cc->dynamic_child_index < prev_sub )
                    inverted = 1;
                prev_sub = cc->dynamic_child_index;
            }
            if( !inverted )
                continue;
            fprintf(stderr, "ORDER parent=0x%08x link order:", parent->component_id);
            for( child = parent->first_child; child >= 0;
                 child = app.tree->components[child].next_sibling )
            {
                struct UITreeComponent const* cc = &app.tree->components[child];
                fprintf(
                    stderr,
                    " %s(0x%08x,sub=%d)",
                    cc->dynamic ? "dyn" : "sta",
                    cc->component_id,
                    cc->dynamic ? cc->dynamic_child_index : -1);
            }
            fprintf(stderr, "\n");
        }
    }

    if( getenv("TORIRS_DUMP_EMIT") )
    {
        for( int i = 0; i < app.emit.count; i++ )
        {
            struct UITreeEmitDesc* d = &app.emit.cmds[i];
            fprintf(
                stderr,
                "EMIT[%d] kind=%d com=0x%08x x=%d y=%d w=%d h=%d scene=%d color=0x%06x "
                "filled=%d trans=%d tiled=%d clip=%d,%d %dx%d\n",
                i,
                (int)d->kind,
                d->component_id,
                d->x,
                d->y,
                d->w,
                d->h,
                d->scene_id,
                d->color,
                d->filled,
                d->trans,
                d->tiled,
                d->clip.x,
                d->clip.y,
                d->clip.w,
                d->clip.h);
        }
    }

    /* TORIRS_WORLD_BMP=1: full App_Render frame (App_WriteBmp is 2D-only, so
     * the 3D world pass never reaches it) to build/world.bmp, then exit —
     * headless end-to-end check of the world load + render pipeline. */
    if( getenv("TORIRS_WORLD_BMP") )
    {
        int* pixels = calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
        assert(pixels);
        /* TORIRS_TEX_AUDIT=1: after the boot settles, sweep every live scene
         * element for face texture ids the scene texture map still lacks — the
         * ground truth for "this face renders untextured". */
        if( getenv("TORIRS_TEX_AUDIT") )
        {
            int ids[512];
            int n = UITreeSceneBridge_CollectMissingTextures(&app.bridge, ids, 512);
            fprintf(stderr, "TEX_AUDIT: %d missing scene textures:", n);
            for( int i = 0; i < n; i++ )
                fprintf(stderr, " %d", ids[i]);
            fprintf(stderr, "\n");
            fprintf(stderr, "TEX_AUDIT: failed:");
            for( int i = 0; i < 2048; i++ )
                if( app.bridge.texture_failed[i] )
                    fprintf(stderr, " %d", i);
            fprintf(stderr, "\n");
        }
        /* TORIRS_TEST_LOCCHANGE=1: exercise the runtime loc-change path offline
         * (debugging the door segfault) by re-applying a change to the first
         * existing scenery loc in the scene. */
        if( getenv("TORIRS_TEST_LOCCHANGE") && app.world_builder && app.world )
        {
            struct World_EntityPool* pool = &app.world->entities.scenery;
            int applied = 0, walls = 0;
            /* Snapshot every existing loc first (ApplyLocChange mutates the pool
             * as we go), then re-apply a change to each shape to exercise the
             * whole scenery_add path — walls (doors) included. */
            struct { int x, z, l, id, shape, angle; } locs[4096];
            int nlocs = 0;
            for( int it = World_EntityPoolHead(pool);
                 it != WORLD_ENTITY_NIL && nlocs < 4096;
                 it = World_EntityPoolNext(pool, it) )
            {
                struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, it);
                if( !sc )
                    continue;
                locs[nlocs].x = sc->grid_position.x;
                locs[nlocs].z = sc->grid_position.z;
                locs[nlocs].l = sc->grid_position.level;
                locs[nlocs].id = sc->loc_id;
                locs[nlocs].shape = sc->shape;
                locs[nlocs].angle = sc->angle;
                nlocs++;
            }
            for( int k = 0; k < nlocs; k++ )
            {
                if( locs[k].shape <= 3 )
                    walls++;
                WorldBuilder_ApplyLocChange(app.world_builder, locs[k].x, locs[k].z,
                                            locs[k].l, locs[k].id, locs[k].shape,
                                            locs[k].angle);
                applied++;
            }
            fprintf(stderr, "TEST_LOCCHANGE: applied %d loc changes (%d walls) ok\n",
                    applied, walls);
        }
        App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        bmp_write_file("build/world.bmp", pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        printf("wrote build/world.bmp (%d emit cmds)\n", app.emit.count);
        free(pixels);
        App_Shutdown(&app);
        return 0;
    }

    {
        char title[64];

        sdl = PlatformSDL2_New();

        snprintf(title, sizeof(title), "torirs iface=%d", cfg.interface_id);
        if( !sdl )
        {
            fprintf(stderr, "window platform alloc failed\n");
            App_Shutdown(&app);
            return 1;
        }
#if defined(TORIRS_HAVE_GL3)
        if( use_opengl3 )
        {
            if( !PlatformSDL2_InitForOpenGL3(
                    sdl, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, title) )
            {
                fprintf(stderr, "SDL OpenGL3 init failed\n");
                PlatformSDL2_Free(sdl);
                App_Shutdown(&app);
                return 1;
            }
            gl3 = ToriRS_GL3_New(UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            if( !gl3 ||
                !ToriRS_GL3_Init(
                    gl3, PlatformSDL2_Window(sdl), app.scene, gl3_zbuffer != 0) )
            {
                fprintf(stderr, "GL3 renderer init failed\n");
                ToriRS_GL3_Free(gl3);
                PlatformSDL2_Free(sdl);
                App_Shutdown(&app);
                return 1;
            }
            /* The depth pass needs the app to stop collecting the visible set
             * through the tile wavefront and the opaque face-distance sort;
             * that is what TORIRS_WORLD_DEPTH selects. Same contract as D3D9. */
            App_SetWorldRenderMode(
                &app, gl3_zbuffer ? TORIRS_WORLD_DEPTH : TORIRS_WORLD_PAINTER);
        }
        else
#else
        /* No desktop-GL renderer in this build; --opengl3 was rejected during
         * argument parsing, so this is unreachable rather than ignored. */
        (void)use_opengl3;
#endif
        if( !PlatformSDL2_Init(sdl, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, title) )
        {
            fprintf(stderr, "window init failed\n");
            PlatformSDL2_Free(sdl);
            App_Shutdown(&app);
            return 1;
        }

#if defined(TORIRS_HAVE_D3D9)
        if( use_d3d9 )
        {
            d3d9 = ToriRS_D3D9_New(UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            if( !d3d9 ||
                !ToriRS_D3D9_Init(
                    d3d9,
                    PlatformSDL2_NativeWindowHandle(sdl),
                    app.scene,
                    d3d9_zbuffer != 0) )
            {
                fprintf(
                    stderr,
                    "D3D9 fixed-function renderer init failed; falling back to GDI Soft3D\n");
                ToriRS_D3D9_Free(d3d9);
                d3d9 = NULL;
            }
            else
                App_SetWorldRenderMode(
                    &app,
                    d3d9_zbuffer ? TORIRS_WORLD_DEPTH : TORIRS_WORLD_PAINTER);
        }
#else
        (void)use_d3d9;
        (void)d3d9_zbuffer;
#endif

        CmdBus_Init(&bus);

        /*
         * Hand the window mode to the platform ONCE, at boot.
         *
         * Without this the two halves of "resizable" disagree for the whole
         * session: RS_CS2Host_Init starts the host in resizable and every
         * clientscript is told so by getwindowmode, while the platform's follow
         * gate starts clear — so the window letterboxes and UPSCALES a 765x503
         * canvas instead of the client laying the gameframe out at the window
         * size. That is the "resizable mode scales instead of resizing" bug; it
         * is a missing boot-time read, not a missing mechanism.
         *
         * Same call the runtime mode switch makes after the frame, so the two
         * paths cannot drift.
         */
        /*
         * Chrome at the display's own pixel density.
         *
         * The framebuffer is drawable pixels now, so a 1x chrome on a 2x
         * display would be laid out in half-size pixels -- correct, sharp, and
         * unreadably small. The fix is the BAKED 2x face, not a stretch: this
         * hands the app the density and every chrome metric follows it.
         *
         * TORIRS_CHROME_SCALE overrides, for working on scaled chrome from an
         * ordinary display (and for pinning the size a screenshot test wants).
         */
        {
            int density = PlatformSDL2_PixelDensity(sdl);
            char const* forced = getenv("TORIRS_CHROME_SCALE");
            if( forced && forced[0] )
                density = atoi(forced);
            App_SetChromeScale(&app, density);
            if( getenv("TORIRS_RESIZE_DEBUG") )
                fprintf(
                    stderr,
                    "chrome: scale %d (display density %d)\n",
                    App_ChromeScale(&app),
                    PlatformSDL2_PixelDensity(sdl));
        }

        {
            int const boot_mode = App_WindowMode(&app);
            bool const resizable = boot_mode == CS2VM_WINDOW_MODE_RESIZABLE;
            PlatformSDL2_SetCanvasFollowsWindow(
                sdl, &bus, resizable, APP_CANVAS_MIN_W, APP_CANVAS_MIN_H);
            if( !resizable )
                CmdBus_PushWindowResize(&bus, APP_CANVAS_MIN_W, APP_CANVAS_MIN_H);
            if( getenv("TORIRS_RESIZE_DEBUG") )
                fprintf(
                    stderr,
                    "windowmode: boot %s\n",
                    CS2VM_WindowModeName(boot_mode));
        }

        /* Audio backend. Opening a device is allowed to fail — a machine with no
         * sound card, or a headless CI box, keeps running silently rather than
         * refusing to start, which is the same courtesy the renderer gets. */
        audio = PlatformAudio_New();
        if( !PlatformAudio_Init(audio, TORIRS_AUDIO_SAMPLE_RATE) )
            fprintf(stderr, "audio: no device; running silent\n");

        /* TORIRS_SIM_SONG / TORIRS_SIM_JINGLE=<id>: start a music track or a
         * jingle once the client is up. The only way to hear the synth without
         * a server, and the check that "music plays" means a speaker rather
         * than a counter. */
        if( getenv("TORIRS_SIM_SONG") )
            sim_song_id = atoi(getenv("TORIRS_SIM_SONG"));
        if( getenv("TORIRS_SIM_JINGLE") )
            sim_jingle_id = atoi(getenv("TORIRS_SIM_JINGLE"));

        if( getenv("TORIRS_SIM_SOUND") )
        {
            int parsed_id = -1;
            int parsed_loops = 1;
            int parsed_every = 0;
            int fields =
                sscanf(getenv("TORIRS_SIM_SOUND"), "%d,%d,%d",
                       &parsed_id, &parsed_loops, &parsed_every);
            if( fields >= 1 && parsed_id >= 0 )
            {
                sim_sound_id = parsed_id;
                sim_sound_loops = fields >= 2 && parsed_loops > 0 ? parsed_loops : 1;
                sim_sound_every = fields >= 3 && parsed_every > 0 ? parsed_every : 0;
            }
            else
            {
                fprintf(stderr, "TORIRS_SIM_SOUND: expected id[,loops[,every_frames]]\n");
            }
        }

        /* TORIRS_CMD_RECORD=file: tee every pushed command to a replayable
         * .trscmd file. TORIRS_CMD_REPLAY=file: drive the loop from a prior
         * recording instead of SDL events, timestamps included. */
        if( getenv("TORIRS_CMD_RECORD") )
        {
            if( !CmdBus_RecordOpen(&bus, getenv("TORIRS_CMD_RECORD")) )
                fprintf(
                    stderr, "cmdbus: cannot record to %s\n", getenv("TORIRS_CMD_RECORD"));
        }
        if( getenv("TORIRS_CMD_REPLAY") )
        {
            replay = CmdReplay_Open(getenv("TORIRS_CMD_REPLAY"));
            if( !replay )
            {
                fprintf(
                    stderr, "cmdbus: cannot replay %s\n", getenv("TORIRS_CMD_REPLAY"));
                PlatformSDL2_Free(sdl);
                App_Shutdown(&app);
                return 1;
            }
        }

        input = LibToriRS_Input_Init(&input_storage, PlatformSDL2_Ticks64());

        /* TORIRS_SEED_CHAT=N: inject N game chat lines (scroll-clipped) for
         * Soft3D / GL3 smoke comparison. */
        if( getenv("TORIRS_SEED_CHAT") )
        {
            long n = atol(getenv("TORIRS_SEED_CHAT"));
            if( n < 1 )
                n = 12;
            if( n > 40 )
                n = 40;
            for( long i = 0; i < n; i++ )
            {
                char line[96];
                snprintf(
                    line,
                    sizeof(line),
                    "Seed chat line %ld — scroll container text check",
                    i + 1);
                RS_Chat_AddMessage(&app.chat, RS_CHAT_TYPE_GAME, NULL, line);
            }
        }

        interactive_render_present(&app, sdl, gl3, d3d9);

        /* TORIRS_MAX_FRAMES=N: exit after N loop iterations (headless smoke
         * runs under SDL_VIDEODRIVER=dummy, where no quit event ever comes). */
        max_frames = getenv("TORIRS_MAX_FRAMES") ? atol(getenv("TORIRS_MAX_FRAMES")) : 0;
        frame_count = 0;

        /* TORIRS_PACE_SPIN=1: spin the 50 fps wait rather than sleeping it. */
        pace_spin = getenv("TORIRS_PACE_SPIN") && atoi(getenv("TORIRS_PACE_SPIN")) != 0;

        /* Socket transport is created only when --connect enabled networking;
         * it bridges the net subsystem's out ring to a TCP socket and pushes
         * received bytes onto the bus as NET_RECV commands. */
        /*
         * The revision decides the transport, unless the manifest overrides it.
         *
         * A rev table describes a *protocol*, so its `transport_kind` is the
         * right default and the only thing that knew about transports until
         * now — `[net:boot] transport=` was parsed into the manifest and then
         * read by nothing but its own unit test. It is honoured here.
         *
         * `embed` is the one value a revision could never supply, because it is
         * not a protocol but a deployment: the server runs in this process and
         * the two ends trade bytes through a queue pair instead of a socket.
         *
         * TORIRS_TRANSPORT=embed|tcp|ws wins over the manifest — run-live.sh
         * uses that to force the in-process server without rewriting INIs.
         */
        {
            int transport_kind = app.net ? app.net->rev->transport_kind : 0;
            const char* transport_name = boot_manifest.transport[0]
                                             ? boot_manifest.transport
                                             : NULL;
            const char* env_transport = getenv("TORIRS_TRANSPORT");

            if( env_transport && env_transport[0] )
                transport_name = env_transport;

            if( transport_name )
            {
                if( strcmp(transport_name, "embed") == 0 )
                    transport_kind = NET_TRANSPORT_EMBED;
                else if( strcmp(transport_name, "ws") == 0 )
                    transport_kind = NET_TRANSPORT_WS;
                else if( strcmp(transport_name, "tcp") == 0 )
                    transport_kind = NET_TRANSPORT_TCP;
                else
                    fprintf(stderr,
                            "torirs: unknown transport=%s — using the revision's\n",
                            transport_name);
            }

            /* One process must not quietly use two caches. The client and its
             * JS5 reader already use the manifest-resolved `cfg.cache_dir`; pass
             * that same directory through mock230's existing deployment knob
             * before the embedded world starts. An explicit MOCK230_CACHE still
             * wins, which preserves the diagnostic override. This matters for
             * isolated cache overlays: their minted npc/loc ids do not exist in
             * the pristine cache. */
            if( transport_kind == NET_TRANSPORT_EMBED && !getenv("MOCK230_CACHE") )
                setenv("MOCK230_CACHE", cfg.cache_dir, 0);
            if( transport_kind == NET_TRANSPORT_EMBED && cfg.net_server_scripts &&
                cfg.net_server_scripts[0] && !getenv("MOCK230_SCRIPTS") )
                setenv("MOCK230_SCRIPTS", cfg.net_server_scripts, 0);

            sock = app.net ? NetTransport_New(transport_kind,
                                              cfg.connect_port > 0 ? cfg.connect_port : 43594,
                                              app.net->rev->name)
                           : NULL;
        }

        /* TORIRS_SIM_OPENMAIN=<iface>: once the gameframe is up, mount an
         * interface into the main-modal slot exactly as an IF_OPENMAIN packet
         * would. Offline repro for server-driven modals (player design 3559). */
        sim_openmain = getenv("TORIRS_SIM_OPENMAIN")
                               ? (int)strtol(getenv("TORIRS_SIM_OPENMAIN"), NULL, 0)
                               : -1;
        sim_openmain_done = 0;

        /* TORIRS_SIM_OPENSIDE=<iface>: the same for the side-panel slot. The
         * main-modal slot refuses a side interface ("no mount region"), so the
         * sidebar panels — settings_side 116 and friends — are only reachable
         * offline through here. */
        sim_openside = getenv("TORIRS_SIM_OPENSIDE")
                               ? (int)strtol(getenv("TORIRS_SIM_OPENSIDE"), NULL, 0)
                               : -1;
        sim_openside_done = 0;

        /* TORIRS_SIM_OPENCHAT=<iface>: same, for the chatback dialog slot
         * (reference IF_OPENCHAT / chatComId). Offline repro for the
         * server-driven chat dialogs (option menus 2459.., npc/player chat). */
        sim_openchat = getenv("TORIRS_SIM_OPENCHAT")
                               ? (int)strtol(getenv("TORIRS_SIM_OPENCHAT"), NULL, 0)
                               : -1;
        sim_openchat_done = 0;

        /* TORIRS_BOOT_STATS=1: how long the gameframe took to come up, and
         * whether the frame loop or the work itself is the limit. `capped` is
         * the number of boot frames that used their whole per-frame scheduler
         * budget — those are frames that had more work ready and were stopped. */
        boot_stats = getenv("TORIRS_BOOT_STATS") ? 1 : 0;
        boot_start_ms = PlatformSDL2_Ticks64();
        boot_reported = 0;

        sim_sethide = getenv("TORIRS_SIM_SETHIDE");
        sim_sethide_done = 0;

        /* TORIRS_SIM_SETVARP="id:value,...": drive the varps the IF1 "active"
         * scripts read (spec energy 300/301, attack style, ...) without a
         * server, so widgets whose whole behaviour is getIfActive can be seen
         * offline. Applied every frame — a re-bake would otherwise reset them. */
        sim_setvarp = getenv("TORIRS_SIM_SETVARP");

        /* TORIRS_SIM_SETTAB="tabno:iface": replay IF_SETTAB offline, so the
         * sidebar panels the server assigns (combat tab 3796, stats, ...) can
         * be inspected without a session. */
        sim_settab = getenv("TORIRS_SIM_SETTAB");
        sim_settab_done = 0;

#if defined(__EMSCRIPTEN__)
        /* Hand the loop to requestAnimationFrame and never return: the browser
         * drives frame_loop_step from here on, and the stack below this point
         * is unwound (which is why the loop's state is at file scope).
         * Shutdown happens in frame_loop_tick when the step says stop. */
        emscripten_set_main_loop(frame_loop_tick, 0, 1);
        return 0;
#else
        while( frame_loop_step() )
        {
        }
        /* Close the final CPU calibration interval before capture/destruction. */
        TorirsPerf_Shutdown();
        frame_loop_teardown();
#endif
    }

    App_Shutdown(&app);
    TorirsPerf_Shutdown();
    return 0;
}
