#ifndef INTERFACE_EDITOR_H
#define INTERFACE_EDITOR_H

#include "../input/libtorirs_input.h"
#include "../render/libtorirs_render.h"
#include "../scripting/libtorirs_scripting.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toridraw/toridraw_scene.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "ui/ui_inv_data_service.h"
#include "ui/ui_minimenu.h"
#include "ui/uitree.h"
#include "vm/cs2vm.h"
#include "toriauxlib/vm/toriauxlibvm.h"

#include <stdbool.h>
#include <stdint.h>

#define IE_WINDOW_W 1280
#define IE_WINDOW_H 800
#define IE_LEFT_PANEL_W 220
#define IE_RIGHT_PANEL_W 320
#define IE_TOOLBAR_H 28
#define IE_ROW_H 20
#define IE_WHEEL_SCROLL_ROWS 3
#define IE_SCROLLBAR_W 10
#define IE_SCROLLBAR_MIN_GRIP_H 20
#define IE_SCROLLBAR_TRACK_ARGB 0xFF23201B
#define IE_SCROLLBAR_GRIP_ARGB 0xFF4D4233
#define IE_PREVIEW_ROOT_W 765
#define IE_PREVIEW_ROOT_H 503

#define IE_MAX_WIDGETS 512
#define IE_MAX_DRAW_ITEMS 4096
#define IE_MAX_HIT_ITEMS 512
#define IE_MAX_EXPANDED 256
#define IE_TEXT_FIELD_LEN 128
#define IE_SPRITE_CACHE_MAX 512
#define IE_FONT_CACHE_MAX 32
#define IE_SPRITE_PICKER_PAGE_SIZE 80
#define IE_SCRIPT_CACHE_MAX 64
#define IE_RUNTIME_INV_HOOK_MAX 32
#define IE_RUNTIME_HOOK_MAX 256
#define IE_TRANSMIT_RING_SIZE 32
#define IE_OBJ_ICON_CACHE_MAX 256
#define IE_SCRIPT_ERROR_LEN 128
#define IE_SCRIPT_ERR_MAX 32

struct InterfaceEditorWidget
{
    Component data;
    int uid;
    int parent_uid;
    int file_index;
    bool is_editor_added;
    int lay_x;
    int lay_y;
    int lay_w;
    int lay_h;
    /** Editor-only: type-2 preview inventory container id (-1 = none). */
    int preview_inv_id;
};

struct InterfaceEditorSpriteEntry
{
    int cache_sprite_id;
    int element_id;
    int frame_count;
};

struct InterfaceEditorFontEntry
{
    int rs_font_id;
    int scene_font_id;
};

enum InterfaceEditorTextFieldKind
{
    IE_FIELD_NONE = 0,
    IE_FIELD_PROPERTY,
    IE_FIELD_ADD_DIALOG_SPRITE,
    IE_FIELD_CONTAINER_OBJ,
    IE_FIELD_CONTAINER_COUNT,
};

enum InterfaceEditorRightTab
{
    IE_TAB_PROPERTIES = 0,
    IE_TAB_CONTAINERS,
};

enum InterfaceEditorScrollTarget
{
    IE_SCROLL_NONE = 0,
    IE_SCROLL_LIST,
    IE_SCROLL_TREE,
    IE_SCROLL_BOTTOM,
};

struct InterfaceEditorScriptEntry
{
    int script_id;
    struct ToriAuxLibCore_ClientScript* loaded;
};

struct InterfaceEditorRuntimeInvHook
{
    int target_component_id;
    int script_id;
    int argc;
    int argv[16];
    int trigger_count;
    int triggers[8];
};

