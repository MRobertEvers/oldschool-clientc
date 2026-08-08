/*
 * The audio engine, with no device attached.
 *
 * Two halves:
 *
 *   unit    the mixer's retained contract -- an asset loads, a voice plays it,
 *           unloading stops the voice, loops repeat, fades reach silence, bus
 *           volume scales, and nothing is left allocated. These are the
 *           properties a backend depends on and cannot check for itself.
 *   render  a real song out of a real cache: decode the packed MIDI, load the
 *           patches it names and the samples those name, synthesise a few
 *           seconds, and look at the samples. This is the only check that the
 *           whole chain -- Vorbis, patch, sequencer, envelopes, mixer -- is
 *           connected the right way round, and it is why it is worth running
 *           even though it needs a cache on disk.
 *
 * "Look at the samples" means: is it loud enough to be music, does it start
 * from silence, and does it have a low zero-crossing rate. A broken synth fails
 * all three in characteristic ways -- silence when the samples never resolve,
 * full-scale noise when the pitch is wrong by orders of magnitude, a DC step
 * when a voice's gain is applied without a ramp.
 *
 * Skips the render half cleanly when no cache is present.
 */

#include "audio/torirs_audio.h"
#include "audio/torirs_midi_synth.h"
#include "audio/torirs_midi_synth.h"
#include "audio/torirs_mixer.h"
#include "audio/torirs_music.h"
#include "audio/torirs_pcm.h"
#include "audio/torirs_soundbank.h"

