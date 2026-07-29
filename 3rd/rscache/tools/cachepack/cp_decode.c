#include "cp_assets.h"

#include "dat2disk.h"
#include "bmp.h"
#include "datatypes/clientscript.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_worldmap.h"
#include "datatypes/maps.h"
#include "cs2/cs2_compile.h"
#include "cs2/cs2_decompile.h"
#include "cs2/cs2_names.h"
#include "filelist.h"
#include "reference_table.h"
#include "datatypes/dat2_component.h"
#include "datatypes/dat2_sprites.h"
#include "datatypes/dat2_texture.h"

#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

/*
 * Friendly forms: the payload as something a person can read and edit, rather
 * than as the bytes the cache holds.
 *
 * The raw asset tree already gives every asset its own named file. This is the
 * next step for the kinds where the bytes mean something a text or image form can
 * express — and only for those. A model stays a model, because there is no
 * readable form of one that is not a lossy conversion (EXCEPTIONS.md A5).
 *
 * Every codec here is held to the same bar as the config text layer: decode,
 * write, read back, encode, and compare against the bytes the cache held. Where
 * that is not exact, the reason is recorded rather than smoothed over.
 *
 * ## Declining is a first-class outcome
 *
 * A codec returns 0 (or NULL) for anything it cannot handle, and the caller falls
 * back to the raw payload. That path is load-bearing, not defensive: 37 of
 * osrs239's clientscripts do not decompile, and a mode that silently dropped them
 * would produce a cache missing 37 scripts with nothing in the output to say so.
 */

/* ---- shared file helpers ------------------------------------------------ */

static int
write_text_file(
    const char* path,
    const struct CP_Lines* lines,
    const char* header)
{
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        fprintf(stderr, "cachepack: cannot write %s: %s\n", path, strerror(errno));
        return 0;
    }
    if( header )
        fprintf(out, "%s", header);
    for( int i = 0; i < lines->count; i++ )
        fprintf(out, "%s\n", lines->lines[i]);
    fclose(out);
    return 1;
}

static uint8_t*
slurp(
    const char* path,
    int* out_size)
{
    FILE* in = fopen(path, "rb");
    if( !in )
        return NULL;
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(in);
        return NULL;
    }
    uint8_t* data = malloc((size_t)size + 1);
    if( !data || (size > 0 && fread(data, 1, (size_t)size, in) != (size_t)size) )
    {
        free(data);
        fclose(in);
        return NULL;
    }
    data[size] = '\0';
    fclose(in);
    *out_size = (int)size;
    return data;
}

/** mkdir -p for a codec that writes a directory of files. */
static int
ensure_dir_path(const char* path)
{
    char buf[1800];
    snprintf(buf, sizeof(buf), "%s", path);
    for( char* p = buf + 1; *p; p++ )
    {
        if( *p != '/' )
            continue;
        *p = '\0';
        mkdir(buf, 0755);
        *p = '/';
    }
    return mkdir(buf, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int
read_file_bytes(
    const char* path,
    uint8_t** out_data,
    int* out_size)
{
    int size = 0;
    uint8_t* data = slurp(path, &size);
    if( !data )
        return 0;
    *out_data = data;
    *out_size = size;
    return 1;
}

/* ---- textures ------------------------------------------------------------ */

/*
 * A texture is not an image.
 *
 * That is the whole reason this one is text. LostCity stores textures as PNG files
 * because a rev-254 texture *is* a bitmap; an OldSchool texture is a small record
 * that names the sprite to draw and how to animate it. Exporting a PNG would mean
 * rendering something the cache never stored.
 */

static int
texture_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    struct RSCache_Dat2Texture* texture =
        RSCache_Dat2TextureNewDecodeProfile(&ctx->profile, (char*)payload, size);
    if( !texture )
        return 0;

    struct CP_Lines lines;
    cp_lines_init(&lines);
    cp_lines_addf(&lines, "averagehsl=%d", texture->average_hsl);
    if( texture->opaque )
        cp_lines_addf(&lines, "opaque=yes");
    /* One line per sprite: its id, its type and its transform travel together
     * because the three arrays are parallel and a hand-edit that desynchronised
     * them would be silently wrong. */
    for( int i = 0; i < texture->sprite_ids_count; i++ )
        cp_lines_addf(&lines, "sprite%d=%d,%d,%d", i + 1, texture->sprite_ids[i],
                      texture->sprite_types ? texture->sprite_types[i] : 0,
                      texture->transforms ? texture->transforms[i] : 0);
    if( texture->animation_direction )
        cp_lines_addf(&lines, "direction=%d", texture->animation_direction);
    if( texture->animation_speed )
        cp_lines_addf(&lines, "speed=%d", texture->animation_speed);

    char path[1700];
    snprintf(path, sizeof(path), "%s.texture", path_stem);
    int ok = write_text_file(path, &lines, NULL);
    cp_lines_free(&lines);
    RSCache_Dat2TextureFree(texture);
    return ok;
}

static uint8_t*
texture_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char path[1700];
    snprintf(path, sizeof(path), "%s.texture", path_stem);
    struct CP_ConfigFile file;
    int text_size = 0;
    uint8_t* text = slurp(path, &text_size);
    if( !text )
        return NULL;

    /* The text layer wants a `[name]` block; a per-file record has no name of its
     * own, so one is supplied to reuse the same parser. */
    char* wrapped = malloc((size_t)text_size + 32);
    if( !wrapped )
    {
        free(text);
        return NULL;
    }
    int prefix = snprintf(wrapped, 32, "[texture]\n");
    memcpy(wrapped + prefix, text, (size_t)text_size);
    free(text);

    int parsed = cp_config_file_load_memory(&file, wrapped, (size_t)(prefix + text_size),
                                            "texture");
    free(wrapped);
    if( !parsed || file.count != 1 )
    {
        if( parsed )
            cp_config_file_free(&file);
        return NULL;
    }

    struct RSCache_Dat2Texture texture;
    memset(&texture, 0, sizeof(texture));
    texture._id = record_id;

    struct CP_IntList ids = { 0 }, types = { 0 }, transforms = { 0 };
    int ok = 1;
    for( int i = 0; i < file.configs[0].count && ok; i++ )
    {
        const char* key = file.configs[0].lines[i].key;
        const char* value = file.configs[0].lines[i].value;
        int index = cp_indexed_key(key, "sprite");
        if( index >= 0 )
        {
            char scratch[128];
            char* fields[3];
            if( strlen(value) >= sizeof(scratch) ||
                cp_split(value, scratch, fields, 3) != 3 )
            {
                ok = 0;
                break;
            }
            int sprite = 0, type = 0, transform = 0;
            ok = cp_parse_int(fields[0], &sprite) && cp_parse_int(fields[1], &type) &&
                 cp_parse_int(fields[2], &transform);
            cp_intlist_set(&ids, index, sprite);
            cp_intlist_set(&types, index, type);
            cp_intlist_set(&transforms, index, transform);
        }
        else if( strcmp(key, "averagehsl") == 0 )
            ok = cp_parse_int(value, &texture.average_hsl);
        else if( strcmp(key, "opaque") == 0 )
            ok = cp_parse_bool(value, &texture.opaque);
        else if( strcmp(key, "direction") == 0 )
            ok = cp_parse_int(value, &texture.animation_direction);
        else if( strcmp(key, "speed") == 0 )
            ok = cp_parse_int(value, &texture.animation_speed);
    }
    cp_config_file_free(&file);

    uint8_t* payload = NULL;
    if( ok )
    {
        texture.sprite_ids = ids.items;
        texture.sprite_types = types.items;
        texture.transforms = transforms.items;
        texture.sprite_ids_count = ids.count;

        uint8_t buffer[512];
        uint32_t written =
            RSCache_Dat2TextureEncodeProfile(&ctx->profile, &texture, buffer, sizeof(buffer));
        if( written > 0 )
        {
            payload = malloc(written);
            if( payload )
            {
                memcpy(payload, buffer, written);
                *out_size = (int)written;
            }
        }
    }
    cp_intlist_free(&ids);
    cp_intlist_free(&types);
    cp_intlist_free(&transforms);
    return payload;
}

/* ---- interfaces ---------------------------------------------------------- */

/*
 * One `.if` per interface, one `[com <id>]` block per component — LostCity's
 * shape, because an interface is a thing you read whole.
 *
 * The archive is not exploded into a file per component (see the register), so
 * this codec receives the whole archive payload, walks its file list, and writes
 * every component into one text file. The reverse rebuilds the file list, which
 * is why each block carries its own component id rather than relying on order.
 *
 * Only the fields the decoder keeps are written, and IF1 and IF3 keep different
 * ones — so the block leads with `if3` and the reader dispatches on it, exactly
 * as the byte decoder dispatches on the 255 marker.
 */

#define IF_EMIT_INT(field, key)                                                               \
    do                                                                                        \
    {                                                                                         \
        if( comp->field != reference->field )                                                 \
            cp_lines_addf(out, key "=%d", comp->field);                                       \
    } while( 0 )

#define IF_EMIT_BOOL(field, key)                                                              \
    do                                                                                        \
    {                                                                                         \
        if( comp->field != reference->field )                                                 \
            cp_lines_addf(out, key "=%s", comp->field ? "yes" : "no");                        \
    } while( 0 )

#define IF_EMIT_STR(field, key)                                                               \
    do                                                                                        \
    {                                                                                         \
        if( comp->field )                                                                     \
            cp_lines_add_str(out, key, comp->field);                                          \
    } while( 0 )

static void
emit_script_vars(
    struct CP_Lines* out,
    const char* key,
    const struct RSCache_Dat2ComponentScriptVar* args,
    int32_t len)
{
    if( !args || len <= 0 )
        return;
    /* One line per hook, arguments comma-joined and tagged by kind — the wire
     * carries a type byte per argument and an int 5 is not a string "5". */
    char buf[4096];
    int written = 0;
    for( int32_t i = 0; i < len && written < (int)sizeof(buf) - 64; i++ )
    {
        if( args[i].type == RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT )
            written += snprintf(buf + written, sizeof(buf) - (size_t)written, i ? ",i:%d" : "i:%d",
                                args[i].value.i);
        else
            written += snprintf(buf + written, sizeof(buf) - (size_t)written, i ? ",s:%s" : "s:%s",
                                args[i].value.s ? args[i].value.s : "");
    }
    cp_lines_add_str(out, key, buf);
}

static void
emit_triggers(
    struct CP_Lines* out,
    const char* key,
    const int32_t* triggers,
    int32_t len)
{
    if( !triggers || len <= 0 )
        return;
    char buf[2048];
    int written = 0;
    for( int32_t i = 0; i < len && written < (int)sizeof(buf) - 16; i++ )
        written += snprintf(buf + written, sizeof(buf) - (size_t)written, i ? ",%d" : "%d",
                            triggers[i]);
    cp_lines_addf(out, "%s=%s", key, buf);
}

