/* Benchmark only: n8x with search-accounting counters. */
#define KERNEL cb_amd64_k_n8x_count
#define CB_BMI2 1
#define CB_BULK 1
#define CB_COUNT 1
#include "g_n8s.inc"
#include "skel.inc"
