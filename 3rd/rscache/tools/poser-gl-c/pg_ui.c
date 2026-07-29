#include "pg_app.h"
#include "pg_font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Panel layout, in pixels.
 *
 * poser-gl declares this with flexbox and lets LEGUI solve it. Immediate mode has
 * no solver, so the arithmetic is here — the same five regions, sized from the
 * window rather than from each other.
 */
#define PG_MARGIN 5.0f
#define PG_MENU_HEIGHT 24.0f
#define PG_LIST_WIDTH 173.0f
#define PG_RIGHT_WIDTH 180.0f
#define PG_TIMELINE_HEIGHT 112.0f
#define PG_EDITOR_HEIGHT 260.0f
#define PG_ROW 15.0f

/* Widget id ranges, so no two panels can collide. */
enum
{
    ID_MENU = 100,
    ID_SEARCH = 200,
    ID_TABS = 210,
    ID_LIST = 220,
    ID_POLYGON = 300,
    ID_SHADING = 310,
    ID_COMPONENT = 320,
    ID_LENGTH = 400,
    ID_ACTIONS = 410,
    ID_TYPES = 420,
    ID_SLIDERS = 430,
    ID_PLAY = 500,
    ID_TIMELINE = 510,
    ID_DIALOG = 600
};

/* ---- list filtering ---------------------------------------------------------- */

static bool
contains_ci(const char* haystack, const char* needle)
{
    size_t n;
    if( !needle || !*needle )
        return true;
    if( !haystack )
        return false;
    n = strlen(needle);
    for( const char* p = haystack; *p; p++ )
    {
        size_t i = 0;
        while( i < n )
        {
            char a = p[i];
            char b = needle[i];
            if( a >= 'A' && a <= 'Z' )
                a = (char)(a - 'A' + 'a');
            if( b >= 'A' && b <= 'Z' )
                b = (char)(b - 'A' + 'a');
            if( a != b )
                break;
            i++;
        }
        if( i == n )
            return true;
    }
    return false;
}

static void
list_push(struct PG_FilteredList* list, int value)
{
    if( list->count == list->capacity )
    {
        int next = list->capacity ? list->capacity * 2 : 256;
        int* grown = realloc(list->indices, (size_t)next * sizeof(int));
        if( !grown )
            return;
        list->indices = grown;
        list->capacity = next;
    }
    list->indices[list->count++] = value;
}

static void
rebuild_list(struct PG_App* app, int tab)
{
    struct PG_FilteredList* list = &app->lists[tab];
    list->count = 0;

    switch( tab )
    {
    case PG_LIST_ENTITY:
        for( int i = 0; i < pg_cache_npc_count(app->cache); i++ )
        {
            const struct PG_NpcDef* npc = pg_cache_npc_at(app->cache, i);
            if( contains_ci(npc->name, list->search) )
                list_push(list, i);
        }
        break;
    case PG_LIST_SEQUENCE:
        for( int i = 0; i < app->animation_count; i++ )
        {
            char id_text[16];
            const struct PG_Animation* anim = app->animations[i];
            snprintf(id_text, sizeof(id_text), "%d", anim->id);
            /* The reference matches sequences on the id string, not the name —
             * most sequences have no name at all. Matching either is strictly
             * more useful and costs nothing when the name is absent. */
            if( contains_ci(id_text, list->search) || contains_ci(anim->name, list->search) )
                list_push(list, i);
        }
        break;
    case PG_LIST_ITEM:
        for( int i = 0; i < pg_cache_item_count(app->cache); i++ )
        {
            const struct PG_ItemDef* item = pg_cache_item_at(app->cache, i);
            if( contains_ci(item->name, list->search) )
                list_push(list, i);
        }
        break;
    default:
        break;
    }
    list->dirty = false;
}

struct RowContext
{
    struct PG_App* app;
    int tab;
};

