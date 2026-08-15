/*
 * audioharness -- the audio subsystem with nothing else attached.
 *
 * This links the audio engine and the cache decoders and *nothing else*: no
 * toridraw, no renderer, no game, no SDL. That is the point. Audio work kept
 * being blocked by an unrelated build break somewhere in the client, and a
 * subsystem you cannot exercise is a subsystem you cannot fix. Everything below
 * runs from a cache directory and a song id.
 *
 * It also carries the measurements. Every audio bug found in this subsystem was
 * diagnosed by rendering something and looking at the samples -- flatness to
 * tell noise from music, high-frequency ratio to catch a staircasing gain,
 * discontinuity counts to catch a clip cut off mid-waveform -- and each of those
 * started life as a throwaway script. They live in audio_analyse.c now, so the
 * next question is one command instead of one script.
 *
 *   audioharness song   <cache> <rev> <id> [-o out.wav] [-s seconds]
 *   audioharness jingle <cache> <rev> <id> [-o out.wav] [-s seconds]
 *   audioharness effect <cache> <rev> <id> [-o out.wav]
 *   audioharness sweep  <cache> <rev> [-n count] [-s seconds]
 *   audioharness stream <cache> <rev> <id> [-b frames]
 *   audioharness wav    <file.wav>
 *   audioharness diff   <a.wav> <b.wav>
 *
 * `sweep` is the one that earns its keep: it renders the first N songs and
 * reports each as ok or BAD with a reason. Rendering one track proves one track
 * works; a cache has 881 of them, and a decoder that mishandles one instrument
 * shows up as a handful of bad tracks rather than as anything visible in a
 * single render.
 *
 * `stream` drives the song through the mixer the way the client does -- push a
 * block, render a block -- and reports whether the result differs from a direct
 * render. It should be bit-identical; if it ever is not, the client's scheduling
 * has started colouring the audio.
 */

#include "audio/tools/audio_analyse.h"
#include <assert.h>
#include "audio/tools/audio_songload.h"
#include "audio/torirs_audio.h"
#include "audio/torirs_midi_synth.h"
#include "audio/torirs_mixer.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HARNESS_RATE TORIRS_AUDIO_SAMPLE_RATE

/** Most notes sounding at once during the last render. A synth that never
 *  releases piles them up, which reads as a loud arrangement in the peak. */
static int g_verbose;
static int g_peak_held;
static int g_peak_notes;

struct options
{
    const char* out_path;
    double seconds;
    int count;
    int block;
};

static void
usage(void)
{
    fprintf(
        stderr,
        "audioharness -- the audio engine, standalone\n"
        "\n"
        "  song   <cache> <rev> <id>   render a music track\n"
        "  jingle <cache> <rev> <id>   render a jingle\n"
        "  effect <cache> <rev> <id>   render a sound effect through the mixer\n"
        "  sweep  <cache> <rev>        render many songs, report each\n"
        "  sweepjingles <cache> <rev>  the same across index 11\n"
        "  stream <cache> <rev> <id>   direct render vs the client's stream path\n"
        "  midi   <cache> <rev> <id>   dump a song's decoded MIDI as events\n"
        "  wav    <file.wav>           analyse a capture (TORIRS_AUDIO_WAV)\n"
        "  diff   <a.wav> <b.wav>      align two captures and report the residual\n"
        "\n"
        "  -o <path>     write the rendered audio to a .wav\n"
        "  -s <seconds>  how much to render (default 8)\n"
        "  -n <count>    how many songs to sweep (default 24)\n"
        "  -b <frames>   stream block size (default 441, one client tick)\n");
}

/** Render a loaded song into a fresh stereo buffer. Caller frees. */
static int16_t*
render_song(
    struct AudioSongLoad* load,
    int frames,
    struct ToriRS_MidiSynthStats* out_stats)
{
    struct ToriRS_MidiSynth synth;
    int16_t* pcm = calloc((size_t)frames * 2, sizeof(int16_t));

