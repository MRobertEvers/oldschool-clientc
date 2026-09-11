#include "dat2_component.h"

#include "../filelist.h"
#include "../rsbuffer.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Localised defaults (LocalisedText in Java) */
static const char kOptionOk[] = "Ok";
static const char kOptionSelect[] = "Select";
static const char kOptionContinue[] = "Continue";

static char*
read_jstring(struct RSCache_Buffer* buf)
{
    char* s = gcstring(buf);
    return s ? s : strdup("");
}

static void
free_script_vars(
    struct RSCache_Dat2ComponentScriptVar* v,
    int32_t n)
{
    if( !v )
        return;
    for( int32_t i = 0; i < n; i++ )
    {
        if( v[i].type == RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_STRING && v[i].value.s )
            free(v[i].value.s);
    }
    free(v);
}

static struct RSCache_Dat2ComponentScriptVar*
read_arguments(
    struct RSCache_Buffer* buf,
    int32_t* outLen,
    bool* hasHook)
{
    int32_t len = g1(buf);
    if( len == 0 )
    {
        *outLen = 0;
        return NULL;
    }
    struct RSCache_Dat2ComponentScriptVar* args =
        calloc((size_t)len, sizeof(struct RSCache_Dat2ComponentScriptVar));
    if( !args )
    {
        *outLen = 0;
        return NULL;
    }
    for( int32_t i = 0; i < len; i++ )
    {
        int t = g1(buf);
        if( t == 0 )
        {
            args[i].type = RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT;
            args[i].value.i = (int32_t)g4(buf);
        }
        else
        {
            args[i].type = RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_STRING;
            args[i].value.s = read_jstring(buf);
        }
    }
    *outLen = len;
    if( hasHook )
        *hasHook = true;
    return args;
}

static int32_t*
read_triggers(
    struct RSCache_Buffer* buf,
    int32_t* outLen)
{
    int32_t len = g1(buf);
    if( len == 0 )
    {
        *outLen = 0;
        return NULL;
    }
    int32_t* t = malloc(sizeof(int32_t) * (size_t)len);
    if( !t )
    {
        *outLen = 0;
        return NULL;
    }
    for( int32_t i = 0; i < len; i++ )
        t[i] = (int32_t)g4(buf);
    *outLen = len;
    return t;
}

void
RSCache_Dat2ComponentInit(struct RSCache_Dat2Component* c)
{
    memset(c, 0, sizeof(*c));
    c->id = -1;
    c->layer = -1;
    c->type = -1;
    c->textFont = -1;
    c->modelSeqId = -1;
    c->modelId = -1;
    c->graphic = -1;
    c->activeGraphic = -1;
    c->linkedComponentId = -1;
    c->modelZoom = 100;
    c->aspect_ratio_w = 1;
    c->aspect_ratio_h = 1;
    c->showObjCount = true;
    c->lineWidth = 1;
    c->aShort49 = 3000;
    c->modelType = 1;
    c->anInt5890 = -1;
    c->anInt5930 = -1;
    c->clickMask = 0;
    c->textureId = -1;
    c->serverActiveProperties.events = 0;
    c->serverActiveProperties.targetMask = -1;
}

void
RSCache_Dat2ComponentFree(struct RSCache_Dat2Component* c)
{
    if( !c )
        return;

    free(c->text);
    free(c->pauseText);
    free(c->name);
    free(c->option);
    free(c->activeText);
    free(c->targetText);
    free(c->opBase);
    free(c->targetVerb);

    free(c->objTypes);
    free(c->objCounts);
    if( c->objOps )
    {
        for( int i = 0; i < 5; i++ )
            free(c->objOps[i]);
        free(c->objOps);
    }

    free(c->invSlotOffsetX);
    free(c->invSlotOffsetY);
    free(c->invSlotGraphicId);

    free_script_vars(c->onLoad, c->onLoadLen);
    free_script_vars(c->onMouseOver, c->onMouseOverLen);
    free_script_vars(c->onMouseLeave, c->onMouseLeaveLen);
    free_script_vars(c->onTargetLeave, c->onTargetLeaveLen);
    free_script_vars(c->onTargetEnter, c->onTargetEnterLen);
    free_script_vars(c->onVarpTransmit, c->onVarpTransmitLen);
    free_script_vars(c->onInvTransmit, c->onInvTransmitLen);
    free_script_vars(c->onStatTransmit, c->onStatTransmitLen);
    free_script_vars(c->onTimer, c->onTimerLen);
    free_script_vars(c->onOp, c->onOpLen);
    free_script_vars(c->onMouseRepeat, c->onMouseRepeatLen);
    free_script_vars(c->onClick, c->onClickLen);
    free_script_vars(c->onClickRepeat, c->onClickRepeatLen);
    free_script_vars(c->onRelease, c->onReleaseLen);
    free_script_vars(c->onHold, c->onHoldLen);
    free_script_vars(c->onDrag, c->onDragLen);
    free_script_vars(c->onDragComplete, c->onDragCompleteLen);
    free_script_vars(c->onScrollWheel, c->onScrollWheelLen);
    free_script_vars(c->onVarcTransmit, c->onVarcTransmitLen);
    free_script_vars(c->onVarcstrTransmit, c->onVarcstrTransmitLen);

    free(c->varpTriggers);
    free(c->inventoryTriggers);
    free(c->statTriggers);
    free(c->varcTriggers);
    free(c->varcstrTriggers);

    if( c->cs1Scripts )
    {
        for( int32_t i = 0; i < c->cs1ScriptsLen; i++ )
            free(c->cs1Scripts[i]);
        free(c->cs1Scripts);
    }
    free(c->cs1ScriptsLengths);
    free(c->cs1ComparisonOpcodes);
    free(c->cs1ComparisonOperands);

    if( c->createdComponents )
    {
        for( int32_t i = 0; i < c->createdComponentsLen; i++ )
        {
            if( c->createdComponents[i] )
            {
                RSCache_Dat2ComponentFree(c->createdComponents[i]);
                free(c->createdComponents[i]);
            }
        }
        free(c->createdComponents);
    }

    if( c->ops )
    {
        for( int32_t i = 0; i < c->opsLen; i++ )
            free(c->ops[i]);
        free(c->ops);
    }
    free(c->opCursors);
}

struct RSCache_Dat2ComponentDecodeRev
RSCache_Dat2ComponentDecodeRevOsrs(int32_t index_revision)
{
    struct RSCache_Dat2ComponentDecodeRev rev = {
        .era = RSCACHE_DAT2_COMPONENT_DECODE_ERA_OSRS,
        .index_revision = index_revision,
    };
    return rev;
}

struct RSCache_Dat2ComponentDecodeRev
RSCache_Dat2ComponentDecodeRev643(void)
{
    struct RSCache_Dat2ComponentDecodeRev rev = {
        .era = RSCACHE_DAT2_COMPONENT_DECODE_ERA_643,
        .index_revision = RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN,
    };
    return rev;
}

struct RSCache_Dat2ComponentDecodeRev
RSCache_Dat2ComponentDecodeRevFromProfile(
    const struct RSCache* cache,
    int32_t index_revision)
{
    assert(cache);
    struct RSCache_Dat2ComponentDecodeRev rev = {
        /* game+epoch is the only thing that can separate the two families: their
         * index revisions occupy the same numeric range. */
        .era = RSCache_IsRs2Dat2(cache) ? RSCACHE_DAT2_COMPONENT_DECODE_ERA_643
                                       : RSCACHE_DAT2_COMPONENT_DECODE_ERA_OSRS,
        .index_revision = index_revision,
        .game_revision = (int32_t)cache->revision,
    };
    return rev;
}

static bool
rev_is_643(struct RSCache_Dat2ComponentDecodeRev rev)
{
    return rev.era == RSCACHE_DAT2_COMPONENT_DECODE_ERA_643;
}

