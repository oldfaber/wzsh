/* 
 * sys/wait.h
 * This file has no copyright assigned and is placed in the Public Domain.
 * 
 * This software is provided as-is. Use it at your own risk. No warranty is given,
 * neither expressed nor implied, and by using this software you accept
 * that the author(s) shall not be held liable for any loss of data, loss of service,
 * or other damages, be they incidental or consequential.
 *
 * waitpid() function and macros often found in POSIX systems
 */


#ifndef	_SYS_WAIT_H_
#define	_SYS_WAIT_H_

#if defined(_MSC_VER)
# if !defined(_PID_T_DEFINED)
#  define _PID_T_DEFINED
typedef int pid_t;
# endif
#else
# include <sys/types.h>
#endif

# define WNOHANG	0
# define WUNTRACED	1

# define WIFEXITED(a)	1
# define WEXITSTATUS(a) (a)
# define WIFSIGNALED(a) ((a!=-1) && ((((unsigned long)(a)) & 0xC0000000L) != 0))
# define WTERMSIG(a)	(((unsigned long)(a))==0xC000013AL?SIGINT:SIGSEGV)
# define WCOREDUMP(a)	0
# define WIFSTOPPED(a)	0
# define WSTOPSIG(a)	0

# ifdef __cplusplus
extern "C" {
# endif

int waitpid(pid_t pid, int *stat_loc, int options);

# ifdef __cplusplus
}
# endif

#endif
