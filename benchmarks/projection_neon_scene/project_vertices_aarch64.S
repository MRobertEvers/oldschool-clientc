/*
 * Hand-written AArch64/Advanced SIMD projection kernels used only by the
 * real-model scene benchmark.  These implement the production clip family:
 *
 *   - staged model-yaw, camera-yaw, camera-pitch rotations in signed 16.16;
 *   - FOV scaling with camera_cot15 followed by >> 6;
 *   - vrecpe + one vrecps refinement and truncating float-to-int conversion;
 *   - near-plane x sentinel (-5000), genuine -5000 nudge (-5001), and the
 *     production convention of leaving clipped y scaled but undivided.
 *
 * The staged shifts are intentional.  A precomposed 3x4 matrix is faster to
 * set up but rounds only once, whereas the renderer rounds after each Euler
 * rotation.  Combining them would make the benchmark compare different
 * pictures.
 */

#if defined(__aarch64__)

#if defined(__APPLE__)
#define C_SYMBOL2(name) _##name
#else
#define C_SYMBOL2(name) name
#endif
#define C_SYMBOL(name) C_SYMBOL2(name)

/* ProjectionNeonSceneArgs offsets. */
#define A_OX       0
#define A_OY       8
#define A_OZ       16
#define A_SX       24
#define A_SY       32
#define A_SZ       40
#define A_VX       48
#define A_VY       56
#define A_VZ       64
#define A_COUNT    72
#define A_CM       76
#define A_SM       80
#define A_CY       84
#define A_SYAW     88
#define A_CP       92
#define A_SP       96
#define A_TX       100
#define A_TY       104
#define A_TZ       108
#define A_COT      112
#define A_MID      116
#define A_NEAR     120
#define A_HAS_YAW  124

.text
.p2align 2

/*
 * Register convention shared by every generated loop:
 *   x0 args; x2/x3/x4 ortho; x5/x6/x7 screen; x9/x10/x11 input
 *   w12 block count
 *   v16 cm, v17 sm, v18 cy, v19 syaw, v20 cp, v21 sp, v22 cot
 *   v23 near, v24 mid, v25/v26/v27 translation
 *   v28 -5000, v29 -5001
 */
