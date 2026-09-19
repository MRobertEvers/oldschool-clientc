/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 * Two independent axes: WHO PUT THE VERTICES IN Y ORDER, and HOW MANY
 * TRIANGLES ARRIVE PER CALL. Three of the four cells exist. There is no
 * self-sorting run door and there should not be: a run only ever comes from
 * the depth sort, which ordered it on the way past.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES
 *   _presorted_asm      already ordered   one        no
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The old names hid the axis that matters. `_tri_` and a bare `_asm` said
 * "one triangle" and left out that this is the only door that sorts;
 * `_batch_` said "several triangles" and left out that a run is ALWAYS
 * presorted -- so nothing in the name told you that the run door and the
 * presorted door share a body, or that "batched" and "presorted" were never
 * alternatives to choose between.
 *
 * All the doors of a family converge on one body, so they cannot draw
 * different triangles. What differs is only what each pays to reach it:
 * sorting pays the six-way compare ladder, which is up to six unpredictable
 * branches on a part that costs twenty pipeline stages for a mispredict; a
 * single-triangle door pays the cdecl marshal, the call, and the callee-saved
 * registers once per triangle where a run pays them once per run.
 */

/*
 * The gouraud triangle, entire, in handrolled i686 SSE2 assembly.
 *
 * WHY THE WHOLE FUNCTION AND NOT JUST THE SPAN
 *
 * The texture side of this rasterizer already has a handrolled span kernel
 * (tex_span_i686.S), and it substitutes for exactly one leaf function while
 * the per-triangle prologue and the scanline walk stay in C. That boundary is
 * not free, and the generated code says how expensive it is. Compile the
 * shipping win32 unity with -O3 and look at the texture walk's row loop: forty
 * memory operations and not one register-resident instruction. Twenty-seven of
 * them come before the call, and sixteen of those twenty-seven are pure
 * re-marshalling of arguments that never change across rows -- the pixel
 * buffer, the screen width, the texel pointer, the three plane normals -- each
 * loaded out of the frame and stored into the outgoing argument area on every
 * single row. The rest are the live accumulators, which cannot survive an
 * opaque call in i686's four callee-saved registers, so they spill and reload
 * around every `call`. Even the loop counter lives in memory.
 *
 * None of that is the compiler being bad at its job. It is the calling
 * convention: fifteen cdecl arguments, once per scanline, to a symbol that by
 * construction cannot be inlined. The only way to delete the marshalling is to
 * delete the boundary, which means the prologue, the walk and the fill have to
 * live in one function -- and if that function is to be hand scheduled at all,
 * it may as well be hand written throughout.
 *
 * So this file is the gouraud triangle end to end, the way
 * project_vertices_aarch64.S is projection end to end: vertex sort, prologue,
 * both trapezoid walks and the span fill, with no C in the hot path and no
 * call in the row loop.
 *
 * WHY GOURAUD, AND WHICH PART OF IT
 *
 * The face census (300 frames, osrs239 lumbridge) says gouraud is 82.5% of
 * drawn faces and 71.0% of drawn area and carries no vector kernel at all,
 * while the texture asm already covers 89.81% of textured area. So gouraud is
 * where the remaining raster time is.
 *
 * But NOT in the fill, and that governs how this file is written. The ablate
 * ladder on the P4 cannot resolve `base - g1` (the fill) at all: 5.39 s in one
 * rep against 0.73 s in the next, on 1500 frames. What does resolve is the
 * walk and the prologue. That agrees with the span trace behind
 * gouraud_span_fill.h -- 37.3% of spans are ONE pixel and 59.5% are under four,
 * so there is barely a fill loop to speed up, and two separate SSE2 attempts on
 * the fill body previously measured 0.0% and 50% SLOWER.
 *
 * The effort in here therefore goes into the prologue and the walk, and the
 * fill is a faithful transcription of `..._span_fill_short` rather than a new
 * idea. Anyone tempted to vectorise the block loop should read
 * gouraud_span_fill.h first; the measurement is already on file.
 *
 * WHAT THE ROW LOOP DOES DIFFERENTLY
 *
 * The edges are stepped BEFORE the fill instead of after it. The C cannot say
 * this tidily -- it computes the span, fills, then steps -- but the step does
 * not depend on the fill, and hoisting it frees %ebx and %esi while the fill
 * runs. That is what buys the fill enough registers to work without spilling on
 * a machine that has eight of them. The colour accumulator is deliberately NOT
 * hoisted with them: the fill needs its pre-step value, and it survives the
 * fill untouched, so it is stepped at the row join where both the drawn and the
 * skipped path meet. Stepping it on the drawn path only would drop a row of
 * gradient on every empty span.
 *
 * EXACTNESS
 *
 * This must be bit-identical to
 * raster_gouraudhsllightness_screen_opaque_bary_branching_s4(), not merely
 * close, because the 4-pixel colour quantization is visible output pinned by
 * toridraw_scanline_parity_test. Load-bearing details:
 *
 *   - The span is walked in blocks of FOUR with ONE palette lookup per block,
 *     phased from x_start, not from the frame buffer. Not eight, not per-pixel.
 *   - Every add on the colour chain is a wrapping 32-bit add and every multiply
 *     keeps only the low 32 bits (graphics/int_wrap.h). `addl` and `imull` are
 *     exactly that: no saturation, no widening.
 *   - The divides truncate toward zero. C does; so does cvttsd2si, which is
 *     what the three edge slopes now go through instead of idivl. See EDGE1
 *     for why moving them onto the FP divider does not cost a bit.
 *   - `(dx << 16)` and `(numerator << 8)` overflow for edge-on slivers, on
 *     purpose. `shll` reproduces the reference client's Java wraparound.
 *   - The 1..3 pixel tail is three unconditional stores and two conditional
 *     indices, never a jump table: the count splits 48/24/16 and a P4 pays
 *     twenty pipeline stages per mispredict.
 *
 * toridraw_gouraud_tri_asm_test compares this against the C reference over
 * randomised triangles, framebuffer against framebuffer, and fails on any
 * difference.
 *
 * ABLATION: like tex_span_i686.S this kernel does not honour TORIDRAW_ABLATE,
 * and the makefile withdraws it from any build that asks for the census or the
 * ablation modes.
 *
 * cdecl, i686, SSE2. Every xmm register is caller-saved in the 32-bit Windows
 * calling convention, so none are preserved here.
 */

	.text

