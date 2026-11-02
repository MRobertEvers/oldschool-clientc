/**
 * The interface codec's typed layer: a `.if` block to an
 * RSCache_Dat2Component and back (editor_if.h).
 *
 * Both halves are cachepack's, key for key: the reader is interface_read's
 * read_component_line and the emitter is interface_write's emit_component
 * (3rd/rscache/tools/cachepack/cp_decode.c), restated here because that file
 * is a tool's and links its whole context. editor_if_test holds the two to
 * each other on every block in the tree: read, emit, compare bytes.
 *
 * Separate from editor_if.c because it links rscache's component type, and
 * the text layer must link into torirsmaped, which carries no rscache.
 */

#include "editor_if.h"

#include "datatypes/dat2_component.h"
#include "rscache_alloc.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- reading ------------------------------------------------------------- */

static int
read_int(
    const char* text,
    int* out)
{
    char* end = NULL;
    long value;

    if( !text || !*text )
        return 0;
    errno = 0;
    value = strtol(text, &end, 0);
    if( errno != 0 || end == text || *end != '\0' )
        return 0;
    if( value < -2147483647L - 1 || value > 2147483647L )
        return 0;
    *out = (int)value;
    return 1;
}

static int
read_bool(
    const char* text,
    bool* out)
{
    if( strcmp(text, "yes") == 0 || strcmp(text, "true") == 0 || strcmp(text, "1") == 0 )
    {
        *out = true;
        return 1;
    }
    if( strcmp(text, "no") == 0 || strcmp(text, "false") == 0 || strcmp(text, "0") == 0 )
    {
        *out = false;
        return 1;
    }
    return 0;
}

/** cp_unescape. */
static void
unescape(
    const char* value,
    char* out,
    size_t out_size)
{
    size_t w = 0;

    for( size_t i = 0; value[i] && w + 1 < out_size; i++ )
    {
        if( value[i] == '\\' && value[i + 1] )
        {
            char next = value[++i];
            out[w++] = next == 'n' ? '\n' : next == 'r' ? '\r' : next;
        }
        else
            out[w++] = value[i];
    }
    out[w] = '\0';
}

/** cp_split into at most `max` comma fields of `scratch`. */
static int
split(
    const char* text,
    char* scratch,
    char** fields,
    int max)
{
    int n = 0;
    char* p = scratch;

    strcpy(scratch, text);
    while( n < max )
    {
        char* comma;
        fields[n++] = p;
        comma = strchr(p, ',');
        if( !comma )
            break;
        *comma = '\0';
        p = comma + 1;
    }
    return n;
}

static int
read_hook(
    const char* value,
    struct RSCache_Dat2ComponentScriptVar** out,
    int32_t* out_len)
{
    struct Editor_IfHook* hook = malloc(sizeof(*hook));
    struct RSCache_Dat2ComponentScriptVar* args;

    assert(hook);
    if( !Editor_IfHookParse(value, hook) )
    {
        free(hook);
        return 0;
    }
    if( hook->arg_count == 0 )
    {
        free(hook);
        return 1;
    }
    args = RSCACHE_CALLOC((size_t)hook->arg_count, sizeof(*args));
    assert(args);
    for( int i = 0; i < hook->arg_count; i++ )
    {
        if( hook->args[i].is_string )
        {
            args[i].type = RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_STRING;
            args[i].value.s = RSCache_Strdup(hook->args[i].text);
            assert(args[i].value.s);
        }
        else
        {
            args[i].type = RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT;
            args[i].value.i = hook->args[i].value;
        }
    }
    /* A key stated twice: the later line wins, as in cachepack. */
    if( *out )
    {
        for( int32_t i = 0; i < *out_len; i++ )
        {
            if( (*out)[i].type == RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_STRING )
                RSCACHE_FREE((*out)[i].value.s);
        }
        RSCACHE_FREE(*out);
    }
    *out = args;
    *out_len = hook->arg_count;
    free(hook);
    return 1;
}

