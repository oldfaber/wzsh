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
 * 00_entry.c
 *
 * extracted silly_entry() from
 * $Header: /p/tcsh/cvsroot/tcsh/win32/support.c,v 1.14 2008/08/31 14:09:01 amold Exp $
 * renamed main_entry()
 * extracted heap_init() and sbrk() from
 * $Header: /p/tcsh/cvsroot/tcsh/win32/fork.c,v 1.11 2008/08/31 14:09:01 amold Exp $
 * and merged here
 *    modifications
 *      test Windows version and refuse to run if Windows is too "old"
 *      only supports VER_PLATFORM_WIN32_NT >= 5.0
 *      sbrk() returns out of memory conditions
 *      heap_init() cannot abort(), it is called before CRT startup
 *      moved HOME and PATH setup to set_HOME_and_PATH()
 *    modified for zsh pre-main initialization
 *    added nt_init() and nt_exit()
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincon.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "ntdbg.h"
#include "forkdata.h"
#include "forklib.h"
#include "forksect.h"
#if defined(SHELL)
#include "shell_init.h"
#endif
#if defined(HAVE_CLIPBOARD)
#include "clipboard.h"
#endif


#if defined(_MSC_VER)
#define SET_ESP(x)    __asm {mov esp,x}
#endif
#if defined(__GNUC__)
#define SET_ESP(x)    __asm__("movl %0,%%esp" : "=r" (x))
#endif

#ifdef __cplusplus
extern "C"
#endif
void __cdecl mainCRTStartup(void *);


extern void set_stackbase(void *);      /* in fork.c */

/*
 * NOTE
 *      This file must be the _FIRST_ object file linked in the application.
 *      The name starting with 00_ helps when is no other way to specify a
 *      link order
*/

/* marks beginning of memory to be copied to child */
unsigned long bookcommon1;
unsigned long bookbss1=0;
unsigned long bookdata1=1L;


/* true if this is a 64 bit process, if error or not found, assume FALSE */
BOOL bIsWow64Process = FALSE;
typedef BOOL (WINAPI *fnIsWOW64Process) (HANDLE, PBOOL);

/* the full Win32 path name, does not need to be copied */
NCGLOBAL char gModuleName[4096];
NCGLOBAL DWORD mainThreadId;

NCSTATIC DWORD winVersion;

/* forward declaration */
static void heap_init(void);


#if defined(_MSC_VER)
# if (_MSC_VER >= 1400)
// see http://msdn.microsoft.com/en-us/library/a9yf33zb%28VS.80%29.aspx
// maybe also set _CrtSetReportMode(_CRT_ASSERT, 0); ?
static void do_nothing(const wchar_t *expression, const wchar_t *function, const wchar_t* file,
                       unsigned int line, uintptr_t pReserved)
{
	UNREFERENCED_PARAMETER(expression);
	UNREFERENCED_PARAMETER(function);
	UNREFERENCED_PARAMETER(file);
	UNREFERENCED_PARAMETER(line);
	UNREFERENCED_PARAMETER(pReserved);
}
# else
#  error Unsupported Microsoft Compiler version
# endif
#endif




static void init_wow64(void)
{
	fnIsWOW64Process pfnIsWOW64;
	bIsWow64Process = FALSE;

	pfnIsWOW64 = (fnIsWOW64Process)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process");
	if (pfnIsWOW64)
		if (!pfnIsWOW64(GetCurrentProcess(), &bIsWow64Process))
			bIsWow64Process = FALSE;
}


/* always initialized, no need to be copied */
NCSTATIC OSVERSIONINFO osver;

/*
 * heap_init() MUST NOT be moved outside the entry point. Sometimes child
 * processes may load random DLLs not loaded by the parent and
 * use the heap address reserved for fmalloc() in the parent. This
 * causes havoc as no dynamic memory can then be inherited.
 *
 */

