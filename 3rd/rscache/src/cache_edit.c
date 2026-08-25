#include "cache_edit.h"

#include "archive.h"
#include "checksum.h"
#include "compression.h"
#include "datatypes/dat1_config_seq.h"
#include "datatypes/dat1_version_list.h"
#include "filelist.h"
#include "reference_table.h"
#include "rsbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum RSCache_Dat2EditPutKind
{
    RSCACHE_DAT2_EDIT_PUT_FILE = 0,
    RSCACHE_DAT2_EDIT_PUT_ARCHIVE = 1,
};

struct RSCache_Dat2EditPut
{
    enum RSCache_Dat2EditPutKind kind;
    int table_id;
    int archive_id;
    int file_id;
    uint8_t* data;
    uint32_t size;
};

struct RSCache_Dat2Edit
{
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2EditPut* puts;
    int put_count;
    int put_capacity;
};

struct RSCache_Dat2EditFileEntry
{
    int file_id;
    uint8_t* data;
    uint32_t size;
};

struct RSCache_Dat2EditWriteInfo
{
    int table_id;
    int archive_id;
    int crc;
    int compressed;
    int uncompressed;
    unsigned char whirlpool[64];
    int* file_ids;
    int file_count;
    bool update_children;
};

static uint8_t*
dat2_edit_copy_bytes(
    const uint8_t* data,
    uint32_t size)
{
    if( size == 0 )
        return NULL;
    assert(data != NULL);
    uint8_t* copy = malloc(size);
    if( !copy )
        return NULL;
    memcpy(copy, data, size);
    return copy;
}

static bool
dat2_edit_push_put(
    struct RSCache_Dat2Edit* edit,
    struct RSCache_Dat2EditPut put)
{
    if( edit->put_count >= edit->put_capacity )
    {
        int new_capacity = edit->put_capacity == 0 ? 8 : edit->put_capacity * 2;
        struct RSCache_Dat2EditPut* grown =
            realloc(edit->puts, (size_t)new_capacity * sizeof(*grown));
        if( !grown )
            return false;
        edit->puts = grown;
        edit->put_capacity = new_capacity;
    }
    edit->puts[edit->put_count++] = put;
    return true;
}

static void
dat2_edit_free_puts(struct RSCache_Dat2Edit* edit)
{
    if( !edit || !edit->puts )
        return;
    for( int i = 0; i < edit->put_count; i++ )
        free(edit->puts[i].data);
    free(edit->puts);
    edit->puts = NULL;
    edit->put_count = 0;
    edit->put_capacity = 0;
}

static void
dat2_edit_file_entries_free(
    struct RSCache_Dat2EditFileEntry* entries,
    int count)
{
    if( !entries )
        return;
    for( int i = 0; i < count; i++ )
        free(entries[i].data);
    free(entries);
}

static void
dat2_edit_write_info_free(struct RSCache_Dat2EditWriteInfo* info)
{
    if( !info )
        return;
    free(info->file_ids);
    info->file_ids = NULL;
    info->file_count = 0;
}

/* dat2_edit_find_file_pos lived here: a linear scan of an archive's entries for
 * one file id. The commit applies one put per record into a single archive, and
 * `loc` alone holds 61,413 of them, so the scan made that O(n^2). It is replaced
 * by dat2_edit_file_index below, which answers the same question by hash. */

/*
 * file_id -> position in `entries`.
 *
 * Applying N file puts to one archive used to be O(N^2): every put ran the
 * linear scan above over everything applied so far. One config archive is not a
 * handful of files — `loc` carries 61,413 — so that single archive spent ~1.9e9
 * comparisons, and on a from-scratch pack every one of them was a miss, because
 * there is no pre-existing archive for a file to already be in. It was the
 * larger half of why a bake ran for tens of minutes.
 *
 * Open addressing with linear probing, sized to a power of two. -1 marks an
 * empty slot, which is safe because a dat2 file id is never negative.
 */
#define DAT2_EDIT_INDEX_EMPTY (-1)

struct dat2_edit_file_index
{
    int* keys;
    int* vals;
    int capacity;
    int count;
};

