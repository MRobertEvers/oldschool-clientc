#include "dat1a_config_component.h"

#include "../shared/shared_rs_buffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
init_component(struct RSCacheDat1A_ConfigComponent* component)
{
    memset(component, 0, sizeof(struct RSCacheDat1A_ConfigComponent));
    component->id = -1;
    component->layer = -1;
    component->type = -1;
    component->buttonType = -1;
    component->clientCode = 0;
    component->width = 0;
    component->height = 0;
    component->alpha = 0;
    component->overlayer = -1;
    component->scriptComparator = NULL;
    component->scriptOperand = NULL;
    component->scripts = NULL;
    component->scripts_count = 0;
    component->scripts_lengths = NULL;
    component->invSlotObjId = NULL;
    component->invSlotObjCount = NULL;
    component->seqFrame = 0;
    component->seqCycle = 0;
    component->children = NULL;
    component->children_count = 0;
    component->activeModelType = 0;
    component->activeModel = 0;
    component->anim = -1;
    component->activeAnim = -1;
    component->zoom = 0;
    component->xan = 0;
    component->yan = 0;
    component->targetVerb = NULL;
    component->targetText = NULL;
    component->targetMask = -1;
    component->option = NULL;
    component->marginX = 0;
    component->marginY = 0;
    component->colour = 0;
    component->activeColour = 0;
    component->overColour = 0;
    component->activeOverColour = 0;
    component->modelType = 0;
    component->model = 0;
    component->text = NULL;
    component->activeText = NULL;
    component->draggable = false;
    component->interactable = false;
    component->usable = false;
    component->swappable = false;
    component->fill = false;
    component->center = false;
    component->shadowed = false;
    component->invSlotOffsetX = NULL;
    component->invSlotOffsetY = NULL;
    component->childX = NULL;
    component->childY = NULL;
    component->iop = NULL;
}

static void
discard_string(char* s)
{
    free(s);
}

static int
read_component_id(
    struct RSCacheShared_RSBuffer* inb,
    int* layer_inout)
{
    int id = g2(inb);
    if( id == 65535 )
    {
        *layer_inout = g2(inb);
        id = g2(inb);
    }
    return id;
}

