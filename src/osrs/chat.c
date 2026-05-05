#include "chat.h"

#include "osrs/buildcachedat.h"
#include "osrs/game.h"
#include "osrs/interface_state.h"
#include "osrs/revconfig/uiscene.h"
#include "tori_rs_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Chat area geometry (matches Client.ts / RS2 layout) ─────────────────── */
#define CHAT_X       7
#define CHAT_Y     342
#define CHAT_W     504
#define CHAT_H      75   /* scrollable message area height */
#define CHAT_LINE_H  14  /* pixel height per message line  */
#define CHAT_MAX_VISIBLE (CHAT_H / CHAT_LINE_H)

#define CHATINPUT_X  7
#define CHATINPUT_Y 459
#define CHATINPUT_W 500
#define CHATINPUT_H  12

/* ── Message type colours (0xRRGGBB) ────────────────────────────────────── */
static int
chat_type_colour(int type)
{
    switch( type )
    {
    case 1:  return 0xFFFF00; /* game message */
    case 2:  return 0xFF0000; /* trade request */
    case 3:  return 0xFF00FF; /* private from */
    case 4:  return 0xC000FF; /* private to */
    case 5:  return 0xFF6000; /* modlevel chat */
    case 7:  return 0x00FFFF; /* clan chat */
    default: return 0xFFFFFF; /* public */
    }
}

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

struct Chat*
chat_new(void)
{
    struct Chat* c = calloc(1, sizeof(struct Chat));
    if( c )
        c->pm_count = 0;
    return c;
}

void
chat_free(struct Chat* chat)
{
    free(chat);
}

/* ── Message ring ──────────────────────────────────────────────────────── */

void
chat_add(struct Chat* chat, int type, const char* sender, const char* text)
{
    if( !chat )
        return;
    int n = chat->message_count;
    if( n >= CHAT_MESSAGE_CAP )
        n = CHAT_MESSAGE_CAP - 1;
    else
        chat->message_count = n + 1;

    /* Shift ring down: [0] is newest. */
    memmove(&chat->messages[1], &chat->messages[0],
            (size_t)n * sizeof(struct ChatMessage));
    chat->messages[0].type = type;
    if( sender )
        strncpy(chat->messages[0].user, sender, sizeof(chat->messages[0].user) - 1);
    else
        chat->messages[0].user[0] = '\0';
    if( text )
        strncpy(chat->messages[0].text, text, sizeof(chat->messages[0].text) - 1);
    else
        chat->messages[0].text[0] = '\0';

    /* Update scroll height to reflect new message. */
    chat->chat_scroll_height = chat->message_count * CHAT_LINE_H;
}

/* ── Helpers ───────────────────────────────────────────────────────────── */

static void
emit_text(
    struct ToriRSRenderCommandBuffer* buf,
    struct DashPixFont*               font,
    int                               font_id,
    const char*                       text,
    int                               x,
    int                               y,
    int                               colour)
{
    if( !buf || !font || !text || !text[0] )
        return;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind                = TORIRS_GFX_DRAW_FONT;
    c->_font_draw.font_id  = font_id;
    c->_font_draw.font     = font;
    c->_font_draw.text     = (uint8_t*)text;
    c->_font_draw.x        = x;
    c->_font_draw.y        = y;
    c->_font_draw.color_rgb = colour;
}

static void
emit_rect(
    struct ToriRSRenderCommandBuffer* buf,
    int x, int y, int w, int h,
    int colour, int alpha, uint8_t fill)
{
    if( !buf || w <= 0 || h <= 0 )
        return;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind              = TORIRS_GFX_DRAW_RECT;
    c->_rect_draw.x      = x;
    c->_rect_draw.y      = y;
    c->_rect_draw.w      = w;
    c->_rect_draw.h      = h;
    c->_rect_draw.color_rgb = colour;
    c->_rect_draw.alpha  = alpha;
    c->_rect_draw.fill   = fill;
}

