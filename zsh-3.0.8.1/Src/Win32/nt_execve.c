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
 * from win32/support.c
 * various routines to do exec, etc.
 *
 */

/*
 * oldfaber:
 *      path_slashify() renamed path_to_slash() and moved to shell_init.c
 *      forward_slash_get_cwd() moved to another file, simplified and renamed getcwd{)
 *      getmachine() removed, tcsh specific
 *      quoteProtect() static
 *      init_wow64() moved to fork.c
 *      silly_entry() renamed main_entry(), modified and moved to 00_entry.c
 *      copy_quote_and_fix_slashes() and concat_args_and_quote() are static
 *      removed the "false UNC" check, never reached
 *      added PATHEXT search
 *      added #! processing, function process_shebang()
 *      removed make_err_str()
 *      merged is_gui()
 *      merged try_shell_ex() from ntfunc.c. Heavily modified.
 *      removed because not needed for Windows 2000 and later
 *      nt_chdir(), fix_path_for_child()/restore_path()
 *      modified copy_quote_and_fix_slashes(), source is const
 *      modified quoteProtect(), src is const
*/


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ntdbg.h"
#include "forklib.h"
#include "shell_init.h"


static size_t copy_quote_and_fix_slashes(const char *source, char *target);
static void concat_args_and_quote(const char *const *args, char **cstr, size_t *clen, char **cend, unsigned int *cmdsize);


/* new function */
static void path_to_backslash(char *ux_path)
{
	while (*ux_path) {
		if (*ux_path == '/')
			*ux_path = '\\';
		ux_path++;
	}
}


static void exec_exit(int exitcode)
{
	/* 
	  this may be called from the parent OR the child
	  ExitProcess does not:
	  call nt_cleanup_term() (leaves hConOut open)
	  call cleanup_signals() for parent
	  but this process is exiting anyway
	*/
	/* give the child a chance to run */
	Sleep(0);
	ExitProcess(exitcode);
}


/* from
 * $Header: /p/tcsh/cvsroot/tcsh/win32/globals.c,v 1.11 2008/09/10 20:34:21 amold Exp $
 *
 * How To Determine Whether an Application is Console or GUI     [win32sdk]
 * ID: Q90493     CREATED: 15-OCT-1992   MODIFIED: 16-DEC-1996
 * extracted is_gui() and moved here
 *
 * tcsh uses async ReadFile. why ?
 */

#if !defined(IMAGE_SIZEOF_NT_OPTIONAL_HEADER)
#define IMAGE_SIZEOF_NT_OPTIONAL32_HEADER    224
#define IMAGE_SIZEOF_NT_OPTIONAL64_HEADER    240

#ifdef _WIN64
#define IMAGE_SIZEOF_NT_OPTIONAL_HEADER     IMAGE_SIZEOF_NT_OPTIONAL64_HEADER
#else
#define IMAGE_SIZEOF_NT_OPTIONAL_HEADER     IMAGE_SIZEOF_NT_OPTIONAL32_HEADER
#endif
#endif

static int is_gui(const char *exename)
{
	HANDLE hImage;

	DWORD  bytes;
	DWORD  SectionOffset;
	DWORD  CoffHeaderOffset;
	DWORD  MoreDosHeader[16];
	ULONG  ntSignature;
	IMAGE_DOS_HEADER      image_dos_header;
	IMAGE_FILE_HEADER     image_file_header;
	IMAGE_OPTIONAL_HEADER image_optional_header;

	hImage = CreateFile(exename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == hImage) {
		return 0;
	}

	/*
	 *  Read the MS-DOS image header.
	 */
	if (!ReadFile(hImage, &image_dos_header, sizeof(IMAGE_DOS_HEADER), &bytes, NULL)) {
		CloseHandle(hImage);
		return 0;
	}

	if (IMAGE_DOS_SIGNATURE != image_dos_header.e_magic) {
		CloseHandle(hImage);
		return 0;
	}

	/* Read more MS-DOS header */
	if (!ReadFile(hImage, MoreDosHeader, sizeof(MoreDosHeader), &bytes, NULL)) {
		CloseHandle(hImage);
		return 0;
	}

	/* Get actual COFF header. */
	CoffHeaderOffset = SetFilePointer(hImage, image_dos_header.e_lfanew, NULL, FILE_BEGIN);

	if (CoffHeaderOffset == (DWORD)(-1)) {
		CloseHandle(hImage);
		return 0;
	}

	CoffHeaderOffset += sizeof(ULONG);

	if (!ReadFile (hImage, &ntSignature, sizeof(ULONG), &bytes, NULL)) {
		CloseHandle(hImage);
		return 0;
	}

	if (IMAGE_NT_SIGNATURE != ntSignature) {
		CloseHandle(hImage);
		return 0;
	}

	SectionOffset = CoffHeaderOffset + IMAGE_SIZEOF_FILE_HEADER + IMAGE_SIZEOF_NT_OPTIONAL_HEADER;

	if (!ReadFile(hImage, &image_file_header, IMAGE_SIZEOF_FILE_HEADER, &bytes, NULL)) {
		CloseHandle(hImage);
		return 0;
	}

	/* Read optional header. */
	if (!ReadFile(hImage, &image_optional_header, IMAGE_SIZEOF_NT_OPTIONAL_HEADER, &bytes, NULL)) {
		CloseHandle(hImage);
		return 0;
	}

	CloseHandle(hImage);

	if (image_optional_header.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI)
		return 1;
	return 0;
}


