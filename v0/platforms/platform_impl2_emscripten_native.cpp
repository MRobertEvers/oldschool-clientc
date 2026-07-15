#include "platform_impl2_emscripten_native.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EM_TRUE
#define EM_TRUE 1
#endif
#ifndef EM_FALSE
#define EM_FALSE 0
#endif

enum
{
    NATIVE_INPUT_RING_CAP = 4096
};

static struct GameInputEvent s_input_ring[NATIVE_INPUT_RING_CAP];
static int s_input_ring_head = 0;
static int s_input_ring_tail = 0;

static int s_prev_game_mx = 0;
static int s_prev_game_my = 0;
static int s_have_prev_mouse = 0;

EM_JS(void, emscripten_native_client_to_canvas, (long client_x, long client_y, int* out_x, int* out_y), {
    var c = document.getElementById('canvas');
    if( !c || !out_x || !out_y )
        return;
    var r = c.getBoundingClientRect();
    var rw = r.width || 1;
    var rh = r.height || 1;
    var sx = c.width / rw;
    var sy = c.height / rh;
    HEAP32[out_x >> 2] = Math.floor((client_x - r.left) * sx);
    HEAP32[out_y >> 2] = Math.floor((client_y - r.top) * sy);
});

static void
native_input_ring_push(const struct GameInputEvent* ev)
{
    int next = (s_input_ring_tail + 1) % NATIVE_INPUT_RING_CAP;
    if( next == s_input_ring_head )
        return;
    s_input_ring[s_input_ring_tail] = *ev;
    s_input_ring_tail = next;
}

static void
native_input_ring_drain_to(struct GInput* input)
{
    while( s_input_ring_head != s_input_ring_tail && input->in_event_count < GAME_INPUT_MAX_EVENTS )
    {
        input->in_events[input->in_event_count] = s_input_ring[s_input_ring_head];
        input->in_event_count++;
        s_input_ring_head = (s_input_ring_head + 1) % NATIVE_INPUT_RING_CAP;
    }
}

static enum GameInputKeyCode
native_keycode_from_code(const char* code)
{
    if( !code )
        return TORIRSK_UNKNOWN;
    /* DOM code strings (subset used by the game). */
    static const struct
    {
        const char* code;
        enum GameInputKeyCode k;
    } map[] = {
        { "KeyA", TORIRSK_A },       { "KeyB", TORIRSK_B },       { "KeyC", TORIRSK_C },
        { "KeyD", TORIRSK_D },       { "KeyE", TORIRSK_E },       { "KeyF", TORIRSK_F },
        { "KeyG", TORIRSK_G },       { "KeyH", TORIRSK_H },       { "KeyI", TORIRSK_I },
        { "KeyJ", TORIRSK_J },       { "KeyK", TORIRSK_K },       { "KeyL", TORIRSK_L },
        { "KeyM", TORIRSK_M },       { "KeyN", TORIRSK_N },       { "KeyO", TORIRSK_O },
        { "KeyP", TORIRSK_P },       { "KeyQ", TORIRSK_Q },       { "KeyR", TORIRSK_R },
        { "KeyS", TORIRSK_S },       { "KeyT", TORIRSK_T },       { "KeyU", TORIRSK_U },
        { "KeyV", TORIRSK_V },       { "KeyW", TORIRSK_W },       { "KeyX", TORIRSK_X },
        { "KeyY", TORIRSK_Y },       { "KeyZ", TORIRSK_Z },       { "Digit0", TORIRSK_0 },
        { "Digit1", TORIRSK_1 },     { "Digit2", TORIRSK_2 },     { "Digit3", TORIRSK_3 },
        { "Digit4", TORIRSK_4 },     { "Digit5", TORIRSK_5 },     { "Digit6", TORIRSK_6 },
        { "Digit7", TORIRSK_7 },     { "Digit8", TORIRSK_8 },     { "Digit9", TORIRSK_9 },
        { "Escape", TORIRSK_ESCAPE }, { "Enter", TORIRSK_RETURN }, { "NumpadEnter", TORIRSK_RETURN },
        { "Backspace", TORIRSK_BACKSPACE }, { "Insert", TORIRSK_INSERT }, { "Delete", TORIRSK_DELETE },
        { "ShiftLeft", TORIRSK_SHIFT }, { "ShiftRight", TORIRSK_SHIFT }, { "ControlLeft", TORIRSK_CTRL },
        { "ControlRight", TORIRSK_CTRL }, { "Comma", TORIRSK_COMMA }, { "Tab", TORIRSK_TAB },
        { "Space", TORIRSK_SPACE }, { "ArrowLeft", TORIRSK_LEFT }, { "ArrowRight", TORIRSK_RIGHT },
        { "ArrowUp", TORIRSK_UP }, { "ArrowDown", TORIRSK_DOWN }, { "PageUp", TORIRSK_PAGE_UP },
        { "PageDown", TORIRSK_PAGE_DOWN },
    };
    for( size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++ )
    {
        if( strcmp(code, map[i].code) == 0 )
            return map[i].k;
    }
    return TORIRSK_UNKNOWN;
}

