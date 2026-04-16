/*
 * Nuklear - 4.9.4 - public domain
 */
/*
 * ==============================================================
 *
 *                              API
 *
 * ===============================================================
 */
#ifndef TORIRS_NK_SDL_RENDERER_H_
#define TORIRS_NK_SDL_RENDERER_H_

#ifndef NK_SDL_RENDERER_SDL_H
#define NK_SDL_RENDERER_SDL_H <SDL.h>
#endif
#include NK_SDL_RENDERER_SDL_H
NK_API struct nk_context*   torirs_nk_sdlren_init(SDL_Window *win, SDL_Renderer *renderer);
NK_API void                 torirs_nk_sdlren_font_stash_begin(struct nk_font_atlas **atlas);
NK_API void                 torirs_nk_sdlren_font_stash_end(void);
NK_API int                  torirs_nk_sdlren_handle_event(SDL_Event *evt);
NK_API void                 torirs_nk_sdlren_render(enum nk_anti_aliasing);
NK_API void                 torirs_nk_sdlren_shutdown(void);
NK_API void                 torirs_nk_sdlren_handle_grab(void);

#if SDL_COMPILEDVERSION < SDL_VERSIONNUM(2, 0, 22)
/* Metal API does not support cliprects with negative coordinates or large
 * dimensions. The issue is fixed in SDL2 with version 2.0.22 but until
 * that version is released, the NK_SDL_CLAMP_CLIP_RECT flag can be used to
 * ensure the cliprect is itself clipped to the viewport.
 * See discussion at https://discourse.libsdl.org/t/rendergeometryraw-producing-different-results-in-metal-vs-opengl/34953
 */
#define NK_SDL_CLAMP_CLIP_RECT
#endif

#endif /* TORIRS_NK_SDL_RENDERER_H_ */

/*
 * ==============================================================
 *
 *                          IMPLEMENTATION
 *
 * ===============================================================
 */
#ifdef TORIRS_NK_SDL_RENDERER_IMPLEMENTATION
#include <string.h>
#include <stdlib.h>

struct torirs_nk_sdlren_device {
    struct nk_buffer cmds;
    struct nk_draw_null_texture tex_null;
    SDL_Texture *font_tex;
};

struct torirs_nk_sdlren_vertex {
    float position[2];
    float uv[2];
    nk_byte col[4];
};

static struct torirs_nk_sdlren_bundle {
    SDL_Window *win;
    SDL_Renderer *renderer;
    struct torirs_nk_sdlren_device ogl;
    struct nk_context ctx;
    struct nk_font_atlas atlas;
    Uint64 time_of_last_frame;
} torirs_nk_sdlren__st;

NK_INTERN void
torirs_nk_sdlren_device_upload_atlas(const void *image, int width, int height)
{
    struct torirs_nk_sdlren_device *dev = &torirs_nk_sdlren__st.ogl;

    SDL_Texture *g_SDLFontTexture = SDL_CreateTexture(torirs_nk_sdlren__st.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (g_SDLFontTexture == NULL) {
        SDL_Log("error creating texture");
        return;
    }
    SDL_UpdateTexture(g_SDLFontTexture, NULL, image, 4 * width);
    SDL_SetTextureBlendMode(g_SDLFontTexture, SDL_BLENDMODE_BLEND);
    dev->font_tex = g_SDLFontTexture;
}

NK_API void
torirs_nk_sdlren_render(enum nk_anti_aliasing AA)
{
    /* setup global state */
    struct torirs_nk_sdlren_device *dev = &torirs_nk_sdlren__st.ogl;

    {
        SDL_Rect saved_clip;
#ifdef NK_SDL_CLAMP_CLIP_RECT
        SDL_Rect viewport;
#endif
        SDL_bool clipping_enabled;
        int vs = sizeof(struct torirs_nk_sdlren_vertex);
        size_t vp = offsetof(struct torirs_nk_sdlren_vertex, position);
        size_t vt = offsetof(struct torirs_nk_sdlren_vertex, uv);
        size_t vc = offsetof(struct torirs_nk_sdlren_vertex, col);

        /* convert from command queue into draw list and draw to screen */
        const struct nk_draw_command *cmd;
        const nk_draw_index *offset = NULL;
        struct nk_buffer vbuf, ebuf;

        /* fill converting configuration */
        struct nk_convert_config config;
        static const struct nk_draw_vertex_layout_element vertex_layout[] = {
            {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct torirs_nk_sdlren_vertex, position)},
            {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct torirs_nk_sdlren_vertex, uv)},
            {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct torirs_nk_sdlren_vertex, col)},
            {NK_VERTEX_LAYOUT_END}
        };

        Uint64 now = SDL_GetTicks64();
        torirs_nk_sdlren__st.ctx.delta_time_seconds = (float)(now - torirs_nk_sdlren__st.time_of_last_frame) / 1000;
        torirs_nk_sdlren__st.time_of_last_frame = now;

        NK_MEMSET(&config, 0, sizeof(config));
        config.vertex_layout = vertex_layout;
        config.vertex_size = sizeof(struct torirs_nk_sdlren_vertex);
        config.vertex_alignment = NK_ALIGNOF(struct torirs_nk_sdlren_vertex);
        config.tex_null = dev->tex_null;
        config.circle_segment_count = 22;
        config.curve_segment_count = 22;
        config.arc_segment_count = 22;
        config.global_alpha = 1.0f;
        config.shape_AA = AA;
        config.line_AA = AA;

        /* convert shapes into vertexes */
        nk_buffer_init_default(&vbuf);
        nk_buffer_init_default(&ebuf);
        nk_convert(&torirs_nk_sdlren__st.ctx, &dev->cmds, &vbuf, &ebuf, &config);

        /* iterate over and execute each draw command */
        offset = (const nk_draw_index*)nk_buffer_memory_const(&ebuf);

        clipping_enabled = SDL_RenderIsClipEnabled(torirs_nk_sdlren__st.renderer);
        SDL_RenderGetClipRect(torirs_nk_sdlren__st.renderer, &saved_clip);
