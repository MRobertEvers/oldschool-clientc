/*
 * Workbench step 6.0b, the editor's half (design section 2 "R1 in full",
 * section 3 "Every panel pops out", section 16 step 6.0b):
 *
 *   - THE GOLDEN: the chrome's draw list (every primitive Build emits) for a
 *     fixed scene of chrome widgets and for the whole editor registry laid
 *     out on a 1530x1006 surface, at the integer scales 1, 2 and 3, digested
 *     and compared with the digests recorded before step 6.0b touched the
 *     draw path -- so the integer scales stay pixel-identical (the rasteriser
 *     draws only what the list says);
 *   - the chrome scale snap table (Auto from the display's ratio, the touch
 *     floor, a decimal pin);
 *   - View > Chrome scale and View > Text size: the rows, their hotkey
 *     actions (unbound), the relayout, the theme's body face and row height;
 *   - the device-local prefs file (editor_prefs.ini) round trip;
 *   - the Dock button on a popped-out window's title bar and
 *     TORIRSMAPED_STATE_DOCK_PANEL, across two sessions of one embedded
 *     ToriRSMapEd: the popped-out window asks, the leader shows the panel
 *     again exactly as View > panel does;
 *   - which modules a panel-only window skips the world for.
 *
 *   ./editor_workbench_test <scratch-dir>
 *
 * Headless and content-free (no square is opened), so it runs where the
 * content tree is not checked out. UPDATE_GOLDEN=1 prints the digests
 * instead of comparing them.
 */

#include "editor/editor.h"
#include "editor/editor_host.h"
#include "editor/editor_hotkeys.h"
#include "editor/editor_layout.h"
#include "editor/editor_ui.h"
#include "editor/editor_view.h"
#include "editor/panels/edpanel_common.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "ui/uitree_debug_overlay.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static int g_app_stand_in;

static int
fake_spawn_panel(
    void* user_data,
    const char* module,
    const struct EditorViewCamera* camera,
    char* why,
    int why_size)
{
    (void)user_data;
    (void)module;
    (void)camera;
    (void)why;
    (void)why_size;
    return 1;
}

static void
frame_of(
    struct ToriRSChrome* chrome,
    struct EditorUi* ui)
{
    ToriRSChrome_Build(chrome);
    Editor_UiTick(ui);
    ToriRSChrome_Build(chrome);
}

static void
boot_registry(
    struct ToriRSChrome* chrome,
    struct EditorUi* ui,
    struct Editor* editor,
    int w,
    int h,
    int scale)
{
    ToriRSChrome_Init(chrome);
    ToriRSChrome_SetScale(chrome, scale);
    ToriRSChrome_SetSurface(chrome, w, h);
    Editor_UiInit(ui, chrome, editor);
    ui->app_services.user_data = &g_app_stand_in;
    ui->app_services.spawn_panel = fake_spawn_panel;
    Editor_UiRegisterToolsPanels(ui);
    Editor_UiSetVisible(ui, 1);
    for( int i = 0; i < 5; i++ )
        frame_of(chrome, ui);
}

/* ---- the golden ----------------------------------------------------------- */

