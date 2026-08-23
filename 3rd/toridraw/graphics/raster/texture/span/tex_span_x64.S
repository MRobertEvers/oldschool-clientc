/*
 * toridraw_texspan_opaque_lerp8_v3_asm -- hand-written x86-64 (MS ABI, SSE2).
 *
 * Twin of draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered in
 * tex.span.sse2.u.c. That C function is the reference and
 * toridraw_texspan_asm_test.c scores this against it span by span.
 *
 * WHAT IS DIFFERENT FROM WHAT THE COMPILER PRODUCED
 *
 *   1. The texture width is resolved once, at the top, into two whole
 *      specialisations of the loop. texture_shift, u_mask, v_mask and the
 *      float clamp bound all become immediates, so `cw >> texture_shift`
 *      stops being a variable shift through cl -- which on this ABI fights
 *      the kernel for rcx -- and the mask constants stop occupying registers.
 *
 *   2. have_cur is gone. The C carries it because it does not want to divide
 *      twice for the same endpoint, but block k's far endpoint and block k+1's
 *      near endpoint are the same quotient of the same au/bv/cw, so "carried"
 *      and "recomputed" are provably the same value. Holding the two endpoints
 *      as a pair makes the flag unnecessary rather than merely cheap.
 *
 *   3. The cold paths -- the integer-divide fallback when the float reciprocal
 *      has lost the low bits, and the per-pixel exact block when the linear fit
 *      is refused -- are laid out out of line, so the fast path through a block
 *      is straight and dense.
 *
 * WHY BY HAND AT ALL: see tex_span_asm.h. The per-block reciprocal chain
 * measured LARGER than the SSE2 gather it feeds (0.353 ms vs 0.266 ms of a
 * 4.004 ms render), it is about 30 cycles of serial latency, and the value it
 * produces is known one block ahead. Register allocation was never the problem;
 * the compiled build spills 2.3% in the hot cluster.
 *
 * ABLATION: this kernel does not honour TORIDRAW_ABLATE_TEX. Those modes exist
 * to decompose the C kernel's cost into a probe binary; measure them against
 * the C twin.
 */

/* ------------------------------------------------------------------ */
/*  Stack frame. Saved regs 64 bytes, frame 112, so an incoming        */
/*  argument n sits at rsp + 176 + 8n.                                 */
/* ------------------------------------------------------------------ */
#define ST_AU8   0
#define ST_BV8   4
#define ST_CW8   8
#define ST_SH8   12
#define ST_AU    16
#define ST_BV    20
#define ST_CW    24
#define REMAIN   28
#define BLKN     32
#define SHADE    36
#define EB_AU    40
#define EB_BV    44
#define EB_CW    48
#define EB_N     52
#define EB_I     56
#define ST_AU16  96
#define ST_BV16  100
#define ST_CW16  104
#define ST_CW24  108
#define SV_XMM6  112
#define SV_XMM7  128
#define A(n) (208 + 8 * (n))(%rsp)

	.text

/* ------------------------------------------------------------------ */
/* w != 0 already established. xmm1 <- 1.0f / (float)\w                */
/* ------------------------------------------------------------------ */
	.macro RECIP w
	cvtsi2ssl \w, %xmm0
#ifdef TEXSPAN_RCP_DIAG
	rcpss   %xmm0, %xmm1
#else
	movss   LC_one(%rip), %xmm1
	divss   %xmm0, %xmm1
#endif
	.endm

/*
 * The same reciprocal, two blocks early and into xmm7.
 *
 * divss is about fourteen cycles and one eight-pixel fill is not that much
 * shadow, so the divide runs a stage ahead of the quotients that consume it:
 * block k divides for block k+3 and multiplies for block k+2. xmm7 is free the
 * whole way through -- the fill lives in xmm0..xmm6 -- so this costs no
 * general register, which is the only reason it is affordable.
 *
 * A zero w is folded to one rather than branched around. Such a block is
 * skipped by the sentinel two blocks later and its quotient never read; the
 * substitution exists only to keep a divide by zero out of MXCSR.
 */
	.macro RECIP7 w
	movl    $1, %r9d
	testl   \w, \w
	cmovel  %r9d, \w
	cvtsi2ssl \w, %xmm0
	movss   LC_one(%rip), %xmm7
	divss   %xmm0, %xmm7
	.endm