static void
list_row(void* ctx, int index, struct PG_GuiListRow* out)
{
    static char buffer[128];
    struct RowContext* rc = ctx;
    struct PG_FilteredList* list = &rc->app->lists[rc->tab];
    int source;

    out->text = "";
    out->marked = false;
    if( index < 0 || index >= list->count )
        return;
    source = list->indices[index];

    switch( rc->tab )
    {
    case PG_LIST_ENTITY:
    {
        const struct PG_NpcDef* npc = pg_cache_npc_at(rc->app->cache, source);
        if( !npc )
            return;
        snprintf(buffer, sizeof(buffer), "%d  %s", npc->id, npc->name ? npc->name : "null");
        break;
    }
    case PG_LIST_SEQUENCE:
    {
        const struct PG_Animation* anim = rc->app->animations[source];
        if( anim->name )
            snprintf(buffer, sizeof(buffer), "%d  %s", anim->id, anim->name);
        else
            snprintf(buffer, sizeof(buffer), "%d", anim->id);
        out->marked = anim->modified;
        break;
    }
    case PG_LIST_ITEM:
    {
        const struct PG_ItemDef* item = pg_cache_item_at(rc->app->cache, source);
        if( !item )
            return;
        snprintf(buffer, sizeof(buffer), "%d  %s", item->id, item->name ? item->name : "null");
        break;
    }
    default:
        return;
    }
    out->text = buffer;
}

/* ---- panels -------------------------------------------------------------------- */

static void
panel(struct PG_Gui* gui, float x, float y, float w, float h, const char* title)
{
    pg_gui_rect(gui, x, y, w, h, PG_COL_PANEL);
    if( title )
    {
        pg_gui_rect(gui, x, y, w, 16.0f, PG_COL_BACKGROUND);
        pg_gui_text_centred(gui, x, y + 5.0f, w, title, PG_COL_TEXT);
    }
}

static void
draw_menu_bar(struct PG_App* app, float x, float y, float w)
{
    struct PG_Gui* gui = &app->gui;
    static const char* labels[] = { "Pack", "Export", "Import", "Undo", "Redo", "Settings", "About" };
    float bx = x + 3.0f;
    const float bw = 62.0f;

    pg_gui_rect(gui, x, y, w, PG_MENU_HEIGHT, PG_COL_BACKGROUND);
    for( int i = 0; i < (int)(sizeof(labels) / sizeof(labels[0])); i++ )
    {
        if( pg_gui_button(gui, ID_MENU + i, bx, y + 3.0f, bw, 18.0f, labels[i]) )
        {
            switch( i )
            {
            case 0:
                app->dialog = PG_DIALOG_PACK;
                snprintf(app->path_buffer, sizeof(app->path_buffer), "%s", "cache.packed");
                break;
            case 1:
                if( pg_app_current_animation(app) )
                {
                    app->dialog = PG_DIALOG_EXPORT;
                    snprintf(
                        app->path_buffer,
                        sizeof(app->path_buffer),
                        "seq_%d.pgl",
                        pg_app_current_animation(app)->id);
                }
                else
                {
                    pg_app_message(app, "Invalid Operation", "No animation selected");
                }
                break;
            case 2:
                app->dialog = PG_DIALOG_IMPORT;
                snprintf(app->path_buffer, sizeof(app->path_buffer), "%s", "animation.pgl");
                break;
            case 3:
                pg_app_undo(app);
                break;
            case 4:
                pg_app_redo(app);
                break;
            case 5:
                app->dialog = PG_DIALOG_SETTINGS;
                break;
            default:
                app->dialog = PG_DIALOG_ABOUT;
                break;
            }
        }
        bx += bw + 3.0f;
    }

    pg_gui_text_clipped(
        gui, bx + 8.0f, y + 8.0f, w - (bx - x) - 12.0f, app->status, PG_COL_TEXT_DIM);
}

