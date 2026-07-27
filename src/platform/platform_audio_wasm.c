/*
 * WebAudio backend (Emscripten).
 *
 * Compiled only for __EMSCRIPTEN__. There is no WebAssembly build of src/ yet —
 * src/makefile is native+SDL2 — so this file exists to pin the browser side of
 * audio/torirs_audio.h down while the interface is being designed, rather than
 * discovering later that a native-shaped interface does not survive the move.
 * It is written to compile and run as-is under emcc; it is not yet exercised.
 *
 * What the browser forces on the design, and why the interface looks the way it
 * does:
 *
 *   - **The game cannot own the clock.** Playback is scheduled by the browser's
 *     AudioContext. A command queue drained once per frame fits that; a callback
 *     mixer the game drives would not.
 *   - **Audio may not start before a user gesture.** Every major browser
 *     suspends a fresh AudioContext until a click or key. So Init cannot fail
 *     just because the context is suspended: it reports success, resumes on the
 *     first gesture, and clips submitted before then are dropped — counted, not
 *     queued, because a sound that arrives seconds late is worse than one that
 *     never plays.
 *   - **Buffers must be copied, not referenced.** A drained command's PCM is
 *     borrowed and the heap can move under WebAssembly memory growth, so each
 *     clip is copied into a JS-side AudioBuffer at submit time. Hence the
 *     interface promising validity only until the next drain — that is exactly
 *     long enough.
 *
 * Unlike the SDL2 backend, clips here *do* overlap: WebAudio gives every
 * AudioBufferSourceNode its own voice and refusing that would mean writing a
 * mixer to be less capable. The game's own overlap rule still runs first, so what
 * reaches here is already the set the reference would have played.
 */

#if defined(__EMSCRIPTEN__)

#include "platform/platform_audio.h"

#include <assert.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The JS side. One AudioContext and one gain node for effect volume.
 *
 * The PCM is 8-bit unsigned; WebAudio wants float in [-1, 1], so each sample is
 * (byte - 128) / 128. Done here rather than in C to keep one copy of the data
 * rather than two.
 */
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
    state.resumed = state.context.state === 'running';

    // Browsers suspend a fresh context until the user interacts. Resume on the
    // first gesture of any kind, then stop listening.
    var resume = function() {
        if( state.context.state !== 'running' )
            state.context.resume().then(function() { state.resumed = true; });
        else
            state.resumed = true;
    };
    ['pointerdown', 'keydown', 'touchend'].forEach(function(event) {
        window.addEventListener(event, resume, { once : false, passive : true });
    });
    document.addEventListener('visibilitychange', function() {
        if( !document.hidden )
            resume();
    });
    return 1;
});

EM_JS(int, torirs_audio_js_play, (const uint8_t* pcm, int count, int sample_rate), {
    var state = Module.torirsAudio;
    if( !state || !state.context )
        return 0;
    if( state.context.state !== 'running' )
        return 0; /* pre-gesture: drop rather than schedule late */

    var buffer = state.context.createBuffer(1, count, sample_rate);
    var channel = buffer.getChannelData(0);
    for( var i = 0; i < count; i++ )
        channel[i] = (HEAPU8[pcm + i] - 128) / 128.0;

    var source = state.context.createBufferSource();
    source.buffer = buffer;
    source.connect(state.gain);
    source.start();
    if( !state.playing )
        state.playing = [];
    state.playing.push(source);
    source.onended = function() {
        var at = state.playing.indexOf(source);
        if( at >= 0 )
            state.playing.splice(at, 1);
    };
    return 1;
});

EM_JS(void, torirs_audio_js_stop, (void), {
    var state = Module.torirsAudio;
    if( !state || !state.playing )
        return;
    state.playing.forEach(function(source) {
        try
        {
            source.stop();
        }
        catch( e )
        {
        }
    });
    state.playing = [];
});

EM_JS(void, torirs_audio_js_set_volume, (double gain), {
    var state = Module.torirsAudio;
    if( state && state.gain )
        state.gain.gain.value = gain;
});

struct PlatformAudio
{
    int sample_rate;
    int volume;
    struct PlatformAudioStats stats;
};

struct PlatformAudio*
PlatformAudio_New(void)
{
    struct PlatformAudio* audio = calloc(1, sizeof(*audio));
    assert(audio);
    audio->volume = TORIRS_AUDIO_VOLUME_MAX;
    audio->stats.volume = TORIRS_AUDIO_VOLUME_MAX;
    return audio;
}

bool
PlatformAudio_Init(
    struct PlatformAudio* audio,
    int sample_rate)
{
    if( !audio )
        return false;
    audio->sample_rate = sample_rate > 0 ? sample_rate : 22050;
    if( !torirs_audio_js_init(audio->sample_rate) )
    {
        fprintf(stderr, "audio: no AudioContext in this browser\n");
        return false;
    }
    /* Open, though possibly suspended until the first user gesture. */
    audio->stats.device_open = true;
    return true;
}

void
PlatformAudio_Free(struct PlatformAudio* audio)
{
    if( !audio )
        return;
    torirs_audio_js_stop();
    free(audio);
}

void
PlatformAudio_Submit(
    struct PlatformAudio* audio,
    const struct ToriRS_AudioCommand* command)
{
    if( !audio || !command )
        return;

    switch( command->kind )
    {
    case TORIRS_AUDIO_CMD_PLAY_PCM:
        audio->stats.submitted++;
        if( !command->pcm || command->sample_count <= 0 || audio->volume <= 0 )
        {
            audio->stats.dropped++;
            break;
        }
        if( torirs_audio_js_play(
                command->pcm,
                command->sample_count,
                command->sample_rate > 0 ? command->sample_rate : audio->sample_rate) )
            audio->stats.played++;
        else
            audio->stats.dropped++;
        break;
    case TORIRS_AUDIO_CMD_STOP_ALL:
        torirs_audio_js_stop();
        break;
    case TORIRS_AUDIO_CMD_SET_VOLUME:
        audio->volume = command->volume < 0 ? 0
                        : command->volume > TORIRS_AUDIO_VOLUME_MAX
                            ? TORIRS_AUDIO_VOLUME_MAX
                            : command->volume;
        audio->stats.volume = audio->volume;
        torirs_audio_js_set_volume((double)audio->volume / (double)TORIRS_AUDIO_VOLUME_MAX);
        break;
    case TORIRS_AUDIO_CMD_NONE:
    default:
        break;
    }
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

int
PlatformAudio_QueuedBytes(struct PlatformAudio* audio)
{
    /* WebAudio schedules rather than queues; nothing meaningful to report. */
    (void)audio;
    return 0;
}

struct PlatformAudioStats
PlatformAudio_Stats(struct PlatformAudio* audio)
{
    struct PlatformAudioStats empty;
    if( !audio )
    {
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return audio->stats;
}

#endif /* __EMSCRIPTEN__ */
