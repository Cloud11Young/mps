/* impl.c.arenacl: ARENA IMPLEMENTATION USING CLIENT MEMORY
 *
 * $HopeName$
 * 
 * Copyright (C) 1996 Harlequin Group, all rights reserved.
 *
 * .readership: MM developers
 * 
 * .design: See design.mps.arena.client.
 *
 *
 * .req: The requirements are not fully known. They are inherited from
 * req.epcore, and from other requirements on the MPS. They include:
 * 
 * .req.client: Manage memory provided by the client.
 * 
 * .req.client.only: Operate entirely within client-provided memory.
 * 
 * .req.extend: Allow extension by the client providing additional chunks
 *   of memory.
 * 
 * .req.extend.few: Normal operation will be in one chunk; "a few" chunks is
 *   unusual; "many" chunks is very unlikely.
 * 
 * .req.extend.slow: Speed may degrade proportionally to the number of 
 *   chunks of memory. 
 *
 * .req.place: Allow preferential placement of segments.
 */

#include "mpm.h"

#ifndef TARGET_ARENA_CLIENT
#error "Client arena not configured"
#endif

SRCID(arenacl, "$HopeName$");

Bool ArenaCheck(Arena arena)
{
  CHECKS(Arena,arena);
  CHECKL(RingCheck(&arena->chunkRing));
  /* no possible check on arena->chunkSerial */
  CHECKL(arena->pageShift < WORD_WIDTH);
  CHECKL(arena->pageSize == 1uL << arena->pageShift);
  return TRUE;
}

typedef struct ChunkStruct *Chunk;	/* chunk type */
typedef struct PageStruct *Page;	/* page type */
typedef Word *BT;               	/* bool table type */

#define ChunkSig	((Sig)0x519C409c)

typedef struct ChunkStruct {    /* chunk structure */
  Sig sig;                      /* impl.h.misc.sig */
  Arena arena;			/* the arena */
  RingStruct arenaRing;		/* ring of chunks within the arena */
  Serial serial;		/* serial within the arena */
  Size pages;                   /* number of pages in chunk */
  Addr base;                    /* base address of chunk */
  Addr limit;                   /* limit address of chunk */
  Addr pageBase;                /* base of first managed page in chunk */
  Page pageTable;               /* the page table */
  BT freeTable;                 /* page free table */
} ChunkStruct;

static Bool ChunkCheck(Chunk chunk)
{
  CHECKS(Chunk, chunk);
  CHECKU(Arena, chunk->arena);
  CHECKL(RingCheck(&chunk->arenaRing));
  CHECKL(chunk->serial <= chunk->arena->chunkSerial);
  CHECKL(chunk->pages >= 0);
  /* check base and limit: */
  CHECKL(chunk->base != (Addr)0);
  CHECKL(chunk->limit != (Addr)0);
  CHECKL(chunk->base < chunk->limit);
  /* check the control structures are not NULL: */
  CHECKL(chunk->pageBase != (Addr)0);
  CHECKL(chunk->pageTable != NULL);
  CHECKL(chunk->freeTable != NULL);
  /* check the control structures are between base and limit */
  /* (allowing for the case in which the chunk manages no pages): */
  CHECKL((Addr)chunk >= chunk->base);
  CHECKL((Addr)chunk < chunk->limit);
  CHECKL(chunk->pageBase > chunk->base);
  CHECKL(chunk->pageBase <= chunk->limit);
  CHECKL((Addr)chunk->pageTable > chunk->base);
  CHECKL((Addr)chunk->pageTable <= chunk->limit);
  CHECKL((Addr)chunk->freeTable > chunk->base);
  CHECKL((Addr)chunk->freeTable <= chunk->limit);
  /* check order of control structures within chunk: */
  CHECKL((Addr)chunk < (Addr)chunk->pageTable);
  CHECKL((Addr)chunk->pageTable <= (Addr)chunk->freeTable);
  CHECKL((Addr)chunk->freeTable <= (Addr)chunk->pageBase);
  /* check size of control structures within chunk: */
  CHECKL(AddrSub((Addr)chunk->pageTable, (Addr)chunk) >= sizeof(ChunkStruct));
  	/* enough space for page table: */
  CHECKL(AddrSub((Addr)chunk->freeTable, (Addr)chunk->pageTable) /
	 sizeof(PageStruct) >= chunk->pages);
	/* enough space for free table: */
  CHECKL(AddrSub(chunk->pageBase, (Addr)chunk->freeTable) / sizeof(Word) >=
	 SizeAlignUp(chunk->pages,WORD_WIDTH) >> WORD_SHIFT);
  	/* enough space for pages */
  CHECKL((AddrSub(chunk->limit, chunk->pageBase) >> chunk->arena->pageShift)
	 == chunk->pages);
  /* .check.tables: could check the consistency of the tables, but not O(1) */
  return TRUE;
}

