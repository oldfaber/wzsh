/*
 *  Copyright (c) 2012, oldfaber _at_ gmail.com
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 *  SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 *  RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *  NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
 *  USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 This file is needed for shell support. It contains
 path_to_slash()
        was path_slashify() from tcsh support.c
 shell_init()
        setup the TERM and PATH environment variables
 zsetarg()
        If a shell is used as a CMD replacement, setting COMSPEC=X:/path/to/shell.exe,
        it must parse the "/c" or "/C" and the following arguments. They are setup by
        programs that expect CMD parsing, like windres.exe or FAR Manager.
        It is done replacing "/c" or "/C" with "-c" and adapting the arguments to what
        a sh-like shell expects. The arguments are parsed before calling main() from
            _setargv(void)
        for Microsoft static CRT, called in mainCRTStartup()
            __getmainargs(int *argcp, char ***argvp, char ***envp, int do_glob, void *unused_startupinfo)
        for mingw RunTime using MSVCRT.DLL
*/


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>

#include "ntdbg.h"
#include "shell_init.h"


/* only called if SHELL is defined */
char *path_to_slash(char *nt_path)
{
	char *pp = nt_path;

	while (*pp) {
		if (*pp == '\\')
			*pp = '/';
		pp++;
	}
	return nt_path;
}                                                  \


/*
 NOTES
        this function must be called BEFORE mainCRTStartup() to set the Windows
        environment that mainCRTStartup() imports into the C runtime environment
*/

void set_HOME_and_PATH(void)
{
	char ptr1[MAX_PATH+1];
	char ptr2[MAX_PATH+1];
	char ptr3[MAX_PATH+1];
	DWORD rc;
	char *path1 = NULL;

	rc = GetEnvironmentVariable("HOME", ptr1, MAX_PATH);
	if (!(rc && (rc < MAX_PATH))) {
		/* home not set */
		ZeroMemory(ptr1, sizeof(ptr1));
		ZeroMemory(ptr2, sizeof(ptr2));
		ZeroMemory(ptr3, sizeof(ptr3));
		GetEnvironmentVariable("USERPROFILE", ptr1, MAX_PATH);
		GetEnvironmentVariable("HOMEDRIVE", ptr2, MAX_PATH);
		GetEnvironmentVariable("HOMEPATH", ptr3, MAX_PATH);
		if (!ptr1[0])
			/* NOTE: wsprintfA has a buffer limit of 1024 bytes, not reached here */
			wsprintfA(ptr1, "%s%s", ptr2[0] ? ptr2 : "C:", ptr3[0] ? ptr3 : "\\");
	}
	path_to_slash(ptr1);
	SetEnvironmentVariable("HOME", ptr1);
	rc = GetEnvironmentVariable("Path", path1, 0);
	if (rc != 0) {
		path1 = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, rc);
		GetEnvironmentVariable("Path", path1, rc);
		SetEnvironmentVariable("PATH", path1);
		HeapFree(GetProcessHeap(), 0, path1);
	}
}


static int keep_single_quote;

/* Replaces
 *      2N backslashes + quote -> N backslashes + begin quoted string
 *      2N + 1 backslashes + quote -> literal
 *      N backslashes + non-quote -> literal
 *      quote + quote in a quoted string -> single quote
 *      quote + quote not in quoted string -> empty string
 *      quote -> begin quoted string
 * Note
 *      the while() loop of this function originates from setargv()
 *      in win/tclAppInit.c from Tcl 8.0.3 source distribution on SourceForge,
 *      but it has been modified for zsh argument parsing.
 *      toslash is a new parameter to convert from '/' to '\'
 */

