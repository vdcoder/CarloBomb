/*
 * topology.c compiled without the 128-bit integer of the compiler, as a
 * compiler that has none compiles it, and the tests of test_topology.c run
 * on that.
 */
#define FRB_NO_INT128 1
#include "../topology.c"

#include "test_topology.c"
