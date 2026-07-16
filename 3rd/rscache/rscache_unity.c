/*
 * RSCache unity build anchor — compile this single file to build the library.
 */
#include "rscache.h"

// clang-format off
#include "../bzip/bzip.c"
#include "../miniz/miniz.c"
#include "src/compression.c"
#include "src/rsbuffer.c"
#include "src/archive.c"
#include "src/disk.c"
#include "src/filelist.c"
#include "src/datatypes/model.c"
#include "src/datatypes/dat2_component.c"
// clang-format on
