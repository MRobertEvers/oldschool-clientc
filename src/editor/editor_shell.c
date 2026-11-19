#include "editor/editor_shell.h"

#include "mem/torirs_mem.h"

#include <assert.h>

struct EditorShell*
EditorShell_Create(void)
{
    /* EDITOR: the map editor's working state; EditorShell_Free releases it. */
    struct EditorShell* shell = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*shell));

    MapEditorGhost_Reset(&shell->map_ghost);
    return shell;
}

void
EditorShell_Free(struct EditorShell* shell)
{
    if( !shell )
        return;
    ToriMem_Free(ToriMem_Editor(), shell);
}
