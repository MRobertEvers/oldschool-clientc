/*
 * torirsmapedctl — a ToriRSMapEd controller, in its own process, with no
 * world at all.
 *
 * This is the design's controller connection made literal: it renders
 * nothing, holds no scene, links no cache codec, and still drives and follows
 * a live editing session. Everything it can do it does through exactly the
 * API the game client's editor uses (editor_host_remote.c), which is what
 * makes it a proof rather than a demo — if a controller can do it from here,
 * a controller can do it from a panel.
 *
 *   torirsmapedctl [--host h] [--port n] [--client <id>] <command> [args]
 *
 * `--client` joins a Client (session group) another connection already
 * started — the id a viewer prints at boot ("client 1"). Without it this
 * connection is its own Client, which is right for the document commands
 * (one world, everybody sees them) and wrong for the state ones (a Client of
 * one hears only itself).
 *
 * Commands:
 *
 *   watch [seconds]        stream the Client's facts as they arrive
 *   status                 protocol, writability, Client id, square count
 *   squares [limit]        the content tree's squares
 *   open <x> <z>           open a square in the authoritative document and
 *                          print what the authority answered
 *   select <x> <z> <lvl>   publish a terrain selection (ABSOLUTE world tile)
 *   tool <n>               publish a tool change (enum Editor_Tool)
 *   sync                   replay the Client's state store
 *   undo | redo            drive the shared history
 *   save                   save every dirty square, server-side
 *   bake                   run the bake, streaming its output
 *   text-replace <key> <find> <replace>
 *                          one TEXT_SPLICE on a text document ("rs2:<path
 *                          under server/scripts>"): the first <find> becomes
 *                          <replace>; unsaved until save or RUN
 *   cfg-copy <type> <name> <new_name>
 *                          SAVE_AS: copy the loc / npc / obj definition
 *                          <name> to <new_name> with a fresh id from
 *                          pack/<type>.alloc and its pack/<type>.client line,
 *                          as one undo step; unsaved until save or RUN
 *   tile-height <mx> <mz> <level> <lx> <lz> <height>
 *                          one TILE command on square m<mx>_<mz>: the tile
 *                          keeps every field but its raw height byte
 *   spawn-edit <row> to <row>
 *                          edit one spawn row of a square's EDITED .spawn as
 *                          one undo step ("edit spawn"). A <row> is
 *                          `<kind> <name> <x> <z> <level> [count]`: kind npc
 *                          or obj, `-npc`/`-obj` for a suppression row, the
 *                          name a pack symbol or `#<id>`, ABSOLUTE tiles, the
 *                          count for an obj only. Same square and kind: one
 *                          SPAWN_EDIT in place; else a delete and an add.
 *                          `gen:` before the first kind edits a GENERATED row
 *                          (areas/world): it is suppressed and the new row is
 *                          added to the edited file.
 *   spawn-move <row> to <x> <z> [level]
 *                          the same, changing only the tile ("move spawn")
 *   cfg-set <type>:<name>[@<overlay>] <key[#n][@line]> <value|->
 *                          one CFG_FIELD on a record block: of
 *                          configs/all.<type>, or with @<overlay> (a path
 *                          under server/scripts ending .<type>) of that
 *                          server overlay. `-` removes the field.
 *   live-attach launch|<host:port> [ticket]
 *                          attach the editor server's live link (launch
 *                          starts the lane's servers) and wait for the answer
 *   live-detach            detach (a launched lane's servers stop)
 *   live-cheat <player> <line>
 *                          a debugproc on the attached server
 *   build packs|client [seconds]
 *   run [seconds]          RUN: save all, build packs, build the client
 *                          cache, reload the attached server; streams every
 *                          line and exits 0 when RUN finished every step
 *
 * Exit status is 0 when the command did what it said, 1 otherwise.
 */

#include "editor/editor_host.h"
#include "mem/torirs_mem.h"
#include "torirs_maped.h"
#include "torirs_maped_build.h"
#include "torirs_maped_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <signal.h>
#include <unistd.h>
#endif

static volatile sig_atomic_t g_stop;

static void
on_interrupt(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

/* ---- fact printing ------------------------------------------------------- */

static char const*
selection_kind_name(int32_t kind)
{
    switch( kind )
    {
    case 0:
        return "none";
    case 1:
        return "terrain";
    case 2:
        return "loc";
    case 3:
        return "npc";
    case 4:
        return "obj";
    default:
        return "?";
    }
}

static char const* const k_cmd_kind_names[] = {
    "tile", "loc", "spawn", "suppress", "cfgfield", "cfgblock", "iffield", "ifcomp", "splice", "spawnedit",
};
_Static_assert(sizeof(k_cmd_kind_names) / sizeof(k_cmd_kind_names[0]) == EDITOR_CMD_KIND_COUNT,
    "a name per command kind");

static void
print_cmd(
    void* user_data,
    uint32_t seq,
    int direction,
    const struct Editor_Cmd* command)
{
    char const* verb = direction == EDITOR_CMD_INVERSE ? "undone" : "applied";
    char const* kind = (unsigned)command->kind < EDITOR_CMD_KIND_COUNT
                           ? k_cmd_kind_names[command->kind]
                           : "?";

    (void)user_data;

    if( command->kind == EDITOR_CMD_TILE )
        printf(
            "cmd  #%u %-7s tile   %s level %d tile %d,%d\n",
            seq,
            verb,
            command->doc_key,
            command->u.tile.level,
            command->u.tile.x,
            command->u.tile.z);
    else
        printf(
            "cmd  #%u %-7s %-6s %s %s%s\n",
            seq,
            verb,
            kind,
            command->doc_key,
            command->has_before ? "-" : "",
            command->has_after ? "+" : "");
    fflush(stdout);
}

static void
print_saved(
    void* user_data,
    int count,
    const struct Editor_DocRef* rows)
{
    (void)user_data;

    printf("save %d document(s):", count);
    for( int i = 0; i < count; i++ )
        printf(" %s", rows[i].key);
    printf("\n");
    fflush(stdout);
}

static void
print_resync(
    void* user_data,
    enum Editor_DocKind kind,
    const char* key)
{
    (void)user_data;
    printf("resync kind %d %s (a command this build cannot read)\n", (int)kind, key);
    fflush(stdout);
}

static void
print_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    (void)user_data;

    if( key == TORIRSMAPED_STATE_SELECTION && count >= 7 )
    {
        printf(
            "sel  %s at %d,%d level %d",
            selection_kind_name(values[0]),
            values[1],
            values[2],
            values[3]);
        if( values[0] == 2 )
            printf(" loc %d shape %d angle %d", values[4], values[5], values[6]);
        printf("\n");
    }
    else if( key == TORIRSMAPED_STATE_TOOL && count >= 1 )
        printf("tool %d\n", values[0]);
    else
    {
        printf("state key %u:", key);
        for( int i = 0; i < count; i++ )
            printf(" %d", values[i]);
        printf("\n");
    }
    fflush(stdout);
}

