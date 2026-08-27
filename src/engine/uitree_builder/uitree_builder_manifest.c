#include "uitree_builder_manifest.h"

#include "revconfig/revconfig_load.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

void
uibuilder_manifest_init(struct UIBuilderManifest* out)
{
    assert(out);
    memset(out, 0, sizeof(*out));
}

void
uibuilder_manifest_free(struct UIBuilderManifest* m)
{
    assert(m);
    for( int i = 0; i < m->inv_count; i++ )
    {
        free(m->invs[i].obj_ids);
        free(m->invs[i].obj_counts);
        m->invs[i].obj_ids = NULL;
        m->invs[i].obj_counts = NULL;
        m->invs[i].item_count = 0;
    }
    free(m->sprites);
    free(m->fonts);
    free(m->components);
    free(m->invs);
    free(m->ops);
    free(m->hotkeys);
    memset(m, 0, sizeof(*m));
}

int
uibuilder_pack_component_id(int componentno)
{
    if( componentno < 0 )
        return -1;
    if( componentno > 0xFFFF )
        return componentno;
    return componentno << 16;
}

int
uibuilder_uicomponent_is_iface_mount(struct RevConfigUIComponentItem const* item)
{
    assert(item);
    return strcmp(item->type, "rs_iface") == 0;
}

int
uibuilder_uicomponent_iface_id(
    struct RevConfigUIComponentItem const* item,
    int root_interface_id)
{
    assert(item);
    /* An rs_iface that names no group mounts the boot/top-level one. Stating the
     * id twice is how a manifest and the server's IF_SETTOPLEVEL drift apart —
     * `[ui:boot] interface_id` (or whatever the server last re-rooted to) is the
     * single place it lives. */
    if( uibuilder_uicomponent_is_iface_mount(item) && item->componentno < 0 )
        return root_interface_id;
    return item->componentno;
}

int
uibuilder_uicomponent_needs_rs_load(struct RevConfigUIComponentItem const* item)
{
    assert(item);
    if( item->componentno < 0 )
        return uibuilder_uicomponent_is_iface_mount(item);

    if( uibuilder_uicomponent_is_iface_mount(item) )
        return 1;
    if( strcmp(item->type, "sidebar") == 0 )
        return 1;
    if( strcmp(item->type, "chat") == 0 )
        return 1;
    if( strcmp(item->type, "rs_layer") == 0 )
        return 1;
    if( strcmp(item->type, "rs_graphic") == 0 )
        return 1;
    if( strcmp(item->type, "rs_text") == 0 )
        return 1;
    if( strcmp(item->type, "rs_rect") == 0 )
        return 1;
    if( strcmp(item->type, "rs_model") == 0 )
        return 1;
    if( strcmp(item->type, "rs_inv") == 0 )
        return 1;
    return 0;
}

static uint8_t
parse_paint_levels_mask(char const* str)
{
    assert(str);
    if( str[0] == '\0' )
        return 0xFu;
    unsigned m = 0u;
    char const* p = str;
    while( *p != '\0' )
    {
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == '\0' )
            break;
        char* end = NULL;
        long lo = strtol(p, &end, 10);
        if( end == p )
        {
            while( *p && *p != ',' )
                p++;
            if( *p == ',' )
                p++;
            continue;
        }
        p = end;
        if( lo >= 0 && lo < 8 )
            m |= 1u << (unsigned)lo;
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == ',' )
            p++;
    }
    return m == 0u ? 0xFu : (uint8_t)m;
}

static void*
grow_array(
    void* ptr,
    int* count,
    int* cap,
    size_t elem_size)
{
    assert(count && cap && elem_size > 0);
    if( *count < *cap )
        return ptr;
    int new_cap = *cap == 0 ? 8 : *cap * 2;
    void* grown = realloc(ptr, (size_t)new_cap * elem_size);
    assert(grown);
    *cap = new_cap;
    return grown;
}

static struct RevConfigUIComponentItem const*
find_component(
    struct RevConfigItemBuffer const* items,
    char const* name)
{
    assert(items && name);
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind != RCITEM_UICOMPONENT )
            continue;
        if( strcmp(items->items[i].u.uicomponent.name, name) == 0 )
            return &items->items[i].u.uicomponent;
    }
    return NULL;
}

static int
component_req_exists(
    struct UIBuilderManifest const* m,
    int packed_id)
{
    for( int i = 0; i < m->component_count; i++ )
    {
        if( m->components[i].packed_id == packed_id )
            return 1;
    }
    return 0;
}

