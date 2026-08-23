#ifndef UITREE_BUILDER_MANIFEST_H
#define UITREE_BUILDER_MANIFEST_H

#include "revconfig/revconfig.h"

#include <stdint.h>

struct UIBuilderSpriteReq
{
    char name[64];
    int archive_id; /* dat2; -1 if name-only dat1 */
    int atlas_index;
    int atlas_count;
    char format[16]; /* pix8/pix32 for dat1 */
    char data_filename[64];
    char index_filename[64];
    char table[32];
    char archive[32];
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
    char transform[4][64];
    int transform_count;
};

struct UIBuilderFontReq
{
    char name[64];
    int archive_id;
    int cache_font_id;
    /* dat1: title-jagfile stem (e.g. "b12"); defaults to the section name. */
    char font_name[64];
};

struct UIBuilderComponentReq
{
    int packed_id;
    int iface_id;
};

struct UIBuilderInvSeed
{
    char name[64];
    int* obj_ids;
    int* obj_counts; /* optional; NULL means all counts are 1 */
    int item_count;
};

enum UIBuilderTreeOpKind
{
    UIBUILDER_OP_PUSH_BUILTIN = 0,
    UIBUILDER_OP_PUSH_RS_SUBTREE,
};

/*
 * One [hotkey:<key>] binding, still in config spelling.
 *
 * The manifest deliberately stays string-shaped: turning a key name into an
 * OSRS code and an effect name into a bit needs input/ and ui/, which the
 * manifest layer (and its standalone test) does not link. uitree_builder_bake
 * resolves both.
 */
struct UIBuilderHotkey
{
    char key_name[64];
    char component_name[64];
    char effect[64];
};

struct UIBuilderTreeOp
{
    enum UIBuilderTreeOpKind kind;
    char name[64];
    /** The [component:…] this op instantiates. Layout entries are named
     *  separately (`name` is the layout's n=), but a hotkey binds a COMPONENT,
     *  so the bake needs the component name to find the node it produced. */
    char component_name[64];
    char parent_name[64];
    char type[32];
    int x;
    int y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    uint8_t has_anchor;
    int top;
    int left;
    int bottom;
    int right;
    int componentno;
    char sprite_ref[64];
    char sprite_active_ref[64];
    char inv_name[64];
    char font_ref[64];
    int font;
    uint8_t has_font_ref;
    int tabno;
    int selected;
    char slot[24];
    int dirty;
    int level_mask;
    /** Effect names this component advertises (revconfig hotkey= lines). */
    char hotkeys[REVCONFIG_COMPONENT_HOTKEY_MAX][64];
    int hotkey_count;
    int color;
    int filled;
    /** type=rs_graphic: repeat the sprite across the box. @see RevConfig `tiled=`. */
    int tiled;
    int center;
    /** type=rs_text: 0 top, 1 centre, 2 bottom. @see RevConfig `valign=`. */
    int valign;
    /** Hover colour, 0 for none. @see RevConfig `over_color=`. */
    int over_color;
    int shadowed;
    char text[256];
    int button_type;
    int client_code;
    char option[REVCONFIG_MENU_OPTION_LEN];
    char ops[REVCONFIG_MENU_OPTION_SLOTS][REVCONFIG_MENU_OPTION_LEN];
    int option_action;
    int op_actions[REVCONFIG_MENU_OPTION_SLOTS];
    char chat_op_report_abuse[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_report_abuse_action;
    char chat_op_add_ignore[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_add_ignore_action;
    char chat_op_add_friend[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_add_friend_action;
    char chat_op_accept_trade[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_accept_trade_action;
    char chat_op_accept_duel[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_accept_duel_action;
    int chat_button_filter;
    char chat_button_label[64];
    int chat_button_label_y;
    int chat_button_mode_y;
    char chat_button_mode_label[4][16];
    int chat_button_mode_color[4];
};

struct UIBuilderManifest
{
    struct UIBuilderSpriteReq* sprites;
    int sprite_count;
    struct UIBuilderFontReq* fonts;
    int font_count;
    struct UIBuilderComponentReq* components;
    int component_count;
    struct UIBuilderInvSeed* invs;
    int inv_count;
    struct UIBuilderTreeOp* ops;
    int op_count;
    struct UIBuilderHotkey* hotkeys;
    int hotkey_count;
};

void
uibuilder_manifest_init(struct UIBuilderManifest* out);

void
uibuilder_manifest_free(struct UIBuilderManifest* m);

/** Pack iface-level componentno as (id << 16); leave already-packed ids alone. */
int
uibuilder_pack_component_id(int componentno);

int
uibuilder_uicomponent_needs_rs_load(struct RevConfigUIComponentItem const* item);

/** True for the component type that mounts a cache interface pack, `rs_iface`. */
int
uibuilder_uicomponent_is_iface_mount(struct RevConfigUIComponentItem const* item);

/**
 * The interface group `item` mounts, or -1 for none.
 *
 * An rs_iface with no componentno= means "the root interface", which is
 * root_interface_id — the manifest's `[ui:boot] interface_id`, or whatever the
 * server last re-rooted to with IF_SETTOPLEVEL. Everything else is its own
 * componentno=.
 */
int
uibuilder_uicomponent_iface_id(
    struct RevConfigUIComponentItem const* item,
    int root_interface_id);

int
uibuilder_manifest_from_revconfig(
    struct UIBuilderManifest* out,
    struct RevConfigItemBuffer const* items);

/** As above, resolving componentno-less rs_iface mounts to root_interface_id. */
int
uibuilder_manifest_from_revconfig_rooted(
    struct UIBuilderManifest* out,
    struct RevConfigItemBuffer const* items,
    int root_interface_id);

int
uibuilder_manifest_from_revconfig_ini(
    struct UIBuilderManifest* out,
    char const* ini_path);

/** Load ui_ini then optional cache_ini (NULL/empty skipped) into one manifest. */
int
uibuilder_manifest_from_revconfig_inis(
    struct UIBuilderManifest* out,
    char const* ui_ini_path,
    char const* cache_ini_path);

/**
 * Every RevConfig source a boot manifest can name, in load order.
 *
 * Order is sibling order: later records append after earlier ones, so a
 * manifest's inline sections extend the shared files rather than restate them.
 * All paths are optional; when none of them yields a layout the builder
 * synthesises the single rs_iface mount that root_interface_id names, which is
 * what a manifest with no RevConfig at all used to get from the interface-open
 * path.
 */
struct UIBuilderManifestSources
{
    char const* ui_ini_path;
    char const* cache_ini_path;
    /** File holding `[revconfig:…]` sections — in practice the manifest itself. */
    char const* inline_ini_path;
    int root_interface_id;
};

int
uibuilder_manifest_from_sources(
    struct UIBuilderManifest* out,
    struct UIBuilderManifestSources const* src);

#endif
