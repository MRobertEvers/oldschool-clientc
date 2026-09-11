#ifndef TORIRS_MINIMAP_MARK_H
#define TORIRS_MINIMAP_MARK_H
#include "ui/uitree_host.h"
#include "toridraw_sprite.h"
#include <assert.h>
#include <stdint.h>

/* Projected tile coverage is tiny (four map pixels per world tile). Scan it
 * into ordinary spans so every renderer uses the same polygon and native mask.
 * Coordinates in the mark are relative to the minimap centre. */
/* Relative map-pixel SW corner and the normal Q16 camera rotation. Keep a
 * four-pixel tile footprint while the player moves within their current tile. */
static inline bool ToriRS_MinimapTileProject(struct UITreeMinimapDot* mark,
    int64_t dx,int64_t dz,int sine,int cosine)
{
    assert(mark);
    if( dx < -84 || dx > 84 || dz < -84 || dz > 84 ||
        (dx+2)*(dx+2)+(dz+2)*(dz+2)>6400 ) return false;
    int const corner_x[4]={0,4,4,0},corner_z[4]={0,0,4,4};
    mark->kind=1;
    for( int i=0;i<4;++i )
    {
        int64_t const x=dx+corner_x[i],z=dz+corner_z[i];
        mark->tile_x[i]=(int)((z*sine+x*cosine)>>16);
        mark->tile_y[i]=-(int)((z*cosine-x*sine)>>16);
    }
    return true;
}

struct ToriRS_MinimapMarkScan
{
    int x, y, left, right, bottom, centre_x, centre_y, box_x, box_y;
    struct ToriDraw_Sprite const* mask;
    int keep_opaque;
};
static inline bool ToriRS_MinimapMarkInside(struct UITreeMinimapDot const* mark,int x,int y)
{
    assert(mark);
    bool positive=false,negative=false;
    for( int i=0;i<4;++i )
    {
        int const j=(i+1)%4;
        int64_t const cross=(int64_t)(mark->tile_x[j]-mark->tile_x[i])*(2*y+1-2*mark->tile_y[i])-
            (int64_t)(mark->tile_y[j]-mark->tile_y[i])*(2*x+1-2*mark->tile_x[i]);
        positive |= cross>0; negative |= cross<0;
    }
    return (positive || negative) && !(positive && negative);
}
static inline uint32_t ToriRS_MinimapMarkPixel(struct ToriRS_MinimapMarkScan const* scan,
    struct UITreeMinimapDot const* mark,int x,int y)
{
    assert(scan);
    assert(mark);
    if( scan->mask )
    {
        int const mx=x-scan->box_x-scan->mask->crop_x,my=y-scan->box_y-scan->mask->crop_y;
        bool const opaque=mx>=0 && my>=0 && mx<scan->mask->width && my<scan->mask->height &&
            scan->mask->pixels_argb[my*scan->mask->width+mx]!=0;
        if( scan->keep_opaque ? !opaque : opaque ) return 0;
    }
    x-=scan->centre_x;y-=scan->centre_y;
    if( !ToriRS_MinimapMarkInside(mark,x,y) ) return 0;
    int const w=mark->tile_outline_width;
    if( w>0 && (!ToriRS_MinimapMarkInside(mark,x-w,y) || !ToriRS_MinimapMarkInside(mark,x+w,y) ||
        !ToriRS_MinimapMarkInside(mark,x,y-w) || !ToriRS_MinimapMarkInside(mark,x,y+w)) )
        return UINT32_C(0xff000000)|(mark->color&0xffffffu);
    return mark->tile_alpha>0 ? ((uint32_t)mark->tile_alpha<<24)|(mark->tile_fill&0xffffffu) : 0;
}
static inline void ToriRS_MinimapMarkBegin(struct ToriRS_MinimapMarkScan* scan,
    struct UITreeMinimapDot const* mark,int centre_x,int centre_y,
    int left,int top,int right,int bottom,int box_x,int box_y,
    struct ToriDraw_Sprite const* mask,int keep_opaque)
{
    assert(scan);
    assert(mark);
    if( mask ) { assert(mask->pixels_argb); }
    int min_x=mark->tile_x[0],max_x=min_x,min_y=mark->tile_y[0],max_y=min_y;
    for( int i=1;i<4;++i )
    {
        if( mark->tile_x[i]<min_x ) min_x=mark->tile_x[i];
        if( mark->tile_x[i]>max_x ) max_x=mark->tile_x[i];
        if( mark->tile_y[i]<min_y ) min_y=mark->tile_y[i];
        if( mark->tile_y[i]>max_y ) max_y=mark->tile_y[i];
    }
    min_x+=centre_x;max_x+=centre_x;min_y+=centre_y;max_y+=centre_y;
    *scan=(struct ToriRS_MinimapMarkScan){
        .left=min_x>left?min_x:left,.right=max_x<right?max_x:right,
        .y=min_y>top?min_y:top,.bottom=max_y<bottom?max_y:bottom,
        .centre_x=centre_x,.centre_y=centre_y,.box_x=box_x,.box_y=box_y,
        .mask=mask,.keep_opaque=keep_opaque};
    scan->x=scan->left;
}
static inline bool ToriRS_MinimapMarkNext(struct ToriRS_MinimapMarkScan* scan,
    struct UITreeMinimapDot const* mark,int* x,int* y,int* width,uint32_t* color)
{
    assert(scan);assert(mark);assert(x);assert(y);assert(width);assert(color);
    while( scan->y<scan->bottom )
    {
        while( scan->x<scan->right )
        {
            uint32_t const pixel=ToriRS_MinimapMarkPixel(scan,mark,scan->x,scan->y);
            int const begin=scan->x++;
            if( !pixel ) continue;
            while( scan->x<scan->right && ToriRS_MinimapMarkPixel(scan,mark,scan->x,scan->y)==pixel ) ++scan->x;
            *x=begin;*y=scan->y;*width=scan->x-begin;*color=pixel;
            return true;
        }
        ++scan->y;scan->x=scan->left;
    }
    return false;
}
#endif
