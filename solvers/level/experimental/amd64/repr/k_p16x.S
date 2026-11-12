/* Experiment C: eight 16-bit packed nodes in an XMM register (AVX-512BW/VL). */
#define KERNEL cb_amd64_k_p16x
#define CB_P16_XMM 1
#include "g_p16.inc"
#include "skel.inc"
