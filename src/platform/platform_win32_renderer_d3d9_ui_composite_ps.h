#ifndef SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_UI_COMPOSITE_PS_H
#define SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_UI_COMPOSITE_PS_H

/**
 * The interface layer's bicubic composite, as ps_2_0 bytecode.
 *
 * Include after <d3d9.h>: every token is spelled with d3d9types.h's own opcode,
 * register-type and mask macros, so no number here is a guess about the format.
 *
 * Neither Windows lane may depend on D3DX or a shader compiler (see the win32
 * block in platform.mk), so this is assembled by hand. It is the D3D9 twin of
 * trspk_opengl3_ui_composite_fragment_shader's u_filter == 2 branch: Catmull-Rom
 * over the 4x4 texel centres around the sample, then the result clamped back
 * into premultiplied range (alpha in [0,1], colour <= alpha).
 *
 * Sampler 0 is the layer texture, POINT filtered and CLAMP addressed. CLAMP is
 * what clamps an out-of-range texel index to the edge texel, the GLSL's
 * clamp(base + offset, 0, size - 1). t0 is the layer uv, 0..1 over the quad.
 *
 * Constants, set per draw (w, h = the layer size in texels):
 *   c0      (w, h, 0, 0)
 *   c1      (-1/w, -1/h, 0, 0)
 *   c2      (1/w, 0, 0, 0)                 one texel right
 *   c3..c6  (-1/w, (j-1)/h, 0, 0)          row j's first texel from the base
 *   c7      (-0.5, -2.5, 1.5, 1.0)
 *   c8      (0.5, 0.0, 0.0, 0.0)
 *
 * The program, in assembly (54 arithmetic + 16 texld; ps_2_0 allows 64 + 32,
 * 12 temporaries, and only replicate swizzles, which is all this uses):
 *
 *   dcl t0
 *   dcl_2d s0
 *   mul r2, t0, c0              ; uv * size
 *   add r2, r2, c7.xxxx         ; p = uv * size - 0.5
 *   frc r1, r2                  ; t = fract(p)
 *   mad r0, r1, c1, t0          ; (floor(p) + 0.5) / size: base texel centre
 *   mul r2, r1, r1              ; t^2
 *   mul r3, r2, r1              ; t^3
 *   mad r4, r1, c7.xxxx, r2
 *   mad r4, r3, c7.xxxx, r4     ; w0 = -0.5t^3 + t^2 - 0.5t
 *   mad r5, r2, c7.yyyy, c7.wwww
 *   mad r5, r3, c7.zzzz, r5     ; w1 = 1.5t^3 - 2.5t^2 + 1
 *   sub r7, r3, r2
 *   mul r7, r7, c8.xxxx         ; w3 = 0.5t^3 - 0.5t^2
 *   add r6, r4, r5
 *   add r6, r6, r7
 *   sub r6, c7.wwww, r6         ; w2 = 1 - w0 - w1 - w3 (the weights sum to 1)
 *   ; each weight holds x in .x and y in .y; for row j = 0..3 (Wj = r4 + j):
 *   add r1, r0, c(3+j)
 *   texld r2, r1, s0
 *   mul r3, r2, r4.xxxx
 *   add r1, r1, c2
 *   texld r2, r1, s0
 *   mad r3, r2, r5.xxxx, r3
 *   add r1, r1, c2
 *   texld r2, r1, s0
 *   mad r3, r2, r6.xxxx, r3
 *   add r1, r1, c2
 *   texld r2, r1, s0
 *   mad r3, r2, r7.xxxx, r3
 *   mul r8, r3, r4.yyyy         ; j == 0
 *   mad r8, r3, Wj.yyyy, r8     ; j > 0
 *   ; end rows
 *   max r8, r8, c8.yyyy         ; >= 0
 *   min r8, r8, c7.wwww         ; <= 1
 *   min r9, r8, r8.wwww         ; colour <= alpha
 *   mov oC0, r9
 */

#define D3D9PS_OP(op, operands) \
    ((DWORD)(op) | ((DWORD)(operands) << D3DSI_INSTLENGTH_SHIFT))
#define D3D9PS_REGISTER(type, number) \
    ((DWORD)0x80000000u | \
     (((DWORD)(type) << D3DSP_REGTYPE_SHIFT) & (DWORD)D3DSP_REGTYPE_MASK) | \
     (((DWORD)(type) << D3DSP_REGTYPE_SHIFT2) & (DWORD)D3DSP_REGTYPE_MASK2) | \
     ((DWORD)(number) & (DWORD)D3DSP_REGNUM_MASK))
