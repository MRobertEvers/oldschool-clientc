#include "render/torirs_silhouette.h"

#include <stdio.h>
#include <stdlib.h>

static int failures, checks;
#define CHECK(c, message) do { ++checks; if( !(c) ) { ++failures; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, message); } } while(0)

static struct ToriRS_SilhouetteVertex
vertex(float x, float y, float depth, float u, float v)
{
    return (struct ToriRS_SilhouetteVertex){x,y,depth,u,v,depth};
}

static void
quad(struct ToriRS_Silhouette* mask, float x0, float y0, float x1, float y1,
     float depth, int opacity, bool subject, int order, bool zbuffer,
     const struct ToriRS_SilhouetteTexture* texture)
{
    struct ToriRS_SilhouetteVertex a[3] = {
        vertex(x0,y0,depth,0,0), vertex(x1,y0,depth,1,0), vertex(x1,y1,depth,1,1)};
    struct ToriRS_SilhouetteVertex b[3] = {
        vertex(x0,y0,depth,0,0), vertex(x1,y1,depth,1,1), vertex(x0,y1,depth,0,1)};
    ToriRS_SilhouetteTriangle(mask,a,opacity,texture!=NULL,texture,subject,order,zbuffer);
    ToriRS_SilhouetteTriangle(mask,b,opacity,texture!=NULL,texture,subject,order,zbuffer);
}

static int
at(struct ToriRS_Silhouette const* mask, int x, int y)
{
    return mask->alpha[(size_t)(y-mask->y)*mask->width + x-mask->x];
}

static void
u_mesh(struct ToriRS_Silhouette* mask, int opacity)
{
    ToriRS_SilhouetteInit(mask,0,0,64,64);
    quad(mask,8,8,56,24,.01f,opacity,true,3,false,NULL);
    quad(mask,8,24,24,56,.01f,opacity,true,3,false,NULL);
    quad(mask,40,24,56,56,.01f,opacity,true,3,false,NULL);
}

static void
test_topology_and_width(void)
{
    for( int width=0; width<=2; ++width )
    {
        struct ToriRS_Silhouette mask;
        u_mesh(&mask,255);
        ToriRS_SilhouetteStyle(&mask,70,width);
        CHECK(at(&mask,16,40)==70,"the actual left leg retains the requested fill alpha");
        CHECK(at(&mask,32,40)==0,"concave notch is empty at every stroke width");
        CHECK(at(&mask,24,40)==(width?255:0),"inner silhouette contour is present only with stroke");
        CHECK(at(&mask,25,40)==(width==2?255:0),"width two reaches exactly a second pixel");
        CHECK(at(&mask,26,40)==0,"stroke does not bridge the remaining notch");
        CHECK(at(&mask,16,24)==70,"triangle and part joins do not create internal outline seams");
        ToriRS_SilhouetteFree(&mask);
    }
    {
        struct ToriRS_Silhouette mask;
        u_mesh(&mask,0);
        ToriRS_SilhouetteStyle(&mask,255,2);
        CHECK(at(&mask,16,40)==0,"fully transparent geometry has no fill");
        CHECK(at(&mask,7,40)==0,"fully transparent geometry has no contour");
        ToriRS_SilhouetteFree(&mask);
    }
}

static void
test_occlusion_and_control(void)
{
    for( int depth_test=0; depth_test<=1; ++depth_test )
    {
        struct ToriRS_Silhouette mask;
        u_mesh(&mask,255);
        ToriRS_SilhouetteStyle(&mask,70,2);
        CHECK(at(&mask,6,40)==255,"always-on-top control retains complete mesh contour");
        quad(&mask,0,30,30,48,.02f,255,false,4,depth_test,NULL);
        CHECK(at(&mask,16,40)==0,"foreground geometry removes the covered fill");
        CHECK(at(&mask,6,40)==0,"foreground geometry also removes the covered outer contour");
        CHECK(at(&mask,16,29)==70,"occlusion cut does not invent a contour across the mesh");
        CHECK(at(&mask,48,40)==70,"uncovered right leg retains its fill");
        CHECK(at(&mask,57,40)==255,"uncovered right contour remains visible");
        ToriRS_SilhouetteFree(&mask);
    }
    {
        struct ToriRS_Silhouette mask;
        u_mesh(&mask,255);
        ToriRS_SilhouetteStyle(&mask,70,2);
        quad(&mask,0,30,30,48,.005f,255,false,4,true,NULL);
        CHECK(at(&mask,16,40)==70,"depth renderer ignores a later but farther surface");
        quad(&mask,0,30,30,48,.02f,128,false,2,true,NULL);
        CHECK(at(&mask,16,40)==35,"near translucent surface attenuates fill despite earlier submission");
        CHECK(at(&mask,6,40)==127,"near translucent surface attenuates contour");
        ToriRS_SilhouetteFree(&mask);
    }
    {
        struct ToriRS_Silhouette mask;
        u_mesh(&mask,255);
        ToriRS_SilhouetteStyle(&mask,70,2);
        quad(&mask,0,30,30,48,.02f,255,false,2,false,NULL);
        CHECK(at(&mask,16,40)==70,"painter renderer respects the actual earlier draw order");
        ToriRS_SilhouetteFree(&mask);
    }
}

