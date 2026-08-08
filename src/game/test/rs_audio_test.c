/*
 * Sound, end to end: cache bytes -> synth render -> game queue -> platform.
 *
 * The parts each have their own tests (rscache's test_sound covers the codec and
 * the renderer), so what is checked here is the wiring between them, which is
 * where a sound system usually fails silently:
 *
 *   - a SYNTH_SOUND packet reaches the platform as *audible* PCM, from a real
 *     cache, on both a dat1 (254) and a dat2 (230) boot. Audible is the point: a
 *     buffer of pure silence would satisfy every other assertion here.
 *   - the queue's timing is the reference's — the server's delay counts down on
 *     client ticks, and the effect's own trim lead-in is added to it.
 *   - the overlap rule drops a short sound landing under a long one, rather than
 *     cutting it off.
 *   - loop repeats lengthen the clip by whole loop spans.
 *   - the volume varp maps to the levels the reference uses.
 *
 * The platform side is the silent backend, so this runs headless and asserts what
 * the game *asked* to be played.
 */

#include "audio/torirs_audio.h"
#include "engine/cache_provider.h"
#include <toridraw.h>
#include <toridraw_scene.h>
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_sound_from_rscache.h"
#include "game/rs_audio.h"
#include "game/rs_cs2_host.h"
#include "features/features.h"
#include "platform/platform_audio_null.h"
#include "platform/platform_x_io.h"
#include "varp/varp_manager.h"
#include "task_runner.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define REPO_ROOT "/Users/matthewevers/Documents/git_repos/3draster"

static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
            printf("  ok: %s\n", msg);                                                             \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

static void
test_audio_settings_snapshot(void)
{
    struct RS_CS2Host host;
    struct RS_CS2AudioSettings settings;
    struct VarPManager varps;
    struct VarPType* types;

    printf("CS2 audio settings snapshot\n");
    memset(&host, 0, sizeof(host));
    host.device_options[RS_CS2_DEVICEOPTION_MASTER_VOLUME] = 80;
    host.game_options[RS_CS2_GAMEOPTION_MUSIC_VOLUME] = 25;
    host.game_options[RS_CS2_GAMEOPTION_SOUND_VOLUME] = 50;
    host.game_options[RS_CS2_GAMEOPTION_AREA_VOLUME] = 75;
    host.audio_settings_dirty = true;
    CHECK(RS_CS2Host_TakeAudioSettings(&host, &settings), "dirty settings snapshot available");
    CHECK(settings.master == 80, "master option preserved");
    CHECK(settings.music == 25, "music option preserved");
    CHECK(settings.sounds == 50, "effects option preserved");
    CHECK(settings.area_sounds == 75, "area option preserved");
    CHECK(!RS_CS2Host_TakeAudioSettings(&host, &settings), "snapshot coalesces until next write");

    /* Mute icons write only the backing varp. The varp side effect must update
     * the same option snapshot as a slider's GAMEOPTION/DEVICEOPTION opcode. */
    VarPManager_Init(&varps);
    types = calloc(RS_CS2_VARP_AREA_OVERRIDE_VOLUME + 1, sizeof(*types));
    CHECK(types != NULL, "audio varp test types allocated");
    if( types )
    {
        CHECK(
            VarPManager_SetVarpTypes(
                &varps, types, RS_CS2_VARP_AREA_OVERRIDE_VOLUME + 1),
            "audio varp test store initialized");
        host.varps = &varps;
        host.audio_settings_dirty = false;
        VarPManager_SetVarpOptimistic(&varps, RS_CS2_VARP_MASTER_VOLUME, 0);
        RS_CS2Host_SyncAudioVarp(&host, RS_CS2_VARP_MASTER_VOLUME);
        CHECK(RS_CS2Host_TakeAudioSettings(&host, &settings), "master mute varp dirties settings");
        CHECK(settings.master == 0, "master mute varp reaches device option");

        VarPManager_SetVarpOptimistic(&varps, RS_CS2_VARP_MUSIC_VOLUME, 57);
        RS_CS2Host_SyncAudioVarp(&host, RS_CS2_VARP_MUSIC_VOLUME);
        CHECK(RS_CS2Host_TakeAudioSettings(&host, &settings), "music mute varp dirties settings");
        CHECK(settings.music == 57, "music varp reaches game option");

        VarPManager_SetVarpOptimistic(&varps, RS_CS2_VARP_AREA_OVERRIDE_ENABLED, 1);
        VarPManager_SetVarpOptimistic(&varps, RS_CS2_VARP_AREA_OVERRIDE_VOLUME, 42);
        RS_CS2Host_SyncAudioVarp(&host, RS_CS2_VARP_AREA_OVERRIDE_VOLUME);
        CHECK(RS_CS2Host_TakeAudioSettings(&host, &settings), "area override dirties settings");
        CHECK(settings.area_sounds == 42, "active area override reaches game option");
    }
    free(types);
    VarPManager_Free(&varps);
}