    assert(pcm);
    ToriRS_MidiSynth_Init(&synth, &load->bank, HARNESS_RATE);
    if( !ToriRS_MidiSynth_Play(&synth, load->song->midi, load->song->midi_size, true) )
    {
        ToriRS_MidiSynth_Free(&synth);
        free(pcm);
        return NULL;
    }
    /*
     * Render in slices so the live note count can be sampled. A mix that gets
     * steadily louder is usually notes accumulating rather than a loud
     * arrangement, and the peak alone cannot tell those apart.
     */
    {
        int done = 0;
        int slice = HARNESS_RATE / 10;
        while( done < frames )
        {
            int n = frames - done < slice ? frames - done : slice;
            ToriRS_MidiSynth_Render(&synth, pcm + (size_t)done * 2, n);
            done += n;
            if( ToriRS_MidiSynth_LiveNoteCount(&synth) > g_peak_notes )
                g_peak_notes = ToriRS_MidiSynth_LiveNoteCount(&synth);
            if( ToriRS_MidiSynth_HeldNoteCount(&synth) > g_peak_held )
                g_peak_held = ToriRS_MidiSynth_HeldNoteCount(&synth);
        }
    }
    if( out_stats )
        *out_stats = synth.stats;
    ToriRS_MidiSynth_Free(&synth);
    return pcm;
}

static void
print_load(const struct AudioSongLoad* load)
{
    printf(
        "  soundfont: %d/%d patches, %d samples loaded, %d missing, %ld KB of PCM\n",
        load->patches_loaded,
        load->patches_named,
        load->samples_loaded,
        load->samples_missing,
        load->bank.sample_bytes / 1024);

    /*
     * A silent render has two very different causes that the counts above
     * cannot separate: a soundfont that decoded to zeros, or a synth that
     * never opened a voice on good PCM. The per-sample peak decides it -- if
     * the source PCM is flat, the fault is in the decoder, not the synth.
     */
    {
        int silent = 0;
        int from_effects = 0;

        for( int i = 0; i < load->bank.sample_count; i++ )
        {
            const struct ToriRS_SoundBankSample* s = &load->bank.samples[i];
            int peak = 0;

            for( int j = 0; j < s->sound.sample_count; j++ )
            {
                int v = s->samples[j];

                if( v < 0 )
                    v = -v;
                if( v > peak )
                    peak = v;
            }
            if( peak == 0 )
                silent++;
            if( s->table != TORIRS_SOUNDBANK_TABLE_MUSIC )
                from_effects++;
            if( load->bank.sample_count <= 4 || g_verbose )
            {
                /*
                 * The source sample's own spectrum. When a rendered track reads
                 * as noise-like, this says whether the noise came out of the
                 * Vorbis decoder or was in the material to begin with -- an
                 * atmospheric pad is legitimately broadband, a mis-decoded one
                 * is broadband for a different reason, and the mix cannot tell
                 * them apart.
                 */
                struct AudioAnalysis sa;

                AudioAnalyse(s->samples, s->sound.sample_count, 1, s->sound.sample_rate, &sa);
                printf(
                    "    sample %s:%-5d %6d frames @ %5d Hz, peak %5d, ",
                    s->table == TORIRS_SOUNDBANK_TABLE_MUSIC ? "music" : "effect",
                    s->id,
                    s->sound.sample_count,
                    s->sound.sample_rate,
                    peak);
                printf("dc %+7.1f, ", sa.dc);
                if( sa.windows > 0 )
                    printf("flatness %.3f, hi %.0f%%\n", sa.flatness, sa.high_ratio * 100.0);
                else
                    printf("too short to analyse\n");
            }
            if( 0 )
                printf(
                    "    sample %s:%d  %d frames @ %d Hz, loop %d..%d%s, peak %d\n",
                    s->table == TORIRS_SOUNDBANK_TABLE_MUSIC ? "music" : "effect",
                    s->id,
                    s->sound.sample_count,
                    s->sound.sample_rate,
                    s->sound.loop_start,
                    s->sound.loop_end,
                    s->sound.loop_end > s->sound.loop_start ? " (loop span)" : "",
                    peak);
        }
        if( silent > 0 )
            printf("    %d of %d samples are entirely silent PCM\n", silent, load->bank.sample_count);
        /* Percussion comes from the sound-effect table's synth programs, and
         * those are broadband by nature. A track built mostly from them reads
         * as noise-like without anything being wrong. */
        if( from_effects > 0 )
            printf(
                "    %d of %d samples are percussion (index 4), %d melodic (index 14)\n",
                from_effects,
                load->bank.sample_count,
                load->bank.sample_count - from_effects);
    }
}

