/* impl.h.mpmtypes: MEMORY POOL MANAGER TYPES
 *
 * $HopeName: MMsrc!mpmtypes.h(MMdevel_trace2.2) $
 * Copyright (C) 1996 Harlequin Group, all rights reserved.
 *
 * .readership: MM developers.
 * .design: design.mps.type
 *
 * .rationale: Types and type constants are almost all defined
 * in this header, in advance of any declarations of prototypes
 * or structures.  This avoids difficulties in defining recursive
 * data structures, and also provides a nice browsable list of
 * types.
 */

#ifndef mpmtypes_h
#define mpmtypes_h

#include "std.h"
#include "mpslib.h"


/* TYPES */

typedef unsigned long Sig;              /* design.mps.sig */
typedef int Bool;                       /* design.mps.type.bool */
typedef int Res;                        /* design.mps.type.res */
typedef void (*Fun)(void);		/* design.mps.type.fun */
typedef MPS_T_WORD Word;                /* design.mps.type.word */
typedef unsigned char Byte;             /* design.mps.type.byte */
typedef struct AddrStruct *Addr;        /* design.mps.type.addr */
typedef Word Size;                      /* design.mps.type.size */
typedef Word Index;                     /* design.mps.type.index */
typedef Word Align;                     /* design.mps.type.align */
typedef unsigned Shift;                 /* design.mps.type.shift */
typedef Addr Ref;                       /* design.mps.type.ref */
typedef Word RefSet;                    /* design.mps.refset */
typedef unsigned Rank;                  /* design.mps.ref */
typedef Byte RankSet;                   /* NOT DOCUMENTED */
typedef Size Epoch;                     /* design.mps.ld */
typedef unsigned TraceId;               /* design.mps.tracer */
typedef Byte TraceSet;                  /* design.mps.tracer */
typedef unsigned TraceState;		/* design.mps.tracer */
typedef unsigned AccessSet;             /* design.mps.type.access-set */
typedef unsigned Attr;                  /* design.mps.type.attr */
typedef int RootVar;                    /* design.mps.type.rootvar */
typedef unsigned Serial;		/* design.mps.type.serial */
typedef struct RingStruct *Ring;        /* design.mps.ring */
typedef struct BufferStruct *Buffer;    /* design.mps.buffer */
typedef struct APStruct *AP;            /* design.mps.buffer */
typedef struct FormatStruct *Format;    /* design.mps.format */
typedef struct LDStruct *LD;            /* design.mps.ld */
typedef struct LockStruct *Lock;        /* impl.c.lock* */
typedef struct PoolStruct *Pool;        /* design.mps.pool */
typedef struct SpaceStruct *Space;      /* design.mps.space */
typedef struct PoolClassStruct *PoolClass; /* impl.c.poolclas */
typedef struct TraceStruct *Trace;      /* design.mps.tracer */
typedef struct FixStruct *Fix;		/* design.mps.tracer */
typedef struct SegStruct *Seg;          /* impl.c.arena* */
typedef struct ArenaStruct *Arena;      /* impl.c.arena* */
typedef struct VMStruct *VM;            /* impl.c.vm* */
typedef struct RootStruct *Root;        /* impl.c.root */
typedef struct ThreadStruct *Thread;    /* impl.c.th* */
typedef struct OptionStruct *Option;    /* NOT DOCUMENTED */
typedef struct ColStruct *Col;          /* NOT DOCUMENTED */


/* Pool*Method -- see design.mps.class-interface */

typedef Res  (*PoolInitMethod)         (Pool pool, va_list arg);
typedef void (*PoolFinishMethod)       (Pool pool);
typedef Res  (*PoolAllocMethod)        (Addr *pReturn, Pool pool, Size size);
typedef void (*PoolFreeMethod)         (Pool pool, Addr old, Size size);
typedef Res  (*PoolBufferInitMethod)   (Pool pool, Buffer buf);
typedef void (*PoolBufferFinishMethod) (Pool pool, Buffer buf);
typedef Res  (*PoolBufferFillMethod)   (Addr *baseReturn, Pool pool,
                                        Buffer buffer, Size size);
typedef Bool (*PoolBufferTripMethod)   (Pool pool, Buffer buffer,
                                        Addr base, Size size);
