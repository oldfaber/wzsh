/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* sys/resource.h - Windows minimal implementation */


#ifndef	_SYS_RESOURCE_H
#define	_SYS_RESOURCE_H 1

#if !defined(HAVE_RLIM_T)
# define HAVE_RLIM_T 1
typedef unsigned long rlim_t;
#endif

#endif
