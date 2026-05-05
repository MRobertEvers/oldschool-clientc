#ifndef CLIENTPROT_REV245_2_H
#define CLIENTPROT_REV245_2_H

#include "osrs/core/clientprot_core.h"
#include "osrs/rscache/rsbuf.h"

struct GGame;

/** Rev-245_2 outbound packet writer.
 * Called by revision_clientprot_emit(); one switch per ClientProtOpKind.
 * @return 0 on success, -1 on unknown kind. */
int
clientprot_rev245_2_emit(
    struct RSBuffer* b,
    struct GGame*    g,
    enum ClientProtOpKind kind,
    void*            args);

#endif /* CLIENTPROT_REV245_2_H */
