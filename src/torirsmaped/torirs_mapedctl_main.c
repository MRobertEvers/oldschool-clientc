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
 *
 * Exit status is 0 when the command did what it said, 1 otherwise.
 */

#include "editor/editor_host.h"
#include "torirs_maped.h"

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

static void
print_cmd(
    void* user_data,
    uint32_t seq,
    int direction,
    const struct Editor_Cmd* command)
{
    (void)user_data;

    if( command->kind == EDITOR_CMD_TILE )
        printf(
            "cmd  #%u %-7s tile   m%d_%d level %d tile %d,%d\n",
            seq,
            direction == EDITOR_CMD_INVERSE ? "undone" : "applied",
            command->map_x,
            command->map_z,
            command->level,
            command->x,
            command->z);
    else
        printf(
            "cmd  #%u %-7s loc    m%d_%d %s%s\n",
            seq,
            direction == EDITOR_CMD_INVERSE ? "undone" : "applied",
            command->map_x,
            command->map_z,
            command->has_before ? "-" : "",
            command->has_after ? "+" : "");
    fflush(stdout);
}

static void
print_saved(
    void* user_data,
    int count,
    const int* coords)
{
    (void)user_data;

    printf("save %d square(s):", count);
    for( int i = 0; i < count; i++ )
        printf(" m%d_%d", coords[i * 2], coords[i * 2 + 1]);
    printf("\n");
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
    NULL, print_cmd, print_saved, print_state
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
    char const* args[4] = { NULL, NULL, NULL, NULL };
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
        else if( arg_count < 4 )
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
            "  select <x> <z> <level> | tool <n> | sync | undo | redo | save | bake\n");
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
            printf("saved %d square(s)\n", saved);
            Editor_HostMapEdDrainFacts(&host, &k_printing_sink);
        }
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
