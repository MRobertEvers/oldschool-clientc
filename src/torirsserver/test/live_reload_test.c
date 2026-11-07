/*
 * The live reload, against the real server, the real content and two real
 * script packs (torirs_server_control.h, docs/MAPED_DESIGN.html #run).
 *
 *   1. A player parked in p_pausebutton across a script reload finishes the
 *      OLD dialogue, and the next click runs the NEW one; the old table is
 *      kept while the player is parked on it and freed when they are not.
 *   2. A square reload sends exactly one REBUILD to the player whose window
 *      covers it, and none for a square nobody's window covers.
 *   3. The edited lane's spawns: a file added, then emptied, stands an npc up
 *      and takes it away; the generated lane is refused as restart-needed.
 *   4. The control link: 0xED on a real loopback socket through the
 *      connection layer's first-byte select, HELLO, STATUS, RELOAD (lines and
 *      RELOAD_DONE), CHEAT, SUBSCRIBE, BYE; and behind the gate a HELLO
 *      without a good ticket is refused.
 *
 * The packs: the script tree is staged under the work directory by the
 * builders' runner (editor/builder/builder_ssc.c, which copies what sscompile
 * reads and nothing else), a probe debugproc is written over the stage, and
 * the real sscompile builds pack A; the probe is rewritten and the same out
 * directory rebuilt incrementally into pack B -- ids stable, as the contract
 * needs. Nothing is written under the content tree.
 *
 *   live_reload_test <content_dir> <cache_dir> <sscompile> <work_dir>
 */
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_boot.h"
#include "torirsserver/torirs_server_content.h"
#include "torirsserver/torirs_server_control.h"
#include "torirsserver/torirs_server_scene.h"
#include "torirsserver/torirs_server_servpack.h"
#include "torirsserver/torirs_server_shop.h"
#include "torirsserver/torirs_server_varps.h"
#include "torirsserver/torirs_server_wire.h"
#include "torirsserver/torirs_server_ws.h"
#include "torirsserver/torirs_server_auth.h"
#include "editor/builder/builder_ssc.h"
#include "platform/launch_sessions.h"
#include "mem/torirs_mem.h"
#include "net/rev/pktnames.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/rev_list.h"
#include "net/rev/revpacket.h"
#include "net/torirs_control.h"

#include <datatypes/dat2_configs.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    printf("live-reload: %-66s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

/* ------------------------------------------------------------------ */
/* Packs                                                               */
/* ------------------------------------------------------------------ */

static const char k_probe_rel[] = "livereload_test/livereload_probe.rs2";

static void
probe_text(
    char* out,
    size_t cap,
    const char* which)
{
    snprintf(out, cap,
             "// Written by src/torirsserver/test/live_reload_test.c into its staged copy\n"
             "// of the tree, never into OSRS-Content.\n"
             "[debugproc,livereload_probe]\n"
             "mes(\"livereload: %s page\");\n"
             "~mesbox(\"Livereload %s page.\");\n"
             "mes(\"livereload: %s done\");\n",
             which, which, which);
}