.macro LOAD_ARGS tex, ceil
    ldr x5, [x0, #A_SX]
    ldr x6, [x0, #A_SY]
    ldr x7, [x0, #A_SZ]
    ldr x9, [x0, #A_VX]
    ldr x10, [x0, #A_VY]
    ldr x11, [x0, #A_VZ]
    ldr w12, [x0, #A_COUNT]
    .if \ceil
        add w12, w12, #3
    .endif
    lsr w12, w12, #2
    .if \tex
        ldr x2, [x0, #A_OX]
        ldr x3, [x0, #A_OY]
        ldr x4, [x0, #A_OZ]
    .endif
    ldr w13, [x0, #A_CM]
    dup v16.4s, w13
    ldr w13, [x0, #A_SM]
    dup v17.4s, w13
    ldr w13, [x0, #A_CY]
    dup v18.4s, w13
    ldr w13, [x0, #A_SYAW]
    dup v19.4s, w13
    ldr w13, [x0, #A_CP]
    dup v20.4s, w13
    ldr w13, [x0, #A_SP]
    dup v21.4s, w13
    ldr w13, [x0, #A_COT]
    dup v22.4s, w13
    ldr w13, [x0, #A_NEAR]
    dup v23.4s, w13
    ldr w13, [x0, #A_MID]
    dup v24.4s, w13
    ldr w13, [x0, #A_TX]
    dup v25.4s, w13
    ldr w13, [x0, #A_TY]
    dup v26.4s, w13
    ldr w13, [x0, #A_TZ]
    dup v27.4s, w13
    mov w13, #-5000
    dup v28.4s, w13
    mov w13, #-5001
    dup v29.4s, w13
.endm

/*
 * Production's fused kernels use scalar C for the final 1--3 vertices.  Match
 * that path too: staged 32-bit wrapping multiplies and signed integer divide.
 * The unfused production kernels instead run their z-div tail through a padded
 * vector helper, so DEFINE_UNFUSED deliberately keeps the vector ceiling.
 */
.macro FUSED_SCALAR_TAIL tex
    ldr w8, [x0, #A_COUNT]
    and w8, w8, #3
    cbz w8, .Ltail_done_\@
.Ltail_loop_\@:
    ldrsh w13, [x9], #2             /* x */
    ldrsh w14, [x10], #2            /* y */
    ldrsh w15, [x11], #2            /* z */

    ldr w1, [x0, #A_CM]
    ldr w12, [x0, #A_SM]
    mul w16, w13, w1
    madd w16, w15, w12, w16
    asr w16, w16, #16               /* model x */
    mul w17, w15, w1
    msub w17, w13, w12, w17
    asr w17, w17, #16               /* model z */
    ldr w1, [x0, #A_TX]
    add w16, w16, w1
    ldr w1, [x0, #A_TY]
    add w14, w14, w1                /* translated y */
    ldr w1, [x0, #A_TZ]
    add w17, w17, w1

    ldr w1, [x0, #A_CY]
    ldr w12, [x0, #A_SYAW]
    mul w13, w16, w1
    madd w13, w17, w12, w13
    asr w13, w13, #16               /* camera x */
    mul w15, w17, w1
    msub w15, w16, w12, w15
    asr w15, w15, #16               /* yawed z */

    ldr w1, [x0, #A_CP]
    ldr w12, [x0, #A_SP]
    mul w16, w14, w1
    msub w16, w15, w12, w16
    asr w16, w16, #16               /* camera y */
    mul w17, w14, w12
    madd w17, w15, w1, w17
    asr w17, w17, #16               /* camera z */

    .if \tex
        str w13, [x2], #4
        str w16, [x3], #4
        str w17, [x4], #4
    .endif

    ldr w1, [x0, #A_COT]
    mul w13, w13, w1
    asr w13, w13, #6                /* scaled x */
    mul w16, w16, w1
    asr w16, w16, #6                /* scaled y */
    ldr w1, [x0, #A_MID]
    sub w15, w17, w1
    str w15, [x7], #4
    ldr w1, [x0, #A_NEAR]
    cmp w17, w1
    b.lt .Ltail_clipped_\@
    sdiv w13, w13, w17
    mov w1, #-5000
    cmp w13, w1
    b.ne .Ltail_x_ready_\@
    mov w13, #-5001
.Ltail_x_ready_\@:
    sdiv w16, w16, w17
    b .Ltail_store_\@
.Ltail_clipped_\@:
    mov w13, #-5000
.Ltail_store_\@:
    str w13, [x5], #4
    str w16, [x6], #4
    subs w8, w8, #1
    b.ne .Ltail_loop_\@
.Ltail_done_\@:
.endm

/* Load four int16 vertices, widen, and perform the exact staged Q16 transform.
 * Results: v5=x camera, v6=y camera, v7=z camera. */
.macro TRANSFORM4 model_yaw
    ldr d0, [x9], #8
    ldr d1, [x10], #8
    ldr d2, [x11], #8
    sshll v0.4s, v0.4h, #0
    sshll v1.4s, v1.4h, #0
    sshll v2.4s, v2.4h, #0

    .if \model_yaw
        mul v3.4s, v0.4s, v16.4s
        mla v3.4s, v2.4s, v17.4s
        sshr v3.4s, v3.4s, #16
        mul v4.4s, v2.4s, v16.4s
        mls v4.4s, v0.4s, v17.4s
        sshr v4.4s, v4.4s, #16
    .else
        mov v3.16b, v0.16b
        mov v4.16b, v2.16b
    .endif
    add v3.4s, v3.4s, v25.4s
    add v1.4s, v1.4s, v26.4s
    add v4.4s, v4.4s, v27.4s

    mul v5.4s, v3.4s, v18.4s
    mla v5.4s, v4.4s, v19.4s
    sshr v5.4s, v5.4s, #16
    mul v7.4s, v4.4s, v18.4s
    mls v7.4s, v3.4s, v19.4s
    sshr v7.4s, v7.4s, #16

    mul v6.4s, v1.4s, v20.4s
    mls v6.4s, v7.4s, v21.4s
    sshr v6.4s, v6.4s, #16
    mul v0.4s, v1.4s, v21.4s
    mla v0.4s, v7.4s, v20.4s
    sshr v7.4s, v0.4s, #16
.endm

/* v5/v6 camera x/y -> v0/v1 scaled numerators. */
.macro SCALE_XY
    mul v0.4s, v5.4s, v22.4s
    mul v1.4s, v6.4s, v22.4s
    sshr v0.4s, v0.4s, #6
    sshr v1.4s, v1.4s, #6
.endm

/* v7 z and v0/v1 scaled numerators -> v4/v31/v2 screen x/y/z. */
.macro DIVIDE_CLIP
    sub v2.4s, v7.4s, v24.4s
    cmlt v3.4s, v7.4s, v23.4s

    scvtf v4.4s, v7.4s
    frecpe v30.4s, v4.4s
    frecps v31.4s, v4.4s, v30.4s
    fmul v30.4s, v30.4s, v31.4s
    scvtf v4.4s, v0.4s
    scvtf v31.4s, v1.4s
    fmul v4.4s, v4.4s, v30.4s
    fmul v31.4s, v31.4s, v30.4s
    fcvtzs v4.4s, v4.4s
    fcvtzs v31.4s, v31.4s

    /* A genuine projected -5000 is reserved as -5001 in the clip family. */
    cmeq v0.4s, v4.4s, v28.4s
    bic v0.16b, v0.16b, v3.16b
    mov v30.16b, v0.16b
    bsl v30.16b, v29.16b, v4.16b
    mov v4.16b, v3.16b
    bsl v4.16b, v28.16b, v30.16b
    mov v30.16b, v3.16b
    bsl v30.16b, v1.16b, v31.16b
    mov v31.16b, v30.16b
.endm

.macro DEFINE_FUSED name, tex
.globl C_SYMBOL(\name)
C_SYMBOL(\name):
    LOAD_ARGS \tex, 0
    cbz w12, 2f
    ldr w1, [x0, #A_HAS_YAW]
    cbz w1, 3f
1:
    TRANSFORM4 1
    .if \tex
        str q5, [x2], #16
        str q6, [x3], #16
        str q7, [x4], #16
    .endif
    SCALE_XY
    DIVIDE_CLIP
    str q4, [x5], #16
    str q31, [x6], #16
    str q2, [x7], #16
    subs w12, w12, #1
    b.ne 1b
    b 2f
3:
    TRANSFORM4 0
    .if \tex
        str q5, [x2], #16
        str q6, [x3], #16
        str q7, [x4], #16
    .endif
    SCALE_XY
    DIVIDE_CLIP
    str q4, [x5], #16
    str q31, [x6], #16
    str q2, [x7], #16
    subs w12, w12, #1
    b.ne 3b
2:
    FUSED_SCALAR_TAIL \tex
    ret
.endm

.macro DEFINE_UNFUSED name, tex
.globl C_SYMBOL(\name)
C_SYMBOL(\name):
    LOAD_ARGS \tex, 1
    cbz w12, 99f
    /* Preserve the screen bases and block count for pass two. */
    mov x14, x5
    mov x15, x6
    mov x16, x7
    mov w17, w12
    ldr w1, [x0, #A_HAS_YAW]
    cbz w1, 3f
1:
    TRANSFORM4 1
    .if \tex
        str q5, [x2], #16
        str q6, [x3], #16
        str q7, [x4], #16
    .endif
    SCALE_XY
    str q0, [x5], #16
    str q1, [x6], #16
    str q7, [x7], #16
    subs w12, w12, #1
    b.ne 1b
    b 4f
3:
    TRANSFORM4 0
    .if \tex
        str q5, [x2], #16
        str q6, [x3], #16
        str q7, [x4], #16
    .endif
    SCALE_XY
    str q0, [x5], #16
    str q1, [x6], #16
    str q7, [x7], #16
    subs w12, w12, #1
    b.ne 3b

4:
    mov x5, x14
    mov x6, x15
    mov x7, x16
    mov w12, w17
2:
    ldr q0, [x5]
    ldr q1, [x6]
    ldr q7, [x7]
    DIVIDE_CLIP
    str q4, [x5], #16
    str q31, [x6], #16
    str q2, [x7], #16
    subs w12, w12, #1
    b.ne 2b
99:
    ret
.endm

DEFINE_FUSED project_vertices_asm_fused_tex, 1
DEFINE_UNFUSED project_vertices_asm_unfused_tex, 1
DEFINE_FUSED project_vertices_asm_fused_notex, 0
DEFINE_UNFUSED project_vertices_asm_unfused_notex, 0

#endif
