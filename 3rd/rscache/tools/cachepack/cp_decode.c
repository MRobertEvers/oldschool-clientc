#include "cp_assets.h"

#include "dat2disk.h"
#include "bmp.h"
#include "datatypes/clientscript.h"
#include "datatypes/dat2_config_db.h"
#include "datatypes/dat2_font_metrics.h"
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

/** Whole file as bytes, for a member a codec stores verbatim. Caller frees. */
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

/* ---- member packs -------------------------------------------------------- */

/*
 * `<archive>.compack` / `<archive>.filepack` — an archive's member list, beside the
 * archive's own file in the table's folder.
 *
 * A multi-file archive is several assets under one id, and something has to say
 * which member is which. Three things were tried before this and each put the
 * answer somewhere that is not a file anyone can read:
 *
 *   - a directory of `<file_id>.<ext>`, so the id was in a *filename* and the
 *     member set came back from `readdir`;
 *   - probing `<stem>.extra2.bin` .. `.extra255.bin`, so the archive an import
 *     produced depended on what the filesystem happened to hold;
 *   - the id encoded in a block header (`[mat_47]`), which works but makes the
 *     header a number when it wants to be a name.
 *
 * The member pack is the answer stated plainly: `<file id>=<member>`, in the format
 * every other pack file in the tree already uses, so it reads and writes with
 * `lc_pack_load`/`lc_pack_save` and gets comment preservation and merge semantics
 * for free.
 *
 *   compack   the archive has a text form, and `<member>` is a block name inside it
 *             (`textures/texture_0.compack` says `0=mat_0`, and
 *             `textures/texture_0.texture` holds `[mat_0]`)
 *   filepack  it does not, and `<member>` is a filename beside it
 *
 * Ids ascending is the order the container stores its payload in and the order the
 * reference table's child list is read in, which is what a pack file gives for free.
 */

int
cp_member_pack_load(
    struct LC_Pack* pack,
    const char* path_stem,
    const char* kind,
    const char* label)
{
    char path[1750];
    snprintf(path, sizeof(path), "%s.%s", path_stem, kind);
    return lc_pack_load(pack, path, label, 1);
}

int
cp_member_pack_save(
    const struct LC_Pack* pack,
    const char* path_stem,
    const char* kind)
{
    char path[1750];
    snprintf(path, sizeof(path), "%s.%s", path_stem, kind);
    return lc_pack_save_quiet(pack, path);
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

/*
 * One file per texture archive, one block per material.
 *
 * The whole table lives in a single archive whose 210 members are the materials a
 * model actually references, so the *member* is the asset and the archive is a
 * container. This codec therefore owns the container: it decodes the member list on
 * the way out and rebuilds it on the way in, exactly as the interface codec does for
 * an interface's components.
 *
 * That replaced a directory of `<file_id>.texture` files, and the difference is not
 * presentation: the importer had to recover which members an archive held — and in
 * what order — by listing the directory.
 *
 * The member list is `textures/texture_0.compack`, beside the archive's own file:
 *
 *     0=mat_0
 *     1=mat_1
 *
 * so a block header is a *name* and the compack ties it to a file id. Rename a
 * material to `lava` in both and nothing else changes. `pack/texture.pack` stays
 * what it always was — one line for the one archive, the way
 * `pack/interface.pack` is one line per interface.
 */

static void
emit_texture_lines(
    const struct RSCache_Dat2Texture* texture,
    struct CP_Lines* lines)
{
    cp_lines_addf(lines, "averagehsl=%d", texture->average_hsl);
    if( texture->opaque )
        cp_lines_addf(lines, "opaque=yes");
    /* One line per sprite: its id, its type and its transform travel together
     * because the three arrays are parallel and a hand-edit that desynchronised
     * them would be silently wrong. */
    for( int i = 0; i < texture->sprite_ids_count; i++ )
        cp_lines_addf(lines, "sprite%d=%d,%d,%d", i + 1, texture->sprite_ids[i],
                      texture->sprite_types ? texture->sprite_types[i] : 0,
                      texture->transforms ? texture->transforms[i] : 0);
    if( texture->animation_direction )
        cp_lines_addf(lines, "direction=%d", texture->animation_direction);
    if( texture->animation_speed )
        cp_lines_addf(lines, "speed=%d", texture->animation_speed);
}

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
    if( file_count <= 0 )
        return 0;
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode((char*)payload, size, file_count);
    if( !files )
        return 0;

    char path[1700];
    snprintf(path, sizeof(path), "%s.texture", path_stem);
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        RSCache_FileListFree(files);
        return 0;
    }
    fprintf(out, "// Texture archive %d — %d materials.\n", record_id, files->file_count);
    fprintf(out, "// One block per material; %s.compack ties each block name to its\n",
            strrchr(path_stem, '/') ? strrchr(path_stem, '/') + 1 : path_stem);
    fprintf(out, "// file id in the archive.\n\n");

    /* The member list, merged over whatever the tree already had — so a material
     * someone renamed keeps its name across a re-export. */
    struct LC_Pack members;
    cp_member_pack_load(&members, path_stem, "compack", "mat");

    struct CP_Lines lines;
    cp_lines_init(&lines);
    int written = 0;
    for( int f = 0; f < files->file_count; f++ )
    {
        int file_id = file_ids ? file_ids[f] : f;
        const char* name = NULL;
        char fallback[32];

        if( file_id >= 0 && file_id < members.capacity && members.names )
            name = members.names[file_id];
        if( !name )
        {
            snprintf(fallback, sizeof(fallback), "mat_%d", file_id);
            name = fallback;
            lc_pack_set(&members, file_id, name);
        }

        if( files->file_sizes[f] <= 0 )
        {
            /* A zero-length member is a hole the archive still counts; it has to
             * come back as one or every later material shifts an id. */
            fprintf(out, "[%s]\nempty=yes\n\n", name);
            written++;
            continue;
        }
        struct RSCache_Dat2Texture* texture = RSCache_Dat2TextureNewDecodeProfile(
            &ctx->profile, files->files[f], files->file_sizes[f]);
        if( !texture )
            continue;
        cp_lines_clear(&lines);
        emit_texture_lines(texture, &lines);
        fprintf(out, "[%s]\n", name);
        for( int i = 0; i < lines.count; i++ )
            fprintf(out, "%s\n", lines.lines[i]);
        fputc('\n', out);
        written++;
        RSCache_Dat2TextureFree(texture);
    }
    cp_lines_free(&lines);
    fclose(out);
    if( written > 0 )
        cp_member_pack_save(&members, path_stem, "compack");
    lc_pack_free(&members);
    RSCache_FileListFree(files);
    return written > 0;
}

/** One material from one block. Returns 0 on a line the grammar does not have. */
static int
read_texture_block(
    struct CP_Ctx* ctx,
    const struct CP_Config* block,
    int file_id,
    uint8_t** out_bytes,
    int* out_size)
{
    struct RSCache_Dat2Texture texture;
    memset(&texture, 0, sizeof(texture));
    texture._id = file_id;

    struct CP_IntList ids = { 0 }, types = { 0 }, transforms = { 0 };
    int ok = 1;
    for( int i = 0; i < block->count && ok; i++ )
    {
        const char* key = block->lines[i].key;
        const char* value = block->lines[i].value;
        int index = cp_indexed_key(key, "sprite");
        if( index >= 0 )
        {
            char scratch[128];
            char* fields[3];
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 3) != 3 )
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

    if( ok )
    {
        texture.sprite_ids = ids.items;
        texture.sprite_types = types.items;
        texture.transforms = transforms.items;
        texture.sprite_ids_count = ids.count;

        uint8_t buffer[512];
        uint32_t written =
            RSCache_Dat2TextureEncodeProfile(&ctx->profile, &texture, buffer, sizeof(buffer));
        if( written == 0 )
            ok = 0;
        else
        {
            *out_bytes = malloc(written);
            if( !*out_bytes )
                ok = 0;
            else
            {
                memcpy(*out_bytes, buffer, written);
                *out_size = (int)written;
            }
        }
    }

    cp_intlist_free(&ids);
    cp_intlist_free(&types);
    cp_intlist_free(&transforms);
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
    if( !cp_config_file_load(&file, path) )
        return NULL;

    /*
     * The compack is the member list: it says which file id each block is, and its
     * ascending ids are the order the container gets. Without it there is nothing
     * tying a block to a number, so this is a failure rather than a guess — an
     * invented id would shift every later material and every model referencing one
     * past that point would draw the wrong texture.
     */
    struct LC_Pack members;
    cp_member_pack_load(&members, path_stem, "compack", "mat");
    if( members.max == 0 )
    {
        fprintf(stderr, "cachepack: %s: no .compack beside it — cannot tell which "
                        "material is which file\n",
                path);
        cp_config_file_free(&file);
        return NULL;
    }

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
        int file_id = lc_pack_find(&members, block->debugname);
        if( file_id < 0 )
        {
            fprintf(stderr, "cachepack: %s: [%s] is not in the .compack\n", path,
                    block->debugname);
            ok = 0;
            break;
        }
        /* Position is the container's order and the id is data, so a hole in the
         * material list survives the round trip. */
        ids[i] = file_id;

        if( cp_config_get(block, "empty") )
        {
            list.files[i] = NULL;
            list.file_sizes[i] = 0;
            continue;
        }

        uint8_t* bytes = NULL;
        int size = 0;
        if( !read_texture_block(ctx, block, file_id, &bytes, &size) )
        {
            fprintf(stderr, "cachepack: %s: material [%s] failed to encode\n", path,
                    block->debugname);
            ok = 0;
            break;
        }
        list.files[i] = (char*)bytes;
        list.file_sizes[i] = size;
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
    lc_pack_free(&members);
    cp_config_file_free(&file);
    return payload;
}

/* ---- interfaces ---------------------------------------------------------- */

/*
 * A component emits only what differs from a component decoded from nothing, so the
 * defaults stay the decoder's rather than becoming a second copy here.
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

static int
append_escaped_arg(
    char* buf,
    int written,
    int cap,
    const char* text)
{
    for( const char* scan = text; *scan && written < cap - 2; scan++ )
    {
        if( *scan == ',' || *scan == '\\' )
            buf[written++] = '\\';
        buf[written++] = *scan;
    }
    buf[written] = '\0';
    return written;
}

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
    buf[0] = '\0';
    for( int32_t i = 0; i < len && written < (int)sizeof(buf) - 64; i++ )
    {
        if( args[i].type == RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT )
            written += snprintf(buf + written, sizeof(buf) - (size_t)written, i ? ",i:%d" : "i:%d",
                                args[i].value.i);
        else
        {
            written += snprintf(buf + written, sizeof(buf) - (size_t)written, i ? ",s:" : "s:");
            written = append_escaped_arg(buf, written, (int)sizeof(buf), args[i].value.s
                                                                            ? args[i].value.s
                                                                            : "");
        }
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
    fprintf(out, "// One block per component; %s.compack ties each block name to its\n",
            strrchr(path_stem, '/') ? strrchr(path_stem, '/') + 1 : path_stem);
    fprintf(out, "// file id in the archive. `if3` selects the field set.\n\n");

    /* Merged over whatever the tree already had, so a component someone renamed
     * keeps its name across a re-export. */
    struct LC_Pack members;
    cp_member_pack_load(&members, path_stem, "compack", "com");

    struct CP_Lines lines;
    cp_lines_init(&lines);
    int written = 0;
    for( int f = 0; f < files->file_count; f++ )
    {
        int file_id = file_ids ? file_ids[f] : f;
        const char* name = NULL;
        char fallback[256];

        if( file_id >= 0 && file_id < members.capacity && members.names )
            name = members.names[file_id];
        if( !name )
        {
            /*
             * The cache's own name for this child, from gameval archive 14 —
             * `bankmain:items` for `(12 << 16) | 12`. The interface is already the
             * file, so only the part after the colon is the block name.
             *
             * This is what makes the compack the single index over an interface's
             * members. It used to fall straight to `com_<id>` and the real names
             * lived in a second file, `pack/component.pack`, keyed by the composed
             * id — two indexes over the same members, one filler and one named.
             */
            const char* gameval = ctx ? cp_component_name(ctx, record_id, file_id) : NULL;
            const char* child = gameval ? strchr(gameval, ':') : NULL;

            if( child && child[1] )
                snprintf(fallback, sizeof(fallback), "%s", child + 1);
            else
                snprintf(fallback, sizeof(fallback), "com_%d", file_id);
            name = fallback;
            lc_pack_set(&members, file_id, name);
        }

        if( files->file_sizes[f] <= 0 )
        {
            /* A zero-length member is a hole the archive still counts; it has to
             * come back as one or every later component shifts an id. */
            fprintf(out, "[%s]\nempty=yes\n\n", name);
            written++;
            continue;
        }
        struct RSCache_Dat2Component* comp = RSCache_Dat2ComponentNewDecode(
            (uint8_t*)files->files[f], files->file_sizes[f], (record_id << 16) | file_id, rev);
        if( !comp )
            continue;
        cp_lines_clear(&lines);
        emit_component(ctx, comp, &reference, &lines);
        fprintf(out, "[%s]\n", name);
        for( int i = 0; i < lines.count; i++ )
            fprintf(out, "%s\n", lines.lines[i]);
        fputc('\n', out);
        written++;
        RSCache_Dat2ComponentFree(comp);
    }
    cp_lines_free(&lines);
    fclose(out);
    if( written > 0 )
        cp_member_pack_save(&members, path_stem, "compack");
    lc_pack_free(&members);
    RSCache_Dat2ComponentFree(&reference);
    RSCache_FileListFree(files);
    return written > 0;
}