#include <rscache.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0;
static int failures = 0;
static const char* group = "";

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if( !(cond) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            printf("   FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, group, #cond);                   \
        }                                                                                          \
    } while( 0 )

#define CHECK_EQ(actual, expected)                                                                 \
    do                                                                                             \
    {                                                                                              \
        long long a = (long long)(actual);                                                         \
        long long e = (long long)(expected);                                                       \
        checks++;                                                                                  \
        if( a != e )                                                                               \
        {                                                                                          \
            failures++;                                                                            \
            printf(                                                                                \
                "   FAIL %s:%d [%s] %s == %s (got %lld, want %lld)\n",                             \
                __FILE__,                                                                          \
                __LINE__,                                                                          \
                group,                                                                             \
                #actual,                                                                           \
                #expected,                                                                         \
                a,                                                                                 \
                e);                                                                                \
        }                                                                                          \
    } while( 0 )

#define GROUP(name)                                                                                \
    do                                                                                             \
    {                                                                                              \
        group = (name);                                                                            \
        printf("-- %s\n", (name));                                                                 \
    } while( 0 )

/* --- helpers -------------------------------------------------------------- */

/** A one-second 440Hz sine, so a test can hear what it asked for. */
static int16_t*
make_tone(
    int sample_count,
    int sample_rate,
    double frequency)
{
    int16_t* samples = malloc((size_t)sample_count * sizeof(int16_t));

    for( int i = 0; i < sample_count; i++ )
        samples[i] =
            (int16_t)(16000.0 * sin(2.0 * 3.14159265358979 * frequency * i / sample_rate));
    return samples;
}

static long
block_energy(
    const int16_t* block,
    int count)
{
    long energy = 0;
    for( int i = 0; i < count; i++ )
        energy += (long)(block[i] < 0 ? -block[i] : block[i]);
    return energy;
}

static int
block_peak(
    const int16_t* block,
    int count)
{
    int peak = 0;
    for( int i = 0; i < count; i++ )
    {
        int magnitude = block[i] < 0 ? -block[i] : block[i];
        if( magnitude > peak )
            peak = magnitude;
    }
    return peak;
}

/* --- mixer unit tests ----------------------------------------------------- */

static void
test_asset_lifecycle(void)
{
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    int16_t* tone = make_tone(4410, 22050, 440.0);
    int16_t block[512 * 2];

    ToriRS_Mixer_Init(&mixer, 22050);
    CHECK_EQ(ToriRS_Mixer_LiveAssetCount(&mixer), 0);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
    command.asset_id = 7;
    command.pcm = tone;
    command.sample_count = 4410;
    command.sample_rate = 22050;
    ToriRS_Mixer_Apply(&mixer, &command);
    CHECK_EQ(ToriRS_Mixer_LiveAssetCount(&mixer), 1);

    /* The mixer copies: freeing the source must not matter. */
    free(tone);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = 7;
    command.voice_id = 100;
    command.bus = TORIRS_AUDIO_BUS_EFFECTS;
    ToriRS_Mixer_Apply(&mixer, &command);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 1);

    ToriRS_Mixer_Render(&mixer, block, 512);
    CHECK(block_peak(block, 512 * 2) > 1000);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 1);

    /* Unloading the asset must stop the voice, not leave it on freed samples. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_UNLOAD);
    command.asset_id = 7;
    ToriRS_Mixer_Apply(&mixer, &command);
    CHECK_EQ(ToriRS_Mixer_LiveAssetCount(&mixer), 0);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 0);

    ToriRS_Mixer_Render(&mixer, block, 512);
    CHECK_EQ(block_peak(block, 512 * 2), 0);

    ToriRS_Mixer_Free(&mixer);
}

static void
test_voice_ends_and_loops(void)
{
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    int16_t* tone = make_tone(1000, 22050, 440.0);
    int16_t block[2048 * 2];

    ToriRS_Mixer_Init(&mixer, 22050);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
    command.asset_id = 1;
    command.pcm = tone;
    command.sample_count = 1000;
    command.sample_rate = 22050;
    ToriRS_Mixer_Apply(&mixer, &command);

    /* One-shot: gone after its own length. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = 1;
    command.voice_id = 1;
    command.loop_count = 0;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_Mixer_Render(&mixer, block, 2048);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 0);
    /* ...and the tail of the block is silence, not a repeat. */
    CHECK_EQ(block_peak(block + 1500 * 2, 500 * 2), 0);

    /* Endless loop: still going after many times its own length. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = 1;
    command.voice_id = 2;
    command.loop_count = -1;
    ToriRS_Mixer_Apply(&mixer, &command);
    for( int i = 0; i < 8; i++ )
        ToriRS_Mixer_Render(&mixer, block, 2048);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 1);
    CHECK(block_peak(block, 2048 * 2) > 1000);

    /* Stopping it takes effect immediately with no fade. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_STOP);
    command.voice_id = 2;
    ToriRS_Mixer_Apply(&mixer, &command);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 0);

    free(tone);
    ToriRS_Mixer_Free(&mixer);
}

/**
 * A clip whose loop span ends before its samples do.
 *
 * The reference bounds playback by the loop span only while passes remain; once
 * the count is exhausted it plays out to the end of the *whole* clip. Ending at
 * `loop_end` instead cuts the waveform off mid-cycle, which is a step
 * discontinuity -- a click on every play of every effect that has a loop span,
 * and completely inaudible to a fixture whose loop covers the whole clip.
 *
 * The fixture here is deliberately shaped so the two behaviours are trivially
 * distinguishable: silence inside the loop span, full scale after it. Stopping
 * early produces an empty block.
 */
static void
test_loop_span_shorter_than_clip(void)
{
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    int16_t* samples = calloc(2000, sizeof(int16_t));
    int16_t block[4096 * 2];

    for( int i = 1000; i < 2000; i++ )
        samples[i] = (int16_t)(i % 2 ? 20000 : -20000);

    ToriRS_Mixer_Init(&mixer, 22050);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
    command.asset_id = 9;
    command.pcm = samples;
    command.sample_count = 2000;
    command.sample_rate = 22050;
    command.loop_start = 0;
    command.loop_end = 1000;
    ToriRS_Mixer_Apply(&mixer, &command);
    free(samples);

    /* Play once: the tail past loop_end must be heard. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = 9;
    command.voice_id = 1;
    command.loop_count = 0;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_Mixer_Render(&mixer, block, 4096);
    CHECK(block_peak(block, 4096 * 2) > 10000);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 0);

    /* Loop twice: the loop span repeats (still silent), and the tail still
     * arrives at the end rather than the voice ending at loop_end. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = 9;
    command.voice_id = 2;
    command.loop_count = 2;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_Mixer_Render(&mixer, block, 4096);
    CHECK(block_peak(block, 4096 * 2) > 10000);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 0);

    ToriRS_Mixer_Free(&mixer);
}

/**
 * A fade under a non-unity bus still reaches silence.
 *
 * The bus gain used to be folded into the voice's left/right gain before each
 * block and restored after, which discarded the ramp's progress every frame: a
 * fade would tick one step and then be undone, forever.
 */
static void
test_fade_under_bus(void)
{
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    int16_t* tone = make_tone(22050, 22050, 440.0);
    int16_t block[2205 * 2];

    ToriRS_Mixer_Init(&mixer, 22050);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
    command.asset_id = 11;
    command.pcm = tone;
    command.sample_count = 22050;
    command.sample_rate = 22050;
    ToriRS_Mixer_Apply(&mixer, &command);
    free(tone);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_BUS_VOLUME);
    command.target_bus = TORIRS_AUDIO_BUS_EFFECTS;
    command.volume = 100;
    ToriRS_Mixer_Apply(&mixer, &command);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = 11;
    command.voice_id = 3;
    command.loop_count = -1;
    ToriRS_Mixer_Apply(&mixer, &command);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_STOP);
    command.voice_id = 3;
    command.fade_ms = 50; /* 1102 frames, well inside one render below */
    ToriRS_Mixer_Apply(&mixer, &command);

    ToriRS_Mixer_Render(&mixer, block, 2205);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 0);
    CHECK(block_energy(block, 400 * 2) > 0);
    /* The last quarter is past the end of the fade: silence. */
    CHECK_EQ(block_peak(block + 1800 * 2, 400 * 2), 0);

    ToriRS_Mixer_Free(&mixer);
}

