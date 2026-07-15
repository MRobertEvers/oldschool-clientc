#ifndef LIST_H
#define LIST_H

#include <stdbool.h>
#include <stddef.h>

/*
 * PostgreSQL-style List: a singly-linked list that stores
 * either integers or generic pointers.
 */

enum ListCellType
{
    LIST_INT,
    LIST_PTR
};

struct ListCell
{
    union
    {
        void* _ptr;
        int _int;
    } data;
    struct ListCell* next;
};

struct List
{
    enum ListCellType type;
    int length;
    struct ListCell* head;
    struct ListCell* tail;
};

/* Constructors */
struct List* list_make(enum ListCellType type);
struct List* list_make1_int(int value);
struct List* list_make1_ptr(void* ptr);

/* Mutators */
struct List* list_append_int(struct List* l, int value);
struct List* list_append_ptr(struct List* l, void* ptr);

/* Accessors */
int list_length(const struct List* l);
bool list_member_int(const struct List* l, int value);
bool list_member_ptr(const struct List* l, void* ptr);

/* Destruction */
void list_free(struct List* l);

/* Returns head cell pointer or NULL */
static inline struct ListCell*
list_head(const struct List* list)
{
    return list ? list->head : NULL;
}

/* Return next list cell or NULL */
static inline struct ListCell*
lnext(const struct List* list, const struct ListCell* cell)
{
    if( !list || !cell )
        return NULL;

    return cell->next;
}

/*
 * PostgreSQL-style foreach macro.
 * `cell` must be a ListCell* declared by the caller.
 */
#define foreach(cell, list)                                                                        \
    for( (cell) = list_head(list); (cell) != NULL; (cell) = lnext(list, cell) )

#endif
