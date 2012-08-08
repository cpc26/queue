/*
 *  Queue load balancing system
 *  $Revision: 1.15 $
 *  Copyright (C) 1998-2000 W. G. Krebs (except where otherwise noted.)
 *  
 *  <wkrebs@gnu.org>
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
 *  Based in part on the batchd component of the "freely redistributable"
 *  Batch suite by Alex White and many others. 
 *
 *
 *  If you make modifications to the source, I would be happy to have
 *  them to include in future releases.  Feel free to send them to:
 *      W. G. Krebs	      				
 *	<wkrebs@gnu.org>
 *
 **************************************************************************/


#include "queue.h"

#ifndef NO_QUEUE_MANAGER
#define QUEUE_MANAGER 1
#endif
  
/*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
#ifdef QUEUE_MANAGER
#include "queue_define.h"
#endif /*QUEUE_MANAGER*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

extern char cookie[];

#define SPOOLDIR QUEUEDIR


void check_query(void);
char *allocline(FILE *f);
#define ANOTHER_HOST -1


#define max(a,b) (a>b ? a : b)

#ifdef RISCos
#define sun
#endif


#ifndef RLIMIT_CPU
/*HP-UX*/
#define RLIMIT_CPU   0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA  2
#define RLIMIT_STACK 3
#define RLIMIT_CORE  4
#define RLIMIT_RSS   5

#define	RLIM_INFINITY	0x7fffffff
#endif /*RLIMIT_CPU*/

#ifndef HAVE_NLIST_H

/*Redhat 5 GNU/linux lacks <nlist.h>*/


struct nlist {
  char            *n_name;
  char            *n_qual;
  unsigned short  n_type;
  unsigned short  n_scope;
  unsigned int    n_info;
  unsigned long   n_value;
};

#endif

#include "lex.h"


/*
 * Generic queue_b structure.  The set of queues, jobs, and running
 * jobs are kept in doubly linked lists like this.  The first element
 * is not part of the queue_b, so "q == q->q_forw" iff q is empty.  An
 * empty queue_b is returned by emptyq().  Items are added by
 * enqueue_b(), removed by dequeue_b().  The queue_b links must appear
 * first in queue_b structures because of the way insque() and
 * remque() work.  */
struct qelem_queue {
    struct qelem_queue *q_forw;
    struct qelem_queue *q_back;
    char q_data[1];		/* not used */
};

/*WGK 1998/06/13 modified the definition of FOREACH to eliminate
busyloop problem in original definition of FOREACH from batch suite.*/

#define FOREACH(p, set) 	/* iterate p over set */ \
	for (p = set; ((p = p->forw) != set) && (p!=0); )

struct job {
  struct job 	 *forw, *back;	/* queue links; must be first */
  char		  j_cfname[DIRSIZ_Q+1];	/* batch queue cf* file name */
  int		  j_localuid;	/* local uid running job */
  char		 *j_userid;	/* userid@host running the job */
  char		 *j_mailuserid;	/* userid@host to get mail */
  char		 *j_jobname;	/* user-supplied name of job */
  char 		 *j_onlyhost;    /*If non-space or non-null,
		 		   only host job may run on.*/
  int 		  j_mailflag;	/*Fromerly donthang.*/
  time_t	  j_qtime;	/* time queued */
  int		  j_pid;	/* non-zero iff job is running */
  char		  j_seen;	/* marks jobs we found in a queue_b */
  int 		  j_lockfd;	/* fd of lock held on file.*/
  struct queue_b *j_queue_b;	/* backpointer to queue_b */
  DOUBLE	  j_totalcpu;
};

struct queue_b {
  struct queue_b *forw, *back;	/* queue_b links; must be first */
  int q_requeue_b; /*WGK 1995 
		     Set if a query was made on this queue_b, to force
		     check despite 1sec mtime granularity.  This is
		     done in query_port, and forces an eventually
		     requeue_b.*/
  unsigned q_nochange:1,	/* Set if no change since last runqueue_b() */
    q_migrate:1, 	/*Set to cause jobs to migrate*/
    q_drain:1,		/* Set to stop new jobs from starting */
    q_deleteq:1,	/* Set if queue_b is being deleted */
    q_stopped:1,	/* Set if queue_b stopped for lower load */
    q_startup:1,	/* Set after leftover of* files cleaned out */
    q_restart:1,	/* Restart on system crash? */
    q_seen:1,		/* Used while scanning for deleted queue_bs */
    q_noprofile:1;	/* No profile; throttle error msgs */
  short	q_nice;		/* Job priority */
  short	q_nexec;	/* Current # of executing jobs */
  short	q_maxexec;	/* Max # of executing jobs */
  short	q_vmaxexec;	/* Virtual Max # of executing jobs */
  short	q_loadsched;	/* Stop scheduling new jobs if over this */
  short	q_loadstop;	/* Stop jobs if over this load */
  short	q_checkpoint;  /*If greater than zero, turn on checkpointing
			 in this queue and try to migrate jobs out if
			 load average exceeds this level, provided an
			 acceptor can be found. This level should be
			 above or equal to loadsched and below
			 loadstop.*/
  short	q_checkpointmode;/*Zero indicates checkpoint off, 
			   1 indicates kernel based checkpointing,
			   two indicates user-based checkpointing.*/
  short	q_restartmode; /*Zero indicates checkpoint restart off, 1
			 indicates kernel based checkpoint restarting,
			 two indicates user-based checkpoint
			 restart.*/

  char *q_restartflag; /*For non-kernel checkpointing, command-line
			 flag job receives when it is restarted.*/
  long	q_minfree;	/* minimum free space on device */
  fdev	q_mfdev;	/* dev that minfree applies to */
  time_t q_mtime;	/* mtime of queue_b directory */
  time_t q_profmtime;	/* mtime of queue_b profile file */
  bat_Mail q_supmail;	/* Type of mail for supervisor */
  bat_Mail q_usermail;	/* Type of notification for user */
  char	*q_supervisor;	/* queue_b supervisor (usually a file name) */
  char	*q_name;	/* name of queue_b, e.g. troff, later */
  char	*q_cfdir;	/* q_name/Q_CFDIR */
  char	*q_profile;	/* q_name/Q_PROFILE */
  char	*q_queue_bstat;	/* q_name/Q_QUEUESTAT */
  char	*q_status1;	/* line 1 of status */
  char	*q_program;	/* external queue_b enabling/disabling control */
  int q_pfactor; 	/*Load Avg host preference factor for this host*/
  char	*q_timestop;	/* time spec. during which queue_b is enabled */
  char	*q_timesched;	/* time spec. during which new jobs are run */
  int	q_oldstat;	/* Previous status from "program" option */
  int	q_status;	/* 1 if go for queue_b */
  int	q_statcntr;	/* Counter for "program" status message */
  struct job *q_jobs;	/* linked list of jobs */
#ifdef HAVE_GETRLIMIT
  struct rlimit q_rlimit[RLIM_NLIMITS];	/* Resource hard/soft limits */
#endif
} *queue_bs;

/*
 * List of running jobs.
 * We use this to map a dead child's pid to a job (and hence queue_b).
 */
struct running {
  struct	running *forw, *back;	/* queue_b links; must be first */
  int r_pid;
  struct job *r_job;
} *running;

/*
 * Signals to ignore.
 */
char ignore[] = {
	SIGPIPE,
#ifdef JOBCTL
	SIGTTOU, SIGTTIN, SIGTSTP,
#endif
};

/*
 * WGK this is a vestige of the old batchd, which started jobs
 * directly.  
 */
#define DEFPATH		"/usr/ucb:/bin:/usr/bin"

/*
 * Handy macros to simplify varargs error messages.
 * Assumes a global char errstr[STRSIZ];
 * Not re-entrant!
 */
#define merror1(fmt,a1)		{ sprintf(errstr,fmt,a1); merror(errstr); }
#define merror2(fmt,a1,a2)	{ sprintf(errstr,fmt,a1,a2); merror(errstr); }
#define merror3(fmt,a1,a2,a3) \
	{ sprintf(errstr,fmt,a1,a2,a3); merror(errstr); }
#define merror4(fmt,a1,a2,a3,a4) \
	{ sprintf(errstr,fmt,a1,a2,a3,a4); merror(errstr); }
#define merror5(fmt,a1,a2,a3,a4,a5) \
	{ sprintf(errstr,fmt,a1,a2,a3,a4,a5); merror(errstr); }
#define mperror1(fmt,a1)	{ sprintf(errstr,fmt,a1); mperror(errstr); }
#define mperror2(fmt,a1,a2)	{ sprintf(errstr,fmt,a1,a2); mperror(errstr); }
#define mperror3(fmt,a1,a2,a3) \
	{ sprintf(errstr,fmt,a1,a2,a3); mperror(errstr); }
#define muser1(who,fmt,a1)	{ sprintf(errstr,fmt,a1); muser(who, errstr);}
#define muser2(who,fmt,a1,a2) \
	{ sprintf(errstr,fmt,a1,a2); muser(who, errstr); }
#define muser3(who,fmt,a1,a2,a3) \
	{ sprintf(errstr,fmt,a1,a2,a3); muser(who, errstr); }
#define muser4(who,fmt,a1,a2,a3,a4) \
	{ sprintf(errstr,fmt,a1,a2,a3,a4); muser(who, errstr); }
#define muser5(who,fmt,a1,a2,a3,a4,a5) \
	{ sprintf(errstr,fmt,a1,a2,a3,a4,a5); muser(who, errstr); }
#define muser6(who,fmt,a1,a2,a3,a4,a5,a6) \
	{ sprintf(errstr,fmt,a1,a2,a3,a4,a5,a6); muser(who, errstr); }
#define muser7(who,fmt,a1,a2,a3,a4,a5,a6,a7) \
	{ sprintf(errstr,fmt,a1,a2,a3,a4,a5,a6,a7); muser(who, errstr); }
#define queue_bstat1(who,fmt,a1) \
	{ sprintf(errstr,fmt,a1); queue_bstat(who, errstr);}
#define queue_bstat2(who,fmt,a1,a2) \
	{ sprintf(errstr,fmt,a1,a2); queue_bstat(who, errstr); }
#define queue_bstat3(who,fmt,a1,a2,a3) \
	{ sprintf(errstr,fmt,a1,a2,a3); queue_bstat(who, errstr); }
#define queue_bstat4(who,fmt,a1,a2,a3,a4) \
	{ sprintf(errstr,fmt,a1,a2,a3,a4); queue_bstat(who, errstr); }
#define mdebug(str)	      { if (debug) muser(debugfile,str); }
#define mdebug1(str,a1)	      { if (debug) muser1(debugfile,str,a1); }
#define mdebug2(str,a1,a2)    { if (debug) muser2(debugfile,str,a1,a2); }
#define mdebug3(str,a1,a2,a3) { if (debug) muser3(debugfile,str,a1,a2,a3); }
#define mdebug4(str,a1,a2,a3,a4) \
	{ if (debug) muser4(debugfile,str,a1,a2,a3,a4); }
#define mdebug5(str,a1,a2,a3,a4,a5) \
	{ if (debug) muser5(debugfile,str,a1,a2,a3,a4,a5); }
#define mdebug6(str,a1,a2,a3,a4,a5,a6) \
	{ if (debug) muser6(debugfile,str,a1,a2,a3,a4,a5,a6); }

extern int errno;
extern char **environ;
extern struct passwd *getpwnam(const char *);
extern struct passwd *getpwuid(uid_t);
extern time_t time(time_t *);
extern char *ctime(const time_t *);
extern char *getenv(const char *);
extern char *index(const char *, int);

void sigalrm(void);
void sigchld(void);
void sighandler(int sig);
void toggledebug(void);
void restartdaemon(void);
char *syserr(void);
struct qelem_queue *enqueue_b(struct qelem_queue *qhead, int size);
struct qelem_queue *emptyq(void);
struct job *jobinfo(struct job *jp, struct queue_b *qp, char *cfname);
char *mycalloc(int n);
FILE *sendmail(register char *to, register char *from);
fdev getfs(char *file);
long fsfree(int dev);
DIR *opencfdir(register struct queue_b *qp);
char *myhostname(void);

static int nkids;
static int saverrno;		/* used to save errno */
static int sigalrmflag;
static int sigchldflag;
static char *spooldir = SPOOLDIR;
static char *pidfile;
static int systemflag = 0;		/* true if just did "system" call */
static int hpuxstatus;			/* Pass child status around */
static int terminatedfailed;		/* true if "terminated" failed */
#ifdef __hpux
char *debugfile = DEBUGFILE;
char errstr[STRSIZ];			/* Global sprintf buffer */
int debug = 0;				/* turn on for debugging output */
#else
static char *debugfile = DEBUGFILE;
static char errstr[STRSIZ];		/* Global sprintf buffer */
static int debug = 0;			/* turn on for debugging output */
#endif
static char *envuser = NULL;		/* $USER */
static int batchdpid = 0;		/* getpid() */
static int restartflag = 0;		/* set to cause reloading batchd */
static int totalrunning = 0;		/* sum of all q_nexec job counts */
static int needtime;			/* true when we need the time */
static char *envinit[3] = { "LOGNAME=root", 0, 0 };

/*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
/*This section of code is (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

int forkedqpid = 0;                     /* pid of forked off queue daemon */
int connect_bit = 0;                    /* need to connect to the queue_manager? */
unsigned int seed = 1;			/* the seed value of the random generator */
int timeout_connect = 0;		/* the max value to reach before we connect to the queue_manager */
int timeout_counter = 0;		/* counts up to the max value (timeout_connect) */	
int uid = -1;				/* the user's uid */
char username[MAXUSERNAME];		/* the user's name */
struct sockfd3_packet packet;  		/* packet to send to the queue_manager */

/* global socket variables */
struct sockaddr_in serv_addr;
struct hostent *hostent_host;
int sendbuff = 16384;

#endif /*QUEUE_MANAGER*/
/*End of code copyrighted by Texas Instruments.*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

	unsigned sleeptime;

int globalargc;
char **globalargv;

void error (char *s);
void mperror (char *s);
void muser (char *s, char *m);
void readqueue_bs (DIR *dir);
void getload (int *a);
void drainqueue_b (register struct queue_b *qp);
void readpro (register struct queue_b *qp);
void requeue_b (register struct queue_b *qp);
void runqueue_b (register struct queue_b *qp, int *load);
int waitforchild (void);
void queue_bstat (struct queue_b *qp, char *str);
void terminated (register int pid, register int status, register DOUBLE totalcpu);
int merror (char *s);
void mymerror (char *s);
void mailclose (FILE *file);
void freequeue_b (struct queue_b *qp);
void abortall (register struct queue_b *qp);
void freeqstorage (struct queue_b *qp);
void releasedev (int dev);
void migratequeue_b (register struct queue_b *qp);
int rltoi (register int rl);
void freejob (struct job *jp);
int abortjob (struct job *jp);
void mailback (int status, register struct job *jp, register bat_Mail mailstat, register char *expl);
int checktime (char *p, struct tm *tp);
int sigalljobs (register struct queue_b *qp, int sig);
int startjob (register struct job *jp);
int transmitjob (struct job *jp);
int itorl (int i);
void freerunning (struct running *rp);
int sigjob (struct job *jp, int sig);
int check1time (char *p, struct tm *tp);
int validhost (struct sockaddr_in address1);

int
main(int argc, char **argv)
{
  register struct queue_b *qp;
  int i, load[3];

  register FILE *f;
  DIR *spooldirdot;
  struct stat sbuf;
  time_t spool_mtime = 0;
  time_t qhostsfile_mtime = 0;
  extern char **environ;
  extern int optind;
  extern char *optarg;

  /* Long getopt options.*/
  static const struct option longopts[] =
  {
    {"sleeptime", required_argument, NULL, 't'},
    {"pidfile", required_argument, NULL, 'p'},
    {"spooldir", required_argument, NULL, 'd'},
    {"debug", no_argument, NULL, 'D'},
    {"version", no_argument, NULL, 'v'},
    { "help", no_argument, NULL, 1 },
    { NULL, 0, NULL, 0 }
  };

  /*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
/*This section of code is (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

  /* Get the IP address of the queue_manager host name */
  if ( (hostent_host = gethostbyname(QMANAGERHOST)) == NULL )
  {
	  fprintf(stderr, "Queued.c Error: invalid host name of the queue manager\n");
	  return -1;
  }

  /* Fill in the server (q_manager) information so that we can connect to it */
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORTNUM3);
  serv_addr.sin_addr = *((struct in_addr *)hostent_host->h_addr);

#endif /*QUEUE_MANAGER*/
/*End of code copyrighted by Texas Instruments.*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

  sleeptime = SLEEPTIME;

  globalargc = argc;
  globalargv = argv;

  {char *pidfilet;
  pidfilet = mymalloc(256 * sizeof(char));
  strcpy(pidfilet, PIDFILE);
  strcat(pidfilet, ".");
  strcat(pidfilet, myhostname());
  pidfile = strdup(pidfilet);
  free(pidfilet);
  }
  /*
   * Grab USER from environment for debugging.
   * Save our pid so children can kill us if something goes really
   * wrong.  
   */
  envuser = getenv("USER");

#ifdef apollo	/* On Apollo, get SYSTYPE, else use SYS V */
  {
    char *systype = getenv ("SYSTYPE");
    if( systype != NULL )
      envinit[1] = systype - strlen("SYSTYPE=");
    else
      envinit[1] = "SYSTYPE=sys5.3";
  }
#endif /*apollo*/

  environ = &envinit[1];	/* get rid of environment */

#ifdef SIGTTOU
  /*
   * Ignore SIGTTOU before we try to print any messages.
   */
  (void)signal(SIGTTOU, SIG_IGN);
#if defined(HAVE_SETLINEBUF)||defined(setlinebuf)
  setlinebuf(stderr);
#endif
#endif /*SIGTTOU*/
  /*
   * argv[0] must be absolute, else we can't restart.
   */
  /*WGK 98/12/19 

    I find this annoying. It makes running batchd less intuitive, and
    it doesn't show up right in ps listings (which can fixed with
    code.)

    However, it seems simpler just to disable this and disable the
    restart feature, which can be gotten around by just KILLTERM and
    re-running batch.*/

  /*if( argv[0][0] != '/' ){
    fprintf(stderr, 
      "%s: Queued must be invoked as a full absolute pathname\n", argv[0]);
    exit(1);
    }
    */
#ifndef NO_ROOT
  if(getuid() != 0)
    fprintf(stderr, "%s: Real uid is %d, not root\n", argv[0], getuid()), exit(1);
  if(geteuid() != 0)
    fprintf(stderr, "%s: Effective uid is %d, not root\n", argv[0], geteuid()), exit(1);
#else
  /*Paranoid check because we use system() if NO_ROOT is on.*/
  if(!geteuid() || !getuid() ) {
    fprintf(stderr, "queued.c was compiled with NO_ROOT and should not be run as a privileged user.\n Please re-compile without NO_ROOT if you wish to run GNU Queue as root.\n Re-run ./configure with the --enable-root option to do this.\n See manual for additional information on the NO_ROOT option.\n");
    exit(2);
  }
#endif /*NO_ROOT*/


#ifdef QUEUE_MANAGER
  /*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
  while ((i = getopt_long(argc, argv, "d:p:t:Dvs:", longopts, NULL)) != EOF)
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/
#else
    while ((i = getopt_long(argc, argv, "d:p:t:Dv", longopts, NULL)) != EOF)
#endif /*QUEUE_MANAGER*/

    switch (i) {
    case 'd':	/* -d spooldir */
      spooldir = optarg;
      break;
    case 'p':
      pidfile = optarg;
      break;
    case 't':
      sleeptime = atoi(optarg);
      if ((int)sleeptime <= 0)
	goto usage;
      break;
    case 'D':
      debug++;
      break;
    /*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
#ifdef QUEUE_MANAGER
    case 's':
      seed = atoi(optarg);
      break;
#endif /*QUEUE_MANAGER*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/
    case 'v':
      fprintf(stderr, "Queued from GNU Queue version %s\n Copyright 1998-2000 W. G. Krebs <wkrebs@gnu.org> and others.\n GNU Queue comes with absolutely no warranty; see COPYING for details.\n\nBug reports to <bug-queue@gnu.org>\n", VERSION);
      exit(0);

    default:
    usage:
      fprintf(stderr, "Queued from GNU Queue version %s\n Copyright 1998-2000 W. G. Krebs <wkrebs@gnu.org> and others\n GNU Queue comes with absolutely no warranty; see COPYING for details.\n\nBug reports to <bug-queue@gnu.org>\n", VERSION);
#ifdef QUEUE_MANAGER
      fprintf(stderr, "Usage: %s [-d dir] [-s seed value] [-p file] [-t time]\n", argv[0]);
#else
      fprintf(stderr, "Usage: %s [-d dir] [-p file] [-t time]\n", argv[0]);
#endif /*QUEUE_MANAGER*/

      fprintf(stderr, "-d or --spooldir Queue spooldir\n");
      fprintf(stderr, "-p or --pidfile pidfile prefix\n");
      fprintf(stderr, "-t or --sleeptime Interval to re-check Queues\n");
      fprintf(stderr, "-D or --debug Increment debug level [runs in foreground]\n");
#ifdef QUEUE_MANAGER
      fprintf(stderr, "-s Initial seed value for random generator\n");
#endif /*QUEUE_MANAGER*/
      fprintf(stderr, "-v or --version Version\n");
      fprintf(stderr, "--help This menu\n");
      fprintf(stderr, "See accompanying manual for additional options.\n");
      exit(1);
    }
  if (optind < argc)
    goto usage;	/* spurious args */

  /*WGK 98/12/23 Make sure we are in the ACL.*/
  {

    int flag = 1;
    char *cannonhost = strdup(canonicalhost(myhostname()));
    if(!Hosts) ReadHosts(QHOSTSFILE);
    for (i=0;i<NHosts;++i) 
      if (strcasecmp(cannonhost, Hosts[i].host)==0) 
	flag = 0;
    if (flag) {
      fprintf(stderr, "This host, %s, is not listed in the host access control list file %s.\nClients will not be able to submit jobs.\n Exiting.\n", cannonhost, QHOSTSFILE);
      exit(2);
    }
    free(cannonhost);
  }

  readcookie();


  /*
   * Make sure we start with a clean signal state.
   */
  for (i = 1; i <= NSIG; i++)
    (void) signal(i, SIG_DFL);
