/*
 * The Lanes panel (module "publish", File > Publish..., Ctrl+Shift+U): where
 * this window's edits land and where they go next (owner, 2026-11-02:
 * "Edits should go to a dedicated 'working' lane and then there should be a
 * publish menu that publishes to a target lane or a new lane"), widened by
 * workbench 6.5 (design #lanes) from the Publish panel into the Lanes panel,
 * and on 2026-11-11 into the whole life of a lane: "do this in a scratch
 * lane, then promote to a new lane".
 *
 * The editor server serves a LANE (torirsmaped/torirs_maped_lanes.h): the
 * main tree itself, or a named lane -- build/lanes/<name>/content, a
 * copy-on-write mirror of the tree that RUN builds and plays on its own. The
 * panel is two pages behind a tab strip, under one line that names the lane
 * ("Lane: working | 3 changed"):
 *
 *   Changes   what THIS lane changed, and where it goes.
 *     the list         every changed file, "M maps/m50_50.jl2 (85.6 KB)":
 *                      as many rows as the panel's height holds, filtered by
 *                      Find change and the three Show boxes (modified, added,
 *                      deleted);
 *     the picked row   Open in editor and Show in Explorer open the LANE's
 *                      file; Diff against base starts the user's diff tool on
 *                      it and a COPY of the base's (editor_diff_cmd.h); Discard
 *                      this change... puts that one file back; Copy list puts
 *                      the listed paths on the clipboard;
 *     Promote to new   (File > Promote to new lane...) a new lane named in
 *     lane...          the box, mirrored from main, holding this lane's
 *                      changes. This lane keeps them. The answer says what
 *                      exists where and how to open the new lane;
 *     Publish into     the changes written into a lane picked in the
 *     lane...          dropdown, or into the main tree. The dropdown opens on
 *                      no target at all -- it used to open on main, one press
 *                      from the owner's tree -- and every publish asks twice:
 *                      the first press asks the server what would be written
 *                      and which of those files the target changed too
 *                      (LANE_PUBLISH's dry run), the second within 8 s goes;
 *     Discard all...   every change back to the base (asks twice).
 *
 *   Lanes     every lane, and the ones this session can go to.
 *     the list         each lane with its change count and, for a tree
 *                      another editor holds, who holds it;
 *     Switch at        the picked lane opens from the next launch on; a
 *     relaunch         double-click on a row (or Enter twice within
 *                      half a second) is the same;
 *     Copy manifest    "lane=<picked>", the line that opens a lane from a
 *     line             manifest's [editor:boot];
 *     Delete lane...   the picked lane, gone (asks twice): its links to the
 *                      base dropped by name, its own files removed, the base
 *                      untouched (ToriRSMapEdLanes_Delete). Refused for main,
 *                      this session's lane, a lane an editor holds and the
 *                      lane the boot lane file names;
 *     Lane from main   a lane mirrored from the main tree, named in the box
 *                      above it; nothing published, not switched to;
 *     Scratch lane     (Ctrl+Shift+N) a lane named scratch-<yymmdd><letter>,
 *                      then the switch to it (staged, below);
 *     Refresh          scan the lanes and the changes again (a save does it).
 *
 * A SWITCH IS STAGED. A running editor server cannot re-point its lane: its
 * tree lock, its documents and their history, the catalog, the live dir and
 * the servers it launched are all the lane's, and every window mirrors them.
 * So Switch (and Scratch lane's switch) writes the lane EVERY LAUNCH opens
 * from then on (LANE_SWITCH: the boot lane file, build/lanes/lane.ini, read
 * by the daemon's main and the embedded server, over the manifest's `lane=`),
 * and the panel's boot line, the status bar and the lane chip say so in the
 * user's terms: "Next launch opens: working. Quit and launch the editor again
 * to switch; this session stays on scratch-261108a". The word "boot" stays
 * out of the panel: a user launches the editor.
 *
 * A session told to IGNORE the boot lane file ($TORIRSMAPED_IGNORE_BOOT_LANE:
 * a test's, an agent's) neither reads nor writes it. The server says so
 * (FACT_LANE_INFO's features), and here Switch is off with the reason on the
 * boot line, Scratch lane makes its lane and says how to open it, and no
 * "next:" lane is shown: that session's next launch is its manifest's lane
 * again, whatever the file says.
 *
 * The module also keeps ui->lane, for every window and every module: the
 * lane's name and its change count, which the status bar's lane chip shows
 * (Editor_Lanes_ChipText). It asks LANE_INFO once, and the count alone
 * (LANE_CHANGES with TORIRSMAPED_LANE_CHANGES_COUNT_ONLY, FACT_LANE_COUNT)
 * every EDITOR_UI_LANE_COUNT_MS, after a save and after a publish, whether
 * the panel shows or not; the full lists only while it shows. The server
 * answers the count and the LIST's counts from its cache of each lane's
 * last walk (torirs_maped_lanes_wire.c), walking again only after a request
 * that can write a tree, so the periodic ask costs no walk of the mirror on
 * the embedded server's (the UI's) thread; Refresh walks every lane again
 * (TORIRSMAPED_LANE_LIST_RESCAN). A server without TORIRSMAPED_CAP_LANES_V2
 * hears no frame it does not know, and one whose FACT_LANE_INFO does not say
 * TORIRSMAPED_LANE_INFO_LIFECYCLE is not sent Delete, Diff, the dry run or a
 * one-file discard: those controls are off.
 *
 * A server on the main tree has nothing to publish: the panel says so, and
 * the boot line says how to get onto a lane. The requests go through the
 * host's lanes frames (Editor_HostMapEdLanesSend / Take, editor_host.h);
 * every answer to an action is a broadcast, so every window's panel shows
 * the same lane.
 */
#include "edpanel_common.h"
#include "edpanel_picker.h"
#include "edpanels.h"
#include "editor/editor.h"
#include "editor/editor_diff_cmd.h"
#include "editor/editor_host.h"
#include "mem/torirs_mem.h"
#include "platform/platform_clipboard.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_buf.h"
#include "torirsmaped/torirs_maped_lanes.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
    /** Each list fills what its page leaves of the panel's height: the
     *  fewest rows it shows, and the most (what it reserves of the chrome's
     *  row pool). A quest is dozens of files; the lists showed six and five
     *  rows in a panel 800 px tall. */
    PUBLISH_CHANGE_ROWS_MIN = 6,
    PUBLISH_CHANGE_ROWS_MAX = 28,
    PUBLISH_LANE_ROWS_MIN = 5,
    PUBLISH_LANE_ROWS_MAX = 20,
    PUBLISH_CHANGE_MAX = TORIRSMAPED_LANE_CHANGE_ROWS_MAX,
    PUBLISH_LANES_MAX = TORIRSMAPED_LANES_MAX,
    /** The dropdown's first option, "Pick a lane...": no target. */
    PUBLISH_TARGET_NONE = -1,
    /** Every two-step command: the second press within this confirms. */
    PUBLISH_CONFIRM_MS = 8000,
    /** Two activations of one list row this close are a double-click. */
    PUBLISH_DOUBLE_MS = 500,
    /** The Show boxes, in kind order. */
    PUBLISH_KIND_MODIFIED = 0,
    PUBLISH_KIND_ADDED = 1,
    PUBLISH_KIND_DELETED = 2,
    PUBLISH_KINDS = 3,
    PUBLISH_TAB_CHANGES = 0,
    PUBLISH_TAB_LANES = 1,
    /** Squares a discard is about to remove (the lane added them). */
    PUBLISH_REMOVED_MAX = 64,
    /** What a LABEL's text holds; a longer sentence is elided here (the
     *  whole of it is in the Console) rather than cut by the chrome, which
     *  logs every cut. */
    PUBLISH_LABEL_MAX = TORIRS_CHROME_INPUT_MAX - 1,
};

/** The command a first press armed; its second press runs it. */
enum publish_arm
{
    PUBLISH_ARM_NONE = 0,
    PUBLISH_ARM_DISCARD_ALL,
    PUBLISH_ARM_DISCARD_ONE,
    PUBLISH_ARM_DELETE,
    PUBLISH_ARM_PUBLISH,
};

struct publish_change
{
    uint32_t kind;
    char path[256];
    uint32_t size;
};

struct publish_lane
{
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];
    int is_main;
    int is_this;
    /** Its change count (-1: not counted yet, or an older server) and who
     *  holds its tree ("" nobody). */
    int changes;
    char held[256];
};

struct publish_square
{
    int x;
    int z;
};

struct PublishPanel
{
    int panel;
    int label_lane;
    int tabs;

    /* the Changes page */
    int label_counts;
    int input_find_change;
    int check_show[PUBLISH_KINDS];
    int list_changes;
    int label_picked;
    int button_open;
    int button_reveal;
    int button_diff;
    int button_discard_one;
    int button_copy;
    int input_new;
    int button_promote;
    int drop_target;
    int check_keep;
    int button_publish;
    int button_discard;

    /* the Lanes page */
    /** Which lane every launch opens, and how to reach a switched one. */
    int label_boot;
    int input_find;
    int list_lanes;
    int label_lane_picked;
    int button_switch;
    int button_copy_line;
    int button_delete;
    int input_lane_name;
    int button_create;
    int button_scratch;
    int button_refresh;

    /* under both pages: what the last command said, how to go on, a note */
    int label_status;
    int label_how;
    int label_note;

    /* the lane, as the server last said (ui->lane holds the shared half) */
    char lane_content[1024];
    char lane_base[1024];
    int info_known;
    /** FACT_LANE_INFO's features (TORIRSMAPED_LANE_INFO_*); 0 from a server
     *  that predates them. */
    uint32_t features;
    char boot_file[1024];

    struct publish_lane lanes[PUBLISH_LANES_MAX];
    int lane_count;
    int list_known;
    /** The lane list's rows: indices into `lanes` that match Find. */
    int lane_rows[PUBLISH_LANES_MAX];
    int lane_row_count;
    char shown_find[TORIRS_CHROME_INPUT_MAX];
    /** The dropdown's options: "Pick a lane...", the other lanes' names,
     *  then the main tree. */
    const char* options[PUBLISH_LANES_MAX + 2];
    int option_lane[PUBLISH_LANES_MAX + 2];
    int option_count;
    /** The publish target, by NAME ("" none). The dropdown's selection is
     *  an index into a list that changes whenever any lane is made or
     *  deleted: held as an index, a publish to "New lane..." left it naming
     *  the lane just made, and a lane made anywhere re-pointed it. */
    char target_name[TORIRSMAPED_LANE_NAME_MAX + 8];

    struct publish_change* changes;
    int change_count;
    int modified;
    int added;
    int deleted;
    int changes_known;
    /** The Show boxes' filter: a kind's changes are listed while its box
     *  is ticked. */
    int show_kind[PUBLISH_KINDS];
    /** The change list's rows: indices into `changes`. */
    int* change_rows;
    int change_row_count;
    char shown_find_change[TORIRS_CHROME_INPUT_MAX];
    /** The change a first activation picked ("" none) and when: a second
     *  activation of the same row within PUBLISH_DOUBLE_MS opens its file.
     *  Not a later one: clicking the picked row again a minute on opened the
     *  external editor unasked (2026-11-11). */
    char picked_change[256];
    int64_t picked_change_ms;
    /** What each list's selection names, kept so it follows its lane or its
     *  file when the rows move under it (a count landing, a filter, a save):
     *  a selection is a row index, and an index into a list that changed
     *  names something else. */
    char sel_lane[TORIRSMAPED_LANE_NAME_MAX + 8];
    char sel_change[256];

    /** Asked the server, nothing heard yet (a request per kind). */
    int info_pending;
    int list_pending;
    int changes_pending;
    int count_pending;
    int action_pending;
    /** The panel refreshes when it opens and after a save (the document's
     *  unsaved state falling back to clean). */
    int refresh_wanted;
    int seen_unsaved;
    /** The panel's visibility last tick: opening it (the hotkey, the View
     *  menu) asks the server afresh. */
    int was_visible;
    /** The chrome clock of the last count ask (the chip's period). */
    int64_t count_asked_ms;
    int count_ever_asked;

    /** Scratch lane: waiting for the lane list to pick a free letter, then
     *  for LANE_CREATE's answer to switch to `scratch_name`. */
    int scratch_wanted;
    char scratch_name[TORIRSMAPED_LANE_NAME_MAX + 8];
    /** Our scratch lane is made and its switch sent: the switch's answer is
     *  worded as the scratch lane's. */
    int scratch_switching;
    /** Promote: our LANE_PUBLISH to a new lane is in flight, and its name. */
    int promoting;
    char promote_name[TORIRSMAPED_LANE_NAME_MAX + 8];
    /** Publish into lane...: the dry run is in flight. */
    int check_pending;
    /** The lane list's picked row (-1 none) and when it was last activated:
     *  a second activation of the same row within PUBLISH_DOUBLE_MS (a
     *  double-click, Enter twice) is its Switch. Not any later one: a Switch
     *  rewrites the boot lane file, and a second plain click on a row the
     *  user had picked a minute before re-pointed every launch (2026-11-11). */
    int picked_row;
    char picked_name[TORIRSMAPED_LANE_NAME_MAX + 8];
    int64_t picked_ms;
    /** The armed two-step command, what it is about (a path, a lane), and
     *  when it was armed on the chrome clock. */
    enum publish_arm armed;
    char armed_subject[256];
    int64_t armed_ms;
    /** The squares the discard in flight removes: they are not reopened. */
    struct publish_square removed[PUBLISH_REMOVED_MAX];
    int removed_count;
    /** The hotkey table's serial the keycaps were worn for. */
    uint32_t chord_serial;
};

/** ToriRSMapEdLanes_NameValid's rule, spelled here: a window without the
 *  embedded server (the web) links no lane module. */