/*
 DESCRIPTION
        execute with ShellExecuteEx. Returns 0 for failure, non 0 for success.
        argv0 is the "script" name
        argv is the list of arguments following the script name
        modifications:
            removed exitsuccess argument
            uses passed-in cmdstr for concat_args_and_quote()
 NOTES
        See
           http://msdn.microsoft.com/en-us/library/windows/desktop/bb759784%28v=vs.85%29.aspx
           for quotation rules (in the Remarks section)
        @@@@ When is SEE_MASK_CONNECTDRV used ?
        Does ShellExecuteEx() pass inheritable handles to processes ?
*/

static int try_shell_ex(char *argv0, const char *const *argv, unsigned long shellexflags, char **cmdstr, unsigned int *cmdsize)
{
	char *cmdend;
	size_t cmdlen;
	SHELLEXECUTEINFO shinfo;
	BOOL nocmd = 0;

	path_to_backslash(argv0);

	/* @@@@ is this code really needed ? when ? */
	if ((!*argv) && (argv0[0] == '\\') && (argv0[1] == '\\')) {
		shellexflags |= SEE_MASK_CONNECTNETDRV;
		nocmd = 1;
		goto noargs;
	}

	cmdend = *cmdstr;
	cmdlen = 0;
	concat_args_and_quote(argv, cmdstr, &cmdlen, &cmdend, cmdsize);
	*cmdend = '\0';

noargs:
	dbgprintf(PR_EXEC, "ShellExecute(%s, ..) with cmdstr [%s]\n", argv0, *cmdstr);
	memset(&shinfo, 0, sizeof(shinfo));
	shinfo.cbSize = sizeof(shinfo);
	shinfo.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_FLAG_DDEWAIT | shellexflags;
	shinfo.hwnd = NULL;
	shinfo.lpVerb = NULL;
	shinfo.lpFile = argv0;
	shinfo.lpParameters = nocmd ? NULL : *cmdstr;
	shinfo.lpDirectory = 0;
	shinfo.nShow = SW_SHOWDEFAULT;

	if (ShellExecuteEx(&shinfo)) {
		DWORD retval = 255;
		dbgprintf(PR_EXEC, "ShellExecute() created process handle 0x%p\n", shinfo.hProcess);
		/* may happen if "executing" a file associated to a running program, i.e.
		   "execute" a .html file with an already opened browser window */
		if (shinfo.hProcess != (HANDLE)0) {
			if (shellexflags & SEE_MASK_NOCLOSEPROCESS) {
				if ((intptr_t)(shinfo.hInstApp) > 32) {
					if (WaitForSingleObject(shinfo.hProcess, INFINITE) == WAIT_OBJECT_0) {
						/* try to get the return value */
						GetExitCodeProcess(shinfo.hProcess, &retval);
					} else {
						dbgprintf(PR_ERROR, "!!! ShellExecute() [%s] WaitForSingleObject() error %ld\n", argv0, GetLastError());
					}
				} else {
					dbgprintf(PR_ERROR, "!!! ShellExecute() [%s] error %p\n", argv0, shinfo.hInstApp);
				}
			}
			/* try to close, it may fail but .. what else could we do */
			CloseHandle(shinfo.hProcess);
		}
		dbgprintf(PR_ALL, "--- %s(): ShellExecute() OK, exiting with code %ld\n", __FUNCTION__, retval);
		exec_exit((int)retval);
	} else {
		dbgprintf(PR_EXEC, "ShellExecute() failed\n");
	}
       	return (0);
}


