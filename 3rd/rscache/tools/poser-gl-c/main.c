/*
 * poser-gl-c — an OpenGL animation editor for RuneScape caches.
 *
 * A C port of fglass/poser-gl, reading caches through 3rd/rscache instead of a
 * per-revision JAR plugin, and drawing through SDL2 instead of LWJGL + LEGUI.
 *
 *   poser-gl --rev osrs230 cache.osrs230
 *
 * See README.md for what carried over and what did not.
 */

#include "pg_app.h"
#include "pg_font.h"
#include "pg_gl.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PG_WIDTH 1280
#define PG_HEIGHT 800
/* poser-gl runs its loop at 50 Hz, and the animation timer counts one client
 * cycle per frame — so the playback speed is tied to this number, not just the
 * smoothness. */
#define PG_TARGET_FPS 50

static void
usage(void)
{
    fprintf(
        stderr,
        "poser-gl-c — animation editor\n"
        "\n"
        "  poser-gl [--rev NAME] CACHE_DIR\n"
        "  poser-gl --font-specimen OUT.bmp\n"
        "\n"
        "  --rev NAME   rscache revision profile (default osrs230)\n"
        "\n"
        "Controls\n"
        "  left drag        pan, or drag a gizmo axis / joint\n"
        "  middle/right     orbit\n"
        "  wheel            zoom\n"
        "  space            play / pause\n"
        "  left / right     step one keyframe\n"
        "  cmd+z, cmd+shift+z   undo / redo\n");
}

/* A specimen sheet of the built-in font, so the glyph art can be checked by eye
 * without a GL context. */
static int
write_font_specimen(const char* path)
{
    const unsigned char* atlas = pg_font_atlas_pixels();
    const int scale = 4;
    const int width = PG_FONT_ATLAS_W * scale;
    const int height = PG_FONT_ATLAS_H * scale;
    const int row_bytes = (width * 3 + 3) & ~3;
    const int image_size = row_bytes * height;
    unsigned char header[54];
    FILE* file = fopen(path, "wb");
    unsigned char* row;

    if( !file )
    {
        fprintf(stderr, "poser-gl: cannot write %s\n", path);
        return 1;
    }
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    *(int*)&header[2] = 54 + image_size;
    *(int*)&header[10] = 54;
    *(int*)&header[14] = 40;
    *(int*)&header[18] = width;
    *(int*)&header[22] = height;
    header[26] = 1;
    header[28] = 24;
    *(int*)&header[34] = image_size;
    fwrite(header, 1, sizeof(header), file);

    row = calloc((size_t)row_bytes, 1);
    for( int y = height - 1; y >= 0; y-- ) /* BMP rows run bottom-up */
    {
        for( int x = 0; x < width; x++ )
        {
            unsigned char value = atlas[(y / scale) * PG_FONT_ATLAS_W + (x / scale)];
            row[x * 3 + 0] = value;
            row[x * 3 + 1] = value;
            row[x * 3 + 2] = value;
        }
        fwrite(row, 1, (size_t)row_bytes, file);
    }
    free(row);
    fclose(file);
    fprintf(stderr, "poser-gl: wrote %s (%dx%d)\n", path, width, height);
    return 0;
}