/* ---- interfaces: reading the text back ---------------------------------- */

/**
 * The next argument separator: a comma that `append_escaped_arg` did not escape.
 *
 * Splitting on every comma is what broke here — a hook string argument can hold
 * one. The escapes are also collapsed in place, so the caller gets the argument's
 * real text.
 */
static char*
split_script_var(char* cursor)
{
    char* read = cursor;
    char* write = cursor;

    while( *read )
    {
        if( *read == '\\' && read[1] )
        {
            *write++ = read[1];
            read += 2;
            continue;
        }
        if( *read == ',' )
        {
            *write = '\0';
            return read + 1;
        }
        *write++ = *read++;
    }
    *write = '\0';
    return NULL;
}

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
        char* next = split_script_var(cursor);
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
        cursor = next;
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

    /* The member list. Without it nothing ties a block to a file id, and an invented
     * id would shift every later component — so this is a failure, not a guess. */
    struct LC_Pack members;
    cp_member_pack_load(&members, path_stem, "compack", "com");
    if( members.max == 0 )
    {
        fprintf(stderr, "cachepack: %s: no .compack beside it — cannot tell which "
                        "component is which file\n",
                path);
        cp_config_file_free(&file);
        return NULL;
    }

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
        /* The id is data, not position, so a hole in the component list survives the
         * round trip. */
        int file_id = lc_pack_find(&members, block->debugname);
        if( file_id < 0 )
        {
            fprintf(stderr, "cachepack: %s: [%s] is not in the .compack\n", path,
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
    lc_pack_free(&members);
    cp_config_file_free(&file);
    return payload;
}

const struct CP_AssetCodec cp_codec_texture = { "texture", NULL, texture_write, texture_read, 0 };
const struct CP_AssetCodec cp_codec_interface = { "if", NULL, interface_write, interface_read, 0 };

/* ---- maps ---------------------------------------------------------------- */

/*
 * LostCity's `.jm2`: a `==== MAP ====` section of tiles keyed `<level> <x> <z>:`,
 * and beside it a `.jl2` holding the square's `==== LOC ====` scenery in the same
 * grammar. LostCity keeps both in one file; here they are two, because they are two
 * members of the archive and the filepack names members.
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
 * but osrs239 squares also carry files 2, 3 and 4 that nothing here decodes — a
 * "readable map format" that silently dropped a third of the archive would be a
 * worse trade than no readable format at all.
 *
 * All five are listed in `<stem>.filepack` — the same index a `dbindex` archive gets.
 * Files 0 and 1 point at the `.jm2` and the `.jl2`, the rest at `.bin`; what differs
 * is whether a member has a text form, not whether the index knows about it.
 *
 * They used to be `<stem>.extra<N>.bin` with no index at all, so the member's id was
 * in its *filename* and the importer recovered the member set by probing `extra2`
 * through `extra255` — which meant the archive an import produced depended on what
 * the filesystem happened to hold. The filepack states it instead.
 *
 * **The jm2 is shared with the server, and only one of its sections is ours.**
 * A square's `==== NPC ====` and `==== OBJ ====` spawns are content too, but they
 * live on the server and never enter the cache — LostCity keeps them in the same
 * file as the terrain, and so does this. So the codec owns MAP and treats every
 * other section as somebody else's: preserved verbatim when unpacking over an
 * existing file, skipped when packing. Without that, unpacking would delete the
 * server's spawns and packing would try to read `0 1 63: 1265` as a tile.
 *
 * The spawns stay with the terrain rather than moving to the `.jl2` because they are
 * the square's, not file 1's — and because `maps/m<x>_<z>.jm2` is the path the server
 * already scans.
 */

#define CP_MAP_TERRAIN_FILE 0
#define CP_MAP_LOCS_FILE 1

/** True when both halves re-encode to exactly the bytes they were decoded from. */
static int
map_reencodes_exactly(
    const struct RSCache_MapTerrain* terrain,
    const struct RSCache_MapLocs* locs,
    int flags,
    const struct RSCache_FileList* files,
    int terrain_index,
    int locs_index)
{
    int ok = 0;
    uint32_t terrain_bound = RSCache_MapTerrainEncodeBoundFor(terrain, flags);
    uint8_t* terrain_bytes = malloc(terrain_bound ? terrain_bound : 1);
    uint32_t locs_bound = RSCache_MapLocsEncodeBound(locs);
    uint8_t* locs_bytes = malloc(locs_bound ? locs_bound : 1);
    if( terrain_bytes && locs_bytes )
    {
        uint32_t tn = RSCache_MapTerrainEncode(terrain, flags, terrain_bytes, terrain_bound);
        uint32_t ln = RSCache_MapLocsEncode(locs, locs_bytes, locs_bound);
        ok = tn == (uint32_t)files->file_sizes[terrain_index] &&
             ln == (uint32_t)files->file_sizes[locs_index] &&
             memcmp(terrain_bytes, files->files[terrain_index], tn) == 0 &&
             memcmp(locs_bytes, files->files[locs_index], ln) == 0;
    }
    free(terrain_bytes);
    free(locs_bytes);
    return ok;
}

/**
 * MAP is the cache's; anything else in the jm2 belongs to the server.
 *
 * LOC is still named here even though locs now live in the `.jl2`, because a jm2
 * written before the split still has a LOC section and carrying it forward as
 * "foreign" would duplicate every loc into the server's half of the file.
 */
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

    int flags = RSCache_MapTerrainFlags(&ctx->profile);
    struct RSCache_MapTerrain* terrain = RSCache_MapTerrainNewFromDecodeFlags(
        files->files[terrain_index], files->file_sizes[terrain_index], 0, 0,
        flags | RSCACHE_MAP_TERRAIN_DECODE_NO_FIXUP);
    struct RSCache_MapLocs* locs =
        RSCache_MapLocsNewDecode(files->files[locs_index], files->file_sizes[locs_index]);
    if( !terrain || !locs )
    {
        RSCache_MapTerrainFree(terrain);
        RSCache_MapLocsFree(locs);
        RSCache_FileListFree(files);
        return 0;
    }

    /*
     * Prove the decode before writing text for it.
     *
     * The tile loop is a fixed 4 x 64 x 64 with no header saying so, and a buffer
     * read past the end returns zeros — so a file that is not a terrain stream
     * decodes to a plausible empty square rather than failing. Archive 25287's
     * file 0 is three bytes, and it produced a 32 KB "square" of nothing.
     *
     * Re-encoding and comparing costs one pass over 54 KB and makes the guarantee
     * exact: a `.jm2` this writes packs back to the bytes it came from, and
     * anything else falls back to the raw payload.
     */
    if( !map_reencodes_exactly(terrain, locs, flags, files, terrain_index, locs_index) )
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
    /*
     * Bytes past the last tile. The tile loop is a fixed 4 x 64 x 64 and the
     * client stops when it has read them all, so nothing interprets these — but
     * every OldSchool 239 square has one, non-zero in 1,612 of 2,934, and a
     * re-encode without it is a byte short of the original. Hex on one line
     * because it is one byte almost everywhere; the one square with 9,564 of them
     * gets a long line rather than a second mechanism.
     */
    if( terrain->trailing_size > 0 )
    {
        fprintf(out, "trailing=");
        for( int i = 0; i < terrain->trailing_size; i++ )
            fprintf(out, "%02x", terrain->trailing[i]);
        fprintf(out, "\n");
    }
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

    if( foreign )
    {
        fprintf(out, "\n%s", foreign);
        free(foreign);
    }
    fclose(out);

    /*
     * Locs are file 1, so they are their own file and their own filepack entry.
     *
     * They used to be a second section of the jm2, which made one path on disk stand
     * for two members of the archive — the filepack then started at `2=` and the two
     * members with a text form were the only ones the index did not name. Splitting
     * them costs a file and buys the rule back: every member is addressable, and the
     * index says where each one is.
     */
    snprintf(path, sizeof(path), "%s.jl2", path_stem);
    out = fopen(path, "wb");
    if( !out )
    {
        RSCache_MapTerrainFree(terrain);
        RSCache_MapLocsFree(locs);
        RSCache_FileListFree(files);
        return 0;
    }
    fprintf(out, "==== LOC ====\n");
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
    fclose(out);

    /*
     * Every member the jm2 has no section for, kept verbatim as files and listed in
     * `<stem>.filepack`. Terrain and locs are not listed: they are the jm2, and an
     * index entry for them would name a file that does not exist.
     */
    {
        struct LC_Pack extras;
        const char* stem_name = strrchr(path_stem, '/');
        char root_dir[1700];
        int listed = 0;

        /* Filepack entries are relative to the table's folder, the same as every
         * other member index, so a member can be moved anywhere under it. */
        snprintf(root_dir, sizeof(root_dir), "%s", path_stem);
        char* cut = strrchr(root_dir, '/');
        if( cut )
            *cut = '\0';
        else
            snprintf(root_dir, sizeof(root_dir), ".");
        stem_name = stem_name ? stem_name + 1 : path_stem;
        cp_member_pack_load(&extras, path_stem, "filepack", "map");
        for( int f = 0; f < files->file_count; f++ )
        {
            int id = file_ids ? file_ids[f] : f;
            const char* member = NULL;
            char fallback[160];

            /*
             * Terrain and locs are listed like everything else — they have a text
             * form, so the index points at the `.jm2` and the `.jl2` rather than at
             * a `.bin`, and the loop below does not rewrite them.
             */
            if( id == CP_MAP_TERRAIN_FILE || id == CP_MAP_LOCS_FILE )
            {
                char text[200];

                snprintf(text, sizeof(text), "%s.%s", stem_name,
                         id == CP_MAP_TERRAIN_FILE ? "jm2" : "jl2");
                lc_pack_set(&extras, id, text);
                listed++;
                continue;
            }
            if( id >= 0 && id < extras.capacity && extras.names )
                member = extras.names[id];
            if( !member )
            {
                snprintf(fallback, sizeof(fallback), "%s/%d.bin", stem_name, id);
                member = fallback;
                lc_pack_set(&extras, id, member);
            }
            snprintf(path, sizeof(path), "%s/%s", root_dir, member);
            char* slash = strrchr(path, '/');
            if( slash )
            {
                *slash = '\0';
                ensure_dir_path(path);
                *slash = '/';
            }
            FILE* extra = fopen(path, "wb");
            if( extra )
            {
                fwrite(files->files[f], 1, (size_t)files->file_sizes[f], extra);
                fclose(extra);
                listed++;
            }
        }
        if( listed )
            cp_member_pack_save(&extras, path_stem, "filepack");
        lc_pack_free(&extras);
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

    /*
     * Locs are their own file, so the two are read together and parsed by one pass —
     * the section machine below already keys off the banner, and a `.jl2` is nothing
     * but a `==== LOC ====` section.
     *
     * A tree exported before the split still has LOC inside its jm2. Appending would
     * then read every loc twice, so that case is refused rather than silently
     * doubled: it is a tree in a state no export produces, and guessing which half is
     * authoritative would be worse than saying so.
     */
    {
        char jl2_path[1700];
        int jl2_size = 0;
        char* jl2;

        snprintf(jl2_path, sizeof(jl2_path), "%s.jl2", path_stem);
        jl2 = (char*)slurp(jl2_path, &jl2_size);
        if( jl2 )
        {
            if( strstr(text, "==== LOC ====") )
            {
                fprintf(stderr,
                        "cachepack: %s has a LOC section and %s exists — locs are in "
                        "two places; delete the section from the jm2\n",
                        path, jl2_path);
                free(jl2);
                free(text);
                return NULL;
            }
            char* joined = (char*)malloc((size_t)text_size + (size_t)jl2_size + 2);
            if( !joined )
            {
                free(jl2);
                free(text);
                return NULL;
            }
            memcpy(joined, text, (size_t)text_size);
            joined[text_size] = '\n';
            memcpy(joined + text_size + 1, jl2, (size_t)jl2_size);
            joined[text_size + 1 + jl2_size] = '\0';
            free(text);
            free(jl2);
            text = joined;
            text_size = text_size + 1 + jl2_size;
        }
    }

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

    /*
     * The archive's other members, from `<stem>.filepack`.
     *
     * Read from an index rather than probed from `<stem>.extra<N>.bin` filenames, so
     * the member set and its order are stated by the tree instead of by whatever the
     * filesystem holds.
     */
    struct LC_Pack extras;
    char root_dir[1700];
    cp_member_pack_load(&extras, path_stem, "filepack", "map");
    snprintf(root_dir, sizeof(root_dir), "%s", path_stem);
    {
        char* cut = strrchr(root_dir, '/');
        if( cut )
            *cut = '\0';
        else
            snprintf(root_dir, sizeof(root_dir), ".");
    }
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

        /* A `key=value` line inside MAP is a header, not a tile. */
        char* equals = strchr(line, '=');
        char* colon = strchr(line, ':');
        if( equals && (!colon || equals < colon) )
        {
            *equals = '\0';
            const char* value = equals + 1;
            if( strcmp(line, "trailing") == 0 )
            {
                size_t digits = strlen(value);
                terrain->trailing_size = (int)(digits / 2);
                terrain->trailing = malloc(digits / 2 ? digits / 2 : 1);
                if( !terrain->trailing )
                {
                    terrain->trailing_size = 0;
                    ok = 0;
                    break;
                }
                for( int i = 0; i < terrain->trailing_size; i++ )
                {
                    unsigned byte = 0;
                    if( sscanf(value + i * 2, "%2x", &byte) != 1 )
                    {
                        ok = 0;
                        break;
                    }
                    terrain->trailing[i] = (uint8_t)byte;
                }
            }
            else
            {
                ok = 0;
            }
            continue;
        }

        int level = 0, x = 0, z = 0;
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
        uint32_t terrain_bound = RSCache_MapTerrainEncodeBoundFor(terrain, flags);
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
            /*
             * Rebuild the whole archive: terrain, locs, then the EXTRA sections the
             * file listed, each back under the id its banner stated and in the order
             * the file gave them.
             *
             * This used to probe `<stem>.extra2.bin` through `.extra255.bin` and
             * take whatever the filesystem happened to hold, which meant the archive
             * an import produced depended on a directory rather than on anything the
             * tree stated. Most squares are five files and the table also holds one
             * 37-file archive, so the count was never assumable either way — now it
             * is simply read.
             */
            struct RSCache_FileList list;
            memset(&list, 0, sizeof(list));
            int capacity = 2 + (extras.max > 0 ? extras.max : 0);
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

                for( int extra = 0; extra < extras.max; extra++ )
                {
                    const char* member = extras.names ? extras.names[extra] : NULL;
                    uint8_t* bytes = NULL;
                    int bytes_size = 0;

                    if( !member )
                        continue;
                    /* 0 and 1 are listed in the filepack too, but they name the jm2
                     * and the jl2 — already decoded above into terrain and locs, and
                     * reading them here as raw bytes would add the same two members
                     * a second time. */
                    if( extra == CP_MAP_TERRAIN_FILE || extra == CP_MAP_LOCS_FILE )
                        continue;
                    snprintf(path, sizeof(path), "%s/%s", root_dir, member);
                    if( !read_file_bytes(path, &bytes, &bytes_size) )
                    {
                        fprintf(stderr, "cachepack: %s.filepack lists %s, which is not on "
                                        "disk\n",
                                path_stem, member);
                        continue;
                    }
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

    lc_pack_free(&extras);
    RSCache_MapTerrainFree(terrain);
    RSCache_MapLocsFree(locs);
    return payload;
}

const struct CP_AssetCodec cp_codec_map = { "jm2", "jl2", map_write, map_read, 0 };

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
     * decoded one — but this one is on the stack, so only the buffers go. */
    RSCache_ClientScriptFreeInplace(&script);
    if( written == 0 )
    {
        free(payload);
        return NULL;
    }
    *out_size = (int)written;
    return payload;
}

/* Trailing 1 is `semantic_only`: the friendly form is source, and compiling source
 * back gives this compiler's bytes rather than Jagex's. See cp_assets.h. */
const struct CP_AssetCodec cp_codec_script = { "cs2", NULL, script_write, script_read, 1 };

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

const struct CP_AssetCodec cp_codec_sprite = { "bmp", NULL, sprite_write, sprite_read, 0 };

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

/*
 * The `labels` archives — a map's place names.
 *
 * `u16 count`, then per label a NUL-terminated name, `u16 x`, `u16 y` and a `u8`
 * size class. The coordinates are world coordinates (Lumbridge reads 3239, 3234)
 * and a `/` in a name is a line break, which is why `Kingdom of/Misthalin` looks
 * the way it does.
 *
 * 50 of osrs239's 51 label archives are two bytes — a count of zero. The
 * fifty-first holds 548 labels in 10,261 bytes and consumes to the byte, which is
 * what makes this layout a reading rather than a guess.
 */
struct CP_WorldMapLabel
{
    char* name;
    int x;
    int y;
    int size;
};

static void
worldmap_labels_free(
    struct CP_WorldMapLabel* labels,
    int count)
{
    for( int i = 0; i < count; i++ )
        free(labels[i].name);
    free(labels);
}

/**
 * Decode a labels file, or return -1.
 *
 * Exact consumption is the test. A PNG or a composite stream will happily produce
 * a plausible count and some strings, and the only thing that separates those from
 * a real labels file is landing on the last byte.
 */
static int
worldmap_try_labels(
    const uint8_t* payload,
    int size,
    struct CP_WorldMapLabel** out_labels)
{
    struct RSCache_Buffer buf;

    *out_labels = NULL;
    if( size < 2 )
        return -1;
    RSCache_BufferInit(&buf, (uint8_t*)payload, (uint32_t)size);
    int count = g2(&buf);
    if( count < 0 || count > 100000 )
        return -1;
    if( count == 0 )
        return buf.position == (uint32_t)size ? 0 : -1;

    struct CP_WorldMapLabel* labels = calloc((size_t)count, sizeof(*labels));
    if( !labels )
        return -1;
    for( int i = 0; i < count; i++ )
    {
        uint32_t start = buf.position;
        while( buf.position < (uint32_t)size && buf.data[buf.position] )
            buf.position++;
        if( buf.position >= (uint32_t)size || buf.position - start > 255 )
        {
            worldmap_labels_free(labels, i);
            return -1;
        }
        int length = (int)(buf.position - start);
        labels[i].name = malloc((size_t)length + 1);
        if( !labels[i].name )
        {
            worldmap_labels_free(labels, i);
            return -1;
        }
        memcpy(labels[i].name, buf.data + start, (size_t)length);
        labels[i].name[length] = '\0';
        buf.position++; /* the NUL */
        if( buf.position + 5 > (uint32_t)size )
        {
            worldmap_labels_free(labels, i + 1);
            return -1;
        }
        labels[i].x = g2(&buf);
        labels[i].y = g2(&buf);
        labels[i].size = g1(&buf);
    }
    if( buf.position != (uint32_t)size )
    {
        worldmap_labels_free(labels, count);
        return -1;
    }
    *out_labels = labels;
    return count;
}

/*
 * The world-map table is laid out **kind by kind**: the archive is one of the five
 * names `class305` declares and the file is one map. So a codec here owns a whole
 * archive of maps, not one record — the same shape the interface and texture codecs
 * have, and for the same reason.
 *
 * Only two of the archives have a text form. `compositetexture` is PNGs and
 * `labels` is empty, and both fall through to the raw payload; running the area
 * decoder over them is what produced the "unknown section type 91 / 219 / 249"
 * flood, because a PNG read as a section list says whatever its pixels say.
 */
enum
{
    CP_WORLDMAP_ARCHIVE_DETAILS = 0,
    CP_WORLDMAP_ARCHIVE_COMPOSITE = 1,
};

static void
worldmap_emit_area(
    const struct RSCache_WorldMapArea* area,
    struct CP_Lines* lines)
{
    cp_lines_add_str(lines, "internal", area->internal_name);
    cp_lines_add_str(lines, "external", area->external_name);
    cp_lines_addf(lines, "origin=%d", area->origin);
    /* All eight digits: the cache's background is 0xFF000000, and a %06X here
     * would write the alpha away and re-encode a different colour. */
    cp_lines_addf(lines, "background=0x%08X", (unsigned)area->background_colour);
    cp_lines_addf(lines, "main=%s", area->is_main ? "yes" : "no");
    cp_lines_addf(lines, "zoom=%d", area->zoom);
    /* Two fields the client never reads; kept so the record re-encodes. */
    cp_lines_addf(lines, "unknown_int=%d", area->unknown_int);
    cp_lines_addf(lines, "unknown_byte=%d", area->unknown_byte);
    for( int i = 0; i < area->section_count; i++ )
    {
        const struct RSCache_WorldMapSection* section = &area->sections[i];
        cp_lines_addf(lines,
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
        cp_lines_addf(lines, "section%d_dst_chunk_y_high=%d", i + 1,
                      section->dst_chunk_y_high);
    }
}

static void
worldmap_emit_composite(
    const struct RSCache_WorldMapArea* area,
    struct CP_Lines* lines)
{
    cp_lines_addf(lines, "data0=%d", area->data0_count);
    for( int i = 0; i < area->region_count; i++ )
    {
        const struct RSCache_WorldMapRegion* region = &area->regions[i];
        cp_lines_addf(lines, "region%d=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", i + 1,
                      region->kind, region->min_plane, region->planes,
                      region->src_region_x, region->src_region_y, region->src_chunk_x,
                      region->src_chunk_y, region->dst_region_x, region->dst_region_y,
                      region->dst_chunk_x, region->dst_chunk_y, region->group_id,
                      region->file_id);
    }
    for( int i = 0; i < area->icon_count; i++ )
        cp_lines_addf(lines, "icon%d=%d,%d,%s", i + 1, area->icons[i].element,
                      area->icons[i].coord, area->icons[i].hidden ? "yes" : "no");
}

/** One map's block into `area`. Returns 0 on a line the grammar does not have. */
static int
worldmap_read_block(
    const struct CP_Config* block,
    int record_id,
    struct RSCache_WorldMapArea* area)
{
    int section_capacity = 0, region_capacity = 0, icon_capacity = 0;
    int ok = 1;

    memset(area, 0, sizeof(*area));
    area->id = record_id;

    for( int i = 0; i < block->count && ok; i++ )
    {
        const char* key = block->lines[i].key;
        const char* value = block->lines[i].value;
        char scratch[512];
        char* fields[20];
        int index;

        if( strcmp(key, "internal") == 0 )
        {
            char buf[512];
            cp_unescape(value, buf, sizeof(buf));
            area->internal_name = strdup(buf);
        }
        else if( strcmp(key, "external") == 0 )
        {
            char buf[512];
            cp_unescape(value, buf, sizeof(buf));
            area->external_name = strdup(buf);
        }
        else if( strcmp(key, "origin") == 0 )
            ok = cp_parse_int(value, &area->origin);
        else if( strcmp(key, "background") == 0 )
        {
            /* 0xFF000000 does not fit a signed int, so it goes through the wider
             * parse and lands as the same 32 bits the cache holds. */
            int64_t colour = 0;
            ok = cp_parse_i64(value, &colour);
            area->background_colour = (int)(uint32_t)colour;
        }
        else if( strcmp(key, "main") == 0 )
            ok = cp_parse_bool(value, &area->is_main);
        else if( strcmp(key, "zoom") == 0 )
            ok = cp_parse_int(value, &area->zoom);
        else if( strcmp(key, "unknown_int") == 0 )
            ok = cp_parse_int(value, &area->unknown_int);
        else if( strcmp(key, "unknown_byte") == 0 )
            ok = cp_parse_int(value, &area->unknown_byte);
        else if( strcmp(key, "data0") == 0 )
            ok = cp_parse_int(value, &area->data0_count);
        else if( strstr(key, "_dst_chunk_y_high") )
        {
            index = atoi(key + 7) - 1;
            if( index >= 0 && index < area->section_count )
                ok = cp_parse_int(value, &area->sections[index].dst_chunk_y_high);
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
                    realloc(area->sections, (size_t)next * sizeof(*grown));
                if( !grown )
                {
                    ok = 0;
                    break;
                }
                memset(grown + section_capacity, 0,
                       (size_t)(next - section_capacity) * sizeof(*grown));
                area->sections = grown;
                section_capacity = next;
            }
            struct RSCache_WorldMapSection* s = &area->sections[index];
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
            if( index + 1 > area->section_count )
                area->section_count = index + 1;
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
                    realloc(area->regions, (size_t)next * sizeof(*grown));
                if( !grown )
                {
                    ok = 0;
                    break;
                }
                memset(grown + region_capacity, 0,
                       (size_t)(next - region_capacity) * sizeof(*grown));
                area->regions = grown;
                region_capacity = next;
            }
            struct RSCache_WorldMapRegion* r = &area->regions[index];
            int* slots[13] = { &r->kind,         &r->min_plane,    &r->planes,
                               &r->src_region_x, &r->src_region_y, &r->src_chunk_x,
                               &r->src_chunk_y,  &r->dst_region_x, &r->dst_region_y,
                               &r->dst_chunk_x,  &r->dst_chunk_y,  &r->group_id,
                               &r->file_id };
            for( int f = 0; f < 13 && ok; f++ )
                ok = cp_parse_int(fields[f], slots[f]);
            if( index + 1 > area->region_count )
                area->region_count = index + 1;
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
                    realloc(area->icons, (size_t)next * sizeof(*grown));
                if( !grown )
                {
                    ok = 0;
                    break;
                }
                memset(grown + icon_capacity, 0,
                       (size_t)(next - icon_capacity) * sizeof(*grown));
                area->icons = grown;
                icon_capacity = next;
            }
            ok = cp_parse_int(fields[0], &area->icons[index].element) &&
                 cp_parse_int(fields[1], &area->icons[index].coord) &&
                 cp_parse_bool(fields[2], &area->icons[index].hidden);
            if( index + 1 > area->icon_count )
                area->icon_count = index + 1;
        }
    }
    return ok;
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
    int composite;
    const char* ext;

    if( record_id == CP_WORLDMAP_ARCHIVE_DETAILS )
    {
        composite = 0;
        ext = "wma";
    }
    else if( record_id == CP_WORLDMAP_ARCHIVE_COMPOSITE )
    {
        composite = 1;
        ext = "wmc";
    }
    else
    {
        /*
         * Everything else is either labels or a PNG, and the labels decode is what
         * tells them apart: exact consumption. A single-file archive's payload is
         * the file itself, with no FileList framing.
         */
        struct CP_WorldMapLabel* labels = NULL;
        int count = file_count == 1 ? worldmap_try_labels(payload, size, &labels) : -1;
        if( count < 0 )
            return 0; /* a PNG, or a labels file this does not read */

        char label_path[1800];
        snprintf(label_path, sizeof(label_path), "%s.wml", path_stem);
        FILE* labels_out = fopen(label_path, "wb");
        if( !labels_out )
        {
            worldmap_labels_free(labels, count);
            return 0;
        }
        fprintf(labels_out,
                "// World map labels — %d of them.\n"
                "// label<N> = name,x,y,size. The coordinates are world coordinates and a\n"
                "// `/` in a name is a line break, so `Kingdom of/Misthalin` is two lines.\n"
                "// size is the text class: 0 small, 1 medium, 2 large.\n\n",
                count);
        for( int i = 0; i < count; i++ )
            fprintf(labels_out, "label%d=%s,%d,%d,%d\n", i + 1, labels[i].name, labels[i].x,
                    labels[i].y, labels[i].size);
        fclose(labels_out);
        worldmap_labels_free(labels, count);
        return 1;
    }
    if( file_count <= 0 )
        return 0;

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode((char*)payload, size, file_count);
    if( !files )
        return 0;

    /*
     * Decode everything before writing anything.
     *
     * A partial text file would be worse than none: the members it dropped would
     * come back missing, and every later map would shift an id. So a single member
     * this decoder cannot read declines the whole archive, and the raw payload is
     * written instead.
     */
    struct RSCache_WorldMapArea* areas =
        calloc((size_t)files->file_count, sizeof(*areas));
    if( !areas )
    {
        RSCache_FileListFree(files);
        return 0;
    }
    int decoded = 0;
    for( ; decoded < files->file_count; decoded++ )
    {
        int file_id = file_ids ? file_ids[decoded] : decoded;
        int got = composite ? worldmap_try_composite(ctx, (const uint8_t*)files->files[decoded],
                                                     files->file_sizes[decoded], &areas[decoded])
                            : worldmap_try_area(ctx, file_id,
                                                (const uint8_t*)files->files[decoded],
                                                files->file_sizes[decoded], &areas[decoded]);
        if( !got )
            break;
    }
    if( decoded < files->file_count )
    {
        for( int i = 0; i < decoded; i++ )
            RSCache_WorldMapAreaFreeInplace(&areas[i]);
        free(areas);
        RSCache_FileListFree(files);
        return 0;
    }

    /* The member list, merged over whatever the tree already had. */
    struct LC_Pack members;
    cp_member_pack_load(&members, path_stem, "compack", "map");

    char path[1800];
    snprintf(path, sizeof(path), "%s.%s", path_stem, ext);
    FILE* out = fopen(path, "wb");
    if( !out )
    {
        for( int i = 0; i < files->file_count; i++ )
            RSCache_WorldMapAreaFreeInplace(&areas[i]);
        free(areas);
        lc_pack_free(&members);
        RSCache_FileListFree(files);
        return 0;
    }

    if( composite )
        fprintf(out,
                "// World map composites — %d maps.\n"
                "// One block per map; the .compack ties each block name to its file id.\n"
                "// `data0` is where the first region block ends — the two blocks are stored\n"
                "// back to back and the split is not derivable.\n"
                "// region: kind,min_plane,planes,src_rx,src_ry,src_cx,src_cy,dst_rx,dst_ry,\n"
                "//         dst_cx,dst_cy,group,file\n"
                "// icon:   element,coord,hidden\n\n",
                files->file_count);
    else
        fprintf(out,
                "// World map areas — %d maps.\n"
                "// One block per map; the .compack ties each block name to its file id.\n"
                "// `section<N>` fields are, in order: type, min_plane, planes, then the\n"
                "// source side and the destination side; a section only uses the ones its\n"
                "// type declares (see RSCACHE_WORLDMAP_SECTION_*).\n\n",
                files->file_count);

    struct CP_Lines lines;
    cp_lines_init(&lines);
    for( int f = 0; f < files->file_count; f++ )
    {
        int file_id = file_ids ? file_ids[f] : f;
        const char* name = NULL;
        char fallback[64];

        if( file_id >= 0 && file_id < members.capacity && members.names )
            name = members.names[file_id];
        if( !name )
        {
            /* The map's own name, which only the details archive states — so the
             * composite archive gets it too, and both files read as maps rather
             * than as numbers. */
            const char* map = cp_assets_worldmap_member(file_id);
            snprintf(fallback, sizeof(fallback), "%s", map ? map : "");
            if( !fallback[0] )
                snprintf(fallback, sizeof(fallback), "map_%d", file_id);
            name = fallback;
            lc_pack_set(&members, file_id, name);
        }

        cp_lines_clear(&lines);
        if( composite )
            worldmap_emit_composite(&areas[f], &lines);
        else
            worldmap_emit_area(&areas[f], &lines);
        fprintf(out, "[%s]\n", name);
        for( int i = 0; i < lines.count; i++ )
            fprintf(out, "%s\n", lines.lines[i]);
        fputc('\n', out);
    }
    cp_lines_free(&lines);
    fclose(out);
    cp_member_pack_save(&members, path_stem, "compack");

    for( int i = 0; i < files->file_count; i++ )
        RSCache_WorldMapAreaFreeInplace(&areas[i]);
    free(areas);
    lc_pack_free(&members);
    RSCache_FileListFree(files);
    return 1;
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
    int composite;
    const char* ext;

    if( record_id == CP_WORLDMAP_ARCHIVE_DETAILS )
    {
        composite = 0;
        ext = "wma";
    }
    else if( record_id == CP_WORLDMAP_ARCHIVE_COMPOSITE )
    {
        composite = 1;
        ext = "wmc";
    }
    else
    {
        /* Labels: one file per archive, so no member list and no compack. */
        char label_path[1800];
        snprintf(label_path, sizeof(label_path), "%s.wml", path_stem);
        struct CP_ConfigFile labels_file;
        int label_text = 0;
        uint8_t* label_bytes = slurp(label_path, &label_text);
        if( !label_bytes )
            return NULL;

        char* wrapped = malloc((size_t)label_text + 16);
        if( !wrapped )
        {
            free(label_bytes);
            return NULL;
        }
        int prefix = snprintf(wrapped, 16, "[labels]\n");
        memcpy(wrapped + prefix, label_bytes, (size_t)label_text);
        free(label_bytes);
        int parsed = cp_config_file_load_memory(&labels_file, wrapped,
                                                (size_t)(prefix + label_text), "labels");
        free(wrapped);
        if( !parsed || labels_file.count != 1 )
        {
            if( parsed )
                cp_config_file_free(&labels_file);
            return NULL;
        }

        const struct CP_Config* block = &labels_file.configs[0];
        struct RSCache_Buffer out;
        if( !RSCache_BufferInitAlloc(&out, 4096) )
        {
            cp_config_file_free(&labels_file);
            return NULL;
        }
        p2(&out, block->count);
        int ok = 1;
        for( int i = 0; i < block->count && ok; i++ )
        {
            char scratch[512];
            char* fields[4];
            const char* value = block->lines[i].value;
            int x = 0, y = 0, size = 0;

            if( cp_indexed_key(block->lines[i].key, "label") < 0 )
            {
                ok = 0;
                break;
            }
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 4) != 4 ||
                !cp_parse_int(fields[1], &x) || !cp_parse_int(fields[2], &y) ||
                !cp_parse_int(fields[3], &size) )
            {
                ok = 0;
                break;
            }
            for( const char* c = fields[0]; *c; c++ )
                p1(&out, (uint8_t)*c);
            p1(&out, 0);
            p2(&out, x);
            p2(&out, y);
            p1(&out, size);
        }
        cp_config_file_free(&labels_file);
        if( !ok )
        {
            RSCache_BufferRelease(&out);
            fprintf(stderr, "cachepack: %s: a label line is not name,x,y,size\n", label_path);
            return NULL;
        }
        uint8_t* payload = malloc(out.position ? out.position : 1);
        if( payload )
        {
            memcpy(payload, out.data, out.position);
            *out_size = (int)out.position;
        }
        RSCache_BufferRelease(&out);
        return payload;
    }

    char path[1800];
    snprintf(path, sizeof(path), "%s.%s", path_stem, ext);
    struct CP_ConfigFile file;
    if( !cp_config_file_load(&file, path) )
        return NULL;

    struct LC_Pack members;
    cp_member_pack_load(&members, path_stem, "compack", "map");
    if( members.max == 0 )
    {
        fprintf(stderr, "cachepack: %s: no .compack beside it — cannot tell which map is "
                        "which file\n",
                path);
        cp_config_file_free(&file);
        return NULL;
    }

    struct RSCache_FileList list;
    memset(&list, 0, sizeof(list));
    list.files = calloc((size_t)file.count, sizeof(char*));
    list.file_sizes = calloc((size_t)file.count, sizeof(int));
    int* ids = calloc((size_t)file.count, sizeof(int));
    uint8_t* payload = NULL;
    int ok = list.files && list.file_sizes && ids;
    int flags = RSCache_WorldMapFlags(&ctx->profile);

    for( int i = 0; i < file.count && ok; i++ )
    {
        const struct CP_Config* block = &file.configs[i];
        int file_id = lc_pack_find(&members, block->debugname);
        struct RSCache_WorldMapArea area;

        if( file_id < 0 )
        {
            fprintf(stderr, "cachepack: %s: [%s] is not in the .compack\n", path,
                    block->debugname);
            ok = 0;
            break;
        }
        ids[i] = file_id;

        if( !worldmap_read_block(block, file_id, &area) )
        {
            fprintf(stderr, "cachepack: %s: map [%s] has a line this grammar does not "
                            "have\n",
                    path, block->debugname);
            RSCache_WorldMapAreaFreeInplace(&area);
            ok = 0;
            break;
        }

        uint32_t bound = composite ? RSCache_WorldMapAreaEncodeIconsBound(&area)
                                   : RSCache_WorldMapAreaEncodeBound(&area);
        uint8_t* bytes = malloc(bound ? bound : 1);
        uint32_t written = 0;
        if( bytes )
            written = composite ? RSCache_WorldMapAreaEncodeIcons(&area, flags, bytes, bound)
                                : RSCache_WorldMapAreaEncode(&area, bytes, bound);
        RSCache_WorldMapAreaFreeInplace(&area);
        if( written == 0 )
        {
            free(bytes);
            fprintf(stderr, "cachepack: %s: map [%s] failed to encode\n", path,
                    block->debugname);
            ok = 0;
            break;
        }
        list.files[i] = (char*)bytes;
        list.file_sizes[i] = (int)written;
        list.file_count++;
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
    lc_pack_free(&members);
    cp_config_file_free(&file);
    return payload;
}



