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
 * The flat triangle, entire, in handrolled i686 SSE2 assembly -- opaque and
 * alpha, batched and single.
 *
 * WHY A SEPARATE KERNEL AND NOT THE GOURAUD ONE
 *
 * A flat face IS a gouraud face with three equal vertex colours, and the two
 * were measured to agree on every pixel of 200,000 randomised triangles and
 * 246 million drawn pixels. So routing flat through gouraud_tri_i686.S is
 * correct. It is also wasteful, and the waste is all in the per-triangle
 * prologue and the inner fill:
 *
 *   - gouraud divides to get two colour gradients, and for a flat face both
 *     come out zero. That is a divsd and two mulsd computing nothing.
 *   - gouraud carries a colour accumulator through both trapezoid walks,
 *     stepping it once per row and once per four-pixel block, and for a flat
 *     face it never changes.
 *   - gouraud looks the palette up once per FOUR PIXELS. A flat triangle needs
 *     exactly one lookup for the whole triangle.
 *   - and once the colour is constant the span stops being a loop that
 *     computes and becomes a loop that stores, which is the one shape on this
 *     machine where a 16-byte SSE2 store is unambiguously the right answer.
 *
 * That last point is worth stating carefully, because gouraud_tri_i686.S says
 * the opposite about ITS fill and is right to. There, four dwords are stored
 * per palette lookup at an arbitrary phase, so a movdqu splits and can straddle
 * a line for no gain -- measured dead even. Here the whole span is one colour,
 * so the fill can align itself first and then run movdqa, and the wider the
 * span the better it does.
 *
 * WHAT IS SHARED WITH GOURAUD, AND WHY
 *
 * The y-sort ladder, the edge slopes, the vertical clip, the no-horizontal-
 * clip proof and the two-trapezoid walk are transcribed from the same C the
 * gouraud kernel was, because the flat C is the same code with the colour
 * taken out -- same six-way sort, same pre-step rules, same segment split.
 * Anything that diverged here would be a bug in one of the two.
 *
 * EXACTNESS
 *
 * Bit-identical to raster_flat_screen_opaque_branching_s4() and
 * raster_flat_screen_alpha_branching_s4(), not merely close, and
 * toridraw_flat_tri_asm_test.c pins it framebuffer against framebuffer.
 * Load-bearing details:
 *
 *   - The alpha blend is alpha_blend() from graphics/alpha.h, done four pixels
 *     at a time. Its per-channel form is (ch * a) >> 8 with no rounding, which
 *     is exactly what pmullw + psrlw gives on 16-bit lanes -- see FILLALPHA for
 *     why the channels cannot carry into each other in either form.
 *   - The source colour's contribution is CONSTANT for the whole triangle, so
 *     it is folded once into K and added with paddd. The add is a full 32-bit
 *     add, carries and all, because the C adds its four terms with `+`.
 *   - `(dx << 16)` overflows for edge-on slivers, on purpose, exactly as in
 *     the gouraud kernel and the reference client.
 *   - The divides truncate toward zero and go through the FP divider; the
 *     argument for why that costs nothing is in gouraud_tri_i686.S's EDGEDIV.
 *   - A span writes (x_end - x_start) pixels, not one more.
 *
 * ABLATION: like the other handrolled kernels this one does not honour
 * TORIDRAW_ABLATE, and the makefile withdraws it from any build that asks for
 * the census or the ablation modes.
 *
 * cdecl, i686, SSE2. Every xmm register is caller-saved in the 32-bit Windows
 * calling convention, so none are preserved here.
 */

	.text

/* ---- frame ------------------------------------------------------------
 *
 * %esp is force-aligned to 16 in the prologue: the vertex groups are written
 * with movdqa, and the splatted fill colour is read with one.
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
#define V_COL4    48	/* the fill colour, four times, for the wide store  */
#define V_ROWBUF  64	/* one staged row, for the single-triangle entries  */
#define V_RGB    112	/* g_hsl16_to_rgb_table[color], once per triangle   */
#define V_SXAC   128	/* step_edge_x_AC_ish16                             */
#define V_SXAB   132	/* step_edge_x_AB_ish16                             */
#define V_SXBC   136	/* step_edge_x_BC_ish16                             */
#define V_SXPAD  140	/* the packed store's unused lane                  */
#define V_EAC    144	/* edge_x_AC_ish16                                  */
#define V_EAB    148	/* edge_x_AB_ish16                                  */
#define V_EBC    152	/* edge_x_BC_ish16                                  */
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
#define V_TMP    200
#define V_ROW    204	/* the staged row being rasterised                  */
#define V_LEFT   208	/* rows still to go                                */
#define V_OLDESP 212	/* %esp before the alignment, for the epilogue      */
#define FRAME    240

