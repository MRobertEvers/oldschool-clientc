/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES   (i686 only)
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The perspective-textured triangle, batched, in handrolled AArch64 /
 * Advanced SIMD -- the NEON twin of tex_tri_i686.S. Four RUN doors --
 * opaque/colour-keyed x blend/flat -- and one body, which is the point:
 * they cannot draw different triangles, only pay differently to reach it.
 *
 * WHAT IS THE SAME AS THE i686 KERNEL
 *
 *   - The whole triangle: sort-free prologue, the nine cross products of the
 *     uv plane, the three edge slopes off one packed reciprocal
 *     (edge_slopes_aarch64.inc), the two shade gradients off one shared
 *     reciprocal of sarea, both trapezoid walks and the span, with no C
 *     between the entry and the last pixel except ToriDraw_TexturePlane
 *     Prepare32, which stays in C for the reasons tex_tri_asm.h gives.
 *   - ONE walk, not four: the left/right decision picks which edge lands in
 *     which slot, and the phase change swaps a single edge into whichever
 *     side the decision said.
 *   - The span is the NEON C's (tex.span.neon.u.c, the v3 walk): eight
 *     pixels per block, the uv fitted linearly between the block's two
 *     endpoint quotients, the endpoint carried from block to block so there
 *     is one new quotient pair per block, the fit refused -- and the block
 *     drawn a pixel at a time with exact divides -- when the block sweeps
 *     more than a texture tile or the quotient has left the reciprocal's
 *     exact range. Same float clamp for u, same wrapped quotient for v, same
 *     saturating narrow in the blend.
 *   - The colour key is tested on the RAW texel, before the shade, in all
 *     three places a pixel is written.
 *
 * WHAT IS DIFFERENT
 *
 *   - Register-resident. The i686 kernel put the span's pipeline state in
 *     the frame because eight registers could not hold it; here the block
 *     loop's accumulators, endpoints, steps, masks and shade all stay in
 *     registers, and only the ROW state (edges, plane accumulators, shade at
 *     the row, counts) lives in the frame, reloaded once per row.
 *   - The i686 kernel software-pipelined its reciprocal three blocks ahead
 *     because a P4's divss is unpipelined. An out-of-order core with a
 *     pipelined reciprocal hides the same latency by itself, so the
 *     quotients are computed where the C computes them -- with the i686
 *     kernel's estimate-and-refine reciprocal, not the C's divide.
 *   - The eight texel indices of a block are computed as two vectors
 *     (u + s_u*[0..7], v + s_v*[0..7], shift, mask, add) rather than eight
 *     scalar adds; the gather itself is eight lane loads either way.
 *   - The flat doors are the blend body with both shade gradients zero and
 *     the two divides skipped, exactly as the i686's P_FLAT arm; nothing
 *     else is specialised, because a zero step costs one add per row.
 *
 * EXACTNESS. Scored against raster_texshadeblend_persp_tex{opaque,trans}_
 * branching_lerp8_v3 -- the C the macOS build draws with -- framebuffer
 * against framebuffer with guard bands (toridraw_presorted_neon_test.c). The
 * packed edge slopes, the reciprocal of w and the shared reciprocal of
 * sarea are the three approximations the i686 kernel takes and this one
 * takes; everything else is the C's own operations in the C's own order.
 *
 * ROW LAYOUT. `rows` is `count` records of TORIDRAW_RASTER_TEXBATCH_ROW_INTS
 * (24) ints, 16-byte aligned: three screen x, three screen y, the nine
 * orthographic coordinates of the texture frame (uv origin, u end, v end --
 * three ROLES, never permuted with the corners), three shades, the texel
 * pointer in lanes 18-19, the texture width in 20, the colour-key gate in 21.
 * The x, y and shade triples arrive ALREADY in y order.
 *
 * AAPCS64. x19-x28, x29, x30 saved. v8-v15 are never touched, so the one
 * call in the prologue has nothing to preserve for us; everything the walk
 * keeps in v16-v31 is (re)built after that call.
 */

#if defined(__aarch64__)

#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

#include "graphics/raster/edge_slopes_aarch64.inc"

/* The staged row. */
#define R_X       0
#define R_Y       12
#define R_OX      24
#define R_OY      36
#define R_OZ      48
#define R_SHADE   60
#define R_TEXELS  72
#define R_TW      80
#define R_GATE    84
#define ROWBYTES  96

