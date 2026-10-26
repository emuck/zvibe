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

/**
 * @def ZVIBE_VERIFY_SAVE_IDENTITY
 * @brief Refuse to restore a raw save made from a different story, and refresh the
 *        interpreter-owned header fields after a restore
 *
 * On by default for hosted builds (the console loads saves from files, where a
 * save from another game is easy to pick) and for every V4 build (a V3 save
 * restored into a V4 story would be decoded wrongly). Off by default for
 * freestanding V3-only firmware, where each game's saves live in its own slot
 * and flash is tight. Override with -DZVIBE_VERIFY_SAVE_IDENTITY=0|1.
 */
#ifndef ZVIBE_VERIFY_SAVE_IDENTITY
#if ZVIBE_ENABLE_V4 || !(ZVIBE_FREESTANDING || defined(ZVIBE_MINIMAL_FEATURES))
#define ZVIBE_VERIFY_SAVE_IDENTITY 1
#else
#define ZVIBE_VERIFY_SAVE_IDENTITY 0
#endif
#endif

/** @brief Largest V3 story file in bytes (Z-machine standard) */
#define Z3_STORY_LIMIT (128 * 1024)
/** @brief Largest V4 story file in bytes (Z-machine standard) */
#define Z4_STORY_LIMIT (256 * 1024)

/** @brief True if story version v can be run by this build (V3 always; V4 when enabled) */
#define Z_VERSION_SUPPORTED(v)  ((v) == 3 || (ZVIBE_ENABLE_V4 && (v) == 4))

#if ZVIBE_ENABLE_V4

/**
 * @def ZVIBE_INTERPRETER_NUMBER
 * @brief Interpreter number written to the V4 header (byte 0x1E, standard section 11.1.3)
 *
 * 1 is "DECSystem-20", the plain-terminal machine type; stories use this number
 * to choose conventions, so a text terminal should not claim to be a graphics
 * machine. The version letter goes in byte 0x1F.
 */
#ifndef ZVIBE_INTERPRETER_NUMBER
#define ZVIBE_INTERPRETER_NUMBER 1
#endif
#ifndef ZVIBE_INTERPRETER_VERSION
#define ZVIBE_INTERPRETER_VERSION 'Z'
#endif
/** @brief Screen size reported in the V4 header (bytes 0x20 height, 0x21 width) */
#ifndef ZVIBE_SCREEN_ROWS
#define ZVIBE_SCREEN_ROWS 24
#endif
#ifndef ZVIBE_SCREEN_COLS
#define ZVIBE_SCREEN_COLS 80
#endif

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
 * @def Z_VERSION_HELPER
 * @brief Declaration prefix for the tiny helpers that select between V3 and V4
 *
 * They exist so that version-dependent code is written once. In a V3-only
 * build each one must compile to exactly the original V3 expression, at every
 * optimization level, so on GCC-family compilers (GCC, XC32, the RISC-V
 * toolchain, Clang) they are forced inline. Without that, -O0 builds call a
 * real function for every object field access.
 */
#if defined(__GNUC__)
#define Z_VERSION_HELPER static inline __attribute__((always_inline))
#else
#define Z_VERSION_HELPER static inline
#endif

/**
 * @brief Pick a value by version: v3 in a V3-only build, runtime choice in a
 *        dual build. Not usable for array dimensions or static initializers.
 */
#define Z_VSEL(v3, v4)     (Z_IS_V4() ? (v4) : (v3))

#endif /* ZVIBE_CONFIG_H */
