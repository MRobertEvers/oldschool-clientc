/*
 * audioprobe — look at what the cache's audio tables actually contain.
 *
 * The audio formats are the least documented part of the cache and the easiest
 * to get subtly wrong: a Vorbis sample that decodes to noise, a song whose
 * events are one field out, a patch that plays the right notes on the wrong
 * instrument. All three fail *quietly*. This tool exists so every claim about
 * those formats can be checked against bytes rather than against a reading of
 * the reference:
 *
 *   audioprobe <cache> <rev>                    survey every audio table
 *   audioprobe <cache> <rev> <table> <archive>  hexdump one archive
 *   audioprobe <cache> <rev> --sample <id> [--out f.wav]   decode a music sample
 *   audioprobe <cache> <rev> --effect <id> [--out f.wav]   decode a sound effect
 *   audioprobe <cache> <rev> --song   <id> [--out f.mid]   unpack a music track
 *   audioprobe <cache> <rev> --jingle <id> [--out f.mid]   unpack a jingle
 *   audioprobe <cache> <rev> --patch  <id>                 dump an instrument
 *   audioprobe <cache> <rev> --sweep                       decode everything, report
 *
 * `--sweep` is the one that matters: it decodes every sample, song and patch in
 * the cache and reports how many failed and how many bytes were left unconsumed.
 * A format error that only affects one archive in a thousand is invisible any
 * other way.
 */
#include "asset_access.h"
#include "tool_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_SOUND_EFFECTS 4
#define TABLE_MUSIC_TRACKS 6
#define TABLE_MUSIC_JINGLES 11
#define TABLE_MUSIC_SAMPLES 14
#define TABLE_MUSIC_PATCHES 15

static void
hexdump(
    const uint8_t* data,
    int size,
    int max)
{
    if( size > max )
        size = max;
    for( int i = 0; i < size; i++ )
    {
        printf("%02x ", data[i]);
        if( (i % 16) == 15 )
            printf("\n");
    }
    if( (size % 16) != 0 )
        printf("\n");
}