/* One staged row: x0,x1,x2,pad / y0,y1,y2,pad / color,alpha,pad,pad. */
#define R_X        0
#define R_Y       16
#define R_C       32
#define R_A       36
#define ROWBYTES  48

#define V_RBX0 (V_ROWBUF + R_X + 0)
#define V_RBX1 (V_ROWBUF + R_X + 4)
#define V_RBX2 (V_ROWBUF + R_X + 8)
#define V_RBY0 (V_ROWBUF + R_Y + 0)
#define V_RBY1 (V_ROWBUF + R_Y + 4)
#define V_RBY2 (V_ROWBUF + R_Y + 8)
#define V_RBC  (V_ROWBUF + R_C)
#define V_RBA  (V_ROWBUF + R_A)

/* Arguments, off the un-aligned frame pointer the prologue parks in %ebp. */
#define A(n) (20 + 4 * (n))(%ebp)

/* ---- helpers ---------------------------------------------------------- */

/*
 * Copy one of the six vertex orderings out of the staged row into the sorted
 * slots. \perm is that ordering as a pshufd immediate; x and y share a layout
 * so one immediate serves both. Unlike the gouraud kernel there is no third
 * group -- a flat face has one colour, not three, and it does not permute.
 */
.macro	FSORT perm
	movl	V_ROW(%esp), %ecx
	movdqa	R_X(%ecx), %xmm0
	movdqa	R_Y(%ecx), %xmm1
	pshufd	$\perm, %xmm0, %xmm0
	pshufd	$\perm, %xmm1, %xmm1
	movdqa	%xmm0, R_X(%ecx)
	movdqa	%xmm1, R_Y(%ecx)
.endm

/*
 * One edge slope, off the FP divider. %eax numerator in, truncated quotient
 * out; \den names the divisor. The exactness argument is in EDGEDIV in
 * gouraud_tri_i686.S and applies here unchanged: both operands are int32 and
 * therefore exact in a double, and the rounding error of a correctly-rounded
 * divide is provably smaller than the distance from the quotient to any
 * integer, so cvttsd2si returns what idivl would have.
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
 * flat_screen_fixed_edges_no_hclip, once per segment. Affine extrema on a
 * segment occur at its endpoints, so two evaluations settle every row of it.
 * Result written to \dstslot as 0 or 1. Clobbers %eax %ecx %edx.
 */
.macro	FNOHCLIP ea0, sa, eb0, sb, cnt, dstslot
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
 * The opaque span. %edi destination, %ecx pixel count (always >= 1). Clobbers
 * %eax %ecx %edx %edi and leaves control at \skip.
 *
 * Three phases: scalar until the destination is 16-byte aligned, then aligned
 * movdqa blocks of four pixels, then a scalar tail. The alignment phase is the
 * whole point -- an unaligned 16-byte store on a P4 is split internally and can
 * straddle a cache line, and paying at most three scalar stores once to avoid
 * that for the rest of the span is a trade that gets better the longer the
 * span is. Under eight pixels there is no block to align FOR, so that case
 * skips the machinery entirely and stores scalar, which is also the case the
 * span trace says is common.
 */
.macro	FILLOPAQUE skip
	movl	V_RGB(%esp), %eax
	cmpl	$8, %ecx
	jl	9300f

	/* ---- lead-in: scalar until %edi is 16-byte aligned ---- */
	movl	%edi, %edx
	negl	%edx
	andl	$15, %edx
	shrl	$2, %edx		/* 0..3 pixels                       */
	je	9100f
	subl	%edx, %ecx
9000:
	movl	%eax, (%edi)
	addl	$4, %edi
	decl	%edx
	jne	9000b
