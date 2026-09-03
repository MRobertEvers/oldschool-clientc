#include "platform/platform_audio.h"

#include "log/torirs_log.h"
#include "platform/platform_android.h"
#include "platform/platform_audio_capture.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <SLES/OpenSLES_AndroidConfiguration.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Android audio backend — OpenSL ES, Android simple buffer queue.
 *
 * The shape is the SDL2 backend's, not the web one's: a real device with a
 * real-time callback that pulls from `ToriRS_Mixer` at the device's own
 * cadence, while the game thread submits commands under a lock. That is what
 * keeps audio continuous across the frame stalls this lane is full of -- a
 * world rebuild or a cache stall on a 2013 phone is hundreds of milliseconds,
 * and a design that fed the device from the frame loop would turn every one of
 * them into a gap in the music.
 *
 * ## Why OpenSL ES and not AAudio
 *
 * AAudio is the newer API and the one with the low-latency path, and it does
 * not exist before API 26. This lane's floor is API 21 and its reference device
 * is a 2013 armv7 phone (android/README.md), so AAudio would be a second
 * backend that the machine this lane exists to serve could never take. OpenSL
 * ES has been present since API 9 and is one implementation for every device
 * the APK installs on. Latency is not the constraint here either: effects are
 * fired on a 600ms server tick and the buffer below is 80ms.
 *
 * ## What Android forces on the design
 *
 *   - **22050Hz is not a device rate.** Every asset in every cache is 22050Hz
 *     (TORIRS_AUDIO_SAMPLE_RATE) and phones run their mixer at 44100 or 48000,
 *     so AudioFlinger resamples. Asking for 22050 anyway is deliberate: the
 *     alternative is resampling every clip, every voice and every synthesised
 *     music frame on the CPU this lane is short of, to hand the platform
 *     something it will touch again regardless.
 *   - **The stream type has to be stated.** Without SL_ANDROID_STREAM_MEDIA the
 *     player can land on a stream the volume rocker does not control while the
 *     game is in front, which reads as "the volume keys do nothing".
 *   - **Nothing stops audio when the activity goes away.** The frame loop keeps
 *     running with no Surface (platform_android.c's Present), so unless the
 *     player is paused the client keeps singing from the recents list. The
 *     Surface is the signal the lane already has for "the activity is stopped";
 *     Update reads it once a frame.
 *   - **The callback thread is not a JVM thread.** Nothing here may call JNI.
 *     It renders the mix and enqueues; that is the whole job.
 */

/**
 * Buffers in flight. Four 20ms blocks is 80ms of queue.
 *
 * Two would be the low-latency choice and is what a phone with a clean
 * scheduler wants; it is not what this lane's device has. Under a world
 * rebuild the audio thread competes with a frame thread and (on the dual-core
 * lane) a model-stage worker for two cores, and 40ms of slack is inside the
 * scheduling noise that produces. The cost is 80ms of latency on a sound
 * effect whose trigger already arrived on a 600ms tick.
 */
#define OPENSLES_BUFFER_COUNT 4

/** One 50Hz tick per buffer, matching the block size every other backend uses. */
#define OPENSLES_BLOCK_DIVISOR 50

/** Diagnostics-only ring for TORIRS_AUDIO_WAV. Shorter than the desktop's 120s:
 *  this is a device where 10MB of capture can be the allocation that ends the
 *  process, and 30s is long enough to hear what went wrong. */
#define OPENSLES_CAPTURE_RING_SECONDS 30

struct PlatformAudio
{
    SLObjectItf engine_object;
    SLEngineItf engine;
    SLObjectItf output_mix_object;
    SLObjectItf player_object;
    SLPlayItf player;
    SLAndroidSimpleBufferQueueItf queue;

    int sample_rate;
    bool device_open;
    /** False while the activity is stopped and the player is parked. */
    bool playing;

    /*
     * The mixer, and the lock that makes it shared state.
     *
     * Held by the game thread across a submit and by the audio thread across a
     * render. Both are short and neither allocates, so this is a plain mutex
     * rather than anything cleverer -- the same trade SDL_LockAudioDevice makes
     * on the desktop lane.
     */
    struct ToriRS_Mixer mixer;
    pthread_mutex_t lock;

    /** OPENSLES_BUFFER_COUNT blocks, cycled round-robin. Enqueue takes the
     *  buffer by address and the device reads it until the next callback, so
     *  these can never be one buffer reused. */
    int16_t* buffers;
    int block_frames;
    int next_buffer;

    int commands;
    int frames_played;

    struct PlatformAudioCapture* capture;

    /* Real-time path diagnostics, read by PlatformAudio_Stats. */
    int callbacks;
    int callback_underruns;
    uint64_t last_callback_ns;
    double callback_interval_min_ms;
    double callback_interval_max_ms;
    double callback_interval_sum_ms;
    double callback_period_ms;
    double callback_jitter_max_ms;
    double render_max_ms;

    int stream_ring_samples;
    int stream_ring_min_frames;
    int stream_ring_max_frames;
    uint64_t stream_ring_sum_frames;
    int stream_ring_current_frames;
};

static uint64_t
now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/** How much music is buffered ahead, sampled where the mix is already locked. */
static void
sample_stream_ring(struct PlatformAudio* audio)
{
    int buffered = 0;
    bool active = false;

    for( int i = 0; i < TORIRS_MIXER_MAX_STREAMS; i++ )
    {
        if( audio->mixer.streams[i].stream_id < 0 )
            continue;
        buffered += audio->mixer.streams[i].buffered_frames;
        active = true;
    }
    if( !active )
        return;
    if( audio->stream_ring_samples == 0 || buffered < audio->stream_ring_min_frames )
        audio->stream_ring_min_frames = buffered;
    if( buffered > audio->stream_ring_max_frames )
        audio->stream_ring_max_frames = buffered;
    audio->stream_ring_current_frames = buffered;
    audio->stream_ring_sum_frames += (uint64_t)buffered;
    audio->stream_ring_samples++;
}

static void
note_callback_interval(struct PlatformAudio* audio)
{
    uint64_t const now = now_ns();

    if( audio->last_callback_ns != 0 )
    {
        double const interval_ms = (double)(now - audio->last_callback_ns) / 1000000.0;

        if( audio->callbacks > 1 )
        {
            if( interval_ms < audio->callback_interval_min_ms )
                audio->callback_interval_min_ms = interval_ms;
            if( interval_ms > audio->callback_interval_max_ms )
                audio->callback_interval_max_ms = interval_ms;
        }
        else
        {
            audio->callback_interval_min_ms = interval_ms;
            audio->callback_interval_max_ms = interval_ms;
        }
        audio->callback_interval_sum_ms += interval_ms;
        if( audio->callback_period_ms > 0.0 )
        {
            double const jitter_ms = fabs(interval_ms - audio->callback_period_ms);

            if( jitter_ms > audio->callback_jitter_max_ms )
                audio->callback_jitter_max_ms = jitter_ms;
            /*
             * A callback that arrives more than one period late means the queue
             * ran dry for the difference. The device filled it with silence and
             * said nothing, which is why this is counted here rather than read
             * back from OpenSL -- the buffer queue reports its depth, not the
             * gaps it played.
             */
            if( interval_ms > audio->callback_period_ms * 1.5 )
            {
                int const missed =
                    (int)floor(interval_ms / audio->callback_period_ms + 0.5) - 1;
                audio->callback_underruns += missed > 0 ? missed : 1;
            }
        }
    }
    audio->last_callback_ns = now;
    audio->callbacks++;
}

/**
 * Render the next block and hand it back to the device.
 *
 * Runs on OpenSL's own thread. Real-time: the only things it does are take the
 * mixer lock, mix, and enqueue.
 */
static void
buffer_callback(
    SLAndroidSimpleBufferQueueItf queue,
    void* context)
{
    struct PlatformAudio* audio = context;
    int16_t* block;
    uint64_t render_start;
    double render_ms;

    assert(audio);
    block = audio->buffers +
            (size_t)audio->next_buffer * audio->block_frames * TORIRS_AUDIO_CHANNELS;
    audio->next_buffer = (audio->next_buffer + 1) % OPENSLES_BUFFER_COUNT;

    pthread_mutex_lock(&audio->lock);
    note_callback_interval(audio);
    sample_stream_ring(audio);
    render_start = now_ns();
    ToriRS_Mixer_Render(&audio->mixer, block, audio->block_frames);
    render_ms = (double)(now_ns() - render_start) / 1000000.0;
    if( render_ms > audio->render_max_ms )
        audio->render_max_ms = render_ms;
    audio->frames_played += audio->block_frames;
    pthread_mutex_unlock(&audio->lock);

    if( audio->capture )
        PlatformAudioCapture_Push(audio->capture, block, audio->block_frames);

    (*queue)->Enqueue(
        queue,
        block,
        (SLuint32)audio->block_frames * TORIRS_AUDIO_CHANNELS * sizeof(int16_t));
}

/** Tear down whatever Init managed to create. Safe at any stage of it. */
static void
close_device(struct PlatformAudio* audio)
{
    assert(audio);
    if( audio->player_object )
    {
        /* Stop before Destroy: SetPlayState(STOPPED) is what guarantees the
         * callback is not running when the buffers under it go away. */
        if( audio->player )
            (*audio->player)->SetPlayState(audio->player, SL_PLAYSTATE_STOPPED);
        if( audio->queue )
            (*audio->queue)->Clear(audio->queue);
        (*audio->player_object)->Destroy(audio->player_object);
        audio->player_object = NULL;
        audio->player = NULL;
        audio->queue = NULL;
    }
    if( audio->output_mix_object )
    {
        (*audio->output_mix_object)->Destroy(audio->output_mix_object);
        audio->output_mix_object = NULL;
    }
    if( audio->engine_object )
    {
        (*audio->engine_object)->Destroy(audio->engine_object);
        audio->engine_object = NULL;
        audio->engine = NULL;
    }
    audio->device_open = false;
    audio->playing = false;
}

struct PlatformAudio*
PlatformAudio_New(void)
{
    struct PlatformAudio* audio = calloc(1, sizeof(*audio));

    assert(audio);
    pthread_mutex_init(&audio->lock, NULL);
    audio->sample_rate = TORIRS_AUDIO_SAMPLE_RATE;
    ToriRS_Mixer_Init(&audio->mixer, audio->sample_rate);
    return audio;
}

bool
PlatformAudio_Init(
    struct PlatformAudio* audio,
    int sample_rate)
{
    SLDataLocator_AndroidSimpleBufferQueue queue_locator;
    SLDataFormat_PCM format;
    SLDataSource source;
    SLDataLocator_OutputMix mix_locator;
    SLDataSink sink;
    SLAndroidConfigurationItf configuration;
    SLint32 stream_type;
    SLresult result;
    SLInterfaceID const player_ids[] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                         SL_IID_ANDROIDCONFIGURATION };
    SLboolean const player_required[] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };

    assert(audio);

    audio->sample_rate = sample_rate > 0 ? sample_rate : TORIRS_AUDIO_SAMPLE_RATE;
    ToriRS_Mixer_Init(&audio->mixer, audio->sample_rate);
    audio->block_frames = audio->sample_rate / OPENSLES_BLOCK_DIVISOR;
    audio->callback_period_ms =
        (double)audio->block_frames * 1000.0 / (double)audio->sample_rate;

    result = slCreateEngine(&audio->engine_object, 0, NULL, 0, NULL, NULL);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: slCreateEngine failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }
    result = (*audio->engine_object)->Realize(audio->engine_object, SL_BOOLEAN_FALSE);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: engine Realize failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }
    result = (*audio->engine_object)->GetInterface(
        audio->engine_object, SL_IID_ENGINE, &audio->engine);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: SL_IID_ENGINE failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }

    /* No interfaces asked of the output mix. Environmental reverb is the only
     * one worth having and no phone this lane targets implements it. */
    result = (*audio->engine)->CreateOutputMix(
        audio->engine, &audio->output_mix_object, 0, NULL, NULL);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: CreateOutputMix failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }
    result =
        (*audio->output_mix_object)->Realize(audio->output_mix_object, SL_BOOLEAN_FALSE);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: output mix Realize failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }

    memset(&queue_locator, 0, sizeof(queue_locator));
    queue_locator.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
    queue_locator.numBuffers = OPENSLES_BUFFER_COUNT;

    memset(&format, 0, sizeof(format));
    format.formatType = SL_DATAFORMAT_PCM;
    format.numChannels = TORIRS_AUDIO_CHANNELS;
    /* samplesPerSec is milliHertz in OpenSL ES, not Hz. A field named for one
     * unit and specified in another is the single easiest thing to get wrong
     * here, and getting it wrong is a player that opens and plays at 1/1000th
     * speed or refuses the format outright. */
    format.samplesPerSec = (SLuint32)audio->sample_rate * 1000u;
    format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    format.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
    format.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    format.endianness = SL_BYTEORDER_LITTLEENDIAN;

    source.pLocator = &queue_locator;
    source.pFormat = &format;

    mix_locator.locatorType = SL_DATALOCATOR_OUTPUTMIX;
    mix_locator.outputMix = audio->output_mix_object;
    sink.pLocator = &mix_locator;
    sink.pFormat = NULL;

    result = (*audio->engine)->CreateAudioPlayer(
        audio->engine,
        &audio->player_object,
        &source,
        &sink,
        (SLuint32)(sizeof(player_ids) / sizeof(player_ids[0])),
        player_ids,
        player_required);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR(
            "audio: CreateAudioPlayer failed (0x%x) for %dHz x%d\n",
            (unsigned)result,
            audio->sample_rate,
            TORIRS_AUDIO_CHANNELS);
        close_device(audio);
        return false;
    }

    /*
     * The stream type, before Realize. SLAndroidConfigurationItf is the one
     * interface OpenSL ES allows to be fetched from an unrealized object, and
     * this is why: the setting has to be in place before the player is bound to
     * an output.
     *
     * MEDIA is what a game belongs on. It is what the volume rocker adjusts
     * while the app is in front, and it is the stream Do Not Disturb and the
     * media notification act on.
     */
    result = (*audio->player_object)->GetInterface(
        audio->player_object, SL_IID_ANDROIDCONFIGURATION, &configuration);
    if( result == SL_RESULT_SUCCESS )
    {
        stream_type = SL_ANDROID_STREAM_MEDIA;
        (*configuration)->SetConfiguration(
            configuration,
            SL_ANDROID_KEY_STREAM_TYPE,
            &stream_type,
            sizeof(stream_type));
    }

    result = (*audio->player_object)->Realize(audio->player_object, SL_BOOLEAN_FALSE);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: player Realize failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }
    result = (*audio->player_object)->GetInterface(
        audio->player_object, SL_IID_PLAY, &audio->player);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: SL_IID_PLAY failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }
    result = (*audio->player_object)->GetInterface(
        audio->player_object, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &audio->queue);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: SL_IID_ANDROIDSIMPLEBUFFERQUEUE failed (0x%x)\n",
            (unsigned)result);
        close_device(audio);
        return false;
    }
    result = (*audio->queue)->RegisterCallback(audio->queue, buffer_callback, audio);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: RegisterCallback failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }

    audio->buffers = calloc(
        (size_t)OPENSLES_BUFFER_COUNT * audio->block_frames * TORIRS_AUDIO_CHANNELS,
        sizeof(int16_t));
    assert(audio->buffers);
    audio->capture =
        PlatformAudioCapture_Open(audio->sample_rate, OPENSLES_CAPTURE_RING_SECONDS);

    /*
     * Prime the queue before starting. The callback is what refills it, so it
     * has to be given something to complete; enqueueing silence rather than a
     * mixed block keeps the very first render on the audio thread, where every
     * later one happens.
     *
     * The mixer still gets one render here, discarded: it grows its
     * accumulator on the first call, and that allocation must not happen on the
     * real-time thread.
     */
    ToriRS_Mixer_Render(&audio->mixer, audio->buffers, audio->block_frames);
    memset(
        audio->buffers,
        0,
        (size_t)OPENSLES_BUFFER_COUNT * audio->block_frames * TORIRS_AUDIO_CHANNELS *
            sizeof(int16_t));
    for( int i = 0; i < OPENSLES_BUFFER_COUNT; i++ )
    {
        result = (*audio->queue)->Enqueue(
            audio->queue,
            audio->buffers + (size_t)i * audio->block_frames * TORIRS_AUDIO_CHANNELS,
            (SLuint32)audio->block_frames * TORIRS_AUDIO_CHANNELS * sizeof(int16_t));
        if( result != SL_RESULT_SUCCESS )
        {
            TORIRS_ERR("audio: priming Enqueue failed (0x%x)\n", (unsigned)result);
            close_device(audio);
            return false;
        }
    }
    audio->next_buffer = 0;

    result = (*audio->player)->SetPlayState(audio->player, SL_PLAYSTATE_PLAYING);
    if( result != SL_RESULT_SUCCESS )
    {
        TORIRS_ERR("audio: SetPlayState(PLAYING) failed (0x%x)\n", (unsigned)result);
        close_device(audio);
        return false;
    }

    audio->device_open = true;
    audio->playing = true;
    TORIRS_LOG(
        "audio: OpenSL ES, %d x %d-frame buffers (%.1f ms) @ %dHz\n",
        OPENSLES_BUFFER_COUNT,
        audio->block_frames,
        audio->callback_period_ms * OPENSLES_BUFFER_COUNT,
        audio->sample_rate);
    return true;
}

