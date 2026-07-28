#ifndef POSER_GL_C_PG_APP_H
#define POSER_GL_C_PG_APP_H

/*
 * The editor's whole state, in one place — poser-gl's RenderContext.
 *
 * Everything hangs off this: the cache, the posed entity, the animation being
 * edited, the undo history and the UI's own selection and scroll positions. The
 * reference threads the same object through every class for the same reason.
 */

#include "pg_anim.h"
#include "pg_cache.h"
#include "pg_gizmo.h"
#include "pg_gui.h"
#include "pg_render.h"

#include <stdbool.h>

#define PG_MAX_COMPONENTS 64
#define PG_SETTINGS_FILE "poser-gl.settings"

enum PG_Colour
{
    PG_COLOUR_GRAY = 0,
    PG_COLOUR_BLACK,
    PG_COLOUR_WHITE,
    PG_COLOUR_RED,
    PG_COLOUR_GREEN,
    PG_COLOUR_BLUE,
    PG_COLOUR_COUNT
};

const char*
pg_colour_name(int colour);
uint32_t
pg_colour_rgba(int colour);

struct PG_Settings
{
    int background;
    int cursor_colour;
    float camera_sensitivity;
    float zoom_sensitivity;
    bool grid_active;
    bool bones_active;
    bool advanced_mode;
};

void
pg_settings_defaults(struct PG_Settings* settings);
void
pg_settings_load(struct PG_Settings* settings);
void
pg_settings_save(const struct PG_Settings* settings);

/** One model in the entity's composition, with the recolours it carries. */
struct PG_EntityComponent
{
    int model_id;
    const uint16_t* recolor_from;
    const uint16_t* recolor_to;
    int recolor_count;
    /** True when an equipped item put this here, so unequipping can remove it. */
    bool from_item;
    int item_id;
};

struct PG_Entity
{
    char name[64];
    int size;
    float scale;
    struct PG_EntityComponent components[PG_MAX_COMPONENTS];
    int component_count;

    struct PG_ModelDef* def;
    struct PG_NormalAdjacency* adjacency;
    struct PG_GlModel model;
};

/* ---- undo history ---------------------------------------------------------- */

enum PG_CommandKind
{
    PG_CMD_TRANSFORM_NODE = 0,
    PG_CMD_CHANGE_LENGTH,
    PG_CMD_ADD_KEYFRAME,
    PG_CMD_PASTE_KEYFRAME,
    PG_CMD_LERP_KEYFRAME,
    PG_CMD_DELETE_KEYFRAME
};

struct PG_Command
{
    int kind;
    int animation_id;
    int keyframe_index;
    int transformation_id;
    int coord_index;
    int value;
    int previous_value;
    /** The removed keyframe, kept whole so a delete can be undone. */
    struct PG_Keyframe removed;
    bool has_removed;
};

/* ---- lists ------------------------------------------------------------------ */

enum PG_ListTab
{
    PG_LIST_ENTITY = 0,
    PG_LIST_SEQUENCE,
    PG_LIST_ITEM,
    PG_LIST_COUNT
};

struct PG_FilteredList
{
    int* indices;
    int count;
    int capacity;
    float scroll;
    int selected; /* index into `indices`, or -1 */
    char search[64];
    bool dirty;
};

/* ---- app --------------------------------------------------------------------- */

enum PG_DialogKind
{
    PG_DIALOG_NONE = 0,
    PG_DIALOG_MESSAGE,
    PG_DIALOG_EXPORT,
    PG_DIALOG_IMPORT,
    PG_DIALOG_PACK,
    PG_DIALOG_SETTINGS,
    PG_DIALOG_ABOUT
};

struct PG_App
{
    struct PG_Cache* cache;
    struct PG_Renderer renderer;
    struct PG_Gui gui;
    struct PG_Camera camera;
    struct PG_Settings settings;

    struct PG_Entity entity;
    bool has_entity;
    float entity_scale;

    struct PG_Animation** animations;
    int animation_count;
    int animation_capacity;
    int current_animation; /* index into `animations`, or -1 */

    bool playing;
    int timer;
    int frame_counter;
    int frame_length;
    int previous_keyframe_id;

    int polygon_mode;
    int shading_type;

