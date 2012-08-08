/*
 *  Queue load balancing system
 *  $Revision: 1.6 $
 *
 *  Copyright (C) 1998-2000 W. G. Krebs <wkrebs@gnu.org>
 *  ReadHosts() Copyright (C) 1998 Free Software Foundation, Inc.
 *
 *  wkrebs@gnu.org
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 1, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  If you make modifications to the source, I would be happy to have
 *  them to include in future releases.  Feel free to send them to:
 *      W. G. Krebs
 *	wkrebs@gnu.org
 *
 **************************************************************************/

#define sethosts 1
#include "queue.h"

char cookie[80];

int
ReadHosts(char *HostFile)
{

  /*Routine to parse hosts from a file, by Dave van Leeuwen*/

  FILE *QHosts;
#define TmpStrSize 256
  char TmpStr[TmpStrSize];
  int i;

  {


    struct stat sbuf;

    /*First make sure that compilation properly defined HostFile.*/

    if(!HostFile || !*HostFile) {
      fprintf(stderr, "ReadHosts: Received null HostFile. Make sure QHOSTSFILE has been properly set in config.h.\nNormally, this is automatically done by `configure'.\nIf this was not done by `./configure' on your platform, this is a bug and you should report it to bug-queue@gnu.org\n\n");
      fprintf(stderr, "ReadHosts: Invalid HostFile. Exiting.\n");
      exit(2);
    }

    /*WGK make sure HostFile is owned by us and has restricted write
      permissions.*/
    stat(HostFile, &sbuf);

#ifdef NO_ROOT
    if (sbuf.st_uid != getuid())
      fprintf(stderr, "%s is not owned by the appropriate user uid: %d\n",
	      HostFile, getuid()), exit(1);
#else
    if (sbuf.st_uid != 0)
      fprintf(stderr, "%s is not owned by the appropriate user uid: %d\n",
	      HostFile, 0), exit(1);
#endif
    if ((sbuf.st_mode & S_IWGRP) | (sbuf.st_mode & S_IWOTH))
      fprintf(stderr, "%s: mode %03o allows general or group write\n",
	      HostFile, sbuf.st_mode), exit(1);
  }

  if((QHosts = fopen(HostFile, "r")) == NULL)
    {
      fprintf(stderr, "ERROR cannot open host file %s\n",HostFile);
      return(-1);
    }

  /*WGK: during cluster operation, we want to periodically re-read the
    ACL, say on a HUP signal, or whenever it has changed. So, we first
    free up the structure.*/

  if(Hosts) {
    for(i = 0;i < NHosts;i++)
      if(Hosts[i].host)
	free (Hosts[i].host);
    free(Hosts);
  }

  NHosts = 0;

  for(;;)
    {
      if(fgets(TmpStr, TmpStrSize - 2, QHosts) == NULL)
	break;
      {
	int j = 0;
	/*WGK: Skip over any white space before comments*/
	while(TmpStr[j] == ' ' | TmpStr[j] == '\t') {j++;}
	if(TmpStr[j] != '#' & TmpStr[j] != '\0')
	  NHosts++;
      }
    }

  if((Hosts = (struct wkstruct *)malloc(sizeof(struct wkstruct) * NHosts))
     == NULL)
    {
      fprintf(stderr, "ERROR allocating memory for hosts struct\n");
      exit(2);
    }

  if(fseek(QHosts, 0, SEEK_SET) != 0)
    {
      fprintf(stderr, "ERROR seeking file %s\n",HostFile);
      exit(2);
    }
  for(i = 0;i < NHosts;i++)
    {
      Hosts[i].host = NULL;
      Hosts[i].load = 1e09;
    }

  for(i = 0;i < NHosts;)
    {
      int j = 0, k;

      if(fgets(TmpStr, TmpStrSize - 2, QHosts) == NULL)
	break;
      TmpStr[TmpStrSize-1] = '\0';
      {
	/*WGK: Skip over any white space before comments*/
	while(TmpStr[j] == ' ' | TmpStr[j] == '\t') {j++;}
	if(TmpStr[j] == '#' | TmpStr[j] == '\0'){
	  NHosts--;
	  continue;
	}
      }
      /*WGK: Truncate host at first white space character (e.g., newline.)*/

      for(k=j;k<strlen(TmpStr);k++)
	if(TmpStr[k] == ' ' | TmpStr[k] == '\t' | TmpStr[k] == '#'
	   | TmpStr[k] == '\n' | TmpStr[k] == '\r') {
	TmpStr[k] = '\0';
	break;
      }

      /*WGK Don't do name lookup on null strings.*/
      if (!TmpStr[j]) {
	NHosts--;
	continue;
      }

      if((Hosts[i].host = strdup(canonicalhost(&TmpStr[j]))) == NULL)
	{
	  /*WGK 1999/01/25 Bugfix. This is probably a bogus host.*/
          fprintf(stderr,
		  "WARNING: Unable to resolve %s or out of memory\n",
		  TmpStr[j]);
          NHosts--;
	  continue;
        }
      i++;

    }
  return(fclose(QHosts));
}

