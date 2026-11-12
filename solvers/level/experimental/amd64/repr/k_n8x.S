/* n8s-bmi2 with link-loop failures accounted for in bulk. */
#define KERNEL cb_amd64_k_n8x
#define CB_BMI2 1
#define CB_BULK 1
#include "g_n8s.inc"
#include "skel.inc"
