#include "queue.h"

#if defined(HAVE_IDENTD)&&defined(NO_ROOT)


#define IDENTD_TIMEOUT 	10 	/*Our own cluster, so let's not waste
                                  a lot of time here.*/
#define	IDENTD_PORT	113	/* Standard identd port*/
#define IDENTD_BUFFER 	512 	/*seems a bit large.*/

/*Return remote user info (we want the UID), given the sockets.

We are on the same cluster, so we may assume the password file is the
same.

We just check that the username or uid returned matches our own; if it
does, we return 1.  If it doesn't (identd returns something wierd,
fails to respond, or times out) we return 0.

This won't work correctly if the identd is spoofing usernames/uids or
returning random entries for privacy reasons.

If this is the case, either don't use the HAVE_IDENTD option, or have
your administrator set up a "real" identd on another port (possible
restricted via tcp_wrappers to the local cluster only.*/

void 
do_nothing2 (void) {
  /*This is a hack, obviously. By providing a 'real' function, ALRM
    will actually be given, interrupting fgets*/
}

int   
check_ident (struct sockaddr_in *local_socket, 
	     struct sockaddr_in *remote_socket)
     /*These are in our host order.*/
{

  int iuid; 
  char uid[10]; /*Buffer for username in ASCII*/
  char   *copyptr;
  char    my_buf[IDENTD_BUFFER];
  int s;
  FILE *sockstream;
  unsigned remoteport;
  unsigned localport;
  struct sockaddr_in remote_socket2; /*These are in network host order.*/
  struct sockaddr_in local_socket2;
  char    user[IDENTD_BUFFER];		


  unsigned int alarm_time;
  void *oldsignal;

  oldsignal = signal(SIGALRM, do_nothing2);
  alarm_time = alarm(IDENTD_TIMEOUT);

  local_socket2 = *local_socket;
  local_socket2.sin_port = htons(0);
  remote_socket2 = *remote_socket;
  remote_socket2.sin_port = htons(IDENTD_PORT);

  s = socket(AF_INET, SOCK_STREAM,0);
  sockstream = fdopen(s, "r+");

  if (bind(s, (struct sockaddr *) &local_socket2, 
	   sizeof(local_socket2)) >= 0 
      && connect(s, (struct sockaddr *) &remote_socket2,
		 sizeof(remote_socket2)) 
      >= 0) {
    
    fprintf(sockstream, "%u,%u\r\n", 
	    ntohs(remote_socket->sin_port),
	    ntohs(local_socket->sin_port));
    fflush(sockstream);

    /*If alarm times out, it will interrupt fgets, and everything
      will just fall through.*/
    if (fgets(my_buf, sizeof(my_buf), sockstream) != 0 &&
	ferror(sockstream) == 0 && feof(sockstream) == 0 ) {
      /*Connection OK, now just grab info. */
      if(sscanf(my_buf, "%u , %u : USERID :%*[^:]:%255s", 
		&remoteport, &localport, user) == 3
	 && ntohs(local_socket->sin_port) == localport
	 && ntohs(remote_socket->sin_port) == remoteport
	 ) {

	if (copyptr = strchr(user, '\r'))
	  *copyptr = 0;
      }
      
    }
    
    signal(alarm, oldsignal);
    alarm(alarm_time);
  }

  fclose(sockstream);
  close(s); /*normally unnecessary*/
  iuid = getuid();
  sprintf(uid, "%d", iuid);
  if (!strcmp(getpwuid(iuid)->pw_name, user) 
      | !strcmp(uid, user)) 
    return(1);
  return(0);
}

#endif /*HAVE_IDENTD and NO_ROOT*/
