#include "mem/torirs_mem.h"
#include "editor.h"

#include "editor_derive.h"
#include "editor_jm2.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor_areas.h"
#include "log/torirs_log.h"

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
    Editor_HistoryInit(&editor->history);

    editor->host = *host;
    editor->host_open = 1;

    editor->writable = Editor_HostMapEdWritable(&editor->host);
    editor->capabilities = Editor_HostMapEdCapabilities(&editor->host);
    if( !editor->writable )
        TORIRS_LOG("editor: another server holds %s — opening read-only\n",
            label);

    /* The named areas and square collections (editor_areas.h): one small
     * document every window of every Client mirrors from the start, so the
     * world overlay and the Areas panel never wait on an open. A server that
     * predates the kind refuses it; the session runs without areas. */
    if( !Editor_OpenDoc(editor, EDITOR_DOC_AREAS, EDITOR_AREAS_DOC_KEY) )
        TORIRS_LOG("editor: %s serves no areas document; named areas are off\n", label);

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
    Editor_HistoryFree(&editor->history);
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
        terrain = ToriMem_Alloc(ToriMem_Assets(), sizeof(*terrain));
        assert(terrain);
        memset(terrain, 0, sizeof(*terrain));
        CacheProvider_MapTerrainAdd(provider, map_id, terrain);
    }
    if( !Editor_SquareDeriveTerrain(square, editor->doc.profile, terrain) )
    {
        TORIRS_ERR("editor: cannot derive terrain for %d,%d\n",
            square->map_x,
            square->map_z);
        return 0;
    }

    locs = CacheProvider_MapSceneryGet(provider, map_id);
    if( !locs )
    {
        locs = ToriMem_Alloc(ToriMem_Assets(), sizeof(*locs));
        assert(locs);
        memset(locs, 0, sizeof(*locs));
        CacheProvider_MapSceneryAdd(provider, map_id, locs);
    }
    ToriMem_Free(ToriMem_Assets(), locs->locs);
    locs->locs = NULL;
    Editor_SquareDeriveLocs(square, locs);

    return 1;
}

/**
 * Fill the mirror's copy of a document from the authority's DOC_OPEN answer.
 * Re-filling a loaded document replaces it — that is the resync path.
 */
static int
mirror_fill(
    struct Editor* editor,
    const struct EditorHost_DocText* text)
{
    struct Editor_DocHandle handle;

    assert(editor);
    assert(text);

    if( !Editor_DocOpen(&editor->doc, text->kind, text->key, &handle) )
    {
        TORIRS_ERR("editor: no room to mirror %s\n", text->key);
        return 0;
    }

    if( handle.square )
    {
        struct Editor_Square* square = handle.square;
        struct Editor_ParseResult result;

        if( text->part_count != 2 )
            return 0;
        result = Editor_Jm2Parse(square, text->parts[0].data, text->parts[0].size);
        if( result.status != EDITOR_PARSE_OK )
        {
            TORIRS_ERR("editor: %s.jm2 line %d: parse error %d\n",
                text->key,
                result.line,
                (int)result.status);
            return 0;
        }
        /* A square with no scenery ships no `.jl2`; zero locs is the right
         * answer, not a parse failure. */
        square->loc_count = 0;
        if( text->parts[1].data && text->parts[1].size > 0 )
        {
            result = Editor_Jl2Parse(square, text->parts[1].data, text->parts[1].size);
            if( result.status != EDITOR_PARSE_OK )
            {
                TORIRS_ERR("editor: %s.jl2 line %d: parse error %d\n",
                    text->key,
                    result.line,
                    (int)result.status);
                return 0;
            }
        }
        Editor_DocSquareSetStorage(&editor->doc, square, text->storage);
        square->dirty_map = (text->dirty & 1) != 0;
        square->dirty_loc = (text->dirty & 2) != 0;
        square->loaded = 1;
        return 1;
    }

    if( text->part_count != 1 )
        return 0;
    if( handle.spawn )
    {
        struct Editor_SpawnParseResult result = Editor_SpawnParse(
            &handle.spawn->file, text->parts[0].data, text->parts[0].size);
        if( !result.ok )
        {
            TORIRS_ERR("editor: spawn %s line %d does not parse\n", text->key, result.line);
            return 0;
        }
        handle.spawn->loaded = 1;
        handle.spawn->dirty = (text->dirty & 1) != 0;
        return 1;
    }
    Editor_TextDocSet(handle.text, text->parts[0].data, text->parts[0].size);
    handle.text->dirty = (text->dirty & 1) != 0;
    return 1;
}