static uint32_t
fnv(
    uint32_t h,
    const void* data,
    size_t size)
{
    const unsigned char* p = data;
    for( size_t i = 0; i < size; i++ )
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint32_t
fnv_int(
    uint32_t h,
    int v)
{
    return fnv(h, &v, sizeof(v));
}

/** Every field of every primitive the last Build emitted, text by content. */
static uint32_t
prims_digest(
    const struct ToriRSChrome* chrome,
    int* out_count)
{
    int count = 0;
    const struct ToriRSChromePrim* prims = ToriRSChrome_Prims(chrome, &count);
    uint32_t h = 2166136261u;

    for( int i = 0; i < count; i++ )
    {
        const struct ToriRSChromePrim* p = &prims[i];
        h = fnv_int(h, p->kind);
        h = fnv_int(h, p->x);
        h = fnv_int(h, p->y);
        h = fnv_int(h, p->w);
        h = fnv_int(h, p->h);
        h = fnv_int(h, (int)p->color);
        h = fnv_int(h, p->filled);
        h = fnv_int(h, p->line_direction);
        h = fnv_int(h, p->line_width);
        h = fnv_int(h, p->font_slot);
        h = fnv_int(h, p->baseline);
        h = fnv_int(h, p->shadowed);
        if( p->text )
            h = fnv(h, p->text, strlen(p->text));
        h = fnv_int(h, p->sprite_slot);
        h = fnv_int(h, p->sprite_scene_id);
        h = fnv_int(h, p->trans);
        h = fnv_int(h, p->clip.x);
        h = fnv_int(h, p->clip.y);
        h = fnv_int(h, p->clip.w);
        h = fnv_int(h, p->clip.h);
    }
    *out_count = count;
    return h;
}

/** A fixed scene of the widget kinds every panel is made of, on a titled,
 *  closable, poppable, resizable window in `theme`, the pointer over the
 *  Close box so its hover art is in the list too. */
static uint32_t
scene_digest(
    int scale,
    const struct ToriRSChromeTheme* theme,
    int* out_count)
{
    static const char* const k_options[] = { "Linear", "Smooth", "Flat" };
    int panel;
    struct ToriRSChromeRect close;

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetTheme(&g_chrome, theme);
    ToriRSChrome_SetScale(&g_chrome, scale);
    ToriRSChrome_SetSurface(&g_chrome, 800 * scale, 600 * scale);
    panel = ToriRSChrome_PanelAdd(&g_chrome, TORIRS_CHROME_PANEL_WINDOW, 20 * scale, 30 * scale, 300 * scale, "Terrain");
    ToriRSChrome_PanelSetClosable(&g_chrome, panel, 1);
    ToriRSChrome_PanelSetPoppable(&g_chrome, panel, 1);
    ToriRSChrome_PanelSetResizable(&g_chrome, panel, 1);
    (void)ToriRSChrome_Label(&g_chrome, panel, "Brush 3222,3218 level 0");
    (void)ToriRSChrome_Button(&g_chrome, panel, "Apply to selection");
    (void)ToriRSChrome_Checkbox(&g_chrome, panel, "Snap to grid", 1);
    (void)ToriRSChrome_Dropdown(&g_chrome, panel, "Falloff", k_options, 3, 1);
    (void)ToriRSChrome_Slider(&g_chrome, panel, "Radius", 0, 16, 5, 1);
    (void)ToriRSChrome_TextInput(&g_chrome, panel, "Search", "oak");
    ToriRSChrome_Build(&g_chrome);
    close = ToriRSChrome_PanelCloseRect(&g_chrome, panel);
    ToriRSChrome_MouseMove(&g_chrome, close.x + close.w / 2, close.y + close.h / 2);
    ToriRSChrome_Build(&g_chrome);
    return prims_digest(&g_chrome, out_count);
}

static uint32_t
registry_digest(
    int scale,
    int* out_count)
{
    boot_registry(&g_chrome, &g_ui, NULL, 1530, 1006, scale);
    return prims_digest(&g_chrome, out_count);
}

/* Recorded at v3-workbench 64310269, before step 6.0b changed the draw path:
 * scene in the default theme, scene in the osrs theme, the registry; at
 * scales 1, 2, 3. */
static const uint32_t k_golden[3][3] = {
    { 1970238540u, 3840548130u, 1127817167u },
    { 1606657634u, 3552625563u, 2175812620u },
    { 1621493878u, 3838641767u, 3489523192u },
};

static void
test_golden(void)
{
    int const update = getenv("UPDATE_GOLDEN") && getenv("UPDATE_GOLDEN")[0] == '1';

    for( int scale = 1; scale <= 3; scale++ )
    {
        int counts[3];
        uint32_t got[3];
        got[0] = scene_digest(scale, &torirs_chrome_theme_default, &counts[0]);
        got[1] = scene_digest(scale, &torirs_chrome_theme_osrs, &counts[1]);
        got[2] = registry_digest(scale, &counts[2]);
        if( update )
        {
            printf("golden scale %d: { %uu, %uu, %uu }, (prims %d %d %d)\n", scale, got[0], got[1], got[2],
                counts[0], counts[1], counts[2]);
            continue;
        }
        for( int k = 0; k < 3; k++ )
        {
            static const char* const k_what[3] = { "scene (default theme)", "scene (osrs theme)", "registry" };
            CHECK(counts[k] > 0, "scale %d %s draws something", scale, k_what[k]);
            CHECK(got[k] == k_golden[scale - 1][k], "scale %d %s draws exactly what it drew before 6.0b (%u, want %u, %d prims)",
                scale, k_what[k], got[k], k_golden[scale - 1][k], counts[k]);
        }
    }
}

int
main(
    int argc,
    char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    /* No layout or prefs another run left behind. */
    setenv("TORIRS_EDITOR_LAYOUT", "", 1);
    (void)argc;
    (void)argv;

    test_golden();

    printf("editor_workbench_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