static void
test_alpha_and_texture_holes(void)
{
    struct ToriRS_Silhouette mask;
    uint32_t const texels[]={0,0xffffffff,0xffffffff,0xffffffff};
    struct ToriRS_SilhouetteTexture texture={texels,2,2,true,false};
    ToriRS_SilhouetteInit(&mask,10,20,24,24);
    quad(&mask,10,20,34,44,.01f,128,true,1,false,&texture);
    ToriRS_SilhouetteStyle(&mask,255,1);
    CHECK(at(&mask,14,24)==0,"color-key texture hole stays transparent");
    CHECK(at(&mask,28,24)==128,"covered textured face preserves its opacity");
    CHECK(at(&mask,28,38)==128,"shared triangle diagonal is blended only once");
    CHECK(at(&mask,21,24)==128,"hole has its own silhouette boundary");
    ToriRS_SilhouetteFree(&mask);
    {
        uint32_t const alphas[]={0x00ffffff,0x80ffffff,0xffffffff,0xffffffff};
        texture=(struct ToriRS_SilhouetteTexture){alphas,2,2,false,true};
        ToriRS_SilhouetteInit(&mask,0,0,24,24);
        quad(&mask,0,0,24,24,.01f,128,true,1,false,&texture);
        ToriRS_SilhouetteStyle(&mask,255,0);
        CHECK(at(&mask,4,4)==0,"zero-alpha texel contributes no silhouette");
        CHECK(at(&mask,18,4)==64,"face and texel alpha combine independently");
        CHECK(at(&mask,18,18)==128,"opaque texel preserves the face alpha");
        ToriRS_SilhouetteFree(&mask);
    }
}

static void
test_clip_and_large_width(void)
{
    struct ToriRS_Silhouette mask;
    ToriRS_SilhouetteInit(&mask,20,30,8,8);
    quad(&mask,-1e20f,-1e20f,1e20f,1e20f,.01f,255,true,1,false,NULL);
    ToriRS_SilhouetteStyle(&mask,70,255);
    CHECK(at(&mask,20,30)==70,"huge projected face is clipped before integer conversion");
    CHECK(at(&mask,27,37)==70,"clip contains the complete requested region");
    ToriRS_SilhouetteFree(&mask);
}

static void
test_perspective_depth(void)
{
    struct ToriRS_Silhouette mask;
    struct ToriRS_SilhouetteVertex const triangle[3]={
        {0,0,.01f,0,0,.01f},{64,0,.02f,0,0,.02f},{0,64,.01f,0,0,.01f}};
    ToriRS_SilhouetteInit(&mask,0,0,64,64);
    ToriRS_SilhouetteTriangle(&mask,triangle,255,false,NULL,true,1,true);
    ToriRS_SilhouetteStyle(&mask,70,0);
    quad(&mask,0,0,64,64,.015f,255,false,2,true,NULL);
    CHECK(at(&mask,8,8)==0,"nearer foreground hides the far side of a slanted face");
    CHECK(at(&mask,40,8)==70,"reciprocal depth keeps the near side visible; linear z would hide it");
    ToriRS_SilhouetteFree(&mask);
}

int main(void)
{
    test_topology_and_width();
    test_occlusion_and_control();
    test_alpha_and_texture_holes();
    test_clip_and_large_width();
    test_perspective_depth();
    printf("silhouette: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