static void
draw_list_panel(struct PG_App* app, float x, float y, float w, float h)
{
    struct PG_Gui* gui = &app->gui;
    static const char* names[] = { "Entity", "Seq", "Item" };
    struct PG_FilteredList* list = &app->lists[app->list_tab];
    struct RowContext ctx = { app, app->list_tab };
    float tab_w = (w - 10.0f) / 3.0f;
    int clicked;

    panel(gui, x, y, w, h, NULL);

    if( pg_gui_text_input(
            gui, ID_SEARCH, x + 5.0f, y + 5.0f, w - 10.0f, PG_ROW, list->search,
            (int)sizeof(list->search), "Search") )
    {
        list->dirty = true;
        list->scroll = 0.0f;
        list->selected = -1;
    }

    for( int i = 0; i < PG_LIST_COUNT; i++ )
    {
        if( pg_gui_toggle(
                gui, ID_TABS + i, x + 5.0f + (float)i * tab_w, y + 25.0f, tab_w - 1.0f, 18.0f,
                names[i], app->list_tab == i, true) )
        {
            app->list_tab = i;
            app->lists[i].dirty = true;
        }
    }

    if( list->dirty )
        rebuild_list(app, app->list_tab);

    clicked = pg_gui_list(
        gui, ID_LIST, x + 5.0f, y + 48.0f, w - 10.0f, h - 53.0f, list->count, list_row, &ctx,
        &list->scroll, list->selected);

    if( clicked >= 0 && clicked < list->count )
    {
        int source = list->indices[clicked];
        list->selected = clicked;
        switch( app->list_tab )
        {
        case PG_LIST_ENTITY:
            pg_app_load_npc(app, pg_cache_npc_at(app->cache, source));
            break;
        case PG_LIST_SEQUENCE:
            pg_app_select_animation(app, source);
            break;
        case PG_LIST_ITEM:
        {
            const struct PG_ItemDef* item = pg_cache_item_at(app->cache, source);
            if( item )
            {
                pg_app_toggle_item(app, item->id, true);
                pg_app_status(app, "Equipped %s", item->name ? item->name : "item");
            }
            break;
        }
        default:
            break;
        }
    }
}

static void
draw_manager_panel(struct PG_App* app, float x, float y, float w, float h)
{
    struct PG_Gui* gui = &app->gui;
    static const char* polygon_names[] = { "Fill", "Wire", "Vert" };
    static const char* shading_names[] = { "Smooth", "Flat", "None" };
    float button_w = (w - 12.0f) / 3.0f;
    float cursor_y;
    char label[96];
    int remove_index = -1;

    panel(gui, x, y, w, h, "Entity Manager");

    snprintf(
        label, sizeof(label), "Selected: %s", app->has_entity ? app->entity.name : "N/A");
    pg_gui_text_clipped(gui, x + 6.0f, y + 22.0f, w - 12.0f, label, PG_COL_TEXT);

    for( int i = 0; i < 3; i++ )
    {
        if( pg_gui_toggle(
                gui, ID_POLYGON + i, x + 6.0f + (float)i * button_w, y + 38.0f,
                button_w - 2.0f, 18.0f, polygon_names[i], app->polygon_mode == i, true) )
            app->polygon_mode = i;
    }
    for( int i = 0; i < 3; i++ )
    {
        if( pg_gui_toggle(
                gui, ID_SHADING + i, x + 6.0f + (float)i * button_w, y + 60.0f,
                button_w - 2.0f, 18.0f, shading_names[i], app->shading_type == i, true) )
        {
            app->shading_type = i;
            /* Flat and smooth differ in how the normals are built, so the buffer
             * has to be rebuilt; "none" only switches the shader off. */
            if( i != PG_SHADING_NONE && app->has_entity )
                pg_upload_entity_model(
                    &app->entity.model,
                    app->entity.def,
                    app->entity.adjacency,
                    i == PG_SHADING_FLAT);
        }
    }

    cursor_y = y + 86.0f;
    pg_gui_rect(gui, x + 6.0f, cursor_y, w - 12.0f, h - (cursor_y - y) - 6.0f, PG_COL_PANEL_DARK);
    pg_gui_push_clip(gui, x + 6.0f, cursor_y, w - 12.0f, h - (cursor_y - y) - 6.0f);
    for( int i = 0; i < app->entity.component_count; i++ )
    {
        float row_y = cursor_y + 2.0f + (float)i * 17.0f;
        if( row_y > y + h - 20.0f )
            break;
        snprintf(label, sizeof(label), "Model %d", app->entity.components[i].model_id);
        pg_gui_rect(gui, x + 8.0f, row_y, w - 16.0f, PG_ROW, PG_COL_BACKGROUND);
        pg_gui_text_clipped(gui, x + 11.0f, row_y + 4.0f, w - 46.0f, label, PG_COL_TEXT);
        if( pg_gui_button(gui, ID_COMPONENT + i, x + w - 28.0f, row_y, 16.0f, PG_ROW, "x") )
            remove_index = i;
    }
    pg_gui_pop_clip(gui);

    if( remove_index >= 0 )
        pg_app_remove_component(app, remove_index);
}