static int
native_dom_button_to_game(int dom_button)
{
    /* DOM: 0=left, 1=middle, 2=right — match SDL_BUTTON_* convention in tori_rs_sdl2_gameinput.c */
    switch( dom_button )
    {
    case 0:
        return TORIRSM_LEFT;
    case 2:
        return TORIRSM_RIGHT;
    case 1:
        return TORIRSM_MIDDLE;
    default:
        return TORIRSM_LEFT;
    }
}

static void
transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    struct Platform2_Emscripten_Native* platform)
{
    float src_aspect = (float)platform->game_screen_width / (float)platform->game_screen_height;
    float dst_aspect = (float)platform->drawable_width / (float)platform->drawable_height;

    int dst_x, dst_y, dst_w, dst_h;

    if( src_aspect > dst_aspect )
    {
        dst_w = platform->drawable_width;
        dst_h = (int)(platform->drawable_width / src_aspect);
        dst_x = 0;
        dst_y = (platform->drawable_height - dst_h) / 2;
    }
    else
    {
        dst_h = platform->drawable_height;
        dst_w = (int)(platform->drawable_height * src_aspect);
        dst_y = 0;
        dst_x = (platform->drawable_width - dst_w) / 2;
    }

    if( window_mouse_x < dst_x || window_mouse_x >= dst_x + dst_w || window_mouse_y < dst_y ||
        window_mouse_y >= dst_y + dst_h )
    {
        *game_mouse_x = -1;
        *game_mouse_y = -1;
    }
    else
    {
        float relative_x = (float)(window_mouse_x - dst_x) / (float)dst_w;
        float relative_y = (float)(window_mouse_y - dst_y) / (float)dst_h;

        *game_mouse_x = (int)(relative_x * platform->game_screen_width);
        *game_mouse_y = (int)(relative_y * platform->game_screen_height);
    }
}

static EM_BOOL
native_on_keydown(int /*eventType*/, const EmscriptenKeyboardEvent* e, void* userData)
{
    struct Platform2_Emscripten_Native* platform = (struct Platform2_Emscripten_Native*)userData;
    if( !platform || !platform->canvas_ready )
        return EM_FALSE;

    enum GameInputKeyCode k = native_keycode_from_code(e->code);
    if( k == TORIRSK_UNKNOWN )
        return EM_FALSE;

    struct GameInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TORIRSEV_KEY_DOWN;
    ev.key_down.key = k;
    native_input_ring_push(&ev);
    return EM_TRUE;
}

static EM_BOOL
native_on_keyup(int /*eventType*/, const EmscriptenKeyboardEvent* e, void* userData)
{
    struct Platform2_Emscripten_Native* platform = (struct Platform2_Emscripten_Native*)userData;
    if( !platform || !platform->canvas_ready )
        return EM_FALSE;

    enum GameInputKeyCode k = native_keycode_from_code(e->code);
    if( k == TORIRSK_UNKNOWN )
        return EM_FALSE;

    struct GameInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TORIRSEV_KEY_UP;
    ev.key_up.key = k;
    native_input_ring_push(&ev);
    return EM_TRUE;
}

static void
native_push_mouse_at_canvas(
    struct Platform2_Emscripten_Native* platform,
    int canvas_mx,
    int canvas_my,
    int* out_gx,
    int* out_gy)
{
    transform_mouse_coordinates(canvas_mx, canvas_my, out_gx, out_gy, platform);
}

static EM_BOOL
native_on_mousemove(int /*eventType*/, const EmscriptenMouseEvent* e, void* userData)
{
    struct Platform2_Emscripten_Native* platform = (struct Platform2_Emscripten_Native*)userData;
    if( !platform || !platform->canvas_ready )
        return EM_FALSE;

    int canvas_mx = 0;
    int canvas_my = 0;
    emscripten_native_client_to_canvas(e->clientX, e->clientY, &canvas_mx, &canvas_my);
    platform->tracked_mouse_x = canvas_mx;
    platform->tracked_mouse_y = canvas_my;

    int gx = 0;
    int gy = 0;
    native_push_mouse_at_canvas(platform, canvas_mx, canvas_my, &gx, &gy);

    int xrel = 0;
    int yrel = 0;
    if( s_have_prev_mouse )
    {
        xrel = gx - s_prev_game_mx;
        yrel = gy - s_prev_game_my;
    }
    s_prev_game_mx = gx;
    s_prev_game_my = gy;
    s_have_prev_mouse = 1;

    struct GameInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TORIRSEV_MOUSE_MOVE;
    ev.mouse_move.x = gx;
    ev.mouse_move.y = gy;
    ev.mouse_move.xrel = xrel;
    ev.mouse_move.yrel = yrel;
    native_input_ring_push(&ev);
    return EM_TRUE;
}

