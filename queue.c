/*
 *  Queue load balancing system
 *  $Revision: 1.11 $
 *
 *  Copyright (C) 1998-2000 W. G. Krebs <wkrebs-gnu.org> (except where otherwise noted.)
 *
 *  <wkrebs@gnu.org>
 * 
 *  This file is part of the GNU Queue package, http://www.gnuqueue.org .
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
 *  Please report bugs to <bug-queue@gnu.org>.
 * 
 *
 **************************************************************************/




#include "queue.h"

/*WGK 2000/10/22 Switch to control the addition of the QUEUE_MANAGER package, submitted by Monica Lau <mllau@alantro.com>.*/

#ifndef NO_QUEUE_MANAGER
#define QUEUE_MANAGER 1
#endif

/* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
#ifdef QUEUE_MANAGER
#include "queue_define.h"
#endif /*QUEUE_MANAGER*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

#define SPOOLDIR QUEUEDIR

#define sock_error(socket, format) write(socket, format, strlen(format)); 

extern char *getenv(const char *);
/*extern char *index();*/

#if defined(HAVE_GETWD)||defined(getwd)
extern char *getwd(char *);
#else
#define getwd(arg)	getcwd(arg, (size_t) sizeof(arg)-1)
#endif /*HAVE_GETWD*/

#ifdef DEBUGG
#define DFPRINTF2(a,b) fprintf(a,b)
#define DFPRINTF3(a,b,c) fprintf(a,b,c)
#else
#define DFPRINTF2(a,b)
#define DFPRINTF3(a,b,c)
#endif

extern int errno;

static char grade = 'q';	/* Priority of this job */
static int vflag;
static char *deleteerr;

/* Make sure all this matches stuff in queued (formerly batchd) too.  */
static char *spooldir = SPOOLDIR;
static char *pidfile = PIDFILE;
static char errstr[STRSIZ];	/* Buffer for SPRINTF error messages */
static int debug = 0;		/* turn on for debugging output */

/*WGK: These are the global variables added for my doqueue/wakeup,
  etc, suite.*/


char ttyinput = 1,ttyoutput = 1;
struct termios globalterm;
struct termios myterm;

int fdsocket2, fdsocket3;

/*WGK: End added doqueue/wakeup suite.*/


FILE *queue, *file2;

extern int	errno;

/*User options for options initalization in main()*/
int donthang = 0;
char *prefhost = NULL;
char *onlyhost = NULL;
char dir = 'q';


int	keepalive = 1;		/* flag for SO_KEEPALIVE socket option */
int	one = 1;		/* used for setsockopt() and ioctl() */
int	zero = 0;		/* used for setsockopt() and ioctl() */

char *name, *name1;
char path[MAX_PATH_SIZE];
int aid, uid, gid, euid, egid, pid, fd, fd2;
long curtime;
gid_t *gidset;
int ngroups;
struct flock mylock;

/*End kludge global variables*/

/* prototypes */
void syserror (char *s);
int validhost (char *hostcpy, int socket);
void Qshusage (void);
void Usage (void);
void error (char *s);
void cleanup(void);

/*WGK: The following is for do_queue part of the system.*/
void 
send_signal(int signal) 
{
  char sig;
  /*Sig pipe means queued has died.*/
  if(signal == 13) exit(0);
  sig = (char) signal;
  /*printf("Send: %d\n", sig);*/
  if (signal == SIGWINCH) {
    /*send new window size as well.*/
    /*send(fdsocket2, &sig, sizeof(sig), 0);*/
#ifdef TIOCSWINSZ
    int ttyfd;
    /*We want to kep queued simple and we are doing non-blocking I/O
      on the other end. This means we need to send both the signal and
      the window size information as a single packet. The following
      hacked structure is guaranteed to be at least as large as a
      memory structure containing both the signal (as char) and the
      window sie, although it might not be aligned properly as a
      packet.*/
    struct q_winsize {
      char signal;
      struct winsize ws;
    } mq_winsize;
    struct winsize *ws = (struct winsize*) (((char*) &mq_winsize) + 1);
    /* Set tty's idea of window size */
    ttyfd = 0;
    *((char*) &mq_winsize) = sig;
    if (ttyoutput) ttyfd = 1;
    if(ttyinput||ttyoutput) {
      ioctl (ttyfd, TIOCGWINSZ, (char *)ws);
      /*  printf ("New size: %d %d\n", ws->ws_row, ws->ws_col);*/
      send(fdsocket2, &mq_winsize, sizeof(sig)+sizeof(struct winsize), 0);
    }
#endif
  }
  /*Just send the signal.*/
  else send(fdsocket2, &sig, sizeof(sig), 0);

  if ((signal == SIGCONT)&&(ttyinput)) {
    /*Need to do something more aggressive than this to solve for
      bg/fg bug.*/
#ifdef TCSANOW
    /*IRIX*/
    tcsetattr(0, TCSANOW, &myterm);
#else
    tcsetattr(0, 0, &myterm);
#endif
  }
}

/* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
/* This section (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

/* The # of licenses that we got on the command line. */
int license_counter = 0;

/* Structure used to store a license name. */
struct license
{
	char name[MAXLICENSELEN];
};

#endif /*QUEUE_MANAGER*/
/* End section copyrighted by Texas Instruments.*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

int
main(int argc, char **argv, char **arge)
{
  /* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
  /* This section (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

  /* packet to send over to the queue manager */
  struct info_packet packet;

  /* the current directory */
  char cur_dir[MAXLOGFILE];

  /* variable that specifies whether we have to connect to the queue manager or not
   * (by default, we have to connect to it) */
  int qmanager_connect = 1;

  /* variables used within case 'a' for licenses */
  int l_index;
  struct license *l_array = NULL, *tl_array = NULL;

  /* variable bit used to let us know if there is a log file or not
   * (used within case 'e'); 0 means no file and 1 means there is a file */
  int logfile = 0;

  #endif /*QUEUE_MANAGER*/
/* End section copyrighted by Texas Instruments.*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

  /*WGK: Some of these may be vestiges.*/
  register int i;
  register char **ep;
  char *jobname = "";
  char *userid = "";
  char *mailuserid = "";
  char *queuename = "now";
  int nocshrc = 0;		
  char fname[MAXPATHLEN];
  char tfname[MAXPATHLEN];
  char cfname[MAXPATHLEN];
  char cookie[MAXPATHLEN];
  long spoolnumber;
  char *shell;
  FILE *tf;
  char cwdirectory[MAXPATHLEN];
  struct stat sbuf;
  char *command = NULL;
  extern int optind;
  extern char *optarg;
  char *optionstring;
  char *invoke;

#ifdef NO_ROOT
  struct sockaddr_in hello;
#endif

  /* Long getopt options.*/
  static const struct option longopts[] =
  {
    {"username", required_argument, NULL, 'l'},
    {"host", required_argument, NULL, 'h'},
    {"robust-host", required_argument, NULL, 'H'},
    {"spooldir", required_argument, NULL, 'j'},
    {"queuedir", required_argument, NULL, 'd'},
    {"immediate", no_argument, NULL, 'i'},
    {"queue", no_argument, NULL, 'q'},
    {"half-pty", no_argument, NULL, 'o'},
    {"full-pty", no_argument, NULL, 'p'},
    {"no-pty", no_argument, NULL, 'n'},
    {"wait", no_argument, NULL, 'w'},
    {"batch", no_argument, NULL, 'r'},
    {"version", no_argument, NULL, 'v'},
    { "help", no_argument, NULL, 1 },
    { NULL, 0, NULL, 0 }
  };

#ifdef MODIFYPRIORITY
  int njobs;
#endif

  uid = getuid();

#ifndef DEBUG
#ifndef NO_ROOT
  if(geteuid() != 0) {
    syserror1("Effective uid is %d, not root (0)\n",
	      geteuid());
    cleanup();
  }
#else
  if(geteuid() == 0) {
    syserror1("Effective uid is %d = root, but GNU Queue was compiled with NO_ROOT set.\n GNU Queue is insecure running as root with NO_ROOT set.\n If you have root privileges, recompile Queue without NO_ROOT and install as a system application.\n Re-run ./configure with the --enable-root option to do this.\n See the manual for additional information. ", geteuid());
    cleanup();
  }
#endif /*NO_ROOT*/
#endif /*DEBUG*/

  /* DVL : Open hosts file and read hosts */

/* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
/* This section (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

/* set DISPLAY if "DISPLAY=:0" */
{
	char display_cmd[50] = "DISPLAY=";
	char hostname[30];
	char display[30];
	char *dgetenv;
	int set_display = 0;

	strcpy(hostname, getenv("HOSTNAME"));
	dgetenv = getenv("DISPLAY");

	/* if DISPLAY is not blank */
	if ( dgetenv != NULL )
	{
	   strcpy(display, dgetenv);
	   if ( display[0] == ':' )  /* if DISPLAY is something like :0 */
	   {
	      strcat(display_cmd, hostname);
	      strcat(display_cmd, display);
	      strcat(display_cmd, ".0");
	      set_display = 1;
	   }
	}

	/* need to set display? */
	if ( set_display )
		putenv(display_cmd);
}

