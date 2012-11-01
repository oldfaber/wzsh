/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * oldfaber:
 *      functions defined in unistd.h and missing from win32.
 *      Some functions were extracted from .../tcsh/win32/bogus.c
 *      and have the above copyright, other are stubs or public domain.
 *      Other were written by me and put in the public domain.
 */


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Lmcons.h>     /* for UNLEN max. user name len AFTER windows.h !*/

#include <errno.h>

#include <unistd.h>
#include <sys/times.h>
#include <pwd.h>
#include <grp.h>

#include "forklib.h"


static struct passwd pass_bogus;
static char username[UNLEN + 1];
static char homedir[MAX_PATH + 1];


uid_t getuid(void)
{
	return 0;
}

gid_t getgid(void)
{
	return 0;
}

uid_t geteuid(void)
{
	return 0;
}

gid_t getegid(void)
{
	return 0;
}

pid_t getppid(void)
{
	/* see also http://www.codeguru.com/Cpp/W-P/win32/article.php/c1437/
	   (how to use NtQueryInformationProcess() to get the parent PID)
	   or use a loop on Process32First in a Toolhelp32Snapshot */
	return 0;
}


/* mksh needs these (?) */

int setuid(uid_t newuid)
{
	return 0;
}

int setgid(gid_t newgid)
{
	return 0;
}

int seteuid(uid_t neweuid)
{
	return 0;
}
int setegid(gid_t newegid)
{
	return 0;
}

unsigned int sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}


static void init_passwd(void)
{
	static char shellname[MAX_PATH];
	static char dummy[2];
	DWORD size = sizeof(username);

	if (pass_bogus.pw_name == NULL) {
		GetUserName(username, &size);
		if (GetEnvironmentVariable("HOME", homedir, sizeof(homedir)) == 0)
			strcpy(homedir, "youdonthavehomeset");
		pass_bogus.pw_dir = &homedir[0];
		pass_bogus.pw_name = &username[0];
		/* if shellname[] is too short use a dummy name */
		if (GetModuleFileName(NULL, shellname, sizeof(shellname)) < sizeof(shellname)) {
			char *s = strrchr(shellname, '.');
			if (s)
				*s = '\0';
		} else
			strcpy(shellname, "/bin/sh");
		pass_bogus.pw_shell = shellname;
		pass_bogus.pw_passwd= &dummy[0];
		pass_bogus.pw_gecos = &dummy[0];
		pass_bogus.pw_passwd= &dummy[0];
	}
}


struct passwd *getpwnam(const char *name)
{
	init_passwd();
	if ((pass_bogus.pw_name == NULL) || stricmp(pass_bogus.pw_name, name))
		return NULL;
	return &pass_bogus;
}


/* used to get the home directory */
struct passwd *getpwuid(uid_t uid)
{
	UNREFERENCED_PARAMETER(uid);
	init_passwd();
	return &pass_bogus;
}


struct group *getgrnam(const char *name)
{
	UNREFERENCED_PARAMETER(name);
	return NULL;
}


pid_t getpgrp(void)
{
	return 0;
}


int setpgid(pid_t pid, pid_t pgid)
{
	UNREFERENCED_PARAMETER(pid);
	UNREFERENCED_PARAMETER(pgid);
	/* always succeed */
	return 0;
}


/* from zsh/ntport/support.c */
char *getlogin(void)
{
	static char  userNameBuffer[UNLEN+1];
	static int gotUser = 0;
	DWORD size = sizeof(userNameBuffer);

	if (!gotUser) {
		if (!GetUserName(userNameBuffer, &size))
			strcpy(userNameBuffer, "bogus");
		gotUser = 1;
	}
	return userNameBuffer;
}


/* called in hashtable.c::fillnameddirtable() */
struct passwd *getpwent(void)
{
	/* see NetQueryDisplayInformation() and/or NetUserEnum to enumerate users */
	/* available on 2000+ */
	return NULL;
}


/* called in hashtable.c::fillnameddirtable() */
void endpwent()
{
	return;
}


/* called in hashtable.c::fillnameddirtable() */
void setpwent()
{
	return;
}


/* called in Modules/parameter.c::get_all_groups */
int getgroups(int gidsetsize, gid_t grouplist[])
{
	/* 0 groups */
	return 0;
}


/* called in Modules/parameter.c::get_all_groups */
struct group *getgrgid(gid_t gid)
{
	return NULL;
}


/*
 NOTES
        from .../tcsh/win32/support.c
        the Windows function has an int len, POSIX has a size_t len
        a len of 256 is always enough, see
        http://msdn.microsoft.com/en-us/library/ms738527%28v=vs.85%29.aspx
*/