char
*localhost(char *host)
{
  /*Author: WGK*/
  register char *i;
  register char *lhost;
  if(host==NULL)return(NULL);
  lhost=strdup(host);
  for(i=lhost;*i!='\0';++i) if (*i=='.') break;
  *i='\0';
  return(lhost);
}

int
countdots(char *host)
{
  int count = 0;
  char *ptr = host;
  while((ptr!=NULL)&&((ptr=index(ptr, '.'))!=NULL)) {
    count++;
    ptr++;
  }
  return(count);
}

char *
canonicalhost(char *host)
{
  char *ret;
  char *best;
  int maxdots = -1;
  int i;
  int tmp;
  static char cache[MAXHOSTNAMELEN + 1];

  /* WGK 1995

     Bugs fixed and changed to return the first name with most dots 1999/02/05.

     More bugs fixed. Added static cache for stuff WGK 1999/02/13 .


     This will save us problems when people upgrade their gethostname()
     from host to host.domainname during an OS upgrade. It has the additional
     advantage that it correctly distinguishes between otherwise identically
     named hosts on different domains, in case anyone ever wants to try to
     exploit that possible security hole,
     and resolves aliases to a single official name for each host.*/

  /*Move host string to our own static area in case host comes from static
    DNS libc areas. This code just lets us call canonicalhost on gethosbyname()
    stuff safely.*/
  struct hostent *thathost;

  strncpy(cache, host, MAXHOSTNAMELEN);
  cache[MAXHOSTNAMELEN-1] = 0;

  /*Appending the domainname from getdomainname() as someone suggested
    will not work because
    getdomainname is the NIS domain, not the BIND domain, and they are quite
    different in many installations! I've reluctantly given up and now
    simply return an unqualified domainname if we have a lazy gethostbyname().*/


  if((thathost = gethostbyname(cache))==NULL) {
    fprintf(stderr, "gethostbyname on %s returned error.\n", cache);
    return(cache);
  }

  best = NULL;

#define find_maxdots(a,b) if ((tmp=countdots(a)) > b) { b = tmp; best = a; }

  find_maxdots(cache, maxdots);
  find_maxdots(thathost->h_name, maxdots);
  for(i = 0;thathost->h_aliases[i]!=NULL;i++)
    find_maxdots(thathost->h_aliases[i],maxdots);

  /*Convert host to lowercase.*/
  if (best) {
    strncpy(cache, best, MAXHOSTNAMELEN);
    cache[MAXHOSTNAMELEN-1] = 0;
  }
  best=cache;
  for(ret=best;*ret!=0;ret++) *ret=tolower(*ret);
  return(best);
}



char *
mymalloc(int n)
{
  register char *p;
  /*extern char *malloc();*/

  if ((p = malloc((size_t)n)) == NULL)
    {
      fprintf(stderr, "Out of memory\n");
      exit(2);
    }
  return p;
}



