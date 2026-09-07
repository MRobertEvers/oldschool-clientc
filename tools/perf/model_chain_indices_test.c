#include "platform/platform_renderer_gles2_indices.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const unsigned counts[]={0,1,2,3,7,8,9,15,16,17,31,32,33,255,256,257,1024};
    const unsigned limits[]={0,1,2,17,21845};
    int faces[1024];
    uint16_t scalar[3074], vector[3074];
    unsigned cases=0;
    gles2_painter_write_indices(NULL,0,0,NULL,0,true);
    for( unsigned li=0;li<sizeof(limits)/sizeof(*limits);li++ )
    for( unsigned ci=0;ci<sizeof(counts)/sizeof(*counts);ci++ )
    for( unsigned shape=0;shape<7;shape++ )
    {
        unsigned count=counts[ci], limit=limits[li], address=limit?65536-limit*3:65535;
        for( unsigned i=0;i<count;i++ )
        {
            int values[]={0,(int)limit-1,(int)limit,-1,INT_MAX,INT_MIN,(int)(i%19)};
            faces[i]=values[(i+shape)%7];
        }
        memset(scalar,0xa5,sizeof(scalar)); memset(vector,0xa5,sizeof(vector));
        gles2_painter_write_indices(scalar+1,address,limit,faces,count,false);
        gles2_painter_write_indices(vector+1,address,limit,faces,count,true);
        if( memcmp(scalar,vector,sizeof(vector)) ) return 1;
        for( unsigned i=0;i<count;i++ )
        {
            unsigned valid=(unsigned)faces[i]<limit;
            unsigned base=address+(valid?(unsigned)faces[i]*3:0);
            for( unsigned k=0;k<3;k++ )
                if( vector[1+i*3+k]!=(uint16_t)(base+(valid?k:0)) ) return 2;
        }
        if( vector[0]!=0xa5a5 || vector[count*3+1]!=0xa5a5 ) return 3;
        cases++;
    }
    printf("PASS: %u index-packing cases, scalar and SIMD, tails and invalid faces\n",cases);
    return 0;
}