static void
draw_editor_panel(struct PG_App* app, float x, float y, float w, float h)
{
    struct PG_Gui* gui = &app->gui;
    struct PG_Animation* anim = pg_app_current_animation(app);
    struct PG_Keyframe* keyframe = NULL;
    struct PG_RigJoint* node = NULL;
    static const char* actions[] = { "Add", "Copy", "Paste", "Lerp", "Del" };
    static const char* types[] = { "Move", "Rot", "Scale" };
    static const char* coords[] = { "X", "Y", "Z" };
    char label[96];
    float action_w = (w - 12.0f) / 5.0f;
    float type_w = (w - 12.0f) / 3.0f;
    int length;

    panel(gui, x, y, w, 90.0f, "Keyframe Editor");

    if( anim && anim->keyframe_count > 0 )
        keyframe = &anim->keyframes[pg_animation_frame_index(anim, app->frame_counter)];

    if( keyframe )
        snprintf(label, sizeof(label), "Selected: %d", keyframe->id);
    else
        snprintf(label, sizeof(label), "Selected: N/A");
    pg_gui_text_centred(gui, x, y + 22.0f, w, label, PG_COL_TEXT);

    pg_gui_text(gui, x + 8.0f, y + 42.0f, "Length", PG_COL_TEXT);
    length = keyframe ? keyframe->length : 0;
    if( pg_gui_slider(gui, ID_LENGTH, x + 55.0f, y + 39.0f, w - 63.0f, PG_ROW, &length, 1, 99) &&
        keyframe )
    {
        struct PG_Command command;
        memset(&command, 0, sizeof(command));
        command.kind = PG_CMD_CHANGE_LENGTH;
        command.value = length;
        command.keyframe_index = -1;
        pg_app_execute(app, command);
    }

    for( int i = 0; i < 5; i++ )
    {
        if( pg_gui_button(
                gui, ID_ACTIONS + i, x + 6.0f + (float)i * action_w, y + 62.0f,
                action_w - 2.0f, 20.0f, actions[i]) )
        {
            switch( i )
            {
            case 0:
                pg_app_add_keyframe(app);
                break;
            case 1:
                pg_app_copy_keyframe(app);
                break;
            case 2:
                pg_app_paste_keyframe(app);
                break;
            case 3:
                pg_app_lerp_keyframe(app);
                break;
            default:
                pg_app_delete_keyframe(app);
                break;
            }
        }
    }

    panel(gui, x, y + 95.0f, w, h - 95.0f, "Node Transformer");

    if( keyframe && app->selected_node_id >= 0 )
        node = pg_rig_skeleton_find(&keyframe->skeleton, app->selected_node_id);

    if( node )
        snprintf(label, sizeof(label), "Selected: %d", node->id);
    else
        snprintf(label, sizeof(label), "Selected: N/A");
    pg_gui_text_centred(gui, x, y + 117.0f, w, label, PG_COL_TEXT);

    for( int i = 0; i < 3; i++ )
    {
        int type = PG_RIG_TRANSLATE + i;
        bool enabled = node && node->child_by_kind[type] >= 0;
        if( pg_gui_toggle(
                gui, ID_TYPES + i, x + 6.0f + (float)i * type_w, y + 134.0f, type_w - 2.0f,
                20.0f, types[i], app->selected_type == type, enabled) )
        {
            app->selected_type = type;
            if( node )
            {
                app->gizmo_enabled = true;
                pg_gizmo_set(
                    &app->gizmo,
                    type == PG_RIG_ROTATE
                        ? PG_GIZMO_ROTATION
                        : (type == PG_RIG_SCALE ? PG_GIZMO_SCALE : PG_GIZMO_TRANSLATION),
                    pg_joint_position(node));
            }
        }
    }

    {
        struct PG_RigJoint* target =
            node ? pg_rig_joint_child(&keyframe->skeleton, node, app->selected_type) : NULL;
        for( int i = 0; i < 3; i++ )
        {
            int value = target ? target->delta[i] : 0;
            float row_y = y + 160.0f + (float)i * 20.0f;
            pg_gui_text(gui, x + 10.0f, row_y + 4.0f, coords[i], PG_COL_TEXT);
            if( pg_gui_slider(
                    gui, ID_SLIDERS + i, x + 26.0f, row_y, w - 34.0f, PG_ROW, &value, -255, 255) &&
                target )
            {
                struct PG_Command command;
                memset(&command, 0, sizeof(command));
                command.kind = PG_CMD_TRANSFORM_NODE;
                command.transformation_id = target->id;
                command.coord_index = i;
                command.value = value;
                command.keyframe_index = -1;
                pg_app_execute(app, command);
            }
        }
    }
}

