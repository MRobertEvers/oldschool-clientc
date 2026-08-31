/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES   (i686 only)
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The perspective-textured triangle, batched, in handrolled A32 / Advanced
 * SIMD -- the armv7 twin of tri.tex.aarch64.S. Four RUN doors --
 * opaque/colour-keyed x blend/flat -- and one body, exactly as on the AArch64
 * lane: they cannot draw different triangles, only pay differently to reach
 * one.
 *
 * WHAT IS DIFFERENT FROM THE AArch64 KERNEL, AND WHY
 *
 * Everything tri.flat.aarch32.S says applies here -- the explicit frame, the
 * register-normalised inner loops, the untouched d8-d15 -- and this kernel
 * adds the two facts that make it the hardest of the four.
 *
 *   - THE CALL CLOBBERS EVERY VECTOR REGISTER THIS KERNEL MAY USE. The
 *     prologue calls ToriDraw_TexturePlanePrepare32, which stays in C for the
 *     reasons tex_tri_asm.h gives. Under AAPCS on A32 the callee-saved vector
 *     registers are d8-d15 and NOTHING ELSE: d0-d7 and d16-d31 -- that is all
 *     of q0-q3 and q8-q15, which is every register this file is allowed to
 *     touch -- are caller-saved. The AArch64 twin gets away with keeping its
 *     walk state in v16-v31 across the same call because AArch64's rule is
 *     different; here there is no register on either side of the call.
 *
 *     So the call is a hard fence. Everything that crosses it crosses it in
 *     the frame, and every vector value the walk uses is built AFTER it:
 *     EDGECONST's q12/q13 (which the AArch64 file also rebuilds, for the same
 *     reason), the two lane vectors, the three index masks and the four float
 *     constants. Before the call the only vector work is none at all -- the
 *     nine cross products of the uv plane are integer and land straight in
 *     the frame, which is where the C wants them anyway.
 *
 *   - q12/q13 ARE THE EDGE LADDER'S ONLY WHILE THE LADDER RUNS. On the flat
 *     and gouraud lanes EDGECONST is hoisted out of the run and q12/q13 are
 *     reserved for its whole length. Here EDGECONST is already per-triangle
 *     (the call ate it), and EDGESLOPES3 is the last thing that reads it, so
 *     q12/q13 are two more scratch registers for the walk below. That is not
 *     a liberty -- with q0-q3 and q8-q15 the walk needs six scratch quads for
 *     the eight-pixel block, and without those two it does not have them.
 *
 *   - THE EXACT DIVIDES ARE f64, NOT sdiv. The span takes a real integer
 *     divide in two places -- tex_span_wrapped_quotient's fallback and every
 *     pixel of tex_span_exact_block -- and unlike the edge ladder's exact
 *     lane, these are on the SHIPPING path. `sdiv` is not in the ARMv7-A base
 *     instruction set (see edge_slopes_aarch32.inc); Cortex-A8 and A9 have
 *     NEON and no integer divide at all, so a kernel that used it would fault
 *     on parts this lane is meant to run on.
 *
 *     A double divide is exact here and needs nothing beyond the VFPv3-D16
 *     that armeabi-v7a already mandates. Both operands are int32 and convert
 *     to double exactly; vdiv.f64 is correctly rounded; and the rounding can
 *     never cross an integer boundary, because |q - k| >= 1/|b| for any
 *     integer k the quotient is not equal to, while half an ulp of q is
 *     |a| / (|b| * 2^53) -- so a wrong truncation would need |a| > 2^53,
 *     and |a| < 2^31. vcvt.s32.f64 truncates toward zero, as C does.
 *
 *   - NO WIDEN OR NARROW HAPPENS IN PLACE. The AArch64 file round-trips a
 *     register through uxtl/uqxtn; A32's VMOVL and VQMOVN do not guarantee an
 *     overlapping source and destination, so the texels travel q13/q14 ->
 *     q11/q15 -> q0 on the way through the blend.
 *
 *   - THE SCALAR FLOAT WORK IS CONFINED TO q0/q1. On A32 the S registers
 *     alias d0-d15 only, so `vcvt.f32.s32`, `vmul.f32 s,s,s`, `vcmp` and
 *     `vdiv.f64` can reach no further than q0-q3 -- and q4-q7 are the
 *     callee-saved half of that, which this file never touches. q0 holds 1/w
 *     and the scratch, q1 the three float constants; the clamp is a NEON
 *     vmax/vmin on d1 against d2/d3 because A32 has no scalar fmax/fmin at
 *     all. The dead lane 1 of every one of those is deliberate.
 *
 *   - THE 8-PIXEL EXACT BLOCK STEPS THE ACCUMULATORS IN PLACE. AArch64 copies
 *     au/bv/cw into three spare registers and advances the originals
 *     afterwards; there are no three spare registers here. Eight iterations
 *     of `au += step_au` IS `au += step_au << 3`, so the exact block walks
 *     r4/r5/r6 and the destination directly and rejoins the loop below the
 *     advance. The ragged tail's exact block does the same and then returns,
 *     where the difference cannot be observed.
 *
 * EXACTNESS. Scored against raster_texshadeblend_persp_tex{opaque,trans}_
 * branching_lerp8_v3 framebuffer against framebuffer with guard bands
 * (toridraw_presorted_neon_test.c). The packed edge slopes, the reciprocal of
 * w and the shared reciprocal of sarea are the three approximations the
 * AArch64 and i686 kernels take, and the three this one takes; everything
 * else is the C's own operations in the C's own order.
 *
 * ROW LAYOUT -- AND IT IS NOT THE AArch64 ONE. `rows` is `count` records of
 * TORIDRAW_RASTER_TEXBATCH_ROW_INTS (24) ints: three screen x, three screen
 * y, the nine orthographic coordinates of the texture frame (uv origin, u
 * end, v end -- three ROLES, never permuted with the corners), three shades,
 * then the texel pointer, the texture width and the colour-key gate. The
 * pointer takes ONE lane on a 32-bit host and two on LP64, so tw and the gate
 * sit at lanes 19 and 20 here where tri.tex.aarch64.S has them at 20 and 21
 * -- see tex_tri_asm.h, which switches the layout on pointer width. The x, y
 * and shade triples arrive ALREADY in y order.
 *
 * AAPCS. r4-r11 and lr are saved; d8-d15 are untouched. The frame is sized so
 * that sp is 8-byte aligned at the one call.
 */