/* ------------------------------------------------------------------ */
/* worldmap geography                                                  */
/* ------------------------------------------------------------------ */
/*
 * A world-map geography file is a bare sequence of tiles — no header, no count, no
 * dimensions. What ends it is the end of the file, so the decoder reads until the
 * bytes run out, and "exact consumption" is the only thing that says the grammar is
 * right. 1,981 of osrs239's members come to 4,096 tiles (a 64 x 64 square) and 165
 * to a multiple of 64 (whole 8 x 8 zones); nothing lands anywhere else.
 *
 * One tile:
 *
 *     u8  flags       bit0 short form, bit1 overlay, bit2 locs, bits 3-4 levels-1
 *     flags == 0      an empty tile, and nothing follows
 *
 *   short form (bit0):
 *     u16 overlay     only when bit1
 *     u16 underlay
 *
 *   long form:
 *     u16 underlay
 *     when bit1:      u8 count, then count x { u16 overlay; u8 shape<<2|rot if != 0 }
 *     when bit2:      levels x { u8 count, then count x { u32 loc; u8 shape<<2|rot } }
 *
 * **The loc id is a plain u32, not a smart.** The deobfuscated client reads it with
 * its 2-or-4-byte "bigsmart", and following that put 883 of 2,101 files into a state
 * where they ran out of bytes mid-tile and 355 parsed by luck. Reading it as a u32
 * takes that to 2,057 of 2,057 — every single-file archive, exactly. The deob is a
 * later revision than this cache, and the field was widened somewhere between them;
 * where the two disagree the bytes win.
 *
 * The other 44 archives hold two or three files, and they only became readable once
 * the table was split (`CP_ASSET_SPLIT`): stored whole, a `.wmg` was its members
 * concatenated *plus the container's trailer*, so no tile decoder could have read
 * it. Those 44 were the entire residue — the grammar was never the problem.
 *
 * Bits 5-7 of the flags byte are never set anywhere in the table, and the levels
 * bits are meaningful even when bit2 is clear (83,922 tiles), so `levels` is written
 * out rather than derived from the presence of a loc list.
 */