/* ---- frame ------------------------------------------------------------
 *
 * %esp is force-aligned to 16 in the prologue, so the three vertex groups
 * below can be written with movdqa. Each carries a fourth lane the body never
 * reads; it is there so ONE pshufd applies the y-sort permutation to a whole
 * group, and so the staged row can be loaded 16 bytes at a time.
 */

#define V_PIX      0	/* pixel_buffer                                     */
#define V_STRIDE   4	/* frame buffer row stride, in pixels               */
#define V_SCRW     8	/* screen_width                                     */
#define V_SCRH    12	/* screen_height                                    */
#define V_X0      16	/* the three vertices, AFTER the y sort             */
#define V_X1      20
#define V_X2      24
#define V_Y0      32
#define V_Y1      36
#define V_Y2      40
#define V_C0      48
#define V_C1      52
#define V_C2      56
#define V_ROWBUF  64	/* one staged row, for the single-triangle entry    */
#define V_SXAC   112	/* step_edge_x_AC_ish16                             */
#define V_SXAB   116	/* step_edge_x_AB_ish16                             */
#define V_SXBC   120	/* step_edge_x_BC_ish16                             */
#define V_SXPAD  124	/* the packed store's unused lane                  */
#define V_SXH    128	/* step_x_hsl_ish8                                  */
#define V_SYH    132	/* step_y_hsl_ish8                                  */
#define V_SXH4   136	/* step_x_hsl_ish8 << 2, the per-block colour step  */
#define V_EAC    140	/* edge_x_AC_ish16                                  */
#define V_EAB    144	/* edge_x_AB_ish16                                  */
#define V_EBC    148	/* edge_x_BC_ish16                                  */
#define V_HSL    152	/* hsl_ish8, carried across both segments           */
#define V_OFF    156	/* row offset into the frame buffer, in pixels      */
#define V_NC1    160	/* segment proved free of horizontal clipping       */
#define V_NC2    164
#define V_N1     168	/* segment row counts                               */
#define V_N2     172
#define V_EL     176	/* the segment loop's left/right edge and steps,    */
#define V_ER     180	/*   named generically so one macro serves both     */
#define V_STEPL  184	/*   segments in both edge orientations            */
#define V_STEPR  188
#define V_NC     192
#define V_CNT    196
#define V_TAIL   200	/* stride & 3, parked while the block loop runs     */
#define V_DAB    204	/* colour deltas, live across two divides           */
#define V_DAC    208
#define V_SAREA  212
#define V_TMP    216
#define V_ALPHA  232	/* the alpha lane's opacity, 0..255           */
#define V_ROW    220	/* the staged row being rasterised                  */
#define V_LEFT   224	/* rows still to go                                */
#define V_OLDESP 228	/* %esp before the alignment, for the epilogue      */
#define FRAME    288

/* One staged row: x0,x1,x2,pad / y0,y1,y2,pad / c0,c1,c2,pad. Sixteen bytes
 * per group and 16-byte aligned, which is what lets the sort be three loads,
 * three shuffles and three stores instead of eighteen scalar moves. */
#define R_X        0
#define R_Y       16
#define R_C       32
#define R_A       44	/* the colour group's spare lane, per-row alpha */
#define ROWBYTES  48

#define V_RBX0 (V_ROWBUF + R_X + 0)
#define V_RBX1 (V_ROWBUF + R_X + 4)
#define V_RBX2 (V_ROWBUF + R_X + 8)
#define V_RBY0 (V_ROWBUF + R_Y + 0)
#define V_RBY1 (V_ROWBUF + R_Y + 4)
#define V_RBY2 (V_ROWBUF + R_Y + 8)
#define V_RBC0 (V_ROWBUF + R_C + 0)
#define V_RBC1 (V_ROWBUF + R_C + 4)
#define V_RBC2 (V_ROWBUF + R_C + 8)

/* Arguments, off the un-aligned frame pointer the prologue parks in %ebp:
 * four pushes and the return address sit between it and argument zero. Read
 * only in the two entries -- %ebp is the colour accumulator once SEGMENT
 * runs, and nothing below Lface wants an argument again. */
#define A(n) (20 + 4 * (n))(%ebp)

/* ---- helpers ---------------------------------------------------------- */

/*
 * Colour accumulator in \src -> palette entry in \dst, clamp and all.
 * The clamp is the one in ToriDraw_Hsl16Ish8ToPixel: an edge can land a subpixel
 * past its vertex after the fixed-point nudge, so the interpolant is not
 * guaranteed inside the palette's range even though every vertex is.
 */
.macro	LOOKUP src, dst
	movl	\src, \dst
	sarl	$8, \dst
	cmpl	$0xFFFF, \dst
	jbe	7000f
	/* Out of range. Negative -> 0, positive -> 0xFFFF, branchlessly:
	 * sar 31 gives -1 or 0, not gives 0 or -1, mask gives 0 or 0xFFFF. */
	sarl	$31, \dst
	notl	\dst
	andl	$0xFFFF, \dst
7000:
	movl	_g_hsl16_to_pixel_table(,\dst,4), \dst
.endm

/*
 * flat_screen_fixed_edges_no_hclip, once per segment. Affine extrema on a
 * segment occur at its endpoints, so two evaluations settle every row of it.
 * Result written to \dstslot as 0 or 1. Clobbers %eax %ecx %edx.
 */
.macro	NOHCLIP ea0, sa, eb0, sb, cnt, dstslot
	movl	\cnt(%esp), %ecx
	testl	%ecx, %ecx
	jle	8100f			/* n <= 0: vacuously true            */
	decl	%ecx			/* n - 1                             */

	movl	\sa(%esp), %eax
	imull	%ecx, %eax
	addl	\ea0(%esp), %eax	/* e_a_last                          */
	movl	\ea0(%esp), %edx	/* e_a_first                         */
	cmpl	%eax, %edx
	jle	8000f
	movl	%eax, %edx		/* min_e_a                           */
8000:
	testl	%edx, %edx
	js	8200f			/* min_e_a < 0 -> clipping possible  */

	movl	\sb(%esp), %eax
	imull	%ecx, %eax
	addl	\eb0(%esp), %eax	/* e_b_last                          */
	movl	\eb0(%esp), %edx	/* e_b_first                         */
	cmpl	%eax, %edx
	jge	8050f
	movl	%eax, %edx		/* max_e_b                           */
