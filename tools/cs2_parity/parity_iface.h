#ifndef PARITY_IFACE_H
#define PARITY_IFACE_H

#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2disk/dat2disk.h"

struct ParityIfaceLoad
{
    RSCacheDat2A_Component* comps;
    int comp_count;
    int* lay_x;
    int* lay_y;
    int* lay_w;
    int* lay_h;
};

int
parity_iface_load(
    struct RSCacheDat2Disk* cache,
    int iface_id,
    int root_w,
    int root_h,
    struct ParityIfaceLoad* out);

void
parity_iface_free(struct ParityIfaceLoad* load);

#endif
