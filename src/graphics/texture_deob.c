
// void
// textureTriangle(
//     int xA,
//     int xB,
//     int xC,
//     int yA,
//     int yB,
//     int yC,
//     int shadeA,
//     int shadeB,
//     int shadeC,
//     int originX,
//     int originY,
//     int originZ,
//     int txB,
//     int txC,
//     int tyB,
//     int tyC,
//     int tzB,
//     int tzC,
//     int* pixel_buffer,
//     int* texels,
//     int texture_width)
// {
//     // _Pix3D.opaque = !_Pix3D.textureHasTransparency[texture];

//     int verticalX = originX - txB;
//     int verticalY = originY - tyB;
//     int verticalZ = originZ - tzB;

//     int horizontalX = txC - originX;
//     int horizontalY = tyC - originY;
//     int horizontalZ = tzC - originZ;

//     int shift_offset = 0;
//     int zshift = 14 - shift_offset;
//     int xshift = 8 - shift_offset;
//     int yshift = 5 - shift_offset;

//     // Shift up by 14, the top 7 bits are the texture coord.
//     // 9 of the bit shift come from the (d * z) that all model vertexes are multiplied by.
//     // So really this is upshifted by 5.
//     // Since the zhat component is really
//     // U = (dudz << 5) * SCALE  + (dudx << 5 * x) + (dudy << 5 * y)
//     // Since SCALE is << 9, then the upshift is really by 5.
//     // the xshift of 8, is pre-multiplied by 8 (<< 3 and << 5).

//     // For U and V, we want the top 7 bits to represent the texture coordinate.
//     // Since we are only shifting up by 5, shifting down by 7, or masking the top 7 bits,
//     // will give the result divided by 4.
//     // Perhaps pnm coords are 4 times longer than textures?
//     // so a U of 4 is actuall u 1?

//     // Another trick used in later deobs is shifting the U value up by 18 (which is already
//     assumed
//     // to be shifted up by 7) so that the top 7 bits and anything else is truncated.

//     int u = ((horizontalX * originY) - (horizontalY * originX)) << zshift;
//     int uStride = ((horizontalY * originZ) - (horizontalZ * originY)) << (xshift);
//     int uStepVertical = ((horizontalZ * originX) - (horizontalX * originZ)) << yshift;

//     int v = ((verticalX * originY) - (verticalY * originX)) << zshift;
//     int vStride = ((verticalY * originZ) - (verticalZ * originY)) << xshift;
//     int vStepVertical = ((verticalZ * originX) - (verticalX * originZ)) << yshift;

//     int w = ((verticalY * horizontalX) - (verticalX * horizontalY)) << zshift;
//     int wStride = ((verticalZ * horizontalY) - (verticalY * horizontalZ)) << xshift;
//     int wStepVertical = ((verticalX * horizontalZ) - (verticalZ * horizontalX)) << yshift;

//     int dxAB = xB - xA;
//     int dyAB = yB - yA;
//     int dxAC = xC - xA;
//     int dyAC = yC - yA;

//     /**
//      * I'm not sure how this works, but later versions use barycentric coordinates
//      * to interpolate colors.
//      */
//     int xStepAB = 0;
//     int shadeStepAB = 0;
//     if( yB != yA )
//     {
//         xStepAB = (dxAB << 16) / dyAB;
//         shadeStepAB = ((shadeB - shadeA) << 16) / dyAB;
//     }

//     int xStepBC = 0;
//     int shadeStepBC = 0;
//     if( yC != yB )
//     {
//         xStepBC = ((xC - xB) << 16) / (yC - yB);
//         shadeStepBC = ((shadeC - shadeB) << 16) / (yC - yB);
//     }

//     int xStepAC = 0;
//     int shadeStepAC = 0;
//     if( yC != yA )
//     {
//         xStepAC = ((xA - xC) << 16) / (yA - yC);
//         shadeStepAC = ((shadeA - shadeC) << 16) / (yA - yC);
//     }

//     // this won't change any rendering, saves not wasting time "drawing" an invalid triangle
//     int triangleArea = (dxAB * dyAC) - (dyAB * dxAC);
//     if( triangleArea == 0 )
//     {
//         return;
//     }

