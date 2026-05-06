#include "chat.h"

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/core/clientprot_core.h"
#include "osrs/game.h"
#include "osrs/interface_state.h"
#include "osrs/revconfig/uiscene.h"
#include "tori_rs_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Legacy fallbacks when UITree passes explicit positive rects (unused by frame path). */
#define CHAT_X 7
#define CHAT_Y 342
#define CHAT_W 504
#define CHAT_H 75
#define CHATINPUT_X 7
#define CHATINPUT_Y 459

void
chat_layout_builtin(struct ChatUILayout* out)
{
    if( !out )
        return;
    /* Matches Client.ts drawChat + areaChatback.draw(17,357). */
    out->chatback_screen_x = 17;
    out->chatback_screen_y = 357;
    out->clip_w = 463;
    out->clip_h = 77;
    out->text_x_local = 4;
    out->scrollbar_x_local = 463;
    out->separator_y_local = 77;
    out->separator_w = 479;
    out->line_h = 14;
    out->input_line_y_local = 90;
}

void
chat_layout_apply_mask(struct ChatUILayout* dst, struct ChatUILayout const* patch, unsigned mask)
{
    if( !dst || !patch )
        return;
    if( mask & CHAT_LAYOUT_BIT_CHATBACK_SCREEN_X )
        dst->chatback_screen_x = patch->chatback_screen_x;
    if( mask & CHAT_LAYOUT_BIT_CHATBACK_SCREEN_Y )
        dst->chatback_screen_y = patch->chatback_screen_y;
    if( mask & CHAT_LAYOUT_BIT_CLIP_W )
        dst->clip_w = patch->clip_w;
    if( mask & CHAT_LAYOUT_BIT_CLIP_H )
        dst->clip_h = patch->clip_h;
    if( mask & CHAT_LAYOUT_BIT_TEXT_X_LOCAL )
        dst->text_x_local = patch->text_x_local;
    if( mask & CHAT_LAYOUT_BIT_SCROLLBAR_X_LOCAL )
        dst->scrollbar_x_local = patch->scrollbar_x_local;
    if( mask & CHAT_LAYOUT_BIT_SEPARATOR_Y_LOCAL )
        dst->separator_y_local = patch->separator_y_local;
    if( mask & CHAT_LAYOUT_BIT_SEPARATOR_W )
        dst->separator_w = patch->separator_w;
    if( mask & CHAT_LAYOUT_BIT_LINE_H )
        dst->line_h = patch->line_h;
    if( mask & CHAT_LAYOUT_BIT_INPUT_LINE_Y_LOCAL )
        dst->input_line_y_local = patch->input_line_y_local;
}

void
chat_layout_from_game(struct GGame const* game, struct ChatUILayout* out)
{
    chat_layout_builtin(out);
    if( game && game->chat_layout_valid )
        *out = game->chat_layout;
}

int
chat_layout_line_h(struct GGame const* game)
{
    struct ChatUILayout L;
    chat_layout_from_game(game, &L);
    return L.line_h > 0 ? L.line_h : 14;
}

/* Privacy strip: Client.ts areaBackbase1.draw(0, 453); centreStringTag coords are buffer-local. */
#define PRIVACY_STRIP_SCREEN_Y 453

/* Client.ts Colour enum (PixFont stores these ints verbatim). */
#define COL_PRIV_WHITE 0xFFFFFF
#define COL_PRIV_BLACK 0x000000
#define COL_PRIV_GREEN 0xFF00
#define COL_PRIV_YELLOW 0xFFFF00
#define COL_PRIV_RED 0xFF0000
#define COL_PRIV_CYAN 0xFFFF

