#include "editor.h"

#include "editor_derive.h"
#include "editor_jm2.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
Editor_Open(
    struct Editor* editor,
    const char* content_dir,
    const char* repo_root,
    const struct RSCache* profile)
{
    assert(editor);
    assert(content_dir);
    assert(profile);

    memset(editor, 0, sizeof(*editor));
    Editor_DocInit(&editor->doc, profile);
    Editor_UndoInit(&editor->undo);

    Editor_HostOpenLocal(&editor->host, content_dir, repo_root);
    editor->host_open = 1;

    editor->writable = editor->host.vtable->session(editor->host.user_data, 1) == EDITOR_HOST_OK;
    if( !editor->writable )
        fprintf(
            stderr,
            "editor: another session holds %s — opening read-only\n",
            content_dir);

    return 1;
}

void
Editor_Close(struct Editor* editor)
{
    if( !editor )
        return;

    if( editor->host_open )
    {
        editor->host.vtable->session(editor->host.user_data, 0);
        Editor_HostClose(&editor->host);
        editor->host_open = 0;
    }
    Editor_DocFree(&editor->doc);
}

/**
 * Derive a square and hand both halves to the provider.
 *
 * The provider takes ownership of what it is given, and replacing an entry
 * leaks the old one, so a re-seed of a square already resident has to reuse
 * the record in place rather than adding a second.
 */
static int
seed_provider(
    struct Editor* editor,
    struct CacheProvider* provider,
    const struct Editor_Square* square)
{
    int map_id = CacheProvider_MapId(square->map_x, square->map_z);
    struct ToriRS_MapTerrain* terrain;
    struct ToriRS_MapLocs* locs;

    assert(editor);
    assert(provider);
    assert(square);

    terrain = CacheProvider_MapTerrainGet(provider, map_id);
    if( !terrain )
    {
        terrain = malloc(sizeof(*terrain));
        assert(terrain);
        memset(terrain, 0, sizeof(*terrain));
        CacheProvider_MapTerrainAdd(provider, map_id, terrain);
    }
    if( !Editor_SquareDeriveTerrain(square, editor->doc.profile, terrain) )
    {
        fprintf(
            stderr,
            "editor: cannot derive terrain for %d,%d\n",
            square->map_x,
            square->map_z);
        return 0;
    }

    locs = CacheProvider_MapSceneryGet(provider, map_id);
    if( !locs )
    {
        locs = malloc(sizeof(*locs));
        assert(locs);
        memset(locs, 0, sizeof(*locs));
        CacheProvider_MapSceneryAdd(provider, map_id, locs);
    }
    free(locs->locs);
    locs->locs = NULL;
    Editor_SquareDeriveLocs(square, locs);

    return 1;
}

int
Editor_LoadSquare(
    struct Editor* editor,
    struct CacheProvider* provider,
    int map_x,
    int map_z)
{
    struct Editor_Square* square;
    struct EditorHost_Blob jm2 = { NULL, 0 };
    struct EditorHost_Blob jl2 = { NULL, 0 };
    struct Editor_ParseResult result;
    enum EditorHost_Status status;

    assert(editor);
    assert(provider);

    square = Editor_DocOpenSquare(&editor->doc, map_x, map_z);
    if( !square )
    {
        fprintf(stderr, "editor: no room for another square\n");
        return 0;
    }
    if( square->loaded )
        return 1;

    status = editor->host.vtable->square_load(
        editor->host.user_data, map_x, map_z, &jm2, &jl2);
    if( status != EDITOR_HOST_OK )
    {
        fprintf(stderr, "editor: cannot read square %d,%d\n", map_x, map_z);
        return 0;
    }

    result = Editor_Jm2Parse(square, jm2.data, jm2.size);
    if( result.status != EDITOR_PARSE_OK )
    {
        fprintf(
            stderr,
            "editor: m%d_%d.jm2 line %d: parse error %d\n",
            map_x,
            map_z,
            result.line,
            (int)result.status);
        Editor_HostBlobFree(&jm2);
        Editor_HostBlobFree(&jl2);
        return 0;
    }

    /* A square with no scenery ships no `.jl2`; zero locs is the right answer,
     * not a parse failure. */
    if( jl2.data && jl2.size > 0 )
    {
        result = Editor_Jl2Parse(square, jl2.data, jl2.size);
        if( result.status != EDITOR_PARSE_OK )
        {
            fprintf(
                stderr,
                "editor: m%d_%d.jl2 line %d: parse error %d\n",
                map_x,
                map_z,
                result.line,
                (int)result.status);
            Editor_HostBlobFree(&jm2);
            Editor_HostBlobFree(&jl2);
            return 0;
        }
    }

    Editor_HostBlobFree(&jm2);
    Editor_HostBlobFree(&jl2);

    if( !seed_provider(editor, provider, square) )
        return 0;

    /* Says which squares the world is showing from the content tree rather
     * than from the bake — the one fact that distinguishes an editor boot from
     * a normal one, and the first thing to check when an edit does not appear. */
    fprintf(
        stderr,
        "editor: seeded m%d_%d from text (%d locs)\n",
        map_x,
        map_z,
        square->loc_count);
    return 1;
}

