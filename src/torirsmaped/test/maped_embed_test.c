/**
 * The whole ToriRSMapEd model with no socket, driven end to end through the
 * embed pipes:
 *
 *   - the server as DOCUMENT AUTHORITY: mirrors mutate only on FACT_CMD
 *     echoes, every connection sees every edit, history is shared, saves
 *     happen server-side and broadcast what landed;
 *   - CONNECTIONS vs CLIENTS: one process holds several connections; a
 *     Client is a group of them. Document facts cross Client boundaries,
 *     state facts do not — one Client's selection never reaches another;
 *   - the file layer underneath: square list, spawn save, the tree lock
 *     (now the server's own), atomic writes.
 *
 *   maped_embed_test <scratch-dir> <source-jm2> <source-jl2>
 *
 * Writes only inside the scratch directory it is given.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_jm2.h"
#include "torirsmaped/torirs_maped.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    char const* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static char*
slurp(
    char const* path,
    size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    data = malloc((size_t)size + 1);
    if( !data || fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

static void
copy_file(
    char const* from,
    char const* to)
{
    size_t size = 0;
    char* data = slurp(from, &size);
    FILE* f;
    if( !data )
    {
        fprintf(stderr, "cannot read %s\n", from);
        exit(2);
    }
    f = fopen(to, "wb");
    if( !f || fwrite(data, 1, size, f) != size )
    {
        fprintf(stderr, "cannot write %s\n", to);
        exit(2);
    }
    fclose(f);
    free(data);
}

/* ---- a mirror: what every real client keeps ------------------------------ */

struct mirror
{
    struct Editor_Doc* doc; /* NULL for a connection with no world */
    int cmds_seen;
    int saved_seen;
    int states_seen;
    uint32_t last_state_key;
    int32_t last_state_values[TORIRSMAPED_STATE_VALUES_MAX];
    int last_state_count;
};

static void
mirror_on_cmd(
    void* user_data,
    uint32_t seq,
    int direction,
    const struct Editor_Cmd* command)
{
    struct mirror* mirror = user_data;
    (void)seq;
    if( mirror->doc )
        Editor_CmdApply(
            mirror->doc,
            command,
            direction ? EDITOR_CMD_INVERSE : EDITOR_CMD_FORWARD);
    mirror->cmds_seen++;
}

static void
mirror_on_saved(
    void* user_data,
    int count,
    const int* coords)
{
    struct mirror* mirror = user_data;
    mirror->saved_seen += count;
    if( !mirror->doc )
        return;
    for( int i = 0; i < count; i++ )
    {
        struct Editor_Square* square =
            Editor_DocFindSquare(mirror->doc, coords[i * 2], coords[i * 2 + 1]);
        if( square )
        {
            square->dirty_map = 0;
            square->dirty_loc = 0;
        }
    }
}

static void
mirror_on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct mirror* mirror = user_data;
    mirror->states_seen++;
    mirror->last_state_key = key;
    mirror->last_state_count = count;
    for( int i = 0; i < count && i < TORIRSMAPED_STATE_VALUES_MAX; i++ )
        mirror->last_state_values[i] = values[i];
}

static int
drain(
    struct EditorHost* host,
    struct mirror* mirror)
{
    struct EditorHostMapEdFacts sink = {
        mirror, mirror_on_cmd, mirror_on_saved, mirror_on_state
    };
    return Editor_HostMapEdDrainFacts(host, &sink);
}

static int
open_square_into(
    struct EditorHost* host,
    struct Editor_Doc* doc,
    int map_x,
    int map_z)
{
    struct EditorHost_Blob jm2 = { NULL, 0 };
    struct EditorHost_Blob jl2 = { NULL, 0 };
    int dirty_map = 0;
    int dirty_loc = 0;
    struct Editor_Square* square;
    struct Editor_ParseResult result;

    if( Editor_HostMapEdSquareOpen(host, map_x, map_z, &jm2, &jl2, &dirty_map, &dirty_loc)
        != EDITOR_HOST_OK )
        return 0;
    square = Editor_DocOpenSquare(doc, map_x, map_z);
    if( !square )
        return 0;
    result = Editor_Jm2Parse(square, jm2.data, jm2.size);
    if( result.status != EDITOR_PARSE_OK )
        return 0;
    if( jl2.data && jl2.size > 0 )
    {
        result = Editor_Jl2Parse(square, jl2.data, jl2.size);
        if( result.status != EDITOR_PARSE_OK )
            return 0;
    }
    square->dirty_map = dirty_map;
    square->dirty_loc = dirty_loc;
    Editor_HostBlobFree(&jm2);
    Editor_HostBlobFree(&jl2);
    return 1;
}

