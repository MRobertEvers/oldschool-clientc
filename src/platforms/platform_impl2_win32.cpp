#if defined(_WIN32)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* Prime dash + cache_dat under C linkage before headers that pull buildcachedat.h (see
 * platform_impl2_win32.h), or MinGW links C++-mangled refs to C symbols from .c files. */
extern "C" {
#include "graphics/dash.h"
#include "osrs/rscache/cache_dat.h"
}

#include "platform_impl2_win32.h"

#include "platform_impl2_win32_renderer_gdisoft3d.h"
#if defined(TORIRS_HAS_D3D8)
#include "platform_impl2_win32_renderer_d3d8.h"
#endif
#include "nuklear/torirs_nuklear.h"

extern "C" {
#include "3rd/lua/lua.h"
#include "osrs/game.h"
#include "osrs/gameproto_parse.h"
#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/lua_api.h"
#include "osrs/lua_sidecar/lua_configfile.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/luac_gametypes.h"
#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/lua_sidecar/luac_sidecar_cachedat.h"
#include "osrs/lua_sidecar/luac_sidecar_config.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/scripts/lua_cache_fnnos.h"
#include "tori_rs.h"
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CACHE_PATH
#define CACHE_PATH "../cache254"
#endif
#ifndef LUA_SCRIPTS_DIR
#define LUA_SCRIPTS_DIR "../src/osrs/scripts"
#endif

enum
{
    WIN32_INPUT_RING_CAP = 4096
};

static struct GameInputEvent s_input_ring[WIN32_INPUT_RING_CAP];
static int s_input_ring_head = 0;
static int s_input_ring_tail = 0;

static int s_prev_game_mx = 0;
static int s_prev_game_my = 0;
static int s_have_prev_mouse = 0;

static void
win32_input_ring_push(const struct GameInputEvent* ev)
{
    int next = (s_input_ring_tail + 1) % WIN32_INPUT_RING_CAP;
    if( next == s_input_ring_head )
        return;
    s_input_ring[s_input_ring_tail] = *ev;
    s_input_ring_tail = next;
}

static void
win32_input_ring_drain_to(struct GInput* input)
{
    while( s_input_ring_head != s_input_ring_tail && input->in_event_count < GAME_INPUT_MAX_EVENTS )
    {
        input->in_events[input->in_event_count] = s_input_ring[s_input_ring_head];
        input->in_event_count++;
        s_input_ring_head = (s_input_ring_head + 1) % WIN32_INPUT_RING_CAP;
    }
}

static enum GameInputKeyCode
win32_vk_to_keycode(WPARAM vk, LPARAM lp)
{
    (void)lp;
    switch( vk )
    {
    case 'A':
        return TORIRSK_A;
    case 'B':
        return TORIRSK_B;
    case 'C':
        return TORIRSK_C;
    case 'D':
        return TORIRSK_D;
    case 'E':
        return TORIRSK_E;
    case 'F':
        return TORIRSK_F;
    case 'G':
        return TORIRSK_G;
    case 'H':
        return TORIRSK_H;
    case 'I':
        return TORIRSK_I;
    case 'J':
        return TORIRSK_J;
    case 'K':
        return TORIRSK_K;
    case 'L':
        return TORIRSK_L;
    case 'M':
        return TORIRSK_M;
    case 'N':
        return TORIRSK_N;
    case 'O':
        return TORIRSK_O;
    case 'P':
        return TORIRSK_P;
    case 'Q':
        return TORIRSK_Q;
    case 'R':
        return TORIRSK_R;
    case 'S':
        return TORIRSK_S;
    case 'T':
        return TORIRSK_T;
    case 'U':
        return TORIRSK_U;
    case 'V':
        return TORIRSK_V;
    case 'W':
        return TORIRSK_W;
    case 'X':
        return TORIRSK_X;
    case 'Y':
        return TORIRSK_Y;
    case 'Z':
        return TORIRSK_Z;
    case '0':
        return TORIRSK_0;
    case '1':
        return TORIRSK_1;
    case '2':
        return TORIRSK_2;
    case '3':
        return TORIRSK_3;
    case '4':
        return TORIRSK_4;
    case '5':
        return TORIRSK_5;
    case '6':
        return TORIRSK_6;
    case '7':
        return TORIRSK_7;
    case '8':
        return TORIRSK_8;
    case '9':
        return TORIRSK_9;
    case VK_ESCAPE:
        return TORIRSK_ESCAPE;
    case VK_RETURN:
        return TORIRSK_RETURN;
    case VK_BACK:
        return TORIRSK_BACKSPACE;
    case VK_INSERT:
        return TORIRSK_INSERT;
    case VK_DELETE:
        return TORIRSK_DELETE;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return TORIRSK_SHIFT;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return TORIRSK_CTRL;
    case VK_OEM_COMMA:
        return TORIRSK_COMMA;
    case VK_TAB:
        return TORIRSK_TAB;
    case VK_SPACE:
        return TORIRSK_SPACE;
    case VK_LEFT:
        return TORIRSK_LEFT;
    case VK_RIGHT:
        return TORIRSK_RIGHT;
    case VK_UP:
        return TORIRSK_UP;
    case VK_DOWN:
        return TORIRSK_DOWN;
    case VK_PRIOR:
        return TORIRSK_PAGE_UP;
    case VK_NEXT:
        return TORIRSK_PAGE_DOWN;
    default:
        return TORIRSK_UNKNOWN;
    }
}

static void
win32_nk_feed_vk(struct nk_context* ctx, WPARAM vk, int down)
{
    if( !ctx )
        return;
    switch( vk )
    {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        nk_input_key(ctx, NK_KEY_SHIFT, down);
        break;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        nk_input_key(ctx, NK_KEY_CTRL, down);
        break;
    case VK_RETURN:
        nk_input_key(ctx, NK_KEY_ENTER, down);
        break;
    case VK_TAB:
        nk_input_key(ctx, NK_KEY_TAB, down);
        break;
    case VK_BACK:
        nk_input_key(ctx, NK_KEY_BACKSPACE, down);
        break;
    case VK_DELETE:
        nk_input_key(ctx, NK_KEY_DEL, down);
        break;
    case VK_LEFT:
        nk_input_key(ctx, NK_KEY_LEFT, down);
        break;
    case VK_RIGHT:
        nk_input_key(ctx, NK_KEY_RIGHT, down);
        break;
    case VK_UP:
        nk_input_key(ctx, NK_KEY_UP, down);
        break;
    case VK_DOWN:
        nk_input_key(ctx, NK_KEY_DOWN, down);
        break;
    case VK_PRIOR:
        nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
        break;
    case VK_NEXT:
        nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
        break;
    case VK_HOME:
        nk_input_key(ctx, NK_KEY_TEXT_START, down);
        nk_input_key(ctx, NK_KEY_SCROLL_START, down);
        break;
    case VK_END:
        nk_input_key(ctx, NK_KEY_TEXT_END, down);
        nk_input_key(ctx, NK_KEY_SCROLL_END, down);
        break;
    case VK_ESCAPE:
        nk_input_key(ctx, NK_KEY_TEXT_RESET_MODE, down);
        break;
    default:
        break;
    }
}

static void
transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    struct Platform2_Win32* platform)
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

