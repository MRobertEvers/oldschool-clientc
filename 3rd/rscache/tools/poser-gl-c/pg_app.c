#include "pg_app.h"

#include "pg_font.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* poser-gl downscales RS2-era models, whose vertices are stored four times
 * larger than the OSRS ones the camera framing was tuned against. */
#define PG_HIGHER_REV_SCALE 4.0f

/* ---- colours and settings ---------------------------------------------------- */

const char*
pg_colour_name(int colour)
{
    static const char* names[] = { "Gray", "Black", "White", "Red", "Green", "Blue" };
    if( colour < 0 || colour >= PG_COLOUR_COUNT )
        return "Gray";
    return names[colour];
}

uint32_t
pg_colour_rgba(int colour)
{
    switch( colour )
    {
    case PG_COLOUR_BLACK:
        return PG_RGB(0, 0, 0);
    case PG_COLOUR_WHITE:
        return PG_RGB(255, 255, 255);
    case PG_COLOUR_RED:
        return PG_RGB(255, 102, 102);
    case PG_COLOUR_GREEN:
        return PG_RGB(102, 255, 102);
    case PG_COLOUR_BLUE:
        return PG_RGB(102, 178, 255);
    default:
        return PG_RGB(33, 33, 33);
    }
}

void
pg_settings_defaults(struct PG_Settings* settings)
{
    settings->background = PG_COLOUR_GRAY;
    settings->cursor_colour = PG_COLOUR_GREEN;
    settings->camera_sensitivity = 1.0f;
    settings->zoom_sensitivity = 1.0f;
    settings->grid_active = true;
    settings->bones_active = true;
    settings->advanced_mode = false;
}

static int
colour_from_name(const char* name)
{
    for( int i = 0; i < PG_COLOUR_COUNT; i++ )
        if( strcmp(pg_colour_name(i), name) == 0 )
            return i;
    return PG_COLOUR_GRAY;
}

void
pg_settings_load(struct PG_Settings* settings)
{
    FILE* file = fopen(PG_SETTINGS_FILE, "r");
    char line[256];

    pg_settings_defaults(settings);
    if( !file )
        return;
    while( fgets(line, sizeof(line), file) )
    {
        char key[64];
        char value[128];
        if( sscanf(line, "%63[^=]=%127[^\n]", key, value) != 2 )
            continue;
        if( strcmp(key, "background") == 0 )
            settings->background = colour_from_name(value);
        else if( strcmp(key, "cursorColour") == 0 )
            settings->cursor_colour = colour_from_name(value);
        else if( strcmp(key, "cameraSensitivity") == 0 )
            settings->camera_sensitivity = (float)atof(value);
        else if( strcmp(key, "zoomSensitivity") == 0 )
            settings->zoom_sensitivity = (float)atof(value);
        else if( strcmp(key, "grid") == 0 )
            settings->grid_active = strcmp(value, "true") == 0;
        else if( strcmp(key, "bones") == 0 )
            settings->bones_active = strcmp(value, "true") == 0;
        else if( strcmp(key, "advanced") == 0 )
            settings->advanced_mode = strcmp(value, "true") == 0;
    }
    fclose(file);
}

void
pg_settings_save(const struct PG_Settings* settings)
{
    FILE* file = fopen(PG_SETTINGS_FILE, "w");
    if( !file )
        return;
    fprintf(file, "background=%s\n", pg_colour_name(settings->background));
    fprintf(file, "cursorColour=%s\n", pg_colour_name(settings->cursor_colour));
    fprintf(file, "cameraSensitivity=%f\n", (double)settings->camera_sensitivity);
    fprintf(file, "zoomSensitivity=%f\n", (double)settings->zoom_sensitivity);
    fprintf(file, "grid=%s\n", settings->grid_active ? "true" : "false");
    fprintf(file, "bones=%s\n", settings->bones_active ? "true" : "false");
    fprintf(file, "advanced=%s\n", settings->advanced_mode ? "true" : "false");
    fclose(file);
}

/* ---- messages ------------------------------------------------------------------ */

void
pg_app_message(struct PG_App* app, const char* title, const char* message)
{
    app->dialog = PG_DIALOG_MESSAGE;
    snprintf(app->dialog_title, sizeof(app->dialog_title), "%s", title);
    snprintf(app->dialog_message, sizeof(app->dialog_message), "%s", message);
}

void
pg_app_status(struct PG_App* app, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(app->status, sizeof(app->status), format, args);
    va_end(args);
}

/* ---- entity ---------------------------------------------------------------------- */

static void
entity_release(struct PG_Entity* entity)
{
    pg_gl_model_free(&entity->model);
    pg_normals_free(entity->adjacency);
    pg_model_free(entity->def);
    entity->adjacency = NULL;
    entity->def = NULL;
}

void
pg_app_rebuild_entity(struct PG_App* app)
{
    struct PG_Entity* entity = &app->entity;
    struct PG_ModelDef** parts;
    struct PG_ModelDef* merged;
    int part_count = 0;

    if( entity->component_count <= 0 )
        return;

    parts = calloc((size_t)entity->component_count, sizeof(*parts));
    if( !parts )
        return;
    for( int i = 0; i < entity->component_count; i++ )
    {
        struct PG_EntityComponent* component = &entity->components[i];
        struct PG_ModelDef* def = pg_cache_load_model(
            app->cache,
            component->model_id,
            component->recolor_from,
            component->recolor_to,
            component->recolor_count);
        if( def )
            parts[part_count++] = def;
    }

    if( part_count == 1 )
    {
        merged = parts[0];
    }
    else
    {
        merged = pg_model_merge(parts, part_count);
        for( int i = 0; i < part_count; i++ )
            pg_model_free(parts[i]);
    }
    free(parts);
    if( !merged )
        return;

    pg_model_compute_groups(merged);

    entity_release(entity);
    entity->def = merged;
    entity->adjacency = pg_normals_build(merged);
    pg_upload_entity_model(
        &entity->model, merged, entity->adjacency, app->shading_type == PG_SHADING_FLAT);

    entity->scale = app->entity_scale;
    pg_renderer_set_grid(&app->renderer, entity->size);
    app->has_entity = true;
}