/* OSRS 237 widened the type-6 model ids from u16-with-65535-sentinel to i32.
 *
 * A known game revision answers this outright. Otherwise fall back to the
 * interfaces index revision, which only discriminates for caches whose
 * interfaces table carries a timestamp — local caches read 997 (kronos), 1193
 * and 1767694604, all below the threshold. An unknown revision keeps the narrow
 * reads, which is what every cache in this repo needs. */
static bool
rev_has_int_model_ids(struct RSCache_Dat2ComponentDecodeRev rev)
{
    if( rev.era != RSCACHE_DAT2_COMPONENT_DECODE_ERA_OSRS )
        return false;
    if( rev.game_revision != RSCACHE_REVISION_UNKNOWN )
        return rev.game_revision >= 237;
    if( rev.index_revision == RSCACHE_DAT2_COMPONENT_INDEX_REVISION_UNKNOWN )
        return false;
    return rev.index_revision > RSCACHE_DAT2_COMPONENT_INDEX_REVISION_237;
}

void
RSCache_Dat2ComponentDecodeIf1(
    struct RSCache_Dat2Component* self,
    struct RSCache_Buffer* buf,
    struct RSCache_Dat2ComponentDecodeRev rev)
{
    self->if3 = false;
    self->clickMask = 0;
    self->type = g1(buf);
    self->buttonType = g1(buf);
    self->clientCode = g2(buf);
    self->baseX = g2b(buf);
    self->baseY = g2b(buf);
    self->baseWidth = g2(buf);
    self->baseHeight = g2(buf);

    self->heightMode = 0;
    self->xMode = 0;
    self->yMode = 0;
    self->widthMode = 0;

    self->transparency = g1(buf);
    self->layer = g2(buf);
    if( self->layer == 65535 )
        self->layer = -1;
    else
        self->layer += self->id & (int32_t)0xFFFF0000u;

    self->linkedComponentId = g2(buf);
    if( self->linkedComponentId == 65535 )
        self->linkedComponentId = -1;

    int32_t condCount = g1(buf);
    if( condCount > 0 )
    {
        self->cs1ComparisonLen = condCount;
        self->cs1ComparisonOpcodes = malloc(sizeof(int32_t) * (size_t)condCount);
        self->cs1ComparisonOperands = malloc(sizeof(int32_t) * (size_t)condCount);
        for( int32_t i = 0; i < condCount; i++ )
        {
            self->cs1ComparisonOpcodes[i] = g1(buf);
            self->cs1ComparisonOperands[i] = g2(buf);
        }
    }

    int32_t scriptCount = g1(buf);
    if( scriptCount > 0 )
    {
        self->cs1ScriptsLen = scriptCount;
        self->cs1Scripts = calloc((size_t)scriptCount, sizeof(int32_t*));
        self->cs1ScriptsLengths = calloc((size_t)scriptCount, sizeof(int32_t));
        for( int32_t i = 0; i < scriptCount; i++ )
        {
            int32_t opCount = g2(buf);
            self->cs1ScriptsLengths[i] = opCount;
            self->cs1Scripts[i] = malloc(sizeof(int32_t) * (size_t)opCount);
            for( int32_t j = 0; j < opCount; j++ )
            {
                int32_t v = g2(buf);
                self->cs1Scripts[i][j] = (v == 65535) ? -1 : v;
            }
        }
    }

    if( self->type == 0 )
    {
        self->scrollHeight = g2(buf);
        self->hidden = (g1(buf) == 1);
    }
    if( self->type == 1 )
    {
        g2(buf);
        g1(buf);
    }
    if( self->type == 2 )
    {
        self->widthMode = 3;
        self->heightMode = 3;
        int32_t cells = self->baseWidth * self->baseHeight;
        self->objCounts = calloc((size_t)cells, sizeof(int32_t));
        self->objTypes = calloc((size_t)cells, sizeof(int32_t));

        int32_t local282 = g1(buf);
        int32_t local286 = g1(buf);
        if( local282 == 1 )
            self->clickMask |= 0x10000000;
        int32_t local297 = g1(buf);
        if( local286 == 1 )
            self->clickMask |= 0x40000000;
        int32_t local309 = g1(buf);
        if( local297 == 1 )
            self->clickMask |= INT32_MIN;
        if( local309 == 1 )
            self->clickMask |= 0x20000000;

        self->marginX = g1(buf);
        self->marginY = g1(buf);

        self->invSlotOffsetX = calloc(20, sizeof(int32_t));
        self->invSlotOffsetY = calloc(20, sizeof(int32_t));
        self->invSlotGraphicId = calloc(20, sizeof(int32_t));
        for( int i = 0; i < 20; i++ )
        {
            int32_t flag = g1(buf);
            self->invSlotPresent[i] = (uint8_t)(flag == 1);
            if( flag == 1 )
            {
                self->invSlotOffsetX[i] = g2b(buf);
                self->invSlotOffsetY[i] = g2b(buf);
                self->invSlotGraphicId[i] = (int32_t)g4(buf);
            }
            else
                self->invSlotGraphicId[i] = -1;
        }
        self->objOps = calloc(5, sizeof(char*));
        for( int i = 0; i < 5; i++ )
        {
            char* op = read_jstring(buf);
            if( op && op[0] != '\0' )
            {
                self->objOps[i] = op;
                self->clickMask |= (int32_t)(1u << (i + 23));
            }
            else
            {
                free(op);
                self->objOps[i] = NULL;
            }
        }
    }
    if( self->type == 3 )
        self->fill = (g1(buf) == 1);

    if( self->type == 4 || self->type == 1 )
    {
        self->textHorizontalAlignment = g1(buf);
        self->textVerticalAlignment = g1(buf);
        self->textLineHeight = g1(buf);
        self->textFont = g2(buf);
        if( self->textFont == 65535 )
            self->textFont = -1;
        self->textShadow = (g1(buf) == 1);
    }
    if( self->type == 4 )
    {
        free(self->text);
        self->text = read_jstring(buf);
        free(self->activeText);
        self->activeText = read_jstring(buf);
    }
    if( self->type == 1 || self->type == 3 || self->type == 4 )
        self->color = (int32_t)g4(buf);

    if( self->type == 3 || self->type == 4 )
    {
        self->activeColour = (int32_t)g4(buf);
        self->overColour = (int32_t)g4(buf);
        self->activeOverColour = (int32_t)g4(buf);
    }
    if( self->type == 5 )
    {
        self->graphic = (int32_t)g4(buf);
        self->activeGraphic = (int32_t)g4(buf);
    }
    if( self->type == 6 )
    {
        self->modelType = 1;
        self->anInt5909 = 1;
        if( rev_has_int_model_ids(rev) )
        {
            self->modelId = (int32_t)g4(buf);
            self->activeModelId = (int32_t)g4(buf);
        }
        else
        {
            self->modelId = g2(buf);
            if( self->modelId == 65535 )
                self->modelId = -1;
            self->activeModelId = g2(buf);
            if( self->activeModelId == 65535 )
                self->activeModelId = -1;
        }
        self->modelSeqId = g2(buf);
        if( self->modelSeqId == 65535 )
            self->modelSeqId = -1;
        self->activeAnimId = g2(buf);
        if( self->activeAnimId == 65535 )
            self->activeAnimId = -1;
        self->modelZoom = g2(buf);
        self->modelXAngle = g2(buf);
        self->modelYAngle = g2(buf);
    }
    if( self->type == 7 )
    {
        self->heightMode = 3;
        self->widthMode = 3;
        int32_t cells = self->baseWidth * self->baseHeight;
        self->objCounts = calloc((size_t)cells, sizeof(int32_t));
        self->objTypes = calloc((size_t)cells, sizeof(int32_t));

        self->textHorizontalAlignment = g1(buf);
        self->textFont = g2(buf);
        if( self->textFont == 65535 )
            self->textFont = -1;
        self->textShadow = (g1(buf) == 1);
        self->color = (int32_t)g4(buf);
        self->marginX = g2b(buf);
        self->marginY = g2b(buf);

        int32_t local729 = g1(buf);
        if( local729 == 1 )
            self->clickMask |= 0x40000000;

        self->objOps = calloc(5, sizeof(char*));
        for( int i = 0; i < 5; i++ )
        {
            char* op = read_jstring(buf);
            if( op && op[0] != '\0' )
            {
                self->objOps[i] = op;
                self->clickMask |= (int32_t)(1u << (i + 23));
            }
            else
            {
                free(op);
                self->objOps[i] = NULL;
            }
        }
    }
    if( self->type == 8 )
    {
        free(self->text);
        self->text = read_jstring(buf);
    }

