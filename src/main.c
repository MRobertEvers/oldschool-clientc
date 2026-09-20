#include "app.h"
#include "boot_telemetry.h"
#include "bootmanifest/bootmanifest.h"
#include "cmd/cmdbus.h"
#include "engine/uitree_scene_bridge.h"
#include "engine/world_builder/world_builder.h"
#include "executor_config.h"
#include "game/content_test.h"
#include "game/cs2_harness.h"
#include "game/rs_chat.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_ui_slots.h"
#include "input/torirs_input.h"
#include "input/torirs_keymap.h"
#include "log/torirs_log.h"
#include "net/net.h"
#include "net/net_out.h"
#include "perf/torirs_perf.h"
#include "torirs_env.h"

#include <inttypes.h>
#if defined(TORIRS_FRAME_TIMES)
#include "../tools/perf/gles2_frame_times.h"
#endif
#include "platform/net_transport.h"
#include "platform/platform_audio.h"
#include "platform/platform_window.h"
#if !defined(TORIRS_PLATFORM_WEB)
#include "platform/platform_x_io_js5.h"
#include "platform/platform_x_io_js5_cache.h"
#endif
#if defined(TORIRS_HAVE_GL3)
/* The desktop GPU renderer, GL 3.2 core. Selected by --opengl3. */
#include "platform/platform_sdl2_renderer_gl3.h"
#else
/* Software-only builds (e.g. the Win32/GDI backend) never include the GL header,
 * so struct ToriPlatformSDL2_Renderer_GL3 needs a file-scope forward declaration -- otherwise the
 * opaque `gl3` pointer in interactive_render_present() and the file-scope `gl3`
 * are two distinct incomplete types and the call is a type error. */
struct ToriPlatformSDL2_Renderer_GL3;
#endif
#if defined(TORIRS_HAVE_D3D9)
#include "platform/platform_win32_renderer_d3d9.h"
#else
struct ToriPlatformWin32_Renderer_D3D9;
#endif
/*
 * The four GPU renderers of the ES family, two per lane, over two shared
 * cores:
 *
 *   shared core                 Android lane      browser lane
 *   3rd/trspk/es2/              --gles2           --webgl1
 *   3rd/trspk/es3/              --gles3           --webgl2
 *
 * A core is not a renderer: it is a TOOLKIT of GL calls with no test in it
 * for which world path is running. Each lane forks it into its own
 * translation units and builds TWO renderers on top -- a painter one and a
 * depth one -- which is why the flags come in pairs and why this file, not
 * a mode field, is what selects between them. @see renderer_active_is_depth.
 * Exactly two of the four families are built for any one lane, which is why
 * every block below is behind its own TORIRS_HAVE_*.
 */
#if defined(TORIRS_HAVE_GLES2)
/* Android's OpenGL ES 2.0 renderer (--gles2 / --gles2-zbuffer). */
#include "platform/platform_androidarmv7_renderer_opengles2.h"
#if defined(TORIRS_HAVE_GLES2_DUALCORE)
#include "platform/platform_androidarmv7_renderer_opengles2_dualcore.h"
#endif
#if defined(TORIRS_HAVE_GLES2_DUALCORE)
/* NULL unless --gles2-dualcore was passed: the ES2 core behind `gles2`,
 * driven through the dual-core lane (which wraps it and does not own it).
 * Android-only -- it is the second Krait core, which no browser has.
 * Declared with the includes, above the frame functions that read it. */
static struct ToriPlatformAndroid_Renderer_GLES2_DualCore* gles2_dualcore_lane;
#endif
#else
struct ToriPlatformAndroid_Renderer_GLES2;
#endif
#if defined(TORIRS_HAVE_WEBGL2)
/* The browser's OpenGL ES 3.0 renderer, on a WebGL2 context
 * (--webgl2 / --webgl2-zbuffer). Runs the shared ES3 core, which Android
 * runs as --gles3. */
#include "platform/platform_web_renderer_webgl2.h"
#else
struct ToriPlatformWeb_Renderer_WebGL2;
#endif
#if defined(TORIRS_HAVE_WEBGL1)
/* The browser's OpenGL ES 2.0 renderer, on a WebGL1 context
 * (--webgl1 / --webgl1-zbuffer). Runs the shared ES2 core. */
#include "platform/platform_web_renderer_webgl1.h"
#else
struct ToriPlatformWeb_Renderer_WebGL1;
#endif
#if defined(TORIRS_HAVE_GLES3)
/* Android's OpenGL ES 3.0 renderer (--gles3 / --gles3-zbuffer).
 * Runs the same ES3 core the browser runs as WebGL2. */
#include "platform/platform_androidarmv7_renderer_opengles3.h"
#else
struct ToriPlatformAndroid_Renderer_GLES3;
#endif
/* GL/WebGL remains opt-in. The XP lane instead defaults to classic fixed-
 * function D3D9; --soft3d explicitly selects its GDI fallback. */
#define TORIRS_GPU_DEFAULT 0
#if defined(TORIRS_HAVE_D3D9)
#define TORIRS_D3D9_DEFAULT 1
#else
#define TORIRS_D3D9_DEFAULT 0
#endif
#include "pacer.h"
#include "render/torirs_frame.h"
#include "render/torirs_renderer_kind.h"
#include "toridraw_eip_sample.h"
#include "toridraw_frame_ab.h"
#include "toridraw_math.h"
#include "ui/torirs_chrome_inkwell.h"
#include "ui/uitree_hover.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_snapshot.h"

#include <assert.h>
#include <bmp.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TORIRS_PLATFORM_WEB)
#include "platform/platform_web_io.h"
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

#endif

/* Repo-relative defaults (run from the repo root); pass an explicit cache dir
 * as argv[1] from anywhere else. The default boot is the 254-era dat1 cache
 * driven by the rs245_2lc RevConfig; --dat2 switches to the js5 cache, where
 * an interface id is opened directly instead. */
#define DAT1_CACHE_DIR "cache254"
#define DAT2_CACHE_DIR "cache.jan2026"

/* Render one frame into a BMP for the CS2 harness. Same path TORIRS_EXIT_BMP
 * uses, so a harness frame and an exit frame are the same picture. */
