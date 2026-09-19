/*
 * WebAudio backend (Emscripten).
 *
 * Compiled only for __EMSCRIPTEN__ and selected by the current PLATFORM=web
 * lane. The browser-specific constraints below are registered centrally in
 * docs/platform_quirks.md.
 *
 * What the browser forces on the design:
 *
 *   - **The game cannot own the clock.** Playback is scheduled by the browser's
 *     AudioContext. A command queue drained once per frame fits that; a
 *     callback mixer the game drives would not.
 *   - **Audio may not start before a user gesture.** Every major browser
 *     suspends a fresh AudioContext until a click or key. So Init cannot fail
 *     just because the context is suspended: it reports success, resumes on the
 *     first gesture, and blocks rendered before then are dropped -- counted,
 *     not queued, because audio that arrives seconds late is worse than audio
 *     that never played.
 *   - **Buffers must be copied, not referenced.** The heap moves under
 *     WebAssembly memory growth, so each mixed block is copied into a JS-side
 *     AudioBuffer at schedule time.
 *
 * The mix itself is `ToriRS_Mixer`, the same one the native backend runs, so
 * voices, loops, fades and bus volumes behave identically in a browser and out
 * of one. What is left here is scheduling: keep a playhead ahead of the
 * context's clock and queue AudioBuffers at it.
 *
 * ## Why the schedule is refilled in a loop, and how deep
 *
 * This is the only backend whose device is fed from the frame loop rather than
 * from a device callback, so the frame rate is the only thing keeping the
 * schedule alive. That makes two numbers load-bearing:
 *
 *   - **How much is queued per Update.** One block per frame caps playback at
 *     one block of audio per frame, so a block that is one 50Hz tick long
 *     breaks even at exactly 50fps and starves below it -- the schedule drains
 *     a little further every frame until the playhead falls behind the context
 *     clock and the listener hears a gap, every frame, for as long as the frame
 *     rate stays down. So Update refills to the lead however many blocks that
 *     takes, which is what platform_audio.h has always said a backend does.
 *   - **How far ahead the lead sits.** The schedule has to survive the gap
 *     between two Updates, so a fixed lead is only ever right for one frame
 *     rate. The lead tracks a decaying peak of the measured Update interval:
 *     it sits at the floor while frames are quick, and deepens when they are
 *     not. It is clamped at both ends -- below, so a quick frame cannot drive
 *     latency to nothing; above, because past WASM_AUDIO_LEAD_MAX the delay
 *     before a click is heard is worse than the gap the depth would prevent.
 *
 * What this does not fix is a stall longer than the lead -- a synchronous
 * decode that parks the frame loop for a second hears a gap no matter how the
 * refill is written, because nothing runs to refill it. Only a mixer running
 * off the main thread (an AudioWorklet pulling a ring) removes that, and it
 * needs the mixer off-thread entirely.
 */

#if defined(__EMSCRIPTEN__)

#include "platform/platform_audio.h"

#include <assert.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Frames per scheduled block: one 50Hz tick. */
#define WASM_AUDIO_BLOCK_FRAMES (TORIRS_AUDIO_SAMPLE_RATE / 50)
/** Shallowest the schedule is allowed to sit, in seconds. */
#define WASM_AUDIO_LEAD_MIN 0.08
/** Deepest the schedule is allowed to sit, in seconds. */
#define WASM_AUDIO_LEAD_MAX 0.50
/** Update intervals the lead covers, over and above the block being queued. */
#define WASM_AUDIO_LEAD_INTERVALS 2.0
/** Per-Update decay of the interval peak: ~2s to halve at 60fps, so one slow
 *  frame deepens the schedule and a recovered frame rate walks it back. */
#define WASM_AUDIO_PEAK_DECAY 0.995
/** Safety valve on one Update's refill. Above the deepest lead, so it only
 *  binds if the loop ever fails to make progress. */