    if( self->buttonType == 2 || self->type == 2 )
    {
        free(self->targetVerb);
        self->targetVerb = read_jstring(buf);
        free(self->targetText);
        self->targetText = read_jstring(buf);
        int32_t local808 = g2(buf) & 0x3F;
        self->clickMask |= local808 << 11;
    }
    if( self->buttonType == 1 || self->buttonType == 4 || self->buttonType == 5 ||
        self->buttonType == 6 )
    {
        free(self->option);
        self->option = read_jstring(buf);
        if( !self->option || self->option[0] == '\0' )
        {
            free(self->option);
            self->optionFromDefault = true;
            if( self->buttonType == 1 )
                self->option = strdup(kOptionOk);
            else if( self->buttonType == 4 || self->buttonType == 5 )
                self->option = strdup(kOptionSelect);
            else
                self->option = strdup(kOptionContinue);
        }
    }
    if( self->buttonType == 1 || self->buttonType == 4 || self->buttonType == 5 )
        self->clickMask |= 0x400000;
    if( self->buttonType == 6 )
        self->clickMask |= 0x1;

    self->serverActiveProperties.events = self->clickMask;
    self->serverActiveProperties.targetMask = -1;
}

static void
decode_if3_hook(
    struct RSCache_Dat2Component* self,
    struct RSCache_Buffer* buf,
    struct RSCache_Dat2ComponentScriptVar** out,
    int32_t* outLen)
{
    bool hookFlag = false;
    *out = read_arguments(buf, outLen, &hookFlag);
    if( hookFlag )
        self->hasHook = true;
}

/*
 * RS2 (634/643) IF3 widget.
 *
 * A different *shape* from the OldSchool layout, not a field-width variation, so
 * it is its own function rather than a gate inside the OSRS one. Ported from the
 * 634 client (`Class46.method433`, ~/Documents/git_repos/634-client) and
 * cross-read against Void's server-side decoder (`InterfaceDecoderFull.kt`,
 * ~/Documents/git_repos/Void_RS2011Server). Where the two disagree the client
 * wins — Void mislabels the type-5 flag bits (it calls bit 1 "imageRepeat"; the
 * client's draw path tiles on bit 0).
 *
 * Field-by-field diff and the source for each decision:
 * docs/RS2_634_CLIENT_REFERENCES.md section 2.
 *
 * Everything the OldSchool layout lacks:
 *
 *   - the leading byte is a *version*, not a bare 255 marker. 255 means -1, which
 *     is what every file in a 634-era cache carries; a non-negative version adds
 *     fields in six places (marked `version >= 0` below) and is decoded here so
 *     that a cache which does use it cannot silently desync.
 *   - `type` bit 7 flags a trailing string.
 *   - type 4 carries a transparency byte after its colour.
 *   - type 6 is entirely different: a flag byte selects which of two viewport
 *     blocks follows, and there is no orthographic/zoom-mode tail.
 *   - a key-binding table sits between the click mask and the name.
 *   - the byte before the ops is a nibble pair: op count low, cursor count high.
 *   - an option-override string sits between the ops and the drag fields.
 *   - three shorts follow the target verb whenever the click mask has any of the
 *     seven "target" bits set. This is the block whose absence left every
 *     type-0 widget in iface 548 six bytes short.
 *   - 20 hooks and 5 trigger tables, against OldSchool's 18 and 3. The extra pairs
 *     are varc and varcstr, appended after the OldSchool set.
 */
static void
decode_if3_rs2(
    struct RSCache_Dat2Component* self,
    struct RSCache_Buffer* buf)
{
    self->if3 = true;

    int32_t version = g1(buf);
    if( version == 255 )
        version = -1;

    self->type = g1(buf);
    if( (self->type & 0x80) != 0 )
    {
        self->type &= 0x7F;
        /* Unused by the client beyond being stored; read to stay aligned. */
        free(self->pauseText);
        self->pauseText = read_jstring(buf);
    }

    self->clientCode = g2(buf);
    self->baseX = g2b(buf);
    self->baseY = g2b(buf);
    self->baseWidth = g2(buf);
    self->baseHeight = g2(buf);

    self->widthMode = g1b(buf);
    self->heightMode = g1b(buf);
    self->xMode = g1b(buf);
    self->yMode = g1b(buf);

    self->layer = g2(buf);
    if( self->layer == 65535 )
        self->layer = -1;
    else
        self->layer += self->id & (int32_t)0xFFFF0000u;

    {
        int32_t flags = g1(buf);
        if( version >= 0 )
            self->noClickThrough = (flags & 0x2) != 0;
        self->hidden = (flags & 0x1) != 0;
    }

    if( self->type == 0 )
    {
        self->scrollWidth = g2(buf);
        self->scrollHeight = g2(buf);
        if( version < 0 )
            self->noClickThrough = (g1(buf) == 1);
    }
    if( self->type == 5 )
    {
        self->graphic = (int32_t)g4(buf);
        self->angle = (int32_t)g2(buf);
        {
            uint8_t flags = (uint8_t)g1(buf);
            self->tiled = (flags & 0x1) != 0;
            self->alpha = (flags & 0x2) != 0;
        }
        self->transparency = g1(buf);
        self->outline = g1(buf);
        self->graphicShadow = (int32_t)g4(buf);
        /* Row swap first, then the in-row swap: vertical, then horizontal
         * (Class207.method1514 / method1518). */
        self->verticalFlip = (g1(buf) == 1);
        self->horizontalFlip = (g1(buf) == 1);
        self->color = (int32_t)g4(buf);
    }
    if( self->type == 6 )
    {
        self->modelType = 1;
        /* The live 727 client widens model/font/sequence references only for
         * interfaces added after archive 1144 (IComponentDefinitions.decode,
         * `ihash >> 16 > 1144`).  This is a transition inside one cache, not a
         * cache-wide revision gate: reading interface 1285's model 70127 as a
         * u16 produced 32769 and shifted its complete type-6 viewport block.
         * BigSmart owns its own -1 sentinel; the older u16 form uses 65535. */
        if( (self->id >> 16) > 1144 )
            self->modelId = gbigsmart(buf);
        else
        {
            self->modelId = g2(buf);
            if( self->modelId == 65535 )
                self->modelId = -1;
        }

        int32_t model_flags = g1(buf);
        bool has_viewport = (model_flags & 0x1) != 0;
        bool has_centred_viewport = (model_flags & 0x2) != 0;
        self->aBoolean411 = (model_flags & 0x4) != 0; /* animated */
        self->modelOrthographic = (model_flags & 0x8) != 0; /* ignores the z-buffer */

        if( has_viewport )
        {
            self->modelXOffset = g2b(buf);
            self->modelYOffset = g2b(buf);
            self->modelXAngle = g2(buf);
            self->modelZAngle = g2(buf);
            self->modelYAngle = g2(buf);
            self->modelZoom = g2(buf);
        }
        else if( has_centred_viewport )
        {
            self->modelXOffset = g2b(buf);
            self->modelYOffset = g2b(buf);
            self->anInt5907 = g2b(buf); /* z offset */
            self->modelXAngle = g2(buf);
            self->modelZAngle = g2(buf);
            self->modelYAngle = g2(buf);
            self->modelZoom = g2b(buf);
        }

        if( (self->id >> 16) > 1144 )
            self->modelSeqId = gbigsmart(buf);
        else
        {
            self->modelSeqId = g2(buf);
            if( self->modelSeqId == 65535 )
                self->modelSeqId = -1;
        }
        if( self->widthMode != 0 )
            self->anInt5957 = g2(buf);
        if( self->heightMode != 0 )
            self->anInt5920 = g2(buf);
    }
    if( self->type == 4 )
    {
        if( (self->id >> 16) > 1144 )
            self->textFont = gbigsmart(buf);
        else
        {
            self->textFont = g2(buf);
            if( self->textFont == 65535 )
                self->textFont = -1;
        }
        free(self->text);
        self->text = read_jstring(buf);
        self->textLineHeight = g1(buf);
        self->textHorizontalAlignment = g1(buf);
        self->textVerticalAlignment = g1(buf);
        self->textShadow = (g1(buf) == 1);
        self->color = (int32_t)g4(buf);
        self->transparency = g1(buf);
        if( version >= 0 )
            self->anInt5921 = g1(buf);
    }
    if( self->type == 3 )
    {
        self->color = (int32_t)g4(buf);
        self->fill = (g1(buf) == 1);
        self->transparency = g1(buf);
    }
    if( self->type == 9 )
    {
        self->lineWidth = g1(buf);
        self->color = (int32_t)g4(buf);
        self->lineDirection = (g1(buf) == 1);
    }