static void
draw_animation_panel(struct PG_App* app, float x, float y, float w, float h)
{
    struct PG_Gui* gui = &app->gui;
    struct PG_Animation* anim = pg_app_current_animation(app);
    float timeline_x = x + 12.0f;
    float timeline_w = w - 24.0f;
    float timeline_y = y + 30.0f;
    float timeline_h = 40.0f;
    int max_length = anim && anim->length > 0 ? anim->length : 50;
    float unit;
    char label[64];

    panel(gui, x, y, w, h, NULL);

    if( pg_gui_button(gui, ID_PLAY, x + 8.0f, y + 4.0f, 52.0f, 18.0f, app->playing ? "Pause" : "Play") )
        pg_app_set_playing(app, !app->playing);
    if( pg_gui_button(gui, ID_PLAY + 1, x + 64.0f, y + 4.0f, 28.0f, 18.0f, "<") )
        pg_app_previous_frame(app);
    if( pg_gui_button(gui, ID_PLAY + 2, x + 96.0f, y + 4.0f, 28.0f, 18.0f, ">") )
        pg_app_next_frame(app);
    if( pg_gui_toggle(
            gui, ID_PLAY + 3, x + 128.0f, y + 4.0f, 70.0f, 18.0f, "Skeleton", app->nodes_enabled,
            true) )
    {
        app->nodes_enabled = !app->nodes_enabled;
        if( !app->nodes_enabled )
        {
            app->selected_node_id = -1;
            app->gizmo_enabled = false;
            app->node_count = 0;
        }
    }

    if( anim )
    {
        snprintf(
            label,
            sizeof(label),
            "seq %d  %d frames  %d cycles%s",
            anim->id,
            anim->keyframe_count,
            anim->length,
            anim->modified ? "  (modified)" : "");
        pg_gui_text_clipped(gui, x + 206.0f, y + 9.0f, w - 214.0f, label, PG_COL_TEXT_DIM);
    }

    pg_gui_rect(gui, timeline_x, timeline_y, timeline_w, timeline_h, PG_COL_PANEL_DARK);
    unit = timeline_w / (float)max_length;

    /* Time ticks, at the same square-root-derived spacing the reference uses so
     * the labels stay legible whether the animation is 20 cycles or 900. */
    {
        int base = 5;
        double root = sqrt((double)max_length);
        int step;
        if( root < base )
            root = base;
        step = base * (int)((root / base) + 0.5);
        if( step <= 0 )
            step = base;
        for( int i = 0; i < max_length; i += step )
        {
            float tick_x = timeline_x + (float)i * unit;
            snprintf(label, sizeof(label), "%d", i);
            if( i != 0 )
                pg_gui_rect(gui, tick_x, timeline_y, 1.0f, timeline_h, PG_COL_WIDGET);
            pg_gui_text(gui, tick_x + 2.0f, timeline_y + timeline_h + 4.0f, label, PG_COL_TEXT_DIM);
        }
    }

    if( anim )
    {
        int cumulative = 0;
        for( int i = 0; i < anim->keyframe_count; i++ )
        {
            float key_x = timeline_x + (float)cumulative * unit;
            bool hovered = pg_gui_hover(gui, key_x - 2.0f, timeline_y, 5.0f, timeline_h);
            pg_gui_rect(
                gui, key_x - 1.0f, timeline_y, 3.0f, timeline_h,
                hovered ? PG_COL_PINK : PG_COL_YELLOW);
            if( hovered && gui->input.pressed[PG_MOUSE_LEFT] )
                pg_app_set_current_frame(app, i, 0);
            cumulative += anim->keyframes[i].length;
        }

        /* Seeking anywhere on the track, not only onto a keyframe. */
        if( pg_gui_hover(gui, timeline_x, timeline_y, timeline_w, timeline_h) )
        {
            gui->mouse_captured = true;
            if( gui->input.down[PG_MOUSE_LEFT] )
            {
                int target = (int)((gui->input.mouse_x - timeline_x) / unit);
                int cursor = 0;
                for( int i = 0; i < anim->keyframe_count; i++ )
                {
                    if( target >= cursor && target < cursor + anim->keyframes[i].length )
                    {
                        pg_app_set_current_frame(app, i, target - cursor);
                        break;
                    }
                    cursor += anim->keyframes[i].length;
                }
            }
        }

        pg_gui_rect(
            gui,
            timeline_x + (float)app->timer * unit - 1.0f,
            timeline_y - 4.0f,
            2.0f,
            timeline_h + 8.0f,
            pg_colour_rgba(app->settings.cursor_colour));
    }
}

