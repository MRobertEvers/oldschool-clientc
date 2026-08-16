#ifndef SRC_NET_MOCK_MOCK230_SAVE_H
#define SRC_NET_MOCK_MOCK230_SAVE_H

/*
 * Player persistence, as an ini file per player.
 *
 * The player pool is what made this possible: `struct Mock230Player` used to be
 * a field inside the server struct, tangled with the connection and the world,
 * and *what is saveable is exactly that struct*. Splitting it out and saving it
 * are the same piece of work seen from two sides.
 *
 * Ini rather than a binary blob, and the reason is that a save file is content
 * a human has to be able to read and fix. A corrupt binary save is a bug report
 * with no evidence in it; a text one shows you the player standing inside a wall
 * with 3 hitpoints, and you can edit the line. It also means a save survives a
 * struct change: an unknown key is skipped and a missing one keeps its default,
 * so adding a field does not invalidate every existing save.
 *
 *   saves/<name>.ini            default; MOCK230_SAVES=dir overrides
 *
 * **What persists is content's decision, not the engine's.** A varp is written
 * only when its `.varp` config says `scope=perm` — LostCity's own rule, and the
 * field `Mock230VarpDef.scope_perm` has been carried and unread since the
 * config reader was written. Anything undeclared is server bookkeeping and is
 * deliberately not saved, which is the same default the transmit gate uses.
 */

struct Mock230Player;

/**
 * Where a player's save lives. Returns a pointer to static storage.
 *
 * The name is sanitised to `[a-z0-9_]` — it comes off the login screen, so it
 * is attacker-controlled, and a name of `../../etc/passwd` must not become a
 * path. An empty result means the name was unusable and nothing should be
 * written.
 */
const char*
mock230_save_path(const char* display_name);

/**
 * Write the player. Returns 1 on success.
 *
 * Creates the save directory if it does not exist. Writes to a temporary file
 * and renames, so a crash mid-write leaves the previous save intact rather than
 * a truncated one — which for a save file is the difference between losing a
 * session and losing a character.
 */
int
mock230_save_player(
    const struct Mock230Player* player,
    const char* path);

/**
 * Read the player back. Returns 1 when a save was loaded, 0 when there is none.
 *
 * **Call after mock230_world_init**, which seeds the defaults this overlays: a
 * missing key keeps whatever init put there, so a save written before a field
 * existed still loads. Returns 0 for an absent file, which is a new character
 * rather than an error.
 */
int
mock230_load_player(
    struct Mock230Player* player,
    const char* path);

#endif