#define D3D9PS_SWIZZLE_XYZW ((DWORD)(D3DVS_X_X | D3DVS_Y_Y | D3DVS_Z_Z | D3DVS_W_W))
#define D3D9PS_SWIZZLE_XXXX ((DWORD)(D3DVS_X_X | D3DVS_Y_X | D3DVS_Z_X | D3DVS_W_X))
#define D3D9PS_SWIZZLE_YYYY ((DWORD)(D3DVS_X_Y | D3DVS_Y_Y | D3DVS_Z_Y | D3DVS_W_Y))
#define D3D9PS_SWIZZLE_ZZZZ ((DWORD)(D3DVS_X_Z | D3DVS_Y_Z | D3DVS_Z_Z | D3DVS_W_Z))
#define D3D9PS_SWIZZLE_WWWW ((DWORD)(D3DVS_X_W | D3DVS_Y_W | D3DVS_Z_W | D3DVS_W_W))

#define D3D9PS_DST(type, number) \
    (D3D9PS_REGISTER(type, number) | (DWORD)D3DSP_WRITEMASK_ALL)
#define D3D9PS_SRC(type, number, swizzle) (D3D9PS_REGISTER(type, number) | (swizzle))

#define D3D9PS_R(n) D3D9PS_DST(D3DSPR_TEMP, n)
#define D3D9PS_RS(n) D3D9PS_SRC(D3DSPR_TEMP, n, D3D9PS_SWIZZLE_XYZW)
#define D3D9PS_RX(n) D3D9PS_SRC(D3DSPR_TEMP, n, D3D9PS_SWIZZLE_XXXX)
#define D3D9PS_RY(n) D3D9PS_SRC(D3DSPR_TEMP, n, D3D9PS_SWIZZLE_YYYY)
#define D3D9PS_RW(n) D3D9PS_SRC(D3DSPR_TEMP, n, D3D9PS_SWIZZLE_WWWW)
#define D3D9PS_C(n) D3D9PS_SRC(D3DSPR_CONST, n, D3D9PS_SWIZZLE_XYZW)
#define D3D9PS_CX(n) D3D9PS_SRC(D3DSPR_CONST, n, D3D9PS_SWIZZLE_XXXX)
#define D3D9PS_CY(n) D3D9PS_SRC(D3DSPR_CONST, n, D3D9PS_SWIZZLE_YYYY)
#define D3D9PS_CZ(n) D3D9PS_SRC(D3DSPR_CONST, n, D3D9PS_SWIZZLE_ZZZZ)
#define D3D9PS_CW(n) D3D9PS_SRC(D3DSPR_CONST, n, D3D9PS_SWIZZLE_WWWW)
#define D3D9PS_T0 D3D9PS_SRC(D3DSPR_TEXTURE, 0, D3D9PS_SWIZZLE_XYZW)
#define D3D9PS_S0 D3D9PS_SRC(D3DSPR_SAMPLER, 0, D3D9PS_SWIZZLE_XYZW)

/* One tap of row `row_weight`'s run: step right (skipped for the first tap),
 * sample, weight by column `column_weight`.x, accumulate into r3. */
#define D3D9PS_TAP_STEP_SAMPLE \
    D3D9PS_OP(D3DSIO_ADD, 3), D3D9PS_R(1), D3D9PS_RS(1), D3D9PS_C(2), \
    D3D9PS_OP(D3DSIO_TEX, 3), D3D9PS_R(2), D3D9PS_RS(1), D3D9PS_S0
#define D3D9PS_ROW(j, combine) \
    D3D9PS_OP(D3DSIO_ADD, 3), D3D9PS_R(1), D3D9PS_RS(0), D3D9PS_C(3 + (j)), \
    D3D9PS_OP(D3DSIO_TEX, 3), D3D9PS_R(2), D3D9PS_RS(1), D3D9PS_S0, \
    D3D9PS_OP(D3DSIO_MUL, 3), D3D9PS_R(3), D3D9PS_RS(2), D3D9PS_RX(4), \
    D3D9PS_TAP_STEP_SAMPLE, \
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(3), D3D9PS_RS(2), D3D9PS_RX(5), D3D9PS_RS(3), \
    D3D9PS_TAP_STEP_SAMPLE, \
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(3), D3D9PS_RS(2), D3D9PS_RX(6), D3D9PS_RS(3), \
    D3D9PS_TAP_STEP_SAMPLE, \
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(3), D3D9PS_RS(2), D3D9PS_RX(7), D3D9PS_RS(3), \
    combine
#define D3D9PS_ROW_FIRST_COMBINE \
    D3D9PS_OP(D3DSIO_MUL, 3), D3D9PS_R(8), D3D9PS_RS(3), D3D9PS_RY(4)
#define D3D9PS_ROW_COMBINE(j) \
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(8), D3D9PS_RS(3), D3D9PS_RY(4 + (j)), D3D9PS_RS(8)

