/*
 * toridraw_textri_opaque_lerp8_v3_asm -- hand-written i686 (cdecl, SSE2).
 *
 * The WHOLE perspective-textured triangle: the six-way vertex sort, the
 * per-triangle prologue, the trapezoid walk and the span fill, with no C
 * between the entry and the last pixel. It is the twin of
 * raster_texshadeblend_persp_texopaque_branching_lerp8_v3 in
 * texshadeblend.persp.texopaque.branching.lerp8_v3.u.c, which stays compiled
 * and stays the reference.
 *
 * WHY A WHOLE TRIANGLE AND NOT JUST A SPAN. There already is a hand-written
 * span -- tex_span_i686.S -- and it is fast. What it cannot avoid is being
 * CALLED. The C walk hands it fifteen arguments once per scanline, of which
 * nine are the same for every row of the triangle: the pixel buffer, the screen
 * width, the three per-pixel plane steps, the per-pixel shade step, the texel
 * pointer, the texture width and the width dispatch that last argument drives.
 * Fifteen pushes, a call and a return, then twelve stores re-deriving strides
 * that did not change, then a branch on a width that did not change either --
 * per row, for every row of every textured triangle in the frame. Hoisting all
 * of that out is what this file is for; the fill itself is the same code, and
 * deliberately so.
 *
 * The fill comes from span/tex_span_body.inc, which tex_span_i686.S includes
 * too. Neither file owns it. That is why the split exists: SPANBODY expands to
 * the largest block of code in the rasterizer and two copies would not share a
 * trace cache.
 *
 * ONE WALK, NOT FOUR. The C writes four row loops -- two trapezoid halves times
 * two answers to "which edge is on the left" -- each with an identical body.
 * Here the branch instead picks which edge goes in the LEFT slot and which in
 * the RIGHT, and the phase change swaps a single edge into whichever side the
 * branch said. What is left is one loop, expanded once per texture width, so
 * the whole kernel holds two copies of the fill rather than eight.
 *
 * ARITHMETIC IS UNCHANGED, ON PURPOSE. Every quotient here is the same idivl
 * the C emits, including the two shade gradients that share a divisor and could
 * therefore be one reciprocal and two multiplies -- the trade that landed in the
 * gouraud kernel. It is not taken here. That change was worth about 20 us of a
 * 39 ms Pentium 4 frame when it was measured, which is under the noise floor of
 * the only instrument that can see this box, and taking it would mean moving
 * the C twin in lockstep to keep toridraw_textri_asm_test bit-exact. The saving
 * this file is after is structural and does not need it.
 *
 * WHAT IS STILL C. ToriDraw_TexturePlanePrepare32, reached through the exported
 * wrapper in tex_tri_asm_support.h. It runs once per triangle, both of its
 * normalisation loops are normally zero iterations, and it maintains the
 * counters the raster debug line reports. Rewriting it would buy nothing and
 * would move those counters out of the C.
 *
 * ABLATION: like the span, this kernel does not honour TORIDRAW_ABLATE_TEX, and
 * the makefile withdraws it from any build that asks for the census or the
 * ablation modes. Those counters live in the C twin; measure them there.
 */

#include "span/tex_span_body.inc"

/* ------------------------------------------------------------------ */
/*  Frame. The span's slots 0..123 keep their offsets, because every    */
/*  macro in the include addresses them from esp and must not care      */
/*  which entry set esp. Triangle state starts above them.              */
/* ------------------------------------------------------------------ */

/* The trapezoid walk. LEFT and RIGHT are slots, not edges: which of A->B,
 * A->C and B->C lands in each is what the C's outer branch decides. */
#define T_LX     124            /* left  edge x, 16.16 */
#define T_LS     128            /* left  edge step per row */
#define T_RX     132
#define T_RS     136
#define T_AU     140            /* plane accumulators at the row's screen centre */
#define T_BV     144
#define T_CW     148
#define T_YAU    152            /* and their per-row steps: the yhat normals */
#define T_YBV    156
#define T_YCW    160
#define T_SHADE  164            /* shade at the row's left screen edge */
#define T_SHY    168            /* per-row shade step */
#define T_SHX    172            /* per-pixel shade step */
#define T_OFF    176            /* pixel offset of the row */
#define T_STRIDE 180
#define T_ROWS   184            /* rows left in the phase being walked */
#define T_ROWS2  188            /* rows in the second phase */
#define T_P2X    192            /* the edge that replaces one side at the phase change */
#define T_P2S    196
#define T_P2L    200            /* nonzero: it replaces LEFT; zero: RIGHT */
#define T_PHASE  204
#define T_DST    208
#define T_SW     212
#define T_SWH    216            /* screen_width >> 1 */
#define T_SWM1   220            /* screen_width - 1 */
#define T_SAC    224            /* the three edge slopes */
#define T_SAB    228
#define T_SBC    232

