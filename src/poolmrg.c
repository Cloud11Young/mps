/* impl.c.poolmrg
 * 
 * MANUAL RANK GUARDIAN POOL
 * 
 * $HopeName: MMsrc!poolmrg.c(MMdevel_assertid.3) $
 * Copyright (C) 1997 The Harlequin Group Limited.  All rights reserved.
 *
 * READERSHIP
 *
 * Any MPS developer.  It will help to read design.mps.poolmrg.
 * 
 * DESIGN
 * 
 * See design.mps.poolmrg.
 * 
 * .access.exact: There is no way to to determine the "correct" rank to
 * scan at when an access fault is taken (we should use whatever rank the
 * tracer is at).  We default to scanning at the RankEXACT.
 *
 * .improve.rank: At the moment, the pool is a guardian for the final
 * rank.  It could be generalized to be a guardian for an arbitrary
 * rank (a guardian for RankEXACT would tell you if the object was
 * ambiguously referenced, for example).  The code that would need to be
 * modified bears this tag.
 */


#include "mpm.h"
#include "poolmrg.h"

SRCID(poolmrg, "$HopeName: MMsrc!poolmrg.c(MMdevel_assertid.3) $");


#define MRGSig          ((Sig)0x519369B0) /* SIGnature MRG POol */

typedef struct MRGStruct {
  PoolStruct poolStruct;        /* generic pool structure */
  RingStruct entry;             /* design.mps.poolmrg.poolstruct.entry */
  RingStruct exit;              /* design.mps.poolmrg.poolstruct.exit */
  RingStruct free;              /* design.mps.poolmrg.poolstruct.free */
  RingStruct group;             /* design.mps.poolmrg.poolstruct.group */
  Size extendBy;                /* design.mps.poolmrg.extend */
  Sig sig;                      /* impl.h.mps.sig */
} MRGStruct;

#define PoolPoolMRG(pool) PARENT(MRGStruct, poolStruct, pool)

static Pool MRGPool(MRG mrg);

/* design.mps.poolmrg.guardian.assoc */
static Index indexOfRefPart(Addr a, Space space)
{
  Seg seg;
  Bool b;
  Addr base;
  Addr *pbase, *pa;

  b = SegOfAddr(&seg, space, a);
  AVER(0xB3690000, b);
  base = SegBase(space, seg);
  pbase = (Addr *)base;
  pa = (Addr *)a;
  return pa - pbase;
}

/* design.mps.poolmrg.guardian.assoc */
static Index indexOfLinkPart(Addr a, Space space)
{
  Seg seg;
  Bool b;
  Addr base ;
  RingStruct *pbase, *pa;

  b = SegOfAddr(&seg, space, a);
  AVER(0xB3690001, b);
  base = SegBase(space, seg);
  pbase = (RingStruct *)base;
  pa = (RingStruct *)a;
  return pa - pbase;
}

#define MRGGroupSig     ((Sig)0x5193699b) /* SIGnature MRG GrouP */

typedef struct MRGGroupStruct {
  Sig sig;                      /* impl.h.misc.sig */
  RingStruct group;		/* design.mps.poolmrg.group.group */
  Seg refseg;                   /* design.mps.poolmrg.group.segs */
  Seg linkseg;                  /* design.mps.poolmrg.group.segs */
} MRGGroupStruct;
typedef MRGGroupStruct *MRGGroup;

static void MRGGroupDestroy(MRGGroup group, MRG mrg)
{
  Pool pool;

  pool = MRGPool(mrg);
  RingRemove(&group->group);
  PoolSegFree(pool, group->refseg);
  PoolSegFree(pool, group->linkseg);
  SpaceFree(PoolSpace(pool), (Addr)group, (Size)sizeof(MRGGroupStruct));
}

