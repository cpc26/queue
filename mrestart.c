#define LPRINTF printf
#define DEBUGG 1

/*
 *	Copyright(C) Eduardo Pinheiro, 1997,1998
 *      Modifications for GNU Queue Copyright (C) W. G. Krebs 1999-2000. All rights reserved.
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
 *  If you make modifications to the source, I would be happy to have
 *  them to include in future releases.  Feel free to send them to:
 *      W. G. Krebs	      				
 *	<wkrebs@gnu.org>
 */

/*WGK 1999/2/27, added LPRINTF to allow this to be run under GNU Queue
  so that debugging messages would not be printed unless this is
  desired.

  Merged split.c sources into mrestart.c, so that Queue just has to call
  this code on the checkpointed file. Hopefully, this entire thing will
  someday be replaced by a single library call by improving the kernel
  interface.

  Added code to nuke the checkpointed file and the intermediates
  created by split. (If you want to restart the file repeatedly, make
  a backup.

  Turned mrestart into a function rather than a program. It is called
  by handle.c on systems that have the checkpointing API. This should
  simply be a single, elegant library/kernel call someday.*/

#include "queue.h"

#ifdef HAVE_ASM_CHECKPOINT_H

#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include "checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/sem.h>
/*#include <sys/ipc.h>*/
#include <sys/time.h> /* macros FD_SET, FD_CLR, ... */
/*#include <sys/ipc.h>*/
#include <sys/shm.h>
#include <malloc.h>
#include <string.h>
#include <sys/types.h>

/*
  Copyright(C) Eduardo Pinheiro 1997, 1998
	
  GNU Copyleft version 2 or later.
	
  ---
  Split 1.6 - splits a checkpointed image file

  Version 1.0 Initial release.
  Version 1.5 No file size information.
  Version 1.6 Used standalone or in COWIX
*/



#define PAGESIZE			4096

/* Forward definition */
static int doit (struct header hdr, int f, FILE *out, FILE *fds);

/*WGK: To restart a multiple-part restart file (multiple-part because
  it contains the children in the same file with parents) it is
  necessary to split the file and then call restart on the split
  file.*/

int 
split (int f, char *out_name)
{
  const char *info_msg = "# This is an auxiliary file. It has information about open files\n# in the checkpointed process.\n";
  FILE *out, *fds;
  int ret;
  int i;

  i=0;
  while (1)
    {
      char *name=(char*)malloc(strlen(out_name)+10);
      char *name_fds=(char*)malloc(strlen(out_name)+10);
      /* char *name_shm=(char*)malloc(strlen(out_name)+10);*/
      struct header hdr;
	
      if ( !name || !name_fds /* || !name_shm */ )
	{
#ifdef DEBUGG
	  LPRINTF("No mem!\n");
#endif
	  exit(-1);
	}

      ret=read(f, &hdr, sizeof(struct header));
      if (ret!=sizeof(struct header))
	break;

      if (!hdr.father)
	{
	  sprintf(name, "%s.c%.4d", out_name, i);
	  sprintf(name_fds, "%s.c%.4d.fds", out_name, i);
	}
      else
	{
	  sprintf(name, "%s.p", out_name);
	  sprintf(name_fds, "%s.p.fds", out_name);
	}

#ifdef DEBUGG
      LPRINTF("Saving %s:\n", name);
#endif
      out=fopen(name, "wb");
      fds=fopen(name_fds, "wb");
      fputs(info_msg, fds);
      fwrite(&hdr, sizeof(struct header), 1, out);
      ret=doit(hdr, f, out, fds);		
      if (ret!=0)
	return ret;
      fclose(out);
      fclose(fds);
      if (!hdr.father) 
	i++;
    }
  close(f);
  return ret;
}