/** Constants c0..c8, see above. */
#define D3D9_UI_COMPOSITE_PS_CONSTANT_COUNT 9u

static DWORD const d3d9_ui_composite_bicubic_ps[] = {
    D3DPS_VERSION(2, 0),
    D3D9PS_OP(D3DSIO_DCL, 2),
    (DWORD)0x80000000u,
    D3D9PS_DST(D3DSPR_TEXTURE, 0),
    D3D9PS_OP(D3DSIO_DCL, 2),
    (DWORD)0x80000000u | (DWORD)D3DSTT_2D,
    D3D9PS_DST(D3DSPR_SAMPLER, 0),

    D3D9PS_OP(D3DSIO_MUL, 3), D3D9PS_R(2), D3D9PS_T0, D3D9PS_C(0),
    D3D9PS_OP(D3DSIO_ADD, 3), D3D9PS_R(2), D3D9PS_RS(2), D3D9PS_CX(7),
    D3D9PS_OP(D3DSIO_FRC, 2), D3D9PS_R(1), D3D9PS_RS(2),
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(0), D3D9PS_RS(1), D3D9PS_C(1), D3D9PS_T0,
    D3D9PS_OP(D3DSIO_MUL, 3), D3D9PS_R(2), D3D9PS_RS(1), D3D9PS_RS(1),
    D3D9PS_OP(D3DSIO_MUL, 3), D3D9PS_R(3), D3D9PS_RS(2), D3D9PS_RS(1),
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(4), D3D9PS_RS(1), D3D9PS_CX(7), D3D9PS_RS(2),
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(4), D3D9PS_RS(3), D3D9PS_CX(7), D3D9PS_RS(4),
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(5), D3D9PS_RS(2), D3D9PS_CY(7), D3D9PS_CW(7),
    D3D9PS_OP(D3DSIO_MAD, 4), D3D9PS_R(5), D3D9PS_RS(3), D3D9PS_CZ(7), D3D9PS_RS(5),
    D3D9PS_OP(D3DSIO_SUB, 3), D3D9PS_R(7), D3D9PS_RS(3), D3D9PS_RS(2),
    D3D9PS_OP(D3DSIO_MUL, 3), D3D9PS_R(7), D3D9PS_RS(7), D3D9PS_CX(8),
    D3D9PS_OP(D3DSIO_ADD, 3), D3D9PS_R(6), D3D9PS_RS(4), D3D9PS_RS(5),
    D3D9PS_OP(D3DSIO_ADD, 3), D3D9PS_R(6), D3D9PS_RS(6), D3D9PS_RS(7),
    D3D9PS_OP(D3DSIO_SUB, 3), D3D9PS_R(6), D3D9PS_CW(7), D3D9PS_RS(6),

    D3D9PS_ROW(0, D3D9PS_ROW_FIRST_COMBINE),
    D3D9PS_ROW(1, D3D9PS_ROW_COMBINE(1)),
    D3D9PS_ROW(2, D3D9PS_ROW_COMBINE(2)),
    D3D9PS_ROW(3, D3D9PS_ROW_COMBINE(3)),

    D3D9PS_OP(D3DSIO_MAX, 3), D3D9PS_R(8), D3D9PS_RS(8), D3D9PS_CY(8),
    D3D9PS_OP(D3DSIO_MIN, 3), D3D9PS_R(8), D3D9PS_RS(8), D3D9PS_CW(7),
    D3D9PS_OP(D3DSIO_MIN, 3), D3D9PS_R(9), D3D9PS_RS(8), D3D9PS_RW(8),
    D3D9PS_OP(D3DSIO_MOV, 2), D3D9PS_DST(D3DSPR_COLOROUT, 0), D3D9PS_RS(9),
    D3DPS_END(),
};

#undef D3D9PS_ROW_COMBINE
#undef D3D9PS_ROW_FIRST_COMBINE
#undef D3D9PS_ROW
#undef D3D9PS_TAP_STEP_SAMPLE
#undef D3D9PS_S0
#undef D3D9PS_T0
#undef D3D9PS_CW
#undef D3D9PS_CZ
#undef D3D9PS_CY
#undef D3D9PS_CX
#undef D3D9PS_C
#undef D3D9PS_RW
#undef D3D9PS_RY
#undef D3D9PS_RX
#undef D3D9PS_RS
#undef D3D9PS_R
#undef D3D9PS_SRC
#undef D3D9PS_DST
#undef D3D9PS_SWIZZLE_WWWW
#undef D3D9PS_SWIZZLE_ZZZZ
#undef D3D9PS_SWIZZLE_YYYY
#undef D3D9PS_SWIZZLE_XXXX
#undef D3D9PS_SWIZZLE_XYZW
#undef D3D9PS_REGISTER
#undef D3D9PS_OP

#endif