/* Resolve the first available font from the UIScene. */
static struct DashPixFont*
chat_resolve_font(struct GGame* game, int* out_id)
{
    if( !game || !game->ui_scene )
        return NULL;

    static const char* const names[] = { "p11", "p12", "b12", "q8" };
    for( int i = 0; i < 4; i++ )
    {
        int fid = uiscene_font_find_id(game->ui_scene, names[i]);
        if( fid < 0 && game->buildcachedat )
        {
            int ref = buildcachedat_get_font_ref_id(game->buildcachedat, names[i]);
            if( ref >= 0 )
            {
                struct DashPixFont* f = uiscene_font_get(game->ui_scene, ref);
                if( f )
                    fid = uiscene_font_add(game->ui_scene, names[i], f);
            }
        }
        if( fid >= 0 )
        {
            struct DashPixFont* f = uiscene_font_get(game->ui_scene, fid);
            if( f )
            {
                *out_id = fid;
                return f;
            }
        }
    }
    return NULL;
}

/* Resolve font: `preset_font_id` is a UIScene id from UITree load (-1 = pick first available). */
static struct DashPixFont*
chat_font_for_draw(struct GGame* game, int preset_font_id, int* out_id)
{
    if( preset_font_id >= 0 && game && game->ui_scene )
    {
        struct DashPixFont* f = uiscene_font_get(game->ui_scene, preset_font_id);
        if( f )
        {
            *out_id = preset_font_id;
            return f;
        }
    }
    return chat_resolve_font(game, out_id);
}

/* ── Frame draw ────────────────────────────────────────────────────────── */

void
chat_draw_messages(
    struct Chat*                      chat,
    struct GGame*                     game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int                               preset_font_id)
{
    if( !chat || !game || !cmdbuf )
        return;

    int font_id = -1;
    struct DashPixFont* font = chat_font_for_draw(game, preset_font_id, &font_id);
    if( !font || chat->message_count <= 0 )
        return;

    int line_h = font->height2d > 0 ? font->height2d : CHAT_LINE_H;

    /* Compute the visible range given scroll position. */
    int scroll = chat->chat_scroll_pos;
    int first_line = scroll / line_h; /* index into messages[] (0=newest) */
    int visible    = CHAT_H / line_h;

    /* Messages are stored newest-first; draw from bottom up. */
    int draw_bottom_y = CHAT_Y + CHAT_H;

    for( int i = 0; i < visible; i++ )
    {
        int msg_idx = first_line + i;
        if( msg_idx >= chat->message_count )
            break;
        struct ChatMessage* m = &chat->messages[msg_idx];
        if( !m->text[0] )
            continue;

        int y = draw_bottom_y - (i + 1) * line_h;

        char line[288];
        if( m->user[0] )
        {
            int used = snprintf(line, sizeof(line), "%s: %s", m->user, m->text);
            (void)used;
        }
        else
        {
            strncpy(line, m->text, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
        }

        /* Shadow. */
        emit_text(cmdbuf, font, font_id, line, CHAT_X + 1, y + 1, 0x000000);
        emit_text(cmdbuf, font, font_id, line, CHAT_X, y, chat_type_colour(m->type));
    }

    /* Scrollbar (right side of chat area). */
    if( chat->chat_scroll_height > CHAT_H )
    {
        int sb_x = CHAT_X + CHAT_W - 16;
        int sb_y = CHAT_Y;
        int sb_h = CHAT_H;

        /* Track background. */
        emit_rect(cmdbuf, sb_x, sb_y, 16, sb_h, 0x000000, 128, 1);

        /* Grip. */
        int max_scroll = chat->chat_scroll_height - CHAT_H;
        if( max_scroll <= 0 )
            max_scroll = 1;
        int grip_h = (sb_h * CHAT_H) / chat->chat_scroll_height;
        if( grip_h < 8 )
            grip_h = 8;
        int grip_y = sb_y + (chat->chat_scroll_pos * (sb_h - grip_h)) / max_scroll;
        emit_rect(cmdbuf, sb_x + 2, grip_y + 1, 12, grip_h - 2, 0x808080, 255, 1);
    }
}

void
chat_draw_input(
    struct Chat*                      chat,
    struct GGame*                     game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int                               preset_font_id)
{
    if( !chat || !game || !cmdbuf )
        return;

