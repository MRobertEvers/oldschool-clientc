/* One platform-neutral semantic executor for embedded browser engines.
 * WebView2, IWebBrowser2 and WKWebView differ below PlatformWindow's browser
 * transport; above it they consume the same local bundle and protocol-1 JSON. */

#include "torirs_chrome_exec.h"
#include "torirs_chrome_mirror.h"

#include "../platform/platform_window.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINBROWSER_COMMAND_MAX 8192
#define WINBROWSER_RAW_MAX (8 * 1024 * 1024)
#define WINBROWSER_RAIL_INTENT_MAX 32

struct WinBrowserJson
{
    char* data;
    size_t size;
    size_t capacity;
    int failed;
};

struct WinBrowserExec
{
    struct PlatformWindow* platform;
    struct ToriRSChromeMirror mirror;
    struct ToriRSChromeCmd commands[WINBROWSER_COMMAND_MAX];
    int command_count;
    int collecting;
    int command_overflow;
    int open;
    int first_batch;
    int panel;
    int check_style;
    int preferred_width;
    uint32_t shell_generation;
    uint32_t page_generation;
    uint32_t widget_serial[TORIRS_CHROME_MAX_WIDGETS];
    uint32_t custom_revision[TORIRS_CHROME_MAX_WIDGETS];
    int custom_panel[TORIRS_CHROME_MAX_WIDGETS];
    int custom_width[TORIRS_CHROME_MAX_WIDGETS];
    int custom_height[TORIRS_CHROME_MAX_WIDGETS];
    struct ToriRSChromeRailIntent rail_intents[WINBROWSER_RAIL_INTENT_MAX];
    int rail_intent_count;
    struct ToriRSChromeRailIntent layout;
    int layout_pending;
    int theme_sent;
    uint32_t outbound_sequence;
    char title[TORIRS_CHROME_TEXT_MAX];
    char raw[WINBROWSER_RAW_MAX];
};

static struct WinBrowserExec g_winbrowser;

static void json_free(struct WinBrowserJson* json)
{
    free(json->data);
    memset(json, 0, sizeof(*json));
}

static int json_reserve(struct WinBrowserJson* json, size_t extra)
{
    size_t required;
    size_t capacity;
    char* grown;
    if( json->failed || extra > SIZE_MAX - json->size - 1 )
    { json->failed = 1; return 0; }
    required = json->size + extra + 1;
    if( required <= json->capacity ) return 1;
    capacity = json->capacity ? json->capacity : 1024;
    while( capacity < required )
    {
        if( capacity > WINBROWSER_RAW_MAX / 2 )
        { capacity = WINBROWSER_RAW_MAX; break; }
        capacity *= 2;
    }
    if( capacity < required || capacity > WINBROWSER_RAW_MAX )
    { json->failed = 1; return 0; }
    grown = (char*)realloc(json->data, capacity);
    if( !grown ) { json->failed = 1; return 0; }
    json->data = grown;
    json->capacity = capacity;
    return 1;
}

static int json_append_n(struct WinBrowserJson* json, char const* text, size_t n)
{
    if( !json_reserve(json, n) ) return 0;
    memcpy(json->data + json->size, text, n);
    json->size += n;
    json->data[json->size] = 0;
    return 1;
}

static int json_append(struct WinBrowserJson* json, char const* text)
{ return json_append_n(json, text, strlen(text)); }

static int json_appendf(struct WinBrowserJson* json, char const* format, ...)
{
    va_list args;
    va_list copy;
    int length;
    va_start(args, format);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if( length < 0 || !json_reserve(json, (size_t)length) )
    { va_end(args); json->failed = 1; return 0; }
    vsnprintf(json->data + json->size, json->capacity - json->size, format, args);
    va_end(args);
    json->size += (size_t)length;
    return 1;
}