static int
doit (struct header hdr, int f, FILE *out, FILE *fds)
{
  int ret, i;
  struct segments seg;
  struct memory mem;
  int skippad;
  struct shared_lib_hdr shlib_hdr;
  struct shared_lib shlib;
  struct pt_regs regs;
  struct open_files_hdr of_hdr;
  struct open_files of;
  struct shm_hdr shm_hdr;
  struct shmem shmem;
  struct sem_hdr sem_hdr;
  struct semaphor semaphor;
  unsigned long size, chunks;
  char *ptr;
  long pos;
  const char *file_type[] = { "PIPE", "FILE", "SOCKET", "OTHERS" };

  ret=read(f, &mem, sizeof(mem));
  ret=fwrite(&mem, sizeof(mem), 1, out);

  skippad = hdr.num_segments * sizeof(struct segments) + 
    sizeof(struct header) + sizeof(struct memory);

#ifdef DEBUGG
  LPRINTF("num_segments = %d\n", hdr.num_segments);
#endif	
  for (i=0, size=0; i<hdr.num_segments; i++)
    {
      ret=read(f, &seg, sizeof(seg));
      ret=fwrite(&seg, sizeof(seg), 1, out);
#ifdef DEBUGG
      LPRINTF("%d, START: %lX, END: %lX, PROT: %lX, FLGS: %lX, SHR: %X", 
	      i, seg.vm_start, seg.vm_end, seg.prot, seg.flags, seg.shared);
#endif
      if (seg.shared)
	{
#ifdef DEBUGG
	  LPRINTF(", FNSIZE: %d, FNAME: %s", strlen(seg.filename), 
		  seg.filename);
#endif
	}
      else
	size += seg.vm_end - seg.vm_start;
#ifdef DEBUGG
      LPRINTF(".\n");
#endif
    }

  if (skippad%PAGESIZE!=0)
    skippad = ((skippad/PAGESIZE)+1)*PAGESIZE-skippad;

#ifdef DEBUGG
  LPRINTF("skippad = %d\n", skippad);
#endif

  ptr=(char*)malloc(skippad);
  if (!ptr)
    {
#ifdef DEBUGG
      LPRINTF("No more mem\n");
#endif
      return -1;
    }
	
  ret=read(f, ptr, skippad);
  if (ret!=skippad)
    {
#ifdef DEBUGG
      LPRINTF("No more file!\n");
#endif
      return -1;
    }	
  ret=fwrite(ptr, skippad, 1, out);
  free(ptr);


  ptr=(char*)malloc(PAGESIZE);
  if (ptr==NULL)
    return -1;

  for (chunks=0; chunks<size; chunks+=ret)
    {
      ret=read(f, ptr, PAGESIZE);
      if (ret<PAGESIZE)
	{
	  pos = lseek(f, 0, SEEK_CUR);
#ifdef DEBUGG
	  LPRINTF("Erro leitura: chunks=%ld, size=%ld, f_pos=%ld\n", 
		  chunks, size, pos);
#endif
	  return -1;
	}
      fwrite(ptr, 1, ret, out); 
    }

  /* Read shared libraries information, if any */
#if 0
  ret = read(f, &shlib_hdr, sizeof(struct shared_lib_hdr));
#ifdef DEBUGG
  LPRINTF("Shared libs: %d\n", shlib_hdr.number_shlib);
#endif
  ret = fwrite(&shlib_hdr, sizeof(struct shared_lib_hdr), 1, out);
  for (i=0; i<shlib_hdr.number_shlib; i++)
    {
      read(f, &shlib, sizeof(struct shared_lib));
      fwrite(&shlib, sizeof(struct shared_lib), 1, out);
      read(f, ptr, shlib.name_size);
      fwrite(ptr, shlib.name_size, 1, out);
    }
#endif

  /* Read registers now */
  ret = read(f, &regs, sizeof(regs));	
  ret = fwrite(&regs, sizeof(regs), 1, out);

#ifdef DEBUGG
  LPRINTF(" ESP: %08lX    EBP: %08lX\n", regs.esp, regs.ebp);
       
  LPRINTF(" EDX: %08lX    ESI: %08lX\n", regs.edx, regs.esi);
       
  LPRINTF(" EAX: %08lX    EDI: %08lX\n", regs.eax, regs.edi);
	
  LPRINTF(" OEAX:%08lX    EIP: %08lX\n", regs.orig_eax, regs.eip);
#endif
  /* ************* */
  /* *** Files *** */
  /* ************* */
  read(f, &of_hdr, sizeof(of_hdr));
  fwrite(&of_hdr, sizeof(of_hdr), 1, out);
		
#ifdef DEBUGG
  LPRINTF("Number of open files: %d\n", of_hdr.number_open_files);
#endif

  for (i=0; i<of_hdr.number_open_files; i++)
    {
      long total;
      char buffer[50];

      read(f, &of, sizeof(of));
      fwrite(&of, sizeof(of), 1, out);
		
#ifdef DEBUGG
      LPRINTF("Tamanho dos dados do fd %d = %d\n", of.fd, of.entry_size);
#endif

      /*  Write file descriptor and type */
      if (of.type==CKPT_PIPE)
	{
	  sprintf(buffer, "%s %.8lX %d %c\n", file_type[of.type], 
		  of.u.pipes.inode, of.fd, ((of.u.pipes.rdwr)?('W'):('R')));
	  fputs(buffer, fds);	
	} else
	  if (of.type==CKPT_FILE)
	    {
	      sprintf(buffer, "%s %.8lX %d %ld %d %d ", file_type[of.type],
		      of.u.file.file, of.fd, of.u.file.file_pos, 
		      of.u.file.flags, of.u.file.mode);
	      fputs(buffer, fds);
	    } 

      total=0;
      while (total<of.entry_size)
	{
	  ret = read(f, ptr, (PAGESIZE>of.entry_size)?of.entry_size:PAGESIZE);
	  ret = fwrite(ptr, 1, ret, out);
	  if (of.type==CKPT_FILE)
	    fputs(ptr, fds);
	  total += ret;
	}
      if (of.type==CKPT_FILE)
	fputs("\n", fds);
    }		

  free(ptr);

  /* *********************** */
  /* **   Shared Memory   ** */
  /* *********************** */
  read(f, &shm_hdr, sizeof(struct shm_hdr));
  fwrite(&shm_hdr, sizeof(struct shm_hdr), 1, out);	
#ifdef DEBUGG
  LPRINTF("Num shared regions: %d\n", shm_hdr.num_shm_regions);
#endif
	
  for (i=0; i<shm_hdr.num_shm_regions; i++)
    {
      int j;
	
      read(f, &shmem, sizeof(struct shmem));
      fwrite(&shmem, sizeof(struct shmem), 1, out);
#ifdef DEBUGG
      LPRINTF("DEBUG: shmflgs %X\n", shmem.shmflg);
#endif
#ifdef DEBUGG
      LPRINTF("Region %d: Num attaches: %d\n", i, shmem.num_attaches);
#endif
      for (j=0; j<shmem.num_attaches; j++)
	{
	  unsigned long raddr;

	  read(f, &raddr, sizeof(raddr));
#ifdef DEBUGG
	  LPRINTF("Attach address: %lX\n", raddr);
#endif
	  fwrite(&raddr, sizeof(raddr), 1, out);
	}	
    }

  /* ********************* */
  /* **    Semaphores   ** */
  /* ********************* */
  read(f, &sem_hdr, sizeof(struct sem_hdr));
  fwrite(&sem_hdr, sizeof(struct sem_hdr), 1, out);
#ifdef DEBUGG
  LPRINTF("Num semaphores: %d\n", sem_hdr.num_sems);
#endif

  for (i=0; i<sem_hdr.num_sems; i++)
    {
      int j;
      short val;
		
      read(f, &semaphor, sizeof(struct semaphor));
      fwrite(&semaphor, sizeof(struct semaphor), 1, out);
#ifdef DEBUGG
      LPRINTF("DEBUG: semflgs=%X\n", semaphor.semflgs); 
      LPRINTF("   Num values: %d\n", semaphor.nsems);
#endif
      for (j=0; j<semaphor.nsems; j++)
	{
	  read(f, &val, sizeof(short));
	  fwrite(&val, sizeof(short), 1, out);
	}
    }	


  pos=lseek(f, 0, SEEK_CUR);
#ifdef DEBUGG
  LPRINTF(" File Pos: %ld\n", pos); 
  LPRINTF("\n---------------------------------------------\n\n");
#endif
  return 0;
}

