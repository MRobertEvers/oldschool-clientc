/*
 * The perspective texture span, as macros -- the shared half of the i686
 * kernels.
 *
 * Two entry points expand these: tex_span_i686.S, which draws one span from a
 * cdecl call, and tex_tri_i686.S, which draws a whole triangle and reaches the
 * same fill without a call at all. They include this file rather than each
 * carrying a copy, because SPANBODY expands to the largest block of code in the
 * rasterizer and two of it would not share a trace cache.
 *
 * Everything below reads and writes the frame slots defined at the top and
 * NOTHING reads a caller argument -- that is what makes it reusable. Whoever
 * expands SPANBODY owes it AU/BV/CW at the left end, REMAIN, BLKN, SHADE, SV,
 * the ST_* strides, %edi at the first pixel and %esi at the texels; and it
 * hands control to the label passed as `done`.
 *
 * WHAT i686 CHANGES, AND WHAT IT DOES NOT
 *
 * The pipeline is identical -- three-deep cur/nxt/fut, with the reciprocal a
 * further stage ahead in xmm7 -- because that is the part that mattered. What
 * changes is where the state lives. Eight general registers cannot hold three
 * quotient pairs, three accumulators, two pointers and two scratch, so the
 * pipeline state moves to the frame and the registers are spent on the only
 * thing read sixteen times per block: the fill.
 *
 * That trade is the right way round here, and the pipeline is the reason.
 * cur/nxt/fut are touched once per block and never inside the fill, so a stack
 * slot costs them one load off the critical path. The per-pixel steps are
 * touched sixteen times, so a stack slot would put a store-to-load forward in
 * front of every pixel. s_u therefore keeps ebx for the whole fill; only s_v is
 * reloaded, and only eight times, because GATHER1 needs two scratch registers
 * to add the row offset to the column and i686 has exactly that many left.
 *
 * xmm6 and xmm7 are not saved. Every xmm register is caller-saved in the
 * 32-bit Windows calling convention -- unlike x64, where xmm6-xmm15 are not,
 * and where this file twin spends two movdqu on them.
 *
 * ABLATION: this kernel does not honour TORIDRAW_ABLATE_TEX, and the makefile
 * withdraws it from any build that asks for the census or the ablation modes.
 * Those counters live in the C twin; measure them there.
 */

/* ------------------------------------------------------------------ */
/*  Stack frame. Four pushes then FRAME bytes, and cdecl puts the      */
/*  return address and then the arguments above them, so argument n    */
/*  (0-based) sits at esp + FRAME + 20 + 4n.                           */
/* ------------------------------------------------------------------ */
#define AU        0
#define BV        4
#define CW        8
#define ST_AU     12
#define ST_BV     16
#define ST_CW     20
#define ST_AU8    24
#define ST_BV8    28
#define ST_CW8    32
#define ST_SH8    36
#define ST_AU16   40
#define ST_BV16   44
#define ST_CW16   48
#define ST_CW24   52
#define REMAIN    56
#define BLKN      60
#define SHADE     64
#define CUR_U     68
#define CUR_V     72
#define NXT_U     76
#define NXT_V     80
#define FUT_U     84
#define FUT_V     88
#define SV        92
#define EB_AU     96
#define EB_BV     100
#define EB_CW     104
#define EB_N      108
#define EB_I      112
#define EB_SH     116

#define FRAME     124
#define A(n) (144 + 4 * (n))(%esp)

	.text

/* ------------------------------------------------------------------ */
/* w != 0 already established. xmm1 <- 1.0f / (float)w                 */
/*                                                                     */
/* rcpss and one Newton-Raphson step, not divss. On a Pentium 4 divss   */
/* is unpipelined -- about twenty-three cycles of ISSUE, not merely of  */
/* latency -- while rcpss is four and two. The refinement                */
/*                                                                     */
/*     r1 = 2*r0 - w*r0*r0                                             */
/*                                                                     */
/* squares rcpss's twelve bits into roughly twenty-four, which is       */
/* float32's own precision; twelve alone is not enough, because WRAPQ   */
/* trusts this quotient out to LC_lim = 2^21 and 2.4e-4 of 2^21 is five */
/* hundred texel rows. This kernel was already multiplying by a         */
/* reciprocal instead of dividing, so it never produced n/w exactly --  */
/* the step widens an error the code already accepts, from about one    */
/* ulp to two or three, rather than introducing a new kind of one.      */
/* Written without LC_one: doubling r0 is where the 2 comes from.       */
/*                                                                     */
/* Clobbers xmm0 only, which is what the old divss form clobbered too.  */
/* ------------------------------------------------------------------ */
	.macro RECIP w
	cvtsi2ssl \w, %xmm0
	rcpss   %xmm0, %xmm1
	mulss   %xmm1, %xmm0
	mulss   %xmm1, %xmm0
	addss   %xmm1, %xmm1
	subss   %xmm0, %xmm1
	.endm