    int font_id = -1;
    struct DashPixFont* font = chat_font_for_draw(game, preset_font_id, &font_id);
    if( !font )
        return;

    /* Draw chat input line. */
    if( chat->chat_input[0] != '\0' )
    {
        char input_buf[128];
        snprintf(input_buf, sizeof(input_buf), "> %s", chat->chat_input);
        emit_text(cmdbuf, font, font_id, input_buf,
                  CHATINPUT_X, CHATINPUT_Y, 0xFFFFFF);
    }
}

void
chat_draw_privacy(
    struct Chat*                      chat,
    struct GGame*                     game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int                               preset_font_id)
{
    if( !chat || !game || !cmdbuf )
        return;

    int font_id = -1;
    struct DashPixFont* font = chat_font_for_draw(game, preset_font_id, &font_id);
    if( !font )
        return;

    /* Privacy buttons at bottom. Labels: Public, Private, Trade, Report Abuse.
     * Based on Client.ts drawPrivacySettings layout. */
    int privacy_y = 481;
    int button_height = 12;
    
    const char* labels[] = { "Public", "Private", "Trade", "Report" };
    int button_colors[] = {
        chat->chat_public_mode ? 0x00FF00 : 0xFFFFFF,
        chat->chat_private_mode ? 0x00FF00 : 0xFFFFFF,
        chat->chat_trade_mode ? 0x00FF00 : 0xFFFFFF,
        0xFFFFFF
    };
    
    int x_positions[] = { CHATINPUT_X, CHATINPUT_X + 120, CHATINPUT_X + 230, CHATINPUT_X + 340 };
    
    for( int i = 0; i < 4; i++ )
    {
        emit_text(cmdbuf, font, font_id, labels[i],
                  x_positions[i], privacy_y, button_colors[i]);
    }
}

void
chat_draw(
    struct Chat*                      chat,
    struct GGame*                     game,
    struct ToriRSRenderCommandBuffer* cmdbuf)
{
    if( !chat || !game || !cmdbuf )
        return;

    chat_draw_messages(chat, game, cmdbuf, -1);
    chat_draw_input(chat, game, cmdbuf, -1);
    chat_draw_privacy(chat, game, cmdbuf, -1);
}

/* ── Click / input ─────────────────────────────────────────────────────── */

void
chat_handle_click(
    struct Chat*  chat,
    struct GGame* game,
    int           mx,
    int           my,
    int           button)
{
    if( !chat || !game || button != 1 /* left */ )
        return;

    /* Scrollbar arrows / track (right side of chat area). */
    int sb_x = CHAT_X + CHAT_W - 16;
    if( mx >= sb_x && mx < sb_x + 16 && my >= CHAT_Y && my < CHAT_Y + CHAT_H )
    {
        int rel_y = my - CHAT_Y;
        int max_scroll = chat->chat_scroll_height - CHAT_H;
        if( max_scroll <= 0 )
            return;

        /* Up arrow zone at top 16px, down arrow at bottom 16px. */
        if( rel_y < 16 )
        {
            chat->chat_scroll_pos -= CHAT_LINE_H;
        }
        else if( rel_y >= CHAT_H - 16 )
        {
            chat->chat_scroll_pos += CHAT_LINE_H;
        }
        else
        {
            /* Drag to position on track. */
            int track_h = CHAT_H - 32;
            if( track_h > 0 )
                chat->chat_scroll_pos = ((rel_y - 16) * max_scroll) / track_h;
        }
        if( chat->chat_scroll_pos < 0 )
            chat->chat_scroll_pos = 0;
        if( chat->chat_scroll_pos > max_scroll )
            chat->chat_scroll_pos = max_scroll;
    }
}
