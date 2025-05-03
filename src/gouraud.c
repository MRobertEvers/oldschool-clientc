
void
gouraud_triangle(
    int* pixels,
    int pa_x,
    int pb_x,
    int pc_x,
    int pa_y,
    int pb_y,
    int pc_y,
    int color_a,
    int color_b,
    int color_c)
{
    int dx_ab = pb_x - pa_x;
    int dy_ab = pb_y - pa_y;
    int dx_ac = pc_x - pa_x;
    int dy_ac = pc_y - pa_y;

    int x_step_ab = 0;
    // int color_step_ab = 0;

    // Dot product of v_ab, v_ac
    int triangle_area = (dx_ab * dy_ac) - (dy_ab * dx_ac);
    if( triangle_area == 0 )
        return;
}

// void gouraudTriangle(int xA, int xB, int xC, int yA, int yB, int yC, int colorA, int colorB, int
// colorC) {
//     int dxAB = xB - xA;
//     int dyAB = yB - yA;
//     int dxAC = xC - xA;
//     int dyAC = yC - yA;

//     int xStepAB = 0;
//     int colorStepAB = 0;
//     if (yB != yA) {
//         xStepAB = (dxAB << 16) / dyAB;
//         colorStepAB = ((colorB - colorA) << 15) / dyAB;
//     }

//     int xStepBC = 0;
//     int colorStepBC = 0;
//     if (yC != yB) {
//         xStepBC = ((xC - xB) << 16) / (yC - yB);
//         colorStepBC = ((colorC - colorB) << 15) / (yC - yB);
//     }

//     int xStepAC = 0;
//     int colorStepAC = 0;
//     if (yC != yA) {
//         xStepAC = ((xA - xC) << 16) / (yA - yC);
//         colorStepAC = ((colorA - colorC) << 15) / (yA - yC);
//     }

//     // this won't change any rendering, saves not wasting time "drawing" an invalid triangle
//     int triangleArea = (dxAB * dyAC) - (dyAB * dxAC);
//     if (triangleArea == 0) {
//         return;
//     }

//     if (yA <= yB && yA <= yC) {
//         if (yA < _Pix2D.bottom) {
//             if (yB > _Pix2D.bottom) {
//                 yB = _Pix2D.bottom;
//             }

//             if (yC > _Pix2D.bottom) {
//                 yC = _Pix2D.bottom;
//             }

//             if (yB < yC) {
//                 xC = xA <<= 16;
//                 colorC = colorA <<= 15;
//                 if (yA < 0) {
//                     xC -= xStepAC * yA;
//                     xA -= xStepAB * yA;
//                     colorC -= colorStepAC * yA;
//                     colorA -= colorStepAB * yA;
//                     yA = 0;
//                 }

//                 xB <<= 16;
//                 colorB <<= 15;
//                 if (yB < 0) {
//                     xB -= xStepBC * yB;
//                     colorB -= colorStepBC * yB;
//                     yB = 0;
//                 }

//                 if ((yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC)) {
//                     yC -= yB;
//                     yB -= yA;
//                     yA = _Pix3D.line_offset[yA];

//                     while (--yB >= 0) {
//                         gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7,
//                         _Pix2D.pixels, yA, 0); xC += xStepAC; xA += xStepAB; colorC +=
//                         colorStepAC; colorA += colorStepAB; yA += _Pix2D.width;
//                     }
//                     while (--yC >= 0) {
//                         gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7,
//                         _Pix2D.pixels, yA, 0); xC += xStepAC; xB += xStepBC; colorC +=
//                         colorStepAC; colorB += colorStepBC; yA += _Pix2D.width;
//                     }
//                 } else {
//                     yC -= yB;
//                     yB -= yA;
//                     yA = _Pix3D.line_offset[yA];

