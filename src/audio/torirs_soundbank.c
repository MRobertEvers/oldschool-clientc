#include "audio/torirs_soundbank.h"

#include "audio/torirs_audio.h"

#include <rscache.h>

#include <stdlib.h>
#include <string.h>

void
ToriRS_SoundBank_Init(struct ToriRS_SoundBank* bank)
{
    if( !bank )
        return;
    memset(bank, 0, sizeof(*bank));
}

static void
free_sample_slot(
    struct ToriRS_SoundBank* bank,
    struct ToriRS_SoundBankSample* sample)
{
    bank->sample_bytes -= (long)sample->sound.sample_count * (long)sizeof(int16_t);
    free(sample->samples);
    memset(sample, 0, sizeof(*sample));
    sample->table = -1;
    sample->id = -1;
}

static void
free_patch_slot(struct ToriRS_SoundBankPatch* patch)
{
    RSCache_MusicPatchFree(patch->patch);
    memset(patch, 0, sizeof(*patch));
    patch->patch_id = -1;
}

void
ToriRS_SoundBank_Free(struct ToriRS_SoundBank* bank)
{
    if( !bank )
        return;
    for( int i = 0; i < bank->patch_count; i++ )
        free_patch_slot(&bank->patches[i]);
    for( int i = 0; i < bank->sample_count; i++ )
        free_sample_slot(bank, &bank->samples[i]);
    free(bank->patches);
    free(bank->samples);
    memset(bank, 0, sizeof(*bank));
}

struct ToriRS_SoundBankPatch*
ToriRS_SoundBank_FindPatch(
    struct ToriRS_SoundBank* bank,
    int patch_id)
{
    if( !bank || patch_id < 0 )
        return NULL;
    for( int i = 0; i < bank->patch_count; i++ )
    {
        if( bank->patches[i].patch_id == patch_id )
            return &bank->patches[i];
    }
    return NULL;
}

struct ToriRS_SoundBankSample*
ToriRS_SoundBank_FindSample(
    struct ToriRS_SoundBank* bank,
    int table,
    int id)
{
    if( !bank || id < 0 )
        return NULL;
    for( int i = 0; i < bank->sample_count; i++ )
    {
        if( bank->samples[i].table == table && bank->samples[i].id == id )
            return &bank->samples[i];
    }
    return NULL;
}

struct ToriRS_SoundBankPatch*
ToriRS_SoundBank_AddPatch(
    struct ToriRS_SoundBank* bank,
    int patch_id,
    struct RSCache_MusicPatch* patch)
{
    struct ToriRS_SoundBankPatch* slot;

    if( !bank || patch_id < 0 || !patch )
    {
        RSCache_MusicPatchFree(patch);
        return NULL;
    }
    slot = ToriRS_SoundBank_FindPatch(bank, patch_id);
    if( slot )
    {
        RSCache_MusicPatchFree(slot->patch);
    }
    else
    {
        if( bank->patch_count >= bank->patch_capacity )
        {
            int next = bank->patch_capacity > 0 ? bank->patch_capacity * 2 : 16;
            struct ToriRS_SoundBankPatch* grown =
                realloc(bank->patches, (size_t)next * sizeof(*grown));
            if( !grown )
            {
                RSCache_MusicPatchFree(patch);
                return NULL;
            }
            bank->patches = grown;
            bank->patch_capacity = next;
        }
        slot = &bank->patches[bank->patch_count++];
        memset(slot, 0, sizeof(*slot));
    }
    slot->patch_id = patch_id;
    slot->patch = patch;
    slot->resolved = false;
    for( int i = 0; i < 128; i++ )
        slot->note_sample[i] = -1;
    return slot;
}

struct ToriRS_SoundBankSample*
ToriRS_SoundBank_AddSample(
    struct ToriRS_SoundBank* bank,
    int table,
    int id,
    int16_t* samples,
    int sample_count,
    int sample_rate,
    int loop_start,
    int loop_end,
    bool ping_pong)
{
    struct ToriRS_SoundBankSample* slot;

    if( !bank || id < 0 || !samples || sample_count <= 0 )
    {
        free(samples);
        return NULL;
    }
    slot = ToriRS_SoundBank_FindSample(bank, table, id);
    if( slot )
    {
        free_sample_slot(bank, slot);
    }
    else
    {
        if( bank->sample_count >= bank->sample_capacity )
        {
            int next = bank->sample_capacity > 0 ? bank->sample_capacity * 2 : 64;
            struct ToriRS_SoundBankSample* grown =
                realloc(bank->samples, (size_t)next * sizeof(*grown));
            if( !grown )
            {
                free(samples);
                return NULL;
            }
            bank->samples = grown;
            bank->sample_capacity = next;
            /*
             * A grown array moves. Every patch holds *indices*, not pointers,
             * precisely so this realloc cannot dangle -- which is why
             * note_sample is an int and ToriRS_SoundBank_NoteSound does the
             * indirection.
             */
        }
        slot = &bank->samples[bank->sample_count++];
        memset(slot, 0, sizeof(*slot));
    }
    slot->table = table;
    slot->id = id;
    slot->samples = samples;
    slot->sound.samples = samples;
    slot->sound.sample_count = sample_count;
    slot->sound.sample_rate = sample_rate > 0 ? sample_rate : TORIRS_AUDIO_SAMPLE_RATE;
    slot->sound.loop_start = loop_start;
    slot->sound.loop_end = loop_end;
    slot->sound.ping_pong = ping_pong;
    bank->sample_bytes += (long)sample_count * (long)sizeof(int16_t);
    return slot;
}

