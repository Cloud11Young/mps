/*  impl.c.vmnt: VIRTUAL MEMORY MAPPING FOR WIN32
 *
 *  $HopeName: MMsrc!vmnt.c(MMdevel_assertid.2) $
 *  Copyright (C) 1995 Harlequin Group, all rights reserved
 *
 *  Design: design.mps.vm
 *
 *  This is the implementation of the virtual memory mapping interface (vm.h)
 *  for Win32s.
 *
 *  The documentation for Win32 used is the "Win32 Programmer's Reference"
 *  provided with Microsoft Visual C++ 2.0.
 *
 *  VirtualAlloc is used to reserve address space and to "commit" (map)
 *  address ranges onto storage.  VirtualFree is used to release and
 *  "decommit" (unmap) pages.  These functions are documented in the
 *  Win32 SDK help, under System Services/Memory Management.
 *
 *  .assume.free.success:  We assume that VirtualFree will never return
 *    an error; this is because we always pass in legal parameters
 *    (hopefully).
 *
 *  .assume.not-last:  We assume that VirtualAlloc will never return
 *    a block of memory that occupies the last page in memory, so
 *    that limit is representable and bigger than base.
 *
 *  .assume.dword-addr:  We assume that the windows type DWORD and
 *    the MM type Addr are the same size.
 *
 *  .assume.dword-align:  We assume that the windows type DWORD and
 *    the MM type Align are the same size.
 *
 *  .assume.lpvoid-addr:  We assume that the windows type LPVOID and
 *    the MM type Addr are the same size.
 *
 *  .assume.sysalign:  The assume that the page size on the system
 *    is a power of two.
 *
 *  Notes
 *   1. GetSystemInfo returns a thing called szAllocationGranularity
 *      the purpose of which is unclear but which might affect the
 *      reservation of address space.  Experimentally, it does not.
 *      Microsoft's documentation is extremely unclear on this point.
 *      richard 1995-02-15
 */

#include "mpm.h"

#ifndef MPS_OS_W3
#error "vmnt.c is Win32 specific, but MPS_OS_W3 is not set"
#endif
#ifdef VM_RM
#error "vmnt.c compiled with VM_RM set"
#endif

#include <windows.h>

SRCID(vmnt, "$HopeName: MMsrc!vmnt.c(MMdevel_assertid.2) $");


#define SpaceVM(space)  (&(space)->arenaStruct.vmStruct)

Align VMAlign(void)
{
  Align align;
  SYSTEM_INFO si;

  /* See .assume.dword-align */
  AVER(0xF3420000, sizeof(DWORD) == sizeof(Align));

  GetSystemInfo(&si);
  align = (Align)si.dwPageSize;
  AVER(0xF3420001, SizeIsP2(align));    /* see .assume.sysalign */

  return align;
}


Bool VMCheck(VM vm)
{
  CHECKS(0xF3420002, VM, vm);
  CHECKL(0xF3420003, vm->base != 0);
  CHECKL(0xF3420004, vm->limit != 0);
  CHECKL(0xF3420005, vm->base < vm->limit);
  CHECKL(0xF3420006, vm->mapped <= vm->reserved);
  CHECKL(0xF3420007, AddrIsAligned(vm->base, vm->align));
  CHECKL(0xF3420008, AddrIsAligned(vm->limit, vm->align));
  return TRUE;
}