static int
read_triggers(
    const char* value,
    int32_t** out,
    int32_t* out_len)
{
    char buf[2048];
    char scratch[2048];
    char* fields[256];
    int count;
    int32_t* triggers;

    if( strlen(value) >= sizeof(buf) )
        return 0;
    snprintf(buf, sizeof(buf), "%s", value);
    if( !buf[0] )
        return 1;
    count = split(buf, scratch, fields, 256);
    triggers = RSCACHE_MALLOC((size_t)count * sizeof(int32_t));
    assert(triggers);
    for( int i = 0; i < count; i++ )
    {
        int parsed = 0;
        if( !read_int(fields[i], &parsed) )
        {
            RSCACHE_FREE(triggers);
            return 0;
        }
        triggers[i] = parsed;
    }
    RSCACHE_FREE(*out);
    *out = triggers;
    *out_len = count;
    return 1;
}

static void
set_string(
    char** field,
    const char* value)
{
    char text[4096];

    unescape(value, text, sizeof(text));
    RSCACHE_FREE(*field);
    *field = RSCache_Strdup(text);
    assert(*field);
}

#define READ_INT(k, f)                                                                            \
    if( strcmp(key, k) == 0 )                                                                     \
    {                                                                                             \
        int parsed = 0;                                                                           \
        if( !read_int(value, &parsed) )                                                           \
            return 0;                                                                             \
        c->f = parsed;                                                                            \
        return 1;                                                                                 \
    }
#define READ_BOOL(k, f)                                                                           \
    if( strcmp(key, k) == 0 )                                                                     \
    {                                                                                             \
        bool flag = false;                                                                        \
        if( !read_bool(value, &flag) )                                                            \
            return 0;                                                                             \
        c->f = flag;                                                                              \
        return 1;                                                                                 \
    }
#define READ_STR(k, f)                                                                            \
    if( strcmp(key, k) == 0 )                                                                     \
    {                                                                                             \
        set_string(&c->f, value);                                                                 \
        return 1;                                                                                 \
    }