/** One frame: tick the game's sound queue, pump async loads, hand the platform
 *  whatever came out. Exactly the order main.c uses. */
static struct ToriDraw_Scene* g_scene = NULL;

static void
frame(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriRS_AudioQueue* queue,
    struct PlatformAudio* platform)
{
    struct ToriRS_AudioCommand commands[TORIRS_AUDIO_QUEUE_MAX];
    int count;

    /* No world and no device: the listener is the origin and the music player
     * synthesises nothing, which is what a headless run looks like. */
    RS_Audio_Tick(audio, provider, runner, g_scene, NULL, 0, 0, 0, NULL, queue);
    /* Loads the tick queued must complete before the next tick can see them. */
    if( runner )
        TaskRunner_Drain(runner);
    count = ToriRS_AudioQueue_Drain(queue, commands, TORIRS_AUDIO_QUEUE_MAX);
    PlatformAudio_SubmitAll(platform, commands, count);
    /* Mixing is what turns an accepted command into audible samples, and the
     * peak this leaves behind is what the audibility checks read. */
    PlatformAudio_Update(platform);
}

/** Run frames until `audio->played` grows, or `limit` frames pass. */
static bool
frames_until_play(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriRS_AudioQueue* queue,
    struct PlatformAudio* platform,
    int limit)
{
    int before = audio->played;
    for( int i = 0; i < limit; i++ )
    {
        frame(audio, provider, runner, queue, platform);
        if( audio->played > before )
            return true;
    }
    return false;
}

/** Let whatever is playing finish, so the overlap rule does not reject the next
 *  clip for reasons the caller did not intend to test. */
static void
frames_until_silent(
    struct RS_Audio* audio,
    struct CacheProvider* provider,
    struct TaskRunner* runner,
    struct ToriRS_AudioQueue* queue,
    struct PlatformAudio* platform)
{
    while( audio->tick <= audio->last_play_tick + audio->last_play_length_ticks )
        frame(audio, provider, runner, queue, platform);
}

/* --- queue behaviour, no cache ------------------------------------------- */