static void
print_notes(const struct ToriRS_MidiSynthStats* s)
{
    printf(
        "  sequencer: %d notes started, %d dropped (no patch %d, no sample %d), "
        "%d events, %d loops\n",
        s->notes_started,
        s->notes_dropped_no_patch + s->notes_dropped_no_sample,
        s->notes_dropped_no_patch,
        s->notes_dropped_no_sample,
        s->events_dispatched,
        s->loops);
    printf("  polyphony: %d notes sounding at the peak, %d of them held\n", g_peak_notes, g_peak_held);
    printf(
        "  headroom:  mix peaked at %d before the clamp (%.2fx full scale)\n",
        s->unclamped_peak,
        s->unclamped_peak / 32768.0);
    if( s->volume_clamped > 0 )
        printf("  WARNING:   %d note-gain evaluations exceeded unity and were clamped\n", s->volume_clamped);
}

/** Read a MIDI variable-length quantity at `*p`, advancing it. */
static int
midi_vlq(const unsigned char** p, const unsigned char* end)
{
    int value = 0;

    while( *p < end )
    {
        int byte = *(*p)++;

        value = (value << 7) | (byte & 0x7f);
        if( (byte & 0x80) == 0 )
            break;
    }
    return value;
}

/*
 * Print a song's decoded MIDI as events.
 *
 * The column-packed form (D4) is re-emitted as a standard SMF, so everything
 * downstream of the decoder sees ordinary MIDI -- which means when a track
 * renders silent, the question "did the decoder emit the right notes" and the
 * question "did the synth play them" are answerable separately. This answers
 * the first.
 */
static int
cmd_midi(
    const char* cache,
    const char* rev,
    int id,
    enum AudioSongSource source)
{
    struct AudioSongLoad load;
    const unsigned char* p;
    const unsigned char* end;
    int track = 0;
    int running = 0;
    long tick = 0;

    if( !AudioSongLoad_Open(&load, cache, rev, source, id) )
    {
        fprintf(stderr, "cannot load song %d\n", id);
        AudioSongLoad_Free(&load);
        return 1;
    }
    p = (const unsigned char*)load.song->midi;
    end = p + load.song->midi_size;
    printf("song %d: %d MIDI bytes\n", id, load.song->midi_size);
    if( load.song->midi_size < 14 )
    {
        AudioSongLoad_Free(&load);
        return 1;
    }
    printf(
        "  header: format %d, %d tracks, division %d\n",
        (p[8] << 8) | p[9],
        (p[10] << 8) | p[11],
        (p[12] << 8) | p[13]);
    p += 14;

    while( p + 8 <= end )
    {
        long length = ((long)p[4] << 24) | ((long)p[5] << 16) | ((long)p[6] << 8) | p[7];
        const unsigned char* stop;

        if( memcmp(p, "MTrk", 4) != 0 )
            break;
        p += 8;
        stop = p + length;
        if( stop > end )
            stop = end;
        printf("  track %d: %ld bytes\n", track++, length);
        tick = 0;
        running = 0;
        while( p < stop )
        {
            int status;

            tick += midi_vlq(&p, stop);
            if( p >= stop )
                break;
            if( *p & 0x80 )
                running = *p++;
            status = running;

            if( status == 0xff )
            {
                int type = p < stop ? *p++ : 0;
                int len = midi_vlq(&p, stop);

                printf("    @%-7ld meta %02x len %d\n", tick, type, len);
                p += len;
            }
            else if( status == 0xf0 || status == 0xf7 )
            {
                int len = midi_vlq(&p, stop);

                printf("    @%-7ld sysex len %d\n", tick, len);
                p += len;
            }
            else
            {
                int channel = status & 0x0f;
                int kind = status & 0xf0;
                int a = p < stop ? *p++ : 0;
                int b = 0;

                if( kind != 0xc0 && kind != 0xd0 )
                    b = p < stop ? *p++ : 0;
                switch( kind )
                {
                case 0x80:
                    printf("    @%-7ld ch%-2d note off  %3d\n", tick, channel, a);
                    break;
                case 0x90:
                    if( b == 0 )
                        printf("    @%-7ld ch%-2d note off  %3d (velocity 0)\n", tick, channel, a);
                    else
                        printf("    @%-7ld ch%-2d note on   %3d velocity %d\n", tick, channel, a, b);
                    break;
                case 0xb0:
                    printf("    @%-7ld ch%-2d control %3d = %d\n", tick, channel, a, b);
                    break;
                case 0xc0:
                    printf("    @%-7ld ch%-2d program %d\n", tick, channel, a);
                    break;
                case 0xe0:
                    printf("    @%-7ld ch%-2d pitch bend %d\n", tick, channel, (b << 7) | a);
                    break;
                default:
                    printf("    @%-7ld ch%-2d status %02x %d %d\n", tick, channel, kind, a, b);
                    break;
                }
            }
        }
        p = stop;
    }
    AudioSongLoad_Free(&load);
    return 0;
}