#if defined(HAVE_SIGSETMASK)||defined(sigsetmask)
  (void)sigsetmask(0);		/* unblock all signals */
#endif
  (void)umask(022);		/* set reasonable mask */
  if(chdir(spooldir) < 0)
    fprintf(stderr, "%s: Can't chdir to %s: %s\n", argv[0], spooldir, syserr()), exit(1);
  if ((spooldirdot = opendir(".")) == NULL)
    fprintf(stderr, "%s: Can't open spool dir %s: %s\n",
	    argv[0], spooldir, syserr()), exit(1);
#ifdef linux
  /*WGK 1998/08/05 Hack: in GNU/Linux, file descriptor is first item
    in DIR structure. For some reason the compiler will not let me
    access the 'fd' entry in the structure. GNU/Linux differs here
    from other OSes in that it has named the descriptor 'fd' rather
    than 'dd_fd'.*/

  if (fstat(*( (int*) spooldirdot), &sbuf) < 0)
#else
  if (fstat(spooldirdot->dd_fd, &sbuf) < 0)
#endif
    fprintf(stderr, "%s: fstat %s: %s\n",
	    argv[0], spooldir, syserr()), exit(1);
#ifndef NO_ROOT
  if (sbuf.st_uid != 0)
    fprintf(stderr, "%s: %s is not owned by root\n",
	    argv[0], spooldir), exit(1);
#else
  if (sbuf.st_uid != getuid())
    fprintf(stderr, "%s: %s is not owned by you. You must own\n the spooldir when running GNU Queue as NO_ROOT.\n",
	    argv[0], spooldir), exit(1);
#endif
  if ((sbuf.st_mode & S_IWGRP) | (sbuf.st_mode & S_IWOTH))
    fprintf(stderr, "%s: %s: mode %03o allows general write\n",
	    argv[0], spooldir, sbuf.st_mode), exit(1);
  if ((f = fopen(pidfile, "r")) != NULL) {
    if (fscanf(f, "%d", &i) == 1 && i > 1 && kill(i, 0) == 0) {
      fprintf(stderr, "%s: Queued seems to be running already (%s contains pid %d). Please check.\n", argv[0], pidfile, i);
      exit(1);
    }
    fclose(f);
  }
#ifndef __hpux_system9
  openlog("queue_bd", LOG_PID, LOG_DAEMON);
#else
  openlog("queue_bd", LOG_PID|LOG_DAEMON);
#endif
  if( debug ){
    char fname[MAXPATHLEN];
    /* Start with fresh debug file */
    sprintf(fname, "%s.bak", debugfile);
    (void) rename(debugfile, fname);
  } else { 
    if ((i = fork()) == -1)
      error("Could not fork\n");
    if (i) {
#if defined(__sgi)||defined(__hpux)
      sleep(1);	/* avoid weird bug on IRIS, HPUX, elsewere.*/
#endif
      _exit(0);
    }
  }
  batchdpid = getpid();
  running = (struct running *) emptyq();
  queue_bs = (struct queue_b *) emptyq();

	/* Log my pid */
  if ((f = fopen(pidfile, "w")) != NULL) {
    fprintf(f, "%d\n", batchdpid);
    if( fclose(f) == EOF ){
      saverrno = errno;
      fprintf(stderr, "Failure on fclose(%s): %s\n",
	      pidfile, syserr() );
      errno = saverrno;
      error2("Failure on fclose(%s): %s\n",
	     pidfile, syserr() );
    }
  }
  else{
    saverrno = errno;
    fprintf(stderr, "Failure on fopen(%s, \"w\"): %s\n",
	    pidfile, syserr() );
    errno = saverrno;
    error2("Failure on fopen(%s, \"w\"): %s\n", pidfile, syserr() );
  }

  if (!debug) {
    for (i = 0; i < sizeof ignore/sizeof ignore[0]; i++)
      (void)signal(ignore[i], SIG_IGN);
    nice(-40);
    nice(20);
    nice(0);

    /*WGK: moved controlling tty code to after fork in child.*/
    /*Batchd now holds on to controlling tty.*/
	

    /*
     *  Get rid of controlling tty
     */ 
#if !defined(HAVE_SETSID)&&!defined(setsid)
#ifdef TIOCNOTTY
    if((i = open("/dev/tty", 2)) != -1) {
      (void) ioctl(i, (long)TIOCNOTTY, (char *)0);
      close(i);
    }
#endif /*TIOCNOTTY*/
#ifdef SETPGRP_VOID
    setpgrp();
#else
    setpgrp(0, 0);
#endif /*SETPGRP_VOID*/
#else /*HAVE_SETSID*/
    (void) setsid();
#endif /*HAVE_SETSID*/
  }


  (void)signal(SIGALRM, sigalrm);
  (void)signal(SIGHUP,  sighandler);
  (void)signal(SIGTERM, sighandler);
  (void)signal(SIGINT,  sighandler);
  (void)signal(SIGQUIT, sighandler);
  (void)signal(SIGCHLD, sigchld);
#ifdef SIGUSR1
  (void)signal(SIGUSR1, toggledebug);
#endif
#ifdef SIGUSR2
  (void)signal(SIGUSR2, restartdaemon);
#endif


  systemflag = 0;		/* Clear HP-UX "system" call flag */
  /*
   * Go to sleep for a while before flooding the system with
   * jobs, in case it crashes again right away, or the
   * system manager wants to prevent jobs from running.
   * Send a SIGALRM to give it a kick-start.
   */
  

  if (!debug) {
    alarm(sleeptime);

    /* WGK: Rather than do a sigpause(), here, we do a check_query
       here, which will cause us to wake up immediately if someone
       submits a new job in the first few minutes. This could cause
       the batchd to flood the system with new jobs in the event of an
       immediate query, but is unlikely to cause any real problems.*/

    check_query();

    (void) alarm(0);
  }

  /*WGK 98/12/23. Check if QHOSTSFILE has changed; if so, re-read
    it.*/


  if (stat(QHOSTSFILE, &sbuf) < 0) {
    mperror1("Can't stat(%s/.) - queued exiting",
	     QHOSTSFILE);
    exit(1);
  }
  if(sbuf.st_mtime != qhostsfile_mtime) {
    qhostsfile_mtime = sbuf.st_mtime;
    mdebug("re-reading the qhostsfile\n");
    ReadHosts(QHOSTSFILE);
  }
  /*End WGK 98/12/23*/


  for(;;) {
    /*
     *  Check if the main spool directory changed.  If so, make a new
     *  list of potential queue_b entries.  
     */
#ifdef linux
    /*WGK 1998/08/05 Hack: in GNU/Linux, file descriptor is first item
      in DIR structure.*/
    if (fstat(*( (int*) spooldirdot), &sbuf) < 0) 
#else
    if (fstat(spooldirdot->dd_fd, &sbuf) < 0) 
#endif
      {
	mperror1("Can't fstat(%s/.) - queued exiting",
		 spooldir);
	exit(1);
      }
    if(sbuf.st_mtime != spool_mtime) {
      spool_mtime = sbuf.st_mtime;
      mdebug("re-reading spool directory\n");
      readqueue_bs(spooldirdot);
    }
    getload(load);
    mdebug3("load averages are %d, %d, %d\n", load[0],
	    load[1], load[2]);
    
    /*
     * Get the current time at most once during
     * the queue_b loop below.
     */
    needtime = 1;
    
    /*
     *  Now check each potential queue_b entry.
     */
    FOREACH (qp, queue_bs) {
      mdebug1("checking queue %s\n", qp->q_name);
      /*
       * If we get a signal to restart the daemon, we
       * have to drain all the queue_bs first.
       */
      if( restartflag )
	drainqueue_b(qp);
      /*
       *  Check if its profile file changed; if so re-read it.
       *  Be generous, if somebody deleted it don't affect
       *  the queue_b or what's running.
       */
      if(stat(qp->q_profile, &sbuf) < 0) {
	int e = errno;
	if( stat(qp->q_name, &sbuf) < 0 ){
	  /* queue_b vanished; this will be
	   * handled by readqueue_bs() next
	   * time around.
	   */
	  mperror1("Can't stat(%s); queue_b vanished?",
		   qp->q_name);
	} else{
	  /*
	   * Profile has vanished.
	   * Complain, but only once; otherwise
	   * we'll flood the mail system.
	   */
	  if (!qp->q_noprofile) {
	    errno = e;
	    mperror1("Can't stat(%s); profile missing?",
		     qp->q_profile);
	    qp->q_noprofile = 1;
	  }
	}
      } else if(sbuf.st_mtime != qp->q_profmtime) {
	qp->q_profmtime = sbuf.st_mtime;
	qp->q_noprofile = 0;
	mdebug1("\tnew profile modify time; reading profile %s\n",
		qp->q_profile);
	readpro(qp);
      }
      /*
       * If not draining, we check for more jobs to add to our
       * internal lists.  We could always add jobs, even if draining,
       * since draining prevents jobs from starting, but that would
       * just waste memory.
       * In fact, we should probably release the lists of non-running
       * jobs in drained queue_bs -- we can get the lists back when
       * the queue_b is restarted.  Check the queue_b directory change
       * time.  If it changed, then something new might have been
       * queue_bd or deleted.  Unfortunately, output files are also
       * created in the queue_b directory, causing unnecessary
       * requeue_bing when they get added and deleted.  */
	if( qp->q_drain == Q_STARTNEW && qp->q_migrate == Q_STARTNEW ){
	  DIR *cfdir;
	  if( (cfdir=opencfdir(qp)) != NULL ){
	    /*WGK: added qp->q_requeue_b to force requeue_b to get
	      around mtime 1sec granularity.*/

#ifdef linux
	    /*WGK 1998/08/05 Hack here again
	      to get this to compile under GNU/Linux.*/
	    if((qp->q_requeue_b)||(fstat(*( (int*) cfdir), &sbuf) >= 0 
				   && sbuf.st_mtime != qp->q_mtime))
#else
	    if((qp->q_requeue_b)||(fstat(cfdir->dd_fd, &sbuf) >= 0
				    && sbuf.st_mtime != qp->q_mtime))
#endif
	      {
		qp->q_mtime = sbuf.st_mtime;
		mdebug1("\tnew '%s' modify time; checking for newly queue_bd jobs\n",
			qp->q_cfdir);
		requeue_b(qp);
	      }
	    closedir(cfdir);
	  }
	}
	/*
	 * Handle starting and stopping our running jobs and start new
	 * jobs if conditions are right.  Even if draining we call
	 * runqueue_b() for the purpose of starting and stopping
	 * current jobs.  
	 */
	runqueue_b(qp, load);
    }

    /*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
/*This section of code is (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

    if ( totalrunning != 0 )  /* if there is a job running */
    {
       /* need to tell queue_manager about this job? */
       if ( connect_bit == 0 )
       {
	  /* MY_DEBUG */
	  printf("Inside connect_bit\n");

	  /* save all the information in the global packet */
	  gethostname(packet.hostname, MAXHOSTNAMELEN - 1);
	  packet.job_pid = forkedqpid;
	  packet.user_id = uid;  
	  strcpy(packet.user, username);  

	  /* Now make a connection to the queue_manager */
	  {
	     int sockfd;  /* socket descriptor */
	     char msg_bit[2] = "1";  /* denotes message type */

	     /* counter to keep track of the number of times
	      * we try connecting to the queue manager */
	     int counter = 1;

	     /* fill in the server port number */
  	     serv_addr.sin_port = htons(PORTNUM3);

	     /* We try to connect ten times to prevent an
	      * overloaded system from keeping us down. */

	     while ( counter <= 10 )
	     {
	        /* Open a TCP socket */
		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	 	{
			fprintf(stderr, "Queued.c Error: can't get TCP socket\n");
			return -1;
		}

		/* Set socket options for TCP socket */
		if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
				(char *) &sendbuff, sizeof(sendbuff)) < 0 )
		{
			fprintf(stderr, "Queued.c Error: setting TCP socket options\n");
			return -1;
		}

		if ( connect(sockfd, (struct sockaddr *) &serv_addr,
					sizeof(serv_addr)) < 0 )
		{
			counter++;
			printf("Connecting to Queue_Manager...\n");
			/* sleep a few seconds before connecting again */
			sleep(3);
		}
		else
		   break;
	     }

	     /* if connection was successful, set bit so that we
	      * do not connect to the queue_manager again */
	     if ( counter != 11 )
	     {
		     connect_bit = 1;

		     /* First, send the message bit */
		     if ( send(sockfd, msg_bit, sizeof(msg_bit), 0) < 0 )
		     {
			     fprintf(stderr, "Queued.c Error: can't send running msg_bit\n");
			     return -1;
		     }

		     /* Next, send the information packet */
		     if ( send(sockfd, &packet, sizeof(packet), 0) < 0 )
		     {
			     fprintf(stderr, "Queued.c Error: can't send packet\n");
			     return -1;
		     }
	     }

	  }  /* closes off connection block */

	} /* closes off "if connect_bit" */

    } /* closes off "if total_running" */

    /* MY_DEBUG */
    {
    char my_host[MAXHOSTNAMELEN];
    gethostname(my_host, MAXHOSTNAMELEN - 1);
    printf("host name: %s\n", my_host);
    printf("totalrunning: %d\n", totalrunning);
    printf("forkedqpid: %d\n", forkedqpid);
    }
    /* End of MY_DEBUG */

#endif /*QUEUE_MANAGER*/
/*End of code copyrighted by Texas Instruments.*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

    /*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
/*This section of code is (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER
    {
	if (timeout_connect == 0)  /* initialize the timeout_connect value */
	{
	   /* generate a random number between max_modulo and min_modulo and assign
	    * timeout_connect to this value */
	   srandom(seed);
	   timeout_connect = random() % MAX_MODULO;
	   if (timeout_connect < MIN_MODULO)
	      timeout_connect = timeout_connect + MIN_MODULO;

	   /* MY_DEBUG */
	   printf("timeout_connect: %d\n", timeout_connect);
	}
	else if (timeout_counter != timeout_connect)
	{
	   /* it's not time to connect to the queue_manager yet, so just increment counter */
	   ++timeout_counter;
	}
	else
	{
	   /* time to connect to the queue_manager */
	   int sockfd;

	   /* MY_DEBUG */
	   printf("Time to Connect!\n");

	   /* generate a new random number for the next timeout connection */
	   timeout_connect = random() % MAX_MODULO;
	   if (timeout_connect < MIN_MODULO)
	      timeout_connect = timeout_connect + MIN_MODULO;

	   /* reset the counter */
	   timeout_counter = 0;

	   /* MY_DEBUG */
	   printf("timeout_connect: %d\n", timeout_connect);

	   /* now connect to the queue_manager */

	   /* fill in the packet information to send to the queue_manager */
	   gethostname(packet.hostname, MAXHOSTNAMELEN - 1);
	   packet.job_pid = forkedqpid;
	   packet.user_id = uid;  
	   strcpy(packet.user, username);

  	   /* fill in the server port number so that we can connect to it */
  	   serv_addr.sin_port = htons(PORTNUM5);

           /* open a TCP socket */
	   if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	   {
		fprintf(stderr, "Queued.c Error: can't get TCP socket\n");
		return -1;
	   }

	   /* set socket options for TCP socket */
	   if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
				(char *) &sendbuff, sizeof(sendbuff)) < 0 )
	   {
		fprintf(stderr, "Queued.c Error: setting TCP socket options\n");
		return -1;
	   }

	   if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0 )
		printf("Can't connect to queue_manager\n");
	   else
	   {
		/* send the packet over to the queue_manager */
	        if ( send(sockfd, &packet, sizeof(packet), 0) < 0 )
	        {
		     fprintf(stderr, "Queued.c Error: can't send packet\n");
		     return -1;
	        }
	   }

	   /* close the socket */
	   close(sockfd);
	}
    }
#endif /*QUEUE_MANAGER*/
/*End of code copyrighted by Texas Instruments.*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

    /*
     * We have started as many jobs as we can.  Now wait for signals.
     * ALRM is sent by BATCH when a new job is queue_bd; we also send
     * it to ourselves every sleeptime seconds so we keep track of
     * changing load.  SIGUSR2 turns on a flag that causes us to stop
     * accepting new jobs, drain the current ones, and after
     * everything is gone reload ourself from argv[0].  This makes
     * starting a new batchd much easier; replace the binary and send
     * batchd SIGUSR2.

     * Any job terminating causes CHLD.  Make sure SIGCHLD is
     * permitted, so we can get job statuses.
     *
     * If waitforchild() found no dead kids, don't rescan the queue_b,
     * just reset the alarm.  This seems to happen if NOKMEM is
     * defined, since the popen()/pclose() generates a SIGCHLD.  */
  resetalarm:
    alarm(sleeptime);
    while( sigalrmflag == 0  &&  sigchldflag == 0 ) {
      
      /*Check sockets*/
      mdebug("Checking for queries.\n");
      check_query();
      
    }
    (void) alarm(0);
#ifdef __hpux
    /*
     * HP-UX: check if any of our child processes have overstepped
     * their cpu limit.  GRS */

    /*WGK: you will need a non-GNU but freely distributable U of
      Waterloo package for this; all it does is implement CPU_LIMIT on
      some HP-UX systems that do not properly support it in the
      kernel. Contact me if you need this package.*/
#ifdef HAVE_UWATERLOO
    if (sigalrmflag)
      CheckHPRunTimes(); 
#endif /*HAVE_UWATERLOO*/
#endif /*__hpux*/
    sigalrmflag = 0;
    if (sigchldflag) {
      mdebug("SIGCHLD flag set; running waitforchild()...\n");
      sigchldflag = 0;
      if (waitforchild() == 0) {
	mdebug("No child found in waitforchild()\n");
	goto resetalarm;
      }
    }
    
    if (restartflag && totalrunning == 0 ){
      mdebug("Queues drained; starting new queued...\n");
      FOREACH (qp, queue_bs){
	muser1(qp->q_supervisor,
	       "%s: restarting: queue daemon restarting\n",
	       qp->q_name);
	queue_bstat(qp, "Daemon restarting\n");
      }
      
      /* Arrange that almost everything get closed on exec.
       */
      for(i=getdtablesize() -1; i >= 3; i--)
	(void) fcntl(i, F_SETFD, 1);/* close-on-exec */
      (void) unlink(pidfile);
      execv(argv[0], argv);
      error1("Cannot execv '%s'; queued exiting\n", argv[0]);
    }
    mdebug("looking around...\n");
  }
}



void
sigchld(void)
{
  sigchldflag = 1;


  /*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
/*This section of code is (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

  /* MY_DEBUG */
  mdebug("SIGCHLD\n");
  printf("In sigchld() function\n\n");

  forkedqpid = 0;  /* set pid of forked off queue daemon back to 0 */
  connect_bit = 0;  /* set connect_bit back to 0 */

  /* Make a connection to the queue_manager to let it know that a job has terminated */
  {
     int sockfd;  /* socket descriptor */
     char msg_bit[2] = "2";  /* denotes message type */

     /* counter to keep track of the number of times we try connecting to the qmanager */
     int counter = 1;

     /* fill in the server port number */
     serv_addr.sin_port = htons(PORTNUM3);

     /* fill in the packet information to send to the queue_manager */
     gethostname(packet.hostname, MAXHOSTNAMELEN - 1);
     packet.job_pid = forkedqpid;
     packet.user_id = uid;  
     strcpy(packet.user, username);
     
     /* We try to connect ten times to prevent an
      * overloaded system from keeping us down. */
     while ( counter <= 10 )
     {
        /* Open a TCP socket */
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "Queued.c Error: can't get TCP socket\n");
		return;
	}

	/* Set socket options for TCP socket */
	if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
				(char *) &sendbuff, sizeof(sendbuff)) < 0 )
	{
		fprintf(stderr, "Queued.c Error: setting TCP socket options\n");
		return;
	}

	if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0 )
	{
		counter++;
		printf("Connecting to Queue_Manager...\n");
		/* sleep a few seconds before connecting again */
		sleep(3);
	}
	else
	   break;
     }

     /* If the connection was successful. */
     if ( counter != 11 )
     {
	     /* First, send the message bit */
	     if ( send(sockfd, msg_bit, sizeof(msg_bit), 0) < 0 )
	     {
		     fprintf(stderr, "Queued.c Error: can't send terminated msg_bit\n");
		     return;
	     }

	     /* Next, send the information packet */
	     if ( send(sockfd, &packet, sizeof(packet), 0) < 0 )
	     {
		     fprintf(stderr, "Queued.c Error: can't send packet\n");
		     return;
	     }
     }

     /* close off the socket connection */
     close(sockfd);
  }

