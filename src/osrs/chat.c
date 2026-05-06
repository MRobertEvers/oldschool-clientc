#include "chat.h"

#include "graphics/dash.h"
#include "jbase37.h"
#include "osrs/buildcachedat.h"
#include "osrs/gamenet_send.h"
#include "osrs/game.h"
#include "osrs/interface_state.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/ui_scrollbar_emit.h"
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
    /* Matches Client.ts drawChat + areaChatback.draw(17,357).
     * Scrollback bottom pen is local y=70 (chatScrollPos + 70 − line*14); separator hline at 77. */
    out->chatback_screen_x = 17;
    out->chatback_screen_y = 357;
    out->clip_w = 463;
    out->clip_h = 77;
    out->text_x_local = 4;
    out->scrollbar_x_local = 463;
    out->separator_y_local = 77; /* bottom_pen_local = separator_y_local − 7 → 70 */
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

/* Client.ts drawChat message fragment colours (Colour.ts / literals). */
#define COL_CHAT_BLACK 0x000000
#define COL_CHAT_BLUE 0x0000FF   /* Colour.BLUE in TS is 0xff → full RGB blue */
#define COL_CHAT_DARKRED 0x800000 /* Colour.DARKRED */
#define COL_CHAT_TRADE_PURPLE 0x800080
#define COL_CHAT_DUEL_BROWN 0x7e3200

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
    /* chat_scroll_height: recomputed in chat_draw_messages (filters + Client.ts +7). */
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
    c->_font_draw.shadowed = 0;
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
    int w = dashfont_text_width_taggable(font, (uint8_t*)(void*)text);
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
        if( fid < 0 && game->gamecache )
        {
            int ref = gamecache_get_font_ref_id(game->gamecache, names[i]);
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

/** Strip @cr1@/@cr2@ mod prefixes (Client.ts drawChat). */
static void
chat_strip_mod_prefix(const char* user, char* out, size_t out_cap, int* mod_level)
{
    if( mod_level )
        *mod_level = 0;
    if( !out || out_cap == 0 )
        return;
    out[0] = '\0';
    if( !user || !user[0] )
        return;

    const char* p = user;
    if( strncmp(p, "@cr1@", 5) == 0 )
    {
        if( mod_level )
            *mod_level = 1;
        p += 5;
    }
    else if( strncmp(p, "@cr2@", 5) == 0 )
    {
        if( mod_level )
            *mod_level = 2;
        p += 5;
    }
    strncpy(out, p, out_cap - 1);
    out[out_cap - 1] = '\0';
}

static int
chat_sender_is_friend_or_self(struct GGame* g, const char* sender)
{
    if( !g || !sender || !sender[0] )
        return 0;
    uint64_t h = strtobase37(sender);
    for( int i = 0; i < g->friend_count; i++ )
    {
        if( (uint64_t)g->friend_usernames[i] == h )
            return 1;
    }
    char self[64];
    game_chat_input_screen_name(g, self, sizeof(self));
    if( strtobase37(self) == h )
        return 1;
    return 0;
}

/** Mirrors Client.ts drawChat visibility (chat modes + split private + friend checks). */
static int
chat_message_should_show(
    struct GGame* g,
    struct Chat* ch,
    int type,
    const char* sender_display)
{
    if( !ch )
        return 1;

    int pub  = ch->chat_public_mode;
    int priv = ch->chat_private_mode;
    int tr   = ch->chat_trade_mode;
    int sp   = ch->split_private;

    switch( type )
    {
    case 0:
        return 1;
    case 1:
        return 1;
    case 2:
        return pub == 0 || (pub == 1 && chat_sender_is_friend_or_self(g, sender_display));
    case 3:
    case 7:
        if( sp != 0 )
            return 0;
        if( type == 7 )
            return 1;
        return priv == 0 || (priv == 1 && chat_sender_is_friend_or_self(g, sender_display));
    case 4:
        return tr == 0 || (tr == 1 && chat_sender_is_friend_or_self(g, sender_display));
    case 5:
        return sp == 0 && priv < 2;
    case 6:
        return sp == 0 && priv < 2;
    case 8:
        return tr == 0 || (tr == 1 && chat_sender_is_friend_or_self(g, sender_display));
    default:
        return 1;
    }
}

/** Client.ts drawChat: multi-fragment colours per message type. */
static void
chat_emit_message_line(
    struct ToriRSRenderCommandBuffer* cmdbuf,
    struct DashPixFont* font,
    int font_id,
    int text_x,
    int y,
    struct ChatMessage* m,
    char* scratch,
    size_t scratch_cap)
{
    char display_sender[16];
    int mod_level = 0;
    chat_strip_mod_prefix(m->user, display_sender, sizeof(display_sender), &mod_level);

    const char* msg = m->text;
    int type = m->type;

    /* PixFont.drawString subtracts height2d from pen_y; dashfont/TORIRS_GFX_DRAW_FONT does not. */
    int draw_y = y;
    if( font->height2d > 0 )
        draw_y -= font->height2d;

    switch( type )
    {
    case 0:
        emit_text(cmdbuf, font, font_id, msg, text_x, draw_y, COL_CHAT_BLACK);
        break;

    case 1:
    case 2:
    {
        int x = text_x;
        if( mod_level > 0 )
            x += 14;
        int n = snprintf(scratch, scratch_cap, "%s:", display_sender);
        if( n > 0 && (size_t)n < scratch_cap )
        {
            emit_text(cmdbuf, font, font_id, scratch, x, draw_y, COL_CHAT_BLACK);
            x += dashfont_text_width(font, (uint8_t*)display_sender) + 8;
        }
        emit_text(cmdbuf, font, font_id, msg, x, draw_y, COL_CHAT_BLUE);
        break;
    }

    case 3:
    case 7:
    {
        static const char kFrom[] = "From ";
        int x = text_x;
        emit_text(cmdbuf, font, font_id, kFrom, x, draw_y, COL_CHAT_BLACK);
        x += dashfont_text_width(font, (uint8_t*)kFrom);
        if( mod_level > 0 )
            x += 14;
        int n = snprintf(scratch, scratch_cap, "%s:", display_sender);
        if( n > 0 && (size_t)n < scratch_cap )
        {
            emit_text(cmdbuf, font, font_id, scratch, x, draw_y, COL_CHAT_BLACK);
            x += dashfont_text_width(font, (uint8_t*)display_sender) + 8;
        }
        emit_text(cmdbuf, font, font_id, msg, x, draw_y, COL_CHAT_DARKRED);
        break;
    }

    case 4:
        snprintf(scratch, scratch_cap, "%s %s", display_sender, msg);
        emit_text(cmdbuf, font, font_id, scratch, text_x, draw_y, COL_CHAT_TRADE_PURPLE);
        break;

    case 5:
        emit_text(cmdbuf, font, font_id, msg, text_x, draw_y, COL_CHAT_DARKRED);
        break;

    case 6:
    {
        char to_pre[24];
        snprintf(to_pre, sizeof(to_pre), "To %s", display_sender);
        int n = snprintf(scratch, scratch_cap, "To %s:", display_sender);
        if( n > 0 && (size_t)n < scratch_cap )
        {
            emit_text(cmdbuf, font, font_id, scratch, text_x, draw_y, COL_CHAT_BLACK);
            int msg_x = text_x + dashfont_text_width(font, (uint8_t*)to_pre) + 12;
            emit_text(cmdbuf, font, font_id, msg, msg_x, draw_y, COL_CHAT_DARKRED);
        }
        break;
    }

    case 8:
        snprintf(scratch, scratch_cap, "%s %s", display_sender, msg);
        emit_text(cmdbuf, font, font_id, scratch, text_x, draw_y, COL_CHAT_DUEL_BROWN);
        break;

    default:
        emit_text(cmdbuf, font, font_id, msg, text_x, draw_y, COL_CHAT_BLACK);
        break;
    }
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
    if( !font )
        goto draw_separator;

    if( chat->message_count <= 0 )
        goto draw_separator;

    int cfg_line_h = chat_layout_line_h(game);
    int line_h = font->height2d > 0 ? font->height2d : cfg_line_h;

    int visible_msg_idx[CHAT_MESSAGE_CAP];
    int visible_count = 0;
    for( int mi = 0; mi < chat->message_count; mi++ )
    {
        struct ChatMessage* mm = &chat->messages[mi];
        if( !mm->text[0] )
            continue;
        char sender_disp[16];
        int mod_tmp;
        chat_strip_mod_prefix(mm->user, sender_disp, sizeof(sender_disp), &mod_tmp);
        if( !chat_message_should_show(game, chat, mm->type, sender_disp) )
            continue;
        visible_msg_idx[visible_count++] = mi;
    }

    int const scroll_extra = 7;
    chat->chat_scroll_height = visible_count * line_h + scroll_extra;
    if( chat->chat_scroll_height < 78 )
        chat->chat_scroll_height = 78;

    int max_scroll_pre = chat->chat_scroll_height - msg_h;
    if( max_scroll_pre < 0 )
        max_scroll_pre = 0;
    if( chat->chat_scroll_pos > max_scroll_pre )
        chat->chat_scroll_pos = max_scroll_pre;

    int scroll        = chat->chat_scroll_pos;
    int first_line    = scroll / line_h;
    int visible_lines = msg_h / line_h;

    /* Client.ts drawChat: y = chatScrollPos + 70 − line*14 (chatback-local pen).
     * 70 tracks separator_y_local − 7 (hline at 77). vi is newest-first index. */
    int bottom_pen_local_off = 0;
    if( use_client_pack )
        bottom_pen_local_off = lay.separator_y_local - 7;

    for( int i = 0; i < visible_lines; i++ )
    {
        int vi = first_line + i;
        if( vi >= visible_count )
            break;
        int msg_idx = visible_msg_idx[vi];
        struct ChatMessage* m = &chat->messages[msg_idx];

        int pen_y;
        if( use_client_pack )
            pen_y = lay.chatback_screen_y + scroll + bottom_pen_local_off - vi * line_h;
        else
        {
            int draw_bottom_y = msg_top_y + msg_h;
            pen_y = draw_bottom_y - (i + 1) * line_h;
        }

        char* scratch = chat->font_draw_line_scratch[msg_idx];
        chat_emit_message_line(cmdbuf, font, font_id, text_x, pen_y, m, scratch, 288);
    }

    /* Scrollbar (Client.ts x=463 local → screen); matches layer/interface emit path. */
    if( chat->chat_scroll_height > msg_h && msg_h >= 32 )
        ui_emit_scrollbar_commands(
            cmdbuf,
            game,
            sb_x,
            msg_top_y,
            msg_h,
            chat->chat_scroll_height,
            chat->chat_scroll_pos);

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
    snprintf(chat->font_draw_input_prefix, sizeof(chat->font_draw_input_prefix), "%s:", username);

    emit_text(cmdbuf, font, font_id, chat->font_draw_input_prefix, label_x, draw_y, 0x000000);

    char prefix_sp[80];
    snprintf(prefix_sp, sizeof(prefix_sp), "%s ", chat->font_draw_input_prefix);
    int wid_sp = dashfont_text_width(font, (uint8_t*)prefix_sp);
    int input_x =
        use_client_pack ? (lay.chatback_screen_x + wid_sp + 6) : (label_x + wid_sp + 6);

    snprintf(chat->font_draw_input_tail, sizeof(chat->font_draw_input_tail), "%s*", chat->chat_input);
    emit_text(cmdbuf, font, font_id, chat->font_draw_input_tail, input_x, draw_y, 0x0000FF);
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
chat_try_begin_typing_click(struct Chat* chat, struct GGame* game, int mx, int my)
{
    if( !chat || !game )
        return 0;
    if( !game_chat_public_input_eligible(game) )
        return 0;

    struct ChatUILayout lay;
    chat_layout_from_game(game, &lay);
    int const input_pen_y = lay.chatback_screen_y + lay.input_line_y_local;
    int const line_h      = chat_layout_line_h(game);
    int const y0          = input_pen_y - line_h - 2;
    int const y1          = input_pen_y + 6;
    int const x0          = lay.chatback_screen_x;
    int const x1          = lay.chatback_screen_x + lay.separator_w;

    if( mx >= x0 && mx < x1 && my >= y0 && my < y1 )
    {
        chat->chat_typing_active = 1;
        return 1;
    }
    return 0;
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
        gamenet_send_chat_setmode(
            game, chat->chat_public_mode, chat->chat_private_mode, chat->chat_trade_mode);
        return 1;
    }
    if( mx >= 135 && mx <= 235 )
    {
        chat->chat_private_mode = (chat->chat_private_mode + 1) % 3;
        gamenet_send_chat_setmode(
            game, chat->chat_public_mode, chat->chat_private_mode, chat->chat_trade_mode);
        return 1;
    }
    if( mx >= 273 && mx <= 373 )
    {
        chat->chat_trade_mode = (chat->chat_trade_mode + 1) % 3;
        gamenet_send_chat_setmode(
            game, chat->chat_public_mode, chat->chat_private_mode, chat->chat_trade_mode);
        return 1;
    }
    if( mx >= 412 && mx <= 512 )
    {
        gamenet_send_close_modal(game);
        /* TS then opens CC_REPORT_INPUT layer; not wired in this client yet. */
        return 1;
    }
    return 0;
}

int
chat_builtin_click_is_chrome(struct GGame const* game, int mx, int my)
{
    if( !game || !game->iface || game->iface->chat_interface_id >= 0 )
        return 0;
    struct ChatUILayout lay;
    chat_layout_from_game(game, &lay);
    int x0 = lay.chatback_screen_x;
    int x1 = lay.chatback_screen_x + lay.separator_w;
    int y0 = lay.chatback_screen_y;
    /* Bottom edge matches chat_handle_privacy_strip_click's upper bound (my <= 499). */
    int y1 = 499;
    return mx >= x0 && mx < x1 && my >= y0 && my <= y1;
}