#define WASM_AUDIO_MAX_BLOCKS_PER_UPDATE 64

EM_JS(int, torirs_audio_js_init, (int sample_rate), {
    if( !Module.torirsAudio )
        Module.torirsAudio = {};
    var state = Module.torirsAudio;
    if( state.context )
        return 1;
    var Ctor = window.AudioContext || window.webkitAudioContext;
    if( !Ctor )
        return 0;
    state.context = new Ctor({ sampleRate: sample_rate });
    state.gain = state.context.createGain();
    state.gain.gain.value = 1.0;
    state.gain.connect(state.context.destination);
    state.playhead = 0;
    state.dropped = 0;
    state.underruns = 0;
    state.started = 0;
    // A suspended context cannot play; resume on the first gesture the page
    // sees. Registering both covers browsers that only honour one.
    var resume = function() {
        if( state.context.state === 'suspended' )
            state.context.resume();
    };
    window.addEventListener('click', resume, { once : false });
    window.addEventListener('keydown', resume, { once : false });
    return 1;
});

/** Schedule one interleaved stereo int16 block. Returns 1 if it was queued. */
EM_JS(int, torirs_audio_js_push, (int ptr, int frames, int sample_rate), {
    var state = Module.torirsAudio;
    if( !state || !state.context )
        return 0;
    if( state.context.state !== 'running' )
    {
        state.dropped += frames;
        return 0;
    }
    var now = state.context.currentTime;
    // A playhead behind the clock means the last block finished with nothing
    // queued behind it: that is the gap the listener heard. Count it before
    // restarting the schedule, so a choppy run is visible in the stats rather
    // than only audible.
    if( state.started && state.playhead < now )
        state.underruns++;
    if( state.playhead < now + 0.01 )
        state.playhead = now + 0.01;
    var buffer = state.context.createBuffer(2, frames, sample_rate);
    var left = buffer.getChannelData(0);
    var right = buffer.getChannelData(1);
    var base = ptr >> 1;
    for( var i = 0; i < frames; i++ )
    {
        left[i] = HEAP16[base + i * 2] / 32768;
        right[i] = HEAP16[base + i * 2 + 1] / 32768;
    }
    var source = state.context.createBufferSource();
    source.buffer = buffer;
    source.connect(state.gain);
    source.start(state.playhead);
    state.playhead += frames / sample_rate;
    state.started = 1;
    return 1;
});

/** Seconds of audio already scheduled but not yet played. */
EM_JS(double, torirs_audio_js_scheduled, (), {
    var state = Module.torirsAudio;
    if( !state || !state.context )
        return 0;
    var ahead = state.playhead - state.context.currentTime;
    return ahead > 0 ? ahead : 0;
});

EM_JS(int, torirs_audio_js_dropped, (), {
    var state = Module.torirsAudio;
    return state ? (state.dropped | 0) : 0;
});

EM_JS(int, torirs_audio_js_underruns, (), {
    var state = Module.torirsAudio;
    return state ? (state.underruns | 0) : 0;
});

struct PlatformAudio
{
    int sample_rate;
    bool device_open;
    struct ToriRS_Mixer mixer;
    int16_t block[WASM_AUDIO_BLOCK_FRAMES * TORIRS_AUDIO_CHANNELS];
    int commands;
    int frames_played;

    /* Scheduling diagnostics, and the interval the lead is derived from. */
    int updates;
    double last_update_ms;
    double interval_peak_s;
    double update_interval_min_ms;
    double update_interval_max_ms;
    double update_interval_sum_ms;
    int queue_min_frames;
    int queue_max_frames;
    int queue_current_frames;
    double queue_sum_frames;
    int queue_samples;
};

struct PlatformAudio*
PlatformAudio_New(void)
{
    struct PlatformAudio* audio = calloc(1, sizeof(*audio));
    assert(audio);
    audio->sample_rate = TORIRS_AUDIO_SAMPLE_RATE;
    ToriRS_Mixer_Init(&audio->mixer, audio->sample_rate);
    return audio;
}

