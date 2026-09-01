#include "revconfig_load.h"

#include "3rd/ini/ini.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* Current [type:name] header `type` for keyval dispatch (component, layout, inv, sprite, …). */
static char s_ini_item_type[64];

/* ---- platform-suffixed sections -------------------------------------------
 *
 * A section name may carry an `@tag` suffix, and the section is loaded only
 * when the tag describes the machine this build is running on:
 *
 *   [component:cross]           every platform
 *   [component:cross@mobile]    touch devices only, and it OVERRIDES the above
 *
 * The suffix is stripped before the name reaches the buffer, so the two
 * declarations are the SAME element -- the later one wins, which is what makes
 * this an override rather than a second component with a decorated name. Order
 * therefore matters exactly as it already does for two files declaring the same
 * element: the base goes first, the platform variant after it.
 *
 * A section whose tag does not match is skipped WHOLE, keys included. That is
 * the only behaviour that composes: a half-applied override would leave an
 * element with some of the mobile fields and some of the desktop ones.
 *
 * Why a section suffix and not a key suffix: what varies between a phone and a
 * desktop is rarely one value. It is a component's whole presentation -- its
 * type, its sprites, its size -- and grouping those under one header keeps the
 * variant readable as a unit and keeps every key lookup in this file ignorant
 * of the mechanism.
 */
#define REVCONFIG_PLATFORM_TAG_MAX 32

/**
 * Does `tag` describe this build?
 *
 * `mobile` is the touch lane -- Android today, and whatever joins it. It is
 * keyed off the PLATFORM rather than off "is there a touchscreen", because a
 * profile is chosen at boot and cannot be re-baked when someone plugs a mouse
 * into a tablet.
 *
 * TORIRS_REVCONFIG_PLATFORM overrides the built-in tag, which is what lets the
 * mobile layout be looked at on a desktop without a device attached. A
 * developer-facing override, in the same spirit as TORIRS_CHROME_EXECUTOR.
 */
static int
revconfig_platform_tag_matches(char const* tag)
{
    char const* self;

    if( !tag || !tag[0] )
        return 1;

    self = getenv("TORIRS_REVCONFIG_PLATFORM");
    if( !self || !self[0] )
    {
#if defined(TORIRS_PLATFORM_ANDROID)
        self = "mobile";
#else
        self = "desktop";
#endif
    }
    return strcmp(tag, self) == 0;
}
static char s_ini_layout_group[32];
/* Section-header prefix this load accepts ("" = the unprefixed dialect), and
 * whether the section currently open failed that test — see
 * revconfig_load_fields_from_ini_prefixed. */
static char s_ini_section_prefix[32];
static int s_ini_section_skipped;

static int
push_field(
    struct RevConfigBuffer* revconfig_buffer,
    uint8_t kind,
    const char* value)
{
    return revconfig_buffer_push_field(revconfig_buffer, kind, value);
}

static void
push_element_from_ini_header(
    struct RevConfigBuffer* revconfig_buffer,
    char const* section_header)
{
    char item_type[64] = { 0 };
    char item_name[64] = { 0 };

    s_ini_section_skipped = 0;
    if( s_ini_section_prefix[0] != '\0' )
    {
        size_t plen = strlen(s_ini_section_prefix);
        if( strncmp(section_header, s_ini_section_prefix, plen) != 0 ||
            section_header[plen] != ':' )
        {
            s_ini_section_skipped = 1;
            return;
        }
        section_header += plen + 1;
    }

    const char* space = strchr(section_header, ':');
    if( space )
    {
        strncpy(item_type, section_header, space - section_header);
        item_type[space - section_header] = '\0';

        strncpy(item_name, space + 1, sizeof(item_name) - 1);
        item_name[sizeof(item_name) - 1] = '\0';
    }
    else
    {
        /*
         * A section with no `:` at all -- `[camera]`, `[features]`, `[chrome]`.
         * There is exactly one of each per profile, so there is nothing to name
         * it by, and the type IS the header.
         *
         * The old code returned here instead, which left s_ini_item_type
         * pointing at whatever section came BEFORE: an unrecognised header's
         * keys were silently applied to the previous item rather than dropped.
         */
        strncpy(item_type, section_header, sizeof(item_type) - 1);
        item_type[sizeof(item_type) - 1] = '\0';
        item_name[0] = '\0';
    }

    /*
     * `name@tag` -- the tag decides whether this section is for this build, and
     * is then removed so the element is stored under its BASE name and
     * overrides the unsuffixed declaration. @see revconfig_platform_tag_matches.
     */
    {
        char* at = strchr(item_name, '@');
        if( at )
        {
            *at = '\0';
            if( !revconfig_platform_tag_matches(at + 1) )
            {
                s_ini_section_skipped = 1;
                return;
            }
        }
    }

    strncpy(s_ini_item_type, item_type, sizeof(s_ini_item_type) - 1);
    s_ini_item_type[sizeof(s_ini_item_type) - 1] = '\0';

    push_field(revconfig_buffer, RCFIELD_ITEMTYPE, item_type);
    if( strcmp(item_type, "layout") == 0 )
    {
        strncpy(s_ini_layout_group, item_name, sizeof(s_ini_layout_group) - 1);
        s_ini_layout_group[sizeof(s_ini_layout_group) - 1] = '\0';
        push_field(revconfig_buffer, RCFIELD_UILAYOUT_GROUP, item_name);
    }
    else if( item_name[0] != '\0' )
        push_field(revconfig_buffer, RCFIELD_ITEMNAME, item_name);
}