#endif /*QUEUE_MANAGER*/
  /* End of code copyrighted by Texas Instruments.*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

  /*
   * Parse command line.
   */

  /* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
#ifdef QUEUE_MANAGER
  optionstring = "h:H:d:opnqj:iwrva:ce:1";
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/
#else
 optionstring = "h:H:d:opnqj:iwrv";
#endif /*QUEUE_MANAGER*/

  if(invoke = rindex(argv[0], '/')) invoke++;
  else invoke = argv[0];

  if(!strcmp(invoke, "qsh")) {

#ifdef QUEUE_MANAGER
    /* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
    optionstring = "+l:d:opnj:iwrva:ce:1";
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/
#else
    optionstring = "+l:d:opnj:iwrv";
#endif /*QUEUE_MANAGER*/


    /*rsh compatibility mode defaults.*/
    ttyoutput = 0;
    ttyinput = 0;
    queuename="now";
    donthang = 0;
  }

/* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
/* This section (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

/* initialize the packet structure to some default values */
strcpy(packet.mode, "interactive");
strcpy(packet.priority, "low");
strcpy(packet.prefhost, "");
strcpy(packet.onlyhost, "");

/* get the user name */
{
   char *user = getenv("USER");
   strcpy(packet.user, user);
}

/* get the real user id */
packet.user_id = getuid();

/* get the real user group id */
packet.gid = getgid();

/* get the date */
{
   int i, n, pid, pfd[2];
   char buffer[30];

   if ( pipe(pfd) == -1 )
   {
	   fprintf(stderr, "Queue.c Error: can't pipe() to get the date\n");
	   return -1;
   }

   if ( (pid = fork()) == 0 )
   {
	   /* child process */
	   close(pfd[0]);
	   close(1);
	   dup(pfd[1]);
	   execlp("date", "date", NULL);
   }
   else if ( pid == -1 )
   {
	   fprintf(stderr, "Queue.c Error: can't fork() off process to get the date\n");
	   return -1;
   }

   /* parent process */
   close(pfd[1]);

   /* read from the pipe */
   if ( (n = read(pfd[0], buffer, sizeof(buffer))) < 0 )
   {
	   fprintf(stderr, "Queue.c Error: can't read from pipe to get the date\n");
	   return -1;
   }
   close(pfd[0]);

   /* wait to bury child process */
   wait(NULL);

   buffer[n] = '\0';

   /* remove the newline character from buffer */
   for (i = 0; i < sizeof(buffer); i++)
      if (buffer[i] == '\n')
	   buffer[i] = '\0';

   strcpy(packet.datesubmit, buffer);
}

/* get the current working directory */
getcwd(cur_dir, MAXLOGFILE);
strcpy(packet.cur_dir, cur_dir);

#endif /*QUEUE_MANAGER*/
/*End of code copyrighted by Texas Instruments.*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

  while ((i = getopt_long(argc, argv, optionstring, longopts, NULL)) != EOF)

    switch (i) {
    case 'l': {
      /*ignored for qsh */
      break;
    }
    case 'h': {
      if (!optarg || !*optarg) break;
      if (!strcmp(optarg, "+")) break; /* '+' is wildcard; ignore.*/
      if (strlen(optarg) > MAXHOSTNAMELEN) {
	fprintf(stderr, "Queue: hostname too long.\n");
	exit(1);
      }

      /* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
      /* This section (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER
      strcpy(packet.prefhost, optarg);
      strcpy(packet.onlyhost, optarg);

      /* lowercase the host names */
      {
      int j;
      for (j = 0; j < strlen(packet.prefhost); j++)
	      packet.prefhost[j] = tolower(packet.prefhost[j]);

      for (j = 0; j < strlen(packet.onlyhost); j++)
	      packet.onlyhost[j] = tolower(packet.onlyhost[j]);
      }

      #endif /*QUEUE_MANAGER*/
      /*End of code copyrighted by Texas Instruments.*/
      /* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

      prefhost = strdup(canonicalhost(optarg));
      if(!validhost(prefhost, 0)) {
	fprintf(stderr, 
		"Queue: invalid queue hostname: %s\n", prefhost); 
	exit(1);
      }
      onlyhost = prefhost;
      break;}
    case 'H': {
      if (!strcmp(optarg, "+")) break; /* '+' is wildcard; ignore.*/
      if (strlen(optarg) > MAXHOSTNAMELEN) {
	fprintf(stderr, "Queue: hostname too long.\n");
	exit(1);
      }

      /* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
      /* This section (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER
      strcpy(packet.prefhost, optarg);

      /* lowercase the host name */
      {
      int j;
      for (j = 0; j < strlen(packet.prefhost); j++)
	      packet.prefhost[j] = tolower(packet.prefhost[j]);
      }

      #endif /*QUEUE_MANAGER*/
      /*End of code copyrighted by Texas Instruments.*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

      prefhost = strdup(canonicalhost(optarg));
      if(!validhost(prefhost, 0)) {
	fprintf(stderr, "Queue: invalid queue hostname: %s\n", prefhost); 
	exit(1);
      }
      prefhost=prefhost;
      onlyhost = NULL;
      break;}
    case 'd': {
      if (strlen(optarg) > 254) {
	fprintf(stderr, "Queue: queuename too long.\n");
	exit(1);
      }
      queuename = optarg;
      break;}
    case 'o': {
      ttyoutput = 0;
      ttyinput = 1;
      break;}
    case 'p': {
      ttyoutput = 1;
      ttyinput = 1;
      break;
    }
    case 'n': {
      ttyoutput = 0;
      ttyinput = 0;
      break;
    }
    case 'j': {
      if (strlen(optarg) > 254) {
	fprintf(stderr, "Queue: jobname too long.\n");
	exit(1);
      }
      jobname = optarg;
      break;}
    case 'i': {
      queuename="now";
      break;
    }
    case 'q': {
      queuename="wait";
      break;
    }
    case 'w': {
      donthang = 0;
      break;
    }
    case 'r': {  /* user requests batch mode */
      /* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
#ifdef QUEUE_MANAGER
      strcpy(packet.mode, "batch");  
#else
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/
      /*2000/10/22 WGK allow donthang to be set properly if QUEUE_MANAGER is compiled out.*/
      donthang = 1;
#endif /*QUEUE_MANAGER*/


      break;
    }
    case 'v':
      {
	fprintf(stderr, "GNU Queue version %s\n Copyright 1998-2000 W. G. Krebs <wkrebs@gnu.org>. All rights reserved.\n", VERSION);
	fprintf(stderr, "GNU Queue comes with absolutely no warranty; see file\nCOPYING for details.\n\n");
	fprintf(stderr, "Bug reports to bug-queue@gnu.org.\n");
	exit(0);
      }
      /* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
      /* This section (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER
case 'a':  /* to get the license names */
{
/* increment the number of licenses */
license_counter++;

/* if l_array is not NULL, need to deallocate the memory */
if ( l_array != NULL )
	free( (void *) l_array );

/* dynamically allocate l_array to hold all the license names */
l_array = (struct license *)calloc(license_counter, sizeof(struct license));

/* copy the contents of tl_array to l_array (if license_counter != 1),
 * along with the new license name */
if ( license_counter != 1 )
{
   for (l_index = 0; l_index < license_counter - 1; l_index++)
	   l_array[l_index] = tl_array[l_index];
}

for(l_index = 0; l_index < strlen(optarg); l_index++)
	l_array[license_counter - 1].name[l_index] = optarg[l_index];

l_array[license_counter - 1].name[l_index] = '\0';

/* now dynamically allocate tl_array, copy everything from l_array to
 * tl_array */
tl_array = (struct license *)calloc(license_counter, sizeof(struct license));
for (l_index = 0; l_index < license_counter; l_index++)
	tl_array[l_index] = l_array[l_index];

break;
}
case 'c':  /* user requests high priority */
{
	strcpy(packet.priority, "high");
	break;
}
case 'e':  /* user specifies log file (only used if in batch mode) */
{
	logfile = 1;
	strcpy(packet.logfile, optarg);
	break;
}
case '1':  /* this is a hack, so that we can specify not to go to the
	      queue manager; this hack is probably not such a good idea because
	      ordinary users can discover this option, but it will have to do for now */
{
	qmanager_connect = 0;
	break;
}
#endif /*QUEUE_MANAGER*/
/* End of code copyrighted by Texas Instruments.*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

    default:
      if(strcmp(invoke, "qsh")==0) Qshusage(); 
      Usage();
    }

/* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
/* This section (C) 2000 Texas Instruments, Inc. All Rights Reserved. Released under GPL.*/
#ifdef QUEUE_MANAGER

