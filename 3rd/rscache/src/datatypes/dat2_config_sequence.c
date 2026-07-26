#include "dat2_config_sequence.h"

#include "../dat2disk.h"
#include "../rsbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
free_sequence(struct RSCache_Dat2ConfigSequence* def);

// package net.runelite.cache.definitions.loaders;

// import lombok.Data;
// import lombok.experimental.Accessors;
// import lombok.extern.slf4j.Slf4j;
// import net.runelite.cache.definitions.SequenceDefinition;
// import net.runelite.cache.io.InputStream;

// @Accessors(chain = true)
// @Data
// @Slf4j
// public class SequenceLoader
// {
// 	public static final int REV_220_SEQ_ARCHIVE_REV = 1141;
// 	public static final int REV_226_SEQ_ARCHIVE_REV = 1268;

// 	private boolean rev220FrameSounds = true;
// 	private boolean rev226 = true;

// 	public SequenceLoader configureForRevision(int rev)
// 	{
// 		this.rev220FrameSounds = rev > REV_220_SEQ_ARCHIVE_REV;
// 		this.rev226 = rev > REV_226_SEQ_ARCHIVE_REV;
// 		return this;
// 	}

// 	public SequenceDefinition load(int id, byte[] b)
// 	{
// 		SequenceDefinition def = new SequenceDefinition(id);
// 		InputStream is = new InputStream(b);

// 		while (true)
// 		{
// 			int opcode = is.readUnsignedByte();
// 			if (opcode == 0)
// 			{
// 				break;
// 			}

// 			this.decodeValues(opcode, def, is);
// 		}

// 		return def;
// 	}

// 	private void decodeValues(int opcode, SequenceDefinition def, InputStream stream)
// 	{
// 		int var3;
// 		int var4;
// 		if (opcode == 1)
// 		{
// 			var3 = stream.readUnsignedShort();
// 			def.frameLengths = new int[var3];

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				def.frameLengths[var4] = stream.readUnsignedShort();
// 			}

// 			def.frameIDs = new int[var3];

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				def.frameIDs[var4] = stream.readUnsignedShort();
// 			}

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				def.frameIDs[var4] += stream.readUnsignedShort() << 16;
// 			}
// 		}
// 		else if (opcode == 2)
// 		{
// 			def.frameStep = stream.readUnsignedShort();
// 		}
// 		else if (opcode == 3)
// 		{
// 			var3 = stream.readUnsignedByte();
// 			def.interleaveLeave = new int[1 + var3];

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				def.interleaveLeave[var4] = stream.readUnsignedByte();
// 			}

// 			def.interleaveLeave[var3] = 9999999;
// 		}
// 		else if (opcode == 4)
// 		{
// 			def.stretches = true;
// 		}
// 		else if (opcode == 5)
// 		{
// 			def.forcedPriority = stream.readUnsignedByte();
// 		}
// 		else if (opcode == 6)
// 		{
// 			def.leftHandItem = stream.readUnsignedShort();
// 		}
// 		else if (opcode == 7)
// 		{
// 			def.rightHandItem = stream.readUnsignedShort();
// 		}
// 		else if (opcode == 8)
// 		{
// 			def.maxLoops = stream.readUnsignedByte();
// 		}
// 		else if (opcode == 9)
// 		{
// 			def.precedenceAnimating = stream.readUnsignedByte();
// 		}
// 		else if (opcode == 10)
// 		{
// 			def.priority = stream.readUnsignedByte();
// 		}
// 		else if (opcode == 11)
// 		{
// 			def.replyMode = stream.readUnsignedByte();
// 		}
// 		else if (opcode == 12)
// 		{
// 			var3 = stream.readUnsignedByte();
// 			def.chatFrameIds = new int[var3];

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				def.chatFrameIds[var4] = stream.readUnsignedShort();
// 			}

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				def.chatFrameIds[var4] += stream.readUnsignedShort() << 16;
// 			}
// 		}
// 		else if (opcode == 13 && !rev226)
// 		{
// 			var3 = stream.readUnsignedByte();

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				def.frameSounds.put(var4, this.readFrameSound(stream));
// 			}
// 		}
// 		else if (opcode == (rev226 ? 13 : 14))
// 		{
// 			def.animMayaID = stream.readInt();
// 		}
// 		else if (opcode == (rev226 ? 14 : 15))
// 		{
// 			var3 = stream.readUnsignedShort();

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				int frame = stream.readUnsignedShort();
// 				def.frameSounds.put(frame, this.readFrameSound(stream));
// 			}
// 		}
// 		else if (opcode == (rev226 ? 15 : 16))
// 		{
// 			def.animMayaStart = stream.readUnsignedShort();
// 			def.animMayaEnd = stream.readUnsignedShort();
// 		}
// 		else if (opcode == 17)
// 		{
// 			def.animMayaMasks = new boolean[256];

// 			var3 = stream.readUnsignedByte();