void
PlatformAudio_Free(struct PlatformAudio* audio)
{
    if( !audio )
        return;
    close_device(audio);
    PlatformAudioCapture_Close(audio->capture);
    ToriRS_Mixer_Free(&audio->mixer);
    pthread_mutex_destroy(&audio->lock);
    free(audio->buffers);
    free(audio);
}

void
PlatformAudio_Submit(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* command)
{
    assert(audio);
    assert(command);
    pthread_mutex_lock(&audio->lock);
    audio->commands++;
    ToriRS_Mixer_Apply(&audio->mixer, command);
    pthread_mutex_unlock(&audio->lock);
}

void
PlatformAudio_SubmitAll(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* commands,
    int count)
{
    assert(audio);
    assert(commands);
    /* One lock for the whole batch, not one per command: a scene rebuild
     * submits dozens at once and each unlock is a chance for the audio thread
     * to render half of them. */
    pthread_mutex_lock(&audio->lock);
    for( int i = 0; i < count; i++ )
    {
        audio->commands++;
        ToriRS_Mixer_Apply(&audio->mixer, &commands[i]);
    }
    pthread_mutex_unlock(&audio->lock);
}

void
PlatformAudio_Update(struct PlatformAudio* audio)
{
    bool foreground;

    assert(audio);
    if( audio->capture )
        PlatformAudioCapture_Drain(audio->capture);
    if( !audio->device_open )
        return;

    /*
     * Park the player while the activity is stopped.
     *
     * The frame loop keeps running while the Activity is stopped, so nothing
     * else in this lane would stop the sound -- the client would go on playing
     * music from the recents list. Activity lifecycle is the signal, not the
     * ANativeWindow: Android can temporarily destroy the SurfaceView
     * while this Activity remains foreground. Pausing rather than stopping
     * keeps the queue intact, so coming back is one call and no re-prime.
     */
    foreground = PlatformAndroid_IsForeground() != 0;
    if( foreground == audio->playing )
        return;
    (*audio->player)->SetPlayState(
        audio->player, foreground ? SL_PLAYSTATE_PLAYING : SL_PLAYSTATE_PAUSED);
    audio->playing = foreground;
    /* The interval clock measures gaps between callbacks; a pause is a gap
     * nobody heard, and leaving it in would report the whole background spell
     * as an underrun. */
    audio->last_callback_ns = 0;
}

