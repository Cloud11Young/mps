/* cbs.c: COALESCING BLOCK STRUCTURE IMPLEMENTATION
 *
 * $Id$
 * Copyright (c) 2001-2013 Ravenbrook Limited.  See end of file for license.
 *
 * .intro: This is a portable implementation of coalescing block
 * structures.
 *
 * .purpose: CBSs are used to manage potentially unbounded
 * collections of memory blocks.
 *
 * .sources: <design/cbs/>.
 */

#include "cbs.h"
#include "rtree.h"
#include "meter.h"
#include "poolmfs.h"
#include "mpm.h"

SRCID(cbs, "$Id$");


typedef struct CBSBlockStruct *CBSBlock;
typedef struct CBSBlockStruct {
  RNodeStruct node;
  Size maxSize; /* accurate maximum block size of sub-tree */
} CBSBlockStruct;

#define CBSBlockBase(block) ((block)->node.base)
#define CBSBlockLimit(block) ((block)->node.limit)
#define CBSBlockSize(block) AddrOffset(CBSBlockBase(block), CBSBlockLimit(block))


#define cbsOfTree(_tree) PARENT(CBSStruct, tree, (_tree))
#define cbsBlockOfNode(_node) PARENT(CBSBlockStruct, node, (_node))
#define treeOfCBS(cbs) (&((cbs)->tree))
#define nodeOfCBSBlock(block) (&((block)->node))
#define keyOfCBSBlock(block) ((void *)&((block)->node.base))


/* cbsEnter, cbsLeave -- Avoid re-entrance
 *
 * .enter-leave: The callbacks are restricted in what they may call.
 * These functions enforce this.
 *
 * .enter-leave.simple: Simple queries may be called from callbacks.
 */

static void cbsEnter(CBS cbs)
{
  /* Don't need to check as always called from interface function. */
  AVER(!cbs->inCBS);
  cbs->inCBS = TRUE;
  return;
}

static void cbsLeave(CBS cbs)
{
  /* Don't need to check as always called from interface function. */
  AVER(cbs->inCBS);
  cbs->inCBS = FALSE;
  return;
}


#if 0
static Size cbsTreeCheck(RNode node)
{
  if (node != RTREE_LEAF) {
    CBSBlock block = cbsBlockOfNode(node);
    Size size = CBSBlockSize(block);
    Size left = cbsTreeCheck(node->left);
    Size right = cbsTreeCheck(node->right);
    if (left > size) size = left;
    if (right > size) size = right;
    AVER(block->maxSize == size);
    return size;
  }
  return 0;
}
#endif


/* CBSCheck -- Check CBS */

Bool CBSCheck(CBS cbs)
{
  /* See .enter-leave.simple. */
  CHECKS(CBS, cbs);
  CHECKL(cbs != NULL);
  CHECKD(RTree, treeOfCBS(cbs));
  /* nothing to check about treeSize */
  CHECKD(Pool, cbs->blockPool);
  CHECKL(BoolCheck(cbs->fastFind));
  CHECKL(BoolCheck(cbs->inCBS));
  /* No MeterCheck */
#if 0
  (void)cbsTreeCheck(treeOfCBS(cbs)->root);
#endif
  return TRUE;
}


static Bool CBSBlockCheck(CBSBlock block)
{
  /* See .enter-leave.simple. */
  UNUSED(block); /* Required because there is no signature */
  CHECKL(block != NULL);
  CHECKD(RNode, nodeOfCBSBlock(block));

  /* If the block is in the middle of being deleted, */
  /* the pointers will be equal. */
  CHECKL(CBSBlockBase(block) <= CBSBlockLimit(block));
  /* Can't check maxSize because it may be invalid at the time */
  return TRUE;
}


/* cbsTestNode, cbsTestTree -- test for nodes larger than the S parameter */

static Bool cbsTestNode(RTree tree, RNode node,
                        void *closureP, Size size)
{
  CBSBlock block;

  AVERT(RTree, tree);
  AVERT(RNode, node);
  AVER(closureP == NULL);
  AVER(size > 0);
  AVER(cbsOfTree(tree)->fastFind);

  block = cbsBlockOfNode(node);

  return CBSBlockSize(block) >= size;
}