/**
 * A ramp must actually move.
 *
 * `step = (target - current) / ramp_frames` truncates to zero whenever the
 * change is smaller than the ramp is long -- and the synth re-ramps every
 * envelope step, 220 frames at 22050Hz. A zero step means the gain sits still
 * for 10ms and then jumps, which is a 100Hz staircase on every voice: not a
 * fault in any note, just a buzz over everything. The reference clamps the ramp
 * length to the largest gain delta so the step is never zero, and this is that
 * invariant.
 */
static void
test_ramp_never_stalls(void)
{
    struct ToriRS_PcmSound sound;
    struct ToriRS_PcmVoice voice;
    int16_t* tone = make_tone(4410, 22050, 440.0);
    int base_left;

    memset(&sound, 0, sizeof(sound));
    sound.samples = tone;
    sound.sample_count = 4410;
    sound.sample_rate = 22050;

    /* A deliberately tiny change over a deliberately long ramp: the case that
     * truncates to a zero step. */
    ToriRS_PcmVoice_Start(&voice, &sound, 22050, 22050, TORIRS_PCM_VOLUME_UNITY / 2,
                          TORIRS_PCM_PAN_CENTRE, -1);
    base_left = voice.left_gain;
    ToriRS_PcmVoice_SetGain(
        &voice, TORIRS_PCM_VOLUME_UNITY / 2 + 4, TORIRS_PCM_PAN_CENTRE, 4096);
    CHECK(voice.left_gain != base_left || voice.ramp_frames > 0);
    if( voice.ramp_frames > 0 )
    {
        CHECK(voice.left_step != 0 || voice.left_gain == voice.target_left_gain);
        /* Clamped to the delta, so it lands quickly rather than staircasing. */
        CHECK(voice.ramp_frames <= 4096);
    }

    /* A large change over a short ramp still ramps, and still gets there. */
    ToriRS_PcmVoice_SetGain(&voice, TORIRS_PCM_VOLUME_UNITY, TORIRS_PCM_PAN_CENTRE, 64);
    CHECK(voice.ramp_frames == 64);
    CHECK(voice.left_step != 0);
    ToriRS_PcmVoice_Skip(&voice, 64);
    CHECK_EQ(voice.left_gain, voice.target_left_gain);
    CHECK_EQ(voice.ramp_frames, 0);

    free(tone);
}

static void
test_fade_and_bus_volume(void)
{
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    int16_t* tone = make_tone(22050, 22050, 440.0);
    int16_t block[4410 * 2];
    long loud;
    long quiet;

    ToriRS_Mixer_Init(&mixer, 22050);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
    command.asset_id = 3;
    command.pcm = tone;
    command.sample_count = 22050;
    command.sample_rate = 22050;
    ToriRS_Mixer_Apply(&mixer, &command);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = 3;
    command.voice_id = 5;
    command.loop_count = -1;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_Mixer_Render(&mixer, block, 4410);
    loud = block_energy(block, 4410 * 2);
    CHECK(loud > 0);

    /* Half bus volume is audibly quieter but not silent. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_BUS_VOLUME);
    command.target_bus = TORIRS_AUDIO_BUS_EFFECTS;
    command.volume = 128;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_Mixer_Render(&mixer, block, 4410);
    quiet = block_energy(block, 4410 * 2);
    CHECK(quiet > 0);
    CHECK(quiet < loud);

    /* A muted bus still ages the voice: a one-shot must not become immortal. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_BUS_VOLUME);
    command.target_bus = TORIRS_AUDIO_BUS_EFFECTS;
    command.volume = 0;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_Mixer_Render(&mixer, block, 4410);
    CHECK_EQ(block_peak(block, 4410 * 2), 0);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 1);

    /* A 100ms fade reaches silence and ends the voice. */
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_BUS_VOLUME);
    command.target_bus = TORIRS_AUDIO_BUS_EFFECTS;
    command.volume = TORIRS_AUDIO_VOLUME_MAX;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_STOP);
    command.voice_id = 5;
    command.fade_ms = 100;
    ToriRS_Mixer_Apply(&mixer, &command);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 1);
    ToriRS_Mixer_Render(&mixer, block, 4410);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 0);
    /* The fade decays: the last quarter is much quieter than the first. */
    CHECK(block_energy(block, 1000 * 2) > block_energy(block + 3000 * 2, 1000 * 2) * 2);

    free(tone);
    ToriRS_Mixer_Free(&mixer);
}