typedef void (*PoolBufferExposeMethod) (Pool pool, Buffer buffer);
typedef void (*PoolBufferCoverMethod)  (Pool pool, Buffer buffer);
typedef Res  (*PoolDescribeMethod)     (Pool pool, mps_lib_FILE *stream);
typedef void (*PoolCondemnMethod)      (Pool pool, Option option, Seg seg, TraceId ti);
typedef Res  (*PoolScanMethod)         (Pool pool, Fix fix, Seg seg);
typedef Res  (*PoolFixMethod)          (Ref *refIO, Pool pool, Fix fix, Seg seg);
typedef void (*PoolReclaimMethod)      (Pool pool, Seg seg, TraceSet ts);


/* Format*Method -- see design.mps.format-interface */
/* These methods must match those defined in the MPS C Interface. */
/* (See impl.h.mps.fmt-methods.) */

typedef Res  (*FormatScanMethod)   (Fix fix, Addr base, Addr limit);
typedef Addr (*FormatSkipMethod)   (Addr object);
typedef void (*FormatMoveMethod)   (Addr object, Addr to);
typedef Addr (*FormatIsMovedMethod)(Addr object);
typedef void (*FormatCopyMethod)   (Addr object, Addr to);
typedef void (*FormatPadMethod)    (Addr base, Size size);


/* Root*Method -- see design.mps.root-interface */
/* These methods must match those defined in the MPS C Interface. */
/* (See impl.h.mps.fmt-methods.) */

typedef Res (*RootScanMethod)   (Fix fix, void *p, size_t s);
typedef Res (*RootScanRegMethod)(Fix fix, Thread thread, void *p);


/* CONSTANTS */

#define SigInvalid      ((Sig)0x51915BAD) /* design.mps.sig */
#define AccessSetEMPTY  ((AccessSet)0)    /* design.mps.type.access-set */
#define AccessREAD      ((AccessSet)(1<<0))
#define AccessWRITE     ((AccessSet)(1<<1))
#define RingNONE        ((Ring)0)         /* design.mps.ring */
#define TraceSetEMPTY	BS_EMPTY(TraceSet)/* design.mps.tracer */
#define TraceSetUNIV	BS_UNIV(TraceSet)
#define RankSetEMPTY	BS_EMPTY(RankSet)
#define RefSetEMPTY	BS_EMPTY(RefSet)
#define RefSetUNIV	BS_UNIV(RefSet)
#define AttrFMT         ((Attr)(1<<0))  /* design.mps.type.attr */
#define AttrSCAN        ((Attr)(1<<1))
#define AttrPM_NO_READ  ((Attr)(1<<2))
#define AttrPM_NO_WRITE ((Attr)(1<<3))
#define AttrALLOC       ((Attr)(1<<4))
#define AttrFREE        ((Attr)(1<<5))
#define AttrBUF         ((Attr)(1<<6))
#define AttrBUF_RESERVE ((Attr)(1<<7))
#define AttrBUF_ALLOC   ((Attr)(1<<8))
#define AttrGC          ((Attr)(1<<9))
#define AttrINCR_RB     ((Attr)(1<<10))
#define AttrINCR_WB     ((Attr)(1<<11))
#define AttrMASK        (AttrFMT | AttrSCAN | AttrPM_NO_READ | \
                         AttrPM_NO_WRITE | AttrALLOC | AttrFREE | \
                         AttrBUF | AttrBUF_RESERVE | AttrBUF_ALLOC | \
                         AttrGC | AttrINCR_RB | AttrINCR_WB)

/* Rank constants -- see design.mps.type.rank */
/* These definitions must match impl.h.mps.rank. */
/* This is checked by impl.c.mpsi.check. */

enum {
  RankNONE = -1,
  RankAMBIG = 0,
  RankEXACT = 1,
  RankWEAK = 2,
  RankFINAL = 3,
  RankMAX
};


/* Root Variants -- see design.mps.type.rootvar */

enum {
  RootFUN,
  RootTABLE,
  RootREG,
  RootFMT
};


/* Boolean Constants -- see design.mps.type.bool */

enum {
  FALSE = 0,
  TRUE = 1
};


/* Result Codes -- see design.mps.type.res */
/* These definitions must match impl.h.mps.res. */
/* This is checked by impl.c.mpsi.check. */

enum {
  ResOK = 0,
  ResFAIL,
  ResRESOURCE,
  ResMEMORY,
  ResLIMIT,
  ResUNIMPL,
  ResIO
};


/* Trace States -- see design.mps.tracer */

enum {
  TraceINIT,
  TraceUNFLIPPED,
  TraceFLIPPED,
  TraceRECLAIM,
  TraceFINISHED
};


#endif /* mpmtypes_h */