#define READ_HOOK(k, f)                                                                           \
    if( strcmp(key, k) == 0 )                                                                     \
        return read_hook(value, &c->f, &c->f##Len);

static int
read_indexed_int_list(
    const char* value,
    int expected,
    int* out)
{
    char scratch[128];
    char* fields[3];

    if( strlen(value) >= sizeof(scratch) || split(value, scratch, fields, expected) != expected )
        return 0;
    for( int i = 0; i < expected; i++ )
    {
        if( !read_int(fields[i], &out[i]) )
            return 0;
    }
    return 1;
}

/** One `key=value` line, as cp_decode.c read_component_line. */
static int
read_line(
    struct RSCache_Dat2Component* c,
    const char* key,
    const char* value)
{
    int index;

    READ_BOOL("if3", if3)
    READ_INT("type", type)
    READ_INT("clientcode", clientCode)
    READ_INT("x", baseX)
    READ_INT("y", baseY)
    READ_INT("width", baseWidth)
    READ_INT("height", baseHeight)
    READ_INT("widthmode", widthMode)
    READ_INT("heightmode", heightMode)
    READ_INT("xmode", xMode)
    READ_INT("ymode", yMode)
    READ_INT("layer", layer)
    READ_BOOL("hidden", hidden)
    READ_INT("trans", transparency)
    READ_INT("colour", color)
    READ_BOOL("fill", fill)
    READ_BOOL("alpha", alpha)
    READ_BOOL("tiled", tiled)
    READ_INT("outline", outline)
    READ_INT("graphic", graphic)
    READ_INT("graphicshadow", graphicShadow)
    READ_BOOL("hflip", horizontalFlip)
    READ_BOOL("vflip", verticalFlip)
    READ_INT("angle", angle)
    READ_INT("scrollwidth", scrollWidth)
    READ_INT("scrollheight", scrollHeight)
    READ_BOOL("noclickthrough", noClickThrough)
    READ_INT("clickmask", clickMask)
    READ_INT("linewidth", lineWidth)
    READ_BOOL("linedirection", lineDirection)
    READ_INT("buttontype", buttonType)
    READ_INT("linked", linkedComponentId)
    READ_INT("marginx", marginX)
    READ_INT("marginy", marginY)
    READ_STR("text", text)
    READ_STR("name", name)
    READ_STR("targetverb", targetVerb)
    READ_STR("targettext", targetText)
    READ_STR("activetext", activeText)
    READ_STR("option", option)
    READ_INT("font", textFont)
    READ_INT("halign", textHorizontalAlignment)
    READ_INT("valign", textVerticalAlignment)
    READ_INT("lineheight", textLineHeight)
    READ_BOOL("shadow", textShadow)
    READ_INT("model", modelId)
    READ_INT("modelzoom", modelZoom)
    READ_INT("modelxan", modelXAngle)
    READ_INT("modelyan", modelYAngle)
    READ_INT("modelzan", modelZAngle)
    READ_INT("modelxof", modelXOffset)
    READ_INT("modelyof", modelYOffset)
    READ_INT("modelanim", modelSeqId)
    READ_BOOL("modelortho", modelOrthographic)
    READ_BOOL("modelfixedzoom", aBoolean411)
    READ_INT("activemodel", activeModelId)
    READ_INT("activeanim", activeAnimId)
    READ_INT("activegraphic", activeGraphic)
    READ_INT("activecolour", activeColour)
    READ_INT("overcolour", overColour)
    READ_INT("activeovercolour", activeOverColour)
    READ_INT("short50", aShort50)
    READ_INT("int5957", anInt5957)
    READ_INT("int5920", anInt5920)
    READ_INT("dragdeadzone", dragDeadZone)
    READ_INT("dragdeadtime", dragDeadTime)
    READ_BOOL("dragrender", dragRender)

    READ_HOOK("onload", onLoad)
    READ_HOOK("onmouseover", onMouseOver)
    READ_HOOK("onmouseleave", onMouseLeave)
    READ_HOOK("ontargetleave", onTargetLeave)
    READ_HOOK("ontargetenter", onTargetEnter)
    READ_HOOK("onvarptransmit", onVarpTransmit)
    READ_HOOK("oninvtransmit", onInvTransmit)
    READ_HOOK("onstattransmit", onStatTransmit)
    READ_HOOK("ontimer", onTimer)
    READ_HOOK("onop", onOp)
    READ_HOOK("onmouserepeat", onMouseRepeat)
    READ_HOOK("onclick", onClick)
    READ_HOOK("onclickrepeat", onClickRepeat)
    READ_HOOK("onrelease", onRelease)
    READ_HOOK("onhold", onHold)
    READ_HOOK("ondrag", onDrag)
    READ_HOOK("ondragcomplete", onDragComplete)
    READ_HOOK("onscrollwheel", onScrollWheel)

    if( strcmp(key, "varptriggers") == 0 )
        return read_triggers(value, &c->varpTriggers, &c->varpTriggersLen);
    if( strcmp(key, "invtriggers") == 0 )
        return read_triggers(value, &c->inventoryTriggers, &c->inventoryTriggersLen);
    if( strcmp(key, "stattriggers") == 0 )
        return read_triggers(value, &c->statTriggers, &c->statTriggersLen);

    if( Editor_IfKeyLookup(key, &index) < 0 )
        return 0;
    if( strncmp(key, "objop", 5) == 0 )
    {
        char text[1024];
        if( !c->objOps )
        {
            c->objOps = RSCACHE_CALLOC(5, sizeof(char*));
            assert(c->objOps);
        }
        unescape(value, text, sizeof(text));
        RSCACHE_FREE(c->objOps[index]);
        c->objOps[index] = RSCache_Strdup(text);
        assert(c->objOps[index]);
        return 1;
    }
    if( strncmp(key, "op", 2) == 0 )
    {
        char text[1024];
        unescape(value, text, sizeof(text));
        RSCache_Dat2ComponentSetOp(c, index, text);
        return 1;
    }
    if( strncmp(key, "invslot", 7) == 0 )
    {
        int fields[3];
        if( !c->invSlotOffsetX )
        {
            c->invSlotOffsetX = RSCACHE_CALLOC(20, sizeof(int32_t));
            c->invSlotOffsetY = RSCACHE_CALLOC(20, sizeof(int32_t));
            c->invSlotGraphicId = RSCACHE_CALLOC(20, sizeof(int32_t));
            assert(c->invSlotOffsetX);
            assert(c->invSlotOffsetY);
            assert(c->invSlotGraphicId);
            for( int i = 0; i < 20; i++ )
                c->invSlotGraphicId[i] = -1;
        }
        if( !read_indexed_int_list(value, 3, fields) )
            return 0;
        c->invSlotOffsetX[index] = fields[0];
        c->invSlotOffsetY[index] = fields[1];
        c->invSlotGraphicId[index] = fields[2];
        c->invSlotPresent[index] = 1;
        return 1;
    }
    if( strncmp(key, "cs1cmp", 6) == 0 )
    {
        int fields[2];
        int need = index + 1;

        if( !read_indexed_int_list(value, 2, fields) )
            return 0;
        if( need > c->cs1ComparisonLen )
        {
            c->cs1ComparisonOpcodes =
                RSCACHE_REALLOC(c->cs1ComparisonOpcodes, (size_t)need * sizeof(int32_t));
            c->cs1ComparisonOperands =
                RSCACHE_REALLOC(c->cs1ComparisonOperands, (size_t)need * sizeof(int32_t));
            assert(c->cs1ComparisonOpcodes);
            assert(c->cs1ComparisonOperands);
            c->cs1ComparisonLen = need;
        }
        c->cs1ComparisonOpcodes[index] = fields[0];
        c->cs1ComparisonOperands[index] = fields[1];
        return 1;
    }
    if( strncmp(key, "cs1script", 9) == 0 )
    {
        int need = index + 1;
        char scratch[4096];
        char* fields[512];
        int count;
        int32_t* ops;

        if( need > c->cs1ScriptsLen )
        {
            c->cs1Scripts = RSCACHE_REALLOC(c->cs1Scripts, (size_t)need * sizeof(int32_t*));
            c->cs1ScriptsLengths =
                RSCACHE_REALLOC(c->cs1ScriptsLengths, (size_t)need * sizeof(int32_t));
            assert(c->cs1Scripts);
            assert(c->cs1ScriptsLengths);
            for( int i = c->cs1ScriptsLen; i < need; i++ )
            {
                c->cs1Scripts[i] = NULL;
                c->cs1ScriptsLengths[i] = 0;
            }
            c->cs1ScriptsLen = need;
        }
        if( !value[0] )
            return 1;
        if( strlen(value) >= sizeof(scratch) )
            return 0;
        count = split(value, scratch, fields, 512);
        ops = RSCACHE_MALLOC((size_t)count * sizeof(int32_t));
        assert(ops);
        for( int i = 0; i < count; i++ )
        {
            int op = 0;
            if( !read_int(fields[i], &op) )
            {
                RSCACHE_FREE(ops);
                return 0;
            }
            ops[i] = op;
        }
        RSCACHE_FREE(c->cs1Scripts[index]);
        c->cs1Scripts[index] = ops;
        c->cs1ScriptsLengths[index] = count;
        return 1;
    }
    /* `empty=yes` marks a zero-length member; the caller handles it. */
    return strcmp(key, "empty") == 0;
}

#undef READ_INT
#undef READ_BOOL
#undef READ_STR
#undef READ_HOOK

/**
 * What cachepack's config reader keeps of a raw value: it ends at an
 * unescaped `//` (a comment) and loses its unescaped trailing blanks. A file
 * written before line_escape existed can hold a value that LOOKS longer than
 * what packs (a URL's `//`, a name's trailing spaces); reading it the
 * packer's way is what lets the test say so.
 */
static void
cachepack_value(char* value)
{
    size_t end;

    for( char* cut = value; *cut; cut++ )
    {
        if( *cut == '\\' && cut[1] )
        {
            cut++;
            continue;
        }
        if( cut[0] == '/' && cut[1] == '/' )
        {
            *cut = '\0';
            break;
        }
    }
    end = strlen(value);
    while( end > 0 && (value[end - 1] == ' ' || value[end - 1] == '\t') )
    {
        size_t run = 0;
        while( end - 1 > run && value[end - 2 - run] == '\\' )
            run++;
        if( run & 1 )
            break;
        value[--end] = '\0';
    }
}

int
Editor_IfBlockReadComponent(
    const struct Editor_IfFile* file,
    int position,
    struct RSCache_Dat2Component* out,
    char* out_bad_key,
    size_t bad_key_size)
{
    struct Editor_CfgBlock block;
    const struct Editor_CfgBlockRef* ref;
    int ok = 1;

    assert(file);
    assert(out);
    assert(position >= 0);
    assert(position < file->index.block_count);

    RSCache_Dat2ComponentInit(out);
    out->id = ((file->interface_id > 0 ? file->interface_id : 0) << 16) | position;
    ref = &file->index.blocks[position];
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, file->text + ref->start, ref->end - ref->start);
    for( int line = 0; line < block.line_count && ok; line++ )
    {
        const struct Editor_CfgLine* parsed = &block.lines[line];
        char key[64];
        char* value;
        size_t value_length;
        const char* raw;

        if( parsed->kind != EDITOR_CFG_LINE_FIELD )
            continue;
        snprintf(key, sizeof(key), "%.*s", (int)parsed->part, block.text + parsed->start);
        raw = Editor_CfgBlockValue(&block, line, &value_length);
        value = malloc(value_length + 1);
        assert(value);
        memcpy(value, raw, value_length);
        value[value_length] = '\0';
        cachepack_value(value);
        ok = read_line(out, key, value);
        free(value);
        if( !ok && out_bad_key && bad_key_size )
            snprintf(out_bad_key, bad_key_size, "%s", key);
    }
    Editor_CfgBlockFree(&block);
    /* What the dat2 decoder derives from the click mask. */
    out->serverActiveProperties.events = out->clickMask;
    return ok;
}