#ifdef NK_SDL_CLAMP_CLIP_RECT
        SDL_RenderGetViewport(torirs_nk_sdlren__st.renderer, &viewport);
#endif

        nk_draw_foreach(cmd, &torirs_nk_sdlren__st.ctx, &dev->cmds)
        {
            if (!cmd->elem_count) continue;

            {
                SDL_Rect r;
                r.x = cmd->clip_rect.x;
                r.y = cmd->clip_rect.y;
                r.w = cmd->clip_rect.w;
                r.h = cmd->clip_rect.h;
#ifdef NK_SDL_CLAMP_CLIP_RECT
                if (r.x < 0) {
                    r.w += r.x;
                    r.x = 0;
                }
                if (r.y < 0) {
                    r.h += r.y;
                    r.y = 0;
                }
                if (r.h > viewport.h) {
                    r.h = viewport.h;
                }
                if (r.w > viewport.w) {
                    r.w = viewport.w;
                }
#endif
                SDL_RenderSetClipRect(torirs_nk_sdlren__st.renderer, &r);
            }

            {
                const void *vertices = nk_buffer_memory_const(&vbuf);

                SDL_RenderGeometryRaw(torirs_nk_sdlren__st.renderer,
                        (SDL_Texture *)cmd->texture.ptr,
                        (const float*)((const nk_byte*)vertices + vp), vs,
                        (const SDL_Color*)((const nk_byte*)vertices + vc), vs,
                        (const float*)((const nk_byte*)vertices + vt), vs,
                        (vbuf.needed / vs),
                        (void *) offset, cmd->elem_count, 2);

                offset += cmd->elem_count;
            }
        }

        SDL_RenderSetClipRect(torirs_nk_sdlren__st.renderer, &saved_clip);
        if (!clipping_enabled) {
            SDL_RenderSetClipRect(torirs_nk_sdlren__st.renderer, NULL);
        }

        nk_clear(&torirs_nk_sdlren__st.ctx);
        nk_buffer_clear(&dev->cmds);
        nk_buffer_free(&vbuf);
        nk_buffer_free(&ebuf);
    }
}

static void
torirs_nk_sdlren_clipboard_paste(nk_handle usr, struct nk_text_edit *edit)
{
    const char *text = SDL_GetClipboardText();
    if (text) {
        nk_textedit_paste(edit, text, nk_strlen(text));
        SDL_free((void *)text);
    }
    (void)usr;
}

static void
torirs_nk_sdlren_clipboard_copy(nk_handle usr, const char *text, int len)
{
    char *str = 0;
    (void)usr;
    if (!len) return;
    str = (char*)malloc((size_t)len+1);
    if (!str) return;
    memcpy(str, text, (size_t)len);
    str[len] = '\0';
    SDL_SetClipboardText(str);
    free(str);
}

