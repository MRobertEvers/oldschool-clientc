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
 *   - The divides truncate toward zero. `idivl` does; so does C.
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

/* ---- frame ------------------------------------------------------------ */

#define V_PIX      0	/* pixel_buffer                                     */
#define V_STRIDE   4	/* frame buffer row stride, in pixels               */
#define V_SCRW     8	/* screen_width                                     */
#define V_SCRH    12	/* screen_height                                    */
#define V_X0      16	/* the three vertices, AFTER the y sort             */
#define V_X1      20
#define V_X2      24
#define V_Y0      28
#define V_Y1      32
#define V_Y2      36
#define V_C0      40
#define V_C1      44
#define V_C2      48
#define V_SXAC    52	/* step_edge_x_AC_ish16                             */
#define V_SXAB    56	/* step_edge_x_AB_ish16                             */
#define V_SXBC    60	/* step_edge_x_BC_ish16                             */
#define V_SXH     64	/* step_x_hsl_ish8                                  */
#define V_SYH     68	/* step_y_hsl_ish8                                  */
#define V_SXH4    72	/* step_x_hsl_ish8 << 2, the per-block colour step  */
#define V_EAC     76	/* edge_x_AC_ish16                                  */
#define V_EAB     80	/* edge_x_AB_ish16                                  */
#define V_EBC     84	/* edge_x_BC_ish16                                  */
#define V_HSL     88	/* hsl_ish8, carried across both segments           */
#define V_OFF     92	/* row offset into the frame buffer, in pixels      */
#define V_NC1     96	/* segment proved free of horizontal clipping       */
#define V_NC2    100
#define V_N1     104	/* segment row counts                               */
#define V_N2     108
#define V_EL     112	/* the segment loop's left/right edge and steps,    */
#define V_ER     116	/*   named generically so one macro serves both     */
#define V_STEPL  120	/*   segments in both edge orientations            */
#define V_STEPR  124
#define V_NC     128
#define V_CNT    132
#define V_TAIL   136	/* stride & 3, parked while the block loop runs     */
#define V_DAB    140	/* colour deltas, live across two divides           */
#define V_DAC    144
#define V_SAREA  148
#define V_TMP    152
#define FRAME    160

/* four pushes + FRAME + the return address */
#define A(n) (180 + 4 * (n))(%esp)

/* ---- helpers ---------------------------------------------------------- */

/*
 * Colour accumulator in \src -> palette entry in \dst, clamp and all.
 * The clamp is the one in ToriDraw_Hsl16Ish8ToRgb: an edge can land a subpixel
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
	movl	_g_hsl16_to_rgb_table(,\dst,4), \dst
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
 * One trapezoid segment. %ebx left edge ish16, %esi right edge ish16,
 * %ebp colour ish8. The horizontal clamps are hoisted out of the loop: the
 * caller proved once per segment whether any row can leave the viewport, so
 * the common case runs a row loop with no clamping in it at all.
 */
.macro	SEGMENT
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

	SPANFILL 5400f
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

	SPANFILL 5800f
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

/* Copy one of the six vertex orderings into the sorted slots. */
.macro	SORTSTORE ia, ib, ic
	movl	A(4 + \ia), %eax
	movl	%eax, V_X0(%esp)
	movl	A(4 + \ib), %eax
	movl	%eax, V_X1(%esp)
	movl	A(4 + \ic), %eax
	movl	%eax, V_X2(%esp)
	movl	A(7 + \ia), %eax
	movl	%eax, V_Y0(%esp)
	movl	A(7 + \ib), %eax
	movl	%eax, V_Y1(%esp)
	movl	A(7 + \ic), %eax
	movl	%eax, V_Y2(%esp)
	movl	A(10 + \ia), %eax
	movl	%eax, V_C0(%esp)
	movl	A(10 + \ib), %eax
	movl	%eax, V_C1(%esp)
	movl	A(10 + \ic), %eax
	movl	%eax, V_C2(%esp)
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

