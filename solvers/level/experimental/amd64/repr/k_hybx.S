/* hyb with BMI2, and certain CTRL link-loop failures accounted for in bulk. */
#define KERNEL cb_amd64_k_hybx
#define CB_BMI2 1
#define CB_BULK 1
#include "g_hyb.inc"
#include "skel.inc"