int
main(
    int argc,
    char** argv)
{
    char const* scratch;
    char jm2_path[1200];
    char jl2_path[1200];
    char path[1300];
    /* Connections: a is Client 1's viewer, a2 its controller (no world),
     * b a second Client's viewer, c a late joiner. */
    struct EditorHost a;
    struct EditorHost a2;
    struct EditorHost b;
    struct EditorHost c;
    struct mirror ma = { NULL, 0, 0, 0, 0, { 0 }, 0 };
    struct mirror ma2 = { NULL, 0, 0, 0, 0, { 0 }, 0 };
    struct mirror mb = { NULL, 0, 0, 0, 0, { 0 }, 0 };
    struct mirror mc = { NULL, 0, 0, 0, 0, { 0 }, 0 };
    struct Editor_Cmd command;
    struct Editor_Tile original_tile;
    int coords[8];
    int count = 0;
    char* disk;
    size_t disk_size = 0;

    if( argc != 4 )
    {
        fprintf(stderr, "usage: maped_embed_test <scratch-dir> <source-jm2> <source-jl2>\n");
        return 2;
    }
    scratch = argv[1];

    snprintf(jm2_path, sizeof(jm2_path), "%s/maps/m50_50.jm2", scratch);
    snprintf(jl2_path, sizeof(jl2_path), "%s/maps/m50_50.jl2", scratch);
    copy_file(argv[2], jm2_path);
    copy_file(argv[3], jl2_path);

    ma.doc = malloc(sizeof(*ma.doc));
    mb.doc = malloc(sizeof(*mb.doc));
    mc.doc = malloc(sizeof(*mc.doc));
    if( !ma.doc || !mb.doc || !mc.doc )
    {
        fprintf(stderr, "out of memory for mirror docs\n");
        return 2;
    }
    Editor_DocInit(ma.doc, NULL);
    Editor_DocInit(mb.doc, NULL);
    Editor_DocInit(mc.doc, NULL);

    /* ---- connections and Clients ----------------------------------------- */

    check(Editor_HostOpenMapEdEmbed(&a, scratch, NULL) == 1, "viewer connection opens");
    check(Editor_HostMapEdWritable(&a), "the server holds the tree lock");
    check(
        Editor_HostOpenMapEdEmbedPeer(&a2, &a, TORIRSMAPED_ROLE_CONTROLLER, 1) == 1,
        "a controller connection joins the same Client");
    check(
        Editor_HostOpenMapEdEmbedPeer(&b, &a, TORIRSMAPED_ROLE_VIEWER, 0) == 1,
        "a second Client's viewer connects");
    check(
        Editor_HostMapEdClientId(&a) == Editor_HostMapEdClientId(&a2),
        "viewer and controller share a Client id");
    check(
        Editor_HostMapEdClientId(&a) != Editor_HostMapEdClientId(&b),
        "the second Client got its own id");

    check(
        a.vtable->square_list(a.user_data, coords, 4, &count) == EDITOR_HOST_OK
            && count == 1 && coords[0] == 50 && coords[1] == 50,
        "square_list names m50_50");

    /* ---- mirrors open the square ------------------------------------------ */

    check(open_square_into(&a, ma.doc, 50, 50) == 1, "Client 1's mirror opens the square");
    check(open_square_into(&b, mb.doc, 50, 50) == 1, "Client 2's mirror opens the square");
    original_tile = Editor_DocFindSquare(ma.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)];

    /* ---- a document edit crosses Client boundaries ------------------------ */

    memset(&command, 0, sizeof(command));
    command.kind = EDITOR_CMD_TILE;
    command.map_x = 50;
    command.map_z = 50;
    command.level = 0;
    command.x = 0;
    command.z = 0;
    command.tile_before = original_tile;
    command.tile_after = original_tile;
    command.tile_after.underlay_id = 7;

    check(
        Editor_HostMapEdCmd(&a, &command) == EDITOR_HOST_OK,
        "the authority accepts the edit");
    drain(&a, &ma);
    drain(&a2, &ma2);
    drain(&b, &mb);
    check(ma.cmds_seen == 1, "the sender got its own echo");
    check(ma2.cmds_seen == 1, "the same Client's controller heard the edit");
    check(mb.cmds_seen == 1, "the OTHER Client heard the edit too — one world");
    check(
        Editor_DocFindSquare(ma.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)].underlay_id
            == 7,
        "the sender's mirror applied the echo");
    check(
        Editor_DocFindSquare(mb.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)].underlay_id
            == 7,
        "the other Client's mirror matches");
    check(
        Editor_DocFindSquare(mb.doc, 50, 50)->dirty_map == 1,
        "the edit marked the mirror dirty");

    /* ---- shared history: the other Client can undo ------------------------ */

    check(Editor_HostMapEdUndo(&b) == 1, "the second Client undoes the edit");
    drain(&a, &ma);
    drain(&b, &mb);
    check(
        memcmp(
            &Editor_DocFindSquare(ma.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)],
            &original_tile,
            sizeof(original_tile))
            == 0,
        "undo restored the original authored tile everywhere");

    check(Editor_HostMapEdRedo(&a2) == 1, "the controller redoes it (no world needed)");
    drain(&a, &ma);
    drain(&b, &mb);
    check(
        Editor_DocFindSquare(ma.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)].underlay_id
            == 7,
        "redo re-applied it everywhere");

    /* ---- a late joiner opens the square: the answer is the DOCUMENT ------- */

    check(
        Editor_HostOpenMapEdEmbedPeer(&c, &a, TORIRSMAPED_ROLE_GENERIC, 0) == 1,
        "a late-joining connection attaches");
    check(open_square_into(&c, mc.doc, 50, 50) == 1, "it opens the square");
    check(
        Editor_DocFindSquare(mc.doc, 50, 50)->tiles[Editor_TileIndex(0, 0, 0)].underlay_id
            == 7,
        "the late joiner received the EDITED document, not the file");
    check(
        Editor_DocFindSquare(mc.doc, 50, 50)->dirty_map == 1,
        "and its dirty flags agree with the authority");

    /* ---- state is scoped to a Client -------------------------------------- */

    {
        int32_t selection[3] = { 2, 17, 33 };
        check(
            Editor_HostMapEdStateSet(&a, TORIRSMAPED_STATE_SELECTION, selection, 3) == 1,
            "the viewer publishes a selection");
    }
    drain(&a, &ma);
    drain(&a2, &ma2);
    drain(&b, &mb);
    check(ma.states_seen == 1, "the viewer got its own state echo");
    check(
        ma2.states_seen == 1 && ma2.last_state_key == TORIRSMAPED_STATE_SELECTION
            && ma2.last_state_count == 3 && ma2.last_state_values[1] == 17,
        "its controller follows the selection");
    check(mb.states_seen == 0, "the other Client never hears it");

    check(Editor_HostMapEdStateSync(&a2) == 1, "STATE_SYNC replays the group's one key");
    drain(&a2, &ma2);
    check(
        ma2.states_seen == 2 && ma2.last_state_values[2] == 33,
        "the replayed state matches");

    /* ---- save happens on the authority ------------------------------------ */

    check(Editor_HostMapEdSaveAll(&a2) == 1, "the controller saves the one dirty square");
    drain(&a, &ma);
    drain(&b, &mb);
    check(ma.saved_seen == 1 && mb.saved_seen == 1, "every Client heard what was saved");
    check(
        Editor_DocFindSquare(ma.doc, 50, 50)->dirty_map == 0
            && Editor_DocFindSquare(mb.doc, 50, 50)->dirty_map == 0,
        "mirrors cleared dirty on the broadcast");
    disk = slurp(jm2_path, &disk_size);
    check(disk && strstr(disk, "u7") != NULL, "the edit reached the disk");
    free(disk);
    disk = slurp(jl2_path, &disk_size);
    check(disk != NULL, "the untouched jl2 still exists");
    free(disk);

    /* ---- the file lane still works ----------------------------------------- */

    {
        char const* spawns = "[npc,1234]\ncoord=0_50_50_10_10\n";
        check(
            a.vtable->spawn_save(a.user_data, 50, 50, spawns, strlen(spawns))
                == EDITOR_HOST_OK,
            "spawn_save answers OK");
        snprintf(
            path,
            sizeof(path),
            "%s/server/scripts/areas/edited/configs/m50_50.spawn",
            scratch);
        disk = slurp(path, &disk_size);
        check(disk != NULL, "the spawn file landed on disk");
        free(disk);
        check(
            a.vtable->spawn_save(a.user_data, 50, 50, NULL, 0) == EDITOR_HOST_OK,
            "an absent spawn text deletes the file");
    }

    /* ---- a second SERVER over the same tree is read-only ------------------- */

    {
        struct EditorHost d;
        check(
            Editor_HostOpenMapEdEmbed(&d, scratch, NULL) == 1,
            "a second server starts (read-only is a state, not a failure)");
        check(!Editor_HostMapEdWritable(&d), "it found the tree locked");
        Editor_HostClose(&d);
    }

    /* ---- teardown ----------------------------------------------------------- */

    /* Peers borrow a's embed, so a closes last. */
    Editor_HostClose(&a2);
    Editor_HostClose(&b);
    Editor_HostClose(&c);
    Editor_HostClose(&a);

    snprintf(path, sizeof(path), "%s/.editor-session.lock", scratch);
    disk = slurp(path, &disk_size);
    check(disk == NULL, "no lock file is stranded after close");
    free(disk);

    Editor_DocFree(ma.doc);
    Editor_DocFree(mb.doc);
    Editor_DocFree(mc.doc);
    free(ma.doc);
    free(mb.doc);
    free(mc.doc);

    printf("maped_embed_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
