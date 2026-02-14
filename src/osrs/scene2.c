#include "scene2.h"

#include <stdlib.h>
#include <string.h>

struct Scene2*
scene2_new(int size)
{
    struct Scene2* scene2 = malloc(sizeof(struct Scene2));
    memset(scene2, 0, sizeof(struct Scene2));

    scene2->element_list = (struct Scene2Element*)malloc(size * sizeof(struct Scene2Element));
    memset(scene2->element_list, 0, size * sizeof(struct Scene2Element));
    scene2->element_count = size;

    return scene2;
}

void
scene2_free(struct Scene2* scene2)
{
    // Free the elements
    free(scene2->element_list);
    free(scene2);
}