/* ---- dialogs ------------------------------------------------------------------------ */

static void
dialog_frame(struct PG_App* app, float w, float h, const char* title, float* out_x, float* out_y)
{
    struct PG_Gui* gui = &app->gui;
    float x = (gui->width - w) / 2.0f;
    float y = (gui->height - h) / 2.0f;

    /* A full-screen catcher, so a click that misses the dialog cannot reach the
     * viewport underneath and move the camera. */
    pg_gui_rect(gui, 0, 0, gui->width, gui->height, PG_RGBA(0, 0, 0, 140));
    if( pg_gui_hover(gui, 0, 0, gui->width, gui->height) )
        gui->mouse_captured = true;

    pg_gui_rect(gui, x, y, w, h, PG_COL_PANEL);
    pg_gui_border(gui, x, y, w, h, PG_COL_WIDGET);
    pg_gui_rect(gui, x, y, w, 18.0f, PG_COL_BACKGROUND);
    pg_gui_text_centred(gui, x, y + 6.0f, w, title, PG_COL_TEXT);
    *out_x = x;
    *out_y = y;
}

static void
draw_dialog(struct PG_App* app)
{
    struct PG_Gui* gui = &app->gui;
    float x, y;

    switch( app->dialog )
    {
    case PG_DIALOG_MESSAGE:
        dialog_frame(app, 300.0f, 90.0f, app->dialog_title, &x, &y);
        pg_gui_text_centred(gui, x, y + 34.0f, 300.0f, app->dialog_message, PG_COL_TEXT);
        if( pg_gui_button(gui, ID_DIALOG, x + 120.0f, y + 58.0f, 60.0f, 20.0f, "OK") )
            app->dialog = PG_DIALOG_NONE;
        break;

    case PG_DIALOG_EXPORT:
        dialog_frame(app, 420.0f, 110.0f, "Export Animation", &x, &y);
        pg_gui_text(gui, x + 10.0f, y + 28.0f, "Path", PG_COL_TEXT);
        pg_gui_text_input(
            gui, ID_DIALOG + 1, x + 45.0f, y + 25.0f, 365.0f, 18.0f, app->path_buffer,
            (int)sizeof(app->path_buffer), "seq.pgl");
        if( pg_gui_button(gui, ID_DIALOG + 2, x + 45.0f, y + 55.0f, 90.0f, 20.0f, "Export PGL") )
        {
            if( pg_export_pgl(app, app->path_buffer) )
                app->dialog = PG_DIALOG_NONE;
        }
        if( pg_gui_button(gui, ID_DIALOG + 3, x + 143.0f, y + 55.0f, 90.0f, 20.0f, "Export DAT") )
        {
            if( pg_export_dat(app, app->path_buffer) )
                app->dialog = PG_DIALOG_NONE;
        }
        if( pg_gui_button(gui, ID_DIALOG + 4, x + 320.0f, y + 55.0f, 90.0f, 20.0f, "Cancel") )
            app->dialog = PG_DIALOG_NONE;
        break;

    case PG_DIALOG_IMPORT:
        dialog_frame(app, 420.0f, 110.0f, "Import Animation", &x, &y);
        pg_gui_text(gui, x + 10.0f, y + 28.0f, "Path", PG_COL_TEXT);
        pg_gui_text_input(
            gui, ID_DIALOG + 5, x + 45.0f, y + 25.0f, 365.0f, 18.0f, app->path_buffer,
            (int)sizeof(app->path_buffer), "animation.pgl");
        if( pg_gui_button(gui, ID_DIALOG + 6, x + 45.0f, y + 55.0f, 90.0f, 20.0f, "Import") )
        {
            if( pg_import_pgl(app, app->path_buffer) )
                app->dialog = PG_DIALOG_NONE;
        }
        if( pg_gui_button(gui, ID_DIALOG + 7, x + 320.0f, y + 55.0f, 90.0f, 20.0f, "Cancel") )
            app->dialog = PG_DIALOG_NONE;
        break;

    case PG_DIALOG_PACK:
        dialog_frame(app, 460.0f, 130.0f, "Pack Into Cache", &x, &y);
        pg_gui_text_clipped(
            gui,
            x + 10.0f,
            y + 26.0f,
            440.0f,
            "Writes the animation to a copy of the cache in this directory.",
            PG_COL_TEXT_DIM);
        pg_gui_text(gui, x + 10.0f, y + 50.0f, "Out", PG_COL_TEXT);
        pg_gui_text_input(
            gui, ID_DIALOG + 8, x + 45.0f, y + 47.0f, 405.0f, 18.0f, app->path_buffer,
            (int)sizeof(app->path_buffer), "cache.packed");
        if( pg_gui_button(gui, ID_DIALOG + 9, x + 45.0f, y + 77.0f, 90.0f, 20.0f, "Pack") )
        {
            if( pg_pack_animation(app, app->path_buffer) )
                app->dialog = PG_DIALOG_NONE;
        }
        if( pg_gui_button(gui, ID_DIALOG + 10, x + 360.0f, y + 77.0f, 90.0f, 20.0f, "Cancel") )
            app->dialog = PG_DIALOG_NONE;
        break;

    case PG_DIALOG_SETTINGS:
    {
        float row = 0.0f;
        int camera = (int)(app->settings.camera_sensitivity * 10.0f);
        int zoom = (int)(app->settings.zoom_sensitivity * 10.0f);

        dialog_frame(app, 340.0f, 190.0f, "Settings", &x, &y);

        pg_gui_text(gui, x + 12.0f, y + 30.0f + row, "Background", PG_COL_TEXT);
        if( pg_gui_button(
                gui, ID_DIALOG + 11, x + 200.0f, y + 27.0f + row, 120.0f, 18.0f,
                pg_colour_name(app->settings.background)) )
            app->settings.background = (app->settings.background + 1) % PG_COLOUR_COUNT;
        row += 24.0f;

        pg_gui_text(gui, x + 12.0f, y + 30.0f + row, "Cursor", PG_COL_TEXT);
        if( pg_gui_button(
                gui, ID_DIALOG + 12, x + 200.0f, y + 27.0f + row, 120.0f, 18.0f,
                pg_colour_name(app->settings.cursor_colour)) )
            app->settings.cursor_colour = (app->settings.cursor_colour + 1) % PG_COLOUR_COUNT;
        row += 24.0f;

        pg_gui_text(gui, x + 12.0f, y + 30.0f + row, "Camera x10", PG_COL_TEXT);
        if( pg_gui_slider(
                gui, ID_DIALOG + 13, x + 200.0f, y + 27.0f + row, 120.0f, 18.0f, &camera, 1, 50) )
            app->settings.camera_sensitivity = (float)camera / 10.0f;
        row += 24.0f;

        pg_gui_text(gui, x + 12.0f, y + 30.0f + row, "Zoom x10", PG_COL_TEXT);
        if( pg_gui_slider(
                gui, ID_DIALOG + 14, x + 200.0f, y + 27.0f + row, 120.0f, 18.0f, &zoom, 1, 50) )
            app->settings.zoom_sensitivity = (float)zoom / 10.0f;
        row += 24.0f;

        pg_gui_text(gui, x + 12.0f, y + 30.0f + row, "Grid", PG_COL_TEXT);
        if( pg_gui_toggle(
                gui, ID_DIALOG + 15, x + 200.0f, y + 27.0f + row, 120.0f, 18.0f,
                app->settings.grid_active ? "On" : "Off", app->settings.grid_active, true) )
            app->settings.grid_active = !app->settings.grid_active;
        row += 24.0f;

        pg_gui_text(gui, x + 12.0f, y + 30.0f + row, "Bones", PG_COL_TEXT);
        if( pg_gui_toggle(
                gui, ID_DIALOG + 16, x + 200.0f, y + 27.0f + row, 120.0f, 18.0f,
                app->settings.bones_active ? "On" : "Off", app->settings.bones_active, true) )
            app->settings.bones_active = !app->settings.bones_active;
        row += 24.0f;

        pg_gui_text(gui, x + 12.0f, y + 30.0f + row, "Advanced", PG_COL_TEXT);
        if( pg_gui_toggle(
                gui, ID_DIALOG + 17, x + 200.0f, y + 27.0f + row, 120.0f, 18.0f,
                app->settings.advanced_mode ? "On" : "Off", app->settings.advanced_mode, true) )
        {
            /* Advanced mode changes which transforms a keyframe carries, so every
             * already-parsed animation has to be re-read for it to take effect. */
            struct PG_Animation* current = pg_app_current_animation(app);
            app->settings.advanced_mode = !app->settings.advanced_mode;
            if( current && !current->modified )
                pg_animation_reload(current, app->cache, app->settings.advanced_mode);
        }

        if( pg_gui_button(gui, ID_DIALOG + 18, x + 130.0f, y + 160.0f, 80.0f, 20.0f, "Close") )
        {
            pg_settings_save(&app->settings);
            app->dialog = PG_DIALOG_NONE;
        }
        break;
    }

    case PG_DIALOG_ABOUT:
        dialog_frame(app, 380.0f, 130.0f, "About", &x, &y);
        pg_gui_text_centred(gui, x, y + 30.0f, 380.0f, "poser-gl-c", PG_COL_TEXT);
        pg_gui_text_centred(
            gui, x, y + 46.0f, 380.0f, "A C port of poser-gl, on rscache and SDL2", PG_COL_TEXT_DIM);
        pg_gui_text_centred(gui, x, y + 66.0f, 380.0f, pg_cache_path(app->cache), PG_COL_TEXT_DIM);
        pg_gui_text_centred(gui, x, y + 80.0f, 380.0f, pg_cache_rev(app->cache), PG_COL_TEXT_DIM);
        if( pg_gui_button(gui, ID_DIALOG + 19, x + 160.0f, y + 100.0f, 60.0f, 20.0f, "OK") )
            app->dialog = PG_DIALOG_NONE;
        break;

    default:
        break;
    }
}

