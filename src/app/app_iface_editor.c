#include "app/app_iface_editor.h"

#include "app/app_internal.h"
#include "datatypes/dat2_component.h"
#include "editor/editor_doc.h"
#include "editor/editor_if.h"
#include "editor/editor_panel.h"
#include "editor/editor_ui.h"
#include "editor/iface/iface_panels.h"
#include "engine/cache_provider.h"
#include "engine/torirs_component_from_rscache.h"
#include "engine/uitree_from_component.h"
#include "game/task_cs2_run.h"
#include "mem/torirs_mem.h"
#include "rscache_alloc.h"
#include "task_runner.h"
#include "ui/uitree.h"
#include "ui/uitree_build.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_input.h"
#include "varc/varc_manager.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** What the App half keeps between frames (EditorIfacePanels.app_state). */
struct AppIfaceState
{
    /** The IFACE text last pushed to the provider and the tree. Owned. */
    char* mounted_text;
    size_t mounted_length;
    char mounted_key[EDITOR_DOC_KEY_MAX];
    /** The mount to repeat when a change needs one; kind 0 = nothing mounted. */
    struct EditorIfaceRequest mount;
    int pick_armed;
    /** The serial runner decrements this as the run task leaves its queue. */
    int run_join;
    int running;
    int running_script;
    int running_traced;
};

static struct AppIfaceState*
state_of(struct EditorIfacePanels* panels)
{
    if( !panels->app_state )
    {
        panels->app_state = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(struct AppIfaceState));
        assert(panels->app_state);
    }
    return panels->app_state;
}

void
AppIfaceEditor_ServiceFree(struct EditorIfacePanels* panels)
{
    struct AppIfaceState* state;

    if( !panels || !panels->app_state )
        return;
    state = panels->app_state;
    ToriMem_Free(ToriMem_Editor(), state->mounted_text);
    ToriMem_Free(ToriMem_Editor(), state);
    panels->app_state = NULL;
}

struct EditorIfacePanels*
AppIfaceEditor_Panels(struct App* app)
{
    void* self;

    assert(app);
    if( !app->editor_panel.ui )
        return NULL;
    self = Editor_UiPanelSelf(app->editor_panel.ui, "iface");
    if( !self )
        self = Editor_UiPanelSelf(app->editor_panel.ui, "cs2");
    return self ? Editor_Iface_PanelsOf(self) : NULL;
}

void
AppIfaceEditor_Configure(
    struct App* app,
    struct EditorIfacePanels* panels)
{
    char cs2_bin[1024] = "";
    char names[1024] = "";
    char work[1024];
    const char* env_names = getenv("TORIRS_CS2_NAMES");
    const char* home = getenv("HOME");
    const char* tmp = getenv("TMPDIR");

    assert(app);
    assert(panels);
    if( app->cfg.editor_repo_root )
        snprintf(cs2_bin, sizeof(cs2_bin), "%s/3rd/rscache/tools/cs2/cs2", app->cfg.editor_repo_root);
    if( env_names )
        snprintf(names, sizeof(names), "%s", env_names);
    else if( home )
    {
        FILE* probe;
        snprintf(names, sizeof(names), "%s/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2", home);
        /* Only when it is there: --names on a missing directory is a failed
         * compile for a reason that has nothing to do with the script. */
        probe = fopen(names, "r");
        if( probe )
            fclose(probe);
        else
            names[0] = '\0';
    }
    snprintf(work, sizeof(work), "%s/torirs_iface_editor", tmp ? tmp : "/tmp");
    EditorIfacePanels_Configure(
        panels, app->cfg.editor_content_dir ? app->cfg.editor_content_dir : "",
        app->cfg.cache_dir ? app->cfg.cache_dir : "", cs2_bin, names, work);
}

void
AppIfaceEditor_Open(
    struct App* app,
    const char* interface_name)
{
    struct EditorIfacePanels* panels;

    assert(app);
    panels = AppIfaceEditor_Panels(app);
    if( !panels )
        return;
    EditorIfacePanels_ShowInterface(panels, &app->dbg_ui, 1);
    panels->shown = 1;
    if( interface_name && interface_name[0] )
        EditorIfacePanels_OpenInterface(panels, &app->dbg_ui, app->editor, interface_name);
}

/* ---- the document as a component pack ------------------------------------ */

/** Every component type the client's converter maps (torirs_types.h). */
static int
type_mountable(int type)
{
    return (type >= 0 && type <= 7) || type == 9;
}

