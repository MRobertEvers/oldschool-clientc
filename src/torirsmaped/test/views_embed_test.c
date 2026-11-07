/**
 * Views (docs/MAPED_DESIGN.html #windows; brief section 1, phase 3 `views`):
 * three connections on one embedded ToriRSMapEd, each a real client session
 * (struct Editor, the mirror the game client runs) with its own EditorView.
 *
 *   - ONE STROKE, THREE IDENTICAL MIRRORS: connection A paints a brush
 *     stroke; every mirror emits byte-identical .jm2 text, equal to the
 *     authority's, and one undo from another connection takes the whole
 *     stroke back everywhere.
 *   - THREE DIFFERENT CAMERAS: each view asks its connection index
 *     (STATE_VIEW_SELF), broadcasts its own pose under STATE_CAMERA_BASE +
 *     index, and hears the other two -- distinct, exact. Then follow (a view
 *     adopts another's pose and tracks it), the publish throttle, the key
 *     ownership rule, stop-broadcast letting followers go, follow selection,
 *     and a connection that leaves taking its camera key with it.
 *   - the thin toolbar on a real ToriRSChrome model, a click included.
 *   - SPAWN_VIEW on the server: UNSUPPORTED unbound, then the capability bit,
 *     the binding's arguments, the refusal of a bad pose, malformed frames.
 *   - the camera's text form, and the launch service's `view` verb under a
 *     fake spawner.
 *   - with a torirsmaped binary: the DAEMON's SPAWN_VIEW end to end -- it
 *     starts the --view-binary (a script standing in for the client) with
 *     exactly the viewer command line.
 *
 *   views_embed_test <content-dir> <scratch-dir> [<torirsmaped>] [<manifest>]
 *
 * Reads m50_50 from the content tree; writes only inside the scratch dir.
 */

#include "bootmanifest/bootmanifest.h"
#include "editor/editor.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_jm2.h"
#include "editor/editor_view.h"
#include "mem/torirs_mem.h"
#include "platform/launch_sessions.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_net.h"
#include "torirsmaped/torirs_maped_view.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>
#include <toridraw_math.h>
#include <toridraw_types.h>

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