static int json_string(struct WinBrowserJson* json, char const* text)
{
    static char const hex[] = "0123456789abcdef";
    if( !json_append(json, "\"") ) return 0;
    if( !text ) text = "";
    for( size_t i = 0; text[i]; i++ )
    {
        unsigned char c = (unsigned char)text[i];
        char escaped[6];
        if( c == '"' || c == '\\' )
        {
            escaped[0] = '\\'; escaped[1] = (char)c;
            if( !json_append_n(json, escaped, 2) ) return 0;
        }
        else if( c == '\n' || c == '\r' || c == '\t' )
        {
            escaped[0] = '\\';
            escaped[1] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
            if( !json_append_n(json, escaped, 2) ) return 0;
        }
        else if( c < 0x20 )
        {
            escaped[0] = '\\'; escaped[1] = 'u'; escaped[2] = '0'; escaped[3] = '0';
            escaped[4] = hex[c >> 4]; escaped[5] = hex[c & 15];
            if( !json_append_n(json, escaped, 6) ) return 0;
        }
        else if( !json_append_n(json, (char const*)&text[i], 1) ) return 0;
    }
    return json_append(json, "\"");
}

static uint32_t effective_page_generation(struct WinBrowserExec const* s)
{
    return s->page_generation ? s->page_generation : s->shell_generation;
}

static void send_json(struct WinBrowserExec* s, struct WinBrowserJson* json)
{
    if( !json->failed && json->data && json->size )
        PlatformWindow_PluginBrowserSend(s->platform, json->data);
    json_free(json);
}

static void send_page_close_generation(
    struct WinBrowserExec* s, uint32_t generation)
{
    struct WinBrowserJson json = { 0 };
    if( !generation ) return;
    json_appendf(&json,
        "{\"protocol\":1,\"type\":\"page.close\",\"pageGeneration\":%u}",
        (unsigned)generation);
    send_json(s, &json);
}

static void reset_mounted_page(struct WinBrowserExec* s)
{
    ToriRSChromeMirror_Init(&s->mirror);
    memset(s->widget_serial, 0, sizeof(s->widget_serial));
    memset(s->custom_revision, 0, sizeof(s->custom_revision));
    memset(s->custom_width, 0, sizeof(s->custom_width));
    memset(s->custom_height, 0, sizeof(s->custom_height));
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        s->custom_panel[i] = -1;
    s->command_count = 0;
    s->collecting = 0;
    s->command_overflow = 0;
    s->first_batch = 1;
    s->panel = -1;
    s->title[0] = 0;
}

static void send_theme(struct WinBrowserExec* s)
{
    static char const theme[] =
        "{\"protocol\":1,\"type\":\"theme\",\"revision\":1,\"assets\":{"
        "\"panelBody\":\"skin/PanelBody.png\","
        "\"pluginIcon\":\"skin/PluginIcon.png\","
        "\"buttonLeft\":\"skin/ButtonLeft.png\","
        "\"buttonMiddle\":\"skin/ButtonMid.png\","
        "\"buttonRight\":\"skin/ButtonRight.png\","
        "\"checkOn\":\"skin/CheckOn.png\"," 
        "\"checkOff\":\"skin/CheckOff.png\"," 
        "\"checkBoxOn\":\"skin/CheckBoxOn.png\"," 
        "\"checkBoxOff\":\"skin/CheckBoxOff.png\"," 
        "\"dropdownBody\":\"skin/DropdownBody.png\"," 
        "\"scrollUp\":\"skin/ScrollUp.png\"," 
        "\"scrollDown\":\"skin/ScrollDown.png\"," 
        "\"scrollTrack\":\"skin/ScrollTrack.png\"," 
        "\"scrollGripTop\":\"skin/ScrollGripTop.png\"," 
        "\"scrollGripMiddle\":\"skin/ScrollGripMid.png\"," 
        "\"scrollGripBottom\":\"skin/ScrollGripBottom.png\"," 
        "\"close\":\"skin/CloseButton.png\"," 
        "\"frameTopLeft\":\"skin/FrameTopLeft.png\","
        "\"frameTop\":\"skin/FrameTop.png\","
        "\"frameTopRight\":\"skin/FrameTopRight.png\","
        "\"frameLeft\":\"skin/FrameLeft.png\","
        "\"frameRight\":\"skin/FrameRight.png\","
        "\"frameBottomLeft\":\"skin/FrameBottomLeft.png\","
        "\"frameBottom\":\"skin/FrameBottom.png\","
        "\"frameBottomRight\":\"skin/FrameBottomRight.png\"}}";
    if( s->theme_sent ) return;
    PlatformWindow_PluginBrowserSend(s->platform, theme);
    s->theme_sent = 1;
}

