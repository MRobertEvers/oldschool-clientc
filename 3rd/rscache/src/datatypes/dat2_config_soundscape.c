#include "dat2_config_soundscape.h"

#include "../rsbuffer.h"

#include <stdlib.h>
#include <string.h>

/*
 * Transcribed from the OldSchool 239 client's `class410` constructor
 * (`Deobfuscator/src_osrs239_rl1_12_33/deob/class410.java`), which is the only
 * statement of this format:
 *
 *     case 1: n = g1; ids = int[n]; for i: ids[i] = g2
 *     case 2: min = g2 * 20; max = g2 * 20; n = g1; ids = int[n];
 *             for i: ids[i] = g2
 *             if (n <= 48 && sets.size() < 8) sets.add(new Set(ids, min, max))
 *     case 3: fadeIn  = (curve = g1, duration = g2 * 20)
 *     case 4: fadeOut = (curve = g1, duration = g2 * 20)
 *
 * Two details of case 2 are load-bearing and easy to lose. The bytes are read
 * *before* the caps are tested, so an oversized set still advances the cursor --
 * dropping it early would desync every opcode after it. And the ×20 is a
 * tick-to-millisecond conversion done during the read, so `min_ms`/`max_ms` are
 * already in milliseconds by the time anyone sees them.
 */

void
RSCache_Dat2ConfigSoundscapeDecodeInplace(
    struct RSCache_Dat2ConfigSoundscape* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buf;

    if( !entry )
        return;
    memset(entry, 0, sizeof(*entry));
    if( !data || data_size <= 0 )
        return;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode;

        if( buf.position >= buf.size )
            return; /* ran out before the terminator: _consumed stays 0 */
        opcode = g1(&buf);
        if( opcode == 0 )
            break;
        if( entry->order_count < RSCACHE_SOUNDSCAPE_MAX_OPCODES )
            entry->order[entry->order_count++] = (uint8_t)opcode;

        switch( opcode )
        {
        case 1:
        {
            int count = g1(&buf);
            /* Assignment, not append -- the reference does `def.ids = new
             * int[n]`, so a second opcode 1 replaces the first outright. */
            if( entry->loop_ids )
                entry->superseded_loops++;
            free(entry->loop_ids);
            entry->loop_ids = NULL;
            entry->loop_count = 0;
            if( count > 0 )
            {
                entry->loop_ids = (int*)malloc((size_t)count * sizeof(int));
                if( !entry->loop_ids )
                    return;
                entry->loop_count = count;
            }
            for( int i = 0; i < count; i++ )
            {
                int id = g2(&buf);
                if( entry->loop_ids )
                    entry->loop_ids[i] = id;
            }
            break;
        }

        case 2:
        {
            int min_ms = g2(&buf) * 20;
            int max_ms = g2(&buf) * 20;
            int count = g1(&buf);
            int* ids = NULL;

            if( count > 0 )
            {
                ids = (int*)malloc((size_t)count * sizeof(int));
                if( !ids )
                    return;
            }
            for( int i = 0; i < count; i++ )
            {
                int id = g2(&buf);
                if( ids )
                    ids[i] = id;
            }

            /* The caps are applied after the read, not instead of it. */
            if( count <= RSCACHE_SOUNDSCAPE_MAX_SET_IDS &&
                entry->set_count < RSCACHE_SOUNDSCAPE_MAX_SETS )
            {
                struct RSCache_SoundscapeSet* set = &entry->sets[entry->set_count++];
                set->ids = ids;
                set->id_count = count;
                set->min_ms = min_ms;
                set->max_ms = max_ms;
            }
            else
            {
                entry->dropped_sets++;
                free(ids);
            }
            break;
        }

        case 3:
            entry->fade_in_curve = g1(&buf);
            entry->fade_in_ms = g2(&buf) * 20;
            break;

        case 4:
            entry->fade_out_curve = g1(&buf);
            entry->fade_out_ms = g2(&buf) * 20;
            break;

        default:
            /*
             * Stop, do not skip. An unknown opcode has an unknown payload
             * length, so every read after it comes from the middle of that
             * payload -- the same rule the loc and npc decoders follow, and for
             * the same reason. `_consumed` stays 0, which is how a caller tells
             * a short decode from a complete one.
             */
            return;
        }
    }

    entry->_consumed = (int)buf.position;
}

struct RSCache_Dat2ConfigSoundscape*
RSCache_Dat2ConfigSoundscapeNewDecode(const void* data, int data_size)
{
    struct RSCache_Dat2ConfigSoundscape* entry =
        (struct RSCache_Dat2ConfigSoundscape*)calloc(1, sizeof(*entry));

    if( !entry )
        return NULL;
    RSCache_Dat2ConfigSoundscapeDecodeInplace(entry, data, data_size);
    return entry;
}

