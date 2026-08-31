/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES   (i686 only)
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The flat triangle, batched, in handrolled Xtensa LX7 for the ESP32-S3 --
 * the third lane of the presorted-run door, beside tri.flat.i686.S (the XP
 * lane's SSE2) and tri.flat.aarch64.S (NEON). Only the two RUN doors exist
 * here, as on AArch64: the depth sort orders every batched row on the way
 * past, and the per-face path keeps the C, which is the arm the A/B baseline
 * measures.
 *
 * THIS ONE NAMES A FORMAT IN ITS FILENAME AND THE OTHER TWO DO NOT, and that
 * is the whole reason it is a separate file rather than an arm of theirs. The
 * i686 and AArch64 kernels index a 4-byte palette and store 4-byte pixels;
 * they are XRGB8888 kernels and flat_tri_asm.h gates them on
 * TORIPIXEL_IS_XRGB8888. A part with a 16-bit panel does not have that
 * framebuffer. So this kernel indexes a 2-byte palette, stores 2-byte pixels,
 * and blends with the RGB565 spread arithmetic -- a different claim, hence a
 * different name and a gate of its own. See KERNELS.md, "The pixel format".
 *
 * WHAT IS DIFFERENT FROM THE AArch64 KERNEL, AND WHY
 *
 *   - THE EDGE SLOPES ARE AN EXACT INTEGER DIVIDE. The NEON lane folds three
 *     divides into one packed float reciprocal because it has no integer
 *     divider worth using; LX7 has QUOS, one instruction, truncating toward
 *     zero exactly as C's division does. So this lane IS the exact edge lane
 *     the NEON kernel has to be assembled into with -DTORIDRAW_EDGE_IDIV to
 *     be scored bit for bit, and there is no approximate variant to choose.
 *
 *   - THE TRIANGLE DOES NOT LIVE IN REGISTERS. AArch64 has thirty-one and
 *     keeps everything live across both trapezoid walks; a windowed Xtensa
 *     frame has fourteen usable, and the alpha span alone wants seven of
 *     them. So the per-triangle prologue computes everything ONCE and parks
 *     it in the frame, and each segment loads the five values it walks with.
 *     That is a dozen loads and stores per TRIANGLE against a per-PIXEL cost,
 *     which is the trade this shape is picking.
 *
 *   - ONE SEGMENT BODY, NOT TWO. The AArch64 kernel expands SEGMENT four
 *     times (branchA and branchB, two trapezoids each). Here the prologue
 *     decides which edge is on the left and writes the answer into the frame,
 *     so there is one expansion per trapezoid -- four span fills in the
 *     kernel instead of eight. On a part with 32 KB of instruction cache that
 *     is not tidiness.
 *
 *   - THE OPAQUE SPAN IS A PIE VECTOR STORE. A flat span is a constant, so
 *     the fill is a memset of a halfword: scalar to the first 16-byte
 *     boundary, EE.VST.128.IP for eight pixels at a time, scalar tail. There
 *     is no vector LOAD in it -- nothing is read -- which is what makes it
 *     worth doing on a bus this narrow.
 *
 *   - THE ALPHA SPAN IS SCALAR, ON PURPOSE. RGB565 blends by spreading the
 *     pixel to G | R....B across a 32-bit word and multiplying the whole
 *     thing once (toripixel_rgb565_alpha_blend). The intermediate needs 32
 *     bits per lane and PIE multiplies 16-bit lanes, so a vector version
 *     would have to deinterleave the channels and would not be the same
 *     arithmetic. Twelve scalar instructions that ARE the same arithmetic
 *     beat a vector transcription that has to be argued about.
 *
 * COPROCESSOR STATE. The opaque door writes q0, which is lazily-saved
 * coprocessor state under ESP-IDF: the first use in a task takes an exception
 * that allocates its save area. That is fine in a task and NOT fine in an
 * ISR. Build with -DTORIDRAW_XTENSA_NO_PIE to get a scalar 32-bit store loop
 * instead and touch no coprocessor register at all; the pixels are the same.
 *
 * ROW LAYOUT. rows is count records of TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS
 * ints: x0,x1,x2,pad / y0,y1,y2,pad / color,alpha,pad,pad -- the screen
 * coordinates with the viewport offset already folded in, already in y order.
 * The same 48-byte record the i686 and AArch64 kernels read; the record is
 * ints and does not know the pixel format.
 *
 * ABI. Windowed or CALL0, whichever the build selected. Six arguments land in
 * a2-a7 either way, no call is made, and nothing outside the window is
 * touched -- so the CALL0 arm is the windowed one with entry/retw swapped for
 * a hand-rolled frame.
 */

/*
 * THE FORMAT GATE IS NOT HERE. An assembly file cannot include
 * graphics/pixel_format.h -- it is C -- so what keeps this kernel away from a
 * framebuffer it does not write is the same thing that keeps tri.flat.i686.S
 * off an ARM lane: the build assembles it only where it belongs, and
 * graphics/raster/flat/flat_tri_asm.h, which DOES read the format, is what
 * routes calls to it. On a build that assembles this and selects another
 * format the header declines, nothing reaches these symbols, and they are
 * dead bytes rather than wrong pixels.
 */

#if defined(__XTENSA__)

/* The staged row, in bytes. Must match TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS. */
#define R_X   0
#define R_Y   16
#define R_C   32
#define R_A   36
#define ROWBYTES 48

/*
 * The frame.
 *
 * THE TOP SIXTEEN BYTES ARE NOT OURS. On a window overflow the handler spills
 * the CALLER's a0-a3 to the sixteen bytes below the CALLER's stack pointer,
 * which is [FRAMESIZE-16, FRAMESIZE) of this frame. So locals start at zero
 * and stop at FRAMESIZE-16 -- which is also where the compiler puts them:
 * compile any windowed leaf and look, `entry a1, 64` with the buffer at a1+0.
 *
 * Getting this backwards does not fault here. It corrupts the CALLER's saved
 * registers, and only when a window overflow happens to land inside this
 * call, so it presents as the caller reading a garbage pointer some
 * iterations later. It cost a device run to find; it is written down so it
 * costs nobody a second one.
 *
 * FR_VEC is first because it is the only field with an alignment requirement:
 * offset zero of a frame is 16-byte aligned because the stack pointer is, and
 * EE.VLD.128 needs that.
 */
#define FR_VEC       0
#define FR_PIXBUF    16
#define FR_STRIDE_B  20
#define FR_SCRW      24
#define FR_SCRH      28
#define FR_ROWS      32
#define FR_LEFT      36
#define FR_PAL       40
#define FR_L1        44
#define FR_R1        48
#define FR_SL1       52
#define FR_SR1       56
#define FR_EBC       60
#define FR_SXBC      64
#define FR_SXAC      68
#define FR_N1        72
#define FR_N2        76
#define FR_NC1       80
#define FR_NC2       84
#define FR_BRANCH    88
#define FR_RETADDR   92
/* 96..111: the caller's spill area. Nothing of ours goes here. */
#define FRAMESIZE    112

#define RGB565_SPREAD_MASK 0x07E0F81F

	.text

/*
 * Register map once the prologue has parked the triangle:
 *
 *   a1  stack pointer          a2  row pointer into the framebuffer (bytes)
 *   a3  stride in BYTES        a4  screen_width where needed, else scratch
 *   a5  left edge   (16.16)    a6  right edge (16.16)
 *   a7  left step   (16.16)    a8  right step (16.16)
 *   a9  rows left in this trapezoid
 *   a10 pixel (opaque)   |  K, the folded source term (alpha)
 *   a11 --               |  RGB565_SPREAD_MASK (alpha)
 *   a12 --               |  32 - (alpha >> 3)  (alpha)
 *   a13 a14 a15  scratch; the span fill also takes a4
 *   q0  eight copies of the pixel (opaque, PIE builds)
 *
 * Everything else -- the second trapezoid's edge, both no-clip proofs, both
 * row counts, the palette, the run cursor -- is in the frame.
 */

/*
 * flat_screen_fixed_edges_no_hclip, once per trapezoid. An affine edge takes
 * its extrema at the endpoints, so two evaluations settle every row of the
 * segment. dst <- 1 when no row of it can leave [0, screen_width).
 */
	.macro	NOHCLIP ea, sa, eb, sb, n, dst, t0, t1
	movi	\dst, 1
	blti	\n, 1, 8891f			/* n <= 0: nothing to clip      */
	addi	\t0, \n, -1
	mull	\t1, \sa, \t0
	add	\t1, \t1, \ea			/* e_a_last                     */
	min	\t1, \t1, \ea			/* min_e_a                      */
	bltz	\t1, 8890f			/* < 0: the left edge escapes   */
	mull	\t1, \sb, \t0
	add	\t1, \t1, \eb			/* e_b_last                     */
	max	\t1, \t1, \eb			/* max_e_b                      */
	l32i	\t0, a1, FR_SCRW
	slli	\t0, \t0, 16
	blt	\t1, \t0, 8891f
8890:
	movi	\dst, 0
8891:
	.endm

/*
 * The opaque span. a13 destination (2-byte aligned), a14 pixel count (>= 1),
 * a10 the pixel, q0 eight copies of it. Clobbers a13, a14, a15, a4.
 *
 * Under sixteen pixels the alignment head costs more than the vector body
 * saves, so the whole span goes down the halfword loop.
 */
	.macro	FILLOPAQUE
#if defined(TORIDRAW_XTENSA_NO_PIE)
	/* No coprocessor register touched: 32-bit stores, two pixels each. */
	blti	a14, 4, 8807f
	extui	a15, a13, 0, 2			/* ptr & 3                      */
	beqz	a15, 8802f
	s16i	a10, a13, 0			/* one pixel to a 4-byte edge   */
	addi	a13, a13, 2
	addi	a14, a14, -1
8802:
	srli	a15, a14, 1
	slli	a4, a10, 16
	or	a4, a4, a10
	loopnez	a15, 8804f
	s32i	a4, a13, 0
	addi	a13, a13, 4
8804:
	extui	a14, a14, 0, 1
8807:
	loopnez	a14, 8809f
	s16i	a10, a13, 0
	addi	a13, a13, 2
8809:
#else
	blti	a14, 16, 8807f
	extui	a15, a13, 0, 4			/* ptr & 15                     */
	beqz	a15, 8802f
	movi	a4, 16
	sub	a15, a4, a15			/* bytes to the boundary        */
	srli	a15, a15, 1			/* ... as pixels, 1..7          */
	sub	a14, a14, a15
	loopnez	a15, 8801f
	s16i	a10, a13, 0
	addi	a13, a13, 2
8801:
8802:
	/* Two stores per iteration, not one. A zero-overhead loop with a
	 * single-instruction body is a shape the assembler does not police --
	 * it accepts a loop ending in a branch, which is architecturally
	 * illegal -- so nothing here is checked by the build, and the pairing
	 * costs nothing: it halves the loop overhead as well. */
	srli	a15, a14, 4			/* whole sixteen-pixel blocks   */
	loopnez	a15, 8803f
	ee.vst.128.ip	q0, a13, 16
	ee.vst.128.ip	q0, a13, 16
8803:
	bbci	a14, 3, 8804f			/* the odd eight-pixel block    */
	ee.vst.128.ip	q0, a13, 16
8804:
	extui	a14, a14, 0, 3			/* 0..7 left over               */
8807:
	loopnez	a14, 8809f
	s16i	a10, a13, 0
	addi	a13, a13, 2
8809:
#endif
	.endm

/*
 * The alpha span. a13 destination, a14 pixel count (>= 1).
 *
 *   b   = (dst | dst << 16) & 0x07E0F81F
 *   out = ((b * (32 - a) + K) >> 5) & 0x07E0F81F,  folded back to 16 bits
 *
 * where K = spread(src) * a was computed once for the triangle. This is
 * toripixel_rgb565_alpha_blend with the source half hoisted, instruction for
 * instruction: the shift right by five must be LOGICAL, because b * 32
 * reaches 0xFC1F03E0 and the C does it in uint32_t.
 *
 * Clobbers a13, a14, a15, a4.
 */
	.macro	FILLALPHA
	loopnez	a14, 8819f
	l16ui	a15, a13, 0
	slli	a4, a15, 16
	or	a15, a15, a4
	and	a15, a15, a11
	mull	a15, a15, a12
	add	a15, a15, a10
	srli	a15, a15, 5
	and	a15, a15, a11
	extui	a4, a15, 16, 16
	or	a15, a15, a4
	s16i	a15, a13, 0
	addi	a13, a13, 2
8819:
	.endm

/*
 * One trapezoid. a5/a6 are the live left and right edges -- stepped in place,
 * so the second trapezoid inherits whatever the first left in the edge they
 * share -- a7/a8 their steps, a9 the row count, a13 the no-clip proof
 * (consumed). The horizontal clamps are hoisted out of the row loop: the
 * common case runs a body with no clamping in it at all.
 */
	.macro	SEGMENT p, tag, fill
	blti	a9, 1, \p\()\tag\()_done
	beqz	a13, \p\()\tag\()_cl_loop

	/* ---- proved inside the viewport: no left/right clamp ---- */
\p\()\tag\()_nc_loop:
	mov	a13, a5
	mov	a14, a6
	add	a5, a5, a7
	add	a6, a6, a8
	beq	a13, a14, \p\()\tag\()_nc_skip
	srai	a13, a13, 16
	srai	a14, a14, 16
	bge	a13, a14, \p\()\tag\()_nc_skip
	sub	a14, a14, a13			/* pixel count                  */
	addx2	a13, a13, a2			/* row + x * sizeof(pixel)      */
	\fill
\p\()\tag\()_nc_skip:
	add	a2, a2, a3
	addi	a9, a9, -1
	bnez	a9, \p\()\tag\()_nc_loop
	j	\p\()\tag\()_done

	/* ---- may leave the viewport: clamp both ends ----
	 *
	 * x_end clamps to screen_width - 1, not to screen_width: that is what
	 * the C reference does, so the last column of a clipped span is not
	 * drawn, and reproducing it is not optional.
	 */
\p\()\tag\()_cl_loop:
	mov	a13, a5
	mov	a14, a6
	add	a5, a5, a7
	add	a6, a6, a8
	beq	a13, a14, \p\()\tag\()_cl_skip
	l32i	a4, a1, FR_SCRW
	srai	a14, a14, 16
	addi	a4, a4, -1
	min	a14, a14, a4
	srai	a13, a13, 16
	movi	a4, 0
	max	a13, a13, a4
	bge	a13, a14, \p\()\tag\()_cl_skip
	sub	a14, a14, a13
	addx2	a13, a13, a2
	\fill
\p\()\tag\()_cl_skip:
	add	a2, a2, a3
	addi	a9, a9, -1
	bnez	a9, \p\()\tag\()_cl_loop
\p\()\tag\()_done:
	.endm

/*
 * Per-triangle colour, opaque: one palette lookup at a 2-byte scale -- the
 * format claim this file makes -- and eight copies of it staged through the
 * frame for the vector store. Building the splat in memory rather than with
 * EE.MOVI.32.Q keeps the lane numbering out of it: four identical words are
 * four identical words whichever lane each lands in.
 */
	.macro	SETUP_OPAQUE
	l32i	a13, a1, FR_ROWS
	l32i	a14, a13, R_C
	l32i	a4, a1, FR_PAL
	addx2	a14, a14, a4
	l16ui	a10, a14, 0
#if !defined(TORIDRAW_XTENSA_NO_PIE)
	slli	a14, a10, 16
	or	a14, a14, a10
	s32i	a14, a1, FR_VEC + 0
	s32i	a14, a1, FR_VEC + 4
	s32i	a14, a1, FR_VEC + 8
	s32i	a14, a1, FR_VEC + 12
	addi	a15, a1, FR_VEC
	ee.vld.128.ip	q0, a15, 0
#endif
	.endm

/*
 * Per-triangle constants, alpha. The staged row carries 0..255; RGB565
 * quantises that to 32 steps, which is what makes the blend one multiply for
 * the whole pixel instead of one per channel.
 */
	.macro	SETUP_ALPHA
	l32i	a13, a1, FR_ROWS
	l32i	a14, a13, R_C
	l32i	a15, a13, R_A
	l32i	a4, a1, FR_PAL
	addx2	a14, a14, a4
	l16ui	a14, a14, 0			/* the source pixel             */
	srli	a15, a15, 3			/* a = alpha >> 3, 0..31        */
	movi	a11, RGB565_SPREAD_MASK
	slli	a4, a14, 16
	or	a14, a14, a4
	and	a14, a14, a11			/* spread(src)                  */
	mull	a10, a14, a15			/* K = spread(src) * a          */
	movi	a12, 32
	sub	a12, a12, a15			/* 32 - a                       */
	.endm

/*
 * The run body. p prefixes the labels, setup the per-triangle colour work,
 * fill the span. Vertices arrive already in y order, so there is no sort
 * ladder here -- that is the whole point of the door.
 */
	.macro	FLATBODY p, setup, fill
\p\()face:
	l32i	a13, a1, FR_ROWS
	l32i	a5, a13, R_X + 0		/* x0                           */
	l32i	a6, a13, R_X + 4		/* x1                           */
	l32i	a7, a13, R_X + 8		/* x2                           */
	l32i	a8, a13, R_Y + 0		/* y0                           */
	l32i	a9, a13, R_Y + 4		/* y1                           */
	l32i	a10, a13, R_Y + 8		/* y2                           */

	/* Off the bottom or off the top: two well-predicted compares. */
	l32i	a14, a1, FR_SCRH
	blt	a14, a8, \p\()next
	bltz	a10, \p\()next

	sub	a11, a10, a8			/* dy_AC                        */
	beqz	a11, \p\()next
	sub	a12, a7, a5			/* dx_AC                        */
	sub	a15, a6, a5			/* dx_AB                        */
	sub	a4, a9, a8			/* dy_AB                        */

	/* sarea = dx_AB*dy_AC - dx_AC*dy_AB, only for the degenerate test. */
	mull	a3, a15, a11
	mull	a14, a12, a4
	sub	a3, a3, a14
	beqz	a3, \p\()next

	/* The three edge slopes. dy_AC > 0 is settled by the y order and the
	 * test above; the other two can be zero and are guarded. */
	slli	a14, a12, 16
	quos	a14, a14, a11			/* sxAC                         */
	s32i	a14, a1, FR_SXAC

	movi	a3, 0				/* sxAB                         */
	blti	a4, 1, \p\()no_ab
	slli	a3, a15, 16
	quos	a3, a3, a4
\p\()no_ab:

	sub	a13, a10, a9			/* dy_BC                        */
	sub	a12, a7, a6			/* dx_BC                        */
	movi	a15, 0				/* sxBC                         */
	blti	a13, 1, \p\()no_bc
	slli	a15, a12, 16
	quos	a15, a15, a13
\p\()no_bc:
	s32i	a15, a1, FR_SXBC

	/* starting edges */
	slli	a11, a5, 16			/* eAC                          */
	mov	a12, a11			/* eAB                          */
	slli	a13, a6, 16			/* eBC                          */

	/* ---- vertical clipping ---- */
	bgez	a8, \p\()y0_ok
	mull	a4, a14, a8
	sub	a11, a11, a4			/* eAC -= sxAC * y0             */
	/* Only pre-step A->B if that edge is actually walked: rows [y0, y1)
	 * use it, so a y1 at or above the viewport leaves the span empty, and
	 * a near-horizontal AB times a distant y0 overflows the 16.16 product
	 * for a result that is then discarded. */
	blti	a9, 1, \p\()no_ab_prestep
	mull	a4, a3, a8
	sub	a12, a12, a4
\p\()no_ab_prestep:
	movi	a8, 0
\p\()y0_ok:
	bgez	a9, \p\()y1_ok
	mull	a4, a15, a9
	sub	a13, a13, a4			/* eBC -= sxBC * y1             */
	movi	a9, 0
\p\()y1_ok:
	s32i	a13, a1, FR_EBC

	/* the first row of the framebuffer this triangle touches */
	l32i	a4, a1, FR_STRIDE_B
	l32i	a2, a1, FR_PIXBUF
	mull	a5, a8, a4
	add	a2, a2, a5

	/* ---- the two row counts ---- */
	l32i	a6, a1, FR_SCRH
	bge	a6, a9, \p\()y1_in
	mov	a9, a6
	mov	a10, a6
	j	\p\()clamped
\p\()y1_in:
	min	a10, a10, a6
\p\()clamped:
	movi	a7, 0
	sub	a5, a9, a8			/* n1                           */
	max	a5, a5, a7
	sub	a6, a10, a9			/* n2                           */
	max	a6, a6, a7
	s32i	a5, a1, FR_N1
	s32i	a6, a1, FR_N2

	/*
	 * Which edge is on the left. A flat top has no AB to compare against,
	 * so the answer comes off BC instead; every other triangle compares
	 * AC against AB. The same test, in the same order, as the other two
	 * lanes.
	 */
	l32i	a15, a1, FR_SXBC
	bne	a8, a9, \p\()flat_no
	bge	a15, a14, \p\()branchA
	j	\p\()branchB
\p\()flat_no:
	bge	a14, a3, \p\()branchA
	j	\p\()branchB

/* ---- AB/BC on the left, AC on the right ---- */
\p\()branchA:
	NOHCLIP a12, a3, a11, a14, a5, a7, a13, a4
	s32i	a7, a1, FR_NC1
	/* AC is advanced across the first trapezoid; BC is not. */
	mull	a7, a5, a14
	add	a7, a7, a11
	l32i	a13, a1, FR_EBC
	NOHCLIP a13, a15, a7, a14, a6, a4, a10, a9
	s32i	a4, a1, FR_NC2
	s32i	a12, a1, FR_L1			/* left  = eAB                  */
	s32i	a11, a1, FR_R1			/* right = eAC                  */
	s32i	a3, a1, FR_SL1
	s32i	a14, a1, FR_SR1
	movi	a13, 0
	s32i	a13, a1, FR_BRANCH
	j	\p\()walk

/* ---- AC on the left, AB/BC on the right ---- */
\p\()branchB:
	NOHCLIP a11, a14, a12, a3, a5, a7, a13, a4
	s32i	a7, a1, FR_NC1
	mull	a7, a5, a14
	add	a7, a7, a11
	l32i	a13, a1, FR_EBC
	NOHCLIP a7, a14, a13, a15, a6, a4, a10, a9
	s32i	a4, a1, FR_NC2
	s32i	a11, a1, FR_L1			/* left  = eAC                  */
	s32i	a12, a1, FR_R1			/* right = eAB                  */
	s32i	a14, a1, FR_SL1
	s32i	a3, a1, FR_SR1
	movi	a13, 1
	s32i	a13, a1, FR_BRANCH

\p\()walk:
	\setup
	l32i	a3, a1, FR_STRIDE_B
	l32i	a5, a1, FR_L1
	l32i	a6, a1, FR_R1
	l32i	a7, a1, FR_SL1
	l32i	a8, a1, FR_SR1
	l32i	a9, a1, FR_N1
	l32i	a13, a1, FR_NC1
	SEGMENT \p, s1, \fill

	/*
	 * The second trapezoid. Whichever edge register the shared AC was
	 * walking still holds it, advanced; only the OTHER side changes, to
	 * BC. That is the one asymmetry between the two branches, and it is
	 * why this is a four-load fixup rather than a second copy of the walk.
	 */
	l32i	a13, a1, FR_BRANCH
	l32i	a14, a1, FR_SXAC
	bnez	a13, \p\()seg2_b
	l32i	a5, a1, FR_EBC			/* left  = eBC                  */
	l32i	a7, a1, FR_SXBC
	mov	a8, a14				/* right stays AC               */
	j	\p\()seg2_go
\p\()seg2_b:
	l32i	a6, a1, FR_EBC			/* right = eBC                  */
	l32i	a8, a1, FR_SXBC
	mov	a7, a14				/* left stays AC                */
\p\()seg2_go:
	l32i	a9, a1, FR_N2
	l32i	a13, a1, FR_NC2
	SEGMENT \p, s2, \fill

\p\()next:
	l32i	a13, a1, FR_ROWS
	addi	a13, a13, ROWBYTES
	s32i	a13, a1, FR_ROWS
	l32i	a14, a1, FR_LEFT
	addi	a14, a14, -1
	s32i	a14, a1, FR_LEFT
	bnez	a14, \p\()face
	.endm

/*
 * Take the frame and park the six arguments and the two run-invariant
 * constants. Nothing below reads a2-a7 as arguments again.
 */
	.macro	ENTER
#if defined(__XTENSA_CALL0_ABI__)
	addi	a1, a1, -FRAMESIZE
	s32i	a0, a1, FR_RETADDR
#else
	entry	a1, FRAMESIZE
#endif
	s32i	a2, a1, FR_PIXBUF
	slli	a3, a3, 1			/* stride: pixels -> bytes      */
	s32i	a3, a1, FR_STRIDE_B
	s32i	a4, a1, FR_SCRW
	s32i	a5, a1, FR_SCRH
	s32i	a6, a1, FR_ROWS
	s32i	a7, a1, FR_LEFT
	movi	a13, g_hsl16_to_pixel_table
	s32i	a13, a1, FR_PAL
	.endm

	.macro	LEAVE
#if defined(__XTENSA_CALL0_ABI__)
	l32i	a0, a1, FR_RETADDR
	addi	a1, a1, FRAMESIZE
	ret
#else
	retw
#endif
	.endm

/*
 * void toridraw_flat_opaque_s4_presorted_run_rgb565_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 */
	.literal_position
	.align	4
	.globl	toridraw_flat_opaque_s4_presorted_run_rgb565_asm
	.type	toridraw_flat_opaque_s4_presorted_run_rgb565_asm, @function
toridraw_flat_opaque_s4_presorted_run_rgb565_asm:
	ENTER
	l32i	a13, a1, FR_LEFT
	blti	a13, 1, Lo_ret
	FLATBODY Lo_, SETUP_OPAQUE, FILLOPAQUE
Lo_ret:
	LEAVE
	.size	toridraw_flat_opaque_s4_presorted_run_rgb565_asm, . - toridraw_flat_opaque_s4_presorted_run_rgb565_asm

/*
 * void toridraw_flat_alpha_s4_presorted_run_rgb565_asm(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * As above, blending instead of storing. The opacity is per ROW, so a run may
 * mix them.
 */
	.literal_position
	.align	4
	.globl	toridraw_flat_alpha_s4_presorted_run_rgb565_asm
	.type	toridraw_flat_alpha_s4_presorted_run_rgb565_asm, @function
toridraw_flat_alpha_s4_presorted_run_rgb565_asm:
	ENTER
	l32i	a13, a1, FR_LEFT
	blti	a13, 1, La_ret
	FLATBODY La_, SETUP_ALPHA, FILLALPHA
La_ret:
	LEAVE
	.size	toridraw_flat_alpha_s4_presorted_run_rgb565_asm, . - toridraw_flat_alpha_s4_presorted_run_rgb565_asm

#endif /* __XTENSA__ */
