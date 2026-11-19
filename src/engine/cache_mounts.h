#ifndef SRC_ENGINE_CACHE_MOUNTS_H
#define SRC_ENGINE_CACHE_MOUNTS_H

/*
 * The cache this client reads, as it is mounted behind the CacheProvider: the
 * disk a dat1 or dat2 cache is opened from, or the record that dat1 comes off
 * a LostCity server's on-demand stream instead, and the build cache of the
 * same format that the provider is a view over.
 *
 * It decides what is open and what is freed. A disk is opened once, from a
 * directory, and a build cache once; closing frees only what was opened, in
 * two steps so the caller can free the IO that reads for the build caches
 * between them. On an on-demand cache it also holds two once-only answers:
 * whether the login checksums were taken from that server (no stated
 * checksums), which is what makes each login attempt refresh them, and
 * whether the on-demand prefetch passes have been queued, which belong to the
 * session's first loading screen and to no later bake.
 *
 * This does not know the manifest, the platform IO or what a missing cache
 * means to the boot. The caller opens what its configuration names, hands the
 * disk to the IO, says why a failed open stops the boot, enables the
 * on-demand stream and reads its checksums, and frees the IO between
 * CacheMounts_CloseBuildCaches and CacheMounts_CloseDisks.
 */

struct CacheProvider;
struct Dat1BuildCache;
struct Dat2BuildCache;
struct RSCache_Dat1Disk;
struct RSCache_Dat2Disk;

struct CacheMounts
{
    struct RSCache_Dat2Disk* dat2_disk;
    struct RSCache_Dat1Disk* dat1_disk;
    /** 1 when dat1 reads go to a LostCity server instead of dat1_disk. The
     *  client itself belongs to the IO (platform_x_io_ondemand.h); this is
     *  only the record that it was enabled. */
    int on_demand;
    /** 1 when the login checksums were taken from that server's /crc (no
     *  stated checksums): each login attempt then refreshes them, because a
     *  server that repacks while the client sits at the title would otherwise
     *  be sent boot-time sums at every later login. */
    int jag_crc_from_ondemand;
    /** 1 once a bake has queued the on-demand prefetch passes. */
    int dat1_prefetch_queued;
    /** The build cache matching the disk; only the one of the configured
     *  format is ever created. */
    struct Dat2BuildCache* dat2_bc;
    struct Dat1BuildCache* dat1_bc;
};

/** Open the dat1 cache in `directory`. The disk, or NULL when there is no
 *  dat1 cache there; nothing is mounted then. */
struct RSCache_Dat1Disk*
CacheMounts_OpenDat1Disk(
    struct CacheMounts* mounts,
    char const* directory);

/** Open the dat2 cache in `directory`, decoding each table only when it is
 *  first read. The disk, or NULL when there is no dat2 cache there. */
struct RSCache_Dat2Disk*
CacheMounts_OpenDat2Disk(
    struct CacheMounts* mounts,
    char const* directory);

/** dat1 comes from the on-demand stream: no disk is opened. */
void
CacheMounts_SetOnDemand(struct CacheMounts* mounts);

int
CacheMounts_OnDemand(struct CacheMounts const* mounts);

struct RSCache_Dat1Disk*
CacheMounts_Dat1Disk(struct CacheMounts const* mounts);

/** The dat2 disk, or NULL when none was opened. */
struct RSCache_Dat2Disk*
CacheMounts_Dat2Disk(struct CacheMounts const* mounts);

/** At boot: 1 when the login checksums are to come from the on-demand server,
 *  which is when the cache is on demand and no checksums were `stated`; that
 *  answer is kept for every later login. */
int
CacheMounts_ArmJagCrcFromOnDemand(
    struct CacheMounts* mounts,
    int stated);

/** Whether each login attempt refreshes the checksums from that server. */
int
CacheMounts_JagCrcFromOnDemand(struct CacheMounts const* mounts);

/** A bake is queuing its preload: 1 when it should carry the on-demand
 *  prefetch passes, which is only on an on-demand cache and only the first
 *  time any bake asks. */
int
CacheMounts_TakeDat1Prefetch(struct CacheMounts* mounts);

/** Create the dat1 build cache and return the provider that views it. */
struct CacheProvider*
CacheMounts_OpenDat1BuildCache(struct CacheMounts* mounts);

/** Create the dat2 build cache and return the provider that views it. */
struct CacheProvider*
CacheMounts_OpenDat2BuildCache(struct CacheMounts* mounts);

/** Free whichever build cache was created. */
void
CacheMounts_CloseBuildCaches(struct CacheMounts* mounts);

/** Free whichever disk was opened. */
void
CacheMounts_CloseDisks(struct CacheMounts* mounts);

#endif