#endif /*QUEUE_MANAGER*/
/*End of code copyrighted by Texas Instruments.*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

}

void
sigalrm(void)
{
  (void)signal(SIGALRM, sigalrm);
  sigalrmflag = 1;
  mdebug("SIGALRM\n");
}

/* This will probably be called during a SHUTDOWN of the system.
 * Hope we have enough time to send all these messages...
 */
void
sighandler(int sig)
{
  register struct queue_b *qp;

#if defined(HAVE_SIGBLOCK)||defined(sigblocK)
  (void) sigblock(~0);		/* Block everything */
#else /*HAVE_SIGBLOCK*/
  (void)signal(SIGALRM, SIG_IGN);
  (void)signal(SIGHUP,  SIG_IGN);
  (void)signal(SIGINT,  SIG_IGN);
  (void)signal(SIGQUIT, SIG_IGN);
  (void)signal(SIGTERM, SIG_IGN);
  (void)signal(SIGCHLD, SIG_IGN);
#endif /*HAVE_SIGBLOCK*/
  mdebug1("Signal %d\n", sig);
  (void) unlink(pidfile);
  /*
   * First send the messages, then do the aborting, in case we are
   * short of time (e.g. during SHUTDOWN).  
   */
  /*
    WGK

    Unlike original batchd, there is no reason to abort all jobs when
    shutting down, as many will be interactive jobs partially
    supervised on the remote end by the stub.  Just disassociate from
    jobs.
  */
  mdebug("Recieved fatal signal, shutting down leaving all jobs unsupervised.\n");
  /*
    FOREACH (qp, queue_bs)
    muser2(qp->q_supervisor,
    "%s: %s -- Aborting all jobs; queue daemon exiting.\n",
    qp->q_name, (sig<NSIG) ? sys_siglist[sig] : "SHUTDOWN" );
    FOREACH (qp, queue_bs)
    abortall(qp);
  */
  /*_cleanup();*/			/* Flush stdio buffers */
  fflush(NULL);
  signal(sig, SIG_DFL);		/* Permit the signal */
  kill(batchdpid, sig);
#if defined(HAVE_SIGSETMASK)||defined(sigsetmask)
  (void) sigsetmask(0);		/* Unblock everything */
#endif /*HAVE_SIGSETMASK*/
  exit(1);
}

void
toggledebug(void)
{
  debug = !debug;
}

void
restartdaemon(void)
{
  mdebug("RESTART daemon signal\n");
  restartflag = 1;		/* start draining queue_bs */
  kill(batchdpid, SIGALRM);	/* wake us up */
}

/* Called from main() if we've received a SIGCHLD child ending signal.
 * Returns number of child processes reaped.  */
int	
waitforchild(void)
{
  register int pid;
  register DOUBLE totalcpu;

#ifndef CLK_TCK
#define CLK_TCK 60
#endif /*CLK_TCK*/

#if !defined(solaris)&&!defined(__hpux)

#ifdef HAVE_WAIT3
  int wait_status;
  struct rusage rusage;
#else 
  int wait_status;
  struct tms tms;
  DOUBLE oldtotalcpu, newtotalcpu;
#endif /*HAVE_WAIT3*/
  int found = 0;	/* "found a child" flag */

#ifdef HAVE_WAIT3
  while ((pid = wait3((void *)&wait_status, WNOHANG, &rusage)) > 0) {
    totalcpu = (double)( rusage.ru_utime.tv_sec
			 + rusage.ru_stime.tv_sec )
      + (double)( rusage.ru_utime.tv_usec
		  + rusage.ru_stime.tv_usec ) / 1000000.0;
#else /*HAVE_WAIT3*/
    (void) times(&tms);
    oldtotalcpu = (DOUBLE)tms.tms_cutime + tms.tms_cstime;
#ifdef __hpux
    while ((pid = wait3((void *)&wait_status, WNOHANG, NULL)) > 0) 
#else
    while ((pid = wait((void *)&wait_status)) > 0) 
#endif
      {
	(void) times(&tms);
	newtotalcpu = (DOUBLE)tms.tms_cutime + tms.tms_cstime;
	totalcpu = (newtotalcpu - oldtotalcpu) / (DOUBLE)CLK_TCK;
	oldtotalcpu = newtotalcpu;
#endif /*HAVE_WAIT3*/
	mdebug3("wait exit pid=%d, stat=0%o, cpu=%.1f\n",
		pid, wait_status, totalcpu);
	terminated(pid, wait_status, totalcpu);
	
#else /*You are using Solaris or HP-UX.*/
	
	union { int w_status; } wait_status;
#ifdef HAVE_WAIT3
	struct rusage rusage;
#else 
	struct tms tms;
	DOUBLE oldtotalcpu, newtotalcpu;
#endif /*HAVE_WAIT3*/
	int found = 0;	/* "found a child" flag */

#ifdef HAVE_WAIT3
	while ((pid = wait3(&wait_status, WNOHANG, &rusage)) > 0) {
	  totalcpu = (double)( rusage.ru_utime.tv_sec
			       + rusage.ru_stime.tv_sec )
	    + (double)( rusage.ru_utime.tv_usec
			+ rusage.ru_stime.tv_usec ) / 1000000.0;
#else /*HAVE_WAIT3*/
	  (void) times(&tms);
	  oldtotalcpu = (DOUBLE)tms.tms_cutime + tms.tms_cstime;
	  while ((pid = wait(&wait_status)) > 0) {
	    (void) times(&tms);
	    newtotalcpu = (DOUBLE)tms.tms_cutime + tms.tms_cstime;
	    totalcpu = (newtotalcpu - oldtotalcpu) / (DOUBLE)CLK_TCK;
	    oldtotalcpu = newtotalcpu;
#endif /*HAVE_WAIT3*/
	    mdebug3("wait exit pid=%d, stat=0%o, cpu=%.1f\n",
		    pid, wait_status.w_status, totalcpu);
	    terminated(pid, wait_status.w_status, totalcpu);
#endif /*solaris or hpux*/

	    /*
	     * If "terminated" couldn't find the job, and "systemflag"
	     * is 1 (indicating we just made a call to "system"),
	     * we've (probably) got the status of the process run by
	     * the "system" call. If so, save this status in
	     * "hpuxstatus" for future reference.
	     *
	     * Otherwise, remove this job from the processes being watched.
	     */
	    if (terminatedfailed && systemflag) {
	      systemflag = 0;
#ifdef solaris
	      hpuxstatus = wait_status.w_status;
#else
	      hpuxstatus = wait_status;
#endif
	    } else {
	      /*WGK: See my earlier comment.*/
#ifdef __hpux
#ifdef HAVE_UWATERLOO
	      RmHPWatch(pid);
#endif /*HAVE_UWATERLOO*/
#endif /*__hpux*/
	}
	found++;		/* Set child found flag */
   }
  (void)signal(SIGCHLD, sigchld);
  return found;
}

extern int sys_nerr;

#ifndef linux
extern char *sys_errlist[];
#endif

char *
syserr(void)
{
  static char buf[80];
	
  if (errno >= 0 && errno < sys_nerr)
    return(sys_errlist[errno]);
  sprintf(buf, "Unknown error %d", errno);
  return(buf);
}

void	
mperror(char *s)
{
  char *p;
  char str[STRSIZ];	/* must have own internal buffer */

  if( (p=index(s, '\n')) != NULL )
    *p = '\0';
  sprintf(str, "%s: %s\n", s, syserr());
  if( p )
    *p = '\n';
  merror(str);
}

int
merror(char *s)
{
#define LOW_ERROR_MAIL_LIMIT 10
#define HIGH_ERROR_MAIL_LIMIT 2000

  /*I imagine sooner or later someone will get annoyed because queued
    filled their mail spool. It hasn't happened yet, but let's not let
    it happen. WGK 1999/01/27 */

  static int counter = 0;

  counter ++;

  /*If we're going to silence error messages, we need a high limit to
    ensure nothing is busy looping through the error mail system.*/

  if(counter == HIGH_ERROR_MAIL_LIMIT) {
    mymerror("HIGH_ERROR_MAIL_LIMIT exceeded. Shutting down queued due to too many internal errors.");
    raise(SIGTERM);
  }

  /*Ideally, we would like to reset counter after, say, 24 hours. This
    could be done by memorizing the time the counter was last updated
    and clearing it if more than 24 hours have passed.*/


  if(counter == LOW_ERROR_MAIL_LIMIT) {
    mymerror("LOW_ERROR_MAIL_LIMIT exceeded. Silencing queued mail error output. Restart queued to resume mail error output.");
    return(0);
  }

  if (counter > LOW_ERROR_MAIL_LIMIT) return(0);

  mymerror(s);
  return(1); /*FIXME: is this return value ever used? If not, just
               make the function void*/
}

void	
mymerror(char *s)
{

  FILE *mail;

  if (debug && envuser != NULL)
    mail = sendmail(envuser, envuser);
  else
    mail = sendmail(QUEUE_MAIL_USERID, (char *)NULL);
  fprintf(mail, "Subject: queued error on %s: %s\n%s", myhostname(),
	  s, s);
  mailclose(mail);
}

/*
 * Fatal error; causes batchd or child process to exit.
 */
void
error(char *s)
{
  char str[STRSIZ];	/* must have own internal buffer */

  sprintf(str, "QUEUED fatal error; queued terminating:\n - %s", s);
  merror(str);
  /*	_cleanup();*/
  fflush(NULL);
  abort();
  exit(1);
}

void
muser(char *s, char *m)
{
  register FILE *mail;
  time_t now;

  /*
   * Sometimes the q_supervisor field will be NULL.
   */
  if(s == NULL || strcmp(s, DEV_NULL) == 0)
    return;
  if( index(s, '/') ){
    if((mail = fopen(s, "a")) == NULL) {
      mperror1("Can't fopen(%s, \"a\"); using stderr", s);
      mail = stderr;
    }
    (void) fcntl(fileno(mail), F_SETFL, O_APPEND);
    now = time((time_t *)0);
    fprintf(mail, "%-15.15s %s ", ctime(&now) + 4, myhostname());
  } else {
    /*
     * If it's going to a real user, which so far is
     * just for chatty job-startup messages,
     * throw in a subject. ..sah
     */
    mail = sendmail(s, (char *)NULL);
    fprintf(mail, "Subject: batch queue_b on %s: %s\n", myhostname(),
	    m);
  }
  fputs(m, mail);
  mailclose(mail);
}

FILE *
sendmail(register char *to, register char *from)
{
  register FILE *mailer;
  register int pid;
  register struct running *rp;
  struct {int read; int write;} mailp;

  if( to == NULL ){
    fprintf(stderr, "QUEUED: internal error: sendmail to NULL\n");
    return stderr;
  }
  if( from == NULL )
    from = "queued";

  if (debug) {
    fprintf(stderr, "SENDMAIL: To '%s' from '%s': ", to, from);
    (void) fflush(stderr);
    return stderr;
  }
  if(pipe((int *)&mailp) < 0) {
    perror("queued pipe()");
  err:
    fprintf(stderr, "QUEUED: sendmail to '%s' failed\n", to);
    return stderr;
  }
  switch(pid = fork()) {
  case 0:
    if (mailp.read != 0) {
      close(0);
      dup(mailp.read);
      close(mailp.read);
    }
    close(mailp.write);
#ifndef apollo	/* Don't do this on Apollo or it will never finish */
    nice(4);	/* lower priority */
#endif
#ifndef DONT_USE_SENDMAIL
    /* The -f option seems to be the only way to prevent
     * bounced mail from bouncing back to root.  Setting
     * the header lines alone doesn't work.  -IAN!
     */



    /*WGK BUGFIX 1999/1/02 If not running as root,
      -f causes sendmail to execute bug ignore the incoming mail. */
#ifndef NO_ROOT
    execle("/usr/lib/sendmail", "sendmail",
	   "-f", from, to, (char *)0, envinit);
    execle("/usr/sbin/sendmail", "sendmail",
	   "-f", from, to, (char *)0, envinit);
#else
    execle("/usr/lib/sendmail", "sendmail",
	   to, (char *)0, envinit);
    execle("/usr/sbin/sendmail", "sendmail",
	   to, (char *)0, envinit);
#endif /*NO_ROOT*/
    perror("QUEUED execl /usr/lib/sendmail failed");
    fflush(stderr);
    execle("/usr/ucb/mail", "mail", to, (char *)0, envinit);
    perror("QUEUED execl /usr/ucb/mail failed");
#endif /*DONT_HAVE_SENDMAIL*/
    execle("/bin/rmail", "rmail", to, (char *)0, envinit);
    perror("QUEUED execl /bin/rmail failed");
    fflush(stderr);
    execle("/bin/mail", "mail", to, (char *)0, envinit);
    perror("QUEUED execl /bin/mail failed");
    fflush(stderr);
    _exit(1);
    /*NOTREACHED*/
  case -1:
    goto err;
  default:
    close(mailp.read);
  }
  nkids++;
  rp = (struct running *)
    enqueue_b((struct qelem_queue *)running, (int)sizeof *rp);
  rp->r_pid = pid;
  mailer = fdopen(mailp.write, "w");
  if(mailer == NULL) {
    perror("QUEUED sendmail: FDOPEN failed");
    close(mailp.write);
    goto err;
  }
  /*
   * Just one \n here so we can throw in a subject. ..sah
   */
  fprintf(mailer, "To: %s\n", to);
  /*WGK 1999/01/02 Many sendmails are rather paranoid as well,
    and discard are email as forged if we claim it came from root.*/
#ifndef NO_ROOT
  fprintf(mailer, "From: The Queue Daemon <root>\n");
#else
  fprintf(mailer, "From: The Queue Daemon c/o <%s>\n", to);
#endif /*NO_ROOT*/
  fflush(mailer);
  if (ferror(mailer)) {
    fclose(mailer);
    goto err;
  }
  return mailer;
}

void	
mailclose(FILE *file)
{
  if(file != stderr)
    (void) fclose(file);
  else
    (void) fflush(file);
}

/*
 * Read the home directory, looking for all current queue_b entries.
 * Symbolic links are ignored, so you can symlink queue_b names
 * together and the batch command will put all requests in one queue_b
 * directory.  readqueue_bs() is done any time we wake up and notice
 * the home directory has changed since we last did this.
 *
 * We first turn off the "seen" bit on every queue_b we already know
 * about.  Then, we look at all the queue_bs in the directory.  Any
 * queue_bs left, that have "seen" bits still off, must have been
 * deleted and are to be shut down.  */
void	
readqueue_bs(DIR *dir)
{
  register struct queue_b *qp;
  struct_dir *d;

  struct stat sb;

  FOREACH (qp, queue_bs)
    qp->q_seen = 0;

  rewinddir(dir);
  while((d =  readdir(dir)) != NULL) {
    /*
     *  Ignore everything starting with a dot.
     */
    if( d->d_name[0] == '.' )
      continue;
    if(lstat(d->d_name, &sb) < 0) {
      mperror1("Can't lstat(%s); skipping queue_b",
	       d->d_name);
      continue;
    }
    /*
     * If the queue_b name is a local symlink we can safely ignore
     * it because it must link to some other queue_b name.
     */
    if((sb.st_mode & S_IFMT) == S_IFLNK){
      char buf[MAXPATHLEN];
      int cc;

      if((cc=readlink(d->d_name, buf, (int)sizeof(buf))) < 0){
	mperror1("Can't readlink(%s); skipping queue_b",
		 d->d_name);
	continue;
      }
      buf[cc] = '\0';
      mdebug2("Symlinked queue_b '%s'->'%s'\n",
	      d->d_name, buf);
      if( index(buf, '/') == 0 )
	continue;	/* skip relative symlinks */
      /*
       * Must be a symlink with a slash in it;
       * have to assume it's a different queue_b.
       * stat() it to get its real mode.
       */
      if(stat(d->d_name, &sb) < 0) {
	mperror1("Can't stat(%s); skipping queue_b",
		 d->d_name);
	continue;
      }
    }
    if((sb.st_mode & S_IFMT) != S_IFDIR) {
      errno = ENOTDIR;
      if(debug) mperror1("Junk file name '%s' ignored", d->d_name);
      continue;
    }
    /*
     *  Found a directory.  Do we already know about it?
     */
    FOREACH (qp, queue_bs)
      if (strcmp(d->d_name, qp->q_name) == 0)
	break;
    /*
     *  Was it a new queue_b?
     */
    if (qp == queue_bs) {
      char str[STRSIZ];

      qp = (struct queue_b *)
	enqueue_b((struct qelem_queue *)queue_bs, (int)sizeof *qp);
      /*
       * This stuff is freed by freequeue_b() when
       * the queue_b entry is deleted.
       */
      qp->q_name = strdup(d->d_name);
      sprintf(str, "%s/%s", qp->q_name, Q_QUEUESTAT);
      qp->q_queue_bstat = strdup(str);
      sprintf(str, "%s/%s", qp->q_name, Q_PROFILE);
      qp->q_profile = strdup(str);
      sprintf(str, "%s/%s", qp->q_name, Q_CFDIR);
      qp->q_cfdir = strdup(str);
      qp->q_jobs = (struct job *) emptyq();
      mdebug1("New queue_b '%s'\n", qp->q_name);
    }
    qp->q_seen = 1;
  }
  /*
   * Any directories which have been deleted imply the queue_bs
   * should end violently since we can't get the output files.
   * If jobs are executing, setting q_deleteq will cause
   * the queue_b entry to be released when the last job terminates.
   * If no jobs are executing, we release the entry now.
   */
  FOREACH (qp, queue_bs) {
    if (qp->q_seen)
      continue;
    merror2("'%s': Aborting vanished queue_b containing %d jobs\n",
	    qp->q_name, qp->q_nexec);
    qp->q_deleteq = 1;
    if (qp->q_nexec == 0)
      freequeue_b(qp);
    else
      abortall(qp);
  }
}

/*
 *  (Re-)Read a profile file under the given queue_b entry.
 */
void	
readpro(register struct queue_b *qp)
{
#ifdef HAVE_GETRLIMIT
  static struct rlimit minus_one = { -1, -1 };
  register int ri;
  register int rl;
  register int limit;
#endif /*HAVE_GETRLIMIT*/
  register int i;
  enum { E_ON, E_DRAIN, E_MIGRATE, E_OFF } execflag;
  FILE *pro;
  bat_Mail *mailp;
  char *startmsg;
  enum keyword kwd;
  extern enum keyword yylex();
  extern char *yytext;
  extern int yylineno;

  /*
   *  First set up queue_b to all defaults so if we re-read a file
   *  we don't get the old values.
   */
  freeqstorage(qp);		/* get rid of old info */
  qp->q_usermail = qp->q_supmail = MAIL_END|MAIL_CRASH;
  qp->q_restart = 0;
#ifdef HAVE_GETRLIMIT
  for( i=0; i<RLIM_NLIMITS; i++ )
    qp->q_rlimit[i] = minus_one;
#endif /*HAVE_GETRLIMIT*/

  qp->q_deleteq	= 0;
  qp->q_nochange	= 0;
  qp->q_drain	= Q_STARTNEW;	/* Default is to run the queue_b */
  qp->q_nice	= BAT_INICE;
  qp->q_maxexec	= BAT_IMAXEXEC;
  qp->q_vmaxexec   = BAT_IMAXEXEC;
  qp->q_loadsched	= BAT_ILOADSCHED;
  qp->q_loadstop	= BAT_ILOADSTOP;
  qp->q_restartflag = BAT_IRESTARTFLAG;
  qp->q_checkpointmode = BAT_ICHECKPOINTMODE;
  qp->q_restartmode = BAT_IRESTARTMODE;
  qp->q_checkpoint	= BAT_ICHECKPOINT;
  qp->q_minfree	= BAT_IMINFREE;
  qp->q_oldstat	= -1;
  qp->q_statcntr	= 0;
  qp->q_pfactor = 1;

  /*
   *  Try to read the profile file.  If not there, assume shutdown this
   *  queue_b.
   */
  execflag = E_ON;		/* set later if "exec" used */
  if((pro = fopen(qp->q_profile, "r")) == NULL) {
    mperror1("Can't fopen(%s, \"r\"); draining this queue_b",
	     qp->q_profile);
    drainqueue_b(qp);	/* stop further jobs from starting */
    return;
  }
  lexfile(pro);
#define	LEX(kwd)		(kwd = yylex())
  while((int)LEX(kwd) != 0) {
    switch(kwd) {
    case K_HOST:
      {
	char *thathost;
	LEX(kwd);
	if((thathost = canonicalhost(yytext))==NULL) {
	  mdebug1("canonicalhost() returned error on %s\n", yytext);
	  goto syntaxerr; 
	}
	if(strcasecmp(myhostname(), thathost)!=0) {
	  while(LEX(kwd)!=K_LINE);
	  break;
	}
	continue;
      }
    case K_LINE:
      break;
    case K_EXEC:
      switch(LEX(kwd)) {
      case K_OFF:
				/* abort running jobs */
				/* don't start new jobs */
	execflag = E_OFF;
	break;
      case K_LINE:
      case K_ON:
				/* don't abort running jobs */
				/* start new jobs */
	execflag = E_ON;
	break;
      case K_DRAIN:
				/* don't abort running jobs */
				/* don't start new jobs */
	execflag = E_DRAIN;
	break;
      default:
	goto syntaxerr;
      }
      break;
    case K_MAXEXEC:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < 0 || i > 100 )
	goto syntaxerr;/* Dumb error msg. -IAN! */
      qp->q_maxexec = (char)i;
      if(qp->q_vmaxexec == BAT_IMAXEXEC) qp->q_vmaxexec =qp->q_maxexec;
      break;
    case K_VMAXEXEC:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < 0 || i > 100 )
	goto syntaxerr;
      qp->q_vmaxexec = (char)i;
      break;
    case K_PFACTOR:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < 1)
	goto syntaxerr;
      qp->q_pfactor = (int)i;
      break;
#ifdef ENABLE_CHECKPOINT
    case K_LOADCHECKPOINT:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < 1)
	goto syntaxerr;
      qp->q_checkpoint = (int)i;
      break;
    case K_RESTARTMODE:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < NO_CHECKPOINT || i>USER_CHECKPOINT)
	goto syntaxerr;
#ifndef HAVE_ASM_CHECKPOINT_H
      /*Kernel checkpointing not supported.*/
      if (i == KERNEL_CHECKPOINT)  goto syntaxerr;
#endif
      qp->q_restartmode = (int)i;
      break;
    case K_CHECKPOINTMODE:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < NO_CHECKPOINT || i>USER_CHECKPOINT)
	goto syntaxerr;