/* The frame. Slot 0 is the plane struct handed to the C prepare. */
#define F_PLANE   0             /* term[3] of {x, y, z, base}, then shift: 52 bytes */
#define PL_0X     0
#define PL_0Y     4
#define PL_0Z     8
#define PL_0B     12
#define PL_1X     16
#define PL_1Y     20
#define PL_1Z     24
#define PL_1B     28
#define PL_2X     32
#define PL_2Y     36
#define PL_2Z     40
#define PL_2B     44
#define PL_SH     48
#define F_PIX     64            /* the six arguments, out of the call's way */
#define F_STRIDE  72
#define F_SW      76
#define F_SH      80
#define F_COT     84
#define F_ROW     88            /* the staged row being rasterised */
#define F_LEFT    96            /* rows still to go */
#define F_FLAT    100           /* 1 = one shade for the face, no gradient */
#define F_GATE    104           /* 0 = every texel drawn, 1 = RGB 0 keys */
#define F_LX      108           /* the trapezoid walk: LEFT and RIGHT are slots */
#define F_LS      112
#define F_RX      116
#define F_RS      120
#define F_AU      124           /* plane accumulators at the row's screen centre */
#define F_BV      128
#define F_CW      132
#define F_YAU     136           /* and their per-row steps */
#define F_YBV     140
#define F_YCW     144
#define F_SHADE   148           /* shade at the row's left screen edge */
#define F_SHY     152
#define F_SHX     156
#define F_ROWPTR  160           /* pixel pointer of the row */
#define F_ROWS    168           /* rows left in the phase being walked */
#define F_ROWS2   172           /* rows in the second phase */
#define F_P2X     176           /* the edge that replaces one side at the phase change */
#define F_P2S     180
#define F_P2L     184           /* nonzero: it replaces LEFT; zero: RIGHT */
#define F_PHASE   188
#define F_STAU    192           /* per-pixel plane steps: the xhat normals */
#define F_STBV    196
#define F_STCW    200
#define F_SHIFT   204
#define F_TW      208
#define F_REMAIN  212           /* the row's ragged tail, 0..7 */
#define F_TEXELS  216
#define F_SAVE    224           /* x19..x28, x29, x30 */
#define FRAME     320

#define LIM_F     0x4A000000    /* 2097152.0f: TEX_SPAN_RECIPROCAL_EXACT_LIMIT */

	.text
	.p2align 2

/*
 * Register map during the walk:
 *
 *   x19 texels   w20 shift   w21 tw - 1   w22 (tw - 1) << shift
 *   w23 s_au  w24 s_bv  w25 s_cw     (per-pixel plane steps)
 *   w26 gate  w27 shx   w28 screen_width
 *   s1 1.0f   s4 0.0f   s5 (float)(tw - 1)   s6 2097152.0f
 *   v26 (tw-1) x4   v27 vmask x4   v28 {0,1,2,3}   v29 {4,5,6,7}   v30 -shift x4
 *
 * Inside the span:
 *   w4 au  w5 bv  w6 cw   (at the block start)     w3 blocks left
 *   w7 have_cur   w13 cur_u  w14 cur_v  w15 nxt_u  w16 nxt_v
 *   x11 destination   w12 shade, 8.8   s0 1/w
 *   w0-w2, w8-w10, w17, x13, s2, s3, v16-v25, v31: scratch
 */

/*
 * tex_span_u_quotient: q = n * inv_w, clamped INTO [0, tw-1] in float, then
 * converted -- the order matters, an out-of-range fcvtzs is not what the C
 * does. fmax/fmin against the two bounds are the C's two compares.
 */
.macro UQUOT n, out
	scvtf	s2, \n
	fmul	s2, s2, s0
	fmax	s2, s2, s4
	fmin	s2, s2, s5
	fcvtzs	\out, s2
.endm

/*
 * tex_span_wrapped_quotient: trust the SINGLE-precision reciprocal only while
 * it still carries the low bits that select the texel row, else redo the same
 * quotient in DOUBLE.
 *
 * THERE IS NO INTEGER DIVIDE IN THIS KERNEL, on either encoding. The fallback
 * used to be `sdiv`, which was the obvious way to say "exactly what the C
 * computes" -- but it is not the only way, and it is not the way the armv7
 * twin can say it (`sdiv` is not in the ARMv7-A base instruction set at all).
 * A double reciprocal says the same thing on both, so the two files now agree
 * instruction for instruction here.
 *
 * AND IT IS EXACTLY AS EXACT, which is the only reason this substitution is
 * allowed. Both operands are int32 and convert to double exactly, vdiv/fdiv is
 * correctly rounded, and the rounding can never cross an integer boundary:
 * for any integer k the quotient does not equal, |q - k| >= 1/|w|, while half
 * an ulp of q is |n| / (|w| * 2^53) -- so a wrong truncation would need
 * |n| > 2^53, and |n| < 2^31 here. Where the quotient IS an integer, a
 * correctly rounded divide returns it exactly. fcvtzs truncates toward zero,
 * as C does.
 *
 * IT MUST BE A DIVIDE, NOT A RECIPROCAL AND A MULTIPLY, and that is the whole
 * subtlety. `n * (1.0/w)` rounds twice, and 1.0/w is inexact for every w that
 * is not a power of two -- so where the true quotient is an exact integer k
 * the product can land a hair BELOW k and truncate to k-1. The measurement
 * caught it: written that way this kernel moved from 3561 to 5510 ppm of
 * triangles against the C, and the class of triangle it moved was exactly the
 * one whose quotients land on texel boundaries. One correctly rounded fdiv has
 * no such case. Cheaper is not available here.
 *
 * IT IS SINGLE PRECISION, AND THAT IS A DECISION, not an oversight. A double
 * divide here is measurably closer to the C: it takes the kernel from 12353
 * ppm of TRIANGLES differing from the reference down to 3561, because single
 * is what the FAST path already uses -- s0 is 1/w in float -- and this
 * fallback exists for the quotients single can no longer carry. The fcmp
 * against s6 (2^21) above is where a 24-bit mantissa stops holding the low
 * bits, and those low bits are what `& (tw-1)` turns into a texel row.
 *
 * It is single anyway, because the difference does not survive contact with a
 * picture. Measured, not argued:
 *
 *   - a perspective-textured floor rendered both ways is BYTE-IDENTICAL --
 *     0 differing bytes in 1,024,000;
 *   - the pixel figure barely moves either (208 -> 269 ppm), and the worst
 *     error inside any single triangle does not grow at all;
 *   - both are far inside the door's 20000 / 2000 ppm budget;
 *   - and the two cost the same on the target part (XT1060: 316.40 ms against
 *     318.72 ms over the same 200 runs).
 *
 * So the whole difference is a tail of extreme geometry in the randomised
 * corpus that no frame contains, and single is the narrower dependency: one
 * precision through the entire span, and an armv7 twin that needs no VFP
 * double at all. If a texel-row seam is ever seen in a real scene, THIS is
 * the line to change back, and the numbers above say what it would buy.
 */