void main_entry(void *peb)
{
	char *base, *s;
	DWORD rc;

	/* skip VERY OLD platforms */
	if (GetVersion() & 0x80000000) {
		MessageBox(NULL, "Platform not supported", "", MB_ICONHAND);
		ExitProcess(0xFF);
	}

	rc = GetModuleFileName(NULL, gModuleName, sizeof(gModuleName));
	/* if gModuleName[] is too short kernels 5.x and 6.x return different values ! */
	if ((rc != 0) && (rc < sizeof(gModuleName))) {
		s = gModuleName;
		/* get last component of gModuleName */
		for (base = s; *s; s++) {
			if (*s == '\\' || *s == '/')
				base = s + 1;
		}
		/* just to be very very safe, if too long truncate to 0 length */
		if (strlen(base) >= FILENAME_MAX)
			*base = '\0';
	} else {
		MessageBox(NULL, "Cannot get Module filename", "", MB_ICONHAND);
		ExitProcess(0xFF);
	}

	/* check for more unsupported platforms */
	osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if (!GetVersionEx(&osver)) {
		MessageBox(NULL,"GetVersionEx failed", base, MB_ICONHAND);
		ExitProcess(0xFF);
	}
	winVersion = osver.dwMajorVersion;

	/* GetVersionEx is available from Windows 2000 onward */
	if (osver.dwPlatformId != VER_PLATFORM_WIN32_NT) {
		MessageBox(NULL, "Platform not supported", base, MB_ICONHAND);
		ExitProcess(0xFF);
	}

#if defined(W32DEBUG)
	init_dbgprintf(base);
#endif

	mainThreadId = GetCurrentThreadId();
	dbgprintf(PR_ALL, "+++ Start main thread %ld cmdline=[%s]\n", mainThreadId, GetCommandLine());

	if (winVersion < 6) { /* no wow64 hackery for version 6+ (VISTA and above) */
		init_wow64();
	}

#ifdef _M_IX86
	// look at the explanation in fork.c for why we do these steps.
	if (bIsWow64Process) {
		HANDLE h64Parent,h64Child;
		char *stk, *end;
		DWORD mb = (1<<20);

		// if we found the events, then we're the product of a fork()
		if (CreateWow64Events(GetCurrentProcessId(), &h64Parent, &h64Child, TRUE)) {

			if (!h64Parent || !h64Child)
				return;

			// tell parent we're rolling
			SetEvent(h64Child);

			if (WaitForSingleObject(h64Parent, FORK_TIMEOUT) != WAIT_OBJECT_0) {
				return;
			}

			// if __forked is 0, we shouldn't have found the events
			if (!__forked)
				return;
		}

		// now create the stack

		if (!__forked) {
			stk = (char *)VirtualAlloc(NULL, mb+65536, MEM_COMMIT, PAGE_READWRITE);
			if (!stk) {
				dbgprintf(PR_ERROR, "!!! virtual alloc in parent failed %ld\n", GetLastError());
				return;
			}
			end = stk + mb + 65536;
			end -= sizeof(char*);

			__fork_stack_begin = end;

			SET_ESP(end);

			set_stackbase(end);
			heap_init();
		}
		else { // child process
			stk = (char*)__fork_stack_begin + sizeof(char*) - mb - 65536;

			dbgprintf(PR_FORKMEM, "begin is %p\n", stk);
			end = (char *)VirtualAlloc(stk, mb+65536 , MEM_RESERVE , PAGE_READWRITE);
			if (!end) {
				dbgprintf(PR_ERROR, "!!! virtual alloc child RESERVE failed with code %ld\n", GetLastError());
				return;
			}
			stk = (char *)VirtualAlloc(end, mb+65536 , MEM_COMMIT , PAGE_READWRITE);
			if (!stk) {
				dbgprintf(PR_ERROR, "!!! virtual alloc child COMMIT failed with code %ld\n", GetLastError());
				return;
			}
			end = stk + mb + 65536;
			SET_ESP(end);
			set_stackbase(end);

			SetEvent(h64Child);

			CloseHandle(h64Parent);
			CloseHandle(h64Child);
		}
	}
#endif /* _M_IX86 */

	SetFileApisToOEM();

	if (!bIsWow64Process)
		heap_init();

#if defined(SHELL)
	set_HOME_and_PATH();
#endif

#if defined(USE_EXCHNDL) && defined(__MINGW32__) && defined(W32DEBUG)
	if (LoadLibrary("exchndl.dll"))
		dbgprintf(PR_ALL, "%s(): exchndl.dll loaded\n", __FUNCTION__);
	else
		dbgprintf(PR_ALL, "%s(): cannot load exchndl.dll\n", __FUNCTION__);
#endif
	/* this call DOES not return ... */
	mainCRTStartup(peb);
}


/*
 DESCRIPTION
        new function, for parent only, children should use _exit()
        It's atexit()'ed from nt_init()
*/

static void nt_exit(void)
{
	dbgprintf(PR_ALL, "--- Cleaning up\n");
#if defined(SHELL)
	nt_cleanup_term();
#endif
	/* SHOULD not be done for children ... */
	nt_cleanup_signals();
}


/*
 DESCRIPTION
        Should be called as the first function after main().
        Performs the necessary steps to setup the fork library. The parent
        returns	to main(), the child longjumps inside fork_init()
*/