/*
 DESCRIPTION
        handle shebang in shell scripts. Opens argv0, the script name, and
        extracts the program to be executed and the arguments.
 RETURNS
        -1 if read error
        0 if script but no shebang
        non zero if the script has a shebang, and fills the cmdstr for
        execution via CreateProcess()
 NOTES
        Zsh limits the !# string (including '\0') to POUNDBANGLIMIT = 64 chars
        The !# execution is implemented on many POSIX system in the kernel,
        not in the shell, thus nt_execve() behaves like the kernel execve(2),
        and directly executes scripts with #!. Zsh exec.c::execute() code has a
        fallback if execve() returns errno = ENOEXEC, and executes /path/to/program
        with the correct args. To do this zsh
        - allocates a larger argv
        - opens and reads the script.
        This function prepares the interpreter for execution by CreateProcess().
        CreateProcess() lpApplication name
        - does not search the PATH
        - must have an extension
        - if it is not a full path the current path is used to complete
          the specification, i.e.
          \bin\xx.exe is treated as
          %CD%\bin\xx.exe
          so unix paths starting with / MUST have the current drive letter added
          Use GetFullPathName() ?
 TESTS
        @@@@
        How is the \r\n line termination handled ?
        mksh has a more refined analysis of the shebang line (see scriptexec)
             skips the BOM if present
             ....
 BUGS
        * DOES NOT work
                #! "D:/path/with spaces/to/p ro gram.exe"  opt1 opt2
        * ignores /bin/tcsh, /bin/csh 
          what to do ?
        * what if /bin/ksh is the win32 port of mksh ?
          do we want the redirection ?
 SEE ALSO
        http://www.in-ulm.de/~mascheck/various/shebang/
*/

static int process_shebang(char *argv0, const char *const *cmdstr, size_t *cmdlen, char **cmdend, unsigned int *cmdsize)
{
	HANDLE hfile;
	char buf[512];
	unsigned int nn, t0;
	char *ptr, *ptr2;
	char *newargv[4];
	char pbuffer[MAX_PATH];
	char *filepart;
	static const char *shellnames[] = {"/bin/sh", "/bin/zsh", "/bin/ksh"};
	static const char usrbinenv[] = "/usr/bin/env";
	static const char usrbinpython[] = "/usr/bin/python";
	static const char usrbinperl[] = "/usr/bin/perl";
	static const char usrbintcl[] = "/usr/bin/tcl";

	hfile = CreateFile(argv0, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile == INVALID_HANDLE_VALUE)
		return (-1);
	if (!ReadFile(hfile, buf, (DWORD)sizeof(buf), (DWORD *)&nn, NULL)) {
		CloseHandle(hfile);
		return (-1);
	}
	CloseHandle(hfile);
	if (!((nn >= 3) && (buf[0] == '#') && (buf[1] == '!')))
		return (0);

	/* this code is more or less what zexecve() does */
	for (t0 = 0; t0 != nn; t0++)
		if ((buf[t0] == '\n') || (buf[t0] == '\r'))
			break;
	while (isspace(buf[t0]))
		buf[t0--] = '\0';
	buf[sizeof(buf)-1] = '\0';
	/* @@@@ fails for "/b/s p/pgm" !!! */
	for (ptr = buf + 2; *ptr && *ptr == ' '; ptr++)
		;
	for (ptr2 = ptr; *ptr && *ptr != ' '; ptr++)
		;
	/* ptr2 is the program name, ptr points to the args */
	dbgprintf(PR_VERBOSE, "%s(): found \"!#\" program=[%s] arg ptr=[%s]\n", __FUNCTION__, ptr2, *ptr ? ptr + 1 : "NULL");
	/* append to the cmdstr
	   should have argv0 ('/' or '\' format ?) */
	if (*ptr) {
		*ptr = '\0';
		newargv[0] = ptr2;
		newargv[1] = ptr + 1;
		newargv[2] = argv0;
	} else {
		newargv[0] = ptr2;
		newargv[1] = argv0;
		newargv[2] = NULL;
	}
	newargv[3] = NULL;
	concat_args_and_quote((const char *const *)newargv, (char **)cmdstr, cmdlen, cmdend, cmdsize);
	*cmdend = '\0';
	/* if ptr2 is a "well known" shell name set argv0 to our module name */
	for (nn = 0; nn < LENGTH_OF(shellnames); nn++) {
		if (strcmp(ptr2, shellnames[nn]) == 0) {
			strcpy(argv0, gModuleName);
			return (1);
		}
	}
	if (strcmp(ptr2, usrbinenv) == 0) {
		SearchPath(NULL, "env.exe", NULL, sizeof(pbuffer), pbuffer, &filepart);
		strcpy(argv0, pbuffer);
	} else if (strcmp(ptr2, usrbinpython) == 0) {
		SearchPath(NULL, "python.exe", NULL, sizeof(pbuffer), pbuffer, &filepart);
		strcpy(argv0, pbuffer);
	} else if (strcmp(ptr2, usrbinperl) == 0) {
		SearchPath(NULL, "perl.exe", NULL, sizeof(pbuffer), pbuffer, &filepart);
		strcpy(argv0, pbuffer);
	} else if (strcmp(ptr2, usrbintcl) == 0) {
		SearchPath(NULL, "tcl.exe", NULL, sizeof(pbuffer), pbuffer, &filepart);
		strcpy(argv0, pbuffer);
	} else {
		char *exeptr;
		path_to_backslash(ptr2);
		strcpy(argv0, ptr2);
		/* if argv0 does not end with ".exe" add ".exe" */
		exeptr = StrStrI(argv0, ".exe");
		if ((exeptr == NULL) || (exeptr != strrchr(argv0, '.'))) {
			strcat(argv0, ".exe");
			dbgprintf(PR_VERBOSE, "%s(): argv0 modified to [%s]\n", __FUNCTION__, argv0);
		}
	}
	return (1);
}


