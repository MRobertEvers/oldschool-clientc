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
Editor_OpenHost(
    struct Editor* editor,
    const struct EditorHost* host,
    const char* label,
    const struct RSCache* profile)
{
    assert(editor);
    assert(host);
    assert(host->vtable);
    assert(label);
    assert(profile);
    /* The session is a mirror; only a ToriRSMapEd connection has an
     * authority behind it to mirror. */
    assert(Editor_HostIsMapEd(host));

    memset(editor, 0, sizeof(*editor));
    Editor_DocInit(&editor->doc, profile);

    editor->host = *host;
    editor->host_open = 1;

    editor->writable = Editor_HostMapEdWritable(&editor->host);
    if( !editor->writable )
        fprintf(
            stderr,
            "editor: another server holds %s — opening read-only\n",
            label);

    return 1;
}

int
Editor_Open(
    struct Editor* editor,
    const char* content_dir,
    const char* repo_root,
    const struct RSCache* profile)
{
    struct EditorHost host;

    assert(editor);
    assert(content_dir);
    assert(profile);

    if( !Editor_HostOpenMapEdEmbed(&host, content_dir, repo_root) )
        return 0;
    return Editor_OpenHost(editor, &host, content_dir, profile);
}

void
Editor_Close(struct Editor* editor)
{
    if( !editor )
        return;

    if( editor->host_open )
    {
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

    int dirty_map = 0;
    int dirty_loc = 0;

    square = Editor_DocOpenSquare(&editor->doc, map_x, map_z);
    if( !square )
    {
        fprintf(stderr, "editor: no room for another square\n");
        return 0;
    }
    if( square->loaded )
        return 1;

    /* Open on the AUTHORITY, mirror what it answers — which is the doc's
     * current text, another client's unsaved edits included, plus the dirty
     * flags so this mirror agrees about what is unsaved. */
    status = Editor_HostMapEdSquareOpen(
        &editor->host, map_x, map_z, &jm2, &jl2, &dirty_map, &dirty_loc);
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

    square->dirty_map = dirty_map;
    square->dirty_loc = dirty_loc;

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

/* ---- the fact sink: where the mirror actually mutates -------------------- */

static void
mirror_on_cmd(
    void* user_data,
    uint32_t seq,
    int direction,
    const struct Editor_Cmd* command)
{
    struct Editor* editor = user_data;

    /*
     * The authority stamps every document fact with a monotonic seq, and a
     * mirror that skips one has silently diverged from the world everyone
     * else is editing. It cannot repair itself — the missing command is gone
     * — so it says so loudly rather than drawing a document nobody agrees
     * with; re-opening the affected squares re-syncs from the authority.
     *
     * Broadcasts share the counter with state facts, so a gap here is normal
     * whenever this Client also moved its selection. Only a BACKWARDS or
     * repeated seq is a true fault.
     */
    if( editor->last_cmd_seq != 0 && seq <= editor->last_cmd_seq )
        fprintf(
            stderr,
            "editor: maped command seq went backwards (%u after %u) — this mirror "
            "may have diverged; re-open the square to re-sync\n",
            seq,
            editor->last_cmd_seq);
    editor->last_cmd_seq = seq;

    /* A command against a square this mirror never opened applies to
     * nothing, and that is correct: this client is not showing it, and the
     * authority's copy is what a save writes. */
    Editor_CmdApply(
        &editor->doc,
        command,
        direction == EDITOR_CMD_INVERSE ? EDITOR_CMD_INVERSE : EDITOR_CMD_FORWARD);
    queue_span(editor, command);
}

static void
mirror_on_saved(
    void* user_data,
    int count,
    const int* coords)
{
    struct Editor* editor = user_data;

    for( int i = 0; i < count; i++ )
    {
        struct Editor_Square* square =
            Editor_DocFindSquare(&editor->doc, coords[i * 2], coords[i * 2 + 1]);
        if( !square )
            continue;
        square->dirty_map = 0;
        square->dirty_loc = 0;
    }
}

static void
mirror_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct Editor* editor = user_data;

    if( editor->on_state )
        editor->on_state(editor->on_state_user_data, key, values, count);
}

int
Editor_PumpFacts(struct Editor* editor)
{
    struct EditorHostMapEdFacts sink;

    assert(editor);

    sink.user_data = editor;
    sink.on_cmd = mirror_on_cmd;
    sink.on_saved = mirror_on_saved;
    sink.on_state = mirror_on_state;
    return Editor_HostMapEdDrainFacts(&editor->host, &sink);
}

/* ---- intents ------------------------------------------------------------- */

int
Editor_Apply(
    struct Editor* editor,
    const struct Editor_Cmd* command)
{
    assert(editor);
    assert(command);

    if( Editor_HostMapEdCmd(&editor->host, command) != EDITOR_HOST_OK )
        return 0;
    /* The authority broadcast before it acked, so the echo is in hand;
     * consuming it here is what lets callers read the document's new state
     * the moment this returns, as they always could. */
    Editor_PumpFacts(editor);
    return 1;
}

int
Editor_Undo(struct Editor* editor)
{
    int count;

    assert(editor);

    count = Editor_HostMapEdUndo(&editor->host);
    Editor_PumpFacts(editor);
    return count;
}

int
Editor_Redo(struct Editor* editor)
{
    int count;

    assert(editor);

    count = Editor_HostMapEdRedo(&editor->host);
    Editor_PumpFacts(editor);
    return count;
}

void
Editor_StrokeBegin(struct Editor* editor)
{
    assert(editor);
    Editor_HostMapEdStroke(&editor->host, 1);
}

void
Editor_StrokeEnd(struct Editor* editor)
{
    assert(editor);
    Editor_HostMapEdStroke(&editor->host, 0);
}

int
Editor_StateSet(
    struct Editor* editor,
    uint32_t key,
    const int32_t* values,
    int count)
{
    assert(editor);
    return Editor_HostMapEdStateSet(&editor->host, key, values, count);
}

void
Editor_SetStateCallback(
    struct Editor* editor,
    void (*on_state)(void* user_data, uint32_t key, const int32_t* values, int count),
    void* user_data)
{
    assert(editor);
    editor->on_state = on_state;
    editor->on_state_user_data = user_data;
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

    /* Other clients' edits land here: their FACT_CMDs mutate the mirror and
     * queue their spans, and this frame's rebuild pass shows them. */
    Editor_PumpFacts(editor);

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
    int saved;

    assert(editor);

    /* The AUTHORITY emits and writes — its document is the one that counts,
     * and every mirror (this one included) clears its dirty flags on the
     * FACT_SAVED broadcast consumed just below. -1 means the server is
     * read-only, exactly as it meant when the lock was this session's. */
    saved = Editor_HostMapEdSaveAll(&editor->host);
    Editor_PumpFacts(editor);
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
