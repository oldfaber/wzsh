/* 
 * sys/times.h
 * This file has no copyright assigned and is placed in the Public Domain.
 * 
 * This software is provided as-is. Use it at your own risk. No warranty is given,
 * neither expressed nor implied, and by using this software you accept
 * that the author(s) shall not be held liable for any loss of data, loss of service,
 * or other damages, be they incidental or consequential.
 *
 * times() function and structures often found in POSIX systems
 */


#ifndef	_SYS_TIMES_H
#define	_SYS_TIMES_H	1

#include <time.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Structure describing CPU time used by a process and its children.  */
struct tms
  {
    clock_t tms_utime;		/* User CPU time.  */
    clock_t tms_stime;		/* System CPU time.  */

    clock_t tms_cutime;		/* User CPU time of dead children.  */
    clock_t tms_cstime;		/* System CPU time of dead children.  */
  };


/* Store the CPU time used by this process and all its
   dead children (and their dead children) in BUFFER.
   Return the elapsed real time, or (clock_t) -1 for errors.
   All times are in CLK_TCKths of a second.  */
extern clock_t times (struct tms *__buffer);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TIMES_H */