/* get the job command */
{
   int i;
   strcpy(packet.job, argv[optind]);
   for(i = optind + 1; i < argc; i++)
	   sprintf(packet.job, "%s %s", packet.job, argv[i]);

   /* parse the command to see if there are any '|'s in there ('|'s do not work in Queue software) */
   for (i = 0; i < sizeof(packet.job); i++)
      if (packet.job[i] == '|')
      {
	 fprintf(stderr, "Queue.c Error: no |'s allowed in Queue software\n");
	 return -1;
      }
}

/* if tl_array is not NULL, need to deallocate its memory */
if ( tl_array != NULL )
	free( (void *) tl_array );

/* check to make sure that user specified license(s) */
if ( license_counter == 0 )
{
	fprintf(stderr, "Queue.c Error: must specify license(s)\n");
	return -1;
}
else
{
	/* lowercase all the license(s) */
	int i, j;
	for (i = 0; i < license_counter; i++)
	{
	   for (j = 0; j < strlen(l_array[i].name); j++)
		   (l_array[i].name)[j] = tolower( (l_array[i].name)[j] );
	}
}

/* if user specified batch mode, check to make sure that we have a log file */
if ( !strcasecmp(packet.mode, "batch") )
{
   if ( logfile != 1 )  /* means, there's no log file */
   {
	   fprintf(stderr, "Queue.c Error: in batch mode, must specify log file\n");
	   return -1;
   }
   else  /* determine the full path of the log file */
   {
      /* if user did not specify an absolute path or if user did not specify
       * tilde (shell automically replaces '~' with the home directory) */
      if (packet.logfile[0] != '/')
      {
	 /* if the user specified "./", then need to get the current directory */
	 if ( packet.logfile[0] == '.' )
	 {
	    int i, j;
	    char logfile[MAXLOGFILE];

	    /* delete '.' from the logfile */
	    for ( i = 0, j = 1; j < strlen(packet.logfile); i++, j++ )
		    logfile[i] = packet.logfile[j];
	    logfile[i] = '\0';

	    /* concatenate the current directory with the logfile */
	    strcat(cur_dir, logfile);
	    strcpy(packet.logfile, cur_dir);
	 }
	 /* else, the user specified a relative path, need to get current directory */
	 else
	 {
	    /* concatenate the current directory with the logfile */
	    strcat(cur_dir, "/");
	    strcat(cur_dir, packet.logfile);
	    strcpy(packet.logfile, cur_dir);
	 }
      }

      /* MY_DEBUG */
      printf("LOGFILE: %s\n", packet.logfile);
   }
}

/* if user specified interactive mode, make sure that job is high priority */ 
if ( !strcasecmp(packet.mode, "interactive") )
	strcpy(packet.priority, "high");

/* Now make a connection to the queue_manager and send all the information to it. */
if ( qmanager_connect )
{
   char buffer[MAXHOSTNAMELEN];
   /* this is the assigned host that we get back from the queue manager */
   char assigned_host[MAXHOSTNAMELEN];

   /* socket variables */
   int sockfd;
   struct sockaddr_in serv_addr;
   struct hostent *host;
   int sendbuff = 16384;

   /* counter to keep track of the number of times we try connecting to the q_manager */
   int counter = 1;

   strcpy(assigned_host, ""); /* initialize assigned_host */
   strcpy(buffer, ""); /* initialize buffer */

   /* Get the IP address of the host name */
   if ( (host = gethostbyname(QMANAGERHOST)) == NULL )
   {
	   fprintf(stderr, "Queue.c Error: invalid host name of the queue manager\n");
	   return -1;
   }

   /* Fill in the server information so that we can connect to it */
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_port = htons(PORTNUM2);
   serv_addr.sin_addr = *((struct in_addr *)host->h_addr);

   /* If in interactive mode, we will try to connect ten times to prevent an
    * overloaded system from  keeping us down.  If in batch mode, we will try
    * connecting forever until a connection is successful. */

   for(;;)
   {
      /* Open a TCP socket */
      if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
      {
	      fprintf(stderr, "Queue.c Error: can't get TCP socket\n");
	      return -1;
      }

      /* Set socket options for TCP socket */
      if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &sendbuff,
			      sizeof(sendbuff)) < 0 )
      {
	      fprintf(stderr, "Queue.c Error: can't set TCP setsock options\n");
	      return -1;
      }

      if ( connect(sockfd, (struct sockaddr *) &serv_addr,
			      sizeof(serv_addr)) < 0 )
      {
	      printf("Connecting to Queue_Manager...\n");
	      sleep(3); /* sleep a few seconds before trying to connect again */
	      close(sockfd);  /* close off the socket */

	      /* interactive mode */
	      if ( !strcmp(packet.mode, "interactive") )
	      {
		      ++counter;

		      /* Is queue manager down? */
		      if ( counter == 11 )
		      {
			      fprintf(stderr, "Queue.c Error: Queue_Manager is either down or is very busy.  Try again later.\n");
			      return -1;
		      }
	      }
      }
      else
	break;
   }

   /* First, send the packet structure. */
   if ( send(sockfd, &packet, sizeof(packet), 0) < 0 )
   {
	   fprintf(stderr, "Queue.c Error: can't send packet to queue manager\n");
	   return -1;
   }

   /* Next, send the license(s). */
   for (l_index = 0; l_index < license_counter; l_index++)
   {
	   if ( send(sockfd, l_array[l_index].name, MAXLICENSELEN, 0) < 0 )
	   {
		   fprintf(stderr, "Queue.c Error: can't send license name(s) to queue manager\n");
		   return -1;
	   }
   }

   /* Send an "EOF" message to let queue manager know that this is the end
    * of the transaction. */
   strcpy(l_array[0].name, "EOF");
   if ( send(sockfd, l_array[0].name, MAXLICENSELEN, 0) < 0 )
   {
	   fprintf(stderr, "Queue.c Error: can't send EOF message to queue_manager\n");
	   return -1;
   }

   /* Finally, if we're in batch mode, need to send the environment to the queue_manager. */
   if ( !strcasecmp(packet.mode, "batch") )
   {
	   /* send string to the queue_manager one at a time (most simplistic method) */
	   char data[MAXSTRINGLEN];
	   struct timeval timeout;
	   char ** arge_index = arge;

	   while ( *arge_index != 0 )
	   {
	      strcpy(data, *arge_index);
	      if ( send(sockfd, data, MAXSTRINGLEN, 0) < 0 )
	      {
		      fprintf(stderr, "Queue.c Error: can't send environment to queue_manager\n");
		      return -1;
	      }

	      /* sleep for 50 microseconds before sending the next string */
	      timeout.tv_sec = 0;
	      timeout.tv_usec = 50;
	      if ( select(0, NULL, NULL, NULL, &timeout) < 0 )
		      fprintf(stderr, "Queue.c Error: can't set timer to send the environment string\n");

	      ++arge_index;
	   }

	   /* Send an "EOF" message to let queue manager know that this is the end
	    * of the transaction. */
	   strcpy(data, "EOF");
	   if ( send(sockfd, data, MAXSTRINGLEN, 0) < 0 )
	   {
		   fprintf(stderr, "Queue.c Error: can't send EOF message to queue_manager\n");
		   return -1;
	   }

	   /* MY_DEBUG */
	   printf("After sending environment to qmanager\n");
   }

   /* if in batch mode, just wait for an ack (job_id) from queue_manager and exit */
   if ( !strcasecmp(packet.mode, "batch") )
   {
	   if ( recv(sockfd, buffer, sizeof(buffer), 0) < 0 )
	   {
		   fprintf(stderr, "Queue.c Error: can't receive ack from queue_manager\n");
		   return -1;
	   }

	   /* did we get an error message? */
	   if ( !strcmp(buffer, "-1") )
	   {
		   fprintf(stderr, "Queue.c Error: user entered either invalid licenses or hosts\n");
		   return -1;
	   }
	   else  /* print out the job_id */
		   printf("JOB_ID: %s\n", buffer);

	   return 0;
   }

   /* now wait for an assigned host */
   if ( recv(sockfd, assigned_host, sizeof(assigned_host), 0) < 0 )
   {
	   fprintf(stderr, "Queue.c Error: can't receive assigned host name\n");
	   return -1;
   }

   /* did we get an error message? */
   if ( !strcmp(assigned_host, "-1") )
   {
	   fprintf(stderr, "Queue.c Error: user entered either invalid licenses or hosts\n");
	   return -1;
   }

   /* wait for the job id */
   if ( recv(sockfd, buffer, sizeof(buffer), 0) < 0 )
   {
	   fprintf(stderr, "Queue.c Error: can't receive job id\n");
	   return -1;
   }

   /* close off the connection */
   close(sockfd);

   /* now assign prefhost and onlyhost to this assigned host */
   prefhost = strdup(canonicalhost(assigned_host));
   onlyhost = prefhost;

   /* MY_DEBUG */
   printf("ASSIGNED_HOST: %s\n", onlyhost);
   printf("JOB_ID: %s\n", buffer);

   /* if we didn't get an assigned host, we should exit */
   if ( !strcmp(assigned_host, "") )
   {
	   fprintf(stderr, "Queue.c Error: did not get an assigned host\n");
	   return -1;
   }

} /* end of block */