static Res MRGGroupCreate(MRGGroup *groupReturn, MRG mrg)
{
  Addr base;
  MRGGroup group;
  Pool pool;
  Res res;
  RingStruct *linkpart;
  Seg linkseg;
  Seg refseg;
  Size linksegsize;
  Space space;
  Addr *refpart;
  Word i, guardians;
  void *v;

  pool = MRGPool(mrg);
  space = PoolSpace(pool);
  res = SpaceAlloc(&v, space, (Size)sizeof(MRGGroupStruct));
  if(res != ResOK)
    goto failSpaceAlloc;
  group = v;
  res = PoolSegAlloc(&refseg, SegPrefDefault(), pool, mrg->extendBy);
  if(res != ResOK)
    goto failRefSegAlloc;

  guardians = mrg->extendBy / sizeof(Addr);     /* per seg */
  linksegsize = guardians * sizeof(RingStruct);
  linksegsize = SizeAlignUp(linksegsize, ArenaAlign(space));
  res = PoolSegAlloc(&linkseg, SegPrefDefault(), pool, linksegsize);
  if(res != ResOK)
    goto failLinkSegAlloc;

  /* Link Segment is coerced to an array of RingStructs, each one */
  /* is appended to the free Ring. */
  /* The ref part of each guardian is cleared. */

  AVER(0xB3690002, guardians > 0);
  base = SegBase(space, linkseg);
  linkpart = (RingStruct *)base;
  refpart = (Addr *)SegBase(space, refseg);

  for(i=0; i<guardians; ++i) {
    RingInit(&linkpart[i]);
    RingAppend(&mrg->free, &linkpart[i]);
    refpart[i] = 0;
  }
  AVER(0xB3690003, (Addr)(&linkpart[i]) <= SegLimit(space, linkseg));
  AVER(0xB3690004, (Addr)(&refpart[i]) <= SegLimit(space, refseg));
  refseg->rankSet = RankSetSingle(RankFINAL); /* design.mps.seg.field.rankSet.start */
  refseg->summary = RefSetUNIV;		/* design.mps.seg.field.summary.start */

  group->refseg = refseg;
  group->linkseg = linkseg;
  refseg->p = group;
  linkseg->p = group;
  RingInit(&group->group);
  RingAppend(&mrg->group, &group->group);
  group->sig = MRGGroupSig;

  return ResOK;

failLinkSegAlloc:
  PoolSegFree(pool, refseg);
failRefSegAlloc:
  SpaceFree(space, (Addr)group, (Size)sizeof(MRGGroupStruct)); 
failSpaceAlloc:
  return res;
}

static Res MRGGroupScan(ScanState ss, MRGGroup group, MRG mrg)
{
  Addr base;
  Res res;
  Space space;
  Addr *refpart;
  Word guardians, i;

  space = PoolSpace(MRGPool(mrg));

  guardians = mrg->extendBy / sizeof(Addr);	/* per seg */
  AVER(0xB3690005, guardians > 0);
  base = SegBase(space, group->refseg);
  refpart = (Addr *)base;
  TRACE_SCAN_BEGIN(ss) {
    for(i=0; i<guardians; ++i) {
      if(!TRACE_FIX1(ss, refpart[i])) continue;
      res = TRACE_FIX2(ss, &refpart[i]);
      if(res != ResOK) {
        return res;
      }
      if(ss->rank == RankFINAL && !ss->wasMarked) {     /* .improve.rank */
        RingStruct *linkpart =
          (RingStruct *)SegBase(space, group->linkseg);
        RingRemove(&linkpart[i]);
        RingAppend(&mrg->exit, &linkpart[i]);
      }
    }
  } TRACE_SCAN_END(ss);

  return ResOK;
}


static Bool MRGCheck(MRG mrg);

static Res MRGInit(Pool pool, va_list args)
{
  MRG mrg = PoolPoolMRG(pool);

  UNUSED(args);
  
  RingInit(&mrg->entry);
  RingInit(&mrg->exit);
  RingInit(&mrg->free);
  RingInit(&mrg->group);
  mrg->extendBy = ArenaAlign(PoolSpace(pool));
  mrg->sig = MRGSig;

  AVERT(0xB3690006, MRG, mrg);

  return ResOK;
}

