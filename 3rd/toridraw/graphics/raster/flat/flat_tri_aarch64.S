/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES   (i686 only)
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The flat triangle, batched, in handrolled AArch64 / Advanced SIMD -- the
 * NEON twin of flat_tri_i686.S. Only the two RUN doors exist on this lane:
 * the depth sort orders every batched row on the way past, and the per-face
 * path keeps the C, which is the arm the A/B baseline measures.
 *
 * WHAT IS DIFFERENT FROM THE i686 KERNEL, AND WHY
 *
 *   - The whole triangle lives in registers. AArch64 has thirty-one, so the
 *     three slopes, the three edges, the row pointer, the two segment counts
 *     and the two no-clip proofs all stay put across both trapezoid walks;
 *     nothing is spilled to the frame between the prologue and the last row.
 *   - The three edge slopes are three exact `sdiv`s. The i686 kernel took a
 *     packed rcpps because its integer divider is not pipelined; this one is,
 *     and an exact divide is what makes the kernel bit-identical to the C
 *     reference instead of merely close. sdiv x/0 is 0 on AArch64, which is
 *     exactly the C's "else 0" arm for the AB and BC edges, so those two
 *     conditionals disappear into the instruction.
 *   - The opaque span is unaligned 16-byte stores throughout, closed with one
 *     overlapping store at the far end instead of a scalar tail: the store is
 *     idempotent, and a modern core splits nothing on a misaligned q store.
 *     Under four pixels it is the same three-store branchless tail the
 *     gouraud fill uses.
 *   - The alpha span is the same lane arithmetic as the SSE2 kernel -- widen
 *     to u16, multiply by (255 - a), shift, narrow, clear the alpha byte, add
 *     the folded source term with a full 32-bit add -- and for the same
 *     reason: alpha_blend() in graphics/alpha.h sums its four terms with a
 *     plain `+`, carries and all, and reproducing that carry is not optional.
 *
 * EXACTNESS. Bit-identical to raster_flat_screen_opaque_branching_s4() and
 * raster_flat_screen_alpha_branching_s4(), framebuffer against framebuffer,
 * including degenerate, flat-topped, sliver and fully-offscreen triangles
 * (toridraw_presorted_neon_test.c).
 *
 * ROW LAYOUT. `rows` is `count` records of TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS
 * ints: x0,x1,x2,pad / y0,y1,y2,pad / color,alpha,pad,pad -- the screen
 * coordinates with the viewport offset already folded in, already in y order.
 * The same 48-byte record the i686 kernel reads.
 *
 * AAPCS64. x19-x28 are saved; no v8-v15 are touched; no call is made.
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
#define R_A   36
#define ROWBYTES 48

	.text
	.p2align 2

/*
 * Register map, once the prologue has run:
 *
 *   x0  pixel_buffer          w1  stride (pixels)      w2 screen_width
 *   w3  screen_height         x4  current row          w5 rows left
 *   x6  stride * 4 (bytes)    x7  g_hsl16_to_rgb_table
 *   w15 sxAC  w16 sxAB  w17 sxBC        (16.16 edge steps)
 *   w19 eAC   w20 eAB   w21 eBC         (16.16 edge x)
 *   x22 pointer to the current row of the frame buffer
 *   w23 n1    w24 n2    w25 nc1  w26 nc2
 *   w27 rgb (opaque) / palette entry (alpha)
 *   v0  rgb x4 (opaque)
 *   v1  K x4, v2 (255-a) x8 u16, v3 0x00FFFFFF x4 (alpha)
 *   w9-w14, x13, w28: scratch
 */

/*
 * flat_screen_fixed_edges_no_hclip, once per segment: affine extrema on a
 * segment occur at its endpoints, so two evaluations settle every row of it.
 * \dst <- 1 when no row of the segment can leave [0, screen_width).
 * Clobbers w9, w10, w14.
 */
.macro NOHCLIP ea0, sa, eb0, sb, n, dst
	cmp	\n, #0
	b.le	81f
	sub	w14, \n, #1
	madd	w9, \sa, w14, \ea0		/* e_a_last                          */
	cmp	\ea0, w9
	csel	w9, \ea0, w9, lt		/* min_e_a                           */
	tbnz	w9, #31, 82f			/* min_e_a < 0 -> clipping possible  */
	madd	w10, \sb, w14, \eb0		/* e_b_last                          */
	cmp	\eb0, w10
	csel	w10, \eb0, w10, gt		/* max_e_b                           */
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
 * The opaque span. x13 destination, w12 pixel count (>= 1).
 * Clobbers x13, w14, x14.
 */
.macro FILLOPAQUE
	cmp	w12, #4
	b.lt	71f
	add	x14, x13, w12, uxtw #2
	sub	x14, x14, #16			/* address of the last 16 bytes      */