    self->clickMask = g3(buf);

    /* Key bindings: a chain of (index nibble, 12-bit modifier, repeat, key code)
     * records terminated by a zero lead byte. The lead byte doubles as the high
     * nibble of the index and the high bits of the modifier, so the terminator
     * check has to happen on the *next* lead byte, not on a count. */
    {
        int32_t lead = g1(buf);
        if( lead != 0 )
        {
            for( ; lead != 0; lead = g1(buf) )
            {
                int32_t slot = (lead >> 4) - 1;
                int32_t modifier = ((lead << 8) | g1(buf)) & 0xFFF;
                if( modifier == 4095 )
                    modifier = -1;
                int8_t repeat = (int8_t)g1(buf);
                int8_t code = (int8_t)g1(buf);
                if( slot >= 0 && slot < 10 )
                {
                    self->if3SlotCursorOrKey[slot] = modifier;
                    self->if3SlotPackedA[slot] = repeat;
                    self->if3SlotPackedB[slot] = code;
                }
            }
        }
    }

    free(self->name);
    self->name = read_jstring(buf);

    {
        int32_t nibbles = g1(buf);
        int32_t op_count = nibbles & 0xF;
        int32_t cursor_count = nibbles >> 4;

        if( op_count > 0 )
        {
            if( self->ops )
            {
                for( int32_t i = 0; i < self->opsLen; i++ )
                    free(self->ops[i]);
                free(self->ops);
            }
            self->opsLen = op_count;
            self->ops = calloc((size_t)op_count, sizeof(char*));
            for( int32_t i = 0; i < op_count; i++ )
                self->ops[i] = read_jstring(buf);
        }

        /* Cursor overrides are stored sparsely: a slot index then the cursor id,
         * at most twice, with every lower slot left at -1. */
        if( cursor_count > 0 )
        {
            int32_t slot = g1(buf);
            free(self->opCursors);
            self->opCursorsLen = slot + 1;
            self->opCursors = malloc(sizeof(int32_t) * (size_t)self->opCursorsLen);
            for( int32_t i = 0; i < self->opCursorsLen; i++ )
                self->opCursors[i] = -1;
            self->opCursors[slot] = g2(buf);
        }
        if( cursor_count > 1 )
        {
            int32_t slot = g1(buf);
            int32_t cursor = g2(buf);
            if( self->opCursors && slot >= 0 && slot < self->opCursorsLen )
                self->opCursors[slot] = cursor;
        }
    }

    free(self->opBase);
    self->opBase = read_jstring(buf);
    if( self->opBase[0] == '\0' )
    {
        free(self->opBase);
        self->opBase = NULL;
    }

    self->dragDeadZone = (uint8_t)g1(buf);
    self->dragDeadTime = (uint8_t)g1(buf);
    self->dragRender = (g1(buf) == 1);

    free(self->targetVerb);
    self->targetVerb = read_jstring(buf);

    /* The seven target bits (clickMask >> 11 & 0x7F) gate a target-mask triplet.
     * Missing it left every widget carrying one six bytes short of its file. */
    if( ((self->clickMask >> 11) & 0x7F) != 0 )
    {
        int32_t target_mask = g2(buf);
        if( target_mask == 65535 )
            target_mask = -1;
        self->serverActiveProperties.targetMask = target_mask;
        self->anInt5930 = g2(buf);
        if( self->anInt5930 == 65535 )
            self->anInt5930 = -1;
        self->anInt5890 = g2(buf);
        if( self->anInt5890 == 65535 )
            self->anInt5890 = -1;
    }

    if( version >= 0 )
    {
        self->anInt5909 = g2(buf);
        if( self->anInt5909 == 65535 )
            self->anInt5909 = -1;
    }

    self->serverActiveProperties.events = self->clickMask;

    if( version >= 0 )
    {
        /* Two id-keyed side tables (int-valued, then string-valued) the client
         * stashes in a hashmap. Nothing here consumes them; read to stay aligned. */
        int32_t count = g1(buf);
        for( int32_t i = 0; i < count; i++ )
        {
            g3(buf);
            g4(buf);
        }
        count = g1(buf);
        for( int32_t i = 0; i < count; i++ )
        {
            g3(buf);
            free(read_jstring(buf));
        }
    }

    decode_if3_hook(self, buf, &self->onLoad, &self->onLoadLen);
    decode_if3_hook(self, buf, &self->onMouseOver, &self->onMouseOverLen);
    decode_if3_hook(self, buf, &self->onMouseLeave, &self->onMouseLeaveLen);
    decode_if3_hook(self, buf, &self->onTargetLeave, &self->onTargetLeaveLen);
    decode_if3_hook(self, buf, &self->onTargetEnter, &self->onTargetEnterLen);
    decode_if3_hook(self, buf, &self->onVarpTransmit, &self->onVarpTransmitLen);
    decode_if3_hook(self, buf, &self->onInvTransmit, &self->onInvTransmitLen);
    decode_if3_hook(self, buf, &self->onStatTransmit, &self->onStatTransmitLen);
    decode_if3_hook(self, buf, &self->onTimer, &self->onTimerLen);
    decode_if3_hook(self, buf, &self->onOp, &self->onOpLen);
    if( version >= 0 )
    {
        struct RSCache_Dat2ComponentScriptVar* extra = NULL;
        int32_t extra_len = 0;
        decode_if3_hook(self, buf, &extra, &extra_len);
        free_script_vars(extra, extra_len);
    }
    decode_if3_hook(self, buf, &self->onMouseRepeat, &self->onMouseRepeatLen);
    decode_if3_hook(self, buf, &self->onClick, &self->onClickLen);
    decode_if3_hook(self, buf, &self->onClickRepeat, &self->onClickRepeatLen);
    decode_if3_hook(self, buf, &self->onRelease, &self->onReleaseLen);
    decode_if3_hook(self, buf, &self->onHold, &self->onHoldLen);
    decode_if3_hook(self, buf, &self->onDrag, &self->onDragLen);
    decode_if3_hook(self, buf, &self->onDragComplete, &self->onDragCompleteLen);
    decode_if3_hook(self, buf, &self->onScrollWheel, &self->onScrollWheelLen);
    decode_if3_hook(self, buf, &self->onVarcTransmit, &self->onVarcTransmitLen);
    decode_if3_hook(self, buf, &self->onVarcstrTransmit, &self->onVarcstrTransmitLen);