/* ── Message type colours (0xRRGGBB) ────────────────────────────────────── */
static int
chat_type_colour(int type)
{
    switch( type )
    {
    case 1:
        return 0xFFFF00; /* game message */
    case 2:
        return 0xFF0000; /* trade request */
    case 3:
        return 0xFF00FF; /* private from */
    case 4:
        return 0xC000FF; /* private to */
    case 5:
        return 0xFF6000; /* modlevel chat */
    case 7:
        return 0x00FFFF; /* clan chat */
    default:
        return 0xFFFFFF; /* public */
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
chat_add(
    struct Chat* chat,
    struct GGame const* game,
    int type,
    const char* sender,
    const char* text)
{
    if( !chat )
        return;
    int n = chat->message_count;
    if( n >= CHAT_MESSAGE_CAP )
        n = CHAT_MESSAGE_CAP - 1;
    else
        chat->message_count = n + 1;

    /* Shift ring down: [0] is newest. */
    memmove(&chat->messages[1], &chat->messages[0], (size_t)n * sizeof(struct ChatMessage));
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
    int lh = chat_layout_line_h(game);
    chat->chat_scroll_height = chat->message_count * lh;
}

/* ── Helpers ───────────────────────────────────────────────────────────── */

static void
emit_text(
    struct ToriRSRenderCommandBuffer* buf,
    struct DashPixFont* font,
    int font_id,
    const char* text,
    int x,
    int y,
    int colour)
{
    if( !buf || !font || !text || !text[0] )
        return;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind = TORIRS_GFX_DRAW_FONT;
    c->_font_draw.font_id = font_id;
    c->_font_draw.font = font;
    c->_font_draw.text = (uint8_t*)text;
    c->_font_draw.x = x;
    c->_font_draw.y = y;
    c->_font_draw.color_rgb = colour;
}

/** Matches Client.ts PixFont.centreStringTag(..., shadowed: true): centred on centre_x_screen,
 * local_y as passed to centreStringTag before y -= height2d. */
static void
emit_centre_string_tag_shadowed(
    struct ToriRSRenderCommandBuffer* cmdbuf,
    struct DashPixFont* font,
    int font_id,
    int centre_x_screen,
    int local_y,
    const char* text,
    int colour_rgb)
{
    if( !cmdbuf || !font || !text || !text[0] )
        return;

    int h2 = font->height2d > 0 ? font->height2d : 12;
    int draw_y = PRIVACY_STRIP_SCREEN_Y + local_y - h2;
    int w = dashfont_text_width(font, (uint8_t*)(void*)text);
    int left_x = centre_x_screen - (w / 2);

    emit_text(cmdbuf, font, font_id, text, left_x + 1, draw_y + 1, COL_PRIV_BLACK);
    emit_text(cmdbuf, font, font_id, text, left_x, draw_y, colour_rgb);
}

static void
emit_rect(
    struct ToriRSRenderCommandBuffer* buf,
    int x,
    int y,
    int w,
    int h,
    int colour,
    int alpha,
    uint8_t fill)
{
    if( !buf || w <= 0 || h <= 0 )
        return;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind = TORIRS_GFX_DRAW_RECT;
    c->_rect_draw.x = x;
    c->_rect_draw.y = y;
    c->_rect_draw.w = w;
    c->_rect_draw.h = h;
    c->_rect_draw.color_rgb = colour;
    c->_rect_draw.alpha = alpha;
    c->_rect_draw.fill = fill;
}

/* Resolve the first available font from the UIScene. */
static struct DashPixFont*
chat_resolve_font(
    struct GGame* game,
    int* out_id)
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
chat_font_for_draw(
    struct GGame* game,
    int preset_font_id,
    int* out_id)
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
    struct Chat* chat,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int preset_font_id,
    int area_x,
    int area_y,
    int area_w,
    int area_h)
{
    if( !chat || !game || !cmdbuf )
        return;

    int use_client_pack = (area_x < 0 && area_y < 0 && area_w < 0 && area_h < 0);

    struct ChatUILayout lay;
    chat_layout_from_game(game, &lay);

    int text_x;
    int msg_top_y;
    int msg_h;
    int sb_x;

    if( use_client_pack )
    {
        text_x = lay.chatback_screen_x + lay.text_x_local;
        msg_top_y = lay.chatback_screen_y;
        msg_h = lay.clip_h;
        sb_x = lay.chatback_screen_x + lay.scrollbar_x_local;
    }
    else
    {
        text_x = area_x >= 0 ? area_x : CHAT_X;
        msg_top_y = area_y >= 0 ? area_y : CHAT_Y;
        msg_h = area_h > 0 ? area_h : CHAT_H;
        sb_x = text_x + (area_w > 0 ? area_w : CHAT_W) - 16;
    }

    int font_id = -1;
    struct DashPixFont* font = chat_font_for_draw(game, preset_font_id, &font_id);
    if( !font || chat->message_count <= 0 )
        goto draw_separator;

    int cfg_line_h = chat_layout_line_h(game);
    int line_h = font->height2d > 0 ? font->height2d : cfg_line_h;

    /* Compute the visible range given scroll position. */
    int scroll = chat->chat_scroll_pos;
    int first_line = scroll / line_h; /* index into messages[] (0=newest) */
    int visible = msg_h / line_h;

    /* Messages are stored newest-first; draw from bottom up. */
    int draw_bottom_y = msg_top_y + msg_h;

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

        /* Client.ts drawChat: single drawString per fragment, no drop shadow. */
        emit_text(cmdbuf, font, font_id, line, text_x, y, chat_type_colour(m->type));
    }

    /* Scrollbar (Client.ts x=463 local → screen). */
    if( chat->chat_scroll_height > msg_h )
    {
        int sb_y = msg_top_y;
        int sb_h = msg_h;

        /* Track background. */
        emit_rect(cmdbuf, sb_x, sb_y, 16, sb_h, 0x000000, 128, 1);

        /* Grip. */
        int max_scroll = chat->chat_scroll_height - msg_h;
        if( max_scroll <= 0 )
            max_scroll = 1;
        int grip_h = (sb_h * msg_h) / chat->chat_scroll_height;
        if( grip_h < 8 )
            grip_h = 8;
        int grip_y = sb_y + (chat->chat_scroll_pos * (sb_h - grip_h)) / max_scroll;
        emit_rect(cmdbuf, sb_x + 2, grip_y + 1, 12, grip_h - 2, 0x808080, 255, 1);
    }

draw_separator:
    /* Client.ts Pix2D.hline(0, 77, 479, BLACK) in chatback-local space. */
    if( use_client_pack )
    {
        int sep_y = lay.chatback_screen_y + lay.separator_y_local;
        emit_rect(cmdbuf, lay.chatback_screen_x, sep_y, lay.separator_w, 1, 0x000000, 255, 1);
    }
}

