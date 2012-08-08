/*WGK 1998/08/05 */
#include "config.h"
#include "define.h"
#include "queue_endian.h"
#ifndef _QUEUE

#define _QUEUE

/*-> ONLY VERY RARELY WILL YOU NEED TO CHANGE THINGS BELOW THIS POINT <-*/

/*You shouldn't need to mess with these either; they are explained in
queued.c*/

#define QUERY_PORT 1423
#define WAKEUP_PORT 1421
#define SERV_PORT 4700

/*->NO LOCAL CONFIGURATION BELOW THIS POINT<-*/

/*Should be set to no less than what the OS supports. 1024 for
  GNU/Linux???*/

#define MAX_PATH_SIZE 10000

#ifndef _MAXNAMLEN
#define _MAXNAMLEN 256
/*This is the maximum length of a filename under a modern Unix
  filesystem, currently around 255.*/
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 64
#endif

struct wkstruct{
  char *host;
  float load;
};


#ifdef sethosts
struct wkstruct *Hosts = NULL;
int NHosts;
#else
extern struct wkstruct *Hosts;
extern int NHosts;
#endif

extern char *canonicalhost(char *host);
extern char *localhost(char *host);

extern char *mymalloc(int n);



typedef double DOUBLE;

#undef DIRSIZ_Q
#define	DIRSIZ_Q	32	/* Used to be 14; could be MAXNAMLEN... -IAN! */

typedef int fdev; /*should normally be OK.*/

/*typedef dev_t fdev;*/


#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifndef MAXBSIZE
#define MAXBSIZE	4096	/* correct for IRIS */
#endif

#ifndef RLIM_NLIMITS
#define RLIM_NLIMITS	6	/* Number of resources, eg RLIMIT_CPU */
#endif

#ifndef R_OK
#define	R_OK	04
#endif

#ifndef SYS_SIGLIST_DECLARED
static char	*sys_siglist[NSIG] = {
  "Signal 0",
  "Hangup",			/* SIGHUP */
  "Interrupt",			/* SIGINT */
  "Quit",			/* SIGQUIT */
  "Illegal instruction",	/* SIGILL */
  "Trace/BPT trap",		/* SIGTRAP */
  "IOT trap",			/* SIGIOT */
  "EMT trap",			/* SIGEMT */
  "Floating point exception",	/* SIGjpE */
  "Killed",			/* SIGKILL */
  "Bus error",			/* SIGBUS */
  "Segmentation fault",		/* SIGSEGV */
  "Bad system call",		/* SIGSYS */
  "Broken pipe",		/* SIGPIPE */
  "Alarm clock",		/* SIGALRM */
  "Terminated",			/* SIGTERM */
  "User-defined signal 1",	/* SIGUSR1 */
  "User-defined signal 2",	/* SIGUSR2 */
  "Child exited",		/* SIGCLD */
  "Power fail",			/* SIGPWR */
  "Signal 20",
  "Signal 21",
  "Signal 22",
  "Signal 23",
  "Signal 24",
  "Signal 25",
  "Signal 26",
  "Signal 27",
  "Signal 28",
  "Signal 29",
  "Signal 30",
#ifndef __hp9000s800
  "Signal 31"
#endif
};
#endif /*SYS_SIGLIST_DECLARED*/


#define	Q_QUEUESTAT		"queuestat"	/* queue status file */
#define	Q_PROFILE		"profile"	/* name of prifile file */
#define	Q_CFDIR			"CFDIR"		/* directory for cf* files */

#define DEV_NULL		"/dev/null"	/* null q_supervisor */

#define Q_STARTNEW	0	/* "exec on" in profile; start new jobs */
#define Q_DRAINING	1	/* "exec off" or "exec drain" in profile */
#define Q_MIGRATING     1       /* "migrate jobs out.*/

#define Q_NOCONTACT 0 /*Don't actually wake up any hosts; just query them.*/
#define Q_CONTACT 1  /*Contact the best host.*/

#define USER_CHECKPOINT 2
#define KERNEL_CHECKPOINT 1
#define NO_CHECKPOINT 0

#define RESTART 1
#define NO_RESTART 0


/* the length of the status prefix */
#define BAT_PREFIX_LEN          2

/* the maximum length of a user-id as defined by utmp */
#define BAT_MAXUID sizeof(((struct utmp *)NULL)->ut_name)

/* startup configuration for queues */
#define BAT_INICE	0	/* initial nice value */
#define BAT_IMAXEXEC	1	/* initial number of running jobs */
#define BAT_ILOADSCHED	25	/* initial load average to schedule jobs */
#define BAT_ILOADSTOP	50	/* initial load average to stop running jobs */
#define BAT_IMINFREE	0	/* initial min. number of free devices */
#define BAT_IRESTARTMODE NO_RESTART
#define BAT_ICHECKPOINTMODE NO_CHECKPOINT
#define BAT_IRESTARTFLAG ""
#define BAT_ICHECKPOINT 26    /*Default load to start checkpoint migrating.*/

#define STRSIZ	256		/* sprintf string buffers */

typedef char bat_Mail;
#define MAIL_START	(1<<0)
#define MAIL_END	(1<<1)
#define MAIL_CRASH	(1<<2)

#define CONTROL_STR     "# BATCH: Start of User Input\n"
#define RETURN_LINES	5

/*
 * Pathnames
 */
/* The next two are defined in Makefile */

#define DEBUGFILE	"/tmp/queued.debug"	/* debug log file */
#define SLEEPTIME	120			/* idle timer between jobs */

/* Old stuff when handle() ran as a QUEUED executable.
#define QUEUED "/home/wkrebs/queue/queue-test/queued"*/



#ifdef SIGSTOP
#define JOBCTL 1		/* Berkeley-style job control available */
#endif /*SIGSTOP*/

#ifdef __hpux
#define POSIX
#endif


#ifdef HAVE_DIRENT_H
#define struct_dir struct dirent
#else
#define struct_dir struct direct
#endif

#define error1(fmt,a1)		{ sprintf(errstr,fmt,a1); error(errstr); }
#define error2(fmt,a1,a2)	{ sprintf(errstr,fmt,a1,a2); error(errstr); }

#define syserror1(fmt,a1)	{ sprintf(errstr,fmt,a1); syserror(errstr); }

/*WGK 1999/01/19  The following defines are needed for SUN-OS 5.x*/

#ifndef sigmask
#define sigmask(sig) \
  (((unsigned long) 1) << (((sig) - 1) % (8 * sizeof (unsigned long int))))
#endif /*sigmask*/

#ifndef HAVE_HERROR
/*WGK Not sure if this is correct, but it will cause it compile, and
  since it is only used once as an error diagnostic, it is not
  essential that this work.*/
#define herror perror
#endif /*HAVE_HERROR*/
/*WGK End SUN-OS stuff.*/

/*WGK 1999/01/24 FreeBSD support stuff*/
/*These constants are Or'd in, so if the platform
  doesn't support them, set them to zero.*/

#ifndef XCASE
#define XCASE   0
#endif /* XCASE*/

#ifndef IUCLC
#define IUCLC 0
#endif /*IUCLC*/

/*WGK End FreeBSD stuff*/

unsigned char *md5(unsigned charstrin, int len);

#endif /*_QUEUE*/
