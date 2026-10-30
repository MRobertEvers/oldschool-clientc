#include "edpanel_common.h"

#include "edpanel_ops.h"
#include "mem/torirs_mem.h"

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
