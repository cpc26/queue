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

#define QMANAGERHOST "aspen"  // the host name of the qmanager 

/* qmanager port numbers to receive connections */
#define PORTNUM2 2138  // receive from queue clients 
#define PORTNUM3 2139  // receive from queue daemon 
#define PORTNUM4 2140  // receive from task_control clients 
#define PORTNUM5 2120  // receive from queue daemon (communication mechanism) 

/* task_manager port number */
#define C_PORTNUM 2150

#define QDIR "/que/queue20/queue"
#define AVAILHOSTS "/que/queue20/share/qhostsfile"
#define AVAILLICENSES "/que/queue20/my_qdir/licenses" 
#define STATUSFILE "/que/queue20/my_qdir/status"
#define QDEBUGFILE "/que/queue20/my_qdir/debug"
#define TEMPFILE "/que/queue20/my_qdir/temp"

#define MAXUSERNAME 20  // maximum length of a user name
#define MAXLICENSELEN 20  // maximum length of a license name
#define MAXHOSTNAMELEN 64  // maximum length of a host name
#define MAXJOBNAME 100   // job command that the user typed 
#define MAXLOGFILE 200  // maximum length of a log file name
#define MAXQUEUECOMMAND 200  // max length of the queue command
#define MAXJOBID 30 // max length of a job id as well as a server name
#define MAXMESGLEN 20  // max length of a message
#define MAXSTRINGLEN 500 // the maximum length of a single environment string

#define MAXLICENSES 10  // the maximum number of licenses that a job can get
#define MAXLICENSEFILE 200  // the maximum length of a license file
#define MAXENVIRON 200  // the maximum number of lines in a user's environment
#define MAXJOBIDMOD 10000  // the maximum length of a job id (used to mod the random generator)

// (This value pertains only to the qmanager.  It's the maximum value allowed
// for the "starve_counter" variable before the qmanager executes the procedures
// after the "select" timeout.)
#define MAXSTARVECOUNTER 200  

// the maximum number of times that the qmanager can receive the same information 
// from the queue daemon before it actually processes the information (communication mechanism)
#define MAXQUEUEDCOUNTER 5 

// the maximum time in seconds that a queue daemon has before the qmanager 
// declares it as being defective (for the communication mechanism)
#define MAXQUEUEDTIME 600 

// For the communication mechanism, the queue daemons have to periodically make a TCP connection
// to the qmanager.  The MAX_MODULO and MIN_MODULO specifies the range of this periodic 
// random value.  (For example, if the queue daemon SLEEPTIME was 10 seconds, then each
// queue daemon would try to connect to the qmanager every 120 to 360 seconds.)
#define MAX_MODULO 36         
#define MIN_MODULO 12 

// a timer that goes off every 10 seconds in which the qmanager checks for jobs in 
// the waiting queues and writes information out to files (Note that if 
// the qmanager receives any incoming connections, this timer gets reset.)
#define SLEEPTIME 10  

// specifies the number of connection requests that can be queued by the system while
// it waits for the server to execute the "accept" system call
#define QUEUELEN 10 

#define DEBUG  // turns on debugging mode in qmanager

// the following is used within the "lmleft" function in queue_manager.cc
#define LMSTAT_MAX_LINE_SIZE 512
#define LMCMD "/tools/flexlm/lmgrd/currentLinux/lmutil lmstat -a -c"
#define ERR_SYS_FAILURE -1
#define ERR_NO_LM_FILE -2
#define ERR_NO_FEATURE -3
#define ERR_NO_LIC_AVAIL -4
#define ERR_NO_TMP_FILE -5

/* structure that qmanager receives from task_control clients */
struct sockfd4_packet
{
   int uid;  // user id
   char job_id[MAXJOBID];  // the id of the job (unique)
   char host_license[MAXHOSTNAMELEN];  // the server or license
   int maxlicense;  // number of licenses to add or delete
   char licensefile[MAXLICENSEFILE];  // the absolute path of a license file 
   char message[MAXMESGLEN];  // suspend, unsuspend, kill, or change the priority of the job  
   			      // adding/deleting hosts, adding/deleting licenses
};

/* structure that qmanager sends to the task_manager */
struct tm_packet
{
   int job_pid;
   char message[MAXMESGLEN];
};
 
/* structure that qmanager receives from queue daemons */
struct sockfd3_packet
{
   char hostname[MAXHOSTNAMELEN];
   int job_pid;
   int user_id;
   char user[MAXUSERNAME];
};

/* structure that qmanager receives from queue clients */
struct info_packet
{
   char user[MAXUSERNAME];
   int user_id; 
   int gid;
   char datesubmit[30];
   char job[MAXJOBNAME]; 
   char priority[5];
   char mode[12];
   char logfile[MAXLOGFILE];
   char prefhost[MAXHOSTNAMELEN];
   char onlyhost[MAXHOSTNAMELEN];
   char cur_dir[MAXLOGFILE];  
};