static int
write_file(
    char const* path,
    char const* data,
    size_t size)
{
    FILE* f = fopen(path, "wb");
    if( !f )
        return 0;
    if( size && fwrite(data, 1, size, f) != size )
    {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}

static int64_t
now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* ---- the three sessions --------------------------------------------------- */

/** What each session's state callback saw, beside its view. */
struct Watch
{
    struct EditorView* view;
    int states;
    /** Facts for one key the test is watching (camera_watch_key). */
    uint32_t watch_key;
    int watch_hits;
    int watch_last_count;
    int watch_last_value0;
};

static void
on_state(
    void* user_data,
    uint32_t key,
    const int32_t* values,
    int count)
{
    struct Watch* watch = user_data;
    watch->states++;
    if( key == watch->watch_key )
    {
        watch->watch_hits++;
        watch->watch_last_count = count;
        watch->watch_last_value0 = count > 0 ? values[0] : 0;
    }
    EditorView_OnState(watch->view, key, values, count);
}

static struct Editor g_ed[3];
static struct EditorView g_views[3];
static struct Watch g_watch[3];
static struct ToriRSChrome g_ui;

static void
pump_all(int n)
{
    for( int i = 0; i < n; i++ )
        Editor_PumpFacts(&g_ed[i]);
}

static char*
emit_jm2(struct Editor* editor)
{
    struct Editor_Square* square = Editor_DocFindSquare(&editor->doc, 50, 50);
    size_t length;
    char* text;
    if( !square )
        return NULL;
    length = Editor_Jm2Emit(square, NULL, 0);
    text = malloc(length + 1);
    Editor_Jm2Emit(square, text, length + 1);
    return text;
}

static int
camera_same(
    const struct EditorViewCamera* a,
    const struct EditorViewCamera* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static struct EditorViewCamera
pose(
    int tile_x,
    int tile_z,
    int level,
    int yaw,
    int pitch,
    int zoom)
{
    struct EditorViewCamera camera;
    camera.x = tile_x * 128 + 64;
    camera.z = tile_z * 128 + 64;
    camera.level = level;
    camera.yaw = yaw;
    camera.pitch = pitch;
    camera.zoom = zoom;
    return camera;
}

static void
test_three_connections(
    char const* scratch,
    const struct RSCache* profile)
{
    struct EditorHost host_a;
    struct EditorHost host_b;
    struct EditorHost host_c;
    struct EditorHost host_q;
    char* before[3];
    char* after[3];
    char* undone[3];
    int idx[3];
    int64_t t = now_ms();

    check(Editor_HostOpenMapEdEmbed(&host_a, scratch, NULL), "the embedded server starts");
    check(
        Editor_HostOpenMapEdEmbedPeer(&host_b, &host_a, TORIRSMAPED_ROLE_VIEWER, 1),
        "a second connection joins A's Client");
    check(
        Editor_HostOpenMapEdEmbedPeer(&host_c, &host_a, TORIRSMAPED_ROLE_VIEWER, 1),
        "a third connection joins A's Client");
    check(
        Editor_HostOpenMapEdEmbedPeer(&host_q, &host_a, TORIRSMAPED_ROLE_CONTROLLER, 0),
        "a fourth connection, its own Client, to read the authority");
    check(
        (Editor_HostMapEdCapabilities(&host_a) & TORIRSMAPED_CAP_SPAWN_VIEW) == 0,
        "the embedded server offers no SPAWN_VIEW (no socket for a second process)");

    Editor_OpenHost(&g_ed[0], &host_a, "view a", profile);
    Editor_OpenHost(&g_ed[1], &host_b, "view b", profile);
    Editor_OpenHost(&g_ed[2], &host_c, "view c", profile);
    for( int i = 0; i < 3; i++ )
    {
        EditorView_Init(&g_views[i], &g_ed[i]);
        memset(&g_watch[i], 0, sizeof(g_watch[i]));
        g_watch[i].view = &g_views[i];
        Editor_SetStateCallback(&g_ed[i], on_state, &g_watch[i]);
        check(Editor_OpenDoc(&g_ed[i], EDITOR_DOC_SQUARE, "m50_50") == 1, "every view opens m50_50");
    }

    /* ---- one stroke, three identical mirrors ------------------------------ */

    for( int i = 0; i < 3; i++ )
        before[i] = emit_jm2(&g_ed[i]);
    check(
        before[0] && before[1] && before[2] && strcmp(before[0], before[1]) == 0
            && strcmp(before[0], before[2]) == 0,
        "the three mirrors start identical");

    Editor_StrokeBegin(&g_ed[0]);
    {
        int accepted = 0;
        for( int i = 0; i < 12; i++ )
        {
            struct Editor_Cmd command;
            struct Editor_Square* square = Editor_DocFindSquare(&g_ed[0].doc, 50, 50);
            int const x = 20 + i;
            int const z = 30 + (i % 3);
            struct Editor_Tile tile = square->tiles[Editor_TileIndex(x, z, 0)];

            Editor_CmdInitSquare(&command, EDITOR_CMD_TILE, 50, 50);
            command.has_before = 1;
            command.has_after = 1;
            command.u.tile.level = 0;
            command.u.tile.x = x;
            command.u.tile.z = z;
            command.u.tile.before = tile;
            command.u.tile.after = tile;
            command.u.tile.after.has_height = 1;
            command.u.tile.after.height = (uint8_t)(tile.height + 7 + i);
            accepted += Editor_Apply(&g_ed[0], &command);
        }
        check(accepted == 12, "the authority takes all 12 commands of the stroke");
    }
    Editor_StrokeEnd(&g_ed[0]);
    pump_all(3);

    for( int i = 0; i < 3; i++ )
        after[i] = emit_jm2(&g_ed[i]);
    check(after[0] && strcmp(after[0], before[0]) != 0, "the stroke changed A's mirror");
    check(
        after[1] && strcmp(after[0], after[1]) == 0, "B's mirror is byte-identical to A's");
    check(
        after[2] && strcmp(after[0], after[2]) == 0, "C's mirror is byte-identical to A's");
    {
        struct EditorHost_DocText text;
        memset(&text, 0, sizeof(text));
        check(
            Editor_HostMapEdDocOpen(&host_q, EDITOR_DOC_SQUARE, "m50_50", &text) == EDITOR_HOST_OK,
            "the authority answers DOC_OPEN");
        check(
            text.part_count == 2 && text.parts[0].data && after[0]
                && strcmp(text.parts[0].data, after[0]) == 0,
            "and its text is the mirrors' text");
        check(text.dirty == 1, "the authority marks the terrain unsaved");
        Editor_HostDocTextFree(&text);
    }
    for( int i = 0; i < 3; i++ )
    {
        struct Editor_Square* square = Editor_DocFindSquare(&g_ed[i].doc, 50, 50);
        check(square && square->dirty_map == 1 && square->dirty_loc == 0,
            "every mirror agrees on what is unsaved");
    }

    check(Editor_Undo(&g_ed[1]) == 12, "one undo from B takes the whole stroke back");
    pump_all(3);
    for( int i = 0; i < 3; i++ )
        undone[i] = emit_jm2(&g_ed[i]);
    check(
        undone[0] && undone[1] && undone[2] && strcmp(undone[0], before[0]) == 0
            && strcmp(undone[1], before[0]) == 0 && strcmp(undone[2], before[0]) == 0,
        "all three mirrors are back to the starting text");
    if( undone[0] && before[0] && strcmp(undone[0], before[0]) != 0 )
    {
        size_t at = 0;
        while( undone[0][at] && undone[0][at] == before[0][at] )
            at++;
        size_t from = at > 40 ? at - 40 : 0;
        fprintf(stderr, "  first difference at byte %zu\n  before: ", at);
        for( size_t k = from; k < at + 40 && before[0][k]; k++ )
            fputc(before[0][k] == '\n' ? '|' : before[0][k], stderr);
        fprintf(stderr, "\n  undone: ");
        for( size_t k = from; k < at + 40 && undone[0][k]; k++ )
            fputc(undone[0][k] == '\n' ? '|' : undone[0][k], stderr);
        fprintf(stderr, "\n");
    }
    for( int i = 0; i < 3; i++ )
    {
        free(before[i]);
        free(after[i]);
        free(undone[i]);
    }

    /* ---- connection indices ----------------------------------------------- */

    for( int i = 0; i < 3; i++ )
        check(EditorView_AskSelf(&g_views[i]) == 1, "a view asks which connection it is");
    pump_all(3);
    for( int i = 0; i < 3; i++ )
        idx[i] = g_views[i].self_index;
    check(idx[0] >= 0 && idx[1] >= 0 && idx[2] >= 0, "every view learned its index");
    check(idx[0] != idx[1] && idx[0] != idx[2] && idx[1] != idx[2], "three distinct indices");
    check(
        g_views[0].group == g_views[1].group && g_views[1].group == g_views[2].group
            && g_views[0].group == Editor_HostMapEdClientId(&g_ed[0].host),
        "VIEW_SELF reports the shared Client");
    check(
        g_watch[0].states == 1 && g_watch[1].states == 1 && g_watch[2].states == 1,
        "the VIEW_SELF answer reaches only the asker (one fact each, no relay)");

    /* ---- three different cameras ------------------------------------------ */

    {
        struct EditorViewCamera const cam[3] = {
            pose(3222, 3218, 0, 0, 300, 1200),
            pose(3230, 3240, 1, 512, 256, 1800),
            pose(3200, 3200, 0, 1024, 383, 2600),
        };
        struct EditorViewCamera moved = pose(3223, 3219, 0, 64, 310, 1100);
        struct EditorViewCamera adopted;

        for( int i = 0; i < 3; i++ )
        {
            EditorView_SetCamera(&g_views[i], &cam[i]);
            check(EditorView_SetBroadcast(&g_views[i], 1) == 1, "a view starts broadcasting");
        }
        pump_all(3);
        for( int i = 0; i < 3; i++ )
        {
            for( int j = 0; j < 3; j++ )
            {
                if( i == j )
                {
                    check(!g_views[i].peers[idx[i]].live, "a view never lists itself as a peer");
                    continue;
                }
                check(
                    g_views[i].peers[idx[j]].live
                        && camera_same(&g_views[i].peers[idx[j]].camera, &cam[j]),
                    "each view hears the other two cameras exactly");
            }
            check(camera_same(&g_views[i].camera, &cam[i]), "and keeps its own pose");
            check(!EditorView_TakeCamera(&g_views[i], &adopted), "nothing moved it");
        }
        check(
            !camera_same(&cam[0], &cam[1]) && !camera_same(&cam[0], &cam[2])
                && !camera_same(&cam[1], &cam[2]),
            "three different cameras");

        /* Follow: C adopts A's pose and tracks it. */
        check(EditorView_Follow(&g_views[2], idx[2]) == 0, "a view cannot follow itself");
        check(EditorView_Follow(&g_views[2], idx[0]) == 1, "C follows A");
        check(
            EditorView_TakeCamera(&g_views[2], &adopted) && camera_same(&adopted, &cam[0]),
            "C adopts A's pose at once");
        EditorView_SetCamera(&g_views[2], &adopted);

        EditorView_SetCamera(&g_views[0], &moved);
        check(EditorView_Tick(&g_views[0], t + 1000) == 1, "A publishes its moved camera");
        pump_all(3);
        check(
            EditorView_TakeCamera(&g_views[2], &adopted) && camera_same(&adopted, &moved),
            "C tracks A's move");
        check(!EditorView_TakeCamera(&g_views[1], &adopted), "B, not following, stays put");
        check(camera_same(&g_views[1].camera, &cam[1]), "B's pose is its own");

        /* The throttle: a second move inside EDITOR_VIEW_PUBLISH_MS waits. */
        moved.x += 128;
        EditorView_SetCamera(&g_views[0], &moved);
        check(EditorView_Tick(&g_views[0], t + 1010) == 0, "a move 10 ms later is held");
        check(EditorView_Tick(&g_views[0], t + 1050) == 1, "and goes out after 33 ms");
        check(EditorView_Tick(&g_views[0], t + 2000) == 0, "an unchanged pose is not resent");
        pump_all(3);
        check(
            EditorView_TakeCamera(&g_views[2], &adopted) && camera_same(&adopted, &moved),
            "C tracks the second move");

        /* Ownership: B writing A's camera key is dropped by the server. */
        {
            int32_t forged[TORIRSMAPED_CAMERA_VALUES] = { 1, 2, 0, 3, 4, 5 };
            Editor_StateSet(
                &g_ed[1], ToriRSMapEd_CameraKey(idx[0]), forged, TORIRSMAPED_CAMERA_VALUES);
            pump_all(3);
            check(
                camera_same(&g_views[2].peers[idx[0]].camera, &moved)
                    && !EditorView_TakeCamera(&g_views[2], &adopted),
                "a connection cannot publish another's camera key");
        }

        /* Stop broadcasting: followers let go. */
        check(EditorView_SetBroadcast(&g_views[0], 0) == 1, "A stops broadcasting");
        pump_all(3);
        check(!g_views[2].peers[idx[0]].live, "C hears that A stopped");
        check(g_views[2].follow_index == -1, "and stops following it");
        check(!g_views[1].peers[idx[0]].live, "B too");

        /* Follow selection: B frames what A selects. */
        g_views[1].follow_selection = 1;
        {
            int32_t selection[7] = { 1, 3201, 3205, 1, 0, 0, 0 };
            Editor_StateSet(&g_ed[0], TORIRSMAPED_STATE_SELECTION, selection, 7);
        }
        pump_all(3);
        check(
            EditorView_TakeCamera(&g_views[1], &adopted) && adopted.x == 3201 * 128 + 64
                && adopted.z == 3205 * 128 + 64 && adopted.level == 1
                && adopted.yaw == cam[1].yaw && adopted.zoom == cam[1].zoom,
            "follow selection frames the selected tile, keeping the view's angles");
        check(!EditorView_TakeCamera(&g_views[2], &adopted), "a view not following selection stays put");

        /* The active level (the menu bar's Level row, PageUp/PageDown) is
         * Client state: A pins level 2, B and C hear it; Auto too. */
        {
            int32_t level = 2;
            for( int i = 1; i < 3; i++ )
            {
                g_watch[i].watch_key = TORIRSMAPED_STATE_EDIT_LEVEL;
                g_watch[i].watch_hits = 0;
            }
            Editor_StateSet(&g_ed[0], TORIRSMAPED_STATE_EDIT_LEVEL, &level, 1);
            pump_all(3);
            check(g_watch[1].watch_hits >= 1 && g_watch[1].watch_last_value0 == 2 && g_watch[2].watch_hits >= 1
                      && g_watch[2].watch_last_value0 == 2,
                "STATE_EDIT_LEVEL: a level pinned in A reaches B and C");
            level = -1;
            Editor_StateSet(&g_ed[0], TORIRSMAPED_STATE_EDIT_LEVEL, &level, 1);
            pump_all(3);
            check(g_watch[1].watch_last_value0 == -1 && g_watch[2].watch_last_value0 == -1,
                "STATE_EDIT_LEVEL: and so does Auto");
            for( int i = 1; i < 3; i++ )
                g_watch[i].watch_key = 0;
        }
    }

    /* ---- the thin toolbar on the real chrome model ------------------------- */

    {
        struct EditorView* view = &g_views[2];
        struct EditorViewToolbar* bar = &view->toolbar;
        struct EditorViewCamera adopted;
        struct ToriRSChromeRect strip;

        ToriRSChrome_Init(&g_ui);
        ToriRSChrome_SetSurface(&g_ui, 1280, 800);
        EditorView_ToolbarBuild(view, &g_ui);
        ToriRSChrome_Build(&g_ui);
        strip = ToriRSChrome_PanelRect(&g_ui, bar->panel);
        check(bar->built && strip.w > 0 && strip.h > 0, "the toolbar strip lays out");
        check(strip.h < 80, "and it is thin (one row)");
        check(!g_ui.panels[bar->menu_level].visible, "its menus start closed");

        /* A real click on the first button. */
        ToriRSChrome_MouseMove(&g_ui, strip.x + 12, strip.y + strip.h / 2);
        ToriRSChrome_MouseDown(&g_ui, strip.x + 12, strip.y + strip.h / 2);
        ToriRSChrome_MouseUp(&g_ui, strip.x + 12, strip.y + strip.h / 2);
        {
            int const clicked = ToriRSChrome_TakeActivated(&g_ui);
            check(clicked == bar->bt_level, "clicking the strip's first button activates Levels");
            check(
                EditorView_ToolbarActivate(view, &g_ui, clicked) == EDITOR_VIEW_ACTION_HANDLED,
                "the view takes it");
        }
        check(g_ui.panels[bar->menu_level].visible, "Levels opens its menu");
        EditorView_ToolbarActivate(view, &g_ui, bar->bt_layers);
        check(
            g_ui.panels[bar->menu_layers].visible && !g_ui.panels[bar->menu_level].visible,
            "one menu at a time");

        check(EditorView_LevelMask(view) == 0, "every level by default");
        ToriRSChrome_RadioSelect(&g_ui, bar->level_items[2]);
        EditorView_ToolbarActivate(view, &g_ui, bar->level_items[2]);
        check(view->level_cap == 1 && EditorView_LevelMask(view) == 0x3,
            "Level 1 caps the view at planes 0..1");
        ToriRSChrome_SetChecked(&g_ui, bar->item_solo, 1);
        EditorView_ToolbarActivate(view, &g_ui, bar->item_solo);
        check(EditorView_LevelMask(view) == 0x2, "Only that level draws plane 1 alone");

        check(EditorView_LayerShown(view, EDITOR_VIEW_LAYER_LOCS), "locs shown by default");
        ToriRSChrome_SetChecked(&g_ui, bar->layer_items[1], 0);
        EditorView_ToolbarActivate(view, &g_ui, bar->layer_items[1]);
        check(!EditorView_LayerShown(view, EDITOR_VIEW_LAYER_LOCS), "the Locs toggle hides locs");
        check(EditorView_LayerShown(view, EDITOR_VIEW_LAYER_TERRAIN), "and nothing else");

        EditorView_ToolbarActivate(view, &g_ui, bar->preset_items[EDITOR_VIEW_PRESET_ISO]);
        check(
            EditorView_TakeCamera(view, &adopted) && adopted.yaw == 256 && adopted.pitch == 256,
            "the three-quarter preset turns the camera");

        ToriRSChrome_SetChecked(&g_ui, bar->item_broadcast, 0);
        EditorView_ToolbarActivate(view, &g_ui, bar->item_broadcast);
        check(!view->broadcast, "the Broadcast check stops the broadcast");
        pump_all(3);
        check(!g_views[0].peers[idx[2]].live, "and A hears C stop");

        /* B is still broadcasting: C's follow menu offers it. */
        EditorView_ToolbarRefresh(view, &g_ui);
        check(
            !g_ui.widgets[bar->follow_items[idx[1]]].hidden
                && g_ui.widgets[bar->follow_items[idx[0]]].hidden,
            "the follow menu lists only views that are broadcasting");
        EditorView_ToolbarActivate(view, &g_ui, bar->follow_items[idx[1]]);
        check(view->follow_index == idx[1], "choosing one follows it");
        check(
            EditorView_ToolbarActivate(view, &g_ui, bar->bt_follow_selection)
                    == EDITOR_VIEW_ACTION_HANDLED
                && view->follow_selection,
            "Follow selection toggles");
        check(
            EditorView_ToolbarActivate(view, &g_ui, bar->bt_open_panels)
                == EDITOR_VIEW_ACTION_OPEN_PANELS,
            "Open panels asks the host to promote the window");
        check(
            EditorView_ToolbarActivate(view, &g_ui, bar->follow_items[EDITOR_VIEW_PEER_MAX - 1] + 1)
                == EDITOR_VIEW_ACTION_NONE,
            "a widget that is not the toolbar's is left alone");
        check(EditorView_ToolbarActivate(view, &g_ui, -1) == EDITOR_VIEW_ACTION_NONE,
            "no activation is no action");
    }

    /* ---- a connection that leaves takes its camera with it ---------------- */

    {
        g_watch[2].watch_key = ToriRSMapEd_CameraKey(idx[1]);
        g_watch[2].watch_hits = 0;
        check(g_views[2].peers[idx[1]].live, "C hears B broadcasting before B leaves");
        Editor_Close(&g_ed[1]);
        pump_all(1);
        Editor_PumpFacts(&g_ed[2]);
        check(
            g_watch[2].watch_hits == 1 && g_watch[2].watch_last_count == 0,
            "B leaving publishes its camera key empty");
        check(!g_views[2].peers[idx[1]].live, "so C's list drops it");
        check(g_views[2].follow_index == -1, "and C, which followed it, lets go");

        g_watch[2].watch_hits = 0;
        check(Editor_HostMapEdStateSync(&g_ed[2].host) >= 1, "C replays the Client's store");
        Editor_PumpFacts(&g_ed[2]);
        check(g_watch[2].watch_hits == 0, "a late joiner never replays a departed camera");
    }

    Editor_HostClose(&host_q);
    Editor_Close(&g_ed[2]);
    Editor_Close(&g_ed[0]);
}

/* ---- the embedded server's loopback door (a popped-out window) ------------- */

struct ListenPump
{
    struct ToriRSMapEdListener* listener;
    volatile int stop;
};

/* Only while the main thread is blocked in the TCP client's synchronous
 * requests (HELLO, the doc open, STATE_SYNC): the server is then touched by
 * this thread alone. */
static void*
listen_pump_run(void* arg)
{
    struct ListenPump* pump = arg;
    while( !pump->stop )
        (void)ToriRSMapEd_ListenerPoll(pump->listener, 5);
    return NULL;
}

static void
test_embed_listener(
    char const* scratch,
    const struct RSCache* profile)
{
    static struct Editor main_ed;
    static struct Editor popped_ed;
    static struct EditorView popped_view;
    static struct Watch main_watch;
    static struct Watch popped_watch;
    static struct EditorView main_view;
    struct EditorHost main_host;
    struct EditorHost popped_host;
    struct ToriRSMapEdListener* listener;
    struct ListenPump pump;
    pthread_t thread;
    int replayed = -1;
    int opened = 0;
    int32_t value;
    char* main_text;
    char* popped_text;

    check(Editor_HostOpenMapEdEmbed(&main_host, scratch, NULL), "listener: the embedded server starts");
    check(Editor_HostMapEdEmbedServer(&main_host) != NULL, "listener: an embed host names its server");
    Editor_OpenHost(&main_ed, &main_host, "main", profile);
    EditorView_Init(&main_view, &main_ed);
    memset(&main_watch, 0, sizeof(main_watch));
    main_watch.view = &main_view;
    Editor_SetStateCallback(&main_ed, on_state, &main_watch);
    check(Editor_OpenDoc(&main_ed, EDITOR_DOC_SQUARE, "m50_50") == 1, "listener: the main window opens m50_50");
    /* The main window has level 2 pinned. */
    value = 2;
    check(Editor_StateSet(&main_ed, TORIRSMAPED_STATE_EDIT_LEVEL, &value, 1), "listener: the main window pins level 2");
    Editor_PumpFacts(&main_ed);

    listener = ToriRSMapEd_ListenerOpen(Editor_HostMapEdEmbedServer(&main_host), 0, "views test");
    check(listener && ToriRSMapEd_ListenerPort(listener) > 0, "listener: the door opens on an ephemeral port");
    if( !listener )
    {
        Editor_Close(&main_ed);
        return;
    }

    /* The popped-out window: a TCP client of the same server, joining the
     * main window's Client, exactly as it would join the daemon. */
    pump.listener = listener;
    pump.stop = 0;
    pthread_create(&thread, NULL, listen_pump_run, &pump);
    if( Editor_HostOpenMapEdTcp(&popped_host, "127.0.0.1", ToriRSMapEd_ListenerPort(listener),
            TORIRSMAPED_ROLE_VIEWER, Editor_HostMapEdClientId(&main_host)) )
    {
        opened = 1;
        Editor_OpenHost(&popped_ed, &popped_host, "popped", profile);
        (void)Editor_OpenDoc(&popped_ed, EDITOR_DOC_SQUARE, "m50_50");
        replayed = Editor_HostMapEdStateSync(&popped_ed.host);
    }
    pump.stop = 1;
    pthread_join(thread, NULL);
    check(opened, "listener: a TCP client connects through the door");
    if( !opened )
    {
        ToriRSMapEd_ListenerClose(listener);
        Editor_Close(&main_ed);
        return;
    }
    check(Editor_HostMapEdClientId(&popped_host) == Editor_HostMapEdClientId(&main_host),
        "listener: it joins the main window's Client");
    check(ToriRSMapEd_ListenerConnections(listener) == 1, "listener: one connection on the door");

    /* The view side of a popped-out window's boot: the Client's pinned
     * level, replayed, becomes its cap. */
    EditorView_Init(&popped_view, &popped_ed);
    memset(&popped_watch, 0, sizeof(popped_watch));
    popped_watch.view = &popped_view;
    Editor_SetStateCallback(&popped_ed, on_state, &popped_watch);
    check(replayed >= 1, "listener: STATE_SYNC replays the Client's store");
    EditorView_ExpectSync(&popped_view, replayed > 0 ? replayed : 0);
    Editor_PumpFacts(&popped_ed);
    check(popped_view.level_cap == 2 && !popped_view.level_solo,
        "listener: the popped-out view starts at the main window's pinned level");
    check(popped_view.sync_pending == 0, "listener: and only the replay is adopted");

    /* The same document on both sides. */
    main_text = emit_jm2(&main_ed);
    popped_text = emit_jm2(&popped_ed);
    check(main_text && popped_text && strcmp(main_text, popped_text) == 0,
        "listener: the popped-out mirror holds the authority's m50_50");
    free(main_text);
    free(popped_text);

    /* The relay both ways, the main window pumping the door each frame. */
    main_watch.watch_key = TORIRSMAPED_STATE_TOOL;
    popped_watch.watch_key = TORIRSMAPED_STATE_TOOL;
    value = 3;
    check(Editor_StateSet(&popped_ed, TORIRSMAPED_STATE_TOOL, &value, 1), "listener: the popped window sets the tool");
    for( int frame = 0; frame < 50 && main_watch.watch_hits == 0; frame++ )
    {
        (void)ToriRSMapEd_ListenerPoll(listener, 2);
        Editor_PumpFacts(&main_ed);
    }
    check(main_watch.watch_hits >= 1 && main_watch.watch_last_value0 == 3,
        "listener: the main window hears the popped window's tool");
    value = 5;
    popped_watch.watch_hits = 0;
    check(Editor_StateSet(&main_ed, TORIRSMAPED_STATE_TOOL, &value, 1), "listener: the main window sets the tool");
    for( int frame = 0; frame < 50 && popped_watch.watch_last_value0 != 5; frame++ )
    {
        Editor_PumpFacts(&main_ed);
        (void)ToriRSMapEd_ListenerPoll(listener, 2);
        Editor_PumpFacts(&popped_ed);
    }
    check(popped_watch.watch_last_value0 == 5, "listener: the popped window hears the main window's tool");

    /* Every shared key, both ways: the selection from the main window, the
     * shared tools (brush, palette, ...) from the popped window, and the
     * catalog pick and pinned level from the main window again. */
    {
        static uint32_t const keys[4] = { TORIRSMAPED_STATE_SELECTION, TORIRSMAPED_STATE_TOOLS,
            TORIRSMAPED_STATE_CATALOG_PICK, TORIRSMAPED_STATE_EDIT_LEVEL };
        static char const* const names[4] = { "selection", "shared tools", "catalog pick", "pinned level" };
        for( int k = 0; k < 4; k++ )
        {
            int32_t values[TORIRSMAPED_TOOLS_VALUES];
            int const count = keys[k] == TORIRSMAPED_STATE_TOOLS ? TORIRSMAPED_TOOLS_VALUES
                              : keys[k] == TORIRSMAPED_STATE_SELECTION ? 7
                              : keys[k] == TORIRSMAPED_STATE_CATALOG_PICK ? 2
                                                                         : 1;
            int const from_popped = (k & 1) != 0;
            struct Editor* sender = from_popped ? &popped_ed : &main_ed;
            struct Watch* hearer = from_popped ? &main_watch : &popped_watch;
            char what[96];
            for( int v = 0; v < count; v++ )
                values[v] = 100 + k * 10 + v;
            if( keys[k] == TORIRSMAPED_STATE_EDIT_LEVEL )
                values[0] = 3;
            hearer->watch_key = keys[k];
            hearer->watch_hits = 0;
            hearer->watch_last_value0 = -1;
            check(Editor_StateSet(sender, keys[k], values, count), "listener: a window sets a shared key");
            for( int frame = 0; frame < 50 && hearer->watch_hits == 0; frame++ )
            {
                Editor_PumpFacts(&main_ed);
                (void)ToriRSMapEd_ListenerPoll(listener, 2);
                Editor_PumpFacts(&popped_ed);
            }
            snprintf(what, sizeof(what), "listener: the %s window's %s reaches the %s window",
                from_popped ? "popped" : "main", names[k], from_popped ? "main" : "popped");
            check(hearer->watch_hits >= 1 && hearer->watch_last_count == count
                      && hearer->watch_last_value0 == values[0],
                what);
        }
    }

    /* Closing the window retires its connection; closing the door is safe
     * with none left, and the session goes on. */
    Editor_Close(&popped_ed);
    for( int frame = 0; frame < 50 && ToriRSMapEd_ListenerConnections(listener) > 0; frame++ )
        (void)ToriRSMapEd_ListenerPoll(listener, 2);
    check(ToriRSMapEd_ListenerConnections(listener) == 0, "listener: a closed window leaves the door");
    ToriRSMapEd_ListenerClose(listener);
    Editor_Close(&main_ed);
}

/* ---- SPAWN_VIEW on a server, frame by frame -------------------------------- */

struct MemPipe
{
    struct ToriRSMapEdBuf to_server;
    struct ToriRSMapEdBuf to_client;
};

static int
pipe_recv(
    void* ctx,
    uint8_t* dst,
    int max)
{
    struct MemPipe* pipe = ctx;
    return ToriRSMapEd_BufRead(&pipe->to_server, dst, max);
}

static int
pipe_send(
    void* ctx,
    const uint8_t* src,
    int len)
{
    struct MemPipe* pipe = ctx;
    return ToriRSMapEd_BufWrite(&pipe->to_client, src, len);
}

static void
raw_send(
    struct MemPipe* pipe,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    uint8_t header[8];
    ToriRSMapEd_WriteU32(header, type);
    ToriRSMapEd_WriteU32(header + 4, length);
    ToriRSMapEd_BufWrite(&pipe->to_server, header, 8);
    if( length )
        ToriRSMapEd_BufWrite(&pipe->to_server, payload, (int)length);
}

/** The next whole frame the server sent; 0 when there is none. */
static int
raw_take(
    struct MemPipe* pipe,
    uint32_t* out_type,
    uint8_t* out_payload,
    uint32_t* out_length)
{
    const uint8_t* head;
    uint32_t length;
    if( ToriRSMapEd_BufAvailable(&pipe->to_client) < 8 )
        return 0;
    head = ToriRSMapEd_BufPeek(&pipe->to_client);
    length = ToriRSMapEd_ReadU32(head + 4);
    if( ToriRSMapEd_BufAvailable(&pipe->to_client) < 8 + (int)length )
        return 0;
    *out_type = ToriRSMapEd_ReadU32(head);
    *out_length = length;
    if( length <= 256 )
        memcpy(out_payload, head + 8, length);
    ToriRSMapEd_BufConsume(&pipe->to_client, 8 + (int)length);
    return 1;
}

struct FakeSpawner
{
    int calls;
    uint32_t group;
    int32_t camera[TORIRSMAPED_CAMERA_VALUES];
};

static enum EditorHost_Status
fake_spawn(
    void* ctx,
    uint32_t group,
    const int32_t* camera,
    int* out_pid)
{
    struct FakeSpawner* fake = ctx;
    fake->calls++;
    fake->group = group;
    memcpy(fake->camera, camera, sizeof(fake->camera));
    *out_pid = 4242;
    return EDITOR_HOST_OK;
}

static void
hello(
    struct MemPipe* pipe,
    struct ToriRSMapEdSession* session,
    uint32_t version,
    uint32_t* out_caps,
    uint32_t* out_group)
{
    uint8_t payload[256];
    uint32_t type;
    uint32_t length;
    ToriRSMapEd_WriteU32(payload, version);
    ToriRSMapEd_WriteU32(payload + 4, TORIRSMAPED_ROLE_CONTROLLER);
    ToriRSMapEd_WriteU32(payload + 8, 0);
    raw_send(pipe, TORIRSMAPED_REQ_HELLO, payload, 12);
    ToriRSMapEd_SessionPump(session);
    *out_caps = 0;
    *out_group = 0;
    if( raw_take(pipe, &type, payload, &length) && type == TORIRSMAPED_FACT_HELLO )
    {
        *out_group = ToriRSMapEd_ReadU32(payload + 8);
        if( length == 16 )
            *out_caps = ToriRSMapEd_ReadU32(payload + 12);
    }
    /* A v2 greeting is followed by the lane's state, FACT_LIVE_STATUS then
     * FACT_LIVE_PLAY (ToriRSMapEd_LiveWelcome); the views below read the
     * frames in order, so the two are taken here. */
    if( version >= TORIRSMAPED_PROTO_VERSION )
    {
        (void)raw_take(pipe, &type, payload, &length);
        (void)raw_take(pipe, &type, payload, &length);
    }
}

static void
put_camera(
    uint8_t* out,
    const int32_t* camera)
{
    for( int i = 0; i < TORIRSMAPED_CAMERA_VALUES; i++ )
        ToriRSMapEd_WriteU32(out + 4 * i, (uint32_t)camera[i]);
}

static void
test_spawn_view_frames(char const* scratch)
{
    static struct ToriRSMapEd server;
    struct ToriRSMapEdSession session;
    struct ToriRSMapEdSession v1_session;
    struct MemPipe pipe;
    struct MemPipe v1_pipe;
    struct ToriRSMapEdTransport transport;
    struct FakeSpawner fake;
    struct ToriRSMapEdViewSpawner spawner;
    uint8_t payload[256];
    uint32_t type;
    uint32_t length;
    uint32_t caps;
    uint32_t group;
    int32_t const camera[TORIRSMAPED_CAMERA_VALUES] = { 3222 * 128 + 64, 3218 * 128, 0, 512, 300,
                                                        1200 };

    memset(&pipe, 0, sizeof(pipe));
    memset(&v1_pipe, 0, sizeof(v1_pipe));
    memset(&fake, 0, sizeof(fake));
    ToriRSMapEd_Open(&server, scratch, NULL);
    transport.ctx = &pipe;
    transport.recv = pipe_recv;
    transport.send = pipe_send;
    ToriRSMapEd_SessionInit(&session, &server, &transport);

    hello(&pipe, &session, TORIRSMAPED_PROTO_VERSION, &caps, &group);
    check(group != 0 && (caps & TORIRSMAPED_CAP_SPAWN_VIEW) == 0,
        "an unbound server leaves the SPAWN_VIEW bit clear");
    put_camera(payload, camera);
    raw_send(&pipe, TORIRSMAPED_REQ_SPAWN_VIEW, payload, 4 * TORIRSMAPED_CAMERA_VALUES);
    ToriRSMapEd_SessionPump(&session);
    check(
        raw_take(&pipe, &type, payload, &length) && type == TORIRSMAPED_FACT_STATUS
            && ToriRSMapEd_ReadU32(payload) == TORIRSMAPED_REQ_SPAWN_VIEW
            && ToriRSMapEd_ReadU32(payload + 4) == EDITOR_HOST_UNSUPPORTED,
        "and answers SPAWN_VIEW UNSUPPORTED");

    spawner.ctx = &fake;
    spawner.spawn = fake_spawn;
    ToriRSMapEd_SetViewSpawner(&server, &spawner);
    hello(&pipe, &session, TORIRSMAPED_PROTO_VERSION, &caps, &group);
    check((caps & TORIRSMAPED_CAP_SPAWN_VIEW) != 0, "a bound server reports the bit");

    put_camera(payload, camera);
    raw_send(&pipe, TORIRSMAPED_REQ_SPAWN_VIEW, payload, 4 * TORIRSMAPED_CAMERA_VALUES);
    ToriRSMapEd_SessionPump(&session);
    check(
        raw_take(&pipe, &type, payload, &length) && type == TORIRSMAPED_FACT_STATUS
            && ToriRSMapEd_ReadU32(payload + 4) == EDITOR_HOST_OK
            && ToriRSMapEd_ReadU32(payload + 8) == 4242,
        "SPAWN_VIEW answers OK with the viewer's pid");
    check(
        fake.calls == 1 && fake.group == group
            && memcmp(fake.camera, camera, sizeof(camera)) == 0,
        "the binding got the requester's Client and the pose");

    {
        int32_t bad[TORIRSMAPED_CAMERA_VALUES];
        memcpy(bad, camera, sizeof(bad));
        bad[TORIRSMAPED_CAMERA_LEVEL] = 9;
        put_camera(payload, bad);
        raw_send(&pipe, TORIRSMAPED_REQ_SPAWN_VIEW, payload, 4 * TORIRSMAPED_CAMERA_VALUES);
        ToriRSMapEd_SessionPump(&session);
        check(
            raw_take(&pipe, &type, payload, &length) && type == TORIRSMAPED_FACT_STATUS
                && ToriRSMapEd_ReadU32(payload + 4) == EDITOR_HOST_REFUSED && fake.calls == 1,
            "a pose the viewer would refuse is REFUSED, and nothing is spawned");
    }

    raw_send(&pipe, TORIRSMAPED_REQ_SPAWN_VIEW, payload, 8);
    check(!ToriRSMapEd_SessionPump(&session), "a short SPAWN_VIEW ends the session (framing)");
    ToriRSMapEd_SessionFree(&session);
    ToriRSMapEd_BufFree(&pipe.to_server);
    ToriRSMapEd_BufFree(&pipe.to_client);

    /* A v1 peer never speaks SPAWN_VIEW; one that does is malformed. */
    transport.ctx = &v1_pipe;
    ToriRSMapEd_SessionInit(&v1_session, &server, &transport);
    hello(&v1_pipe, &v1_session, TORIRSMAPED_PROTO_VERSION_V1, &caps, &group);
    put_camera(payload, camera);
    raw_send(&v1_pipe, TORIRSMAPED_REQ_SPAWN_VIEW, payload, 4 * TORIRSMAPED_CAMERA_VALUES);
    check(!ToriRSMapEd_SessionPump(&v1_session) && fake.calls == 1,
        "a v1 peer's SPAWN_VIEW closes it");
    ToriRSMapEd_SessionFree(&v1_session);
    ToriRSMapEd_BufFree(&v1_pipe.to_server);
    ToriRSMapEd_BufFree(&v1_pipe.to_client);

    ToriRSMapEd_SetViewSpawner(&server, NULL);
    check((server.capabilities & TORIRSMAPED_CAP_SPAWN_VIEW) == 0, "clearing the binding clears the bit");
    ToriRSMapEd_Close(&server);
}

/* ---- the camera's text ------------------------------------------------------ */

static void
test_camera_text(void)
{
    int32_t values[TORIRSMAPED_CAMERA_VALUES];
    int32_t back[TORIRSMAPED_CAMERA_VALUES];
    char text[96];
    int roundtrips = 1;

    check(ToriRSMapEd_CameraParse("3222,3218,0,0,300,1200", values), "whole tiles parse");
    check(values[0] == 3222 * 128 && values[1] == 3218 * 128 && values[4] == 300,
        "x/z become fine units");
    check(ToriRSMapEd_CameraParse("3222.5,3218.25,1,2047,383,65535", values),
        "fractional tiles parse");
    check(values[0] == 3222 * 128 + 64 && values[1] == 3218 * 128 + 32 && values[2] == 1,
        "a fraction rounds to the nearest 1/128");
    ToriRSMapEd_CameraFormat(values, text, sizeof(text));
    check(strcmp(text, "3222.5,3218.25,1,2047,383,65535") == 0, "and formats back the same");

    for( int32_t fine = 0; fine < 128 * 3; fine++ )
    {
        int32_t pose[TORIRSMAPED_CAMERA_VALUES] = { 3200 * 128 + fine, fine, 3, 7, 9, 11 };
        ToriRSMapEd_CameraFormat(pose, text, sizeof(text));
        if( !ToriRSMapEd_CameraParse(text, back) || memcmp(back, pose, sizeof(pose)) != 0 )
            roundtrips = 0;
    }
    check(roundtrips, "every 1/128 step round-trips through the text exactly");

    check(!ToriRSMapEd_CameraParse("3222,3218,4,0,300,1200", values), "level 4 is refused");
    check(!ToriRSMapEd_CameraParse("3222,3218,0,2048,300,1200", values), "yaw 2048 is refused");
    check(!ToriRSMapEd_CameraParse("3222,3218,0,0,300", values), "five fields are refused");
    check(!ToriRSMapEd_CameraParse("3222,3218,0,0,300,1200,1", values), "seven are refused");
    check(!ToriRSMapEd_CameraParse("-1,3218,0,0,300,1200", values), "a negative tile is refused");
    check(!ToriRSMapEd_CameraParse("16384,0,0,0,0,0", values), "past the map is refused");
    check(!ToriRSMapEd_CameraParse("3222.,0,0,0,0,0", values), "a bare point is refused");
    check(!ToriRSMapEd_CameraParse("", values), "empty is refused");
}

/* ---- the launch service's `view` verb ---------------------------------------- */

struct FakeLaunch
{
    int spawns;
    char argv_line[2048];
    char log_path[1024];
    int has_prefs_blank;
    int has_party_join;
    int signals;
    int alive;
};

static int
fl_spawn(
    void* context,
    char* const* argv,
    char* const* environment,
    char const* log_path,
    int process_group,
    int* out_pid,
    int* out_process_group)
{
    struct FakeLaunch* fake = context;
    (void)process_group;
    fake->spawns++;
    fake->argv_line[0] = '\0';
    for( int i = 0; argv[i]; i++ )
    {
        strncat(fake->argv_line, argv[i], sizeof(fake->argv_line) - strlen(fake->argv_line) - 2);
        strncat(fake->argv_line, " ", sizeof(fake->argv_line) - strlen(fake->argv_line) - 1);
    }
    snprintf(fake->log_path, sizeof(fake->log_path), "%s", log_path);
    fake->has_prefs_blank = 0;
    fake->has_party_join = 0;
    for( int i = 0; environment && environment[i]; i++ )
    {
        if( strcmp(environment[i], "TORIRS_PREFS=") == 0 )
            fake->has_prefs_blank = 1;
        if( strncmp(environment[i], "TORIRS_EMBED_PARTY_JOIN=", 24) == 0 )
            fake->has_party_join = 1;
    }
    fake->alive = 1;
    *out_pid = 5000 + fake->spawns;
    *out_process_group = 5000 + fake->spawns;
    return 0;
}

static int
fl_signal(
    void* context,
    int pid,
    int signal_number)
{
    struct FakeLaunch* fake = context;
    (void)pid;
    (void)signal_number;
    fake->signals++;
    fake->alive = 0;
    return 0;
}

static int
fl_alive(
    void* context,
    int pid)
{
    struct FakeLaunch* fake = context;
    (void)pid;
    return fake->alive;
}

static int
fl_reap(
    void* context,
    int pid,
    int block,
    int* out_exit_code,
    int* out_signal)
{
    struct FakeLaunch* fake = context;
    (void)pid;
    (void)block;
    if( fake->alive )
        return 0;
    *out_exit_code = -1;
    *out_signal = 15;
    return 1;
}

static int64_t
fl_clock(void* context)
{
    (void)context;
    return 1000;
}

static char*
launch(
    struct LaunchSessions* table,
    char const* verb,
    char const* body)
{
    int size = 0;
    return LaunchSessions_Answer(table, verb, body, (int)strlen(body), "local", &size);
}

static void
test_launch_view(char const* scratch)
{
    static struct FakeLaunch fake;
    struct LaunchSessions_Spawner spawner;
    struct LaunchSessions_Config config;
    struct LaunchSessions* table;
    char directory[1024];
    char body[512];
    char expect[1024];
    char* answer;
    struct EditorViewCamera const camera = pose(3222, 3218, 0, 512, 300, 1200);
    int length;

    memset(&spawner, 0, sizeof(spawner));
    spawner.context = &fake;
    spawner.spawn = fl_spawn;
    spawner.signal_process = fl_signal;
    spawner.signal_group = fl_signal;
    spawner.process_alive = fl_alive;
    spawner.reap_child = fl_reap;
    snprintf(directory, sizeof(directory), "%s/launch", scratch);

    memset(&config, 0, sizeof(config));
    config.binary_path = "/opt/torirs/torirs";
    config.launch_directory = directory;
    config.spawner = &spawner;
    config.clock_ms = fl_clock;
    table = LaunchSessions_New(&config);
    answer = launch(table, "view", "maped=tcp://127.0.0.1:43610\ngroup=3\n");
    check(strncmp(answer, "error: this host was started without --manifest", 47) == 0,
        "a host with no manifest refuses a view");
    ToriMem_Free(ToriMem_Net(), answer);
    LaunchSessions_Free(table);

    config.manifest_path = "manifests/manifest_osrs242_mapeditor.ini";
    table = LaunchSessions_New(&config);

    length = EditorView_SpawnBody("tcp://127.0.0.1:43610", 3, &camera, NULL, body, (int)sizeof(body));
    check(length > 0 && strstr(body, "camera=3222.5,3218.5,0,512,300,1200\n") != NULL,
        "EditorView_SpawnBody writes the launch lines");
    answer = launch(table, "view", body);
    check(strncmp(answer, "ok\nview=1\npid=5001\n", 19) == 0, "the view verb starts a viewer");
    ToriMem_Free(ToriMem_Net(), answer);
    snprintf(expect, sizeof(expect),
        "/opt/torirs/torirs --manifest manifests/manifest_osrs242_mapeditor.ini --editor-view "
        "--maped tcp://127.0.0.1:43610 --maped-group 3 --camera 3222.5,3218.5,0,512,300,1200 ");
    check(strcmp(fake.argv_line, expect) == 0, "with exactly the viewer command line");
    if( strcmp(fake.argv_line, expect) != 0 )
        fprintf(stderr, "  got:  %s\n  want: %s\n", fake.argv_line, expect);
    check(strstr(fake.log_path, "/launch/views/view1.log") != NULL, "logging under the launch dir");
    check(fake.has_prefs_blank && !fake.has_party_join,
        "never the owner's preferences, never a party seat's env");

    /* A popped-out panel: the body names it, the window boots with it. */
    length = EditorView_SpawnBody("tcp://127.0.0.1:51129", 3, &camera, "terrain", body, (int)sizeof(body));
    check(length > 0 && strstr(body, "\npanel=terrain\n") != NULL, "a panel's spawn body names the panel");
    check(EditorView_SpawnBody("tcp://127.0.0.1:51129", 3, &camera, "world", expect, (int)sizeof(expect)) > 0
              && strstr(expect, "\npanel=world\n"),
        "a popped-out World's body names it");
    check(EditorView_SpawnBody("tcp://127.0.0.1:51129", 3, &camera, NULL, expect, (int)sizeof(expect)) > 0
              && !strstr(expect, "panel="),
        "a plain viewer's body names none");
    answer = launch(table, "view", body);
    check(strncmp(answer, "ok\nview=2\n", 10) == 0, "the view verb starts a panel window");
    ToriMem_Free(ToriMem_Net(), answer);
    snprintf(expect, sizeof(expect),
        "/opt/torirs/torirs --manifest manifests/manifest_osrs242_mapeditor.ini --editor-view "
        "--maped tcp://127.0.0.1:51129 --maped-group 3 --camera 3222.5,3218.5,0,512,300,1200 "
        "--editor-panel terrain ");
    check(strcmp(fake.argv_line, expect) == 0, "booted --editor-panel terrain");
    if( strcmp(fake.argv_line, expect) != 0 )
        fprintf(stderr, "  got:  %s\n  want: %s\n", fake.argv_line, expect);
    answer = launch(table, "view", "maped=tcp://127.0.0.1:43610\ngroup=3\npanel=../x\n");
    check(strncmp(answer, "error: launch/view panel=", 25) == 0, "a panel that is not a module word is refused");
    ToriMem_Free(ToriMem_Net(), answer);
    answer = launch(table, "view", "maped=tcp://127.0.0.1:43610\ngroup=3\npanel=--connect\n");
    check(strncmp(answer, "error: launch/view panel=", 25) == 0, "a panel smuggling a flag is refused");
    ToriMem_Free(ToriMem_Net(), answer);
    /* The process side: the name main.c hands over, "world" being the view. */
    EditorView_SetBootPanel("terrain");
    check(EditorView_BootPanel() && strcmp(EditorView_BootPanel(), "terrain") == 0, "the boot panel is kept");
    EditorView_SetBootPanel("world");
    check(EditorView_BootPanel() && strcmp(EditorView_BootPanel(), "world") == 0, "world boots the World panel alone");
    EditorView_SetBootPanel(NULL);
    check(EditorView_BootPanel() == NULL, "no panel boots a plain viewer");

    answer = launch(table, "view", "maped=http://x\ngroup=3\n");
    check(strncmp(answer, "error: launch/view needs maped=", 31) == 0, "a non-maped URL is refused");
    ToriMem_Free(ToriMem_Net(), answer);
    answer = launch(table, "view", "maped=tcp://h:1 --connect evil\ngroup=3\n");
    check(strncmp(answer, "error:", 6) == 0, "a URL smuggling a flag is refused");
    ToriMem_Free(ToriMem_Net(), answer);
    answer = launch(table, "view", "maped=tcp://127.0.0.1:43610\n");
    check(strncmp(answer, "error: launch/view needs group=", 31) == 0, "a view needs its Client");
    ToriMem_Free(ToriMem_Net(), answer);
    answer = launch(table, "view", "maped=tcp://127.0.0.1:43610\ngroup=3\ncamera=1,2,3\n");
    check(strncmp(answer, "error: launch/view camera=", 26) == 0, "a three-field camera is refused");
    ToriMem_Free(ToriMem_Net(), answer);
    answer = launch(table, "view", "maped=tcp://127.0.0.1:43610\ngroup=3\nprogram=/bin/sh\n");
    check(strncmp(answer, "error: launch/view does not take program=", 41) == 0,
        "a view names no program");
    ToriMem_Free(ToriMem_Net(), answer);
    check(fake.spawns == 2, "none of the refusals spawned anything");

    fake.signals = 0;
    LaunchSessions_Free(table);
    check(fake.signals > 0 && !fake.alive, "the host's exit ends its views");
}

/* ---- the orbit pose <-> eye --------------------------------------------------- */

static void
test_pose_eye(void)
{
    int worst = 0;
    int cases = 0;

    for( int yaw = 0; yaw < 2048; yaw += 97 )
    {
        for( int pitch = 128; pitch <= 383; pitch += 51 )
        {
            for( int zoom = 400; zoom <= 4000; zoom += 900 )
            {
                struct EditorViewCamera const pose = { 3222 * 128 + 37, 3218 * 128 + 101, 1, yaw,
                                                       pitch, zoom };
                struct EditorViewCamera back;
                struct ToriDraw_Position eye;
                int const base_x = 3176 * 128;
                int const base_z = 3168 * 128;
                int error;

                EditorView_PoseEye(&pose, base_x, base_z, -240, &eye);
                /* Flat ground: the ray from that eye lands back on the point. */
                EditorView_PoseFromEye(&eye, yaw, pitch, -240, base_x, base_z, 1, 999, &back);
                error = abs(back.x - pose.x);
                if( abs(back.z - pose.z) > error )
                    error = abs(back.z - pose.z);
                if( abs(back.zoom - pose.zoom) > error )
                    error = abs(back.zoom - pose.zoom);
                if( back.yaw != pose.yaw || back.pitch != pose.pitch || back.level != 1 )
                    error = 1 << 20;
                if( error > worst )
                {
                    worst = error;
                    if( error > 4 )
                        fprintf(stderr, "  yaw %d pitch %d zoom %d: eye (%d,%d,%d) -> x %d z %d zoom %d\n",
                            yaw, pitch, zoom, eye.x, eye.y, eye.z, back.x - pose.x, back.z - pose.z,
                            back.zoom);
                }
                cases++;
            }
        }
    }
    printf("views_embed_test: pose -> eye -> pose over %d poses, worst error %d fine units\n",
        cases, worst);
    check(worst <= 4, "an orbit pose survives the trip through a free camera's eye");

    {
        struct ToriDraw_Position eye;
        struct EditorViewCamera back;
        memset(&eye, 0, sizeof(eye));
        eye.x = 1000;
        eye.y = -100;
        eye.z = 1000;
        EditorView_PoseFromEye(&eye, 0, 256, -500, 0, 0, 0, 1234, &back);
        check(back.zoom == 1234, "an eye below the ground keeps the view's distance");
    }
}

/* ---- the manifest's viewer keys ---------------------------------------------- */

static void
test_manifest(char const* scratch)
{
    static struct BootManifest bm;
    char path[1024];
    char const* const identity = "[cache:boot]\nepoch=dat2\ngame=oldschool\nrevision=241\n"
                                 "quirks=none\ndir=x\n";
    char text[1024];

    snprintf(path, sizeof(path), "%s/view_manifest.ini", scratch);
    snprintf(text, sizeof(text),
        "%s[editor:boot]\nserver=tcp\nport=43610\nclient=4\npanel=view\n"
        "camera=3222.5,3218,0,512,300,1200\n",
        identity);
    write_file(path, text, strlen(text));
    memset(&bm, 0, sizeof(bm));
    check(BootManifest_LoadFile(&bm, path) == 0, "a panel=view manifest on server=tcp loads");
    check(bm.editor_panel == BOOTMANIFEST_EDITOR_PANEL_VIEW, "panel=view is a view boot");
    check(strcmp(bm.editor_camera, "3222.5,3218,0,512,300,1200") == 0, "camera= is kept as text");

    snprintf(text, sizeof(text), "%s[editor:boot]\ncontent_dir=x\npanel=view\n", identity);
    write_file(path, text, strlen(text));
    memset(&bm, 0, sizeof(bm));
    check(BootManifest_LoadFile(&bm, path) != 0,
        "panel=view with the embedded server is refused at load");

    snprintf(text, sizeof(text), "%s[editor:boot]\ncontent_dir=x\npanel=window\n", identity);
    write_file(path, text, strlen(text));
    memset(&bm, 0, sizeof(bm));
    check(BootManifest_LoadFile(&bm, path) != 0, "an unknown panel= is still refused");
}

/* ---- the daemon end to end ------------------------------------------------- */

static int
tcp_connect(int port)
{
    for( int attempt = 0; attempt < 100; attempt++ )
    {
        struct sockaddr_in addr;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t)port);
        if( connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0 )
            return fd;
        close(fd);
        usleep(50 * 1000);
    }
    return -1;
}

