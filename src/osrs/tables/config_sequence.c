#include "config_sequence.h"

#include "configs.h"
#include "osrs/cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define REV_220_SEQ_ARCHIVE_REV 1141
#define REV_226_SEQ_ARCHIVE_REV 1268

static void
add_frame_sound(struct CacheConfigFrameSoundMap* map, int frame, struct CacheConfigFrameSound sound)
{
    if( map->count >= map->capacity )
    {
        int new_capacity = map->capacity == 0 ? 4 : map->capacity * 2;
        map->frames = realloc(map->frames, new_capacity * sizeof(int));
        map->sounds = realloc(map->sounds, new_capacity * sizeof(struct CacheConfigFrameSound));
        map->capacity = new_capacity;
    }
    map->frames[map->count] = frame;
    map->sounds[map->count] = sound;
    map->count++;
}

static void
decode_pre_220_frame_sound(struct CacheConfigFrameSound* sound, struct Buffer* buffer)
{
    // Old format: 24-bit int with packed fields
    int bits = read_24(buffer);
    sound->location = bits & 15;
    sound->id = bits >> 8;
    sound->loops = (bits >> 4) & 7;
    sound->retain = 0;
    sound->weight = -1;
}

static void
decode_220_226_frame_sound(struct CacheConfigFrameSound* sound, struct Buffer* buffer)
{
    // New format: separate fields
    sound->id = read_16(buffer);
    sound->loops = read_8(buffer);
    sound->location = read_8(buffer);
    sound->retain = read_8(buffer);
    sound->weight = -1;
}

static void
decode_226_plus_frame_sound(struct CacheConfigFrameSound* sound, struct Buffer* buffer)
{
    // New format: frame sounds with weights
    sound->id = read_16(buffer);
    sound->weight = read_8(buffer);
    sound->loops = read_8(buffer);
    sound->location = read_8(buffer);
    sound->retain = read_8(buffer);
}

static void
handle_frame_sounds_pre_220(struct CacheConfigSequence* def, struct Buffer* buffer)
{
    int var3 = read_8(buffer);
    for( int var4 = 0; var4 < var3; ++var4 )
    {
        struct CacheConfigFrameSound sound = { 0 };
        decode_pre_220_frame_sound(&sound, buffer);
        if( sound.id >= 1 && sound.loops >= 1 && sound.location >= 0 && sound.retain >= 0 )
        {
            add_frame_sound(&def->frame_sounds, var4, sound);
        }
    }
}

static void
handle_frame_sounds_220_226(struct CacheConfigSequence* def, struct Buffer* buffer)
{
    int var3 = read_8(buffer);
    for( int var4 = 0; var4 < var3; ++var4 )
    {
        struct CacheConfigFrameSound sound = { 0 };
        decode_220_226_frame_sound(&sound, buffer);
        if( sound.id >= 1 && sound.loops >= 1 && sound.location >= 0 && sound.retain >= 0 )
        {
            add_frame_sound(&def->frame_sounds, var4, sound);
        }
    }
}

static void
handle_frame_sounds_226_plus(struct CacheConfigSequence* def, struct Buffer* buffer)
{
    int var3 = read_16(buffer);
    for( int var4 = 0; var4 < var3; ++var4 )
    {
        int frame = read_16(buffer);
        struct CacheConfigFrameSound sound = { 0 };
        decode_226_plus_frame_sound(&sound, buffer);
        if( sound.id >= 1 && sound.loops >= 1 && sound.location >= 0 && sound.retain >= 0 )
        {
            add_frame_sound(&def->frame_sounds, frame, sound);
        }
    }
}

static void
decode_sequence_pre_220(struct CacheConfigSequence* def, struct Buffer* buffer)
{
    // Initialize frame sounds map
    def->frame_sounds.frames = NULL;
    def->frame_sounds.sounds = NULL;
    def->frame_sounds.count = 0;
    def->frame_sounds.capacity = 0;

    while( true )
    {
        int opcode = read_8(buffer);
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
        {
            int var3 = read_16(buffer);
            def->frame_count = var3;
            def->frame_lengths = malloc(var3 * sizeof(int));
            def->frame_ids = malloc(var3 * sizeof(int));
            memset(def->frame_lengths, 0, var3 * sizeof(int));
            memset(def->frame_ids, 0, var3 * sizeof(int));

            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_lengths[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] += read_16(buffer) << 16;
            }
            break;
        }
        case 2:
            def->frame_step = read_16(buffer);
            break;
        case 3:
        {
            int var3 = read_8(buffer);
            def->interleave_leave = malloc((var3 + 1) * sizeof(int));
            memset(def->interleave_leave, 0, (var3 + 1) * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->interleave_leave[var4] = read_8(buffer);
            }
            def->interleave_leave[var3] = 9999999;
            break;
        }
        case 4:
            def->stretches = true;
            break;
        case 5:
            def->forced_priority = read_8(buffer);
            break;
        case 6:
            def->left_hand_item = read_16(buffer);
            break;
        case 7:
            def->right_hand_item = read_16(buffer);
            break;
        case 8:
            def->max_loops = read_8(buffer);
            break;
        case 9:
            def->precedence_animating = read_8(buffer);
            break;
        case 10:
            def->priority = read_8(buffer);
            break;
        case 11:
            def->reply_mode = read_8(buffer);
            break;
        case 12:
        {
            int var3 = read_8(buffer);
            def->chat_frame_ids = malloc(var3 * sizeof(int));
            memset(def->chat_frame_ids, 0, var3 * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] += read_16(buffer) << 16;
            }
            break;
        }
        case 13:
            handle_frame_sounds_pre_220(def, buffer);
            break;
        case 14:
            def->anim_maya_id = read_32(buffer);
            break;
        case 15:
            handle_frame_sounds_220_226(def, buffer);
            break;
        case 16:
            def->anim_maya_start = read_16(buffer);
            def->anim_maya_end = read_16(buffer);
            break;
        case 17:
        {
            def->anim_maya_masks = calloc(256, sizeof(bool));
            int var3 = read_8(buffer);
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->anim_maya_masks[read_8(buffer)] = true;
            }
            break;
        }
        case 18:
            def->debug_name = read_string(buffer);
            break;
        default:
            printf("Unrecognized opcode %d\n", opcode);
            break;
        }
    }
}

static void
decode_sequence_220_226(struct CacheConfigSequence* def, struct Buffer* buffer)
{
    // Initialize frame sounds map
    def->frame_sounds.frames = NULL;
    def->frame_sounds.sounds = NULL;
    def->frame_sounds.count = 0;
    def->frame_sounds.capacity = 0;

    while( true )
    {
        int opcode = read_8(buffer);
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
        {
            int var3 = read_16(buffer);
            def->frame_count = var3;
            def->frame_lengths = malloc(var3 * sizeof(int));
            def->frame_ids = malloc(var3 * sizeof(int));
            memset(def->frame_lengths, 0, var3 * sizeof(int));
            memset(def->frame_ids, 0, var3 * sizeof(int));

            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_lengths[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] += read_16(buffer) << 16;
            }
            break;
        }
        case 2:
            def->frame_step = read_16(buffer);
            break;
        case 3:
        {
            int var3 = read_8(buffer);
            def->interleave_leave = malloc((var3 + 1) * sizeof(int));
            memset(def->interleave_leave, 0, (var3 + 1) * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->interleave_leave[var4] = read_8(buffer);
            }
            def->interleave_leave[var3] = 9999999;
            break;
        }
        case 4:
            def->stretches = true;
            break;
        case 5:
            def->forced_priority = read_8(buffer);
            break;
        case 6:
            def->left_hand_item = read_16(buffer);
            break;
        case 7:
            def->right_hand_item = read_16(buffer);
            break;
        case 8:
            def->max_loops = read_8(buffer);
            break;
        case 9:
            def->precedence_animating = read_8(buffer);
            break;
        case 10:
            def->priority = read_8(buffer);
            break;
        case 11:
            def->reply_mode = read_8(buffer);
            break;
        case 12:
        {
            int var3 = read_8(buffer);
            def->chat_frame_ids = malloc(var3 * sizeof(int));
            memset(def->chat_frame_ids, 0, var3 * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] += read_16(buffer) << 16;
            }
            break;
        }
        case 13:
            handle_frame_sounds_220_226(def, buffer);
            break;
        case 14:
            def->anim_maya_id = read_32(buffer);
            break;
        case 15:
            handle_frame_sounds_220_226(def, buffer);
            break;
        case 16:
            def->anim_maya_start = read_16(buffer);
            def->anim_maya_end = read_16(buffer);
            break;
        case 17:
        {
            def->anim_maya_masks = calloc(256, sizeof(bool));
            int var3 = read_8(buffer);
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->anim_maya_masks[read_8(buffer)] = true;
            }
            break;
        }
        case 18:
            def->debug_name = read_string(buffer);
            break;
        default:
            printf("Unrecognized opcode %d\n", opcode);
            break;
        }
    }
}