/* Prologue only. Dead once the walk starts, but not reused: the walk has no
 * spare frame pressure and overlapping them would make the next edit here a
 * debugging session. */
#define P_X0     236
#define P_X1     240
#define P_X2     244
#define P_Y0     248
#define P_Y1     252
#define P_Y2     256
#define P_S0     260
#define P_S1     264
#define P_S2     268
#define P_DYAC   272
#define P_DYAB   276
#define P_DXAC   280
#define P_DXAB   284
#define P_DYBC   288
#define P_DXBC   292
#define P_SAREA  296
#define P_DAB    300
#define P_DAC    304
#define P_EAC    308
#define P_EAB    312
#define P_EBC    316
#define P_UX     320
#define P_UY     324
#define P_UZ     328
#define P_VX     332
#define P_VY     336
#define P_VZ     340

/* struct ToriDraw_TexturePlane32: term[3] of {x, y, z, base} then shift.
 * Built here, handed to the C prepare, read back. 52 bytes. */
#define P_PLANE  344
#define PL_0X    (P_PLANE + 0)
#define PL_0Y    (P_PLANE + 4)
#define PL_0Z    (P_PLANE + 8)
#define PL_0B    (P_PLANE + 12)
#define PL_1X    (P_PLANE + 16)
#define PL_1Y    (P_PLANE + 20)
#define PL_1Z    (P_PLANE + 24)
#define PL_1B    (P_PLANE + 28)
#define PL_2X    (P_PLANE + 32)
#define PL_2Y    (P_PLANE + 36)
#define PL_2Z    (P_PLANE + 40)
#define PL_2B    (P_PLANE + 44)
#define PL_SH    (P_PLANE + 48)

#define TFRAME   400

/* Four pushes, then TFRAME, then the return address, then argument 0. */
#define T(n) (TFRAME + 20 + 4 * (n))(%esp)

/* Argument slots, by name, so the permutation macro below can do arithmetic
 * on the vertex index without anybody having to count pushes. */
#define A_PIX     0
#define A_STRIDE  1
#define A_SW      2
#define A_SH      3
#define A_COT     4
#define A_X       5            /* x0 x1 x2 */
#define A_Y       8            /* y0 y1 y2 */
#define A_OX      11           /* uvorigin_x0, uend_x1, vend_x2 */
#define A_OY      14
#define A_OZ      17
#define A_SHADE   20           /* shade7bit a b c */
#define A_TEXELS  23
#define A_TW      24

	.text

/*
 * Take the vertices in the order (a, b, c) and land them in the P_ slots.
 *
 * The C reaches the same place by calling the ordered function with its
 * arguments permuted, which costs it nothing because the permutation is in the
 * call. Here it is nine loads and nine stores, emitted six times and executed
 * once -- the alternative, a compare-exchange network over the slots, would run
 * three unpredictable branches and up to eighteen moves to save code the trace
 * cache never sees.
 */
	.macro VSORT a, b, c
	movl	T(A_X + \a), %eax
	movl	%eax, P_X0(%esp)
	movl	T(A_X + \b), %eax
	movl	%eax, P_X1(%esp)
	movl	T(A_X + \c), %eax
	movl	%eax, P_X2(%esp)
	movl	T(A_Y + \a), %eax
	movl	%eax, P_Y0(%esp)
	movl	T(A_Y + \b), %eax
	movl	%eax, P_Y1(%esp)
	movl	T(A_Y + \c), %eax
	movl	%eax, P_Y2(%esp)
	movl	T(A_SHADE + \a), %eax
	movl	%eax, P_S0(%esp)
	movl	T(A_SHADE + \b), %eax
	movl	%eax, P_S1(%esp)
	movl	T(A_SHADE + \c), %eax
	movl	%eax, P_S2(%esp)
	.endm