void
PlatformAudio_Feedback(
    struct PlatformAudio* audio,
    struct ToriRS_AudioFeedback* out)
{
    assert(out);
    memset(out, 0, sizeof(*out));
    if( !audio )
        return;
    pthread_mutex_lock(&audio->lock);
    ToriRS_Mixer_Feedback(&audio->mixer, out);
    out->device_open = audio->device_open;
    pthread_mutex_unlock(&audio->lock);
}

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio)
{
    struct PlatformAudioStats stats;
    struct ToriRS_MixerStats mixer_stats;

    memset(&stats, 0, sizeof(stats));
    assert(audio);
    pthread_mutex_lock(&audio->lock);
    mixer_stats = ToriRS_Mixer_Stats(&audio->mixer);
    stats.commands = audio->commands;
    stats.assets_live = mixer_stats.assets_live;
    stats.voices_live = mixer_stats.voices_live;
    stats.voices_started = mixer_stats.voices_started;
    stats.voices_stolen = mixer_stats.voices_stolen;
    stats.voices_rejected = mixer_stats.voices_rejected;
    stats.frames_played = audio->frames_played;
    stats.stream_dropped_frames = mixer_stats.stream_dropped_frames;
    stats.stream_starved_frames = mixer_stats.stream_starved_frames;
    for( int i = 0; i < TORIRS_AUDIO_BUS_COUNT; i++ )
        stats.bus_volume[i] = mixer_stats.bus_volume[i];
    stats.device_open = audio->device_open;
    stats.updates = audio->callbacks;
    stats.underruns = audio->callback_underruns;
    stats.queue_min_frames =
        audio->stream_ring_samples > 0 ? audio->stream_ring_min_frames : 0;
    stats.queue_max_frames = audio->stream_ring_max_frames;
    stats.queue_current_frames = audio->stream_ring_current_frames;
    stats.queue_mean_frames = audio->stream_ring_samples > 0
                                  ? (double)audio->stream_ring_sum_frames /
                                        (double)audio->stream_ring_samples
                                  : 0.0;
    stats.update_interval_min_ms =
        audio->callbacks > 1 ? audio->callback_interval_min_ms : 0.0;
    stats.update_interval_max_ms = audio->callback_interval_max_ms;
    stats.update_interval_mean_ms =
        audio->callbacks > 1
            ? audio->callback_interval_sum_ms / (double)(audio->callbacks - 1)
            : 0.0;
    stats.callback_period_ms = audio->callback_period_ms;
    stats.callback_jitter_max_ms = audio->callback_jitter_max_ms;
    stats.render_max_ms = audio->render_max_ms;
    pthread_mutex_unlock(&audio->lock);
    stats.capture_dropped_frames =
        audio->capture ? PlatformAudioCapture_DroppedFrames(audio->capture) : 0;
    return stats;
}

int
PlatformAudio_LiveAssetCount(struct PlatformAudio* audio)
{
    int count;

    if( !audio )
        return 0;
    pthread_mutex_lock(&audio->lock);
    count = ToriRS_Mixer_LiveAssetCount(&audio->mixer);
    pthread_mutex_unlock(&audio->lock);
    return count;
}