#ifndef HAVE_ASM_CHECKPOINT_H
      /*Kernel checkpointing not supported.*/
      if (i == KERNEL_CHECKPOINT)  goto syntaxerr;
#endif
      qp->q_checkpointmode = (int)i;
      break;
    case K_RESTARTFLAG:
      if(LEX(kwd) != K_VARIABLE)
	goto syntaxerr;
      qp->q_restartflag = strdup(yytext);
      break;
#endif
    case K_SUPERVISOR:
      if(LEX(kwd) != K_VARIABLE)
	goto syntaxerr;
      qp->q_supervisor = strdup(yytext);
      break;
    case K_MAIL:
      mailp = &qp->q_usermail;
    mailstuff:
      *mailp = 0;
      if(LEX(kwd) == K_LINE)
	break;
      if(kwd != K_VARIABLE)
	goto syntaxerr;
      if(index(yytext, 's'))
	*mailp |= MAIL_START;
      if(index(yytext, 'e'))
	*mailp |= MAIL_END;
      if(index(yytext, 'c'))
	*mailp |= MAIL_CRASH;
      break;
    case K_MAILSUPERVISOR:
      mailp = &qp->q_supmail;
      goto mailstuff;
    case K_LOADSCHED:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < 0 || i > 100 )
	goto syntaxerr;
      qp->q_loadsched = (char)i;
      break;
#ifdef JOBCTL
    case K_LOADSTOP:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < 0 || i > 100 )
	goto syntaxerr;
      qp->q_loadstop = (char)i;
      break;
#endif /*JOBCTL*/  
    case K_NICE:
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      i = atoi(yytext);
      if( i < (-40) || i > 40 )
	goto syntaxerr;
      qp->q_nice = (char) i;
      break;
#ifdef HAVE_RLIMIT
    case K_RLIMITCPU:
    case K_RLIMITFSIZE:
    case K_RLIMITDATA:
    case K_RLIMITSTACK:
    case K_RLIMITCORE:
    case K_RLIMITRSS:
    case K_RLIMITNOFILE:
    case K_RLIMITVMEM:
      /* If only one number is given, we set only the
       * current limit.  If two numbers, we set current and
       * maximum limits.
       */
      ri = rltoi(rl = Ktorl(kwd));/* index in q_rlimit array */
      if(LEX(kwd) != K_NUMBER)
	goto syntaxerr;
      limit = atoi(yytext);
      if(LEX(kwd) == K_LINE){
	if(getrlimit(rl, &(qp->q_rlimit[ri])) != 0){
	  mperror1("%s: getrlimit failed",
		   qp->q_name);
	  break;
	}
      }
      qp->q_rlimit[ri].rlim_cur = limit;
      if(kwd == K_LINE)
	break;
      if(kwd != K_NUMBER)
	goto syntaxerr;
      qp->q_rlimit[ri].rlim_max = atoi(yytext);
      break;
#endif /*HAVE_GETRLIMIT*/
    case K_MINFREE:
      if(LEX(kwd) != K_VARIABLE)
	goto syntaxerr;
      if ((qp->q_mfdev = getfs(yytext)) < 0) {
	merror2("%s: getfs(%s) failed\n",
		qp->q_profile, yytext );
	LEX(kwd);			/* eat the number */
	break;
      }
      if (LEX(kwd) != K_NUMBER || (i = atoi(yytext)) < 0) {
	releasedev(qp->q_mfdev);
	goto syntaxerr;
      }
      qp->q_minfree = i;
      break;
    case K_RESTART:
      qp->q_restart = 1;
      break;
#ifdef HAVE_GETRLIMIT
#ifndef JOBCTL
    case K_LOADSTOP:
#endif /*JOBCTL*/
    case K_RLIMITCPU:
    case K_RLIMITFSIZE:
    case K_RLIMITDATA:
    case K_RLIMITSTACK:
    case K_RLIMITCORE:
    case K_RLIMITRSS:
      merror1("'%s' is not available on this system, ignored\n",
	      yytext);
      while (LEX(kwd) != K_LINE)
	;
      break;
#endif /*HAVE_GETRLIMIT*/
    case K_PROGRAM:
      if(LEX(kwd) != K_VARIABLE)
	goto syntaxerr;
      if (qp->q_program)
	merror1("%s: profile has more than one program spec.\n", qp->q_name);
      qp->q_program = mymalloc(40+strlen(yytext) + strlen(qp->q_name)+3);
      sprintf(qp->q_program, "%s; %s %s",
	      debug ?
	      "set -x" :
	      "exec </dev/null >/dev/null 2>&1",
	      yytext, qp->q_name);
      break;
    case K_TIMESTOP:
      if(LEX(kwd) != K_VARIABLE)
	goto syntaxerr;
      if (qp->q_timestop)
	merror1("%s: profile has more than one timestop spec.\n", qp->q_name);
      qp->q_timestop = strdup(yytext);
      break;
    case K_TIMESCHED:
      if(LEX(kwd) != K_VARIABLE)
	goto syntaxerr;
      if (qp->q_timesched)
	merror1("%s: profile has more than one timesched spec.\n", qp->q_name);
      qp->q_timesched = strdup(yytext);
      break;
    default:
      goto syntaxerr;
    }
    if(kwd != K_LINE)
      if(yylex() != K_LINE)
	goto syntaxerr;
  }
  (void) fclose(pro);

  switch (execflag) {
  case E_ON:
    startmsg = "enabled";	/* exec on: just keep going */
    break;
  case E_OFF:
    abortall(qp);	/* exec off: kill all jobs and drain queue_b */
    startmsg = "aborted";
    break;
  case E_DRAIN:
    drainqueue_b(qp);	/* exec drain: stop further jobs starting */
    startmsg = "draining";
    break;
  case E_MIGRATE:
    migratequeue_b(qp);
    startmsg = "migrating";
    break;
  default:
    startmsg = "unknown";
    merror2("%s: internal queued error for exec flag %d\n",
	    qp->q_name, execflag);
    drainqueue_b(qp);	/* stop further jobs from starting */
    return;
  }

  if( qp->q_supervisor == NULL ){
    merror1("%s: No queue_b supervisor userid or file specified\n",
	    qp->q_profile );
    drainqueue_b(qp);	/* stop further jobs from starting */
    return;
  }
  if( qp->q_loadsched<0 || qp->q_loadstop<0 || qp->q_maxexec<0 
      || qp->q_checkpoint <0 ){
    merror5("%s: loadsched %d or loadstop %d or checkpoint %d, or maxexec %d is < 0\n",
	    qp->q_profile, qp->q_loadsched, qp->q_loadstop, qp->q_checkpoint,
	    qp->q_maxexec );
    drainqueue_b(qp);	/* stop further jobs from starting */
    return;
  }

  sprintf(errstr, "%s: %s: maxexec=%d loadsched=%d\n",
	  qp->q_name, startmsg, qp->q_maxexec, qp->q_loadsched );
#ifdef JOBCTL
  sprintf( index(errstr, '\n'), " loadstop=%d\n", qp->q_loadstop);
#endif
#ifdef ENABLE_CHECKPOINT
  sprintf( index(errstr, '\n'), " checkpoint=%d\n", qp->q_checkpoint);
#endif
  sprintf( index(errstr, '\n'), " nice=%d\n", qp->q_nice);
  if( qp->q_minfree != 0 )
    sprintf( index(errstr, '\n'), " minfree=%ld\n", qp->q_minfree);
  if (qp->q_timestop)
    sprintf(index(errstr, '\n'), " timestop=%s\n", qp->q_timestop);
  if (qp->q_timesched)
    sprintf(index(errstr, '\n'), " timesched=%s\n", qp->q_timesched);
  if (qp->q_program)
    sprintf(index(errstr, '\n'), " program='%s'\n", qp->q_program);
#ifdef HAVE_GETRLIMIT
  {
    struct rlimit *rlp = &(qp->q_rlimit[rltoi(RLIMIT_CPU)]);
    if( rlp->rlim_cur >= 0 && rlp->rlim_max >= 0 )
      sprintf( index(errstr, '\n'), " cpu %d min\n",
	       rlp->rlim_cur/60 );
    else
      sprintf( index(errstr, '\n'), " cpu inf\n");
  }
#else
  sprintf( index(errstr, '\n'), " cpu inf\n");
#endif
  muser(qp->q_supervisor, errstr);
  qp->q_status1 = strdup(errstr);
  return;

 syntaxerr:
  merror3("%s: Syntax error in profile, line %d near '%s'\n",
	  qp->q_profile, yylineno, yytext);
  drainqueue_b(qp);	/* stop further jobs from starting */
}

/*
 * Open, or create and open, the directory containing cf* queue_bd
 * files.  Return NULL on failure.  */
DIR *
opencfdir(register struct queue_b *qp)
{
  register DIR *cfdir;

  while( (cfdir = opendir(qp->q_cfdir)) == NULL) {
    int saveumask;

    if( errno != ENOENT ){
      mperror1("Cannot opendir(%s); draining queue_b",
	       qp->q_cfdir);
      drainqueue_b(qp);	/* stop further jobs from starting */
      return NULL;
    }
    saveumask = umask(022);
    if( mkdir(qp->q_cfdir, 0775) < 0 ){
      mperror1("Cannot mkdir(%s); draining queue_b",
	       qp->q_cfdir);
      drainqueue_b(qp);	/* stop further jobs from starting */
      (void) umask(saveumask);
      return NULL;
    }
    (void) umask(saveumask);
    muser1(qp->q_supervisor, "%s: directory created\n",
	   qp->q_cfdir);
    /* mkdir worked; loop back to try opendir() again */
  }
  return cfdir;
}

/*
 * (Re-)Read the q_cfdir directory for a queue_b and find all jobs
 * ready to run.
 *
 * First, turn off all the "seen" bits for known jobs in this queue_b.
 * Then, read the queue_b directory and for each directory entry that
 * starts with "cf" see if we recognize its name.  If we recognize its
 * name, turn on the "seen" bit for the job.  If unrecognized, record
 * the job and turn on the "seen" bit.  After all this, re-scan our
 * list of known jobs in this queue_b; if any jobs are not marked
 * "seen" then they have been deleted and we must abort them.
 *
 * On start-up, once, check for of* files that indicate jobs were
 * killed in mid-execution.  Restart those that ask for it.  */
void	
requeue_b(register struct queue_b *qp)
{
  register DIR *cfdir;
  struct_dir *d;
  register struct job *jp;
  char fname[DIRSIZ_Q*3+3];
  int trouble;

  qp->q_nochange = 0;	/* things have changed */

  qp->q_requeue_b=0;        /*WGK: Do not force another requeue_b.*/

  if( (cfdir = opencfdir(qp)) == NULL)
    return;

  FOREACH (jp, qp->q_jobs) {
    jp->j_seen = 0;
  }
  while((d = readdir(cfdir)) != NULL) {
    /*
     *  Ignore everything starting with a dot.
     */
    if( d->d_name[0] == '.' )
      continue;

    /*
     * Ignore anything that doesn't start with "cf".
     * Warn about junk files in this directory.
     * Don't warn about tf* temp files.
     */
    if( d->d_name[0] != 'c' || d->d_name[1] != 'f' ){
      if( d->d_name[1] != 'f' )
	merror2("Junk file name '%s/%s' ignored\n",
		qp->q_cfdir, d->d_name);
      continue;
    }

    FOREACH (jp, qp->q_jobs)
      if(strcmp(d->d_name, jp->j_cfname) == 0)
	break;
    if (jp != qp->q_jobs) {
      jp->j_seen = 1;	/* already know about job */
      continue;
    }
    /*
     * New job; add it.
     */
    jp = (struct job *)
      enqueue_b((struct qelem_queue *)qp->q_jobs, (int)sizeof *jp);
    if( jobinfo(jp, qp, d->d_name) == NULL ){
      freejob(jp);
      continue;
    }
    mdebug5("\tnew job '%s/%s' (%s) user '%s' (%s)\n",
	    qp->q_cfdir, jp->j_cfname, jp->j_jobname,
	    jp->j_userid, jp->j_mailuserid);
  }
  closedir(cfdir);
  /*
   * Look at our internal table and check that every job we
   * know about is still in the directory.  If one has been
   * deleted and is currently running, abort it; otherwise
   * just remove the job from the table.
   */
  trouble = 0;
  FOREACH (jp, qp->q_jobs)
    if (jp->j_seen == 0) {
      mdebug1("\tdropping vanished job %s\n", jp->j_cfname);
      if(jp->j_pid>0) {
	if (!abortjob(jp)) {
	  trouble = 1;
	  merror1("Trouble aborting vanished job %s\n", jp->j_cfname);
	  muser1(qp->q_supervisor, "Trouble aborting vanished job %s\n", 
		 jp->j_cfname);
	  /*
	   * The Apollo loses track of jobs/processes sometimes,
	   * possibly due to forking problems, and
	   * the cleanup routine 'terminated' fails.
	   * To recover from this, just delete the input
	   * and output files manually, and this code will
	   * get batchd back into sync.
	   * Could try
	   *	terminated(jp->j_pid, SIGKILL<<8, 0.0);
	   * instead of the 'freejob' immediately below here,
	   * but what if 'terminated' fails again (it already
	   * failed once or we wouldn't be here). There is
	   * also the problem that the job might not be in
	   * the list of "running" jobs any more.
	   * If 'terminated' could be used reliably, the
	   * 'trouble' loop below this loop could be eliminated.
	   */
	  freejob(jp);
	}
      } else
	freejob(jp);
      break;
    }
  /*
   * If there was a vanished job that was supposedly still
   * running but could not be aborted, the job count for
   * this queue_b is probably wrong, so figure it out again.
   * Also try to correct the totalrunning counter.
   */
  if (trouble) {
    totalrunning -= qp->q_nexec;
    qp->q_nexec = 0;
    FOREACH (jp, qp->q_jobs)
      if(jp->j_pid>0)
	qp->q_nexec++;
    totalrunning += qp->q_nexec;
  }
  /*
   * Check for restarted jobs.  This is done exactly once per new queue_b,
   * usually when BATCHD gets started after a reboot and makes a list
   * of its queue_bs.  Nothing turns q_startup off again, so this gets
   * done only once, even if a queue_b is aborted and re-enabled.
   * You have to actually delete the queue_b to reset the flag.
   * We also create a missing q_cfdir subdirectory at this point.
   */
	
  if(qp->q_startup == 0) {
    register DIR *qname;

    qp->q_startup = 1;
    queue_bstat(qp, "Daemon restarting\n");

    if((qname = opendir(qp->q_name)) == NULL) {
      mperror1("Can't opendir(%s); draining queue_b",
	       qp->q_name);
    }
    while((d = readdir(qname)) != NULL) {
      /*
       *  Ignore everything starting with a dot.
       */
      if( d->d_name[0] == '.' )
	continue;
      if( d->d_name[0] != 'o' || d->d_name[1] != 'f' ){
	/*
	 * Delete all old process group files.
	 * Zero old queue_b status files.
	 */
	if((d->d_name[0]=='e' && d->d_name[1]=='f')){
	  sprintf(fname, "%s/%s",
		  qp->q_name, d->d_name);
	  (void)unlink(fname);
	}
	continue;
      }
      /*
       * Must be an of* output file.
       * Do we know about its corresponding cf* file?
       */
      FOREACH (jp, qp->q_jobs)
	if (strcmp(d->d_name+1, jp->j_cfname+1) == 0)
	  break;
      if (jp == qp->q_jobs) {
	/*
	 * cf* file not found: we have a left over
	 * of* output file with no control file.
	 * Complain and get rid of it.
	 */
	sprintf(fname, "%s/%s", qp->q_name, d->d_name);
	(void)unlink(fname);
	muser1(qp->q_supervisor,
	       "%s: Old file deleted\n", fname);
	continue;
      }

      /*WGK96/6/14 Only restart if not in interactive
	mode and restart allowed in this queue_b.
	If we are in interactive mode, there is
	no way to restart since we have lost
	the client-side stub.*/


      if((qp->q_restart)&&(jp->j_mailflag)){

	/*
	 * Old of* output file for a cf* file found; restart.
	 * mailback() deletes the of* and ef* files.
	 */
	
	mailback(0, jp, MAIL_CRASH,
		 qp->q_restart?"restarted":"not restarted");
	continue;
      }
      /*
       *  Restart disallowed, delete the cf* file.
       */
      sprintf(fname, "%s/%s", qp->q_cfdir, jp->j_cfname);

      /* WGK 95/3/7: OK, OK, upon restart we may
	 take the risk of unlinking any files we cannot write lock.*/
      {
	struct flock mylock;
	int filedes;
			  
	if((filedes= open(fname, O_WRONLY))<0) {
	  mperror1("Restart: Problem opening file %s for lock.", fname);
	}
	mylock.l_type = F_WRLCK;
	mylock.l_whence = SEEK_SET;
	mylock.l_start = 0;
	mylock.l_len = 0;

	errno = 0;
	if(fcntl(filedes, F_SETLK, &mylock)<0 && errno != ENOLCK) {
	  if(errno == EACCES) {
	    mdebug1("startup: %s appears to be locked by another process; Not unlinking\n", fname);
	  }
	  else mperror1("Problem locking file %s", fname);
	  close(filedes);
	}
	else {
	  close(filedes);
	  if( unlink(fname) == -1) {
	    saverrno = errno;
	    mperror1("Can't unlink(%s)", fname);
	    muser2(qp->q_supervisor,
		   "%s: Can't unlink dead job%s",
		   qp->q_name,
		   saverrno==ENOENT? "": "; stopping queue_b");
	    if (saverrno != ENOENT)
				/* stop further jobs from starting */
	      drainqueue_b(qp);
	  }
	  freejob(jp);
	}
      }
    }
    closedir(qname);
  }
}

/*
 * Get information on this job and return it in jp.
 * Return NULL if anything goes wrong.
 */