/* ---- entry ------------------------------------------------------------------------- */

void
pg_ui_draw(struct PG_App* app)
{
    struct PG_Gui* gui = &app->gui;
    float width = gui->width;
    float height = gui->height;
    float main_y = PG_MARGIN + PG_MENU_HEIGHT + 2.0f;
    float main_h = height - main_y - PG_TIMELINE_HEIGHT - PG_MARGIN * 2.0f;
    float right_x = width - PG_MARGIN - PG_RIGHT_WIDTH;
    float manager_h = main_h - PG_EDITOR_HEIGHT - 5.0f;

    if( main_h < 120.0f )
        main_h = 120.0f;
    if( manager_h < 60.0f )
        manager_h = 60.0f;

    draw_menu_bar(app, PG_MARGIN, PG_MARGIN, width - PG_MARGIN * 2.0f);
    draw_list_panel(app, PG_MARGIN, main_y, PG_LIST_WIDTH, main_h);
    draw_manager_panel(app, right_x, main_y, PG_RIGHT_WIDTH, manager_h);
    draw_editor_panel(
        app, right_x, main_y + manager_h + 5.0f, PG_RIGHT_WIDTH, PG_EDITOR_HEIGHT);
    draw_animation_panel(
        app,
        PG_MARGIN,
        height - PG_MARGIN - PG_TIMELINE_HEIGHT,
        width - PG_MARGIN * 2.0f,
        PG_TIMELINE_HEIGHT);

    app->view_x = (int)(PG_MARGIN + PG_LIST_WIDTH + PG_MARGIN);
    app->view_y = (int)main_y;
    app->view_w = (int)(right_x - PG_MARGIN - (float)app->view_x);
    app->view_h = (int)main_h;
    if( app->view_w < 32 )
        app->view_w = 32;
    if( app->view_h < 32 )
        app->view_h = 32;

    if( app->dialog != PG_DIALOG_NONE )
        draw_dialog(app);
}