static int selected_width(
    struct ToriRSChromeRailSnapshot const* snapshot)
{
    int width = 360;
    int count = snapshot->entry_count;
    if( count < 0 ) count = 0;
    if( count > TORIRS_CHROME_RAIL_ENTRY_MAX ) count = TORIRS_CHROME_RAIL_ENTRY_MAX;
    for( int i = 0; i < count; i++ )
        if( snapshot->entries[i].plugin_index == snapshot->selected_entry &&
            snapshot->entries[i].preferred_width > 0 )
        { width = snapshot->entries[i].preferred_width; break; }
    if( width < 280 ) width = 280;
    if( width > 640 ) width = 640;
    return width;
}

static void browser_rail_sync(
    void* user, struct ToriRSChromeRailSnapshot const* snapshot)
{
    struct WinBrowserExec* s = user;
    struct WinBrowserJson json = { 0 };
    uint32_t old_page_generation;
    uint32_t new_page_generation;
    int count;
    if( !s || !snapshot || !PlatformWindow_PluginBrowserEnsure(s->platform) ) return;
    old_page_generation = effective_page_generation(s);
    new_page_generation = snapshot->page_generation
                              ? snapshot->page_generation
                              : snapshot->selection_generation;
    if( s->open && old_page_generation && new_page_generation &&
        old_page_generation != new_page_generation )
    {
        /* A visible A -> B rail selection does not tear the executor down.
         * Close A under its old identity and make the next model transaction
         * an atomic snapshot for B; a delta under B cannot patch A's DOM. */
        send_page_close_generation(s, old_page_generation);
        reset_mounted_page(s);
    }
    s->shell_generation = snapshot->selection_generation;
    s->page_generation = snapshot->page_generation;
    s->preferred_width = selected_width(snapshot);
    if( s->open && snapshot->expanded )
        PlatformWindow_ChromeOpen(s->platform, s->preferred_width, 480, "Plugins");
    send_theme(s);
    count = snapshot->entry_count;
    if( count < 0 ) count = 0;
    if( count > TORIRS_CHROME_RAIL_ENTRY_MAX ) count = TORIRS_CHROME_RAIL_ENTRY_MAX;
    json_appendf(&json,
        "{\"protocol\":1,\"type\":\"rail.snapshot\","
        "\"registryRevision\":%u,\"selectionGeneration\":%u,"
        "\"pageGeneration\":%u,\"activePlugin\":%d,"
        "\"lastSelectedPlugin\":%d,\"selectedEntry\":%d,"
        "\"expanded\":%s,\"entries\":[",
        (unsigned)snapshot->registry_revision,
        (unsigned)snapshot->selection_generation,
        (unsigned)snapshot->page_generation,
        snapshot->active_plugin, snapshot->last_selected_plugin,
        snapshot->selected_entry, snapshot->expanded ? "true" : "false");
    for( int i = 0; i < count; i++ )
    {
        struct ToriRSChromeRailEntry const* entry = &snapshot->entries[i];
        if( i ) json_append(&json, ",");
        json_appendf(&json,
            "{\"kind\":%d,\"pluginIndex\":%d,\"preferredWidth\":%d,"
            "\"title\":", entry->kind, entry->plugin_index, entry->preferred_width);
        json_string(&json, entry->title);
        json_append(&json, ",\"iconAsset\":");
        json_string(&json, entry->icon_asset);
        json_append(&json, ",\"badge\":");
        json_string(&json, entry->badge);
        json_appendf(&json, ",\"attention\":%s}", entry->attention ? "true" : "false");
    }
    json_append(&json, "]}");
    send_json(s, &json);
}

static void browser_rail_icon(void* user, struct ToriRSChromeRailIcon const* icon)
{
    struct WinBrowserExec* s = user;
    struct WinBrowserJson json = { 0 };
    char key[64];
    char url[256] = "";
    if( !s || !icon || icon->plugin_index < 0 || icon->revision == 0 ||
        icon->width < 0 || icon->height < 0 || icon->width > 64 || icon->height > 64 )
        return;
    if( icon->width > 0 && icon->height > 0 )
    {
        snprintf(key, sizeof(key), "rail-%d", icon->plugin_index);
        PlatformWindow_PluginBrowserBitmapUrl(
            s->platform, key, icon->revision, icon->argb,
            icon->width, icon->height, url, (int)sizeof(url));
    }
    json_appendf(&json,
        "{\"protocol\":1,\"type\":\"rail.icon\",\"pluginIndex\":%d,"
        "\"revision\":%u,\"width\":%d,\"height\":%d,\"url\":",
        icon->plugin_index, (unsigned)icon->revision, icon->width, icon->height);
    json_string(&json, url);
    json_append(&json, "}");
    send_json(s, &json);
}

