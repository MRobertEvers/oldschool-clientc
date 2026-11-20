#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_FRIENDCHAT_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_FRIENDCHAT_H

#include <stdint.h>

/*
 * Friends chat: the chat-channel tab (interface 7, `chatchannel_current`) and
 * its setup panel (94, `chatchannel_setup`).
 *
 * Built from RSProx recordings, not from a reference port: LostCity is 2004
 * and has no friends chat. The owner's September test series (index 745..770,
 * rev 233, two accounts in one channel) shows every exchange below from both
 * sides, and the rev-242 sessions (4567, 4392) show the same packets now:
 *
 *   join      FRIENDCHAT_JOIN_LEAVE name -> "Attempting to join chat-channel..."
 *             that tick, the answer a tick or two later (753 [7..9], 762
 *             [7..8]): "Now talking in chat-channel <name>", "To talk, start
 *             each line of chat with the / symbol." and the FULL channel; or
 *             one refusal line.
 *   leave     FRIENDCHAT_JOIN_LEAVE with no name -> "You have left the
 *             channel." and the empty FULL, same tick (753 [954]); every other
 *             member gets SINGLEUSER rank -128.
 *   say       MESSAGE_PUBLIC type 2 "/text" -> MESSAGE_FRIENDCHANNEL "Text" to
 *             every member, the slash gone and the first letter capitalised
 *             (236 [131155] "/sup fellas" -> "Sup fellas"; 756 [20]).
 *   kick      FRIENDCHAT_KICK name -> the kicker "Your request to kick/ban this
 *             user was successful.", the kicked "You have been kicked from the
 *             channel." and the empty FULL, everyone else SINGLEUSER -128
 *             (761 [96..98] / 762 [44]). A rejoin is "You are temporarily
 *             banned from this chat-channel." (762 [57]) -- until the channel
 *             empties: the owner left and rejoined and the kicked player got
 *             straight back in (761 [248..258], 762 [203]).
 *   settings  the owner's changes (prefix, who can enter / talk / kick, a
 *             friend's rank) reach the live channel 100 ticks after the first
 *             of them: "Changes will take effect on your friends channel in the
 *             next 60 seconds." opens the window, later changes inside it say
 *             nothing (753 [25]..[75], [146]->[247], [375]->[475]; 769 [74],
 *             [95], [222]->[323]). Enabling a disabled channel is immediate
 *             (753 [753]); disabling closes it at the flush, the owner told
 *             "Your chat-channel has now been disabled!" and every member
 *             "You have been removed from this channel." a tick later (753
 *             [475..476], 752 [1288..1289]).
 *
 * Like the friend service (torirs_server_friends.h) this is name-keyed and
 * process-scoped: a channel outlives its owner's session (Jagex's friends chat
 * runs with the owner offline), and the owner's settings outlive it too. The
 * settings and the channel a player was in at logout are also written to the
 * player's save (`[friendchat]`), which the friend lists are not.
 *
 * This module decides and sends packets; it says nothing itself. Every
 * sentence is content's (CONTENT_ARCHITECTURE.md §8.2(a)): each notice is a
 * `[proc,friendchat_*]` in interface_friends/scripts/friendchat.rs2, run with
 * the player it is for active.
 *
 * Ranks are the wire's: -1 anyone (not a friend of the owner), 0 a friend,
 * 1 Recruit .. 6 General, 7 the owner. A friend's rank lives here, keyed by
 * (owner, friend), and goes out on the friend list's own row
 * (UPDATE_FRIENDLIST `rank`) as well as on the channel's.
 */

struct ToriRSServer;
struct ToriRSServerPlayer;

enum
{
    TORIRSSERVER_FRIENDCHAT_RANK_ANYONE = -1,
    TORIRSSERVER_FRIENDCHAT_RANK_FRIEND = 0,
    TORIRSSERVER_FRIENDCHAT_RANK_GENERAL = 6,
    TORIRSSERVER_FRIENDCHAT_RANK_OWNER = 7,
    /** SINGLEUSER's delete. */
    TORIRSSERVER_FRIENDCHAT_RANK_GONE = -128,
};

/** What a settings change did, for content to word. */
enum ToriRSServerFriendchatChange
{
    /** Nothing new to say: the value was already pending or live, or a window
     *  is already open (the change still lands at its flush). */
    TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET = 0,
    /** A 100-tick window opened: "Changes will take effect ..." */
    TORIRSSERVER_FRIENDCHAT_CHANGE_PENDING = 1,
    /** A disabled channel was enabled, now: "Your chat-channel has now been
     *  enabled!" and how to join it. */
    TORIRSSERVER_FRIENDCHAT_CHANGE_ENABLED = 2,
};

/** Drop every channel, setting and rank. The selftest's, like
 *  ToriRSServer_FriendsReset; the world reset does not call it. */
void
ToriRSServer_FriendchatReset(void);

