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

#include <fstream.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <grp.h>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <iterator>
#include <algorithm>
#include "queue_define.h"

/* VARIOUS STRUCTURES */

// packet used to keep track of the license(s) that we've confiscated
// from a low-priority running job
struct conflic_packet
{
   int job_pid;  // pid of the forked off queue daemon
   vector<string> license_vector;
};

// used to keep track of which queue the job is in and which host it is running on
struct task_packet
{
   int uid;  // user id of the job
   string queue_type;  // type of queue the job is in
   string hostname;  // host that job is running on
};

// contains all the information that we need for the communication mechanism
struct sockfd5_commun
{
   int counter;  // number of times that we've received the same information from the queue daemon in a row
   int startup;  // when the queue_manager starts up and hears from this queue daemon for the first time
   struct timeval time;  // stores the time
};

// contains all the information that we need about each job
struct job_info
{
   string host_running;  // host that job is running on
   string status;  // job is either running, waiting, or sleeping
   int job_pid;
   int batch_pid;  // child process id, so that we can do a waitpid() on it
   int sockfd;  // need to save socket connection if in interac. mode and no hosts
   string job_id;  // this is a unique id that identifies the job
   vector<string> license_vector;
   struct info_packet packet;
   map<string, conflic_packet> conf_license;  // contains confiscated license(s) info.
   list<string> avail_license_erase;  // pertains to confiscated license(s)
   int confiscated;  // tells us if this job has licenses confiscated or not
   vector<string> vec_arge;  // user's environment (for batch jobs)
};

/* GLOBAL VARIABLES */

// create available host names and available licenses data structures 
list<string> avail_hosts;   
list<string> valid_hosts;
map<string, int> avail_licenses;   
map<string, string> license_files;

// create the "queues"
map<string, job_info> high_running;
map<string, job_info> low_running;
map<string, job_info> intermediate; 
list<job_info> high_waiting;
list<job_info> low_waiting;

// a data structure to keep track of which queue the job belongs to and which host
// it is running on (used for book-keeping purposes for the task_usercontrol and 
// task_manager processes)
map<string, task_packet> job_find;    

// a list of defective servers (note: a server is defective if either its queue daemon or 
// its task_manager is down)
list<string> defective_hosts;

// a list of job messages from users that we can't process yet
list<sockfd4_packet> job_messages;

// a list of servers running the queue daemons for the communication mechanism
map<string, sockfd5_commun> communicate;

// host to run job on
char assigned_host[MAXHOSTNAMELEN];  

// counter used to ensure that the procedures after the "select" timeout will be
// executed (since it's possible that these procedures can starve without this counter)
int starve_counter = 0;

/* FUNCTIONS */

int hlavail_i(job_info temp_job, int new_sockfd, list<string>::iterator it_find );
int hlavail_b(job_info temp_job, int waitq, list<string>::iterator it_find );
int hl_unavail(job_info temp_job, int new_sockfd, int waitq, list<string>::iterator it_find);
int tm_connect(string hostname, struct tm_packet tmpacket);
int task_control(struct sockfd4_packet packet, map<string, task_packet>::iterator it_jobfind);
int defective_server(string server);
int unsuspend_jobs(map<string, int> unsuspend_servers);
int check_wait();
int check_queued();
int check_jobmsg();
int create_newjob(struct sockfd3_packet packet);
int move_job(map<string, job_info>::iterator it_find, struct sockfd3_packet packet);
void sigpipe_handler(int);
int lmleft (const char* given_licfile, const char* given_feature);

/* FILES */

#ifdef DEBUG
ofstream fout_debug;
int display_debug();
void hdisplay_debug(job_info);
#endif

// file that contains information about the status of jobs
FILE *fout_status;
int display_status();
void hdisplay_status(job_info);

// "temp" file is used to output and read in the pid values of batch jobs, so that if 
// we need to manually kill the job we can (otherwise, we run into the problem of 
// waiting forever for the child process to terminate)
ifstream fin;