static LRESULT CALLBACK
Win32_Platform_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct Platform2_Win32* platform =
        (struct Platform2_Win32*)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch( msg )
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);
        int cw = cr.right - cr.left;
        int ch = cr.bottom - cr.top;
        if( platform && platform->win32_renderer_for_paint && cw > 0 && ch > 0 )
        {
            if( platform->win32_renderer_kind == TORIRS_WIN32_RENDERER_KIND_GDI )
            {
                PlatformImpl2_Win32_Renderer_GDISoft3D_PresentToHdc(
                    (struct Platform2_Win32_Renderer_GDISoft3D*)platform->win32_renderer_for_paint,
                    hdc,
                    cw,
                    ch);
            }
#if defined(TORIRS_HAS_D3D8)
            else if( platform->win32_renderer_kind == TORIRS_WIN32_RENDERER_KIND_D3D8 )
            {
                PlatformImpl2_Win32_Renderer_D3D8_PresentOrInvalidate(
                    (struct Platform2_Win32_Renderer_D3D8*)platform->win32_renderer_for_paint,
                    hdc,
                    cw,
                    ch);
            }
#endif
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if( platform )
        {
            int nw = LOWORD(lParam);
            int nh = HIWORD(lParam);
            if( nw > 0 && nh > 0 )
            {
                platform->window_width = nw;
                platform->window_height = nh;
                platform->drawable_width = nw;
                platform->drawable_height = nh;
            }
            if( platform->win32_renderer_for_paint )
            {
                if( platform->win32_renderer_kind == TORIRS_WIN32_RENDERER_KIND_GDI )
                {
                    PlatformImpl2_Win32_Renderer_GDISoft3D_MarkLetterboxDirty(
                        (struct Platform2_Win32_Renderer_GDISoft3D*)
                            platform->win32_renderer_for_paint);
                }
#if defined(TORIRS_HAS_D3D8)
                else if( platform->win32_renderer_kind == TORIRS_WIN32_RENDERER_KIND_D3D8 )
                {
                    PlatformImpl2_Win32_Renderer_D3D8_MarkResizeDirty(
                        (struct Platform2_Win32_Renderer_D3D8*)platform->win32_renderer_for_paint);
                }
#endif
            }
            /* Force a full-client WM_PAINT: when the window grows, BeginPaint's HDC is clipped
             * to the newly-exposed region. Without a full invalidation, FillRect would miss the
             * parts of the old client that are now letterbox bars (e.g. aspect flip on resize).
             * WM_ERASEBKGND still returns TRUE so there is no flicker from a default erase. */
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_CLOSE:
        if( platform && platform->poll_input )
            platform->poll_input->quit = 1;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        if( !platform )
            break;
        if( platform->nk_ctx_for_input )
            win32_nk_feed_vk((struct nk_context*)platform->nk_ctx_for_input, wParam, 1);
        enum GameInputKeyCode k = win32_vk_to_keycode(wParam, lParam);
        if( k == TORIRSK_UNKNOWN )
            break;
        struct GameInputEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TORIRSEV_KEY_DOWN;
        ev.key_down.key = k;
        win32_input_ring_push(&ev);
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        if( !platform )
            break;
        if( platform->nk_ctx_for_input )
            win32_nk_feed_vk((struct nk_context*)platform->nk_ctx_for_input, wParam, 0);
        enum GameInputKeyCode k = win32_vk_to_keycode(wParam, lParam);
        if( k == TORIRSK_UNKNOWN )
            break;
        struct GameInputEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TORIRSEV_KEY_UP;
        ev.key_up.key = k;
        win32_input_ring_push(&ev);
        return 0;
    }

    case WM_CHAR:
    {
        if( !platform || !platform->nk_ctx_for_input )
            break;
        nk_input_unicode((struct nk_context*)platform->nk_ctx_for_input, (nk_rune)(wParam & 0xFFFFu));
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if( !platform )
            break;
        int mx = (int)(short)LOWORD(lParam);
        int my = (int)(short)HIWORD(lParam);
        platform->tracked_mouse_x = mx;
        platform->tracked_mouse_y = my;

        int gx = 0;
        int gy = 0;
        transform_mouse_coordinates(mx, my, &gx, &gy, platform);

        if( platform->nk_ctx_for_input )
            nk_input_motion((struct nk_context*)platform->nk_ctx_for_input, gx, gy);

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
        win32_input_ring_push(&ev);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        if( !platform )
            break;
        int mx = (int)(short)LOWORD(lParam);
        int my = (int)(short)HIWORD(lParam);
        platform->tracked_mouse_x = mx;
        platform->tracked_mouse_y = my;
        int gx = 0;
        int gy = 0;
        transform_mouse_coordinates(mx, my, &gx, &gy, platform);
        if( platform->nk_ctx_for_input )
        {
            struct nk_context* nk = (struct nk_context*)platform->nk_ctx_for_input;
            if( msg == WM_LBUTTONDOWN )
                nk_input_button(nk, NK_BUTTON_LEFT, gx, gy, 1);
            else if( msg == WM_RBUTTONDOWN )
                nk_input_button(nk, NK_BUTTON_RIGHT, gx, gy, 1);
            else
                nk_input_button(nk, NK_BUTTON_MIDDLE, gx, gy, 1);
        }
        struct GameInputEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TORIRSEV_MOUSE_DOWN;
        ev.mouse_down.mouse_x = gx;
        ev.mouse_down.mouse_y = gy;
        if( msg == WM_LBUTTONDOWN )
            ev.mouse_down.button = TORIRSM_LEFT;
        else if( msg == WM_RBUTTONDOWN )
            ev.mouse_down.button = TORIRSM_RIGHT;
        else
            ev.mouse_down.button = TORIRSM_MIDDLE;
        win32_input_ring_push(&ev);
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    {
        if( !platform )
            break;
        int mx = (int)(short)LOWORD(lParam);
        int my = (int)(short)HIWORD(lParam);
        platform->tracked_mouse_x = mx;
        platform->tracked_mouse_y = my;
        int gx = 0;
        int gy = 0;
        transform_mouse_coordinates(mx, my, &gx, &gy, platform);
        if( platform->nk_ctx_for_input )
        {
            struct nk_context* nk = (struct nk_context*)platform->nk_ctx_for_input;
            if( msg == WM_LBUTTONUP )
                nk_input_button(nk, NK_BUTTON_LEFT, gx, gy, 0);
            else if( msg == WM_RBUTTONUP )
                nk_input_button(nk, NK_BUTTON_RIGHT, gx, gy, 0);
            else
                nk_input_button(nk, NK_BUTTON_MIDDLE, gx, gy, 0);
        }
        struct GameInputEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TORIRSEV_MOUSE_UP;
        ev.mouse_up.mouse_x = gx;
        ev.mouse_up.mouse_y = gy;
        if( msg == WM_LBUTTONUP )
            ev.mouse_up.button = TORIRSM_LEFT;
        else if( msg == WM_RBUTTONUP )
            ev.mouse_up.button = TORIRSM_RIGHT;
        else
            ev.mouse_up.button = TORIRSM_MIDDLE;
        win32_input_ring_push(&ev);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if( !platform )
            break;
        /* WM_MOUSEWHEEL: lParam is screen coordinates on Win32. */
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        ScreenToClient(hwnd, &pt);
        int mx = pt.x;
        int my = pt.y;
        platform->tracked_mouse_x = mx;
        platform->tracked_mouse_y = my;
        int gx = 0;
        int gy = 0;
        transform_mouse_coordinates(mx, my, &gx, &gy, platform);
        if( platform->nk_ctx_for_input )
        {
            float delta = (float)(short)HIWORD(wParam) / (float)WHEEL_DELTA;
            nk_input_scroll(
                (struct nk_context*)platform->nk_ctx_for_input,
                nk_vec2(0.0f, delta));
        }
        struct GameInputEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = TORIRSEV_MOUSE_WHEEL;
        ev.mouse_wheel.mouse_x = gx;
        ev.mouse_wheel.mouse_y = gy;
        ev.mouse_wheel.wheel = (int)(short)HIWORD(wParam);
        win32_input_ring_push(&ev);
        return 0;
    }

    default:
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static struct LuaGameType*
game_callback(
    void* ctx,
    struct LuaGameType* args);

struct Platform2_Win32*
Platform2_Win32_New(void)
{
    struct Platform2_Win32* platform =
        (struct Platform2_Win32*)malloc(sizeof(struct Platform2_Win32));
    memset(platform, 0, sizeof(struct Platform2_Win32));

    platform->lua_sidecar = LuaCSidecar_New(platform, game_callback);
    if( !platform->lua_sidecar )
    {
        free(platform);
        return NULL;
    }

    platform->cache_dat = cache_dat_new_from_directory(CACHE_PATH);

    return platform;
}

void
Platform2_Win32_Shutdown(struct Platform2_Win32* platform)
{
    if( !platform )
        return;
    if( platform->hwnd )
    {
        DestroyWindow((HWND)platform->hwnd);
        platform->hwnd = NULL;
    }
}

void
Platform2_Win32_Free(struct Platform2_Win32* platform)
{
    if( !platform )
        return;
    Platform2_Win32_Shutdown(platform);
    if( platform->lua_sidecar )
        LuaCSidecar_Free(platform->lua_sidecar);
    if( platform->cache_dat )
        cache_dat_free(platform->cache_dat);
    free(platform);
}

static const char kWin32ClassName[] = "TorIRSWin32Platform";

bool
Platform2_Win32_InitForSoft3D(
    struct Platform2_Win32* platform,
    int screen_width,
    int screen_height)
{
    if( !platform || screen_width <= 0 || screen_height <= 0 )
        return false;

    HINSTANCE hinst = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    if( !GetClassInfoA(hinst, kWin32ClassName, &wc) )
    {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = Win32_Platform_WndProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kWin32ClassName;
        if( !RegisterClassA(&wc) )
        {
            DWORD err = GetLastError();
            if( err != ERROR_CLASS_ALREADY_EXISTS )
            {
                printf("RegisterClassA failed: %lu\n", (unsigned long)err);
                return false;
            }
        }
    }
    else
    {
        (void)wc; /* Class already registered; wc filled by GetClassInfoA. */
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, screen_width, screen_height };
    AdjustWindowRect(&rc, style, FALSE);
    int win_w = rc.right - rc.left;
    int win_h = rc.bottom - rc.top;

    HWND hwnd = CreateWindowExA(
        0,
        kWin32ClassName,
        "TorIRS (Win32)",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        win_w,
        win_h,
        NULL,
        NULL,
        hinst,
        NULL);
    if( !hwnd )
    {
        printf("CreateWindowExA failed\n");
        return false;
    }

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)platform);
    platform->hwnd = (void*)hwnd;

    RECT client;
    GetClientRect(hwnd, &client);
    int cw = client.right - client.left;
    int ch = client.bottom - client.top;
    platform->window_width = cw;
    platform->window_height = ch;
    platform->drawable_width = cw;
    platform->drawable_height = ch;
    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;
    platform->display_scale = 1.0f;
    platform->last_frame_time_ticks = (uint32_t)GetTickCount();

    s_input_ring_head = s_input_ring_tail = 0;
    s_have_prev_mouse = 0;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    return true;
}