void
pg_app_load_npc(struct PG_App* app, const struct PG_NpcDef* npc)
{
    struct PG_Entity* entity = &app->entity;

    if( !npc || npc->model_count <= 0 )
        return;

    pg_app_reset_animation(app);
    entity_release(entity);
    memset(entity, 0, sizeof(*entity));

    snprintf(entity->name, sizeof(entity->name), "%s", npc->name ? npc->name : "null");
    entity->size = npc->size > 0 ? npc->size : 1;

    for( int i = 0; i < npc->model_count && entity->component_count < PG_MAX_COMPONENTS; i++ )
    {
        struct PG_EntityComponent* component = &entity->components[entity->component_count];
        bool duplicate = false;
        for( int j = 0; j < entity->component_count; j++ )
            if( entity->components[j].model_id == npc->models[i] )
                duplicate = true;
        if( duplicate )
            continue;
        memset(component, 0, sizeof(*component));
        component->model_id = npc->models[i];
        component->recolor_from = npc->recolor_from;
        component->recolor_to = npc->recolor_to;
        component->recolor_count = npc->recolor_count;
        entity->component_count++;
    }
    pg_app_rebuild_entity(app);
}

void
pg_app_remove_component(struct PG_App* app, int index)
{
    struct PG_Entity* entity = &app->entity;
    if( index < 0 || index >= entity->component_count || entity->component_count <= 1 )
        return;
    memmove(
        &entity->components[index],
        &entity->components[index + 1],
        (size_t)(entity->component_count - index - 1) * sizeof(entity->components[0]));
    entity->component_count--;
    pg_app_rebuild_entity(app);
}

void
pg_app_toggle_item(struct PG_App* app, int item_id, bool equip)
{
    const struct PG_ItemDef* item;
    struct PG_Entity* entity = &app->entity;
    int before = entity->component_count;
    int models[3];

    if( !app->has_entity )
        return;
    item = pg_cache_item(app->cache, item_id);
    if( !item )
        return;
    models[0] = item->model0;
    models[1] = item->model1;
    models[2] = item->model2;

    if( equip )
    {
        for( int i = 0; i < 3; i++ )
        {
            bool present = false;
            struct PG_EntityComponent* component;
            if( models[i] <= 0 || entity->component_count >= PG_MAX_COMPONENTS )
                continue;
            for( int j = 0; j < entity->component_count; j++ )
                if( entity->components[j].model_id == models[i] )
                    present = true;
            if( present )
                continue;
            component = &entity->components[entity->component_count++];
            memset(component, 0, sizeof(*component));
            component->model_id = models[i];
            component->recolor_from = item->recolor_from;
            component->recolor_to = item->recolor_to;
            component->recolor_count = item->recolor_count;
            component->from_item = true;
            component->item_id = item->id;
        }
    }
    else
    {
        int write = 0;
        for( int i = 0; i < entity->component_count; i++ )
        {
            bool drop = false;
            for( int m = 0; m < 3; m++ )
                if( models[m] > 0 && entity->components[i].model_id == models[m] )
                    drop = true;
            if( !drop )
                entity->components[write++] = entity->components[i];
        }
        entity->component_count = write;
    }

    /* Only rebuild when the composition actually changed, matching the
     * reference — equipping something already worn should not restart the pose. */
    if( entity->component_count != before )
        pg_app_rebuild_entity(app);
}

/* ---- animation list -------------------------------------------------------------- */

struct PG_Animation*
pg_app_current_animation(struct PG_App* app)
{
    if( app->current_animation < 0 || app->current_animation >= app->animation_count )
        return NULL;
    return app->animations[app->current_animation];
}

void
pg_app_add_animation(struct PG_App* app, struct PG_Animation* animation)
{
    if( app->animation_count == app->animation_capacity )
    {
        int next = app->animation_capacity ? app->animation_capacity * 2 : 1024;
        struct PG_Animation** grown = realloc(app->animations, (size_t)next * sizeof(*grown));
        if( !grown )
            return;
        app->animations = grown;
        app->animation_capacity = next;
    }
    app->animations[app->animation_count++] = animation;
    app->current_animation = app->animation_count - 1;
    app->lists[PG_LIST_SEQUENCE].dirty = true;
}

static int
animation_max_id(const struct PG_App* app)
{
    int max = -1;
    for( int i = 0; i < app->animation_count; i++ )
        if( app->animations[i]->id > max )
            max = app->animations[i]->id;
    return max;
}

struct PG_Animation*
pg_app_animation_or_copy(struct PG_App* app)
{
    struct PG_Animation* current = pg_app_current_animation(app);
    struct PG_Animation* copy;

    if( !current )
        return NULL;
    if( current->modified )
        return current;

    copy = pg_animation_copy(current, animation_max_id(app) + 1);
    if( !copy )
        return NULL;
    pg_app_add_animation(app, copy);
    pg_app_status(app, "Editing copy as sequence %d", copy->id);
    return copy;
}