static void
emit_component(
    struct CP_Ctx* ctx,
    const struct RSCache_Dat2Component* comp,
    const struct RSCache_Dat2Component* reference,
    struct CP_Lines* out)
{
    cp_lines_addf(out, "if3=%s", comp->if3 ? "yes" : "no");
    cp_lines_addf(out, "type=%d", comp->type);

    IF_EMIT_INT(clientCode, "clientcode");
    IF_EMIT_INT(baseX, "x");
    IF_EMIT_INT(baseY, "y");
    IF_EMIT_INT(baseWidth, "width");
    IF_EMIT_INT(baseHeight, "height");
    IF_EMIT_INT(widthMode, "widthmode");
    IF_EMIT_INT(heightMode, "heightmode");
    IF_EMIT_INT(xMode, "xmode");
    IF_EMIT_INT(yMode, "ymode");
    IF_EMIT_INT(layer, "layer");
    IF_EMIT_BOOL(hidden, "hidden");
    IF_EMIT_INT(transparency, "trans");
    IF_EMIT_INT(color, "colour");
    IF_EMIT_BOOL(fill, "fill");
    IF_EMIT_BOOL(alpha, "alpha");
    IF_EMIT_BOOL(tiled, "tiled");
    IF_EMIT_INT(outline, "outline");
    IF_EMIT_INT(graphic, "graphic");
    IF_EMIT_INT(graphicShadow, "graphicshadow");
    IF_EMIT_BOOL(horizontalFlip, "hflip");
    IF_EMIT_BOOL(verticalFlip, "vflip");
    IF_EMIT_INT(angle, "angle");
    IF_EMIT_INT(scrollWidth, "scrollwidth");
    IF_EMIT_INT(scrollHeight, "scrollheight");
    IF_EMIT_BOOL(noClickThrough, "noclickthrough");
    IF_EMIT_INT(clickMask, "clickmask");
    IF_EMIT_INT(lineWidth, "linewidth");
    IF_EMIT_BOOL(lineDirection, "linedirection");
    IF_EMIT_INT(buttonType, "buttontype");
    IF_EMIT_INT(linkedComponentId, "linked");
    IF_EMIT_INT(marginX, "marginx");
    IF_EMIT_INT(marginY, "marginy");

    IF_EMIT_STR(text, "text");
    IF_EMIT_STR(name, "name");
    IF_EMIT_STR(targetVerb, "targetverb");
    IF_EMIT_STR(targetText, "targettext");
    IF_EMIT_STR(activeText, "activetext");
    if( comp->option && !comp->optionFromDefault )
        cp_lines_add_str(out, "option", comp->option);
    IF_EMIT_INT(textFont, "font");
    IF_EMIT_INT(textHorizontalAlignment, "halign");
    IF_EMIT_INT(textVerticalAlignment, "valign");
    IF_EMIT_INT(textLineHeight, "lineheight");
    IF_EMIT_BOOL(textShadow, "shadow");

    IF_EMIT_INT(modelId, "model");
    IF_EMIT_INT(modelZoom, "modelzoom");
    IF_EMIT_INT(modelXAngle, "modelxan");
    IF_EMIT_INT(modelYAngle, "modelyan");
    IF_EMIT_INT(modelZAngle, "modelzan");
    IF_EMIT_INT(modelXOffset, "modelxof");
    IF_EMIT_INT(modelYOffset, "modelyof");
    IF_EMIT_INT(modelSeqId, "modelanim");
    IF_EMIT_BOOL(modelOrthographic, "modelortho");
    IF_EMIT_INT(activeModelId, "activemodel");
    IF_EMIT_INT(activeAnimId, "activeanim");
    IF_EMIT_INT(activeGraphic, "activegraphic");
    IF_EMIT_INT(activeColour, "activecolour");
    IF_EMIT_INT(overColour, "overcolour");
    IF_EMIT_INT(activeOverColour, "activeovercolour");

    if( comp->aShort50 != reference->aShort50 )
        cp_lines_addf(out, "short50=%d", comp->aShort50);
    IF_EMIT_INT(anInt5957, "int5957");
    IF_EMIT_INT(anInt5920, "int5920");

    for( int32_t i = 0; i < comp->opsLen; i++ )
    {
        if( comp->ops && comp->ops[i] )
        {
            char key[24];
            snprintf(key, sizeof(key), "op%d", i + 1);
            cp_lines_add_str(out, key, comp->ops[i]);
        }
    }
    if( comp->objOps )
    {
        for( int i = 0; i < 5; i++ )
        {
            if( comp->objOps[i] )
            {
                char key[24];
                snprintf(key, sizeof(key), "objop%d", i + 1);
                cp_lines_add_str(out, key, comp->objOps[i]);
            }
        }
    }

    IF_EMIT_INT(dragDeadZone, "dragdeadzone");
    IF_EMIT_INT(dragDeadTime, "dragdeadtime");
    IF_EMIT_BOOL(dragRender, "dragrender");

    /* IF1 inventory slots: written only where the wire said the slot was there,
     * which is what `invSlotPresent` records. */
    for( int i = 0; i < 20; i++ )
    {
        if( comp->invSlotPresent[i] )
            cp_lines_addf(out, "invslot%d=%d,%d,%d", i + 1, comp->invSlotOffsetX[i],
                          comp->invSlotOffsetY[i], comp->invSlotGraphicId[i]);
    }

    for( int32_t i = 0; i < comp->cs1ComparisonLen; i++ )
        cp_lines_addf(out, "cs1cmp%d=%d,%d", i + 1, comp->cs1ComparisonOpcodes[i],
                      comp->cs1ComparisonOperands[i]);
    for( int32_t i = 0; i < comp->cs1ScriptsLen; i++ )
    {
        char buf[4096];
        int written = 0;
        for( int32_t j = 0; j < comp->cs1ScriptsLengths[i] && written < (int)sizeof(buf) - 16; j++ )
            written += snprintf(buf + written, sizeof(buf) - (size_t)written, j ? ",%d" : "%d",
                                comp->cs1Scripts[i][j]);
        cp_lines_addf(out, "cs1script%d=%s", i + 1, comp->cs1ScriptsLengths[i] ? buf : "");
    }

    emit_script_vars(out, "onload", comp->onLoad, comp->onLoadLen);
    emit_script_vars(out, "onmouseover", comp->onMouseOver, comp->onMouseOverLen);
    emit_script_vars(out, "onmouseleave", comp->onMouseLeave, comp->onMouseLeaveLen);
    emit_script_vars(out, "ontargetleave", comp->onTargetLeave, comp->onTargetLeaveLen);
    emit_script_vars(out, "ontargetenter", comp->onTargetEnter, comp->onTargetEnterLen);
    emit_script_vars(out, "onvarptransmit", comp->onVarpTransmit, comp->onVarpTransmitLen);
    emit_script_vars(out, "oninvtransmit", comp->onInvTransmit, comp->onInvTransmitLen);
    emit_script_vars(out, "onstattransmit", comp->onStatTransmit, comp->onStatTransmitLen);
    emit_script_vars(out, "ontimer", comp->onTimer, comp->onTimerLen);
    emit_script_vars(out, "onop", comp->onOp, comp->onOpLen);
    emit_script_vars(out, "onmouserepeat", comp->onMouseRepeat, comp->onMouseRepeatLen);
    emit_script_vars(out, "onclick", comp->onClick, comp->onClickLen);
    emit_script_vars(out, "onclickrepeat", comp->onClickRepeat, comp->onClickRepeatLen);
    emit_script_vars(out, "onrelease", comp->onRelease, comp->onReleaseLen);
    emit_script_vars(out, "onhold", comp->onHold, comp->onHoldLen);
    emit_script_vars(out, "ondrag", comp->onDrag, comp->onDragLen);
    emit_script_vars(out, "ondragcomplete", comp->onDragComplete, comp->onDragCompleteLen);
    emit_script_vars(out, "onscrollwheel", comp->onScrollWheel, comp->onScrollWheelLen);

    emit_triggers(out, "varptriggers", comp->varpTriggers, comp->varpTriggersLen);
    emit_triggers(out, "invtriggers", comp->inventoryTriggers, comp->inventoryTriggersLen);
    emit_triggers(out, "stattriggers", comp->statTriggers, comp->statTriggersLen);
}

#undef IF_EMIT_INT
#undef IF_EMIT_BOOL
#undef IF_EMIT_STR

static int
interface_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    if( file_count <= 0 )
        return 0;
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode((char*)payload, size, file_count);
    if( !files )
        return 0;

    /* The interfaces table's own revision gates the type-6 model-id width, so it
     * is read from the open cache rather than carried on the context. */
    int table = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_INTERFACES);
    struct RSCache_ReferenceTable* reftable =
        table == RSCACHE_DAT2_DISK_TABLE_ABSENT ? NULL : ctx->cache.disk->tables[table];
    struct RSCache_Dat2ComponentDecodeRev rev = RSCache_Dat2ComponentDecodeRevFromProfile(
        &ctx->profile, reftable ? reftable->version : -1);

    /* A reference component, decoded from nothing, supplies the defaults every
     * field is compared against — the same trick the config text layer uses, and
     * for the same reason: the defaults stay the decoder's, not a second copy. */
    struct RSCache_Dat2Component reference;
    RSCache_Dat2ComponentInit(&reference);

    char path[1700];
    snprintf(path, sizeof(path), "%s.if", path_stem);
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        RSCache_FileListFree(files);
        return 0;
    }
    fprintf(out, "// Interface %d — %d components.\n", record_id, files->file_count);
    fprintf(out, "// One [com <id>] block per component; `if3` selects the field set.\n\n");

    struct CP_Lines lines;
    cp_lines_init(&lines);
    int written = 0;
    for( int f = 0; f < files->file_count; f++ )
    {
        int file_id = file_ids ? file_ids[f] : f;
        if( files->file_sizes[f] <= 0 )
        {
            /* A zero-length member is a hole the archive still counts; it has to
             * come back as one or every later component shifts an id. */
            fprintf(out, "[com_%d]\nempty=yes\n\n", file_id);
            written++;
            continue;
        }
        struct RSCache_Dat2Component* comp = RSCache_Dat2ComponentNewDecode(
            (uint8_t*)files->files[f], files->file_sizes[f], (record_id << 16) | file_id, rev);
        if( !comp )
            continue;
        cp_lines_clear(&lines);
        emit_component(ctx, comp, &reference, &lines);
        char header[64];
        snprintf(header, sizeof(header), "[com_%d]\n", file_id);
        fprintf(out, "%s", header);
        for( int i = 0; i < lines.count; i++ )
            fprintf(out, "%s\n", lines.lines[i]);
        fputc('\n', out);
        written++;
        RSCache_Dat2ComponentFree(comp);
    }
    cp_lines_free(&lines);
    fclose(out);
    RSCache_Dat2ComponentFree(&reference);
    RSCache_FileListFree(files);
    return written > 0;
}


/* ---- interfaces: reading the text back ---------------------------------- */

/** `i:5` / `s:text` — the wire carries a type byte per argument, so the text does
 *  too. An int 5 and a string "5" are different bytes and different behaviour. */
