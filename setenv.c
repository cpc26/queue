/* Copyright (C) 1992, 1995, 1996, 1997 Free Software Foundation, Inc.
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
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#if !_LIBC
# if !defined(errno) && !defined(HAVE_ERRNO_DECL)
extern int errno;
# endif
# define __set_errno(ev) ((errno) = (ev))
#endif

#if _LIBC || HAVE_STDLIB_H
# include <stdlib.h>
#endif
#if _LIBC || HAVE_STRING_H
# include <string.h>
#endif
#if _LIBC || HAVE_UNISTD_H
# include <unistd.h>
#endif

#if !_LIBC
# define __environ	environ
# ifndef HAVE_ENVIRON_DECL
extern char **environ;
# endif
#endif

#if _LIBC
/* This lock protects against simultaneous modifications of `environ'.  */
# include <libc-lock.h>
__libc_lock_define_initialized (static, envlock)
# define LOCK	__libc_lock_lock (envlock)
# define UNLOCK	__libc_lock_unlock (envlock)
#else
# define LOCK
# define UNLOCK
#endif

/* In the GNU C library we must keep the namespace clean.  */
#ifdef _LIBC
# define clearenv __clearenv
#endif


/* If this variable is not a null pointer we allocated the current
   environment.  */
static char **last_environ;


int
setenv (name, value, replace)
     const char *name;
     const char *value;
     int replace;
{
  register char **ep;
  register size_t size;
  const size_t namelen = strlen (name);
  const size_t vallen = strlen (value) + 1;

  LOCK;

  size = 0;
  if (__environ != NULL)
    for (ep = __environ; *ep != NULL; ++ep)
      if (!strncmp (*ep, name, namelen) && (*ep)[namelen] == '=')
	break;
      else
	++size;

  if (__environ == NULL || *ep == NULL)
    {
      char **new_environ;
      if (__environ == last_environ && __environ != NULL)
	/* We allocated this space; we can extend it.  */
	new_environ = (char **) realloc (last_environ,
					 (size + 2) * sizeof (char *));
      else
	new_environ = (char **) malloc ((size + 2) * sizeof (char *));

      if (new_environ == NULL)
	{
	  UNLOCK;
	  return -1;
	}

      new_environ[size] = malloc (namelen + 1 + vallen);
      if (new_environ[size] == NULL)
	{
	  free ((char *) new_environ);
	  __set_errno (ENOMEM);
	  UNLOCK;
	  return -1;
	}

      if (__environ != last_environ)
	memcpy ((char *) new_environ, (char *) __environ,
		size * sizeof (char *));

      memcpy (new_environ[size], name, namelen);
      new_environ[size][namelen] = '=';
      memcpy (&new_environ[size][namelen + 1], value, vallen);

      new_environ[size + 1] = NULL;

      last_environ = __environ = new_environ;
    }
  else if (replace)
    {
      size_t len = strlen (*ep);
      if (len + 1 < namelen + 1 + vallen)
	{
	  /* The existing string is too short; malloc a new one.  */
	  char *new = malloc (namelen + 1 + vallen);
	  if (new == NULL)
	    {
	      UNLOCK;
	      return -1;
	    }
	  *ep = new;
	  memcpy (*ep, name, namelen);
	  (*ep)[namelen] = '=';
	}
      memcpy (&(*ep)[namelen + 1], value, vallen);
    }

  UNLOCK;

  return 0;
}

void
unsetenv (name)
     const char *name;
{
  const size_t len = strlen (name);
  char **ep;

  LOCK;

  for (ep = __environ; *ep; ++ep)
    if (!strncmp (*ep, name, len) && (*ep)[len] == '=')
      {
	/* Found it.  Remove this pointer by moving later ones back.  */
	char **dp = ep;
	do
	  dp[0] = dp[1];
	while (*dp++);
	/* Continue the loop in case NAME appears again.  */
      }

  UNLOCK;
}

/* The `clearenv' was planned to be added to POSIX.1 but probably
   never made it.  Nevertheless the POSIX.9 standard (POSIX bindings
   for Fortran 77) requires this function.  */
int
clearenv ()
{
  LOCK;

  if (__environ == last_environ && __environ != NULL)
    {
      /* We allocated this environment so we can free it.  */
      free (__environ);
      last_environ = NULL;
    }

  /* Clear the environment pointer removes the whole environment.  */
  __environ = NULL;

  UNLOCK;

  return 0;
}
#ifdef _LIBC
# undef clearenv
weak_alias (__clearenv, clearenv)
#endif