bool
PlatformAudio_Init(
    struct PlatformAudio* audio,
    int sample_rate)
{
    assert(audio);
    audio->sample_rate = sample_rate > 0 ? sample_rate : TORIRS_AUDIO_SAMPLE_RATE;
    ToriRS_Mixer_Init(&audio->mixer, audio->sample_rate);
    audio->device_open = torirs_audio_js_init(audio->sample_rate) != 0;
    return audio->device_open;
}

void
PlatformAudio_Free(struct PlatformAudio* audio)
{
    if( !audio )
        return;
    ToriRS_Mixer_Free(&audio->mixer);
    free(audio);
}

void
PlatformAudio_Submit(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* command)
{
    assert(audio);
    assert(command);
    audio->commands++;
    ToriRS_Mixer_Apply(&audio->mixer, command);
}

void
PlatformAudio_SubmitAll(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* commands,
    int count)
{
    assert(audio);
    assert(commands);
    for( int i = 0; i < count; i++ )
        PlatformAudio_Submit(audio, &commands[i]);
}

/**
 * How deep the schedule should sit right now, in seconds.
 *
 * Deep enough to survive the Update intervals this run has actually been
 * seeing, clamped so neither a fast run nor a one-off stall can push the
 * latency somewhere useless.
 */
static double
wasm_audio_target_lead(const struct PlatformAudio* audio, double block_seconds)
{
    double lead;

    assert(audio);
    lead = audio->interval_peak_s * WASM_AUDIO_LEAD_INTERVALS + block_seconds;
    if( lead < WASM_AUDIO_LEAD_MIN )
        lead = WASM_AUDIO_LEAD_MIN;
    if( lead > WASM_AUDIO_LEAD_MAX )
        lead = WASM_AUDIO_LEAD_MAX;
    return lead;
}

/** Fold this Update's arrival into the interval peak and the interval stats. */
static void
wasm_audio_note_interval(struct PlatformAudio* audio, double now_ms)
{
    assert(audio);
    if( audio->updates > 0 )
    {
        double interval_ms = now_ms - audio->last_update_ms;

        if( interval_ms < 0.0 )
            interval_ms = 0.0;
        if( audio->updates == 1 || interval_ms < audio->update_interval_min_ms )
            audio->update_interval_min_ms = interval_ms;
        if( interval_ms > audio->update_interval_max_ms )
            audio->update_interval_max_ms = interval_ms;
        audio->update_interval_sum_ms += interval_ms;

        audio->interval_peak_s *= WASM_AUDIO_PEAK_DECAY;
        if( interval_ms / 1000.0 > audio->interval_peak_s )
            audio->interval_peak_s = interval_ms / 1000.0;
    }
    audio->last_update_ms = now_ms;
    audio->updates++;
}

/** Record how much was queued when this Update found the schedule. */
static void
wasm_audio_note_queue(struct PlatformAudio* audio, double scheduled_seconds)
{
    int frames;

    assert(audio);
    frames = (int)(scheduled_seconds * audio->sample_rate);
    if( audio->queue_samples == 0 || frames < audio->queue_min_frames )
        audio->queue_min_frames = frames;
    if( frames > audio->queue_max_frames )
        audio->queue_max_frames = frames;
    audio->queue_current_frames = frames;
    audio->queue_sum_frames += frames;
    audio->queue_samples++;
}