// 			for (var4 = 0; var4 < var3; ++var4)
// 			{
// 				def.animMayaMasks[stream.readUnsignedByte()] = true;
// 			}
// 		}
// 		else if (opcode == 18)
// 		{
// 			def.debugName = stream.readString();
// 		}
// 		else
// 		{
// 			log.warn("Unrecognized opcode {}", opcode);
// 		}
// 	}

// 	private SequenceDefinition.Sound readFrameSound(InputStream stream)
// 	{
// 		int id;
// 		int loops;
// 		int location;
// 		int retain;
// 		int weight = -1;
// 		if (!rev220FrameSounds)
// 		{
// 			int bits = stream.read24BitInt();
// 			location = bits & 15;
// 			id = bits >> 8;
// 			loops = bits >> 4 & 7;
// 			retain = 0;
// 		}
// 		else
// 		{
// 			id = stream.readUnsignedShort();
// 			if (rev226)
// 			{
// 				weight = stream.readUnsignedByte();
// 			}
// 			loops = stream.readUnsignedByte();
// 			location = stream.readUnsignedByte();
// 			retain = stream.readUnsignedByte();
// 		}

// 		if (id >= 1 && loops >= 1 && location >= 0 && retain >= 0)
// 		{
// 			return new SequenceDefinition.Sound(id, loops, location, retain, weight);
// 		}
// 		else
// 		{
// 			return null;
// 		}
// 	}
// }

/* The era thresholds now live in the header as
 * RSCACHE_SEQUENCE_ARCHIVE_REV_220 / _226, next to the codec-version constants
 * they select, so a caller can reason about them without reading this file. */

static void
add_frame_sound(
    struct RSCache_Dat2ConfigFrameSoundMap* map,
    int frame,
    struct RSCache_Dat2ConfigFrameSound sound)
{
    if( map->count >= map->capacity )
    {
        int new_capacity = map->capacity == 0 ? 4 : map->capacity * 2;
        map->frames = realloc(map->frames, new_capacity * sizeof(int));
        map->sounds = realloc(map->sounds, new_capacity * sizeof(struct RSCache_Dat2ConfigFrameSound));
        map->capacity = new_capacity;
    }
    map->frames[map->count] = frame;
    map->sounds[map->count] = sound;
    map->count++;
}

static void
decode_frame_sound_v1(
    struct RSCache_Dat2ConfigFrameSound* sound,
    struct RSCache_Buffer* buffer)
{
    // Old format: 24-bit int with packed fields
    int bits = g3(buffer);
    sound->location = bits & 15;
    sound->id = bits >> 8;
    sound->loops = (bits >> 4) & 7;
    sound->retain = 0;
    sound->weight = -1;
}

static void
decode_frame_sound_v2(
    struct RSCache_Dat2ConfigFrameSound* sound,
    struct RSCache_Buffer* buffer)
{
    // New format: separate fields
    sound->id = g2(buffer);
    sound->loops = g1(buffer);
    sound->location = g1(buffer);
    sound->retain = g1(buffer);
    sound->weight = -1;
}

static void
decode_frame_sound_v3(
    struct RSCache_Dat2ConfigFrameSound* sound,
    struct RSCache_Buffer* buffer)
{
    // New format: frame sounds with weights
    sound->id = g2(buffer);
    sound->weight = g1(buffer);
    sound->loops = g1(buffer);
    sound->location = g1(buffer);
    sound->retain = g1(buffer);
}

static void
handle_frame_sounds_pre_220(
    struct RSCache_Dat2ConfigSequence* def,
    struct RSCache_Buffer* buffer)
{
    int var3 = g1(buffer);
    for( int var4 = 0; var4 < var3; ++var4 )
    {
        struct RSCache_Dat2ConfigFrameSound sound = { 0 };
        decode_frame_sound_v1(&sound, buffer);
        if( sound.id >= 1 && sound.loops >= 1 && sound.location >= 0 && sound.retain >= 0 )
        {
            add_frame_sound(&def->frame_sounds, var4, sound);
        }
    }
}

static void
handle_frame_sounds_220_226(
    struct RSCache_Dat2ConfigSequence* def,
    struct RSCache_Buffer* buffer)
{
    int var3 = g1(buffer);
    for( int var4 = 0; var4 < var3; ++var4 )
    {
        struct RSCache_Dat2ConfigFrameSound sound = { 0 };
        decode_frame_sound_v2(&sound, buffer);
        if( sound.id >= 1 && sound.loops >= 1 && sound.location >= 0 && sound.retain >= 0 )
        {
            add_frame_sound(&def->frame_sounds, var4, sound);
        }
    }
}

/*
 * The *framed* list, as used by opcode 15 in the v1/v2 eras (and opcode 14 in v3).
 *
 * Distinct from handle_frame_sounds_pre_220 / _220_226, which are the *positional*
 * lists used by opcode 13: those take a u8 count and derive the frame from the loop
 * index, whereas this takes a u16 count and reads an explicit frame per entry.
 *
 * Both forms coexist in the same era — see RuneLite's SequenceLoader, quoted at the
 * top of this file: `opcode == 13 && !rev226` is positional while
 * `opcode == (rev226 ? 14 : 15)` is framed. This decoder previously used the
 * positional handler for opcode 15 too, which misaligned any record that used it.
 *
 * `record` selects the per-sound shape, which follows the era independently of the
 * list shape.
 */