/* ---- playback --------------------------------------------------------------------- */

void
pg_app_set_playing(struct PG_App* app, bool playing)
{
    if( playing && !pg_app_current_animation(app) )
        return;
    app->playing = playing;
}

void
pg_app_set_current_frame(struct PG_App* app, int frame, int offset)
{
    struct PG_Animation* anim = pg_app_current_animation(app);
    int index;
    int cumulative = 0;

    if( !anim || anim->keyframe_count == 0 )
        return;
    app->frame_counter = pg_animation_frame_index(anim, frame);
    index = pg_animation_frame_index(anim, app->frame_counter);
    for( int i = 0; i < index; i++ )
        cumulative += anim->keyframes[i].length;
    app->timer = cumulative + offset;
    app->frame_length = anim->keyframes[index].length - offset;
}

void
pg_app_next_frame(struct PG_App* app)
{
    pg_app_set_current_frame(app, app->frame_counter + 1, 0);
}

void
pg_app_previous_frame(struct PG_App* app)
{
    pg_app_set_current_frame(app, app->frame_counter - 1, 0);
}

static void
animation_toggle_items(struct PG_App* app, struct PG_Animation* anim, bool equip)
{
    const int items[2] = { anim->left_hand_item, anim->right_hand_item };
    for( int i = 0; i < 2; i++ )
    {
        /* The sequence stores the item id offset by 512; anything below that is
         * not an item reference at all. */
        if( items[i] < PG_ITEM_OFFSET )
            continue;
        pg_app_toggle_item(app, items[i] - PG_ITEM_OFFSET, equip);
    }
}

void
pg_app_reset_animation(struct PG_App* app)
{
    struct PG_Animation* previous = pg_app_current_animation(app);
    if( !previous )
        return;
    app->current_animation = -1;
    animation_toggle_items(app, previous, false);

    app->timer = 0;
    app->frame_counter = 0;
    app->frame_length = 0;
    app->previous_keyframe_id = -1;
    app->playing = false;
    pg_app_history_reset(app);
    app->selected_node_id = -1;
    app->gizmo_enabled = false;
    app->node_count = 0;

    if( app->entity.def )
    {
        pg_model_reset_transformations(app->entity.def);
        pg_upload_entity_model(
            &app->entity.model,
            app->entity.def,
            app->entity.adjacency,
            app->shading_type == PG_SHADING_FLAT);
    }
}

void
pg_app_select_animation(struct PG_App* app, int index)
{
    struct PG_Animation* anim;

    if( index < 0 || index >= app->animation_count )
        return;
    pg_app_reset_animation(app);
    anim = app->animations[index];
    pg_animation_load(anim, app->cache, app->settings.advanced_mode);
    if( anim->keyframe_count == 0 )
    {
        pg_app_status(app, "Sequence %d has no usable frames", anim->id);
        return;
    }

    app->current_animation = index;
    animation_toggle_items(app, anim, true);
    app->frame_length = anim->keyframes[0].length;
    app->frame_counter = 0;
    app->timer = 0;
    pg_app_set_playing(app, true);
    pg_app_status(app, "Sequence %d, %d keyframes", anim->id, anim->keyframe_count);
}

/* ---- undo history ---------------------------------------------------------------- */

static struct PG_Animation*
animation_by_id(struct PG_App* app, int id)
{
    for( int i = 0; i < app->animation_count; i++ )
        if( app->animations[i]->id == id )
            return app->animations[i];
    return NULL;
}

static void
command_release(struct PG_Command* command)
{
    if( command->has_removed )
        pg_keyframe_free_contents(&command->removed);
    command->has_removed = false;
}

void
pg_app_history_reset(struct PG_App* app)
{
    for( int i = 0; i < app->history_count; i++ )
        command_release(&app->history[i]);
    app->history_count = 0;
    app->history_index = 0;
}

static void
history_push(struct PG_App* app, struct PG_Command command)
{
    /* Everything after the insertion point is unreachable once a new command
     * lands, so it is dropped rather than kept as a second branch. */
    for( int i = app->history_index; i < app->history_count; i++ )
        command_release(&app->history[i]);
    app->history_count = app->history_index;

    if( app->history_count == app->history_capacity )
    {
        int next = app->history_capacity ? app->history_capacity * 2 : 64;
        struct PG_Command* grown = realloc(app->history, (size_t)next * sizeof(*grown));
        if( !grown )
            return;
        app->history = grown;
        app->history_capacity = next;
    }
    app->history[app->history_count++] = command;
    app->history_index = app->history_count;
}

static void
animation_updated(struct PG_App* app, struct PG_Animation* anim)
{
    anim->length = pg_animation_calculate_length(anim);
    pg_app_set_playing(app, false);
    pg_app_set_current_frame(app, app->frame_counter, 0);
}

