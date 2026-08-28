/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES   (i686 only)
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The gouraud triangle, batched, in handrolled AArch64 / Advanced SIMD --
 * the NEON twin of gouraud_tri_i686.S. Only the two RUN doors exist on this
 * lane; the per-face path keeps the C, which is what the A/B baseline
 * measures.
 *
 * WHAT IS DIFFERENT FROM THE i686 KERNEL
 *
 *   - Everything the walk carries -- three slopes, three edges, the colour
 *     accumulator and its two steps, the row pointer, the segment counts --
 *     lives in registers for the whole triangle. The i686 kernel spent its
 *     effort choosing which four values got a register; this one has thirty.
 *   - The three edge slopes come off one packed NEON reciprocal, the i686
 *     kernel's EDGESLOPES3 ladder (edge_slopes_aarch64.inc). The two colour
 *     gradients share one double reciprocal of sarea, as the i686 kernel and
 *     the C both do: one fdiv, two fmul, two fcvtzs.
 *   - The fill is toridraw_gouraud_span_fill_short transcribed: one palette
 *     lookup per FOUR pixels, phased from x_start, stored as one q register;
 *     the 1..3 tail is three stores and two conditional indices. The 4-pixel
 *     colour quantization is visible output (toridraw_scanline_parity_test)
 *     and is reproduced exactly, not re-blocked.
 *   - The colour accumulator is stepped at the row join, drawn or not, as the
 *     C does; the edges are stepped before the fill, which frees them from
 *     the fill's register pressure -- the same schedule the i686 kernel uses.
 *
 * EXACTNESS. Scored against
 * raster_gouraudhsllightness_screen_opaque_bary_branching_s4() and the alpha
 * twin, framebuffer against framebuffer (toridraw_presorted_neon_test.c),
 * on the i686 kernel's triangle and pixel budget. The walk and both fills
 * are bit-identical: assembled with -DTORIDRAW_EDGE_IDIV the test demands
 * every pixel identical, and gets it.
 *
 * ROW LAYOUT. `rows` is `count` 48-byte records: x0,x1,x2,pad / y0,y1,y2,pad
 * / c0,c1,c2,alpha -- already in y order, colours permuted with the vertices,
 * the viewport offset folded in. The same record the i686 kernel reads; the
 * opaque door never looks at lane 11.
 *
 * AAPCS64. x19-x28 saved; v8-v15 untouched; no call is made.
 */

#if defined(__aarch64__)

#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

#define R_X   0
#define R_Y   16
#define R_C   32
#define R_A   44
#define ROWBYTES 48

#include "graphics/raster/edge_slopes_aarch64.inc"

	.text
	.p2align 2

/*
 * Register map, once the prologue has run:
 *
 *   x0  pixel_buffer          w1  stride (pixels)      w2 screen_width
 *   w3  screen_height         x4  current row          w5 rows left
 *   x6  stride * 4 (bytes)    x7  g_hsl16_to_rgb_table
 *   v17 65536.0f x4  v18 {0,0,-1,0}    (edge slope constants)
 *   w8  step_y_hsl_ish8       (per-row colour step)
 *   w15 sxAC  w16 sxAB  w17 sxBC        (16.16 edge steps)
 *   w19 eAC   w20 eAB   w21 eBC         (16.16 edge x)
 *   x22 pointer to the current row of the frame buffer
 *   w23 n1    w24 n2    w25 nc1  w26 nc2
 *   w27 hsl_ish8 (row colour accumulator)
 *   w28 step_x_hsl_ish8      (per-pixel colour step; x4 per block via lsl #2)
 *   alpha: v7 alpha x8 (u16), v2 (255-alpha) x8, v3 0x00FFFFFF x4; v1 = K
 *   w9-w14, x13, x14: scratch     v0, v4-v6: scratch
 */

/*
 * Colour accumulator in \acc -> palette entry in \dst, clamp and all: an
 * edge can land a subpixel past its vertex after the fixed-point nudge, so
 * the interpolant is not guaranteed inside the palette even though every
 * vertex is. Out of range -> 0 or 0xFFFF, branchlessly, off a rarely-taken
 * branch. Clobbers nothing else.
 */
.macro LOOKUP acc, dst
	asr	\dst, \acc, #8
	tst	\dst, #0xFFFF0000
	b.eq	90f
	asr	\dst, \dst, #31
	mvn	\dst, \dst
	and	\dst, \dst, #0xFFFF