int main()
{
   /* (1) Initialize the data structures. */

   // initialize: read the valid host names from the file into valid_hosts 
   {
	string temp;
	ifstream fin(AVAILHOSTS);

	if ( fin.bad() )
	{
	   cerr << "Can't open " << AVAILHOSTS << " ";
	   perror("for reading");
	   return -1;
 	}

	while ( fin >> temp )
  	{
	   valid_hosts.push_back(temp);
   	}

	fin.close();
   }

   // initialize: read the available licenses from the file into avail_licenses
   {
	string temp, i, licensefile;
	ifstream fin(AVAILLICENSES);

	if ( fin.bad() )
	{
	   cerr << "Can't open " << AVAILLICENSES << " ";
	   perror("for reading");
	   return -1;
 	}

	while ( fin >> temp >> i >> licensefile)
	{
	   // check if the license file and the license feature are valid
	   if ( lmleft(licensefile.c_str(), temp.c_str()) < 0 )
	   {
	      cerr << "Invalid license file or license feature" << endl;
	      return -1;
	   }

	   avail_licenses.insert( map<string, int>::
					value_type(temp, atoi(i.c_str())) );
	   license_files.insert( map<string, string>::value_type(temp, licensefile) );
	}

	fin.close();
   }

   // initialize: set up the communication data structure
   {
   	// get the current time
	struct timeval time;
	gettimeofday(&time, NULL);

	// temporary sockfd5_commun packet
	struct sockfd5_commun packet;
	packet.counter = 0;
	packet.startup = 1;
	packet.time = time;

	list<string>::iterator it_begin = valid_hosts.begin(), it_end = valid_hosts.end();
	while (it_begin != it_end)
	{
	   communicate.insert( map<string, sockfd5_commun>::value_type(*it_begin, packet) );
	   ++it_begin;
	}
   }

   /* (2) Open up four sockets to accept connections on: 
	  socket2 is from  queue.c for message bit, 
	  socket3 is from queued.c, socket4 is from task_usercontrol.cc,
	  socket5 is from queued.c for communication */

   int sockfd2, sockfd3, sockfd4, sockfd5; 
   struct sockaddr_in serv_addr2, serv_addr3, serv_addr4, serv_addr5, cli_addr;
   unsigned int clilen = sizeof(cli_addr);

   // Bind our local addresses so that the clients can send to us 
   serv_addr2.sin_family = AF_INET;
   serv_addr2.sin_port = htons(PORTNUM2);
   serv_addr2.sin_addr.s_addr = INADDR_ANY;

   serv_addr3.sin_family = AF_INET;
   serv_addr3.sin_port = htons(PORTNUM3);
   serv_addr3.sin_addr.s_addr = INADDR_ANY;

   serv_addr4.sin_family = AF_INET;
   serv_addr4.sin_port = htons(PORTNUM4);
   serv_addr4.sin_addr.s_addr = INADDR_ANY;

   serv_addr5.sin_family = AF_INET;
   serv_addr5.sin_port = htons(PORTNUM5);
   serv_addr5.sin_addr.s_addr = INADDR_ANY;

   // open up three TCP sockets
   if ( (sockfd2 = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
   {
   	perror("Error on second socket()"); 
	return -1;
   }

   if ( (sockfd3 = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
   {
   	perror("Error on third socket()");
	return -1;
   }

   if ( (sockfd4 = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
   {
   	perror("Error on fourth socket()");
	return -1;
   }

   if ( (sockfd5 = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
   {
   	perror("Error on fifth socket()");
	return -1;
   }

   // set socket options for TCP sockets 
   int sendbuff = 16384;

   if ( setsockopt(sockfd2, SOL_SOCKET, SO_REUSEADDR,
   	(char *) &sendbuff, sizeof(sendbuff)) < 0 )
   {
   	perror("Error on second TCP setsock option");
	return -1;
   }

   if ( setsockopt(sockfd3, SOL_SOCKET, SO_REUSEADDR,
   	(char *) &sendbuff, sizeof(sendbuff)) < 0 )
   {
   	perror("Error on third TCP setsock option");
	return -1;
   }

   if ( setsockopt(sockfd4, SOL_SOCKET, SO_REUSEADDR,
   	(char *) &sendbuff, sizeof(sendbuff)) < 0 )
   {
   	perror("Error on fourth TCP setsock option");
	return -1;
   }

   if ( setsockopt(sockfd5, SOL_SOCKET, SO_REUSEADDR,
   	(char *) &sendbuff, sizeof(sendbuff)) < 0 )
   {
   	perror("Error on fifth TCP setsock option");
	return -1;
   }

   // do bind()'s on all five sockets  
   if ( bind(sockfd2, (struct sockaddr *) &serv_addr2, sizeof(serv_addr2)) < 0 )
   {
   	perror("Error on second bind()");
	return -1;
   }

   if ( bind(sockfd3, (struct sockaddr *) &serv_addr3, sizeof(serv_addr3)) < 0 )
   {
   	perror("Error on third bind()");
	return -1;
   }

   if ( bind(sockfd4, (struct sockaddr *) &serv_addr4, sizeof(serv_addr4)) < 0 )
   {
   	perror("Error on fourth bind()");
	return -1;
   }

   if ( bind(sockfd5, (struct sockaddr *) &serv_addr5, sizeof(serv_addr5)) < 0 )
   {
   	perror("Error on fifth bind()");
	return -1;
   }

   // do listen()'s to indicate that server is ready to receive connections 
   if ( listen(sockfd2, QUEUELEN) < 0 )
   {
   	perror("Error on second listen()");
	return -1;
   }

   if ( listen(sockfd3, QUEUELEN) < 0 )
   {
   	perror("Error on third listen()");
	return -1;
   }

   if ( listen(sockfd4, QUEUELEN) < 0 )
   {
   	perror("Error on fourth listen()");
	return -1;
   }

   if ( listen(sockfd5, QUEUELEN) < 0 )
   {
   	perror("Error on fith listen()");
	return -1;
   }

   /* (3) Now just wait for any connections */

   // set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
   signal(SIGPIPE, sigpipe_handler); 

   for(;;)
   {
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(sockfd2, &rfds);
	FD_SET(sockfd3, &rfds);
	FD_SET(sockfd4, &rfds);
	FD_SET(sockfd5, &rfds);

	struct timeval timer;
	timer.tv_sec = SLEEPTIME;
	timer.tv_usec = 0;

	// set the timer 
	if ( select(32, &rfds, NULL, NULL, &timer) < 0 )
	{
	   perror("Error on select()");
	   return -1;
	}

	// (3.1) Are there any connections from sockfd2?
	if ( FD_ISSET(sockfd2, &rfds) )
	{
	   struct job_info temp_job;  // job_info element to insert into some queue
	   struct task_packet task_job;  // element to insert into job_find
	   string user_jobid;  // the job id (must be unique) 
	   int new_sockfd;
	   int flag1 = 0;  // bit is on if user entered invalid hosts or licenses
	   int flag2 = 0;  // bit is on if licenses are not available
	   int flag3 = 0;  // bit is on if user's onlyhost is not available
	   int discard_job = 0;  // initialize variable to 0 

	   // Accept a connection; fills in client address information 
   	   if ( (new_sockfd = accept(sockfd2, (struct sockaddr *) &cli_addr,
  			&clilen)) < 0 )
   	   {
		perror("Error on accept() for sockfd2");
		return -1;
	   }

	   #ifdef DEBUG
	   cout << "Connection from: " << inet_ntoa(cli_addr.sin_addr) << endl;
	   #endif

	   // (a) get the packet structure first
   	   struct info_packet packet;
   	   if ( recv(new_sockfd, &packet, sizeof(packet), 0) == -1 )
   	   {
		perror("Error on receiving packet structure");
		close(new_sockfd);
		continue;
   	   }

	   int for_counter = 0;  // prevents infinite looping within the for loops
   	   // (b) next, get the list of licenses
   	   for(;;)
   	   {
		char buffer[MAXLICENSELEN];
		if ( recv(new_sockfd, buffer, sizeof(buffer), 0) == -1 )
		{
	   	   perror("Error on receiving list of licenses");
		   discard_job = 1;
		   break;
		}

		if ( !strstr(buffer, "EOF") )
	  	   temp_job.license_vector.push_back(buffer);
		else 
	   	   break;

	    	for_counter++;
		if (for_counter == MAXLICENSES) 
		   { discard_job = 1; cerr << "License for_counter" << endl; break; }
   	   }

	   #ifdef DEBUG
	   cout << "After getting licenses" << endl;
	   #endif

	   // (b.1) finally, if user specifies batch mode, need to get user's
	   // environment
	   if ( !strcasecmp(packet.mode, "batch") )
	   {
	      // (do later)  need to set a timer so that if we don't receive
	      // the user's environment within so many seconds, will discard
	      // job; this prevents us from looping forever

	      // store each environment string in temp_job's vector
	      for(;;)
	      {
	         char data[MAXSTRINGLEN];
		 if ( recv(new_sockfd, data, MAXSTRINGLEN, 0) < 0 )
		 {
		    perror("Error on receiving user's environment");
		    discard_job = 1;
		    break;
		 }

		 if ( !strstr(data, "EOF") )
		    temp_job.vec_arge.push_back(data);
		 else
		    break;

	      	 for_counter++;
		 if (for_counter == MAXENVIRON) 
		    { discard_job = 1; cerr << "Environment for_counter" << endl; break; }
	      }
	   }

	   // job needs to be discarded?
	   if ( discard_job ) 
	   {
		close(new_sockfd);  // closes off the socket connection
		continue;
	   }

	   #ifdef DEBUG
	   cout << "After getting environment" << endl;
	   #endif

	   // store the info packet in temp_job element
	   temp_job.packet = packet;
	   temp_job.confiscated = 0;  // initialize bit to 0
	   list<string>::iterator host_iter;  // a hack (fix later)

	   // (c) check if the user-entered hosts and licenses are valid
	   {
		map<string, int>::iterator license_iter;

		// (c1) check for vaild hosts first
		if ( strcasecmp(temp_job.packet.onlyhost, "") )
	 	{
		   host_iter = find(valid_hosts.begin(), valid_hosts.end(),
					temp_job.packet.onlyhost);

		   if ( host_iter == valid_hosts.end() )
			flag1 = 1;	
		   else
		   {    // now check if this "onlyhost" is available	
			host_iter = find(avail_hosts.begin(), avail_hosts.end(), 
						temp_job.packet.onlyhost);

			if ( host_iter == avail_hosts.end() )
			   flag3 = 1;		
			else
			   strcpy(assigned_host, host_iter->c_str());
		   }
		}	
		else if ( strcasecmp(temp_job.packet.prefhost, "") )
		{
		   host_iter = find(valid_hosts.begin(), valid_hosts.end(),
					temp_job.packet.prefhost);

		   if ( host_iter == valid_hosts.end() )
			flag1 = 1;		 
		}

		// (c2) now check for valid licenses
   		for (int i = 0; i < temp_job.license_vector.size(); i++)
		{
		   license_iter = avail_licenses.find(temp_job.license_vector[i]);

		   if ( license_iter == avail_licenses.end() )
		  	flag1 = 1;
		   else if ( license_iter->second <= 0 )
		   {	// this license is currently unavailable
			flag2 = 1;
		   }
		}

		// valid hosts and licenses?
	 	if ( flag1 == 1 )
		{
		   // invalid; send "-1" error message back to queue client
		   char buffer[MAXHOSTNAMELEN] = "-1";
   		   if ( send(new_sockfd, buffer, sizeof(buffer), 0) < 0 )
   		   {
			perror("Error on sending to queue client");
   		   }
	  	   
		   close(new_sockfd);
		   continue;	
		}		
   	   } 

	   // (d) create the job id (must be unique)
	   for(;;)
	   {
		// generate a random number
		long int rand_num = random() % MAXJOBIDMOD;

		// job_id = uid + random number
		char uid[10]; char rnum[30];
		sprintf(uid, "%d%s", temp_job.packet.user_id, "_");
		sprintf(rnum, "%ld", rand_num);

		// set user_jobid 
		user_jobid = uid;
		user_jobid = user_jobid + rnum;

	 	// set temp_job's job_id to user_jobid
		temp_job.job_id = user_jobid;

		// check to make sure that job id does not exist already
		map<string, task_packet>::iterator it_find = job_find.find(temp_job.job_id);
		if ( it_find == job_find.end() )
		   break;
	   }

	   // (e) if in batch mode, then send the job_id back to queue client and 
	   // close off socket connection
	   if ( !strcasecmp(temp_job.packet.mode, "batch") )
	   { 
	   	char buffer[MAXHOSTNAMELEN];
		strcpy(buffer, temp_job.job_id.c_str());
   		if ( send(new_sockfd, buffer, sizeof(buffer), 0) < 0 )
   		{
		   perror("Error on sending ack to queue client");
   		}
		close(new_sockfd);
	   }

	   // (f) put the job in the appropriate waiting queue, then call the
	   // check_wait() function to see if we can run any jobs

	   // if in interactive mode, save the socket descriptor so that
	   // we can connect back to this process later
	   if ( !strcasecmp(temp_job.packet.mode, "interactive") )
	   	temp_job.sockfd = new_sockfd;

	   // save other information about the job in temp_job
	   temp_job.host_running = "";
	   temp_job.status = "waiting";

	   // store the job in the appropriate queue
	   if ( !strcasecmp(temp_job.packet.priority, "high") )
	   {
	   	high_waiting.push_back(temp_job);
		task_job.queue_type = "high_waiting";  // get the queue_type of task_job
	   }
	   else
	   {
	   	low_waiting.push_back(temp_job);		
	   	task_job.queue_type = "low_waiting";  // get the queue_type of task_job
	   }

	   // insert task_job into job_find
	   task_job.uid = temp_job.packet.user_id;
	   job_find.insert( map<string, task_packet>::value_type(user_jobid, task_job) );

	   // call check_wait()
	   check_wait();

	   // check if we need to execute the procedures after the "select" timeout 
	   ++starve_counter;
	   if ( starve_counter == MAXSTARVECOUNTER )
	   {
	      starve_counter = 0;  // reset the variable
	      check_jobmsg();
	      display_status();
	      waitpid(-1, NULL, WNOHANG);  // see if there are any zombie processes we need to bury

	      #ifdef DEBUG
	      display_debug();
	      #endif
	   }

	   continue;  // continue checking for any connections

	} // closes FD_ISSET

	// (3.2) Are there any connections from sockfd3?
	if ( FD_ISSET(sockfd3, &rfds) )
	{
	   int new_sockfd;

	   // Accept a connection; fills in client address information.
	   if ( (new_sockfd = accept(sockfd3, (struct sockaddr *) &cli_addr,
			&clilen)) < 0 )
	   {
		perror("Error on accept() for sockfd3");
		return -1;
	   }

	   #ifdef DEBUG
	   cout << "Connection from: " << inet_ntoa(cli_addr.sin_addr) << endl;
	   #endif

	   // (a) get the message bit first to determine what kind of message this is
	   char msg_bit[2];
	   if ( recv(new_sockfd, msg_bit, sizeof(msg_bit), 0) == -1 )
	   {
		perror("Error on receiving message bit");
		continue;
	   }
	   
	   // next, get the packet 
	   struct sockfd3_packet packet; 
	   if ( recv(new_sockfd, &packet, sizeof(packet), 0) == -1 )
	   {
		perror("Error on receving packet within (3.2)");
		continue;
	   }

	   // close off the socket connection
	   close(new_sockfd); 

	   #ifdef DEBUG
	   cout << "message bit: " << msg_bit << endl;
	   cout << "packet: " << packet.hostname << " " << packet.job_pid << " " 
		<< packet.user_id << endl;
	   #endif

	   // (b) process the information according to its message type

	   if ( !strcasecmp(msg_bit, "1") )  
	   {
	   	// (b1) job is running, so move the job element from the intermediate queue
	   	// to the running queue

	   	map<string, job_info>::iterator it_find;
		it_find = intermediate.find(packet.hostname);

		// make sure that the job is in the intermediate queue before deleting anything 
		// (otherwise we'll get a bunch of segmentation faults)
		if ( it_find != intermediate.end() )
		   move_job(it_find, packet);
	   }
	   else if ( !strcasecmp(msg_bit, "2") )  
	   {
		// (b2) job has terminated: so remove this job element from the running queue,
		// update the avail_hosts and avail_licenses; if the job was in batch mode,
		// have to do a waitpid on this child to bury this zombie process

		int batchpid = -1;
	   	map<string, job_info>::iterator it_find;

		// check which queue this job element belongs to and delete it
		if ((it_find = intermediate.find(packet.hostname)) != intermediate.end())
		{
		   // need to bury zombie process?
		   if ( !strcasecmp(it_find->second.packet.mode, "batch") )
		      batchpid = it_find->second.batch_pid;

		   // update avail_hosts and avail_licenses
		   avail_hosts.push_back(packet.hostname);

		   for ( int i=0; i < it_find->second.license_vector.size(); i++ )
		   {
			map<string, int>::iterator license_iter;
			license_iter = avail_licenses.find(
						it_find->second.license_vector[i]);
			license_iter->second = license_iter->second + 1;
		   }

		   // delete element off of job_find as well
		   map<string, task_packet>::iterator it_jobfind;
		   it_jobfind = job_find.find(it_find->second.job_id);
		   job_find.erase(it_jobfind); 

		   intermediate.erase(it_find);
		}
		else if ((it_find = high_running.find(packet.hostname)) != high_running.end())
		{
		   // need to bury zombie process?
		   if ( !strcasecmp(it_find->second.packet.mode, "batch") )
		      batchpid = it_find->second.batch_pid;

		   // update avail_hosts 
		   avail_hosts.push_back(packet.hostname);

		   // check if we have confiscated any licenses
		   if ( !it_find->second.conf_license.empty() )
		   {
			// update avail_licenses if avail_license_erase is not empty
			list<string>::iterator 
				it_begin = it_find->second.avail_license_erase.begin(),
				it_end = it_find->second.avail_license_erase.end();

			while ( it_begin != it_end )
			{
			   map<string, int>::iterator license_iter;
			   license_iter = avail_licenses.find(*it_begin);
			   license_iter->second = license_iter->second + 1;
			   ++it_begin;
			}

			// now open up a connection to all the hosts in 
			// conf_license to "unsuspend" the suspended jobs
		        map<string, conflic_packet>::iterator 
					itb = it_find->second.conf_license.begin(),
					ite = it_find->second.conf_license.end();

			while ( itb != ite )
			{
			   struct tm_packet tmpacket;  // packet to send to task_manager
			   tmpacket.job_pid = itb->second.job_pid; 
			   strcpy(tmpacket.message, "unsuspend");

			   // call tm_connect function
			   if ( tm_connect(itb->first, tmpacket) == 0 )
			   {
			      // update the status of the confiscated license job
			      map<string, job_info>::iterator it_jfind =
							low_running.find(itb->first);
			      it_jfind->second.status = "running";
			      it_jfind->second.confiscated = 0;  // set bit back to 0
			   }

			   ++itb;
			} 
		   } 
		   else
		   {
		      #ifdef DEBUG 
		      cout << "No confiscated licenses" << endl;
		      #endif

		      // update avail_licenses
		      for ( int i=0; i < it_find->second.license_vector.size(); i++ )
		      {
			map<string, int>::iterator license_iter;
			license_iter = avail_licenses.find(
					 	it_find->second.license_vector[i]);
			license_iter->second = license_iter->second + 1;
		      }
		   }

		   // delete element off of job_find as well
		   map<string, task_packet>::iterator it_jobfind;
		   it_jobfind = job_find.find(it_find->second.job_id);
		   job_find.erase(it_jobfind); 

		   high_running.erase(it_find);
		}
		else if ((it_find = low_running.find(packet.hostname)) != low_running.end())
		{
		   // need to bury zombie process?
		   if ( !strcasecmp(it_find->second.packet.mode, "batch") )
		      batchpid = it_find->second.batch_pid;

		   // update avail_hosts and avail_licenses
		   avail_hosts.push_back(packet.hostname);

		   for ( int i=0; i < it_find->second.license_vector.size(); i++ )
		   {
			map<string, int>::iterator license_iter;
			license_iter = avail_licenses.find(
						it_find->second.license_vector[i]);
			license_iter->second = license_iter->second + 1;
		   }

		   // delete element off of job_find as well
		   map<string, task_packet>::iterator it_jobfind;
		   it_jobfind = job_find.find(it_find->second.job_id);
		   job_find.erase(it_jobfind); 

		   low_running.erase(it_find);
		}

		// need to bury zombie process?
		if ( batchpid != -1 )
		{
		   	// sometimes we have to manually kill off the forked off
		   	// queue_manager (better to do this way; otherwise,
		   	// waitpid will block forever waiting for the child to die)

		 	// the command to get the "sh..." pid first
			char pidcmd[100];
			strcpy(pidcmd, "ps alx | grep -v 'ps alx' | grep que | grep");
			sprintf(pidcmd, "%s %d %s", pidcmd, batchpid,
						"| awk '{print $3}' | grep -v");
			sprintf(pidcmd, "%s %d %s %s", pidcmd, batchpid, ">", TEMPFILE); 

			#ifdef DEBUG
			cout << pidcmd << endl;
			#endif

			if ( system(pidcmd) < 0 )
			   perror("Error on pidcmd system()");
			else
			{
   			   // now read in the value of the "sh" pid
   			   int sh_pid = -1;
   			   fin.open(TEMPFILE);

			   if ( !fin.bad() )
			   {
   			      fin >> sh_pid;
   			      fin.close();

   			      if ( sh_pid != -1 )
   			      {
			         char pidcmd[100];
			         strcpy(pidcmd, "ps alx | grep -v 'ps alx' | grep que | grep");
			         sprintf(pidcmd, "%s %d %s", pidcmd, sh_pid,
					"| awk '{print $3}' | grep -v");
			         sprintf(pidcmd, "%s %d %s %s", pidcmd, sh_pid, ">", TEMPFILE);

        		         if ( system(pidcmd) < 0 )
	   			   perror("Error on pidcmd system()");
			         else
			         {
			            // now get the actual pid that we need to kill the job
			            int kill_pid = -1;
			            fin.open(TEMPFILE);

				    if ( !fin.bad() )
				    {
			               fin >> kill_pid;
			               fin.close();

			               #ifdef DEBUG
			               cout << "kill_pid: " << kill_pid << endl;
			               #endif
	
			               // send the process the kill signal
				       if (kill_pid > 0)
				       { 
		  	                  kill( kill_pid, SIGTERM );  // try SIGTERM first
		  	                  kill( kill_pid, SIGKILL );
				       }
				    }
			         }
   			      } // closes off "if (sh_pid..."
			   }
			}

			// now bury this child	
			waitpid(batchpid, NULL, WNOHANG);

			#ifdef DEBUG
			cout << "after waiting on child process" << endl;
			#endif
		}

		// (c) Need to remove licenses?  (Ask Scott about this.) 
	   }

	   // check if we need to execute the procedures after the "select" timeout 
	   ++starve_counter;
	   if ( starve_counter == MAXSTARVECOUNTER )
	   {
	      starve_counter = 0;  // reset the variable
	      check_wait();
	      check_jobmsg();
	      display_status();
	      waitpid(-1, NULL, WNOHANG);  // see if there are any zombie processes we need to bury

	      #ifdef DEBUG
	      display_debug();
	      #endif
	   }

	   continue;  // continue checking for any connections

	} // closes FD_ISSET

	// (3.3) Are there any connections from sockfd4?
	if ( FD_ISSET(sockfd4, &rfds) )
	{
	   int new_sockfd;
	   char ack_mesg[3];  // ack message to send back to client

	   // accept a connection; fills in client address information
	   if ( (new_sockfd = accept(sockfd4, (struct sockaddr *) &cli_addr,
			&clilen)) < 0 )
	   {
		perror("Error on accept() for sockfd4");
		return -1;
	   }

	   #ifdef DEBUG
	   cout << "Connection from: " << inet_ntoa(cli_addr.sin_addr) << endl;
	   #endif

	   // (a) get the packet of information from client 
	   struct sockfd4_packet packet; 
	   if ( recv(new_sockfd, &packet, sizeof(packet), 0) == -1 )
	   {
		perror("Error on receving packet from sockfd4");
		close(new_sockfd);
		continue;
	   }

	   #ifdef DEBUG
	   cout << "In sockfd4: " << endl;
	   cout << packet.uid << ", " << packet.job_id << ", " << packet.host_license << ", "
	   	<< packet.maxlicense << ", " << packet.licensefile << ", " << packet.message << endl;
	   #endif

	   // (b) check what kind of message we have
	   if ( !strcmp(packet.message, "add_host") || !strcmp(packet.message, "delete_host") ||
	   	!strcmp(packet.message, "add_license") || !strcmp(packet.message, "delete_license") )
	   {
	      // this message is related to dynamically adding/deleting servers/licenses

	      // initialize the ack message
	      strcpy(ack_mesg, "1");  // everything is ok

	      // check if the user is root 
	      if ( packet.uid == 0 )
	      {
	         // if the message is to add a new host
		 if ( !strcmp(packet.message, "add_host") )
		 {
		    // if the host already exist, this is an error
		    list<string>::iterator it_addhost = find(valid_hosts.begin(), valid_hosts.end(),
		    						packet.host_license);
		    if (it_addhost == valid_hosts.end())
		    {
		       // add this new server to valid_hosts list but not to avail_hosts list yet
		       valid_hosts.push_back(packet.host_license);

		       // add this server to the communicate data structure
   		       // get the current time
		       struct timeval time;
		       gettimeofday(&time, NULL);

		       // temporary sockfd5_commun packet
		       struct sockfd5_commun sockfd5_packet;
		       sockfd5_packet.counter = 0;
		       sockfd5_packet.startup = 1;
		       sockfd5_packet.time = time;
	               communicate.insert( map<string, sockfd5_commun>::value_type(packet.host_license, 
		       							sockfd5_packet) );

		       // finally, check if this server is in the defective server list; if so, 
		       // delete this server from this list
		       list<string>::iterator host_iter =
		       	 find(defective_hosts.begin(), defective_hosts.end(), packet.host_license);
		       if (host_iter != defective_hosts.end())
		          defective_hosts.erase(host_iter);
		    }
		    else
		       strcpy(ack_mesg, "-3");  // host already exists
		 }
		 else if ( !strcmp(packet.message, "delete_host") )
		 {
		    // call the "defective_server" function
		    if ( defective_server(packet.host_license) < 0 )
		       strcpy(ack_mesg, "-4");  // host does not exists
		 }
		 else if ( !strcmp(packet.message, "add_license") )
		 {
		    map<string, int>::iterator it_addlicense = avail_licenses.find(packet.host_license);

		    // if license already exists, increment its counter by packet.maxlicense
		    if ( it_addlicense != avail_licenses.end() )
		    {
		       it_addlicense->second = it_addlicense->second + packet.maxlicense;

		       // if the license file is not equal to "", then update license file if file is valid
		       if ( strcmp(packet.licensefile, "") )
		       {
		          // check if packet's license file is valid
	   		  if ( lmleft(packet.licensefile, packet.host_license) >= 0 )
			  {
		             map<string, string>::iterator it_licensefile = 
			  				license_files.find(packet.host_license);
			     if (it_licensefile != license_files.end())
			        it_licensefile->second = packet.licensefile;
			  }
		       }
		    }
		    else  // license does not exist, so create a new license with total of packet.maxlicense 
		    {
		       // if the packet's license file is equal to "", this is an error 
		       if ( strcmp(packet.licensefile, "") )
		       {
		          // check if packet's license file is valid 
	   		  if ( lmleft(packet.licensefile, packet.host_license) < 0 )
			     strcpy(ack_mesg, "-6");  // invalid license file
			  else
			  {
		             avail_licenses.insert( map<string, int>::value_type(packet.host_license, 
		       									packet.maxlicense) );
			     license_files.insert( map<string, string>::value_type(packet.host_license,
			  						packet.licensefile) );
			  }
		       }
		       else
		          strcpy(ack_mesg, "-6");  // no license file
		    }
		 }
		 else if ( !strcmp(packet.message, "delete_license") )
		 {
		    // if license does not exist, this is an error
		    map<string, int>::iterator it_dlicense = avail_licenses.find(packet.host_license);

		    if (it_dlicense != avail_licenses.end())
		    {
		       it_dlicense->second = it_dlicense->second - packet.maxlicense;

		       // if the license file is not equal to "", then update license file if file is valid
		       if ( strcmp(packet.licensefile, "") )
		       {
		          // check if packet's license file is valid
	   		  if ( lmleft(packet.licensefile, packet.host_license) >= 0 )
			  {
		             map<string, string>::iterator it_licensefile = 
			  				license_files.find(packet.host_license);
			     if (it_licensefile != license_files.end())
			        it_licensefile->second = packet.licensefile;
			  }
		       }
		    }
		    else
		       strcpy(ack_mesg, "-5");  // license does not exists
		 }
	      }
	      else
	         strcpy(ack_mesg, "-2");  // permission denied

	      // send ack back to client 
	      if ( send(new_sockfd, ack_mesg, sizeof(ack_mesg), 0) < 1 )
	      {
		 perror("Error on sending acknowledgement to sockfd4");
	      }

	      // close off the socket connection
	      close(new_sockfd);
	   }
	   else
	   {
	      // this message is job related

	      map<string, task_packet>::iterator it_jobfind;
	      it_jobfind = job_find.find(packet.job_id);

	      if ( it_jobfind == job_find.end() )
		 strcpy(ack_mesg, "-1");  // job does not exist
	      else if ( (packet.uid == 0) || (packet.uid == it_jobfind->second.uid) )
		 strcpy(ack_mesg, "1");  // everything is ok
	      else
		 strcpy(ack_mesg, "-2");  // permission denied

	      // send ack back to client
	      if ( send(new_sockfd, ack_mesg, sizeof(ack_mesg), 0) < 1 )
	      {
		 perror("Error on sending acknowledgement to sockfd4");
	      } 

	      // close off the socket connection
	      close(new_sockfd);

	      // should we go on or stop now?
	      if ( (!strcasecmp(ack_mesg, "-1")) || (!strcasecmp(ack_mesg, "-2")) )
		continue;

	      // if job has license(s) confiscated or if job is in the intermediate queue,
	      // then we can't process this message yet
	      if ( it_jobfind->second.queue_type == "intermediate" )
	      {
	         // if the message is not to change the priority of the job
		 if ( strcmp(packet.message, "high_priority") && strcmp(packet.message, "low_priority") )
		 {
	            job_messages.push_back(packet);
		    continue;
		 }
	      }
	      else
	      {
	         // check if job has licenses confiscated
		 map<string, job_info>::iterator iter_find;
	         if ( it_jobfind->second.queue_type == "low_running" )
		 {
		    iter_find = low_running.find(it_jobfind->second.hostname);
		    if ( iter_find != low_running.end() )
		       if ( iter_find->second.confiscated )
		       {
		          job_messages.push_back(packet);
			  continue;
		       }
		 }
	      }

	      // call the task_control function to process the message
	      task_control(packet, it_jobfind);
	   }

	   // check if we need to execute the procedures after the "select" timeout 
	   ++starve_counter;
	   if ( starve_counter == MAXSTARVECOUNTER )
	   {
	      starve_counter = 0;  // reset the variable
	      check_wait();
	      check_jobmsg();
	      display_status();
	      waitpid(-1, NULL, WNOHANG);  // see if there are any zombie processes we need to bury

	      #ifdef DEBUG
	      display_debug();
	      #endif
	   }

	   continue;  // continue checking for any connections

	} // closes FD_ISSET

	// (3.4) Are there any connections from sockfd5?
	if ( FD_ISSET(sockfd5, &rfds) )
	{
	   int new_sockfd;

	   // accept a connection; fills in client address information
	   if ( (new_sockfd = accept(sockfd5, (struct sockaddr *) &cli_addr,
			&clilen)) < 0 )
	   {
		perror("Error on accept() for sockfd5");
		return -1;
	   }

	   #ifdef DEBUG
	   cout << "Connection from: " << inet_ntoa(cli_addr.sin_addr) << endl;
	   #endif

	   // (a) get the packet of information from client 
	   struct sockfd3_packet packet; 
	   if ( recv(new_sockfd, &packet, sizeof(packet), 0) == -1 )
	   {
		perror("Error on receving packet from sockfd3");
		close(new_sockfd);
		continue;
	   }

	   // close off the socket connection
	   close(new_sockfd); 

	   #ifdef DEBUG
	   cout << "In sockfd5: " << endl;
	   cout << "packet: " << packet.hostname << " " << packet.job_pid << " " 
		<< packet.user_id << endl;
	   #endif

	   // find this server in the communicate data structure
	   map<string, sockfd5_commun>::iterator itc_find = communicate.find(packet.hostname);
	   if (itc_find != communicate.end())
	   {
	      // reset the timer for this server
	      struct timeval time;
	      gettimeofday(&time, NULL);
	      itc_find->second.time = time;

	      // We have serveral cases to consider now: 
	      // (a) server is in the avail_hosts list and queued says no job is running
	      // (b) server is in the avail_hosts list and queued says a job is running
	      // (c) server is not in the avail_hosts list and queued says no job is running
	      // (d) server is not in the avail_hosts list and queued says a job is running.
	      // Case (a) is ok.  We have to worry about cases (b), (c), and (d).
	      
	      list<string>::iterator it_hosts = 
		   find(avail_hosts.begin(), avail_hosts.end(), itc_find->first);
	      if (it_hosts != avail_hosts.end())
	      {
	         // server is in the avail_hosts list, so just reset its counter
		 itc_find->second.counter = 0;

		 // (b) if queue daemon says that a job is running, this is a problem -- handle this!
		 if (packet.job_pid != 0)
		 {
		    // create a new job and put this job in the high_running queue -- problem
		    // is that I would not know how many licenses are available (root would have
		    // to update the licenses data structure manually)

   		    // delete this host name off of avail_hosts
		    avail_hosts.erase(it_hosts);

		    // call the create_newjob() function 
		    create_newjob(packet);
		 }
	      }
	      else
	      {
	         // server is not in the avail_hosts list, so check if queue daemon says if a job 
		 // is running or not

		 if (packet.job_pid != 0)
		 {
		    // (d) job is running, so just reset the counter
		    itc_find->second.counter = 0;

		    // set its startup bit to 0
		    itc_find->second.startup = 0;
		    
		    // check if job is in any of the running or intermediate queues; if 
		    // job is not in any of these queues, then we simply create a new
		    // job and put it in the high_running queue  (Note: this
		    // check is essentially used when the queue_manager first fires up.)
		    int createjob = 1;
   		    map<string, job_info>::iterator it_jobfind;
   		    if ((it_jobfind = high_running.find(itc_find->first)) != high_running.end())
		       createjob = 0;
   		    else if ((it_jobfind = low_running.find(itc_find->first)) != low_running.end())
		       createjob = 0;
   		    else if ((it_jobfind = intermediate.find(itc_find->first)) != intermediate.end())
		    {
		       createjob = 0;

		       // move the job to the high running queue
		       move_job(it_jobfind, packet);
		    }

		    if (createjob)  // need to create a new job
		       create_newjob(packet);
		 }
		 else  
		 {
		    // (c) job is not running 

		    // if this is the first time that we've heard from this queue daemon, then
		    // just put this server in the avail_hosts list and set its startup bit to 0
		    if (itc_find->second.startup)
		    {
		       avail_hosts.push_back(itc_find->first);
		       itc_find->second.startup = 0;
		       continue;
		    }

		    // The bad case:
		    // first, increment its counter; if the counter is equal to MAXQUEUEDCOUNTER,
		    // then update the data structures and reset counter
		    itc_find->second.counter = itc_find->second.counter + 1;
		    if (itc_find->second.counter == MAXQUEUEDCOUNTER)  
		    {
		       // reset the counter
		       itc_find->second.counter = 0;

		       // update avail_hosts 
		       avail_hosts.push_back(itc_find->first);
	
   		       // check the running queues and the intermediate queue to see if there is 
   		       // a job running on this server; if there is, delete this job from the queue

		       // Note: If batch job, need to wait on child process.  If this job
		       // has confiscated any licenses, needs to unsuspend all the suspended processes.
		       // (Haven't put in the code for this yet.)
		       int batchpid = -1;
   		       map<string, job_info>::iterator it_jobfind;

   		       if ((it_jobfind = high_running.find(itc_find->first)) != high_running.end())
   		       {
			  // delete the element off of job_find as well
			  map<string, task_packet>::iterator it_find;
			  it_find = job_find.find(it_jobfind->second.job_id);
			  job_find.erase(it_find);
	
		 	  // update the avail_licenses
		   	  for ( int i=0; i < it_jobfind->second.license_vector.size(); i++ )
		   	  {
				map<string, int>::iterator license_iter;
				license_iter = avail_licenses.find(
						it_jobfind->second.license_vector[i]);
				license_iter->second = license_iter->second + 1;
		   	  }

		  	  // need to bury zombie process?
			  if ( !strcasecmp(it_jobfind->second.packet.mode, "batch") )
	   		     batchpid = it_jobfind->second.batch_pid;

			  high_running.erase(it_jobfind);
   		       }
   		       else if ((it_jobfind = low_running.find(itc_find->first)) != low_running.end())
   		       {
  			  // delete the element off of job_find as well
			  map<string, task_packet>::iterator it_find;
			  it_find = job_find.find(it_jobfind->second.job_id);
			  job_find.erase(it_find);

		 	  // update the avail_licenses
		   	  for ( int i=0; i < it_jobfind->second.license_vector.size(); i++ )
		   	  {
				map<string, int>::iterator license_iter;
				license_iter = avail_licenses.find(
						it_jobfind->second.license_vector[i]);
				license_iter->second = license_iter->second + 1;
		   	  }

		  	  // need to bury zombie process?
			  if ( !strcasecmp(it_jobfind->second.packet.mode, "batch") )
	   		     batchpid = it_jobfind->second.batch_pid;

			  low_running.erase(it_jobfind);
   		       }
   		       else if ((it_jobfind = intermediate.find(itc_find->first)) != intermediate.end())
   		       {
  			  // delete the element off of job_find as well
			  map<string, task_packet>::iterator it_find;
			  it_find = job_find.find(it_jobfind->second.job_id);
			  job_find.erase(it_find);

		 	  // update the avail_licenses
		   	  for ( int i=0; i < it_jobfind->second.license_vector.size(); i++ )
		   	  {
				map<string, int>::iterator license_iter;
				license_iter = avail_licenses.find(
						it_jobfind->second.license_vector[i]);
				license_iter->second = license_iter->second + 1;
		   	  }

		  	  // need to bury zombie process?
			  if ( !strcasecmp(it_jobfind->second.packet.mode, "batch") )
	   		     batchpid = it_jobfind->second.batch_pid;

			  intermediate.erase(it_jobfind);
   		       }

	// need to bury zombie process?
	if ( batchpid != -1 )
   	{
	// sometimes we have to manually kill off the forked off
	// queue_manager (better to do this way; otherwise,
	// waitpid will block forever waiting for the child to die)

 	// the command to get the "sh..." pid first
	char pidcmd[100];
	strcpy(pidcmd, "ps alx | grep -v 'ps alx' | grep que | grep");
	sprintf(pidcmd, "%s %d %s", pidcmd, batchpid,
					"| awk '{print $3}' | grep -v");
	sprintf(pidcmd, "%s %d %s %s", pidcmd, batchpid, ">", TEMPFILE);

	#ifdef DEBUG
	cout << pidcmd << endl;
	#endif

	if ( system(pidcmd) < 0 )
	   perror("Error on pidcmd system()");
	else
	{
   	   // now read in the value of the "sh" pid
   	   int sh_pid = -1;
   	   fin.open(TEMPFILE);

	   if ( !fin.bad() )
	   {
   	      fin >> sh_pid;
   	      fin.close();

   	      if ( sh_pid != -1 )
   	      {
	         char pidcmd[100];
	         strcpy(pidcmd, "ps alx | grep -v 'ps alx' | grep que | grep");
	         sprintf(pidcmd, "%s %d %s", pidcmd, sh_pid,
			"| awk '{print $3}' | grep -v");
	         sprintf(pidcmd, "%s %d %s %s", pidcmd, sh_pid, ">", TEMPFILE);

  	         if ( system(pidcmd) < 0 )
  		   perror("Error on pidcmd system()");
	         else
	         {
	            // now get the actual pid that we need to kill the job
	            int kill_pid = -1;
	            fin.open(TEMPFILE);

		    if ( !fin.bad() )
		    {
	               fin >> kill_pid;
	               fin.close();

		       #ifdef DEBUG
		       cout << "kill_pid: " << kill_pid << endl;
		       #endif
	
		       // send the process the kill signal
		       if (kill_pid > 0)
		       { 
		          kill( kill_pid, SIGTERM );  // try SIGTERM first
	 	          kill( kill_pid, SIGKILL );
		       }
		    }
	         }
   	      } // closes off "if (sh_pid..."
	   }
	}

	// now bury this child	
	waitpid(batchpid, NULL, WNOHANG);
	
	#ifdef DEBUG
	cout << "after waiting on child process" << endl;
	#endif
   }

		    }
		 }
	      }
	   }

	   // check if we need to execute the procedures after the "select" timeout 
	   ++starve_counter;
	   if ( starve_counter == MAXSTARVECOUNTER )
	   {
	      starve_counter = 0;  // reset the variable
	      check_wait();
	      check_jobmsg();
	      display_status();
	      waitpid(-1, NULL, WNOHANG);  // see if there are any zombie processes we need to bury

	      #ifdef DEBUG
	      display_debug();
	      #endif
	   }

	   continue;  // continue checking for any connections

	} // closes FD_ISSET

	#ifdef DEBUG
	cout << "Timed Out" << endl; 
	#endif

	// reset the starve_counter variable first
	starve_counter = 0;

	/* (4) Check if we can run any jobs in the waiting queues */
	check_wait();

	/* (5) Check if we can process any messages in the job_messages data structure. */
	check_jobmsg();

	/* (6) Update the status file. */
	display_status();

	/* (7) See if there are any zombie processes we need to bury. */
	waitpid(-1, NULL, WNOHANG);  

	#ifdef DEBUG
	display_debug();
	#endif

	/* (8) Check if any of the queue daemons are down. */
	check_queued();	

   }  // closes for loop

   return 0;
}

// updates the status file
int display_status()
{
   // open the status file
   if ( (fout_status = fopen(STATUSFILE, "w+")) == NULL )
   {
      perror("Can't open status file");
      return -1;
   }

   // print out the available servers
   list<string>::iterator list_begin, list_end;
   map<string, int>::iterator map_begin, map_end;

   fprintf(fout_status, "%s ", "AVAILABLE SERVERS:");
   list_begin = avail_hosts.begin(); list_end = avail_hosts.end();
   while ( list_begin != list_end )
   {
	fprintf(fout_status, "%s ", list_begin->c_str()); 
	++list_begin;
   }
   fprintf(fout_status, "\n");

   // print out the available licenses
   fprintf(fout_status, "%s", "AVAILABLE LICENSES: ");
   map_begin = avail_licenses.begin(); map_end = avail_licenses.end();
   while ( map_begin != map_end )
   {
	fprintf(fout_status, "%s %d ", map_begin->first.c_str(), map_begin->second); 
	++map_begin;
   }
   fprintf(fout_status, "\n");

   // print out the valid servers
   fprintf(fout_status, "%s ", "VALID SERVERS: ");
   list_begin = valid_hosts.begin(); list_end = valid_hosts.end();
   while ( list_begin != list_end )
   {
	fprintf(fout_status, "%s ", list_begin->c_str()); 
	++list_begin;
   }
   fprintf(fout_status, "\n");

   // print out the defective servers
   fprintf(fout_status, "%s ", "DEFECTIVE SERVERS: ");
   list_begin = defective_hosts.begin(); list_end = defective_hosts.end();
   while ( list_begin != list_end )
   {
	fprintf(fout_status, "%s ", list_begin->c_str()); 
	++list_begin;
   }
   fprintf(fout_status, "\n\n");

   // print out the number of jobs 
   fprintf(fout_status, "%s\n", "NUMBER OF JOBS SUBMITTED");
   fprintf(fout_status, "%s\n", "------------------------");
   fprintf(fout_status, "%s %d\n", "HIGH PRIORITY RUNNING JOBS:", high_running.size());
   fprintf(fout_status, "%s %d\n", "LOW PRIORITY RUNNING JOBS:", low_running.size());
   fprintf(fout_status, "%s %d\n", "INTERMEDIATE JOBS:", intermediate.size());
   fprintf(fout_status, "%s %d\n", "HIGH PRIORITY WAITING JOBS:", high_waiting.size());
   fprintf(fout_status, "%s %d\n\n", "LOW PRIORITY WAITING JOBS:", low_waiting.size());

   fprintf(fout_status, "%-11s %-10s %-6s %s %-9s %s %s %-29s %-35s %s\n", "JOB_ID", "USER", "USERID", 
   			"MODE", "SERVER", "STATUS", "PRIORITY", "DATE", "JOB", "LICENSE(S)");
   fprintf(fout_status, "-----------------------------------------------------------------------------------------------------------------------------------------\n");

   // print out information from the high-low_running, and intermediate queues
   for ( int i = 0; i < 3; i++ )
   {
	map<string, job_info>::iterator q_begin, q_end;
  	if ( i == 0 )
	{ q_begin = high_running.begin(); q_end = high_running.end(); }
	else if ( i == 1 )
	{ q_begin = low_running.begin(); q_end = low_running.end(); }
	else
	{ q_begin = intermediate.begin(); q_end = intermediate.end(); }

	while ( q_begin != q_end )
	{
	   hdisplay_status(q_begin->second);
	   ++q_begin;
	} 
   }

   // now print out information from the high-low waiting queues
   for ( int i = 0; i < 2; i++ )
   {
      list<job_info>::iterator q_begin, q_end;

      if ( i == 0 )
      { q_begin = high_waiting.begin(); q_end = high_waiting.end(); }
      else
      { q_begin = low_waiting.begin(); q_end = low_waiting.end(); }

      while ( q_begin != q_end )
      {
	 hdisplay_status(*q_begin);
	 ++ q_begin;
      }
   }

   fclose(fout_status);
   return 0;
}

// a helper display function to the display_status function
void hdisplay_status(job_info packet)
{
   fprintf(fout_status, "%-11s %-10s %-7d ", packet.job_id.c_str(), packet.packet.user,
   	packet.packet.user_id);

   // print out the mode
   if ( !strcasecmp(packet.packet.mode, "interactive") )
	fprintf(fout_status, "%-4s", "I ");
   else 
	fprintf(fout_status, "%-4s", "B ");

   // print out the server
   if ( packet.host_running == "" )
   	fprintf(fout_status, "%-11s", "N/A");
   else
   	fprintf(fout_status, "%-11s", packet.host_running.c_str());

   // print out the status
   if ( (packet.status == "running") || (packet.status == "suspend") )
	fprintf(fout_status, "%-7s ", "run");
   else
	fprintf(fout_status, "%-7s ", "wait");

   // print out the priority
   if ( !strcasecmp(packet.packet.priority, "high") )
	fprintf(fout_status, "%-7s", "high  ");
   else
	fprintf(fout_status, "%-7s", "low  ");
	  
   // print out the date
   fprintf(fout_status, "%-30s", packet.packet.datesubmit);

   // print out the job command 
   fprintf(fout_status, "%-35s  ", packet.packet.job);

   // print out the licenses 
   fprintf(fout_status, "%s", "[ ");
   for (int i = 0; i < packet.license_vector.size(); i++)
   	fprintf(fout_status, "%s ", packet.license_vector[i].c_str());
   fprintf(fout_status, "%s\n", "] ");
}

#ifdef DEBUG
int display_debug()
{
	fout_debug.open(QDEBUGFILE);
	if ( fout_debug.bad() )
	{
	   perror("Can't open debug file");
	   return -1;
	}

	// (1) Display valid_hosts, avail_hosts, avail_licenses first. 
 	list<string>::iterator list_begin, list_end;
	map<string, int>::iterator map_begin, map_end;

	fout_debug << "VALID_HOSTS" << endl;
	list_begin = valid_hosts.begin(); list_end = valid_hosts.end();
	while ( list_begin != list_end )
	{
 	   fout_debug << *list_begin << " ";	
	   ++list_begin;
	}
	fout_debug << endl;

	fout_debug << "AVAIL_HOSTS" << endl;
	list_begin = avail_hosts.begin(); list_end = avail_hosts.end();
	while ( list_begin != list_end )
	{
 	   fout_debug << *list_begin << " ";	
	   ++list_begin;
	}
	fout_debug << endl;

	fout_debug << "AVAIL_LICENSES" << endl;
	map_begin = avail_licenses.begin(); map_end = avail_licenses.end();
	while ( map_begin != map_end )
	{
		fout_debug << map_begin->first << " " << map_begin->second << " ";
		++map_begin;
	}
	fout_debug << endl;

	// Display the defective_hosts.
	fout_debug << "DEFECTIVE_HOSTS" << endl;
	list_begin = defective_hosts.begin(); list_end = defective_hosts.end();
	while ( list_begin != list_end )
	{
 	   fout_debug << *list_begin << " ";	
	   ++list_begin;
	}
	fout_debug << endl;

	// Display the license files.
	fout_debug << "LICENSE_FILES: " << endl;
	map<string, string>::iterator itl_begin = license_files.begin(), itl_end = license_files.end();
	while ( itl_begin != itl_end )
	{
	   fout_debug << itl_begin->first << " " << itl_begin->second << endl; 
	   ++itl_begin;
	}
	fout_debug << endl;

	// (2) Display all the queues.
	map<string, job_info>::iterator queue_begin, queue_end;
	list<job_info>::iterator qlist_begin, qlist_end;

	fout_debug << "HIGH_RUNNING QUEUE" << endl;
	queue_begin = high_running.begin(); queue_end = high_running.end();
	while ( queue_begin != queue_end )
	{
		hdisplay_debug(queue_begin->second);
		++ queue_begin;
	}

	fout_debug << "LOW_RUNNING QUEUE" << endl;
	queue_begin = low_running.begin(); queue_end = low_running.end();
	while ( queue_begin != queue_end )
	{
		hdisplay_debug(queue_begin->second);
		++ queue_begin;
	}
 		
	fout_debug << "INTERMEDIATE QUEUE" << endl;
	queue_begin = intermediate.begin(); queue_end = intermediate.end();
	while ( queue_begin != queue_end )
	{
		hdisplay_debug(queue_begin->second);
		++ queue_begin;
	}
 		
	fout_debug << "HIGH_WAITING QUEUE" << endl;
	qlist_begin = high_waiting.begin(); qlist_end = high_waiting.end();
	while ( qlist_begin != qlist_end )
	{
		hdisplay_debug(*qlist_begin);
		++ qlist_begin;
	}
 		
	fout_debug << "LOW_WAITING QUEUE" << endl;
	qlist_begin = low_waiting.begin(); qlist_end = low_waiting.end();
	while ( qlist_begin != qlist_end )
	{
		hdisplay_debug(*qlist_begin);
		++ qlist_begin;
	}

	// (3) Display the job_find map.
	fout_debug << "JOB_FIND MAP" << endl;
 	map<string, task_packet>::iterator itb_jobfind = job_find.begin(),
				ite_jobfind = job_find.end();
	while ( itb_jobfind != ite_jobfind )
	{
		fout_debug << itb_jobfind->first << " " << itb_jobfind->second.uid 
		     << " " << itb_jobfind->second.queue_type 
		     << endl; 
		++itb_jobfind;	   
	}
	fout_debug << endl;

	// (4) Display the job_messages. 
	fout_debug << "JOB_MESSAGES" << endl;
	list<sockfd4_packet>::iterator itb_jobmsg = job_messages.begin(), 
					ite_jobmsg = job_messages.end();
	while ( itb_jobmsg != ite_jobmsg )
	{
 	   fout_debug << itb_jobmsg->job_id << " " << itb_jobmsg->message << endl;	
	   ++itb_jobmsg;
	}

	fout_debug.close();
}
#endif

#ifdef DEBUG
void hdisplay_debug(job_info packet)
{
   fout_debug << "Job Element: " << endl;

   // display the structure first
   fout_debug << "user: " << packet.packet.user << ", ";
   fout_debug << "user_id: " << packet.packet.user_id << ", ";
   fout_debug << "group_id: " << packet.packet.gid << ", ";
   fout_debug << "date submitted: " << packet.packet.datesubmit << ", "; 
   fout_debug << "job: " << packet.packet.job << ", ";
   fout_debug << "mode: " << packet.packet.mode << ", ";
   fout_debug << "confiscated: " << packet.confiscated << ", ";
   fout_debug << "priority: " << packet.packet.priority << ", ";
   if (!strcasecmp(packet.packet.mode, "batch"))
	fout_debug << "logfile: " << packet.packet.logfile << ", ";
   fout_debug << "prefhost: " << packet.packet.prefhost << ", ";
   fout_debug << "onlyhost: " << packet.packet.onlyhost << ", ";
   fout_debug << "status: " << packet.status << ", ";
   if ( !strcasecmp(packet.status.c_str(), "running"))
	fout_debug << "job_pid: " << packet.job_pid << ", ";

   // print out the job_id
   fout_debug << "JOB_ID: " << packet.job_id << ", "; 

   // now display the vector buffer
   fout_debug << endl << "Licenses: ";
   for (int i = 0; i < packet.license_vector.size(); i++)
	fout_debug << packet.license_vector[i] << " "; 

   fout_debug << endl << endl;
}
#endif

// jumps to this function when host and license(s) are available, interactive job
int hlavail_i(job_info temp_job, int new_sockfd, list<string>::iterator it_find )
{
   // Just in case the interactive user decides to quit the queue client program.
   signal(SIGPIPE, sigpipe_handler); 

   // send the server name to the queue client
   if (send(new_sockfd, assigned_host, sizeof(assigned_host), 0) < 0)
   {
	   perror("Error on sending assigned host");
	   return -1;
   }

   // send the job id to the queue client
   char buffer[MAXHOSTNAMELEN];
   strcpy(buffer, temp_job.job_id.c_str());
   if (send(new_sockfd, buffer, sizeof(buffer), 0) < 0)
   {
	   perror("Error on sending job_id");
	   return -1;
   }

   // close off the socket connection
   close(new_sockfd);

   // delete this host name off of avail_hosts
   if ( it_find != avail_hosts.end() )
	avail_hosts.erase(it_find);

   // update available licenses list 			
   map<string, int>::iterator license_iter;
   for (int i = 0; i < temp_job.license_vector.size(); i++)
   {
  	license_iter = avail_licenses.find(temp_job.license_vector[i]);
	license_iter->second = license_iter->second - 1; 
   }

   // save other information about the job in temp_job
   temp_job.host_running = assigned_host;
   temp_job.status = "waiting";

   // put job in intermediate queue
   intermediate.insert( map<string, job_info>::value_type(assigned_host, temp_job) );

   return 0;
}

// jumps to this function when host and license(s) are available, batch job
int hlavail_b(job_info temp_job, int waitq, list<string>::iterator it_find)
{
   // set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
   signal(SIGPIPE, sigpipe_handler); 

   // (1) piece together the command line argument

   // command to "cd" to the user's directory where "queue" was submitted
   char cd_cmd[MAXQUEUECOMMAND];
   strcpy(cd_cmd, "cd");
   sprintf(cd_cmd, "%s %s%s", cd_cmd, temp_job.packet.cur_dir, "; pwd;");

   // the queue command
   char q_command[MAXQUEUECOMMAND];
   strcpy(q_command, QDIR);
   sprintf(q_command, "%s %s %s", q_command, "-h", assigned_host);

   // a dummy license so that queue.c won't complain that we didn't
   // specify any licenses
   sprintf(q_command, "%s %s %s", q_command, "-a", "license");

   // a hack so that queue.c will not make a connection back to us 
   sprintf(q_command, "%s %s", q_command, "-1");

   sprintf(q_command, "%s %s %s", q_command, "--", temp_job.packet.job); 
   
   // redirect the stdout of the batch job to the log file
   sprintf(q_command, "%s %s %s", q_command, ">", 
					temp_job.packet.logfile);

   // the whole command
   char cmd[MAXQUEUECOMMAND];
   strcpy(cmd, cd_cmd);
   sprintf(cmd, "%s %s", cmd, q_command); 

   #ifdef DEBUG
   cout << "cmd: " << cmd << endl;
   #endif

   // (2) now fork off a child process	
   int pid;
   if ( (pid = fork()) == 0 )
   {
	/* child process */

	// set the current environment to the user's environment
	extern char ** environ;
	for ( int i=0; i < temp_job.vec_arge.size(); i++ )
	   putenv(temp_job.vec_arge[i].c_str());

	// set the uid and gid to the user of the current job
	if ( setgid(temp_job.packet.gid) < 0 )
	{
	   perror("Error: on setting gid");
	   exit(-1);
	}  
	
	// initialize the group access list
	if ( initgroups(temp_job.packet.user, temp_job.packet.gid) < 0 )
	{
	   perror("Error: on setting initgroups");
	   exit(-1);
	}

	if ( setuid(temp_job.packet.user_id) < 0 )
	{
	   perror("Error: on setting user id");
	   exit(-1);
	}
    
	// create the user's logfile 
	ofstream fout(temp_job.packet.logfile);
	if ( fout.bad() )
	{
	   perror("Can't create user's logfile"); 
	   exit(-1);
	}
	fout.close();

	// now execute the job
	if ( system(cmd) < 0 )
	{
	   perror("Error: in system()");
	   exit(-1);
	}

	#ifdef DEBUG
	cout << "Child process finishes..." << endl;
	#endif

	// exit when done
	exit(1);
   }
   else if ( pid == -1 )
   {
	// fork error: too many processes right now, so just try forking this
	// job again later; 
	return 1;
   }
   else
   {
	/* parent process */

	// store the child's pid in temp_job
	temp_job.batch_pid = pid;

	#ifdef DEBUG
	cout << "batch_pid: " << temp_job.batch_pid << endl;
	#endif

	// delete this host name off of avail_hosts
	if ( it_find != avail_hosts.end() )
	   avail_hosts.erase(it_find);

	// update available licenses list 			
 	map<string, int>::iterator license_iter;
	for (int i = 0; i < temp_job.license_vector.size(); i++)
	{
	   license_iter = avail_licenses.find(temp_job.license_vector[i]);
	   license_iter->second = license_iter->second - 1; 
	}

	// save other information about the job in temp_job
	temp_job.host_running = assigned_host;
	temp_job.status = "waiting";

	// put job in intermediate queue
	intermediate.insert( map<string, job_info>::value_type(assigned_host, temp_job) );

	return 0;
   }
}

// jumps to this function when job is high priority and host is available but license(s) are 
// not available (for both interactive and batch jobs)
int hl_unavail(job_info temp_job, int new_sockfd, int waitq, list<string>::iterator it_find)
{
   // set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
   signal(SIGPIPE, sigpipe_handler); 

   map<string, conflic_packet> temp_map;
   list<string> avail_erase;  // licenses to erase from avail_licenses
   map<string, int> unsuspend_servers;  // list of jobs to unsuspend 

   // insert all the licenses that we need into a list
   list<string> license_vector;
   for ( int i = 0; i < temp_job.license_vector.size(); i++ )
	license_vector.push_back(temp_job.license_vector[i]);

   // 1st, iterate through the avail_licenses list to check if
   // there are any licenses available before going to the queue
   list<string>::iterator itb_need = license_vector.begin(),	
				itb_end = license_vector.end();

   while ( itb_need != itb_end )
   {
      map<string, int>::iterator it_find = avail_licenses.find(*itb_need);

      if ( it_find->second > 0 )
      {
	 /* this is an error -- can't update yet!
	 // update available license list
	 it_find->second = it_find->second - 1; */

	 avail_erase.push_back(*itb_need);

	 // save the state of the current iterators 
	 list<string>::iterator temp1_iter = itb_need;
	 list<string>::iterator temp2_iter = ++temp1_iter;

	 // delete the license off of license_vector
	 license_vector.erase(itb_need);
	
	 // update the iterators
	 itb_need = temp2_iter;
	 itb_end = license_vector.end();

	 continue;  // go back to the top of the loop
     }

     ++itb_need;
   }
		 
   // 2nd, iterate through each job in the low_running queue
   // (forget about the intermediate queue -- too complicated)
   map<string, job_info>::iterator it_begin = low_running.begin(),
			   	   it_end = low_running.end();
   while ( it_begin != it_end )
   {
      // if the job has any licenses already confiscated by
      // another job, skip this job (makes life simpler)
      if ( it_begin->second.confiscated == 1 )
      {
	 ++it_begin;
	 continue;
      }

      // packet that holds the confiscated license(s) 
      // that we've gotten from this job
      struct conflic_packet tempconf_packet;

      // set iterators for license_vector list (license(s) needed)
      list<string>::iterator itb_need = license_vector.begin(),	
					itb_end = license_vector.end();

      // for each license in the license_vector list
      while ( itb_need != itb_end )
      {
	   // Note: this will not work if we need multiple license
	   // of the same type (like, we need two "apple" licenses).
	   // This is assuming that we need only one license of each
	   // type.  Can fix later if need to.

	   // for each license in the low_running job's license_vector
	   for (int j = 0; j < it_begin->second.license_vector.size(); j++)
	   {
	      // if the licenses match, then insert into tempconf_packet 
	      if ( !strcasecmp((*itb_need).c_str(),
			(it_begin->second.license_vector[j]).c_str()) )
		tempconf_packet.license_vector.push_back(*itb_need);	
	   }
	   ++itb_need;	
      }

      if ( !tempconf_packet.license_vector.empty() )
      {
	 // insert the forked off queued pid into the temp_map
	 tempconf_packet.job_pid = it_begin->second.job_pid;
 
	 // insert hostname and tempconf_packet into the temp_map 
	 temp_map.insert( map<string, conflic_packet>::
				value_type(it_begin->first, tempconf_packet) );

	 // now delete licenses off of the license_vector list
	 for ( int k = 0; k < tempconf_packet.license_vector.size(); k++ )
	 {
	       list<string>::iterator itlist_find = 
		find(license_vector.begin(), license_vector.end(), 		
	       tempconf_packet.license_vector[k]);

	       license_vector.erase(itlist_find);
	 } 
       }

       ++it_begin;
   }	

   // if license_vector is empty, means that we have all the
   // confiscated licenses available 
   if ( license_vector.empty() )
   {
	#ifdef DEBUG
	cout << "licenses available!" << endl;
	#endif

	// (1) open up a connection to the "task_manager" of every host in 
	// temp_map and send the job a "suspend" signal
        map<string, conflic_packet>::iterator itb = temp_map.begin(),
					ite = temp_map.end();

	while ( itb != ite )
	{
	   // send the packet over to the task_manager
	   struct tm_packet tmpacket;  // packet to send to task_manager
	   tmpacket.job_pid = itb->second.job_pid; 
	   strcpy(tmpacket.message, "suspend");

	   // call tm_connect function
	   if ( tm_connect(itb->first, tmpacket) == 0 )
	   {
	      // put server name in temporary list "unsuspend_servers"
	      unsuspend_servers.insert( map<string, int>::value_type(itb->first, 
	      						itb->second.job_pid) );

	      // update the status of the confiscated license job
	      map<string, job_info>::iterator it_jfind =
					low_running.find(itb->first);
	      it_jfind->second.status = "suspend";
	      it_jfind->second.confiscated = 1;  // set confiscated bit
	   }
	   else
	   {
	      // got an error, so we have to unsuspend all the jobs that we've suspended;
	      // call "unsuspend_jobs" function to handle this error condition
	      unsuspend_jobs(unsuspend_servers);
	      return -1;
	   }

	   ++itb;
	} 

	// (2) assign temp_job's conf_license to temp_map
	// (note: we are assuming that all the connections are 
	// successful -- have to handle error cases later);
	// also assign temp_job's avail_license_erase to avail_erase
	temp_job.conf_license = temp_map;  
	temp_job.avail_license_erase = avail_erase;

	// (3) remove the licenses manually

	// (4) now follow the same exact format as above where hosts
	// and licenses are available 

	// start a new block
	{  
	   // interactive mode: send hostname back to queue client
	   if ( !strcasecmp(temp_job.packet.mode, "interactive") )
	   {
		if (send(new_sockfd, assigned_host, sizeof(assigned_host), 0) < 0)
		{
		   perror("Error on sending assigned host to queue client");
		   unsuspend_jobs(unsuspend_servers);
		   return -1;
		}

		// close off the socket connection
		close(new_sockfd);

		// delete this host name off of avail_hosts
		if ( it_find != avail_hosts.end() )
		   avail_hosts.erase(it_find);

		// save other information about the job in temp_job
		temp_job.host_running = assigned_host;
		temp_job.status = "waiting";

		// need to update avail_licenses list (if we took any licenses
		// from this list)
		list<string>::iterator it_begin = avail_erase.begin(),
					it_end = avail_erase.end();

		while ( it_begin != it_end )
		{
           	   map<string, int>::iterator it_find = 
			avail_licenses.find(*it_begin);
	   	   it_find->second = it_find->second - 1; 
	   	   ++it_begin;
		}

		// put job in intermediate queue
	 	intermediate.insert( map<string, job_info>::value_type(assigned_host,
									temp_job) ); 

		return 0;
	   }
	   // else, in batch mode
	   else
	   {
   		// (1) piece together the command line argument

   		// command to "cd" to the user's directory where "queue" was submitted
   		char cd_cmd[MAXQUEUECOMMAND];
   		strcpy(cd_cmd, "cd");
   		sprintf(cd_cmd, "%s %s%s", cd_cmd, temp_job.packet.cur_dir, "; pwd;");

   		// the queue command
   		char q_command[MAXQUEUECOMMAND];
   		strcpy(q_command, QDIR);
   		sprintf(q_command, "%s %s %s", q_command, "-h", assigned_host);

   		// a dummy license so that queue.c won't complain that we didn't
   		// specify any licenses
   		sprintf(q_command, "%s %s %s", q_command, "-a", "license");

   		// a hack so that queue.c will not make a connection back to us 
   		sprintf(q_command, "%s %s", q_command, "-1");

   		sprintf(q_command, "%s %s %s", q_command, "--", temp_job.packet.job); 
   
   		// redirect the stdout of the batch job to the log file
   		sprintf(q_command, "%s %s %s", q_command, ">", 
					temp_job.packet.logfile);

   		// the whole command
   		char cmd[MAXQUEUECOMMAND];
   		strcpy(cmd, cd_cmd);
   		sprintf(cmd, "%s %s", cmd, q_command); 

   		#ifdef DEBUG
   		cout << "cmd: " << cmd << endl;
   		#endif

		// (2) now fork off a child process	
		int pid;
		if ( (pid = fork()) == 0 )
		{
		   /* child process */

		   // set the current environment to the user's environment
		   extern char ** environ;
		   for ( int i=0; i < temp_job.vec_arge.size(); i++ )
	   		putenv(temp_job.vec_arge[i].c_str());

		   // set the uid and gid to the user of the current job
		   if ( setgid(temp_job.packet.gid) < 0 )
		   {
			perror("Error: on setting gid");
		  	exit(-1);
		   }  

		   // initialize the group access list
	  	   if ( initgroups(temp_job.packet.user, temp_job.packet.gid) < 0 )
		   {
	   		perror("Error: on setting initgroups");
	   		exit(-1);
		   }
			
		   if ( setuid(temp_job.packet.user_id) < 0 )
		   {
			perror("Error: on setting user id");
			exit(-1);
		   }

		   // create the user's logfile 
		   ofstream fout(temp_job.packet.logfile);
		   if ( fout.bad() )
		   {
	   		perror("Can't create user's logfile"); 
	   		exit(-1);
		   }
		   fout.close();

		   // now execute the job
		   if ( system(cmd) < 0 )
		   {
	   		perror("Error: in system()");
	   		exit(-1);
		   }

		   #ifdef DEBUG
		   cout << "Child process finishes..." << endl;
		   #endif

		   // exit when done
		   exit(1);
		}
		else if ( pid == -1 )
		{
		   // fork error: too many processes right now, so just try forking this
		   // job again later; job will be put in the waiting queue 

		   // call the error handling routine to unsuspend jobs
		   unsuspend_jobs(unsuspend_servers);

		   return -1;
		}	
		else
		{
		   /* parent process */

		   // store the child's pid in temp_job
		   temp_job.batch_pid = pid;

		  #ifdef DEBUG
		  cout << "batch_pid: " << temp_job.batch_pid << endl;
		  #endif

		   // delete this host name off of avail_hosts
		   if ( it_find != avail_hosts.end() )
			avail_hosts.erase(it_find);

		   // save other information about the job in temp_job
		   temp_job.host_running = assigned_host;
		   temp_job.status = "waiting";

		   // need to update avail_licenses list (if we took any licenses
		   // from this list)
		   list<string>::iterator it_begin = avail_erase.begin(),
					it_end = avail_erase.end();

		   while ( it_begin != it_end )
		   {
           	      map<string, int>::iterator it_find = 
			avail_licenses.find(*it_begin);
	   	      it_find->second = it_find->second - 1; 
	   	      ++it_begin;
		   }

		   // put job in intermediate queue
		   intermediate.insert( map<string, job_info>::value_type(assigned_host,
									temp_job) ); 

		   return 0;
		}

	   }  // closes off batch mode else 

	}  // closes off block
   }  // closes "if" license_vector is empty
   else
   {
	#ifdef DEBUG
	cout << "licenses not available" << endl;
	#endif

	return 1;
   } 

   #ifdef DEBUG
   list<string>::iterator itb = license_vector.begin(),
			ite = license_vector.end();
   cout << "License Vector: " << endl;
   while ( itb != ite )
   {
	cout << *itb << endl;
	++itb;
   }

   map<string, conflic_packet>::iterator itbegin =
	temp_job.conf_license.begin(), itend = temp_job.conf_license.end();
   while ( itbegin != itend )
   {
	cout << itbegin->first << ": " << itbegin->second.job_pid << ": ";
	for ( int g = 0; g < itbegin->second.license_vector.size(); g++ )
	   cout << itbegin->second.license_vector[g] << ", ";
	cout << endl; 
	++itbegin;
   }
   #endif

}

// jumps to this function whenever we need to make a connection to the task manager
int tm_connect(string hostname, struct tm_packet tmpacket)
{
   // set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
   signal(SIGPIPE, sigpipe_handler); 

   int sockfd;
   int counter = 1;
   struct sockaddr_in serv_addr;
   struct hostent *host;

   // get the IP address of the host to connect to
   if ( (host = gethostbyname(hostname.c_str())) == NULL )
   {
	perror("Invalid host name!");
	return 1;
   }

   // fill in the server information so that we can connect to it
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_port = htons(C_PORTNUM);
   serv_addr.sin_addr = *((struct in_addr *)host->h_addr);

   // We try to connect ten times to prevent an overloaded 
   // system from keeping us down. 
   while ( counter <= 10 )
   {
      // open up a TCP socket
      if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
      {
	 perror("Error on socket()");
	 return 1;
      }
		
      // set socket options for TCP socket
      int sendbuff = 16384;
      if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
		(char *) &sendbuff, sizeof(sendbuff)) < 0 )
      {
 	 perror("Error on TCP setsock option");
	 return 1;
      }
		
      if ( connect(sockfd, (struct sockaddr *) &serv_addr,
		sizeof(serv_addr)) < 0 )
      {
	 counter++;
	 cout << "Connecting to task_manager..." << endl;
	 sleep(3);  // sleep a few seconds before trying again	
      }
      else
	 break;
   }

   // if can't connect to task_manager, then assume that the server is down; put the server in 
   // the defective server list and call "defective_server"
   if ( counter == 11 )
   {
      defective_hosts.push_back(hostname);
      defective_server(hostname);
      return -1;
   }
   else
   {
      if ( send(sockfd, &tmpacket, sizeof(tmpacket), 0) < 0 )
      {
         defective_hosts.push_back(hostname);
         defective_server(hostname);
         perror("Error on sending packet");
	 return -1;
      }
   }

   // close off the socket connection
   close(sockfd);			   
 
   return 0;
}

// this function is called when we have to process user's message to kill, suspend, ... a job
int task_control(struct sockfd4_packet packet, map<string, task_packet>::iterator it_jobfind)
{
   	   // set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
  	   signal(SIGPIPE, sigpipe_handler); 

	   // (a) process the message 
	   int connect_bit = 0;  // need to connect to task_manager?
	   tm_packet tmpacket;  // packet to send to task_manager 
	   
	   // (a1) if the message is "kill" job
	   if ( !strcasecmp(packet.message, "kill") )
	   {
		// if the job is in the waiting queue
		if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_waiting") ||
		     !strcasecmp(it_jobfind->second.queue_type.c_str(), "low_waiting") )
		{
		   // just delete the job off the queue 

		   int bit = 0;  // tells us which queue we're in
		   list<job_info>::iterator it_begin, it_end;
		   if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_waiting") )
		   { bit = 1; it_begin = high_waiting.begin(); 
			it_end = high_waiting.end(); }
		   else
		   { it_begin = low_waiting.begin(); it_end = low_waiting.end(); }

		   while ( it_begin != it_end )
		   {
		      // found the job element
		      if ( it_begin->job_id == it_jobfind->first )
		      {
		 	 if ( bit )
			 {
		   	    // if job is interactive, need to close off the socket
			    // connection first to free up the socket descriptor
			    if ( !strcasecmp(it_begin->packet.mode, "interactive") )
				close(it_begin->sockfd);
			    high_waiting.erase(it_begin);
			 }
			 else
			 {
		   	    // if job is interactive, need to close off the socket
			    // connection first to free up the socket descriptor
			    if ( !strcasecmp(it_begin->packet.mode, "interactive") ) 
				close(it_begin->sockfd);
			    low_waiting.erase(it_begin);	 
			 }
		         break;
		      }
		      ++it_begin;
		   }

		   // update the job_find data structure
		   job_find.erase(it_jobfind);
		}
		// else, job is in running queue
		else
		{
		   // get all the information to send to the task_manager 
		   map<string, job_info>::iterator it_find;
		   
		   if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_running") )
			it_find = high_running.find(it_jobfind->second.hostname);
		   else if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "low_running") )
			it_find = low_running.find(it_jobfind->second.hostname);
		 
	 	   tmpacket.job_pid = it_find->second.job_pid;
		   strcpy(tmpacket.message, "kill");

		   #ifdef DEBUG
		   cout << "tmpacket.job_pid: " << tmpacket.job_pid << " " 
			<< "tmpacket.message: " << tmpacket.message << endl; 
		   #endif

		   // need to connect to task_manager
		   connect_bit = 1;
		}
	   }  // closes off (a1)

	   // (a2) else if the message is "suspend" job
	   else if ( !strcasecmp(packet.message, "suspend") )
	   {
		// if the job is in the waiting queue
		if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_waiting") ||
		     !strcasecmp(it_jobfind->second.queue_type.c_str(), "low_waiting") )
		{
		   list<job_info>::iterator it_begin, it_end;
		   if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_waiting") )
		   { it_begin = high_waiting.begin(); it_end = high_waiting.end(); }
		   else
		   { it_begin = low_waiting.begin(); it_end = low_waiting.end(); }

		   while ( it_begin != it_end )
		   {
		      // found the job element
		      if ( it_begin->job_id == it_jobfind->first )
		      {
		   	 // change the status of the job to "suspend" 
			 it_begin->status = "suspend"; 
			 break;
		      }
		      ++it_begin;
		   }
		}
		// else, job is in running queue
		else
		{
		   // get all the information to send to the task_manager 
		   map<string, job_info>::iterator it_find;
		   
		   if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_running") )
			it_find = high_running.find(it_jobfind->second.hostname);
		   else if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "low_running") )
			it_find = low_running.find(it_jobfind->second.hostname);
		 
	 	   tmpacket.job_pid = it_find->second.job_pid;
		   strcpy(tmpacket.message, "suspend");

		   // for now, just assume that job is suspended correctly 
		   // (change status of job to "suspend")
		   it_find->second.status = "suspend"; 

		   #ifdef DEBUG
		   cout << "tmpacket.job_pid: " << tmpacket.job_pid << " " 
			<< "tmpacket.message: " << tmpacket.message << endl; 
		   #endif

		   // need to connect to task_manager
		   connect_bit = 1;
		}
	   }

	   // (a3) else if the message is "unsuspend" job
	   else if ( !strcasecmp(packet.message, "unsuspend") )
	   {
		// if the job is in the waiting queue
		if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_waiting") ||
		     !strcasecmp(it_jobfind->second.queue_type.c_str(), "low_waiting") )
		{
		   list<job_info>::iterator it_begin, it_end;
		   if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_waiting") )
		   { it_begin = high_waiting.begin(); it_end = high_waiting.end(); }
		   else
		   { it_begin = low_waiting.begin(); it_end = low_waiting.end(); }

		   while ( it_begin != it_end )
		   {
		      // found the job element
		      if ( it_begin->job_id == it_jobfind->first )
		      {
		   	 // change the status of the job back to "waiting" 
			 it_begin->status = "waiting"; 
			 break;
		      }
		      ++it_begin;
		   }
		}
		// else, job is in running queue
		else
		{
		   // get all the information to send to the task_manager 
		   map<string, job_info>::iterator it_find;
		   
		   if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_running") )
			it_find = high_running.find(it_jobfind->second.hostname);
		   else if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "low_running") )
			it_find = low_running.find(it_jobfind->second.hostname);
		 
	 	   tmpacket.job_pid = it_find->second.job_pid;
		   strcpy(tmpacket.message, "unsuspend");

		   // for now, just assume that job is unsuspended correctly 
		   it_find->second.status = "running";

		   #ifdef DEBUG
		   cout << "tmpacket.job_pid: " << tmpacket.job_pid << " " 
			<< "tmpacket.message: " << tmpacket.message << endl; 
		   #endif

		   // need to connect to task_manager
		   connect_bit = 1;
		}
	   }

	   // (a4) else if the message is to change the job to high_priority
	   else if ( !strcasecmp(packet.message, "high_priority") )
	   {
		// if job is in low_waiting queue
		if ( !strcasecmp(it_jobfind->second.queue_type.c_str(),"low_waiting") ) 
		{
		   // move the job to high_waiting queue
		   list<job_info>::iterator it_begin = low_waiting.begin(),
						it_end = low_waiting.end();

		   while ( it_begin != it_end )
		   {
		      // found the job element
		      if ( it_begin->job_id == it_jobfind->first )
		      {
			 strcpy(it_begin->packet.priority, "high");
			 high_waiting.push_back(*it_begin);
			 low_waiting.erase(it_begin);
		         break;
		      }
		      ++it_begin;
		   }

		   // update the job_find data structure
		   it_jobfind->second.queue_type = "high_waiting"; 
		}
		// else if job is in low_running queue
		else if (!strcasecmp(it_jobfind->second.queue_type.c_str(), "low_running"))
		{
		   // move the job to high_running queue
		   map<string, job_info>::iterator it_find = 
			low_running.find(it_jobfind->second.hostname);

		   strcpy(it_find->second.packet.priority, "high");
		   high_running.insert( map<string, job_info>::value_type(
			it_jobfind->second.hostname, it_find->second) );

		   low_running.erase(it_find);

		   // update the job_find data structure
		   it_jobfind->second.queue_type = "high_running"; 
		} 
		// else if job is in intermediate queue
		else if (!strcasecmp(it_jobfind->second.queue_type.c_str(),"intermediate"))
		{
		   // change its priority to "high" 
		   map<string, job_info>::iterator it_find = 
			intermediate.find(it_jobfind->second.hostname);
		   strcpy(it_find->second.packet.priority, "high"); 
		}
	   }

	   // (a5) else if the message is to change the job to low_priority
	   else if ( !strcasecmp(packet.message, "low_priority") )
	   {
		// if job is in high_waiting queue
		if ( !strcasecmp(it_jobfind->second.queue_type.c_str(), "high_waiting") ) 
		{
		   // move the job to low_waiting queue
		   list<job_info>::iterator it_begin = high_waiting.begin(),
						it_end = high_waiting.end();

		   while ( it_begin != it_end )
		   {
		      // found the job element
		      if ( it_begin->job_id == it_jobfind->first )
		      {
			 // if the job is interactive, then just return (can't 
			 // change interactive jobs to low priority)
			 if ( !strcmp(it_begin->packet.mode, "interactive") )
			    return 0;
		       
			 strcpy(it_begin->packet.priority, "low");
			 low_waiting.push_back(*it_begin);
			 high_waiting.erase(it_begin);
		         break;
		      }
		      ++it_begin;
		   }

		   // update the job_find data structure
		   it_jobfind->second.queue_type = "low_waiting"; 
		}
		// else if job is in high_running queue
		else if (!strcasecmp(it_jobfind->second.queue_type.c_str(), "high_running"))
		{
		   // move the job to low_running queue
		   map<string, job_info>::iterator it_find = 
			high_running.find(it_jobfind->second.hostname);

		   // if the job is interactive, then just return (can't 
		   // change interactive jobs to low priority)
		   if ( !strcmp(it_find->second.packet.mode, "interactive") )
		      return 0;

		   strcpy(it_find->second.packet.priority, "low");
		   low_running.insert( map<string, job_info>::value_type(
			it_jobfind->second.hostname, it_find->second) );

		   high_running.erase(it_find);

		   // update the job_find data structure
		   it_jobfind->second.queue_type = "low_running"; 
		} 
		// else if job is in intermediate queue
		else if (!strcasecmp(it_jobfind->second.queue_type.c_str(), "intermediate"))
		{
		   // change its priority to "low" 
		   map<string, job_info>::iterator it_find = 
			intermediate.find(it_jobfind->second.hostname);

		   // if the job is interactive, then just return (can't 
		   // change interactive jobs to low priority)
		   if ( !strcmp(it_find->second.packet.mode, "interactive") )
		      return 0;

		   strcpy(it_find->second.packet.priority, "low"); 
		}
	   }

	   // (b) open up a connection to the task_manager and send it 
	   // the relevant information to do something to the job
	   if ( connect_bit )
	   {
	      // call tm_connect function
	      if ( tm_connect(it_jobfind->second.hostname, tmpacket) != 0 )
	      {
		 // something went wrong
		 return -1;
	      }
	   }  // closes off connection block

	   return 0;
}