/**
 * The IFACE text as the ToriRS_ComponentPack the cache would have given for
 * it: every block through the typed layer, then the client's own converter.
 * NULL when a block does not read or names a type the converter does not
 * map (it would abort on it), with `why` saying which.
 */
static struct ToriRS_ComponentPack*
pack_from_text(
    const char* text,
    size_t length,
    char* why,
    size_t why_size)
{
    struct Editor_IfFile file;
    struct RSCache_Dat2ComponentPack* source;
    struct ToriRS_ComponentPack* pack = NULL;
    int count;
    int ok = 1;

    Editor_IfIndex(&file, text, length);
    count = Editor_IfComponentCount(&file);
    if( count == 0 || file.interface_id < 0 )
    {
        snprintf(why, why_size, "no components, or no interface id in the preamble");
        Editor_IfFree(&file);
        return NULL;
    }
    source = RSCACHE_CALLOC(1, sizeof(*source));
    assert(source);
    source->components = RSCACHE_CALLOC((size_t)count, sizeof(*source->components));
    assert(source->components);
    source->component_count = count;
    for( int i = 0; i < count && ok; i++ )
    {
        char bad[64] = "";
        char empty[8];
        struct RSCache_Dat2Component* component = RSCACHE_MALLOC(sizeof(*component));

        assert(component);
        source->components[i] = component;
        if( !Editor_IfBlockReadComponent(&file, i, component, bad, sizeof(bad)) )
        {
            snprintf(why, why_size, "component %d: %s does not read", i, bad);
            ok = 0;
        }
        else if( Editor_IfBlockValue(&file, i, "empty", empty, sizeof(empty)) == 1 )
        {
            /* A zero-length member: a hidden layer holds its id. */
            component->type = 0;
            component->hidden = true;
        }
        else if( !type_mountable(component->type) )
        {
            snprintf(why, why_size, "component %d: type %d is not one the client maps", i, component->type);
            ok = 0;
        }
    }
    Editor_IfFree(&file);
    if( ok )
        pack = ToriRS_ComponentPackFromRSCacheDat2(source);
    RSCache_Dat2ComponentPackFree(source);
    if( ok && !pack )
        snprintf(why, why_size, "the converter made no pack");
    return pack;
}

/** Put the document's pack where the interface open will look first. */
static int
publish_pack(
    struct App* app,
    struct EditorIfacePanels* panels,
    const char* text,
    size_t length,
    struct ToriRS_ComponentPack** out_pack)
{
    char why[160] = "";
    struct ToriRS_ComponentPack* pack;

    *out_pack = NULL;
    if( !app->provider || panels->model.interface_id < 0 )
        return 0;
    pack = pack_from_text(text, length, why, sizeof(why));
    if( !pack )
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        snprintf(line, sizeof(line), "Not mounted: %s", why);
        if( panels->lb_status >= 0 )
            ToriRSChrome_SetText(&app->dbg_ui, panels->lb_status, line);
        return 0;
    }
    /* The provider owns it from here (and frees the cache's copy it replaces). */
    CacheProvider_ComponentPackAdd(app->provider, panels->model.interface_id, pack);
    *out_pack = pack;
    return 1;
}

static void
remember_text(
    struct AppIfaceState* state,
    const char* key,
    const char* text,
    size_t length)
{
    ToriMem_Free(ToriMem_Editor(), state->mounted_text);
    state->mounted_text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(state->mounted_text);
    memcpy(state->mounted_text, text, length);
    state->mounted_text[length] = '\0';
    state->mounted_length = length;
    snprintf(state->mounted_key, sizeof(state->mounted_key), "%s", key);
}

static void
mount(
    struct App* app,
    struct EditorIfacePanels* panels,
    struct AppIfaceState* state,
    const struct EditorIfaceRequest* request,
    const char* text,
    size_t length)
{
    struct ToriRS_ComponentPack* pack;

    if( !publish_pack(app, panels, text, length, &pack) )
        return;
    if( request->kind == EDITOR_IFACE_REQUEST_MOUNT_ROOT )
        App_OpenRootInterface(app, request->interface_id);
    else
        App_OpenSubInterface(app, request->target_uid, request->interface_id, request->sub_type);
    state->mount = *request;
    remember_text(state, panels->model.doc_key, text, length);
}

/* ---- refill -------------------------------------------------------------- */