static int
parse_script_vars(
    const char* value,
    struct RSCache_Dat2ComponentScriptVar** out,
    int32_t* out_len)
{
    char buf[4096];
    cp_unescape(value, buf, sizeof(buf));
    if( !buf[0] )
        return 1;

    int capacity = 8, count = 0;
    struct RSCache_Dat2ComponentScriptVar* args = calloc((size_t)capacity, sizeof(*args));
    if( !args )
        return 0;

    char* cursor = buf;
    while( cursor )
    {
        char* comma = strchr(cursor, ',');
        if( comma )
            *comma = '\0';
        if( count == capacity )
        {
            int next = capacity * 2;
            struct RSCache_Dat2ComponentScriptVar* grown =
                realloc(args, (size_t)next * sizeof(*grown));
            if( !grown )
            {
                free(args);
                return 0;
            }
            args = grown;
            capacity = next;
        }
        if( cursor[0] == 's' && cursor[1] == ':' )
        {
            args[count].type = RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_STRING;
            args[count].value.s = strdup(cursor + 2);
        }
        else
        {
            const char* text = (cursor[0] == 'i' && cursor[1] == ':') ? cursor + 2 : cursor;
            args[count].type = RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT;
            if( !cp_parse_int(text, &args[count].value.i) )
            {
                free(args);
                return 0;
            }
        }
        count++;
        cursor = comma ? comma + 1 : NULL;
    }
    *out = args;
    *out_len = count;
    return 1;
}

static int
parse_triggers(
    const char* value,
    int32_t** out,
    int32_t* out_len)
{
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", value);
    if( !buf[0] )
        return 1;
    char* fields[256];
    char scratch[2048];
    int count = cp_split(buf, scratch, fields, 256);
    int32_t* triggers = malloc((size_t)count * sizeof(int32_t));
    if( !triggers )
        return 0;
    for( int i = 0; i < count; i++ )
    {
        int value_i = 0;
        if( !cp_parse_int(fields[i], &value_i) )
        {
            free(triggers);
            return 0;
        }
        triggers[i] = value_i;
    }
    *out = triggers;
    *out_len = count;
    return 1;
}

#define IF_READ_INT(key, field)                                                               \
    if( strcmp(name, key) == 0 )                                                              \
    {                                                                                         \
        int tmp = 0;                                                                          \
        if( !cp_parse_int(value, &tmp) )                                                      \
            return 0;                                                                         \
        comp->field = tmp;                                                                    \
        return 1;                                                                             \
    }

#define IF_READ_BOOL(key, field)                                                              \
    if( strcmp(name, key) == 0 )                                                              \
    {                                                                                         \
        bool flag = false;                                                                    \
        if( !cp_parse_bool(value, &flag) )                                                    \
            return 0;                                                                         \
        comp->field = flag;                                                                   \
        return 1;                                                                             \
    }

#define IF_READ_STR(key, field)                                                               \
    if( strcmp(name, key) == 0 )                                                              \
    {                                                                                         \
        char text[4096];                                                                      \
        cp_unescape(value, text, sizeof(text));                                               \
        free(comp->field);                                                                    \
        comp->field = strdup(text);                                                           \
        return 1;                                                                             \
    }

#define IF_READ_HOOK(key, field)                                                              \
    if( strcmp(name, key) == 0 )                                                              \
        return parse_script_vars(value, &comp->field, &comp->field##Len);

static int
read_component_line(
    struct RSCache_Dat2Component* comp,
    const char* name,
    const char* value)
{
    int index;

    IF_READ_BOOL("if3", if3)
    IF_READ_INT("type", type)
    IF_READ_INT("clientcode", clientCode)
    IF_READ_INT("x", baseX)
    IF_READ_INT("y", baseY)
    IF_READ_INT("width", baseWidth)
    IF_READ_INT("height", baseHeight)
    IF_READ_INT("widthmode", widthMode)
    IF_READ_INT("heightmode", heightMode)
    IF_READ_INT("xmode", xMode)
    IF_READ_INT("ymode", yMode)
    IF_READ_INT("layer", layer)
    IF_READ_BOOL("hidden", hidden)
    IF_READ_INT("trans", transparency)
    IF_READ_INT("colour", color)
    IF_READ_BOOL("fill", fill)
    IF_READ_BOOL("alpha", alpha)
    IF_READ_BOOL("tiled", tiled)
    IF_READ_INT("outline", outline)
    IF_READ_INT("graphic", graphic)
    IF_READ_INT("graphicshadow", graphicShadow)
    IF_READ_BOOL("hflip", horizontalFlip)
    IF_READ_BOOL("vflip", verticalFlip)
    IF_READ_INT("angle", angle)
    IF_READ_INT("scrollwidth", scrollWidth)
    IF_READ_INT("scrollheight", scrollHeight)
    IF_READ_BOOL("noclickthrough", noClickThrough)
    IF_READ_INT("clickmask", clickMask)
    IF_READ_INT("linewidth", lineWidth)
    IF_READ_BOOL("linedirection", lineDirection)
    IF_READ_INT("buttontype", buttonType)
    IF_READ_INT("linked", linkedComponentId)
    IF_READ_INT("marginx", marginX)
    IF_READ_INT("marginy", marginY)
    IF_READ_STR("text", text)
    IF_READ_STR("name", name)
    IF_READ_STR("targetverb", targetVerb)
    IF_READ_STR("targettext", targetText)
    IF_READ_STR("activetext", activeText)
    IF_READ_STR("option", option)
    IF_READ_INT("font", textFont)
    IF_READ_INT("halign", textHorizontalAlignment)
    IF_READ_INT("valign", textVerticalAlignment)
    IF_READ_INT("lineheight", textLineHeight)
    IF_READ_BOOL("shadow", textShadow)
    IF_READ_INT("model", modelId)
    IF_READ_INT("modelzoom", modelZoom)
    IF_READ_INT("modelxan", modelXAngle)
    IF_READ_INT("modelyan", modelYAngle)
    IF_READ_INT("modelzan", modelZAngle)
    IF_READ_INT("modelxof", modelXOffset)
    IF_READ_INT("modelyof", modelYOffset)
    IF_READ_INT("modelanim", modelSeqId)
    IF_READ_BOOL("modelortho", modelOrthographic)
    IF_READ_INT("activemodel", activeModelId)
    IF_READ_INT("activeanim", activeAnimId)
    IF_READ_INT("activegraphic", activeGraphic)
    IF_READ_INT("activecolour", activeColour)
    IF_READ_INT("overcolour", overColour)
    IF_READ_INT("activeovercolour", activeOverColour)
    IF_READ_INT("short50", aShort50)
    IF_READ_INT("int5957", anInt5957)
    IF_READ_INT("int5920", anInt5920)
    IF_READ_INT("dragdeadzone", dragDeadZone)
    IF_READ_INT("dragdeadtime", dragDeadTime)
    IF_READ_BOOL("dragrender", dragRender)

    IF_READ_HOOK("onload", onLoad)
    IF_READ_HOOK("onmouseover", onMouseOver)
    IF_READ_HOOK("onmouseleave", onMouseLeave)
    IF_READ_HOOK("ontargetleave", onTargetLeave)
    IF_READ_HOOK("ontargetenter", onTargetEnter)
    IF_READ_HOOK("onvarptransmit", onVarpTransmit)
    IF_READ_HOOK("oninvtransmit", onInvTransmit)
    IF_READ_HOOK("onstattransmit", onStatTransmit)
    IF_READ_HOOK("ontimer", onTimer)
    IF_READ_HOOK("onop", onOp)
    IF_READ_HOOK("onmouserepeat", onMouseRepeat)
    IF_READ_HOOK("onclick", onClick)
    IF_READ_HOOK("onclickrepeat", onClickRepeat)
    IF_READ_HOOK("onrelease", onRelease)
    IF_READ_HOOK("onhold", onHold)
    IF_READ_HOOK("ondrag", onDrag)
    IF_READ_HOOK("ondragcomplete", onDragComplete)
    IF_READ_HOOK("onscrollwheel", onScrollWheel)

    if( strcmp(name, "varptriggers") == 0 )
        return parse_triggers(value, &comp->varpTriggers, &comp->varpTriggersLen);
    if( strcmp(name, "invtriggers") == 0 )
        return parse_triggers(value, &comp->inventoryTriggers, &comp->inventoryTriggersLen);
    if( strcmp(name, "stattriggers") == 0 )
        return parse_triggers(value, &comp->statTriggers, &comp->statTriggersLen);

    if( (index = cp_indexed_key(name, "op")) >= 0 )
    {
        char text[1024];
        cp_unescape(value, text, sizeof(text));
        RSCache_Dat2ComponentSetOp(comp, index, text);
        return 1;
    }
    if( (index = cp_indexed_key(name, "objop")) >= 0 )
    {
        if( index >= 5 )
            return 0;
        if( !comp->objOps )
            comp->objOps = calloc(5, sizeof(char*));
        if( !comp->objOps )
            return 0;
        char text[1024];
        cp_unescape(value, text, sizeof(text));
        free(comp->objOps[index]);
        comp->objOps[index] = strdup(text);
        return 1;
    }
    if( (index = cp_indexed_key(name, "invslot")) >= 0 )
    {
        if( index >= 20 )
            return 0;
        if( !comp->invSlotOffsetX )
        {
            comp->invSlotOffsetX = calloc(20, sizeof(int32_t));
            comp->invSlotOffsetY = calloc(20, sizeof(int32_t));
            comp->invSlotGraphicId = calloc(20, sizeof(int32_t));
            if( !comp->invSlotOffsetX || !comp->invSlotOffsetY || !comp->invSlotGraphicId )
                return 0;
            for( int i = 0; i < 20; i++ )
                comp->invSlotGraphicId[i] = -1;
        }
        char scratch[128];
        char* fields[3];
        if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 3) != 3 )
            return 0;
        int offset_x = 0, offset_y = 0, graphic = 0;
        if( !cp_parse_int(fields[0], &offset_x) || !cp_parse_int(fields[1], &offset_y) ||
            !cp_parse_int(fields[2], &graphic) )
            return 0;
        comp->invSlotOffsetX[index] = offset_x;
        comp->invSlotOffsetY[index] = offset_y;
        comp->invSlotGraphicId[index] = graphic;
        comp->invSlotPresent[index] = 1;
        return 1;
    }
    if( (index = cp_indexed_key(name, "cs1cmp")) >= 0 )
    {
        char scratch[64];
        char* fields[2];
        if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 2) != 2 )
            return 0;
        int need = index + 1;
        if( need > comp->cs1ComparisonLen )
        {
            int32_t* opcodes =
                realloc(comp->cs1ComparisonOpcodes, (size_t)need * sizeof(int32_t));
            int32_t* operands =
                realloc(comp->cs1ComparisonOperands, (size_t)need * sizeof(int32_t));
            if( !opcodes || !operands )
                return 0;
            comp->cs1ComparisonOpcodes = opcodes;
            comp->cs1ComparisonOperands = operands;
            comp->cs1ComparisonLen = need;
        }
        int opcode = 0, operand = 0;
        if( !cp_parse_int(fields[0], &opcode) || !cp_parse_int(fields[1], &operand) )
            return 0;
        comp->cs1ComparisonOpcodes[index] = opcode;
        comp->cs1ComparisonOperands[index] = operand;
        return 1;
    }
    if( (index = cp_indexed_key(name, "cs1script")) >= 0 )
    {
        int need = index + 1;
        if( need > comp->cs1ScriptsLen )
        {
            int32_t** scripts = realloc(comp->cs1Scripts, (size_t)need * sizeof(int32_t*));
            int32_t* lengths = realloc(comp->cs1ScriptsLengths, (size_t)need * sizeof(int32_t));
            if( !scripts || !lengths )
                return 0;
            for( int i = comp->cs1ScriptsLen; i < need; i++ )
            {
                scripts[i] = NULL;
                lengths[i] = 0;
            }
            comp->cs1Scripts = scripts;
            comp->cs1ScriptsLengths = lengths;
            comp->cs1ScriptsLen = need;
        }
        if( !value[0] )
            return 1;
        char scratch[4096];
        char* fields[512];
        if( strlen(value) >= sizeof(scratch) )
            return 0;
        int count = cp_split(value, scratch, fields, 512);
        int32_t* ops = malloc((size_t)count * sizeof(int32_t));
        if( !ops )
            return 0;
        for( int i = 0; i < count; i++ )
        {
            int op = 0;
            if( !cp_parse_int(fields[i], &op) )
            {
                free(ops);
                return 0;
            }
            ops[i] = op;
        }
        free(comp->cs1Scripts[index]);
        comp->cs1Scripts[index] = ops;
        comp->cs1ScriptsLengths[index] = count;
        return 1;
    }
    /* `empty=yes` marks a zero-length member; handled by the caller. */
    if( strcmp(name, "empty") == 0 )
        return 1;
    return 0;
}