/** Matches CC_SETONCLICK..CC_SETONCLANCHANNELTRANSMIT opcode offset from 1400. */
enum InterfaceEditorSetonEvent
{
    IE_SETON_ON_CLICK = 0,
    IE_SETON_ON_HOLD,
    IE_SETON_ON_RELEASE,
    IE_SETON_ON_MOUSEOVER,
    IE_SETON_ON_MOUSELEAVE,
    IE_SETON_ON_DRAG,
    IE_SETON_ON_TARGETLEAVE,
    IE_SETON_ON_VARTRANSMIT,
    IE_SETON_ON_TIMER,
    IE_SETON_ON_OP,
    IE_SETON_ON_DRAGCOMPLETE,
    IE_SETON_ON_CLICKREPEAT,
    IE_SETON_ON_MOUSEREPEAT,
    IE_SETON_ON_INVTRANSMIT = 14,
    IE_SETON_ON_STATTRANSMIT,
    IE_SETON_ON_TARGETENTER,
    IE_SETON_ON_SCROLLWHEEL,
    IE_SETON_ON_CHATTRANSMIT,
    IE_SETON_ON_KEY,
    IE_SETON_ON_FRIENDTRANSMIT,
    IE_SETON_ON_CLANTRANSMIT,
    IE_SETON_ON_MISCTRANSMIT,
    IE_SETON_ON_DIALOGABORT,
    IE_SETON_ON_SUBCHANGE,
    IE_SETON_ON_STOCKTRANSMIT,
    IE_SETON_ON_RESIZE = 27,
    IE_SETON_ON_CLANSETTINGSTRANSMIT,
    IE_SETON_ON_CLANCHANNELTRANSMIT,
    IE_SETON_EVENT_COUNT
};

struct InterfaceEditorRuntimeHook
{
    int target_component_id;
    enum InterfaceEditorSetonEvent event;
    int script_id;
    int argc;
    int argv[16];
    int trigger_count;
    int triggers[8];
    bool used;
};

struct InterfaceEditorTransmitCycles
{
    int cycle_cntr;
    int changed_inv_count;
    int changed_inv_buffer[IE_TRANSMIT_RING_SIZE];
    int changed_varp_count;
    int changed_varp_buffer[IE_TRANSMIT_RING_SIZE];
    bool transmit_dirty;
    bool widgets_loaded_dirty;
    int last_transmit_process_cycle;
};

struct InterfaceEditorObjIconEntry
{
    int obj_id;
    int element_id;
};

struct InterfaceEditorTextField
{
    bool active;
    enum InterfaceEditorTextFieldKind kind;
    int property_id;
    char buffer[IE_TEXT_FIELD_LEN];
    int cursor;
};

enum InterfaceEditorPropertyId
{
    IE_PROP_RAW_X = 1,
    IE_PROP_RAW_Y,
    IE_PROP_RAW_W,
    IE_PROP_RAW_H,
    IE_PROP_X_MODE,
    IE_PROP_Y_MODE,
    IE_PROP_W_MODE,
    IE_PROP_H_MODE,
    IE_PROP_OPACITY,
    IE_PROP_SCROLL_W,
    IE_PROP_SCROLL_H,
    IE_PROP_TEXT,
    IE_PROP_FONT_ID,
    IE_PROP_LINE_HEIGHT,
    IE_PROP_TEXT_ALIGN_H,
    IE_PROP_TEXT_ALIGN_V,
    IE_PROP_COLOR,
    IE_PROP_COLOR2,
    IE_PROP_SPRITE_ID,
    IE_PROP_SPRITE_ID2,
    IE_PROP_ANGLE,
    IE_PROP_MODEL_ID,
    IE_PROP_ZOOM,
    IE_PROP_LINE_WIDTH,
    IE_PROP_GRID_COLS,
    IE_PROP_GRID_ROWS,
    IE_PROP_GRID_X_PITCH,
    IE_PROP_GRID_Y_PITCH,
    IE_PROP_OBJ_ID,
    IE_PROP_OBJ_COUNT,
    IE_PROP_PREVIEW_INV,
};

enum InterfaceEditorDrawKind
{
    IEDRAW_FILL_RECT = 0,
    IEDRAW_FONT,
    IEDRAW_SPRITE,
    IEDRAW_SELECTION,
};