int
AppIfaceEditor_RefillNode(
    struct UITree* tree,
    int index,
    const struct UIBuildComponent* build)
{
    const struct UITreeComponent* node;
    enum UITreeComponentType want;

    assert(tree);
    assert(build);
    if( index < 0 || index >= (int)tree->component_count )
        return 0;
    node = &tree->components[index];
    switch( build->type )
    {
    case UIBUILD_LAYER:
        want = UIELEM_RS_LAYER;
        break;
    case UIBUILD_RECT:
        want = UIELEM_RS_RECT;
        break;
    case UIBUILD_TEXT:
        want = UIELEM_RS_TEXT;
        break;
    case UIBUILD_GRAPHIC:
        want = UIELEM_RS_GRAPHIC;
        break;
    case UIBUILD_MODEL:
        want = UIELEM_RS_MODEL;
        break;
    default:
        return 0;
    }
    if( node->type != want )
        return 0;

    UITree_SetPositionModesAt(tree, index, build->base_x, build->base_y, build->x_mode, build->y_mode);
    UITree_SetSizeModesAt(
        tree, index, build->base_width, build->base_height, build->width_mode, build->height_mode);
    UITree_SetHideAt(tree, index, build->hide);
    UITree_SetTransparencyAt(tree, index, build->transparency);
    if( build->type == UIBUILD_LAYER )
        UITree_SetScrollSizeAt(tree, index, build->scroll_width, build->scroll_height);
    else if( build->type == UIBUILD_TEXT )
    {
        UITree_SetTextAt(tree, index, build->text ? build->text : "");
        UITree_SetColourAt(tree, index, build->color);
    }
    else if( build->type == UIBUILD_RECT )
        UITree_SetColourAt(tree, index, build->color);
    else if( build->type == UIBUILD_MODEL )
    {
        UITree_SetModelAt(tree, index, build->model_id);
        UITree_SetModelPoseAt(
            tree, index, build->model_x_offset, build->model_y_offset, build->model_xan,
            build->model_yan, build->model_zan, build->model_zoom);
    }
    return 1;
}

/** Keys a refill writes onto a live node; any other change remounts. */
static int
refillable_key(const char* key)
{
    static const char* const keys[] = {
        "x",        "y",        "width",    "height",   "xmode",    "ymode",       "widthmode",
        "heightmode", "hidden", "trans",    "colour",   "text",     "model",       "modelzoom",
        "modelxan", "modelyan", "modelzan", "modelxof", "modelyof", "scrollwidth", "scrollheight",
    };
    for( size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++ )
    {
        if( strcmp(key, keys[i]) == 0 )
            return 1;
    }
    return 0;
}

/** Do two blocks differ only in refillable lines? (1 also when identical.) */
static int
block_change_refillable(
    const struct Editor_IfFile* before,
    const struct Editor_IfFile* after,
    int position)
{
    struct Editor_CfgBlock a;
    struct Editor_CfgBlock b;
    int ok = 1;
    const struct Editor_CfgBlockRef* ra = &before->index.blocks[position];
    const struct Editor_CfgBlockRef* rb = &after->index.blocks[position];

    Editor_CfgBlockInit(&a);
    Editor_CfgBlockInit(&b);
    Editor_CfgBlockSet(&a, before->text + ra->start, ra->end - ra->start);
    Editor_CfgBlockSet(&b, after->text + rb->start, rb->end - rb->start);
    /* Every line of each side that the other does not hold verbatim. */
    for( int pass = 0; pass < 2 && ok; pass++ )
    {
        const struct Editor_CfgBlock* one = pass == 0 ? &a : &b;
        const struct Editor_CfgBlock* other = pass == 0 ? &b : &a;

        for( int line = 0; line < one->line_count && ok; line++ )
        {
            const struct Editor_CfgLine* parsed = &one->lines[line];
            char key[EDITOR_CMD_NAME_MAX];
            size_t value_length;
            const char* value;
            int at;

            if( parsed->kind == EDITOR_CFG_LINE_HEADER )
            {
                ok = parsed->part == other->lines[0].part
                    && memcmp(one->text + parsed->start + 1, other->text + other->lines[0].start + 1, parsed->part) == 0;
                continue;
            }
            if( parsed->kind != EDITOR_CFG_LINE_FIELD || parsed->part >= sizeof(key) )
                continue;
            snprintf(key, sizeof(key), "%.*s", (int)parsed->part, one->text + parsed->start);
            value = Editor_CfgBlockValue(one, line, &value_length);
            at = Editor_CfgBlockFieldFind(other, key, 0);
            if( at >= 0 )
            {
                size_t other_length;
                const char* other_value = Editor_CfgBlockValue(other, at, &other_length);
                if( other_length == value_length && memcmp(other_value, value, value_length) == 0 )
                    continue;
            }
            ok = refillable_key(key);
        }
    }
    Editor_CfgBlockFree(&a);
    Editor_CfgBlockFree(&b);
    return ok;
}

