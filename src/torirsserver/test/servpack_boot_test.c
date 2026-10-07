/*
 * The server boots from the server pack alone.
 *
 * Every config record the server holds is the pack's: the client record cachepack
 * merged from configs/ and server/scripts, and its server band. So three things
 * have to hold, and this checks each:
 *
 *   1. A real tree's pack loads, and every table the engine reads is populated
 *      from it (npc, obj, loc, struct, healthbar, db, shops, the client's varp
 *      bound) with zero content errors.
 *   2. No pack at all is a refusal (TORIRSSERVER_BOOT_NO_PACK), not a boot from
 *      config text -- there is no text reader left to fall back to.
 *   3. A pack whose stamp does not match its tree (stale) is a refusal.
 *   4. A fresh-stamped pack with an archive that does not validate is a refusal
 *      too, not a boot with that type quietly empty.
 *
 * Skips (exit 0, and says so) when the tree has no pack yet.
 *
 * Run: make -C src test-torirsserver-servpack-boot
 */

#include "torirs_server.h"
#include "torirs_server_boot.h"
#include "torirs_server_content.h"
#include "torirs_server_db.h"
#include "torirs_server_shop.h"

#include "rscache_serverpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_failures;
static int g_checks;

static void
check(
    int ok,
    const char* what)
{
    g_checks++;
    if( !ok )
    {
        g_failures++;
        printf("servpack-boot: FAIL %s\n", what);
    }
}

static void
write_bytes(
    const char* path,
    const unsigned char* bytes,
    size_t size)
{
    FILE* file = fopen(path, "wb");

    if( !file )
    {
        printf("servpack-boot: FAIL cannot write %s\n", path);
        exit(1);
    }
    if( size )
        fwrite(bytes, 1, size, file);
    fclose(file);
}

int
main(
    int argc,
    char** argv)
{
    const char* content = argc > 1 ? argv[1] : "OSRS-Content/osrs239-content";
    const char* scratch = argc > 2 ? argv[2] : "build/servpack_boot_scratch";
    char path[1024];
    struct stat info;

    snprintf(path, sizeof(path), "%s/server/pack/main_file_cache.dat2", content);
    if( !getenv("TORIRSSERVER_PACK_DIR") && stat(path, &info) != 0 )
    {
        printf("servpack-boot: SKIPPED — no server pack at %s/server/pack (build it with "
               "`cachepack pack --src %s --server-only`)\n",
               content, content);
        return 0;
    }

    /* 1. The real pack. */
    check(ToriRSServer_BootLoadContent(content, NULL) == 0, "the tree's server pack loads");
    check(ToriRSServer_ContentErrorCount() == 0, "with zero content errors");
    check(ToriRSServer_NpcInfoCount() > 0, "npc records come from the pack");
    check(ToriRSServer_ObjInfoCount() > 0, "obj records come from the pack");
    check(ToriRSServer_LocInfoCount() > 0, "loc records come from the pack");
    check(ToriRSServer_StructInfoCount() > 0, "struct records come from the pack");
    check(ToriRSServer_HealthbarInfoCount() > 0, "healthbar records come from the pack");
    check(ToriRSServer_DbTableCount() > 0, "db tables come from the pack");
    check(ToriRSServer_DbTotalRowCount() > 0, "db rows come from the pack");
    check(ToriRSServer_ShopDefCount() > 0, "shop definitions come from the pack's inv bands");
    check(ToriRSServer_VarpClientCount() > 0, "the client's varp bound comes from the pack");
    ToriRSServer_BootFree();

    /* 2. No pack: refused, nothing read in its place. */
    snprintf(path, sizeof(path), "%s/none", scratch);
    setenv("TORIRSSERVER_PACK_DIR", path, 1);
    check(ToriRSServer_BootLoadContent(content, NULL) == TORIRSSERVER_BOOT_NO_PACK,
          "a missing pack refuses to boot");
    ToriRSServer_BootFree();

    /*
     * 3. A damaged pack: an obj client-record archive (group 10 + 64) whose idx
     *    entry points at bytes the dat2 does not have. A stale or truncated pack
     *    looks exactly like this.
     */
    {
        static const unsigned char entry[6] = { 0, 0, 100, 0, 0, 1 };
        char dir[1024];

        snprintf(dir, sizeof(dir), "%s/damaged", scratch);
        mkdir(scratch, 0755);
        mkdir(dir, 0755);
        snprintf(path, sizeof(path), "%s/main_file_cache.dat2", dir);
        write_bytes(path, NULL, 0);
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", dir, 10 + 64);
        write_bytes(path, entry, sizeof(entry));
        setenv("TORIRSSERVER_PACK_DIR", dir, 1);
        {
            struct RSCache_ServerPackStamp stamp;

            memset(&stamp, 0, sizeof(stamp));
            /* 3. Stale: a stamp from some other tree. */
            stamp.fingerprint = RSCache_ServerPackFingerprint(content, NULL, 0) ^ 1;
            check(RSCache_ServerPackStampWrite(dir, &stamp), "a stamp writes");
            check(ToriRSServer_BootLoadContent(content, NULL) == TORIRSSERVER_BOOT_NO_PACK,
                  "a stale pack refuses to boot");
            ToriRSServer_BootFree();

            /* 4. Fresh stamp, damaged archive. */
            stamp.fingerprint = RSCache_ServerPackFingerprint(content, NULL, 0);
            check(RSCache_ServerPackStampWrite(dir, &stamp), "a stamp writes");
            check(ToriRSServer_BootLoadContent(content, NULL) == TORIRSSERVER_BOOT_NO_PACK,
                  "an archive that does not validate refuses to boot");
            ToriRSServer_BootFree();
        }
    }
    unsetenv("TORIRSSERVER_PACK_DIR");

    printf("servpack-boot: %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
