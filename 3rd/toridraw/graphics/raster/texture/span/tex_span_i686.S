/*
 * toridraw_texspan_opaque_lerp8_v3_asm -- hand-written i686 (cdecl, SSE2).
 *
 * One span, called once per scanline. The kernel itself is tex_span_body.inc;
 * this file is the cdecl wrapper around it -- unpack fifteen arguments into the
 * frame slots the macros read, pick the texture width, expand the fill.
 *
 * The whole-triangle kernel in tex_tri_i686.S does the same unpacking ONCE per
 * triangle instead of once per row, which is why it exists; this entry stays
 * because toridraw_texspan_asm_test.c scores a single span against
 * draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered, and a test
 * that can only reach the kernel through a whole triangle cannot say which
 * pixel of which span went wrong.
 */

#include "tex_span_body.inc"

	.globl  _toridraw_texspan_opaque_lerp8_v3_asm
	.def    _toridraw_texspan_opaque_lerp8_v3_asm; .scl 2; .type 32; .endef

_toridraw_texspan_opaque_lerp8_v3_asm:
	pushl   %ebx
	pushl   %esi
	pushl   %edi
	pushl   %ebp
	subl    $FRAME, %esp

	/* x0 = (screen_x0_ish16 - 1) >> 16, floored at 0 */
	movl    A(2), %eax
	decl    %eax
	sarl    $16, %eax
	xorl    %edx, %edx
	testl   %eax, %eax
	cmovsl  %edx, %eax

	/* x1 = screen_x1_ish16 >> 16, capped at screen_width - 1 */
	movl    A(3), %ecx
	sarl    $16, %ecx
	movl    A(1), %edx
	decl    %edx
	cmpl    A(1), %ecx
	cmovgel %edx, %ecx

	cmpl    %ecx, %eax
	jge     .Lret

	/* adjust = x0 - (screen_width >> 1); au/bv/cw stepped to the left end */
	movl    A(1), %edx
	sarl    $1, %edx
	movl    %eax, %ebx
	subl    %edx, %ebx

	movl    A(8), %edx
	imull   %ebx, %edx
	addl    A(5), %edx
	movl    %edx, AU(%esp)
	movl    A(9), %edx
	imull   %ebx, %edx
	addl    A(6), %edx
	movl    %edx, BV(%esp)
	movl    A(10), %edx
	imull   %ebx, %edx
	addl    A(7), %edx
	movl    %edx, CW(%esp)

	/* steps -> whole blocks plus a remainder */
	subl    %eax, %ecx
	movl    %ecx, %edx
	andl    $7, %edx
	movl    %edx, REMAIN(%esp)
	sarl    $3, %ecx
	movl    %ecx, BLKN(%esp)

	/* dst = pixel_buffer + pixel_offset + x0 */
	movl    A(4), %edx
	addl    %eax, %edx
	movl    A(0), %edi
	leal    (%edi,%edx,4), %edi

	/* shade = wrap_add(shade, wrap_mul(step_shade, x0)) */
	movl    A(12), %edx
	imull   %eax, %edx
	addl    A(11), %edx
	movl    %edx, SHADE(%esp)

	/* Per-pixel, per-block, and the two lookahead strides the pipeline uses. */
	movl    A(8), %eax
	movl    %eax, ST_AU(%esp)
	shll    $3, %eax
	movl    %eax, ST_AU8(%esp)
	addl    %eax, %eax
	movl    %eax, ST_AU16(%esp)

	movl    A(9), %eax
	movl    %eax, ST_BV(%esp)
	shll    $3, %eax
	movl    %eax, ST_BV8(%esp)
	addl    %eax, %eax
	movl    %eax, ST_BV16(%esp)

	movl    A(10), %eax
	movl    %eax, ST_CW(%esp)
	shll    $3, %eax
	movl    %eax, ST_CW8(%esp)
	movl    %eax, %edx
	addl    %eax, %eax
	movl    %eax, ST_CW16(%esp)
	addl    %edx, %eax
	movl    %eax, ST_CW24(%esp)

	movl    A(12), %eax
	shll    $3, %eax
	movl    %eax, ST_SH8(%esp)

	movl    A(13), %esi

	cmpl    $128, A(14)
	je      .Lwidth128
	jmp     .Lwidth64

.Lret:
	addl    $FRAME, %esp
	popl    %ebp
	popl    %edi
	popl    %esi
	popl    %ebx
	ret

.Lwidth128:
	/* The trailing 0 is `flat`: this entry is the gradient-shaded span, so
	 * it keeps the per-block broadcast off SHADE. The constant-shade arm is
	 * reached only through tex_tri_i686.S, which primes xmm4 once per
	 * triangle -- there is nowhere in a standalone span call to do that. */
	SPANBODY 7, 127, 0x3F80, LC_f127, 128, .Lret, 0, 0

.Lwidth64:
	SPANBODY 6, 63, 0x0FC0, LC_f63, 64, .Lret, 0, 0