static uint32_t
dat2_edit_index_hash(int key)
{
    /* splitmix-style finaliser: file ids are dense small integers, and the low
     * bits alone would cluster badly under linear probing. */
    uint32_t x = (uint32_t)key;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void
dat2_edit_index_free(struct dat2_edit_file_index* index)
{
    if( !index )
        return;
    free(index->keys);
    free(index->vals);
    index->keys = NULL;
    index->vals = NULL;
    index->capacity = 0;
    index->count = 0;
}

static int
dat2_edit_index_find(const struct dat2_edit_file_index* index, int file_id)
{
    uint32_t mask;
    uint32_t slot;

    if( !index || index->capacity == 0 )
        return -1;

    mask = (uint32_t)index->capacity - 1u;
    slot = dat2_edit_index_hash(file_id) & mask;
    for( ;; )
    {
        if( index->keys[slot] == DAT2_EDIT_INDEX_EMPTY )
            return -1;
        if( index->keys[slot] == file_id )
            return index->vals[slot];
        slot = (slot + 1u) & mask;
    }
}

static bool dat2_edit_index_insert(struct dat2_edit_file_index* index, int file_id, int pos);

static bool
dat2_edit_index_grow(struct dat2_edit_file_index* index, int min_capacity)
{
    struct dat2_edit_file_index grown;
    int capacity = index->capacity ? index->capacity * 2 : 64;

    while( capacity < min_capacity )
        capacity *= 2;

    grown.capacity = capacity;
    grown.count = 0;
    grown.keys = malloc((size_t)capacity * sizeof(*grown.keys));
    grown.vals = malloc((size_t)capacity * sizeof(*grown.vals));
    if( !grown.keys || !grown.vals )
    {
        free(grown.keys);
        free(grown.vals);
        return false;
    }
    for( int i = 0; i < capacity; i++ )
        grown.keys[i] = DAT2_EDIT_INDEX_EMPTY;

    for( int i = 0; i < index->capacity; i++ )
    {
        if( index->keys[i] == DAT2_EDIT_INDEX_EMPTY )
            continue;
        if( !dat2_edit_index_insert(&grown, index->keys[i], index->vals[i]) )
        {
            dat2_edit_index_free(&grown);
            return false;
        }
    }

    dat2_edit_index_free(index);
    *index = grown;
    return true;
}

static bool
dat2_edit_index_insert(struct dat2_edit_file_index* index, int file_id, int pos)
{
    uint32_t mask;
    uint32_t slot;

    assert(file_id >= 0);

    /* Keep the load factor under ~0.7; linear probing degrades sharply past it. */
    if( index->capacity == 0 || (index->count + 1) * 10 >= index->capacity * 7 )
    {
        if( !dat2_edit_index_grow(index, index->capacity ? index->capacity * 2 : 64) )
            return false;
    }

    mask = (uint32_t)index->capacity - 1u;
    slot = dat2_edit_index_hash(file_id) & mask;
    for( ;; )
    {
        if( index->keys[slot] == DAT2_EDIT_INDEX_EMPTY )
        {
            index->keys[slot] = file_id;
            index->vals[slot] = pos;
            index->count++;
            return true;
        }
        if( index->keys[slot] == file_id )
        {
            index->vals[slot] = pos;
            return true;
        }
        slot = (slot + 1u) & mask;
    }
}

static int
dat2_edit_file_entry_cmp(const void* a, const void* b)
{
    int x = ((const struct RSCache_Dat2EditFileEntry*)a)->file_id;
    int y = ((const struct RSCache_Dat2EditFileEntry*)b)->file_id;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/*
 * Was an insertion sort. That is fine for the handful of files most archives
 * hold and quadratic for the ones that matter: a config archive holds one file
 * per record, and `loc` has 61,413 — about 1.9e9 element moves for that archive
 * alone, on top of the identical cost the lookup scan was already paying.
 *
 * file_ids are unique within an archive (they are the record ids), so an
 * unstable sort is safe here: no two entries compare equal.
 */
static void
dat2_edit_sort_file_entries(
    struct RSCache_Dat2EditFileEntry* entries,
    int count)
{
    if( count < 2 )
        return;
    qsort(entries, (size_t)count, sizeof(*entries), dat2_edit_file_entry_cmp);
}

/* (table, archive) as one sortable key. Both are non-negative table/archive ids,
 * so the pair packs into 64 bits and compares as a plain integer. */
static uint64_t
dat2_edit_archive_key(int table_id, int archive_id)
{
    return ((uint64_t)(uint32_t)table_id << 32) | (uint32_t)archive_id;
}

static int
dat2_edit_archive_key_cmp(const void* a, const void* b)
{
    uint64_t x = *(const uint64_t*)a;
    uint64_t y = *(const uint64_t*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static bool
dat2_edit_archive_key_present(const uint64_t* keys, int count, int table_id, int archive_id)
{
    uint64_t want;

    if( !keys || count <= 0 )
        return false;
    want = dat2_edit_archive_key(table_id, archive_id);
    return bsearch(&want, keys, (size_t)count, sizeof(*keys), dat2_edit_archive_key_cmp) != NULL;
}

/* dat2_edit_has_archive_put lived here. It answered the same question by
 * scanning every put, from inside a loop over every put; the sorted-key search
 * above replaces it. */

static bool
dat2_edit_encode_and_write(
    const char* directory,
    int table_id,
    int archive_id,
    const uint8_t* payload,
    uint32_t payload_size,
    struct RSCache_Dat2EditWriteInfo* out_info)
{
    assert(directory != NULL);
    assert(out_info != NULL);

    uint32_t bound = RSCache_ArchiveEncodeBound(payload_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP);
    uint8_t* container = malloc(bound ? bound : 1);
    if( !container )
        return false;

    uint32_t container_size = RSCache_ArchiveEncode(
        container,
        bound,
        payload,
        payload_size,
        RSCACHE_ARCHIVE_COMPRESSION_GZIP,
        NULL);
    if( container_size == 0 )
    {
        free(container);
        return false;
    }

    if( RSCache_Dat2DiskWriteArchive(
            directory, table_id, archive_id, container, (int)container_size) != 0 )
    {
        free(container);
        return false;
    }

    out_info->table_id = table_id;
    out_info->archive_id = archive_id;
    out_info->crc = (int)RSCache_Crc32Buffer(container, container_size);
    out_info->compressed = (int)container_size;
    out_info->uncompressed = (int)payload_size;
    RSCache_Whirlpool(container, container_size, out_info->whirlpool);

    free(container);
    return true;
}

static bool
dat2_edit_load_existing_files(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id,
    struct RSCache_Dat2EditFileEntry** out_entries,
    int* out_count)
{
    assert(out_entries && out_count);
    *out_entries = NULL;
    *out_count = 0;

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(disk, table_id, archive_id);
    if( !archive )
        return true;

    if( !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return true;
    }

    struct RSCache_FileList* filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return false;
    }

    struct RSCache_Dat2EditFileEntry* entries =
        calloc((size_t)archive->file_count, sizeof(*entries));
    if( !entries )
    {
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        return false;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        entries[i].file_id = archive->file_ids[i];
        entries[i].size = (uint32_t)filelist->file_sizes[i];
        if( entries[i].size > 0 )
        {
            entries[i].data = dat2_edit_copy_bytes(
                (const uint8_t*)filelist->files[i], entries[i].size);
            if( !entries[i].data )
            {
                dat2_edit_file_entries_free(entries, i);
                RSCache_FileListFree(filelist);
                RSCache_Dat2DiskArchiveFree(archive);
                return false;
            }
        }
    }

    *out_entries = entries;
    *out_count = archive->file_count;
    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);
    return true;
}

static bool
dat2_edit_apply_file_put(
    struct RSCache_Dat2EditFileEntry** entries,
    int* count,
    int* capacity,
    struct dat2_edit_file_index* index,
    int file_id,
    const uint8_t* data,
    uint32_t size)
{
    assert(entries && count && capacity);

    /* Was dat2_edit_find_file_pos over every entry applied so far. Same answer,
     * without the scan — see the note on dat2_edit_file_index. */
    int pos = dat2_edit_index_find(index, file_id);
    uint8_t* copy = dat2_edit_copy_bytes(data, size);
    if( size > 0 && !copy )
        return false;

    if( pos >= 0 )
    {
        free((*entries)[pos].data);
        (*entries)[pos].data = copy;
        (*entries)[pos].size = size;
        return true;
    }

    if( *count >= *capacity )
    {
        int new_capacity = *capacity == 0 ? 8 : *capacity * 2;
        struct RSCache_Dat2EditFileEntry* grown =
            realloc(*entries, (size_t)new_capacity * sizeof(*grown));
        if( !grown )
        {
            free(copy);
            return false;
        }
        *entries = grown;
        *capacity = new_capacity;
    }

    (*entries)[*count].file_id = file_id;
    (*entries)[*count].data = copy;
    (*entries)[*count].size = size;
    if( !dat2_edit_index_insert(index, file_id, *count) )
        return false;
    (*count)++;
    return true;
}

static bool
dat2_edit_commit_file_group(
    struct RSCache_Dat2Edit* edit,
    const char* directory,
    int table_id,
    int archive_id,
    struct RSCache_Dat2EditWriteInfo* out_info)
{
    struct RSCache_Dat2EditFileEntry* entries = NULL;
    int count = 0;
    int capacity = 0;

    if( !dat2_edit_load_existing_files(edit->disk, table_id, archive_id, &entries, &count) )
        return false;
    capacity = count;

    /* Index whatever the archive already held, then keep it current as puts are
     * applied, so each put is a hash lookup rather than a scan of the archive. */
    struct dat2_edit_file_index index = { NULL, NULL, 0, 0 };
    for( int i = 0; i < count; i++ )
    {
        if( !dat2_edit_index_insert(&index, entries[i].file_id, i) )
        {
            dat2_edit_index_free(&index);
            dat2_edit_file_entries_free(entries, count);
            return false;
        }
    }

    for( int i = 0; i < edit->put_count; i++ )
    {
        struct RSCache_Dat2EditPut* put = &edit->puts[i];
        if( put->kind != RSCACHE_DAT2_EDIT_PUT_FILE )
            continue;
        if( put->table_id != table_id || put->archive_id != archive_id )
            continue;
        if( !dat2_edit_apply_file_put(
                &entries, &count, &capacity, &index, put->file_id, put->data, put->size) )
        {
            dat2_edit_index_free(&index);
            dat2_edit_file_entries_free(entries, count);
            return false;
        }
    }
    dat2_edit_index_free(&index);

    if( count <= 0 )
    {
        dat2_edit_file_entries_free(entries, count);
        return false;
    }

    dat2_edit_sort_file_entries(entries, count);

    struct RSCache_FileList filelist = { 0 };
    filelist.file_count = count;
    filelist.files = malloc((size_t)count * sizeof(char*));
    filelist.file_sizes = malloc((size_t)count * sizeof(int));
    int* file_ids = malloc((size_t)count * sizeof(int));
    if( !filelist.files || !filelist.file_sizes || !file_ids )
    {
        free(filelist.files);
        free(filelist.file_sizes);
        free(file_ids);
        dat2_edit_file_entries_free(entries, count);
        return false;
    }

    for( int i = 0; i < count; i++ )
    {
        filelist.files[i] = (char*)entries[i].data;
        filelist.file_sizes[i] = (int)entries[i].size;
        file_ids[i] = entries[i].file_id;
    }

    uint32_t group_bound = RSCache_FileListEncodeBound(&filelist);
    uint8_t* group = malloc(group_bound ? group_bound : 1);
    if( !group )
    {
        free(filelist.files);
        free(filelist.file_sizes);
        free(file_ids);
        dat2_edit_file_entries_free(entries, count);
        return false;
    }

    uint32_t group_size = RSCache_FileListEncode(&filelist, group, group_bound);
    free(filelist.files);
    free(filelist.file_sizes);
    if( group_size == 0 )
    {
        free(group);
        free(file_ids);
        dat2_edit_file_entries_free(entries, count);
        return false;
    }

    memset(out_info, 0, sizeof(*out_info));
    bool ok = dat2_edit_encode_and_write(
        directory, table_id, archive_id, group, group_size, out_info);
    free(group);
    dat2_edit_file_entries_free(entries, count);
    if( !ok )
    {
        free(file_ids);
        return false;
    }

    out_info->file_ids = file_ids;
    out_info->file_count = count;
    out_info->update_children = true;
    return true;
}

static bool
dat2_edit_commit_archive_put(
    struct RSCache_Dat2Edit* edit,
    const char* directory,
    const struct RSCache_Dat2EditPut* put,
    struct RSCache_Dat2EditWriteInfo* out_info)
{
    (void)edit;
    assert(put && put->kind == RSCACHE_DAT2_EDIT_PUT_ARCHIVE);

    memset(out_info, 0, sizeof(*out_info));
    if( !dat2_edit_encode_and_write(
            directory, put->table_id, put->archive_id, put->data, put->size, out_info) )
        return false;

    /* Whole-archive puts replace the decompressed payload. New archives get a
     * single child id 0; existing child lists are refreshed by the caller only
     * when update_children is set (FILE puts). */
    out_info->update_children = false;
    return true;
}

static bool
dat2_edit_ids_contains(
    const int* ids,
    int id_count,
    int archive_id)
{
    for( int i = 0; i < id_count; i++ )
    {
        if( ids[i] == archive_id )
            return true;
    }
    return false;
}

static bool
dat2_edit_reftable_ensure_archive(
    struct RSCache_ReferenceTable* table,
    int archive_id)
{
    assert(table != NULL);
    assert(archive_id >= 0);

    if( archive_id >= table->archive_count )
    {
        int new_count = archive_id + 1;
        struct RSCache_ReferenceTableArchive* grown = realloc(
            table->archives, (size_t)new_count * sizeof(struct RSCache_ReferenceTableArchive));
        if( !grown )
            return false;
        for( int i = table->archive_count; i < new_count; i++ )
        {
            memset(&grown[i], 0, sizeof(grown[i]));
            grown[i].index = -1;
        }
        table->archives = grown;
        table->archive_count = new_count;
    }

    if( table->archives[archive_id].index >= 0 )
    {
        assert(dat2_edit_ids_contains(table->ids, table->id_count, archive_id));
        return true;
    }

    int* grown_ids = realloc(table->ids, (size_t)(table->id_count + 1) * sizeof(int));
    if( !grown_ids )
        return false;
    table->ids = grown_ids;

    int insert_at = table->id_count;
    for( int i = 0; i < table->id_count; i++ )
    {
        if( table->ids[i] > archive_id )
        {
            insert_at = i;
            break;
        }
    }
    for( int i = table->id_count; i > insert_at; i-- )
        table->ids[i] = table->ids[i - 1];
    table->ids[insert_at] = archive_id;
    table->id_count++;

    table->archives[archive_id].index = archive_id;
    return true;
}

static struct RSCache_ReferenceTable*
dat2_edit_ensure_table(
    struct RSCache_Dat2Disk* disk,
    int table_id)
{
    assert(disk != NULL);
    assert(RSCache_Dat2DiskIsValidTableId(table_id));

    if( disk->tables[table_id] )
        return disk->tables[table_id];

    struct RSCache_ReferenceTable* table = calloc(1, sizeof(*table));
    if( !table )
        return NULL;
    table->format = 7;
    table->version = 1;
    table->flags = RSCACHE_REFTABLE_FLAG_SIZES;
    table->ids = NULL;
    table->id_count = 0;
    table->archives = NULL;
    table->archive_count = 0;
    disk->tables[table_id] = table;
    return table;
}

static bool
dat2_edit_set_children(
    struct RSCache_ReferenceTable* table,
    struct RSCache_ReferenceTableArchive* archive,
    const int* file_ids,
    int file_count)
{
    assert(table != NULL);
    assert(archive != NULL);
    assert(file_count >= 0);
    assert(file_count == 0 || file_ids != NULL);

    struct RSCache_ReferenceTableArchiveFile* files = NULL;
    if( file_count > 0 )
    {
        files = calloc((size_t)file_count, sizeof(*files));
        if( !files )
            return false;
        for( int i = 0; i < file_count; i++ )
            files[i].id = file_ids[i];
    }

    /* A decoded archive's children are a slice of the table's pool — replacing
     * them just abandons the slice; only an owned array is freed. */
    if( !RSCache_ReferenceTableChildrenPooled(table, archive->children.files) )
        free(archive->children.files);
    if( !RSCache_ReferenceTableChildNameHashesPooled(table, archive->children.name_hashes) )
        free(archive->children.name_hashes);
    archive->children.files = files;
    /* A child list built from ids alone carries no names, which is what the
     * calloc'd array used to say by leaving every name_hash zero. */
    archive->children.name_hashes = NULL;
    archive->children.count = file_count;
    return true;
}

static bool
dat2_edit_update_reference_entry(
    struct RSCache_ReferenceTable* table,
    const struct RSCache_Dat2EditWriteInfo* info)
{
    assert(table && info);

    if( !dat2_edit_reftable_ensure_archive(table, info->archive_id) )
        return false;

    struct RSCache_ReferenceTableArchive* archive = &table->archives[info->archive_id];
    archive->crc = info->crc;
    archive->version = archive->version <= 0 ? 1 : archive->version + 1;
    if( (table->flags & RSCACHE_REFTABLE_FLAG_SIZES) != 0 )
    {
        archive->compressed = info->compressed;
        archive->uncompressed = info->uncompressed;
    }
    if( (table->flags & RSCACHE_REFTABLE_FLAG_WHIRLPOOL) != 0 )
    {
        memcpy(
            RSCache_ReferenceTableWhirlpoolSlot(table, info->archive_id),
            info->whirlpool,
            RSCACHE_REFTABLE_WHIRLPOOL_BYTES);
    }

    if( info->update_children )
    {
        if( !dat2_edit_set_children(table, archive, info->file_ids, info->file_count) )
            return false;
    }
    else if( archive->children.count == 0 )
    {
        int zero = 0;
        if( !dat2_edit_set_children(table, archive, &zero, 1) )
            return false;
    }

    return true;
}

static bool
dat2_edit_write_reference_table(
    struct RSCache_Dat2Disk* disk,
    const char* directory,
    int table_id)
{
    assert(disk && directory);
    struct RSCache_ReferenceTable* table = disk->tables[table_id];
    assert(table != NULL);

    table->version += 1;

    uint32_t bound = RSCache_ReferenceTableEncodeBound(table);
    uint8_t* raw = malloc(bound ? bound : 1);
    if( !raw )
        return false;

    uint32_t raw_size = RSCache_ReferenceTableEncode(table, raw, bound);
    if( raw_size == 0 )
    {
        free(raw);
        return false;
    }

    uint32_t cbound = RSCache_ArchiveEncodeBound(raw_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP);
    uint8_t* container = malloc(cbound ? cbound : 1);
    if( !container )
    {
        free(raw);
        return false;
    }

    uint32_t container_size = RSCache_ArchiveEncode(
        container, cbound, raw, raw_size, RSCACHE_ARCHIVE_COMPRESSION_GZIP, NULL);
    free(raw);
    if( container_size == 0 )
    {
        free(container);
        return false;
    }

    int rc = RSCache_Dat2DiskWriteArchive(
        directory,
        RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID,
        table_id,
        container,
        (int)container_size);
    free(container);
    return rc == 0;
}

struct RSCache_Dat2Edit*
RSCache_Dat2EditNew(struct RSCache_Dat2Disk* disk)
{
    assert(disk != NULL);

    struct RSCache_Dat2Edit* edit = calloc(1, sizeof(*edit));
    if( !edit )
        return NULL;
    edit->disk = disk;
    return edit;
}

void
RSCache_Dat2EditFree(struct RSCache_Dat2Edit* edit)
{
    if( !edit )
        return;
    dat2_edit_free_puts(edit);
    free(edit);
}

bool
RSCache_Dat2EditPutFile(
    struct RSCache_Dat2Edit* edit,
    int table_id,
    int archive_id,
    int file_id,
    const uint8_t* data,
    uint32_t size)
{
    assert(edit != NULL);
    assert(RSCache_Dat2DiskIsValidTableId(table_id));
    assert(archive_id >= 0);
    assert(file_id >= 0);
    assert(size == 0 || data != NULL);

    struct RSCache_Dat2EditPut put = { 0 };
    put.kind = RSCACHE_DAT2_EDIT_PUT_FILE;
    put.table_id = table_id;
    put.archive_id = archive_id;
    put.file_id = file_id;
    put.size = size;
    put.data = dat2_edit_copy_bytes(data, size);
    if( size > 0 && !put.data )
        return false;

    if( !dat2_edit_push_put(edit, put) )
    {
        free(put.data);
        return false;
    }
    return true;
}

bool
RSCache_Dat2EditPutArchive(
    struct RSCache_Dat2Edit* edit,
    int table_id,
    int archive_id,
    const uint8_t* data,
    uint32_t size)
{
    assert(edit != NULL);
    assert(RSCache_Dat2DiskIsValidTableId(table_id));
    assert(archive_id >= 0);
    assert(size == 0 || data != NULL);

    struct RSCache_Dat2EditPut put = { 0 };
    put.kind = RSCACHE_DAT2_EDIT_PUT_ARCHIVE;
    put.table_id = table_id;
    put.archive_id = archive_id;
    put.file_id = -1;
    put.size = size;
    put.data = dat2_edit_copy_bytes(data, size);
    if( size > 0 && !put.data )
        return false;

    if( !dat2_edit_push_put(edit, put) )
    {
        free(put.data);
        return false;
    }
    return true;
}

bool
RSCache_Dat2EditCommit(
    struct RSCache_Dat2Edit* edit,
    const char* directory)
{
    assert(edit != NULL);
    assert(edit->disk != NULL);
    assert(directory != NULL);

    if( edit->put_count <= 0 )
        return true;

    struct RSCache_Dat2EditWriteInfo* writes = NULL;
    int write_count = 0;
    int write_capacity = 0;
    bool ok = false;

    /*
     * Which (table, archive) pairs have a whole-archive put, answered once.
     *
     * The loop below asks this for every FILE put, and dat2_edit_has_archive_put
     * answers it by scanning every put — so a pack with 83k config puts spent
     * ~7e9 comparisons here alone. Sorting the archive-put keys once and binary
     * searching turns the whole question into O(n log n).
     */
    uint64_t* archive_keys = NULL;
    int archive_key_count = 0;
    for( int i = 0; i < edit->put_count; i++ )
    {
        if( edit->puts[i].kind == RSCACHE_DAT2_EDIT_PUT_ARCHIVE )
            archive_key_count++;
    }
    if( archive_key_count > 0 )
    {
        archive_keys = malloc((size_t)archive_key_count * sizeof(*archive_keys));
        if( !archive_keys )
            return false;
        int k = 0;
        for( int i = 0; i < edit->put_count; i++ )
        {
            if( edit->puts[i].kind != RSCACHE_DAT2_EDIT_PUT_ARCHIVE )
                continue;
            archive_keys[k++] = dat2_edit_archive_key(edit->puts[i].table_id,
                                                      edit->puts[i].archive_id);
        }
        qsort(archive_keys, (size_t)archive_key_count, sizeof(*archive_keys),
              dat2_edit_archive_key_cmp);
    }

    /* FILE puts, grouped by (table_id, archive_id). Skip groups that also have
     * an ARCHIVE put — the whole-archive write wins. */
    for( int i = 0; i < edit->put_count; i++ )
    {
        struct RSCache_Dat2EditPut* put = &edit->puts[i];
        if( put->kind != RSCACHE_DAT2_EDIT_PUT_FILE )
            continue;

        bool already = false;
        for( int j = 0; j < write_count; j++ )
        {
            if( writes[j].table_id == put->table_id && writes[j].archive_id == put->archive_id )
            {
                already = true;
                break;
            }
        }
        if( already )
            continue;
        if( dat2_edit_archive_key_present(archive_keys, archive_key_count, put->table_id,
                                          put->archive_id) )
            continue;

        if( write_count >= write_capacity )
        {
            int new_capacity = write_capacity == 0 ? 8 : write_capacity * 2;
            struct RSCache_Dat2EditWriteInfo* grown =
                realloc(writes, (size_t)new_capacity * sizeof(*grown));
            if( !grown )
                goto done;
            writes = grown;
            write_capacity = new_capacity;
        }

        if( !dat2_edit_commit_file_group(
                edit, directory, put->table_id, put->archive_id, &writes[write_count]) )
            goto done;
        write_count++;
    }

    for( int i = 0; i < edit->put_count; i++ )
    {
        struct RSCache_Dat2EditPut* put = &edit->puts[i];
        if( put->kind != RSCACHE_DAT2_EDIT_PUT_ARCHIVE )
            continue;

        if( write_count >= write_capacity )
        {
            int new_capacity = write_capacity == 0 ? 8 : write_capacity * 2;
            struct RSCache_Dat2EditWriteInfo* grown =
                realloc(writes, (size_t)new_capacity * sizeof(*grown));
            if( !grown )
                goto done;
            writes = grown;
            write_capacity = new_capacity;
        }

        /* Later ARCHIVE puts for the same id replace earlier write records. */
        int replace = -1;
        for( int j = 0; j < write_count; j++ )
        {
            if( writes[j].table_id == put->table_id && writes[j].archive_id == put->archive_id )
            {
                replace = j;
                break;
            }
        }

        struct RSCache_Dat2EditWriteInfo info;
        if( !dat2_edit_commit_archive_put(edit, directory, put, &info) )
            goto done;

        if( replace >= 0 )
        {
            dat2_edit_write_info_free(&writes[replace]);
            writes[replace] = info;
        }
        else
        {
            writes[write_count++] = info;
        }
    }

    bool touched_tables[RSCACHE_DAT2_DISK_TABLE_CAPACITY];
    memset(touched_tables, 0, sizeof(touched_tables));

    for( int i = 0; i < write_count; i++ )
    {
        int table_id = writes[i].table_id;
        struct RSCache_ReferenceTable* table = dat2_edit_ensure_table(edit->disk, table_id);
        if( !table )
            goto done;
        if( !dat2_edit_update_reference_entry(table, &writes[i]) )
            goto done;
        touched_tables[table_id] = true;
    }

    for( int table_id = 0; table_id < RSCACHE_DAT2_DISK_TABLE_CAPACITY; table_id++ )
    {
        if( !touched_tables[table_id] )
            continue;
        if( !dat2_edit_write_reference_table(edit->disk, directory, table_id) )
            goto done;
    }

    dat2_edit_free_puts(edit);
    ok = true;

done:
    for( int i = 0; i < write_count; i++ )
        dat2_edit_write_info_free(&writes[i]);
    free(writes);
    free(archive_keys);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Dat1Edit                                                                   */
/* -------------------------------------------------------------------------- */

enum RSCache_Dat1EditPutKind
{
    RSCACHE_DAT1_EDIT_PUT_NPC = 0,
    RSCACHE_DAT1_EDIT_PUT_SEQ = 1,
    RSCACHE_DAT1_EDIT_PUT_SEQ_LIST = 2,
    RSCACHE_DAT1_EDIT_PUT_MODEL = 3,
    RSCACHE_DAT1_EDIT_PUT_ANIM = 4,
};

struct RSCache_Dat1EditPut
{
    enum RSCache_Dat1EditPutKind kind;
    int id;
    uint8_t* data;
    uint32_t size;
    bool already_gzipped;
};

struct RSCache_Dat1Edit
{
    struct RSCache_Dat1Disk* disk;
    struct RSCache_Dat1EditPut* puts;
    int put_count;
    int put_capacity;
};

static uint8_t*
dat1_edit_copy_bytes(
    const uint8_t* data,
    uint32_t size)
{
    if( size == 0 )
        return NULL;
    assert(data != NULL);
    uint8_t* copy = malloc(size);
    if( !copy )
        return NULL;
    memcpy(copy, data, size);
    return copy;
}

static bool
dat1_edit_push_put(
    struct RSCache_Dat1Edit* edit,
    struct RSCache_Dat1EditPut put)
{
    if( edit->put_count >= edit->put_capacity )
    {
        int new_capacity = edit->put_capacity == 0 ? 8 : edit->put_capacity * 2;
        struct RSCache_Dat1EditPut* grown =
            realloc(edit->puts, (size_t)new_capacity * sizeof(*grown));
        if( !grown )
            return false;
        edit->puts = grown;
        edit->put_capacity = new_capacity;
    }
    edit->puts[edit->put_count++] = put;
    return true;
}

static void
dat1_edit_free_puts(struct RSCache_Dat1Edit* edit)
{
    if( !edit || !edit->puts )
        return;
    for( int i = 0; i < edit->put_count; i++ )
        free(edit->puts[i].data);
    free(edit->puts);
    edit->puts = NULL;
    edit->put_count = 0;
    edit->put_capacity = 0;
}

static bool
dat1_edit_jag_upsert(
    struct RSCache_FileListDat* jag,
    const char* name,
    uint8_t* data,
    int size)
{
    assert(jag && name);
    assert(size == 0 || data != NULL);

    int hash = RSCache_ArchiveNameHashDat(name);
    int idx = RSCache_FileListDatFindFileByName(jag, name);
    if( idx >= 0 )
    {
        free(jag->files[idx]);
        jag->files[idx] = (char*)data;
        jag->file_sizes[idx] = size;
        jag->file_name_hashes[idx] = hash;
        return true;
    }

    int new_count = jag->file_count + 1;
    char** files = realloc(jag->files, (size_t)new_count * sizeof(char*));
    int* sizes = realloc(jag->file_sizes, (size_t)new_count * sizeof(int));
    int* hashes = realloc(jag->file_name_hashes, (size_t)new_count * sizeof(int));
    if( !files || !sizes || !hashes )
    {
        if( files )
            jag->files = files;
        if( sizes )
            jag->file_sizes = sizes;
        if( hashes )
            jag->file_name_hashes = hashes;
        return false;
    }
    jag->files = files;
    jag->file_sizes = sizes;
    jag->file_name_hashes = hashes;
    jag->files[jag->file_count] = (char*)data;
    jag->file_sizes[jag->file_count] = size;
    jag->file_name_hashes[jag->file_count] = hash;
    jag->file_count = new_count;
    return true;
}

static bool
dat1_edit_write_jagfile(
    const char* directory,
    int archive_id,
    struct RSCache_FileListDat* jag)
{
    assert(directory && jag);
    uint32_t bound = RSCache_FileListDatEncodeBound(jag);
    uint8_t* encoded = malloc(bound ? bound : 1);
    if( !encoded )
        return false;
    uint32_t size = RSCache_FileListDatEncode(jag, true, encoded, bound);
    if( size == 0 )
    {
        free(encoded);
        return false;
    }
    int rc = RSCache_Dat1DiskWriteArchive(
        directory, RSCACHE_DAT1_DISK_TABLE_CONFIGS, archive_id, encoded, (int)size);
    free(encoded);
    return rc == 0;
}

static struct RSCache_FileListDat*
dat1_edit_load_jag(
    struct RSCache_Dat1Disk* disk,
    int archive_id)
{
    assert(disk != NULL);
    struct RSCache_Dat1DiskArchive* archive = RSCache_Dat1DiskArchiveNewLoad(
        disk, RSCACHE_DAT1_DISK_TABLE_CONFIGS, archive_id);
    if( !archive )
        return NULL;
    struct RSCache_FileListDat* jag =
        RSCache_FileListDatNewFromDecode(archive->data, archive->data_size);
    RSCache_Dat1DiskArchiveFree(archive);
    return jag;
}

static bool
dat1_edit_ensure_u16(
    uint16_t** arr,
    int* count,
    int id,
    uint16_t fill)
{
    assert(arr && count);
    assert(id >= 0);
    if( id < *count )
        return true;
    int new_count = id + 1;
    uint16_t* grown = realloc(*arr, (size_t)new_count * sizeof(uint16_t));
    if( !grown )
        return false;
    for( int i = *count; i < new_count; i++ )
        grown[i] = fill;
    *arr = grown;
    *count = new_count;
    return true;
}

static bool
dat1_edit_ensure_i32(
    int32_t** arr,
    int* count,
    int id,
    int32_t fill)
{
    assert(arr && count);
    assert(id >= 0);
    if( id < *count )
        return true;
    int new_count = id + 1;
    int32_t* grown = realloc(*arr, (size_t)new_count * sizeof(int32_t));
    if( !grown )
        return false;
    for( int i = *count; i < new_count; i++ )
        grown[i] = fill;
    *arr = grown;
    *count = new_count;
    return true;
}

static bool
dat1_edit_ensure_u8(
    uint8_t** arr,
    int* count,
    int id,
    uint8_t fill)
{
    assert(arr && count);
    assert(id >= 0);
    if( id < *count )
        return true;
    int new_count = id + 1;
    uint8_t* grown = realloc(*arr, (size_t)new_count);
    if( !grown )
        return false;
    for( int i = *count; i < new_count; i++ )
        grown[i] = fill;
    *arr = grown;
    *count = new_count;
    return true;
}

static bool
dat1_edit_gzip_payload(
    const uint8_t* data,
    uint32_t size,
    bool already_gzipped,
    uint8_t** out_bytes,
    uint32_t* out_size)
{
    assert(out_bytes && out_size);
    *out_bytes = NULL;
    *out_size = 0;

    if( already_gzipped )
    {
        *out_bytes = dat1_edit_copy_bytes(data, size);
        if( size > 0 && !*out_bytes )
            return false;
        *out_size = size;
        return true;
    }

    uint32_t bound = RSCache_CompressionGzipCompressBound((int)size);
    uint8_t* packed = malloc(bound ? bound : 1);
    if( !packed )
        return false;
    uint32_t packed_size =
        RSCache_CompressionGzipCompress(packed, (int)bound, data, (int)size);
    if( packed_size == 0 )
    {
        free(packed);
        return false;
    }
    *out_bytes = packed;
    *out_size = packed_size;
    return true;
}

static bool
dat1_edit_splice_indexed(
    struct RSCache_FileListDat* jag,
    const char* dat_name,
    const char* idx_name,
    int entry_id,
    const uint8_t* record,
    uint32_t record_size)
{
    assert(jag && dat_name && idx_name);
    assert(entry_id >= 0);
    assert(record_size == 0 || record != NULL);

    int dat_idx = RSCache_FileListDatFindFileByName(jag, dat_name);
    int idx_idx = RSCache_FileListDatFindFileByName(jag, idx_name);
    if( dat_idx < 0 || idx_idx < 0 )
        return false;

    struct RSCache_FileListDatIndexed* indexed = RSCache_FileListDatIndexedNewFromDecode(
        jag->files[idx_idx],
        jag->file_sizes[idx_idx],
        jag->files[dat_idx],
        jag->file_sizes[dat_idx]);
    if( !indexed )
        return false;

    int count = indexed->offset_count;
    if( entry_id >= count )
        count = entry_id + 1;

    /* Rebuild .dat: leading u16 count (offsets start at 2), then records. */
    size_t total = 2;
    for( int i = 0; i < count; i++ )
    {
        if( i == entry_id )
            total += record_size;
        else if( i < indexed->offset_count )
        {
            int next = (i + 1 < indexed->offset_count) ? indexed->offsets[i + 1]
                                                       : indexed->data_size;
            total += (size_t)(next - indexed->offsets[i]);
        }
        else
            total += 1; /* empty opcode-0 record */
    }

    uint8_t* new_dat = malloc(total ? total : 1);
    int* new_offsets = malloc((size_t)count * sizeof(int));
    if( !new_dat || !new_offsets )
    {
        free(new_dat);
        free(new_offsets);
        RSCache_FileListDatIndexedFree(indexed);
        return false;
    }

    /* Mirror the decoder's base of 2: a leading u16 count, then payloads. */
    new_dat[0] = (uint8_t)((count >> 8) & 0xff);
    new_dat[1] = (uint8_t)(count & 0xff);
    size_t cursor = 2;
    for( int i = 0; i < count; i++ )
    {
        new_offsets[i] = (int)cursor;
        if( i == entry_id )
        {
            if( record_size > 0 )
                memcpy(new_dat + cursor, record, record_size);
            cursor += record_size;
        }
        else if( i < indexed->offset_count )
        {
            int next = (i + 1 < indexed->offset_count) ? indexed->offsets[i + 1]
                                                       : indexed->data_size;
            int len = next - indexed->offsets[i];
            if( len > 0 )
                memcpy(new_dat + cursor, indexed->data + indexed->offsets[i], (size_t)len);
            cursor += (size_t)len;
        }
        else
        {
            new_dat[cursor++] = 0;
        }
    }

    struct RSCache_FileListDatIndexed rebuilt = { 0 };
    rebuilt.data = (char*)new_dat;
    rebuilt.data_size = (int)cursor;
    rebuilt.offsets = new_offsets;
    rebuilt.offset_count = count;

    uint32_t idx_bound = RSCache_FileListDatIndexedEncodeIndexBound(&rebuilt);
    uint8_t* new_idx = malloc(idx_bound ? idx_bound : 1);
    if( !new_idx )
    {
        free(new_dat);
        free(new_offsets);
        RSCache_FileListDatIndexedFree(indexed);
        return false;
    }
    uint32_t idx_size =
        RSCache_FileListDatIndexedEncodeIndex(&rebuilt, new_idx, idx_bound);
    if( idx_size == 0 )
    {
        free(new_dat);
        free(new_offsets);
        free(new_idx);
        RSCache_FileListDatIndexedFree(indexed);
        return false;
    }

    /* Transfer ownership of new_dat into the jag; free old via upsert. */
    if( !dat1_edit_jag_upsert(jag, dat_name, new_dat, (int)cursor) )
    {
        free(new_dat);
        free(new_offsets);
        free(new_idx);
        RSCache_FileListDatIndexedFree(indexed);
        return false;
    }
    free(new_offsets);
    if( !dat1_edit_jag_upsert(jag, idx_name, new_idx, (int)idx_size) )
    {
        free(new_idx);
        RSCache_FileListDatIndexedFree(indexed);
        return false;
    }

    RSCache_FileListDatIndexedFree(indexed);
    return true;
}

static bool
dat1_edit_splice_seq(
    struct RSCache_FileListDat* jag,
    int seq_id,
    const uint8_t* record,
    uint32_t record_size)
{
    assert(jag != NULL);
    assert(seq_id >= 0);
    assert(record_size == 0 || record != NULL);

    int dat_idx = RSCache_FileListDatFindFileByName(jag, "seq.dat");
    if( dat_idx < 0 )
        return false;

    struct RSCache_Dat1ConfigSeqList* list = RSCache_Dat1ConfigSeqListNewDecode(
        jag->files[dat_idx], jag->file_sizes[dat_idx]);
    if( !list )
        return false;

    if( seq_id >= list->seqs_count )
    {
        int new_count = seq_id + 1;
        struct RSCache_Dat1ConfigSeq* grown =
            realloc(list->seqs, (size_t)new_count * sizeof(*grown));
        if( !grown )
        {
            RSCache_Dat1ConfigSeqListFree(list);
            return false;
        }
        for( int i = list->seqs_count; i < new_count; i++ )
            memset(&grown[i], 0, sizeof(grown[i]));
        /* Empty seq defaults match init_seq; encode will emit just terminator. */
        for( int i = list->seqs_count; i < new_count; i++ )
        {
            grown[i].loops = -1;
            grown[i].priority = 5;
            grown[i].replaceheldleft = -1;
            grown[i].replaceheldright = -1;
            grown[i].maxloops = 99;
            grown[i].preanim_move = -1;
            grown[i].postanim_move = -1;
            grown[i].duplicate_behavior = -1;
        }
        list->seqs = grown;
        list->seqs_count = new_count;
    }

    /* Replace the target record by decoding the provided bytes in place. */
    RSCache_Dat1ConfigSeqFreeInplace(&list->seqs[seq_id]);
    RSCache_Dat1ConfigSeqDecodeInplace(
        &list->seqs[seq_id], (char*)record, (int)record_size);

    uint32_t bound = RSCache_Dat1ConfigSeqListEncodeBound(list);
    uint8_t* encoded = malloc(bound ? bound : 1);
    if( !encoded )
    {
        RSCache_Dat1ConfigSeqListFree(list);
        return false;
    }
    uint32_t size = RSCache_Dat1ConfigSeqListEncode(list, encoded, bound);
    RSCache_Dat1ConfigSeqListFree(list);
    if( size == 0 )
    {
        free(encoded);
        return false;
    }
    if( !dat1_edit_jag_upsert(jag, "seq.dat", encoded, (int)size) )
    {
        free(encoded);
        return false;
    }
    return true;
}

struct RSCache_Dat1Edit*
RSCache_Dat1EditNew(struct RSCache_Dat1Disk* disk)
{
    assert(disk != NULL);
    struct RSCache_Dat1Edit* edit = calloc(1, sizeof(*edit));
    if( !edit )
        return NULL;
    edit->disk = disk;
    return edit;
}

void
RSCache_Dat1EditFree(struct RSCache_Dat1Edit* edit)
{
    if( !edit )
        return;
    dat1_edit_free_puts(edit);
    free(edit);
}

bool
RSCache_Dat1EditPutNpc(
    struct RSCache_Dat1Edit* edit,
    int npc_id,
    const uint8_t* record,
    uint32_t size)
{
    assert(edit != NULL);
    assert(npc_id >= 0);
    assert(size == 0 || record != NULL);

    struct RSCache_Dat1EditPut put = { 0 };
    put.kind = RSCACHE_DAT1_EDIT_PUT_NPC;
    put.id = npc_id;
    put.size = size;
    put.data = dat1_edit_copy_bytes(record, size);
    if( size > 0 && !put.data )
        return false;
    if( !dat1_edit_push_put(edit, put) )
    {
        free(put.data);
        return false;
    }
    return true;
}

bool
RSCache_Dat1EditPutSeq(
    struct RSCache_Dat1Edit* edit,
    int seq_id,
    const uint8_t* record,
    uint32_t size)
{
    assert(edit != NULL);
    assert(seq_id >= 0);
    assert(size == 0 || record != NULL);

    struct RSCache_Dat1EditPut put = { 0 };
    put.kind = RSCACHE_DAT1_EDIT_PUT_SEQ;
    put.id = seq_id;
    put.size = size;
    put.data = dat1_edit_copy_bytes(record, size);
    if( size > 0 && !put.data )
        return false;
    if( !dat1_edit_push_put(edit, put) )
    {
        free(put.data);
        return false;
    }
    return true;
}

bool
RSCache_Dat1EditPutSeqList(
    struct RSCache_Dat1Edit* edit,
    const uint8_t* seq_dat,
    uint32_t size)
{
    assert(edit != NULL);
    assert(size == 0 || seq_dat != NULL);

    struct RSCache_Dat1EditPut put = { 0 };
    put.kind = RSCACHE_DAT1_EDIT_PUT_SEQ_LIST;
    put.id = -1;
    put.size = size;
    put.data = dat1_edit_copy_bytes(seq_dat, size);
    if( size > 0 && !put.data )
        return false;
    if( !dat1_edit_push_put(edit, put) )
    {
        free(put.data);
        return false;
    }
    return true;
}

bool
RSCache_Dat1EditPutModel(
    struct RSCache_Dat1Edit* edit,
    int model_id,
    const uint8_t* gzip_or_raw,
    uint32_t size,
    bool already_gzipped)
{
    assert(edit != NULL);
    assert(model_id >= 0);
    assert(size == 0 || gzip_or_raw != NULL);

    struct RSCache_Dat1EditPut put = { 0 };
    put.kind = RSCACHE_DAT1_EDIT_PUT_MODEL;
    put.id = model_id;
    put.size = size;
    put.already_gzipped = already_gzipped;
    put.data = dat1_edit_copy_bytes(gzip_or_raw, size);
    if( size > 0 && !put.data )
        return false;
    if( !dat1_edit_push_put(edit, put) )
    {
        free(put.data);
        return false;
    }
    return true;
}

bool
RSCache_Dat1EditPutAnimArchive(
    struct RSCache_Dat1Edit* edit,
    int archive_id,
    const uint8_t* decompressed_or_gzip,
    uint32_t size,
    bool already_gzipped)
{
    assert(edit != NULL);
    assert(archive_id >= 0);
    assert(size == 0 || decompressed_or_gzip != NULL);

    struct RSCache_Dat1EditPut put = { 0 };
    put.kind = RSCACHE_DAT1_EDIT_PUT_ANIM;
    put.id = archive_id;
    put.size = size;
    put.already_gzipped = already_gzipped;
    put.data = dat1_edit_copy_bytes(decompressed_or_gzip, size);
    if( size > 0 && !put.data )
        return false;
    if( !dat1_edit_push_put(edit, put) )
    {
        free(put.data);
        return false;
    }
    return true;
}

bool
RSCache_Dat1EditCommit(
    struct RSCache_Dat1Edit* edit,
    const char* directory)
{
    assert(edit != NULL);
    assert(edit->disk != NULL);
    assert(directory != NULL);

    if( edit->put_count <= 0 )
        return true;

    bool need_configs = false;
    bool need_version = false;
    for( int i = 0; i < edit->put_count; i++ )
    {
        switch( edit->puts[i].kind )
        {
        case RSCACHE_DAT1_EDIT_PUT_NPC:
        case RSCACHE_DAT1_EDIT_PUT_SEQ:
        case RSCACHE_DAT1_EDIT_PUT_SEQ_LIST:
            need_configs = true;
            break;
        case RSCACHE_DAT1_EDIT_PUT_MODEL:
        case RSCACHE_DAT1_EDIT_PUT_ANIM:
            need_version = true;
            break;
        }
    }

    struct RSCache_FileListDat* configs = NULL;
    struct RSCache_FileListDat* version_jag = NULL;
    struct RSCache_Dat1VersionList* vl = NULL;
    bool ok = false;

    if( need_configs )
    {
        configs = dat1_edit_load_jag(edit->disk, RSCACHE_DAT1_CONFIG_CONFIGS);
        if( !configs )
            goto done;

        for( int i = 0; i < edit->put_count; i++ )
        {
            struct RSCache_Dat1EditPut* put = &edit->puts[i];
            if( put->kind == RSCACHE_DAT1_EDIT_PUT_NPC )
            {
                if( !dat1_edit_splice_indexed(
                        configs, "npc.dat", "npc.idx", put->id, put->data, put->size) )
                    goto done;
            }
            else if( put->kind == RSCACHE_DAT1_EDIT_PUT_SEQ_LIST )
            {
                uint8_t* copy = dat1_edit_copy_bytes(put->data, put->size);
                if( put->size > 0 && !copy )
                    goto done;
                if( !dat1_edit_jag_upsert(configs, "seq.dat", copy, (int)put->size) )
                {
                    free(copy);
                    goto done;
                }
            }
            else if( put->kind == RSCACHE_DAT1_EDIT_PUT_SEQ )
            {
                if( !dat1_edit_splice_seq(configs, put->id, put->data, put->size) )
                    goto done;
            }
        }

        if( !dat1_edit_write_jagfile(directory, RSCACHE_DAT1_CONFIG_CONFIGS, configs) )
            goto done;
    }

    if( need_version )
    {
        version_jag = dat1_edit_load_jag(edit->disk, RSCACHE_DAT1_CONFIG_VERSION_LIST);
        if( !version_jag )
            goto done;
        vl = RSCache_Dat1VersionListNewFromJagfile(version_jag);
        if( !vl )
            goto done;

        for( int i = 0; i < edit->put_count; i++ )
        {
            struct RSCache_Dat1EditPut* put = &edit->puts[i];
            if( put->kind != RSCACHE_DAT1_EDIT_PUT_MODEL &&
                put->kind != RSCACHE_DAT1_EDIT_PUT_ANIM )
                continue;

            uint8_t* packed = NULL;
            uint32_t packed_size = 0;
            if( !dat1_edit_gzip_payload(
                    put->data, put->size, put->already_gzipped, &packed, &packed_size) )
                goto done;

            int table_id = put->kind == RSCACHE_DAT1_EDIT_PUT_MODEL
                ? RSCACHE_DAT1_DISK_TABLE_MODELS
                : RSCACHE_DAT1_DISK_TABLE_ANIMATIONS;

            if( RSCache_Dat1DiskWriteArchive(
                    directory, table_id, put->id, packed, (int)packed_size) != 0 )
            {
                free(packed);
                goto done;
            }

            int32_t crc = (int32_t)RSCache_Crc32Buffer(packed, packed_size);
            free(packed);

            if( put->kind == RSCACHE_DAT1_EDIT_PUT_MODEL )
            {
                if( !dat1_edit_ensure_u16(&vl->model_version, &vl->model_version_count, put->id, 0) )
                    goto done;
                if( !dat1_edit_ensure_i32(&vl->model_crc, &vl->model_crc_count, put->id, 0) )
                    goto done;
                if( !dat1_edit_ensure_u8(&vl->model_index, &vl->model_index_count, put->id, 0) )
                    goto done;
                vl->model_version[put->id] =
                    (uint16_t)(vl->model_version[put->id] == 0 ? 1 : vl->model_version[put->id] + 1);
                vl->model_crc[put->id] = crc;
                /* model_index stores a priority byte; leave existing / zero. */
            }
            else
            {
                if( !dat1_edit_ensure_u16(&vl->anim_version, &vl->anim_version_count, put->id, 0) )
                    goto done;
                if( !dat1_edit_ensure_i32(&vl->anim_crc, &vl->anim_crc_count, put->id, 0) )
                    goto done;
                if( !dat1_edit_ensure_u16(&vl->anim_index, &vl->anim_index_count, put->id, 0) )
                    goto done;
                vl->anim_version[put->id] =
                    (uint16_t)(vl->anim_version[put->id] == 0 ? 1 : vl->anim_version[put->id] + 1);
                vl->anim_crc[put->id] = crc;
                /* anim_index stores a length-ish u16; use packed size clamped. */
                vl->anim_index[put->id] =
                    packed_size > 0xFFFF ? 0xFFFF : (uint16_t)packed_size;
            }
        }

        if( RSCache_Dat1VersionListEncodeIntoJagfile(vl, version_jag) == 0 )
            goto done;
        if( !dat1_edit_write_jagfile(
                directory, RSCACHE_DAT1_CONFIG_VERSION_LIST, version_jag) )
            goto done;
    }

    dat1_edit_free_puts(edit);
    ok = true;

done:
    RSCache_Dat1VersionListFree(vl);
    if( version_jag )
        RSCache_FileListDatFree(version_jag);
    if( configs )
        RSCache_FileListDatFree(configs);
    return ok;
}