static int
publish_name_valid(const char* name)
{
    size_t const length = strlen(name);
    if( length == 0 || length > TORIRSMAPED_LANE_NAME_MAX || strcmp(name, "main") == 0 )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') )
            return 0;
    }
    return 1;
}

int
Editor_LanesScratchName(
    const char* yymmdd,
    const char* const* taken,
    int count,
    char* out,
    int out_size)
{
    assert(yymmdd);
    assert(taken || count == 0);
    assert(out);
    assert(out_size > 0);
    assert(strlen(yymmdd) == 6);
    for( char letter = 'a'; letter <= 'z'; letter++ )
    {
        int used = 0;
        snprintf(out, (size_t)out_size, "scratch-%s%c", yymmdd, letter);
        for( int i = 0; i < count && !used; i++ )
            used = strcmp(taken[i], out) == 0;
        if( !used )
            return 1;
    }
    out[0] = '\0';
    return 0;
}

void
Editor_LanesSizeText(
    uint32_t bytes,
    char* out,
    int out_size)
{
    assert(out);
    assert(out_size > 0);
    if( bytes < 1024u )
        snprintf(out, (size_t)out_size, "%u bytes", bytes);
    else if( bytes < 1024u * 1024u )
        snprintf(out, (size_t)out_size, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(out, (size_t)out_size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
}

int
Editor_LanesPathSquare(
    const char* path,
    int* out_x,
    int* out_z)
{
    const char* file;
    int consumed = 0;
    int x = 0;
    int z = 0;

    assert(path);
    assert(out_x);
    assert(out_z);
    file = strrchr(path, '/');
    file = file ? file + 1 : path;
    if( sscanf(file, "m%d_%d.jm2%n", &x, &z, &consumed) != 2 || consumed == 0 || file[consumed] != '\0' )
        return 0;
    *out_x = x;
    *out_z = z;
    return 1;
}

int
Editor_LanesPathSquareFile(
    const char* path,
    int* out_x,
    int* out_z)
{
    static const char* const kinds[] = { "jm2", "jl2", "filepack" };
    const char* file;
    const char* dot;
    int consumed = 0;
    int x = 0;
    int z = 0;

    assert(path);
    assert(out_x);
    assert(out_z);
    file = strrchr(path, '/');
    file = file ? file + 1 : path;
    dot = strchr(file, '.');
    if( !dot || sscanf(file, "m%d_%d%n", &x, &z, &consumed) != 2 || file + consumed != dot )
        return 0;
    for( size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++ )
    {
        if( strcmp(dot + 1, kinds[i]) == 0 )
        {
            *out_x = x;
            *out_z = z;
            return 1;
        }
    }
    return 0;
}

int
Editor_LanesNameSuggest(
    const char* typed,
    char* out,
    int out_size)
{
    int const limit = out_size - 1 < TORIRSMAPED_LANE_NAME_MAX ? out_size - 1 : TORIRSMAPED_LANE_NAME_MAX;
    int at = 0;
    int dash = 0;

    assert(typed);
    assert(out);
    assert(out_size > 0);
    for( const char* c = typed; *c; c++ )
    {
        char const ch = (*c >= 'A' && *c <= 'Z') ? (char)(*c - 'A' + 'a') : *c;
        if( !((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') )
        {
            dash = 1; /* a space or a stray character: one '-' between words */
            continue;
        }
        if( at + (dash && at > 0) + 1 > limit )
            break;
        if( dash && at > 0 )
            out[at++] = '-';
        dash = 0;
        out[at++] = ch;
    }
    out[at] = '\0';
    return at > 0 && strcmp(out, "main") != 0;
}

static struct PublishPanel*
publish_self(struct EditorUi* ui)
{
    return Editor_UiPanelSelf(ui, "publish");
}

static int
publish_v2(const struct EditorUi* ui)
{
    return ui->editor && (ui->editor->capabilities & TORIRSMAPED_CAP_LANES_V2) != 0;
}

/** The server answers Delete, Diff, the dry run and a one-file discard. */
static int
publish_lifecycle(const struct PublishPanel* self)
{
    return (self->features & TORIRSMAPED_LANE_INFO_LIFECYCLE) != 0;
}

/** This session ignores the boot lane file: a Switch would not be recorded. */
static int
publish_boot_ignored(const struct PublishPanel* self)
{
    return (self->features & TORIRSMAPED_LANE_INFO_BOOT_IGNORED) != 0;
}

static int
publish_send(
    struct EditorUi* ui,
    uint32_t type,
    struct ToriRSMapEdBuf* payload)
{
    int sent;
    if( !ui->editor )
    {
        ToriRSMapEd_BufFree(payload);
        return 0;
    }
    sent = Editor_HostMapEdLanesSend(&ui->editor->host, type,
        payload->data ? payload->data + payload->head : NULL, (uint32_t)ToriRSMapEd_BufAvailable(payload));
    ToriRSMapEd_BufFree(payload);
    return sent;
}

/** A label's text, elided to what a LABEL holds, and the row hidden while
 *  it has nothing to say. */
static void
publish_label(
    struct EditorUi* ui,
    int widget,
    const char* text)
{
    char fit[PUBLISH_LABEL_MAX + 1];

    if( strlen(text) > (size_t)PUBLISH_LABEL_MAX )
    {
        snprintf(fit, sizeof(fit), "%.*s...", PUBLISH_LABEL_MAX - 3, text);
        text = fit;
    }
    ToriRSChrome_SetText(ui->chrome, widget, text);
    ToriRSChrome_SetHidden(ui->chrome, widget, text[0] == '\0');
}

/** What the last command said (and, under it, how to go on and a note: ""
 *  hides either row). */
static void
publish_say(
    struct EditorUi* ui,
    struct PublishPanel* self,
    const char* status,
    const char* how,
    const char* note)
{
    publish_label(ui, self->label_status, status);
    publish_label(ui, self->label_how, how);
    publish_label(ui, self->label_note, note);
}

/* ---- the two-step commands -------------------------------------------------------- */

static int
publish_arm_button(
    const struct PublishPanel* self,
    enum publish_arm arm)
{
    return arm == PUBLISH_ARM_DISCARD_ALL   ? self->button_discard
           : arm == PUBLISH_ARM_DISCARD_ONE ? self->button_discard_one
           : arm == PUBLISH_ARM_DELETE      ? self->button_delete
                                            : self->button_publish;
}

static const char*
publish_arm_label(
    enum publish_arm arm,
    int armed)
{
    if( arm == PUBLISH_ARM_DISCARD_ALL )
        return armed ? "Discard all: sure?" : "Discard all...";
    if( arm == PUBLISH_ARM_DISCARD_ONE )
        return armed ? "Discard this change: sure?" : "Discard this change...";
    if( arm == PUBLISH_ARM_DELETE )
        return armed ? "Delete lane: sure?" : "Delete lane...";
    return armed ? "Publish: sure?" : "Publish into lane...";
}

static void
publish_disarm(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    if( self->armed == PUBLISH_ARM_NONE )
        return;
    /* Every confirm's prompt ("Press ...: sure? within 8 s") is the row
     * under the status (publish_prompt; a publish's sits under the dry run's
     * answer): it goes when the confirm does. */
    publish_label(ui, self->label_how, "");
    /* A publish's dry run ("7 file(s) would be written into the MAIN tree")
     * stayed on the row above after the confirm lapsed or the target
     * changed, reading as a publish still pending. The confirm's own press
     * says "publishing..." straight after this. */
    if( self->armed == PUBLISH_ARM_PUBLISH )
        publish_label(ui, self->label_status, "Not published: the confirm lapsed; nothing was written.");
    ToriRSChrome_SetText(ui->chrome, publish_arm_button(self, self->armed), publish_arm_label(self->armed, 0));
    self->armed = PUBLISH_ARM_NONE;
    self->armed_subject[0] = '\0';
}

/** Arm `arm` for `subject` (a path, a lane's name; "" for the lot): its
 *  button says "sure?" until it is pressed again, another command is armed,
 *  the subject changes, or PUBLISH_CONFIRM_MS pass.
 *
 *  A BUTTON's caption is its TEXT. Discard... armed itself with SetLabel
 *  until 2026-11-11, which the agent's `ui` reported and the screen never
 *  drew: the button went on reading "Discard..." through its own confirm. */
static void
publish_arm(
    struct EditorUi* ui,
    struct PublishPanel* self,
    enum publish_arm arm,
    const char* subject)
{
    publish_disarm(ui, self);
    self->armed = arm;
    self->armed_ms = ui->chrome->now_ms;
    snprintf(self->armed_subject, sizeof(self->armed_subject), "%s", subject);
    ToriRSChrome_SetText(ui->chrome, publish_arm_button(self, arm), publish_arm_label(arm, 1));
}

/** An armed Discard / Delete's question, in the panel and on the status bar.
 *  The panel's status row is emptied: it held the LAST command's answer
 *  ("published 1 file(s) to x"), which read as this command's under a
 *  button saying "sure?". */
static void
publish_prompt(
    struct EditorUi* ui,
    struct PublishPanel* self,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

static void
publish_prompt(
    struct EditorUi* ui,
    struct PublishPanel* self,
    const char* format,
    ...)
{
    char text[PUBLISH_LABEL_MAX + 1];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    publish_say(ui, self, "", text, "");
    Editor_UiStatus(ui, "%s", text);
}

static int
publish_is_armed(
    const struct PublishPanel* self,
    enum publish_arm arm,
    const char* subject)
{
    return self->armed == arm && strcmp(self->armed_subject, subject) == 0;
}

/* ---- asking the server ------------------------------------------------------------ */

/** The count alone, for the chip (a server without the frame: nothing). */
static void
publish_ask_count(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;

    /* An ask the server never answered (its walk was dropped by a Switch or
     * a Discard) must not hold the chip forever: after four periods, ask again. */
    if( self->count_pending && ui->chrome->now_ms - self->count_asked_ms >= 4 * EDITOR_UI_LANE_COUNT_MS )
        self->count_pending = 0;
    self->count_asked_ms = ui->chrome->now_ms;
    self->count_ever_asked = 1;
    if( !publish_v2(ui) || self->count_pending )
        return;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, TORIRSMAPED_LANE_CHANGES_COUNT_ONLY);
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_CHANGES, &payload) )
        self->count_pending = 1;
}

static void
publish_ask_info(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_INFO, &payload) )
        self->info_pending = 1;
}

/** `rescan` (Refresh): every lane's mirror walked again rather than the
 *  server's cached counts (TORIRSMAPED_LANE_LIST_RESCAN). */
static void
publish_ask_list(
    struct EditorUi* ui,
    struct PublishPanel* self,
    int rescan)
{
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    if( rescan )
        ToriRSMapEd_PutU32(&payload, TORIRSMAPED_LANE_LIST_RESCAN);
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_LIST, &payload) )
        self->list_pending = 1;
}

static void
publish_ask(
    struct EditorUi* ui,
    struct PublishPanel* self,
    int rescan)
{
    struct ToriRSMapEdBuf payload;

    publish_ask_info(ui, self);
    /* The rows first: their walk of this lane is what the server's count
     * cache then answers the LIST's row for this lane (and the chip) from. */
    memset(&payload, 0, sizeof(payload));
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_CHANGES, &payload) )
        self->changes_pending = 1;
    publish_ask_list(ui, self, rescan);
    self->refresh_wanted = 0;
}

static const char*
publish_kind_letter(uint32_t kind)
{
    return kind == TORIRSMAPED_LANE_CHANGE_MODIFIED ? "M" : kind == TORIRSMAPED_LANE_CHANGE_ADDED ? "A" : "D";
}

static const char*
publish_kind_word(uint32_t kind)
{
    return kind == TORIRSMAPED_LANE_CHANGE_MODIFIED ? "modified"
           : kind == TORIRSMAPED_LANE_CHANGE_ADDED  ? "added"
                                                    : "deleted";
}

static int
publish_contains(
    const char* text,
    const char* filter)
{
    size_t const n = strlen(filter);

    if( n == 0 )
        return 1;
    for( const char* at = text; *at; at++ )
    {
        size_t i = 0;
        while( i < n && at[i] )
        {
            char a = at[i];
            char b = filter[i];
            if( a >= 'A' && a <= 'Z' )
                a = (char)(a - 'A' + 'a');
            if( b >= 'A' && b <= 'Z' )
                b = (char)(b - 'A' + 'a');
            if( a != b )
                break;
            i++;
        }
        if( i == n )
            return 1;
    }
    return 0;
}

/** The shared half: ui->lane changes only when a field does. */
static void
publish_lane_note(
    struct EditorUi* ui,
    const char* name,
    int is_main,
    int writable,
    int changes,
    const char* next_boot,
    const char* asked)
{
    struct EditorUiLane* lane = &ui->lane;
    struct EditorUiLane before = *lane;

    if( name )
    {
        snprintf(lane->name, sizeof(lane->name), "%s", name);
        lane->is_main = is_main;
        lane->known = 1;
    }
    if( writable >= 0 )
        lane->writable = writable;
    if( changes >= 0 )
    {
        lane->changes = changes;
        lane->changes_known = 1;
    }
    if( next_boot )
    {
        /* lane.ini naming the lane it serves now is no switch waiting: it is
         * this lane pinned for every launch. */
        lane->pinned = next_boot[0] && strcmp(next_boot, lane->name) == 0;
        snprintf(lane->next_boot, sizeof(lane->next_boot), "%s", lane->pinned ? "" : next_boot);
    }
    if( asked )
        snprintf(lane->asked, sizeof(lane->asked), "%s", asked);
    if( memcmp(&before, lane, sizeof(before)) != 0 )
        lane->serial++;
}