static int is_shell_script(const char *ext)
{
	/* @@@@ use zsh.exe/sh.exe association to test for shell script extension
	   see http://stackoverflow.com/questions/3536634/getting-file-associations-using-windows-api
	   to use AssocQueryStringW() */
	return (stricmp(ext, ".SH") == 0);
}


/*
 NOTES:
        This function does not replicate cmd behaviour and uses PATHEXT only if
        prog does not have an extension.
        The memory allocated with heap_alloc() is not freed, because the calling
        process is about to exit

        It prog is not executable and has an "unkown" extension it MUST have a
        valid shebang, otherwise nr_execve() returns EPERM.
        Returning ENOEXEC to zexecve() would cause to execute "/bin/sh prog"
        and allow to execute any text file with any extension.
        Cygwin bash shell and Interix ksh allows this: what should we do ?
*/

int nt_execve(const char *prog, const char *const *args, const char *const *envir)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	enum {none, directex, shellex} execmode;
	DWORD exitcode;
	DWORD dwCreationflags;
	int priority;
	char *argv0;
	char *cmdstr, *cmdend;
	unsigned int cmdsize;
	size_t prognamelen, cmdlen;
	int hasext;
	char extension[_MAX_FNAME];
	const char *begin, *end, *extptr;
	static char exts[MAX_PATH];

	UNREFERENCED_PARAMETER(envir);

	/* get default PATHEXT or use empty exts */
	if (!*exts) {
		DWORD rc;
		/* not initialized */
		rc = GetEnvironmentVariable("PATHEXT", exts, sizeof(exts));
		if ((rc == 0) || (rc >= sizeof(exts)))
			/* if error or PATHEXT too big will retry at the next call */
			*exts = 0;
	}

	/* if prog has an extension initialize begin end to skip PATHEXT search */
	prognamelen = strlen(prog);
	extptr = prog + prognamelen - 1;
	hasext = 0;
	while (extptr > prog && !ISPATHSEP(*extptr)) {
		if (*extptr == '.' && *(extptr - 1) != ':' && !ISPATHSEP(*(extptr - 1))) {
			hasext++;
			break;
		}
		extptr--;
	}
	if (hasext) {
		begin = ".";
		end = "";
		strcpy(extension, extptr);
	} else {
		begin = exts;
		end = exts;
		*extension = '\0';
	}

	argv0 = (char *)heap_alloc(MAX_PATH);
	/* (prognamelen + 1) does not really matter, argv0 is '\0' filled */
	memcpy(argv0, prog, prognamelen + 1);

	errno = 0;
	execmode = none;
	/* NOTE: loops over PATHEXT if no extension found */
	while (*begin) {
		size_t extlen;
		if (GetBinaryType(argv0, &exitcode)) {
			/* exists and is executable
			   NOTE: an "xxx.exe" without a correct PE header (i.e. a text file)
			   has type "DOS binary", but execution will generate a WOW error */
			execmode = directex;
			break;
		}
		if (GetLastError() == ERROR_BAD_EXE_FORMAT) {
			/* exists but is not "executable" */
			execmode = shellex;
			break;
		}
		if (hasext)
			break;
		/* get next PATHEXT extension */
		while (*begin && (*begin != '.'))
			begin++;
		while (*end && (*end != ';'))
			end++;
		if (!*begin)
			break;
		extlen = end - begin;
		if (extlen < sizeof(extension)) {
			memcpy(extension, begin, extlen);
			extension[extlen] = '\0';
			/* prognamelen ignores the last '\r' if present */
			memcpy(argv0, prog, prognamelen);
			/* update argv0 adding the extension to prog */
			memcpy(argv0 + prognamelen, extension, extlen + 1);
		}
		begin = end;
		/* skip sequences of ';' */
		while (*end && *end == ';')
			end++;
	};

	cmdstr = (char *)heap_alloc(MAX_PATH << 2);
	cmdsize = MAX_PATH << 2;
	cmdlen = 0;
	cmdend = cmdstr;

	dbgprintf(PR_VERBOSE, "%s(): execute [%s] extension=[%s] mode=%d hasext=%d\n", __FUNCTION__, argv0, extension, execmode, hasext);
	/* skip over program name */
	args++;

	/* the file (after PATHEXT search) exists, but it's not "executable" */
	if (execmode == shellex) {
		/* if prog had no extension or has the extension associated to shell scripts */
		if ((hasext == 0 && *extension == '\0') || is_shell_script(extension)) {
			int res = process_shebang(argv0, (const char *const *)&cmdstr, &cmdlen, &cmdend, &cmdsize);
			if (res < 0) {
				execmode = none;
			} else if (res == 0) {
				char *newargv[2];
				cmdlen = copy_quote_and_fix_slashes(gModuleName, cmdstr);
				cmdend = cmdstr + cmdlen;
				newargv[0] = path_to_slash(argv0);
				newargv[1] = NULL;
				concat_args_and_quote((const char *const *)newargv, &cmdstr, &cmdlen, &cmdend, &cmdsize);
				*cmdend = 0;
				argv0 = gModuleName;
				execmode = directex;
			} else {
				cmdend = cmdstr + cmdlen;
				execmode = directex;
			}
		} else {
			unsigned long shflags = 0L;
			/* if the file extension is in pathext, use the same console
			   and wait for child. StrStrI() is from shlwapi */
			if (StrStrI(exts, extension))
				shflags = SEE_MASK_NO_CONSOLE | SEE_MASK_NOCLOSEPROCESS;
			if (try_shell_ex(argv0, args, shflags, &cmdstr, &cmdsize))
				return (0);
			/* ShellExecute failed, the file has an unknown extension, but it
			    may be a shell script with a shebang */
			if (process_shebang(argv0, (const char *const *)&cmdstr, &cmdlen, &cmdend, &cmdsize) > 0) {
				cmdend = cmdstr + cmdlen;
				execmode = directex;
			} else {
				/* the file extension is NOT known and the file has NO shebang:
				   returns EPERM, see NOTES */
				errno = EPERM;
				return (-1);
			}
		}
	} else if (execmode == directex) {
		cmdlen = copy_quote_and_fix_slashes(prog, cmdstr);
		cmdend = cmdstr + cmdlen;
	}
	if (execmode == none) {
		/* error: prog not found even after trying PATHEXT extensions */
		errno = ENOENT;
		return (-1);
	}

	concat_args_and_quote(args, &cmdstr, &cmdlen, &cmdend, &cmdsize);
	if (*cmdstr == ' ') {
		/* if we left a ' ' for the quote and there is no quote */
		cmdstr++;
		cmdlen--;
	}
	*cmdend = 0;

	init_startupinfo(&si);
	dwCreationflags = GetPriorityClass(GetCurrentProcess());
	priority = GetThreadPriority(GetCurrentThread());

