#include "list.h"

#include <stdlib.h>
#include <string.h>

static struct ListCell*
make_cell(void)
{
    struct ListCell* c = malloc(sizeof(struct ListCell));
    c->next = NULL;
    return c;
}

struct List*
list_make(enum ListCellType type)
{
    struct List* l = malloc(sizeof(struct List));
    l->type = type;
    l->length = 0;
    l->head = l->tail = NULL;
    return l;
}

struct List*
list_make1_int(int value)
{
    struct List* l = list_make(LIST_INT);
    return list_append_int(l, value);
}

struct List*
list_make1_ptr(void* ptr)
{
    struct List* l = list_make(LIST_PTR);
    return list_append_ptr(l, ptr);
}

struct List*
list_append_int(struct List* l, int value)
{
    struct ListCell* c = make_cell();
    c->data._int = value;

    if( l->tail )
        l->tail->next = c;
    else
        l->head = c;

    l->tail = c;
    l->length++;
    return l;
}

struct List*
list_append_ptr(struct List* l, void* ptr)
{
    struct ListCell* c = make_cell();
    c->data._ptr = ptr;

    if( l->tail )
        l->tail->next = c;
    else
        l->head = c;

    l->tail = c;
    l->length++;
    return l;
}

int
list_length(const struct List* l)
{
    return l ? l->length : 0;
}

bool
list_member_int(const struct List* l, int value)
{
    if( !l || l->type != LIST_INT )
        return false;

    for( struct ListCell* c = l->head; c; c = c->next )
        if( c->data._int == value )
            return true;

    return false;
}

bool
list_member_ptr(const struct List* l, void* ptr)
{
    if( !l || l->type != LIST_PTR )
        return false;

    for( struct ListCell* c = l->head; c; c = c->next )
        if( c->data._ptr == ptr )
            return true;

    return false;
}

void
list_free(struct List* l)
{
    if( !l )
        return;

    struct ListCell* c = l->head;
    struct ListCell* next = NULL;
    while( c )
    {
        next = c->next;
        free(c);
        c = next;
    }
    free(l);
}
