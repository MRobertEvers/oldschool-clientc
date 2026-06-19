#include "rscache_shared_archive.h"

#include "rscache_shared_string_utils.h"

#include <stdlib.h>

//   static hashDjb2(str: string) {
//         let hash = 0;
//         if (str.length === 0) {
//             return hash;
//         }
//         let char: number;
//         for (let i = 0; i < str.length; i++) {
//             char = str.charCodeAt(i);
//             hash = (hash << 5) - hash + char;
//             hash = hash & hash; // Convert to 32bit integer
//         }
//         return hash;
//     }

static int
hash_djb2(char* name)
{
    int hash = 0;
    for( int i = 0; name[i] != '\0'; i++ )
    {
        hash = (hash << 5) - hash + name[i];
        hash = hash & hash; // Convert to 32bit integer
    }
    return hash;
}

static int
hash_rolling_polynomial_uppercase(const char* name)
{
    char c = 0;
    int hash = 0;
    for( int i = 0; name[i] != '\0'; i++ )
    {
        c = name[i];
        RSCacheShared_StrAsciiToupper(&c, 1);

        hash = (hash * 61 + c - 32) | 0;
    }
    return hash;
}

int
RSCacheShared_ArchiveNameHashDat2(char* name)
{
    return hash_djb2(name);
}

int
RSCacheShared_ArchiveNameHashDat(const char* name)
{
    return hash_rolling_polynomial_uppercase(name);
}