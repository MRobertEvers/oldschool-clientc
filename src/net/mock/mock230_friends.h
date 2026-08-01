#ifndef SRC_NET_MOCK_MOCK230_FRIENDS_H
#define SRC_NET_MOCK_MOCK230_FRIENDS_H

#include <stdint.h>

/*
 * The friend / ignore / private-chat service.
 *
 * This is the port of LostCity's `engine/src/server/friend/FriendServer.ts` +
 * `FriendServerRepository.ts`. In the reference that is a *separate process*
 * reached over a WebSocket, with a worker thread on the world's side; the whole
 * of that machinery exists to survive a multi-process, multi-world 2004
 * deployment, and this server is one process and one world. So what is ported
 * is the reference's **message vocabulary and its rules**, as ordinary
 * in-process calls. See docs/FRIENDS_PRIVATE_CHAT.md §5 for the list of what is
 * deliberately left behind (the socket, the JSON framing, the thread, the
 * per-world player tables, the nine RELAY_* admin opcodes, the moderation
 * tables and the automated ban).
 *
 * Three properties of the reference are load-bearing and are kept:
 *
 * 1. **Keyed by base-37 name, never by player slot.** `getFollowers` — "who has
 *    alice in their list" — has to answer for people who are *not logged in*,
 *    which a per-`Mock230Player` array structurally cannot do. Every entry here
 *    outlives the player slot that created it.
 *
 * 2. **Process-scoped, not world-scoped.** The store is a file-static in
 *    mock230_friends.c rather than a field on `struct Mock230Server`, because
 *    `serve()` (mock230_main.c) `memset`s the world at the top of every
 *    connection. A roster on the world would be erased between two sessions of
 *    the same process, which is exactly the case this service exists to
 *    handle: alice adds bob while bob is offline. The reference gets this for
 *    free by being a different process; here it is a deliberate choice, and
 *    `mock230_friends_reset` is the only thing that clears it.
 *
 * 3. **`isVisibleTo` is the whole social-visibility policy** and is ported
 *    verbatim (FriendServerRepository.ts:332-355). Staff bypass, then
 *    ignored-by-target, then OFF, then FRIENDS-iff-mutual, else visible; an
 *    unknown player defaults to OFF.
 *
 * ------------------------------------------------------------------
 * PERSISTENCE: THERE IS NONE. THIS IS IN-MEMORY ONLY, AND ON PURPOSE.
 * ------------------------------------------------------------------
 *
 * A restart loses every friend list, every ignore list and every chat mode.
 * There is no `mock230_friends_save`, no `mock230_friends_load`, and no file
 * format — deliberately, and this paragraph is the loudness that decision was
 * required to come with (docs/FRIENDS_PRIVATE_CHAT.md §6, and
 * docs/osrs230_mockserver.md §3.15, which records what happens when intent is
 * written down and later read as fact).
 *
 * The reasoning, in short, because a reader who does not know it will "fix"
 * this by wiring the save file and be wrong:
 *
 *   - `mock230_save_player` / `mock230_load_player` have **no callers at all**,
 *     so a returning player is not a case anything in this server can be tested
 *     against today.
 *   - The reference does **not** put friend lists in the player save either. It
 *     keeps name-keyed `friendlist` / `ignorelist` tables, precisely because
 *     followers must be enumerable while their owner is offline. So wiring the
 *     player save would not give this feature persistence; it needs a second,
 *     name-keyed store either way.
 *
 * The follow-up, when someone wants it, is the reference's shape: one
 * name-keyed file owned and written by this module, reusing the write-then-
 * rename and the name sanitisation already in mock230_save.c. That is a
 * separate work item with its own test. What must NOT happen is a
 * `mock230_friends_save()` with no callers.
 */

struct Mock230Player;

enum
{
    /*
     * Storage ceilings, not policy. These size C arrays, which is the one kind
     * of number mock230_ids.h says stays in C ("content decides how much of the
     * array is used and the loader checks it fits"). The *caps* — how many
     * friends a player may actually have — are content's and come from
     * `.constant`s; see mock230_friends_cap_friends below. A content cap larger
     * than the ceiling here is reported at resolve time rather than silently
     * clamped.
     */

    /*
     * Names the service knows about at once, logged in or not. Entries are
     * created on demand — being *listed* by somebody is enough — and never
     * recycled inside one process run, which is what makes an offline follower
     * enumerable.
     *
     * It has to comfortably exceed the friend cap, not merely equal it: one
     * player with a full list occupies that many roster slots plus their own.
     */
    MOCK230_SOCIAL_ROSTER_MAX = 512,
    /* Per-list ceilings. The lists themselves are grown on demand, so a roster
     * of 512 names that nobody has listed costs 512 small structs and no
     * arrays. */
    MOCK230_SOCIAL_FRIENDS_MAX = 256,
    MOCK230_SOCIAL_IGNORES_MAX = 128,
};

/*
 * The private-chat mode, as the wire encodes it.
 *
 * A protocol encoding rather than content: these are the numbers the client
 * puts in CHAT_SETMODE and reads back out of CHAT_FILTER_SETTINGS, so no
 * content file could state them and none could be checked against one. Same
 * three values as the reference's `ChatModePrivate` (ChatModes.ts).
 *
 * `public` has a fourth value (HIDE = 3) and `tradeduel` has the same three;
 * both are stored and echoed and read by nothing, here as in the reference.
 */
enum Mock230ChatModePrivate
{
    MOCK230_CHAT_PRIVATE_ON = 0,
    MOCK230_CHAT_PRIVATE_FRIENDS = 1,
    MOCK230_CHAT_PRIVATE_OFF = 2,
};

/**
 * What a mutation did.
 *
 * The reference returns nothing from all four of these and even *commented out*
 * its own `console.error`s on the failure paths — it is silent when you add
 * someone twice, silent when the list is full, silent when you delete a name
 * that is not there. That silence is the ported behaviour, so no caller of this
 * module may turn a non-OK result into a message to the player: inventing one
 * would be inventing a behaviour the reference does not have
 * (docs/FRIENDS_PRIVATE_CHAT.md §2.3). The result exists so the *caller* knows
 * whether anything changed and therefore whether to send an update packet, and
 * so the selftest can assert on the cap.
 */
enum Mock230SocialResult
{
    MOCK230_SOCIAL_OK = 0,
    /** Already in the list (add), or not in it (delete). Nothing changed. */
    MOCK230_SOCIAL_UNCHANGED,
    /** Refused by the content cap. */
    MOCK230_SOCIAL_FULL,
    /** The name does not round-trip through base 37 — the reference's
     *  `fromBase37(name) === 'invalid_name'` check. */
    MOCK230_SOCIAL_INVALID_NAME,
    /** The service itself is out of roster slots. An engine ceiling being hit,
     *  which is a bug report rather than a game outcome; it is reported to
     *  stderr where it happens. */
    MOCK230_SOCIAL_NO_ROOM,
};

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/**
 * Drop the entire roster.
 *
 * Deliberately **not** called from `mock230_world_reset`: the world is reset
 * between socket sessions and the roster is the thing that has to survive that.
 * The callers are the selftest (so one section cannot see another's names) and
 * a host that genuinely wants a clean process.
 *
 * Also drops the cached content caps, so a content reload re-reads them.
 */
void
mock230_friends_reset(void);

/* ------------------------------------------------------------------ */
/* Presence — the reference's PLAYER_LOGIN / PLAYER_LOGOUT            */
/* ------------------------------------------------------------------ */

/**
 * Register a name as online in this world.
 *
 * `private_mode` is clamped to the three valid values exactly as the reference
 * does (out of range becomes ON, FriendServer.ts:120-123). `staff_level > 1`
 * makes the name staff, which is the one thing that bypasses `isVisibleTo`.
 *
 * Returns 0 when the roster is full, 1 otherwise. The caller sends the list
 * dump and the follower broadcast; this function sends nothing, because this
 * module owns no encoder.
 */
int
mock230_friends_login(
    int64_t name37,
    int public_mode,
    int private_mode,
    int trade_mode,
    int staff_level);

/**
 * Mark a name offline. The entry, its lists and its modes stay — only `world`
 * goes to 0, because a follower must still be able to see the name in their
 * list with "Offline" beside it.
 *
 * (The reference deletes the row here and reloads it from its database on the
 * next login. With no database, deleting would be losing the list.)
 */
void
mock230_friends_logout(int64_t name37);

/** Is this name online, and on which world? 0 = offline or unknown. This is the
 *  raw answer; `mock230_friends_get` is the one that applies `isVisibleTo`. */
int
mock230_friends_world(int64_t name37);

/** The reference's PLAYER_CHAT_SETMODE. `private_mode` is clamped as at login.
 *  Modes live here rather than on `struct Mock230Player` so there is one copy:
 *  `isVisibleTo` is the thing that reads them, and it answers for names whose
 *  player slot is long gone. */
void
mock230_friends_set_chat_modes(
    int64_t name37,
    int public_mode,
    int private_mode,
    int trade_mode);

/** Read the three modes back, for the CHAT_FILTER_SETTINGS echo. Any out
 *  pointer may be NULL. Unknown name yields the reference's defaults. */
void
mock230_friends_chat_modes(
    int64_t name37,
    int* out_public,
    int* out_private,
    int* out_trade);

/* ------------------------------------------------------------------ */
/* Mutation — FRIENDLIST_ADD / DEL, IGNORELIST_ADD / DEL               */
/* ------------------------------------------------------------------ */

enum Mock230SocialResult
mock230_friends_add(
    int64_t name37,
    int64_t target37);

enum Mock230SocialResult
mock230_friends_del(
    int64_t name37,
    int64_t target37);

enum Mock230SocialResult
mock230_friends_ignore_add(
    int64_t name37,
    int64_t target37);

enum Mock230SocialResult
mock230_friends_ignore_del(
    int64_t name37,
    int64_t target37);

/* ------------------------------------------------------------------ */
/* Queries — what an encoder needs to write a packet                   */
/* ------------------------------------------------------------------ */

/** How many names are in this player's friend list. */
int
mock230_friends_count(int64_t name37);

/**
 * One row of the friend list, in insertion order.
 *
 * `*out_world` is the number the wire carries and the client draws: **0 when
 * the friend is offline OR when `isVisibleTo` says this viewer may not see
 * them**, non-zero otherwise. That conflation is the reference's
 * (`sendPlayerWorldUpdate`, FriendServer.ts:475) and is the whole of how
 * "Private chat: off" hides you from your own friends.
 *
 * Returns 0 for an out-of-range index, leaving the outputs untouched.
 */
int
mock230_friends_get(
    int64_t name37,
    int index,
    int64_t* out_name37,
    int* out_world);

int
mock230_friends_ignore_count(int64_t name37);

int
mock230_friends_ignore_get(
    int64_t name37,
    int index,
    int64_t* out_name37);

/** Is `other37` in `name37`'s friend / ignore list? */
int
mock230_friends_is_friend(
    int64_t name37,
    int64_t other37);

int
mock230_friends_is_ignored(
    int64_t name37,
    int64_t other37);

/**
 * May `viewer37` see `other37`'s online status?
 *
 * FriendServerRepository.ts:332-355, ported unchanged. Note the asymmetry: it
 * is `other`'s ignore list and `other`'s private mode that decide, and
 * `viewer`'s staff level that overrides.
 */
int
mock230_friends_visible_to(
    int64_t viewer37,
    int64_t other37);

/**
 * Everyone who has `name37` in their friend list — the reference's
 * `getFollowers`, which is what a login, a logout or a chat-mode change has to
 * broadcast to.
 *
 * Writes up to `max` names and returns how many exist; a return greater than
 * `max` means the caller's buffer was too small.
 */
int
mock230_friends_followers(
    int64_t name37,
    int64_t* out,
    int max);

/* ------------------------------------------------------------------ */
/* Private messages                                                    */
/* ------------------------------------------------------------------ */

/**
 * The next message id, which is **never 0**.
 *
 * The client dedupes private messages against a zero-filled ring
 * (`app->pm_message_ids`), so a 0 id is a message it silently drops — the
 * reference says so in as many words at `World.pmCount` ("can't be 0 as
 * clients will ignore the pm, their array is filled with 0 as default").
 *
 * The reference mixes a random byte in to keep ids apart across worlds and
 * restarts; there is one world here, and this tree keeps its randomness behind
 * one fixed seed on purpose, so this counts deterministically instead.
 */
int32_t
mock230_friends_next_pm_id(void);

/* ------------------------------------------------------------------ */
/* Caps — content's numbers, read by name                              */
/* ------------------------------------------------------------------ */

/*
 * `^friend_max`, `^ignore_max` and `^private_message_max_bytes` out of the
 * content tree's `.constant` files, the same seam `^lootdrop_duration` uses
 * (CONTENT_ARCHITECTURE.md §8.2(e)). They are not enum keys and not C
 * literals: the reference states them as engine literals
 * (FriendServerRepository.ts:231,268 and MessagePrivateHandler.ts:13), and a
 * cap is a config-shaped constant, which this tree's rules put in content.
 *
 * **Until content declares them** — this lane could not write the content tree
 * — they are absent, which is reported once to stderr and falls back to the
 * storage ceiling above. That is a deliberately visible degradation and not a
 * hidden default: a missing cap means "as many as the array holds", never a
 * number this file made up.
 *
 * `_bytes`, not `_chars`: the private-message cap counts the *packed* bytes on
 * the wire, which is what the reference tests (`input.length > 100`, on the raw
 * slice, before WordPack.unpack) and what this server tests. Roughly two typed
 * characters ride in each byte, so a cap named for characters would have been a
 * cap that lied by a factor of two.
 */
int
mock230_friends_cap_friends(void);

int
mock230_friends_cap_ignores(void);

int
mock230_friends_cap_pm_bytes(void);

/* ------------------------------------------------------------------ */
/* Spam protection                                                     */
/* ------------------------------------------------------------------ */

/**
 * The reference's `player.socialProtect`: **one social packet per tick**, per
 * player, across the whole family (both list adds, both deletes, the private
 * message and public chat). Every one of the six handlers checks it first and
 * latches it (e.g. FriendListAddHandler.ts:9,14).
 *
 * Returns 1 and latches when the packet may be processed, 0 when this player
 * has already spent this tick's social packet. `phase_cleanup_player` clears
 * the latch, where the reference clears it in `resetEntity`.
 *
 * It matters more here than it did in 2004: the modern client can drive these
 * paths from CS2 far faster than the 2004 one could from its own UI.
 */
int
mock230_friends_social_gate(struct Mock230Player* player);

#endif