NK_API struct nk_context*
torirs_nk_sdlren_init(SDL_Window *win, SDL_Renderer *renderer)
{
#ifndef NK_SDL_CLAMP_CLIP_RECT
    SDL_RendererInfo info;
    SDL_version runtimeVer;

    /* warn for cases where NK_SDL_CLAMP_CLIP_RECT should have been set but isn't */
    SDL_GetRendererInfo(renderer, &info);
    SDL_GetVersion(&runtimeVer);
    if (strncmp("metal", info.name, 5) == 0 &&
        SDL_VERSIONNUM(runtimeVer.major, runtimeVer.minor, runtimeVer.patch) < SDL_VERSIONNUM(2, 0, 22))
    {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "renderer is using Metal API but runtime SDL version %d.%d.%d is older than compiled version %d.%d.%d, "
            "which may cause issues with rendering",
            runtimeVer.major, runtimeVer.minor, runtimeVer.patch,
            SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL
        );
    }
#endif
    torirs_nk_sdlren__st.win = win;
    torirs_nk_sdlren__st.renderer = renderer;
    torirs_nk_sdlren__st.time_of_last_frame = SDL_GetTicks64();
    nk_init_default(&torirs_nk_sdlren__st.ctx, 0);
    torirs_nk_sdlren__st.ctx.clip.copy = torirs_nk_sdlren_clipboard_copy;
    torirs_nk_sdlren__st.ctx.clip.paste = torirs_nk_sdlren_clipboard_paste;
    torirs_nk_sdlren__st.ctx.clip.userdata = nk_handle_ptr(0);
    nk_buffer_init_default(&torirs_nk_sdlren__st.ogl.cmds);
    return &torirs_nk_sdlren__st.ctx;
}

NK_API void
torirs_nk_sdlren_font_stash_begin(struct nk_font_atlas **atlas)
{
    nk_font_atlas_init_default(&torirs_nk_sdlren__st.atlas);
    nk_font_atlas_begin(&torirs_nk_sdlren__st.atlas);
    *atlas = &torirs_nk_sdlren__st.atlas;
}

NK_API void
torirs_nk_sdlren_font_stash_end(void)
{
    const void *image; int w, h;
    image = nk_font_atlas_bake(&torirs_nk_sdlren__st.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    torirs_nk_sdlren_device_upload_atlas(image, w, h);
    nk_font_atlas_end(&torirs_nk_sdlren__st.atlas, nk_handle_ptr(torirs_nk_sdlren__st.ogl.font_tex), &torirs_nk_sdlren__st.ogl.tex_null);
    {
        struct nk_font *use_font = torirs_nk_sdlren__st.atlas.default_font
            ? torirs_nk_sdlren__st.atlas.default_font
            : torirs_nk_sdlren__st.atlas.fonts;
        if (use_font)
            nk_style_set_font(&torirs_nk_sdlren__st.ctx, &use_font->handle);
    }
}

NK_API void
torirs_nk_sdlren_handle_grab(void)
{
    struct nk_context *ctx = &torirs_nk_sdlren__st.ctx;
    if (ctx->input.mouse.grab) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
    } else if (ctx->input.mouse.ungrab) {
        /* better support for older SDL by setting mode first; causes an extra mouse motion event */
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_WarpMouseInWindow(torirs_nk_sdlren__st.win, (int)ctx->input.mouse.prev.x, (int)ctx->input.mouse.prev.y);
    } else if (ctx->input.mouse.grabbed) {
        ctx->input.mouse.pos.x = ctx->input.mouse.prev.x;
        ctx->input.mouse.pos.y = ctx->input.mouse.prev.y;
    }
}