    self->varpTriggers = read_triggers(buf, &self->varpTriggersLen);
    self->inventoryTriggers = read_triggers(buf, &self->inventoryTriggersLen);
    self->statTriggers = read_triggers(buf, &self->statTriggersLen);
    self->varcTriggers = read_triggers(buf, &self->varcTriggersLen);
    self->varcstrTriggers = read_triggers(buf, &self->varcstrTriggersLen);
}

void
RSCache_Dat2ComponentDecodeIf3(
    struct RSCache_Dat2Component* self,
    struct RSCache_Buffer* buf,
    struct RSCache_Dat2ComponentDecodeRev rev)
{
    if( rev_is_643(rev) )
    {
        decode_if3_rs2(self, buf);
        return;
    }

    self->if3 = true;
    g1(buf); // Skips the 255 marker

    self->type = g1(buf);
    self->clientCode = g2(buf);
    self->baseX = g2b(buf);
    self->baseY = g2b(buf);
    if( self->type == 9 )
    {
        self->baseWidth = g2b(buf);
        self->baseHeight = g2b(buf);
    }
    else
    {
        self->baseWidth = g2(buf);
        self->baseHeight = g2(buf);
    }

    self->widthMode = g1b(buf);
    self->heightMode = g1b(buf);
    self->xMode = g1b(buf);
    self->yMode = g1b(buf);

    self->layer = g2(buf);
    if( self->layer == 65535 )
        self->layer = -1;
    else
        self->layer += self->id & (int32_t)0xFFFF0000u;

    self->hidden = (g1(buf) == 1);

    if( self->type == 0 )
    {
        self->scrollWidth = g2(buf);
        self->scrollHeight = g2(buf);
        self->noClickThrough = (g1(buf) == 1);
    }
    if( self->type == 5 )
    {
        self->graphic = (int32_t)g4(buf);
        self->angle = (int32_t)g2(buf);
        {
            uint8_t flags = (uint8_t)g1(buf);
            self->alpha = (flags & 0x2) != 0;
            self->tiled = (flags & 0x1) != 0;
        }
        self->transparency = g1(buf);
        self->outline = g1(buf);
        self->graphicShadow = (int32_t)g4(buf);
        if( rev_is_643(rev) )
        {
            self->horizontalFlip = (g1(buf) == 1);
            self->verticalFlip = (g1(buf) == 1);
            self->color = (int32_t)g4(buf);
        }
        else
        {
            self->verticalFlip = (g1(buf) == 1);
            self->horizontalFlip = (g1(buf) == 1);
        }
    }
    if( self->type == 6 )
    {
        self->modelType = 1;
        if( rev_has_int_model_ids(rev) )
        {
            self->modelId = (int32_t)g4(buf);
        }
        else
        {
            self->modelId = g2(buf);
            if( self->modelId == 65535 )
                self->modelId = -1;
        }
        self->modelXOffset = g2b(buf);
        self->modelYOffset = g2b(buf);
        self->modelXAngle = g2(buf);
        self->modelYAngle = g2(buf);
        self->modelZAngle = g2(buf);
        self->modelZoom = g2(buf);
        self->modelSeqId = g2(buf);
        if( self->modelSeqId == 65535 )
            self->modelSeqId = -1;
        self->modelOrthographic = (g1(buf) == 1);
        self->aShort50 = (int16_t)g2(buf);
        if( rev_is_643(rev) )
        {
            self->aShort49 = (int16_t)g2(buf);
            self->aBoolean411 = (g1(buf) == 1);
            if( self->widthMode != 0 )
                self->anInt5957 = g2(buf);
            if( self->heightMode != 0 )
                self->anInt5920 = g2(buf);
        }
        else
        {
            /* OSRS: both override shorts are present whenever either size
             * mode is dynamic — cache data has wm=1,hm=0 files carrying both
             * (e.g. interface 272 file 4); gating the second on heightMode
             * misaligns the rest of the file. */
            if( self->widthMode != 0 || self->heightMode != 0 )
            {
                self->anInt5957 = g2(buf);
                self->anInt5920 = g2(buf);
            }
        }
    }
    if( self->type == 4 )
    {
        self->textFont = g2(buf);
        if( self->textFont == 65535 )
            self->textFont = -1;
        free(self->text);
        self->text = read_jstring(buf);
        self->textLineHeight = g1(buf);
        self->textHorizontalAlignment = g1(buf);
        self->textVerticalAlignment = g1(buf);
        self->textShadow = (g1(buf) == 1);
        self->color = (int32_t)g4(buf);
    }
    if( self->type == 3 )
    {
        self->color = (int32_t)g4(buf);
        self->fill = (g1(buf) == 1);
        self->transparency = g1(buf);
    }
    if( self->type == 9 )
    {
        self->lineWidth = g1(buf);
        self->color = (int32_t)g4(buf);
        self->lineDirection = (g1(buf) == 1);
    }

    self->clickMask = g3(buf);
    free(self->name);
    self->name = read_jstring(buf);

    int32_t actionCount = g1(buf);
    if( actionCount > 0 )
    {
        if( self->ops )
        {
            for( int32_t i = 0; i < self->opsLen; i++ )
                free(self->ops[i]);
            free(self->ops);
        }
        self->opsLen = actionCount;
        self->ops = calloc((size_t)actionCount, sizeof(char*));
        for( int32_t i = 0; i < actionCount; i++ )
            self->ops[i] = read_jstring(buf);
    }

    self->dragDeadZone = (uint8_t)g1(buf);
    self->dragDeadTime = (uint8_t)g1(buf);
    self->dragRender = (g1(buf) == 1);

    free(self->targetVerb);
    self->targetVerb = read_jstring(buf);

    self->serverActiveProperties.events = self->clickMask;
    self->serverActiveProperties.targetMask = -1;

    decode_if3_hook(self, buf, &self->onLoad, &self->onLoadLen);
    decode_if3_hook(self, buf, &self->onMouseOver, &self->onMouseOverLen);
    decode_if3_hook(self, buf, &self->onMouseLeave, &self->onMouseLeaveLen);
    decode_if3_hook(self, buf, &self->onTargetLeave, &self->onTargetLeaveLen);
    decode_if3_hook(self, buf, &self->onTargetEnter, &self->onTargetEnterLen);
    decode_if3_hook(self, buf, &self->onVarpTransmit, &self->onVarpTransmitLen);
    decode_if3_hook(self, buf, &self->onInvTransmit, &self->onInvTransmitLen);
    decode_if3_hook(self, buf, &self->onStatTransmit, &self->onStatTransmitLen);
    decode_if3_hook(self, buf, &self->onTimer, &self->onTimerLen);
    decode_if3_hook(self, buf, &self->onOp, &self->onOpLen);
    decode_if3_hook(self, buf, &self->onMouseRepeat, &self->onMouseRepeatLen);
    decode_if3_hook(self, buf, &self->onClick, &self->onClickLen);
    decode_if3_hook(self, buf, &self->onClickRepeat, &self->onClickRepeatLen);
    decode_if3_hook(self, buf, &self->onRelease, &self->onReleaseLen);
    decode_if3_hook(self, buf, &self->onHold, &self->onHoldLen);
    decode_if3_hook(self, buf, &self->onDrag, &self->onDragLen);
    decode_if3_hook(self, buf, &self->onDragComplete, &self->onDragCompleteLen);
    decode_if3_hook(self, buf, &self->onScrollWheel, &self->onScrollWheelLen);

