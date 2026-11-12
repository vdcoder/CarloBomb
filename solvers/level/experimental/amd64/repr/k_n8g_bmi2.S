/* Whole graph in registers (budgets up to 8), BMI1/BMI2 shifts. */
#define KERNEL cb_amd64_k_n8g_bmi2
#define CB_BMI2 1
#include "g_n8g.inc"
#include "skel.inc"