void
Editor_Lanes_ChipText(
    const struct EditorUi* ui,
    char* out,
    int out_size)
{
    const struct EditorUiLane* lane;
    int at;

    assert(ui);
    assert(out);
    assert(out_size > 0);
    lane = &ui->lane;
    out[0] = '\0';
    if( !lane->known )
        return;
    if( lane->is_main )
        at = snprintf(out, (size_t)out_size, "%s", lane->name);
    else if( !lane->changes_known )
        at = snprintf(out, (size_t)out_size, "%s", lane->name);
    else if( lane->changes == 0 )
        at = snprintf(out, (size_t)out_size, "%s | clean", lane->name);
    else
        at = snprintf(out, (size_t)out_size, "%s | %d changed", lane->name, lane->changes);
    /* Only a switch that waits: a session that ignores the boot lane file is
     * told no next lane (the server's FACT_LANE_COUNT), so it shows none. */
    if( lane->next_boot[0] && at > 0 && at < out_size )
        snprintf(out + at, (size_t)(out_size - at), " | next: %s", lane->next_boot);
}

/* ---- the lists ------------------------------------------------------------------ */

static void
publish_lane_row_text(
    const struct EditorUi* ui,
    const struct publish_lane* lane,
    char* out,
    size_t out_size)
{
    char count[48];
    /* The lane a Switch staged: it opens at the next launch. */
    int const next = !lane->is_this && ui->lane.next_boot[0] && strcmp(lane->name, ui->lane.next_boot) == 0;

    if( lane->is_main )
        snprintf(count, sizeof(count), "the tree");
    else if( lane->changes < 0 )
        snprintf(count, sizeof(count), "counting...");
    else if( lane->changes == 0 )
        snprintf(count, sizeof(count), "clean");
    else
        snprintf(count, sizeof(count), "%d changed", lane->changes);
    /* This lane's tree is held by this very server: no news. Another
     * editor's is "open in an editor": who (a pid and a whole command line,
     * 200 bytes of it) is the picked lane's line under the list. */
    int const locked = lane->held[0] && !lane->is_this;
    snprintf(out, out_size, "%s%s | %s%s%s", lane->name, lane->is_this ? " (this)" : "", count,
        next ? " | next launch" : "", locked ? " | open in an editor" : "");
}

/** The lane the Lanes list's selection names, NULL when none. */
static const struct publish_lane*
publish_picked_lane(
    const struct EditorUi* ui,
    const struct PublishPanel* self)
{
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_lanes);

    return row >= 0 && row < self->lane_row_count ? &self->lanes[self->lane_rows[row]] : NULL;
}

/** 1 when `lane` is the one the boot lane file names for the next launch
 *  (a Switch staged it): Delete lane is refused for it. A session that
 *  ignores that file has no next lane. */
static int
publish_lane_is_next_boot(
    const struct EditorUi* ui,
    const struct PublishPanel* self,
    const struct publish_lane* lane)
{
    return !publish_boot_ignored(self) && ui->lane.next_boot[0] && strcmp(ui->lane.next_boot, lane->name) == 0;
}

/** The change the Changes list's selection names, NULL when none. */
static const struct publish_change*
publish_picked_change(
    const struct EditorUi* ui,
    const struct PublishPanel* self)
{
    int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_changes);

    return row >= 0 && row < self->change_row_count ? &self->changes[self->change_rows[row]] : NULL;
}

/** Note what the lists' selections name NOW, however they moved (a click,
 *  the arrow keys, the agent). Called only while the rows are the ones the
 *  selections were made on: at the top of a tick, ahead of the server's
 *  facts, and when a list is activated. 1 when either moved; a two-step
 *  command armed for the old pick is disarmed. */
static int
publish_track(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const struct publish_lane* lane = publish_picked_lane(ui, self);
    const struct publish_change* change = publish_picked_change(ui, self);
    const char* name = lane ? lane->name : "";
    const char* path = change ? change->path : "";
    int moved = 0;

    if( strcmp(self->sel_lane, name) != 0 )
    {
        snprintf(self->sel_lane, sizeof(self->sel_lane), "%s", name);
        if( self->armed == PUBLISH_ARM_DELETE )
            publish_disarm(ui, self);
        moved = 1;
    }
    if( strcmp(self->sel_change, path) != 0 )
    {
        snprintf(self->sel_change, sizeof(self->sel_change), "%s", path);
        if( self->armed == PUBLISH_ARM_DISCARD_ONE )
            publish_disarm(ui, self);
        moved = 1;
    }
    return moved;
}

static void
publish_fill_lanes(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, self->list_lanes, self->lane_row_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_lanes, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < self->lane_row_count; row++ )
    {
        char text[300];
        publish_lane_row_text(ui, &self->lanes[self->lane_rows[row]], text, sizeof(text));
        ToriRSChrome_VListSetRow(ui->chrome, self->list_lanes, row, text);
    }
}

/** The rows Find lane lets through; the pick follows its lane to its new
 *  row, or is dropped with it. */
static void
publish_filter_lanes(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
    int selected = -1;
    int picked = -1;

    if( !find )
        find = "";
    self->lane_row_count = 0;
    for( int i = 0; i < self->lane_count; i++ )
    {
        if( !publish_contains(self->lanes[i].name, find) )
            continue;
        if( self->sel_lane[0] && strcmp(self->lanes[i].name, self->sel_lane) == 0 )
            selected = self->lane_row_count;
        if( self->picked_name[0] && strcmp(self->lanes[i].name, self->picked_name) == 0 )
            picked = self->lane_row_count;
        self->lane_rows[self->lane_row_count++] = i;
    }
    snprintf(self->shown_find, sizeof(self->shown_find), "%s", find);
    if( selected < 0 && self->sel_lane[0] )
    {
        self->sel_lane[0] = '\0';
        if( self->armed == PUBLISH_ARM_DELETE )
            publish_disarm(ui, self);
    }
    /* The row a second activation must repeat moves with its lane too. */
    self->picked_row = picked;
    if( picked < 0 )
        self->picked_name[0] = '\0';
    publish_fill_lanes(ui, self);
    ToriRSChrome_VListSetSelected(ui->chrome, self->list_lanes, selected);
}

static int
publish_kind_index(uint32_t kind)
{
    return kind == TORIRSMAPED_LANE_CHANGE_MODIFIED ? PUBLISH_KIND_MODIFIED
           : kind == TORIRSMAPED_LANE_CHANGE_ADDED  ? PUBLISH_KIND_ADDED
                                                    : PUBLISH_KIND_DELETED;
}

static void
publish_fill_rows(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    int first;
    int shown;

    ToriRSChrome_VListSetCount(ui->chrome, self->list_changes, self->change_row_count);
    if( !ToriRSChrome_VListWindow(ui->chrome, self->list_changes, &first, &shown) )
        return;
    for( int row = first; row < first + shown && row < self->change_row_count; row++ )
    {
        char text[300];
        char size[24];
        const struct publish_change* change = &self->changes[self->change_rows[row]];
        if( change->kind == TORIRSMAPED_LANE_CHANGE_DELETED )
            snprintf(text, sizeof(text), "%s %s", publish_kind_letter(change->kind), change->path);
        else
        {
            Editor_LanesSizeText(change->size, size, (int)sizeof(size));
            snprintf(text, sizeof(text), "%s %s (%s)", publish_kind_letter(change->kind), change->path, size);
        }
        ToriRSChrome_VListSetRow(ui->chrome, self->list_changes, row, text);
    }
}

/** The line under the Changes list: the picked file, whole (a row elides a
 *  long path), and what it is. */
static void
publish_picked_text(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const struct publish_change* change = publish_picked_change(ui, self);
    char text[400];

    if( !change )
        snprintf(text, sizeof(text), "%s", self->change_row_count ? "Pick a change for its file." : "");
    else if( change->kind == TORIRSMAPED_LANE_CHANGE_DELETED )
        snprintf(text, sizeof(text), "%s: deleted by this lane", change->path);
    else
        snprintf(text, sizeof(text), "%s: %s, %u bytes", change->path, publish_kind_word(change->kind), change->size);
    publish_label(ui, self->label_picked, text);
}

/** The rows Find change and the Show boxes let through; the pick follows
 *  its file to its new row, or is dropped with it. */
static void
publish_filter_changes(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const char* find = ToriRSChrome_Text(ui->chrome, self->input_find_change);
    int selected = -1;

    if( !find )
        find = "";
    self->change_row_count = 0;
    for( int i = 0; i < self->change_count; i++ )
    {
        if( !self->show_kind[publish_kind_index(self->changes[i].kind)] || !publish_contains(self->changes[i].path, find) )
            continue;
        if( self->sel_change[0] && strcmp(self->changes[i].path, self->sel_change) == 0 )
            selected = self->change_row_count;
        self->change_rows[self->change_row_count++] = i;
    }
    snprintf(self->shown_find_change, sizeof(self->shown_find_change), "%s", find);
    if( selected < 0 )
    {
        self->sel_change[0] = '\0';
        self->picked_change[0] = '\0';
        if( self->armed == PUBLISH_ARM_DISCARD_ONE )
            publish_disarm(ui, self);
    }
    publish_fill_rows(ui, self);
    ToriRSChrome_VListSetSelected(ui->chrome, self->list_changes, selected);
    publish_picked_text(ui, self);
}

static void
publish_chords(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    /* Publish into lane... wears none: Ctrl+Shift+U opens this panel, it
     * does not publish, and a keycap on the button said it did. */
    edp_button_chord(ui, self->button_promote, EDITOR_HK_FILE_PROMOTE_LANE);
    edp_button_chord(ui, self->button_scratch, EDITOR_HK_FILE_SCRATCH_LANE);
    edp_button_chord(ui, self->button_delete, EDITOR_HK_FILE_DELETE_LANE);
    edp_button_chord(ui, self->button_open, EDITOR_HK_FILE_LANE_OPEN);
    edp_button_chord(ui, self->button_reveal, EDITOR_HK_FILE_LANE_REVEAL);
    edp_button_chord(ui, self->button_diff, EDITOR_HK_FILE_LANE_DIFF);
    edp_button_chord(ui, self->button_discard_one, EDITOR_HK_FILE_LANE_DISCARD);
    edp_button_chord(ui, self->button_copy, EDITOR_HK_FILE_LANE_COPY);
    self->chord_serial = ui->hotkeys.serial;
}

/** The boot line: which lane every launch opens, and how to reach a
 *  switched one. Said in the user's terms -- a launch, not a boot; "quit
 *  and launch the editor again", not a file name -- because the staged
 *  switch is the one thing about lanes a user cannot see happen. */
static void
publish_boot_text(
    const struct EditorUi* ui,
    const struct PublishPanel* self,
    char* out,
    size_t out_size)
{
    const struct EditorUiLane* lane = &ui->lane;
    int const asked_differs = lane->asked[0] && strcmp(lane->asked, lane->name) != 0;

    if( !lane->known )
        out[0] = '\0';
    else if( publish_boot_ignored(self) )
        snprintf(out, out_size,
            "Every launch of this session opens: %s (its manifest's lane). It ignores the boot lane file, so Switch "
            "is off.",
            lane->name);
    else if( lane->next_boot[0] )
        snprintf(out, out_size,
            "Next launch opens: %s. Quit and launch the editor again to switch; this session stays on %s.",
            lane->next_boot, lane->name);
    else if( lane->pinned )
        snprintf(out, out_size, "Every launch opens: %s (set by Switch%s%s).", lane->name,
            asked_differs ? "; the manifest asks for " : "", asked_differs ? lane->asked : "");
    else
        snprintf(out, out_size, "Every launch opens: %s (the manifest's lane). Switch or Scratch lane changes that.",
            lane->name);
}

/** How to get into lane `name`, for a session that can switch and for one
 *  that cannot. */
static void
publish_how_to_open(
    const struct PublishPanel* self,
    const char* name,
    char* out,
    size_t out_size)
{
    if( publish_boot_ignored(self) )
        snprintf(out, out_size, "To work in %s: put lane=%s under [editor:boot] in your manifest, then launch the editor.",
            name, name);
    else
        snprintf(out, out_size,
            "To work in %s: Lanes tab, pick it, Switch at relaunch; or put lane=%s under [editor:boot] in your manifest.",
            name, name);
}

/** The line under the Lanes list: what the picked lane is and how to get
 *  into it. */
static void
publish_lane_picked_text(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const struct publish_lane* lane = publish_picked_lane(ui, self);
    char text[400];

    if( !lane )
        snprintf(text, sizeof(text), "Pick a lane to switch to it, copy its manifest line or delete it.");
    else if( lane->is_this )
        snprintf(text, sizeof(text), "%s is the lane this session edits.", lane->name);
    else if( lane->is_main )
        snprintf(text, sizeof(text), "main is the base tree: every lane is made from it and published into it.");
    else if( lane->held[0] )
        snprintf(text, sizeof(text), "%s is open in an editor: %s", lane->name, lane->held);
    else
        publish_how_to_open(self, lane->name, text, sizeof(text));
    publish_label(ui, self->label_lane_picked, text);
}

/** The list of the showing page takes the height its panel has left: the
 *  panel's view less every other row, in whole list rows, as the Catalog's
 *  result list does (edpanel_catalog.c, catalog_list_fit). The other rows do
 *  not depend on the list's, so it settles in one Build, and a frame where
 *  nothing moved sets nothing (the setter compares). */