int
Editor_OpenDoc(
    struct Editor* editor,
    enum Editor_DocKind kind,
    const char* key)
{
    struct EditorHost_DocText text;
    enum EditorHost_Status status;
    int ok;

    assert(editor);
    assert(key);

    /* Open on the AUTHORITY, mirror what it answers — the document's current
     * text, another client's unsaved edits included, plus the dirty flags so
     * this mirror agrees about what is unsaved. */
    status = Editor_HostMapEdDocOpen(&editor->host, kind, key, &text);
    if( status != EDITOR_HOST_OK )
    {
        TORIRS_ERR("editor: cannot open %s (status %d)\n", key, (int)status);
        Editor_HostDocTextFree(&text);
        return 0;
    }
    ok = mirror_fill(editor, &text);
    Editor_HostDocTextFree(&text);
    return ok;
}

void
Editor_CloseDoc(
    struct Editor* editor,
    enum Editor_DocKind kind,
    const char* key)
{
    assert(editor);
    assert(key);
    Editor_HostMapEdDocClose(&editor->host, kind, key);
}

int
Editor_SaveDoc(
    struct Editor* editor,
    enum Editor_DocKind kind,
    const char* key)
{
    int saved;

    assert(editor);
    assert(key);

    saved = Editor_HostMapEdSave(&editor->host, kind, key);
    Editor_PumpFacts(editor);
    return saved;
}

enum EditorHost_Status
Editor_CreateSquare(
    struct Editor* editor,
    const struct EditorHost_DocCreate* request)
{
    enum EditorHost_Status status;
    char key[EDITOR_DOC_KEY_MAX + EDITOR_DOC_STORAGE_MAX];

    assert(editor);
    assert(request);

    {
        char label[EDITOR_HISTORY_LABEL_MAX];
        if( request->template_kind == EDITOR_DOC_TEMPLATE_COPY )
            snprintf(label, sizeof(label), "New square %.24s (copy of %.40s)", request->key, request->source_key);
        else
            snprintf(label, sizeof(label), "New square %.24s (%s)", request->key,
                request->template_kind == EDITOR_DOC_TEMPLATE_FLAT ? "flat" : "empty");
        editor->history_intent++;
        Editor_HistoryBreak(&editor->history, label);
    }
    status = Editor_HostMapEdDocCreate(&editor->host, request);
    /* The fill's FACT_CMDs went out before the ack; this mirror does not
     * hold the square yet, so they applied to nothing — opening it now
     * receives the filled text in one answer. */
    Editor_PumpFacts(editor);
    editor->history_intent--;
    Editor_HistoryBreak(&editor->history, NULL);
    if( status != EDITOR_HOST_OK )
        return status;
    if( request->storage[0] && strcmp(request->storage, "maps") != 0 )
        snprintf(key, sizeof(key), "%s/%s", request->storage, request->key);
    else
        snprintf(key, sizeof(key), "%s", request->key);
    return Editor_OpenDoc(editor, EDITOR_DOC_SQUARE, key) ? EDITOR_HOST_OK : EDITOR_HOST_IO_ERROR;
}