//     if( yA <= yB && yA <= yC )
//     {
//         if( yA < SCREEN_HEIGHT )
//         {
//             if( yB > SCREEN_HEIGHT )
//             {
//                 yB = SCREEN_HEIGHT;
//             }

//             if( yC > SCREEN_HEIGHT )
//             {
//                 yC = SCREEN_HEIGHT;
//             }

//             if( yB < yC )
//             {
//                 xC = xA <<= 16;
//                 shadeC = shadeA <<= 16;
//                 if( yA < 0 )
//                 {
//                     xC -= xStepAC * yA;
//                     xA -= xStepAB * yA;
//                     shadeC -= shadeStepAC * yA;
//                     shadeA -= shadeStepAB * yA;
//                     yA = 0;
//                 }

//                 xB <<= 16;
//                 shadeB <<= 16;
//                 if( yB < 0 )
//                 {
//                     xB -= xStepBC * yB;
//                     shadeB -= shadeStepBC * yB;
//                     yB = 0;
//                 }

//                 int dy = yA - (SCREEN_HEIGHT / 2);
//                 u += uStepVertical * dy;
//                 v += vStepVertical * dy;
//                 w += wStepVertical * dy;

//                 if( (yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC) )
//                 {
//                     yC -= yB;
//                     yB -= yA;
//                     yA = yA * SCREEN_WIDTH;

//                     while( --yB >= 0 )
//                     {
//                         textureRaster(
//                             xC >> 16,
//                             xA >> 16,
//                             pixel_buffer,
//                             yA,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeC >> 8,
//                             shadeA >> 8);
//                         xC += xStepAC;
//                         xA += xStepAB;
//                         shadeC += shadeStepAC;
//                         shadeA += shadeStepAB;
//                         yA += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                     while( --yC >= 0 )
//                     {
//                         textureRaster(
//                             xC >> 16,
//                             xB >> 16,
//                             pixel_buffer,
//                             yA,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeC >> 8,
//                             shadeB >> 8);
//                         xC += xStepAC;
//                         xB += xStepBC;
//                         shadeC += shadeStepAC;
//                         shadeB += shadeStepBC;
//                         yA += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                 }
//                 else
//                 {
//                     yC -= yB;
//                     yB -= yA;
//                     yA = yA * SCREEN_WIDTH;

//                     while( --yB >= 0 )
//                     {
//                         textureRaster(
//                             xA >> 16,
//                             xC >> 16,
//                             pixel_buffer,
//                             yA,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeA >> 8,
//                             shadeC >> 8);
//                         xC += xStepAC;
//                         xA += xStepAB;
//                         shadeC += shadeStepAC;
//                         shadeA += shadeStepAB;
//                         yA += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                     while( --yC >= 0 )
//                     {
//                         textureRaster(
//                             xB >> 16,
//                             xC >> 16,
//                             pixel_buffer,
//                             yA,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeB >> 8,
//                             shadeC >> 8);
//                         xC += xStepAC;
//                         xB += xStepBC;
//                         shadeC += shadeStepAC;
//                         shadeB += shadeStepBC;
//                         yA += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                 }
//             }
//             else
//             {
//                 xB = xA <<= 16;
//                 shadeB = shadeA <<= 16;
//                 if( yA < 0 )
//                 {
//                     xB -= xStepAC * yA;
//                     xA -= xStepAB * yA;
//                     shadeB -= shadeStepAC * yA;
//                     shadeA -= shadeStepAB * yA;
//                     yA = 0;
//                 }

//                 xC <<= 16;
//                 shadeC <<= 16;
//                 if( yC < 0 )
//                 {
//                     xC -= xStepBC * yC;
//                     shadeC -= shadeStepBC * yC;
//                     yC = 0;
//                 }

//                 int dy = yA - (SCREEN_HEIGHT / 2);
//                 u += uStepVertical * dy;
//                 v += vStepVertical * dy;
//                 w += wStepVertical * dy;

//                 if( (yA == yC || xStepAC >= xStepAB) && (yA != yC || xStepBC <= xStepAB) )
//                 {
//                     yB -= yC;
//                     yC -= yA;
//                     yA = yA * SCREEN_WIDTH;