9100:
	/* ---- aligned body: four pixels per store ---- */
	movl	%ecx, %edx
	shrl	$2, %edx
	andl	$3, %ecx
	movdqa	V_COL4(%esp), %xmm0
	.p2align 4
9200:
	movdqa	%xmm0, (%edi)
	addl	$16, %edi
	decl	%edx
	jne	9200b

	testl	%ecx, %ecx
	je	\skip
9300:
	/* ---- tail: 1..7 pixels, or the whole span when it was short ---- */
	movl	%eax, (%edi)
	addl	$4, %edi
	decl	%ecx
	jne	9300b
.endm

/*
 * The alpha span. %edi destination, %ecx pixel count (always >= 1). Clobbers
 * %ecx %edx %edi and leaves control at \skip. Live constants, set once per
 * triangle: %xmm3 = K four times, %xmm4 = 0x00FFFFFF four times,
 * %xmm5 = (255 - alpha) in eight 16-bit lanes, %xmm6 = zero.
 *
 * alpha_blend() reduces, once the source colour is constant, to
 *
 *     out = K + per-channel((dst_channel * a_inv) >> 8)
 *
 * and each half of that maps onto SSE2 exactly, not approximately.
 *
 * The channel term first. The C keeps red and blue in one 32-bit multiply by
 * masking with 0x00FF00FF -- the two are far enough apart that scaling by an
 * 8-bit alpha cannot carry one into the other -- and does green separately.
 * Unpacking the four bytes into four 16-bit lanes achieves the same separation
 * more directly: each lane holds one channel, the widest product is
 * 255 * 255 = 65025 which still fits a lane, and psrlw $8 is the same shift the
 * C applies. So pmullw + psrlw computes (ch * a_inv) >> 8 per channel, which is
 * what the C's mask-and-shift extracts. packuswb cannot saturate here because
 * every lane is already at most 254.
 *
 * Then the top byte. Every term the C sums is masked to 0x00FF00FF or 0x0000FF00,
 * so its result always has a zero alpha byte; the unpack has no such mask, so
 * one pand puts that back.
 *
 * Finally K is added with paddd -- a full 32-bit add, carries between channels
 * included, because the C adds its four terms with plain `+` and a channel sum
 * over 255 really does spill into the next byte. Reproducing that spill is not
 * optional; it is what the reference client draws.
 *
 * The 1..3 pixel tail runs the identical sequence one pixel at a time rather
 * than a scalar transcription of the blend. A second implementation of the
 * arithmetic is a second thing that can drift.
 */
.macro	FILLALPHA skip
	cmpl	$4, %ecx
	jl	9600f

	movl	%ecx, %edx
	shrl	$2, %edx
	andl	$3, %ecx
	.p2align 4
9500:
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
	decl	%edx
	jne	9500b

	testl	%ecx, %ecx
	je	\skip
9600:
	movd	(%edi), %xmm0
	punpcklbw %xmm6, %xmm0
	pmullw	%xmm5, %xmm0
	psrlw	$8, %xmm0
	packuswb %xmm0, %xmm0
	pand	%xmm4, %xmm0
	paddd	%xmm3, %xmm0
	movd	%xmm0, (%edi)
	addl	$4, %edi
	decl	%ecx
	jne	9600b
.endm

/*
 * One trapezoid segment. %ebx left edge ish16, %esi right edge ish16. The
 * horizontal clamps are hoisted out of the loop: the caller proved once per
 * segment whether any row can leave the viewport, so the common case runs a
 * row loop with no clamping in it at all.
 *
 * Unlike the gouraud twin there is nothing to carry between rows but the two
 * edges -- no colour accumulator, so no join to step it at.
 */
.macro	FSEGMENT fill
	movl	V_EL(%esp), %ebx
	movl	V_ER(%esp), %esi
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

	addl	V_OFF(%esp), %eax
	movl	V_PIX(%esp), %edi
	leal	(%edi,%eax,4), %edi

	\fill 5400f
5400:
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

	addl	V_OFF(%esp), %eax
	movl	V_PIX(%esp), %edi
	leal	(%edi,%eax,4), %edi

	\fill 5800f