struct job *
jobinfo(struct job *jp,     /* where to put job info */                
	struct queue_b *qp, /* queue_b name on which to queue_b job */ 
	char *cfname)       /* cf* file name of queue_bd job */        
{
  register int i;
  char filename[DIRSIZ_Q*3+3];
  register FILE *f;
  register char *p;
  struct stat sbuf;

  /*WGK made major changes to jobinfo.*/

  if( strlen(cfname) >= DIRSIZ_Q ){
    merror3("%s: '%s': file name longer than %d characters\n",
	    qp->q_cfdir, cfname, DIRSIZ_Q-1);
    return NULL;
  }


  sprintf(filename, "%s/%s", qp->q_cfdir, cfname);

  if ((f = fopen(filename, "r")) == NULL){
    mperror1("Can't fopen(%s, \"r\")", filename);
    return NULL;
  }

  if( fstat(fileno(f), &sbuf) < 0 ){
    mperror1("Can't fstat(%s)", filename);
    return NULL;
  }

  jp->j_seen     = 1;		     /* we've seen this job */
  jp->j_queue_b    = qp;	     /* back pointer to queue_b */
  /*2000/7/19 WGK Draft protocol changes. First thing in job control file is version
    controll string, which is null terminated.*/
  /*This had better be VERSION1 or something is very,very wrong.
    Perhaps someone will add code to deal with VERSION0 or other
     version strings if and when they come into use.*/

  {char *version = allocline(f);
  free(version);
  }

  netfread(&(jp->j_localuid), sizeof(int), 1, f); /* local uid running job */
  jp->j_qtime    = sbuf.st_mtime;    /* time of queue_bing */
  (void) strcpy(jp->j_cfname, cfname);

  jp->j_userid=allocline(f);
  jp->j_mailuserid=allocline(f);
  jp->j_jobname=allocline(f);
  {char *tmp = allocline(f);
  if(tmp && *tmp && *tmp != ' ') {
    jp->j_onlyhost=strdup(canonicalhost(tmp));
  }
  free(tmp);
  }
  netfread(&(jp->j_mailflag), sizeof(int), 1, f); /* local uid running job */

  if( ferror(f) ){
    mperror1("Error reading '%s'", filename);
    return NULL;
  }
  (void) fclose(f);

  if( jp->j_localuid != 0 || jp->j_userid == NULL ){
    register struct passwd *pw;

    if((pw = getpwuid(jp->j_localuid)) == NULL) {
      merror2("%s: %d: no such local uid\n",
	      filename, jp->j_localuid);
      return NULL;
    }
    /*WGK 98/12/19 Forgery is only possible if something is wrong
      with spool directory permissions, which queued checks for.
      So, this is a vestige, and is not really needed.*/
    if( jp->j_localuid != 0 && jp->j_userid != NULL
	&& strcmp(jp->j_userid, pw->pw_name) != 0 ){
      merror3("%s: '%s' attempted to forge userid '%s'\n",
	      filename, pw->pw_name, jp->j_userid);
      return NULL;
    }
    jp->j_userid = strdup(pw->pw_name);
  }

  if( jp->j_mailuserid == NULL )
    jp->j_mailuserid = strdup(jp->j_userid);

  if( jp->j_jobname == NULL )
    jp->j_jobname = strdup(jp->j_cfname);

  /*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
#ifdef QUEUE_MANAGER
  uid = jp->j_localuid;
  strcpy(username, jp->j_userid);
#endif /*QUEUE_MANAGER*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

  /*Enforced host control. There are two modes of host control. With
    onlyhost a space, the preferred host is sent over through the
    query port control. If the preferred host is down, another host
    will eventually see the job and run it, a more robust solution. If
    onlyhost is set, only that host can actually run the job. This is
    needed for shell scripts and MPI support.*/

  if(jp->j_onlyhost && (*jp->j_onlyhost != ' ') 
     && (strcasecmp(jp->j_onlyhost, myhostname())))
    jp->j_pid = ANOTHER_HOST;

  mdebug6("jobinfo: localuid: %d userid: %s mailuserid: %s jobname: %s mailflag: %d onlyhost: %s\n", 
	  jp->j_localuid, jp->j_userid, jp->j_mailuserid, jp->j_jobname, 
	  jp->j_mailflag, jp->j_onlyhost?jp->j_onlyhost:"");
  return jp;
}


/*
 * A queue_b that might have changed.
 * Wander along it and start jobs if possible.
 * If we couldn't fork we just return; we'll try again later.
 * The q_queue_bstat file contains the queue_b status.
 */
void	
runqueue_b(register struct queue_b *qp, int *load)
{
  register struct job *jp;
  register struct job *bestjp;
  long fsf;
  int start, drain = 0, migrate = 0, checkpoint = 0, restart = 0, printstat;
  char msg[256];

  /*
   * The variable "start" is non-zero iff all conditions
   * are go for running jobs in this queue_b.
   * The conditions are: load average low enough,
   * current time within the queue_b's time spec(s),
   * queue_b program says the queue_b is enabled.
   * The last two conditions only apply if the queue_b
   * has an associated time spec. and program.
   *
   *
   * We check the conditions in increasing order of cost,
   * to avoid the expensive tests.  (It would be nice to avoid
   * these expensive tests if there are no jobs in the queue_b.)
   * As we go, we append test results to the string "msg".
   */

  checkpoint = qp->q_checkpointmode;
  migrate = load[0] >= qp->q_checkpoint;
  start = load[0] < qp->q_loadstop;
  sprintf(msg, "1 min load average %d %s %d, ",
	  load[0], start ? "<" : ">=", qp->q_loadstop);
  if (start && (qp->q_timestop || qp->q_timesched)) {
    /* Current time must be within the queue_b's timestop spec. */
    static struct tm now;
    if (needtime) {		/* read the clock */
      time_t t;
      time(&t);
      now = *localtime(&t);
      needtime = 0;
    }
    if (qp->q_timestop) {
      start = checktime(qp->q_timestop, &now);
      mdebug3("%s: timestop '%s' says %s\n",
	      qp->q_name, qp->q_timestop,
	      start ? "start" : "stop");
      sprintf(msg+strlen(msg), "timestop '%s' is %sactive, ",
	      qp->q_timestop, start ? "" : "in");
    }
    if (start && qp->q_timesched) {
      drain = !checktime(qp->q_timesched, &now);
      mdebug3("%s: timesched '%s' says %s\n",
	      qp->q_name, qp->q_timesched,
	      drain ? "drain" : "start");
      sprintf(msg+strlen(msg), "timesched '%s' says %s, ",
	      qp->q_timesched, drain ? "drain" : "start");
    }
  }
  if (start && qp->q_program) {
    /* Queue's program must agree */
    int status;
    mdebug2("%s: running '%s'\n", qp->q_name, qp->q_program);
    status = system(qp->q_program);
    mdebug3("%s: status is %d/%d\n",
	    qp->q_name, status&0xff, (status>>8)&0xff);

    /*
     * Only print enabled/disabled/drain status if the
     * queue_b status has changed, or every 50 passes.
     */
    qp->q_statcntr++;
    if (qp->q_oldstat == status && qp->q_statcntr / 50 == 0)
      printstat = 0;
    else {
      printstat = 1;
      qp->q_statcntr = 0;
    }
    qp->q_oldstat = status;		/* Save current status */
    switch ((status&0xff) ? -1 : ((status>>8)&0xff)) {
    case 0:	/* start */
      mdebug1("starting %s\n", qp->q_name);
      if (printstat)
	muser1(qp->q_supervisor, "%s: Queue is enabled\n",
	       qp->q_name);
      start = 1;
      sprintf(msg+strlen(msg), "Program '%s' says start, ",
	      qp->q_program);
      break;
    case 1: /* stop */
      mdebug1("stopping %s\n", qp->q_name);
      if (printstat)
	muser1(qp->q_supervisor, "%s: Queue is disabled\n",
	       qp->q_name);
      start = 0;
      sprintf(msg+strlen(msg), "Program '%s' says stop, ",
	      qp->q_program);
      break;
    case 2:	/* don't schedule new jobs */
      mdebug1("draining %s\n", qp->q_name);
      if (printstat)
	muser2(qp->q_supervisor, "%s: Queue is %s\n",
	       qp->q_name, 
	       qp->q_nexec ? "draining" : "drained");
      sprintf(msg+strlen(msg), "Program '%s' says drain, ",
	      qp->q_program);
      drain++;
      break;
    case 4:	/* don't schedule new jobs and migrating */
      mdebug1("migrating %s\n", qp->q_name);
      if (printstat)
	muser2(qp->q_supervisor, "%s: Queue is %s\n",
	       qp->q_name, 
	       qp->q_nexec ? "migrating" : "migrated");
      sprintf(msg+strlen(msg), "Program '%s' says migrate, ",
	      qp->q_program);
      migrate++;
      drain++;
      break;
    default:
      /*
       * Bogus exit status.
       * Stop everything, drain the queue_b, notify supervisor,
       * and ignore the program.
       */
      merror3("Queue %s: program '%s' returned weird status %d; draining queue_b\n",
	      qp->q_name, qp->q_program, status);
      drainqueue_b(qp);
      free(qp->q_program);
      qp->q_program = NULL;
      start = 0;
      break;
    }
  }
  if (strcmp(msg+strlen(msg)-2, ", ") == 0)
    msg[strlen(msg)-2] = 0;		/* clean up msg tail */

  if (qp->q_stopped && start) {		/* Restart stopped queue_b */
    qp->q_stopped = 0;
    queue_bstat1(qp, "Restarted; %s\n", msg);
    if( qp->q_nexec != 0 ){
      muser3(qp->q_supervisor,
	     "%s: Restarted; jobs=%d %s\n",
	     qp->q_name, qp->q_nexec, msg);
#ifdef JOBCTL
      (void) sigalljobs(qp, SIGCONT);
#endif
    }
  }

  if (!qp->q_stopped && !start) {		/* Stop the queue_b */
    qp->q_stopped = 1;
    queue_bstat1(qp, "Stopped; %s\n", msg);
    if( qp->q_nexec != 0 ){
      muser3(qp->q_supervisor, "%s: Stopped; jobs=%d %s\n",
	     qp->q_name, qp->q_nexec, msg);
#ifdef JOBCTL
      /*95/7/10 WGK: send SIGUSR1 to queue_bd to tell it stop everything.*/
      (void) sigalljobs(qp, SIGUSR1);
#endif
    }
  }

#ifdef ENABLE_CHECKPOINT
  if (qp->q_checkpointmode && migrate) { 

    int fd;

    /*WGK 1999/3/7 Load average is high enough to migrate. Here's the
      plan: for now, we'll always migrate, since this is a test
      version & we'd like to be able to migrate to the same host
      because it's convenient. But there's a call we can make:

      fd = wakeup(qp->q_name, Q_MIGRATING, Q_NOCONTACT,NULL) which will
      return minus one if there are no available hosts accepting
      incoming migrators for this Queue.

      In this case, we would then not execute the following code, and
      wait for loadstop to kick in and suspend the processes in the
      Queue. We need to make sure, however, that we restart the
      processes once a migrator does indeed become available.*/

    qp->q_migrate = 1;
    queue_bstat1(qp, "Migrating; %s\n", msg);
    if( qp->q_nexec != 0 ){
      muser3(qp->q_supervisor, "%s: Migrating; jobs=%d %s\n",
	     qp->q_name, qp->q_nexec, msg);
      /*1999/3/06 WGK: send SIGUSR2 to queue_bd to tell it to migrate
        everything.*/
      (void) sigalljobs(qp, SIGUSR2);
	
    }
  }
#endif /*ENABLE_CHECKPOINT*/

  /*WGK 2000/07/30 we need to think about sending jobs to other machines here. The
rule is this:

- If the queue is stopped or draining (but not migrating), we send jobs off.
- If we are running the maximum number of jobs, we send jobs off.

  */

#define TRANSMITJOBS 1
#ifdef TRANSMITJOBS 

  if (qp->q_stopped||!start||drain||(qp->q_nexec >= qp->q_maxexec)) {

    /*
We can't run any more jobs in this queue, but try to see if there's someone else out there who might
take this job off our hands. 2000/07/30.*/

    int flag = 0;
    while(flag!=-1) {
      bestjp = NULL;
      FOREACH (jp, qp->q_jobs){
	if (jp->j_pid == 0 && (bestjp == NULL ||
			       strcmp(bestjp->j_cfname+BAT_PREFIX_LEN,
				      jp->j_cfname+BAT_PREFIX_LEN) > 0))
	  bestjp = jp;
      }
      /*Non-null bestjp indicates there's a job waiting to run that can't be run, so send it off.
	However, a -1 from transmitjob indicates there aren't any hosts out there that
        are accepting jobs in this queue, so abort this procedure if we see that.*/
      if (bestjp) flag = transmitjob(bestjp); /*Job can't run, send it off*/
      else flag = -1; /*No more jobs.*/
    }
  }
#endif /*TRANSMITJOBS*/

  /* We assume that if the queue_b is stopped we don't want to worry
   * about starting any more jobs.  Unix doesn't let you exec a job
   * and have it in "stopped" mode anyway.  */
  if( qp->q_stopped ){
    mdebug1("\tqueue_b is (still) stopped; jobs = %d\n", qp->q_nexec);
    /* Don't set the q_nochange flag; we want to check the load every
     * time around.  */
    return;
  }

  qp->q_status = 1 ;
  /* If we can't start more jobs, or the "program" for this queue_b
   * said to drain the queue_b, don't go on checking.  */
  if (!start || drain) {
    qp->q_status = 0;
    mdebug1("Can't start new jobs: %s\n", msg);
    queue_bstat1(qp, "Can't start new jobs: %s\n", msg);
    return;
  }

  /* Queue is not stopped.  
   * If the q_nochange flag is set, nothing has
   * happened that would allow us to try to start more jobs, i.e. no
   * job has finished, the profile hasn't changed, etc., so we don't
   * need to do anything.  */
  if(qp->q_nochange){
    mdebug("\tqueue_b status has not changed since last time\n");
    return;
  }

  /*
   * If profile file said "exec off" or
   * "exec drain", don't start new jobs.
   */
  if(qp->q_drain == Q_DRAINING){
    mdebug2("\tqueue_b is %s; jobs = %d\n",
	    qp->q_nexec ? "draining" : "drained", qp->q_nexec);
    queue_bstat( qp, qp->q_nexec ? "Draining" : "Drained" );
    /* Status of this flag won't change until
     * a new profile is read.
     */
    qp->q_nochange = 1;
    return;
  }
  if(qp->q_migrate == Q_MIGRATING){
    mdebug2("\tqueue_b is %s; jobs = %d\n",
	    qp->q_nexec ? "migrating" : "migrated", qp->q_nexec);
    queue_bstat( qp, qp->q_nexec ? "Migrating" : "Migrated" );
    /* Status of this flag won't change until
     * a new profile is read.
     */
    qp->q_nochange = 1;
    return;
  }
  if(load[1] >= qp->q_loadsched){
    queue_bstat2(qp, "5 minute load %d >= scheduling limit %d\n",
		 load[1], qp->q_loadsched);
    if( qp->q_nexec != 0 ){
      muser4(qp->q_supervisor,
	     "%s: Paused; jobs=%d  5 min load %d >= %d\n",
	     qp->q_name, qp->q_nexec,
	     load[1], qp->q_loadsched );
    }
    mdebug2("\tqueue_b 5 minute load %d >= scheduling limit %d\n",
	    load[1], qp->q_loadsched);
    /* Don't set the q_nochange flag; we want to check the
     * load every time around.
     */
    return;
  }
  if( qp->q_minfree != 0 && (fsf=fsfree(qp->q_mfdev)) < qp->q_minfree){
    queue_bstat3(qp, "Free space on file system #0%o = %ld < %ld\n",
		 qp->q_mfdev, fsf, qp->q_minfree );
    if( qp->q_nexec != 0 ){
      muser5(qp->q_supervisor,
	     "%s: Paused; jobs=%d space left on file system #0%o = %ld < %ld\n",
	     qp->q_name, qp->q_nexec,
	     qp->q_mfdev, fsf, qp->q_minfree );
    }
    mdebug3("\tqueue_b space left on file system #0%o = %ld < %ld\n",
	    qp->q_mfdev, fsf, qp->q_minfree );
    /* Don't set the q_nochange flag; we want to check the
     * file system space every time around.
     */
    return;
  }

  /*
   * We must be in a position to consider starting new jobs.
   * The name of the job determines its priority.
   * The first letter after "cf" is the overall priority,
   * after which comes the time the job was queue_bd.
   * Find the oldest non-running job in the highest priority queue_b.
   */
  while (qp->q_nexec < qp->q_maxexec) {
    bestjp = NULL;
    FOREACH (jp, qp->q_jobs){
      if (jp->j_pid == 0 && (bestjp == NULL ||
			     strcmp(bestjp->j_cfname+BAT_PREFIX_LEN,
				    jp->j_cfname+BAT_PREFIX_LEN) > 0))
	bestjp = jp;
    }
    if (bestjp == NULL)
      break;		/* no more jobs waiting to start */
#define FORK_FAILED		1
#define UNKNOWN_LOCALUID	2
#define LOCK_FAILED		3
#define ALREADY_LOCKED          4
#define APPARANTLY_LOCKED       5

    switch(startjob(bestjp) ){
    case APPARANTLY_LOCKED:
    case ALREADY_LOCKED:
      /* Probably running on another
	 host; forget about it and mark it
	 as such.*/
      break;
    case LOCK_FAILED:
    case FORK_FAILED:
      return;		/* give up for now; try later */
    case UNKNOWN_LOCALUID:
      break;		/* skip this one */
    case 0:
      break;		/* successful start */
    }
  }
  /*
   * Queue has max jobs running, or no job is waiting to run.
   * Don't try to do more that starting/stopping currently running
   * jobs next time around until some event (such as job ending, new
   * job, new profile, etc.) turns off the q_nochange flag and allows
   * us to consider starting new jobs.
   * (Above doesn't hold for programmed or timed queue_bs.)
   */
  if (qp->q_program == NULL && qp->q_timestop == NULL && qp->q_timesched == NULL)
    qp->q_nochange = 1;	/* set before writing queue_bstat */
  if( qp->q_nexec == 0 ){
    queue_bstat(qp, "No jobs in queue\n");
    mdebug("\tqueue has no jobs\n");
  }
  else if( qp->q_nexec < qp->q_maxexec ){
    queue_bstat(qp, "All queue_bd jobs started\n");
    mdebug1("\tqueue has all queue_bd jobs started; jobs=%d\n",
	    qp->q_nexec );
  }
  else{
    queue_bstat(qp, "Running max number of jobs\n");
    mdebug1("\tqueue is running max number of jobs = %d\n",
	    qp->q_nexec );
  }
}

/*
 * Write the string into the queue_b status file.
 */
void	
queue_bstat(struct queue_b *qp, char *str)
{
  FILE *fp;

  if ((fp = fopen(qp->q_queue_bstat, "w")) == NULL) {
    mperror1("Can't open(%s) queue status file for writing",
	     qp->q_queue_bstat);
    return;
  }
  if (qp->q_status1)
    fputs(qp->q_status1, fp);
  fprintf(fp, "%s: drain=%d migrate=%d, deleteq=%d stopped=%d jobs=%d: %s\n",
	  qp->q_name, qp->q_drain, qp->q_migrate, qp->q_deleteq, qp->q_stopped,
	  qp->q_nexec, str);
  if (fclose(fp) != 0)
    mperror1("%s: fclose failed", qp->q_queue_bstat);
}

void
sig_ignore(void) {}

/*
 * Given a pointer to a job that is not yet running,
 * start it executing.
 * Return 0 on success; non-zero on failure to start the job.
 * If the forked child has a disaster, it has no way to communicate
 * that back to batchd itself...
 */