static void
test_stream(void)
{
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    struct ToriRS_AudioFeedback feedback;
    int16_t pushed[1024 * 2];
    int16_t block[512 * 2];

    for( int i = 0; i < 1024; i++ )
    {
        pushed[i * 2] = 8000;
        pushed[i * 2 + 1] = -8000;
    }

    ToriRS_Mixer_Init(&mixer, 22050);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_OPEN);
    command.stream_id = 0;
    command.channels = 2;
    command.sample_rate = 22050;
    command.bus = TORIRS_AUDIO_BUS_MUSIC;
    command.volume = TORIRS_AUDIO_VOLUME_MAX;
    ToriRS_Mixer_Apply(&mixer, &command);

    ToriRS_Mixer_Feedback(&mixer, &feedback);
    CHECK(feedback.stream_headroom[0] > 0);
    CHECK_EQ(feedback.stream_buffered[0], 0);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_PUSH);
    command.stream_id = 0;
    command.pcm = pushed;
    command.frame_count = 1024;
    ToriRS_Mixer_Apply(&mixer, &command);

    ToriRS_Mixer_Feedback(&mixer, &feedback);
    CHECK_EQ(feedback.stream_buffered[0], 1024);

    ToriRS_Mixer_Render(&mixer, block, 512);
    CHECK(block[0] > 7000);
    CHECK(block[1] < -7000);
    ToriRS_Mixer_Feedback(&mixer, &feedback);
    CHECK_EQ(feedback.stream_buffered[0], 512);

    /* Draining past the end is silence, not garbage. */
    ToriRS_Mixer_Render(&mixer, block, 512);
    ToriRS_Mixer_Render(&mixer, block, 512);
    CHECK_EQ(block_peak(block, 512 * 2), 0);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_CLOSE);
    command.stream_id = 0;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_Mixer_Free(&mixer);
}

static void
test_asset_clear_and_stop_all(void)
{
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    int16_t* tone = make_tone(2000, 22050, 220.0);

    ToriRS_Mixer_Init(&mixer, 22050);
    for( int i = 0; i < 8; i++ )
    {
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
        command.asset_id = i;
        command.pcm = tone;
        command.sample_count = 2000;
        command.sample_rate = 22050;
        ToriRS_Mixer_Apply(&mixer, &command);

        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
        command.asset_id = i;
        command.voice_id = 200 + i;
        command.loop_count = -1;
        ToriRS_Mixer_Apply(&mixer, &command);
    }
    CHECK_EQ(ToriRS_Mixer_LiveAssetCount(&mixer), 8);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 8);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STOP_ALL);
    ToriRS_Mixer_Apply(&mixer, &command);
    CHECK_EQ(ToriRS_Mixer_LiveVoiceCount(&mixer), 0);
    CHECK_EQ(ToriRS_Mixer_LiveAssetCount(&mixer), 8);

    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_CLEAR);
    ToriRS_Mixer_Apply(&mixer, &command);
    CHECK_EQ(ToriRS_Mixer_LiveAssetCount(&mixer), 0);

    free(tone);
    ToriRS_Mixer_Free(&mixer);
}

/* --- soundbank unit tests ------------------------------------------------- */

static void
test_soundbank_refcounts(void)
{
    struct ToriRS_SoundBank bank;
    int16_t* a = make_tone(100, 22050, 440.0);
    int16_t* b = make_tone(100, 22050, 660.0);

    ToriRS_SoundBank_Init(&bank);
    CHECK(ToriRS_SoundBank_AddSample(
              &bank, TORIRS_SOUNDBANK_TABLE_MUSIC, 1, a, 100, 22050, 0, 100, false) != NULL);
    CHECK(ToriRS_SoundBank_AddSample(
              &bank, TORIRS_SOUNDBANK_TABLE_EFFECTS, 1, b, 100, 22050, 0, 100, false) != NULL);
    /* Same id, different table: two distinct samples, which is the whole point
     * of keying by table. */
    CHECK_EQ(bank.sample_count, 2);
    CHECK(ToriRS_SoundBank_FindSample(&bank, TORIRS_SOUNDBANK_TABLE_MUSIC, 1) !=
          ToriRS_SoundBank_FindSample(&bank, TORIRS_SOUNDBANK_TABLE_EFFECTS, 1));

    ToriRS_SoundBank_Free(&bank);
    CHECK_EQ(bank.sample_count, 0);
    CHECK_EQ(bank.sample_bytes, 0);
}

/* --- end-to-end render ---------------------------------------------------- */

struct song_render
{
    int patches_requested;
    int patches_loaded;
    int samples_loaded;
    int notes_started;
    int notes_dropped_no_sample;
};

