/* impl.c.mpmss: MPM STRESS TEST
 *
 * $HopeName: MMsrc!mpmss.c(MMdevel_sw_eq.4) $
 * Copyright (C) 1997.  Harlequin Group plc.  All rights reserved.
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "poolmfs.h"
#include "poolmv.h"
#include "mps.h"
#include "mpslib.h"


#define TEST_ARENA_SIZE         ((Size)16<<20)
#define TEST_SET_SIZE           500
#define TEST_LOOPS              10


static mps_res_t stress(mps_class_t class, mps_space_t space,
                        size_t (*size)(int i), ...)
{
  mps_res_t res;
  mps_pool_t pool;
  va_list arg;
  int i, k;
  int *ps[TEST_SET_SIZE];
  size_t ss[TEST_SET_SIZE];

  va_start(arg, size);
  res = mps_pool_create_v(&pool, class, space, arg);
  va_end(arg);
  if(res != MPS_RES_OK) return res;

  for(i=0; i<TEST_SET_SIZE; ++i) {
    ss[i] = (*size)(i);

    res = mps_alloc((mps_addr_t *)&ps[i], pool, ss[i]);
    if(res != MPS_RES_OK) return res;
    *ps[i] = 1; /* Write something, so it gets swap. */

    if(i && i%4 == 0) putchar('\n');
    printf("%8lX %6lX ", (unsigned long)ps[i], (unsigned long)ss[i]);
  }
  putchar('\n');

  for (k=0; k<TEST_LOOPS; ++k) {
    /* shuffle all the objects */
    for(i=0; i<TEST_SET_SIZE; ++i) {
      int j = rand()%(TEST_SET_SIZE-i);
      void *tp;
      size_t ts;
      
      tp = ps[j]; ts = ss[j];
      ps[j] = ps[i]; ss[j] = ss[i];
      ps[i] = tp; ss[i] = ts;
    }
    /* free half of the objects */
    /* upper half, as when allocaating them again we want smaller objects */
    /* see randomSize() */
    for(i=TEST_SET_SIZE/2; i<TEST_SET_SIZE; ++i) {
      mps_free(pool, (mps_addr_t)ps[i], ss[i]);
      /*    if(i == TEST_SET_SIZE/2)
	    PoolDescribe((Pool)pool, mps_lib_stdout); */
    }
    /* allocate some new objects */
    for(i=TEST_SET_SIZE/2; i<TEST_SET_SIZE; ++i) {
      ss[i] = (*size)(i);
      res = mps_alloc((mps_addr_t *)&ps[i], pool, ss[i]);
      if(res != MPS_RES_OK) return res;
      
      if(i && i%4==0) putchar('\n');
      printf("%8lX %6lX ", (unsigned long)ps[i], (unsigned long)ss[i]);
    }
    putchar('\n');
  }

  mps_pool_destroy(pool);

  return MPS_RES_OK;
}


static size_t randomSize(int i)
{
  return (rand() % 1000)+1;
  (void)i;
}


static size_t fixedSizeSize = 0;

static size_t fixedSize(int i)
{
  return fixedSizeSize;
  (void)i;
}


static void die(mps_res_t res, const char *s)
{
  if(res != MPS_RES_OK) {
    fprintf(stderr, "%s: %d\n", s, res);
    exit(1);
  }
}


int main(void)
{
  mps_space_t space;
  void *block;

  block = malloc(TEST_ARENA_SIZE);
  die(block == NULL ? ResFAIL : ResOK, "malloc");
  die(mps_space_create_wmem(&space, (Addr)block, TEST_ARENA_SIZE), "SpaceInit");

  die(stress((mps_class_t)PoolClassMV(),
             space, randomSize, (size_t)65536,
             (size_t)32, (size_t)65536),
      "stress MV");
  fixedSizeSize = 13;
  die(stress((mps_class_t)PoolClassMFS(),
             space, fixedSize, (size_t)100000, fixedSizeSize),
      "stress MFS");

  mps_space_destroy(space);

  return 0;
}