int
startjob(register struct job *jp)
{
  register struct queue_b *qp;
  register int i, pid;
  register struct running *rp;
  register struct passwd *pw;
  char filename[MAXPATHLEN];
  char fname[MAXPATHLEN];
  int newpgrp;
  FILE *pgrpfile;
  struct flock mylock;
  int restart;
  int checkpoint;
  char mfile[DIRSIZ_Q*3+3];


  qp = jp->j_queue_b;	/* queue_b in which job resides */
  sprintf(filename, "%s/%s", qp->q_cfdir, jp->j_cfname);


  checkpoint = qp->q_checkpointmode;
  restart = NO_RESTART;

#ifdef ENABLE_CHECKPOINT
  /*Migrator code. WGK 1999/3/6. If there's a corresponding mf file,
    only consider starting the job if we are allowed to restart jobs.*/

  sprintf(mfile, "%s/m%s", qp->q_name, jp->j_cfname+1);
  if(access(mfile, F_OK)==0) {
    if(qp->q_restartmode == NO_RESTART) {
      mdebug1("\t%s can't be checkpoint restarted on this host\n", jp->j_cfname);
      jp->j_pid = ANOTHER_HOST;
      return(APPARANTLY_LOCKED);
    }
    else {
      restart=RESTART; /*Mark this job as a restart*/
      mdebug1("\t%s is an incoming migrator candidate.\n", jp->j_cfname);
    }
	  
  }
  else {
    sprintf(mfile, "%s/x%s", qp->q_name, jp->j_cfname+1);
  }

#endif /*ENABLE_CHECKPOINT*/

  mdebug1("\tstarting job '%s'\n", filename);

  if((pw = getpwuid(jp->j_localuid)) == NULL){
    merror3("%s: Unknown local uid %d (mail=%s)\n", filename,
	    jp->j_localuid, jp->j_mailuserid );
    return UNKNOWN_LOCALUID;	/* unsuccessful start */
  }

  /*
   * Because of the way signals are implemented in Sun Unix
   *  (a table of function pointers is kept in user space)
   * we better use fork() instead of vfork(); if you use vfork,
   * subsequent signal() calls (before an exec) affect the signal tables
   * of BOTH the parent and the child, which we don't want, and
   * can cause odd failures of the batch daemon when it tries
   * to jump in strange ways.  Even under 4.3bsd on Vaxen, the
   * resource usage is not zeroed in a vfork() child, so be
   * careful to ignore SIGXCPU until after the exec().
   * 89/01/06 Even the ignore and handler doesn't work, so don't
   * bother trying to use vfork() at all until resource usage is
   * zeroed properly by the kernel.  -IAN!
   */

  if (qp->q_checkpointmode == KERNEL_CHECKPOINT)
    {mdebug4("\tPlanning to call handle with arguments %s, %d, %d, %s\n", filename, checkpoint, restart, mfile);}
  else{
    mdebug4("\tPlanning to call handle with arguments %s, %d, %d, %s\n", filename, checkpoint, restart, qp->q_restartflag);}

  pid = fork();
  if( pid == -1 ){
    mperror("queued forking");
    return FORK_FAILED;	/* unsuccessful start */
  }
  if(pid) {
    /* START OF PARENT */
    register int sameuser = 0;
    register long delayed;

    mdebug2("\tforked off job '%s', pid %d\n", filename, pid);
    nkids++;
    qp->q_nexec++;
    totalrunning++;	/* total number of running jobs */

    /*2000/10/22 Code added by Monica Lau <mllau@alantro.com>*/
#ifdef QUEUE_MANAGER
    forkedqpid = pid;
#endif /*QUEUE_MANAGER*/
/*2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

    jp->j_pid = pid;
    rp = (struct running *)
      enqueue_b((struct qelem_queue *)running, (int)sizeof *rp);
    rp->r_pid = pid;
    rp->r_job = jp;
#ifdef __hpux
    /*
     * Add the job to the list of pids we are watching.  GRS
     */
#ifdef HAVE_UWATERLOO
    AddHPWatch(pid, qp->q_rlimit);
#endif
#endif /*__hpux*/
    if(qp->q_usermail & MAIL_START)
      if(debug) muser1(jp->j_mailuserid,
		       "%s: Job is starting now.\n", filename);
    if(qp->q_supmail & MAIL_START)
      sameuser = (strcmp(pw->pw_name, jp->j_mailuserid) == 0);
    delayed = time((time_t *)0) - jp->j_qtime;
    delayed = (delayed+30) / 60;	/* convert seconds to minutes */
    muser7(qp->q_supervisor,
	   "%s: %s: START (delayed %ld min): %s%s%s%s\n",
	   qp->q_name, jp->j_cfname, (long)delayed, pw->pw_name,
	   sameuser ? "" : " (",
	   sameuser ? "" : jp->j_mailuserid,
	   sameuser ? "" : ")" );
    return 0;	/* successful start of job */
    /* END OF PARENT */
  }
  /*
   * Child. Setuid to the owner, go to the working directory, set up
   * i/o redirection stuff to mail back output, etc.  If someone
   * deleted the queue_b, a lot of this setup won't work, but we don't
   * want to kill off batchd because of it.  If anything fails after
   * the fork(), and we don't kill batchd, the job will be
   * automatically deleted.  So be careful about letting a messed-up
   * batchd run and purge all its queue_bs.  */

  /* Write the pgrp to 'ef' filename so the batch cancelling program
   * knows what process group to killpg().  If write fails, just
   * assume the queue_b has been deleted.  */

  /*Previously this wrote out the pgrp of the process. We don't know
    this, but do know pgrp of queue_bd, which can be sent SIGTERM to
    kill process.  But, this is not very useful. We prefer to simply
    delete the file and send wakeup to host so that it can kill the
    job. So, forget about pgrp. So it is now a host name file. 95/7/12
    WGK.*/

  /*This file now just tells a top/yamm like process which host the
    program is running on. WGK*/


  sprintf(fname, "%s/e%s", qp->q_name, jp->j_cfname+1);
  if((pgrpfile = fopen(fname, "w")) == NULL) {
    mperror1("fopen(%s, \"w\") failed'\n", fname);
    exit(1);
  }
  fprintf(pgrpfile, "%s\n", myhostname());
  if( fclose(pgrpfile) == EOF ){
    mperror1("fclose on '%s' failed'\n", fname);
    exit(1);
  }

  /*WGK 1.099-p2 do not delete the following stuff; handle()
    in mail batch mode send output to stdout/stderr!!!! */

  /* Close all file descriptors and open them as follows:
   *   0 - /dev/null
   *   1 - output of* file
   *   2 - output of* file
   */
  for(i=getdtablesize()-1 ; i >= 0; i--)
    close(i);
  open("/dev/null", 0, 0);		/* stdin */
  sprintf(fname, "%s/o%s", qp->q_name, jp->j_cfname+1);
  if(creat(fname, 0600) < 0) {		/* stdout */
    mperror1("Can't creat(%s) output file", fname );
    exit(1);
  }
  else {
    if(open(fname, O_WRONLY) < 0) {		/* stdout */
      mperror1("Can't reopen(%s) output file", fname );
      exit(1);
    }
  }
  /* stderr */
  if( dup(1) == -1 ){
    mperror1("'%s': dup(1) failed", fname );
    exit(1);
  }

  /*
   * At this point, all user-caused error messages can simply go
   * in the open output file on stderr.
   */

  /* This creates a file owned by the user.  This means the user can
   * write into this file, so the user can start a long-sleeping batch
   * job and use this file for non-quota storage, but then the user
   * could do that with the user-owned input file too...  If this
   * fails, well, too bad; keep going.  -IAN!  */

  if( fchown(1, pw->pw_uid, pw->pw_gid) == -1 ){
    mperror3("'%s': fchown(1, %d, %d) failed",
	     fname, pw->pw_uid, pw->pw_gid );
    /* no exit; just keep going */
  }

  /*
   * The original environment has been thrown away, so this becomes
   * the basic environment.  Everything else comes from "setenv"
   * commands at the beginning of the queue_bd batch file.
   */
#define OVERWRITE 1
  if(    setenv("BATCHQUEUE",	qp->q_name,	OVERWRITE)
	 || setenv("BATCHJOB",	jp->j_cfname,	OVERWRITE)
	 || setenv("PATH",		DEFPATH,	OVERWRITE) ){
    fprintf(stderr, "QUEUED: '%s': Failed to set environment: %s\n",
	    filename, syserr() );
    /* no exit; just keep going */
  }
#ifdef HAVE_GETRLIMIT
  /*
   * vfork doesn't zero resource usage in the child the way fork does,
   * so ignore XCPU here.  Note that caught signals are reset to the
   * default after exec.  */
#ifdef SIGXCPU
  (void)signal(SIGXCPU, sig_ignore);
#endif
#ifndef solaris
  /*Eric Deal <eric.deal@conexant.com> found that this setrlimit
code breaks Solaris. Should test to see if it breaks other platforms
as well. GNU/Linux seems OK.*/
  for( i=0; i<RLIM_NLIMITS; i++ ){
    register struct rlimit *rlp = &(qp->q_rlimit[i]);
    if( rlp->rlim_cur >= 0 && rlp->rlim_max >= 0 )
      (void) setrlimit( itorl(i), rlp );
  }
#endif /*end not for solaris.*/
#endif /*HAVE_GETRLIMIT*/
  if (qp->q_nice){
    if( nice(qp->q_nice) < 0 ){
      mperror2("'%s': nice(%d) failed",
	       qp->q_name, qp->q_nice );
      /* no exit; just keep going */
    }
  }

  /* WGK: deleted initgroups, setgid/setuid, and chdir since these are done in handle() code.*/

  /*
   * Make sure we start with a clean signal state.
   * Don't re-enable the SIGXCPU purposefully ignored above;
   * it will get reset to default on exec.
   */
  for (i = 1; i < NSIG; i++)
#if defined(SIGXCPU)
    if( i == SIGXCPU )
      (void)signal(SIGXCPU, sig_ignore);
    else
#endif
      (void) signal(i, SIG_DFL);
#if defined(HAVE_SIGSETMASK)||defined(sigsetmask)
  (void)sigsetmask(0);		/* unblock all signals */
#endif
  (void)alarm(0);			/* disable pending alarms */
  /* Because of the chdir() above, we need an absolute pathname.
   */
  sprintf(fname, "%s/%s", spooldir, filename);
	
  /*
    1998/08/05

    Here's the main hack: Instead of cancelling the job,
    batchd now calls queue_bd. We're also using our binary-format structure
    to give job info, rather than the shell script original used by
    batch.

  */

  /*1998/12/23 WGK:

    Instead of calling the old queued, we call the handle routine
    with which we are now linked.

  */
  /*	execl(QUEUED, "queued", fname, (char *)0);
	
	saverrno = errno;
	fprintf(stderr, "QUEUED: '%s': Unable to execute: %s; job deleted\n",
	fname, syserr() );
	errno = saverrno;
	mperror2("Can't execl(%s, %s, 0)", fname, filename);
	exit(1);*/
  if (qp->q_checkpointmode == KERNEL_CHECKPOINT)
    handle(filename, checkpoint, restart, mfile);
  else
    handle(filename, checkpoint, restart, qp->q_restartflag);
  exit(1);
}

#ifdef HAVE_GETRLIMIT
/*
 * Structure to match lex keywords to RLIMIT values and to small
 * integers to index the q_rlimit array.
 */
static struct {
  int r;
  enum keyword kwd;
} rtab[] = {
#ifdef RLIMIT_CPU
  RLIMIT_CPU,	K_RLIMITCPU,
#endif
#ifdef RLIMIT_FSIZE
  RLIMIT_FSIZE,	K_RLIMITFSIZE,
#endif
#ifdef RLIMIT_DATA
  RLIMIT_DATA,	K_RLIMITDATA,
#endif
#ifdef RLIMIT_STACK
  RLIMIT_STACK,	K_RLIMITSTACK,
#endif
#ifdef RLIMIT_CORE
  RLIMIT_CORE,	K_RLIMITCORE,
#endif
#ifdef RLIMIT_RSS
  RLIMIT_RSS,	K_RLIMITRSS,
#endif
#ifdef RLIMIT_NOFILE
  RLIMIT_NOFILE,  K_RLIMITNOFILE,
#endif
#ifdef RLIMIT_VMEM
  RLIMIT_VMEM, K_RLIMITVMEM,
#endif
};

/* Turn RLIMIT manifest number into a small Integer 0 <= i < RLIM_NLIMITS
 * used to index the q_rlimit array.
 */
int
rltoi(register int rl)
{
  register int i;

  for (i = 0; i < sizeof rtab/sizeof rtab[0]; i++)
    if (rtab[i].r == rl)
      return i;
  error1("%d: invalid RLIMIT value\n", rl);
  /*NOTREACHED*/
}

/* Turn K token from LEX into RLIMIT number.
 */
int
Ktorl(register enum keyword kwd)
{
  register int i;

  for (i = 0; i < sizeof rtab/sizeof rtab[0]; i++)
    if (rtab[i].kwd == kwd)
      return rtab[i].r;
  error1("%d: invalid keyword value\n", (int)kwd);
  /*NOTREACHED*/
}

/* Turn small Integer 0 <= i < RLIM_NLIMITS into RLIMIT number.
 */
int
itorl(int i)
{
  if ((unsigned)i < sizeof rtab/sizeof rtab[0])
    return rtab[i].r;
  error1("%d: invalid integer rlimit value\n", i);
  /*NOTREACHED*/
}
#endif /*HAVE_GETRLIMIT*/

/*
 * WGK 2000/07/30 Here's the code that brings us to the long-anticipated 1.20 release:

this code actually gets jobs lying about on the server onto another server.
 */
int
transmitjob(struct job *jp) {
  struct queue_b *qp;
  char cfname[DIRSIZ_Q*3+3];
  char cfname2[DIRSIZ_Q*3+3];
  FILE *myfile, *cfile;
  int fd, minusone=-1, oldalrm;
  char c;

  qp = jp->j_queue_b;	/* queue_b in which job resides */

  sprintf(cfname, "%s/%s", qp->q_cfdir, jp->j_cfname);
  sprintf(cfname2, "%s/%c%s", qp->q_cfdir, 't', jp->j_cfname+1);
  /* WGK 2000/08/11 Bug spotted by eht@sourceforge.net */
  /* QueueD could deadlock if both are trying to transmit at same time to
     each other. So, always fork by setting TRANSMIT_DEBUG .*/
  /* #undef TRANSMIT_DEBUG */
#ifdef TRANSMIT_DEBUG
    systemflag = 1;
    if(!fork()) {
      if((fd = wakeup(qp->q_name, Q_STARTNEW, Q_CONTACT, myhostname()))<0) exit(0);
#else
      /*Don't send it to this machine; that would be silly. Hence,
	we set the not host argument, so we don't even bother contacting that machine.*/
      if ((fd = wakeup(qp->q_name, Q_STARTNEW, Q_CONTACT, myhostname()))<0) return -1; /* No available hosts (besides maybe ourselves).*/
#endif
      myfile=fdopen(fd, "w");

      /*minus one indicates we are sending out cfile to start new.*/

      netfwrite(&minusone, sizeof(int), 1, myfile);

  link(cfname, cfname2); /*The reason for this slight of hand is to ensure that
			   on machines where we still NFS share there aren't problems. This directory
                           probably shouldn't be shared, so this can be gotten rid of.*/
  unlink(cfname);
   oldalrm = alarm(5); /*5 seconds should be enough to send this file.*/
    sigalrmflag=0;

      if(!(cfile = fopen(cfname2, "r"))) {
	mdebug1("Unable to open %s\n", cfname2);
#ifdef TRANSMIT_DEBUG
	fprintf(stderr, "Unable to open %s\n", cfname2);
	exit(2);
#endif
	return -1;
      }
      mdebug1("Transmitting %s\n", cfname);
      while(!sigalrmflag&&((c=fgetc(cfile))!=EOF)) {fputc(c, myfile);}
      fclose(cfile);
      alarm(oldalrm);
      fclose(myfile);
      close(fd);
      unlink(cfname2);
      
#ifdef TRANSMIT_DEBUG
      exit(0);
}
#endif
return 0;
}


/*
 * Deal with a terminated job.
 */
void
terminated(register int pid, register int status, register DOUBLE totalcpu)
{
  register struct running *rp;
  register struct queue_b *qp;
  register struct job *jp;
  char cfname[DIRSIZ_Q*3+3];
  char cfname2[DIRSIZ_Q*3+3];
  char mfname[DIRSIZ_Q*3+3];
  char mfname2[DIRSIZ_Q*3+3];
  char str[STRSIZ];

  mdebug1("terminated job pid %d\n", pid);
#ifdef __hpux
  terminatedfailed = 0;
#endif
  FOREACH (rp, running)
    if(rp->r_pid == pid)
      goto found;
  /*
   * If the pid wasn't in the internal list, and the "system" call
   * flag is set, assume all is OK in HP-UX land (and set the
   * flag to indicate that this routine failed.
   */
  if (systemflag) {
    terminatedfailed = 1;
    return;
  }
  sprintf(str, "Status %d (0%o) from process %d but process not in internal table\n",
	  status, status, pid);
  merror(str);
  return;
 found:
  nkids--;
  jp = rp->r_job;
  if (jp)
    qp = jp->j_queue_b;
  freerunning(rp);
  if (jp == NULL) {
    /*  Assume this is return of a mail process */
    if( status != 0 )
      fprintf(stderr,
	      "QUEUE: Status %d (0%o) from QUEUED MAIL process %d\n",
	      status, status, pid);
    return;
  }
  /*
   * Paranoia
   */
  {
    struct queue_b *q;
    struct job *j;

    FOREACH (q, queue_bs)
      if (q == qp)
	FOREACH (j, qp->q_jobs)
	  if (j == jp)
	    goto ok;
    error1("Tables corrupted: pid %d not found\n", pid);
  ok:;
  }
  /*WGK 1999/03/06 Don't nuke cf file or send mail back for
    migrated jobs.*/
  sprintf(mfname, "%s/%c%s", qp->q_name, 'x', jp->j_cfname+1);
  sprintf(cfname2, "%s/%c%s", qp->q_cfdir, 't', jp->j_cfname+1);
  sprintf(cfname, "%s/%s", qp->q_cfdir, jp->j_cfname);
  if(access(mfname, F_OK)==0) {
    int fd, c, oldalrm;
    FILE *myfile, *cfile;
    int minustwo = -2;
    int len;
    struct stat sbuf;
    mdebug3("Preparing to migrate out job pid %d name '%s/%s'\n",
	    pid, jp->j_queue_b->q_cfdir, jp->j_cfname );
    link(cfname, cfname2);
#define MIGRATE_DEBUG 1
#ifdef MIGRATE_DEBUG
    systemflag = 1;
    if(!fork()) {
#endif
      fprintf(stderr, "opening %s\n", mfname);
      if(!(cfile = fopen(mfname, "r"))) {
	fprintf(stderr, "Unable to open %s\n", mfname);
	exit(2);
      }
#ifdef MIGRAT_DEBUG
      fd = wakeup(qp->q_name, Q_MIGRATING, Q_CONTACT,NULL);
#else
      fd = wakeup(qp->q_name, Q_MIGRATING, Q_CONTACT,myhostname());
#endif
      myfile=fdopen(fd, "w");

      /*First four bytes give length of incoming file followed by
	filename.  If length is -2, this is a mfile, and the filename
	is unnecessary, but the length of the file is expected as the
	next integer.*/


      netfwrite(&minustwo, sizeof(int), 1, myfile);
      if (stat(mfname, &sbuf) < 0) { /*error*/;}

      len = (int) sbuf.st_size;
      netfwrite(&len, sizeof(int), 1, myfile);



      oldalrm = alarm(5); /*5 seconds should be enough to send this file.*/
      sigalrmflag=0;
      while(len--&&!sigalrmflag&&((c=fgetc(cfile))!=EOF)) {fputc(c, myfile);}
      fclose(cfile);
      if (len!=-1) {
	/*Error.*/ 
#ifdef MIGRATE_DEBUG
	fprintf(stderr, "Outgoing migrator did not transmit properly, %d left.\n", len);
	exit(2);
#endif
      }
      fprintf(stderr, "opening %s\n", cfname2);
      /*When length has expired, time to send cfile.*/
      if(!(cfile = fopen(cfname2, "r"))) {
	fprintf(stderr, "Unable to open %s\n", cfname);
	exit(2);
      }
      fprintf(stderr, "opening %s\n", cfname2);
      while(!sigalrmflag&&((c=fgetc(cfile))!=EOF)) {fputc(c, myfile);}
      fclose(cfile);
      alarm(oldalrm);
      fclose(myfile);
      close(fd);
      unlink(mfname);
      /*	    unlink(cfname);*/
#ifdef MIGRATE_DEBUG
      exit(0);
    }
#endif
  }
  else {
    mdebug3("terminated job pid %d name '%s/%s'\n",
	    pid, jp->j_queue_b->q_cfdir, jp->j_cfname );
    jp->j_totalcpu = totalcpu;
    mailback(status, jp, MAIL_END, (char *)0);
    if(qp->q_supmail & MAIL_END) {
      sprintf(errstr,
	      "%s: %s: END: cpu %.1fs signal %d exit %d (0%o)\n",
	      qp->q_name, jp->j_cfname, totalcpu, status & 0377,
	      (unsigned)status >> 8, (unsigned)status >> 8);
      muser(qp->q_supervisor, errstr);
    }
  }
	
  if(unlink(cfname) == -1) {
    int e = errno;
		
    mperror1("Can't unlink(%s)", cfname);
    sprintf(errstr, "%s: Can't unlink job '%s'%s\n", qp->q_name,
	    cfname, e==ENOENT? "": "; stopping queue_b");
    muser(qp->q_supervisor, errstr);
    if (e != ENOENT)
      drainqueue_b(qp);	/* stop further jobs from starting */
  }
	
  freejob(jp);

  qp->q_nochange = 0;
  --totalrunning;	/* total number of running jobs */
  if (--qp->q_nexec == 0) {
    /*
     *  Drained?
     */
    if(qp->q_drain == Q_DRAINING) {
      muser1(qp->q_supervisor,
	     "%s: drained: finally\n", qp->q_name);
    }
    if(qp->q_migrate == Q_MIGRATING) {
      muser1(qp->q_supervisor,
	     "%s: migrated: finally\n", qp->q_name);
    }
    /*
     * When a queue_b vanishes, the q_deleteq flag is set.
     * When we have dealt with the last terminated job in this
     * queue_b, we release the queue_b table entry.  */
    if(qp->q_deleteq)
      freequeue_b(qp);
  }
}

/* A queue_b has vanished and no jobs are executing in it.
 * Get rid of all we know about it.
 */

