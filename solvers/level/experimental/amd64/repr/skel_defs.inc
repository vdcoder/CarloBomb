/*
 * Constants shared by skel.inc and the g_*.inc representation files.
 * (The C preprocessor expands names where a macro body is written, so
 * anything a representation uses must be defined before it is included.)
 */
#ifndef CARLOBOMB_SKEL_DEFS_INC
#define CARLOBOMB_SKEL_DEFS_INC

#include "asm_layout.h"

#ifdef CB_FRAME8
#define FRAME 8
#define FRAME_SHIFT 3
#else
#define FRAME 16
#define FRAME_SHIFT 4
#endif

#define L_LIMIT  qword ptr [rsp]
#define L_TEND   qword ptr [rsp + 8]
#define L_MAX    dword ptr [rsp + 16]
#define L_STATE  qword ptr [rsp + 24]
#define LOCALS   32

/* A bulk skip (CB_BULK) counts 3 steps per node at once, at most 3 * 64.
 * Below this many remaining steps the per-node code is used, so that a step
 * limit is always met at exactly the same step. */
#define CB_BULK_MARGIN 256

#endif