#define MAX_OPEN_FILES			255
#define NULL_FD				-1

#define OUR_SEMAPHORE			55  /* Magic number. Could be any number. */	
#define CREATE_SEMAPHORE(num_sems)	(semget(OUR_SEMAPHORE, num_sems, IPC_CREAT | S_IRWXU))
#define DROP_SEMAPHORE(id_sem)		{ union semun dummy;		\
					  semctl(id_sem, 0, IPC_RMID, dummy); \
					}
#define INIT_SEMAPHORE(id_sem, which_sem, init_val) 			\
					{ union semun arg;		\
					  arg.val = init_val;		\
					  semctl(id_sem, which_sem, SETVAL, arg);	\
					}
#define UP(id_sem, which_sem)		{ struct sembuf sembuf;		\
					  sembuf.sem_op = 1;		\
					  sembuf.sem_num = which_sem;	\
					  sembuf.sem_flg = SEM_UNDO;	\
					  semop(id_sem, &sembuf, 1); 	\
					}
#define DOWN(id_sem, which_sem)		{ struct sembuf sembuf;		\
					  sembuf.sem_op = -1;		\
					  sembuf.sem_num = which_sem;	\
					  sembuf.sem_flg = SEM_UNDO;	\
					  semop(id_sem, &sembuf, 1);	\
					}