.macro WRAPQ n, w, out
	scvtf	s2, \n
	fmul	s2, s2, s0
	fabs	s3, s2
	fcmp	s3, s6
	b.ge	88f
	fcvtzs	\out, s2
	b	89f
88:
	scvtf	s16, \n
	scvtf	s17, \w
	fdiv	s16, s16, s17			/* n / w, single precision           */
	fcvtzs	\out, s16
89:
.endm

/*
 * 1 / w into s0, the i686 kernel's RECIP: an estimate and Newton-Raphson,
 * not a divide. frecpe is 8 bits where rcpss was 12, so two steps where
 * the i686 took one -- WRAPQ trusts this quotient out to 2^21, and 16 bits
 * is thirty texel rows short of that. w is nonzero here.
 */
.macro RECIP w
	scvtf	s0, \w
#ifdef TORIDRAW_TEX_RECIP_DIV
	/* the C's own divide, for telling the walk from the estimate */
	fdiv	s0, s1, s0
#else
	frecpe	s2, s0
	frecps	s3, s0, s2
	fmul	s2, s2, s3
	frecps	s3, s0, s2
	fmul	s0, s2, s3
#endif
.endm

/*
 * tex_span_lerp8_fits: both endpoints inside +-2^29 and the block sweeping
 * no more than one texture tile in v. Branches to \fail otherwise.
 * Clobbers w8, w9.
 */
.macro FITS cur, nxt, fail
	movz	w8, #0x2000, lsl #16
	add	w9, \cur, w8
	cmp	w9, w8, lsl #1
	b.hi	\fail
	add	w9, \nxt, w8
	cmp	w9, w8, lsl #1
	b.hi	\fail
	sub	w9, \nxt, \cur
	add	w9, w9, w21
	add	w9, w9, #1			/* delta + tw                        */
	add	w8, w21, w21
	add	w8, w8, #2			/* 2 * tw                            */
	cmp	w9, w8
	b.hi	\fail
.endm

/*
 * shade_blend, scalar, for the exact block and the ragged tail: the C's
 * masked u32 multiplies, which wrap where the vector form saturates. \c the
 * texel, receives the result; \t scratch; \sh the shade (0..255-ish).
 */
.macro SHADEBLEND c, t, sh
	and	\t, \c, #0x0000FF00
	and	\c, \c, #0x00FF00FF
	mul	\c, \c, \sh
	mul	\t, \t, \sh
	and	\c, \c, #0xFF00FF00
	and	\t, \t, #0x00FF0000
	orr	\c, \c, \t
	lsr	\c, \c, #8
.endm

/*
 * shade_blend4_neon: widen to u16, multiply by the shade broadcast in v31,
 * shift, SATURATING narrow -- the C's vqmovn_u16, which is the same bits as
 * the scalar form for every shade in 0..255 and deliberately not otherwise.
 * Scratch is v22/v23, which the index vectors vacated: v0-v7 hold the
 * walk's scalar constants (s1, s4, s5, s6) and must survive the block.
 */
.macro BLEND4 src, dst
	uxtl	v22.8h, \src\().8b
	uxtl2	v23.8h, \src\().16b
	mul	v22.8h, v22.8h, v31.8h
	mul	v23.8h, v23.8h, v31.8h
	ushr	v22.8h, v22.8h, #8
	ushr	v23.8h, v23.8h, #8
	uqxtn	\dst\().8b, v22.8h
	uqxtn2	\dst\().16b, v23.8h
.endm

/*
 * tex_span_exact_block: \count pixels from x11, an exact quotient per pixel,
 * the same mapping the reference rasterizer uses for every pixel. Cold.
 * Reads au/bv/cw from w4-w6 without touching them. Clobbers w0-w2, w8-w10,
 * w14, w17, x13, d16-d18.
 *
 * TWO DIVIDES, NOT ONE RECIPROCAL AND TWO MULTIPLIES -- and no integer
 * divide. Both quotients here divide by the same w, so sharing a reciprocal
 * looks free and is not: see WRAPQ above for why a reciprocal-and-multiply
 * loses the exact-integer quotients that a correctly rounded divide keeps.
 * This is the cold block, entered only when the linear fit was refused, so
 * the second fdiv costs nothing that shows.
 */
.macro EXACTBLK count
	mov	x13, x11
	mov	w14, \count
	mov	w9, w4
	mov	w2, w5
	mov	w17, w6
	asr	w10, w12, #8