#define CP_GEO_MAX_LEVELS 4

struct CP_GeoLoc
{
    uint32_t id;
    uint8_t attr;
};

struct CP_GeoOverlay
{
    uint16_t id;
    uint8_t attr; /* unread when id == 0: the encoder writes no byte for it */
};

struct CP_GeoTile
{
    uint8_t flags;
    uint16_t underlay;
    uint16_t overlay; /* short form only */
    int overlay_count;
    struct CP_GeoOverlay* overlays;
    int loc_count[CP_GEO_MAX_LEVELS];
    struct CP_GeoLoc* locs[CP_GEO_MAX_LEVELS];
};

struct CP_GeoFile
{
    struct CP_GeoTile* tiles;
    int tile_count;
};

static void
geo_file_free(struct CP_GeoFile* file)
{
    for( int i = 0; i < file->tile_count; i++ )
    {
        free(file->tiles[i].overlays);
        for( int lv = 0; lv < CP_GEO_MAX_LEVELS; lv++ )
            free(file->tiles[i].locs[lv]);
    }
    free(file->tiles);
    memset(file, 0, sizeof(*file));
}

/** Reads tiles until the bytes run out. 0 when a read would pass the end. */
static int
geo_decode(const uint8_t* data, int size, struct CP_GeoFile* out)
{
    int pos = 0, capacity = 0;

    memset(out, 0, sizeof(*out));
    while( pos < size )
    {
        struct CP_GeoTile* tile;
        int levels;

        if( out->tile_count == capacity )
        {
            int next = capacity ? capacity * 2 : 256;
            struct CP_GeoTile* grown =
                (struct CP_GeoTile*)realloc(out->tiles, (size_t)next * sizeof(*grown));
            if( !grown )
                goto fail;
            out->tiles = grown;
            capacity = next;
        }
        tile = &out->tiles[out->tile_count++];
        memset(tile, 0, sizeof(*tile));

        tile->flags = data[pos++];
        if( tile->flags == 0 )
            continue;
        if( tile->flags & 0xE0 )
            goto fail; /* never set in any osrs239 member; a set bit means drift */

        if( tile->flags & 1 )
        {
            if( tile->flags & 2 )
            {
                if( pos + 2 > size )
                    goto fail;
                tile->overlay = (uint16_t)((data[pos] << 8) | data[pos + 1]);
                pos += 2;
            }
            if( pos + 2 > size )
                goto fail;
            tile->underlay = (uint16_t)((data[pos] << 8) | data[pos + 1]);
            pos += 2;
            continue;
        }

        levels = ((tile->flags & 24) >> 3) + 1;
        if( pos + 2 > size )
            goto fail;
        tile->underlay = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        pos += 2;

        if( tile->flags & 2 )
        {
            if( pos >= size )
                goto fail;
            tile->overlay_count = data[pos++];
            if( tile->overlay_count )
            {
                tile->overlays = (struct CP_GeoOverlay*)calloc((size_t)tile->overlay_count,
                                                               sizeof(*tile->overlays));
                if( !tile->overlays )
                    goto fail;
            }
            for( int i = 0; i < tile->overlay_count; i++ )
            {
                if( pos + 2 > size )
                    goto fail;
                tile->overlays[i].id = (uint16_t)((data[pos] << 8) | data[pos + 1]);
                pos += 2;
                if( tile->overlays[i].id != 0 )
                {
                    if( pos >= size )
                        goto fail;
                    tile->overlays[i].attr = data[pos++];
                }
            }
        }

        if( tile->flags & 4 )
        {
            for( int lv = 0; lv < levels; lv++ )
            {
                if( pos >= size )
                    goto fail;
                tile->loc_count[lv] = data[pos++];
                if( tile->loc_count[lv] )
                {
                    tile->locs[lv] = (struct CP_GeoLoc*)calloc((size_t)tile->loc_count[lv],
                                                               sizeof(**tile->locs));
                    if( !tile->locs[lv] )
                        goto fail;
                }
                for( int i = 0; i < tile->loc_count[lv]; i++ )
                {
                    if( pos + 5 > size )
                        goto fail;
                    tile->locs[lv][i].id =
                        ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos + 1] << 16) |
                        ((uint32_t)data[pos + 2] << 8) | (uint32_t)data[pos + 3];
                    tile->locs[lv][i].attr = data[pos + 4];
                    pos += 5;
                }
            }
        }
    }
    return pos == size;

