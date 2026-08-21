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
#include "torirs_chrome_metrics.h"
#include "torirs_chrome_mirror.h"
#include "torirs_chrome_skin.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ---- the skin ------------------------------------------------------------
 *
 * The page draws the same chrome the game does, out of the same baked images:
 * the tradebacking behind the panel and every field, the interfaces' 17x17
 * tick and cross for a boolean, the scrollbar's own arrows and grip, and the
 * nine-slice panel frame. It is the CS2 executor's look, rebuilt out of DOM
 * nodes instead of interface components.
 *
 * WHY THE PIXELS CROSS AT ALL. Every other way for the page to get them is
 * worse. Shipping the images beside index.html means a second copy of the bake
 * that can go stale, and a lane that changed cache would serve last cache's
 * art; asking the cache at runtime is the exact failure the bake exists to
 * remove (see the note in torirs_chrome_exec_cs2.c). The wasm already holds
 * them in .rdata, so it hands them over.
 *
 * RAW RGBA, NOT PNG. The page turns the bytes into an ImageData, puts them on
 * a canvas and takes a data: URL back out -- three lines of platform API
 * against a PNG encoder in C that would exist for this alone. It costs about
 * 70KB of base64 ONCE, at the frame the window is first opened, and nothing
 * per frame after.
 *
 * The metrics ride the same crossing, so the page lays its rows out on the
 * numbers in torirs_chrome_metrics.h rather than on a second set spelled in
 * CSS -- which is what the header exists to prevent.
 */

EM_JS(void, web_chrome_skin_metrics, (char const* json), {
    if( typeof window.torirsChromeSkinMetrics === 'function' )
        window.torirsChromeSkinMetrics(JSON.parse(UTF8ToString(json)));
});

EM_JS(void, web_chrome_skin_sprite, (int slot, int w, int h, char const* b64), {
    if( typeof window.torirsChromeSkinSprite === 'function' )
        window.torirsChromeSkinSprite(slot, w, h, UTF8ToString(b64));
});