static EM_BOOL
native_on_mousedown(int /*eventType*/, const EmscriptenMouseEvent* e, void* userData)
{
    struct Platform2_Emscripten_Native* platform = (struct Platform2_Emscripten_Native*)userData;
    if( !platform || !platform->canvas_ready )
        return EM_FALSE;

    int canvas_mx = 0;
    int canvas_my = 0;
    emscripten_native_client_to_canvas(e->clientX, e->clientY, &canvas_mx, &canvas_my);
    platform->tracked_mouse_x = canvas_mx;
    platform->tracked_mouse_y = canvas_my;

    int gx = 0;
    int gy = 0;
    native_push_mouse_at_canvas(platform, canvas_mx, canvas_my, &gx, &gy);

    struct GameInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TORIRSEV_MOUSE_DOWN;
    ev.mouse_down.mouse_x = gx;
    ev.mouse_down.mouse_y = gy;
    ev.mouse_down.button = native_dom_button_to_game((int)e->button);
    native_input_ring_push(&ev);
    return EM_TRUE;
}

static EM_BOOL
native_on_mouseup(int /*eventType*/, const EmscriptenMouseEvent* e, void* userData)
{
    struct Platform2_Emscripten_Native* platform = (struct Platform2_Emscripten_Native*)userData;
    if( !platform || !platform->canvas_ready )
        return EM_FALSE;

    int canvas_mx = 0;
    int canvas_my = 0;
    emscripten_native_client_to_canvas(e->clientX, e->clientY, &canvas_mx, &canvas_my);
    platform->tracked_mouse_x = canvas_mx;
    platform->tracked_mouse_y = canvas_my;

    int gx = 0;
    int gy = 0;
    native_push_mouse_at_canvas(platform, canvas_mx, canvas_my, &gx, &gy);

    struct GameInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TORIRSEV_MOUSE_UP;
    ev.mouse_up.mouse_x = gx;
    ev.mouse_up.mouse_y = gy;
    ev.mouse_up.button = native_dom_button_to_game((int)e->button);
    native_input_ring_push(&ev);
    return EM_TRUE;
}

static EM_BOOL
native_on_wheel(int /*eventType*/, const EmscriptenWheelEvent* e, void* userData)
{
    struct Platform2_Emscripten_Native* platform = (struct Platform2_Emscripten_Native*)userData;
    if( !platform || !platform->canvas_ready )
        return EM_FALSE;

    int gx = platform->input ? platform->input->mouse_state.x : 0;
    int gy = platform->input ? platform->input->mouse_state.y : 0;

    struct GameInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TORIRSEV_MOUSE_WHEEL;
    ev.mouse_wheel.mouse_x = gx;
    ev.mouse_wheel.mouse_y = gy;
    ev.mouse_wheel.wheel = (int)e->deltaY;
    native_input_ring_push(&ev);
    return EM_TRUE;
}

static void
native_register_input_callbacks(struct Platform2_Emscripten_Native* platform)
{
    const char* target = "#canvas";
    emscripten_set_keydown_callback(target, platform, EM_TRUE, native_on_keydown);
    emscripten_set_keyup_callback(target, platform, EM_TRUE, native_on_keyup);
    emscripten_set_mousemove_callback(target, platform, EM_TRUE, native_on_mousemove);
    emscripten_set_mousedown_callback(target, platform, EM_TRUE, native_on_mousedown);
    emscripten_set_mouseup_callback(target, platform, EM_TRUE, native_on_mouseup);
    emscripten_set_wheel_callback(target, platform, EM_TRUE, native_on_wheel);
}

static void
native_unregister_input_callbacks(void)
{
    const char* target = "#canvas";
    emscripten_set_keydown_callback(target, NULL, EM_TRUE, NULL);
    emscripten_set_keyup_callback(target, NULL, EM_TRUE, NULL);
    emscripten_set_mousemove_callback(target, NULL, EM_TRUE, NULL);
    emscripten_set_mousedown_callback(target, NULL, EM_TRUE, NULL);
    emscripten_set_mouseup_callback(target, NULL, EM_TRUE, NULL);
    emscripten_set_wheel_callback(target, NULL, EM_TRUE, NULL);
}

