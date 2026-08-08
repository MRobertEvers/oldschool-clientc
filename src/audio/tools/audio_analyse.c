#include "audio/tools/audio_analyse.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** Window for the spectral measures. Long enough to resolve a bass note. */
#define ANALYSE_FFT_SIZE 4096
/**
 * A step this large between adjacent samples is a click, not a waveform.
 *
 * Relative to the signal, not absolute. A loud passage has genuinely steep
 * slopes -- a 7000-rms mix crosses 3200 between samples constantly -- so a fixed
 * threshold flags every loud track and tells you nothing. What marks a click is
 * a step far larger than the material around it, which is what the multiple of
 * RMS expresses. The floor keeps a near-silent passage from flagging its own
 * noise floor.
 */
#define ANALYSE_DISCONTINUITY_FLOOR 3200
#define ANALYSE_DISCONTINUITY_RMS_MULTIPLE 6
/** Runs of exact zero longer than this are a gap, not a rest. */
#define ANALYSE_GAP_RUN 8

/** In-place radix-2 FFT of `n` complex pairs. */
static void
fft(
    double* re,
    double* im,
    int n)
{
    for( int i = 1, j = 0; i < n; i++ )
    {
        int bit = n >> 1;
        for( ; j & bit; bit >>= 1 )
            j ^= bit;
        j |= bit;
        if( i < j )
        {
            double tr = re[i];
            double ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }
    for( int len = 2; len <= n; len <<= 1 )
    {
        double angle = -2.0 * M_PI / len;
        double wr = cos(angle);
        double wi = sin(angle);
        for( int i = 0; i < n; i += len )
        {
            double cr = 1.0;
            double ci = 0.0;
            for( int k = 0; k < len / 2; k++ )
            {
                double ur = re[i + k];
                double ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                double nr;
                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
                nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

void
AudioAnalyse(
    const int16_t* pcm,
    int frames,
    int channels,
    int sample_rate,
    struct AudioAnalysis* out)
{
    double* re;
    double* im;
    double flat_sum = 0.0;
    double high_sum = 0.0;
    double total_sum = 0.0;
    int windows = 0;
    long zero_run = 0;
    double square_sum = 0.0;
    double value_sum = 0.0;
    int start;
    int discontinuity_threshold;

    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( !pcm || frames <= 0 || channels <= 0 )
    {
        out->silent = true;
        out->flatness = 1.0;
        return;
    }
    out->frames = frames;
    out->channels = channels;
    out->sample_rate = sample_rate;

    /* First pass: level. The thresholds below are relative to it. */
    for( int i = 0; i < frames; i++ )
    {
        int v = pcm[(size_t)i * channels];
        int magnitude = v < 0 ? -v : v;

        if( magnitude > out->peak )
            out->peak = magnitude;
        if( v >= 32767 || v <= -32768 )
            out->clipped++;
        square_sum += (double)v * v;
        value_sum += v;
    }
    out->rms = sqrt(square_sum / frames);
    discontinuity_threshold = (int)(out->rms * ANALYSE_DISCONTINUITY_RMS_MULTIPLE);
    if( discontinuity_threshold < ANALYSE_DISCONTINUITY_FLOOR )
        discontinuity_threshold = ANALYSE_DISCONTINUITY_FLOOR;

    /* Second pass: artifacts, judged against that level. */
    for( int i = 1; i < frames; i++ )
    {
        int v = pcm[(size_t)i * channels];
        int step = v - pcm[(size_t)(i - 1) * channels];

        if( step > discontinuity_threshold || step < -discontinuity_threshold )
            out->discontinuities++;
        /*
         * Gaps only count after the first second (a load-time silence at the
         * head of a capture is expected) and only when the signal is loud on
         * *both* sides. Sparse music is full of legitimate silence between
         * phrases; a stream dropout is a hole punched into something that was
         * sounding.
         */
        if( i > sample_rate )
        {
            if( v == 0 )
            {
                zero_run++;
            }
            else
            {
                if( zero_run > ANALYSE_GAP_RUN && zero_run < sample_rate / 4 )
                {
                    int before = i - (int)zero_run - 1;
                    int a = before >= 0 ? pcm[(size_t)before * channels] : 0;
                    int loud = (int)(out->rms / 2);
                    if( (a > loud || a < -loud) && (v > loud || v < -loud) )
                    {
                        out->gap_runs++;
                        out->gap_samples += zero_run;
                    }
                }
                zero_run = 0;
            }
        }
    }
    out->dc = value_sum / frames;
    out->silent = out->peak == 0;
    if( out->silent )
    {
        out->flatness = 1.0;
        return;
    }

    re = malloc(sizeof(double) * ANALYSE_FFT_SIZE);
    im = malloc(sizeof(double) * ANALYSE_FFT_SIZE);
    if( !re || !im )
    {
        free(re);
        free(im);
        return;
    }
    start = frames > sample_rate * 2 ? sample_rate : 0;
    for( int s = start; s + ANALYSE_FFT_SIZE <= frames; s += ANALYSE_FFT_SIZE )
    {
        double log_sum = 0.0;
        double linear_sum = 0.0;
        double window_energy = 0.0;
        int bins = 0;

        /*
         * Skip windows that are essentially silent.
         *
         * Flatness measures whether a spectrum is tonal or noise-like, and near
         * silence is neither -- what it contains is the noise floor, whose
         * spectrum is flat by construction. Averaging those windows in makes
         * every *sparse* track read as noise: a piece with thirteen notes in
         * twenty seconds spends most of its windows in a decay tail, so the
         * average reports the rests rather than the music. The threshold is a
         * fraction of the track's own RMS so it follows the material.
         */
        for( int i = 0; i < ANALYSE_FFT_SIZE; i++ )
        {
            double v = pcm[(size_t)(s + i) * channels];

            window_energy += v * v;
        }
        if( sqrt(window_energy / ANALYSE_FFT_SIZE) < out->rms / 8.0 )
            continue;

        for( int i = 0; i < ANALYSE_FFT_SIZE; i++ )
        {
            /* Hann, so a note that does not fit the window does not smear into
             * the high bins and read as grit. */
            double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / (ANALYSE_FFT_SIZE - 1));
            re[i] = pcm[(size_t)(s + i) * channels] * w;
            im[i] = 0.0;
        }
        fft(re, im, ANALYSE_FFT_SIZE);
        for( int k = 1; k < ANALYSE_FFT_SIZE / 2; k++ )
        {
            double magnitude = sqrt(re[k] * re[k] + im[k] * im[k]) + 1e-9;
            double hz = (double)k * sample_rate / ANALYSE_FFT_SIZE;

            log_sum += log(magnitude);
            linear_sum += magnitude;
            total_sum += magnitude;
            if( hz > 5000.0 )
                high_sum += magnitude;
            bins++;
        }
        if( bins > 0 && linear_sum > 0.0 )
        {
            double geometric = exp(log_sum / bins);
            double arithmetic = linear_sum / bins;
            flat_sum += geometric / arithmetic;
            windows++;
        }
    }
    free(re);
    free(im);
    /*
     * Report the window count rather than inventing a flatness.
     *
     * A block shorter than one FFT window, or one whose every window fell below
     * the silence floor, produces no spectral measurement at all. Defaulting
     * that to 1.0 says "white noise" -- the most alarming value in the range --
     * about something that was never measured, and a short sample then reads as
     * a broken decode. Callers check `windows` before believing `flatness`.
     */
    out->windows = windows;
    out->flatness = windows > 0 ? flat_sum / windows : 0.0;
    out->high_ratio = total_sum > 0.0 ? high_sum / total_sum : 0.0;
}

bool
AudioAnalyse_LooksMusical(
    const struct AudioAnalysis* a,
    const char** reason)
{
    const char* why = NULL;

    if( !a )
        why = "no analysis";
    else if( a->silent )
        why = "silent";
    else if( a->peak < 300 )
        why = "inaudibly quiet";
    else if( a->clipped > a->frames / 1000 )
        why = "clipping";
    else if( a->windows > 0 && a->flatness > 0.55 )
        why = "noise-like spectrum";
    else if( a->gap_runs > 4 )
        why = "gaps in the signal";
    else if( a->discontinuities > a->frames / 500 )
        why = "clicks";
    else if( a->dc > 2000.0 || a->dc < -2000.0 )
        why = "DC offset";

    if( reason )
        *reason = why;
    return why == NULL;
}

void
AudioAnalyse_PrintLine(
    const char* label,
    const struct AudioAnalysis* a)
{
    const char* why = NULL;
    bool ok = AudioAnalyse_LooksMusical(a, &why);

    printf(
        "%-22s peak %5d rms %6.0f flat %.3f hi %4.1f%% clicks %5ld gaps %3ld  %s%s\n",
        label,
        a->peak,
        a->rms,
        a->flatness,
        100.0 * a->high_ratio,
        a->discontinuities,
        a->gap_runs,
        ok ? "ok" : "BAD: ",
        ok ? "" : why);
}

void
AudioAnalyse_PrintReport(
    const char* label,
    const struct AudioAnalysis* a)
{
    const char* why = NULL;
    bool ok = AudioAnalyse_LooksMusical(a, &why);

    printf("%s\n", label);
    printf(
        "  %d frames, %d ch, %d Hz (%.2f s)\n",
        a->frames,
        a->channels,
        a->sample_rate,
        a->sample_rate > 0 ? (double)a->frames / a->sample_rate : 0.0);
    printf("  peak %d, rms %.0f, dc %+.1f, clipped %ld\n", a->peak, a->rms, a->dc, a->clipped);
    printf(
        "  discontinuities %ld, gap runs %ld (%ld samples)\n",
        a->discontinuities,
        a->gap_runs,
        a->gap_samples);
    printf(
        "  spectral flatness %.4f over %d windows (1.0 = white noise), "
        "energy above 5kHz %.1f%%\n",
        a->flatness,
        a->windows,
        100.0 * a->high_ratio);
    printf("  verdict: %s%s\n", ok ? "looks musical" : "BAD -- ", ok ? "" : why);
}

/* --- WAV ------------------------------------------------------------------ */

static void
put32(
    unsigned char* p,
    uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void
put16(
    unsigned char* p,
    uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

bool
AudioWriteWav(
    const char* path,
    const int16_t* pcm,
    int frames,
    int channels,
    int sample_rate)
{
    unsigned char header[44];
    uint32_t data_bytes = (uint32_t)frames * (uint32_t)channels * 2u;
    FILE* f;

    if( !path || !pcm || frames <= 0 )
        return false;
    f = fopen(path, "wb");
    if( !f )
        return false;
    memset(header, 0, sizeof(header));
    memcpy(header, "RIFF", 4);
    put32(header + 4, 36 + data_bytes);
    memcpy(header + 8, "WAVEfmt ", 8);
    put32(header + 16, 16);
    put16(header + 20, 1);
    put16(header + 22, (uint16_t)channels);
    put32(header + 24, (uint32_t)sample_rate);
    put32(header + 28, (uint32_t)(sample_rate * channels * 2));
    put16(header + 32, (uint16_t)(channels * 2));
    put16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    put32(header + 40, data_bytes);
    fwrite(header, 1, sizeof(header), f);
    fwrite(pcm, 2, (size_t)frames * channels, f);
    fclose(f);
    return true;
}

bool
AudioReadWav(
    const char* path,
    int16_t** out_pcm,
    int* out_frames,
    int* out_channels,
    int* out_sample_rate)
{
    unsigned char header[44];
    FILE* f = fopen(path, "rb");
    uint32_t data_bytes;
    int channels;
    int rate;
    int16_t* pcm;

    if( !f )
        return false;
    if( fread(header, 1, sizeof(header), f) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0 )
    {
        fclose(f);
        return false;
    }
    channels = header[22] | (header[23] << 8);
    rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    data_bytes = (uint32_t)header[40] | ((uint32_t)header[41] << 8) |
                 ((uint32_t)header[42] << 16) | ((uint32_t)header[43] << 24);
    if( channels <= 0 || channels > 2 || rate <= 0 || data_bytes == 0 )
    {
        fclose(f);
        return false;
    }
    pcm = malloc(data_bytes);
    if( !pcm )
    {
        fclose(f);
        return false;
    }
    data_bytes = (uint32_t)fread(pcm, 1, data_bytes, f);
    fclose(f);

    *out_pcm = pcm;
    *out_frames = (int)(data_bytes / (uint32_t)(channels * 2));
    *out_channels = channels;
    *out_sample_rate = rate;
    return true;
}
