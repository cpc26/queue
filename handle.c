/*
 *  Queue load balancing system
 *  $Revision: 1.8 $
 *
 *  Copyright (C) 1998-2000 W. G. Krebs
 *
 *  wkrebs@gnu.org
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
 *	wkrebs@gnu.org
 *
 **************************************************************************/

#include "queue.h"

#ifdef HAVE_ASM_CHECKPOINT_H
#include "checkpoint.h"
extern void mrestart(char *);
#endif /*HAVE_ASM_CHECKPOINT_H*/

#if defined(HAVE_IDENTD) && defined(NO_ROOT)
extern int check_ident();
#endif

extern void mkutmp();

int fd3, fd22, collected;
char mode;

#ifdef __hpux
struct sigvec mysignal;
#else /*GNU/Linux*/
struct sigaction mysignal;
#endif


int controlmsg (char *string);

void
do_nothing(void)
{
  /*This is a hack, obviously.*/
}

char *fgetl(FILE *stream);

/*Maximum size of a setenv array*/

#define SAFETY 10000
#define FORK 1

/*Maximum number of environmental variables minus one*/
#define EMAX 1000

char tmp[10000];
int keepalive=1;
int one = 1;

int checkpointmode = 0;
int restartmode = 0;
char *restartfile = NULL;

int compar(const void *one, const void *two)
{
  return(strcmp((char*)one, (char*) two));
}

int fdsock1; /*Stdin/out Socket.*/
int fdsock2; /*Standard error and control socket.*/
int pid=0, newwait, donthang;
FILE *queue;
int dead;

int loadstop = 0;

char ttyinput, ttyoutput, ttyerror;

void
chldsigh(void)
{
  char val;
  if(pid == 0) return; /*Strange, no child.*/
  if(!waitpid(pid, &newwait, WUNTRACED|WNOHANG)) return; /*Not our child*/

  /*WGK 95/7/10: This code doesn't work well with queueIII, which
    auto-suspends processes under certain conditions. Moreover, there
    are probably other ways for a determined user to tie up the queue,
    so what's the point.*/
  /*    if((donthang==1)&&(WIFSTOPPED(newwait)!=0)) {
      fprintf(queue,
	      "\nShell was killed by queued because\n it was stopped \
	      in queue by signal %d.\n",
	      WSTOPSIG(newwait));
      kill(-pid, SIGTERM);
      sleep(1);
      kill(-pid, SIGKILL);
    }
    else {*/

  if(WIFEXITED(newwait)) {
    val = WEXITSTATUS(newwait);

    if((val > 127)||(val < 0)) val = 127;
  }
  else if(WIFSIGNALED(newwait))  val = -WTERMSIG(newwait);
  else val = -WSTOPSIG(newwait);
  /*fprintf(stderr, "Sending: %d\n", (int)val);*/

  /*Alert stub to suspension/resume only if not a result of load
    control.  This way stub does not suspend under these
    circumstances.*/
  if ((donthang==0)&&((loadstop==0)
		      ||!(WIFSTOPPED(newwait))))
    send(fdsock2, &val, sizeof(val), MSG_OOB);

  /*Make sure we terminate*/
  if(WIFSTOPPED(newwait)==0) dead = 1;
}