void
chat_draw_input(
    struct Chat* chat,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int preset_font_id,
    int pen_x,
    int pen_y)
{
    if( !chat || !game || !cmdbuf )
        return;

    int font_id = -1;
    struct DashPixFont* font = chat_font_for_draw(game, preset_font_id, &font_id);
    if( !font )
        return;

    int use_client_pack = (pen_x < 0 && pen_y < 0);
    struct ChatUILayout lay;
    chat_layout_from_game(game, &lay);

    int label_x;
    int input_pen_y;

    if( use_client_pack )
    {
        label_x = lay.chatback_screen_x + lay.text_x_local;
        input_pen_y = lay.chatback_screen_y + lay.input_line_y_local;
    }
    else
    {
        label_x = pen_x >= 0 ? pen_x : CHATINPUT_X;
        input_pen_y = pen_y >= 0 ? pen_y : CHATINPUT_Y;
    }

    int draw_y = input_pen_y;
    if( font->height2d > 0 )
        draw_y -= font->height2d;

    /* Client.ts: drawString(username + ':', …); drawString(chatInput + '*', …) — no shadow. */
    char username[64];
    game_chat_input_screen_name(game, username, sizeof(username));
    char prefix[72];
    snprintf(prefix, sizeof(prefix), "%s:", username);

    emit_text(cmdbuf, font, font_id, prefix, label_x, draw_y, 0x000000);

    char prefix_sp[80];
    snprintf(prefix_sp, sizeof(prefix_sp), "%s ", prefix);
    int wid_sp = dashfont_text_width(font, (uint8_t*)prefix_sp);
    int input_x =
        use_client_pack ? (lay.chatback_screen_x + wid_sp + 6) : (label_x + wid_sp + 6);

    char tail[96];
    snprintf(tail, sizeof(tail), "%s*", chat->chat_input);
    emit_text(cmdbuf, font, font_id, tail, input_x, draw_y, 0x0000FF);
}

void
chat_draw_privacy(
    struct Chat* chat,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmdbuf,
    int preset_font_id)
{
    if( !chat || !game || !cmdbuf )
        return;

    int font_id = -1;
    struct DashPixFont* font = chat_font_for_draw(game, preset_font_id, &font_id);
    if( !font )
        return;

    /* Mirrors Client.ts redrawPrivacySettings (centreStringTag + Colour.*). */
    emit_centre_string_tag_shadowed(cmdbuf, font, font_id, 55, 28, "Public chat", COL_PRIV_WHITE);

    int pub_col = COL_PRIV_GREEN;
    const char* pub_str = "On";
    if( chat->chat_public_mode == 1 )
    {
        pub_col = COL_PRIV_YELLOW;
        pub_str = "Friends";
    }
    else if( chat->chat_public_mode == 2 )
    {
        pub_col = COL_PRIV_RED;
        pub_str = "Off";
    }
    else if( chat->chat_public_mode == 3 )
    {
        pub_col = COL_PRIV_CYAN;
        pub_str = "Hide";
    }
    emit_centre_string_tag_shadowed(cmdbuf, font, font_id, 55, 41, pub_str, pub_col);

    emit_centre_string_tag_shadowed(cmdbuf, font, font_id, 184, 28, "Private chat", COL_PRIV_WHITE);

    int priv_col = COL_PRIV_GREEN;
    const char* priv_str = "On";
    if( chat->chat_private_mode == 1 )
    {
        priv_col = COL_PRIV_YELLOW;
        priv_str = "Friends";
    }
    else if( chat->chat_private_mode == 2 )
    {
        priv_col = COL_PRIV_RED;
        priv_str = "Off";
    }
    emit_centre_string_tag_shadowed(cmdbuf, font, font_id, 184, 41, priv_str, priv_col);

    emit_centre_string_tag_shadowed(cmdbuf, font, font_id, 324, 28, "Trade/duel", COL_PRIV_WHITE);

    int trade_col = COL_PRIV_GREEN;
    const char* trade_str = "On";
    if( chat->chat_trade_mode == 1 )
    {
        trade_col = COL_PRIV_YELLOW;
        trade_str = "Friends";
    }
    else if( chat->chat_trade_mode == 2 )
    {
        trade_col = COL_PRIV_RED;
        trade_str = "Off";
    }
    emit_centre_string_tag_shadowed(cmdbuf, font, font_id, 324, 41, trade_str, trade_col);

    emit_centre_string_tag_shadowed(cmdbuf, font, font_id, 458, 33, "Report abuse", COL_PRIV_WHITE);
}

