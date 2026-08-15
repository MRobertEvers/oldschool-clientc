#include "ui/uitree_component_options.h"

#include "ui/uitree.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Shared, zeroed, and never written. What the read accessors hand back for a
 * component with no block, so the ~7,000 nodes that have nothing to say cost a
 * NULL pointer rather than 1,368 bytes of zeroes. */
static struct UITreeMenuOptions const k_menu_options_none;
static struct UITreeOpKeys const k_op_keys_none;

/* ------------------------------------------------------------------ */
/* Menu options                                                        */
/* ------------------------------------------------------------------ */

struct UITreeMenuOptions const*
UITree_MenuOptions(struct UITreeComponent const* component)
{
    assert(component);
    return component->menu_options ? component->menu_options : &k_menu_options_none;
}

struct UITreeMenuOptions*
UITree_MenuOptionsMut(struct UITreeComponent* component)
{
    assert(component);
    if( !component->menu_options )
        component->menu_options =
            (struct UITreeMenuOptions*)calloc(1, sizeof(struct UITreeMenuOptions));
    return component->menu_options;
}

int
UITree_HasMenuOptions(struct UITreeComponent const* component)
{
    return component && component->menu_options != NULL;
}

/* Is there anything here worth allocating a block for? Cheaper than it looks:
 * the common case is the first test failing. */
static int
menu_options_is_empty(struct UITreeMenuOptions const* opts)
{
    assert(opts);
    if( opts->option[0] || opts->target_verb[0] || opts->target_base[0] )
        return 0;
    if( opts->option_action || opts->submenus )
        return 0;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        if( opts->ops[i][0] || opts->op_actions[i] )
            return 0;
    return 1;
}

void
UITree_MenuOptionsSet(
    struct UITreeComponent* component,
    struct UITreeMenuOptions const* src)
{
    struct UITreeMenuOptions* dst;
    struct UITreeMenuSubmenuOptions* submenu_copy = NULL;

    assert(component);

    if( menu_options_is_empty(src) )
    {
        UITree_MenuOptionsFree(component);
        return;
    }
    if( src == component->menu_options )
        return;

    /* The submenu block is owned per component and must not be aliased —
     * both sides are reclaimed independently. */
    if( src->submenus )
    {
        submenu_copy =
            (struct UITreeMenuSubmenuOptions*)calloc(1, sizeof(struct UITreeMenuSubmenuOptions));
        if( submenu_copy )
            memcpy(submenu_copy, src->submenus, sizeof(*submenu_copy));
    }

    dst = UITree_MenuOptionsMut(component);
    if( !dst )
    {
        free(submenu_copy);
        return;
    }
    UITree_MenuSubmenuFree(dst);
    *dst = *src;
    dst->submenus = submenu_copy;
}

void
UITree_MenuOptionsFree(struct UITreeComponent* component)
{
    if( !component || !component->menu_options )
        return;
    UITree_MenuSubmenuFree(component->menu_options);
    free(component->menu_options);
    component->menu_options = NULL;
}

/* ------------------------------------------------------------------ */
/* Op keys                                                             */
/* ------------------------------------------------------------------ */

struct UITreeOpKeys const*
UITree_OpKeys(struct UITreeComponent const* component)
{
    assert(component);
    return component->op_keys ? component->op_keys : &k_op_keys_none;
}

struct UITreeOpKeys*
UITree_OpKeysMut(struct UITreeComponent* component)
{
    assert(component);
    if( !component->op_keys )
        component->op_keys = (struct UITreeOpKeys*)calloc(1, sizeof(struct UITreeOpKeys));
    return component->op_keys;
}

int
UITree_HasOpKeys(struct UITreeComponent const* component)
{
    return component && component->op_keys != NULL;
}

void
UITree_OpKeysSet(
    struct UITreeComponent* component,
    struct UITreeOpKeys const* src)
{
    struct UITreeOpKeys* dst;
    int binds = 0;

    assert(component);

    if( src )
    {
        binds = src->has_bindings;
        for( int i = 0; i < UITREE_OPKEY_SLOTS && !binds; i++ )
            binds = src->slots[i].bound;
    }
    if( !binds )
    {
        UITree_OpKeysFree(component);
        return;
    }
    if( src == component->op_keys )
        return;

    dst = UITree_OpKeysMut(component);
    if( dst )
        *dst = *src;
}

void
UITree_OpKeysFree(struct UITreeComponent* component)
{
    if( !component )
        return;
    free(component->op_keys);
    component->op_keys = NULL;
}
