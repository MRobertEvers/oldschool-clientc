#include "osrs/cache.h"
#include "osrs/render.h"
#include "osrs/tables/model.h"

#include <SDL.h>
#include <stdbool.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

int g_sin_table[2048];
int g_cos_table[2048];
int g_tan_table[2048];

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
int g_hsl16_to_rgb_table[65536];

int
pix3d_set_gamma(int rgb, double gamma)
{
    double r = (double)(rgb >> 16) / 256.0;
    double g = (double)(rgb >> 8 & 0xff) / 256.0;
    double b = (double)(rgb & 0xff) / 256.0;
    double powR = pow(r, gamma);
    double powG = pow(g, gamma);
    double powB = pow(b, gamma);
    int intR = (int)(powR * 256.0);
    int intG = (int)(powG * 256.0);
    int intB = (int)(powB * 256.0);
    return (intR << 16) + (intG << 8) + intB;
}

void
pix3d_set_brightness(int* palette, double brightness)
{
    double random_brightness = brightness;
    int offset = 0;
    for( int y = 0; y < 512; y++ )
    {
        double hue = (double)(y / 8) / 64.0 + 0.0078125;
        double saturation = (double)(y & 0x7) / 8.0 + 0.0625;
        for( int x = 0; x < 128; x++ )
        {
            double lightness = (double)x / 128.0;
            double r = lightness;
            double g = lightness;
            double b = lightness;
            if( saturation != 0.0 )
            {
                double q;
                if( lightness < 0.5 )
                {
                    q = lightness * (saturation + 1.0);
                }
                else
                {
                    q = lightness + saturation - lightness * saturation;
                }
                double p = lightness * 2.0 - q;
                double t = hue + 0.3333333333333333;
                if( t > 1.0 )
                {
                    t--;
                }
                double d11 = hue - 0.3333333333333333;
                if( d11 < 0.0 )
                {
                    d11++;
                }
                if( t * 6.0 < 1.0 )
                {
                    r = p + (q - p) * 6.0 * t;
                }
                else if( t * 2.0 < 1.0 )
                {
                    r = q;
                }
                else if( t * 3.0 < 2.0 )
                {
                    r = p + (q - p) * (0.6666666666666666 - t) * 6.0;
                }
                else
                {
                    r = p;
                }
                if( hue * 6.0 < 1.0 )
                {
                    g = p + (q - p) * 6.0 * hue;
                }
                else if( hue * 2.0 < 1.0 )
                {
                    g = q;
                }
                else if( hue * 3.0 < 2.0 )
                {
                    g = p + (q - p) * (0.6666666666666666 - hue) * 6.0;
                }
                else
                {
                    g = p;
                }
                if( d11 * 6.0 < 1.0 )
                {
                    b = p + (q - p) * 6.0 * d11;
                }
                else if( d11 * 2.0 < 1.0 )
                {
                    b = q;
                }
                else if( d11 * 3.0 < 2.0 )
                {
                    b = p + (q - p) * (0.6666666666666666 - d11) * 6.0;
                }
                else
                {
                    b = p;
                }
            }
            int intR = (int)(r * 256.0);
            int intG = (int)(g * 256.0);
            int intB = (int)(b * 256.0);
            int rgb = (intR << 16) + (intG << 8) + intB;
            int rgbAdjusted = pix3d_set_gamma(rgb, random_brightness);
            palette[offset++] = rgbAdjusted;
        }
    }
}

void
init_hsl16_to_rgb_table()
{
    pix3d_set_brightness(g_hsl16_to_rgb_table, 0.8);
}