8050:
	movl	V_SCRW(%esp), %eax
	shll	$16, %eax
	cmpl	%eax, %edx
	jge	8200f
8100:
	movl	$1, \dstslot(%esp)
	jmp	8300f
8200:
	movl	$0, \dstslot(%esp)
8300:
.endm

/*
 * One edge slope, off the FP divider instead of the integer one.
 *
 * %eax carries the numerator in, the truncated quotient out; \den names the
 * divisor register. Clobbers %xmm0 %xmm1 and, unlike the idivl it replaces,
 * leaves %edx alone -- there is no cltd to sign-extend into it.
 *
 * WHY THIS IS STILL EXACT, despite being floating point.
 *
 * The result is a screen column in 1/65536ths, and a triangle whose edge lands
 * one column off does not tile with its neighbour. So "close" is not good
 * enough here even though it is elsewhere in this file, and the substitution
 * has to be argued rather than measured.
 *
 * Let a be the numerator and b the divisor, both int32, b != 0. Every such a
 * and b is exact in a double (53 bits > 32). If a/b is an integer it is below
 * 2^53 and therefore exact, so the conversion is a no-op. If it is not an
 * integer, its distance to the nearest integer is at least 1/|b|, because the
 * fractional part is m/|b| for some 1 <= m <= |b|-1. The rounding error of a
 * correctly-rounded divide is at most ulp(q)/2 <= |q| * 2^-52, and
 * |q| < 2^31/|b|, so the error is under 2^-21/|b| -- strictly smaller than the
 * 1/|b| that separates q from any integer. The rounded result therefore lies
 * between the same two integers as the exact quotient, and cvttsd2si, which
 * truncates toward zero exactly as idivl does, returns the same value.
 *
 * A bare reciprocal multiply -- one divsd for 1/b, then a mulsd -- would NOT
 * inherit that. It rounds twice, so the error can reach a full ulp and cross
 * an integer, which is the one thing an edge slope may not do. It also buys
 * nothing: the three slopes here divide by three different values, so there is
 * no reciprocal to share between them, and 1/b + multiply is strictly more
 * work than the divide it replaces. The shared reciprocal in the colour block
 * above is a different case and stays as it is -- two quotients, one divisor.
 *
 * Assemble with -DTORIDRAW_EDGE_IDIV to put idivl back, which is how
 * the two are A/B'd against each other on the box.
 */
.macro	EDGE1 den
#ifdef TORIDRAW_EDGE_IDIV
	cltd
	idivl	\den
#else
	cvtsi2sd %eax, %xmm0
	cvtsi2sd \den, %xmm1
	divsd	%xmm1, %xmm0
	cvttsd2si %xmm0, %eax
#endif
.endm

#include "graphics/raster/edge_slopes_i686.inc"


/*
 * The span fill. On entry %eax is the colour at x_start, %ecx the pixel count
 * (always >= 1), %edi the destination pointer. Clobbers %eax %ecx %edx %edi
 * and leaves control at \skip.
 *
 * toridraw_gouraud_span_fill_short transcribed: the under-four span is lifted
 * clear of the block loop, because 59.5% of spans take zero trips through it
 * and the test that decides so splits 59.5/40.5 -- barely more predictable
 * than the jump table it replaced.
 */
.macro	SPANFILL skip
	cmpl	$4, %ecx
	jl	6100f			/* under four: tail only, no loop    */

	movl	%ecx, %edx
	andl	$3, %ecx
	movl	%ecx, V_TAIL(%esp)
	shrl	$2, %edx		/* whole blocks of four              */
	.p2align 4
6000:
	LOOKUP	%eax, %ecx
	/* Four dword stores, not one movdqu. gcc was already emitting
	 * pshufd+movups for the C and it measured dead even with the scalar
	 * form; an unaligned 16-byte store is a bad trade on a P4, which splits
	 * it and can straddle a cache line. */
	movl	%ecx, 0(%edi)
	movl	%ecx, 4(%edi)
	movl	%ecx, 8(%edi)
	movl	%ecx, 12(%edi)
	addl	$16, %edi
	addl	V_SXH4(%esp), %eax
	decl	%edx
	jne	6000b

	movl	V_TAIL(%esp), %ecx
	testl	%ecx, %ecx
	je	\skip
6100:
	/* 1..3 pixels. Three stores and two conditional indices; nothing
	 * outside [0, count) is ever addressed, and the duplicate store is a
	 * store-buffer hit, cheaper than being wrong about a branch. */
	LOOKUP	%eax, %edx
	xorl	%eax, %eax
	cmpl	$2, %ecx
	setge	%al			/* i1 = count > 1                    */
	movl	%edx, (%edi)
	movl	%edx, (%edi,%eax,4)
	xorl	%eax, %eax
	cmpl	$3, %ecx
	setge	%al
	addl	%eax, %eax		/* i2 = count > 2 ? 2 : 0            */
	movl	%edx, (%edi,%eax,4)
.endm

/*
 * Per-triangle setup for the two lanes.
 *
 * The opaque lane prepares nothing: its span stores a palette entry and needs
 * no state beyond the colour accumulator the walk already carries.
 */
.macro	GSETUP_OPAQUE
.endm

/*
 * The alpha lane's constants, set once per triangle and live across both
 * trapezoid walks. EDGESLOPES3 has finished with %xmm0-%xmm5 by the time this
 * runs, which is why it runs here and not earlier.
 *
 *   %xmm7  alpha,       eight 16-bit lanes
 *   %xmm5  255 - alpha, eight 16-bit lanes
 *   %xmm6  zero, for the byte unpack
 *   %xmm4  0x00FFFFFF four times, to clear the alpha byte the unpack keeps
 */
.macro	GSETUP_ALPHA
	movl	V_ROW(%esp), %ecx
	movl	R_A(%ecx), %ecx
	movl	%ecx, V_ALPHA(%esp)
	movd	%ecx, %xmm7
	pshuflw	$0x00, %xmm7, %xmm7
	pshufd	$0x00, %xmm7, %xmm7

	movl	$0xFF, %eax
	subl	%ecx, %eax
	movd	%eax, %xmm5
	pshuflw	$0x00, %xmm5, %xmm5
	pshufd	$0x00, %xmm5, %xmm5

	pcmpeqd	%xmm4, %xmm4
	psrld	$8, %xmm4
	pxor	%xmm6, %xmm6
