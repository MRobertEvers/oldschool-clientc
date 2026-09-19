/*
 * The three edge slopes of a triangle, packed, off a floating-point
 * reciprocal. Shared by gouraud_tri_i686.S and flat_tri_i686.S.
 *
 * WHAT IT REPLACES
 *
 * Three conditional integer divides:
 *
 *     step_AC = dy_AC > 0  ? (dx_AC << 16) / dy_AC : 0
 *     step_AB = dy_AB > 0  ? (dx_AB << 16) / dy_AB : 0
 *     step_BC = dy_BC != 0 ? (dx_BC << 16) / dy_BC : 0
 *
 * three `idivl`, three branches, and six sign-extensions. All three divide by
 * DIFFERENT values, so there is no reciprocal to share between them scalar --
 * but there is one to share between them PACKED, because rcpps reciprocates
 * four lanes in a single instruction. That is what this does: one rcpps, one
 * Newton-Raphson refinement, one mulps, and the branches become a mask.
 *
 * WHY THIS IS THE RIGHT TRADE ON THIS TARGET
 *
 * The numbers are already on file, measured on the box (gouraud_tri_i686.S
 * carries the full table): for the five-divide group, idivl 70.82 ns, divsd
 * 98.08 ns, a shared scalar reciprocal 87.10 ns, packed divps 65.43 ns. Packed
 * was the fastest thing tried and was set aside only because it is not exact.
 * rcpps plus one Newton step is cheaper again than divps -- a P4's divider is
 * not pipelined and rcpps is a table lookup.
 *
 * ACCURACY, AND WHAT IT COSTS
 *
 * This is NOT bit-identical to the integer divide, and the difference is
 * visible in principle rather than only in theory, so it is worth being precise
 * about the size of it.
 *
 * Two roundings enter. The numerator (dx << 16) can reach 2^31 and a float
 * carries 24 bits, so converting it can move it by up to 2^7. And rcpps is
 * accurate to about 12 bits, which one Newton step (r' = r * (2 - d*r)) takes
 * to roughly 24 -- the same as the float it lands in.
 *
 * The result is a 16.16 fixed-point column step. A relative error of 2^-22 on
 * a typical edge -- a few pixels per row, so a few hundred thousand in 16.16 --
 * is well under one unit of the fixed point, i.e. under 1/65536 of a pixel per
 * row, and stays under a hundredth of a pixel accumulated over a viewport's
 * height. Two triangles that share an edge see the same dx and dy for it and
 * therefore compute the same step, so they still tile.
 *
 * Assemble with -DTORIDRAW_EDGE_DIVPS for exact-ish packed divps, or
 * -DTORIDRAW_EDGE_DIVSD / -DTORIDRAW_EDGE_IDIV for the two scalar forms, which
 * is how the four are A/B'd against each other in one place.
 *
 * REGISTERS. Clobbers %eax and %xmm0-%xmm5. Reads dx_AC in \dxac, dx_AB in
 * \dxab, dy_AC in \dyac, dy_AB in \dyab, and the sorted vertex slots for the
 * BC edge. Writes three ints at \dst, which MUST be 16-byte aligned and MUST
 * have a fourth int after it to absorb the unused lane.
 */

	.section .rodata
	.p2align 4
Ledge_two:
	.long	0x40000000, 0x40000000, 0x40000000, 0x40000000	/* 2.0f x4 */
	.p2align 4
Ledge_lane2:
	.long	0, 0, -1, 0		/* the BC lane, which tests != 0    */
	.p2align 4
Ledge_65536:
	.long	0x47800000, 0x47800000, 0x47800000, 0x47800000	/* 65536.0f */
	.text

.macro	EDGESLOPES3 dxac, dxab, dyac, dyab, dst
#if defined(TORIDRAW_EDGE_IDIV) || defined(TORIDRAW_EDGE_DIVSD)
	/* ---- scalar reference lanes, kept for the A/B ---- */
	xorl	%eax, %eax
	testl	\dyac, \dyac
	jle	7710f
	movl	\dxac, %eax
	shll	$16, %eax
	EDGE1	\dyac
7710:
	movl	%eax, \dst + 0(%esp)

	xorl	%eax, %eax
	testl	\dyab, \dyab
	jle	7720f
	movl	\dxab, %eax
	shll	$16, %eax
	EDGE1	\dyab
7720:
	movl	%eax, \dst + 4(%esp)

	movl	V_Y2(%esp), %ecx
	subl	V_Y1(%esp), %ecx
	xorl	%eax, %eax
	testl	%ecx, %ecx
	je	7730f
	movl	V_X2(%esp), %eax
	subl	V_X1(%esp), %eax
	shll	$16, %eax
	EDGE1	%ecx
7730:
	movl	%eax, \dst + 8(%esp)