/* ------------------------------------------------------------------ */
/* Session                                                             */
/* ------------------------------------------------------------------ */

/** After ToriRSServer_FriendsLogin: the owner's settings from the save (when
 *  this process has none of its own yet), the empty channel packet the login
 *  burst carries (RSProx index 753 [0]), and the owner's display name for the
 *  channel's FULL. */
void
ToriRSServer_FriendchatLogin(struct ToriRSServerPlayer* player);

/** The channel to rejoin: the owner the player was talking under at logout,
 *  "" for none. Content sends it to `chatchannel_login` (clientscript 3765),
 *  which makes the client join (RSProx index 4567 [7..8]). */
const char*
ToriRSServer_FriendchatRejoinName(const struct ToriRSServerPlayer* player);

/** The channel the player is talking in now: its owner's display name, ""
 *  for none. */
const char*
ToriRSServer_FriendchatCurrent(const struct ToriRSServerPlayer* player);

/** Before the slot goes: out of the channel, the other members told. */
void
ToriRSServer_FriendchatLogout(struct ToriRSServerPlayer* player);

/** A display name changed (Tutorial Island): the owner's settings and ranks
 *  move to the new name, as ToriRSServer_FriendsRename moves the lists. */
void
ToriRSServer_FriendchatRename(
    int64_t old37,
    int64_t new37);

/** Once a tick: answers to joins, settings flushes, closing channels. */
void
ToriRSServer_FriendchatTick(struct ToriRSServer* srv);

/* ------------------------------------------------------------------ */
/* Client packets                                                      */
/* ------------------------------------------------------------------ */

/** FRIENDCHAT_JOIN_LEAVE. `name` NULL or "" is the leave. */
void
ToriRSServer_FriendchatJoinLeave(
    struct ToriRSServerPlayer* player,
    const char* name);

/** FRIENDCHAT_KICK. */
void
ToriRSServer_FriendchatKick(
    struct ToriRSServerPlayer* player,
    const char* name);

/** FRIENDCHAT_SETRANK: the owner ranks a friend (1..6, 0 to clear). */
void
ToriRSServer_FriendchatSetRank(
    struct ToriRSServerPlayer* player,
    const char* name,
    int rank);

/** MESSAGE_PUBLIC type 2: `text` is the line as typed, its leading slash
 *  included. `crown` is the sender's chat crown. */
void
ToriRSServer_FriendchatSay(
    struct ToriRSServerPlayer* player,
    const char* text,
    int crown);

/** A friend left the owner's list: their rank goes with them. */
void
ToriRSServer_FriendchatFriendRemoved(
    int64_t owner37,
    int64_t friend37);

/** The friend's rank in the owner's channel, for UPDATE_FRIENDLIST: 0 for a
 *  friend with none. */
int
ToriRSServer_FriendchatFriendRank(
    int64_t owner37,
    int64_t friend37);

/* ------------------------------------------------------------------ */
/* The owner's settings (content commands, the setup panel)            */
/* ------------------------------------------------------------------ */

/** The settings as the owner last set them (pending ones included). */
int
ToriRSServer_FriendchatEnabled(const struct ToriRSServerPlayer* owner);
int
ToriRSServer_FriendchatEnterRank(const struct ToriRSServerPlayer* owner);
int
ToriRSServer_FriendchatTalkRank(const struct ToriRSServerPlayer* owner);
int
ToriRSServer_FriendchatKickRank(const struct ToriRSServerPlayer* owner);
/** The prefix as the panel shows it ("Test Fc Name": base 37, each word
 *  capitalised; RSProx index 745 [68]). "" when none was ever set. */
const char*
ToriRSServer_FriendchatPrefix(const struct ToriRSServerPlayer* owner);

enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatSetEnterRank(
    struct ToriRSServerPlayer* owner,
    int rank);
enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatSetTalkRank(
    struct ToriRSServerPlayer* owner,
    int rank);
enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatSetKickRank(
    struct ToriRSServerPlayer* owner,
    int rank);
/** Set the prefix (base 37: the first 12 characters) and enable the channel. */
enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatSetPrefix(
    struct ToriRSServerPlayer* owner,
    const char* prefix);
enum ToriRSServerFriendchatChange
ToriRSServer_FriendchatDisable(struct ToriRSServerPlayer* owner);

/* ------------------------------------------------------------------ */
/* Save                                                                */
/* ------------------------------------------------------------------ */

/** What `[friendchat]` holds for this player: their channel's settings and
 *  the channel they are in (its owner's display name, "" for none). 0 when
 *  there is nothing to write -- no settings and no channel. */
int
ToriRSServer_FriendchatSaveFields(
    const struct ToriRSServerPlayer* player,
    int* out_enabled,
    int64_t* out_prefix37,
    int* out_enter,
    int* out_talk,
    int* out_kick,
    char* out_channel,
    int channel_size);

#endif