//                     while( --yC >= 0 )
//                     {
//                         textureRaster(
//                             xA >> 16,
//                             xB >> 16,
//                             pixel_buffer,
//                             yA,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeA >> 8,
//                             shadeB >> 8);
//                         xB += xStepAC;
//                         xA += xStepAB;
//                         shadeB += shadeStepAC;
//                         shadeA += shadeStepAB;
//                         yA += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                     while( --yB >= 0 )
//                     {
//                         textureRaster(
//                             xA >> 16,
//                             xC >> 16,
//                             pixel_buffer,
//                             yA,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeA >> 8,
//                             shadeC >> 8);
//                         xC += xStepBC;
//                         xA += xStepAB;
//                         shadeC += shadeStepBC;
//                         shadeA += shadeStepAB;
//                         yA += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                 }
//                 else
//                 {
//                     yB -= yC;
//                     yC -= yA;
//                     yA = yA * SCREEN_WIDTH;

//                     while( --yC >= 0 )
//                     {
//                         textureRaster(
//                             xB >> 16,
//                             xA >> 16,
//                             pixel_buffer,
//                             yA,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeB >> 8,
//                             shadeA >> 8);
//                         xB += xStepAC;
//                         xA += xStepAB;
//                         shadeB += shadeStepAC;
//                         shadeA += shadeStepAB;
//                         yA += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                     while( --yB >= 0 )
//                     {
//                         textureRaster(
//                             xC >> 16,
//                             xA >> 16,
//                             pixel_buffer,
//                             yA,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeC >> 8,
//                             shadeA >> 8);
//                         xC += xStepBC;
//                         xA += xStepAB;
//                         shadeC += shadeStepBC;
//                         shadeA += shadeStepAB;
//                         yA += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                 }
//             }
//         }
//     }
//     else if( yB <= yC )
//     {
//         if( yB < SCREEN_HEIGHT )
//         {
//             if( yC > SCREEN_HEIGHT )
//             {
//                 yC = SCREEN_HEIGHT;
//             }

//             if( yA > SCREEN_HEIGHT )
//             {
//                 yA = SCREEN_HEIGHT;
//             }

//             if( yC < yA )
//             {
//                 xA = xB <<= 16;
//                 shadeA = shadeB <<= 16;
//                 if( yB < 0 )
//                 {
//                     xA -= xStepAB * yB;
//                     xB -= xStepBC * yB;
//                     shadeA -= shadeStepAB * yB;
//                     shadeB -= shadeStepBC * yB;
//                     yB = 0;
//                 }

//                 xC <<= 16;
//                 shadeC <<= 16;
//                 if( yC < 0 )
//                 {
//                     xC -= xStepAC * yC;
//                     shadeC -= shadeStepAC * yC;
//                     yC = 0;
//                 }

//                 int dy = yB - (SCREEN_HEIGHT / 2);
//                 u += uStepVertical * dy;
//                 v += vStepVertical * dy;
//                 w += wStepVertical * dy;

//                 if( (yB != yC && xStepAB < xStepBC) || (yB == yC && xStepAB > xStepAC) )
//                 {
//                     yA -= yC;
//                     yC -= yB;
//                     yB = yB * SCREEN_WIDTH;

//                     while( --yC >= 0 )
//                     {
//                         textureRaster(
//                             xA >> 16,
//                             xB >> 16,
//                             pixel_buffer,
//                             yB,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeA >> 8,
//                             shadeB >> 8);
//                         xA += xStepAB;
//                         xB += xStepBC;
//                         shadeA += shadeStepAB;
//                         shadeB += shadeStepBC;
//                         yB += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                     while( --yA >= 0 )
//                     {
//                         textureRaster(
//                             xA >> 16,
//                             xC >> 16,
//                             pixel_buffer,
//                             yB,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeA >> 8,
//                             shadeC >> 8);
//                         xA += xStepAB;
//                         xC += xStepAC;
//                         shadeA += shadeStepAB;
//                         shadeC += shadeStepAC;
//                         yB += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                 }
//                 else
//                 {
//                     yA -= yC;
//                     yC -= yB;
//                     yB = yB * SCREEN_WIDTH;