static int
write_wav(
    const char* path,
    const struct RSCache_AudioSample* sample)
{
    FILE* f = fopen(path, "wb");
    uint32_t data_bytes;
    uint32_t chunk;
    uint16_t s16;
    uint32_t u32;

    if( !f )
        return 0;
    data_bytes = (uint32_t)sample->sample_count * 2u;
    fwrite("RIFF", 1, 4, f);
    chunk = 36 + data_bytes;
    fwrite(&chunk, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    u32 = 16;
    fwrite(&u32, 4, 1, f);
    s16 = 1;
    fwrite(&s16, 2, 1, f); /* PCM */
    s16 = 1;
    fwrite(&s16, 2, 1, f); /* mono */
    u32 = (uint32_t)sample->sample_rate;
    fwrite(&u32, 4, 1, f);
    u32 = (uint32_t)sample->sample_rate * 2u;
    fwrite(&u32, 4, 1, f);
    s16 = 2;
    fwrite(&s16, 2, 1, f);
    s16 = 16;
    fwrite(&s16, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
    fwrite(sample->samples, 2, (size_t)sample->sample_count, f);
    fclose(f);
    return 1;
}

/** Load the shared music-sample setup header (index 14 archive 0). */
static struct RSCache_VorbisSetup*
load_music_setup(struct Tool_Dat2Cache* c)
{
    struct Tool_Bytes bytes = { 0 };
    struct RSCache_VorbisSetup* setup;

    if( !tool_dat2_archive_bytes(c, TABLE_MUSIC_SAMPLES, 0, &bytes) )
        return NULL;
    setup = RSCache_VorbisSetupNewDecode((const char*)bytes.data, bytes.size);
    tool_bytes_free(&bytes);
    return setup;
}

static int
survey(struct Tool_Dat2Cache* c)
{
    static const int tables[] = { TABLE_SOUND_EFFECTS,
                                  TABLE_MUSIC_TRACKS,
                                  TABLE_MUSIC_JINGLES,
                                  TABLE_MUSIC_SAMPLES,
                                  TABLE_MUSIC_PATCHES };
    static const char* names[] = { "sound effects", "music tracks", "music jingles",
                                   "music samples", "music patches" };

    for( unsigned t = 0; t < sizeof(tables) / sizeof(tables[0]); t++ )
    {
        struct RSCache_ReferenceTable* ref = c->disk->tables[tables[t]];
        long total = 0;
        int multi_file = 0;

        if( !ref )
        {
            printf("table %2d %-14s: absent\n", tables[t], names[t]);
            continue;
        }
        for( int i = 0; i < ref->id_count; i++ )
        {
            if( ref->archives[i].children.count > 1 )
                multi_file++;
            total += ref->archives[i].compressed;
        }
        printf(
            "table %2d %-14s: %d archives, %d multi-file, %ld compressed bytes\n",
            tables[t],
            names[t],
            ref->id_count,
            multi_file,
            total);
    }
    return 0;
}

static int
dump_archive(
    struct Tool_Dat2Cache* c,
    int table,
    int archive)
{
    struct Tool_Bytes bytes = { 0 };

    if( !tool_dat2_archive_bytes(c, table, archive, &bytes) )
    {
        printf("table %d archive %d: absent\n", table, archive);
        return 2;
    }
    printf("table %d archive %d: %d bytes\n", table, archive, bytes.size);
    hexdump(bytes.data, bytes.size, 512);
    tool_bytes_free(&bytes);
    return 0;
}

static void
report_sample(
    const char* what,
    int id,
    const struct RSCache_AudioSample* sample)
{
    printf(
        "%s %d: rate %d, %d samples (%.3fs), loop %d..%d%s\n",
        what,
        id,
        sample->sample_rate,
        sample->sample_count,
        sample->sample_rate > 0 ? (double)sample->sample_count / sample->sample_rate : 0.0,
        sample->loop_start,
        sample->loop_end,
        sample->loop ? " (looping)" : "");
}

static int
decode_music_sample(
    struct Tool_Dat2Cache* c,
    int id,
    const char* out_path)
{
    struct RSCache_VorbisSetup* setup = load_music_setup(c);
    struct Tool_Bytes bytes = { 0 };
    struct RSCache_AudioSample* sample;

    if( !setup )
    {
        fprintf(stderr, "music sample setup (table 14 archive 0) did not decode\n");
        return 2;
    }
    if( !tool_dat2_archive_bytes(c, TABLE_MUSIC_SAMPLES, id, &bytes) )
    {
        fprintf(stderr, "music sample %d: absent\n", id);
        RSCache_VorbisSetupFree(setup);
        return 2;
    }
    sample = RSCache_VorbisSampleNewDecode(setup, (const char*)bytes.data, bytes.size);
    tool_bytes_free(&bytes);
    RSCache_VorbisSetupFree(setup);
    if( !sample )
    {
        fprintf(stderr, "music sample %d: decode failed\n", id);
        return 2;
    }
    report_sample("music sample", id, sample);
    if( out_path && !write_wav(out_path, sample) )
        fprintf(stderr, "could not write %s\n", out_path);
    else if( out_path )
        printf("wrote %s\n", out_path);
    RSCache_AudioSampleFree(sample);
    return 0;
}

static int
sweep_samples(struct Tool_Dat2Cache* c)
{
    struct RSCache_VorbisSetup* setup = load_music_setup(c);
    struct RSCache_ReferenceTable* ref = c->disk->tables[TABLE_MUSIC_SAMPLES];
    int ok = 0;
    int failed = 0;
    long frames = 0;

    if( !setup || !ref )
    {
        fprintf(stderr, "music samples: no setup or no table\n");
        return 2;
    }
    for( int i = 0; i < ref->id_count; i++ )
    {
        int id = ref->ids[i];
        struct Tool_Bytes bytes = { 0 };
        struct RSCache_AudioSample* sample;

        if( id == 0 )
            continue; /* the setup header itself */
        if( !tool_dat2_archive_bytes(c, TABLE_MUSIC_SAMPLES, id, &bytes) )
            continue;
        sample = RSCache_VorbisSampleNewDecode(setup, (const char*)bytes.data, bytes.size);
        tool_bytes_free(&bytes);
        if( sample )
        {
            ok++;
            frames += sample->sample_count;
            RSCache_AudioSampleFree(sample);
        }
        else
        {
            failed++;
            if( failed <= 10 )
                fprintf(stderr, "  music sample %d failed\n", id);
        }
    }
    RSCache_VorbisSetupFree(setup);
    printf("music samples: %d decoded, %d failed, %ld frames total\n", ok, failed, frames);
    return failed == 0 ? 0 : 1;
}

static int
decode_effect(
    struct Tool_Dat2Cache* c,
    int id,
    const char* out_path)
{
    struct RSCache_ReferenceTable* ref = c->disk->tables[TABLE_SOUND_EFFECTS];
    struct Tool_Bytes bytes = { 0 };
    struct RSCache_FileList* group = NULL;
    struct RSCache_AudioSample* sample = NULL;
    int file_count = 0;

    if( !ref )
        return 2;
    for( int i = 0; i < ref->id_count; i++ )
    {
        if( ref->ids[i] == id )
            file_count = ref->archives[i].children.count;
    }
    if( !tool_dat2_archive_bytes(c, TABLE_SOUND_EFFECTS, id, &bytes) )
    {
        fprintf(stderr, "sound effect %d: absent\n", id);
        return 2;
    }
    if( file_count > 1 )
    {
        group = RSCache_FileListNewFromDecode((char*)bytes.data, bytes.size, file_count);
        if( group && group->file_count > 1 )
            sample = RSCache_VorbisEffectSampleNewDecode(group->files[1], group->file_sizes[1]);
    }
    if( !sample )
    {
        fprintf(stderr, "sound effect %d: no Vorbis sample (files=%d)\n", id, file_count);
        if( group )
            RSCache_FileListFree(group);
        tool_bytes_free(&bytes);
        return 2;
    }
    report_sample("sound effect", id, sample);
    if( out_path && write_wav(out_path, sample) )
        printf("wrote %s\n", out_path);
    RSCache_AudioSampleFree(sample);
    RSCache_FileListFree(group);
    tool_bytes_free(&bytes);
    return 0;
}

static int
decode_song(
    struct Tool_Dat2Cache* c,
    int table,
    int id,
    const char* out_path)
{
    struct Tool_Bytes bytes = { 0 };
    struct RSCache_MusicSong* song;

    if( !tool_dat2_archive_bytes(c, table, id, &bytes) )
    {
        fprintf(stderr, "table %d archive %d: absent\n", table, id);
        return 2;
    }
    song = RSCache_MusicSongNewDecode((const char*)bytes.data, bytes.size);
    if( !song )
    {
        fprintf(stderr, "table %d archive %d: song decode failed (%d bytes)\n", table, id,
                bytes.size);
        tool_bytes_free(&bytes);
        return 2;
    }
    printf(
        "song %d: %d packed bytes -> %d MIDI bytes, %d tracks, division %d, %d patches\n",
        id,
        bytes.size,
        song->midi_size,
        song->track_count,
        song->division,
        song->patch_count);
    for( int i = 0; i < song->patch_count; i++ )
    {
        int notes = 0;
        for( int n = 0; n < 128; n++ )
        {
            if( RSCache_MusicSongPatchUsesNote(&song->patches[i], n) )
                notes++;
        }
        printf("  patch %3d: %d notes, first tick %d\n", song->patches[i].patch_id, notes,
               song->patches[i].first_tick);
    }
    if( out_path )
    {
        FILE* f = fopen(out_path, "wb");
        if( f )
        {
            fwrite(song->midi, 1, (size_t)song->midi_size, f);
            fclose(f);
            printf("wrote %s\n", out_path);
        }
    }
    RSCache_MusicSongFree(song);
    tool_bytes_free(&bytes);
    return 0;
}

static int
decode_patch(
    struct Tool_Dat2Cache* c,
    int id)
{
    struct Tool_Bytes bytes = { 0 };
    struct RSCache_MusicPatch* patch;
    int voiced = 0;

    if( !tool_dat2_archive_bytes(c, TABLE_MUSIC_PATCHES, id, &bytes) )
    {
        fprintf(stderr, "patch %d: absent\n", id);
        return 2;
    }
    patch = RSCache_MusicPatchNewDecode((const char*)bytes.data, bytes.size);
    if( !patch )
    {
        fprintf(stderr, "patch %d: decode failed (%d bytes)\n", id, bytes.size);
        tool_bytes_free(&bytes);
        return 2;
    }
    for( int n = 0; n < 128; n++ )
    {
        if( patch->sample_ref[n] != 0 )
            voiced++;
    }
    printf(
        "patch %d: %d/%d bytes consumed, %d voiced notes, %d envelope sets, volume scale %d\n",
        id,
        patch->_consumed,
        bytes.size,
        voiced,
        patch->envelope_count,
        patch->volume_scale);
    for( int n = 0; n < 128; n++ )
    {
        if( patch->sample_ref[n] == 0 )
            continue;
        printf(
            "  note %3d: sample %d (%s%s), pitch %+d, vol %d, pan %d, env %d, group %d\n",
            n,
            RSCache_MusicPatchNoteSampleId(patch, n),
            RSCache_MusicPatchNoteIsMusicSample(patch, n) ? "music" : "effect",
            (patch->pitch_offset[n] & (int16_t)0x8000) != 0 ? ", loop" : "",
            patch->pitch_offset[n] & 0x7FFF,
            patch->volume[n],
            patch->pan[n],
            patch->envelope_index[n],
            patch->note_group[n]);
    }
    RSCache_MusicPatchFree(patch);
    tool_bytes_free(&bytes);
    return 0;
}

/** Decode every song, jingle and patch; report failures and short consumption. */
static int
sweep_music(struct Tool_Dat2Cache* c)
{
    static const int song_tables[] = { TABLE_MUSIC_TRACKS, TABLE_MUSIC_JINGLES };
    static const char* song_names[] = { "tracks", "jingles" };
    int status = 0;

    for( unsigned t = 0; t < 2; t++ )
    {
        struct RSCache_ReferenceTable* ref = c->disk->tables[song_tables[t]];
        int ok = 0;
        int failed = 0;
        long midi_bytes = 0;
        long packed_bytes = 0;

        if( !ref )
            continue;
        for( int i = 0; i < ref->id_count; i++ )
        {
            struct Tool_Bytes bytes = { 0 };
            struct RSCache_MusicSong* song;
            if( !tool_dat2_archive_bytes(c, song_tables[t], ref->ids[i], &bytes) )
                continue;
            song = RSCache_MusicSongNewDecode((const char*)bytes.data, bytes.size);
            packed_bytes += bytes.size;
            tool_bytes_free(&bytes);
            if( song )
            {
                ok++;
                midi_bytes += song->midi_size;
                RSCache_MusicSongFree(song);
            }
            else
            {
                failed++;
                if( failed <= 10 )
                    fprintf(stderr, "  %s %d failed\n", song_names[t], ref->ids[i]);
            }
        }
        printf(
            "music %s: %d decoded, %d failed, %ld packed -> %ld MIDI bytes\n",
            song_names[t],
            ok,
            failed,
            packed_bytes,
            midi_bytes);
        if( failed )
            status = 1;
    }

    {
        struct RSCache_ReferenceTable* ref = c->disk->tables[TABLE_MUSIC_PATCHES];
        int ok = 0;
        int failed = 0;
        int short_consumed = 0;

        if( ref )
        {
            for( int i = 0; i < ref->id_count; i++ )
            {
                struct Tool_Bytes bytes = { 0 };
                struct RSCache_MusicPatch* patch;
                if( !tool_dat2_archive_bytes(c, TABLE_MUSIC_PATCHES, ref->ids[i], &bytes) )
                    continue;
                patch = RSCache_MusicPatchNewDecode((const char*)bytes.data, bytes.size);
                if( patch )
                {
                    ok++;
                    if( patch->_consumed != bytes.size )
                    {
                        short_consumed++;
                        if( short_consumed <= 10 )
                            fprintf(
                                stderr,
                                "  patch %d consumed %d of %d\n",
                                ref->ids[i],
                                patch->_consumed,
                                bytes.size);
                    }
                    RSCache_MusicPatchFree(patch);
                }
                else
                {
                    failed++;
                    if( failed <= 10 )
                        fprintf(stderr, "  patch %d failed\n", ref->ids[i]);
                }
                tool_bytes_free(&bytes);
            }
            printf(
                "music patches: %d decoded, %d failed, %d not consumed exactly\n",
                ok,
                failed,
                short_consumed);
            if( failed || short_consumed )
                status = 1;
        }
    }
    return status;
}

int
main(
    int argc,
    char** argv)
{
    const char* dir;
    const char* rev;
    const char* out_path = NULL;
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    int status = 0;
    int arg = 3;

    if( argc < 3 )
    {
        fprintf(stderr, "usage: audioprobe <cache-dir> <rev> [table archive | --sample id | ...]\n");
        return 1;
    }
    dir = argv[1];
    rev = argv[2];

    for( int i = 3; i < argc; i++ )
    {
        if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_path = argv[i + 1];
    }

    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
        return 1;
    if( !tool_dat2_open(dir, &profile, &cache) )
        return 1;

    if( arg >= argc )
    {
        status = survey(&cache);
    }
    else if( strcmp(argv[arg], "--sample") == 0 && arg + 1 < argc )
    {
        status = decode_music_sample(&cache, atoi(argv[arg + 1]), out_path);
    }
    else if( strcmp(argv[arg], "--effect") == 0 && arg + 1 < argc )
    {
        status = decode_effect(&cache, atoi(argv[arg + 1]), out_path);
    }
    else if( strcmp(argv[arg], "--song") == 0 && arg + 1 < argc )
    {
        status = decode_song(&cache, TABLE_MUSIC_TRACKS, atoi(argv[arg + 1]), out_path);
    }
    else if( strcmp(argv[arg], "--jingle") == 0 && arg + 1 < argc )
    {
        status = decode_song(&cache, TABLE_MUSIC_JINGLES, atoi(argv[arg + 1]), out_path);
    }
    else if( strcmp(argv[arg], "--patch") == 0 && arg + 1 < argc )
    {
        status = decode_patch(&cache, atoi(argv[arg + 1]));
    }
    else if( strcmp(argv[arg], "--sweep") == 0 )
    {
        status = sweep_samples(&cache);
        status |= sweep_music(&cache);
    }
    else if( argv[arg][0] != '-' && arg + 1 < argc )
    {
        status = dump_archive(&cache, atoi(argv[arg]), atoi(argv[arg + 1]));
    }
    else
    {
        fprintf(stderr, "unrecognised arguments\n");
        status = 1;
    }

    tool_dat2_close(&cache);
    return status;
}