/*
 * DIVR: \q <- \n / \w, the exact C quotient (truncated toward zero), using
 * only 32-bit operations. Expects 1.0f / (float)\w in xmm1 and |\w| >= 2 --
 * the caller peels +/-1, which also disposes of INT_MIN / -1. \n is a memory
 * operand, read again by the fixup; \q, \r, \t are three distinct scratch
 * registers, none of them \w. Clobbers xmm2 and xmm3.
 *
 * The float product lands within a unit of the true quotient, the 32-bit
 * remainder it leaves is exact even where the multiply wraps (n - q*w is
 * congruent mod 2^32 and small enough to be meant literally), and one more
 * multiply by the same reciprocal refines it. Whatever is left is walked off
 * a step at a time: the remainder is home when it is zero, or has the
 * numerator's sign and is smaller in magnitude than the divisor -- which is
 * exactly C truncation. Brute force over 209 million adversarial pairs
 * (divrefine_b.c, kept with the perf notes) never needed more than one step
 * under round-to-nearest, and the loop terminates under any MXCSR rounding
 * mode because each step moves the remainder |\w| closer to home.
 */
	.macro DIVR n, w, q, r, t
	cvtsi2ssl \n, %xmm2
	mulss   %xmm1, %xmm2
	cvtss2si %xmm2, \q
	movl    \q, \t
	imull   \w, \t
	movl    \n, \r
	subl    \t, \r
	cvtsi2ssl \r, %xmm2
	mulss   %xmm1, %xmm2
	cvtss2si %xmm2, \t
	addl    \t, \q
	imull   \w, \t
	subl    \t, \r
7:
	testl   \r, \r
	je      9f
	movl    \r, \t
	xorl    \n, \t
	js      8f
	movd    \r, %xmm3
	movl    \r, \t
	negl    \t
	cmovsl  \r, \t
	movl    \w, \r
	negl    \r
	cmovsl  \w, \r
	cmpl    \r, \t
	movd    %xmm3, \r
	jb      9f
8:
	movl    \r, \t
	xorl    \w, \t
	sarl    $31, \t
	orl     $1, \t
	addl    \t, \q
	imull   \w, \t
	subl    \t, \r
	jmp     7b
9:
	.endm

/*
 * tex_span_u_quotient: clamp in float, THEN convert.
 *
 * The order is the whole point -- an out-of-range cvttss2si yields INT_MIN on
 * x86, which is why the C clamps in float too. maxss against +0.0 also lands
 * q == -0.0 on zero, because MAXSS returns its source operand when the two
 * compare equal.
 *
 * \n numerator reg, \twm1f float (texture_width-1), \out result. Clobbers
 * xmm2, xmm3.
 */
	.macro QUOTU n, twm1f, out
	cvtsi2ssl \n, %xmm2
	mulss   %xmm1, %xmm2
	xorps   %xmm3, %xmm3
	maxss   %xmm3, %xmm2
	minss   \twm1f(%rip), %xmm2
	cvttss2si %xmm2, \out
	.endm