struct InterfaceEditorDrawItem
{
    enum InterfaceEditorDrawKind kind;
    int x;
    int y;
    int w;
    int h;
    int argb;
    int font_id;
    int color;
    int center;
    int shadowed;
    char text[IE_TEXT_FIELD_LEN];
    int sprite_element_id;
    int sprite_atlas_index;
    int sprite_alpha;
    int tiled;
};

enum InterfaceEditorHitKind
{
    IEHIT_NONE = 0,
    IEHIT_LIST_ROW,
    IEHIT_TREE_ROW,
    IEHIT_TREE_EXPAND,
    IEHIT_TREE_ADD,
    IEHIT_CANVAS_WIDGET,
    IEHIT_PROPERTY_FIELD,
    IEHIT_PROPERTY_CHECKBOX,
    IEHIT_PROPERTY_DROPDOWN,
    IEHIT_PROPERTY_BUTTON,
    IEHIT_ADD_DIALOG_TYPE,
    IEHIT_ADD_DIALOG_SPRITE_FIELD,
    IEHIT_ADD_DIALOG_SPRITE_PICKER_BTN,
    IEHIT_ADD_DIALOG_BACKDROP,
    IEHIT_ADD_DIALOG_CANCEL,
    IEHIT_ADD_DIALOG_ADD,
    IEHIT_SPRITE_THUMB,
    IEHIT_MINIMENU,
    IEHIT_TOOLBAR_RUN_SCRIPTS,
    IEHIT_TOOLBAR_TRACE,
    IEHIT_TAB_PROPERTIES,
    IEHIT_TAB_CONTAINERS,
    IEHIT_CONTAINER_SOURCE_TAB,
    IEHIT_CONTAINER_SLOT_OBJ,
    IEHIT_CONTAINER_SLOT_COUNT,
    IEHIT_SCROLLBAR_GRIP,
};

struct InterfaceEditorHitItem
{
    enum InterfaceEditorHitKind kind;
    int x;
    int y;
    int w;
    int h;
    int param0;
    int param1;
};

enum GameInterfaceEditor_FramePhase
{
    IE_FRAME_GC_EVENTS = 0,
    IE_FRAME_BEGIN_2D,
    IE_FRAME_DRAW_LIST,
    IE_FRAME_END_2D,
    IE_FRAME_DONE,
};

struct GameInterfaceEditor_FrameState
{
    enum GameInterfaceEditor_FramePhase phase;
    int event_index;
    int draw_index;
};

struct GameInterfaceEditor
{
    struct LibToriRS_ScriptQueue* script_queue;
    struct ToriAuxLibCore* core;
    struct ToriAuxLibCache* cache;
    struct ToriAuxLibTD* td;
    struct ToriDraw_Scene* scene;
    struct RSCacheDat2Disk* dat2_cache;

    int* group_ids;
    int group_count;

    int* sprite_ids;
    int sprite_id_count;

    int loaded_group_id;
    struct InterfaceEditorWidget widgets[IE_MAX_WIDGETS];
    int widget_count;
    int draw_order[IE_MAX_WIDGETS];
    int draw_order_count;

    int selected_uid;
    int next_element_id;
    int next_dynamic_child;

    struct InterfaceEditorSpriteEntry sprite_cache[IE_SPRITE_CACHE_MAX];
    int sprite_cache_count;
    struct InterfaceEditorFontEntry font_cache[IE_FONT_CACHE_MAX];
    int font_cache_count;

    int list_scroll;
    int tree_scroll;
    int props_scroll;
    int list_content_h;
    int tree_content_h;
    int props_content_h;
    int expanded_uids[IE_MAX_EXPANDED];
    int expanded_count;

    struct InterfaceEditorTextField text_field;
    struct UIMinimenuState dropdown;
    int dropdown_property_id;

    bool add_dialog_open;
    int add_dialog_parent_uid;
    int add_dialog_type;
    int add_dialog_sprite_id;

    bool sprite_picker_open;
    int sprite_picker_page;
    int sprite_picker_target;

    struct InterfaceEditorDrawItem draw_list[IE_MAX_DRAW_ITEMS];
    int draw_count;
    struct InterfaceEditorHitItem hit_list[IE_MAX_HIT_ITEMS];
    int hit_count;