/*
 * The same reciprocal, three blocks early and into xmm7.
 *
 * The reciprocal runs a stage ahead of the quotients that consume it: block k
 * computes for block k+3 and multiplies for block k+2. xmm7 is free the whole
 * way through, because the fill lives in xmm0..xmm6, so this costs no general
 * register. On i686 that is not a nicety; there were none to spend.
 *
 * That hoist hides LATENCY, and it is why the divide here was tolerable for as
 * long as it was. It does nothing for throughput, which is the half that
 * actually bit: a Pentium 4's divss occupies its unit for around twenty-three
 * cycles whether or not anyone is waiting on the result, and an eight-pixel
 * fill is nowhere near that much shadow. So the divide is gone -- see RECIP for
 * the rcpss-plus-Newton sequence and why one refinement step is the right
 * number. Same five-instruction shape here, writing xmm7 and scratching xmm0;
 * xmm1 already holds the reciprocal issued three blocks back, moved out of xmm7
 * before this macro runs, so the two never collide.
 *
 * A zero w is folded to one rather than branched around. Such a block is
 * skipped by the sentinel two blocks later and its quotient never read; the
 * substitution exists only to keep a reciprocal of zero -- an infinity that
 * would then be multiplied by a numerator -- out of the pipeline.
 */
	.macro RECIP7 w, tmp
	movl    $1, \tmp
	testl   \w, \w
	cmovel  \tmp, \w
	cvtsi2ssl \w, %xmm0
	rcpss   %xmm0, %xmm7
	mulss   %xmm7, %xmm0
	mulss   %xmm7, %xmm0
	addss   %xmm7, %xmm7
	subss   %xmm0, %xmm7
	.endm

/*
 * tex_span_u_quotient: clamp in float, THEN convert.
 *
 * The order is the whole point -- an out-of-range cvttss2si yields INT_MIN on
 * x86, which is why the C clamps in float too. maxss against +0.0 also lands
 * q == -0.0 on zero, because MAXSS returns its source operand when the two
 * compare equal.
 *
 * n numerator (register or m32), twm1f float (texture_width - 1), out a
 * register. Clobbers xmm2, xmm3.
 */
	.macro QUOTU n, twm1f, out
	cvtsi2ssl \n, %xmm2
	mulss   %xmm1, %xmm2
	xorps   %xmm3, %xmm3
	maxss   %xmm3, %xmm2
	minss   \twm1f, %xmm2
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
 * n numerator (register or m32), w divisor (register or m32) -- but not eax or
 * edx, which the divide sequence writes before it reads w. out a register.
 * Clobbers xmm2, xmm3, eax, edx.
 */
	.macro WRAPQ n, w, out
	cvtsi2ssl \n, %xmm2
	mulss   %xmm1, %xmm2
	movaps  %xmm2, %xmm3
	andps   LC_absmask, %xmm3
	comiss  LC_lim, %xmm3
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
 * shade_blend4_sse in place on v, 16-bit shade broadcast in xmm4, zero in
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
 * One texel into dstx. u_scan eax, v_scan ecx, texels esi, scratch edx/ebp.
 *
 * The index is ((u_scan >> SH) & TWM1) + (v_scan & VMASK) -- the C expression
 * with v_mask spelled out. v_mask is exactly (texture_width - 1) << SH, so the
 * masked v_scan is already the row offset and needs no shift of its own.
 *
 * The u mask is dropped, and that is a claim rather than a shortcut, so here is
 * the argument. QUOTU clamps both endpoints into [0, texture_width - 1] before
 * they ever reach here, u_scan runs from cur_u << SH to nxt_u << SH, and a
 * linear ramp between two in-range endpoints stays in range. So u_scan >> SH is
 * already inside [0, texture_width - 1] and & TWM1 cannot change it. The C
 * keeps the mask because the same helper serves paths whose endpoints are not
 * clamped; this one only ever runs behind QUOTU.
 *
 * Two scratch registers, and there is no way down to one: the column and the
 * row are independent computations that have to be summed, and neither folds
 * into the addressing mode. That is what costs s_v its register.
 */
	.macro GATHER1 sh, twm1, vmask, dstx
	movl    %eax, %edx
	sarl    $\sh, %edx
	movl    %ecx, %ebp
	andl    $\vmask, %ebp
	addl    %ebp, %edx
	movd    (%esi,%edx,4), \dstx
	.endm