static void
harness_shot(
    void* user,
    char const* path)
{
    struct App* app = (struct App*)user;
    int* pixels = (int*)calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
    assert(pixels);
    App_Render(app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    bmp_write_file(path, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    free(pixels);
}

#define DEFAULT_REVCONFIG_UI "revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini"
#define DEFAULT_REVCONFIG_CACHE "revconfig/rs245_2lc/rs245_2lc_dat1_cache.ini"
#define CONFIG_DIR "config"
#define SCRIPT_DIR "script"

/*
 * "Nothing has said which interface to open yet."
 *
 * The boot group used to be the literal 84, which is the Lost City gameframe
 * and nothing else's — every other cache opening it got whatever that id
 * happens to be there. It now comes from the resolved profile's `[iface:boot]`,
 * below, and this sentinel is only how the CLI parse records that neither a
 * positional argument nor a manifest overrode it.
 */
#define INTERFACE_ID_UNSET 0

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
        TORIRS_REPORT("  ");
    TORIRS_REPORT(
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
        TORIRS_REPORT(
            " graphic=%d",
            UITreeSceneBridge_SpriteCacheIdForScene(&app->bridge, c->u.rs_graphic.scene_id));
    else if( c->type == UIELEM_RS_TEXT )
        TORIRS_REPORT(
            " font=%d color=0x%x text=\"%s\"",
            c->u.rs_text.font_id,
            (unsigned)c->u.rs_text.color,
            c->u.rs_text.text ? c->u.rs_text.text : "");
    else if( c->type == UIELEM_RS_LINE )
        TORIRS_REPORT(
            " color=0x%x width=%d dir=%d",
            (unsigned)c->u.rs_line.color,
            c->u.rs_line.line_width,
            c->u.rs_line.horizontal ? 1 : 0);

    if( c->type != UIELEM_RS_LAYER )
        TORIRS_REPORT(
            " abs=%d,%d %dx%d hidden=%d ownhide=%d",
            c->position.abs_x,
            c->position.abs_y,
            c->position.abs_w,
            c->position.abs_h,
            dump_node_hidden(tree, idx),
            c->behavior.hide);
    TORIRS_REPORT("\n");

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
            TORIRS_REPORT(
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
    for( int32_t i = app->tree->root_index; i >= 0; i = app->tree->components[i].next_sibling )
    {
        struct UITreeComponent const* c = &app->tree->components[i];
        int children = 0;
        for( int32_t k = c->first_child; k >= 0; k = app->tree->components[k].next_sibling )
            children++;
        TORIRS_LOG(
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

/*
 * Where a presented frame comes from, per lane.
 *
 * One supplier each, handed to App_DrawComplete after the present, and called
 * by it ONLY when a capture is actually waiting -- see App_DrawComplete for
 * why that ordering is the point rather than an optimisation.
 *
 * The GPU ones read the device back; the software one already has the pixels
 * it just wrote and only has to copy them. D3D9 has no readback and passes
 * NULL, which is the documented way to say "use the software re-render".
 */
#if defined(TORIRS_HAVE_GL3)
static int
capture_from_gl3(
    void* user,
    int* pixels,
    int width,
    int height)
{
    return ToriPlatformSDL2_Renderer_GL3_ReadPixels((struct ToriPlatformSDL2_Renderer_GL3*)user, pixels, width, height) ? 1 : 0;
}
#endif

/* The canvas IS this lane's framebuffer, so the "readback" is a copy. The size
 * check is not defensive noise: a resize lands between App_Render and here, and
 * copying the wrong number of rows out of the smaller of the two is how a
 * screenshot would become a heap overrun. */
#if defined(TORIRS_HAVE_D3D9)
static int
capture_from_d3d9(
    void* user,
    int* pixels,
    int width,
    int height)
{
    return ToriPlatformWin32_Renderer_D3D9_ReadPixels((struct ToriPlatformWin32_Renderer_D3D9*)user, pixels, width, height) ? 1 : 0;
}
#endif

#if defined(TORIRS_HAVE_GLES2)
static int
capture_from_gles2(
    void* user,
    int* pixels,
    int width,
    int height)
{
    return ToriPlatformAndroid_Renderer_GLES2_ReadPixels((struct ToriPlatformAndroid_Renderer_GLES2*)user, pixels, width, height) ? 1 : 0;
}
#endif

#if defined(TORIRS_HAVE_WEBGL2)
static int
capture_from_webgl2(
    void* user,
    int* pixels,
    int width,
    int height)
{
    return ToriPlatformWeb_Renderer_WebGL2_ReadPixels((struct ToriPlatformWeb_Renderer_WebGL2*)user, pixels, width, height) ? 1 : 0;
}
#endif

#if defined(TORIRS_HAVE_WEBGL1)
static int
capture_from_webgl1(
    void* user,
    int* pixels,
    int width,
    int height)
{
    return ToriPlatformWeb_Renderer_WebGL1_ReadPixels((struct ToriPlatformWeb_Renderer_WebGL1*)user, pixels, width, height) ? 1 : 0;
}
#endif

#if defined(TORIRS_HAVE_GLES3)
static int
capture_from_gles3(
    void* user,
    int* pixels,
    int width,
    int height)
{
    return ToriPlatformAndroid_Renderer_GLES3_ReadPixels((struct ToriPlatformAndroid_Renderer_GLES3*)user, pixels, width, height) ? 1 : 0;
}
#endif

static int
capture_from_software(
    void* user,
    int* pixels,
    int width,
    int height)
{
    struct PlatformWindow* platform = (struct PlatformWindow*)user;
    int const* src = PlatformWindow_Pixels(platform);

    int const src_w = PlatformWindow_Width(platform);
    int const src_h = PlatformWindow_Height(platform);

    if( !src || src_w <= 0 || src_h <= 0 )
        return 0;
    if( width != UITREE_LAYOUT_ROOT_W || height != UITREE_LAYOUT_ROOT_H )
        return 0;
    if( src_w == width && src_h == height )
    {
        memcpy(pixels, src, (size_t)width * (size_t)height * sizeof(int));
        return 1;
    }
    /* A scaled buffer, sampled down to the layout the caller asked for --
     * what the GPU lanes' ReadPixels do with their own targets. */
    for( int y = 0; y < height; y++ )
    {
        int const* row = src + (size_t)((long long)y * src_h / height) * (size_t)src_w;
        for( int x = 0; x < width; x++ )
            pixels[(size_t)y * (size_t)width + (size_t)x] = row[(long long)x * src_w / width];
    }
    return 1;
}

/**
 * The touch layer's "is this point a window rather than the world?".
 *
 * The plugin panel and the developer chrome are drawn INSIDE the world's
 * rectangle, so the viewport test alone calls a finger landing on one a camera
 * drag -- which the camera then refuses, because its own gate asks this same
 * question. A drag on the panel's scrollbar drove nothing at all.
 */
static int
touch_overlay_owns_point(
    void* user,
    int x,
    int y)
{
    return App_PointerOwnedByUi((struct App*)user, x, y);
}

/* The software lane's buffer for this frame: the render size the present will
 * place the layout at. @see ClientScale_Present. */
static void
main_software_buffer_size(
    struct App* app,
    struct PlatformWindow* platform,
    int* out_w,
    int* out_h)
{
    struct ClientScaleSettings settings;
    struct ClientScalePresent present;
    int area_w = 0;
    int area_h = 0;

    assert(app);
    assert(platform);
    assert(out_w);
    assert(out_h);
    *out_w = UITREE_LAYOUT_ROOT_W;
    *out_h = UITREE_LAYOUT_ROOT_H;
    PlatformWindow_GameAreaPixels(platform, &area_w, &area_h);
    if( area_w <= 0 || area_h <= 0 )
        return;
    App_ClientScaleSettings(app, &settings);
    ClientScale_Present(
        &settings, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, area_w, area_h, &present);
    *out_w = present.render_w;
    *out_h = present.render_h;
}

/* A layout-pixel damage rect in the render buffer's pixels, rounded outward
 * so a scaled edge is never left stale. */
static void
main_damage_to_buffer(
    struct PlatformWindow* platform,
    int* x,
    int* y,
    int* w,
    int* h)
{
    int const bw = PlatformWindow_Width(platform);
    int const bh = PlatformWindow_Height(platform);
    int const lw = UITREE_LAYOUT_ROOT_W;
    int const lh = UITREE_LAYOUT_ROOT_H;
    long long x0;
    long long y0;
    long long x1;
    long long y1;

    assert(platform);
    if( bw == lw && bh == lh )
        return;
    /* Two layout pixels wider each way: a Linear or Bicubic interface filter
     * reads that far past a changed texel (soft3d_segment_end). */
    x0 = ((long long)*x - 2) * bw / lw;
    y0 = ((long long)*y - 2) * bh / lh;
    x1 = ((long long)(*x + *w + 2) * bw + lw - 1) / lw;
    y1 = ((long long)(*y + *h + 2) * bh + lh - 1) / lh;
    if( x0 < 0 )
        x0 = 0;
    if( y0 < 0 )
        y0 = 0;
    if( x1 > bw )
        x1 = bw;
    if( y1 > bh )
        y1 = bh;
    *x = (int)x0;
    *y = (int)y0;
    *w = (int)(x1 - x0);
    *h = (int)(y1 - y0);
}

/** Interactive present: Soft3D writes pixels then blits; GPU backends drain the
 * same retained frame and present it. Headless/BMP paths keep using App_Render.
 *
 * `world_depth` is the renderer SELECTION for the families whose painter and
 * depth passes are two whole renderers rather than two modes of one: this is
 * the one place that knows which was brought up, and calling the right entry
 * point here is what keeps the test out of the renderers themselves.
 * @see renderer_active_is_depth. */
static void
interactive_render_present(
    struct App* app,
    struct PlatformWindow* platform,
    struct ToriPlatformSDL2_Renderer_GL3* gl3,
    struct ToriPlatformWin32_Renderer_D3D9* d3d9,
    struct ToriPlatformAndroid_Renderer_GLES2* gles2,
    struct ToriPlatformWeb_Renderer_WebGL2* webgl2,
    struct ToriPlatformWeb_Renderer_WebGL1* webgl1,
    struct ToriPlatformAndroid_Renderer_GLES3* gles3,
    bool world_depth)
{
    int const interface_scale_mode = RS_CS2Host_UiScaleMode(&app->host);
    struct ClientScaleSettings client_scale;

    /* Device options 15 and 30..33 are presentation state, just like option
     * 27's canvas size. Apply them immediately after the click that changed
     * them and to every renderer lane; each setter is cheap while the value is
     * unchanged. The interface filter (15) samples interface art a GPU lane
     * draws larger than it is; the output filter samples the finished frame
     * onto the window. */
    App_ClientScaleSettings(app, &client_scale);
    PlatformWindow_SetClientScaling(platform, &client_scale);
    {
        /* The readout the settings page shows. The renderers place the frame
         * with the same function and the same inputs, so this is what they
         * drew, not an estimate of it. */
        struct ClientScalePresent present;
        int area_w = 0;
        int area_h = 0;
        PlatformWindow_GameAreaPixels(platform, &area_w, &area_h);
        ClientScale_Present(
            &client_scale,
            UITREE_LAYOUT_ROOT_W,
            UITREE_LAYOUT_ROOT_H,
            area_w,
            area_h,
            &present);
        App_SetClientScalePresent(app, &present);
    }
#if defined(TORIRS_HAVE_D3D9)
    if( d3d9 )
    {
        struct ToriRS_Frame frame;
        int progress = 0;
        int pick_armed = 0;

        App_NoteFrameDrawn(app);
        ToriPlatformWin32_Renderer_D3D9_SetInterfaceScaleMode(d3d9, interface_scale_mode);
        ToriPlatformWin32_Renderer_D3D9_SetClientScaling(d3d9, &client_scale);

        if( App_IsBooting(app, &progress) )
        {
            /* Post-login loading is a black screen with only the sentence
             * (-1 = clear only, no bar). The caption is the app's on every
             * lane -- App_BootBarCaption both picks the words and registers
             * the face in the scene the renderer resolves font ids against. */
            int caption_font_id = -1;
            char const* caption = App_BootBarCaption(app, &caption_font_id);

            ToriPlatformWin32_Renderer_D3D9_DrawBootBar(
                d3d9, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
        }
        else if( App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
        {
            if( app->world_mouse_in_viewport )
            {
                ToriPlatformWin32_Renderer_D3D9_SetPick(d3d9, app->world_mouse_x, app->world_mouse_y);
                pick_armed = 1;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
            {
                ToriPlatformWin32_Renderer_D3D9_RenderFrame(d3d9, &frame);
            }
            if( torirs_env_frame_debug() )
                TORIRS_LOG(
                    "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
                    frame.dbg_emit_element,
                    frame.dbg_emit_terrain,
                    frame.dbg_drop_not_live,
                    frame.dbg_drop_no_model);
            if( pick_armed )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PICK_FINISH)
                {
                    App_PickFinish(app, ToriPlatformWin32_Renderer_D3D9_PickHits(d3d9));
                }
            }
        }
        /*
         * BEFORE the present, unlike every other lane.
         *
         * The swap chain is D3DSWAPEFFECT_DISCARD, which leaves the back
         * buffer undefined the moment it is presented -- so this is the last
         * instant the finished frame still exists to be read. RuneLite's GPU
         * plugin reads after its swapBuffers because GL's back buffer survives
         * one; D3D9's does not, and the ordering has to follow the API rather
         * than the other lanes.
         */
        App_DrawComplete(app, capture_from_d3d9, d3d9);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
            ToriPlatformWin32_Renderer_D3D9_Present(d3d9);
        }
        return;
    }
#else
    (void)d3d9;
#endif

#if defined(TORIRS_HAVE_GLES2)
    if( gles2 )
    {
        struct ToriRS_Frame frame;
        int progress = 0;
        int pick_armed = 0;

        App_NoteFrameDrawn(app);
        ToriPlatformAndroid_Renderer_GLES2_SetInterfaceScaleMode(gles2, interface_scale_mode);
        ToriPlatformAndroid_Renderer_GLES2_SetClientScaling(gles2, &client_scale);

        if( App_IsBooting(app, &progress) )
        {
            int caption_font_id = -1;
            char const* caption = App_BootBarCaption(app, &caption_font_id);

            if( world_depth )
                ToriPlatformAndroid_Renderer_GLES2_ZBufferDrawBootBar(
                    gles2, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
            else
                ToriPlatformAndroid_Renderer_GLES2_PainterDrawBootBar(
                    gles2, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
        }
        else if( App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
        {
            if( app->world_mouse_in_viewport )
            {
                ToriPlatformAndroid_Renderer_GLES2_SetPick(gles2, app->world_mouse_x, app->world_mouse_y);
                pick_armed = 1;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
            {
#if defined(TORIRS_HAVE_GLES2_DUALCORE)
                if( gles2_dualcore_lane )
                    ToriPlatformAndroid_Renderer_GLES2_DualCore_RenderFrame(gles2_dualcore_lane, &frame);
                else
#endif
                    if( world_depth )
                        ToriPlatformAndroid_Renderer_GLES2_ZBufferRenderFrame(gles2, &frame);
                    else
                        ToriPlatformAndroid_Renderer_GLES2_PainterRenderFrame(gles2, &frame);
            }
            if( torirs_env_frame_debug() )
                TORIRS_LOG(
                    "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
                    frame.dbg_emit_element,
                    frame.dbg_emit_terrain,
                    frame.dbg_drop_not_live,
                    frame.dbg_drop_no_model);
            if( pick_armed )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PICK_FINISH)
                {
                    App_PickFinish(app, ToriPlatformAndroid_Renderer_GLES2_PickHits(gles2));
                }
            }
        }
        /*
         * BEFORE the swap, like D3D9 and unlike the desktop GL lane: an EGL
         * window surface's back buffer is undefined after eglSwapBuffers
         * (EGL_BUFFER_DESTROYED is the default swap behaviour), so this is
         * the last instant the finished frame exists to be read.
         */
        App_DrawComplete(app, capture_from_gles2, gles2);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
#if defined(TORIRS_FRAME_TIMES)
            uint64_t before_us = PlatformWindow_TicksUs();
#endif
            PlatformWindow_PresentGL(platform);
#if defined(TORIRS_FRAME_TIMES)
            ToriRS_FrameTimes_Present(before_us, PlatformWindow_TicksUs());
#endif
        }
        return;
    }
#else
    (void)gles2;
#endif

#if defined(TORIRS_HAVE_WEBGL2)
    if( webgl2 )
    {
        struct ToriRS_Frame frame;
        int progress = 0;
        int pick_armed = 0;

        App_NoteFrameDrawn(app);
        ToriPlatformWeb_Renderer_WebGL2_SetInterfaceScaleMode(webgl2, interface_scale_mode);
        ToriPlatformWeb_Renderer_WebGL2_SetClientScaling(webgl2, &client_scale);

        if( App_IsBooting(app, &progress) )
        {
            int caption_font_id = -1;
            char const* caption = App_BootBarCaption(app, &caption_font_id);

            if( world_depth )
                ToriPlatformWeb_Renderer_WebGL2_ZBufferDrawBootBar(
                    webgl2, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
            else
                ToriPlatformWeb_Renderer_WebGL2_PainterDrawBootBar(
                    webgl2, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
        }
        else if( App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
        {
            if( app->world_mouse_in_viewport )
            {
                ToriPlatformWeb_Renderer_WebGL2_SetPick(webgl2, app->world_mouse_x, app->world_mouse_y);
                pick_armed = 1;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
            {
                if( world_depth )
                    ToriPlatformWeb_Renderer_WebGL2_ZBufferRenderFrame(webgl2, &frame);
                else
                    ToriPlatformWeb_Renderer_WebGL2_PainterRenderFrame(webgl2, &frame);
            }
            if( torirs_env_frame_debug() )
                TORIRS_LOG(
                    "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
                    frame.dbg_emit_element,
                    frame.dbg_emit_terrain,
                    frame.dbg_drop_not_live,
                    frame.dbg_drop_no_model);
            if( pick_armed )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PICK_FINISH)
                {
                    App_PickFinish(app, ToriPlatformWeb_Renderer_WebGL2_PickHits(webgl2));
                }
            }
        }
        /* BEFORE the swap, for the same reason the WebGL1 lane reads there:
         * the drawable's contents are undefined once it has been presented. */
        App_DrawComplete(app, capture_from_webgl2, webgl2);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
#if defined(TORIRS_FRAME_TIMES)
            uint64_t before_us = PlatformWindow_TicksUs();
#endif
            PlatformWindow_PresentGL(platform);
#if defined(TORIRS_FRAME_TIMES)
            ToriRS_FrameTimes_Present(before_us, PlatformWindow_TicksUs());
#endif
        }
        return;
    }
#else
    (void)webgl2;
#endif

#if defined(TORIRS_HAVE_WEBGL1)
    if( webgl1 )
    {
        struct ToriRS_Frame frame;
        int progress = 0;
        int pick_armed = 0;

        App_NoteFrameDrawn(app);
        ToriPlatformWeb_Renderer_WebGL1_SetInterfaceScaleMode(webgl1, interface_scale_mode);
        ToriPlatformWeb_Renderer_WebGL1_SetClientScaling(webgl1, &client_scale);

        if( App_IsBooting(app, &progress) )
        {
            int caption_font_id = -1;
            char const* caption = App_BootBarCaption(app, &caption_font_id);

            if( world_depth )
                ToriPlatformWeb_Renderer_WebGL1_ZBufferDrawBootBar(
                    webgl1, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
            else
                ToriPlatformWeb_Renderer_WebGL1_PainterDrawBootBar(
                    webgl1, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
        }
        else if( App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
        {
            if( app->world_mouse_in_viewport )
            {
                ToriPlatformWeb_Renderer_WebGL1_SetPick(webgl1, app->world_mouse_x, app->world_mouse_y);
                pick_armed = 1;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
            {
                if( world_depth )
                    ToriPlatformWeb_Renderer_WebGL1_ZBufferRenderFrame(webgl1, &frame);
                else
                    ToriPlatformWeb_Renderer_WebGL1_PainterRenderFrame(webgl1, &frame);
            }
            if( torirs_env_frame_debug() )
                TORIRS_LOG(
                    "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
                    frame.dbg_emit_element,
                    frame.dbg_emit_terrain,
                    frame.dbg_drop_not_live,
                    frame.dbg_drop_no_model);
            if( pick_armed )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PICK_FINISH)
                {
                    App_PickFinish(app, ToriPlatformWeb_Renderer_WebGL1_PickHits(webgl1));
                }
            }
        }
        /* BEFORE the swap: the drawable's contents are undefined once it has
         * been presented, so this is the last instant the finished frame
         * exists to be read. */
        App_DrawComplete(app, capture_from_webgl1, webgl1);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
#if defined(TORIRS_FRAME_TIMES)
            uint64_t before_us = PlatformWindow_TicksUs();
#endif
            PlatformWindow_PresentGL(platform);
#if defined(TORIRS_FRAME_TIMES)
            ToriRS_FrameTimes_Present(before_us, PlatformWindow_TicksUs());
#endif
        }
        return;
    }
#else
    (void)webgl1;
#endif

#if defined(TORIRS_HAVE_GLES3)
    if( gles3 )
    {
        struct ToriRS_Frame frame;
        int progress = 0;
        int pick_armed = 0;

        App_NoteFrameDrawn(app);
        ToriPlatformAndroid_Renderer_GLES3_SetInterfaceScaleMode(gles3, interface_scale_mode);
        ToriPlatformAndroid_Renderer_GLES3_SetClientScaling(gles3, &client_scale);

        if( App_IsBooting(app, &progress) )
        {
            int caption_font_id = -1;
            char const* caption = App_BootBarCaption(app, &caption_font_id);

            if( world_depth )
                ToriPlatformAndroid_Renderer_GLES3_ZBufferDrawBootBar(
                    gles3, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
            else
                ToriPlatformAndroid_Renderer_GLES3_PainterDrawBootBar(
                    gles3, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
        }
        else if( App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
        {
            if( app->world_mouse_in_viewport )
            {
                ToriPlatformAndroid_Renderer_GLES3_SetPick(gles3, app->world_mouse_x, app->world_mouse_y);
                pick_armed = 1;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
            {
                if( world_depth )
                    ToriPlatformAndroid_Renderer_GLES3_ZBufferRenderFrame(gles3, &frame);
                else
                    ToriPlatformAndroid_Renderer_GLES3_PainterRenderFrame(gles3, &frame);
            }
            if( torirs_env_frame_debug() )
                TORIRS_LOG(
                    "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
                    frame.dbg_emit_element,
                    frame.dbg_emit_terrain,
                    frame.dbg_drop_not_live,
                    frame.dbg_drop_no_model);
            if( pick_armed )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PICK_FINISH)
                {
                    App_PickFinish(app, ToriPlatformAndroid_Renderer_GLES3_PickHits(gles3));
                }
            }
        }
        /* BEFORE the swap: the drawable's contents are undefined once it has
         * been presented, so this is the last instant the finished frame
         * exists to be read. */
        App_DrawComplete(app, capture_from_gles3, gles3);
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
#if defined(TORIRS_FRAME_TIMES)
            uint64_t before_us = PlatformWindow_TicksUs();
#endif
            PlatformWindow_PresentGL(platform);
#if defined(TORIRS_FRAME_TIMES)
            ToriRS_FrameTimes_Present(before_us, PlatformWindow_TicksUs());
#endif
        }
        return;
    }
#else
    (void)gles3;
#endif

#if defined(TORIRS_HAVE_GL3)
    if( gl3 )
    {
        struct ToriRS_Frame frame;
        int progress = 0;
        int pick_armed = 0;
        int const chrome_w = PlatformWindow_ChromeWidth(platform);
        int const chrome_h = PlatformWindow_ChromeHeight(platform);

        App_NoteFrameDrawn(app);
        ToriPlatformSDL2_Renderer_GL3_SetInterfaceScaleMode(gl3, interface_scale_mode);
        ToriPlatformSDL2_Renderer_GL3_SetClientScaling(gl3, &client_scale);
        ToriPlatformSDL2_Renderer_GL3_SetHostRightInset(gl3, chrome_w);

        if( App_IsBooting(app, &progress) )
        {
            /* Post-login loading is a black screen with only the sentence
             * (-1 = clear only, no bar). The caption is the app's on every
             * lane -- App_BootBarCaption both picks the words and registers
             * the face in the scene the renderer resolves font ids against. */
            int caption_font_id = -1;
            char const* caption = App_BootBarCaption(app, &caption_font_id);

            ToriPlatformSDL2_Renderer_GL3_DrawBootBar(
                gl3, App_BootTextOnly(app) ? -1 : progress, caption_font_id, caption);
        }
        else if( App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
        {
            if( app->world_mouse_in_viewport )
            {
                ToriPlatformSDL2_Renderer_GL3_SetPick(gl3, app->world_mouse_x, app->world_mouse_y);
                pick_armed = 1;
            }
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
            {
                ToriPlatformSDL2_Renderer_GL3_RenderFrame(gl3, &frame);
            }
            if( torirs_env_frame_debug() )
                TORIRS_LOG(
                    "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
                    frame.dbg_emit_element,
                    frame.dbg_emit_terrain,
                    frame.dbg_drop_not_live,
                    frame.dbg_drop_no_model);
            if( pick_armed )
            {
                TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PICK_FINISH)
                {
                    App_PickFinish(app, ToriPlatformSDL2_Renderer_GL3_PickHits(gl3));
                }
            }
        }
#if !defined(__APPLE__)
        if( chrome_w > 0 && chrome_h > 0 )
        {
            int const* pixels = PlatformWindow_ChromeTakeDirty(platform)
                                    ? PlatformWindow_ChromePixels(platform)
                                    : NULL;
            ToriPlatformSDL2_Renderer_GL3_DrawChromePixels(gl3, pixels, chrome_w, chrome_h);
        }
#else
        /* The Cocoa child WKWebView owns the reserved right inset directly;
         * there is no retained SDL chrome texture underneath it to composite. */
        (void)chrome_h;
#endif
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
            PlatformWindow_PresentGL(platform);
        }
        /*
         * AFTER the swap, and that is measured rather than reasoned.
         *
         * Reading before it returns the clear colour on this stack: SDL's GL
         * context on macOS is Metal-backed and the frame is not resident in a
         * readable buffer until the swap flushes it. The spec argument for
         * reading first (the back buffer is defined up to the swap and
         * undefined after) describes an implementation this is not one of.
         *
         * RuneLite reads after its swapBuffers too, though not from the same
         * buffer -- rlawt's AWTContext.getBufferMode hands it GL_FRONT, or the
         * FBO's GL_COLOR_ATTACHMENT0 when it renders through one.
         */
        App_DrawComplete(app, capture_from_gl3, gl3);
        return;
    }
#else
    (void)gl3;
#endif

    /* The buffer, which interface scaling makes larger than the layout. */
    App_Render(
        app, PlatformWindow_Pixels(platform), PlatformWindow_Width(platform), PlatformWindow_Height(platform));
    {
        /* Present only what the render actually wrote. Off unless damage
         * drawing is armed, in which case App_Render already left the rest of
         * the buffer holding last frame's pixels -- so this is not an
         * optimisation layered on top of a full render, it is the other half
         * of one decision. */
        int dx;
        int dy;
        int dw;
        int dh;

        if( App_PresentDamage(app, &dx, &dy, &dw, &dh) )
        {
            struct ToriRS_DamageRect const* dr;
            int n;

            /* Damage is measured in layout pixels; the buffer it presents is
             * the render's. */
            main_damage_to_buffer(platform, &dx, &dy, &dw, &dh);
            PlatformWindow_SetPresentDamage(platform, dx, dy, dw, dh);
            n = App_DamageRects(app, &dr);
            if( n > 0 )
            {
                int rects[PLATFORM_PRESENT_DAMAGE_RECT_MAX][4];

                if( n > PLATFORM_PRESENT_DAMAGE_RECT_MAX )
                    n = PLATFORM_PRESENT_DAMAGE_RECT_MAX;
                for( int i = 0; i < n; i++ )
                {
                    rects[i][0] = dr[i].x;
                    rects[i][1] = dr[i].y;
                    rects[i][2] = dr[i].w;
                    rects[i][3] = dr[i].h;
                    main_damage_to_buffer(
                        platform, &rects[i][0], &rects[i][1], &rects[i][2], &rects[i][3]);
                }
                PlatformWindow_SetPresentDamageRects(platform, (int const(*)[4])rects, n);
            }
        }
    }
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
    {
        PlatformWindow_Present(platform);
    }
    /* After the present, matching the GL lanes. This one's buffer is
     * client-side and valid either side of it, so the ordering is chosen to
     * keep one rule rather than because this lane needs it. */
    App_DrawComplete(app, capture_from_software, platform);
}

/* App_RunOnce returned no frame commit.  The software surface can safely
 * re-upload its retained pixels (useful after a window expose), but swapping an
 * undrawn GL backbuffer can alternate to older contents and a D3D9 DISCARD
 * swapchain explicitly does not preserve its backbuffer.  Leave GPU front
 * buffers alone; the window/compositor retains the last committed frame. */
static void
interactive_present_retained(
    struct PlatformWindow* platform,
    struct ToriPlatformSDL2_Renderer_GL3* gl3,
    struct ToriPlatformWin32_Renderer_D3D9* d3d9,
    struct ToriPlatformAndroid_Renderer_GLES2* gles2,
    struct ToriPlatformWeb_Renderer_WebGL2* webgl2,
    struct ToriPlatformWeb_Renderer_WebGL1* webgl1,
    struct ToriPlatformAndroid_Renderer_GLES3* gles3)
{
#if defined(TORIRS_HAVE_D3D9)
    if( d3d9 )
        return;
#else
    (void)d3d9;
#endif
#if defined(TORIRS_HAVE_GLES2)
    if( gles2 )
        return;
#else
    (void)gles2;
#endif
#if defined(TORIRS_HAVE_WEBGL2)
    if( webgl2 )
        return;
#else
    (void)webgl2;
#endif
#if defined(TORIRS_HAVE_WEBGL1)
    if( webgl1 )
        return;
#else
    (void)webgl1;
#endif
#if defined(TORIRS_HAVE_GLES3)
    if( gles3 )
        return;
#else
    (void)gles3;
#endif
#if defined(TORIRS_HAVE_GL3)
    if( gl3 )
        return;
#else
    (void)gl3;
#endif
    PlatformWindow_Present(platform);
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
// clang-format off
EM_JS(
    int,
    web_document_hidden,
    (void),
    { return (typeof document !== 'undefined' && document.hidden) ? 1 : 0; });

/* TORIRS_PERF=1 only (see torirs_perf.h): drops a User Timing mark plus a
 * console.warn at `label`, so a captured DevTools trace shows *why* a frame
 * ran long without the manual cross-referencing (WebSocketReceive bursts
 * against AnimationFrame durations, sample by sample) that a periodic
 * camera stutter took to trace back to frame_loop_step's own raf<->
 * settimeout(0) pacing flip, tripped by the 600ms server tick's burst of
 * small packets. All formatting happens in C; this just posts the string. */
EM_JS(
    void,
    web_mark_frame_event,
    (const char* label),
    {
        var s = UTF8ToString(label);
        if( typeof performance !== 'undefined' && performance.mark )
            performance.mark(s);
        console.warn('[torirs] ' + s);
    });
// clang-format on

/* Tell whoever embedded this page that the module will take commands now.
 *
 * An embedder can see the iframe load and the canvas appear well before the
 * runtime is far enough in to accept a cmdbus frame, and a harness that guesses
 * at that gap guesses wrong. Called once, where the loop begins: the exports
 * exist, the ring exists, and anything pushed from here on drains on the next
 * iteration. A page with no such hook is the ordinary case and costs the call.
 */
// clang-format off
EM_JS(
    void,
    web_announce_ready,
    (void),
    {
        if( typeof window !== 'undefined' && typeof window.torirsAnnounceReady === 'function' )
            window.torirsAnnounceReady();
    });
// clang-format on
#endif

static struct App app;
static struct ToriRS_ExecutorConfig executor_cfg;
static struct AppConfig cfg = {
    .cache_dir = NULL, /* resolved from cache_kind below */
    .config_dir = CONFIG_DIR,
    .script_dir = SCRIPT_DIR,
    .interface_id = INTERFACE_ID_UNSET,
    .cache_kind = APP_CACHE_DAT1,
    /* -1 = no manifest spawn; app_world_load_begin falls back to the client default. */
    .spawn_x = -1,
    .spawn_z = -1,
};

static struct PlatformWindow* platform;
static struct LibToriRS_Input input_storage;
static struct LibToriRS_Input* input;
/* The ring is 128KB and there is exactly one bus per process. */
static struct ToriRS_CmdBus bus;
static FILE* replay;
static uint64_t replay_now;
/* NULL unless the desktop-GL renderer was built AND --opengl3 was passed. */
static struct ToriPlatformSDL2_Renderer_GL3* gl3;
/* NULL unless the Win32 fixed-function D3D9 renderer was selected. */
static struct ToriPlatformWin32_Renderer_D3D9* d3d9;
/* NULL unless the Android GLES2 renderer was built AND --gles2 was passed. */
static struct ToriPlatformAndroid_Renderer_GLES2* gles2;
/* NULL unless the WebGL2 renderer was built AND --webgl2 was passed. */
static struct ToriPlatformWeb_Renderer_WebGL2* webgl2;
/* NULL unless the WebGL1 renderer was built AND --webgl1 was passed. */
static struct ToriPlatformWeb_Renderer_WebGL1* webgl1;
/* NULL unless the GLES3 renderer was built AND --gles3 was passed. */
static struct ToriPlatformAndroid_Renderer_GLES3* gles3;

/* --- the renderer, and switching it live ---------------------------------
 *
 * Exactly one of the handles above is live, or none for Soft3D, and which one
 * is `renderer_active`. The launch's flags choose the first; after that
 * Client Settings may choose another (device option RS_CS2_DEVICEOPTION_RENDERER),
 * and renderer_follow_request() swaps it between two frames: the old
 * renderer is freed, the window is moved to the new way of presenting, and
 * the new renderer is started and handed the scene's resources again. None of
 * that touches the App, the network or the plugins -- the loop simply draws
 * the next frame with a different renderer.
 */
static enum ToriRS_RendererKind renderer_active;

/*
 * Whether the live renderer is a family's DEPTH one.
 *
 * The GPU families whose painter and depth passes are separate renderers --
 * two sets of entry points over one handle, no flag and no mode field --
 * are selected by this. It is deliberately the only such test in the
 * program: below it nothing asks again. @see 3rd/trspk/es3/trspk_es3.h.
 */
static bool
renderer_active_is_depth(void)
{
    return renderer_active == TORIRS_RENDERER_KIND_OPENGL3_DEPTH ||
           renderer_active == TORIRS_RENDERER_KIND_GLES2_DEPTH ||
           renderer_active == TORIRS_RENDERER_KIND_GLES3_DEPTH ||
           renderer_active == TORIRS_RENDERER_KIND_D3D9_DEPTH ||
           renderer_active == TORIRS_RENDERER_KIND_WEBGL1_DEPTH ||
           renderer_active == TORIRS_RENDERER_KIND_WEBGL2_DEPTH;
}
/* What the launch's flags chose -- the meaning of RS_CS2_RENDERER_LAUNCH_DEFAULT.
 * Moved to the renderer actually running if the launch's could not start. */
static enum ToriRS_RendererKind renderer_launch;
/* The option value whose start failed, or 0. Not retried until the pick
 * moves, or every frame would tear the renderer down and fail again. */
static int renderer_refused;
/* --gles2-dualcore: the dual-core lane wraps GLES2 whenever it starts. */
static bool renderer_gles2_dualcore;

static enum PlatformPresent
renderer_present(enum ToriRS_RendererKind kind)
{
    switch( kind )
    {
    case TORIRS_RENDERER_KIND_SOFTWARE:
        return PLATFORM_PRESENT_SOFTWARE;
    case TORIRS_RENDERER_KIND_OPENGL3:
    case TORIRS_RENDERER_KIND_OPENGL3_DEPTH:
    case TORIRS_RENDERER_KIND_GLES2:
    case TORIRS_RENDERER_KIND_GLES2_DEPTH:
    case TORIRS_RENDERER_KIND_WEBGL2:
    case TORIRS_RENDERER_KIND_WEBGL2_DEPTH:
    case TORIRS_RENDERER_KIND_WEBGL1:
    case TORIRS_RENDERER_KIND_WEBGL1_DEPTH:
    case TORIRS_RENDERER_KIND_GLES3:
    case TORIRS_RENDERER_KIND_GLES3_DEPTH:
        return PLATFORM_PRESENT_GL;
    case TORIRS_RENDERER_KIND_D3D9:
    case TORIRS_RENDERER_KIND_D3D9_DEPTH:
        return PLATFORM_PRESENT_NATIVE;
    case TORIRS_RENDERER_KIND_COUNT:
        break;
    }
    assert(0 && "not a renderer kind");
    return PLATFORM_PRESENT_SOFTWARE;
}

/* Is the renderer compiled into this binary? */
static bool
renderer_built(enum ToriRS_RendererKind kind)
{
    switch( kind )
    {
    case TORIRS_RENDERER_KIND_SOFTWARE:
        return true;
    case TORIRS_RENDERER_KIND_OPENGL3:
    case TORIRS_RENDERER_KIND_OPENGL3_DEPTH:
#if defined(TORIRS_HAVE_GL3)
        return true;
#else
        return false;
#endif
    case TORIRS_RENDERER_KIND_GLES2:
    case TORIRS_RENDERER_KIND_GLES2_DEPTH:
#if defined(TORIRS_HAVE_GLES2)
        return true;
#else
        return false;
#endif
    case TORIRS_RENDERER_KIND_D3D9:
    case TORIRS_RENDERER_KIND_D3D9_DEPTH:
#if defined(TORIRS_HAVE_D3D9)
        return true;
#else
        return false;
#endif
    case TORIRS_RENDERER_KIND_WEBGL2:
    case TORIRS_RENDERER_KIND_WEBGL2_DEPTH:
#if defined(TORIRS_HAVE_WEBGL2)
        return true;
#else
        return false;
#endif
    case TORIRS_RENDERER_KIND_WEBGL1:
    case TORIRS_RENDERER_KIND_WEBGL1_DEPTH:
#if defined(TORIRS_HAVE_WEBGL1)
        return true;
#else
        return false;
#endif
    case TORIRS_RENDERER_KIND_GLES3:
    case TORIRS_RENDERER_KIND_GLES3_DEPTH:
#if defined(TORIRS_HAVE_GLES3)
        return true;
#else
        return false;
#endif
    case TORIRS_RENDERER_KIND_COUNT:
        break;
    }
    assert(0 && "not a renderer kind");
    return false;
}

/* The renderers this build has AND this window can present, as
 * TORIRS_RENDERER_KIND_BIT flags. */
static unsigned
renderer_available(void)
{
    unsigned mask = 0;
    for( int kind = 0; kind < TORIRS_RENDERER_KIND_COUNT; kind++ )
        if( renderer_built((enum ToriRS_RendererKind)kind) &&
            PlatformWindow_PresentAvailable(
                platform, renderer_present((enum ToriRS_RendererKind)kind)) )
            mask |= TORIRS_RENDERER_KIND_BIT(kind);
    /* A lane may be drawing with a renderer its window would not offer again
     * (Android, the browser): that one is still on the list. */
    return mask | TORIRS_RENDERER_KIND_BIT(renderer_active);
}

/* A renderer flag was given: the saved pick does not decide this launch. */
static bool renderer_launch_flagged;

static void
renderer_publish(void)
{
    RS_CS2Host_SetRendererStatus(
        &app.host,
        (int)renderer_active,
        renderer_available(),
        renderer_refused,
        renderer_launch_flagged);
}

/*
 * Start `kind` on a window already presenting its way, with no other renderer
 * live. Also puts the App in the mode that renderer draws with. On failure
 * nothing is left behind.
 */
static bool
renderer_start(enum ToriRS_RendererKind kind)
{
    assert(renderer_built(kind));
#if defined(TORIRS_HAVE_GL3)
    assert(!gl3);
#endif
#if defined(TORIRS_HAVE_D3D9)
    assert(!d3d9);
#endif
#if defined(TORIRS_HAVE_GLES2)
    assert(!gles2);
#endif
#if defined(TORIRS_HAVE_WEBGL2)
    assert(!webgl2);
#endif
#if defined(TORIRS_HAVE_WEBGL1)
    assert(!webgl1);
#endif
#if defined(TORIRS_HAVE_GLES3)
    assert(!gles3);
#endif
    switch( kind )
    {
    case TORIRS_RENDERER_KIND_SOFTWARE:
        App_SetWorldRenderMode(&app, TORIRS_WORLD_PAINTER);
        App_SetRendererAnimatesTextures(&app, false);
        return true;
#if defined(TORIRS_HAVE_GL3)
    case TORIRS_RENDERER_KIND_OPENGL3:
    case TORIRS_RENDERER_KIND_OPENGL3_DEPTH:
    {
        bool const depth = kind == TORIRS_RENDERER_KIND_OPENGL3_DEPTH;
        gl3 = ToriPlatformSDL2_Renderer_GL3_New(UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        assert(gl3);
        if( !ToriPlatformSDL2_Renderer_GL3_Init(gl3, PlatformWindow_GLWindow(platform), app.scene, depth) )
        {
            TORIRS_ERR("GL3 renderer init failed\n");
            ToriPlatformSDL2_Renderer_GL3_Free(gl3);
            gl3 = NULL;
            return false;
        }
        /* The depth pass needs the app to stop collecting the visible set
         * through the tile wavefront and the opaque face-distance sort;
         * that is what TORIRS_WORLD_DEPTH selects. Same contract as D3D9. */
        App_SetWorldRenderMode(&app, depth ? TORIRS_WORLD_DEPTH : TORIRS_WORLD_PAINTER);
        App_SetRendererAnimatesTextures(&app, true);
        return true;
    }
#endif
#if defined(TORIRS_HAVE_GLES2)
    case TORIRS_RENDERER_KIND_GLES2:
    case TORIRS_RENDERER_KIND_GLES2_DEPTH:
    {
        bool const depth = kind == TORIRS_RENDERER_KIND_GLES2_DEPTH;
        gles2 = ToriPlatformAndroid_Renderer_GLES2_New(UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        assert(gles2);
        if( !(depth ? ToriPlatformAndroid_Renderer_GLES2_ZBufferInit(gles2, PlatformWindow_GLWindow(platform), app.scene)
                    : ToriPlatformAndroid_Renderer_GLES2_PainterInit(
                          gles2, PlatformWindow_GLWindow(platform), app.scene)) )
        {
            TORIRS_ERR("GLES2 renderer init failed\n");
            ToriPlatformAndroid_Renderer_GLES2_Free(gles2);
            gles2 = NULL;
            return false;
        }
        /* Same contract as D3D9 and GL3: the depth pass needs the app to
         * stop collecting the visible set through the tile wavefront and
         * the opaque face-distance sort. */
        App_SetWorldRenderMode(&app, depth ? TORIRS_WORLD_DEPTH : TORIRS_WORLD_PAINTER);
        App_SetRendererAnimatesTextures(&app, true);
#if defined(TORIRS_HAVE_GLES2_DUALCORE)
        /* The lane wraps the renderer made here and keeps driving through
         * `gles2` for everything but the frame itself. */
        if( renderer_gles2_dualcore )
            gles2_dualcore_lane = ToriPlatformAndroid_Renderer_GLES2_DualCore_New(ToriPlatformAndroid_Renderer_GLES2_Core(gles2), depth);
#endif
        return true;
    }
#endif
#if defined(TORIRS_HAVE_WEBGL1)
    case TORIRS_RENDERER_KIND_WEBGL1:
    case TORIRS_RENDERER_KIND_WEBGL1_DEPTH:
    {
        bool const depth = kind == TORIRS_RENDERER_KIND_WEBGL1_DEPTH;
        webgl1 = ToriPlatformWeb_Renderer_WebGL1_New(UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        assert(webgl1);
        if( !(depth
                  ? ToriPlatformWeb_Renderer_WebGL1_ZBufferInit(webgl1, PlatformWindow_GLWindow(platform), app.scene)
                  : ToriPlatformWeb_Renderer_WebGL1_PainterInit(
                        webgl1, PlatformWindow_GLWindow(platform), app.scene)) )
        {
            TORIRS_ERR("WebGL1 renderer init failed\n");
            ToriPlatformWeb_Renderer_WebGL1_Free(webgl1);
            webgl1 = NULL;
            return false;
        }
        App_SetWorldRenderMode(&app, depth ? TORIRS_WORLD_DEPTH : TORIRS_WORLD_PAINTER);
        App_SetRendererAnimatesTextures(&app, true);
        return true;
    }
#endif
#if defined(TORIRS_HAVE_GLES3)
    case TORIRS_RENDERER_KIND_GLES3:
    case TORIRS_RENDERER_KIND_GLES3_DEPTH:
    {
        bool const depth = kind == TORIRS_RENDERER_KIND_GLES3_DEPTH;
        gles3 = ToriPlatformAndroid_Renderer_GLES3_New(UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        assert(gles3);
        if( !(depth ? ToriPlatformAndroid_Renderer_GLES3_ZBufferInit(gles3, PlatformWindow_GLWindow(platform), app.scene)
                    : ToriPlatformAndroid_Renderer_GLES3_PainterInit(
                          gles3, PlatformWindow_GLWindow(platform), app.scene)) )
        {
            TORIRS_ERR("GLES3 renderer init failed\n");
            ToriPlatformAndroid_Renderer_GLES3_Free(gles3);
            gles3 = NULL;
            return false;
        }
        App_SetWorldRenderMode(&app, depth ? TORIRS_WORLD_DEPTH : TORIRS_WORLD_PAINTER);
        App_SetRendererAnimatesTextures(&app, true);
        return true;
    }
#endif
#if defined(TORIRS_HAVE_WEBGL2)
    case TORIRS_RENDERER_KIND_WEBGL2:
    case TORIRS_RENDERER_KIND_WEBGL2_DEPTH:
    {
        bool const depth = kind == TORIRS_RENDERER_KIND_WEBGL2_DEPTH;
        webgl2 = ToriPlatformWeb_Renderer_WebGL2_New(UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        assert(webgl2);
        if( !(depth
                  ? ToriPlatformWeb_Renderer_WebGL2_ZBufferInit(webgl2, PlatformWindow_GLWindow(platform), app.scene)
                  : ToriPlatformWeb_Renderer_WebGL2_PainterInit(
                        webgl2, PlatformWindow_GLWindow(platform), app.scene)) )
        {
            TORIRS_ERR("WebGL2 renderer init failed\n");
            ToriPlatformWeb_Renderer_WebGL2_Free(webgl2);
            webgl2 = NULL;
            return false;
        }
        /* Same contract as every other GPU renderer: the depth pass needs the
         * app to stop collecting the visible set through the tile wavefront
         * and the opaque face-distance sort. */
        App_SetWorldRenderMode(&app, depth ? TORIRS_WORLD_DEPTH : TORIRS_WORLD_PAINTER);
        App_SetRendererAnimatesTextures(&app, true);
        return true;
    }
#endif
#if defined(TORIRS_HAVE_D3D9)
    case TORIRS_RENDERER_KIND_D3D9:
    case TORIRS_RENDERER_KIND_D3D9_DEPTH:
    {
        bool const depth = kind == TORIRS_RENDERER_KIND_D3D9_DEPTH;
        d3d9 = ToriPlatformWin32_Renderer_D3D9_New(UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        if( !d3d9 ||
            !ToriPlatformWin32_Renderer_D3D9_Init(d3d9, PlatformWindow_NativeWindowHandle(platform), app.scene, depth) )
        {
            TORIRS_ERR("D3D9 fixed-function renderer init failed\n");
            ToriPlatformWin32_Renderer_D3D9_Free(d3d9);
            d3d9 = NULL;
            return false;
        }
        App_SetWorldRenderMode(&app, depth ? TORIRS_WORLD_DEPTH : TORIRS_WORLD_PAINTER);
        App_SetRendererAnimatesTextures(&app, true);
        return true;
    }
#endif
    default:
        break;
    }
    assert(0 && "renderer_built() said yes to a renderer with no start");
    return false;
}

/* Free whichever GPU renderer is live. Soft3D has nothing to free. */
static void
renderer_stop(void)
{
#if defined(TORIRS_HAVE_GLES2_DUALCORE)
    /* Before the renderer it wraps: the join must come first. */
    ToriPlatformAndroid_Renderer_GLES2_DualCore_Free(gles2_dualcore_lane);
    gles2_dualcore_lane = NULL;
#endif
#if defined(TORIRS_HAVE_GLES2)
    ToriPlatformAndroid_Renderer_GLES2_Free(gles2);
    gles2 = NULL;
#endif
#if defined(TORIRS_HAVE_WEBGL2)
    ToriPlatformWeb_Renderer_WebGL2_Free(webgl2);
    webgl2 = NULL;
#endif
#if defined(TORIRS_HAVE_WEBGL1)
    ToriPlatformWeb_Renderer_WebGL1_Free(webgl1);
    webgl1 = NULL;
#endif
#if defined(TORIRS_HAVE_GLES3)
    ToriPlatformAndroid_Renderer_GLES3_Free(gles3);
    gles3 = NULL;
#endif
#if defined(TORIRS_HAVE_GL3)
    ToriPlatformSDL2_Renderer_GL3_Free(gl3);
    gl3 = NULL;
#endif
#if defined(TORIRS_HAVE_D3D9)
    ToriPlatformWin32_Renderer_D3D9_Free(d3d9);
    d3d9 = NULL;
#endif
}

/* Move the window to `kind`'s present and start it; false leaves nothing live. */
static bool
renderer_bring_up(enum ToriRS_RendererKind kind)
{
    if( !PlatformWindow_SetPresent(platform, renderer_present(kind)) )
    {
        TORIRS_ERR("renderer: this window cannot present for renderer %d\n", (int)kind);
        return false;
    }
    return renderer_start(kind);
}

/*
 * Replace the running renderer with `kind`. On failure the previous renderer
 * is brought back, and Soft3D if even that will not start -- the loop always
 * has something to draw with.
 */
static bool
renderer_switch(enum ToriRS_RendererKind kind)
{
    enum ToriRS_RendererKind const previous = renderer_active;
    bool started;
    uint64_t const begin_ms = PlatformWindow_Ticks64();

    assert(kind != renderer_active);
    renderer_stop();
    started = renderer_bring_up(kind);
    if( started )
        renderer_active = kind;
    else if( renderer_bring_up(previous) )
        renderer_active = previous;
    else
    {
        bool const software = renderer_bring_up(TORIRS_RENDERER_KIND_SOFTWARE);
        assert(software);
        (void)software;
        renderer_active = TORIRS_RENDERER_KIND_SOFTWARE;
    }

    /*
     * A retained renderer learns the scene only from its load events, and the
     * ones for everything already loaded were drained by the renderer that
     * was just freed. Whatever is still queued is from before the switch and
     * describes a scene the replay covers whole, so it goes first -- a load
     * left in the queue would be baked twice. Soft3D reads the scene directly
     * and needs no replay.
     */
    ToriDraw_SceneFrameEnd(app.scene);
    if( renderer_active != TORIRS_RENDERER_KIND_SOFTWARE )
        ToriDraw_SceneReemitRendererLoads(app.scene);

    TORIRS_REPORT(
        "renderer: %d -> %d %s in %llu ms\n",
        (int)previous,
        (int)kind,
        started ? "started" : "refused",
        (unsigned long long)(PlatformWindow_Ticks64() - begin_ms));
    return started;
}

/*
 * Between two frames: start the renderer Client Settings asks for, if it is
 * not the one running.
 */
static void
renderer_follow_request(void)
{
    int const request = RS_CS2Host_RendererRequest(&app.host);
    enum ToriRS_RendererKind const wanted = request == RS_CS2_RENDERER_LAUNCH_DEFAULT
                                                ? renderer_launch
                                                : (enum ToriRS_RendererKind)(request - 1);

    if( renderer_refused && request != renderer_refused )
        renderer_refused = 0;
    if( wanted != renderer_active && request != renderer_refused )
    {
        if( !(renderer_available() & TORIRS_RENDERER_KIND_BIT(wanted)) )
        {
            /* A pick saved on a machine or build with a renderer this one
             * lacks. Left in the store: it is the player's, and the machine
             * that can honour it still reads the same file. */
            TORIRS_REPORT("renderer: %d is not available here\n", (int)wanted);
            renderer_refused = request;
        }
        /* A world load mid-batch has members still to be announced; the
         * replay would bake them before the batch does. Next frame. */
        else if( !ToriDraw_SceneBatchBuilding(app.scene) )
        {
            if( !renderer_switch(wanted) )
                renderer_refused = request;
        }
    }
    renderer_publish();
}
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
static int sim_after_ready;
static int sim_ready;
static int sim_ready_failed;
static uint64_t sim_ready_start_ms;
static uint64_t sim_next_frame_ms;

#if defined(TORIRS_PLATFORM_WEB)
/*
 * The host's way in: a batch of cmdbus frames, straight from the page.
 *
 * The client is embedded — a dev tool, an editor, a test page — and the host
 * wants to say "open interface 600" or "set varp 300", which no synthesised
 * click expresses. The TORIRS_SIM_* harnesses answer that natively but are read
 * once before the loop, and several call App_BootWait, which spins on
 * TaskRunner_Step and never returns against this lane's asynchronous IO. So the
 * seam has to be the thing that is already drained once per iteration.
 *
 * The wire is src/web/torirs_channel.js's, which is cmdring.h's, which is the
 * record-file format: [u32 type][u16 length][payload], little-endian, several
 * concatenated. A host-driven session therefore records and replays like any
 * other, with its commands at the frames the input around them landed on.
 *
 * VALIDATED, NOT ASSERTED. Everywhere else in this file a malformed frame would
 * be a bug in our own producer and would assert. These bytes are written by a
 * separate implementation in another language, and OPT=1 compiles asserts out
 * anyway (src/makefile's -DNDEBUG note), so a truncated batch has to be caught
 * here or it walks the heap. Refusing is not a silent failure: it returns the
 * count accepted and says what it rejected.
 *
 * Returns frames accepted, or -1 if the batch was malformed (frames before the
 * bad header are still accepted — the ring took them and the drain will run
 * them). A full ring also stops the walk; the count says how far it got.
 */
EMSCRIPTEN_KEEPALIVE int
torirs_cmdbus_push_bytes(
    const uint8_t* data,
    int length)
{
    /* cmdring.h's header, restated as offsets rather than as the struct,
     * because what crosses is a byte layout and reading it as one is what makes
     * the two implementations agree. */
    enum
    {
        HEADER_BYTES = 6
    };
    int offset = 0;
    int accepted = 0;

    if( !data || length < 0 )
    {
        fprintf(stderr, "cmdbus: push_bytes given no batch\n");
        return -1;
    }

    while( offset < length )
    {
        uint32_t type;
        uint32_t payload_length;

        if( length - offset < HEADER_BYTES )
        {
            fprintf(
                stderr,
                "cmdbus: push_bytes truncated header at %d of %d, %d frames in\n",
                offset,
                length,
                accepted);
            return -1;
        }
        type = (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) |
               ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
        payload_length = (uint32_t)data[offset + 4] | ((uint32_t)data[offset + 5] << 8);
        offset += HEADER_BYTES;

        if( payload_length > TORIRS_CMD_MAX_PAYLOAD ||
            payload_length > (uint32_t)(length - offset) )
        {
            fprintf(
                stderr,
                "cmdbus: push_bytes frame type %u claims %u bytes, %d remain\n",
                type,
                payload_length,
                length - offset);
            return -1;
        }

        if( !CmdBus_Push(&bus, type, data + offset, (uint16_t)payload_length) )
        {
            /* The ring is full. Not an error in the batch: the host is ahead of
             * the frame loop, and the frames it already gave us will drain. */
            fprintf(stderr, "cmdbus: push_bytes ring full, %d of the batch accepted\n", accepted);
            return accepted;
        }
        offset += (int)payload_length;
        accepted++;
    }
    return accepted;
}
#endif

/**
 * @brief Frame at which EIP sampling begins; see the call site for why.
 *
 * Read once and cached. This sits on the per-frame path, and the whole point
 * of the sampler is that it does not perturb what it measures -- a getenv per
 * frame would be a small lie told nine hundred times.
 */
static long
eip_sample_warmup_frames(void)
{
    static long warmup = -1;
    const char* v;

    if( warmup >= 0 )
        return warmup;
    v = getenv("TORIDRAW_EIP_SAMPLE_WARMUP");
    warmup = v ? atol(v) : 100;
    if( warmup < 1 )
        warmup = 1;
    return warmup;
}
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
static int world_reported;
static char const* sim_sethide;
static int sim_sethide_done;
static char const* sim_setvarp;
static char const* sim_settab;
static int sim_settab_done;
static int uncapped;
/* TORIRS_PACE_SPIN=1: burn the pacing wait instead of sleeping it. Profiling
 * aid for isolating wake-up cost from render cost; see docs/PERF_HARNESS.md. */
static int pace_spin;

/*
 * How the frame is paced. `--pacer=NAME` or TORIRS_PACER, NAME being
 * `gameshell` (default) or `deadline`; see src/pacer.h for what each
 * one does and what it costs.
 *
 * The flag wins over the environment: an env var is ambient and a flag is a
 * decision made for this run.
 */
static struct ToriRS_Pacer frame_pacer;
static char const* pacer_name_opt;

/*
 * Milliseconds per drawn frame. 20 (50 fps) unless TORIRS_FRAME_MS says otherwise.
 *
 * This is a knob because the frame rate is not comparable between clients and
 * was assumed to be. The Java client on the XP target renders far below 50: its
 * GameShell paces `mainloop()` (logic) against `deltime` and lets `mainredraw()`
 * run once per iteration with only a 1 ms sleep. Ours pins the draw at 20 ms and
 * hits it. Comparing "% of one core" between a client doing 50 draws a second
 * and one doing 23 measures the frame rate, not the renderer.
 *
 * That 1 ms sleep is NOT free, which is where the earlier reading of this went
 * wrong: it concluded the Java draw rate "floats to whatever the machine
 * manages" and that the client was raster-bound down to 31. Instrumenting
 * GameShell's own loop on the box says otherwise -- in-world it asks for its
 * 1 ms floor on 100 % of frames and the OS charges it ~16 ms, 41 % of a 43 ms
 * frame, because nothing in that process holds the Windows timer period down
 * and the wait rounds up to a 15.625 ms tick. Removing only the floor, with the
 * raster work untouched, takes it 23.0 fps -> 43.4 fps. It is raster-bound to
 * ~44 and then sleep-bound the rest of the way.
 *
 * Per frame we are still the cheaper client, and by more than was thought: when
 * the Java client is actually running rather than idling it spends 20.7 CPU ms
 * per frame against our 14.96. The 16.23 ms once recorded here was an average
 * over a frame that is 41 % sleep.
 *
 * So this exists to hold the draw rate fixed while comparing, and to let the
 * deployed cap be set deliberately rather than by a literal buried in the pacer.
 */
static int
frame_period_ms(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* v = getenv("TORIRS_FRAME_MS");
        int ms = (v && *v) ? atoi(v) : 20;
        /* A zero or negative period would spin the loop with no wait at all;
         * that is what --uncapped is for, and it says so explicitly. */
        cached = ms > 0 ? ms : 20;
    }
    return cached;
}

/*
 * GameShell's `mindel`: the floor under the wait, in ms. 1 is the reference's
 * value. 0 is the arm that removes the floor entirely -- on the XP target that
 * is worth 23.0 -> 43.4 fps in the Java client, so it is the first thing to
 * reach for when this pacer looks slow.
 */
static int
pacer_mindel_ms(void)
{
    char const* v = getenv("TORIRS_PACER_MINDEL");
    int ms;
    if( !v || !*v )
        return 1;
    ms = atoi(v);
    return ms > 0 ? ms : 0;
}

static enum ToriRS_PacerKind
pacer_kind_selected(void)
{
#if defined(TORIRS_PLATFORM_WEB)
    /*
     * The browser paces us -- requestAnimationFrame decides when a frame runs
     * and there is no wait here to own. A rate estimator that cannot act on its
     * estimate would only skew the logic clock, so web always takes the
     * wall-clock pacer regardless of what was asked for.
     */
    return TORIRS_PACER_DEADLINE;
#else
    char const* name = pacer_name_opt;
    int ok = 0;
    enum ToriRS_PacerKind kind;

    if( !name || !*name )
        name = getenv("TORIRS_PACER");
    if( !name || !*name )
        return TORIRS_PACER_GAMESHELL;

    kind = ToriRS_Pacer_KindFromName(name, &ok);
    if( !ok )
    {
        /* Naming a pacer that does not exist is a typo, and silently running
         * the default would hide it for the whole run -- which, for a knob
         * whose entire purpose is A/B measurement, invalidates the arm. */
        fprintf(stderr, "torirs: unknown pacer '%s' (expected 'gameshell' or 'deadline')\n", name);
        exit(2);
    }
    return kind;
#endif
}

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

/*
 * The `chrome_scale=dynamic` ladder: device pixels per ToriRSChrome pixel.
 *
 * Two questions live in here, and conflating them is what makes a HighDPI boot
 * silently resize its own panels. HOW BIG the chrome should be is a question
 * about ROOM, and room is measured in POINTS -- 500 rows of window per step,
 * so the classic 503-row frame is one step and a window twice that is two. How
 * many pixels one chrome pixel spans is a question about the DISPLAY, and that
 * is the density. The scale is the product of the two.
 *
 * Reading the ladder off the raw canvas instead counts density twice: on a 2x
 * display a 733-point window is a 1466-row canvas, which reads as two steps
 * and then gets drawn at 2x anyway -- panels half again as large as the same
 * window gets on an ordinary display, purely for being drawn sharply. Turning
 * HighDPI on is supposed to change the RESOLUTION of the chrome and nothing
 * else about it.
 *
 * No upper clamp: App_SetChromeScale holds it to what the bake carries
 * (TORIRS_CHROME_SCALE_MAX), which is the one place that knows.
 */
static int
main_dynamic_chrome_scale(
    int canvas_h,
    int density)
{
    int steps;

    assert(canvas_h > 0);
    assert(density > 0);
    steps = canvas_h / density / 500;
    if( steps < 1 )
        steps = 1;
    return steps * density;
}

/** One iteration of the frame loop. Returns 0 when the client should stop. */
static int
frame_loop_step(void)
{
#if defined(TORIRS_PLATFORM_WEB)
    /* Carry last frame's queued cache reads to the IO server and take delivery
     * of whatever came back. Nothing else in the process runs every frame, and
     * a request nobody carries parks the task queue forever. */
    PlatformWeb_Pump();

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
    PlatformWeb_SetBlockingReads(app.app_state != APP_STATE_READY);

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
        int waiting = app.app_state != APP_STATE_READY && PlatformWeb_PendingTotal() > 0;
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
                    paced_mode < 0                ? "init"
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
    if( PlatformWindow_QuitRequested(platform) )
    {
        /* Both dumps are no-ops unless their env knob asked for them, and
         * both are idempotent, so the two exits below can each call them
         * without agreeing on which one runs. */
        ToriDraw_EipSampleStop("quit");
        ToriDraw_FrameAbDump("quit");
        return 0;
    }

    uint64_t now;
    /*
     * The clock App_RunOnce derives its logic tick count from. Equal to `now`
     * under the deadline pacer; under the GameShell pacer it is the pacer's own
     * clock, advanced by exactly the ticks its `ratio` owes this iteration.
     * Nothing else in the frame may use it -- input stamps, plugin frame starts
     * and animation all want real time.
     */
    uint64_t logic_now;
    int app_redraw;
    uint64_t frame_start_us;
    /* When the screen is next allowed to be redrawn.
     *
     * Only consulted while the async pipeline has work and the loop is
     * therefore not sleeping: without it, a loop spinning to drain IO would
     * present every iteration and spend on redraws exactly the time the spin
     * exists to give back. The present-skip that reads it is not native-only,
     * so neither is the deadline. */
    static uint64_t next_draw_ms;
#if !defined(__EMSCRIPTEN__)
    uint64_t frame_start_ms;
#endif

    /* Counted whether or not a cap is set: the TORIRS_SIM_* harness knobs
     * address frames by this number, and an uncapped run -- the only one
     * whose logic ticks are wall-clock, so the only one that behaves like a
     * player's -- used to leave it at zero and never fire them. */
    if( sim_after_ready )
    {
        uint64_t const simulation_now = PlatformWindow_Ticks64();
        if( !sim_ready )
        {
            if( app.app_state == APP_STATE_READY && app.screen == APP_SCREEN_GAME &&
                !App_AsyncPending(&app) &&
                (!app.net || (app.net->state == TORIRS_NET_GAME && app.rebuild_zone_x >= 0)) )
            {
                sim_ready = 1;
                frame_count = 0;
                sim_next_frame_ms = simulation_now;
                TORIRS_REPORT(
                    "SIM_READY elapsed_ms=%llu tree_generation=%u\n",
                    (unsigned long long)(simulation_now - sim_ready_start_ms),
                    app.tree ? app.tree->generation : 0);
                if( g_torirs_perf_enabled )
                {
                    TorirsPerf_Shutdown();
                    TorirsPerf_Init(1);
                    TORIRS_REPORT(
                        "SIM_PERF_BEGIN: native gameplay ready; startup samples excluded\n");
                }
            }
            else if( simulation_now - sim_ready_start_ms > 60000 )
            {
                TORIRS_ERR("SIM_READY failed: gameplay did not become ready within 60 seconds\n");
                sim_ready_failed = 1;
                return 0;
            }
        }
        /* Loading spins drain real IO; they are not scenario time. Advancing
         * this clock at most once per 20 ms also prevents a later async mount
         * from consuming the whole test before its server can answer. */
        if( sim_ready && simulation_now >= sim_next_frame_ms )
        {
            frame_count++;
            sim_next_frame_ms = simulation_now + 20;
        }
    }
    else
        frame_count++;
    if( max_frames > 0 && frame_count > max_frames )
    {
        ToriDraw_EipSampleStop("frames");
        ToriDraw_FrameAbDump("frames");
        return 0;
    }

    /*
     * Switch the EIP sampler on once the run has reached steady state.
     *
     * The first frames of a bounded run are scene load-in: cold caches, cache
     * archives being decompressed, models being built. Sampling those charges
     * the frame's composition to code that runs a hundred times in a
     * nine-hundred-frame run, which is precisely the misattribution the
     * sampler exists to avoid. TORIDRAW_EIP_SAMPLE_WARMUP=N moves the line;
     * the default is deliberately generous, since 100 of 900 frames is 11% of
     * the run and the steady state is what the remaining 89% measures.
     *
     * Start, not Stop, carries the warmup: stopping is driven by the exits
     * above, which is where the run actually ends.
     */
    if( frame_count == eip_sample_warmup_frames() )
        ToriDraw_EipSampleStart();

    /* Unconditional, unlike frame_count above, which only moves when the
     * run was bounded. TORIRS_WEDGE_CAM_PATH phases off this, and a camera
     * that only moved under TORIRS_MAX_FRAMES would be a knob you could not
     * eyeball before trusting it. */
    {
        extern long g_torirs_frame_no;
        g_torirs_frame_no++;
    }

#if !defined(__EMSCRIPTEN__)
    /* The pacing budget starts before any frame work. The input timestamp
     * below is intentionally separate: using it as the origin omitted the
     * pre-poll work and made nominal 20 ms frames longer than 20 ms. */
    frame_start_ms = PlatformWindow_Ticks64();
#endif
    /* The developer overlay's readout (App_NoteFrameTime), which measures the
     * same interval the perf harness calls a frame — and, like it, is closed
     * before the pacing sleep so the number is work and not the cap. Sampled
     * on every platform: the browser lane has no sleep to exclude but has the
     * same question to answer. */
    frame_start_us = PlatformWindow_TicksUs();
#if defined(TORIRS_FRAME_TIMES)
    ToriRS_FrameTimes_Begin(frame_start_us);
#endif
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
            if( period_us >= 4000 && (raf_baseline_us == 0 || period_us < raf_baseline_us) )
                raf_baseline_us = period_us;
            else if( raf_baseline_us != 0 && period_us > raf_baseline_us + raf_baseline_us / 2 )
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
                TORIRS_LOG("bmp_series: frame_count=%ld\n", frame_count);
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
                snprintf(
                    path_b, sizeof(path_b), "%s/frame_%05ld_bucket.bmp", series_dir, frame_count);
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
        TORIRS_ERR(
            "boot: %llums  frames=%d steps=%ld capped=%d\n",
            (unsigned long long)(PlatformWindow_Ticks64() - boot_start_ms),
            app.boot_frames,
            app.boot_steps,
            app.boot_frames_budget_capped);
        ToriRS_BootTelemetry_Report(stderr, &app.runner, &app.exec_runner);
    }
    /* The whole startup, once the world is in: the title's report above ends
     * before login, and the marks after it (login, world load) are what a
     * networked boot is mostly made of. */
    if( boot_stats && !world_reported && app.app_state == APP_STATE_READY && app.world_active )
    {
        world_reported = 1;
        TORIRS_ERR("boot: world ready\n");
        ToriRS_BootTelemetry_Report(stderr, &app.runner, &app.exec_runner);
    }
    if( boot_stats && frame_count == max_frames - 1 )
        TORIRS_ERR(
            "post-boot: busy_frames=%d busy_steps=%ld (frames that used the "
            "whole budget with work still queued)\n",
            app.busy_frames,
            app.busy_steps);

    if( sim_openmain > 0 && !sim_openmain_done && app.app_state == APP_STATE_READY )
    {
        TORIRS_LOG("sim_openmain: opening main modal iface=%d\n", sim_openmain);
        RS_UISlots_OpenMain(&app, sim_openmain);
        sim_openmain_done = 1;
    }

    if( sim_openside > 0 && !sim_openside_done && app.app_state == APP_STATE_READY )
    {
        TORIRS_LOG("sim_openside: opening side panel iface=%d\n", sim_openside);
        RS_UISlots_OpenSide(&app, sim_openside);
        sim_openside_done = 1;
    }

    if( sim_openchat > 0 && !sim_openchat_done && app.app_state == APP_STATE_READY )
    {
        TORIRS_LOG("sim_openchat: opening chat dialog iface=%d\n", sim_openchat);
        RS_UISlots_OpenChat(&app, sim_openchat);
        sim_openchat_done = 1;
    }

    if( sim_settab && !sim_settab_done && app.app_state == APP_STATE_READY )
    {
        char* tab_sep = NULL;
        int tabno = (int)strtol(sim_settab, &tab_sep, 0);
        int tab_iface = tab_sep && *tab_sep == ':' ? (int)strtol(tab_sep + 1, NULL, 0) : -1;
        TORIRS_LOG("sim_settab: tab=%d iface=%d\n", tabno, tab_iface);
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
            TORIRS_LOG("sim_sethide: com=%ld hide=%d\n", com, hide);
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
            TORIRS_LOG("sim_music: playing track %d\n", sim_song_id);
            App_PlaySong(&app, sim_song_id, true, 0, 0);
        }
        if( sim_jingle_id >= 0 )
        {
            TORIRS_LOG("sim_music: playing jingle %d\n", sim_jingle_id);
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
            TORIRS_LOG("sim_sound: queueing effect %d loops=%d\n", sim_sound_id, sim_sound_loops);
            App_PlaySound(&app, sim_sound_id, sim_sound_loops, 0);
            sim_sound_next = sim_sound_every > 0 ? frame_count + sim_sound_every : -1;
        }
    }

    if( replay )
    {
        if( !CmdReplay_PumpFrame(replay, &bus, &replay_now) )
            return 0; /* recording exhausted */
        now = replay_now;
        /* A recording carries its own clock and is not paced at all (the wait
         * below is skipped for `replay`). Feeding those timestamps to a rate
         * estimator would have it measure the recording rather than the
         * machine, and would make replay depend on which pacer was selected. */
        logic_now = now;
    }
    else
    {
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_INPUT_PREP)
        {
            now = ContentTest_Begin(&app, sock, &bus, PlatformWindow_Ticks64());
            /* Once per iteration, before any frame work: this is the sample
             * point the GameShell pacer's ten-iteration ring is built on. */
            logic_now = ContentTest_Enabled() ? now : ToriRS_Pacer_BeginFrame(&frame_pacer, now);
            CmdBus_PushFrame(&bus, now);
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PLATFORM_POLL)
            {
                /*
                 * Before the poll, so the fingers this drain interprets are
                 * measured against the viewport the LAST frame actually drew.
                 * Publishing it after would test a touch against a box that
                 * does not exist on screen yet.
                 *
                 * And only while a world desc actually survived that walk:
                 * world_emit_desc is not cleared when the world goes away, so a
                 * login screen or a full-screen world map would otherwise hand
                 * the camera gesture a box that is no longer on the canvas --
                 * every other reader of the desc guards it the same way.
                 */
                PlatformWindow_SetTouchViewport(
                    platform,
                    app.world_view_valid ? app.world_emit_desc.x : 0,
                    app.world_view_valid ? app.world_emit_desc.y : 0,
                    app.world_view_valid ? app.world_emit_desc.w : 0,
                    app.world_view_valid ? app.world_emit_desc.h : 0);
                PlatformWindow_PollCommands(platform, &bus);
                if( sock )
                    NetTransport_Poll(sock, app.net, &bus);
            }

            /* Scheduled plugin settings edits use the normal validated config API. */
            {
                static int initialized;
                static char const* cursor;
                if( !initialized )
                {
                    cursor = getenv("TORIRS_SIM_PLUGIN_CONFIG");
                    initialized = 1;
                }
                if( cursor && *cursor )
                {
                    long at = -1;
                    char id[64] = { 0 }, key[64] = { 0 }, value[192] = { 0 };
                    if( sscanf(cursor, "%ld,%63[^,],%63[^,],%191[^;]", &at, id, key, value) != 4 ||
                        at < 0 )
                    {
                        TORIRS_REPORT("sim_plugin_config: invalid input\n");
                        cursor = NULL;
                    }
                    else if( frame_count >= at && app.plugins )
                    {
                        int index = PluginHost_IndexOf(app.plugins, id);
                        int applied =
                            index >= 0 && PluginHost_ConfigSet(app.plugins, index, key, value);
                        TORIRS_REPORT(
                            "sim_plugin_config: frame=%ld id=%s key=%s applied=%d\n",
                            frame_count,
                            id,
                            key,
                            applied);
                        char const* next = strchr(cursor, ';');
                        cursor = next ? next + 1 : NULL;
                    }
                }
            }

            /* Drive the same enable/disable operation used by plugin settings. */
            {
                static int initialized;
                static char const* cursor;
                if( !initialized )
                {
                    cursor = getenv("TORIRS_SIM_PLUGIN_TOGGLE");
                    initialized = 1;
                }
                if( cursor && *cursor )
                {
                    long at = -1;
                    int enabled = -1;
                    char id[64] = { 0 };
                    if( sscanf(cursor, "%ld,%63[^,],%d", &at, id, &enabled) != 3 || at < 0 ||
                        (enabled != 0 && enabled != 1) )
                    {
                        TORIRS_REPORT("sim_plugin_toggle: invalid input\n");
                        cursor = NULL;
                    }
                    else if( frame_count >= at && app.plugins )
                    {
                        int index = PluginHost_IndexOf(app.plugins, id);
                        if( index >= 0 )
                            PluginHost_SetEnabled(app.plugins, index, enabled != 0);
                        TORIRS_REPORT(
                            "sim_plugin_toggle: frame=%ld id=%s enabled=%d applied=%d\n",
                            frame_count,
                            id,
                            enabled,
                            index >= 0);
                        char const* next = strchr(cursor, ';');
                        cursor = next ? next + 1 : NULL;
                    }
                }
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
                        TORIRS_LOG(
                            "sim_drag: %ld,%ld -> %ld,%ld button=%ld (%ld left)\n",
                            drag_x0,
                            drag_y0,
                            drag_x1,
                            drag_y1,
                            drag_button,
                            drag_repeats - 1);
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
                            TORIRS_LOG(
                                "sim_wheel: %ld,%ld notches=%ld x%ld\n",
                                wheel_x,
                                wheel_y,
                                wheel_notches,
                                wheel_repeats);
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
                        TORIRS_LOG("sim_hook: com=0x%lx script=%d\n", hook_com, hook.script_id);
                        /* A real op click latches which op it was; an onop script
                         * that switches on event_opindex (every list row does) is
                         * a no-op without it. 1 = the primary left-click op. */
                        app.host.event_op_index = 1;
                        RS_CS2_DispatchHook(&app.host, &app.runner, (int)hook_com, &hook);
                    }
                    else
                        TORIRS_ERR("sim_hook: component 0x%lx not found\n", hook_com);
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
                    /* TORIRS_REPORT and not TORIRS_LOG: @see the sim_varbit
                     * receipt below for why a drive lever's only receipt may
                     * not be compiled out of the build every capture uses. */
                    TORIRS_REPORT(
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

                    /* REPORT, not LOG: the harness reads this in the OPT build to
                     * tell "the op was sent" from "no such npc was in the scene". */
                    TORIRS_REPORT(
                        "sim_opnpc: op=%ld npc=%ld slot=%d\n", sim_opnpc_op, sim_opnpc_npc, slot);
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
                /* TORIRS_SCREENSHOT=<name>: a picture of the frame from the
                 * renderer that is ACTUALLY drawing it -- glReadPixels on the GL
                 * lanes, GetRenderTargetData on D3D9, the canvas on soft3d.
                 *
                 * Distinct from TORIRS_EXIT_BMP, which re-renders through
                 * App_Render into a plain buffer: that is the software rasteriser
                 * no matter which renderer the run selected, so it cannot answer
                 * any question about GPU state and quietly looks like it can.
                 *
                 * TORIRS_SCREENSHOT_FRAME picks when; the default is late enough
                 * to be in the world rather than on the loading bar. */
                {
                    static int shot_done = 0;
                    /* Its own counter. frame_count only advances when
                     * TORIRS_MAX_FRAMES is set -- the ++ sits behind that
                     * short-circuit at the top of the loop -- so keying off it
                     * made this silently never fire in a time-bounded run. */
                    static long shot_frames = 0;
                    char const* shot_name = torirs_env_screenshot();

                    shot_frames++;
                    if( !shot_done && shot_name && *shot_name )
                    {
                        char const* at = getenv("TORIRS_SCREENSHOT_FRAME");
                        long shot_frame = at ? strtol(at, NULL, 0) : 400;
                        if( shot_frames >= shot_frame )
                        {
                            char path[512];
                            shot_done = 1;
                            if( App_RequestScreenshot(
                                    &app,
                                    getenv("TORIRS_SCREENSHOT_DIR"),
                                    shot_name,
                                    path,
                                    (int)sizeof(path)) &&
                                path[0] )
                                TORIRS_REPORT("screenshot: queued %s\n", path);
                            else
                                TORIRS_REPORT("screenshot: refused\n");
                        }
                    }
                }
                /* TORIRS_CS2_HARNESS=<cases.json>: run the cross-client case list
                 * once the client is far enough in to have a cache, a host and a
                 * runner, then leave. TORIRS_CS2_HARNESS_FRAME picks how far in;
                 * the default is late enough for login to have completed against
                 * ToriRSServer, because a case that reads a varp needs the varps.
                 * The run ends the way every other headless run here ends, with
                 * TORIRS_MAX_FRAMES — the harness does not invent a second exit
                 * path. */
                {
                    static int harness_done = 0;
                    char const* harness_cases = torirs_env_cs2_harness();
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
                    TORIRS_LOG("sim_runscript: script=%ld argc=%d\n", rs_script, rs_argc);
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

            /*
             * TORIRS_SIM_VARBIT="<frame>,<id>,<value>[;<frame>,<id>,<value>...]":
             * write a client varbit at a frame.
             *
             * The All Settings rows are varbits, and nothing in the cache writes
             * one -- the panel's own row does, through a path that needs a real
             * click on a real mounted panel. That makes every one of the seventy-four
             * Activities rows unverifiable from a headless run without either a
             * click script per row or this. See NXT_CLIENT_PLUGINS.md.
             *
             * Optimistic, i.e. exactly what the panel's write is: the value stands
             * until the server says otherwise, which offline it never does.
             */
            {
                static char const* vb_cursor = NULL;
                static int vb_init = 0;
                static long vb_frame = -1;
                static long vb_id = 0;
                static long vb_value = 0;
                if( !vb_init )
                {
                    vb_init = 1;
                    vb_cursor = getenv("TORIRS_SIM_VARBIT");
                }
                if( vb_frame < 0 && vb_cursor && *vb_cursor )
                {
                    char* end = NULL;
                    vb_frame = strtol(vb_cursor, &end, 0);
                    if( end && *end == ',' )
                    {
                        vb_id = strtol(end + 1, &end, 0);
                        vb_value = (end && *end == ',') ? strtol(end + 1, &end, 0) : 0;
                        vb_cursor = (end && *end == ';') ? end + 1 : NULL;
                    }
                    else
                    {
                        vb_cursor = NULL;
                        vb_frame = -1;
                    }
                }
                if( vb_frame >= 0 && frame_count >= vb_frame )
                {
                    VarPManager_SetVarbitOptimistic(&app.varps, (int)vb_id, (int)vb_value);
                    RS_CS2Host_NotifyVarChanged(&app.host, -1);
                    /*
                     * Mirror it to the server too, exactly as a panel click would.
                     *
                     * Ten Activities rows are decided server-side, and this
                     * variable is the only way to reach any row from a headless
                     * run -- nothing in the cache writes these varbits. A simulated
                     * write the server never heard about would leave every one of
                     * those rows untestable, which is the state that made them look
                     * unimplementable in the first place.
                     */
                    RS_CS2Host_QueueSettingsMirror(&app.host, (int)vb_id, (int)vb_value);
                    /*
                     * TORIRS_REPORT, not TORIRS_LOG: this is the RECEIPT for a
                     * lever, not narration.
                     *
                     * Every screenshot in tools/porcelain_gate/shots is taken
                     * with an OPT=1 binary, which is -DNDEBUG, which compiles
                     * TORIRS_LOG away -- so the one line that says whether the
                     * write landed, and what the varbit read back as, was
                     * absent from every capture log while `sim_cmd:` beside it
                     * printed. A cannon capture whose threshold row was never
                     * written therefore looked exactly like one whose write had
                     * landed and done nothing, and the reads-back field is what
                     * separates "the profile has no such varbit" from "the
                     * value is too wide for its bits".
                     *
                     * It meets the channel's own test: it prints only because
                     * someone set TORIRS_SIM_VARBIT.
                     */
                    TORIRS_REPORT(
                        "sim_varbit: %ld = %ld (base varp %d, reads back %d)\n",
                        vb_id,
                        vb_value,
                        VarPManager_VarbitBaseVar(&app.varps, (int)vb_id),
                        VarPManager_GetVarbit(&app.varps, (int)vb_id));
                    vb_frame = -1;
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
                    TORIRS_REPORT("sim_type: %c%ld at frame %ld\n", kind, val, frame_count);

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
                        TORIRS_LOG(
                            "sim_hotkey: '%s' -> osrs_key=%d at frame %ld\n",
                            name,
                            hk_key,
                            hk_frame);
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
                        /* REPORT, not LOG: a harness that set TORIRS_SIM_CMD wants
                         * to see its commands go out, in the optimized build too. */
                        TORIRS_REPORT("sim_cmd: frame %ld sent ::%s\n", (long)frame_count, text);
                    }
                    cmd_cursor = body[len] == ';' ? body + len + 1 : NULL;
                }
            }

            /*
             * TORIRS_SIM_SONG="frame,id": start a music track at that frame.
             *
             * A track is server-driven -- a content script's `midi_song` -- so
             * a lane whose scripts never reach that op plays nothing, and the
             * longest load chain in the client goes unexercised. That is a
             * measurement hole rather than a content one: the music loader's
             * cost is its round trips (runner telemetry, MusicLoad), and there
             * was no headless way to make it pay them. Same shape and same
             * reason as TORIRS_SIM_CMD above; the frame must land after login,
             * since the load needs the cache the session opened.
             */
            {
                static int song_init = 0;
                static long song_frame = -1;
                static long song_id = -1;
                if( !song_init )
                {
                    char const* spec = getenv("TORIRS_SIM_SONG");
                    song_init = 1;
                    if( spec && *spec )
                    {
                        char* end = NULL;
                        song_frame = strtol(spec, &end, 0);
                        if( end && *end == ',' )
                            song_id = strtol(end + 1, NULL, 0);
                        else
                            song_frame = -1;
                    }
                }
                if( song_id >= 0 && song_frame >= 0 && frame_count >= song_frame &&
                    app.app_state == APP_STATE_READY )
                {
                    TORIRS_REPORT("sim_song: play %ld\n", song_id);
                    App_PlaySong(&app, (int)song_id, true, 0, 0);
                    song_id = -1;
                }
            }

            /*
             * TORIRS_SIM_KEYHOLD="<LibToriRS_KeyCode>[,<code>...]": press these
             * keys once, on the first frame, and never release them.
             *
             * Exists for the click sims below, which have no way to say "with
             * shift down" -- and shift is not a decoration on a right click, it is
             * what makes a whole class of rows appear at all. The cache's own
             * client ops ("Mark tile", "Tag") are shift-gated, and without this
             * there is no headless way to reach one.
             *
             * Held rather than pulsed because `key_held` is sticky until a key-up
             * (LibToriRS_Input_End): one press at the top of the run is the whole
             * mechanism, and nothing here ever wants to let go.
             */
            {
                static int keyhold_done = 0;
                static long keyhold_frame = -1;
                /* TORIRS_SIM_KEYHOLD_FRAME=N delays the press to loop iteration N:
                 * a key pressed on frame 1 lands on the title screen, and a held
                 * arrow there rotates no camera. */
                if( keyhold_frame < 0 )
                {
                    char const* at = getenv("TORIRS_SIM_KEYHOLD_FRAME");
                    keyhold_frame = at ? strtol(at, NULL, 0) : 1;
                    if( keyhold_frame < 1 )
                        keyhold_frame = 1;
                }
                if( !keyhold_done && frame_count >= keyhold_frame )
                {
                    char const* spec = getenv("TORIRS_SIM_KEYHOLD");
                    keyhold_done = 1;
                    while( spec && *spec )
                    {
                        char* end = NULL;
                        long code = strtol(spec, &end, 0);
                        if( end == spec )
                            break;
                        CmdBus_PushKey(&bus, TORIRS_CMD_INPUT_KEY_DOWN, (uint8_t)code);
                        TORIRS_REPORT("sim_keyhold: holding key %ld\n", code);
                        spec = (end && *end == ',') ? end + 1 : NULL;
                    }
                }
            }

            /*
             * TORIRS_SIM_CAMERA_YAW="<0..2047>" with
             * TORIRS_SIM_CAMERA_YAW_FRAME=N: park the follow camera at a known
             * yaw from loop frame N on, so a world overlay can be photographed
             * with its subject IN the viewport instead of beside it.
             *
             * The yaw lives in `orbit`, not in world_camera: the follow step
             * rebuilds world_camera.yaw from orbit.yaw every cycle, so a park
             * written to world_camera survives exactly one render. The
             * pre-loop block below got away with that because it renders and
             * exits; from inside the loop it would simply not turn the camera.
             * yaw_velocity goes to zero with it -- the reference's camera
             * coasts, and a park that leaves a velocity behind drifts away
             * from the angle the drive asked for over the frames that follow.
             *
             * This is the lever the entity-highlighter's live capture needed
             * and did not have. Its three tagged npcs stand 11-12 tiles north
             * and 5-8 tiles east of the only teleport that lane can reach;
             * `wanderrange` keeps them from walking off, which is what the job
             * comment argued, but nothing kept them in FRONT of a camera whose
             * reset yaw looks north -- two of the three projected past the
             * viewport's right edge and the drive had no way to say otherwise.
             */
            {
                static int yaw_done = 0;
                static long yaw_frame = -1;
                if( yaw_frame < 0 )
                {
                    char const* at = getenv("TORIRS_SIM_CAMERA_YAW_FRAME");
                    yaw_frame = at ? strtol(at, NULL, 0) : 1;
                    if( yaw_frame < 1 )
                        yaw_frame = 1;
                }
                if( !yaw_done && getenv("TORIRS_SIM_CAMERA_YAW") &&
                    getenv("TORIRS_SIM_CAMERA_YAW_FRAME") && frame_count >= yaw_frame )
                {
                    yaw_done = 1;
                    app.orbit.yaw = ToriDraw_NormalizeAngle(
                        (int)strtol(getenv("TORIRS_SIM_CAMERA_YAW"), NULL, 0));
                    app.orbit.yaw_velocity = 0;
                    /*
                     * Yaw only, and the pitch is deliberately NOT a second
                     * knob beside it. Measured on this lane: the reset pitch
                     * of 148 puts the highlighter's trio at y=13 and a park at
                     * 190 puts them off the top of the viewport at y=4, so the
                     * pitch band's flat end (128) is worth about four pixels
                     * of headroom and its steep end throws the subject away
                     * entirely. A knob that cannot move the picture in the
                     * direction a drive needs is a knob nobody can use.
                     *
                     * REPORT for the same reason sim_cmd is: a drive that
                     * asked for an angle wants the angle in the optimized
                     * build's log, not only in a debug one.
                     */
                    TORIRS_REPORT(
                        "sim_camera_yaw: frame %ld parked at %d\n",
                        (long)frame_count,
                        app.orbit.yaw);
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
                        TORIRS_REPORT(
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
                        TORIRS_REPORT("sim_click_at: released %ld,%ld\n", pend_x, pend_y);
                        pend_frame = -1;
                    }
                }
            }

            /* TORIRS_SIM_CLICK_NPC="frame,npc_type[,right]": click the first live
             * npc of that cache type (-1: any npc) where it is DRAWN, inside the
             * world viewport. The pointer moves to the
             * npc's projected body on the frame, presses three frames later and
             * releases the frame after, like TORIRS_SIM_CLICK_AT; the projection
             * is asked again every frame until the npc is in the scene and on
             * screen. Wandering npcs make a fixed coordinate a coin toss, and a
             * right-click with TORIRS_SIM_KEYHOLD's shift is the only headless
             * way to a plugin's rows on an npc's menu. */
            {
                static int sim_npc_init = 0;
                static long npc_frame = -1, npc_type, npc_right, npc_step = -1;
                static int npc_x, npc_y, npc_followed_type = -1;
                if( !sim_npc_init )
                {
                    char const* spec = getenv("TORIRS_SIM_CLICK_NPC");
                    char* end = NULL;
                    sim_npc_init = 1;
                    if( spec && *spec )
                    {
                        npc_frame = strtol(spec, &end, 0);
                        if( end && *end == ',' )
                        {
                            npc_type = strtol(end + 1, &end, 0);
                            npc_right = (end && *end == ',') ? strtol(end + 1, &end, 0) : 0;
                        }
                        else
                            npc_frame = -1;
                    }
                }
                if( npc_frame >= 0 && frame_count >= npc_frame )
                {
                    if( npc_step < 0 )
                    {
                        int found_type = -1;
                        int slot =
                            App_NpcScreenPosition(&app, (int)npc_type, &npc_x, &npc_y, &found_type);
                        if( slot >= 0 )
                        {
                            npc_step = 0;
                            npc_followed_type = found_type;
                            CmdBus_PushMouseMove(&bus, npc_x, npc_y);
                            TORIRS_REPORT(
                                "sim_click_npc: frame=%ld type=%d slot=%d move %d,%d right=%ld\n",
                                frame_count,
                                found_type,
                                slot,
                                npc_x,
                                npc_y,
                                npc_right);
                        }
                    }
                    else
                    {
                        uint8_t btn = npc_right ? 3 : 1;
                        npc_step++;
                        /* Follow the body until the press: a wandering npc walks
                         * out from under a pointer parked three frames earlier,
                         * and the menu then belongs to the ground it stood on. */
                        if( npc_step < 3 )
                        {
                            int follow_type = -1;
                            int follow_x;
                            int follow_y;
                            if( App_NpcScreenPosition(
                                    &app, (int)npc_type, &follow_x, &follow_y, &follow_type) >= 0 &&
                                (npc_type >= 0 || follow_type == npc_followed_type) )
                            {
                                npc_x = follow_x;
                                npc_y = follow_y;
                                CmdBus_PushMouseMove(&bus, npc_x, npc_y);
                            }
                        }
                        if( npc_step == 3 )
                            CmdBus_PushMouseButton(
                                &bus, TORIRS_CMD_INPUT_MOUSE_DOWN, btn, npc_x, npc_y);
                        else if( npc_step >= 4 )
                        {
                            CmdBus_PushMouseButton(
                                &bus, TORIRS_CMD_INPUT_MOUSE_UP, btn, npc_x, npc_y);
                            TORIRS_REPORT("sim_click_npc: released %d,%d\n", npc_x, npc_y);
                            npc_frame = -1;
                        }
                    }
                }
            }

            /* TORIRS_SIM_MENU_ROW="frame,prefix": from that frame on, wait for an
             * open right-click menu with a row whose text starts with `prefix`
             * (colour tags included: "Tag @yel@") and left-click its centre with
             * the same move/press/release cadence as TORIRS_SIM_CLICK_AT. This is
             * how a plugin's retained menu row is picked headlessly: the menu's
             * position follows the click that opened it, so no fixed coordinate
             * can be written down in advance. */
            {
                static int sim_row_init = 0;
                static long row_frame = -1, row_step = -1;
                static char row_prefix[64];
                static int row_x, row_y;
                if( !sim_row_init )
                {
                    char const* spec = getenv("TORIRS_SIM_MENU_ROW");
                    char* end = NULL;
                    sim_row_init = 1;
                    if( spec && *spec )
                    {
                        row_frame = strtol(spec, &end, 0);
                        if( end && *end == ',' )
                            snprintf(row_prefix, sizeof(row_prefix), "%s", end + 1);
                        else
                            row_frame = -1;
                    }
                }
                if( row_frame >= 0 && frame_count >= row_frame )
                {
                    if( row_step < 0 )
                    {
                        char text[128];
                        if( App_MinimenuRowCenter(
                                &app, row_prefix, &row_x, &row_y, text, sizeof(text)) )
                        {
                            row_step = 0;
                            CmdBus_PushMouseMove(&bus, row_x, row_y);
                            TORIRS_REPORT(
                                "sim_menu_row: frame=%ld row '%s' move %d,%d\n",
                                frame_count,
                                text,
                                row_x,
                                row_y);
                        }
                    }
                    else
                    {
                        row_step++;
                        if( row_step == 3 )
                            CmdBus_PushMouseButton(
                                &bus, TORIRS_CMD_INPUT_MOUSE_DOWN, 1, row_x, row_y);
                        else if( row_step >= 4 )
                        {
                            CmdBus_PushMouseButton(
                                &bus, TORIRS_CMD_INPUT_MOUSE_UP, 1, row_x, row_y);
                            TORIRS_REPORT("sim_menu_row: released %d,%d\n", row_x, row_y);
                            row_frame = -1;
                        }
                    }
                }
            }

            /* TORIRS_SIM_MOVE_AT="frame,x,y[;frame,x,y...]": park the pointer at a
             * main-loop frame WITHOUT pressing anything. The hover-driven native
             * paths (the cache's mouse-over highlight groups, tooltips, hover
             * colours) need the pointer over a subject for many frames; a click
             * would also walk, talk or open a menu and change what is being
             * measured. */
            {
                static char const* sim_move_cursor = NULL;
                static int sim_move_init = 0;
                static long move_frame = -1, move_x, move_y;
                if( !sim_move_init )
                {
                    sim_move_init = 1;
                    sim_move_cursor = getenv("TORIRS_SIM_MOVE_AT");
                }
                if( move_frame < 0 && sim_move_cursor && *sim_move_cursor )
                {
                    char* end = NULL;
                    move_frame = strtol(sim_move_cursor, &end, 0);
                    if( end && *end == ',' )
                    {
                        move_x = strtol(end + 1, &end, 0);
                        move_y = (end && *end == ',') ? strtol(end + 1, &end, 0) : 0;
                        sim_move_cursor = (end && *end == ';') ? end + 1 : NULL;
                    }
                    else
                    {
                        sim_move_cursor = NULL;
                        move_frame = -1;
                    }
                }
                if( move_frame >= 0 && frame_count >= move_frame )
                {
                    CmdBus_PushMouseMove(&bus, (int)move_x, (int)move_y);
                    TORIRS_REPORT(
                        "sim_move_at: frame=%ld move %ld,%ld\n", move_frame, move_x, move_y);
                    move_frame = -1;
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
                    TORIRS_LOG("sim_resize: frame=%ld %ldx%ld\n", rz_frame, rz_w, rz_h);
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
                    TORIRS_LOG("sim_window: frame=%ld %ldx%ld\n", wz_frame, wz_w, wz_h);
                    PlatformWindow_SetWindowSize(platform, (int)wz_w, (int)wz_h);
                    wz_frame = -1;
                }
            }
        }
    }

#if !defined(TORIRS_PLATFORM_WEB)
    if( executor_cfg.js5_enabled &&
        PlatformXIO_Js5Pump(app.runner.px, PlatformWindow_Ticks64()) < 0 )
    {
        TORIRS_ERR(
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
        /*
         * The touch marker is sized in POINTS, so it follows the DISPLAY's
         * density and not the chrome ladder below.
         *
         * Those are two different questions, and the ladder answers the other
         * one: it asks how much ROOM there is and scales panels to fill it, so
         * on a large 1x window it returns 2 or 3 with no display density
         * involved at all. A marker has nothing to do with room -- it is sized
         * against the finger that made it -- so it takes the raw density and
         * lets the chrome take the product. Idempotent, and unconditional for
         * the same reason the block below is: a window dragged onto a display
         * of a different density raises no event that says so.
         */
        ToriRSInkwell_SetDensity(PlatformWindow_PixelDensity(platform));

        /*
         * On this platform the pointer IS a finger, so the finger may turn the
         * camera whatever the revision's desktop `controls=` list says.
         *
         * Set here rather than compiled into app.c so a desktop run can turn it
         * on -- a touchscreen laptop, or a test that wants the gesture without
         * a phone -- and so app.c is left stating the rule instead of the
         * platform. @see App.touch_camera.
         */
#if defined(TORIRS_PLATFORM_ANDROID)
        app.touch_camera = 1;
#endif
        /*
         * touch_ui is NOT set here any more, and neither is touch_scroll.
         *
         * Both are resolved in App_Init, beside the clientscript identity
         * they follow. This block runs from the frame loop, which is after
         * PluginHost_Start, so the capability a plugin reads at on_start --
         * the only place a key declaration can be made -- was false on every
         * lane and true from frame one onwards, with nobody listening by
         * then. Measured: CAPPROBE at=start touch=0, at=frame60 touch=1.
         */

        /* Cheap and unconditional: a window dragged from a Retina display to
         * an ordinary one changes density with no event that says so, and
         * App_SetChromeScale returns immediately when nothing moved. */
        if( !torirs_env_chrome_scale() )
        {
            if( app.cfg.chrome_scale < 0 )
            {
                /* dynamic: re-derive from the canvas every frame, so a drag
                 * to fullscreen steps the chrome up as the canvas grows -- and
                 * so a drag onto a display of a different density re-picks the
                 * baked face for it, which raises no event of its own. */
                int const density = PlatformWindow_PixelDensity(platform);
                int const scale = main_dynamic_chrome_scale(UITREE_LAYOUT_ROOT_H, density);
                if( App_SetChromeScale(&app, scale) && getenv("TORIRS_RESIZE_DEBUG") )
                    TORIRS_LOG(
                        "chrome: scale %d (canvas %dx%d, density %d)\n",
                        App_ChromeScale(&app),
                        UITREE_LAYOUT_ROOT_W,
                        UITREE_LAYOUT_ROOT_H,
                        density);
            }
            else if( app.cfg.chrome_scale == 0 )
                App_SetChromeScale(&app, PlatformWindow_PixelDensity(platform));
            /* > 0: pinned by the manifest; set once at boot, never followed. */
        }
        /*
         * The layout is the pointer's space and the frame's shape; the
         * buffer is what the renderer draws. A GPU renderer sizes its own
         * target from the same arithmetic (ClientScale_Present), so only the
         * software buffer is sized here -- to the render size, which is the
         * layout only at 100% interface scaling.
         */
        PlatformWindow_SetLayoutSize(platform, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        {
            int buffer_w = UITREE_LAYOUT_ROOT_W;
            int buffer_h = UITREE_LAYOUT_ROOT_H;
            bool gpu = false;
#if defined(TORIRS_HAVE_D3D9)
            gpu = gpu || d3d9;
#endif
#if defined(TORIRS_HAVE_GL3)
            gpu = gpu || gl3;
#endif
#if defined(TORIRS_HAVE_GLES2)
            gpu = gpu || gles2;
#endif
            if( !gpu )
                main_software_buffer_size(&app, platform, &buffer_w, &buffer_h);
            PlatformWindow_Resize(platform, buffer_w, buffer_h);
        }
#if defined(TORIRS_HAVE_D3D9)
        if( d3d9 )
            ToriPlatformWin32_Renderer_D3D9_SetViewport(d3d9, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
#if defined(TORIRS_HAVE_GL3)
        if( gl3 )
            ToriPlatformSDL2_Renderer_GL3_SetViewport(gl3, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
#if defined(TORIRS_HAVE_GLES2)
        if( gles2 )
            ToriPlatformAndroid_Renderer_GLES2_SetViewport(gles2, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
#if defined(TORIRS_HAVE_WEBGL2)
        if( webgl2 )
            ToriPlatformWeb_Renderer_WebGL2_SetViewport(webgl2, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
#if defined(TORIRS_HAVE_WEBGL1)
        if( webgl1 )
            ToriPlatformWeb_Renderer_WebGL1_SetViewport(webgl1, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
#if defined(TORIRS_HAVE_GLES3)
        if( gles3 )
            ToriPlatformAndroid_Renderer_GLES3_SetViewport(gles3, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
#endif
    }

    app_redraw = 0;
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_APP_RUN)
    {
        app_redraw = App_RunOnce(&app, logic_now, input);
        /* Acceptance sessions rasterize explicit checkpoints; logic still runs at 50 Hz. */
        if( ContentTest_Enabled() && getenv("TORIRS_CONTENT_TEST_CHECKPOINTS") )
        {
            int capture_pending = 0;
            for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
                capture_pending |= app.plugin_screenshots[i].in_use;
            if( !capture_pending )
                app_redraw = 0;
        }

        /*
         * While the async pipeline has work, this loop stops waiting out the
         * frame cap (see the pacing block at the end) and iterates as fast as
         * the work allows. The SCREEN must not follow it there -- redrawing
         * every iteration would spend on presents exactly the time the spin
         * exists to give back to the IO.
         *
         * So the present keeps the cap even when the loop does not: it is
         * allowed through when its own deadline has passed, and otherwise the
         * frame's work is done without drawing it. A frame with no outstanding
         * IO paces as it always did and reaches this with the deadline already
         * behind it.
         */
        /*
         * Two reasons the present skips a frame the loop just ran, and they
         * share one deadline.
         *
         * The async one is above: a loop spinning to drain IO must not spend
         * the time it saves on redraws.
         *
         * The other is a machine that cannot hold the frame rate. The pacer
         * steps its DRAW budget down when frames stop fitting (pacer.c), and
         * the present has to honour that or the step-down buys nothing -- the
         * loop would draw every iteration exactly as before and the longer
         * wait would never be reached. The world keeps ticking at period_ms
         * either way; only the screen slows down.
         */
        /*
         * And the third: the Display panel's "Limit Framerate" row, which the
         * person chose. It joins the pacer's own budget by taking whichever
         * asks for the longer gap -- a 15 fps cap on a machine already stepped
         * down to 30 is still 15, and a 60 fps cap does not undo a step-down
         * to 30.
         *
         * It caps the SCREEN and nothing else, exactly like the step-down
         * above it: the world still ticks at period_ms, so a capped client
         * plays at the same speed and simply draws less. That is the whole
         * point of the row on a phone, where the draw is the battery.
         */
        /* ONE lever. The configured cap -- revconfig's, or the cache's Limit
         * Framerate row where the profile hands it to CS2 -- is restated on
         * the pacer every frame and folded into the same draw budget its
         * adaptive step-down uses. There is no second cap path here any more:
         * the one there was gated the screen at 15 fps while the pacer, its
         * trace and the FPS readout all reported 50. */
        int draw_period_ms;
        ToriRS_Pacer_SetCapFps(&frame_pacer, App_FrameCapFps(&app));
        draw_period_ms = ToriRS_Pacer_DrawPeriodMs(&frame_pacer);
        if( app_redraw && !uncapped && !replay &&
            (App_AsyncPending(&app) || draw_period_ms > frame_pacer.period_ms) )
        {
            uint64_t const draw_now = PlatformWindow_Ticks64();
            if( draw_now < next_draw_ms )
                app_redraw = 0;
            else
            {
                /*
                 * The next deadline comes off the LAST one, not off now.
                 *
                 * A draw can only land on a loop iteration, and iterations are
                 * period_ms apart, so `now + budget` always rounds the gap UP
                 * to the next iteration: a 15 fps cap (66 ms) drew every 80 ms
                 * and measured 13.5 fps on the phone. Advancing the deadline
                 * itself lets the error alternate instead of accumulating --
                 * 80, 60, 60, 80 -- and the average is the rate that was
                 * asked for. A deadline already in the past (the client was
                 * busy, or had stopped drawing) resyncs to now rather than
                 * trying to catch up with a burst.
                 */
                uint64_t const next = next_draw_ms + (uint64_t)draw_period_ms;
                next_draw_ms = next > draw_now ? next : draw_now + (uint64_t)draw_period_ms;
            }
        }
    }
    /*
     * Nowhere to draw: no draw. The world and the network have already ticked
     * above; what is skipped is App_Render and the present, the whole of the
     * frame's cost on a phone. This is the state a phone-width plugin page
     * puts the client in -- the chrome takes the surface and the game is
     * hidden until it collapses -- and, measured, the client went on painting
     * the hidden world at ~0.6 of a core for as long as the page was up.
     * Retained presents are skipped for the same reason: there is no
     * retained frame to show and no surface to show it on.
     */
    if( !PlatformWindow_CanPresent(platform) )
        app_redraw = 0;
    /*
     * An input frame App_RunOnce did not consume is held over to the next
     * iteration (LibToriRS_Input_Continue) instead of being started fresh, so a
     * settlement wait cannot swallow the click that arrived during it.
     *
     * BOOTING is deliberately outside that: it returns before interaction too,
     * but it is a loading screen, and a click aimed at a screen that is gone by
     * the time the client can act on it is better dropped than replayed.
     *
     * A press whose release has not happened yet is not that. It is a gesture
     * still in progress -- the finger or the button is down NOW -- and dropping
     * its press edge does not drop the click, it splits it: `mouse_button_held`
     * survives Begin(), so the release still arrives as a click, only with
     * nothing pressed under it. The tree answers `clicked=-1` and the release
     * falls through to the world as a walk-here. Measured on the boot: the same
     * press-and-release on the same login toggle resolves to the component when
     * the press lands after READY and to nothing when it lands one frame
     * earlier.
     */
    input_frame_pending =
        !App_InputFrameConsumed(&app) &&
        (app.app_state == APP_STATE_READY || LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) ||
         LibToriRS_Input_IsMouseHeld(input, TORIRSM_MIDDLE) ||
         LibToriRS_Input_IsMouseHeld(input, TORIRSM_RIGHT));
    /* TORIRS_SWAP_DEBUG=1: how many of the last 300 loop iterations actually
     * re-rendered, beside the present cadence the platform prints. A screen
     * that updates less often than the loop runs is one of these two numbers
     * being low, and they name different culprits. */
    {
        static int debug_draw = -1;
        static int debug_frames = 0;
        static int debug_rendered = 0;
        static uint64_t debug_work_us = 0;
        static int debug_work_frames = 0;
        if( debug_draw < 0 )
            debug_draw = getenv("TORIRS_SWAP_DEBUG") != NULL;
        if( debug_draw )
        {
            debug_rendered += app_redraw ? 1 : 0;
            {
                /* Zero on a host that never called App_NoteFrameTime; such a
                 * frame is left out of the mean rather than counted as free. */
                uint64_t work = App_LastFrameUs(&app);
                if( work )
                {
                    debug_work_us += work;
                    debug_work_frames++;
                }
            }
            if( ++debug_frames == 300 )
            {
                /*
                 * WORK, not cadence. Under a cap the wall time between two of
                 * these lines is the cap and says nothing about the renderer;
                 * what a capped run can still compare is how much of the
                 * budget each frame spent. This is the same number the
                 * developer overlay draws as "Frame" -- App_NoteFrameTime
                 * closes it before the pacing sleep -- summed here so a run
                 * needs no screenshot to be read, and reading it needs no adb
                 * command that would perturb what it measures.
                 */
                fprintf(
                    stderr,
                    "draw: %d of %d loop iterations re-rendered "
                    "(cap %d fps, draw period %d ms, async %d) work mean %.2f ms "
                    "over %d frames\n",
                    debug_rendered,
                    debug_frames,
                    App_FrameCapFps(&app),
                    ToriRS_Pacer_DrawPeriodMs(&frame_pacer),
                    App_AsyncPending(&app) ? 1 : 0,
                    debug_work_frames ? (double)debug_work_us / debug_work_frames / 1000.0 : 0.0,
                    debug_work_frames);
                debug_frames = 0;
                debug_rendered = 0;
                debug_work_us = 0;
                debug_work_frames = 0;
            }
        }
    }
    if( ContentTest_DrawRequested(&app) )
        app_redraw = 1;
    if( app_redraw )
    {
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_DISPLAY)
        {
            interactive_render_present(
                &app, platform, gl3, d3d9, gles2, webgl2, webgl1, gles3,
                renderer_active_is_depth());
        }
    }
    else
    {
        TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PRESENT)
        {
            interactive_present_retained(platform, gl3, d3d9, gles2, webgl2, webgl1, gles3);
        }
    }

    /* Here, after the present: a frame is never half drawn by one renderer
     * and finished by another, and the drawn frame has just emptied the scene
     * queue the new renderer's replay goes into. */
    renderer_follow_request();

    /* Fixed mode: script 5355 carves the popout strip from the canvas. Grow the
     * canvas by the measured strip so the classic frame stays APP_CANVAS_MIN_W
     * and the strip sits outside it. Must run after App_RunOnce so open/close
     * layout (5354) has already widened/collapsed the strip. The next frame's
     * drain/resize/present picks up the new size. */
    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_WINDOW_SYNC)
    {
        /* "Interface scaling" (device option 27) on a RESIZABLE lane shrinks the
         * canvas the window is letterboxed from, so it is a canvas change and
         * nothing else — no window call, and no bus round trip, because the
         * click that caused it is already in the recorded stream and the canvas
         * is a pure function of it and the window size. The surface reconcile
         * at the top of the next frame picks up the new backbuffer size.
         *
         * A FIXED lane cannot shrink its canvas, so the branch below grows the
         * window instead. */
        /* The display the window is on, before the scale is synced against it:
         * a window dragged to a monitor of another density re-lays out in the
         * same frame. */
        App_SetDisplayDensity(&app, PlatformWindow_DisplayDensityPercent(platform));
        App_SyncClientScale(&app);

        /* The inset sync runs first and unconditionally: a `&&`/`||` that
         * short-circuited past it would stop the strip from ever measuring. */
        int const fixed_inset_changed =
            App_WindowMode(&app) == CS2VM_WINDOW_MODE_FIXED ? App_SyncFixedChromeInset(&app) : 0;
        if( App_WindowMode(&app) == CS2VM_WINDOW_MODE_FIXED &&
            (fixed_inset_changed || app.host.client_scale_dirty) )
        {
            int const window_w = App_FixedWindowWidth(&app);
            int const window_h = App_FixedWindowHeight(&app);
            app.host.client_scale_dirty = false;
            /*
             * Clearing the follow gate IS the snap: with follow=false the
             * platform states the floor and sizes the window to it, in that
             * order, and it is the one path that caps both at the display.
             * The frame times the interface scale is not a size any display
             * has to hold -- at 300% it is 2295x1509 points, and the window
             * that came back from a screen change was larger than the desk,
             * with a minimum size to match. @see platform_window.h.
             *
             * It also has to run BEFORE anything else resizes the window:
             * leaving resizable is where the platform records the size to
             * hand back on the way in again (`resizable_w`), and it records
             * what SDL reports at that moment. A snap first meant it read the
             * already-snapped 765x503, remembered nothing, and a later return
             * to resizable restored the fixed frame's size instead of the
             * window the user had. Visible the moment a lane pins fixed
             * before a resizable plugin frame commits: the Modern Resizable
             * layout came up in a 765x503 window on a 1200x800 desktop.
             */
            PlatformWindow_SetCanvasFollowsWindow(platform, &bus, false, window_w, window_h);
            if( getenv("TORIRS_RESIZE_DEBUG") )
                TORIRS_REPORT(
                    "fixed-chrome: canvas %dx%d window %dx%d (scale %d%%)\n",
                    UITREE_LAYOUT_ROOT_W,
                    UITREE_LAYOUT_ROOT_H,
                    window_w,
                    window_h,
                    RS_CS2Host_UiScalePercent(&app.host));
        }
        /*
         * Resizable mode: the SAME strip, carved from a canvas nobody grew.
         *
         * Both resizable toplevels dock interface 728's rail inside the canvas
         * and lay themselves out beside it, so a window at the 765x503 minimum
         * gave the frame 723 columns -- below the floor the whole frame is
         * authored to, and the chatbox ran under the sidebar. The floor is the
         * frame's, so the WINDOW has to carry the strip on top of it at 100%;
         * when it is too small for that, ask the window for it, exactly as the
         * fixed branch does. A window that refuses (maximised, or no room on
         * the display) lays the frame out in what it has.
         *
         * The floor is the frame at the CHOSEN interface scale, and it is the
         * window's minimum as well as a growth: at 100% only, a 200% scale on
         * a Retina display laid the login screen out in a quarter of itself.
         * The platform caps it at the display; what that leaves short, the
         * layout lowers the scale for (platform/client_scale.h rule 5).
         *
         * The window is asked in points: on a 2x display the two differ by the
         * density, and asking for pixels there would double a window that only
         * needed 42 more columns.
         */
        else
        {
            int floor_w = 0;
            int floor_h = 0;
            if( App_ResizableWindowFloor(&app, &floor_w, &floor_h) )
            {
                int const density = PlatformWindow_DisplayDensityPercent(platform);
                assert(density > 0);
                PlatformWindow_SetGameAreaFloor(
                    platform,
                    (int)(((long long)floor_w * 100 + density - 1) / density),
                    (int)(((long long)floor_h * 100 + density - 1) / density));
            }
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
            int keyboard_on = 0;
            if( App_TakeTextInputChange(&app, &keyboard_on) )
                PlatformWindow_SetTextInput(platform, keyboard_on);
        }
        {
            char url[512];
            if( App_TakeOpenUrl(&app, url, (int)sizeof(url)) )
                PlatformWindow_OpenUrl(platform, url);
        }
        {
            int new_mode = 0;
            if( App_TakeWindowModeChange(&app, &new_mode) )
            {
                bool const resizable = new_mode == CS2VM_WINDOW_MODE_RESIZABLE;
                TORIRS_LOG("windowmode: %s\n", resizable ? "resizable" : "fixed");
                /* Resizable reads these as the window's floor only; fixed
                 * snaps the window to them, so they carry the scale. */
                PlatformWindow_SetCanvasFollowsWindow(
                    platform,
                    &bus,
                    resizable,
                    resizable ? APP_CANVAS_MIN_W : App_FixedWindowWidth(&app),
                    resizable ? APP_CANVAS_MIN_H : App_FixedWindowHeight(&app));
                if( !resizable )
                    CmdBus_PushWindowResize(&bus, APP_CANVAS_MIN_W, APP_CANVAS_MIN_H);
                /* Strip inset is applied next frame once layout has measured it. */
            }
            {
                int layout_mode = 0;
                if( App_TakeClientLayoutChange(&app, &layout_mode) )
                {
                    TORIRS_LOG("client_layout: mode=%d\n", layout_mode);
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
            audio, audio_commands, App_DrainAudio(&app, audio_commands, TORIRS_AUDIO_QUEUE_MAX));
        PlatformAudio_Update(audio);
        PlatformAudio_Feedback(audio, &audio_feedback);
        App_SetAudioFeedback(&app, &audio_feedback);
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
    App_NoteFrameTime(&app, PlatformWindow_TicksUs() - frame_start_us);
#if defined(TORIRS_FRAME_TIMES)
    ToriRS_FrameTimes_End(PlatformWindow_TicksUs());
#endif

    /*
     * TORIRS_FPS_REPORT=1: frames per second, every two seconds.
     *
     * Not a nicety. A CPU percentage is only comparable between two clients
     * that are drawing at the same rate, and that was assumed rather than
     * checked for a long time -- the Java client turned out to be rendering 31
     * fps against our 50, which invalidated every "% of one core" comparison
     * made against it. It also decides whether an ablation is readable at all:
     * a client that is missing its frame cap absorbs a deleted phase as frame
     * time instead of as CPU, and the arm then shows no saving at all.
     *
     * So every measured arm should be able to state its own frame rate.
     */
    {
        static int report = -1;
        static uint64_t win_start_ms;
        static int win_frames;

        if( report < 0 )
            report = getenv("TORIRS_FPS_REPORT") ? 1 : 0;
        if( report )
        {
            uint64_t now_ms = PlatformWindow_Ticks64();
            if( win_start_ms == 0 )
                win_start_ms = now_ms;
            /*
             * Frames PRESENTED, not iterations run.
             *
             * These were the same number until the loop learned to skip a
             * draw -- once while the async pipeline is draining, and again
             * when the pacer steps the draw budget down on a machine that
             * cannot hold the rate. Counting iterations after that reports a
             * frame rate nobody is looking at: the loop can spin at 49/s
             * while the screen updates 30 times, and the fps line would say
             * 49.
             *
             * That matters more than a cosmetic mislabel, because this number
             * is what says whether two clients are comparable at all -- the
             * whole reason it exists (see above). A draw rate inflated by
             * skipped presents would put the comparison back exactly where
             * that comment says it was.
             */
            if( app_redraw )
                win_frames++;
            if( now_ms - win_start_ms >= 2000 )
            {
                TORIRS_REPORT(
                    "[fps] %.1f\n", (double)win_frames * 1000.0 / (double)(now_ms - win_start_ms));
                win_start_ms = now_ms;
                win_frames = 0;
            }
        }
    }
    /* The browser paces us: emscripten_set_main_loop is backed by
     * requestAnimationFrame, and a blocking sleep here would stall the page's
     * whole main thread rather than yield it. */
#if !defined(__EMSCRIPTEN__)
    /* The frame cap. The selected pacer owns where the wait ends: the deadline
     * pacer returns an absolute deadline, so an early wakeup is retried and the
     * frame's complete workload counts against its 20 ms budget; the GameShell
     * pacer returns now-plus-`del`, which is a duration and cannot recover the
     * time an overrun cost. --uncapped performs no artificial wait at all. */
    /*
     * The cap paces the screen, not the pipeline.
     *
     * App_RunOnce drains a bounded number of async steps per frame (32 once
     * past boot), so sleeping out the rest of the frame while work is still
     * queued caps the pipeline at budget-times-framerate -- which on a cold
     * boot is the client's whole world download, and is why an uncapped run
     * reached the world visibly sooner than a capped one on the same machine.
     *
     * There is no busy-wait here: the loop goes straight back into
     * App_RunOnce, which does real work. When the queue drains, async_pending
     * clears and the ordinary wait resumes on the very next frame.
     */
    ContentTest_End(&app, sock);
    if( !replay && !uncapped && !ContentTest_Enabled() && !App_AsyncPending(&app) )
    {
        uint64_t pace_begin_us = PlatformWindow_TicksUs();
        uint64_t wait_until_ms =
            ToriRS_Pacer_WaitDeadline(&frame_pacer, frame_start_ms, PlatformWindow_Ticks64());

        if( pace_spin )
        {
            /* TORIRS_PACE_SPIN=1 holds the core busy across the wait instead of
             * sleeping. Diagnostic only — it pins a core at 100% — but it is the
             * only way to separate the cap's own cost from the cost of resuming
             * a CPU that Windows parked during the sleep. */
            while( PlatformWindow_Ticks64() < wait_until_ms )
                ;
        }
        else
            PlatformWindow_SleepUntil(wait_until_ms);

        {
            uint64_t const pace_end_us = PlatformWindow_TicksUs();
            TORIRS_PERF_CARRY(TORIRS_PERF_STAGE_PACE, (pace_end_us - pace_begin_us) * 1000u);
            ToriRS_Pacer_NoteFrame(&frame_pacer, pace_end_us, pace_end_us - pace_begin_us);
        }
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
            /*
             * These frames CONTINUE the loop's clock; they do not restart it.
             *
             * `now_ms` is the clock the session is judged against, and it is
             * the one App_RunOnce hands NetLinkWatch_Step. The four frames
             * used to be stamped 20/40/60/80 -- absolute, from zero -- so
             * after a run of any length the clock jumped BACKWARDS by the
             * whole session, `now_ms - last_recv_ms` wrapped unsigned, and
             * the watch read the wrap as fifteen silent seconds. It then tore
             * the session down: "Connection lost / Please wait - attempting
             * to reestablish" across the viewport, and with it the local
             * player, every npc and every minimap dot. Four frames of parked
             * pointer are not supposed to cost a session, and every shot that
             * asked for a hover paid for one -- which is what left the tile
             * indicator's own headline photograph with no player to mark.
             */
            uint64_t const hov_base_ms = app.last_frame_ms;
            for( int t = 0; t < 4; t++ )
            {
                uint64_t const hov_ms = hov_base_ms + (uint64_t)(t + 1) * 20;
                LibToriRS_Input_Begin(hov_input, hov_ms);
                LibToriRS_Input_PushMouseMove(hov_input, hov_x, hov_y);
                LibToriRS_Input_End(hov_input);
                (void)App_RunOnce(&app, hov_ms, hov_input);
                /*
                 * Unconditionally, and not on App_RunOnce's redraw answer:
                 * the world PICK is armed inside App_Render, so the parked
                 * pointer only reaches the pickset and world_hover_tile_x/z
                 * by rendering. Without this the pointer moved and nothing
                 * re-picked, and hover-tile consumers kept answering the tile
                 * the last main-loop event left -- on the CS1 lane that was
                 * the login click, a tile the ~varrock teleport had since put
                 * under a roof. A frame that reports no redraw has still
                 * moved the pointer, so the redraw flag is the wrong question
                 * for these four.
                 */

                /*
                 * Why rendering is the fix and not a nicety: App_RunOnce only
                 * latches the mouse point. The scene pick runs inside
                 * App_Render -- a hittest as each visible model projects -- and
                 * app_world_pick_finish writes world_hover_tile from it, so the
                 * hover tile any overlay reads is the LAST RENDER'S. Four
                 * logic-only frames moved the pointer and left the pick where
                 * the main loop's final frame had put it: on CS2 the mouse had
                 * never been in the viewport, so hover_tile() answered nothing
                 * and the indicator drew no marker; on the live CS1 lane the
                 * pointer had last been at the login click, so the marker sat
                 * on a roof a hundred pixels from where the drive asked. Both
                 * pictures read as a plugin that draws in the wrong place
                 * rather than a drive that never delivered the hover.
                 */
                sim_render_frame(&app);
            }
            TORIRS_LOG(
                "sim_hover: parked at %d,%d hover_com_id=%d\n", hov_x, hov_y, app.hover_com_id);
        }
        /* Post-mount snapshot: unlike the boot-time TORIRS_DUMP_TREE (which
         * runs before any server IF_OPENSUB lands), this dumps after the
         * frame loop so server-driven interface mounts are visible. */
        if( getenv("TORIRS_TRACE_NATIVE_UI") && app.plugins )
            for( int i = 0; i < PluginHost_Count(app.plugins); ++i )
                TORIRS_REPORT(
                    "PLUGIN_STATE id=%s enabled=%d running=%d error=%d\n",
                    PluginHost_Name(app.plugins, i),
                    PluginHost_IsEnabled(app.plugins, i),
                    PluginHost_IsRunning(app.plugins, i),
                    PluginHost_Error(app.plugins, i) != NULL);
        /* The local player's whole tiles and the plugin world-object counts,
         * for the tile-marker and loot-beam rules: a marker or a beam is
         * judged against what the engine holds, not only against its pixels. */
        if( getenv("TORIRS_TRACE_NATIVE_UI") )
        {
            int true_x, true_z, level, dest_x, dest_z, flag_x, flag_z, draw_x, draw_z;
            int in_use, active, built;
            if( App_LocalPlayerTiles(
                    &app,
                    &true_x,
                    &true_z,
                    &level,
                    &dest_x,
                    &dest_z,
                    &flag_x,
                    &flag_z,
                    &draw_x,
                    &draw_z) )
                TORIRS_REPORT(
                    "NATIVE_PLAYER true=%d,%d,%d dest=%d,%d flag=%d,%d draw=%d,%d\n",
                    true_x,
                    true_z,
                    level,
                    dest_x,
                    dest_z,
                    flag_x,
                    flag_z,
                    draw_x,
                    draw_z);
            App_PluginObjectCounts(&app, &in_use, &active, &built);
            TORIRS_REPORT(
                "PLUGIN_SCENE_OBJECTS in_use=%d active=%d built=%d\n", in_use, active, built);
            App_TraceWorldEntities(&app);
        }
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
            if( getenv("TORIRS_TRACE_NATIVE_UI") )
                TORIRS_REPORT("NATIVE_ROOT id=%d\n", app.boot_interface_id);
            char const* filter = getenv("TORIRS_DUMP_BOUNDS");
            int want = strcmp(filter, "all") == 0 ? -1 : (int)strtol(filter, NULL, 0);
            for( uint32_t i = 0; i < app.tree->component_count; i++ )
            {
                struct UITreeComponent const* c = &app.tree->components[i];
                if( c->freed || (want >= 0 && ((c->component_id >> 16) & 0xFFFF) != want) )
                    continue;
                /* TORIRS_REPORT, not TORIRS_LOG: the lever is the env var,
                 * and the optimised build is the one the geometry question is
                 * asked of. */
                TORIRS_REPORT(
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
                    c->type == UIELEM_RS_GRAPHIC ? UITreeSceneBridge_SpriteCacheIdForScene(
                                                       &app.bridge, c->u.rs_graphic.scene_id)
                                                 : -1,
                    dump_node_hidden(app.tree, (int32_t)i),
                    c->position.abs_x,
                    c->position.abs_y,
                    c->position.abs_w,
                    c->position.abs_h,
                    c->position.width,
                    c->position.height,
                    (int)c->position.width_mode,
                    (int)c->position.height_mode,
                    (int)c->position.x_mode,
                    (int)c->position.y_mode,
                    c->type == UIELEM_RS_LAYER ? c->u.rs_layer.scroll_width : -1,
                    c->type == UIELEM_RS_LAYER ? c->u.rs_layer.scroll_height : -1,
                    c->scroll_x,
                    c->scroll_y);
                /* The plugin anchor this node carries, so a capture rule can
                 * assert "the camera REPLACES the report button" instead of
                 * inferring it from paint bits. 0:-1 when none. */
                int32_t anchor_target = -1;
                enum UITreeWidgetRelation const anchor_relation =
                    UITree_WidgetAnchorAt(app.tree, (int32_t)i, &anchor_target);
                if( getenv("TORIRS_TRACE_NATIVE_UI") )
                    TORIRS_REPORT(
                        "NATIVE_UI node=%u incarnation=%" PRIu64 " parent=%d com=%d "
                        "type=%s hidden=%d native_paint=%d native_input=%d native_hide=%u slot=%u "
                        "member=%u role=%u "
                        "box=%d,%d,%d,%d cs1_scripts=%d active=%d anchor=%d:%d plugin_hidden=%d\n",
                        i,
                        c->incarnation,
                        c->parent,
                        c->component_id,
                        UITree_ComponentTypeStr(c->type),
                        dump_node_hidden(app.tree, (int32_t)i),
                        UITree_NodeNativeVisible(
                            app.tree, &app.ui_host, (int32_t)i, app.hover_com_id),
                        UITree_NodeNativeInputPresent(app.tree, &app.ui_host, (int32_t)i),
                        c->native_hide,
                        c->slot_tag,
                        c->frame_member_plus1,
                        c->role_id,
                        c->position.abs_x,
                        c->position.abs_y,
                        c->position.abs_w,
                        c->position.abs_h,
                        c->behavior.scripts_count,
                        c->cs1_active,
                        (int)anchor_relation,
                        anchor_target,
                        (int)c->widget_hidden);
                /* Every widget a plugin OWNS, by the key it created it under:
                 * a text's length and hash, so a caption's content can be
                 * checked; an image's scene id, so its draw command can be
                 * found in the EMIT_EXIT list; and whether it is hidden, so a
                 * plate the plugin put away is not read as one it drew. */
                if( getenv("TORIRS_TRACE_NATIVE_UI") && c->plugin_owner &&
                    c->type == UIELEM_RS_TEXT )
                {
                    char const* text = c->u.rs_text.text ? c->u.rs_text.text : "";
                    uint64_t hash = UITree_NodeTextHash(app.tree, (int32_t)i);
                    TORIRS_REPORT(
                        "OWNED_WIDGET owner=%" PRIu64
                        " key=%s node=%u box=%d,%d,%d,%d len=%zu hash=%016" PRIx64 " hidden=%d\n",
                        c->plugin_owner,
                        c->plugin_key ? c->plugin_key : "",
                        i,
                        c->position.abs_x,
                        c->position.abs_y,
                        c->position.abs_w,
                        c->position.abs_h,
                        strlen(text),
                        hash,
                        dump_node_hidden(app.tree, (int32_t)i));
                }
                else if( getenv("TORIRS_TRACE_NATIVE_UI") && c->plugin_owner )
                {
                    TORIRS_REPORT(
                        "OWNED_WIDGET owner=%" PRIu64
                        " key=%s node=%u box=%d,%d,%d,%d type=%s scene=%d hidden=%d\n",
                        c->plugin_owner,
                        c->plugin_key ? c->plugin_key : "",
                        i,
                        c->position.abs_x,
                        c->position.abs_y,
                        c->position.abs_w,
                        c->position.abs_h,
                        UITree_ComponentTypeStr(c->type),
                        c->type == UIELEM_RS_GRAPHIC ? c->u.rs_graphic.scene_id : -1,
                        dump_node_hidden(app.tree, (int32_t)i));
                }
                if( getenv("TORIRS_TRACE_NATIVE_UI") && c->type == UIELEM_RS_TEXT &&
                    c->u.rs_text.input )
                {
                    char const* text = c->u.rs_text.text ? c->u.rs_text.text : "";
                    uint64_t hash = UITree_NodeTextHash(app.tree, (int32_t)i);
                    TORIRS_REPORT(
                        "NATIVE_INPUT parent=%d com=%d focused=%d len=%zu hash=%016" PRIx64 "\n",
                        c->parent >= 0 ? app.tree->components[c->parent].component_id : -1,
                        c->component_id,
                        UITree_InputFocusId(app.tree) == c->component_id,
                        strlen(text),
                        hash);
                }
            }
            /* Every semantic role that resolves, and the box of the node it
             * resolves to: the lane's own surface a plugin dressed by role
             * (the chat backing, the chat bar) is found the way the plugin
             * found it, not by a cache id that differs per toplevel. */
            for( int ri = 0; getenv("TORIRS_TRACE_NATIVE_UI") && ri < app.ui_roles.count; ri++ )
            {
                int32_t const node = UITree_RoleNode(app.tree, &app.ui_roles, (uint16_t)(ri + 1));
                struct UITreeComponent const* c;
                if( node < 0 )
                    continue;
                c = &app.tree->components[node];
                TORIRS_REPORT(
                    "ROLE_WIDGET role=%s node=%d com=0x%08x box=%d,%d,%d,%d hidden=%d\n",
                    app.ui_roles.entries[ri].name,
                    (int)node,
                    (unsigned)c->component_id,
                    c->position.abs_x,
                    c->position.abs_y,
                    c->position.abs_w,
                    c->position.abs_h,
                    dump_node_hidden(app.tree, node));
            }
        }

        if( getenv("TORIRS_TRACE_NATIVE_UI") )
            TORIRS_REPORT(
                "NATIVE_CHAT_MODES public=%d private=%d trade=%d\n",
                app.slots.chat_filter_mode[RS_UI_CHAT_FILTER_PUBLIC],
                app.slots.chat_filter_mode[RS_UI_CHAT_FILTER_PRIVATE],
                app.slots.chat_filter_mode[RS_UI_CHAT_FILTER_TRADE]);

        /* The cache's highlight groups as the engine recorded them: one line
         * per live group (colour set and flags non-zero) with the members the
         * scripts named for it. Independent of what any plugin then drew. */
        if( getenv("TORIRS_TRACE_NATIVE_UI") )
        {
            struct RS_HighlightState const* hl = &app.host.highlight;
            for( int kind = 0; kind < RS_HIGHLIGHT_KIND_COUNT; kind++ )
                for( int group = 0; group < RS_HIGHLIGHT_GROUP_MAX; group++ )
                {
                    struct RS_HighlightStyle const* style = &hl->style[kind][group];
                    int members = 0;
                    if( style->colour < 0 || style->flags == 0 )
                        continue;
                    for( int i = 0; i < hl->member_count[kind]; i++ )
                        members += hl->member[kind][i].group == group;
                    for( int i = 0; i < hl->named_count; i++ )
                        members += hl->named[i].kind == kind && hl->named[i].group == group;
                    TORIRS_REPORT(
                        "NATIVE_HIGHLIGHT kind=%d group=%d colour=%06x outline=%d opacity=%d "
                        "flags=%d members=%d\n",
                        kind,
                        group,
                        style->colour & 0xffffff,
                        style->outline_width,
                        style->opacity,
                        style->flags,
                        members);
                }
        }

        /* The plugin window's active page as the host holds it: which plugin
         * and face is up, and every model widget with its current selection.
         * Independent of the chrome executor that painted it. Beside it, the
         * two device options the Client Settings page writes, read from the
         * CS2 host's option table rather than from the page. */
        if( getenv("TORIRS_TRACE_NATIVE_UI") && app.plugins )
        {
            int const active = PluginHost_PanelActive(app.plugins);
            uint32_t const generation = PluginHost_PanelSelectionGeneration(app.plugins);
            int const count =
                active >= 0 ? PluginHost_PanelWidgetCount(app.plugins, generation) : 0;
            TORIRS_REPORT(
                "PLUGIN_PANEL visible=%d plugin=%s view=%d generation=%u widgets=%d\n",
                app.plugin_panel_visible,
                active >= 0 ? PluginHost_Name(app.plugins, active) : "-",
                PluginHost_PanelView(app.plugins),
                generation,
                count);
            for( int i = 0; i < count; i++ )
            {
                struct ToriRS_PanelWidget const* w =
                    PluginHost_PanelWidgetAt(app.plugins, generation, i);
                if( !w )
                    continue;
                TORIRS_REPORT(
                    "PLUGIN_PANEL_WIDGET index=%d kind=%d id=%s label=%s selected=%d value=%s "
                    "text=%s\n",
                    i,
                    w->kind,
                    w->id,
                    w->label,
                    w->selected,
                    w->structured_select ? w->selected_value : "",
                    w->text);
            }
            /* Custom rows draw through the plugin's own on_ui_draw into a
             * region the executor allotted; the region is the only native
             * record of where that painting landed. */
            for( int i = 0; i < app.plugin_panel_row_count; i++ )
            {
                struct AppPluginPanelRow const* row = &app.plugin_panel_rows[i];
                if( row->widget_kind != TORIRS_PANEL_WIDGET_CUSTOM || !row->custom_layout_valid )
                    continue;
                TORIRS_REPORT(
                    "PLUGIN_PANEL_CUSTOM id=%s region=%d,%d,%d,%d\n",
                    row->widget_id,
                    row->custom_region.x,
                    row->custom_region.y,
                    row->custom_region.w,
                    row->custom_region.h);
            }
            /* The client's own stat table, as the server has stated it: a
             * skill with last_seen 0 has no reading yet (the plugin API says
             * so too), which is what separates the login burst from a gain. */
            for( int i = 0; i < RS_PLAYER_STATS_SKILL_COUNT; i++ )
                TORIRS_REPORT(
                    "NATIVE_SKILL index=%d base=%d current=%d xp=%d stated=%d\n",
                    i,
                    app.stats.base_level[i],
                    app.stats.current_level[i],
                    app.stats.xp[i],
                    app.stats.last_seen_level[i]);
            TORIRS_REPORT(
                "NATIVE_DEVICE_OPTION ui_scale=%d ui_scale_mode=%d\n",
                RS_CS2Host_GetOption(&app.host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_UI_SCALE),
                RS_CS2Host_GetOption(
                    &app.host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_UI_SCALE_MODE));
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
                /* The model pose rides at the END of the line: the parity
                 * parser (cs2dom's emit_parity.js) matches an unanchored
                 * prefix, so trailing fields are additive. A pixel diff on a
                 * model widget is unexplainable without the angles. */
                TORIRS_REPORT(
                    "EMIT_EXIT[%d] kind=%d com=0x%08x (%d|%d) x=%d y=%d w=%d h=%d scene=%d "
                    "model=%d "
                    "color=0x%06x filled=%d trans=%d tiled=%d clip=%d,%d %dx%d "
                    "mzoom=%d mxan=%d myan=%d mzan=%d mox=%d moy=%d\n",
                    i,
                    (int)d->kind,
                    d->component_id,
                    group,
                    d->component_id & 0xFFFF,
                    d->x,
                    d->y,
                    d->w,
                    d->h,
                    d->scene_id,
                    d->model_id,
                    d->color,
                    d->filled,
                    d->trans,
                    d->tiled,
                    d->clip.x,
                    d->clip.y,
                    d->clip.w,
                    d->clip.h,
                    d->model_zoom,
                    d->model_xan,
                    d->model_yan,
                    d->model_zan,
                    d->model_x_offset,
                    d->model_y_offset);
            }
        }
        if( torirs_env_net_debug() && app.tree )
        {
            for( int t = 0; t < 14; t++ )
                TORIRS_LOG(
                    "exit: tab %d overlay=%d owner=%d\n",
                    t,
                    app.slots.side_overlay_id[t],
                    app.slots.side_owner_index[t]);
            for( uint32_t i = 0; i < app.tree->component_count; i++ )
            {
                struct UITreeComponent const* c = &app.tree->components[i];
                if( c->type == UIELEM_BUILTIN_TAB_ICONS )
                    TORIRS_LOG(
                        "exit: tab_icon idx=%u tab=%d freed=%d hide=%d scene=%d x=%d y=%d\n",
                        i,
                        c->u.tab_icon.tabno,
                        (int)c->freed,
                        (int)c->behavior.hide,
                        c->u.tab_icon.scene_id,
                        c->position.abs_x,
                        c->position.abs_y);
            }
            for( int t = 0; t < 14; t++ )
                TORIRS_LOG(
                    "exit: tabgate %d enabled=%d flash_hidden=%d flash_tab=%d\n",
                    t,
                    RS_UISlots_TabEnabled(&app.slots, t),
                    RS_UISlots_TabFlashHidden(&app.slots, t, app.logic_cycle),
                    app.slots.flash_tab);
            for( int f = 0; f < 6; f++ )
                TORIRS_LOG(
                    "exit: scene_font %d has=%d\n", f, (int)ToriDraw_SceneFontHas(app.scene, f));
            TORIRS_LOG("exit: hover_com_id=%d\n", app.hover_com_id);
            TORIRS_LOG(
                "exit: minimap_view valid=%d box=%d,%d %dx%d yaw=%d\n",
                (int)app.minimap.valid,
                app.minimap.x,
                app.minimap.y,
                app.minimap.w,
                app.minimap.h,
                MinimapView_Yaw(&app.minimap));
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
                    TORIRS_LOG(
                        "exit: com=%d idx=%u type=%d freed=%d hide=%d text='%s' "
                        "abs=%d,%d wh=%dx%d font=%d color=0x%x "
                        "textalign=%d,%d lineheight=%d parent=%d\n",
                        want,
                        i,
                        (int)c->type,
                        (int)c->freed,
                        (int)c->behavior.hide,
                        c->type == UIELEM_RS_TEXT && c->u.rs_text.text ? c->u.rs_text.text : "",
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
        if( getenv("TORIRS_TRACE_NATIVE_UI") && app.tree )
            for( int i = 0; i < RS_OVERLAY_MAX; ++i )
            {
                struct RS_Overlay const* overlay = RS_OverlayGet(&app.host.overlay, i);
                if( !overlay || overlay->anchor != RS_OVERLAY_ANCHOR_STATIC ||
                    overlay->static_type != RS_OVERLAY_TYPE_COORD || overlay->slot != 0 )
                    continue;
                int root = UITree_FindByComponentId(app.tree, overlay->component_id);
                if( root < 0 )
                    continue;
                int emitted = 0;
                for( int j = 0; j < app.emit.count; ++j )
                {
                    int node = app.emit.cmds[j].node_index, guard = 0;
                    while( node >= 0 && (uint32_t)node < app.tree->component_count &&
                           guard++ < (int)app.tree->component_count )
                    {
                        if( node == root )
                        {
                            ++emitted;
                            break;
                        }
                        node = app.tree->components[node].parent;
                    }
                }
                struct UITreeComponent const* c = &app.tree->components[root];
                int captions = 0, hidden_captions = 0, caption_emits = 0, buttons = 0,
                    live_buttons = 0;
                for( int child = c->first_child; child >= 0;
                     child = app.tree->components[child].next_sibling )
                {
                    struct UITreeComponent const* item = &app.tree->components[child];
                    if( item->type == UIELEM_RS_TEXT && item->position.width_mode == 1 &&
                        item->position.width == 108 )
                    {
                        ++captions;
                        hidden_captions += item->widget_hidden != 0;
                        int painted = 0;
                        for( int j = 0; j < app.emit.count; ++j )
                            if( app.emit.cmds[j].node_index == child )
                            {
                                ++caption_emits;
                                ++painted;
                            }
                        char const* text = item->u.rs_text.text ? item->u.rs_text.text : "";
                        uint64_t hash = UINT64_C(14695981039346656037);
                        for( unsigned char const* p = (unsigned char const*)text; *p; ++p )
                            hash = (hash ^ *p) * UINT64_C(1099511628211);
                        TORIRS_REPORT(
                            "NATIVE_GROUND_CAPTION root=%d node=%d painted=%d box=%d,%d,%d,%d "
                            "color=%06x len=%zu hash=%016" PRIx64 "\n",
                            root,
                            child,
                            painted,
                            item->position.abs_x,
                            item->position.abs_y,
                            item->position.abs_w,
                            item->position.abs_h,
                            item->u.rs_text.color & 0xffffffu,
                            strlen(text),
                            hash);
                    }
                    bool has_op = false;
                    if( item->menu_options )
                        for( int op = 0; op < UITREE_MENU_OPTION_SLOTS; ++op )
                            has_op |= item->menu_options->ops[op][0] != 0;
                    if( has_op )
                    {
                        ++buttons;
                        bool live = UITree_NodeNativeInputPresent(app.tree, &app.ui_host, child);
                        live_buttons += live;
                        TORIRS_REPORT(
                            "NATIVE_GROUND_CONTROL root=%d node=%d live=%d box=%d,%d,%d,%d\n",
                            root,
                            child,
                            live,
                            item->position.abs_x,
                            item->position.abs_y,
                            item->position.abs_w,
                            item->position.abs_h);
                    }
                }
                TORIRS_REPORT(
                    "NATIVE_GROUND_OVERLAY root=%d widget_hide=%d native_hide=%d emitted=%d "
                    "coord=%d captions=%d hidden_captions=%d caption_emits=%d buttons=%d "
                    "live_buttons=%d\n",
                    root,
                    c->widget_hidden,
                    c->native_hide,
                    emitted,
                    overlay->coord,
                    captions,
                    hidden_captions,
                    caption_emits,
                    buttons,
                    live_buttons);
            }
        if( getenv("TORIRS_TRACE_NATIVE_UI") )
            for( int i = 0; i < app.overlays.world_count; ++i )
            {
                struct UITreeEntityOverlay const* entry = &app.overlays.world[i];
                if( entry->kind != UITREE_ENTITY_OVERLAY_TEXT )
                    continue;
                uint64_t hash = UINT64_C(14695981039346656037);
                for( unsigned char const* p = (unsigned char const*)entry->text; *p; ++p )
                    hash = (hash ^ *p) * UINT64_C(1099511628211);
                TORIRS_REPORT(
                    "OVERLAY_TEXT x=%d y=%d color=%06x len=%zu hash=%016" PRIx64 "\n",
                    entry->x,
                    entry->y,
                    entry->color & 0xffffffu,
                    strlen(entry->text),
                    hash);
            }
        /*
         * WHO OWNS THE CHAT REGION IN THE PICTURE THAT WAS JUST TAKEN.
         *
         * Unconditional, and beside the BMP write rather than behind a trace
         * flag, because it answers a question every capture of a chat-speaking
         * plugin depends on and no capture could answer: the message log --
         * and therefore every `Porcelain_Notify` line, every `mes`, every
         * game message a plugin can produce -- is SUPPRESSED whenever an
         * interface is mounted in the chat region. Not covered: not drawn at
         * all. @see RS_UISlots_ChatRegionIface, and drawChat's `if
         * (chatInterfaceId !== -1) ... else if (tutComId !== -1)` that it
         * mirrors.
         *
         * Without this line a plugin that said nothing and a plugin whose line
         * had nowhere to go are the SAME PICTURE, and that is not a theory:
         * thirty-seven of the forty-seven live captures taken on this
         * lane's own 2004 frame were photographed with a server modal owning
         * the region, and nothing in the harness could say so -- the state was
         * found by a person cropping one of them by hand. A log line costs
         * nothing and every capture already keeps its log.
         *
         * `iface` is what the region shows: the IF_OPENCHAT dialogue if one is
         * mounted, otherwise the tutorial-progress component, otherwise -1 for
         * the message log itself. The two ids are printed alongside so the
         * reader knows WHICH of the two owns it without a table.
         */
        TORIRS_REPORT(
            "CHAT_REGION iface=%d chat_com=%d tut_com=%d log_visible=%d\n",
            RS_UISlots_ChatRegionIface(&app.slots),
            app.slots.chat_com_id,
            app.slots.tut_com_id,
            RS_UISlots_ChatRegionIface(&app.slots) == -1);
        bmp_write_file(
            getenv("TORIRS_EXIT_BMP"), pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        TORIRS_LOG("wrote %s\n", getenv("TORIRS_EXIT_BMP"));
        free(pixels);
    }

    if( replay )
    {
        fclose(replay);
        /* TORIRS_REPLAY_BMP=path: dump the final replayed frame for golden
         * comparison against the recorded session. */
        if( getenv("TORIRS_REPLAY_BMP") )
        {
            int* pixels = calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
            assert(pixels);
            App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            bmp_write_file(
                getenv("TORIRS_REPLAY_BMP"), pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            TORIRS_LOG("wrote %s\n", getenv("TORIRS_REPLAY_BMP"));
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
        TORIRS_ERR(
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
        TORIRS_LOG(
            "audio: bus gains effects/music/area %d/%d/%d\n",
            stats.bus_volume[TORIRS_AUDIO_BUS_EFFECTS],
            stats.bus_volume[TORIRS_AUDIO_BUS_MUSIC],
            stats.bus_volume[TORIRS_AUDIO_BUS_AREA]);
        /* "callbacks" is wrong on a lane whose schedule is fed from the frame
         * loop, where the count is Updates. The device period is the tell:
         * only a callback lane has one. */
        TORIRS_LOG(
            "audio: %d %s, %d underruns, period %.2f ms, "
            "interval %.2f/%.2f/%.2f ms, jitter peak %.2f ms, render peak %.2f ms\n",
            stats.updates,
            stats.callback_period_ms > 0.0 ? "callbacks" : "updates",
            stats.underruns,
            stats.callback_period_ms,
            stats.update_interval_min_ms,
            stats.update_interval_mean_ms,
            stats.update_interval_max_ms,
            stats.callback_jitter_max_ms,
            stats.render_max_ms);
        TORIRS_LOG(
            "audio: %s %d/%.1f/%d frames (now %d), capture dropped %d frames\n",
            stats.callback_period_ms > 0.0 ? "stream ring" : "schedule depth",
            stats.queue_min_frames,
            stats.queue_mean_frames,
            stats.queue_max_frames,
            stats.queue_current_frames,
            stats.capture_dropped_frames);
    }
    PlatformAudio_Free(audio);
    renderer_stop();
    PlatformWindow_Free(platform);
}

#if defined(__EMSCRIPTEN__)
/*
 * May the platform step the runner between frames?
 *
 * Set the moment the frame loop is handed to the browser and cleared when it
 * ends, so a pump can never run against an App that is still being built or
 * has been torn down. Everything before the loop is main() running to
 * completion on one JavaScript task, which nothing can interleave with.
 */
static int web_pump_armed;

/*
 * The executor's landed hook: reads have been answered, step whatever they
 * unblocked NOW rather than on the next animation frame.
 *
 * Called from platform_web_io.js (TORIRS_WEB_IO.pumpNow) on a MessageChannel
 * task it posts after answering a batch -- a macrotask, so it runs after
 * every promise continuation of the turn that delivered the bytes and never
 * from inside a C call. A 4 ms setTimeout clamp, which is what the boot's
 * settimeout(0) pacing degrades to once nested, is exactly the latency this
 * exists to remove: a task chain that asks for one group at a time paid it
 * per link.
 *
 * Returns 1 when it stepped anything, 0 when it was not armed.
 */
EMSCRIPTEN_KEEPALIVE int
torirs_web_io_pump(void)
{
    if( !web_pump_armed )
        return 0;
    App_PumpAsync(&app, 0);
    return 1;
}

/* The boot marks and both runners' counters as JSON, malloc'd; the page
 * frees it with _free. See boot_telemetry.h. */
EMSCRIPTEN_KEEPALIVE char*
torirs_telemetry_json(void)
{
    return ToriRS_BootTelemetry_Json(&app.runner, &app.exec_runner);
}

/* The browser owns the frame clock, so the loop is inverted: instead of the
 * client calling the platform once per iteration, the platform calls the
 * client. Same step function either way. */
static void
frame_loop_tick(void)
{
    if( frame_loop_step() )
        return;
    web_pump_armed = 0;
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
        TORIRS_LOG("torirs: %s takes an integer in %d..%d\n", flag, min, max);
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
        TORIRS_LOG(
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
        TORIRS_ERR("torirs: failed to attach JS5 reference-table primer\n");
        return -1;
    }

    while( status == 0 )
    {
        if( PlatformXIOJs5Cache_Tick(prime, PlatformWindow_Ticks64()) < 0 )
            status = -1;
        else if( PlatformXIOJs5Cache_MetadataReady(prime) )
            status = 1;
        else
            PlatformWindow_SleepUntil(PlatformWindow_Ticks64() + 1u);
    }

    {
        struct Js5Progress progress;
        PlatformXIOJs5Cache_GetProgress(prime, &progress);
        if( status < 0 )
            TORIRS_ERR(
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
            TORIRS_LOG(
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
        TORIRS_ERR(
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
        TORIRS_ERR("torirs: failed to attach JS5 cache producer\n");
        return -1;
    }

    /*
     * The master index is always requested from the server. Valid local
     * reference tables may satisfy the subsequent checks, but no core task is
     * stepped until all server-authoritative metadata is installed.
     */
    while( (status = PlatformXIO_Js5Pump(app.runner.px, PlatformWindow_Ticks64())) == 0 )
        PlatformWindow_SleepUntil(PlatformWindow_Ticks64() + 1u);
    if( status < 0 )
    {
        struct Js5Progress progress;
        PlatformXIO_Js5GetProgress(app.runner.px, &progress);
        TORIRS_ERR(
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
        TORIRS_LOG(
            "torirs: JS5 metadata ready (%u references, %llu network bytes)\n",
            (unsigned)progress.references_ready,
            (unsigned long long)progress.bytes_received);
    }
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
    /* The Android lane's own GLES2 renderer, and its depth-buffered pass. */
    int use_gles2;
    int gles2_zbuffer;
    /* The GLES2 renderer driven through the dual-core lane
     * (--gles2-dualcore / --gles2-dualcore-zbuffer). */
    int gles2_dualcore;
    /* The browser's WebGL2 renderer, and its depth-buffered pass
     * (--webgl2 / --webgl2-zbuffer). */
    int use_webgl2;
    int webgl2_zbuffer;
    /* The browser's WebGL1 renderer (--webgl1 / --webgl1-zbuffer): the same
     * ES2 core Android runs as --gles2, on a WebGL1 context. */
    int use_webgl1;
    int webgl1_zbuffer;
    /* Android's OpenGL ES 3 renderer (--gles3 / --gles3-zbuffer): the same
     * ES3 core the browser runs as --webgl2, on EGL. */
    int use_gles3;
    int gles3_zbuffer;
    /* A renderer flag was given, by the command line or the manifest. The
     * launch then starts with that renderer whatever Client Settings saved. */
    int renderer_flag;
};

static void
main_print_usage(char const* program)
{
    TORIRS_LOG(
        "usage: %s [cache_dir] [interface_id] [--manifest <boot.ini>] "
        "[--dat1|--dat2] [--revconfig <ui.ini>] [--revconfig-cache <cache.ini>] "
        "[--bmp] [--connect host[:port]] [--port N] [--offline] [--user U] "
        "[--pass P] [--rev lc254|lc245_2|xrsps233] "
        "[--js5|--no-js5] [--js5-host H] [--js5-port N] "
        "[--js5-fallback-port N] [--js5-revision N] [--uncapped] "
        "[--pacer gameshell|deadline] "
        "[--windowmode fixed|resizable] [--window WxH] "
        "[--opengl3|--opengl3-zbuffer|--webgl1|--webgl1-zbuffer|"
        "--webgl2|--webgl2-zbuffer|--gles2|--gles2-zbuffer|--gles3|--gles3-zbuffer|"
        "--gles2-dualcore|--gles2-dualcore-zbuffer|"
        "--d3d9|--d3d9-zbuffer|--soft3d]\n",
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
                TORIRS_ERR("torirs: [client:args] cannot contain --manifest\n");
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
                    "--js5-revision", argv[++argi], 1, 2147483647, &executor_cfg.js5_revision) !=
                0 )
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
        if( strcmp(argv[argi], "--resume") == 0 && argi + 1 < argc )
        {
            cfg.connect_resume = argv[++argi];
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
        if( strcmp(argv[argi], "--pacer") == 0 && argi + 1 < argc )
        {
            /* Rejected here rather than at pacer init, which does not run until
             * the cache and manifest are up: a typo would otherwise take a full
             * boot to surface, and for a knob whose only purpose is A/B
             * measurement, silently running the other arm is the worst outcome
             * there is. TORIRS_PACER is checked again at init, because the
             * environment does not come through here. */
            int pacer_ok = 0;
            pacer_name_opt = argv[++argi];
            ToriRS_Pacer_KindFromName(pacer_name_opt, &pacer_ok);
            if( !pacer_ok )
            {
                fprintf(
                    stderr,
                    "torirs: --pacer takes gameshell|deadline (got '%s')\n",
                    pacer_name_opt);
                return 0;
            }
            continue;
        }
        if( strcmp(argv[argi], "--windowmode") == 0 && argi + 1 < argc )
        {
            cfg.window_mode = CS2VM_WindowModeFromName(argv[++argi]);
            if( !cfg.window_mode )
            {
                TORIRS_LOG("torirs: --windowmode takes fixed|resizable\n");
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
                TORIRS_LOG("torirs: --window takes WxH\n");
                return 0;
            }
            cfg.window_w = (int)w;
            cfg.window_h = (int)h;
            continue;
        }
        /*
         * The GPU renderer flags. One spelling per lane, and each build accepts
         * only the spelling it can honour and names the right one otherwise, so
         * a flag is never silently ignored:
         *
         *   --opengl3[-zbuffer]   desktop GL 3.2 (TORIRS_HAVE_GL3)
         *   --webgl1[-zbuffer]    the browser: the GLES2 renderer on a WebGL1
         *                         context (TORIRS_HAVE_GLES2 + TORIRS_PLATFORM_WEB)
         *   --webgl2[-zbuffer]    the browser: the WebGL2 renderer, OpenGL ES
         *                         3.0 (TORIRS_HAVE_WEBGL2)
         *   --gles2[-zbuffer]     Android: the same GLES2 renderer on EGL
         *                         (TORIRS_HAVE_GLES2, not web)
         *
         * --webgl1 and --gles2 select the same renderer; they are kept as two
         * names because a manifest carrying the browser's flag must not be
         * aliased onto a phone unnoticed, or the reverse. --webgl2 is a
         * DIFFERENT renderer from both (platform_web_renderer_webgl2_*.c), so it
         * is a third name and not a modifier of the first.
         */
        if( strcmp(argv[argi], "--opengl3") == 0 || strcmp(argv[argi], "--opengl3-zbuffer") == 0 )
        {
            int const zbuffer = strcmp(argv[argi], "--opengl3-zbuffer") == 0;
            /* Read in every branch below but the "not built here" one. */
            (void)zbuffer;
#if defined(TORIRS_HAVE_GL3)
            state->use_opengl3 = 1;
            state->renderer_flag = 1;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->gl3_zbuffer = zbuffer;
            state->use_gles2 = 0;
            state->use_webgl2 = 0;
            state->webgl2_zbuffer = 0;
            continue;
#elif defined(TORIRS_HAVE_GLES2) && defined(TORIRS_PLATFORM_WEB)
            TORIRS_ERR(
                "torirs: this build renders through WebGL1 — use --webgl1%s\n",
                zbuffer ? "-zbuffer" : "");
            return 0;
#elif defined(TORIRS_HAVE_GLES2)
            TORIRS_ERR(
                "torirs: this build renders through GLES2 — use --gles2%s\n",
                zbuffer ? "-zbuffer" : "");
            return 0;
#else
            TORIRS_LOG("torirs: %s is not available in this build\n", argv[argi]);
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--webgl1") == 0 || strcmp(argv[argi], "--webgl1-zbuffer") == 0 )
        {
            int const zbuffer = strcmp(argv[argi], "--webgl1-zbuffer") == 0;
            /* Read in every branch below but the "not built here" one. */
            (void)zbuffer;
#if defined(TORIRS_HAVE_WEBGL1)
            state->use_webgl1 = 1;
            state->renderer_flag = 1;
            state->webgl1_zbuffer = zbuffer;
            state->use_gles2 = 0;
            state->gles2_zbuffer = 0;
            state->use_gles3 = 0;
            state->gles3_zbuffer = 0;
            state->use_opengl3 = 0;
            state->gl3_zbuffer = 0;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->use_webgl2 = 0;
            state->webgl2_zbuffer = 0;
            continue;
#elif defined(TORIRS_HAVE_GLES2)
            /* WebGL1 is a browser API. The renderer is the same one, but the
             * flag is refused rather than aliased, so a manifest written for
             * the browser does not run on a phone unnoticed. */
            TORIRS_ERR(
                "torirs: %s is the browser build's flag — use --gles2%s\n",
                argv[argi],
                zbuffer ? "-zbuffer" : "");
            return 0;
#elif defined(TORIRS_HAVE_GL3)
            TORIRS_LOG(
                "torirs: %s is the browser build's flag — use --opengl3%s\n",
                argv[argi],
                zbuffer ? "-zbuffer" : "");
            return 0;
#else
            TORIRS_LOG(
                "torirs: %s is the browser build's flag and is not available here\n", argv[argi]);
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--webgl2") == 0 || strcmp(argv[argi], "--webgl2-zbuffer") == 0 )
        {
            int const zbuffer = strcmp(argv[argi], "--webgl2-zbuffer") == 0;
            /* Read in every branch below but the "not built here" one. */
            (void)zbuffer;
#if defined(TORIRS_HAVE_WEBGL2)
            state->use_webgl2 = 1;
            state->renderer_flag = 1;
            state->webgl2_zbuffer = zbuffer;
            state->use_webgl1 = 0;
            state->webgl1_zbuffer = 0;
            state->use_gles3 = 0;
            state->gles3_zbuffer = 0;
            state->use_gles2 = 0;
            state->gles2_zbuffer = 0;
            state->gles2_dualcore = 0;
            state->use_opengl3 = 0;
            state->gl3_zbuffer = 0;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            continue;
#elif defined(TORIRS_HAVE_GLES2) && defined(TORIRS_PLATFORM_WEB)
            TORIRS_ERR(
                "torirs: this build has no WebGL2 renderer — use --webgl1%s\n",
                zbuffer ? "-zbuffer" : "");
            return 0;
#else
            TORIRS_LOG(
                "torirs: %s is the browser build's flag and is not available here\n", argv[argi]);
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--gles2-dualcore") == 0 ||
            strcmp(argv[argi], "--gles2-dualcore-zbuffer") == 0 )
        {
            int const zbuffer = strcmp(argv[argi], "--gles2-dualcore-zbuffer") == 0;
            (void)zbuffer;
#if defined(TORIRS_HAVE_GLES2_DUALCORE)
            state->use_gles2 = 1;
            state->renderer_flag = 1;
            state->gles2_zbuffer = zbuffer;
            state->gles2_dualcore = 1;
            state->use_opengl3 = 0;
            state->gl3_zbuffer = 0;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->use_webgl2 = 0;
            state->webgl2_zbuffer = 0;
            continue;
#else
            TORIRS_ERR(
                "torirs: %s is the Android build's flag and is not available here\n", argv[argi]);
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--gles3") == 0 || strcmp(argv[argi], "--gles3-zbuffer") == 0 )
        {
            int const zbuffer = strcmp(argv[argi], "--gles3-zbuffer") == 0;
            /* Read in every branch below but the "not built here" one. */
            (void)zbuffer;
#if defined(TORIRS_HAVE_GLES3)
            state->use_gles3 = 1;
            state->renderer_flag = 1;
            state->gles3_zbuffer = zbuffer;
            state->use_gles2 = 0;
            state->gles2_zbuffer = 0;
            state->gles2_dualcore = 0;
            state->use_webgl1 = 0;
            state->webgl1_zbuffer = 0;
            state->use_webgl2 = 0;
            state->webgl2_zbuffer = 0;
            state->use_opengl3 = 0;
            state->gl3_zbuffer = 0;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            continue;
#elif defined(TORIRS_HAVE_WEBGL2)
            TORIRS_ERR(
                "torirs: %s is the Android build's flag — use --webgl2%s\n",
                argv[argi],
                zbuffer ? "-zbuffer" : "");
            return 0;
#else
            TORIRS_ERR(
                "torirs: %s is the Android build's flag and is not available here\n", argv[argi]);
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--gles2") == 0 || strcmp(argv[argi], "--gles2-zbuffer") == 0 )
        {
            int const zbuffer = strcmp(argv[argi], "--gles2-zbuffer") == 0;
            /* Read in every branch below but the "not built here" one. */
            (void)zbuffer;
#if defined(TORIRS_HAVE_GLES2) && !defined(TORIRS_PLATFORM_WEB)
            state->use_gles2 = 1;
            state->renderer_flag = 1;
            state->gles2_zbuffer = zbuffer;
            state->use_opengl3 = 0;
            state->gl3_zbuffer = 0;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->use_webgl2 = 0;
            state->webgl2_zbuffer = 0;
            continue;
#elif defined(TORIRS_HAVE_GLES2)
            TORIRS_ERR(
                "torirs: %s is the Android build's flag — use --webgl1%s\n",
                argv[argi],
                zbuffer ? "-zbuffer" : "");
            return 0;
#else
            TORIRS_ERR(
                "torirs: %s is the Android build's flag and is not available here\n", argv[argi]);
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--d3d9") == 0 )
        {
#if defined(TORIRS_HAVE_D3D9)
            state->use_d3d9 = 1;
            state->renderer_flag = 1;
            state->use_opengl3 = 0;
            state->d3d9_zbuffer = 0;
            state->use_gles2 = 0;
            continue;
#else
            TORIRS_LOG("torirs: --d3d9 is not available in this build\n");
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--d3d9-zbuffer") == 0 )
        {
#if defined(TORIRS_HAVE_D3D9)
            state->use_d3d9 = 1;
            state->renderer_flag = 1;
            state->use_opengl3 = 0;
            state->use_gles2 = 0;
            state->d3d9_zbuffer = 1;
            continue;
#else
            TORIRS_LOG("torirs: --d3d9-zbuffer is not available in this build\n");
            return 0;
#endif
        }
        if( strcmp(argv[argi], "--soft3d") == 0 )
        {
            state->renderer_flag = 1;
            state->use_opengl3 = 0;
            state->use_d3d9 = 0;
            state->d3d9_zbuffer = 0;
            state->gl3_zbuffer = 0;
            state->use_gles2 = 0;
            state->gles2_zbuffer = 0;
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
            /* strtol with the end pointer, not atoi: 0 is a REAL interface
             * (100guide_eggs_overlay), and atoi's 0-on-garbage made it
             * indistinguishable from a typo. Only non-numeric input and
             * negative ids are invalid. */
            char* id_end = NULL;
            cfg.interface_id = (int)strtol(argv[argi], &id_end, 10);
            if( id_end == argv[argi] || *id_end != '\0' || cfg.interface_id < 0 )
            {
                TORIRS_ERR("invalid interface id: %s\n", argv[argi]);
                return 0;
            }
            positional++;
            continue;
        }

    invalid:
        TORIRS_ERR(
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
    return strcmp(argument, "--manifest") == 0 || strcmp(argument, "--port") == 0 ||
           strcmp(argument, "--revconfig") == 0 || strcmp(argument, "--revconfig-cache") == 0 ||
           strcmp(argument, "--connect") == 0 || strcmp(argument, "--user") == 0 ||
           strcmp(argument, "--pass") == 0 || strcmp(argument, "--resume") == 0 ||
           strcmp(argument, "--rev") == 0 ||
           strcmp(argument, "--js5-host") == 0 || strcmp(argument, "--js5-port") == 0 ||
           strcmp(argument, "--js5-fallback-port") == 0 ||
           strcmp(argument, "--js5-revision") == 0 || strcmp(argument, "--windowmode") == 0 ||
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
        .use_gles2 = 0,
        .gles2_zbuffer = 0,
        .gles2_dualcore = 0,
        .use_webgl2 = 0,
        .webgl2_zbuffer = 0,
        .use_webgl1 = 0,
        .webgl1_zbuffer = 0,
        .use_gles3 = 0,
        .gles3_zbuffer = 0,
        .renderer_flag = 0,
    };
    int argi;
    int i;
    int preview_width = 0;
    int preview_height = 0;

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
            boot_manifest.client_arg_count, manifest_argv, 0, 1, argv[0], &arg_state) )
        return 1;
    if( !main_parse_argument_layer(argc, argv, 1, 0, argv[0], &arg_state) )
        return 1;

    int const write_bmp = arg_state.write_bmp;
    int const use_opengl3 = arg_state.use_opengl3;
    int const use_d3d9 = arg_state.use_d3d9;
    int const d3d9_zbuffer = arg_state.d3d9_zbuffer;
    int const gl3_zbuffer = arg_state.gl3_zbuffer;
    int const use_gles2 = arg_state.use_gles2;
    int const gles2_zbuffer = arg_state.gles2_zbuffer;
    int const gles2_dualcore = arg_state.gles2_dualcore;
    int const use_webgl2 = arg_state.use_webgl2;
    int const webgl2_zbuffer = arg_state.webgl2_zbuffer;
    int const use_webgl1 = arg_state.use_webgl1;
    int const webgl1_zbuffer = arg_state.webgl1_zbuffer;
    int const use_gles3 = arg_state.use_gles3;
    int const gles3_zbuffer = arg_state.gles3_zbuffer;
    int const renderer_flag = arg_state.renderer_flag;

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
            cfg.cache_kind = named.epoch == 1 /* DAT1 */ ? APP_CACHE_DAT1 : APP_CACHE_DAT2;
        }
        else
        {
            TORIRS_LOG(
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
            TORIRS_ERR("torirs: %s\n", error);
            return 1;
        }
        if( executor_cfg.js5_enabled )
            TORIRS_LOG(
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
        TORIRS_LOG("torirs: JS5 incremental cache loading is native-only\n");
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

    /*
     * The boot interface, when neither a positional argument nor a manifest
     * named one. It is a cache id like any other, so the resolved profile
     * answers it: `[iface:boot]` in the *_ui.ini (or the cache half, or the
     * manifest's own inline sections -- all three are read here, in load
     * order).
     *
     * Left unset if the profile does not state one. A dat1 boot then has no
     * gameframe to open and says so; a dat2 boot does not come through here at
     * all, because its manifest states `interface_id` in `[ui:boot]`.
     */
    if( cfg.interface_id == INTERFACE_ID_UNSET )
    {
        struct RevConfigRefs boot_refs;
        int declared;
        RevConfigRefs_Init(&boot_refs);
        RevConfigRefs_LoadSources(
            &boot_refs, cfg.revconfig_ui_ini, cfg.revconfig_cache_ini, cfg.revconfig_inline_ini);
        declared = RevConfigRefs_Get(&boot_refs, "iface", "boot");
        RevConfigRefs_Free(&boot_refs);
        if( declared > 0 )
            cfg.interface_id = declared;
    }

    {
        /* An on-demand boot opens no directory, so naming one here would be a
         * line of output pointing at a cache this run never reads -- the
         * DEFAULT_CACHE_DIR fallback at that, which is somebody else's world.
         * Say where the bytes actually come from instead. */
        char const* cache_label = cfg.cache_on_demand ? "(on demand)" : cfg.cache_dir;

        if( cfg.revconfig_ui_ini )
            TORIRS_LOG(
                "torirs: %s cache=%s revconfig=%s cache_ini=%s\n",
                cfg.cache_kind == APP_CACHE_DAT1 ? "dat1" : "dat2",
                cache_label,
                cfg.revconfig_ui_ini,
                cfg.revconfig_cache_ini ? cfg.revconfig_cache_ini : "(none)");
        else
            TORIRS_LOG(
                "torirs: %s cache=%s iface=%d\n",
                cfg.cache_kind == APP_CACHE_DAT1 ? "dat1" : "dat2",
                cache_label,
                cfg.interface_id);
    }

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
        if( getenv("TORIRS_PREVIEW_BMP") &&
            (root_w <= 0 || root_h <= 0 || root_w > 4096 || root_h > 4096) )
        {
            TORIRS_LOG("native preview size must be 1..4096 on each axis\n");
            return 1;
        }
        UITree_LayoutSetRootSize((int)root_w, (int)root_h);
        /* The ordinary client canvas has a deliberate 765x503 floor. A native
         * interface preview is a host slot rather than a game window, so keep
         * the requested dimensions and restore them after App_Init publishes
         * the normal canvas through App_SetCanvasSize. This opt-in path is the
         * only place where a sub-minimum canvas is legal. */
        if( getenv("TORIRS_PREVIEW_BMP") )
        {
            preview_width = (int)root_w;
            preview_height = (int)root_h;
        }
        TORIRS_LOG("root_size: %dx%d\n", UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
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
    else if( cfg.window_w > 0 && cfg.window_h > 0 && cfg.window_mode != CS2VM_WINDOW_MODE_FIXED )
    {
        UITree_LayoutSetRootSize(cfg.window_w, cfg.window_h);
    }

#if !defined(TORIRS_PLATFORM_WEB)
    if( executor_prepare_js5_cache() != 0 )
        return 1;
#endif
    ToriRS_BootTelemetry_Mark("app_init");
    App_Init(&app, &cfg);
    ToriRS_BootTelemetry_Mark("app_init:done");
    if( getenv("TORIRS_PREVIEW_BMP") )
    {
        /* Default to the fixed-mode main/modal slot used by cs2dom. App_Init
         * has no interface nodes yet, so restoring the root and host viewport
         * here cannot skip resize hooks; App_OpenRootInterface below observes
         * these exact dimensions on its first layout and onLoad pass. */
        if( preview_width <= 0 || preview_height <= 0 )
        {
            preview_width = 512;
            preview_height = 334;
        }
        UITree_LayoutSetRootSize(preview_width, preview_height);
        app.host.viewport_w = preview_width;
        app.host.viewport_h = preview_height;
        TORIRS_LOG("preview_size: %dx%d\n", preview_width, preview_height);
    }
    /*
     * No JS5 attach on the browser lane, and nothing missing.
     *
     * A producer is attached to a PlatformX_IO here so that a cache miss can
     * park on it. In a browser the platform executor is JavaScript and attaches
     * nothing: the producer web_cache_boot.c started before main() is still
     * running, and the executor reaches it through its own entry points
     * (ToriRS_WebApi_Js5*) when the record database misses.
     */
#if !defined(TORIRS_PLATFORM_WEB)
    if( executor_attach_and_prime_js5() != 0 )
    {
        App_Shutdown(&app);
        return 1;
    }
#endif
    TorirsPerf_Init(0);
    /* Before anything can read it: App_Init has already run RS_CS2Host_Init,
     * whose default the manifest is entitled to override, and the root
     * interface's own scripts (opened on the next line) call getwindowmode.
     *
     * An unstated windowmode is DERIVED from the interface logic rather than
     * left at the host's default, because that default (resizable) is a CS2
     * assumption: a CS2 gameframe relayouts to whatever canvas it is given, and
     * a CS1 one cannot -- it is a baked 765x503 layout, and the only thing a
     * bigger canvas does to it is leave the rest of the canvas black.
     *
     * That was visible two ways at once on a HighDPI display, where the canvas
     * follows a drawable twice the window points: the frame drew at 1x in the
     * top-left quarter, and every click landed at double its coordinate,
     * because MapMouse scales window points into the canvas by exactly the
     * ratio the frame was not drawn at. Fixed pins the canvas at 765x503 and
     * letterboxes it into the drawable, which on a 2x display is an exact
     * doubling -- and MapMouse undoes the same letterbox, so clicks land where
     * they are drawn.
     *
     * A manifest that states `[ui:boot] windowmode=` still wins: this only
     * fills in the case nobody answered. */
    if( !cfg.window_mode && App_UiLogic(&app) == APP_UI_LOGIC_CS1 )
        cfg.window_mode = CS2VM_WINDOW_MODE_FIXED;
    App_SetBootWindowMode(&app, cfg.window_mode);
    /*
     * No gameframe before the server asks for one.
     *
     * A networked boot is re-rooted at login: the world sends IF_OPENTOP with
     * the group the player's display mode actually wants
     * (ToriRSServer_GameframeOpentop), and App_OpenRootInterface throws the
     * whole tree away to bake it. Mounting `[ui:boot] interface_id` here
     * therefore loads and lays out an entire cache gameframe -- packs, onload
     * scripts, transmit dispatch -- purely to discard it a moment later, and
     * shows it meanwhile, so a client that has not connected to anything looks
     * exactly like one that has. The world half of the same automount is
     * already gated this way (`[ui:varc]`, `[ui:gameframe]` and the region load
     * in Task_AppBoot all test `!net_enabled`); this is the root itself.
     *
     * Offline there is no such packet, so the manifest's id is the only answer
     * there will ever be and it still roots the tree -- which is what keeps the
     * offline worlds (bench, mapeditor, packed, worldmap, rs634void) and
     * `--offline` on any networked manifest booting into a frame as before.
     *
     * -1 rather than 0 for "no root": INTERFACE_ID_UNSET is 0, but 0 is a real
     * cache group to the builder, which would mount it instead of nothing.
     */
    /*
     * A networked boot goes to the title screen when the profile declares one;
     * everything else roots a frame as before.
     *
     * The gate is the profile's, not a flag: a manifest that ships no
     * [layout:title] has no title screen to show, and the offline, bench and
     * map-editor lanes are exactly that. Credentials on the command line no
     * longer skip the screen -- they prefill it and submit through the same
     * path a clicked Login takes, so the scripted lanes exercise the flow
     * rather than bypassing it.
     */
    if( app.net_enabled && App_HasTitleScreen(&app) )
    {
        /* Loading FIRST, login after: the gameframe bake -- where the
         * interface packs, media and fonts are actually fetched -- runs under
         * the startup loading bar, and the title screen replaces it the
         * moment it settles. The post-login rebake then crosses warm caches
         * showing only "entering world", instead of replaying the loading
         * captions after the login screen. */
        App_BootGameframeThenTitle(&app);
    }
    else
    {
        App_OpenRootInterface(
            &app,
            app.net_enabled || cfg.interface_id == INTERFACE_ID_UNSET ? -1 : cfg.interface_id);
    }

    /* Boot is fully async (App_RunOnce pumps it; App_Render shows a loading
     * bar). The headless harness/debug paths below inspect the freshly built
     * tree synchronously, so pump the boot to completion for them; the plain
     * interactive run skips this and renders the loading state instead. */
    if( write_bmp || getenv("TORIRS_PREVIEW_BMP") || getenv("TORIRS_WORLD_NODE_DEBUG") ||
        getenv("TORIRS_SIM_CLICK") || getenv("TORIRS_SIM_KEYS") || getenv("TORIRS_SIM_WORLD_KEY") ||
        getenv("TORIRS_SIM_MOUSE_CLICK") || getenv("TORIRS_DUMP_EMIT") ||
        getenv("TORIRS_DUMP_TREE") || getenv("TORIRS_WORLD_BMP") || getenv("TORIRS_DUMP_ROLES") ||
        getenv("TORIRS_DUMP_CLIENTCODES") || getenv("TORIRS_CMD_REPLAY") )
        App_BootWait(&app);

    if( getenv("TORIRS_PREVIEW_BMP") && app.preview_state_failed )
    {
        App_Shutdown(&app);
        return 1;
    }

    /* TORIRS_WORLD_NODE_DEBUG=1: world viewport node state + root sibling
     * chain (the emit walk draws the chain in order). idx=-1 means the opened
     * interface has no viewport, so the world is intentionally not loaded;
     * client_code=1337 confirms a cache CONTENT_WORLD layer was the source. */
    if( getenv("TORIRS_WORLD_NODE_DEBUG") )
    {
        int32_t widx = App_WorldNodeIndex(&app);
        TORIRS_LOG("world node idx=%d\n", widx);
        if( widx >= 0 )
        {
            struct UITreeComponent const* wc = &app.tree->components[widx];
            TORIRS_LOG(
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
        TORIRS_LOG("root chain:");
        for( int32_t r = app.tree->root_index; r >= 0; r = app.tree->components[r].next_sibling )
            TORIRS_LOG(" 0x%08x", app.tree->components[r].component_id);
        TORIRS_LOG("\n");
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
            TORIRS_LOG("sim_click: com=0x%x script=%d\n", com_id, hook.script_id);
            RS_CS2_DispatchHook(&app.host, &app.runner, com_id, &hook);
        }
        else
            TORIRS_ERR("sim_click: component 0x%x not found\n", com_id);

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
            TORIRS_LOG("sim_click: post-click ticks done\n");
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

            TORIRS_LOG(
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
                TORIRS_LOG("pick_sweep: %d,%d\n", pxi, py);
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
            TORIRS_LOG("hover_probe y=%3d:", hy);
            for( int hx = hx0; hx <= hx1; hx += hstep )
                TORIRS_LOG(
                    " %d",
                    UITree_FindHoveredComponentIdForRegion(
                        app.tree,
                        &app.ui_host,
                        -1,
                        hx,
                        hy,
                        0,
                        0,
                        UITREE_LAYOUT_ROOT_W,
                        UITREE_LAYOUT_ROOT_H));
            TORIRS_LOG("\n");
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
                TORIRS_LOG("sim_keys: %c%ld\n", kind, val);
            }
            else if( sk_tail_ticks-- <= 0 )
                break;
            LibToriRS_Input_End(sk_input);
            (void)App_RunOnce(&app, sk_ms, sk_input);
            sk_ms += 20;
        }
        TORIRS_LOG("sim_keys: done\n");
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

            TORIRS_LOG("sim_world_key: '%c' at %d,%d\n", key_char ? key_char : '?', wkx, wky);
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
     * yaw keys are the arrows, which the key sim above cannot send.
     *
     * TORIRS_SIM_CAMERA_YAW_FRAME says the drive wants the park applied from a
     * MAIN-LOOP frame instead -- the only placement a lane that has to log in,
     * skip a tutorial and teleport can use -- and that block owns the variable
     * then. Running both would spend two extra frames here on a clock that
     * starts at 1 ms, which on a live lane is the session teardown the shots
     * README dates its broken hover captures by. */
    if( getenv("TORIRS_SIM_CAMERA_YAW") && !getenv("TORIRS_SIM_CAMERA_YAW_FRAME") )
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
                /* Both, and orbit is the one that lasts: the follow step
                 * rebuilds world_camera.yaw from orbit.yaw, so a park written
                 * only to world_camera is gone by the next cycle. This path
                 * renders immediately and never saw that, which is exactly why
                 * the in-loop twin above could not reuse it. */
                app.orbit.yaw =
                    ToriDraw_NormalizeAngle((int)strtol(getenv("TORIRS_SIM_CAMERA_YAW"), NULL, 0));
                app.orbit.yaw_velocity = 0;
                app.world_camera.yaw = app.orbit.yaw;
                TORIRS_LOG("sim_camera_yaw: %d\n", app.world_camera.yaw);
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
                TORIRS_LOG(
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
            TORIRS_LOG(
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

    /*
     * TORIRS_DUMP_ROLES=1: what every declared semantic role resolves to.
     *
     * The one question a screenshot cannot answer about a role, and the same
     * question UITree_FrameHiddenCount exists for one level down: a plugin
     * that offers no verb may be looking at a lane whose profile never named
     * the element, or at a binding that names the wrong node. "declared but
     * unresolved" and "not declared at all" print differently here, because
     * they are different bugs with the same symptom.
     *
     * Also the survey instrument. A role bound on a lane it has not been
     * measured on prints its node and its box, which is how the binding gets
     * checked against what is actually on screen rather than against memory.
     */
    if( getenv("TORIRS_DUMP_ROLES") && app.tree )
    {
        /*
         * Which interface groups are actually mounted, before the roles
         * themselves.
         *
         * dump_tree walks from the root interface and so shows only what hangs
         * under it; a role bound to `iface(<name>)` resolves against the whole
         * component array. When a binding does not resolve, "that group is not
         * mounted on this lane" and "that group is mounted and the child is
         * wrong" are the two answers, and this is what tells them apart.
         */
        {
            int groups[64];
            int group_count = 0;
            for( uint32_t gi = 0; gi < app.tree->component_count; gi++ )
            {
                int id = app.tree->components[gi].component_id;
                int group;
                int seen = 0;
                if( app.tree->components[gi].freed || id < 0 )
                    continue;
                group = (id >> 16) & 0xffff;
                for( int k = 0; k < group_count; k++ )
                    seen |= groups[k] == group;
                if( !seen && group_count < (int)(sizeof(groups) / sizeof(groups[0])) )
                    groups[group_count++] = group;
            }
            TORIRS_LOG("mounted groups (%d):", group_count);
            for( int k = 0; k < group_count; k++ )
                TORIRS_LOG(" %d", groups[k]);
            TORIRS_LOG("\n");
        }

        TORIRS_LOG("roles: %d declared\n", app.ui_roles.count);
        for( int ri = 0; ri < app.ui_roles.count; ri++ )
        {
            struct UITreeRoleEntry const* entry = &app.ui_roles.entries[ri];
            int32_t node = UITree_RoleNode(app.tree, &app.ui_roles, (uint16_t)(ri + 1));

            if( node < 0 )
            {
                TORIRS_LOG(
                    "  %-24s UNRESOLVED (%s%d match rungs)\n",
                    entry->name,
                    entry->authored ? "authored + " : "",
                    entry->matcher_count);
                continue;
            }
            struct UITreeComponent const* c = &app.tree->components[node];
            TORIRS_LOG(
                "  %-24s node=%d com=0x%08x type=%d%s%s box=%d,%d %dx%d\n",
                entry->name,
                (int)node,
                c->component_id,
                (int)c->type,
                c->dynamic ? " dynamic" : "",
                (c->behavior.hide || c->frame_hidden) ? " hidden" : "",
                c->position.abs_x,
                c->position.abs_y,
                c->position.abs_w,
                c->position.abs_h);
        }
    }

    /*
     * TORIRS_DUMP_CLIENTCODES=1: every live node carrying a clientCode.
     *
     * The cache's own semantic tagging, which is where a `clientcode()` rung
     * gets its number from. Reading it off the tree is the point: the code
     * tables in rs_clientcode.h are the CS1 era's, and which of them a given
     * dat2 gameframe actually ships is a fact about that cache.
     */
    if( getenv("TORIRS_DUMP_CLIENTCODES") && app.tree )
    {
        TORIRS_LOG("clientcodes: %d live\n", app.tree->client_code.count);
        for( int32_t si = 0; si < app.tree->client_code.count; si++ )
        {
            int32_t idx = app.tree->client_code.slots[si];
            struct UITreeComponent const* c;
            if( idx < 0 || (uint32_t)idx >= app.tree->component_count )
                continue;
            c = &app.tree->components[idx];
            if( c->freed )
                continue;
            TORIRS_LOG(
                "  code=%-5d node=%d com=0x%08x type=%d box=%d,%d %dx%d\n",
                c->behavior.client_code,
                (int)idx,
                c->component_id,
                (int)c->type,
                c->position.abs_x,
                c->position.abs_y,
                c->position.abs_w,
                c->position.abs_h);
        }
    }

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
        TORIRS_LOG("emit_skip: com=0x%x dropped %d cmds\n", skip_com, app.emit.count - kept);
        app.emit.count = kept;
    }

    /* TORIRS_PREVIEW_BMP=path: render one deterministic interface frame through
     * the production App/UITree/Soft3D pipeline and exit before creating a
     * platform window. Unlike TORIRS_WORLD_BMP this accepts an output path and,
     * with the size restoration above, can represent a real 512x334 host slot. */
    if( getenv("TORIRS_PREVIEW_BMP") )
    {
        char const* path = getenv("TORIRS_PREVIEW_BMP");
        int* pixels;
        FILE* probe;
        if( !path[0] )
        {
            TORIRS_LOG("TORIRS_PREVIEW_BMP requires a non-empty path\n");
            App_Shutdown(&app);
            return 1;
        }
        pixels = calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
        assert(pixels);
        App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        if( getenv("TORIRS_PREVIEW_TREE") )
        {
            char const* tree_path = getenv("TORIRS_PREVIEW_TREE");
            if( !tree_path[0] || UITreeSnapshot_WriteJson(
                                     app.tree,
                                     &app.emit,
                                     tree_path,
                                     cfg.interface_id,
                                     UITREE_LAYOUT_ROOT_W,
                                     UITREE_LAYOUT_ROOT_H) != 0 )
            {
                TORIRS_ERR("failed to write native preview tree %s\n", tree_path);
                free(pixels);
                App_Shutdown(&app);
                return 1;
            }
        }
        bmp_write_file(path, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        free(pixels);
        probe = fopen(path, "rb");
        if( !probe )
        {
            TORIRS_ERR("failed to write native preview %s\n", path);
            App_Shutdown(&app);
            return 1;
        }
        fclose(probe);
        TORIRS_LOG(
            "wrote %s (%dx%d, %d emit cmds)\n",
            path,
            UITREE_LAYOUT_ROOT_W,
            UITREE_LAYOUT_ROOT_H,
            app.emit.count);
        App_Shutdown(&app);
        return 0;
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
            TORIRS_LOG("wrote %s (%d cmds)\n", path, app.emit.count);
        else
            TORIRS_ERR("failed to write %s\n", path);
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
            TORIRS_LOG(
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
            TORIRS_LOG("ORDER parent=0x%08x link order:", parent->component_id);
            for( child = parent->first_child; child >= 0;
                 child = app.tree->components[child].next_sibling )
            {
                struct UITreeComponent const* cc = &app.tree->components[child];
                TORIRS_LOG(
                    " %s(0x%08x,sub=%d)",
                    cc->dynamic ? "dyn" : "sta",
                    cc->component_id,
                    cc->dynamic ? cc->dynamic_child_index : -1);
            }
            TORIRS_LOG("\n");
        }
    }

    if( getenv("TORIRS_DUMP_EMIT") )
    {
        for( int i = 0; i < app.emit.count; i++ )
        {
            struct UITreeEmitDesc* d = &app.emit.cmds[i];
            TORIRS_LOG(
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
            TORIRS_ERR("TEX_AUDIT: %d missing scene textures:", n);
            for( int i = 0; i < n; i++ )
                TORIRS_LOG(" %d", ids[i]);
            TORIRS_LOG("\n");
            TORIRS_ERR("TEX_AUDIT: failed:");
            for( int i = 0; i < 2048; i++ )
                if( app.bridge.texture_failed[i] )
                    TORIRS_LOG(" %d", i);
            TORIRS_LOG("\n");
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
            struct
            {
                int x, z, l, id, shape, angle;
            } locs[4096];
            int nlocs = 0;
            for( int it = World_EntityPoolHead(pool); it != WORLD_ENTITY_NIL && nlocs < 4096;
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
                WorldBuilder_ApplyLocChange(
                    app.world_builder,
                    locs[k].x,
                    locs[k].z,
                    locs[k].l,
                    locs[k].id,
                    locs[k].shape,
                    locs[k].angle);
                applied++;
            }
            TORIRS_LOG("TEST_LOCCHANGE: applied %d loc changes (%d walls) ok\n", applied, walls);
        }
        App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        bmp_write_file("build/world.bmp", pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        TORIRS_LOG("wrote build/world.bmp (%d emit cmds)\n", app.emit.count);
        free(pixels);
        App_Shutdown(&app);
        return 0;
    }

    {
        /* The window is called ToriRS, and stays called ToriRS.
         *
         * It used to carry the boot interface id, and then be rewritten every
         * frame with the hovered and clicked component ids. On Windows that is
         * not a string assignment: SetWindowText crosses into the kernel,
         * posts WM_SETTEXT, and repaints the non-client title bar -- fifty
         * times a second, to show two numbers only a developer reads. It cost
         * measurable kernel time on the XP target (see
         * docs/2004Scape_Memory_Usage.md), which is a strange price for a
         * caption nobody was looking at.
         *
         * The hover/click ids belong in the developer overlay, which is where
         * a developer already looks and which costs nothing when it is off. */
        char const* title = "ToriRS";

        platform = PlatformWindow_New();

        if( !platform )
        {
            TORIRS_ERR("window platform alloc failed\n");
            App_Shutdown(&app);
            return 1;
        }
        /* Only when the manifest actually said something. Unset leaves the
         * platform's own default standing, which is what makes HighDPI
         * automatic: a boot that never heard of this still gets a device-pixel
         * drawable on the displays that have one.
         *
         * Before either Init below, and it has to be: ALLOW_HIGHDPI is a
         * window-creation flag and SDL cannot add it to a live window. Getting
         * this after the window is a drawable at window points for the whole
         * session, which the compositor then magnifies -- the frame looks
         * scaled and nothing downstream can tell that it was. */
#if defined(TORIRS_PLATFORM_WEB)
        if( cfg.hidpi )
            PlatformWindow_SetWantHighDPI(cfg.hidpi > 0);
#else
        /*
         * Off the web, `hidpi=` no longer decides the drawable; it decides what
         * HighDPI "automatic" means. The drawable is always device pixels, so
         * the density is always detectable and every HighDPI mode is one the
         * player can switch to live. `hidpi=0` asked for a frame laid out and
         * rendered at window points, and "window points" is exactly that:
         * the same size and the same pixel cost, magnified by the present
         * instead of by the compositor.
         *
         * The web keeps the creation flag: there the density is
         * devicePixelRatio, up to 3 on a phone, and a software rasteriser that
         * has to opt in to that many pixels.
         *
         * TORIRS_HIDPI still overrides at creation (platform_sdl2.c), and =1
         * also asks for device pixels, as it always has.
         */
#if !defined(TORIRS_PLATFORM_ANDROID)
        /* Android is absent on purpose: a Surface has no points layer, so the
         * shared manifests' hidpi=0 never shrank a phone's frame, and must not
         * start to. */
        {
            char const* const env = getenv("TORIRS_HIDPI");
            bool const env_device_pixels = env && env[0] && env[0] != '0';
            if( cfg.hidpi < 0 && !env_device_pixels )
                App_SetHighDpiAuto(&app, CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS);
        }
#endif
#endif
        /*
         * The launch's renderer, from the flags. Each flag was refused at parse
         * time where this build cannot honour it, so the #ifs only keep the
         * other lanes' flags from being unused variables.
         */
        renderer_gles2_dualcore = gles2_dualcore != 0;
        renderer_launch_flagged = renderer_flag != 0;
        renderer_launch = TORIRS_RENDERER_KIND_SOFTWARE;
        if( use_webgl2 )
            renderer_launch =
                webgl2_zbuffer ? TORIRS_RENDERER_KIND_WEBGL2_DEPTH : TORIRS_RENDERER_KIND_WEBGL2;
        else if( use_webgl1 )
            renderer_launch =
                webgl1_zbuffer ? TORIRS_RENDERER_KIND_WEBGL1_DEPTH : TORIRS_RENDERER_KIND_WEBGL1;
        else if( use_gles3 )
            renderer_launch =
                gles3_zbuffer ? TORIRS_RENDERER_KIND_GLES3_DEPTH : TORIRS_RENDERER_KIND_GLES3;
        else if( use_gles2 )
            renderer_launch =
                gles2_zbuffer ? TORIRS_RENDERER_KIND_GLES2_DEPTH : TORIRS_RENDERER_KIND_GLES2;
        else if( use_opengl3 )
            renderer_launch =
                gl3_zbuffer ? TORIRS_RENDERER_KIND_OPENGL3_DEPTH : TORIRS_RENDERER_KIND_OPENGL3;
        else if( use_d3d9 )
            renderer_launch =
                d3d9_zbuffer ? TORIRS_RENDERER_KIND_D3D9_DEPTH : TORIRS_RENDERER_KIND_D3D9;
        assert(renderer_built(renderer_launch));

        if( renderer_present(renderer_launch) == PLATFORM_PRESENT_GL
                ? !PlatformWindow_InitForOpenGL3(
                      platform, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, title)
                : !PlatformWindow_Init(platform, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, title) )
        {
            TORIRS_ERR("window init failed\n");
            PlatformWindow_Free(platform);
            App_Shutdown(&app);
            return 1;
        }
        if( !PlatformWindow_SetPresent(platform, renderer_present(renderer_launch)) ||
            !renderer_start(renderer_launch) )
        {
            /* D3D9 is optional on the XP lane: GDI Soft3D draws the same
             * frame on any machine. A GL lane that cannot start its renderer
             * is a failed launch, as it always was -- the flag asked for it. */
            if( renderer_present(renderer_launch) != PLATFORM_PRESENT_NATIVE )
            {
                PlatformWindow_Free(platform);
                App_Shutdown(&app);
                return 1;
            }
            TORIRS_ERR("renderer: falling back to GDI Soft3D\n");
            renderer_launch = TORIRS_RENDERER_KIND_SOFTWARE;
            {
                bool const software = PlatformWindow_SetPresent(platform, PLATFORM_PRESENT_SOFTWARE) &&
                                      renderer_start(TORIRS_RENDERER_KIND_SOFTWARE);
                assert(software);
                (void)software;
            }
        }
        renderer_active = renderer_launch;
        renderer_publish();
        /* REPORT: which renderer a run drew with is the first question about
         * any frame it produced, and optimized builds compile TORIRS_LOG out. */
        TORIRS_REPORT(
            "renderer: %d at launch (available 0x%x)\n",
            (int)renderer_active,
            renderer_available());

        /*
         * Whether this device has keys to summon, asked once and held.
         *
         * Here because the question needs the platform handle and App_Init has
         * none -- the App is deliberately platform-free. Before the frame
         * loop, and that is the point: PluginHost_Start runs from a task the
         * loop pumps, so a capability resolved any later is false at every
         * on_start and true from frame one, with nobody left listening. That
         * is the exact defect touch_ui was moved out of this file to fix; @see
         * the note beside app.touch_camera above, and App::touch_ui.
         */
        app.has_screen_keyboard = PlatformWindow_HasScreenKeyboard(platform);
        TORIRS_LOG("screen_keyboard: %d\n", app.has_screen_keyboard);

        /*
         * The other half of the touch viewport published every frame below:
         * which points inside it are covered by a window.
         *
         * Registered once -- the App outlives the platform here, and the
         * predicate reads live state, so there is nothing to keep current.
         */
        PlatformWindow_SetTouchOverlayTest(platform, touch_overlay_owns_point, &app);

        /*
         * Choose the plugin window's presentation.
         *
         * Here rather than in the App because choosing needs the platform handle,
         * and the App is deliberately platform-free -- App_Render is handed a pixel
         * buffer rather than a window for the same reason. What crosses is a
         * vtable, not a started executor: the App brings it up the first time the
         * plugin window is opened, so a session that never opens it never opens a
         * second OS window either.
         *
         * TORIRS_CHROME_EXECUTOR names one (`web` or `browser`), beside
         * TORIRS_CHROME_THEME which the developer chrome already reads. An unknown
         * name, or one this build has no executor for, lands on the in-canvas
         * chrome -- which is what every lane without a native executor uses anyway.
         */
        {
            /* The manifest says which, the env var overrides it -- the same
             * precedence TORIRS_CHROME_THEME has over the theme beside it, and
             * what lets a lane ship a default a developer can step past
             * without editing it. */
            char const* want = getenv("TORIRS_CHROME_EXECUTOR");
            int wanted = boot_manifest.chrome_executor;
            int chosen = boot_manifest.chrome_executor_set;
            int got = TORIRS_CHROME_EXEC_BUFFER;
            struct ToriRSChromeExec chrome_exec;

            if( want && want[0] )
            {
                int const from_env = ToriRSChromeExec_KindFromName(want);
                if( from_env < 0 )
                    TORIRS_LOG(
                        "chrome: '%s' is not an executor "
                        "(web|browser); using the build default\n",
                        want);
                else
                {
                    wanted = from_env;
                    chosen = 1;
                }
            }
            /* No configured choice means the supported web presenter compiled
             * for this build. A negative request is internal factory syntax,
             * not a parseable `platform` pseudo-executor. */
            if( !chosen )
                wanted = -1;
            chrome_exec = ToriRSChromeExec_ForKind(wanted, platform, &got);
            if( chosen && got != wanted )
                TORIRS_LOG(
                    "chrome: no '%s' executor in this build; the plugin window stays in the "
                    "canvas\n",
                    ToriRSChromeExec_KindName(wanted));
            App_SetPluginChromeExec(&app, &chrome_exec, got, chosen);
        }
        /* Where plugin destinations are offered: the manifest says, and
         * TORIRS_PLUGIN_NAV overrides it the way TORIRS_CHROME_EXECUTOR
         * overrides the executor above. */
        {
            char const* want = getenv("TORIRS_PLUGIN_NAV");
            int mode = boot_manifest.plugin_nav;
            if( want && want[0] )
            {
                int const from_env = ToriRSPluginNav_ModeFromName(want);
                if( from_env < 0 )
                    TORIRS_LOG("chrome: TORIRS_PLUGIN_NAV must be auto|rail, got '%s'\n", want);
                else
                    mode = from_env;
            }
            App_SetPluginNavMode(&app, mode);
        }

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
            int density = PlatformWindow_PixelDensity(platform);
            char const* forced = torirs_env_chrome_scale();
            /* Precedence: the env pin (a dev working on scaled chrome from a
             * 1x display), then the manifest's stated size, then the display
             * itself. The manifest slot is what lets a boot say "this editor
             * draws its chrome at 2x" without every launch exporting an env. */
            if( forced && forced[0] )
                density = atoi(forced);
            else if( cfg.chrome_scale > 0 )
                density = cfg.chrome_scale;
            else if( cfg.chrome_scale < 0 )
                /* dynamic: proportional to the canvas, 500 POINTS per step,
                 * times the density -- the classic 503-row frame is one step,
                 * a window twice that is two, and each step is drawn at the
                 * display's own resolution. */
                density = main_dynamic_chrome_scale(UITREE_LAYOUT_ROOT_H, density);
            App_SetChromeScale(&app, density);
            if( getenv("TORIRS_RESIZE_DEBUG") )
                TORIRS_LOG(
                    "chrome: scale %d (display density %d)\n",
                    App_ChromeScale(&app),
                    PlatformWindow_PixelDensity(platform));
        }

        {
            int const boot_mode = App_WindowMode(&app);
            bool const resizable = boot_mode == CS2VM_WINDOW_MODE_RESIZABLE;
            /* A scale restored from preferences is already in the host here. */
            PlatformWindow_SetCanvasFollowsWindow(
                platform,
                &bus,
                resizable,
                resizable ? APP_CANVAS_MIN_W : App_FixedWindowWidth(&app),
                resizable ? APP_CANVAS_MIN_H : App_FixedWindowHeight(&app));
            if( !resizable )
                CmdBus_PushWindowResize(&bus, APP_CANVAS_MIN_W, APP_CANVAS_MIN_H);
            if( getenv("TORIRS_RESIZE_DEBUG") )
                TORIRS_LOG("windowmode: boot %s\n", CS2VM_WindowModeName(boot_mode));
        }

        /* Audio backend. Opening a device is allowed to fail — a machine with no
         * sound card, or a headless CI box, keeps running silently rather than
         * refusing to start, which is the same courtesy the renderer gets. */
        audio = PlatformAudio_New();
        if( !PlatformAudio_Init(audio, TORIRS_AUDIO_SAMPLE_RATE) )
        {
            TORIRS_LOG("audio: no device; running silent\n");
            /* Silence is free: without this the game still decodes every clip
             * and synthesises every music frame, then hands it to a backend
             * that drops it. */
            App_SetAudioDevicePresent(&app, false);
        }
        /*
         * The music player is a generator the mixer pulls, so on a backend with
         * a device thread the render reaches into its synth. Hand it that
         * thread's lock: without this every song change races the render.
         * Zeroed, and free, on the backends that render from the frame loop.
         */
        ToriRS_Music_SetExclusion(&app.audio.music, PlatformAudio_Exclusion(audio));
        /* And size its synth for that backend's block, so the first song does
         * not grow the accumulator on the device thread. */
        ToriRS_Music_Reserve(&app.audio.music, PlatformAudio_BlockFrames(audio));

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
            int fields = sscanf(
                getenv("TORIRS_SIM_SOUND"), "%d,%d,%d", &parsed_id, &parsed_loops, &parsed_every);
            if( fields >= 1 && parsed_id >= 0 )
            {
                sim_sound_id = parsed_id;
                sim_sound_loops = fields >= 2 && parsed_loops > 0 ? parsed_loops : 1;
                sim_sound_every = fields >= 3 && parsed_every > 0 ? parsed_every : 0;
            }
            else
            {
                TORIRS_ERR("TORIRS_SIM_SOUND: expected id[,loops[,every_frames]]\n");
            }
        }

        /* TORIRS_CMD_RECORD=file: tee every pushed command to a replayable
         * .trscmd file. TORIRS_CMD_REPLAY=file: drive the loop from a prior
         * recording instead of SDL events, timestamps included. */
        if( getenv("TORIRS_CMD_RECORD") )
        {
            if( !CmdBus_RecordOpen(&bus, getenv("TORIRS_CMD_RECORD")) )
                TORIRS_ERR("cmdbus: cannot record to %s\n", getenv("TORIRS_CMD_RECORD"));
        }
        if( getenv("TORIRS_CMD_REPLAY") )
        {
            replay = CmdReplay_Open(getenv("TORIRS_CMD_REPLAY"));
            if( !replay )
            {
                TORIRS_ERR("cmdbus: cannot replay %s\n", getenv("TORIRS_CMD_REPLAY"));
                PlatformWindow_Free(platform);
                App_Shutdown(&app);
                return 1;
            }
        }

        input = LibToriRS_Input_Init(&input_storage, PlatformWindow_Ticks64());

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
                    line, sizeof(line), "Seed chat line %ld — scroll container text check", i + 1);
                RS_CS2Host_ChatAdd(&app.host, RS_CHAT_TYPE_GAME, NULL, NULL, line);
            }
        }

        interactive_render_present(
                &app, platform, gl3, d3d9, gles2, webgl2, webgl1, gles3,
                renderer_active_is_depth());

        /* TORIRS_MAX_FRAMES=N: exit after N loop iterations (headless smoke
         * runs under SDL_VIDEODRIVER=dummy, where no quit event ever comes). */
        max_frames = getenv("TORIRS_MAX_FRAMES") ? atol(getenv("TORIRS_MAX_FRAMES")) : 0;
        sim_after_ready =
            getenv("TORIRS_SIM_AFTER_READY") && atoi(getenv("TORIRS_SIM_AFTER_READY"));
        sim_ready = sim_ready_failed = 0;
        sim_ready_start_ms = PlatformWindow_Ticks64();
        sim_next_frame_ms = 0;
        frame_count = sim_after_ready ? -1 : 0;
        {
            /* The logic pacer needs this too: a bounded run ticks once per
             * frame rather than on the wall clock, so `clientclock` lands on
             * the same cycle every run and an emit dump is reproducible. */
            extern long g_torirs_max_frames;
            /* A readiness-based native scenario uses the ordinary logic clock.
             * Only its synthetic actions and exit fence use scenario ticks. */
            g_torirs_max_frames = sim_after_ready ? 0 : max_frames;
        }

        /* TORIRS_PACE_SPIN=1: spin the 50 fps wait rather than sleeping it. */
        pace_spin = getenv("TORIRS_PACE_SPIN") && atoi(getenv("TORIRS_PACE_SPIN")) != 0;

        ToriRS_Pacer_Init(
            &frame_pacer, pacer_kind_selected(), frame_period_ms(), pacer_mindel_ms());
        /* REPORT, not LOG: TORIRS_LOG compiles out under NDEBUG, which is
         * exactly the optimized build every measurement is taken on. An arm
         * that cannot say which pacer it ran is not a result. */
        TORIRS_REPORT(
            "pacer: %s (period %d ms, mindel %d ms)\n",
            ToriRS_Pacer_KindName(frame_pacer.kind),
            frame_period_ms(),
            pacer_mindel_ms());

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
            const char* transport_name =
                boot_manifest.transport[0] ? boot_manifest.transport : NULL;
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
                    TORIRS_ERR(
                        "torirs: unknown transport=%s — using the revision's\n", transport_name);
            }

            /* One process must not quietly use two caches. The client and its
             * JS5 reader already use the manifest-resolved `cfg.cache_dir`; pass
             * that same directory through ToriRSServer's existing deployment knob
             * before the embedded world starts. An explicit TORIRSSERVER_CACHE still
             * wins, which preserves the diagnostic override. This matters for
             * isolated cache overlays: their minted npc/loc ids do not exist in
             * the pristine cache. */
            if( transport_kind == NET_TRANSPORT_EMBED && !getenv("TORIRSSERVER_CACHE") )
                setenv("TORIRSSERVER_CACHE", cfg.cache_dir, 0);
            if( transport_kind == NET_TRANSPORT_EMBED && cfg.net_server_scripts &&
                cfg.net_server_scripts[0] && !getenv("TORIRSSERVER_SCRIPTS") )
                setenv("TORIRSSERVER_SCRIPTS", cfg.net_server_scripts, 0);

            sock = app.net ? NetTransport_New(
                                 transport_kind,
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
        boot_start_ms = PlatformWindow_Ticks64();
        boot_reported = 0;
        world_reported = 0;
        ToriRS_BootTelemetry_Mark("frame_loop");

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
#if defined(TORIRS_PLATFORM_WEB)
        /* Before the unwind, not after: there is no "after". */
        web_announce_ready();
#endif
        web_pump_armed = 1;
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
    /*
     * The last flush. stderr is fully buffered (see the setvbuf above) and the
     * per-frame flush lives inside the loop, so everything the shutdown path
     * writes -- the audio ledger, the perf summary, the leak counts -- sits in
     * the buffer when main returns. On a host that exits, the C runtime flushes
     * it. Android does not exit: the frame thread returns into JNI and the
     * process goes on living, and the whole teardown report was being dropped
     * on the one lane where it is the only way to see it.
     */
    fflush(stderr);
    return sim_ready_failed ? 1 : 0;
}