    int preview_x;
    int preview_y;
    int preview_w;
    int preview_h;
    float preview_scale;

    struct GameInterfaceEditor_FrameState frame;

    struct UITree* cs2_tree;
    struct CS2VM* cs2vm;
    struct CS2Host cs2host;
    struct ToriAuxLibVM* cs1vm_wrap;
    bool scripts_ran;
    char script_error[IE_SCRIPT_ERROR_LEN];
    char script_errors[IE_SCRIPT_ERR_MAX][IE_SCRIPT_ERROR_LEN];
    int script_error_count;
    bool cs2_trace_enabled;

    int diag_onload_run;
    int diag_onload_failed;
    int diag_gosub_unresolved;
    int diag_cc_create_ok;
    int diag_cc_create_failed;

    struct InterfaceEditorScriptEntry script_cache[IE_SCRIPT_CACHE_MAX];
    int script_cache_count;
    struct InterfaceEditorRuntimeInvHook runtime_inv_hooks[IE_RUNTIME_INV_HOOK_MAX];
    int runtime_inv_hook_count;
    struct InterfaceEditorRuntimeHook runtime_hooks[IE_RUNTIME_HOOK_MAX];
    int runtime_hook_count;
    struct InterfaceEditorTransmitCycles transmit_cycles;
    int script_tick_accum_ms;

    struct UIInvDataService inv_data;
    int worn_source_id;
    int inventory_source_id;

    struct InterfaceEditorObjIconEntry obj_icon_cache[IE_OBJ_ICON_CACHE_MAX];
    int obj_icon_cache_count;

    enum InterfaceEditorRightTab right_tab;
    int containers_source_id;
    int containers_scroll;
    int containers_content_h;
    int container_edit_source_id;
    int container_edit_slot;

    bool scrollbar_drag_active;
    enum InterfaceEditorScrollTarget scrollbar_drag_target;

    int canvas_drag_uid;
    uint64_t saved_drag_zone_px;
    uint64_t saved_drag_time_ms;
};

struct GameInterfaceEditor*
GameInterfaceEditor_New(
    struct LibToriRS_ScriptQueue* script_queue,
    struct ToriDraw_Scene* scene);

void
GameInterfaceEditor_Free(struct GameInterfaceEditor* game);

void
GameInterfaceEditor_SetCore(struct GameInterfaceEditor* game, struct ToriAuxLibCore* core);

void
GameInterfaceEditor_SetCache(struct GameInterfaceEditor* game, struct ToriAuxLibCache* cache);

void
GameInterfaceEditor_SetTD(struct GameInterfaceEditor* game, struct ToriAuxLibTD* td);

void
GameInterfaceEditor_SetDat2Cache(
    struct GameInterfaceEditor* game,
    struct RSCacheDat2Disk* dat2_cache);

bool
GameInterfaceEditor_EnumerateInterfaceGroups(struct GameInterfaceEditor* game);

bool
GameInterfaceEditor_LoadGroup(struct GameInterfaceEditor* game, int group_id);

int
GameInterfaceEditor_CountDynamicCs2Children(struct GameInterfaceEditor* game);

void
GameInterfaceEditor_PrintDynamicCs2Children(struct GameInterfaceEditor* game);

void
GameInterfaceEditor_ProcessInput(
    struct GameInterfaceEditor* game,
    struct LibToriRS_Input* input);

void
GameInterfaceEditor_FrameBegin(struct GameInterfaceEditor* game, int cycles_elapsed);

bool
GameInterfaceEditor_FrameNextCommand(
    struct GameInterfaceEditor* game,
    struct LibToriRS_RenderCommand* command);

void
GameInterfaceEditor_FrameEnd(struct GameInterfaceEditor* game);

void
GameInterfaceEditor_RunScripts(struct GameInterfaceEditor* game);

void
GameInterfaceEditor_FreeScripts(struct GameInterfaceEditor* game);

#endif
