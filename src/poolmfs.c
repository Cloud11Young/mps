/* impl.c.poolmfs: MANUAL FIXED SMALL UNIT POOL
 *
 * $HopeName: MMsrc!poolmfs.c(MMdevel_assertid.3) $
 * Copyright (C) 1997 The Harlequin Group Limited.  All rights reserved.
 *
 * This is the implementation of the MFS pool class.
 *
 * DESIGN
 *
 * .design.misplaced: This design is misplaced, it should be in a
 * separate document.
 *
 * MFS operates in a very simple manner: each segment is divided into
 * units.  Free units are kept on a linked list using a header stored
 * in the unit itself.  The linked list is not ordered; allocation and
 * deallocation simply pop and push from the head of the list.  This is
 * fast, but successive allocations might have poor locality if
 * previous successive frees did.
 *
 * .restriction: This pool cannot allocate from the space control
 * pool (as the control pool is an instance of PoolClassMV and MV uses
 * MFS in its implementation), nor can it allocate sub-pools, as that
 * causes allocation in the control pool.
 *
 * Notes
 *
 * .freelist.fragments: The simple freelist policy might lead to poor
 * locality of allocation if the list gets fragmented.
 * 
 * .buffer.not: This pool doesn't support fast cache allocation, which
 * is a shame.
 */


#include "mpm.h"
#include "poolmfs.h"

SRCID(poolmfs, "$HopeName: MMsrc!poolmfs.c(MMdevel_assertid.3) $");


/*  == Round up ==
 *
 *  Rounds n up to the nearest multiple of unit.
 */

#define ROUND(unit, n)  ((n)+(unit)-1 - ((n)+(unit)-1)%(unit))

#define PoolPoolMFS(pool)       PARENT(MFSStruct, poolStruct, pool)


/*  == Free List Structure ==
 */

typedef struct MFSHeaderStruct {
  struct MFSHeaderStruct *next;
} HeaderStruct, *Header;



#define UNIT_MIN        sizeof(HeaderStruct)

MFSInfo MFSGetInfo(void)
{
  static const struct MFSInfoStruct info =
  {
    /* unitSizeMin */   UNIT_MIN
  };
  return &info;
}


Pool (MFSPool)(MFS mfs)
{
  AVERT(0xB3F50000, MFS, mfs);
  return &mfs->poolStruct;
}


static Res MFSInit(Pool pool, va_list arg)
{
  Size extendBy, unitSize;
  MFS mfs;
  Space space;

  AVER(0xB3F50001, pool != NULL);

  extendBy = va_arg(arg, Size);
  unitSize = va_arg(arg, Size);

  AVER(0xB3F50002, unitSize >= UNIT_MIN);
  AVER(0xB3F50003, extendBy >= unitSize);
  
  mfs = PoolPoolMFS(pool);
  space = PoolSpace(pool);

  mfs->unroundedUnitSize = unitSize;

  unitSize = SizeAlignUp(unitSize, MPS_PF_ALIGN);
  extendBy = SizeAlignUp(extendBy, ArenaAlign(space));

  mfs->extendBy = extendBy;
  mfs->unitSize = unitSize;
  mfs->unitsPerSeg = extendBy/unitSize;
  mfs->freeList = NULL;
  mfs->segList = (Seg)0;
  mfs->sig = MFSSig;

  AVERT(0xB3F50004, MFS, mfs);

  return ResOK;
}


static void MFSFinish(Pool pool)
{
  Seg seg;
  MFS mfs;

  AVERT(0xB3F50005, Pool, pool);
  mfs = PoolPoolMFS(pool);
  AVERT(0xB3F50006, MFS, mfs);

  seg = mfs->segList;
  while(seg != NULL) {
    Seg nextSeg = (Seg)seg->p;   /* .seg.chain */
    PoolSegFree(pool, seg);
    seg = nextSeg;
  }

  mfs->sig = SigInvalid;
}


/*  == Allocate ==
 *
 *  Allocation simply involves taking a unit from the front of the freelist
 *  and returning it.  If there are none, a new segment is allocated.
 */

