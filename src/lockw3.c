/*  impl.c.locknt
 *
 *                  RECURSIVE LOCKS IN WIN32
 *
 *  $HopeName: MMsrc!locknt.c(MMdevel_assertid.2) $
 *
 *  Copyright (C) 1995 Harlequin Group, all rights reserved
 *
 *  These are implemented using critical sections.
 *  See the section titled "Synchronization functions" in the Groups
 *  chapter of the Microsoft Win32 API Programmer's Reference.
 *  The "Synchronization" section of the Overview is also relevant.
 *
 *  Critical sections support recursive locking, so the implementation
 *  could be trivial.  This implementation counts the claims to provide
 *  extra checking.
 *
 *  The limit on the number of recursive claims is the max of
 *  ULONG_MAX and the limit imposed by critical sections, which
 *  is believed to be about UCHAR_MAX.
 *
 *  During use the claims field is updated to remember the number of
 *  claims acquired on a lock.  This field must only be modified
 *  while we are inside the critical section.
 */

#include "mpm.h"

#ifndef MPS_OS_W3
#error "locknt.c is specific to Win32 but MPS_OS_W3 not defined"
#endif

#include <windows.h>

SRCID(locknt, "$HopeName: MMsrc!locknt.c(MMdevel_assertid.2) $");

Bool LockCheck(Lock lock)
{
  CHECKS(0x7C420000, Lock, lock);
  return TRUE;
}

void LockInit(Lock lock)
{
  AVER(0x7C420001, lock != NULL);
  lock->claims = 0;
  InitializeCriticalSection(&lock->cs);
  lock->sig = LockSig;
  AVERT(0x7C420002, Lock, lock);
}

void LockFinish(Lock lock)
{
  AVERT(0x7C420003, Lock, lock);
  /* Lock should not be finished while held */
  AVER(0x7C420004, lock->claims == 0);
  DeleteCriticalSection(&lock->cs);
  lock->sig = SigInvalid;
}

void LockClaim(Lock lock)
{
  AVERT(0x7C420005, Lock, lock);
  EnterCriticalSection(&lock->cs);
  /* This should be the first claim.  Now we are inside the
   * critical section it is ok to check this. */
  AVER(0x7C420006, lock->claims == 0);
  lock->claims = 1;
}

void LockReleaseMPM(Lock lock)
{
  AVERT(0x7C420007, Lock, lock);
  AVER(0x7C420008, lock->claims == 1);  /* The lock should only be held once */
  lock->claims = 0;  /* Must set this before leaving CS */
  LeaveCriticalSection(&lock->cs);
}

void LockClaimRecursive(Lock lock)
{
  AVERT(0x7C420009, Lock, lock);
  EnterCriticalSection(&lock->cs);
  ++lock->claims;
  AVER(0x7C42000A, lock->claims > 0);
}

void LockReleaseRecursive(Lock lock)
{
  AVERT(0x7C42000B, Lock, lock);
  AVER(0x7C42000C, lock->claims > 0);
  --lock->claims;
  LeaveCriticalSection(&lock->cs);
}