fail:
    geo_file_free(out);
    return 0;
}

/** Writes what `geo_decode` read, byte for byte. Returns bytes written, 0 on error. */
static int
geo_encode(const struct CP_GeoFile* file, uint8_t* out, int capacity)
{
    int pos = 0;

#define CP_GEO_PUT(byte)                                                                 \
    do                                                                                   \
    {                                                                                    \
        if( pos >= capacity )                                                            \
            return 0;                                                                    \
        out[pos++] = (uint8_t)(byte);                                                    \
    } while( 0 )

    for( int i = 0; i < file->tile_count; i++ )
    {
        const struct CP_GeoTile* tile = &file->tiles[i];
        int levels = ((tile->flags & 24) >> 3) + 1;

        CP_GEO_PUT(tile->flags);
        if( tile->flags == 0 )
            continue;
        if( tile->flags & 1 )
        {
            if( tile->flags & 2 )
            {
                CP_GEO_PUT(tile->overlay >> 8);
                CP_GEO_PUT(tile->overlay & 0xff);
            }
            CP_GEO_PUT(tile->underlay >> 8);
            CP_GEO_PUT(tile->underlay & 0xff);
            continue;
        }
        CP_GEO_PUT(tile->underlay >> 8);
        CP_GEO_PUT(tile->underlay & 0xff);
        if( tile->flags & 2 )
        {
            CP_GEO_PUT(tile->overlay_count);
            for( int e = 0; e < tile->overlay_count; e++ )
            {
                CP_GEO_PUT(tile->overlays[e].id >> 8);
                CP_GEO_PUT(tile->overlays[e].id & 0xff);
                if( tile->overlays[e].id != 0 )
                    CP_GEO_PUT(tile->overlays[e].attr);
            }
        }
        if( tile->flags & 4 )
        {
            for( int lv = 0; lv < levels; lv++ )
            {
                CP_GEO_PUT(tile->loc_count[lv]);
                for( int e = 0; e < tile->loc_count[lv]; e++ )
                {
                    uint32_t id = tile->locs[lv][e].id;
                    CP_GEO_PUT(id >> 24);
                    CP_GEO_PUT(id >> 16);
                    CP_GEO_PUT(id >> 8);
                    CP_GEO_PUT(id);
                    CP_GEO_PUT(tile->locs[lv][e].attr);
                }
            }
        }
    }
#undef CP_GEO_PUT
    return pos;
}

/**
 * One tile per line, empty tiles skipped, so a 4,096-tile square that is mostly one
 * repeated overlay is a few hundred lines rather than four thousand.
 *
 * The index is the tile's position in the sequence and is written explicitly, which
 * is what lets the empty ones be omitted: on the way back in, a gap is empty tiles.
 * `shape;rot` is `attr >> 2` and `attr & 3`, the same spelling the jm2 uses.
 */
