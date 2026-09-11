/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES   (i686 only)
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The gouraud triangle, batched, in handrolled Xtensa LX7 for the ESP32-S3 --
 * the twin of tri.flat.rgb565.xtensa.S and the third lane of this family,
 * beside tri.gouraudhsllightness.i686.S and .aarch64.S. Only the two RUN
 * doors exist here, as on AArch64; the per-face path keeps the C, which is
 * the arm the A/B baseline measures.
 *
 * It serves both 16-bit orders, RGB565 and RGB565_BE, under entry points that
 * each name the one they write. See tri.flat.rgb565.xtensa.S for why the
 * filename fixes the channels and spans the byte order.
 *
 * WHAT IS DIFFERENT FROM THE AArch64 KERNEL, AND WHY
 *
 *   - THE COLOUR GRADIENTS DIVIDE. This is the one real divergence in the
 *     lane and it is forced. Both steps are `(numerator << 8) / sarea`, and
 *     every other lane computes them from ONE double reciprocal and two
 *     multiplies -- gouraudhsllightness_barycentric_steps.h argues that case
 *     at length, against a core that has double. LX7 does not: its FPU is
 *     single precision, `numerator << 8` overruns a 24-bit mantissa by
 *     construction, and `double` here is eight libgcc calls per triangle.
 *     QUOS is one instruction and about twenty cycles, and it is EXACT where
 *     the double arm is out by one wherever sarea divides the numerator.
 *
 *     So this kernel takes the integer divide, and the C reference it is
 *     scored against is built with -DTORIDRAW_GOURAUD_STEP_IDIV, which is
 *     that same choice. The separation is the one -DTORIDRAW_EDGE_IDIV draws
 *     for the edge slopes: with both sides on the exact arm every pixel must
 *     match, so a difference is a bug in the walk and never a rounding.
 *
 *   - THE TRIANGLE DOES NOT LIVE IN REGISTERS. AArch64 keeps the slopes, the
 *     edges, the colour accumulator and both steps live across both trapezoid
 *     walks in its thirty-one registers. Fourteen do not go that far once the
 *     span fill wants six, so the prologue parks the triangle in the frame
 *     and each row loads the five values it walks with. The same trade the
 *     flat kernel makes, with more to park: the row colour accumulator, both
 *     edge steps and step_y are all frame-resident, and each is touched once
 *     per ROW against a span fill that is touched once per PIXEL.
 *
 *   - THE RETURN ADDRESS IS SPILLED. a0 is the sixth scratch register the
 *     alpha span needs and there is no seventh anywhere. ENTER writes it to
 *     the frame and LEAVE restores it, which is legal precisely because a
 *     window overflow spills and an underflow restores whatever a0 holds --
 *     the value only has to be right at RETW, not throughout. The flat kernel
 *     does not need this and does not do it.
 *
 *   - THE FOUR-PIXEL BLOCK IS FOUR STORES, not one wide one. The colour
 *     quantization is per four pixels and is visible output
 *     (toridraw_scanline_parity_test holds it), so a wider store would have
 *     to carry two palette entries in one vector and rebuild it every block.
 *     Four halfword stores against a palette lookup and a clamp is not where
 *     this kernel's time goes.
 *
 * ROW LAYOUT. `rows` is `count` 48-byte records: x0,x1,x2,pad / y0,y1,y2,pad
 * / c0,c1,c2,alpha -- already in y order, colours permuted with the vertices,
 * the viewport offset folded in. The same record the other two lanes read;
 * the opaque door never looks at lane 11.
 *
 * ABI. Windowed or CALL0. Six arguments land in a2-a7 either way and no call
 * is made.
 */

#if defined(__XTENSA__)

#if defined(TORIDRAW_XTENSA_RGB565_BE)
#define SYM_OPAQUE toridraw_gouraud_opaque_s4_presorted_run_rgb565_be_asm
#define SYM_ALPHA  toridraw_gouraud_alpha_s4_presorted_run_rgb565_be_asm
#else
#define SYM_OPAQUE toridraw_gouraud_opaque_s4_presorted_run_rgb565_asm
#define SYM_ALPHA  toridraw_gouraud_alpha_s4_presorted_run_rgb565_asm
#endif

#define R_X   0
#define R_Y   16
#define R_C   32
#define R_A   44
#define ROWBYTES 48