static void
add_sprite(
    struct UIBuilderManifest* out,
    int* sprite_cap,
    struct RevConfigCacheItem const* cache)
{
    assert(out && sprite_cap && cache);
    out->sprites = grow_array(out->sprites, &out->sprite_count, sprite_cap, sizeof(*out->sprites));
    struct UIBuilderSpriteReq* s = &out->sprites[out->sprite_count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, cache->name, sizeof(s->name) - 1);
    s->archive_id = cache->archive_id;
    s->atlas_index = cache->atlas_index;
    s->atlas_count = cache->atlas_count;
    strncpy(s->format, cache->format, sizeof(s->format) - 1);
    strncpy(s->data_filename, cache->data_filename, sizeof(s->data_filename) - 1);
    strncpy(s->index_filename, cache->index_filename, sizeof(s->index_filename) - 1);
    strncpy(s->table, cache->table, sizeof(s->table) - 1);
    strncpy(s->archive, cache->archive, sizeof(s->archive) - 1);
    strncpy(s->group, cache->group, sizeof(s->group) - 1);
    s->defaults_slot = cache->defaults_slot;
    s->crop_x = cache->crop_x;
    s->crop_y = cache->crop_y;
    s->crop_width = cache->crop_width;
    s->crop_height = cache->crop_height;
    s->transform_count = cache->transform_count;
    for( int t = 0; t < cache->transform_count && t < 4; t++ )
        strncpy(s->transform[t], cache->transform[t], sizeof(s->transform[t]) - 1);
}

static void
add_font(
    struct UIBuilderManifest* out,
    int* font_cap,
    struct RevConfigFontItem const* font)
{
    assert(out && font_cap && font);
    out->fonts = grow_array(out->fonts, &out->font_count, font_cap, sizeof(*out->fonts));
    struct UIBuilderFontReq* f = &out->fonts[out->font_count++];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, font->name, sizeof(f->name) - 1);
    f->archive_id = font->archive_id;
    f->cache_font_id = font->cache_font_id;
    strncpy(
        f->font_name,
        font->font_name[0] != '\0' ? font->font_name : font->name,
        sizeof(f->font_name) - 1);
    strncpy(f->group, font->group, sizeof(f->group) - 1);
}

static void
add_component_req(
    struct UIBuilderManifest* out,
    int* component_cap,
    int componentno)
{
    assert(out && component_cap);
    int packed = uibuilder_pack_component_id(componentno);
    assert(packed >= 0);
    if( component_req_exists(out, packed) )
        return;
    out->components =
        grow_array(out->components, &out->component_count, component_cap, sizeof(*out->components));
    struct UIBuilderComponentReq* c = &out->components[out->component_count++];
    c->packed_id = packed;
    c->iface_id = packed >> 16;
}

/* A binding whose component or effect is blank is a malformed section, not a
 * disabled one — dropping it here keeps the bake's resolve loop total. */
static void
add_hotkey(
    struct UIBuilderManifest* out,
    int* hotkey_cap,
    struct RevConfigHotkeyItem const* hotkey)
{
    assert(out && hotkey_cap && hotkey);
    if( hotkey->name[0] == '\0' || hotkey->component[0] == '\0' || hotkey->effect[0] == '\0' )
        return;

    out->hotkeys = grow_array(out->hotkeys, &out->hotkey_count, hotkey_cap, sizeof(*out->hotkeys));
    struct UIBuilderHotkey* binding = &out->hotkeys[out->hotkey_count++];
    memset(binding, 0, sizeof(*binding));
    strncpy(binding->key_name, hotkey->name, sizeof(binding->key_name) - 1);
    strncpy(binding->component_name, hotkey->component, sizeof(binding->component_name) - 1);
    strncpy(binding->effect, hotkey->effect, sizeof(binding->effect) - 1);
}

static void
add_inv(
    struct UIBuilderManifest* out,
    int* inv_cap,
    struct RevConfigInvItem const* inv)
{
    assert(out && inv_cap && inv);
    out->invs = grow_array(out->invs, &out->inv_count, inv_cap, sizeof(*out->invs));
    struct UIBuilderInvSeed* s = &out->invs[out->inv_count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, inv->name, sizeof(s->name) - 1);
    s->item_count = inv->item_count;
    if( s->item_count <= 0 )
        return;
    s->obj_ids = calloc((size_t)s->item_count, sizeof(int));
    s->obj_counts = calloc((size_t)s->item_count, sizeof(int));
    assert(s->obj_ids && s->obj_counts);
    for( int i = 0; i < s->item_count; i++ )
    {
        s->obj_ids[i] = revconfig_parse_int(inv->items[i]);
        s->obj_counts[i] = 1;
    }
}