void nt_init(char ***argvp)
{
	/* protect against missing main_entry() initialization, should only
	   occur if this program is not linked correctly */
	if (!gModuleName[0]) {
		MessageBox(NULL, "Invalid application initialization!", "Error", MB_ICONHAND);
		ExitProcess(1);
	}
#if defined(_MSC_VER)
	(void)_set_invalid_parameter_handler((_invalid_parameter_handler)do_nothing);
#endif
#if defined(SHELL)
	shell_init();
#endif
	nt_init_io();
	nt_init_signals();
	/* the child does NOT return from this call! */
	fork_init(argvp);
	/* @@@@ the child does not init the clipboard! */
#if defined(HAVE_CLIPBOARD)
	init_clipboard();
#endif
	/* nt_exit is for parent only */
	atexit(nt_exit);
}


//
// Implementation of sbrk() for the fmalloc family
//
/*
 oldfaber:
        the old VirtualAlloc(.., MEM_DECOMMIT) does NOT exist! modified to VirtualFree().
        sbrk() in our allocator is only called from morecore(), and always with a
        positive delta, i.e. it never deallocates memory
*/

void *sbrk(ptrdiff_t delta)
{
	void *retval;
	void *old_top = __heap_top;
	char *b = (char*)__heap_top;
	static unsigned long sbrkmem = 0L;

	if (delta == 0)
		return (__heap_top);
	if (delta > 0) {
		retval = VirtualAlloc(__heap_top, delta, MEM_COMMIT, PAGE_READWRITE);
		if (retval == NULL) {
			dbgprintf(PR_ERROR, "!!! %s(0x%p): Out of memory\n", __FUNCTION__, (void *)delta);
			errno = ENOMEM;
			return ((void *)(-1));
		}
	} else {
		if (!VirtualFree(((char *)__heap_top) - delta, -delta, MEM_DECOMMIT)) {
			dbgprintf(PR_ERROR, "!!! %s(): VirtualFree(0x%p, 0x%p) error %ld\n", __FUNCTION__, (char *)__heap_top - delta, (void *)(-delta), GetLastError());
			errno = ENOMEM;
			return ((void *)(-1));
		}
	}
	b += delta;
	__heap_top = (void*)b;
	sbrkmem += (unsigned long)delta;
	dbgprintf(PR_ALLOC, "%s(%ld): returning 0x%p (total %lu)\n", __FUNCTION__, (long)delta, old_top, sbrkmem);
	return (old_top);
}


// This function basically reserves some heap space.
// In the child it also commits the size committed in the parent.

#define MIN_HEAPSIZE (33554432)

static void heap_init(void)
{
	void *temp;
	size_t len;
	DWORD allocflags = MEM_RESERVE;

	if (__forked) {
		temp = VirtualAlloc(__heap_base, __heap_size, MEM_RESERVE, PAGE_READWRITE);
		dbgprintf(PR_FORKMEM, "child heap at 0x%p len %u\n", __heap_base, __heap_size);
		if (temp != (void *)__heap_base) {
			if (!temp) {
				dbgprintf(PR_ERROR, "!!! %s(): child VirtualAlloc(0x%p, %u, ..) returned 0x%p, error %ld\n", __FUNCTION__, __heap_base, __heap_size, temp, GetLastError());
				ExitProcess(3);
			} else
				__heap_base = temp;
		}
		len = (char*)__heap_top - (char*)__heap_base;
		if (len) {
			if (!VirtualAlloc((void*)__heap_base, len, MEM_COMMIT, PAGE_READWRITE)) {
				dbgprintf(PR_ERROR, "!!! %s(): child VirtualAlloc(0x%p, %u, MEM_COMMIT, ..) error %ld\n", __FUNCTION__, __heap_base, len, GetLastError());
				ExitProcess(3);
			}
		} else {
			dbgprintf(PR_WARN, "VirtualAlloc COMMIT len is 0\n");
		}
	} else {
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		/* allocate 32M dwAllocationGranularity is 64k on NT 4.0+ on all arch! */
		__heap_size = (sysinfo.dwAllocationGranularity * 512);
		/* test and reset it anyway */
		if (__heap_size < (size_t)MIN_HEAPSIZE) {
			dbgprintf(PR_WARN, "__heap size forced to 32M\n");
			__heap_size = (size_t)MIN_HEAPSIZE;
		}
		if (winVersion >= 6)
			/* under VISTA/7 or above allocate at the top of user memory,
			   allocating from the bottom frequently fails, making fork() fail.
			   Allocate from top also for 64 bit shells */
			allocflags |= MEM_TOP_DOWN;
		__heap_base = VirtualAlloc(NULL, __heap_size, allocflags, PAGE_READWRITE);
		dbgprintf(PR_FORKMEM, "heap base at 0x%p len %u\n",  __heap_base, __heap_size);
		if (__heap_base == 0) {
			dbgprintf(PR_ERROR, "!!! parent VirtualAlloc for size %u failed, code %ld. Exit\n", __heap_size, GetLastError());
			ExitProcess(3);
		}
		__heap_top = __heap_base;
	}
}