.endm

/*
 * The source colour's whole contribution to alpha_blend(), for the palette
 * entry in \src, broadcast into %xmm3.
 *
 *   K = (((o & 0xFF00FF) * a) >> 8) & 0xFF00FF
 *     + (((o & 0xFF00)   * a) >> 8) & 0xFF00
 *
 * The C keeps red and blue in one 32-bit multiply by masking them apart and
 * does green separately; unpacking the bytes into 16-bit lanes separates all
 * three at once, and the widest product is 255*255 which still fits a lane. So
 * pmullw + psrlw is the same (ch * a) >> 8 the C's mask-and-shift extracts.
 * The pand puts back the zero alpha byte every one of the C's masks implies.
 *
 * Unlike the flat kernel this cannot be hoisted out of the span: the colour
 * changes every four pixels, which is the whole difference between the two.
 */
.macro	GBLENDK src
	movd	\src, %xmm3
	punpcklbw %xmm6, %xmm3
	pmullw	%xmm7, %xmm3
	psrlw	$8, %xmm3
	packuswb %xmm3, %xmm3
	pand	%xmm4, %xmm3
	pshufd	$0x00, %xmm3, %xmm3
.endm

/*
 * The alpha span. On entry %eax is the colour at x_start, %ecx the pixel count
 * (always >= 1), %edi the destination. Clobbers %eax %ecx %edx %edi and leaves
 * control at \skip.
 *
 * Blocked in fours from x_start exactly as the opaque span is, because the
 * palette lookup is per block in both and the phase is visible output.
 *
 * The 1..3 pixel tail is a real loop, not the opaque span's branchless triple
 * store. That trick works because a store is idempotent and a blend is not:
 * writing the same pixel twice would blend it twice.
 */
.macro	SPANFILL_ALPHA skip
	cmpl	$4, %ecx
	jl	6600f

	movl	%ecx, %edx
	andl	$3, %ecx
	movl	%ecx, V_TAIL(%esp)
	shrl	$2, %edx
	.p2align 4
6500:
	LOOKUP	%eax, %ecx
	GBLENDK	%ecx
	movdqu	(%edi), %xmm0
	movdqa	%xmm0, %xmm1
	punpcklbw %xmm6, %xmm0
	punpckhbw %xmm6, %xmm1
	pmullw	%xmm5, %xmm0
	pmullw	%xmm5, %xmm1
	psrlw	$8, %xmm0
	psrlw	$8, %xmm1
	packuswb %xmm1, %xmm0
	pand	%xmm4, %xmm0
	paddd	%xmm3, %xmm0
	movdqu	%xmm0, (%edi)
	addl	$16, %edi
	addl	V_SXH4(%esp), %eax
	decl	%edx
	jne	6500b

	movl	V_TAIL(%esp), %ecx
	testl	%ecx, %ecx
	je	\skip
6600:
	LOOKUP	%eax, %edx
	GBLENDK	%edx
	movl	%ecx, %edx
6700:
	movd	(%edi), %xmm0
	punpcklbw %xmm6, %xmm0
	pmullw	%xmm5, %xmm0
	psrlw	$8, %xmm0
	packuswb %xmm0, %xmm0
	pand	%xmm4, %xmm0
	paddd	%xmm3, %xmm0
	movd	%xmm0, (%edi)
	addl	$4, %edi
	decl	%edx
	jne	6700b
.endm

/*
 * One trapezoid segment. %ebx left edge ish16, %esi right edge ish16,
 * %ebp colour ish8. The horizontal clamps are hoisted out of the loop: the
 * caller proved once per segment whether any row can leave the viewport, so
 * the common case runs a row loop with no clamping in it at all.
 */
.macro	SEGMENT fill
	movl	V_EL(%esp), %ebx
	movl	V_ER(%esp), %esi
	movl	V_HSL(%esp), %ebp
	movl	V_CNT(%esp), %eax
	testl	%eax, %eax
	jle	5900f
	cmpl	$0, V_NC(%esp)
	je	5500f

	/* ---- proved inside the viewport: no left/right clamp ---- */
	.p2align 4
5000:
	movl	%ebx, %eax
	movl	%esi, %ecx
	addl	V_STEPL(%esp), %ebx
	addl	V_STEPR(%esp), %esi

	cmpl	%ecx, %eax
	je	5400f
	sarl	$16, %eax
	sarl	$16, %ecx
	cmpl	%ecx, %eax
	jge	5400f
	subl	%eax, %ecx		/* pixel count                       */

	movl	%eax, %edx
	imull	V_SXH(%esp), %edx	/* x_start * step, low 32 bits only  */
	addl	%ebp, %edx
	addl	V_OFF(%esp), %eax
	movl	V_PIX(%esp), %edi
	leal	(%edi,%eax,4), %edi
	movl	%edx, %eax

	\fill 5400f
5400:
	addl	V_SYH(%esp), %ebp	/* every row, drawn or not           */
	movl	V_STRIDE(%esp), %eax
	addl	%eax, V_OFF(%esp)
	decl	V_CNT(%esp)
	jne	5000b
	jmp	5900f

	/* ---- may leave the viewport: clamp both ends ---- */
	.p2align 4
5500:
	movl	%ebx, %eax
	movl	%esi, %ecx
	addl	V_STEPL(%esp), %ebx
	addl	V_STEPR(%esp), %esi

	cmpl	%ecx, %eax
	je	5800f
	sarl	$16, %ecx
	movl	V_SCRW(%esp), %edx
	cmpl	%edx, %ecx
	jl	5600f
	leal	-1(%edx), %ecx		/* x_end = screen_width - 1          */
5600:
	sarl	$16, %eax
	testl	%eax, %eax
	jns	5700f
	xorl	%eax, %eax		/* x_start = 0                       */
5700:
	cmpl	%ecx, %eax
	jge	5800f
	subl	%eax, %ecx

	movl	%eax, %edx
	imull	V_SXH(%esp), %edx
	addl	%ebp, %edx
	addl	V_OFF(%esp), %eax
	movl	V_PIX(%esp), %edi
	leal	(%edi,%eax,4), %edi
	movl	%edx, %eax

	\fill 5800f