void
PlatformAudio_Update(struct PlatformAudio* audio)
{
    double block_seconds;
    double scheduled;
    double lead;
    int blocks = 0;

    assert(audio);
    if( !audio->device_open )
        return;

    wasm_audio_note_interval(audio, emscripten_get_now());

    block_seconds = (double)WASM_AUDIO_BLOCK_FRAMES / audio->sample_rate;
    lead = wasm_audio_target_lead(audio, block_seconds);
    scheduled = torirs_audio_js_scheduled();
    wasm_audio_note_queue(audio, scheduled);

    /*
     * Top the schedule back up to the lead, however many blocks that takes.
     * The local running total is what the push does to the playhead, so the
     * loop does not have to ask the context its time once per block; the next
     * Update reads the real depth again and corrects any drift.
     *
     * A failed push means the context is not running -- no gesture yet. One
     * block is still rendered and dropped, so voices and songs age at the same
     * rate they would if they were audible, and the loop stops there rather
     * than rendering the whole lead into nothing every frame.
     */
    while( scheduled < lead && blocks < WASM_AUDIO_MAX_BLOCKS_PER_UPDATE )
    {
        ToriRS_Mixer_Render(&audio->mixer, audio->block, WASM_AUDIO_BLOCK_FRAMES);
        if( !torirs_audio_js_push(
                (int)(intptr_t)audio->block, WASM_AUDIO_BLOCK_FRAMES, audio->sample_rate) )
            break;
        audio->frames_played += WASM_AUDIO_BLOCK_FRAMES;
        scheduled += block_seconds;
        blocks++;
    }
}

int
PlatformAudio_BlockFrames(struct PlatformAudio* audio)
{
    return audio ? WASM_AUDIO_BLOCK_FRAMES : 0;
}

struct ToriRS_AudioExclusion
PlatformAudio_Exclusion(struct PlatformAudio* audio)
{
    struct ToriRS_AudioExclusion exclusion;

    /*
     * Nothing to exclude: this backend renders from the frame loop, on the same
     * thread the game mutates from. The zeroed handle's acquire and release are
     * no-ops, so the game locks unconditionally and pays nothing here.
     */
    (void)audio;
    memset(&exclusion, 0, sizeof(exclusion));
    return exclusion;
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
    ToriRS_Mixer_Feedback(&audio->mixer, out);
    out->device_open = audio->device_open;
}

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio)
{
    struct PlatformAudioStats stats;
    struct ToriRS_MixerStats mixer_stats;

    memset(&stats, 0, sizeof(stats));
    assert(audio);
    mixer_stats = ToriRS_Mixer_Stats(&audio->mixer);
    stats.commands = audio->commands;
    stats.assets_live = mixer_stats.assets_live;
    stats.voices_live = mixer_stats.voices_live;
    stats.voices_started = mixer_stats.voices_started;
    stats.voices_stolen = mixer_stats.voices_stolen;
    stats.voices_rejected = mixer_stats.voices_rejected;
    stats.frames_played = audio->frames_played;
    stats.stream_dropped_frames = mixer_stats.stream_dropped_frames + torirs_audio_js_dropped();
    stats.stream_starved_frames = mixer_stats.stream_starved_frames;
    for( int i = 0; i < TORIRS_AUDIO_BUS_COUNT; i++ )
        stats.bus_volume[i] = mixer_stats.bus_volume[i];
    stats.device_open = audio->device_open;

    stats.updates = audio->updates;
    stats.underruns = torirs_audio_js_underruns();
    stats.queue_min_frames = audio->queue_min_frames;
    stats.queue_max_frames = audio->queue_max_frames;
    stats.queue_current_frames = audio->queue_current_frames;
    if( audio->queue_samples > 0 )
        stats.queue_mean_frames = audio->queue_sum_frames / audio->queue_samples;
    if( audio->updates > 1 )
    {
        stats.update_interval_min_ms = audio->update_interval_min_ms;
        stats.update_interval_max_ms = audio->update_interval_max_ms;
        stats.update_interval_mean_ms =
            audio->update_interval_sum_ms / (audio->updates - 1);
    }
    return stats;
}

int
PlatformAudio_LiveAssetCount(struct PlatformAudio* audio)
{
    return audio ? ToriRS_Mixer_LiveAssetCount(&audio->mixer) : 0;
}

#endif /* __EMSCRIPTEN__ */
