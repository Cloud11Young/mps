/* impl.c.mpsioan: HARLEQUIN MEMORY POOL SYSTEM I/O IMPLEMENTATION (ANSI)
 *
 * $HopeName: MMsrc!mpsioan.c(MMdevel_event.1) $
 * Copyright (C) 1996 Harlequin Group, all rights reserved.
 *
 * .readership: MPS developers.
 *
 * TRANSGRESSIONS (rule.impl.trans)
 *
 * There's no way this meets all the reqiurements yet.
 */

#include "mpsio.h"
#include <stdio.h>

#include "mpstd.h"		/* .sunos.warn */
#ifdef MPS_OS_SU
#include "ossu.h"
#endif

mps_res_t mps_io_create(mps_io_t *mps_io_r)
{
  FILE *f;
  
  f = fopen("mpsio.log", "wb");
  if(f == NULL)
    return MPS_RES_IO;
  
  *mps_io_r = (mps_io_t)f;
  return MPS_RES_OK;
}

void mps_io_destroy(mps_io_t mps_io)
{
  FILE *f = (FILE *)mps_io;

  (void)fclose(f);
}

mps_res_t mps_io_flush(mps_io_t mps_io, void *mps_buf, size_t mps_size)
{
  FILE *f = (FILE *)mps_io;
  size_t n;

  n = fwrite(mps_buf, mps_size, 1, f);
  if(n != 1)
    return MPS_RES_IO;
  
  return MPS_RES_OK;
}
