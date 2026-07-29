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

/*
 * The prayers themselves are content: `skill_prayer/configs/prayers.prayer`
 * for the table and `prayers.constant` for the overhead icon indices, which
 * are read through mock230_content_prayer and mock230_prayer_headicon. The
 * prayer book's own id is in mock230_ids.
 *
 * A prayer is identified here by its index in that file, which is the bit it
 * occupies in `player->prayer_active` — see MOCK230_PRAYER_MAX for what bounds
 * that.
 */

/** Enable each prayer button's op 1 so a click reaches the server. Part of the
 *  login burst; see mock230_equipment_arm_worn_tab for why. */
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

/** Turn one prayer on or off by index, applying the level requirement and the
 *  conflict groups. Returns 1 if anything changed. */
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

/** 1 when a prayer drawing this overhead icon is up. */
int
mock230_prayer_protecting(
    const struct Mock230Player* player,
    int headicon);

/**
 * An overhead icon index by its `^headicon_prayer_*` constant, or -1.
 *
 * The caller names the icon rather than the prayer on purpose: what the combat
 * code cares about is "is anything protecting from melee up", and the icon is
 * exactly that question — one icon, however many prayers might draw it.
 */
int
mock230_prayer_headicon(const char* symbol);

/** Prayer name, for chat messages. */
const char*
mock230_prayer_name(int prayer);

/** A prayer's index by its `[symbol]` in prayers.prayer, or -1. For callers
 *  that mean one particular prayer rather than "whichever the player clicked". */
int
mock230_prayer_index(const char* symbol);

#endif
