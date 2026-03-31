#include "component.h"

#include "../rsbuf.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Localised defaults (LocalisedText in Java) */
static const char kOptionOk[] = "Ok";
static const char kOptionSelect[] = "Select";
static const char kOptionContinue[] = "Continue";

static char*
read_jstring(struct RSBuffer* buf)
{
    char* s = gcstring(buf);
    return s ? s : strdup("");
}

static void
free_script_vars(
    ComponentScriptVar* v,
    int32_t n)
{
    if( !v )
        return;
    for( int32_t i = 0; i < n; i++ )
    {
        if( v[i].type == SCRIPT_VAR_STRING && v[i].value.s )
            free(v[i].value.s);
    }
    free(v);
}

static ComponentScriptVar*
read_arguments(
    struct RSBuffer* buf,
    int32_t* outLen,
    bool* hasHook)
{
    int32_t len = g1(buf);
    if( len == 0 )
    {
        *outLen = 0;
        return NULL;
    }
    ComponentScriptVar* args = calloc((size_t)len, sizeof(ComponentScriptVar));
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
            args[i].type = SCRIPT_VAR_INT;
            args[i].value.i = (int32_t)g4(buf);
        }
        else
        {
            args[i].type = SCRIPT_VAR_STRING;
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
    struct RSBuffer* buf,
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
Component_init(Component* c)
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
Component_free(Component* c)
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
    free(c->cs1ComparisonOpcodes);
    free(c->cs1ComparisonOperands);

    if( c->createdComponents )
    {
        for( int32_t i = 0; i < c->createdComponentsLen; i++ )
        {
            if( c->createdComponents[i] )
            {
                Component_free(c->createdComponents[i]);
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

void
Component_clearStaticCaches(void)
{
    /* SoftLruHashTable not ported in C yet; mirrors Component.clear() no-op for statics. */
}

void
Component_decodeIf1(
    Component* self,
    struct RSBuffer* buf)
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
        for( int32_t i = 0; i < scriptCount; i++ )
        {
            int32_t opCount = g2(buf);
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
        self->modelId = g2(buf);
        self->anInt5909 = 1;
        if( self->modelId == 65535 )
            self->modelId = -1;
        self->activeModelId = g2(buf);
        if( self->activeModelId == 65535 )
            self->activeModelId = -1;
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
    Component* self,
    struct RSBuffer* buf,
    ComponentScriptVar** out,
    int32_t* outLen)
{
    bool hookFlag = false;
    *out = read_arguments(buf, outLen, &hookFlag);
    if( hookFlag )
        self->hasHook = true;
}

void
Component_decodeIf3(
    Component* self,
    struct RSBuffer* buf)
{
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
        self->textureId = (int32_t)g2(buf);
        self->tiled = (g1(buf) == 1);
        self->transparency = g1(buf);
        self->outline = g1(buf);
        self->graphicShadow = (int32_t)g4(buf);
        self->verticalFlip = (g1(buf) == 1);
        self->horizontalFlip = (g1(buf) == 1);
    }
    if( self->type == 6 )
    {
        self->modelType = 1;
        self->modelId = g2(buf);
        if( self->modelId == 65535 )
            self->modelId = -1;
        self->modelXOffset = g2b(buf);
        self->modelYOffset = g2b(buf);
        self->modelXAngle = g2(buf);
        self->modelZAngle = g2(buf);
        self->modelYAngle = g2(buf);
        self->modelZoom = g2(buf);
        self->modelSeqId = g2(buf);
        if( self->modelSeqId == 65535 )
            self->modelSeqId = -1;
        self->modelOrthographic = (g1(buf) == 1);
        g2(buf);
        if( self->widthMode != 0 )
            self->anInt5957 = g2(buf);
        if( self->heightMode != 0 )
            g2(buf);
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

void
Component_setOp(
    Component* self,
    int32_t i,
    const char* op)
{
    if( i < 0 )
        return;
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

Model*
Component_getModel(
    Component* self,
    SeqType* seq,
    PlayerAppearance* appearance,
    int32_t frame,
    int32_t arg3,
    int32_t arg4,
    bool arg5)
{
    (void)self;
    (void)seq;
    (void)appearance;
    (void)frame;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    return NULL;
}
