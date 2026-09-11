#include "rs_chat.h"

#include "rs_social.h"
#include "input/torirs_keymap.h"
#include "ui/uitree_host.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Reference palette (Colour.*). */
enum
{
    CHAT_COLOR_BLACK = 0x000000,
    CHAT_COLOR_BLUE = 0x0000FF,
    CHAT_COLOR_DARKBLUE = 0x000080,
    CHAT_COLOR_DARKRED = 0x800000,
    CHAT_COLOR_TRADE = 0x800080,
    CHAT_COLOR_DUEL = 0x7E3200,
    /* Not a reference colour: the unfocused prompt, muted against the
     * chatbox's tan so it reads as chrome rather than as a message. The
     * WORDING is the profile's (revconfig `prompt=`, @see RS_Chat_BuildView);
     * the colour stays here because the span api carries a plain int and no
     * markup parser sits between the ini and this file. */
    CHAT_COLOR_PROMPT = 0x4A443A,
};

void
RS_Chat_Init(struct RS_Chat* chat, char const* username)
{
    assert(chat);
    memset(chat, 0, sizeof(*chat));
    snprintf(chat->username, sizeof(chat->username), "%s", username ? username : "Player");
}

/*
 * Push one node onto its type's ring and onto the global order.
 *
 * The ring recycles rather than allocates once it is full, which is the
 * reference's own shape (class55.method1052 takes line 99 back and shifts):
 * a client that runs for an hour reaches steady state and stops touching the
 * allocator entirely.
 */
static struct RS_ChatNode*
chat_ring_push(
    struct RS_Chat* chat,
    int type)
{
    struct RS_ChatTypeRing* ring;
    struct RS_ChatNode* node;
    int i;

    assert(chat);
    assert(type >= 0 && type < RS_CHAT_TYPE_MAX);

    if( !chat->rings[type] )
    {
        chat->rings[type] = calloc(1, sizeof(*chat->rings[type]));
        assert(chat->rings[type]);
    }
    ring = chat->rings[type];

    if( ring->count == RS_CHAT_TYPE_LINES )
    {
        /* Recycle the oldest of this type: unlink it from the global order
         * first, or the list keeps a node the ring is about to overwrite. */
        node = ring->lines[RS_CHAT_TYPE_LINES - 1];
        assert(node);
        if( node->newer )
            node->newer->older = node->older;
        else
            chat->newest = node->older;
        if( node->older )
            node->older->newer = node->newer;
        else
            chat->oldest = node->newer;
        if( chat->uid_memo == node )
            chat->uid_memo = NULL;
    }
    else
    {
        node = calloc(1, sizeof(*node));
        assert(node);
        ring->count++;
    }

    for( i = ring->count - 1; i > 0; i-- )
        ring->lines[i] = ring->lines[i - 1];
    ring->lines[0] = node;

    memset(node, 0, sizeof(*node));
    node->older = chat->newest;
    if( chat->newest )
        chat->newest->newer = node;
    else
        chat->oldest = node;
    chat->newest = node;
    return node;
}

void
RS_Chat_AddMessage(
    struct RS_Chat* chat,
    int type,
    char const* name,
    char const* sender,
    char const* text,
    int clock)
{
    struct RS_ChatNode* node;

    assert(chat);
    assert(type >= 0);
    assert(type < RS_CHAT_TYPE_MAX);

    if( chat->message_count < RS_CHAT_MESSAGE_MAX )
        chat->message_count++;
    memmove(
        &chat->messages[1],
        &chat->messages[0],
        (size_t)(chat->message_count - 1) * sizeof(struct RS_ChatMessage));
    memset(&chat->messages[0], 0, sizeof(chat->messages[0]));
    chat->messages[0].type = type;
    if( name )
        snprintf(chat->messages[0].sender, sizeof(chat->messages[0].sender), "%s", name);
    if( text )
        snprintf(chat->messages[0].text, sizeof(chat->messages[0].text), "%s", text);

    node = chat_ring_push(chat, type);
    node->uid = chat->next_uid++;
    node->clock = clock;
    node->type = type;
    if( name )
        snprintf(node->name, sizeof(node->name), "%s", name);
    if( sender )
        snprintf(node->sender, sizeof(node->sender), "%s", sender);
    if( text )
        snprintf(node->text, sizeof(node->text), "%s", text);
}