70:
	str	q0, [x13], #16
	cmp	x13, x14
	b.lo	70b
	str	q0, [x14]			/* overlapping close; idempotent     */
	b	73f
71:
	/* 1..3 pixels: three stores, two conditional indices, no jump table. */
	str	w27, [x13]
	cmp	w12, #1
	cset	w14, gt
	str	w27, [x13, w14, uxtw #2]
	cmp	w12, #2
	cset	w14, gt
	lsl	w14, w14, #1			/* i2 = count > 2 ? 2 : 0            */
	str	w27, [x13, w14, uxtw #2]
73:
.endm

/*
 * The alpha span. x13 destination, w12 pixel count (>= 1).
 * v1 = K x4, v2 = (255 - a) in eight u16 lanes, v3 = 0x00FFFFFF x4.
 *
 *   out = K + per-channel((dst_channel * a_inv) >> 8)
 *
 * The 1..3 tail runs the identical lane sequence one pixel at a time: a
 * blend is not idempotent, so the opaque span's overlapping close is not
 * available here, and a second scalar transcription of the arithmetic would
 * be a second thing that can drift.
 * Clobbers x13, w12, w14, v4-v6.
 */
.macro FILLALPHA
	cmp	w12, #4
	b.lt	75f
	lsr	w14, w12, #2
	and	w12, w12, #3
74:
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
	subs	w14, w14, #1
	b.ne	74b
	cbz	w12, 76f
75:
	ldr	s4, [x13]
	uxtl	v5.8h, v4.8b
	mul	v5.4h, v5.4h, v2.4h
	ushr	v5.4h, v5.4h, #8
	xtn	v4.8b, v5.8h
	and	v4.8b, v4.8b, v3.8b
	add	v4.2s, v4.2s, v1.2s
	str	s4, [x13], #4
	subs	w12, w12, #1
	b.ne	75b
76:
.endm

/*
 * One trapezoid segment. \el / \er are the live left and right edge
 * registers (stepped in place, so the second segment sees the first's
 * final AC), \sl / \sr their steps, \nc the no-clip proof, \cnt the row
 * count (consumed). The horizontal clamps are hoisted out: the common case
 * runs a row loop with no clamping in it at all.
 * Clobbers w9-w14, x13, x14 and whatever \fill clobbers.
 */
.macro SEGMENT el, er, sl, sr, nc, cnt, fill
	cmp	\cnt, #0
	b.le	69f
	cbz	\nc, 65f

	/* ---- proved inside the viewport: no left/right clamp ---- */
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
	sub	w12, w10, w9			/* pixel count                       */
	add	x13, x22, w9, sxtw #2
	\fill
64:
	add	x22, x22, x6
	subs	\cnt, \cnt, #1
	b.ne	60b
	b	69f

	/* ---- may leave the viewport: clamp both ends ---- */
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
	csel	w10, w11, w10, ge		/* x_end = screen_width - 1          */
	asr	w9, w9, #16
	cmp	w9, #0
	csel	w9, wzr, w9, lt			/* x_start = 0                       */
	cmp	w9, w10
	b.ge	68f
	sub	w12, w10, w9
	add	x13, x22, w9, sxtw #2
	\fill
68:
	add	x22, x22, x6
	subs	\cnt, \cnt, #1
	b.ne	65b
69:
.endm

/* Opaque per-triangle colour: one palette lookup, splatted for the store. */
.macro SETUP_OPAQUE
	ldr	w27, [x4, #R_C]
	ldr	w27, [x7, w27, uxtw #2]
	dup	v0.4s, w27
.endm

/*
 * Alpha per-triangle constants: the source colour's whole contribution,
 *
 *     K = (((o & 0xFF00FF) * a) >> 8) & 0xFF00FF
 *       + (((o & 0xFF00)   * a) >> 8) & 0xFF00
 *
 * computed per channel in u16 lanes (the widest product, 255*255, fits), then
 * (255 - a) broadcast for the destination term, and the alpha-byte mask.
 */
.macro SETUP_ALPHA
	ldr	w27, [x4, #R_C]
	ldr	w27, [x7, w27, uxtw #2]		/* o                                 */
	ldr	w28, [x4, #R_A]			/* a                                 */
	fmov	s1, w27
	dup	v4.8h, w28
	uxtl	v1.8h, v1.8b
	mul	v1.4h, v1.4h, v4.4h
	ushr	v1.4h, v1.4h, #8
	xtn	v1.8b, v1.8h
	mvni	v3.4s, #0xFF, lsl #24		/* 0x00FFFFFF x4                     */
	and	v1.8b, v1.8b, v3.8b
	dup	v1.4s, v1.s[0]			/* K x4                              */
	/* w11 (x2) is dead once the BC slope is taken; w14 (y2) is not. */
	mov	w11, #255
	sub	w11, w11, w28
	dup	v2.8h, w11			/* a_inv x8                          */
.endm

/*
 * The run body. \p prefixes the labels, \setup the per-triangle colour
 * preparation, \fill the span. Vertices arrive already in y order.
 */
.macro FLATBODY p, setup, fill
\p\()face:
	ldp	w9, w10, [x4, #R_X]
	ldr	w11, [x4, #R_X + 8]
	ldp	w12, w13, [x4, #R_Y]
	ldr	w14, [x4, #R_Y + 8]

	/* Off the bottom or off the top: two well-predicted compares. */
	cmp	w12, w3
	b.gt	\p\()next
	tbnz	w14, #31, \p\()next

	sub	w19, w14, w12			/* dy_AC                             */
	cbz	w19, \p\()next
	sub	w20, w11, w9			/* dx_AC                             */
	sub	w21, w10, w9			/* dx_AB                             */
	sub	w23, w13, w12			/* dy_AB                             */

	/* sarea = dx_AB*dy_AC - dx_AC*dy_AB, only for the degenerate test. */
	mul	w24, w21, w19
	msub	w24, w20, w23, w24
	cbz	w24, \p\()next

	/* The three edge slopes, exact. sdiv by zero is zero on this
	 * architecture, which is the C's "else 0" for AB and BC. */
	sub	w25, w14, w13			/* dy_BC                             */
	sub	w26, w11, w10			/* dx_BC                             */
	lsl	w15, w20, #16
	sdiv	w15, w15, w19
	lsl	w16, w21, #16
	sdiv	w16, w16, w23
	lsl	w17, w26, #16
	sdiv	w17, w17, w25

	\setup

	/* starting edges */
	lsl	w19, w9, #16
	mov	w20, w19
	lsl	w21, w10, #16

	/* ---- vertical clipping ---- */
	tbz	w12, #31, \p\()y0_ok
	msub	w19, w15, w12, w19		/* eAC -= sxAC * y0                  */
	/* Only pre-step A->B if that edge is actually walked. */
	cmp	w13, #0
	b.le	\p\()no_ab_prestep
	msub	w20, w16, w12, w20
\p\()no_ab_prestep:
	mov	w12, #0
\p\()y0_ok:
	tbz	w13, #31, \p\()y1_ok
	msub	w21, w17, w13, w21		/* eBC -= sxBC * y1                  */
	mov	w13, #0
\p\()y1_ok:
	mul	w28, w12, w1
	add	x22, x0, w28, sxtw #2		/* row pointer                       */

	cmp	w13, w3
	b.le	\p\()y1_in
	mov	w13, w3
	mov	w14, w3
	b	\p\()clamped
\p\()y1_in:
	cmp	w14, w3
	csel	w14, w3, w14, gt
\p\()clamped:

	/* segment row counts */
	sub	w23, w13, w12
	cmp	w23, #0
	csel	w23, wzr, w23, lt
	sub	w24, w14, w13
	cmp	w24, #0
	csel	w24, wzr, w24, lt

	/* Which edge is on the left for this triangle. */
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
	/* BC is not advanced during the first trapezoid; AC is. */
	madd	w28, w23, w15, w19
	NOHCLIP w21, w17, w28, w15, w24, w26
	SEGMENT w20, w19, w16, w15, w25, w23, \fill
	SEGMENT w21, w19, w17, w15, w26, w24, \fill
	b	\p\()next

/* ---- AC on the left, AB/BC on the right ---- */
\p\()branchB:
	NOHCLIP w19, w15, w20, w16, w23, w25
	madd	w28, w23, w15, w19
	NOHCLIP w28, w15, w21, w17, w24, w26
	SEGMENT w19, w20, w15, w16, w25, w23, \fill
	SEGMENT w19, w21, w15, w17, w26, w24, \fill

\p\()next:
	add	x4, x4, #ROWBYTES
	subs	w5, w5, #1
	b.ne	\p\()face
.endm

/* Save, and take the two loop-invariant constants once for the run. */
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
 * void toridraw_flat_opaque_s4_presorted_run_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 */
	.globl	SYM(toridraw_flat_opaque_s4_presorted_run_asm)
SYM(toridraw_flat_opaque_s4_presorted_run_asm):
	cmp	w5, #0
	b.le	Lo_ret
	ENTER
	FLATBODY Lo_, SETUP_OPAQUE, FILLOPAQUE
	LEAVE
Lo_ret:
	ret

/*
 * void toridraw_flat_alpha_s4_presorted_run_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * As above, blending instead of storing. The per-row alpha is in the staged
 * row, so a run may mix opacities.
 */
	.globl	SYM(toridraw_flat_alpha_s4_presorted_run_asm)
SYM(toridraw_flat_alpha_s4_presorted_run_asm):
	cmp	w5, #0
	b.le	La_ret
	ENTER
	FLATBODY La_, SETUP_ALPHA, FILLALPHA
	LEAVE
La_ret:
	ret

#endif /* __aarch64__ */
