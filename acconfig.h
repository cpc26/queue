
#define PACKAGE Queue

#undef HAVE_UT_ADDR

/*Version is set by ./configure. */
#define VERSION undefined

/*
set this to a suitable user to receive error messages from Queue.
It is automatically set to the installing user by ./configure */

#define QUEUE_MAIL_USERID	"root"		

/*Set the following to where the queued_pid file should reside.
Queued will append a hostname to the end of the file, so
this file may be placed in a shared directory, or it may reside in
a directory by itself. The important thing is that the directory
be writable by queued. It is automatically set from localstatedir
by ./configure */

#define PIDFILE "/usr/local/var/queued.pid"

/*Set the following to where the NFS-shared spool directory for Queue
should reside. This is set automatically from localstatedir 
by ./configure .*/

#define QUEUEDIR /usr/local/var/queue

/*Cookiefile is used in the NO_ROOT environment to allow Queue to
  authenticate itself to QueueD.*/

#define COOKIEFILE /usr/local/share/queuecookiefile

/*Set the following to where you want the host access control list file
to reside. This is set automatically from sysconfdir by ./configure .*/

#define QHOSTSFILE /usr/local/share/qhostsfile

/* Define the following if identd service (RFC 931) is reliable throughout
   the cluster.

   Because Queue uses privileged ports when running as root, this is only 
   useful and honored when you are installing Queue
   for a single user without root privileges. (no_root is defined.) */

#undef HAVE_IDENTD

/* Define the following if you are installing this for use
   by a single user without root privileges.

   Although this is the default ./configure option (because users without root
   privileges are assumed to be less sophisticated), the preferred
   means of installing Queue is cluster-wide, which requires
   setuid root bits on the queue executable.

   See the manual for more information on these issues.*/

#undef NO_ROOT

/*Compile-in user and kernel-level checkpointing support.*/

#undef ENABLE_CHECKPOINT

/*Define if have the misfortune to be running Solaris*/
#undef solaris

/*Define if running on digital.*/
#undef BROKEN_TERMIOS_VMIN


/*Controls whether or not QUEUE_MANAGER package is compiled in.*/
#undef NO_QUEUE_MANAGER