//                     while( --yC >= 0 )
//                     {
//                         textureRaster(
//                             xB >> 16,
//                             xA >> 16,
//                             pixel_buffer,
//                             yB,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeB >> 8,
//                             shadeA >> 8);
//                         xA += xStepAB;
//                         xB += xStepBC;
//                         shadeA += shadeStepAB;
//                         shadeB += shadeStepBC;
//                         yB += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                     while( --yA >= 0 )
//                     {
//                         textureRaster(
//                             xC >> 16,
//                             xA >> 16,
//                             pixel_buffer,
//                             yB,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeC >> 8,
//                             shadeA >> 8);
//                         xA += xStepAB;
//                         xC += xStepAC;
//                         shadeA += shadeStepAB;
//                         shadeC += shadeStepAC;
//                         yB += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                 }
//             }
//             else
//             {
//                 xC = xB <<= 16;
//                 shadeC = shadeB <<= 16;
//                 if( yB < 0 )
//                 {
//                     xC -= xStepAB * yB;
//                     xB -= xStepBC * yB;
//                     shadeC -= shadeStepAB * yB;
//                     shadeB -= shadeStepBC * yB;
//                     yB = 0;
//                 }

//                 xA <<= 16;
//                 shadeA <<= 16;
//                 if( yA < 0 )
//                 {
//                     xA -= xStepAC * yA;
//                     shadeA -= shadeStepAC * yA;
//                     yA = 0;
//                 }

//                 int dy = yB - (SCREEN_HEIGHT / 2);
//                 u += uStepVertical * dy;
//                 v += vStepVertical * dy;
//                 w += wStepVertical * dy;

//                 if( xStepAB < xStepBC )
//                 {
//                     yC -= yA;
//                     yA -= yB;
//                     yB = yB * SCREEN_WIDTH;

//                     while( --yA >= 0 )
//                     {
//                         textureRaster(
//                             xC >> 16,
//                             xB >> 16,
//                             pixel_buffer,
//                             yB,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeC >> 8,
//                             shadeB >> 8);
//                         xC += xStepAB;
//                         xB += xStepBC;
//                         shadeC += shadeStepAB;
//                         shadeB += shadeStepBC;
//                         yB += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                     while( --yC >= 0 )
//                     {
//                         textureRaster(
//                             xA >> 16,
//                             xB >> 16,
//                             pixel_buffer,
//                             yB,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeA >> 8,
//                             shadeB >> 8);
//                         xA += xStepAC;
//                         xB += xStepBC;
//                         shadeA += shadeStepAC;
//                         shadeB += shadeStepBC;
//                         yB += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                 }
//                 else
//                 {
//                     yC -= yA;
//                     yA -= yB;
//                     yB = yB * SCREEN_WIDTH;

//                     while( --yA >= 0 )
//                     {
//                         textureRaster(
//                             xB >> 16,
//                             xC >> 16,
//                             pixel_buffer,
//                             yB,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeB >> 8,
//                             shadeC >> 8);
//                         xC += xStepAB;
//                         xB += xStepBC;
//                         shadeC += shadeStepAB;
//                         shadeB += shadeStepBC;
//                         yB += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                     while( --yC >= 0 )
//                     {
//                         textureRaster(
//                             xB >> 16,
//                             xA >> 16,
//                             pixel_buffer,
//                             yB,
//                             texels,
//                             0,
//                             0,
//                             u,
//                             v,
//                             w,
//                             uStride,
//                             vStride,
//                             wStride,
//                             shadeB >> 8,
//                             shadeA >> 8);
//                         xA += xStepAC;
//                         xB += xStepBC;
//                         shadeA += shadeStepAC;
//                         shadeB += shadeStepBC;
//                         yB += SCREEN_WIDTH;
//                         u += uStepVertical;
//                         v += vStepVertical;
//                         w += wStepVertical;
//                     }
//                 }
//             }
//         }
//     }
//     else if( yC < SCREEN_HEIGHT )
//     {
//         if( yA > SCREEN_HEIGHT )
//         {
//             yA = SCREEN_HEIGHT;
//         }