void
Platform2_Win32_PollEvents(
    struct Platform2_Win32* platform,
    struct GInput* input,
    int chat_focused)
{
    (void)chat_focused;
    if( !platform || !input )
        return;

    platform->poll_input = input;

    struct nk_context* nk_ctx = (struct nk_context*)platform->nk_ctx_for_input;
    if( nk_ctx )
        nk_input_begin(nk_ctx);

    MSG msg;
    while( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) )
    {
        if( msg.message == WM_QUIT )
        {
            input->quit = 1;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if( nk_ctx )
        nk_input_end(nk_ctx);

    double time_s = (double)(uint32_t)GetTickCount() / 1000.0;
    double time_delta_seconds = time_s - input->time;
    input->time_delta_accumulator_seconds += time_delta_seconds;
    input->time = time_s;

    win32_input_ring_drain_to(input);

    if( platform->hwnd )
    {
        int mx = platform->tracked_mouse_x;
        int my = platform->tracked_mouse_y;
        transform_mouse_coordinates(mx, my, &input->mouse_state.x, &input->mouse_state.y, platform);
    }

    platform->poll_input = NULL;
}

static struct LuaGameType*
game_callback(
    void* ctx,
    struct LuaGameType* args)
{
    struct Platform2_Win32* platform = (struct Platform2_Win32*)ctx;
    struct BuildCacheDat* bcd = platform->current_game->buildcachedat;

    struct LuaGameType* command_gametype = LuaGameType_GetVarTypeArrayAt(args, 0);
    assert(command_gametype->kind == LUAGAMETYPE_INT);
    LuaApiId api_id = (LuaApiId)LuaGameType_GetInt(command_gametype);

    struct LuaGameType* args_view = LuaGameType_NewVarTypeArrayView(args, 1);
    struct LuaGameType* result = lua_api_dispatch(
        platform->current_game,
        bcd,
        platform->current_game->sys_dash,
        api_id,
        args_view);
    LuaGameType_Free(args_view);

    if( result )
        return result;
    return LuaGameType_NewVoid();
}

static struct LuaGameType*
on_lua_async_call(
    struct Platform2_Win32* platform,
    int command,
    struct LuaGameType* args)
{
    struct CacheDat* cache_dat = platform->cache_dat;

    switch( command )
    {
    case FUNC_LOAD_ARCHIVE:
        return LuaCSidecar_CachedatLoadArchive(cache_dat, args);
    case FUNC_LOAD_ARCHIVES:
        return LuaCSidecar_CachedatLoadArchives(cache_dat, args);
    case FUNC_LOAD_CONFIG_FILE:
        return LuaCSidecar_Config_LoadConfig(args);
    case FUNC_LOAD_CONFIG_FILES:
    {
        struct LuaGameType* result = LuaCSidecar_Config_LoadConfigs(args);
        int n = LuaGameType_GetUserDataArrayCount(result);
        for( int i = 0; i < n; i++ )
        {
            void* file = LuaGameType_GetUserDataArrayAt(result, i);
            if( file )
                LuaCSidecar_TrackConfigFile(platform->lua_sidecar, (struct LuaConfigFile*)file);
        }
        return result;
    }
    default:
        assert(false && "Unknown cachedat function");
        return NULL;
    }
}

void
Platform2_Win32_RunLuaScripts(struct Platform2_Win32* platform, struct GGame* game)
{
    while( !LibToriRS_LuaScriptQueueIsEmpty(game) )
    {
        struct LuaGameScript script;
        memset(&script, 0, sizeof(script));
        LibToriRS_LuaScriptQueuePop(game, &script);

        if( !platform->lua_sidecar )
            return;

        struct LuaCYield yield = { 0 };
        int script_status = LuaCSidecar_RunScript(platform->lua_sidecar, &script, &yield);
        LuaGameType_Free(script.args);
        script.args = NULL;

        while( script_status == LUACSIDECAR_YIELDED )
        {
            struct LuaGameType* result = on_lua_async_call(platform, yield.command, yield.args);
            LuaGameType_Free(yield.args);
            yield.args = NULL;

            script_status = LuaCSidecar_ResumeScript(platform->lua_sidecar, result, &yield);
            LuaGameType_Free(result);
        }

        LuaCSidecar_GC(platform->lua_sidecar);

        void* pkt_free = script.lc245_packet_item_to_free;
        if( pkt_free )
        {
            gameproto_free_lc245_2_item((struct RevPacket_LC245_2_Item*)pkt_free);
            free(pkt_free);
        }
    }
}

#endif /* _WIN32 */
