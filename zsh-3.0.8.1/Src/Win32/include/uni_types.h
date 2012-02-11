/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* uni_types.h - Missing types from sys/types.h */


#ifndef	_UNI_TYPES_H
#define	_UNI_TYPES_H	1

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Microsoft includes unconditionally define this struct */
#if !(defined(_TIMEVAL_DEFINED)||defined(_WINSOCKAPI_))
#define _TIMEVAL_DEFINED
struct timeval{
	long tv_sec;
	long tv_usec;
};
#endif

/* some defines not available in win32 headers,
   the values where scraped from the internet */
#if !defined(S_IFBLK)
# define	S_IFBLK		0x3000	/* never used under win32 */
#endif
#if !defined(S_IFIFO)
# define	S_IFIFO		0x1000	/* FIFO */
#endif
#if !defined(S_IFLNK)
# define	S_IFLNK		0xA000	/* symlink */
#endif
#if !defined(S_ISDIR) && defined(S_IFDIR)
# define S_ISDIR(a)	(((a) & S_IFMT) == S_IFDIR)
#endif	/* ! S_ISDIR && S_IFDIR */
#if !defined(S_ISCHR) && defined(S_IFCHR)
# define S_ISCHR(a)	(((a) & S_IFMT) == S_IFCHR)
#endif /* ! S_ISCHR && S_IFCHR */
#if !defined(S_ISBLK) && defined(S_IFBLK)
# define S_ISBLK(a)	(((a) & S_IFMT) == S_IFBLK)
#endif	/* ! S_ISBLK && S_IFBLK */
#if !defined(S_ISREG) && defined(S_IFREG)
# define S_ISREG(a)	(((a) & S_IFMT) == S_IFREG)
#endif	/* ! S_ISREG && S_IFREG */
#if !defined(S_ISFIFO) && defined(S_IFIFO)
# define S_ISFIFO(a)	(((a) & S_IFMT) == S_IFIFO)
#endif	/* ! S_ISFIFO && S_IFIFO */
#if !defined(S_ISNAM) && defined(S_IFNAM)
# define S_ISNAM(a)	(((a) & S_IFMT) == S_IFNAM)
#endif	/* ! S_ISNAM && S_IFNAM */
#if !defined(S_ISLNK) && defined(S_IFLNK)
# define S_ISLNK(a)	(((a) & S_IFMT) == S_IFLNK)
#endif	/* ! S_ISLNK && S_IFLNK */
#if !defined(S_ISSOCK) && defined(S_IFSOCK)
# define S_ISSOCK(a)	(((a) & S_IFMT) == S_IFSOCK)
#endif	/* ! S_ISSOCK && S_IFSOCK */

#if !defined(UID_T_DEFINED)
# define UID_T_DEFINED
# if defined(uid_t)
/* work-aroud configure */
#  undef uid_t
# endif
typedef int uid_t;
#endif
#if !defined(GID_T_DEFINED)
# define GID_T_DEFINED
# if defined(gid_t)
/* work-aroud configure */
#  undef gid_t
# endif
typedef int gid_t;
#endif
#if defined(_MSC_VER)
typedef int mode_t;
# if !defined(_PID_T_DEFINED)
#  define _PID_T_DEFINED
typedef int pid_t;
# endif
typedef int int32_t;
#endif

#if !(defined(__MINGW32__)||defined(__MINGW64__))
struct timezone {
	int tz_minuteswest;
	int dsttime;
};
#endif

#endif