static void
fill_tree_op_from_component(
    struct UIBuilderTreeOp* op,
    struct RevConfigUIComponentItem const* comp,
    int root_interface_id)
{
    assert(op && comp);
    strncpy(op->type, comp->type, sizeof(op->type) - 1);
    op->componentno = uibuilder_uicomponent_iface_id(comp, root_interface_id);
    strncpy(op->sprite_ref, comp->sprite, sizeof(op->sprite_ref) - 1);
    strncpy(op->sprite_active_ref, comp->sprite_active, sizeof(op->sprite_active_ref) - 1);
    strncpy(op->inv_name, comp->inv, sizeof(op->inv_name) - 1);
    op->font = comp->font;
    op->has_font_ref = comp->has_font_ref;
    if( comp->has_font_ref )
        strncpy(op->font_ref, comp->font_ref, sizeof(op->font_ref) - 1);
    op->tabno = comp->tabno;
    op->selected = comp->selected;
    strncpy(op->slot, comp->slot, sizeof(op->slot) - 1);
    strncpy(op->role, comp->role, sizeof(op->role) - 1);
    op->level_mask = parse_paint_levels_mask(comp->paint_levels);
    op->hotkey_count = comp->hotkey_count;
    for( int i = 0; i < comp->hotkey_count && i < REVCONFIG_COMPONENT_HOTKEY_MAX; i++ )
        strncpy(op->hotkeys[i], comp->hotkeys[i], sizeof(op->hotkeys[i]) - 1);
    op->color = comp->color;
    op->filled = comp->filled;
    op->tiled = comp->tiled;
    op->center = comp->center;
    op->valign = comp->valign;
    op->over_color = comp->over_color;
    op->shadowed = comp->shadowed;
    strncpy(op->text, comp->text, sizeof(op->text) - 1);
    strncpy(op->title_field, comp->title_field, sizeof(op->title_field) - 1);
    strncpy(op->title_prefix, comp->title_prefix, sizeof(op->title_prefix) - 1);
    strncpy(op->title_caret, comp->title_caret, sizeof(op->title_caret) - 1);
    op->title_caret_blink = comp->title_caret_blink;
    strncpy(op->title_mask, comp->title_mask, sizeof(op->title_mask) - 1);
    op->title_maxlen = comp->title_maxlen;
    strncpy(op->title_charset, comp->title_charset, sizeof(op->title_charset) - 1);
    strncpy(op->title_action, comp->title_action, sizeof(op->title_action) - 1);
    op->title_message_index = comp->title_message_index;
    op->title_px_per_percent = comp->title_px_per_percent;
    op->text_baseline = comp->text_baseline;
    op->button_type = comp->button_type;
    op->client_code = comp->client_code;
    strncpy(op->option, comp->option, sizeof(op->option) - 1);
    op->option_action = comp->option_action;
    for( int i = 0; i < REVCONFIG_MENU_OPTION_SLOTS; i++ )
    {
        strncpy(op->ops[i], comp->ops[i], sizeof(op->ops[i]) - 1);
        op->op_actions[i] = comp->op_actions[i];
    }
    strncpy(
        op->chat_op_report_abuse,
        comp->chat_op_report_abuse,
        sizeof(op->chat_op_report_abuse) - 1);
    op->chat_op_report_abuse_action = comp->chat_op_report_abuse_action;
    strncpy(
        op->chat_op_add_ignore, comp->chat_op_add_ignore, sizeof(op->chat_op_add_ignore) - 1);
    op->chat_op_add_ignore_action = comp->chat_op_add_ignore_action;
    strncpy(
        op->chat_op_add_friend, comp->chat_op_add_friend, sizeof(op->chat_op_add_friend) - 1);
    op->chat_op_add_friend_action = comp->chat_op_add_friend_action;
    strncpy(
        op->chat_op_accept_trade,
        comp->chat_op_accept_trade,
        sizeof(op->chat_op_accept_trade) - 1);
    op->chat_op_accept_trade_action = comp->chat_op_accept_trade_action;
    strncpy(
        op->chat_op_accept_duel, comp->chat_op_accept_duel, sizeof(op->chat_op_accept_duel) - 1);
    op->chat_op_accept_duel_action = comp->chat_op_accept_duel_action;
    op->chat_button_filter = comp->chat_button_filter;
    strncpy(op->chat_button_label, comp->chat_button_label, sizeof(op->chat_button_label) - 1);
    op->chat_button_label_y = comp->chat_button_label_y;
    op->chat_button_mode_y = comp->chat_button_mode_y;
    for( int i = 0; i < 4; i++ )
    {
        strncpy(
            op->chat_button_mode_label[i],
            comp->chat_button_mode_label[i],
            sizeof(op->chat_button_mode_label[i]) - 1);
        op->chat_button_mode_color[i] = comp->chat_button_mode_color[i];
    }

    if( uibuilder_uicomponent_needs_rs_load(comp) )
        op->kind = UIBUILDER_OP_PUSH_RS_SUBTREE;
    else
        op->kind = UIBUILDER_OP_PUSH_BUILTIN;
}