/* if l_array is not NULL, need to deallocate its memory */
if ( l_array != NULL )
	free( (void *) l_array );

#endif /*QUEUE_MANAGER*/
/* End of code copyrighted by Texas Instruments.*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

  /*WGK 98/12/24 If we're running 'qsh', next thing is the hostname,
    not the command line. This is then followed by the command line;
    no '--' is used with qsh.*/

  if(strcmp(invoke, "qsh")==0) {
    if (optind >= argc) {Qshusage(); exit(1);}
    if (!strcmp(argv[optind], "+")) optind++;
    else{
      if (strlen(argv[optind]) > 1000) {
	fprintf(stderr, "Queue: hostname too long.\n");
	exit(1);
      }
      prefhost = strdup(canonicalhost(argv[optind++]));
      if(!validhost(prefhost, 0)) {Qshusage(); exit(1);}
      onlyhost = prefhost;
    }
  }

  if (optind >= argc) {Usage(); exit(1);}

  if(!Hosts) {
    if(ReadHosts(QHOSTSFILE) < 0)
      {
	fprintf(stderr, "Can not read queue hosts file %s\n", QHOSTSFILE);
	exit(1);
      }
  }

  readcookie();

  if( jobname == NULL ){
    for( i = optind; i < argc ; i++) {
      if(argv[i][0] == '-')	continue;
      jobname = argv[i];
      if (strlen(jobname) > 254) {
	fprintf(stderr, "Queue: Jobname too long.\n");
	exit(1);
      }
    }
  }


  /*
   *  The grade is a-z and only SU can set it to values lower than 'h'.
   */
  if(grade < 'a'  ||  grade > 'z'  ||  (grade < 'h')  &&  uid) {
    error1("Grade `%c' is invalid.\n", grade);
    cleanup();
  }

  /*
   * Have to check for queue names like:  .  ..  ../../..
   */

  if( index(queuename, '/') != NULL
      || strcmp(queuename, ".") == 0 || strcmp(queuename, "..") == 0 ){
    error1("'%s': Invalid queue name\n", queuename);
    Usage();
  }
  if( strlen(queuename) >= DIRSIZ_Q ){
    error2("'%s': Queue name longer than %d characters\n",
	   queuename, DIRSIZ_Q-1);
    Usage();
  }

#ifdef MODIFYPRIORITY

  /*WGK Old batch code: Around here people tend to submit a lot jobs
    at one time which can cause the queue to become clogged.  So we
    count the number of currently queued, uncanceled jobs and adjust
    the priority based based on this information.  Note that really
    low priority jobs can be starved by the batchd so you may not want
    to do this. GRS */
  njobs = CountJobs(fname);
  grade += njobs;
  if(grade > 'z')
    grade = 'z';