static void
geo_emit(FILE* out, const struct CP_GeoFile* file)
{
    fprintf(out,
            "==== GEOGRAPHY ====\n"
            "// One line per non-empty tile, `<index>: <fields>`. A missing index is an\n"
            "// empty tile. 4096 is a 64x64 square; a multiple of 64 is that many 8x8\n"
            "// zones.\n"
            "//\n"
            "//   s          short form: an underlay and at most one overlay\n"
            "//   u<n>       underlay\n"
            "//   o<n>       overlay (short form)\n"
            "//   lv<n>      how many loc levels the flags declare, 1-4\n"
            "//   ov:<list>  overlay list, `id;shape;rot` or bare `0` for an empty slot\n"
            "//   L<n>:<l>   locs on level n, `id;shape;rot`; `L<n>:` alone is none\n"
            "\n"
            /*
             * Stated, not counted from the lines.
             *
             * Empty tiles are omitted, so the trailing ones leave no trace — and a
             * geography file is a bare stream with no count of its own, so a text form
             * that stopped at the last non-empty tile would encode a shorter member and
             * the cache would take it. 179 of 2,101 members end in an empty tile.
             */
            "tiles=%d\n",
            file->tile_count);
    for( int i = 0; i < file->tile_count; i++ )
    {
        const struct CP_GeoTile* tile = &file->tiles[i];
        int levels = ((tile->flags & 24) >> 3) + 1;

        if( tile->flags == 0 )
            continue;
        fprintf(out, "%d:", i);
        if( tile->flags & 1 )
        {
            fprintf(out, " s u%u", tile->underlay);
            if( tile->flags & 2 )
                fprintf(out, " o%u", tile->overlay);
            fprintf(out, "\n");
            continue;
        }
        fprintf(out, " u%u lv%d", tile->underlay, levels);
        if( tile->flags & 2 )
        {
            fprintf(out, " ov:");
            for( int e = 0; e < tile->overlay_count; e++ )
            {
                if( e )
                    fprintf(out, ",");
                if( tile->overlays[e].id == 0 )
                    fprintf(out, "0");
                else
                    fprintf(out, "%u;%u;%u", tile->overlays[e].id,
                            tile->overlays[e].attr >> 2, tile->overlays[e].attr & 3);
            }
        }
        if( tile->flags & 4 )
        {
            for( int lv = 0; lv < levels; lv++ )
            {
                fprintf(out, " L%d:", lv);
                for( int e = 0; e < tile->loc_count[lv]; e++ )
                {
                    if( e )
                        fprintf(out, ",");
                    fprintf(out, "%u;%u;%u", tile->locs[lv][e].id,
                            tile->locs[lv][e].attr >> 2, tile->locs[lv][e].attr & 3);
                }
            }
        }
        fprintf(out, "\n");
    }
}

/** Reads back what `geo_emit` wrote. 0 on any line it cannot account for. */
static int
geo_parse(char* text, struct CP_GeoFile* out)
{
    char* cursor = text;
    int capacity = 0;

    memset(out, 0, sizeof(*out));
    while( cursor && *cursor )
    {
        char* newline = strchr(cursor, '\n');
        char* line = cursor;
        char* colon;
        int index;
        struct CP_GeoTile tile;
        int levels = 1;
        char* token;

        if( newline )
            *newline = '\0';
        cursor = newline ? newline + 1 : NULL;

        while( *line == ' ' || *line == '\t' )
            line++;
        if( !*line || line[0] == '=' || (line[0] == '/' && line[1] == '/') )
            continue;

        if( strncmp(line, "tiles=", 6) == 0 )
        {
            int stated = atoi(line + 6);

            if( stated < 0 )
                goto fail;
            while( out->tile_count < stated )
            {
                if( out->tile_count == capacity )
                {
                    int next = capacity ? capacity * 2 : 256;
                    struct CP_GeoTile* grown = (struct CP_GeoTile*)realloc(
                        out->tiles, (size_t)next * sizeof(*grown));
                    if( !grown )
                        goto fail;
                    out->tiles = grown;
                    capacity = next;
                }
                memset(&out->tiles[out->tile_count++], 0, sizeof(*out->tiles));
            }
            continue;
        }

        colon = strchr(line, ':');
        if( !colon )
            goto fail;
        *colon = '\0';
        index = atoi(line);
        if( index < 0 )
            goto fail;

        memset(&tile, 0, sizeof(tile));
        /* Grow to the stated index: the gap is empty tiles, which is why they can be
         * left out of the file at all. */
        while( out->tile_count <= index )
        {
            if( out->tile_count == capacity )
            {
                int next = capacity ? capacity * 2 : 256;
                struct CP_GeoTile* grown =
                    (struct CP_GeoTile*)realloc(out->tiles, (size_t)next * sizeof(*grown));
                if( !grown )
                    goto fail;
                out->tiles = grown;
                capacity = next;
            }
            memset(&out->tiles[out->tile_count++], 0, sizeof(*out->tiles));
        }

        for( token = strtok(colon + 1, " \t"); token; token = strtok(NULL, " \t") )
        {
            if( strcmp(token, "s") == 0 )
                tile.flags |= 1;
            else if( token[0] == 'u' )
                tile.underlay = (uint16_t)strtoul(token + 1, NULL, 10);
            else if( token[0] == 'o' && token[1] != 'v' )
            {
                tile.flags |= 2;
                tile.overlay = (uint16_t)strtoul(token + 1, NULL, 10);
            }
            else if( strncmp(token, "lv", 2) == 0 )
                levels = atoi(token + 2);
            else if( strncmp(token, "ov:", 3) == 0 )
            {
                char* entry = token + 3;

                tile.flags |= 2;
                while( *entry )
                {
                    char* comma = strchr(entry, ',');
                    unsigned id = 0, shape = 0, rot = 0;
                    struct CP_GeoOverlay* grown;

                    if( comma )
                        *comma = '\0';
                    if( sscanf(entry, "%u;%u;%u", &id, &shape, &rot) != 3 &&
                        sscanf(entry, "%u", &id) != 1 )
                        goto fail;
                    grown = (struct CP_GeoOverlay*)realloc(
                        tile.overlays, (size_t)(tile.overlay_count + 1) * sizeof(*grown));
                    if( !grown )
                        goto fail;
                    tile.overlays = grown;
                    tile.overlays[tile.overlay_count].id = (uint16_t)id;
                    tile.overlays[tile.overlay_count].attr = (uint8_t)(shape * 4 + rot);
                    tile.overlay_count++;
                    entry = comma ? comma + 1 : entry + strlen(entry);
                }
            }
            else if( token[0] == 'L' )
            {
                char* body = strchr(token, ':');
                int lv;

                if( !body )
                    goto fail;
                *body++ = '\0';
                lv = atoi(token + 1);
                if( lv < 0 || lv >= CP_GEO_MAX_LEVELS )
                    goto fail;
                tile.flags |= 4;
                while( *body )
                {
                    char* comma = strchr(body, ',');
                    unsigned id = 0, shape = 0, rot = 0;
                    struct CP_GeoLoc* grown;

                    if( comma )
                        *comma = '\0';
                    if( sscanf(body, "%u;%u;%u", &id, &shape, &rot) != 3 )
                        goto fail;
                    grown = (struct CP_GeoLoc*)realloc(
                        tile.locs[lv], (size_t)(tile.loc_count[lv] + 1) * sizeof(*grown));
                    if( !grown )
                        goto fail;
                    tile.locs[lv] = grown;
                    tile.locs[lv][tile.loc_count[lv]].id = id;
                    tile.locs[lv][tile.loc_count[lv]].attr = (uint8_t)(shape * 4 + rot);
                    tile.loc_count[lv]++;
                    body = comma ? comma + 1 : body + strlen(body);
                }
            }
            else
                goto fail;
        }
        if( levels < 1 || levels > CP_GEO_MAX_LEVELS )
            goto fail;
        if( !(tile.flags & 1) )
            tile.flags |= (uint8_t)((levels - 1) << 3);
        out->tiles[index] = tile;
    }
    return 1;

fail:
    geo_file_free(out);
    return 0;
}

/**
 * The tile count is not in the file, so a text form that ends early would encode a
 * short member and the cache would take it. Every member is re-encoded and compared
 * before its text is kept — the same guarantee the jm2 gets, for the same reason.
 */
static int
geo_roundtrips(const struct CP_GeoFile* file, const uint8_t* original, int size)
{
    uint8_t* check = (uint8_t*)malloc((size_t)size ? (size_t)size : 1);
    int written;
    int same;

    if( !check )
        return 0;
    written = geo_encode(file, check, size);
    same = written == size && memcmp(check, original, (size_t)size) == 0;
    free(check);
    return same;
}

static int
geo_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    struct RSCache_FileList* files = NULL;
    const uint8_t* single = payload;
    int single_size = size;
    struct LC_Pack members;
    char root_dir[1700];
    const char* stem_name;
    int wrote = 0;

    (void)ctx;
    (void)record_id;

    if( file_count > 1 )
    {
        files = RSCache_FileListNewFromDecode((char*)payload, size, file_count);
        if( !files )
            return 0;
    }

    snprintf(root_dir, sizeof(root_dir), "%s", path_stem);
    {
        char* cut = strrchr(root_dir, '/');
        if( cut )
            *cut = '\0';
        else
            snprintf(root_dir, sizeof(root_dir), ".");
    }
    stem_name = strrchr(path_stem, '/');
    stem_name = stem_name ? stem_name + 1 : path_stem;
    cp_member_pack_load(&members, path_stem, "filepack", "worldmapgeo");

    for( int f = 0; f < (files ? files->file_count : 1); f++ )
    {
        const uint8_t* bytes = files ? (const uint8_t*)files->files[f] : single;
        int bytes_size = files ? files->file_sizes[f] : single_size;
        int id = file_ids ? file_ids[f] : f;
        struct CP_GeoFile decoded;
        char path[1900];
        char member[256];
        FILE* out;

        if( !geo_decode(bytes, bytes_size, &decoded) )
            goto give_up;
        if( !geo_roundtrips(&decoded, bytes, bytes_size) )
        {
            geo_file_free(&decoded);
            goto give_up;
        }
        if( files )
            snprintf(member, sizeof(member), "%s/%d.wmg", stem_name, id);
        else
            snprintf(member, sizeof(member), "%s.wmg", stem_name);
        snprintf(path, sizeof(path), "%s/%s", root_dir, member);
        if( files )
        {
            char dir[1900];
            snprintf(dir, sizeof(dir), "%s/%s", root_dir, stem_name);
            ensure_dir_path(dir);
        }
        out = fopen(path, "wb");
        if( !out )
        {
            geo_file_free(&decoded);
            goto give_up;
        }
        geo_emit(out, &decoded);
        fclose(out);
        geo_file_free(&decoded);
        if( files )
            lc_pack_set(&members, id, member);
        wrote++;
    }

    if( files && wrote )
        cp_member_pack_save(&members, path_stem, "filepack");
    lc_pack_free(&members);
    RSCache_FileListFree(files);
    return wrote > 0;

give_up:
    lc_pack_free(&members);
    RSCache_FileListFree(files);
    return 0;
}

static uint8_t*
geo_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char root_dir[1700];
    struct LC_Pack members;
    struct RSCache_FileList list;
    int* ids = NULL;
    uint8_t* payload = NULL;
    int capacity;

    (void)ctx;
    (void)record_id;

    snprintf(root_dir, sizeof(root_dir), "%s", path_stem);
    {
        char* cut = strrchr(root_dir, '/');
        if( cut )
            *cut = '\0';
        else
            snprintf(root_dir, sizeof(root_dir), ".");
    }
    cp_member_pack_load(&members, path_stem, "filepack", "worldmapgeo");

    memset(&list, 0, sizeof(list));
    capacity = members.max > 0 ? members.max + 1 : 1;
    list.files = (char**)calloc((size_t)capacity, sizeof(char*));
    list.file_sizes = (int*)calloc((size_t)capacity, sizeof(int));
    ids = (int*)calloc((size_t)capacity, sizeof(int));
    if( !list.files || !list.file_sizes || !ids )
        goto done;

    for( int id = 0; id < capacity; id++ )
    {
        const char* member = NULL;
        char member_path[1900];
        char fallback[256];
        int text_size = 0;
        char* text;
        struct CP_GeoFile decoded;
        uint8_t* bytes;
        int written, bound;

        if( members.max > 0 )
        {
            if( id >= members.capacity || !members.names || !members.names[id] )
                continue;
            member = members.names[id];
        }
        else
        {
            /* A one-file archive has no filepack: the member is the `.wmg` itself. */
            const char* stem_name = strrchr(path_stem, '/');
            snprintf(fallback, sizeof(fallback), "%s.wmg",
                     stem_name ? stem_name + 1 : path_stem);
            member = fallback;
        }
        snprintf(member_path, sizeof(member_path), "%s/%s", root_dir, member);
        text = (char*)slurp(member_path, &text_size);
        if( !text )
            goto done;
        if( !geo_parse(text, &decoded) )
        {
            free(text);
            goto done;
        }
        free(text);

        /* One tile is at most 1 + 2 + 1 + 255*3 + 4*(1 + 255*5) — bounded, so a
         * generous per-tile allowance beats a second encode pass to measure. */
        bound = decoded.tile_count * 6 + 1;
        for( int i = 0; i < decoded.tile_count; i++ )
        {
            bound += decoded.tiles[i].overlay_count * 3;
            for( int lv = 0; lv < CP_GEO_MAX_LEVELS; lv++ )
                bound += 1 + decoded.tiles[i].loc_count[lv] * 5;
        }
        bytes = (uint8_t*)malloc((size_t)bound);
        if( !bytes )
        {
            geo_file_free(&decoded);
            goto done;
        }
        written = geo_encode(&decoded, bytes, bound);
        geo_file_free(&decoded);
        if( written <= 0 )
        {
            free(bytes);
            goto done;
        }
        list.files[list.file_count] = (char*)bytes;
        list.file_sizes[list.file_count] = written;
        ids[list.file_count] = id;
        list.file_count++;
        if( members.max <= 0 )
            break;
    }

    if( list.file_count == 0 )
        goto done;
    if( list.file_count == 1 )
    {
        /* A single-file archive's payload is the file, with no FileList framing. */
        payload = (uint8_t*)list.files[0];
        *out_size = list.file_sizes[0];
        list.files[0] = NULL;
        *out_file_count = 1;
        *out_file_ids = ids;
        ids = NULL;
    }
    else
    {
        uint32_t bound = RSCache_FileListEncodeBound(&list);
        payload = (uint8_t*)malloc(bound ? bound : 1);
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
                *out_file_count = list.file_count;
                *out_file_ids = ids;
                ids = NULL;
            }
        }
    }