#undef IF_READ_INT
#undef IF_READ_BOOL
#undef IF_READ_STR
#undef IF_READ_HOOK

static uint8_t*
interface_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char path[1700];
    snprintf(path, sizeof(path), "%s.if", path_stem);
    struct CP_ConfigFile file;
    if( !cp_config_file_load(&file, path) )
        return NULL;

    int table = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_INTERFACES);
    struct RSCache_ReferenceTable* reftable =
        table == RSCACHE_DAT2_DISK_TABLE_ABSENT ? NULL : ctx->cache.disk->tables[table];
    struct RSCache_Dat2ComponentDecodeRev rev = RSCache_Dat2ComponentDecodeRevFromProfile(
        &ctx->profile, reftable ? reftable->version : -1);

    struct RSCache_FileList list;
    memset(&list, 0, sizeof(list));
    list.file_count = file.count;
    list.files = calloc((size_t)file.count, sizeof(char*));
    list.file_sizes = calloc((size_t)file.count, sizeof(int));
    int* ids = calloc((size_t)file.count, sizeof(int));
    uint8_t* payload = NULL;
    int ok = list.files && list.file_sizes && ids;

    for( int i = 0; i < file.count && ok; i++ )
    {
        const struct CP_Config* block = &file.configs[i];
        /* The block name is `com <id>`; the id is data, not position, so a hole
         * in the component list survives the round trip. */
        int file_id = 0;
        if( sscanf(block->debugname, "com_%d", &file_id) != 1 )
        {
            fprintf(stderr, "cachepack: %s: bad component header [%s]\n", path,
                    block->debugname);
            ok = 0;
            break;
        }
        ids[i] = file_id;

        const char* empty = cp_config_get(block, "empty");
        if( empty )
        {
            list.files[i] = NULL;
            list.file_sizes[i] = 0;
            continue;
        }

        struct RSCache_Dat2Component comp;
        RSCache_Dat2ComponentInit(&comp);
        comp.id = (record_id << 16) | file_id;
        for( int line = 0; line < block->count && ok; line++ )
        {
            if( !read_component_line(&comp, block->lines[line].key, block->lines[line].value) )
            {
                fprintf(stderr, "cachepack: %s:%d: bad component property %s\n", path,
                        block->lines[line].line_no, block->lines[line].key);
                ok = 0;
            }
        }
        if( ok )
        {
            uint32_t bound = RSCache_Dat2ComponentEncodeIf3Bound(&comp);
            uint8_t* bytes = malloc(bound ? bound : 1);
            uint32_t written =
                bytes ? RSCache_Dat2ComponentEncodeIf3(&comp, rev, bytes, bound) : 0;
            if( written == 0 )
            {
                free(bytes);
                fprintf(stderr, "cachepack: %s: component %d failed to encode\n", path, file_id);
                ok = 0;
            }
            else
            {
                list.files[i] = (char*)bytes;
                list.file_sizes[i] = (int)written;
            }
        }
        RSCache_Dat2ComponentFree(&comp);
    }

    if( ok )
    {
        uint32_t bound = RSCache_FileListEncodeBound(&list);
        payload = malloc(bound ? bound : 1);
        if( payload )
        {
            uint32_t written = RSCache_FileListEncode(&list, payload, bound);
            if( written == 0 )
            {
                free(payload);
                payload = NULL;
            }
            else
            {
                *out_size = (int)written;
                *out_file_ids = ids;
                *out_file_count = file.count;
                ids = NULL;
            }
        }
    }

    for( int i = 0; i < list.file_count; i++ )
        free(list.files[i]);
    free(list.files);
    free(list.file_sizes);
    free(ids);
    cp_config_file_free(&file);
    return payload;
}

const struct CP_AssetCodec cp_codec_texture = { "texture", texture_write, texture_read };
const struct CP_AssetCodec cp_codec_interface = { "if", interface_write, interface_read };

/* ---- maps ---------------------------------------------------------------- */

/*
 * LostCity's `.jm2`: a `==== MAP ====` section of tiles and a `==== LOC ====`
 * section of scenery, both keyed `<level> <x> <z>:`.
 *
 * Two things this has to get right that the format does not make obvious.
 *
 * **Height comes from `authored_height`, never `height`.** The terrain decode
 * rewrites `height` for every tile — procedurally where the file gave none, scaled
 * where it did — so by the time anyone sees it there is nothing writable left.
 * `height_authored` is what separates "the file said nothing" from "the file said
 * this" (EXCEPTIONS.md B8).
 *
 * **A maps archive is five files, not two.** Terrain is file 0 and locs file 1,
 * but osrs239 squares also carry files 2, 3 and 4 that nothing here decodes. They
 * are written beside the `.jm2` as `.extra<N>.bin` and put back on import, because
 * a "readable map format" that silently dropped a third of the archive would be a
 * worse trade than no readable format at all.
 *
 * **The jm2 is shared with the server, and only two of its sections are ours.**
 * A square's `==== NPC ====` and `==== OBJ ====` spawns are content too, but they
 * live on the server and never enter the cache — LostCity keeps them in the same
 * file as the terrain, and so does this. So the codec owns MAP and LOC and treats
 * every other section as somebody else's: preserved verbatim when unpacking over
 * an existing file, skipped when packing. Without that, unpacking would delete
 * the server's spawns and packing would try to read `0 1 63: 1265` as a tile.
 */

#define CP_MAP_TERRAIN_FILE 0
#define CP_MAP_LOCS_FILE 1

/** MAP and LOC are the cache's; anything else in the file belongs to the server. */
static int
map_section_is_ours(const char* banner)
{
    return strstr(banner, "MAP") != NULL || strstr(banner, "LOC") != NULL;
}

/**
 * The sections of an existing `.jm2` this codec does not own, as one malloc'd
 * string (or NULL when there are none).
 *
 * Collected section by section rather than as one tail, so a file that interleaves
 * (`NPC`, `MAP`, `OBJ`) keeps its two foreign sections and does not smuggle a
 * second copy of MAP through. Leading comments before the first banner are *not*
 * carried: they describe the file as the tool is about to rewrite it.
 */
static char*
map_foreign_sections(const char* path)
{
    int size = 0;
    char* text = (char*)slurp(path, &size);
    if( !text )
        return NULL;

    char* kept = NULL;
    int kept_len = 0, kept_cap = 0;
    char* cursor = text;
    int keeping = 0;
    while( cursor && *cursor )
    {
        char* newline = strchr(cursor, '\n');
        int line_len = newline ? (int)(newline - cursor) + 1 : (int)strlen(cursor);
        if( cursor[0] == '=' )
            keeping = !map_section_is_ours(cursor);
        if( keeping )
        {
            if( kept_len + line_len + 1 > kept_cap )
            {
                int next = kept_cap ? kept_cap * 2 : 4096;
                while( next < kept_len + line_len + 1 )
                    next *= 2;
                char* grown = realloc(kept, (size_t)next);
                if( !grown )
                    break;
                kept = grown;
                kept_cap = next;
            }
            memcpy(kept + kept_len, cursor, (size_t)line_len);
            kept_len += line_len;
            kept[kept_len] = '\0';
        }
        cursor = newline ? newline + 1 : NULL;
    }
    free(text);
    return kept;
}