//                     while (--yB >= 0) {
//                         gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7,
//                         _Pix2D.pixels, yA, 0); xC += xStepAC; xA += xStepAB; colorC +=
//                         colorStepAC; colorA += colorStepAB; yA += _Pix2D.width;
//                     }
//                     while (--yC >= 0) {
//                         gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7,
//                         _Pix2D.pixels, yA, 0); xC += xStepAC; xB += xStepBC; colorC +=
//                         colorStepAC; colorB += colorStepBC; yA += _Pix2D.width;
//                     }
//                 }
//             } else {
//                 xB = xA <<= 16;
//                 colorB = colorA <<= 15;
//                 if (yA < 0) {
//                     xB -= xStepAC * yA;
//                     xA -= xStepAB * yA;
//                     colorB -= colorStepAC * yA;
//                     colorA -= colorStepAB * yA;
//                     yA = 0;
//                 }

//                 xC <<= 16;
//                 colorC <<= 15;
//                 if (yC < 0) {
//                     xC -= xStepBC * yC;
//                     colorC -= colorStepBC * yC;
//                     yC = 0;
//                 }

//                 if ((yA != yC && xStepAC < xStepAB) || (yA == yC && xStepBC > xStepAB)) {
//                     yB -= yC;
//                     yC -= yA;
//                     yA = _Pix3D.line_offset[yA];

//                     while (--yC >= 0) {
//                         gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7,
//                         _Pix2D.pixels, yA, 0); xB += xStepAC; xA += xStepAB; colorB +=
//                         colorStepAC; colorA += colorStepAB; yA += _Pix2D.width;
//                     }
//                     while (--yB >= 0) {
//                         gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7,
//                         _Pix2D.pixels, yA, 0); xC += xStepBC; xA += xStepAB; colorC +=
//                         colorStepBC; colorA += colorStepAB; yA += _Pix2D.width;
//                     }
//                 } else {
//                     yB -= yC;
//                     yC -= yA;
//                     yA = _Pix3D.line_offset[yA];

//                     while (--yC >= 0) {
//                         gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7,
//                         _Pix2D.pixels, yA, 0); xB += xStepAC; xA += xStepAB; colorB +=
//                         colorStepAC; colorA += colorStepAB; yA += _Pix2D.width;
//                     }
//                     while (--yB >= 0) {
//                         gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7,
//                         _Pix2D.pixels, yA, 0); xC += xStepBC; xA += xStepAB; colorC +=
//                         colorStepBC; colorA += colorStepAB; yA += _Pix2D.width;
//                     }
//                 }
//             }
//         }
//     } else if (yB <= yC) {
//         if (yB < _Pix2D.bottom) {
//             if (yC > _Pix2D.bottom) {
//                 yC = _Pix2D.bottom;
//             }

//             if (yA > _Pix2D.bottom) {
//                 yA = _Pix2D.bottom;
//             }

//             if (yC < yA) {
//                 xA = xB <<= 16;
//                 colorA = colorB <<= 15;
//                 if (yB < 0) {
//                     xA -= xStepAB * yB;
//                     xB -= xStepBC * yB;
//                     colorA -= colorStepAB * yB;
//                     colorB -= colorStepBC * yB;
//                     yB = 0;
//                 }

//                 xC <<= 16;
//                 colorC <<= 15;
//                 if (yC < 0) {
//                     xC -= xStepAC * yC;
//                     colorC -= colorStepAC * yC;
//                     yC = 0;
//                 }

//                 if ((yB != yC && xStepAB < xStepBC) || (yB == yC && xStepAB > xStepAC)) {
//                     yA -= yC;
//                     yC -= yB;
//                     yB = _Pix3D.line_offset[yB];

//                     while (--yC >= 0) {
//                         gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7,
//                         _Pix2D.pixels, yB, 0); xA += xStepAB; xB += xStepBC; colorA +=
//                         colorStepAB; colorB += colorStepBC; yB += _Pix2D.width;
//                     }
//                     while (--yA >= 0) {
//                         gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7,
//                         _Pix2D.pixels, yB, 0); xA += xStepAB; xC += xStepAC; colorA +=
//                         colorStepAB; colorC += colorStepAC; yB += _Pix2D.width;
//                     }
//                 } else {
//                     yA -= yC;
//                     yC -= yB;
//                     yB = _Pix3D.line_offset[yB];