/*
 * tex_span_wrapped_quotient: trust the reciprocal only while it still carries
 * the low bits that select the texel row, else divide.
 *
 * The slow side is the C's own `n / w`, instruction for instruction: one idivl,
 * so the result is the reference quotient by construction rather than by an
 * argument about rounding. This kernel is 32-bit throughout -- no operand, and
 * no intermediate, is ever wider than a dword.
 *
 * The one case idivl cannot take is INT_MIN / -1, where the true quotient is
 * 2^31 and the instruction raises #DE instead of producing it. w == -1 is
 * therefore peeled off in front: n / -1 is -n for every other n, and at
 * n == INT_MIN negl wraps back to INT_MIN, which is the value x86 hands back
 * for an out-of-range conversion anyway. Two instructions, on a path that only
 * runs once the reciprocal has already been refused.
 *
 * \n numerator reg or m32, \w divisor reg or m32 -- but not eax or edx, which
 * the divide sequence writes before it reads \w. \out result. Clobbers xmm2,
 * xmm3, eax, edx.
 */
	.macro WRAPQ n, w, out
	cvtsi2ssl \n, %xmm2
	mulss   %xmm1, %xmm2
	movaps  %xmm2, %xmm3
	andps   LC_absmask(%rip), %xmm3
	comiss  LC_lim(%rip), %xmm3
	jp      8f
	jae     8f
	cvttss2si %xmm2, \out
	jmp     9f
8:
	movl    \n, %eax
	cmpl    $-1, \w
	je      7f
	cltd
	idivl   \w
	movl    %eax, \out
	jmp     9f
7:
	negl    %eax
	movl    %eax, \out
9:
	.endm

/*
 * shade_blend4_sse in place on \v, 16-bit shade broadcast in xmm4, zero in
 * xmm5. Widen to 16-bit, multiply, shift, pack -- the same four steps in the
 * same order as the intrinsic twin, so the result is the same bits and not
 * merely the same colour.
 */
	.macro BLEND4 v
	movdqa  \v, %xmm2
	punpcklbw %xmm5, %xmm2
	movdqa  \v, %xmm3
	punpckhbw %xmm5, %xmm3
	pmullw  %xmm4, %xmm2
	pmullw  %xmm4, %xmm3
	psrlw   $8, %xmm2
	psrlw   $8, %xmm3
	packuswb %xmm3, %xmm2
	movdqa  %xmm2, \v
	.endm

/*
 * One texel into \dstx. u_scan eax, v_scan ecx, texels r12, scratch edx/r9d.
 *
 * The index is ((u_scan >> SH) & TWM1) + (v_scan & VMASK) -- the C expression
 * with v_mask spelled out. v_mask is exactly (texture_width-1) << texture_shift,
 * so the masked v_scan is already the row offset and needs no shift of its own.
 *
 * The u mask is dropped, and that is a claim rather than a shortcut, so here is
 * the argument. QUOTU clamps both endpoints into [0, texture_width-1] before
 * they ever reach here, u_scan runs from cur_u << SH to nxt_u << SH, and a
 * linear ramp between two in-range endpoints stays in range. So u_scan >> SH is
 * already inside [0, texture_width-1] and & TWM1 cannot change it. The C keeps
 * the mask because the same helper serves paths whose endpoints are not
 * clamped; this one only ever runs behind QUOTU.
 */
	.macro GATHER1 sh, twm1, vmask, dstx
	movl    %eax, %edx
	sarl    $\sh, %edx
	movl    %ecx, %r9d
	andl    $\vmask, %r9d
	addl    %r9d, %edx
	movd    (%r12,%rdx,4), \dstx
	.endm

/*
 * Four texels -> xmm0, advancing the scan by s_u (r10d) / s_v (r11d).
 *
 * The steps are in registers rather than the frame on purpose. They are
 * produced immediately above this point and consumed sixteen times, so a stack
 * slot puts a store-to-load forward in front of every pixel -- which measured
 * as three quarters of the kernel.
 */
	.macro GATHER4 sh, twm1, vmask
	GATHER1 \sh, \twm1, \vmask, %xmm0
	addl    %r10d, %eax
	addl    %r11d, %ecx
	GATHER1 \sh, \twm1, \vmask, %xmm6
	addl    %r10d, %eax
	addl    %r11d, %ecx
	punpckldq %xmm6, %xmm0
	GATHER1 \sh, \twm1, \vmask, %xmm1
	addl    %r10d, %eax
	addl    %r11d, %ecx
	GATHER1 \sh, \twm1, \vmask, %xmm6
	addl    %r10d, %eax
	addl    %r11d, %ecx
	punpckldq %xmm6, %xmm1
	punpcklqdq %xmm1, %xmm0
	.endm