EM_JS(void, web_chrome_skin_done, (void), {
    if( typeof window.torirsChromeSkinDone === 'function' )
        window.torirsChromeSkinDone();
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

/* ---- handing the skin over ------------------------------------------------ */

/**
 * The slots the PAGE draws with.
 *
 * Not every baked slot: the wrench is the sidebar launcher's, which the game
 * canvas draws and this window never does, and the frame's centre piece is
 * baked but deliberately never drawn (the tile is already under it -- see
 * dbg_push_frame). Sending them would be two more base64 blobs across the wall
 * for images nothing asks for.
 */
static int const k_web_skin_slots[] = {
    TORIRS_CHROME_SKIN_PANEL_BODY,
    TORIRS_CHROME_SKIN_DROPDOWN_BODY,
    TORIRS_CHROME_SKIN_CHECK_ON,
    TORIRS_CHROME_SKIN_CHECK_OFF,
    TORIRS_CHROME_SKIN_SCROLL_UP,
    TORIRS_CHROME_SKIN_SCROLL_DOWN,
    TORIRS_CHROME_SKIN_SCROLL_TRACK,
    TORIRS_CHROME_SKIN_SCROLL_GRIP_TOP,
    TORIRS_CHROME_SKIN_SCROLL_GRIP_MID,
    TORIRS_CHROME_SKIN_SCROLL_GRIP_BOTTOM,
    TORIRS_CHROME_SKIN_FRAME_TOP_LEFT,
    TORIRS_CHROME_SKIN_FRAME_TOP,
    TORIRS_CHROME_SKIN_FRAME_TOP_RIGHT,
    TORIRS_CHROME_SKIN_FRAME_LEFT,
    TORIRS_CHROME_SKIN_FRAME_RIGHT,
    TORIRS_CHROME_SKIN_FRAME_BOTTOM_LEFT,
    TORIRS_CHROME_SKIN_FRAME_BOTTOM,
    TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT,
};

/**
 * One sprite as base64 RGBA, in the byte order ImageData wants.
 *
 * The bake is 0xAARRGGBB host-endian words; a canvas wants R,G,B,A bytes. The
 * shuffle is here rather than in the page because it is a property of the
 * BAKE's format, and a page reading the words directly would be right only on
 * a little-endian machine that happened to agree.
 */
static char*
chrome_web_sprite_b64(struct ToriRSChromeSkin_Sprite const* spr)
{
    static char const* const k_alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    long const bytes = (long)spr->w * (long)spr->h * 4;
    char* out = malloc((size_t)((bytes + 2) / 3) * 4 + 1);
    unsigned char triple[3];
    long o = 0;
    int t = 0;

    assert(out);
    /* Three bytes to four, no line breaks: a data: URL takes none and the page
     * never reads this as text. `bytes` is a whole number of pixels times
     * four, so the tail is always empty -- the padding branches are still
     * written, because a decoder is entitled to a well-formed string and a
     * sprite with an odd byte count would otherwise produce a silent one. */
    for( long i = 0; i < bytes; i++ )
    {
        uint32_t const p = spr->argb[i / 4];
        switch( i % 4 )
        {
        case 0:
            triple[t++] = (unsigned char)((p >> 16) & 0xFF); /* R */
            break;
        case 1:
            triple[t++] = (unsigned char)((p >> 8) & 0xFF); /* G */
            break;
        case 2:
            triple[t++] = (unsigned char)(p & 0xFF); /* B */
            break;
        default:
            triple[t++] = (unsigned char)((p >> 24) & 0xFF); /* A */
            break;
        }
        if( t < 3 )
            continue;
        out[o++] = k_alphabet[triple[0] >> 2];
        out[o++] = k_alphabet[((triple[0] & 0x03) << 4) | (triple[1] >> 4)];
        out[o++] = k_alphabet[((triple[1] & 0x0F) << 2) | (triple[2] >> 6)];
        out[o++] = k_alphabet[triple[2] & 0x3F];
        t = 0;
    }
    if( t == 1 )
    {
        out[o++] = k_alphabet[triple[0] >> 2];
        out[o++] = k_alphabet[(triple[0] & 0x03) << 4];
        out[o++] = '=';
        out[o++] = '=';
    }
    else if( t == 2 )
    {
        out[o++] = k_alphabet[triple[0] >> 2];
        out[o++] = k_alphabet[((triple[0] & 0x03) << 4) | (triple[1] >> 4)];
        out[o++] = k_alphabet[(triple[1] & 0x0F) << 2];
        out[o++] = '=';
    }
    out[o] = '\0';
    return out;
}

/**
 * Hand the page the metrics and the images, once, at open.
 *
 * A build with no baked skin sends the metrics and no sprites, and the page
 * keeps its flat fallback stylesheet -- the same degradation every other
 * consumer of the skin already has, and the reason the page's base sheet is
 * complete on its own rather than assuming the overrides arrive.
 */
static void
chrome_web_push_skin(void)
{
    char json[512];

    snprintf(
        json,
        sizeof(json),
        "{\"pad\":%d,\"rowH\":%d,\"rowGap\":%d,\"labelW\":%d,\"box\":%d,"
        "\"checkGap\":%d,\"toggleW\":%d,\"toggleH\":%d,\"rowIcon\":%d,"
        "\"rowIconGap\":%d,\"rowNameGap\":%d,\"dot\":%d,\"dotPitch\":%d,"
        "\"dotInset\":%d,\"scrollW\":%d,\"swatch\":%d,\"swatchGap\":%d,"
        "\"frame\":%d,\"tabH\":%d,\"tabPadX\":%d,\"fieldPadX\":%d,"
        "\"fieldInset\":%d,\"dropArrow\":%d}",
        TORIRS_CHROME_M_PAD,
        TORIRS_CHROME_M_ROW_H,
        TORIRS_CHROME_M_ROW_GAP,
        TORIRS_CHROME_M_LABEL_W,
        TORIRS_CHROME_M_BOX,
        TORIRS_CHROME_M_CHECK_GAP,
        TORIRS_CHROME_M_TOGGLE_W,
        TORIRS_CHROME_M_TOGGLE_H,
        TORIRS_CHROME_M_ROW_ICON,
        TORIRS_CHROME_M_ROW_ICON_GAP,
        TORIRS_CHROME_M_ROW_NAME_GAP,
        TORIRS_CHROME_M_DOT,
        TORIRS_CHROME_M_DOT_PITCH,
        TORIRS_CHROME_M_DOT_INSET,
        TORIRS_CHROME_M_SCROLL_W,
        TORIRS_CHROME_M_SWATCH,
        TORIRS_CHROME_M_SWATCH_GAP,
        TORIRS_CHROME_M_FRAME,
        TORIRS_CHROME_M_TAB_H,
        TORIRS_CHROME_M_TAB_PAD_X,
        TORIRS_CHROME_M_FIELD_PAD_X,
        TORIRS_CHROME_M_FIELD_INSET,
        TORIRS_CHROME_M_DROP_ARROW);
    web_chrome_skin_metrics(json);

    for( int i = 0; i < (int)(sizeof(k_web_skin_slots) / sizeof(k_web_skin_slots[0])); i++ )
    {
        int const slot = k_web_skin_slots[i];
        struct ToriRSChromeSkin_Sprite const* spr = ToriRSChromeSkin_ForSlot(slot);
        char* b64;

        if( !spr || spr->w <= 0 || spr->h <= 0 )
            continue;
        b64 = chrome_web_sprite_b64(spr);
        web_chrome_skin_sprite(slot, spr->w, spr->h, b64);
        free(b64);
    }
    web_chrome_skin_done();
}

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
    /* After open(), because the page builds its root there and the stylesheet
     * these produce is scoped to it -- and before any command, so the first
     * row that arrives is already laid out on the real metrics rather than
     * reflowed a frame later. */
    chrome_web_push_skin();
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