/* Four texels -> xmm0, advancing the scan by s_u (ebx) / s_v (frame). */
	.macro GATHER4 sh, twm1, vmask
	GATHER1 \sh, \twm1, \vmask, %xmm0
	addl    %ebx, %eax
	addl    SV(%esp), %ecx
	GATHER1 \sh, \twm1, \vmask, %xmm6
	addl    %ebx, %eax
	addl    SV(%esp), %ecx
	punpckldq %xmm6, %xmm0
	GATHER1 \sh, \twm1, \vmask, %xmm1
	addl    %ebx, %eax
	addl    SV(%esp), %ecx
	GATHER1 \sh, \twm1, \vmask, %xmm6
	addl    %ebx, %eax
	addl    SV(%esp), %ecx
	punpckldq %xmm6, %xmm1
	punpcklqdq %xmm1, %xmm0
	.endm

/* The eight-pixel linear block, into edi. */
	.macro FILL8 sh, twm1, vmask
	GATHER4 \sh, \twm1, \vmask
	BLEND4  %xmm0
	movdqu  %xmm0, (%edi)
	GATHER4 \sh, \twm1, \vmask
	BLEND4  %xmm0
	movdqu  %xmm0, 16(%edi)
	.endm

/*
 * shade_blend, scalar. c is the texel and receives the result, t is scratch,
 * sh8 is the shade (0..255) and may be a memory operand.
 *
 * Two masked multiplies rather than three, because red and blue are eight bits
 * apart and cannot carry into one another: c * shade is at most 255 * 255,
 * which leaves the top byte of each 16-bit field clear.
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
 * tex_span_exact_block. Count already in EB_N, shade in EB_SH.
 *
 * Cold: only blocks whose linear fit was refused arrive here, and the fit is
 * refused when the block sweeps more than a texture tile or the quotient has
 * left the reciprocal exact range. Two exact quotients per pixel, because that
 * is what the reference rasterizer computes and matching it bit for bit is the
 * entire point.
 *
 * Works from its own copies of au/bv/cw so the span accumulators are left
 * alone, and touches only eax/ecx/edx/ebx/ebp besides.
 */
	.macro EXACTBLK sh, twm1
	movl    AU(%esp), %eax
	movl    %eax, EB_AU(%esp)
	movl    BV(%esp), %eax
	movl    %eax, EB_BV(%esp)
	movl    CW(%esp), %eax
	movl    %eax, EB_CW(%esp)
	movl    $0, EB_I(%esp)
1:
	movl    EB_I(%esp), %ebx
	cmpl    EB_N(%esp), %ebx
	jge     3f
	movl    EB_CW(%esp), %ecx
	sarl    $\sh, %ecx
	testl   %ecx, %ecx
	je      2f

	/*
	 * The C's own two divides, idivl for idivl. A reciprocal refine was
	 * tried in this spot -- one divss, then each quotient from a rounded
	 * float estimate plus an exact 32-bit fixup, brute-forced correct
	 * over 209 million adversarial pairs -- and measured FIVE TIMES
	 * slower than this. Each refined quotient is a serial chain of two
	 * converts, two multiplies and a masked fixup, about 45 cycles that
	 * out-of-order execution cannot overlap because each step feeds the
	 * next; a modern idivl is about 15. A Pentium 4 does not save it
	 * either: NetBurst imull is 15-18 cycles and movd about 10, which
	 * prices the refine's instruction mix above the 60-80 cycle idivl
	 * it replaces. The divide unit wins this one on every target.
	 *
	 * w == -1 is peeled because idivl faults on INT_MIN / -1 where negl
	 * simply wraps. w == 0 cannot reach here -- the testl above skips it.
	 */
	cmpl    $-1, %ecx
	je      5f
	movl    EB_AU(%esp), %eax
	cltd
	idivl   %ecx
	movl    %eax, %ebp
	movl    EB_BV(%esp), %eax
	cltd
	idivl   %ecx
	movl    %eax, %edx
	movl    %ebp, %eax
	jmp     0f
