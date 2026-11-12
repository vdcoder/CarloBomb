/* Benchmark only: hybx with search-accounting counters. */
#define KERNEL cb_amd64_k_hybx_count
#define CB_BMI2 1
#define CB_BULK 1
#define CB_COUNT 1
#include "g_hyb.inc"
#include "skel.inc"