static bool
command_execute(struct PG_App* app, struct PG_Command* command, bool redo)
{
    struct PG_Animation* anim;
    struct PG_Keyframe* keyframe;

    switch( command->kind )
    {
    case PG_CMD_TRANSFORM_NODE:
    {
        struct PG_Transformation* target;
        anim = redo ? animation_by_id(app, command->animation_id) : pg_app_animation_or_copy(app);
        if( !anim )
            return false;
        command->animation_id = anim->id;
        if( !redo )
            command->keyframe_index = pg_animation_frame_index(anim, app->frame_counter);
        if( command->keyframe_index < 0 || command->keyframe_index >= anim->keyframe_count )
            return false;
        keyframe = &anim->keyframes[command->keyframe_index];
        target = pg_keyframe_find(keyframe, command->transformation_id);
        if( !target )
            return false;
        if( !redo )
            command->previous_value = target->delta[command->coord_index];
        target->delta[command->coord_index] = command->value;
        keyframe->modified = true;
        return true;
    }
    case PG_CMD_CHANGE_LENGTH:
        anim = redo ? animation_by_id(app, command->animation_id) : pg_app_animation_or_copy(app);
        if( !anim )
            return false;
        command->animation_id = anim->id;
        if( !redo )
            command->keyframe_index = pg_animation_frame_index(anim, app->frame_counter);
        if( command->keyframe_index < 0 || command->keyframe_index >= anim->keyframe_count )
            return false;
        keyframe = &anim->keyframes[command->keyframe_index];
        if( !redo )
            command->previous_value = keyframe->length;
        keyframe->length = command->value;
        keyframe->modified = true;
        animation_updated(app, anim);
        return true;

    case PG_CMD_ADD_KEYFRAME:
    case PG_CMD_PASTE_KEYFRAME:
    case PG_CMD_LERP_KEYFRAME:
    {
        struct PG_Keyframe source;
        bool inserted;

        anim = redo ? animation_by_id(app, command->animation_id) : pg_app_animation_or_copy(app);
        if( !anim || anim->keyframe_count == 0 )
            return false;
        command->animation_id = anim->id;
        if( !redo )
            command->keyframe_index = pg_animation_frame_index(anim, app->frame_counter) + 1;

        if( command->kind == PG_CMD_PASTE_KEYFRAME )
        {
            if( !app->has_copied )
                return false;
            if( app->copied.framemap && anim->keyframes[0].framemap &&
                app->copied.framemap->id != anim->keyframes[0].framemap->id )
            {
                pg_app_message(app, "Invalid Operation", "Skeletons do not match");
                return false;
            }
            pg_keyframe_copy(&source, &app->copied, anim->keyframe_count);
        }
        else if( command->kind == PG_CMD_ADD_KEYFRAME )
        {
            int previous = command->keyframe_index - 1;
            if( previous < 0 || previous >= anim->keyframe_count )
                previous = anim->keyframe_count - 1;
            pg_keyframe_copy(&source, &anim->keyframes[previous], anim->keyframe_count);
        }
        else
        {
            int index = command->keyframe_index - 1;
            int next_index;
            struct PG_Keyframe* first;
            struct PG_Keyframe* second;
            struct PG_Keyframe* largest;
            struct PG_Keyframe* smallest;

            if( anim->keyframe_count <= 1 )
            {
                pg_app_message(app, "Invalid Operation", "Insufficient number of keyframes");
                return false;
            }
            if( index < 0 || index >= anim->keyframe_count )
                index = 0;
            next_index = (index + 1) % anim->keyframe_count;
            first = &anim->keyframes[index];
            second = &anim->keyframes[next_index];
            if( first->framemap && second->framemap && first->framemap->id != second->framemap->id )
            {
                pg_app_message(app, "Invalid Operation", "Skeletons do not match");
                return false;
            }
            largest = first->transformation_count > second->transformation_count ? first : second;
            smallest = largest == first ? second : first;
            pg_keyframe_copy(&source, largest, anim->keyframe_count);
            /* Halfway between the two, transform by transform. Walking the
             * smaller list bounds the pairing; the extra transforms of the larger
             * keep whatever they had. */
            for( int i = 0; i < smallest->transformation_count && i < source.transformation_count;
                 i++ )
            {
                for( int k = 0; k < 3; k++ )
                {
                    float a = (float)first->transformations[i].delta[k];
                    float b = (float)second->transformations[i].delta[k];
                    source.transformations[i].delta[k] = (int)(a + (b - a) * 0.5f);
                }
            }
            source.modified = true;
        }

        inserted = pg_animation_insert_keyframe(anim, &source, command->keyframe_index);
        pg_keyframe_free_contents(&source);
        if( !inserted )
        {
            pg_app_message(app, "Invalid Operation", "Max animation length reached");
            return false;
        }
        pg_app_set_current_frame(app, command->keyframe_index, 0);
        animation_updated(app, anim);
        return true;
    }

    case PG_CMD_DELETE_KEYFRAME:
        anim = redo ? animation_by_id(app, command->animation_id) : NULL;
        if( !redo )
        {
            struct PG_Animation* current = pg_app_current_animation(app);
            if( !current )
                return false;
            if( current->keyframe_count <= 1 )
            {
                pg_app_message(app, "Invalid Operation", "Unable to delete the last keyframe");
                return false;
            }
            anim = pg_app_animation_or_copy(app);
            if( !anim )
                return false;
            command->animation_id = anim->id;
            command->keyframe_index = pg_animation_frame_index(anim, app->frame_counter);
        }
        if( !anim || command->keyframe_index < 0 || command->keyframe_index >= anim->keyframe_count )
            return false;
        command_release(command);
        pg_keyframe_copy(
            &command->removed,
            &anim->keyframes[command->keyframe_index],
            anim->keyframes[command->keyframe_index].id);
        command->has_removed = true;
        pg_animation_remove_keyframe_at(anim, command->keyframe_index);
        animation_updated(app, anim);
        return true;

    default:
        return false;
    }
}