enum EditorHost_Status
Editor_CopyRecord(
    struct Editor* editor,
    const char* type,
    const char* name,
    const char* new_name,
    int* out_id)
{
    enum EditorHost_Status status;
    char key[EDITOR_DOC_KEY_MAX];
    char new_key[EDITOR_DOC_KEY_MAX];
    int written;

    assert(editor);
    assert(type);
    assert(name);
    assert(new_name);

    if( out_id )
        *out_id = -1;
    written = snprintf(key, sizeof(key), "%s:%s", type, name);
    if( written <= 0 || (size_t)written >= sizeof(key) )
        return EDITOR_HOST_REFUSED;
    written = snprintf(new_key, sizeof(new_key), "%s:%s", type, new_name);
    if( written <= 0 || (size_t)written >= sizeof(new_key) )
        return EDITOR_HOST_REFUSED;
    {
        char label[EDITOR_HISTORY_LABEL_MAX];
        snprintf(label, sizeof(label), "Copy %.40s to %.40s", key, new_name);
        editor->history_intent++;
        Editor_HistoryBreak(&editor->history, label);
    }
    status = Editor_HostMapEdSaveAs(&editor->host, EDITOR_DOC_CFG, key, new_key, out_id);
    /* The copy's FACT_CMDs went out before the ack: the ledger and
     * membership documents this mirror holds take them now; the new record
     * this mirror does not hold yet arrives whole when it opens. */
    Editor_PumpFacts(editor);
    editor->history_intent--;
    Editor_HistoryBreak(&editor->history, NULL);
    if( status != EDITOR_HOST_OK )
        return status;
    return Editor_OpenDoc(editor, EDITOR_DOC_CFG, new_key) ? EDITOR_HOST_OK : EDITOR_HOST_IO_ERROR;
}