5800:
	movl	V_STRIDE(%esp), %eax
	addl	%eax, V_OFF(%esp)
	decl	V_CNT(%esp)
	jne	5500b

5900:
	movl	%ebx, V_EL(%esp)
	movl	%esi, V_ER(%esp)
.endm

/* Load the generic segment slots for one segment. */
.macro	FSETSEG el, er, sl, sr, nc, cnt
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

/*
 * The prologue both doors share: save, park an argument pointer in %ebp,
 * force the frame to a 16-byte boundary and take the four screen constants
 * once for the whole batch.
 */
.macro	FENTER
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

/* Stage the nine vertex arguments of a single-triangle call into the row. */
.macro	FSTAGE1
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
	movl	%eax, V_RBC(%esp)
	leal	V_ROWBUF(%esp), %eax
	movl	%eax, V_ROW(%esp)
	movl	$1, V_LEFT(%esp)
.endm

/*
 * ONE TRIANGLE, from the staged row at V_ROW.
 *
 * \p prefixes the named labels so the macro can be instantiated twice, and
 * \fill picks the span. \setup emits the per-triangle colour preparation,
 * which is the only other thing the two lanes disagree about.
 */
.macro	FLATBODY p, setup, fill
\p\()ysort:
	/* ---- the one-triangle door's y sort, with the same early-outs ----
	 *
	 * Only that door reaches this. A batched row arrives already ordered:
	 * the depth sort had all three y values in hand for the winding, so it
	 * does this once, in C, for every face it accepts -- and every kernel
	 * that reads the scratch, this one included, is spared a six-way
	 * compare ladder per triangle.
	 *
	 * Transcribed from the C wrapper, `<=` tie-breaks included: triangles
	 * that tie differently stop tiling with each other.
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
	FSORT	0xE4			/* 0, 1, 2 */
	jmp	\p\()face
\p\()sort_a2:
	testl	%ecx, %ecx
	js	\p\()next
	FSORT	0xD8			/* 0, 2, 1 */
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
	FSORT	0xC9			/* 1, 2, 0 */
	jmp	\p\()face
\p\()sort_b2:
	testl	%edx, %edx
	js	\p\()next
	FSORT	0xE1			/* 1, 0, 2 */
	jmp	\p\()face

\p\()sort_c:
	cmpl	V_SCRH(%esp), %edx
	jg	\p\()next
	cmpl	%ecx, %eax
	jg	\p\()sort_c2
	testl	%ecx, %ecx
	js	\p\()next
	FSORT	0xD2			/* 2, 0, 1 */
	jmp	\p\()face
\p\()sort_c2:
	testl	%eax, %eax
	js	\p\()next
	FSORT	0xC6			/* 2, 1, 0 */