static void MRGFinish(Pool pool)
{
  MRG mrg;
  Ring node;

  AVERT(0xB3690007, Pool, pool);
  mrg = PoolPoolMRG(pool);
  AVERT(0xB3690008, MRG, mrg);

  node = RingNext(&mrg->group);
  while(node != &mrg->group) {
    Ring next = RingNext(node);
    MRGGroup group = RING_ELT(MRGGroup, group, node);
    MRGGroupDestroy(group, mrg);

    node = next;
  }

  mrg->sig = SigInvalid;
}

static Pool MRGPool(MRG mrg)
{
  AVERT(0xB3690009, MRG, mrg);
  return &mrg->poolStruct;
}

static Res MRGAlloc(Addr *pReturn, Pool pool, Size size)
{
  Addr *refpart;
  Bool b;
  Index gi;
  MRG mrg;
  MRGGroup group;
  MRGGroup junk;                /* .group.useless */
  Res res;
  Ring f;
  Seg seg;
  Space space;

  AVERT(0xB369000A, Pool, pool);
  mrg = PoolPoolMRG(pool);
  AVERT(0xB369000B, MRG, mrg);

  AVER(0xB369000C, pReturn != NULL);
  AVER(0xB369000D, size == sizeof(Addr));   /* design.mps.poolmrg.alloc.one-size */

  space = PoolSpace(pool);

  f = RingNext(&mrg->free);

  /* design.mps.poolmrg.alloc.grow */

  if(f == &mrg->free) {                 /* (Ring has no elements) */
    res = MRGGroupCreate(&junk, mrg);   /* .group.useless: group isn't used */
    if(res != ResOK) {
      return res;
    }
    f = RingNext(&mrg->free);
  }
  AVER(0xB369000E, f != &mrg->free);

  /* design.mps.poolmrg.alloc.pop */
  RingRemove(f);
  RingAppend(&mrg->entry, f);
  gi = indexOfLinkPart((Addr)f, space);
  b = SegOfAddr(&seg, space, (Addr)f);
  AVER(0xB369000F, b);
  group = seg->p;
  refpart = (Addr *)SegBase(space, group->refseg);

  /* design.mps.poolmrg.guardian.ref.alloc */
  *pReturn = (Addr)(&refpart[gi]);
  return ResOK;
}

static void MRGFree(Pool pool, Addr old, Size size)
{
  MRG mrg;
  Index gi;
  Space space;
  Seg seg;
  MRGGroup group;
  Bool b;
  RingStruct *linkpart;

  AVERT(0xB3690010, Pool, pool);
  mrg = PoolPoolMRG(pool);
  AVERT(0xB3690011, MRG, mrg);

  AVER(0xB3690012, old != (Addr)0);
  AVER(0xB3690013, size == sizeof(Addr));

  space = PoolSpace(pool);
  b = SegOfAddr(&seg, space, old);
  AVER(0xB3690014, b);
  group = seg->p;
  linkpart = (RingStruct *)SegBase(space, group->linkseg);

  /* design.mps.poolmrg.guardian.ref.free */
  gi = indexOfRefPart(old, space);

  AVER(0xB3690015, RingCheck(&linkpart[gi]));
  RingRemove(&linkpart[gi]);
  RingAppend(&mrg->free, &linkpart[gi]);
  *(Addr *)old = 0;     /* design.mps.poolmrg.free.overwrite */
}

