#ifndef SRC_GAME_RS_CHAT_H
#define SRC_GAME_RS_CHAT_H

#include "ui/uitree_chatview.h"

#include <stdint.h>

struct RS_Social;
struct UITreeHost;

/*
 * Chat model: the 100-entry message ring, filter-aware line layout, the typed
 * input line, and the social/amount input flows (reference chatText/chatType/
 * chatUsername + drawChat + handleInputKey). Message TYPES are the 254-era
 * protocol values carried by MESSAGE_GAME/MESSAGE_PRIVATE packets.
 */

#define RS_CHAT_MESSAGE_MAX 100
#define RS_CHAT_SENDER_LEN 64
#define RS_CHAT_TEXT_LEN 200
#define RS_CHAT_INPUT_LEN 80

/*
 * How many chat TYPES the per-type store covers.
 *
 * The reference keeps one list per type in a hash map keyed by the type, so it
 * has no ceiling at all; the number that matters is the one the cache's own
 * scripts sweep, and `[proc,script553]` walks `chattype` 0..118 looking for the
 * newest message. 128 covers that with room, and an out-of-range type from the
 * wire is dropped rather than clamped -- clamping would file a message under
 * somebody else's tab.
 */
#define RS_CHAT_TYPE_MAX 128
/** Messages kept per type (reference class55: a 100-entry ring, newest at 0). */
#define RS_CHAT_TYPE_LINES 100

/* Chat scrollbar geometry, region-local (reference: message column 463 wide,
 * 77px visible window; scrollbar sits just right of the column). */
#define RS_CHAT_SCROLLBAR_LEFT 463
#define RS_CHAT_VIEW_HEIGHT 77

enum RS_ChatMessageType
{
    RS_CHAT_TYPE_GAME = 0,
    RS_CHAT_TYPE_PUBLIC_MOD = 1,
    RS_CHAT_TYPE_PUBLIC = 2,
    RS_CHAT_TYPE_PRIVATE_FROM = 3,
    RS_CHAT_TYPE_TRADEREQ = 4,
    RS_CHAT_TYPE_PRIVATE_SYSTEM = 5,
    RS_CHAT_TYPE_PRIVATE_TO = 6,
    RS_CHAT_TYPE_PRIVATE_FROM_MOD = 7,
    RS_CHAT_TYPE_DUELREQ = 8,
};

/** Social-input kinds (reference socialInputType). */
enum RS_ChatSocialInput
{
    RS_CHAT_SOCIAL_NONE = 0,
    RS_CHAT_SOCIAL_ADD_FRIEND = 1,
    RS_CHAT_SOCIAL_DEL_FRIEND = 2,
    RS_CHAT_SOCIAL_ADD_IGNORE = 4,
    RS_CHAT_SOCIAL_DEL_IGNORE = 5,
};

struct RS_ChatMessage
{
    int type;
    char sender[RS_CHAT_SENDER_LEN];
    char text[RS_CHAT_TEXT_LEN];
};

/*
 * One message, as the cache's scripts read it (reference class76 / MessageNode).
 *
 * `name` and `sender` are two different strings and conflating them is a bug
 * with teeth: `name` is what the chatbox prints before the colon -- already
 * decorated, `<img=...>Zezima` -- and `sender` is the account the friend and
 * ignore lists are keyed on. `chat_gethistoryex_*` returns both, and the
 * friend/ignore state it also returns is computed from `sender`.
 *
 * `uid` is globally unique and increasing; it is the handle the scripts walk
 * the history with (`chat_getprevuid`). `clock` is the client cycle at insert,
 * which scripts compare against `clientclock` -- the broadcast banner in
 * script553 shows a message only while it is under 3000 cycles old.
 */
struct RS_ChatNode
{
    int uid;
    int clock;
    int type;
    char name[RS_CHAT_SENDER_LEN];
    char sender[RS_CHAT_SENDER_LEN];
    char text[RS_CHAT_TEXT_LEN];
    /** Global insertion order, newest first. The scripts walk history by uid
     *  rather than by type, so the per-type rings alone cannot answer them. */
    struct RS_ChatNode* newer;
    struct RS_ChatNode* older;
};

/** One type's ring. Allocated on first use: a client sees a handful of the 118
 *  types, and a dense table would be megabytes of nothing. */
struct RS_ChatTypeRing
{
    /** lines[0] is newest. Nodes are reused once the ring is full, which is
     *  what the reference does (class55.method1052 recycles line 99). */
    struct RS_ChatNode* lines[RS_CHAT_TYPE_LINES];
    int count;
};

struct RS_Chat
{
    /** messages[0] is newest (reference inserts at the front). */
    struct RS_ChatMessage messages[RS_CHAT_MESSAGE_MAX];
    int message_count;

    /* ---- The per-type store the cache's chatbox scripts read ---- */

    struct RS_ChatTypeRing* rings[RS_CHAT_TYPE_MAX];
    /** Newest node overall; `older` chains back through every live node. */
    struct RS_ChatNode* newest;
    struct RS_ChatNode* oldest;
    /** Next uid to hand out. Monotonic for the life of the client, so a uid
     *  never names two messages. */
    int next_uid;
    /** One-entry memo for uid lookup. The scripts walk the history strictly
     *  in order -- read uid, ask for the previous one, read that -- so a
     *  single remembered node turns a list walk per line into a pointer hop. */
    struct RS_ChatNode* uid_memo;
    /** CHAT_SETMESSAGEFILTER / CHAT_GETMESSAGEFILTER: the public-chat search
     *  string the chatbox filters on. Client-side, and empty means no filter. */
    char message_filter[RS_CHAT_TEXT_LEN];
    /** CHAT_SETTIMESTAMPS / CHAT_GETTIMESTAMPS: the timestamp display mode. */
    int timestamps;

    int scroll_pos;
    /** Scrollbar grip held this frame (reference scrollGrabbed); widens the
     *  grab hit area so a fast drag doesn't slip off the 16px column. */
    int scroll_grabbed;
    char input[RS_CHAT_INPUT_LEN];
    char username[RS_CHAT_SENDER_LEN];

    int social_input_open;
    int social_input_type; /* enum RS_ChatSocialInput */
    char social_input[RS_CHAT_SENDER_LEN];
    char social_header[96];

    int dialog_input_open;
    char dialog_input[16];
};

/** Filter modes + friend lookup the layout needs, snapshot per frame. */
struct RS_ChatFilters
{
    int public_mode;
    int private_mode;
    int trade_mode;
    struct RS_Social const* social;
};

void
RS_Chat_Init(struct RS_Chat* chat, char const* username);

/**
 * Does this message occupy a chat line under the current filters?
 *
 * The reference only advances its line counter for messages that pass, so this
 * is the one definition of "visible" for the surface path (`RS_Chat_BuildView`)
 * -- the layout and the scrollbar both read it, and two copies would disagree
 * the first time a filter mode gained a case, showing up as a line counted for
 * the scrollbar but never drawn.
 *
 * A cache revision asks nothing of this: `[proc,rebuildchatbox]` applies the
 * filters itself, in `~panetest` and `~filtertest`, over the same messages.
 */
int
RS_Chat_MessagePasses(
    struct RS_ChatFilters const* filters,
    struct RS_ChatMessage const* message);

/**
 * Append a message to both stores.
 *
 * `type` is the wire's chat type, not a client enum: MESSAGE_GAME carries it,
 * and the cache's scripts switch on it to pick a colour and a prefix. A type
 * outside 0..RS_CHAT_TYPE_MAX asserts -- it is a decode bug, and filing it
 * under a clamped type puts a private message on the public tab.
 *
 * `name` is the printable sender ("" for a system line), `sender` the account
 * name friend/ignore is keyed on (may be NULL when the wire carries none, and
 * then friend state reads as neither). `clock` is the client cycle; callers
 * with a CS2 host should go through RS_CS2Host_ChatAdd, which supplies it and
 * marks the chat-transmit channel dirty in one step.
 */
void
RS_Chat_AddMessage(
    struct RS_Chat* chat,
    int type,
    char const* name,
    char const* sender,
    char const* text,
    int clock);

/** Release the per-type store. The 254-era ring is inline and needs nothing. */
void
RS_Chat_Free(struct RS_Chat* chat);

/** CHAT_GETHISTORYLENGTH: messages held for `type`. Unknown type answers 0. */
int
RS_Chat_TypeCount(
    struct RS_Chat const* chat,
    int type);

/** CHAT_GETHISTORY*_BYTYPEANDLINE: line 0 is the newest. NULL past the end. */
struct RS_ChatNode const*
RS_Chat_NodeByTypeAndLine(
    struct RS_Chat const* chat,
    int type,
    int line);

/** CHAT_GETHISTORY*_BYUID. NULL once the message has fallen out of its ring. */
struct RS_ChatNode const*
RS_Chat_NodeByUid(
    struct RS_Chat* chat,
    int uid);

/** CHAT_GETPREVUID: the next OLDER message, -1 at the end of the history.
 *  This is the walk `[proc,rebuildchatbox]` fills the chatbox with. */
int
RS_Chat_PrevUid(
    struct RS_Chat* chat,
    int uid);

/** CHAT_GETNEXTUID: the next NEWER message, -1 at the head. */
int
RS_Chat_NextUid(
    struct RS_Chat* chat,
    int uid);

/** Friend/ignore state of a node's `sender`, as chat_gethistoryex_* reports
 *  it: 1 friend, 2 ignored, 0 neither. `social` may be NULL (answers 0). */
int
RS_Chat_NodeFriendState(
    struct RS_ChatNode const* node,
    struct RS_Social const* social);

/**
 * Render the model into the flattened draw view (reference drawChat layout:
 * p12 lines bottom-up at 14px, baseline y = scroll + 70 - line*14, input line
 * at y 90). `dialog_mounted` suppresses message drawing while a chat
 * interface occupies the region. ui_host is used only for MEASURE_TEXT.
 *
 * `focused` is the chat input's focus state: unfocused, the input line is the
 * "Press Enter to chat..." prompt rather than `name: typed*`, since the caret
 * would otherwise sit under a line that is not collecting keys.
 */
void
RS_Chat_BuildView(
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters,
    struct UITreeHost const* ui_host,
    int font_id,
    int dialog_mounted,
    int focused,
    struct UIChatView* out);

/**
 * Sender under a region-local point, for the minimenu social rows (inverse of
 * the BuildView line layout). Returns 1 and fills out_sender/out_chat_type
 * when the point lands on a line with a sender.
 */
int
RS_Chat_LineAt(
    struct RS_Chat const* chat,
    struct RS_ChatFilters const* filters,
    int local_x,
    int local_y,
    char* out_sender,
    int sender_cap,
    int* out_chat_type);

/** Scroll by wheel steps (positive = older messages), clamped. */
void
RS_Chat_Scroll(
    struct RS_Chat* chat,
    struct RS_ChatFilters const* filters,
    int wheel_y);

/**
 * Drive the chat scrollbar from a held left click (reference doScrollbar):
 * (local_x, local_y) are relative to the chat region origin; cycle is the
 * number of frames the button has been held (arrow scroll accelerates with it,
 * grip drag needs cycle > 0). Returns nonzero when the point fell on the
 * scrollbar (arrows, track or grip) and scroll_pos may have changed.
 */
int
RS_Chat_ScrollbarInput(
    struct RS_Chat* chat,
    struct RS_ChatFilters const* filters,
    int local_x,
    int local_y,
    int cycle);

/**
 * One keyboard event (reference handleInputKey chat branches): printable
 * chars append to whichever input line is open, backspace deletes, return
 * submits (public chat = local echo + optional send hook; social input
 * mutates the social store). Returns nonzero when the event changed state.
 */
int
RS_Chat_HandleKey(
    struct RS_Chat* chat,
    struct RS_Social* social,
    int key_typed,
    int key_pressed);

#endif