void
RSCache_Dat2ConfigSoundscapeFreeInplace(struct RSCache_Dat2ConfigSoundscape* entry)
{
    if( !entry )
        return;
    free(entry->loop_ids);
    entry->loop_ids = NULL;
    entry->loop_count = 0;
    for( int i = 0; i < RSCACHE_SOUNDSCAPE_MAX_SETS; i++ )
    {
        free(entry->sets[i].ids);
        entry->sets[i].ids = NULL;
        entry->sets[i].id_count = 0;
    }
    entry->set_count = 0;
}

void
RSCache_Dat2ConfigSoundscapeFree(struct RSCache_Dat2ConfigSoundscape* entry)
{
    if( !entry )
        return;
    RSCache_Dat2ConfigSoundscapeFreeInplace(entry);
    free(entry);
}

uint32_t
RSCache_Dat2ConfigSoundscapeEncodeBound(const struct RSCache_Dat2ConfigSoundscape* entry)
{
    uint32_t bound = 1; /* the terminator */

    if( !entry )
        return 0;
    /*
     * Opcode 1 is counted even when it carries no ids. A decoded record can
     * hold `order = {1, ...}` with `loop_count == 0` -- the writer emitted an
     * empty list -- and the encoder reproduces that, so the two bytes are real.
     * Gating this on `loop_count > 0` under-reserved by exactly those two bytes
     * and the buffer asserted on the last record of the sweep.
     */
    bound += 2 + (uint32_t)entry->loop_count * 2;
    for( int i = 0; i < entry->set_count; i++ )
        bound += 6 + (uint32_t)entry->sets[i].id_count * 2;
    bound += 4; /* opcode 3 */
    bound += 4; /* opcode 4 */
    return bound;
}

/* Emit one opcode's payload. Returns 0 when the record cannot supply it. */
static int
soundscape_emit(
    struct RSCache_Buffer* buf,
    const struct RSCache_Dat2ConfigSoundscape* entry,
    int opcode,
    int* set_cursor)
{
    switch( opcode )
    {
    case 1:
        p1(buf, 1);
        p1(buf, entry->loop_count);
        for( int i = 0; i < entry->loop_count; i++ )
            p2(buf, entry->loop_ids[i]);
        return 1;

    case 2:
    {
        const struct RSCache_SoundscapeSet* set;
        if( *set_cursor >= entry->set_count )
            return 0;
        set = &entry->sets[(*set_cursor)++];
        p1(buf, 2);
        /*
         * The x20 is undone here, so a round trip is byte-exact for the tick
         * values the cache actually carries. A millisecond value that is not a
         * multiple of 20 cannot have come off the wire and would not round
         * trip; nothing in this tree produces one.
         */
        p2(buf, set->min_ms / 20);
        p2(buf, set->max_ms / 20);
        p1(buf, set->id_count);
        for( int j = 0; j < set->id_count; j++ )
            p2(buf, set->ids[j]);
        return 1;
    }

    case 3:
        p1(buf, 3);
        p1(buf, entry->fade_in_curve);
        p2(buf, entry->fade_in_ms / 20);
        return 1;

    case 4:
        p1(buf, 4);
        p1(buf, entry->fade_out_curve);
        p2(buf, entry->fade_out_ms / 20);
        return 1;

    default:
        return 0;
    }
}

uint32_t
RSCache_Dat2ConfigSoundscapeEncode(
    const struct RSCache_Dat2ConfigSoundscape* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buf;
    int set_cursor = 0;

    if( !entry || !out )
        return 0;
    /* A record whose decode threw payload away cannot be rebuilt from what was
     * kept. Saying so beats writing a shorter record that looks fine. */
    if( entry->dropped_sets > 0 || entry->superseded_loops > 0 )
        return 0;
    if( out_capacity < RSCache_Dat2ConfigSoundscapeEncodeBound(entry) )
        return 0;
    RSCache_BufferInit(&buf, out, out_capacity);

    if( entry->order_count > 0 )
    {
        int seen[5] = { 0, 0, 0, 0, 0 };

        for( int i = 0; i < entry->order_count; i++ )
        {
            int opcode = entry->order[i];
            /* The bound reserves one of each singleton opcode, so a record
             * whose order repeats one would overrun. No cache record does;
             * refusing beats writing past the caller's buffer. */
            if( opcode == 1 || opcode == 3 || opcode == 4 )
            {
                if( seen[opcode] )
                    return 0;
                seen[opcode] = 1;
            }
            if( !soundscape_emit(&buf, entry, opcode, &set_cursor) )
                return 0;
        }
    }
    else
    {
        /* Built in memory rather than decoded: canonical order. */
        if( entry->loop_count > 0 && !soundscape_emit(&buf, entry, 1, &set_cursor) )
            return 0;
        while( set_cursor < entry->set_count )
            if( !soundscape_emit(&buf, entry, 2, &set_cursor) )
                return 0;
        if( (entry->fade_in_curve != 0 || entry->fade_in_ms != 0) &&
            !soundscape_emit(&buf, entry, 3, &set_cursor) )
            return 0;
        if( (entry->fade_out_curve != 0 || entry->fade_out_ms != 0) &&
            !soundscape_emit(&buf, entry, 4, &set_cursor) )
            return 0;
    }
    p1(&buf, 0);

    return buf.position;
}