void
freequeue_b(struct queue_b *qp)
{
  register struct job *jp;

  if (qp->q_nexec != 0){
    merror2("Attempt to free queue_b '%s' before %d jobs have finished",
	    qp->q_name, qp->q_nexec);
    return;
  }
  freeqstorage(qp);	/* free profile dynamic storage */
  if (qp->q_name){
    free(qp->q_name);
    qp->q_name = NULL;
  }
  if (qp->q_queue_bstat){
    free(qp->q_queue_bstat);
    qp->q_queue_bstat = NULL;
  }
  if (qp->q_profile){
    free(qp->q_profile);
    qp->q_profile = NULL;
  }
  if (qp->q_cfdir){
    free(qp->q_cfdir);
    qp->q_cfdir = NULL;
  }
  FOREACH (jp, qp->q_jobs)
    freejob(jp);
  free((char *)qp->q_jobs);
  remque((struct qelem_queue *)qp);
  free((char *)qp);
}

/*
 * Free dynamic strings and close open units in a queue_b structure.
 * This is done before re-reading the queue_b profile and when
 * deleting a queue_b.  
 * Only stuff that might be set in the profile file is freed.  */
void	
freeqstorage(struct queue_b *qp)
{
  if (qp->q_minfree){
    releasedev(qp->q_mfdev);
    qp->q_minfree = 0;
    qp->q_mfdev = -1;
  }
  if (qp->q_supervisor){
    free(qp->q_supervisor);
    qp->q_supervisor = NULL;
  }
  if (qp->q_status1){
    free(qp->q_status1);
    qp->q_status1 = NULL;
  }
  if (qp->q_program) {
    free(qp->q_program);
    qp->q_program = NULL;
  }
  if (qp->q_timestop) {
    free(qp->q_timestop);
    qp->q_timestop = NULL;
  }
  if (qp->q_timesched) {
    free(qp->q_timesched);
    qp->q_timesched = NULL;
  }
}

void	
freejob(struct job *jp)
{
  if (jp->j_jobname)
    free(jp->j_jobname);
  if (jp->j_userid)
    free(jp->j_userid);
  if (jp->j_mailuserid)
    free(jp->j_mailuserid);
  if (jp->j_onlyhost)
    free(jp->j_onlyhost);
  /*Unlock job and release lock file descriptor.*/
  if (jp->j_lockfd > 0) close(jp->j_lockfd);

  /*WGK 1998/08/14 _MAJOR_ GNU/Linux bug fix here.
    Original batch code called free() first and then remque,
    which is actually OK with most versions of Unix that only
    do garbage collection upon the next call to calloc or malloc.
    GNU/Linux does garbage collection immediately, and so this corrupted
    everything. Code should be checked to make sure this doesn't happen
    elsewhere.*/
  remque((struct qelem_queue *)jp);
  free((char *)jp);
	
}

void	
freerunning(struct running *rp)
{
  remque((struct qelem_queue *)rp);
  free((char *)rp);
}

void
mailback(int status, register struct job *jp, register bat_Mail mailstat, register char *expl)
{
  extern FILE *sendmail(register char *to, register char *from);
  register struct queue_b *qp = jp->j_queue_b;
  register FILE *mail;
  char fname[DIRSIZ_Q*3+3];
  char pgrpname[DIRSIZ_Q*3+3];
  char iname[DIRSIZ_Q*3+3];
  FILE *inputf;
  register int n;
  register int output;
  register int outstat;
  struct stat sb;

  char *subj_keyword;

  /*
   * Send mail even if we aren't supposed to if there is output
   * or status is non-null.
   * Make sure bounced mail goes to the real user.
   */
  sprintf(fname, "%s/o%s", qp->q_name, jp->j_cfname+1);
  sprintf(pgrpname, "%s/e%s", qp->q_name, jp->j_cfname+1);
  outstat = stat(fname, &sb);

  /*WGK: Oh no we don't. If mailflag zero, we are in interactive
    mode. Don't send mail.*/

  if((!jp->j_mailflag)||((qp->q_usermail & mailstat) == 0  &&
			 (outstat == -1 || sb.st_size==0)  &&
			 status == 0)) {
    /* vestige:
       if(unlink(fname) < 0)
       mperror1("Can't unlink(%s)", fname);*/
    (void)unlink(pgrpname);
    return;
  }
  mail = sendmail(jp->j_mailuserid, jp->j_userid);
  fprintf( mail, "Sender: %s\n", jp->j_userid);
  fprintf( mail, "Reply-To: %s\n", jp->j_userid);
  fprintf( mail, "Errors-To: %s\n", jp->j_userid);
  /*
   * Give some sort of hopefully useful subject.
   */
  if ( mailstat & MAIL_CRASH ) {
    subj_keyword = "Crash";
  } else if ( status != 0 ) {
    subj_keyword = "Unsuccessful";
  } else {
    subj_keyword = "Success";
  }
  fprintf(mail, "Subject: %s%s: '%s' in '%s' queue_b\n\n",
	  subj_keyword, sb.st_size ? "+Output" : "",
	  jp->j_jobname, qp->q_name );

  fprintf(mail, "Job '%s' in '%s' queue_b on %s has ", jp->j_cfname, qp->q_name, myhostname());
  if(mailstat & MAIL_CRASH)
    fprintf(mail, "been caught in crash; it was %s", expl);
  else {
    fprintf(mail, "completed");
    if(status == 0)
      fprintf(mail, " successfully");
    else {
      register int sig = status & 0177;

      if(sig) {
	if(sig > NSIG)
	  fprintf(mail, " with unknown status 0%o", status);
	else
	  fprintf(mail, " with signal termination: %s",
		  sys_siglist[sig]);
	if(status & 0200)
	  fprintf(mail, ", and a core dump");
      } else
	fprintf(mail, " with exit code %d", status >> 8);
    }
    fprintf(mail, ".\nTotal CPU used: %.1f sec", jp->j_totalcpu);
  }
  fprintf(mail, ".\n");
  /*
   * Also send along first few lines of the input,
   * to make it clear what was going on.
   * sahayman oct 31/86
   */

  /*WGK: Yeah, there was some cute code that printed the first few
    lines in a batch file. We don't use batch files, so we don't use it.*/

  if(outstat == -1)
    fprintf(mail, "Can't stat output file %s\n", fname);
  else if(sb.st_size > 0) {
    fprintf(mail, "Output %sfollows:\n\n", mailstat & MAIL_CRASH?
	    "to crash point ": "");
    if((output = open(fname, 0)) == -1) {
      mperror1("Can't open(%s) output file", fname);
      fprintf(mail,
	      "QUEUED can't read output data from '%s'\n",
	      fname);
    } else {
      char *buffer = mymalloc(MAXBSIZE);

      (void) fflush(mail);
      while ((n = read(output, buffer, MAXBSIZE)) > 0)
	write(fileno(mail), buffer, n);
      close(output);
      free(buffer);
    }
  }
  if(unlink(fname) < 0)
    mperror1("Can't unlink(%s)", fname);
  (void)unlink(pgrpname);
  mailclose(mail);
}

/*
 *  Abort a particular job.  Use extreme prejudice.
 *  Note that we count on wait getting the process back later.
 */
int
abortjob(struct job *jp)
{
  /*WGK: Bad idea; we running queue_bd. Hope queued is awake.*/
  /*	return( sigjob(jp, SIGKILL) );*/
  return( sigjob(jp, SIGTERM) );
}

/*
 *  Abort all jobs in a particular queue_b and drain it so no new jobs start.
 */
void	
abortall(register struct queue_b *qp)
{
  int numkilled;

  drainqueue_b(qp);		/* stop further jobs from starting */
	
  /*Assumed queue_bd works will enough to act on a SIGTERM.*/
  numkilled = sigalljobs(qp, SIGTERM);
  /*	numkilled = sigalljobs(qp, SIGKILL);*/

  muser3( qp->q_supervisor,
	  "%s: Queue aborted; jobs=%d, jobs killed=%d\n",
	  qp->q_name, qp->q_nexec, numkilled );
  queue_bstat1(qp, "Aborted; killed=%d\n", numkilled);
}

/*
 * Send a specified signal to all executing jobs in a queue_b.
 * Don't try to signal jobs that haven't started running.
 */
int
sigalljobs(register struct queue_b *qp, int sig)
{
  register struct job *jp;
  int n = 0;

  FOREACH (jp, qp->q_jobs)
    if( jp->j_pid > 0 )
      n += sigjob(jp, sig);
  return n;
}

/*
 * Send a specified signal to a job process group.
 * We arranged that each job is in its own
 * process group, so we can send the signal
 * to all processes that are part of the job.
 * Return 1 if we successfully killed the job.
 */
int
sigjob(struct job *jp, int sig)
{
  if( jp->j_pid <= 1 ){
    merror3("Job '%s/%s' has invalid pid %d\n",
	    (jp->j_queue_b && jp->j_queue_b->q_cfdir)
	    ? jp->j_queue_b->q_cfdir : "???",
	    jp->j_cfname ? jp->j_cfname : "???",
	    jp->j_pid );
    return 0;
  }
  /* negative process id means kill the whole process group */
  return( (kill(-jp->j_pid, sig) == -1) ? 0 : 1 );
}

/*
 * Stop new jobs from starting in this queue_b.
 * This should be the only place, other than the profile reading,
 * where q_drain is set.
 */
void	
drainqueue_b(register struct queue_b *qp)
{
  if(qp->q_drain == Q_DRAINING)
    return;		/* already draining */

  qp->q_drain = Q_DRAINING;
  qp->q_nochange = 0;	/* tell runqueue_b() about the change */

  muser3(qp->q_supervisor, "%s: %s; jobs left = %d\n",
	 qp->q_name,
	 qp->q_nexec ? "draining" : "drained",
	 qp->q_nexec);
}

void
migratequeue_b(register struct queue_b *qp)
{
  if(qp->q_migrate == Q_MIGRATING)
    return;		/* already migrating*/

  qp->q_migrate = Q_MIGRATING;
  qp->q_nochange = 0;	/* tell runqueue_b() about the change */

  muser3(qp->q_supervisor, "%s: %s; jobs left = %d\n",
	 qp->q_name,
	 qp->q_nexec ? "migrating" : "migrated",
	 qp->q_nexec);
}

struct	nlist nl[] = {
#ifdef stardent
# define unixpath "/unix"
  { "avenrun" },
#else
#ifdef __hpux
# define unixpath "/hp-ux"
#ifdef __hppa       /* series 700 & 800 */
  { "avenrun" },
#else               /* series 300 & 400 */
  { "_avenrun" },
#endif
#else
# define unixpath "/vmunix"
  { "_avenrun" },
#endif
#endif
  { 0 },
};

/* WGK 2000/5/17 Using getloadf() from GNU Emacs in getloadavg.c as suggested by Dan Nicolaescu <dann@ics.uic.edu>*/

getloadf(a)
     float *a;
{
  /*WGK 2000/5/17 The old getloadf actually did a lot of OS-dependent stuff to try to figure out the load. It returned float. Getloadavg returns double.
    We should just re-write the two places that use float. But this is a safer quick hack.*/

static double atemp[3];
 register int i;
 (void) getloadavg (atemp,3);
 for(i=0;i<3;++i) a[i] = atemp[i];
}

void
getload(int *a)
{
  static float atemp[3];
  register int i;
  getloadf(atemp);
  for(i=0;i<3;++i) a[i] = atemp[i];
}


struct qelem_queue *
enqueue_b(struct qelem_queue *qhead, int size)	/* allocate a new element, add it to the queue_b */
	                          
{
  struct qelem_queue *q;

  q = (struct qelem_queue *) mycalloc(size);	/* init to zeroes */
  insque(q, qhead);
  return q;
}

struct qelem_queue *
emptyq(void)		/* return an empty queue_b */
{
  struct qelem_queue *q;

  q = (struct qelem_queue *) mycalloc((int)sizeof *q);
  q->q_forw = q->q_back = q;

  return q;
}

char *
mycalloc(int n)
{
  register char *p;
  /*	extern char *calloc();*/

  if ((p = calloc(1, (size_t)n)) == NULL)
    error("Out of memory\n");
  return p;
}

char *
myhostname(void)
{


  static char *h = 0;

  if (h == 0) {
    char *host, *ret;
    host = mymalloc(MAXHOSTNAMELEN* sizeof(char));
    gethostname(host, MAXHOSTNAMELEN- 1);
    h = strdup(canonicalhost(host));
    free(host);

    /*Convert hostname to lowercase.*/
    for(ret=h;*ret!=0;ret++) *ret=tolower(*ret);
  }
  return h;
}


/*Define WEIRD_SYSTEM to use the default behavior at the bottom of
this file; most others will recognize this.*/
#ifndef WEIRD_SYSTEM

fdev
getfs(char *file)
{
  int fsdev;
#if defined(sun) || defined(apollo) || defined(stardent) || defined(__hpux) || defined(linux)
  if ((fsdev = open(file, O_RDONLY, 0)) < 0)
    mperror1("Can't open(%s) for reading", file);
  return fsdev;
#else
#ifdef __sgi
  struct mntent *mnt;
  FILE *fp;
  struct stat stbuf, stb;

  if (stat(file, &stbuf) < 0) {
    mperror1("Can't stat(%s)", file);
    return -1;
  }
  if ((fp = setmntent("/etc/fstab", "r")) == NULL) {
    mperror1("Can't read %s", "/etc/fstab");
    return -1;
  }
  while (mnt = getmntent(fp)) {
    if (stat(mnt->mnt_fsname, &stb) == 0 &&
	stb.st_rdev == stbuf.st_rdev) {
      endmntent(fp);
      if ((fsdev = open(mnt->mnt_fsname, O_RDONLY)) < 0) {
	mperror1("Can't open(%s) file system for reading",
		 mnt->mnt_fsname);
	return -1;
      }
      return fsdev;
    }
  }
  endmntent(fp);
#else
  struct fstab *fsp;
  struct stat stbuf, stb;

  if (stat(file, &stbuf) < 0) {
    mperror1("Can't stat(%s)", file);
    return -1;
  }
  setfsent();
  while ((fsp = getfsent()) != NULL) {
    if (stat(fsp->fs_spec, &stb) == 0 &&
	stb.st_rdev == stbuf.st_dev) {
      endfsent();
      if ((fsdev = open(fsp->fs_spec, O_RDONLY)) < 0) {
	mperror1("Can't open(%s) file system for reading",
		 fsp->fs_spec);
	return -1;
      }
      return fsdev;
    }
  }
  endfsent();
#endif /*__sgi*/
  merror1("%s: located on unknown device\n", file);
  return -1;
#endif /*sun*/
}

long
fsfree(int dev)
{

  /*WGK 1998/08/05 The purpose of the following code is to check disk
    free space, which was a nice (but unessential) feature of the
    original batch program.

    As you might expect, the code is very much OS dependent.*/

#ifdef HAVE_SYS_STATVFS_H
#define STATFS statvfs
#define FSTATFS fstatvfs
#else
#define STATFS statfs
#define FSTATFS fstatfs
#endif

#if !defined(apollo)&&!defined(stardent)
  struct STATFS f;
  if (FSTATFS(dev, &f) < 0) {
    merror1("fstatfs: %s\n", syserr());
    return 0;
  }
  return f.f_bavail*f.f_bsize/1024;
#else
#ifdef apollo
  /* Correct Apollo DN10000 brain-damage: f_bfree is return in kB, not blocks */
#if (_ISP__A88K == 1)
#define APOLLOFACTOR 4
#else
#define APOLLOFACTOR 1
#endif
  struct STATFS f;

  if (FSTATFS(dev, &f, sizeof f, 0) < 0) {
    merror1("fstatfs: %s\n", syserr());
    return 0;
  }
  return ((float)f.f_bfree/APOLLOFACTOR)*f.f_bsize/1024;
#else
#ifdef stardent
  struct STATFS f;

  if (FSTATFS(dev, &f, sizeof f, 0) < 0) {
    merror1("fstatfs: %s\n", syserr());
    return 0;
  }
  return f.f_bfree*f.f_bsize/1024;
#else

  struct fs sblock;
  long freesp;

  (void) lseek(dev, (off_t)(SBLOCK * DEV_BSIZE), 0);
  if (read(dev, (char *)&sblock, (int)sizeof sblock) != sizeof sblock) {
    merror1("Superblock read error: %s\n", syserr());
    return 0;
  }
  freesp = freespace(&sblock, sblock.fs_minfree);
  if (freesp > 0)
    return freesp * sblock.fs_fsize / 1024;
  return 0;
#endif /*stardent*/
#endif /*apollo*/
#endif /*sun*/
}

void	
releasedev(int dev)
{
  if (dev > 0)
    close(dev);
}
#else /*not __sgi or stardent or apollo; this is misc code that can be used if
	above failed; please report to bug-queue@gnu.org*/

fdev
getfs(file)
     char *file;
{
  struct stat statb;

  if (stat(file, &statb) < 0) {
    mperror1("Can't stat(%s)", file);
    return -1;
  }
  return statb.st_dev;
}

long
fsfree(dev)
     dev_t dev;
{
  struct ustat ustatb;
  char str[STRSIZ];


  if (ustat(dev, &ustatb) < 0) {
    sprintf(str, "ustat of %d/%d: %s\n", (dev>>8)&0xff, dev&0xff,
	    syserr());
    merror(str);
    return 0;
  } else
    return ustatb.j_tfree * BBSIZE / 1024;
}

/*ARGSUSED*/
void	
releasedev(dev)
     dev_t dev;
{
}
#endif /*misc code to be used if specific code fails.*/

#if !defined(HAVE_INSQUE)&&!defined(insque)
void
insque(struct qelem_queue *elem, struct qelem_queue *pred)
{
  elem->q_forw = pred->q_forw;
  elem->q_back = pred;
  pred->q_forw->q_back = elem;
  pred->q_forw = elem;
}

void	
remque(struct qelem_queue *elem)
{
  elem->q_back->q_forw = elem->q_forw;
  elem->q_forw->q_back = elem->q_back;
  /* return elem; */
}
#endif /*no insque*/

/*
 * Return non-zero if the time `tp' is within
 * the given time specifier.  This function just
 * breaks the specifier into comma-separated elements,
 * calls check1time on each, and returns true if any of them match.
 */
int
checktime(char *p, struct tm *tp)
{
  char *q;
  int i;

  while (p) {
    if ((q = index(p, ',')) != NULL)
      *q = 0;
    i = check1time(p, tp);
    if (q != NULL)
      *q++ = ',';
    if (i)
      return i;
    p = q;
  }
  return 0;
}

int
check1time(char *p, struct tm *tp)
{
  int i, tl, th, tn, dayok=0;
  static struct {
    char *str;
    enum { Wk, Night, Any, Evening } tok;
  } t[] = {
    "Wk", Wk,
    "Night", Night,
    "Any", Any,
    "Evening", Evening,
  };

  while (isspace(*p))
    p++;
  if (strncmp(p, "SuMoTuWeThFrSa"+tp->tm_wday*2, 2) == 0)
    dayok = 1, p += 2;
  else for (i = 0; i < sizeof t/sizeof t[0]; i++) {
    if (strncmp(p, t[i].str, strlen(t[i].str)) != 0)
      continue;
    p += strlen(t[i].str);
    switch (t[i].tok) {
    case Wk:
      if (tp->tm_wday >= 1 && tp->tm_wday <= 5)
	dayok = 1;
      break;
    case Night:
      if (tp->tm_wday == 6 || /* Sat */
	  tp->tm_hour >= 23 || tp->tm_hour < 8 ||
	  /* Sunday before 5pm */
	  (tp->tm_wday == 0 && tp->tm_hour < 17))
	dayok = 1;
      break;
    case Any:
      dayok = 1;
      break;
    case Evening:
      /* Sat or Sun */
      if (tp->tm_wday == 6 || tp->tm_wday == 0 ||
	  tp->tm_hour >= 17 || tp->tm_hour < 8)
	dayok = 1;
      break;
    }
    break;
  }
  if (sscanf(p, "%d-%d", &tl, &th) != 2)
    return dayok;
  tn = tp->tm_hour * 100 + tp->tm_min;
  if (th < tl) { 		/* crosses midnight */
    if (tl <= tn || tn < th)
      return 1;
  } else {
    if (tl <= tn && tn < th)
      return 1;
  }
  return 0;

}



/* WGK: CHECK_QUERY() opens two socket streams, QUERY_PORT and
   WAKEUP_PORT and waits on them in a critical select() call that is
   periodically interupted by the usual ALRM signal. This allows
   batchd to act as a server.

   QUERY_PORT is given the name of a queue_b and returns with the
   virtual load average on that queue_b.

   WAKEUP_PORT does the equivalent of an ALRM signal. It saves us the
   trouble of having an alarm signal go off where it should not,
   however, and instead allows us to deal with wakeup signals in the
   appropriate order.  */

/* WGK 98/12/23 Added security checks on these ports to prevent
   possibility of denial of service attack (not really; more like
   slowdown) from outside the cluster.*/