/* The eight-pixel linear block, into r13. */
	.macro FILL8 sh, twm1, vmask
#ifndef TEXSPAN_NO_FILL
	GATHER4 \sh, \twm1, \vmask
	BLEND4  %xmm0
	movdqu  %xmm0, (%r13)
	GATHER4 \sh, \twm1, \vmask
	BLEND4  %xmm0
	movdqu  %xmm0, 16(%r13)
#endif
	.endm

/*
 * shade_blend, scalar. \c is the texel and receives the result, \t is scratch,
 * shade (0..255) in r8d.
 *
 * Two masked multiplies rather than three, because red and blue are eight bits
 * apart and cannot carry into one another: c*shade is at most 255*255, which
 * leaves the top byte of each 16-bit field clear.
 */
	.macro SHADEBLEND c, t, sh8
	movl    \c, \t
	andl    $0x00FF00FF, \c
	andl    $0x0000FF00, \t
	imull   \sh8, \c
	imull   \sh8, \t
	andl    $0xFF00FF00, \c
	andl    $0x00FF0000, \t
	orl     \t, \c
	shrl    $8, \c
	.endm

/*
 * tex_span_exact_block. Count already in EB_N(%rsp), shade in r8d.
 *
 * Cold: only blocks whose linear fit was refused arrive here, and the fit is
 * refused when the block sweeps more than a texture tile or the quotient has
 * left the reciprocal's exact range. Two exact quotients per pixel, because
 * that is what the reference rasterizer computes and matching it bit for bit
 * is the entire point; DIVR makes them without touching the divide unit.
 *
 * Works from stack copies of au/bv/cw so ebx/ebp/esi stay the caller's, and
 * touches only eax/ecx/edx/r9d besides.
 */
	.macro EXACTBLK sh, twm1, shreg
	movl    %ebx, EB_AU(%rsp)
	movl    %ebp, EB_BV(%rsp)
	movl    %esi, EB_CW(%rsp)
	movl    $0, EB_I(%rsp)
1:
	movl    EB_I(%rsp), %r9d
	cmpl    EB_N(%rsp), %r9d
	jge     3f
	movl    EB_CW(%rsp), %ecx
	sarl    $\sh, %ecx
	testl   %ecx, %ecx
	je      2f

	/*
	 * The two exact divides the C spends here, made without the divide
	 * unit: one divss builds 1/w and DIVR refines both quotients from it
	 * with 32-bit multiplies. A Pentium 4 idivl is 60-80 cycles and two of
	 * them serialise; its divss is around 23 and the two refines overlap.
	 *
	 * |w| == 1 is peeled first: DIVR wants |w| >= 2, the quotients are
	 * just +/-n, and negl wraps INT_MIN / -1 where idivl would fault.
	 * w == 0 cannot reach here -- the testl above skips it.
	 */
	cmpl    $1, %ecx
	je      4f
	cmpl    $-1, %ecx
	je      5f
	movss   LC_one(%rip), %xmm1
	cvtsi2ssl %ecx, %xmm2
	divss   %xmm2, %xmm1
	DIVR    EB_AU(%rsp), %ecx, %r9d, %edx, %eax
	movd    %r9d, %xmm0
	DIVR    EB_BV(%rsp), %ecx, %r9d, %edx, %eax
	movl    %r9d, %edx
	movd    %xmm0, %eax
	jmp     0f
4:
	movl    EB_AU(%rsp), %eax
	movl    EB_BV(%rsp), %edx
	jmp     0f
5:
	movl    EB_AU(%rsp), %eax
	negl    %eax
	movl    EB_BV(%rsp), %edx
	negl    %edx
