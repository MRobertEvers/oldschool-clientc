#include "editor/editor_shell.h"

#include "mem/torirs_mem.h"

#include <assert.h>

struct EditorShell*
EditorShell_Create(void)
{
    /* EDITOR: the editors' working state; EditorShell_Free releases it. */
    struct EditorShell* shell = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*shell));

    MapEditorGhost_Reset(&shell->map_ghost);
    EditorPreviewCamera_Reset(&shell->preview_camera);
    return shell;
}

void
EditorShell_Free(struct EditorShell* shell)
{
    if( !shell )
        return;
    ToriMem_Free(ToriMem_Editor(), shell);
}

void
EditorShell_FootprintSeed(
    struct EditorShell* shell,
    int mode)
{
    assert(shell);
    if( mode < 0 )
        mode = 0;
    shell->hover_footprint = mode;
    shell->hover_footprint_mode = mode > 0 ? mode : 1;
}

int
EditorShell_FootprintToggle(struct EditorShell* shell)
{
    assert(shell);
    shell->hover_footprint = shell->hover_footprint ? 0 : shell->hover_footprint_mode;
    return shell->hover_footprint;
}