static void
read_component_body(
    struct RSCacheShared_RSBuffer* inb,
    struct RSCacheDat1A_ConfigComponent* component,
    int id,
    int layer)
{
    const bool store = component != NULL;

    if( store )
    {
        component->id = id;
        component->layer = layer;
    }

    int type = g1(inb);
    int buttonType = g1(inb);
    int clientCode = g2(inb);
    int width = g2(inb);
    int height = g2(inb);
    int alpha = g1(inb);

    if( store )
    {
        component->type = type;
        component->buttonType = buttonType;
        component->clientCode = clientCode;
        component->width = width;
        component->height = height;
        component->alpha = alpha;
    }

    int overlayer = g1(inb);
    if( overlayer == 0 )
    {
        overlayer = -1;
    }
    else
    {
        overlayer = ((overlayer - 1) << 8) + g1(inb);
    }
    if( store )
        component->overlayer = overlayer;

    int comparatorCount = g1(inb);
    if( comparatorCount > 0 )
    {
        if( store )
        {
            component->scriptComparator = malloc(comparatorCount * sizeof(int));
            memset(component->scriptComparator, 0, comparatorCount * sizeof(int));
            component->scriptOperand = malloc(comparatorCount * sizeof(int));
            memset(component->scriptOperand, 0, comparatorCount * sizeof(int));
        }

        for( int i = 0; i < comparatorCount; i++ )
        {
            int cmp = g1(inb);
            int op = g2(inb);
            if( store )
            {
                component->scriptComparator[i] = cmp;
                component->scriptOperand[i] = op;
            }
        }
    }

    int scriptCount = g1(inb);
    if( scriptCount > 0 )
    {
        if( store )
        {
            component->scripts_count = scriptCount;
            component->scripts = malloc(scriptCount * sizeof(int*));
            memset(component->scripts, 0, scriptCount * sizeof(int*));
            component->scripts_lengths = malloc(scriptCount * sizeof(int));
            memset(component->scripts_lengths, 0, scriptCount * sizeof(int));
        }

        for( int i = 0; i < scriptCount; i++ )
        {
            int opcodeCount = g2(inb);
            if( store )
            {
                component->scripts_lengths[i] = opcodeCount;
                component->scripts[i] = malloc(opcodeCount * sizeof(int));
                memset(component->scripts[i], 0, opcodeCount * sizeof(int));
            }

            for( int j = 0; j < opcodeCount; j++ )
            {
                int opcode = g2(inb);
                if( store )
                    component->scripts[i][j] = opcode;
            }
        }
    }

    if( type == COMPONENT_TYPE_LAYER )
    {
        int scroll = g2(inb);
        bool hide = g1(inb) == 1;
        int childCount = g2(inb);

        if( store )
        {
            component->scroll = scroll;
            component->hide = hide;
            component->children_count = childCount;
            component->children = malloc(childCount * sizeof(int));
            memset(component->children, 0, childCount * sizeof(int));
            component->childX = malloc(childCount * sizeof(int));
            memset(component->childX, 0, childCount * sizeof(int));
            component->childY = malloc(childCount * sizeof(int));
            memset(component->childY, 0, childCount * sizeof(int));
        }

        for( int i = 0; i < childCount; i++ )
        {
            int child = g2(inb);
            int child_x = g2b(inb);
            int child_y = g2b(inb);
            if( store )
            {
                component->children[i] = child;
                component->childX[i] = child_x;
                component->childY[i] = child_y;
            }
        }
    }

    if( type == COMPONENT_TYPE_UNUSED )
    {
        inb->position += 3;
    }

    if( type == COMPONENT_TYPE_INV )
    {
        if( store )
        {
            component->invSlotObjId = malloc(width * height * sizeof(int));
            memset(component->invSlotObjId, 0, width * height * sizeof(int));
            component->invSlotObjCount = malloc(width * height * sizeof(int));
            memset(component->invSlotObjCount, 0, width * height * sizeof(int));
        }

        bool draggable = g1(inb) == 1;
        bool interactable = g1(inb) == 1;
        bool usable = g1(inb) == 1;
        bool swappable = g1(inb) == 1;
        int marginX = g1(inb);
        int marginY = g1(inb);

        if( store )
        {
            component->draggable = draggable;
            component->interactable = interactable;
            component->usable = usable;
            component->swappable = swappable;
            component->marginX = marginX;
            component->marginY = marginY;
            component->invSlotOffsetX = malloc(20 * sizeof(int));
            memset(component->invSlotOffsetX, 0, 20 * sizeof(int));
            component->invSlotOffsetY = malloc(20 * sizeof(int));
            memset(component->invSlotOffsetY, 0, 20 * sizeof(int));
            component->invSlotGraphic = malloc(20 * sizeof(char*));
            memset(component->invSlotGraphic, 0, 20 * sizeof(char*));
        }

        for( int i = 0; i < 20; i++ )
        {
            if( g1(inb) == 1 )
            {
                int off_x = g2b(inb);
                int off_y = g2b(inb);
                char* graphic = gstringnewline(inb);
                if( store )
                {
                    component->invSlotOffsetX[i] = off_x;
                    component->invSlotOffsetY[i] = off_y;
                    component->invSlotGraphic[i] = graphic;
                    if( component->invSlotGraphic[i] != NULL &&
                        strlen(component->invSlotGraphic[i]) == 0 )
                    {
                        free(component->invSlotGraphic[i]);
                        component->invSlotGraphic[i] = NULL;
                    }
                }
                else
                {
                    discard_string(graphic);
                }
            }
        }

        if( store )
        {
            component->iop = malloc(5 * sizeof(char*));
            memset(component->iop, 0, 5 * sizeof(char*));
        }

        for( int i = 0; i < 5; i++ )
        {
            char* iop = gstringnewline(inb);
            if( store )
            {
                component->iop[i] = iop;
                if( component->iop[i] != NULL && strlen(component->iop[i]) == 0 )
                {
                    free(component->iop[i]);
                    component->iop[i] = NULL;
                }
            }
            else
            {
                discard_string(iop);
            }
        }
    }

    if( type == COMPONENT_TYPE_RECT )
    {
        bool fill = g1(inb) == 1;
        if( store )
            component->fill = fill;
    }

    if( type == COMPONENT_TYPE_TEXT || type == COMPONENT_TYPE_UNUSED )
    {
        bool center = g1(inb) == 1;
        int font = g1(inb);
        bool shadowed = g1(inb) == 1;
        if( store )
        {
            component->center = center;
            component->font = font;
            component->shadowed = shadowed;
        }
    }

    if( type == COMPONENT_TYPE_TEXT )
    {
        char* text = gstringnewline(inb);
        char* activeText = gstringnewline(inb);
        if( store )
        {
            component->text = text;
            component->activeText = activeText;
        }
        else
        {
            discard_string(text);
            discard_string(activeText);
        }
    }

    if( type == COMPONENT_TYPE_UNUSED || type == COMPONENT_TYPE_RECT || type == COMPONENT_TYPE_TEXT )
    {
        int colour = g4(inb);
        if( store )
            component->colour = colour;
    }

    if( type == COMPONENT_TYPE_RECT || type == COMPONENT_TYPE_TEXT )
    {
        int activeColour = g4(inb);
        int overColour = g4(inb);
        int activeOverColour = g4(inb);
        if( store )
        {
            component->activeColour = activeColour;
            component->overColour = overColour;
            component->activeOverColour = activeOverColour;
        }
    }

    if( type == COMPONENT_TYPE_GRAPHIC )
    {
        char* graphic = gstringnewline(inb);
        char* activeGraphic = gstringnewline(inb);
        if( store )
        {
            component->graphic = graphic;
            component->activeGraphic = activeGraphic;
        }
        else
        {
            discard_string(graphic);
            discard_string(activeGraphic);
        }
    }

    if( type == COMPONENT_TYPE_MODEL )
    {
        int model = g1(inb);
        int modelType = 0;
        int modelId = 0;
        if( model != 0 )
        {
            modelType = 1;
            modelId = ((model - 1) << 8) + g1(inb);
        }

        int activeModel = g1(inb);
        int activeModelType = 0;
        int activeModelId = 0;
        if( activeModel != 0 )
        {
            activeModelType = 1;
            activeModelId = ((activeModel - 1) << 8) + g1(inb);
        }

        int anim = g1(inb);
        if( anim == 0 )
        {
            anim = -1;
        }
        else
        {
            anim = ((anim - 1) << 8) + g1(inb);
        }

        int activeAnim = g1(inb);
        if( activeAnim == 0 )
        {
            activeAnim = -1;
        }
        else
        {
            activeAnim = ((activeAnim - 1) << 8) + g1(inb);
        }

        int zoom = g2(inb);
        int xan = g2(inb);
        int yan = g2(inb);

        if( store )
        {
            component->modelType = modelType;
            component->model = modelId;
            component->activeModelType = activeModelType;
            component->activeModel = activeModelId;
            component->anim = anim;
            component->activeAnim = activeAnim;
            component->zoom = zoom;
            component->xan = xan;
            component->yan = yan;
        }
    }

    if( type == COMPONENT_TYPE_INV_TEXT )
    {
        if( store )
        {
            component->invSlotObjId = malloc(width * height * sizeof(int));
            memset(component->invSlotObjId, 0, width * height * sizeof(int));
            component->invSlotObjCount = malloc(width * height * sizeof(int));
            memset(component->invSlotObjCount, 0, width * height * sizeof(int));
        }

        bool center = g1(inb) == 1;
        int font = g1(inb);
        bool shadowed = g1(inb) == 1;
        int colour = g4(inb);
        int marginX = g2b(inb);
        int marginY = g2b(inb);
        bool interactable = g1(inb) == 1;

        if( store )
        {
            component->center = center;
            component->font = font;
            component->shadowed = shadowed;
            component->colour = colour;
            component->marginX = marginX;
            component->marginY = marginY;
            component->interactable = interactable;
            component->iop = malloc(5 * sizeof(char*));
            memset(component->iop, 0, 5 * sizeof(char*));
        }

        for( int i = 0; i < 5; i++ )
        {
            char* iop = gstringnewline(inb);
            if( store )
            {
                component->iop[i] = iop;
                if( component->iop[i] != NULL && strlen(component->iop[i]) == 0 )
                {
                    free(component->iop[i]);
                    component->iop[i] = NULL;
                }
            }
            else
            {
                discard_string(iop);
            }
        }
    }

    if( buttonType == COMPONENT_BUTTON_TYPE_TARGET || type == COMPONENT_TYPE_INV )
    {
        char* targetVerb = gstringnewline(inb);
        char* targetText = gstringnewline(inb);
        int targetMask = g2(inb);
        if( store )
        {
            component->targetVerb = targetVerb;
            component->targetText = targetText;
            component->targetMask = targetMask;
        }
        else
        {
            discard_string(targetVerb);
            discard_string(targetText);
        }
    }

    if( buttonType == COMPONENT_BUTTON_TYPE_OK || buttonType == COMPONENT_BUTTON_TYPE_TOGGLE ||
        buttonType == COMPONENT_BUTTON_TYPE_SELECT || buttonType == COMPONENT_BUTTON_TYPE_CONTINUE )
    {
        char* option = gstringnewline(inb);
        if( store )
        {
            component->option = option;
            if( component->option != NULL && strlen(component->option) == 0 )
            {
                free((void*)component->option);
                if( buttonType == COMPONENT_BUTTON_TYPE_OK )
                {
                    component->option = strdup("Ok");
                }
                else if( buttonType == COMPONENT_BUTTON_TYPE_TOGGLE )
                {
                    component->option = strdup("Select");
                }
                else if( buttonType == COMPONENT_BUTTON_TYPE_SELECT )
                {
                    component->option = strdup("Select");
                }
                else if( buttonType == COMPONENT_BUTTON_TYPE_CONTINUE )
                {
                    component->option = strdup("Continue");
                }
            }
        }
        else
        {
            discard_string(option);
        }
    }
}