0:
	/* eax = au / w, edx = bv / w */

	/* clamp(au / w, 0, texture_width - 1) */
	xorl    %r9d, %r9d
	testl   %eax, %eax
	cmovsl  %r9d, %eax
	movl    $\twm1, %r9d
	cmpl    %r9d, %eax
	cmovgl  %r9d, %eax

	/* (bv / w) & (texture_width - 1), already the row offset once shifted */
	andl    $\twm1, %edx
	shll    $\sh, %edx
	addl    %edx, %eax

	movl    (%r12,%rax,4), %eax
	SHADEBLEND %eax, %edx, \shreg
	movl    EB_I(%rsp), %r9d
	movl    %eax, (%r13,%r9,4)
2:
	movl    ST_AU(%rsp), %eax
	addl    %eax, EB_AU(%rsp)
	movl    ST_BV(%rsp), %eax
	addl    %eax, EB_BV(%rsp)
	movl    ST_CW(%rsp), %eax
	addl    %eax, EB_CW(%rsp)
	incl    EB_I(%rsp)
	jmp     1b
3:
	.endm

	.globl  toridraw_texspan_opaque_lerp8_v3_asm
	.def    toridraw_texspan_opaque_lerp8_v3_asm; .scl 2; .type 32; .endef

toridraw_texspan_opaque_lerp8_v3_asm:
	pushq   %rbx
	pushq   %rbp
	pushq   %rsi
	pushq   %rdi
	pushq   %r12
	pushq   %r13
	pushq   %r14
	pushq   %r15
	subq    $144, %rsp
	movdqu  %xmm6, SV_XMM6(%rsp)
	movdqu  %xmm7, SV_XMM7(%rsp)

	/* x0 = (screen_x0_ish16 - 1) >> 16, floored at 0 */
	leal    -1(%r8), %eax
	sarl    $16, %eax
	xorl    %r11d, %r11d
	testl   %eax, %eax
	cmovsl  %r11d, %eax

	/* x1 = screen_x1_ish16 >> 16, capped at screen_width - 1 */
	movl    %r9d, %r10d
	sarl    $16, %r10d
	leal    -1(%rdx), %r11d
	cmpl    %edx, %r10d
	cmovgel %r11d, %r10d

	cmpl    %r10d, %eax
	jge     .Lret

	/* adjust = x0 - (screen_width >> 1); au/bv/cw stepped to the left end */
	movl    %edx, %r11d
	sarl    $1, %r11d
	movl    %eax, %edi
	subl    %r11d, %edi

	movl    A(6), %ebx
	movl    A(9), %r9d
	imull   %edi, %r9d
	addl    %r9d, %ebx

	movl    A(7), %ebp
	movl    A(10), %r9d
	imull   %edi, %r9d
	addl    %r9d, %ebp

	movl    A(8), %esi
	movl    A(11), %r9d
	imull   %edi, %r9d
	addl    %r9d, %esi

	/* steps -> blocks, remaining */
	movl    %r10d, %edi
	subl    %eax, %edi
	movl    %edi, %r10d
	sarl    $3, %r10d
	andl    $7, %edi
	movl    %edi, REMAIN(%rsp)

	/* dst = pixel_buffer + pixel_offset + x0 */
	movl    A(5), %edi
	addl    %eax, %edi
	movslq  %edi, %rdi
	leaq    (%rcx,%rdi,4), %r13

	/* shade = wrap_add(shade, wrap_mul(step_shade, x0)) */
	movl    A(13), %edi
	imull   %eax, %edi
	addl    A(12), %edi

	/* per-block and per-pixel steps */
	movl    A(9), %eax
	movl    %eax, ST_AU(%rsp)
	shll    $3, %eax
	movl    %eax, ST_AU8(%rsp)
	movl    A(10), %eax
	movl    %eax, ST_BV(%rsp)
	shll    $3, %eax
	movl    %eax, ST_BV8(%rsp)
	movl    A(11), %eax
	movl    %eax, ST_CW(%rsp)
	shll    $3, %eax
	movl    %eax, ST_CW8(%rsp)
	movl    A(13), %eax
	shll    $3, %eax
	movl    %eax, ST_SH8(%rsp)

	movq    A(14), %r12
	movl    %r10d, BLKN(%rsp)
	movl    %edi, SHADE(%rsp)
	movl    ST_AU8(%rsp), %eax
	addl    %eax, %eax
	movl    %eax, ST_AU16(%rsp)
	movl    ST_BV8(%rsp), %eax
	addl    %eax, %eax
	movl    %eax, ST_BV16(%rsp)
	movl    ST_CW8(%rsp), %eax
	addl    %eax, %eax
	movl    %eax, ST_CW16(%rsp)
	addl    ST_CW8(%rsp), %eax
	movl    %eax, ST_CW24(%rsp)

	cmpl    $128, A(15)
	je      .Lwidth128
	jmp     .Lwidth64

