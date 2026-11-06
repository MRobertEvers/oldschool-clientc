/**
 * The interface editor's and CS2 tools' registration with the editor's panel
 * registry (editor/editor_ui.h): one shared EditorIfacePanels behind two
 * registry entries, "iface" and "cs2".
 *
 * The registry hands out one activation and one event queue per frame; the
 * "iface" entry's activate and event cover every widget of both, so the
 * "cs2" entry carries only what the View menu needs (its chrome panel) and
 * its visibility. Whichever registers first owns the tick.
 *
 * App-bound (like the catalog, inspector and locs modules): with `ui->app`
 * set, each tick also runs AppIfaceEditor_Service, which is where a mount, a
 * run, a var write and the refill reach the client.
 */

#include "editor/iface/iface_panels.h"

#include "app/app_iface_editor.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanels.h"
#include "torirsmaped/torirs_maped_build.h"
#include "mem/torirs_mem.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <string.h>

struct EditorIfaceModule
{
    /** First, so a registry self is also the panels. */
    struct EditorIfacePanels panels;
    /** Registry entries pointing at this state; freed at the last. */
    int owners;
    /** Compile's CS2 job (workbench 6.6). */
    struct Editor_BuildJob compile_job;
};

static void
module_compile_line(
    void* user,
    const char* text)
{
    struct EditorIfacePanels* panels = user;
    EditorCs2_CompileTakeLine(panels->compile, text);
}

/** Compile's job: sent when the panel asked, its lines into the result as
 *  they arrive, the result shown at its end. Every frame, shown or not. */
static void
module_compile(
    struct EditorUi* ui,
    struct EditorIfaceModule* module)
{
    struct EditorIfacePanels* panels = &module->panels;

    if( panels->compile_pending )
    {
        char why[160];
        panels->compile_pending = 0;
        if( strncmp(panels->code_key, "cs2:", 4) != 0
            || !Editor_BuildJobSend(ui, &module->compile_job, TORIRSMAPED_BUILD_CS2, panels->code_key + 4, why,
                sizeof(why)) )
        {
            if( strncmp(panels->code_key, "cs2:", 4) != 0 )
                snprintf(why, sizeof(why), "the CODE view holds no cs2 script");
            EditorCs2_CompileTakeLine(panels->compile, why);
            EditorIfacePanels_ShowCompile(panels, ui->chrome);
            Editor_UiStatus(ui, "Compile refused: %s", why);
            return;
        }
        panels->compile_running = 1;
    }
    if( !panels->compile_running )
        return;
    if( !Editor_BuildJobPoll(ui, &module->compile_job, module_compile_line, panels) )
        return;
    panels->compile_running = 0;
    if( module->compile_job.status != EDITOR_HOST_OK && panels->compile->line_count == 0 )
    {
        /* The server never ran the compiler: say why. */
        EditorCs2_CompileTakeLine(panels->compile, Editor_BuildJobWhy(module->compile_job.status));
        EditorIfacePanels_ShowCompile(panels, ui->chrome);
        Editor_UiStatus(ui, "Compile refused: %s", Editor_BuildJobWhy(module->compile_job.status));
        return;
    }
    EditorCs2_CompileEnd(panels->compile, module->compile_job.status == EDITOR_HOST_OK,
        module->compile_job.status == EDITOR_HOST_OK ? 0 : 1, "");
    EditorIfacePanels_ShowCompile(panels, ui->chrome);
    Editor_UiStatus(ui, "cs2 compile %s: compiled %d, failed %d, %d declined (%.1f s)", panels->code_key + 4,
        panels->compile->compiled, panels->compile->failed, panels->compile->decline_count,
        module->compile_job.seconds);
}

struct EditorIfacePanels*
Editor_Iface_PanelsOf(void* self)
{
    assert(self);
    return &((struct EditorIfaceModule*)self)->panels;
}

static void
module_tick(
    struct EditorUi* ui,
    void* self)
{
    struct EditorIfaceModule* module = self;
    struct EditorIfacePanels* panels = &module->panels;
    int iface_visible;
    int cs2_visible;

    assert(ui);
    assert(module);

    /* The companions follow the Interface panel, which the View menu toggles
     * without telling anyone. */
    module_compile(ui, module);
    iface_visible = ui->chrome->panels[panels->panel_iface].visible;
    cs2_visible = ui->chrome->panels[panels->panel_cs2].visible;
    if( iface_visible != panels->shown )
    {
        EditorIfacePanels_ShowInterface(panels, ui->chrome, iface_visible);
        panels->shown = iface_visible;
    }
    if( !iface_visible && !cs2_visible )
        return;
    EditorIfacePanels_Tick(panels, ui->chrome, ui->editor);
    if( ui->app )
        AppIfaceEditor_Service(ui->app, panels, ui->chrome, ui->editor);
}