#if defined(__arm__) && !defined(__aarch64__) && ( defined(__ARM_NEON) || defined(__ARM_NEON__) )

#define SYM(name) name

/* The staged row -- ILP32 lane numbering; see the header. */
#define R_X       0
#define R_Y       12
#define R_OX      24
#define R_OY      36
#define R_OZ      48
#define R_SHADE   60
#define R_TEXELS  72
#define R_TW      76
#define R_GATE    80
#define ROWBYTES  96

/*
 * The frame. Slot 0 is the plane struct handed to the C prepare, so the call
 * takes the plane pointer as a bare `mov r0, sp`.
 */
#define F_PLANE     0	/* term[3] of {x, y, z, base}, then shift: 52 bytes   */
#define PL_0X       0
#define PL_0Y       4
#define PL_0Z       8
#define PL_0B      12
#define PL_1X      16
#define PL_1Y      20
#define PL_1Z      24
#define PL_1B      28
#define PL_2X      32
#define PL_2Y      36
#define PL_2Z      40
#define PL_2B      44
#define PL_SH      48
#define F_PIX      52	/* the seven arguments, out of the call's way        */
#define F_STRIDE   56
#define F_STRIDEB  60	/* stride * 4, in bytes -- the row advance           */
#define F_SW       64
#define F_SH       68
#define F_COT      72
#define F_ROW      76	/* the staged row being rasterised                   */
#define F_LEFT     80	/* rows still to go                                  */
#define F_FLAT     84	/* 1 = one shade for the face, no gradient           */
#define F_GATE     88	/* 0 = every texel drawn, 1 = RGB 0 keys             */
#define F_LX       92	/* the trapezoid walk: LEFT and RIGHT are slots      */
#define F_LS       96
#define F_RX      100
#define F_RS      104
#define F_AU      108	/* plane accumulators at the row's screen centre     */
#define F_BV      112
#define F_CW      116
#define F_YAU     120	/* and their per-row steps                           */
#define F_YBV     124
#define F_YCW     128
#define F_SHADE   132	/* shade at the row's left screen edge               */
#define F_SHY     136
#define F_SHX     140
#define F_ROWPTR  144	/* pixel pointer of the row                          */
#define F_ROWS    148	/* rows left in the phase being walked               */
#define F_ROWS2   152	/* rows in the second phase                          */
#define F_P2X     156	/* the edge that replaces one side at the phase change */
#define F_P2S     160
#define F_P2L     164	/* nonzero: it replaces LEFT; zero: RIGHT            */
#define F_PHASE   168
#define F_STAU    172	/* per-pixel plane steps: the xhat normals           */
#define F_STBV    176
#define F_STCW    180
#define F_SAU8    184	/* and the same three shifted left by three, which   */
#define F_SBV8    188	/*   is what a block advances by                     */
#define F_SCW8    192
#define F_SHX8    196
#define F_SHIFT   200
#define F_TWM1    204
#define F_VMASK   208
#define F_TEXELS  212
#define F_REMAIN  216	/* the row's ragged tail, 0..7                       */
#define F_SAC     220	/* the three 16.16 edge steps                        */
#define F_SAB     224
#define F_SBC     228
/*
 * 236, not 232: `push {r4-r11, lr}` is nine words, so an 8-aligned FRAMESZ
 * would leave sp 4 off at the call. AAPCS wants it 8-aligned there and the C
 * prepare is entitled to assume it.
 */
#define FRAMESZ   236

/* Above the frame AND above the nine saved words. */
#define A_COT    (FRAMESZ + 36)
#define A_ROWS   (FRAMESZ + 40)
#define A_COUNT  (FRAMESZ + 44)

/* 2097152.0f: TEX_SPAN_RECIPROCAL_EXACT_LIMIT. */
#define LIM_F     0x4A000000

#include "graphics/raster/edge_slopes_aarch32.inc"

	.syntax unified
	.arm
	.fpu neon
	.text
	.p2align 2

/*
 * Register map during the walk. Nothing survives in an ARM register from one
 * triangle to the next; the per-triangle constants a segment would keep in a
 * register on AArch64 (texels, shift, tw-1, the gate, the three plane steps)
 * live in the frame and are reloaded where they are read, which for all but
 * `shift` is once per block.
 *
 *   r0, r1, r2  scratch      r3 blocks left in the row
 *   r4 au   r5 bv   r6 cw    r7 have_cur
 *   r8 cur_u   r9 cur_v   r10 nxt_u   r11 nxt_v
 *   r12 destination          lr shade, ish8
 *
 *   s0  1/w        s1, s2, s3  scratch
 *   s4  0.0f       s5  2097152.0f      s6  (float)(tw - 1)      s7 scratch
 *   q2  {0,1,2,3}  q3  {4,5,6,7}
 *   q8  (tw-1) x4  q9  vmask x4        q10 -shift x4
 *   q0, q11, q12, q13, q14, q15 scratch
 *
 * q4-q7 (d8-d15) are the callee-saved vector registers under AAPCS and are
 * deliberately untouched, here and in all four kernels.
 */

/*
 * An exact int32 divide, truncating toward zero, out of a correctly rounded
 * double divide -- see the header for why this is exact and why it is not
 * `sdiv`. Clobbers d0 and d1, so 1/w must be spent; every caller has spent it.
 */
.macro DIVEXACT n, w, out
	vmov	s2, \n
	vcvt.f64.s32	d0, s2
	vmov	s7, \w
	vcvt.f64.s32	d1, s7			/* s7, not s2: d1 IS s2/s3           */
	vdiv.f64	d0, d0, d1
	vcvt.s32.f64	s2, d0			/* truncates toward zero, as C does  */
	vmov	\out, s2
.endm

/*
 * 1 / w into s0, the i686 kernel's RECIP: an estimate and Newton-Raphson, not
 * a divide. vrecpe is 8 bits where rcpss was 12, so two steps where the i686
 * took one. w is nonzero here.
 *
 * A32 has no scalar reciprocal estimate -- vrecpe/vrecps are NEON and want a
 * d register -- so the ladder runs on d0 against q15's halves and lane 1 goes
 * along for the ride. That is the only difference from the AArch64 macro, and
 * it is the same divergence edge_slopes_aarch32.inc measured: A32's vrecps
 * rounds the product before subtracting where A64's frecps fuses, so the two
 * disagree in the last place and are equally accurate.
 */
.macro RECIP w
	vmov	s0, \w
	vcvt.f32.s32	s0, s0