/** Decode one archive of a table, or NULL. */
static struct RSCache_Dat2DiskArchive*
load_archive(
    struct RSCache_Dat2Disk* disk,
    int logical_table,
    int archive_id)
{
    int table = RSCache_Dat2DiskTableId(disk, logical_table);
    if( table < 0 )
        return NULL;
    return RSCache_Dat2DiskArchiveNewLoad(disk, table, archive_id);
}

/**
 * Build the soundbank a song needs and render it.
 *
 * Deliberately eager and synchronous: the client loads these through the task
 * runner, but the property under test is that the *data* fits together, and a
 * scheduler in the middle would only make a failure harder to localise.
 */
static void
test_render_song(
    const char* cache_dir,
    const char* profile_name,
    int song_id)
{
    char path[512];
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_MusicSong* song;
    struct RSCache_VorbisSetup* setup = NULL;
    struct ToriRS_SoundBank bank;
    struct ToriRS_MidiSynth synth;
    struct song_render report;
    int16_t* block;
    const int frames = 22050 * 4;
    long energy = 0;
    int peak = 0;
    int crossings = 0;
    int lead_silence = 0;

    memset(&report, 0, sizeof(report));
    snprintf(path, sizeof(path), "%s", cache_dir);
    disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP %s: not present\n", cache_dir);
        return;
    }
    if( !RSCache_ProfileByName(profile_name, &profile) )
    {
        printf("   SKIP %s: no profile %s\n", cache_dir, profile_name);
        RSCache_Dat2DiskFree(disk);
        return;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    archive = load_archive(disk, RSCACHE_DAT2_TABLE_MUSIC_TRACKS, song_id);
    if( !archive )
    {
        printf("   SKIP %s: no track %d\n", cache_dir, song_id);
        RSCache_Dat2DiskFree(disk);
        return;
    }
    song = RSCache_MusicSongNewDecode(archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    CHECK(song != NULL);
    if( !song )
    {
        RSCache_Dat2DiskFree(disk);
        return;
    }

    /* The shared Vorbis setup, without which every music sample is silence. */
    archive = load_archive(disk, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, 0);
    if( archive )
    {
        setup = RSCache_VorbisSetupNewDecode(archive->data, archive->data_size);
        RSCache_Dat2DiskArchiveFree(archive);
    }
    CHECK(setup != NULL);

    ToriRS_SoundBank_Init(&bank);

    for( int i = 0; i < song->patch_count; i++ )
    {
        int patch_id = song->patches[i].patch_id;
        struct RSCache_MusicPatch* decoded;
        struct ToriRS_SoundBankPatch* slot;

        report.patches_requested++;
        archive = load_archive(disk, RSCACHE_DAT2_TABLE_MUSIC_PATCHES, patch_id);
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
            bool music_table;
            int table;

            if( !RSCache_MusicSongPatchUsesNote(&song->patches[i], note) )
                continue;
            sample_id = RSCache_MusicPatchNoteSampleId(decoded, note);
            if( sample_id < 0 )
                continue;
            music_table = RSCache_MusicPatchNoteIsMusicSample(decoded, note);
            table = music_table ? TORIRS_SOUNDBANK_TABLE_MUSIC : TORIRS_SOUNDBANK_TABLE_EFFECTS;
            if( ToriRS_SoundBank_FindSample(&bank, table, sample_id) )
                continue;

            if( music_table )
            {
                struct RSCache_AudioSample* pcm;
                archive = load_archive(disk, RSCACHE_DAT2_TABLE_MUSIC_SAMPLES, sample_id);
                if( !archive || !setup )
                {
                    RSCache_Dat2DiskArchiveFree(archive);
                    continue;
                }
                pcm = RSCache_VorbisSampleNewDecode(setup, archive->data, archive->data_size);
                RSCache_Dat2DiskArchiveFree(archive);
                if( !pcm )
                    continue;
                if( ToriRS_SoundBank_AddSample(
                        &bank,
                        table,
                        sample_id,
                        pcm->samples,
                        pcm->sample_count,
                        pcm->sample_rate,
                        pcm->loop_start,
                        pcm->loop_end,
                        pcm->loop) )
                {
                    report.samples_loaded++;
                    pcm->samples = NULL; /* ownership moved into the bank */
                }
                RSCache_AudioSampleFree(pcm);
            }
            else
            {
                /* Percussion comes out of the sound-effect table's synth
                 * programs, rendered the same way an effect is. */
                struct RSCache_SoundEffect* effect;
                struct RSCache_SoundPcm pcm;
                int16_t* widened;

                archive = load_archive(disk, RSCACHE_DAT2_TABLE_SOUND_EFFECTS, sample_id);
                if( !archive )
                    continue;
                effect = RSCache_SoundEffectNewDecode(&profile, archive->data, archive->data_size);
                RSCache_Dat2DiskArchiveFree(archive);
                if( !effect )
                    continue;
                memset(&pcm, 0, sizeof(pcm));
                if( !RSCache_SoundEffectRender(&profile, effect, &pcm) || pcm.sample_count <= 0 )
                {
                    RSCache_SoundEffectFree(effect);
                    RSCache_SoundPcmRelease(&pcm);
                    continue;
                }
                widened = malloc((size_t)pcm.sample_count * sizeof(int16_t));
                for( int s = 0; s < pcm.sample_count; s++ )
                    widened[s] = (int16_t)(((int)pcm.samples[s] - 128) << 8);
                if( ToriRS_SoundBank_AddSample(
                        &bank,
                        table,
                        sample_id,
                        widened,
                        pcm.sample_count,
                        pcm.sample_rate,
                        0,
                        pcm.sample_count,
                        false) )
                    report.samples_loaded++;
                RSCache_SoundPcmRelease(&pcm);
                RSCache_SoundEffectFree(effect);
            }
        }

        slot = ToriRS_SoundBank_AddPatch(&bank, patch_id, decoded);
        if( slot )
        {
            report.patches_loaded++;
            ToriRS_SoundBank_ResolvePatch(&bank, slot);
            ToriRS_SoundBank_RetainPatch(&bank, patch_id);
        }
    }

    ToriRS_MidiSynth_Init(&synth, &bank, 22050);
    CHECK(ToriRS_MidiSynth_Play(&synth, song->midi, song->midi_size, true));

    block = calloc((size_t)frames * 2, sizeof(int16_t));
    ToriRS_MidiSynth_Render(&synth, block, frames);

    for( int i = 0; i < frames * 2; i++ )
    {
        int magnitude = block[i] < 0 ? -block[i] : block[i];
        energy += magnitude;
        if( magnitude > peak )
            peak = magnitude;
    }
    for( int i = 2; i < frames * 2; i += 2 )
    {
        if( (block[i - 2] < 0) != (block[i] < 0) )
            crossings++;
    }
    while( lead_silence < frames && block[lead_silence * 2] == 0 )
        lead_silence++;

    report.notes_started = synth.stats.notes_started;
    report.notes_dropped_no_sample = synth.stats.notes_dropped_no_sample;

    printf(
        "   %s track %d: %d/%d patches, %d samples, %d notes (%d dropped), peak %d, "
        "mean |x| %ld, %d zero-crossings/s\n",
        cache_dir,
        song_id,
        report.patches_loaded,
        report.patches_requested,
        report.samples_loaded,
        report.notes_started,
        report.notes_dropped_no_sample,
        peak,
        energy / (frames * 2),
        crossings / 4);

    /* Every patch the song names must be in the cache and must decode. */
    CHECK_EQ(report.patches_loaded, report.patches_requested);
    /* Notes must actually start, and must find their samples. */
    CHECK(report.notes_started > 0);
    CHECK_EQ(report.notes_dropped_no_sample, 0);
    /* Music, not silence. */
    CHECK(peak > 2000);
    CHECK(energy / (frames * 2) > 50);
    /* Music, not noise: a broken pitch or a broken sample reads as full-scale
     * hiss, which has a zero-crossing rate in the thousands per second. */
    CHECK(crossings / 4 < 8000);
    /* No DC step at the start: the first sample is silence or near it. */
    CHECK(block[0] > -2000 && block[0] < 2000);

    /*
     * TORIRS_AUDIO_TEST_WAV=<dir> drops what was rendered next to the numbers.
     * The statistical checks above catch a *broken* synth; only listening
     * catches a subtly wrong one, and the cheapest way to make that possible is
     * for the test to leave its output on disk.
     */
    {
        const char* dump_dir = getenv("TORIRS_AUDIO_TEST_WAV");
        if( dump_dir )
        {
            char out_path[512];
            FILE* f;
            snprintf(out_path, sizeof(out_path), "%s/%s_track%d.wav", dump_dir, profile_name,
                     song_id);
            f = fopen(out_path, "wb");
            if( f )
            {
                uint32_t data_bytes = (uint32_t)frames * 4u;
                uint32_t u32;
                uint16_t u16;
                fwrite("RIFF", 1, 4, f);
                u32 = 36 + data_bytes;
                fwrite(&u32, 4, 1, f);
                fwrite("WAVEfmt ", 1, 8, f);
                u32 = 16;
                fwrite(&u32, 4, 1, f);
                u16 = 1;
                fwrite(&u16, 2, 1, f);
                u16 = 2;
                fwrite(&u16, 2, 1, f);
                u32 = 22050;
                fwrite(&u32, 4, 1, f);
                u32 = 22050 * 4;
                fwrite(&u32, 4, 1, f);
                u16 = 4;
                fwrite(&u16, 2, 1, f);
                u16 = 16;
                fwrite(&u16, 2, 1, f);
                fwrite("data", 1, 4, f);
                fwrite(&data_bytes, 4, 1, f);
                fwrite(block, 2, (size_t)frames * 2, f);
                fclose(f);
                printf("   wrote %s\n", out_path);
            }
        }
    }

    free(block);
    ToriRS_MidiSynth_Free(&synth);
    ToriRS_SoundBank_Free(&bank);
    CHECK_EQ(bank.sample_bytes, 0);
    RSCache_VorbisSetupFree(setup);
    RSCache_MusicSongFree(song);
    RSCache_Dat2DiskFree(disk);
}