static int browser_begin(void* user)
{
    struct WinBrowserExec* s = user;
    if( !s || !PlatformWindow_PluginBrowserEnsure(s->platform) ||
        PlatformWindow_PluginBrowserFailed(s->platform) )
        return 0;
    if( !PlatformWindow_ChromeOpen(
            s->platform, s->preferred_width > 0 ? s->preferred_width : 360,
            480, "Plugins") )
        return 0;
    reset_mounted_page(s);
    s->open = 1;
    return 1;
}

static void append_command(
    struct WinBrowserJson* json, struct ToriRSChromeCmd const* cmd)
{
    json_appendf(json,
        "{\"k\":%d,\"p\":%d,\"w\":%d,\"tab\":%d,\"v\":%d,"
        "\"c\":%u,\"x\":%d,\"y\":%d,\"cw\":%d,\"ch\":%d,"
        "\"label\":",
        cmd->kind, cmd->panel, cmd->widget, cmd->tab, cmd->value,
        (unsigned)cmd->color, cmd->x, cmd->y, cmd->w, cmd->h);
    json_string(json, cmd->label);
    json_append(json, ",\"text\":");
    json_string(json, cmd->text);
    json_appendf(json, ",\"s\":%u}", (unsigned)cmd->serial);
}

static void send_batch(struct WinBrowserExec* s)
{
    struct WinBrowserJson json = { 0 };
    uint32_t const generation = effective_page_generation(s);
    int emitted = 0;
    if( s->command_overflow || s->command_count <= 0 || !generation ) return;
    if( s->first_batch )
    {
        json_appendf(&json,
            "{\"protocol\":1,\"type\":\"page.snapshot\","
            "\"pageGeneration\":%u,\"panel\":%d,\"title\":",
            (unsigned)generation, s->panel);
        json_string(&json, s->title);
        json_appendf(&json, ",\"checkStyle\":%d,\"commands\":[", s->check_style);
    }
    else
        json_appendf(&json,
            "{\"protocol\":1,\"type\":\"page.delta\","
            "\"pageGeneration\":%u,\"commands\":[", (unsigned)generation);
    for( int i = 0; i < s->command_count; i++ )
    {
        /* A snapshot already starts from an empty page. A close from the old
         * retained model would hide that new page again when panel handles
         * are recycled across plugin selections. */
        if( s->first_batch &&
            s->commands[i].kind == TORIRS_CHROME_CMD_PANEL_CLOSE )
            continue;
        if( emitted++ ) json_append(&json, ",");
        append_command(&json, &s->commands[i]);
    }
    json_append(&json, "]}");
    if( !json.failed ) s->first_batch = 0;
    send_json(s, &json);
}

static void browser_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct WinBrowserExec* s = user;
    if( !s || !cmd || !s->open ) return;
    if( cmd->kind == TORIRS_CHROME_CMD_SYNC_BEGIN )
    {
        /*
         * A PAGE BOUNDARY, stated by the stream that is about to replace the
         * page. @see TORIRS_CHROME_CMD_SYNC_BEGIN.
         *
         * Dropping the mounted page HERE is what makes the close and the
         * snapshot one ordered pair: `first_batch` is set by the reset, so the
         * commands collected between this and SYNC_END go out as a complete
         * image rather than as a patch to a DOM that is no longer there.
         *
         * Guarded on `first_batch` so this is idempotent with the rail path
         * below, which reaches the same conclusion from the selection
         * generation a moment earlier. Whichever notices first does the work;
         * the other finds it already done and does not send a second close.
         */
        if( cmd->value && !s->first_batch )
        {
            send_page_close_generation(s, effective_page_generation(s));
            reset_mounted_page(s);
        }
        s->collecting = 1;
        s->command_count = 0;
        s->command_overflow = 0;
        return;
    }
    if( cmd->kind == TORIRS_CHROME_CMD_SYNC_END )
    {
        if( s->collecting ) send_batch(s);
        s->collecting = 0;
        s->command_count = 0;
        return;
    }
    if( !s->collecting || s->command_overflow ) return;
    ToriRSChromeMirror_Apply(&s->mirror, cmd);
    if( cmd->kind == TORIRS_CHROME_CMD_PANEL_OPEN )
    {
        s->panel = cmd->panel;
        snprintf(s->title, sizeof(s->title), "%s", cmd->text);
    }
    else if( cmd->kind == TORIRS_CHROME_CMD_PANEL_TITLE && cmd->panel == s->panel )
        snprintf(s->title, sizeof(s->title), "%s", cmd->text);
    else if( cmd->kind == TORIRS_CHROME_CMD_PANEL_CLOSE && cmd->panel == s->panel )
        s->panel = -1;
    else if( cmd->kind == TORIRS_CHROME_CMD_CHECK_STYLE )
        s->check_style = cmd->value;
    if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_ADD && cmd->widget >= 0 &&
        cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
        s->widget_serial[cmd->widget] = cmd->serial;
    else if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_REMOVE && cmd->widget >= 0 &&
             cmd->widget < TORIRS_CHROME_MAX_WIDGETS )
    {
        s->widget_serial[cmd->widget] = 0;
        s->custom_panel[cmd->widget] = -1;
        s->custom_width[cmd->widget] = 0;
        s->custom_height[cmd->widget] = 0;
    }
    if( s->command_count >= WINBROWSER_COMMAND_MAX )
    { s->command_count = 0; s->command_overflow = 1; return; }
    s->commands[s->command_count++] = *cmd;
}