//                     while (--yC >= 0) {
//                         gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7,
//                         _Pix2D.pixels, yB, 0); xA += xStepAB; xB += xStepBC; colorA +=
//                         colorStepAB; colorB += colorStepBC; yB += _Pix2D.width;
//                     }
//                     while (--yA >= 0) {
//                         gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7,
//                         _Pix2D.pixels, yB, 0); xA += xStepAB; xC += xStepAC; colorA +=
//                         colorStepAB; colorC += colorStepAC; yB += _Pix2D.width;
//                     }
//                 }
//             } else {
//                 xC = xB <<= 16;
//                 colorC = colorB <<= 15;
//                 if (yB < 0) {
//                     xC -= xStepAB * yB;
//                     xB -= xStepBC * yB;
//                     colorC -= colorStepAB * yB;
//                     colorB -= colorStepBC * yB;
//                     yB = 0;
//                 }

//                 xA <<= 16;
//                 colorA <<= 15;
//                 if (yA < 0) {
//                     xA -= xStepAC * yA;
//                     colorA -= colorStepAC * yA;
//                     yA = 0;
//                 }

//                 if (xStepAB < xStepBC) {
//                     yC -= yA;
//                     yA -= yB;
//                     yB = _Pix3D.line_offset[yB];

//                     while (--yA >= 0) {
//                         gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7,
//                         _Pix2D.pixels, yB, 0); xC += xStepAB; xB += xStepBC; colorC +=
//                         colorStepAB; colorB += colorStepBC; yB += _Pix2D.width;
//                     }
//                     while (--yC >= 0) {
//                         gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7,
//                         _Pix2D.pixels, yB, 0); xA += xStepAC; xB += xStepBC; colorA +=
//                         colorStepAC; colorB += colorStepBC; yB += _Pix2D.width;
//                     }
//                 } else {
//                     yC -= yA;
//                     yA -= yB;
//                     yB = _Pix3D.line_offset[yB];

//                     while (--yA >= 0) {
//                         gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7,
//                         _Pix2D.pixels, yB, 0); xC += xStepAB; xB += xStepBC; colorC +=
//                         colorStepAB; colorB += colorStepBC; yB += _Pix2D.width;
//                     }
//                     while (--yC >= 0) {
//                         gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7,
//                         _Pix2D.pixels, yB, 0); xA += xStepAC; xB += xStepBC; colorA +=
//                         colorStepAC; colorB += colorStepBC; yB += _Pix2D.width;
//                     }
//                 }
//             }
//         }
//     } else if (yC < _Pix2D.bottom) {
//         if (yA > _Pix2D.bottom) {
//             yA = _Pix2D.bottom;
//         }

//         if (yB > _Pix2D.bottom) {
//             yB = _Pix2D.bottom;
//         }

//         if (yA < yB) {
//             xB = xC <<= 16;
//             colorB = colorC <<= 15;
//             if (yC < 0) {
//                 xB -= xStepBC * yC;
//                 xC -= xStepAC * yC;
//                 colorB -= colorStepBC * yC;
//                 colorC -= colorStepAC * yC;
//                 yC = 0;
//             }

//             xA <<= 16;
//             colorA <<= 15;
//             if (yA < 0) {
//                 xA -= xStepAB * yA;
//                 colorA -= colorStepAB * yA;
//                 yA = 0;
//             }

//             if (xStepBC < xStepAC) {
//                 yB -= yA;
//                 yA -= yC;
//                 yC = _Pix3D.line_offset[yC];

//                 while (--yA >= 0) {
//                     gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, _Pix2D.pixels,
//                     yC, 0); xB += xStepBC; xC += xStepAC; colorB += colorStepBC; colorC +=
//                     colorStepAC; yC += _Pix2D.width;
//                 }
//                 while (--yB >= 0) {
//                     gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, _Pix2D.pixels,
//                     yC, 0); xB += xStepBC; xA += xStepAB; colorB += colorStepBC; colorA +=
//                     colorStepAB; yC += _Pix2D.width;
//                 }
//             } else {
//                 yB -= yA;
//                 yA -= yC;
//                 yC = _Pix3D.line_offset[yC];

