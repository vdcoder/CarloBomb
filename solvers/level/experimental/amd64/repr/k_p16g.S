/* Experiment B, literal layout: four 16-bit packed nodes in one GPR. */
#define KERNEL cb_amd64_k_p16g
#define CB_P16_GPR 1
#include "g_p16.inc"
#include "skel.inc"
