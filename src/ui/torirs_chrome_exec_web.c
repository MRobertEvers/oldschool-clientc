/*
 * The web chrome executor: the plugin window as real DOM controls.
 *
 * A NATIVE-WIDGET executor (see the two kinds in torirs_chrome_exec.h): the
 * chrome's display list is not used at all, because an <input> cannot be
 * reconstructed from rectangles. What crosses is the command stream, and the
 * page turns each command into a DOM node.
 *
 * C ASKS, THE PAGE OWNS THE DOM. Every crossing is an EM_JS call onto a
 * `window.torirsChrome*` hook, exactly as web_editor_open_panel_tab already
 * asks for a panel tab. A page that defines no hooks leaves begin() returning
 * false and the plugin window falls back to in-canvas chrome -- so a stale
 * index.html degrades rather than breaking the client.
 *
 * NOTHING HERE WAITS ON THE PAGE. The commands go out as fire-and-forget calls
 * and the intents are pulled from a queue the page fills; the renderer never
 * blocks on the DOM. That is the channel's own state rule
 * (src/web/torirs_channel.js: "nothing may make the renderer wait on a panel")
 * applied to a second consumer of it.
 *
 * Compiled only in the web lane; TORIRS_CHROME_EXEC_WEB_AVAILABLE tells the
 * chooser it is here.
 */

#include "torirs_chrome_exec.h"
#include "torirs_chrome_mirror.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
/* So the file still compiles natively for a syntax check, exactly as
 * dat2_web_store.c and platform_x_io_web.c already do. Every EM_JS body below
 * is then dead code that never runs. */
#define EM_JS(ret, name, args, ...) static ret name args { return (ret)0; }
#define EM_JS_VOID(name, args, ...) static void name args {}
#endif

/* ---- the page's side of the wall ----------------------------------------- */

/**
 * Is the page able to host the window at all?
 *
 * Asked once, in begin(), rather than assumed: the client and the page are
 * versioned separately -- a user with a cached index.html can be running last
 * week's hooks against this week's wasm -- and the honest answer to "these
 * hooks do not exist" is in-canvas chrome, not a window that silently does
 * nothing.
 */
EM_JS(int, web_chrome_available, (void), {
    return (typeof window.torirsChromeOpen === 'function' &&
            typeof window.torirsChromeApply === 'function') ? 1 : 0;
});

EM_JS(int, web_chrome_open, (void), {
    try
    {
        return window.torirsChromeOpen() ? 1 : 0;
    }
    catch( e )
    {
        console.warn('[torirs] chrome open failed', e);
        return 0;
    }
});

EM_JS(void, web_chrome_close, (void), {
    if( typeof window.torirsChromeClose === 'function' )
        window.torirsChromeClose();
});

/**
 * One command, as JSON.
 *
 * JSON rather than the channel's packed CmdBus frames, deliberately. The
 * channel's format exists so a frame produced in the page can be handed
 * straight to the wasm bus; this direction has no such requirement, the volume
 * is a handful of commands when a tab is built and none at all on a quiet
 * frame, and a page that can be debugged by reading its console beats one
 * whose messages have to be unpacked first.
 */
EM_JS(void, web_chrome_apply, (char const* json), {
    try
    {
        window.torirsChromeApply(JSON.parse(UTF8ToString(json)));
    }
    catch( e )
    {
        console.warn('[torirs] chrome apply failed', e);
    }
});

/**
 * Take one queued intent as JSON, or "" when the queue is empty.
 *
 * Pulled one at a time rather than as a batch because the page's queue is
 * short by construction -- it is what a user did since the last frame -- and a
 * batch would need a second buffer on each side of the wall to hold it.
 */
EM_JS(char*, web_chrome_take_intent, (void), {
    if( typeof window.torirsChromeTakeIntent !== 'function' )
        return stringToNewUTF8('');
    var s = '';
    try
    {
        s = window.torirsChromeTakeIntent() || '';
    }
    catch( e )
    {
        console.warn('[torirs] chrome intent failed', e);
    }
    return stringToNewUTF8(s);
});

/* ---- the executor -------------------------------------------------------- */

struct ChromeWeb
{
    int open;
    struct ToriRSChromeMirror mirror;
    /** One command's JSON, reused. Sized for the longest command: two 64-byte
     *  strings plus the fixed fields and their escaping. */
    char json[512];
};

static struct ChromeWeb g_chrome_web;

static int
chrome_web_begin(void* user)
{
    struct ChromeWeb* s = user;

    assert(s);
    if( !web_chrome_available() )
    {
        fprintf(stderr, "chrome: the page defines no torirsChrome* hooks\n");
        return 0;
    }
    if( !web_chrome_open() )
        return 0;
    ToriRSChromeMirror_Init(&s->mirror);
    s->open = 1;
    return 1;
}

static void
chrome_web_end(void* user)
{
    struct ChromeWeb* s = user;

    assert(s);
    if( !s->open )
        return;
    web_chrome_close();
    s->open = 0;
}

/** JSON-escape into `dst`; only the two characters a chrome string can carry
 *  that JSON cannot. Labels and values are plain bytes from a config store or
 *  a plugin, so there is no wider grammar to defend against here. */
