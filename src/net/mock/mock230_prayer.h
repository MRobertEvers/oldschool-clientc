#ifndef SRC_NET_MOCK_MOCK230_PRAYER_H
#define SRC_NET_MOCK_MOCK230_PRAYER_H

/*
 * Prayers, and the thing they are visible as: the overhead icon.
 *
 * The icon is not its own packet. It is one byte of the player's appearance
 * block, so turning a protection prayer on is an appearance change like
 * putting on a helmet, and every client that can see the player learns about
 * it through the same PLAYER_INFO they were going to get anyway.
 *
 * See docs/mock230_player_systems.md §4.
 */

#include <stdint.h>

struct Mock230Server;
struct Mock230Player;

enum
{
    /* OldSchool's 29 prayers, in the order interface 541 lists them — which is
     * by level, and which is also the order its 29 buttons (541:9..541:37) are
     * laid out in. Verified: the gameframe's CS2 sets op1="Activate" on exactly
     * those 29 components and no others. */
    MOCK230_PRAYER_COUNT = 29,

    MOCK230_PRAYER_IFACE = 541,
    MOCK230_PRAYER_FIRST_BUTTON = 9,

    /* Headicon sprite indices in the `headicons` pack, which is also the bit
     * position each one occupies in the appearance byte. */
    MOCK230_HEADICON_PROTECT_MELEE = 0,
    MOCK230_HEADICON_PROTECT_MISSILES = 1,
    MOCK230_HEADICON_PROTECT_MAGIC = 2,
    MOCK230_HEADICON_RETRIBUTION = 3,
    MOCK230_HEADICON_SMITE = 4,
    MOCK230_HEADICON_REDEMPTION = 5,
    MOCK230_HEADICON_NONE = -1,
};

/** Enable the 29 prayer buttons' op 1 so a click reaches the server. Part of
 *  the login burst; see mock230_equipment_arm_worn_tab for why. */
void
mock230_prayer_arm_buttons(struct Mock230Server* srv);

/**
 * Handle a click on a prayer button. Returns 1 when the component was one.
 */
int
mock230_prayer_handle_button(
    struct Mock230Server* srv,
    int component,
    int op);

/** Turn one prayer (0..MOCK230_PRAYER_COUNT-1) on or off, applying the level
 *  requirement and the conflict groups. Returns 1 if anything changed. */
int
mock230_prayer_toggle(
    struct Mock230Server* srv,
    int prayer);

/** Drop every active prayer — death, and running out of points. */
void
mock230_prayer_clear(struct Mock230Server* srv);

/** One tick of drain. */
void
mock230_prayer_tick(struct Mock230Server* srv);

/** The appearance byte: a bit per overhead icon, 0 when none is up. */
int
mock230_prayer_headicon_mask(const struct Mock230Player* player);

/** 1 when the named protection prayer is up (MOCK230_HEADICON_PROTECT_*). */
int
mock230_prayer_protecting(
    const struct Mock230Player* player,
    int headicon);

/** Prayer name, for chat messages. */
const char*
mock230_prayer_name(int prayer);

#endif
