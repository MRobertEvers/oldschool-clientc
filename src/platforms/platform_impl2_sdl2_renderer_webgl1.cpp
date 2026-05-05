#ifdef __EMSCRIPTEN__

#include "platforms/platform_impl2_sdl2_renderer_webgl1.h"

#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"

#define TORIRS_NK_SDL_GLES2_IMPLEMENTATION
#include "nuklear/backends/sdl_opengles2/nuklear_torirs_sdl_gles2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "graphics/dash.h"
#include "osrs/game.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

/* ────────────────────────────────────────────────────────────────────────
 * Helper: create the tiny 2D-compositing GL program.
 * Draws a fullscreen textured quad with straight RGBA alpha-blend.
 * ──────────────────────────────────────────────────────────────────────── */
static GLuint
webgl1_ui2d_create_program(GLint* out_a_pos, GLint* out_a_uv, GLint* out_u_tex)
{
    static const char* vs_src =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_uv = a_uv; }\n";
    static const char* fs_src =
        "precision mediump float;\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_tex;\n"
        "void main() { gl_FragColor = texture2D(u_tex, v_uv); }\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    *out_a_pos = glGetAttribLocation(prog, "a_pos");
    *out_a_uv  = glGetAttribLocation(prog, "a_uv");
    *out_u_tex = glGetUniformLocation(prog, "u_tex");
    return prog;
}

/* Convert ARGB int[] → RGBA uint8_t[] in-place (output buffer must be 4× input size). */
static void
argb_to_rgba(const int* src, int pixel_count, uint8_t* dst)
{
    for( int i = 0; i < pixel_count; ++i )
    {
        uint32_t p = (uint32_t)src[i];
        dst[i * 4 + 0] = (uint8_t)((p >> 16) & 0xffu); /* R */
        dst[i * 4 + 1] = (uint8_t)((p >> 8) & 0xffu);  /* G */
        dst[i * 4 + 2] = (uint8_t)(p & 0xffu);          /* B */
        dst[i * 4 + 3] = (uint8_t)((p >> 24) & 0xffu);  /* A */
    }
}