//         if( yB > SCREEN_HEIGHT )
//         {
//             yB = SCREEN_HEIGHT;
//         }

//         if( yA < yB )
//         {
//             xB = xC <<= 16;
//             shadeB = shadeC <<= 16;
//             if( yC < 0 )
//             {
//                 xB -= xStepBC * yC;
//                 xC -= xStepAC * yC;
//                 shadeB -= shadeStepBC * yC;
//                 shadeC -= shadeStepAC * yC;
//                 yC = 0;
//             }

//             xA <<= 16;
//             shadeA <<= 16;
//             if( yA < 0 )
//             {
//                 xA -= xStepAB * yA;
//                 shadeA -= shadeStepAB * yA;
//                 yA = 0;
//             }

//             int dy = yC - (SCREEN_HEIGHT / 2);
//             u += uStepVertical * dy;
//             v += vStepVertical * dy;
//             w += wStepVertical * dy;

//             if( xStepBC < xStepAC )
//             {
//                 yB -= yA;
//                 yA -= yC;
//                 yC = yC * SCREEN_WIDTH;

//                 while( --yA >= 0 )
//                 {
//                     textureRaster(
//                         xB >> 16,
//                         xC >> 16,
//                         pixel_buffer,
//                         yC,
//                         texels,
//                         0,
//                         0,
//                         u,
//                         v,
//                         w,
//                         uStride,
//                         vStride,
//                         wStride,
//                         shadeB >> 8,
//                         shadeC >> 8);
//                     xB += xStepBC;
//                     xC += xStepAC;
//                     shadeB += shadeStepBC;
//                     shadeC += shadeStepAC;
//                     yC += SCREEN_WIDTH;
//                     u += uStepVertical;
//                     v += vStepVertical;
//                     w += wStepVertical;
//                 }
//                 while( --yB >= 0 )
//                 {
//                     textureRaster(
//                         xB >> 16,
//                         xA >> 16,
//                         pixel_buffer,
//                         yC,
//                         texels,
//                         0,
//                         0,
//                         u,
//                         v,
//                         w,
//                         uStride,
//                         vStride,
//                         wStride,
//                         shadeB >> 8,
//                         shadeA >> 8);
//                     xB += xStepBC;
//                     xA += xStepAB;
//                     shadeB += shadeStepBC;
//                     shadeA += shadeStepAB;
//                     yC += SCREEN_WIDTH;
//                     u += uStepVertical;
//                     v += vStepVertical;
//                     w += wStepVertical;
//                 }
//             }
//             else
//             {
//                 yB -= yA;
//                 yA -= yC;
//                 yC = yC * SCREEN_WIDTH;

//                 while( --yA >= 0 )
//                 {
//                     textureRaster(
//                         xC >> 16,
//                         xB >> 16,
//                         pixel_buffer,
//                         yC,
//                         texels,
//                         0,
//                         0,
//                         u,
//                         v,
//                         w,
//                         uStride,
//                         vStride,
//                         wStride,
//                         shadeC >> 8,
//                         shadeB >> 8);
//                     xB += xStepBC;
//                     xC += xStepAC;
//                     shadeB += shadeStepBC;
//                     shadeC += shadeStepAC;
//                     yC += SCREEN_WIDTH;
//                     u += uStepVertical;
//                     v += vStepVertical;
//                     w += wStepVertical;
//                 }
//                 while( --yB >= 0 )
//                 {
//                     textureRaster(
//                         xA >> 16,
//                         xB >> 16,
//                         pixel_buffer,
//                         yC,
//                         texels,
//                         0,
//                         0,
//                         u,
//                         v,
//                         w,
//                         uStride,
//                         vStride,
//                         wStride,
//                         shadeA >> 8,
//                         shadeB >> 8);
//                     xB += xStepBC;
//                     xA += xStepAB;
//                     shadeB += shadeStepBC;
//                     shadeA += shadeStepAB;
//                     yC += SCREEN_WIDTH;
//                     u += uStepVertical;
//                     v += vStepVertical;
//                     w += wStepVertical;
//                 }
//             }
//         }
//         else
//         {
//             xA = xC <<= 16;
//             shadeA = shadeC <<= 16;
//             if( yC < 0 )
//             {
//                 xA -= xStepBC * yC;
//                 xC -= xStepAC * yC;
//                 shadeA -= shadeStepBC * yC;
//                 shadeC -= shadeStepAC * yC;
//                 yC = 0;
//             }