static Bool cbsTestTree(RTree tree, RNode node,
                        void *closureP, Size size)
{
  CBSBlock block;

  AVERT(RTree, tree);
  AVERT(RNode, node);
  AVER(closureP == NULL);
  AVER(size > 0);
  AVER(cbsOfTree(tree)->fastFind);

  block = cbsBlockOfNode(node);

  return block->maxSize >= size;
}


/* cbsUpdateNode -- update size info after restructuring */

static void cbsUpdateNode(RNode node)
{
  Size maxSize;
  CBSBlock block;

  AVERT(RNode, node);
  if (node->left != RTREE_LEAF)
    AVERT(RNode, node->left);
  if (node->right != RTREE_LEAF)
    AVERT(RNode, node->right);

  block = cbsBlockOfNode(node);
  maxSize = CBSBlockSize(block);

  if (node->left != RTREE_LEAF) {
    Size size = cbsBlockOfNode(node->left)->maxSize;
    if (size > maxSize)
      maxSize = size;
  }

  if (node->right != RTREE_LEAF) {
    Size size = cbsBlockOfNode(node->right)->maxSize;
    if (size > maxSize)
      maxSize = size;
  }

  block->maxSize = maxSize;
}


/* CBSInit -- Initialise a CBS structure
 *
 * See <design/cbs/#function.cbs.init>.
 */

ARG_DEFINE_KEY(cbs_extend_by, Size);

Res CBSInit(Arena arena, CBS cbs, void *owner, Align alignment,
            Bool fastFind, ArgList args)
{
  Size extendBy = CBS_EXTEND_BY_DEFAULT;
  ArgStruct arg;
  Res res;

  AVERT(Arena, arena);

  if (ArgPick(&arg, args, MPS_KEY_CBS_EXTEND_BY))
    extendBy = arg.val.size;

  if (fastFind)
    RTreeInit(treeOfCBS(cbs), cbsUpdateNode);
  else
    RTreeInit(treeOfCBS(cbs), RTreeTrivUpdate);
  MPS_ARGS_BEGIN(pcArgs) {
    MPS_ARGS_ADD(pcArgs, MPS_KEY_MFS_UNIT_SIZE, sizeof(CBSBlockStruct));
    MPS_ARGS_ADD(pcArgs, MPS_KEY_EXTEND_BY, extendBy);
    MPS_ARGS_DONE(pcArgs);
    res = PoolCreate(&(cbs->blockPool), arena, PoolClassMFS(), pcArgs);
  } MPS_ARGS_END(pcArgs);
  if (res != ResOK)
    return res;
  cbs->treeSize = 0;

  cbs->fastFind = fastFind;
  cbs->alignment = alignment;
  cbs->inCBS = TRUE;

  METER_INIT(cbs->treeSearch, "size of tree", (void *)cbs);

  cbs->sig = CBSSig;

  AVERT(CBS, cbs);
  EVENT2(CBSInit, cbs, owner);
  cbsLeave(cbs);
  return ResOK;
}


/* CBSFinish -- Finish a CBS structure
 *
 * See <design/cbs/#function.cbs.finish>.
 */

void CBSFinish(CBS cbs)
{
  AVERT(CBS, cbs);
  cbsEnter(cbs);

  METER_EMIT(&cbs->treeSearch);

  cbs->sig = SigInvalid;

  RTreeReset(treeOfCBS(cbs)); /* discard blocks en masse */
  RTreeFinish(treeOfCBS(cbs));
  PoolDestroy(cbs->blockPool);
}


/* Node change operators
 *
 * These four functions are called whenever blocks are created,
 * destroyed, grow, or shrink.  They maintain the maxSize if fastFind is
 * enabled.
 */

static void cbsBlockDelete(CBS cbs, CBSBlock block)
{
  AVERT(CBS, cbs);
  AVERT(CBSBlock, block);

  METER_ACC(cbs->treeSearch, cbs->treeSize);
  RTreeDelete(treeOfCBS(cbs), nodeOfCBSBlock(block));
  STATISTIC(--cbs->treeSize);

  /* make invalid */
  block->node.limit = block->node.base;

  PoolFree(cbs->blockPool, (Addr)block, sizeof(CBSBlockStruct));

  return;
}