Res VMCreate(Space *spaceReturn, Size size, Addr base)
{
  LPVOID vbase;
  Align align;
  VM vm;
  Space space;

  AVER(0xF3420009, spaceReturn != NULL);
  AVER(0xF342000A, sizeof(LPVOID) == sizeof(Addr));  /* .assume.lpvoid-addr */

  /* See .assume.dword-addr */
  AVER(0xF342000B, sizeof(DWORD) == sizeof(Addr));

  align = VMAlign();
  AVER(0xF342000C, SizeIsP2(align));    /* see .assume.sysalign */

  AVER(0xF342000D, SizeIsAligned(size, align));
  AVER(0xF342000E, base == NULL);

  /* Allocate some store for the space descriptor.
   * This is likely to be wasteful see issue.vmnt.waste */
  vbase = VirtualAlloc(NULL, SizeAlignUp(sizeof(SpaceStruct), align),
          MEM_COMMIT, PAGE_READWRITE);
  if(vbase == NULL)
    return ResMEMORY;
  space = (Space)vbase;
  vm = SpaceVM(space);

  /* Allocate the address space. */
  vbase = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
  if(vbase == NULL)
    return ResRESOURCE;

  AVER(0xF342000F, AddrIsAligned(vbase, align));

  vm->align = align;
  vm->base = (Addr)vbase;
  vm->limit = AddrAdd(vbase, size);
  vm->reserved = size;
  vm->mapped = 0;
  AVER(0xF3420010, vm->base < vm->limit);  /* .assume.not-last */

  vm->sig = VMSig;

  AVERT(0xF3420011, VM, vm);

  *spaceReturn = space;
  return ResOK;
}


void VMDestroy(Space space)
{
  BOOL b;
  VM vm;

  vm = SpaceVM(space);
  AVERT(0xF3420012, VM, vm);
  AVER(0xF3420013, vm->mapped == 0);

  /* This appears to be pretty pointless, since the vm descriptor page
   * is about to vanish completely.  However, the VirtualFree might
   * fail and it would be nice to have a dead sig there. */
  vm->sig = SigInvalid;

  b = VirtualFree((LPVOID)vm->base, (DWORD)0, MEM_RELEASE);
  AVER(0xF3420014, b != 0);

  b = VirtualFree((LPVOID)space, (DWORD)0, MEM_RELEASE);
  AVER(0xF3420015, b != 0);
}


Addr VMBase(Space space)
{
  VM vm = SpaceVM(space);

  AVERT(0xF3420016, VM, vm);
  return vm->base;
}

Addr VMLimit(Space space)
{
  VM vm = SpaceVM(space);

  AVERT(0xF3420017, VM, vm);
  return vm->limit;
}


Size VMReserved(Space space)
{
  VM vm = SpaceVM(space);
  AVERT(0xF3420018, VM, vm);
  return vm->reserved;
}

Size VMMapped(Space space)
{
  VM vm = SpaceVM(space);
  AVERT(0xF3420019, VM, vm);
  return vm->mapped;
}


Res VMMap(Space space, Addr base, Addr limit)
{
  VM vm = SpaceVM(space);
  LPVOID b;
  Align align = vm->align;

  AVERT(0xF342001A, VM, vm);
  AVER(0xF342001B, AddrIsAligned(base, align));
  AVER(0xF342001C, AddrIsAligned(limit, align));
  AVER(0xF342001D, vm->base <= base);
  AVER(0xF342001E, base < limit);
  AVER(0xF342001F, limit <= vm->limit);

  /* .improve.query-map: We could check that the pages we are about to
   * map are unmapped using VirtualQuery. */

  b = VirtualAlloc((LPVOID)base, (DWORD)AddrOffset(base, limit),
       MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  if(b == NULL)
    return ResMEMORY;

  AVER(0xF3420020, (Addr)b == base);        /* base should've been aligned */

  vm->mapped += AddrOffset(base, limit);

  return ResOK;
}


void VMUnmap(Space space, Addr base, Addr limit)
{
  VM vm = SpaceVM(space);
  Align align = vm->align;
  BOOL b;

  AVERT(0xF3420021, VM, vm);
  AVER(0xF3420022, AddrIsAligned(base, align));
  AVER(0xF3420023, AddrIsAligned(limit, align));
  AVER(0xF3420024, vm->base <= base);
  AVER(0xF3420025, base < limit);
  AVER(0xF3420026, limit <= vm->limit);

  /* .improve.query-unmap: Could check that the pages we are about
   * to unmap are mapped using VirtualQuery. */

  b = VirtualFree((LPVOID)base, (DWORD)AddrOffset(base, limit), MEM_DECOMMIT);
  AVER(0xF3420027, b != 0);  /* .assume.free.success */
  vm->mapped -= AddrOffset(base, limit);
}