/*
 * The frame. As in the flat kernel the TOP sixteen bytes are the caller's
 * a0-a3 spill area and are not ours; locals run from zero to FRAMESIZE-16.
 */
#define FR_HSL       0    /* the row colour accumulator, ish8 */
#define FR_SL        4    /* the live left edge step          */
#define FR_SR        8    /* the live right edge step         */
#define FR_STEPY     12
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
#define FR_TAIL      96   /* the span's 1..3 pixel remainder  */
#define FR_STEPX     100  /* step_x_hsl_ish8, reloaded per triangle */
/* 112..127: the caller's spill area. Nothing of ours goes here. */
#define FRAMESIZE    128

#define RGB565_SPREAD_MASK 0x07E0F81F

	.text

/*
 * Register map once the prologue has parked the triangle:
 *
 *   a1  stack pointer          a2  row pointer into the framebuffer (bytes)
 *   a3  stride in BYTES        a5  left edge   (16.16)
 *   a6  right edge (16.16)     a9  rows left in this trapezoid
 *   a11 step_x_hsl_ish8        a13 g_hsl16_to_pixel_table
 *   a7  RGB565_SPREAD_MASK  |  a8  32 - (alpha >> 3)   (the alpha door only)
 *   a0 a4 a10 a12 a14 a15  scratch
 */

/*
 * Colour accumulator in \acc -> palette entry in \dst, clamp and all.
 *
 * An edge can land a subpixel past its vertex after the fixed-point nudge, so
 * the interpolant is not guaranteed inside the palette even though every
 * vertex is. Out of range clamps to 0 or 0xFFFF branchlessly, off a
 * rarely-taken branch. \t is scratch.
 *
 * The palette is indexed at a 2-BYTE scale: this file's format claim, exactly
 * as in the flat kernel.
 */
	.macro	LOOKUP acc, dst, t
	srai	\dst, \acc, 8
	/* One instruction for the range test, not two: BLTUI is UNSIGNED, so a
	 * negative index reads as huge and takes the clamp arm -- which is
	 * exactly `(unsigned)hsl16 > 0xFFFF`, the C's own test. 65536 is in
	 * B4CONSTU, so it is an immediate rather than a register. This is the
	 * hottest instruction in the kernel: it runs once per four pixels. */
	bltui	\dst, 65536, 9001f
	srai	\dst, \dst, 31			/* -1 when negative, else 0     */
	movi	\t, -1
	xor	\dst, \dst, \t			/* 0 when negative, else -1     */
	extui	\dst, \dst, 0, 16		/* 0 or 0xFFFF                  */
9001:
	addx2	\dst, \dst, a13
	l16ui	\dst, \dst, 0
	.endm

/* flat_screen_fixed_edges_no_hclip; see tri.flat.rgb565.xtensa.S. */
	.macro	NOHCLIP ea, sa, eb, sb, n, dst, t0, t1
	movi	\dst, 1
	blti	\n, 1, 9011f
	addi	\t0, \n, -1
	mull	\t1, \sa, \t0
	add	\t1, \t1, \ea
	min	\t1, \t1, \ea
	bltz	\t1, 9010f
	mull	\t1, \sb, \t0
	add	\t1, \t1, \eb
	max	\t1, \t1, \eb
	l32i	\t0, a1, FR_SCRW
	slli	\t0, \t0, 16
	blt	\t1, \t0, 9011f
9010:
	movi	\dst, 0
9011:
	.endm

/* A 16-bit byte swap in place, \t scratch. Only reached on the BE arm. */
	.macro	BSWAP16 r, t
	srli	\t, \r, 8
	slli	\r, \r, 8
	or	\r, \r, \t
	extui	\r, \r, 0, 16
	.endm

/*
 * The opaque span. a4 destination, a10 pixel count (>= 1), a12 the colour
 * accumulator at x_start. One palette lookup per FOUR pixels, phased from
 * x_start, exactly as toridraw_gouraud_span_fill_short blocks it -- the
 * quantization is visible output and is reproduced, not re-blocked.
 * Clobbers a0, a4, a10, a12, a14, a15.
 */
	.macro	SPANFILL
	blti	a10, 4, 9105f
	srli	a0, a10, 2			/* whole four-pixel blocks      */
	extui	a10, a10, 0, 2			/* 0..3 left over               */
	/*
	 * step_x * 4 is hoisted, and the block loop is a zero-overhead LOOP.
	 * Both were inside the loop first, and on a span that crosses the panel
	 * that cost this kernel more than the assembly won: three instructions
	 * of the fourteen in a four-pixel block, which measured as 0.89x the C
	 * -- the asm LOSING. a7 is free here because it carries the spread mask
	 * only on the alpha door.
	 */
	slli	a7, a11, 2			/* step_x * 4                   */
	loopnez	a0, 9104f
	LOOKUP	a12, a14, a15
	s16i	a14, a4, 0
	s16i	a14, a4, 2
	s16i	a14, a4, 4
	s16i	a14, a4, 6
	addi	a4, a4, 8
	add	a12, a12, a7