static void
add_layout_op(
    struct UIBuilderManifest* out,
    int* op_cap,
    struct RevConfigItemBuffer const* items,
    struct RevConfigUILayoutItem const* layout,
    int root_interface_id)
{
    assert(out && op_cap && items && layout);
    /* Bare '=' separators produce empty layout shells — skip them. */
    if( layout->component[0] == '\0' )
        return;

    struct RevConfigUIComponentItem const* comp = find_component(items, layout->component);
    assert(comp && "layout c= must name a [component:] section");

    out->ops = grow_array(out->ops, &out->op_count, op_cap, sizeof(*out->ops));
    struct UIBuilderTreeOp* op = &out->ops[out->op_count++];
    memset(op, 0, sizeof(*op));

    strncpy(op->name, layout->name, sizeof(op->name) - 1);
    strncpy(op->component_name, layout->component, sizeof(op->component_name) - 1);
    strncpy(op->parent_name, layout->parent, sizeof(op->parent_name) - 1);
    op->x = layout->x;
    op->y = layout->y;
    op->width = layout->width > 0 ? layout->width : comp->width;
    op->height = layout->height > 0 ? layout->height : comp->height;
    op->has_anchor = layout->has_anchor;
    if( layout->has_anchor )
    {
        op->anchor_x = layout->anchor_x;
        op->anchor_y = layout->anchor_y;
    }
    else
    {
        op->anchor_x = comp->anchor_x;
        op->anchor_y = comp->anchor_y;
    }
    op->top = layout->top;
    op->left = layout->left;
    op->bottom = layout->bottom;
    op->right = layout->right;
    op->dirty = layout->dirty;

    fill_tree_op_from_component(op, comp, root_interface_id);
}

int
uibuilder_manifest_group_wanted(
    char const* group,
    char const* select,
    char const* exclude)
{
    assert(group);

    /* Untagged records belong to every build. Without this the key would be
     * subtractive and adding it would change what every existing profile
     * builds. */
    if( group[0] == '\0' )
        return 1;
    if( exclude && exclude[0] != '\0' && strcmp(group, exclude) == 0 )
        return 0;
    if( select && select[0] != '\0' )
        return strcmp(group, select) == 0;
    return 1;
}

int
uibuilder_manifest_from_revconfig(
    struct UIBuilderManifest* out,
    struct RevConfigItemBuffer const* items)
{
    return uibuilder_manifest_from_revconfig_rooted(out, items, -1);
}

int
uibuilder_manifest_from_revconfig_rooted(
    struct UIBuilderManifest* out,
    struct RevConfigItemBuffer const* items,
    int root_interface_id)
{
    return uibuilder_manifest_from_revconfig_grouped(out, items, root_interface_id, NULL, NULL);
}