int defective_server(string server)
{
   // set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
   signal(SIGPIPE, sigpipe_handler); 

   list<string>::iterator it_dhost = find(valid_hosts.begin(), 
 				valid_hosts.end(), server);

   // if host does not exist, this is an error 
   if (it_dhost == valid_hosts.end())
	return -1;

   // delete this server from valid_hosts
   valid_hosts.erase(it_dhost);

   // delete this server from avail_hosts (if it's in there currently)
   it_dhost = find(avail_hosts.begin(), avail_hosts.end(), server);
   if (it_dhost != avail_hosts.end())
	avail_hosts.erase(it_dhost);

   // delete this server from the communicate data structure
   map<string, sockfd5_commun>::iterator itc_find = communicate.find(server);
   if (itc_find != communicate.end())
   	communicate.erase(itc_find);

   // check the running queues and the intermediate queue to see if there is 
   // a job running on this server; if there is, delete this job from the queue
   map<string, job_info>::iterator it_jobfind;

   // Note: If batch job, need to wait on child process.  If this job
   // has confiscated any licenses, needs to unsuspend all the suspended processes.
   int batchpid = -1;

   if ((it_jobfind = high_running.find(server)) != high_running.end())
   {
	// delete the element off of job_find as well
	map<string, task_packet>::iterator it_find;
	it_find = job_find.find(it_jobfind->second.job_id);
	job_find.erase(it_find);

	// need to bury zombie process?
	if ( !strcasecmp(it_jobfind->second.packet.mode, "batch") )
	   batchpid = it_jobfind->second.batch_pid;

	// check if we have confiscated any licenses
	if ( !it_jobfind->second.conf_license.empty() )
	{
	   // open up a connection to all the hosts in 
	   // conf_license to "unsuspend" the suspended jobs
	   map<string, conflic_packet>::iterator 
			itb = it_jobfind->second.conf_license.begin(),
			ite = it_jobfind->second.conf_license.end();

	   while ( itb != ite )
	   {
		struct tm_packet tmpacket;  // packet to send to task_manager
		tmpacket.job_pid = itb->second.job_pid; 
		strcpy(tmpacket.message, "unsuspend");

		// call tm_connect function
		if ( tm_connect(itb->first, tmpacket) == 0 )
		{
		      // update the status of the confiscated license job
		      map<string, job_info>::iterator it_jfind =
							low_running.find(itb->first);
		      it_jfind->second.status = "running";
		      it_jfind->second.confiscated = 0;  // set bit back to 0
		}

		++itb;
 	   } 
	} 
	else
	{
	   #ifdef DEBUG
	   cout << "No confiscated licenses" << endl;
	   #endif
	}

	high_running.erase(it_jobfind);
   }
   else if ((it_jobfind = low_running.find(server)) != low_running.end())
   {
  	// delete the element off of job_find as well
	map<string, task_packet>::iterator it_find;
	it_find = job_find.find(it_jobfind->second.job_id);
	job_find.erase(it_find);

	// need to bury zombie process?
	if ( !strcasecmp(it_jobfind->second.packet.mode, "batch") )
	   batchpid = it_jobfind->second.batch_pid;

	low_running.erase(it_jobfind);
   }
   else if ((it_jobfind = intermediate.find(server)) != intermediate.end())
   {
  	// delete the element off of job_find as well
	map<string, task_packet>::iterator it_find;
	it_find = job_find.find(it_jobfind->second.job_id);
	job_find.erase(it_find);

	// need to bury zombie process?
	if ( !strcasecmp(it_jobfind->second.packet.mode, "batch") )
	   batchpid = it_jobfind->second.batch_pid;

	intermediate.erase(it_jobfind);
   }

   // need to bury zombie process?
   if ( batchpid != -1 )
   {
	// sometimes we have to manually kill off the forked off
	// queue_manager (better to do this way; otherwise,
	// waitpid will block forever waiting for the child to die)

 	// the command to get the "sh..." pid first
	char pidcmd[100];
	strcpy(pidcmd, "ps alx | grep -v 'ps alx' | grep que | grep");
	sprintf(pidcmd, "%s %d %s", pidcmd, batchpid,
					"| awk '{print $3}' | grep -v");
	sprintf(pidcmd, "%s %d %s %s", pidcmd, batchpid, ">", TEMPFILE);

	#ifdef DEBUG
	cout << pidcmd << endl;
	#endif

	if ( system(pidcmd) < 0 )
	   perror("Error on pidcmd system()");
	else
	{
   	   // now read in the value of the "sh" pid
   	   int sh_pid = -1;
   	   fin.open(TEMPFILE);

	   if ( !fin.bad() )
	   {
   	      fin >> sh_pid;
   	      fin.close();

   	      if ( sh_pid != -1 )
   	      {
	         char pidcmd[100];
	         strcpy(pidcmd, "ps alx | grep -v 'ps alx' | grep que | grep");
	         sprintf(pidcmd, "%s %d %s", pidcmd, sh_pid,
			"| awk '{print $3}' | grep -v");
	         sprintf(pidcmd, "%s %d %s %s", pidcmd, sh_pid, ">", TEMPFILE);

  	         if ( system(pidcmd) < 0 )
  		   perror("Error on pidcmd system()");
	         else
	         {
	            // now get the actual pid that we need to kill the job
	            int kill_pid = -1;
	            fin.open(TEMPFILE);

		    if ( !fin.bad() )
		    {
	               fin >> kill_pid;
	               fin.close();

		       #ifdef DEBUG
		       cout << "kill_pid: " << kill_pid << endl;
		       #endif
	
		       // send the process the kill signal
		       if (kill_pid > 0)
		       { 
		          kill( kill_pid, SIGTERM );  // try SIGTERM first
	 	          kill( kill_pid, SIGKILL );
		       }
		    }
	         }
   	      } // closes off "if (sh_pid..."
	   }
	}

	// now bury this child	
	waitpid(batchpid, NULL, WNOHANG);
	
	#ifdef DEBUG
	cout << "after waiting on child process" << endl;
	#endif
   }

   return 0;

}

