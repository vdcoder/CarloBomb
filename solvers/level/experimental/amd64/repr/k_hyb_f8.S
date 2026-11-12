/* Frame experiment: hyb with 8-byte frame slots (32-bit resume offset + 32-bit saved value). */
#define KERNEL cb_amd64_k_hyb_f8
#define CB_FRAME8 1
#include "g_hyb.inc"
#include "skel.inc"
