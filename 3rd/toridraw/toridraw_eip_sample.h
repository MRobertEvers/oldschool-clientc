#ifndef TORIDRAW_EIP_SAMPLE_H
#define TORIDRAW_EIP_SAMPLE_H

/**
 * Where does the frame actually go?
 *
 * Three hand-written SSE2 kernels have now come back neutral on the XP box,
 * and the census explains why: gouraud is 82.5% of faces and 71% of drawn
 * area, the texture asm covers 89.8% of textured area, the per-triangle
 * prologue is 1.5% of the frame -- and the frame is 37.7 ms. The rasterizer
 * is not where the time is. Every kernel chosen so far was chosen by
 * reasoning about the rasterizer because the rasterizer is the part with
 * counters in it, which is a lamppost, not a measurement.
 *
 * This is the flashlight. A second thread suspends the render thread about a
 * thousand times a second, reads EIP, and resumes it. No instrumentation in
 * the measured code at all -- which matters here more than usual, because
 * TORIRS_PERF=1 costs 69% of this box's frame and therefore cannot answer any
 * question about the frame's composition.
 *
 * Attribution is deliberately split in two:
 *
 *   - Samples inside the executable's own .text land in a direct-mapped
 *     histogram at 16-byte granularity. That is fine enough to separate
 *     adjacent functions and coarse enough that the table is a couple of MB.
 *     Offsets are dumped from the module base, and resolved off-box against
 *     `nm torirs.exe` -- XP does not relocate the main image, so the linked
 *     addresses and the runtime addresses are the same numbers.
 *
 *   - Samples anywhere else -- SDL, the CRT, gdi32, the kernel -- are bucketed
 *     by 64 KB page into a small linear table. That is exactly enough
 *     resolution to name the DLL responsible, which is the only question worth
 *     asking about them at this stage.
 *
 * Both halves are pre-allocated before sampling starts. The sampler thread
 * must never allocate or take a lock while the target is suspended: the
 * target may be holding the CRT heap lock at that instant, and a sampler that
 * waits for it deadlocks the process.
 *
 * Off by default. TORIDRAW_EIP_SAMPLE=1 turns it on;
 * TORIDRAW_EIP_SAMPLE_FILE=<path> redirects the dump.
 */

/** Begin sampling the calling thread. No-op unless TORIDRAW_EIP_SAMPLE=1. */
void ToriDraw_EipSampleStart(void);

/**
 * Stop sampling and write the histogram. `label` names the run in the dump
 * and supplies the default filename; it is not optional.
 */
void ToriDraw_EipSampleStop(const char* label);

#endif /* TORIDRAW_EIP_SAMPLE_H */
