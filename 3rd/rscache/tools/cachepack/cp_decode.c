#include "cp_assets.h"

#include "dat2disk.h"
#include "filelist.h"
#include "reference_table.h"
#include "datatypes/dat2_component.h"
#include "datatypes/dat2_texture.h"

#include <errno.h>
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

/* ---- textures ------------------------------------------------------------ */

/*
 * A texture is not an image.
 *
 * That is the whole reason this one is text. LostCity stores `textures/*.png`
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

const struct CP_AssetCodec cp_codec_texture = { "texture", texture_write, texture_read };

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
            fprintf(out, "[com %d]\nempty=yes\n\n", file_id);
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
        snprintf(header, sizeof(header), "[com %d]\n", file_id);
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

