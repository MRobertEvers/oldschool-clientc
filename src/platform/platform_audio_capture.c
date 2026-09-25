#include "platform/platform_audio_capture.h"

#include "audio/torirs_audio.h"
#include "log/torirs_log.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/*
 * The WAV tee. See platform_audio_capture.h for what it is for.
 *
 * The ring is single-producer/single-consumer with monotonically increasing
 * 64-bit frame counters, so the empty and full cases are told apart by
 * subtraction rather than by a spare slot, and neither side ever has to look at
 * the other's index twice. That is what lets Push run on a real-time thread
 * with no lock at all: it publishes its write index with a release store, and
 * Drain publishes its read index the same way.
 */

struct PlatformAudioCapture
{
    FILE* file;
    /** Frames actually written to `file`; patched into the header on close. */
    long frames_written;
    int16_t* ring;
    int ring_frames;
    _Atomic uint64_t read_frame;
    _Atomic uint64_t write_frame;
    _Atomic int dropped_frames;
    int sample_rate;
};

/** Canonical 44-byte PCM header. The two length fields are patched on close. */
static void
write_header(
    FILE* file,
    int sample_rate)
{
    unsigned char header[44] = { 0 };
    int const byte_rate = sample_rate * TORIRS_AUDIO_CHANNELS * 2;

    memcpy(header, "RIFF", 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16;
    header[20] = 1;
    header[22] = TORIRS_AUDIO_CHANNELS;
    header[24] = (unsigned char)(sample_rate & 0xFF);
    header[25] = (unsigned char)((sample_rate >> 8) & 0xFF);
    header[26] = (unsigned char)((sample_rate >> 16) & 0xFF);
    header[28] = (unsigned char)(byte_rate & 0xFF);
    header[29] = (unsigned char)((byte_rate >> 8) & 0xFF);
    header[30] = (unsigned char)((byte_rate >> 16) & 0xFF);
    header[32] = TORIRS_AUDIO_CHANNELS * 2;
    header[34] = 16;
    memcpy(header + 36, "data", 4);
    fwrite(header, 1, sizeof(header), file);
}

struct PlatformAudioCapture*
PlatformAudioCapture_Open(
    int sample_rate,
    int ring_seconds)
{
    char const* path = getenv("TORIRS_AUDIO_WAV");
    struct PlatformAudioCapture* capture;
    FILE* file;

    assert(sample_rate > 0);
    assert(ring_seconds > 0);
    if( !path )
        return NULL;
    file = fopen(path, "wb");
    if( !file )
    {
        TORIRS_ERR("audio: cannot open capture file %s\n", path);
        return NULL;
    }

    capture = calloc(1, sizeof(*capture));
    assert(capture);
    capture->file = file;
    capture->sample_rate = sample_rate;
    capture->ring_frames = sample_rate * ring_seconds;
    capture->ring =
        calloc((size_t)capture->ring_frames * TORIRS_AUDIO_CHANNELS, sizeof(int16_t));
    assert(capture->ring);
    write_header(capture->file, sample_rate);
    TORIRS_REPORT(
        "audio: capturing to %s via %d-frame asynchronous ring\n",
        path,
        capture->ring_frames);
    return capture;
}

void
PlatformAudioCapture_Push(
    struct PlatformAudioCapture* capture,
    const int16_t* pcm,
    int frames)
{
    uint64_t write;
    uint64_t read;
    int first;

    assert(capture);
    assert(pcm);
    if( frames <= 0 )
        return;
    write = atomic_load_explicit(&capture->write_frame, memory_order_relaxed);
    read = atomic_load_explicit(&capture->read_frame, memory_order_acquire);
    /*
     * Drop rather than block or overwrite. The consumer is the frame thread,
     * and a frame thread that stalls for two seconds is exactly the condition
     * being investigated -- corrupting the recording of it, or stalling the
     * device to preserve it, would destroy the evidence either way. The
     * shortfall is counted so the gap in the file is accounted for.
     */
    if( write - read + (uint64_t)frames > (uint64_t)capture->ring_frames )
    {
        atomic_fetch_add_explicit(&capture->dropped_frames, frames, memory_order_relaxed);
        return;
    }
    first = capture->ring_frames - (int)(write % (uint64_t)capture->ring_frames);
    if( first > frames )
        first = frames;
    memcpy(
        capture->ring +
            (size_t)(write % (uint64_t)capture->ring_frames) * TORIRS_AUDIO_CHANNELS,
        pcm,
        (size_t)first * TORIRS_AUDIO_CHANNELS * sizeof(int16_t));
    if( first < frames )
        memcpy(
            capture->ring,
            pcm + (size_t)first * TORIRS_AUDIO_CHANNELS,
            (size_t)(frames - first) * TORIRS_AUDIO_CHANNELS * sizeof(int16_t));
    atomic_store_explicit(
        &capture->write_frame, write + (uint64_t)frames, memory_order_release);
}

void
PlatformAudioCapture_Drain(struct PlatformAudioCapture* capture)
{
    uint64_t read;
    uint64_t write;

    assert(capture);
    read = atomic_load_explicit(&capture->read_frame, memory_order_relaxed);
    write = atomic_load_explicit(&capture->write_frame, memory_order_acquire);
    while( read < write )
    {
        int frames = (int)(write - read);
        int const contiguous =
            capture->ring_frames - (int)(read % (uint64_t)capture->ring_frames);
        size_t written;

        if( frames > contiguous )
            frames = contiguous;
        written = fwrite(
            capture->ring +
                (size_t)(read % (uint64_t)capture->ring_frames) * TORIRS_AUDIO_CHANNELS,
            sizeof(int16_t) * TORIRS_AUDIO_CHANNELS,
            (size_t)frames,
            capture->file);
        read += written;
        capture->frames_written += (long)written;
        atomic_store_explicit(&capture->read_frame, read, memory_order_release);
        if( written != (size_t)frames )
            break;
    }
}

int
PlatformAudioCapture_DroppedFrames(struct PlatformAudioCapture* capture)
{
    assert(capture);
    return atomic_load_explicit(&capture->dropped_frames, memory_order_relaxed);
}

void
PlatformAudioCapture_Close(struct PlatformAudioCapture* capture)
{
    uint32_t data_bytes;
    uint32_t riff_size;

    if( !capture )
        return;
    PlatformAudioCapture_Drain(capture);
    data_bytes = (uint32_t)(capture->frames_written * TORIRS_AUDIO_CHANNELS * 2);
    riff_size = 36 + data_bytes;
    fseek(capture->file, 4, SEEK_SET);
    fwrite(&riff_size, 4, 1, capture->file);
    fseek(capture->file, 40, SEEK_SET);
    fwrite(&data_bytes, 4, 1, capture->file);
    fclose(capture->file);
    free(capture->ring);
    free(capture);
}
