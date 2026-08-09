/*
 * 3rd/rscache subset for interfacex — dat2 component + filelist only.
 * main.c already includes v0 osrs/rscache/rscache_unity.h for the rest.
 */
// clang-format off
#include "../../3rd/bzip/bzip.c"
#include "../../3rd/miniz/miniz.c"
#include "../../3rd/xteas/xteas.c"
#include "../../3rd/rscache/src/compression.c"
#include "../../3rd/rscache/src/rsbuffer.c"
#include "../../3rd/rscache/src/archive.c"
#include "../../3rd/rscache/src/reference_table.c"
#include "../../3rd/rscache/src/xtea_config.c"
#include "../../3rd/rscache/src/dat2disk.c"
#include "../../3rd/rscache/src/dat1disk.c"
#include "../../3rd/rscache/src/datatypes/mapsquares.c"
#include "../../3rd/rscache/src/filelist.c"
#include "../../3rd/rscache/src/datatypes/dat2_component.c"
// clang-format on