int unsuspend_jobs(map<string, int> unsuspend_servers)
{
   // set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
   signal(SIGPIPE, sigpipe_handler); 

   // open up a connection to the "task_manager" of every host in 
   // unsuspend_servers and send the job an "unsuspend" signal
   map<string, int>::iterator itb = unsuspend_servers.begin(),
					ite = unsuspend_servers.end();

   while ( itb != ite )
   {
	// send the packet over to the task_manager
	struct tm_packet tmpacket;  // packet to send to task_manager
	tmpacket.job_pid = itb->second; 
	strcpy(tmpacket.message, "unsuspend");

	// call tm_connect function
	if ( tm_connect(itb->first, tmpacket) == 0 )
	{
	   // update the status of the confiscated license job
	   map<string, job_info>::iterator it_jfind =
				low_running.find(itb->first);
	   it_jfind->second.status = "running";
	   it_jfind->second.confiscated = 0;  // unset confiscated bit
	}

	++itb;
   }

   return 0;
}

// checks if we can run any jobs in the waiting queues
int check_wait()
{
   	// set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
   	signal(SIGPIPE, sigpipe_handler); 

	list<job_info>::iterator iter_begin, iter_end;

	for ( int i = 0; i < 2; i++ )
	{
	   int high_queue;  // denotes that we are working on the high priority queue 
 
	   // check high priority queue first
	   if ( i == 0 )
	   { iter_begin = high_waiting.begin(); iter_end = high_waiting.end(); 
	     high_queue = 1; }
	   else
	   { iter_begin = low_waiting.begin(); iter_end = low_waiting.end(); 
	     high_queue = 0; }

	   while ( iter_begin != iter_end )
	   {
	      int host_flag = 0;  // are there any hosts available?
	      list<string>::iterator it_find; // avail_hosts list iterator

	      // (a1) if job's status is "suspend," skip it
	      if ( !strcasecmp(iter_begin->status.c_str(), "suspend") )
	      {
		 ++iter_begin;
		 continue;
	      }

	      // (a) figure out what the host should be 
	      if ( !avail_hosts.empty() ) 
	      {
		 // check if user specified onlyhost 
		 if ( strcasecmp(iter_begin->packet.onlyhost, "") )
		 {
		    it_find = find(avail_hosts.begin(), avail_hosts.end(), 
							iter_begin->packet.onlyhost);    
		    if ( it_find != avail_hosts.end() )
		    {
			strcpy(assigned_host, it_find->c_str());
			host_flag = 1;
		    }
		 } 
		 // else if user specified prefhost, then see if this host is available
		 else if ( strcasecmp(iter_begin->packet.prefhost, "") )
		 {
		    host_flag = 1;
		    it_find = find(avail_hosts.begin(), avail_hosts.end(),
							iter_begin->packet.onlyhost);

		    if ( it_find != avail_hosts.end() )
			strcpy(assigned_host, it_find->c_str());
		    else
		    {
			// just get the first hostname off of the avail_hosts list
			it_find = avail_hosts.begin();
			strcpy(assigned_host, it_find->c_str());
		    }
		 }
		 // else, just get the first hostname off of the avail_hosts list
		 else
		 {
	    	    host_flag = 1;
		    it_find = avail_hosts.begin();
		    strcpy(assigned_host, it_find->c_str());			   
		 }
	      }

	      // (b) if a host is available, check if the licenses are available  
	      if ( host_flag )
	      {
	         int license_flag  = 1;  // are the licenses available?
		 map<string, int>::iterator license_iter;
  		 for ( int i = 0; i < iter_begin->license_vector.size(); i++ )
		 {
		    license_iter = avail_licenses.find(iter_begin->license_vector[i]);

		    if ( license_iter->second <= 0 )
			license_flag = 0;
		 } 

		 // (b1) if the licenses are available (we think licenses are available)
		 if ( license_flag )
		 {
		    // call the "lmleft" function to ensure that there really are licenses available
		    int license_notavail = 0;
		    map<string, string>::iterator it_licensefile;
  		    for ( int i = 0; i < iter_begin->license_vector.size(); i++ )
		    {
		       it_licensefile = license_files.find(iter_begin->license_vector[i]);
		       if (it_licensefile != license_files.end())
		       {
		          // license is really not available 
		          if ( lmleft(it_licensefile->second.c_str(), it_licensefile->first.c_str()) <= 0 )
			  {
			     license_notavail = 1;
			     break;
			  }
		       }
		    }	 

		    // if licenses are not available
		    if (license_notavail)
		    {
		       ++iter_begin;
		       continue;
		    }

		    // interactive mode: send hostname back to queue client
		    if ( !strcasecmp(iter_begin->packet.mode, "interactive") )
		    {
			// call hlavail_i function
			// (note: we stored the socket descriptor within the job element
			// itself, so we haven't closed off the connection yet)
		 	if ( hlavail_i(*iter_begin, iter_begin->sockfd, it_find) != 0 )
			{
			   close(iter_begin->sockfd);

			   // delete the job off of job_find
		   	   map<string, task_packet>::iterator it_jobfind;
		   	   it_jobfind = job_find.find(iter_begin->job_id);
			   job_find.erase(it_jobfind);
			}
			else
			{
		   	   // change the queue_type of the job element in job_find
		   	   map<string, task_packet>::iterator it_jobfind;
		   	   it_jobfind = job_find.find(iter_begin->job_id);
		   	   it_jobfind->second.queue_type = "intermediate"; 
		   	   it_jobfind->second.hostname = assigned_host; 
			}

			// delete the job off of the waiting queue

			// temp2_iter points to the next element in the queue
			list<job_info>::iterator temp1_iter = iter_begin;
			list<job_info>::iterator temp2_iter = ++temp1_iter;

			if ( high_queue == 1 )
			{
			   high_waiting.erase(iter_begin);
			
			   // update the iterators
			   iter_begin = temp2_iter;
			   iter_end = high_waiting.end();
			}
			else
			{
			   low_waiting.erase(iter_begin);

			   // update the iterators
			   iter_begin = temp2_iter;
			   iter_end = low_waiting.end();
			}

			continue; // go back to the while loop
		    }
		    // else, in batch mode
		    else
		    {
			// call hlavail_b function
			int return_val;
			if ( (return_val = hlavail_b(*iter_begin, 1, it_find)) == 1 )
			   continue; // fork failed, so job is still in waiting queue
			else if ( return_val == 0 )
			{
		   	   // change the queue_type of the job element in job_find
		   	   map<string, task_packet>::iterator it_jobfind;
		   	   it_jobfind = job_find.find(iter_begin->job_id);
		   	   it_jobfind->second.queue_type = "intermediate"; 
		   	   it_jobfind->second.hostname = assigned_host; 
			}
			else
			{
			   // delete the job off of job_find
		   	   map<string, task_packet>::iterator it_jobfind;
		   	   it_jobfind = job_find.find(iter_begin->job_id);
			   job_find.erase(it_jobfind);
			}

			// delete the job off of the waiting queue

			// temp2_iter points to the next element in the queue
			list<job_info>::iterator temp1_iter = iter_begin;
			list<job_info>::iterator temp2_iter = ++temp1_iter;

			if ( high_queue == 1 )
			{
			   high_waiting.erase(iter_begin);
			
			   // update the iterators
			   iter_begin = temp2_iter;
			   iter_end = high_waiting.end();
			} 
			else
			{
			   low_waiting.erase(iter_begin);

			   // update the iterators
			   iter_begin = temp2_iter;
			   iter_end = low_waiting.end();
			}

			 continue; // go back to the while loop
		    }
		 }
		 // (b2) if the licenses are not available
		 else
		 {
		   #ifdef CONFISCATE 
		   if ( high_queue == 1 )
		   {		   
			// call the function (if function returns 0, means that
			// everything went well; nonzero otherwise)
			if ( hl_unavail(*iter_begin, iter_begin->sockfd, 1, it_find) != 0 )
			{
			   // something went wrong
			}
		 	else
			{
			   // change the queue_type of the job in job_find
			   map<string, task_packet>::iterator mfind = job_find.find(
								iter_begin->job_id);
			   mfind->second.queue_type = "intermediate";
			   mfind->second.hostname = assigned_host;

			   // delete the job element off of the waiting queue
			   list<job_info>::iterator temp1_iter = iter_begin;
			   list<job_info>::iterator temp2_iter = ++temp1_iter;

			   high_waiting.erase(iter_begin);
	
			   // update the iterators
			   iter_begin = temp2_iter;
			   iter_end = high_waiting.end();

			   continue;  // go back to the while loop
			}
		   } 
		   #endif

		   // otherwise, the job is low_priority, so do nothing
		 }

		}
 
	      ++iter_begin;
	   }
	}

	return 0;
}