/* `id=` is scoped to the cache-ref section types so it can never be mistaken
 * for a key of some future [component:…] or [sprite:…] spelling. */
static int
ini_type_is_cacheref(const char* item_type)
{
    static char const* const kinds[] = { REVCONFIG_CACHEREF_KINDS };
    assert(item_type);
    for( size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++ )
    {
        if( strcmp(item_type, kinds[i]) == 0 )
            return 1;
    }
    return 0;
}

static void
push_field_from_ini_kv(
    struct RevConfigBuffer* vec,
    const char* key,
    const char* value)
{
    if( s_ini_section_skipped )
        return;

    if( key[0] == '\0' )
    {
        /* Bare '=' line: record separator within a multi-entry layout section. */
        if( strcmp(s_ini_item_type, "layout") == 0 )
        {
            push_field(vec, RCFIELD_ITEMDONE, "");
            push_field(vec, RCFIELD_ITEMTYPE, "layout");
            if( s_ini_layout_group[0] != '\0' )
                push_field(vec, RCFIELD_UILAYOUT_GROUP, s_ini_layout_group);
        }
        return;
    }

    uint8_t kind = RCFIELD_NONE;

    /*
     * The three nameless sections first. Their keys are scoped to them the way
     * `id=` is scoped to the cache-ref kinds -- `mover=` and `controls=` are
     * ordinary enough words that a future [component:...] spelling could want
     * them, and the section type is what keeps the two apart.
     */
    if( strcmp(s_ini_item_type, "features") == 0 )
    {
        if( strcmp(key, "era") == 0 )
            kind = RCFIELD_FEATURES_ERA;
        else if( strcmp(key, "ground_click_nearest") == 0 )
            kind = RCFIELD_FEATURES_GROUND_CLICK_NEAREST;
        else if( strcmp(key, "ground_click_unbounded") == 0 )
            kind = RCFIELD_FEATURES_GROUND_CLICK_UNBOUNDED;
        else if( strcmp(key, "ground_click_offmap") == 0 )
            kind = RCFIELD_FEATURES_GROUND_CLICK_OFFMAP;
        else if( strcmp(key, "mover") == 0 )
            kind = RCFIELD_FEATURES_MOVER;
        else if( strcmp(key, "painter_draw_distance") == 0 )
            kind = RCFIELD_FEATURES_PAINTER_DRAW_DISTANCE;
        else
            TORIRS_LOG("revconfig: [features] has no key '%s'\n", key);
        if( kind != RCFIELD_NONE )
            push_field(vec, kind, value);
        return;
    }
    if( strcmp(s_ini_item_type, "camera") == 0 )
    {
        if( strcmp(key, "zoom") == 0 )
            kind = RCFIELD_CAMERA_ZOOM;
        else if( strcmp(key, "controls") == 0 )
            kind = RCFIELD_CAMERA_CONTROLS;
        else if( strcmp(key, "wheel_step") == 0 )
            kind = RCFIELD_CAMERA_WHEEL_STEP;
        else
            TORIRS_LOG("revconfig: [camera] has no key '%s'\n", key);
        if( kind != RCFIELD_NONE )
            push_field(vec, kind, value);
        return;
    }
    if( strcmp(s_ini_item_type, "chrome") == 0 )
    {
        if( strcmp(key, "plugin_button_iface") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_IFACE;
        else if( strcmp(key, "plugin_button_parent") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_PARENT;
        else if( strcmp(key, "plugin_button_x") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_X;
        else if( strcmp(key, "plugin_button_y") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_Y;
        else if( strcmp(key, "plugin_button_w") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_W;
        else if( strcmp(key, "plugin_button_h") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_H;
        else if( strcmp(key, "plugin_button_op") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_OP;
        else if( strcmp(key, "plugin_button_anchor") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_ANCHOR;
        else if( strcmp(key, "plugin_button_align") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_ALIGN;
        else if( strcmp(key, "plugin_button_margin") == 0 )
            kind = RCFIELD_CHROME_PLUGIN_BUTTON_MARGIN;
        else
            TORIRS_LOG("revconfig: [chrome] has no key '%s'\n", key);
        if( kind != RCFIELD_NONE )
            push_field(vec, kind, value);
        return;
    }

    /* Section type comes from [type:name] header (e.g. component, layout, inv, sprite). */
    if( strcmp(key, "sprite") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_SPRITE;
    else if( strcmp(key, "type") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TYPE;
    /* `c=` names a component in both a layout entry and a hotkey binding; the
     * section type disambiguates. Layout stays the unqualified fallback so the
     * existing `[layout:…]` bodies (which never state their type per entry) are
     * unaffected. */
    else if( strcmp(key, "c") == 0 && strcmp(s_ini_item_type, "hotkey") == 0 )
        kind = RCFIELD_HOTKEY_COMPONENT;
    else if( strcmp(key, "e") == 0 && strcmp(s_ini_item_type, "hotkey") == 0 )
        kind = RCFIELD_HOTKEY_EFFECT;
    else if( strcmp(key, "c") == 0 )
        kind = RCFIELD_UILAYOUT_COMPONENT;
    else if( strcmp(key, "x") == 0 )
        kind = RCFIELD_UILAYOUT_X;
    else if( strcmp(key, "y") == 0 )
        kind = RCFIELD_UILAYOUT_Y;
    else if( strcmp(key, "w") == 0 )
    {
        if( strcmp(s_ini_item_type, "component") == 0 )
            kind = RCFIELD_UICOMPONENT_WIDTH;
        else if( strcmp(s_ini_item_type, "layout") == 0 )
            kind = RCFIELD_UILAYOUT_WIDTH;
    }
    else if( strcmp(key, "h") == 0 )
    {
        if( strcmp(s_ini_item_type, "component") == 0 )
            kind = RCFIELD_UICOMPONENT_HEIGHT;
        else if( strcmp(s_ini_item_type, "layout") == 0 )
            kind = RCFIELD_UILAYOUT_HEIGHT;
    }
    else if( strcmp(key, "anchor_x") == 0 )
    {
        if( strcmp(s_ini_item_type, "component") == 0 )
            kind = RCFIELD_UICOMPONENT_ANCHOR_X;
        else if( strcmp(s_ini_item_type, "layout") == 0 )
            kind = RCFIELD_UILAYOUT_ANCHOR_X;
    }
    else if( strcmp(key, "anchor_y") == 0 )
    {
        if( strcmp(s_ini_item_type, "component") == 0 )
            kind = RCFIELD_UICOMPONENT_ANCHOR_Y;
        else if( strcmp(s_ini_item_type, "layout") == 0 )
            kind = RCFIELD_UILAYOUT_ANCHOR_Y;
    }
    else if( strcmp(key, "tabno") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TABNO;
    else if( strcmp(key, "selected") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_SELECTED;
    else if( strcmp(key, "slot") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_SLOT;
    else if( strcmp(key, "role") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_ROLE;
    /* `match=` is scoped to [role:…] the way `id=` is to the cache-ref kinds:
     * it is a common enough word that a future section spelling it must not
     * silently land here. */
    else if( strcmp(key, "match") == 0 && strcmp(s_ini_item_type, "role") == 0 )
        kind = RCFIELD_ROLE_MATCH;
    else if( strcmp(key, "componentno") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_COMPONENTNO;
    else if( strcmp(key, "inv") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_INV;
    else if( strcmp(key, "paint_levels") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_PAINT_LEVELS;
    else if( strcmp(key, "hotkey") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_HOTKEY;
    else if( strcmp(key, "color") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_COLOR;
    else if( strcmp(key, "filled") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_FILLED;
    else if( strcmp(key, "tiled") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TILED;
    else if( strcmp(key, "font") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_FONT;
    /* type=inkwell. @see ui/torirs_chrome_inkwell.h. */
    else if( strcmp(key, "style") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_INK_STYLE;
    else if( strcmp(key, "walk_color") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_INK_WALK_COLOR;
    else if( strcmp(key, "interact_color") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_INK_INTERACT_COLOR;
    else if( strcmp(key, "center") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CENTER;
    else if( strcmp(key, "valign") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_VALIGN;
    else if( strcmp(key, "over_color") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OVER_COLOR;
    else if( strcmp(key, "shadowed") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_SHADOWED;
    else if( strcmp(key, "text") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TEXT;
    /* Title-screen widget keys. `field=`, `action=`, `index=` and `mask=` are
     * ordinary words, so all of these are scoped to [component:…] the way the
     * rest of the component vocabulary is. */
    else if( strcmp(key, "field") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_FIELD;
    else if( strcmp(key, "prefix") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_PREFIX;
    else if( strcmp(key, "caret") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_CARET;
    else if( strcmp(key, "caret_blink") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_CARET_BLINK;
    else if( strcmp(key, "mask") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_MASK;
    else if( strcmp(key, "maxlen") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_MAXLEN;
    else if( strcmp(key, "charset") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_CHARSET;
    else if( strcmp(key, "action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_ACTION;
    else if( strcmp(key, "index") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_MESSAGE_INDEX;
    else if( strcmp(key, "px_per_percent") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TITLE_PX_PER_PERCENT;
    else if( strcmp(key, "flame_bias") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_FLAME_BIAS;
    else if( strcmp(key, "flame_sway") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_FLAME_SWAY;
    else if( strcmp(key, "flame_run") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_FLAME_RUN;
    else if( strcmp(key, "flame_row") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_FLAME_ROW;
    else if( strcmp(key, "flame_blur") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_FLAME_BLUR;
    else if( strcmp(key, "baseline") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_TEXT_BASELINE;
    /* `text=` already means the literal of an rs_text; under [string:] it is
     * the whole point of the section, so the type disambiguates as usual. */
    else if( strcmp(key, "text") == 0 && strcmp(s_ini_item_type, "string") == 0 )
        kind = RCFIELD_STRING_TEXT;
    /* The loading screen's own list. Every key is scoped to [preload:] so
     * names as generic as `id` and `kind` cannot collide with a component's. */
    else if( strcmp(s_ini_item_type, "preload") == 0 )
    {
        if( strcmp(key, "kind") == 0 )
            kind = RCFIELD_PRELOAD_KIND;
        if( strcmp(key, "archive") == 0 )
            kind = RCFIELD_PRELOAD_ARCHIVE;
        if( strcmp(key, "id") == 0 )
            kind = RCFIELD_PRELOAD_ID;
        if( strcmp(key, "percent") == 0 )
            kind = RCFIELD_PRELOAD_PERCENT;
        if( strcmp(key, "say") == 0 )
            kind = RCFIELD_PRELOAD_SAY;
        if( strcmp(key, "weight") == 0 )
            kind = RCFIELD_PRELOAD_WEIGHT;
        if( strcmp(key, "render") == 0 )
            kind = RCFIELD_PRELOAD_RENDER;
        if( strcmp(key, "order") == 0 )
            kind = RCFIELD_PRELOAD_ORDER;
    }
    else if( strcmp(s_ini_item_type, "login_reply") == 0 )
    {
        if( strcmp(key, "screen") == 0 )
            kind = RCFIELD_LOGIN_REPLY_SCREEN;
        else if( strcmp(key, "line1") == 0 )
            kind = RCFIELD_LOGIN_REPLY_LINE1;
        else if( strcmp(key, "line2") == 0 )
            kind = RCFIELD_LOGIN_REPLY_LINE2;
        else if( strcmp(key, "line3") == 0 )
            kind = RCFIELD_LOGIN_REPLY_LINE3;
        else
            TORIRS_LOG("revconfig: [login_reply:] has no key '%s'\n", key);
        if( kind != RCFIELD_NONE )
            push_field(vec, kind, value);
        return;
    }
    else if( strcmp(key, "option") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OPTION;
    else if( strcmp(key, "option_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OPTION_ACTION;
    else if( strcmp(key, "op0") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP0;
    else if( strcmp(key, "op1") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP1;
    else if( strcmp(key, "op2") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP2;
    else if( strcmp(key, "op3") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP3;
    else if( strcmp(key, "op4") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP4;
    else if( strcmp(key, "op0_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP0_ACTION;
    else if( strcmp(key, "op1_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP1_ACTION;
    else if( strcmp(key, "op2_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP2_ACTION;
    else if( strcmp(key, "op3_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP3_ACTION;
    else if( strcmp(key, "op4_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_OP4_ACTION;
    else if( strcmp(key, "button_type") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_BUTTON_TYPE;
    else if( strcmp(key, "client_code") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CLIENT_CODE;
    else if( strcmp(key, "chat_op_report_abuse") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE;
    else if(
        strcmp(key, "chat_op_report_abuse_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE_ACTION;
    else if( strcmp(key, "chat_op_add_ignore") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE;
    else if(
        strcmp(key, "chat_op_add_ignore_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE_ACTION;
    else if( strcmp(key, "chat_op_add_friend") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND;
    else if(
        strcmp(key, "chat_op_add_friend_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND_ACTION;
    else if(
        strcmp(key, "chat_op_accept_trade") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE;
    else if(
        strcmp(key, "chat_op_accept_trade_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE_ACTION;
    else if( strcmp(key, "chat_op_accept_duel") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL;
    else if(
        strcmp(key, "chat_op_accept_duel_action") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL_ACTION;
    else if( strcmp(key, "prompt") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_PROMPT;
    else if( strcmp(key, "filter") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_FILTER;
    else if( strcmp(key, "label") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL;
    else if( strcmp(key, "label_y") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL_Y;
    else if( strcmp(key, "mode_y") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE_Y;
    else if( strcmp(key, "mode0") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0;
    else if( strcmp(key, "mode1") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1;
    else if( strcmp(key, "mode2") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2;
    else if( strcmp(key, "mode3") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3;
    else if( strcmp(key, "mode0_color") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0_COLOR;
    else if( strcmp(key, "mode1_color") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1_COLOR;
    else if( strcmp(key, "mode2_color") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2_COLOR;
    else if( strcmp(key, "mode3_color") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3_COLOR;
    else if( strcmp(key, "item") == 0 && strcmp(s_ini_item_type, "inv") == 0 )
        kind = RCFIELD_INV_ITEM;
    else if( strcmp(key, "sprite_active") == 0 && strcmp(s_ini_item_type, "component") == 0 )
        kind = RCFIELD_UICOMPONENT_SPRITE_ACTIVE;
    else if( strcmp(key, "left") == 0 )
        kind = RCFIELD_UILAYOUT_LEFT;
    else if( strcmp(key, "top") == 0 )
        kind = RCFIELD_UILAYOUT_TOP;
    else if( strcmp(key, "right") == 0 )
        kind = RCFIELD_UILAYOUT_RIGHT;
    else if( strcmp(key, "bottom") == 0 )
        kind = RCFIELD_UILAYOUT_BOTTOM;
    else if( strcmp(key, "dirty") == 0 && strcmp(s_ini_item_type, "layout") == 0 )
        kind = RCFIELD_UILAYOUT_DIRTY;
    else if( strcmp(key, "xalign") == 0 && strcmp(s_ini_item_type, "layout") == 0 )
        kind = RCFIELD_UILAYOUT_XALIGN;
    else if( strcmp(key, "safe_area") == 0 && strcmp(s_ini_item_type, "layout") == 0 )
        kind = RCFIELD_UILAYOUT_SAFE_AREA;
    else if( strcmp(key, "safe_area_margin") == 0 && strcmp(s_ini_item_type, "layout") == 0 )
        kind = RCFIELD_UILAYOUT_SAFE_AREA_MARGIN;
    else if( (strcmp(key, "p") == 0 || strcmp(key, "parent") == 0) &&
             strcmp(s_ini_item_type, "layout") == 0 )
        kind = RCFIELD_UILAYOUT_PARENT;
    else if( (strcmp(key, "n") == 0 || strcmp(key, "name") == 0) &&
             strcmp(s_ini_item_type, "layout") == 0 )
        kind = RCFIELD_UILAYOUT_NAME;
    else if( strcmp(key, "table") == 0 )
        kind = RCFIELD_CACHE_TABLE;
    else if( strcmp(key, "archive_id") == 0 )
        kind = RCFIELD_CACHE_ARCHIVE_ID;
    else if( strcmp(key, "archive") == 0 )
        kind = RCFIELD_CACHE_ARCHIVE;
    /* `slot=` is only meaningful under `table=defaults`; it is the position in
     * the defaults record, which is how the client addresses these sprites. */
    else if( strcmp(key, "slot") == 0 )
        kind = RCFIELD_CACHE_DEFAULTS_SLOT;
    else if( strcmp(key, "container") == 0 )
        kind = RCFIELD_CACHE_CONTAINER;
    else if( strcmp(key, "index") == 0 )
        kind = RCFIELD_CACHE_INDEX_FILENAME;
    else if( strcmp(key, "filename") == 0 )
        kind = RCFIELD_CACHE_DATA_FILENAME;
    else if( strcmp(key, "format") == 0 )
        kind = RCFIELD_CACHE_FORMAT;
    else if( strcmp(key, "atlas_index") == 0 )
        kind = RCFIELD_CACHE_ATLAS_INDEX;
    else if( strcmp(key, "atlas_count") == 0 )
        kind = RCFIELD_CACHE_ATLAS_COUNT;
    else if( strcmp(key, "crop_x") == 0 )
        kind = RCFIELD_CACHE_CROP_X;
    else if( strcmp(key, "crop_y") == 0 )
        kind = RCFIELD_CACHE_CROP_Y;
    else if( strcmp(key, "crop_width") == 0 )
        kind = RCFIELD_CACHE_CROP_WIDTH;
    else if( strcmp(key, "crop_height") == 0 )
        kind = RCFIELD_CACHE_CROP_HEIGHT;
    /* `group=` is scoped to the asset sections: it says which [layout:<group>]
     * builds load this sprite or font, and "group" is an ordinary enough word
     * that a future [component:…] spelling must not silently land here. */
    else if(
        strcmp(key, "group") == 0 &&
        (strcmp(s_ini_item_type, "sprite") == 0 || strcmp(s_ini_item_type, "font") == 0) )
        kind = RCFIELD_CACHE_GROUP;
    else if( strcmp(key, "font_name") == 0 )
        kind = RCFIELD_CACHE_FONT_NAME;
    else if( strcmp(key, "cache_font_id") == 0 )
        kind = RCFIELD_CACHE_FONT_ID;
    else if( strcmp(key, "id") == 0 && ini_type_is_cacheref(s_ini_item_type) )
        kind = RCFIELD_CACHEREF_ID;
    else if(
        strcmp(key, "transform1") == 0 || strcmp(key, "transform2") == 0 ||
        strcmp(key, "transform3") == 0 || strcmp(key, "transform4") == 0 )
        kind = RCFIELD_CACHE_TRANSFORM;

    if( kind != RCFIELD_NONE )
        push_field(vec, kind, value);
}

void
revconfig_load_fields_from_ini_bytes_prefixed(
    const uint8_t* data,
    uint32_t size,
    const char* section_prefix,
    struct RevConfigBuffer* revconfig_buffer)
{
    assert(revconfig_buffer);
    if( size == 0 )
        return;
    assert(data);

    s_ini_item_type[0] = '\0';
    s_ini_section_skipped = 0;
    if( section_prefix && section_prefix[0] )
    {
        strncpy(s_ini_section_prefix, section_prefix, sizeof(s_ini_section_prefix) - 1);
        s_ini_section_prefix[sizeof(s_ini_section_prefix) - 1] = '\0';
    }
    else
        s_ini_section_prefix[0] = '\0';

    struct INIReader reader = { 0 };
    ini_reader_init(&reader);

    struct INIElement element = { 0 };
    int parse_result = TORI_INI_ERR_OK;
    while( (parse_result = ini_reader_next(&reader, (uint8_t*)data, size, &element)) ==
           TORI_INI_ERR_OK )
    {
        switch( element.kind )
        {
        case INI_ELEMENT_UNDEFINED:
            break;
        case INI_ELEMENT_SECTION:
            push_element_from_ini_header(revconfig_buffer, element._section.name);
            break;
        case INI_ELEMENT_SECTION_END:
            if( !s_ini_section_skipped )
                push_field(revconfig_buffer, RCFIELD_ITEMDONE, "");
            break;
        case INI_ELEMENT_KEYVAL:
            push_field_from_ini_kv(revconfig_buffer, element._keyval.name, element._keyval.value);
            break;
        }
    }

    push_field(revconfig_buffer, RCFIELD_ITEMDONE, "");
    s_ini_section_prefix[0] = '\0';
    s_ini_section_skipped = 0;
    if( parse_result != TORI_INI_ERR_NONE || reader.state != INI_READER_STATE_DONE )
    {
        TORIRS_ERR("revconfig_load_fields_from_ini_bytes: parse failed result=%d state=%d "
            "offset=%u size=%u\n",
            parse_result,
            (int)reader.state,
            reader.offset,
            size);
    }
    assert(parse_result == TORI_INI_ERR_NONE && reader.state == INI_READER_STATE_DONE);
}

void
revconfig_load_fields_from_ini_bytes(
    const uint8_t* data,
    uint32_t size,
    struct RevConfigBuffer* revconfig_buffer)
{
    revconfig_load_fields_from_ini_bytes_prefixed(data, size, NULL, revconfig_buffer);
}

void
revconfig_load_fields_from_ini_prefixed(
    const char* filename,
    const char* section_prefix,
    struct RevConfigBuffer* revconfig_buffer)
{
    /*
     * Binary mode, and the size is what fread returned rather than what ftell
     * said. In text mode on Windows the CRT eats the '\r' of every CRLF, so a
     * short read is the normal case for a checked-out .ini and the old
     * `!= file_size` bail silently loaded nothing. The reader treats '\r' as a
     * line terminator (3rd/ini/ini.c), so the bytes go through untranslated.
     */
    FILE* f = fopen(filename, "rb");
    if( !f )
        return;

    char* file_data = NULL;
    long file_size = 0;
    size_t read_size = 0;

    if( fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return;
    }

    file_data = malloc((size_t)file_size);
    assert(file_data);

    read_size = fread(file_data, 1, (size_t)file_size, f);
    fclose(f);

    if( read_size == 0 )
    {
        free(file_data);
        return;
    }

    revconfig_load_fields_from_ini_bytes_prefixed(
        (const uint8_t*)file_data, (uint32_t)read_size, section_prefix, revconfig_buffer);
    free(file_data);
}

void
revconfig_load_fields_from_ini(
    const char* filename,
    struct RevConfigBuffer* revconfig_buffer)
{
    revconfig_load_fields_from_ini_prefixed(filename, NULL, revconfig_buffer);
}

int
revconfig_ini_has_layout_group(
    const char* filename,
    const char* group)
{
    char line[512];
    char wanted[64];
    int found = 0;
    size_t wanted_len;
    FILE* file;

    assert(filename);
    assert(group);
    if( !filename[0] || !group[0] )
        return 0;

    snprintf(wanted, sizeof(wanted), "[layout:%s]", group);
    wanted_len = strlen(wanted);

    file = fopen(filename, "r");
    if( !file )
        return 0;

    while( !found && fgets(line, sizeof(line), file) )
    {
        char const* scan = line;
        while( *scan == ' ' || *scan == '\t' )
            scan++;
        if( strncmp(scan, wanted, wanted_len) == 0 )
            found = 1;
    }

    fclose(file);
    return found;
}

int
revconfig_ini_has_prefixed_sections(
    const char* filename,
    const char* section_prefix)
{
    char line[512];
    size_t plen;
    int found = 0;
    FILE* f;

    assert(filename);
    assert(section_prefix);
    if( !filename[0] || !section_prefix[0] )
        return 0;

    f = fopen(filename, "r");
    if( !f )
        return 0;

    plen = strlen(section_prefix);
    while( !found && fgets(line, sizeof(line), f) )
    {
        char const* p = line;
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p != '[' )
            continue;
        p++;
        if( strncmp(p, section_prefix, plen) == 0 && p[plen] == ':' )
            found = 1;
    }

    fclose(f);
    return found;
}