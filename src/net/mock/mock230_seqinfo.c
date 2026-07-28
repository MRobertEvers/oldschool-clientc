/*
 * Sequence names from the cache.
 *
 * Every one of this cache's 12,205 sequences carries a debug name, and the
 * combat ones follow a convention: `<subject>_attack`, `_block`, `_death`,
 * `_ready`. So combat animations are *derivable* rather than guessed —
 * `goblin_attack_unarmed` is 309, `human_unarmedpunch` is 422, and nothing here
 * needs a table of magic numbers that silently rots when the cache changes.
 *
 * That matters because a wrong sequence id is invisible: the client either
 * plays nothing or plays something unrelated, and neither looks like an error.
 */

#include "mock230.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SeqName
{
    char* name;
    int id;
};

static struct SeqName* g_seqs;
static int g_seq_count;

int
mock230_seqinfo_load(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int loaded = 0;

    mock230_seqinfo_free();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = 230;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        char fallback[512];

        snprintf(fallback, sizeof(fallback), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(fallback);
    }
    if( !disk )
        return 0;

    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_SEQUENCE);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_SEQUENCE, archive->revision);

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    g_seqs = (struct SeqName*)calloc((size_t)archive->file_count, sizeof(*g_seqs));
    if( g_seqs )
    {
        for( int i = 0; i < archive->file_count; i++ )
        {
            struct RSCache_Dat2ConfigSequence* seq =
                RSCache_Dat2ConfigSequenceNewDecodeProfile(
                    &profile, files->files[i], files->file_sizes[i]);

            if( !seq )
                continue;
            if( seq->debug_name && seq->debug_name[0] )
            {
                g_seqs[loaded].name = strdup(seq->debug_name);
                g_seqs[loaded].id = archive->file_ids[i];
                loaded++;
            }
            RSCache_Dat2ConfigSequenceFree(seq);
        }
    }
    g_seq_count = loaded;

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);

    fprintf(stderr, "mock230: sequence names loaded (%d named)\n", loaded);
    return loaded;
}

void
mock230_seqinfo_free(void)
{
    if( g_seqs )
    {
        for( int i = 0; i < g_seq_count; i++ )
            free(g_seqs[i].name);
        free(g_seqs);
    }
    g_seqs = NULL;
    g_seq_count = 0;
}

int
mock230_seq_by_name(const char* name)
{
    if( !g_seqs || !name )
        return -1;
    for( int i = 0; i < g_seq_count; i++ )
    {
        if( strcmp(g_seqs[i].name, name) == 0 )
            return g_seqs[i].id;
    }
    return -1;
}

/**
 * Turn an npc's display name into the prefix its sequences use.
 *
 * "Goblin" -> "goblin", "Guard" -> "guard". Spaces and apostrophes become
 * underscores and are dropped respectively, which is the convention the cache
 * follows for multi-word names.
 */
static void
seq_prefix_for(
    const char* display_name,
    char* out,
    size_t capacity)
{
    size_t out_length = 0;

    for( const char* cursor = display_name; *cursor && out_length + 1 < capacity; cursor++ )
    {
        char c = *cursor;

        if( c == '\'' )
            continue;
        if( c == ' ' || c == '-' )
            c = '_';
        else if( c >= 'A' && c <= 'Z' )
            c = (char)(c - 'A' + 'a');
        out[out_length++] = c;
    }
    out[out_length] = '\0';
}

int
mock230_seq_for_npc(
    int npc_type,
    const char* suffix)
{
    char prefix[96];
    char candidate[160];
    int id;

    seq_prefix_for(mock230_npcinfo(npc_type)->name, prefix, sizeof(prefix));
    if( !prefix[0] )
        return -1;

    snprintf(candidate, sizeof(candidate), "%s%s", prefix, suffix);
    id = mock230_seq_by_name(candidate);
    if( id >= 0 )
        return id;

    /* Unarmed variants: the cache spells a goblin's swing
     * `goblin_attack_unarmed`, a hobgoblin's `hobgoblin_attackunarmed`. Both
     * forms appear, so both are tried before giving up. */
    if( strcmp(suffix, "_attack") == 0 )
    {
        snprintf(candidate, sizeof(candidate), "%s_attack_unarmed", prefix);
        id = mock230_seq_by_name(candidate);
        if( id >= 0 )
            return id;
        snprintf(candidate, sizeof(candidate), "%s_attackunarmed", prefix);
        id = mock230_seq_by_name(candidate);
        if( id >= 0 )
            return id;
    }
    return -1;
}