/* Regular files of `from` into `to` (created), recursively. */
static int
copy_tree(
    const char* from,
    const char* to)
{
    DIR* dir = opendir(from);
    struct dirent* entry;
    int ok = 1;

    if( !dir )
        return 0;
    mkdir(to, 0755);
    while( ok && (entry = readdir(dir)) )
    {
        char src[2048];
        char dst[2048];
        struct stat st;

        if( strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            continue;
        snprintf(src, sizeof(src), "%s/%s", from, entry->d_name);
        snprintf(dst, sizeof(dst), "%s/%s", to, entry->d_name);
        if( stat(src, &st) != 0 )
            continue;
        if( S_ISDIR(st.st_mode) )
        {
            ok = copy_tree(src, dst);
            continue;
        }
        {
            FILE* in = fopen(src, "rb");
            FILE* out = in ? fopen(dst, "wb") : NULL;
            char buffer[65536];
            size_t n;

            if( !in || !out )
                ok = 0;
            while( ok && (n = fread(buffer, 1, sizeof(buffer), in)) > 0 )
                ok = fwrite(buffer, 1, n, out) == n;
            if( in )
                fclose(in);
            if( out )
                fclose(out);
        }
    }
    closedir(dir);
    return ok;
}

static int
build_packs(
    const char* content_dir,
    const char* sscompile,
    const char* work_dir,
    char* pack_a,
    char* pack_b,
    size_t cap)
{
    static struct Builder_Ssc ssc;
    static struct Builder_SscResult result;
    char text[1024];
    char marker[1200];
    char restage_dat[1300];
    struct stat st;

    if( !Builder_SscInit(&ssc, sscompile, content_dir, work_dir) )
        return 0;
    /*
     * The packs of an earlier run that built both, unless asked again
     * (LIVE_RELOAD_RESTAGE=1). The stage copies the LIVE content tree, which
     * other sessions edit: a unit somebody is half-way through fails the whole
     * compile, and that says nothing about the reload under test.
     */
    snprintf(marker, sizeof(marker), "%s/packs.ok", work_dir);
    snprintf(pack_a, cap, "%s/pack_a", work_dir);
    snprintf(pack_b, cap, "%s", ssc.out);
    snprintf(restage_dat, sizeof(restage_dat), "%s/script.dat", pack_a);
    if( !getenv("LIVE_RELOAD_RESTAGE") && stat(marker, &st) == 0 && stat(restage_dat, &st) == 0 )
    {
        printf("live-reload: reusing packs A and B from %s (LIVE_RELOAD_RESTAGE=1 rebuilds)\n",
               work_dir);
        return 1;
    }
    remove(marker);
    if( !Builder_SscStage(&ssc) )
        return 0;
    probe_text(text, sizeof(text), "old");
    if( !Builder_SscPut(&ssc, k_probe_rel, text, strlen(text)) )
        return 0;
    if( Builder_SscRun(&ssc, BUILDER_SSC_COMPILE, &result) != BUILDER_SSC_OK )
    {
        printf("live-reload: pack A: %s (see %s)\n", result.summary, result.log_path);
        return 0;
    }
    printf("live-reload: pack A: %s\n", result.summary);
    snprintf(pack_a, cap, "%s/pack_a", work_dir);
    if( !copy_tree(ssc.out, pack_a) )
        return 0;
    probe_text(text, sizeof(text), "new");
    if( !Builder_SscPut(&ssc, k_probe_rel, text, strlen(text)) )
        return 0;
    if( Builder_SscRun(&ssc, BUILDER_SSC_COMPILE, &result) != BUILDER_SSC_OK )
    {
        printf("live-reload: pack B: %s (see %s)\n", result.summary, result.log_path);
        return 0;
    }
    printf("live-reload: pack B: %s\n", result.summary);
    snprintf(pack_b, cap, "%s", ssc.out);
    {
        FILE* done = fopen(marker, "wb");

        if( done )
            fclose(done);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The world                                                           */
/* ------------------------------------------------------------------ */

static struct ToriRSServerCapture g_capture;

/* One tick, with the scene barrier acknowledged as a client would. */
static void
tick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    if( player->login_scene_pending || player->rebuild_scene_pending )
        ToriRSServer_WorldInput(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    ToriRSServer_WorldTick(srv);
}

/* Does the capture hold a MESSAGE_GAME to `player` containing `needle`? */
static int
said(
    struct ToriRSServer* srv,
    const char* needle)
{
    for( int i = ToriRSServer_CaptureFindNamed(&g_capture, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(&g_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
    {
        char text[TORIRSSERVER_CAPTURE_BYTES];

        if( ToriRSServer_WireReadMessageGame(srv->wire, g_capture.packets[i].data,
                                             g_capture.packets[i].len, text, (int)sizeof(text)) &&
            strstr(text, needle) )
            return 1;
    }
    return 0;
}

static int
count_named(int pkt_name)
{
    int count = 0;

    for( int i = ToriRSServer_CaptureFindNamed(&g_capture, pkt_name, 0); i >= 0;
         i = ToriRSServer_CaptureFindNamed(&g_capture, pkt_name, i + 1) )
        count++;
    return count;
}

static void
test_pausebutton_across_reload(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    const char* pack_a,
    const char* pack_b)
{
    struct ToriRSServerReloadRequest* request = calloc(1, sizeof(*request));
    struct ToriRSServerReloadResult* result = calloc(1, sizeof(*result));
    int uid;

    check(request && result, "request and result allocated");
    ToriRSServer_WorldSetActive(srv, player);

    ToriRSServer_CaptureReset(&g_capture);
    ToriRSServer_CaptureBegin(srv, &g_capture);
    ToriRSServer_ScriptsRunDebugproc(srv, "livereload_probe");
    ToriRSServer_CaptureEnd(srv);
    check(said(srv, "livereload: old page"), "pack A: the probe's first line is the old one");
    check(player->active_script != NULL, "the player is parked in the probe's dialogue");
    check(player->resume_button_count > 0, "the dialogue registered its continue button");
    uid = player->resume_button_count > 0 ? player->resume_buttons[0] : -1;

    /* As the wire names it: the pack's manifest file. */
    snprintf(request->script_dir, sizeof(request->script_dir), "%s/pack.manifest", pack_b);
    ToriRSServer_Reload(srv, request, result);
    check(result->status == TORIRSSERVER_RELOAD_OK, "RELOAD naming pack B's pack.manifest answers OK");
    check(result->scripts_swapped >= 1, "the reload names the probe as changed");
    check(ToriRSServer_ScriptsRetiredCount(srv) == 1,
          "the old table is kept while the player is parked on it");
    ToriRSServer_ScriptsRetireSweep(srv);
    check(ToriRSServer_ScriptsRetiredCount(srv) == 1,
          "a sweep keeps it: the parked state still runs on it");

    ToriRSServer_CaptureReset(&g_capture);
    ToriRSServer_CaptureBegin(srv, &g_capture);
    ToriRSServer_WorldSetActive(srv, player);
    check(uid >= 0 && ToriRSServer_ScriptsResumeButton(srv, uid) != 0,
          "the continue click resumes the parked dialogue");
    ToriRSServer_CaptureEnd(srv);
    check(said(srv, "livereload: old done"), "the parked dialogue finishes as it was written (old)");
    check(!said(srv, "livereload: new"), "and says nothing of the new text");
    check(player->active_script == NULL, "the player is no longer parked");
    ToriRSServer_ScriptsRetireSweep(srv);
    check(ToriRSServer_ScriptsRetiredCount(srv) == 0,
          "the old table is freed once nothing runs on it");

    ToriRSServer_CaptureReset(&g_capture);
    ToriRSServer_CaptureBegin(srv, &g_capture);
    ToriRSServer_WorldSetActive(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "livereload_probe");
    uid = player->resume_button_count > 0 ? player->resume_buttons[0] : -1;
    if( uid >= 0 )
        ToriRSServer_ScriptsResumeButton(srv, uid);
    ToriRSServer_CaptureEnd(srv);
    check(said(srv, "livereload: new page"), "the next click runs the new dialogue");
    check(said(srv, "livereload: new done"), "and the new dialogue finishes");
    check(!said(srv, "livereload: old"), "with nothing of the old one");

    /* The same pack again is a no-op; a pack that does not load leaves the
     * running table alone. */
    check(ToriRSServer_ScriptsLoad(srv, pack_b) > 0, "a login's ScriptsLoad of the same pack is a no-op");
    check(ToriRSServer_ScriptsRetiredCount(srv) == 0, "and retires nothing");
    snprintf(request->script_dir, sizeof(request->script_dir), "%s/does_not_exist", pack_a);
    ToriRSServer_Reload(srv, request, result);
    check(result->status == TORIRSSERVER_RELOAD_FAILED, "a pack that does not load answers FAILED");
    check(result->line_count > 0 && strstr(result->lines[0], "not swapped"),
          "and says why in a line");
    ToriRSServer_CaptureReset(&g_capture);
    ToriRSServer_CaptureBegin(srv, &g_capture);
    ToriRSServer_WorldSetActive(srv, player);
    ToriRSServer_ScriptsRunDebugproc(srv, "livereload_probe");
    ToriRSServer_CaptureEnd(srv);
    check(said(srv, "livereload: new page"), "the running table is still pack B");
    ToriRSServer_WorldSetActive(srv, player);
    ToriRSServer_ScriptsCloseDialogue(srv);
    free(request);
    free(result);
}

static void
test_square_reload(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int sx = player->x >> 6;
    int sz = player->z >> 6;
    int far_x = 20;
    int far_z = 20;
    int reloaded;

    for( int i = 0; i < 4; i++ )
        tick(srv, player);
    check(!player->login_scene_pending && !player->rebuild_scene_pending,
          "the player is past the login scene barrier");

    /* A square nobody's window covers: nothing owed. */
    ToriRSServer_CaptureReset(&g_capture);
    ToriRSServer_CaptureBegin(srv, &g_capture);
    reloaded = ToriRSServer_WorldReloadSquares(srv, &far_x, &far_z, 1);
    for( int i = 0; i < 3; i++ )
        tick(srv, player);
    ToriRSServer_CaptureEnd(srv);
    check(reloaded == 1, "a square nobody holds is current at once");
    check(count_named(PKT_NAME_REBUILD_NORMAL) + count_named(PKT_NAME_REBUILD_REGION) == 0,
          "no REBUILD for a square outside every window");

    ToriRSServer_CaptureReset(&g_capture);
    ToriRSServer_CaptureBegin(srv, &g_capture);
    reloaded = ToriRSServer_WorldReloadSquares(srv, &sx, &sz, 1);
    check(player->rebuild_pending == 1, "the covering player is owed a REBUILD");
    for( int i = 0; i < 4; i++ )
        tick(srv, player);
    ToriRSServer_CaptureEnd(srv);
    check(reloaded == 1, "the player's own square reloaded");
    check(count_named(PKT_NAME_REBUILD_NORMAL) + count_named(PKT_NAME_REBUILD_REGION) == 1,
          "exactly one REBUILD to the covering player");
    /* torirs_server_encode.c spends the mark on every rebuild it writes
     * (REBUILD_NORMAL has no reload field), so it is gone after the one sent. */
    check(ToriRSServer_WorldTakeRebuildReload(player) == 0,
          "the REBUILD sent spent the reload mark");

    /* The mark itself, read before any tick writes the rebuild: set once. */
    (void)ToriRSServer_WorldReloadSquares(srv, &sx, &sz, 1);
    check(ToriRSServer_WorldTakeRebuildReload(player) == 1,
          "the encoder is told the next REBUILD_REGION carries reload=1");
    check(ToriRSServer_WorldTakeRebuildReload(player) == 0, "once");
    for( int i = 0; i < 4; i++ )
        tick(srv, player);

    /* CACHE_INVALIDATE is a control frame (net/torirs_control.h), not a
     * packet: no opcode and no packet name. The reload sends it at the tick
     * boundary, before the REBUILD it owes goes out in the same tick, and on
     * the same connection that order is the order the client applies them. */
    {
        int archives[2] = { 16, 17 };
        int frame_at = -1;
        int rebuild_at = -1;

        ToriRSServer_CaptureReset(&g_capture);
        ToriRSServer_CaptureBegin(srv, &g_capture);
        (void)ToriRSServer_WorldReloadSquares(srv, &sx, &sz, 1);
        (void)ToriRSServer_SendCacheInvalidate(player, 5, archives, 2);
        for( int i = 0; i < 4; i++ )
            tick(srv, player);
        ToriRSServer_CaptureEnd(srv);
        for( int i = 0; i < g_capture.count; i++ )
        {
            const struct ToriRSServerCapturedPacket* p = &g_capture.packets[i];

            if( p->control_type == TORIRS_CONTROL_CACHE_INVALIDATE && frame_at < 0 )
            {
                struct TorirsControlFrame frame = { p->control_type, p->len, (uint8_t*)p->data };
                int index = -1;
                int count = -1;

                frame_at = i;
                check(p->name == PKT_NAME_NONE, "a control frame has no packet name");
                check(p->opcode == TORIRS_CONTROL_ESCAPE, "it goes out under the escape");
                check(torirs_control_invalidate_decode(&frame, &index, &count) && index == 5 &&
                          count == 2 && torirs_control_invalidate_archive(&frame, 0) == 16 &&
                          torirs_control_invalidate_archive(&frame, 1) == 17,
                      "its payload is index 5 and the two archives");
            }
            if( (p->name == PKT_NAME_REBUILD_NORMAL || p->name == PKT_NAME_REBUILD_REGION) &&
                rebuild_at < 0 )
                rebuild_at = i;
        }
        check(frame_at >= 0, "one CACHE_INVALIDATE control frame reaches the player");
        check(rebuild_at >= 0, "the reloaded square still owes its REBUILD");
        check(frame_at >= 0 && rebuild_at > frame_at, "the control frame goes out ahead of the REBUILD");
    }
}

static int
npc_at(
    struct ToriRSServer* srv,
    int type,
    int x,
    int z)
{
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        const struct ToriRSServerNpc* npc = &srv->npcs[slot];

        if( npc->active && !npc->pending_free && npc->type == type && npc->spawn_x == x &&
            npc->spawn_z == z )
            return 1;
    }
    return 0;
}

static int
write_text(
    const char* path,
    const char* text)
{
    FILE* file = fopen(path, "wb");

    if( !file )
        return 0;
    fputs(text, file);
    fclose(file);
    return 1;
}

static void
test_spawn_reload(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    const char* work_dir)
{
    char lane[1200];
    char path[1400];
    char row[256];
    const char* rel = "areas/edited/configs/livereload_test.spawn";
    const char* generated = "areas/world/configs/m50_50.spawn";
    int man = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "man");
    int x = player->x + 2;
    int z = player->z + 2;
    struct ToriRSServerReloadResult* result = calloc(1, sizeof(*result));
    int changed;

    check(man >= 0 && result, "the npc symbol `man` resolves");
    snprintf(lane, sizeof(lane), "%s/spawn_tree", work_dir);
    snprintf(path, sizeof(path), "%s/areas", lane);
    mkdir(lane, 0755);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/areas/edited", lane);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/areas/edited/configs", lane);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/%s", lane, rel);
    snprintf(row, sizeof(row), "==== NPC ====\nman %d %d %d\n", x, z, player->level);
    check(write_text(path, row), "the edited lane file is written under the work dir");

    check(!npc_at(srv, man, x, z), "no man stands there before the reload");
    changed = ToriRSServer_WorldReloadSpawns(srv, lane, &rel, 1, result);
    for( int i = 0; i < 2; i++ )
        tick(srv, player);
    check(changed == 1, "one spawn added");
    check(npc_at(srv, man, x, z), "the added spawn stands up beside the player");

    check(write_text(path, "==== NPC ====\n"), "the lane file is emptied");
    changed = ToriRSServer_WorldReloadSpawns(srv, lane, &rel, 1, result);
    for( int i = 0; i < 2; i++ )
        tick(srv, player);
    check(changed == 1, "one spawn removed");
    check(!npc_at(srv, man, x, z), "the removed spawn is taken out of the world");

    memset(result, 0, sizeof(*result));
    changed = ToriRSServer_WorldReloadSpawns(srv, lane, &generated, 1, result);
    check(changed == 0, "the generated lane is not reloaded");
    check(result->restart_count == 1 && strstr(result->restart[0], "boot-only"),
          "and is named restart-needed");
    free(result);
}

/* ------------------------------------------------------------------ */
/* The control link                                                    */
/* ------------------------------------------------------------------ */

static void
put_u32(
    uint8_t* at,
    uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

static uint32_t
get_u32(const uint8_t* at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

struct Frame
{
    uint8_t data[8192];
    int len;
};

static void
frame_u32(
    struct Frame* f,
    uint32_t value)
{
    put_u32(f->data + f->len, value);
    f->len += 4;
}

static void
frame_str(
    struct Frame* f,
    const char* text)
{
    size_t n = strlen(text);

    frame_u32(f, (uint32_t)n);
    memcpy(f->data + f->len, text, n);
    f->len += (int)n;
}

static int
send_frame(
    int fd,
    uint32_t type,
    const struct Frame* f)
{
    uint8_t header[8];

    put_u32(header, type);
    put_u32(header + 4, (uint32_t)(f ? f->len : 0));
    if( send(fd, header, 8, 0) != 8 )
        return 0;
    return !f || f->len == 0 || send(fd, f->data, (size_t)f->len, 0) == f->len;
}

/* Read whatever the server wrote within the time allowed. */
static int
drain(
    int fd,
    uint8_t* buffer,
    int cap,
    int have)
{
    struct pollfd p = { fd, POLLIN, 0 };

    while( have < cap && poll(&p, 1, 200) > 0 )
    {
        ssize_t n = recv(fd, buffer + have, (size_t)(cap - have), 0);

        if( n <= 0 )
            break;
        have += (int)n;
    }
    return have;
}

static int
find_frame(
    const uint8_t* buffer,
    int len,
    uint32_t type,
    int nth,
    const uint8_t** payload);

static void
tick(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player);

/*
 * Tick until a frame of `type` has come back (at most ten ticks), keeping
 * everything read. The link answers at the tick boundary that first SEES the
 * request, and loopback delivery is not synchronous with send(), so a request
 * written just before a tick may be read at the one after it -- which is the
 * contract, not a fault.
 */
static int
await_frame(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int fd,
    uint32_t type,
    uint8_t* buffer,
    int cap)
{
    int have = 0;
    const uint8_t* payload;

    for( int i = 0; i < 10; i++ )
    {
        tick(srv, player);
        have = drain(fd, buffer, cap, have);
        if( find_frame(buffer, have, type, 0, &payload) >= 0 )
            break;
    }
    return have;
}

/* The `nth` frame of `type` in `buffer`, its payload and length; -1 if none. */
static int
find_frame(
    const uint8_t* buffer,
    int len,
    uint32_t type,
    int nth,
    const uint8_t** payload)
{
    int at = 0;

    while( len - at >= 8 )
    {
        uint32_t t = get_u32(buffer + at);
        uint32_t n = get_u32(buffer + at + 4);

        if( (uint32_t)(len - at - 8) < n )
            break;
        if( t == type && nth-- == 0 )
        {
            *payload = buffer + at + 8;
            return (int)n;
        }
        at += 8 + (int)n;
    }
    return -1;
}

static int
lines_contain(
    const uint8_t* buffer,
    int len,
    const char* needle)
{
    const uint8_t* payload;
    int n;

    for( int i = 0; (n = find_frame(buffer, len, TORIRSSERVER_CTL_FACT_LINE, i, &payload)) >= 0; i++ )
    {
        char text[512];
        uint32_t length = get_u32(payload);

        if( length > sizeof(text) - 1 || (int)length + 4 > n )
            continue;
        memcpy(text, payload + 4, length);
        text[length] = '\0';
        if( strstr(text, needle) )
            return 1;
    }
    return 0;
}

/* A loopback pair: `client` and `server` ends of one TCP connection. */
static int
loopback_pair(
    int* client,
    int* server)
{
    struct sockaddr_in addr;
    socklen_t length = sizeof(addr);
    int listener = socket(AF_INET, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if( listener < 0 || bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(listener, 1) != 0 || getsockname(listener, (struct sockaddr*)&addr, &length) != 0 )
        return 0;
    *client = socket(AF_INET, SOCK_STREAM, 0);
    if( *client < 0 || connect(*client, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
        return 0;
    *server = accept(listener, NULL, NULL);
    close(listener);
    return *server >= 0;
}

/* The connection layer's first-byte select, as the host runs it, on the
 * server end; then the host lets its copy go. */
static int
open_through_select(
    int server,
    int gated)
{
    static struct ToriRSServerConn conn;
    enum ToriRSServerConnOpen open;

    ToriRSServer_ConnBegin(&conn, server);
    conn.require_ticket = gated;
    for( int i = 0; i < 50; i++ )
    {
        open = ToriRSServer_ConnOpenStep(&conn);
        if( open != TORIRSSERVER_CONN_OPENING )
            break;
        usleep(10000);
    }
    close(server);
    return open == TORIRSSERVER_CONN_OPEN_FAILED;
}

static void
test_control_link(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    static uint8_t in[1 << 20];
    int client = -1;
    int server = -1;
    int have = 0;
    uint8_t selector = TORIRSSERVER_CTL_FIRST_BYTE;
    struct Frame f;
    const uint8_t* payload;
    int n;

    check(loopback_pair(&client, &server), "a loopback connection to the game port's layer");
    check(send(client, &selector, 1, 0) == 1, "the editor says 0xED");
    memset(&f, 0, sizeof(f));
    frame_u32(&f, TORIRSSERVER_CTL_VERSION);
    frame_str(&f, "");
    check(send_frame(client, TORIRSSERVER_CTL_HELLO, &f), "and CTL_HELLO behind it");
    check(open_through_select(server, 0) && ToriRSServer_ControlCount() == 1,
          "the first-byte select hands it to the control link");

    have = await_frame(srv, player, client, TORIRSSERVER_CTL_FACT_HELLO, in, (int)sizeof(in));
    n = find_frame(in, have, TORIRSSERVER_CTL_FACT_HELLO, 0, &payload);
    check(n >= 8 && get_u32(payload) == TORIRSSERVER_CTL_VERSION && get_u32(payload + 4) == 1,
          "CTL_FACT_HELLO: version 1, accepted (loopback, no gate)");

    have = 0;
    check(send_frame(client, TORIRSSERVER_CTL_STATUS, NULL), "CTL_STATUS sent");
    have = await_frame(srv, player, client, TORIRSSERVER_CTL_FACT_STATUS, in, (int)sizeof(in));
    n = find_frame(in, have, TORIRSSERVER_CTL_FACT_STATUS, 0, &payload);
    check(n >= 8 && get_u32(payload) == 1, "CTL_FACT_STATUS: one player online");
    check(n >= 8 && get_u32(payload + 4) <= (uint32_t)srv->tick && get_u32(payload + 4) + 10 > (uint32_t)srv->tick,
          "and the world's tick");
    if( n >= 12 )
    {
        uint32_t stamp = get_u32(payload + 8);

        check(stamp > 0, "and the running script pack's stamp");
    }

    /* RELOAD: the player's square, nothing else. */
    memset(&f, 0, sizeof(f));
    frame_str(&f, "");
    frame_u32(&f, 0);
    frame_u32(&f, 1);
    frame_u32(&f, (uint32_t)(player->x >> 6));
    frame_u32(&f, (uint32_t)(player->z >> 6));
    frame_u32(&f, 0);
    check(send_frame(client, TORIRSSERVER_CTL_RELOAD, &f), "CTL_RELOAD of the player's square sent");
    ToriRSServer_CaptureReset(&g_capture);
    ToriRSServer_CaptureBegin(srv, &g_capture);
    have = await_frame(srv, player, client, TORIRSSERVER_CTL_FACT_RELOAD_DONE, in, (int)sizeof(in));
    /* And the REBUILD the reload owed, through its barrier. */
    for( int i = 0; i < 3; i++ )
        tick(srv, player);
    ToriRSServer_CaptureEnd(srv);
    check(lines_contain(in, have, "squares: 1 of 1 current"), "CTL_FACT_LINE says the square is current");
    n = find_frame(in, have, TORIRSSERVER_CTL_FACT_RELOAD_DONE, 0, &payload);
    check(n >= 24 && get_u32(payload) == TORIRSSERVER_RELOAD_OK, "CTL_FACT_RELOAD_DONE: status OK");
    check(n >= 24 && get_u32(payload + 12) == 1, "squares_reloaded = 1");
    check(count_named(PKT_NAME_REBUILD_NORMAL) + count_named(PKT_NAME_REBUILD_REGION) == 1,
          "and the player got exactly one REBUILD");
    ToriRSServer_WorldTakeRebuildReload(player);

    /* CHEAT: an engine cheat through content-then-ladder, its message back. */
    memset(&f, 0, sizeof(f));
    frame_str(&f, player->account_name);
    frame_str(&f, "::livereload_probe");
    check(send_frame(client, TORIRSSERVER_CTL_CHEAT, &f), "CTL_CHEAT ::livereload_probe sent");
    have = await_frame(srv, player, client, TORIRSSERVER_CTL_FACT_STATUS, in, (int)sizeof(in));
    check(lines_contain(in, have, "livereload: new page"), "its message comes back as a line");
    check(lines_contain(in, have, "cheat: ran"), "with the verdict");
    check(find_frame(in, have, TORIRSSERVER_CTL_FACT_STATUS, 0, &payload) >= 0,
          "then CTL_FACT_STATUS");
    ToriRSServer_WorldSetActive(srv, player);
    ToriRSServer_ScriptsCloseDialogue(srv);

    /* SUBSCRIBE: the current value at once, then each change. */
    memset(&f, 0, sizeof(f));
    frame_str(&f, player->account_name);
    frame_u32(&f, 1);
    frame_u32(&f, 1700);
    check(send_frame(client, TORIRSSERVER_CTL_SUBSCRIBE, &f), "CTL_SUBSCRIBE to varp 1700 sent");
    have = await_frame(srv, player, client, TORIRSSERVER_CTL_FACT_VARS, in, (int)sizeof(in));
    n = find_frame(in, have, TORIRSSERVER_CTL_FACT_VARS, 0, &payload);
    check(n > 0, "CTL_FACT_VARS with the current value");
    ToriRSServer_VarpsPut(&player->varps, 1700, 4242);
    have = await_frame(srv, player, client, TORIRSSERVER_CTL_FACT_VARS, in, (int)sizeof(in));
    n = find_frame(in, have, TORIRSSERVER_CTL_FACT_VARS, 0, &payload);
    if( n > 0 )
    {
        uint32_t name_len = get_u32(payload);
        const uint8_t* rows = payload + 4 + name_len;

        check(get_u32(rows) == 1 && get_u32(rows + 4) == 1700 && (int32_t)get_u32(rows + 8) == 4242,
              "a change is sent: varp 1700 = 4242");
    }
    else
        check(0, "a change is sent: varp 1700 = 4242");
    tick(srv, player);
    have = drain(client, in, (int)sizeof(in), 0);
    check(find_frame(in, have, TORIRSSERVER_CTL_FACT_VARS, 0, &payload) < 0,
          "and nothing while it stays put");

    check(send_frame(client, TORIRSSERVER_CTL_BYE, NULL), "CTL_BYE sent");
    for( int i = 0; i < 10 && ToriRSServer_ControlCount() > 0; i++ )
        tick(srv, player);
    check(ToriRSServer_ControlCount() == 0, "the link closes on BYE");
    close(client);

    /* Behind the gate: a bad ticket is refused, a minted one accepted. */
    for( int good = 0; good < 2; good++ )
    {
        char ticket[TORIRSSERVER_GATE_TICKET_MAX];

        client = server = -1;
        check(loopback_pair(&client, &server), good ? "gated: a second connection" : "gated: a connection");
        if( good )
            ToriRSServer_GateTicketMint("editor", (long long)time(NULL) + 3600, ticket);
        else
            snprintf(ticket, sizeof(ticket), "%s", "not-a-ticket");
        send(client, &selector, 1, 0);
        memset(&f, 0, sizeof(f));
        frame_u32(&f, TORIRSSERVER_CTL_VERSION);
        frame_str(&f, ticket);
        send_frame(client, TORIRSSERVER_CTL_HELLO, &f);
        check(open_through_select(server, 1), "gated: adopted by the select");
        have = await_frame(srv, player, client, TORIRSSERVER_CTL_FACT_HELLO, in, (int)sizeof(in));
        n = find_frame(in, have, TORIRSSERVER_CTL_FACT_HELLO, 0, &payload);
        check(n >= 8 && get_u32(payload + 4) == (uint32_t)good,
              good ? "gated: a minted ticket is accepted" : "gated: a bad ticket is refused");
        if( good )
            send_frame(client, TORIRSSERVER_CTL_BYE, NULL);
        for( int i = 0; i < 10 && ToriRSServer_ControlCount() > 0; i++ )
            tick(srv, player);
        check(ToriRSServer_ControlCount() == 0, good ? "gated: closed on BYE" : "gated: refused link closed");
        close(client);
    }
}

/* Does any line of the reload's answer contain `text`? */
static int
reload_said(
    const struct ToriRSServerReloadResult* result,
    const char* text)
{
    for( int i = 0; i < result->line_count; i++ )
        if( strstr(result->lines[i], text) )
            return 1;
    return 0;
}

/* Every npc slot with a def points at the def the content table holds NOW
 * for its type (or the engine default): the re-point pass ran. */
static int
npc_defs_current(const struct ToriRSServer* srv, int* out_with_def)
{
    *out_with_def = 0;
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        const struct ToriRSServerNpc* npc = &srv->npcs[i];
        const struct ToriRSServerNpcDef* want;

        if( !npc->def )
            continue;
        (*out_with_def)++;
        want = ToriRSServer_ContentNpc(npc->type);
        if( npc->def != (want ? want : ToriRSServer_ContentNpcDefault()) )
            return 0;
    }
    return 1;
}

/*
 * The server-pack config reload (docs/serverpack.md "The hot-reload contract"
 * section 5): loc, npc and obj records replaced at the tick boundary from the
 * pack the server booted on (the same bytes, so every answer must be what it
 * was), every npc's def re-pointed into the new table, a name-table group
 * answered restart-needed; then a pack whose loc records are missing is
 * refused and the running tables are kept.
 */
static void
test_config_reload(
    struct ToriRSServer* srv,
    const char* content_dir,
    const char* work_dir)
{
    struct ToriRSServerReloadRequest* request = calloc(1, sizeof(*request));
    struct ToriRSServerReloadResult* result = calloc(1, sizeof(*result));
    char pack_dir[1200];
    char broken[1200];
    char op_before[64];
    char name_before[64];
    const char* op;
    int locs_before;
    int npcs_before;
    int with_def = 0;
    int stairs;
    int replaced;
    int refused = -1;

    check(request && result, "config reload: request and result allocated");
    stairs = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, "spiralstairsbottom_3");
    check(stairs >= 0, "config reload: the loc `spiralstairsbottom_3` resolves");
    op = stairs >= 0 ? ToriRSServer_SceneLocOp(stairs, 1) : NULL;
    snprintf(op_before, sizeof(op_before), "%s", op ? op : "");
    check(op_before[0] != '\0', "config reload: the stairs offer op1 before the reload");
    snprintf(name_before, sizeof(name_before), "%s",
             ToriRSServer_ObjInfo(995)->name ? ToriRSServer_ObjInfo(995)->name : "");
    locs_before = ToriRSServer_LocInfoCount();
    npcs_before = ToriRSServer_NpcInfoCount();
    check(npc_defs_current(srv, &with_def) && with_def > 0,
          "config reload: npcs are standing, each on its current def");

    request->group_count = 4;
    request->groups[0].index = TORIRSSERVER_RELOAD_SERVPACK_INDEX;
    request->groups[0].archive = 64 + RSCACHE_DAT2_CONFIG_KIND_LOCS;
    request->groups[1].index = TORIRSSERVER_RELOAD_SERVPACK_INDEX;
    request->groups[1].archive = 64 + RSCACHE_DAT2_CONFIG_KIND_NPC;
    request->groups[2].index = TORIRSSERVER_RELOAD_SERVPACK_INDEX;
    request->groups[2].archive = RSCACHE_DAT2_CONFIG_KIND_OBJECT;
    request->groups[3].index = TORIRSSERVER_RELOAD_SERVPACK_INDEX;
    request->groups[3].archive = 128;
    ToriRSServer_Reload(srv, request, result);
    check(result->status == TORIRSSERVER_RELOAD_RESTART_NEEDED,
          "config reload: answers RESTART_NEEDED (the name table)");
    check(result->groups_replaced == 3, "config reload: the loc, npc and obj groups are replaced");
    check(reload_said(result, "config: loc 1 of 1 group(s) replaced"), "config reload: says loc replaced");
    check(reload_said(result, "player(s) owed a REBUILD") && srv->players[0].rebuild_pending,
          "config reload: the player is owed a REBUILD (its scenery re-reads the loc records)");
    check(reload_said(result, "config: npc 1 of 1 group(s) replaced"), "config reload: says npc replaced");
    check(reload_said(result, "config: obj 1 of 1 group(s) replaced"), "config reload: says obj replaced");
    check(result->restart_count == 1 && strstr(result->restart[0], "name table"),
          "config reload: the name table is the one restart row");
    check(ToriRSServer_LocInfoCount() == locs_before, "config reload: the same loc record count");
    check(ToriRSServer_NpcInfoCount() == npcs_before, "config reload: the same npc record count");
    op = stairs >= 0 ? ToriRSServer_SceneLocOp(stairs, 1) : NULL;
    check(op && strcmp(op, op_before) == 0, "config reload: the stairs' op1 reads the same off the new table");
    check(ToriRSServer_ObjInfo(995)->name && strcmp(ToriRSServer_ObjInfo(995)->name, name_before) == 0,
          "config reload: obj 995's name reads the same off the new table");
    check(npc_defs_current(srv, &with_def) && with_def > 0,
          "config reload: every npc's def was re-pointed into the new table");

    /* A pack that opens but holds no loc records: refused whole, nothing
     * swapped. Symlinks to the booted pack's files, all but idx70. */
    ToriRSServer_ServPackDir(content_dir, ToriRSServer_WorldCacheDir(), pack_dir, sizeof(pack_dir));
    snprintf(broken, sizeof(broken), "%s/broken_servpack", work_dir);
    {
        char cmd[3000];
        snprintf(cmd, sizeof(cmd),
                 "rm -rf '%s' && mkdir -p '%s' && for f in '%s'/main_file_cache.*; do "
                 "case \"$f\" in *.idx70) ;; *) ln -s \"$f\" '%s'/ ;; esac; done",
                 broken, broken, pack_dir, broken);
        check(system(cmd) == 0, "config reload: a pack without loc records is staged");
    }
    memset(result, 0, sizeof(*result));
    {
        int groups[2] = { 64 + RSCACHE_DAT2_CONFIG_KIND_LOCS, 0 };

        replaced = ToriRSServer_ServPackReloadGroups(broken, groups, 1, ToriRSServer_ReloadConfigKind,
                                                     srv, result, &refused);
    }
    check(replaced == 0 && refused == 1, "config reload: a pack without loc records is refused");
    check(reload_said(result, "the running table is kept"), "config reload: and says the table is kept");
    check(ToriRSServer_LocInfoCount() == locs_before, "config reload: the loc table was not touched");
    op = stairs >= 0 ? ToriRSServer_SceneLocOp(stairs, 1) : NULL;
    check(op && strcmp(op, op_before) == 0, "config reload: the stairs still offer op1");
    free(request);
    free(result);
}

int
main(
    int argc,
    char** argv)
{
    struct ToriRSServerBootConfig config;
    static char pack_a[1200];
    static char pack_b[1200];
    char saves[1200];
    struct ToriRSServer* srv;
    struct ToriRSServerPlayer* player;

    if( argc != 5 )
    {
        fprintf(stderr, "usage: %s <content_dir> <cache_dir> <sscompile> <work_dir>\n", argv[0]);
        return 2;
    }
    mkdir(argv[4], 0755);
    snprintf(saves, sizeof(saves), "%s/saves", argv[4]);
    mkdir(saves, 0755);
    setenv("TORIRSSERVER_SAVES", saves, 1);

    /* sscompile runs through the host's spawner (builder_ssc.c, since the
     * workbench's BUILD jobs); without one Builder_SscRun answers NOT_RUN. */
    Builder_SscSetSpawner(LaunchSessions_SystemSpawner());
    if( !build_packs(argv[1], argv[3], argv[4], pack_a, pack_b, sizeof(pack_a)) )
    {
        check(0, "packs A and B compile from the staged tree");
        printf("live-reload: %d checks, %d failed\n", g_checks, g_failures);
        return 1;
    }
    check(1, "packs A and B compile from the staged tree");

    ToriRSServer_BootDefaults(&config);
    config.content_dir = argv[1];
    config.cache_dir = argv[2];
    config.script_dir = pack_a;
    check(ToriRSServer_BootLoad(&config) == 0, "the server boots on the content and cache");
    ToriRSServer_GateKeyLoad();

    srv = ToriMem_Calloc(ToriMem_ServerWorld(), 1, sizeof(*srv));
    srv->wire = ToriRSServer_WireByName("osrs239");
    check(srv->wire != NULL, "the osrs239 wire (the selftest's, whose MESSAGE_GAME the wire reader decodes)");
    ToriRSServer_ShopSeed(srv);
    player = ToriRSServer_WorldAddPlayer(srv, NULL);
    ToriRSServer_WorldInit(srv, ToriRSServer_BootZone(config.home_x),
                           ToriRSServer_BootZone(config.home_z));
    ToriRSServer_WorldPlayerInit(player);
    ToriRSServer_WorldSetDisplayName(player, "livereload");
    check(ToriRSServer_ScriptsLoad(srv, pack_a) > 0, "pack A loads");

    test_pausebutton_across_reload(srv, player, pack_a, pack_b);
    test_square_reload(srv, player);
    test_spawn_reload(srv, player, argv[4]);
    test_config_reload(srv, argv[1], argv[4]);
    test_control_link(srv, player);

    ToriRSServer_ControlCloseAll();
    printf("live-reload: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