int
Editor_LoadSquare(
    struct Editor* editor,
    struct CacheProvider* provider,
    int map_x,
    int map_z)
{
    struct Editor_Square* square;
    char key[EDITOR_DOC_KEY_MAX];

    assert(editor);
    assert(provider);

    square = Editor_DocFindSquare(&editor->doc, map_x, map_z);
    if( square && square->loaded )
        return 1;
    if( !square && editor->doc.square_count == EDITOR_DOC_MAX_SQUARES )
    {
        TORIRS_ERR("editor: no room for another square\n");
        return 0;
    }

    Editor_SquareKey(key, map_x, map_z);
    if( !Editor_OpenDoc(editor, EDITOR_DOC_SQUARE, key) )
    {
        TORIRS_ERR("editor: cannot read square %d,%d\n", map_x, map_z);
        return 0;
    }
    square = Editor_DocFindSquare(&editor->doc, map_x, map_z);
    assert(square);

    if( !seed_provider(editor, provider, square) )
        return 0;

    /* Says which squares the world is showing from the content tree rather
     * than from the bake — the one fact that distinguishes an editor boot from
     * a normal one, and the first thing to check when an edit does not appear. */
    TORIRS_LOG("editor: seeded m%d_%d from text (%d locs)\n",
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
        TORIRS_LOG("editor: maped command seq went backwards (%u after %u) — this mirror "
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
    Editor_HistoryOnFact(
        &editor->history, direction == EDITOR_CMD_INVERSE ? EDITOR_CMD_INVERSE : EDITOR_CMD_FORWARD,
        command);
}

static void
mirror_on_saved(
    void* user_data,
    int count,
    const struct Editor_DocRef* rows)
{
    struct Editor* editor = user_data;

    for( int i = 0; i < count; i++ )
    {
        struct Editor_DocHandle handle;
        if( Editor_DocFind(&editor->doc, rows[i].kind, rows[i].key, &handle) )
            Editor_DocHandleClearDirty(&handle);
    }
}

/** A FACT_CMD this build cannot read: re-open the document it named, if
 *  this mirror holds it, so the next frame shows the authority's text. */
static void
mirror_on_resync(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key)
{
    struct Editor* editor = user_data;
    struct Editor_DocHandle handle;

    if( !Editor_DocFind(&editor->doc, kind, key, &handle) )
        return;
    TORIRS_LOG("editor: a newer peer edited %s in a way this build cannot apply — "
               "re-opening it\n",
        key);
    if( Editor_OpenDoc(editor, kind, key) && handle.square )
        queue_rebuild(editor, handle.square->map_x, handle.square->map_z);
}

/* TORIRS_MAPED_STATE_DEBUG=1: one line per shared-state fact this session
 * hears and per state it sets -- the Client, which key and
 * its first values -- so a relay between windows can be watched in each
 * process's log. */
static int
state_debug(void)
{
    static int known;
    static int on;
    if( !known )
    {
        char const* value = getenv("TORIRS_MAPED_STATE_DEBUG");
        on = value && value[0] && value[0] != '0';
        known = 1;
    }
    return on;
}

static void
state_debug_line(
    const struct Editor* editor,
    const char* verb,
    uint32_t key,
    const int32_t* values,
    int count)
{
    TORIRS_REPORT("maped state: Client %u %s key %u [%d]%s%d %d %d\n",
        Editor_HostIsMapEd(&editor->host) ? Editor_HostMapEdClientId(&editor->host) : 0u, verb, key, count,
        count > 0 ? " " : "", count > 0 ? values[0] : 0, count > 1 ? values[1] : 0, count > 2 ? values[2] : 0);
}

static void
mirror_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct Editor* editor = user_data;

    if( state_debug() )
        state_debug_line(editor, "heard", key, values, count);
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
    sink.on_resync = mirror_on_resync;
    if( editor->history_intent )
        return Editor_HostMapEdDrainFacts(&editor->host, &sink);
    {
        /* Facts no intent of this session asked for: another window's. Its
         * stroke boundaries are not on the wire, so one pump is one group
         * (inside this session's own stroke they join it, as the
         * authority's single open stroke does). */
        int drained;
        if( !editor->history_stroke )
            Editor_HistoryBreak(&editor->history, NULL);
        drained = Editor_HostMapEdDrainFacts(&editor->host, &sink);
        Editor_HistoryBatchEnd(&editor->history);
        if( !editor->history_stroke )
            Editor_HistoryBreak(&editor->history, NULL);
        return drained;
    }
}

/* ---- intents ------------------------------------------------------------- */

int
Editor_Apply(
    struct Editor* editor,
    const struct Editor_Cmd* command)
{
    assert(editor);
    assert(command);

    /* Outside a stroke every command is its own undo step on the authority,
     * and so its own row here. */
    if( !editor->history_stroke )
        Editor_HistoryBreak(&editor->history, NULL);
    editor->history_intent++;
    if( Editor_HostMapEdCmd(&editor->host, command) != EDITOR_HOST_OK )
    {
        editor->history_intent--;
        return 0;
    }
    /* The authority broadcast before it acked, so the echo is in hand;
     * consuming it here is what lets callers read the document's new state
     * the moment this returns, as they always could. */
    Editor_PumpFacts(editor);
    editor->history_intent--;
    return 1;
}

int
Editor_Undo(struct Editor* editor)
{
    int count;

    assert(editor);

    /* Facts from elsewhere that are already queued are a batch of their
     * own, not part of this undo. */
    Editor_PumpFacts(editor);
    editor->history_intent++;
    Editor_HistoryReplayBegin(&editor->history, -1);
    count = Editor_HostMapEdUndo(&editor->host);
    Editor_PumpFacts(editor);
    Editor_HistoryReplayEnd(&editor->history, count);
    editor->history_intent--;
    return count;
}

int
Editor_Redo(struct Editor* editor)
{
    int count;

    assert(editor);

    Editor_PumpFacts(editor);
    editor->history_intent++;
    Editor_HistoryReplayBegin(&editor->history, 1);
    count = Editor_HostMapEdRedo(&editor->host);
    Editor_PumpFacts(editor);
    Editor_HistoryReplayEnd(&editor->history, count);
    editor->history_intent--;
    return count;
}

void
Editor_StrokeBegin(struct Editor* editor)
{
    assert(editor);
    Editor_HostMapEdStroke(&editor->host, 1);
    editor->history_stroke = 1;
    Editor_HistoryBreak(&editor->history, NULL);
}

const struct Editor_History*
Editor_GetHistory(const struct Editor* editor)
{
    assert(editor);
    return &editor->history;
}

void
Editor_HistoryName(
    struct Editor* editor,
    const char* verb)
{
    assert(editor);
    assert(verb);
    Editor_HistoryVerb(&editor->history, verb);
}

void
Editor_StrokeEnd(struct Editor* editor)
{
    assert(editor);
    Editor_HostMapEdStroke(&editor->host, 0);
    editor->history_stroke = 0;
    Editor_HistoryBreak(&editor->history, NULL);
}

int
Editor_StateSet(
    struct Editor* editor,
    uint32_t key,
    const int32_t* values,
    int count)
{
    assert(editor);
    if( state_debug() )
        state_debug_line(editor, "sets", key, values, count);
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