#if defined(W32DEBUG)
	/* DebugView output is very difficult to read with overlong lines */
	if (cmdlen < 128)
		dbgprintf(PR_EXEC, "%s(): CreateProcess(%s, ..) cmdstr=[%s]\n", __FUNCTION__, argv0, cmdstr);
	else {
		char shortbuf[128+4];
		memcpy(shortbuf, cmdstr, 128);
		memcpy(shortbuf + 128, "...", 4);
		dbgprintf(PR_EXEC, "nt_execve(): CreateProcess(%s, ..) cmdstr=[%s]\n", argv0, shortbuf);
	}
#endif
	if (!CreateProcess(argv0, cmdstr, NULL, NULL,
			   TRUE, // need this for redirecting std handles
			   dwCreationflags | CREATE_SUSPENDED,
			   NULL, NULL, &si, &pi)) {
                exitcode = GetLastError();
		if (exitcode == ERROR_BAD_EXE_FORMAT) {
			dbgprintf(PR_ERROR, "!!! CreateProcess(%s, ..) error BAD_EXE_FORMAT in %s\n", argv0, __FUNCTION__);
			errno  = ENOEXEC;
		} else if (exitcode == ERROR_INVALID_PARAMETER) {
			dbgprintf(PR_ERROR, "!!! CreateProcess(%s, ..) error INVALID_PARAMETER in %s, cmdstr len=%u\n", argv0, __FUNCTION__, strlen(cmdstr));
			/* exceeded command line */
			/* return NOT found, ENAMETOOLONG is correct but not understood by
			   the shell that will retry with another path ... */
			errno = ENOENT;
		} else {
			dbgprintf(PR_ERROR, "!!! CreateProcess(%s, ..) error %ld in %s\n", argv0, exitcode, __FUNCTION__);
			errno = ENOENT;
		}
		goto fail_return;
	} else {
		exitcode = 0;
		if (!SetThreadPriority(pi.hThread, priority))
			dbgprintf(PR_ERROR, "!!! SetThreadPriority(0x%p) failed, error %ld\n", pi.hThread, GetLastError());
		ResumeThread(pi.hThread);
		if (!is_gui(argv0)) {
			if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0)
				dbgprintf(PR_ERROR, "!!! error %ld waiting for process %ld\n", GetLastError(), pi.dwProcessId);
			if (!GetExitCodeProcess(pi.hProcess, &exitcode))
				dbgprintf(PR_ERROR, "!!! GetExitCodeProcess(0x%p, ..) error %ld in %s\n", pi.hProcess, GetLastError(), __FUNCTION__);
		}
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		close_si_handles();
		/* @@@@ should wait for the clipboard ?
		if (is_dev_clipboard_active) {
			CloseHandle((HANDLE)_get_osfhandle(0));
			CloseHandle((HANDLE)_get_osfhandle(1));
			CloseHandle((HANDLE)_get_osfhandle(2));
			...
			WaitForSingleObject(ghdevclipthread,60*1000);
			}
		*/
		dbgprintf(PR_ALL, "--- %s(): Exec'd process %ld terminated with exitcode %ld\n", __FUNCTION__, pi.dwProcessId, exitcode);
		exec_exit((int)exitcode);
	}