/* PageStruct -- page structure
 *
 * The page table is lifted entirely from arenavm. See
 * design.mps.arenavm.table.* */

typedef struct PageStruct {     /* page structure */
  union {
    SegStruct head;             /* segment */
    struct {
      Pool pool;                /* .page: NULL, must be first field
                                 * see impl.h.mpmst.seg.pool */
      Seg seg;                  /* segment at base page of run */
      Addr limit;               /* limit of segment */
    } tail;                     /* tail page */
  } the;
} PageStruct;

/* would like to be able to write a PageCheck, but Pages don't even
 * have a signature */

/* Page Index to Base address mapping
 *
 * See design.mps.arenavm.table.linear
 */

#define PageBase(chunk, pi) \
  AddrAdd((chunk)->pageBase, ((pi) << (chunk)->arena->pageShift))

/* Index Types
 * 
 * PI is the type of a value used to index into a page table.
 * 
 * BI is the type of a value used to index into a bool table (See BTGet
 * and BTSet in this module).
 */

typedef Size PI;
typedef Size BI;

/* BTGet -- get a bool from a bool table
 *
 * Note: The function version of BTGet isn't used anywhere in
 * this source, but is left here in case we want to revert from
 * the macro.
 */

#if 0
static Bool (BTGet)(BT bt, BI i)
{
  Size wi = i >> WORD_SHIFT;            /* word index */
  Size bi = i & (WORD_WIDTH - 1);       /* bit index */
  return (bt[wi] >> bi) & 1;
}
#endif /* 0 */

#define BTGet(bt, i) \
  (((bt)[(i)>>WORD_SHIFT] >> ((i)&(WORD_WIDTH-1))) & 1)


/* BTSet -- set a bool in a bool table */

static void BTSet(BT bt, BI i, Bool b)
{
  Size bi = i & (WORD_WIDTH - 1);       /* bit index */
  Word mask = ~((Word)1 << bi);
  Size wi = i >> WORD_SHIFT;            /* word index */
  bt[wi] = (bt[wi] & mask) | ((Word)b << bi);
}

/* Space Arena Projection
 * 
 * Only the arena module needs to discuss the arena object, hence, this
 * method is private to this module.
 */

#define SpaceArena(space)       (&(space)->arenaStruct)

/* ChunkCreate -- create a chunk */

static Res ChunkCreate(Chunk *chunkReturn, Addr base, Addr limit, Arena arena)
{
  Chunk chunk;
  Addr a;
  Size tablePages;
  Size freeTableWords;
  PI i;

  AVERT(Arena, arena);
  AVER(chunkReturn != NULL);
  AVER(base != (Addr)0);
  AVER(limit != (Addr)0);
  AVER(limit > base);

  /* allocate the chunk */

  a = AddrAlignUp(base,ARCH_ALIGN);
  chunk = (Chunk) a;

  /* figure out the sizes and locations of the control structures */
  a = AddrAlignUp(AddrAdd(a,sizeof(ChunkStruct)), ARCH_ALIGN);

  if (a > limit) /* the chunk is too small */
    return ResRESOURCE;

  tablePages = AddrSub(limit,a) >> arena->pageShift;

  chunk->pageTable = (Page) a;
  a = AddrAlignUp(AddrAdd(a,sizeof(PageStruct) * tablePages), ARCH_ALIGN);

  chunk->freeTable = (BT) a;
  freeTableWords = SizeAlignUp(tablePages, WORD_WIDTH) >> WORD_SHIFT;
  a = AddrAlignUp(AddrAdd(a,freeTableWords * sizeof(Word)), ARCH_ALIGN);

  /* the rest is in managed pages; there may be some wastage at the end */
  chunk->pageBase = a;

  /* initialize the remaining slots */
  chunk->base = base;
  chunk->limit = limit;
  chunk->pages = AddrSub(chunk->limit, chunk->pageBase) >> arena->pageShift;
  chunk->arena = arena;

  /* initialize the freeTable */
  /* .improve.twiddle.init: Could go a lot faster with bit twiddling */
  for(i = 0; i < tablePages; ++i)
    BTSet(chunk->freeTable, i, TRUE);

  /* link to the arena */
  RingInit(&chunk->arenaRing);
  RingAppend(&arena->chunkRing, &chunk->arenaRing);
  chunk->serial = arena->chunkSerial;
  ++ arena->chunkSerial;

  /* sign it, check it, return it */
  chunk->sig = ChunkSig;
  AVERT(Chunk, chunk);

  *chunkReturn = chunk;
  return ResOK;
}