/*
 * Render a song twice: straight through, and by seeking into it.
 *
 * The seek is what MIDI_SWAP is built on, and a seek has a specific way of
 * being wrong that is invisible in a single render: arriving at the target tick
 * with the wrong instrument or volume selected, because the program changes and
 * controllers before that point were skipped along with the notes. Comparing a
 * seeked render against the tail of a full one catches exactly that -- the two
 * cannot match unless the channel state was reconstructed.
 *
 * They are not expected to be bit-identical: notes struck before the seek point
 * are still ringing in the full render and were deliberately not started in the
 * seeked one. What must match is the material that starts after it.
 */
static int
cmd_seek(
    const char* cache,
    const char* rev,
    int id,
    int start_tick,
    const struct options* opt)
{
    struct AudioSongLoad load;
    struct ToriRS_MidiSynth synth;
    struct AudioAnalysis full_a;
    struct AudioAnalysis seek_a;
    int frames = (int)(opt->seconds * HARNESS_RATE);
    int16_t* full;
    int16_t* seeked;

    if( !AudioSongLoad_Open(&load, cache, rev, AUDIO_SONG_TRACK, id) )
    {
        fprintf(stderr, "cannot load song %d\n", id);
        AudioSongLoad_Free(&load);
        return 1;
    }
    g_peak_notes = 0;
    g_peak_held = 0;
    full = render_song(&load, frames, NULL);

    seeked = calloc((size_t)frames * 2, sizeof(int16_t));
    assert(full);
    assert(seeked);
    ToriRS_MidiSynth_Init(&synth, &load.bank, HARNESS_RATE);
    if( !ToriRS_MidiSynth_PlayFrom(
            &synth, load.song->midi, load.song->midi_size, true, start_tick) )
    {
        fprintf(stderr, "seek failed\n");
        ToriRS_MidiSynth_Free(&synth);
        free(full);
        free(seeked);
        AudioSongLoad_Free(&load);
        return 1;
    }
    printf(
        "song %d: seeking to tick %d landed at tick %d\n",
        id,
        start_tick,
        synth.current_tick);
    ToriRS_MidiSynth_Render(&synth, seeked, frames);
    printf("  channels configured at the seek point:\n");
    for( int i = 0; i < 16; i++ )
    {
        const struct ToriRS_MidiChannel* ch = &synth.channels[i];

        if( ch->patch_id != 0 || ch->volume != 0 )
            printf(
                "    ch%-2d patch %-5d volume %-6d expression %-6d pan %d\n",
                i,
                ch->patch_id,
                ch->volume,
                ch->expression,
                ch->pan);
    }
    AudioAnalyse(full, frames, 2, HARNESS_RATE, &full_a);
    AudioAnalyse(seeked, frames, 2, HARNESS_RATE, &seek_a);
    AudioAnalyse_PrintLine("  from the top ", &full_a);
    AudioAnalyse_PrintLine("  after a seek ", &seek_a);
    if( opt->out_path )
        AudioWriteWav(opt->out_path, seeked, frames, 2, HARNESS_RATE);

    ToriRS_MidiSynth_Free(&synth);
    free(full);
    free(seeked);
    AudioSongLoad_Free(&load);
    return seek_a.silent ? 1 : 0;
}