static void
publish_fit(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const struct ToriRSChromePanel* panel = &ui->chrome->panels[self->panel];
    int const lanes = ToriRSChrome_PanelActiveTab(ui->chrome, self->panel) == PUBLISH_TAB_LANES;
    int const list = lanes ? self->list_lanes : self->list_changes;
    int const least = lanes ? PUBLISH_LANE_ROWS_MIN : PUBLISH_CHANGE_ROWS_MIN;
    int const most = lanes ? PUBLISH_LANE_ROWS_MAX : PUBLISH_CHANGE_ROWS_MAX;
    int const row_h = ToriRSChrome_VListRowHeight(ui->chrome);
    int const shown = ui->chrome->widgets[list].rows;
    int rows;

    /* Not built yet, or collapsed to its title: no heights to fit to. */
    if( panel->view_h <= 0 || panel->content_h <= 0 )
        return;
    assert(row_h > 0);
    rows = (panel->view_h - (panel->content_h - shown * row_h)) / row_h;
    if( rows < least )
        rows = least;
    if( rows > most )
        rows = most;
    ToriRSChrome_VListSetRows(ui->chrome, list, rows);
}

static void
publish_refresh_widgets(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    char text[1200];
    char chip[96];
    int const on_lane = ui->lane.known && !ui->lane.is_main;
    int const can_write = on_lane && ui->lane.writable && !self->action_pending;
    const struct publish_lane* picked_lane;
    const struct publish_change* picked_change;
    int selected = 0;
    int target_is_base = 0;

    Editor_Lanes_ChipText(ui, chip, (int)sizeof(chip));
    if( !ui->lane.known )
        snprintf(text, sizeof(text), "Lane: asking the editor server...");
    else if( ui->lane.is_main )
        snprintf(text, sizeof(text), "Lane: main -- the tree itself, nothing to publish (%s)", self->lane_content);
    else
        snprintf(text, sizeof(text), "Lane: %s%s", chip, ui->lane.writable ? "" : " [read-only]");
    publish_label(ui, self->label_lane, text);
    publish_boot_text(ui, self, text, sizeof(text));
    publish_label(ui, self->label_boot, text);
    if( !self->changes_known )
        snprintf(text, sizeof(text), "%s", on_lane ? "Changes: scanning..." : "");
    else if( self->change_count == 0 )
        snprintf(text, sizeof(text), "No changes: the lane is its base.");
    else
        snprintf(text, sizeof(text), "%d changed: %d modified, %d added, %d deleted%s",
            self->modified + self->added + self->deleted, self->modified, self->added, self->deleted,
            self->change_count < self->modified + self->added + self->deleted ? " (first 1024 listed)" : "");
    publish_label(ui, self->label_counts, text);

    /* The targets: none, every other lane, then the main tree -- last, and
     * never the default. The selection follows the target's NAME. */
    self->option_count = 0;
    self->options[self->option_count] = "Pick a lane...";
    self->option_lane[self->option_count] = PUBLISH_TARGET_NONE;
    self->option_count++;
    for( int pass = 0; pass < 2; pass++ )
        for( int i = 0; i < self->lane_count && self->option_count < PUBLISH_LANES_MAX + 2; i++ )
        {
            if( self->lanes[i].is_this || self->lanes[i].is_main != pass )
                continue;
            if( strcmp(self->lanes[i].name, self->target_name) == 0 )
            {
                selected = self->option_count;
                target_is_base = self->lanes[i].is_main;
            }
            self->options[self->option_count] = self->lanes[i].is_main ? "main (the base tree)" : self->lanes[i].name;
            self->option_lane[self->option_count] = i;
            self->option_count++;
        }
    /* A target that is no lane any more (deleted) is no target. */
    if( selected == 0 && self->target_name[0] && self->list_known )
    {
        self->target_name[0] = '\0';
        if( self->armed == PUBLISH_ARM_PUBLISH )
            publish_disarm(ui, self);
    }
    ToriRSChrome_DropdownSetOptions(ui->chrome, self->drop_target, self->options, self->option_count, selected);
    /* Keep... only means something when the target is this lane's base. */
    ToriRSChrome_SetHidden(ui->chrome, self->check_keep, !target_is_base);

    publish_filter_lanes(ui, self);
    publish_filter_changes(ui, self);
    publish_lane_picked_text(ui, self);
    picked_lane = publish_picked_lane(ui, self);
    picked_change = publish_picked_change(ui, self);

    ToriRSChrome_SetDisabled(ui->chrome, self->button_promote, !can_write);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_publish,
        !can_write || self->change_count == 0 || selected == 0 || self->check_pending);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_discard, !can_write || self->change_count == 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_discard_one,
        !can_write || !picked_change || !publish_lifecycle(self));
    ToriRSChrome_SetDisabled(ui->chrome, self->button_open,
        !picked_change || picked_change->kind == TORIRSMAPED_LANE_CHANGE_DELETED);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_reveal, !picked_change);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_diff,
        !picked_change || picked_change->kind != TORIRSMAPED_LANE_CHANGE_MODIFIED || !publish_lifecycle(self));
    ToriRSChrome_SetDisabled(ui->chrome, self->button_copy, self->change_row_count == 0);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_create, !ui->lane.known || self->action_pending);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_scratch, !ui->lane.known || self->action_pending);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_switch,
        !publish_v2(ui) || self->action_pending || !picked_lane || publish_boot_ignored(self));
    ToriRSChrome_SetDisabled(ui->chrome, self->button_copy_line, !picked_lane);
    ToriRSChrome_SetDisabled(ui->chrome, self->button_delete,
        !publish_lifecycle(self) || self->action_pending || !picked_lane || picked_lane->is_main
            || picked_lane->is_this || picked_lane->held[0] || publish_lane_is_next_boot(ui, self, picked_lane));
    ToriRSChrome_SetDisabled(ui->chrome, self->button_refresh, !ui->lane.known || self->changes_pending);
}

/* ---- after a discard --------------------------------------------------------------- */

static size_t
publish_change_stem_length(const char* path);

/** 1 when (x, z) is among the squares the discard in flight removes. */
static int
publish_square_removed(
    const struct PublishPanel* self,
    int x,
    int z)
{
    for( int r = 0; r < self->removed_count; r++ )
        if( self->removed[r].x == x && self->removed[r].z == z )
            return 1;
    return 0;
}

/** 1 when every listed change sharing `path`'s stem (a square's .jm2, .jl2,
 *  .filepack) is one the lane ADDED. */
static int
publish_stem_all_added(
    const struct PublishPanel* self,
    const char* path)
{
    size_t const stem = publish_change_stem_length(path);

    for( int i = 0; i < self->change_count; i++ )
    {
        const char* const other = self->changes[i].path;
        if( publish_change_stem_length(other) == stem && strncmp(other, path, stem) == 0
            && self->changes[i].kind != TORIRSMAPED_LANE_CHANGE_ADDED )
            return 0;
    }
    return 1;
}

/**
 * The squares the discard about to be sent removes. One path (`only`): the
 * square whose terrain file the lane ADDED and this discards. The lot
 * (`only` NULL): every square whose listed files are all ADDED -- the base
 * keeps a square's .jm2, .jl2 and .filepack together, so a square with
 * nothing but additions is one the base does not have. Told by the .jm2
 * alone, a square whose .jm2 an earlier one-file discard had already taken
 * (its .jl2 and .filepack still listed) was reopened after Discard all, and
 * the World went on drawing a square that no longer existed.
 */
static void
publish_note_removed(
    struct PublishPanel* self,
    const char* only)
{
    self->removed_count = 0;
    for( int i = 0; i < self->change_count && self->removed_count < PUBLISH_REMOVED_MAX; i++ )
    {
        const struct publish_change* change = &self->changes[i];
        struct publish_square square;
        if( change->kind != TORIRSMAPED_LANE_CHANGE_ADDED )
            continue;
        if( only )
        {
            if( strcmp(change->path, only) == 0 && Editor_LanesPathSquare(change->path, &square.x, &square.z) )
                self->removed[self->removed_count++] = square;
            continue;
        }
        if( Editor_LanesPathSquareFile(change->path, &square.x, &square.z)
            && !publish_square_removed(self, square.x, square.z) && publish_stem_all_added(self, change->path) )
            self->removed[self->removed_count++] = square;
    }
}

/**
 * Reopen the squares this window shows: the server dropped its documents.
 *
 * Not every one of them. On 2026-11-11 a Discard killed the editor: the
 * window had Lumbridge, a scratch square and a square the lane had added 17
 * squares east, the discard removed the added one, and reopening all three
 * as one working set sized a scene to their bounding box ("single allocation
 * over 4 GB"). So: a square the discard removed is not reopened (it is not
 * there); a scratch square is not (it lived in the server's documents, which
 * went); and a square more than two away from the first is not (a working
 * set is 3x3 at most, EDITOR_UI_OPEN_SET_MAX; one further off came from a
 * place or a copy, and the world is one scene).
 */
/** Nothing this window showed is left (the discard took every square it
 *  had): open the newest square of File > Recent squares that the content
 *  tree's maps/ still has, rather than leave the World drawing a square that
 *  is gone under a Squares panel saying "nothing open". 0 when none fits. */
static int
publish_reopen_recent(
    struct EditorUi* ui,
    const struct PublishPanel* self)
{
    const struct Editor_Picker* picker = edp_squares_picker(ui);
    int maps;

    if( !picker )
        return 0;
    maps = Editor_PickerStorageIndex(picker, "maps");
    for( int i = 0; i < EDITOR_RECENT_MAX; i++ )
    {
        char storage[EDITOR_DOC_KEY_MAX];
        int x;
        int z;
        if( !Editor_RecentEntry(&ui->recent, i, &x, &z, storage, (int)sizeof(storage)) )
            break;
        /* The lane's own squares can be gone with the discard; the picker's
         * atlas may predate it, so the discard's own list is asked first. */
        if( storage[0] || publish_square_removed(self, x, z) || Editor_PickerFind(picker, x, z, maps) < 0 )
            continue;
        Editor_UiOpenSquare(ui, x, z, "", -1, 0);
        Editor_UiLog(ui, "publish: the discard took every square this window showed; opened m%d_%d (your most recent"
                         " square that is still there)", x, z);
        return 1;
    }
    return 0;
}

static void
publish_reopen(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct EditorUiSquareRef refs[EDITOR_UI_OPEN_SET_MAX];
    struct Editor_Doc* doc;
    int count = 0;
    int dropped = 0;

    if( !ui->editor )
        return;
    doc = &ui->editor->doc;
    /* The list first, then the opens: an open may rewrite the document's
     * squares under a walk of them. */
    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        const char* storage;
        int removed;
        if( !square->loaded )
            continue;
        storage = Editor_DocSquareStorage(doc, square);
        removed = publish_square_removed(self, square->map_x, square->map_z);
        if( removed || strcmp(storage, EDITOR_DOC_SCRATCH_STORAGE) == 0 || count >= EDITOR_UI_OPEN_SET_MAX
            || (count > 0
                && (abs(square->map_x - refs[0].map_x) > 2 || abs(square->map_z - refs[0].map_z) > 2)) )
        {
            dropped++;
            continue;
        }
        refs[count].map_x = square->map_x;
        refs[count].map_z = square->map_z;
        snprintf(refs[count].storage, sizeof(refs[count].storage), "%s", storage);
        count++;
    }
    if( count == 0 && dropped > 0 && publish_reopen_recent(ui, self) )
    {
        self->removed_count = 0;
        return;
    }
    self->removed_count = 0;
    for( int i = 0; i < count; i++ )
        Editor_UiOpenSquare(ui, refs[i].map_x, refs[i].map_z, refs[i].storage, -1, i > 0);
    if( count && dropped )
        Editor_UiLog(ui, "publish: reopened %d square(s) from the base; %d more (discarded, scratch or far off) closed",
            count, dropped);
    else if( count )
        Editor_UiLog(ui, "publish: reopened %d square(s) from the base", count);
    else if( dropped )
        Editor_UiLog(ui, "publish: no square this window showed is left to reopen; open one from the Squares panel");
}

/** The scratch lane, once the list said which letters are taken. */
static void
publish_scratch_go(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];

    self->scratch_wanted = 0;
    if( !Editor_Lanes_ScratchNameFor(ui, (long long)time(NULL), name, (int)sizeof(name)) )
    {
        Editor_UiStatus(ui, "Scratch lane refused: today's 26 scratch lanes (a to z) exist; promote or delete one");
        return;
    }
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, name);
    if( !publish_send(ui, TORIRSMAPED_REQ_LANE_CREATE, &payload) )
    {
        Editor_UiStatus(ui, "Scratch lane refused: the editor server connection is gone");
        return;
    }
    snprintf(self->scratch_name, sizeof(self->scratch_name), "%s", name);
    self->action_pending = 1;
    publish_say(ui, self, "making the scratch lane (about 15 s)...", "", "");
    Editor_UiStatus(ui, "Scratch lane %s: mirroring main...", name);
}

/* ---- the picked change's file ------------------------------------------------------ */

/** The picked change's file in the LANE, as this machine spells it. 0 with
 *  a status sentence when there is no pick or no lane directory here. */
static int
publish_change_path(
    struct EditorUi* ui,
    struct PublishPanel* self,
    const char* verb,
    const struct publish_change** out_change,
    char* out,
    size_t out_size)
{
    const struct publish_change* change = publish_picked_change(ui, self);

    *out_change = change;
    if( !change )
    {
        Editor_UiStatus(ui, "%s refused: pick a file in the Lanes panel's Changes list first", verb);
        return 0;
    }
    if( !self->lane_content[0] )
    {
        Editor_UiStatus(ui, "%s refused: this window does not know the lane's folder yet", verb);
        return 0;
    }
    snprintf(out, out_size, "%s/%s", self->lane_content, change->path);
    return 1;
}

