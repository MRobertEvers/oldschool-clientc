#include "config_component.h"

#include "../rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
init_component(struct CacheDatConfigComponent* component)
{
    memset(component, 0, sizeof(struct CacheDatConfigComponent));
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
    component->invSlotObjId = NULL;
    component->invSlotObjCount = NULL;
    component->seqFrame = 0;
    component->seqCycle = 0;
    component->children = NULL;
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
    component->text = NULL;
    component->activeText = NULL;
    component->draggable = false;
    component->interactable = false;
    component->usable = false;
    component->swappable = false;
    component->fill = false;
    component->center = false;
}

static struct CacheDatConfigComponent*
decode_component(struct RSBuffer* inb)
{
    struct CacheDatConfigComponent* component = malloc(sizeof(struct CacheDatConfigComponent));
    init_component(component);

    int layer = -1;

    while( inb->position < inb->size )
    {
        int id = g2(inb);
        if( id == 65535 )
        {
            layer = g2(inb);
            id = g2(inb);
        }

        component->id = id;
        component->layer = layer;
        component->type = g1(inb);
        component->buttonType = g1(inb);
        component->clientCode = g2(inb);
        component->width = g2(inb);
        component->height = g2(inb);
        component->alpha = g1(inb);

        component->overlayer = g1(inb);
        if( component->overlayer == 0 )
        {
            component->overlayer = -1;
        }
        else
        {
            component->overlayer = ((component->overlayer - 1) << 8) + g1(inb);
        }

        int comparatorCount = g1(inb);
        if( comparatorCount > 0 )
        {
            component->scriptComparator = malloc(comparatorCount * sizeof(int));
            memset(component->scriptComparator, 0, comparatorCount * sizeof(int));
            component->scriptOperand = malloc(comparatorCount * sizeof(int));
            memset(component->scriptOperand, 0, comparatorCount * sizeof(int));

            for( int i = 0; i < comparatorCount; i++ )
            {
                component->scriptComparator[i] = g1(inb);
                component->scriptOperand[i] = g2(inb);
            }
        }

        int scriptCount = g1(inb);
        if( scriptCount > 0 )
        {
            component->scripts = malloc(scriptCount * sizeof(int*));
            memset(component->scripts, 0, scriptCount * sizeof(int*));

            component->scripts_lengths = malloc(scriptCount * sizeof(int));
            memset(component->scripts_lengths, 0, scriptCount * sizeof(int));

            for( int i = 0; i < scriptCount; i++ )
            {
                int opcodeCount = g2(inb);

                component->scripts_lengths[i] = opcodeCount;
                component->scripts[i] = malloc(opcodeCount * sizeof(int));
                memset(component->scripts[i], 0, opcodeCount * sizeof(int));

                for( int j = 0; j < opcodeCount; j++ )
                {
                    component->scripts[i][j] = g2(inb);
                }
            }
        }

        if( component->type == COMPONENT_TYPE_LAYER )
        {
            component->scroll = g2(inb);
            component->hide = g1(inb) == 1;

            int childCount = g2(inb);
            component->children = malloc(childCount * sizeof(int));
            memset(component->children, 0, childCount * sizeof(int));
            component->childX = malloc(childCount * sizeof(int));
            memset(component->childX, 0, childCount * sizeof(int));
            component->childY = malloc(childCount * sizeof(int));
            memset(component->childY, 0, childCount * sizeof(int));

            for( int i = 0; i < childCount; i++ )
            {
                component->children[i] = g2(inb);
                component->childX[i] = g2b(inb);
                component->childY[i] = g2b(inb);
            }
        }

        if( component->type == COMPONENT_TYPE_UNUSED )
        {
            inb->position += 3;
        }

        if( component->type == COMPONENT_TYPE_INV )
        {
            component->invSlotObjId = malloc(component->width * component->height * sizeof(int));
            memset(component->invSlotObjId, 0, component->width * component->height * sizeof(int));
            component->invSlotObjCount = malloc(component->width * component->height * sizeof(int));
            memset(
                component->invSlotObjCount, 0, component->width * component->height * sizeof(int));

            component->draggable = g1(inb) == 1;
            component->interactable = g1(inb) == 1;
            component->usable = g1(inb) == 1;
            component->swappable = g1(inb) == 1;
            component->marginX = g1(inb);
            component->marginY = g1(inb);

            component->invSlotOffsetX = malloc(20 * sizeof(int));
            memset(component->invSlotOffsetX, 0, 20 * sizeof(int));
            component->invSlotOffsetY = malloc(20 * sizeof(int));
            memset(component->invSlotOffsetY, 0, 20 * sizeof(int));
            component->usable = g1(inb) == 1;
            component->swappable = g1(inb) == 1;
            component->marginX = g1(inb);
            component->marginY = g1(inb);

            component->invSlotOffsetX = malloc(20 * sizeof(int));
            memset(component->invSlotOffsetX, 0, 20 * sizeof(int));
            component->invSlotOffsetY = malloc(20 * sizeof(int));
            memset(component->invSlotOffsetY, 0, 20 * sizeof(int));
            component->invSlotGraphic = malloc(20 * sizeof(char*));
            memset(component->invSlotGraphic, 0, 20 * sizeof(char*));

            for( int i = 0; i < 20; i++ )
            {
                if( g1(inb) == 1 )
                {
                    component->invSlotOffsetX[i] = g2b(inb);
                    component->invSlotOffsetY[i] = g2b(inb);

                    char* graphic = gstringnewline(inb);
                    component->invSlotGraphic[i] = graphic;
                }
            }

            component->iop = malloc(5 * sizeof(char*));
            memset(component->iop, 0, 5 * sizeof(char*));
            for( int i = 0; i < 5; i++ )
            {
                component->iop[i] = gstringnewline(inb);
                if( component->iop[i] != NULL && strlen(component->iop[i]) == 0 )
                    component->iop[i] = NULL;
            }
        }

        if( component->type == COMPONENT_TYPE_RECT )
        {
            component->fill = g1(inb) == 1;
        }

        if( component->type == COMPONENT_TYPE_TEXT || component->type == COMPONENT_TYPE_UNUSED )
        {
            component->center = g1(inb) == 1;
            int font = g1(inb);
            component->font = font;
            component->shadowed = g1(inb) == 1;
        }

        if( component->type == COMPONENT_TYPE_TEXT )
        {
            component->text = gstringnewline(inb);
            component->activeText = gstringnewline(inb);
        }

        if( component->type == COMPONENT_TYPE_UNUSED || component->type == COMPONENT_TYPE_RECT ||
            component->type == COMPONENT_TYPE_TEXT )
        {
            component->colour = g4(inb);
        }

        if( component->type == COMPONENT_TYPE_RECT || component->type == COMPONENT_TYPE_TEXT )
        {
            component->activeColour = g4(inb);
            component->overColour = g4(inb);
            component->activeOverColour = g4(inb);
        }

        if( component->type == COMPONENT_TYPE_GRAPHIC )
        {
            char* graphic = gstringnewline(inb);
            component->graphic = graphic;

            char* activeGraphic = gstringnewline(inb);
            component->activeGraphic = activeGraphic;
        }

        if( component->type == COMPONENT_TYPE_MODEL )
        {
            int model = g1(inb);
            if( model != 0 )
            {
                component->modelType = 1;
                component->model = ((model - 1) << 8) + g1(inb);
            }

            int activeModel = g1(inb);
            if( activeModel != 0 )
            {
                component->activeModelType = 1;
                component->activeModel = ((activeModel - 1) << 8) + g1(inb);
            }

            component->anim = g1(inb);
            if( component->anim == 0 )
            {
                component->anim = -1;
            }
            else
            {
                component->anim = ((component->anim - 1) << 8) + g1(inb);
            }

            component->activeAnim = g1(inb);
            if( component->activeAnim == 0 )
            {
                component->activeAnim = -1;
            }
            else
            {
                component->activeAnim = ((component->activeAnim - 1) << 8) + g1(inb);
            }

            component->zoom = g2(inb);
            component->xan = g2(inb);
            component->yan = g2(inb);
        }

        if( component->type == COMPONENT_TYPE_INV_TEXT )
        {
            component->invSlotObjId = malloc(component->width * component->height * sizeof(int));
            memset(component->invSlotObjId, 0, component->width * component->height * sizeof(int));
            component->invSlotObjCount = malloc(component->width * component->height * sizeof(int));
            memset(
                component->invSlotObjCount, 0, component->width * component->height * sizeof(int));

            component->center = g1(inb) == 1;
            component->font = g1(inb);

            component->shadowed = g1(inb) == 1;
            component->colour = g4(inb);
            component->marginX = g2b(inb);
            component->marginY = g2b(inb);
            component->interactable = g1(inb) == 1;

            component->iop = malloc(5 * sizeof(char*));
            memset(component->iop, 0, 5 * sizeof(char*));
            for( int i = 0; i < 5; i++ )
            {
                component->iop[i] = gstringnewline(inb);
                if( component->iop[i] != NULL && strlen(component->iop[i]) == 0 )
                    component->iop[i] = NULL;
            }
        }
    }

    if( component->buttonType == COMPONENT_BUTTON_TYPE_TARGET ||
        component->type == COMPONENT_TYPE_INV )
    {
        component->targetVerb = gstringnewline(inb);
        component->targetText = gstringnewline(inb);
        component->targetMask = g2(inb);
    }

    if( component->buttonType == COMPONENT_BUTTON_TYPE_OK ||
        component->buttonType == COMPONENT_BUTTON_TYPE_TOGGLE ||
        component->buttonType == COMPONENT_BUTTON_TYPE_SELECT ||
        component->buttonType == COMPONENT_BUTTON_TYPE_CONTINUE )
    {
        component->option = gstringnewline(inb);

        if( component->option != NULL && strlen(component->option) == 0 )
        {
            if( component->buttonType == COMPONENT_BUTTON_TYPE_OK )
            {
                component->option = "Ok";
            }
            else if( component->buttonType == COMPONENT_BUTTON_TYPE_TOGGLE )
            {
                component->option = "Select";
            }
            else if( component->buttonType == COMPONENT_BUTTON_TYPE_SELECT )
            {
                component->option = "Select";
            }
            else if( component->buttonType == COMPONENT_BUTTON_TYPE_CONTINUE )
            {
                component->option = "Continue";
            }
        }
    }

    return component;
}

struct CacheDatConfigComponentList*
cache_dat_config_component_list_new_decode(
    void* data,
    int size)
{
    struct RSBuffer buffer;
    rsbuf_init(&buffer, data, size);

    struct CacheDatConfigComponentList* component_list =
        malloc(sizeof(struct CacheDatConfigComponentList));
    memset(component_list, 0, sizeof(struct CacheDatConfigComponentList));

    int component_count = g2(&buffer);

    component_list->components = malloc(component_count * sizeof(struct CacheDatConfigComponent**));
    memset(
        component_list->components, 0, component_count * sizeof(struct CacheDatConfigComponent*));

    component_list->components_count = component_count;

    for( int i = 0; i < component_count; i++ )
    {
        component_list->components[i] = decode_component(&buffer);
        if( component_list->components[i] == NULL )
        {
            assert(false && "Failed to decode component");
            return NULL;
        }
    }
    return component_list;
}