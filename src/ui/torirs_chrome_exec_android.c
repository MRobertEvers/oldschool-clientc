/*
 * Android packaged-WebView plugin chrome.
 *
 * The frame thread emits synchronous ToriRSChrome commands, while framework
 * Views may only be touched by Android's UI thread. The UI thread applies the
 * command stream to one canonical local HTML bundle;
 * no plugin supplies HTML or script and no network navigation is permitted.
 *
 * User actions travel the other direction through a bounded, mutex-protected
 * queue. The UI thread only copies a result-shaped intent into it; poll drains
 * it on the frame thread, which is the only thread allowed to mutate the
 * authoritative chrome model or invoke a plugin.
 *
 * Exactly one process-global instance exists because Android has one Activity
 * presenter and the product contract permits one selected plugin page. No
 * jobject, View or JNI environment leaks into this file.
 */

#include "torirs_chrome_exec.h"

#include "platform/platform_android.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* A full snapshot is normally below 2,500 commands (384 widgets plus their
 * properties). Leave room for large dropdown palettes while keeping the queue
 * strictly bounded. An overflow drops the WHOLE transaction; a partial native
 * form would be worse than the previous complete one. */
#define ANDROID_CHROME_COMMAND_MAX 8192
#define ANDROID_CHROME_INTENT_MAX 64
#define ANDROID_CHROME_RAIL_INTENT_MAX 32
#define ANDROID_CHROME_CUSTOM_SIDE_MAX 4096
#define ANDROID_CHROME_CUSTOM_PIXEL_MAX 2000000U

struct AndroidChrome
{
    struct ToriRSChromeCmd commands[ANDROID_CHROME_COMMAND_MAX];
    int command_count;
    int collecting;
    int command_overflow;
    pthread_mutex_t intent_lock;
    struct ToriRSChromeIntent intents[ANDROID_CHROME_INTENT_MAX];
    int intent_count;
    int intent_overflow;
    int begun;
    int active_panel;
    uint32_t rail_page_generation;
    int widget_panel[TORIRS_CHROME_MAX_WIDGETS];
    uint32_t widget_generation[TORIRS_CHROME_MAX_WIDGETS];
    uint32_t widget_serial[TORIRS_CHROME_MAX_WIDGETS];
    int custom_width[TORIRS_CHROME_MAX_WIDGETS];
    int custom_height[TORIRS_CHROME_MAX_WIDGETS];
    struct ToriRSChromeRailIntent rail_intents[ANDROID_CHROME_RAIL_INTENT_MAX];
    int rail_intent_count;
    int rail_intent_overflow;
    struct ToriRSChromeRailIntent rail_layout;
    int rail_layout_pending;
    uint64_t rail_sequence;
    /** -1 = no rail action pending, otherwise the requested expanded state.
     * Kept across end(), because the collapsed rail outlives the executor. */
    int expanded_request;
};

static struct AndroidChrome g_android_chrome = {
    .intent_lock = PTHREAD_MUTEX_INITIALIZER,
    .active_panel = -1,
    .expanded_request = -1,
};

static void
android_copy_text(char* out, int capacity, char const* text)
{
    int i = 0;

    if( text )
        for( ; i < capacity - 1 && text[i]; i++ )
            out[i] = text[i];
    out[i] = '\0';
}

static int
android_chrome_begin(void* user)
{
    struct AndroidChrome* chrome = user;

    if( !PlatformAndroidJni_ChromeAvailable() )
        return 0;
    chrome->command_count = 0;
    chrome->collecting = 0;
    chrome->command_overflow = 0;
    pthread_mutex_lock(&chrome->intent_lock);
    chrome->intent_count = 0;
    chrome->intent_overflow = 0;
    chrome->begun = 1;
    pthread_mutex_unlock(&chrome->intent_lock);
    return 1;
}

/** Commit the event identity map only for a transaction Java will actually
 * receive. In particular, an over-capacity transaction is dropped whole and
 * must not make a not-rendered widget callable through a stale DOM node. */