static void browser_end(void* user)
{
    struct WinBrowserExec* s = user;
    if( !s || !s->open ) return;
    send_page_close_generation(s, effective_page_generation(s));
    PlatformWindow_ChromeClose(s->platform);
    s->open = 0;
    s->panel = -1;
    s->command_count = 0;
    s->collecting = 0;
}

static int json_int(char const* json, char const* key, int fallback)
{
    char pattern[64];
    char const* at;
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    at = strstr(json, pattern);
    if( !at ) return fallback;
    return (int)strtol(at + strlen(pattern), NULL, 10);
}

static int json_bool(char const* json, char const* key, int fallback)
{
    char pattern[64];
    char const* at;
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    at = strstr(json, pattern);
    if( !at ) return fallback;
    at += strlen(pattern);
    while( *at == ' ' || *at == '\t' || *at == '\r' || *at == '\n' ) at++;
    if( strncmp(at, "true", 4) == 0 ) return 1;
    if( strncmp(at, "false", 5) == 0 ) return 0;
    return (int)strtol(at, NULL, 10) != 0;
}

static uint32_t json_u32(char const* json, char const* key, uint32_t fallback)
{
    char pattern[64];
    char const* at;
    char* end;
    unsigned long value;
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    at = strstr(json, pattern);
    if( !at ) return fallback;
    at += strlen(pattern);
    while( *at == ' ' || *at == '\t' || *at == '\r' || *at == '\n' ) at++;
    if( *at < '0' || *at > '9' ) return fallback;
    value = strtoul(at, &end, 10);
    if( end == at || value > UINT32_MAX ) return fallback;
    return (uint32_t)value;
}

static void json_text(char const* json, char const* key, char* out, int capacity)
{
    char pattern[64];
    char const* at;
    int used = 0;
    if( capacity <= 0 ) return;
    out[0] = 0;
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    at = strstr(json, pattern);
    if( !at ) return;
    at += strlen(pattern);
    while( *at && *at != '"' && used < capacity - 1 )
    {
        if( *at != '\\' || !at[1] ) { out[used++] = *at++; continue; }
        at++;
        if( *at == 'n' ) out[used++] = '\n';
        else if( *at == 'r' ) out[used++] = '\r';
        else if( *at == 't' ) out[used++] = '\t';
        else out[used++] = *at;
        at++;
    }
    out[used] = 0;
}

/* Return the suffix after one JSON string value, respecting escapes. Widget
 * text precedes x/y/g/s in the canonical intent shape; parsing those numeric
 * fields only from this suffix prevents escaped user text such as `"g":0`
 * from being mistaken for protocol structure by the deliberately tiny parser. */
