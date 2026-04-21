#!/usr/bin/env python3
"""Extract fused projection chunks from projection_simd.u.c and adapt for projection16_simd (int16 vertices)."""
from __future__ import annotations

import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SIMD = (ROOT / "src/graphics/projection_simd.u.c").read_text().splitlines(True)


def slice_lines(start_predicate, end_predicate):
    start = next(i for i, line in enumerate(SIMD) if start_predicate(line))
    end = next(i for i, line in enumerate(SIMD) if i > start and end_predicate(line))
    return "".join(SIMD[start:end])


def transform_project16(text: str) -> str:
    text = text.replace("int* vertex_x,", "vertexint_t* vertex_x,")
    text = text.replace("int* vertex_y,", "vertexint_t* vertex_y,")
    text = text.replace("int* vertex_z,", "vertexint_t* vertex_z,")
    # NEON loads
    text = text.replace(
        "int32x4_t xv4 = vld1q_s32(&vertex_x[i]);",
        "int32x4_t xv4 = vmovl_s16(vld1_s16(&vertex_x[i]));",
    )
    text = text.replace(
        "int32x4_t yv4 = vld1q_s32(&vertex_y[i]);",
        "int32x4_t yv4 = vmovl_s16(vld1_s16(&vertex_y[i]));",
    )
    text = text.replace(
        "int32x4_t zv4 = vld1q_s32(&vertex_z[i]);",
        "int32x4_t zv4 = vmovl_s16(vld1_s16(&vertex_z[i]));",
    )
    # Scalar tails: int32 uses >>15 + SCALE_UNIT; int16 dense uses >>6 after mul
    old_tail = (
        "        int screen_x = (x_scene * cot_fov_half_ish15) >> 15;\n"
        "        int screen_y = (y_scene * cot_fov_half_ish15) >> 15;\n"
        "        screen_x = SCALE_UNIT(screen_x);\n"
        "        screen_y = SCALE_UNIT(screen_y);\n"
    )
    new_tail = (
        "        int screen_x = x_scene * cot_fov_half_ish15;\n"
        "        int screen_y = y_scene * cot_fov_half_ish15;\n"
        "        screen_x >>= 6;\n"
        "        screen_y >>= 6;\n"
    )
    text = text.replace(old_tail, new_tail)
    # AVX2 loads (8-wide int32 -> widen int16)
    text = text.replace(
        "__m256i xv8 = _mm256_loadu_si256((__m256i*)&vertex_x[i]);",
        "__m256i xv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_x[i]));",
    )
    text = text.replace(
        "__m256i yv8 = _mm256_loadu_si256((__m256i*)&vertex_y[i]);",
        "__m256i yv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_y[i]));",
    )
    text = text.replace(
        "__m256i zv8 = _mm256_loadu_si256((__m256i*)&vertex_z[i]);",
        "__m256i zv8 = _mm256_cvtepi16_epi32(_mm_loadu_si128((__m128i*)&vertex_z[i]));",
    )
    # SSE int loads
    text = text.replace(
        "__m128i xv4 = _mm_loadu_si128((__m128i*)&vertex_x[i]);",
        "__m128i xv4 = proj_cvtepi16_epi32_lo64(_mm_loadl_epi64((__m128i*)&vertex_x[i]));",
    )
    text = text.replace(
        "__m128i yv4 = _mm_loadu_si128((__m128i*)&vertex_y[i]);",
        "__m128i yv4 = proj_cvtepi16_epi32_lo64(_mm_loadl_epi64((__m128i*)&vertex_y[i]));",
    )
    text = text.replace(
        "__m128i zv4 = _mm_loadu_si128((__m128i*)&vertex_z[i]);",
        "__m128i zv4 = proj_cvtepi16_epi32_lo64(_mm_loadl_epi64((__m128i*)&vertex_z[i]));",
    )
    return text


def main():
    neon = slice_lines(
        lambda l: "/* Fused: ortho + FOV" in l,
        lambda l: l.startswith("#elif defined(__AVX2__)"),
    )
    avx = slice_lines(
        lambda l: "/* ---- Fused AVX2:" in l,
        lambda l: l.startswith("#elif (defined(__SSE2__)"),
    )
    sse = slice_lines(
        lambda l: "/* ---- Fused SSE2/SSE4.1:" in l,
        lambda l: l.startswith("#elif defined(__SSE__)"),
    )
    sse_float = slice_lines(
        lambda l: "/* Fused SSE1 float path" in l,
        lambda l: l.startswith("#else // __SSE_"),
    )

    out_dir = ROOT / "src/graphics"
    (out_dir / "_projection16_fused_neon.gen.c").write_text(transform_project16(neon))
    (out_dir / "_projection16_fused_avx.gen.c").write_text(transform_project16(avx))
    (out_dir / "_projection16_fused_sse.gen.c").write_text(transform_project16(sse))
    (out_dir / "_projection16_fused_sse_float.gen.c").write_text(transform_project16(sse_float))
    print("Wrote gen fragments to src/graphics/_projection16_fused_*.gen.c")


if __name__ == "__main__":
    main()
