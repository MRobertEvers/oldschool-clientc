#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stdbool.h>

/**
 * The .dat2 file is a concatenation of archives.
 * An archive may be a single data blob, or contain multiple files.
 *
 * On disk, the archives may be compressed.
 */
struct Dat2Archive
{
    char* data;
    int data_size;
    int archive_id;
};

#endif