#include "archive.h"

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

int
archive_name_hash(char* name)
{
    return hash_djb2(name);
}