static void
test_queue_without_cache(void)
{
    struct RS_Audio audio;
    struct ToriRS_AudioQueue queue;
    struct PlatformAudio* platform = PlatformAudio_New();

    printf("queue (no cache)\n");
    PlatformAudio_Init(platform, TORIRS_AUDIO_SAMPLE_RATE);
    RS_Audio_Init(&audio);
    ToriRS_AudioQueue_Reset(&queue);

    CHECK(audio.enabled, "sound starts enabled");
    CHECK(
        audio.effect_volume == RS_AUDIO_DEFAULT_VOLUME,
        "effect volume starts at the reference default");

    /* An effect that will never become resident is dropped, not leaked. */
    RS_Audio_Synth(&audio, 12345, 1, 0);
    CHECK(audio.count == 1, "synth packet queued");
    for( int i = 0; i < RS_AUDIO_LOAD_WAIT_TICKS + 2; i++ )
        frame(&audio, NULL, NULL, &queue, platform);
    CHECK(audio.count == 0, "entry dropped after the load wait");
    CHECK(audio.dropped_absent == 1, "drop counted as absent");
    CHECK(audio.played == 0, "nothing played");

    /* Volume levels: the reference's 128/96/64/32/mute ladder. */
    RS_Audio_SetVolumeLevel(&audio, 0, &queue);
    CHECK(audio.effect_volume == 255 && audio.enabled, "level 0 -> full");
    RS_Audio_SetVolumeLevel(&audio, 3, &queue);
    CHECK(audio.effect_volume == 64 && audio.enabled, "level 3 -> quarter");
    RS_Audio_SetVolumeLevel(&audio, 4, &queue);
    CHECK(!audio.enabled, "level 4 -> muted");
    RS_Audio_Synth(&audio, 0, 1, 0);
    CHECK(audio.count == 0, "muted: packets are not queued at all");
    RS_Audio_SetVolumeLevel(&audio, 2, &queue);
    CHECK(audio.effect_volume == 128 && audio.enabled, "level 2 -> half, re-enabled");
    RS_Audio_SetVolumeLevel(&audio, 9, &queue);
    CHECK(audio.effect_volume == 128, "unknown level ignored");

    /* The volume commands reached the platform. */
    {
        struct ToriRS_AudioCommand commands[TORIRS_AUDIO_QUEUE_MAX];
        int count = ToriRS_AudioQueue_Drain(&queue, commands, TORIRS_AUDIO_QUEUE_MAX);
        PlatformAudio_SubmitAll(platform, commands, count);
        /* Two buses per accepted level change; the unknown level pushed none. */
        CHECK(count == 8, "effects and area buses set on every accepted level change");
        CHECK(
            PlatformAudio_Stats(platform).bus_volume[TORIRS_AUDIO_BUS_EFFECTS] == 128,
            "platform ended at half volume on the effects bus");
    }

    /* Interface sliders are independent percentages under one master gain.
     * Stored bus values remain pre-master so unmuting restores them exactly. */
    ToriRS_AudioQueue_Reset(&queue);
    RS_Audio_SetMasterVolume(&audio, 128, &queue);
    RS_Audio_SetBusVolume(&audio, TORIRS_AUDIO_BUS_EFFECTS, 200, &queue);
    RS_Audio_SetBusVolume(&audio, TORIRS_AUDIO_BUS_AREA, 100, &queue);
    RS_Audio_SetBusVolume(&audio, TORIRS_AUDIO_BUS_MUSIC, 50, &queue);
    {
        struct ToriRS_AudioCommand commands[TORIRS_AUDIO_QUEUE_MAX];
        int count = ToriRS_AudioQueue_Drain(&queue, commands, TORIRS_AUDIO_QUEUE_MAX);
        struct PlatformAudioStats stats;
        PlatformAudio_SubmitAll(platform, commands, count);
        stats = PlatformAudio_Stats(platform);
        CHECK(audio.master_volume == 128, "master setting retained");
        CHECK(audio.effect_volume == 200, "effects setting retained before master");
        CHECK(stats.bus_volume[TORIRS_AUDIO_BUS_EFFECTS] == 100, "master scales effects once");
        CHECK(stats.bus_volume[TORIRS_AUDIO_BUS_AREA] == 50, "master scales area once");
        CHECK(stats.bus_volume[TORIRS_AUDIO_BUS_MUSIC] == 25, "master scales music once");
    }
    ToriRS_AudioQueue_Reset(&queue);
    RS_Audio_SetMasterVolume(&audio, 0, &queue);
    {
        struct ToriRS_AudioCommand commands[TORIRS_AUDIO_QUEUE_MAX];
        int count = ToriRS_AudioQueue_Drain(&queue, commands, TORIRS_AUDIO_QUEUE_MAX);
        struct PlatformAudioStats stats;
        PlatformAudio_SubmitAll(platform, commands, count);
        stats = PlatformAudio_Stats(platform);
        CHECK(stats.bus_volume[TORIRS_AUDIO_BUS_EFFECTS] == 0, "master mute silences effects");
        CHECK(stats.bus_volume[TORIRS_AUDIO_BUS_AREA] == 0, "master mute silences area");
        CHECK(stats.bus_volume[TORIRS_AUDIO_BUS_MUSIC] == 0, "master mute silences music");
    }

    /* Queue cap is the reference's 50. */
    RS_Audio_Init(&audio);
    for( int i = 0; i < RS_AUDIO_QUEUE_MAX + 5; i++ )
        RS_Audio_Synth(&audio, i, 1, 0);
    CHECK(audio.count == RS_AUDIO_QUEUE_MAX, "queue caps at RS_AUDIO_QUEUE_MAX");
    CHECK(audio.dropped_queue_full == 5, "overflow counted");

    RS_Audio_Shutdown(&audio);
    PlatformAudio_Free(platform);
}

/* --- against a real cache ------------------------------------------------ */

struct harness
{
    struct RS_Audio audio;
    struct ToriRS_AudioQueue queue;
    struct PlatformAudio* platform;
    struct CacheProvider* provider;
    struct TaskRunner runner;
    struct ToriRS_IO* io;
    struct ToriRS_TaskQueue* task_queue;
    struct PlatformX_IO* px;
    struct Dat1BuildCache* dat1_bc;
    struct Dat2BuildCache* dat2_bc;
    struct RSCache_Dat1Disk* dat1_disk;
    struct RSCache_Dat2Disk* dat2_disk;
};