5:
	movl    EB_AU(%esp), %eax
	negl    %eax
	movl    EB_BV(%esp), %edx
	negl    %edx
0:
	/* eax = au / w, edx = bv / w */

	/* clamp(au / w, 0, texture_width - 1) */
	xorl    %ebp, %ebp
	testl   %eax, %eax
	cmovsl  %ebp, %eax
	movl    $\twm1, %ebp
	cmpl    %ebp, %eax
	cmovgl  %ebp, %eax

	/* (bv / w) & (texture_width - 1), the row offset once shifted */
	andl    $\twm1, %edx
	shll    $\sh, %edx
	addl    %edx, %eax

	movl    (%esi,%eax,4), %eax
	SHADEBLEND %eax, %edx, EB_SH(%esp)
	movl    %eax, (%edi,%ebx,4)
2:
	movl    ST_AU(%esp), %eax
	addl    %eax, EB_AU(%esp)
	movl    ST_BV(%esp), %eax
	addl    %eax, EB_BV(%esp)
	movl    ST_CW(%esp), %eax
	addl    %eax, EB_CW(%esp)
	incl    EB_I(%esp)
	jmp     1b
3:
	.endm

/*
 * The span itself, specialised per texture width so every shift and mask is an
 * immediate.
 *
 * REGISTERS, once the fill is running:
 *   eax  u_scan          ecx  v_scan
 *   ebx  s_u             esi  texels          edi  dst
 *   edx, ebp             scratch for the texel index
 * s_v, au/bv/cw, the cur/nxt/fut quotient pairs, the block count and the shade
 * all live in the frame. See the header: they are read once per block, not
 * sixteen times, so they are the ones that can afford to.
 *
 * THE PIPELINE. Three quotient pairs are live -- cur (this block), nxt (the
 * next, which supplies this one its right-hand endpoint) and fut. nxt of block
 * k and cur of block k+1 are the same quotient of the same au/bv/cw, so there
 * is exactly one new pair per block, not two. Block k issues fut for block k+2
 * at its top and does not read it until k+1, so the divide chain has a whole
 * fill to hide in. The reciprocal that feeds those quotients is a further
 * stage ahead again, in xmm7 -- see RECIP7.
 *
 * A cur_u of -1 means the block had w == 0 and draws nothing. QUOTU clamps
 * every real quotient into [0, texture_width - 1], so -1 is unreachable
 * otherwise and one cmpl replaces the flag register this used to need.
 */
	.macro SPANBODY sh, twm1, vmask, twm1f, tag, done

	/* --- prime cur, the quotient pair for block 0 --- */
	movl    CW(%esp), %ecx
	sarl    $\sh, %ecx
	testl   %ecx, %ecx
	je      1f
	RECIP   %ecx
	QUOTU   AU(%esp), \twm1f, %edx
	movl    %edx, CUR_U(%esp)
	WRAPQ   BV(%esp), %ecx, %edx
	movl    %edx, CUR_V(%esp)
	jmp     2f
1:
	movl    $-1, CUR_U(%esp)
2:

	/* --- prime nxt, the pair for block 1 --- */
	movl    CW(%esp), %ecx
	addl    ST_CW8(%esp), %ecx
	sarl    $\sh, %ecx
	testl   %ecx, %ecx
	je      3f
	RECIP   %ecx
	movl    AU(%esp), %edx
	addl    ST_AU8(%esp), %edx
	QUOTU   %edx, \twm1f, %ebx
	movl    %ebx, NXT_U(%esp)
	movl    BV(%esp), %edx
	addl    ST_BV8(%esp), %edx
	WRAPQ   %edx, %ecx, %ebx
	movl    %ebx, NXT_V(%esp)
	jmp     4f
