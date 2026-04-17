#include "torirs_nk_sdl_input.h"

#include "nuklear/torirs_nuklear.h"

#include <SDL.h>
#include <string.h>

int
torirs_nk_sdl_translate_event(struct nk_context* ctx, SDL_Event* evt)
{
    if( !ctx || !evt )
        return 0;

    int ctrl_down = SDL_GetModState() & KMOD_CTRL;
    static int insert_toggle = 0;

    switch( evt->type )
    {
    case SDL_KEYUP:
    case SDL_KEYDOWN:
    {
        int down = evt->type == SDL_KEYDOWN;
        switch( evt->key.keysym.sym )
        {
        case SDLK_RSHIFT:
        case SDLK_LSHIFT:
            nk_input_key(ctx, NK_KEY_SHIFT, down);
            break;
        case SDLK_DELETE:
            nk_input_key(ctx, NK_KEY_DEL, down);
            break;
        case SDLK_KP_ENTER:
        case SDLK_RETURN:
            nk_input_key(ctx, NK_KEY_ENTER, down);
            break;
        case SDLK_TAB:
            nk_input_key(ctx, NK_KEY_TAB, down);
            break;
        case SDLK_BACKSPACE:
            nk_input_key(ctx, NK_KEY_BACKSPACE, down);
            break;
        case SDLK_HOME:
            nk_input_key(ctx, NK_KEY_TEXT_START, down);
            nk_input_key(ctx, NK_KEY_SCROLL_START, down);
            break;
        case SDLK_END:
            nk_input_key(ctx, NK_KEY_TEXT_END, down);
            nk_input_key(ctx, NK_KEY_SCROLL_END, down);
            break;
        case SDLK_PAGEDOWN:
            nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
            break;
        case SDLK_PAGEUP:
            nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
            break;
        case SDLK_z:
            nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && ctrl_down);
            break;
        case SDLK_r:
            nk_input_key(ctx, NK_KEY_TEXT_REDO, down && ctrl_down);
            break;
        case SDLK_c:
            nk_input_key(ctx, NK_KEY_COPY, down && ctrl_down);
            break;
        case SDLK_v:
            nk_input_key(ctx, NK_KEY_PASTE, down && ctrl_down);
            break;
        case SDLK_x:
            nk_input_key(ctx, NK_KEY_CUT, down && ctrl_down);
            break;
        case SDLK_b:
            nk_input_key(ctx, NK_KEY_TEXT_LINE_START, down && ctrl_down);
            break;
        case SDLK_e:
            nk_input_key(ctx, NK_KEY_TEXT_LINE_END, down && ctrl_down);
            break;
        case SDLK_UP:
            nk_input_key(ctx, NK_KEY_UP, down);
            break;
        case SDLK_DOWN:
            nk_input_key(ctx, NK_KEY_DOWN, down);
            break;
        case SDLK_ESCAPE:
            nk_input_key(ctx, NK_KEY_TEXT_RESET_MODE, down);
            break;
        case SDLK_INSERT:
            if( down )
                insert_toggle = !insert_toggle;
            if( insert_toggle )
                nk_input_key(ctx, NK_KEY_TEXT_INSERT_MODE, down);
            else
                nk_input_key(ctx, NK_KEY_TEXT_REPLACE_MODE, down);
            break;
        case SDLK_a:
            if( ctrl_down )
                nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, down);
            break;
        case SDLK_LEFT:
            if( ctrl_down )
                nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
            else
                nk_input_key(ctx, NK_KEY_LEFT, down);
            break;
        case SDLK_RIGHT:
            if( ctrl_down )
                nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
            else
                nk_input_key(ctx, NK_KEY_RIGHT, down);
            break;
        default:
            break;
        }
        return 1;
    }

    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEBUTTONDOWN:
    {
        int down = evt->type == SDL_MOUSEBUTTONDOWN;
        const int x = evt->button.x, y = evt->button.y;
        switch( evt->button.button )
        {
        case SDL_BUTTON_LEFT:
            if( evt->button.clicks > 1 )
                nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, down);
            nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
            break;
        case SDL_BUTTON_MIDDLE:
            nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down);
            break;
        case SDL_BUTTON_RIGHT:
            nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down);
            break;
        case SDL_BUTTON_X1:
            nk_input_button(ctx, NK_BUTTON_X1, x, y, down);
            break;
        case SDL_BUTTON_X2:
            nk_input_button(ctx, NK_BUTTON_X2, x, y, down);
            break;
        default:
            break;
        }
        return 1;
    }

    case SDL_MOUSEMOTION:
        if( ctx->input.mouse.grabbed )
        {
            int x = (int)ctx->input.mouse.prev.x, y = (int)ctx->input.mouse.prev.y;
            nk_input_motion(ctx, x + evt->motion.xrel, y + evt->motion.yrel);
        }
        else
            nk_input_motion(ctx, evt->motion.x, evt->motion.y);
        return 1;

    case SDL_TEXTINPUT:
    {
        nk_glyph glyph;
        memcpy(glyph, evt->text.text, NK_UTF_SIZE);
        nk_input_glyph(ctx, glyph);
        return 1;
    }

    case SDL_MOUSEWHEEL:
        nk_input_scroll(ctx, nk_vec2(evt->wheel.preciseX, evt->wheel.preciseY));
        return 1;

    default:
        break;
    }
    return 0;
}