int gethostname(char *buf, int len)
{
	if (!GetComputerName(buf, (DWORD *)&len))
		return 0;
	return -1;
}


/* dummy ! */
int readlink(const char *path, char *buf, size_t len)
{
	UNREFERENCED_PARAMETER(path);
	UNREFERENCED_PARAMETER(buf);
	UNREFERENCED_PARAMETER(len);
	/* always fail */
	return -1;
}


/* declared in sys/times.h */
/* from .../tcsh/win32/bogus.c */
clock_t times(struct tms *ignore)
{
	FILETIME c,e,kernel,user;

	ignore->tms_utime=0;
	ignore->tms_stime=0;
	ignore->tms_cutime=0;
	ignore->tms_cstime=0;
	if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &kernel, &user))
		return -1;

	if (kernel.dwHighDateTime){
		return GetTickCount();
	}
	//
	// Units of 10ms. I *think* this is right. -amol 6/2/97
	ignore->tms_stime = kernel.dwLowDateTime / 1000 /100;
	ignore->tms_utime = user.dwLowDateTime / 1000 /100;

	return GetTickCount();
}


#if !(defined(__MINGW32__)||defined(__MINGW64__))
/*
 DESCRIPTION
        implementation of gettimeofday
        public domain code from ttcp.c (Test TCP connection)
*/

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	SYSTEMTIME st;

	UNREFERENCED_PARAMETER(tz);
	GetLocalTime(&st);
	tv->tv_sec = st.wSecond;
	tv->tv_usec = st.wMilliseconds*1000;
	return 0;
}
#endif


/* from .../tcsh/win32/bogus.c
   uses nt_isatty() from io.c */
char *ttyname(int fd)
{
	static const char tname[] = "/dev/tty";

	if (isatty(fd))
		return (char *)tname;
	errno = ENOTTY;
	return NULL;
}


/*
 DESCRIPTION
	This function is usually declared in sys/fcntl.h. It is not implemented
	in Windows C run-times.
 NOTES
	This is the place to add FD_CLOEXEC flag handling
        Uses iodupfd from io.c
*/

int fcntl(int fd, int command, ...)
{
	va_list vl;
	int result = 0;

	va_start(vl, command);
	if (command == F_DUPFD) {
		int fdoffset = va_arg(vl, int);
		result = iodupfd(fd, fdoffset);
	}
	va_end(vl);
	return result;
}


/*
 DESCRIPTION
        Modify the Win32 environment. See the libc documentation.
*/

int setenv(const char *name, const char *value, int replace)
{
	/* maximum variable length, see
	   http://msdn.microsoft.com/en-us/library/windows/desktop/ms682653%28v=vs.85%29.aspx
        */
	char oldval[32767+1];
	DWORD rc;

	if (!name || *name == 0 || strchr(name, '=') || !value) {
		errno = EINVAL;
		return -1;
	}
	/* maximum variable length, see  */
	rc = GetEnvironmentVariable(name, oldval, sizeof(oldval));
	if (rc == 0) {
		/* not found */
		if (!SetEnvironmentVariable(name, value)) {
			errno = EINVAL;
			return -1;
		} else {
			return 0;
		}

	} else if (rc < sizeof(oldval)) {
		/* found */
		if (!replace)
			return 0;
		if (SetEnvironmentVariable(name, value))
			return 0;
	}
	/* buffer too small or set error */
	errno = EINVAL;
	return -1;
}


int unsetenv(const char *name)
{
	if (!name || *name == 0 || strchr(name, '=') ||
	    !SetEnvironmentVariable(name, NULL)) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}


#if defined(STDCRTLIB)
/* this function must be replaced with a real implementation */
int fork(void)
{
        errno = ENOMEM;
        return -1;
}


/* this function must be replaced with a real implementation */
int pipe(int fd[2])
{
        errno = ENOMEM;
        return -1;
}


/* this function must be replaced with a real implementation */
int iodupfd(int fd, int fdoffset)
{
        errno = ENOMEM;
        return -1;
}
#endif


#if defined(ZSHV5)
/* dummy ! */
int chown(const char *filename, uid_t owner, gid_t group)
{
	UNREFERENCED_PARAMETER(filename);
	UNREFERENCED_PARAMETER(owner);
	UNREFERENCED_PARAMETER(group);
	/* always succeed */
	return 0;
}


/* use CreateSymbolicLink in vista/7,
   see http://www.codeproject.com/Articles/194/Windows-2000-Junction-Points
       for XP (only for directories) */
int link(const char *oldname, const char *newname)
{
	UNREFERENCED_PARAMETER(oldname);
	UNREFERENCED_PARAMETER(newname);
	/* always fail */
	return -1;
}
#endif
