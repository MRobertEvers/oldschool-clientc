/*
 * The gouraud (HSL-lightness) triangle, batched, in handrolled A32 / Advanced
 * SIMD -- the armv7 twin of tri.gouraudhsllightness.aarch64.S. Only the two RUN
 * doors exist, as on the AArch64 lane.
 *
 * WHAT IS DIFFERENT FROM THE AArch64 KERNEL, AND WHY
 *
 * Everything said in tri.flat.aarch32.S applies here -- the explicit frame, the
 * register-normalised segment, the GOT-relative palette, the untouched
 * d8-d15 -- and this kernel is where the fourteen-register budget actually
 * bites, because the gouraud row loop carries three more live values than the
 * flat one: the colour accumulator, its per-pixel step and the palette base.
 *
 *   - THE PALETTE LIVES IN r7 THE WHOLE TIME, and screen_width does not.
 *     The AArch64 kernel keeps both. Here LOOKUP runs once per four-pixel
 *     block and needs the palette in a register or the block loop grows a
 *     load; the horizontal clamp needs screen_width once per ROW and only on
 *     the clipped path. So the scarce register went to the hotter of the two
 *     and the clamped loop reloads screen_width per row -- into r11, in the
 *     window before r11 becomes the pixel count.
 *
 *   - step_x LIVES IN lr. It is read once per span and once per block, so it
 *     cannot be a frame slot; lr is free after the prologue push and is the
 *     only register left. step_y is the opposite -- once per ROW, drawn or
 *     not -- so it stays in the frame and is loaded at the row join.
 *
 *   - THE SPAN BORROWS ONE FRAME WORD. Splitting a pixel count into blocks and
 *     a remainder needs both halves live across the block loop, and after the
 *     colour accumulator, the palette entry, the count and the destination
 *     there is no fifth scratch register. The remainder goes to F_TMP: one
 *     store and one load per SPAN, not per block.
 *
 *   - NO WIDEN OR NARROW HAPPENS IN PLACE. The AArch64 file round-trips one
 *     register through uxtl/xtn; A32's VMOVL and VMOVN do not guarantee an
 *     overlapping source and destination, so the colour travels through
 *     separate registers on the way out.
 *
 *   - alpha x8 IS IN d30, NOT q7. q7 is d14-d15, which are callee-saved under
 *     AAPCS; the AArch64 file's v7 is not. And it is eight BYTES, not eight
 *     halfwords: the blend multiplies with VMULL.U8, which widens as it goes
 *     -- see BLEND4.
 *
 * THE COLOUR STEPS ARE STILL DOUBLE. Both gradients divide by sarea, and the C
 * (gouraudhsllightness_barycentric_steps.h) does it in double. VFP's scalar
 * vdiv.f64 is exact and is what the AArch64 file uses too, so this is a
 * transcription and not an approximation -- the reciprocal estimate is for the
 * EDGE slopes, where the error budget was measured. The `lsl #8` wraps by
 * design, as the C's toridraw_wrap_shl does.
 *
 * EXACTNESS. Scored against raster_gouraudhsllightness_screen_*_bary_branching_s4()
 * framebuffer against framebuffer (toridraw_presorted_neon_test.c). Assembled
 * with -DTORIDRAW_EDGE_IDIV the test demands every pixel identical and gets it.
 *
 * ROW LAYOUT. `rows` is `count` 48-byte records: x0,x1,x2,pad / y0,y1,y2,pad /
 * c0,c1,c2,alpha -- already in y order, colours permuted with the vertices, the
 * viewport offset folded in. The opaque door never looks at lane 11.
 *
 * AAPCS. r4-r11 and lr are saved; d8-d15 are untouched; no call is made.
 */

#if defined(__arm__) && !defined(__aarch64__) && ( defined(__ARM_NEON) || defined(__ARM_NEON__) )

#define SYM(name) name