/**
 * The document moved on from what is mounted. Refill each changed node when
 * every change is one a node takes in place; otherwise remount (the pack is
 * the document's, so the remount shows the edit).
 */
static void
follow_document(
    struct App* app,
    struct EditorIfacePanels* panels,
    struct AppIfaceState* state,
    const char* text,
    size_t length)
{
    struct Editor_IfFile before;
    struct Editor_IfFile after;
    int count;
    int refill = 1;

    Editor_IfIndex(&before, state->mounted_text, state->mounted_length);
    Editor_IfIndex(&after, text, length);
    count = Editor_IfComponentCount(&after);
    if( count != Editor_IfComponentCount(&before) || !app->tree )
        refill = 0;
    for( int i = 0; i < count && refill; i++ )
        refill = block_change_refillable(&before, &after, i);

    if( refill )
    {
        struct ToriRS_ComponentPack* pack;

        /* The pack goes in first, so a later remount (or a script reading
         * the record) sees the same component the node now shows. */
        if( publish_pack(app, panels, text, length, &pack) )
        {
            for( int i = 0; i < count && i < pack->component_count; i++ )
            {
                const struct Editor_CfgBlockRef* rb = &after.index.blocks[i];
                const struct Editor_CfgBlockRef* ra = &before.index.blocks[i];
                struct UIBuildComponent build;
                int index;

                if( ra->end - ra->start == rb->end - rb->start
                    && memcmp(before.text + ra->start, after.text + rb->start, rb->end - rb->start) == 0 )
                    continue;
                index = UITree_FindByComponentId(app->tree, pack->components[i].id);
                if( index < 0 )
                    continue;
                UITree_FillBuildFromToriRS(&build, &pack->components[i]);
                if( !AppIfaceEditor_RefillNode(app->tree, index, &build) )
                {
                    refill = 0;
                    break;
                }
            }
        }
        else
            refill = 0;
    }
    Editor_IfFree(&before);
    Editor_IfFree(&after);

    if( refill )
        remember_text(state, panels->model.doc_key, text, length);
    else
        mount(app, panels, state, &state->mount, text, length);
}

/* ---- the frame ----------------------------------------------------------- */

static void
var_set(
    struct App* app,
    const struct EditorIfaceRequest* request)
{
    switch( request->var_kind )
    {
    case EDITOR_CS2_WATCH_VARP:
        VarPManager_SetVarpOptimistic(&app->varps, request->var_id, request->value);
        break;
    case EDITOR_CS2_WATCH_VARBIT:
        VarPManager_SetVarbitOptimistic(&app->varps, request->var_id, request->value);
        break;
    case EDITOR_CS2_WATCH_VARC:
        VarCManager_SetInt(&app->varcs, request->var_id, request->value);
        break;
    case EDITOR_CS2_WATCH_VARCSTR:
        VarCManager_SetString(&app->varcs, request->var_id, request->text);
        break;
    }
}

static void
run(
    struct App* app,
    struct EditorIfacePanels* panels,
    struct AppIfaceState* state,
    const struct EditorIfaceRequest* request)
{
    const char* strings[4];
    struct ToriRS_Task* task;

    if( state->running )
    {
        ToriRSChrome_SetText(&app->dbg_ui, panels->lb_run, "A run is still going");
        return;
    }
    for( int i = 0; i < request->string_count; i++ )
        strings[i] = request->strings[i];
    if( request->trace )
        EditorCs2Trace_Begin(&panels->trace);
    task = CreateTask_CS2RunMixed(
        &app->host, request->script_id, request->component_id, request->component_id,
        request->arg_count ? request->args : NULL, request->arg_count, request->string_mask,
        request->string_count ? strings : NULL, request->string_count);
    if( !task )
    {
        if( request->trace )
            EditorCs2Trace_End(&panels->trace);
        ToriRSChrome_SetText(&app->dbg_ui, panels->lb_run, "The VM could not start that script");
        return;
    }
    /* Counted before it is queued: the queue decrements on removal. */
    task->join = &state->run_join;
    state->run_join++;
    state->running = 1;
    state->running_script = request->script_id;
    state->running_traced = request->trace;
    TaskRunner_AddRenderBlockingSerialTask(&app->runner, task);
}