/* ArenaCreate -- create the arena
 *
 * In fact, this creates the space structure and initializes the
 * arena part, and makes the first chunk from the memory left over.
 */

Res ArenaCreate(Space *spaceReturn, Size size, Addr base)
{
  Space space;
  Arena arena;
  Addr limit;
  Res res;
  Chunk chunk;
  
  AVER(spaceReturn != NULL);
  AVER(size > 0);
  AVER(base != (Addr)0);

  limit = AddrAdd(base,size);

  /* allocate the space */
  base = AddrAlignUp(base,ARCH_ALIGN);
  space = (Space) base;
  base = AddrAlignUp(AddrAdd(base,sizeof(SpaceStruct)),ARCH_ALIGN);

  arena = SpaceArena(space);
  arena->pageSize = ArenaClientPageSize;
  arena->pageShift = SizeLog2(arena->pageSize);

  RingInit(&arena->chunkRing);
  arena->chunkSerial = (Serial)0;

  /* have to have a valid arena before calling ChunkCreate */
  arena->sig = ArenaSig;
  
  AVERT(Arena, arena);

  res = ChunkCreate(&chunk, base, limit, arena);
  if (res)
    return res;
  
  /* Set the zone shift to divide the initial chunk into the same
   * number of zones as will fit into a reference set (the number of
   * bits in a word). Note that some zones are discontiguous in the
   * arena if the size is not a power of 2. */

  space->zoneShift = SizeFloorLog2(size >> WORD_SHIFT);

  *spaceReturn = space;
  return ResOK;
}

/* ArenaDestroy -- finish the arena and destroy the space structure */

void ArenaDestroy(Space space)
{
  Arena arena;
  AVERT(Arena, SpaceArena(space));
  arena = SpaceArena(space);
  arena->sig = SigInvalid;
}

/* ArenaExtend: this extends the arena */

res ArenaExtend(Space space, Addr base, Addr limit)
{
  Arena arena;
  Chunk chunk;
  Res res;

  AVERT(Space,space);
  AVER(base != (Addr)0);
  AVER(limit != (Addr)0);
  AVER(limit > base);
  
  arena = SpaceArena(space);
  res = ChunkCreate(&chunk, base, limit, arena);
  return res;
}

/* ArenaRetract returns ResFAIL if there is no such chunk, or if it
   exists but is not fully free. */

res ArenaRetract(Space space, Addr base, Addr limit)
{
  Arena arena;
  Chunk chunk;
  Ring node;
  
  AVERT(Space, space);
  AVER(base != (Addr)0);
  AVER(limit != (Addr)0);
  AVER(limit > base);

  arena = SpaceArena(space);

  RING_FOR(node, &arena->chunkRing) {
    Chunk chunk = RING_ELT(Chunk, arenaRing, node);
    AVERT(Chunk, chunk);
    if ((chunk->base == base) &&
	(chunk->limit == limit)) {
      /* check that it's empty */
      PI pi;
      for (pi = 0; pi < chunk->pages; pi++) {
	if (BTGet(chunk->freeTable, pi) != FALSE)
	  return ResFAIL;
      }
      return ResOK;
    }
  }

  return ResFAIL;	/* no such chunk */
}


/* ArenaReserved -- return the amount of reserved address space
 * ArenaCommitted -- return the amount of committed virtual memory
 * 
 * since this uses real client memory, these two numbers are both the same.
 */

Size ArenaReserved(Space space)
{
  Arena arena;
  Size size;
  Ring node;

  AVERT(Arena, SpaceArena(space));

  arena = SpaceArena(space);

  size = 0;
  RING_FOR(node, &arena->chunkRing) { /* .req.extend.slow */
    Chunk chunk = RING_ELT(Chunk, arenaRing, node);
    AVERT(Chunk, chunk);
    size += AddrSub(chunk->limit, chunk->base);
  }
}

Size ArenaCommitted(Space space)
{
  return ArenaReserved(space);
}

/* SegCheck -- check the consistency of a segment structure */  

Bool SegCheck(Seg seg)
{
  CHECKU(Pool, seg->pool);
  /* .seg.check-little: all other fields can't be checked */
  return TRUE;
}

/* preferences... */

#define SegPrefSig	((Sig)0x5195e997)

struct SegPrefStruct {	/* segment placement preferences */
  Sig sig;		/* impl.h.misc.sig */
  Space space;		/* the owning space */
  Bool high;		/* high or low */
} SegPrefStruct;

typedef struct SegPrefStruct *SegPref;

typedef int SegPrefKind;

enum {
  SegPrefHigh = 0,	/* allocate segments downwards from top of chunk */
  SegPrefLow		/* allocate segments upwards from bottom of chunk */
};

Bool SegPrefCheck(SegPref pref)
{
  CHECKS(SegPref, pref);
  CHECKU(Space, pref->space);
  CHECKD(Bool, pref->high);
  /* nothing else to check */
  return TRUE;
}

/* SegPrefCreate */

Res SegPrefCreate (SegPref *prefReturn, Space space)
{
  void *p;
  Res res;
  SegPref pref;

  AVERT(Space, space);
  AVER(sp != NULL);

  res = SpaceAlloc(&p, space, sizeof(SegPrefStruct));
  if (res)
    return res;

  pref = (SegPref)res;

  pref->space = space;
  pref->high = FALSE;

  pref->sig = SegPrefSig;
  AVERT(SegPref, pref);
  *prefReturn = pref;
  return ResOK;
}

void SegPrefDestroy (SegPref sp)
{
  Space space;

  AVERT(SegPref, sp);
  space = sp->space;

  sp->Sig = SigInvalid;
  SpaceFree(space, (void*)sp, sizeof(SegPrefStruct));
}

Res SegPrefExpress (SegPref sp, SegPrefKind kind, void *p)
{
  AVERT(SegPref,sp);
  switch(kind) {
  case SegPrefHigh:
    AVER(p == NULL);
    sp->high = TRUE;
    return ResOK;
  case SegPrefLow:
    AVER(p == NULL);
    sp->high = FALSE;
    return ResOK;
  default:
    /* see design.mps.pref.default */
    return ResOK;
  }
}

/* ChunkSegAlloc: allocate a segment in a chunk */

static Res ChunkSegAlloc(Seg *segReturn, SegPref pref, Size pages, Pool pool,
			 Chunk chunk)
{
  PI pi, count, base;
  Seg seg;
  Res res;
  Arena arena;

  AVER(segReturn != NULL);
  AVERT(Chunk, chunk);

  if (chunk->pages == 0)
    return ResRESOURCE;
  
  arena = chunk->arena;

  /* Search for a free run of pages in the free table.
   * .improve.bit-twiddle:  This code can probably be seriously
   * optimised by twiddling the bit table.
   */

  count = 0;

  if (pref->high) {
    pi = chunk->pages;
    while (pi != 0) {
      pi--;
      if (BTGet(chunk->freeTable,pi)) {
	++count;
	if (count == pages) {
	  base = pi;
	  goto found;
	}
      } else
	count = 0;
    }
  } else {
    pi = 0;
    while (pi != chunk->pages) {
      if(BTGet(chunk->freeTable, pi)) {
	if(count == 0)
	  base = pi;
	++count;
	if(count == pages)
	  goto found;
      } else
	count = 0;
      pi++;
    }
  }
  
  /* No space was found.
   * .improve.alloc-fail: This could be because the request was
   * too large, or perhaps because of fragmentation.  We could return a
   * more meaningful code.
   */
  return ResRESOURCE;

found:
  /* Initialize the generic segment structure. */
  seg = &chunk->pageTable[base].the.head;
  seg->pool = pool;
  seg->p = NULL;
  seg->rank = RankEXACT;    /*  exact by default */
  seg->condemned = TraceIdNONE;

  seg->pm = AccessSetEMPTY; /* see impl.c.shield */
  seg->sm = AccessSetEMPTY;
  seg->depth = 0;

  /* Allocate the first page, and, if there is more than one page,
   * allocate the rest of the pages and store the multi-page information
   * in the page table.
   */
  AVER(BTGet(chunk->freeTable, base));
  BTSet(chunk->freeTable, base, FALSE);
  if(pages > 1) {
    Addr limit = PageBase(chunk, base + pages);
    seg->single = FALSE;
    for(pi = base + 1; pi < base + pages; ++pi) {
      AVER(BTGet(chunk->freeTable, pi));
      BTSet(chunk->freeTable, pi, FALSE);
      chunk->pageTable[pi].the.tail.pool = NULL;
      chunk->pageTable[pi].the.tail.seg = seg;
      chunk->pageTable[pi].the.tail.limit = limit;
    }
  } else {
    seg->single = TRUE;
  }
  
  AVERT(Seg, seg);

  *segReturn = seg;
  return ResOK;
}