/* ---- entry ------------------------------------------------------------
 * void toridraw_gouraud_tri_opaque_s4_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, int x0, int x1, int x2, int y0, int y1, int y2,
 *     int color0_hsl16, int color1_hsl16, int color2_hsl16);
 */
	.globl	_toridraw_gouraud_tri_opaque_s4_asm
_toridraw_gouraud_tri_opaque_s4_asm:
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	subl	$FRAME, %esp

	movl	A(0), %eax
	movl	%eax, V_PIX(%esp)
	movl	A(1), %eax
	movl	%eax, V_STRIDE(%esp)
	movl	A(2), %eax
	movl	%eax, V_SCRW(%esp)
	movl	A(3), %eax
	movl	%eax, V_SCRH(%esp)

	/* ---- sort the three vertices by y, with the same early-outs ---- */
	movl	A(7), %eax		/* y0 */
	movl	A(8), %ecx		/* y1 */
	movl	A(9), %edx		/* y2 */

	cmpl	%ecx, %eax
	jg	Lsort_b
	cmpl	%edx, %eax
	jg	Lsort_b
	cmpl	V_SCRH(%esp), %eax
	jg	Lret
	cmpl	%edx, %ecx
	jg	Lsort_a2
	testl	%edx, %edx
	js	Lret
	SORTSTORE 0, 1, 2
	jmp	Lsorted
Lsort_a2:
	testl	%ecx, %ecx
	js	Lret
	SORTSTORE 0, 2, 1
	jmp	Lsorted

Lsort_b:
	cmpl	%edx, %ecx
	jg	Lsort_c
	cmpl	V_SCRH(%esp), %ecx
	jg	Lret
	cmpl	%eax, %edx
	jg	Lsort_b2
	testl	%eax, %eax
	js	Lret
	SORTSTORE 1, 2, 0
	jmp	Lsorted
Lsort_b2:
	testl	%edx, %edx
	js	Lret
	SORTSTORE 1, 0, 2
	jmp	Lsorted

Lsort_c:
	cmpl	V_SCRH(%esp), %edx
	jg	Lret
	cmpl	%ecx, %eax
	jg	Lsort_c2
	testl	%ecx, %ecx
	js	Lret
	SORTSTORE 2, 0, 1
	jmp	Lsorted
Lsort_c2:
	testl	%eax, %eax
	js	Lret
	SORTSTORE 2, 1, 0

Lsorted:
	/* ---- per-triangle prologue ---- */
	movl	V_Y2(%esp), %eax
	subl	V_Y0(%esp), %eax	/* dy_AC */
	je	Lret
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
	je	Lret
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

	/* The three edge slopes. The divides stay divides: five per triangle
	 * measured ~0.5% of frame, about 2.7 cycles apiece because they are
	 * independent and the core overlaps them. A reciprocal table was built,
	 * measured and thrown away -- see the note in the C source. Unlike the
	 * colour pair above these three divide by three DIFFERENT things, so
	 * there is no shared reciprocal to amortise, and they produce screen
	 * columns, which may not be approximate at all. */
	xorl	%eax, %eax
	testl	%ebx, %ebx
	jle	Lno_ac
	movl	%esi, %eax
	shll	$16, %eax
	cltd
	idivl	%ebx
Lno_ac:
	movl	%eax, V_SXAC(%esp)

	xorl	%eax, %eax
	testl	%ebp, %ebp
	jle	Lno_ab
	movl	%edi, %eax
	shll	$16, %eax
	cltd
	idivl	%ebp
Lno_ab:
	movl	%eax, V_SXAB(%esp)

	movl	V_Y2(%esp), %ecx
	subl	V_Y1(%esp), %ecx
	xorl	%eax, %eax
	testl	%ecx, %ecx
	je	Lno_bc
	movl	V_X2(%esp), %eax
	subl	V_X1(%esp), %eax
	shll	$16, %eax
	cltd
	idivl	%ecx