void
RS_Chat_Free(struct RS_Chat* chat)
{
    int t;
    int i;

    if( !chat )
        return;
    for( t = 0; t < RS_CHAT_TYPE_MAX; t++ )
    {
        struct RS_ChatTypeRing* ring = chat->rings[t];
        if( !ring )
            continue;
        for( i = 0; i < ring->count; i++ )
            free(ring->lines[i]);
        free(ring);
        chat->rings[t] = NULL;
    }
    chat->newest = NULL;
    chat->oldest = NULL;
    chat->uid_memo = NULL;
}

int
RS_Chat_TypeCount(
    struct RS_Chat const* chat,
    int type)
{
    assert(chat);
    /* A type outside the table is not a caller bug here: the opcode's argument
     * comes from a cache script sweeping a range, and script553 sweeps 0..118
     * whether or not this client has ever seen those types. */
    if( type < 0 || type >= RS_CHAT_TYPE_MAX || !chat->rings[type] )
        return 0;
    return chat->rings[type]->count;
}

struct RS_ChatNode const*
RS_Chat_NodeByTypeAndLine(
    struct RS_Chat const* chat,
    int type,
    int line)
{
    assert(chat);
    if( type < 0 || type >= RS_CHAT_TYPE_MAX || !chat->rings[type] )
        return NULL;
    if( line < 0 || line >= chat->rings[type]->count )
        return NULL;
    return chat->rings[type]->lines[line];
}

/* Find by uid, newest first, remembering the answer.
 *
 * The memo is not an optimisation looking for a problem: rebuildchatbox reads
 * a node by uid and then immediately asks for that node's previous uid, once
 * per line it draws, so without it every line pays a walk from the head and a
 * full chatbox is quadratic in the history length. */
static struct RS_ChatNode*
chat_find_uid(
    struct RS_Chat* chat,
    int uid)
{
    struct RS_ChatNode* n;

    assert(chat);
    if( uid < 0 )
        return NULL;
    if( chat->uid_memo && chat->uid_memo->uid == uid )
        return chat->uid_memo;
    for( n = chat->newest; n; n = n->older )
    {
        if( n->uid == uid )
        {
            chat->uid_memo = n;
            return n;
        }
        /* Ordered by uid, so once we are past it the message is gone. */
        if( n->uid < uid )
            break;
    }
    return NULL;
}

struct RS_ChatNode const*
RS_Chat_NodeByUid(
    struct RS_Chat* chat,
    int uid)
{
    return chat_find_uid(chat, uid);
}

int
RS_Chat_PrevUid(
    struct RS_Chat* chat,
    int uid)
{
    struct RS_ChatNode* n = chat_find_uid(chat, uid);
    if( !n || !n->older )
        return -1;
    chat->uid_memo = n->older;
    return n->older->uid;
}

int
RS_Chat_NextUid(
    struct RS_Chat* chat,
    int uid)
{
    struct RS_ChatNode* n = chat_find_uid(chat, uid);
    if( !n || !n->newer )
        return -1;
    chat->uid_memo = n->newer;
    return n->newer->uid;
}

int
RS_Chat_NodeFriendState(
    struct RS_ChatNode const* node,
    struct RS_Social const* social)
{
    assert(node);
    if( !social || !node->sender[0] )
        return 0;
    if( RS_Social_IsFriend(social, node->sender) )
        return 1;
    if( RS_Social_IsIgnored(social, node->sender) )
        return 2;
    return 0;
}

static int
is_friend(
    struct RS_ChatFilters const* filters,
    char const* name)
{
    assert(name);
    if( !filters->social || !name[0] )
        return 0;
    for( int i = 0; i < filters->social->friend_count; i++ )
    {
        if( strcasecmp(filters->social->friend_name[i], name) == 0 )
            return 1;
    }
    return 0;
}

/* Does this message occupy a chat line under the current filters?
 * (The reference only advances `line` for messages that pass.)
 *
 * Exported as RS_Chat_MessagePasses because the layout and the scrollbar are
 * two readers of one rule; `message_passes` stays as the local spelling so the
 * call sites below read unchanged. */
int
RS_Chat_MessagePasses(
    struct RS_ChatFilters const* filters,
    struct RS_ChatMessage const* msg)
{
    switch( msg->type )
    {
    case RS_CHAT_TYPE_GAME:
        return 1;
    case RS_CHAT_TYPE_PUBLIC_MOD:
        return 1;
    case RS_CHAT_TYPE_PUBLIC:
        return filters->public_mode == 0 ||
               (filters->public_mode == 1 && is_friend(filters, msg->sender));
    case RS_CHAT_TYPE_PRIVATE_FROM:
        return filters->private_mode == 0 ||
               (filters->private_mode == 1 && is_friend(filters, msg->sender));
    case RS_CHAT_TYPE_PRIVATE_FROM_MOD:
        return 1;
    case RS_CHAT_TYPE_TRADEREQ:
        return filters->trade_mode == 0 ||
               (filters->trade_mode == 1 && is_friend(filters, msg->sender));
    case RS_CHAT_TYPE_DUELREQ:
        return filters->trade_mode == 0 ||
               (filters->trade_mode == 1 && is_friend(filters, msg->sender));
    case RS_CHAT_TYPE_PRIVATE_SYSTEM:
    case RS_CHAT_TYPE_PRIVATE_TO:
        return filters->private_mode < 2;
    default:
        return 0;
    }
}

static int
message_passes(
    struct RS_ChatFilters const* filters,
    struct RS_ChatMessage const* msg)
{
    return RS_Chat_MessagePasses(filters, msg);
}

/* Total pixel height of the filtered message column (reference chatScrollHeight):
 * one 14px line per visible message + 7px, floored at the 78px the scrollbar
 * math and clamps assume. */
static int
scroll_height_of(
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters)
{
    int total_lines = 0;
    int scroll_height;
    for( int i = 0; i < chat->message_count; i++ )
    {
        if( message_passes(filters, &chat->messages[i]) )
            total_lines++;
    }
    scroll_height = total_lines * 14 + 7;
    return scroll_height < 78 ? 78 : scroll_height;
}

static int
measure(
    struct UITreeHost const* ui_host,
    int font_id,
    char const* text)
{
    struct UITreeHostRequest req = {
        .kind = UITREE_HOST_MEASURE_TEXT,
        .u.measure_text.font_id = font_id,
        .u.measure_text.text = text,
    };
    return UITree_Host(ui_host, &req);
}

static struct UIChatViewSpan*
line_add_span(
    struct UIChatViewLine* line,
    int x,
    int color,
    char const* text)
{
    struct UIChatViewSpan* span;
    if( line->span_count >= UI_CHATVIEW_SPAN_MAX )
        return NULL;
    span = &line->spans[line->span_count++];
    span->x = x;
    span->color = color;
    strncpy(span->text, text, UI_CHATVIEW_TEXT_LEN - 1);
    span->text[UI_CHATVIEW_TEXT_LEN - 1] = '\0';
    return span;
}

/* Lay one visible message out into spans (reference drawChat per-type
 * branches; mod-crown icons are skipped until their sprites are wired). */
static void
layout_message(
    struct UIChatViewLine* line,
    struct RS_ChatMessage const* msg,
    struct UITreeHost const* ui_host,
    int font_id)
{
    char buf[UI_CHATVIEW_TEXT_LEN];
    int x = 4;

    switch( msg->type )
    {
    case RS_CHAT_TYPE_GAME:
        line_add_span(line, 4, CHAT_COLOR_BLACK, msg->text);
        break;
    case RS_CHAT_TYPE_PUBLIC_MOD:
    case RS_CHAT_TYPE_PUBLIC:
        snprintf(buf, sizeof(buf), "%s:", msg->sender);
        line_add_span(line, x, CHAT_COLOR_BLACK, buf);
        x += measure(ui_host, font_id, msg->sender) + 8;
        line_add_span(line, x, CHAT_COLOR_BLUE, msg->text);
        break;
    case RS_CHAT_TYPE_PRIVATE_FROM:
    case RS_CHAT_TYPE_PRIVATE_FROM_MOD:
        snprintf(buf, sizeof(buf), "From %s:", msg->sender);
        line_add_span(line, x, CHAT_COLOR_BLACK, buf);
        x += measure(ui_host, font_id, buf) + 8;
        line_add_span(line, x, CHAT_COLOR_DARKRED, msg->text);
        break;
    case RS_CHAT_TYPE_TRADEREQ:
        snprintf(buf, sizeof(buf), "%s %s", msg->sender, msg->text);
        line_add_span(line, 4, CHAT_COLOR_TRADE, buf);
        break;
    case RS_CHAT_TYPE_PRIVATE_SYSTEM:
        line_add_span(line, 4, CHAT_COLOR_DARKRED, msg->text);
        break;
    case RS_CHAT_TYPE_PRIVATE_TO:
        snprintf(buf, sizeof(buf), "To %s:", msg->sender);
        line_add_span(line, 4, CHAT_COLOR_BLACK, buf);
        x = measure(ui_host, font_id, buf) + 12;
        line_add_span(line, x, CHAT_COLOR_DARKRED, msg->text);
        break;
    case RS_CHAT_TYPE_DUELREQ:
        snprintf(buf, sizeof(buf), "%s %s", msg->sender, msg->text);
        line_add_span(line, 4, CHAT_COLOR_DUEL, buf);
        break;
    default:
        break;
    }
}

void
RS_Chat_BuildView(
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters,
    struct UITreeHost const* ui_host,
    int font_id,
    int dialog_mounted,
    int focused,
    char const* prompt,
    struct UIChatView* out)
{
    assert(chat && filters && out);
    memset(out, 0, sizeof(*out));
    out->font_id = font_id;
    out->scroll_pos = chat->scroll_pos;

    if( dialog_mounted )
        return; /* the mounted pack draws; visible stays 0 */
    out->visible = 1;

    if( chat->social_input_open || chat->dialog_input_open )
    {
        char buf[128];
        out->centered = 1;
        out->center_count = 2;
        out->center_lines[0].baseline_y = 40;
        line_add_span(
            &out->center_lines[0],
            0,
            CHAT_COLOR_BLACK,
            chat->social_input_open ? chat->social_header : "Enter amount:");
        out->center_lines[1].baseline_y = 60;
        snprintf(
            buf,
            sizeof(buf),
            "%s*",
            chat->social_input_open ? chat->social_input : chat->dialog_input);
        line_add_span(&out->center_lines[1], 0, CHAT_COLOR_DARKBLUE, buf);
        return;
    }

    {
        int line = 0;
        int total_lines = 0;
        for( int i = 0; i < chat->message_count; i++ )
        {
            struct RS_ChatMessage const* msg = &chat->messages[i];
            int baseline;
            if( !message_passes(filters, msg) )
                continue;
            baseline = chat->scroll_pos + 70 - line * 14;
            line++;
            total_lines++;
            if( baseline <= 0 || baseline >= 110 )
                continue;
            if( out->line_count < UI_CHATVIEW_LINE_MAX )
            {
                struct UIChatViewLine* view_line = &out->lines[out->line_count++];
                view_line->baseline_y = baseline;
                layout_message(view_line, msg, ui_host, font_id);
            }
        }
        out->scroll_height = total_lines * 14 + 7;
        if( out->scroll_height < 78 )
            out->scroll_height = 78;
    }

    {
        char buf[128];
        out->has_input_line = 1;
        out->input_line.baseline_y = 90;
        /* Unfocused, the line says how to start typing instead of showing a
         * name and a caret it is not collecting anything into. The wording is
         * the profile's (`prompt=` on the chat component, which a `@mobile`
         * override retells for a finger); NULL/empty is "no override", and
         * keeps the reference wording. */
        if( !focused )
        {
            line_add_span(
                &out->input_line,
                4,
                CHAT_COLOR_PROMPT,
                prompt && prompt[0] ? prompt : "Press Enter to chat...");
            return;
        }
        snprintf(buf, sizeof(buf), "%s:", chat->username);
        line_add_span(&out->input_line, 4, CHAT_COLOR_BLACK, buf);
        {
            char input_buf[RS_CHAT_INPUT_LEN + 2];
            int x;
            snprintf(buf, sizeof(buf), "%s: ", chat->username);
            x = measure(ui_host, out->font_id, buf) + 6;
            snprintf(input_buf, sizeof(input_buf), "%s*", chat->input);
            line_add_span(&out->input_line, x, CHAT_COLOR_BLUE, input_buf);
        }
    }
}