static void
harness_free(struct harness* harness)
{
    RS_Audio_Shutdown(&harness->audio);
    PlatformAudio_Free(harness->platform);
    if( harness->task_queue )
        ToriRS_TaskQueue_Free(harness->task_queue);
    if( harness->io )
        ToriRS_IO_Free(harness->io);
    PlatformX_IO_Free(harness->px);
    if( harness->dat1_bc )
        dat1_buildcache_free(harness->dat1_bc);
    if( harness->dat2_bc )
        dat2_buildcache_free(harness->dat2_bc);
    if( harness->dat1_disk )
        RSCache_Dat1DiskFree(harness->dat1_disk);
    if( harness->dat2_disk )
        RSCache_Dat2DiskFree(harness->dat2_disk);
    memset(harness, 0, sizeof(*harness));
}

static bool
harness_init(
    struct harness* harness,
    const char* cache_dir,
    bool dat1,
    const struct RSCache* profile)
{
    struct stat info;

    memset(harness, 0, sizeof(*harness));
    if( stat(cache_dir, &info) != 0 )
    {
        printf("  SKIP: %s not present\n", cache_dir);
        return false;
    }

    /* A scene per harness, like a scene per boot: assets from one cache must
     * not be visible to the next, or "how many assets are live" means nothing. */
    ToriDraw_SceneFree(g_scene);
    g_scene = ToriDraw_SceneNew(0);

    harness->platform = PlatformAudio_New();
    PlatformAudio_Init(harness->platform, TORIRS_AUDIO_SAMPLE_RATE);
    RS_Audio_Init(&harness->audio);
    /* State the era the cache belongs to: audio behaviour depends on it, and a
     * harness that leaves it unstated tests whichever default happens to be
     * compiled in rather than what either era does. */
    RS_Audio_SetFeatures(
        &harness->audio, dat1 ? ToriRS_Features_LostCity() : ToriRS_Features_OSRS());
    ToriRS_AudioQueue_Reset(&harness->queue);

    harness->io = ToriRS_IO_New();
    harness->task_queue = ToriRS_TaskQueue_New();
    harness->px = PlatformX_IO_New();

    if( dat1 )
    {
        harness->dat1_disk = RSCache_Dat1DiskNewFromDirectory(cache_dir);
        if( !harness->dat1_disk )
        {
            printf("  SKIP: could not open dat1 cache %s\n", cache_dir);
            harness_free(harness);
            return false;
        }
        PlatformX_IO_InitDat1Disk(harness->px, harness->dat1_disk);
        harness->dat1_bc = dat1_buildcache_new();
        harness->provider = dat1_buildcache_as_provider(harness->dat1_bc);
    }
    else
    {
        harness->dat2_disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
        if( !harness->dat2_disk )
        {
            printf("  SKIP: could not open dat2 cache %s\n", cache_dir);
            harness_free(harness);
            return false;
        }
        RSCache_Dat2DiskSetProfile(harness->dat2_disk, profile);
        PlatformX_IO_InitDat2Disk(harness->px, harness->dat2_disk);
        harness->dat2_bc = dat2_buildcache_new();
        harness->provider = dat2_buildcache_as_provider(harness->dat2_bc);
    }
    CacheProvider_SetProfile(harness->provider, profile);

    harness->runner.queue = harness->task_queue;
    harness->runner.io = harness->io;
    harness->runner.px = harness->px;
    return true;
}

/**
 * The core claim: a packet in, an audible clip out.
 *
 * Three effects are played one after another (waiting for each to finish) rather
 * than one, because a single id that happened to be an empty program would make
 * a broken pipeline look fine.
 */
