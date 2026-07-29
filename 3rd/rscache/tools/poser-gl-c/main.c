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
#include <stdint.h>
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
        "  --npc ID     select this entity at startup\n"
        "  --seq ID     select this animation at startup\n"
        "  --nodes      start with the skeleton shown\n"
        "  --export-pgl PATH write the selected animation and exit\n"
        "  --import-pgl PATH load a .pgl at startup\n"
        "  --pack DIR        pack the selected animation into a copy of the cache and exit\n"
        "  --sim-click X,Y   click once at this window point (harness)\n"
        "  --self-test       drive the editing commands and exit\n"
        "  --shot PATH  render, write a BMP of the window and exit\n"
        "  --shot-frames N   frames to run before --shot (default 60)\n"
        "\n"
        "Controls\n"
        "  left drag        pan, or drag a gizmo axis / joint\n"
        "  middle/right     orbit\n"
        "  wheel            zoom\n"
        "  space            play / pause\n"
        "  left / right     step one keyframe\n"
        "  cmd+z, cmd+shift+z   undo / redo\n"
        "  cmd+e            repeat the last export\n");
}

/**
 * A 24-bit BMP from top-down RGB rows. Shared by the font specimen and the
 * screenshot harness, which are the two ways this tool can be checked without a
 * person looking at the window.
 */
static int
write_bmp(const char* path, const unsigned char* rgb, int width, int height)
{
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
            const unsigned char* pixel = &rgb[(y * width + x) * 3];
            row[x * 3 + 0] = pixel[2]; /* BMP stores BGR */
            row[x * 3 + 1] = pixel[1];
            row[x * 3 + 2] = pixel[0];
        }
        fwrite(row, 1, (size_t)row_bytes, file);
    }
    free(row);
    fclose(file);
    fprintf(stderr, "poser-gl: wrote %s (%dx%d)\n", path, width, height);
    return 0;
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
    unsigned char* rgb = malloc((size_t)width * (size_t)height * 3);
    int result;

    if( !rgb )
        return 1;
    for( int y = 0; y < height; y++ )
    {
        for( int x = 0; x < width; x++ )
        {
            unsigned char value = atlas[(y / scale) * PG_FONT_ATLAS_W + (x / scale)];
            rgb[(y * width + x) * 3 + 0] = value;
            rgb[(y * width + x) * 3 + 1] = value;
            rgb[(y * width + x) * 3 + 2] = value;
        }
    }
    result = write_bmp(path, rgb, width, height);
    free(rgb);
    return result;
}

/** Read the default framebuffer back and write it out, flipping to top-down. */
static void
write_screenshot(const char* path, int width, int height)
{
    unsigned char* pixels = malloc((size_t)width * (size_t)height * 3);
    unsigned char* flipped = malloc((size_t)width * (size_t)height * 3);

    if( !pixels || !flipped )
    {
        free(pixels);
        free(flipped);
        return;
    }
    pg_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pg_glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    for( int y = 0; y < height; y++ )
        memcpy(
            &flipped[(size_t)y * (size_t)width * 3],
            &pixels[(size_t)(height - 1 - y) * (size_t)width * 3],
            (size_t)width * 3);
    write_bmp(path, flipped, width, height);
    free(pixels);
    free(flipped);
}

/*
 * Drive the editing commands from code.
 *
 * Every one of them copies keyframes, reallocates transformation arrays and
 * rewrites the parent indices those arrays hold, which is where a port like this
 * breaks — and none of it is reachable without a mouse. Running the lot in order
 * and then undoing back to the start is the check that fits in a harness.
 */
