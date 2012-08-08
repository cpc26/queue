/* Copyright (C) 1991 Free Software Foundation, Inc.
This file is part of the GNU C Library.

The GNU C Library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The GNU C Library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with the GNU C Library; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 675 Mass Ave,
Cambridge, MA 02139, USA.  */

/*Hacked on 1999/01/26 by WGK for GNU Queue for systems which lack sigblock.*/

#include "queue.h"

#undef __sigmask
#define __sigmask(sig) \
  (((unsigned long) 1) << (((sig) - 1) % (8 * sizeof (unsigned long int))))

#undef __sigaddset
#define __sigaddset(set, sig)   ((*(set) |= __sigmask (sig)), 0)

#undef __sigemptyset
#define __sigemptyset(set)	((*(set) = 0L), 0)

#undef __sigismember
#define __sigismember(set, sig) ((*(set) & __sigmask (sig)) ? 1 : 0)

/* Block signals in MASK, returning the old mask.  */
int sigblock (mask)
int mask;
{
  register int sig;
  sigset_t set, oset;

  if (__sigemptyset((unsigned long*) &set) < 0)
    return -1;
  
  if (sizeof (mask) == sizeof (set))
    set = *((sigset_t*) &mask);
  else
    for (sig = 1; sig < NSIG; ++sig)
      if ((mask & sigmask(sig)) &&
	  __sigaddset((unsigned long*)&set, sig) < 0)
	return -1;

  if (sigprocmask(SIG_BLOCK, &set, &oset) < 0)
    return -1;

  mask = 0;
  if (sizeof (mask) == sizeof (oset))
    mask = *((int*) &oset);
  else
    for (sig = 1; sig < NSIG; ++sig)
      if (__sigismember((unsigned long*) &oset, sig))
	mask |= sigmask(sig);

  return mask;
}