struct Platform2_Emscripten_Native*
Platform2_Emscripten_Native_New(void)
{
    struct Platform2_Emscripten_Native* platform =
        (struct Platform2_Emscripten_Native*)malloc(sizeof(struct Platform2_Emscripten_Native));
    memset(platform, 0, sizeof(struct Platform2_Emscripten_Native));

    platform->input = (struct GInput*)malloc(sizeof(struct GInput));
    memset(platform->input, 0, sizeof(struct GInput));

    return platform;
}

void
Platform2_Emscripten_Native_Free(struct Platform2_Emscripten_Native* platform)
{
    if( !platform )
        return;
    if( platform->input )
        free(platform->input);
    free(platform);
}

bool
Platform2_Emscripten_Native_InitForSoft3D(
    struct Platform2_Emscripten_Native* platform,
    int canvas_width,
    int canvas_height)
{
    emscripten_set_canvas_element_size("#canvas", canvas_width, canvas_height);

    platform->window_width = canvas_width;
    platform->window_height = canvas_height;
    platform->drawable_width = canvas_width;
    platform->drawable_height = canvas_height;
    platform->game_screen_width = canvas_width;
    platform->game_screen_height = canvas_height;
    platform->display_scale = 1.0f;
    platform->last_frame_time_ticks = (uint64_t)emscripten_get_now();
    platform->canvas_ready = true;

    s_input_ring_head = s_input_ring_tail = 0;
    s_have_prev_mouse = 0;

    native_register_input_callbacks(platform);
    return true;
}

void
Platform2_Emscripten_Native_Shutdown(struct Platform2_Emscripten_Native* platform)
{
    if( !platform )
        return;
    platform->canvas_ready = false;
    native_unregister_input_callbacks();
}

void
Platform2_Emscripten_Native_SyncCanvasCssSize(
    struct Platform2_Emscripten_Native* platform,
    struct GGame* game_nullable)
{
    if( !platform )
        return;

    double css_width = 0.0;
    double css_height = 0.0;
    emscripten_get_element_css_size("#canvas", &css_width, &css_height);
    int new_w = (int)css_width;
    int new_h = (int)css_height;
    if( new_w <= 0 || new_h <= 0 )
        return;

    if( game_nullable && game_nullable->iface_view_port &&
        game_nullable->iface_view_port->clip_right <= 0 && platform->game_screen_width > 0 )
    {
        struct DashViewPort* ivp = game_nullable->iface_view_port;
        ivp->clip_left = 0;
        ivp->clip_top = 0;
        ivp->clip_right = platform->game_screen_width;
        ivp->clip_bottom = platform->game_screen_height;
    }

    if( new_w == platform->window_width && new_h == platform->window_height )
        return;

    platform->window_width = new_w;
    platform->window_height = new_h;
    platform->drawable_width = new_w;
    platform->drawable_height = new_h;

    emscripten_set_canvas_element_size("#canvas", new_w, new_h);
}

void
Platform2_Emscripten_Native_PollEvents(struct Platform2_Emscripten_Native* platform)
{
    struct GInput* input = platform->input;

    Platform2_Emscripten_Native_SyncCanvasCssSize(platform, platform->current_game);

    double time_s = emscripten_get_now() / 1000.0;
    if( input->time == 0.0 )
    {
        input->time = time_s;
    }
    else
    {
        double time_delta_seconds = time_s - input->time;
        input->time_delta_accumulator_seconds += time_delta_seconds;
        input->time = time_s;
    }

    native_input_ring_drain_to(input);

    {
        int mx = platform->tracked_mouse_x;
        int my = platform->tracked_mouse_y;
        transform_mouse_coordinates(mx, my, &input->mouse_state.x, &input->mouse_state.y, platform);
    }
}

static void
send_lua_game_script_to_js(struct LuaGameScript* script)
{
    // clang-format off
    EM_ASM(
        {
            if (typeof window.done === 'undefined') {
                window.done = false;
            }

            var ptr = $0;
            window.LUA_SCRIPT_QUEUE = window.LUA_SCRIPT_QUEUE || [];
            window.LUA_SCRIPT_QUEUE.push(ptr);
        },
        script);
    // clang-format on
}

void
Platform2_Emscripten_Native_RunLuaScripts(struct Platform2_Emscripten_Native* platform)
{
    struct LuaGameScript* script = NULL;

    while( !LibToriRS_LuaScriptQueueIsEmpty(platform->current_game) )
    {
        script = LuaGameScript_New();
        LibToriRS_LuaScriptQueuePop(platform->current_game, script);

        send_lua_game_script_to_js(script);
    }
}