/* ---- emitting ------------------------------------------------------------ */

struct emit
{
    char* out;
    size_t size;
    size_t written;
};

static void
emit_raw(
    struct emit* e,
    const char* key,
    const char* raw)
{
    int needed = snprintf(
        e->written < e->size ? e->out + e->written : NULL,
        e->written < e->size ? e->size - e->written : 0, "%s=%s\n", key, raw);
    assert(needed >= 0);
    e->written += (size_t)needed;
}

static void
emit_int(
    struct emit* e,
    const char* key,
    int value)
{
    char raw[32];
    snprintf(raw, sizeof(raw), "%d", value);
    emit_raw(e, key, raw);
}

/** A string field, spelled as cachepack's cp_lines_add_str + line_escape. */
static void
emit_str(
    struct emit* e,
    const char* key,
    const char* text)
{
    size_t needed = Editor_IfStringRaw(text, NULL, 0);
    char* raw = malloc(needed + 1);

    assert(raw);
    Editor_IfStringRaw(text, raw, needed + 1);
    emit_raw(e, key, raw);
    free(raw);
}

static void
emit_hook(
    struct emit* e,
    const char* key,
    const struct RSCache_Dat2ComponentScriptVar* args,
    int32_t len)
{
    struct Editor_IfHook* hook;
    size_t needed;
    char* raw;

    if( !args || len <= 0 )
        return;
    hook = calloc(1, sizeof(*hook));
    assert(hook);
    for( int32_t i = 0; i < len && i < EDITOR_IF_HOOK_ARGS_MAX; i++ )
    {
        if( args[i].type == RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT )
            hook->args[i].value = args[i].value.i;
        else
        {
            hook->args[i].is_string = 1;
            snprintf(
                hook->args[i].text, sizeof(hook->args[i].text), "%s",
                args[i].value.s ? args[i].value.s : "");
        }
        hook->arg_count++;
    }
    needed = Editor_IfHookFormat(hook, NULL, 0);
    raw = malloc(needed + 1);
    assert(raw);
    Editor_IfHookFormat(hook, raw, needed + 1);
    emit_raw(e, key, raw);
    free(raw);
    free(hook);
}