static const struct EditorHostMapEdFacts k_printing_sink = {
    NULL, print_cmd, print_saved, print_state, print_resync
};

static void
print_bake_line(
    void* user_data,
    const char* line)
{
    (void)user_data;
    printf("bake %s\n", line);
    fflush(stdout);
}

/* ---- commands ------------------------------------------------------------ */

static int
cmd_squares(
    struct EditorHost* host,
    int limit)
{
    int coords[4096 * 2];
    int count = 0;

    if( host->vtable->square_list(host->user_data, coords, 4096, &count) != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: square_list failed\n");
        return 1;
    }
    printf("%d square(s)\n", count);
    for( int i = 0; i < count && i < limit; i++ )
        printf("  m%d_%d\n", coords[i * 2], coords[i * 2 + 1]);
    if( count > limit )
        printf("  ... %d more (raise the limit argument to see them)\n", count - limit);
    return 0;
}

static int
cmd_open(
    struct EditorHost* host,
    int map_x,
    int map_z)
{
    struct EditorHost_Blob jm2 = { NULL, 0 };
    struct EditorHost_Blob jl2 = { NULL, 0 };
    int dirty_map = 0;
    int dirty_loc = 0;
    enum EditorHost_Status status;

    status = Editor_HostMapEdSquareOpen(
        host, map_x, map_z, &jm2, &jl2, &dirty_map, &dirty_loc);
    if( status != EDITOR_HOST_OK )
    {
        fprintf(
            stderr, "torirsmapedctl: cannot open m%d_%d (status %d)\n", map_x, map_z,
            (int)status);
        return 1;
    }
    /* The sizes are the DOCUMENT's current emit, so a square another
     * connection has edited reads back larger or smaller than its file. */
    printf(
        "m%d_%d open: jm2 %zu bytes, jl2 %zu bytes, dirty map=%d loc=%d\n",
        map_x,
        map_z,
        jm2.size,
        jl2.size,
        dirty_map,
        dirty_loc);
    Editor_HostBlobFree(&jm2);
    Editor_HostBlobFree(&jl2);
    return 0;
}

static int
parse_kind(char const* name)
{
    static char const* const names[] = { "square", "spawn", "cfg", "iface", "text" };
    for( int i = 0; i < EDITOR_DOC_KIND_COUNT; i++ )
    {
        if( strcmp(name, names[i]) == 0 )
            return i;
    }
    return -1;
}

/** DOC_OPEN: the authority's current text of any document, sizes only. */
static int
cmd_doc(
    struct EditorHost* host,
    char const* kind_name,
    char const* key)
{
    struct EditorHost_DocText text;
    enum EditorHost_Status status;
    int kind = parse_kind(kind_name);

    if( kind < 0 )
    {
        fprintf(stderr, "torirsmapedctl: kind is square|spawn|cfg|iface|text\n");
        return 1;
    }
    status = Editor_HostMapEdDocOpen(host, (enum Editor_DocKind)kind, key, &text);
    if( status != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: cannot open %s (status %d)\n", key, (int)status);
        Editor_HostDocTextFree(&text);
        return 1;
    }
    printf("%s open:%s%s", text.key, text.storage[0] ? " storage " : "", text.storage);
    for( int i = 0; i < text.part_count; i++ )
    {
        if( text.parts[i].data )
            printf(" part%d %zu bytes", i, text.parts[i].size);
        else
            printf(" part%d absent", i);
    }
    printf(", dirty %u\n", text.dirty);
    Editor_HostDocTextFree(&text);
    return 0;
}

/** DOC_CREATE: a new square, as the picker's New button sends it. */
static int
cmd_new(
    struct EditorHost* host,
    int map_x,
    int map_z,
    char const* storage,
    char const* template_name,
    char const* source)
{
    struct EditorHost_DocCreate request;
    enum EditorHost_Status status;

    memset(&request, 0, sizeof(request));
    request.kind = EDITOR_DOC_SQUARE;
    Editor_SquareKey(request.key, map_x, map_z);
    snprintf(request.storage, sizeof(request.storage), "%s", storage ? storage : "");
    request.template_kind = EDITOR_DOC_TEMPLATE_EMPTY;
    if( template_name && strcmp(template_name, "flat") == 0 )
    {
        request.template_kind = EDITOR_DOC_TEMPLATE_FLAT;
        request.fill_height = source ? atoi(source) : 0;
        request.fill_underlay = 1;
    }
    else if( template_name && strcmp(template_name, "copy") == 0 && source )
    {
        request.template_kind = EDITOR_DOC_TEMPLATE_COPY;
        snprintf(request.source_key, sizeof(request.source_key), "%s", source);
    }
    status = Editor_HostMapEdDocCreate(host, &request);
    if( status != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: create %s refused (status %d)\n", request.key,
            (int)status);
        return 1;
    }
    printf("created %s in %s\n", request.key, request.storage[0] ? request.storage : "maps");
    return 0;
}

/**
 * Stream facts until the deadline or an interrupt.
 *
 * Polls rather than blocks: DrainFacts is a poll by contract (a client's frame
 * loop must never wait on it), so a watcher sleeps between drains instead of
 * asking the transport for a blocking read it does not offer.
 */
static int
cmd_watch(
    struct EditorHost* host,
    int seconds)
{
    time_t const started = time(NULL);

    printf("watching (Ctrl-C to stop)\n");
    fflush(stdout);
    while( !g_stop )
    {
        Editor_HostMapEdDrainFacts(host, &k_printing_sink);
        if( seconds > 0 && difftime(time(NULL), started) >= seconds )
            break;
#if !defined(_WIN32)
        usleep(50 * 1000);
#endif
    }
    return 0;
}