static void
test_cache_pipeline(
    const char* label,
    const char* cache_dir,
    bool dat1,
    const struct RSCache* profile)
{
    struct harness harness;
    int played_ids[3] = { -1, -1, -1 };
    int audible = 0;

    printf("pipeline: %s\n", label);
    if( !harness_init(&harness, cache_dir, dat1, profile) )
        return;

    for( int index = 0; index < 3; index++ )
    {
        int sound_id = index;
        char message[96];

        frames_until_silent(
            &harness.audio, harness.provider, &harness.runner, &harness.queue, harness.platform);
        RS_Audio_Synth(&harness.audio, sound_id, 1, 0);

        snprintf(message, sizeof(message), "%s: effect %d played", label, sound_id);
        CHECK(
            frames_until_play(
                &harness.audio,
                harness.provider,
                &harness.runner,
                &harness.queue,
                harness.platform,
                RS_AUDIO_LOAD_WAIT_TICKS),
            message);

        played_ids[index] = PlatformAudioNull_LastVoiceSource(harness.platform);
        if( PlatformAudioNull_LastPeak(harness.platform) > 0 )
            audible++;
    }

    CHECK(played_ids[0] == 0 && played_ids[1] == 1 && played_ids[2] == 2,
          "the platform saw each effect id in order");
    CHECK(audible == 3, "every clip mixed to non-silent samples");
    CHECK(PlatformAudio_Stats(harness.platform).voices_started == 3, "three voices started");
    CHECK(
        PlatformAudio_Stats(harness.platform).voices_rejected == 0,
        "no voice named an asset the backend lacked");
    CHECK(
        PlatformAudio_Stats(harness.platform).assets_live == 3,
        "one retained asset per distinct effect");

    /* Delay: the server's tick count is honoured, plus the effect's own trim. */
    {
        struct ToriDraw_Sound* sound;
        int before;

        frames_until_silent(
            &harness.audio, harness.provider, &harness.runner, &harness.queue, harness.platform);
        sound = ToriDraw_SceneSoundGet(g_scene, 0);
        CHECK(sound != NULL, "effect 0 is a published scene asset after playing");
        RS_Audio_Synth(&harness.audio, 0, 1, 10);
        before = harness.audio.tick;
        CHECK(
            frames_until_play(
                &harness.audio,
                harness.provider,
                &harness.runner,
                &harness.queue,
                harness.platform,
                200),
            "delayed effect eventually played");
        /* The reference's schedule exactly: a queue pass with delay > 0
         * decrements and does nothing, so a delay of D plays on pass D + 1. */
        CHECK(
            harness.audio.tick - before == 10 + (sound ? sound->queue_delay : 0) + 1,
            "played on pass delay + trim + 1");
    }

    /*
     * Loop repeats are now the mixer's job rather than a pre-expanded buffer,
     * so what is checked here is that the clip's loop span survived the trip
     * into the scene asset -- which is what the mixer loops on.
     */
    {
        struct ToriDraw_Sound* published = NULL;
        for( int id = 0; id < 200 && !published; id++ )
        {
            struct ToriDraw_Sound* candidate;
            struct ToriRS_Task* task = CreateTask_SoundLoad(harness.provider, id);
            if( task )
            {
                ToriRS_TaskQueue_Add(harness.runner.queue, task);
                TaskRunner_Drain(&harness.runner);
            }
            RS_Audio_Synth(&harness.audio, id, 1, 0);
            frame(
                &harness.audio,
                harness.provider,
                &harness.runner,
                &harness.queue,
                harness.platform);
            candidate = ToriDraw_SceneSoundGet(g_scene, id);
            if( candidate && candidate->loop_start >= 0 &&
                candidate->loop_end > candidate->loop_start )
                published = candidate;
        }
        if( !published )
            printf("  SKIP: %s has no looping effect in the first 200 ids\n", label);
        else
            CHECK(
                published->loop_end > published->loop_start &&
                    published->loop_end <= published->sample_count,
                "the published asset carries a usable loop span");
    }

    harness_free(&harness);
}

/**
 * The overlap rule: a short clip arriving while a long one plays is dropped.
 *
 * Reference behaviour, and worth its own test because the alternative — cutting
 * the long sound off — is what a naive queue does and sounds obviously wrong.
 */