// checks if any of the queue daemons are down
int check_queued()
{
   // a temporary list to hold any servers that are down 
   list<string> downservers;

   // get the current time
   struct timeval time;
   gettimeofday(&time, NULL);

   // now iterate through each element of the communicate data structure, deleting any servers
   // where the time has expired
   map<string, sockfd5_commun>::iterator it_begin = communicate.begin(), it_end = communicate.end();
   while (it_begin != it_end)
   {
      if ( (time.tv_sec - it_begin->second.time.tv_sec) > MAXQUEUEDTIME )
         downservers.push_back(it_begin->first);

      ++it_begin;
   }

   // if the temporary list is not empty, call the defective server function
   list<string>::iterator itb = downservers.begin(), ite = downservers.end();
   while (itb != ite)
   {
      defective_hosts.push_back(*itb);
      defective_server(*itb);
      ++itb;
   }
 
   return 0;
}

// checks if there are any job messages that we can process
int check_jobmsg()
{  
   	   // set up the handler just in case we get a SIGPIPE signal (which will crash the queue_manager!)
   	   signal(SIGPIPE, sigpipe_handler); 

	   map<string, task_packet>::iterator it_jobfind;
	   list<sockfd4_packet>::iterator itb_jobmsg = job_messages.begin(),
	   					ite_jobmsg = job_messages.end();

	   while (itb_jobmsg != ite_jobmsg)
	   {
	      // find this element in job_find
	      it_jobfind = job_find.find(itb_jobmsg->job_id);

	      // if the element is not in job_find, then delete this element
	      if ( it_jobfind == job_find.end() )
	      {
	         list<sockfd4_packet>::iterator temp1_iter = itb_jobmsg;
		 list<sockfd4_packet>::iterator temp2_iter = ++temp1_iter;

		 job_messages.erase(itb_jobmsg);

		 // update the iterators
		 itb_jobmsg = temp2_iter;
		 ite_jobmsg = job_messages.end();

		 continue;
	      }
	      else
	      {
		 // if job is in the intermediate queue, then skip this job
		 if ( it_jobfind->second.queue_type == "intermediate" )
		 {
		    ++itb_jobmsg;
		    continue;
		 }
		 else 
		 {
		    // if its licenses are confiscated
		    if ( it_jobfind->second.queue_type == "low_running" )
		    {
		        map<string, job_info>::iterator iter_find =
		       				low_running.find(it_jobfind->second.hostname);
			if ( iter_find != low_running.end() )
			   if ( iter_find->second.confiscated )
			   {
			      ++itb_jobmsg;
			      continue;
			   }
		    }

		    // if got to here, means that job is not in intermediate queue and
		    // licenses are not confiscated, so call the task_control function
              	    task_control(*itb_jobmsg, it_jobfind);
		 }
	      }

	      ++itb_jobmsg;
	   }

	   return 0;
}

