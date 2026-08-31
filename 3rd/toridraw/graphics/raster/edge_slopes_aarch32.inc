/*
 * The three edge slopes of a triangle, packed, off a NEON reciprocal -- the
 * A32 twin of edge_slopes_aarch64.inc, shared by tri.flat.aarch32.S,
 * tri.gouraudhsllightness.aarch32.S and tri.tex.aarch32.S.
 *
 * WHAT IT REPLACES
 *
 *     step_AC = dy_AC > 0  ? (dx_AC << 16) / dy_AC : 0
 *     step_AB = dy_AB > 0  ? (dx_AB << 16) / dy_AB : 0
 *     step_BC = dy_BC != 0 ? (dx_BC << 16) / dy_BC : 0
 *
 * Three divides by three DIFFERENT values, so scalar there is nothing to
 * share; packed there is. Numerators and divisors into one vector each, the
 * live-lane mask built from the INTEGER divisors, one reciprocal estimate,
 * Newton-Raphson, the 16.16 scale applied AFTER the divide (so the inputs
 * convert exactly and the only rounding is the reciprocal's own), truncate,
 * mask.
 *
 * WHY THIS IS A SEPARATE FILE FROM THE AArch64 ONE. A32 and A64 Advanced SIMD
 * are different instruction sets, not two spellings of one. The ladder reads
 * the same instruction for instruction -- vrecpe.f32 / vrecps.f32 against
 * frecpe / frecps, in the same order on the same operands -- and everything
 * around it is what forces a separate file: A64's lane moves, its 31
 * general-purpose registers and its `mov wD, vN.s[i]` all have narrower A32
 * spellings, and the kernels that include this file are differently shaped as
 * a result. See the register note below.
 *
 * IT IS NOT BIT-IDENTICAL TO THE AArch64 LANE, AND CANNOT BE. Measured on a
 * Krait core (Motorola XT1060) against the same ladder on Apple arm64, lane
 * for lane on identical inputs:
 *
 *     vrecpe          identical      the estimate table is architectural
 *     first vrecps    identical
 *     second vrecps   DIVERGES       by one ulp, in both directions
 *
 * The first refinement corrects an 8-bit estimate, so (2 - d*r) sits about
 * 2^-8 away from 1.0 and has precision to spare. By the second the correction
 * term is down around 2^-21, and there A64's frecps -- which computes
 * 2 - d*r fused, with one rounding -- and A32's vrecps on this core, which
 * rounds the product before subtracting, disagree in the last place. Nothing
 * in this file can close that: it is the instruction, not the sequence.
 *
 * WHICH DOES NOT MATTER, and the measurement is the reason it does not.
 * Bit-identity with AArch64 was never the standard -- the AArch64 lane is not
 * bit-identical to the exact divide either, and is scored on a pixel budget
 * for exactly that reason. What matters is whether this lane is as ACCURATE,
 * and it is. Over 600,000 lanes against the exact integer divide:
 *
 *     AArch64   193666 lanes differ   worst 32 ulp   mean 15.04
 *     A32       193700 lanes differ   worst 32 ulp   mean 15.04
 *
 * Identical worst case, identical mean, 34 lanes apart in which ones. So the
 * two encodings are the same approximation and this lane is held to the same
 * pixel budget, not to the AArch64 lane's exact output. A worst case of 32
 * units of 16.16 is half a thousandth of a pixel per row.
 *
 * WHICH ARM. The reciprocal estimate is the default, as it is in the AArch64
 * and i686 files: an 8-bit estimate takes two Newton steps to reach float's
 * own precision (8 -> 16 -> 32 bits). vrecps computes (2 - d*r) in one
 * instruction, so a step is two instructions and no 2.0f constant is needed.
 *
 * AND IT IS THE FASTER ARM HERE, MEASURED, NOT INHERITED. armv7 is the one
 * lane where that was worth checking rather than assuming: unlike AArch64,
 * where the exact arm is a baseline instruction, `sdiv` on A32 is an
 * extension, so it was not obvious which way this core would fall. On the
 * Krait part (Motorola XT1060), 20,000,000 slope sets, three runs each:
 *
 *     vrecpe + two Newton steps    44.95 / 44.49 / 44.63 ns per triangle
 *     three sdiv                   54.58 / 54.64 / 52.55 ns per triangle
 *
 * The packed reciprocal does three lanes at once where sdiv does one at a
 * time, and that outweighs the four extra instructions -- about 1.2x, in the
 * same direction the i686 and AArch64 lanes chose for the same reason. So the
 * default below is the fast arm AND the arm every other ISA in this tree
 * takes; `sdiv` stays purely as the exactness proof.
 *
 * THE BIAS. Identical to the AArch64 lane's, for the identical reason: an
 * edge whose slope is an exact integer -- a third of all edges have dy == 1 --
 * where a reciprocal one ulp LOW makes num * r truncate to k*65536 - 1, and
 * an edge that should land on a pixel boundary every row lands one column
 * short on every row. So the 16.16 scale carries a one-ulp upward bias
 * (65536.0078125f): an exact quotient now truncates to its exact value, and a
 * non-integer quotient moves by one 16.16 unit only when it was already
 * within a ulp of the next.
 *
 * It is NOT bit-identical to the integer divide, and toridraw_presorted_neon
 * _test.c scores it on a triangle and pixel budget an order of magnitude above
 * what was measured. Assemble with -DTORIDRAW_EDGE_IDIV for the exact `sdiv`
 * lane, under which both numbers must be exactly zero -- that is what
 * separates a regression in the walk from the approximation in the slope.
 *
 * THE EXACT LANE NEEDS AN ARMv7 EXTENSION. `sdiv` is not in the ARMv7-A base
 * instruction set the way it is in AArch64; it arrives with the integer-divide
 * extension, which every Cortex-A7/A15-class and Krait part has and older
 * armv7 parts do not. That is acceptable precisely because this arm is the
 * TEST lane and never ships: the shipping default is the reciprocal above,
 * which needs nothing beyond baseline NEON. `.arch_extension idiv` is emitted
 * only inside the -DTORIDRAW_EDGE_IDIV arm so that a build without it does not
 * carry the directive at all. sdiv x/0 is 0 on this architecture, exactly as
 * on AArch64, which is the C's "else 0" arm -- so the exact lane has no
 * conditionals either.
 *
 * REGISTERS -- AND THE ONE PLACE THIS FILE IS CHEAPER THAN THE A64 ONE.
 * Reads dx_AC in \dxac, dx_AB in \dxab, dx_BC in \dxbc, dy_AC in \dyac,
 * dy_AB in \dyab, dy_BC in \dybc (all ARM registers). Writes the three steps
 * to \sac, \sab, \sbc.
 *
 * It takes NO scratch ARM register. The A64 macro needs one to plant the
 * constant 1 in the divisor's unused lane 3 (a zero divisor there would make
 * an infinity the mask would have to clean); here the whole divisor vector is
 * initialised with `vmov.i32 q9, #1` before the three real lanes are written
 * over it, which costs the same one instruction and no register. On a lane
 * with fourteen usable ARM registers instead of thirty-one that is not a
 * micro-optimisation -- it is the difference between the caller keeping its
 * edge accumulators live across this macro and spilling them.
 *
 * Clobbers q8, q9, q10, q11 and q14 (d16-d23, d28-d29). Reads q12 (the biased
 * 65536.0f x4) and q13 ({0, 0, -1, 0}), which EDGECONST prepares once per run.
 * q4-q7 (d8-d15) are the callee-saved vector registers under AAPCS and are
 * deliberately untouched, here and in all three kernels.
 */