9104:
	beqz	a10, 9109f
9105:
	/* 1..3 pixels: one lookup, three stores, two conditional indices --
	 * the same shape the other two lanes use, and safe to repeat because a
	 * store is idempotent where a blend is not. */
	LOOKUP	a12, a14, a15
	s16i	a14, a4, 0
	movi	a15, 0
	blti	a10, 2, 9106f
	movi	a15, 2
9106:
	add	a15, a15, a4
	s16i	a14, a15, 0
	movi	a15, 0
	blti	a10, 3, 9107f
	movi	a15, 4
9107:
	add	a15, a15, a4
	s16i	a14, a15, 0
9109:
	.endm

/*
 * One blended pixel at [\p, \off] under the source term \k, \x and \y
 * scratch. The RGB565 blend with the source half hoisted; see
 * toripixel_rgb565_alpha_blend and the flat kernel's FILLALPHA.
 */
	.macro	BLEND1 p, off, k, x, y
	l16ui	\x, \p, \off
#if defined(TORIDRAW_XTENSA_RGB565_BE)
	BSWAP16	\x, \y
#endif
	slli	\y, \x, 16
	or	\x, \x, \y
	and	\x, \x, a7
	mull	\x, \x, a8
	add	\x, \x, \k
	srli	\x, \x, 5
	and	\x, \x, a7
	extui	\y, \x, 16, 16
	or	\x, \x, \y
#if defined(TORIDRAW_XTENSA_RGB565_BE)
	BSWAP16	\x, \y
#endif
	s16i	\x, \p, \off
	.endm

/*
 * The source colour's whole contribution for the palette entry in \src, left
 * in \dst: K = spread(src) * (alpha >> 3). \t is scratch, and \src is
 * consumed.
 *
 * Unlike the flat kernel this cannot be hoisted out of the span -- the colour
 * changes every four pixels -- which is what makes the alpha door the one
 * that needed a sixth scratch register.
 */
	.macro	BLENDK src, dst, t
#if defined(TORIDRAW_XTENSA_RGB565_BE)
	BSWAP16	\src, \t
#endif
	slli	\t, \src, 16
	or	\src, \src, \t
	and	\src, \src, a7			/* spread(src)                  */
	movi	\t, 32
	sub	\t, \t, a8			/* alpha >> 3                   */
	mull	\dst, \src, \t
	.endm

/*
 * The alpha span. a4 destination, a10 pixel count (>= 1), a12 the colour
 * accumulator at x_start. Same four-pixel blocking as the opaque span,
 * because the palette lookup is per block in both and the phase is visible
 * output. The 1..3 tail is a real loop: a blend is not idempotent, so the
 * opaque span's duplicate stores are not available.
 *
 * THE REMAINDER LIVES IN THE FRAME. Every one of a0, a4, a10, a12, a14 and
 * a15 is spoken for inside the block -- pointer, block count, colour, K and
 * the blend's two temporaries -- so the 1..3 count has nowhere to sit. It is
 * written once per span and read once, which is the cheapest thing in the
 * span to send to memory.
 * Clobbers a0, a4, a10, a12, a14, a15.
 */
	.macro	SPANFILL_ALPHA
	extui	a15, a10, 0, 2
	s32i	a15, a1, FR_TAIL
	srli	a0, a10, 2
	beqz	a0, 9205f
9201:
	LOOKUP	a12, a14, a15
	BLENDK	a14, a14, a15
	BLEND1	a4, 0, a14, a15, a10
	BLEND1	a4, 2, a14, a15, a10
	BLEND1	a4, 4, a14, a15, a10
	BLEND1	a4, 6, a14, a15, a10
	addi	a4, a4, 8
	slli	a15, a11, 2			/* step_x * 4                   */
	add	a12, a12, a15
	addi	a0, a0, -1
	bnez	a0, 9201b