static void
queue_rebuild(
    struct Editor* editor,
    int map_x,
    int map_z)
{
    assert(editor);

    for( int i = 0; i < editor->rebuild_count; i++ )
    {
        if( editor->rebuild_queue[i * 2] == map_x && editor->rebuild_queue[i * 2 + 1] == map_z )
            return;
    }
    if( editor->rebuild_count >= EDITOR_REBUILD_QUEUE_MAX )
        return;

    editor->rebuild_queue[editor->rebuild_count * 2] = map_x;
    editor->rebuild_queue[editor->rebuild_count * 2 + 1] = map_z;
    editor->rebuild_count++;
}

static void
queue_span(
    struct Editor* editor,
    const struct Editor_Cmd* command)
{
    int coords[EDITOR_REBUILD_QUEUE_MAX * 2];
    int count;

    assert(editor);
    assert(command);

    count = Editor_CmdRebuildSpan(command, coords, EDITOR_REBUILD_QUEUE_MAX);
    if( count > EDITOR_REBUILD_QUEUE_MAX )
        count = EDITOR_REBUILD_QUEUE_MAX;
    for( int i = 0; i < count; i++ )
        queue_rebuild(editor, coords[i * 2], coords[i * 2 + 1]);
}

static void
on_history_step(
    void* user_data,
    const struct Editor_Cmd* command)
{
    queue_span(user_data, command);
}

int
Editor_Apply(
    struct Editor* editor,
    const struct Editor_Cmd* command)
{
    assert(editor);
    assert(command);

    if( !Editor_CmdApply(&editor->doc, command, EDITOR_CMD_FORWARD) )
        return 0;

    Editor_UndoPush(&editor->undo, command);
    queue_span(editor, command);
    return 1;
}

int
Editor_Undo(struct Editor* editor)
{
    assert(editor);
    return Editor_UndoUndo(&editor->undo, &editor->doc, on_history_step, editor);
}

int
Editor_Redo(struct Editor* editor)
{
    assert(editor);
    return Editor_UndoRedo(&editor->undo, &editor->doc, on_history_step, editor);
}

int
Editor_DrainRebuilds(
    struct Editor* editor,
    struct CacheProvider* provider,
    int* out_coords,
    int max)
{
    int reported = 0;

    assert(editor);
    assert(provider);
    assert(out_coords || max == 0);

    for( int i = 0; i < editor->rebuild_count; i++ )
    {
        int map_x = editor->rebuild_queue[i * 2];
        int map_z = editor->rebuild_queue[i * 2 + 1];
        struct Editor_Square* square = Editor_DocFindSquare(&editor->doc, map_x, map_z);

        /* A queued neighbour the document never opened is not an error: the
         * edit reaches across the seam, but there is nothing loaded on the
         * other side to remesh. */
        if( !square || !square->loaded )
            continue;

        seed_provider(editor, provider, square);
        if( reported < max )
        {
            out_coords[reported * 2] = map_x;
            out_coords[reported * 2 + 1] = map_z;
        }
        reported++;
    }
    editor->rebuild_count = 0;

    return reported < max ? reported : max;
}

int
Editor_SaveAll(struct Editor* editor)
{
    int saved = 0;

    assert(editor);

    if( !editor->writable )
        return -1;

    for( int i = 0; i < editor->doc.square_count; i++ )
    {
        struct Editor_Square* square = &editor->doc.squares[i];
        char* jm2_text = NULL;
        char* jl2_text = NULL;
        size_t jm2_length = 0;
        size_t jl2_length = 0;
        enum EditorHost_Status status;

        if( !square->dirty_map && !square->dirty_loc )
            continue;

        /* Only the changed half is emitted; the other stays NULL and the host
         * leaves that file alone. Rewriting an untouched `.jl2` would show up
         * as a diff on placements the user never edited. */
        if( square->dirty_map )
        {
            jm2_length = Editor_Jm2Emit(square, NULL, 0);
            jm2_text = malloc(jm2_length + 1);
            assert(jm2_text);
            Editor_Jm2Emit(square, jm2_text, jm2_length + 1);
        }
        if( square->dirty_loc )
        {
            jl2_length = Editor_Jl2Emit(square, NULL, 0);
            jl2_text = malloc(jl2_length + 1);
            assert(jl2_text);
            Editor_Jl2Emit(square, jl2_text, jl2_length + 1);
        }

        status = editor->host.vtable->square_save(
            editor->host.user_data,
            square->map_x,
            square->map_z,
            jm2_text,
            jm2_length,
            jl2_text,
            jl2_length);

        free(jm2_text);
        free(jl2_text);

        if( status != EDITOR_HOST_OK )
        {
            fprintf(
                stderr, "editor: save failed for %d,%d\n", square->map_x, square->map_z);
            continue;
        }

        square->dirty_map = 0;
        square->dirty_loc = 0;
        saved++;
    }
    return saved;
}

int
Editor_Bake(
    struct Editor* editor,
    EditorHost_ProgressFn on_progress,
    void* progress_user_data)
{
    assert(editor);

    return editor->host.vtable->bake(
               editor->host.user_data, on_progress, progress_user_data) == EDITOR_HOST_OK;
}