.macro EDGECONST
#ifndef TORIDRAW_EDGE_IDIV
#ifdef TORIDRAW_EDGE_DIVPS
	/* A32 NEON has no vector divide at any width, so the "one correctly
	 * rounded packed divide" arm the i686 and AArch64 files offer cannot
	 * exist here. Selecting it is a build error rather than a silent
	 * fallback to the estimate: the whole point of that arm is to be the
	 * exactly-rounded one, and a lane that quietly gave you the estimate
	 * instead would make the comparison it exists for meaningless. */
	.error "TORIDRAW_EDGE_DIVPS: A32 NEON has no vector divide; use the default reciprocal arm or -DTORIDRAW_EDGE_IDIV"
#else
#ifndef TORIDRAW_EDGE_BIAS_ULP
#define TORIDRAW_EDGE_BIAS_ULP 1
#endif
	movw	r9, #TORIDRAW_EDGE_BIAS_ULP	/* 65536.0f, biased up by a few ulp */
	movt	r9, #0x4780			/*   -- see the header               */
	vdup.32	q12, r9
#endif
	vmov.i32	q13, #0
	mvn	r9, #0				/* -1                                */
	vmov.32	d27[0], r9			/* the BC lane, which tests != 0     */
#endif
.endm

.macro EDGESLOPES3 dxac, dxab, dxbc, dyac, dyab, dybc, sac, sab, sbc
#ifdef TORIDRAW_EDGE_IDIV
	.arch_extension idiv
	lsl	\sac, \dxac, #16
	sdiv	\sac, \sac, \dyac
	lsl	\sab, \dxab, #16
	sdiv	\sab, \sab, \dyab
	lsl	\sbc, \dxbc, #16
	sdiv	\sbc, \sbc, \dybc
#else
	/* numerators (dx_AC, dx_AB, dx_BC, 0) -- NOT shifted left by 16 first;
	 * see edge_slopes_i686.inc for why the scale is applied after. */
	vmov.i32	q8, #0
	vmov.32	d16[0], \dxac
	vmov.32	d16[1], \dxab
	vmov.32	d17[0], \dxbc
	/* divisors (dy_AC, dy_AB, dy_BC, 1): lane 3 is never used, but a zero
	 * divisor there would make an infinity the mask would have to clean.
	 * Planted by initialising the vector to 1 rather than by moving a 1
	 * through an ARM register -- see the register note in the header. */
	vmov.i32	q9, #1
	vmov.32	d18[0], \dyac
	vmov.32	d18[1], \dyab
	vmov.32	d19[0], \dybc
	/* the live-lane mask, from the INTEGER divisors: dy > 0 everywhere,
	 * plus dy < 0 for the BC lane alone */
	vcgt.s32	q10, q9, #0
	vclt.s32	q11, q9, #0
	vand	q11, q11, q13
	vorr	q10, q10, q11
	vcvt.f32.s32	q8, q8
	vcvt.f32.s32	q9, q9
	/* one reciprocal estimate for all three, refined twice, scaled */
	vrecpe.f32	q11, q9
	vrecps.f32	q14, q9, q11
	vmul.f32	q11, q11, q14
	vrecps.f32	q14, q9, q11
	vmul.f32	q11, q11, q14
	vmul.f32	q11, q11, q12		/* fold in the (biased) 16.16 scale  */
	vmul.f32	q8, q8, q11
	vcvt.s32.f32	q8, q8			/* truncates toward zero, as C does  */
	vand	q8, q8, q10			/* dead lanes -> 0, the branches gone */
	vmov.32	\sac, d16[0]
	vmov.32	\sab, d16[1]
	vmov.32	\sbc, d17[0]
#endif
.endm