5800:
	addl	V_SYH(%esp), %ebp
	movl	V_STRIDE(%esp), %eax
	addl	%eax, V_OFF(%esp)
	decl	V_CNT(%esp)
	jne	5500b

5900:
	movl	%ebx, V_EL(%esp)
	movl	%esi, V_ER(%esp)
	movl	%ebp, V_HSL(%esp)
.endm

/*
 * Copy one of the six vertex orderings out of the staged row into the sorted
 * slots.
 *
 * \perm is that ordering as a pshufd immediate: result lane i takes source
 * lane (perm >> 2i) & 3. x, y and colour are laid out identically in the row,
 * so ONE immediate permutes all three groups, and lane 3 is padding in and
 * padding out. Eighteen scalar moves become three loads, three shuffles and
 * three stores -- and unlike a scalar copy the loads and stores are aligned,
 * which is why the frame is aligned at all.
 */
/*
 * Write one of the six vertex orderings back into the staging row.
 *
 * Only the one-triangle door reaches this. A batched row arrives already
 * sorted -- the depth sort ordered it, having had all three y values in hand
 * for the winding anyway -- so the batch path has no ladder and no permute at
 * all, which is the whole point of sorting up there.
 *
 * \perm is the ordering as a pshufd immediate: result lane i takes source lane
 * (perm >> 2i) & 3. x, y and colour share a layout in the row, so one immediate
 * permutes all three, and lane 3 is padding in and padding out.
 */
.macro	SORTSTORE perm
	movl	V_ROW(%esp), %ecx
	movdqa	R_X(%ecx), %xmm0
	movdqa	R_Y(%ecx), %xmm1
	movdqa	R_C(%ecx), %xmm2
	pshufd	$\perm, %xmm0, %xmm0
	pshufd	$\perm, %xmm1, %xmm1
	pshufd	$\perm, %xmm2, %xmm2
	movdqa	%xmm0, R_X(%ecx)
	movdqa	%xmm1, R_Y(%ecx)
	movdqa	%xmm2, R_C(%ecx)
.endm

/* Load the generic segment slots for one segment. */
.macro	SETSEG el, er, sl, sr, nc, cnt
	movl	\el(%esp), %eax
	movl	%eax, V_EL(%esp)
	movl	\er(%esp), %eax
	movl	%eax, V_ER(%esp)
	movl	\sl(%esp), %eax
	movl	%eax, V_STEPL(%esp)
	movl	\sr(%esp), %eax
	movl	%eax, V_STEPR(%esp)
	movl	\nc(%esp), %eax
	movl	%eax, V_NC(%esp)
	movl	\cnt(%esp), %eax
	movl	%eax, V_CNT(%esp)
.endm

/* ---- entry ------------------------------------------------------------ */

/*
 * The prologue both doors share: save, park an argument pointer in %ebp,
 * force the frame to a 16-byte boundary and take the four screen constants
 * ONCE. Everything from Lface down reads them out of the frame, so a batch of
 * a thousand triangles loads them once between them instead of once each.
 *
 * The alignment is not decoration. SORTSTORE writes three 16-byte groups with
 * movdqa, and a P4 that split those stores would give back more than the
 * shuffle saves. cdecl promises nothing about %esp, so the alignment is taken
 * here and the un-aligned value parked for the epilogue.
 */
.macro	ENTER
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	%esp, %ebp
	subl	$FRAME, %esp
	andl	$-16, %esp
	movl	%ebp, V_OLDESP(%esp)

	movl	A(0), %eax
	movl	%eax, V_PIX(%esp)
	movl	A(1), %eax
	movl	%eax, V_STRIDE(%esp)
	movl	A(2), %eax
	movl	%eax, V_SCRW(%esp)
	movl	A(3), %eax
	movl	%eax, V_SCRH(%esp)
.endm

/*
 * void toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * `rows` is `count` staged rows of ROWBYTES each, 16-byte aligned, holding
 * one triangle's three screen x, three screen y and three hsl16 colours.
 *
 * THIS is the reason the file was restructured. The per-face cost the batch
 * deletes is not the fill -- it is the boundary around it: the caller's
 * thirteen-argument cdecl marshal, the call itself, four pushes and four pops
 * of the callee-saved registers, and the four screen constants re-read out of
 * the incoming frame. Thirteen arguments, once per triangle, to a symbol that
 * by construction cannot be inlined, is the same complaint this file opens
 * with about the C texture walk -- one level up. So the boundary moves out to
 * the run: the batch enters once, and rasterises every triangle in it without
 * a single call, push or argument load in between.
 *
 * Draw order is preserved by the CALLER, which flushes the batch the moment a
 * face it cannot take appears. A painter has no depth buffer, so a batch that
 * reordered anything would be wrong however fast it was.
 */
	.globl	_toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm
_toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm:
	ENTER
	movl	A(5), %eax
	testl	%eax, %eax
	jle	Ldone
	movl	%eax, V_LEFT(%esp)
	movl	A(4), %eax
	movl	%eax, V_ROW(%esp)
	jmp	Lo_face

/*
 * void toridraw_gouraud_opaque_s4_sorting_xrgb8888_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, int x0, int x1, int x2, int y0, int y1, int y2,
 *     int color0_hsl16, int color1_hsl16, int color2_hsl16);
 *
 * The one-triangle door, unchanged in signature and behaviour. It is still
 * needed: the faces the batcher will not take -- textured, flat, alpha,
 * near-clipped -- arrive one at a time and in draw order, and so do all the
 * callers that are not the painter.
 *
 * It stages its nine vertex arguments into a single row and walks the SAME
 * body. That is the point of writing it this way rather than as a second
 * kernel: there is one prologue, one walk and one fill in this file, so the
 * batched and unbatched paths cannot drift apart, and the bit-exactness the
 * existing test pins for one of them is inherited by the other.
 */
	.globl	_toridraw_gouraud_opaque_s4_sorting_xrgb8888_asm