    bool nodes_enabled;
    int* node_indices; /* into the current keyframe's transformations */
    int node_count;
    int node_capacity;
    int selected_node_id;
    int selected_type;
    int hovered_node_id;
    struct PG_Gizmo gizmo;
    bool gizmo_enabled;

    struct PG_Keyframe copied;
    bool has_copied;

    /**
     * Framemaps synthesised by a .pgl import, which no cache archive owns.
     *
     * Keyframes share a framemap pointer — a copy does not clone it, exactly as
     * the reference's copy constructor does not — so ownership cannot sit on the
     * keyframe without risking a double free. It sits here and is released once.
     */
    struct PG_FrameMapDef** owned_framemaps;
    int owned_framemap_count;
    int owned_framemap_capacity;

    struct PG_Command* history;
    int history_count;
    int history_capacity;
    int history_index;

    int list_tab;
    struct PG_FilteredList lists[PG_LIST_COUNT];

    int dialog;
    char dialog_title[64];
    char dialog_message[192];
    char path_buffer[512];
    char last_export[512];
    int last_export_format;

    int view_x, view_y, view_w, view_h;
    int window_w, window_h;
    char status[256];
    bool running;
};

int
pg_app_init(struct PG_App* app, const char* cache_dir, const char* rev);
void
pg_app_free(struct PG_App* app);

/** One editor step: playback, camera, picking and the whole draw. */
void
pg_app_frame(struct PG_App* app, const struct PG_GuiInput* input, int window_w, int window_h);

/* ---- entity ------------------------------------------------------------------- */

void
pg_app_load_npc(struct PG_App* app, const struct PG_NpcDef* npc);
void
pg_app_rebuild_entity(struct PG_App* app);
void
pg_app_remove_component(struct PG_App* app, int index);
void
pg_app_toggle_item(struct PG_App* app, int item_id, bool equip);

/* ---- animation ----------------------------------------------------------------- */

struct PG_Animation*
pg_app_current_animation(struct PG_App* app);
void
pg_app_select_animation(struct PG_App* app, int index);
void
pg_app_reset_animation(struct PG_App* app);
void
pg_app_set_playing(struct PG_App* app, bool playing);
void
pg_app_set_current_frame(struct PG_App* app, int frame, int offset);
void
pg_app_next_frame(struct PG_App* app);
void
pg_app_previous_frame(struct PG_App* app);
/**
 * The animation to edit: the current one if it is already a copy, otherwise a
 * fresh copy numbered above the cache's highest sequence id. This is what keeps
 * an edit from silently rewriting a cache animation in place.
 */
struct PG_Animation*
pg_app_animation_or_copy(struct PG_App* app);
void
pg_app_add_animation(struct PG_App* app, struct PG_Animation* animation);

/* ---- commands ------------------------------------------------------------------- */

bool
pg_app_execute(struct PG_App* app, struct PG_Command command);
void
pg_app_undo(struct PG_App* app);
void
pg_app_redo(struct PG_App* app);
void
pg_app_history_reset(struct PG_App* app);

/* ---- keyframe actions ------------------------------------------------------------ */

void
pg_app_add_keyframe(struct PG_App* app);
void
pg_app_copy_keyframe(struct PG_App* app);
void
pg_app_paste_keyframe(struct PG_App* app);
void
pg_app_lerp_keyframe(struct PG_App* app);
void
pg_app_delete_keyframe(struct PG_App* app);

/* ---- transfer --------------------------------------------------------------------- */

/** poser-gl's own .pgl interchange format, byte compatible with the reference. */
bool
pg_export_pgl(struct PG_App* app, const char* path);
bool
pg_import_pgl(struct PG_App* app, const char* path);
/** The 317 frame/framemap pair poser-gl's DAT export writes. */
bool
pg_export_dat(struct PG_App* app, const char* path);
/** Write the current animation back into a copy of the cache. */
bool
pg_pack_animation(struct PG_App* app, const char* out_directory);

void
pg_app_message(struct PG_App* app, const char* title, const char* message);
void
pg_app_status(struct PG_App* app, const char* format, ...);

/* ---- ui ------------------------------------------------------------------------------ */

void
pg_ui_draw(struct PG_App* app);

#endif
