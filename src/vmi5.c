/* impl.c.vmso: VIRTUAL MEMORY MAPPING FOR IRIX 5 (AND 6)
 *
 * $HopeName$
 * Copyright (C) 1995,1997 Harlequin Group, all rights reserved
 *
 * Design: design.mps.vm
 *
 * This is the implementation of the virtual memory mapping interface
 * (vm.h) for IRIX 5.x.
 *
 * mmap(2) is used to reserve address space by creating a mapping to
 * /etc/passwd with page access none.  mmap(2) is used to map pages
 * onto store by creating a copy-on-write mapping to /dev/zero.
 *
 * Attempting to reserve address space by mapping /dev/zero results in
 * swap being reserved.  So, we use a shared mapping of /etc/passwd,
 * the only file we can think of which is pretty much guaranteed to be
 * around.
 *
 * .assume.not-last: The implementation of VMCreate assumes that
 *   mmap() will not choose a region which contains the last page
 *   in the address space, so that the limit of the mapped area
 *   is representable.
 *
 * .assume.size: The maximum size of the reserved address space
 *   is limited by the range of "int".  This will probably be half
 *   of the address space.@@@@
 *
 * .assume.mmap.err: EAGAIN is the only error we really expect to
 *   get from mmap.  The others are either caused by invalid params
 *   or features we don't use.  See mmap(2) for details.
 *
 * TRANSGRESSIONS
 *
 * .fildes.name: VMStruct has two fields whose names violate our
 * naming conventions.  They are called none_fd and zero_fd to
 * emphasize that they are file descriptors and this fact is not
 * reflected in their type.  */

#include "mpm.h"

#ifndef MPS_OS_I5
#error "vmi5.c is IRIX 5 specific, but MPS_OS_I5 is not set"
#endif

/* Open sesame magic @@@@ */
#define _POSIX_SOURCE

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h> /* for _SC_PAGESIZE */

SRCID(vmi5, "$HopeName$");


/* VMStruct -- virtual memory structure */

#define VMSig           ((Sig)0x519B3999) /* SIGnature VM */

/* The names of zero_fd and none_fd are transgressions, see .fildes.name */
typedef struct VMStruct {
  Sig sig;                      /* design.mps.sig */
  int zero_fd;                  /* fildes for mmap */
  int none_fd;                  /* fildes for mmap */
  Align align;                  /* page size */
  Addr base, limit;             /* boundaries of reserved space */
  Size reserved;                /* total reserved address space */
  Size mapped;                  /* total mapped memory */
} VMStruct;


Align VMAlign(void)
{
  Align align;

  align = (Align)sysconf(_SC_PAGESIZE);
  AVER(SizeIsP2(align));

  return align;
}


Bool VMCheck(VM vm)
{
  CHECKS(VM, vm);
  CHECKL(vm->zero_fd >= 0);
  CHECKL(vm->none_fd >= 0);
  CHECKL(vm->zero_fd != vm->none_fd);
  CHECKL(vm->base != 0);
  CHECKL(vm->limit != 0);
  CHECKL(vm->base < vm->limit);
  CHECKL(vm->mapped <= vm->reserved);
  CHECKL(SizeIsP2(vm->align));
  CHECKL(AddrIsAligned(vm->base, vm->align));
  CHECKL(AddrIsAligned(vm->limit, vm->align));
  return TRUE;
}


Res VMCreate(VM *vmReturn, Size size)
{
  void *addr;
  Align align;
  int zero_fd, none_fd;
  VM vm;
  Res res;

  align = VMAlign();

  AVER(vmReturn != NULL);
  AVER(SizeIsAligned(size, align));
  AVER(size != 0);
  AVER(size <= INT_MAX); /* see .assume.size */

  zero_fd = open("/dev/zero", O_RDONLY);
  if(zero_fd == -1)
    return ResFAIL;
  none_fd = open("/etc/passwd", O_RDONLY);
  if(none_fd == -1) {
    res = ResFAIL;
    goto failNoneFd;
  }

  /* Map in a page to store the descriptor on. */
  addr = mmap((void *)0, (size_t)SizeAlignUp(sizeof(VMStruct), align),
              PROT_READ | PROT_WRITE, MAP_PRIVATE,
              zero_fd, (off_t)0);
  if(addr == (void *)-1) {
    AVER(errno == EAGAIN); /* .assume.mmap.err */
    res = (errno == EAGAIN) ? ResMEMORY : ResFAIL;
    goto failVMMap;
  }
  vm = (VM)addr;

  vm->zero_fd = zero_fd;
  vm->none_fd = none_fd;
  vm->align = align;

  /* .map.reserve: See .assume.not-last. */
  addr = mmap((void *)0, (size_t)size, PROT_NONE, MAP_SHARED,
	      none_fd, (off_t)0);
  if(addr == (void *)-1) {
    AVER(errno == EAGAIN); /* .assume.mmap.err */
    res = (errno == EAGAIN) ? ResRESOURCE : ResFAIL;
    goto failReserve;
  }

  vm->base = (Addr)addr;
  vm->limit = AddrAdd(vm->base, size);
  vm->reserved = size;
  vm->mapped = (Size)0;

  vm->sig = VMSig;

  AVERT(VM, vm);

  EVENT_PAA(VMCreate, vm, vm->base, vm->limit);

  *vmReturn = vm;
  return ResOK;

failReserve:
  (void)munmap((void *)vm, (size_t)SizeAlignUp(sizeof(VMStruct), align));
failVMMap:
  (void)close(none_fd);
failNoneFd:
  (void)close(zero_fd);
  return res;
}