/*
 * One cross-product component: dst = p*q - r*s, all frame slots.
 */
	.macro CROSS p, q, r, s, dst
	movl	\p(%esp), %eax
	imull	\q(%esp), %eax
	movl	\r(%esp), %edx
	imull	\s(%esp), %edx
	subl	%edx, %eax
	movl	%eax, \dst(%esp)
	.endm

/*
 * The same, with two of the four factors read from arguments rather than
 * slots -- the uv origin never needed a slot of its own.
 */
	.macro CROSSA p, qarg, r, sarg, dst
	movl	T(\qarg), %eax
	imull	\p(%esp), %eax
	movl	T(\sarg), %edx
	imull	\r(%esp), %edx
	subl	%edx, %eax
	movl	%eax, \dst(%esp)
	.endm

/*
 * step = (dx << 16) / dy, or zero when the edge spans no rows.
 *
 * cltd then idivl is the 32-bit C divide exactly: the dividend is a
 * sign-extended 32-bit value and the divisor is at least one, so the quotient
 * cannot leave the range idivl can represent.
 */
	.macro EDGESTEP dy, dx, dst
	movl	$0, \dst(%esp)
	movl	\dy(%esp), %ecx
	testl	%ecx, %ecx
	jle	9f
	movl	\dx(%esp), %eax
	shll	$16, %eax
	cltd
	idivl	%ecx
	movl	%eax, \dst(%esp)
9:
	.endm

	.globl  _toridraw_textri_opaque_lerp8_v3_asm
	.def    _toridraw_textri_opaque_lerp8_v3_asm; .scl 2; .type 32; .endef

_toridraw_textri_opaque_lerp8_v3_asm:
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	pushl	%ebp
	subl	$TFRAME, %esp

	/* ---------------------------------------------------------------- */
	/* Sort the three (x, y, shade) triples by y, stably.                */
	/*                                                                   */
	/* The comparison chain is the C's, branch for branch, because the    */
	/* C's tie-breaks are load-bearing: every test is <=, so equal y's    */
	/* keep their original order, and a triangle that ties differently    */
	/* here would walk its trapezoid halves in the other order and stop   */
	/* tiling with its neighbour.                                        */
	/* ---------------------------------------------------------------- */
	movl	T(A_Y + 0), %eax
	movl	T(A_Y + 1), %ecx
	movl	T(A_Y + 2), %edx

	cmpl	%ecx, %eax
	jg	.Lnot_a
	cmpl	%edx, %eax
	jg	.Lnot_a
	cmpl	%edx, %ecx
	jg	1f
	VSORT	0, 1, 2
	jmp	.Lsorted
1:	VSORT	0, 2, 1
	jmp	.Lsorted

.Lnot_a:
	cmpl	%edx, %ecx
	jg	.Lc_first
	cmpl	%eax, %edx
	jg	2f
	VSORT	1, 2, 0
	jmp	.Lsorted
2:	VSORT	1, 0, 2
	jmp	.Lsorted

.Lc_first:
	cmpl	%ecx, %eax
	jg	3f
	VSORT	2, 0, 1
	jmp	.Lsorted
3:	VSORT	2, 1, 0

