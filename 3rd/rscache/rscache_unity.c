/*
 * RSCache unity build anchor — compile this single file to build the library.
 */
#include "rscache.h"

// clang-format off
#include "../bzip/bzip.c"
#include "../miniz/miniz.c"
#include "../xteas/xteas.c"
#include "src/compression.c"
#include "src/rsbuffer.c"
#include "src/archive.c"
#include "src/reference_table.c"
#include "src/xtea_config.c"
#include "src/dat2disk.c"
#include "src/dat1disk.c"
#include "src/filelist.c"
#include "src/datatypes/mapsquares.c"
#include "src/datatypes/model.c"
#include "src/datatypes/dat2_component.c"
// clang-format on