.Lret:
	movdqu  SV_XMM6(%rsp), %xmm6
	movdqu  SV_XMM7(%rsp), %xmm7
	addq    $144, %rsp
	popq    %r15
	popq    %r14
	popq    %r13
	popq    %r12
	popq    %rdi
	popq    %rsi
	popq    %rbp
	popq    %rbx
	ret

/*
 * The span body, emitted once per texture width.
 *
 * ebx au, ebp bv, esi cw, r12 texels, r13 dst, r14d/r15d cur, r10d/r11d nxt
 * (steps during the fill), edi/r8d fut, eax/ecx/edx/r9d scratch. The block
 * count and the shade live in the frame, because the third quotient pair is
 * worth more than either: they are touched once per block and never by the
 * fill, while fut is what lets the divide overlap anything at all.
 *
 * THE PIPELINE
 *
 * nxt of block k and cur of block k+1 are the same quotient of the same
 * au/bv/cw, so there is exactly one new pair per block, and it is a pure
 * function of accumulators that are known from the top of the span. The
 * measurement that forced this shape: with the fill's inputs held constant --
 * every other instruction still executing -- the kernel ran 67.9 us against the
 * C twin's 105.5. Restore the dependency and it is 162.8. The work was never
 * the problem; divss latency standing in front of the fill was.
 *
 * So block k issues the quotients for block k+2 at its top and does not read
 * them until block k+1 rotates them in. The fill in between is around ten
 * cycles of throughput with nothing to wait for, and the divide finishes
 * underneath it.
 *
 * have_cur is gone, and so is the .Lnow skip. A block whose w is zero draws
 * nothing, and that fact now travels in cur_u itself as -1 -- QUOTU clamps
 * every real quotient into [0, texture_width - 1], so -1 cannot be one. One
 * compare replaces a flag register, which is what paid for fut.
 */
	.macro SPANBODY sh, twm1, vmask, twm1f, tag
	/* Prime cur (block 0). */
	movl    %esi, %ecx
	sarl    $\sh, %ecx
	testl   %ecx, %ecx
	je      1f
	RECIP   %ecx
	QUOTU   %ebx, \twm1f, %r14d
	WRAPQ   %ebp, %ecx, %r15d
	jmp     2f
1:
	movl    $-1, %r14d
2:
	/* Prime nxt (block 1). */
	movl    %esi, %ecx
	addl    ST_CW8(%rsp), %ecx
	sarl    $\sh, %ecx
	testl   %ecx, %ecx
	je      3f
	RECIP   %ecx
	movl    %ebx, %edx
	addl    ST_AU8(%rsp), %edx
	QUOTU   %edx, \twm1f, %r10d
	movl    %ebp, %edx
	addl    ST_BV8(%rsp), %edx
	WRAPQ   %edx, %ecx, %r11d
	jmp     4f
