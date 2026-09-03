/*
 * The web chrome executor: fixed POD into the canonical retained DOM bundle.
 *
 * One of the two external web executors: the chrome's display list is not used
 * at all, because an <input> cannot be reconstructed from rectangles.
 * Commands cross to the application-owned web
 * adapter, which preserves SYNC transactions and publishes protocol-1
 * snapshots/deltas into one persistent canonical iframe.
 *
 * C ASKS, THE BUNDLE OWNS THE DOM. Every crossing is an EM_JS call onto a
 * `window.torirsChrome*` compatibility hook. A page that defines no hooks
 * leaves begin() returning false and the plugin window falls back to in-canvas
 * chrome -- so a stale index.html degrades rather than breaking the client.
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
#include "torirs_chrome_exec_web.h"
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
            typeof window.torirsChromeApplyBatch === 'function' &&
            typeof window.torirsChromeTakeDeliveryLoss === 'function' &&
            typeof window.torirsChromeTakeIntentOverflow === 'function') ? 1 : 0;
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

EM_JS(int, web_chrome_rail_sync, (char const* json), {
    if( typeof window.torirsChromeRailSync !== 'function' )
        return 0;
    try
    {
        return window.torirsChromeRailSync(
            JSON.parse(UTF8ToString(json))) === true ? 1 : 0;
    }
    catch( e )
    {
        console.warn('[torirs] plugin rail sync failed', e);
        return 0;
    }
});

EM_JS(int, web_chrome_rail_icon,
      (int plugin, unsigned revision, int w, int h, char const* b64), {
    if( typeof window.torirsChromeRailIcon !== 'function' )
        return 0;
    try
    {
        return window.torirsChromeRailIcon(
            plugin, revision, w, h, b64 ? UTF8ToString(b64) : "") === true ? 1 : 0;
    }
    catch( e )
    {
        console.warn('[torirs] plugin rail icon failed', e);
        return 0;
    }
});

/**
 * One complete retained transaction, as a JSON array of commands.
 *
 * One call rather than one per command: crossing Wasm -> JS and JSON.parse are
 * fixed overheads, while the retained transaction is already the atomic unit
 * the DOM consumes. A quiet frame calls neither this nor any compatibility
 * hook, and a one-property patch crosses once.
 */
EM_JS(int, web_chrome_apply_batch, (char const* json), {
    try
    {
        if( typeof window.torirsChromeApplyBatch !== 'function' )
            return 0;
        return window.torirsChromeApplyBatch(
            JSON.parse(UTF8ToString(json))) === true ? 1 : 0;
    }
    catch( e )
    {
        console.warn('[torirs] chrome apply failed', e);
        return 0;
    }
});

/* A queued transaction can be accepted before the iframe is ready and later
 * rejected when that runtime attaches. Poll the host's one-shot loss latch so
 * even an otherwise idle model replaces the missing transaction. */
EM_JS(int, web_chrome_take_delivery_loss, (void), {
    if( typeof window.torirsChromeTakeDeliveryLoss !== 'function' )
        return 0;
    try { return window.torirsChromeTakeDeliveryLoss() ? 1 : 0; }
    catch( e ) { return 1; }
});

/* A dirty custom well. The hook consumes and converts the call-scoped HEAPU32
 * view synchronously before the App reuses its raster scratch. */
