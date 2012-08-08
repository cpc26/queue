/* queue_endian.h - sets up BIG/LITTLE endian defines for queue */

/* EJD 8/14/2000 */
/* Determine system endianness for sha/networking
 *
 * This header tries to pick out the little endian systems based
 * on #defines that exist.  It explicitly looks for little-endian
 * defines on various platforms, and if found, it sets the
 * LITTLE_ENDIAN_HOST variable for use in sha1.c and various
 * portions of queue that handle creation of endian-neutral
 * job files.
 *
 * If at the end of the search for little-endian defines, the
 * LITTLE_ENDIAN_HOST is not defined, we assume that the host
 * is big-endian and set the BIG_ENDIAN_HOST define instead.
 *
 * If you add checking for more platforms, please indicate the
 * platform before the check.
 */

#ifndef _QUEUE_ENDIAN_H
#define _QUEUE_ENDIAN_H

/* Linux */
#ifdef BYTE_ORDER
  #if BYTE_ORDER == __LITTLE_ENDIAN
    #define LITTLE_ENDIAN_HOST
  #endif
#endif

/* Solaris */
#ifdef _LITTLE_ENDIAN
  #define LITTLE_ENDIAN_HOST
#endif

/* ---------------------------------------------------- */
/* If no little endian defines found, assume big endian */
/* ---------------------------------------------------- */

#ifndef LITTLE_ENDIAN_HOST
  #define BIG_ENDIAN_HOST
#endif

#endif /* _QUEUE_ENDIAN_H */

/* end of queue_endian.h */