static int
cmd_song(
    const char* cache,
    const char* rev,
    int id,
    enum AudioSongSource source,
    const struct options* opt)
{
    struct AudioSongLoad load;
    struct ToriRS_MidiSynthStats stats;
    struct AudioAnalysis analysis;
    int frames = (int)(opt->seconds * HARNESS_RATE);
    int16_t* pcm;
    char label[128];
    const char* why = NULL;

    if( !AudioSongLoad_Open(&load, cache, rev, source, id) )
    {
        fprintf(stderr, "cannot load %s %d from %s (%s)\n",
                source == AUDIO_SONG_JINGLE ? "jingle" : "song", id, cache, rev);
        AudioSongLoad_Free(&load);
        return 2;
    }
    snprintf(label, sizeof(label), "%s %d of %s",
             source == AUDIO_SONG_JINGLE ? "jingle" : "track", id, cache);
    printf("%s: %d MIDI tracks, division %d\n", label, load.song->track_count,
           load.song->division);
    print_load(&load);

    memset(&stats, 0, sizeof(stats));
    g_peak_notes = 0;
    g_peak_held = 0;
    pcm = render_song(&load, frames, &stats);
    if( !pcm )
    {
        fprintf(stderr, "render failed\n");
        AudioSongLoad_Free(&load);
        return 2;
    }
    print_notes(&stats);
    AudioAnalyse(pcm, frames, 2, HARNESS_RATE, &analysis);
    AudioAnalyse_PrintReport("  audio:", &analysis);
    if( opt->out_path && AudioWriteWav(opt->out_path, pcm, frames, 2, HARNESS_RATE) )
        printf("  wrote %s\n", opt->out_path);

    free(pcm);
    AudioSongLoad_Free(&load);
    return AudioAnalyse_LooksMusical(&analysis, &why) ? 0 : 1;
}

/**
 * Render one sound effect the way the game does: decode, widen to 16-bit, load
 * it into the mixer as an asset and play a voice on it.
 */
static int
cmd_effect(
    const char* cache,
    const char* rev,
    int id,
    const struct options* opt)
{
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_SoundEffect* effect;
    struct RSCache_SoundPcm rendered;
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    struct AudioAnalysis analysis;
    int16_t* widened;
    int16_t* out;
    int frames;
    int table;
    const char* why = NULL;
    char label[128];

    if( !RSCache_ProfileByName(rev, &profile) )
        return 2;
    disk = RSCache_Dat2DiskNewFromDirectory(cache);
    if( !disk )
        return 2;
    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_SOUND_EFFECTS);
    archive = table >= 0 ? RSCache_Dat2DiskArchiveNewLoad(disk, table, id) : NULL;
    if( !archive )
    {
        fprintf(stderr, "effect %d absent from %s\n", id, cache);
        RSCache_Dat2DiskFree(disk);
        return 2;
    }
    effect = RSCache_SoundEffectNewDecode(&profile, archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    memset(&rendered, 0, sizeof(rendered));
    if( !effect || !RSCache_SoundEffectRender(&profile, effect, &rendered) ||
        rendered.sample_count <= 0 )
    {
        fprintf(stderr, "effect %d has no audible tone\n", id);
        RSCache_SoundEffectFree(effect);
        RSCache_SoundPcmRelease(&rendered);
        RSCache_Dat2DiskFree(disk);
        return 2;
    }

    widened = malloc((size_t)rendered.sample_count * sizeof(int16_t));
    for( int i = 0; i < rendered.sample_count; i++ )
        widened[i] = (int16_t)(((int)rendered.samples[i] - 128) << 8);

    /* Twice the clip's length, so the tail past a loop span is included -- that
     * is where a clip cut off at loop_end shows up as a click. */
    frames = rendered.sample_count * 2;
    out = calloc((size_t)frames * 2, sizeof(int16_t));

    ToriRS_Mixer_Init(&mixer, HARNESS_RATE);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_ASSET_LOAD);
    command.asset_id = id;
    command.pcm = widened;
    command.sample_count = rendered.sample_count;
    command.sample_rate = rendered.sample_rate;
    command.loop_start = rendered.loop_start;
    command.loop_end = rendered.loop_end;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_VOICE_START);
    command.asset_id = id;
    command.voice_id = 1;
    command.source_id = id;
    ToriRS_Mixer_Apply(&mixer, &command);
    ToriRS_Mixer_Render(&mixer, out, frames);

    snprintf(label, sizeof(label), "  effect %d (%d samples @%dHz, loop %d..%d):",
             id, rendered.sample_count, rendered.sample_rate, rendered.loop_start,
             rendered.loop_end);
    AudioAnalyse(out, frames, 2, HARNESS_RATE, &analysis);
    AudioAnalyse_PrintReport(label, &analysis);
    if( opt->out_path && AudioWriteWav(opt->out_path, out, frames, 2, HARNESS_RATE) )
        printf("  wrote %s\n", opt->out_path);

    ToriRS_Mixer_Free(&mixer);
    free(out);
    free(widened);
    RSCache_SoundPcmRelease(&rendered);
    RSCache_SoundEffectFree(effect);
    RSCache_Dat2DiskFree(disk);
    return AudioAnalyse_LooksMusical(&analysis, &why) ? 0 : 1;
}

