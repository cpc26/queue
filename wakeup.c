/*
 *  Queue load balancing system
 *  $Revision: 1.8 $
 *
 *  Copyright (C) 1998-2000 W. G. Krebs
 *
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

#define min(a,b) (a < b ? a : b)
#ifdef DEBUGG
#define DFPRINTF2(a,b) fprintf(a,b)
#define DFPRINTF3(a,b,c) fprintf(a,b,c)
#else
#define DFPRINTF2(a,b)
#define DFPRINTF3(a,b,c)
#endif

extern char cookie[];

void
donothing(void)
{
  /*Signal handler.*/
}


int
compar_wk(struct wkstruct *one, struct wkstruct *two)
{
  if (one->load == two->load) return(0);
  return( (one->load < two->load ? -1 : 1));
}

/*Wake up daemon if sleeping*/
int w_fd, miport;

int
con_daemon(char *hostname, char *queuename, int migrate)
{
  int count=0;
  int status, w_fd;
  struct sockaddr_in hello, *incoming;

  struct hostent *myhost;
  int len;
  bzero((char *) &hello, sizeof(hello));
  /*hello.sin_addr.s_addr = htonl( 0 |127 >> IN_CLASSA_NSHIFT |  0 >> IN_CLASSB_NSHIFT| 0 >> IN_CLASSC_NSHIFT |  1 >> IN_CLASSD_NSHIFT); */
  hello.sin_port = htons(WAKEUP_PORT);

  /*printf("Connecting to %s\n", hostname);*/

  myhost = gethostbyname(hostname);
  if(myhost==0) {
    perror("gethostbyname");
    exit(2);
  }
  bcopy(myhost->h_addr_list[0], (caddr_t) &hello.sin_addr, myhost->h_length);
  hello.sin_family = myhost->h_addrtype;

  /* We try to connect five times to prevent an overloaded system from
     keeping us down.*/
 again:
#ifndef NO_ROOT
  miport = IPPORT_RESERVED - 1;
  if((w_fd = rresvport(&miport))<0){
    perror("con_daemon:rresvport");
    exit(2);
  };
#else
  if((w_fd = socket(AF_INET, SOCK_STREAM, 0))<0) {
    perror("con_daemon: socket");
    exit(2);
  }
#endif /*NO_ROOT*/
  status = connect(w_fd, (const void*) &hello, sizeof(hello));
  if(status <0) {
    if(count < 5) {
      /*     sleep(1);*/
      count++;
      close(w_fd);
      goto again;
    }
    perror("con_daemon: connect");
    fprintf(stderr, "QueueD down on %s?\n", hostname);
    close(w_fd);
    return(-1);
  }
  /*close(w_fd);*/

  {/*WGK 2000/7/19 the draft protocol requires "JOBCONTROLFILE" followed
by the version string for this protocol (VERSION0) followed by the
version of the job ontrol file (VERSION1 is the only version supported.*/
           static char *tmpstring="JOBCONTROLFILE\nVERSION0\nVERSION1\n";
	     write(w_fd,tmpstring,strlen(tmpstring));
  }

  /*Draft protocol compliance.
Use half of sha1() mechanism used later on to confirm cookie without
    actually sending over the network. This will bring us into complaince
with the draft protocol. Other means of authentication can be imagined,
such as those based on a signature, which is essentially an
assymetric authentication. At present, there is no compelling reason
to favor assymetric authentication over symmetric authentication. If
we reach the point where we have huge networks, we might want to
have different types of "keys" --- master, sub-master that only
works on a subnetwork, &c., in which
case an assymetric mechanism might be preferable than giving out the
master key to ever client and server. We're nowhere near that point yet.
WGK 20000729.*/

  /* Old protocol.  write(w_fd, &cookie[0], strlen(&cookie[0]));*/

	 #define ONE_WAY(a) sha1(a);
         #define randomstr(a) strcpy(a,"oscar");

      /*Confirm one-way trapped cookie and then write out clear
        cookie.*/
DFPRINTF2(stderr,"in wakeup reading cnounce\n");
    /*WGK 2000/07/20 digest authentication cnounce/nounce scheme.*/

    {char cnounce[21], nounce[21], bigcookie[256];
    char c;
    char *randomcookie;
     int k;
    alarm(5);
    k = 0;
    do {
      read(w_fd, &cnounce[k], 1);
      k++;
    } while (cnounce[k-1] && k<21);
    alarm(0);
    cnounce[20] = 0;

DFPRINTF3(stderr,"cnounce %s\n",cnounce);

    /*OK, cnounce read, now generate nounce.*/
    randomstr(&nounce[0]);
    DFPRINTF3(stderr,"nounce %s\n",nounce);
    write(w_fd, &nounce, strlen(nounce)+1);

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
	read(w_fd, &c, 1);
	DFPRINTF3(stderr,"%c",c);
	if (c != remotecookie[k])  flag = 1;
      }
      alarm(0);
      if(flag)   {
	DFPRINTF2(stderr,"\nbad remotecookie\n");
	/*We need to start waiting on this socket again.*/
	fprintf(stderr,"Cookiefile authentication with server failed! Someone else is running Queue on this cluster or the other side has the wrong cookiefile!\n");
	close(w_fd);
	return(-1);
      }
      if(!flag)   DFPRINTF2(stderr,"\ngood remotecookie\n");
      /*OK, now we need to reverse the procedure.*/

      randomstr(&cnounce[0]);

      /*Send cnounce, as compliant with draft protocol. WGK 2000/07/20*/
      write(w_fd, &cnounce, strlen(cnounce)+1);
      DFPRINTF3(stderr,"wrote cnounce %s\n",cnounce);
      /*Read nounce, as compliant with draft protocol. WGK 2000/07/20*/

      alarm(5);
    k = 0;
    do {
      read(w_fd, &nounce[k], 1);
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
      DFPRINTF3(stderr,"randomcookie %s\n: ",randomcookie);
      /*Write the challenge response, thus proving via digest authentication we know the cookie without actually giving away what the cookie is. This concludes the mutual authentication as outlined in the draft protocol. WGK 07/20/2000 */

flag = 0;
      write(w_fd, &randomcookie[0], strlen(randomcookie)+1);
       /*Wait for the OK byte from other side indicating authentication has been successful.*/

      alarm(10);
      read(w_fd, &c, 1);
      alarm(0);
      if(c|flag) {
	/*We need to start waiting on this socket again.*/
	DFPRINTF2(stderr,"Cookiefile authentication with server failed! Someone else is running Queue on this cluster or the other side has the wrong cookiefile!\n");
	close(w_fd);
	return(-1);
      }
    }
    }

    DFPRINTF2(stderr,"cookie OK\n");

  {
    char *tmp;
    tmp = malloc(strlen(queuename)+2);
    if(!migrate) sprintf(tmp, "%s\n", queuename);
    else sprintf(tmp, "%s%c\n", queuename, '\001');
    write(w_fd, tmp, strlen(tmp));
    free(tmp);
  }
  return(w_fd);
}