static void
android_chrome_commit_identities(struct AndroidChrome* chrome)
{
    pthread_mutex_lock(&chrome->intent_lock);
    for( int at = 0; at < chrome->command_count; at++ )
    {
        struct ToriRSChromeCmd const* command = &chrome->commands[at];

        if( command->kind == TORIRS_CHROME_CMD_WIDGET_ADD ||
            command->kind == TORIRS_CHROME_CMD_WIDGET_REMOVE )
        {
            int const widget = command->widget;
            if( widget < 0 || widget >= TORIRS_CHROME_MAX_WIDGETS )
                continue;
            chrome->widget_generation[widget] =
                command->kind == TORIRS_CHROME_CMD_WIDGET_ADD
                    ? chrome->rail_page_generation
                    : 0;
            chrome->widget_serial[widget] =
                command->kind == TORIRS_CHROME_CMD_WIDGET_ADD ? command->serial : 0;
            chrome->custom_width[widget] = 0;
            chrome->custom_height[widget] = 0;
            chrome->widget_panel[widget] =
                command->kind == TORIRS_CHROME_CMD_WIDGET_ADD ? command->panel : -1;
        }
        else if( command->kind == TORIRS_CHROME_CMD_PANEL_OPEN )
            chrome->active_panel = command->panel;
        else if( command->kind == TORIRS_CHROME_CMD_PANEL_CLOSE )
        {
            if( chrome->active_panel == command->panel )
                chrome->active_panel = -1;
            for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
                if( chrome->widget_panel[i] == command->panel )
                {
                    chrome->widget_generation[i] = 0;
                    chrome->widget_serial[i] = 0;
                    chrome->custom_width[i] = 0;
                    chrome->custom_height[i] = 0;
                    chrome->widget_panel[i] = -1;
                }
        }
    }
    pthread_mutex_unlock(&chrome->intent_lock);
}

static void
android_chrome_apply(void* user, struct ToriRSChromeCmd const* command)
{
    struct AndroidChrome* chrome = user;

    if( command->kind == TORIRS_CHROME_CMD_SYNC_BEGIN )
    {
        chrome->command_count = 0;
        chrome->command_overflow = 0;
        chrome->collecting = 1;
        return;
    }
    if( command->kind == TORIRS_CHROME_CMD_SYNC_END )
    {
        if( !chrome->collecting )
            return;
        chrome->collecting = 0;
        if( chrome->command_overflow )
        {
            fprintf(
                stderr,
                "chrome: Android transaction exceeded %d commands; dropped atomically\n",
                ANDROID_CHROME_COMMAND_MAX);
            return;
        }
        /* Quiet syncs contain only the markers. Do not enqueue sixty empty UI
         * Runnables per second when the retained model did not change. */
        if( chrome->command_count > 0 )
        {
            android_chrome_commit_identities(chrome);
            PlatformAndroidJni_ApplyChromeBatch(chrome->commands, chrome->command_count);
        }
        return;
    }
    if( !chrome->collecting || chrome->command_overflow )
        return;
    if( chrome->command_count >= ANDROID_CHROME_COMMAND_MAX )
    {
        chrome->command_overflow = 1;
        chrome->command_count = 0;
        return;
    }
    chrome->commands[chrome->command_count++] = *command;
}

static void
android_chrome_end(void* user)
{
    struct AndroidChrome* chrome = user;

    chrome->command_count = 0;
    chrome->collecting = 0;
    chrome->command_overflow = 0;
    pthread_mutex_lock(&chrome->intent_lock);
    chrome->begun = 0;
    chrome->active_panel = -1;
    chrome->intent_count = 0;
    memset(chrome->widget_generation, 0, sizeof(chrome->widget_generation));
    memset(chrome->widget_serial, 0, sizeof(chrome->widget_serial));
    memset(chrome->custom_width, 0, sizeof(chrome->custom_width));
    memset(chrome->custom_height, 0, sizeof(chrome->custom_height));
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        chrome->widget_panel[i] = -1;
    pthread_mutex_unlock(&chrome->intent_lock);
    /* Never under intent_lock: this crosses into Java, whose UI work may later
     * re-enter through PlatformAndroidChrome_PostIntent. */
    PlatformAndroidJni_EndChrome();
}

