/*
 *  Queue load balancing system
 *  $Revision: 1.3 $
 *  Copyright (C) 1998-2000 W. G. Krebs <wkrebs@gnu.org>
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

#include "queue.h"

char *mtos(void);
char buf2[1000];

char *line = NULL;

struct utmp myu;

extern int compar(const void *one, const void *two);



int 
allocpty(void)
{
  static int num = 0;
  static char *dirstack = 0;
  static int dirsize = 0;
  static int dirnum = 0;
  struct stat buf;
  int i, jobs;
  char *dirname;
  int uid, gid,fd;
  DIR *dir;
  FILE *queue;
  struct dirent *temp;
  char *file, c;

#ifdef HAVE__GETPTY

  /*IRIX*/

  line = _getpty(&fd, O_RDWR|O_NDELAY, 0600, 0);

  if(line) return(fd);

#else

  dirnum = 0;
#ifdef __hpux
  dir = opendir("/dev/ptym");
#else
  dir = opendir("/dev");
#endif
  while (1) {
    if(dirnum == dirsize) {
      dirsize+=100;
      dirstack = realloc(dirstack, dirsize*(_MAXNAMLEN + 1));
    }
    temp = readdir(dir);
    if (!temp) break;
    /*#ifdef linux*/
    if (!(temp->d_name[0] == 'p' && temp->d_name[1] == 't' 
	  && temp->d_name[2] == 'y')) 
      continue;
    /*#endif*/
    strcpy(dirstack + (_MAXNAMLEN + 1) * dirnum++, temp->d_name);
  }
  closedir(dir);

  qsort(dirstack, dirnum-1, _MAXNAMLEN + 1, compar);
  jobs = 0;
  for(i=0; i<dirnum-1; ++i) {
    file = dirstack + i*(_MAXNAMLEN + 1);

#ifndef linux
    /*Next line really only needed for hpux version.*/
    if((!strcmp(file, "."))||(!strcmp(file, "..")))  continue;
#endif

#ifdef __hpux
    sprintf(buf2, "/dev/ptym/%s", file);
#else
    /*GNU/linux*/
    sprintf(buf2, "/dev/%s", file);
#endif


    if((fd = open(buf2, O_RDWR))<0) {
      if(errno==EBUSY) continue;
      if(errno==ENODEV) continue;
      if(errno==EIO) continue;
      syslog(LOG_ERR, "ptyalloc open:%s:%m", buf2);
      return(-1);
    }

    return(fd);
  }
#endif /*HAVE__GETPTY*/
  syslog(LOG_ERR, "ptyalloc: no more ptys");
  return (-1);
}

void 
deallocpty(void) 
{
#ifndef NO_ROOT
  setutent();
  myu.ut_type = DEAD_PROCESS;
  getutid(&myu);
  pututline(&myu);
  endutent();
#endif /*NO_ROOT*/
}


void 
mkutmp(char *name, int pty2, int uid, int gid)
{
  char *line;
  char buf[255];
  line = mtos();
  strcpy(myu.ut_user, name);
#ifdef linux
  strncpy(myu.ut_id,line+8,2);
#else
  strncpy(myu.ut_id, line+12,2);
#endif
  strcpy(buf, "/dev/tty");
  strcat(buf, myu.ut_id);
#ifdef linux
  strcpy(myu.ut_line, line+5);
#else
  strcpy(myu.ut_line, line+9);
#endif
  myu.ut_pid = getpid();
  myu.ut_type = USER_PROCESS;
  myu.ut_time = time(NULL);
#ifdef HAVE_UT_ADDR
  strcpy(myu.ut_host, "Queue process");
  myu.ut_addr = 0L;
#endif
  setutent();
  getutid(&myu);
  pututline(&myu);
  fchown(pty2, uid, gid);
  fchmod(pty2, S_IRUSR|S_IWUSR);

  if(!strcmp(buf,"/dev/tty")) {
    syslog(LOG_ERR,"queue: pty.c: mkutmp: bug: buf equals /dev/tty.");
  }
  else {
  chown(buf, uid, gid);
  chmod(buf, S_IRUSR|S_IWUSR);
  }
}

/*HP ptsname is buggy! So we write our own!*/
char 
*mtos(void)
{
  char *ptym;
  static char buf[16];
  register int i;

#ifdef HAVE__GETPTY
  if(line) return(line);

  syslog(LOG_ERR, "mtos: call allocpty first.");
  return(NULL);

#else

  ptym = buf2;
  for(i=0;ptym[i]!=0;++i) if(ptym[i]=='/') {
    ptym = &ptym[i+1];
    i = 0;
  }
  if(ptym[0]!='p') 
    syslog(LOG_ERR, "mtos: bad pty: %s", ptym);

#ifdef __hpux
  sprintf(buf, "/dev/pty/t%s", ptym+1);
#else
  /*gnu/linux*/
  sprintf(buf, "/dev/t%s", ptym+1);
#endif

  return(buf);
#endif /*HAVE__GETPTY*/
}