struct list
{
  int pid;
  struct list *next;
} head;
	
struct pipe_list
{
  int fd;
  unsigned long inode;
  unsigned rdwr:1;
  struct pipe_list *next;
} *pipe_head = NULL, *c_pipe_head = NULL;

struct file_list
{
  int fd;
  unsigned long file; /* ptr struct file */
  unsigned long file_pos;
  int flags;
  mode_t mode;
  char *filename;
  struct file_list *next;
} *file_head = NULL, *c_file_head = NULL;


/* 
   Garantees that the fd to pipes will be of number specified.  
*/
static int 
open_special_pipe (int fd0, int fd1)
{
  int p[2], ret;
	
  ret=pipe(p);
  if (ret==-1)
    return -1;
  if (fd0!=NULL_FD)
    {
      ret=dup2(p[0], fd0);
      if (ret!=fd0)
	return -2;
    }
  if (fd1!=NULL_FD)
    {
      ret=dup2(p[1], fd1);
      if (ret!=fd1)
	return -3;
    }
  if (fd0!=p[0])	
    close(p[0]);
  if (fd1!=p[1])
    close(p[1]);
  return 0;
}

int 
fileexists (char *name)
{
  FILE *f;
	
#ifdef DEBUGG
  LPRINTF("Testando arquivo %s:", name);
#endif
  f = fopen(name, "rb");

  if (f==NULL)
    {
#ifdef DEBUGG
      LPRINTF(" nao existe\n");
#endif
      return 0;
    }
  fclose(f);
#ifdef DEBUGG
  LPRINTF(" existe\n");	
#endif
  return 1;
}

static int 
open_special_file (int fd, char *filename, int flags, int mode)
{
  int ret;

  ret=open(filename, flags, mode);
  if (ret==-1)
    {
#ifdef DEBUGG
      LPRINTF("Cannot open file %s\n", filename);
#endif
      perror("open");
#ifdef DEBUGG
      LPRINTF("Can't restart!\n");
#endif
      exit(-1); /* we'd better do some deallocation before leaving */
    }
  if (ret==fd)
    return ret;
  dup2(ret, fd);
  close(ret);
  return fd;
}

static void 
open_father_files (void)
{
  struct file_list *fdl;

  for (fdl=file_head; fdl!=NULL; fdl=fdl->next)
    {
#ifdef DEBUGG
      LPRINTF("PAI: abrindo fd %d arq:%s\n", fdl->fd, fdl->filename);
#endif
      open_special_file(fdl->fd, fdl->filename, fdl->flags, fdl->mode);
      lseek(fdl->fd, fdl->file_pos, SEEK_SET);
    }	
}

static void 
open_father_pipes (void) 
{
  struct pipe_list *fdl, *co;

  for (fdl=pipe_head; fdl!=NULL; fdl=fdl->next)
    {
      for (co=pipe_head; co!=NULL; co=co->next)
	{
	  if (co==fdl)
	    continue;
	  if (co->inode==fdl->inode)
	    {
	      int fd0, fd1;

	      fd0 = (fdl->rdwr==0)?(fdl->fd):(co->fd);
	      fd1 = (fdl->rdwr==1)?(fdl->fd):(co->fd);
#ifdef DEBUGG
	      LPRINTF("Abrindo pipes %d e %d\n", fd0, fd1);
#endif
	      open_special_pipe(fd0, fd1);
	      break;
	    }
			
			
	}		
    }

}

static void 
trim (char *buf)
{
  char *s;
  for (s=buf; *s==' '; s++);
  strncpy(buf, s, sizeof(buf));
}

/*
void 
remove_from_file_list (struct file_list **flist, struct file_list *ptr)
{
  struct file_list *p;
	
  if (*flist==NULL)
    return;
  if (*flist==ptr)
    {
      p=flist;
      *flist=*flist->next;
      free(p);
      return;
    }
  for(p=flist;p!=NULL;p=p->next)
    if (p->next==ptr)
      {
	p->next=ptr->next;
	free(ptr->filename);
	free(ptr);
	return;
      }
	
}
*/