void 
check_query(void)
{

  /*Check_query() by WGK.*/

  static int sfd = -1, wfd = -1;
  struct sockaddr_in hello;

  if(sfd==-1) {
    if((sfd = socket(AF_INET, SOCK_STREAM, 0))<0) {
      mperror("socket");
      exit(2);
    }

    /*Begin Copyright (C) 1999 "Matthias Urlichs" <smurf@noris.de> 1999/03/04*/
    /*This code is under GPL; see License file for details.*/
#ifdef SO_REUSEADDR
    {
      int reuse=1;
      setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    }
#endif
    /*End "Matthias Urlichs" <smurf@noris.de> 1999/03/04*/

    hello.sin_family=AF_INET;
    hello.sin_port = htons(QUERY_PORT);
    hello.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(sfd, &hello, sizeof(hello))<0) {
      mperror("bind");
      exit(2);
    }
    listen(sfd, 10);
  }
  if(wfd==-1) {
    if((wfd = socket(AF_INET, SOCK_STREAM, 0))<0) {
      mperror("socket");
      exit(2);
    }

    /*Begin Copyright (C) 1999 "Matthias Urlichs" <smurf@noris.de> 1999/03/04*/
    /*This code is under GPL; see License file for details.*/
#ifdef SO_REUSEADDR
    {
      int reuse=1;
      setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    }
#endif
    /*End "Matthias Urlichs" <smurf@noris.de> 1999/03/04*/

    hello.sin_family=AF_INET;
    hello.sin_port = htons(WAKEUP_PORT);
    /*hello.sin_addr.s_addr = htonl(INADDR_ANY);*/
    hello.sin_addr.s_addr = INADDR_ANY;
    if(bind(wfd, &hello, sizeof(hello))<0) {
      mperror("bind");
      exit(2);
    }
    listen(wfd, 10);
  }


  while(1) {

    int len, fd22, fd3, oldalrm, migrator = 0;
    fd_set rfds;
    struct sockaddr_in incoming;
    FILE *myfile, *cfile;
    char queue_bname[255];
    struct stat sbuf;
    struct queue_b *qp;
    float load;
    float avg[3];
    char tfname[1024];
    char fname[1024];
    char cfname[1024];
    char mfname[1024];
    char tmpcookie[80];
    char path[256];
    char *deleteerr;
    char grade = 'm';
    int spoolnumber, fdcf;
    int mode, c;
    struct timeval mytime;
    struct flock mylock;

    len = sizeof(incoming);
    /*Absolutely guarantee we don't get stuck in select.*/
    mytime.tv_sec = sleeptime;
    mytime.tv_usec = 0;

    FD_ZERO(&rfds);
    FD_SET(sfd, &rfds);
    FD_SET(wfd, &rfds);
    errno = 0;
    if((sigalrmflag)||(sigchldflag)) return;

    if ((fd22 = select(32, &rfds, (fd_set *) 0, (fd_set *) 0,
		       (struct timeval*) &mytime)) < 1) {

      /*If a timeout or a signal has occured, we return to the mainloop;
	else we record error but continue checking for queries.*/

      if((sigalrmflag)||(sigchldflag)) return;
      if(fd22!=0) {	  
	if(debug) mperror("check_query: select");
	continue;
      }
      continue;
    }

    if(FD_ISSET(sfd, &rfds)) {
      mdebug("Accepting connection on query port.\n");
      if((fd22 = accept(sfd, (void*) &incoming, &len))<0) {
	if(debug) mperror("check_query: accept");
	return;
      }

      /*WGK Check to make sure this host is authorized.*/

      if(!validhost(incoming)) goto abort2;


      myfile = fdopen(fd22, "r");

      /*fgets will time out when our ALRM signal comes in anyway, so we don't
	need to protect against long timing.*/

      /*Guarantee we don't get stuck in fgets due to pokey or malicious client.*/
      oldalrm = alarm(5);

      /*WGK 2000/07/19. The draft protocol now requires us to expect the
followin string: "QUERY\nVERSION0\nVERSION1\n" specifying a VERSION0
query for a VERSION1 job control file, the only type we currently
support.*/
      if(fgets(queue_bname, 254, myfile)==NULL) goto abort;
      if(strcmp(queue_bname,"QUERY\n")) goto abort;
      if(fgets(queue_bname, 254, myfile)==NULL) goto abort;
      if(strcmp(queue_bname,"VERSION0\n")) goto abort;
      if(fgets(queue_bname, 254, myfile)==NULL) goto abort;
      if(strcmp(queue_bname,"VERSION1\n")) goto abort;

      /*Older protocol now resumes.*/
      if(fgets(queue_bname, 254, myfile)==NULL) goto abort;
      alarm(oldalrm);

      /*Eliminate possible trailing CR*/
      queue_bname[254] = 0;
      migrator = 0;
      len = strlen(queue_bname);
      if((len>=1)&&(queue_bname[len - 1]=='\012')) queue_bname[--len] = 0;
      if((len>=1)&&(queue_bname[len - 1]=='\015')) queue_bname[--len] = 0;

      /*A trailing 001 tells us that we are inquiring about a job to be migrated.*/

      if((len>=1)&&(queue_bname[len - 1]=='\001')) {
	queue_bname[--len] = 0;
	migrator = 1;
	mdebug1("QUEUED:  queue_bname indicates migrated job (%s)\n",queue_bname);

      }
      if(len<1) goto abort;

      {
	int status = -1;
	FOREACH(qp, queue_bs) if(!(status=strcmp(qp->q_name, queue_bname))) break;
	if(status!=0) goto abort;
      }

  if(!qp->q_status)
    {
        load = 2e08;
	mdebug("QUEUED:  qp->q_status is 0 -> load=2e08\n");
    }
      if(migrator && qp->q_restartmode == NO_RESTART)
        {
        load = 2e08;
        mdebug2("QUEUED:  migrator=%d q_restartmode=%d\n",migrator, qp->q_restartmode);
        }
      else {
	getloadf(avg);
	mdebug1("QUEUED:  calculating load... avg=%f\n",avg[0]);
	load = (avg[0] + 1.0)/((max(0, qp->q_vmaxexec - qp->q_nexec)+1)*qp->q_pfactor);
	mdebug1("QUEUED:  calculated load=%f\n",load);
      }
      /*This is a hack. We really shouldn't be using a single stream to
	do input/output on a socket. So, we only use one stream. We probably
	should get rid of streams all together here.*/


      /* EJD - 8/14/2000 - I changed this to netfwrite(myfile...) to
                     allow the loads to be host-independent.
                     write() is used here, but fread() in wakeup.c */

      /* was:      write(fd22, &load, sizeof(float)); */

      {
      FILE *myfilew;
      myfilew=fdopen(fd22,"w");
      mdebug2("QUEUED:  load=%5.2f (0x%x) on host.\n",load,load);
      netfwrite(&load, sizeof(float), 1, myfilew);
      fclose(myfilew);
      }

      mdebug2("Load average query response: %5.2f to query on %s\n", load, queue_bname);

      /*END EJD 2000/08/14.*/

      mdebug4("Load average: %f vmaxexec: %d nexec: %d pfactor: %d\n", avg[0],
	      qp->q_vmaxexec, qp->q_nexec, qp->q_pfactor);

      /*WGK: This is a hack to get around 1sec mtime granularity problem, where
	batch may not recognize queue_b dir has changed and so may never run
	requeue_b. Solution: If we are queried on a dir we KNOW it has changed,
	whatever mtime says.*/

      qp->q_requeue_b = 1;

      /*The key to abort is that it _continues_ on error, so that a bad query
	does NOT effect a wakeup.*/
    abort:
      fclose(myfile);
    abort2:
      /*Make sure the socket is gone.*/
      close(fd22);
    }
    if(FD_ISSET(wfd, &rfds)) {
      mdebug("Accepting connection on wakeup port.\n");
      if((fd22 = accept(wfd, (void*) &incoming, &len))<0) {
	if(debug) mperror("check_query: accepting wakeup");
	return;
      }
      /*WGK Check to make sure this host is authorized.*/

      if(!validhost(incoming)) goto abort4;
      /*  Again, to prevent cracked versions from non-privileged users, we check that the server is comming in on a reserved port*/

      incoming.sin_port = ntohs((u_short) incoming.sin_port);
#ifndef NO_ROOT
      if (incoming.sin_port >= IPPORT_RESERVED  ||
	  incoming.sin_port <  IPPORT_RESERVED/2) {
	merror1( "Connection from %s on invalid port",
		 inet_ntoa(incoming.sin_addr));
	goto abort4;
      }
#else
      /*We aren't root so we don't use reserved ports. To substitute, we try to use ident
	information. This prevents people from spoofing queued.c */
#ifdef HAVE_IDENTD
      incoming.sin_port = ntohs((u_short) incoming.sin_port);
      if(!check_ident(&hello, &incoming))  {
	merror1("Connection from %s returned bad identd information or identd timed out, and we are compiled with -DNO_ROOT and -DHAVE_IDENTD set.\n
Have your administrator fix identd problems on your cluster, OR recompile without
-DHAVE_IDENTD, OR increase identd timeout in ident.c OR install Queue as root.\n",
		inet_ntoa(incoming.sin_addr));
	goto abort4;
      }
#endif /*HAVE_IDENTD*/
#endif /*NO_ROOT*/ 



      myfile = fdopen(fd22, "r");

      /*Guarantee we don't get stuck in fgets due to pokey or malicious client.*/
      oldalrm = alarm(5);

      /*WGK 2000/07/19. The draft protocol calls for 
	"JOBCONTROLFILE\nVERSION0\nVERSION1\n" so lets make sure we get that.*/
      
            if((!fgets(&tmpcookie[0], 79, myfile))||(strcmp(&tmpcookie[0], "JOBCONTROLFILE\n"))||(!fgets(&tmpcookie[0], 79, myfile))||(strcmp(&tmpcookie[0], "VERSION0\n"))||(!fgets(&tmpcookie[0], 79, myfile))||(strcmp(&tmpcookie[0], "VERSION1\n"))) {
	    merror1("QueueD: Received invalid or unsupported JOBCONTROLFILE protocol version string: '%s'!\n",tmpcookie); goto abort;}

	 /*WGK 2000/07/18 sha1 ([FIPS180-1], [ANSI930-2] & [ISOIEC10118-3])
 is a much faster one-way function
than crypt, so we can simplify the code here a bit, provided MD5 compiles
correctly on your machine (use the md5test -x testing option if in doubt.
If not, you can always go back to the above code, which used slow UNIX
crypt().*/

	 #define ONE_WAY(a) sha1(a);
         #define randomstr(a) strcpy(a,"bigbird")

    {char cnounce[21], nounce[21], bigcookie[256];
    char *randomcookie;
    int k;

      randomstr(&cnounce[0]);

      /*Send cnounce, as compliant with draft protocol. WGK 2000/07/20*/
      write(fd22, &cnounce, strlen(cnounce)+1);
 
      /*Read nounce, as compliant with draft protocol. WGK 2000/07/20*/
 
      alarm(5);
      k = 0;
      do {
	read(fd22, &nounce[k], 1);
	k++;
      } while (nounce[k-1] && k<21);
      alarm(0);
      nounce[20] = 0;

      /*Response to challenge is constructed from nounce, cookie, and cnounce.*/

      strcpy(bigcookie,nounce);
      strcat(bigcookie,cookie);
      strcat(bigcookie,cnounce);

      randomcookie = ONE_WAY(bigcookie);

      /*Write the challenge response, thus proving via digest authentication we know the cookie without actually giving away what the cookie is. This concludes the mutual authentication as outlined in the draft protocol. WGK 07/20/2000 */

      write(fd22, &randomcookie[0], strlen(randomcookie)+1);

      /* Reverse the procedure. Read cnounce.*/

    alarm(5);
    k = 0;
    do {
      read(fd22, &cnounce[k], 1);
      k++;
    } while (cnounce[k-1] && k<21);
    alarm(0);
    cnounce[20] = 0;


    /*OK, cnounce read, now generate nounce.*/
    randomstr(&nounce[0]);
    write(fd22, &nounce, strlen(nounce)+1);
    
    strcpy(bigcookie,nounce);
    strcat(bigcookie,cookie);
    strcat(bigcookie,cnounce);
    {
      int flag = 0;
      char c;
      /*Crypt always returns the same number of characters, so we
	don't need to worry about the wrong daemon here.*/
      char *remotecookie;
      int k, j;
      remotecookie = ONE_WAY(bigcookie);
      j =strlen(remotecookie)+1;
      alarm(5);
      flag = 0;
      for(k=0;k<j&&!flag;k++){
	read(fd22, &c, 1);
	if (c != remotecookie[k]) flag = 1;
      }
      alarm(0);

      if (flag) {
	/*Alert queue.c that cookie is bad; it will have to
	  wait for a new queued */
	c = 1;
	write(fd22, &c, 1);
	merror1("QueueD: Received invalid cookie. In NO_ROOT, COOKIEFILE must be
the same on all machines! Received cookie: %s\n",tmpcookie); goto abort;

      }

	 /*All clear*/
	 c = 0;
	 write(fd22, &c, 1);


    }
    
    }


	    /*The old protocol continues. WGK 20000723*/

      if(fgets(queue_bname, 254, myfile)==NULL) goto abort;
      alarm(oldalrm);

      /*Eliminate possible trailing CR*/
      queue_bname[254] = 0;
      migrator = 0;
      len = strlen(queue_bname);
      if((len>=1)&&(queue_bname[len - 1]=='\012')) queue_bname[--len] = 0;
      if((len>=1)&&(queue_bname[len - 1]=='\015')) queue_bname[--len] = 0;

      /*A trailing 001 tells us that we are sending about a job to be migrated.*/

      if((len>=1)&&(queue_bname[len - 1]=='\001')) {
	queue_bname[--len] = 0;
	migrator = 1;
      }
      if(len<1) goto abort;

      {
	int status = -1;
	FOREACH(qp, queue_bs) if(!(status=strcmp(qp->q_name, queue_bname))) break;
	if(status!=0) goto abort3 /*protocol error*/;
      }

      netfread(&mode, sizeof(int), 1, myfile);

      if(mode!=-1 && mode!=-2) goto abort3 /*protocol error, for now.*/ ;

      /*
       * If the underlying Q_CFDIR is missing, create it.
       */
      (void)sprintf(fname, "%s/%s/%s", spooldir, queue_bname, Q_CFDIR);
      while( stat(fname, &sbuf) < 0 ||
	     (errno=ENOTDIR, (sbuf.st_mode & S_IFMT) != S_IFDIR)) {
	int saveumask;

	if( errno != ENOENT ){
	  perror(fname);
	  merror1("'%s': Cannot stat queue directory\n", fname);
	}
	saveumask = umask(022);
	if( mkdir(fname, 0775) < 0 ){
	  perror(fname);
	  merror1("Cannot mkdir(%s)\n", fname);
	  umask(saveumask);
	  goto abort3;
	}
	umask(saveumask);
	if( debug )
	  fprintf(stderr, "SYSLOG: queueD: created directory '%s'\n", fname);
	else
	  syslog(LOG_INFO, "queueD: created directory '%s'\n", fname);
	/* mkdir worked; loop back to try stat() again */
      }

      /* 95/7/3 WK: I put some of the old batch code here.*/

      /*
       * Spool temp file name starts with tf, followed by grade, followed by
       * a unique sequential number.  Real file name starts with cf.
       * The files are sorted by this name for priority of execution.
       */
      /*#ifdef BSD4_2*/

#define FMT "%s/%s/%s/%cf%c%ld"
#define FMT2 "%s/%s/%cf%c%ld"

      /*#else
	#define FMT2 "%s/%s/%cf%c%010D"
	#endif*/

    another:
      spoolnumber = unique();
      (void)sprintf(tfname, FMT, spooldir, queue_bname,
		    Q_CFDIR, 't', grade, spoolnumber);
      (void)sprintf(mfname, FMT2, spooldir, queue_bname, 'm',
		    grade, spoolnumber);
      (void)sprintf(cfname, FMT, spooldir, queue_bname,
		    Q_CFDIR, 'c', grade, spoolnumber);


      /*OK. Problem: Some commands invoke themselves recursively,
	which can cause spool files to be created very rapidly. This
	caused the old cfname to be overwritten as it was being read
	by the daemon! Solution: we first attempt to lock tfname
	below, and then check for cfname. This ensure we will not
	overwrite anything.*/

      deleteerr = tfname;		/* cleanup() deletes this file */

      /*If file exists, goto another.*/

      /*WGK 99/3/4 added S_IRUSR|S_IWUSR based on suggestion from
        zblazek@engr.UVic.CA*/

      if((fdcf=open(tfname, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR))<0) {
	if(errno==EEXIST) goto another; 
	perror("open");
	exit(2);
      }

      /*Ok, we have successfully created the file exclusively. Fine.
	Now check to make sure cfname does not also exist. If it does,
	then we have detected a race condition. We back off and sleep
	to let the time change.*/

      if(access(cfname, F_OK)==0) {
	sleep(1);
	close(fdcf);
	unlink(tfname);
	goto another;
      }
	
      mylock.l_type = F_WRLCK;
      mylock.l_whence = SEEK_SET;
      mylock.l_start = 0;
      mylock.l_len = 0;
      if(fcntl(fdcf, F_SETLKW, &mylock)<0 && errno != ENOLCK) {
	perror("fcntl");
	goto abort3;
      } 



      if (mode==-2) {
	int len;
	cfile = fopen(mfname, "w");
  
	oldalrm = alarm(5); /*5 seconds should be enough to send this file.*/
	sigalrmflag=0;

	netfread(&len, sizeof(int), 1, myfile);
	if(len<0) merror("QueueD accepted incoming migrator with bogus length!\n");

	while(len-- && !sigalrmflag &&  ((c=fgetc(myfile))!=EOF)) 
	  {fputc(c, cfile);}
    
	alarm(oldalrm);
	if(len!=-1) merror2("QueueD incoming migrator didn't finish mfile transmission: %d %d.\n", len, sigalrmflag);
	fclose(cfile);

      }

      cfile = fdopen(fdcf, "w");

      {int c;

      oldalrm = alarm(5); /*5 seconds should be enough to get this file.*/
      sigalrmflag=0;
      while(!sigalrmflag&&((c=fgetc(myfile))!=EOF)) {fputc(c, cfile);}
      alarm(oldalrm);

      fclose(cfile);
      close(fdcf);

      link(tfname, cfname);
      unlink(tfname);
      }

      /*Whew! All done.*/
      sigalrmflag = 1;

    abort3:
      fclose(myfile);
      unlink(tfname);
    abort4:
      /*Make sure the socket is gone.*/
      close(fd22);
      return;
    }
    continue;
  }
}


char *
allocline(FILE *f)
{
  char buf[1024];		/* Big enough for longest header line */
  /*WGK 2000/10/31 RedHat 7.0 signs integers different, so we need to use
an integer to take values from fget.
Bugfix inspired from fix submitted by "Arthur H. Britto II" <ahbritto@iat.com> */
  int  buffree  = sizeof(buf)-1,c;
  char *cur = buf;
  while ( (buffree--) && (c=fgetc(f)) && (c!=EOF) ) *cur++ = c;
  *cur++ = 0;
  if(*buf!='\0') return(strdup(buf));
  else return(NULL);
}

int
validhost(struct sockaddr_in address1)
{
  /*WGK: 98/12/23 added routine to protect non-critical query ports
    from denial of service attacks.*/
  register int i;
  int flag = 1; 
  char *hostname;
  struct hostent *hostp;
  char remotehost[100];

  if(!Hosts) ReadHosts(QHOSTSFILE);

  hostp = gethostbyaddr((char *) &(address1.sin_addr), 
			sizeof(struct in_addr), address1.sin_family);
  if (hostp) {
    /*Some anti DNS spoofing code; we do a reverse lookup to ensure
      everything is in order, in the style of TCP/IP wrapper.  */

    strncpy(remotehost, hostp->h_name, sizeof(remotehost) - 1);
    remotehost[sizeof(remotehost) - 1] = 0;
    if ( (hostp = gethostbyname(remotehost)) == NULL) {
      fprintf(stderr, "Unable to perform host lookup for %s", 
	      remotehost);
      return(0);
    }
    for ( ; ; hostp->h_addr_list++) {

      /*Bug fix by Stephen Jazdzewski <Stephen@Geocast.Net>; 
check for Null first before doing comparison to avoid segfault
in anonmolous /etc/hosts DNS situation.*/	    
      if (hostp->h_addr_list[0] == NULL) {
	mdebug2("DNS or /etc/hosts weirdness: Host addr %s not listed for host %s", 
		inet_ntoa(address1.sin_addr), hostp->h_name);
	return(0);
      }

      if (bcmp(hostp->h_addr_list[0], (caddr_t) &address1.sin_addr, 
	       sizeof(address1.sin_addr)) == 0)
	break; /* reverse lookup test passed.*/
    }
    /*Can't call canonical host with static hostp->h_name, make local
            copy.*/
    strncpy(remotehost, hostp->h_name, sizeof(remotehost) - 1);
    remotehost[sizeof(remotehost) - 1] = 0;
    hostname = canonicalhost(remotehost); /*fully qualified*/
  } else
    hostname = "unresolved"; 
  /*Our DNS must be down and this will probably fail; we need to add
    code to deal with bare IP addrs at some point.*/


  if(hostname) for (i=0;i<NHosts;++i) 
    if (strcasecmp(hostname, Hosts[i].host)==0) flag = 0;
  if (flag) {

    mdebug1("Host %s is not a valid server host.\n", hostname);
    return(0);
  }
  return(1);
}