/* ---- documents ------------------------------------------------------------- */

/** TEXT_SPLICE: the first `find` in the document becomes `replace`. */
static int
cmd_text_replace(
    struct EditorHost* host,
    char const* key,
    char const* find,
    char const* replace)
{
    struct EditorHost_DocText text;
    struct Editor_Cmd splice;
    char const* body;
    char const* at;
    size_t find_length = strlen(find);
    size_t replace_length = strlen(replace);
    enum EditorHost_Status status;

    memset(&text, 0, sizeof(text));
    if( Editor_HostMapEdDocOpen(host, EDITOR_DOC_TEXT, key, &text) != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: cannot open %s\n", key);
        return 1;
    }
    body = text.parts[0].data ? (char const*)text.parts[0].data : "";
    at = find_length > 0 ? strstr(body, find) : NULL;
    if( !at )
    {
        fprintf(stderr, "torirsmapedctl: '%s' is not in %s\n", find, key);
        Editor_HostDocTextFree(&text);
        return 1;
    }
    if( find_length + replace_length > EDITOR_CMD_SPLICE_MAX )
    {
        fprintf(stderr, "torirsmapedctl: a splice holds %d bytes\n", (int)EDITOR_CMD_SPLICE_MAX);
        Editor_HostDocTextFree(&text);
        return 1;
    }
    Editor_CmdInit(&splice, EDITOR_DOC_TEXT, key, EDITOR_CMD_TEXT_SPLICE);
    splice.u.text_splice.offset = (uint32_t)(at - body);
    splice.u.text_splice.before_len = (uint32_t)find_length;
    splice.u.text_splice.after_len = (uint32_t)replace_length;
    memcpy(splice.u.text_splice.text, find, find_length);
    memcpy(splice.u.text_splice.text + find_length, replace, replace_length);
    Editor_HostDocTextFree(&text);
    status = Editor_HostMapEdCmd(host, &splice);
    if( status != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: the splice was refused (status %d)\n", (int)status);
        return 1;
    }
    printf("spliced %s at %u: '%s' -> '%s'\n", key, splice.u.text_splice.offset, find, replace);
    return 0;
}

/** SAVE_AS on a config record: "<type>:<name>" -> "<type>:<new_name>". */
static int
cmd_cfg_copy(
    struct EditorHost* host,
    char const* type,
    char const* name,
    char const* new_name)
{
    char key[EDITOR_DOC_KEY_MAX];
    char new_key[EDITOR_DOC_KEY_MAX];
    enum EditorHost_Status status;
    int id = -1;
    int written;

    written = snprintf(key, sizeof(key), "%s:%s", type, name);
    if( written <= 0 || (size_t)written >= sizeof(key) )
    {
        fprintf(stderr, "torirsmapedctl: %s:%s is too long\n", type, name);
        return 1;
    }
    written = snprintf(new_key, sizeof(new_key), "%s:%s", type, new_name);
    if( written <= 0 || (size_t)written >= sizeof(new_key) )
    {
        fprintf(stderr, "torirsmapedctl: %s:%s is too long\n", type, new_name);
        return 1;
    }
    status = Editor_HostMapEdSaveAs(host, EDITOR_DOC_CFG, key, new_key, &id);
    if( status != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: copying %s to %s was refused (status %d)\n", key, new_key,
            (int)status);
        return 1;
    }
    printf("copied %s to %s, id %d\n", key, new_key, id);
    return 0;
}

/** One `.jm2` tile line's tokens (`h<n> o<id>;<shape>;<rot> f<n> u<n>`), the
 *  grammar editor_jm2.c parses. 0 for a token it does not know. */
static int
tile_parse_tokens(
    char const* tokens,
    struct Editor_Tile* tile)
{
    char const* p = tokens;

    memset(tile, 0, sizeof(*tile));
    while( *p && *p != '\n' )
    {
        char kind;
        unsigned long value;
        char* end;

        while( *p == ' ' )
            p++;
        if( !*p || *p == '\n' )
            break;
        kind = *p++;
        value = strtoul(p, &end, 10);
        if( end == p )
            return 0;
        p = end;
        if( kind == 'h' )
        {
            tile->has_height = 1;
            tile->height = (uint8_t)value;
        }
        else if( kind == 'o' )
        {
            tile->has_overlay = 1;
            tile->overlay_id = (uint16_t)value;
            if( *p == ';' )
            {
                tile->shape = (uint8_t)strtoul(p + 1, &end, 10);
                p = end;
                if( *p == ';' )
                {
                    tile->rotation = (uint8_t)strtoul(p + 1, &end, 10);
                    p = end;
                }
            }
        }
        else if( kind == 'f' )
            tile->settings = (uint8_t)value;
        else if( kind == 'u' )
            tile->underlay_id = (uint8_t)value;
        else
            return 0;
    }
    return 1;
}

/** TILE: square m<mx>_<mz>'s tile (level, lx, lz) with its raw height byte set
 *  to `height` (1 spells an explicit zero, as the format does). */
static int
cmd_tile_height(
    struct EditorHost* host,
    int map_x,
    int map_z,
    int level,
    int lx,
    int lz,
    int height)
{
    struct EditorHost_DocText text;
    struct Editor_Cmd command;
    char key[EDITOR_DOC_KEY_MAX];
    char prefix[32];
    char const* body;
    char const* line = NULL;
    enum EditorHost_Status status;

    if( level < 0 || level > 3 || lx < 0 || lx > 63 || lz < 0 || lz > 63 || height < 1 || height > 255 )
    {
        fprintf(stderr, "torirsmapedctl: tile-height wants level 0..3, a local tile 0..63 and a "
                        "height 1..255\n");
        return 1;
    }
    Editor_SquareKey(key, map_x, map_z);
    memset(&text, 0, sizeof(text));
    if( Editor_HostMapEdDocOpen(host, EDITOR_DOC_SQUARE, key, &text) != EDITOR_HOST_OK
        || !text.parts[0].data )
    {
        fprintf(stderr, "torirsmapedctl: cannot open %s\n", key);
        Editor_HostDocTextFree(&text);
        return 1;
    }
    body = (char const*)text.parts[0].data;
    snprintf(prefix, sizeof(prefix), "%d %d %d:", level, lx, lz);
    for( char const* p = body; p && *p; )
    {
        if( strncmp(p, prefix, strlen(prefix)) == 0 )
        {
            line = p + strlen(prefix);
            break;
        }
        p = strchr(p, '\n');
        if( p )
            p++;
    }
    Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, map_x, map_z);
    command.u.tile.level = level;
    command.u.tile.x = lx;
    command.u.tile.z = lz;
    /* No line: the all-default tile. */
    if( line && !tile_parse_tokens(line, &command.u.tile.before) )
    {
        fprintf(stderr, "torirsmapedctl: %s's line '%s' does not parse\n", key, prefix);
        Editor_HostDocTextFree(&text);
        return 1;
    }
    if( !line )
        memset(&command.u.tile.before, 0, sizeof(command.u.tile.before));
    Editor_HostDocTextFree(&text);
    command.u.tile.after = command.u.tile.before;
    command.u.tile.after.has_height = 1;
    command.u.tile.after.height = (uint8_t)height;
    status = Editor_HostMapEdCmd(host, &command);
    if( status != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: the tile edit was refused (status %d)\n", (int)status);
        return 1;
    }
    printf("%s tile %d %d %d: height %s%d -> %d\n", key, level, lx, lz,
        command.u.tile.before.has_height ? "" : "(none) ", command.u.tile.before.height, height);
    return 0;
}