static int
publish_open_change(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const struct publish_change* change;
    char path[1400];
    char command[2048];
    enum EditorHost_Status status;

    if( !publish_change_path(ui, self, "Open in editor", &change, path, sizeof(path)) )
        return 0;
    if( change->kind == TORIRSMAPED_LANE_CHANGE_DELETED )
    {
        Editor_UiStatus(ui, "Open in editor refused: %s was deleted by this lane; there is no file to open", change->path);
        return 0;
    }
    status = Editor_HostLocalOpenInEditor(path, 1, command, sizeof(command));
    if( status != EDITOR_HOST_OK )
    {
        Editor_UiStatus(ui, "Open in editor %s: %s",
            status == EDITOR_HOST_UNSUPPORTED ? "is not available on this platform" : "did not start", change->path);
        Editor_UiLog(ui, "editor: the lane's file is %s", path);
        return 0;
    }
    Editor_UiLog(ui, "editor: open in editor: %s", command);
    Editor_UiStatus(ui, "Opened the lane's %s in your editor", change->path);
    return 1;
}

static int
publish_reveal_change(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const struct publish_change* change;
    char path[1400];
    char command[2048];
    enum EditorHost_Status status;

    if( !publish_change_path(ui, self, EDITOR_REVEAL_CAPTION, &change, path, sizeof(path)) )
        return 0;
    status = Editor_HostLocalRevealInFileManager(path, command, sizeof(command));
    if( status != EDITOR_HOST_OK )
    {
        Editor_UiStatus(ui, "%s %s: %s", EDITOR_REVEAL_CAPTION,
            status == EDITOR_HOST_UNSUPPORTED ? "is not available on this platform"
            : status == EDITOR_HOST_REFUSED   ? "found nothing of this path on disk"
                                              : "did not start",
            change->path);
        Editor_UiLog(ui, "editor: the lane's file is %s", path);
        return 0;
    }
    Editor_UiLog(ui, "editor: show in %s: %s", EDITOR_REVEAL_APP, command);
    if( change->kind == TORIRSMAPED_LANE_CHANGE_DELETED )
        Editor_UiStatus(ui, "%s was deleted by this lane: showing its folder in %s", change->path, EDITOR_REVEAL_APP);
    else
        Editor_UiStatus(ui, "Showing the lane's %s in %s", change->path, EDITOR_REVEAL_APP);
    return 1;
}

/** Diff against base: ask the server for a copy of the base's file
 *  (LANE_DIFF); FACT_LANE_DIFF starts the diff tool. */
static int
publish_diff_change(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const struct publish_change* change = publish_picked_change(ui, self);
    struct ToriRSMapEdBuf payload;

    if( !change )
    {
        Editor_UiStatus(ui, "Diff refused: pick a file in the Lanes panel's Changes list first");
        return 0;
    }
    if( change->kind != TORIRSMAPED_LANE_CHANGE_MODIFIED )
    {
        Editor_UiStatus(ui, "Diff refused: %s was %s by this lane; there are not two files to compare", change->path,
            publish_kind_word(change->kind));
        return 0;
    }
    if( !publish_lifecycle(self) )
    {
        Editor_UiStatus(ui, "Diff refused: this editor server predates it");
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, change->path);
    if( !publish_send(ui, TORIRSMAPED_REQ_LANE_DIFF, &payload) )
    {
        Editor_UiStatus(ui, "Diff refused: the editor server connection is gone");
        return 0;
    }
    return 1;
}

/** FACT_LANE_DIFF: the two files are on disk; start the user's diff tool. */
static void
publish_diff_landed(
    struct EditorUi* ui,
    uint32_t status,
    const char* lane_path,
    const char* base_copy,
    const char* why)
{
    char command[2700];
    const char* name = strrchr(lane_path, '/');
    enum EditorHost_Status started;

    name = name ? name + 1 : lane_path;
    if( status != EDITOR_HOST_OK )
    {
        Editor_UiStatus(ui, "Diff refused: %s", why);
        return;
    }
    started = Editor_DiffInEditor(base_copy, lane_path, command, sizeof(command));
    if( started == EDITOR_HOST_OK )
    {
        Editor_UiLog(ui, "editor: diff: %s", command);
        Editor_UiStatus(ui, "Diff of %s opened: the base's side is a copy, the lane's side is the file", name);
        return;
    }
    Editor_UiLog(ui, "editor: the base's copy is %s", base_copy);
    Editor_UiLog(ui, "editor: the lane's file is %s", lane_path);
    if( started == EDITOR_HOST_ABSENT )
        Editor_UiStatus(ui,
            "Diff needs VS Code on PATH (code --diff) or TORIRS_EDITOR_DIFF_CMD; both files are named in the Console");
    else
        Editor_UiStatus(ui, "Diff %s; both files are named in the Console",
            started == EDITOR_HOST_UNSUPPORTED ? "is not available on this platform" : "did not start");
}

/** Copy list: the listed changes' paths, one a line. */
static int
publish_copy_changes(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    size_t const room = (size_t)self->change_row_count * (sizeof(self->changes[0].path) + 1) + 1;
    char* text;
    size_t at = 0;
    int took;

    if( self->change_row_count == 0 )
    {
        Editor_UiStatus(ui, "Copy list refused: the Changes list is empty");
        return 0;
    }
    text = ToriMem_Alloc(ToriMem_Editor(), room);
    assert(text);
    for( int row = 0; row < self->change_row_count; row++ )
        at += (size_t)snprintf(text + at, room - at, "%s\n", self->changes[self->change_rows[row]].path);
    took = PlatformClipboard_SetText(text);
    ToriMem_Free(ToriMem_Editor(), text);
    if( !took )
    {
        Editor_UiStatus(ui, "Copy list refused: the system clipboard did not take the %d path(s)", self->change_row_count);
        return 0;
    }
    Editor_UiStatus(ui, "Copied %d changed path(s) to the clipboard%s", self->change_row_count,
        self->change_row_count < self->change_count ? " (the ones listed)" : "");
    return 1;
}

/** How much of `path` names the thing its file belongs to: up to the first
 *  '.' of its last part ("maps/m64_50.jm2" -> "maps/m64_50"). */
static size_t
publish_change_stem_length(const char* path)
{
    const char* const slash = strrchr(path, '/');
    const char* const leaf = slash ? slash + 1 : path;
    const char* const dot = strchr(leaf, '.');

    return dot && dot != leaf ? (size_t)(dot - path) : strlen(path);
}

/** The OTHER listed changes that share `path`'s stem: a square's .jm2 / .jl2 /
 *  .filepack, a script's sibling files. */
static int
publish_change_siblings(
    const struct PublishPanel* self,
    const char* path)
{
    size_t const stem = publish_change_stem_length(path);
    int count = 0;

    for( int i = 0; i < self->change_count; i++ )
    {
        const char* const other = self->changes[i].path;

        if( strcmp(other, path) != 0 && publish_change_stem_length(other) == stem && strncmp(other, path, stem) == 0 )
            count++;
    }
    return count;
}

/** Discard this change...: the picked file back to the base (asks twice). */
static int
publish_discard_change(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const struct publish_change* change = publish_picked_change(ui, self);
    struct ToriRSMapEdBuf payload;
    char path[sizeof(change->path)];

    if( !change )
    {
        Editor_UiStatus(ui, "Discard refused: pick a file in the Lanes panel's Changes list first");
        return 0;
    }
    if( !publish_lifecycle(self) )
    {
        Editor_UiStatus(ui, "Discard refused: this editor server discards the lot or nothing (Discard all...)");
        return 0;
    }
    snprintf(path, sizeof(path), "%s", change->path);
    if( !publish_is_armed(self, PUBLISH_ARM_DISCARD_ONE, path) )
    {
        int const siblings = publish_change_siblings(self, path);
        char rest[96];

        /* A square is several files (.jm2, .jl2, .filepack): dropping one
         * leaves the others, which the prompt says rather than hides. */
        rest[0] = '\0';
        if( siblings > 0 )
            snprintf(rest, sizeof(rest), " %d more change(s) of %.*s stay.", siblings,
                (int)publish_change_stem_length(path), path);
        publish_arm(ui, self, PUBLISH_ARM_DISCARD_ONE, path);
        publish_prompt(ui, self, "Discard %s? %s%s Press Discard this change: sure?", path,
            change->kind == TORIRSMAPED_LANE_CHANGE_ADDED
                ? "This lane added it; it is removed (the base has none)."
                : change->kind == TORIRSMAPED_LANE_CHANGE_DELETED ? "The base's file comes back."
                                                                  : "The base's version comes back.",
            rest);
        return 1;
    }
    publish_disarm(ui, self);
    publish_note_removed(self, path);
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, path);
    if( !publish_send(ui, TORIRSMAPED_REQ_LANE_DISCARD, &payload) )
    {
        Editor_UiStatus(ui, "Discard refused: the editor server connection is gone");
        return 0;
    }
    self->action_pending = 1;
    publish_say(ui, self, "discarding the one change...", "", "");
    return 1;
}

/* ---- the server's answers ------------------------------------------------------------ */