/* --- seeking, which MIDI_SWAP is built on ---------------------------------- */

/** Append a variable-length quantity. */
static int
smf_vlq(uint8_t* out, int value)
{
    if( value < 128 )
    {
        out[0] = (uint8_t)value;
        return 1;
    }
    out[0] = (uint8_t)(0x80 | (value >> 7));
    out[1] = (uint8_t)(value & 127);
    return 2;
}

/*
 * A hand-built single-track SMF:
 *
 *   tick   0  program change ch0 -> 40, controller 7 (volume) -> 100
 *   tick 100  note on  ch0 note 60
 *   tick 200  note off ch0 note 60
 *   tick 300  end of track
 *
 * Seeking past tick 100 must arrive with the program and volume applied and
 * *without* note 60 sounding. Those two requirements pull in opposite
 * directions -- the naive implementations either replay everything (and the
 * skipped notes all fire at once) or skip everything (and the channel is left
 * on the default instrument at the default volume) -- which is why this is
 * tested on a file whose expected state is written down rather than on cache
 * content where "sounds about right" is the only available check.
 */
static int
build_test_smf(uint8_t* out)
{
    uint8_t* p = out;
    uint8_t* track_len;

    memcpy(p, "MThd", 4);
    p += 4;
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 6; p += 4;
    p[0] = 0; p[1] = 0; p += 2;   /* format 0 */
    p[0] = 0; p[1] = 1; p += 2;   /* one track */
    p[0] = 0; p[1] = 96; p += 2;  /* division */

    memcpy(p, "MTrk", 4);
    p += 4;
    track_len = p;
    p += 4;

    p += smf_vlq(p, 0);
    *p++ = 0xC0; *p++ = 40;              /* program change */
    p += smf_vlq(p, 0);
    *p++ = 0xB0; *p++ = 7; *p++ = 100;   /* channel volume */
    p += smf_vlq(p, 100);
    *p++ = 0x90; *p++ = 60; *p++ = 100;  /* note on */
    p += smf_vlq(p, 100);
    *p++ = 0x80; *p++ = 60; *p++ = 0;    /* note off */
    p += smf_vlq(p, 100);
    *p++ = 0xFF; *p++ = 0x2F; *p++ = 0;  /* end of track */

    {
        long n = p - track_len - 4;
        track_len[0] = (uint8_t)(n >> 24);
        track_len[1] = (uint8_t)(n >> 16);
        track_len[2] = (uint8_t)(n >> 8);
        track_len[3] = (uint8_t)n;
    }
    return (int)(p - out);
}

