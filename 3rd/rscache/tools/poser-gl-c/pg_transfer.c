#include "cache_write.h"
#include "pg_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Import, export and pack.
 *
 * The `.pgl` codec here is byte compatible with poser-gl's: same revision byte,
 * same field order, same signed 16-bit deltas. That is the point of porting it
 * at all — a file written by either editor opens in the other, so this is not a
 * fork of the format.
 */

/* ---- a growable byte writer ------------------------------------------------- */

struct Writer
{
    uint8_t* data;
    int size;
    int capacity;
    bool failed;
};

static void
w_reserve(struct Writer* out, int extra)
{
    if( out->size + extra <= out->capacity )
        return;
    {
        int next = out->capacity ? out->capacity * 2 : 4096;
        uint8_t* grown;
        while( next < out->size + extra )
            next *= 2;
        grown = realloc(out->data, (size_t)next);
        if( !grown )
        {
            out->failed = true;
            return;
        }
        out->data = grown;
        out->capacity = next;
    }
}

static void
w1(struct Writer* out, int value)
{
    w_reserve(out, 1);
    if( out->failed )
        return;
    out->data[out->size++] = (uint8_t)(value & 0xFF);
}

static void
w2(struct Writer* out, int value)
{
    w1(out, value >> 8);
    w1(out, value);
}

static void
w4(struct Writer* out, int value)
{
    w2(out, value >> 16);
    w2(out, value);
}

/* ---- a reader ----------------------------------------------------------------- */

struct Reader
{
    const uint8_t* data;
    int size;
    int position;
    bool failed;
};

static int
r1(struct Reader* in)
{
    if( in->position + 1 > in->size )
    {
        in->failed = true;
        return 0;
    }
    return in->data[in->position++];
}

static int
r2u(struct Reader* in)
{
    int high = r1(in);
    int low = r1(in);
    return (high << 8) | low;
}

/** Signed, because deltas are written with Java's writeShort and go negative. */
static int
r2s(struct Reader* in)
{
    int value = r2u(in);
    return value > 32767 ? value - 65536 : value;
}

static int
r4(struct Reader* in)
{
    int high = r2u(in);
    int low = r2u(in);
    return (high << 16) | low;
}

static uint8_t*
read_file(const char* path, int* out_size)
{
    FILE* file = fopen(path, "rb");
    uint8_t* data;
    long size;

    *out_size = 0;
    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)size);
    if( !data || fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (int)size;
    return data;
}