void
init_sin_table()
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(sin((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_sin_table[i] = (int)(sin((double)i * 0.0030679615) * (1 << 16));
}

void
init_cos_table()
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(cos((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_cos_table[i] = (int)(cos((double)i * 0.0030679615) * (1 << 16));
}

void
init_tan_table()
{
    for( int i = 0; i < 2048; i++ )
        g_tan_table[i] = (int)(tan((double)i * 0.0030679615) * (1 << 16));
}

struct Game
{
    int camera_yaw;
    int camera_pitch;
    int camera_roll;
    int camera_fov;
    int camera_x;
    int camera_y;
    int camera_z;

    int model_yaw;
    int model_pitch;
    int model_roll;
    int model_fov;
    int model_x;
    int model_y;
    int model_z;

    bool mouse_held;
    int last_mouse_x;
    int last_mouse_y;

    int model_id;
    int frame_id;

    // Business end
    struct Cache* cache;
};

enum MouseFlag
{
    MOUSE_FLAG_ACTIVE = 1 << 0,
    MOUSE_FLAG_EV_DOWN = 1 << 1,
    MOUSE_FLAG_EV_UP = 1 << 2,
    MOUSE_FLAG_EV_MOVED = 1 << 3,
};

#define MOUSE_FLAG_CLEAR_EV(flags) (flags & MOUSE_FLAG_ACTIVE)

enum KeyFlag
{
    KEY_FLAG_ACTIVE = 1 << 0,
    KEY_FLAG_EV_UP = 1 << 1,
    KEY_FLAG_EV_DOWN = 1 << 2,
};

#define KEY_FLAG_CLEAR_EV(flags) (flags & KEY_FLAG_ACTIVE)

struct GameInput
{
    bool quit;

    int mouse_x;
    int mouse_y;

    int mouse_clicked_ev_x;
    int mouse_clicked_ev_y;
    // b1 mouse down, b2 mouse up, b3 mouse moved
    char mouse;
    int mouse_wheel_ev;

    // Keys we care about.
    // b0 key is down, b1 key released, b2 key pressed (on down)
    char key_w;
    char key_a;
    char key_s;
    char key_d;
    char key_up;
    char key_down;
    char key_left;
    char key_right;
    char key_q;
    char key_e;
    char key_i;
    char key_k;
    char key_j;
    char key_l;
    char key_u;
    char key_o;
    char key_f;
    char key_g;
};

struct PlatformSDL2
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;

    int* pixel_buffer;
};

static void
game_input_sdl2(struct GameInput* input)
{
    SDL_Event event;

    input->mouse_wheel_ev = 0;

    input->mouse = MOUSE_FLAG_CLEAR_EV(input->mouse);
    input->key_a = KEY_FLAG_CLEAR_EV(input->key_a);
    input->key_w = KEY_FLAG_CLEAR_EV(input->key_w);
    input->key_s = KEY_FLAG_CLEAR_EV(input->key_s);
    input->key_d = KEY_FLAG_CLEAR_EV(input->key_d);
    input->key_up = KEY_FLAG_CLEAR_EV(input->key_up);
    input->key_down = KEY_FLAG_CLEAR_EV(input->key_down);
    input->key_left = KEY_FLAG_CLEAR_EV(input->key_left);
    input->key_right = KEY_FLAG_CLEAR_EV(input->key_right);

    while( SDL_PollEvent(&event) )
    {
        switch( event.type )
        {
        case SDL_QUIT:
            input->quit = true;
            goto done;
        case SDL_MOUSEBUTTONDOWN:
            if( event.button.button == SDL_BUTTON_LEFT )
            {
                input->mouse_clicked_ev_x = event.button.x;
                input->mouse_clicked_ev_y = event.button.y;
                input->mouse |= MOUSE_FLAG_ACTIVE | MOUSE_FLAG_EV_DOWN;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if( event.button.button == SDL_BUTTON_LEFT )
                input->mouse |= MOUSE_FLAG_EV_UP;
            break;
        case SDL_MOUSEMOTION:
            input->mouse |= MOUSE_FLAG_EV_MOVED;

            input->mouse_x = event.motion.x;
            input->mouse_y = event.motion.y;
            break;
        case SDL_MOUSEWHEEL:
            input->mouse_wheel_ev = event.wheel.y;
            break;
        case SDL_KEYDOWN:
        {
            switch( event.key.keysym.sym )
            {
            case SDLK_ESCAPE:
                goto done;
            case SDLK_UP:
                input->key_up |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_DOWN:
                input->key_down |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_LEFT:
                input->key_left |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_RIGHT:
                input->key_right |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_GREATER:
                break;
            case SDLK_LESS:
                input->key_down |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_w:
                input->key_w |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_s:
                input->key_s |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_a:
                input->key_a |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_d:
                input->key_d |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_q:
                input->key_q |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_e:
                input->key_e |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_i:
                input->key_i |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_k:
                input->key_k |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_j:
                input->key_j |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_l:
                input->key_l |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_u:
                input->key_u |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_o:
                input->key_o |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_f:
                input->key_f |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            case SDLK_g:
                input->key_g |= KEY_FLAG_ACTIVE | KEY_FLAG_EV_DOWN;
                break;
            }
            break;
        }
        case SDL_KEYUP:
        {
            switch( event.key.keysym.sym )
            {
            case SDLK_UP:
                input->key_up &= ~KEY_FLAG_ACTIVE;
                input->key_up |= KEY_FLAG_EV_UP;
                break;
            case SDLK_DOWN:
                input->key_down &= ~KEY_FLAG_ACTIVE;
                input->key_down |= KEY_FLAG_EV_UP;
                break;
            case SDLK_LEFT:
                input->key_left &= ~KEY_FLAG_ACTIVE;
                input->key_left |= KEY_FLAG_EV_UP;
                break;
            case SDLK_RIGHT:
                input->key_right &= ~KEY_FLAG_ACTIVE;
                input->key_right |= KEY_FLAG_EV_UP;
                break;
            case SDLK_GREATER:
                break;
            case SDLK_LESS:
                input->key_down &= ~KEY_FLAG_ACTIVE;
                input->key_down |= KEY_FLAG_EV_UP;
                break;
            case SDLK_w:
                input->key_w &= ~KEY_FLAG_ACTIVE;
                input->key_w |= KEY_FLAG_EV_UP;
                break;
            case SDLK_s:
                input->key_s &= ~KEY_FLAG_ACTIVE;
                input->key_s |= KEY_FLAG_EV_UP;
                break;
            case SDLK_a:
                input->key_a &= ~KEY_FLAG_ACTIVE;
                input->key_a |= KEY_FLAG_EV_UP;
                break;
            case SDLK_d:
                input->key_d &= ~KEY_FLAG_ACTIVE;
                input->key_d |= KEY_FLAG_EV_UP;
                break;
            case SDLK_q:
                input->key_q &= ~KEY_FLAG_ACTIVE;
                input->key_q |= KEY_FLAG_EV_UP;
                break;
            case SDLK_e:
                input->key_e &= ~KEY_FLAG_ACTIVE;
                input->key_e |= KEY_FLAG_EV_UP;
                break;
            case SDLK_i:
                input->key_i &= ~KEY_FLAG_ACTIVE;
                input->key_i |= KEY_FLAG_EV_UP;
                break;
            case SDLK_k:
                input->key_k &= ~KEY_FLAG_ACTIVE;
                input->key_k |= KEY_FLAG_EV_UP;
                break;
            case SDLK_j:
                input->key_j &= ~KEY_FLAG_ACTIVE;
                input->key_j |= KEY_FLAG_EV_UP;
                break;
            case SDLK_l:
                input->key_l &= ~KEY_FLAG_ACTIVE;
                input->key_l |= KEY_FLAG_EV_UP;
                break;
            case SDLK_u:
                input->key_u &= ~KEY_FLAG_ACTIVE;
                input->key_u |= KEY_FLAG_EV_UP;
                break;
            case SDLK_o:
                input->key_o &= ~KEY_FLAG_ACTIVE;
                input->key_o |= KEY_FLAG_EV_UP;
                break;
            case SDLK_f:
                input->key_f &= ~KEY_FLAG_ACTIVE;
                input->key_f |= KEY_FLAG_EV_UP;
                break;
            case SDLK_g:
                input->key_g &= ~KEY_FLAG_ACTIVE;
                input->key_g |= KEY_FLAG_EV_UP;
                break;
            }
            break;
        }
        default:

            break;
        }
    }
done:
    return;
}

static bool
game_init(struct Game* game)
{
    game->cache = cache_new_from_directory("../cache");
    if( !game->cache )
        return false;

    game->model_id = 9319;
    game->camera_z = 420;

    return true;
}

static void
game_update(struct Game* game, struct GameInput* input)
{
    static const int CAMERA_SPEED = 10;
    static const int CAMERA_ANGLE_SPEED = 10;
    if( input->key_w & KEY_FLAG_ACTIVE )
        game->camera_z += CAMERA_SPEED;

    if( input->key_s & KEY_FLAG_ACTIVE )
        game->camera_z -= CAMERA_SPEED;

    if( input->key_a & KEY_FLAG_ACTIVE )
        game->camera_x -= CAMERA_SPEED;

    if( input->key_d & KEY_FLAG_ACTIVE )
        game->camera_x += CAMERA_SPEED;

    if( input->key_up & KEY_FLAG_ACTIVE )
        game->camera_pitch = (game->camera_pitch + CAMERA_ANGLE_SPEED) % 2048;

    if( input->key_down & KEY_FLAG_ACTIVE )
        game->camera_pitch = (game->camera_pitch - CAMERA_ANGLE_SPEED + 2048) % 2048;

    if( input->key_left & KEY_FLAG_ACTIVE )
        game->camera_yaw = (game->camera_yaw - CAMERA_ANGLE_SPEED + 2048) % 2048;

    if( input->key_right & KEY_FLAG_ACTIVE )
        game->camera_yaw = (game->camera_yaw + CAMERA_ANGLE_SPEED) % 2048;

    if( input->key_q & KEY_FLAG_ACTIVE )
        game->camera_roll = (game->camera_roll - CAMERA_ANGLE_SPEED + 2048) % 2048;

    if( input->key_e & KEY_FLAG_ACTIVE )
        game->camera_roll = (game->camera_roll + CAMERA_ANGLE_SPEED) % 2048;

    if( input->key_i & KEY_FLAG_ACTIVE )
        game->camera_fov += 1;

    if( input->key_k & KEY_FLAG_ACTIVE )
        game->camera_fov -= 1;

    if( input->key_j & KEY_FLAG_ACTIVE )
        game->model_yaw = (game->model_yaw + CAMERA_ANGLE_SPEED) % 2048;

    if( input->key_l & KEY_FLAG_ACTIVE )
        game->model_yaw = (game->model_yaw - CAMERA_ANGLE_SPEED + 2048) % 2048;

    if( input->key_u & KEY_FLAG_ACTIVE )
        game->model_pitch = (game->model_pitch + CAMERA_ANGLE_SPEED) % 2048;

    if( input->key_o & KEY_FLAG_ACTIVE )
        game->model_pitch = (game->model_pitch - CAMERA_ANGLE_SPEED + 2048) % 2048;

    if( input->key_f & KEY_FLAG_ACTIVE )
        game->model_roll = (game->model_roll + CAMERA_ANGLE_SPEED) % 2048;

    if( input->key_g & KEY_FLAG_ACTIVE )
        game->model_roll = (game->model_roll - CAMERA_ANGLE_SPEED + 2048) % 2048;

    if( game->camera_fov < 171 ) // 20 degrees in units of 2048ths of 2π (20/360 * 2048)
        game->camera_fov = 171;
    if( game->camera_fov > 1280 ) // 150 degrees in units of 2048ths of 2π (150/360 * 2048)
        game->camera_fov = 1280;

    if( input->mouse_wheel_ev )
        game->camera_z += input->mouse_wheel_ev * 10;

    if( input->mouse & MOUSE_FLAG_ACTIVE && input->mouse & MOUSE_FLAG_EV_MOVED )
    {
        if( game->mouse_held )
        {
            int mouse_delta_x = input->mouse_x - game->last_mouse_x;
            int mouse_delta_y = input->mouse_y - game->last_mouse_y;
            while( mouse_delta_x < 0 )
                mouse_delta_x += 2048;
            while( mouse_delta_y < 0 )
                mouse_delta_y += 2048;

            game->model_yaw = (game->model_yaw + (mouse_delta_x)) % 2048;
            game->model_pitch = (game->model_pitch + (mouse_delta_y)) % 2048;
        }

        game->last_mouse_x = input->mouse_x;
        game->last_mouse_y = input->mouse_y;
    }

    if( input->mouse & MOUSE_FLAG_EV_DOWN )
    {
        game->mouse_held = true;
        game->last_mouse_x = input->mouse_x;
        game->last_mouse_y = input->mouse_y;
    }

    if( input->mouse & MOUSE_FLAG_EV_UP )
        game->mouse_held = false;
}

static void
game_render_sdl2(struct Game* game, struct PlatformSDL2* platform)
{
    SDL_Texture* texture = platform->texture;
    SDL_Renderer* renderer = platform->renderer;
    int* pixel_buffer = platform->pixel_buffer;
    memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    struct Model* model = model_new_from_cache(game->cache, game->model_id);

    struct ModelBones* bones = modelbones_new_decode(model->vertex_bone_map, model->vertex_count);

    render_model_frame(
        pixel_buffer,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        10,
        game->model_yaw,
        game->model_pitch,
        game->model_roll,
        game->camera_x,
        game->camera_y,
        game->camera_z,
        game->camera_yaw,
        game->camera_pitch,
        game->camera_roll,
        game->camera_fov,
        model,
        bones,
        NULL,
        NULL);

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        pixel_buffer,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        32,
        SCREEN_WIDTH * sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);

    // Copy the pixels into the texture
    int* pix_write = NULL;
    int _pitch_unused = 0;
    if( SDL_LockTexture(texture, NULL, (void**)&pix_write, &_pitch_unused) < 0 )
        return;

    int row_size = SCREEN_WIDTH * sizeof(int);
    int* src_pixels = (int*)surface->pixels;
    for( int src_y = 0; src_y < (SCREEN_HEIGHT); src_y++ )
    {
        // Calculate offset in texture to write a single row of pixels
        int* row = &pix_write[(src_y * SCREEN_WIDTH)];
        // Copy a single row of pixels
        memcpy(row, &src_pixels[(src_y - 0) * SCREEN_WIDTH], row_size);
    }

    // Unlock the texture so that it may be used elsewhere
    SDL_UnlockTexture(texture);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    model_free(model);
    modelbones_free(bones);
}

static bool
platform_sdl2_init(struct PlatformSDL2* platform)
{
    int init = SDL_INIT_VIDEO;

    int res = SDL_Init(init);
    if( res < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_Window* window = SDL_CreateWindow(
        "Oldschool Runescape",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_ALLOW_HIGHDPI);
    platform->window = window;

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    platform->renderer = renderer;

    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Create texture
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);
    platform->texture = texture;

    platform->pixel_buffer = (int*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    memset(platform->pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    return true;
}

int
main(int argc, char* argv[])
{
    init_cos_table();
    init_sin_table();
    init_tan_table();
    init_hsl16_to_rgb_table();

    struct GameInput input = { 0 };
    struct Game game = { 0 };
    struct PlatformSDL2 platform = { 0 };

    if( !platform_sdl2_init(&platform) )
        return 1;

    if( !game_init(&game) )
        return 2;

    const int TARGET_FRAME_TIME = 1000 / 30; // 30 FPS = 33.33ms per frame
    Uint32 last_frame_time = SDL_GetTicks();

    while( !input.quit )
    {
        Uint32 current_time = SDL_GetTicks();
        Uint32 elapsed = current_time - last_frame_time;

        // If we're running too fast, wait until we hit our target frame time
        if( elapsed < TARGET_FRAME_TIME )
            SDL_Delay(TARGET_FRAME_TIME - elapsed);

        last_frame_time = SDL_GetTicks();

        game_input_sdl2(&input);
        if( input.quit )
            break;

        game_update(&game, &input);

        game_render_sdl2(&game, &platform);
    }

    return 0;
}