/* ---- spawn rows ------------------------------------------------------------ */

struct ctl_spawn_row
{
    int is_obj;
    int suppress;
    char name[EDITOR_CMD_NAME_MAX];
    int x;
    int z;
    int level;
    int count;
};

/** `<kind> <name> <x> <z> <level> [count]` from args[*at]. Returns 1 and
 *  advances *at, 0 with a message. */
static int
ctl_parse_spawn_row(
    char const* const* args,
    int arg_count,
    int* at,
    struct ctl_spawn_row* out)
{
    char const* kind;
    int i = *at;

    memset(out, 0, sizeof(*out));
    if( arg_count - i < 5 )
    {
        fprintf(stderr, "torirsmapedctl: a spawn row is <kind> <name> <x> <z> <level> [count]\n");
        return 0;
    }
    kind = args[i++];
    out->suppress = kind[0] == '-';
    if( out->suppress )
        kind++;
    if( strcmp(kind, "npc") == 0 )
        out->is_obj = 0;
    else if( strcmp(kind, "obj") == 0 )
        out->is_obj = 1;
    else
    {
        fprintf(stderr, "torirsmapedctl: spawn kind '%s' is not npc, obj, -npc or -obj\n", args[i - 1]);
        return 0;
    }
    if( strlen(args[i]) >= sizeof(out->name) || !args[i][0] || strchr(args[i], ' ') )
    {
        fprintf(stderr, "torirsmapedctl: '%s' is not a spawn name\n", args[i]);
        return 0;
    }
    snprintf(out->name, sizeof(out->name), "%s", args[i++]);
    out->x = atoi(args[i++]);
    out->z = atoi(args[i++]);
    out->level = atoi(args[i++]);
    out->count = 1;
    if( out->is_obj && i < arg_count && strcmp(args[i], "to") != 0 )
        out->count = atoi(args[i++]);
    if( out->x < 0 || out->z < 0 || out->level < 0 || out->level > 3 || out->count < 1 )
    {
        fprintf(stderr, "torirsmapedctl: a spawn row wants tiles >= 0, level 0..3, count >= 1\n");
        return 0;
    }
    *at = i;
    return 1;
}

static void
ctl_spawn_key(
    const struct ctl_spawn_row* row,
    char out[EDITOR_DOC_KEY_MAX])
{
    Editor_SquareKey(out, row->x / EDITOR_SQUARE_X, row->z / EDITOR_SQUARE_Z);
}

static void
ctl_spawn_cmd_row(
    const struct ctl_spawn_row* row,
    struct Editor_CmdSpawnRow* out)
{
    memset(out, 0, sizeof(*out));
    out->is_obj = row->is_obj;
    out->suppress = row->suppress;
    snprintf(out->name, sizeof(out->name), "%s", row->name);
    out->abs_x = row->x;
    out->abs_z = row->z;
    out->level = row->level;
    out->count = row->is_obj ? row->count : 1;
}

/** A SPAWN / SPAWN_SUPPRESS add (adding) or delete of `row`. */
static void
ctl_spawn_add_delete(
    struct Editor_Cmd* out,
    const struct ctl_spawn_row* row,
    int adding)
{
    char key[EDITOR_DOC_KEY_MAX];

    ctl_spawn_key(row, key);
    Editor_CmdInit(out, EDITOR_DOC_SPAWN, key, row->suppress ? EDITOR_CMD_SPAWN_SUPPRESS : EDITOR_CMD_SPAWN);
    out->has_before = !adding;
    out->has_after = adding;
    out->u.spawn.is_obj = row->is_obj;
    snprintf(out->u.spawn.name, sizeof(out->u.spawn.name), "%s", row->name);
    out->u.spawn.abs_x = row->x;
    out->u.spawn.abs_z = row->z;
    out->u.spawn.level = row->level;
    out->u.spawn.count = row->is_obj ? row->count : 1;
}

/** The edited document's line for `row`, for SPAWN_EDIT's byte-exact undo:
 *  the first line whose tokens are the row's. "" when there is none. */
static void
ctl_spawn_line_text(
    char const* body,
    const struct ctl_spawn_row* row,
    char* out,
    size_t out_size)
{
    int in_obj = -1;

    out[0] = '\0';
    for( char const* p = body; p && *p; )
    {
        char const* end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        char line[512];
        char name[EDITOR_CMD_NAME_MAX + 2];
        int x;
        int z;
        int level;
        int count = 1;
        int fields;

        snprintf(line, sizeof(line), "%.*s", (int)(length < sizeof(line) ? length : sizeof(line) - 1), p);
        if( strncmp(line, "====", 4) == 0 )
            in_obj = strstr(line, "OBJ") != NULL;
        else if( in_obj == row->is_obj )
        {
            char* comment = strstr(line, "//");
            if( comment )
                *comment = '\0';
            fields = sscanf(line, "%65s %d %d %d %d", name, &x, &z, &level, &count);
            if( fields >= 4 && (name[0] == '-') == row->suppress
                && strcmp(name + (row->suppress ? 1 : 0), row->name) == 0 && x == row->x && z == row->z
                && level == row->level && (!row->is_obj || count == row->count) && length < out_size )
            {
                snprintf(out, out_size, "%.*s", (int)length, p);
                return;
            }
        }
        p = end ? end + 1 : NULL;
    }
}