#endif

  {
    static char sigs[] = { SIGINT, SIGHUP, SIGTERM, SIGQUIT };

    for (i = 0; i < sizeof sigs/sizeof sigs[0]; i++)
      if (signal(sigs[i], SIG_IGN) != SIG_IGN)
	signal(sigs[i], cleanup);
  }


  /*
   * Set up a $USER mailuserid for ROOT if one isn't given.
   */
  if( uid == 0 && mailuserid == NULL
      && (mailuserid = getenv("USER")) != NULL ){
    if( getpwnam(mailuserid) == NULL ){
      error1("Can't find $USER='%s' in passwd file\n",
	     mailuserid);
      cleanup();
    }
  }

  /*WGK : This is where my old doqueue/queue routine started.*/
  {

    char *cur, *cwd, c;
    int i;
    int s, s2, newsocket, timo, len;
    long oldmask;
    struct rlimit myrlimit;
    struct hostent *myhost;
    int			ch, addrlength;
    struct linger		linger;
    int prio;
    mode_t mask;
    int null;
    int estatus = 2;
#ifdef ENABLE_CHECKPOINT
    int checkpointed = 0;
#endif


    struct sockaddr_in	address1;
    int fdpipe[2], childpid, maxfdp1, cint, oursecport;
    fd_set ready, readf, urg, urgfrom;
    short secondport;
    char *cp, *hostname;
    char servuname[16], cliuname[16];
    char remotehost[2 * MAXHOSTNAMELEN + 1];
    char buf[BUFSIZ], sigval;
    struct passwd *pwd;
    struct hostent *hostp;

    char val=0;

#ifndef NO_ROOT
    int miport = IPPORT_RESERVED - 1;
#else
    int miport = 32700;
#endif

    /*sigsetmask(~sigmask(SIGALRM));*/
    /*oldmask = sigblock(sigmask(SIGURG));*/
    timo = 1;

#ifndef NO_ROOT
    if (donthang==0) s = rresvport(&miport);
#else
    if (donthang==0) {
      s = socket(AF_INET, SOCK_STREAM, 0);

      {
	hello.sin_family=AF_INET;
	hello.sin_addr.s_addr = htonl(INADDR_ANY);
      portagain:
	hello.sin_port = htons(miport);
	if(bind(s, &hello, sizeof(hello))<0) {
	  if (miport >0) {
	    miport--;
	    goto portagain;
	  }
	  perror("queue: bind");
	  exit(2);
	}               

      }
    }
#endif
    /*sigblock(oldmask);*/

    if((cwd = (char*) getcwd(path, MAX_PATH_SIZE))==0) {
      perror("queue: getcwd");
      exit(2);
    }


#if defined(HAVE_SYS_AUDIT_H) && defined(__hpux)
    aid = getaudid(); /*HP-UX audit trail user id.*/
#else
    aid = getuid(); /*Basically ignored on other systems.*/
#endif
    euid = geteuid();
    egid = getegid();
    uid = getuid();
    gid = getgid();
    /*Directory is potentially user accessible, so give permissions to
      root only. GID stores uid of user. This trick is used by
      CountJobs() below, and also potentially in the cancel program.*/

    /*Wake up the sleeping daemon at some point.*/

    if(prefhost == NULL) fd = wakeup(queuename, Q_STARTNEW, Q_CONTACT,NULL);
    else {
      /*WGK 95/7/7: Important, bugfix: We must call getrldavg to get
	around mtime granularity problem in queued, since getrldavg
	informs queued to check queuename more frequently than mtime
	might otherwise indicate.  If this is not done host selection
	is very unreliable.*/

      getrldavg(prefhost, queuename);
      fd = con_daemon(prefhost, queuename, Q_STARTNEW);

      /*If -h option was used and we couldn't open a socket to that
	host, send it to any host on the network.*/
      if((fd<0) && (!onlyhost)) fd = wakeup(queuename, Q_STARTNEW, Q_CONTACT,NULL);
    }

    if(fd<0) {
      fprintf(stderr, "Queue: Couldn't connect with a queueD.\n");
      exit(2);
    }

    queue = fdopen(fd, "w");

    /*First four bytes give length of incoming file followed by
      filename.  If length is minus1, this is a CFfile, and the
      filename is unnecessary.*/

    {
      int minusone = -1;
      netfwrite(&minusone, sizeof(int), 1, queue);
    }

    /*Per draft protocol, start of job control file is the version
      control string. This is the VERSION1 job control file.*/

    fputs("VERSION1",queue);
    fputc('\0', queue);
    /*UID, userid, mailuserid, and jobname come first because of
      jobinfo(). */

    netfwrite(&uid, sizeof(int), 1, queue);
    fputs(userid, queue);
    fputc('\0', queue);
    fputs(mailuserid, queue);
    fputc('\0', queue);
    fputs(jobname, queue);
    fputc('\0', queue);
    if (onlyhost && *onlyhost) {fputs(onlyhost, queue);}
    else {fputs(" ", queue);}
    fputc('\0', queue);
    netfwrite(&donthang, sizeof(int), 1, queue);

    /*WGK Set the cookie here. We basically just combine the tfname,
      which contains the time, with a psuedo-random number. It's not
      great, but without read access to the spool dir it is hard to
      guess, and NO_ROOT users really concerned about security should
      use HAVE_IDENTD. The main purpose of the cookie is to prevent
      the wrong daemon from connecting.*/

    {
      /*Convert jobname into an integer.*/
      char job[4];
      int i;
      if(strlen(jobname)<4) {
	for(i=0;i<4;++i) job[i] = 0;
	for(i=0;i<strlen(jobname);++i) job[i] = jobname[i];
      }
      else for(i=0;i<4;++i) job[i] = jobname[i];

      srandom( (unsigned int) (unique() + uid + *((int*) (&job))));
    }
    sprintf(cookie, "%ld%ld%d", random(), spoolnumber, uid);

    fputs(cookie, queue);
    fputc('\0', queue);

    gethostname(buf, sizeof(buf));
    if(!(myhost = gethostbyname(buf))) {
      /*WGK 98/12/19 prevent seg fault if DNS goes down.*/
      setuid(getuid()); /*Drop privileges*/
      fprintf(stderr, 
	      "queue: gethostbyname failed; can't get my IP address.\n");
      herror("queue");
      fprintf(stderr, "queue: DNS is probably down and resolv+ isn't configured to get the IP address from /etc/hosts.\n");
      exit(2);
    }

    /* EJD -- 8/14/2000 -- host address is already network order - Don't swap */
     fwrite(myhost->h_addr, sizeof(address1.sin_addr), 1, queue);
    netfwrite(&miport, sizeof(int), 1, queue);

    {int temp;
    temp = argc - optind;
    netfwrite(&temp, sizeof(int), 1, queue);
    for(i=optind;i<argc;++i) {
      fputs(argv[i], queue);
      fputc(0, queue);
    }
    }

    cur = cwd;
    do {fputc(*cur, queue);} while (*cur++!=0);
    if((ngroups = getgroups(0, NULL))<0) {
      perror("getgroups1");
      exit(2);
    }
    if((gidset = (gid_t*) malloc(ngroups*sizeof(gid_t)))==0) {
      fprintf(stderr, "Out of memory.\n");
      exit(2);
    }

    if((ngroups = getgroups(ngroups, gidset))<0) {
      perror("getgroups2");
      exit(2);
    }
    netfwrite(&aid, sizeof(int), 1, queue);
    netfwrite(&euid, sizeof(int), 1, queue);
    netfwrite(&egid, sizeof(int), 1, queue);
    netfwrite(&gid, sizeof(int), 1, queue);
    netfwrite(&ngroups, sizeof(int), 1, queue);
    netfwrite(gidset, sizeof(gid_t), ngroups, queue);
    free(gidset);
    {
      int temp;
      char ttyerror = 0;
#ifdef TIOCSWINSZ
      struct winsize ws;
#endif
      ttyoutput = 0;
      if (ttyinput) ttyinput = (char) isatty(0);
      netfwrite(&ttyinput, sizeof(ttyinput), 1, queue);
      if (ttyinput) ttyoutput= (char) isatty(1);
      netfwrite(&ttyoutput, sizeof(ttyoutput), 1, queue);
      if (ttyoutput) ttyerror= (char) isatty(2);
      netfwrite(&ttyerror, sizeof(ttyoutput), 1, queue);
      if((ttyinput)||(ttyoutput)) {
	if((!ttyinput)&&(ttyinput)) temp = 1;
	else temp = 0;
	tcgetattr(temp, &globalterm);
	/* Set tty's idea of window size */
#ifdef TIOCSWINSZ
        ioctl (temp, TIOCGWINSZ, (char *)&ws);
#endif

      }
      netfwrite(&globalterm, sizeof(globalterm), 1, queue);
#ifdef TIOCSWINSZ
      netfwrite(&ws, sizeof(ws), 1, queue);
#endif
    }
    while ((cur = *arge)!=0) {
      do {fputc(*cur, queue);} while (*cur++!=0);
      arge++;
    }
    fputc(0, queue);
    mask=umask(0);
    umask(mask);
    netfwrite(&mask, sizeof(mode_t), 1, queue);
    prio = getpriority(PRIO_PROCESS, 0);
    netfwrite(&prio, sizeof(int), 1, queue);
    for(i=0;i<8;++i) {
      getrlimit(i, &myrlimit);
      netfwrite(&myrlimit, sizeof(struct rlimit), 1, queue);
    }

    fflush(queue);
    fclose(queue);

    /*We enter the critical zone in which the daemon may try to
      contact us.*/

    /*We need to listen for more than one daemon because the wrong
      daemon could try to connect.*/

    if(donthang==0) listen(s, 10);
    else exit(0);

#ifdef ENABLE_CHECKPOINT
  checkpoint_restart:
    /*WGK: I'll bet this has bugs, we probably haven't perfectly
      re-initalized anything.*/

    /*Bring back privileges we previously saved.*/

    setuid(0);

#endif


  wrong_daemon:

    {int mylen;
    addrlength = sizeof(address1);
    if((newsocket = accept(s, &address1, &addrlength))< 1) {
      perror("Accept from queued");
      exit(2);
    }
    }

    /*WGK We want to set SO_KEEPALIVE and SO_LINGER. SO_KEEPALIVE is
      needed to allow the daemons to detect if one or the other hosts
      has gone down; SO_LINGER allows proper cleanup.*/

    if (keepalive &&
	setsockopt(newsocket, SOL_SOCKET, SO_KEEPALIVE, (char *) &one,
		   sizeof(one)) < 0)
      perror("setsockopt(SO_KEEPALIVE)");

    linger.l_onoff = 1;
    linger.l_linger = 60;
    if (setsockopt(newsocket, SOL_SOCKET, SO_LINGER, (char *) &linger,
		   sizeof(linger)) < 0)
      perror("setsockopt(SO_LINGER)");

    /*This had better be an internet address.*/

    if (address1.sin_family != AF_INET) {
      fprintf(stderr, "Strangeness: non-internet from address\n");
      exit(1);
    }

#ifdef IP_OPTIONS
    {
      struct protoent	*ip;
      u_char		optionbuf[BUFSIZ/3], *optionptr;
      int		optionsize, prot;
      char		buffl[BUFSIZ], *miptr;



      if ( (ip = getprotobyname("ip")) != NULL)
	prot = ip->p_proto;
      else
	prot = IPPROTO_IP;

      optionsize = sizeof(optionbuf);
      if (getsockopt(newsocket, prot, IP_OPTIONS, 
		     (char *) optionbuf, &optionsize) == 0
	  && optionsize != 0) {
	/* We probably don't need this since we're checking to make
	   sure they're coming in on a reserved port anyway, but just
	   to be thorough I'm incuding it here. If we detect this, we
	   complain loudly to syslog().*/

	miptr = buffl;
	optionptr = optionbuf;
	for ( ; optionsize > 0; optionptr++, optionsize--, miptr += 3)
	  sprintf(miptr, " %2.2x", *optionptr);
	/* print each option byte in standard format.*/
	fprintf(stderr,
		"Strangeness: Connection received using IP options, but ignored (cracker?): %s", buffl);

	/* Turn off the options.  If this doesn't work, we quit.*/

	if (setsockopt(newsocket, prot, IP_OPTIONS,
		       (void *) NULL, (int) optionsize) != 0) {
	  fprintf(stderr, "setsockopt IP_OPTIONS NULL: %m");
	  exit(1);
	}
      }
    }
#endif

    /* Again, to prevent cracked versions from non-privileged users,
        we check that the server is comming in on a reserved port*/

    address1.sin_port = ntohs((u_short) address1.sin_port);
#ifndef NO_ROOT
    if (address1.sin_port >= IPPORT_RESERVED  ||
	address1.sin_port <  IPPORT_RESERVED/2) {
      fprintf(stderr, "Connection from %s on invalid port",
	      inet_ntoa(address1.sin_addr));
      exit(1);
    }
#else
    /*We aren't root so we don't use reserved ports. To substitute, we
      try to use ident information. This prevents people from spoofing
      queued.c */
#ifdef HAVE_IDENTD
    address1.sin_port = ntohs((u_short) address1.sin_port);
    if(!check_ident(&hello, &address1))  {
      fprintf(stderr, "Connection from %s returned bad identd information or identd timed out, and we are compiled with -DNO_ROOT and -DHAVE_IDENTD set.\n
Have your administrator fix identd problems on your cluster, OR recompile without
-DHAVE_IDENTD, OR increase identd timeout in ident.c OR install Queue as root.\n",
	      inet_ntoa(address1.sin_addr));
      exit(2);
    }
#endif /*HAVE_IDENTD*/
#endif /*NO_ROOT*/ 

	 /*#if defined(HAVE_IDENTD) || !defined(NO_ROOT) || defined(NO_SECURITY)*/
	 /*WGK 1998/12/25 get speed back if crypt() isn't needed because portsare privileged or verifiable.*/
	 /*#define ONE_WAY(a) ""*/
	 /*#else*/
	 /*Performance hit.*/
	 /*#define ONE_WAY(a) crypt(a, "aa");*/
	 /*#endif*/

	 /*WGK 2000/07/18 sha1 ([FIPS180-1], [ANSI930-2] & [ISOIEC10118-3])
 is a much faster one-way function
than crypt, so we can simplify the code here a bit, provided MD5 compiles
correctly on your machine (use the md5test -x testing option if in doubt.)
If not, you can always go back to the above code, which used slow UNIX
crypt().*/

	 #define ONE_WAY(a) sha1(a);
         #define randomstr(a) strcpy(a,"oscar");

    /*WGK 1998/12/24

      /*Confirm one-way trapped cookie and then write out clear
        cookie.*/
DFPRINTF2(stderr,"reading cnounce\n");
    /*WGK 2000/07/20 digest authentication cnounce/nounce scheme.*/

    {char cnounce[21], nounce[21], bigcookie[256];
    char *randomcookie;
    char c;
     int k;
    alarm(5);
    k = 0;
    do {
      read(newsocket, &cnounce[k], 1);
      k++;
    } while (cnounce[k-1] && k<21);
    alarm(0);
    cnounce[20] = 0;

DFPRINTF3(stderr,"cnounce %s\n",cnounce);

    /*OK, cnounce read, now generate nounce.*/
    randomstr(&nounce[0]);
    DFPRINTF3(stderr,"nounce %s\n",nounce);
    write(newsocket, &nounce, strlen(nounce)+1);
    
    strcpy(bigcookie,nounce);
    strcat(bigcookie,cookie);
    strcat(bigcookie,cnounce);
    DFPRINTF3(stderr,"bigcookie %s\n",bigcookie);
    {
      int flag = 0;
      char c;
      /*Crypt always returns the same number of characters, so we
	don't need to worry about the wrong daemon here.*/
      char *remotecookie;
      int k, j;
      remotecookie = ONE_WAY(bigcookie);
    DFPRINTF3(stderr,"remotecookie %s\nreceived: ",remotecookie);
      j =strlen(remotecookie)+1;
      for(k=0;k<j&&!flag;k++){
	alarm(5);
	read(newsocket, &c, 1);
	DFPRINTF3(stderr,"%c",c);
	if (c != remotecookie[k])  flag = 1;
      }
      alarm(0);
      if(flag)   {
	DFPRINTF2(stderr,"\nbad remotecookie\n");
	close(newsocket);
	goto wrong_daemon;
      }
      if(!flag)   DFPRINTF2(stderr,"\ngood remotecookie\n");
      /*OK, now we need to reverse the procedure.*/

      randomstr(&cnounce[0]);

      /*Send cnounce, as compliant with draft protocol. WGK 2000/07/20*/
      write(newsocket, &cnounce, strlen(cnounce)+1);
      DFPRINTF3(stderr,"wrote cnounce %s\n",cnounce);
      /*Read nounce, as compliant with draft protocol. WGK 2000/07/20*/
 
      alarm(5);
    k = 0;
    do {
      read(newsocket, &nounce[k], 1);
      k++;
    } while (nounce[k-1] && k<21);
      alarm(0);
      nounce[20] = 0;
      DFPRINTF3(stderr,"read nounce %s\n",nounce);
      /*Response to challenge is constructed from nounce, cookie, and cnounce.*/

      strcpy(bigcookie,nounce);
      strcat(bigcookie,cookie);
      strcat(bigcookie,cnounce);
      DFPRINTF3(stderr,"bigcookie %s\n ",bigcookie);
      randomcookie = ONE_WAY(bigcookie);
      DFPRINTF3(stderr,"remotecookie %s\n: ",remotecookie);
      /*Write the challenge response, thus proving via digest authentication we know the cookie without actually giving away what the cookie is. This concludes the mutual authentication as outlined in the draft protocol. WGK 07/20/2000 */

flag = 0;
      write(newsocket, &randomcookie[0], strlen(randomcookie)+1);
       /*Wait for the OK byte from other side indicating authentication has been successful.*/
  
      alarm(10);
      read(newsocket, &c, 1);
      alarm(0);
      if(c|flag) {
	/*We need to start waiting on this socket again.*/
	close(newsocket);
	goto wrong_daemon;
      }
    }
    }

    DFPRINTF2(stderr,"cookie OK\n");

    /*Cookie OK; give up the port and DNS access.*/

    /*WGK 1999/02/27

      Unless, of course, we're checkpoint migrating, in which we might
      need to start listening on this port again if the job should
      happen to migrate to another host.

      Ideally, we'd know this in advance. But, we don't. (So, we're
      squatting on privileged ports. :( ) */

#ifndef ENABLE_CHECKPOINT
    close(s);
#endif


    /* The server will send us the secondary port via an ascii
       string. We abort if we don't get it in 10 seconds.*/

    alarm(10);
    secondport = 0;
    for ( ; ; ) {
      if ( (cint = read(newsocket, &c, 1)) != 1) {
	if (cint < 0)
	  perror("queue: read");
	shutdown(newsocket, 2);
	exit(1);
      }
      if (c == 0)		
	break;
      secondport = (secondport * 10) + (c - '0');
    }
    alarm(0);

    /*The secondary port must also be a reserved port.*/
#ifndef NO_ROOT
    if (secondport >= IPPORT_RESERVED) {
      fprintf(stderr, "Cracker? Our secondary port from server was not a reserved port\n");
      exit(1);
    }
#endif

#ifndef NO_ROOT
    oursecport = IPPORT_RESERVED - 1; /* starting port# to try */
    if ( (fdsocket2 = rresvport(&oursecport)) < 0) {
      perror("queue: problem getting second socket");
      exit(1);
    }
#else

    if ( (fdsocket2 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      perror("queue: problem getting second socket");
      exit(1);
    }
 
#endif /*NO_ROOT*/

    address1.sin_port = htons((u_short) secondport);
    if (connect(fdsocket2, (struct sockaddr *) &address1,
		sizeof(address1)) < 0) {
      perror("queue: problem connecting second socket");
      fprintf(stderr, "Socket: %d\n", secondport);
      exit(1);
    }

    /*We need to check that this host is allowed to connect.*/

    hostp = gethostbyaddr((char *) &(address1.sin_addr), sizeof(struct in_addr), address1.sin_family);
    if (hostp) {
      /*Some anti DNS spoofing code; we do a reverse lookup to ensure
	everything is in order, in the style of TCP/IP wrapper.  */

      strncpy(remotehost, hostp->h_name, sizeof(remotehost) - 1);
      remotehost[sizeof(remotehost) - 1] = 0;
      if ( (hostp = gethostbyname(remotehost)) == NULL) {
	fprintf(stderr, "Unable to perform host lookup for %s", remotehost);
	sock_error(newsocket, "Couldn't look do host lookup on your host");
	exit(1);
      }
      for ( ; ; hostp->h_addr_list++) {
	if (bcmp(hostp->h_addr_list[0], (caddr_t) &address1.sin_addr, sizeof(address1.sin_addr)) == 0)
	  break; /* reverse lookup test passed.*/
	    
	if (hostp->h_addr_list[0] == NULL) {
	  fprintf(stderr,
		  "Host addr %s not listed for host %s", inet_ntoa(address1.sin_addr), hostp->h_name);
	  sock_error(newsocket, "DNS address mismatch for host; cracker or inconsistent DNS database?");
	  exit(1);
	}
      }
      /*Can't call canonicalhost with static hostp->h_name*/
      strncpy(remotehost, hostp->h_name, sizeof(remotehost) - 1);
      remotehost[sizeof(remotehost) - 1] = 0;
      hostname = canonicalhost(remotehost);
    } 
    else
      hostname = "unresolved"; 
    /*Our DNS must be down and this will probably fail.*/

    if(!validhost(hostname, newsocket)) exit(1);

    /*A null byte to queued tells it that everythig is OK.*/

    if (write(newsocket, "", 1) != 1)
      exit(1);


    /*WGK 1998/8/28: We've closed the control file and renamed it; we
      no longer need to bind reserved ports.

      Least privilege at this point specifies we may turn ourselves
      into the oridinary user; this will let the user send signals
      directly to us as well. However, it isn't essential.*/

#if defined(ENABLE_CHECKPOINT) && !defined(NO_ROOT)
    /*If we are checkpoint migrating, we may need the privileges again
      to grab a reserved port once the process migrates. So, we only
      want give up privileges temporarily. If the kernel has
      setresuid, this can be done. Otherwise, we have to hold on to
      privileges if we want to support checkpointing.*/

#ifdef HAVE_SETRESUID
    setresuid(uid, uid, 0);
#endif

#else

    setuid(uid);

#endif /*ENABLE_CHECKPOINT and !NO_ROOT*/




    FD_ZERO(&readf);
    FD_SET(0, &readf);
    FD_SET(newsocket, &readf);
    FD_SET(fdsocket2, &readf);

    /*WGK: Do we need this due to bug in HP-UX?*/
    fdsocket3 = dup(fdsocket2);

    /*WGK: OOB messages will contain information about wait signals
      from the remote job.  
      Non-OOB is stderr.*/

    setsockopt(fdsocket3, SOL_SOCKET, SO_OOBINLINE, &one, sizeof(one));

    FD_ZERO(&urgfrom);
    FD_SET(fdsocket3, &urgfrom);

    /*If 0 is a tty we want it in raw mode*/

    if(ttyinput) {

      memcpy(&myterm, &globalterm, sizeof(globalterm));
      myterm.c_iflag &= ~(IXON|IXANY|IXOFF|ICRNL|IGNCR|INLCR|IUCLC);
      myterm.c_lflag &= ~(ICANON|ISIG|ECHO|ECHOE|ECHOK|ECHONL|IEXTEN|XCASE);
      if(ttyoutput) myterm.c_oflag &= ~OPOST;
#ifndef BROKEN_TERMIOS_VMIN
      myterm.c_cc[VMIN]=2;
#else
      myterm.c_cc[VMIN]=1;
#endif
      myterm.c_cc[VTIME]=1;
#ifdef TCSANOW
      /*IRIX*/
      tcsetattr(0, TCSANOW, &myterm);
#else
      tcsetattr(0, 0, &myterm);
#endif
    }


    /* Reliably copy over all signals to fdsocket2, where they will be
       sent to the other process*/

    {
#ifdef __hpux
      struct sigvec mysignal;
      mysignal.sv_handler = send_signal;
      /*Block all other signals so that signals are delivered in order
        received.*/
      mysignal.sv_mask= ~0L;
      for(i=0;i<64;++i) sigvector(i, &mysignal, NULL);    
#else  /*GNU/Linux*/
      struct sigaction mysignal;
      mysignal.sa_handler = send_signal;

      /*This is the most portable version, it assumes that the signal
	mask is at the start of the mysignal object (which is a
	structure on some systems and a long int on others), where it
	normally is.*/

      *((unsigned long int*) &mysignal.sa_mask) = ~0L;


      /*#ifdef linux /*Actually Redhat GNU/Linux
	mysignal.sa_mask.__val[0] = ~0L;
	#else
	mysignal.sa_mask.__sigbits[0] = ~0L;
	#endif
      */


      /*This blocks the first 64 signals; since sigsetmask and other
	calls use long integer type its not clear whether GNU/Linux
	uses the other 64 signals that are partially defined in
	<sigset.h>*/
      mysignal.sa_flags = SA_RESTART;
      for(i=0;i<64;++i) sigaction(i, &mysignal, NULL);    
#endif
      sigsetmask(0L);
    }     

#ifdef ENABLE_CHECKPOINT
    if(checkpointed) {

      /*Send the interactive process a SIGCONT after a checkpoint so
	that it sets up the terminal on the other end.*/

      send_signal(SIGCONT);
      checkpointed = 0;
    }
#endif /*ENABLE_CHECKPOINT*/

    do {	
      ready = readf;
      urg = urgfrom;

      /*We only need to worry about signal interruptions at the select
	call. If select says data is there, read and write cannot be
	interrupted.*/

      if (select(32, &ready, (fd_set *) 0,
		 &urg, (struct timeval *) 0) < 0)
	/* wait until something to read */
	{  if(errno!=EINTR) break; continue;}
      if (FD_ISSET(newsocket, &ready)) {
	/*Because of OOB, this _can_ get interrupted signal (SIGURG
          when not blocked)

	  This causes all sort of busy looping problems when OOB data
	  is present, but our process has died. Solution: Don't let
	  this read get interrupted by signals at all costs!.*/
	long oldmask;
	oldmask=sigsetmask(~0L);
	if ((cint=read(newsocket, buf, sizeof(buf))) <= 0) { 


	  /*  if (errno!= EINTR) FD_CLR(newsocket, &readf);*/

	  FD_CLR(newsocket, &readf);
	}
	else
	  write(1, buf, cint);
	sigsetmask(oldmask);
      }
      if (FD_ISSET(0, &ready)) {
	cint = read(0, buf, sizeof(buf));
	if (cint <= 0) {
	  /*WK: Bugfix: We only close the standard input if we are in
	    the foreground, or if 0 is in fact not a tty, or if 0 has
	    lost its process group (e.g., a hangup.) This way, if we
	    are thrown in the background by Control-Z/bg, we can later
	    be foregrounded. (Otherwise, standard input returns 0
	    since we are catching all signals, and we then close the
	    socket. bad.)*/
	  pid_t pgrp;
	  if(((pgrp=tcgetpgrp(0))<0)||(
				       pgrp==getpgrp())){
	    /*We are either in the foreground or 0 is not a tty.*/
	    shutdown(newsocket, 1);
	    FD_CLR(0, &readf);
	  }
	} else
	  write(newsocket, buf, cint);
      }

      /*Problem: OOB data may be left unread even though all non-OOB
	data read and socket closed. Of course, server doesn't close
	socket until has send OOB data (DUH), but our rountine below
	doesn't check for remaining OOB data before rushing off can
	clearing fdsocket2 on readf. So, we need to check for
	remaining OOB data before continuing. This is done in the else
	corresponding to the if. WGK 95/7/11.*/



      if(FD_ISSET(fdsocket3, &urg)) {
	int atmark;
	(void) ioctl(fdsocket2, SIOCATMARK, (char*)&atmark);
	/*We don't want to read OOB data until we've read every last
	  drop of regular data first. __HPUX requires that we be in
	  OOBINLINE mode in order to select successfully when OOB data
	  comes in. But, if we are in OOB inline mode, we could
	  potentially be reading regular data.  So, we turn off
	  OOBINLINE mode before reading regular data, thus ensuring
	  that that code only reads regular data. So, here we just
	  need to ensure we are at the mark; if we are, this is OOB
	  data.*/
	if(atmark) {
	  /*Check for OOB data*/
	  if(recv(fdsocket3, &val, sizeof(val), 0) <= 0) FD_CLR(fdsocket3, &urgfrom);
	  else {
	    /*fprintf(stderr, "Received: %d\n", val);*/

#ifdef ENABLE_CHECKPOINT

	    /*If the job just checkpointed, we need to restart
	      everything.  We don't want to send ourselves the
	      checkpoint signal!*/

#define SIGCHKPT SIGUNUSED


	    if (-val == SIGCHKPT) {checkpointed = 1;
	    val = 0;
	    }
#endif


	    if(val < 0) {

	      /*Stop or kill ourselves*/
#ifdef __hpux
	      struct sigvec mysignal;

	      mysignal.sv_handler = SIG_DFL;
	      mysignal.sv_mask= ~0L;
#else
	      struct sigaction mysignal;
	      mysignal.sa_handler = SIG_DFL;
	      /* #ifdef linux
		 mysignal.sa_mask.__val[0] = ~0L;*/ /*Blocks first 64 signals.

		 #else
		    mysignal.sa_mask.__sigbits[0] = ~0L;
		 #endif
	      */

	      /*Again, this is thought to be the most portable
		version:*/
  
	      *((unsigned long int*) &mysignal.sa_mask) = ~0L;


  
	      mysignal.sa_flags = SA_RESTART;
#endif

#ifdef TCSANOW
	      if(ttyinput)    tcsetattr(0, TCSANOW, &globalterm);
#else
	      if(ttyinput)    tcsetattr(0, 0, &globalterm);
#endif
#ifdef __hpux
	      sigvector((int) -val, &mysignal, NULL);
#else
	      sigaction((int) -val, &mysignal, NULL);
#endif
	      /* Danger of race condition. We use sigsetmask to
		 minimize it; Con't won't be sent until we've restored
		 the suspension signal. Still, we can be gotten by a
		 suspension followed very rapidly by cont and then
		 identical suspension.*/
	      sigsetmask(~sigmask((int)-val));
	      raise((int)-val);
	      sigsetmask(~0L);
#ifdef __hpux
	      mysignal.sv_handler = send_signal;
	      sigvector((int)-val, &mysignal, NULL);
#else
	      mysignal.sa_handler = send_signal;
	      sigaction((int)-val, &mysignal, NULL);
#endif
	      sigsetmask(0L);
	      /*For some reason the FD_SETs are garbled after a
                continuation signal.*/
	      continue;
	    }
	    else {
	      estatus = (int) val;
	      FD_CLR(fdsocket3, &urgfrom);
	    }
	
	  }
	  /* Be sure to re-select so that FD_ISSET is correct for
             fdsocket2.*/
	  continue;
	}
      }
      else if(!FD_ISSET(fdsocket2, &readf)) {
	/*fdsocket2 has closed, no OOB data remains, so can clear OOB
	  read flag and exit loop.*/
	FD_CLR(fdsocket3, &urgfrom);
      }
      if (FD_ISSET(fdsocket2, &ready)) {
	/* Turn off OOB inline mode so as not to read it in now.*/
	setsockopt(fdsocket3, SOL_SOCKET, SO_OOBINLINE, &zero, sizeof(one));
	if ((cint=read(fdsocket2, buf, sizeof(buf))) >0 ){
	  write(2, buf, cint);  
	}
	else {FD_CLR(fdsocket2, &readf);}
	/* Turn OOB inline back on so that select() works.*/
	setsockopt(fdsocket3, SOL_SOCKET, SO_OOBINLINE, &one, sizeof(one));
      }
    } while (FD_ISSET(fdsocket2, &readf) ||
	     FD_ISSET(newsocket, &readf)||FD_ISSET(fdsocket3, &urgfrom));
  exit:
#ifdef TCSANOW
    /*IRIX*/
    if(ttyinput) tcsetattr(0, TCSANOW, &globalterm);
#else
    if(ttyinput) tcsetattr(0, 0, &globalterm);
#endif /*TCSANOW*/
#ifdef ENABLE_CHECKPOINT
    if(checkpointed) {
      estatus = 2;
      fflush(NULL);
      close(fdsocket2);
      close(newsocket);
      close(fdsocket3);
      goto checkpoint_restart;
    }
#endif /*HAVE CHECKPOINT*/
    exit(estatus);
  }
}