/** Index of a sample slot, or -1. */
static int
sample_index(
    struct ToriRS_SoundBank* bank,
    int table,
    int id)
{
    for( int i = 0; i < bank->sample_count; i++ )
    {
        if( bank->samples[i].table == table && bank->samples[i].id == id )
            return i;
    }
    return -1;
}

int
ToriRS_SoundBank_ResolvePatch(
    struct ToriRS_SoundBank* bank,
    struct ToriRS_SoundBankPatch* patch)
{
    int missing = 0;

    if( !bank || !patch || !patch->patch )
        return 0;
    for( int note = 0; note < 128; note++ )
    {
        int id = RSCache_MusicPatchNoteSampleId(patch->patch, note);
        int table;
        int index;

        if( id < 0 )
            continue;
        if( patch->note_sample[note] >= 0 )
            continue;
        table = RSCache_MusicPatchNoteIsMusicSample(patch->patch, note)
                    ? TORIRS_SOUNDBANK_TABLE_MUSIC
                    : TORIRS_SOUNDBANK_TABLE_EFFECTS;
        index = sample_index(bank, table, id);
        if( index < 0 )
        {
            missing++;
            continue;
        }
        patch->note_sample[note] = index;
        bank->samples[index].ref_count++;
    }
    patch->resolved = missing == 0;
    return missing;
}

const struct ToriRS_PcmSound*
ToriRS_SoundBank_NoteSound(
    struct ToriRS_SoundBank* bank,
    const struct ToriRS_SoundBankPatch* patch,
    int note)
{
    int index;

    if( !bank || !patch || note < 0 || note >= 128 )
        return NULL;
    index = patch->note_sample[note];
    if( index < 0 || index >= bank->sample_count )
        return NULL;
    if( !bank->samples[index].samples )
        return NULL;
    return &bank->samples[index].sound;
}

void
ToriRS_SoundBank_RetainPatch(
    struct ToriRS_SoundBank* bank,
    int patch_id)
{
    struct ToriRS_SoundBankPatch* patch = ToriRS_SoundBank_FindPatch(bank, patch_id);
    if( patch )
        patch->ref_count++;
}

/** Drop the last slot into `index`, keeping the arrays dense. */
static void
compact_patch(
    struct ToriRS_SoundBank* bank,
    int index)
{
    bank->patch_count--;
    if( index != bank->patch_count )
        bank->patches[index] = bank->patches[bank->patch_count];
}

/**
 * Remove a sample slot and fix up every patch index that pointed past it.
 *
 * Compaction moves the last slot into the hole, so exactly two indices change:
 * the removed one becomes invalid and the last one becomes `index`.
 */
static void
compact_sample(
    struct ToriRS_SoundBank* bank,
    int index)
{
    int moved_from;

    free_sample_slot(bank, &bank->samples[index]);
    bank->sample_count--;
    moved_from = bank->sample_count;
    if( index != moved_from )
        bank->samples[index] = bank->samples[moved_from];

    for( int p = 0; p < bank->patch_count; p++ )
    {
        for( int note = 0; note < 128; note++ )
        {
            int at = bank->patches[p].note_sample[note];
            if( at == index )
                bank->patches[p].note_sample[note] = -1;
            else if( at == moved_from )
                bank->patches[p].note_sample[note] = index;
        }
    }
}

void
ToriRS_SoundBank_ReleasePatch(
    struct ToriRS_SoundBank* bank,
    int patch_id)
{
    struct ToriRS_SoundBankPatch* patch = ToriRS_SoundBank_FindPatch(bank, patch_id);
    int patch_index;

    if( !bank || !patch )
        return;
    if( --patch->ref_count > 0 )
        return;

    for( int note = 0; note < 128; note++ )
    {
        int index = patch->note_sample[note];
        if( index < 0 || index >= bank->sample_count )
            continue;
        patch->note_sample[note] = -1;
        if( --bank->samples[index].ref_count <= 0 )
            compact_sample(bank, index);
    }

    patch_index = (int)(patch - bank->patches);
    free_patch_slot(patch);
    compact_patch(bank, patch_index);
}