done:
    for( int i = 0; i < list.file_count; i++ )
        free(list.files[i]);
    free(list.files);
    free(list.file_sizes);
    free(ids);
    lc_pack_free(&members);
    return payload;
}


/* ------------------------------------------------------------------ */
/* dbindex                                                             */
/* ------------------------------------------------------------------ */
/*
 * A dbtable's inverted indexes: what `DB_FIND` scans to answer "which rows have
 * this value in this column".
 *
 * The archive is one table's worth. File 0 is the master — every row id, which is
 * what `DB_FINDALL` returns — and file N is the index for column N-1. So
 * `dbindex_0` file 34 is column 33 of table 0 (quest).
 *
 * One file:
 *
 *     varint2 tuple_size          how many field positions this column indexes
 *     repeat tuple_size:
 *       u8      base_type         0 int, 1 long, 2 string
 *       varint2 entry_count
 *       repeat entry_count:
 *         value                   g4 / g8 / NUL-terminated string, per base_type
 *         varint2 row_count
 *         repeat row_count: varint2 row_id
 *
 * A column with several typed fields gets several tuple positions, which is why
 * column 33 has `tuple_size=2` — its declared types are 17 and 0.
 *
 * The text form is one file per archive, blocks per member, in the shape textures
 * and interfaces use:
 *
 *     [column_33]
 *     base=0:int
 *     base=1:int
 *     index=0:0:23,46,51
 *     index=1:32000:61
 *
 * `base=` is written for *every* tuple position including the empty ones —
 * `dbindex_188` file 2 is seven empty int slots and nothing else, so the positions
 * are the only thing that file contains. A position with no `index=` line has
 * entry_count 0. Entries stay in binary order because `DB_FIND` scans linearly and
 * sorting them would change what the cache says.
 *
 * Ints print signed, matching dbrow, so 0xffffffff reads as -1. Strings are quoted,
 * because a raw one could contain the `:` that separates the fields.
 *
 * **This is derived data, not authored content.** It is a projection of the dbrows,
 * so the readable form is for inspection and CS2 debugging rather than a source of
 * truth — but it still round-trips byte-exactly, and is only written when it does.
 */

static const char*
dbindex_base_name(int base_type)
{
    switch( base_type )
    {
    case RSCACHE_DB_BASE_LONG:
        return "long";
    case RSCACHE_DB_BASE_STRING:
        return "string";
    default:
        return "int";
    }
}

static int
dbindex_base_type(const char* name)
{
    if( strcmp(name, "long") == 0 )
        return RSCACHE_DB_BASE_LONG;
    if( strcmp(name, "string") == 0 )
        return RSCACHE_DB_BASE_STRING;
    if( strcmp(name, "int") == 0 )
        return RSCACHE_DB_BASE_INT;
    return -1;
}

/** Bytes for one decoded index file. 0 when it would not fit in `capacity`. */
static int
dbindex_encode(
    const struct RSCache_DbIndexFile* file,
    uint8_t* out,
    int capacity)
{
    struct RSCache_Buffer buf;

    RSCache_BufferInit(&buf, out, (uint32_t)capacity);
    RSCache_BufferWriteVarInt2(&buf, file->tuple_size);
    for( int tp = 0; tp < file->tuple_size; tp++ )
    {
        const struct RSCache_DbIndexTuple* tuple = &file->tuples[tp];

        RSCache_BufferP1(&buf, tuple->base_type);
        RSCache_BufferWriteVarInt2(&buf, tuple->entry_count);
        for( int e = 0; e < tuple->entry_count; e++ )
        {
            const struct RSCache_DbIndexEntry* ent = &tuple->entries[e];

            switch( tuple->base_type )
            {
            case RSCACHE_DB_BASE_STRING:
                for( const char* c = ent->string_value ? ent->string_value : ""; *c; c++ )
                    RSCache_BufferP1(&buf, (uint8_t)*c);
                RSCache_BufferP1(&buf, 0);
                break;
            case RSCACHE_DB_BASE_LONG:
                RSCache_BufferP8(&buf, ent->long_value);
                break;
            default:
                RSCache_BufferP4(&buf, ent->int_value);
                break;
            }
            RSCache_BufferWriteVarInt2(&buf, ent->row_count);
            for( int r = 0; r < ent->row_count; r++ )
                RSCache_BufferWriteVarInt2(&buf, ent->row_ids[r]);
        }
    }
    if( buf.position > (uint32_t)capacity )
        return 0;
    return (int)buf.position;
}

static void
dbindex_emit(FILE* out, const struct RSCache_DbIndexFile* file)
{
    for( int tp = 0; tp < file->tuple_size; tp++ )
        fprintf(out, "base=%d:%s\n", tp, dbindex_base_name(file->tuples[tp].base_type));
    for( int tp = 0; tp < file->tuple_size; tp++ )
    {
        const struct RSCache_DbIndexTuple* tuple = &file->tuples[tp];

        for( int e = 0; e < tuple->entry_count; e++ )
        {
            const struct RSCache_DbIndexEntry* ent = &tuple->entries[e];

            fprintf(out, "index=%d:", tp);
            switch( tuple->base_type )
            {
            case RSCACHE_DB_BASE_STRING:
                fprintf(out, "\"");
                for( const char* c = ent->string_value ? ent->string_value : ""; *c; c++ )
                {
                    if( *c == '"' || *c == '\\' )
                        fprintf(out, "\\");
                    fprintf(out, "%c", *c);
                }
                fprintf(out, "\"");
                break;
            case RSCACHE_DB_BASE_LONG:
                fprintf(out, "%lld", (long long)ent->long_value);
                break;
            default:
                fprintf(out, "%d", ent->int_value);
                break;
            }
            fprintf(out, ":");
            for( int r = 0; r < ent->row_count; r++ )
                fprintf(out, "%s%d", r ? "," : "", ent->row_ids[r]);
            fprintf(out, "\n");
        }
    }
}

/** Grow `file` so tuple position `tp` exists. 0 on allocation failure. */
static int
dbindex_ensure_tuple(struct RSCache_DbIndexFile* file, int tp)
{
    struct RSCache_DbIndexTuple* grown;

    if( tp < 0 || tp > 4096 )
        return 0;
    if( tp < file->tuple_size )
        return 1;
    grown = (struct RSCache_DbIndexTuple*)realloc(file->tuples,
                                                  (size_t)(tp + 1) * sizeof(*grown));
    if( !grown )
        return 0;
    file->tuples = grown;
    memset(&file->tuples[file->tuple_size], 0,
           (size_t)(tp + 1 - file->tuple_size) * sizeof(*grown));
    file->tuple_size = tp + 1;
    return 1;
}

/**
 * Reads one member's block back. 0 on any line it cannot account for.
 *
 * `value` is parsed by the position's declared base type, so a `base=` line must
 * come before the `index=` lines that use it — which is how the emitter writes them
 * and the only order in which a string containing `:` is unambiguous.
 */
static int
dbindex_parse_block(const struct CP_Config* block, struct RSCache_DbIndexFile* out)
{
    memset(out, 0, sizeof(*out));
    for( int i = 0; i < block->count; i++ )
    {
        const char* key = block->lines[i].key;
        char* value = block->lines[i].value;

        if( strcmp(key, "base") == 0 )
        {
            char type_name[32];
            int tp = -1;

            if( sscanf(value, "%d:%31s", &tp, type_name) != 2 )
                goto fail;
            if( !dbindex_ensure_tuple(out, tp) )
                goto fail;
            out->tuples[tp].base_type = dbindex_base_type(type_name);
            if( out->tuples[tp].base_type < 0 )
                goto fail;
        }
        else if( strcmp(key, "index") == 0 )
        {
            struct RSCache_DbIndexTuple* tuple;
            struct RSCache_DbIndexEntry* grown;
            struct RSCache_DbIndexEntry* ent;
            char* cursor = value;
            int tp = (int)strtol(cursor, &cursor, 10);

            if( *cursor != ':' || !dbindex_ensure_tuple(out, tp) )
                goto fail;
            cursor++;
            tuple = &out->tuples[tp];
            grown = (struct RSCache_DbIndexEntry*)realloc(
                tuple->entries, (size_t)(tuple->entry_count + 1) * sizeof(*grown));
            if( !grown )
                goto fail;
            tuple->entries = grown;
            ent = &tuple->entries[tuple->entry_count++];
            memset(ent, 0, sizeof(*ent));

            if( tuple->base_type == RSCACHE_DB_BASE_STRING )
            {
                char* text;
                int len = 0;

                if( *cursor != '"' )
                    goto fail;
                cursor++;
                text = (char*)malloc(strlen(cursor) + 1);
                if( !text )
                    goto fail;
                while( *cursor && *cursor != '"' )
                {
                    if( *cursor == '\\' && cursor[1] )
                        cursor++;
                    text[len++] = *cursor++;
                }
                text[len] = '\0';
                if( *cursor != '"' )
                {
                    free(text);
                    goto fail;
                }
                cursor++;
                ent->string_value = text;
            }
            else if( tuple->base_type == RSCACHE_DB_BASE_LONG )
                ent->long_value = (int64_t)strtoll(cursor, &cursor, 10);
            else
                ent->int_value = (int)strtol(cursor, &cursor, 10);

            if( *cursor != ':' )
                goto fail;
            cursor++;
            /* An empty tail is a value with no rows, which the encoder writes as
             * row_count 0 — not the same as the line being absent. */
            while( *cursor )
            {
                int* rows = (int*)realloc(ent->row_ids,
                                          (size_t)(ent->row_count + 1) * sizeof(int));

                if( !rows )
                    goto fail;
                ent->row_ids = rows;
                ent->row_ids[ent->row_count++] = (int)strtol(cursor, &cursor, 10);
                if( *cursor == ',' )
                    cursor++;
                else if( *cursor )
                    goto fail;
            }
        }
        else
            goto fail;
    }
    return 1;

fail:
    RSCache_Dat2DbIndexFileFreeInplace(out);
    return 0;
}