static int
map_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    if( file_count < 2 )
        return 0;
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode((char*)payload, size, file_count);
    if( !files || files->file_count < 2 )
    {
        RSCache_FileListFree(files);
        return 0;
    }

    int terrain_index = -1, locs_index = -1;
    for( int f = 0; f < files->file_count; f++ )
    {
        int id = file_ids ? file_ids[f] : f;
        if( id == CP_MAP_TERRAIN_FILE )
            terrain_index = f;
        else if( id == CP_MAP_LOCS_FILE )
            locs_index = f;
    }
    if( terrain_index < 0 || locs_index < 0 )
    {
        RSCache_FileListFree(files);
        return 0;
    }

    struct RSCache_MapTerrain* terrain = RSCache_MapTerrainNewFromDecodeFlags(
        files->files[terrain_index], files->file_sizes[terrain_index], 0, 0,
        RSCache_MapTerrainFlags(&ctx->profile) | RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP);
    struct RSCache_MapLocs* locs =
        RSCache_MapLocsNewDecode(files->files[locs_index], files->file_sizes[locs_index]);
    if( !terrain || !locs )
    {
        RSCache_MapTerrainFree(terrain);
        RSCache_MapLocsFree(locs);
        RSCache_FileListFree(files);
        return 0;
    }

    char path[1700];
    snprintf(path, sizeof(path), "%s.jm2", path_stem);
    /* Read before truncating: the spawns are in the file we are about to open "wb". */
    char* foreign = map_foreign_sections(path);
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        free(foreign);
        RSCache_MapTerrainFree(terrain);
        RSCache_MapLocsFree(locs);
        RSCache_FileListFree(files);
        return 0;
    }

    fprintf(out, "==== MAP ====\n");
    for( int level = 0; level < RSCACHE_MAP_TERRAIN_LEVELS; level++ )
    {
        for( int x = 0; x < RSCACHE_MAP_TERRAIN_X; x++ )
        {
            for( int z = 0; z < RSCACHE_MAP_TERRAIN_Z; z++ )
            {
                const struct RSCache_MapFloor* tile =
                    &terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level)];
                char tokens[96];
                int written = 0;
                if( tile->height_authored )
                    written += snprintf(tokens + written, sizeof(tokens) - (size_t)written,
                                        "h%d ", tile->authored_height);
                if( tile->attr_opcode != 0 )
                {
                    /* `shape` and `rotation` are `(attr-2)/4` and `(attr-2)&3`, so
                     * the three spell each other and LostCity's form is exact. */
                    written += snprintf(tokens + written, sizeof(tokens) - (size_t)written,
                                        "o%d;%d;%d ", tile->overlay_id, tile->shape,
                                        tile->rotation);
                }
                if( tile->settings != 0 )
                    written += snprintf(tokens + written, sizeof(tokens) - (size_t)written,
                                        "f%d ", tile->settings);
                if( tile->underlay_id != 0 )
                    written += snprintf(tokens + written, sizeof(tokens) - (size_t)written,
                                        "u%d ", tile->underlay_id);
                if( written > 0 )
                {
                    tokens[written - 1] = '\0'; /* drop the trailing space */
                    fprintf(out, "%d %d %d: %s\n", level, x, z, tokens);
                }
            }
        }
    }

    fprintf(out, "\n==== LOC ====\n");
    for( int i = 0; i < locs->locs_count; i++ )
    {
        const struct RSCache_MapLoc* loc = &locs->locs[i];
        if( loc->orientation == 0 )
            fprintf(out, "%d %d %d: %d %d\n", loc->chunk_pos_level, loc->chunk_pos_x,
                    loc->chunk_pos_z, loc->loc_id, loc->shape_select);
        else
            fprintf(out, "%d %d %d: %d %d %d\n", loc->chunk_pos_level, loc->chunk_pos_x,
                    loc->chunk_pos_z, loc->loc_id, loc->shape_select, loc->orientation);
    }
    if( foreign )
    {
        fprintf(out, "\n%s", foreign);
        free(foreign);
    }
    fclose(out);

    /* Everything the jm2 has no section for, kept verbatim. */
    for( int f = 0; f < files->file_count; f++ )
    {
        int id = file_ids ? file_ids[f] : f;
        if( id == CP_MAP_TERRAIN_FILE || id == CP_MAP_LOCS_FILE )
            continue;
        snprintf(path, sizeof(path), "%s.extra%d.bin", path_stem, id);
        FILE* extra = fopen(path, "wb");
        if( extra )
        {
            fwrite(files->files[f], 1, (size_t)files->file_sizes[f], extra);
            fclose(extra);
        }
    }

    RSCache_MapTerrainFree(terrain);
    RSCache_MapLocsFree(locs);
    RSCache_FileListFree(files);
    return 1;
}

static uint8_t*
map_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char path[1700];
    snprintf(path, sizeof(path), "%s.jm2", path_stem);
    int text_size = 0;
    char* text = (char*)slurp(path, &text_size);
    if( !text )
        return NULL;

    struct RSCache_MapTerrain* terrain = calloc(1, sizeof(*terrain));
    struct RSCache_MapLocs* locs = calloc(1, sizeof(*locs));
    int loc_capacity = 256;
    if( terrain && locs )
        locs->locs = malloc((size_t)loc_capacity * sizeof(*locs->locs));
    if( !terrain || !locs || !locs->locs )
    {
        free(text);
        free(terrain);
        if( locs )
            free(locs->locs);
        free(locs);
        return NULL;
    }

    /* Before the first banner nothing is claimed; a bare tile list would be a MAP
     * section without its header, which the exporter never writes. */
    enum
    {
        CP_JM2_NONE,
        CP_JM2_MAP,
        CP_JM2_LOC,
        CP_JM2_FOREIGN
    } section = CP_JM2_NONE;
    int seen_ours = 0;
    int ok = 1;
    char* cursor = text;
    while( cursor && *cursor && ok )
    {
        char* newline = strchr(cursor, '\n');
        if( newline )
            *newline = '\0';
        char* line = cursor;
        cursor = newline ? newline + 1 : NULL;

        while( *line == ' ' )
            line++;
        if( !*line || (line[0] == '/' && line[1] == '/') )
            continue;
        if( line[0] == '=' )
        {
            if( strstr(line, "LOC") )
                section = CP_JM2_LOC;
            else if( strstr(line, "MAP") )
                section = CP_JM2_MAP;
            else
                section = CP_JM2_FOREIGN;
            seen_ours |= section != CP_JM2_FOREIGN;
            continue;
        }
        /* A server-only section: its lines look like tile lines and are not. */
        if( section == CP_JM2_FOREIGN || section == CP_JM2_NONE )
            continue;

        int level = 0, x = 0, z = 0;
        char* colon = strchr(line, ':');
        if( !colon || sscanf(line, "%d %d %d", &level, &x, &z) != 3 )
        {
            ok = 0;
            break;
        }
        if( level < 0 || level >= RSCACHE_MAP_TERRAIN_LEVELS || x < 0 ||
            x >= RSCACHE_MAP_TERRAIN_X || z < 0 || z >= RSCACHE_MAP_TERRAIN_Z )
        {
            ok = 0;
            break;
        }
        char* rest = colon + 1;

        if( section == CP_JM2_LOC )
        {
            int loc_id = 0, shape = 0, angle = 0;
            int fields = sscanf(rest, "%d %d %d", &loc_id, &shape, &angle);
            if( fields < 2 )
            {
                ok = 0;
                break;
            }
            if( locs->locs_count == loc_capacity )
            {
                int next = loc_capacity * 2;
                struct RSCache_MapLoc* grown =
                    realloc(locs->locs, (size_t)next * sizeof(*grown));
                if( !grown )
                {
                    ok = 0;
                    break;
                }
                locs->locs = grown;
                loc_capacity = next;
            }
            struct RSCache_MapLoc* loc = &locs->locs[locs->locs_count++];
            loc->loc_id = loc_id;
            loc->shape_select = shape;
            loc->orientation = fields >= 3 ? angle : 0;
            loc->chunk_pos_x = x;
            loc->chunk_pos_z = z;
            loc->chunk_pos_level = level;
            continue;
        }

        struct RSCache_MapFloor* tile =
            &terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level)];
        char* token = strtok(rest, " \t");
        while( token && ok )
        {
            int a = 0, b = 0, c = 0;
            switch( token[0] )
            {
            case 'h':
                tile->height_authored = 1;
                tile->authored_height = (uint8_t)atoi(token + 1);
                break;
            case 'o':
                if( sscanf(token + 1, "%d;%d;%d", &a, &b, &c) != 3 )
                    ok = 0;
                else
                {
                    tile->overlay_id = (uint16_t)a;
                    tile->shape = (uint8_t)b;
                    tile->rotation = (uint8_t)c;
                    /* The opcode is what the encoder writes; shape and rotation
                     * are two views of it, so it is rebuilt rather than stored. */
                    tile->attr_opcode = (uint8_t)(b * 4 + c + 2);
                }
                break;
            case 'f':
                tile->settings = (uint8_t)atoi(token + 1);
                break;
            case 'u':
                tile->underlay_id = (uint8_t)atoi(token + 1);
                break;
            default:
                ok = 0;
                break;
            }
            token = strtok(NULL, " \t");
        }
    }
    free(text);

    /* A jm2 the server authored for a square the cache has no terrain for is all
     * spawns. Encoding it would put a blank square into the cache, which reads as
     * a black hole in the world rather than as the absence it is. */
    if( !seen_ours )
        ok = 0;

    uint8_t* payload = NULL;
    if( ok )
    {
        int flags = RSCache_MapTerrainFlags(&ctx->profile);
        uint32_t terrain_bound = RSCache_MapTerrainEncodeBound(flags);
        uint8_t* terrain_bytes = malloc(terrain_bound);
        uint32_t terrain_size =
            terrain_bytes ? RSCache_MapTerrainEncode(terrain, flags, terrain_bytes, terrain_bound)
                          : 0;

        uint32_t locs_bound = RSCache_MapLocsEncodeBound(locs);
        uint8_t* locs_bytes = malloc(locs_bound);
        uint32_t locs_size =
            locs_bytes ? RSCache_MapLocsEncode(locs, locs_bytes, locs_bound) : 0;

        if( terrain_size && locs_size )
        {
            /* Rebuild the whole archive: terrain, locs, then whatever extras the
             * export set aside, each back under its own id. */
            struct RSCache_FileList list;
            memset(&list, 0, sizeof(list));
            /* Most squares are five files, but the maps table also holds one
             * 37-file archive, so the extras are counted rather than assumed. */
            int capacity = 2;
            for( int extra = 2; extra < 256; extra++ )
            {
                snprintf(path, sizeof(path), "%s.extra%d.bin", path_stem, extra);
                struct stat probe;
                if( stat(path, &probe) == 0 )
                    capacity = extra + 1;
            }
            list.files = calloc((size_t)capacity, sizeof(char*));
            list.file_sizes = calloc((size_t)capacity, sizeof(int));
            int* ids = calloc((size_t)capacity, sizeof(int));
            if( list.files && list.file_sizes && ids )
            {
                list.files[0] = (char*)terrain_bytes;
                list.file_sizes[0] = (int)terrain_size;
                ids[0] = CP_MAP_TERRAIN_FILE;
                list.files[1] = (char*)locs_bytes;
                list.file_sizes[1] = (int)locs_size;
                ids[1] = CP_MAP_LOCS_FILE;
                list.file_count = 2;

                for( int extra = 2; extra < capacity; extra++ )
                {
                    snprintf(path, sizeof(path), "%s.extra%d.bin", path_stem, extra);
                    uint8_t* bytes = NULL;
                    int bytes_size = 0;
                    if( !read_file_bytes(path, &bytes, &bytes_size) )
                        continue; /* a gap in the ids is legal and skipped */
                    list.files[list.file_count] = (char*)bytes;
                    list.file_sizes[list.file_count] = bytes_size;
                    ids[list.file_count] = extra;
                    list.file_count++;
                }

                uint32_t bound = RSCache_FileListEncodeBound(&list);
                payload = malloc(bound ? bound : 1);
                if( payload )
                {
                    uint32_t written = RSCache_FileListEncode(&list, payload, bound);
                    if( written == 0 )
                    {
                        free(payload);
                        payload = NULL;
                    }
                    else
                    {
                        *out_size = (int)written;
                        *out_file_ids = ids;
                        *out_file_count = list.file_count;
                        ids = NULL;
                    }
                }
                /* The two encoders' buffers are owned by the list entries. */
                for( int i = 2; i < list.file_count; i++ )
                    free(list.files[i]);
            }
            free(list.files);
            free(list.file_sizes);
            free(ids);
        }
        free(terrain_bytes);
        free(locs_bytes);
    }

    RSCache_MapTerrainFree(terrain);
    RSCache_MapLocsFree(locs);
    return payload;
}

