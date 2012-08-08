GNU Queue load-balancing/batch processing system

Copyright 1998-2000 W. G. Krebs (see file 'COPYING' for terms of GPL license)
<wkrebs@gnu.org> 

GNU Queue is distributed in the hope that it is useful but comes 
with absolutely no warranty; see file `COPYING' for details.

INTRODUCTION


This load-balancing system features an innovative 'stub daemon'
mechanism which allows users to control their remote jobs in a nearly
seamless and transparent fashion within a cluster of homogeneous
UNIX workstations. When an interactive remote job is
launched, such as say Matlab, or EMACS interfacing Allegro Lisp, a
stub daemon runs on the remote end. By sending signals to the remote
stub - including hitting the suspend key - the process on the remote
end may be controlled. Resuming the stub resumes the remote job. The
user's environment is almost completely replicated, including not only
environmental variables, but nice values, rlimits, terminal settings
are all replicated on the remote end. Together with MIT_MAGIC_COOKIE_1
(or xhost +; yuck) the system is X-windows transparent as well,
provided the users local DISPLAY variable is set to the fully
qualified pathname of the local machine. 

One of the most appealing features of the stub system even with
experienced users is that asynchronous job control of remote jobs by
the shell is possible and intuitive. One simply runs the stub in the
background under the local shell; the shell notifies the user when the
remote job has a change in status by monitoring the stub daemon. 

When the remote process has terminated, the stub returns the exit
value to the shell; otherwise, the stub simulates a death by the same
signal as that which terminated or suspended the remote job. In this
way, control of the remote process is intuitive even to novice users,
as it is just like controlling a local job from the shell. Many of my
original users had to be reminded that their jobs were, in fact,
running remotely. 

In addition, queue also features a more traditional distributed batch
processing environment, with results returned to the user via
email. In addition, traditional batch processing limitations may be
placed on jobs running in either environment (stub or with the email
mechanism) such as suspension of jobs if the system exceeds a certain
load average, limits on CPU time, disk free requirements, limits on
the times in which jobs may run, etc. (These are documented in the
sample 'profile' file included and in the manuals in `doc/' as well.) 

This version of GNU Queue features (we hope) a nearly
blinking-lights automatic installation process described in
the `INSTALL' file.

The major decision to make is whether or not you wish to install
GNU Queue cluster-wide for all users (requires root privileges
and an NFS shared directory that is root-writable throughout the cluster)
or simply for a single user (which requires no special privileges but does
requires an NFS shared directory writable by that user, something
typical in a homogenuous cluster.)

This decision affects whether or not you supply `./configure' the
--enable-root option. The rest of the installation decisions
are normally taken care of by `./confiugre', see `INSTALL' for further
details.

For additional information please refer to the manuals in `doc/'.

PLEASE SEND US FEEDBACK ON QUEUE!

Whether you have a queue-tip, are queue-less about how to solve a
problem, or simply have another bad queue joke, que[ue] us in at
bug-queue@gnu.org and we'll take our que[ue] from you on how best
to improve the software and documentation.

We have set up a developing mailing list, 'queue-developers.'
Currently, the list is working on several fun projects, including MPI
& PVM support, secure socket connections, AFS & Kerberos support. We're also
porting and improving a nifty utility that lets you monitor and control 
the execution of Queue jobs throughout your cluster. The list is a great way 
to tap into the group's expertise and keep up with the latest developments.

So, come join the fun and keep up with the latest developments by visiting
http://lists.sourceforge.net/mailman/listinfo/queue-developers .

Be sure to visit the application's homepage, 
http://www.gnuqueue.org .