static void cbsBlockShrunk(CBS cbs, CBSBlock block, Size oldSize)
{
  Size newSize;

  /* AVERT(CBS, cbs); */
  AVERT(CBSBlock, block);

  newSize = CBSBlockSize(block);
  AVER(oldSize > newSize);

  if (cbs->fastFind) {
    RTreeRefresh(treeOfCBS(cbs), nodeOfCBSBlock(block));
    AVER(CBSBlockSize(block) <= block->maxSize);
  }
}

static void cbsBlockGrew(CBS cbs, CBSBlock block, Size oldSize)
{
  Size newSize;

  /* AVERT(CBS, cbs); */
  AVERT(CBSBlock, block);

  newSize = CBSBlockSize(block);
  AVER(oldSize < newSize);

  if (cbs->fastFind) {
    RTreeRefresh(treeOfCBS(cbs), nodeOfCBSBlock(block));
    AVER(CBSBlockSize(block) <= block->maxSize);
  }
}

/* cbsBlockAlloc -- allocate a new block and set its base and limit,
   but do not insert it into the tree yet */

static Res cbsBlockAlloc(CBSBlock *blockReturn, CBS cbs, Range range)
{
  Res res;
  CBSBlock block;
  Addr p;

  AVER(blockReturn != NULL);
  AVERT(CBS, cbs);
  AVERT(Range, range);

  res = PoolAlloc(&p, cbs->blockPool, sizeof(CBSBlockStruct),
                  /* withReservoirPermit */ FALSE);
  if (res != ResOK)
    goto failPoolAlloc;
  block = (CBSBlock)p;

  RNodeInit(nodeOfCBSBlock(block), RangeBase(range), RangeLimit(range));
  block->maxSize = CBSBlockSize(block);

  AVERT(CBSBlock, block);
  *blockReturn = block;
  return ResOK;

failPoolAlloc:
  AVER(res != ResOK);
  return res;
}

/* cbsBlockInsert -- insert a block into the tree */

static void cbsBlockInsert(CBS cbs, CBSBlock block)
{
  AVERT(CBS, cbs);
  AVERT(CBSBlock, block);

  METER_ACC(cbs->treeSearch, cbs->treeSize);
  RTreeInsert(treeOfCBS(cbs), nodeOfCBSBlock(block));
  STATISTIC(++cbs->treeSize);
}


/* cbsInsertIntoTree -- Insert a range into the tree */