int
RS_Chat_LineAt(
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters,
    int local_x,
    int local_y,
    char* out_sender,
    int sender_cap,
    int* out_chat_type)
{
    assert(chat && filters);
    (void)local_x;

    if( chat->social_input_open || chat->dialog_input_open )
        return 0;

    {
        int line = 0;
        for( int i = 0; i < chat->message_count; i++ )
        {
            struct RS_ChatMessage const* msg = &chat->messages[i];
            int baseline;
            if( !message_passes(filters, msg) )
                continue;
            baseline = chat->scroll_pos + 70 - line * 14;
            line++;
            if( baseline <= 0 || baseline >= 110 )
                continue;
            /* Text band: baseline-13 .. baseline+1 (14px stride). */
            if( local_y < baseline - 13 || local_y > baseline + 1 )
                continue;
            if( !msg->sender[0] )
                return 0;
            switch( msg->type )
            {
            case RS_CHAT_TYPE_PUBLIC_MOD:
            case RS_CHAT_TYPE_PUBLIC:
            case RS_CHAT_TYPE_PRIVATE_FROM:
            case RS_CHAT_TYPE_PRIVATE_FROM_MOD:
            case RS_CHAT_TYPE_TRADEREQ:
            case RS_CHAT_TYPE_DUELREQ:
                if( out_sender )
                {
                    strncpy(out_sender, msg->sender, (size_t)sender_cap - 1);
                    out_sender[sender_cap - 1] = '\0';
                }
                if( out_chat_type )
                    *out_chat_type = msg->type;
                return 1;
            default:
                return 0;
            }
        }
    }
    return 0;
}

void
RS_Chat_Scroll(
    struct RS_Chat* chat,
    struct RS_ChatFilters const* filters,
    int wheel_y)
{
    int scroll_height;

    assert(chat && filters);
    scroll_height = scroll_height_of(chat, filters);

    chat->scroll_pos += wheel_y * 14;
    if( chat->scroll_pos < 0 )
        chat->scroll_pos = 0;
    if( chat->scroll_pos > scroll_height - 77 )
        chat->scroll_pos = scroll_height - 77;
}

int
RS_Chat_ScrollbarInput(
    struct RS_Chat* chat,
    struct RS_ChatFilters const* filters,
    int x,
    int y,
    int cycle)
{
    /* Reference doScrollbar (Client.ts:10525) with left=463, top=0, height=77
     * — the chat scrollbar's local geometry. scroll_pos is the non-inverted
     * grip offset the renderer draws from (torirs_frame vertical_scrollbar_grip),
     * so the same proportional math maps a grip drag straight back to it. */
    int const left = RS_CHAT_SCROLLBAR_LEFT;
    int const top = 0;
    int const height = RS_CHAT_VIEW_HEIGHT;
    int scroll_height = scroll_height_of(chat, filters);
    int padding = chat->scroll_grabbed ? 32 : 0;
    int handled = 0;

    assert(chat && filters);
    chat->scroll_grabbed = 0;

    if( x >= left && x < left + 16 && y >= top && y < top + 16 )
    {
        chat->scroll_pos -= cycle * 4; /* up arrow */
        handled = 1;
    }
    else if( x >= left && x < left + 16 && y >= top + height - 16 && y < top + height )
    {
        chat->scroll_pos += cycle * 4; /* down arrow */
        handled = 1;
    }
    else if(
        x >= left - padding && x < left + padding + 16 && y >= top + 16 &&
        y < top + height - 16 && cycle > 0 )
    {
        /* Grip drag: place the grip centre under the cursor. */
        int grip_size = ((height - 32) * height) / scroll_height;
        int grip_y;
        int max_y;
        if( grip_size < 8 )
            grip_size = 8;
        grip_y = y - top - grip_size / 2 - 16;
        max_y = height - grip_size - 32;
        if( max_y > 0 )
            chat->scroll_pos = ((scroll_height - height) * grip_y) / max_y;
        chat->scroll_grabbed = 1;
        handled = 1;
    }

    if( chat->scroll_pos > scroll_height - height )
        chat->scroll_pos = scroll_height - height;
    if( chat->scroll_pos < 0 )
        chat->scroll_pos = 0;
    return handled;
}