void 
parse_pipe (char *buf, struct pipe_list **p_head)
{
  int fd;
  char c;
  unsigned long inode;
  struct pipe_list *fdl;
  
  buf+=5; /* skips the string "PIPE " */
  sscanf(buf, "%lX %d %c\n", &inode, &fd, &c);
  if (strlen(buf)<10)
    {
#ifdef DEBUGG
      LPRINTF("Error on file fds\n");
#endif
      exit(-1);
    }                
  fdl=(struct pipe_list*)malloc(sizeof(struct pipe_list));
  fdl->inode = inode;
  fdl->fd = fd;
  fdl->rdwr = (c=='W');
  fdl->next=*p_head;
  *p_head = fdl;
}

void 
parse_file (char *buf, struct file_list **f_head)
{
  unsigned long file, file_pos;
  int mode, flags, fd;
  char filename[500];
  struct file_list *fdl;
		
  buf+=5; /* skips the string "FILE " */
  sscanf(buf, "%lX %d %ld %d %d %s\n", 
	 &file, &fd, &file_pos, &flags, &mode, filename);
  fdl=(struct file_list*)malloc(sizeof(struct file_list));
  fdl->file = file;
  fdl->fd = fd;
  fdl->file_pos = file_pos;
  fdl->flags = flags;
  fdl->mode = mode;
  fdl->filename=strdup(filename);
  fdl->next = *f_head;
  *f_head = fdl;
}

void 
open_father_fds (char *name)
{
  FILE *f;
  char buf[520], *filename;

  sprintf(buf, "%s.fds", name);
  f=fopen(buf, "rt");
  filename=strdup(buf);
	

  if (f==NULL)
    return;
#ifdef DEBUGG
  LPRINTF("Abri %s\n", buf);
#endif
  while (!feof(f))
    {
      char type[30];
      char *s;

      s=fgets(buf, sizeof(buf), f);
      if (s==NULL)
	break;
      if (buf[0]=='#')
	continue;
      trim(buf);
      if (buf[0]=='\n')
	continue;
      sscanf(buf, "%s ", type);
      if (strcmp(type, "PIPE")==0)
	parse_pipe(buf, &pipe_head);
      else
	if (strcmp(type, "FILE")==0)
	  parse_file(buf, &file_head);
    }
  fclose(f); 	
  /*WGK 1999/2/27 delete files as we read them in so we don't have to
    delete them later.*/
  unlink(filename);
  free(filename);
  open_father_pipes();
  open_father_files();
  /*
    open_father_sockets();
  */
}

void 
open_child_files (void)
{
  struct file_list *flp, *flc;
	
  /* Now compare with parents files to check whether or not
     we can inherit descriptors */
	
  for (flp=file_head; flp!=NULL; flp=flp->next)
    {
      int we_did_inherit=0;
	
      for (flc=c_file_head; flc!=NULL; flc=flc->next)
	{
	  if (flp->file==flc->file) /* share same file struct */
	    {	
	      we_did_inherit=1;

#ifdef DEBUGG
	      LPRINTF("FILHO(S): herdando fd %d ", flp->fd);
#endif
				/* do we share the same fd? If not, duplicate
				   it to our new home (new fd) */
	      if (flp->fd!=flc->fd)
		{
#ifdef DEBUGG
		  LPRINTF("e mudando para %d", flc->fd);
#endif
		  dup2(flp->fd, flc->fd);
		  flc->fd=-1; /* so we won't try to open it again later. */
		  /* there is no need to seek to file_pos
		     because the parent has already done so */
		}
	      else
		{
#ifdef DEBUGG
		  LPRINTF("diretamente");
		  LPRINTF("\n");
#endif
		}
	    }
	}
      if (!we_did_inherit)
	close(flp->fd);
		
    }
	
  for (flc=c_file_head; flc!=NULL; flc=flc->next)
    {
      if (flc->fd>=0)
	{
#ifdef DEBUGG
	  LPRINTF("FILHO(S): abrindo fd %d arquivo %s\n", flc->fd, 
		  flc->filename);
#endif
	  open_special_file(flc->fd, flc->filename, flc->flags, flc->mode);
	  lseek(flc->fd, flc->file_pos, SEEK_SET); 	
	  flc->fd=-1;
	}
    }
}

