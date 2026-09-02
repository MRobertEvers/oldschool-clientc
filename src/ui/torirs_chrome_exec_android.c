/*
 * Android native-widget plugin chrome.
 *
 * The frame thread emits synchronous ToriRSChrome commands, while framework
 * Views may only be touched by Android's UI thread. This executor is the
 * transaction boundary between them: it copies every delta between SYNC_BEGIN
 * and SYNC_END, calls JNI once at the end, and never waits for Java to apply
 * it. ClientActivity posts that whole batch as one Runnable.
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
};

static struct AndroidChrome g_android_chrome = {
    .intent_lock = PTHREAD_MUTEX_INITIALIZER,
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
            PlatformAndroidJni_ApplyChromeBatch(chrome->commands, chrome->command_count);
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
    chrome->intent_count = 0;
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
    int kind, int panel, int widget, int value, char const* text)
{
    struct AndroidChrome* chrome = &g_android_chrome;
    struct ToriRSChromeIntent intent;
    int needed;

    if( kind < TORIRS_CHROME_INTENT_ACTIVATE || kind > TORIRS_CHROME_INTENT_CLOSE )
        return;
    memset(&intent, 0, sizeof(intent));
    intent.kind = kind;
    intent.panel = panel;
    intent.widget = widget;
    intent.value = value;
    android_copy_text(intent.text, (int)sizeof(intent.text), text);

    /* Like ToriRSChromeMirror_PushToggle and the web executor: changing the
     * boolean result also activates the row, which is how plugin enablement
     * and callbacks run after the model accepts the new checked value. Reserve
     * both slots before writing either so the pair is atomic. */
    needed = kind == TORIRS_CHROME_INTENT_TOGGLE ? 2 : 1;
    pthread_mutex_lock(&chrome->intent_lock);
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

struct ToriRSChromeExec
ToriRSChromeExec_Android(void* platform)
{
    struct ToriRSChromeExec exec;

    (void)platform; /* The Activity is process-global; no ANativeWindow owns it. */
    memset(&exec, 0, sizeof(exec));
    exec.user = &g_android_chrome;
    exec.begin = android_chrome_begin;
    exec.apply = android_chrome_apply;
    exec.end = android_chrome_end;
    exec.poll = android_chrome_poll;
    return exec;
}