void VMDestroy(VM vm)
{
  int r;
  int zero_fd, none_fd;

  AVERT(VM, vm);
  AVER(vm->mapped == (Size)0);

  /* This appears to be pretty pointless, since the descriptor */
  /* page is about to vanish completely.  However, munmap might fail */
  /* for some reason, and this would ensure that it was still */
  /* discovered if sigs were being checked. */
  vm->sig = SigInvalid;

  none_fd = vm->none_fd;
  zero_fd = vm->zero_fd;
  r = munmap((void *)vm->base, (size_t)AddrOffset(vm->base, vm->limit));
  AVER(r == 0);
  r = munmap((void *)vm, (size_t)SizeAlignUp(sizeof(VMStruct), vm->align));
  AVER(r == 0);
  r = close(none_fd);
  AVER(r == 0);
  r = close(zero_fd);
  AVER(r == 0);

  EVENT_P(VMDestroy, vm);
}


Addr VMBase(VM vm)
{
  AVERT(VM, vm);
  return vm->base;
}

Addr VMLimit(VM vm)
{
  AVERT(VM, vm);
  return vm->limit;
}


Size VMReserved(VM vm)
{
  AVERT(VM, vm);
  return vm->reserved;
}

Size VMMapped(VM vm)
{
  AVERT(VM, vm);
  return vm->mapped;
}


Res VMMap(VM vm, Addr base, Addr limit)
{
  Size size;

  AVERT(VM, vm);
  AVER(sizeof(int) == sizeof(Addr));
  AVER(base < limit);
  AVER(base >= vm->base);
  AVER(limit <= vm->limit);
  AVER(AddrOffset(base, limit) <= INT_MAX);
  AVER(AddrIsAligned(base, vm->align));
  AVER(AddrIsAligned(limit, vm->align));

  /* Map /dev/zero onto the area with a copy-on-write policy.  This */
  /* effectively populates the area with zeroed memory. */
  size = AddrOffset(base, limit);
  if(mmap((void *)base, (size_t)size,
	  PROT_READ | PROT_WRITE | PROT_EXEC,
	  MAP_PRIVATE | MAP_FIXED,
	  vm->zero_fd, (off_t)0)
     == (void *)-1) {
    AVER(errno == EAGAIN); /* .assume.mmap.err */
    return ResMEMORY;
  }

  vm->mapped += size;

  return ResOK;
}


void VMUnmap(VM vm, Addr base, Addr limit)
{
  Size size;
  void *addr;

  AVERT(VM, vm);
  AVER(sizeof(int) == sizeof(Addr));
  AVER(base < limit);
  AVER(base >= vm->base);
  AVER(limit <= vm->limit);
  AVER(AddrIsAligned(base, vm->align));
  AVER(AddrIsAligned(limit, vm->align));

  /* Map /etc/passwd onto the area, allowing no access.  This */
  /* effectively depopulates the area from memory, but keeps */
  /* it "busy" as far as the OS is concerned, so that it will not */
  /* be re-used by other calls to mmap which do not specify */
  /* MAP_FIXED.  The offset is specified to mmap so that */
  /* the OS merges this mapping with .map.reserve. */
  size = AddrOffset(base, limit);
  addr = mmap((void *)base, (size_t)size,
              PROT_NONE, MAP_SHARED | MAP_FIXED,
              vm->none_fd, (off_t)AddrOffset(vm->base, base));
  AVER(addr == (void *)base);

  vm->mapped -= size;
}
