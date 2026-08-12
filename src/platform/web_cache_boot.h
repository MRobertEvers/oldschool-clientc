#ifndef SRC_PLATFORM_WEB_CACHE_BOOT_H
#define SRC_PLATFORM_WEB_CACHE_BOOT_H

/*
 * The browser's pre-main() JS5 metadata barrier. The rationale — why the loop
 * has to be inverted so the page drives it — is at the top of web_cache_boot.c.
 *
 * The page reaches the barrier through the exported torirs_web_cache_prime_*
 * functions; C reaches its results through the three below.
 */

struct Dat2WebStore;

/** The record store the prime opened, or NULL if it never ran. Borrowed: the
 *  store outlives the primer deliberately, so App_Init can open its own dat2
 *  facade over the same records. */
struct Dat2WebStore*
WebCacheBoot_Store(void);

/** The cache directory the manifest named, or NULL. This is the store's key. */
const char*
WebCacheBoot_CacheDir(void);

/** Non-zero once every reference table is installed. Nothing may open the
 *  cache before this is true — App_Init decodes reference tables itself and a
 *  missing one is not survivable there. */
int
WebCacheBoot_Ready(void);

/*
 * The endpoint and revision the prime actually used.
 *
 * The attached producer reads them from here rather than re-deriving them from
 * argv, so the connection that is known to work is the connection the client
 * keeps. Two paths resolving "which JS5 server" independently is how a boot
 * ends up priming against one endpoint and then failing every group read
 * against another, with both halves looking correct in isolation.
 */
const char*
WebCacheBoot_Js5Host(void);

int
WebCacheBoot_Js5Port(void);

int
WebCacheBoot_Js5Revision(void);

#endif