static void
command_unexecute(struct PG_App* app, struct PG_Command* command)
{
    struct PG_Animation* anim = animation_by_id(app, command->animation_id);
    struct PG_Keyframe* keyframe;

    if( !anim )
        return;
    switch( command->kind )
    {
    case PG_CMD_TRANSFORM_NODE:
        if( command->keyframe_index < 0 || command->keyframe_index >= anim->keyframe_count )
            return;
        keyframe = &anim->keyframes[command->keyframe_index];
        {
            struct PG_Transformation* target =
                pg_keyframe_find(keyframe, command->transformation_id);
            if( target )
                target->delta[command->coord_index] = command->previous_value;
        }
        return;
    case PG_CMD_CHANGE_LENGTH:
        if( command->keyframe_index < 0 || command->keyframe_index >= anim->keyframe_count )
            return;
        anim->keyframes[command->keyframe_index].length = command->previous_value;
        animation_updated(app, anim);
        return;
    case PG_CMD_ADD_KEYFRAME:
    case PG_CMD_PASTE_KEYFRAME:
    case PG_CMD_LERP_KEYFRAME:
        pg_animation_remove_keyframe_at(anim, command->keyframe_index);
        animation_updated(app, anim);
        return;
    case PG_CMD_DELETE_KEYFRAME:
        if( command->has_removed )
            pg_animation_insert_keyframe(anim, &command->removed, command->keyframe_index);
        animation_updated(app, anim);
        return;
    default:
        return;
    }
}

bool
pg_app_execute(struct PG_App* app, struct PG_Command command)
{
    if( !command_execute(app, &command, false) )
    {
        command_release(&command);
        return false;
    }
    history_push(app, command);
    return true;
}

void
pg_app_undo(struct PG_App* app)
{
    if( app->history_index <= 0 )
        return;
    command_unexecute(app, &app->history[--app->history_index]);
}

void
pg_app_redo(struct PG_App* app)
{
    if( app->history_index >= app->history_count )
        return;
    command_execute(app, &app->history[app->history_index++], true);
}

/* ---- keyframe actions --------------------------------------------------------------- */

static void
push_simple_command(struct PG_App* app, int kind)
{
    struct PG_Command command;
    memset(&command, 0, sizeof(command));
    command.kind = kind;
    command.keyframe_index = -1;
    pg_app_execute(app, command);
}

void
pg_app_add_keyframe(struct PG_App* app)
{
    push_simple_command(app, PG_CMD_ADD_KEYFRAME);
}

void
pg_app_paste_keyframe(struct PG_App* app)
{
    push_simple_command(app, PG_CMD_PASTE_KEYFRAME);
}

void
pg_app_lerp_keyframe(struct PG_App* app)
{
    push_simple_command(app, PG_CMD_LERP_KEYFRAME);
}

void
pg_app_delete_keyframe(struct PG_App* app)
{
    push_simple_command(app, PG_CMD_DELETE_KEYFRAME);
}

void
pg_app_copy_keyframe(struct PG_App* app)
{
    struct PG_Animation* anim = pg_app_current_animation(app);
    int index;

    if( !anim || anim->keyframe_count == 0 )
        return;
    index = pg_animation_frame_index(anim, app->frame_counter);
    if( app->has_copied )
        pg_keyframe_free_contents(&app->copied);
    pg_keyframe_copy(&app->copied, &anim->keyframes[index], anim->keyframes[index].id);
    app->has_copied = true;
    /* Copy is not undoable in the reference either — nothing in the document
     * changed, so there is nothing to reverse. */
    pg_app_status(app, "Copied keyframe %d", anim->keyframes[index].id);
}

/* ---- nodes ------------------------------------------------------------------------- */

static void
node_push(struct PG_App* app, int index)
{
    if( app->node_count == app->node_capacity )
    {
        int next = app->node_capacity ? app->node_capacity * 2 : 64;
        int* grown = realloc(app->node_indices, (size_t)next * sizeof(int));
        if( !grown )
            return;
        app->node_indices = grown;
        app->node_capacity = next;
    }
    app->node_indices[app->node_count++] = index;
}

static float
node_scale(const struct PG_App* app)
{
    if( !app->has_entity )
        return 0.0f;
    return 2.5f + (float)app->entity.size;
}

static void
collect_nodes(struct PG_App* app, struct PG_Keyframe* keyframe)
{
    float divisor = pg_cache_is_higher_rev(app->cache) ? PG_HIGHER_REV_SCALE : 1.0f;

    app->node_count = 0;
    if( !app->nodes_enabled || !app->entity.def )
        return;

    for( int i = 0; i < keyframe->transformation_count; i++ )
    {
        struct PG_Transformation* node = &keyframe->transformations[i];
        bool can_display;
        bool is_origin;

        if( !node->is_reference )
            continue;
        pg_reference_set_position(node, app->entity.def, divisor);

        /* A reference with no rotation is a pivot the animation never turns, and
         * showing it clutters the rig with joints nothing can be done to. */
        can_display = node->child_by_type[PG_TF_ROTATION] >= 0 || app->settings.advanced_mode;
        is_origin = node->position.x == 0.0f && node->position.y == 0.0f &&
                    node->position.z == 0.0f;
        if( !can_display || is_origin )
            continue;

        node->highlighted = false;
        node_push(app, i);
    }
}

static struct PG_Transformation*
selected_node(struct PG_App* app, struct PG_Keyframe* keyframe)
{
    if( app->selected_node_id < 0 || !keyframe )
        return NULL;
    for( int i = 0; i < app->node_count; i++ )
    {
        struct PG_Transformation* node = &keyframe->transformations[app->node_indices[i]];
        if( node->id == app->selected_node_id )
            return node;
    }
    return NULL;
}