static int
ctl_spawn_rows_equal(
    const struct ctl_spawn_row* a,
    const struct ctl_spawn_row* b)
{
    return a->is_obj == b->is_obj && a->suppress == b->suppress && strcmp(a->name, b->name) == 0
           && a->x == b->x && a->z == b->z && a->level == b->level && (!a->is_obj || a->count == b->count);
}

/**
 * spawn-edit / spawn-move: the commands the Spawns panel's edit form makes
 * (Editor_SpawnEditPlan, editor_spawn.h), built here without the codec --
 * this binary links no document.
 */
static int
cmd_spawn_edit(
    struct EditorHost* host,
    int move_only,
    char const* const* args,
    int arg_count)
{
    struct ctl_spawn_row old_row;
    struct ctl_spawn_row new_row;
    struct Editor_Cmd commands[2];
    struct EditorHost_DocText text;
    char old_key[EDITOR_DOC_KEY_MAX];
    char new_key[EDITOR_DOC_KEY_MAX];
    char kind_arg[16];
    char const* local_args[16];
    int generated = 0;
    int count = 0;
    int at = 0;
    int failed = 0;

    if( arg_count < 1 )
    {
        fprintf(stderr, "torirsmapedctl: %s <row> to ...\n", move_only ? "spawn-move" : "spawn-edit");
        return 1;
    }
    for( int i = 0; i < arg_count; i++ )
        local_args[i] = args[i];
    if( strncmp(args[0], "gen:", 4) == 0 )
    {
        generated = 1;
        snprintf(kind_arg, sizeof(kind_arg), "%s", args[0] + 4);
        local_args[0] = kind_arg;
    }
    if( !ctl_parse_spawn_row(local_args, arg_count, &at, &old_row) )
        return 1;
    if( at >= arg_count || strcmp(local_args[at], "to") != 0 )
    {
        fprintf(stderr, "torirsmapedctl: expected `to` after the old row\n");
        return 1;
    }
    at++;
    if( move_only )
    {
        if( arg_count - at < 2 )
        {
            fprintf(stderr, "torirsmapedctl: spawn-move <row> to <x> <z> [level]\n");
            return 1;
        }
        new_row = old_row;
        new_row.x = atoi(local_args[at]);
        new_row.z = atoi(local_args[at + 1]);
        if( arg_count - at >= 3 )
            new_row.level = atoi(local_args[at + 2]);
        if( new_row.x < 0 || new_row.z < 0 || new_row.level < 0 || new_row.level > 3 )
        {
            fprintf(stderr, "torirsmapedctl: spawn-move wants tiles >= 0 and level 0..3\n");
            return 1;
        }
    }
    else if( !ctl_parse_spawn_row(local_args, arg_count, &at, &new_row) )
        return 1;
    if( generated && old_row.suppress )
    {
        fprintf(stderr, "torirsmapedctl: a generated row is never a suppression row\n");
        return 1;
    }
    if( ctl_spawn_rows_equal(&old_row, &new_row) )
    {
        printf("nothing to change\n");
        return 0;
    }

    ctl_spawn_key(&old_row, old_key);
    ctl_spawn_key(&new_row, new_key);
    memset(&text, 0, sizeof(text));
    if( Editor_HostMapEdDocOpen(host, EDITOR_DOC_SPAWN, old_key, &text) != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: cannot open the spawn document %s\n", old_key);
        return 1;
    }
    if( strcmp(old_key, new_key) != 0 )
    {
        struct EditorHost_DocText other;
        memset(&other, 0, sizeof(other));
        if( Editor_HostMapEdDocOpen(host, EDITOR_DOC_SPAWN, new_key, &other) != EDITOR_HOST_OK )
        {
            fprintf(stderr, "torirsmapedctl: cannot open the spawn document %s\n", new_key);
            Editor_HostDocTextFree(&text);
            return 1;
        }
        Editor_HostDocTextFree(&other);
    }

    if( generated )
    {
        /* The generated file is never written: suppress, then add. */
        struct ctl_spawn_row mark = old_row;
        mark.suppress = 1;
        ctl_spawn_add_delete(&commands[count++], &mark, 1);
        new_row.suppress = 0;
        ctl_spawn_add_delete(&commands[count++], &new_row, 1);
    }
    else if( old_row.is_obj == new_row.is_obj && strcmp(old_key, new_key) == 0 )
    {
        Editor_CmdInit(&commands[0], EDITOR_DOC_SPAWN, old_key, EDITOR_CMD_SPAWN_EDIT);
        commands[0].has_before = 1;
        commands[0].has_after = 1;
        ctl_spawn_cmd_row(&old_row, &commands[0].u.spawn_edit.before);
        ctl_spawn_cmd_row(&new_row, &commands[0].u.spawn_edit.after);
        ctl_spawn_line_text(text.parts[0].data ? (char const*)text.parts[0].data : "", &old_row,
            commands[0].u.spawn_edit.before_text, sizeof(commands[0].u.spawn_edit.before_text));
        count = 1;
    }
    else
    {
        ctl_spawn_add_delete(&commands[count++], &old_row, 0);
        ctl_spawn_add_delete(&commands[count++], &new_row, 1);
    }
    Editor_HostDocTextFree(&text);

    if( Editor_HostMapEdStroke(host, 1) != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: the authority refused a stroke\n");
        return 1;
    }
    for( int i = 0; i < count && !failed; i++ )
    {
        enum EditorHost_Status status = Editor_HostMapEdCmd(host, &commands[i]);
        if( status != EDITOR_HOST_OK )
        {
            fprintf(stderr, "torirsmapedctl: the spawn edit was refused (status %d): is the old row in "
                            "%s's edited file?\n", (int)status, old_key);
            failed = 1;
        }
    }
    (void)Editor_HostMapEdStroke(host, 0);
    if( failed )
    {
        /* A half-applied step is worse than none. */
        if( count > 1 )
            (void)Editor_HostMapEdUndo(host);
        return 1;
    }
    printf("%s %s %s %d %d %d -> %s %d %d %d (%d command%s, one undo step)\n",
        move_only ? "moved" : "edited", old_row.is_obj ? "obj" : "npc", old_row.name, old_row.x, old_row.z,
        old_row.level, new_row.name, new_row.x, new_row.z, new_row.level, count, count == 1 ? "" : "s");
    return 0;
}