int
uibuilder_manifest_from_revconfig_grouped(
    struct UIBuilderManifest* out,
    struct RevConfigItemBuffer const* items,
    int root_interface_id,
    char const* layout_group,
    char const* layout_group_exclude)
{
    assert(out);
    assert(items);

    uibuilder_manifest_free(out);
    uibuilder_manifest_init(out);

    int sprite_cap = 0;
    int font_cap = 0;
    int component_cap = 0;
    int inv_cap = 0;
    int op_cap = 0;
    int hotkey_cap = 0;

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        switch( item->kind )
        {
        case RCITEM_CACHE_SPRITE:
            if( uibuilder_manifest_group_wanted(
                    item->u.cache.group, layout_group, layout_group_exclude) )
                add_sprite(out, &sprite_cap, &item->u.cache);
            break;
        case RCITEM_CACHE_FONT:
            if( uibuilder_manifest_group_wanted(
                    item->u.font.group, layout_group, layout_group_exclude) )
                add_font(out, &font_cap, &item->u.font);
            break;
        case RCITEM_INV:
            add_inv(out, &inv_cap, &item->u.inv);
            break;
        case RCITEM_UICOMPONENT:
            if( uibuilder_uicomponent_needs_rs_load(&item->u.uicomponent) )
            {
                int iface =
                    uibuilder_uicomponent_iface_id(&item->u.uicomponent, root_interface_id);
                if( iface >= 0 )
                    add_component_req(out, &component_cap, iface);
                else
                    TORIRS_LOG("revconfig: component '%s' is an rs_iface with no componentno= and no "
                        "root interface to fall back on; it will mount nothing\n",
                        item->u.uicomponent.name);
            }
            break;
        case RCITEM_HOTKEY:
            add_hotkey(out, &hotkey_cap, &item->u.hotkey);
            break;
        default:
            break;
        }
    }

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind != RCITEM_UILAYOUT )
            continue;
        if( !uibuilder_manifest_group_wanted(
                items->items[i].u.uilayout.layout_group, layout_group, layout_group_exclude) )
            continue;
        add_layout_op(out, &op_cap, items, &items->items[i].u.uilayout, root_interface_id);
    }

    return 0;
}

int
uibuilder_manifest_from_revconfig_ini(
    struct UIBuilderManifest* out,
    char const* ini_path)
{
    return uibuilder_manifest_from_revconfig_inis(out, ini_path, NULL);
}

int
uibuilder_manifest_from_revconfig_inis(
    struct UIBuilderManifest* out,
    char const* ui_ini_path,
    char const* cache_ini_path)
{
    struct UIBuilderManifestSources src = { 0 };
    src.ui_ini_path = ui_ini_path;
    src.cache_ini_path = cache_ini_path;
    src.root_interface_id = -1;
    return uibuilder_manifest_from_sources(out, &src);
}

/*
 * The layout a manifest gets when it names an interface but declares no
 * RevConfig at all: one rs_iface mount, which is exactly what the old
 * open-the-interface-as-the-root path produced. Written as RevConfig text rather
 * than as hand-built ops so there is only one thing to keep correct, and so the
 * default is something a manifest can copy, paste and then edit.
 */
static char const k_default_root_layout[] =
    "[component:root_interface]\n"
    "type=rs_iface\n"
    "[layout:root]\n"
    "c=root_interface\n";

int
uibuilder_manifest_from_sources(
    struct UIBuilderManifest* out,
    struct UIBuilderManifestSources const* src)
{
    assert(out);
    assert(src);

    struct RevConfigBuffer* fields = revconfig_buffer_new(256);
    assert(fields);

    int have_layout = 0;
    if( src->ui_ini_path && src->ui_ini_path[0] != '\0' )
    {
        revconfig_load_fields_from_ini(src->ui_ini_path, fields);
        have_layout = 1;
    }
    if( src->cache_ini_path && src->cache_ini_path[0] != '\0' )
        revconfig_load_fields_from_ini(src->cache_ini_path, fields);
    /* Last, so a manifest's inline sections extend a shared UI file: layout
     * records append in load order, and that order is the sibling order. */
    if( src->inline_ini_path && src->inline_ini_path[0] != '\0' )
    {
        if( revconfig_ini_has_prefixed_sections(src->inline_ini_path, "revconfig") )
        {
            revconfig_load_fields_from_ini_prefixed(src->inline_ini_path, "revconfig", fields);
            have_layout = 1;
        }
    }
    if( !have_layout )
        revconfig_load_fields_from_ini_bytes(
            (uint8_t const*)k_default_root_layout,
            (uint32_t)(sizeof(k_default_root_layout) - 1),
            fields);

    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(64);
    assert(items);
    revconfig_items_build(fields, items);

    int rc = uibuilder_manifest_from_revconfig_grouped(
        out, items, src->root_interface_id, src->layout_group, src->layout_group_exclude);

    revconfig_item_buffer_free(items);
    revconfig_buffer_free(fields);
    return rc;
}