_toridraw_gouraud_opaque_s4_sorting_xrgb8888_asm:
	ENTER
	movl	A(4), %eax
	movl	%eax, V_RBX0(%esp)
	movl	A(5), %eax
	movl	%eax, V_RBX1(%esp)
	movl	A(6), %eax
	movl	%eax, V_RBX2(%esp)
	movl	A(7), %eax
	movl	%eax, V_RBY0(%esp)
	movl	A(8), %eax
	movl	%eax, V_RBY1(%esp)
	movl	A(9), %eax
	movl	%eax, V_RBY2(%esp)
	movl	A(10), %eax
	movl	%eax, V_RBC0(%esp)
	movl	A(11), %eax
	movl	%eax, V_RBC1(%esp)
	movl	A(12), %eax
	movl	%eax, V_RBC2(%esp)
	leal	V_ROWBUF(%esp), %eax
	movl	%eax, V_ROW(%esp)
	movl	$1, V_LEFT(%esp)
	jmp	Lo_ysort

/*
 * void toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * The blending twin of the batch above, taking the opacity per row -- a run
 * may mix them, and the batcher has no reason to split a run that does.
 *
 * The C twin is raster_gouraudhsllightness_screen_alpha_bary_branching_s4,
 * which had no assembly at all until now: it is 1.08% of the XP frame, small
 * next to the opaque lanes, but it is reached through the same shim marshal
 * and the same per-face call the batch exists to delete, so leaving it out
 * would have meant a face class that still cut every run in half.
 */
	.globl	_toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm
_toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm:
	ENTER
	movl	A(5), %eax
	testl	%eax, %eax
	jle	Ldone
	movl	%eax, V_LEFT(%esp)
	movl	A(4), %eax
	movl	%eax, V_ROW(%esp)
	jmp	La_face

/*
 * void toridraw_gouraud_alpha_s4_sorting_xrgb8888_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, int x0, int x1, int x2, int y0, int y1, int y2,
 *     int color0_hsl16, int color1_hsl16, int color2_hsl16, int alpha);
 */
	.globl	_toridraw_gouraud_alpha_s4_sorting_xrgb8888_asm
_toridraw_gouraud_alpha_s4_sorting_xrgb8888_asm:
	ENTER
	movl	A(4), %eax
	movl	%eax, V_RBX0(%esp)
	movl	A(5), %eax
	movl	%eax, V_RBX1(%esp)
	movl	A(6), %eax
	movl	%eax, V_RBX2(%esp)
	movl	A(7), %eax
	movl	%eax, V_RBY0(%esp)
	movl	A(8), %eax
	movl	%eax, V_RBY1(%esp)
	movl	A(9), %eax
	movl	%eax, V_RBY2(%esp)
	movl	A(10), %eax
	movl	%eax, V_RBC0(%esp)
	movl	A(11), %eax
	movl	%eax, V_RBC1(%esp)
	movl	A(12), %eax
	movl	%eax, V_RBC2(%esp)
	movl	A(13), %eax
	movl	%eax, (V_ROWBUF + R_A)(%esp)
	leal	V_ROWBUF(%esp), %eax
	movl	%eax, V_ROW(%esp)
	movl	$1, V_LEFT(%esp)
	jmp	La_ysort

/* ---- one triangle ----------------------------------------------------- */
/*
 * ONE TRIANGLE, from the staged row at V_ROW.
 *
 * \p prefixes the named labels so the macro can be instantiated once per
 * opacity, \fill picks the span, and \setup emits whatever the fill needs
 * prepared per triangle. Everything else -- the sort ladder for the
 * one-triangle door, the prologue, the edge slopes, the vertical clip and
 * both trapezoid walks -- is shared, so the two lanes cannot drift apart.
 */
.macro	GOURAUDBODY p, setup, fill
\p\()ysort:
	/* ---- the one-triangle door's y sort, with the same early-outs ----
	 *
	 * Transcribed from the C wrapper, `<=` tie-breaks included: triangles
	 * that tie differently stop tiling with each other, so the chain is
	 * load-bearing and not merely a comparison order. The depth sort runs
	 * the identical decision in C for every batched face.
	 */
	movl	V_ROW(%esp), %esi
	movl	R_Y+0(%esi), %eax	/* y0 */
	movl	R_Y+4(%esi), %ecx	/* y1 */
	movl	R_Y+8(%esi), %edx	/* y2 */

	cmpl	%ecx, %eax
	jg	\p\()sort_b
	cmpl	%edx, %eax
	jg	\p\()sort_b
	cmpl	V_SCRH(%esp), %eax
	jg	\p\()next
	cmpl	%edx, %ecx
	jg	\p\()sort_a2
	testl	%edx, %edx
	js	\p\()next
	SORTSTORE 0xE4		/* 0, 1, 2 */
	jmp	\p\()face
\p\()sort_a2:
	testl	%ecx, %ecx
	js	\p\()next
	SORTSTORE 0xD8		/* 0, 2, 1 */
	jmp	\p\()face

\p\()sort_b:
	cmpl	%edx, %ecx
	jg	\p\()sort_c
	cmpl	V_SCRH(%esp), %ecx
	jg	\p\()next
	cmpl	%eax, %edx
	jg	\p\()sort_b2
	testl	%eax, %eax
	js	\p\()next
	SORTSTORE 0xC9		/* 1, 2, 0 */
	jmp	\p\()face
\p\()sort_b2:
	testl	%edx, %edx
	js	\p\()next
	SORTSTORE 0xE1		/* 1, 0, 2 */
	jmp	\p\()face

\p\()sort_c:
	cmpl	V_SCRH(%esp), %edx
	jg	\p\()next
	cmpl	%ecx, %eax
	jg	\p\()sort_c2
	testl	%ecx, %ecx
	js	\p\()next
	SORTSTORE 0xD2		/* 2, 0, 1 */
	jmp	\p\()face
\p\()sort_c2:
	testl	%eax, %eax
	js	\p\()next
	SORTSTORE 0xC6		/* 2, 1, 0 */

/* ---- one triangle, vertices already in y order ------------------------- */
\p\()face:
	/*
	 * What the six-way ladder above collapses to once the order is known:
	 * the whole of its clipping content is "the top vertex is below the
	 * viewport" and "the bottom vertex is above it". Two compares, both
	 * well predicted, against a ladder of six that is not.
	 */
	movl	V_ROW(%esp), %ecx
	movl	R_Y+0(%ecx), %eax	/* y_min */
	cmpl	V_SCRH(%esp), %eax
	jg	\p\()next
	movl	R_Y+8(%ecx), %eax	/* y_max */
	testl	%eax, %eax
	js	\p\()next

	movdqa	R_X(%ecx), %xmm0
	movdqa	R_Y(%ecx), %xmm1
	movdqa	R_C(%ecx), %xmm2
	movdqa	%xmm0, V_X0(%esp)
	movdqa	%xmm1, V_Y0(%esp)
	movdqa	%xmm2, V_C0(%esp)

