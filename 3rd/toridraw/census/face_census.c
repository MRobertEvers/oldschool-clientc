#include "census/face_census.h"

#if defined(TORIDRAW_FACE_CENSUS) && TORIDRAW_FACE_CENSUS

#include <stdio.h>
#include <stdlib.h>

struct ToriDraw_FaceCensus g_toridraw_face_census;

static char const* const g_face_census_names[TORIDRAW_FACE_CENSUS_BUCKETS] = {
    "tex blend opaque  [ASM]",
    "tex flat  opaque",
    "tex blend trans",
    "tex flat  trans",
    "tex blend affine v3",
    "tex blend affine",
    "tex flat  affine v3",
    "tex flat  affine",
    "gouraud",
    "flat",
};

void
ToriDraw_FaceCensusDump(void)
{
    char const* path = getenv("TORIDRAW_FACE_CENSUS_FILE");
    FILE* f = path ? fopen(path, "w") : stderr;
    double tex_faces = 0.0;
    double tex_area = 0.0;
    double all_faces = 0.0;
    double all_area = 0.0;
    double all_area_raw = 0.0;
    int i;

    if( !f )
        f = stderr;

    for( i = 0; i < TORIDRAW_FACE_CENSUS_BUCKETS; i++ )
    {
        all_faces += g_toridraw_face_census.faces[i];
        all_area += g_toridraw_face_census.area[i];
        all_area_raw += g_toridraw_face_census.area_raw[i];
        if( i <= TORIDRAW_FACE_CENSUS_TEX_FLAT_AFFINE )
        {
            tex_faces += g_toridraw_face_census.faces[i];
            tex_area += g_toridraw_face_census.area[i];
        }
    }

    /* The two %tex columns are shares of the TEXTURED subtotal, not of
     * everything -- they exist to answer "how much of texture work does the asm
     * cover". Non-textured rows print "-" rather than 0.00%, which read as
     * "gouraud is 0% of faces" in the first dump. The %all columns are the ones
     * that compare families against each other. */
    fprintf(f, "\n=== toridraw face census ===\n");
    fprintf(f, "%-24s %14s %8s %8s %16s %8s %8s\n",
            "bucket", "faces", "%texf", "%allf", "area px", "%texa", "%alla");
    for( i = 0; i < TORIDRAW_FACE_CENSUS_BUCKETS; i++ )
    {
        double fa = g_toridraw_face_census.faces[i];
        double ar = g_toridraw_face_census.area[i];
        int textured = (i <= TORIDRAW_FACE_CENSUS_TEX_FLAT_AFFINE);
        char texf[16];
        char texa[16];

        if( fa == 0.0 )
            continue;

        if( textured && tex_faces > 0.0 )
            sprintf(texf, "%6.2f%%", 100.0 * fa / tex_faces);
        else
            sprintf(texf, "%7s", "-");
        if( textured && tex_area > 0.0 )
            sprintf(texa, "%6.2f%%", 100.0 * ar / tex_area);
        else
            sprintf(texa, "%7s", "-");

        fprintf(f, "%-24s %14.0f %8s %7.2f%% %16.0f %8s %7.2f%%\n",
                g_face_census_names[i], fa,
                texf,
                all_faces > 0.0 ? 100.0 * fa / all_faces : 0.0,
                ar,
                texa,
                all_area > 0.0 ? 100.0 * ar / all_area : 0.0);
    }
    fprintf(f, "\nfaces total          %.0f  (textured %.0f)\n",
            all_faces, tex_faces);
    fprintf(f, "area total           %.0f px  (textured %.0f px)\n",
            all_area, tex_area);
    /* Clipped is the one that means "pixels the rasteriser stored". The raw
     * total sits beside it so the gap -- area projected off the edge of the
     * viewport -- is visible rather than inferred; reporting only raw is what
     * produced a bogus 8.46x overdraw figure. */
    fprintf(f, "area CLIPPED         %.0f px   RAW %.0f px   raw/clipped %.2fx\n",
            all_area, all_area_raw,
            all_area > 0.0 ? all_area_raw / all_area : 0.0);
    fprintf(f, "asm-covered share    %.2f%% of textured area, %.2f%% of all area\n",
            tex_area > 0.0
                ? 100.0 * g_toridraw_face_census.area[
                      TORIDRAW_FACE_CENSUS_TEX_BLEND_OPAQUE] / tex_area
                : 0.0,
            all_area > 0.0
                ? 100.0 * g_toridraw_face_census.area[
                      TORIDRAW_FACE_CENSUS_TEX_BLEND_OPAQUE] / all_area
                : 0.0);
    fflush(f);
    if( path && f != stderr )
        fclose(f);
}

#endif
