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
 * of one. What is left here is scheduling: keep a playhead a little ahead of
 * the context's clock and queue one AudioBuffer per frame at it.
 */

#if defined(__EMSCRIPTEN__)

#include "platform/platform_audio.h"

#include <assert.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** How far ahead of the context clock to keep the schedule, in seconds. */
#define WASM_AUDIO_LEAD 0.08
/** Frames per scheduled block: one 50Hz tick. */
#define WASM_AUDIO_BLOCK_FRAMES (TORIRS_AUDIO_SAMPLE_RATE / 50)

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
    var now = state.context.currentTime;
    if( state.playhead < now + 0.01 )
        state.playhead = now + 0.01;
    source.start(state.playhead);
    state.playhead += frames / sample_rate;
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

struct PlatformAudio
{
    int sample_rate;
    bool device_open;
    struct ToriRS_Mixer mixer;
    int16_t block[WASM_AUDIO_BLOCK_FRAMES * TORIRS_AUDIO_CHANNELS];
    int commands;
    int frames_played;
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
    if( !audio )
        return false;
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
    if( !audio || !command )
        return;
    audio->commands++;
    ToriRS_Mixer_Apply(&audio->mixer, command);
}

void
PlatformAudio_SubmitAll(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* commands,
    int count)
{
    if( !audio || !commands )
        return;
    for( int i = 0; i < count; i++ )
        PlatformAudio_Submit(audio, &commands[i]);
}

void
PlatformAudio_Update(struct PlatformAudio* audio)
{
    if( !audio || !audio->device_open )
        return;
    /* One block ahead of the lead is enough; scheduling more only adds
     * latency, and the browser will not thank us for a hundred queued nodes. */
    if( torirs_audio_js_scheduled() > WASM_AUDIO_LEAD )
        return;
    ToriRS_Mixer_Render(&audio->mixer, audio->block, WASM_AUDIO_BLOCK_FRAMES);
    if( torirs_audio_js_push(
            (int)(intptr_t)audio->block, WASM_AUDIO_BLOCK_FRAMES, audio->sample_rate) )
        audio->frames_played += WASM_AUDIO_BLOCK_FRAMES;
}

void
PlatformAudio_Feedback(
    struct PlatformAudio* audio,
    struct ToriRS_AudioFeedback* out)
{
    if( !out )
        return;
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
    if( !audio )
        return stats;
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
    return stats;
}

int
PlatformAudio_LiveAssetCount(struct PlatformAudio* audio)
{
    return audio ? ToriRS_Mixer_LiveAssetCount(&audio->mixer) : 0;
}

#endif /* __EMSCRIPTEN__ */