static int
android_chrome_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct AndroidChrome* chrome = user;
    int count;

    if( !out || max <= 0 )
        return 0;
    pthread_mutex_lock(&chrome->intent_lock);
    count = chrome->intent_count < max ? chrome->intent_count : max;
    for( int i = 0; i < count; i++ )
        out[i] = chrome->intents[i];
    for( int i = count; i < chrome->intent_count; i++ )
        chrome->intents[i - count] = chrome->intents[i];
    chrome->intent_count -= count;
    if( chrome->intent_overflow )
    {
        fprintf(stderr, "chrome: Android intent queue overflow; newest action dropped\n");
        chrome->intent_overflow = 0;
    }
    pthread_mutex_unlock(&chrome->intent_lock);
    return count;
}

void
PlatformAndroidChrome_PostIntent(
    int kind,
    int panel,
    int widget,
    int value,
    char const* text,
    int x,
    int y,
    uint32_t selection_generation,
    uint32_t widget_serial)
{
    struct AndroidChrome* chrome = &g_android_chrome;
    struct ToriRSChromeIntent intent;
    int needed;

    if( kind < TORIRS_CHROME_INTENT_ACTIVATE ||
        kind > TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE )
        return;
    memset(&intent, 0, sizeof(intent));
    intent.kind = kind;
    intent.panel = panel;
    intent.widget = widget;
    intent.value = value;
    intent.x = x;
    intent.y = y;
    intent.selection_generation = selection_generation;
    intent.widget_serial = widget_serial;
    android_copy_text(intent.text, (int)sizeof(intent.text), text);

    /* Like ToriRSChromeMirror_PushToggle and the web executor: changing the
     * boolean result also activates the row, which is how plugin enablement
     * and callbacks run after the model accepts the new checked value. Reserve
     * both slots before writing either so the pair is atomic. */
    needed = kind == TORIRS_CHROME_INTENT_TOGGLE ? 2 : 1;
    pthread_mutex_lock(&chrome->intent_lock);
    if( panel != chrome->active_panel ||
        selection_generation == 0 ||
        selection_generation != chrome->rail_page_generation ||
        (widget < 0 && kind != TORIRS_CHROME_INTENT_CLOSE) ||
        (widget >= 0 &&
         (widget >= TORIRS_CHROME_MAX_WIDGETS || widget_serial == 0 ||
          chrome->widget_panel[widget] != panel ||
          chrome->widget_generation[widget] != selection_generation ||
          chrome->widget_serial[widget] != widget_serial)) ||
        (kind == TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE &&
         (widget < 0 || x < 0 || y < 0 ||
          x >= chrome->custom_width[widget] || y >= chrome->custom_height[widget])) )
    {
        pthread_mutex_unlock(&chrome->intent_lock);
        return;
    }
    if( !chrome->begun || chrome->intent_count + needed > ANDROID_CHROME_INTENT_MAX )
    {
        if( chrome->begun )
            chrome->intent_overflow = 1;
        pthread_mutex_unlock(&chrome->intent_lock);
        return;
    }
    chrome->intents[chrome->intent_count++] = intent;
    if( kind == TORIRS_CHROME_INTENT_TOGGLE )
    {
        intent.kind = TORIRS_CHROME_INTENT_ACTIVATE;
        intent.value = 0;
        intent.text[0] = '\0';
        chrome->intents[chrome->intent_count++] = intent;
    }
    pthread_mutex_unlock(&chrome->intent_lock);
}