void
bigsigh(int n)
{
  /* 95/7/7 WK: The purpose of this hack is to propagate signals from
     batchd. Queued and batchd must run as separate processes
     (although could be same binary) so some interprocess
     communication necessary, at least until light threading comes
     along. Even then, number of file descriptors needed recommends
     sep process. Ok, SIGSTOP can't be caught, and SIGTERM isn't quite
     what we want, so we translate these.  We don't suspend
     ourselves.*/

  if(pid==0) return;



/*If we have the kernel API, exec checkpoint call on SIGUSR2.*/

  if (n==SIGUSR2) {

#ifdef HAVE_ASM_CHECKPOINT_H
    if(checkpointmode==KERNEL_CHECKPOINT) {
      int fd, ret;
      char val;
      fd=creat(restartfile, S_IRWXU);

      /*Prevent sigchldh from learning of death of process until we
	are ready.  There is actually a slight race condition here:
	what happens if the child dies after the sigsetmask but before
	the checkpoint? Queue will be informed that the child died due
	to checkpoint. We either need CKPT_STOP, so that when the
	process dies there is no doubt what killed it, or wait needs
	to return SIGCHKPT as the cause of death.*/

      sigsetmask(~0L);
      ret = checkpoint(pid, fd,
		       CKPT_CHECK_ALL|CKPT_KILL|CKPT_NO_SHARED_LIBRARIES
		       |CKPT_NO_BINARY_FILE);
      /*Process should die now. Report the fact that it was
	checkpointed to queue. sigchldh will subsequently report its
	death.*/
      val = -SIGUNUSED;
      if (!donthang) send(fdsock2, &val, sizeof(val), MSG_OOB);
      /*Now allow sigchldh to inform Queue of death of child.*/
      sigsetmask(~sigmask(SIGCHLD));
      close(fd);
    }
#endif /*HAVE_ASM_CHECKPOINT_H*/

    if(checkpointmode!=USER_CHECKPOINT)  return; /*Ignore signal.*/

  }



  /*Load average flag controls SIGSUSP/SIGCONT behavior in SIGCHLD,
    which is one reason we don't stay suspended. If we get SIGUSR1,
    send SIGSTOP to chld, and mark it with loadstop. If the user
    attempts to resume the stub, (via fg) we detect it and warn the
    user. Sigchldh also doesn't bother to suspend the parent under
    these circumstances.*/

  if(n == SIGUSR1) {
    if(!loadstop)
      controlmsg("[Load average on CPU server too high; process suspended. Please stand-by.]\n");
    loadstop = 1;
    n = SIGSTOP;
  }

  if(n==SIGCONT) {
    if(loadstop) controlmsg("[Your process has resumed execution.]\n");
    loadstop = 0;
  }

  kill(-pid, n);

  if(n!=SIGSTOP) {

    /*With SIGCONT and SIGTERM we have to be careful to ignore the
      next issuance of the signal, since if we are in child's process
      group will send ourselves SIGCONT. We must clear the signal
      before continuing.*/

#ifdef __hpux
    struct sigvec mysignal;
    mysignal.sv_handler = SIG_IGN;
    mysignal.sv_mask = ~sigmask(SIGCHLD);
    sigvector(n, &mysignal, NULL);
#else /*GNU/Linux*/
    struct sigaction mysignal;
    mysignal.sa_handler = SIG_IGN;

    /*This is the most portable version, it assumes
      that the signal mask is at the start of the mysignal object
      (which is a structure on some systems and a long int on others),
      where it normally is.*/

    *((unsigned long int*) &mysignal.sa_mask) = ~sigmask(SIGCHLD);

    /*#ifdef linux
      mysignal.sa_mask.__val[0] = ~sigmask(SIGCHLD);
      #else
      mysignal.sa_mask.__sigbits[0] = ~sigmask(SIGCHLD);
      #endif*/
    /*This blocks the first 64 signals; since sigsetmask and other
      calls use long integer type its not clear whether GNU/Linux uses
      the other 64 signals that are partially defined in <sigset.h>*/
    mysignal.sa_flags = SA_RESTART;
    sigaction(n, &mysignal, NULL);
#endif
    /*Allow pending signal to come in and be ignored.*/
    sigsetmask(~sigmask(n));
    /*Restore signal mask Asap.*/
    sigsetmask(~sigmask(SIGCHLD));
#ifdef __hpux
    mysignal.sv_handler = bigsigh;
    sigvector(n, &mysignal, NULL);
#else /*GNU/Linux*/
    mysignal.sa_handler = bigsigh;
    sigaction(n, &mysignal, NULL);
#endif
  }

  if(n==SIGTERM) {
    int nine = SIGKILL;
    /*Give process group some time to think it over and say last prayers.*/

    sleep(1);

    /*WGK 1998/08/05 It might be desirable to increase
      this time or provide the queue user an option to increase this
      time for some jobs. On today's fast systems, however, 1 second
      wait should be enough for all but the most overloaded or network-bound
      jobs.*/
    /*Block sighandler to prevent race condition.*/
    sigsetmask(~0L);
    /*What? Not yet dead? Tell stub process was killed, since we can't
      do this once we're dead.*/
    if ((donthang==0)&&(!dead)) send(fdsock2, &nine, sizeof(nine), MSG_OOB);
    /*Cleanup, as SIGKILL to process group may kill ourselves.*/
    if(ttyinput||ttyoutput) deallocpty();
    /*_cleanup();*/
    fflush(NULL);
    kill(-pid, SIGKILL);
    raise(SIGKILL);
  }
}

int
controlmsg(char *string)
{
  int desc;
  FILE *mstream;

  /*Determine if we can send a control message to a tty;
    If so, which one?*/

  if(ttyerror) desc = fdsock2;
  else {
    if(ttyoutput) desc = fdsock1;
    else return(0);
  }

  /*Success, we have a tty to send to.*/

  if((desc = dup(desc)) < 0) return(-1);
  if((mstream = fdopen(desc, "w"))==NULL) return(-1);

  fprintf(mstream, "%s", string);
  fclose(mstream);
  return(1);
}

extern int globalargc;
extern char **globalargv;