static int
tcp_frame(
    int fd,
    uint32_t type,
    const uint8_t* payload,
    uint32_t length)
{
    uint8_t header[8];
    ToriRSMapEd_WriteU32(header, type);
    ToriRSMapEd_WriteU32(header + 4, length);
    if( write(fd, header, 8) != 8 )
        return 0;
    return length == 0 || write(fd, payload, length) == (ssize_t)length;
}

static int
tcp_take(
    int fd,
    uint32_t* out_type,
    uint8_t* out_payload,
    uint32_t* out_length)
{
    uint8_t header[8];
    size_t got = 0;
    while( got < 8 )
    {
        ssize_t n = read(fd, header + got, 8 - got);
        if( n <= 0 )
            return 0;
        got += (size_t)n;
    }
    *out_type = ToriRSMapEd_ReadU32(header);
    *out_length = ToriRSMapEd_ReadU32(header + 4);
    got = 0;
    while( got < *out_length )
    {
        /* A body past the caller's 256 bytes (a live status with its
         * message) is read off the socket and dropped, so the next frame
         * still starts at a header. */
        uint8_t sink[256];
        size_t const room = *out_length - got;
        ssize_t n = got < 256 ? read(fd, out_payload + got, room < 256 - got ? room : 256 - got)
                              : read(fd, sink, room < sizeof(sink) ? room : sizeof(sink));
        if( n <= 0 )
            return 0;
        got += (size_t)n;
    }
    return *out_length <= 256;
}

