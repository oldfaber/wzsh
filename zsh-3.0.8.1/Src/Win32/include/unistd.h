/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* unistd.h -- for *nix challenged environments */


#ifndef	_UNISTD_H
#define	_UNISTD_H	1

/* functions missing from this file may be found in */
#include <process.h>
#include <stddef.h>
#include <io.h>

#include "uni_types.h"

#if defined(__MINGW32__)||defined(__MINGW64__)
# include <stdint.h>
# include <sys/time.h>
#endif
#if defined(__GNUC__)
# include_next <unistd.h>
#endif

#if defined(_MSC_VER)
/* NOTE: Microsoft CRT gives a runtime parameter error for access(s, X_OK) */
/* file test macros:
   00  Existence only           01  Executable
   02  Write permission         04  Read permission
   06  Read and write permission
*/
# define F_OK 0
# define X_OK 1
# define W_OK 2
# define R_OK 4
#endif

/* these are command values for fcntl(), not found in windows fcntl.h */
#define	F_DUPFD	0      	/* duplicate file descriptor */
#define	F_GETFD	1      	/* get file descriptor flags */
#define	F_SETFD	2      	/* set file descriptor flags */
#define	F_GETFL	3      	/* get file status flags */
#define	F_SETFL	4      	/* set file status flags */
/* requests, not found in windows fcntl.h */
#define	FD_CLOEXEC 1	
/* get/set non blocking mode, not found in windows fcntl.h */
#define	O_NONBLOCK	0x0004

#ifdef	__cplusplus
extern "C" {
#endif

/* windows declaration in winsock.h differs ... */
#if !(defined(_MSC_VER) && defined(_WINSOCKAPI_))
int gethostname(char *buf, int len);
#endif

int readlink(const char *__path, char *__buf, size_t __len);
char *getlogin(void);
char *ttyname(int fd);
int nice(int increment);
/* MSVC runtime only has _pipe() */
int pipe(int hpipe[2]);
int fork(void);
#if !defined(__MINGW32__)
/* defined in sys/time.h with a different prototype */
int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
pid_t getppid(void);
pid_t getpgrp (void);
int setuid(uid_t newuid);
int setgid(gid_t newgid);
int seteuid(uid_t neweuid);
int setegid(gid_t newegid);
int setpgid(pid_t pid, pid_t pgid);
int getgroups(int gidsetsize, gid_t grouplist[]);
unsigned int sleep(unsigned int seconds);
int chown(const char *filename, uid_t owner, gid_t group);
int link(const char *oldname, const char *newname);
/* usually declared in stdlib.h */
int setenv(const char *name, const char *value, int replace);
int unsetenv(const char *name);

/* this function is available in the fork library */
void *sbrk(ptrdiff_t delta);

/* this function should be in fcntl.h, but windows compiler already have
   a sys/fcntl.h file, lacking this function */
int fcntl(int, int, ...);

#ifdef	__cplusplus
}
#endif

#endif /* unistd.h  */