void
handle(char *thefile, int checkpoint, int restart, char *restartdata)
{
  static int num = 0;
  static char *dirstack = 0;
  static int dirsize = 0;
  static int dirnum = 0;
  int i, args, ret;
  char *estack[EMAX];
  char **command;
  char *dirname;
  struct termios globalterm;
#ifdef TIOCSWINSZ
  struct winsize ws;
#endif
  int aid, uid, gid, euid, egid, fd, fd2;
  int ngroups;
  gid_t *gidset;
  int again;
  DIR *dir;
  struct dirent *temp;
  char *file;
  int filedes[2], fdpipe[2], mail, mywait;
  mode_t mask;
  int prio;
  struct sockaddr_in myclient;
  struct rlimit myrlimit[8];
  int miport, miport2;
  u_short rport;
  fd_set readfrom, ready;
  int maxfdp1;
  long oldmask;
  int ppid = 0;
  int pty1, pty2;

  char *userid, *mailuserid, *jobname, *onlyhost, *cookie;

  restartfile = restartdata;
  restartmode = restart;
  checkpointmode = checkpoint;

  again = 1;
  file = thefile;

  /*Change the status line in ps to make clearer who exactly we are.*/

  globalargv[0] = "queued_slave";

  /*if(ReadHosts(QHOSTSFILE) < 0)
    {
    fprintf(stderr, "Can not read queue hosts file %s\n", QHOSTSFILE);
    exit(1);
    }
  */


  while (again==1) {
    again=0;
    mode = 'i';

    sigsetmask(~0L);

    mail = 1;

    if((fd = open(file, O_RDONLY))<0) continue;
    if((queue=fdopen(fd, "r"))==0) continue;

    /*Another daemon deleted the file while we were trying to lock the
      file.*/

    /*The draft protocol says that VERSION1 should be the first thing
      in the file.*/

    {char *version=fgetl(queue);
    if(strcmp(version,"VERSION1")) {
     syslog(LOG_ERR, "Unsuported job control file version.\n");
     exit(2);
    }
    free (version);
    }


    /*The following three pieces of info are needed at the beginning
     because jobinfo() reads them in first for logging info.*/

    netfread(&uid, sizeof(int), 1, queue);
    userid=fgetl(queue);
    mailuserid=fgetl(queue);
    jobname=fgetl(queue);
    onlyhost=fgetl(queue); /*Batchd already checked this for us.*/
    netfread(&donthang, sizeof(donthang), 1, queue);

    cookie=fgetl(queue); /*it general, it prevents a mix-up of ports
			   if queue.c has been killed in the mean-time
			   and another queue.c is residing on the same
			   port.  With NO_ROOT, prevents malicious
			   users; in general, it prevents*/

    /* EJD - 8/14/2000 - Don't need to swap host address */
    fread(&(myclient.sin_addr), sizeof(myclient.sin_addr), 1, queue);
    {int tmpport;
    netfread(&tmpport, sizeof(int), 1, queue);
    rport = htons((u_short) tmpport);
    }

   {register int i;
   int adj = 0;
   netfread(&args, sizeof(int), 1, queue);

#ifdef ENABLE_CHECKPOINT

   /*WGK 1999/3/5 Modified to support user checkpoint restart API.*/
   if((checkpoint == USER_CHECKPOINT) && restart && (restartdata) && (*restartdata)) adj = 1;

#endif /*ENABLE_CHECKPOINT*/

   command = (char **) malloc((args + 1 + adj)*sizeof(char*));

   if (args) command[0] = fgetl(queue);

   if (adj) {
     command[1] = restartdata;
     args++;}

   for(i=adj+1;i<args;++i) command[i] = fgetl(queue);
   command[args] = 0;
   }

   pipe(filedes);

   dirname=fgetl(queue);
   netfread(&aid, sizeof(int), 1, queue);
   netfread(&euid, sizeof(int), 1, queue);
   netfread(&egid, sizeof(int), 1, queue);
   netfread(&gid, sizeof(int), 1, queue);
   netfread(&ngroups, sizeof(int), 1, queue);

   if((gidset = malloc(ngroups*sizeof(gid_t)))==0) {
     syslog(LOG_ERR, "Out of memory.\n");
     exit(2);
   }

   netfread(gidset, sizeof(gid_t), ngroups, queue);
   netfread(&ttyinput, sizeof(ttyinput), 1, queue);
   netfread(&ttyoutput, sizeof(ttyoutput), 1, queue);
   netfread(&ttyerror, sizeof(ttyoutput), 1, queue);
   netfread(&globalterm, sizeof(globalterm), 1, queue);
#ifdef TIOCSWINSZ
   netfread(&ws, sizeof(ws), 1, queue);
#endif
   num = 0;
   while (*(estack[num] = fgetl(queue))!=0) {
     if(strcmp(estack[num], "QMAIL=FALSE")==0) mail =0;
     if(++num + 1 > EMAX) {
       syslog(LOG_ERR, "Too many environmental variables!\n");
       exit(2);
     }
   }
   sprintf(tmp, "QUEUE=%c", mode);
   estack[num]=tmp;
   estack[num+1]=0;
   netfread(&mask, sizeof(mode_t), 1, queue);
   netfread(&prio, sizeof(int), 1, queue);
   netfread(myrlimit, sizeof(struct rlimit), 8, queue);
   /*unlink(file);*/
   /*fclose(queue);*/
   if(donthang==0) {

     {

       int slptim, length, miport;
       struct sockaddr_in address1, address2;
       long oldmask;
       char c;
       struct hostent	*hostp;
       fd_set readfdesc;
       length = sizeof(myclient.sin_addr);
       if ( (hostp = gethostbyaddr(&(myclient.sin_addr), length, AF_INET)) == NULL) {
	 syslog(LOG_ERR, "gethostbyaddr failed: %m");
	 goto berror;
       }
#ifndef NO_ROOT
       miport = IPPORT_RESERVED - 1;
#else
       miport = 32700;
#endif
       slptim  = 1;
       oldmask = sigblock(sigmask(SIGURG));
       while (1) {

#ifndef NO_ROOT
	 if ( (fdsock1 = rresvport(&miport)) < 0) {
	   /*	    if (errno == EAGAIN)
		    fprintf(stderr, "socket: All ports are in use?\n");
		    else */
	   syslog(LOG_ERR, "handle: %m");
	   sigsetmask(oldmask);
	   goto berror;
	 }
#else
	 if ( (fdsock1 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	   /*if (errno == EAGAIN)
	     fprintf(stderr, "socket: All ports are in use?\n");
	     else*/
	   syslog(LOG_ERR, "handle: %m");
	   sigsetmask(oldmask);
	   goto berror;
	 }

#endif

	 bzero((char *) &address1, sizeof(address1));
	 address1.sin_family = hostp->h_addrtype;
	 bcopy(hostp->h_addr_list[0], (caddr_t)&address1.sin_addr,
	       hostp->h_length);
	 address1.sin_port = rport;
	 if (connect(fdsock1, (struct sockaddr *) &address1,
		     sizeof(address1)) >= 0)
	   break;		/* OK*/

	 close(fdsock1);
#ifndef NO_ROOT
	 if (errno == EADDRINUSE) {
	   miport--;
	   continue;
	 }
#endif

	 if (errno == ECONNREFUSED & slptim <= 4) {
	   /*Connection refused; maybe server just overloaded? Sleep a bit, then try again.*/
	
	   sleep(slptim);
	   slptim += 2;	
	   continue;
	 }

	 if (hostp->h_addr_list[1] != NULL) {
	   /*Be sure to try all possible addrs for the host.*/
	   perror((char *) 0);
	
	   hostp->h_addr_list++;
	   bcopy(hostp->h_addr_list[0], (caddr_t) &address1.sin_addr,
		 hostp->h_length);


	   /*inet_ntoa is trustworthy, but we don't really need this
	     bit of code unless we are having problems .*/
	   /*	    {
		    char addr_buf[100];
		    sprintf(&addr_buf, "Attempting to connect to queue on %s\n",
		    inet_ntoa((struct in_addr)address1.sin_addr));
		    syslog(LOG_NOTICE, &addr_buf);
		    }*/
	   continue;
	 }
	
	 /*perror(hostp->h_name);*/	/*so long*/
	 sigsetmask(oldmask);
	 goto berror;
       }

       {
	 /*We create a second socket which handles sterr and also
	   control messages between the clients. This needs to be on a
	   reserved port as well.*/

	 char num[8];
	 int tempsocket, length;
	 struct sockaddr_in hello;


#ifndef NO_ROOT
	 miport--;	/* decrement for starting port# */
	 if ( (tempsocket = rresvport(&miport)) < 0)
	   goto berror;
#else
	 miport--;
	 tempsocket = socket(AF_INET, SOCK_STREAM, 0);
	 {

	   hello.sin_family=AF_INET;
	   hello.sin_addr.s_addr = htonl(INADDR_ANY);
	 portagain:
	   hello.sin_port = htons(miport);
	   if(bind(tempsocket, &hello, sizeof(hello))<0) {
	     if (miport >0) {
	       miport--;
	       goto portagain;
	     }
	     perror("bind");
	     exit(2);
	   }
	 }
#endif /*NO_ROOT*/


	 /*WGK 1998/12/24 Now we want to read our cookie from Queue to
	   make so this isn't another queue.c using the same port
	   since the old one died. Also, in NO_ROOT stops malicious
	   users in the absence of HAVE_IDENTD */

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
      write(fdsock1, &cnounce, strlen(cnounce)+1);

      /*Read nounce, as compliant with draft protocol. WGK 2000/07/20*/

      alarm(5);
      k = 0;
      do {
	read(fdsock1, &nounce[k], 1);
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

      write(fdsock1, &randomcookie[0], strlen(randomcookie)+1);

      /* Reverse the procedure. Read cnounce.*/

    alarm(5);
    k = 0;
    do {
      read(fdsock1, &cnounce[k], 1);
      k++;
    } while (cnounce[k-1] && k<21);
    alarm(0);
    cnounce[20] = 0;


    /*OK, cnounce read, now generate nounce.*/
    randomstr(&nounce[0]);
    write(fdsock1, &nounce, strlen(nounce)+1);

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
	read(fdsock1, &c, 1);
	if (c != remotecookie[k]) flag = 1;
      }
      alarm(0);

      if (flag) {
	/*Alert queue.c that cookie is bad; it will have to
	  wait for a new queued */
	c = 1;
	write(fdsock1, &c, 1);
	/*die.*/
	goto berror2;
      }

	 /*All clear*/
	 c = 0;
	 write(fdsock1, &c, 1);


    }

    }

	 listen(tempsocket, 1);

	 /*Port number is written out on ASCII for queue, so that it
	   knows where to connect to.*/


	 sprintf(num, "%d", miport);
	 if (write(fdsock1, num, strlen(num)+1) != strlen(num)+1) {
	   syslog(LOG_ERR, "write: setting up queued control/stderr socket: %m");
	   close(tempsocket);
	   goto berror;
	 }

	 FD_ZERO(&readfdesc);
	 FD_SET(fdsock1, &readfdesc);
	 FD_SET(tempsocket, &readfdesc);
	 errno = 0;
	 if ((select(32, &readfdesc, (fd_set *) 0, (fd_set *) 0,
		     (struct timeval *) 0) < 1) ||
	     !FD_ISSET(tempsocket, &readfdesc)) {
	   if (errno != 0)
	     syslog(LOG_ERR, "select: setting up queued control/stderr socket: %m");
	   else
	     syslog(LOG_ERR,
		    "select: Strangeness in setting up queued control/stderr socket.\n");
	   close(tempsocket);
	   goto berror;
	 }

	 /*accept to the connect from queue.*/

	 length = sizeof(address2);
	 fdsock2 = accept(tempsocket, &address2, &length);
	 close(tempsocket);	
	 if (fdsock2 < 0) {
	   syslog(LOG_ERR, "accept on secondary socket: %m");
	   miport = 0;
	   goto berror2;
	 }

#ifndef NO_ROOT

	 /*Verify that queue is connecting to us via a reserved port
           as well.*/

	 address2.sin_port = ntohs((u_short) address2.sin_port);
	 if ((address2.sin_family != AF_INET) ||
	     (address2.sin_port >= IPPORT_RESERVED) ||
	     (address2.sin_port <  IPPORT_RESERVED/2)) {
	   syslog(LOG_ERR,
		  "queued: Remote queue did not bind from a reserved port!\n");
	   goto berror2;
	 }
#else
#ifdef HAVE_IDENTD
	 /*We don't use reserved ports since we can't get root, so we
	   try to use identd information instead to prevent another
	   user on the same host from masquerading as the legitimate
	   queue.*/



	 if(!check_ident(&hello, &address2)) {
	   syslog(LOG_ERR,
		  "queued: Remote queue host returned bad identd information and -DHAVE_IDENTD was set!\n");
	   goto berror2;
	 }
		
#endif /*HAVE_IDENTD*/
#endif /*NO_ROOT*/
       }

       alarm(5);
       if (read(fdsock1, &c, 1) != 1) {	
	 /* to start off, read a status byte from server.*/
	 syslog(LOG_ERR, "Queued: Failed to read status byte from queue\n");
	 goto berror2;
       }
       alarm(0);


       if (c != 0) {
	 alarm(30);
	 /*Non-zero staus byte indicates an error on the queue side.*/

	 while (read(fdsock1, &c, 1) == 1) {
	   write(2, &c, 1);
	   if (c == '\n')
	     break;
	 }
	 alarm(0);
	 goto berror2;
       }

       sigsetmask(oldmask);


	


       /* We want to turn on SO_KEEPALIVE so that we can detect when
	  either one of the daemons has gone down, such as due to a
	  host or client crash.*/

       if (keepalive &&
	   setsockopt(fdsock2, SOL_SOCKET, SO_KEEPALIVE, (char *) &one,
		      sizeof(one)) < 0)
	 syslog(LOG_WARNING, "setsockopt SO_KEEPALIVE error: %m");

       if (pipe(fdpipe) < 0) {
	 syslog(LOG_ERR, "Can't make pipe.\n");
	 exit(1);
       }

       if (fdpipe[0] > fdsock2)
	 maxfdp1 = fdpipe[0];
       else
	 maxfdp1 = fdsock2;
       maxfdp1++;

     }
     if(ttyinput||ttyoutput) {
       if((pty1 = allocpty())<0) {
	 FILE *temp;
	 temp = fdopen(fdsock2, "w");
	 fprintf(temp, "No more ptys.\n");
	 syslog(LOG_ERR, "No more ptys.\n");
	 fclose(temp);
	 exit(2);
       }
       fchown(pty1, 0, 0);
       fchmod(pty1, S_IRUSR|S_IWUSR);
     }
   }
   dead = 0;

   {
#ifdef __hpux
     struct sigvec mysignal;
     mysignal.sv_handler = chldsigh;
     mysignal.sv_mask = ~sigmask(SIGCHLD);
     mysignal.sv_flags = SV_BSDSIG;
     sigvector(SIGCHLD, &mysignal, NULL);
     mysignal.sv_handler = bigsigh;
     sigvector(SIGUSR1, &mysignal, NULL);
     sigvector(SIGUSR2, &mysignal, NULL);
     sigvector(SIGCONT, &mysignal, NULL);
     sigvector(SIGTERM, &mysignal, NULL);
#else /*GNU/Linux*/
     struct sigaction mysignal;
     mysignal.sa_handler = chldsigh;

     /*See previous comment.*/
     *((unsigned long int*) &mysignal.sa_mask) = ~sigmask(SIGCHLD);

     /*#ifdef linux
       mysignal.sa_mask.__val[0] = ~sigmask(SIGCHLD);
       #else
       mysignal.sa_mask.__sigbits[0] = ~sigmask(SIGCHLD);
       #endif
     */

     mysignal.sa_flags = SA_RESTART; /*BSD signal behavior*/
     sigaction(SIGCHLD, &mysignal, NULL);
     mysignal.sa_handler = bigsigh;
     sigaction(SIGUSR1, &mysignal, NULL);
     sigaction(SIGUSR2, &mysignal, NULL);
     sigaction(SIGCONT, &mysignal, NULL);
     sigaction(SIGTERM, &mysignal, NULL);
#endif
     mywait = 0;
     sigsetmask(~(sigmask(SIGCHLD)|sigmask(SIGUSR1)|sigmask(SIGUSR2)|sigmask(SIGCONT)|sigmask(SIGTERM)));
   }

   fflush(stdout);
   fflush(stdin);
   setsid();
   setpgid(0, 0);

   if((donthang==0) && (ttyinput||ttyoutput)){
     if((pty2 = open(mtos(), O_RDWR))<0) {
       syslog(LOG_ERR, "slave pty fd # %d open: %s %m", pty1, mtos());
       exit(2);
     }
   }

#if FORK > 0
   if((pid=fork())==0) {
#endif

     close(fd3);

     setpgid(0, 0);

     if(ttyinput||ttyoutput) {
       tcsetpgrp(pty2, getpid());

#ifndef NO_ROOT
       /*Without privileges, we can't create a utmp entry.*/
       mkutmp("root", pty2, uid, gid);
#endif

       tcflush(pty2, TCIOFLUSH);
#ifdef TCSANOW
       /*IRIX requires TCSANOW to be present.*/
       if(tcsetattr(pty2, TCSANOW, &globalterm)<0) {
	 syslog(LOG_ERR, "tcsetattr on fd %d: %m", pty2);
	 exit(2);
       }
#else
       if(tcsetattr(pty2, 0, &globalterm)<0) {
	 syslog(LOG_ERR, "tcsetattr on fd %d: %m", pty2);
	 exit(2);
       }
#endif /*TCSANOW*/
#ifdef TIOCSWINSZ
       if(ioctl (pty2, TIOCSWINSZ, (char *)&ws) <0) {
	 syslog(LOG_ERR, "ioctl TIOCSWINSZ: %m");
	 exit(2);
       };
#endif
     }

     umask(mask);
#ifndef NO_ROOT
     if(setgroups(ngroups, gidset)<0){
       syslog(LOG_ERR, "setgroups: %m");
       exit(2);
     }
#endif

     free(gidset);

#if defined(HAVE_SYS_AUDIT_H) && defined(__hpux)
     setaudid(aid); /*HP-UX*/
#endif

     /* WGK 98/08/06 the latest GNU/Linux kernel has implemented
       setresuid but not setresgid, even though it is documented in
       the man page. :-(

       When the effective user id is root, setgid(x) should be
       equivalent to setresgid(x, x, x) so, it shouldn't matter for
       this application.  (They are _not_equivalent when the effective
       user is not root, which is why setgid IMHO should be made
       obsolete as it behaves quite differently depending on who the
       user is.)  */

#ifdef HAVE_SETRESGID
     if(setresgid(gid, gid, gid)<0) {
       /*GNU/Linux kernel doesn't support it yet?*/
#endif
       if(setgid(gid)<0) {
#ifndef NO_ROOT
	 syslog(LOG_ERR, "setgid: %m");
	 exit(2);
#endif
	 ;
       }
#ifdef HAVE_SETRESGID
     }
#endif

#ifndef NO_ROOT
#ifdef HAVE_SETRESUID
     if(setresuid(uid, uid, uid)<0) {
       /*GNU/Linux kernel doesn't support it yet?*/
#endif

       if (setuid(uid)<0) {
	 syslog(LOG_ERR, "setuid: %m");
	 exit(2);
       }
#ifdef HAVE_SETRESUID
     }
#endif /*HAVE_SETRESUID*/
#endif /*NO_ROOT*/

     setpriority(PRIO_PROCESS, 0, prio);
     /*   for(i=0;i<8;++i) {
       static struct rlimit templimit;*/
       /*Do not

WGK 2000/08/11 Eric Deal <eric.deal@conexant.com> pointed out that this
"Do not" comment looks bogus, because it goes too far into the code.

The entire for loop should be commented out, which I've now this. This
probably created another bug involving Xwindows applications that close
stdin and stdout that wasn't present in old versions of Queue.
*//*
	 getrlimit(i, &templimit);

	 (templimit.rlim_curr < myrlimit[i].rlim_curr ? myrlimit[i].rlim_curr = templimit.rlim_curr)
	 (templimit.rlim_max < myrlimit[i].rlim_max ? myrlimit[i].rlim_max = templimit.rlim_max)
	 ssetrlimit(i, &myrlimit[i]);
	 }*/

	 closelog();

	 fflush(stdout);
	 fflush(stderr);

	 #if FORK > 0
	 if(donthang==1)
	 {
	 int fd;
	 fd = open("/dev/null", O_RDWR);
	 dup2(fd, 0);
	 if(mail!=1) {
	 dup2(fd, 2);
	 dup2(fd, 1);
	 }
	 close(fd);
	 }
	 else {
	 #endif

	 close(fdsock2);		/* control process handles this fd */
       close(fdpipe[0]);	/* close read end of pipe */
       dup2(fdpipe[1], 2);	/* stderr of shell has to go through
				   pipe to control process */
       close(fdpipe[1]);
       if(ttyoutput==0) dup2(fdsock1, 1);
       else {dup2(pty2, 1);
       if(ttyerror) {
	 dup2(pty2, 2);
       }
       }
       if(ttyinput==0) dup2(fdsock1, 0);
       else {dup2(pty2, 0);
       }
       if(!(ttyinput||ttyoutput)) close(fdsock1);
       /*
	 For some bizarre reason adding this breaks everything
	 else close(pty2);*/

#if FORK > 0

     }

#endif

     /*We chdir as the user because, under NFS, we are more likely to
       get there as the real user, and also because this way stderr is OK
       so that we can send over a message.*/

     if (chdir(dirname)<0) {
       char junk[256];
       if(gethostname(junk, 255)==0){
	 junk[255] = 0;
	 fprintf(stderr, "queued: chdir to %s on host %s failed.\n", dirname, junk);
       }

     }
     else
       {
	 register int i, tmp;
	 char *path, *cur, *start, *end;
	 char **search;
#define MAXCMD 1024
	 char file[MAXCMD+1];
	 char flag = 0;
	 int len;

	 if((*command[0] == '/') || (*command[0] == '.'))  {
	   strncpy(file, command[0], MAXCMD);
	   file[MAXCMD] = 0;
	   if(access(file, X_OK)==0)   flag = 1;
	 }
	 else {

	   /*1999/02/13 WGK bugfix. Search through estack to find PATH.*/

	   len = MAXCMD - strlen(command[0]) - 1;

	   path = NULL;

	   {char **tmp = estack;
	   while(tmp && *tmp && **tmp) {
	     if(!strncmp(*tmp, "PATH=", 5)) {
	       path = *tmp + 5;
	       break;
	     }
	     tmp++;
	   }
	   }

	   if(path && *path) {
	     cur = path;
	     end = path + strlen(path);
	     start = cur;
	     while(cur++!=end) {
	       if(((cur==end)||(*cur==':'))&&((cur-start)<len)) {
		 strncpy(file, start, cur - start);
		 file[cur-start] = 0;
		 strcat(file, "/");
		 strcat(file, command[0]);
		 if(access(file, X_OK)==0) {
		   flag = 1;
		   break;
		 }
		 start = cur + 1;
	       }
	     }
	   }
	 }

	 if(!flag) {
	   fprintf(stderr, "%s: Command not found.\n", command[0]);
	   fflush(stderr);
	   /*WGK 98/12/23 Bugfix Not sure why a simple exit doesn't
	     work here (it seemed to work on some operating systems
	     after a sleep.

	     Instead, we'll do the normal the signal-handler stuff and
	     then do an exit(2) instead of the exec. This seems to fix
	     the problem.

	     What may be happening is that we receive notification of
	     our death or something via signals (perhaps a pipe/socket
	     related signal) and this causes a hang in exit in our
	     signal handler; clearing all signals before exit fixes
	     the problem. Of course, then it's not clear why a sleep()
	     seemed to fix it sometimes as well. */
	 }
	 {
	   register int i;
#ifdef __hpux
	   for(i=0;i<64;++i) sigvector(i, SIG_DFL, NULL);
#else
	   /*GNU/Linux*/
	   struct sigaction myaction;
	   myaction.sa_handler= SIG_DFL;
	   for(i=0;i<64;++i) sigaction(i, &myaction, NULL);
#endif
	   sigsetmask(0L);
	 }
	 /*WGK 1999/3/5 restart kernel API.*/
#ifdef HAVE_ASM_CHECKPOINT_H
	 if (checkpoint==KERNEL_CHECKPOINT && flag) {
	   ret=collect_data(getpid());
	   if (ret!=0)
	     {
	       fprintf(stderr, "Error (%d) preparing to collect data\n", ret);
	       exit(2);
	     }
	 }

	 if((checkpoint==KERNEL_CHECKPOINT) && flag && restart == RESTART && restartdata && *restartdata) {
	   mrestart(restartdata);
	   /*Normally not reached.*/
	   fprintf(stderr, "QueueD: handle: In process migration, unable to restart file %s\n",
		   restartdata);
	   exit(2);
	 }
#endif /*HAVE_ASM_CHECKPOINT_H*/

	 if (flag) execve(file, command, estack);
       }
     /*WGK 98/12/23 Command not found or problem with the exec.*/
     exit(2);

#if FORK > 0
   }
#endif

   close(filedes[0]);

   if(donthang == 0) {
     int null;
     close(fdpipe[1]);	/*Close the write end of pipe.*/

     null = open("/dev/null", O_RDWR);

     FD_ZERO(&readfrom);

     FD_SET(fdsock2, &readfrom);
     /*The following line is needed because of the drain code.*/
     fcntl(fdsock2, F_SETFL, O_NDELAY);
     FD_SET(fdpipe[0], &readfrom);
     fcntl(fdpipe[0], F_SETFL, O_NDELAY);
     if (ttyinput) {
       FD_SET(fdsock1, &readfrom);
       fcntl(fdsock1, F_SETFL, O_NDELAY);
     }
     if (ttyoutput) {
       FD_SET(pty1, &readfrom);
       /* Very important! */
       fcntl(pty1, F_SETFL, O_NDELAY);
     }
     if(!(ttyoutput||ttyinput))	close(fdsock1);

     do {
       ready = readfrom;
       if (select(32, &ready, (fd_set *) 0,
		  (fd_set *) 0, (struct timeval *) 0) < 0)
	 /* wait until something to read */
	 {if(errno!=EINTR) break;
	 if(dead) {
	   FD_CLR(fdsock2, &readfrom);
	   if(ttyinput||ttyoutput) {
	     FD_CLR(fdsock1, &readfrom);
	     if(FD_ISSET(pty1, &readfrom)){
	       /*without the next line we sometimes go infinite loop!*/
	       FD_SET(pty1, &ready);
	       /*We can't do a select because select will block on the
		 dead pty.  But, we could still need to drain stderr
		 too! So set this, and let nblocking worry about the
		 rest.*/
	       FD_SET(fdpipe[0], &ready);
	       goto drain;}
	   }
	 }
	 continue;
	 }
     drain:				
       if (FD_ISSET(fdsock2, &ready)) {
	 static char sigval;
	 if ((read(fdsock2, &sigval, 1)) <=0) {
	   /*The death of the control stream
	     is ominous. We invoke the
	     our SIGTERM handler.*/
				/*	raise(SIGTERM); */
	   FD_CLR(fdsock2, &readfrom);
	 }
	 else {

	   /*printf ("Received: %d\n", (int) sigval);*/

	   /* WK 95/7/10: A determined user may restart a stopped job
	      anyway by directly sending SIGCONT to the process, and
	      we would be none the wiser since wait() would not tell
	      us the process was restarted. Only actively scanning the
	      process table entry would alert us to this
	      situation. Since we don't do this, I see no reason to be
	      HAVE_IDENTD over SIGCONTs comming from the stub. A
	      second problem is that some signals, eg SIGTERM, are
	      followed by a SIGCONT by the operating system (HP-UX) to
	      force the process to resume prior to termination. So, if
	      the user sends over a SIGTERM and we propagate, we are,
	      in effect, sending a SIGCONT too. We could catch this,
	      but it gets complicated and may prevent the user from
	      killing stopped jobs before they restart.*/

	   if(((int)sigval==SIGCONT)&(ttyinput||ttyoutput))
	     tcsetpgrp(pty1, pid);


	   /*If the signal is SIGWINCH we can expect the window size
             to be sent as well.*/
#ifdef TIOCSWINSZ
	   if (((int)sigval == SIGWINCH) & (ttyinput||ttyoutput)) {
	     struct winsize ws;
	     /*WGK Our code in WINCH ensures that all of this is sent
	       as a single packet, so we need not be concerned about
	       the fact that we are non-blocking here.*/
	     read(fdsock2, &ws, sizeof(ws));
	     /*printf ("Queued New window size: %d %d\n", ws.ws_row, ws.ws_col);*/
	     /*WGK note: On some systems the following ioctl will
               generate a SIGWINCH as well.*/
	     ioctl (pty1, TIOCSWINSZ, (char *)&ws);
	   }
#endif

	   kill(-pid, (int) sigval);
	 }
       }
       if (FD_ISSET(fdpipe[0], &ready)) {
	 static int cc;
	 static char buf[BUFSIZ];
	 errno = 0;
	 cc = read(fdpipe[0], buf, sizeof(buf));
	 if (cc <= 0) {
	   FD_CLR(fdpipe[0], &readfrom);
	 } else
	   write(fdsock2, buf, cc);
       }

       if ((ttyinput|ttyoutput) && FD_ISSET(pty1, &ready)) {
	 static int cc;
	 static char buf[BUFSIZ];
	 errno = 0;
	 cc = read(pty1, buf, sizeof(buf));
	 if (cc <= 0) {
	   FD_CLR(pty1, &readfrom);
	 } else
	   write(fdsock1, buf, cc);
       }
       if (FD_ISSET(fdsock1, &ready)) {
	 static int cc;
	 static char buf[BUFSIZ];
	 errno = 0;
	 cc = read(fdsock1, buf, sizeof(buf));
	 if (cc <= 0) {
	   FD_CLR(fdsock1, &readfrom);
	 } else
	   /*We should only be here if
	     ttyinput, otherwise we would have given up fdsock1.*/
	   /*assert(ttyinput);*/
	   write(pty1, buf, cc);
       }
       if(dead) {
	 FD_CLR(fdsock2, &readfrom);
	 if(ttyinput||ttyoutput) {
	   FD_CLR(fdsock1, &readfrom);
	   if(FD_ISSET(pty1, &readfrom)){
	     FD_SET(fdsock2, &ready);
	     FD_SET(pty1, &ready);
	     goto drain;}
	 }
       }
			
     } while (FD_ISSET(fdsock2, &readfrom) ||
	      FD_ISSET(fdpipe[0], &readfrom)||
	      ((ttyinput|ttyoutput)&&FD_ISSET(pty1, &readfrom))
	      ||FD_ISSET(fdsock1, &readfrom));
     close(null);
     close(fdpipe[0]);
     if(ttyinput||ttyoutput) {
       /*We need pty2 help open in case process suspends. If no active
	 process on pty2, pty2 process group gets HANGUP.*/
       close(pty2);
       close(pty1);
       close(fdsock1);
       deallocpty();
     }
   }

   /* WK 95/7/10 We wait here until the SIGCHLD handler puts our wait
      status into newwait. Actually, will the above mess even
      terminate if dead is not set? Yes, but only under wierd
      conditions in which the sockets are closed prematurely and the
      child is subsequently killed. Sleep(3c) is supposed to be
      interrupted by caught signals, so we should be OK.*/

   while (!dead) sleep(1);

   /* Shut it down*/
   sigsetmask(~0L);

   /*Stub will die after fdsock2 is closed, so we don't want that to
     happen until we know exit status has been delivered via dead
     above.*/

   close(fdsock2);

   if(donthang==1) queue = fdopen(filedes[1], "w");
   else {
     close(filedes[1]);
     queue=fopen("/dev/null", "w");
   }
   /*Get rid of anything else*/

   wait(NULL);

   /*Simulate exit status of child.*/

   if(WIFEXITED(mywait)!=0) exit(WEXITSTATUS(mywait));
   if(WCOREDUMP(mywait)==0) {
     /*Process did not dump core, so prevent a simulated core dump.*/
     struct rlimit rlp;
     rlp.rlim_cur = 0;
     rlp.rlim_max = 0;

#ifdef __hpux
#define RLIMIT_CORE 4
#endif

     setrlimit(RLIMIT_CORE, &rlp);
   }
   if(WIFSIGNALED(mywait)==0)
     fprintf(queue, "\nShell terminated from unknown cause.\n");
   fflush(stdout);
   fclose(queue);
   if(WIFSIGNALED(mywait)!=0) {
     signal(WTERMSIG(mywait), SIG_DFL);
     sigsetmask(~sigmask(WTERMSIG(mywait)));
     /*Make sure everything gets flushed out.*/
     /*_cleanup();*/
     fflush(NULL);
     raise(WTERMSIG(mywait));
   }
   exit(2);


   /*Connection to qhang failed.*/
 berror2:
   if(donthang==0) close(fdsock2);
 berror:
   if(donthang==0) {close(fdsock1);
   sigsetmask(oldmask);
   close(filedes[0]);
   close(filedes[1]);
   break;
   }
 }