static void
select_node(struct PG_App* app, struct PG_Transformation* node)
{
    app->selected_node_id = node->id;
    if( node->child_by_type[app->selected_type] < 0 )
    {
        /* Fall back to whatever this joint does have, in the order the framemap
         * listed them. */
        app->selected_type = PG_TF_TRANSLATION;
        for( int t = PG_TF_TRANSLATION; t <= PG_TF_SCALE; t++ )
        {
            if( node->child_by_type[t] >= 0 )
            {
                app->selected_type = t;
                break;
            }
        }
    }
    app->gizmo_enabled = true;
    pg_gizmo_set(
        &app->gizmo,
        app->selected_type == PG_TF_ROTATION
            ? PG_GIZMO_ROTATION
            : (app->selected_type == PG_TF_SCALE ? PG_GIZMO_SCALE : PG_GIZMO_TRANSLATION),
        node->position);
}

/* ---- frame ------------------------------------------------------------------------ */

static void
tick_animation(struct PG_App* app)
{
    struct PG_Animation* anim = pg_app_current_animation(app);
    struct PG_Keyframe* keyframe;

    if( !anim || !app->has_entity || anim->keyframe_count == 0 )
        return;

    if( app->timer > PG_MAX_ANIMATION_LENGTH )
        pg_app_set_current_frame(app, 0, 0);

    if( app->playing )
    {
        if( --app->frame_length <= 0 )
        {
            app->frame_counter++;
            app->frame_length =
                anim->keyframes[pg_animation_frame_index(anim, app->frame_counter)].length;
        }
        if( anim->length > 0 )
            app->timer = (app->timer + 1) % anim->length;
    }

    keyframe = &anim->keyframes[pg_animation_frame_index(anim, app->frame_counter)];
    pg_keyframe_apply(keyframe, app->entity.def);
    collect_nodes(app, keyframe);
    pg_upload_entity_model(
        &app->entity.model,
        app->entity.def,
        app->entity.adjacency,
        app->shading_type == PG_SHADING_FLAT);

    if( keyframe->id != app->previous_keyframe_id )
    {
        struct PG_Transformation* node = selected_node(app, keyframe);
        if( node )
            select_node(app, node);
        app->previous_keyframe_id = keyframe->id;
    }
}

static struct PG_Ray
view_ray(struct PG_App* app, struct PG_Mat4 view)
{
    struct PG_Ray ray;
    struct PG_Mat4 proj_view = pg_mat4_mul(app->renderer.projection, view);
    int viewport[4] = { 0, 0, app->view_w, app->view_h };
    float x = app->gui.input.mouse_x - (float)app->view_x;
    /* The window reports a top-down cursor and the unproject wants bottom-up. */
    float y = (float)app->view_h - (app->gui.input.mouse_y - (float)app->view_y);

    memset(&ray, 0, sizeof(ray));
    pg_unproject_ray(proj_view, x, y, viewport, &ray);
    return ray;
}

