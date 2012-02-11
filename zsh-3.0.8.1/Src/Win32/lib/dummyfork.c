/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/*
 * oldfaber:
 *      dummy functions of the *nix C library that do not exist in win32.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#include <uni_types.h>

#include "sys/times.h"

/* hack for configure */
static int forcefork;

gid_t getgid(void)
{
	return 0;
}

uid_t getuid(void)
{
	return 0;
}

gid_t getegid(void)
{
	return 0;
}

uid_t geteuid(void)
{
	return 0;
}

pid_t getppid(void)
{
	return 0;
}

unsigned int sleep(unsigned int seconds) 
{
        return (0);
}

struct passwd *getpwnam(const char *name)
{
       	return NULL;
}

struct passwd *getpwuid(uid_t uid)
{
	return NULL;
}

struct group * getgrnam(const char *name)
{
	return NULL;
}

pid_t getpgrp(void)
{
        return (0);
}

pid_t setpgrp(void)
{
        /* needed for zsh 3.0.8 configure test */
        return (0);
}

int setpgid(pid_t pid, pid_t pgid)
{
        /* always succeed */
        return (0);
}

char *getlogin(void)
{
	return NULL;
}

struct passwd * getpwent(void)
{
        return (NULL);
}

void endpwent()
{
        return;
}

void setpwent()
{
        return;
}

int getgroups(int gidsetsize, gid_t grouplist[])
{
	/* 0 groups */
	return (0);
}

/* not used for zsh 3.0.8 */
struct group *getgrgid(gid_t gid)
{
	return (NULL);
}

int gethostname(char *buf, int len)
{
        return (-1);
}

char *ttyname(int fd)
{
        return NULL;
}

int readlink(const char *path, char *buf, size_t len)
{
	/* always fail */
        return (-1);
}

/* not used for zsh 3.0.8 */
int link(const char *oldname, const char *newname)
{
	/* always fail */
        return (-1);
}

clock_t times(struct tms *ignore)
{
	return (clock_t)(-1);
}

int fork(void)
{
	if (forcefork) {
		forcefork = 0;
                return (0);
	}
        errno = ENOMEM;
        return (-1);
}

/* tested by configure */
void *sbrk(ptrdiff_t delta)
{
        return NULL;
}

int waitpid(pid_t pid, int *statloc, int options)
{
        return (-1);
}

int kill(int pid, int sig) 
{
        return (-1);
}

int nice(int niceness)
{
        return (-1);
}

unsigned int alarm(unsigned int seconds)
{
        return 0;
}

int fcntl(int fd, int command, ...)
{
        return 0;
}

int pipe(int pipefd[2])
{
        return (-1);
}

/* signal handling */
void _nt_signal(void) 
{
}

int sigaction(int signo, void *act, void *oact)
{
	if (signo == 15)
                forcefork++;
        return 0;
}

int sigaddset(void *set, int signo)
{
        return 0;
}

int sigdelset(void *set, int signo)
{
        return 0;
}

int sigprocmask(int how, void *set, void *oset)
{
        return 0;
}

int sigsuspend(void *mask)
{
        /* return success ! */
	errno = EINTR;
	return (-1);
}

/* needed for zsh 3.0.8 configure tests */
pid_t wait(int *status)
{
        return 0;
}

#if !(defined(__MINGW32__)||defined(__MINGW64__))
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	return 0;
}
#endif