static void
publish_apply_fact(
    struct EditorUi* ui,
    struct PublishPanel* self,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    if( type == TORIRSMAPED_FACT_LANE_INFO )
    {
        char live[1024];
        char name[TORIRSMAPED_LANE_NAME_MAX + 8];
        char next_boot[TORIRSMAPED_LANE_NAME_MAX + 8];
        int is_main;
        int writable;
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        ToriRSMapEd_GetString(&reader, self->lane_content, sizeof(self->lane_content));
        ToriRSMapEd_GetString(&reader, live, sizeof(live));
        ToriRSMapEd_GetString(&reader, self->lane_base, sizeof(self->lane_base));
        is_main = (int)ToriRSMapEd_GetU32(&reader);
        writable = (int)ToriRSMapEd_GetU32(&reader);
        self->info_pending = 0;
        if( reader.bad )
            return;
        self->info_known = 1;
        /* Where the lane's files are on disk, for every panel that opens or
         * reveals one (the Triggers panel's paths, editor_panel.c). */
        if( strcmp(ui->lane.content, self->lane_content) != 0 )
        {
            snprintf(ui->lane.content, sizeof(ui->lane.content), "%s", self->lane_content);
            ui->lane.serial++;
        }
        /* The workbench tail: what the next launch opens, then what the
         * boot asked for; then the life-cycle tail: the server's features
         * and where the boot lane file is (a server before any of them says
         * none of them). */
        {
            char asked[TORIRSMAPED_LANE_NAME_MAX + 8];
            const char* next_heard = NULL;
            const char* asked_heard = NULL;
            self->features = 0;
            self->boot_file[0] = '\0';
            ToriRSMapEd_GetString(&reader, next_boot, sizeof(next_boot));
            if( !reader.bad )
            {
                next_heard = next_boot;
                ToriRSMapEd_GetString(&reader, asked, sizeof(asked));
                if( !reader.bad )
                {
                    uint32_t features;
                    asked_heard = asked;
                    features = ToriRSMapEd_GetU32(&reader);
                    ToriRSMapEd_GetString(&reader, self->boot_file, sizeof(self->boot_file));
                    if( !reader.bad )
                        self->features = features;
                    else
                        self->boot_file[0] = '\0';
                }
            }
            publish_lane_note(ui, name, is_main, writable, is_main ? 0 : -1, next_heard, asked_heard);
        }
    }
    else if( type == TORIRSMAPED_FACT_LANE_LIST )
    {
        uint32_t count = ToriRSMapEd_GetU32(&reader);
        uint32_t tail;
        self->lane_count = 0;
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            char content[1024];
            char base[1024];
            struct publish_lane lane;
            memset(&lane, 0, sizeof(lane));
            ToriRSMapEd_GetString(&reader, lane.name, sizeof(lane.name));
            ToriRSMapEd_GetString(&reader, content, sizeof(content));
            ToriRSMapEd_GetString(&reader, base, sizeof(base));
            lane.is_main = (int)ToriRSMapEd_GetU32(&reader);
            lane.is_this = (int)ToriRSMapEd_GetU32(&reader);
            lane.changes = -1;
            if( self->lane_count < PUBLISH_LANES_MAX )
                self->lanes[self->lane_count++] = lane;
        }
        /* The workbench tail: counts and lock holders, row for row. */
        tail = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < tail && !reader.bad; i++ )
        {
            char held[256];
            int32_t const changes = (int32_t)ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetString(&reader, held, sizeof(held));
            if( reader.bad || (int)i >= self->lane_count )
                break;
            self->lanes[i].changes = changes;
            snprintf(self->lanes[i].held, sizeof(self->lanes[i].held), "%s", held);
        }
        self->list_pending = 0;
        self->list_known = 1;
        if( self->scratch_wanted )
            publish_scratch_go(ui, self);
    }
    else if( type == TORIRSMAPED_FACT_LANE_CHANGES )
    {
        uint32_t rows;
        uint32_t const state = ToriRSMapEd_GetU32(&reader);
        if( state == TORIRSMAPED_LANE_CHANGES_SCANNING )
        {
            /* The rows follow as a broadcast when the scan ends. */
            self->changes_known = 0;
            self->changes_pending = 1;
            return;
        }
        self->modified = (int)ToriRSMapEd_GetU32(&reader);
        self->added = (int)ToriRSMapEd_GetU32(&reader);
        self->deleted = (int)ToriRSMapEd_GetU32(&reader);
        rows = ToriRSMapEd_GetU32(&reader);
        self->change_count = 0;
        for( uint32_t i = 0; i < rows && !reader.bad; i++ )
        {
            struct publish_change change;
            change.kind = ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetString(&reader, change.path, sizeof(change.path));
            change.size = ToriRSMapEd_GetU32(&reader);
            if( self->change_count < PUBLISH_CHANGE_MAX )
                self->changes[self->change_count++] = change;
        }
        self->changes_known = 1;
        self->changes_pending = 0;
        if( ui->lane.known )
            publish_lane_note(ui, NULL, 0, -1, self->modified + self->added + self->deleted, NULL, NULL);
    }
    else if( type == TORIRSMAPED_FACT_LANE_COUNT )
    {
        char name[TORIRSMAPED_LANE_NAME_MAX + 8];
        char next_boot[TORIRSMAPED_LANE_NAME_MAX + 8];
        int is_main;
        int changes;
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        is_main = (int)ToriRSMapEd_GetU32(&reader);
        changes = (int)ToriRSMapEd_GetU32(&reader);
        changes += (int)ToriRSMapEd_GetU32(&reader);
        changes += (int)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, next_boot, sizeof(next_boot));
        if( reader.bad )
            return;
        /* The server broadcasts a lane's count as its walk lands (its own
         * lane's answers the chip; any lane's refreshes the list's row), so a
         * count may name a lane that is not this session's. */
        for( int i = 0; i < self->lane_count; i++ )
            if( strcmp(self->lanes[i].name, name) == 0 )
                self->lanes[i].changes = changes;
        if( ui->lane.known && strcmp(ui->lane.name, name) != 0 )
            return;
        self->count_pending = 0;
        publish_lane_note(ui, name, is_main, -1, changes, next_boot, NULL);
    }
    else if( type == TORIRSMAPED_FACT_LANE_DIFF )
    {
        char lane_path[2100];
        char base_copy[1200];
        char why[1400];
        uint32_t const status = ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, lane_path, sizeof(lane_path));
        ToriRSMapEd_GetString(&reader, base_copy, sizeof(base_copy));
        ToriRSMapEd_GetString(&reader, why, sizeof(why));
        if( !reader.bad )
            publish_diff_landed(ui, status, lane_path, base_copy, why);
    }
    else if( type == TORIRSMAPED_FACT_LANE_PUBLISHED )
    {
        uint32_t kind = ToriRSMapEd_GetU32(&reader);
        uint32_t status = ToriRSMapEd_GetU32(&reader);
        char message[1200];
        char target[64];
        char how[400];
        uint32_t written;
        uint32_t removed;
        uint32_t reload;
        ToriRSMapEd_GetString(&reader, message, sizeof(message));
        ToriRSMapEd_GetString(&reader, target, sizeof(target));
        written = ToriRSMapEd_GetU32(&reader);
        removed = ToriRSMapEd_GetU32(&reader);
        reload = ToriRSMapEd_GetU32(&reader);
        if( kind == TORIRSMAPED_LANE_PUBLISHED_CHECK )
        {
            /* The dry run of a Publish into lane...: ours arms the second
             * press; another window's is that window's. */
            if( !self->check_pending )
                return;
            self->check_pending = 0;
            Editor_UiLog(ui, "publish: %s", message);
            if( status != EDITOR_HOST_OK )
            {
                publish_say(ui, self, message, "", "");
                Editor_UiStatus(ui, "Publish refused: %s", message);
            }
            else
            {
                publish_arm(ui, self, PUBLISH_ARM_PUBLISH, target);
                publish_say(ui, self, message, "Press Publish: sure? within 8 s to write them; nothing is written yet.", "");
                if( strcmp(target, "main") == 0 )
                    /* The base every lane is made from: named as a tree, never
                     * as just another lane name. */
                    Editor_UiStatus(ui, "Publish %u file(s) into the MAIN tree (%s)? Press Publish: sure? within 8 s",
                        written, self->lane_base);
                else if( removed )
                    Editor_UiStatus(ui, "Publish %u file(s) into %s, OVERWRITING %u it changed too? Press Publish: sure?",
                        written, target, removed);
                else
                    Editor_UiStatus(ui, "Publish %u file(s) into %s? Press Publish: sure? within 8 s", written, target);
            }
            self->refresh_wanted = 1;
            return;
        }
        self->action_pending = 0;
        publish_say(ui, self, message, "", "");
        if( kind == TORIRSMAPED_LANE_PUBLISHED_CREATE && self->scratch_name[0]
            && strcmp(target, self->scratch_name) == 0 )
        {
            /* Our scratch lane: made, now the session goes to it -- unless
             * it ignores the file a switch is written to. */
            if( status != EDITOR_HOST_OK )
                Editor_UiStatus(ui, "Scratch lane refused: %s", message);
            else if( !publish_v2(ui) )
                Editor_UiStatus(ui,
                    "Scratch lane %s made from main; this editor server cannot switch lanes: launch the editor with "
                    "lane=%s",
                    target, target);
            else if( publish_boot_ignored(self) )
            {
                snprintf(message, sizeof(message), "Scratch lane %s made from main.", target);
                publish_how_to_open(self, target, how, sizeof(how));
                publish_say(ui, self, message, how, "");
                Editor_UiLog(ui, "publish: %s", how);
                Editor_UiStatus(ui,
                    "Scratch lane %s made from main. This session ignores the boot lane file, so no switch was recorded",
                    target);
            }
            else
                self->scratch_switching = Editor_Lanes_Switch(ui, target);
            self->scratch_name[0] = '\0';
        }
        else if( kind == TORIRSMAPED_LANE_PUBLISHED_CREATE )
        {
            /* Lane from main: the server's sentence carries two whole paths;
             * here it is the lane and how to get into it. */
            if( status != EDITOR_HOST_OK )
                Editor_UiStatus(ui, "New lane refused: %s", message);
            else
            {
                snprintf(message, sizeof(message), "Lane %s made from main: a clean mirror, nothing published into it.",
                    target);
                publish_how_to_open(self, target, how, sizeof(how));
                publish_say(ui, self, message, how, "");
                Editor_UiLog(ui, "publish: %s", how);
                Editor_UiStatus(ui, "%s", message);
                ToriRSChrome_SetText(ui->chrome, self->input_lane_name, "");
            }
        }
        else if( kind == TORIRSMAPED_LANE_PUBLISHED_SWITCH )
        {
            int const scratch = self->scratch_switching;
            self->scratch_switching = 0;
            if( status == EDITOR_HOST_OK )
            {
                publish_lane_note(ui, NULL, 0, -1, -1, target, NULL);
                if( strcmp(target, ui->lane.name) == 0 )
                    Editor_UiStatus(ui, "This editor is on %s already; every launch opens it from now on", target);
                else if( scratch )
                    Editor_UiStatus(ui,
                        "Scratch lane %s made from main; it opens from the next launch on. Quit and launch the "
                        "editor again to go there",
                        target);
                else
                    Editor_UiStatus(ui,
                        "Switched: %s opens from the next launch on. Quit and launch the editor again to go there; "
                        "this session stays on %s",
                        target, ui->lane.name);
            }
            else
                Editor_UiStatus(ui, "%s", message);
        }
        else if( kind == TORIRSMAPED_LANE_PUBLISHED_PUBLISH && self->promoting
                 && strcmp(target, self->promote_name) == 0 )
        {
            /* Our promote: what exists where, how to get there, and what
             * the new lane does not have. */
            self->promoting = 0;
            if( status != EDITOR_HOST_OK )
                Editor_UiStatus(ui, "Promote refused: %s", message);
            else
            {
                static const char k_note[] =
                    "Its first RUN is a full build (its live/ is empty) and its play account starts fresh.";
                snprintf(message, sizeof(message), "Lane %s made with %u file(s); this lane (%s) is unchanged.", target,
                    written + removed, ui->lane.name);
                publish_how_to_open(self, target, how, sizeof(how));
                publish_say(ui, self, message, how, k_note);
                Editor_UiLog(ui, "publish: %s", how);
                Editor_UiLog(ui, "publish: %s", k_note);
                Editor_UiStatus(ui, "%s", message);
                ToriRSChrome_SetText(ui->chrome, self->input_new, "");
            }
        }
        else if( kind == TORIRSMAPED_LANE_PUBLISHED_DELETE && status == EDITOR_HOST_OK )
        {
            snprintf(message, sizeof(message),
                "Lane %s deleted: %u link(s) to the base dropped, %u file(s) of its own removed. The base tree was not "
                "touched.",
                target, written, removed);
            publish_say(ui, self, message, "", "");
            Editor_UiStatus(ui, "%s", message);
        }
        else
            Editor_UiStatus(ui, "%s%s: %s",
                kind == TORIRSMAPED_LANE_PUBLISHED_PUBLISH   ? "Publish"
                : kind == TORIRSMAPED_LANE_PUBLISHED_DISCARD ? "Discard"
                : kind == TORIRSMAPED_LANE_PUBLISHED_DELETE  ? "Delete lane"
                                                             : "New lane",
                status == EDITOR_HOST_OK ? "" : " refused", message);
        if( reload )
            publish_reopen(ui, self);
        /* The lane list and the changes moved: ask again. */
        self->refresh_wanted = 1;
        publish_ask_count(ui, self);
    }
}

static void
publish_tick(
    struct EditorUi* ui,
    void* opaque)
{
    struct PublishPanel* self = opaque;
    uint32_t type;
    uint8_t* body;
    uint32_t length;
    int heard = 0;
    int moved = 0;
    int visible;

    if( !ui->editor )
        return;
    visible = ui->chrome->panels[self->panel].visible;
    if( visible && !self->was_visible )
        self->refresh_wanted = 1;
    /* The selections, read against the rows they were made on: before any
     * fact below rewrites those, and not on the frame the panel opens (the
     * lists may have moved while it was shut). */
    else if( visible )
        moved = publish_track(ui, self);
    self->was_visible = visible;
    /* The lane's name for every window, from the first frame: one cheap
     * LANE_INFO, whether the panel shows or not. */
    if( !self->info_known && !self->info_pending )
        publish_ask_info(ui, self);
    while( Editor_HostMapEdLanesTake(&ui->editor->host, &type, &body, &length) )
    {
        publish_apply_fact(ui, self, type, body, length);
        ToriMem_Free(ToriMem_Editor(), body);
        heard = 1;
    }
    {
        int const unsaved = Editor_DocHasUnsaved(&ui->editor->doc);
        if( self->seen_unsaved && !unsaved && ui->lane.known && !ui->lane.is_main )
        {
            self->refresh_wanted = 1;
            publish_ask_count(ui, self);
        }
        self->seen_unsaved = unsaved;
    }
    /* The chip's count, on its period (the count alone: no rows). */
    if( ui->lane.known && !ui->lane.is_main
        && (!self->count_ever_asked || ui->chrome->now_ms - self->count_asked_ms >= EDITOR_UI_LANE_COUNT_MS) )
        publish_ask_count(ui, self);
    if( self->refresh_wanted && visible )
        publish_ask(ui, self, 0);
    if( self->armed != PUBLISH_ARM_NONE && ui->chrome->now_ms - self->armed_ms >= PUBLISH_CONFIRM_MS )
        publish_disarm(ui, self);
    if( self->chord_serial != ui->hotkeys.serial )
        publish_chords(ui, self);
    if( !visible )
        return;
    if( heard || self->refresh_wanted || moved )
        publish_refresh_widgets(ui, self);
    else
    {
        const char* find = ToriRSChrome_Text(ui->chrome, self->input_find);
        const char* find_change = ToriRSChrome_Text(ui->chrome, self->input_find_change);
        if( find && strcmp(find, self->shown_find) != 0 )
            publish_refresh_widgets(ui, self);
        else if( find_change && strcmp(find_change, self->shown_find_change) != 0 )
            publish_refresh_widgets(ui, self);
        else
        {
            if( ToriRSChrome_VListNeedsRows(ui->chrome, self->list_lanes) )
                publish_fill_lanes(ui, self);
            if( ToriRSChrome_VListNeedsRows(ui->chrome, self->list_changes) )
                publish_fill_rows(ui, self);
        }
    }
    publish_fit(ui, self);
}

/** Lane from main: a clean mirror of main named in the Lanes page's box. */
static void
publish_create(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    const char* name = ToriRSChrome_Text(ui->chrome, self->input_lane_name);
    struct ToriRSMapEdBuf payload;

    char suggest[TORIRSMAPED_LANE_NAME_MAX + 8];

    if( name && name[0] && !publish_name_valid(name) && Editor_LanesNameSuggest(name, suggest, (int)sizeof(suggest)) )
    {
        Editor_UiStatus(ui, "Lane from main refused: \"%s\" is not a lane name (a-z, 0-9, _ and -); try %s", name,
            suggest);
        (void)ToriRSChrome_FocusWidget(ui->chrome, self->input_lane_name, (int)strlen(name));
        return;
    }
    if( !name || !publish_name_valid(name) )
    {
        Editor_UiStatus(ui, "Lane from main refused: type its name in Lane name first (a-z, 0-9, _ and -, not main)");
        (void)ToriRSChrome_FocusWidget(ui->chrome, self->input_lane_name, name ? (int)strlen(name) : 0);
        return;
    }
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, name);
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_CREATE, &payload) )
    {
        self->action_pending = 1;
        publish_say(ui, self, "making the lane (about 15 s)...", "", "");
    }
    publish_refresh_widgets(ui, self);
}

/** Publish into lane...: the first press asks the server what would be
 *  written and overwritten; the second, armed by that answer, publishes. */
static void
publish_publish(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;
    uint32_t flags = ToriRSChrome_Checked(ui->chrome, self->check_keep) ? 0u : (uint32_t)TORIRSMAPED_LANE_PUBLISH_RESET;
    int const sure = publish_is_armed(self, PUBLISH_ARM_PUBLISH, self->target_name);

    if( !self->target_name[0] )
    {
        Editor_UiStatus(ui, "Publish refused: pick the lane to publish into first (Publish into); a new lane is Promote");
        return;
    }
    /* A server without the dry run publishes on the first press, as it
     * always did -- but the main tree still takes two. */
    if( !sure && publish_lifecycle(self) )
        flags |= TORIRSMAPED_LANE_PUBLISH_CHECK;
    else if( !sure && strcmp(self->target_name, "main") == 0 )
    {
        publish_arm(ui, self, PUBLISH_ARM_PUBLISH, self->target_name);
        Editor_UiStatus(ui, "Publish %d file(s) into the MAIN tree (%s)? Press Publish: sure? within 8 s",
            self->modified + self->added + self->deleted, self->lane_base);
        return;
    }
    publish_disarm(ui, self);
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, self->target_name);
    ToriRSMapEd_PutString(&payload, "");
    ToriRSMapEd_PutU32(&payload, flags);
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_PUBLISH, &payload) )
    {
        if( flags & TORIRSMAPED_LANE_PUBLISH_CHECK )
        {
            self->check_pending = 1;
            publish_say(ui, self, "checking what a publish would write...", "", "");
        }
        else
        {
            self->action_pending = 1;
            publish_say(ui, self, "publishing...", "", "");
        }
    }
    publish_refresh_widgets(ui, self);
}