static int
self_test(struct PG_App* app)
{
    struct PG_Animation* anim = pg_app_current_animation(app);
    int original_keyframes;
    int failures = 0;

    if( !anim )
    {
        fprintf(stderr, "poser-gl: self-test needs --seq\n");
        return 1;
    }
    original_keyframes = anim->keyframe_count;
    fprintf(stderr, "self-test: seq %d, %d keyframes\n", anim->id, original_keyframes);

    pg_app_add_keyframe(app);
    anim = pg_app_current_animation(app);
    if( anim->keyframe_count != original_keyframes + 1 )
    {
        fprintf(stderr, "self-test: add did not insert (%d)\n", anim->keyframe_count);
        failures++;
    }
    /* The first edit copies the animation rather than touching the cache one. */
    if( !anim->modified )
    {
        fprintf(stderr, "self-test: edited animation is not marked modified\n");
        failures++;
    }

    pg_app_copy_keyframe(app);
    pg_app_paste_keyframe(app);
    anim = pg_app_current_animation(app);
    if( anim->keyframe_count != original_keyframes + 2 )
    {
        fprintf(stderr, "self-test: paste did not insert (%d)\n", anim->keyframe_count);
        failures++;
    }

    pg_app_lerp_keyframe(app);
    anim = pg_app_current_animation(app);
    if( anim->keyframe_count != original_keyframes + 3 )
    {
        fprintf(stderr, "self-test: lerp did not insert (%d)\n", anim->keyframe_count);
        failures++;
    }

    pg_app_delete_keyframe(app);
    anim = pg_app_current_animation(app);
    if( anim->keyframe_count != original_keyframes + 2 )
    {
        fprintf(stderr, "self-test: delete did not remove (%d)\n", anim->keyframe_count);
        failures++;
    }

    /* A transformation edit, then check the value landed and undoes cleanly. */
    {
        struct PG_Keyframe* keyframe =
            &anim->keyframes[pg_animation_frame_index(anim, app->frame_counter)];
        struct PG_RigJoint* target = NULL;
        int before = 0;

        for( int i = 0; i < keyframe->skeleton.joint_count; i++ )
        {
            if( keyframe->skeleton.joints[i].kind == PG_RIG_ROTATE )
            {
                target = &keyframe->skeleton.joints[i];
                break;
            }
        }
        if( !target )
        {
            fprintf(stderr, "self-test: keyframe has no rotation to edit\n");
            failures++;
        }
        else
        {
            struct PG_Command command;
            int id = target->id;
            before = target->delta[0];
            memset(&command, 0, sizeof(command));
            command.kind = PG_CMD_TRANSFORM_NODE;
            command.transformation_id = id;
            command.coord_index = 0;
            command.value = before + 37;
            command.keyframe_index = -1;
            pg_app_execute(app, command);

            anim = pg_app_current_animation(app);
            keyframe = &anim->keyframes[pg_animation_frame_index(anim, app->frame_counter)];
            target = pg_rig_skeleton_find(&keyframe->skeleton, id);
            if( !target || target->delta[0] != before + 37 )
            {
                fprintf(stderr, "self-test: transform did not apply\n");
                failures++;
            }
            pg_app_undo(app);
            anim = pg_app_current_animation(app);
            keyframe = &anim->keyframes[pg_animation_frame_index(anim, app->frame_counter)];
            target = pg_rig_skeleton_find(&keyframe->skeleton, id);
            if( !target || target->delta[0] != before )
            {
                fprintf(stderr, "self-test: transform did not undo\n");
                failures++;
            }
        }
    }

    /* Undo everything, then redo it, and check the count comes back both ways. */
    for( int i = 0; i < 8; i++ )
        pg_app_undo(app);
    anim = pg_app_current_animation(app);
    if( anim->keyframe_count != original_keyframes )
    {
        fprintf(
            stderr,
            "self-test: undo did not restore the keyframe count (%d, wanted %d)\n",
            anim->keyframe_count,
            original_keyframes);
        failures++;
    }
    for( int i = 0; i < 8; i++ )
        pg_app_redo(app);
    anim = pg_app_current_animation(app);
    if( anim->keyframe_count != original_keyframes + 2 )
    {
        fprintf(
            stderr,
            "self-test: redo did not restore the keyframe count (%d, wanted %d)\n",
            anim->keyframe_count,
            original_keyframes + 2);
        failures++;
    }

    fprintf(stderr, "self-test: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
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
    const char* shot_path = NULL;
    int shot_frames = 60;
    int frame_index = 0;
    int start_npc = INT32_MIN;
    int start_seq = -1;
    bool start_nodes = false;
    /* A scripted click, for checking picking and the gizmos without a person at
     * the mouse. Fires once, a third of the way into a --shot run. */
    float sim_click_x = -1.0f;
    float sim_click_y = -1.0f;
    int sim_click_frame = -1;
    const char* export_pgl = NULL;
    const char* import_pgl = NULL;
    const char* pack_dir = NULL;
    bool run_self_test = false;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--npc") == 0 && i + 1 < argc )
            start_npc = atoi(argv[++i]);
        else if( strcmp(argv[i], "--seq") == 0 && i + 1 < argc )
            start_seq = atoi(argv[++i]);
        else if( strcmp(argv[i], "--nodes") == 0 )
            start_nodes = true;
        else if( strcmp(argv[i], "--export-pgl") == 0 && i + 1 < argc )
            export_pgl = argv[++i];
        else if( strcmp(argv[i], "--import-pgl") == 0 && i + 1 < argc )
            import_pgl = argv[++i];
        else if( strcmp(argv[i], "--pack") == 0 && i + 1 < argc )
            pack_dir = argv[++i];
        else if( strcmp(argv[i], "--self-test") == 0 )
            run_self_test = true;
        else if( strcmp(argv[i], "--sim-click") == 0 && i + 1 < argc )
        {
            if( sscanf(argv[++i], "%f,%f", &sim_click_x, &sim_click_y) != 2 )
            {
                fprintf(stderr, "poser-gl: --sim-click wants X,Y\n");
                return 1;
            }
        }
        else if( strcmp(argv[i], "--shot") == 0 && i + 1 < argc )
            shot_path = argv[++i];
        else if( strcmp(argv[i], "--shot-frames") == 0 && i + 1 < argc )
            shot_frames = atoi(argv[++i]);
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

    if( start_npc != INT32_MIN )
    {
        const struct PG_NpcDef* npc = pg_cache_npc(app.cache, start_npc);
        if( npc )
            pg_app_load_npc(&app, npc);
        else
            fprintf(stderr, "poser-gl: npc %d absent\n", start_npc);
    }
    if( start_nodes )
        app.nodes_enabled = true;
    if( start_seq >= 0 )
    {
        int index = -1;
        for( int i = 0; i < app.animation_count; i++ )
            if( app.animations[i]->id == start_seq )
                index = i;
        if( index >= 0 )
            pg_app_select_animation(&app, index);
        else
            fprintf(stderr, "poser-gl: sequence %d absent\n", start_seq);
    }

    if( import_pgl )
        pg_import_pgl(&app, import_pgl);
    if( run_self_test )
    {
        int result = self_test(&app);
        pg_app_free(&app);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result;
    }
    if( pack_dir )
    {
        bool ok = pg_pack_animation(&app, pack_dir);
        fprintf(stderr, "poser-gl: %s\n", ok ? app.status : app.dialog_message);
        pg_app_free(&app);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ok ? 0 : 1;
    }
    if( export_pgl )
    {
        bool ok = pg_export_pgl(&app, export_pgl);
        fprintf(stderr, "poser-gl: %s\n", app.status);
        pg_app_free(&app);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ok ? 0 : 1;
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
                else if( key == SDLK_e && modifier )
                    pg_export_redo(&app);
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
        /* On a retina display the drawable is twice the window. Everything the
         * editor lays out is in window points; this is the factor that turns a
         * point rectangle into the device pixels glViewport and glScissor want. */
        dpi_scale = window_w > 0 ? (float)drawable_w / (float)window_w : 1.0f;

        {
            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            /* SDL reports the cursor in points, which is the space the UI is laid
             * out in — so it is used as-is. */
            input.mouse_x = (float)mouse_x;
            input.mouse_y = (float)mouse_y;
            input.mouse_dx = input.mouse_x - last_mouse_x;
            input.mouse_dy = input.mouse_y - last_mouse_y;
            last_mouse_x = input.mouse_x;
            last_mouse_y = input.mouse_y;
        }

        if( sim_click_x >= 0.0f && frame_index >= shot_frames / 3 && sim_click_frame < 2 )
        {
            /* Press on the first of the two frames and release on the second, so
             * the button does not stay stuck down and drag the camera for the
             * rest of the run. */
            input.mouse_x = sim_click_x;
            input.mouse_y = sim_click_y;
            input.mouse_dx = 0.0f;
            input.mouse_dy = 0.0f;
            last_mouse_x = sim_click_x;
            last_mouse_y = sim_click_y;
            if( sim_click_frame < 0 )
            {
                input.pressed[PG_MOUSE_LEFT] = true;
                input.down[PG_MOUSE_LEFT] = true;
                sim_click_frame = 0;
            }
            else
            {
                input.released[PG_MOUSE_LEFT] = true;
                input.down[PG_MOUSE_LEFT] = false;
                sim_click_frame = 2;
            }
        }

        pg_app_frame(&app, &input, window_w, window_h, dpi_scale);

        /* Before the swap: on a double-buffered context the back buffer is what
         * was just drawn, and after swapping it holds the previous frame. */
        if( shot_path && ++frame_index >= shot_frames )
        {
            write_screenshot(shot_path, drawable_w, drawable_h);
            app.running = false;
        }
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