/**
 * Render many songs briefly and report each.
 *
 * A single render proves a single track; the interesting failures are the ones
 * that affect a handful of tracks out of hundreds, which only a sweep finds.
 */
static int
cmd_sweep(
    const char* cache,
    const char* rev,
    enum AudioSongSource source,
    const struct options* opt)
{
    int frames = (int)(opt->seconds * HARNESS_RATE);
    int bad = 0;
    int loaded = 0;
    int skipped = 0;

    for( int id = 0; id < opt->count; id++ )
    {
        struct AudioSongLoad load;
        struct ToriRS_MidiSynthStats stats;
        struct AudioAnalysis analysis;
        int16_t* pcm;
        char label[64];
        const char* why = NULL;

        if( !AudioSongLoad_Open(&load, cache, rev, source, id) )
        {
            AudioSongLoad_Free(&load);
            skipped++;
            continue;
        }
        memset(&stats, 0, sizeof(stats));
        g_peak_notes = 0;
        g_peak_held = 0;
        pcm = render_song(&load, frames, &stats);
        if( !pcm )
        {
            printf("track %-4d  RENDER FAILED\n", id);
            AudioSongLoad_Free(&load);
            bad++;
            continue;
        }
        loaded++;
        AudioAnalyse(pcm, frames, 2, HARNESS_RATE, &analysis);
        snprintf(
            label,
            sizeof(label),
            "%s %-4d",
            source == AUDIO_SONG_JINGLE ? "jingle" : "track ",
            id);
        if( !AudioAnalyse_LooksMusical(&analysis, &why) )
            bad++;
        /* A song whose soundfont did not assemble is a different failure from
         * one that renders badly, and it has to be visible either way. */
        if( load.patches_loaded < load.patches_named || stats.notes_dropped_no_patch > 0 ||
            stats.notes_dropped_no_sample > 0 || !AudioAnalyse_LooksMusical(&analysis, &why) )
        {
            AudioAnalyse_PrintLine(label, &analysis);
            printf(
                "             patches %d/%d, samples %d (%d missing), notes %d "
                "(dropped %d/%d)\n",
                load.patches_loaded,
                load.patches_named,
                load.samples_loaded,
                load.samples_missing,
                stats.notes_started,
                stats.notes_dropped_no_patch,
                stats.notes_dropped_no_sample);
            printf("             peak polyphony %d (%d held)\n", g_peak_notes, g_peak_held);
        }
        free(pcm);
        AudioSongLoad_Free(&load);
    }
    printf("\nsweep: %d rendered, %d absent, %d flagged\n", loaded, skipped, bad);
    return bad == 0 ? 0 : 1;
}

/**
 * The client's music path, without the client: synthesise a block, push it into
 * a mixer stream, render the mixer. The result must equal a direct render.
 */
