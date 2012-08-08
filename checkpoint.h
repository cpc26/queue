

#ifndef _CHECKPOINT_H
#define _CHECKPOINT_H

#ifdef __CHECKPOINT__
#define EXTERN
#else
#define EXTERN	extern
#endif

#include <asm/ptrace.h>
#include <linux/pipe_fs_i.h>
/*#include <linux/ipc.h> */

/* Bitwise definition:    
   bit  0 = 0 - One process,          1 - All children 
   bit  1 = 0 - Continue,             1 - Kill
   bit  2 = 0 - W/Shared Library,     1 - W/O Shared Library
   bit  3 = 0 - W/Common components   1 - W/O Common components
   bits 4-7 = Unused 
*/
#define CKPT_MAX_FILENAME		120

#define CKPT_CHECK_ALL			0x01
#define CKPT_KILL			0x02
#define CKPT_NO_SHARED_LIBRARIES	0x04
#define CKPT_NO_BINARY_FILE		0x08

/* Type of file descriptor encountered: */
#define CKPT_PIPE			0
#define CKPT_FILE			1
#define CKPT_OTHERS			2

/* Used during processes' execution */

struct shlib_list
{
        char *filename;                 /* name of shared lib */
        struct shlib_list *next;        /* ptr to next */
};

struct mmap_list
{
        char *filename;                 /* name of interpreter */
	unsigned long vm_start;		/* start address */ 
	unsigned long prot;		/* protection */
	unsigned long flags;		/* flags */
        struct mmap_list *next;         /* ptr to next */
};

struct shmem_list
{
	int shmid;			/* id of shared mem region */
	struct shmem_list *next;	/* ptr to next */
}; 

struct sem_list
{
	int semid;			/* id of semaphore */
	struct sem_list *next;		/* ptr to next */
};

/* Used during process' dump */

struct header
{
        int num_segments;
        unsigned father:1;
	unsigned in_sys_call:1;
};

struct segments
{
        unsigned long vm_start;
        unsigned long vm_end;
        unsigned long prot;
	unsigned long flags;
        unsigned shared:1;
        /* needed when shared==1 */
        unsigned long offset; /* file position */
        char filename[CKPT_MAX_FILENAME];  /* Maximum interpreter filename, for simplicity */
};

struct memory
{
        unsigned long context;
        unsigned long start_code, end_code, start_data, end_data;
        unsigned long start_brk, brk, start_stack, start_mmap;
        unsigned long arg_start, arg_end, env_start, env_end;
};

struct shared_lib_hdr
{
	int number_shlib;
};

struct shared_lib
{
	int name_size;	 /* The size of the name string that follows this struct in ckpt file */
};

struct open_files_hdr
{
	int number_open_files;
};

struct open_files
{
	int entry_size; /* How many bytes are we dumping after this struct */
	int type; /* CKPT_PIPE, CKPT_FILE, others */
	int fd; /* Original fd */
	union
	{
		struct 
		{
			unsigned long inode; /* just a unique identifier of inode */
			int lock; /* Pipe lock */
			unsigned blocked:1;
			unsigned rdwr:1; /* 0 - read, 1 - write */
		} pipes;
		struct 
		{
			unsigned long int file_pos;
			unsigned long file; /* unique identifier of struct file */
			int flags;
			int mode;
			char *filename; /* see (1) */
		} file; 
	} u;
}; 

/* 
   (1) This is a dummy pointer. The string is actually dumped after the
   open_files struct, and it's size is 'entry_size'. 
*/

struct shm_hdr
{
	int num_shm_regions; /* number of shared memory regions present */
};

struct shmem
{
	int shmid;
	key_t key;
	int size;
	int shmflg;
	int num_attaches;	/* number of long ints after this header. see(2)*/ 
}; 

/*
   (2) This number is not the number of processes sharing one shared region,
   but the number of different regions attached (by the sys call shmat()) to
   a single shared region (shmid). The address of each region attached is 
   dumped after this structure and is an unsigned long int. 
*/

struct sem_hdr
{
	int num_sems;		/* number of different id semaphores */
};

struct semaphor
{
	int semid;	/* id of semaphore */
	key_t key;		 
	int semflgs;
	int nsems;	/* number of semaphores values after this header. See(3).*/
};

/* 
   (3). This number indicates the number of short ints that follow this header.
   Each short int represents the current value of each semaphore.
*/

#ifdef __KERNEL__

#include <linux/sched.h>

#define CKPT_MAX_CALLERS_CONC  	5 /* Max process requesting checkpoint simultaneously */
#define CKPT_MAX_CALLED_CONC	5*CKPT_MAX_CALLERS_CONC

int do_checkpoint (struct pt_regs regs);
void verify_checkpoint_end (void);

void ckpt_open (char *pathname, int fd);
void ckpt_close (struct file *f);

void ckpt_mmap_insert (struct task_struct *p, unsigned long start, unsigned long prot,
			unsigned long flags, char *filename);
void ckpt_mmap_delete (struct task_struct *p, unsigned long start);
void ckpt_shlib_insert (struct task_struct *p, char *filename);
/* void ckpt_shlib_delete (struct task_struct *p, ?); */
void ckpt_shmem_insert (struct task_struct *p, int shmid);
void ckpt_shmem_delete (struct task_struct *p, int shmid);
void ckpt_sem_insert (struct task_struct *p, int semid);
void ckpt_sem_delete (struct task_struct *p, int semid);
void ckpt_fork_lists (struct task_struct *p);
void ckpt_exec_lists (struct task_struct *p);
void ckpt_free_lists (struct task_struct *p);

#else

#include <linux/unistd.h>

static inline _syscall3 (int, checkpoint, int, pid, int, fd, int, flags);
static inline _syscall1 (int, restart, char*, filename);
static inline _syscall1 (int, collect_data, int, pid);

#endif

#endif