//                 while (--yA >= 0) {
//                     gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, _Pix2D.pixels,
//                     yC, 0); xB += xStepBC; xC += xStepAC; colorB += colorStepBC; colorC +=
//                     colorStepAC; yC += _Pix2D.width;
//                 }
//                 while (--yB >= 0) {
//                     gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, _Pix2D.pixels,
//                     yC, 0); xB += xStepBC; xA += xStepAB; colorB += colorStepBC; colorA +=
//                     colorStepAB; yC += _Pix2D.width;
//                 }
//             }
//         } else {
//             xA = xC <<= 16;
//             colorA = colorC <<= 15;
//             if (yC < 0) {
//                 xA -= xStepBC * yC;
//                 xC -= xStepAC * yC;
//                 colorA -= colorStepBC * yC;
//                 colorC -= colorStepAC * yC;
//                 yC = 0;
//             }

//             xB <<= 16;
//             colorB <<= 15;
//             if (yB < 0) {
//                 xB -= xStepAB * yB;
//                 colorB -= colorStepAB * yB;
//                 yB = 0;
//             }

//             if (xStepBC < xStepAC) {
//                 yA -= yB;
//                 yB -= yC;
//                 yC = _Pix3D.line_offset[yC];

//                 while (--yB >= 0) {
//                     gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, _Pix2D.pixels,
//                     yC, 0); xA += xStepBC; xC += xStepAC; colorA += colorStepBC; colorC +=
//                     colorStepAC; yC += _Pix2D.width;
//                 }
//                 while (--yA >= 0) {
//                     gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, _Pix2D.pixels,
//                     yC, 0); xB += xStepAB; xC += xStepAC; colorB += colorStepAB; colorC +=
//                     colorStepAC; yC += _Pix2D.width;
//                 }
//             } else {
//                 yA -= yB;
//                 yB -= yC;
//                 yC = _Pix3D.line_offset[yC];

//                 while (--yB >= 0) {
//                     gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, _Pix2D.pixels,
//                     yC, 0); xA += xStepBC; xC += xStepAC; colorA += colorStepBC; colorC +=
//                     colorStepAC; yC += _Pix2D.width;
//                 }
//                 while (--yA >= 0) {
//                     gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, _Pix2D.pixels,
//                     yC, 0); xB += xStepAB; xC += xStepAC; colorB += colorStepAB; colorC +=
//                     colorStepAC; yC += _Pix2D.width;
//                 }
//             }
//         }
//     }
// }

// void gouraudRaster(int x0, int x1, int color0, int color1, int *dst, int offset, int length) {
//     int rgb;

//     if (_Pix3D.jagged) {
//         int colorStep;

//         if (_Pix3D.clipX) {
//             if (x1 - x0 > 3) {
//                 colorStep = (color1 - color0) / (x1 - x0);
//             } else {
//                 colorStep = 0;
//             }

//             if (x1 > _Pix2D.bound_x) {
//                 x1 = _Pix2D.bound_x;
//             }

//             if (x0 < 0) {
//                 color0 -= x0 * colorStep;
//                 x0 = 0;
//             }

//             if (x0 >= x1) {
//                 return;
//             }

//             offset += x0;
//             length = (x1 - x0) >> 2;
//             colorStep <<= 2;
//         } else if (x0 < x1) {
//             offset += x0;
//             length = (x1 - x0) >> 2;

//             if (length > 0) {
//                 colorStep = (color1 - color0) * _Pix3D.reciprical15[length] >> 15;
//             } else {
//                 colorStep = 0;
//             }
//         } else {
//             return;
//         }

//         if (_Pix3D.alpha == 0) {
//             while (--length >= 0) {
//                 rgb = _Pix3D.palette[color0 >> 8];
//                 color0 += colorStep;