static void
android_chrome_custom_present(
    void* user, struct ToriRSChromeCustomFrame const* frame)
{
    struct AndroidChrome* chrome = user;
    int logical_w;
    int logical_h;
    uint64_t pixels;

    if( !chrome || !frame || frame->widget < 0 ||
        frame->widget >= TORIRS_CHROME_MAX_WIDGETS || !frame->argb ||
        frame->scale_milli <= 0 || frame->width <= 0 || frame->height <= 0 ||
        frame->width > ANDROID_CHROME_CUSTOM_SIDE_MAX ||
        frame->height > ANDROID_CHROME_CUSTOM_SIDE_MAX ||
        frame->stride != frame->width || frame->selection_generation == 0 ||
        frame->widget_serial == 0 )
        return;
    pixels = (uint64_t)(unsigned int)frame->width *
             (uint64_t)(unsigned int)frame->height;
    if( pixels > ANDROID_CHROME_CUSTOM_PIXEL_MAX )
        return;
    logical_w = (int)((int64_t)frame->width * 1000 / frame->scale_milli);
    logical_h = (int)((int64_t)frame->height * 1000 / frame->scale_milli);
    if( logical_w <= 0 || logical_h <= 0 )
        return;
    pthread_mutex_lock(&chrome->intent_lock);
    if( frame->panel != chrome->active_panel ||
        frame->selection_generation != chrome->rail_page_generation ||
        chrome->widget_panel[frame->widget] != frame->panel ||
        chrome->widget_generation[frame->widget] != frame->selection_generation ||
        chrome->widget_serial[frame->widget] != frame->widget_serial )
    {
        pthread_mutex_unlock(&chrome->intent_lock);
        return;
    }
    chrome->custom_width[frame->widget] = logical_w;
    chrome->custom_height[frame->widget] = logical_h;
    pthread_mutex_unlock(&chrome->intent_lock);
    PlatformAndroidJni_ApplyChromeCustom(frame);
}

void
PlatformAndroidChrome_RequestExpanded(int expanded)
{
    struct AndroidChrome* chrome = &g_android_chrome;

    pthread_mutex_lock(&chrome->intent_lock);
    chrome->expanded_request = expanded ? 1 : 0;
    pthread_mutex_unlock(&chrome->intent_lock);
}

int
PlatformAndroidChrome_TakeExpandedRequest(int* out_expanded)
{
    struct AndroidChrome* chrome = &g_android_chrome;
    int have;

    if( !out_expanded )
        return 0;
    pthread_mutex_lock(&chrome->intent_lock);
    have = chrome->expanded_request >= 0;
    if( have )
    {
        *out_expanded = chrome->expanded_request;
        chrome->expanded_request = -1;
    }
    pthread_mutex_unlock(&chrome->intent_lock);
    return have;
}

static uint64_t
android_rail_sequence_next(struct AndroidChrome* chrome)
{
    chrome->rail_sequence++;
    if( chrome->rail_sequence == 0 )
        chrome->rail_sequence++;
    return chrome->rail_sequence;
}

void
PlatformAndroidChrome_PostRailSelect(
    int plugin_index, uint32_t selection_generation)
{
    struct AndroidChrome* chrome = &g_android_chrome;
    struct ToriRSChromeRailIntent intent;

    if( plugin_index < -2 || plugin_index == -1 || selection_generation == 0 )
        return;
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_RAIL_INTENT_SELECT;
    intent.plugin_index = plugin_index;
    intent.selection_generation = selection_generation;

    pthread_mutex_lock(&chrome->intent_lock);
    intent.sequence = android_rail_sequence_next(chrome);
    if( chrome->rail_intent_count >= ANDROID_CHROME_RAIL_INTENT_MAX )
    {
        /* Coalesce the tail: bounded memory, while the most recent destination
         * remains the one the frame thread will ultimately select. */
        chrome->rail_intents[ANDROID_CHROME_RAIL_INTENT_MAX - 1] = intent;
        chrome->rail_intent_overflow = 1;
    }
    else
        chrome->rail_intents[chrome->rail_intent_count++] = intent;
    pthread_mutex_unlock(&chrome->intent_lock);
}

