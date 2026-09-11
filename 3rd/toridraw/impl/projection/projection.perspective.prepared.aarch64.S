    .section __TEXT,__text,regular,pure_instructions

/*
 * The screen box, accumulated where the screen coordinates are born.
 *
 * Every generic (non-exact-four) path keeps lane-wise min x / max x / min y /
 * max y of every full block in v8-v11 and writes them to the Scene's
 * projection_bound block, 128 bytes past the output-pointer block (right
 * after the prepared camera; toridraw.c pins the layout). Four ops per
 * block on registers the body already holds, against the second pass over
 * every output the C sweep costs a large model. The scalar tail does not
 * accumulate: the caller records how many vertices the block covers
 * (count & ~3) and sweeps the remainder itself.
 *
 * v8-v11 are callee-saved in their low 64 bits, so the generic paths save
 * and restore d8-d11 around their loop; the exact-four bodies never touch
 * them and keep their zero-stack entry.
 */
    .macro BOUND_INIT
    mvni    v8.4s, #0x80, lsl #24
    movi    v9.4s, #0x80, lsl #24
    mvni    v10.4s, #0x80, lsl #24
    movi    v11.4s, #0x80, lsl #24
    .endm

/* v25 = screen x, v26 = screen y, both already fcvtzs'd. */
    .macro BOUND_ACCUMULATE
    smin    v8.4s, v8.4s, v25.4s
    smax    v9.4s, v9.4s, v25.4s
    smin    v10.4s, v10.4s, v26.4s
    smax    v11.4s, v11.4s, v26.4s
    .endm

    .macro BOUND_SAVE_REGS
    stp     d8, d9, [sp, #-32]!
    stp     d10, d11, [sp, #16]
    .endm

/* \base addresses the output-pointer block. */
    .macro BOUND_STORE_AND_RESTORE base
    stp     q8, q9, [\base, #128]
    stp     q10, q11, [\base, #160]
    ldp     d10, d11, [sp, #16]
    ldp     d8, d9, [sp], #32
    .endm

/* Exact scalar integer tail. Constants are splatted in v0-v4/v16-v20/v30. */
    .macro WHOLE_SCALAR_TAIL tex, vx, vy, vz, sx, sy, sz, rem
    cbz     \rem, 99f
98:
    ldrsh   w14, [\vx], #2
    ldrsh   w15, [\vy], #2
    ldrsh   w16, [\vz], #2

    fmov    w12, s0
    fmov    w13, s1
    mul     w17, w14, w12
    madd    w17, w16, w13, w17
    mul     w16, w16, w12
    msub    w14, w14, w13, w16
    asr     w17, w17, #16
    asr     w14, w14, #16
    fmov    w12, s2
    add     w17, w17, w12
    fmov    w12, s3
    add     w15, w15, w12
    fmov    w12, s4
    add     w14, w14, w12

    fmov    w12, s16
    fmov    w13, s17
    mul     w16, w17, w12
    madd    w16, w14, w13, w16
    mul     w14, w14, w12
    msub    w14, w17, w13, w14
    asr     w16, w16, #16
    asr     w14, w14, #16

    fmov    w12, s18
    fmov    w13, s19
    mul     w17, w15, w12
    msub    w17, w14, w13, w17
    mul     w15, w15, w13
    madd    w14, w14, w12, w15
    asr     w17, w17, #16
    asr     w14, w14, #16

    .if \tex
        str w16, [x0], #4
        str w17, [x1], #4
        str w14, [x2], #4
    .endif

    fmov    w12, s20
    mul     w16, w16, w12
    mul     w17, w17, w12
    asr     w16, w16, #6
    asr     w17, w17, #6
    fmov    w12, s30
    sub     w13, w14, w12
    str     w13, [\sz], #4
    sdiv    w16, w16, w14
    sdiv    w17, w17, w14
    str     w16, [\sx], #4
    str     w17, [\sy], #4
    subs    \rem, \rem, #1
    b.ne    98b
99:
    .endm

/* One generic four-lane iteration. Inputs are v5-v7, constants v0-v4. */
    .macro WHOLE_VECTOR_BODY tex, sx, sy, sz
    sshll   v5.4s, v5.4h, #0
    sshll   v7.4s, v7.4h, #0
    mul     v21.4s, v5.4s, v0.4s
    mla     v21.4s, v7.4s, v1.4s
    mul     v22.4s, v7.4s, v0.4s
    mls     v22.4s, v5.4s, v1.4s
    mov     v23.16b, v2.16b
    ssra    v23.4s, v21.4s, #16
    mov     v24.16b, v3.16b
    saddw   v24.4s, v24.4s, v6.4h
    mov     v25.16b, v4.16b
    ssra    v25.4s, v22.4s, #16

    mul     v21.4s, v23.4s, v16.4s
    mla     v21.4s, v25.4s, v17.4s
    mul     v22.4s, v25.4s, v16.4s
    mls     v22.4s, v23.4s, v17.4s
    sshr    v21.4s, v21.4s, #16
    sshr    v22.4s, v22.4s, #16

    mul     v23.4s, v24.4s, v18.4s
    mls     v23.4s, v22.4s, v19.4s
    mul     v24.4s, v24.4s, v19.4s
    mla     v24.4s, v22.4s, v18.4s
    sshr    v23.4s, v23.4s, #16
    sshr    v24.4s, v24.4s, #16

    mul     v25.4s, v21.4s, v20.4s
    mul     v26.4s, v23.4s, v20.4s
    sshr    v25.4s, v25.4s, #6
    sshr    v26.4s, v26.4s, #6
    scvtf   v27.4s, v24.4s
    frecpe  v28.4s, v27.4s
    frecps  v27.4s, v27.4s, v28.4s
    fmul    v27.4s, v28.4s, v27.4s
    scvtf   v25.4s, v25.4s
    fmul    v25.4s, v25.4s, v27.4s
    fcvtzs  v25.4s, v25.4s
    scvtf   v26.4s, v26.4s
    fmul    v26.4s, v26.4s, v27.4s
    sub     v31.4s, v24.4s, v30.4s

    .if \tex
        str q21, [x0], #16
        str q23, [x1], #16
        str q24, [x2], #16
    .endif
    str     q31, [\sz], #16
    fcvtzs  v26.4s, v26.4s
    str     q25, [\sx], #16
    str     q26, [\sy], #16
    BOUND_ACCUMULATE
    .endm

/* Exact4 body uses v0-v2 inputs, v3-v4 model and v5-v7 translations. */
    .macro WHOLE_EXACT4_BODY tex, midreg, sx, sy, sz
    sshll   v0.4s, v0.4h, #0
    sshll   v2.4s, v2.4h, #0
    mul     v21.4s, v0.4s, v3.4s
    mla     v21.4s, v2.4s, v4.4s
    mul     v22.4s, v2.4s, v3.4s
    mls     v22.4s, v0.4s, v4.4s
    ssra    v5.4s, v21.4s, #16
    saddw   v6.4s, v6.4s, v1.4h
    ssra    v7.4s, v22.4s, #16

    mul     v21.4s, v5.4s, v16.4s
    mla     v21.4s, v7.4s, v17.4s
    mul     v22.4s, v7.4s, v16.4s
    mls     v22.4s, v5.4s, v17.4s
    sshr    v21.4s, v21.4s, #16
    sshr    v22.4s, v22.4s, #16

    mul     v23.4s, v6.4s, v18.4s
    mls     v23.4s, v22.4s, v19.4s
    mul     v24.4s, v6.4s, v19.4s
    mla     v24.4s, v22.4s, v18.4s
    sshr    v23.4s, v23.4s, #16
    sshr    v24.4s, v24.4s, #16

    mul     v25.4s, v21.4s, v20.4s
    mul     v26.4s, v23.4s, v20.4s
    sshr    v25.4s, v25.4s, #6
    sshr    v26.4s, v26.4s, #6
    scvtf   v27.4s, v24.4s
    frecpe  v28.4s, v27.4s
    frecps  v27.4s, v27.4s, v28.4s
    fmul    v27.4s, v28.4s, v27.4s
    scvtf   v25.4s, v25.4s
    fmul    v25.4s, v25.4s, v27.4s
    fcvtzs  v25.4s, v25.4s
    scvtf   v26.4s, v26.4s
    fmul    v26.4s, v26.4s, v27.4s
    dup     v29.4s, \midreg
    sub     v29.4s, v24.4s, v29.4s

    .if \tex
        str q21, [x0]
        str q23, [x1]
        str q24, [x2]
    .endif
    str     q29, [\sz]
    fcvtzs  v26.4s, v26.4s
    str     q25, [\sx]
    str     q26, [\sy]
    ret
    .endm

/* model_yaw==0 preserves the C no-rotation path, independent of table[0]. */
    .macro WHOLE_NOYAW_SCALAR_TAIL tex, vx, vy, vz, sx, sy, sz, rem
    cbz     \rem, 99f
98:
    ldrsh   w14, [\vx], #2
    ldrsh   w15, [\vy], #2
    ldrsh   w16, [\vz], #2
    fmov    w12, s2
    add     w17, w14, w12
    fmov    w12, s3
    add     w15, w15, w12
    fmov    w12, s4
    add     w14, w16, w12

    fmov    w12, s16
    fmov    w13, s17
    mul     w16, w17, w12
    madd    w16, w14, w13, w16
    mul     w14, w14, w12
    msub    w14, w17, w13, w14
    asr     w16, w16, #16
    asr     w14, w14, #16

    fmov    w12, s18
    fmov    w13, s19
    mul     w17, w15, w12
    msub    w17, w14, w13, w17
    mul     w15, w15, w13
    madd    w14, w14, w12, w15
    asr     w17, w17, #16
    asr     w14, w14, #16

    .if \tex
        str w16, [x0], #4
        str w17, [x1], #4
        str w14, [x2], #4
    .endif
    fmov    w12, s20
    mul     w16, w16, w12
    mul     w17, w17, w12
    asr     w16, w16, #6
    asr     w17, w17, #6
    fmov    w12, s30
    sub     w13, w14, w12
    str     w13, [\sz], #4
    sdiv    w16, w16, w14
    sdiv    w17, w17, w14
    str     w16, [\sx], #4
    str     w17, [\sy], #4
    subs    \rem, \rem, #1
    b.ne    98b
99:
    .endm

    .macro WHOLE_NOYAW_VECTOR_BODY tex, sx, sy, sz
    mov     v23.16b, v2.16b
    saddw   v23.4s, v23.4s, v5.4h
    mov     v24.16b, v3.16b
    saddw   v24.4s, v24.4s, v6.4h
    mov     v25.16b, v4.16b
    saddw   v25.4s, v25.4s, v7.4h

    mul     v21.4s, v23.4s, v16.4s
    mla     v21.4s, v25.4s, v17.4s
    mul     v22.4s, v25.4s, v16.4s
    mls     v22.4s, v23.4s, v17.4s
    sshr    v21.4s, v21.4s, #16
    sshr    v22.4s, v22.4s, #16

    mul     v23.4s, v24.4s, v18.4s
    mls     v23.4s, v22.4s, v19.4s
    mul     v24.4s, v24.4s, v19.4s
    mla     v24.4s, v22.4s, v18.4s
    sshr    v23.4s, v23.4s, #16
    sshr    v24.4s, v24.4s, #16

    mul     v25.4s, v21.4s, v20.4s
    mul     v26.4s, v23.4s, v20.4s
    sshr    v25.4s, v25.4s, #6
    sshr    v26.4s, v26.4s, #6
    scvtf   v27.4s, v24.4s
    frecpe  v28.4s, v27.4s
    frecps  v27.4s, v27.4s, v28.4s
    fmul    v27.4s, v28.4s, v27.4s
    scvtf   v25.4s, v25.4s
    fmul    v25.4s, v25.4s, v27.4s
    fcvtzs  v25.4s, v25.4s
    scvtf   v26.4s, v26.4s
    fmul    v26.4s, v26.4s, v27.4s
    sub     v31.4s, v24.4s, v30.4s

    .if \tex
        str q21, [x0], #16
        str q23, [x1], #16
        str q24, [x2], #16
    .endif
    str     q31, [\sz], #16
    fcvtzs  v26.4s, v26.4s
    str     q25, [\sx], #16
    str     q26, [\sy], #16
    BOUND_ACCUMULATE
    .endm

    .macro WHOLE_NOYAW_EXACT4_BODY tex, midreg, sx, sy, sz
    saddw   v5.4s, v5.4s, v0.4h
    saddw   v6.4s, v6.4s, v1.4h
    saddw   v7.4s, v7.4s, v2.4h

    mul     v21.4s, v5.4s, v16.4s
    mla     v21.4s, v7.4s, v17.4s
    mul     v22.4s, v7.4s, v16.4s
    mls     v22.4s, v5.4s, v17.4s
    sshr    v21.4s, v21.4s, #16
    sshr    v22.4s, v22.4s, #16

    mul     v23.4s, v6.4s, v18.4s
    mls     v23.4s, v22.4s, v19.4s
    mul     v24.4s, v6.4s, v19.4s
    mla     v24.4s, v22.4s, v18.4s
    sshr    v23.4s, v23.4s, #16
    sshr    v24.4s, v24.4s, #16

    mul     v25.4s, v21.4s, v20.4s
    mul     v26.4s, v23.4s, v20.4s
    sshr    v25.4s, v25.4s, #6
    sshr    v26.4s, v26.4s, #6
    scvtf   v27.4s, v24.4s
    frecpe  v28.4s, v27.4s
    frecps  v27.4s, v27.4s, v28.4s
    fmul    v27.4s, v28.4s, v27.4s
    scvtf   v25.4s, v25.4s
    fmul    v25.4s, v25.4s, v27.4s
    fcvtzs  v25.4s, v25.4s
    scvtf   v26.4s, v26.4s
    fmul    v26.4s, v26.4s, v27.4s
    dup     v29.4s, \midreg
    sub     v29.4s, v24.4s, v29.4s

    .if \tex
        str q21, [x0]
        str q23, [x1]
        str q24, [x2]
    .endif
    str     q29, [\sz]
    fcvtzs  v26.4s, v26.4s
    str     q25, [\sx]
    str     q26, [\sy]
    ret
    .endm

/*
 * Renderer-native ABI. x0 addresses the contiguous six-pointer Scene block
 * (screen xyz, then orthographic xyz); its prepared-camera vectors begin
 * exactly 48 bytes later, and the projection_bound block it writes back (see
 * BOUND_INIT above) 128 bytes later. x1-x3 are input xyz, w4=count, w5=model
 * yaw, w6=model mid-z, and x7 addresses contiguous Position xyz integers.
 * The exact-four bodies are zero-stack; the generic paths spill d8-d11.
 */
    .p2align 2
    .globl _toridraw_project_vertices_fused_neon_noclip_native_prepared_aarch64
_toridraw_project_vertices_fused_neon_noclip_native_prepared_aarch64:
    add     x8, x0, #48
    ldp     q16, q17, [x8]
    ldp     q18, q19, [x8, #32]
    ldr     q20, [x8, #64]
    cbz     w5, Lnative_tex_noyaw

    adrp    x8, _g_projection_model_yaw_table@PAGE
    add     x8, x8, _g_projection_model_yaw_table@PAGEOFF
    add     x8, x8, w5, uxtw #3

    cmp     w4, #4
    b.ne    Lnative_tex_generic
    ld2r.4s { v3, v4 }, [x8]
    ldr     d0, [x1]
    ldr     d1, [x2]
    ldr     d2, [x3]
    mov     x8, x0
    ldp     x3, x4, [x8]
    ldp     x5, x0, [x8, #16]
    ldp     x1, x2, [x8, #32]
    ld1r.4s { v5 }, [x7], #4
    ld1r.4s { v6 }, [x7], #4
    ld1r.4s { v7 }, [x7]
    WHOLE_EXACT4_BODY 1, w6, x3, x4, x5

Lnative_tex_generic:
    BOUND_SAVE_REGS
    BOUND_INIT
    mov     x9, x0
    ld2r.4s { v0, v1 }, [x8]
    ld1r.4s { v2 }, [x7], #4
    ld1r.4s { v3 }, [x7], #4
    ld1r.4s { v4 }, [x7]
    dup     v30.4s, w6
    and     w11, w4, #3
    lsr     w13, w4, #2
    mov     x8, x3
    mov     x7, x2
    mov     x6, x1
    mov     x15, x0
    ldp     x3, x4, [x15]
    ldp     x5, x0, [x15, #16]
    ldp     x1, x2, [x15, #32]
    cbz     w13, Lnative_tex_tail
Lnative_tex_loop:
    ldr     d5, [x6], #8
    ldr     d6, [x7], #8
    ldr     d7, [x8], #8
    WHOLE_VECTOR_BODY 1, x3, x4, x5
    subs    w13, w13, #1
    b.ne    Lnative_tex_loop
Lnative_tex_tail:
    WHOLE_SCALAR_TAIL 1, x6, x7, x8, x3, x4, x5, w11
    BOUND_STORE_AND_RESTORE x9
    ret

Lnative_tex_noyaw:
    cmp     w4, #4
    b.ne    Lnative_tex_noyaw_generic
    ldr     d0, [x1]
    ldr     d1, [x2]
    ldr     d2, [x3]
    mov     x8, x0
    ldp     x3, x4, [x8]
    ldp     x5, x0, [x8, #16]
    ldp     x1, x2, [x8, #32]
    ld1r.4s { v5 }, [x7], #4
    ld1r.4s { v6 }, [x7], #4
    ld1r.4s { v7 }, [x7]
    WHOLE_NOYAW_EXACT4_BODY 1, w6, x3, x4, x5

Lnative_tex_noyaw_generic:
    BOUND_SAVE_REGS
    BOUND_INIT
    mov     x9, x0
    ld1r.4s { v2 }, [x7], #4
    ld1r.4s { v3 }, [x7], #4
    ld1r.4s { v4 }, [x7]
    dup     v30.4s, w6
    and     w11, w4, #3
    lsr     w13, w4, #2
    mov     x8, x3
    mov     x7, x2
    mov     x6, x1
    mov     x15, x0
    ldp     x3, x4, [x15]
    ldp     x5, x0, [x15, #16]
    ldp     x1, x2, [x15, #32]
    cbz     w13, Lnative_tex_noyaw_tail
Lnative_tex_noyaw_loop:
    ldr     d5, [x6], #8
    ldr     d6, [x7], #8
    ldr     d7, [x8], #8
    WHOLE_NOYAW_VECTOR_BODY 1, x3, x4, x5
    subs    w13, w13, #1
    b.ne    Lnative_tex_noyaw_loop
Lnative_tex_noyaw_tail:
    WHOLE_NOYAW_SCALAR_TAIL 1, x6, x7, x8, x3, x4, x5, w11
    BOUND_STORE_AND_RESTORE x9
    ret

    .p2align 2
    .globl _toridraw_project_vertices_fused_neon_notex_noclip_native_prepared_aarch64
_toridraw_project_vertices_fused_neon_notex_noclip_native_prepared_aarch64:
    add     x8, x0, #48
    ldp     q16, q17, [x8]
    ldp     q18, q19, [x8, #32]
    ldr     q20, [x8, #64]
    cbz     w5, Lnative_notex_noyaw

    adrp    x8, _g_projection_model_yaw_table@PAGE
    add     x8, x8, _g_projection_model_yaw_table@PAGEOFF
    add     x8, x8, w5, uxtw #3

    cmp     w4, #4
    b.ne    Lnative_notex_generic
    ld2r.4s { v3, v4 }, [x8]
    ldr     d0, [x1]
    ldr     d1, [x2]
    ldr     d2, [x3]
    mov     x8, x0
    ldp     x0, x1, [x8]
    ldr     x2, [x8, #16]
    ld1r.4s { v5 }, [x7], #4
    ld1r.4s { v6 }, [x7], #4
    ld1r.4s { v7 }, [x7]
    WHOLE_EXACT4_BODY 0, w6, x0, x1, x2

Lnative_notex_generic:
    BOUND_SAVE_REGS
    BOUND_INIT
    mov     x9, x0
    ld2r.4s { v0, v1 }, [x8]
    ld1r.4s { v2 }, [x7], #4
    ld1r.4s { v3 }, [x7], #4
    ld1r.4s { v4 }, [x7]
    dup     v30.4s, w6
    mov     w7, w4
    mov     x5, x3
    mov     x4, x2
    mov     x3, x1
    mov     x8, x0
    ldp     x0, x1, [x8]
    ldr     x2, [x8, #16]
    and     w8, w7, #3
    lsr     w7, w7, #2
    cbz     w7, Lnative_notex_tail
Lnative_notex_loop:
    ldr     d5, [x3], #8
    ldr     d6, [x4], #8
    ldr     d7, [x5], #8
    WHOLE_VECTOR_BODY 0, x0, x1, x2
    subs    w7, w7, #1
    b.ne    Lnative_notex_loop
Lnative_notex_tail:
    WHOLE_SCALAR_TAIL 0, x3, x4, x5, x0, x1, x2, w8
    BOUND_STORE_AND_RESTORE x9
    ret

Lnative_notex_noyaw:
    cmp     w4, #4
    b.ne    Lnative_notex_noyaw_generic
    ldr     d0, [x1]
    ldr     d1, [x2]
    ldr     d2, [x3]
    mov     x8, x0
    ldp     x0, x1, [x8]
    ldr     x2, [x8, #16]
    ld1r.4s { v5 }, [x7], #4
    ld1r.4s { v6 }, [x7], #4
    ld1r.4s { v7 }, [x7]
    WHOLE_NOYAW_EXACT4_BODY 0, w6, x0, x1, x2

Lnative_notex_noyaw_generic:
    BOUND_SAVE_REGS
    BOUND_INIT
    mov     x9, x0
    ld1r.4s { v2 }, [x7], #4
    ld1r.4s { v3 }, [x7], #4
    ld1r.4s { v4 }, [x7]
    dup     v30.4s, w6
    mov     w7, w4
    mov     x5, x3
    mov     x4, x2
    mov     x3, x1
    mov     x8, x0
    ldp     x0, x1, [x8]
    ldr     x2, [x8, #16]
    and     w8, w7, #3
    lsr     w7, w7, #2
    cbz     w7, Lnative_notex_noyaw_tail
Lnative_notex_noyaw_loop:
    ldr     d5, [x3], #8
    ldr     d6, [x4], #8
    ldr     d7, [x5], #8
    WHOLE_NOYAW_VECTOR_BODY 0, x0, x1, x2
    subs    w7, w7, #1
    b.ne    Lnative_notex_noyaw_loop
Lnative_notex_noyaw_tail:
    WHOLE_NOYAW_SCALAR_TAIL 0, x3, x4, x5, x0, x1, x2, w8
    BOUND_STORE_AND_RESTORE x9
    ret

    .subsections_via_symbols