static Res cbsInsertIntoTree(Range rangeReturn, CBS cbs, Range range)
{
  Res res;
  Addr base, limit, newBase, newLimit;
  RNode leftNode, rightNode;
  CBSBlock leftCBS, rightCBS;
  Bool leftMerge, rightMerge;
  Size oldSize;

  AVER(rangeReturn != NULL);
  AVERT(CBS, cbs);
  AVERT(Range, range);
  AVER(RangeIsAligned(range, cbs->alignment));

  base = RangeBase(range);
  limit = RangeLimit(range);

  METER_ACC(cbs->treeSearch, cbs->treeSize);
  
  if (!RTreeNeighbours(&leftNode, &rightNode, treeOfCBS(cbs), base)) {
    res = ResFAIL;
    goto fail;
  }
  
  /* Note, one neighbour will be at the tree root, and the other its
     direct child. */
  
  /* The two cases below are not quite symmetrical, because base was
   * passed into the call to SplayTreeNeighbours(), but limit was not.
   * So we know that if there is a left neighbour, then leftCBS->limit
   * <= base (this is ensured by cbsCompare, which is the
   * comparison method on the tree). But if there is a right
   * neighbour, all we know is that base < rightCBS->base. But for the
   * range to fit, we need limit <= rightCBS->base too. Hence the extra
   * check and the possibility of failure in the second case.
   */
  if (leftNode == NULL) {
    leftCBS = NULL;
    leftMerge = FALSE;
  } else {
    leftCBS = cbsBlockOfNode(leftNode);
    AVER(CBSBlockLimit(leftCBS) <= base);
    leftMerge = CBSBlockLimit(leftCBS) == base;
  }

  if (rightNode == NULL) {
    rightCBS = NULL;
    rightMerge = FALSE;
  } else {
    rightCBS = cbsBlockOfNode(rightNode);
    if (rightCBS != NULL && limit > CBSBlockLimit(rightCBS)) {
      res = ResFAIL;
      goto fail;
    }
    rightMerge = CBSBlockBase(rightCBS) == limit;
  }

  newBase = leftMerge ? CBSBlockBase(leftCBS) : base;
  newLimit = rightMerge ? CBSBlockLimit(rightCBS) : limit;
  
  /* .node.poke: Note that it's OK to directly change the ranges in the tree
     nodes because we know that we're not making them overlap, so the tree's
     invariants are not violated. */

  if (leftMerge && rightMerge) {
    Size oldLeftSize = CBSBlockSize(leftCBS);
    Addr rightLimit = CBSBlockLimit(rightCBS);
    cbsBlockDelete(cbs, rightCBS);
    leftCBS->node.limit = rightLimit;
    cbsBlockGrew(cbs, leftCBS, oldLeftSize);

  } else if (leftMerge) {
    oldSize = CBSBlockSize(leftCBS);
    leftCBS->node.limit = limit;
    cbsBlockGrew(cbs, leftCBS, oldSize);

  } else if (rightMerge) {
    oldSize = CBSBlockSize(rightCBS);
    rightCBS->node.base = base;
    cbsBlockGrew(cbs, rightCBS, oldSize);

  } else {
    CBSBlock block;
    res = cbsBlockAlloc(&block, cbs, range);
    if (res != ResOK)
      goto fail;
    cbsBlockInsert(cbs, block);
  }

  AVER(newBase <= base);
  AVER(newLimit >= limit);
  RangeInit(rangeReturn, newBase, newLimit);

  return ResOK;

fail:
  AVER(res != ResOK);
  return res;
}


/* CBSInsert -- Insert a range into the CBS
 *
 * See <design/cbs/#functions.cbs.insert>.
 */

Res CBSInsert(Range rangeReturn, CBS cbs, Range range)
{
  Res res;

  AVERT(CBS, cbs);
  cbsEnter(cbs);

  AVER(rangeReturn != NULL);
  AVERT(Range, range);
  AVER(RangeIsAligned(range, cbs->alignment));

  res = cbsInsertIntoTree(rangeReturn, cbs, range);

  cbsLeave(cbs);
  return res;
}


/* cbsDeleteFromTree -- delete blocks from the tree */

static Res cbsDeleteFromTree(Range rangeReturn, CBS cbs, Range range)
{
  Res res;
  CBSBlock cbsBlock;
  RNode node = NULL;
  Addr base, limit, oldBase, oldLimit;
  Size oldSize;

  AVER(rangeReturn != NULL);
  AVERT(CBS, cbs);
  AVERT(Range, range);
  AVER(RangeIsAligned(range, cbs->alignment));

  base = RangeBase(range);
  limit = RangeLimit(range);

  METER_ACC(cbs->treeSearch, cbs->treeSize);
  if (!RTreeFind(&node, treeOfCBS(cbs), base)) {
    res = ResFAIL;
    goto failSplayTreeSearch;
  }
    
  cbsBlock = cbsBlockOfNode(node);

  if (limit > CBSBlockLimit(cbsBlock)) {
    res = ResFAIL;
    goto failLimitCheck;
  }

  oldBase = CBSBlockBase(cbsBlock);
  oldLimit = CBSBlockLimit(cbsBlock);
  oldSize = CBSBlockSize(cbsBlock);
  RangeInit(rangeReturn, oldBase, oldLimit);

  if (base == oldBase && limit == oldLimit) {
    /* entire block */
    cbsBlockDelete(cbs, cbsBlock);

  } else if (base == oldBase) {
    /* remaining fragment at right */
    AVER(limit < oldLimit);
    cbsBlock->node.base = limit; /* .node.poke */
    cbsBlockShrunk(cbs, cbsBlock, oldSize);

  } else if (limit == oldLimit) {
    /* remaining fragment at left */
    AVER(base > oldBase);
    cbsBlock->node.limit = base;  /* .node.poke */
    cbsBlockShrunk(cbs, cbsBlock, oldSize);

  } else {
    /* two remaining fragments. shrink block to represent fragment at
       left, and create new block for fragment at right. */
    RangeStruct newRange;
    CBSBlock newBlock;
    AVER(base > oldBase);
    AVER(limit < oldLimit);
    RangeInit(&newRange, limit, oldLimit);
    res = cbsBlockAlloc(&newBlock, cbs, &newRange);
    if (res != ResOK) {
      goto failAlloc;
    }
    cbsBlock->node.limit = base; /* .node.poke */
    cbsBlockShrunk(cbs, cbsBlock, oldSize);
    cbsBlockInsert(cbs, newBlock);
  }

  return ResOK;

failAlloc:
failLimitCheck:
failSplayTreeSearch:
  AVER(res != ResOK);
  return res;
}