static int process_arg(char **cmdlinep, char **parg, int toslash)
{
	int inquote, copy, slashes;
	char *p = *cmdlinep;
	char *arg = *parg;
	char *saved = arg;

	while (isspace(*p))
		p++;
	if (*p == '\0') {
		*cmdlinep = p;
		/* no more arguments to process */
		return (0);
	}

	inquote = 0;
	slashes = 0;
	while (1) {
		copy = 1;
		if (!inquote && *p == '\'') {
			dbgprintf(PR_ARGS, "%s(): keep_single_quote=%d\n", __FUNCTION__, keep_single_quote);
                        if (!keep_single_quote)
				p++;
			do {
				*arg++ = *p++;
			} while (*p && *p != '\'');
			if (*p) {
				if (keep_single_quote)
					*arg++ = *p++;
				else
					p++;
			}
			/* do not modify arguments within single quotes */
			toslash = 0;
			break;
		}
		while (*p == '\\') {
			slashes++;
			p++;
		}
		if (*p == '"') {
			if ((slashes & 1) == 0) {
				copy = 0;
				if ((inquote) && (p[1] == '"')) {
					p++;
					copy = 1;
				} else {
					inquote = !inquote;
				}
			}
			slashes >>= 1;
		}

		while (slashes) {
			*arg = '\\';
			arg++;
			slashes--;
		}

		if ((*p == '\0') || (!inquote && isspace(*p))) {
			break;
		}
		if (copy) {
			*arg = *p;
			arg++;
		}
		p++;
	}
	*arg = 0;
	if (toslash)
		path_to_slash(saved);
	*cmdlinep = p;
	*parg = arg;
	return (1);
}


/* copy until the string is finished or **p == stopchar */
static void process_fname(char **pp, char **parg, int stopchar)
{
	char *p = *pp;
	char *arg = *parg;
	char *saved = arg;

	*arg++ = *p++;
	while (*p && *p != stopchar)
		*arg++ = *p++;
	if (*p == '"')
                /* copy the closing quote */
		*arg++ = *p++;
	*arg = 0;
	path_to_slash(saved);
        *pp = p;
        *parg = arg;
}


/*
 * zsetarg
 *      Parse the Windows command line string into argc/argv.
 *      argv0 - change backslashes to slashes
 *      special handling of "/c" or "/C" arguments
 * RETURNS
 *      0 on out of memory error, else non zero
 * NOTES
 *      Allocates memory, only freed at program exit
 */

static int zsetarg(int *xargc, char ***xargv)
{
	unsigned int size, argc;
	char *p, *argvector, *arg;
	char **newargv;
	size_t cmdlinelen;
	char *lastquote;
	char *cmdline = GetCommandLine();

	/* the maximum number of args is the number of non-whitespace
	   strings found in cmdline */
	size = 2;
	for (p = cmdline; *p != '\0'; p++) {
		if (isspace(*p)) {
			size++;
			while (isspace(*p))
				p++;
			if (*p == '\0')
				break;
		}
	}
	cmdlinelen = strlen(cmdline);
	argvector = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
				      size * sizeof(char *) + cmdlinelen + 1);
	if (!argvector) {
		/* fatal error, returns with argc == 0 ! */
		dbgprintf(PR_ERROR, "!!! zsetarg(): cannot allocate memory\n");
		*xargc = 0;
		*xargv = NULL;
		return (0);
	}

	/* the argv[] array starts at argvector, the string space comes after */
	newargv = (char **)argvector;
	argvector += size * sizeof(char *);
	size--;

	p = cmdline;
	for (argc = 0; argc < size; argc++) {
		newargv[argc] = arg = argvector;
		if (!process_arg(&p, &arg, argc == 0))
			break;
		if (stricmp(newargv[argc], "-c") == 0) {
                        keep_single_quote = 0;
		} else if (stricmp(newargv[argc], "/c") == 0) {
			/* replace "/c" or "/C" with "-c" */
                        keep_single_quote = 1;
			*newargv[argc] = '-';
			*(newargv[argc] + 1) = 'c';
			*arg++ = 0;
			argc++;
			/* no more argument array space */
			if (argc >= size)
				break;
			/* special processing of the remaining of the line */
			newargv[argc] = arg;
			while (isspace(*p))
				p++;
			if (*p == '"') {
				/* /C is followed by a quoted argument list */
				lastquote = &cmdline[cmdlinelen - 1];
				while (isspace(*lastquote))
					lastquote--;
				p++;  /* skip the quote */
				process_fname(&p, &arg, *p == '"' ? '"' : ' ');
				/* copy the remaining part of the command line */
				dbgprintf(PR_ARGS, "%s(): now p=[%s]\n", __FUNCTION__, p);
				while (*p && p <= lastquote)
					*arg++ = *p++;
				arg--;
			} else {
				/* the program name is not quoted, copy and convert the program path after -c */
				process_arg(&p, &arg, 1);
				/* copy the remaining part of the command line */
				while (*p)
					*arg++ = *p++;
			}
			*arg = 0;
			argc++;
			break;
		}
		argvector = arg + 1;
	}
	newargv[argc] = NULL;
	*xargc = argc;
	*xargv = newargv;
	return (1);
}