3:
	movl    $-1, %r10d
4:
	/* Seed the reciprocal pipeline: block 0 consumes 1/w_2. */
	movl    %esi, %ecx
	addl    ST_CW16(%rsp), %ecx
	sarl    $\sh, %ecx
	RECIP7  %ecx

	movl    BLKN(%rsp), %eax
	testl   %eax, %eax
	jle     .Ltail\tag

.Lblk\tag:
	/*
	 * Two hoists in one. The reciprocal for block k+3 is issued here, into
	 * xmm7, while the one issued three blocks back is consumed to build the
	 * quotient pair for block k+2. Everything below is the shadow both hide
	 * in.
	 */
	movl    %esi, %ecx
	addl    ST_CW16(%rsp), %ecx
	sarl    $\sh, %ecx
	movaps  %xmm7, %xmm1
	movl    %esi, %eax
	addl    ST_CW24(%rsp), %eax
	sarl    $\sh, %eax
	RECIP7  %eax
	testl   %ecx, %ecx
	je      .Lfutz\tag
	movl    %ebx, %edx
	addl    ST_AU16(%rsp), %edx
	QUOTU   %edx, \twm1f, %edi
	movl    %ebp, %edx
	addl    ST_BV16(%rsp), %edx
	WRAPQ   %edx, %ecx, %r8d
.Lfutd\tag:
	/* w was zero here: the block draws nothing, but still rotates. */
	cmpl    $-1, %r14d
	je      .Lrot\tag
	/* w is zero one block on: the reference takes the exact path. */
	cmpl    $-1, %r10d
	je      .Lexact8\tag

	/* tex_span_lerp8_fits(cur_v, nxt_v, texture_width) */
	movl    %r15d, %eax
	addl    $536870912, %eax
	cmpl    $1073741824, %eax
	ja      .Lexact8\tag
	movl    %r11d, %eax
	addl    $536870912, %eax
	cmpl    $1073741824, %eax
	ja      .Lexact8\tag
	movl    %r11d, %eax
	subl    %r15d, %eax
	addl    $(\twm1 + 1), %eax
	cmpl    $(2 * \twm1 + 2), %eax
	ja      .Lexact8\tag

	/*
	 * s_u / s_v, scan seeds, shade broadcast, then the eight pixels.
	 *
	 * cur is rotated to nxt here rather than after the fill, which frees
	 * r10d/r11d to hold the two steps for the whole of FILL8. That is the
	 * point: the steps are read sixteen times and must not be in the frame.
	 */
	movl    %r10d, %edx
	subl    %r14d, %edx
	shll    $(\sh - 3), %edx
	movl    %r11d, %r9d
	subl    %r15d, %r9d
	shll    $(\sh - 3), %r9d

	movl    %r14d, %eax
	shll    $\sh, %eax
	movl    %r15d, %ecx
	andl    $\twm1, %ecx
	shll    $\sh, %ecx

	movl    %r10d, %r14d
	movl    %r11d, %r15d
	movl    %edx, %r10d
	movl    %r9d, %r11d

	movl    SHADE(%rsp), %edx
	sarl    $8, %edx
	movd    %edx, %xmm4
	pshuflw $0, %xmm4, %xmm4
	pshufd  $0, %xmm4, %xmm4
	pxor    %xmm5, %xmm5

	FILL8   \sh, \twm1, \vmask
	jmp     .Ladv\tag

.Lexact8\tag:
	/*
	 * Shade goes in r14d. r14d/r15d hold cur, which .Lrot is about to
	 * overwrite from r10d/r11d, so they are already dead here.
	 */
	movl    SHADE(%rsp), %r14d
	sarl    $8, %r14d
	movl    $8, EB_N(%rsp)
	EXACTBLK \sh, \twm1, %r14d
.Lrot\tag:
	movl    %r10d, %r14d
	movl    %r11d, %r15d