    self->varpTriggers = read_triggers(buf, &self->varpTriggersLen);
    self->inventoryTriggers = read_triggers(buf, &self->inventoryTriggersLen);
    self->statTriggers = read_triggers(buf, &self->statTriggersLen);
}

/* ---- IF1 encode --------------------------------------------------------- */

/** Inverse of read_jstring: NUL-terminated, and a NULL string is an empty one —
 *  which is what the decoder produces for an empty field, so they agree. */
static void
write_jstring(
    struct RSCache_Buffer* buf,
    const char* str)
{
    pjstr(buf, str ? str : "", RSCACHE_JSTR_TERMINATOR_NULL);
}

/**
 * Encode an IF1 component — the inverse of RSCache_Dat2ComponentDecodeIf1.
 *
 * Harder than IF3 in one way that is worth naming: several stream bytes are not
 * *stored*, they are folded into `clickMask` on the way in. Those are recovered by
 * reading the bit back out, which works because each one owns a distinct bit:
 *
 *   type 2   four flag bytes -> 0x10000000, 0x40000000, INT32_MIN, 0x20000000
 *   type 2/7 an op string present -> bit (23 + i)
 *   type 7   one flag byte -> 0x40000000
 *   button 2 a 6-bit field -> bits 11..16
 *
 * Two things genuinely cannot come back, and both are recorded rather than
 * papered over:
 *
 *   - **type 1 discards three bytes** (`g2` then `g1`) without storing them, so
 *     they re-encode as zeros. Same class as the lossy config decoders in
 *     EXCEPTIONS.md B2.
 *   - **an empty `option` decodes to a default** ("Ok" / "Select" / "Continue")
 *     by button type, so an absent option and one that literally says "Ok" are
 *     the same state afterwards. This writes the empty string whenever the value
 *     equals the default its button type would have produced, because that is
 *     what the caches actually contain — measured, not assumed.
 */
static uint32_t
component_encode_if1(
    const struct RSCache_Dat2Component* self,
    struct RSCache_Dat2ComponentDecodeRev rev,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, out, out_capacity);

    p1(&buf, self->type);
    p1(&buf, self->buttonType);
    p2(&buf, self->clientCode);
    p2(&buf, self->baseX);
    p2(&buf, self->baseY);
    p2(&buf, self->baseWidth);
    p2(&buf, self->baseHeight);
    p1(&buf, self->transparency);

    if( self->layer == -1 )
        p2(&buf, 65535);
    else
        p2(&buf, (self->layer - (self->id & (int32_t)0xFFFF0000u)) & 0xFFFF);
    p2(&buf, self->linkedComponentId == -1 ? 65535 : (self->linkedComponentId & 0xFFFF));

    p1(&buf, self->cs1ComparisonLen);
    for( int32_t i = 0; i < self->cs1ComparisonLen; i++ )
    {
        p1(&buf, self->cs1ComparisonOpcodes[i]);
        p2(&buf, self->cs1ComparisonOperands[i]);
    }

    p1(&buf, self->cs1ScriptsLen);
    for( int32_t i = 0; i < self->cs1ScriptsLen; i++ )
    {
        int32_t op_count = self->cs1ScriptsLengths[i];
        p2(&buf, op_count);
        for( int32_t j = 0; j < op_count; j++ )
        {
            int32_t value = self->cs1Scripts[i][j];
            p2(&buf, value == -1 ? 65535 : (value & 0xFFFF));
        }
    }

    if( self->type == 0 )
    {
        p2(&buf, self->scrollHeight);
        p1(&buf, self->hidden ? 1 : 0);
    }
    if( self->type == 1 )
    {
        /* Read and discarded on the way in — see the note above. */
        p2(&buf, 0);
        p1(&buf, 0);
    }
    if( self->type == 2 )
    {
        p1(&buf, (self->clickMask & 0x10000000) ? 1 : 0);
        p1(&buf, (self->clickMask & 0x40000000) ? 1 : 0);
        p1(&buf, (self->clickMask & INT32_MIN) ? 1 : 0);
        p1(&buf, (self->clickMask & 0x20000000) ? 1 : 0);
        p1(&buf, self->marginX);
        p1(&buf, self->marginY);
        for( int i = 0; i < 20; i++ )
        {
            /* The flag says "this slot has an offset block"; the decoder marks an
             * absent one by leaving the graphic id at -1. */
            bool present = self->invSlotPresent[i] != 0;
            p1(&buf, present ? 1 : 0);
            if( present )
            {
                p2(&buf, self->invSlotOffsetX[i]);
                p2(&buf, self->invSlotOffsetY[i]);
                p4(&buf, self->invSlotGraphicId[i]);
            }
        }
        for( int i = 0; i < 5; i++ )
            write_jstring(&buf, self->objOps ? self->objOps[i] : NULL);
    }
    if( self->type == 3 )
        p1(&buf, self->fill ? 1 : 0);

    if( self->type == 4 || self->type == 1 )
    {
        p1(&buf, self->textHorizontalAlignment);
        p1(&buf, self->textVerticalAlignment);
        p1(&buf, self->textLineHeight);
        p2(&buf, self->textFont == -1 ? 65535 : (self->textFont & 0xFFFF));
        p1(&buf, self->textShadow ? 1 : 0);
    }
    if( self->type == 4 )
    {
        write_jstring(&buf, self->text);
        write_jstring(&buf, self->activeText);
    }
    if( self->type == 1 || self->type == 3 || self->type == 4 )
        p4(&buf, self->color);
    if( self->type == 3 || self->type == 4 )
    {
        p4(&buf, self->activeColour);
        p4(&buf, self->overColour);
        p4(&buf, self->activeOverColour);
    }
    if( self->type == 5 )
    {
        p4(&buf, self->graphic);
        p4(&buf, self->activeGraphic);
    }
    if( self->type == 6 )
    {
        if( rev_has_int_model_ids(rev) )
        {
            p4(&buf, self->modelId);
            p4(&buf, self->activeModelId);
        }
        else
        {
            p2(&buf, self->modelId == -1 ? 65535 : (self->modelId & 0xFFFF));
            p2(&buf, self->activeModelId == -1 ? 65535 : (self->activeModelId & 0xFFFF));
        }
        p2(&buf, self->modelSeqId == -1 ? 65535 : (self->modelSeqId & 0xFFFF));
        p2(&buf, self->activeAnimId == -1 ? 65535 : (self->activeAnimId & 0xFFFF));
        p2(&buf, self->modelZoom);
        p2(&buf, self->modelXAngle);
        p2(&buf, self->modelYAngle);
    }
    if( self->type == 7 )
    {
        p1(&buf, self->textHorizontalAlignment);
        p2(&buf, self->textFont == -1 ? 65535 : (self->textFont & 0xFFFF));
        p1(&buf, self->textShadow ? 1 : 0);
        p4(&buf, self->color);
        p2(&buf, self->marginX);
        p2(&buf, self->marginY);
        p1(&buf, (self->clickMask & 0x40000000) ? 1 : 0);
        for( int i = 0; i < 5; i++ )
            write_jstring(&buf, self->objOps ? self->objOps[i] : NULL);
    }
    if( self->type == 8 )
        write_jstring(&buf, self->text);

    if( self->buttonType == 2 || self->type == 2 )
    {
        write_jstring(&buf, self->targetVerb);
        write_jstring(&buf, self->targetText);
        p2(&buf, (self->clickMask >> 11) & 0x3F);
    }
    if( self->buttonType == 1 || self->buttonType == 4 || self->buttonType == 5 ||
        self->buttonType == 6 )
    {
        /* The wire said what it said: `optionFromDefault` is the only thing that
         * can tell an empty option from one spelling out its own default. */
        write_jstring(&buf, self->optionFromDefault ? "" : self->option);
    }

    return buf.position;
}

/* ---- IF3 encode --------------------------------------------------------- */