/** Discard all...: every change back to the base (asks twice). */
static void
publish_discard_all(
    struct EditorUi* ui,
    struct PublishPanel* self)
{
    struct ToriRSMapEdBuf payload;

    if( !publish_is_armed(self, PUBLISH_ARM_DISCARD_ALL, "") )
    {
        publish_arm(ui, self, PUBLISH_ARM_DISCARD_ALL, "");
        publish_prompt(ui, self, "Discard all %d change(s), unsaved edits too? Press Discard all: sure? within 8 s",
            self->modified + self->added + self->deleted);
        return;
    }
    publish_disarm(ui, self);
    publish_note_removed(self, NULL);
    memset(&payload, 0, sizeof(payload));
    if( publish_send(ui, TORIRSMAPED_REQ_LANE_DISCARD, &payload) )
    {
        self->action_pending = 1;
        publish_say(ui, self, "discarding...", "", "");
    }
    publish_refresh_widgets(ui, self);
}

static int
publish_activate(
    struct EditorUi* ui,
    void* opaque,
    int widget)
{
    struct PublishPanel* self = opaque;

    if( widget == self->tabs )
    {
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->drop_target )
    {
        int const selected = ToriRSChrome_DropdownSelected(ui->chrome, self->drop_target);
        int const lane = selected >= 0 && selected < self->option_count ? self->option_lane[selected] : PUBLISH_TARGET_NONE;
        snprintf(self->target_name, sizeof(self->target_name), "%s", lane == PUBLISH_TARGET_NONE ? "" : self->lanes[lane].name);
        if( self->armed == PUBLISH_ARM_PUBLISH )
            publish_disarm(ui, self);
        publish_refresh_widgets(ui, self);
        return 1;
    }
    for( int i = 0; i < PUBLISH_KINDS; i++ )
        if( widget == self->check_show[i] )
        {
            self->show_kind[i] = ToriRSChrome_Checked(ui->chrome, widget);
            publish_refresh_widgets(ui, self);
            return 1;
        }
    /* A text input activates when it is committed: Enter, and focus going
     * elsewhere too. So a name box is only ever read by its button -- a
     * click on Promote first blurred the box, which promoted, and then
     * pressed the button, which promoted again. */
    if( widget == self->input_find || widget == self->input_find_change || widget == self->check_keep
        || widget == self->input_new || widget == self->input_lane_name )
        return 1;
    if( widget == self->list_changes )
    {
        const struct publish_change* change = publish_picked_change(ui, self);
        /* A double-click on a row (Enter twice) opens its file; a click
         * picks it. */
        int64_t const now = ui->chrome->now_ms;
        int const again = change && strcmp(change->path, self->picked_change) == 0
                          && now - self->picked_change_ms <= PUBLISH_DOUBLE_MS;
        (void)publish_track(ui, self);
        snprintf(self->picked_change, sizeof(self->picked_change), "%s", change ? change->path : "");
        self->picked_change_ms = now;
        publish_refresh_widgets(ui, self);
        if( again && change->kind != TORIRSMAPED_LANE_CHANGE_DELETED )
            (void)publish_open_change(ui, self);
        return 1;
    }
    if( widget == self->list_lanes )
    {
        /* A second activation of the picked row (Enter, a double-click) is
         * its Switch; the first picks it. */
        int const row = ToriRSChrome_VListSelected(ui->chrome, self->list_lanes);
        (void)publish_track(ui, self);
        if( row >= 0 && row < self->lane_row_count )
        {
            const char* picked = self->lanes[self->lane_rows[row]].name;
            int64_t const now = ui->chrome->now_ms;
            if( row == self->picked_row && strcmp(picked, self->picked_name) == 0
                && now - self->picked_ms <= PUBLISH_DOUBLE_MS )
                (void)Editor_Lanes_Switch(ui, picked);
            else
            {
                self->picked_row = row;
                snprintf(self->picked_name, sizeof(self->picked_name), "%s", picked);
            }
            self->picked_ms = now;
        }
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_refresh )
    {
        self->changes_known = 0;
        publish_ask(ui, self, 1);
        publish_ask_count(ui, self);
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_scratch )
    {
        (void)Editor_Lanes_Scratch(ui);
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_switch )
    {
        const struct publish_lane* lane = publish_picked_lane(ui, self);
        if( !lane )
            Editor_UiStatus(ui, "Switch refused: pick a lane in the list first");
        else
            (void)Editor_Lanes_Switch(ui, lane->name);
        return 1;
    }
    if( widget == self->button_copy_line )
    {
        const struct publish_lane* lane = publish_picked_lane(ui, self);
        char line[TORIRSMAPED_LANE_NAME_MAX + 16];
        if( !lane )
            Editor_UiStatus(ui, "Copy manifest line refused: pick a lane in the list first");
        else
        {
            snprintf(line, sizeof(line), "lane=%s", lane->name);
            if( PlatformClipboard_SetText(line) )
                Editor_UiStatus(ui, "Copied %s: paste it under [editor:boot] in a manifest to launch on that lane", line);
            else
                Editor_UiStatus(ui, "Copy refused: the system clipboard did not take it; the line is %s", line);
        }
        return 1;
    }
    if( widget == self->button_promote )
    {
        (void)Editor_Lanes_Promote(ui, NULL);
        return 1;
    }
    if( widget == self->button_publish )
    {
        publish_publish(ui, self);
        return 1;
    }
    if( widget == self->button_discard )
    {
        publish_discard_all(ui, self);
        return 1;
    }
    if( widget == self->button_discard_one )
    {
        (void)publish_discard_change(ui, self);
        publish_refresh_widgets(ui, self);
        return 1;
    }
    if( widget == self->button_delete )
    {
        (void)Editor_Lanes_Delete(ui);
        return 1;
    }
    if( widget == self->button_create )
    {
        publish_create(ui, self);
        return 1;
    }
    if( widget == self->button_open )
    {
        (void)publish_open_change(ui, self);
        return 1;
    }
    if( widget == self->button_reveal )
    {
        (void)publish_reveal_change(ui, self);
        return 1;
    }
    if( widget == self->button_diff )
    {
        (void)publish_diff_change(ui, self);
        return 1;
    }
    if( widget == self->button_copy )
    {
        (void)publish_copy_changes(ui, self);
        return 1;
    }
    return 0;
}

/** The editor opened or closed: the panel starts (and stays) hidden until
 *  File > Publish... opens it, like the Live panel; TORIRS_EDITOR_PUBLISH=show
 *  (photographs) opens it with the editor. */
static void
publish_set_visible(
    struct EditorUi* ui,
    void* opaque,
    int visible)
{
    struct PublishPanel* self = opaque;
    const char* value = getenv("TORIRS_EDITOR_PUBLISH");
    int const show = visible && value && strcmp(value, "show") == 0;
    ToriRSChrome_PanelSetVisible(ui->chrome, self->panel, show);
    if( show )
        self->refresh_wanted = 1;
}

static void
publish_destroy(
    struct EditorUi* ui,
    void* opaque)
{
    struct PublishPanel* self = opaque;
    (void)ui;
    ToriMem_Free(ToriMem_Editor(), self->change_rows);
    ToriMem_Free(ToriMem_Editor(), self->changes);
    ToriMem_Free(ToriMem_Editor(), self);
}

/** A command button: as wide as its row, so its caption is never the one
 *  the label column cuts ("Switch at rela..."). */
static int
publish_button(
    struct EditorUi* ui,
    struct PublishPanel* self,
    const char* caption,
    const char* tip)
{
    int const widget = ToriRSChrome_Button(ui->chrome, self->panel, caption);

    assert(widget >= 0);
    ToriRSChrome_ButtonSetFill(ui->chrome, widget, 1);
    ToriRSChrome_SetTooltip(ui->chrome, widget, tip);
    return widget;
}

void
Editor_Publish_Register(struct EditorUi* ui)
{
    struct PublishPanel* self;
    struct EditorUiPanel desc;
    struct ToriRSChrome* chrome;
    static const char* const k_first_options[] = { "Pick a lane..." };
    static const char* const k_tabs[] = { "Changes", "Lanes" };
    static const char* const k_show[PUBLISH_KINDS] = { "Show modified", "Show added", "Show deleted" };

    assert(ui);
    if( !ui->editor )
        return;
    chrome = ui->chrome;
    self = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*self));
    assert(self);
    self->changes = ToriMem_Calloc(ToriMem_Editor(), PUBLISH_CHANGE_MAX, sizeof(*self->changes));
    assert(self->changes);
    self->change_rows = ToriMem_Calloc(ToriMem_Editor(), PUBLISH_CHANGE_MAX, sizeof(*self->change_rows));
    assert(self->change_rows);
    self->seen_unsaved = Editor_DocHasUnsaved(&ui->editor->doc);
    self->picked_row = -1;
    for( int i = 0; i < PUBLISH_KINDS; i++ )
        self->show_kind[i] = 1;

    self->panel = ToriRSChrome_PanelAdd(chrome, TORIRS_CHROME_PANEL_WINDOW, edp_px(ui, 300), edp_px(ui, 28),
        edp_px(ui, 320), "Lanes");
    assert(self->panel >= 0);
    self->label_lane = ToriRSChrome_Label(chrome, self->panel, "Lane: asking the editor server...");
    self->tabs = ToriRSChrome_Tabs(chrome, self->panel, k_tabs, 2, PUBLISH_TAB_CHANGES);
    assert(self->tabs >= 0);

    /* ---- Changes: what this lane changed, and where it goes ---- */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, PUBLISH_TAB_CHANGES);
    self->label_counts = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_find_change = ToriRSChrome_TextInput(chrome, self->panel, "Find change", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find_change, "part of a changed file's path");
    for( int i = 0; i < PUBLISH_KINDS; i++ )
        self->check_show[i] = ToriRSChrome_Checkbox(chrome, self->panel, k_show[i], 1);
    self->list_changes = ToriRSChrome_VList(chrome, self->panel, "Changes", PUBLISH_CHANGE_ROWS_MAX, 0);
    assert(self->list_changes >= 0);
    ToriRSChrome_VListSetRows(chrome, self->list_changes, PUBLISH_CHANGE_ROWS_MIN);
    ToriRSChrome_SetTooltip(chrome, self->list_changes, "Every file this lane changed; double-click one to open it");
    self->label_picked = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_open = publish_button(ui, self, "Open in editor", "The lane's file, in your own editor");
    self->button_reveal = publish_button(ui, self, EDITOR_REVEAL_CAPTION, "The lane's file, in its folder");
    self->button_diff =
        publish_button(ui, self, "Diff against base", "Your diff tool: a copy of the base's file and the lane's");
    self->button_discard_one =
        publish_button(ui, self, publish_arm_label(PUBLISH_ARM_DISCARD_ONE, 0), "That one file back to the base (press twice)");
    self->button_copy = publish_button(ui, self, "Copy list", "The listed paths, one a line, to the clipboard");
    ToriRSChrome_Separator(chrome, self->panel);
    self->input_new = ToriRSChrome_TextInput(chrome, self->panel, "New lane name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_new, "the new lane: a-z, 0-9, _ and -");
    self->button_promote = publish_button(ui, self, "Promote to new lane...",
        "A new lane from main with these changes; this lane keeps them");
    ToriRSChrome_Separator(chrome, self->panel);
    self->drop_target = ToriRSChrome_Dropdown(chrome, self->panel, "Publish into", k_first_options, 1, 0);
    ToriRSChrome_SetTooltip(chrome, self->drop_target, "An existing lane, or the main tree; a new lane is Promote");
    self->check_keep = ToriRSChrome_Checkbox(chrome, self->panel, "Keep the changes here after publishing", 0);
    ToriRSChrome_SetTooltip(chrome, self->check_keep, "Unticked: publishing into the base leaves this lane clean");
    ToriRSChrome_SetHidden(chrome, self->check_keep, 1);
    self->button_publish = publish_button(ui, self, publish_arm_label(PUBLISH_ARM_PUBLISH, 0),
        "First press lists what it would overwrite; second publishes");
    self->button_discard = publish_button(ui, self, publish_arm_label(PUBLISH_ARM_DISCARD_ALL, 0),
        "Every change back to the base, unsaved ones too (press twice)");

    /* ---- Lanes: every lane, and the ones this session can go to ---- */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, PUBLISH_TAB_LANES);
    self->label_boot = ToriRSChrome_Label(chrome, self->panel, "");
    self->input_find = ToriRSChrome_TextInput(chrome, self->panel, "Find lane", "");
    ToriRSChrome_SetTooltip(chrome, self->input_find, "part of a lane's name");
    self->list_lanes = ToriRSChrome_VList(chrome, self->panel, "Lanes", PUBLISH_LANE_ROWS_MAX, 0);
    assert(self->list_lanes >= 0);
    ToriRSChrome_VListSetRows(chrome, self->list_lanes, PUBLISH_LANE_ROWS_MIN);
    ToriRSChrome_SetTooltip(chrome, self->list_lanes, "Every lane; double-click a row to switch");
    self->label_lane_picked = ToriRSChrome_Label(chrome, self->panel, "");
    self->button_switch =
        publish_button(ui, self, "Switch at relaunch", "The picked lane opens at the next launch (quit, launch again)");
    self->button_copy_line =
        publish_button(ui, self, "Copy manifest line", "lane=<picked>, for a manifest's [editor:boot]");
    self->button_delete = publish_button(ui, self, publish_arm_label(PUBLISH_ARM_DELETE, 0),
        "The picked lane, gone; the base tree is not touched (twice)");
    ToriRSChrome_Separator(chrome, self->panel);
    self->input_lane_name = ToriRSChrome_TextInput(chrome, self->panel, "Lane name", "");
    ToriRSChrome_SetTooltip(chrome, self->input_lane_name, "for Lane from main: a-z, 0-9, _ and -");
    self->button_create =
        publish_button(ui, self, "Lane from main", "A clean mirror of main, named in Lane name; not switched to");
    self->button_scratch =
        publish_button(ui, self, "Scratch lane", "A clean mirror of main as it is now; opens at next launch");
    self->button_refresh = publish_button(ui, self, "Refresh", "Count every lane's changes again");

    /* ---- under both pages ---- */
    ToriRSChrome_PanelBeginTab(chrome, self->panel, -1);
    self->label_status = ToriRSChrome_Label(chrome, self->panel, "");
    self->label_how = ToriRSChrome_Label(chrome, self->panel, "");
    self->label_note = ToriRSChrome_Label(chrome, self->panel, "");
    publish_say(ui, self, "", "", "");
    publish_chords(ui, self);
    ToriRSChrome_PanelSetResizable(chrome, self->panel, 1);
    ToriRSChrome_PanelSetVisible(chrome, self->panel, 0);

    memset(&desc, 0, sizeof(desc));
    desc.name = "publish";
    desc.title = "Lanes";
    desc.self = self;
    desc.chrome_panel = self->panel;
    desc.tick = publish_tick;
    desc.activate = publish_activate;
    desc.set_visible = publish_set_visible;
    desc.destroy = publish_destroy;
    Editor_UiAddPanel(ui, &desc);
}

