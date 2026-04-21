#!/usr/bin/env python3
"""
Split projection_simd.u.c / projection16_simd.u.c into arch-specific *_*.u.c files.
Run from repo root: python3 scripts/split_projection_simd_arch.py
"""
from pathlib import Path


def write_text(path: Path, text: str) -> None:
    path.write_text(text)
    print("Wrote", path)


def split_one(
    src: Path,
    prefix: str,
    guard: str,
    *,
    neon: tuple[int, int],  # inclusive 1-based
    avx: tuple[int, int],
    sse_kernel: tuple[int, int],  # inclusive, int SSE path body (first static inline through closing })
    sse_float: tuple[int, int],  # inclusive
    scalar: tuple[int, int],  # inclusive (includes #else line, #endif closes chain)
    sixdof: tuple[int, int],  # inclusive 6DOF section (before final #endif of TU)
):
    lines = src.read_text().splitlines(keepends=True)

    def slc(a: int, b: int) -> str:
        """Extract 1-based lines [a, b] inclusive."""
        return "".join(lines[a - 1 : b])

    # --- NEON: body still starts with #if NEON; needs one #endif before outer guard close ---
    neon_body = slc(*neon).rstrip() + "\n"
    neon_txt = (
        f"#ifndef {guard}_NEON_U_C\n"
        f"#define {guard}_NEON_U_C\n\n"
        f"{neon_body}\n"
        "#endif\n\n"
        f"#endif /* {guard}_NEON_U_C */\n"
    )
    write_text(Path(f"src/graphics/{prefix}.neon.u.c"), neon_txt)

    # --- AVX ---
    avx_lines = lines[avx[0] - 1 : avx[1]]
    avx_lines[0] = "#if defined(__AVX2__) && !defined(AVX2_DISABLED)\n"
    avx_txt = (
        f"#ifndef {guard}_AVX_U_C\n"
        f"#define {guard}_AVX_U_C\n\n"
        + "".join(avx_lines).rstrip()
        + "\n#endif\n\n"
        f"#endif /* {guard}_AVX_U_C */\n"
    )
    write_text(Path(f"src/graphics/{prefix}.avx.u.c"), avx_txt)

    # --- SSE41 / SSE2 ---
    sse_k = slc(*sse_kernel).rstrip() + "\n"
    sse41_txt = (
        f"#ifndef {guard}_SSE41_U_C\n"
        f"#define {guard}_SSE41_U_C\n\n"
        "#if defined(__SSE4_1__) && !defined(SSE2_DISABLED)\n"
        '#include "sse2_41compat.h"\n'
        '#include "projection_zdiv_simd.sse41.u.c"\n'
        "#define PROJECTION_ZDIV_SSE_APPLY projection_zdiv_sse41_apply\n\n"
        f"{sse_k}"
        "#endif\n\n"
        f"#endif /* {guard}_SSE41_U_C */\n"
    )
    write_text(Path(f"src/graphics/{prefix}.sse41.u.c"), sse41_txt)

    sse2_txt = (
        f"#ifndef {guard}_SSE2_U_C\n"
        f"#define {guard}_SSE2_U_C\n\n"
        "#if defined(__SSE2__) && !defined(__SSE4_1__) && !defined(SSE2_DISABLED)\n"
        '#include "sse2_41compat.h"\n'
        '#include "projection_zdiv_simd.sse2.u.c"\n'
        "#define PROJECTION_ZDIV_SSE_APPLY projection_zdiv_sse2_apply\n\n"
        f"{sse_k}"
        "#endif\n\n"
        f"#endif /* {guard}_SSE2_U_C */\n"
    )
    write_text(Path(f"src/graphics/{prefix}.sse2.u.c"), sse2_txt)

    # --- SSE float ---
    # sse_float[1] is exclusive slice end index (same convention as slice end after last AVX/SSE float line)
    sf_lines = lines[sse_float[0] - 1 : sse_float[1]]
    sf_lines[0] = "#if defined(__SSE__) && !defined(SSE_DISABLED)\n"
    sse_float_txt = (
        f"#ifndef {guard}_SSE_FLOAT_U_C\n"
        f"#define {guard}_SSE_FLOAT_U_C\n\n"
        + "".join(sf_lines).rstrip()
        + "\n#endif\n\n"
        f"#endif /* {guard}_SSE_FLOAT_U_C */\n"
    )
    write_text(Path(f"src/graphics/{prefix}.sse_float.u.c"), sse_float_txt)

    # --- Scalar: drop outer #else line; omit chain-closing #endif ---
    assert lines[scalar[0] - 1].strip().startswith("#else")
    # Line after #else through line before SIMD-chain #endif  (exclusive end index = scalar[1]-1)
    sc_body = lines[scalar[0] : scalar[1] - 1]
    scalar_txt = (
        f"#ifndef {guard}_SCALAR_U_C\n"
        f"#define {guard}_SCALAR_U_C\n\n"
        + "".join(sc_body)
        + f"#endif /* {guard}_SCALAR_U_C */\n"
    )
    write_text(Path(f"src/graphics/{prefix}.scalar.u.c"), scalar_txt)

    # --- Dispatcher + 6DOF ---
    preamble = "".join(lines[0 : neon[0] - 1])
    sixdof = slc(*sixdof).rstrip() + "\n"

    dispatcher = preamble
    dispatcher += "#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)\n"
    dispatcher += f'#include "{prefix}.neon.u.c"\n'
    dispatcher += "#elif defined(__AVX2__) && !defined(AVX2_DISABLED)\n"
    dispatcher += f'#include "{prefix}.avx.u.c"\n'
    dispatcher += "#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)\n"
    dispatcher += f'#include "{prefix}.sse41.u.c"\n'
    dispatcher += "#elif defined(__SSE2__) && !defined(SSE2_DISABLED)\n"
    dispatcher += f'#include "{prefix}.sse2.u.c"\n'
    dispatcher += "#elif defined(__SSE__) && !defined(SSE_DISABLED)\n"
    dispatcher += f'#include "{prefix}.sse_float.u.c"\n'
    dispatcher += "#else\n"
    dispatcher += f'#include "{prefix}.scalar.u.c"\n'
    dispatcher += "#endif\n\n"
    dispatcher += sixdof
    dispatcher += "#endif\n"

    src.write_text(dispatcher)


def main():
    split_one(
        Path("src/graphics/projection_simd.u.c"),
        "projection_simd",
        "PROJECTION_SIMD",
        neon=(18, 1346),
        avx=(1349, 2991),
        sse_kernel=(3002, 4499),
        sse_float=(4501, 6307),
        scalar=(6310, 6629),
        sixdof=(6631, 6878),
    )

    split_one(
        Path("src/graphics/projection16_simd.u.c"),
        "projection16_simd",
        "PROJECTION16_SIMD",
        neon=(20, 1338),
        avx=(1339, 2972),
        sse_kernel=(2983, 4469),
        sse_float=(4471, 6275),
        scalar=(6280, 6599),
        sixdof=(6601, 6847),
    )

    print("Done.")


if __name__ == "__main__":
    main()