static void
write_arguments(
    struct RSCache_Buffer* buf,
    const struct RSCache_Dat2ComponentScriptVar* args,
    int32_t len)
{
    if( !args || len <= 0 )
    {
        p1(buf, 0);
        return;
    }
    p1(buf, len);
    for( int32_t i = 0; i < len; i++ )
    {
        if( args[i].type == RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT )
        {
            p1(buf, 0);
            p4(buf, args[i].value.i);
        }
        else
        {
            /* Any non-zero type byte selects the string branch on read; 1 is the
             * canonical spelling and the only one the corpus carries. */
            p1(buf, 1);
            write_jstring(buf, args[i].value.s);
        }
    }
}

static void
write_triggers(
    struct RSCache_Buffer* buf,
    const int32_t* triggers,
    int32_t len)
{
    if( !triggers || len <= 0 )
    {
        p1(buf, 0);
        return;
    }
    p1(buf, len);
    for( int32_t i = 0; i < len; i++ )
        p4(buf, triggers[i]);
}

static uint32_t
arguments_bound(
    const struct RSCache_Dat2ComponentScriptVar* args,
    int32_t len)
{
    uint32_t bound = 1;
    for( int32_t i = 0; i < len && args; i++ )
    {
        bound += 1;
        if( args[i].type == RSCACHE_DAT2_COMPONENT_SCRIPT_VAR_INT )
            bound += 4;
        else
            bound += (uint32_t)(args[i].value.s ? strlen(args[i].value.s) : 0) + 1;
    }
    return bound;
}

uint32_t
RSCache_Dat2ComponentEncodeIf3Bound(const struct RSCache_Dat2Component* self)
{
    if( !self )
        return 0;
    /*
     * Covers *both* layouts, because one entry point encodes both. Sizing this
     * for IF3 alone is what the write-past-the-end assert in rsbuffer catches:
     * an IF1 type-2 widget carries 20 inventory slots and five object ops that
     * no IF3 component has.
     */
    uint32_t bound = 256;
    bound += (uint32_t)(self->text ? strlen(self->text) : 0) + 1;
    bound += (uint32_t)(self->name ? strlen(self->name) : 0) + 1;
    bound += (uint32_t)(self->targetVerb ? strlen(self->targetVerb) : 0) + 1;
    bound += 1;
    for( int32_t i = 0; i < self->opsLen; i++ )
        bound += (uint32_t)(self->ops && self->ops[i] ? strlen(self->ops[i]) : 0) + 1;

    /* --- IF1-only fields --- */
    bound += (uint32_t)(self->activeText ? strlen(self->activeText) : 0) + 1;
    bound += (uint32_t)(self->targetText ? strlen(self->targetText) : 0) + 1;
    bound += (uint32_t)(self->option ? strlen(self->option) : 0) + 1;
    bound += (uint32_t)self->cs1ComparisonLen * 3u + 1u;
    for( int32_t i = 0; i < self->cs1ScriptsLen; i++ )
        bound += (uint32_t)self->cs1ScriptsLengths[i] * 2u + 2u;
    bound += 1;
    /* 20 inventory slots at a flag plus eight bytes, and five object ops. */
    bound += 20u * 9u;
    if( self->objOps )
    {
        for( int i = 0; i < 5; i++ )
            bound += (uint32_t)(self->objOps[i] ? strlen(self->objOps[i]) : 0) + 1;
    }
    else
    {
        bound += 5;
    }

    bound += arguments_bound(self->onLoad, self->onLoadLen);
    bound += arguments_bound(self->onMouseOver, self->onMouseOverLen);
    bound += arguments_bound(self->onMouseLeave, self->onMouseLeaveLen);
    bound += arguments_bound(self->onTargetLeave, self->onTargetLeaveLen);
    bound += arguments_bound(self->onTargetEnter, self->onTargetEnterLen);
    bound += arguments_bound(self->onVarpTransmit, self->onVarpTransmitLen);
    bound += arguments_bound(self->onInvTransmit, self->onInvTransmitLen);
    bound += arguments_bound(self->onStatTransmit, self->onStatTransmitLen);
    bound += arguments_bound(self->onTimer, self->onTimerLen);
    bound += arguments_bound(self->onOp, self->onOpLen);
    bound += arguments_bound(self->onMouseRepeat, self->onMouseRepeatLen);
    bound += arguments_bound(self->onClick, self->onClickLen);
    bound += arguments_bound(self->onClickRepeat, self->onClickRepeatLen);
    bound += arguments_bound(self->onRelease, self->onReleaseLen);
    bound += arguments_bound(self->onHold, self->onHoldLen);
    bound += arguments_bound(self->onDrag, self->onDragLen);
    bound += arguments_bound(self->onDragComplete, self->onDragCompleteLen);
    bound += arguments_bound(self->onScrollWheel, self->onScrollWheelLen);

    bound += (uint32_t)(self->varpTriggersLen + self->inventoryTriggersLen +
                        self->statTriggersLen) *
                 4u +
             3u;
    return bound;
}