#ifdef TORIDRAW_TEX_RECIP_DIV
	/* the C's own divide, for telling the walk from the estimate */
	vmov.f32	s1, #1.0
	vdiv.f32	s0, s1, s0
#else
	vrecpe.f32	d30, d0
	vrecps.f32	d31, d0, d30
	vmul.f32	d30, d30, d31
	vrecps.f32	d31, d0, d30
	vmul.f32	d0, d30, d31
#endif
.endm

/*
 * tex_span_u_quotient: q = n * inv_w, clamped INTO [0, tw-1] in float, then
 * converted -- the order matters, an out-of-range conversion is not what the
 * C does.
 *
 * The two bounds are a NEON vmax/vmin on d1 rather than the AArch64 file's
 * scalar fmax/fmin, which A32 does not have at any width. That is why 0.0f
 * and (float)(tw-1) sit in the LOW lane of two different d registers: one
 * vmax and one vmin can each only reach lane 0 of its operand. Lane 1 does
 * the same arithmetic on scratch and is thrown away.
 */
.macro UQUOT n, out
	vmov	s2, \n
	vcvt.f32.s32	s2, s2
	vmul.f32	s2, s2, s0
	vmax.f32	d1, d1, d2		/* max(q, 0.0f)                      */
	vmin.f32	d1, d1, d3		/* min(q, (float)(tw - 1))           */
	vcvt.s32.f32	s2, s2
	vmov	\out, s2
.endm

/*
 * tex_span_wrapped_quotient: trust the float reciprocal only while it still
 * carries the low bits that select the texel row, else the C's own `n / w`.
 */
.macro WRAPQ n, w, out
	vmov	s2, \n
	vcvt.f32.s32	s2, s2
	vmul.f32	s2, s2, s0
	vabs.f32	s3, s2
	vcmp.f32	s3, s5
	vmrs	APSR_nzcv, fpscr
	bge	88f
	vcvt.s32.f32	s2, s2
	vmov	\out, s2
	b	89f
88:
	DIVEXACT \n, \w, \out
89:
.endm

/*
 * tex_span_lerp8_fits: both endpoints inside +-2^29 and the block sweeping no
 * more than one texture tile in v. Branches to \fail otherwise.
 * Clobbers r0, r1.
 */