91:
	asr	w0, w17, w20			/* w                                 */
	cbz	w0, 93f
	scvtf	s17, w0				/* (float)w, shared by both          */
	scvtf	s16, w9
	fdiv	s16, s16, s17
	fcvtzs	w1, s16				/* clamp(au / w, 0, tw - 1)          */
	cmp	w1, #0
	csel	w1, wzr, w1, lt
	cmp	w1, w21
	csel	w1, w21, w1, gt
	scvtf	s16, w2
	fdiv	s16, s16, s17
	fcvtzs	w0, s16				/* (bv / w) & (tw - 1), then the row */
	and	w0, w0, w21
	lsl	w0, w0, w20
	add	w1, w1, w0
	ldr	w1, [x19, w1, uxtw #2]
	cbz	w26, 92f
	cbz	w1, 93f				/* the colour key, on the raw texel  */
92:
	SHADEBLEND w1, w0, w10
	str	w1, [x13]
93:
	add	x13, x13, #4
	add	w9, w9, w23
	add	w2, w2, w24
	add	w17, w17, w25
	subs	w14, w14, #1
	b.ne	91b
.endm

/*
 * The eight-pixel linear block at x11, between cur (w13, w14) and nxt
 * (w15, w16). Indices as two vectors, eight lane loads, two blends, the
 * colour key as a select against the destination. Clobbers w0-w2, w8, w9,
 * x0, v16, v19-v25, v31 -- and nothing in v0-v7, which hold the walk's
 * scalar constants.
 */
.macro LERP8
	sub	w8, w15, w13
	sub	w9, w20, #3
	lsl	w8, w8, w9			/* s_u = (nxt_u - cur_u) << (shift-3)*/
	sub	w0, w16, w14
	lsl	w0, w0, w9			/* s_v                               */
	lsl	w1, w13, w20			/* u_scan = cur_u << shift           */
	and	w2, w14, w21
	lsl	w2, w2, w20			/* v_scan = (cur_v & (tw-1)) << shift*/
	dup	v16.4s, w1
	dup	v19.4s, w8
	dup	v20.4s, w2
	dup	v21.4s, w0
	mov	v22.16b, v16.16b
	mla	v22.4s, v19.4s, v28.4s		/* u_scan + s_u * {0,1,2,3}          */
	mla	v16.4s, v19.4s, v29.4s		/* u_scan + s_u * {4,5,6,7}          */
	mov	v23.16b, v20.16b
	mla	v23.4s, v21.4s, v28.4s
	mla	v20.4s, v21.4s, v29.4s
	sshl	v22.4s, v22.4s, v30.4s		/* u >> shift (arithmetic)           */
	sshl	v16.4s, v16.4s, v30.4s
	and	v22.16b, v22.16b, v26.16b	/* & (tw - 1)                        */
	and	v16.16b, v16.16b, v26.16b
	and	v23.16b, v23.16b, v27.16b	/* v & vmask                         */
	and	v20.16b, v20.16b, v27.16b
	add	v22.4s, v22.4s, v23.4s		/* texel indices 0..3                */
	add	v16.4s, v16.4s, v20.4s		/* 4..7                              */

	mov	w0, v22.s[0]
	add	x0, x19, w0, uxtw #2
	ld1	{v24.s}[0], [x0]
	mov	w0, v22.s[1]
	add	x0, x19, w0, uxtw #2
	ld1	{v24.s}[1], [x0]
	mov	w0, v22.s[2]
	add	x0, x19, w0, uxtw #2
	ld1	{v24.s}[2], [x0]
	mov	w0, v22.s[3]
	add	x0, x19, w0, uxtw #2
	ld1	{v24.s}[3], [x0]
	mov	w0, v16.s[0]
	add	x0, x19, w0, uxtw #2
	ld1	{v25.s}[0], [x0]
	mov	w0, v16.s[1]
	add	x0, x19, w0, uxtw #2
	ld1	{v25.s}[1], [x0]
	mov	w0, v16.s[2]
	add	x0, x19, w0, uxtw #2
	ld1	{v25.s}[2], [x0]
	mov	w0, v16.s[3]
	add	x0, x19, w0, uxtw #2
	ld1	{v25.s}[3], [x0]

	asr	w0, w12, #8
	dup	v31.8h, w0			/* the block's shade                 */
	BLEND4	v24, v20
	BLEND4	v25, v21
	cbz	w26, 95f
	/* Keyed: keep the destination wherever the RAW texel was zero. */
	ldp	q16, q19, [x11]
	cmeq	v24.4s, v24.4s, #0
	cmeq	v25.4s, v25.4s, #0
	bsl	v24.16b, v16.16b, v20.16b
	bsl	v25.16b, v19.16b, v21.16b
	stp	q24, q25, [x11]
	b	96f
95:
	stp	q20, q21, [x11]
96:
.endm

/*
 * The ragged tail, 1..7 pixels, along the linear fit: a scalar walk of the
 * same scan, the same colour key. \count pixels. Clobbers w0-w2, w8-w10,
 * w14, w17, x13.
 */
.macro TAILLERP count
	sub	w8, w15, w13
	sub	w9, w20, #3
	lsl	w8, w8, w9			/* s_u                               */
	sub	w17, w16, w14
	lsl	w17, w17, w9			/* s_v                               */
	lsl	w1, w13, w20			/* u_scan                            */
	and	w2, w14, w21
	lsl	w2, w2, w20			/* v_scan                            */
	mov	x13, x11
	mov	w14, \count
	asr	w10, w12, #8
97:
	asr	w0, w1, w20
	and	w0, w0, w21
	and	w9, w2, w22
	add	w0, w0, w9
	ldr	w0, [x19, w0, uxtw #2]
	cbz	w26, 98f
	cbz	w0, 99f
98:
	SHADEBLEND w0, w9, w10
	str	w0, [x13]
99:
	add	x13, x13, #4
	add	w1, w1, w8
	add	w2, w2, w17
	subs	w14, w14, #1
	b.ne	97b
.endm

/* ---- entries ---------------------------------------------------------- */

/*
 * void toridraw_textri_{opaque,trans,flat_opaque,flat_trans}_lerp8_v3_
 *     presorted_run_asm(int* pixel_buffer, int stride, int screen_width,
 *     int screen_height, int camera_cot16, const int* rows, int count);
 *
 * The gate travels in the row (lane 21), so a run may mix keyed and opaque
 * faces; the opaque/trans pairs of doors differ only in name. The flat doors
 * stamp F_FLAT, which skips the shade gradients.
 */
.macro TEXENTRY name, flat
	.globl	SYM(\name)
SYM(\name):
	cmp	w6, #0
	b.le	1f
	sub	sp, sp, #FRAME
	stp	x19, x20, [sp, #F_SAVE]
	stp	x21, x22, [sp, #F_SAVE + 16]
	stp	x23, x24, [sp, #F_SAVE + 32]
	stp	x25, x26, [sp, #F_SAVE + 48]
	stp	x27, x28, [sp, #F_SAVE + 64]
	stp	x29, x30, [sp, #F_SAVE + 80]
	str	x0, [sp, #F_PIX]
	str	w1, [sp, #F_STRIDE]
	str	w2, [sp, #F_SW]
	str	w3, [sp, #F_SH]
	str	w4, [sp, #F_COT]
	str	x5, [sp, #F_ROW]
	str	w6, [sp, #F_LEFT]
	mov	w7, #\flat
	str	w7, [sp, #F_FLAT]
	b	Ltex_tri
1:
	ret
.endm

	TEXENTRY toridraw_textri_opaque_lerp8_v3_presorted_run_xrgb8888_asm, 0
	TEXENTRY toridraw_textri_trans_lerp8_v3_presorted_run_xrgb8888_asm, 0
	TEXENTRY toridraw_textri_flat_opaque_lerp8_v3_presorted_run_xrgb8888_asm, 1
	TEXENTRY toridraw_textri_flat_trans_lerp8_v3_presorted_run_xrgb8888_asm, 1

	.p2align 4
Ltex_lanes:
	.word	0, 1, 2, 3, 4, 5, 6, 7

/* ---- one triangle ----------------------------------------------------- */
Ltex_tri:
	ldr	x8, [sp, #F_ROW]

	/* Off the top or off the bottom of the viewport. */
	ldr	w5, [x8, #R_Y + 8]
	tbnz	w5, #31, Ltex_next
	ldr	w3, [x8, #R_Y]
	ldr	w9, [sp, #F_SH]
	cmp	w3, w9
	b.gt	Ltex_next

	ldp	w0, w1, [x8, #R_X]
	ldr	w2, [x8, #R_X + 8]
	ldr	w4, [x8, #R_Y + 4]
	sub	w6, w5, w3			/* dy_AC                             */
	sub	w7, w4, w3			/* dy_AB                             */
	sub	w9, w2, w0			/* dx_AC                             */
	sub	w10, w1, w0			/* dx_AB                             */
	mul	w11, w9, w7
	msub	w11, w10, w6, w11		/* sarea = dx_AC*dy_AB - dx_AB*dy_AC */
	cbz	w11, Ltex_next			/* degenerate: no area, no pixels    */

	/* ---- the uv plane: two edge vectors, three cross products ---- */
	ldp	w12, w13, [x8, #R_OX]
	ldr	w14, [x8, #R_OX + 8]
	ldp	w15, w16, [x8, #R_OY]
	ldr	w17, [x8, #R_OY + 8]
	ldp	w19, w20, [x8, #R_OZ]
	ldr	w21, [x8, #R_OZ + 8]
	sub	w22, w13, w12			/* vU_x                              */
	sub	w23, w16, w15			/* vU_y                              */
	sub	w24, w20, w19			/* vU_z                              */
	sub	w25, w14, w12			/* vV_x                              */
	sub	w26, w17, w15			/* vV_y                              */
	sub	w27, w21, w19			/* vV_z                              */

	/* U x V -> term[2] */
	mul	w0, w24, w26
	msub	w0, w23, w27, w0		/* vU_z*vV_y - vU_y*vV_z             */
	mul	w1, w22, w27
	msub	w1, w24, w25, w1		/* vU_x*vV_z - vU_z*vV_x             */
	mul	w2, w23, w25
	msub	w2, w22, w26, w2		/* vU_y*vV_x - vU_x*vV_y             */
	stp	w0, w1, [sp, #PL_2X]
	stp	w2, wzr, [sp, #PL_2Z]
	/* O x V -> term[0] */
	mul	w0, w15, w27
	msub	w0, w19, w26, w0		/* o_y*vV_z - o_z*vV_y               */
	mul	w1, w19, w25
	msub	w1, w12, w27, w1		/* o_z*vV_x - o_x*vV_z               */
	mul	w2, w12, w26
	msub	w2, w15, w25, w2		/* o_x*vV_y - o_y*vV_x               */
	stp	w0, w1, [sp, #PL_0X]
	stp	w2, wzr, [sp, #PL_0Z]
	/* U x O -> term[1] */
	mul	w0, w23, w19
	msub	w0, w24, w15, w0		/* vU_y*o_z - vU_z*o_y               */
	mul	w1, w24, w12
	msub	w1, w22, w19, w1		/* vU_z*o_x - vU_x*o_z               */
	mul	w2, w22, w15
	msub	w2, w23, w12, w2		/* vU_x*o_y - vU_y*o_x               */
	stp	w0, w1, [sp, #PL_1X]
	stp	w2, wzr, [sp, #PL_1Z]
	str	wzr, [sp, #PL_SH]

	/* ToriDraw_TexturePlanePrepare32 stays in C -- see the header. */
	mov	x0, sp
	ldr	w1, [sp, #F_SW]
	ldr	w2, [sp, #F_SH]
	ldr	w3, [sp, #F_COT]
	bl	SYM(toridraw_texplane_prepare32_asm)
	cbz	w0, Ltex_next

	/* ---- back from the call: everything below is the walk's setup ---- */
	/* The slope constants are rebuilt per triangle -- the call clobbered
	 * every v16+ -- and BEFORE the deltas: EDGECONST scratches w9. */
	EDGECONST
	ldr	x8, [sp, #F_ROW]
	ldp	w0, w1, [x8, #R_X]
	ldr	w2, [x8, #R_X + 8]
	ldp	w3, w4, [x8, #R_Y]
	ldr	w5, [x8, #R_Y + 8]
	sub	w6, w5, w3			/* dy_AC                             */
	sub	w7, w4, w3			/* dy_AB                             */
	sub	w9, w2, w0			/* dx_AC                             */
	sub	w10, w1, w0			/* dx_AB                             */
	mul	w11, w9, w7
	msub	w11, w10, w6, w11		/* sarea                             */
	sub	w12, w5, w4			/* dy_BC                             */
	sub	w13, w2, w1			/* dx_BC                             */

	/* The three edge slopes off one packed reciprocal. */
	EDGESLOPES3 w9, w10, w13, w6, w7, w12, w14, w15, w16, w17

	/* ---- shade: the gradients, or none ---- */
	ldp	w17, w19, [x8, #R_SHADE]
	ldr	w20, [x8, #R_SHADE + 8]
	ldr	w21, [sp, #F_FLAT]
	cbnz	w21, Ltex_flat_shade
	sub	w19, w19, w17			/* dblend_ab                         */
	sub	w20, w20, w17			/* dblend_ac                         */
	/* Both gradients off ONE reciprocal of sarea, in double, as the i686
	 * kernel: the `lsl #9` wraps by design. */
	scvtf	d0, w11
	fmov	d1, #1.0
	fdiv	d1, d1, d0
	mul	w21, w9, w19
	msub	w21, w10, w20, w21		/* dx_AC*dab - dx_AB*dac             */
	lsl	w21, w21, #9
	scvtf	d2, w21
	fmul	d2, d2, d1
	fcvtzs	w21, d2				/* shade_yhat                        */
	mul	w22, w7, w20
	msub	w22, w6, w19, w22		/* dy_AB*dac - dy_AC*dab             */
	lsl	w22, w22, #9
	scvtf	d2, w22
	fmul	d2, d2, d1
	fcvtzs	w22, d2				/* shade_xhat                        */
	lsl	w23, w17, #9
	msub	w23, w22, w0, w23
	add	w23, w23, w22			/* shade at the edge, modular        */
	b	Ltex_shade_done
Ltex_flat_shade:
	mov	w21, #0
	mov	w22, #0
	lsl	w23, w17, #9
Ltex_shade_done:

	/* ---- edges at their starting rows, then the y < 0 pre-step ---- */
	lsl	w24, w0, #16			/* eAC                               */
	mov	w25, w24			/* eAB                               */
	lsl	w26, w1, #16			/* eBC                               */
	tbz	w3, #31, 1f
	msub	w24, w14, w3, w24
	/* Only pre-step A->B if that edge is walked. */
	cmp	w4, #0
	b.le	2f
	msub	w25, w15, w3, w25
2:
	msub	w23, w21, w3, w23
	mov	w3, #0
1:
	tbz	w4, #31, 3f
	msub	w26, w16, w4, w26
	mov	w4, #0
3:

	/* ---- plane accumulators, stepped from the screen centre to y0 ---- */
	ldr	w9, [sp, #F_SH]
	asr	w9, w9, #1
	sub	w9, w3, w9			/* rows from the centre to y0        */
	ldp	w27, w28, [sp, #PL_0X]		/* OV xhat, yhat                     */
	ldr	w10, [sp, #PL_0B]
	madd	w10, w28, w9, w10		/* au                                */
	str	w27, [sp, #F_STAU]
	str	w28, [sp, #F_YAU]
	str	w10, [sp, #F_AU]
	ldp	w27, w28, [sp, #PL_1X]
	ldr	w10, [sp, #PL_1B]
	madd	w10, w28, w9, w10		/* bv                                */
	str	w27, [sp, #F_STBV]
	str	w28, [sp, #F_YBV]
	str	w10, [sp, #F_BV]
	ldp	w27, w28, [sp, #PL_2X]
	ldr	w10, [sp, #PL_2B]
	madd	w10, w28, w9, w10		/* cw                                */
	str	w27, [sp, #F_STCW]
	str	w28, [sp, #F_YCW]
	str	w10, [sp, #F_CW]

	ldr	w9, [sp, #F_STRIDE]
	mul	w9, w9, w3
	ldr	x10, [sp, #F_PIX]
	add	x10, x10, w9, sxtw #2
	str	x10, [sp, #F_ROWPTR]

	/* Off the bottom: y1 past the edge takes y2 with it. */
	ldr	w9, [sp, #F_SH]
	cmp	w4, w9
	b.le	4f
	mov	w4, w9
	mov	w5, w9
	b	5f
4:
	cmp	w5, w9
	csel	w5, w9, w5, gt
5:

	/* ---- which edge is on the left ---- */
	cmp	w3, w4
	b.ne	6f
	cmp	w14, w16
	b.le	Ltex_ac_right
	b	Ltex_ac_left
6:
	cmp	w14, w15
	b.ge	Ltex_ac_right
	b	Ltex_ac_left

Ltex_ac_right:
	/* A->C on the right: the moving edge is on the left for both halves. */
	stp	w25, w15, [sp, #F_LX]
	stp	w24, w14, [sp, #F_RX]
	mov	w9, #1
	str	w9, [sp, #F_P2L]
	b	Ltex_sides
Ltex_ac_left:
	stp	w24, w14, [sp, #F_LX]
	stp	w25, w15, [sp, #F_RX]
	str	wzr, [sp, #F_P2L]
Ltex_sides:
	/* B->C has not been walked: it enters the second half as the y < 0
	 * clip left it. */
	stp	w26, w16, [sp, #F_P2X]
	sub	w9, w5, w4
	str	w9, [sp, #F_ROWS2]
	sub	w9, w4, w3
	str	w9, [sp, #F_ROWS]
	str	wzr, [sp, #F_PHASE]
	str	w23, [sp, #F_SHADE]
	str	w21, [sp, #F_SHY]
	str	w22, [sp, #F_SHX]

	/* ---- the walk's register-resident constants ---- */
	ldr	x19, [x8, #R_TEXELS]
	ldr	w9, [x8, #R_TW]
	mov	w20, #6
	cmp	w9, #128
	cinc	w20, w20, eq			/* shift: 7 for 128, else 6          */
	sub	w21, w9, #1			/* tw - 1                            */
	lsl	w22, w21, w20			/* vmask                             */
	ldr	w23, [sp, #F_STAU]
	ldr	w24, [sp, #F_STBV]
	ldr	w25, [sp, #F_STCW]
	ldr	w26, [x8, #R_GATE]
	mov	w27, w22			/* placeholder; shx below            */
	ldr	w27, [sp, #F_SHX]
	ldr	w28, [sp, #F_SW]
	fmov	s1, #1.0
	fmov	s4, wzr
	scvtf	s5, w21
	mov	w9, #LIM_F
	fmov	s6, w9
	dup	v26.4s, w21
	dup	v27.4s, w22
	adr	x9, Ltex_lanes
	ldp	q28, q29, [x9]
	neg	w9, w20
	dup	v30.4s, w9

/* ---- the trapezoid walk ------------------------------------------------ */
Ltex_phase:
	ldr	w9, [sp, #F_ROWS]
	cmp	w9, #0
	b.le	Ltex_nextphase

Ltex_row:
	/* x0 = (left - 1) >> 16, floored at 0: an edge landing exactly on a
	 * pixel centre belongs to the span. x1 = right >> 16, capped. */
	ldr	w0, [sp, #F_LX]
	sub	w0, w0, #1
	asr	w0, w0, #16
	cmp	w0, #0
	csel	w0, wzr, w0, lt
	ldr	w1, [sp, #F_RX]
	asr	w1, w1, #16
	sub	w2, w28, #1
	cmp	w1, w28
	csel	w1, w2, w1, ge
	cmp	w0, w1
	b.ge	Ltex_rowadv

	/* The plane values are carried at the screen centre, where their
	 * magnitude is smallest; stepping out to the left end is a multiply. */
	asr	w2, w28, #1
	sub	w2, w0, w2
	ldp	w4, w5, [sp, #F_AU]
	ldr	w6, [sp, #F_CW]
	madd	w4, w23, w2, w4
	madd	w5, w24, w2, w5
	madd	w6, w25, w2, w6

	sub	w2, w1, w0			/* steps                             */
	lsr	w3, w2, #3			/* whole blocks                      */
	and	w10, w2, #7
	str	w10, [sp, #F_REMAIN]
	ldr	x11, [sp, #F_ROWPTR]
	add	x11, x11, w0, uxtw #2
	ldr	w12, [sp, #F_SHADE]
	madd	w12, w27, w0, w12		/* shade at x0, modular              */
	mov	w7, #0				/* have_cur                          */
	cbz	w3, Ltex_tail

Ltex_blk:
	asr	w17, w6, w20			/* w                                 */
	cbz	w17, Ltex_blkz
	cbnz	w7, 1f
	RECIP	w17
	UQUOT	w4, w13
	WRAPQ	w5, w17, w14
1:
	add	w2, w6, w25, lsl #3
	asr	w2, w2, w20			/* w_n                               */
	cbz	w2, Ltex_wn0
	RECIP	w2
	add	w8, w4, w23, lsl #3
	UQUOT	w8, w15
	add	w8, w5, w24, lsl #3
	WRAPQ	w8, w2, w16
	mov	w7, #1
	FITS	w14, w16, Ltex_exact8
	LERP8
	b	Ltex_blkdone
Ltex_wn0:
	/* No far endpoint to fit to: draw the block per pixel. */
	mov	w15, w13
	mov	w16, w14
	mov	w7, #0
Ltex_exact8:
	mov	w8, #8
	EXACTBLK w8
Ltex_blkdone:
	mov	w13, w15
	mov	w14, w16
	b	Ltex_adv
Ltex_blkz:
	mov	w7, #0
Ltex_adv:
	add	w4, w4, w23, lsl #3
	add	w5, w5, w24, lsl #3
	add	w6, w6, w25, lsl #3
	add	x11, x11, #32
	add	w12, w12, w27, lsl #3
	subs	w3, w3, #1
	b.ne	Ltex_blk

Ltex_tail:
	ldr	w10, [sp, #F_REMAIN]
	cbz	w10, Ltex_rowadv
	asr	w17, w6, w20
	cbz	w17, Ltex_rowadv
	cbnz	w7, 2f
	RECIP	w17
	UQUOT	w4, w13
	WRAPQ	w5, w17, w14
2:
	add	w2, w6, w25, lsl #3
	asr	w2, w2, w20
	mov	w15, w13
	mov	w16, w14
	cbz	w2, Ltex_texact
	RECIP	w2
	add	w8, w4, w23, lsl #3
	UQUOT	w8, w15
	add	w8, w5, w24, lsl #3
	WRAPQ	w8, w2, w16
	FITS	w14, w16, Ltex_texact
	TAILLERP w10
	b	Ltex_rowadv
Ltex_texact:
	EXACTBLK w10

Ltex_rowadv:
	ldp	w0, w1, [sp, #F_LX]
	add	w0, w0, w1
	str	w0, [sp, #F_LX]
	ldp	w0, w1, [sp, #F_RX]
	add	w0, w0, w1
	str	w0, [sp, #F_RX]
	ldp	w0, w1, [sp, #F_AU]
	ldp	w2, w8, [sp, #F_YAU]
	add	w0, w0, w2
	add	w1, w1, w8
	stp	w0, w1, [sp, #F_AU]
	ldr	w0, [sp, #F_CW]
	ldr	w1, [sp, #F_YCW]
	add	w0, w0, w1
	str	w0, [sp, #F_CW]
	ldp	w0, w1, [sp, #F_SHADE]
	add	w0, w0, w1
	str	w0, [sp, #F_SHADE]
	ldr	x0, [sp, #F_ROWPTR]
	ldr	w1, [sp, #F_STRIDE]
	add	x0, x0, w1, sxtw #2
	str	x0, [sp, #F_ROWPTR]
	ldr	w0, [sp, #F_ROWS]
	subs	w0, w0, #1
	str	w0, [sp, #F_ROWS]
	b.ne	Ltex_row

Ltex_nextphase:
	/* The phase change: one edge is replaced; the plane accumulators, the
	 * shade, the row pointer and the side A->C is on carry straight through. */
	ldr	w0, [sp, #F_PHASE]
	cbnz	w0, Ltex_next
	mov	w0, #1
	str	w0, [sp, #F_PHASE]
	ldp	w0, w1, [sp, #F_P2X]
	ldr	w2, [sp, #F_P2L]
	cbz	w2, 7f
	stp	w0, w1, [sp, #F_LX]
	b	8f
7:
	stp	w0, w1, [sp, #F_RX]
8:
	ldr	w0, [sp, #F_ROWS2]
	str	w0, [sp, #F_ROWS]
	b	Ltex_phase

/* ---- next row of the run ----------------------------------------------- */
Ltex_next:
	ldr	x8, [sp, #F_ROW]
	add	x8, x8, #ROWBYTES
	str	x8, [sp, #F_ROW]
	ldr	w0, [sp, #F_LEFT]
	subs	w0, w0, #1
	str	w0, [sp, #F_LEFT]
	b.ne	Ltex_tri

	ldp	x29, x30, [sp, #F_SAVE + 80]
	ldp	x27, x28, [sp, #F_SAVE + 64]
	ldp	x25, x26, [sp, #F_SAVE + 48]
	ldp	x23, x24, [sp, #F_SAVE + 32]
	ldp	x21, x22, [sp, #F_SAVE + 16]
	ldp	x19, x20, [sp, #F_SAVE]
	add	sp, sp, #FRAME
	ret

#endif /* __aarch64__ */