/** cfg-set: one CFG_FIELD on a record block (all.<type> or an overlay). */
static int
cmd_cfg_set(
    struct EditorHost* host,
    char const* key,
    char const* spec,
    char const* value)
{
    struct EditorHost_DocText text;
    struct Editor_Cmd command;
    char field[EDITOR_CMD_NAME_MAX];
    char before[EDITOR_CMD_VALUE_MAX];
    char const* body;
    enum EditorHost_Status status;
    int occurrence = 0;
    int seen = 0;
    char const* hash;
    char const* at_sign;
    size_t field_length;

    if( strlen(spec) >= sizeof(field) || strlen(value) >= EDITOR_CMD_VALUE_MAX || strchr(value, '\n') )
    {
        fprintf(stderr, "torirsmapedctl: the field spec or value is too long\n");
        return 1;
    }
    /* The key part of `key[#n][@line]`, and its occurrence. */
    hash = strchr(spec, '#');
    at_sign = strchr(spec, '@');
    field_length = hash ? (size_t)(hash - spec) : at_sign ? (size_t)(at_sign - spec) : strlen(spec);
    snprintf(field, sizeof(field), "%.*s", (int)field_length, spec);
    if( hash )
        occurrence = atoi(hash + 1);

    memset(&text, 0, sizeof(text));
    if( Editor_HostMapEdDocOpen(host, EDITOR_DOC_CFG, key, &text) != EDITOR_HOST_OK || !text.parts[0].data )
    {
        fprintf(stderr, "torirsmapedctl: cannot open the record %s\n", key);
        Editor_HostDocTextFree(&text);
        return 1;
    }
    /* The current value of that occurrence ("" = absent). */
    before[0] = '\0';
    body = (char const*)text.parts[0].data;
    {
    int line_index = 0;
    int found_line = -1;
    for( char const* p = body; p && *p; line_index++ )
    {
        char const* end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        if( length > 0 && p[length - 1] == '\r' )
            length--;
        if( length > field_length && memcmp(p, field, field_length) == 0 && p[field_length] == '=' )
        {
            if( seen++ == occurrence )
            {
                size_t v = length - field_length - 1;
                if( v >= sizeof(before) )
                    v = sizeof(before) - 1;
                memcpy(before, p + field_length + 1, v);
                before[v] = '\0';
                found_line = line_index;
                break;
            }
        }
        p = end ? end + 1 : NULL;
    }
    /* A removal names its line, so the undo puts it back there. */
    if( strcmp(value, "-") == 0 && found_line >= 0 && !at_sign )
    {
        char positioned[EDITOR_CMD_NAME_MAX];
        if( snprintf(positioned, sizeof(positioned), "%s#%d@%d", field, occurrence, found_line)
            < (int)sizeof(positioned) )
            snprintf(field, sizeof(field), "%s", positioned);
        spec = field;
    }
    }
    Editor_HostDocTextFree(&text);

    Editor_CmdInit(&command, EDITOR_DOC_CFG, key, EDITOR_CMD_CFG_FIELD);
    command.has_before = 1;
    command.has_after = 1;
    snprintf(command.u.cfg_field.key, sizeof(command.u.cfg_field.key), "%s", spec);
    snprintf(command.u.cfg_field.before, sizeof(command.u.cfg_field.before), "%s", before);
    snprintf(command.u.cfg_field.after, sizeof(command.u.cfg_field.after), "%s", strcmp(value, "-") == 0 ? "" : value);
    status = Editor_HostMapEdCmd(host, &command);
    if( status != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: the field edit was refused (status %d)\n", (int)status);
        return 1;
    }
    printf("%s %s: '%s' -> '%s'\n", key, spec, before, command.u.cfg_field.after);
    return 0;
}

/* ---- builds, RUN and the live link ----------------------------------------- */

static const struct EditorHostMapEdFacts k_quiet_sink = { 0 };

/** The outcome a live wait is for. */
struct live_wait
{
    /** FACT_LIVE_STATUS: wait for ATTACHED/FAILED (attach), DETACHED (detach). */
    int want_status;
    /** FACT_BUILD_DONE (build) or FACT_RUN_DONE (run). */
    uint32_t want_fact;
    int done;
    int ok;
};

/** Print one live-editor fact; fold it into `wait`. */
static void
live_print(
    uint32_t type,
    const uint8_t* body,
    uint32_t length,
    struct live_wait* wait)
{
    struct ToriRSMapEdReader reader;
    char text[1024];

    ToriRSMapEd_ReaderInit(&reader, body, length);
    if( type == TORIRSMAPED_FACT_BUILD_LINE )
    {
        uint32_t what = ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        printf("  [build %u] %s\n", what, text);
    }
    else if( type == TORIRSMAPED_FACT_BUILD_DONE )
    {
        uint32_t what = ToriRSMapEd_GetU32(&reader);
        uint32_t status = ToriRSMapEd_GetU32(&reader);
        uint32_t units = ToriRSMapEd_GetU32(&reader);
        printf("build %u done: status %u, %u unit(s)\n", what, status, units);
        if( wait->want_fact == TORIRSMAPED_FACT_BUILD_DONE )
        {
            wait->done = 1;
            wait->ok = status == EDITOR_HOST_OK;
        }
    }
    else if( type == TORIRSMAPED_FACT_RUN_DONE )
    {
        uint32_t status = ToriRSMapEd_GetU32(&reader);
        uint32_t step = ToriRSMapEd_GetU32(&reader);
        uint32_t counts[5];
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        for( int i = 0; i < 5; i++ )
            counts[i] = ToriRSMapEd_GetU32(&reader);
        printf("RUN done: status %u, step %u, '%s'; scripts_swapped %u groups_replaced %u "
               "squares_reloaded %u spawns_changed %u restart %u\n",
            status, step, text, counts[0], counts[1], counts[2], counts[3], counts[4]);
        for( uint32_t i = 0; i < counts[4] && !reader.bad; i++ )
        {
            ToriRSMapEd_GetString(&reader, text, sizeof(text));
            printf("  restart needed: %s\n", text);
        }
        if( wait->want_fact == TORIRSMAPED_FACT_RUN_DONE )
        {
            wait->done = 1;
            wait->ok = status == EDITOR_HOST_OK && step == TORIRSMAPED_RUN_STEP_DONE;
        }
    }
    else if( type == TORIRSMAPED_FACT_LIVE_STATUS )
    {
        uint32_t state = ToriRSMapEd_GetU32(&reader);
        uint32_t launched = ToriRSMapEd_GetU32(&reader);
        char address[256];
        char name[128];
        char stamp[256];
        uint32_t players;
        uint32_t tick;
        uint32_t stale;
        ToriRSMapEd_GetString(&reader, address, sizeof(address));
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        players = ToriRSMapEd_GetU32(&reader);
        tick = ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, stamp, sizeof(stamp));
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        stale = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < stale && !reader.bad; i++ )
            ToriRSMapEd_GetString(&reader, text, sizeof(text));
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        printf("live: state %u%s %s '%s' players %u tick %u script '%s' %s\n", state,
            launched ? " (launched)" : "", address, name, players, tick, stamp, text);
        if( wait->want_status == 1 && (state == TORIRSMAPED_LIVE_ATTACHED || state == TORIRSMAPED_LIVE_FAILED) )
        {
            wait->done = 1;
            wait->ok = state == TORIRSMAPED_LIVE_ATTACHED;
        }
        else if( wait->want_status == 2 && state == TORIRSMAPED_LIVE_DETACHED )
        {
            wait->done = 1;
            wait->ok = 1;
        }
    }
    else if( type == TORIRSMAPED_FACT_LIVE_LINE )
    {
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        printf("  [live] %s\n", text);
    }
    fflush(stdout);
}