void
cleanup(void)
{
  exit(1);
}

/*Read a string from the socket making sure that it fits into our buffer.*/
void
getstr(int socket, char *buffer, 
       int size, /* size of the char array */
       char *errm /* what to say if string is too long.*/)
{
  char	byte;

  do {
    if (read(0, &byte, 1) != 1)
      exit(1);	/*EOF?*/
    *buffer++ = byte;
    if (--size == 0) {
      char buffer[BUFSIZ];
      sprintf(buffer, "%s too long.\n", errm);
      sock_error(socket, buffer);
      exit(1);
    }
  } while (byte != 0);	
}

void
Qshusage(void)
{

  fflush(stdout);
  fprintf(stderr, "Queue load balancing system \t http://www.gnuqueue.org .\n\nCopyright 1998-2000 W. G. Krebs <wkrebs@gnu.org> and others.\n\nPlease report bugs to <bug-queue@gnu.org>.\n\nThere is absolutely no warranty for queue; see file COPYING for details.\n\n");
  fprintf(stderr, "qsh is Queue's rsh compatibility mode.\n\n");
  fprintf(stderr, "Usage:\n  qsh [-l ignored] hostname command command.options\n\n-l option is ignored for rsh compatbility.\n\nIf hostname is set to '+', hostname is left up to GNU Queue.\n");
  exit(1);
}