static char const* json_after_string(char const* json, char const* key)
{
    char pattern[64];
    char const* at;

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    at = strstr(json, pattern);
    if( !at ) return NULL;
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

static int json_type(char const* json, char const* expected)
{
    char type[64];
    json_text(json, "type", type, (int)sizeof(type));
    return strcmp(type, expected) == 0;
}

static int accept_sequence(struct WinBrowserExec* s, char const* json)
{
    uint32_t const sequence = json_u32(json, "sequence", 0);
    if( !sequence ||
        (s->outbound_sequence &&
         (int32_t)(sequence - s->outbound_sequence) <= 0) )
        return 0;
    s->outbound_sequence = sequence;
    return 1;
}

static void queue_rail(struct WinBrowserExec* s, struct ToriRSChromeRailIntent const* intent)
{
    if( s->rail_intent_count >= WINBROWSER_RAIL_INTENT_MAX )
        s->rail_intents[WINBROWSER_RAIL_INTENT_MAX - 1] = *intent;
    else s->rail_intents[s->rail_intent_count++] = *intent;
}

static void drain_messages(struct WinBrowserExec* s)
{
    for( int guard = 0; guard < 64; guard++ )
    {
        int const length = PlatformWindow_PluginBrowserPoll(
            s->platform, s->raw, (int)sizeof(s->raw));
        if( length <= 0 ) break;
        if( json_int(s->raw, "protocol", 0) != 1 ) continue;
        if( json_type(s->raw, "rail.select") )
        {
            struct ToriRSChromeRailIntent intent;
            if( !accept_sequence(s, s->raw) ) continue;
            memset(&intent, 0, sizeof(intent));
            intent.kind = TORIRS_CHROME_RAIL_INTENT_SELECT;
            intent.plugin_index = json_int(s->raw, "pluginIndex", -1);
            intent.selection_generation = json_u32(
                s->raw, "selectionGeneration", 0);
            intent.sequence = json_u32(s->raw, "sequence", 0);
            if( intent.plugin_index >= -2 && intent.plugin_index != -1 &&
                intent.selection_generation == s->shell_generation && intent.sequence )
                queue_rail(s, &intent);
        }
        else if( json_type(s->raw, "layout") )
        {
            struct ToriRSChromeRailIntent intent;
            if( !accept_sequence(s, s->raw) ) continue;
            memset(&intent, 0, sizeof(intent));
            intent.kind = TORIRS_CHROME_RAIL_INTENT_LAYOUT;
            intent.selection_generation = json_u32(
                s->raw, "selectionGeneration", 0);
            intent.sequence = json_u32(s->raw, "sequence", 0);
            intent.width = json_int(s->raw, "width", 0);
            intent.height = json_int(s->raw, "height", 0);
            intent.scale_milli = json_int(s->raw, "scaleMilli", 1000);
            intent.size_class = json_int(s->raw, "sizeClass", 0);
            intent.visible = json_bool(s->raw, "visible", 0);
            intent.game_visible = 1;
            if( intent.selection_generation == s->shell_generation && intent.sequence &&
                intent.width >= 0 && intent.height >= 0 && intent.scale_milli > 0 )
            { s->layout = intent; s->layout_pending = 1; }
        }
        else if( json_type(s->raw, "widget.intent") )
        {
            struct ToriRSChromeIntent intent;
            struct ToriRSChromeMirrorWidget* widget;
            char const* tail;
            if( !accept_sequence(s, s->raw) ) continue;
            memset(&intent, 0, sizeof(intent));
            intent.kind = json_int(s->raw, "k", 0);
            intent.panel = json_int(s->raw, "p", -1);
            intent.widget = json_int(s->raw, "w", -1);
            intent.value = json_int(s->raw, "v", 0);
            json_text(s->raw, "text", intent.text, (int)sizeof(intent.text));
            tail = json_after_string(s->raw, "text");
            if( !tail ) continue;
            intent.x = json_int(tail, "x", 0);
            intent.y = json_int(tail, "y", 0);
            intent.selection_generation = json_u32(tail, "g", 0);
            intent.widget_serial = json_u32(tail, "s", 0);
            if( !s->open || intent.kind < TORIRS_CHROME_INTENT_ACTIVATE ||
                intent.kind > TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE ||
                intent.selection_generation != effective_page_generation(s) )
                continue;
            if( intent.kind == TORIRS_CHROME_INTENT_CLOSE )
            {
                if( intent.panel == s->panel && intent.widget == -1 )
                    ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
                continue;
            }
            if( intent.widget < 0 || intent.widget >= TORIRS_CHROME_MAX_WIDGETS ||
                s->widget_serial[intent.widget] != intent.widget_serial )
                continue;
            widget = ToriRSChromeMirror_Widget(&s->mirror, intent.widget);
            if( !widget || widget->panel != intent.panel ) continue;
            if( intent.kind == TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE &&
                (s->custom_panel[intent.widget] != intent.panel || intent.x < 0 ||
                 intent.y < 0 || intent.x >= s->custom_width[intent.widget] ||
                 intent.y >= s->custom_height[intent.widget]) )
                continue;
            ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
            if( intent.kind == TORIRS_CHROME_INTENT_TOGGLE )
                ToriRSChromeMirror_PushActivate(&s->mirror, intent.panel, intent.widget);
        }
    }
}

static int browser_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct WinBrowserExec* s = user;
    if( !s || !out || max <= 0 ) return 0;
    drain_messages(s);
    return ToriRSChromeMirror_Poll(&s->mirror, out, max);
}