9205:
	l32i	a0, a1, FR_TAIL
	beqz	a0, 9209f
	LOOKUP	a12, a14, a15
	BLENDK	a14, a14, a15
9206:
	BLEND1	a4, 0, a14, a15, a10
	addi	a4, a4, 2
	addi	a0, a0, -1
	bnez	a0, 9206b
9209:
	.endm

/*
 * One trapezoid. a5/a6 are the live left and right edges -- stepped in place,
 * so the second trapezoid inherits whatever the first left in the edge they
 * share -- a9 the row count, a4 the no-clip proof (consumed). The steps come
 * out of FR_SL/FR_SR, and the colour accumulator is stepped at the row join,
 * drawn or not, exactly as the C does.
 */
	.macro	SEGMENT p, tag, fill
	blti	a9, 1, \p\()\tag\()_done
	beqz	a4, \p\()\tag\()_cl_loop

	/* ---- proved inside the viewport: no left/right clamp ---- */
\p\()\tag\()_nc_loop:
	l32i	a14, a1, FR_SL
	l32i	a15, a1, FR_SR
	mov	a10, a5
	mov	a12, a6
	add	a5, a5, a14
	add	a6, a6, a15
	beq	a10, a12, \p\()\tag\()_nc_skip
	srai	a10, a10, 16
	srai	a12, a12, 16
	bge	a10, a12, \p\()\tag\()_nc_skip
	sub	a14, a12, a10			/* pixel count                  */
	addx2	a4, a10, a2			/* row + x * sizeof(pixel)      */
	l32i	a15, a1, FR_HSL
	mull	a12, a10, a11			/* x_start * step_x             */
	add	a12, a12, a15			/* ... + hsl                    */
	mov	a10, a14
	\fill
\p\()\tag\()_nc_skip:
	l32i	a14, a1, FR_HSL
	l32i	a15, a1, FR_STEPY
	add	a14, a14, a15
	s32i	a14, a1, FR_HSL
	add	a2, a2, a3
	addi	a9, a9, -1
	bnez	a9, \p\()\tag\()_nc_loop
	j	\p\()\tag\()_done

	/* ---- may leave the viewport: clamp both ends ----
	 *
	 * x_end clamps to screen_width - 1, not to screen_width, because that
	 * is what the C reference does; the last column of a clipped span is
	 * not drawn and reproducing that is not optional.
	 */
\p\()\tag\()_cl_loop:
	l32i	a14, a1, FR_SL
	l32i	a15, a1, FR_SR
	mov	a10, a5
	mov	a12, a6
	add	a5, a5, a14
	add	a6, a6, a15
	beq	a10, a12, \p\()\tag\()_cl_skip
	l32i	a4, a1, FR_SCRW
	srai	a12, a12, 16
	addi	a4, a4, -1
	min	a12, a12, a4
	srai	a10, a10, 16
	movi	a4, 0
	max	a10, a10, a4
	bge	a10, a12, \p\()\tag\()_cl_skip
	sub	a14, a12, a10
	addx2	a4, a10, a2
	l32i	a15, a1, FR_HSL
	mull	a12, a10, a11
	add	a12, a12, a15
	mov	a10, a14
	\fill
\p\()\tag\()_cl_skip:
	l32i	a14, a1, FR_HSL
	l32i	a15, a1, FR_STEPY
	add	a14, a14, a15
	s32i	a14, a1, FR_HSL
	add	a2, a2, a3
	addi	a9, a9, -1
	bnez	a9, \p\()\tag\()_cl_loop
\p\()\tag\()_done:
	.endm

/* The opaque door carries no per-triangle colour constants. */
	.macro	SETUP_OPAQUE
	.endm

/*
 * The alpha door's two, live across both trapezoid walks: the spread mask and
 * 32 - (alpha >> 3). RGB565 quantises the opacity to 32 steps, which is what
 * makes the blend one multiply for the whole pixel instead of one per channel.
 */
	.macro	SETUP_ALPHA
	l32i	a14, a1, FR_ROWS
	l32i	a15, a14, R_A
	movi	a7, RGB565_SPREAD_MASK
	srli	a15, a15, 3
	movi	a8, 32
	sub	a8, a8, a15
	.endm

