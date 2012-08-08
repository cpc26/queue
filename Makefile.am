
#Makefile.am part of GNU Queue. Copyright 1998-2000 W. G. Krebs
#Distributed under GPL. Absolutely no warranty. See file COPYING for
#details and terms of license.

sbin_PROGRAMS = queued queue_manager task_manager
bin_PROGRAMS = queue task_control 

#WGK 2000/08/11 Don't install random anywhere
#"rdiego@iol.it"<rdiego@iol.it> pointed out that random must be
#installed by Makefile.am (rather than in install-local-stuff)
#because it needs to link gethostbyname under Solaris.

noinst_PROGRAMS = random
random_SOURCES = random.c qlib.c

queued_SOURCES = queued.c lex.l handle.c ident.c pty.c qlib.c mrestart.c wakeup.c sha1.c
queued_LDADD = @LEXLIB@ @LIBOBJS@
queue_SOURCES = queue.c wakeup.c ident.c qlib.c sha1.c
queue_LDADD = @LIBOBJS@

queue_manager_SOURCES = queue_manager.cc queue_define.h
queue_manager_LDADD = @LIBOBJS@

task_manager_SOURCES = task_manager.cc queue_define.h
task_manager_LDADD = @LIBOBJS@

task_control_SOURCES = task_control.cc queue_define.h
task_control_LDADD = @LIBOBJS@


install-exec-local: install-local-stuff
	$(INSTALL) -d @bindir@ >/dev/null 2>&1 || (mkdir @bindir@; exit 0) 
	$(INSTALL) -d @sbindir@ > /dev/null 2>&1 || (mkdir @sbindir@ ; exit 0)
	$(INSTALL) -m @MODE@ -o @OWNER@ queue ${bindir}/queue
	$(INSTALL) -m 700 -o @OWNER@ queued ${sbindir}/queued
	(ln -s ${bindir}/queue ${bindir}/qsh; exit 0)
	(ln -s queue qsh; exit 0)

install-local-stuff:
	$(INSTALL) -o @OWNER@ -m 700 -d @localstatedir@/ || (mkdir @datadir@; chmod 700 @datadir@; exit 0)
	$(INSTALL) -d @datadir@ > /dev/null 2>&1 || (mkdir @datadir@; exit 0)
	if test ! -s @datadir@/queuecookiefile ; then ./random > @datadir@/queuecookiefile; chmod 400 @datadir@/queuecookiefile; fi
	echo Adding this host to Queue host access control list file...
	HOSTNAME=`hostname`; grep $$HOSTNAME @datadir@/qhostsfile; if test $$? -ne 0 ; then echo $$HOSTNAME >> @datadir@/qhostsfile; fi; exit 0
	(chmod 644 @datadir@/qhostsfile >/dev/null 2>&1; exit 0) 
	$(INSTALL) -d @localstatedir@ || (mkdir @localstatedir@; exit 0)

install-data-local:
	$(INSTALL) -d @localstatedir@ || (mkdir @localstatedir@; exit 0)
	$(INSTALL) -o @OWNER@ -m 700 -d @localstatedir@/queue/ || (mkdir @localstatedir@/queue; chmod 700 @localstatedir@/queue; exit 0)
	$(INSTALL) -o @OWNER@ -m 700 -d @localstatedir@/queue/now || (mkdir @localstatedir@/queue/now; chmod 700 @localstatedir@/queue/now; exit 0)
	$(INSTALL) -o @OWNER@ -m 700 -d @localstatedir@/queue/wait || (mkdir @localstatedir@/queue/wait; chmod 700 @localstatedir@/queue/wait; exit 0)
	$(INSTALL) -o @OWNER@ -m 600 profile @localstatedir@/queue/now
	$(INSTALL) -o @OWNER@ -m 600 profile @localstatedir@/queue/wait
	$(INSTALL) -m 755 -d @infodir@ || ( mkdir @infodir@; chmod 755 @infodir )
	$(INSTALL) -m 644 doc/queue.info @infodir@
	(install-info --infodir=@infodir@ @infodir@/queue.info; exit 0)
	$(INSTALL) -m 755 -d @mandir@/man1 || ( mkdir @mandir@; mkdir @mandir@/man; chmod 755 @mandir@ @mandir@/man1 )
	cp doc/queue.man doc/queue.1
	$(INSTALL) -m 644 doc/queue.1 @mandir@/man1/
	(ln @mandir@/man1/queue.1 @mandir@/man1/qsh.1; exit 0)

uninstall-local: uninstall-local-exec uninstall-local-data

uninstall-local-data:
	(rm -rf @localstatedir@/queue; exit 0)
	(rm -rf @localstatedir@/queued.pid*; exit 0)
	echo You may wish to delete @datadir@/qhostsfile as well.

uninstall-local-exec:
	(rm  ${bindir}/queue; exit 0)	
	(rm  ${sbindir}/queued; exit 0)	

