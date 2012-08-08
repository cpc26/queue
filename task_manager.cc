/* 
 *  Copyright (C) 2000 Texas Instruments, Inc.
 *  Author: Monica Lau <mllau@alantro.com>
 *
 *  This file is part of the GNU Queue package, http://www.gnuqueue.org
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation.
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
 *  Please report GNU Queue bugs to <bug-queue@gnu.org>.
 *
 **************************************************************************/

#include "config.h"
#ifndef NO_QUEUE_MANAGER

#include <iostream.h> 
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include "queue_define.h"

// used to get the pid of the actual job, not the forked off queue  
void get_jobpid(char buffer[], int job_pid);

void main()
{
	struct tm_packet packet;  // packet to receive from the queue_manager

   	// socket variables
   	int sockfd, new_sockfd;
   	struct sockaddr_in serv_addr, cli_addr;
   	unsigned int clilen = sizeof(cli_addr);

   	// bind our local address so that clients can send to us
   	serv_addr.sin_family = AF_INET;
   	serv_addr.sin_port = htons(C_PORTNUM);
   	serv_addr.sin_addr.s_addr = INADDR_ANY;

   	// open a TCP socket
   	if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
   	{
      	   perror("task_manager: error on getting TCP socket");
      	   return -1;
   	}

   	// set socket option for TCP socket
   	int sendbuff = 16384;
 	if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
		(char *) &sendbuff, sizeof(sendbuff)) < 0 )
   	{
      	   perror("task_manager: error on TCP setsock option");
      	   return -1;
   	}

   	// do a bind() on socket
   	if ( bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0 )
   	{
      	   perror("task_manager: error on bind()");
      	   return -1;
   	}
	
   	// do a listen to indicate that server is ready to receive connections
   	if ( listen(sockfd, QUEUELEN) < 0 )
   	{
      	   perror("task_manager: error on listen()");
      	   return -1;
   	}

   	// (1) Now just wait for any connections from the queue_manager.
   	for(;;)
   	{
	   // accept a connection
	   if ( (new_sockfd = accept(sockfd, (struct sockaddr *) &cli_addr,
						&clilen)) < 0 )
	   {
	   	perror("task_manager: error on accepting socket");
	   	return -1;
  	   }

	   /* MY_DEBUG */
	   cout << "Connection from: " << inet_ntoa(cli_addr.sin_addr) << endl;

	   // (2) get the packet structure from the queue_manager
	   if ( recv(new_sockfd, &packet, sizeof(packet), 0) < 0 )
	   {
	   	perror("task_manager: error on receiving packet");
	   	return -1;
	   }

	   // close off the connection
	   close(new_sockfd);

	   // MY_DEBUG
	   cout << "packet.job_pid: " << packet.job_pid << endl;
	   cout << "packet.message: " << packet.message << endl;

	   // (3) get the pid of the actual job, not the forked-off queued
	
   	   // variables used in the process to get the pid of the actual job 
	   int job_pid; // pid of the actual job 
   	   char buffer[10]; 

	   get_jobpid(buffer, packet.job_pid); 

   	   // (3a) store the actual job pid 
   	   job_pid = atoi(buffer);

   	   /* MY_DEBUG */
   	   cout << "job_pid: " << job_pid << endl;

	   // if the job_pid is less than 0, don't use "kill" at all (very dangerous)
	   if (job_pid <= 0)
	   	continue;
	
	   // (4) Send signal to the actual job, depending on what message we get.  (Note that
    	   // if we get a -1 value from kill(), this means that the process does not exist.)
	   if ( !strcmp(packet.message, "suspend") )
	   {
	   	cout << "suspend here!" << endl;
	   	if ( kill(job_pid, SIGSTOP) < 0 )
	   	{
	      	   perror("task_manager: error on sending SIGSTOP");
	   	}
	   }
	   else if ( !strcmp(packet.message, "unsuspend") )
	   {
	   	cout << "unsuspend here!" << endl;
	   	if ( kill(job_pid, SIGCONT) < 0 )
	   	{
	      	   perror("task_manager: error on sending SIGCONT");
	   	}
	   }
	   else if ( !strcmp(packet.message, "kill") )
	   {
	   	cout << "kill here!" << endl;
	   	// try killing the job first with SIGTERM
	   	if ( kill(job_pid, SIGTERM) < 0 )
	   	{
	      	   perror("task_manager: error on sending SIGTERM");
	   	}

	   	// if job still exists, kill it with SIGKILL
	   	if ( kill(job_pid, SIGKILL) < 0 )
	   	{
	      	   // Process does not exist.  SIGTERM did the job. 
	   	} 
	   }

	}  // closes for loop
  
}

// function used to get the pid of the actual job, not the forked off queue daemon
void get_jobpid(char buffer[], int job_pid)
{
   int n, pid, pfd[2];
   char pidcmd[100];

   // command used to get the pid of the actual job
   strcpy(pidcmd, "ps alx | grep -v 'ps alx' | grep");
   sprintf(pidcmd, "%s %d", pidcmd, job_pid);
   sprintf(pidcmd, "%s %s", pidcmd, "| awk '{print $3}' | grep -v");
   sprintf(pidcmd, "%s %d", pidcmd, job_pid);

   if ( pipe(pfd) == -1 )
   {
	perror("task_manager: error on pipe");
        return;
   }

   if ( (pid = fork()) == 0 )
   { // child process 
        close(pfd[0]);
        close(1);
        dup(pfd[1]);
        if ( system(pidcmd) == -1 )
        {
	   perror("task_manager: error on system");
	   return;
        }

	// this command is simply to ensure that the parent process does not block
	// forever if the actual job pid does not exist (doesn't block on reading
	// the pipe)
	if ( system("pwd") == -1 )
	{
	   perror("task_manager: error on pwd system");
	   return;
	}

        exit(0);
   }
   else if ( pid == -1 )
   {
        perror("task_manager: error on fork()");
        return;
   }

   // parent process  

   // initialize the buffer to a bunch of letters (non-digits)
   strcpy(buffer, "abcdefghi");

   // read from the pipe
   if ( (n = read(pfd[0], buffer, 10)) < 0 )
   {
         perror("task_manager: error on reading from pipe");
         return;
   }
   buffer[n] = '\0';

   // parse the buffer to cut off the parts that contain letters and not digits
   for (int i = 0; i < sizeof(buffer); i++)
   {
   	if ( !isdigit(buffer[i]) )
	{
	   buffer[i]='\0';
	   break;
	}
   }

   close(pfd[1]);
   close(pfd[0]);
   wait(NULL);
}
#else
main()
{
}
#endif NO_QUEUE_MANAGER