90:
	ldr	\dst, [x7, \dst, uxtw #2]
.endm

/* flat_screen_fixed_edges_no_hclip; see flat_tri_aarch64.S. Clobbers w9, w10, w14. */
.macro NOHCLIP ea0, sa, eb0, sb, n, dst
	cmp	\n, #0
	b.le	81f
	sub	w14, \n, #1
	madd	w9, \sa, w14, \ea0
	cmp	\ea0, w9
	csel	w9, \ea0, w9, lt
	tbnz	w9, #31, 82f
	madd	w10, \sb, w14, \eb0
	cmp	\eb0, w10
	csel	w10, \eb0, w10, gt
	cmp	w10, w2, lsl #16
	b.ge	82f
81:
	mov	\dst, #1
	b	83f
82:
	mov	\dst, #0
83:
.endm

/*
 * The opaque span. x13 destination, w12 pixel count (>= 1), w11 the colour
 * at x_start. toridraw_gouraud_span_fill_short: the under-four span is lifted
 * clear of the block loop, because 59.5% of spans take zero trips through it.
 * Clobbers w10, w11, w12, x13, w14, v0.
 */
.macro SPANFILL
	cmp	w12, #4
	b.lt	71f
	lsr	w14, w12, #2
	and	w12, w12, #3
70:
	LOOKUP	w11, w10
	dup	v0.4s, w10
	str	q0, [x13], #16
	add	w11, w11, w28, lsl #2
	subs	w14, w14, #1
	b.ne	70b
	cbz	w12, 73f
71:
	/* 1..3 pixels: three stores and two conditional indices. */
	LOOKUP	w11, w10
	str	w10, [x13]
	cmp	w12, #1
	cset	w14, gt
	str	w10, [x13, w14, uxtw #2]
	cmp	w12, #2
	cset	w14, gt
	lsl	w14, w14, #1
	str	w10, [x13, w14, uxtw #2]
73:
.endm

/*
 * The source colour's whole contribution to alpha_blend() for the palette
 * entry in \src, broadcast into v1:
 *
 *   K = (((o & 0xFF00FF) * a) >> 8) & 0xFF00FF + (((o & 0xFF00) * a) >> 8) & 0xFF00
 *
 * per channel in u16 lanes. Unlike the flat kernel this cannot be hoisted
 * out of the span: the colour changes every four pixels.
 */
.macro BLENDK src
	fmov	s1, \src
	uxtl	v1.8h, v1.8b
	mul	v1.4h, v1.4h, v7.4h
	ushr	v1.4h, v1.4h, #8
	xtn	v1.8b, v1.8h
	and	v1.8b, v1.8b, v3.8b
	dup	v1.4s, v1.s[0]
.endm

/* Four destination pixels at x13 blended under v1 and stored. */
.macro BLEND4
	ldr	q4, [x13]
	uxtl	v5.8h, v4.8b
	uxtl2	v6.8h, v4.16b
	mul	v5.8h, v5.8h, v2.8h
	mul	v6.8h, v6.8h, v2.8h
	ushr	v5.8h, v5.8h, #8
	ushr	v6.8h, v6.8h, #8
	xtn	v4.8b, v5.8h
	xtn2	v4.16b, v6.8h
	and	v4.16b, v4.16b, v3.16b
	add	v4.4s, v4.4s, v1.4s
	str	q4, [x13], #16
.endm

/*
 * The alpha span. Same blocking as the opaque one, because the palette
 * lookup is per block in both and the phase is visible output. The 1..3
 * tail is a real loop: a blend is not idempotent, so the opaque span's
 * duplicate stores are not available.
 * Clobbers w10-w12, x13, w14, v1, v4-v6.
 */
.macro SPANFILL_ALPHA
	cmp	w12, #4
	b.lt	76f
	lsr	w14, w12, #2
	and	w12, w12, #3
75:
	LOOKUP	w11, w10
	BLENDK	w10
	BLEND4
	add	w11, w11, w28, lsl #2
	subs	w14, w14, #1
	b.ne	75b
	cbz	w12, 78f
76:
	LOOKUP	w11, w10
	BLENDK	w10
77:
	ldr	s4, [x13]
	uxtl	v5.8h, v4.8b
	mul	v5.4h, v5.4h, v2.4h
	ushr	v5.4h, v5.4h, #8
	xtn	v4.8b, v5.8h
	and	v4.8b, v4.8b, v3.8b
	add	v4.2s, v4.2s, v1.2s
	str	s4, [x13], #4
	subs	w12, w12, #1
	b.ne	77b
78:
.endm

/*
 * One trapezoid segment; see flat_tri_aarch64.S for the shape. The colour
 * accumulator w27 is stepped at the row join, drawn or not, and the colour
 * at x_start is x_start * step_x + hsl (low 32 bits, as the C wraps).
 */
.macro SEGMENT el, er, sl, sr, nc, cnt, fill
	cmp	\cnt, #0
	b.le	69f
	cbz	\nc, 65f

	.p2align 4
60:
	mov	w9, \el
	mov	w10, \er
	add	\el, \el, \sl
	add	\er, \er, \sr
	cmp	w9, w10
	b.eq	64f
	asr	w9, w9, #16
	asr	w10, w10, #16
	cmp	w9, w10
	b.ge	64f
	sub	w12, w10, w9
	madd	w11, w9, w28, w27		/* colour at x_start                 */
	add	x13, x22, w9, sxtw #2
	\fill
64:
	add	w27, w27, w8			/* every row, drawn or not           */
	add	x22, x22, x6
	subs	\cnt, \cnt, #1
	b.ne	60b
	b	69f

	.p2align 4
65:
	mov	w9, \el
	mov	w10, \er
	add	\el, \el, \sl
	add	\er, \er, \sr
	cmp	w9, w10
	b.eq	68f
	asr	w10, w10, #16
	sub	w11, w2, #1
	cmp	w10, w2
	csel	w10, w11, w10, ge
	asr	w9, w9, #16
	cmp	w9, #0
	csel	w9, wzr, w9, lt
	cmp	w9, w10
	b.ge	68f
	sub	w12, w10, w9
	madd	w11, w9, w28, w27
	add	x13, x22, w9, sxtw #2
	\fill
68:
	add	w27, w27, w8
	add	x22, x22, x6
	subs	\cnt, \cnt, #1
	b.ne	65b
69:
.endm

.macro SETUP_OPAQUE
.endm

/* The alpha lane's constants, live across both trapezoid walks. */
.macro SETUP_ALPHA
	ldr	w11, [x4, #R_A]
	dup	v7.8h, w11			/* alpha x8                          */
	/* w26 (dy_BC) is dead once the BC slope is taken; w14 (y2) is not. */
	mov	w26, #255
	sub	w26, w26, w11
	dup	v2.8h, w26			/* 255 - alpha, x8                   */
	mvni	v3.4s, #0xFF, lsl #24		/* 0x00FFFFFF x4                     */
.endm

.macro GOURAUDBODY p, setup, fill
\p\()face:
	ldp	w9, w10, [x4, #R_X]
	ldr	w11, [x4, #R_X + 8]
	ldp	w12, w13, [x4, #R_Y]
	ldr	w14, [x4, #R_Y + 8]

	cmp	w12, w3
	b.gt	\p\()next
	tbnz	w14, #31, \p\()next

	sub	w19, w14, w12			/* dy_AC                             */
	cbz	w19, \p\()next
	sub	w20, w11, w9			/* dx_AC                             */
	sub	w21, w10, w9			/* dx_AB                             */
	sub	w23, w13, w12			/* dy_AB                             */

	/* sarea = dx_AB*dy_AC - dx_AC*dy_AB */
	mul	w24, w21, w19
	msub	w24, w20, w23, w24
	cbz	w24, \p\()next

	/* Both colour gradients divide by sarea: one reciprocal, two multiplies,
	 * in double, exactly as gouraudhsllightness_barycentric_steps.h. The
	 * `lsl #8` wraps by design, as the C's toridraw_wrap_shl does. */
	ldp	w25, w26, [x4, #R_C]		/* c0, c1                            */
	ldr	w27, [x4, #R_C + 8]		/* c2                                */
	scvtf	d0, w24
	fmov	d1, #1.0
	fdiv	d1, d1, d0			/* 1.0 / sarea                       */
	sub	w26, w26, w25			/* d_hsl_AB                          */
	sub	w27, w27, w25			/* d_hsl_AC                          */
	mul	w28, w26, w19			/* d_hsl_AB * dy_AC                  */
	msub	w28, w27, w23, w28		/* - d_hsl_AC * dy_AB                */
	lsl	w28, w28, #8
	scvtf	d2, w28
	fmul	d2, d2, d1
	fcvtzs	w28, d2				/* step_x_hsl_ish8                   */
	mul	w8, w27, w21			/* d_hsl_AC * dx_AB                  */
	msub	w8, w26, w20, w8		/* - d_hsl_AB * dx_AC                */
	lsl	w8, w8, #8
	scvtf	d2, w8
	fmul	d2, d2, d1
	fcvtzs	w8, d2				/* step_y_hsl_ish8                   */

	/* The three edge slopes, all three at once, off one packed reciprocal.
	 * See edge_slopes_aarch64.inc. AFTER the colour steps, whose double
	 * reciprocal lives in d1 -- the ladder scratches v19-v23 only. */
	sub	w26, w14, w13			/* dy_BC                             */
	sub	w27, w11, w10			/* dx_BC                             */
	EDGESLOPES3 w20, w21, w27, w19, w23, w26, w15, w16, w17, w11

	/* hsl = (step_x + (color0 << 8)) - x0*step_x, all modular */
	lsl	w27, w25, #8
	add	w27, w27, w28
	msub	w27, w9, w28, w27

	\setup

	/* starting edges */
	lsl	w19, w9, #16
	mov	w20, w19
	lsl	w21, w10, #16

	/* ---- vertical clipping ---- */
	tbz	w12, #31, \p\()y0_ok
	msub	w19, w15, w12, w19
	cmp	w13, #0
	b.le	\p\()no_ab_prestep
	msub	w20, w16, w12, w20
\p\()no_ab_prestep:
	msub	w27, w8, w12, w27		/* hsl -= step_y * y0                */
	mov	w12, #0
\p\()y0_ok:
	tbz	w13, #31, \p\()y1_ok
	msub	w21, w17, w13, w21
	mov	w13, #0
\p\()y1_ok:
	mul	w9, w12, w1
	add	x22, x0, w9, sxtw #2		/* row pointer                       */

	cmp	w13, w3
	b.le	\p\()y1_in
	mov	w13, w3
	mov	w14, w3
	b	\p\()clamped
\p\()y1_in:
	cmp	w14, w3
	csel	w14, w3, w14, gt
\p\()clamped:

	sub	w23, w13, w12
	cmp	w23, #0
	csel	w23, wzr, w23, lt
	sub	w24, w14, w13
	cmp	w24, #0
	csel	w24, wzr, w24, lt

	cmp	w12, w13
	b.ne	\p\()flat_no
	cmp	w15, w17
	b.le	\p\()branchA
	b	\p\()branchB
\p\()flat_no:
	cmp	w15, w16
	b.ge	\p\()branchA
	b	\p\()branchB

/* ---- AB/BC on the left, AC on the right ---- */
\p\()branchA:
	NOHCLIP w20, w16, w19, w15, w23, w25
	/* BC is not advanced during the first trapezoid; AC is. w11, not one
	 * of NOHCLIP's own scratch registers. */
	madd	w11, w23, w15, w19
	NOHCLIP w21, w17, w11, w15, w24, w26
	SEGMENT w20, w19, w16, w15, w25, w23, \fill
	SEGMENT w21, w19, w17, w15, w26, w24, \fill
	b	\p\()next

/* ---- AC on the left, AB/BC on the right ---- */
\p\()branchB:
	NOHCLIP w19, w15, w20, w16, w23, w25
	madd	w11, w23, w15, w19
	NOHCLIP w11, w15, w21, w17, w24, w26
	SEGMENT w19, w20, w15, w16, w25, w23, \fill
	SEGMENT w19, w21, w15, w17, w26, w24, \fill

\p\()next:
	add	x4, x4, #ROWBYTES
	subs	w5, w5, #1
	b.ne	\p\()face
.endm

.macro ENTER
	stp	x19, x20, [sp, #-80]!
	stp	x21, x22, [sp, #16]
	stp	x23, x24, [sp, #32]
	stp	x25, x26, [sp, #48]
	stp	x27, x28, [sp, #64]
	sxtw	x6, w1
	lsl	x6, x6, #2
#if defined(__APPLE__)
	adrp	x7, _g_hsl16_to_rgb_table@GOTPAGE
	ldr	x7, [x7, _g_hsl16_to_rgb_table@GOTPAGEOFF]
#else
	adrp	x7, :got:g_hsl16_to_rgb_table
	ldr	x7, [x7, :got_lo12:g_hsl16_to_rgb_table]
#endif
	EDGECONST
.endm

.macro LEAVE
	ldp	x27, x28, [sp, #64]
	ldp	x25, x26, [sp, #48]
	ldp	x23, x24, [sp, #32]
	ldp	x21, x22, [sp, #16]
	ldp	x19, x20, [sp], #80
	ret
.endm

/*
 * void toridraw_gouraud_opaque_s4_presorted_run_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 */
	.globl	SYM(toridraw_gouraud_opaque_s4_presorted_run_asm)
SYM(toridraw_gouraud_opaque_s4_presorted_run_asm):
	cmp	w5, #0
	b.le	Lo_ret
	ENTER
	GOURAUDBODY Lo_, SETUP_OPAQUE, SPANFILL
	LEAVE
Lo_ret:
	ret

/*
 * void toridraw_gouraud_alpha_s4_presorted_run_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * The blending twin, opacity per row in lane 11; a run may mix them.
 */
	.globl	SYM(toridraw_gouraud_alpha_s4_presorted_run_asm)
SYM(toridraw_gouraud_alpha_s4_presorted_run_asm):
	cmp	w5, #0
	b.le	La_ret
	ENTER
	GOURAUDBODY La_, SETUP_ALPHA, SPANFILL_ALPHA
	LEAVE
La_ret:
	ret

#endif /* __aarch64__ */