NK_API int
torirs_nk_sdlren_handle_event(SDL_Event *evt)
{
    struct nk_context *ctx = &torirs_nk_sdlren__st.ctx;
    int ctrl_down = SDL_GetModState() & KMOD_CTRL;
    static int insert_toggle = 0;

    switch(evt->type)
    {
        case SDL_KEYUP: /* KEYUP & KEYDOWN share same routine */
        case SDL_KEYDOWN:
            {
                int down = evt->type == SDL_KEYDOWN;
                switch(evt->key.keysym.sym)
                {
                    case SDLK_RSHIFT: /* RSHIFT & LSHIFT share same routine */
                    case SDLK_LSHIFT:    nk_input_key(ctx, NK_KEY_SHIFT, down); break;
                    case SDLK_DELETE:    nk_input_key(ctx, NK_KEY_DEL, down); break;

                    case SDLK_KP_ENTER:
                    case SDLK_RETURN:    nk_input_key(ctx, NK_KEY_ENTER, down); break;

                    case SDLK_TAB:       nk_input_key(ctx, NK_KEY_TAB, down); break;
                    case SDLK_BACKSPACE: nk_input_key(ctx, NK_KEY_BACKSPACE, down); break;
                    case SDLK_HOME:      nk_input_key(ctx, NK_KEY_TEXT_START, down);
                                         nk_input_key(ctx, NK_KEY_SCROLL_START, down); break;
                    case SDLK_END:       nk_input_key(ctx, NK_KEY_TEXT_END, down);
                                         nk_input_key(ctx, NK_KEY_SCROLL_END, down); break;
                    case SDLK_PAGEDOWN:  nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down); break;
                    case SDLK_PAGEUP:    nk_input_key(ctx, NK_KEY_SCROLL_UP, down); break;
                    case SDLK_z:         nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && ctrl_down); break;
                    case SDLK_r:         nk_input_key(ctx, NK_KEY_TEXT_REDO, down && ctrl_down); break;
                    case SDLK_c:         nk_input_key(ctx, NK_KEY_COPY, down && ctrl_down); break;
                    case SDLK_v:         nk_input_key(ctx, NK_KEY_PASTE, down && ctrl_down); break;
                    case SDLK_x:         nk_input_key(ctx, NK_KEY_CUT, down && ctrl_down); break;
                    case SDLK_b:         nk_input_key(ctx, NK_KEY_TEXT_LINE_START, down && ctrl_down); break;
                    case SDLK_e:         nk_input_key(ctx, NK_KEY_TEXT_LINE_END, down && ctrl_down); break;
                    case SDLK_UP:        nk_input_key(ctx, NK_KEY_UP, down); break;
                    case SDLK_DOWN:      nk_input_key(ctx, NK_KEY_DOWN, down); break;
                    case SDLK_ESCAPE:    nk_input_key(ctx, NK_KEY_TEXT_RESET_MODE, down); break;
                    case SDLK_INSERT:
                        if (down) insert_toggle = !insert_toggle;
                        if (insert_toggle) {
                            nk_input_key(ctx, NK_KEY_TEXT_INSERT_MODE, down);
                        } else {
                            nk_input_key(ctx, NK_KEY_TEXT_REPLACE_MODE, down);
                        }
                        break;
                    case SDLK_a:
                        if (ctrl_down)
                            nk_input_key(ctx,NK_KEY_TEXT_SELECT_ALL, down);
                        break;
                    case SDLK_LEFT:
                        if (ctrl_down)
                            nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
                        else nk_input_key(ctx, NK_KEY_LEFT, down);
                        break;
                    case SDLK_RIGHT:
                        if (ctrl_down)
                            nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
                        else nk_input_key(ctx, NK_KEY_RIGHT, down);
                        break;
                }
            }
            return 1;

        case SDL_MOUSEBUTTONUP: /* MOUSEBUTTONUP & MOUSEBUTTONDOWN share same routine */
        case SDL_MOUSEBUTTONDOWN:
            {
                int down = evt->type == SDL_MOUSEBUTTONDOWN;
                const int x = evt->button.x, y = evt->button.y;
                switch(evt->button.button)
                {
                    case SDL_BUTTON_LEFT:
                        if (evt->button.clicks > 1)
                            nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, down);
                        nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down); break;
                    case SDL_BUTTON_MIDDLE: nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down); break;
                    case SDL_BUTTON_RIGHT:  nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down); break;
                    case SDL_BUTTON_X1:     nk_input_button(ctx, NK_BUTTON_X1, x, y, down); break;
                    case SDL_BUTTON_X2:     nk_input_button(ctx, NK_BUTTON_X2, x, y, down); break;
                }
            }
            return 1;

        case SDL_MOUSEMOTION:
            if (ctx->input.mouse.grabbed) {
                int x = (int)ctx->input.mouse.prev.x, y = (int)ctx->input.mouse.prev.y;
                nk_input_motion(ctx, x + evt->motion.xrel, y + evt->motion.yrel);
            }
            else nk_input_motion(ctx, evt->motion.x, evt->motion.y);
            return 1;

        case SDL_TEXTINPUT:
            {
                nk_glyph glyph;
                memcpy(glyph, evt->text.text, NK_UTF_SIZE);
                nk_input_glyph(ctx, glyph);
            }
            return 1;

        case SDL_MOUSEWHEEL:
            nk_input_scroll(ctx,nk_vec2(evt->wheel.preciseX, evt->wheel.preciseY));
            return 1;
    }
    return 0;
}

NK_API
void torirs_nk_sdlren_shutdown(void)
{
    struct torirs_nk_sdlren_device *dev = &torirs_nk_sdlren__st.ogl;
    nk_font_atlas_clear(&torirs_nk_sdlren__st.atlas);
    nk_free(&torirs_nk_sdlren__st.ctx);
    SDL_DestroyTexture(dev->font_tex);
    /* glDeleteTextures(1, &dev->font_tex); */
    nk_buffer_free(&dev->cmds);
    memset(&torirs_nk_sdlren__st, 0, sizeof(torirs_nk_sdlren__st));
}

#endif /* TORIRS_NK_SDL_RENDERER_IMPLEMENTATION */