static int browser_rail_poll(
    void* user, struct ToriRSChromeRailIntent* out, int max)
{
    struct WinBrowserExec* s = user;
    int count;
    if( !s || !out || max <= 0 ) return 0;
    drain_messages(s);
    count = s->rail_intent_count < max ? s->rail_intent_count : max;
    for( int i = 0; i < count; i++ ) out[i] = s->rail_intents[i];
    for( int i = count; i < s->rail_intent_count; i++ )
        s->rail_intents[i - count] = s->rail_intents[i];
    s->rail_intent_count -= count;
    if( count < max && s->layout_pending )
    { out[count++] = s->layout; s->layout_pending = 0; }
    return count;
}

static void browser_custom_present(
    void* user, struct ToriRSChromeCustomFrame const* frame)
{
    struct WinBrowserExec* s = user;
    struct WinBrowserJson json = { 0 };
    uint32_t revision;
    char key[96];
    char url[256];
    if( !s || !s->open || !frame || !frame->argb || frame->stride != frame->width ||
        frame->widget < 0 || frame->widget >= TORIRS_CHROME_MAX_WIDGETS ||
        frame->width <= 0 || frame->height <= 0 || frame->scale_milli <= 0 ||
        frame->selection_generation != effective_page_generation(s) ||
        frame->widget_serial == 0 ||
        s->widget_serial[frame->widget] != frame->widget_serial )
        return;
    revision = ++s->custom_revision[frame->widget];
    if( !revision ) revision = ++s->custom_revision[frame->widget];
    snprintf(key, sizeof(key), "custom-%u-%u",
        (unsigned)frame->selection_generation, (unsigned)frame->widget_serial);
    if( !PlatformWindow_PluginBrowserBitmapUrl(
            s->platform, key, revision, frame->argb, frame->width, frame->height,
            url, (int)sizeof(url)) )
        return;
    s->custom_panel[frame->widget] = frame->panel;
    s->custom_width[frame->widget] =
        (int)((int64_t)frame->width * 1000 / frame->scale_milli);
    s->custom_height[frame->widget] =
        (int)((int64_t)frame->height * 1000 / frame->scale_milli);
    json_appendf(&json,
        "{\"protocol\":1,\"type\":\"custom.bitmap\","
        "\"pageGeneration\":%u,\"panel\":%d,\"widget\":%d,"
        "\"widgetSerial\":%u,\"revision\":%u,\"scaleMilli\":%d,"
        "\"width\":%d,\"height\":%d,\"url\":",
        (unsigned)effective_page_generation(s), frame->panel, frame->widget,
        (unsigned)frame->widget_serial, (unsigned)revision, frame->scale_milli,
        frame->width, frame->height);
    json_string(&json, url);
    json_append(&json, "}");
    send_json(s, &json);
}

struct ToriRSChromeExec
ToriRSChromeExec_Browser(void* platform)
{
    struct ToriRSChromeExec exec;
    memset(&exec, 0, sizeof(exec));
    memset(&g_winbrowser, 0, sizeof(g_winbrowser));
    g_winbrowser.platform = (struct PlatformWindow*)platform;
    g_winbrowser.panel = -1;
    g_winbrowser.preferred_width = 360;
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        g_winbrowser.custom_panel[i] = -1;
    exec.user = &g_winbrowser;
    exec.begin = browser_begin;
    exec.apply = browser_apply;
    exec.end = browser_end;
    exec.poll = browser_poll;
    exec.rail_sync = browser_rail_sync;
    exec.rail_icon = browser_rail_icon;
    exec.rail_poll = browser_rail_poll;
    exec.custom_present = browser_custom_present;
    return exec;
}
