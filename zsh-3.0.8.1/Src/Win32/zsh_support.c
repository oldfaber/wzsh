/* 
 * Copyright (c) 1997-2002, 2010, Amol Deshpande and contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *     * Neither the name of the author nor the names of the contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 New file, misc help functions for zsh
*/


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <malloc.h>
#include <errno.h>

#include "ntdbg.h"
#include "forklib.h"
#include "shell_init.h"


/*
 DESCRIPTION
	Takes pwd as argument. extracts drive letter or server name
	and puts parentheses around it before returning the extracted
	string
 NOTES
	from winzsh .../winnt/support.c
	called from zle_misc.c, declared in ntport.h
*/

char *fmt_pwd_for_prompt(const char *dir)
{
	static char xtractbuf[MAX_PATH]; 
	char *ptr;

	if (!dir)
		return NULL;

	if (!(*dir) || !(*(dir+1)) )
		return NULL;

	if (*dir == '/' && *(dir+1) != '/')
		return NULL;

	xtractbuf[0] = '(';

	ptr = &xtractbuf[1];

	if (*(dir+1) == ':') { /* path with mapped or local drive*/
		xtractbuf[1] = *dir;
		xtractbuf[2] = *(dir+1);
		xtractbuf[3] = ')';
		xtractbuf[4] = 0;
	}
	else if (*dir == '/' && *(dir+1) == '/') {
		*ptr++ = *dir++;
		*ptr++ = *dir++;
		while(*dir && (*dir != '/')) {
			*ptr = *dir;

			dir++;
			ptr++;
		}
		*ptr++ = ')';
		*ptr = 0;
	}
	return &xtractbuf[0];
}


/* from winzsh .../winnt/support.c, was forward_slash_get_cwd()
   NOTE
	GetCurrentDirectory wants the lenght including the '\0', so this is safe
*/

char *getcwd(char *path, size_t maxlen)
{
	DWORD rc;

	rc = GetCurrentDirectory((DWORD)maxlen, path);
	if (rc > (DWORD)maxlen) {
		errno = ERANGE;
		return NULL;
	}
	path_to_slash(path);
	return path;
}


/*
 from winzsh .../winnt/support.c
 called from builtin.c::cd_new_pwd()
*/
void caseify_pwd(char *curwd)
{
	char *sp, *dp, p,*s;
	WIN32_FIND_DATA fdata;
	HANDLE hFind;

	if (*curwd == '/' && (!curwd[1] || curwd[1] == '/'))
		return;
	sp = curwd +3;
	dp = curwd +3;
	do {
		p= *sp;
		if (p && p != '/') {
			sp++;
			continue;
		} else {
			*sp = 0;
			hFind = FindFirstFile(curwd,&fdata);
			*sp = p;
			if (hFind != INVALID_HANDLE_VALUE) {
				FindClose(hFind);
				s = fdata.cFileName;
				while (*s) {
					*dp++ = *s++;
				}
				dp++;
				sp = dp;
			} else {
				sp++;
				dp = sp;
			}
		}
		sp++;
	} while (p != 0);
}


/*
 called from builtin.c. Does the path start with a directory separator char
 or is the second character of p a ':' ?
*/

int is_win32abspath(const char p[])
{
	return ((p[0] == '/') || (p[0] == '\\') || (p[1] && (p[1] == ':')));
}


/* called from zle_main.c::getkey(), replaced WIN32-code with this function.
   howlong is in 10ms units */
int waitkey(int fd, int howlong)
{
	DWORD rc = WaitForSingleObject((HANDLE)_nt_get_osfhandle(fd), howlong*10);

	if (rc == WAIT_OBJECT_0)
		return (0);
	if (rc == WAIT_TIMEOUT)
		return (1);
	/* WAIT_FAILED because WAIT_ABANDONED cannot be returned */
	dbgprintf(PR_ERROR, "!!! WaitForSingleObject() returned %ld for fd %d\n", GetLastError(), fd);
	return (0);
}


/* called from ../utils.c::feep() to emit a beep, avoids WINDOWS.H dependency in utils.c */
void win32beep(void)
{
	MessageBeep(MB_ICONQUESTION);
}


/* called from init.c to set the default path on win32 */
char *get_os_dir(const char *sysdir)
{
	static char temp[MAX_PATH];

	if (strcmp(sysdir, "WINDOWS") == 0) {
		GetWindowsDirectory(temp, MAX_PATH-1);
		return path_to_slash(temp);
	} else if (strcmp(sysdir, "SYSTEM") == 0) {
		GetSystemDirectory(temp, MAX_PATH-1);
		return path_to_slash(temp);
	}
        temp[0] = '\0';
       	return temp;
}


/*
 DESCRIPTION
	read the environment variable envstring (which is considered a path)
	convert it to '/', and terminate it with a '/' if not terminated.
	returns the converted variable or NULL if not found or buffer too short
 NOTES
	used in params.c to get the TMPPREFIX
*/

char *xgetenvpath(const char *envstring, char *buf, size_t bufsize)
{
	DWORD rc = GetEnvironmentVariable(envstring, buf, (DWORD)bufsize);
	if (rc && rc < bufsize) {
		path_to_slash(buf);
		if (buf[rc - 1] == '/')
			return (buf);
		if (rc >= (bufsize - 1))
			return ((char *)NULL);
		buf[rc] = '/';
		buf[rc + 1] = '\0';
		return buf;
	}
	return ((char *)NULL);
}