//             xB <<= 16;
//             shadeB <<= 16;
//             if( yB < 0 )
//             {
//                 xB -= xStepAB * yB;
//                 shadeB -= shadeStepAB * yB;
//                 yB = 0;
//             }

//             int dy = yC - (SCREEN_HEIGHT / 2);
//             u += uStepVertical * dy;
//             v += vStepVertical * dy;
//             w += wStepVertical * dy;

//             if( xStepBC < xStepAC )
//             {
//                 yA -= yB;
//                 yB -= yC;
//                 yC = yC * SCREEN_WIDTH;

//                 while( --yB >= 0 )
//                 {
//                     textureRaster(
//                         xA >> 16,
//                         xC >> 16,
//                         pixel_buffer,
//                         yC,
//                         texels,
//                         0,
//                         0,
//                         u,
//                         v,
//                         w,
//                         uStride,
//                         vStride,
//                         wStride,
//                         shadeA >> 8,
//                         shadeC >> 8);
//                     xA += xStepBC;
//                     xC += xStepAC;
//                     shadeA += shadeStepBC;
//                     shadeC += shadeStepAC;
//                     yC += SCREEN_WIDTH;
//                     u += uStepVertical;
//                     v += vStepVertical;
//                     w += wStepVertical;
//                 }
//                 while( --yA >= 0 )
//                 {
//                     textureRaster(
//                         xB >> 16,
//                         xC >> 16,
//                         pixel_buffer,
//                         yC,
//                         texels,
//                         0,
//                         0,
//                         u,
//                         v,
//                         w,
//                         uStride,
//                         vStride,
//                         wStride,
//                         shadeB >> 8,
//                         shadeC >> 8);
//                     xB += xStepAB;
//                     xC += xStepAC;
//                     shadeB += shadeStepAB;
//                     shadeC += shadeStepAC;
//                     yC += SCREEN_WIDTH;
//                     u += uStepVertical;
//                     v += vStepVertical;
//                     w += wStepVertical;
//                 }
//             }
//             else
//             {
//                 yA -= yB;
//                 yB -= yC;
//                 yC = yC * SCREEN_WIDTH;

//                 while( --yB >= 0 )
//                 {
//                     textureRaster(
//                         xC >> 16,
//                         xA >> 16,
//                         pixel_buffer,
//                         yC,
//                         texels,
//                         0,
//                         0,
//                         u,
//                         v,
//                         w,
//                         uStride,
//                         vStride,
//                         wStride,
//                         shadeC >> 8,
//                         shadeA >> 8);
//                     xA += xStepBC;
//                     xC += xStepAC;
//                     shadeA += shadeStepBC;
//                     shadeC += shadeStepAC;
//                     yC += SCREEN_WIDTH;
//                     u += uStepVertical;
//                     v += vStepVertical;
//                     w += wStepVertical;
//                 }
//                 while( --yA >= 0 )
//                 {
//                     textureRaster(
//                         xC >> 16,
//                         xB >> 16,
//                         pixel_buffer,
//                         yC,
//                         texels,
//                         0,
//                         0,
//                         u,
//                         v,
//                         w,
//                         uStride,
//                         vStride,
//                         wStride,
//                         shadeC >> 8,
//                         shadeB >> 8);
//                     xB += xStepAB;
//                     xC += xStepAC;
//                     shadeB += shadeStepAB;
//                     shadeC += shadeStepAC;
//                     yC += SCREEN_WIDTH;
//                     u += uStepVertical;
//                     v += vStepVertical;
//                     w += wStepVertical;
//                 }
//             }
//         }
//     }
// }

// void
// textureRaster(
//     int xA,
//     int xB,
//     int* dst,
//     int offset,
//     int* texels,
//     int curU,
//     int curV,
//     int u,
//     int v,
//     int w,
//     int uStride,
//     int vStride,
//     int wStride,
//     int shadeA,
//     int shadeB)
// {
//     if( xA >= xB )
//     {
//         return;
//     }