#define R_X   0
#define R_Y   16
#define R_C   32
#define R_A   44
#define ROWBYTES 48

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
#define F_HSL      76	/* colour accumulator, ish8; stepped per row         */
#define F_STEPX    80	/* per-pixel colour step (lives in lr in a segment)  */
#define F_STEPY    84	/* per-row colour step                               */
#define F_C0       88	/* vertex 0's colour, while the steps are computed   */
#define F_DAB      92	/* d_hsl_AB, ditto                                   */
#define F_TMP      96	/* the span's block remainder -- see the header      */
#define F_STAGE   104	/* the edge ladder's input staging area, 32 bytes    */
#define FRAMESZ   136

#define A_ROWS   (FRAMESZ + 36)
#define A_COUNT  (FRAMESZ + 40)

#include "graphics/raster/edge_slopes_aarch32.inc"

	.syntax unified
	.arm
	.fpu neon
	.text
	.p2align 2

/*
 * Register map inside a segment:
 *
 *   r0 el   r1 er   r2 sl   r3 sr   r4 rows left in segment
 *   r5 framebuffer row   r6 stride bytes   r7 g_hsl16_to_pixel_table
 *   r8 hsl_ish8 (row colour accumulator)   lr step_x_hsl_ish8
 *   r9, r10, r11, r12 scratch
 *   q0 splat;  q1 K x4, d4 (255-a) x8 u8, q3 0x00FFFFFF x4, d30 a x8 u8
 *   (alpha door);  q8-q11, q14 scratch;  q12, q13 the edge-ladder constants
 */

/*
 * Colour accumulator in \acc -> the ADDRESS of its palette entry in \dst,
 * clamp and all: an edge can land a subpixel past its vertex after the
 * fixed-point nudge, so the interpolant is not guaranteed inside the palette
 * even though every vertex is. Out of range -> 0 or 0xFFFF, branchlessly, off
 * a rarely-taken branch.
 *
 * IT YIELDS AN ADDRESS, WHERE THE AArch64 MACRO YIELDS THE ENTRY. Both hot
 * consumers -- the opaque splat and the alpha blend constant -- want the
 * colour in a NEON register, and on this part the way to get it there is to
 * load it there (VLD1 to all lanes / to one lane) rather than to load it into
 * an ARM register and transfer. Stopping one instruction earlier is what makes
 * that possible; see the note in SPANFILL. The LOOKUP wrapper below adds the
 * `ldr` back for the paths that genuinely need an ARM register.
 *
 * The AArch64 file tests `tst dst, #0xFFFF0000`. That constant is not an ARM
 * data-processing immediate (8 bits rotated by an even amount cannot make it),
 * so the same question is asked as an UNSIGNED compare against 0x10000, which
 * is: a negative accumulator is a huge unsigned and fails it exactly as an
 * over-large positive one does.
 */
.macro LOOKUP_ADDR acc, dst, pal
	asr	\dst, \acc, #8
	cmp	\dst, #0x10000
	blo	90f
	asr	\dst, \dst, #31
	mvn	\dst, \dst
	uxth	\dst, \dst
90:
	add	\dst, \pal, \dst, lsl #2
.endm

/* The palette ENTRY, for the paths that need it in an ARM register. */
.macro LOOKUP acc, dst, pal
	LOOKUP_ADDR \acc, \dst, \pal
	ldr	\dst, [\dst]
.endm

/* flat_screen_fixed_edges_no_hclip; see tri.flat.aarch32.S. */
.macro NOHCLIP ea0, sa, eb0, sb, n, dst, t0, t1
	cmp	\n, #0
	ble	81f
	sub	\t0, \n, #1
	mla	\t1, \sa, \t0, \ea0
	cmp	\ea0, \t1
	movlt	\t1, \ea0
	cmp	\t1, #0
	blt	82f
	mla	\t1, \sb, \t0, \eb0
	cmp	\eb0, \t1
	movgt	\t1, \eb0
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
 * The opaque span. r12 destination, r11 pixel count (>= 1), r9 the colour at
 * x_start, r7 the palette, lr the per-pixel step.
 * toridraw_gouraud_span_fill_short: the under-four span is lifted clear of the
 * block loop, because 59.5% of spans take zero trips through it.
 * Clobbers r9, r10, r11, r12.
 */