/* Allocate (or reallocate) the UI software pixel buffers if the size changed. */
static bool
webgl1_ui2d_ensure_buffers(Platform2_SDL2_Renderer_WebGL1* r, int w, int h)
{
    if( r->ui_buf_width == w && r->ui_buf_height == h && r->ui_pixel_buffer )
        return true;
    free(r->ui_pixel_buffer);
    free(r->ui_rgba_buffer);
    r->ui_pixel_buffer = (int*)malloc((size_t)(w * h) * sizeof(int));
    r->ui_rgba_buffer  = (uint8_t*)malloc((size_t)(w * h) * 4u);
    if( !r->ui_pixel_buffer || !r->ui_rgba_buffer )
    {
        free(r->ui_pixel_buffer);
        free(r->ui_rgba_buffer);
        r->ui_pixel_buffer = nullptr;
        r->ui_rgba_buffer  = nullptr;
        r->ui_buf_width    = 0;
        r->ui_buf_height   = 0;
        return false;
    }
    r->ui_buf_width  = w;
    r->ui_buf_height = h;
    /* Resize the GL texture to match. */
    if( r->ui2d_texture )
    {
        glBindTexture(GL_TEXTURE_2D, r->ui2d_texture);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    return true;
}

/* Composite the 2D UI pixel buffer over whatever is currently in the framebuffer. */
static void
webgl1_ui2d_composite(Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r->ui_pixel_buffer || !r->ui_rgba_buffer || !r->ui2d_prog || !r->ui2d_texture )
        return;

    int w = r->ui_buf_width;
    int h = r->ui_buf_height;

    /* Convert and upload. */
    argb_to_rgba(r->ui_pixel_buffer, w * h, r->ui_rgba_buffer);
    glBindTexture(GL_TEXTURE_2D, r->ui2d_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, r->ui_rgba_buffer);

    /* Draw fullscreen quad with alpha blending. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(r->ui2d_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->ui2d_texture);
    glUniform1i(r->ui2d_u_tex, 0);

    /* Fullscreen quad: two triangles covering NDC [-1,+1]. UV origin is top-left so flip V. */
    float verts[] = {
        /* pos.x  pos.y   uv.x  uv.y */
        -1.0f, +1.0f,   0.0f, 0.0f,
        -1.0f, -1.0f,   0.0f, 1.0f,
        +1.0f, +1.0f,   1.0f, 0.0f,
        +1.0f, -1.0f,   1.0f, 1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, r->ui2d_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray((GLuint)r->ui2d_a_pos);
    glEnableVertexAttribArray((GLuint)r->ui2d_a_uv);
    glVertexAttribPointer((GLuint)r->ui2d_a_pos, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glVertexAttribPointer((GLuint)r->ui2d_a_uv,  2, GL_FLOAT, GL_FALSE, 16, (void*)8);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray((GLuint)r->ui2d_a_pos);
    glDisableVertexAttribArray((GLuint)r->ui2d_a_uv);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

static struct nk_context* s_webgl1_nk = nullptr;
static Uint64 s_webgl1_ui_prev_perf = 0;

Platform2_SDL2_Renderer_WebGL1*
PlatformImpl2_SDL2_Renderer_WebGL1_New(
    int width,
    int height)
{
    Platform2_SDL2_Renderer_WebGL1* renderer = new Platform2_SDL2_Renderer_WebGL1();
    renderer->width = width;
    renderer->height = height;
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Free(Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer )
        return;
    if( renderer->platform && renderer->platform->window && renderer->gl_context )
    {
        if( SDL_GL_MakeCurrent(renderer->platform->window, renderer->gl_context) == 0 )
        {
            if( s_webgl1_nk )
            {
                torirs_nk_ui_clear_active();
                torirs_nk_gles2_shutdown();
                s_webgl1_nk = nullptr;
            }
            if( renderer->ui2d_texture )
                glDeleteTextures(1, &renderer->ui2d_texture);
            if( renderer->ui2d_vbo )
                glDeleteBuffers(1, &renderer->ui2d_vbo);
            if( renderer->ui2d_prog )
                glDeleteProgram(renderer->ui2d_prog);
        }
    }
    free(renderer->ui_pixel_buffer);
    free(renderer->ui_rgba_buffer);
    if( renderer->trspk )
        TRSPK_WebGL1_Shutdown(renderer->trspk);
    renderer->trspk = nullptr;
    if( renderer->gl_context )
        SDL_GL_DeleteContext(renderer->gl_context);
    renderer->gl_context = nullptr;
    delete renderer;
}

bool
PlatformImpl2_SDL2_Renderer_WebGL1_Init(
    Platform2_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;
    renderer->platform = platform;
    renderer->gl_context = SDL_GL_CreateContext(platform->window);
    if( !renderer->gl_context )
        return false;
    SDL_GL_MakeCurrent(platform->window, renderer->gl_context);
    SDL_GL_GetDrawableSize(platform->window, &renderer->width, &renderer->height);
    renderer->trspk =
        TRSPK_WebGL1_InitWithCurrentContext((uint32_t)renderer->width, (uint32_t)renderer->height);
    if( !renderer->trspk || !renderer->trspk->ready )
    {
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }

    s_webgl1_nk = torirs_nk_gles2_init(platform->window);
    if( !s_webgl1_nk )
    {
        fprintf(stderr, "WebGL1: Nuklear GLES2 init failed\n");
        TRSPK_WebGL1_Shutdown(renderer->trspk);
        renderer->trspk = nullptr;
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }
    {
        struct nk_font_atlas* atlas = nullptr;
        torirs_nk_gles2_font_stash_begin(&atlas);
        nk_font_atlas_add_default(atlas, 13.0f * platform->display_scale, nullptr);
        torirs_nk_gles2_font_stash_end();
    }
    torirs_nk_ui_set_active(s_webgl1_nk, torirs_nk_gles2_handle_event, torirs_nk_gles2_handle_grab);
    s_webgl1_ui_prev_perf = SDL_GetPerformanceCounter();

    /* Create GL resources for 2D UI compositing. */
    renderer->ui2d_prog = webgl1_ui2d_create_program(
        &renderer->ui2d_a_pos, &renderer->ui2d_a_uv, &renderer->ui2d_u_tex);
    glGenTextures(1, &renderer->ui2d_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->ui2d_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenBuffers(1, &renderer->ui2d_vbo);

    /* Allocate initial 2D pixel buffers at renderer size. */
    if( renderer->width > 0 && renderer->height > 0 )
        webgl1_ui2d_ensure_buffers(renderer, renderer->width, renderer->height);

    renderer->gl_ready = true;
    return true;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Render(
    Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->gl_ready || !renderer->trspk || !game || !render_command_buffer )
        return;
    SDL_GL_MakeCurrent(renderer->platform->window, renderer->gl_context);
    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(renderer->platform->window, &drawable_w, &drawable_h);
    if( drawable_w != renderer->width || drawable_h != renderer->height )
    {
        renderer->width = drawable_w;
        renderer->height = drawable_h;
        TRSPK_WebGL1_Resize(renderer->trspk, (uint32_t)drawable_w, (uint32_t)drawable_h);
    }
    int win_width = renderer->platform->game_screen_width;
    int win_height = renderer->platform->game_screen_height;
    if( win_width <= 0 || win_height <= 0 )
        SDL_GetWindowSize(renderer->platform->window, &win_width, &win_height);
    if( win_width > 0 && win_height > 0 )
        TRSPK_WebGL1_SetWindowSize(renderer->trspk, (uint32_t)win_width, (uint32_t)win_height);

    /* Ensure UI pixel buffers match the drawable size. */
    webgl1_ui2d_ensure_buffers(renderer, drawable_w, drawable_h);

    TRSPK_ResourceCache* cache = TRSPK_WebGL1_GetCache(renderer->trspk);
    TRSPK_Batch16* staging = TRSPK_WebGL1_GetBatchStaging(renderer->trspk);
    TRSPK_WebGL1EventContext events = {};
    events.renderer = renderer->trspk;
    events.cache = cache;
    events.staging = staging;
    events.current_batch_id = renderer->current_model_batch_id;
    events.batch_active = renderer->current_model_batch_active;
    if( win_width > 0 && win_height > 0 && game )
    {
        trspk_webgl1_compute_default_pass_logical(
            &events.default_pass_logical, win_width, win_height, game);
        events.has_default_pass_logical =
            events.default_pass_logical.width > 0 && events.default_pass_logical.height > 0;
    }
    else
    {
        events.has_default_pass_logical = false;
    }

    renderer->diag_frame_model_draw_cmds = 0u;
    renderer->diag_frame_pose_invalid_skips = 0u;
    renderer->diag_frame_submitted_model_draws = 0u;
    TRSPK_WebGL1_FrameBegin(renderer->trspk);
    Uint64 const frame_work_t0 = SDL_GetPerformanceCounter();
    LibToriRS_FrameBegin(game, render_command_buffer);

    struct ToriRSRenderCommand cmd = { 0 };
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
    {
        switch( cmd.kind )
        {
        case TORIRS_GFX_RES_MODEL_LOAD:
            trspk_webgl1_event_res_model_load(&events, &cmd);
            break;
        case TORIRS_GFX_RES_MODEL_UNLOAD:
            trspk_webgl1_event_res_model_unload(&events, &cmd);
            break;
        case TORIRS_GFX_RES_TEX_LOAD:
            trspk_webgl1_event_tex_load(&events, &cmd);
            break;
        case TORIRS_GFX_RES_FONT_LOAD:
        case TORIRS_GFX_RES_FONT_UNLOAD:
        case TORIRS_GFX_RES_SPRITE_LOAD:
        case TORIRS_GFX_RES_SPRITE_UNLOAD:
            /* Fonts and sprites are CPU-side pointers; nothing to upload to GPU. */
            break;
        case TORIRS_GFX_BATCH3D_BEGIN:
            trspk_webgl1_event_batch3d_begin(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_MODEL_ADD:
            trspk_webgl1_event_batch3d_model_add(&events, &cmd);
            break;
        case TORIRS_GFX_RES_ANIM_LOAD:
            if( cmd._animation_load.usage_hint == TORIRS_USAGE_SCENERY )
                trspk_webgl1_event_batch3d_anim_add(&events, &cmd);
            else
                trspk_webgl1_event_res_anim_load(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_ANIM_ADD:
            trspk_webgl1_event_batch3d_anim_add(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_END:
            trspk_webgl1_event_batch3d_end(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_CLEAR:
            trspk_webgl1_event_batch3d_clear(&events, &cmd);
            break;
        case TORIRS_GFX_DRAW_MODEL:
            trspk_webgl1_event_draw_model(
                &events, game, &cmd, renderer->facebuffer.indices, TRSPK_FACEBUFFER_INDEX_CAPACITY);
            break;
        case TORIRS_GFX_STATE_BEGIN_3D:
            trspk_webgl1_event_state_begin_3d(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_CLEAR_RECT:
            trspk_webgl1_event_state_clear_rect(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_END_3D:
            trspk_webgl1_event_state_end_3d(&events, game, (double)game->cycle);
            break;

        /* ── 2D UI commands ──────────────────────────────────────────── */
        case TORIRS_GFX_STATE_BEGIN_2D:
        {
            /* Clear the UI pixel buffer at the start of a 2D pass. */
            if( renderer->ui_pixel_buffer && renderer->ui_buf_width > 0 )
                memset(renderer->ui_pixel_buffer, 0,
                    (size_t)(renderer->ui_buf_width * renderer->ui_buf_height) * sizeof(int));
            renderer->ui_clip_l = 0;
            renderer->ui_clip_t = 0;
            renderer->ui_clip_r = renderer->ui_buf_width;
            renderer->ui_clip_b = renderer->ui_buf_height;
            renderer->ui_clip_stack_top = -1;
            /* Propagate clip bounds to iface_view_port so Dash 2D functions pick them up. */
            if( game->iface_view_port )
            {
                game->iface_view_port->clip_left   = renderer->ui_clip_l;
                game->iface_view_port->clip_top    = renderer->ui_clip_t;
                game->iface_view_port->clip_right  = renderer->ui_clip_r;
                game->iface_view_port->clip_bottom = renderer->ui_clip_b;
                game->iface_view_port->width       = renderer->ui_buf_width;
                game->iface_view_port->height      = renderer->ui_buf_height;
                game->iface_view_port->stride      = renderer->ui_buf_width;
            }
        }
        break;

        case TORIRS_GFX_STATE_END_2D:
            /* Upload the 2D pixel buffer and composite it over the 3D scene. */
            webgl1_ui2d_composite(renderer);
            break;

        case TORIRS_GFX_STATE_PUSH_CLIP:
        {
            if( !renderer->ui_pixel_buffer || renderer->ui_clip_stack_top + 1 >= WEBGL1_UI_CLIP_STACK )
                break;
            int x = cmd._push_clip.x;
            int y = cmd._push_clip.y;
            int w2 = cmd._push_clip.w;
            int h2 = cmd._push_clip.h;
            int xr = x + w2;
            int yb = y + h2;
            int i = ++renderer->ui_clip_stack_top;
            renderer->ui_clip_stack_l[i] = renderer->ui_clip_l;
            renderer->ui_clip_stack_t[i] = renderer->ui_clip_t;
            renderer->ui_clip_stack_r[i] = renderer->ui_clip_r;
            renderer->ui_clip_stack_b[i] = renderer->ui_clip_b;
            renderer->ui_clip_l = x  > renderer->ui_clip_l ? x  : renderer->ui_clip_l;
            renderer->ui_clip_t = y  > renderer->ui_clip_t ? y  : renderer->ui_clip_t;
            renderer->ui_clip_r = xr < renderer->ui_clip_r ? xr : renderer->ui_clip_r;
            renderer->ui_clip_b = yb < renderer->ui_clip_b ? yb : renderer->ui_clip_b;
            if( game->iface_view_port )
            {
                game->iface_view_port->clip_left   = renderer->ui_clip_l;
                game->iface_view_port->clip_top    = renderer->ui_clip_t;
                game->iface_view_port->clip_right  = renderer->ui_clip_r;
                game->iface_view_port->clip_bottom = renderer->ui_clip_b;
            }
        }
        break;

        case TORIRS_GFX_STATE_POP_CLIP:
        {
            if( renderer->ui_clip_stack_top < 0 )
                break;
            int i = renderer->ui_clip_stack_top--;
            renderer->ui_clip_l = renderer->ui_clip_stack_l[i];
            renderer->ui_clip_t = renderer->ui_clip_stack_t[i];
            renderer->ui_clip_r = renderer->ui_clip_stack_r[i];
            renderer->ui_clip_b = renderer->ui_clip_stack_b[i];
            if( game->iface_view_port )
            {
                game->iface_view_port->clip_left   = renderer->ui_clip_l;
                game->iface_view_port->clip_top    = renderer->ui_clip_t;
                game->iface_view_port->clip_right  = renderer->ui_clip_r;
                game->iface_view_port->clip_bottom = renderer->ui_clip_b;
            }
        }
        break;

        case TORIRS_GFX_DRAW_SPRITE:
        {
            struct DashSprite* sp = cmd._sprite_draw.sprite;
            if( !sp || !sp->pixels_argb || !renderer->ui_pixel_buffer )
                break;
            if( !game->sys_dash || !game->iface_view_port )
                break;
            int rot = cmd._sprite_draw.rotation_r2pi2048;
            int srw = cmd._sprite_draw.src_bb_w;
            int srh = cmd._sprite_draw.src_bb_h;
            if( srw <= 0 ) srw = sp->width;
            if( srh <= 0 ) srh = sp->height;

            /* Feature 5: per-sprite clip override. */
            int saved_l = 0, saved_t = 0, saved_r = 0, saved_b = 0;
            bool had_per_clip = false;
            if( cmd._sprite_draw.clip_w > 0 && game->iface_view_port )
            {
                struct DashViewPort* vp = game->iface_view_port;
                saved_l = vp->clip_left;  saved_t = vp->clip_top;
                saved_r = vp->clip_right; saved_b = vp->clip_bottom;
                had_per_clip = true;
                int cx  = cmd._sprite_draw.clip_x;
                int cy  = cmd._sprite_draw.clip_y;
                int cxr = cx + cmd._sprite_draw.clip_w;
                int cyb = cy + cmd._sprite_draw.clip_h;
                dash2d_set_bounds(vp,
                    cx  > saved_l ? cx  : saved_l,
                    cy  > saved_t ? cy  : saved_t,
                    cxr < saved_r ? cxr : saved_r,
                    cyb < saved_b ? cyb : saved_b);
            }

            /* Feature 4: masked sprite. */
            struct DashSprite* mask_sp = cmd._sprite_draw.mask_sprite;
            uint32_t* masked_pixels = nullptr;
            struct DashSprite masked_tmp = *sp;
            if( mask_sp && mask_sp->pixels_argb && cmd._sprite_draw.mask_element_id >= 0 )
            {
                int pw = sp->width, ph = sp->height;
                masked_pixels = (uint32_t*)malloc((size_t)(pw * ph) * sizeof(uint32_t));
                if( masked_pixels )
                {
                    int mw = mask_sp->width, mh = mask_sp->height;
                    for( int py = 0; py < ph; py++ )
                    {
                        for( int px = 0; px < pw; px++ )
                        {
                            uint32_t s = sp->pixels_argb[py * pw + px];
                            uint32_t ma = (px < mw && py < mh)
                                ? ((mask_sp->pixels_argb[py * mw + px] >> 24) & 0xff)
                                : 0xffu;
                            uint32_t oa = (s >> 24) & 0xff;
                            uint32_t na = (oa * ma) / 255u;
                            masked_pixels[py * pw + px] = (s & 0x00ffffffu) | (na << 24);
                        }
                    }
                    masked_tmp.pixels_argb = (uint32_t*)masked_pixels;
                    sp = &masked_tmp;
                }
            }

            if( cmd._sprite_draw.rotated )
            {
                dash2d_blit_rotated_ex(
                    (int*)sp->pixels_argb, sp->width,
                    cmd._sprite_draw.src_bb_x, cmd._sprite_draw.src_bb_y, srw, srh,
                    cmd._sprite_draw.src_anchor_x, cmd._sprite_draw.src_anchor_y,
                    renderer->ui_pixel_buffer, renderer->ui_buf_width, renderer->ui_buf_height,
                    cmd._sprite_draw.dst_bb_x, cmd._sprite_draw.dst_bb_y,
                    cmd._sprite_draw.dst_bb_w, cmd._sprite_draw.dst_bb_h,
                    cmd._sprite_draw.dst_anchor_x, cmd._sprite_draw.dst_anchor_y,
                    rot);
            }
            else if( cmd._sprite_draw.alpha_mode == 1 )
            {
                dash2d_blit_sprite_alpha(game->sys_dash, sp, game->iface_view_port,
                    cmd._sprite_draw.dst_bb_x, cmd._sprite_draw.dst_bb_y,
                    255, renderer->ui_pixel_buffer);
            }
            else
            {
                dash2d_blit_sprite(game->sys_dash, sp, game->iface_view_port,
                    cmd._sprite_draw.dst_bb_x, cmd._sprite_draw.dst_bb_y,
                    renderer->ui_pixel_buffer);
            }

            free(masked_pixels);
            if( had_per_clip )
            {
                dash2d_set_bounds(game->iface_view_port, saved_l, saved_t, saved_r, saved_b);
            }
        }
        break;

        case TORIRS_GFX_DRAW_FONT:
        {
            struct DashPixFont* f  = cmd._font_draw.font;
            const uint8_t*     txt = cmd._font_draw.text;
            if( !f || !txt || !renderer->ui_pixel_buffer )
                break;
            int cl = renderer->ui_clip_l, ct = renderer->ui_clip_t;
            int cr = renderer->ui_clip_r, cb = renderer->ui_clip_b;
            if( cl < cr && ct < cb )
            {
                dashfont_draw_text_ex_clipped(
                    f, (uint8_t*)txt,
                    cmd._font_draw.x, cmd._font_draw.y,
                    cmd._font_draw.color_rgb,
                    renderer->ui_pixel_buffer, renderer->ui_buf_width,
                    cl, ct, cr, cb);
            }
        }
        break;

        case TORIRS_GFX_DRAW_RECT:
        {
            if( !renderer->ui_pixel_buffer )
                break;
            int x = cmd._rect_draw.x;
            int y = cmd._rect_draw.y;
            int w2 = cmd._rect_draw.w;
            int h2 = cmd._rect_draw.h;
            if( w2 <= 0 || h2 <= 0 )
                break;
            int rgb   = cmd._rect_draw.color_rgb;
            int alpha = cmd._rect_draw.alpha;
            uint8_t fill = cmd._rect_draw.fill;
            /* Clip against current viewport. */
            int x0 = x < renderer->ui_clip_l ? renderer->ui_clip_l : x;
            int y0 = y < renderer->ui_clip_t ? renderer->ui_clip_t : y;
            int x1 = x + w2 > renderer->ui_clip_r ? renderer->ui_clip_r : x + w2;
            int y1 = y + h2 > renderer->ui_clip_b ? renderer->ui_clip_b : y + h2;
            if( x0 >= x1 || y0 >= y1 )
                break;
            int* pb = renderer->ui_pixel_buffer;
            int stride = renderer->ui_buf_width;
            if( alpha < 255 && alpha > 0 )
            {
                dash2d_fill_rect_alpha(pb, stride, x0, y0, x1 - x0, y1 - y0, rgb, alpha);
            }
            else if( fill )
            {
                dash2d_fill_rect(pb, stride, x0, y0, x1 - x0, y1 - y0, rgb);
            }
            else
            {
                dash2d_draw_rect(pb, stride, x0, y0, x1 - x0, y1 - y0, rgb);
            }
        }
        break;

        default:
            break;
        }
    }
    renderer->current_model_batch_id = events.current_batch_id;
    renderer->current_model_batch_active = events.batch_active;
    renderer->diag_frame_model_draw_cmds = events.diag_frame_model_draw_cmds;
    renderer->diag_frame_pose_invalid_skips = events.diag_frame_pose_invalid_skips;
    renderer->diag_frame_submitted_model_draws = events.diag_frame_submitted_model_draws;
    TRSPK_WebGL1_FrameEnd(renderer->trspk);

    if( s_webgl1_nk )
    {
        double const frame_work_avg_ms = torirs_nk_ui_frame_work_avg_ms();
        double const dt = torirs_nk_ui_frame_delta_sec(&s_webgl1_ui_prev_perf);
        TorirsNkDebugPanelParams params = {};
        params.window_title = "Info";
        params.delta_time_sec = dt;
        params.view_w_cap = drawable_w;
        params.view_h_cap = drawable_h;
        params.sdl_window = renderer->platform->window;
        params.soft3d = nullptr;
        params.include_soft3d_extras = 0;
        params.include_gpu_frame_stats = 1;
        params.gpu_model_draws = renderer->diag_frame_model_draw_cmds;
        params.gpu_tris = 0u;
        params.gpu_submitted_model_draws = renderer->diag_frame_submitted_model_draws;
        params.gpu_pose_invalid_skips = renderer->diag_frame_pose_invalid_skips;
        params.gpu_dynamic_index_draws = 0u;
        params.gpu_gl_index_draw_calls = renderer->trspk->diag_frame_submitted_draws;
        params.gpu_gl_pass_subdraws = renderer->trspk->diag_frame_pass_subdraws;
        params.gpu_gl_merge_brk_chunk = renderer->trspk->diag_frame_merge_break_chunk;
        params.gpu_gl_merge_brk_vbo = renderer->trspk->diag_frame_merge_break_vbo;
        params.gpu_gl_merge_brk_pool = renderer->trspk->diag_frame_merge_break_pool_gap;
        params.gpu_gl_merge_brk_invalid = renderer->trspk->diag_frame_merge_break_next_invalid;
        params.gpu_gl_merge_outer_skips = renderer->trspk->diag_frame_merge_outer_skips;
        if( frame_work_avg_ms >= 0.0 )
        {
            params.include_frame_work_timing = 1;
            params.frame_work_avg_ms = frame_work_avg_ms;
        }
        torirs_nk_debug_panel_draw(s_webgl1_nk, game, &params);
        torirs_nk_ui_after_frame(s_webgl1_nk);
        torirs_nk_gles2_render(NK_ANTI_ALIASING_ON, 512 * 1024, 128 * 1024);
        Uint64 const frame_work_t1 = SDL_GetPerformanceCounter();
        Uint64 const frame_work_freq = SDL_GetPerformanceFrequency();
        double const frame_work_sec =
            frame_work_freq ? (double)(frame_work_t1 - frame_work_t0) / (double)frame_work_freq
                            : 0.0;
        torirs_nk_ui_frame_work_push_sec(frame_work_sec);
    }

    SDL_GL_SwapWindow(renderer->platform->window);
    LibToriRS_FrameEnd(game);
}

#endif
