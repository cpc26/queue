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
#include <errno.h>
#include "queue_define.h"
#include "queue.h"

// packet to send over to the queue_manager
struct sockfd4_packet packet;

// prints out the command line options
void Usage();

void main(int argc, char ** argv)
{
   // socket variables  
   int sockfd;
   struct sockaddr_in serv_addr;
   struct hostent *host;

   // variables used to parse the command line 
   int i;
   extern int optind;
   char *optionstring = "abcdhklusm";

   // initialize the packet to some default values 
   strcpy(packet.job_id, "");
   strcpy(packet.host_license, "");
   strcpy(packet.message, "");
   strcpy(packet.licensefile, "");
   packet.maxlicense = 1;

   // (1) parse command line  
   while ((i = getopt_long(argc, argv, optionstring, NULL, NULL)) != EOF)
	switch(i) {
   case 'a': {
 	strcpy(packet.message, "add_host");	
	break;
   }
   case 'b': {
 	strcpy(packet.message, "delete_host");	
	break;
   }
   case 'c': {
 	strcpy(packet.message, "add_license");	
	break;
   }
   case 'd': {
 	strcpy(packet.message, "delete_license");	
	break;
   }
   case 'h': {
 	strcpy(packet.message, "high_priority");	
	break;
   }
   case 'k': {
 	strcpy(packet.message, "kill");
	break;
   }
   case 'l': {
	strcpy(packet.message, "low_priority");
	break;
   }
   case 'u': {
	strcpy(packet.message, "unsuspend");
	break;
   }
   case 's': {
	strcpy(packet.message, "suspend");
	break;
   }
   case 'm': {
   	Usage();
	return 0;
	}
   default:
	Usage();
	return -1;
	}

   // get the user id 
   packet.uid = getuid();

   // if the message is to add/delete hosts or licenses
   if ( !strcmp(packet.message, "add_host") || !strcmp(packet.message, "delete_host") || 
   	!strcmp(packet.message, "add_license") || !strcmp(packet.message, "delete_license") )
   {
   	strcpy(packet.host_license, argv[2]);  // get the host or license 

	// lowercase the string
	for (int i = 0; i < strlen(packet.host_license); i++)
	   packet.host_license[i] = tolower(packet.host_license[i]);

	// check if user specified license file or number of licenses to add/delete 
	if (argc == 5)
	{
	   packet.maxlicense = atoi(argv[3]);
	   strcpy(packet.licensefile, argv[4]);
	}
	else if (argc == 4)
	{
	   // check if the fourth argument is a number or a license file
	   if ( argv[3][0] == '/' )
	      strcpy(packet.licensefile, argv[3]);
	   else
	      packet.maxlicense = atoi(argv[3]);
	}
   }
   else
   	strcpy(packet.job_id, argv[optind]);  // get the job id

   /* MY_DEBUG */
   /*
   cout << "packet.uid: " << packet.uid << ", "  
	<< "packet.job_id: " << packet.job_id << ", "
	<< "packet.host_license: " << packet.host_license << ", "
	<< "packet.maxlicense: " << packet.maxlicense << ", "
	<< "packet.licensefile: " << packet.licensefile << ", "
	<< "packet.message: " << packet.message << endl;
   */

   // (2) now make a connection to the queue_manager and send it the information 

   // get the IP address of the host name 
   if ( (host = gethostbyname(QMANAGERHOST)) == NULL )
   {
	perror("task_control: invalid host name!");
	return -1;
   }

   // fill in the server information so that we can connect to it 
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_port = htons(PORTNUM4);
   serv_addr.sin_addr = *((struct in_addr *)host->h_addr);

   /* We try to connect at most ten times to prevent an overloaded system from
      keeping us down. */ 
   int counter = 1;
   while ( counter <= 10 )
   {
	// open a TCP socket 
	if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
   	{
   	   perror("task_control: error on TCP socket()");
	   return -1;
   	}

   	// set socket options for TCP socket 
   	int sendbuff = 16384;
   	if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
   		(char *) &sendbuff, sizeof(sendbuff)) < 0 )
   	{
   	   perror("task_control: error on TCP setsock option");
	   return -1;
   	}

	// connect to the server 
	if ( connect(sockfd, (struct sockaddr *) &serv_addr,
			sizeof(serv_addr)) < 0 )
	{
	   counter++;
	   cerr << "task_control: Connecting to Queue_Manager..." << endl;
	   sleep(3);  // sleep a few seconds before trying to connect again
   	}
	else
	   break;
   }

   // Is queue manager down? 
   if ( counter == 11 )
   {
	cerr << "task_control: Queue_Manager is either down or is very busy.  "
	     << "Try again later." << endl; 
	return -1;
   }

   // send the packet 
   if ( send(sockfd, &packet, sizeof(packet), 0) < 0 )
   {
	perror("task_control: error on sending packet");
	return -1;
   }

   // (3) wait for an acknowledgement from queue_manager
   char ack_mesg[3];
   if ( recv(sockfd, ack_mesg, sizeof(ack_mesg), 0) < 0 )
   {
	perror("task_control: error on receving ack from queue_manager");
	return -1;
   } 

   // did we get an error message?
   if ( !strcmp(ack_mesg, "-1") )
   {
	cerr << "task_control: error -- invalid job id" << endl;
	return -1;
   }
   else if ( !strcmp(ack_mesg, "-2") )
   {
	cerr << "task_control: error -- permission denied" << endl;
	return -1;
   }
   else if ( !strcmp(ack_mesg, "-3") )
   {
	cerr << "task_control: error -- server already exists" << endl;
	return -1;
   }
   else if ( !strcmp(ack_mesg, "-4") )
   {
	cerr << "task_control: error -- server does not exists" << endl;
	return -1;
   }
   else if ( !strcmp(ack_mesg, "-5") )
   {
	cerr << "task_control: error -- license does not exists" << endl;
	return -1;
   }
   else if ( !strcmp(ack_mesg, "-6") )
   {
	cerr << "task_control: error -- no license file or invalid license file for license" << endl;
	return -1;
   }

   close(sockfd);
}

void Usage()
{
   cerr << "Usage:" << endl << "\ttask_control -<option> <job_id/server/license> [# of licenses] [license file]" << endl;
   cerr << "-k: kill job" << endl
   	<< "-s: suspend job" << endl
	<< "-u: unsuspend job" << endl
	<< "-l: change job to low priority" << endl
	<< "-h: change job to high priority" << endl
	<< "-a: add a server (must be root)" << endl
	<< "-b: delete a server (root)" << endl
	<< "-c: add a license (root)" << endl
	<< "-d: delete a license (root)" << endl
	<< "-m: This menu." << endl;
}


#else
main()
{
}
#endif /*NO_QUEUE_MANAGER*/