void
Usage(void)
{

  fflush(stdout);
  fprintf(stderr, "Queue load balancing system \t http://www.gnuqueue.org .\n\nCopyright 1998-2000 W. G. Krebs <wkrebs@gnu.org> and others.\n\nPlease report bugs to <bug-queue@gnu.org>.\n\nThere is absolutely no warranty for queue; see file COPYING for details.\n\n");
  fprintf(stderr, "Usage:\n   queue [-h hostname] [-i|-q] [-o|-p|-n] [-w|-r] [-j jobname] [-d spooldir] -- command command.options\n");
  fprintf(stderr, "-h hostname or --host hostname: force queue to run on hostname.\n");
  fprintf(stderr, "-H hostname or --robust-host hostname: Run job on hostname if it is up.\n");
  fprintf(stderr, "-i|-q or --immediate|--queue: Toggle between immediate and queue mode, default is to queue, which requires specification of a spooldir.\n");
  fprintf(stderr, "[-d spooldir] or [--spooldir spooldir] specifies the name of the batch processing directory, e.g., gnulab\n");
  fprintf(stderr, "-o|-p|-n or --half-pty|--full-pty|--no-pty: Toggle between half pty emulation, full pty emulation, and the more efficient no pty emulation.\n");
#ifdef QUEUE_MANAGER
  fprintf(stderr, "-w or --wait Wait (stub daemon).\n");
#else
 fprintf(stderr, "-w|-r or --wait|--batch Toggle between wait (stub daemon) and return (mail batch) mode, default depends on invocation command.\n");
#endif /*QUEUE_MANAGER/*
  fprintf(stderr, "-v or --version: Version\n");

  /* 2000/10/22 added by Monica Lau <mllau@alantro.com>*/
#ifdef QUEUE_MANAGER
  fprintf(stderr,"-a license: Specifies a license that job needs in order to run.  If need to specify more than once license, then type in multiple '-a license'.\n");
  fprintf(stderr, "-r: Specifies batch mode; default is interactive mode.\n");
  fprintf(stderr, "-c: Specifies high priority job; default is low priority.\n");
  fprintf(stderr, "-e logfile: Specifies the file name of a log file.\n");
  #endif /*QUEUE_MANAGER*/
/* 2000/10/22 End of code added by Monica Lau <mllau@alantro.com>*/

  fprintf(stderr, "--help: This menu\n");
  fprintf(stderr, "See documentation for full explanation of options.\n");
  fprintf(stderr, "\nExamples:\n to run interactive matlab remotely: queue -i -w -p -- matlab\n To get the best host to run jobs on: queue -i -w -- hostname\n");
  cleanup();
}