/* ---- one triangle, vertices already in y order ------------------------- */
\p\()face:
	/* What the six-way ladder collapses to once the order is known: the
	 * top vertex below the viewport, or the bottom one above it. Two
	 * compares, both well predicted, against six that are not. */
	movl	V_ROW(%esp), %ecx
	movl	R_Y+0(%ecx), %eax	/* y_min */
	cmpl	V_SCRH(%esp), %eax
	jg	\p\()next
	movl	R_Y+8(%ecx), %eax	/* y_max */
	testl	%eax, %eax
	js	\p\()next

	movdqa	R_X(%ecx), %xmm0
	movdqa	R_Y(%ecx), %xmm1
	movdqa	%xmm0, V_X0(%esp)
	movdqa	%xmm1, V_Y0(%esp)

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

	/* sarea = dx_AB*dy_AC - dx_AC*dy_AB, kept only for the degenerate
	 * test -- there is no colour gradient here to divide by it. imul and
	 * not pmuludq for the reason gouraud_tri_i686.S measured: the packing
	 * costs more than the multiply saves on a P4. */
	movl	%edi, %eax
	imull	%ebx, %eax
	movl	%esi, %edx
	imull	%ebp, %edx
	subl	%edx, %eax
	testl	%eax, %eax
	je	\p\()next

	\setup

	/* The three edge slopes, all three at once, off one reciprocal.
	 * See edge_slopes_i686.inc. */
	EDGESLOPES3 %esi, %edi, %ebx, %ebp, V_SXAC

	/* starting edges */
	movl	V_X0(%esp), %eax
	shll	$16, %eax
	movl	%eax, V_EAC(%esp)
	movl	%eax, V_EAB(%esp)
	movl	V_X1(%esp), %eax
	shll	$16, %eax
	movl	%eax, V_EBC(%esp)

	/* ---- vertical clipping ---- */
	movl	V_Y0(%esp), %ecx
	testl	%ecx, %ecx
	jns	\p\()y0_ok
	movl	V_SXAC(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_EAC(%esp)
	/* Only pre-step A->B if that edge is actually walked; a near-horizontal
	 * AB times a distant y0 overflows the 16.16 product for a result that
	 * is then discarded. */
	cmpl	$0, V_Y1(%esp)
	jle	\p\()no_ab_prestep
	movl	V_SXAB(%esp), %eax
	imull	%ecx, %eax
	subl	%eax, V_EAB(%esp)
\p\()no_ab_prestep:
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
	FNOHCLIP V_EAB, V_SXAB, V_EAC, V_SXAC, V_N1, V_NC1
	/* BC is not advanced during the first trapezoid; AC is. */
	movl	V_N1(%esp), %eax
	imull	V_SXAC(%esp), %eax
	addl	V_EAC(%esp), %eax
	movl	%eax, V_TMP(%esp)
	FNOHCLIP V_EBC, V_SXBC, V_TMP, V_SXAC, V_N2, V_NC2

	FSETSEG V_EAB, V_EAC, V_SXAB, V_SXAC, V_NC1, V_N1
	FSEGMENT \fill
	/* AC carries its stepped value into the second trapezoid. */
	movl	V_ER(%esp), %eax
	movl	%eax, V_EAC(%esp)
	FSETSEG V_EBC, V_EAC, V_SXBC, V_SXAC, V_NC2, V_N2
	FSEGMENT \fill
	jmp	\p\()next

/* ---- AC on the left, AB/BC on the right ---- */
\p\()branchB:
	FNOHCLIP V_EAC, V_SXAC, V_EAB, V_SXAB, V_N1, V_NC1
	movl	V_N1(%esp), %eax
	imull	V_SXAC(%esp), %eax
	addl	V_EAC(%esp), %eax
	movl	%eax, V_TMP(%esp)
	FNOHCLIP V_TMP, V_SXAC, V_EBC, V_SXBC, V_N2, V_NC2

	FSETSEG V_EAC, V_EAB, V_SXAC, V_SXAB, V_NC1, V_N1
	FSEGMENT \fill
	movl	V_EL(%esp), %eax
	movl	%eax, V_EAC(%esp)
	FSETSEG V_EAC, V_EBC, V_SXAC, V_SXBC, V_NC2, V_N2
	FSEGMENT \fill

\p\()next:
	movl	V_ROW(%esp), %eax
	addl	$ROWBYTES, %eax
	movl	%eax, V_ROW(%esp)
	decl	V_LEFT(%esp)
	jne	\p\()face

\p\()done:
	movl	V_OLDESP(%esp), %esp
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret
.endm

/*
 * Opaque per-triangle colour: one palette lookup, then the same word four
 * times for the aligned store. The index is a vertex colour and always in
 * range, so unlike the gouraud interpolant it needs no clamp.
 */
.macro	FSETUP_OPAQUE
	movl	V_ROW(%esp), %ecx
	movl	R_C(%ecx), %edx
	movl	_g_hsl16_to_rgb_table(,%edx,4), %edx
	movl	%edx, V_RGB(%esp)
	movd	%edx, %xmm0
	pshufd	$0x00, %xmm0, %xmm0
	movdqa	%xmm0, V_COL4(%esp)
.endm

/*
 * Alpha per-triangle constants. The source colour's whole contribution is
 * fixed for the triangle, so it is computed once, exactly as the C computes
 * it, and thereafter added with one paddd per four pixels:
 *
 *     K = (((o & 0xFF00FF) * a) >> 8) & 0xFF00FF
 *       + (((o & 0xFF00)   * a) >> 8) & 0xFF00
 *
 * The multiplies must be unsigned -- 0xFF00FF * 0xFF is 4,261,543,425, which
 * fits a uint32 and overflows a signed one. mull is unsigned, and only its low
 * half is taken, which is the 32-bit product the C computes. It also writes
 * %edx, so the colour is parked before the first one rather than kept there.
 */
.macro	FSETUP_ALPHA
	movl	V_ROW(%esp), %ecx
	movl	R_C(%ecx), %edx
	movl	_g_hsl16_to_rgb_table(,%edx,4), %edx	/* o */
	movl	%edx, V_RGB(%esp)
	movl	R_A(%ecx), %ecx				/* a */

	/* red+blue term */
	movl	%edx, %eax
	andl	$0x00FF00FF, %eax
	mull	%ecx
	shrl	$8, %eax
	andl	$0x00FF00FF, %eax
	movl	%eax, V_TMP(%esp)
	/* green term */
	movl	V_RGB(%esp), %eax
	andl	$0x0000FF00, %eax
	mull	%ecx
	shrl	$8, %eax
	andl	$0x0000FF00, %eax
	addl	V_TMP(%esp), %eax			/* K */
	movd	%eax, %xmm3
	pshufd	$0x00, %xmm3, %xmm3

	/* a_inv, broadcast to eight 16-bit lanes */
	movl	$0xFF, %eax
	subl	%ecx, %eax
	movd	%eax, %xmm5
	pshuflw	$0x00, %xmm5, %xmm5
	pshufd	$0x00, %xmm5, %xmm5

	/* 0x00FFFFFF four times, and zero */
	pcmpeqd	%xmm4, %xmm4
	psrld	$8, %xmm4
	pxor	%xmm6, %xmm6
.endm

/* ---- entries ---------------------------------------------------------- */

/*
 * void toridraw_flat_opaque_s4_presorted_run_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * `rows` is `count` staged rows of ROWBYTES, 16-byte aligned, each holding one
 * triangle's three screen x, three screen y, its hsl16 colour and its alpha.
 * The whole run is rasterised without returning: no call, no cdecl marshalling
 * and no register save/restore per triangle. Draw order is preserved by the
 * caller, which flushes the batch the moment a face it cannot take appears.
 */
	.globl	_toridraw_flat_opaque_s4_presorted_run_asm
_toridraw_flat_opaque_s4_presorted_run_asm:
	FENTER
	movl	A(5), %eax
	testl	%eax, %eax
	jle	Lo_done
	movl	%eax, V_LEFT(%esp)
	movl	A(4), %eax
	movl	%eax, V_ROW(%esp)
	jmp	Lo_face

/*
 * void toridraw_flat_opaque_s4_sorting_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, int x0, int x1, int x2, int y0, int y1, int y2,
 *     int color_hsl16);
 *
 * The one-triangle door. It stages its arguments into a single row and walks
 * the same body, so the two cannot diverge.
 */
	.globl	_toridraw_flat_opaque_s4_sorting_asm
_toridraw_flat_opaque_s4_sorting_asm:
	FENTER
	FSTAGE1

	FLATBODY Lo_, FSETUP_OPAQUE, FILLOPAQUE

/*
 * void toridraw_flat_alpha_s4_presorted_run_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * As above, blending instead of storing. The per-row alpha is in the staged
 * row, so a run may mix opacities.
 */
	.globl	_toridraw_flat_alpha_s4_presorted_run_asm
_toridraw_flat_alpha_s4_presorted_run_asm:
	FENTER
	movl	A(5), %eax
	testl	%eax, %eax
	jle	La_done
	movl	%eax, V_LEFT(%esp)
	movl	A(4), %eax
	movl	%eax, V_ROW(%esp)
	jmp	La_face

/*
 * void toridraw_flat_alpha_s4_sorting_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, int x0, int x1, int x2, int y0, int y1, int y2,
 *     int color_hsl16, int alpha);
 */
	.globl	_toridraw_flat_alpha_s4_sorting_asm
_toridraw_flat_alpha_s4_sorting_asm:
	FENTER
	FSTAGE1
	movl	A(11), %eax
	movl	%eax, V_RBA(%esp)

	FLATBODY La_, FSETUP_ALPHA, FILLALPHA
