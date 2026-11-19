#ifndef SRC_GAME_RS_SESSION_INFO_H
#define SRC_GAME_RS_SESSION_INFO_H

/*
 * What the server tells the client about this session once or rarely, kept
 * as the server last said it: the LAST_LOGIN_INFO the welcome screen rows are
 * read from, the UPDATE_REBOOT_TIMER countdown, the UNKNOWN_STRING,
 * SITE_SETTINGS and SET_WEBSITE_TOKEN strings, whether ENABLE_TRACKING is in
 * force, and whether SET_MULTIWAY last said the player is in a multi-combat
 * zone.
 *
 * Most of it is plain storage: a packet writes a field and a reader reads it.
 * The one thing it decides is the reboot countdown. The packet's ticks are
 * stored as client cycles, the countdown is stepped once per cycle and stops
 * at 1 rather than reaching 0, because 0 means "no update pending" and
 * counting into it would erase the warning at the moment it matters most. The
 * line the reboot_timer widget shows is formatted from it here.
 *
 * This does not know how many client cycles a server tick or a second is,
 * when a cycle has passed, or which screen is open. The caller passes the
 * cycle counts, steps the countdown, and decides what a change redraws.
 */

#include <stdbool.h>

/** LAST_LOGIN_INFO, for the welcome screen clientcode rows. */
struct RS_SessionWelcome
{
    int last_ip;
    int days_since_login;
    /**
     * Days since the recovery questions were last changed, with two values
     * that are not day counts at all: 200 = never set any, 201 = the server is
     * not telling (which is also the only state in which `member_warning` has
     * anything to say). Reference daysSinceRecoveriesChanged.
     */
    int days_since_recovery;
    int unread_messages;
    /** 1 = a member logged into a non-members world; the recovery rows carry
     *  that warning instead of recovery text. */
    int member_warning;
};

struct RS_SessionInfo
{
    struct RS_SessionWelcome welcome;
    /**
     * UPDATE_REBOOT_TIMER countdown in client cycles (reference rebootTimer),
     * NOT in the server ticks the packet carries. 0 = no update pending.
     */
    int reboot_timer;
    /** Formatted countdown line handed to the reboot_timer widget; rebuilt on
     *  every read, so it is only valid until the next one. */
    char reboot_timer_text[48];
    /* UNKNOWN_STRING (deob client.field772) and SITE_SETTINGS (the string the
     * client hands its site-settings store): kept as the client keeps them. */
    char unknown_string[256];
    char site_settings[1024];
    char website_token[256]; /* SET_WEBSITE_TOKEN */
    int tracking_enabled;    /* ENABLE/FINISH_TRACKING */
    /** SET_MULTIWAY: 1 = in a multi-combat zone, stored as the wire says it. */
    int multiway;
};

/** Nothing said yet: no welcome, no update pending, empty strings, no
 *  tracking, multiway 0. */
void
RS_SessionInfo_Reset(struct RS_SessionInfo* info);

/** UPDATE_REBOOT_TIMER: `ticks` server ticks of `cycles_per_tick` client
 *  cycles each. 0 ticks cancels the countdown. */
void
RS_SessionInfo_SetRebootTimer(
    struct RS_SessionInfo* info,
    int ticks,
    int cycles_per_tick);

/**
 * One client cycle passed (reference gameLoop: `if (rebootTimer > 1)
 * rebootTimer--`). Returns true when the countdown moved, which is when the
 * line on screen may have changed.
 */
bool
RS_SessionInfo_StepRebootTimer(struct RS_SessionInfo* info);

/**
 * The system-update line, or NULL when no update is pending.
 *
 * Reference drawScene: seconds = rebootTimer / cycles_per_second, then
 * minutes:seconds zero-padded. The returned pointer is `info`'s own buffer and
 * lives until the next call.
 */
char const*
RS_SessionInfo_RebootTimerText(
    struct RS_SessionInfo* info,
    int cycles_per_second);

/** SET_MULTIWAY. Returns true when the stored value changed, which is when
 *  the multi-combat icon may have to be redrawn. */
bool
RS_SessionInfo_SetMultiway(
    struct RS_SessionInfo* info,
    int multiway);

/** The SET_MULTIWAY value last stored, 0 before any. */
int
RS_SessionInfo_Multiway(struct RS_SessionInfo const* info);

#endif /* SRC_GAME_RS_SESSION_INFO_H */