/* CBSDelete -- Remove a range from a CBS
 *
 * See <design/cbs/#function.cbs.delete>.
 */

Res CBSDelete(Range rangeReturn, CBS cbs, Range range)
{
  Res res;

  AVERT(CBS, cbs);
  cbsEnter(cbs);

  AVER(rangeReturn != NULL);
  AVERT(Range, range);
  AVER(RangeIsAligned(range, cbs->alignment));

  res = cbsDeleteFromTree(rangeReturn, cbs, range);

  cbsLeave(cbs);
  return res;
}


#if 0
static Res cbsBlockDescribe(CBSBlock block, mps_lib_FILE *stream)
{
  Res res;

  if (stream == NULL) return ResFAIL;

  res = WriteF(stream,
               "[$P,$P) {$U}",
               (WriteFP)CBSBlockBase(block),
               (WriteFP)CBSBlockLimit(block),
               (WriteFU)block->maxSize,
               NULL);
  return res;
}

static Res cbsSplayNodeDescribe(SplayNode node, mps_lib_FILE *stream)
{
  Res res;

  if (node == NULL) return ResFAIL;
  if (stream == NULL) return ResFAIL;

  res = cbsBlockDescribe(cbsBlockOfNode(node), stream);
  return res;
}
#endif


/* CBSIterate -- Iterate all blocks in CBS
 *
 * This is not necessarily efficient.
 * See <design/cbs/#function.cbs.iterate>.
 */

void CBSIterate(CBS cbs, CBSIterateMethod iterate,
                void *closureP, Size closureS)
{
  RNode node;
  Addr next;
  CBSBlock cbsBlock;

  AVERT(CBS, cbs);
  cbsEnter(cbs);
  AVER(FUNCHECK(iterate));

  /* .splay-iterate.slow: We assume that splay tree iteration does */
  /* searches and meter it.  FIXME: True of RTrees? */
  METER_ACC(cbs->treeSearch, cbs->treeSize);

  RTREE_FOR(node, treeOfCBS(cbs), next) {
    RangeStruct range;
    cbsBlock = cbsBlockOfNode(node);
    RangeInit(&range, CBSBlockBase(cbsBlock), CBSBlockLimit(cbsBlock));
    if (!(*iterate)(cbs, &range, closureP, closureS))
      break;
    METER_ACC(cbs->treeSearch, cbs->treeSize);
    AVERT(CBS, cbs);
  }

  cbsLeave(cbs);
  return;
}


/* FindDeleteCheck -- check method for a FindDelete value */

Bool FindDeleteCheck(FindDelete findDelete)
{
  CHECKL(findDelete == FindDeleteNONE
         || findDelete == FindDeleteLOW
         || findDelete == FindDeleteHIGH
         || findDelete == FindDeleteENTIRE);
  UNUSED(findDelete); /* <code/mpm.c#check.unused> */

  return TRUE;
}


/* cbsFindDeleteRange -- delete appropriate range of block found */

