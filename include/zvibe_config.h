/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_config.h
 * @brief Build-time configuration: which Z-machine versions are compiled in
 *
 * This is the one place that decides how V3 and V4 coexist. See
 * docs/V4_DESIGN.md for the reasoning.
 *
 * - ZVIBE_ENABLE_V4 = 0 (default): V3 only. Z_IS_V4() is the constant 0, so
 *   every V4 branch is removed by the compiler, and V4-only source files and
 *   table entries are excluded with `#if ZVIBE_ENABLE_V4`. A V3-only build
 *   must stay byte-for-byte as small as before; check with
 *   scripts/core_size.sh.
 * - ZVIBE_ENABLE_V4 = 1: V3 and V4 in one build. The story header selects the
 *   version at load time; Z_IS_V4() reads it.
 *
 * Rules for using these macros:
 * - Use `#if ZVIBE_ENABLE_V4` for anything that must not exist in a V3-only
 *   build: struct members, array dimensions, V4-only files, V4 table entries.
 *   A live function-pointer table keeps every handler it names, so the
 *   optimizer cannot be relied on to drop them.
 * - Use Z_IS_V4() / Z_VSEL() for behavior inside functions that exist in both
 *   builds.
 * - Never use Z_VSEL() for an array dimension: in a dual build it is a runtime
 *   expression. Use a `#if`-selected constant such as Z_MAX_OPERANDS.
 */

#ifndef ZVIBE_CONFIG_H
#define ZVIBE_CONFIG_H

/**
 * @def ZVIBE_ENABLE_V4
 * @brief Compile in Z-machine V4 support (default: 0, V3 only)
 *
 * Override at compile time: -DZVIBE_ENABLE_V4=1
 */
#ifndef ZVIBE_ENABLE_V4
#define ZVIBE_ENABLE_V4 0
#endif

/* Accept only the literal tokens 0 and 1. A plain `#if (X != 0 && X != 1)` would
 * treat a misspelling such as -DZVIBE_ENABLE_V4=yes as 0 (unknown identifiers
 * are 0 in #if) and silently build V3-only. The value is pasted between a
 * prefix and a suffix: only 0 and 1 form a defined name. For `yes` or `01` the
 * name is undefined (0, rejected); for `(1)` the paste is invalid; for `1+0` or
 * `1&&0` the suffix lands on the last token and forms an invalid number, so no
 * expression is silently accepted. */
#define ZVIBE_CFG_PASTE3_(a, b, c) a##b##c
#define ZVIBE_CFG_PASTE3(a, b, c)  ZVIBE_CFG_PASTE3_(a, b, c)
#define ZVIBE_CFG_BOOL_0_END 1
#define ZVIBE_CFG_BOOL_1_END 1
#if !ZVIBE_CFG_PASTE3(ZVIBE_CFG_BOOL_, ZVIBE_ENABLE_V4, _END)
#error "ZVIBE_ENABLE_V4 must be defined as 0 or 1"
#endif

/**
 * @def ZVIBE_STACK_WORDS
 * @brief Z-machine stack size in 16-bit words (default: 512 = 1KB)
 *
 * Enough for typical V3 games (call depth ~10-20). Larger V4 games may need
 * more; override per target: -DZVIBE_STACK_WORDS=1024
 */
#ifndef ZVIBE_STACK_WORDS
#define ZVIBE_STACK_WORDS 512
#endif

#if (ZVIBE_STACK_WORDS < 64)
#error "ZVIBE_STACK_WORDS is too small to run any story"
#endif

/** @brief Largest V3 story file in bytes (Z-machine standard) */
#define Z3_STORY_LIMIT (128 * 1024)
/** @brief Largest V4 story file in bytes (Z-machine standard) */
#define Z4_STORY_LIMIT (256 * 1024)

#if ZVIBE_ENABLE_V4

/**
 * @brief True when the loaded story is V4 (runtime test in a dual build)
 *
 * Internal to the core: needs zvibe.h (for G) and a loaded story context
 * (G non-NULL). Not part of the public API.
 */
#define Z_IS_V4()          (G->header.version == 4)
/** @brief Maximum operands per instruction: call_vs2 takes up to 8 in V4 */
#define Z_MAX_OPERANDS     8
/** @brief Largest story file any enabled version accepts */
#define Z_STORY_MEM_SIZE   Z4_STORY_LIMIT

#else /* V3 only */

#define Z_IS_V4()          0
#define Z_MAX_OPERANDS     4
#define Z_STORY_MEM_SIZE   Z3_STORY_LIMIT

#endif /* ZVIBE_ENABLE_V4 */

/**
 * @brief Pick a value by version: v3 in a V3-only build, runtime choice in a
 *        dual build. Not usable for array dimensions or static initializers.
 */
#define Z_VSEL(v3, v4)     (Z_IS_V4() ? (v4) : (v3))

#endif /* ZVIBE_CONFIG_H */
