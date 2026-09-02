/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES   (i686 only)
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The flat triangle, batched, in handrolled A32 / Advanced SIMD -- the armv7
 * twin of tri.flat.aarch64.S. Only the two RUN doors exist here, as on the
 * AArch64 lane: the depth sort orders every batched row on the way past, and
 * the per-face path keeps the C, which is the arm the A/B baseline measures.
 *
 * WHAT IS DIFFERENT FROM THE AArch64 KERNEL, AND WHY
 *
 * The arithmetic is the same arithmetic. The shape is not, and one fact drives
 * every difference: A32 has fourteen usable general-purpose registers where
 * AArch64 has thirty-one.
 *
 *   - THE TRIANGLE DOES NOT FIT IN REGISTERS. The AArch64 kernel keeps the
 *     three slopes, the three edges, the row pointer, the two segment counts
 *     and the two no-clip proofs live across both trapezoid walks and spills
 *     nothing. Here they live in an explicit frame (F_* below) and the segment
 *     loads the six values it actually steps into a fixed register set on the
 *     way in. That is a per-SEGMENT cost -- eight loads, entered twice per
 *     triangle -- not a per-row one: the row loop itself touches no memory but
 *     the framebuffer, exactly as the AArch64 loop does. Spilling per row
 *     instead would have cost more than the whole kernel saves.
 *
 *   - THE SEGMENT IS REGISTER-NORMALISED. AArch64 passes a different register
 *     pair per branch (branchA walks AB then BC against AC; branchB walks AC
 *     against AB then BC) because it has the registers to name them in place.
 *     Here every segment runs on r0/r1 (edges) and r2/r3 (steps) whatever it is
 *     walking, and the macro takes FRAME OFFSETS rather than registers. The AC
 *     edge is stepped by the first segment and read by the second, so the
 *     updated edges are written back -- that store is load-bearing, not
 *     bookkeeping.
 *
 *   - THE EDGE LADDER NEEDS NO SCRATCH REGISTER. See the register note in
 *     edge_slopes_aarch32.inc: the A32 macro plants the divisor's unused lane
 *     with vmov.i32 rather than through an ARM register, which on this lane is
 *     the difference between the caller keeping its accumulators and spilling.
 *
 *   - d8-d15 ARE NEVER TOUCHED. They are the callee-saved vector registers
 *     under AAPCS; avoiding them outright is cheaper than a vpush/vpop pair
 *     around a run. q0-q3 hold the per-triangle colour constants, q8-q11 and
 *     q14-q15 are scratch, q12-q13 the edge-ladder constants for the run.
 *
 *   - THE PALETTE IS REACHED THROUGH THE GOT, PC-relative (GOT_PREL), which is
 *     the ARM32 equivalent of the AArch64 file's `adrp :got:`. It has to be:
 *     this assembles into a PIC shared object, and a movw/movt absolute pair
 *     would be a text relocation the Android loader rejects.
 *
 * EXACTNESS. Scored against raster_flat_screen_opaque_branching_s4() and
 * raster_flat_screen_alpha_branching_s4() framebuffer against framebuffer,
 * including degenerate, flat-topped, sliver and fully-offscreen triangles
 * (toridraw_presorted_neon_test.c), on the same triangle and pixel budget the
 * AArch64 and i686 kernels are held to. The walk and both fills are
 * bit-identical: assembled with -DTORIDRAW_EDGE_IDIV (the exact edge lane) the
 * test demands every pixel identical, and gets it.
 *
 * It is NOT bit-identical to the AArch64 kernel's default arm and cannot be --
 * A32's vrecps and A64's frecps disagree in the last place. It is exactly as
 * ACCURATE, which is the standard that lane is itself held to; the measurement
 * is in edge_slopes_aarch32.inc.
 *
 * ROW LAYOUT. `rows` is `count` records of TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS
 * ints: x0,x1,x2,pad / y0,y1,y2,pad / color,alpha,pad,pad -- the screen
 * coordinates with the viewport offset already folded in, already in y order.
 * The same 48-byte record the i686 and AArch64 kernels read.
 *
 * AAPCS. r4-r11 and lr are saved; d8-d15 are untouched; no call is made.
 */