float
getrldavg(char *hostname, char *queuename, char migrate)
{
  int count=0;
  int status, w_fd;
  struct sockaddr_in hello, *incoming;
  float load;
  FILE *argh;

  struct hostent *myhost;
  int len;
  bzero((char *) &hello, sizeof(hello));
  /*hello.sin_addr.s_addr = htonl( 0 |127 >> IN_CLASSA_NSHIFT |  0 >> IN_CLASSB_NSHIFT| 0 >> IN_CLASSC_NSHIFT |  1 >> IN_CLASSD_NSHIFT); */
  hello.sin_port = htons(QUERY_PORT);

  myhost = gethostbyname(hostname);
  if(myhost==0) {
    perror("gethostbyname");
    exit(2);
  }
  bcopy(myhost->h_addr_list[0], (caddr_t) &hello.sin_addr, myhost->h_length);
  hello.sin_family = myhost->h_addrtype;

  /* We try to connect five times to prevent an overloaded system from
     keeping us down.*/
 again2:
  if((w_fd = socket(AF_INET, SOCK_STREAM, 0))<0) {
    perror("socket");
    exit(2);
  }
 again:
  status = connect(w_fd, (const void*) &hello, sizeof(hello));
  if(status <0) {
#if 0
    /* doesn't work, w_fd is in error state and cannot be reused
       anyway. */
    if(count < 1) {
      sleep(1);
      count++;
      goto again;
    }
#endif
    perror("getrldavg: connect");
    fprintf(stderr, "QueueD down on %s?\n", hostname);
    close(w_fd);
    return(1e08);
  }

  {/*WGK 2000/7/19 the draft protocol requires "QUERY" followed
by the version string for this protocol (VERSION0) followed by the
version of the job ontrol file (VERSION1 is the only version supported.*/
        char *tmpstring="QUERY\nVERSION0\nVERSION1\n";
	  write(w_fd,tmpstring,strlen(tmpstring));
  }
  /*Older protocol resumes.*/

  /*WK: Quick hack to get queueload from other host.*/



  argh = fdopen(w_fd, "r+");

  if(!migrate) fprintf(argh, "%s\n", queuename);
  else fprintf(argh, "%s%c\n", queuename, '\001');
  fflush(argh);
  load = 1e09;
  /*SIGALRM will interrupt the fread.*/
  signal(SIGALRM, donothing);
  alarm(5);

  netfread(&load, sizeof(load), 1, argh);

  alarm(0);
  fclose(argh);
  close(w_fd); /*Make sure it is closed.*/
  if(load == 1e09) {
    count++;
    if(count < 5) goto again2;
  }
  /*printf("%s returned load %f\n", hostname, load);*/
  return(load);
}

int
wakeup(char *queuename, int migrate, int contact, char *nothost)
{
  int ret;
  char buf[255];
  FILE* message;
  register int i;

  for (i=0;i<NHosts;++i) {
    if((nothost)&&(!strcmp(nothost,Hosts[i].host))) Hosts[i].load=1e08;
    else Hosts[i].load = getrldavg(Hosts[i].host, queuename, migrate);
  }
  qsort(Hosts, NHosts, sizeof(struct wkstruct), compar_wk);

  /*If migrating, might want to add code to prevent us from contacting
    the localhost at some point. However, if code in queued is
    correct, localhost will always return 1e08 on migrating since load
    average is above loadsched, so not needed.*/

  ret= -1;
  if(contact)
    for(i=0;ret==-1 & i<NHosts; ++i ) {

      /*WGK 2000/07/30. Prevent from sending jobs to ourself, which is
       usually the value of nothost.*/
      if((nothost)&&(!strcmp(nothost,Hosts[i].host))) continue;

      ret = con_daemon(Hosts[i].host, queuename, migrate);
    }
  return(ret);
}