static void
emit_triggers(
    struct emit* e,
    const char* key,
    const int32_t* triggers,
    int32_t len)
{
    char buf[2048];
    int written = 0;

    if( !triggers || len <= 0 )
        return;
    for( int32_t i = 0; i < len && written < (int)sizeof(buf) - 16; i++ )
        written += snprintf(buf + written, sizeof(buf) - (size_t)written, i ? ",%d" : "%d", triggers[i]);
    emit_raw(e, key, buf);
}

#define EMIT_INT(f, k)                                                                            \
    do                                                                                            \
    {                                                                                             \
        if( c->f != reference.f )                                                                 \
            emit_int(&e, k, c->f);                                                                \
    } while( 0 )
#define EMIT_BOOL(f, k)                                                                           \
    do                                                                                            \
    {                                                                                             \
        if( c->f != reference.f )                                                                 \
            emit_raw(&e, k, c->f ? "yes" : "no");                                                 \
    } while( 0 )
#define EMIT_STR(f, k)                                                                            \
    do                                                                                            \
    {                                                                                             \
        if( c->f )                                                                                \
            emit_str(&e, k, c->f);                                                                \
    } while( 0 )

size_t
Editor_IfComponentEmit(
    const struct RSCache_Dat2Component* c,
    char* out,
    size_t out_size)
{
    struct RSCache_Dat2Component reference;
    struct emit e = { out, out_size, 0 };

    assert(c);
    assert(out || out_size == 0);

    if( out_size )
        out[0] = '\0';
    RSCache_Dat2ComponentInit(&reference);

    emit_raw(&e, "if3", c->if3 ? "yes" : "no");
    emit_int(&e, "type", c->type);
    EMIT_INT(clientCode, "clientcode");
    EMIT_INT(baseX, "x");
    EMIT_INT(baseY, "y");
    EMIT_INT(baseWidth, "width");
    EMIT_INT(baseHeight, "height");
    EMIT_INT(widthMode, "widthmode");
    EMIT_INT(heightMode, "heightmode");
    EMIT_INT(xMode, "xmode");
    EMIT_INT(yMode, "ymode");
    EMIT_INT(layer, "layer");
    EMIT_BOOL(hidden, "hidden");
    EMIT_INT(transparency, "trans");
    EMIT_INT(color, "colour");
    EMIT_BOOL(fill, "fill");
    EMIT_BOOL(alpha, "alpha");
    EMIT_BOOL(tiled, "tiled");
    EMIT_INT(outline, "outline");
    EMIT_INT(graphic, "graphic");
    EMIT_INT(graphicShadow, "graphicshadow");
    EMIT_BOOL(horizontalFlip, "hflip");
    EMIT_BOOL(verticalFlip, "vflip");
    EMIT_INT(angle, "angle");
    EMIT_INT(scrollWidth, "scrollwidth");
    EMIT_INT(scrollHeight, "scrollheight");
    EMIT_BOOL(noClickThrough, "noclickthrough");
    EMIT_INT(clickMask, "clickmask");
    EMIT_INT(lineWidth, "linewidth");
    EMIT_BOOL(lineDirection, "linedirection");
    EMIT_INT(buttonType, "buttontype");
    EMIT_INT(linkedComponentId, "linked");
    EMIT_INT(marginX, "marginx");
    EMIT_INT(marginY, "marginy");

    EMIT_STR(text, "text");
    EMIT_STR(name, "name");
    EMIT_STR(targetVerb, "targetverb");
    EMIT_STR(targetText, "targettext");
    EMIT_STR(activeText, "activetext");
    if( c->option && !c->optionFromDefault )
        emit_str(&e, "option", c->option);
    EMIT_INT(textFont, "font");
    EMIT_INT(textHorizontalAlignment, "halign");
    EMIT_INT(textVerticalAlignment, "valign");
    EMIT_INT(textLineHeight, "lineheight");
    EMIT_BOOL(textShadow, "shadow");

    EMIT_INT(modelId, "model");
    EMIT_INT(modelZoom, "modelzoom");
    EMIT_INT(modelXAngle, "modelxan");
    EMIT_INT(modelYAngle, "modelyan");
    EMIT_INT(modelZAngle, "modelzan");
    EMIT_INT(modelXOffset, "modelxof");
    EMIT_INT(modelYOffset, "modelyof");
    EMIT_INT(modelSeqId, "modelanim");
    EMIT_BOOL(modelOrthographic, "modelortho");
    EMIT_BOOL(aBoolean411, "modelfixedzoom");
    EMIT_INT(activeModelId, "activemodel");
    EMIT_INT(activeAnimId, "activeanim");
    EMIT_INT(activeGraphic, "activegraphic");
    EMIT_INT(activeColour, "activecolour");
    EMIT_INT(overColour, "overcolour");
    EMIT_INT(activeOverColour, "activeovercolour");

    EMIT_INT(aShort50, "short50");
    EMIT_INT(anInt5957, "int5957");
    EMIT_INT(anInt5920, "int5920");

    for( int32_t i = 0; i < c->opsLen; i++ )
    {
        if( c->ops && c->ops[i] )
        {
            char key[24];
            snprintf(key, sizeof(key), "op%d", i + 1);
            emit_str(&e, key, c->ops[i]);
        }
    }
    if( c->objOps )
    {
        for( int i = 0; i < 5; i++ )
        {
            if( c->objOps[i] )
            {
                char key[24];
                snprintf(key, sizeof(key), "objop%d", i + 1);
                emit_str(&e, key, c->objOps[i]);
            }
        }
    }

    EMIT_INT(dragDeadZone, "dragdeadzone");
    EMIT_INT(dragDeadTime, "dragdeadtime");
    EMIT_BOOL(dragRender, "dragrender");

    for( int i = 0; i < 20; i++ )
    {
        if( c->invSlotPresent[i] )
        {
            char key[24];
            char raw[64];
            snprintf(key, sizeof(key), "invslot%d", i + 1);
            snprintf(
                raw, sizeof(raw), "%d,%d,%d", c->invSlotOffsetX[i], c->invSlotOffsetY[i],
                c->invSlotGraphicId[i]);
            emit_raw(&e, key, raw);
        }
    }
    for( int32_t i = 0; i < c->cs1ComparisonLen; i++ )
    {
        char key[24];
        char raw[64];
        snprintf(key, sizeof(key), "cs1cmp%d", i + 1);
        snprintf(raw, sizeof(raw), "%d,%d", c->cs1ComparisonOpcodes[i], c->cs1ComparisonOperands[i]);
        emit_raw(&e, key, raw);
    }
    for( int32_t i = 0; i < c->cs1ScriptsLen; i++ )
    {
        char key[24];
        char buf[4096];
        int written = 0;

        buf[0] = '\0';
        for( int32_t j = 0; j < c->cs1ScriptsLengths[i] && written < (int)sizeof(buf) - 16; j++ )
            written += snprintf(buf + written, sizeof(buf) - (size_t)written, j ? ",%d" : "%d", c->cs1Scripts[i][j]);
        snprintf(key, sizeof(key), "cs1script%d", i + 1);
        emit_raw(&e, key, buf);
    }

    emit_hook(&e, "onload", c->onLoad, c->onLoadLen);
    emit_hook(&e, "onmouseover", c->onMouseOver, c->onMouseOverLen);
    emit_hook(&e, "onmouseleave", c->onMouseLeave, c->onMouseLeaveLen);
    emit_hook(&e, "ontargetleave", c->onTargetLeave, c->onTargetLeaveLen);
    emit_hook(&e, "ontargetenter", c->onTargetEnter, c->onTargetEnterLen);
    emit_hook(&e, "onvarptransmit", c->onVarpTransmit, c->onVarpTransmitLen);
    emit_hook(&e, "oninvtransmit", c->onInvTransmit, c->onInvTransmitLen);
    emit_hook(&e, "onstattransmit", c->onStatTransmit, c->onStatTransmitLen);
    emit_hook(&e, "ontimer", c->onTimer, c->onTimerLen);
    emit_hook(&e, "onop", c->onOp, c->onOpLen);
    emit_hook(&e, "onmouserepeat", c->onMouseRepeat, c->onMouseRepeatLen);
    emit_hook(&e, "onclick", c->onClick, c->onClickLen);
    emit_hook(&e, "onclickrepeat", c->onClickRepeat, c->onClickRepeatLen);
    emit_hook(&e, "onrelease", c->onRelease, c->onReleaseLen);
    emit_hook(&e, "onhold", c->onHold, c->onHoldLen);
    emit_hook(&e, "ondrag", c->onDrag, c->onDragLen);
    emit_hook(&e, "ondragcomplete", c->onDragComplete, c->onDragCompleteLen);
    emit_hook(&e, "onscrollwheel", c->onScrollWheel, c->onScrollWheelLen);

    emit_triggers(&e, "varptriggers", c->varpTriggers, c->varpTriggersLen);
    emit_triggers(&e, "invtriggers", c->inventoryTriggers, c->inventoryTriggersLen);
    emit_triggers(&e, "stattriggers", c->statTriggers, c->statTriggersLen);

    RSCache_Dat2ComponentFree(&reference);
    return e.written;
}

#undef EMIT_INT
#undef EMIT_BOOL
#undef EMIT_STR