static void
handle_frame_sounds_framed(
    struct RSCache_Dat2ConfigSequence* def,
    struct RSCache_Buffer* buffer,
    void (*record)(struct RSCache_Dat2ConfigFrameSound*, struct RSCache_Buffer*))
{
    int count = g2(buffer);
    for( int i = 0; i < count; ++i )
    {
        int frame = g2(buffer);
        struct RSCache_Dat2ConfigFrameSound sound = { 0 };
        record(&sound, buffer);
        if( sound.id >= 1 && sound.loops >= 1 && sound.location >= 0 && sound.retain >= 0 )
            add_frame_sound(&def->frame_sounds, frame, sound);
    }
}

static void
handle_frame_sounds_226_plus(
    struct RSCache_Dat2ConfigSequence* def,
    struct RSCache_Buffer* buffer)
{
    int var3 = g2(buffer);
    for( int var4 = 0; var4 < var3; ++var4 )
    {
        int frame = g2(buffer);
        struct RSCache_Dat2ConfigFrameSound sound = { 0 };
        decode_frame_sound_v3(&sound, buffer);
        if( sound.id >= 1 && sound.loops >= 1 && sound.location >= 0 && sound.retain >= 0 )
        {
            add_frame_sound(&def->frame_sounds, frame, sound);
        }
    }
}

static void
decode_sequence_v1(
    struct RSCache_Dat2ConfigSequence* def,
    struct RSCache_Buffer* buffer)
{
    // Initialize frame sounds map
    def->frame_sounds.frames = NULL;
    def->frame_sounds.sounds = NULL;
    def->frame_sounds.count = 0;
    def->frame_sounds.capacity = 0;

    while( true )
    {
        int opcode = g1(buffer);
        if( opcode == 0 )
        {
            def->_consumed = (int)buffer->position;
            break;
        }

        switch( opcode )
        {
        case 1:
        {
            int var3 = g2(buffer);
            def->frame_count = var3;
            def->frame_lengths = malloc(var3 * sizeof(int));
            def->frame_ids = malloc(var3 * sizeof(int));
            memset(def->frame_lengths, 0, var3 * sizeof(int));
            memset(def->frame_ids, 0, var3 * sizeof(int));

            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_lengths[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] += g2(buffer) << 16;
            }
            break;
        }
        case 2:
            def->frame_step = g2(buffer);
            break;
        case 3:
        {
            int var3 = g1(buffer);
            def->interleave_leave = malloc((var3 + 1) * sizeof(int));
            memset(def->interleave_leave, 0, (var3 + 1) * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->interleave_leave[var4] = g1(buffer);
            }
            def->interleave_leave[var3] = 9999999;
            break;
        }
        case 4:
            def->stretches = true;
            break;
        case 5:
            def->forced_priority = g1(buffer);
            break;
        case 6:
            def->left_hand_item = g2(buffer);
            break;
        case 7:
            def->right_hand_item = g2(buffer);
            break;
        case 8:
            def->max_loops = g1(buffer);
            break;
        case 9:
            def->precedence_animating = g1(buffer);
            break;
        case 10:
            def->priority = g1(buffer);
            break;
        case 11:
            def->reply_mode = g1(buffer);
            break;
        case 12:
        {
            int var3 = g1(buffer);
            def->chat_frame_id_count = var3;
            def->chat_frame_ids = malloc(var3 * sizeof(int));
            memset(def->chat_frame_ids, 0, var3 * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] += g2(buffer) << 16;
            }
            break;
        }
        case 13:
            handle_frame_sounds_pre_220(def, buffer);
            break;
        case 14:
            def->anim_maya_id = g4(buffer);
            break;
        case 15:
            /* Framed list (u16 count + explicit frame) with v1's packed record. */
            handle_frame_sounds_framed(def, buffer, decode_frame_sound_v1);
            break;
        case 16:
            def->anim_maya_start = g2(buffer);
            def->anim_maya_end = g2(buffer);
            break;
        case 17:
        {
            def->anim_maya_masks = calloc(256, sizeof(bool));
            int var3 = g1(buffer);
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->anim_maya_masks[g1(buffer)] = true;
            }
            break;
        }
        case 18:
            def->debug_name = gcstring(buffer);
            break;
        default:
            printf("Unrecognized opcode %d\n", opcode);
            break;
        }
    }
}

static void
decode_sequence_v2(
    struct RSCache_Dat2ConfigSequence* def,
    struct RSCache_Buffer* buffer)
{
    // Initialize frame sounds map
    def->frame_sounds.frames = NULL;
    def->frame_sounds.sounds = NULL;
    def->frame_sounds.count = 0;
    def->frame_sounds.capacity = 0;

    while( true )
    {
        int opcode = g1(buffer);
        if( opcode == 0 )
        {
            def->_consumed = (int)buffer->position;
            break;
        }

        switch( opcode )
        {
        case 1:
        {
            int var3 = g2(buffer);
            def->frame_count = var3;
            def->frame_lengths = malloc(var3 * sizeof(int));
            def->frame_ids = malloc(var3 * sizeof(int));
            memset(def->frame_lengths, 0, var3 * sizeof(int));
            memset(def->frame_ids, 0, var3 * sizeof(int));

            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_lengths[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] += g2(buffer) << 16;
            }
            break;
        }
        case 2:
            def->frame_step = g2(buffer);
            break;
        case 3:
        {
            int var3 = g1(buffer);
            def->interleave_leave = malloc((var3 + 1) * sizeof(int));
            memset(def->interleave_leave, 0, (var3 + 1) * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->interleave_leave[var4] = g1(buffer);
            }
            def->interleave_leave[var3] = 9999999;
            break;
        }
        case 4:
            def->stretches = true;
            break;
        case 5:
            def->forced_priority = g1(buffer);
            break;
        case 6:
            def->left_hand_item = g2(buffer);
            break;
        case 7:
            def->right_hand_item = g2(buffer);
            break;
        case 8:
            def->max_loops = g1(buffer);
            break;
        case 9:
            def->precedence_animating = g1(buffer);
            break;
        case 10:
            def->priority = g1(buffer);
            break;
        case 11:
            def->reply_mode = g1(buffer);
            break;
        case 12:
        {
            int var3 = g1(buffer);
            def->chat_frame_id_count = var3;
            def->chat_frame_ids = malloc(var3 * sizeof(int));
            memset(def->chat_frame_ids, 0, var3 * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] += g2(buffer) << 16;
            }
            break;
        }
        case 13:
            handle_frame_sounds_220_226(def, buffer);
            break;
        case 14:
            def->anim_maya_id = g4(buffer);
            break;
        case 15:
            /* Framed list (u16 count + explicit frame) with v2's record. */
            handle_frame_sounds_framed(def, buffer, decode_frame_sound_v2);
            break;
        case 16:
            def->anim_maya_start = g2(buffer);
            def->anim_maya_end = g2(buffer);
            break;
        case 17:
        {
            def->anim_maya_masks = calloc(256, sizeof(bool));
            int var3 = g1(buffer);
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->anim_maya_masks[g1(buffer)] = true;
            }
            break;
        }
        case 18:
            def->debug_name = gcstring(buffer);
            break;
        default:
            printf("Unrecognized opcode %d\n", opcode);
            break;
        }
    }
}

static void
decode_sequence_v3(
    struct RSCache_Dat2ConfigSequence* def,
    struct RSCache_Buffer* buffer)
{
    // Initialize frame sounds map
    def->frame_sounds.frames = NULL;
    def->frame_sounds.sounds = NULL;
    def->frame_sounds.count = 0;
    def->frame_sounds.capacity = 0;

    while( true )
    {
        int opcode = g1(buffer);
        if( opcode == 0 )
        {
            def->_consumed = (int)buffer->position;
            break;
        }

        switch( opcode )
        {
        case 1:
        {
            int var3 = g2(buffer);
            def->frame_count = var3;
            def->frame_lengths = malloc(var3 * sizeof(int));
            def->frame_ids = malloc(var3 * sizeof(int));
            memset(def->frame_lengths, 0, var3 * sizeof(int));
            memset(def->frame_ids, 0, var3 * sizeof(int));

            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_lengths[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] += g2(buffer) << 16;
            }
            break;
        }
        case 2:
            def->frame_step = g2(buffer);
            break;
        case 3:
        {
            int var3 = g1(buffer);
            def->interleave_leave = malloc((var3 + 1) * sizeof(int));
            memset(def->interleave_leave, 0, (var3 + 1) * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->interleave_leave[var4] = g1(buffer);
            }
            def->interleave_leave[var3] = 9999999;
            break;
        }
        case 4:
            def->stretches = true;
            break;
        case 5:
            def->forced_priority = g1(buffer);
            break;
        case 6:
            def->left_hand_item = g2(buffer);
            break;
        case 7:
            def->right_hand_item = g2(buffer);
            break;
        case 8:
            def->max_loops = g1(buffer);
            break;
        case 9:
            def->precedence_animating = g1(buffer);
            break;
        case 10:
            def->priority = g1(buffer);
            break;
        case 11:
            def->reply_mode = g1(buffer);
            break;
        case 12:
        {
            int var3 = g1(buffer);
            def->chat_frame_id_count = var3;
            def->chat_frame_ids = malloc(var3 * sizeof(int));
            memset(def->chat_frame_ids, 0, var3 * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] = g2(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] += g2(buffer) << 16;
            }
            break;
        }
        case 13:
            def->anim_maya_id = g4(buffer);
            break;
        case 14:
            handle_frame_sounds_226_plus(def, buffer);
            break;
        case 15:
            def->anim_maya_start = g2(buffer);
            def->anim_maya_end = g2(buffer);
            break;
        case 16:
            /* rev226+: vertical offset (single signed byte). RuneLite/xrsps SeqType. */
            def->vertical_offset = (int8_t)g1(buffer);
            break;
        case 17:
        {
            def->anim_maya_masks = calloc(256, sizeof(bool));
            int var3 = g1(buffer);
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->anim_maya_masks[g1(buffer)] = true;
            }
            break;
        }
        case 18:
            def->debug_name = gcstring(buffer);
            break;
        case 100:
        {
            /* rev226+: per-frame blend table (frame, interval pairs). Parsed to keep
             * the stream aligned; not used by playback. */
            int var3 = g1(buffer);
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                (void)g2(buffer); /* frame */
                (void)g2(buffer); /* interval */
            }
            break;
        }
        default:
            printf("Unrecognized opcode %d\n", opcode);
            break;
        }
    }
}

/* Highest frame index carrying a sound, or -1. v1 and v2 store the frame index
 * implicitly as the position in the list, so the list has to be written dense up
 * to this bound with zero-filled holes. */
static int
frame_sound_highest_frame(const struct RSCache_Dat2ConfigFrameSoundMap* map)
{
    int highest = -1;
    for( int i = 0; i < map->count; i++ )
    {
        if( map->frames[i] > highest )
            highest = map->frames[i];
    }
    return highest;
}

static const struct RSCache_Dat2ConfigFrameSound*
frame_sound_for_frame(
    const struct RSCache_Dat2ConfigFrameSoundMap* map,
    int frame)
{
    for( int i = 0; i < map->count; i++ )
    {
        if( map->frames[i] == frame )
            return &map->sounds[i];
    }
    return NULL;
}

uint32_t
RSCache_Dat2ConfigSequenceEncodeCodec(
    const struct RSCache_Dat2ConfigSequence* def,
    int codec_version,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !def || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    bool is_v3 = codec_version == RSCACHE_CODEC_SEQUENCE_V3;

    /*
     * The opcode *numbers* move between eras, not just the record shapes:
     *
     *   opcode   v1                  v2                  v3
     *   13       frame sounds (g3)   frame sounds        anim_maya_id
     *   14       anim_maya_id        anim_maya_id        frame sounds (framed)
     *   15       frame sounds        frame sounds        maya start/end
     *   16       maya start/end      maya start/end      vertical offset
     *   100      -                   -                   blend table
     *
     * so the codec version has to be known to emit any of them.
     */
    int opcode_maya_id = is_v3 ? 13 : 14;
    int opcode_sounds = is_v3 ? 14 : 13;
    int opcode_maya_range = is_v3 ? 15 : 16;

    if( def->frame_count > 0 )
    {
        p1(&buffer, 1);
        p2(&buffer, def->frame_count);
        for( int i = 0; i < def->frame_count; i++ )
            p2(&buffer, def->frame_lengths[i]);
        /* Frame ids are split: low 16 bits for every frame, then the high 16. */
        for( int i = 0; i < def->frame_count; i++ )
            p2(&buffer, def->frame_ids[i] & 0xFFFF);
        for( int i = 0; i < def->frame_count; i++ )
            p2(&buffer, (def->frame_ids[i] >> 16) & 0xFFFF);
    }

    if( def->frame_step != 0 )
    {
        p1(&buffer, 2);
        p2(&buffer, def->frame_step);
    }

    if( def->interleave_leave )
    {
        /* The decoder appends a 9999999 sentinel, so the wire count is the number
         * of entries before it. */
        int count = 0;
        while( def->interleave_leave[count] != 9999999 )
            count++;

        p1(&buffer, 3);
        p1(&buffer, count);
        for( int i = 0; i < count; i++ )
            p1(&buffer, def->interleave_leave[i]);
    }

    if( def->stretches )
        p1(&buffer, 4);
    if( def->forced_priority != 0 )
    {
        p1(&buffer, 5);
        p1(&buffer, def->forced_priority);
    }
    if( def->left_hand_item != 0 )
    {
        p1(&buffer, 6);
        p2(&buffer, def->left_hand_item);
    }
    if( def->right_hand_item != 0 )
    {
        p1(&buffer, 7);
        p2(&buffer, def->right_hand_item);
    }
    if( def->max_loops != 0 )
    {
        p1(&buffer, 8);
        p1(&buffer, def->max_loops);
    }
    if( def->precedence_animating != 0 )
    {
        p1(&buffer, 9);
        p1(&buffer, def->precedence_animating);
    }
    if( def->priority != 0 )
    {
        p1(&buffer, 10);
        p1(&buffer, def->priority);
    }
    if( def->reply_mode != 0 )
    {
        p1(&buffer, 11);
        p1(&buffer, def->reply_mode);
    }

    if( def->chat_frame_ids && def->chat_frame_id_count > 0 )
    {
        p1(&buffer, 12);
        p1(&buffer, def->chat_frame_id_count);
        /* Split low/high halves, exactly as opcode 1 does for frame ids. */
        for( int i = 0; i < def->chat_frame_id_count; i++ )
            p2(&buffer, def->chat_frame_ids[i] & 0xFFFF);
        for( int i = 0; i < def->chat_frame_id_count; i++ )
            p2(&buffer, (def->chat_frame_ids[i] >> 16) & 0xFFFF);
    }

    if( def->anim_maya_id != 0 )
    {
        p1(&buffer, opcode_maya_id);
        p4(&buffer, def->anim_maya_id);
    }

    if( def->frame_sounds.count > 0 )
    {
        p1(&buffer, opcode_sounds);

        if( is_v3 )
        {
            /* v3 stores the frame explicitly, so only real entries are written. */
            p2(&buffer, def->frame_sounds.count);
            for( int i = 0; i < def->frame_sounds.count; i++ )
            {
                const struct RSCache_Dat2ConfigFrameSound* sound = &def->frame_sounds.sounds[i];
                p2(&buffer, def->frame_sounds.frames[i]);
                p2(&buffer, sound->id);
                p1(&buffer, sound->weight);
                p1(&buffer, sound->loops);
                p1(&buffer, sound->location);
                p1(&buffer, sound->retain);
            }
        }
        else
        {
            /* v1/v2 index by position, so the list is dense. Frames with no sound
             * are written as zeros, which the decoder's own filter (id >= 1) drops
             * again on the way back in. */
            int highest = frame_sound_highest_frame(&def->frame_sounds);
            int count = highest + 1;
            p1(&buffer, count);

            for( int frame = 0; frame < count; frame++ )
            {
                const struct RSCache_Dat2ConfigFrameSound* sound =
                    frame_sound_for_frame(&def->frame_sounds, frame);

                if( codec_version == RSCACHE_CODEC_SEQUENCE_V1 )
                {
                    /* Packed: id << 8 | loops << 4 | location. retain and weight
                     * have no home in this record. */
                    int bits = sound ? ((sound->id << 8) | ((sound->loops & 7) << 4) |
                                        (sound->location & 15))
                                     : 0;
                    p3(&buffer, bits);
                }
                else
                {
                    p2(&buffer, sound ? sound->id : 0);
                    p1(&buffer, sound ? sound->loops : 0);
                    p1(&buffer, sound ? sound->location : 0);
                    p1(&buffer, sound ? sound->retain : 0);
                }
            }
        }
    }

    if( def->anim_maya_start != 0 || def->anim_maya_end != 0 )
    {
        p1(&buffer, opcode_maya_range);
        p2(&buffer, def->anim_maya_start);
        p2(&buffer, def->anim_maya_end);
    }

    /* Opcode 16 is the vertical offset only in v3; in v1/v2 that number is the maya
     * range, already written above. */
    if( is_v3 && def->vertical_offset != 0 )
    {
        p1(&buffer, 16);
        p1b(&buffer, def->vertical_offset);
    }

    if( def->anim_maya_masks )
    {
        int count = 0;
        for( int i = 0; i < 256; i++ )
        {
            if( def->anim_maya_masks[i] )
                count++;
        }

        p1(&buffer, 17);
        p1(&buffer, count);
        for( int i = 0; i < 256; i++ )
        {
            if( def->anim_maya_masks[i] )
                p1(&buffer, i);
        }
    }

    if( def->debug_name )
    {
        p1(&buffer, 18);
        pjstr(&buffer, def->debug_name, RSCACHE_JSTR_TERMINATOR_NULL);
    }

    p1(&buffer, 0);
    return buffer.position;
}

uint32_t
RSCache_Dat2ConfigSequenceEncode(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigSequence* def,
    uint8_t* out,
    uint32_t out_capacity)
{
    return RSCache_Dat2ConfigSequenceEncodeCodec(
        def, RSCache_Dat2ConfigSequenceCodecVersion(cache), out, out_capacity);
}

static void
decode_sequence_codec(
    struct RSCache_Dat2ConfigSequence* def,
    int codec_version,
    struct RSCache_Buffer* buffer);

void
RSCache_Dat2ConfigSequenceFree(struct RSCache_Dat2ConfigSequence* def)
{
    if( !def )
        return;

    RSCache_Dat2ConfigSequenceFreeInplace(def);

    free(def);
}

void
RSCache_Dat2ConfigSequenceFreeInplace(struct RSCache_Dat2ConfigSequence* def)
{
    if( !def )
        return;

    if( def->frame_ids )
    {
        free(def->frame_ids);
    }
    if( def->frame_lengths )
    {
        free(def->frame_lengths);
    }
    if( def->interleave_leave )
    {
        free(def->interleave_leave);
    }
    if( def->chat_frame_ids )
    {
        free(def->chat_frame_ids);
    }
    if( def->anim_maya_masks )
    {
        free(def->anim_maya_masks);
    }
    if( def->debug_name )
    {
        free(def->debug_name);
    }
    if( def->frame_sounds.frames )
    {
        free(def->frame_sounds.frames);
    }
    if( def->frame_sounds.sounds )
    {
        free(def->frame_sounds.sounds);
    }
}

static void
decode_sequence_codec(
    struct RSCache_Dat2ConfigSequence* def,
    int codec_version,
    struct RSCache_Buffer* buffer)
{
    switch( codec_version )
    {
    case RSCACHE_CODEC_SEQUENCE_V1:
        decode_sequence_v1(def, buffer);
        break;
    case RSCACHE_CODEC_SEQUENCE_V2:
        decode_sequence_v2(def, buffer);
        break;
    default:
        decode_sequence_v3(def, buffer);
        break;
    }
}

int
RSCache_Dat2ConfigSequenceCodecVersion(const struct RSCache* cache)
{
    assert(cache);
    /* Newest layout when nothing identifies the cache: every dat2 cache the
     * client ships decodes as v3, and a modern archive revision (a unix
     * timestamp) sits far above both thresholds anyway. */
    int derived = RSCACHE_CODEC_SEQUENCE_V3;

    if( !RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_SEQUENCE, 220, RSCACHE_SEQUENCE_ARCHIVE_REV_220 + 1, true) )
        derived = RSCACHE_CODEC_SEQUENCE_V1;
    else if( !RSCache_RevisionAtLeastOsrs(
                 cache, RSCACHE_TYPE_SEQUENCE, 226, RSCACHE_SEQUENCE_ARCHIVE_REV_226 + 1, true) )
        derived = RSCACHE_CODEC_SEQUENCE_V2;

    return RSCache_CodecVersionOr(cache, RSCACHE_TYPE_SEQUENCE, derived);
}

void
RSCache_Dat2ConfigSequenceDecodeProfile(
    struct RSCache_Dat2ConfigSequence* sequence,
    const struct RSCache* cache,
    char* data,
    int buffer_size)
{
    assert(cache);
    struct RSCache_Buffer buffer = {
        .data = (uint8_t*)(data), .size = (uint32_t)(buffer_size), .position = 0
    };
    decode_sequence_codec(sequence, RSCache_Dat2ConfigSequenceCodecVersion(cache), &buffer);
}

struct RSCache_Dat2ConfigSequence*
RSCache_Dat2ConfigSequenceNewDecodeProfile(
    const struct RSCache* cache,
    char* data,
    int data_size)
{
    assert(cache);
    struct RSCache_Dat2ConfigSequence* def = malloc(sizeof(struct RSCache_Dat2ConfigSequence));
    memset(def, 0, sizeof(struct RSCache_Dat2ConfigSequence));
    RSCache_Dat2ConfigSequenceDecodeProfile(def, cache, data, data_size);
    return def;
}

static void
print_sequence(struct RSCache_Dat2ConfigSequence* def)
{
    printf("Sequence %d:\n", def->id);
    printf("Frame count: %d\n", def->frame_count);
    printf("Frame step: %d\n", def->frame_step);
    printf("Stretches: %s\n", def->stretches ? "true" : "false");
    printf("Forced priority: %d\n", def->forced_priority);
    printf("Left hand item: %d\n", def->left_hand_item);
    printf("Right hand item: %d\n", def->right_hand_item);
    printf("Max loops: %d\n", def->max_loops);
    printf("Precedence animating: %d\n", def->precedence_animating);
    printf("Priority: %d\n", def->priority);
    printf("Reply mode: %d\n", def->reply_mode);
    printf("Anim maya ID: %d\n", def->anim_maya_id);
    printf("Anim maya start: %d\n", def->anim_maya_start);
    printf("Anim maya end: %d\n", def->anim_maya_end);

    if( def->interleave_leave )
    {
        printf("Interleave leave: ");
        for( int i = 0; def->interleave_leave[i] != 9999999; i++ )
        {
            printf("%d ", def->interleave_leave[i]);
        }
        printf("\n");
    }

    if( def->chat_frame_ids )
    {
        printf("Chat frame IDs: ");
        for( int i = 0; i < def->frame_count; i++ )
        {
            printf("%d ", def->chat_frame_ids[i]);
        }
        printf("\n");
    }

    if( def->anim_maya_masks )
    {
        printf("Anim maya masks: ");
        for( int i = 0; i < 256; i++ )
        {
            if( def->anim_maya_masks[i] )
            {
                printf("%d ", i);
            }
        }
        printf("\n");
    }

    if( def->debug_name )
    {
        printf("Debug name: %s\n", def->debug_name);
    }

    printf("\nFrames:\n");
    for( int i = 0; i < def->frame_count; i++ )
    {
        printf("Frame %d: ID=%d, Length=%d\n", i, def->frame_ids[i], def->frame_lengths[i]);
    }
}