/*
 * The run body. \p prefixes the labels, \setup the per-triangle colour work,
 * \fill the span. Vertices and colours arrive already in y order.
 */
	.macro	GOURAUDBODY p, setup, fill
\p\()face:
	l32i	a4, a1, FR_ROWS
	l32i	a5, a4, R_X + 0			/* x0                           */
	l32i	a6, a4, R_X + 4			/* x1                           */
	l32i	a7, a4, R_X + 8			/* x2                           */
	l32i	a8, a4, R_Y + 0			/* y0                           */
	l32i	a9, a4, R_Y + 4			/* y1                           */
	l32i	a10, a4, R_Y + 8		/* y2                           */

	/* Off the bottom or off the top: two well-predicted compares. */
	l32i	a14, a1, FR_SCRH
	blt	a14, a8, \p\()next
	bltz	a10, \p\()next

	sub	a11, a10, a8			/* dy_AC                        */
	beqz	a11, \p\()next
	sub	a12, a7, a5			/* dx_AC                        */
	sub	a15, a6, a5			/* dx_AB                        */
	sub	a13, a9, a8			/* dy_AB                        */

	/* sarea = dx_AB*dy_AC - dx_AC*dy_AB, the degenerate test and the
	 * colour gradients' common divisor. */
	mull	a3, a15, a11
	mull	a14, a12, a13
	sub	a3, a3, a14
	beqz	a3, \p\()next

	/*
	 * The two colour gradients. Exact integer divides -- see the header,
	 * and gouraudhsllightness_barycentric_steps.h's IDIV arm, which is what
	 * the C reference is built with when this kernel is scored.
	 *
	 * The `slli ..., 8` is toridraw_wrap_shl: it is MEANT to wrap. The
	 * d_hsl differences reach +-65535 and the dy they multiply reaches a
	 * few hundred, so the shifted product does not fit an int32 and the C
	 * says so.
	 */
	l32i	a0, a4, R_C + 0			/* c0                           */
	l32i	a14, a4, R_C + 4		/* c1                           */
	l32i	a4, a4, R_C + 8			/* c2                           */
	sub	a14, a14, a0			/* d_hsl_AB                     */
	sub	a4, a4, a0			/* d_hsl_AC                     */

	mull	a13, a14, a11			/* d_hsl_AB * dy_AC             */
	sub	a10, a9, a8			/* dy_AB (a13 was consumed)     */
	mull	a10, a4, a10			/* d_hsl_AC * dy_AB             */
	sub	a13, a13, a10
	slli	a13, a13, 8
	quos	a13, a13, a3			/* step_x_hsl_ish8              */

	mull	a10, a4, a15			/* d_hsl_AC * dx_AB             */
	mull	a4, a14, a12			/* d_hsl_AB * dx_AC             */
	sub	a10, a10, a4
	slli	a10, a10, 8
	quos	a10, a10, a3			/* step_y_hsl_ish8              */
	s32i	a10, a1, FR_STEPY

	/* hsl = (c0 << 8) + step_x - x0 * step_x, all modular */
	slli	a0, a0, 8
	add	a0, a0, a13
	mull	a14, a5, a13
	sub	a0, a0, a14
	s32i	a0, a1, FR_HSL
	s32i	a13, a1, FR_STEPX

	/* ---- the three edge slopes ---- */
	l32i	a4, a1, FR_ROWS
	l32i	a8, a4, R_Y + 0			/* y0 (a8 was dx_AB's dy)       */
	l32i	a9, a4, R_Y + 4			/* y1                           */
	l32i	a10, a4, R_Y + 8		/* y2                           */
	sub	a12, a7, a5			/* dx_AC                        */
	sub	a15, a6, a5			/* dx_AB                        */
	sub	a3, a10, a8			/* dy_AC                        */

	slli	a14, a12, 16
	quos	a14, a14, a3			/* sxAC                         */
	s32i	a14, a1, FR_SXAC

	sub	a4, a9, a8			/* dy_AB                        */
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
	/* Only pre-step A->B if that edge is walked; see the flat kernel. */
	blti	a9, 1, \p\()no_ab_prestep
	mull	a4, a3, a8
	sub	a12, a12, a4