static bool
write_file(const char* path, const uint8_t* data, int size)
{
    FILE* file = fopen(path, "wb");
    bool ok;
    if( !file )
        return false;
    ok = fwrite(data, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    return ok;
}

/* ---- .pgl export ---------------------------------------------------------------- */

static void
encode_framemap(struct Writer* out, const struct PG_FrameMapDef* framemap)
{
    w1(out, framemap->length);
    for( int i = 0; i < framemap->length; i++ )
        w1(out, framemap->types[i]);
    for( int i = 0; i < framemap->length; i++ )
        w1(out, framemap->map_lengths[i]);
    for( int i = 0; i < framemap->length; i++ )
        for( int j = 0; j < framemap->map_lengths[i]; j++ )
            w1(out, framemap->maps[i][j]);
}

static void
encode_keyframe(struct Writer* out, const struct PG_Keyframe* keyframe)
{
    int reference_count = 0;

    w2(out, keyframe->framemap ? keyframe->framemap->id : -1);
    for( int i = 0; i < keyframe->transformation_count; i++ )
        if( keyframe->transformations[i].is_reference )
            reference_count++;
    w1(out, reference_count);

    for( int i = 0; i < keyframe->transformation_count; i++ )
    {
        const struct PG_Transformation* reference = &keyframe->transformations[i];
        if( !reference->is_reference )
            continue;
        w2(out, reference->id);
        for( int k = 0; k < 3; k++ )
            w2(out, reference->delta[k]);
        w1(out, reference->child_count);
        for( int c = 0; c < reference->child_count; c++ )
        {
            const struct PG_Transformation* child =
                &keyframe->transformations[reference->child_order[c]];
            w2(out, child->id);
            w1(out, child->type);
            for( int k = 0; k < 3; k++ )
                w2(out, child->delta[k]);
        }
    }
}

bool
pg_export_pgl(struct PG_App* app, const char* path)
{
    struct PG_Animation* anim = pg_app_current_animation(app);
    struct Writer out = { 0 };
    bool ok;

    if( !anim )
    {
        pg_app_message(app, "Invalid Operation", "No animation selected");
        return false;
    }

    w1(&out, 0); /* revision byte, for backwards compatibility */
    w2(&out, anim->keyframe_count);
    for( int i = 0; i < anim->keyframe_count; i++ )
    {
        const struct PG_Keyframe* keyframe = &anim->keyframes[i];
        w2(&out, keyframe->length);
        if( !keyframe->framemap )
        {
            free(out.data);
            pg_app_message(app, "Export Failed", "Keyframe has no framemap");
            return false;
        }
        encode_framemap(&out, keyframe->framemap);
        encode_keyframe(&out, keyframe);
    }
    w4(&out, anim->left_hand_item);
    w4(&out, anim->right_hand_item);

    ok = !out.failed && write_file(path, out.data, out.size);
    free(out.data);
    if( ok )
    {
        snprintf(app->last_export, sizeof(app->last_export), "%s", path);
        app->last_export_format = 0;
        pg_app_status(app, "Exported %d bytes to %s", out.size, path);
    }
    else
    {
        pg_app_message(app, "Export Failed", "Could not write the file");
    }
    return ok;
}

/* ---- .pgl import ------------------------------------------------------------------ */

static void
own_framemap(struct PG_App* app, struct PG_FrameMapDef* framemap)
{
    if( app->owned_framemap_count == app->owned_framemap_capacity )
    {
        int next = app->owned_framemap_capacity ? app->owned_framemap_capacity * 2 : 16;
        struct PG_FrameMapDef** grown =
            realloc(app->owned_framemaps, (size_t)next * sizeof(*grown));
        if( !grown )
            return;
        app->owned_framemaps = grown;
        app->owned_framemap_capacity = next;
    }
    app->owned_framemaps[app->owned_framemap_count++] = framemap;
}

static struct PG_FrameMapDef*
decode_framemap(struct Reader* in)
{
    struct PG_FrameMapDef* framemap = calloc(1, sizeof(*framemap));
    if( !framemap )
        return NULL;
    framemap->owned = true;
    framemap->id = -1;
    framemap->length = r1(in);
    if( framemap->length <= 0 || in->failed )
    {
        free(framemap);
        return NULL;
    }
    framemap->types = calloc((size_t)framemap->length, sizeof(int));
    framemap->map_lengths = calloc((size_t)framemap->length, sizeof(int));
    framemap->maps = calloc((size_t)framemap->length, sizeof(int*));
    if( !framemap->types || !framemap->map_lengths || !framemap->maps )
    {
        free(framemap->types);
        free(framemap->map_lengths);
        free(framemap->maps);
        free(framemap);
        return NULL;
    }
    for( int i = 0; i < framemap->length; i++ )
        framemap->types[i] = r1(in);
    for( int i = 0; i < framemap->length; i++ )
    {
        framemap->map_lengths[i] = r1(in);
        framemap->maps[i] =
            framemap->map_lengths[i] > 0
                ? calloc((size_t)framemap->map_lengths[i], sizeof(int))
                : NULL;
    }
    for( int i = 0; i < framemap->length; i++ )
        for( int j = 0; j < framemap->map_lengths[i]; j++ )
            if( framemap->maps[i] )
                framemap->maps[i][j] = r1(in);
    return framemap;
}

bool
pg_import_pgl(struct PG_App* app, const char* path)
{
    struct Reader in = { 0 };
    uint8_t* data;
    int size;
    struct PG_Animation* anim;
    int keyframe_count;
    int max_id;

    data = read_file(path, &size);
    if( !data )
    {
        pg_app_message(app, "Import Failed", "Could not read the file");
        return false;
    }
    in.data = data;
    in.size = size;

    max_id = -1;
    for( int i = 0; i < app->animation_count; i++ )
        if( app->animations[i]->id > max_id )
            max_id = app->animations[i]->id;

    anim = pg_animation_new_empty(max_id + 1);
    if( !anim )
    {
        free(data);
        return false;
    }

    r1(&in); /* revision byte */
    keyframe_count = r2u(&in);

    for( int i = 0; i < keyframe_count && !in.failed; i++ )
    {
        int length = r2u(&in);
        struct PG_FrameMapDef* framemap = decode_framemap(&in);
        struct PG_Keyframe keyframe;
        int reference_count;

        if( !framemap )
        {
            in.failed = true;
            break;
        }
        own_framemap(app, framemap);

        pg_keyframe_init(&keyframe);
        keyframe.id = i;
        keyframe.frame_id = -1;
        keyframe.length = length;
        keyframe.framemap = framemap;
        keyframe.modified = true;

        framemap->id = r2s(&in);
        reference_count = r1(&in);

        for( int r = 0; r < reference_count && !in.failed; r++ )
        {
            int reference_id = r2s(&in);
            int delta[3];
            int child_count;
            struct PG_Transformation reference;
            int reference_index;

            for( int k = 0; k < 3; k++ )
                delta[k] = r2s(&in);
            child_count = r1(&in);

            memset(&reference, 0, sizeof(reference));
            reference.id = reference_id;
            reference.type = PG_TF_REFERENCE;
            reference.is_reference = true;
            reference.parent = -1;
            for( int t = 0; t < 4; t++ )
            {
                reference.child_by_type[t] = -1;
                reference.child_order[t] = -1;
            }
            if( reference_id >= 0 && reference_id < framemap->length )
            {
                reference.labels = framemap->maps[reference_id];
                reference.label_count = framemap->map_lengths[reference_id];
            }
            memcpy(reference.delta, delta, sizeof(delta));

            /* Grown by hand rather than through the keyframe helper, which is
             * private to the animation module; the layout is the same. */
            {
                struct PG_Transformation* grown = realloc(
                    keyframe.transformations,
                    (size_t)(keyframe.transformation_count + 1) * sizeof(*grown));
                if( !grown )
                {
                    in.failed = true;
                    break;
                }
                keyframe.transformations = grown;
                keyframe.transformation_capacity = keyframe.transformation_count + 1;
                keyframe.transformations[keyframe.transformation_count] = reference;
                reference_index = keyframe.transformation_count++;
            }

            for( int c = 0; c < child_count && !in.failed; c++ )
            {
                int child_id = r2s(&in);
                int child_type = r1(&in);
                struct PG_Transformation child;
                struct PG_Transformation* grown;

                memset(&child, 0, sizeof(child));
                child.id = child_id;
                child.type = child_type;
                child.parent = -1;
                for( int t = 0; t < 4; t++ )
                {
                    child.child_by_type[t] = -1;
                    child.child_order[t] = -1;
                }
                for( int k = 0; k < 3; k++ )
                    child.delta[k] = r2s(&in);
                if( child_id >= 0 && child_id < framemap->length )
                {
                    child.labels = framemap->maps[child_id];
                    child.label_count = framemap->map_lengths[child_id];
                }
                if( child_type < PG_TF_TRANSLATION || child_type > PG_TF_SCALE )
                    continue;

                grown = realloc(
                    keyframe.transformations,
                    (size_t)(keyframe.transformation_count + 1) * sizeof(*grown));
                if( !grown )
                {
                    in.failed = true;
                    break;
                }
                keyframe.transformations = grown;
                keyframe.transformation_capacity = keyframe.transformation_count + 1;
                keyframe.transformations[keyframe.transformation_count] = child;
                keyframe.transformations[reference_index].child_by_type[child_type] =
                    keyframe.transformation_count;
                keyframe.transformations[reference_index]
                    .child_order[keyframe.transformations[reference_index].child_count++] =
                    keyframe.transformation_count;
                keyframe.transformation_count++;
            }
        }

        pg_keyframe_build_skeleton(&keyframe);
        pg_animation_insert_keyframe(anim, &keyframe, anim->keyframe_count);
        pg_keyframe_free_contents(&keyframe);
    }

    anim->left_hand_item = r4(&in);
    anim->right_hand_item = r4(&in);
    free(data);

    if( in.failed || anim->keyframe_count == 0 )
    {
        pg_animation_free(anim);
        pg_app_message(app, "Import Failed", "The file is not a readable .pgl");
        return false;
    }

    anim->loaded = true;
    anim->length = pg_animation_calculate_length(anim);
    pg_app_add_animation(app, anim);
    pg_app_select_animation(app, app->animation_count - 1);
    pg_app_status(app, "Imported %s as sequence %d", path, anim->id);
    return true;
}

/* ---- .dat export ------------------------------------------------------------------- */

/* Non-zero components only, which is what the 317 frame format encodes. */
static int
delta_mask(const int delta[3])
{
    return (delta[0] != 0 ? 1 : 0) | (delta[1] != 0 ? 2 : 0) | (delta[2] != 0 ? 4 : 0);
}

bool
pg_export_dat(struct PG_App* app, const char* path)
{
    struct PG_Animation* anim = pg_app_current_animation(app);
    struct PG_FrameMapDef* framemap;
    struct Writer out = { 0 };
    int modified_count = 0;
    int written_index = 0;
    bool ok;

    if( !anim || anim->keyframe_count == 0 )
    {
        pg_app_message(app, "Invalid Operation", "No animation selected");
        return false;
    }
    framemap = anim->keyframes[0].framemap;
    if( !framemap )
    {
        pg_app_message(app, "Export Failed", "Keyframe has no framemap");
        return false;
    }

    /* The 317 framemap, whose fields are 16 bit where the newer one's are 8. */
    w2(&out, framemap->length);
    for( int i = 0; i < framemap->length; i++ )
        w2(&out, framemap->types[i]);
    for( int i = 0; i < framemap->length; i++ )
        w2(&out, framemap->map_lengths[i]);
    for( int i = 0; i < framemap->length; i++ )
        for( int j = 0; j < framemap->map_lengths[i]; j++ )
            w2(&out, framemap->maps[i][j]);

    for( int i = 0; i < anim->keyframe_count; i++ )
        if( anim->keyframes[i].modified )
            modified_count++;
    w2(&out, modified_count);

    for( int i = 0; i < anim->keyframe_count; i++ )
    {
        const struct PG_Keyframe* keyframe = &anim->keyframes[i];
        int index = 0;

        if( !keyframe->modified )
            continue;
        w2(&out, written_index++);
        w1(&out, keyframe->transformation_count);

        for( int t = 0; t < keyframe->transformation_count; t++ )
        {
            const struct PG_Transformation* transformation = &keyframe->transformations[t];
            int mask;

            /* Ignored masks for the transform indices this keyframe skipped, so
             * the reader lands each flag byte on the bone it belongs to. */
            for( int pad = index; pad < transformation->id; pad++ )
                w1(&out, 0);
            index = transformation->id + 1;

            mask = delta_mask(transformation->delta);
            w1(&out, mask);
            if( mask == 0 )
                continue;
            if( mask & 1 )
                w2(&out, transformation->delta[0]);
            if( mask & 2 )
                w2(&out, transformation->delta[1]);
            if( mask & 4 )
                w2(&out, transformation->delta[2]);
        }
    }

    ok = !out.failed && write_file(path, out.data, out.size);
    free(out.data);
    if( ok )
    {
        snprintf(app->last_export, sizeof(app->last_export), "%s", path);
        app->last_export_format = 1;
        pg_app_status(app, "Exported %d bytes to %s", out.size, path);
    }
    else
    {
        pg_app_message(app, "Export Failed", "Could not write the file");
    }
    return ok;
}

void
pg_export_redo(struct PG_App* app)
{
    if( app->last_export[0] == '\0' )
        return;
    if( app->last_export_format == 1 )
        pg_export_dat(app, app->last_export);
    else
        pg_export_pgl(app, app->last_export);
}

/* ---- pack ------------------------------------------------------------------------- */

bool
pg_pack_animation(struct PG_App* app, const char* out_directory)
{
    struct PG_Animation* anim = pg_app_current_animation(app);
    struct PG_FrameMapDef* framemap;
    struct PG_PackFrame* frames;
    struct PG_SeqDef sequence;
    int* frame_ids;
    int* frame_lengths;
    int modified_count = 0;
    int archive_id = -1;
    int result;

    if( !anim )
    {
        pg_app_message(app, "Invalid Operation", "No animation selected");
        return false;
    }
    if( !anim->modified )
    {
        pg_app_message(app, "Invalid Operation", "This animation has not been modified");
        return false;
    }
    framemap = anim->keyframes[0].framemap;
    if( !framemap || framemap->id < 0 )
    {
        /* An imported animation carries a framemap that no cache archive backs, so
         * there is no id for a frame to reference. Packing it would write frames
         * pointing at a skeleton the client cannot load. */
        pg_app_message(app, "Invalid Operation", "Animation has no cache framemap");
        return false;
    }

    for( int i = 0; i < anim->keyframe_count; i++ )
        if( anim->keyframes[i].modified )
            modified_count++;
    if( modified_count == 0 )
    {
        pg_app_message(app, "Invalid Operation", "No keyframe was edited");
        return false;
    }

    if( tool_copy_cache_dir(pg_cache_path(app->cache), out_directory) != 0 )
    {
        pg_app_message(app, "Pack Failed", "Could not copy the cache");
        return false;
    }

    frames = calloc((size_t)modified_count, sizeof(*frames));
    frame_ids = calloc((size_t)anim->keyframe_count, sizeof(int));
    frame_lengths = calloc((size_t)anim->keyframe_count, sizeof(int));
    if( !frames || !frame_ids || !frame_lengths )
    {
        free(frames);
        free(frame_ids);
        free(frame_lengths);
        return false;
    }

    /* A sequence's frame id embeds the archive it lives in, so the archive id has
     * to be settled before the sequence record is built. Every unmodified
     * keyframe keeps pointing at the frame it came from — only what was edited
     * gets a slot in the new archive. */
    archive_id = pg_cache_next_anim_archive_id(app->cache);
    if( archive_id < 0 )
    {
        free(frames);
        free(frame_ids);
        free(frame_lengths);
        pg_app_message(app, "Pack Failed", "Cache has no animations table");
        return false;
    }
    {
        int slot = 0;
        for( int i = 0; i < anim->keyframe_count; i++ )
        {
            struct PG_Keyframe* keyframe = &anim->keyframes[i];
            frame_lengths[i] = keyframe->length;
            if( !keyframe->modified )
            {
                frame_ids[i] = keyframe->frame_id;
                continue;
            }
            frames[slot].count = keyframe->transformation_count;
            frames[slot].indices = calloc((size_t)keyframe->transformation_count, sizeof(int));
            frames[slot].delta_x = calloc((size_t)keyframe->transformation_count, sizeof(int));
            frames[slot].delta_y = calloc((size_t)keyframe->transformation_count, sizeof(int));
            frames[slot].delta_z = calloc((size_t)keyframe->transformation_count, sizeof(int));
            for( int t = 0; t < keyframe->transformation_count; t++ )
            {
                frames[slot].indices[t] = keyframe->transformations[t].id;
                frames[slot].delta_x[t] = keyframe->transformations[t].delta[0];
                frames[slot].delta_y[t] = keyframe->transformations[t].delta[1];
                frames[slot].delta_z[t] = keyframe->transformations[t].delta[2];
            }
            frame_ids[i] = ((archive_id & 0xFFFF) << 16) | (slot & 0xFFFF);
            slot++;
        }
    }

    memset(&sequence, 0, sizeof(sequence));
    sequence.id = anim->id;
    sequence.frame_count = anim->keyframe_count;
    sequence.frame_ids = frame_ids;
    sequence.frame_lengths = frame_lengths;
    sequence.loop_offset = anim->loop_offset;
    sequence.left_hand_item = anim->left_hand_item;
    sequence.right_hand_item = anim->right_hand_item;

    result = pg_cache_pack_animation(
        app->cache, out_directory, framemap, frames, modified_count, &sequence, &archive_id);

    for( int i = 0; i < modified_count; i++ )
    {
        free(frames[i].indices);
        free(frames[i].delta_x);
        free(frames[i].delta_y);
        free(frames[i].delta_z);
    }
    free(frames);
    free(frame_ids);
    free(frame_lengths);

    if( result != 0 )
    {
        pg_app_message(app, "Pack Failed", "See the console for the reason");
        return false;
    }
    anim->modified = false;
    app->lists[PG_LIST_SEQUENCE].dirty = true;
    pg_app_status(
        app, "Packed sequence %d into %s (archive %d)", anim->id, out_directory, archive_id);
    return true;
}