#if defined(__arm__) && !defined(__aarch64__) && ( defined(__ARM_NEON) || defined(__ARM_NEON__) )

#define SYM(name) name

#define R_X   0
#define R_Y   16
#define R_C   32
#define R_A   36
#define ROWBYTES 48

/*
 * The frame. Everything the triangle needs that will not fit in fourteen
 * registers. The first eight are per-RUN and written once by ENTER; the rest
 * are per-triangle.
 */
#define F_PIXBUF    0	/* toripixel_t* pixel_buffer                         */
#define F_STRIDE    4	/* stride, in pixels                                 */
#define F_SCRW      8	/* screen_width                                      */
#define F_SCRH     12	/* screen_height                                     */
#define F_STRIDEB  16	/* stride * 4, in bytes -- the row advance           */
#define F_PAL      20	/* g_hsl16_to_pixel_table                            */
#define F_ROWS     24	/* the current row record                            */
#define F_LEFT     28	/* rows left in the run                              */
#define F_SAC      32	/* 16.16 edge steps                                  */
#define F_SAB      36
#define F_SBC      40
#define F_EAC      44	/* 16.16 edge x, stepped in place by SEGMENT         */
#define F_EAB      48
#define F_EBC      52
#define F_N1       56	/* rows in the first / second trapezoid              */
#define F_N2       60
#define F_NC1      64	/* the two no-clip proofs                            */
#define F_NC2      68
#define F_ROWPTR   72	/* framebuffer row pointer                           */
#define F_STAGE    80	/* the edge ladder's input staging area, 32 bytes    */
#define FRAMESZ   112	/* 8-byte aligned, as AAPCS requires at a call       */

/* push {r4-r11, lr} is nine words, so the two stack arguments sit above the
 * frame AND above the saved registers. */
#define A_ROWS   (FRAMESZ + 36)
#define A_COUNT  (FRAMESZ + 40)

#include "graphics/raster/edge_slopes_aarch32.inc"

	.syntax unified
	.arm
	.fpu neon
	.text
	.p2align 2

/*
 * Register map inside a segment, which is the only place it is tight:
 *
 *   r0 el (left edge)     r1 er (right edge)    r2 sl      r3 sr
 *   r4 rows in segment    r5 framebuffer row    r6 stride bytes
 *   r7 screen_width       r8 colour (opaque) / free (alpha)
 *   r9, r10, r11, r12, lr scratch
 *   q0 rgb x4 (opaque);  q1 K x4, d4 (255-a) x8 u8, q3 0x00FFFFFF x4 (alpha)
 *   q8-q11, q14, q15 scratch;  q12, q13 the edge-ladder constants
 */

/*
 * flat_screen_fixed_edges_no_hclip, once per segment: affine extrema on a
 * segment occur at its endpoints, so two evaluations settle every row of it.
 * \dst <- 1 when no row of the segment can leave [0, screen_width).
 */