static void
draw_scene(struct PG_App* app)
{
    struct PG_Mat4 view;
    struct PG_Ray ray;
    struct PG_Animation* anim = pg_app_current_animation(app);
    struct PG_Keyframe* keyframe = NULL;
    float divisor = pg_cache_is_higher_rev(app->cache) ? PG_HIGHER_REV_SCALE : 1.0f;
    bool over_view =
        !app->gui.mouse_captured && app->dialog == PG_DIALOG_NONE &&
        pg_gui_hover(
            &app->gui, (float)app->view_x, (float)app->view_y, (float)app->view_w,
            (float)app->view_h);

    if( anim && anim->keyframe_count > 0 )
        keyframe = &anim->keyframes[pg_animation_frame_index(anim, app->frame_counter)];

    {
        uint32_t background = pg_colour_rgba(app->settings.background);
        /* The viewport is the one place the scene leaves point space: GL wants
         * device pixels, so every edge of the view rectangle is scaled here. */
        int px = (int)((float)app->view_x * app->dpi);
        int py = (int)((float)(app->window_h - app->view_y - app->view_h) * app->dpi);
        int pw = (int)((float)app->view_w * app->dpi);
        int ph = (int)((float)app->view_h * app->dpi);

        pg_glViewport(px, py, pw, ph);
        pg_glEnable(GL_SCISSOR_TEST);
        pg_glScissor(px, py, pw, ph);
        pg_glClearColor(
            (float)((background >> 24) & 0xFF) / 255.0f,
            (float)((background >> 16) & 0xFF) / 255.0f,
            (float)((background >> 8) & 0xFF) / 255.0f,
            1.0f);
        pg_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        pg_glDisable(GL_SCISSOR_TEST);
    }

    pg_glEnable(GL_DEPTH_TEST);
    pg_glEnable(GL_CULL_FACE);
    /* Front faces, not back: the entity shader flips x, which reverses winding. */
    pg_glCullFace(GL_FRONT);
    pg_glEnable(GL_BLEND);
    pg_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    pg_glPolygonMode(
        GL_FRONT_AND_BACK,
        app->polygon_mode == PG_POLYGON_LINE
            ? GL_LINE
            : (app->polygon_mode == PG_POLYGON_POINT ? GL_POINT : GL_FILL));

    tick_animation(app);

    {
        struct PG_Vec2 orbit = { 0, 0 };
        bool orbiting = false;
        if( over_view && (app->gui.input.down[PG_MOUSE_MIDDLE] || app->gui.input.down[PG_MOUSE_RIGHT]) )
        {
            orbit.x = app->gui.input.mouse_dx;
            orbit.y = app->gui.input.mouse_dy;
            orbiting = true;
        }
        if( over_view && app->gui.input.wheel != 0.0f )
            pg_camera_scroll(&app->camera, app->gui.input.wheel);
        pg_camera_tick(
            &app->camera,
            8.0f * app->settings.zoom_sensitivity,
            0.5f * app->settings.camera_sensitivity,
            orbit,
            orbiting);
    }

    view = pg_camera_view_matrix(&app->camera);
    ray = view_ray(app, view);

    if( app->has_entity )
        pg_render_entity(
            &app->renderer,
            &app->entity.model,
            view,
            pg_vec3(0, 0, 0),
            pg_vec3(0, 0, 0),
            app->entity.scale,
            app->shading_type);

    pg_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    /* Node interaction, then the nodes, then the grid, then the selected node and
     * its gizmo — the reference's order, and the reason the selected joint is
     * never hidden behind the manipulator. */
    if( app->nodes_enabled && keyframe )
    {
        int closest = -1;
        float min_distance = 3.402823466e+38F;
        float scale = node_scale(app);

        if( app->settings.bones_active )
        {
            for( int i = 0; i < app->node_count; i++ )
            {
                struct PG_Transformation* node = &keyframe->transformations[app->node_indices[i]];
                struct PG_Transformation* parent;
                int parent_index = node->parent;
                bool parent_visible = false;

                if( parent_index < 0 )
                    continue;
                parent = &keyframe->transformations[parent_index];
                for( int j = 0; j < app->node_count; j++ )
                    if( keyframe->transformations[app->node_indices[j]].id == parent->id )
                        parent_visible = true;
                if( !parent_visible )
                    continue;
                if( keyframe->root_node >= 0 )
                {
                    int root_id = keyframe->transformations[keyframe->root_node].id;
                    if( node->id == root_id || parent->id == root_id )
                        continue;
                }
                if( parent->child_by_type[PG_TF_ROTATION] < 0 )
                    continue;
                pg_render_bone(
                    &app->renderer,
                    view,
                    node->position,
                    parent->position,
                    app->entity.scale * divisor);
            }
        }

        if( over_view )
        {
            for( int i = 0; i < app->node_count; i++ )
            {
                struct PG_Transformation* node = &keyframe->transformations[app->node_indices[i]];
                struct PG_Vec3 min = pg_vec3_sub(node->position, pg_vec3(scale, scale, scale));
                struct PG_Vec3 max = pg_vec3_add(node->position, pg_vec3(scale, scale, scale));
                float near_t = 0.0f;
                float far_t = 0.0f;
                if( pg_intersect_ray_aabb(ray, min, max, &near_t, &far_t) &&
                    near_t < min_distance )
                {
                    min_distance = near_t;
                    closest = i;
                }
            }
        }

        app->hovered_node_id = -1;
        if( closest >= 0 )
        {
            struct PG_Transformation* node = &keyframe->transformations[app->node_indices[closest]];
            bool gizmo_busy = app->gizmo_enabled && app->gizmo.selected_axis >= 0;
            node->highlighted = true;
            app->hovered_node_id = node->id;
            if( app->gui.input.pressed[PG_MOUSE_LEFT] && !gizmo_busy )
            {
                if( app->selected_node_id == node->id )
                {
                    app->selected_node_id = -1;
                    app->gizmo_enabled = false;
                }
                else
                {
                    select_node(app, node);
                    pg_app_set_playing(app, false);
                }
            }
        }

        for( int i = 0; i < app->node_count; i++ )
        {
            struct PG_Transformation* node = &keyframe->transformations[app->node_indices[i]];
            bool is_root = keyframe->root_node >= 0 &&
                           keyframe->transformations[keyframe->root_node].id == node->id;
            if( node->id == app->selected_node_id )
                continue;
            pg_render_node(&app->renderer, view, node->position, scale, node->highlighted, is_root);
        }
    }

    if( app->settings.grid_active )
        pg_render_grid(&app->renderer, view, 75.0f);

    if( app->nodes_enabled && keyframe && app->selected_node_id >= 0 )
    {
        struct PG_Transformation* node = selected_node(app, keyframe);
        if( node && app->gizmo_enabled )
        {
            int axis;
            int delta = 0;
            bool cyclic = false;

            app->gizmo.position = node->position;
            axis = pg_gizmo_update(
                &app->gizmo,
                ray,
                over_view && app->gui.input.down[PG_MOUSE_LEFT],
                app->entity.size,
                &delta,
                &cyclic);
            if( axis >= 0 && delta != 0 )
            {
                struct PG_Transformation* target = pg_reference_child(keyframe, node, app->selected_type);
                if( target )
                {
                    struct PG_Command command;
                    int next = target->delta[axis] + delta;
                    if( cyclic )
                    {
                        if( next > 255 )
                            next = -255;
                        else if( next < -255 )
                            next = 255;
                    }
                    else
                    {
                        next = pg_clampi(next, -255, 255);
                    }
                    memset(&command, 0, sizeof(command));
                    command.kind = PG_CMD_TRANSFORM_NODE;
                    command.transformation_id = target->id;
                    command.coord_index = axis;
                    command.value = next;
                    command.keyframe_index = -1;
                    pg_app_execute(app, command);
                }
            }
            pg_gizmo_render(&app->gizmo, &app->renderer, view, app->entity.size);
        }
        if( node )
            pg_render_node(&app->renderer, view, node->position, node_scale(app), true, false);
    }

    if( over_view && app->gui.input.down[PG_MOUSE_LEFT] &&
        !(app->gizmo_enabled && app->gizmo.selected_axis >= 0) && app->hovered_node_id < 0 )
    {
        struct PG_Vec2 delta = { app->gui.input.mouse_dx, app->gui.input.mouse_dy };
        pg_camera_pan(&app->camera, view, delta, 0.5f * app->settings.camera_sensitivity);
    }
}