//     int opaque = 0;
//     int shadeStrides;
//     int strides;
//     // Alpha true
//     if( xB != xA )
//     {
//         shadeStrides = (shadeB - shadeA) / (xB - xA);

//         if( xB > SCREEN_WIDTH )
//         {
//             xB = SCREEN_WIDTH;
//         }

//         if( xA < 0 )
//         {
//             shadeA -= xA * shadeStrides;
//             xA = 0;
//         }

//         if( xA >= xB )
//         {
//             return;
//         }

//         strides = (xB - xA) >> 3;
//         shadeStrides <<= 12;
//         shadeA <<= 9;
//     }
//     else
//     {
//         if( xB - xA > 7 )
//         {
//             strides = (xB - xA) >> 3;
//             // shadeStrides = (shadeB - shadeA) * _Pix3D.reciprical15[strides] >> 6;
//         }
//         else
//         {
//             strides = 0;
//             shadeStrides = 0;
//         }

//         shadeA <<= 9;
//     }

//     offset += xA;

//     // if lowdetail
//     if( false )
//     {
//         int nextU = 0;
//         int nextV = 0;
//         int dx = xA - (SCREEN_WIDTH / 2);

//         u = u + (uStride >> 3) * dx;
//         v = v + (vStride >> 3) * dx;
//         w = w + (wStride >> 3) * dx;

//         int curW = w >> 12;
//         if( curW != 0 )
//         {
//             curU = u / curW;
//             curV = v / curW;
//             if( curU < 0 )
//             {
//                 curU = 0;
//             }
//             else if( curU > 0xfc0 )
//             {
//                 curU = 0xfc0;
//             }
//         }

//         u = u + uStride;
//         v = v + vStride;
//         w = w + wStride;

//         curW = w >> 12;
//         if( curW != 0 )
//         {
//             nextU = u / curW;
//             nextV = v / curW;
//             if( nextU < 0x7 )
//             {
//                 nextU = 0x7;
//             }
//             else if( nextU > 0xfc0 )
//             {
//                 nextU = 0xfc0;
//             }
//         }

//         int stepU = (nextU - curU) >> 3;
//         int stepV = (nextV - curV) >> 3;
//         curU += (shadeA >> 3) & 0xc0000;
//         int shadeShift = shadeA >> 23;

//         if( true )
//         {
//             while( strides-- > 0 )
//             {
//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU = nextU;
//                 curV = nextV;

//                 u += uStride;
//                 v += vStride;
//                 w += wStride;

//                 curW = w >> 12;
//                 if( curW != 0 )
//                 {
//                     nextU = u / curW;
//                     nextV = v / curW;
//                     if( nextU < 0x7 )
//                     {
//                         nextU = 0x7;
//                     }
//                     else if( nextU > 0xfc0 )
//                     {
//                         nextU = 0xfc0;
//                     }
//                 }

//                 stepU = (nextU - curU) >> 3;
//                 stepV = (nextV - curV) >> 3;
//                 shadeA += shadeStrides;
//                 curU += (shadeA >> 3) & 0xc0000;
//                 shadeShift = shadeA >> 23;
//             }

//             strides = xB - xA & 0x7;
//             while( strides-- > 0 )
//             {
//                 dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;
//             }
//         }
//         else
//         {
//             while( strides-- > 0 )
//             {
//                 int rgb;
//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU = nextU;
//                 curV = nextV;

//                 u += uStride;
//                 v += vStride;
//                 w += wStride;

//                 curW = w >> 12;
//                 if( curW != 0 )
//                 {
//                     nextU = u / curW;
//                     nextV = v / curW;
//                     if( nextU < 7 )
//                     {
//                         nextU = 7;
//                     }
//                     else if( nextU > 0xfc0 )
//                     {
//                         nextU = 0xfc0;
//                     }
//                 }

//                 stepU = (nextU - curU) >> 3;
//                 stepV = (nextV - curV) >> 3;
//                 shadeA += shadeStrides;
//                 curU += (shadeA >> 3) & 0xc0000;
//                 shadeShift = shadeA >> 23;
//             }

//             strides = (xB - xA) & 0x7;
//             while( strides-- > 0 )
//             {
//                 int rgb;
//                 if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }

//                 offset++;
//                 curU += stepU;
//                 curV += stepV;
//             }
//         }
//     }
//     else
//     {
//         int nextU = 0;
//         int nextV = 0;
//         int dx = xA - (SCREEN_WIDTH / 2);

//         // This should really be (dx >> 3)
//         // Since we are making dx>>3 strides (linear interpolate between every 8 pixels)
//         u = u + (uStride >> 3) * dx;
//         v = v + (vStride >> 3) * dx;
//         w = w + (wStride >> 3) * dx;

//         // The math gives u, 0-1, but we need 0 - 128. So the result should auto be upshifted by
//         7
//         // And we want the top 7 bits of each to be the texture coord.
//         // is this (7 for the texture size) + (7 the top 7 bits)
//         int curW = w >> 14;
//         if( curW != 0 )
//         {
//             curU = u / curW;
//             curV = v / curW;
//             if( curU < 0 )
//             {
//                 curU = 0;
//             }
//             else if( curU > 0x3f80 )
//             {
//                 curU = 0x3f80;
//             }
//         }

//         // It appears that the uStrides are pre-multiplied by eight (shifted up by << 8 rather
//         than
//         // << 5)
//         //
//         u = u + uStride;
//         v = v + vStride;
//         w = w + wStride;

//         curW = w >> 14;
//         if( curW != 0 )
//         {
//             nextU = u / curW;
//             nextV = v / curW;
//             if( nextU < 0x7 )
//             {
//                 nextU = 0x7;
//             }
//             // 0x3f80 top 7 bits of a 14 bit number.
//             // 16256
//             else if( nextU > 0x3f80 )
//             {
//                 nextU = 0x3f80;
//             }
//         }

//         int stepU = (nextU - curU) >> 3;
//         int stepV = (nextV - curV) >> 3;
//         curU += shadeA & 0x600000;
//         int shadeShift = shadeA >> 23;

//         if( opaque )
//         {
//             while( strides-- > 0 )
//             {
//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;

//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU = nextU;
//                 curV = nextV;

//                 u += uStride;
//                 v += vStride;
//                 w += wStride;

//                 curW = w >> 14;
//                 if( curW != 0 )
//                 {
//                     nextU = u / curW;
//                     nextV = v / curW;
//                     if( nextU < 0x7 )
//                     {
//                         nextU = 0x7;
//                     }
//                     else if( nextU > 0x3f80 )
//                     {
//                         nextU = 0x3f80;
//                     }
//                 }

//                 stepU = (nextU - curU) >> 3;
//                 stepV = (nextV - curV) >> 3;
//                 shadeA += shadeStrides;
//                 curU += shadeA & 0x600000;
//                 shadeShift = shadeA >> 23;
//             }

//             strides = xB - xA & 0x7;
//             while( strides-- > 0 )
//             {
//                 dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
//                 curU += stepU;
//                 curV += stepV;
//             }
//         }
//         else
//         {
//             while( strides-- > 0 )
//             {
//                 int rgb;
//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU += stepU;
//                 curV += stepV;

//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }
//                 offset++;
//                 curU = nextU;
//                 curV = nextV;

//                 u += uStride;
//                 v += vStride;
//                 w += wStride;

//                 curW = w >> 14;
//                 if( curW != 0 )
//                 {
//                     nextU = u / curW;
//                     nextV = v / curW;
//                     if( nextU < 0x7 )
//                     {
//                         nextU = 0x7;
//                     }
//                     else if( nextU > 0x3f80 )
//                     {
//                         nextU = 0x3f80;
//                     }
//                 }

//                 stepU = (nextU - curU) >> 3;
//                 stepV = (nextV - curV) >> 3;
//                 shadeA += shadeStrides;
//                 curU += shadeA & 0x600000;
//                 shadeShift = shadeA >> 23;
//             }

//             strides = xB - xA & 0x7;
//             while( strides-- > 0 )
//             {
//                 int rgb;
//                 if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
//                 {
//                     dst[offset] = rgb;
//                 }

//                 offset++;
//                 curU += stepU;
//                 curV += stepV;
//             }
//         }
//     }
// }