// creates a new job to put in the high_running queue
int create_newjob(struct sockfd3_packet packet)
{
   struct job_info temp_job;  // job_info element to insert into some queue
   struct info_packet temp_info;
   strcpy(temp_info.priority, "high");
   strcpy(temp_info.user, packet.user);
   strcpy(temp_info.datesubmit, "");
   strcpy(temp_info.job, "");
   temp_info.user_id = packet.user_id;
   strcpy(temp_info.mode, "interactive");
   temp_job.host_running = packet.hostname;
   temp_job.status = "running";
   temp_job.confiscated = 0;
   temp_job.job_pid = packet.job_pid;
   temp_job.packet = temp_info;

   // generate a random number for the job_id
   for(;;)
   {
      long int rand_num = random() % MAXJOBIDMOD;
      char uid[10]; char rnum[30];
      sprintf(uid, "%d%s", temp_job.packet.user_id, "_");
      sprintf(rnum, "%ld", rand_num);
      temp_job.job_id = uid;
      temp_job.job_id = temp_job.job_id + rnum;

      // check to make sure that job id does not exist already
      map<string, task_packet>::iterator it_find = job_find.find(temp_job.job_id);
      if ( it_find == job_find.end() )
	 break;
   }

   // insert task_job into job_find
   struct task_packet task_job; 
   task_job.uid = temp_job.packet.user_id;
   job_find.insert( map<string, task_packet>::value_type(temp_job.job_id, task_job) );

   // put job in high_running queue
   high_running.insert( map<string, job_info>::value_type(packet.hostname, temp_job) );

   return 0;
}