static Res MFSAlloc(Addr *pReturn, Pool pool, Size size)
{
  Header f;
  Res res;
  MFS mfs;

  AVERT(0xB3F50007, Pool, pool);
  mfs = PoolPoolMFS(pool);
  AVERT(0xB3F50008, MFS, mfs);

  AVER(0xB3F50009, pReturn != NULL);
  AVER(0xB3F5000A, size == mfs->unroundedUnitSize);

  f = mfs->freeList;

  /* If the free list is empty then extend the pool with a new segment. */

  if(f == NULL)
  {
    Seg seg;
    Word i, unitsPerSeg;
    Size unitSize;
    Addr base;
    Header header = NULL, next;
    Space space;

    space = PoolSpace(pool);

    /* Create a new segment and attach it to the pool. */
    res = PoolSegAlloc(&seg, SegPrefDefault(), pool, mfs->extendBy);
    if(res != ResOK)
      return res;

    /* .seg.chain: chain segs through seg->p */
    seg->p = (void *)mfs->segList;
    mfs->segList = seg;

    /* Sew together all the new empty units in the segment, working down */
    /* from the top so that they are in ascending order of address on the */
    /* free list. */

    unitsPerSeg = mfs->unitsPerSeg;
    unitSize = mfs->unitSize;
    base = SegBase(space, seg);
    next = NULL;

#define SUB(b, s, i)    ((Header)AddrAdd(b, (s)*(i)))

    for(i=0; i<unitsPerSeg; ++i)
    {
      header = SUB(base, unitSize, unitsPerSeg-i - 1);
      AVER(0xB3F5000B, AddrIsAligned(header, pool->alignment));
      AVER(0xB3F5000C, AddrAdd((Addr)header, unitSize) <= SegLimit(space, seg));
      header->next = next;
      next = header;
    }

#undef SUB

    /* The first unit in the segment is now the head of the new free list. */
    f = header;
  }

  AVER(0xB3F5000D, f != NULL);

  /* Detach the first free unit from the free list and return its address. */

  mfs->freeList = f->next;

  *pReturn = (Addr)f;
  return ResOK;
}


/*  == Free ==
 *
 *  Freeing a unit simply involves pushing it onto the front of the
 *  freelist.
 */

static void MFSFree(Pool pool, Addr old, Size size)
{
  Header h;
  MFS mfs;

  AVERT(0xB3F5000E, Pool, pool);
  mfs = PoolPoolMFS(pool);
  AVERT(0xB3F5000F, MFS, mfs);

  AVER(0xB3F50010, old != (Addr)0);
  AVER(0xB3F50011, size == mfs->unroundedUnitSize);

  /* .freelist.fragments */
  h = (Header)old;
  h->next = mfs->freeList;
  mfs->freeList = h;
}


static Res MFSDescribe(Pool pool, mps_lib_FILE *stream)
{
  MFS mfs;
  Res res;

  AVERT(0xB3F50012, Pool, pool);
  mfs = PoolPoolMFS(pool);
  AVERT(0xB3F50013, MFS, mfs);

  AVER(0xB3F50014, stream != NULL);

  res = WriteF(stream,
               "  unrounded unit size $W\n", (WriteFW)mfs->unroundedUnitSize,
               "  unit size $W\n",           (WriteFW)mfs->unitSize,
               "  segment size $W\n",        (WriteFW)mfs->extendBy,
               "  units per segment $U\n",   (WriteFU)mfs->unitsPerSeg,
               "  free list begins at $P\n", (WriteFP)mfs->freeList,
               "  seg list begin at $P\n",   (WriteFP)mfs->segList,
               NULL);
  if(res != ResOK) return res;

  return ResOK;
}


static PoolClassStruct PoolClassMFSStruct = {
  PoolClassSig,
  "MFS",                                /* name */
  sizeof(MFSStruct),                    /* size */
  offsetof(MFSStruct, poolStruct),      /* offset */
  AttrALLOC | AttrFREE,                 /* attr */
  MFSInit,                              /* init */
  MFSFinish,                            /* finish */
  MFSAlloc,                             /* alloc */
  MFSFree,                              /* free */
  PoolNoBufferInit,                     /* bufferInit */
  PoolNoBufferFill,                     /* bufferFill */
  PoolNoBufferEmpty,                    /* bufferEmpty */
  PoolNoBufferFinish,                   /* bufferFinish */
  PoolNoCondemn,                        /* condemn */
  PoolNoGrey,                           /* grey */
  PoolNoScan,                           /* scan */
  PoolNoFix,                            /* fix */
  PoolNoReclaim,                        /* reclaim */
  MFSDescribe,                          /* describe */
  PoolClassSig                          /* impl.h.mpmst.class.end-sig */
};


PoolClass PoolClassMFS(void)
{
  return &PoolClassMFSStruct;
}

Bool MFSCheck(MFS mfs)
{
  Space space;

  CHECKS(0xB3F50015, MFS, mfs);
  CHECKD(0xB3F50016, Pool, &mfs->poolStruct);
  CHECKL(0xB3F50017, mfs->poolStruct.class == &PoolClassMFSStruct);
  CHECKL(0xB3F50018, mfs->unroundedUnitSize >= UNIT_MIN);
  CHECKL(0xB3F50019, mfs->extendBy >= UNIT_MIN);
  space = PoolSpace(&mfs->poolStruct);
  CHECKL(0xB3F5001A, SizeIsAligned(mfs->extendBy, ArenaAlign(space)));
  CHECKL(0xB3F5001B, SizeAlignUp(mfs->unroundedUnitSize, mfs->poolStruct.alignment) ==
         mfs->unitSize);
  CHECKL(0xB3F5001C, mfs->unitsPerSeg == mfs->extendBy/mfs->unitSize);
  if(mfs->freeList != NULL) {
    /* free list is stored in the pool's managed memory */
    CHECKL(0xB3F5001D, PoolHasAddr(&mfs->poolStruct, (Addr)mfs->freeList));
  }
  if(mfs->segList != (Seg)0) {
    CHECKD(0xB3F5001E, Seg, mfs->segList);
  }
  return TRUE;
}