3:
	movl    $-1, NXT_U(%esp)
4:

	/* Seed the reciprocal pipeline: block 0 consumes 1/w_2. */
	movl    CW(%esp), %eax
	addl    ST_CW16(%esp), %eax
	sarl    $\sh, %eax
	RECIP7  %eax, %edx

	cmpl    $0, BLKN(%esp)
	jle     .Ltail\tag

.Lblk\tag:
	/*
	 * Two hoists in one. The reciprocal for block k+3 is issued here, into
	 * xmm7, while the one issued three blocks back is consumed to build the
	 * quotient pair for block k+2. Everything below is the shadow both hide
	 * in.
	 */
	movl    CW(%esp), %ecx
	addl    ST_CW16(%esp), %ecx
	sarl    $\sh, %ecx
	movaps  %xmm7, %xmm1
	movl    CW(%esp), %eax
	addl    ST_CW24(%esp), %eax
	sarl    $\sh, %eax
	RECIP7  %eax, %edx
	testl   %ecx, %ecx
	je      .Lfutz\tag
	movl    AU(%esp), %edx
	addl    ST_AU16(%esp), %edx
	QUOTU   %edx, \twm1f, %ebx
	movl    %ebx, FUT_U(%esp)
	movl    BV(%esp), %edx
	addl    ST_BV16(%esp), %edx
	WRAPQ   %edx, %ecx, %ebx
	movl    %ebx, FUT_V(%esp)
.Lfutd\tag:

	cmpl    $-1, CUR_U(%esp)
	je      .Lrot\tag
	cmpl    $-1, NXT_U(%esp)
	je      .Lexact8\tag

	/*
	 * Does the linear fit hold? Both endpoints must stay inside the range
	 * where a 16.16 scan cannot overflow, and the block must not sweep more
	 * than one texture tile in v -- past that the wrap is not a straight
	 * line and the exact path has to run.
	 */
	movl    CUR_V(%esp), %eax
	addl    $536870912, %eax
	cmpl    $1073741824, %eax
	ja      .Lexact8\tag
	movl    NXT_V(%esp), %ecx
	movl    %ecx, %eax
	addl    $536870912, %eax
	cmpl    $1073741824, %eax
	ja      .Lexact8\tag
	subl    CUR_V(%esp), %ecx
	movl    %ecx, %eax
	addl    $(\twm1 + 1), %eax
	cmpl    $(2 * \twm1 + 2), %eax
	ja      .Lexact8\tag

	/* ecx still holds nxt_v - cur_v: the per-pixel v step, pre-shifted. */
	shll    $(\sh - 3), %ecx
	movl    %ecx, SV(%esp)
	movl    NXT_U(%esp), %ebx
	subl    CUR_U(%esp), %ebx
	shll    $(\sh - 3), %ebx

	movl    CUR_U(%esp), %eax
	shll    $\sh, %eax
	movl    CUR_V(%esp), %ecx
	andl    $\twm1, %ecx
	shll    $\sh, %ecx

	/* cur <- nxt while the endpoints are still in registers. */
	movl    NXT_U(%esp), %edx
	movl    %edx, CUR_U(%esp)
	movl    NXT_V(%esp), %edx
	movl    %edx, CUR_V(%esp)

	movl    SHADE(%esp), %edx
	sarl    $8, %edx
	movd    %edx, %xmm4
	pshuflw $0, %xmm4, %xmm4
	pshufd  $0, %xmm4, %xmm4
	pxor    %xmm5, %xmm5

	FILL8   \sh, \twm1, \vmask
	jmp     .Ladv\tag

.Lexact8\tag:
	movl    SHADE(%esp), %eax
	sarl    $8, %eax
	movl    %eax, EB_SH(%esp)
	movl    $8, EB_N(%esp)
	EXACTBLK \sh, \twm1
.Lrot\tag:
	movl    NXT_U(%esp), %eax
	movl    %eax, CUR_U(%esp)
	movl    NXT_V(%esp), %eax
	movl    %eax, CUR_V(%esp)