/** Send one live request and print facts until `wait` is answered or the
 *  deadline passes. */
static int
live_request(
    struct EditorHost* host,
    uint32_t type,
    struct ToriRSMapEdBuf* payload,
    struct live_wait* wait,
    int seconds)
{
    time_t const started = time(NULL);
    int sent = Editor_HostMapEdLiveSend(host, type, payload->data ? payload->data + payload->head : NULL,
        (uint32_t)ToriRSMapEd_BufAvailable(payload));

    ToriRSMapEd_BufFree(payload);
    if( !sent )
    {
        fprintf(stderr, "torirsmapedctl: the connection is gone\n");
        return 1;
    }
    while( !g_stop && !wait->done )
    {
        uint32_t fact_type;
        uint8_t* body;
        uint32_t length;

        Editor_HostMapEdDrainFacts(host, &k_quiet_sink);
        while( Editor_HostMapEdLiveTake(host, &fact_type, &body, &length) )
        {
            live_print(fact_type, body, length, wait);
            ToriMem_Free(ToriMem_Editor(), body);
        }
        if( wait->done )
            break;
        if( difftime(time(NULL), started) >= seconds )
        {
            fprintf(stderr, "torirsmapedctl: no answer in %d s\n", seconds);
            return 1;
        }
#if !defined(_WIN32)
        usleep(20 * 1000);
#endif
    }
    return wait->ok ? 0 : 1;
}

int
main(
    int argc,
    char** argv)
{
    char const* host_name = "localhost";
    int port = TORIRSMAPED_DEFAULT_PORT;
    uint32_t join_group = 0;
    struct EditorHost host;
    char const* command = NULL;
    char const* args[16] = { NULL };
    int arg_count = 0;
    int result = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--host") == 0 && i + 1 < argc )
            host_name = argv[++i];
        else if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
            port = atoi(argv[++i]);
        else if( strcmp(argv[i], "--client") == 0 && i + 1 < argc )
            join_group = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if( !command )
            command = argv[i];
        else if( arg_count < (int)(sizeof(args) / sizeof(args[0])) )
            args[arg_count++] = argv[i];
        else
        {
            fprintf(stderr, "torirsmapedctl: too many arguments\n");
            return 1;
        }
    }
    if( !command )
    {
        fprintf(
            stderr,
            "usage: torirsmapedctl [--host h] [--port n] [--client <id>] <command>\n"
            "  watch [seconds] | status | squares [limit] | open <x> <z>\n"
            "  select <x> <z> <level> | tool <n> | sync | undo | redo | save | bake\n"
            "  doc <square|spawn|cfg|iface|text> <key>\n"
            "  new <x> <z> [storage] [empty|flat <height>|copy <key>]\n"
            "  text-replace <key> <find> <replace> | cfg-copy <type> <name> <new_name>\n"
            "  tile-height <mx> <mz> <level> <lx> <lz> <height>\n"
            "  spawn-edit [gen:]<kind> <name> <x> <z> <level> [count] to <kind> <name> <x> <z> <level> [count]\n"
            "  spawn-move [gen:]<kind> <name> <x> <z> <level> [count] to <x> <z> [level]\n"
            "    (kind npc|obj|-npc|-obj; name a pack symbol or #<id>; absolute tiles)\n"
            "  cfg-set <type>:<name>[@<overlay under server/scripts>] <key[#n][@line]> <value|->\n"
            "  live-attach launch|<host:port> [ticket] | live-detach | live-cheat <player> <line>\n"
            "  build packs|client [seconds] | run [seconds]\n");
        return 1;
    }

#if !defined(_WIN32)
    signal(SIGINT, on_interrupt);
    signal(SIGPIPE, SIG_IGN);
