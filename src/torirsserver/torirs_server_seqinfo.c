/*
 * Sequence names and priorities from the cache.
 *
 * Two questions, both about a sequence and neither about a *game rule*: what id
 * does this name have, and what priority does that id declare. Content names an
 * animation, this file resolves it, and the resolution is exact — the name has
 * to exist.
 *
 * There used to be a third thing here: a convention that built a sequence name
 * out of an npc's display name (`"Goblin"` -> `goblin_attack`) so the engine
 * could pick an animation for an npc content had not described. It is gone, and
 * the reasoning is worth keeping because it looked like a good idea for a long
 * time. It made the engine name content, which is the arrangement this server is
 * organised against; and it was wrong for most of the roster while being right
 * for the monsters anyone would spot-check, since the cache has no `man_attack`,
 * `woman_attack`, `guard_attack` or `duck_attack`. Every human swung with no
 * animation and nothing said so. What an undescribed npc gets now is the
 * content tree's own `[default]` block.
 *
 * A wrong sequence id is invisible — the client either plays nothing or plays
 * something unrelated, and neither looks like an error — which is the whole
 * reason resolution belongs somewhere a test can reach it.
 */

#include "torirs_server.h"
#include <assert.h>

#include "torirs_server_content.h"

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

/*
 * Animation priority, by sequence id.
 *
 * Indexed rather than searched because the gate runs on every swing, every
 * flinch and every emote — a linear scan over 14,000 records per animation is
 * the kind of cost that only shows up under load.
 *
 * `TORIRSSERVER_SEQ_PRIORITY_DEFAULT` is the reference client's own default for a
 * record that omits opcode 5, and it is also what an id past the end of the
 * table reports. That second case matters: a sequence named out of `seq.pack`
 * but absent from the cache would otherwise report 0 and lose every comparison,
 * which is a silent "this animation never plays" rather than an error.
 */
#define TORIRSSERVER_SEQ_PRIORITY_DEFAULT 5

static uint8_t* g_seq_priority;
static int g_seq_priority_count;

int
ToriRSServer_SeqInfoLoad(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int loaded = 0;

    ToriRSServer_SeqInfoFree();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = TORIRSSERVER_CACHE_REVISION;

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

    /* Sized by the highest id present, not by the file count: the table is
     * indexed by sequence id and the ids are sparse. */
    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] >= g_seq_priority_count )
            g_seq_priority_count = archive->file_ids[i] + 1;
    }
    g_seq_priority = (uint8_t*)malloc((size_t)g_seq_priority_count);
    assert(g_seq_priority);
    memset(g_seq_priority, TORIRSSERVER_SEQ_PRIORITY_DEFAULT, (size_t)g_seq_priority_count);

    g_seqs = (struct SeqName*)calloc((size_t)archive->file_count, sizeof(*g_seqs));
    assert(g_seqs);
    for( int i = 0; i < archive->file_count; i++ )
    {
        struct RSCache_Dat2ConfigSequence* seq =
            RSCache_Dat2ConfigSequenceNewDecodeProfile(
                &profile, files->files[i], files->file_sizes[i]);

        if( !seq )
            continue;
        if( getenv("TORIRSSERVER_SEQ_DUMP") )
        {
            int want = atoi(getenv("TORIRSSERVER_SEQ_DUMP"));
            int id = archive->file_ids[i];

            if( want > 0 && id >= want && id <= want + 9 )
            {
                int total = 0;
                for( int f = 0; f < seq->frame_count; f++ )
                    total += seq->frame_lengths ? seq->frame_lengths[f] : 0;
                fprintf(stderr,
                        "seqdump %d frames=%d cycles=%d ticks=%d loops=%d name=%s\n",
                        id, seq->frame_count, total, total / 30, seq->max_loops,
                        seq->debug_name ? seq->debug_name : "-");
            }
        }
        if( seq->debug_name && seq->debug_name[0] )
        {
            g_seqs[loaded].name = strdup(seq->debug_name);
            g_seqs[loaded].id = archive->file_ids[i];
            loaded++;
        }
        /* The decoder zero-initialises, so a record that omits opcode 5
         * arrives as 0 rather than as the reference's default of 5. Taking
         * that number literally would make every un-prioritised animation
         * lose to every other one — the gate would then be a fancier way of
         * saying "the first animation of the tick wins". */
        if( archive->file_ids[i] < g_seq_priority_count && seq->forced_priority > 0 )
            g_seq_priority[archive->file_ids[i]] = (uint8_t)seq->forced_priority;
        RSCache_Dat2ConfigSequenceFree(seq);
    }
    g_seq_count = loaded;

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);

    /* OldSchool stopped writing debug names into sequence records, and rev 239
     * has none at all. That is not an error: pack/seq.pack names all 14,413 of
     * them, and ToriRSServer_SeqByName falls through to it. */
    fprintf(stderr, "torirsserver: sequence debug names loaded (%d named%s, %d priorities)\n", loaded,
            loaded ? "" : " — falling back to pack/seq.pack", g_seq_priority_count);
    return loaded;
}

void
ToriRSServer_SeqInfoFree(void)
{
    if( g_seqs )
    {
        for( int i = 0; i < g_seq_count; i++ )
            free(g_seqs[i].name);
        free(g_seqs);
    }
    g_seqs = NULL;
    g_seq_count = 0;
    free(g_seq_priority);
    g_seq_priority = NULL;
    g_seq_priority_count = 0;
}

int
ToriRSServer_SeqPriority(int seq_id)
{
    if( seq_id < 0 || seq_id >= g_seq_priority_count || !g_seq_priority )
        return TORIRSSERVER_SEQ_PRIORITY_DEFAULT;
    return g_seq_priority[seq_id];
}

int
ToriRSServer_SeqPriorityKnown(int seq_id)
{
    return seq_id >= 0 && seq_id < g_seq_priority_count && g_seq_priority != NULL;
}

/**
 * The id of a named sequence.
 *
 * Two sources, in order. A sequence record may carry a `debug_name`, which is the
 * cache naming itself and needs nothing else loaded — but OldSchool stopped
 * writing them, and rev 239 has none at all. The content tree's `pack/seq.pack`
 * is the other: 14,413 names, taken from the cache's own gameval index when it
 * was unpacked, and the same names the scripts are written against.
 *
 * Trying the record first keeps a cache that still has debug names working
 * without a content tree beside it.
 */
int
ToriRSServer_SeqByName(const char* name)
{
    assert(name);
    for( int i = 0; i < g_seq_count; i++ )
    {
        if( strcmp(g_seqs[i].name, name) == 0 )
            return g_seqs[i].id;
    }
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_SEQ, name);
}
