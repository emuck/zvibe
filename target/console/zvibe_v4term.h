/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * zvibe_v4term.h - Full-screen terminal play of version 4 stories (see zvibe_v4term.c)
 */
#ifndef ZVIBE_V4TERM_H
#define ZVIBE_V4TERM_H

#include <stdio.h>

/** 1 if story_file is a version 4 story and stdin and stdout are terminals */
int zvibe_v4term_wants(const char *story_file);

/**
 * Play the story full-screen until it ends. seed seeds the random number
 * generator; save_file is where save and restore go; the two FILEs (either may
 * be NULL) receive the story's text and the command lines typed.
 * @return a process exit status
 */
int zvibe_v4term_play(const char *story_file, int seed, const char *save_file, FILE *transcript, FILE *commands);

#endif
