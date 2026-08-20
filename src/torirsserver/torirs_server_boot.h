#ifndef SRC_NET_MOCK_MOCK230_BOOT_H
#define SRC_NET_MOCK_MOCK230_BOOT_H

/*
 * Bringing the server's static data up, in the one order that works.
 *
 * This exists because the order is load-bearing and was previously spelled out
 * only as a comment in main(). Three examples, all of which fail silently
 * rather than loudly when reversed:
 *
 *   - the content tree seeds combat bonuses from params the *cache* loaders
 *     decoded, so loading content first yields npcs with no bonuses — which
 *     reads as a combat-formula bug, not a load-order one;
 *   - mock230_ids_resolve looks names up in packs the content load installed,
 *     so running it first leaves every engine-addressed id at -1;
 *   - the bank resolves its container by an id those two steps produced.
 *
 * One caller got this right by construction. A second caller — an in-process
 * host — would have had to get it right by copying, which is how load orders
 * drift. So it is a function, and there is exactly one of it.
 */

struct Mock230BootConfig
{
    /** Cache the config decoders and the scene read from. */
    const char* cache_dir;
    /** The LostCity-shaped content tree. */
    const char* content_dir;
    /** Compiled script pack. Defaults to <content_dir>/server/scripts/build. */
    const char* script_dir;
    /** Tile a session logs in on, and respawns at. */
    int home_x;
    int home_z;
};

/**
 * Defaults, with the MOCK230_* environment overrides applied.
 *
 * `cache_dir` / `content_dir` / `script_dir` point into static storage owned by
 * this module, so the config may be copied but must not outlive the process.
 */
void
mock230_boot_defaults(struct Mock230BootConfig* config);

/**
 * Run every loader, in order. Returns the number of content errors — non-zero
 * means the server will address something that does not exist, which is not
 * fatal (running with no content tree is a supported mode) but is never what
 * anyone wanted.
 */
int
mock230_boot_load(const struct Mock230BootConfig* config);

/** Release everything mock230_boot_load allocated. */
void
mock230_boot_free(void);

/** The origin zone for a home tile. Derived rather than configured — two
 *  numbers that have to agree are one number. */
static inline int
mock230_boot_zone(int home_tile)
{
    return home_tile >> 3;
}

#endif
