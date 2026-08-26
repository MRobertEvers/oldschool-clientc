#ifndef SRC_PLATFORM_PLATFORM_X_IO_ONDEMAND_H
#define SRC_PLATFORM_PLATFORM_X_IO_ONDEMAND_H

/*
 * The dat1 "OnDemand" cache source: a LostCity server, instead of a
 * main_file_cache.* on this machine.
 *
 * A 2004-era client has no local cache to speak of. It downloads the nine jag
 * archives from the world's *web* port over HTTP, and streams everything else
 * -- models, animation frames, midi, map squares -- over a second connection
 * to the *game* port that speaks its own tiny protocol. This module is that
 * client, in the shape the IO executor already understands: it answers with
 * the same `struct RSCache_Dat1DiskArchive` the disk layer produces, already
 * gunzipped, so nothing above PlatformX_IO can tell the two apart.
 *
 * ## The two wires
 *
 * HTTP (web port, default 80). `GET /title`, `/config`, `/interface`,
 * `/media`, `/versionlist`, `/textures`, `/wordenc`, `/sounds` each return one
 * jag archive verbatim -- exactly the bytes idx0 file 1..8 holds. `GET /crc`
 * returns the nine login checksums plus a trailing hash.
 *
 * On-demand (game port, default 43594). Send the single byte 15 and read the
 * eight-byte reply; the socket is then a file pipe for the rest of its life
 * (the server moves it to a state where it will never speak game protocol
 * again, so this must be its OWN connection, never the one the client logs in
 * on). A request is four bytes -- archive (0..3), file (u16), priority (0..2)
 * -- and the answer arrives as chunks of a six-byte header (archive, file u16,
 * total length u16, part) followed by up to 500 payload bytes. A total length
 * of zero means the server has no such file.
 *
 * `archive` there is the idx file minus one: 0 = models, 1 = animation frames,
 * 2 = midi, 3 = maps. That is `RSCache_Dat1DiskTable - 1`, and the conversion
 * is done here rather than at the call sites so table ids stay one namespace.
 *
 * ## Blocking, on purpose
 *
 * Every read blocks until its archive is complete. The dat1 half of
 * PlatformX_IO is synchronous -- `load_cache_item_dat1` returns the archive it
 * was asked for, and `PlatformX_IO_Pending` reports nothing outstanding -- and
 * a backend that yielded would need the whole async pending path the dat2/JS5
 * side carries. Against a server on this machine a fetch is well under a
 * millisecond; against a remote one this will stutter, which is the honest
 * symptom of a synchronous cache read over a network and not something to hide
 * behind a partial answer.
 */

#include <stdint.h>

struct PlatformX_IO;
struct RSCache_Dat1DiskArchive;
struct RSCache_MapSquares;

struct PlatformXIOOnDemand;

/*
 * ## Attachment
 *
 * Executor-only, and deliberately declared here rather than in
 * platform_x_io.h -- the same split platform_x_io_js5.h makes for the dat2
 * side, and for the same reason: App and TaskRunner keep seeing one IO
 * interface, and whether the cache was on this machine at startup or is being
 * answered over a socket is not their business.
 *
 * `px` owns the client once enabled and frees it with itself, so the caller
 * holds no handle. Unavailable in a TORIRS_PLATFORM_X_IO_NO_ONDEMAND build --
 * its own flag, separate from the dat2 side's TORIRS_PLATFORM_X_IO_NO_JS5,
 * because io_server wants exactly one of the two: it PROXIES a LostCity cache
 * to the browser, so the dat1 source is the point of it linking this file at
 * all, while the JS5 pump and the SDL2 clock it is paced by are not.
 */

/**
 * Point this IO at a LostCity server for dat1 reads.
 *
 * Refuses when a dat1 disk is already open or a source is already enabled:
 * the two are alternatives for one table space, and a backend holding both
 * would silently prefer one of them.
 *
 * `game_port` 0 means 43594 and `web_port` 0 means 80. Returns 0 on success
 * and -1 when the server cannot be reached -- which is a runtime state (it may
 * not be started yet), so the caller reports rather than asserts.
 */
int
PlatformXIO_Dat1OnDemandEnable(
    struct PlatformX_IO* px,
    const char* host,
    int game_port,
    int web_port);

/**
 * The nine login checksums from the server this IO is reading its cache from.
 *
 * Returns 0 on success, -1 if no on-demand source is enabled or the endpoint
 * could not be read. See PlatformXIOOnDemand_JagChecksums for why asking the
 * cache's own server beats any stated value.
 */