\p\()no_ab_prestep:
	/* hsl -= step_y * y0, every time, walked or not. */
	l32i	a4, a1, FR_STEPY
	mull	a4, a4, a8
	l32i	a0, a1, FR_HSL
	sub	a0, a0, a4
	s32i	a0, a1, FR_HSL
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

	/* Which edge is on the left; the same test, in the same order, as the
	 * other two lanes. */
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
	mull	a7, a5, a14			/* AC advanced across seg 1     */
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
	l32i	a13, a1, FR_PAL
	l32i	a3, a1, FR_STRIDE_B
	l32i	a11, a1, FR_STEPX
	l32i	a5, a1, FR_L1
	l32i	a6, a1, FR_R1
	l32i	a14, a1, FR_SL1
	s32i	a14, a1, FR_SL
	l32i	a14, a1, FR_SR1
	s32i	a14, a1, FR_SR
	l32i	a9, a1, FR_N1
	l32i	a4, a1, FR_NC1
	SEGMENT \p, s1, \fill

	/*
	 * The second trapezoid. Whichever edge register the shared AC was
	 * walking still holds it, advanced; only the OTHER side changes, to BC.
	 */
	l32i	a14, a1, FR_BRANCH
	l32i	a15, a1, FR_SXAC
	bnez	a14, \p\()seg2_b
	l32i	a5, a1, FR_EBC			/* left  = eBC                  */
	l32i	a14, a1, FR_SXBC
	s32i	a14, a1, FR_SL
	s32i	a15, a1, FR_SR			/* right stays AC               */
	j	\p\()seg2_go
\p\()seg2_b:
	l32i	a6, a1, FR_EBC			/* right = eBC                  */
	l32i	a14, a1, FR_SXBC
	s32i	a14, a1, FR_SR
	s32i	a15, a1, FR_SL			/* left stays AC                */
\p\()seg2_go:
	l32i	a9, a1, FR_N2
	l32i	a4, a1, FR_NC2
	SEGMENT \p, s2, \fill

\p\()next:
	l32i	a14, a1, FR_ROWS
	addi	a14, a14, ROWBYTES
	s32i	a14, a1, FR_ROWS
	l32i	a15, a1, FR_LEFT
	addi	a15, a15, -1
	s32i	a15, a1, FR_LEFT
	bnez	a15, \p\()face
	.endm

	.macro	ENTER
#if defined(__XTENSA_CALL0_ABI__)
	addi	a1, a1, -FRAMESIZE
#else
	entry	a1, FRAMESIZE
#endif
	/* a0 becomes the sixth scratch register; see the header. */
	s32i	a0, a1, FR_RETADDR
	s32i	a2, a1, FR_PIXBUF
	slli	a3, a3, 1			/* stride: pixels -> bytes      */
	s32i	a3, a1, FR_STRIDE_B
	s32i	a4, a1, FR_SCRW
	s32i	a5, a1, FR_SCRH
	s32i	a6, a1, FR_ROWS
	s32i	a7, a1, FR_LEFT
	movi	a14, g_hsl16_to_pixel_table
	s32i	a14, a1, FR_PAL
	.endm

	.macro	LEAVE
	l32i	a0, a1, FR_RETADDR
#if defined(__XTENSA_CALL0_ABI__)
	addi	a1, a1, FRAMESIZE
	ret
#else
	retw
#endif
	.endm

/*
 * void <SYM_OPAQUE>(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 */
	.literal_position
	.align	4
	.globl	SYM_OPAQUE
	.type	SYM_OPAQUE, @function
SYM_OPAQUE:
	ENTER
	l32i	a14, a1, FR_LEFT
	blti	a14, 1, Lgo_ret
	GOURAUDBODY Lgo_, SETUP_OPAQUE, SPANFILL
Lgo_ret:
	LEAVE
	.size	SYM_OPAQUE, . - SYM_OPAQUE

/*
 * void <SYM_ALPHA>(
 *     toripixel_t* pixel_buffer, int stride, int screen_width,
 *     int screen_height, const int* rows, int count);
 *
 * As above, blending instead of storing. The opacity is per ROW, so a run may
 * mix them.
 */
	.literal_position
	.align	4
	.globl	SYM_ALPHA
	.type	SYM_ALPHA, @function
SYM_ALPHA:
	ENTER
	l32i	a14, a1, FR_LEFT
	blti	a14, 1, Lga_ret
	GOURAUDBODY Lga_, SETUP_ALPHA, SPANFILL_ALPHA
Lga_ret:
	LEAVE
	.size	SYM_ALPHA, . - SYM_ALPHA

#endif /* __XTENSA__ */