const struct CP_AssetCodec cp_codec_map = { "jm2", map_write, map_read };

/* ---- clientscripts ------------------------------------------------------- */

/*
 * CS2 source, through the decompiler and compiler the library already has.
 *
 * Nothing is reimplemented here: `RSCache_CS2_Decompile` and
 * `RSCache_CS2_Compile` are the same pair `tools/cs2` drives, and they already
 * reach a source fixed point on the corpus (EXCEPTIONS.md G2). What this adds is
 * the plumbing they need from an open cache.
 *
 * Two things they need that a bare payload cannot supply:
 *
 *  - **Other scripts.** A `~proc` call is typed by its callee's signature, so
 *    decompiling one script means being able to load any other. The source below
 *    reads table 12 on demand and caches what it decoded.
 *  - **Param types.** `oc_param` pushes an int or a string depending on the param
 *    config, which is a stack-shape question rather than a cosmetic one. The types
 *    are loaded once from the cache's param group.
 *
 * A script that will not decompile is declined rather than approximated — 37 of
 * osrs239's do — and the caller writes the raw `.cs2` bytecode instead. That is
 * why the fallback exists.
 */

struct cp_cs2_state
{
    struct CP_Ctx* ctx;
    int table;
    /** id -> decoded script, filled lazily; NULL means "tried and failed". */
    struct RSCache_ClientScript** scripts;
    uint8_t* attempted;
    int capacity;
    struct RSCache_CS2_Names names;
    int names_loaded;
};

static struct cp_cs2_state g_cs2;

static const struct RSCache_CS2_Script*
cs2_load_script(
    void* user,
    int script_id)
{
    struct cp_cs2_state* state = user;
    if( script_id < 0 || script_id >= state->capacity )
        return NULL;
    if( !state->attempted[script_id] )
    {
        state->attempted[script_id] = 1;
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(state->ctx->cache.disk, state->table, script_id);
        if( archive )
        {
            state->scripts[script_id] = RSCache_ClientScriptNewFromDecodeFlags(
                script_id, (const uint8_t*)archive->data, archive->data_size,
                RSCache_ClientScriptFlags(&state->ctx->profile));
            RSCache_Dat2DiskArchiveFree(archive);
        }
    }
    return state->scripts[script_id] ? &state->scripts[script_id]->script : NULL;
}

static enum RSCache_CS2_Type
cs2_load_param_type(
    void* user,
    int param_id)
{
    struct cp_cs2_state* state = user;
    return RSCache_CS2_NamesParamType(&state->names, param_id);
}

/** Load every param's value type once; the decompiler asks per call site. */
static void
cs2_load_param_types(struct cp_cs2_state* state)
{
    struct CP_Group group;
    if( !cp_group_open(state->ctx, CP_TYPE_PARAM, &group) )
        return;
    for( int i = 0; i < group.count; i++ )
    {
        int id = group.ids ? group.ids[i] : i;
        int size = 0;
        const uint8_t* record = cp_group_record(&group, i, &size);
        if( !record )
            continue;
        struct RSCache_Dat2ConfigParam param;
        memset(&param, 0, sizeof(param));
        RSCache_Dat2ConfigParamDecodeInplace(&param, record, size);
        /* The config stores the script var-type *character*; the decompiler wants
         * the enum. Passing the char straight through mistypes every param, and
         * the symptom is not a wrong name — it is a stack-shape mismatch that
         * fails the decompile of every script touching a param. */
        enum RSCache_CS2_Type type = RSCache_CS2_TypeOfDescAuto((uint8_t)param.type);
        if( type != RSCACHE_CS2_TYPE_NONE )
            RSCache_CS2_NamesSetParamType(&state->names, id, type);
        RSCache_Dat2ConfigParamFreeInplace(&param);
    }
    cp_group_free(&group);
}

static int
cs2_state_ready(struct CP_Ctx* ctx)
{
    if( g_cs2.ctx == ctx )
        return 1;
    memset(&g_cs2, 0, sizeof(g_cs2));
    g_cs2.ctx = ctx;
    g_cs2.table = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_CLIENTSCRIPT);
    if( g_cs2.table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return 0;
    struct RSCache_ReferenceTable* rt = ctx->cache.disk->tables[g_cs2.table];
    g_cs2.capacity = rt ? rt->archive_count : 0;
    if( g_cs2.capacity <= 0 )
        return 0;
    g_cs2.scripts = calloc((size_t)g_cs2.capacity, sizeof(*g_cs2.scripts));
    g_cs2.attempted = calloc((size_t)g_cs2.capacity, 1);
    if( !g_cs2.scripts || !g_cs2.attempted )
        return 0;
    RSCache_CS2_NamesInit(&g_cs2.names);
    /* RuneStar's name tables are optional and not vendored (EXCEPTIONS.md G5);
     * without them a decompile is still correct, just `obj_995` rather than
     * `coins_995`. `CACHEPACK_CS2_NAMES` points at them when they are to hand. */
    const char* names_dir = getenv("CACHEPACK_CS2_NAMES");
    if( names_dir )
        RSCache_CS2_NamesLoadDirectory(&g_cs2.names, names_dir);
    cs2_load_param_types(&g_cs2);
    g_cs2.names_loaded = 1;
    return 1;
}

static int
script_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    if( !cs2_state_ready(ctx) )
        return 0;

    struct RSCache_CS2_DecompileOptions options;
    memset(&options, 0, sizeof(options));
    options.scripts.user = &g_cs2;
    options.scripts.load = cs2_load_script;
    options.param_types.user = &g_cs2;
    options.param_types.load = cs2_load_param_type;
    options.names = g_cs2.names_loaded ? &g_cs2.names : NULL;

    char error[512] = "";
    char* name = NULL;
    char* source = RSCache_CS2_Decompile(record_id, &options, &name, error, sizeof(error));
    if( !source )
    {
        free(name);
        return 0; /* declined: the caller writes the bytecode instead */
    }

    char path[1700];
    snprintf(path, sizeof(path), "%s.cs2", path_stem);
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        free(source);
        free(name);
        return 0;
    }
    fputs(source, out);
    fclose(out);
    free(source);
    free(name);
    return 1;
}

static uint8_t*
script_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char path[1700];
    snprintf(path, sizeof(path), "%s.cs2", path_stem);
    int source_size = 0;
    char* source = (char*)slurp(path, &source_size);
    if( !source )
        return NULL;
    if( !cs2_state_ready(ctx) )
    {
        free(source);
        return NULL;
    }

    struct RSCache_CS2_CompileOptions options;
    memset(&options, 0, sizeof(options));
    options.scripts.user = &g_cs2;
    options.scripts.load = cs2_load_script;
    options.param_types.user = &g_cs2;
    options.param_types.load = cs2_load_param_type;
    options.names = g_cs2.names_loaded ? &g_cs2.names : NULL;

    struct RSCache_ClientScript script;
    memset(&script, 0, sizeof(script));
    char error[512] = "";
    bool ok = RSCache_CS2_Compile(source, &options, &script, error, sizeof(error));
    free(source);
    if( !ok )
    {
        fprintf(stderr, "cachepack: script %d: %s\n", record_id, error);
        return NULL;
    }

    uint32_t bound = RSCache_ClientScriptEncodeBound(&script);
    uint8_t* payload = malloc(bound ? bound : 1);
    uint32_t written =
        payload ? RSCache_ClientScriptEncode(&ctx->profile, &script, payload, bound) : 0;
    /* `RSCache_CS2_Compile` fills a script that owns its buffers, exactly like a
     * decoded one, so it is released the same way. */
    RSCache_ClientScriptFree(&script);
    if( written == 0 )
    {
        free(payload);
        return NULL;
    }
    *out_size = (int)written;
    return payload;
}

const struct CP_AssetCodec cp_codec_script = { "cs2", script_write, script_read };

/* ---- sprites ------------------------------------------------------------- */

/*
 * A sprite pack becomes a directory of BMPs plus one `pack.meta`.
 *
 * A pack holds several sprites over a shared palette, and each sprite carries
 * geometry a bitmap has no room for — the size it occupies in memory against the
 * size it was stored at, and where the stored part sits inside it. So the image
 * goes in the BMP and everything else in the sidecar.
 *
 * **The palette is written out and read back, not re-derived.** Two entries can
 * hold the same colour, so RGB does not determine the index; going back through
 * the recorded palette keeps the exact indices the cache had. A colour the
 * palette does not contain (someone edited the BMP) takes the nearest entry, and
 * says so.
 *
 * Alpha rides in the BMP: `bmp_write_file` writes 32-bit BGRA, so the sprite's
 * own alpha plane survives without a second sidecar.
 *
 * Decode with RSCACHE_SPRITELOAD_FLAG_NONE — normalising rewrites the pack in
 * place, turning crop sizes into memory sizes and zeroing the offsets, so an
 * encode of a normalised pack is a valid pack of different sprites
 * (EXCEPTIONS.md B3b).
 */

static int
sprite_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    struct RSCache_Dat2SpritePack* pack =
        RSCache_Dat2SpritePackNewDecode(payload, size, RSCACHE_SPRITELOAD_FLAG_NONE);
    if( !pack || pack->count <= 0 )
    {
        RSCache_Dat2SpritePackFree(pack);
        return 0;
    }

    if( ensure_dir_path(path_stem) != 0 )
    {
        RSCache_Dat2SpritePackFree(pack);
        return 0;
    }

    char path[1800];
    snprintf(path, sizeof(path), "%s/pack.meta", path_stem);
    FILE* meta = fopen(path, "wb");
    if( !meta )
    {
        RSCache_Dat2SpritePackFree(pack);
        return 0;
    }
    fprintf(meta, "// Sprite pack %d — %d sprites over a shared palette.\n", record_id,
            pack->count);
    fprintf(meta, "// The BMPs carry the pixels; these are what a bitmap cannot hold.\n");
    fprintf(meta, "count=%d\n", pack->count);
    fprintf(meta, "palette=%d\n", pack->palette_length);
    for( int i = 0; i < pack->palette_length; i++ )
        fprintf(meta, "p%d=0x%06X\n", i, pack->palette[i] & 0xFFFFFF);

    int ok = 1;
    for( int i = 0; i < pack->count && ok; i++ )
    {
        const struct RSCache_Dat2Sprite* sprite = &pack->sprites[i];
        fprintf(meta, "sprite%d=%d,%d,%d,%d,%d,%d\n", i, sprite->width, sprite->height,
                sprite->crop_width, sprite->crop_height, sprite->offset_x, sprite->offset_y);

        int pixels_count = sprite->crop_width * sprite->crop_height;
        if( pixels_count <= 0 )
            continue;
        int* pixels = malloc((size_t)pixels_count * sizeof(int));
        if( !pixels )
        {
            ok = 0;
            break;
        }
        for( int p = 0; p < pixels_count; p++ )
        {
            int index = sprite->palette_pixels[p];
            int rgb = (index >= 0 && index < pack->palette_length) ? pack->palette[index] : 0;
            int alpha = sprite->pixel_alphas ? sprite->pixel_alphas[p] : (index != 0 ? 255 : 0);
            pixels[p] = (alpha << 24) | (rgb & 0xFFFFFF);
        }
        snprintf(path, sizeof(path), "%s/%d.bmp", path_stem, i);
        bmp_write_file(path, pixels, sprite->crop_width, sprite->crop_height);
        free(pixels);
    }
    fclose(meta);
    RSCache_Dat2SpritePackFree(pack);
    return ok;
}

