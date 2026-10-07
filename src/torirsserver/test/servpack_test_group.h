#ifndef TORIRSSERVER_TEST_SERVPACK_TEST_GROUP_H
#define TORIRSSERVER_TEST_SERVPACK_TEST_GROUP_H

/*
 * The audit tests' view of a config group, from the server pack.
 *
 * Each audit decodes the records itself and holds the server's tables to them.
 * The server reads the server pack's client records (the tree merged by
 * cachepack), so that is what the audit decodes too -- the pristine cache would
 * call every authored overlay a server bug. The group is presented as the
 * archive + file list the audits already walk, so only the open changes.
 */

#include "../torirs_server_servpack.h"

#include <assert.h>
#include <rscache.h>
#include <stdlib.h>
#include <string.h>

/** Set by each test's main: the pack the server booted from. */
static char g_test_pack_dir[1024];

/** Config kind `kind`'s records as an archive (ids) and a file list (bytes).
 *  Free both with the library's own RSCache_FileListFree / ...ArchiveFree. */
static struct RSCache_Dat2DiskArchive*
test_pack_group(
    int kind,
    struct RSCache_FileList** out_files)
{
    struct RSCache_ServerPack pack;
    struct ToriRSServerKindRecords records;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;

    assert(out_files);
    *out_files = NULL;
    if( !ToriRSServer_ServPackOpen(&pack, g_test_pack_dir) )
        return NULL;
    if( !ToriRSServer_ServPackKindLoad(&pack, kind, &records) )
    {
        RSCache_ServerPackClose(&pack);
        return NULL;
    }
    RSCache_ServerPackClose(&pack);
    archive = calloc(1, sizeof(*archive));
    files = calloc(1, sizeof(*files));
    assert(archive);
    assert(files);
    archive->file_count = records.count;
    archive->file_ids = malloc(((size_t)records.count + 1) * sizeof(int));
    files->file_count = records.count;
    files->files = malloc(((size_t)records.count + 1) * sizeof(char*));
    files->file_sizes = malloc(((size_t)records.count + 1) * sizeof(int));
    assert(archive->file_ids);
    assert(files->files);
    assert(files->file_sizes);
    for( int i = 0; i < records.count; i++ )
    {
        archive->file_ids[i] = records.ids[i];
        files->files[i] = (char*)records.files[i];
        files->file_sizes[i] = (int)records.sizes[i];
        records.files[i] = NULL; /* now the file list's */
    }
    ToriRSServer_ServPackKindFree(&records);
    *out_files = files;
    return archive;
}

#endif