static struct RSCacheDat1A_ConfigComponentList*
config_component_list_new_decode_impl(
    void* data,
    int size,
    bool* needed,
    int needed_count)
{
    struct RSCacheShared_RSBuffer buffer;
    RSCacheShared_RSBufferInit(&buffer, data, size);

    struct RSCacheDat1A_ConfigComponentList* component_list =
        malloc(sizeof(struct RSCacheDat1A_ConfigComponentList));
    memset(component_list, 0, sizeof(struct RSCacheDat1A_ConfigComponentList));

    int component_count = g2(&buffer);

    component_list->components = malloc(component_count * sizeof(struct RSCacheDat1A_ConfigComponent**));
    memset(
        component_list->components, 0, component_count * sizeof(struct RSCacheDat1A_ConfigComponent*));

    component_list->components_count = component_count;

    int layer = -1;
    while( buffer.position < buffer.size )
    {
        int id = read_component_id(&buffer, &layer);
        bool want = !needed;
        if( needed )
            want = id >= 0 && id < needed_count && needed[id];

        struct RSCacheDat1A_ConfigComponent* comp = NULL;
        if( want )
        {
            comp = malloc(sizeof(struct RSCacheDat1A_ConfigComponent));
            init_component(comp);
            read_component_body(&buffer, comp, id, layer);
        }
        else
        {
            read_component_body(&buffer, NULL, id, layer);
        }

        if( comp )
        {
            if( comp->id >= 0 && comp->id < component_count )
                component_list->components[comp->id] = comp;

            if( needed && comp->type == COMPONENT_TYPE_LAYER && comp->children )
            {
                for( int i = 0; i < comp->children_count; i++ )
                {
                    int child = comp->children[i];
                    if( child >= 0 && child < needed_count )
                        needed[child] = true;
                }
            }
        }
    }

    return component_list;
}