//                 dst[offset++] = rgb;
//                 dst[offset++] = rgb;
//                 dst[offset++] = rgb;
//                 dst[offset++] = rgb;
//             }

//             length = (x1 - x0) & 0x3;
//             if (length > 0) {
//                 rgb = _Pix3D.palette[color0 >> 8];

//                 while (--length >= 0) {
//                     dst[offset++] = rgb;
//                 }
//             }
//         } else {
//             int alpha = _Pix3D.alpha;
//             int invAlpha = 256 - _Pix3D.alpha;

//             while (--length >= 0) {
//                 rgb = _Pix3D.palette[color0 >> 8];
//                 color0 += colorStep;

//                 rgb = ((((rgb & 0xff00ff) * invAlpha) >> 8) & 0xff00ff) + ((((rgb & 0xff00) *
//                 invAlpha) >> 8) & 0xff00); dst[offset] = rgb + ((((dst[offset] & 0xff00ff) *
//                 alpha) >> 8) & 0xff00ff) + ((((dst[offset] & 0xff00) * alpha) >> 8) & 0xff00);
//                 offset++;
//                 dst[offset] = rgb + ((((dst[offset] & 0xff00ff) * alpha) >> 8) & 0xff00ff) +
//                 ((((dst[offset] & 0xff00) * alpha) >> 8) & 0xff00); offset++; dst[offset] = rgb +
//                 ((((dst[offset] & 0xff00ff) * alpha) >> 8) & 0xff00ff) + ((((dst[offset] &
//                 0xff00) * alpha) >> 8) & 0xff00); offset++; dst[offset] = rgb + ((((dst[offset] &
//                 0xff00ff) * alpha) >> 8) & 0xff00ff) + ((((dst[offset] & 0xff00) * alpha) >> 8) &
//                 0xff00); offset++;
//             }

//             length = (x1 - x0) & 0x3;
//             if (length > 0) {
//                 rgb = _Pix3D.palette[color0 >> 8];
//                 rgb = ((((rgb & 0xff00ff) * invAlpha) >> 8) & 0xff00ff) + ((((rgb & 0xff00) *
//                 invAlpha) >> 8) & 0xff00);

//                 while (--length >= 0) {
//                     dst[offset] = rgb + ((((dst[offset] & 0xff00ff) * alpha) >> 8) & 0xff00ff) +
//                     ((((dst[offset] & 0xff00) * alpha) >> 8) & 0xff00); offset++;
//                 }
//             }
//         }
//     } else if (x0 < x1) {
//         int colorStep = (color1 - color0) / (x1 - x0);

//         if (_Pix3D.clipX) {
//             if (x1 > _Pix2D.bound_x) {
//                 x1 = _Pix2D.bound_x;
//             }

//             if (x0 < 0) {
//                 color0 -= x0 * colorStep;
//                 x0 = 0;
//             }

//             if (x0 >= x1) {
//                 return;
//             }
//         }

//         offset += x0;
//         length = x1 - x0;

//         if (_Pix3D.alpha == 0) {
//             while (--length >= 0) {
//                 dst[offset++] = _Pix3D.palette[color0 >> 8];
//                 color0 += colorStep;
//             }
//         } else {
//             int alpha = _Pix3D.alpha;
//             int invAlpha = 256 - _Pix3D.alpha;

//             while (--length >= 0) {
//                 rgb = _Pix3D.palette[color0 >> 8];
//                 color0 += colorStep;

//                 rgb = ((((rgb & 0xff00ff) * invAlpha) >> 8) & 0xff00ff) + ((((rgb & 0xff00) *
//                 invAlpha) >> 8) & 0xff00); dst[offset] = rgb + ((((dst[offset] & 0xff00ff) *
//                 alpha) >> 8) & 0xff00ff) + ((((dst[offset] & 0xff00) * alpha) >> 8) & 0xff00);
//                 offset++;
//             }
//         }
//     }
// }