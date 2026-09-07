/* Correctness-only skeletal reuse fixture; no synthetic performance samples. */
#include "toridraw.h"
#include "toridraw_scene.h"
#include "toridraw_model.h"
#include "toridraw_animation.h"
#include <stdio.h>
#include <stdlib.h>
static int checks;
#define CHECK(c) do {checks++;if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void)
{
    ToriDraw_Init();
    struct ToriDraw_Model* m=ToriDraw_ModelNew(1,0,0);
    m->vertices_x=malloc(2);m->vertices_y=malloc(2);m->vertices_z=malloc(2);
    m->vertices_x[0]=10;m->vertices_y[0]=-20;m->vertices_z[0]=30;
    ToriDraw_ModelCaptureOriginalVertices(m);
    m->animaya_vertex_count=1;m->animaya_group_counts=malloc(1);m->animaya_group_counts[0]=1;
    m->animaya_groups=malloc(sizeof(void*));m->animaya_scales=malloc(sizeof(void*));
    m->animaya_groups[0]=malloc(1);m->animaya_scales[0]=malloc(1);
    m->animaya_groups[0][0]=0;m->animaya_scales[0][0]=255;
    float matrices[32]={0},other_matrix[16]={0};
    for(int f=0;f<2;f++)for(int i=0;i<4;i++)matrices[f*16+i*5]=1.0f;
    for(int i=0;i<4;i++)other_matrix[i*5]=1.0f;
    matrices[16+12]=50.0f;other_matrix[12]=70.0f;
    struct ToriDraw_SkeletalAnim a={.bone_count=1,.frame_count=2,.matrices=matrices};
    struct ToriDraw_SkeletalAnim b={.bone_count=1,.frame_count=1,.matrices=other_matrix};
    struct ToriDraw_SceneElement e={0};e.model=ToriDraw_ModelHandleOwned(m);e.is_skeletal=true;e.skeletal_animation=&a;e.posed_primary=-1;
    ToriDraw_SceneElementApplyAnimationResolved(&e,0,true,0,true);
    CHECK(m->vertices_x[0]==10 && m->vertices_y[0]==-20 && m->vertices_z[0]==30);
    CHECK(e.posed_primary==1 && e.posed_frame==0 && e.posed_track==&a);
    ToriDraw_SceneElementApplyAnimationResolved(&e,0,true,1,true);CHECK(m->vertices_x[0]==60);
    ToriDraw_SceneElementApplyAnimationResolved(&e,0,true,1,true);CHECK(m->vertices_x[0]==60);
    e.skeletal_animation=&b;
    ToriDraw_SceneElementApplyAnimationResolved(&e,0,true,0,true);CHECK(m->vertices_x[0]==80);
    CHECK(e.posed_track==&b);
    e.skeletal_animation=&a;
    ToriDraw_SceneElementApplyAnimationResolved(&e,0,true,99,true);CHECK(m->vertices_x[0]==10 && e.posed_frame==0);
    m->vertices_x[0]=12345;
    ToriDraw_SceneElementApplyAnimationResolved(&e,0,true,0,false);CHECK(m->vertices_x[0]==10);
    ToriDraw_ModelFree(m);
    printf("PASS: %d skeletal pose-reuse checks\n",checks);return 0;
}