int
main(int argc, char** argv)
{
    const char* cache_dir = NULL;
    const char* rev = "osrs230";
    SDL_Window* window;
    SDL_GLContext context;
    struct PG_App app;
    struct PG_GuiInput input;
    Uint32 previous_ticks;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--font-specimen") == 0 && i + 1 < argc )
            return write_font_specimen(argv[++i]);
        else if( strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 )
        {
            usage();
            return 0;
        }
        else if( argv[i][0] == '-' )
        {
            fprintf(stderr, "poser-gl: unknown option %s\n", argv[i]);
            usage();
            return 1;
        }
        else
        {
            cache_dir = argv[i];
        }
    }
    if( !cache_dir )
    {
        usage();
        return 1;
    }

    if( SDL_Init(SDL_INIT_VIDEO) != 0 )
    {
        fprintf(stderr, "poser-gl: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    window = SDL_CreateWindow(
        "poser-gl-c",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        PG_WIDTH,
        PG_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if( !window )
    {
        fprintf(stderr, "poser-gl: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 1024, 700);

    context = SDL_GL_CreateContext(window);
    if( !context )
    {
        fprintf(stderr, "poser-gl: no OpenGL 3.3 core context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    if( pg_gl_load() != 0 )
    {
        fprintf(stderr, "poser-gl: incomplete OpenGL 3.3 core\n");
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    fprintf(
        stderr,
        "poser-gl: GL %s on %s\n",
        (const char*)pg_glGetString(GL_VERSION),
        (const char*)pg_glGetString(GL_RENDERER));
    pg_glEnable(GL_PROGRAM_POINT_SIZE);

    if( pg_app_init(&app, cache_dir, rev) != 0 )
    {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_StartTextInput();
    memset(&input, 0, sizeof(input));
    previous_ticks = SDL_GetTicks();

    while( app.running )
    {
        SDL_Event event;
        int drawable_w = 0;
        int drawable_h = 0;
        int window_w = 0;
        int window_h = 0;
        float dpi_scale;
        Uint32 now;

        input.pressed[0] = input.pressed[1] = input.pressed[2] = false;
        input.released[0] = input.released[1] = input.released[2] = false;
        input.wheel = 0.0f;
        input.text[0] = '\0';
        input.backspace = false;
        input.enter = false;

        while( SDL_PollEvent(&event) )
        {
            switch( event.type )
            {
            case SDL_QUIT:
                app.running = false;
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                int button = -1;
                if( event.button.button == SDL_BUTTON_LEFT )
                    button = PG_MOUSE_LEFT;
                else if( event.button.button == SDL_BUTTON_MIDDLE )
                    button = PG_MOUSE_MIDDLE;
                else if( event.button.button == SDL_BUTTON_RIGHT )
                    button = PG_MOUSE_RIGHT;
                if( button >= 0 )
                {
                    bool down = event.type == SDL_MOUSEBUTTONDOWN;
                    if( down && !input.down[button] )
                        input.pressed[button] = true;
                    if( !down && input.down[button] )
                        input.released[button] = true;
                    input.down[button] = down;
                }
                break;
            }
            case SDL_MOUSEWHEEL:
                input.wheel += (float)event.wheel.y;
                break;
            case SDL_TEXTINPUT:
                snprintf(input.text, sizeof(input.text), "%s", event.text.text);
                break;
            case SDL_KEYDOWN:
            {
                SDL_Keycode key = event.key.keysym.sym;
                /* Cmd on macOS, Ctrl elsewhere — the same split the reference's
                 * key handler makes. */
                bool modifier = (event.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) != 0;
                bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
                bool typing = app.gui.focus != 0;

                if( key == SDLK_BACKSPACE )
                    input.backspace = true;
                else if( key == SDLK_RETURN || key == SDLK_KP_ENTER )
                    input.enter = true;

                if( typing && !modifier )
                    break;

                if( key == SDLK_z && modifier && shift )
                    pg_app_redo(&app);
                else if( key == SDLK_z && modifier )
                    pg_app_undo(&app);
                else if( key == SDLK_SPACE )
                    pg_app_set_playing(&app, !app.playing);
                else if( key == SDLK_RIGHT )
                    pg_app_next_frame(&app);
                else if( key == SDLK_LEFT )
                    pg_app_previous_frame(&app);
                else if( key == SDLK_ESCAPE )
                    app.dialog = PG_DIALOG_NONE;
                break;
            }
            default:
                break;
            }
        }

        SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
        SDL_GetWindowSize(window, &window_w, &window_h);
        /* On a retina display the drawable is twice the window, and every UI
         * rectangle is authored in window points. Scaling the cursor by the same
         * factor keeps hit tests where they are drawn. */
        dpi_scale = window_w > 0 ? (float)drawable_w / (float)window_w : 1.0f;

        {
            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            input.mouse_x = (float)mouse_x * dpi_scale;
            input.mouse_y = (float)mouse_y * dpi_scale;
            input.mouse_dx = input.mouse_x - last_mouse_x;
            input.mouse_dy = input.mouse_y - last_mouse_y;
            last_mouse_x = input.mouse_x;
            last_mouse_y = input.mouse_y;
        }

        pg_app_frame(&app, &input, drawable_w, drawable_h);
        SDL_GL_SwapWindow(window);

        now = SDL_GetTicks();
        {
            Uint32 elapsed = now - previous_ticks;
            Uint32 target = 1000 / PG_TARGET_FPS;
            if( elapsed < target )
                SDL_Delay(target - elapsed);
        }
        previous_ticks = SDL_GetTicks();
    }

    pg_settings_save(&app.settings);
    pg_app_free(&app);
    SDL_StopTextInput();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