Lno_bc:
	movl	%eax, V_SXBC(%esp)

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
	jns	Ly0_ok
	movl	V_SXAC(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_EAC(%esp)
	/* Only pre-step A->B if that edge is actually walked. A near-horizontal
	 * AB has a slope of hundreds of px per row; times a distant y0 that
	 * overflows the 16.16 product for a result that is then discarded. */
	cmpl	$0, V_Y1(%esp)
	jle	Lno_ab_prestep
	movl	V_SXAB(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_EAB(%esp)
Lno_ab_prestep:
	movl	V_SYH(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_HSL(%esp)
	movl	$0, V_Y0(%esp)
Ly0_ok:
	movl	V_Y1(%esp), %ecx
	testl	%ecx, %ecx
	jns	Ly1_ok
	movl	V_SXBC(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_EBC(%esp)
	movl	$0, V_Y1(%esp)
Ly1_ok:
	movl	V_Y0(%esp), %eax
	imull	V_STRIDE(%esp), %eax
	movl	%eax, V_OFF(%esp)

	movl	V_SCRH(%esp), %ecx
	movl	V_Y1(%esp), %eax
	cmpl	%ecx, %eax
	jle	Ly1_in
	movl	%ecx, V_Y1(%esp)
	movl	%ecx, V_Y2(%esp)
	jmp	Lclamped
Ly1_in:
	movl	V_Y2(%esp), %eax
	cmpl	%ecx, %eax
	jle	Lclamped
	movl	%ecx, V_Y2(%esp)
Lclamped:

	/* segment row counts */
	movl	V_Y1(%esp), %eax
	subl	V_Y0(%esp), %eax
	jns	Ln1_ok
	xorl	%eax, %eax
Ln1_ok:
	movl	%eax, V_N1(%esp)
	movl	V_Y2(%esp), %eax
	subl	V_Y1(%esp), %eax
	jns	Ln2_ok
	xorl	%eax, %eax
Ln2_ok:
	movl	%eax, V_N2(%esp)

	/* Which edge is on the left for this triangle. */
	movl	V_Y0(%esp), %eax
	cmpl	V_Y1(%esp), %eax
	jne	Lflat_no
	movl	V_SXAC(%esp), %eax
	cmpl	V_SXBC(%esp), %eax
	jle	LbranchA
	jmp	LbranchB
Lflat_no:
	movl	V_SXAC(%esp), %eax
	cmpl	V_SXAB(%esp), %eax
	jge	LbranchA
	jmp	LbranchB

/* ---- AB/BC on the left, AC on the right ---- */
LbranchA:
	NOHCLIP V_EAB, V_SXAB, V_EAC, V_SXAC, V_N1, V_NC1
	/* BC is not advanced during the first trapezoid; AC is. */
	movl	V_N1(%esp), %eax
	imull	V_SXAC(%esp), %eax
	addl	V_EAC(%esp), %eax
	movl	%eax, V_TMP(%esp)
	NOHCLIP V_EBC, V_SXBC, V_TMP, V_SXAC, V_N2, V_NC2

	SETSEG V_EAB, V_EAC, V_SXAB, V_SXAC, V_NC1, V_N1
	SEGMENT
	/* AC carries its stepped value into the second trapezoid. */
	movl	V_ER(%esp), %eax
	movl	%eax, V_EAC(%esp)
	SETSEG V_EBC, V_EAC, V_SXBC, V_SXAC, V_NC2, V_N2
	SEGMENT
	jmp	Lret

/* ---- AC on the left, AB/BC on the right ---- */
LbranchB:
	NOHCLIP V_EAC, V_SXAC, V_EAB, V_SXAB, V_N1, V_NC1
	movl	V_N1(%esp), %eax
	imull	V_SXAC(%esp), %eax
	addl	V_EAC(%esp), %eax
	movl	%eax, V_TMP(%esp)
	NOHCLIP V_TMP, V_SXAC, V_EBC, V_SXBC, V_N2, V_NC2

	SETSEG V_EAC, V_EAB, V_SXAC, V_SXAB, V_NC1, V_N1
	SEGMENT
	movl	V_EL(%esp), %eax
	movl	%eax, V_EAC(%esp)
	SETSEG V_EAC, V_EBC, V_SXAC, V_SXBC, V_NC2, V_N2
	SEGMENT

Lret:
	addl	$FRAME, %esp
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret
