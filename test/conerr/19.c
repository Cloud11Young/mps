/* 
TEST_HEADER
 id = $Id$
 summary = alloc in an uncreated pool
 language = c
 link = testlib.o
OUTPUT_SPEC
 abort = true
END_HEADER
*/

#include "testlib.h"
#include "mpscmv.h"

static void test(void)
{
 mps_arena_t arena;
 mps_pool_t pool = (mps_pool_t)1;

 mps_addr_t obj;

 cdie(mps_arena_create(&arena, mps_arena_class_vm(), mmqaArenaSIZE), "create arena");

/*
 cdie(
  mps_pool_create(&pool, arena, mps_class_mv(),
   extendBy, avgSize, maxSize),
  "create pool");
*/

 cdie(mps_alloc(&obj, pool, 152), "allocate");

 mps_pool_destroy(pool);
 comment("Destroyed pool");

 mps_arena_destroy(arena);
 comment("Destroyed arena.");
}

int main(void)
{
 easy_tramp(test);
 return 0;
}