static void cbsFindDeleteRange(Range rangeReturn, Range oldRangeReturn,
                               CBS cbs, Range range, Size size,
                               FindDelete findDelete)
{
  Bool callDelete = TRUE;
  Addr base, limit;

  AVER(rangeReturn != NULL);
  AVER(oldRangeReturn != NULL);
  AVERT(CBS, cbs);
  AVERT(Range, range);
  AVER(RangeIsAligned(range, cbs->alignment));
  AVER(size > 0);
  AVER(SizeIsAligned(size, cbs->alignment));
  AVER(RangeSize(range) >= size);
  AVERT(FindDelete, findDelete);

  base = RangeBase(range);
  limit = RangeLimit(range);

  switch(findDelete) {

  case FindDeleteNONE:
    callDelete = FALSE;
    break;

  case FindDeleteLOW:
    limit = AddrAdd(base, size);
    break;

  case FindDeleteHIGH:
    base = AddrSub(limit, size);
    break;

  case FindDeleteENTIRE:
    /* do nothing */
    break;

  default:
    NOTREACHED;
    break;
  }

  RangeInit(rangeReturn, base, limit);

  if (callDelete) {
    Res res;
    res = cbsDeleteFromTree(oldRangeReturn, cbs, rangeReturn);
    /* Can't have run out of memory, because all our callers pass in
       blocks that were just found in the tree, and we only
       deleted from one end of the block, so cbsDeleteFromTree did not
       need to allocate a new block. */
    AVER(res == ResOK);
  } else {
    /* FIXME: Implement RangeCopy macro */
    mps_lib_memcpy(oldRangeReturn, rangeReturn, sizeof(RangeStruct));
  }
}


/* CBSFindFirst -- find the first block of at least the given size */

Bool CBSFindFirst(Range rangeReturn, Range oldRangeReturn,
                  CBS cbs, Size size, FindDelete findDelete)
{
  Bool found;
  RNode node;

  AVERT(CBS, cbs);
  cbsEnter(cbs);

  AVER(rangeReturn != NULL);
  AVER(oldRangeReturn != NULL);
  AVER(size > 0);
  AVER(SizeIsAligned(size, cbs->alignment));
  AVER(cbs->fastFind);
  AVERT(FindDelete, findDelete);

  METER_ACC(cbs->treeSearch, cbs->treeSize);
  found = RTreeFindFirst(&node, treeOfCBS(cbs), &cbsTestNode,
                         &cbsTestTree, NULL, size);
  if (found) {
    CBSBlock block;
    RangeStruct range;
    block = cbsBlockOfNode(node);
    AVER(CBSBlockSize(block) >= size);
    RangeInit(&range, CBSBlockBase(block), CBSBlockLimit(block));
    AVER(RangeSize(&range) >= size);
    cbsFindDeleteRange(rangeReturn, oldRangeReturn, cbs, &range,
                       size, findDelete);
  }

  cbsLeave(cbs);
  return found;
}


Bool CBSFindLast(Range rangeReturn, Range oldRangeReturn,
                 CBS cbs, Size size, FindDelete findDelete)
{
  UNUSED(rangeReturn);
  UNUSED(oldRangeReturn);
  UNUSED(cbs);
  UNUSED(size);
  UNUSED(findDelete);
  NOTREACHED;
  return FALSE;
}

Bool CBSFindLargest(Range rangeReturn, Range oldRangeReturn,
                    CBS cbs, Size size, FindDelete findDelete)
{
  UNUSED(rangeReturn);
  UNUSED(oldRangeReturn);
  UNUSED(cbs);
  UNUSED(size);
  UNUSED(findDelete);
  NOTREACHED;
  return FALSE;
}

Res CBSDescribe(CBS cbs, mps_lib_FILE *stream)
{
  UNUSED(cbs);
  UNUSED(stream);
  NOTREACHED;
  return ResUNIMPL;
}

#if 0

/* CBSFindLast -- find the last block of at least the given size */

