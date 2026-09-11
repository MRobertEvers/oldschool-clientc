/*
 * The three edge slopes of a triangle, packed, off a NEON reciprocal -- the
 * AArch64 twin of edge_slopes_i686.inc, shared by flat_tri_aarch64.S,
 * gouraud_tri_aarch64.S and tex_tri_aarch64.S.
 *
 * WHAT IT REPLACES
 *
 *     step_AC = dy_AC > 0  ? (dx_AC << 16) / dy_AC : 0
 *     step_AB = dy_AB > 0  ? (dx_AB << 16) / dy_AB : 0
 *     step_BC = dy_BC != 0 ? (dx_BC << 16) / dy_BC : 0
 *
 * Three divides by three DIFFERENT values, so scalar there is nothing to
 * share; packed there is, because frecpe reciprocates four lanes at once and
 * the three conditionals collapse into a mask. This is the i686 kernel's
 * EDGESLOPES3 transcribed onto Advanced SIMD: numerators and divisors into
 * one vector each, the live-lane mask built from the INTEGER divisors, one
 * reciprocal estimate, Newton-Raphson, the 16.16 scale applied AFTER the
 * divide (so the inputs convert exactly and the only rounding is the
 * reciprocal's own), truncate, mask.
 *
 * WHICH ARM. The reciprocal estimate is the default, as it is in the i686
 * file: frecpe is an 8-bit estimate where rcpps was 12, so it takes two
 * Newton steps where the i686 took one (8 -> 16 -> 32 bits, float's own
 * precision). frecps computes (2 - d*r) in one instruction, so a step is two
 * instructions and the 2.0f constant the i686 loads is not needed.
 *
 * THE BIAS. Measured plain, this arm moved 0.3% of triangles against the
 * exact divide, and every one of them was the same case: an edge whose
 * slope is an exact integer -- a third of all edges have dy == 1 -- where a
 * reciprocal one ulp LOW makes num * r truncate to k*65536 - 1, and an edge
 * that should land on a pixel boundary every row lands one column short on
 * every row. So the 16.16 scale carries a one-ulp upward bias
 * (65536.0078125f): an exact quotient now truncates to its exact value,
 * and a non-integer quotient moves by one 16.16 unit only when it was
 * already within a ulp of the next. Measured over 20,000 triangles per
 * door, bias 0 / 1 / 2 / 4 ulp: 3000 / 150 / 1700 / 10400 ppm of triangles
 * moved -- one ulp is the minimum that covers the estimate's own error and
 * anything more starts moving the non-integer class instead.
 *
 * -DTORIDRAW_EDGE_DIVPS selects the i686 file's other arm, one packed
 * correctly-rounded fdiv, which this core would also run at full rate.
 *
 * It is NOT bit-identical to the integer divide, and toridraw_presorted_neon
 * _test.c scores it the way toridraw_flat_tri_asm_test.c scores the i686
 * ladder: a triangle and pixel budget an order of magnitude above what was
 * measured. Assemble with -DTORIDRAW_EDGE_IDIV for the exact `sdiv` lane,
 * under which both numbers must be exactly zero -- that is what separates a
 * regression in the walk from the approximation in the slope. sdiv x/0 is 0
 * on this architecture, which is exactly the C's "else 0" arm, so the exact
 * lane has no conditionals either.
 *
 * REGISTERS. Reads dx_AC in \dxac, dx_AB in \dxab, dx_BC in \dxbc, dy_AC in
 * \dyac, dy_AB in \dyab, dy_BC in \dybc (all w registers). Writes the three
 * steps to \sac, \sab, \sbc. The packed lane clobbers \tmp (a w register)
 * and v19-v23, and reads v17 (65536.0f x4) and v18 ({0, 0, -1, 0}), which
 * EDGECONST prepares once per run; v19-v23 and v16-v18 are caller-saved and
 * nothing in the three kernels keeps state in them.
 */

.macro EDGECONST
#ifndef TORIDRAW_EDGE_IDIV
#ifdef TORIDRAW_EDGE_DIVPS
	movi	v17.4s, #0x47, lsl #24		/* 0x47000000 = 32768.0f            */
	fadd	v17.4s, v17.4s, v17.4s		/* 65536.0f x4                       */
#else
#ifndef TORIDRAW_EDGE_BIAS_ULP
#define TORIDRAW_EDGE_BIAS_ULP 1
#endif
	movz	w9, #0x4780, lsl #16		/* 65536.0f, biased up by a few ulp */
	movk	w9, #TORIDRAW_EDGE_BIAS_ULP	/*   -- see the header               */
	dup	v17.4s, w9
#endif
	movi	v18.2d, #0
	mov	w9, #-1
	mov	v18.s[2], w9			/* the BC lane, which tests != 0     */
#endif
.endm

.macro EDGESLOPES3 dxac, dxab, dxbc, dyac, dyab, dybc, sac, sab, sbc, tmp
#ifdef TORIDRAW_EDGE_IDIV
	lsl	\sac, \dxac, #16
	sdiv	\sac, \sac, \dyac
	lsl	\sab, \dxab, #16
	sdiv	\sab, \sab, \dyab
	lsl	\sbc, \dxbc, #16
	sdiv	\sbc, \sbc, \dybc
#else
	/* numerators (dx_AC, dx_AB, dx_BC, 0) -- NOT shifted left by 16 first;
	 * see edge_slopes_i686.inc for why the scale is applied after. */
	fmov	s19, \dxac
	mov	v19.s[1], \dxab
	mov	v19.s[2], \dxbc
	/* divisors (dy_AC, dy_AB, dy_BC, 1): lane 3 is never used, but a zero
	 * divisor there would make an infinity the mask would have to clean. */
	fmov	s20, \dyac
	mov	v20.s[1], \dyab
	mov	v20.s[2], \dybc
	mov	\tmp, #1
	mov	v20.s[3], \tmp
	/* the live-lane mask, from the INTEGER divisors: dy > 0 everywhere,
	 * plus dy < 0 for the BC lane alone */
	cmgt	v21.4s, v20.4s, #0
	cmlt	v22.4s, v20.4s, #0
	and	v22.16b, v22.16b, v18.16b
	orr	v21.16b, v21.16b, v22.16b
	scvtf	v19.4s, v19.4s
	scvtf	v20.4s, v20.4s
#ifdef TORIDRAW_EDGE_DIVPS
	/* one correctly-rounded packed divide, then the 16.16 scale */
	fdiv	v19.4s, v19.4s, v20.4s
	fmul	v19.4s, v19.4s, v17.4s
#else
	/* one reciprocal estimate for all three, refined twice, scaled */
	frecpe	v22.4s, v20.4s
	frecps	v23.4s, v20.4s, v22.4s
	fmul	v22.4s, v22.4s, v23.4s
	frecps	v23.4s, v20.4s, v22.4s
	fmul	v22.4s, v22.4s, v23.4s
	fmul	v22.4s, v22.4s, v17.4s		/* fold in the (biased) 16.16 scale  */
	fmul	v19.4s, v19.4s, v22.4s
#endif
	fcvtzs	v19.4s, v19.4s			/* truncates toward zero, as C does  */
	and	v19.16b, v19.16b, v21.16b	/* dead lanes -> 0, the branches gone */
	mov	\sac, v19.s[0]
	mov	\sab, v19.s[1]
	mov	\sbc, v19.s[2]
#endif
.endm