static void
free_sequence(struct RSCache_Dat2ConfigSequence* def)
{
    if( def->frame_ids )
    {
        free(def->frame_ids);
    }
    if( def->frame_lengths )
    {
        free(def->frame_lengths);
    }
    if( def->interleave_leave )
    {
        free(def->interleave_leave);
    }
    if( def->chat_frame_ids )
    {
        free(def->chat_frame_ids);
    }
    if( def->anim_maya_masks )
    {
        free(def->anim_maya_masks);
    }
    if( def->debug_name )
    {
        free(def->debug_name);
    }
    if( def->frame_sounds.frames )
    {
        free(def->frame_sounds.frames);
    }
    if( def->frame_sounds.sounds )
    {
        free(def->frame_sounds.sounds);
    }
}

// if (code === 1) {
//     this.frameCount = dat.g1();
//     this.frames = new Int16Array(this.frameCount);
//     this.iframes = new Int16Array(this.frameCount);
//     this.delay = new Int16Array(this.frameCount);

//     for (let i: number = 0; i < this.frameCount; i++) {
//         this.frames[i] = dat.g2();

//         this.iframes[i] = dat.g2();
//         if (this.iframes[i] === 65535) {
//             this.iframes[i] = -1;
//         }

//         this.delay[i] = dat.g2();
//     }
// } else if (code === 2) {
//     this.loops = dat.g2();
// } else if (code === 3) {
//     const count: number = dat.g1();
//     this.walkmerge = new Int32Array(count + 1);

//     for (let i: number = 0; i < count; i++) {
//         this.walkmerge[i] = dat.g1();
//     }

//     this.walkmerge[count] = 9999999;
// } else if (code === 4) {
//     this.stretches = true;
// } else if (code === 5) {
//     this.priority = dat.g1();
// } else if (code === 6) {
//     this.replaceheldleft = dat.g2();
// } else if (code === 7) {
//     this.replaceheldright = dat.g2();
// } else if (code === 8) {
//     this.maxloops = dat.g1();
// } else if (code === 9) {
//     this.preanim_move = dat.g1();
// } else if (code === 10) {
//     this.postanim_move = dat.g1();
// } else if (code === 11) {
//     this.duplicatebehavior = dat.g1();
// } else {
//     console.log('Error unrecognised seq config code: ', code);
// }

struct RSCache_Dat1ConfigSequence*
RSCache_Dat1ConfigSequenceNewDecode(
    char* buffer,
    int buffer_size)
{
    struct RSCache_Dat1ConfigSequence* def = malloc(sizeof(struct RSCache_Dat1ConfigSequence));
    memset(def, 0, sizeof(struct RSCache_Dat1ConfigSequence));

    RSCache_Dat1ConfigSequenceDecodeInplace(def, buffer, buffer_size);

    return def;
}

int
RSCache_Dat1ConfigSequenceDecodeInplace(
    struct RSCache_Dat1ConfigSequence* def,
    char* data,
    int data_size)
{
    struct RSCache_Buffer buffer = { .data = (uint8_t*)(data), .size = (uint32_t)(data_size), .position = 0 };

    while( true )
    {
        int opcode = g1(&buffer);
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
            def->frame_count = g1(&buffer);

            def->frames = malloc(def->frame_count * sizeof(int));
            def->iframes = malloc(def->frame_count * sizeof(int));
            def->delay = malloc(def->frame_count * sizeof(int));
            memset(def->frames, 0, def->frame_count * sizeof(int));
            memset(def->iframes, 0, def->frame_count * sizeof(int));
            memset(def->delay, 0, def->frame_count * sizeof(int));

            for( int i = 0; i < def->frame_count; i++ )
            {
                int frame = g2(&buffer);
                def->frames[i] = frame;
                int iframe = g2(&buffer);
                if( iframe == 65535 )
                {
                    iframe = -1;
                }
                def->iframes[i] = iframe;

                int delay = g2(&buffer);
                def->delay[i] = delay;
            }
            break;
        case 2:
            def->loops = g2(&buffer);
            break;
        case 3:
        {
            int count = g1(&buffer);
            def->walkmerge = malloc((count + 1) * sizeof(int));
            memset(def->walkmerge, 0, (count + 1) * sizeof(int));
            for( int i = 0; i < count; i++ )
            {
                def->walkmerge[i] = g1(&buffer);
            }
            def->walkmerge[count] = 9999999;
        }
        break;
        case 4:
            def->stretches = true;
            break;
        case 5:
            def->priority = g1(&buffer);
            break;
        case 6:
            def->replaceheldleft = g2(&buffer);
            break;
        case 7:
            def->replaceheldright = g2(&buffer);
            break;
        case 8:
            def->maxloops = g1(&buffer);
            break;
        case 9:
            def->preanim_move = g1(&buffer);
            break;
        case 10:
            def->postanim_move = g1(&buffer);
            break;
        case 11:
            def->duplicate_behavior = g1(&buffer);
            break;
        default:
            printf("Unrecognized opcode %d\n", opcode);
            break;
        }
    }

    return buffer.position;
}

void
RSCache_Dat1ConfigSequenceFree(struct RSCache_Dat1ConfigSequence* seq)
{
    if( !seq )
        return;
    free(seq->frames);
    free(seq->iframes);
    free(seq->delay);
    free(seq->walkmerge);
    free(seq);
}