.Ladv\tag:
	movl    FUT_U(%esp), %eax
	movl    %eax, NXT_U(%esp)
	movl    FUT_V(%esp), %eax
	movl    %eax, NXT_V(%esp)
	movl    ST_AU8(%esp), %eax
	addl    %eax, AU(%esp)
	movl    ST_BV8(%esp), %eax
	addl    %eax, BV(%esp)
	movl    ST_CW8(%esp), %eax
	addl    %eax, CW(%esp)
	movl    ST_SH8(%esp), %eax
	addl    %eax, SHADE(%esp)
	addl    $32, %edi
	decl    BLKN(%esp)
	jne     .Lblk\tag
	jmp     .Ltail\tag

.Lfutz\tag:
	movl    $-1, FUT_U(%esp)
	jmp     .Lfutd\tag

/*
 * The ragged end, 1..7 pixels. Same two tests, then either the exact loop or a
 * scalar walk of the linear scan -- no eight-wide store can be used here, so
 * there is nothing to vectorise.
 */
.Ltail\tag:
	cmpl    $0, REMAIN(%esp)
	jle     \done
	cmpl    $-1, CUR_U(%esp)
	je      \done
	cmpl    $-1, NXT_U(%esp)
	je      .Ltexact\tag

	movl    CUR_V(%esp), %eax
	addl    $536870912, %eax
	cmpl    $1073741824, %eax
	ja      .Ltexact\tag
	movl    NXT_V(%esp), %eax
	addl    $536870912, %eax
	cmpl    $1073741824, %eax
	ja      .Ltexact\tag
	movl    NXT_V(%esp), %eax
	subl    CUR_V(%esp), %eax
	addl    $(\twm1 + 1), %eax
	cmpl    $(2 * \twm1 + 2), %eax
	jbe     .Ltlerp\tag

.Ltexact\tag:
	movl    SHADE(%esp), %eax
	sarl    $8, %eax
	movl    %eax, EB_SH(%esp)
	movl    REMAIN(%esp), %eax
	movl    %eax, EB_N(%esp)
	EXACTBLK \sh, \twm1
	jmp     \done

.Ltlerp\tag:
	movl    NXT_U(%esp), %ebx
	subl    CUR_U(%esp), %ebx
	shll    $(\sh - 3), %ebx
	movl    NXT_V(%esp), %eax
	subl    CUR_V(%esp), %eax
	shll    $(\sh - 3), %eax
	movl    %eax, SV(%esp)

	movl    CUR_U(%esp), %eax
	shll    $\sh, %eax
	movl    CUR_V(%esp), %ecx
	andl    $\twm1, %ecx
	shll    $\sh, %ecx

	movl    SHADE(%esp), %edx
	sarl    $8, %edx
	movl    %edx, EB_SH(%esp)
	movl    REMAIN(%esp), %edx
	movl    %edx, EB_N(%esp)
.Ltloop\tag:
	movl    %eax, %edx
	sarl    $\sh, %edx
	movl    %ecx, %ebp
	andl    $\vmask, %ebp
	addl    %ebp, %edx
	movl    (%esi,%edx,4), %edx
	SHADEBLEND %edx, %ebp, EB_SH(%esp)
	movl    %edx, (%edi)
	addl    $4, %edi
	addl    %ebx, %eax
	addl    SV(%esp), %ecx
	decl    EB_N(%esp)
	jne     .Ltloop\tag
	jmp     \done
	.endm
	.section .rdata,"dr"
	.align 16
LC_absmask:
	.long 0x7FFFFFFF
	.long 0x7FFFFFFF
	.long 0x7FFFFFFF
	.long 0x7FFFFFFF
	.align 4
LC_one:
	.long 0x3F800000        /* 1.0f */
LC_lim:
	.long 0x4A000000        /* 2097152.0f -- above this a float32 quotient
				 * has already dropped the low bits that pick
				 * the texel row, so WRAPQ must divide. */
LC_f127:
	.long 0x42FE0000        /* 127.0f */
LC_f63:
	.long 0x427C0000        /* 63.0f */

	/* The constants sit in the middle of the translation unit now -- this
	 * file is included ABOVE the entry that uses them, where the original
	 * had them last. Switch back, or every instruction after the include
	 * assembles into .rdata and the first call into it faults. */
	.text