/* SegAlloc -- allocate a segment from the arena */

Res SegAlloc(Seg *segReturn, SegPref pref, Space space, Size size, Pool pool)
{
  Arena arena;
  Res res;
  Ring node;
  Size pages;

  AVER(segReturn != NULL);
  AVERT(SegPref, pref);
  AVER(size > 0);
  AVERT(Pool, pool);
  
  /* NULL is used as a discriminator (see
   * design.mps.arenavm.table.disc), therefore the real pool must be
   * non-NULL.
   */
  AVER(pool != NULL);

  arena = SpaceArena(space);

  AVERT(Arena, arena);
  AVER(SizeIsAligned(size, arena->pageSize));

  pages = size >> arena->pageShift;

  RING_FOR(node, &arena->chunkRing) { /* .req.extend.slow */
    Chunk chunk = RING_ELT(Chunk, arenaRing, node);
    res = ChunkSegAlloc(segReturn, pref, pages, pool, chunk);
    if (res == ResOK)
      return res;
  }
  return ResRESOURCE;
}

/* SegChunk: identify the chunk (and index) in which a segment resides */

static Res SegChunk(chunk *chunkReturn, PI *piReturn, Seg seg, Arena arena)
{
  Page page;
  Ring node;
  
  AVER(chunkReturn != NULL);
  AVERT(Seg, seg);
  AVERT(Arena, arena);

  page = PARENT(PageStruct, the.head, seg);

  RING_FOR(node, arena->chunkRing) {
    Chunk chunk = RING_ELT(Chunk, arenaRing, node);
    if ((page >= chunk->pageTable) &&
	(page < (chunk->pageTable + chunk->pages))) {
      *piReturn = page - chunk->pageTable;
      *chunkReturn = chunk;
      return ResOK;
    }
  }
  return ResFAIL;
}


/* SegFree - free a segment in the arena */

void SegFree(Space space, Seg seg)
{
  Arena arena;
  Chunk chunk;
  Page page;
  PI pi, pl, pn;
  Addr base, limit; 
  Res res;

  AVERT(Seg, seg);

  arena = SpaceArena(space);
  AVERT(Arena, arena);

  res = SegChunk(&chunk, &pi, seg, arena);
  AVER(res == ResOK);

  limit = SegLimit(space, seg);

  /* Remember the base address of the segment so it can be */
  /* unmapped .free.unmap */
  base = PageBase(chunk, pi);

  /* Calculate the number of pages in the segment, and hence the
   * limit for .free.loop */
  pn = AddrOffset(base, limit) >> arena->pageShift;
  pl = pi + pn;
  /* .free.loop: */
  for( ; pi < pl; ++pi) {
    AVER(BTGet(chunk->freeTable, pi) == FALSE);
    BTSet(chunk->freeTable, pi, TRUE);
  }

  /* Double check that .free.loop takes us to the limit page of the
   * segment.
   */
  AVER(PageBase(chunk, pi) == limit);
}


/* ArenaAlign -- return the alignment of segments */

Align ArenaAlign(Space space)
{
  Arena arena;
  AVERT(Arena, SpaceArena(space));
  arena = SpaceArena(space);
  return arena->pageSize;
}


/* SegBase -- return the base address of a segment
 *
 * The segment base is calculated by identifying the chunk and page
 * index, then multiplying that by the page size and adding it to
 * the chunk base address. */

Addr SegBase(Space space, Seg seg)
{
  Arena arena;
  PI pi;
  Chunk chunk;
  Res res;
  
  AVERT(Seg, seg);

  arena = SpaceArena(space);
  AVERT(Arena, arena);

  res = SegChunk(&chunk, &pi, seg, arena);
  AVER(res == ResOK);

  return PageBase(chunk, pi);
}