static int
module_activate(
    struct EditorUi* ui,
    void* self,
    int widget)
{
    struct EditorIfaceModule* module = self;

    assert(ui);
    assert(module);
    return EditorIfacePanels_OnActivate(&module->panels, ui->chrome, ui->editor, widget);
}

static int
module_event(
    struct EditorUi* ui,
    void* self,
    const struct ToriRSChromeEvent* event)
{
    struct EditorIfaceModule* module = self;

    assert(ui);
    assert(module);
    return EditorIfacePanels_OnEvent(&module->panels, ui->chrome, ui->editor, event);
}

/** The editor opened or closed: the panels start (and stay) hidden until the
 *  View menu opens them; closing the editor closes them. */
static void
iface_set_visible(
    struct EditorUi* ui,
    void* self,
    int visible)
{
    struct EditorIfaceModule* module = self;

    (void)visible;
    assert(ui);
    assert(module);
    EditorIfacePanels_ShowInterface(&module->panels, ui->chrome, 0);
    module->panels.shown = 0;
}

static void
cs2_set_visible(
    struct EditorUi* ui,
    void* self,
    int visible)
{
    struct EditorIfaceModule* module = self;

    (void)visible;
    assert(ui);
    assert(module);
    EditorIfacePanels_ShowCs2(&module->panels, ui->chrome, 0);
}

static void
module_destroy(
    struct EditorUi* ui,
    void* self)
{
    struct EditorIfaceModule* module = self;

    (void)ui;
    if( !module )
        return;
    if( --module->owners > 0 )
        return;
    AppIfaceEditor_ServiceFree(&module->panels);
    EditorIfacePanels_Free(&module->panels);
    ToriMem_Free(ToriMem_Editor(), module);
}

/** The shared state, made (built, configured) on the first registration. */
static struct EditorIfaceModule*
module_get(
    struct EditorUi* ui,
    int* out_created)
{
    struct EditorIfaceModule* module = Editor_UiPanelSelf(ui, "iface");

    if( !module )
        module = Editor_UiPanelSelf(ui, "cs2");
    *out_created = module == NULL;
    if( module )
        return module;
    module = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*module));
    assert(module);
    EditorIfacePanels_Init(&module->panels);
    EditorIfacePanels_Build(&module->panels, ui->chrome);
    if( ui->app )
        AppIfaceEditor_Configure(ui->app, &module->panels);
    return module;
}

void
Editor_Iface_Register(struct EditorUi* ui)
{
    struct EditorUiPanel desc;
    int created;
    struct EditorIfaceModule* module;

    assert(ui);
    assert(ui->chrome);
    module = module_get(ui, &created);
    module->owners++;

    memset(&desc, 0, sizeof(desc));
    desc.name = "iface";
    desc.title = "Interface editor";
    desc.self = module;
    desc.chrome_panel = module->panels.panel_iface;
    desc.tick = created ? module_tick : NULL;
    desc.activate = module_activate;
    desc.event = module_event;
    desc.set_visible = iface_set_visible;
    desc.destroy = module_destroy;
    Editor_UiAddPanel(ui, &desc);
}

void
Editor_Cs2_Register(struct EditorUi* ui)
{
    struct EditorUiPanel desc;
    int created;
    struct EditorIfaceModule* module;

    assert(ui);
    assert(ui->chrome);
    module = module_get(ui, &created);
    module->owners++;

    memset(&desc, 0, sizeof(desc));
    desc.name = "cs2";
    desc.title = "CS2";
    desc.self = module;
    desc.chrome_panel = module->panels.panel_cs2;
    /* One activate covers both entries' widgets; only the first entry runs
     * the tick. A cs2 registered alone takes both. */
    desc.tick = created ? module_tick : NULL;
    desc.activate = created ? module_activate : NULL;
    desc.event = created ? module_event : NULL;
    desc.set_visible = cs2_set_visible;
    desc.destroy = module_destroy;
    Editor_UiAddPanel(ui, &desc);
}