static void
test_daemon(
    char const* scratch,
    char const* daemon,
    char const* manifest)
{
    char script[1024];
    char argv_out[1024];
    char content[1024];
    char text[2048];
    char expect[1024];
    char port_text[16];
    char const* env_port = getenv("TORIRSMAPED_VIEWS_TEST_PORT");
    int const port = env_port ? atoi(env_port) : 43619;
    pid_t pid;
    int fd;
    uint8_t payload[256];
    uint32_t type;
    uint32_t length;
    uint32_t group = 0;
    uint32_t caps = 0;
    int spawned_pid = 0;
    int32_t const camera[TORIRSMAPED_CAMERA_VALUES] = { 3222 * 128 + 64, 3218 * 128, 1, 512, 300,
                                                        1200 };

    snprintf(content, sizeof(content), "%s", scratch);
    snprintf(script, sizeof(script), "%s/fake_viewer.sh", scratch);
    snprintf(argv_out, sizeof(argv_out), "%s/fake_viewer.argv", scratch);
    unlink(argv_out);
    snprintf(text, sizeof(text), "#!/bin/sh\necho \"$@\" > '%s'\nexit 0\n", argv_out);
    check(write_file(script, text, strlen(text)) && chmod(script, 0755) == 0,
        "a script stands in for the client binary");
    snprintf(port_text, sizeof(port_text), "%d", port);

    pid = fork();
    if( pid == 0 )
    {
        if( chdir(scratch) != 0 )
            _exit(126);
        execl(daemon, daemon, port_text, "--content-dir", content, "--repo-root", "",
            "--view-binary", script, "--view-manifest", manifest, (char*)NULL);
        _exit(127);
    }
    fd = tcp_connect(port);
    check(fd >= 0, "the daemon listens");
    if( fd < 0 )
    {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return;
    }
    ToriRSMapEd_WriteU32(payload, TORIRSMAPED_PROTO_VERSION);
    ToriRSMapEd_WriteU32(payload + 4, TORIRSMAPED_ROLE_CONTROLLER);
    ToriRSMapEd_WriteU32(payload + 8, 0);
    tcp_frame(fd, TORIRSMAPED_REQ_HELLO, payload, 12);
    if( tcp_take(fd, &type, payload, &length) && type == TORIRSMAPED_FACT_HELLO && length == 16 )
    {
        group = ToriRSMapEd_ReadU32(payload + 8);
        caps = ToriRSMapEd_ReadU32(payload + 12);
    }
    /* The lane's state follows the greeting (ToriRSMapEd_LiveWelcome). */
    (void)tcp_take(fd, &type, payload, &length);
    (void)tcp_take(fd, &type, payload, &length);
    check(group != 0 && (caps & TORIRSMAPED_CAP_SPAWN_VIEW), "the daemon offers SPAWN_VIEW");

    put_camera(payload, camera);
    tcp_frame(fd, TORIRSMAPED_REQ_SPAWN_VIEW, payload, 4 * TORIRSMAPED_CAMERA_VALUES);
    if( tcp_take(fd, &type, payload, &length) && type == TORIRSMAPED_FACT_STATUS
        && ToriRSMapEd_ReadU32(payload + 4) == EDITOR_HOST_OK )
        spawned_pid = (int)ToriRSMapEd_ReadU32(payload + 8);
    check(spawned_pid > 0, "SPAWN_VIEW starts a viewer process and says its pid");

    text[0] = '\0';
    for( int waited = 0; waited < 100; waited++ )
    {
        size_t size;
        char* got = slurp(argv_out, &size);
        if( got && size > 0 )
        {
            snprintf(text, sizeof(text), "%s", got);
            free(got);
            break;
        }
        free(got);
        usleep(50 * 1000);
    }
    snprintf(expect, sizeof(expect),
        "--manifest %s --editor-view --maped tcp://127.0.0.1:%d --maped-group %u "
        "--camera 3222.5,3218,1,512,300,1200\n",
        manifest, port, (unsigned)group);
    check(strcmp(text, expect) == 0, "the viewer got exactly the --editor-view command line");
    if( strcmp(text, expect) != 0 )
        fprintf(stderr, "  got:  %s  want: %s", text, expect);

    close(fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

int
main(
    int argc,
    char** argv)
{
    char const* content;
    char const* scratch;
    char path[1024];
    char from[1024];
    char* data;
    size_t size;
    struct RSCache profile;

    if( argc < 3 )
    {
        fprintf(stderr,
            "usage: views_embed_test <content-dir> <scratch-dir> [<torirsmaped>] [<manifest>]\n");
        return 2;
    }
    content = argv[1];
    scratch = argv[2];

    /* m50_50 into a tree of the test's own; the content tree is read only. */
    snprintf(path, sizeof(path), "%s/maps", scratch);
    mkdir(path, 0755);
    for( int half = 0; half < 2; half++ )
    {
        char const* ext = half ? "jl2" : "jm2";
        snprintf(from, sizeof(from), "%s/maps/m50_50.%s", content, ext);
        data = slurp(from, &size);
        check(data != NULL, "m50_50 reads from the content tree");
        if( !data )
            return 1;
        snprintf(path, sizeof(path), "%s/maps/m50_50.%s", scratch, ext);
        check(write_file(path, data, size), "and copies into the scratch tree");
        free(data);
    }

    if( !RSCache_ProfileByName("osrs242", &profile) )
    {
        fprintf(stderr, "views_embed_test: no osrs242 cache profile\n");
        return 1;
    }

    test_camera_text();
    /* The trig tables the orbit math reads (App_Init does this in a client). */
    ToriDraw_InitMath();
    test_pose_eye();
    test_three_connections(scratch, &profile);
    test_embed_listener(scratch, &profile);
    test_spawn_view_frames(scratch);
    test_launch_view(scratch);
    test_manifest(scratch);
    if( argc >= 5 )
        test_daemon(scratch, argv[3], argv[4]);
    else
        printf("views_embed_test: no torirsmaped given -- the daemon's SPAWN_VIEW is not run\n");

    printf("views_embed_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