void
chat_draw(
    struct Chat* chat,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmdbuf)
{
    if( !chat || !game || !cmdbuf )
        return;

    chat_draw_messages(chat, game, cmdbuf, -1, -1, -1, -1, -1);
    chat_draw_input(chat, game, cmdbuf, -1, -1, -1);
    chat_draw_privacy(chat, game, cmdbuf, -1);
}

/* ── Click / input ─────────────────────────────────────────────────────── */

void
chat_handle_click(
    struct Chat* chat,
    struct GGame* game,
    int mx,
    int my,
    int button)
{
    if( !chat || !game || button != 1 /* left */ )
        return;

    /* Client.ts chatback layout (matches drawScrollbar + message clip). */
    struct ChatUILayout lay;
    chat_layout_from_game(game, &lay);
    int const sb_x = lay.chatback_screen_x + lay.scrollbar_x_local;
    int const sb_y = lay.chatback_screen_y;
    int const sb_h = lay.clip_h;
    int const step = chat_layout_line_h(game);

    if( mx >= sb_x && mx < sb_x + 16 && my >= sb_y && my < sb_y + sb_h )
    {
        int rel_y = my - sb_y;
        int max_scroll = chat->chat_scroll_height - sb_h;
        if( max_scroll <= 0 )
            return;

        /* Up arrow zone at top 16px, down arrow at bottom 16px. */
        if( rel_y < 16 )
        {
            chat->chat_scroll_pos -= step;
        }
        else if( rel_y >= sb_h - 16 )
        {
            chat->chat_scroll_pos += step;
        }
        else
        {
            /* Drag to position on track. */
            int track_h = sb_h - 32;
            if( track_h > 0 )
                chat->chat_scroll_pos = ((rel_y - 16) * max_scroll) / track_h;
        }
        if( chat->chat_scroll_pos < 0 )
            chat->chat_scroll_pos = 0;
        if( chat->chat_scroll_pos > max_scroll )
            chat->chat_scroll_pos = max_scroll;
    }
}

int
chat_handle_privacy_strip_click(
    struct Chat* chat,
    struct GGame* game,
    int mx,
    int my,
    int button)
{
    if( !chat || !game || button != 1 /* left */ )
        return 0;
    /* Client.ts drawChat / chat interface covers this area when chatComId !== -1. */
    if( !game->iface || game->iface->chat_interface_id >= 0 )
        return 0;

    /* Client.ts chatModeLoop(): y 467–499; x bands for each column + report abuse. */
    if( my < 467 || my > 499 )
        return 0;

    if( mx >= 6 && mx <= 106 )
    {
        chat->chat_public_mode = (chat->chat_public_mode + 1) % 4;
        clientprot_chat_setmode(
            game, chat->chat_public_mode, chat->chat_private_mode, chat->chat_trade_mode);
        return 1;
    }
    if( mx >= 135 && mx <= 235 )
    {
        chat->chat_private_mode = (chat->chat_private_mode + 1) % 3;
        clientprot_chat_setmode(
            game, chat->chat_public_mode, chat->chat_private_mode, chat->chat_trade_mode);
        return 1;
    }
    if( mx >= 273 && mx <= 373 )
    {
        chat->chat_trade_mode = (chat->chat_trade_mode + 1) % 3;
        clientprot_chat_setmode(
            game, chat->chat_public_mode, chat->chat_private_mode, chat->chat_trade_mode);
        return 1;
    }
    if( mx >= 412 && mx <= 512 )
    {
        clientprot_close_modal(game);
        /* TS then opens CC_REPORT_INPUT layer; not wired in this client yet. */
        return 1;
    }
    return 0;
}