#endif

    if( !Editor_HostOpenMapEdTcp(
            &host, host_name, port, TORIRSMAPED_ROLE_CONTROLLER, join_group) )
        return 1;

    printf(
        "connected to %s:%d as client %u (%s)\n",
        host_name,
        port,
        Editor_HostMapEdClientId(&host),
        Editor_HostMapEdWritable(&host) ? "writable" : "read-only");

    if( strcmp(command, "watch") == 0 )
        result = cmd_watch(&host, arg_count > 0 ? atoi(args[0]) : 0);
    else if( strcmp(command, "status") == 0 )
    {
        int coords[1];
        int count = 0;
        host.vtable->square_list(host.user_data, coords, 0, &count);
        printf("protocol %d, %d square(s) in the tree\n", TORIRSMAPED_PROTO_VERSION, count);
    }
    else if( strcmp(command, "squares") == 0 )
        result = cmd_squares(&host, arg_count > 0 ? atoi(args[0]) : 20);
    else if( strcmp(command, "open") == 0 && arg_count >= 2 )
        result = cmd_open(&host, atoi(args[0]), atoi(args[1]));
    else if( strcmp(command, "doc") == 0 && arg_count >= 2 )
        result = cmd_doc(&host, args[0], args[1]);
    else if( strcmp(command, "new") == 0 && arg_count >= 2 )
        result = cmd_new(&host, atoi(args[0]), atoi(args[1]), args[2], args[3], args[4]);
    else if( strcmp(command, "select") == 0 && arg_count >= 3 )
    {
        /* Values per TORIRSMAPED_STATE_SELECTION: kind 1 is terrain, and the
         * tile is ABSOLUTE — a controller has no scene to measure against. */
        int32_t values[7] = { 1, atoi(args[0]), atoi(args[1]), atoi(args[2]), -1, -1, -1 };
        result = Editor_HostMapEdStateSet(&host, TORIRSMAPED_STATE_SELECTION, values, 7)
                     ? 0
                     : 1;
        if( !result )
            printf("published selection %s %s level %s\n", args[0], args[1], args[2]);
    }
    else if( strcmp(command, "tool") == 0 && arg_count >= 1 )
    {
        int32_t values[1] = { atoi(args[0]) };
        result =
            Editor_HostMapEdStateSet(&host, TORIRSMAPED_STATE_TOOL, values, 1) ? 0 : 1;
        if( !result )
            printf("published tool %s\n", args[0]);
    }
    else if( strcmp(command, "sync") == 0 )
    {
        int keys = Editor_HostMapEdStateSync(&host);
        if( keys < 0 )
            result = 1;
        else
        {
            printf("%d state key(s) in this Client:\n", keys);
            Editor_HostMapEdDrainFacts(&host, &k_printing_sink);
        }
    }
    else if( strcmp(command, "undo") == 0 || strcmp(command, "redo") == 0 )
    {
        int count = strcmp(command, "undo") == 0 ? Editor_HostMapEdUndo(&host)
                                                 : Editor_HostMapEdRedo(&host);
        printf("%s %d command(s)\n", command, count);
        Editor_HostMapEdDrainFacts(&host, &k_printing_sink);
    }
    else if( strcmp(command, "save") == 0 )
    {
        int saved = Editor_HostMapEdSaveAll(&host);
        if( saved < 0 )
        {
            fprintf(stderr, "torirsmapedctl: the server cannot save (read-only)\n");
            result = 1;
        }
        else
        {
            printf("saved %d document(s)\n", saved);
            Editor_HostMapEdDrainFacts(&host, &k_printing_sink);
        }
    }
    else if( strcmp(command, "text-replace") == 0 && arg_count >= 3 )
        result = cmd_text_replace(&host, args[0], args[1], args[2]);
    else if( strcmp(command, "cfg-copy") == 0 && arg_count >= 3 )
        result = cmd_cfg_copy(&host, args[0], args[1], args[2]);
    else if( strcmp(command, "tile-height") == 0 && arg_count >= 6 )
        result = cmd_tile_height(&host, atoi(args[0]), atoi(args[1]), atoi(args[2]),
            atoi(args[3]), atoi(args[4]), atoi(args[5]));
    else if( strcmp(command, "spawn-edit") == 0 || strcmp(command, "spawn-move") == 0 )
        result = cmd_spawn_edit(&host, strcmp(command, "spawn-move") == 0, args, arg_count);
    else if( strcmp(command, "cfg-set") == 0 && arg_count >= 3 )
        result = cmd_cfg_set(&host, args[0], args[1], args[2]);
    else if( strcmp(command, "live-attach") == 0 && arg_count >= 1 )
    {
        struct ToriRSMapEdBuf payload = { 0 };
        struct live_wait wait = { 1, 0, 0, 0 };
        int launch = strcmp(args[0], "launch") == 0;
        ToriRSMapEd_PutU32(&payload, launch ? 1u : 0u);
        ToriRSMapEd_PutString(&payload, launch ? "" : args[0]);
        ToriRSMapEd_PutString(&payload, arg_count >= 2 ? args[1] : "");
        result = live_request(&host, TORIRSMAPED_REQ_LIVE_ATTACH, &payload, &wait, 90);
    }
    else if( strcmp(command, "live-detach") == 0 )
    {
        struct ToriRSMapEdBuf payload = { 0 };
        struct live_wait wait = { 2, 0, 0, 0 };
        result = live_request(&host, TORIRSMAPED_REQ_LIVE_DETACH, &payload, &wait, 30);
    }
    else if( strcmp(command, "live-cheat") == 0 && arg_count >= 2 )
    {
        struct ToriRSMapEdBuf payload = { 0 };
        /* Answered by lines, then a status. */
        struct live_wait wait = { 1, 0, 0, 0 };
        ToriRSMapEd_PutString(&payload, args[0]);
        ToriRSMapEd_PutString(&payload, args[1]);
        result = live_request(&host, TORIRSMAPED_REQ_LIVE_CHEAT, &payload, &wait, 30);
    }
    else if( strcmp(command, "build") == 0 && arg_count >= 1 )
    {
        struct ToriRSMapEdBuf payload = { 0 };
        struct live_wait wait = { 0, TORIRSMAPED_FACT_BUILD_DONE, 0, 0 };
        ToriRSMapEd_PutU32(&payload, strcmp(args[0], "client") == 0 ? TORIRSMAPED_BUILD_CLIENT_CACHE
                                                                     : TORIRSMAPED_BUILD_PACKS);
        result = live_request(&host, TORIRSMAPED_REQ_BUILD, &payload, &wait,
            arg_count >= 2 ? atoi(args[1]) : 1800);
    }
    else if( strcmp(command, "run") == 0 )
    {
        struct ToriRSMapEdBuf payload = { 0 };
        struct live_wait wait = { 0, TORIRSMAPED_FACT_RUN_DONE, 0, 0 };
        result = live_request(&host, TORIRSMAPED_REQ_RUN, &payload, &wait,
            arg_count >= 1 ? atoi(args[0]) : 1800);
    }
    else if( strcmp(command, "bake") == 0 )
        result = host.vtable->bake(host.user_data, print_bake_line, NULL) == EDITOR_HOST_OK
                     ? 0
                     : 1;
    else
    {
        fprintf(stderr, "torirsmapedctl: unknown command '%s'\n", command);
        result = 1;
    }

    Editor_HostClose(&host);
    return result;
}