/* Submit whichever input line is open. */
static int
chat_submit(
    struct RS_Chat* chat,
    struct RS_Social* social)
{
    if( chat->social_input_open )
    {
        if( chat->social_input[0] && social )
        {
            switch( chat->social_input_type )
            {
            case RS_CHAT_SOCIAL_ADD_FRIEND:
                RS_Social_AddFriend(social, chat->social_input, 0);
                break;
            case RS_CHAT_SOCIAL_DEL_FRIEND:
                RS_Social_DelFriend(social, chat->social_input);
                break;
            case RS_CHAT_SOCIAL_ADD_IGNORE:
                RS_Social_AddIgnore(social, chat->social_input);
                break;
            case RS_CHAT_SOCIAL_DEL_IGNORE:
                RS_Social_DelIgnore(social, chat->social_input);
                break;
            default:
                break;
            }
        }
        chat->social_input_open = 0;
        chat->social_input[0] = '\0';
        return 1;
    }
    if( chat->dialog_input_open )
    {
        /* Amount consumed by the pending server op once networking exists. */
        chat->dialog_input_open = 0;
        chat->dialog_input[0] = '\0';
        return 1;
    }
    if( chat->input[0] )
    {
        /* Local echo; the send hook (MESSAGE_PUBLIC) attaches with net.
         * This path is the dat1 surface chatbox, which has no CS2 host and so
         * no client clock to stamp -- the clock is only ever read back by the
         * cache's scripts, and a dat1 boot runs none. */
        RS_Chat_AddMessage(
            chat, RS_CHAT_TYPE_PUBLIC, chat->username, chat->username, chat->input, 0);
        chat->input[0] = '\0';
        chat->scroll_pos = 0;
        return 1;
    }
    return 0;
}

static int
append_char(
    char* buf,
    int cap,
    int ch)
{
    int len = (int)strlen(buf);
    if( len + 1 >= cap )
        return 0;
    buf[len] = (char)ch;
    buf[len + 1] = '\0';
    return 1;
}

int
RS_Chat_HandleKey(
    struct RS_Chat* chat,
    struct RS_Social* social,
    int key_typed,
    int key_pressed)
{
    char* target;
    int cap;

    assert(chat);

    if( chat->social_input_open )
    {
        target = chat->social_input;
        cap = (int)sizeof(chat->social_input);
    }
    else if( chat->dialog_input_open )
    {
        target = chat->dialog_input;
        cap = (int)sizeof(chat->dialog_input);
    }
    else
    {
        target = chat->input;
        cap = (int)sizeof(chat->input);
    }

    /* key_typed carries OSRS internal key codes (see torirs_keymap.c), not raw
     * VK/ASCII: Enter is 84 and Backspace 85, while 13 is Escape. */
    if( key_typed == TORIRS_OSRSKEY_ENTER )
        return chat_submit(chat, social);
    if( key_typed == TORIRS_OSRSKEY_BACKSPACE )
    {
        int len = (int)strlen(target);
        if( len == 0 )
            return 0;
        target[len - 1] = '\0';
        return 1;
    }
    if( key_typed == -1 && key_pressed >= 32 )
    {
        /*
         * The reference caps typed characters at 122 ('z') everywhere except a
         * line that already starts with "::", where it goes to 126:
         *
         *   key >= 32 && (key <= 122 || (chatTyped.startsWith('::') && key <= 126))
         *
         * That widening is the whole reason a client command can carry `~`
         * (126), `{`, `|` or `}` — without it `::~zuk` is untypeable, because
         * the tilde is silently dropped. Social and amount prompts keep the
         * narrow range, as they do in the reference.
         */
        int max_char = 122;
        if( target == chat->input && target[0] == ':' && target[1] == ':' )
            max_char = 126;
        if( key_pressed > max_char )
            return 0;
        /* Amount input takes digits only (reference dialogInput). */
        if( chat->dialog_input_open && (key_pressed < '0' || key_pressed > '9') )
            return 0;
        return append_char(target, cap, key_pressed);
    }
    return 0;
}