fail_return:
        heap_free(cmdstr);
	close_si_handles();
	exec_exit(-1);
	return (-1);
}


/* This function from  Mark Tucker (mtucker@fiji.sidefx.com) */
static void quoteProtect(char *dest, const char *src)
{
	const char *prev, *curr;

	for (curr = src; *curr; curr++) {
		// Protect " from MS-DOS expansion
		if (*curr == '"') {
			// Now, protect each preceeding backslash
			for (prev = curr-1; prev >= src && *prev == '\\'; prev--)
				*dest++ = '\\';

			*dest++ = '\\';
		}
		*dest++ = *curr;
	}
	*dest = 0;
}


/*
 * Copy source into target, quote if it has space, and convert '/' to '\'.
 *
 * hasdot is set to 1 if source ends in a file extension
 * return value is the  length of the string copied.
 *
 * oldfaber:
 * used for the first (lpApplicationName) and for second (lpCommandLine)
 * parameters of CreateProcess(). If source has embedded spaces it must
 * be quoted when used inside the lpCommandLine, see
 * http://blogs.msdn.com/oldnewthing/archive/2007/05/15/2636224.aspx
 * This function 0-terminates the target string, and does non leave
 * any leading char to be stripped after the call
 * Return value is the (unsigned) target length
 *
 * Added stripping of '\r' at the end of source, maybe due to a '\r\n' script
 * hasdot is not used
 */