uint32_t
RSCache_Dat2ComponentEncodeIf3(
    const struct RSCache_Dat2Component* self,
    struct RSCache_Dat2ComponentDecodeRev rev,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !self || !out )
        return 0;
    if( rev_is_643(rev) )
        return 0; /* the RS2 layout is decoded by decode_if3_rs2 and not inverted here */
    if( out_capacity < RSCache_Dat2ComponentEncodeIf3Bound(self) )
        return 0;
    /* IF1 and IF3 are one dispatch on the way in and one on the way out; the
     * caller should not have to know which era a component came from. */
    if( !self->if3 )
        return component_encode_if1(self, rev, out, out_capacity);

    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, out, out_capacity);

    p1(&buf, 255); /* the IF3 marker the decoder skips */
    p1(&buf, self->type);
    p2(&buf, self->clientCode);
    p2(&buf, self->baseX);
    p2(&buf, self->baseY);
    p2(&buf, self->baseWidth);
    p2(&buf, self->baseHeight);

    p1(&buf, (uint8_t)self->widthMode);
    p1(&buf, (uint8_t)self->heightMode);
    p1(&buf, (uint8_t)self->xMode);
    p1(&buf, (uint8_t)self->yMode);

    /* Stored relative to the owning interface; the decoder adds it back. */
    if( self->layer == -1 )
        p2(&buf, 65535);
    else
        p2(&buf, (self->layer - (self->id & (int32_t)0xFFFF0000u)) & 0xFFFF);

    p1(&buf, self->hidden ? 1 : 0);

    if( self->type == 0 )
    {
        p2(&buf, self->scrollWidth);
        p2(&buf, self->scrollHeight);
        p1(&buf, self->noClickThrough ? 1 : 0);
    }
    if( self->type == 5 )
    {
        p4(&buf, self->graphic);
        p2(&buf, self->angle);
        p1(&buf, (self->alpha ? 0x2 : 0) | (self->tiled ? 0x1 : 0));
        p1(&buf, self->transparency);
        p1(&buf, self->outline);
        p4(&buf, self->graphicShadow);
        p1(&buf, self->verticalFlip ? 1 : 0);
        p1(&buf, self->horizontalFlip ? 1 : 0);
    }
    if( self->type == 6 )
    {
        if( rev_has_int_model_ids(rev) )
        {
            p4(&buf, self->modelId);
        }
        else
        {
            p2(&buf, self->modelId == -1 ? 65535 : (self->modelId & 0xFFFF));
        }
        p2(&buf, self->modelXOffset);
        p2(&buf, self->modelYOffset);
        p2(&buf, self->modelXAngle);
        p2(&buf, self->modelYAngle);
        p2(&buf, self->modelZAngle);
        p2(&buf, self->modelZoom);
        p2(&buf, self->modelSeqId == -1 ? 65535 : (self->modelSeqId & 0xFFFF));
        p1(&buf, self->modelOrthographic ? 1 : 0);
        p2(&buf, (uint16_t)self->aShort50);
        /* Both override shorts travel together whenever either size mode is
         * dynamic — see the decoder's note; gating them separately misaligns
         * everything after. */
        if( self->widthMode != 0 || self->heightMode != 0 )
        {
            p2(&buf, self->anInt5957);
            p2(&buf, self->anInt5920);
        }
    }
    if( self->type == 4 )
    {
        p2(&buf, self->textFont == -1 ? 65535 : (self->textFont & 0xFFFF));
        write_jstring(&buf, self->text);
        p1(&buf, self->textLineHeight);
        p1(&buf, self->textHorizontalAlignment);
        p1(&buf, self->textVerticalAlignment);
        p1(&buf, self->textShadow ? 1 : 0);
        p4(&buf, self->color);
    }
    if( self->type == 3 )
    {
        p4(&buf, self->color);
        p1(&buf, self->fill ? 1 : 0);
        p1(&buf, self->transparency);
    }
    if( self->type == 9 )
    {
        p1(&buf, self->lineWidth);
        p4(&buf, self->color);
        p1(&buf, self->lineDirection ? 1 : 0);
    }

    p3(&buf, self->clickMask);
    write_jstring(&buf, self->name);

    /* The decoder only allocates when the count is positive, so an all-NULL ops
     * array and an absent one are the same state and both write 0. */
    int32_t action_count = self->ops ? self->opsLen : 0;
    p1(&buf, action_count);
    for( int32_t i = 0; i < action_count; i++ )
        write_jstring(&buf, self->ops[i]);

    p1(&buf, self->dragDeadZone);
    p1(&buf, self->dragDeadTime);
    p1(&buf, self->dragRender ? 1 : 0);

    write_jstring(&buf, self->targetVerb);

    write_arguments(&buf, self->onLoad, self->onLoadLen);
    write_arguments(&buf, self->onMouseOver, self->onMouseOverLen);
    write_arguments(&buf, self->onMouseLeave, self->onMouseLeaveLen);
    write_arguments(&buf, self->onTargetLeave, self->onTargetLeaveLen);
    write_arguments(&buf, self->onTargetEnter, self->onTargetEnterLen);
    write_arguments(&buf, self->onVarpTransmit, self->onVarpTransmitLen);
    write_arguments(&buf, self->onInvTransmit, self->onInvTransmitLen);
    write_arguments(&buf, self->onStatTransmit, self->onStatTransmitLen);
    write_arguments(&buf, self->onTimer, self->onTimerLen);
    write_arguments(&buf, self->onOp, self->onOpLen);
    write_arguments(&buf, self->onMouseRepeat, self->onMouseRepeatLen);
    write_arguments(&buf, self->onClick, self->onClickLen);
    write_arguments(&buf, self->onClickRepeat, self->onClickRepeatLen);
    write_arguments(&buf, self->onRelease, self->onReleaseLen);
    write_arguments(&buf, self->onHold, self->onHoldLen);
    write_arguments(&buf, self->onDrag, self->onDragLen);
    write_arguments(&buf, self->onDragComplete, self->onDragCompleteLen);
    write_arguments(&buf, self->onScrollWheel, self->onScrollWheelLen);

    write_triggers(&buf, self->varpTriggers, self->varpTriggersLen);
    write_triggers(&buf, self->inventoryTriggers, self->inventoryTriggersLen);
    write_triggers(&buf, self->statTriggers, self->statTriggersLen);

    return buf.position;
}

void
RSCache_Dat2ComponentSetOp(
    struct RSCache_Dat2Component* self,
    int32_t i,
    const char* op)
{
    assert(i >= 0);
    int32_t need = i + 1;
    if( !self->ops || need > self->opsLen )
    {
        char** nops = calloc((size_t)need, sizeof(char*));
        if( !nops )
            return;
        if( self->ops )
        {
            for( int32_t j = 0; j < self->opsLen; j++ )
                nops[j] = self->ops[j];
            free(self->ops);
        }
        self->ops = nops;
        self->opsLen = need;
    }
    if( self->ops[i] )
        free(self->ops[i]);
    self->ops[i] = op ? strdup(op) : NULL;
}

struct RSCache_Dat2Component*
RSCache_Dat2ComponentNewDecode(
    uint8_t* data,
    int data_size,
    int packed_id,
    struct RSCache_Dat2ComponentDecodeRev rev)
{
    assert(data != NULL);
    assert(data_size > 0);

    struct RSCache_Dat2Component* comp = calloc(1, sizeof(struct RSCache_Dat2Component));
    assert(comp != NULL);

    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, data, (uint32_t)data_size);
    RSCache_Dat2ComponentInit(comp);
    comp->id = packed_id;
    if( (unsigned char)data[0] == (unsigned char)255 )
        RSCache_Dat2ComponentDecodeIf3(comp, &buf, rev);
    else
        RSCache_Dat2ComponentDecodeIf1(comp, &buf, rev);
    return comp;
}

void
RSCache_Dat2ComponentPackFree(struct RSCache_Dat2ComponentPack* pack)
{
    if( !pack )
        return;

    if( pack->components )
    {
        for( int i = 0; i < pack->component_count; i++ )
        {
            if( pack->components[i] )
            {
                RSCache_Dat2ComponentFree(pack->components[i]);
                free(pack->components[i]);
            }
        }
        free(pack->components);
    }
    free(pack);
}

struct RSCache_Dat2ComponentPack*
RSCache_Dat2ComponentPackNewFromFileList(
    struct RSCache_FileList* filelist,
    int interface_id,
    struct RSCache_Dat2ComponentDecodeRev rev,
    int const* file_ids)
{
    assert(filelist != NULL);
    assert(filelist->file_count > 0);

    struct RSCache_Dat2ComponentPack* pack = calloc(1, sizeof(struct RSCache_Dat2ComponentPack));
    assert(pack != NULL);

    pack->component_count = filelist->file_count;
    pack->components = calloc((size_t)filelist->file_count, sizeof(struct RSCache_Dat2Component*));
    assert(pack->components != NULL);

    /* Probed once: this loop runs for every component of every interface. */
    static int dump_bytes = -1;
    if( dump_bytes < 0 )
        dump_bytes = getenv("RSCACHE_DUMP_COMPONENT_BYTES") != NULL;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int file_id = file_ids ? file_ids[i] : i;
        int packed_id = (interface_id << 16) | (file_id & 0xFFFF);
        if( dump_bytes )
        {
            int dump_n = filelist->file_sizes[i] < 26 ? filelist->file_sizes[i] : 26;
            fprintf(stderr, "RAWBYTES iface=%d file=%d:", interface_id, file_id);
            for( int db = 0; db < dump_n; db++ )
                fprintf(stderr, " %02x", (unsigned char)filelist->files[i][db]);
            fprintf(stderr, "\n");
        }
        pack->components[i] = RSCache_Dat2ComponentNewDecode(
            (uint8_t*)filelist->files[i], filelist->file_sizes[i], packed_id, rev);
        assert(pack->components[i] != NULL);
    }

    return pack;
}

struct RSCache_Dat2ComponentPack*
RSCache_Dat2ComponentPackNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int interface_id,
    struct RSCache_Dat2ComponentDecodeRev rev)
{
    assert(archive != NULL);
    struct RSCache_FileList* filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    assert(filelist != NULL);

    /* archive->file_ids comes from the table's reference entry, so it carries the
     * real (possibly sparse) file ids that the uid must be built from. */
    struct RSCache_Dat2ComponentPack* pack =
        RSCache_Dat2ComponentPackNewFromFileList(filelist, interface_id, rev, archive->file_ids);
    RSCache_FileListFree(filelist);
    return pack;
}