void 
open_child_fds (char *name)
{
  FILE *f;
  char buf[520], *filename;
  char type[10];
	
  sprintf(buf, "%s.fds", name);
  f=fopen(buf, "rt");
  filename=strdup(buf);

  if (f==NULL)
    return;

  while (!feof(f))
    {
      char *s;
      s=fgets(buf, sizeof(buf), f);
      if (s==NULL)
	break;
      if (buf[0]=='#')
	continue;
      trim(buf);
      if (buf[0]=='\n')
	continue;
      if (strlen(buf)<10)
	continue;
      sscanf(buf, "%s ", type);	
      /*
	if (strcmp(type, "PIPE")==0)
	parse_pipe(buf, &c_pipe_head);
	else
      */
      if (strcmp(type, "FILE")==0)
	parse_file(buf, &c_file_head);
    }	
  fclose(f);
  /*WGK 1999/2/27 delete files as we read them in so we don't have to
    delete them later.*/
  unlink(filename);
  free(filename);

  /*
    open_child_pipes();
  */
  open_child_files();
  /*
    open_child_sockets();
  */
}

void 
mrestart(char* file)
{   
  int ret, i;
  struct list *p, *q;
  int sem;
  char *name, *name2;
  int f;
  chdir(QUEUEDIR);

  f=open(file, O_RDONLY);
  if (!f)
    {

#ifdef DEBUGG
      LPRINTF("File %s not found or invalid\n", file);
#endif
      exit(2);
    }

  name2=(char*)malloc(strlen(file)+strlen(".splt")+sizeof(char));

  name=(char*)malloc(strlen(file)+strlen(".c0000")+sizeof(char));	
  if (!name||!name2)
    {
#ifdef DEBUGG
      LPRINTF("No mem\n");
#endif
      exit(-1);
    }

  strcpy(name2, file);
  strcat(name2, ".splt");

  split(f, name2);
  unlink(file);


  sprintf(name, "%s.p", name2);	

  if (!fileexists(name))
    {
#ifdef DEBUGG
      LPRINTF("no parent process %s found\n", name);
#endif
      free(name);
      exit(-1);
    }
  open_father_fds(name);

  p=&head;
  p->next=NULL;

  sem = CREATE_SEMAPHORE(3);
  if (sem<0)
    {
      perror("semaphore");
      exit(-1);
    }
  INIT_SEMAPHORE(sem, 0, 0);
  INIT_SEMAPHORE(sem, 1, 0);
	
  for (i=0; ; i++) 
    {
      int pid;
		
      sprintf(name, "%s.c%.4d", name2, i);
      if (fileexists(name))
	{
	  pid=fork();
	  if (pid==0)
	    {
	      open_child_fds(name);
	      sem = CREATE_SEMAPHORE(3);
#ifdef DEBUGG
	      LPRINTF("CHILD: Waiting initialization of pid=%d(%s)\n", 
		      getpid(), name);
#endif
	      DOWN(sem, 1); 
	      UP(sem, 0); 
#ifdef DEBUGG
	      LPRINTF("CHILD: Initializing pid=%d\n", getpid());
#endif
	      collect_data(getpid());
	      ret = restart(name); 
	      exit(0);
	    }
	}
      else
	{
	  p=head.next;
	  while (p!=NULL)
	    {
#ifdef DEBUGG
	      LPRINTF("PARENT: signaling pid=%d, ret=%d\n", p->pid, ret);
#endif
	      UP(sem, 1);
	      DOWN(sem, 0);
	      q=p;
	      p=p->next;
	      free(q);				
	    }
#ifdef __COWIX__
	  if (kill(getppid(), SIGUSR1)==-1)
#ifdef DEBUGG
	    LPRINTF("Error sending signal to parent\n");
#endif 
#endif /*__COWIX__*/
	  DROP_SEMAPHORE(sem); 
	  sprintf(name, "%s.p", name2);	
#ifdef DEBUGG
	  LPRINTF("PARENT: Initialising parent pid=%d(%s)\n", getpid(), name);
#endif
	  collect_data(getpid());
	  ret=restart(name); 
#ifdef DEBUGG
	  LPRINTF("shouldn't print this, but just in case ret=%d\n", ret);
#endif
	  exit(-1);
	}	
      q = p;
      p = (struct list*)malloc(sizeof(struct list));
      q->next=p;
      p->pid = pid;
      p->next = NULL;
    } 
#ifdef DEBUGG
  LPRINTF("(this shouldn't print)\n");
#endif
}

#endif /*HAVE_ASM_CHECKPOINT_H*/