static size_t copy_quote_and_fix_slashes(const char *source, char *target)
{
	char *save = target;
	size_t len = 0;
	int hasspace = 0;

	while (*source) {
		if (*source == '/') {
			*target++ = '\\';
		} else if (!hasspace && *source == ' ') {
			hasspace = 1;
			/* make room and add starting quote */
			memmove(save+1, save, len);
			*save = '"';
			target++;
			len++;
			*target++ = *source;
		} else
			*target++ = *source;
		source++;
		len++;
	}

	/* remove last (stray) '\r', found if a shell script has "\r\n" line endings */
	if (*(target-1) == '\r') {
		target--;
		len--;
		*target = '\0';
	}

	if (hasspace) {
		*target++ = '"';
		len++;
	}
	*target = '\0';

	return len;
}


/*
 * args is the const array to be quoted and concatenated
 * *cstr is the resulting string
 * *clen is current cstr size
 * *cend points to the end of the constructed string
 * *cmdsize is the cstr buffer size
 *
 * PROBLEMS:
 *      - heap_realloc() is not tested for failure
 *      - when re-allocating the passed-in *cstr pointer is lost, generating a leak
 *
 * This routine is a replacement for the old, horrible strcat() loop
 * that was used to turn the argv[] array into a string for CreateProcess().
 * It's about a zillion times faster.
 * -amol 2/4/99
 */
static void concat_args_and_quote(const char *const *args, char **cstr, size_t *clen,
                                  char **cend, unsigned int *cmdsize) {

	unsigned int argcount, arglen;
	size_t cmdlen;
	const char *tempptr;
	char *cmdend, *cmdstr;
	short quotespace;
	short quotequote;
	short n_quotequote;

	/*
		quotespace hack needed since execv() would have separated args, but
		createproces doesnt
		-amol 9/14/96
	*/
	cmdend= *cend;
	cmdstr = *cstr;
	cmdlen = *clen;

	argcount = 0;
	while (*args && (cmdlen < 65500) ) {

		argcount++;
		arglen = 0;

		/* first, count the current argument and check if we need to quote. */
		quotespace = quotequote = n_quotequote = 0;
		tempptr = *args;

		if(!*tempptr) {
			/* check for empty argument, will be replaced by "" */
			quotespace = 1;
		}
		else {
			/* count spaces, tabs and quotes. */
			while(*tempptr) {
				if (*tempptr == ' ' || *tempptr == '\t')
					quotespace = 1;
				else if (*tempptr == '"') {
					quotequote = 1;
					n_quotequote++;
				} else if (*tempptr == '\\') {
					n_quotequote++;
				}
				tempptr++;
				arglen++;
			}
		}

		/* Next, realloc target string if necessary */
		while (cmdlen + 2 + arglen + 2*quotespace + quotequote * n_quotequote > *cmdsize) {
			tempptr = cmdstr;
			dbgprintf(PR_WARN, "Heap realloc before %p\n", cmdstr);
			cmdstr = (char *)heap_realloc(cmdstr, *cmdsize<<1);
			/* @@@@ does NOT test for failure ... */
			if (tempptr != cmdstr) {
				cmdend = cmdstr + (cmdend-tempptr);
			}
			dbgprintf(PR_WARN, "Heap realloc after %p\n", cmdstr);
			*cmdsize <<=1;
		}

		/* add space before next argument */
		*cmdend++ = ' ';
		cmdlen++;

		if (quotespace) {
			/* we need to quote, so output a quote. */
			*cmdend++ = '"';
			cmdlen++;
		}

		if (n_quotequote > 0){
			/* quote quotes and copy into the destination */
			*cmdend=0;
			quoteProtect(cmdend,*args);
			while(*cmdend) {
				cmdend++;
				cmdlen++;
			}
		} else {
			/* directly copy the argument into the destination */
			tempptr = *args;
			while(*tempptr) {
				*cmdend++ = *tempptr++;
				cmdlen++;
			}
		}

		if (quotespace) {
			*cmdend++ = '"';
			cmdlen ++;
		}

		args++;
	}
	*clen = cmdlen;
	*cend = cmdend;
	*cstr = cmdstr;

}