int
PlatformXIO_Dat1OnDemandJagChecksums(
    struct PlatformX_IO* px,
    int32_t out[9]);

/**
 * Raw container bytes for one dat1 cache read, for a caller that will hand
 * them to somebody else's decoder rather than decode them here.
 *
 * This is the proxy's entry point. io_server answers a browser's dat1 read
 * with it: the page has no socket to a LostCity server and no HTTP origin it
 * is allowed to read one from, so the bytes come through this process. What
 * crosses is the container exactly as the server serves it -- the jag file for
 * table 0, the STORED (still compressed) file for tables 1..4 -- because the
 * browser's IndexedDB holds raw containers for dat2 already and one shape for
 * both is what keeps the executor's decode step honest.
 *
 * `flags` is the item's, so a map read is resolved through the versionlist the
 * same way a local one is; see the implementation for why that cannot be left
 * to the caller. `*out_format` receives the enum RSCache_ArchiveFormat those
 * bytes are in, which is the one thing a later decode cannot recover from the
 * bytes themselves.
 *
 * malloc'd, caller frees. NULL when no on-demand source is enabled, when the
 * square is not one this world ships, or when the server does not have it --
 * all three legitimate runtime states rather than contract violations.
 */
uint8_t*
PlatformXIO_Dat1OnDemandContainerFetch(
    struct PlatformX_IO* px,
    int table_id,
    int archive_id,
    int flags,
    int* out_format,
    int* out_size);

/**
 * Open both wires and decode the versionlist's map_index.
 *
 * `web_port` of 0 means 80, `game_port` of 0 means 43594 -- the LostCity
 * defaults, which is what a manifest that names neither is asking for.
 *
 * Returns NULL when the server cannot be reached or answers something that is
 * not a cache; the caller reports it, because "no server" is a legitimate
 * runtime state (it may simply not be started yet) and not a contract
 * violation.
 */
struct PlatformXIOOnDemand*
PlatformXIOOnDemand_New(
    const char* host,
    int game_port,
    int web_port);

void
PlatformXIOOnDemand_Free(struct PlatformXIOOnDemand* od);

/**
 * Region -> map archive table, from the `map_index` file of the versionlist
 * archive. Borrowed; valid until Free. Never NULL on a live handle -- a
 * versionlist that carried no map_index fails construction instead, because
 * every map square would silently resolve to "not shipped".
 */
struct RSCache_MapSquares*
PlatformXIOOnDemand_MapSquares(struct PlatformXIOOnDemand* od);

/**
 * One dat1 archive, in the same representation `RSCache_Dat1DiskArchiveNewLoad`
 * returns: table 0 stays a raw jag file (the multifile decoder above unpacks
 * it), tables 1..4 arrive gzipped on the wire and are decompressed here.
 *
 * Returns NULL when the server does not have the file. Free with
 * RSCache_Dat1DiskArchiveFree.
 */
struct RSCache_Dat1DiskArchive*
PlatformXIOOnDemand_ArchiveLoad(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id);

/**
 * The same fetch as ArchiveLoad, stopping one step earlier.
 *
 * ArchiveLoad decompresses tables 1..4 on the way past, because its caller
 * wants an archive. This one hands back the container as the server serves it,
 * because ITS caller is a proxy and the decode belongs to whoever finally
 * reads the bytes -- doing it here would mean deciding, on this side of a
 * socket, what a client on the other side is going to need.
 *
 * `*out_format` receives the enum RSCache_ArchiveFormat the bytes are in
 * (DAT_MULTIFILE for the table-0 jag archives, DAT for the rest). malloc'd,
 * caller frees. NULL when the server does not have it.
 */
uint8_t*
PlatformXIOOnDemand_ContainerFetch(
    struct PlatformXIOOnDemand* od,
    int table_id,
    int archive_id,
    int* out_format,
    int* out_size);

/**
 * The nine jag checksums the login block must echo, from `GET /crc`.
 *
 * These belong to the server instance, not to the revision: it recomputes them
 * from whatever cache it packed. Reading them from the same server the cache
 * comes from is what makes a manifest's `jag_crc=` line unnecessary -- a
 * stale one is the "client out of date" reply that costs an afternoon.
 *
 * Returns 0 on success, -1 if the endpoint could not be read.
 */
int
PlatformXIOOnDemand_JagChecksums(
    struct PlatformXIOOnDemand* od,
    int32_t out[9]);

#endif