.macro NOHCLIP ea0, sa, eb0, sb, n, dst, t0, t1
	cmp	\n, #0
	ble	81f
	sub	\t0, \n, #1
	mla	\t1, \sa, \t0, \ea0		/* e_a_last                          */
	cmp	\ea0, \t1
	movlt	\t1, \ea0			/* min_e_a                           */
	cmp	\t1, #0
	blt	82f				/* min_e_a < 0 -> clipping possible  */
	mla	\t1, \sb, \t0, \eb0		/* e_b_last                          */
	cmp	\eb0, \t1
	movgt	\t1, \eb0			/* max_e_b                           */
	ldr	\t0, [sp, #F_SCRW]
	cmp	\t1, \t0, lsl #16
	bge	82f
81:
	mov	\dst, #1
	b	83f
82:
	mov	\dst, #0
83:
.endm

/*
 * The opaque span. r12 destination, r11 pixel count (>= 1), r8 the colour,
 * q0 the colour splatted. Unaligned 16-byte stores throughout, closed with one
 * overlapping store at the far end instead of a scalar tail: the store is
 * idempotent. Under four pixels it is the same three-store branchless tail the
 * AArch64 kernel uses.
 * Clobbers r9, r12, lr.
 */
.macro FILLOPAQUE
	cmp	r11, #4
	blt	71f
	add	lr, r12, r11, lsl #2
	sub	lr, lr, #16			/* address of the last 16 bytes      */
#ifdef TORIDRAW_FLAT_FILL_V1
70:
	vst1.32	{d0, d1}, [r12]!
	cmp	r12, lr
	blo	70b
	vst1.32	{d0, d1}, [lr]			/* overlapping close; idempotent     */
	b	73f
#else
	cmp	r11, #8
	blt	72f
	/* Eight or more: one unaligned store, the cursor rounded up to the next
	 * 16-byte boundary (which that store has already covered), then
	 * ALIGNED stores two at a time while a whole pair still fits, and two
	 * overlapping closes at end-32 and end-16 -- between them they cover
	 * whatever the pair loop left, which is under 32 bytes, and the store
	 * is idempotent so overlap costs nothing. Nothing is written past the
	 * span: the pair loop only runs while its 32 bytes fit. */
	vst1.32	{d0, d1}, [r12]
	add	r12, r12, #16
	bic	r12, r12, #15
	sub	r9, lr, #16			/* end - 32: the pair loop's stop    */
	cmp	r12, r9
	bhs	74f
70:
	vst1.32	{d0, d1}, [r12:128]!
	vst1.32	{d0, d1}, [r12:128]!
	cmp	r12, r9
	blo	70b
74:
	vst1.32	{d0, d1}, [r9]
	vst1.32	{d0, d1}, [lr]
	b	73f
72:
	/* 4..7: the first 16 bytes and the last 16, overlapping. */
	vst1.32	{d0, d1}, [r12]
	vst1.32	{d0, d1}, [lr]
	b	73f
#endif
71:
	/* 1..3 pixels: three stores, two conditional indices, no jump table. */
	str	r8, [r12]
	cmp	r11, #1
	movgt	r9, #1
	movle	r9, #0
	str	r8, [r12, r9, lsl #2]
	cmp	r11, #2
	movgt	r9, #2
	movle	r9, #0
	str	r8, [r12, r9, lsl #2]
73:
.endm

/*
 * The alpha span. r12 destination, r11 pixel count (>= 1).
 * q1 = K x4, d4 = (255 - a) in eight u8 lanes, q3 = 0x00FFFFFF x4.
 *
 *   out = K + per-channel((dst_channel * a_inv) >> 8)
 *
 * The same lane arithmetic as the SSE2 and AArch64 kernels, and for the same
 * reason: alpha_blend() in graphics/alpha.h sums its four terms with a plain
 * `+`, carries and all, and reproducing that carry is not optional -- which is
 * why the add at the end is a full 32-bit add after the alpha byte is cleared.
 *
 * THE PRODUCT IS A WIDENING MULTIPLY AND THE SHIFT A NARROWING ONE. The
 * AArch64 file widens (uxtl), multiplies, shifts and narrows (xtn): four
 * instructions per four pixels. VMULL.U8 does the widen and the multiply in
 * one and VSHRN.U16 #8 the shift and the narrow in one, with the identical
 * result: every product is at most 255 * 255 and fits its u16 lane, and the
 * high byte VSHRN keeps is the byte the shift-then-narrow kept. Same bits,
 * half the instructions, and the exact arm of the test says so.
 *
 * Spans of eight or more go two blocks at a time -- one four-register load,
 * eight products in flight, one store -- which is what feeds an in-order
 * NEON pipe: the loop overhead halves and a load has two blocks of work to
 * hide behind.
 *
 * The 1..3 tail runs the identical lane sequence one pixel at a time: a blend
 * is not idempotent, so the opaque span's overlapping close is not available,
 * and a second scalar transcription of the arithmetic would be a second thing
 * that can drift.
 * Clobbers r11, r12, lr, q8-q11, q14, q15.
 */
.macro FILLALPHA
	cmp	r11, #8
	blt	73f
	lsr	lr, r11, #3
	and	r11, r11, #7
72:
	vld1.32	{d16-d19}, [r12]
	vmull.u8	q10, d16, d4
	vmull.u8	q11, d17, d4
	vmull.u8	q14, d18, d4
	vmull.u8	q15, d19, d4
	vshrn.u16	d16, q10, #8
	vshrn.u16	d17, q11, #8
	vshrn.u16	d18, q14, #8
	vshrn.u16	d19, q15, #8
	vand	q8, q8, q3
	vand	q9, q9, q3
	vadd.i32	q8, q8, q1
	vadd.i32	q9, q9, q1
	vst1.32	{d16-d19}, [r12]!
	subs	lr, lr, #1
	bne	72b
73:
	cmp	r11, #4
	blt	75f
	vld1.32	{d16, d17}, [r12]
	vmull.u8	q9, d16, d4
	vmull.u8	q10, d17, d4
	vshrn.u16	d16, q9, #8
	vshrn.u16	d17, q10, #8
	vand	q8, q8, q3
	vadd.i32	q8, q8, q1
	vst1.32	{d16, d17}, [r12]!
	subs	r11, r11, #4
	beq	76f
75:
	cmp	r11, #0
	beq	76f
74:
	vld1.32	{d16[0]}, [r12]
	vmull.u8	q9, d16, d4
	vshrn.u16	d16, q9, #8
	vand	d16, d16, d6
	vadd.i32	d16, d16, d2
	vst1.32	{d16[0]}, [r12]!
	subs	r11, r11, #1
	bne	74b
76:
.endm

/*
 * One trapezoid segment. Unlike the AArch64 macro this takes FRAME OFFSETS,
 * not registers -- see the header. \off_el / \off_er are the left and right
 * edges (stepped in place and written back, so the second segment sees the
 * first's final AC), \off_sl / \off_sr their steps, \off_nc the no-clip proof,
 * \off_cnt the row count. The horizontal clamps are hoisted out: the common
 * case runs a row loop with no clamping in it at all.
 */
.macro SEGMENT off_el, off_er, off_sl, off_sr, off_nc, off_cnt, fill
	ldr	r4, [sp, #\off_cnt]
	cmp	r4, #0
	ble	69f
	ldr	r0, [sp, #\off_el]
	ldr	r1, [sp, #\off_er]
	ldr	r2, [sp, #\off_sl]
	ldr	r3, [sp, #\off_sr]
	ldr	r5, [sp, #F_ROWPTR]
	ldr	r6, [sp, #F_STRIDEB]
	ldr	r7, [sp, #F_SCRW]
	ldr	r12, [sp, #\off_nc]
	cmp	r12, #0
	beq	65f

	/* ---- proved inside the viewport: no left/right clamp ---- */
	.p2align 4
60:
	mov	r9, r0
	mov	r10, r1
	add	r0, r0, r2
	add	r1, r1, r3
	cmp	r9, r10
	beq	64f
	asr	r9, r9, #16
	asr	r10, r10, #16
	cmp	r9, r10
	bge	64f
	sub	r11, r10, r9			/* pixel count                       */
	add	r12, r5, r9, lsl #2
	\fill
64:
	add	r5, r5, r6
	subs	r4, r4, #1
	bne	60b
	b	68f

	/* ---- may leave the viewport: clamp both ends ---- */
	.p2align 4
65:
	mov	r9, r0
	mov	r10, r1
	add	r0, r0, r2
	add	r1, r1, r3
	cmp	r9, r10
	beq	67f
	asr	r10, r10, #16
	cmp	r10, r7
	subge	r10, r7, #1			/* x_end = screen_width - 1          */
	asr	r9, r9, #16
	cmp	r9, #0
	movlt	r9, #0				/* x_start = 0                       */
	cmp	r9, r10
	bge	67f
	sub	r11, r10, r9
	add	r12, r5, r9, lsl #2
	\fill
67:
	add	r5, r5, r6
	subs	r4, r4, #1
	bne	65b
68:
	str	r0, [sp, #\off_el]
	str	r1, [sp, #\off_er]
	str	r5, [sp, #F_ROWPTR]
69:
.endm

/*
 * Opaque per-triangle colour: one palette lookup, splatted for the store.
 * Reads the row record through r11 and leaves the colour in r8, which is the
 * one register SEGMENT does not touch.
 */
.macro SETUP_OPAQUE
	ldr	r0, [r11, #R_C]
	ldr	r1, [sp, #F_PAL]
	ldr	r8, [r1, r0, lsl #2]
	vdup.32	q0, r8
.endm

/*
 * Alpha per-triangle constants: the source colour's whole contribution,
 *
 *     K = (((o & 0xFF00FF) * a) >> 8) & 0xFF00FF
 *       + (((o & 0xFF00)   * a) >> 8) & 0xFF00
 *
 * computed per channel in u16 lanes (the widest product, 255*255, fits), then
 * (255 - a) broadcast as BYTES for the destination term -- the span's VMULL
 * widens on the way in -- and the alpha-byte mask.
 *
 * Unlike the AArch64 file this never widens or narrows in place: A32's VMOVL
 * and VMOVN do not guarantee an overlapping source and destination register,
 * so the colour travels d16 -> q10 -> d22 rather than round-tripping q1.
 */
.macro SETUP_ALPHA
	vmvn.i32	q3, #0xff000000		/* 0x00FFFFFF x4                     */
	ldr	r0, [r11, #R_C]
	ldr	r1, [sp, #F_PAL]
	ldr	r0, [r1, r0, lsl #2]		/* o                                 */
	ldr	r1, [r11, #R_A]			/* a                                 */
	vmov.32	d16[0], r0
	vdup.16	q9, r1
	vmovl.u8	q10, d16
	vmul.i16	q10, q10, q9
	vshr.u16	q10, q10, #8
	vmovn.i16	d22, q10
	vand	d22, d22, d6
	vdup.32	q1, d22[0]			/* K x4                              */
	rsb	r1, r1, #255
	vdup.8	d4, r1				/* a_inv x8, u8: VMULL widens it     */
.endm

/*
 * The run body. \p prefixes the labels, \setup the per-triangle colour
 * preparation, \fill the span. Vertices arrive already in y order.
 *
 * The ORDER here differs from the AArch64 body for one reason: \setup wants
 * r8, and r8 is holding the AB slope until the vertical clip is done with it.
 * So the colour is prepared after the clip rather than before, and the branch
 * selection below re-reads the slopes from the frame rather than keeping them.
 */
.macro FLATBODY p, setup, fill
\p\()face:
	ldr	r11, [sp, #F_ROWS]
	ldr	r0, [r11, #R_X + 0]		/* x0                                */
	ldr	r1, [r11, #R_X + 4]		/* x1                                */
	ldr	r2, [r11, #R_X + 8]		/* x2                                */
	ldr	r3, [r11, #R_Y + 0]		/* y0                                */
	ldr	r4, [r11, #R_Y + 4]		/* y1                                */
	ldr	r5, [r11, #R_Y + 8]		/* y2                                */

	/* Off the bottom or off the top: two well-predicted compares. */
	ldr	r6, [sp, #F_SCRH]
	cmp	r3, r6
	bgt	\p\()next
	cmp	r5, #0
	blt	\p\()next

	subs	r6, r5, r3			/* dy_AC                             */
	beq	\p\()next
	sub	r7, r2, r0			/* dx_AC                             */
	sub	r8, r1, r0			/* dx_AB                             */
	sub	r9, r4, r3			/* dy_AB                             */

	/* sarea = dx_AB*dy_AC - dx_AC*dy_AB, only for the degenerate test. */
	mul	r10, r8, r6
	mls	r10, r7, r9, r10
	cmp	r10, #0
	beq	\p\()next

	/* The three edge slopes, all three at once, off one packed reciprocal.
	 * See edge_slopes_aarch32.inc. The outputs alias the dx inputs, which
	 * both arms of the macro permit. */
	sub	r10, r5, r4			/* dy_BC                             */
	sub	r12, r2, r1			/* dx_BC                             */
	EDGESLOPES3 r7, r8, r12, r6, r9, r10, r7, r8, r12, lr, F_STAGE, F_SAC

	/* starting edges -- x0 and x1 are dead after this */
	lsl	r6, r0, #16			/* eAC                               */
	mov	r9, r6				/* eAB                               */
	lsl	r10, r1, #16			/* eBC                               */

	/* ---- vertical clipping ---- */
	cmp	r3, #0
	bge	\p\()y0_ok
	mls	r6, r7, r3, r6			/* eAC -= sxAC * y0                  */
	/* Only pre-step A->B if that edge is actually walked. */
	cmp	r4, #0
	ble	\p\()no_ab_prestep
	mls	r9, r8, r3, r9
\p\()no_ab_prestep:
	mov	r3, #0
\p\()y0_ok:
	cmp	r4, #0
	bge	\p\()y1_ok
	mls	r10, r12, r4, r10		/* eBC -= sxBC * y1                  */
	mov	r4, #0
\p\()y1_ok:
	str	r6, [sp, #F_EAC]
	str	r9, [sp, #F_EAB]
	str	r10, [sp, #F_EBC]

	/* The colour, now that the AB slope in r8 is spent. */
	\setup

	/* row pointer, off the clipped y0 */
	ldr	r2, [sp, #F_STRIDE]
	mul	r2, r3, r2
	ldr	r11, [sp, #F_PIXBUF]
	add	r11, r11, r2, lsl #2
	str	r11, [sp, #F_ROWPTR]

	ldr	r2, [sp, #F_SCRH]
	cmp	r4, r2
	ble	\p\()y1_in
	mov	r4, r2
	mov	r5, r2
	b	\p\()clamped
\p\()y1_in:
	cmp	r5, r2
	movgt	r5, r2
\p\()clamped:

	/* segment row counts */
	subs	r2, r4, r3
	movlt	r2, #0
	str	r2, [sp, #F_N1]
	subs	r0, r5, r4
	movlt	r0, #0
	str	r0, [sp, #F_N2]

	/* Which edge is on the left for this triangle. */
	ldr	r0, [sp, #F_SAC]
	cmp	r3, r4
	bne	\p\()flat_no
	ldr	r1, [sp, #F_SBC]
	cmp	r0, r1
	ble	\p\()branchA
	b	\p\()branchB
\p\()flat_no:
	ldr	r1, [sp, #F_SAB]
	cmp	r0, r1
	bge	\p\()branchA
	b	\p\()branchB

/* ---- AB/BC on the left, AC on the right ---- */
\p\()branchA:
	ldr	r0, [sp, #F_EAB]
	ldr	r1, [sp, #F_SAB]
	ldr	r2, [sp, #F_EAC]
	ldr	r3, [sp, #F_SAC]
	ldr	r4, [sp, #F_N1]
	NOHCLIP r0, r1, r2, r3, r4, r5, r9, r10
	str	r5, [sp, #F_NC1]
	/* BC is not advanced during the first trapezoid; AC is. */
	mla	r6, r4, r3, r2
	ldr	r0, [sp, #F_EBC]
	ldr	r1, [sp, #F_SBC]
	ldr	r4, [sp, #F_N2]
	NOHCLIP r0, r1, r6, r3, r4, r5, r9, r10
	str	r5, [sp, #F_NC2]
	SEGMENT F_EAB, F_EAC, F_SAB, F_SAC, F_NC1, F_N1, \fill
	SEGMENT F_EBC, F_EAC, F_SBC, F_SAC, F_NC2, F_N2, \fill
	b	\p\()next

/* ---- AC on the left, AB/BC on the right ---- */
\p\()branchB:
	ldr	r0, [sp, #F_EAC]
	ldr	r1, [sp, #F_SAC]
	ldr	r2, [sp, #F_EAB]
	ldr	r3, [sp, #F_SAB]
	ldr	r4, [sp, #F_N1]
	NOHCLIP r0, r1, r2, r3, r4, r5, r9, r10
	str	r5, [sp, #F_NC1]
	ldr	r3, [sp, #F_SAC]
	mla	r6, r4, r3, r0
	ldr	r0, [sp, #F_EBC]
	ldr	r1, [sp, #F_SBC]
	ldr	r4, [sp, #F_N2]
	NOHCLIP r6, r3, r0, r1, r4, r5, r9, r10
	str	r5, [sp, #F_NC2]
	SEGMENT F_EAC, F_EAB, F_SAC, F_SAB, F_NC1, F_N1, \fill
	SEGMENT F_EAC, F_EBC, F_SAC, F_SBC, F_NC2, F_N2, \fill

\p\()next:
	ldr	r0, [sp, #F_ROWS]
	add	r0, r0, #ROWBYTES
	str	r0, [sp, #F_ROWS]
	ldr	r1, [sp, #F_LEFT]
	subs	r1, r1, #1
	str	r1, [sp, #F_LEFT]
	bne	\p\()face
.endm

/*
 * Save, build the frame, and take the run-invariant constants once. The
 * palette is reached PC-relative through the GOT; \pal_lit is the literal that
 * holds its offset and \pal_pc the label that offset is measured from, both
 * supplied by the caller so the literal can sit outside the function and cost
 * no branch over it.
 */
.macro ENTER pal_lit, pal_pc
	push	{r4-r11, lr}
	sub	sp, sp, #FRAMESZ
	str	r0, [sp, #F_PIXBUF]
	str	r1, [sp, #F_STRIDE]
	str	r2, [sp, #F_SCRW]
	str	r3, [sp, #F_SCRH]
	lsl	r0, r1, #2
	str	r0, [sp, #F_STRIDEB]
	ldr	r0, [sp, #A_ROWS]
	str	r0, [sp, #F_ROWS]
	ldr	r0, [sp, #A_COUNT]
	str	r0, [sp, #F_LEFT]
	ldr	r0, \pal_lit
\pal_pc:
	ldr	r0, [pc, r0]
	str	r0, [sp, #F_PAL]
	EDGECONST
	EDGESTAGE_INIT r9, F_STAGE
.endm

.macro LEAVE
	add	sp, sp, #FRAMESZ
	pop	{r4-r11, pc}
.endm

/*
 * void toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 */
	.p2align 2
.Lpal_opaque:
	.word	g_hsl16_to_pixel_table(GOT_PREL) - (.Lpalpc_opaque + 8)
	.globl	SYM(toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm)
	.type	SYM(toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm), %function
SYM(toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm):
	ldr	r12, [sp, #4]			/* count                             */
	cmp	r12, #0
	ble	.Lo_ret
	ENTER .Lpal_opaque, .Lpalpc_opaque
	FLATBODY Lo_, SETUP_OPAQUE, FILLOPAQUE
	LEAVE
.Lo_ret:
	bx	lr

/*
 * void toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * As above, blending instead of storing. The per-row alpha is in the staged
 * row, so a run may mix opacities.
 */
	.p2align 2
.Lpal_alpha:
	.word	g_hsl16_to_pixel_table(GOT_PREL) - (.Lpalpc_alpha + 8)
	.globl	SYM(toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm)
	.type	SYM(toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm), %function
SYM(toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm):
	ldr	r12, [sp, #4]			/* count                             */
	cmp	r12, #0
	ble	.La_ret
	ENTER .Lpal_alpha, .Lpalpc_alpha
	FLATBODY La_, SETUP_ALPHA, FILLALPHA
	LEAVE
.La_ret:
	bx	lr

#endif /* __arm__ && !__aarch64__ && __ARM_NEON */