static void
decode_sequence_226_plus(struct CacheConfigSequence* def, struct Buffer* buffer)
{
    // Initialize frame sounds map
    def->frame_sounds.frames = NULL;
    def->frame_sounds.sounds = NULL;
    def->frame_sounds.count = 0;
    def->frame_sounds.capacity = 0;

    while( true )
    {
        int opcode = read_8(buffer);
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
        {
            int var3 = read_16(buffer);
            def->frame_count = var3;
            def->frame_lengths = malloc(var3 * sizeof(int));
            def->frame_ids = malloc(var3 * sizeof(int));
            memset(def->frame_lengths, 0, var3 * sizeof(int));
            memset(def->frame_ids, 0, var3 * sizeof(int));

            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_lengths[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->frame_ids[var4] += read_16(buffer) << 16;
            }
            break;
        }
        case 2:
            def->frame_step = read_16(buffer);
            break;
        case 3:
        {
            int var3 = read_8(buffer);
            def->interleave_leave = malloc((var3 + 1) * sizeof(int));
            memset(def->interleave_leave, 0, (var3 + 1) * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->interleave_leave[var4] = read_8(buffer);
            }
            def->interleave_leave[var3] = 9999999;
            break;
        }
        case 4:
            def->stretches = true;
            break;
        case 5:
            def->forced_priority = read_8(buffer);
            break;
        case 6:
            def->left_hand_item = read_16(buffer);
            break;
        case 7:
            def->right_hand_item = read_16(buffer);
            break;
        case 8:
            def->max_loops = read_8(buffer);
            break;
        case 9:
            def->precedence_animating = read_8(buffer);
            break;
        case 10:
            def->priority = read_8(buffer);
            break;
        case 11:
            def->reply_mode = read_8(buffer);
            break;
        case 12:
        {
            int var3 = read_8(buffer);
            def->chat_frame_ids = malloc(var3 * sizeof(int));
            memset(def->chat_frame_ids, 0, var3 * sizeof(int));
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] = read_16(buffer);
            }
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->chat_frame_ids[var4] += read_16(buffer) << 16;
            }
            break;
        }
        case 13:
            def->anim_maya_id = read_32(buffer);
            break;
        case 14:
            handle_frame_sounds_226_plus(def, buffer);
            break;
        case 15:
            def->anim_maya_start = read_16(buffer);
            def->anim_maya_end = read_16(buffer);
            break;
        case 17:
        {
            def->anim_maya_masks = calloc(256, sizeof(bool));
            int var3 = read_8(buffer);
            for( int var4 = 0; var4 < var3; ++var4 )
            {
                def->anim_maya_masks[read_8(buffer)] = true;
            }
            break;
        }
        case 18:
            def->debug_name = read_string(buffer);
            break;
        default:
            printf("Unrecognized opcode %d\n", opcode);
            break;
        }
    }
}

struct CacheConfigSequence*
config_sequence_new_decode(int revision, char* data, int data_size)
{
    struct CacheConfigSequence* def = malloc(sizeof(struct CacheConfigSequence));
    memset(def, 0, sizeof(struct CacheConfigSequence));

    struct Buffer buffer = { .data = data, .data_size = data_size, .position = 0 };

    decode_sequence(def, revision, &buffer);

    return def;
}

void
config_sequence_free(struct CacheConfigSequence* def)
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

    free(def);
}

void
config_sequence_decode_inplace(
    struct CacheConfigSequence* sequence, int revision, char* data, int buffer_size)
{
    struct Buffer buffer = { .data = data, .data_size = buffer_size, .position = 0 };
    decode_sequence(sequence, revision, &buffer);
}

void
decode_sequence(struct CacheConfigSequence* def, int revision, struct Buffer* buffer)
{
    if( revision <= REV_220_SEQ_ARCHIVE_REV )
    {
        decode_sequence_pre_220(def, buffer);
    }
    else if( revision <= REV_226_SEQ_ARCHIVE_REV )
    {
        decode_sequence_220_226(def, buffer);
    }
    else
    {
        decode_sequence_226_plus(def, buffer);
    }
}

void
print_sequence(struct CacheConfigSequence* def)
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

void
free_sequence(struct CacheConfigSequence* def)
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

struct CacheConfigSequenceTable*
config_sequence_table_new(struct Cache* cache)
{
    struct CacheConfigSequenceTable* table = malloc(sizeof(struct CacheConfigSequenceTable));
    if( !table )
    {
        printf("config_sequence_table_new: Failed to allocate table\n");
        return NULL;
    }
    memset(table, 0, sizeof(struct CacheConfigSequenceTable));

    table->archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_SEQUENCE);
    if( !table->archive )
    {
        printf("config_sequence_table_new: Failed to load archive\n");
        goto error;
    }

    table->file_list = filelist_new_from_cache_archive(table->archive);
    if( !table->file_list )
    {
        printf("config_sequence_table_new: Failed to load file list\n");
        goto error;
    }

    return table;
error:
    free(table);
    return NULL;
}

void
config_sequence_table_free(struct CacheConfigSequenceTable* table)
{
    filelist_free(table->file_list);
    cache_archive_free(table->archive);
    free(table);
}

struct CacheConfigSequence*
config_sequence_table_get_new(struct CacheConfigSequenceTable* table, int id)
{
    if( id < 0 || id > table->file_list->file_count )
    {
        printf("config_sequence_table_get_new: Invalid id %d\n", id);
        return NULL;
    }

    if( table->value )
    {
        // config_sequence_free(table->value);
        table->value = NULL;
    }

    table->value = malloc(sizeof(struct CacheConfigSequence));
    memset(table->value, 0, sizeof(struct CacheConfigSequence));

    config_sequence_decode_inplace(
        table->value,
        table->archive->revision,
        table->file_list->files[id],
        table->file_list->file_sizes[id]);

    table->value->id = id;

    return table->value;
}