static int
cmd_stream(
    const char* cache,
    const char* rev,
    int id,
    const struct options* opt)
{
    struct AudioSongLoad load;
    struct ToriRS_MidiSynth synth;
    struct ToriRS_Mixer mixer;
    struct ToriRS_AudioCommand command;
    struct AudioAnalysis direct_analysis;
    struct AudioAnalysis stream_analysis;
    int frames = (int)(opt->seconds * HARNESS_RATE);
    int block = opt->block > 0 ? opt->block : 441;
    int16_t* direct;
    int16_t* streamed;
    int16_t* scratch;
    long worst = 0;

    if( !AudioSongLoad_Open(&load, cache, rev, AUDIO_SONG_TRACK, id) )
    {
        AudioSongLoad_Free(&load);
        return 2;
    }
    direct = render_song(&load, frames, NULL);
    if( !direct )
    {
        AudioSongLoad_Free(&load);
        return 2;
    }

    streamed = calloc((size_t)frames * 2, sizeof(int16_t));
    scratch = calloc((size_t)block * 2, sizeof(int16_t));
    ToriRS_MidiSynth_Init(&synth, &load.bank, HARNESS_RATE);
    ToriRS_MidiSynth_Play(&synth, load.song->midi, load.song->midi_size, true);
    ToriRS_Mixer_Init(&mixer, HARNESS_RATE);
    ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_OPEN);
    command.stream_id = 0;
    command.channels = 2;
    command.sample_rate = HARNESS_RATE;
    command.bus = TORIRS_AUDIO_BUS_MUSIC;
    command.volume = TORIRS_AUDIO_VOLUME_MAX;
    ToriRS_Mixer_Apply(&mixer, &command);

    for( int done = 0; done < frames; )
    {
        int n = frames - done < block ? frames - done : block;
        ToriRS_MidiSynth_Render(&synth, scratch, n);
        ToriRS_AudioCommand_Init(&command, TORIRS_AUDIO_CMD_STREAM_PUSH);
        command.stream_id = 0;
        command.pcm = scratch;
        command.frame_count = n;
        command.channels = 2;
        ToriRS_Mixer_Apply(&mixer, &command);
        ToriRS_Mixer_Render(&mixer, streamed + (size_t)done * 2, n);
        done += n;
    }
    for( int i = 0; i < frames * 2; i++ )
    {
        long d = direct[i] - streamed[i];
        if( d < 0 )
            d = -d;
        if( d > worst )
            worst = d;
    }

    AudioAnalyse(direct, frames, 2, HARNESS_RATE, &direct_analysis);
    AudioAnalyse(streamed, frames, 2, HARNESS_RATE, &stream_analysis);
    AudioAnalyse_PrintLine("direct render", &direct_analysis);
    AudioAnalyse_PrintLine("via mixer stream", &stream_analysis);
    printf(
        "block %d frames: worst sample difference %ld, stream dropped %d starved %d\n",
        block,
        worst,
        ToriRS_Mixer_Stats(&mixer).stream_dropped_frames,
        ToriRS_Mixer_Stats(&mixer).stream_starved_frames);
    if( worst != 0 )
        printf("  the mixer stream is NOT transparent -- scheduling is colouring the audio\n");

    ToriRS_Mixer_Free(&mixer);
    ToriRS_MidiSynth_Free(&synth);
    free(scratch);
    free(streamed);
    free(direct);
    AudioSongLoad_Free(&load);
    return worst == 0 ? 0 : 1;
}

static int
cmd_wav(const char* path)
{
    struct AudioAnalysis analysis;
    int16_t* pcm;
    int frames;
    int channels;
    int rate;

    if( !AudioReadWav(path, &pcm, &frames, &channels, &rate) )
    {
        fprintf(stderr, "cannot read %s\n", path);
        return 2;
    }
    AudioAnalyse(pcm, frames, channels, rate, &analysis);
    AudioAnalyse_PrintReport(path, &analysis);
    free(pcm);
    return 0;
}

/**
 * Align two captures and report how far apart they are.
 *
 * The alignment matters: comparing spectra of two recordings that start at
 * different points in the same song reads as a difference in the audio when it
 * is only a difference in where you looked. That mistake cost an afternoon, so
 * the tool does the alignment.
 */
static int
cmd_diff(
    const char* path_a,
    const char* path_b)
{
    int16_t* a;
    int16_t* b;
    int frames_a;
    int frames_b;
    int channels_a;
    int channels_b;
    int rate_a;
    int rate_b;
    int probe = 2048;
    int probe_at;
    long best_error = -1;
    int best_offset = 0;
    long worst = 0;
    double mean = 0.0;
    int overlap;

    if( !AudioReadWav(path_a, &a, &frames_a, &channels_a, &rate_a) )
        return 2;
    if( !AudioReadWav(path_b, &b, &frames_b, &channels_b, &rate_b) )
    {
        free(a);
        return 2;
    }
    probe_at = frames_a > rate_a ? rate_a / 2 : 0;
    if( probe_at + probe > frames_a )
        probe = frames_a - probe_at;

    for( int off = 0; off + probe < frames_b; off++ )
    {
        long error = 0;
        for( int i = 0; i < probe; i += 7 )
        {
            long d = a[(size_t)(probe_at + i) * channels_a] - b[(size_t)(off + i) * channels_b];
            error += d < 0 ? -d : d;
        }
        if( best_error < 0 || error < best_error )
        {
            best_error = error;
            best_offset = off;
        }
        if( best_error == 0 )
            break;
    }

    overlap = frames_a - probe_at;
    if( overlap > frames_b - best_offset )
        overlap = frames_b - best_offset;
    for( int i = 0; i < overlap; i++ )
    {
        long d = a[(size_t)(probe_at + i) * channels_a] - b[(size_t)(best_offset + i) * channels_b];
        if( d < 0 )
            d = -d;
        mean += (double)d;
        if( d > worst )
            worst = d;
    }
    if( overlap > 0 )
        mean /= overlap;

    printf("%s vs %s\n", path_a, path_b);
    printf("  best alignment: b[%d] == a[%d] (shift %d), probe residual %ld\n",
           best_offset, probe_at, best_offset - probe_at, best_error);
    printf("  overlap %d frames: worst |diff| %ld, mean |diff| %.1f\n", overlap, worst, mean);
    printf("  %s\n", worst == 0 ? "identical once aligned" : "they differ");
    free(a);
    free(b);
    return worst == 0 ? 0 : 1;
}

int
main(
    int argc,
    char** argv)
{
    struct options opt;
    const char* positional[5] = { NULL, NULL, NULL, NULL, NULL };
    int positional_count = 0;

    memset(&opt, 0, sizeof(opt));
    opt.seconds = 8.0;
    opt.count = 24;
    opt.block = 441;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "-o") == 0 && i + 1 < argc )
            opt.out_path = argv[++i];
        else if( strcmp(argv[i], "-s") == 0 && i + 1 < argc )
            opt.seconds = atof(argv[++i]);
        else if( strcmp(argv[i], "-n") == 0 && i + 1 < argc )
            opt.count = atoi(argv[++i]);
        else if( strcmp(argv[i], "-v") == 0 )
            g_verbose = 1;
        else if( strcmp(argv[i], "-b") == 0 && i + 1 < argc )
            opt.block = atoi(argv[++i]);
        else if( positional_count < 5 )
            positional[positional_count++] = argv[i];
    }
    if( positional_count < 1 )
    {
        usage();
        return 1;
    }
    if( opt.seconds <= 0.0 )
        opt.seconds = 8.0;

    if( strcmp(positional[0], "song") == 0 && positional_count >= 4 )
        return cmd_song(positional[1], positional[2], atoi(positional[3]), AUDIO_SONG_TRACK, &opt);
    if( strcmp(positional[0], "jingle") == 0 && positional_count >= 4 )
        return cmd_song(positional[1], positional[2], atoi(positional[3]), AUDIO_SONG_JINGLE, &opt);
    if( strcmp(positional[0], "effect") == 0 && positional_count >= 4 )
        return cmd_effect(positional[1], positional[2], atoi(positional[3]), &opt);
    if( strcmp(positional[0], "sweep") == 0 && positional_count >= 3 )
        return cmd_sweep(positional[1], positional[2], AUDIO_SONG_TRACK, &opt);
    if( strcmp(positional[0], "sweepjingles") == 0 && positional_count >= 3 )
        return cmd_sweep(positional[1], positional[2], AUDIO_SONG_JINGLE, &opt);
    if( strcmp(positional[0], "stream") == 0 && positional_count >= 4 )
        return cmd_stream(positional[1], positional[2], atoi(positional[3]), &opt);
    if( strcmp(positional[0], "seek") == 0 && positional_count >= 4 )
        return cmd_seek(
            positional[1],
            positional[2],
            atoi(positional[3]),
            positional_count >= 5 ? atoi(positional[4]) : 1920,
            &opt);
    if( strcmp(positional[0], "midi") == 0 && positional_count >= 4 )
        return cmd_midi(positional[1], positional[2], atoi(positional[3]), AUDIO_SONG_TRACK);
    if( strcmp(positional[0], "wav") == 0 && positional_count >= 2 )
        return cmd_wav(positional[1]);
    if( strcmp(positional[0], "diff") == 0 && positional_count >= 3 )
        return cmd_diff(positional[1], positional[2]);

    usage();
    return 1;
}