EM_JS(int, web_chrome_custom_present,
    (int panel, int widget, unsigned generation, unsigned serial,
     int scale_milli, int width, int height, uint32_t const* argb), {
    if( typeof window.torirsChromeCustom !== 'function' ||
        width <= 0 || height <= 0 || scale_milli <= 0 )
        return 0;
    try
    {
        var count = width * height;
        var copy = HEAPU32.subarray(argb >> 2, (argb >> 2) + count);
        return window.torirsChromeCustom(
            panel, widget, generation >>> 0, serial >>> 0,
            scale_milli, width, height, copy) === true ? 1 : 0;
    }
    catch( e )
    {
        console.warn('[torirs] custom chrome frame failed', e);
        return 0;
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
        return stringToNewUTF8("");
    var s = "";
    try
    {
        s = window.torirsChromeTakeIntent() || "";
    }
    catch( e )
    {
        console.warn('[torirs] chrome intent failed', e);
    }
    return stringToNewUTF8(s);
});

EM_JS(int, web_chrome_take_intent_overflow, (void), {
    if( typeof window.torirsChromeTakeIntentOverflow !== 'function' )
        return 0;
    try { return window.torirsChromeTakeIntentOverflow() ? 1 : 0; }
    catch( e ) { return 1; }
});

/* ---- the executor -------------------------------------------------------- */

struct ChromeWeb
{
    int open;
    int active_panel;
    uint32_t page_generation;
    struct ToriRSChromeMirror mirror;
    /** One command's JSON, reused. Sized for the longest command: a label and
     *  a value, each of which can double under escaping, plus the fixed fields
     *  and their punctuation. */
    char json[2 * (TORIRS_CHROME_LABEL_MAX + 2 * TORIRS_CHROME_TEXT_MAX) + 320];
    /** Reused transaction envelope: grown on demand and retained across opens. */
    char* batch_json;
    size_t batch_len;
    size_t batch_cap;
    int batch_commands;
    int batch_failed;
    int collecting;
    int snapshot_needed;
    int rail_snapshot_needed;
    int custom_panel[TORIRS_CHROME_MAX_WIDGETS];
    uint32_t custom_generation[TORIRS_CHROME_MAX_WIDGETS];
    uint32_t custom_serial[TORIRS_CHROME_MAX_WIDGETS];
    int custom_width[TORIRS_CHROME_MAX_WIDGETS];
    int custom_height[TORIRS_CHROME_MAX_WIDGETS];
    /** Identity of every semantic widget, not only CUSTOM. A handle can be
     * recycled immediately after page replacement, so existence in the mirror
     * alone is not a sufficient stale-intent fence. */
    uint32_t widget_serial[TORIRS_CHROME_MAX_WIDGETS];
};

static struct ChromeWeb g_chrome_web;

#define WEB_CHROME_BATCH_MAX (8u * 1024u * 1024u)

#define WEB_CHROME_RAIL_INTENT_MAX 32
static struct ToriRSChromeRailIntent
    g_chrome_web_rail_intents[WEB_CHROME_RAIL_INTENT_MAX];
static int g_chrome_web_rail_intent_count;
static struct ToriRSChromeRailIntent g_chrome_web_rail_layout;
static int g_chrome_web_rail_layout_pending;
static uint64_t g_chrome_web_rail_sequence;

/*
 * Collapsed-rail requests outlive ChromeWeb::open: closing the pane shuts the
 * widget executor down, while the host-owned rail deliberately remains. Keep
 * this one-bit desired state outside g_chrome_web so a subsequent executor
 * bind cannot erase a click that the app frame has not consumed yet.
 */
static int g_chrome_web_open_request;
static int g_chrome_web_open_request_pending;

static void
chrome_web_reset_mounted(struct ChromeWeb* s)
{
    assert(s);
    ToriRSChromeMirror_Init(&s->mirror);
    s->active_panel = -1;
    memset(s->widget_serial, 0, sizeof(s->widget_serial));
    memset(s->custom_generation, 0, sizeof(s->custom_generation));
    memset(s->custom_serial, 0, sizeof(s->custom_serial));
    memset(s->custom_width, 0, sizeof(s->custom_width));
    memset(s->custom_height, 0, sizeof(s->custom_height));
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        s->custom_panel[i] = -1;
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void
ToriRSChromeExecWeb_RequestOpen(int open)
{
    g_chrome_web_open_request = open ? 1 : 0;
    g_chrome_web_open_request_pending = 1;
}

int
ToriRSChromeExecWeb_TakeOpenRequest(int* open)
{
    if( !open || !g_chrome_web_open_request_pending )
        return 0;
    *open = g_chrome_web_open_request;
    g_chrome_web_open_request_pending = 0;
    return 1;
}

static uint64_t
chrome_web_rail_sequence_next(void)
{
    g_chrome_web_rail_sequence++;
    if( g_chrome_web_rail_sequence == 0 )
        g_chrome_web_rail_sequence++;
    return g_chrome_web_rail_sequence;
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void
ToriRSChromeExecWeb_RequestSelect(
    int plugin_index, uint32_t selection_generation)
{
    struct ToriRSChromeRailIntent intent;

    if( plugin_index < -2 || plugin_index == -1 || selection_generation == 0 )
        return;
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_RAIL_INTENT_SELECT;
    intent.plugin_index = plugin_index;
    intent.selection_generation = selection_generation;
    intent.sequence = chrome_web_rail_sequence_next();
    if( g_chrome_web_rail_intent_count >= WEB_CHROME_RAIL_INTENT_MAX )
    {
        /* Keep the latest requested destination. The application deliberately
         * coalesces clicks from one displayed generation to this last event. */
        g_chrome_web_rail_intents[WEB_CHROME_RAIL_INTENT_MAX - 1] = intent;
        return;
    }
    g_chrome_web_rail_intents[g_chrome_web_rail_intent_count++] = intent;
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
void
ToriRSChromeExecWeb_RequestLayout(
    uint32_t selection_generation,
    int width,
    int height,
    int scale_milli,
    int size_class,
    int visible,
    int game_visible)
{
    if( selection_generation == 0 || width < 0 || height < 0 || scale_milli <= 0 )
        return;
    memset(&g_chrome_web_rail_layout, 0, sizeof(g_chrome_web_rail_layout));
    g_chrome_web_rail_layout.kind = TORIRS_CHROME_RAIL_INTENT_LAYOUT;
    g_chrome_web_rail_layout.selection_generation = selection_generation;
    g_chrome_web_rail_layout.sequence = chrome_web_rail_sequence_next();
    g_chrome_web_rail_layout.width = width;
    g_chrome_web_rail_layout.height = height;
    g_chrome_web_rail_layout.scale_milli = scale_milli;
    g_chrome_web_rail_layout.size_class = size_class;
    g_chrome_web_rail_layout.visible = visible ? 1 : 0;
    g_chrome_web_rail_layout.game_visible = game_visible ? 1 : 0;
    g_chrome_web_rail_layout_pending = 1;
}

/**
 * One authored rail icon as base64 RGBA, in the byte order ImageData wants.
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
    chrome_web_reset_mounted(s);
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
    s->collecting = 0;
    s->batch_len = 0;
    chrome_web_reset_mounted(s);
}

/**
 * JSON-escape into `dst`.
 *
 * The quote and the backslash, and the CONTROL BYTES -- which JSON forbids
 * inside a string literal outright, so one of them does not corrupt a value on
 * the far side, it makes JSON.parse throw and the whole command vanish. A
 * multiline field's value is full of '\n', which is exactly how that was
 * found: every panel holding one went silent the moment a line was typed.
 *
 * A control byte with no short escape is DROPPED rather than written as \u00XX.
 * Nothing this seam carries has a use for one -- the model's own KeyChar
 * refuses everything below 0x20 -- and a six-character escape is a length this
 * fixed buffer would have to budget for on every byte.
 */
static void
chrome_web_escape(char* dst, int cap, char const* src)
{
    int o = 0;
    if( !src )
        src = "";
    for( int i = 0; src[i] && o < cap - 2; i++ )
    {
        char esc = 0;
        switch( src[i] )
        {
        case '"':
        case '\\':
            esc = src[i];
            break;
        case '\n':
            esc = 'n';
            break;
        case '\r':
            esc = 'r';
            break;
        case '\t':
            esc = 't';
            break;
        default:
            break;
        }
        if( esc )
        {
            dst[o++] = '\\';
            dst[o++] = esc;
            continue;
        }
        if( (unsigned char)src[i] < 0x20 )
            continue;
        dst[o++] = src[i];
    }
    dst[o] = '\0';
}

static int
chrome_web_batch_reserve(struct ChromeWeb* s, size_t extra)
{
    size_t required;
    size_t capacity;
    char* grown;

    if( s->batch_failed || extra > SIZE_MAX - s->batch_len - 1 )
    {
        s->batch_failed = 1;
        return 0;
    }
    required = s->batch_len + extra + 1;
    if( required > WEB_CHROME_BATCH_MAX )
    {
        s->batch_failed = 1;
        return 0;
    }
    if( required <= s->batch_cap )
        return 1;
    capacity = s->batch_cap ? s->batch_cap : 4096;
    while( capacity < required )
    {
        if( capacity >= WEB_CHROME_BATCH_MAX / 2 )
        {
            capacity = WEB_CHROME_BATCH_MAX;
            break;
        }
        capacity *= 2;
    }
    if( capacity < required )
    {
        s->batch_failed = 1;
        return 0;
    }
    grown = realloc(s->batch_json, capacity);
    if( !grown )
    {
        s->batch_failed = 1;
        return 0;
    }
    s->batch_json = grown;
    s->batch_cap = capacity;
    return 1;
}

static int
chrome_web_batch_append(struct ChromeWeb* s, char const* text, size_t length)
{
    if( !chrome_web_batch_reserve(s, length) )
        return 0;
    memcpy(s->batch_json + s->batch_len, text, length);
    s->batch_len += length;
    s->batch_json[s->batch_len] = '\0';
    return 1;
}

static void
chrome_web_batch_begin(struct ChromeWeb* s)
{
    s->batch_len = 0;
    s->batch_commands = 0;
    s->batch_failed = 0;
    s->collecting = 1;
    (void)chrome_web_batch_append(s, "[", 1);
}

static void
chrome_web_batch_command(
    struct ChromeWeb* s, struct ToriRSChromeCmd const* cmd)
{
    char label[TORIRS_CHROME_LABEL_MAX * 2 + 1];
    char text[TORIRS_CHROME_TEXT_MAX * 2 + 1];
    char detail[TORIRS_CHROME_TEXT_MAX * 2 + 1];
    int length;

    if( !s->collecting || s->batch_failed )
        return;
    if( s->batch_commands >= TORIRS_CHROME_PROTOCOL_COMMAND_MAX )
    {
        s->batch_failed = 1;
        return;
    }
    chrome_web_escape(label, (int)sizeof(label), cmd->label);
    chrome_web_escape(text, (int)sizeof(text), cmd->text);
    chrome_web_escape(detail, (int)sizeof(detail), cmd->detail);
    length = snprintf(
        s->json,
        sizeof(s->json),
        "%s{\"k\":%d,\"p\":%d,\"w\":%d,\"tab\":%d,\"v\":%d,\"c\":%u,"
        "\"x\":%d,\"y\":%d,\"cw\":%d,\"ch\":%d,\"s\":%u,"
        "\"label\":\"%s\",\"text\":\"%s\",\"detail\":\"%s\"}",
        s->batch_commands ? "," : "",
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
        (unsigned)cmd->serial,
        label,
        text,
        detail);
    if( length < 0 || (size_t)length >= sizeof(s->json) )
    {
        s->batch_failed = 1;
        return;
    }
    if( chrome_web_batch_append(s, s->json, (size_t)length) )
        s->batch_commands++;
}

static void
chrome_web_batch_end(struct ChromeWeb* s)
{
    int delivered = 0;

    if( !s->collecting )
        return;
    if( !s->batch_failed && chrome_web_batch_append(s, "]", 1) )
        delivered = web_chrome_apply_batch(s->batch_json);
    if( !delivered )
    {
        s->snapshot_needed = 1;
        /* Commands were folded into the local lifecycle mirror while the
         * transaction was assembled. They are no more delivered than Sync's
         * shadow is, so invalidate returned intents until the retry snapshot. */
        chrome_web_reset_mounted(s);
        fprintf(stderr, "chrome: web transaction rejected; requesting snapshot\n");
    }
    s->collecting = 0;
    s->batch_len = 0;
    s->batch_commands = 0;
    s->batch_failed = 0;
}

static int
chrome_web_rail_sync(
    void* user, struct ToriRSChromeRailSnapshot const* snapshot)
{
    struct ChromeWeb* s = user;
    /* Manage plus all 32 plugin entries, with every byte doubled by JSON
     * escaping, fit below 16 KiB.
     * Keep extra headroom so a future scalar does not turn a complete snapshot
     * into a prefix the page could mistake for truth. */
    static char json[32768];
    int at;
    int count;

    if( !s || !snapshot )
        return 0;
    count = snapshot->entry_count;
    if( count < 0 )
        count = 0;
    if( count > TORIRS_CHROME_RAIL_ENTRY_MAX )
        count = TORIRS_CHROME_RAIL_ENTRY_MAX;
    at = snprintf(
        json,
        sizeof(json),
        "{\"protocol\":1,\"type\":\"rail.snapshot\","
        "\"registryRevision\":%u,\"selectionGeneration\":%u,"
        "\"pageGeneration\":%u,\"activePlugin\":%d,"
        "\"lastSelectedPlugin\":%d,\"selectedEntry\":%d,"
        "\"expanded\":%s,\"entries\":[",
        (unsigned)snapshot->registry_revision,
        (unsigned)snapshot->selection_generation,
        (unsigned)snapshot->page_generation,
        snapshot->active_plugin,
        snapshot->last_selected_plugin,
        snapshot->selected_entry,
        snapshot->expanded ? "true" : "false");
    if( at < 0 || at >= (int)sizeof(json) )
        return 0;
    for( int i = 0; i < count; i++ )
    {
        struct ToriRSChromeRailEntry const* entry = &snapshot->entries[i];
        char title[TORIRS_CHROME_RAIL_TITLE_MAX * 2 + 1];
        char icon[TORIRS_CHROME_RAIL_ICON_MAX * 2 + 1];
        char badge[TORIRS_CHROME_RAIL_BADGE_MAX * 2 + 1];
        int n;

        chrome_web_escape(title, sizeof(title), entry->title);
        chrome_web_escape(icon, sizeof(icon), entry->icon_asset);
        chrome_web_escape(badge, sizeof(badge), entry->badge);
        n = snprintf(
            json + at,
            sizeof(json) - (size_t)at,
            "%s{\"kind\":%d,\"pluginIndex\":%d,\"preferredWidth\":%d,"
            "\"attention\":%s,\"title\":\"%s\","
            "\"iconAsset\":\"%s\",\"badge\":\"%s\"}",
            i ? "," : "",
            entry->kind,
            entry->plugin_index,
            entry->preferred_width,
            entry->attention ? "true" : "false",
            title,
            icon,
            badge);
        if( n < 0 || n >= (int)(sizeof(json) - (size_t)at) )
            return 0;
        at += n;
    }
    if( at + 3 > (int)sizeof(json) )
        return 0;
    json[at++] = ']';
    json[at++] = '}';
    json[at] = '\0';
    if( !web_chrome_rail_sync(json) )
        return 0;
    if( s->page_generation != snapshot->page_generation )
        chrome_web_reset_mounted(s);
    s->page_generation = snapshot->page_generation;
    return 1;
}

static int
chrome_web_rail_icon(void* user, struct ToriRSChromeRailIcon const* icon)
{
    char* b64 = NULL;

    (void)user;
    if( !icon )
        return 0;
    if( icon->width > 0 && icon->height > 0 )
    {
        struct ToriRSChromeSkin_Sprite sprite;
        sprite.w = icon->width;
        sprite.h = icon->height;
        sprite.argb = icon->argb;
        b64 = chrome_web_sprite_b64(&sprite);
        if( !b64 )
            return 0;
    }
    {
        int const sent = web_chrome_rail_icon(
        icon->plugin_index,
        (unsigned)icon->revision,
        icon->width,
        icon->height,
        b64 ? b64 : "");
        free(b64);
        return sent;
    }
}

static int
chrome_web_rail_poll(
    void* user, struct ToriRSChromeRailIntent* out, int max)
{
    int count;

    (void)user;
    if( !out || max <= 0 )
        return 0;
    count = g_chrome_web_rail_intent_count < max
                ? g_chrome_web_rail_intent_count
                : max;
    for( int i = 0; i < count; i++ )
        out[i] = g_chrome_web_rail_intents[i];
    for( int i = count; i < g_chrome_web_rail_intent_count; i++ )
        g_chrome_web_rail_intents[i - count] = g_chrome_web_rail_intents[i];
    g_chrome_web_rail_intent_count -= count;
    if( count < max && g_chrome_web_rail_layout_pending )
    {
        out[count++] = g_chrome_web_rail_layout;
        g_chrome_web_rail_layout_pending = 0;
    }
    return count;
}

static void
chrome_web_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ChromeWeb* s = user;

    assert(s);
    assert(cmd);
    if( !s->open )
        return;

    /*
     * A PAGE BOUNDARY, stated by the stream that replaces the page.
     * @see TORIRS_CHROME_CMD_SYNC_BEGIN.
     *
     * The rail path reaches the same conclusion from the selection generation,
     * but on whichever frame the rail is published -- which is not the frame
     * the model moved on when the boundary came from a control on the page
     * itself. Dropping here puts the discard and its replacement in one
     * transaction. Local only, and therefore idempotent with the rail path.
     */
    if( cmd->kind == TORIRS_CHROME_CMD_SYNC_BEGIN && cmd->value )
    {
        chrome_web_reset_mounted(s);
        /* And the identity, so the rail's later arrival is recognised as the
         * page already mounted rather than as a further change. */
        if( cmd->serial )
            s->page_generation = cmd->serial;
    }
    if( cmd->kind == TORIRS_CHROME_CMD_SYNC_BEGIN )
        chrome_web_batch_begin(s);
    else if( !s->collecting )
        return;

    /* The mirror first, so the page's command and this executor's idea of what
     * exists cannot disagree about a handle that was just recycled. */
    ToriRSChromeMirror_Apply(&s->mirror, cmd);

    if( cmd->kind == TORIRS_CHROME_CMD_PANEL_OPEN )
        s->active_panel = cmd->panel;
    else if( cmd->kind == TORIRS_CHROME_CMD_PANEL_CLOSE &&
             cmd->panel == s->active_panel )
        s->active_panel = -1;
    else if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_ADD &&
             cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        s->widget_serial[cmd->widget] = cmd->serial;
    else if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_REMOVE &&
             cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        s->widget_serial[cmd->widget] = 0;

    if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_ADD ||
        cmd->kind == TORIRS_CHROME_CMD_WIDGET_REMOVE )
    {
        if( cmd->widget >= 0 && cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        {
            s->custom_panel[cmd->widget] = -1;
            s->custom_generation[cmd->widget] = 0;
            s->custom_serial[cmd->widget] = 0;
            s->custom_width[cmd->widget] = 0;
            s->custom_height[cmd->widget] = 0;
        }
    }

    /* Preserve the markers in the one array: the page adapter turns them into
     * one atomic page.snapshot/page.delta envelope without another boundary
     * crossing or parse per property. */
    chrome_web_batch_command(s, cmd);
    if( cmd->kind == TORIRS_CHROME_CMD_SYNC_END )
        chrome_web_batch_end(s);
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
        if( *at != '\\' || !at[1] )
        {
            out[o++] = *at++;
            continue;
        }
        /* The escapes chrome_web_escape writes, read back. Turning "\\n" into a
         * literal 'n' -- which skipping the backslash and copying the next byte
         * does -- silently rewrites every line break in a multiline field into
         * the letter n, and the value still looks plausible. */
        at++;
        switch( *at )
        {
        case 'n':
            out[o++] = '\n';
            break;
        case 'r':
            out[o++] = '\r';
            break;
        case 't':
            out[o++] = '\t';
            break;
        default:
            out[o++] = *at;
            break;
        }
        at++;
    }
    out[o] = '\0';
}

/* The canonical intent orders its user-controlled text before x/y/g/s. Find
 * the real end of that JSON string (rather than `strstr`ing through its
 * contents) and parse the identity fields only from the structural suffix. */
static char const*
chrome_web_after_string(char const* json, char const* key)
{
    char pattern[16];
    char const* at;

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    at = strstr(json, pattern);
    if( !at )
        return NULL;
    at += strlen(pattern);
    while( *at )
    {
        if( *at == '\\' && at[1] )
        {
            at += 2;
            continue;
        }
        if( *at == '"' )
            return at + 1;
        at++;
    }
    return NULL;
}

static int
chrome_web_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ChromeWeb* s = user;
    int queued;

    assert(s);
    assert(out);
    if( !s->open || max <= 0 )
        return 0;

    if( web_chrome_take_intent_overflow() )
    {
        s->snapshot_needed = 1;
        fprintf(stderr,
            "chrome: web input queue lost a gesture; restating visible state\n");
    }

    /* Empty an earlier partial drain before importing another browser burst;
     * otherwise two individually bounded pulls can overflow their shared
     * queue between the host's 16-intent Pump calls. */
    queued = ToriRSChromeMirror_Poll(&s->mirror, out, max);
    if( queued > 0 )
        return queued;

    /* Drain the page into the mirror's queue, then hand that out: the mirror
     * already owns the shuffle-the-tail-down behaviour a partial drain needs,
     * and duplicating it here would be a second place to lose a click. */
    for( int guard = 0; guard < TORIRS_CHROME_MIRROR_INTENTS; guard++ )
    {
        char* json = web_chrome_take_intent();
        struct ToriRSChromeIntent intent;
        char const* tail;

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
        tail = chrome_web_after_string(json, "text");
        if( !tail )
        {
            free(json);
            continue;
        }
        intent.x = chrome_web_int(tail, "x", 0);
        intent.y = chrome_web_int(tail, "y", 0);
        intent.selection_generation =
            (uint32_t)chrome_web_int(tail, "g", 0);
        intent.widget_serial = (uint32_t)chrome_web_int(tail, "s", 0);
        free(json);

        /* Validate the complete identity again on the wasm side. The outer
         * document already fences these values, but a queued page-A event can
         * otherwise arrive after page B has recycled the same widget handle. */
        if( intent.kind < TORIRS_CHROME_INTENT_ACTIVATE ||
            intent.kind > TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE ||
            intent.selection_generation == 0 ||
            intent.selection_generation != s->page_generation ||
            intent.panel != s->active_panel )
            continue;
        if( intent.kind == TORIRS_CHROME_INTENT_CLOSE )
        {
            if( intent.widget != -1 )
                continue;
        }
        else
        {
            struct ToriRSChromeMirrorWidget const* widget;

            if( intent.widget < 0 || intent.widget >= TORIRS_CHROME_MAX_WIDGETS )
                continue;
            widget = ToriRSChromeMirror_Widget(&s->mirror, intent.widget);
            if( !widget || widget->panel != intent.panel ||
                s->widget_serial[intent.widget] != intent.widget_serial )
                continue;
        }
        if( intent.kind == TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE &&
            (s->custom_panel[intent.widget] != intent.panel ||
             s->custom_generation[intent.widget] != intent.selection_generation ||
             s->custom_serial[intent.widget] != intent.widget_serial ||
             intent.x < 0 || intent.y < 0 ||
             intent.x >= s->custom_width[intent.widget] ||
             intent.y >= s->custom_height[intent.widget]) )
            continue;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
    }

    if( ToriRSChromeMirror_TakeIntentOverflow(&s->mirror) )
    {
        s->snapshot_needed = 1;
        fprintf(stderr, "chrome: web intent queue overflow; requesting snapshot\n");
    }

    return ToriRSChromeMirror_Poll(&s->mirror, out, max);
}

static int
chrome_web_custom_present(
    void* user, struct ToriRSChromeCustomFrame const* frame)
{
    struct ChromeWeb* s = user;
    int logical_width;
    int logical_height;

    if( !s || !s->open || !frame || !frame->argb || frame->width <= 0 ||
        frame->height <= 0 || frame->stride != frame->width ||
        frame->scale_milli <= 0 || frame->selection_generation == 0 ||
        frame->widget_serial == 0 || frame->widget < 0 ||
        frame->widget >= TORIRS_CHROME_MAX_WIDGETS )
        return 0;
    logical_width =
        (int)((int64_t)frame->width * 1000 / frame->scale_milli);
    logical_height =
        (int)((int64_t)frame->height * 1000 / frame->scale_milli);
    if( logical_width <= 0 || logical_height <= 0 )
        return 0;
    if( !web_chrome_custom_present(
        frame->panel,
        frame->widget,
        frame->selection_generation,
        frame->widget_serial,
        frame->scale_milli,
        frame->width,
        frame->height,
        frame->argb) )
    {
        s->custom_panel[frame->widget] = -1;
        s->custom_generation[frame->widget] = 0;
        s->custom_serial[frame->widget] = 0;
        s->custom_width[frame->widget] = 0;
        s->custom_height[frame->widget] = 0;
        s->snapshot_needed = 1;
        return 0;
    }
    s->custom_panel[frame->widget] = frame->panel;
    s->custom_generation[frame->widget] = frame->selection_generation;
    s->custom_serial[frame->widget] = frame->widget_serial;
    s->custom_width[frame->widget] = logical_width;
    s->custom_height[frame->widget] = logical_height;
    return 1;
}

static int
chrome_web_collect_delivery_loss(struct ChromeWeb* s)
{
    if( s && web_chrome_take_delivery_loss() )
    {
        s->snapshot_needed = 1;
        s->rail_snapshot_needed = 1;
    }
    return s != NULL;
}

static int
chrome_web_take_snapshot_request(void* user)
{
    struct ChromeWeb* s = user;
    int requested;

    if( !s ) return 0;
    (void)chrome_web_collect_delivery_loss(s);
    requested = s->snapshot_needed;
    s->snapshot_needed = 0;
    return requested;
}

static int
chrome_web_take_rail_snapshot_request(void* user)
{
    struct ChromeWeb* s = user;
    int requested;

    if( !s ) return 0;
    (void)chrome_web_collect_delivery_loss(s);
    requested = s->rail_snapshot_needed;
    s->rail_snapshot_needed = 0;
    return requested;
}

struct ToriRSChromeExec
ToriRSChromeExec_Web(void)
{
    struct ToriRSChromeExec exec;

    memset(&exec, 0, sizeof(exec));
    free(g_chrome_web.batch_json);
    memset(&g_chrome_web, 0, sizeof(g_chrome_web));
    g_chrome_web.active_panel = -1;
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        g_chrome_web.custom_panel[i] = -1;

    exec.user = &g_chrome_web;
    exec.begin = chrome_web_begin;
    exec.apply = chrome_web_apply;
    exec.end = chrome_web_end;
    exec.poll = chrome_web_poll;
    exec.rail_sync = chrome_web_rail_sync;
    exec.rail_icon = chrome_web_rail_icon;
    exec.rail_poll = chrome_web_rail_poll;
    exec.custom_present = chrome_web_custom_present;
    exec.take_snapshot_request = chrome_web_take_snapshot_request;
    exec.take_rail_snapshot_request = chrome_web_take_rail_snapshot_request;
    /* The DOM holds the widgets; BUFFER alone uses the in-canvas prim list. */
    return exec;
}