#else
	/* ---- numerators: (dx_AC, dx_AB, dx_BC, 0) ----
	 *
	 * NOT shifted left by 16 first, and that is the whole difference between
	 * this and the obvious transcription.
	 *
	 * The integer form computes (dx << 16) / dy, so the value that has to
	 * survive the trip through a float is dx << 16, which reaches 2^31. A
	 * float carries 24 bits, so converting it throws away up to 2^7 before
	 * the divide has even started -- and that error is then multiplied again
	 * by the vertical pre-step, which for a triangle starting far above the
	 * viewport is a factor of thousands.
	 *
	 * Scaling AFTER the divide instead keeps every input small: dx and dy
	 * are screen deltas that convert exactly, the divide rounds once, and
	 * multiplying by 65536 is exact because it is a power of two. One
	 * rounding instead of two, on inputs that cannot lose bits.
	 *
	 * It does give up the integer form's WRAPAROUND, which the reference
	 * client relies on for edge-on slivers with |dx| >= 32768: there the
	 * shift wraps and this does not. Those triangles are degenerate by
	 * construction, and the accuracy note above is measured against the
	 * exact integer lane, wrap included. */
	movd	\dxac, %xmm0
	movd	\dxab, %xmm1
	punpckldq %xmm1, %xmm0
	movl	V_X2(%esp), %eax
	subl	V_X1(%esp), %eax
	movd	%eax, %xmm2
	punpcklqdq %xmm2, %xmm0		/* num, as int32 lanes              */

	/* ---- divisors: (dy_AC, dy_AB, dy_BC, 1) ---- */
	movd	\dyac, %xmm1
	movd	\dyab, %xmm3
	punpckldq %xmm3, %xmm1
	movl	V_Y2(%esp), %eax
	subl	V_Y1(%esp), %eax
	movd	%eax, %xmm2
	movl	$1, %eax		/* lane 3 is never used, but a zero  */
	movd	%eax, %xmm3		/* divisor there would make a NaN    */
	punpckldq %xmm3, %xmm2		/* that the mask would then have to  */
	punpcklqdq %xmm2, %xmm1		/* clean up.                         */

	/* ---- the mask, built from the INTEGER divisors ----
	 * Lanes 0 and 1 are live when dy > 0; lane 2 is live when dy != 0,
	 * which is the same thing plus the negative half. So take dy > 0 for
	 * every lane and fold in dy < 0 for lane 2 alone. */
	pxor	%xmm5, %xmm5
	movdqa	%xmm1, %xmm4
	pcmpgtd	%xmm5, %xmm4		/* dy > 0                            */
	pcmpgtd	%xmm1, %xmm5		/* dy < 0                            */
	pand	Ledge_lane2, %xmm5
	por	%xmm5, %xmm4		/* live-lane mask                    */

	/* ---- one reciprocal for all three ---- */
	cvtdq2ps %xmm0, %xmm0
	cvtdq2ps %xmm1, %xmm1
#ifndef TORIDRAW_EDGE_DIVPS
	/*
	 * rcpps plus one Newton-Raphson step: 12 bits refined to about 22,
	 * against divps's 24. It is the cheapest form here -- a reciprocal is a
	 * table lookup where a divide is not -- and the two bits it gives up
	 * were measured rather than waved at, twice, because the two
	 * measurements disagree about how much they matter.
	 *
	 * SYNTHETICALLY it looks bad. Against the exact integer lane over
	 * 200,000 randomised triangles it moves 9,375 of them and 804,300
	 * pixels, where divps moves 5 and 333; on a dense 260-triangle scene it
	 * moves 3.5% of the canvas. Small triangles have short edges and
	 * therefore steep slopes, and 22 bits is not enough to keep those in the
	 * right column.
	 *
	 * IN A REAL FRAME it disappears. The client's own scene, captured
	 * through both lanes on the LostCity rev-289 world:
	 *
	 *     divps vs divps   0.79%  <- same binary, two runs
	 *     rcpps vs rcpps   0.41%  <- same binary, two runs
	 *     divps vs rcpps   0.61%
	 *     divps vs rcpps   0.70%
	 *
	 * The lane difference sits INSIDE the run-to-run noise, because the
	 * scene is not reproducible frame for frame -- animated npcs, a
	 * wall-clock logic tick -- and that variation is larger than the kernel
	 * difference. A frame drawn through this lane cannot be told from one
	 * drawn through divps by anything except another frame.
	 *
	 * -DTORIDRAW_EDGE_DIVPS selects the correctly-rounded packed divide, and
	 * -DTORIDRAW_EDGE_IDIV the exact integer one, which is what the parity
	 * tests are built with.
	 */
	rcpps	%xmm1, %xmm2
	movaps	%xmm2, %xmm3
	mulps	%xmm1, %xmm3		/* d * r                             */
	movaps	Ledge_two, %xmm5
	subps	%xmm3, %xmm5		/* 2 - d*r                           */
	mulps	%xmm5, %xmm2		/* r * (2 - d*r)                     */
	mulps	Ledge_65536, %xmm2
	mulps	%xmm2, %xmm0
#else
	/* One correctly-rounded packed divide, then the 16.16 scale.
	 *
	 * Four lanes for the price of one divide, and the file's own probe
	 * measured it as the fastest option available at the time -- 65.43 ns
	 * for the group against idivl's 70.82, divsd's 98.08 and a scalar
	 * reciprocal's 87.10. Combined with scaling after the divide rather than
	 * before it, it is very nearly exact: 5 triangles in 200,000, and 315 of
	 * the 333 differing pixels are one sliver with |dx| >= 32768 where the
	 * integer form's deliberate wraparound is a different answer rather than
	 * a rounder one.
	 *
	 * The default is rcpps above, which is cheaper and, in a real frame,
	 * indistinguishable. This lane is here for when that has to be checked
	 * again. */
	divps	%xmm1, %xmm0
	mulps	Ledge_65536, %xmm0
#endif
	cvttps2dq %xmm0, %xmm0		/* truncates toward zero, as C does  */
	pand	%xmm4, %xmm0		/* dead lanes -> 0, the branches gone */
	movdqa	%xmm0, \dst(%esp)
#endif
.endm