int
Editor_Publish_Open(struct EditorUi* ui)
{
    assert(ui);
    if( !Editor_UiFindPanel(ui, "publish") )
    {
        Editor_UiLog(ui, "publish: this window has no Lanes panel (no editor server)");
        return 0;
    }
    return Editor_UiTogglePanel(ui, "publish");
}

int
Editor_Lanes_Show(struct EditorUi* ui)
{
    struct PublishPanel* self;

    assert(ui);
    self = publish_self(ui);
    if( !self )
    {
        Editor_UiStatus(ui, "Lanes: this window has no Lanes panel (no editor server)");
        return 0;
    }
    if( !ui->chrome->panels[self->panel].visible )
        (void)Editor_UiTogglePanel(ui, "publish");
    return 1;
}

/** The panel shown on `tab`, for a command that needs its page in view. */
static struct PublishPanel*
publish_show_tab(
    struct EditorUi* ui,
    int tab)
{
    struct PublishPanel* self = publish_self(ui);

    if( !self || !Editor_Lanes_Show(ui) )
        return NULL;
    ToriRSChrome_PanelSetActiveTab(ui->chrome, self->panel, tab);
    return self;
}

int
Editor_LanesList(
    struct EditorUi* ui,
    struct Editor_LaneList* out)
{
    struct PublishPanel* self;

    assert(ui);
    assert(out);
    out->count = 0;
    self = publish_self(ui);
    if( !self || !self->list_known )
        return 0;
    for( int i = 0; i < self->lane_count && out->count < EDITOR_LANES_LIST_MAX; i++ )
    {
        struct Editor_LaneListRow* row = &out->rows[out->count++];
        snprintf(row->name, sizeof(row->name), "%s", self->lanes[i].name);
        row->is_this = self->lanes[i].is_this;
        publish_lane_row_text(ui, &self->lanes[i], row->summary, sizeof(row->summary));
    }
    return out->count;
}

int
Editor_Lanes_ScratchNameFor(
    struct EditorUi* ui,
    long long when,
    char* out,
    int out_size)
{
    struct PublishPanel* self;
    const char* taken[PUBLISH_LANES_MAX];
    char day[16];
    time_t const seconds = (time_t)when;
    struct tm local;
    int count = 0;

    assert(ui);
    assert(out);
    self = publish_self(ui);
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    snprintf(day, sizeof(day), "%02d%02d%02d", local.tm_year % 100, local.tm_mon + 1, local.tm_mday);
    if( self )
        for( int i = 0; i < self->lane_count; i++ )
            taken[count++] = self->lanes[i].name;
    return Editor_LanesScratchName(day, taken, count, out, out_size);
}

int
Editor_Lanes_Scratch(struct EditorUi* ui)
{
    struct PublishPanel* self;

    assert(ui);
    self = publish_self(ui);
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Scratch lane refused: this window has no editor server");
        return 0;
    }
    if( self->action_pending || self->scratch_wanted )
    {
        Editor_UiStatus(ui, "Scratch lane refused: a lane action is in flight; wait for it");
        return 0;
    }
    /* The letters taken are the list's: ask it afresh, then make the lane. */
    self->scratch_wanted = 1;
    publish_ask_list(ui, self, 0);
    Editor_UiStatus(ui, "Scratch lane: reading the lanes for a free name...");
    return 1;
}

int
Editor_Lanes_Switch(
    struct EditorUi* ui,
    const char* name)
{
    struct PublishPanel* self;
    struct ToriRSMapEdBuf payload;

    assert(ui);
    assert(name);
    self = publish_self(ui);
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Switch refused: this window has no editor server");
        return 0;
    }
    if( !publish_v2(ui) )
    {
        Editor_UiStatus(ui, "Switch refused: this editor server predates lane switching; launch the editor with lane=%s",
            name);
        return 0;
    }
    /* A switch is a line in the boot lane file, and this session was told
     * to leave that file alone (it is the owner's): nothing is recorded,
     * and the sentence says what opens the lane instead. */
    if( publish_boot_ignored(self) )
    {
        Editor_UiStatus(ui,
            "Switch refused: this session ignores the boot lane file, so it was not recorded. Launch with lane=%s in "
            "your manifest",
            name);
        return 0;
    }
    if( ui->lane.known && strcmp(name, ui->lane.name) == 0 && !ui->lane.next_boot[0] )
    {
        Editor_UiStatus(ui, "Switch: this editor is on %s already; nothing to change", name);
        return 0;
    }
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, name);
    if( !publish_send(ui, TORIRSMAPED_REQ_LANE_SWITCH, &payload) )
    {
        Editor_UiStatus(ui, "Switch refused: the editor server connection is gone");
        return 0;
    }
    self->action_pending = 1;
    publish_say(ui, self, "switching...", "", "");
    return 1;
}

int
Editor_Lanes_Promote(
    struct EditorUi* ui,
    const char* name)
{
    struct PublishPanel* self;
    struct ToriRSMapEdBuf payload;
    char wanted[TORIRS_CHROME_INPUT_MAX];

    assert(ui);
    self = publish_show_tab(ui, PUBLISH_TAB_CHANGES);
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Promote refused: this window has no editor server");
        return 0;
    }
    if( !name )
        name = ToriRSChrome_Text(ui->chrome, self->input_new);
    snprintf(wanted, sizeof(wanted), "%s", name ? name : "");
    if( !ui->lane.known || ui->lane.is_main )
    {
        Editor_UiStatus(ui, "Promote refused: this editor edits the main tree itself; there is no lane to promote");
        return 0;
    }
    if( !ui->lane.writable )
    {
        Editor_UiStatus(ui, "Promote refused: this window is read-only (another editor holds %s)", ui->lane.name);
        return 0;
    }
    if( self->action_pending )
    {
        Editor_UiStatus(ui, "Promote refused: a lane action is in flight; wait for it");
        return 0;
    }
    if( !publish_name_valid(wanted) )
    {
        char suggest[TORIRSMAPED_LANE_NAME_MAX + 8];

        /* The one thing the command needs is where the caret goes. */
        (void)ToriRSChrome_FocusWidget(ui->chrome, self->input_new, (int)strlen(wanted));
        if( wanted[0] && Editor_LanesNameSuggest(wanted, suggest, (int)sizeof(suggest)) )
            Editor_UiStatus(ui, "Promote refused: \"%s\" is not a lane name (a-z, 0-9, _ and -); try %s", wanted,
                suggest);
        else
            Editor_UiStatus(ui,
                "Promote: type the new lane's name in New lane name (a-z, 0-9, _ and -), then Promote to new lane...");
        return 0;
    }
    for( int i = 0; i < self->lane_count; i++ )
        if( strcmp(self->lanes[i].name, wanted) == 0 )
        {
            Editor_UiStatus(ui, "Promote refused: lane %s exists; name a new one, or publish into it (Publish into)",
                wanted);
            return 0;
        }
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, "");
    ToriRSMapEd_PutString(&payload, wanted);
    ToriRSMapEd_PutU32(&payload, 0u);
    if( !publish_send(ui, TORIRSMAPED_REQ_LANE_PUBLISH, &payload) )
    {
        Editor_UiStatus(ui, "Promote refused: the editor server connection is gone");
        return 0;
    }
    self->action_pending = 1;
    self->promoting = 1;
    snprintf(self->promote_name, sizeof(self->promote_name), "%s", wanted);
    publish_say(ui, self, "making the lane and copying this lane's changes into it (about 15 s)...", "", "");
    Editor_UiStatus(ui, "Promote: making lane %s from main, then copying this lane's changes into it...", wanted);
    publish_refresh_widgets(ui, self);
    return 1;
}

int
Editor_Lanes_Delete(struct EditorUi* ui)
{
    struct PublishPanel* self;
    const struct publish_lane* lane;
    struct ToriRSMapEdBuf payload;
    char name[TORIRSMAPED_LANE_NAME_MAX + 8];

    assert(ui);
    self = publish_show_tab(ui, PUBLISH_TAB_LANES);
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Delete lane refused: this window has no editor server");
        return 0;
    }
    lane = publish_picked_lane(ui, self);
    if( !lane )
    {
        Editor_UiStatus(ui, "Delete lane: pick the lane in the Lanes list first, then Delete lane...");
        return 0;
    }
    if( !publish_lifecycle(self) )
    {
        Editor_UiStatus(ui, "Delete lane refused: this editor server predates it");
        return 0;
    }
    if( lane->is_main )
    {
        Editor_UiStatus(ui, "Delete lane refused: main is the base tree itself, not a lane");
        return 0;
    }
    if( lane->is_this )
    {
        Editor_UiStatus(ui, "Delete lane refused: %s is the lane this editor is on; launch on another lane to delete it",
            lane->name);
        return 0;
    }
    if( lane->held[0] )
    {
        /* "pid N (C:\...\torirs.exe)": the pid names it; a path cut short
         * in the status bar named nothing ("(pid 25056 (C:\Users\...\oldschool-clie);"). */
        const char* const paren = strstr(lane->held, " (");
        int const shown = paren ? (int)(paren - lane->held) : (int)strlen(lane->held);
        Editor_UiStatus(ui, "Delete lane refused: %s is open in an editor (%.*s); close that editor first", lane->name,
            shown, lane->held);
        return 0;
    }
    if( publish_lane_is_next_boot(ui, self, lane) )
    {
        /* The server refuses it too; said here, before a confirm is asked. */
        Editor_UiStatus(ui, "Delete lane refused: every launch opens %s (Switch named it); switch to another lane first",
            lane->name);
        return 0;
    }
    if( self->action_pending )
    {
        Editor_UiStatus(ui, "Delete lane refused: a lane action is in flight; wait for it");
        return 0;
    }
    snprintf(name, sizeof(name), "%s", lane->name);
    if( !publish_is_armed(self, PUBLISH_ARM_DELETE, name) )
    {
        char count[48];
        if( lane->changes > 0 )
            snprintf(count, sizeof(count), "its %d change(s) go with it", lane->changes);
        else if( lane->changes == 0 )
            snprintf(count, sizeof(count), "it has no changes");
        else
            /* Not counted yet (its walk is queued): never "no changes". */
            snprintf(count, sizeof(count), "any changes it has go with it");
        publish_arm(ui, self, PUBLISH_ARM_DELETE, name);
        publish_prompt(ui, self, "Delete lane %s? %s; the base tree is not touched. Press Delete lane: sure? within 8 s",
            name, count);
        return 1;
    }
    publish_disarm(ui, self);
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, name);
    if( !publish_send(ui, TORIRSMAPED_REQ_LANE_DELETE, &payload) )
    {
        Editor_UiStatus(ui, "Delete lane refused: the editor server connection is gone");
        return 0;
    }
    self->action_pending = 1;
    publish_say(ui, self, "deleting the lane...", "", "");
    publish_refresh_widgets(ui, self);
    return 1;
}

int
Editor_Lanes_ChangeCommand(
    struct EditorUi* ui,
    enum Editor_LaneChangeCommand command)
{
    struct PublishPanel* self;
    int done;

    assert(ui);
    self = publish_show_tab(ui, PUBLISH_TAB_CHANGES);
    if( !self || !ui->editor )
    {
        Editor_UiStatus(ui, "Lanes: this window has no Lanes panel (no editor server)");
        return 0;
    }
    if( command == EDITOR_LANE_CHANGE_OPEN )
        done = publish_open_change(ui, self);
    else if( command == EDITOR_LANE_CHANGE_REVEAL )
        done = publish_reveal_change(ui, self);
    else if( command == EDITOR_LANE_CHANGE_DIFF )
        done = publish_diff_change(ui, self);
    else if( command == EDITOR_LANE_CHANGE_DISCARD )
    {
        done = publish_discard_change(ui, self);
        publish_refresh_widgets(ui, self);
    }
    else
    {
        assert(command == EDITOR_LANE_CHANGE_COPY_LIST);
        done = publish_copy_changes(ui, self);
    }
    return done;
}