\p\()sorted:
	/* ---- per-triangle prologue ---- */
	movl	V_Y2(%esp), %eax
	subl	V_Y0(%esp), %eax	/* dy_AC */
	je	\p\()next
	movl	%eax, %ebx		/* %ebx = dy_AC */

	movl	V_X2(%esp), %esi
	subl	V_X0(%esp), %esi	/* %esi = dx_AC */
	movl	V_X1(%esp), %edi
	subl	V_X0(%esp), %edi	/* %edi = dx_AB */
	movl	V_Y1(%esp), %ebp
	subl	V_Y0(%esp), %ebp	/* %ebp = dy_AB */

	/* sarea = dx_AB*dy_AC - dx_AC*dy_AB
	 *
	 * These six cross-product multiplies look like the natural home for
	 * pmuludq, which does two at a time and which every P4 optimisation note
	 * calls cheaper than imul. The packing is provably correct -- the low 32
	 * bits of a signed and of an unsigned product are the same bits, and the
	 * low 32 bits are all this keeps -- so the only open question was speed,
	 * and it has now been measured on the box rather than assumed:
	 *
	 *   per 6-multiply group   imul      pmuludq
	 *   Pentium 4              5.42 ns   7.52 ns   (0.721x -- pmuludq LOSES)
	 *   modern x64             --        1.009x    (dead even)
	 *
	 * pmuludq loses on the P4 because the packing costs more than the
	 * multiply saves: two movd and a pshufd per pair, and the P4's SSE2
	 * integer unit is not the part of it that is fast. imul stays.
	 *
	 * That probe also priced the five divides, with a FIXED divisor: idiv
	 * 70.82 ns for the group, divsd 98.08 ns (0.722x), reciprocal-of-sarea
	 * 87.10 ns (0.813x), packed divps 65.43 ns (1.082x, not exact). Read on
	 * a fixed divisor that says idiv wins outright, and for the three edge
	 * slopes it still does. A second probe with the divisor drawn from the
	 * distribution actually seen in a scene moved the baseline to 74.81 ns
	 * and split the answer: the two COLOUR quotients share a divisor, so
	 * they pay for one reciprocal between them rather than one each, and
	 * that reverses the sign. See the colour block below. */
	movl	%edi, %eax
	imull	%ebx, %eax
	movl	%esi, %edx
	imull	%ebp, %edx
	subl	%edx, %eax
	testl	%eax, %eax
	je	\p\()next
	movl	%eax, V_SAREA(%esp)

	movl	V_C1(%esp), %eax
	subl	V_C0(%esp), %eax
	movl	%eax, V_DAB(%esp)
	movl	V_C2(%esp), %eax
	subl	V_C0(%esp), %eax
	movl	%eax, V_DAC(%esp)

	/* Both colour gradients divide by this same sarea, so they cost one
	 * reciprocal and two multiplies instead of two divides.
	 *
	 * This is the only quotient in the prologue allowed to be approximate,
	 * and the reason is what it feeds: these two are a shade step in
	 * 1/256ths that the span fill shifts down by 8 into a palette index,
	 * whereas the three edge slopes below are a screen x in 1/65536ths, and
	 * a triangle whose edge lands one column off does not tile with its
	 * neighbour. Do not extend this downwards.
	 *
	 * Width is double and that is not conservatism -- it was measured.
	 * `shll $8` here WRAPS by design, so the numerator is a full 32-bit
	 * value and cvtsi2ss would round eight bits of it away before dividing:
	 * worst |error| 43 against the divide, versus 1 for double. The P4 does
	 * not charge for the wider divide anyway, because it has separate
	 * integer and FP dividers and this one divsd hides completely under the
	 * three idivl below -- 62.51 ns for the whole group with divss against
	 * 65.11 ns with divsd, which is inside the run-to-run spread. The
	 * numbers and the 8 KB-table arm that also lost are in
	 * gouraudhsllightness_barycentric_steps.h.
	 *
	 * xmm is free here: this kernel uses none of it anywhere else, and
	 * xmm0-7 are all caller-saved in cdecl. %ecx is dead until the BC edge
	 * loads it further down. */
	movl	$1, %eax
	cvtsi2sd %eax, %xmm7
	movl	V_SAREA(%esp), %ecx
	cvtsi2sd %ecx, %xmm6
	divsd	%xmm6, %xmm7		/* 1.0 / sarea, once for both */

	/* step_x_hsl_ish8 = ((d_hsl_AB*dy_AC - d_hsl_AC*dy_AB) << 8) / sarea */
	movl	V_DAB(%esp), %eax
	imull	%ebx, %eax
	movl	V_DAC(%esp), %edx
	imull	%ebp, %edx
	subl	%edx, %eax
	shll	$8, %eax
	cvtsi2sd %eax, %xmm0
	mulsd	%xmm7, %xmm0
	cvttsd2si %xmm0, %eax		/* truncates toward zero, as C does */
	movl	%eax, V_SXH(%esp)
	shll	$2, %eax
	movl	%eax, V_SXH4(%esp)

	/* step_y_hsl_ish8 = ((d_hsl_AC*dx_AB - d_hsl_AB*dx_AC) << 8) / sarea */
	movl	V_DAC(%esp), %eax
	imull	%edi, %eax
	movl	V_DAB(%esp), %edx
	imull	%esi, %edx
	subl	%edx, %eax
	shll	$8, %eax
	cvtsi2sd %eax, %xmm1
	mulsd	%xmm7, %xmm1
	cvttsd2si %xmm1, %eax
	movl	%eax, V_SYH(%esp)

	/* The three edge slopes, all three at once, off ONE reciprocal.
	 *
	 * They divide by three DIFFERENT values, so scalar there is nothing
	 * to share and a reciprocal is strictly more work than the divide it
	 * would replace. Packed there is: rcpps reciprocates four lanes in one
	 * instruction, and the three conditionals collapse into a mask.
	 *
	 * The measurement that says this is the right shape is already on file,
	 * a few lines up: for the divide group, idivl 70.82 ns, divsd 98.08,
	 * a shared scalar reciprocal 87.10, packed divps 65.43. Packed was the
	 * fastest thing tried and was set aside only because it is not exact.
	 * rcpps plus a Newton step is cheaper again -- the divider on this part
	 * is not pipelined and rcpps is a table lookup.
	 *
	 * See edge_slopes_i686.inc for what the approximation costs and how
	 * much of a pixel that is worth. */
	EDGESLOPES3 %esi, %edi, %ebx, %ebp, V_SXAC

	/* starting edges */
	movl	V_X0(%esp), %eax
	shll	$16, %eax
	movl	%eax, V_EAC(%esp)
	movl	%eax, V_EAB(%esp)
	movl	V_X1(%esp), %eax
	shll	$16, %eax
	movl	%eax, V_EBC(%esp)

	/* hsl = (step_x + (color0 << 8)) - x0*step_x, all modular */
	movl	V_C0(%esp), %eax
	shll	$8, %eax
	addl	V_SXH(%esp), %eax
	movl	V_X0(%esp), %edx
	imull	V_SXH(%esp), %edx
	subl	%edx, %eax
	movl	%eax, V_HSL(%esp)

	/* ---- vertical clipping ---- */
	movl	V_Y0(%esp), %ecx
	testl	%ecx, %ecx
	jns	\p\()y0_ok
	movl	V_SXAC(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_EAC(%esp)
	/* Only pre-step A->B if that edge is actually walked. A near-horizontal
	 * AB has a slope of hundreds of px per row; times a distant y0 that
	 * overflows the 16.16 product for a result that is then discarded. */
	cmpl	$0, V_Y1(%esp)
	jle	\p\()no_ab_prestep
	movl	V_SXAB(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_EAB(%esp)
\p\()no_ab_prestep:
	movl	V_SYH(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_HSL(%esp)
	movl	$0, V_Y0(%esp)
\p\()y0_ok:
	movl	V_Y1(%esp), %ecx
	testl	%ecx, %ecx
	jns	\p\()y1_ok
	movl	V_SXBC(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_EBC(%esp)
	movl	$0, V_Y1(%esp)
\p\()y1_ok:
	movl	V_Y0(%esp), %eax
	imull	V_STRIDE(%esp), %eax
	movl	%eax, V_OFF(%esp)

	movl	V_SCRH(%esp), %ecx
	movl	V_Y1(%esp), %eax
	cmpl	%ecx, %eax
	jle	\p\()y1_in
	movl	%ecx, V_Y1(%esp)
	movl	%ecx, V_Y2(%esp)
	jmp	\p\()clamped
\p\()y1_in:
	movl	V_Y2(%esp), %eax
	cmpl	%ecx, %eax
	jle	\p\()clamped
	movl	%ecx, V_Y2(%esp)
\p\()clamped:

	/* segment row counts */
	movl	V_Y1(%esp), %eax
	subl	V_Y0(%esp), %eax
	jns	\p\()n1_ok
	xorl	%eax, %eax
\p\()n1_ok:
	movl	%eax, V_N1(%esp)
	movl	V_Y2(%esp), %eax
	subl	V_Y1(%esp), %eax
	jns	\p\()n2_ok
	xorl	%eax, %eax
\p\()n2_ok:
	movl	%eax, V_N2(%esp)

	\setup

	/* Which edge is on the left for this triangle. */
	movl	V_Y0(%esp), %eax
	cmpl	V_Y1(%esp), %eax
	jne	\p\()flat_no
	movl	V_SXAC(%esp), %eax
	cmpl	V_SXBC(%esp), %eax
	jle	\p\()branchA
	jmp	\p\()branchB
\p\()flat_no:
	movl	V_SXAC(%esp), %eax
	cmpl	V_SXAB(%esp), %eax
	jge	\p\()branchA
	jmp	\p\()branchB

/* ---- AB/BC on the left, AC on the right ---- */
\p\()branchA:
	NOHCLIP V_EAB, V_SXAB, V_EAC, V_SXAC, V_N1, V_NC1
	/* BC is not advanced during the first trapezoid; AC is. */
	movl	V_N1(%esp), %eax
	imull	V_SXAC(%esp), %eax
	addl	V_EAC(%esp), %eax
	movl	%eax, V_TMP(%esp)
	NOHCLIP V_EBC, V_SXBC, V_TMP, V_SXAC, V_N2, V_NC2

	SETSEG V_EAB, V_EAC, V_SXAB, V_SXAC, V_NC1, V_N1
	SEGMENT \fill
	/* AC carries its stepped value into the second trapezoid. */
	movl	V_ER(%esp), %eax
	movl	%eax, V_EAC(%esp)
	SETSEG V_EBC, V_EAC, V_SXBC, V_SXAC, V_NC2, V_N2
	SEGMENT \fill
	jmp	\p\()next

/* ---- AC on the left, AB/BC on the right ---- */
\p\()branchB:
	NOHCLIP V_EAC, V_SXAC, V_EAB, V_SXAB, V_N1, V_NC1
	movl	V_N1(%esp), %eax
	imull	V_SXAC(%esp), %eax
	addl	V_EAC(%esp), %eax
	movl	%eax, V_TMP(%esp)
	NOHCLIP V_TMP, V_SXAC, V_EBC, V_SXBC, V_N2, V_NC2

	SETSEG V_EAC, V_EAB, V_SXAC, V_SXAB, V_NC1, V_N1
	SEGMENT \fill
	movl	V_EL(%esp), %eax
	movl	%eax, V_EAC(%esp)
	SETSEG V_EAC, V_EBC, V_SXAC, V_SXBC, V_NC2, V_N2
	SEGMENT \fill

/* ---- next row --------------------------------------------------------- */
\p\()next:
	movl	V_ROW(%esp), %eax
	addl	$ROWBYTES, %eax
	movl	%eax, V_ROW(%esp)
	decl	V_LEFT(%esp)
	jne	\p\()face

	jmp	Ldone
.endm

	GOURAUDBODY Lo_, GSETUP_OPAQUE, SPANFILL
	GOURAUDBODY La_, GSETUP_ALPHA, SPANFILL_ALPHA

Ldone:
	movl	V_OLDESP(%esp), %esp
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret
