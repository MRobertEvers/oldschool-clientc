#include "platform_sdl2.h"

#include "graphics/dash.h"
#include "libtorirs.h"
#include "libtorirs_internal.h"
#include "toridraw/toridraw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LibToriPlatformSDL2
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    int* pixel_buffer;
    int width;
    int height;
    struct ToriDraw_ViewPort viewport;
    struct ToriDraw_Camera camera;
};

static struct ToriDraw_ModelHandle __attribute__((unused))
toridraw_handle_from_dash_model(struct DashModel* dash_model)
{
    struct ToriDraw_ModelHandle hnd = { .kind = TORIDRAWMK_NONE };
    if( !dash_model )
        return hnd;
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = (struct ToriDraw_Model*)(void*)dash_model;
    return hnd;
}

static void
init_viewport(
    struct ToriDraw_ViewPort* viewport,
    int width,
    int height)
{
    viewport->width = width;
    viewport->height = height;
    viewport->stride = width;
    viewport->x_center = width / 2;
    viewport->y_center = height / 2;
    viewport->clip_left = 0;
    viewport->clip_top = 0;
    viewport->clip_right = width;
    viewport->clip_bottom = height;
}

static void
init_camera(struct ToriDraw_Camera* camera)
{
    memset(camera, 0, sizeof(struct ToriDraw_Camera));
    camera->fov_rpi2048 = 512;
    camera->near_plane_z = 50;
    camera->pitch = 148;
    camera->yaw = 0;
    camera->roll = 0;
}

struct LibToriPlatformSDL2*
LibToriPlatformSDL2_New(void)
{
    struct LibToriPlatformSDL2* platform = malloc(sizeof(struct LibToriPlatformSDL2));
    if( !platform )
        return NULL;
    memset(platform, 0, sizeof(struct LibToriPlatformSDL2));
    return platform;
}

void
LibToriPlatformSDL2_Free(struct LibToriPlatformSDL2* platform)
{
    if( !platform )
        return;
    if( platform->pixel_buffer )
    {
        free(platform->pixel_buffer);
        platform->pixel_buffer = NULL;
    }
    if( platform->texture )
    {
        SDL_DestroyTexture(platform->texture);
        platform->texture = NULL;
    }
    if( platform->renderer )
    {
        SDL_DestroyRenderer(platform->renderer);
        platform->renderer = NULL;
    }
    if( platform->window )
    {
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
    }
    SDL_Quit();
    free(platform);
}

bool
LibToriPlatformSDL2_InitForSoft3D(
    struct LibToriPlatformSDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !platform )
        return false;
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->renderer = SDL_CreateRenderer(platform->window, -1, SDL_RENDERER_ACCELERATED);
    if( !platform->renderer )
    {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(platform->window);
        platform->window = NULL;
        SDL_Quit();
        return false;
    }

    platform->width = screen_width;
    platform->height = screen_height;

    size_t const pixel_count = (size_t)screen_width * (size_t)screen_height;
    platform->pixel_buffer = (int*)malloc(pixel_count * sizeof(int));
    if( !platform->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        LibToriPlatformSDL2_Free(platform);
        return false;
    }
    memset(platform->pixel_buffer, 0, pixel_count * sizeof(int));

    platform->texture = SDL_CreateTexture(
        platform->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        screen_width,
        screen_height);
    if( !platform->texture )
    {
        printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
        LibToriPlatformSDL2_Free(platform);
        return false;
    }

    init_viewport(&platform->viewport, screen_width, screen_height);
    init_camera(&platform->camera);

    return true;
}

bool
LibToriPlatformSDL2_InitForOpenGL3(
    struct LibToriPlatformSDL2* platform,
    int screen_width,
    int screen_height)
{
    if( !platform )
        return false;
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    platform->width = screen_width;
    platform->height = screen_height;
    platform->renderer = NULL;
    platform->texture = NULL;
    platform->pixel_buffer = NULL;

    return true;
}

SDL_Window*
LibToriPlatformSDL2_GetWindow(struct LibToriPlatformSDL2* platform)
{
    if( !platform )
        return NULL;
    return platform->window;
}

void
LibToriPlatformSDL2_Render(
    struct LibToriPlatformSDL2* platform,
    struct LibToriRS_Instance* instance,
    int model_id)
{
    (void)model_id;
    if( !platform || !platform->pixel_buffer || !platform->texture || !platform->renderer )
        return;

    size_t const pixel_count = (size_t)platform->width * (size_t)platform->height;
    memset(platform->pixel_buffer, 0, pixel_count * sizeof(int));

    struct ToriDraw_Context* draw_context = NULL;
    if( instance && instance->model_viewer )
        draw_context = instance->model_viewer->context;

    bool has_3d = false;
    struct LibToriRS_RenderCommand_Begin3D cur_3d;

    LibToriRS_FrameBegin(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
    {
        switch( command.kind )
        {
        case TORIRSRC_BEGIN_3D:
            cur_3d = command.u.begin_3d;
            has_3d = true;
            break;
        case TORIRSRC_END_3D:
            has_3d = false;
            break;
        case TORIRSRC_MODEL:
            if( !draw_context || !has_3d )
                break;
            {
                struct ToriDraw_Position tmp_pos = cur_3d.camera_position;
                struct ToriDraw_ViewPort tmp_vp = cur_3d.view_port;
                struct ToriDraw_Camera tmp_cam = cur_3d.camera;
                toridraw_render_model(
                    command.u.model.model,
                    draw_context,
                    &tmp_pos,
                    &tmp_vp,
                    &tmp_cam,
                    platform->pixel_buffer);
            }
            break;
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(instance);

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        platform->pixel_buffer,
        platform->width,
        platform->height,
        32,
        platform->width * (int)sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);
    if( !surface )
        return;

    int* pix_write = NULL;
    int texture_pitch = 0;
    if( SDL_LockTexture(platform->texture, NULL, (void**)&pix_write, &texture_pitch) < 0 )
    {
        SDL_FreeSurface(surface);
        return;
    }

    int* src_pixels = (int*)surface->pixels;
    int texture_w = texture_pitch / (int)sizeof(int);

    memset(pix_write, 0, (size_t)texture_pitch * (size_t)platform->height);

    for( int src_y = 0; src_y < platform->height; src_y++ )
    {
        int* row = &pix_write[src_y * texture_w];
        memcpy(row, &src_pixels[src_y * platform->width], (size_t)platform->width * sizeof(int));
    }

    SDL_UnlockTexture(platform->texture);
    SDL_FreeSurface(surface);

    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(platform->window, &window_width, &window_height);

    SDL_Rect dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = platform->width;
    dst_rect.h = platform->height;

    if( window_width > 0 && window_height > 0 )
    {
        float src_aspect = (float)platform->width / (float)platform->height;
        float window_aspect = (float)window_width / (float)window_height;

        if( src_aspect > window_aspect )
        {
            dst_rect.w = window_width;
            dst_rect.h = (int)(window_width / src_aspect);
            dst_rect.x = 0;
            dst_rect.y = (window_height - dst_rect.h) / 2;
        }
        else
        {
            dst_rect.h = window_height;
            dst_rect.w = (int)(window_height * src_aspect);
            dst_rect.y = 0;
            dst_rect.x = (window_width - dst_rect.w) / 2;
        }
    }

    SDL_RenderClear(platform->renderer);
    SDL_RenderCopy(platform->renderer, platform->texture, NULL, &dst_rect);
    SDL_RenderPresent(platform->renderer);
}

void
LibToriPlatformSDL2_PollEvents(
    struct LibToriPlatformSDL2* platform,
    struct LibToriRS_CommandQueue* command_queue)
{
    if( !platform )
        return;
    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        switch( event.type )
        {
        case SDL_QUIT:
            LibToriRS_CommandQueue_PushQuitEvent(command_queue);
            break;
        case SDL_KEYDOWN:
            LibToriRS_CommandQueue_PushKeyEvent(
                command_queue,
                LibToriPlatformSDL2_SDLKeyCodeToKeyCode(event.key.keysym.sym),
                TORIRSEV_KEY_DOWN);
            break;
        case SDL_KEYUP:
            LibToriRS_CommandQueue_PushKeyEvent(
                command_queue,
                LibToriPlatformSDL2_SDLKeyCodeToKeyCode(event.key.keysym.sym),
                TORIRSEV_KEY_UP);
            break;
        case SDL_MOUSEBUTTONDOWN:
            LibToriRS_CommandQueue_PushMouseButtonEvent(
                command_queue,
                LibToriPlatformSDL2_SDLMouseButtonToMouseButton(event.button.button),
                TORIRSEV_MOUSE_DOWN,
                event.button.x,
                event.button.y);
            break;
        case SDL_MOUSEBUTTONUP:
            LibToriRS_CommandQueue_PushMouseButtonEvent(
                command_queue,
                LibToriPlatformSDL2_SDLMouseButtonToMouseButton(event.button.button),
                TORIRSEV_MOUSE_UP,
                event.button.x,
                event.button.y);
            break;
        case SDL_MOUSEMOTION:
            LibToriRS_CommandQueue_PushMouseMoveEvent(
                command_queue,
                event.motion.x,
                event.motion.y,
                event.motion.xrel,
                event.motion.yrel);
            break;
        }
    }
}

enum LibToriRS_KeyCode
LibToriPlatformSDL2_SDLKeyCodeToKeyCode(SDL_Keycode key_code)
{
    switch( key_code )
    {
    case SDLK_a:
        return TORIRSK_A;
    case SDLK_b:
        return TORIRSK_B;
    case SDLK_c:
        return TORIRSK_C;
    case SDLK_d:
        return TORIRSK_D;
    case SDLK_e:
        return TORIRSK_E;
    case SDLK_f:
        return TORIRSK_F;
    case SDLK_g:
        return TORIRSK_G;
    case SDLK_h:
        return TORIRSK_H;
    case SDLK_i:
        return TORIRSK_I;
    case SDLK_j:
        return TORIRSK_J;
    case SDLK_k:
        return TORIRSK_K;
    case SDLK_l:
        return TORIRSK_L;
    case SDLK_m:
        return TORIRSK_M;
    case SDLK_n:
        return TORIRSK_N;
    case SDLK_o:
        return TORIRSK_O;
    case SDLK_p:
        return TORIRSK_P;
    case SDLK_q:
        return TORIRSK_Q;
    case SDLK_r:
        return TORIRSK_R;
    case SDLK_s:
        return TORIRSK_S;
    case SDLK_t:
        return TORIRSK_T;
    case SDLK_u:
        return TORIRSK_U;
    case SDLK_v:
        return TORIRSK_V;
    case SDLK_w:
        return TORIRSK_W;
    case SDLK_x:
        return TORIRSK_X;
    case SDLK_y:
        return TORIRSK_Y;
    case SDLK_z:
        return TORIRSK_Z;
    case SDLK_0:
        return TORIRSK_0;
    case SDLK_1:
        return TORIRSK_1;
    case SDLK_2:
        return TORIRSK_2;
    case SDLK_3:
        return TORIRSK_3;
    case SDLK_4:
        return TORIRSK_4;
    case SDLK_5:
        return TORIRSK_5;
    case SDLK_6:
        return TORIRSK_6;
    case SDLK_7:
        return TORIRSK_7;
    case SDLK_8:
        return TORIRSK_8;
    case SDLK_9:
        return TORIRSK_9;
    case SDLK_ESCAPE:
        return TORIRSK_ESCAPE;
    case SDLK_RETURN:
        return TORIRSK_RETURN;
    case SDLK_BACKSPACE:
        return TORIRSK_BACKSPACE;
    case SDLK_INSERT:
        return TORIRSK_INSERT;
    case SDLK_DELETE:
        return TORIRSK_DELETE;
    case SDLK_LSHIFT:
        return TORIRSK_SHIFT;
    case SDLK_RSHIFT:
        return TORIRSK_SHIFT;
    case SDLK_LCTRL:
        return TORIRSK_CTRL;
    case SDLK_RCTRL:
        return TORIRSK_CTRL;
    case SDLK_TAB:
        return TORIRSK_TAB;
    case SDLK_SPACE:
        return TORIRSK_SPACE;
    case SDLK_LEFT:
        return TORIRSK_LEFT;
    case SDLK_RIGHT:
        return TORIRSK_RIGHT;
    case SDLK_UP:
        return TORIRSK_UP;
    case SDLK_DOWN:
        return TORIRSK_DOWN;
    case SDLK_PAGEUP:
        return TORIRSK_PAGE_UP;
    case SDLK_PAGEDOWN:
        return TORIRSK_PAGE_DOWN;
    case SDLK_COMMA:
        return TORIRSK_COMMA;
    default:
        return TORIRSK_UNKNOWN;
    }
    return TORIRSK_UNKNOWN;
}

enum LibToriRS_MouseButton
LibToriPlatformSDL2_SDLMouseButtonToMouseButton(int mouse_button)
{
    switch( mouse_button )
    {
    case SDL_BUTTON_LEFT:
        return TORIRSM_LEFT;
    case SDL_BUTTON_MIDDLE:
        return TORIRSM_MIDDLE;
    case SDL_BUTTON_RIGHT:
        return TORIRSM_RIGHT;
    default:
        return TORIRSM_UNKNOWN;
    }
    return TORIRSM_UNKNOWN;
}
