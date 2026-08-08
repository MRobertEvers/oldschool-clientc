#include "audio/tools/audio_songload.h"

#include "audio/torirs_audio.h"

#include <stdlib.h>
#include <string.h>

/** Decoded archive for one logical table, or NULL. */
static struct RSCache_Dat2DiskArchive*
archive_of(
    struct RSCache_Dat2Disk* disk,
    int logical_table,
    int archive_id)
{
    int table = RSCache_Dat2DiskTableId(disk, logical_table);

    if( table < 0 || archive_id < 0 )
        return NULL;
    return RSCache_Dat2DiskArchiveNewLoad(disk, table, archive_id);
}

/** Widen a sound-effect render (8-bit unsigned) into the bank's 16-bit form. */
static int16_t*
widen(const struct RSCache_SoundPcm* pcm)
{
    int16_t* out = malloc((size_t)pcm->sample_count * sizeof(int16_t));

    if( !out )
        return NULL;
    for( int i = 0; i < pcm->sample_count; i++ )
        out[i] = (int16_t)(((int)pcm->samples[i] - 128) << 8);
    return out;
}

/** One sample a patch's note references, from whichever table it lives in. */
static bool
load_sample(
    struct AudioSongLoad* load,
    int table,
    int sample_id)
{
    struct RSCache_Dat2DiskArchive* archive;

    if( ToriRS_SoundBank_FindSample(&load->bank, table, sample_id) )
        return true;

    if( table == TORIRS_SOUNDBANK_TABLE_MUSIC )
    {
        struct RSCache_AudioSample* sample;
        bool ok;

        if( !load->setup )
            return false;
        archive = archive_of(load->disk, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, sample_id);
        if( !archive )
            return false;
        sample = RSCache_VorbisSampleNewDecode(load->setup, archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !sample )
            return false;
        ok = ToriRS_SoundBank_AddSample(
                 &load->bank,
                 table,
                 sample_id,
                 sample->samples,
                 sample->sample_count,
                 sample->sample_rate,
                 sample->loop_start,
                 sample->loop_end,
                 sample->loop) != NULL;
        if( ok )
            sample->samples = NULL; /* ownership moved into the bank */
        RSCache_AudioSampleFree(sample);
        return ok;
    }

    /* Percussion comes out of the sound-effect table's synth programs. */
    {
        struct RSCache_SoundEffect* effect;
        struct RSCache_SoundPcm pcm;
        int16_t* widened;
        bool ok = false;

        archive = archive_of(load->disk, RSCACHE_DAT2_TABLE_SOUND_EFFECTS, sample_id);
        if( !archive )
            return false;
        effect = RSCache_SoundEffectNewDecode(&load->profile, archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !effect )
            return false;
        memset(&pcm, 0, sizeof(pcm));
        if( RSCache_SoundEffectRender(&load->profile, effect, &pcm) && pcm.sample_count > 0 )
        {
            widened = widen(&pcm);
            if( widened )
                ok = ToriRS_SoundBank_AddSample(
                         &load->bank,
                         table,
                         sample_id,
                         widened,
                         pcm.sample_count,
                         pcm.sample_rate,
                         0,
                         pcm.sample_count,
                         false) != NULL;
        }
        RSCache_SoundPcmRelease(&pcm);
        RSCache_SoundEffectFree(effect);
        return ok;
    }
}

bool
AudioSongLoad_Open(
    struct AudioSongLoad* out,
    const char* cache_dir,
    const char* profile_name,
    enum AudioSongSource source,
    int song_id)
{
    struct RSCache_Dat2DiskArchive* archive;
    int song_table;

    if( !out )
        return false;
    memset(out, 0, sizeof(*out));
    ToriRS_SoundBank_Init(&out->bank);

    if( !RSCache_ProfileByName(profile_name, &out->profile) )
        return false;
    out->disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !out->disk )
        return false;
    RSCache_Dat2DiskSetProfile(out->disk, &out->profile);

    song_table = source == AUDIO_SONG_JINGLE ? RSCACHE_DAT2_TABLE_MUSIC_JINGLES
                                             : RSCACHE_DAT2_TABLE_MUSIC_TRACKS;
    archive = archive_of(out->disk, song_table, song_id);
    if( !archive )
        return false;
    out->song = RSCache_MusicSongNewDecode(archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    if( !out->song )
        return false;

    /* The one setup header every music sample decodes against. */
    archive = archive_of(out->disk, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, 0);
    if( archive )
    {
        out->setup = RSCache_VorbisSetupNewDecode(archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    out->patches_named = out->song->patch_count;
    for( int i = 0; i < out->song->patch_count; i++ )
    {
        int patch_id = out->song->patches[i].patch_id;
        struct RSCache_MusicPatch* decoded;
        struct ToriRS_SoundBankPatch* slot;

        if( ToriRS_SoundBank_FindPatch(&out->bank, patch_id) )
        {
            ToriRS_SoundBank_RetainPatch(&out->bank, patch_id);
            out->patches_loaded++;
            continue;
        }
        archive = archive_of(out->disk, RSCACHE_DAT2_TABLE_MUSIC_PATCHES, patch_id);
        if( !archive )
            continue;
        decoded = RSCache_MusicPatchNewDecode(archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !decoded )
            continue;

        /* Only the notes the song actually plays need samples. */
        for( int note = 0; note < 128; note++ )
        {
            int sample_id;
            int table;

            if( !RSCache_MusicSongPatchUsesNote(&out->song->patches[i], note) )
                continue;
            sample_id = RSCache_MusicPatchNoteSampleId(decoded, note);
            if( sample_id < 0 )
                continue;
            table = RSCache_MusicPatchNoteIsMusicSample(decoded, note)
                        ? TORIRS_SOUNDBANK_TABLE_MUSIC
                        : TORIRS_SOUNDBANK_TABLE_EFFECTS;
            if( ToriRS_SoundBank_FindSample(&out->bank, table, sample_id) )
                continue;
            if( load_sample(out, table, sample_id) )
                out->samples_loaded++;
            else
                out->samples_missing++;
        }

        slot = ToriRS_SoundBank_AddPatch(&out->bank, patch_id, decoded);
        if( slot )
        {
            ToriRS_SoundBank_ResolvePatch(&out->bank, slot);
            ToriRS_SoundBank_RetainPatch(&out->bank, patch_id);
            out->patches_loaded++;
        }
    }
    return true;
}

void
AudioSongLoad_Free(struct AudioSongLoad* load)
{
    if( !load )
        return;
    ToriRS_SoundBank_Free(&load->bank);
    RSCache_MusicSongFree(load->song);
    RSCache_VorbisSetupFree(load->setup);
    if( load->disk )
        RSCache_Dat2DiskFree(load->disk);
    memset(load, 0, sizeof(*load));
}
