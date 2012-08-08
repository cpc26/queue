#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_ARPA_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_FSTAB_H
#include <fstab.h>
#endif
#ifdef HAVE_I386_VMPARAM_H
#include <i386/vmparam.h>
#endif
#ifdef HAVE_MACHINE_PARAM_H
#include <machine/param.h>
#endif
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#ifndef __hpux
#ifdef HAVE_NDIR_H
#include <ndir.h>
#endif
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NLIST_H
#include <nlist.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_SYS_AUDIT_H
#include <sys/audit.h>
#endif
#ifdef HAVE_DIR_H
#include <sys/dir.h>
#endif
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#ifdef HAVE_SYS_FIXPOINT_H
#include <sys/fixpoint.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#ifndef sun
#include <sys/ioctl.h>
#endif
#endif
#ifdef HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_PSTAT_H
#include <sys/pstat.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#else
#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#endif
#ifdef HAVE_SYS_SYSMP_H
#include <sys/sysmp.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif
#include <termios.h>
#ifdef TM_WITH_SYS_TIME
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#endif
#ifdef HAVE_ULIMIT_H
#include <ulimit.h>
#endif
#ifdef HAVE_UNISTD_H
/*GNU/Linux wants this or it won't include crypt.*/
#define _XOPEN_SOURCE
#include <unistd.h>
#endif
#ifdef HAVE_USTAT_H
#include <ustat.h>
#endif
#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif
#ifdef HAVE_VFORK_H
#include <vfork.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifndef HAVE_GETOPT_LONG
#include "getopt_long.h"
#else
#ifdef HAVE_GETOPT_H
#include "getopt.h"
#endif
#endif