static void
test_midi_seek(void)
{
    uint8_t smf[256];
    int size = build_test_smf(smf);
    struct ToriRS_SoundBank bank;
    struct ToriRS_MidiSynth synth;

    ToriRS_SoundBank_Init(&bank);

    /* From the top: the note is struck. */
    ToriRS_MidiSynth_Init(&synth, &bank, 22050);
    CHECK(ToriRS_MidiSynth_Play(&synth, smf, size, false));
    {
        int16_t block[512 * 2];
        ToriRS_MidiSynth_Render(&synth, block, 512);
    }
    CHECK_EQ(synth.channels[0].patch_id, 40);
    CHECK_EQ(synth.channels[0].volume, 100 << 7);
    ToriRS_MidiSynth_Free(&synth);

    /* Seeking past the note: same channel state, note not sounding. */
    ToriRS_MidiSynth_Init(&synth, &bank, 22050);
    CHECK(ToriRS_MidiSynth_PlayFrom(&synth, smf, size, false, 150));
    CHECK(synth.current_tick >= 150);
    /* The state events before the seek point were applied... */
    CHECK_EQ(synth.channels[0].patch_id, 40);
    CHECK_EQ(synth.channels[0].volume, 100 << 7);
    /*
     * ...but the note struck at tick 100 was not replayed.
     *
     * Checked via the *dropped* counter, not `notes_started`. The bank here is
     * empty, so a note that reaches channel_note_on is dropped for having no
     * patch and never counts as started -- meaning `notes_started == 0` holds
     * whether the note was suppressed or merely unplayable, and asserting it
     * would pass even with the suppression removed. This distinguishes: a
     * suppressed note never reaches the lookup at all.
     */
    CHECK_EQ(ToriRS_MidiSynth_LiveNoteCount(&synth), 0);
    CHECK_EQ(synth.stats.notes_started, 0);
    CHECK_EQ(synth.stats.notes_dropped_no_patch, 0);
    /* The audio clock landed with the sequencer, so the first render does not
     * replay the span that was just skipped. */
    CHECK(synth.position == synth.next_event_time);
    ToriRS_MidiSynth_Free(&synth);

    /* A zero/negative start tick is exactly Play. */
    ToriRS_MidiSynth_Init(&synth, &bank, 22050);
    CHECK(ToriRS_MidiSynth_PlayFrom(&synth, smf, size, false, 0));
    CHECK_EQ(synth.current_tick, 0);
    ToriRS_MidiSynth_Free(&synth);

    ToriRS_SoundBank_Free(&bank);
}

