#ifndef LIST_MACROS_H
#define LIST_MACROS_H

/* Iterate through a singly linked list.
   head = pointer to first element
   iter = loop variable
   nextfield = name of the pointer field (e.g. next)
*/
#define ll_foreach(head, iter) for( (iter) = (head); (iter) != NULL; (iter) = (iter)->next )

// clang-format off
#define ll_last(head, iter)                        \
    ({                                             \
        if( head && iter )                         \
        {                                          \
            while( iter->next != NULL )            \
                iter = iter->next;                 \
        }                                          \
    })
// clang-format on

#endif