/* SegLimit -- return the limit address (end+1) of a segment
 *
 * If the segment is a single page, then the limit is just
 * the next page, otherwise it is stored on the next page
 * table entry.
 */

Addr SegLimit(Space space, Seg seg)
{
  Arena arena;

  AVERT(Seg, seg);

  arena = SpaceArena(space);
  AVERT(Arena, arena);

  if(seg->single)
    return AddrAdd(SegBase(space, seg), arena->pageSize);
  else {
    page = PARENT(PageStruct, the.head, seg);
    return page[1].the.tail.limit;
  }
}


/* SegSize -- return the size (limit - base) of a segment
 *
 * .improve.redundant-calc: There is scope for optimizing this,
 * because both base and limit calls do roughly the same thing twice.
 */

Size SegSize(Space space, Seg seg)
{
  AVERT(Arena, SpaceArena(space));
  AVERT(Seg, seg);
  return AddrOffset(SegBase(space, seg), SegLimit(space, seg));
}


/* SegOfAddr -- return the segment which encloses an address
 *
 * If the address is within the bounds of the arena, calculate the
 * page table index from the address and see if the page is allocated.
 * If it is a head page, return the segment, otherwise follow the
 * tail's pointer back to the segment in the head page.
 */

Bool SegOfAddr(Seg *segReturn, Space space, Addr addr)
{
  Arena arena;
  Ring node;
  
  AVER(segReturn != NULL);
  
  arena = SpaceArena(space);
  AVERT(Arena, arena);

  RING_FOR(node, &arena-chunkRing) {
    Chunk chunk = RING_ELT(Chunk, arenaRing, node);
    if(chunk->base <= addr && addr < chunk->limit) {
      PI pi = AddrOffset(chunk->pageBase, addr) >> arena->pageShift;
      if(!BTGet(chunk->freeTable, pi)) {
	Page page = &chunk->pageTable[pi];
	if(page->the.head.pool != NULL)
	  *segReturn = &page->the.head;
	else
	  *segReturn = page->the.tail.seg;
	return TRUE;
      }
    }
  }
  
  return FALSE;
}


/* SegSearchChunk -- search for a segment in a given chunk
 *
 * Searches for a segment in the chunk starting at page index pi,
 * return NULL if there is none.  A segment is present if it is
 * not free, and its pool is not NULL.
 *
 * This function is private to this module and is used in the segment
 * iteration protocol (SegFirst and SegNext).
 */
static Seg SegSearchChunk(Chunk chunk, PI pi)
{
  AVERT(Chunk, chunk);
  AVER(pi <= chunk->pages);

  while(pi < chunk->pages &&
        (BTGet(chunk->freeTable, pi) ||
         chunk->pageTable[pi].the.head.pool == NULL))
    ++pi;
  
  if(pi < chunk->pages)
    return &chunk->pageTable[pi].the.head;
  
  AVER(pi == chunk->pages);
  return NULL;
}


/* SegFirst -- return the first segment in the arena
 *
 * This is used to start an iteration over all segments in the arena.
 */

Seg SegFirst(Space space)
{
  Arena arena;
  Chunk chunk;
  Ring node;

  arena = SpaceArena(space);
  AVERT(Arena, arena);

  /* must do the right thing for chunks with no pages */
  RING_FOR(node, &arena->chunkRing) {
    Chunk chunk = RING_ELT(Chunk, arenaRing, node);
    Seg seg = SegSearchChunk(chunk, 0);
    if (seg != NULL)
      return seg;
  }

  return NULL;
}


/* SegNext -- return the next segment in the arena
 *
 * This is used as the iteration step when iterating over all
 * segments in the arena.
 */

Seg SegNext(Space space, Seg seg)
{
  Arena arena;
  Chunk chunk;
  Page page;
  PI pi;
  Res res;
  Seg next;

  AVERT(Space, space);
  AVERT(Seg, seg);

  AVERT(Arena, SpaceArena(space));
  arena = SpaceArena(space);

  res = SegChunk(&chunk, &pi, seg, arena);
  AVER(res == ResOK);

  next = SegSearchChunk(chunk, pi+1);

  while (next == NULL) { /* then we've reached the end of the chunk */
    Ring node = &chunk->arenaRing;
    node = node->next;
    if (node == &arena->chunkRing)
      return NULL;
    chunk = RING_ELT(Chunk, arenaRing, node);
    next = SegSearchChunk(chunk,0);
  }
  return next;
}