static void
chrome_web_escape(char* dst, int cap, char const* src)
{
    int o = 0;
    if( !src )
        src = "";
    for( int i = 0; src[i] && o < cap - 2; i++ )
    {
        if( src[i] == '"' || src[i] == '\\' )
            dst[o++] = '\\';
        dst[o++] = src[i];
    }
    dst[o] = '\0';
}

static void
chrome_web_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ChromeWeb* s = user;
    char label[TORIRS_CHROME_LABEL_MAX * 2];
    char text[TORIRS_CHROME_TEXT_MAX * 2];

    assert(s);
    assert(cmd);
    if( !s->open )
        return;

    /* The mirror first, so the page's command and this executor's idea of what
     * exists cannot disagree about a handle that was just recycled. */
    ToriRSChromeMirror_Apply(&s->mirror, cmd);

    /* The sync brackets are for an executor that batches; the DOM does its own
     * batching in the layout pass, so sending them would be two messages a
     * frame that the page would only ignore. */
    if( cmd->kind == TORIRS_CHROME_CMD_SYNC_BEGIN || cmd->kind == TORIRS_CHROME_CMD_SYNC_END )
        return;

    chrome_web_escape(label, (int)sizeof(label), cmd->label);
    chrome_web_escape(text, (int)sizeof(text), cmd->text);
    snprintf(
        s->json,
        sizeof(s->json),
        "{\"k\":%d,\"p\":%d,\"w\":%d,\"tab\":%d,\"v\":%d,\"c\":%u,"
        "\"x\":%d,\"y\":%d,\"cw\":%d,\"ch\":%d,\"label\":\"%s\",\"text\":\"%s\"}",
        cmd->kind,
        cmd->panel,
        cmd->widget,
        cmd->tab,
        cmd->value,
        (unsigned)cmd->color,
        cmd->x,
        cmd->y,
        cmd->w,
        cmd->h,
        label,
        text);
    web_chrome_apply(s->json);
}

/**
 * Parse one integer field out of the page's intent JSON.
 *
 * A three-field hand parser rather than a JSON library, because the shape is
 * fixed and produced by code in this same repository: the page emits exactly
 * {"k":..,"p":..,"w":..,"v":..,"text":".."} and nothing else is accepted.
 * Anything unrecognised yields the default and the intent is dropped, which is
 * the right answer for a message this side did not write.
 */
static int
chrome_web_int(char const* json, char const* key, int fallback)
{
    char pattern[16];
    char const* at;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    at = strstr(json, pattern);
    if( !at )
        return fallback;
    at += strlen(pattern);
    return (int)strtol(at, NULL, 10);
}

static void
chrome_web_str(char const* json, char const* key, char* out, int cap)
{
    char pattern[16];
    char const* at;
    int o = 0;

    out[0] = '\0';
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    at = strstr(json, pattern);
    if( !at )
        return;
    at += strlen(pattern);
    while( *at && *at != '"' && o < cap - 1 )
    {
        if( *at == '\\' && at[1] )
            at++;
        out[o++] = *at++;
    }
    out[o] = '\0';
}

static int
chrome_web_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ChromeWeb* s = user;

    assert(s);
    assert(out);
    if( !s->open )
        return 0;

    /* Drain the page into the mirror's queue, then hand that out: the mirror
     * already owns the shuffle-the-tail-down behaviour a partial drain needs,
     * and duplicating it here would be a second place to lose a click. */
    for( int guard = 0; guard < TORIRS_CHROME_MIRROR_INTENTS; guard++ )
    {
        char* json = web_chrome_take_intent();
        struct ToriRSChromeIntent intent;

        if( !json )
            break;
        if( !json[0] )
        {
            free(json);
            break;
        }

        memset(&intent, 0, sizeof(intent));
        intent.kind = chrome_web_int(json, "k", 0);
        intent.panel = chrome_web_int(json, "p", -1);
        intent.widget = chrome_web_int(json, "w", -1);
        intent.value = chrome_web_int(json, "v", 0);
        chrome_web_str(json, "text", intent.text, (int)sizeof(intent.text));
        free(json);

        /* A widget the mirror does not know is a message from a stale page --
         * one that kept a node after a REMOVE. Dropped rather than forwarded:
         * applying it would mutate whatever recycled that handle. */
        if( intent.kind <= 0 )
            continue;
        if( intent.widget >= 0 && !ToriRSChromeMirror_Widget(&s->mirror, intent.widget) )
            continue;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);

        /* A toggle is also an activation, so the host's TakeActivated drain
         * runs -- the same pairing the mirror's PushToggle makes. */
        if( intent.kind == TORIRS_CHROME_INTENT_TOGGLE )
            ToriRSChromeMirror_PushActivate(&s->mirror, intent.panel, intent.widget);
    }

    return ToriRSChromeMirror_Poll(&s->mirror, out, max);
}

struct ToriRSChromeExec
ToriRSChromeExec_Web(void)
{
    struct ToriRSChromeExec exec;

    memset(&exec, 0, sizeof(exec));
    memset(&g_chrome_web, 0, sizeof(g_chrome_web));

    exec.user = &g_chrome_web;
    exec.begin = chrome_web_begin;
    exec.apply = chrome_web_apply;
    exec.end = chrome_web_end;
    exec.poll = chrome_web_poll;
    /* Not a surface executor: no present, no surface_input. The DOM holds the
     * widgets, so there is no display list to place. */
    return exec;
}
