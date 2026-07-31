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
 * Prayer is content, table and rules both:
 * `skill_prayer/configs/prayers.prayer` states each prayer's level, drain rate,
 * exclusion group, varbit, button and overhead icon, and
 * `skill_prayer/scripts/prayer.rs2` owns the toggle, the checks, the drain and
 * every message. The prayer book's own interface id is in mock230_ids.
 *
 * **The cache varbits are the state.** There is no server-side mask any more —
 * there used to be (`player->prayer_active`), mirrored into the varbits so the
 * book would light up, and having two copies with the C one authoritative is
 * what kept the rules in C: whatever a script wrote, the next mirror overwrote.
 *
 * A prayer is identified by its index in prayers.prayer, which is only a
 * position in that table now — not a bit in anything.
 */

/** Deliver a click to a prayer's button, which is the only way to toggle one:
 *  the switch itself is `[if_button,prayerbook:prayerN]` in content. Returns 1
 *  when content claimed it. */
int
mock230_prayer_click(
    struct Mock230Server* srv,
    int prayer);

/** 1 when this prayer's varbit is set. */
int
mock230_prayer_active(
    struct Mock230Server* srv,
    int prayer);

/** Drop every active prayer — death, and running out of points. Performed by
 *  `[proc,prayer_deactivate_all]`; the engine only names the moment. */
void
mock230_prayer_clear(struct Mock230Server* srv);

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