exit(2);
}

char *
fgetl(FILE *stream)
{
  char *cur;
  char *limit;
  char *heap;
  int size;

  /*Allocate a big buffer so that we don't have to keep calling
    realloc.*/
  size = SAFETY;
  heap = (char*) malloc((size)*sizeof(char));
  cur = heap;
  limit = heap + size;
  while((*cur++=fgetc(stream))!=0) {
    if(cur >= limit) {
      syslog(LOG_ERR, "fgetl: Setenv line too long\n");
      exit(2);
    }
  }
  /*Deallocate excess memory.*/
  heap = (char*) realloc(heap, ( cur - heap )*sizeof(char));
  return(heap);
}

/*WGK 98/12/23

  Got rid of main; we are now linked into queued (formerly batchd).

  This code is still useful if we want to run handle as a separate
  executable, which may be necessary for debugging purposes. (A
  debugger cannot follow a fork.)*/

/*
main(argn, argv)
     int argn;
     char **argv;
     {
     if(argn!=2) {
     fprintf(stderr, "Usage: queued filename\n");
     exit(2);
     }
     #ifndef __hpux__system9
     openlog("queued", LOG_PID, LOG_DAEMON);
     #else
     openlog("queued", LOG_PID|LOG_DAEMON);
     #endif
     handle(argv[1]);
     }
*/