/** Read back what `bmp_write_file` wrote: 32-bit BGRA, bottom-up, 54-byte header. */
static int*
bmp_read_file(
    const char* path,
    int* out_width,
    int* out_height)
{
    int size = 0;
    uint8_t* data = slurp(path, &size);
    if( !data || size < 54 || data[0] != 'B' || data[1] != 'M' )
    {
        free(data);
        return NULL;
    }
    int offset = (int)(data[10] | (data[11] << 8) | (data[12] << 16) | (data[13] << 24));
    int width = (int)(data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24));
    int height = (int)(data[22] | (data[23] << 8) | (data[24] << 16) | (data[25] << 24));
    int bpp = data[28] | (data[29] << 8);
    if( bpp != 32 || width <= 0 || height <= 0 || offset + width * height * 4 > size )
    {
        free(data);
        return NULL;
    }
    int* pixels = malloc((size_t)width * (size_t)height * sizeof(int));
    if( !pixels )
    {
        free(data);
        return NULL;
    }
    const uint8_t* src = data + offset;
    for( int y = height - 1; y >= 0; y-- )
    {
        for( int x = 0; x < width; x++ )
        {
            pixels[y * width + x] = (src[3] << 24) | (src[2] << 16) | (src[1] << 8) | src[0];
            src += 4;
        }
    }
    free(data);
    *out_width = width;
    *out_height = height;
    return pixels;
}

static uint8_t*
sprite_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char path[1800];
    snprintf(path, sizeof(path), "%s/pack.meta", path_stem);
    struct CP_ConfigFile meta;
    int meta_size = 0;
    char* text = (char*)slurp(path, &meta_size);
    if( !text )
        return NULL;
    char* wrapped = malloc((size_t)meta_size + 16);
    if( !wrapped )
    {
        free(text);
        return NULL;
    }
    int prefix = snprintf(wrapped, 16, "[pack]\n");
    memcpy(wrapped + prefix, text, (size_t)meta_size);
    free(text);
    int parsed =
        cp_config_file_load_memory(&meta, wrapped, (size_t)(prefix + meta_size), "pack.meta");
    free(wrapped);
    if( !parsed || meta.count != 1 )
    {
        if( parsed )
            cp_config_file_free(&meta);
        return NULL;
    }

    struct RSCache_Dat2SpritePack pack;
    memset(&pack, 0, sizeof(pack));
    struct CP_IntList palette = { 0 };
    struct CP_IntList geometry = { 0 };
    int declared_count = 0;
    int ok = 1;

    for( int i = 0; i < meta.configs[0].count && ok; i++ )
    {
        const char* key = meta.configs[0].lines[i].key;
        const char* value = meta.configs[0].lines[i].value;
        if( strcmp(key, "count") == 0 )
            ok = cp_parse_int(value, &declared_count);
        else if( strcmp(key, "palette") == 0 )
            continue; /* the entries themselves carry the length */
        else if( key[0] == 'p' && key[1] >= '0' && key[1] <= '9' )
        {
            int index = atoi(key + 1);
            int colour = 0;
            ok = cp_parse_int(value, &colour);
            cp_intlist_set(&palette, index, colour);
        }
        else if( strncmp(key, "sprite", 6) == 0 )
        {
            int index = atoi(key + 6);
            char scratch[128];
            char* fields[6];
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 6) != 6 )
            {
                ok = 0;
                break;
            }
            for( int f = 0; f < 6; f++ )
            {
                int parsed_value = 0;
                ok = ok && cp_parse_int(fields[f], &parsed_value);
                cp_intlist_set(&geometry, index * 6 + f, parsed_value);
            }
        }
    }
    cp_config_file_free(&meta);

    uint8_t* payload = NULL;
    if( ok && declared_count > 0 )
    {
        pack.count = declared_count;
        pack.palette_length = palette.count;
        pack.palette = palette.items;
        pack.sprites = calloc((size_t)declared_count, sizeof(*pack.sprites));
        ok = pack.sprites != NULL;

        for( int i = 0; i < declared_count && ok; i++ )
        {
            struct RSCache_Dat2Sprite* sprite = &pack.sprites[i];
            sprite->width = geometry.items[i * 6 + 0];
            sprite->height = geometry.items[i * 6 + 1];
            sprite->crop_width = geometry.items[i * 6 + 2];
            sprite->crop_height = geometry.items[i * 6 + 3];
            sprite->offset_x = geometry.items[i * 6 + 4];
            sprite->offset_y = geometry.items[i * 6 + 5];

            int pixels_count = sprite->crop_width * sprite->crop_height;
            if( pixels_count <= 0 )
                continue;
            snprintf(path, sizeof(path), "%s/%d.bmp", path_stem, i);
            int bmp_width = 0, bmp_height = 0;
            int* pixels = bmp_read_file(path, &bmp_width, &bmp_height);
            if( !pixels || bmp_width != sprite->crop_width || bmp_height != sprite->crop_height )
            {
                free(pixels);
                ok = 0;
                break;
            }
            sprite->palette_pixels = malloc((size_t)pixels_count);
            sprite->pixel_alphas = malloc((size_t)pixels_count);
            if( !sprite->palette_pixels || !sprite->pixel_alphas )
            {
                free(pixels);
                ok = 0;
                break;
            }
            for( int p = 0; p < pixels_count; p++ )
            {
                int rgb = pixels[p] & 0xFFFFFF;
                int alpha = (pixels[p] >> 24) & 0xFF;
                /* Index 0 is the pack's transparent slot; a fully clear pixel is
                 * that slot regardless of what colour it happens to carry. */
                int index = 0;
                if( alpha != 0 || rgb != 0 )
                {
                    int best = 0, best_error = 1 << 30;
                    for( int c = 0; c < pack.palette_length; c++ )
                    {
                        int diff_r = ((pack.palette[c] >> 16) & 0xFF) - ((rgb >> 16) & 0xFF);
                        int diff_g = ((pack.palette[c] >> 8) & 0xFF) - ((rgb >> 8) & 0xFF);
                        int diff_b = (pack.palette[c] & 0xFF) - (rgb & 0xFF);
                        int error = diff_r * diff_r + diff_g * diff_g + diff_b * diff_b;
                        if( error < best_error )
                        {
                            best_error = error;
                            best = c;
                            if( error == 0 )
                                break;
                        }
                    }
                    index = best;
                }
                sprite->palette_pixels[p] = (uint8_t)index;
                sprite->pixel_alphas[p] = (uint8_t)alpha;
            }
            free(pixels);
        }

        if( ok )
        {
            uint32_t bound = RSCache_Dat2SpritePackEncodeBound(&pack);
            payload = malloc(bound ? bound : 1);
            uint32_t written =
                payload ? RSCache_Dat2SpritePackEncode(&pack, payload, bound) : 0;
            if( written == 0 )
            {
                free(payload);
                payload = NULL;
            }
            else
            {
                *out_size = (int)written;
            }
        }
    }

    if( pack.sprites )
    {
        for( int i = 0; i < pack.count; i++ )
        {
            free(pack.sprites[i].palette_pixels);
            free(pack.sprites[i].pixel_alphas);
        }
        free(pack.sprites);
    }
    cp_intlist_free(&palette);
    cp_intlist_free(&geometry);
    return payload;
}

const struct CP_AssetCodec cp_codec_sprite = { "bmp", sprite_write, sprite_read };

/* ---- world map ----------------------------------------------------------- */

/*
 * The world map table does not label what is in it. Archive 0 holds one area
 * record per file ("main" / "Gielinor Surface"), archive 1 the matching
 * compositemaps, archive 2 the rendered PNGs, and 51 further archives hold a
 * single file each. Nothing in the container says so.
 *
 * So this does not hardcode the layout. It tries a decode and then **re-encodes
 * and compares**: a decode that reproduces its own input byte for byte was the
 * right decode, and one that does not is declined. That is the same evidence the
 * round-trip harnesses use, applied per record instead of per corpus, and it
 * means a cache that arranges the table differently still comes out right.
 *
 * PNGs need no help — the payload sniffing already gives them `.png`, which is
 * what makes the map images viewable.
 */

static int
worldmap_try_area(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    struct RSCache_WorldMapArea* out)
{
    memset(out, 0, sizeof(*out));
    if( !RSCache_WorldMapAreaDecodeInplace(out, record_id, payload, size) )
    {
        RSCache_WorldMapAreaFreeInplace(out);
        return 0;
    }
    uint32_t bound = RSCache_WorldMapAreaEncodeBound(out);
    uint8_t* check = malloc(bound ? bound : 1);
    uint32_t written = check ? RSCache_WorldMapAreaEncode(out, check, bound) : 0;
    int exact = written == (uint32_t)size && memcmp(check, payload, (size_t)size) == 0;
    free(check);
    if( !exact )
        RSCache_WorldMapAreaFreeInplace(out);
    return exact;
}

static int
worldmap_try_composite(
    struct CP_Ctx* ctx,
    const uint8_t* payload,
    int size,
    struct RSCache_WorldMapArea* out)
{
    memset(out, 0, sizeof(*out));
    int flags = RSCache_WorldMapFlags(&ctx->profile);
    RSCache_WorldMapAreaDecodeIconsInplace(out, payload, size, flags);
    if( out->region_count == 0 && out->icon_count == 0 )
    {
        RSCache_WorldMapAreaFreeInplace(out);
        return 0;
    }
    uint32_t bound = RSCache_WorldMapAreaEncodeIconsBound(out);
    uint8_t* check = malloc(bound ? bound : 1);
    uint32_t written = check ? RSCache_WorldMapAreaEncodeIcons(out, flags, check, bound) : 0;
    int exact = written == (uint32_t)size && memcmp(check, payload, (size_t)size) == 0;
    free(check);
    if( !exact )
        RSCache_WorldMapAreaFreeInplace(out);
    return exact;
}