void
pg_app_frame(
    struct PG_App* app,
    const struct PG_GuiInput* input,
    int window_w,
    int window_h,
    float dpi)
{
    app->window_w = window_w;
    app->window_h = window_h;
    app->dpi = dpi > 0.0f ? dpi : 1.0f;

    pg_glViewport(0, 0, (int)((float)window_w * app->dpi), (int)((float)window_h * app->dpi));
    pg_glDisable(GL_SCISSOR_TEST);
    pg_glClearColor(33.0f / 255.0f, 33.0f / 255.0f, 33.0f / 255.0f, 1.0f);
    pg_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    pg_gui_begin(&app->gui, (float)window_w, (float)window_h, app->dpi, input);
    /* The UI runs first so it can claim the cursor before the 3D view sees it —
     * otherwise a click on a panel over the viewport would also pick a joint. */
    pg_ui_draw(app);

    if( app->renderer.viewport[2] != app->view_w || app->renderer.viewport[3] != app->view_h )
    {
        app->renderer.projection = pg_create_projection_matrix(app->view_w, app->view_h);
        app->renderer.viewport[2] = app->view_w;
        app->renderer.viewport[3] = app->view_h;
    }

    draw_scene(app);

    pg_glViewport(0, 0, (int)((float)window_w * app->dpi), (int)((float)window_h * app->dpi));
    pg_glDisable(GL_DEPTH_TEST);
    pg_gui_end(&app->gui);
}

/* ---- lifecycle -------------------------------------------------------------------- */

int
pg_app_init(struct PG_App* app, const char* cache_dir, const char* rev)
{
    memset(app, 0, sizeof(*app));
    app->current_animation = -1;
    app->selected_node_id = -1;
    app->hovered_node_id = -1;
    app->selected_type = PG_TF_TRANSLATION;
    app->previous_keyframe_id = -1;
    app->shading_type = PG_SHADING_SMOOTH;
    app->polygon_mode = PG_POLYGON_FILL;
    app->running = true;
    pg_settings_load(&app->settings);
    for( int i = 0; i < PG_LIST_COUNT; i++ )
    {
        app->lists[i].selected = -1;
        app->lists[i].dirty = true;
    }

    app->cache = pg_cache_open(cache_dir, rev);
    if( !app->cache )
        return -1;

    if( pg_renderer_init(&app->renderer) )
        return -1;
    if( pg_font_init() )
        return -1;
    if( pg_gui_init(&app->gui, &app->renderer) )
        return -1;
    pg_camera_init(&app->camera);

    app->entity_scale =
        pg_cache_is_higher_rev(app->cache) ? 1.0f / PG_HIGHER_REV_SCALE : 1.0f;

    for( int i = 0; i < pg_cache_seq_count(app->cache); i++ )
    {
        struct PG_Animation* anim =
            pg_animation_new_from_sequence(pg_cache_seq_at(app->cache, i));
        if( !anim )
            continue;
        if( app->animation_count == app->animation_capacity )
        {
            int next = app->animation_capacity ? app->animation_capacity * 2 : 1024;
            struct PG_Animation** grown =
                realloc(app->animations, (size_t)next * sizeof(*grown));
            if( !grown )
                break;
            app->animations = grown;
            app->animation_capacity = next;
        }
        app->animations[app->animation_count++] = anim;
    }
    app->current_animation = -1;

    /* The composed player if the cache has one, else the first entry — the same
     * fallback the reference uses. */
    {
        const struct PG_NpcDef* start = pg_cache_npc(app->cache, PG_PLAYER_NPC_ID);
        if( !start )
            start = pg_cache_npc_at(app->cache, 0);
        pg_app_load_npc(app, start);
    }
    pg_app_status(app, "%s (%s)", pg_cache_path(app->cache), pg_cache_rev(app->cache));
    return 0;
}

void
pg_app_free(struct PG_App* app)
{
    pg_app_history_reset(app);
    free(app->history);
    if( app->has_copied )
        pg_keyframe_free_contents(&app->copied);
    for( int i = 0; i < app->animation_count; i++ )
        pg_animation_free(app->animations[i]);
    free(app->animations);
    free(app->node_indices);
    for( int i = 0; i < app->owned_framemap_count; i++ )
    {
        struct PG_FrameMapDef* framemap = app->owned_framemaps[i];
        if( framemap->maps )
        {
            for( int j = 0; j < framemap->length; j++ )
                free(framemap->maps[j]);
            free(framemap->maps);
        }
        free(framemap->types);
        free(framemap->map_lengths);
        free(framemap);
    }
    free(app->owned_framemaps);
    for( int i = 0; i < PG_LIST_COUNT; i++ )
        free(app->lists[i].indices);
    entity_release(&app->entity);
    pg_gui_free(&app->gui);
    pg_font_free();
    pg_renderer_free(&app->renderer);
    pg_cache_close(app->cache);
}
