#include "bake_chain_format.h"
#include "toridraw.h"
#include "toridraw_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    ToriDraw_Init();struct ToriDraw_Model* m=ToriDraw_ModelNew(4,2,0);
    int16_t x[]={-200,300,400,-100},y[]={0,20,-30,10},z[]={-150,-120,250,300};
    int16_t a[]={0,2},b[]={1,3},c[]={2,0},tex[]={-1,7},coords[]={-1,0},p[]={0},q[]={1},n[]={2};
    uint16_t ca[]={1234,5678},cb[]={1300,5800},cc[]={1400,5900};uint8_t alpha[]={127,200};
    m->vertices_x=x;m->vertices_y=y;m->vertices_z=z;m->face_indices_a=a;m->face_indices_b=b;m->face_indices_c=c;
    m->face_colors_a=ca;m->face_colors_b=cb;m->face_colors_c=cc;m->face_alphas=alpha;m->face_textures=tex;
    m->face_texture_coords=coords;m->textured_p_coordinate=p;m->textured_m_coordinate=q;m->textured_n_coordinate=n;m->textured_face_count=1;
    unsigned checks=0;
    for(int pitch=0;pitch<2048;pitch+=127)for(int yaw=0;yaw<2048;yaw+=131){
        struct ToriDraw_Position pos={.x=2345,.y=-900,.z=6789,.pitch=pitch,.yaw=yaw};
        struct TRSPK_WorldPlacement placement;trspk_toridraw_placement_init(&placement,&pos);
        float xyz[12];trspk_toridraw_world_vertices(m,&placement,xyz);
        for(unsigned face=0;face<2;face++){
            struct TRSPK_ToriDrawBakeFaceVerts aa,bb;trspk_toridraw_bake_face(m,face,&placement,NULL,true,TRSPK_BAKE_COLOR_ARGB,&aa);
            trspk_toridraw_bake_face_cached(m,face,&placement,NULL,true,TRSPK_BAKE_COLOR_ARGB,xyz,&bb);
            struct BakeChainFace fa=bake_chain_face(&aa),fb=bake_chain_face(&bb);
            if(memcmp(&fa,&fb,sizeof(fa))){fprintf(stderr,"bake mismatch pitch %d yaw %d face %u\n",pitch,yaw,face);return 1;}checks++;
        }
    }
    free(m);printf("PASS: %u textured/untextured world-coordinate bake comparisons\n",checks);return 0;
}