static int
worldmap_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    char path[1800];
    struct RSCache_WorldMapArea area;

    if( worldmap_try_area(ctx, record_id, payload, size, &area) )
    {
        struct CP_Lines lines;
        cp_lines_init(&lines);
        cp_lines_add_str(&lines, "internal", area.internal_name);
        cp_lines_add_str(&lines, "external", area.external_name);
        cp_lines_addf(&lines, "origin=%d", area.origin);
        /* All eight digits: the cache's background is 0xFF000000, and a %06X here
         * would write the alpha away and re-encode a different colour. */
        cp_lines_addf(&lines, "background=0x%08X", (unsigned)area.background_colour);
        cp_lines_addf(&lines, "main=%s", area.is_main ? "yes" : "no");
        cp_lines_addf(&lines, "zoom=%d", area.zoom);
        /* Two fields the client never reads; kept so the record re-encodes. */
        cp_lines_addf(&lines, "unknown_int=%d", area.unknown_int);
        cp_lines_addf(&lines, "unknown_byte=%d", area.unknown_byte);
        for( int i = 0; i < area.section_count; i++ )
        {
            const struct RSCache_WorldMapSection* section = &area.sections[i];
            cp_lines_addf(&lines,
                          "section%d=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                          i + 1, section->type, section->min_plane, section->planes,
                          section->src_region_x, section->src_region_y,
                          section->src_region_x_end, section->src_region_y_end,
                          section->src_chunk_x_low, section->src_chunk_y_low,
                          section->src_chunk_x_high, section->src_chunk_y_high,
                          section->dst_region_x, section->dst_region_y,
                          section->dst_region_x_end, section->dst_region_y_end,
                          section->dst_chunk_x_low, section->dst_chunk_y_low,
                          section->dst_chunk_x_high);
            cp_lines_addf(&lines, "section%d_dst_chunk_y_high=%d", i + 1,
                          section->dst_chunk_y_high);
        }
        snprintf(path, sizeof(path), "%s.wma", path_stem);
        int ok = write_text_file(path, &lines,
                                 "// World map area. `section<N>` fields are, in order:\n"
                                 "// type, min_plane, planes, then the source side and the\n"
                                 "// destination side; a section only uses the ones its type\n"
                                 "// declares (see RSCACHE_WORLDMAP_SECTION_*).\n");
        cp_lines_free(&lines);
        RSCache_WorldMapAreaFreeInplace(&area);
        return ok;
    }

    if( worldmap_try_composite(ctx, payload, size, &area) )
    {
        struct CP_Lines lines;
        cp_lines_init(&lines);
        cp_lines_addf(&lines, "data0=%d", area.data0_count);
        for( int i = 0; i < area.region_count; i++ )
        {
            const struct RSCache_WorldMapRegion* region = &area.regions[i];
            cp_lines_addf(&lines, "region%d=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", i + 1,
                          region->kind, region->min_plane, region->planes,
                          region->src_region_x, region->src_region_y, region->src_chunk_x,
                          region->src_chunk_y, region->dst_region_x, region->dst_region_y,
                          region->dst_chunk_x, region->dst_chunk_y, region->group_id,
                          region->file_id);
        }
        for( int i = 0; i < area.icon_count; i++ )
            cp_lines_addf(&lines, "icon%d=%d,%d,%s", i + 1, area.icons[i].element,
                          area.icons[i].coord, area.icons[i].hidden ? "yes" : "no");
        snprintf(path, sizeof(path), "%s.wmc", path_stem);
        int ok = write_text_file(
            &path[0], &lines,
            "// World map composite. `data0` is where the first region block ends —\n"
            "// the two blocks are stored back to back and the split is not derivable.\n"
            "// region: kind,min_plane,planes,src_rx,src_ry,src_cx,src_cy,dst_rx,dst_ry,\n"
            "//         dst_cx,dst_cy,group,file\n"
            "// icon:   element,coord,hidden\n");
        cp_lines_free(&lines);
        RSCache_WorldMapAreaFreeInplace(&area);
        return ok;
    }

    return 0; /* PNGs and the rest keep their payload */
}

static uint8_t*
worldmap_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char path[1800];
    int is_composite = 0;
    snprintf(path, sizeof(path), "%s.wma", path_stem);
    int text_size = 0;
    uint8_t* text = slurp(path, &text_size);
    if( !text )
    {
        snprintf(path, sizeof(path), "%s.wmc", path_stem);
        text = slurp(path, &text_size);
        is_composite = 1;
    }
    if( !text )
        return NULL;

    char* wrapped = malloc((size_t)text_size + 16);
    if( !wrapped )
    {
        free(text);
        return NULL;
    }
    int prefix = snprintf(wrapped, 16, "[wm]\n");
    memcpy(wrapped + prefix, text, (size_t)text_size);
    free(text);
    struct CP_ConfigFile file;
    int parsed =
        cp_config_file_load_memory(&file, wrapped, (size_t)(prefix + text_size), "worldmap");
    free(wrapped);
    if( !parsed || file.count != 1 )
    {
        if( parsed )
            cp_config_file_free(&file);
        return NULL;
    }

    struct RSCache_WorldMapArea area;
    memset(&area, 0, sizeof(area));
    area.id = record_id;
    int section_capacity = 0, region_capacity = 0, icon_capacity = 0;
    int ok = 1;

    for( int i = 0; i < file.configs[0].count && ok; i++ )
    {
        const char* key = file.configs[0].lines[i].key;
        const char* value = file.configs[0].lines[i].value;
        char scratch[512];
        char* fields[20];
        int index;

        if( strcmp(key, "internal") == 0 )
        {
            char buf[512];
            cp_unescape(value, buf, sizeof(buf));
            area.internal_name = strdup(buf);
        }
        else if( strcmp(key, "external") == 0 )
        {
            char buf[512];
            cp_unescape(value, buf, sizeof(buf));
            area.external_name = strdup(buf);
        }
        else if( strcmp(key, "origin") == 0 )
            ok = cp_parse_int(value, &area.origin);
        else if( strcmp(key, "background") == 0 )
        {
            /* 0xFF000000 does not fit a signed int, so it goes through the wider
             * parse and lands as the same 32 bits the cache holds. */
            int64_t colour = 0;
            ok = cp_parse_i64(value, &colour);
            area.background_colour = (int)(uint32_t)colour;
        }
        else if( strcmp(key, "main") == 0 )
            ok = cp_parse_bool(value, &area.is_main);
        else if( strcmp(key, "zoom") == 0 )
            ok = cp_parse_int(value, &area.zoom);
        else if( strcmp(key, "unknown_int") == 0 )
            ok = cp_parse_int(value, &area.unknown_int);
        else if( strcmp(key, "unknown_byte") == 0 )
            ok = cp_parse_int(value, &area.unknown_byte);
        else if( strcmp(key, "data0") == 0 )
            ok = cp_parse_int(value, &area.data0_count);
        else if( strstr(key, "_dst_chunk_y_high") )
        {
            index = atoi(key + 7) - 1;
            if( index >= 0 && index < area.section_count )
                ok = cp_parse_int(value, &area.sections[index].dst_chunk_y_high);
        }
        else if( (index = cp_indexed_key(key, "section")) >= 0 )
        {
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 18) != 18 )
            {
                ok = 0;
                break;
            }
            if( index >= section_capacity )
            {
                int next = index + 8;
                struct RSCache_WorldMapSection* grown =
                    realloc(area.sections, (size_t)next * sizeof(*grown));
                if( !grown )
                {
                    ok = 0;
                    break;
                }
                memset(grown + section_capacity, 0,
                       (size_t)(next - section_capacity) * sizeof(*grown));
                area.sections = grown;
                section_capacity = next;
            }
            struct RSCache_WorldMapSection* s = &area.sections[index];
            int* slots[18] = { &s->type,
                               &s->min_plane,
                               &s->planes,
                               &s->src_region_x,
                               &s->src_region_y,
                               &s->src_region_x_end,
                               &s->src_region_y_end,
                               &s->src_chunk_x_low,
                               &s->src_chunk_y_low,
                               &s->src_chunk_x_high,
                               &s->src_chunk_y_high,
                               &s->dst_region_x,
                               &s->dst_region_y,
                               &s->dst_region_x_end,
                               &s->dst_region_y_end,
                               &s->dst_chunk_x_low,
                               &s->dst_chunk_y_low,
                               &s->dst_chunk_x_high };
            for( int f = 0; f < 18 && ok; f++ )
                ok = cp_parse_int(fields[f], slots[f]);
            if( index + 1 > area.section_count )
                area.section_count = index + 1;
        }
        else if( (index = cp_indexed_key(key, "region")) >= 0 )
        {
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 13) != 13 )
            {
                ok = 0;
                break;
            }
            if( index >= region_capacity )
            {
                int next = index + 16;
                struct RSCache_WorldMapRegion* grown =
                    realloc(area.regions, (size_t)next * sizeof(*grown));
                if( !grown )
                {
                    ok = 0;
                    break;
                }
                memset(grown + region_capacity, 0,
                       (size_t)(next - region_capacity) * sizeof(*grown));
                area.regions = grown;
                region_capacity = next;
            }
            struct RSCache_WorldMapRegion* r = &area.regions[index];
            int* slots[13] = { &r->kind,         &r->min_plane,    &r->planes,
                               &r->src_region_x, &r->src_region_y, &r->src_chunk_x,
                               &r->src_chunk_y,  &r->dst_region_x, &r->dst_region_y,
                               &r->dst_chunk_x,  &r->dst_chunk_y,  &r->group_id,
                               &r->file_id };
            for( int f = 0; f < 13 && ok; f++ )
                ok = cp_parse_int(fields[f], slots[f]);
            if( index + 1 > area.region_count )
                area.region_count = index + 1;
        }
        else if( (index = cp_indexed_key(key, "icon")) >= 0 )
        {
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 3) != 3 )
            {
                ok = 0;
                break;
            }
            if( index >= icon_capacity )
            {
                int next = index + 32;
                struct RSCache_WorldMapIcon* grown =
                    realloc(area.icons, (size_t)next * sizeof(*grown));
                if( !grown )
                {
                    ok = 0;
                    break;
                }
                memset(grown + icon_capacity, 0,
                       (size_t)(next - icon_capacity) * sizeof(*grown));
                area.icons = grown;
                icon_capacity = next;
            }
            ok = cp_parse_int(fields[0], &area.icons[index].element) &&
                 cp_parse_int(fields[1], &area.icons[index].coord) &&
                 cp_parse_bool(fields[2], &area.icons[index].hidden);
            if( index + 1 > area.icon_count )
                area.icon_count = index + 1;
        }
    }
    cp_config_file_free(&file);

    uint8_t* payload = NULL;
    if( ok )
    {
        uint32_t bound = is_composite ? RSCache_WorldMapAreaEncodeIconsBound(&area)
                                      : RSCache_WorldMapAreaEncodeBound(&area);
        payload = malloc(bound ? bound : 1);
        uint32_t written = 0;
        if( payload )
        {
            written = is_composite ? RSCache_WorldMapAreaEncodeIcons(
                                         &area, RSCache_WorldMapFlags(&ctx->profile), payload,
                                         bound)
                                   : RSCache_WorldMapAreaEncode(&area, payload, bound);
        }
        if( written == 0 )
        {
            free(payload);
            payload = NULL;
        }
        else
        {
            *out_size = (int)written;
        }
    }
    RSCache_WorldMapAreaFreeInplace(&area);
    return payload;
}

const struct CP_AssetCodec cp_codec_worldmap = { "wma", worldmap_write, worldmap_read };