#if defined(W32DEBUG)
#define PARGV(x) pargv(x)
static void pargv(char **argvp)
{
	char **a = argvp;
	int ii = 0;

	/* Print __argv stopping on NULL pointer,
	   not on argument count as zsh tests this condition */
	while (*a) {
		dbgprintf(PR_ARGS, "%s(): argv[%d]=[%s]\n", __FUNCTION__, ii++, *a);
		a++;
	}
}
#else
#define PARGV(x)
#endif


/* __argc and __argv are declared in stdlib.h */

#if defined(_MSC_VER)
/* this is the hook for Microsoft static run time library */
void _setargv(void)
{
	zsetarg(&__argc, &__argv);
	PARGV(__argv);
}
#endif

#if defined(__MINGW32__)
int _CRT_glob = 0;

/* this is the hook for MSVCRT.dll
        do_glob (not used) is 1 if globbing
        startupinfo (not used) is the STARTUPINFO for the process
*/
int __getmainargs(int *argcp, char ***argvp, char ***__UNUSED_PARAM(envp),
		  int __UNUSED_PARAM(do_glob), void *__UNUSED_PARAM(startupinfo))
{
	if (zsetarg(argcp, argvp)) {
		__argc = *argcp;
		__argv = *argvp;
		PARGV(__argv);
		/* 0 on success, <0 on error */
		return (0);
	}
	return (-1);
}
#endif


/*
 DESCRIPTION
        Remove ".exe" from the name because emulate() tests for the name
        without .exe. zsh main() does a reverse search of '/', so retain the
        starting "X:/" or "//" for UNC paths
*/

static void setup_program_name(char *name)
{
	int plen;

	/* argv[0] may be NULL ... at least in POSIX */
	if (name == NULL || *name == '\0')
		return;
	plen = (int)strlen(name);
	/* remove the ".EXE" part from the full path */
	if ((plen >= 5) &&
	    toupper(*(name+plen-1)) == 'E' &&
	    toupper(*(name+plen-2)) == 'X' &&
	    toupper(*(name+plen-3)) == 'E' &&
		    *(name+plen-4) == '.') {
		*(name+plen-4) = '\0';
	}
}

/*
 DESCRIPTION
        zsh-specific program initialization.
*/

void shell_init(void)
{
	char **ep;
	char *Path_ptr, *PATH_ptr;
	int hasPATH = 0;

	dbgprintf(PR_VERBOSE, "%s(): argc=%d argv[0]=[%s]\n", __FUNCTION__, __argc, __argv[0]);
	setup_program_name(__argv[0]);
	if (GetEnvironmentVariable("TERM", NULL, 0) == 0) {
		SetEnvironmentVariable("TERM", "vt100");
		putenv("TERM=vt100");
	}
	/* if the program uses the CRTDLL the PATH setting in main_entry is
	   not available in environ, set it now */
	for (ep = environ; *ep != NULL; ep++)
		if (strncmp(*ep, "PATH=", 5) == 0)
			hasPATH = 1;
	if (!hasPATH) {
		/* this is set in the standard Windows environment */
		Path_ptr = getenv("Path");
		if (Path_ptr) {
			/* SetEnvironmentVariable() does not like the pointer
			   returned from getenv(), use the local copy.
			   Allocate adding space for strlen("PATH=")+1 */
			PATH_ptr = (char *)_alloca(strlen(Path_ptr) + 5 + 1);
			if (PATH_ptr) {
				/* leave room for "PATH=" */
				strcpy(PATH_ptr + 5, Path_ptr);
				if (!SetEnvironmentVariable("PATH", PATH_ptr + 5))
					dbgprintf(PR_ERROR, "!!! %s(): SetEnvironmentVariable(\"%s\") error %ld\n", __FUNCTION__, PATH_ptr + 5, GetLastError());
				/* this is needed when zsh does a getenv("PATH") */
				memcpy(PATH_ptr, "PATH=", 5);
				if (putenv(PATH_ptr) < 0)
					dbgprintf(PR_ERROR, "!!! %s(): putenv(\"%s\") failed\n", __FUNCTION__, PATH_ptr);
			}
		}
	}
	/* only useful for shell-like programs */
	nt_init_term();
}