char *
readcookie(void)
{
#ifdef NO_ROOT
  static int cookieread = 0;
  FILE *cookiefile;

  if(!cookieread) {
    struct stat sbuf;
    if(stat(COOKIEFILE, &sbuf)<0) {
      fprintf(stderr, "Couldn't stat cookiefile %s\n", COOKIEFILE);
      exit(2);
    }
    if (sbuf.st_uid != getuid())
      fprintf(stderr, "readcookie: %s is not owned by you. You must own\n the cookiefile when running GNU Queue as NO_ROOT.\n",
	      COOKIEFILE), exit(2);
    if ((sbuf.st_mode & S_IWGRP) | (sbuf.st_mode & S_IWOTH)
	| (sbuf.st_mode & S_IRGRP) | (sbuf.st_mode & S_IROTH))
      fprintf(stderr, "readcookie: %s: mode %03o allows general read and/or write; cookiefile must be accessible only by you.\n",
	      COOKIEFILE, sbuf.st_mode), exit(2);
    if(!(cookiefile=fopen(COOKIEFILE, "r"))
       || !fgets(&cookie[0], 79, cookiefile)) {
      fprintf(stderr, "Unable to read valid cookiefile %s!\n", COOKIEFILE);
      exit(2);
    }
    cookieread = 1;
  }

  return (&cookie[0]);

#else /*NO_ROOT*/

  strcpy(cookie, "\n");
  return(&cookie[0]);
#endif /*NO_ROOT*/
}


/*Unique() is originally from batch*/
/*
 * Generate a unique number.
 * We use the time of day for the high bits
 * (so sorting the files by ASCII string compares
 * sorts them by creation time - an important property)
 * concatenated with some lower bits of the process ID.
 *
 * Possible failures:
 *    -	If this UNIX ever creates more than
 *	2^PIDBITS processes in one second, and two of them are
 *	"batch", then they might come up with the same filename.
 *	Make PIDBITS large enough to make this arbitrarily unlikely.
 *    - Every 24855/(2^PIDBITS) days, the unique number overflows.
 *	This results in newer entries being processed before ones
 *	that predate the rollover.  This is a problem only if
 *	the directory queue never drains completely, leaving the old ones stuck
 *	for a LONG time, or if absolute order of processing is important.
 *
 * Setting PIDBITS to 4 handles process creation rates of up to 16/sec,
 * and gives a unique number overflow every 4 years or so.  This should
 * be good enough for most applications.
 */
#define	PIDBITS	4

long
unique(void)
{
  return ( (time((long *)0)<<PIDBITS) | getpid()&((1<<PIDBITS)-1) )
    & 0x7fffffff;
}



/* netfwrite/netfread */
/* These two routines should be used when writing/reading binary data
 * to/from the job control files to ensure cross-platform operation.
 *
 * The calls are identical to the fwrite/fread functions (especially
 * for big-endian systems :).
 */
size_t netfwrite (void *databuf, size_t size, size_t nmemb, FILE *stream) {

  int i,j;
  size_t rc,count;

#ifdef BIG_ENDIAN_HOST
  rc=fwrite(databuf, size, nmemb, stream);
#else
  count=0;
  for(i=0;i<nmemb;i++) {
    for (j=0;j<size;j++) {
      count+=fwrite(databuf + (i*size) + (size-1-j), 1,1,stream);
    }
  }
  rc = count / size;
#endif

  return(rc);
}

size_t netfread (void *databuf, size_t size, size_t nmemb, FILE *stream)
{
  int i,j;
  size_t count,rc;

#ifdef BIG_ENDIAN_HOST
  rc=fread(databuf, size, nmemb, stream);
#else
  count=0;
  for (i=0;i<nmemb;i++) {
    for (j=0;j<size;j++) {
      count += fread(databuf + (i*size) + (size-1-j), 1, 1, stream );
    }
  }
  rc=count/size;
#endif

  return(rc);
}