.macro SPANFILL
#ifdef TORIDRAW_GOURAUD_SPAN_V1
	cmp	r11, #4
	blt	71f
	and	r10, r11, #3
	str	r10, [sp, #F_TMP]		/* see the header: no fifth scratch  */
	lsr	r11, r11, #2
70:
	LOOKUP_ADDR r9, r10, r7
	vld1.32	{d0[], d1[]}, [r10]
	vst1.32	{d0, d1}, [r12]!
	add	r9, r9, lr, lsl #2
	subs	r11, r11, #1
	bne	70b
	ldr	r11, [sp, #F_TMP]
	cmp	r11, #0
	beq	73f
#else
	/* THE COUNT IS WALKED DOWN IN PLACE, four at a time, so the block loop
	 * and the remainder share one register and the span borrows no frame
	 * word: r11 is "pixels not yet started" and goes negative on the way
	 * out, where adding the four back yields the 0..3 tail. */
	subs	r11, r11, #4
	blt	72f
	/* THE BROADCAST COMES FROM MEMORY, NOT FROM AN ARM REGISTER.
	 *
	 * The obvious transcription of the AArch64 line is `ldr` the palette
	 * entry, then `vdup.32 q0, r10`. That is an ARM->NEON register transfer
	 * in the innermost loop, and on this class of part (Krait) it is the
	 * single most expensive thing in the span: the two register files sit
	 * either side of a queue, and a transfer drains it. On the A64 core the
	 * AArch64 file was tuned against, `dup v0.4s, w10` costs almost nothing,
	 * which is exactly why the transcription looks free and is not.
	 *
	 * VLD1 "load single element to all lanes" does the whole job in one
	 * instruction and never leaves NEON, so LOOKUP hands over an ADDRESS
	 * rather than a value. Measured on the XT1060, big triangles, this door
	 * against the C it replaces: 0.68x before, 1.36x after.
	 *
	 * AND THE NEXT ADDRESS IS RESOLVED BEFORE THIS STORE ISSUES: the vld1
	 * has taken its address by the time the ARM side steps the accumulator
	 * and clamps the next one, so the palette address for block n+1 is
	 * ready when block n's store goes out and the ARM never waits on the
	 * NEON queue between blocks. The one address computed past the end is
	 * never loaded. */
	LOOKUP_ADDR r9, r10, r7
70:
	vld1.32	{d0[], d1[]}, [r10]
	add	r9, r9, lr, lsl #2
	LOOKUP_ADDR r9, r10, r7
	vst1.32	{d0, d1}, [r12]!
	subs	r11, r11, #4
	bge	70b
72:
	adds	r11, r11, #4			/* the 0..3 remainder                */
	beq	73f
#endif
71:
	/* 1..3 pixels: three stores and two conditional indices. */
	LOOKUP	r9, r10, r7
	str	r10, [r12]
	cmp	r11, #1
	movgt	r9, #1
	movle	r9, #0
	str	r10, [r12, r9, lsl #2]
	cmp	r11, #2
	movgt	r9, #2
	movle	r9, #0
	str	r10, [r12, r9, lsl #2]
73:
.endm

/*
 * The source colour's whole contribution to alpha_blend() for the palette
 * entry in \src, broadcast into q1:
 *
 *   K = (((o & 0xFF00FF) * a) >> 8) & 0xFF00FF + (((o & 0xFF00) * a) >> 8) & 0xFF00
 *
 * per channel in u16 lanes. Unlike the flat kernel this cannot be hoisted out
 * of the span: the colour changes every four pixels.
 */
.macro BLENDK addr
	/* Straight from the palette into a NEON lane -- see the note in SPANFILL
	 * for why this must not travel through an ARM register on this part. */
	vld1.32	{d16[0]}, [\addr]
	vmull.u8	q9, d16, d30
	vshrn.u16	d20, q9, #8
	vand	d20, d20, d6
	vdup.32	q1, d20[0]
.endm

/*
 * Four destination pixels at r12 blended under q1 and stored.
 *
 * VMULL.U8 widens and multiplies in one instruction and VSHRN.U16 #8 shifts
 * and narrows in one, and the result is the AArch64 file's uxtl / mul / ushr
 * / xtn bit for bit: no product exceeds 255 * 255, so nothing is lost in the
 * u16 lane and the byte VSHRN keeps is the byte the narrow kept. Six vector
 * instructions where there were ten, held to the exact arm of the test.
 */
.macro BLEND4
	vld1.32	{d16, d17}, [r12]
	vmull.u8	q9, d16, d4
	vmull.u8	q10, d17, d4
	vshrn.u16	d16, q9, #8
	vshrn.u16	d17, q10, #8
	vand	q8, q8, q3
	vadd.i32	q8, q8, q1
	vst1.32	{d16, d17}, [r12]!
.endm

/*
 * BLENDK and BLEND4 as one block, the two chains interleaved: the palette
 * entry and the four destination pixels are loaded back to back, and the
 * destination's products -- which do not depend on K -- run while K is
 * being narrowed and splatted. On an in-order NEON pipe that is the
 * difference between two dependent chains and one. Same instructions, same
 * bits; only the order.
 */
.macro BLENDK4 addr
	vld1.32	{d16[0]}, [\addr]
	vld1.32	{d22, d23}, [r12]
	vmull.u8	q9, d16, d30		/* K's products                      */
	vmull.u8	q14, d22, d4		/* the destination's                 */
	vmull.u8	q10, d23, d4
	vshrn.u16	d17, q9, #8		/* d17, not d20: q10 is still live   */
	vshrn.u16	d22, q14, #8
	vshrn.u16	d23, q10, #8
	vand	d17, d17, d6
	vand	q11, q11, q3
	vdup.32	q1, d17[0]
	vadd.i32	q11, q11, q1
	vst1.32	{d22, d23}, [r12]!
.endm

/*
 * The alpha span. Same blocking as the opaque one, because the palette lookup
 * is per block in both and the phase is visible output. The 1..3 tail is a
 * real loop: a blend is not idempotent, so the opaque span's duplicate stores
 * are not available.
 */
.macro SPANFILL_ALPHA
	cmp	r11, #4
	blt	76f
	and	r10, r11, #3
	str	r10, [sp, #F_TMP]
	lsr	r11, r11, #2
75:
	LOOKUP_ADDR r9, r10, r7
	BLENDK4	r10
	add	r9, r9, lr, lsl #2
	subs	r11, r11, #1
	bne	75b
	ldr	r11, [sp, #F_TMP]
	cmp	r11, #0
	beq	78f
76:
	LOOKUP_ADDR r9, r10, r7
	BLENDK	r10
77:
	vld1.32	{d16[0]}, [r12]
	vmull.u8	q9, d16, d4
	vshrn.u16	d16, q9, #8
	vand	d16, d16, d6
	vadd.i32	d16, d16, d2
	vst1.32	{d16[0]}, [r12]!
	subs	r11, r11, #1
	bne	77b
78:
.endm

/*
 * One trapezoid segment; see tri.flat.aarch32.S for the shape and why this
 * takes frame offsets. The colour accumulator is stepped at the row join,
 * drawn or not, and the colour at x_start is x_start * step_x + hsl (low 32
 * bits, as the C wraps).
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
	ldr	r7, [sp, #F_PAL]
	ldr	r8, [sp, #F_HSL]
	ldr	lr, [sp, #F_STEPX]
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
	sub	r11, r10, r9
	add	r12, r5, r9, lsl #2
	mla	r9, r9, lr, r8			/* colour at x_start                 */
	\fill
64:
	ldr	r10, [sp, #F_STEPY]
	add	r8, r8, r10			/* every row, drawn or not           */
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
	ldr	r11, [sp, #F_SCRW]		/* r11 before it is the pixel count  */
	cmp	r10, r11
	subge	r10, r11, #1
	asr	r9, r9, #16
	cmp	r9, #0
	movlt	r9, #0
	cmp	r9, r10
	bge	67f
	sub	r11, r10, r9
	add	r12, r5, r9, lsl #2
	mla	r9, r9, lr, r8
	\fill
67:
	ldr	r10, [sp, #F_STEPY]
	add	r8, r8, r10
	add	r5, r5, r6
	subs	r4, r4, #1
	bne	65b
68:
	str	r0, [sp, #\off_el]
	str	r1, [sp, #\off_er]
	str	r5, [sp, #F_ROWPTR]
	str	r8, [sp, #F_HSL]
69:
.endm

.macro SETUP_OPAQUE
.endm

/* The alpha lane's constants, live across both trapezoid walks. */
.macro SETUP_ALPHA
	ldr	r2, [r11, #R_A]
	vdup.8	d30, r2				/* alpha x8, u8: VMULL widens it     */
	rsb	r2, r2, #255
	vdup.8	d4, r2				/* 255 - alpha, x8, u8               */
	vmvn.i32	q3, #0xff000000		/* 0x00FFFFFF x4                     */
.endm

.macro GOURAUDBODY p, setup, fill
\p\()face:
	ldr	r11, [sp, #F_ROWS]
	ldr	r0, [r11, #R_X + 0]		/* x0                                */
	ldr	r1, [r11, #R_X + 4]		/* x1                                */
	ldr	r2, [r11, #R_X + 8]		/* x2                                */
	ldr	r3, [r11, #R_Y + 0]		/* y0                                */
	ldr	r4, [r11, #R_Y + 4]		/* y1                                */
	ldr	r5, [r11, #R_Y + 8]		/* y2                                */

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

	/* sarea = dx_AB*dy_AC - dx_AC*dy_AB */
	mul	r10, r8, r6
	mls	r10, r7, r9, r10
	cmp	r10, #0
	beq	\p\()next

	/* Both colour gradients divide by sarea: one reciprocal, two multiplies,
	 * in double, exactly as gouraudhsllightness_barycentric_steps.h. */
	vmov	s0, r10
	vcvt.f64.s32	d1, s0
	vmov.f64	d2, #1.0
	vdiv.f64	d1, d2, d1		/* 1.0 / sarea                       */

	ldr	r10, [r11, #R_C + 0]		/* c0                                */
	str	r10, [sp, #F_C0]
	ldr	r12, [r11, #R_C + 4]		/* c1                                */
	ldr	lr,  [r11, #R_C + 8]		/* c2                                */
	sub	r12, r12, r10			/* d_hsl_AB                          */
	sub	lr,  lr,  r10			/* d_hsl_AC                          */
	str	r12, [sp, #F_DAB]

	/* step_x_hsl_ish8 */
	mul	r10, r12, r6			/* d_hsl_AB * dy_AC                  */
	mls	r10, lr, r9, r10		/* - d_hsl_AC * dy_AB                */
	lsl	r10, r10, #8
	vmov	s0, r10
	vcvt.f64.s32	d3, s0
	vmul.f64	d3, d3, d1
	vcvt.s32.f64	s0, d3

	/* step_y_hsl_ish8 */
	mul	r12, lr, r8			/* d_hsl_AC * dx_AB                  */
	ldr	lr, [sp, #F_DAB]
	mls	r12, lr, r7, r12		/* - d_hsl_AB * dx_AC                */
	lsl	r12, r12, #8
	vmov	s1, r12
	vcvt.f64.s32	d2, s1
	vmul.f64	d2, d2, d1
	vcvt.s32.f64	s1, d2
#ifdef TORIDRAW_EDGE_STAGE_OUT
	/* Both steps sit in d0 = {s0, s1}, and F_STEPX / F_STEPY are adjacent:
	 * one vector store, and the one the ARM needs now comes back by ldr. */
	add	lr, sp, #F_STEPX
	vst1.32	{d0}, [lr]
	ldr	r10, [sp, #F_STEPX]
#else
	vmov	r10, s0
	str	r10, [sp, #F_STEPX]
	vmov	r12, s1
	str	r12, [sp, #F_STEPY]
#endif

	/* hsl = (step_x + (color0 << 8)) - x0*step_x, all modular */
	ldr	lr, [sp, #F_C0]
	lsl	lr, lr, #8
	add	lr, lr, r10
	mls	lr, r0, r10, lr
	str	lr, [sp, #F_HSL]

	/* The three edge slopes, all three at once, off one packed reciprocal.
	 * AFTER the colour steps, whose double reciprocal lives in d1 -- the
	 * ladder scratches q8-q11 and q14 only. */
	sub	r10, r5, r4			/* dy_BC                             */
	sub	r12, r2, r1			/* dx_BC                             */
	EDGESLOPES3 r7, r8, r12, r6, r9, r10, r7, r8, r12, lr, F_STAGE, F_SAC

	/* starting edges */
	lsl	r6, r0, #16			/* eAC                               */
	mov	r9, r6				/* eAB                               */
	lsl	r10, r1, #16			/* eBC                               */

	/* ---- vertical clipping ---- */
	cmp	r3, #0
	bge	\p\()y0_ok
	mls	r6, r7, r3, r6
	cmp	r4, #0
	ble	\p\()no_ab_prestep
	mls	r9, r8, r3, r9
\p\()no_ab_prestep:
	ldr	r2, [sp, #F_HSL]
	ldr	lr, [sp, #F_STEPY]
	mls	r2, lr, r3, r2			/* hsl -= step_y * y0                */
	str	r2, [sp, #F_HSL]
	mov	r3, #0
\p\()y0_ok:
	cmp	r4, #0
	bge	\p\()y1_ok
	mls	r10, r12, r4, r10
	mov	r4, #0
\p\()y1_ok:
	str	r6, [sp, #F_EAC]
	str	r9, [sp, #F_EAB]
	str	r10, [sp, #F_EBC]

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

	subs	r2, r4, r3
	movlt	r2, #0
	str	r2, [sp, #F_N1]
	subs	r0, r5, r4
	movlt	r0, #0
	str	r0, [sp, #F_N2]

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
 * void toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 */
	.p2align 2
.Lpal_opaque:
	.word	g_hsl16_to_pixel_table(GOT_PREL) - (.Lpalpc_opaque + 8)
	.globl	SYM(toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm)
	.type	SYM(toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm), %function
SYM(toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm):
	ldr	r12, [sp, #4]
	cmp	r12, #0
	ble	.Lo_ret
	ENTER .Lpal_opaque, .Lpalpc_opaque
	GOURAUDBODY Lo_, SETUP_OPAQUE, SPANFILL
	LEAVE
.Lo_ret:
	bx	lr

/*
 * void toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * The blending twin, opacity per row in lane 11; a run may mix them.
 */
	.p2align 2
.Lpal_alpha:
	.word	g_hsl16_to_pixel_table(GOT_PREL) - (.Lpalpc_alpha + 8)
	.globl	SYM(toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm)
	.type	SYM(toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm), %function
SYM(toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm):
	ldr	r12, [sp, #4]
	cmp	r12, #0
	ble	.La_ret
	ENTER .Lpal_alpha, .Lpalpc_alpha
	GOURAUDBODY La_, SETUP_ALPHA, SPANFILL_ALPHA
	LEAVE
.La_ret:
	bx	lr

#endif /* __arm__ && !__aarch64__ && __ARM_NEON */