static Res MRGDescribe(Pool pool, mps_lib_FILE *stream)
{
  MRG mrg;
  Ring r;
  Space space;
  Bool b;
  MRGGroup group;
  Index gi;
  Seg seg;
  Addr *refpart;

  AVERT(0xB3690016, Pool, pool);
  mrg = PoolPoolMRG(pool);
  AVERT(0xB3690017, MRG, mrg);
  /* Cannot check stream */

  space = PoolSpace(pool);

  WriteF(stream, "  extendBy $W\n", mrg->extendBy, NULL);
  WriteF(stream, "  Entry queue:\n", NULL);
  RING_FOR(r, &mrg->entry) {
    b = SegOfAddr(&seg, space, (Addr)r);
    AVER(0xB3690018, b);
    group = seg->p;
    refpart = (Addr *)SegBase(space, group->refseg);
    gi = indexOfLinkPart((Addr)r, space);
    WriteF(stream,
           "    at $A ref $A\n",
           (WriteFA)&refpart[gi], (WriteFA)refpart[gi],
           NULL);
  }
  WriteF(stream, "  Exit queue:\n", NULL);
  RING_FOR(r, &mrg->exit) {
    b = SegOfAddr(&seg, space, (Addr)r);
    AVER(0xB3690019, b);
    group = seg->p;
    refpart = (Addr *)SegBase(space, group->refseg);
    gi = indexOfLinkPart((Addr)r, space);
    WriteF(stream,
           "    at $A ref $A\n",
           (WriteFA)&refpart[gi], (WriteFA)refpart[gi],
           NULL);
  }

  return ResOK;
}

static Res MRGScan(ScanState ss, Pool pool, Seg seg)
{
  MRG mrg;
  Res res;
  MRGGroup group;

  AVERT(0xB369001A, ScanState, ss);
  AVERT(0xB369001B, Pool, pool);
  mrg = PoolPoolMRG(pool);
  AVERT(0xB369001C, MRG, mrg);
  AVERT(0xB369001D, Seg, seg);

  AVER(0xB369001E, seg->rankSet == RankSetSingle(RankFINAL));
  AVER(0xB369001F, TraceSetInter(seg->grey, ss->traces) != TraceSetEMPTY);
  group = (MRGGroup)seg->p;
  AVER(0xB3690020, seg == group->refseg);

  res = MRGGroupScan(ss, group, mrg);
  if(res != ResOK) return res;

  return ResOK;
}

static PoolClassStruct PoolClassMRGStruct = {
  PoolClassSig,                         /* sig */
  "MRG",                                /* name */
  sizeof(MRGStruct),                    /* size */
  offsetof(MRGStruct, poolStruct),      /* offset */
  AttrSCAN | AttrALLOC | AttrFREE | AttrINCR_RB,
  MRGInit,                              /* init */
  MRGFinish,                            /* finish */
  MRGAlloc,                             /* alloc */
  MRGFree,                              /* free */
  PoolNoBufferInit,                     /* bufferInit */
  PoolNoBufferFill,                     /* bufferFill */
  PoolNoBufferEmpty,                    /* bufferEmpty */
  PoolNoBufferFinish,                   /* bufferFinish */
  PoolNoCondemn,                        /* condemn */
  PoolTrivGrey,                         /* grey */
  MRGScan,                              /* scan */
  PoolNoFix,                            /* fix */
  PoolNoReclaim,                        /* reclaim */
  MRGDescribe,                          /* describe */
  PoolClassSig                          /* impl.h.mpmst.class.end-sig */
};

PoolClass PoolClassMRG(void)
{
  return &PoolClassMRGStruct;
}

/* .check.norecurse: the expression &mrg->poolStruct is used instead of
 * the more natural MRGPool(mrg).  The latter results in infinite
 * recursion because MRGPool calls MRGCheck.
 */
static Bool MRGCheck(MRG mrg)
{
  Space space;

  CHECKS(0xB3690021, MRG, mrg);
  CHECKD(0xB3690022, Pool, &mrg->poolStruct);
  CHECKL(0xB3690023, mrg->poolStruct.class == &PoolClassMRGStruct);
  CHECKL(0xB3690024, RingCheck(&mrg->entry));
  CHECKL(0xB3690025, RingCheck(&mrg->exit));
  CHECKL(0xB3690026, RingCheck(&mrg->free));
  CHECKL(0xB3690027, RingCheck(&mrg->group));
  space = PoolSpace(&mrg->poolStruct);  /* .check.norecurse */
  CHECKL(0xB3690028, mrg->extendBy == ArenaAlign(space));
  return TRUE;
}
