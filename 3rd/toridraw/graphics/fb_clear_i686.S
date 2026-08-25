/*
 * toridraw_fb_clear32_nt_asm -- hand-written i686 (cdecl, SSE2).
 *
 * Fill a 32-bit framebuffer with a constant, using non-temporal stores.
 *
 * WHY THIS ONE IS WORTH ASSEMBLY AND THE SPAN FILL IS NOT.
 *
 * A normal store to a line that is not in cache pays a read-for-ownership: the
 * processor fetches the whole 64-byte line from RAM so that it can write four
 * bytes into it -- and here the line's old contents are about to be discarded
 * in their entirety, so that read is pure waste. movntdq skips the cache and
 * goes to a write-combine buffer instead; when consecutive stores fill a buffer
 * completely it is flushed as one full-line burst and the ownership read never
 * happens. Measured on the Pentium 4 target: 1.296 GB/s normal, 3.060 GB/s
 * non-temporal, a 2.36x difference.
 *
 * That win exists ONLY at streaming length. The same probe measured the same
 * two instruction sequences at the rasterizer's real span length -- 7.24 pixels
 * -- and found non-temporal stores NINE TIMES SLOWER. A buffer evicted before
 * it is full cannot be written as one burst, so it goes out as several partial
 * transactions; with six buffers on this part and one short run per scanline,
 * nothing ever fills. Worse, a run that short is mostly the unaligned head and
 * tail that movntdq cannot do, and mixing normal and non-temporal stores inside
 * one cache line forces the buffer out and evicts the cached copy on every
 * pixel.
 *
 * So the shape decides it, not the instruction. This buffer is 765x503x4 =
 * 1.54 MB, contiguous, aligned, written once and never read back in the same
 * pass -- the friendliest case there is. The rasterizer's spans are the
 * opposite, and deliberately keep their ordinary stores.
 *
 * At 385,395 pixels the clear moves 1.54 MB per frame: 1.19 ms at the normal
 * store rate, 0.50 ms non-temporally, on a frame that measures 37.7 ms.
 *
 * void toridraw_fb_clear32_nt_asm(uint32_t* dst, size_t count_px, uint32_t v);
 *
 *   8(%ebp)  dst        the pixel buffer; any alignment
 *  12(%ebp)  count_px   pixels, not bytes
 *  16(%ebp)  v          the value written to every one of them
 *
 * The twin is ToriDraw_FbClear32 in fb_clear.h, which stays compiled and stays
 * the reference; toridraw_fb_clear_test compares the two byte for byte across
 * every alignment and every length that exercises a different path through
 * this file.
 *
 * SFENCE, ONCE, AT THE END. Non-temporal stores are weakly ordered with respect
 * to other stores, so the caller's next write -- and, more to the point, the
 * blit that reads this buffer to put it on screen -- must not be allowed to
 * pass them. One sfence after the last store covers the whole fill. It is not
 * needed on the paths that took no non-temporal store at all, and those exits
 * skip it.
 */

	.text
	.p2align 4,,15
	.globl  _toridraw_fb_clear32_nt_asm
	.def    _toridraw_fb_clear32_nt_asm; .scl 2; .type 32; .endef

_toridraw_fb_clear32_nt_asm:
	pushl   %ebp
	movl    %esp, %ebp
	pushl   %edi
	pushl   %ebx

	movl    8(%ebp), %edi           /* dst      */
	movl    12(%ebp), %ecx          /* count_px */
	movl    16(%ebp), %eax          /* v        */

	testl   %ecx, %ecx
	jz      .Lret

	/*
	 * Head: scalar stores until the pointer is 16-byte aligned, because
	 * movntdq faults on anything else. In practice SDL hands us a page-
	 * aligned surface and this runs zero times, but a caller with a sub-
	 * rectangle would land here and must not be a crash.
	 */
.Lhead:
	testl   $15, %edi
	jz      .Laligned
	movl    %eax, (%edi)
	addl    $4, %edi
	subl    $1, %ecx
	jnz     .Lhead
	jmp     .Lret                   /* consumed by the head; no NT store */

.Laligned:
	movd    %eax, %xmm0
	pshufd  $0, %xmm0, %xmm0        /* the value, four across */

	/*
	 * Sixty-four bytes an iteration -- one whole cache line, which is the
	 * unit a write-combine buffer flushes in. Four stores per branch keeps
	 * the loop overhead off a part whose branch mispredict costs twenty
	 * cycles.
	 */
	movl    %ecx, %ebx
	shrl    $4, %ebx                /* 16 px = 64 B blocks */
	jz      .Lblk4

.Lblk64:
	movntdq %xmm0, 0(%edi)
	movntdq %xmm0, 16(%edi)
	movntdq %xmm0, 32(%edi)
	movntdq %xmm0, 48(%edi)
	addl    $64, %edi
	subl    $1, %ebx
	jnz     .Lblk64
	andl    $15, %ecx               /* what the 64-byte blocks left behind */

	/* Fewer than sixteen pixels remain; take them four at a time. */
.Lblk4:
	movl    %ecx, %ebx
	shrl    $2, %ebx
	jz      .Ltail

.Lblk16:
	movntdq %xmm0, 0(%edi)
	addl    $16, %edi
	subl    $1, %ebx
	jnz     .Lblk16
	andl    $3, %ecx

	/*
	 * Tail: the last one to three pixels are a partial line whatever we do,
	 * so they go out as ordinary stores rather than dragging a fifth
	 * write-combine buffer into existence for twelve bytes.
	 */
.Ltail:
	testl   %ecx, %ecx
	jz      .Lfence
.Ltail1:
	movl    %eax, (%edi)
	addl    $4, %edi
	subl    $1, %ecx
	jnz     .Ltail1

.Lfence:
	sfence

.Lret:
	popl    %ebx
	popl    %edi
	popl    %ebp
	ret