.Lsorted:
	/* Off the top or off the bottom of the viewport. */
	movl	P_Y2(%esp), %eax
	testl	%eax, %eax
	js	.Lret
	movl	P_Y0(%esp), %eax
	cmpl	T(A_SH), %eax
	jg	.Lret

	/* ---------------------------------------------------------------- */
	/* Edge deltas and the signed area.                                  */
	/* ---------------------------------------------------------------- */
	movl	P_Y2(%esp), %eax
	subl	P_Y0(%esp), %eax
	movl	%eax, P_DYAC(%esp)
	movl	P_Y1(%esp), %eax
	subl	P_Y0(%esp), %eax
	movl	%eax, P_DYAB(%esp)
	movl	P_X2(%esp), %eax
	subl	P_X0(%esp), %eax
	movl	%eax, P_DXAC(%esp)
	movl	P_X1(%esp), %eax
	subl	P_X0(%esp), %eax
	movl	%eax, P_DXAB(%esp)

	movl	P_DXAC(%esp), %eax
	imull	P_DYAB(%esp), %eax
	movl	P_DXAB(%esp), %edx
	imull	P_DYAC(%esp), %edx
	subl	%edx, %eax
	je	.Lret			/* degenerate: no area, no pixels */
	movl	%eax, P_SAREA(%esp)

	movl	P_Y2(%esp), %eax
	subl	P_Y1(%esp), %eax
	movl	%eax, P_DYBC(%esp)
	movl	P_X2(%esp), %eax
	subl	P_X1(%esp), %eax
	movl	%eax, P_DXBC(%esp)

	movl	P_S1(%esp), %eax
	subl	P_S0(%esp), %eax
	movl	%eax, P_DAB(%esp)
	movl	P_S2(%esp), %eax
	subl	P_S0(%esp), %eax
	movl	%eax, P_DAC(%esp)

	EDGESTEP P_DYAC, P_DXAC, T_SAC
	EDGESTEP P_DYAB, P_DXAB, T_SAB
	EDGESTEP P_DYBC, P_DXBC, T_SBC

	/* ---------------------------------------------------------------- */
	/* The uv plane. Two edge vectors in world space, then the three     */
	/* cross products that make A, B and C -- the homogeneous numerators */
	/* the span divides.                                                 */
	/* ---------------------------------------------------------------- */
	movl	T(A_OX + 1), %eax
	subl	T(A_OX + 0), %eax
	movl	%eax, P_UX(%esp)
	movl	T(A_OY + 1), %eax
	subl	T(A_OY + 0), %eax
	movl	%eax, P_UY(%esp)
	movl	T(A_OZ + 1), %eax
	subl	T(A_OZ + 0), %eax
	movl	%eax, P_UZ(%esp)

	movl	T(A_OX + 2), %eax
	subl	T(A_OX + 0), %eax
	movl	%eax, P_VX(%esp)
	movl	T(A_OY + 2), %eax
	subl	T(A_OY + 0), %eax
	movl	%eax, P_VY(%esp)
	movl	T(A_OZ + 2), %eax
	subl	T(A_OZ + 0), %eax
	movl	%eax, P_VZ(%esp)

	/* U x V */
	CROSS	P_UZ, P_VY, P_UY, P_VZ, PL_2X
	CROSS	P_UX, P_VZ, P_UZ, P_VX, PL_2Y
	CROSS	P_UY, P_VX, P_UX, P_VY, PL_2Z

	/* O x V */
	CROSSA	P_VZ, (A_OY + 0), P_VY, (A_OZ + 0), PL_0X
	CROSSA	P_VX, (A_OZ + 0), P_VZ, (A_OX + 0), PL_0Y
	CROSSA	P_VY, (A_OX + 0), P_VX, (A_OY + 0), PL_0Z

	/* U x O */
	CROSSA	P_UY, (A_OZ + 0), P_UZ, (A_OY + 0), PL_1X
	CROSSA	P_UZ, (A_OX + 0), P_UX, (A_OZ + 0), PL_1Y
	CROSSA	P_UX, (A_OY + 0), P_UY, (A_OX + 0), PL_1Z

	/* The C builds this struct with a designated initialiser, so the
	 * bases and the shift start at zero. Prepare overwrites all four on
	 * the path that returns true, but a debugger reading a rejected plane
	 * should see what the C would have left there. */
	movl	$0, PL_0B(%esp)
	movl	$0, PL_1B(%esp)
	movl	$0, PL_2B(%esp)
	movl	$0, PL_SH(%esp)

	/* ToriDraw_TexturePlanePrepare32 stays in C -- see the header. cdecl
	 * preserves ebx, esi, edi and ebp, and every value this kernel still
	 * needs is in the frame regardless. */
	movl	T(A_COT), %eax
	movl	T(A_SH), %ecx
	movl	T(A_SW), %edx
	leal	P_PLANE(%esp), %ebx
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	pushl	%ebx
	call	_toridraw_texplane_prepare32_asm
	addl	$16, %esp
	testl	%eax, %eax
	je	.Lret

	/* ---------------------------------------------------------------- */
	/* Shade gradients. Cramer's rule over the same sarea, shifted up by  */
	/* nine: one for 7-bit shades reaching 0..255, eight for the          */
	/* fractional part the span carries and shifts back off.              */
	/* ---------------------------------------------------------------- */
	movl	P_DAB(%esp), %eax
	imull	P_DXAC(%esp), %eax
	movl	P_DAC(%esp), %edx
	imull	P_DXAB(%esp), %edx
	subl	%edx, %eax
	shll	$9, %eax
	cltd
	idivl	P_SAREA(%esp)
	movl	%eax, T_SHY(%esp)

	movl	P_DAC(%esp), %eax
	imull	P_DYAB(%esp), %eax
	movl	P_DAB(%esp), %edx
	imull	P_DYAC(%esp), %edx
	subl	%edx, %eax
	shll	$9, %eax
	cltd
	idivl	P_SAREA(%esp)
	movl	%eax, T_SHX(%esp)

	/* Modular by design: the reference client relies on int wraparound
	 * here and an edge-on triangle reaches the overflow. Plain addl and
	 * subl ARE that wraparound; it is the C that needs int_wrap.h to say
	 * so without tripping the sanitiser. */
	movl	P_S0(%esp), %eax
	shll	$9, %eax
	movl	T_SHX(%esp), %edx
	imull	P_X0(%esp), %edx
	subl	%edx, %eax
	addl	T_SHX(%esp), %eax
	movl	%eax, T_SHADE(%esp)

	/* ---------------------------------------------------------------- */
	/* Edges at their starting rows, then the y < 0 pre-step.            */
	/* ---------------------------------------------------------------- */
	movl	P_X0(%esp), %eax
	shll	$16, %eax
	movl	%eax, P_EAC(%esp)
	movl	%eax, P_EAB(%esp)
	movl	P_X1(%esp), %eax
	shll	$16, %eax
	movl	%eax, P_EBC(%esp)

	movl	P_Y0(%esp), %ecx
	testl	%ecx, %ecx
	jns	4f
	movl	T_SAC(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, P_EAC(%esp)

	/* Only pre-step A->B if that edge is walked: rows [y0, y1) use it, so
	 * a y1 at or above the viewport leaves the span empty. A near-
	 * horizontal AB has a slope of hundreds of px per row, and times a
	 * distant y0 that overflows the 16.16 product for a result that is
	 * then discarded. A live edge cannot overflow -- dy >= |y0| bounds it
	 * by dx << 16. Full argument in raster/zbuffer/zbuf.screen.u.c. */
	cmpl	$0, P_Y1(%esp)
	jle	5f
	movl	T_SAB(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, P_EAB(%esp)
5:
	movl	T_SHY(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, T_SHADE(%esp)
	movl	$0, P_Y0(%esp)
4:
	movl	P_Y1(%esp), %ecx
	testl	%ecx, %ecx
	jns	6f
	movl	T_SBC(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, P_EBC(%esp)
	movl	$0, P_Y1(%esp)
6:

	/* ---------------------------------------------------------------- */
	/* Plane accumulators, stepped from the screen centre down to y0.    */
	/* ---------------------------------------------------------------- */
	movl	PL_0B(%esp), %eax
	movl	%eax, T_AU(%esp)
	movl	PL_1B(%esp), %eax
	movl	%eax, T_BV(%esp)
	movl	PL_2B(%esp), %eax
	movl	%eax, T_CW(%esp)

	movl	T(A_SH), %eax
	sarl	$1, %eax
	movl	P_Y0(%esp), %ecx
	subl	%eax, %ecx		/* rows from the screen centre to y0 */

	movl	PL_0Y(%esp), %eax
	movl	%eax, T_YAU(%esp)
	imull	%ecx, %eax
	addl	%eax, T_AU(%esp)
	movl	PL_1Y(%esp), %eax
	movl	%eax, T_YBV(%esp)
	imull	%ecx, %eax
	addl	%eax, T_BV(%esp)
	movl	PL_2Y(%esp), %eax
	movl	%eax, T_YCW(%esp)
	imull	%ecx, %eax
	addl	%eax, T_CW(%esp)

	movl	P_Y0(%esp), %eax
	imull	T(A_STRIDE), %eax
	movl	%eax, T_OFF(%esp)

	/* Off the bottom. y1 past the edge takes y2 with it -- the second
	 * trapezoid half starts at y1 and so cannot have any rows either. */
	movl	T(A_SH), %edx
	cmpl	%edx, P_Y1(%esp)
	jle	7f
	movl	%edx, P_Y1(%esp)
	movl	%edx, P_Y2(%esp)
	jmp	8f
7:	cmpl	%edx, P_Y2(%esp)
	jle	8f
	movl	%edx, P_Y2(%esp)
8:

	/* ---------------------------------------------------------------- */
	/* Which edge is on the left.                                        */
	/*                                                                   */
	/* A->C spans the whole triangle and is therefore one side for both   */
	/* halves; A->B and B->C take turns on the other. The test is the     */
	/* C's: when the first half has no rows at all there is no A->B slope */
	/* to compare against, so the flat-top case compares A->C to B->C     */
	/* instead.                                                          */
	/* ---------------------------------------------------------------- */
	movl	P_Y0(%esp), %eax
	cmpl	P_Y1(%esp), %eax
	jne	10f
	movl	T_SAC(%esp), %eax
	cmpl	T_SBC(%esp), %eax
	jle	.Lac_right
	jmp	.Lac_left
10:
	movl	T_SAC(%esp), %eax
	cmpl	T_SAB(%esp), %eax
	jge	.Lac_right
	jmp	.Lac_left

	/* A->C on the right: the moving edge is on the left for both halves. */
.Lac_right:
	movl	P_EAB(%esp), %eax
	movl	%eax, T_LX(%esp)
	movl	T_SAB(%esp), %eax
	movl	%eax, T_LS(%esp)
	movl	P_EAC(%esp), %eax
	movl	%eax, T_RX(%esp)
	movl	T_SAC(%esp), %eax
	movl	%eax, T_RS(%esp)
	movl	$1, T_P2L(%esp)
	jmp	.Lsides

	/* A->C on the left. */
.Lac_left:
	movl	P_EAC(%esp), %eax
	movl	%eax, T_LX(%esp)
	movl	T_SAC(%esp), %eax
	movl	%eax, T_LS(%esp)
	movl	P_EAB(%esp), %eax
	movl	%eax, T_RX(%esp)
	movl	T_SAB(%esp), %eax
	movl	%eax, T_RS(%esp)
	movl	$0, T_P2L(%esp)

.Lsides:
	/* B->C has not been walked, so it enters the second half at the value
	 * the y < 0 clip left it at. */
	movl	P_EBC(%esp), %eax
	movl	%eax, T_P2X(%esp)
	movl	T_SBC(%esp), %eax
	movl	%eax, T_P2S(%esp)

	movl	P_Y2(%esp), %eax
	subl	P_Y1(%esp), %eax
	movl	%eax, T_ROWS2(%esp)
	movl	P_Y1(%esp), %eax
	subl	P_Y0(%esp), %eax
	movl	%eax, T_ROWS(%esp)
	movl	$0, T_PHASE(%esp)

	/* ---------------------------------------------------------------- */
	/* The nine per-row invariants the C re-derived once per scanline.    */
	/* This is the whole point of the file.                              */
	/* ---------------------------------------------------------------- */
	movl	PL_0X(%esp), %eax
	movl	%eax, ST_AU(%esp)
	shll	$3, %eax
	movl	%eax, ST_AU8(%esp)
	addl	%eax, %eax
	movl	%eax, ST_AU16(%esp)

	movl	PL_1X(%esp), %eax
	movl	%eax, ST_BV(%esp)
	shll	$3, %eax
	movl	%eax, ST_BV8(%esp)
	addl	%eax, %eax
	movl	%eax, ST_BV16(%esp)

	movl	PL_2X(%esp), %eax
	movl	%eax, ST_CW(%esp)
	shll	$3, %eax
	movl	%eax, ST_CW8(%esp)
	movl	%eax, %edx
	addl	%eax, %eax
	movl	%eax, ST_CW16(%esp)
	addl	%edx, %eax
	movl	%eax, ST_CW24(%esp)

	movl	T_SHX(%esp), %eax
	shll	$3, %eax
	movl	%eax, ST_SH8(%esp)

	movl	T(A_PIX), %eax
	movl	%eax, T_DST(%esp)
	movl	T(A_STRIDE), %eax
	movl	%eax, T_STRIDE(%esp)
	movl	T(A_SW), %eax
	movl	%eax, T_SW(%esp)
	movl	%eax, %edx
	sarl	$1, %edx
	movl	%edx, T_SWH(%esp)
	decl	%eax
	movl	%eax, T_SWM1(%esp)

	/* esi is the texel pointer for the rest of the kernel. SPANBODY reads
	 * it and never writes it, which is what lets it stay in a register
	 * across every span of the triangle. */
	movl	T(A_TEXELS), %esi

	/* The width dispatch, once per triangle instead of once per row. */
	cmpl	$128, T(A_TW)
	je	.Lwalk128
	jmp	.Lwalk64

.Lret:
	addl	$TFRAME, %esp
	popl	%ebp
	popl	%edi
	popl	%esi
	popl	%ebx
	ret

/*
 * The trapezoid walk, specialised per texture width so that SPANBODY's shifts
 * and masks are immediates -- the same reason the span entry specialises, and
 * the reason this is a macro rather than a loop over a width variable.
 */
	.macro WALK sh, twm1, vmask, twm1f, tag
.Lphase\tag:
	cmpl	$0, T_ROWS(%esp)
	jle	.Lnext\tag

.Lrow\tag:
	/* x0 = (left - 1) >> 16, floored at 0. The decrement is the C's: an
	 * edge landing exactly on a pixel centre belongs to the span. */
	movl	T_LX(%esp), %eax
	decl	%eax
	sarl	$16, %eax
	xorl	%edx, %edx
	testl	%eax, %eax
	cmovsl	%edx, %eax

	/* x1 = right >> 16, capped at screen_width - 1 */
	movl	T_RX(%esp), %ecx
	sarl	$16, %ecx
	cmpl	T_SW(%esp), %ecx
	cmovgel	T_SWM1(%esp), %ecx

	cmpl	%ecx, %eax
	jge	.Lrowadv\tag

	/* The plane values are carried at the screen centre, where their
	 * magnitude is smallest and TexturePlanePrepare32's bound was proved;
	 * stepping out to the left end is a multiply, once per row. */
	movl	%eax, %ebx
	subl	T_SWH(%esp), %ebx

	movl	ST_AU(%esp), %edx
	imull	%ebx, %edx
	addl	T_AU(%esp), %edx
	movl	%edx, AU(%esp)
	movl	ST_BV(%esp), %edx
	imull	%ebx, %edx
	addl	T_BV(%esp), %edx
	movl	%edx, BV(%esp)
	movl	ST_CW(%esp), %edx
	imull	%ebx, %edx
	addl	T_CW(%esp), %edx
	movl	%edx, CW(%esp)

	subl	%eax, %ecx
	movl	%ecx, %edx
	andl	$7, %edx
	movl	%edx, REMAIN(%esp)
	sarl	$3, %ecx
	movl	%ecx, BLKN(%esp)

	movl	T_OFF(%esp), %edx
	addl	%eax, %edx
	movl	T_DST(%esp), %edi
	leal	(%edi,%edx,4), %edi

	movl	T_SHX(%esp), %edx
	imull	%eax, %edx
	addl	T_SHADE(%esp), %edx
	movl	%edx, SHADE(%esp)

	SPANBODY \sh, \twm1, \vmask, \twm1f, \tag, .Lrowadv\tag

.Lrowadv\tag:
	movl	T_LS(%esp), %eax
	addl	%eax, T_LX(%esp)
	movl	T_RS(%esp), %eax
	addl	%eax, T_RX(%esp)
	movl	T_YAU(%esp), %eax
	addl	%eax, T_AU(%esp)
	movl	T_YBV(%esp), %eax
	addl	%eax, T_BV(%esp)
	movl	T_YCW(%esp), %eax
	addl	%eax, T_CW(%esp)
	movl	T_SHY(%esp), %eax
	addl	%eax, T_SHADE(%esp)
	movl	T_STRIDE(%esp), %eax
	addl	%eax, T_OFF(%esp)
	decl	T_ROWS(%esp)
	jne	.Lrow\tag

.Lnext\tag:
	/* The phase change. One edge is replaced; everything else -- the
	 * plane accumulators, the shade, the row offset and the side that
	 * A->C is on -- carries straight through, which is exactly what the
	 * C's second while loop does by not resetting any of it. */
	cmpl	$0, T_PHASE(%esp)
	jne	.Lret
	movl	$1, T_PHASE(%esp)
	movl	T_P2X(%esp), %eax
	movl	T_P2S(%esp), %edx
	cmpl	$0, T_P2L(%esp)
	je	11f
	movl	%eax, T_LX(%esp)
	movl	%edx, T_LS(%esp)
	jmp	12f
11:	movl	%eax, T_RX(%esp)
	movl	%edx, T_RS(%esp)
12:	movl	T_ROWS2(%esp), %eax
	movl	%eax, T_ROWS(%esp)
	jmp	.Lphase\tag
	.endm

.Lwalk128:
	WALK	7, 127, 0x3F80, LC_f127, 128

.Lwalk64:
	WALK	6, 63, 0x0FC0, LC_f63, 64