static void
test_overlap_rule(
    const char* cache_dir,
    const struct RSCache* profile)
{
    struct harness harness;
    int longest_id = -1;
    int shortest_id = -1;
    int longest_samples = 0;
    int shortest_samples = 1 << 30;

    printf("overlap rule\n");
    if( !harness_init(&harness, cache_dir, true, profile) )
        return;

    /* Find a long effect and a short one to collide. */
    for( int id = 0; id < 60; id++ )
    {
        struct ToriRS_Sound* sound;
        struct ToriRS_Task* task = CreateTask_SoundLoad(harness.provider, id);
        if( task )
        {
            ToriRS_TaskQueue_Add(harness.runner.queue, task);
            TaskRunner_Drain(&harness.runner);
        }
        sound = CacheProvider_SoundGet(harness.provider, id);
        if( !sound )
            continue;
        if( sound->sample_count > longest_samples )
        {
            longest_samples = sound->sample_count;
            longest_id = id;
        }
        if( sound->sample_count < shortest_samples )
        {
            shortest_samples = sound->sample_count;
            shortest_id = id;
        }
    }

    if( longest_id < 0 || shortest_id < 0 || longest_id == shortest_id )
    {
        printf("  SKIP: no long/short pair available\n");
        harness_free(&harness);
        return;
    }

    RS_Audio_Synth(&harness.audio, longest_id, 1, 0);
    CHECK(
        frames_until_play(
            &harness.audio,
            harness.provider,
            &harness.runner,
            &harness.queue,
            harness.platform,
            50),
        "long effect played");

    RS_Audio_Synth(&harness.audio, shortest_id, 1, 0);
    frame(&harness.audio, harness.provider, &harness.runner, &harness.queue, harness.platform);
    CHECK(harness.audio.dropped_overlap == 1, "short effect refused under the long one");
    CHECK(PlatformAudioNull_LastVoiceSource(harness.platform) == longest_id,
          "the platform's last voice is still the long clip");

    /* Once it has finished, the short one is welcome. */
    frames_until_silent(
        &harness.audio, harness.provider, &harness.runner, &harness.queue, harness.platform);
    RS_Audio_Synth(&harness.audio, shortest_id, 1, 0);
    CHECK(
        frames_until_play(
            &harness.audio,
            harness.provider,
            &harness.runner,
            &harness.queue,
            harness.platform,
            50),
        "short effect plays once the long one ends");
    CHECK(PlatformAudioNull_LastVoiceSource(harness.platform) == shortest_id,
          "the platform saw the short clip");

    harness_free(&harness);
}

/**
 * The same collision on the modern era: both sounds play.
 *
 * The counterpart to the rule above, and the reason the rule is gated. The
 * modern client's mixer holds eight priority lists and mixes all of them;
 * applying the 2004 monophony to it silently drops most of a combat tick, where
 * a hit splat, a block and a special land within a few ticks of each other.
 */
static void
test_polyphonic_era(
    const char* cache_dir,
    const struct RSCache* profile)
{
    struct harness harness;

    printf("polyphony (modern era)\n");
    if( !harness_init(&harness, cache_dir, false, profile) )
        return;

    RS_Audio_Synth(&harness.audio, 0, 1, 0);
    CHECK(
        frames_until_play(
            &harness.audio, harness.provider, &harness.runner, &harness.queue, harness.platform,
            RS_AUDIO_LOAD_WAIT_TICKS),
        "first effect played");
    RS_Audio_Synth(&harness.audio, 1, 1, 0);
    CHECK(
        frames_until_play(
            &harness.audio, harness.provider, &harness.runner, &harness.queue, harness.platform,
            RS_AUDIO_LOAD_WAIT_TICKS),
        "second effect played over the first");
    CHECK(harness.audio.dropped_overlap == 0, "nothing refused for overlapping");
    CHECK(
        PlatformAudio_Stats(harness.platform).voices_live >= 1,
        "the backend is sounding at least one voice");

    harness_free(&harness);
}

int
main(void)
{
    struct RSCache dat1_profile;
    struct RSCache dat2_profile;

    /* Clips are scene assets now, so the game layer needs a scene to publish
     * into even with nothing to draw. */
    g_scene = ToriDraw_SceneNew(0);

    test_audio_settings_snapshot();
    test_queue_without_cache();

    if( !RSCache_ProfileByName("lc254", &dat1_profile) )
    {
        printf("SKIP: lc254 profile unavailable\n");
        return g_fail != 0;
    }
    if( !RSCache_ProfileByName("osrs230", &dat2_profile) )
    {
        printf("SKIP: osrs230 profile unavailable\n");
        return g_fail != 0;
    }

    test_cache_pipeline("dat1 254", REPO_ROOT "/cache254.lostcity", true, &dat1_profile);
    test_cache_pipeline("dat2 230", REPO_ROOT "/cache.osrs230", false, &dat2_profile);
    test_overlap_rule(REPO_ROOT "/cache254.lostcity", &dat1_profile);
    test_polyphonic_era(REPO_ROOT "/cache.osrs230", &dat2_profile);

    ToriDraw_SceneFree(g_scene);
    g_scene = NULL;

    if( g_fail )
        printf("rs_audio_test: %d FAILED\n", g_fail);
    else
        printf("rs_audio_test: all checks passed\n");
    return g_fail != 0;
}
