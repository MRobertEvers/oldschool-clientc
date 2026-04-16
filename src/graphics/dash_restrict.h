#ifndef DASH_RESTRICT_H
#define DASH_RESTRICT_H

#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
/* GCC, Clang, and Intel use __restrict__ or __restrict */
#define RESTRICT __restrict__
#elif defined(_MSC_VER)
/* MSVC uses __restrict */
#define RESTRICT __restrict
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* Standard C99 and later */
#define RESTRICT restrict
#else
/* Fallback for older or incompatible compilers */
#define RESTRICT
#endif

#endif
