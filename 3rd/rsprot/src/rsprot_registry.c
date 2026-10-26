/*
 * The registry's lookups (include/rsprot_registry.h), over the table
 * rsprot_packets_unity.c generates from packets/: compiled in that TU, after
 * the table.
 */
#include "../include/rsprot_registry.h"
#include "rsprot_tables_def.h"

#include <string.h>

const RsprotRegistryEntry *
rsprot_registry_find(const char *prot, RsprotRegistryDir dir)
{
    for (int i = 0; i < rsprot_registry_count; i++)
        if (rsprot_registry[i].dir == dir && strcmp(rsprot_registry[i].prot, prot) == 0)
            return &rsprot_registry[i];
    return 0;
}

RsprotCodecFn
rsprot_registry_codec(const char *prot, RsprotRegistryDir dir, RsprotRev rev)
{
    const RsprotRegistryEntry *e = rsprot_registry_find(prot, dir);
    if (!e)
        return 0;
    return rsprot_version_pick(e->ranges, *e->count, rev);
}

RsprotCodecFn
rsprot_registry_resolve(const char *prot, RsprotRegistryDir dir, RsprotRev rev)
{
    char alt[96];
    size_t n = strlen(prot);
    RsprotCodecFn fn = rsprot_registry_codec(prot, dir, rev);

    if (fn || n + 4 >= sizeof(alt))
        return fn;
    memcpy(alt, prot, n + 1);
    if (n > 3 && strcmp(prot + n - 3, "_V1") == 0) {
        alt[n - 3] = '\0';
        return rsprot_registry_codec(alt, dir, rev);
    }
    memcpy(alt + n, "_V1", 4);
    return rsprot_registry_codec(alt, dir, rev);
}

RsprotCodecFn
rsprot_registry_resolve_def(const RsprotProtDef *def, RsprotRegistryDir dir, RsprotRev rev,
                            const char **prot_out)
{
    char alt[96];
    size_t n = strlen(def->name);
    RsprotCodecFn fn;

    /* A row that names its version calls that one, or none: the suffix rule
     * would pair it with the wrong prot. */
    if (def->codec) {
        fn = rsprot_registry_codec(def->codec, dir, rev);
        if (fn && prot_out)
            *prot_out = rsprot_registry_find(def->codec, dir)->prot;
        return fn;
    }
    if ((fn = rsprot_registry_codec(def->name, dir, rev))) {
        if (prot_out)
            *prot_out = rsprot_registry_find(def->name, dir)->prot;
        return fn;
    }
    if (n + 4 >= sizeof(alt))
        return 0;
    memcpy(alt, def->name, n + 1);
    if (n > 3 && strcmp(def->name + n - 3, "_V1") == 0)
        alt[n - 3] = '\0';
    else
        memcpy(alt + n, "_V1", 4);
    if ((fn = rsprot_registry_codec(alt, dir, rev)) && prot_out)
        *prot_out = rsprot_registry_find(alt, dir)->prot;
    return fn;
}
