#include "cachepack.h"

#include "archive.h"

#include <assert.h>

int
cp_pack_archive_identifier(
    const struct LC_Pack* pack,
    int id,
    const char* fallback_name)
{
    int code = 0;
    const char* hashname;

    if( pack && lc_pack_hashcode(pack, id, &code) )
        return code;
    hashname = pack ? lc_pack_hashname(pack, id) : NULL;
    if( hashname && hashname[0] )
        return RSCache_ArchiveNameHashDat2((char*)hashname);
    assert(fallback_name && fallback_name[0]);
    return RSCache_ArchiveNameHashDat2((char*)fallback_name);
}