static void
watch_read(
    struct App* app,
    struct EditorIfacePanels* panels)
{
    for( int i = 0; i < panels->watch.count; i++ )
    {
        const struct EditorCs2WatchRow* row = &panels->watch.rows[i];
        switch( row->kind )
        {
        case EDITOR_CS2_WATCH_VARP:
            EditorCs2Watch_Set(&panels->watch, i, VarPManager_GetVarp(&app->varps, row->id), NULL);
            break;
        case EDITOR_CS2_WATCH_VARBIT:
            EditorCs2Watch_Set(&panels->watch, i, VarPManager_GetVarbit(&app->varps, row->id), NULL);
            break;
        case EDITOR_CS2_WATCH_VARC:
            EditorCs2Watch_Set(&panels->watch, i, VarCManager_GetInt(&app->varcs, row->id), NULL);
            break;
        case EDITOR_CS2_WATCH_VARCSTR:
            EditorCs2Watch_Set(&panels->watch, i, 0, VarCManager_GetString(&app->varcs, row->id));
            break;
        }
    }
}

void
AppIfaceEditor_Service(
    void* opaque,
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor)
{
    struct App* app = opaque;
    struct AppIfaceState* state;
    struct EditorIfaceRequest request;
    size_t length = 0;
    const char* text;

    assert(app);
    assert(panels);
    assert(ui);

    state = state_of(panels);
    text = EditorIfacePanels_Text(panels, editor, &length);

    while( EditorIfacePanels_TakeRequest(panels, &request) )
    {
        switch( request.kind )
        {
        case EDITOR_IFACE_REQUEST_MOUNT_ROOT:
        case EDITOR_IFACE_REQUEST_MOUNT_SUB:
            if( text )
                mount(app, panels, state, &request, text, length);
            break;
        case EDITOR_IFACE_REQUEST_RUN:
            run(app, panels, state, &request);
            break;
        case EDITOR_IFACE_REQUEST_VAR_SET:
            var_set(app, &request);
            break;
        case EDITOR_IFACE_REQUEST_PICK:
            state->pick_armed = request.value;
            break;
        }
    }

    /* The mirror's text moved (this view's edit or another's). */
    if( text && state->mount.kind != EDITOR_IFACE_REQUEST_NONE
        && strcmp(state->mounted_key, panels->model.doc_key) == 0
        && (state->mounted_length != length || memcmp(state->mounted_text, text, length) != 0) )
        follow_document(app, panels, state, text, length);

    if( state->running && state->run_join == 0 )
    {
        char line[TORIRS_CHROME_LABEL_MAX];
        int steps = state->running_traced ? EditorCs2Trace_End(&panels->trace) : 0;

        state->running = 0;
        snprintf(line, sizeof(line), "Script %d finished%s%d instructions traced", state->running_script,
                 state->running_traced ? ", " : "", steps);
        if( !state->running_traced )
            snprintf(line, sizeof(line), "Script %d finished", state->running_script);
        EditorIfacePanels_TraceDone(panels, ui, line);
    }
    watch_read(app, panels);
}

int
AppIfaceEditor_PickArmed(struct App* app)
{
    struct EditorIfacePanels* panels;

    assert(app);
    panels = AppIfaceEditor_Panels(app);
    return panels && panels->app_state && ((struct AppIfaceState*)panels->app_state)->pick_armed;
}

int
AppIfaceEditor_PickAt(
    struct App* app,
    int x,
    int y)
{
    struct EditorIfacePanels* panels;
    int32_t index;
    int steps = 0;
    int base;

    assert(app);
    if( !AppIfaceEditor_PickArmed(app) || !app->tree )
        return 0;
    panels = AppIfaceEditor_Panels(app);
    base = panels->model.interface_id > 0 ? panels->model.interface_id : 0;
    index = UITree_HitTest(app->tree, x, y);
    /* The hit is the deepest node; walk up to the interface's own. Bounded by
     * the depth, never a tree walk. */
    while( index >= 0 && index < (int)app->tree->component_count && steps++ < 256 )
    {
        int component_id = app->tree->components[index].component_id;
        if( component_id >= 0 && (component_id >> 16) == base
            && EditorIfaceModel_FindComponent(&panels->model, component_id) >= 0 )
        {
            EditorIfacePanels_SelectComponent(panels, &app->dbg_ui, component_id);
            return 1;
        }
        index = app->tree->components[index].parent;
    }
    return 0;
}
