#include "edpanel_common.h"

#include "edpanel_ops.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"

#include <assert.h>

static struct Editor_Cmd* g_scratch;

struct Editor_Cmd*
edp_scratch(int* out_capacity)
{
    assert(out_capacity);
    /* One allocation for the session: the largest single action is a
     * rectangle of EDITOR_RECT_TILES_MAX tiles, and a brush is smaller. */
    if( !g_scratch )
    {
        g_scratch = ToriMem_Calloc(ToriMem_Editor(), EDITOR_RECT_TILES_MAX, sizeof(*g_scratch));
        assert(g_scratch);
    }
    *out_capacity = EDITOR_RECT_TILES_MAX;
    return g_scratch;
}

void
edp_scratch_free(void)
{
    ToriMem_Free(ToriMem_Editor(), g_scratch);
    g_scratch = NULL;
}

int
edp_apply(
    struct EditorUi* ui,
    const struct Editor_Cmd* commands,
    int count,
    const char* what)
{
    int applied = 0;

    assert(ui);
    assert(commands || count == 0);
    assert(what);

    if( count <= 0 )
        return 0;
    if( !ui->editor )
    {
        Editor_UiLog(ui, "editor: %s needs an editor session", what);
        return 0;
    }
    if( !ui->tools.stroke_open )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryName(ui->editor, what);
    }
    for( int i = 0; i < count; i++ )
        applied += Editor_Apply(ui->editor, &commands[i]) ? 1 : 0;
    if( !ui->tools.stroke_open )
        Editor_StrokeEnd(ui->editor);
    if( applied < count )
        Editor_UiLog(ui, "editor: %s: the authority refused %d of %d edits", what, count - applied, count);
    return applied;
}

int
edp_ensure_doc(
    struct EditorUi* ui,
    enum Editor_DocKind kind,
    const char* key)
{
    struct Editor_DocHandle handle;

    assert(ui);
    assert(key);

    if( !ui->editor )
        return 0;
    if( Editor_DocFind(&ui->editor->doc, kind, key, &handle) && Editor_DocHandleLoaded(&handle) )
        return 1;
    return Editor_OpenDoc(ui->editor, kind, key);
}

/* ---- the active level ------------------------------------------------------ */

int
edp_level_follow_vis(
    int old_edit,
    int old_vis,
    int new_edit)
{
    assert(old_edit >= EDP_LEVEL_AUTO);
    assert(old_edit < EDITOR_SQUARE_LEVELS);
    assert(new_edit >= EDP_LEVEL_AUTO);
    assert(new_edit < EDITOR_SQUARE_LEVELS);
    if( new_edit == EDP_LEVEL_AUTO )
        return -1;
    if( old_edit != EDP_LEVEL_AUTO && old_vis < 0 )
        return -1;
    return new_edit;
}

void
edp_level_set(
    struct EditorUi* ui,
    int level)
{
    int32_t value = level;

    assert(ui);
    assert(level >= EDP_LEVEL_AUTO);
    assert(level < EDITOR_SQUARE_LEVELS);
    ui->tools.vis_level = edp_level_follow_vis(ui->tools.edit_level, ui->tools.vis_level, level);
    ui->tools.edit_level = level;
    (void)Editor_UiPublish(ui, TORIRSMAPED_STATE_EDIT_LEVEL, &value, 1);
}

int
edp_level_step(
    struct EditorUi* ui,
    int delta,
    int auto_level)
{
    int level;

    assert(ui);
    level = ui->tools.edit_level >= 0 ? ui->tools.edit_level : auto_level;
    level += delta;
    if( level < 0 )
        level = 0;
    if( level > EDITOR_SQUARE_LEVELS - 1 )
        level = EDITOR_SQUARE_LEVELS - 1;
    edp_level_set(ui, level);
    return level;
}

int
edp_level_show_above(
    struct EditorUi* ui,
    int on)
{
    assert(ui);
    if( ui->tools.edit_level < 0 )
        return 0;
    ui->tools.vis_level = on ? -1 : ui->tools.edit_level;
    return 1;
}

int
edp_level_showing_above(const struct EditorUi* ui)
{
    assert(ui);
    return ui->tools.edit_level >= 0 && ui->tools.vis_level < 0;
}

int
edp_level_click(
    const struct EditorUi* ui,
    int hover_level)
{
    int level;

    assert(ui);
    level = ui->tools.edit_level >= 0 ? ui->tools.edit_level : hover_level;
    if( level < 0 )
        level = 0;
    if( level > EDITOR_SQUARE_LEVELS - 1 )
        level = EDITOR_SQUARE_LEVELS - 1;
    return level;
}