.Ladv\tag:
	movl    %edi, %r10d
	movl    %r8d, %r11d
	addl    ST_AU8(%rsp), %ebx
	addl    ST_BV8(%rsp), %ebp
	addl    ST_CW8(%rsp), %esi
	addq    $32, %r13
	movl    ST_SH8(%rsp), %eax
	addl    %eax, SHADE(%rsp)
	decl    BLKN(%rsp)
	jne     .Lblk\tag
	jmp     .Ltail\tag

.Lfutz\tag:
	movl    $-1, %edi
	jmp     .Lfutd\tag

/*
 * The tail. cur and nxt are already in hand -- the loop's last rotate left
 * cur_N in r14d/r15d and cur_N+1 in r10d/r11d, and when the loop never ran the
 * priming above did the same -- so the two divides the tail used to repeat are
 * simply gone.
 */
.Ltail\tag:
	movl    REMAIN(%rsp), %eax
	testl   %eax, %eax
	jle     .Lret
	cmpl    $-1, %r14d
	je      .Lret
	cmpl    $-1, %r10d
	je      .Ltexact\tag

	movl    %r15d, %eax
	addl    $536870912, %eax
	cmpl    $1073741824, %eax
	ja      .Ltexact\tag
	movl    %r11d, %eax
	addl    $536870912, %eax
	cmpl    $1073741824, %eax
	ja      .Ltexact\tag
	movl    %r11d, %eax
	subl    %r15d, %eax
	addl    $(\twm1 + 1), %eax
	cmpl    $(2 * \twm1 + 2), %eax
	jbe     .Ltlerp\tag

.Ltexact\tag:
	movl    SHADE(%rsp), %r8d
	sarl    $8, %r8d
	movl    REMAIN(%rsp), %eax
	movl    %eax, EB_N(%rsp)
	EXACTBLK \sh, \twm1, %r8d
	jmp     .Lret

/*
 * The tail carries a quarter of the span's pixels, so it gets the same
 * treatment as the block loop: steps in registers, count in a register, and one
 * branch per pixel instead of a memory compare and an unconditional jump back.
 * au/bv/cw are dead once the quotients above are computed, so ebx/ebp/esi are
 * free and no slot is needed for any of it.
 */
.Ltlerp\tag:
	movl    %r10d, %ebx
	subl    %r14d, %ebx
	shll    $(\sh - 3), %ebx
	movl    %r11d, %ebp
	subl    %r15d, %ebp
	shll    $(\sh - 3), %ebp

	movl    %r14d, %eax
	shll    $\sh, %eax
	movl    %r15d, %ecx
	andl    $\twm1, %ecx
	shll    $\sh, %ecx

	movl    SHADE(%rsp), %r8d
	sarl    $8, %r8d
	movl    REMAIN(%rsp), %esi
	xorl    %r14d, %r14d
.Ltloop\tag:
	movl    %eax, %edx
	sarl    $\sh, %edx
	movl    %ecx, %r15d
	andl    $\vmask, %r15d
	addl    %r15d, %edx
	movl    (%r12,%rdx,4), %r10d
	SHADEBLEND %r10d, %r11d, %r8d
	movl    %r10d, (%r13,%r14,4)
	addl    %ebx, %eax
	addl    %ebp, %ecx
	incl    %r14d
	cmpl    %esi, %r14d
	jl      .Ltloop\tag
	jmp     .Lret
	.endm

.Lwidth128:
	SPANBODY 7, 127, 0x3F80, LC_f127, 128

.Lwidth64:
	SPANBODY 6, 63, 0x0FC0, LC_f63, 64

	.section .rdata,"dr"
	.align 16
LC_absmask:
	.long   0x7FFFFFFF
	.long   0
	.long   0
	.long   0
LC_one:
	.float  1.0
/* TEX_SPAN_RECIPROCAL_EXACT_LIMIT */
LC_lim:
	.float  2097152.0
LC_f127:
	.float  127.0
LC_f63:
	.float  63.0