static void
test_music_swap_state(void)
{
    struct ToriRS_MusicPlayer player;

    ToriRS_Music_Init(&player);

    /* Nothing queued and nothing playing: a swap must not become a song change,
     * because MIDI_SWAP carries no id of its own. */
    CHECK_EQ(player.secondary_song, -1);
    ToriRS_Music_Swap(&player, 100, 100);
    CHECK(!player.has_request);

    /* A queued variant with nothing playing is still a no-op. */
    ToriRS_Music_SetSecondary(&player, 77);
    CHECK_EQ(player.secondary_song, 77);
    ToriRS_Music_Swap(&player, 100, 100);
    CHECK(!player.has_request);

    /* With a song playing, the swap requests the variant and keeps the outgoing
     * song as the new secondary, so swapping again comes back. */
    player.current_song = 42;
    player.current_source = TORIRS_MUSIC_SOURCE_TRACK;
    player.current_loop = true;
    player.state = TORIRS_MUSIC_PLAYING;
    player.synth.current_tick = 1234;
    ToriRS_Music_Swap(&player, 100, 200);
    CHECK(player.has_request);
    CHECK_EQ(player.request_song, 77);
    CHECK_EQ(player.request_fade_out_ms, 100);
    CHECK_EQ(player.request_fade_in_ms, 200);
    CHECK_EQ(player.secondary_song, 42);
    CHECK_EQ(player.request_resume_tick, 1234);
    CHECK(player.request_loop);

    /*
     * A swap whose target is already playing must not leave the resume armed.
     *
     * Request declines the redundant change, and an armed resume tick would
     * then be spent on whatever song changed next -- that song would start from
     * the middle of itself, far from the swap that caused it.
     */
    ToriRS_Music_Init(&player);
    player.current_song = 42;
    player.current_source = TORIRS_MUSIC_SOURCE_TRACK;
    player.state = TORIRS_MUSIC_PLAYING;
    player.synth.current_tick = 999;
    ToriRS_Music_SetSecondary(&player, 42); /* the variant *is* what is playing */
    ToriRS_Music_Swap(&player, 0, 0);
    CHECK(!player.has_request);
    CHECK_EQ(player.request_resume_tick, 0);

    ToriRS_Music_Free(&player);
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_root = argc > 1 ? argv[1] : ".";
    char path[512];

    GROUP("mixer: asset lifecycle");
    test_asset_lifecycle();

    GROUP("mixer: voice end and loop");
    test_voice_ends_and_loops();

    GROUP("mixer: loop span shorter than clip");
    test_loop_span_shorter_than_clip();

    GROUP("voice: a ramp never stalls");
    test_ramp_never_stalls();

    GROUP("mixer: fade under a non-unity bus");
    test_fade_under_bus();

    GROUP("mixer: fades and bus volume");
    test_fade_and_bus_volume();

    GROUP("mixer: streams");
    test_stream();

    GROUP("mixer: clear and stop-all");
    test_asset_clear_and_stop_all();


    GROUP("soundbank");
    test_soundbank_refcounts();

    GROUP("midi seek and song swap");
    test_midi_seek();
    test_music_swap_state();

    GROUP("render a real song");
    snprintf(path, sizeof(path), "%s/cache.osrs239", cache_root);
    test_render_song(path, "osrs239", 0);
    snprintf(path, sizeof(path), "%s/cache.osrs230", cache_root);
    test_render_song(path, "osrs230", 2);

    printf("audio_test: %d/%d checks %s\n", checks - failures, checks,
           failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