Bool CBSFindLast(Range rangeReturn, Range oldRangeReturn,
                 CBS cbs, Size size, FindDelete findDelete)
{
  Bool found;
  SplayNode node;

  AVERT(CBS, cbs);
  cbsEnter(cbs);

  AVER(rangeReturn != NULL);
  AVER(oldRangeReturn != NULL);
  AVER(size > 0);
  AVER(SizeIsAligned(size, cbs->alignment));
  AVER(cbs->fastFind);
  AVERT(FindDelete, findDelete);

  METER_ACC(cbs->treeSearch, cbs->treeSize);
  found = RTreeFindLast(&node, treeOfCBS(cbs), &cbsTestNode,
                        &cbsTestTree, NULL, size);
  if (found) {
    CBSBlock block;
    RangeStruct range;
    block = cbsBlockOfNode(node);
    AVER(CBSBlockSize(block) >= size);
    RangeInit(&range, CBSBlockBase(block), CBSBlockLimit(block));
    AVER(RangeSize(&range) >= size);
    cbsFindDeleteRange(rangeReturn, oldRangeReturn, cbs, &range,
                       size, findDelete);
  }

  cbsLeave(cbs);
  return found;
}


/* CBSFindLargest -- find the largest block in the CBS */

Bool CBSFindLargest(Range rangeReturn, Range oldRangeReturn,
                    CBS cbs, Size size, FindDelete findDelete)
{
  Bool found = FALSE;
  SplayNode root;
  Bool notEmpty;

  AVERT(CBS, cbs);
  cbsEnter(cbs);

  AVER(rangeReturn != NULL);
  AVER(oldRangeReturn != NULL);
  AVER(cbs->fastFind);
  AVERT(FindDelete, findDelete);

  notEmpty = SplayRoot(&root, treeOfCBS(cbs));
  if (notEmpty) {
    RangeStruct range;
    CBSBlock block;
    SplayNode node = NULL;    /* suppress "may be used uninitialized" */
    Size maxSize;

    maxSize = cbsBlockOfNode(root)->maxSize;
    if (maxSize >= size) {
      METER_ACC(cbs->treeSearch, cbs->treeSize);
      found = SplayFindFirst(&node, treeOfCBS(cbs), &cbsTestNode,
                             &cbsTestTree, NULL, maxSize);
      AVER(found); /* maxSize is exact, so we will find it. */
      block = cbsBlockOfNode(node);
      AVER(CBSBlockSize(block) >= maxSize);
      RangeInit(&range, CBSBlockBase(block), CBSBlockLimit(block));
      AVER(RangeSize(&range) >= maxSize);
      cbsFindDeleteRange(rangeReturn, oldRangeReturn, cbs, &range,
                         maxSize, findDelete);
    }
  }

  cbsLeave(cbs);
  return found;
}


/* CBSDescribe -- describe a CBS
 *
 * See <design/cbs/#function.cbs.describe>.
 */

Res CBSDescribe(CBS cbs, mps_lib_FILE *stream)
{
  Res res;

  if (!TESTT(CBS, cbs)) return ResFAIL;
  if (stream == NULL) return ResFAIL;

  res = WriteF(stream,
               "CBS $P {\n", (WriteFP)cbs,
               "  alignment: $U\n", (WriteFU)cbs->alignment,
               "  blockPool: $P\n", (WriteFP)cbs->blockPool,
               "  fastFind: $U\n", (WriteFU)cbs->fastFind,
               "  inCBS: $U\n", (WriteFU)cbs->inCBS,
               "  treeSize: $U\n", (WriteFU)cbs->treeSize,
               NULL);
  if (res != ResOK) return res;

  res = RTreeDescribe(treeOfCBS(cbs), stream, &cbsSplayNodeDescribe);
  if (res != ResOK) return res;

  res = METER_WRITE(cbs->treeSearch, stream);
  if (res != ResOK) return res;

  res = WriteF(stream, "}\n", NULL);
  return res;
}

#endif


/* C. COPYRIGHT AND LICENSE
 *
 * Copyright (C) 2001-2013 Ravenbrook Limited <http://www.ravenbrook.com/>.
 * All rights reserved.  This is an open source license.  Contact
 * Ravenbrook for commercial licensing options.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. Redistributions in any form must be accompanied by information on how
 * to obtain complete source code for this software and any accompanying
 * software that uses this software.  The source code must either be
 * included in the distribution or be available for no more than the cost
 * of distribution plus a nominal fee, and must be freely redistributable
 * under reasonable conditions.  For an executable file, complete source
 * code means the source code for all modules it contains. It does not
 * include source code for modules or files that typically accompany the
 * major components of the operating system on which the executable file
 * runs.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE, OR NON-INFRINGEMENT, ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS AND CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