#ifdef MODIFYPRIORITY
/*
 * CountJobs: This function counts the number of currently queued,
 * non-cancelled jobs submitted by the user in this queue.
 *
 * Written by G. Strachan June 1992 as part of the batch suite
 * Modified 1995 by WGK to support queue-submitted jobs.  */

int
CountJobs(fname)
     char *fname;
{
  int uid = getuid();
  DIR *dir;
  struct stat stb;
  struct_dir *dp;
  char job[1024];
  int njobs = 0;

  if((dir = opendir(fname)) == NULL)
    perror(fname);
  while((dp = readdir(dir)) != NULL) {
    if(strncmp(dp->d_name, "cf", 2) != 0)
      continue;
    strcpy(job, fname);
    strcat(job, "/");
    strcat(job, dp->d_name);
    /* check if we own it and it isn't world readable */
    /*WGK 1995/7/12: GID stores UID in CFDIR, but file is actually
      root-owned, and has no group permissions. This prevents user
      from tampering with it, but makes it easy to count how many jobs
      a user owns. Esp. convieient on machines in which gid==uid
      frequent.*/
    if(stat(job, &stb) == 0) {
      if((stb.st_gid == uid) &&
	 ((stb.st_mode & S_IRGRP) == 0) &&
	 ((stb.st_mode & S_IROTH) == 0))
	njobs++;
    }
  }
  return(njobs);
}
#endif


/*WGK: These rountines were added later and should probably either be
better integrated into the existing code or deleted and made inline in
the few places in which they are used.*/

/*  User error.*/
void	
error(char *s)
{
  fprintf(stderr, "queue: %s", s);
  if( index(s, '\n') == NULL )
    fprintf(stderr, "\n");
  fprintf(stderr, "queue: No job submitted.\n");
}

/*System error; job not submitted.  */
void	
syserror(char *s)
{
  fprintf(stderr, "queue: %s", s);
  if( index(s, '\n') == NULL )
    fprintf(stderr, "\n");
  fprintf(stderr, "queue: No job submitted.\n");
  if( debug )
    fprintf(stderr, "SYSLOG: queue: %s\n", s);
  else
    syslog(LOG_ALERT, "queue: %s", s);
}

int 
validhost(char *hostcpy, int socket)
{
  char buf[MAXHOSTNAMELEN + 1];
  register int i;
  int flag = 1;
  char *hostname = buf;

  hostname = strncpy(hostname, hostcpy, MAXHOSTNAMELEN); 
  /*Unfortunately, the static area from gethostbynames is completely
    unstable on some platforms and will not survive a call to stat(),
    which is used in readhosts.*/
  hostname[MAXHOSTNAMELEN] = 0;

  if(!Hosts) {
    if(ReadHosts(QHOSTSFILE) < 0)
      {
	fprintf(stderr, "Can not read queue hosts file %s\n", QHOSTSFILE);
	exit(1);
      }
  }

  for (i=0;i<NHosts;++i) if (strcasecmp(hostname, Hosts[i].host)==0) flag = 0;
  if (flag) {

    fprintf(stderr, "Host %s is not a valid server host.\n", hostname);
    fprintf(stderr, "Authorized hosts in %s are:\n", QHOSTSFILE);
    for(i=0;i<NHosts;++i) fprintf(stderr, "%s\n", Hosts[i].host);
    fprintf(stderr, "\n");
    if(socket) 
      sock_error(socket, 
		 "This server is not in QHOSTSFILE as a valid server host.\n");
    return(0);
  }
  return(1);
}