.macro FITS cur, nxt, fail
	mov	r0, #0x20000000
	add	r1, \cur, r0
	cmp	r1, r0, lsl #1
	bhi	\fail
	add	r1, \nxt, r0
	cmp	r1, r0, lsl #1
	bhi	\fail
	sub	r1, \nxt, \cur
	ldr	r0, [sp, #F_TWM1]
	add	r1, r1, r0
	add	r1, r1, #1			/* delta + tw                        */
	add	r0, r0, r0
	add	r0, r0, #2			/* 2 * tw                            */
	cmp	r1, r0
	bhi	\fail
.endm

/*
 * shade_blend, scalar, for the exact block and the ragged tail: the C's masked
 * u32 multiplies, which wrap where the vector form saturates. \c the texel,
 * receives the result; \t scratch; \sh the shade.
 *
 * Four of the AArch64 file's six masks are not ARM data-processing immediates
 * -- 0x00FF00FF and 0xFF00FF00 cannot be made from eight bits and an even
 * rotate -- so each is asked as the two `bic`s of its complement's halves,
 * which are. Same bits, no constant register, and this is the cold path.
 */
.macro SHADEBLEND c, t, sh
	bic	\t, \c, #0xFF000000
	bic	\t, \t, #0x0000FF00		/* t = c & 0x00FF00FF                */
	and	\c, \c, #0x0000FF00
	mul	\t, \t, \sh
	mul	\c, \c, \sh
	bic	\t, \t, #0x00FF0000
	bic	\t, \t, #0x000000FF		/* t &= 0xFF00FF00                   */
	and	\c, \c, #0x00FF0000
	orr	\c, \t, \c
	lsr	\c, \c, #8
.endm

/*
 * tex_span_exact_block: \count pixels from r12, an exact divide per pixel, the
 * same mapping the reference rasterizer uses for every pixel. Cold.
 *
 * Unlike the AArch64 macro this does NOT copy the accumulators: it steps r4,
 * r5, r6 and r12 in place, which after eight iterations leaves exactly what
 * the block advance would have written, so the 8-pixel caller rejoins the loop
 * below that advance (see the header). r8/r9 are free here because they hold
 * cur_u/cur_v, and every path out of this macro overwrites those with nxt.
 * Clobbers r0, r1, r2, r8, r9.
 */
.macro EXACTBLK count
	mov	r8, \count
	asr	r9, lr, #8			/* the block's shade                 */
91:
	ldr	r0, [sp, #F_SHIFT]
	asr	r1, r6, r0			/* w                                 */
	cmp	r1, #0
	beq	93f
	DIVEXACT r4, r1, r2		/* clamp(au / w, 0, tw - 1)          */
	ldr	r0, [sp, #F_TWM1]
	cmp	r2, #0
	movlt	r2, #0
	cmp	r2, r0
	movgt	r2, r0
	DIVEXACT r5, r1, r1		/* (bv / w) & (tw - 1), then the row */
	and	r1, r1, r0
	ldr	r0, [sp, #F_SHIFT]
	add	r2, r2, r1, lsl r0
	ldr	r0, [sp, #F_TEXELS]
	ldr	r2, [r0, r2, lsl #2]
	ldr	r0, [sp, #F_GATE]
	cmp	r0, #0
	beq	92f
	cmp	r2, #0
	beq	93f				/* the colour key, on the raw texel  */
92:
	SHADEBLEND r2, r1, r9
	str	r2, [r12]
93:
	add	r12, r12, #4
	ldr	r0, [sp, #F_STAU]
	add	r4, r4, r0
	ldr	r0, [sp, #F_STBV]
	add	r5, r5, r0
	ldr	r0, [sp, #F_STCW]
	add	r6, r6, r0
	subs	r8, r8, #1
	bne	91b
.endm

/*
 * The eight-pixel linear block at r12, between cur (r8, r9) and nxt (r10,
 * r11). Indices as two vectors, eight lane loads, two blends, the colour key
 * as a select against the destination.
 *
 * The register budget is the whole shape of this macro. Six scratch quads
 * exist -- q0 (1/w is spent by now) plus q11-q15 -- so the u lanes are built
 * and folded down to two registers BEFORE the v lanes start, and the two
 * halves of the blend are carried through the store one at a time rather than
 * both being live at once as they are on AArch64. The store cursor is a copy
 * in r0 so that r12 still points at the block for the advance below.
 * Clobbers r0, r1, r2, q0, q11-q15.
 */
.macro LERP8
	ldr	r0, [sp, #F_SHIFT]
	sub	r2, r0, #3
	sub	r1, r10, r8
	lsl	r1, r1, r2			/* s_u = (nxt_u - cur_u) << (shift-3)*/
	vdup.32	q12, r1
	sub	r1, r11, r9
	lsl	r1, r1, r2			/* s_v                               */
	vdup.32	q14, r1
	lsl	r1, r8, r0			/* u_scan = cur_u << shift           */
	vdup.32	q11, r1
	ldr	r2, [sp, #F_TWM1]
	and	r1, r9, r2
	lsl	r1, r1, r0			/* v_scan = (cur_v & (tw-1)) << shift*/
	vdup.32	q13, r1

	vmov	q15, q11
	vmla.i32	q15, q12, q2		/* u_scan + s_u * {0,1,2,3}          */
	vmla.i32	q11, q12, q3		/* u_scan + s_u * {4,5,6,7}          */
	vshl.s32	q15, q15, q10		/* u >> shift (arithmetic)           */
	vshl.s32	q11, q11, q10
	vand	q15, q15, q8			/* & (tw - 1)                        */
	vand	q11, q11, q8
	vmov	q0, q13
	vmla.i32	q0, q14, q2
	vmla.i32	q13, q14, q3
	vand	q0, q0, q9			/* v & vmask                         */
	vand	q13, q13, q9
	vadd.i32	q15, q15, q0		/* texel indices 0..3                */
	vadd.i32	q11, q11, q13		/* 4..7                              */

	ldr	r0, [sp, #F_TEXELS]
	vmov.32	r1, d30[0]
	add	r1, r0, r1, lsl #2
	vld1.32	{d26[0]}, [r1]
	vmov.32	r1, d30[1]
	add	r1, r0, r1, lsl #2
	vld1.32	{d26[1]}, [r1]
	vmov.32	r1, d31[0]
	add	r1, r0, r1, lsl #2
	vld1.32	{d27[0]}, [r1]
	vmov.32	r1, d31[1]
	add	r1, r0, r1, lsl #2
	vld1.32	{d27[1]}, [r1]
	vmov.32	r1, d22[0]
	add	r1, r0, r1, lsl #2
	vld1.32	{d28[0]}, [r1]
	vmov.32	r1, d22[1]
	add	r1, r0, r1, lsl #2
	vld1.32	{d28[1]}, [r1]
	vmov.32	r1, d23[0]
	add	r1, r0, r1, lsl #2
	vld1.32	{d29[0]}, [r1]
	vmov.32	r1, d23[1]
	add	r1, r0, r1, lsl #2
	vld1.32	{d29[1]}, [r1]

	asr	r1, lr, #8
	vdup.16	q12, r1				/* the block's shade                 */
	mov	r0, r12
	ldr	r2, [sp, #F_GATE]

	/* shade_blend4_neon on texels 0..3: widen to u16, multiply, shift,
	 * SATURATING narrow -- the C's vqmovn_u16. */
	vmovl.u8	q11, d26
	vmovl.u8	q15, d27
	vmul.i16	q11, q11, q12
	vmul.i16	q15, q15, q12
	vshr.u16	q11, q11, #8
	vshr.u16	q15, q15, #8
	vqmovn.u16	d0, q11
	vqmovn.u16	d1, q15
	cmp	r2, #0
	beq	94f
	/* Keyed: keep the destination wherever the RAW texel was zero. */
	vld1.32	{d22, d23}, [r0]
	vceq.i32	q15, q13, #0
	vbsl	q15, q11, q0
	vst1.32	{d30, d31}, [r0]!
	b	95f
94:
	vst1.32	{d0, d1}, [r0]!
95:
	vmovl.u8	q11, d28
	vmovl.u8	q15, d29
	vmul.i16	q11, q11, q12
	vmul.i16	q15, q15, q12
	vshr.u16	q11, q11, #8
	vshr.u16	q15, q15, #8
	vqmovn.u16	d0, q11
	vqmovn.u16	d1, q15
	cmp	r2, #0
	beq	96f
	vld1.32	{d22, d23}, [r0]
	vceq.i32	q15, q14, #0
	vbsl	q15, q11, q0
	vst1.32	{d30, d31}, [r0]
	b	97f
96:
	vst1.32	{d0, d1}, [r0]
97:
.endm

/*
 * The ragged tail, 1..7 pixels, along the linear fit: a scalar walk of the
 * same scan, the same colour key. \count pixels, in a register.
 *
 * Every register the span was carrying is dead by here except the destination
 * and the shade -- the tail is the last thing a row does -- so this one can
 * afford to keep its four scan values live in r3-r6 and only reload the masks.
 * It reads the count out of the frame rather than taking it as a parameter:
 * the four scan values are built from r8-r11, so a count handed over in a
 * register would have to survive that and there is nowhere left to put it.
 * Clobbers r0-r9.
 */
.macro TAILLERP
	ldr	r0, [sp, #F_SHIFT]
	sub	r2, r0, #3
	sub	r3, r10, r8
	lsl	r3, r3, r2			/* s_u                               */
	sub	r4, r11, r9
	lsl	r4, r4, r2			/* s_v                               */
	lsl	r5, r8, r0			/* u_scan                            */
	ldr	r2, [sp, #F_TWM1]
	and	r6, r9, r2
	lsl	r6, r6, r0			/* v_scan                            */
	mov	r7, r12				/* cursor                            */
	ldr	r8, [sp, #F_REMAIN]
	asr	r9, lr, #8
85:
	ldr	r0, [sp, #F_SHIFT]
	asr	r1, r5, r0
	ldr	r2, [sp, #F_TWM1]
	and	r1, r1, r2
	ldr	r2, [sp, #F_VMASK]
	and	r2, r6, r2
	add	r1, r1, r2
	ldr	r0, [sp, #F_TEXELS]
	ldr	r0, [r0, r1, lsl #2]
	ldr	r1, [sp, #F_GATE]
	cmp	r1, #0
	beq	86f
	cmp	r0, #0
	beq	87f
86:
	SHADEBLEND r0, r1, r9
	str	r0, [r7]
87:
	add	r7, r7, #4
	add	r5, r5, r3
	add	r6, r6, r4
	subs	r8, r8, #1
	bne	85b
.endm

/* ---- entries ---------------------------------------------------------- */

/*
 * void toridraw_textri_{opaque,trans,flat_opaque,flat_trans}_lerp8_v3_
 *     presorted_run_xrgb8888_asm(int* pixel_buffer, int stride,
 *     int screen_width, int screen_height, int camera_cot16,
 *     const int* rows, int count);
 *
 * Seven arguments: four in r0-r3 and three on the stack, which is why the
 * count is read at [sp, #8] before anything is pushed. The gate travels in the
 * row (lane 20), so a run may mix keyed and opaque faces and the opaque/trans
 * pairs of doors differ only in name. The flat doors stamp F_FLAT, which skips
 * the shade gradients.
 */
.macro TEXENTRY name, flat
	.globl	SYM(\name)
	.type	SYM(\name), %function
SYM(\name):
	ldr	r12, [sp, #8]			/* count                             */
	cmp	r12, #0
	ble	99f
	push	{r4-r11, lr}
	sub	sp, sp, #FRAMESZ
	str	r0, [sp, #F_PIX]
	str	r1, [sp, #F_STRIDE]
	str	r2, [sp, #F_SW]
	str	r3, [sp, #F_SH]
	lsl	r0, r1, #2
	str	r0, [sp, #F_STRIDEB]
	ldr	r0, [sp, #A_COT]
	str	r0, [sp, #F_COT]
	ldr	r0, [sp, #A_ROWS]
	str	r0, [sp, #F_ROW]
	ldr	r0, [sp, #A_COUNT]
	str	r0, [sp, #F_LEFT]
	mov	r0, #\flat
	str	r0, [sp, #F_FLAT]
	b	Ltex_tri
99:
	bx	lr
.endm

	TEXENTRY toridraw_textri_opaque_lerp8_v3_presorted_run_xrgb8888_asm, 0
	TEXENTRY toridraw_textri_trans_lerp8_v3_presorted_run_xrgb8888_asm, 0
	TEXENTRY toridraw_textri_flat_opaque_lerp8_v3_presorted_run_xrgb8888_asm, 1
	TEXENTRY toridraw_textri_flat_trans_lerp8_v3_presorted_run_xrgb8888_asm, 1

	.p2align 4
Ltex_lanes:
	.word	0, 1, 2, 3, 4, 5, 6, 7
/*
 * Reached PC-relative off a label difference the assembler folds, which is the
 * ARM32 equivalent of the AArch64 file's `adr x9, Ltex_lanes`. A bare `adr`
 * cannot do it: its offset is an eight-bit rotated immediate, and the code
 * that wants the table is several hundred bytes below.
 */
	.p2align 2
.Ltex_lanes_off:
	.word	Ltex_lanes - (.Ltex_lanes_pc + 8)

/* ---- one triangle ----------------------------------------------------- */
Ltex_tri:
	ldr	lr, [sp, #F_ROW]

	/* Off the top or off the bottom of the viewport. */
	ldr	r5, [lr, #R_Y + 8]		/* y2                                */
	cmp	r5, #0
	blt	Ltex_next
	ldr	r3, [lr, #R_Y + 0]		/* y0                                */
	ldr	r9, [sp, #F_SH]
	cmp	r3, r9
	bgt	Ltex_next

	ldr	r0, [lr, #R_X + 0]
	ldr	r1, [lr, #R_X + 4]
	ldr	r2, [lr, #R_X + 8]
	ldr	r4, [lr, #R_Y + 4]
	sub	r6, r5, r3			/* dy_AC                             */
	sub	r7, r4, r3			/* dy_AB                             */
	sub	r9, r2, r0			/* dx_AC                             */
	sub	r10, r1, r0			/* dx_AB                             */
	mul	r11, r9, r7
	mls	r11, r10, r6, r11		/* sarea = dx_AC*dy_AB - dx_AB*dy_AC */
	cmp	r11, #0
	beq	Ltex_next			/* degenerate: no area, no pixels    */

	/* ---- the uv plane: two edge vectors, three cross products ----
	 * All integer, and all of it lands in the frame -- which is where the C
	 * prepare below wants it, and the only place anything can be while the
	 * call is running. */
	ldr	r0, [lr, #R_OX + 0]		/* o_x                               */
	ldr	r3, [lr, #R_OX + 4]
	ldr	r6, [lr, #R_OX + 8]
	ldr	r1, [lr, #R_OY + 0]		/* o_y                               */
	ldr	r4, [lr, #R_OY + 4]
	ldr	r7, [lr, #R_OY + 8]
	ldr	r2, [lr, #R_OZ + 0]		/* o_z                               */
	ldr	r5, [lr, #R_OZ + 4]
	ldr	r8, [lr, #R_OZ + 8]
	sub	r3, r3, r0			/* vU_x                              */
	sub	r4, r4, r1			/* vU_y                              */
	sub	r5, r5, r2			/* vU_z                              */
	sub	r6, r6, r0			/* vV_x                              */
	sub	r7, r7, r1			/* vV_y                              */
	sub	r8, r8, r2			/* vV_z                              */
	/* U x V -> term[2] */
	mul	r9, r5, r7
	mls	r9, r4, r8, r9			/* vU_z*vV_y - vU_y*vV_z             */
	str	r9, [sp, #PL_2X]
	mul	r9, r3, r8
	mls	r9, r5, r6, r9			/* vU_x*vV_z - vU_z*vV_x             */
	str	r9, [sp, #PL_2Y]
	mul	r9, r4, r6
	mls	r9, r3, r7, r9			/* vU_y*vV_x - vU_x*vV_y             */
	str	r9, [sp, #PL_2Z]
	/* O x V -> term[0] */
	mul	r9, r1, r8
	mls	r9, r2, r7, r9			/* o_y*vV_z - o_z*vV_y               */
	str	r9, [sp, #PL_0X]
	mul	r9, r2, r6
	mls	r9, r0, r8, r9			/* o_z*vV_x - o_x*vV_z               */
	str	r9, [sp, #PL_0Y]
	mul	r9, r0, r7
	mls	r9, r1, r6, r9			/* o_x*vV_y - o_y*vV_x               */
	str	r9, [sp, #PL_0Z]
	/* U x O -> term[1] */
	mul	r9, r4, r2
	mls	r9, r5, r1, r9			/* vU_y*o_z - vU_z*o_y               */
	str	r9, [sp, #PL_1X]
	mul	r9, r5, r0
	mls	r9, r3, r2, r9			/* vU_z*o_x - vU_x*o_z               */
	str	r9, [sp, #PL_1Y]
	mul	r9, r3, r1
	mls	r9, r4, r0, r9			/* vU_x*o_y - vU_y*o_x               */
	str	r9, [sp, #PL_1Z]
	mov	r9, #0
	str	r9, [sp, #PL_0B]
	str	r9, [sp, #PL_1B]
	str	r9, [sp, #PL_2B]
	str	r9, [sp, #PL_SH]

	/* ToriDraw_TexturePlanePrepare32 stays in C -- see tex_tri_asm.h. */
	mov	r0, sp				/* the plane IS the frame's slot 0   */
	ldr	r1, [sp, #F_SW]
	ldr	r2, [sp, #F_SH]
	ldr	r3, [sp, #F_COT]
	bl	SYM(toridraw_texplane_prepare32_asm)
	cmp	r0, #0
	beq	Ltex_next

	/* ---- back from the call ----
	 * r0-r3, r12, lr and EVERY vector register this file may touch are gone;
	 * see the header. Nothing was being kept in one, and everything below
	 * rebuilds what the walk needs. EDGECONST goes first because it is what
	 * EDGESLOPES3 reads, and because it scratches r9. */
	EDGECONST
	ldr	r0, [sp, #F_ROW]
	ldr	r1, [r0, #R_X + 0]		/* x0                                */
	ldr	r2, [r0, #R_X + 4]		/* x1                                */
	ldr	r3, [r0, #R_X + 8]		/* x2                                */
	ldr	r4, [r0, #R_Y + 0]		/* y0                                */
	ldr	r5, [r0, #R_Y + 4]		/* y1                                */
	ldr	r6, [r0, #R_Y + 8]		/* y2                                */
	sub	r7, r6, r4			/* dy_AC                             */
	sub	r8, r5, r4			/* dy_AB                             */
	sub	r9, r3, r1			/* dx_AC                             */
	sub	r10, r2, r1			/* dx_AB                             */
	sub	r11, r6, r5			/* dy_BC                             */
	sub	r12, r3, r2			/* dx_BC                             */

	/* The three edge slopes, all three at once, off one packed reciprocal.
	 * The outputs alias the dx inputs, which both arms of the macro permit. */
	EDGESLOPES3 r9, r10, r12, r7, r8, r11, r9, r10, r12
	str	r9,  [sp, #F_SAC]
	str	r10, [sp, #F_SAB]
	str	r12, [sp, #F_SBC]

	/* ---- shade: the gradients, or none ----
	 * dx_AC and dx_AB are recomputed rather than saved: the slope ladder was
	 * handed them as its own outputs, and two subs cost less than the two
	 * frame words that would have kept them. */
	sub	r9, r3, r1			/* dx_AC                             */
	sub	r10, r2, r1			/* dx_AB                             */
	mul	r11, r9, r8
	mls	r11, r10, r7, r11		/* sarea                             */
	ldr	r12, [sp, #F_FLAT]
	ldr	r2, [r0, #R_SHADE + 0]
	cmp	r12, #0
	bne	Ltex_flat_shade
	ldr	r3, [r0, #R_SHADE + 4]
	ldr	r4, [r0, #R_SHADE + 8]
	sub	r3, r3, r2			/* dblend_ab                         */
	sub	r4, r4, r2			/* dblend_ac                         */
	/* Both gradients off ONE reciprocal of sarea, in double, as the i686 and
	 * AArch64 kernels: the `lsl #9` wraps by design. */
	vmov	s0, r11
	vcvt.f64.s32	d1, s0
	vmov.f64	d2, #1.0
	vdiv.f64	d1, d2, d1		/* 1.0 / sarea                       */
	mul	r5, r9, r3
	mls	r5, r10, r4, r5			/* dx_AC*dab - dx_AB*dac             */
	lsl	r5, r5, #9
	vmov	s0, r5
	vcvt.f64.s32	d3, s0
	vmul.f64	d3, d3, d1
	vcvt.s32.f64	s0, d3
	vmov	r5, s0				/* shade_yhat                        */
	mul	r6, r8, r4
	mls	r6, r7, r3, r6			/* dy_AB*dac - dy_AC*dab             */
	lsl	r6, r6, #9
	vmov	s0, r6
	vcvt.f64.s32	d3, s0
	vmul.f64	d3, d3, d1
	vcvt.s32.f64	s0, d3
	vmov	r6, s0				/* shade_xhat                        */
	lsl	r2, r2, #9
	mls	r2, r6, r1, r2
	add	r2, r2, r6			/* shade at the edge, modular        */
	b	Ltex_shade_done
Ltex_flat_shade:
	mov	r5, #0
	mov	r6, #0
	lsl	r2, r2, #9
Ltex_shade_done:
	str	r2, [sp, #F_SHADE]
	str	r5, [sp, #F_SHY]
	str	r6, [sp, #F_SHX]

	/* ---- edges at their starting rows, then the y < 0 pre-step ---- */
	ldr	r1, [r0, #R_X + 0]		/* x0                                */
	ldr	r2, [r0, #R_X + 4]		/* x1                                */
	ldr	r3, [r0, #R_Y + 0]		/* y0                                */
	ldr	r4, [r0, #R_Y + 4]		/* y1                                */
	ldr	r5, [r0, #R_Y + 8]		/* y2                                */
	ldr	r7, [sp, #F_SAC]
	ldr	r8, [sp, #F_SAB]
	ldr	r9, [sp, #F_SBC]
	lsl	r10, r1, #16			/* eAC                               */
	mov	r11, r10			/* eAB                               */
	lsl	r12, r2, #16			/* eBC                               */
	cmp	r3, #0
	bge	1f
	mls	r10, r7, r3, r10		/* eAC -= sAC * y0                   */
	/* Only pre-step A->B if that edge is actually walked. */
	cmp	r4, #0
	ble	2f
	mls	r11, r8, r3, r11
2:
	ldr	r1, [sp, #F_SHADE]
	ldr	r2, [sp, #F_SHY]
	mls	r1, r2, r3, r1			/* shade -= shy * y0                 */
	str	r1, [sp, #F_SHADE]
	mov	r3, #0
1:
	cmp	r4, #0
	bge	3f
	mls	r12, r9, r4, r12		/* eBC -= sBC * y1                   */
	mov	r4, #0
3:

	/* ---- plane accumulators, stepped from the screen centre to y0 ---- */
	ldr	r1, [sp, #F_SH]
	asr	r1, r1, #1
	sub	r1, r3, r1			/* rows from the centre to y0        */
	ldr	r2, [sp, #PL_0Y]
	ldr	r6, [sp, #PL_0B]
	mla	r6, r2, r1, r6			/* au                                */
	str	r6, [sp, #F_AU]
	str	r2, [sp, #F_YAU]
	ldr	r2, [sp, #PL_0X]
	str	r2, [sp, #F_STAU]
	ldr	r2, [sp, #PL_1Y]
	ldr	r6, [sp, #PL_1B]
	mla	r6, r2, r1, r6			/* bv                                */
	str	r6, [sp, #F_BV]
	str	r2, [sp, #F_YBV]
	ldr	r2, [sp, #PL_1X]
	str	r2, [sp, #F_STBV]
	ldr	r2, [sp, #PL_2Y]
	ldr	r6, [sp, #PL_2B]
	mla	r6, r2, r1, r6			/* cw                                */
	str	r6, [sp, #F_CW]
	str	r2, [sp, #F_YCW]
	ldr	r2, [sp, #PL_2X]
	str	r2, [sp, #F_STCW]

	ldr	r1, [sp, #F_STRIDE]
	mul	r1, r1, r3
	ldr	r2, [sp, #F_PIX]
	add	r2, r2, r1, lsl #2
	str	r2, [sp, #F_ROWPTR]

	/* Off the bottom: y1 past the edge takes y2 with it. */
	ldr	r1, [sp, #F_SH]
	cmp	r4, r1
	ble	4f
	mov	r4, r1
	mov	r5, r1
	b	5f
4:
	cmp	r5, r1
	movgt	r5, r1
5:

	/* ---- which edge is on the left ---- */
	cmp	r3, r4
	bne	6f
	cmp	r7, r9
	ble	Ltex_ac_right
	b	Ltex_ac_left
6:
	cmp	r7, r8
	bge	Ltex_ac_right
	b	Ltex_ac_left

Ltex_ac_right:
	/* A->C on the right: the moving edge is on the left for both halves. */
	str	r11, [sp, #F_LX]
	str	r8,  [sp, #F_LS]
	str	r10, [sp, #F_RX]
	str	r7,  [sp, #F_RS]
	mov	r1, #1
	str	r1, [sp, #F_P2L]
	b	Ltex_sides
Ltex_ac_left:
	str	r10, [sp, #F_LX]
	str	r7,  [sp, #F_LS]
	str	r11, [sp, #F_RX]
	str	r8,  [sp, #F_RS]
	mov	r1, #0
	str	r1, [sp, #F_P2L]
Ltex_sides:
	/* B->C has not been walked: it enters the second half as the y < 0 clip
	 * left it. */
	str	r12, [sp, #F_P2X]
	str	r9,  [sp, #F_P2S]
	sub	r1, r5, r4
	str	r1, [sp, #F_ROWS2]
	sub	r1, r4, r3
	str	r1, [sp, #F_ROWS]
	mov	r1, #0
	str	r1, [sp, #F_PHASE]

	/* ---- the walk's constants, all of them rebuilt after the call ---- */
	ldr	r1, [r0, #R_TEXELS]		/* ONE lane on a 32-bit host         */
	str	r1, [sp, #F_TEXELS]
	ldr	r1, [r0, #R_GATE]
	str	r1, [sp, #F_GATE]
	ldr	r1, [r0, #R_TW]
	mov	r2, #6
	cmp	r1, #128
	moveq	r2, #7				/* shift: 7 for 128, else 6          */
	str	r2, [sp, #F_SHIFT]
	sub	r1, r1, #1			/* tw - 1                            */
	str	r1, [sp, #F_TWM1]
	lsl	r3, r1, r2			/* vmask                             */
	str	r3, [sp, #F_VMASK]
	vdup.32	q8, r1
	vdup.32	q9, r3
	rsb	r2, r2, #0
	vdup.32	q10, r2				/* -shift x4, for the vshl           */
	vmov	s6, r1
	vcvt.f32.s32	s6, s6			/* (float)(tw - 1)                   */
	mov	r2, #0
	vmov	s4, r2				/* 0.0f                              */
	mov	r2, #LIM_F
	vmov	s5, r2
	ldr	r2, .Ltex_lanes_off
.Ltex_lanes_pc:
	add	r2, pc, r2
	vld1.32	{d4, d5}, [r2]!			/* q2 = {0,1,2,3}                    */
	vld1.32	{d6, d7}, [r2]			/* q3 = {4,5,6,7}                    */
	ldr	r1, [sp, #F_STAU]
	lsl	r1, r1, #3
	str	r1, [sp, #F_SAU8]
	ldr	r1, [sp, #F_STBV]
	lsl	r1, r1, #3
	str	r1, [sp, #F_SBV8]
	ldr	r1, [sp, #F_STCW]
	lsl	r1, r1, #3
	str	r1, [sp, #F_SCW8]
	ldr	r1, [sp, #F_SHX]
	lsl	r1, r1, #3
	str	r1, [sp, #F_SHX8]

/* ---- the trapezoid walk ------------------------------------------------ */
Ltex_phase:
	ldr	r0, [sp, #F_ROWS]
	cmp	r0, #0
	ble	Ltex_nextphase

Ltex_row:
	/* x0 = (left - 1) >> 16, floored at 0: an edge landing exactly on a pixel
	 * centre belongs to the span. x1 = right >> 16, capped. */
	ldr	r0, [sp, #F_LX]
	sub	r0, r0, #1
	asr	r0, r0, #16
	cmp	r0, #0
	movlt	r0, #0
	ldr	r1, [sp, #F_RX]
	asr	r1, r1, #16
	ldr	r2, [sp, #F_SW]
	cmp	r1, r2
	subge	r1, r2, #1
	cmp	r0, r1
	bge	Ltex_rowadv

	/* The plane values are carried at the screen centre, where their
	 * magnitude is smallest; stepping out to the left end is a multiply. */
	asr	r2, r2, #1
	sub	r2, r0, r2
	ldr	r4, [sp, #F_AU]
	ldr	r3, [sp, #F_STAU]
	mla	r4, r3, r2, r4
	ldr	r5, [sp, #F_BV]
	ldr	r3, [sp, #F_STBV]
	mla	r5, r3, r2, r5
	ldr	r6, [sp, #F_CW]
	ldr	r3, [sp, #F_STCW]
	mla	r6, r3, r2, r6

	ldr	r12, [sp, #F_ROWPTR]
	add	r12, r12, r0, lsl #2
	ldr	lr, [sp, #F_SHADE]
	ldr	r2, [sp, #F_SHX]
	mla	lr, r2, r0, lr			/* shade at x0, modular              */

	sub	r2, r1, r0			/* steps                             */
	and	r1, r2, #7
	str	r1, [sp, #F_REMAIN]
	lsr	r3, r2, #3			/* whole blocks                      */
	mov	r7, #0				/* have_cur                          */
	cmp	r3, #0
	beq	Ltex_tail

Ltex_blk:
	ldr	r0, [sp, #F_SHIFT]
	asr	r1, r6, r0			/* w                                 */
	cmp	r1, #0
	beq	Ltex_blkz
	cmp	r7, #0
	bne	1f
	RECIP	r1
	UQUOT	r4, r8
	WRAPQ	r5, r1, r9
1:
	ldr	r0, [sp, #F_SHIFT]
	ldr	r2, [sp, #F_SCW8]
	add	r2, r6, r2
	asr	r2, r2, r0			/* w_n                               */
	cmp	r2, #0
	beq	Ltex_wn0
	RECIP	r2
	ldr	r0, [sp, #F_SAU8]
	add	r0, r4, r0
	UQUOT	r0, r10
	ldr	r0, [sp, #F_SBV8]
	add	r0, r5, r0
	WRAPQ	r0, r2, r11
	mov	r7, #1
	FITS	r9, r11, Ltex_exact8
	LERP8
	b	Ltex_blkdone
Ltex_wn0:
	/* No far endpoint to fit to: draw the block per pixel. */
	mov	r10, r8
	mov	r11, r9
	mov	r7, #0
Ltex_exact8:
	EXACTBLK #8
	/* r4/r5/r6 and r12 were stepped through the block, so this path rejoins
	 * BELOW the advance -- see the header. */
	mov	r8, r10
	mov	r9, r11
	b	Ltex_adv_shade
Ltex_blkdone:
	mov	r8, r10
	mov	r9, r11
	b	Ltex_adv
Ltex_blkz:
	mov	r7, #0
Ltex_adv:
	ldr	r0, [sp, #F_SAU8]
	add	r4, r4, r0
	ldr	r0, [sp, #F_SBV8]
	add	r5, r5, r0
	ldr	r0, [sp, #F_SCW8]
	add	r6, r6, r0
	add	r12, r12, #32
Ltex_adv_shade:
	ldr	r0, [sp, #F_SHX8]
	add	lr, lr, r0
	subs	r3, r3, #1
	bne	Ltex_blk

Ltex_tail:
	ldr	r0, [sp, #F_REMAIN]
	cmp	r0, #0
	beq	Ltex_rowadv
	ldr	r0, [sp, #F_SHIFT]
	asr	r1, r6, r0
	cmp	r1, #0
	beq	Ltex_rowadv
	cmp	r7, #0
	bne	7f
	RECIP	r1
	UQUOT	r4, r8
	WRAPQ	r5, r1, r9
7:
	ldr	r0, [sp, #F_SHIFT]
	ldr	r2, [sp, #F_SCW8]
	add	r2, r6, r2
	asr	r2, r2, r0
	mov	r10, r8
	mov	r11, r9
	cmp	r2, #0
	beq	Ltex_texact
	RECIP	r2
	ldr	r0, [sp, #F_SAU8]
	add	r0, r4, r0
	UQUOT	r0, r10
	ldr	r0, [sp, #F_SBV8]
	add	r0, r5, r0
	WRAPQ	r0, r2, r11
	FITS	r9, r11, Ltex_texact
	TAILLERP
	b	Ltex_rowadv
Ltex_texact:
	ldr	r0, [sp, #F_REMAIN]
	EXACTBLK r0

Ltex_rowadv:
	ldr	r0, [sp, #F_LX]
	ldr	r1, [sp, #F_LS]
	add	r0, r0, r1
	str	r0, [sp, #F_LX]
	ldr	r0, [sp, #F_RX]
	ldr	r1, [sp, #F_RS]
	add	r0, r0, r1
	str	r0, [sp, #F_RX]
	ldr	r0, [sp, #F_AU]
	ldr	r1, [sp, #F_YAU]
	add	r0, r0, r1
	str	r0, [sp, #F_AU]
	ldr	r0, [sp, #F_BV]
	ldr	r1, [sp, #F_YBV]
	add	r0, r0, r1
	str	r0, [sp, #F_BV]
	ldr	r0, [sp, #F_CW]
	ldr	r1, [sp, #F_YCW]
	add	r0, r0, r1
	str	r0, [sp, #F_CW]
	ldr	r0, [sp, #F_SHADE]
	ldr	r1, [sp, #F_SHY]
	add	r0, r0, r1
	str	r0, [sp, #F_SHADE]
	ldr	r0, [sp, #F_ROWPTR]
	ldr	r1, [sp, #F_STRIDEB]
	add	r0, r0, r1
	str	r0, [sp, #F_ROWPTR]
	ldr	r0, [sp, #F_ROWS]
	subs	r0, r0, #1
	str	r0, [sp, #F_ROWS]
	bne	Ltex_row

Ltex_nextphase:
	/* The phase change: one edge is replaced; the plane accumulators, the
	 * shade, the row pointer and the side A->C is on carry straight through. */
	ldr	r0, [sp, #F_PHASE]
	cmp	r0, #0
	bne	Ltex_next
	mov	r0, #1
	str	r0, [sp, #F_PHASE]
	ldr	r0, [sp, #F_P2X]
	ldr	r1, [sp, #F_P2S]
	ldr	r2, [sp, #F_P2L]
	cmp	r2, #0
	beq	8f
	str	r0, [sp, #F_LX]
	str	r1, [sp, #F_LS]
	b	9f
8:
	str	r0, [sp, #F_RX]
	str	r1, [sp, #F_RS]
9:
	ldr	r0, [sp, #F_ROWS2]
	str	r0, [sp, #F_ROWS]
	b	Ltex_phase

/* ---- next row of the run ----------------------------------------------- */
Ltex_next:
	ldr	r0, [sp, #F_ROW]
	add	r0, r0, #ROWBYTES
	str	r0, [sp, #F_ROW]
	ldr	r0, [sp, #F_LEFT]
	subs	r0, r0, #1
	str	r0, [sp, #F_LEFT]
	bne	Ltex_tri

	add	sp, sp, #FRAMESZ
	pop	{r4-r11, pc}

#endif /* __arm__ && !__aarch64__ && __ARM_NEON */