static int
dbindex_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    struct RSCache_FileList* files = NULL;
    struct LC_Pack members;
    char path[1900];
    FILE* out;
    int wrote = 0;

    (void)ctx;
    (void)record_id;

    files = file_count > 1 ? RSCache_FileListNewFromDecode((char*)payload, size, file_count)
                           : NULL;
    if( file_count > 1 && !files )
        return 0;

    cp_member_pack_load(&members, path_stem, "compack", "col");
    snprintf(path, sizeof(path), "%s.dbi", path_stem);
    out = fopen(path, "wb");
    if( !out )
    {
        lc_pack_free(&members);
        RSCache_FileListFree(files);
        return 0;
    }
    fprintf(out,
            "// A dbtable's inverted indexes — what DB_FIND scans.\n"
            "//\n"
            "// [master] is file 0, every row id in the table, which DB_FINDALL returns.\n"
            "// [column_N] is file N+1, the index for that column. A column with several\n"
            "// typed fields gets one tuple position per field.\n"
            "//\n"
            "//   base=<pos>:<int|long|string>   declared for every position, empty or not\n"
            "//   index=<pos>:<value>:<row>,...  entries in binary order; DB_FIND is linear\n"
            "//\n"
            "// Derived from the dbrows rather than authored — a readable view, not a\n"
            "// source of truth. %s.compack ties each block to its file id.\n\n",
            strrchr(path_stem, '/') ? strrchr(path_stem, '/') + 1 : path_stem);

    for( int f = 0; f < (files ? files->file_count : 1); f++ )
    {
        const uint8_t* bytes = files ? (const uint8_t*)files->files[f] : payload;
        int bytes_size = files ? files->file_sizes[f] : size;
        int id = file_ids ? file_ids[f] : f;
        struct RSCache_DbIndexFile decoded;
        uint8_t* check;
        int written;
        char member[64];

        RSCache_Dat2DbIndexFileDecodeInplace(&decoded, id, bytes, bytes_size);
        /*
         * Proved before it is written, like the jm2 and the geography: a stream this
         * does not understand still decodes to a plausible empty index, and the text
         * for it would encode a *shorter* member that the cache would accept.
         */
        check = (uint8_t*)malloc((size_t)bytes_size ? (size_t)bytes_size : 1);
        written = check ? dbindex_encode(&decoded, check, bytes_size) : 0;
        if( written != bytes_size || memcmp(check, bytes, (size_t)bytes_size) != 0 )
        {
            free(check);
            RSCache_Dat2DbIndexFileFreeInplace(&decoded);
            fclose(out);
            remove(path);
            lc_pack_free(&members);
            RSCache_FileListFree(files);
            return 0;
        }
        free(check);

        if( id == 0 )
            snprintf(member, sizeof(member), "master");
        else
            snprintf(member, sizeof(member), "column_%d", id - 1);
        fprintf(out, "[%s]\n", member);
        dbindex_emit(out, &decoded);
        fprintf(out, "\n");
        lc_pack_set(&members, id, member);
        RSCache_Dat2DbIndexFileFreeInplace(&decoded);
        wrote++;
    }
    fclose(out);
    if( wrote )
        cp_member_pack_save(&members, path_stem, "compack");
    lc_pack_free(&members);
    RSCache_FileListFree(files);
    return wrote > 0;
}

static uint8_t*
dbindex_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char path[1900];
    struct CP_ConfigFile file;
    struct LC_Pack members;
    struct RSCache_FileList list;
    int* ids = NULL;
    uint8_t* payload = NULL;

    (void)ctx;
    (void)record_id;

    snprintf(path, sizeof(path), "%s.dbi", path_stem);
    if( !cp_config_file_load(&file, path) )
        return NULL;
    cp_member_pack_load(&members, path_stem, "compack", "col");
    if( members.max == 0 )
    {
        fprintf(stderr, "cachepack: %s: no .compack beside it — cannot tell which "
                        "block is which file\n",
                path);
        cp_config_file_free(&file);
        lc_pack_free(&members);
        return NULL;
    }

    memset(&list, 0, sizeof(list));
    list.files = (char**)calloc((size_t)file.count, sizeof(char*));
    list.file_sizes = (int*)calloc((size_t)file.count, sizeof(int));
    ids = (int*)calloc((size_t)file.count, sizeof(int));
    if( !list.files || !list.file_sizes || !ids )
        goto done;

    for( int i = 0; i < file.count; i++ )
    {
        const struct CP_Config* block = &file.configs[i];
        struct RSCache_DbIndexFile decoded;
        int id = lc_pack_find(&members, block->debugname);
        uint8_t* bytes;
        int bound, written;

        if( id < 0 )
        {
            fprintf(stderr, "cachepack: %s: [%s] is not in the .compack\n", path,
                    block->debugname);
            goto done;
        }
        if( !dbindex_parse_block(block, &decoded) )
        {
            fprintf(stderr, "cachepack: %s: [%s] did not parse\n", path,
                    block->debugname);
            goto done;
        }
        /* Every field is bounded: a varint2 is at most 5 bytes, a value at most 8
         * plus a string's own length. */
        bound = 8 + decoded.tuple_size * 8;
        for( int tp = 0; tp < decoded.tuple_size; tp++ )
        {
            for( int e = 0; e < decoded.tuples[tp].entry_count; e++ )
            {
                const struct RSCache_DbIndexEntry* ent = &decoded.tuples[tp].entries[e];

                bound += 16 + ent->row_count * 5;
                if( ent->string_value )
                    bound += (int)strlen(ent->string_value);
            }
        }
        bytes = (uint8_t*)malloc((size_t)bound);
        written = bytes ? dbindex_encode(&decoded, bytes, bound) : 0;
        RSCache_Dat2DbIndexFileFreeInplace(&decoded);
        if( written <= 0 )
        {
            free(bytes);
            goto done;
        }
        list.files[list.file_count] = (char*)bytes;
        list.file_sizes[list.file_count] = written;
        ids[list.file_count] = id;
        list.file_count++;
    }

    if( list.file_count == 1 )
    {
        payload = (uint8_t*)list.files[0];
        *out_size = list.file_sizes[0];
        list.files[0] = NULL;
        *out_file_count = 1;
        *out_file_ids = ids;
        ids = NULL;
    }
    else if( list.file_count > 1 )
    {
        uint32_t bound = RSCache_FileListEncodeBound(&list);
        payload = (uint8_t*)malloc(bound ? bound : 1);
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
                *out_file_count = list.file_count;
                *out_file_ids = ids;
                ids = NULL;
            }
        }
    }

done:
    for( int i = 0; i < list.file_count; i++ )
        free(list.files[i]);
    free(list.files);
    free(list.file_sizes);
    free(ids);
    lc_pack_free(&members);
    cp_config_file_free(&file);
    return payload;
}


/* ------------------------------------------------------------------ */
/* font metrics                                                        */
/* ------------------------------------------------------------------ */
/*
 * Table 13 holds **metrics, not glyphs** — 256 advance widths and an ascent, and
 * nothing else. Every one of osrs239's 21 files is exactly 257 bytes, which is
 * `RSCACHE_DAT2_FONT_METRICS_V1_SIZE`.
 *
 * The pictures are in the sprite table: `p11_full`, `p12_full`, `b12_full` and the
 * rest are sprite archives of 194 members, one per character, and those already
 * export as BMPs — `sprites/p11_full/65.bmp` is a 5x8 capital A. So there is nothing
 * here to render; what this table needs is for its numbers to be readable.
 *
 *     ascent=11
 *     advance=65:5     // 'A'
 *
 * A width of 0 is written too, because the position is what gives the byte meaning
 * and a 257-byte file with lines missing would encode short. Printable characters
 * get their glyph in a trailing comment, which is the whole point of the exercise:
 * `advance=32:4` says nothing and `advance=32:4  // ' '` says it is the space.
 *
 * V2 (263 bytes, a two-byte header and the ascent at 258) is not written by this
 * cache. The reader handles it; this codec declines it rather than guessing at the
 * six bytes it would have to reproduce.
 */

static int
font_write(
    struct CP_Ctx* ctx,
    int record_id,
    const uint8_t* payload,
    int size,
    const int* file_ids,
    int file_count,
    const char* path_stem)
{
    struct RSCache_Dat2FontMetrics metrics;
    char path[1900];
    FILE* out;

    (void)ctx;
    (void)record_id;
    (void)file_ids;

    /* One payload, and only the layout this cache actually uses: anything else
     * falls through to the raw bytes rather than being half-described. */
    if( file_count > 1 || size != RSCACHE_DAT2_FONT_METRICS_V1_SIZE )
        return 0;
    if( !RSCache_Dat2FontMetricsDecodeCodec(payload, size, RSCACHE_CODEC_FONT_METRICS_V1,
                                            &metrics) )
        return 0;

    snprintf(path, sizeof(path), "%s.fm", path_stem);
    out = fopen(path, "wb");
    if( !out )
        return 0;
    fprintf(out,
            "// Font metrics — advance widths, not glyphs.\n"
            "//\n"
            "// The pictures live in the sprite table: p11_full, p12_full, b12_full and\n"
            "// friends are sprite archives with one member per character, already\n"
            "// exported as BMP. sprites/p11_full/65.bmp is a capital A.\n"
            "//\n"
            "//   ascent          baseline offset, one byte\n"
            "//   advance=<c>:<n> how far the pen moves after character c\n\n");
    /* A block header, because cp_config_file_load rejects a property before any
     * `[name]` — there is one font per file, so one block. */
    fprintf(out, "[metrics]\n");
    fprintf(out, "ascent=%d\n\n", metrics.ascent);
    for( int c = 0; c < 256; c++ )
    {
        fprintf(out, "advance=%d:%d", c, metrics.advance[c]);
        if( c > 32 && c < 127 )
            fprintf(out, "  // '%c'", (char)c);
        else if( c == 32 )
            fprintf(out, "  // ' '");
        fprintf(out, "\n");
    }
    fclose(out);
    return 1;
}

static uint8_t*
font_read(
    struct CP_Ctx* ctx,
    int record_id,
    const char* path_stem,
    int** out_file_ids,
    int* out_file_count,
    int* out_size)
{
    char path[1900];
    struct CP_ConfigFile file;
    uint8_t* payload;
    int ascent = -1;
    int seen[256];

    (void)ctx;
    (void)record_id;
    (void)out_file_ids;

    snprintf(path, sizeof(path), "%s.fm", path_stem);
    if( !cp_config_file_load(&file, path) )
        return NULL;
    payload = (uint8_t*)calloc(RSCACHE_DAT2_FONT_METRICS_V1_SIZE, 1);
    if( !payload )
    {
        cp_config_file_free(&file);
        return NULL;
    }
    memset(seen, 0, sizeof(seen));

    /* One `[metrics]` block per file. */
    for( int i = 0; i < file.count; i++ )
    {
        const struct CP_Config* block = &file.configs[i];

        for( int line = 0; line < block->count; line++ )
        {
            const char* key = block->lines[line].key;
            const char* value = block->lines[line].value;

            if( strcmp(key, "ascent") == 0 )
                ascent = atoi(value);
            else if( strcmp(key, "advance") == 0 )
            {
                int c = -1, width = 0;

                if( sscanf(value, "%d:%d", &c, &width) != 2 || c < 0 || c > 255 ||
                    width < 0 || width > 255 )
                    goto fail;
                payload[c] = (uint8_t)width;
                seen[c] = 1;
            }
            else
                goto fail;
        }
    }
    /* Every position, or the file describes a different font than it came from. */
    for( int c = 0; c < 256; c++ )
    {
        if( !seen[c] )
            goto fail;
    }
    if( ascent < 0 || ascent > 255 )
        goto fail;
    payload[256] = (uint8_t)ascent;
    *out_size = RSCACHE_DAT2_FONT_METRICS_V1_SIZE;
    *out_file_count = 1;
    cp_config_file_free(&file);
    return payload;

fail:
    fprintf(stderr, "cachepack: %s: expected `ascent` and 256 `advance` lines\n", path);
    free(payload);
    cp_config_file_free(&file);
    return NULL;
}

const struct CP_AssetCodec cp_codec_font = { "fm", NULL, font_write, font_read, 0 };

const struct CP_AssetCodec cp_codec_dbindex = { "dbi", NULL, dbindex_write, dbindex_read, 0 };

const struct CP_AssetCodec cp_codec_worldmapgeo = { "wmg", NULL, geo_write, geo_read, 0 };

/* Also writes `.wmc` (compositemap) and `.wml` (labels); `ext2` holds the one
 * that shares the archive-level slot. */
const struct CP_AssetCodec cp_codec_worldmap = { "wma", "wmc", worldmap_write, worldmap_read, 0 };