// moves the packet from the intermediate queue to the high_running queue
int move_job(map<string, job_info>::iterator it_find, struct sockfd3_packet packet)
{
   struct job_info temp_job;			
   temp_job = it_find->second;
   temp_job.status = "running";
   temp_job.job_pid = packet.job_pid;

   intermediate.erase(it_find);

   if  ( !strcasecmp(temp_job.packet.priority, "high") )
   {
	high_running.insert( map<string, job_info>::value_type(packet.hostname, temp_job) );

	// change the queue_type of the job element in job_find
	map<string, task_packet>::iterator it_jobfind;
	it_jobfind = job_find.find(temp_job.job_id);
	it_jobfind->second.queue_type = "high_running"; 
   }
   else
   {
   	low_running.insert( map<string, job_info>::value_type(packet.hostname, temp_job) );

	// change the queue_type of the job element in job_find
	map<string, task_packet>::iterator it_jobfind;
	it_jobfind = job_find.find(temp_job.job_id);
	it_jobfind->second.queue_type = "low_running"; 
   }

   return 0;
}

void sigpipe_handler(int)
{
   #ifdef DEBUG
   cout << "In SIGPIPE Handler" << endl;
   #endif
}

int lmleft (const char* given_licfile, const char* given_feature)
{
    char  cmd[1024];
    //------------------------------------------------------------------------------
    // Run the lmstat command and redirect the output to a temporary file.
    //------------------------------------------------------------------------------
    sprintf(cmd, "%s %s > %s", LMCMD, given_licfile, TEMPFILE);
    //printf("EXEC: %s\n", cmd);
    if (system(cmd))
    {
        fprintf(stderr, "ERROR: Command exited in error: \"%s\".\n", cmd);
        return ERR_SYS_FAILURE;
    }
    //------------------------------------------------------------------------------
    // Retreive the output from the temporary file for parsing.
    //------------------------------------------------------------------------------
    FILE* LMOUT;
    LMOUT=fopen(TEMPFILE, "r");
    if (NULL==LMOUT) {
        return ERR_NO_TMP_FILE;
    }

    int  feature_found    = 0;
    int  feature_totallic = 0;
    int  feature_inuse    = 0;
    char line[LMSTAT_MAX_LINE_SIZE];
    char feature[LMSTAT_MAX_LINE_SIZE];
    int  totallic;

    //------------------------------------------------------------------------------
    // Parse the output of the lmstat command line by line.
    //------------------------------------------------------------------------------
    while (!feof(LMOUT))
    {
        fgets(line, LMSTAT_MAX_LINE_SIZE, LMOUT);  // put next line into "line" var

        if (strstr(line, "Cannot find license file") || strstr(line, "Error getting status"))
        {
            fprintf(stderr, "ERROR: Given license file, \"%s\",  not found.\n", given_licfile);
            return ERR_NO_LM_FILE;
        }
        else if (sscanf(line, "Users of %[^:]:  (Total of %d licenses available)", feature, &totallic))
        {
            //printf("feature: %s, total licenses: %d\n", feature, totallic);
            if (feature_found) {
                break;  // This is the beginning of the listing for the next feature.
            } else if (! strcasecmp(feature, given_feature)) {
                feature_found = 1;
                feature_totallic = totallic;
            }
        }
        else if (feature_found && strstr(line, ", start "))
        {
            //printf("  in use: %s", line);
            feature_inuse++;
        }
    }
    if (fclose(LMOUT)) {
        return ERR_NO_TMP_FILE;
    }

    if (!feature_found)
    {
        fprintf(stderr, "ERROR: Given feature, \"%s\", not found.\n", given_feature);
        return ERR_NO_FEATURE;
    }
    if (feature_totallic <= 0)
    {
        fprintf(stderr, "ERROR: No available licenses for given feature, \"%s\".\n", given_feature);
        return ERR_NO_LIC_AVAIL;
    }

    //------------------------------------------------------------------------------
    // Make the return status of this script equal to the number of licenses left for use.
    //------------------------------------------------------------------------------
    int num_left = feature_totallic - feature_inuse;

    #ifdef DEBUG
    printf("NUM LICENSES LEFT: %d\n", num_left);
    #endif

    return num_left;
}

#else
main()
{
}

#endif /*NO_QUEUE_MANAGER*/