void
PlatformAndroidChrome_PostRailLayout(
    uint32_t selection_generation,
    int width,
    int height,
    int scale_milli,
    int size_class,
    int visible,
    int game_visible)
{
    struct AndroidChrome* chrome = &g_android_chrome;

    if( selection_generation == 0 || width < 0 || height < 0 || scale_milli <= 0 )
        return;
    pthread_mutex_lock(&chrome->intent_lock);
    memset(&chrome->rail_layout, 0, sizeof(chrome->rail_layout));
    chrome->rail_layout.kind = TORIRS_CHROME_RAIL_INTENT_LAYOUT;
    chrome->rail_layout.selection_generation = selection_generation;
    chrome->rail_layout.sequence = android_rail_sequence_next(chrome);
    chrome->rail_layout.width = width;
    chrome->rail_layout.height = height;
    chrome->rail_layout.scale_milli = scale_milli;
    chrome->rail_layout.size_class = size_class;
    chrome->rail_layout.visible = visible ? 1 : 0;
    chrome->rail_layout.game_visible = game_visible ? 1 : 0;
    /* Layout is state, not a history. Replacing the pending copy prevents a
     * resize storm from taking slots away from discrete selection gestures. */
    chrome->rail_layout_pending = 1;
    pthread_mutex_unlock(&chrome->intent_lock);
}

static void
android_chrome_rail_sync(
    void* user, struct ToriRSChromeRailSnapshot const* snapshot)
{
    struct AndroidChrome* chrome = user;

    if( !chrome || !snapshot )
        return;
    pthread_mutex_lock(&chrome->intent_lock);
    if( chrome->rail_page_generation != snapshot->page_generation )
    {
        chrome->rail_page_generation = snapshot->page_generation;
        chrome->active_panel = -1;
        chrome->intent_count = 0;
        chrome->intent_overflow = 0;
        memset(chrome->widget_generation, 0, sizeof(chrome->widget_generation));
        memset(chrome->widget_serial, 0, sizeof(chrome->widget_serial));
        memset(chrome->custom_width, 0, sizeof(chrome->custom_width));
        memset(chrome->custom_height, 0, sizeof(chrome->custom_height));
        for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
            chrome->widget_panel[i] = -1;
    }
    pthread_mutex_unlock(&chrome->intent_lock);
    PlatformAndroidJni_ApplyChromeRail(snapshot);
}

static void
android_chrome_rail_icon(void* user, struct ToriRSChromeRailIcon const* icon)
{
    (void)user;
    PlatformAndroidJni_ApplyChromeRailIcon(icon);
}

static int
android_chrome_rail_poll(
    void* user, struct ToriRSChromeRailIntent* out, int max)
{
    struct AndroidChrome* chrome = user;
    int count;

    if( !out || max <= 0 )
        return 0;
    pthread_mutex_lock(&chrome->intent_lock);
    count = chrome->rail_intent_count < max ? chrome->rail_intent_count : max;
    for( int i = 0; i < count; i++ )
        out[i] = chrome->rail_intents[i];
    for( int i = count; i < chrome->rail_intent_count; i++ )
        chrome->rail_intents[i - count] = chrome->rail_intents[i];
    chrome->rail_intent_count -= count;
    if( count < max && chrome->rail_layout_pending )
    {
        out[count++] = chrome->rail_layout;
        chrome->rail_layout_pending = 0;
    }
    if( chrome->rail_intent_overflow )
    {
        fprintf(stderr, "chrome: Android rail queue overflow; intermediate selection coalesced\n");
        chrome->rail_intent_overflow = 0;
    }
    pthread_mutex_unlock(&chrome->intent_lock);
    return count;
}

struct ToriRSChromeExec
ToriRSChromeExec_Android(
    void* platform, ToriRSChromeRasteriseFn rasterise, void* rasterise_user)
{
    struct ToriRSChromeExec exec;

    (void)platform; /* The Activity is process-global; no ANativeWindow owns it. */
    (void)rasterise;
    (void)rasterise_user;
    memset(&exec, 0, sizeof(exec));
    exec.user = &g_android_chrome;
    exec.begin = android_chrome_begin;
    exec.apply = android_chrome_apply;
    exec.end = android_chrome_end;
    exec.poll = android_chrome_poll;
    exec.rail_sync = android_chrome_rail_sync;
    exec.rail_icon = android_chrome_rail_icon;
    exec.rail_poll = android_chrome_rail_poll;
    exec.custom_present = android_chrome_custom_present;
    return exec;
}