struct RSCacheDat1A_ConfigComponentList*
RSCacheDat1A_ConfigComponentListNewDecode(
    void* data,
    int size)
{
    return config_component_list_new_decode_impl(data, size, NULL, 0);
}

struct RSCacheDat1A_ConfigComponentList*
RSCacheDat1A_ConfigComponentListNewDecodeFiltered(
    void* data,
    int size,
    bool* needed,
    int needed_count)
{
    return config_component_list_new_decode_impl(data, size, needed, needed_count);
}

int
RSCacheDat1A_ConfigComponentListPeekCount(
    void* data,
    int size)
{
    if( !data || size < 2 )
        return 0;

    struct RSCacheShared_RSBuffer buffer;
    RSCacheShared_RSBufferInit(&buffer, data, size);
    return g2(&buffer);
}

void
RSCacheDat1A_ConfigComponentFree(struct RSCacheDat1A_ConfigComponent* c)
{
    if( !c )
        return;

    free(c->scriptComparator);
    free(c->scriptOperand);

    if( c->scripts )
    {
        for( int i = 0; i < c->scripts_count; i++ )
            free(c->scripts[i]);
        free(c->scripts);
    }
    free(c->scripts_lengths);

    free(c->invSlotObjId);
    free(c->invSlotObjCount);

    free(c->children);
    free(c->childX);
    free(c->childY);

    free(c->invSlotOffsetX);
    free(c->invSlotOffsetY);

    if( c->invSlotGraphic )
    {
        for( int i = 0; i < 20; i++ )
            free(c->invSlotGraphic[i]);
        free(c->invSlotGraphic);
    }

    if( c->iop )
    {
        for( int i = 0; i < 5; i++ )
            free(c->iop[i]);
        free(c->iop);
    }

    free(c->targetVerb);
    free(c->targetText);
    free((void*)c->option);
    free(c->text);
    free(c->activeText);
    free(c->graphic);
    free(c->activeGraphic);

    free(c);
}
