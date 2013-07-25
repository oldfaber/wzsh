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
 * from
 * $Header: /p/tcsh/cvsroot/tcsh/win32/fork.c,v 1.11 2008/08/31 14:09:01 amold Exp $
 *
 * The fork() here is based on the ideas used by cygwin
 * -amol
 *
 */

/*
 * oldfaber:
 *      removed unused stack_probe
 *      moved heap_init() and sbrk() to 00_entry.c.
 *      merged, modified and renamed fork_copy_user_mem() to static copymem()
 *      fixed non inheritable STD handles
 *      added child termination on error
 *      fork_init() is now void, and saves child argvp
 *      fork() uses GetCommandLine() for lpCommandLine
 *      heavvy whitespace/comment modification
 *      added NCSTATIC/NCGLOBAL for non-copied variables.
 *      removed _M_ALPHA code. ALPHA is stuck at a pre-release Windows 2000.
 *      removed the exception registration hack
 *      Only use the faster and magical NtCurrentTeb() "function"
 * TODO:
 *      @@@@ fix fork() error path mess !
 * NOTES
 *      Use this (stealed from the cygwin mailing list) to test the fork speed:
 *      #!/bin/sh
 *      while (true); do date; done | uniq -c
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <setjmp.h>

#include "forklib.h"
#include "forksect.h"
#include "forkdata.h"
#include "ntdbg.h"

#if defined(_MSC_VER)
#pragma warning(push,3) // forget about W4 here
#endif


/*
 * Apparently , visual c++ on the alpha does not place the
 * fork data contiguously. To work around that, Mark created
 * this structure (see forkdata.h)
 * -amol
 */
/* explicitly copied */
NCGLOBAL ForkData gForkData;
NCSTATIC HANDLE hin, hout, herr;

/* should not be copied, they keep the child argv ! */
NCSTATIC char ***childargvp;
NCSTATIC char **childargv;

#define GETSTACKBASE()          (((NT_TIB*)NtCurrentTeb())->StackBase)


/* ONLY used if bIsWow64Process */
void set_stackbase(void *ptr)
{
	GETSTACKBASE() = ptr;
}


/*
 DESCRIPTION
        If oldh is inheritable return it, else duplicate oldh and return the
        duplicated handle.
 NOTES
        A shell script lauched from the windows association with redirected
        I/O has non-inheritable redirected handles, and a forked sub-shell fails
        to read or write these. Tested with a script.sh
                #!/bin/sh
                # this fails
                (echo "$0 on stdout")
                exit 0
        lauched as
        script.sh > eee
        from the command line (.sh are associated with zsh!)
*/

static HANDLE newhandle(HANDLE oldh, HANDLE *newh)
{
	DWORD flags;
	HANDLE self;

	*newh = INVALID_HANDLE_VALUE;
	if (!GetHandleInformation(oldh, &flags)) {
		dbgprintf(PR_ERROR, "!!! GetHandleInformation(0x%p) error %ld\n", oldh, GetLastError());
		return *newh;
	}
	/* if inheritable just use it */
	if (flags & HANDLE_FLAG_INHERIT)
		return oldh;
	self = GetCurrentProcess();
	/* else duplicate oldh, the new handle is inheritable */
	if (!DuplicateHandle(self, oldh, self, newh, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
		dbgprintf(PR_ERROR, "!!! DuplicateHandle(0x%p, ..) error %ld in %s\n", oldh, GetLastError(), __FUNCTION__);
		return *newh;
	}
	dbgprintf(PR_IO, "%s() duplicated handle 0x%p => 0x%p\n", __FUNCTION__, oldh, *newh);
	return *newh;
}


/*
 NOTES
        also called from nt_execve
*/

void close_si_handles(void)
{
	if (hin != INVALID_HANDLE_VALUE)
		CloseHandle(hin);
	if (hout != INVALID_HANDLE_VALUE)
		CloseHandle(hout);
	if (herr != INVALID_HANDLE_VALUE)
		CloseHandle(herr);
}


/*
 DESCRIPTION
        setup the STARTUPINFO record, setting the hStd* handles from the
        STD_* handles of the process if inheritable or duplicating them
*/

void init_startupinfo(STARTUPINFO *si)
{
	memset(si, 0, sizeof(STARTUPINFO));
	si->cb = sizeof(si);
	/* the STD_x_HANDLE are not always inheritable, setup before forking. */
	si->dwFlags = STARTF_USESTDHANDLES;
	si->hStdInput = newhandle(GetStdHandle(STD_INPUT_HANDLE), &hin);
	si->hStdOutput = newhandle(GetStdHandle(STD_OUTPUT_HANDLE), &hout);
	si->hStdError = newhandle(GetStdHandle(STD_ERROR_HANDLE), &herr);
}


/*
 * This function copies memory from p1 to p2 for process hproc.
 * The skeleton was tcsh/win32/globals.c::fork_copy_user_mem().
 * The pointer check and swap is for the common block, its limit may be reversed
 * (swpping pointers makes this function unusable for the stack copy).
 * Returns zero if copy failed, non zero otherwise
 */
static int copymem(HANDLE hproc, void *p1, void *p2)
{
	SIZE_T size, bytes;
	void *low = p1, *high = p2;

	if (p1 > p2) {
		low = p2;
		high = p1;
	}
	dbgprintf(PR_FORKMEM, "low is 0x%p high is 0x%p\n", low, high);
	size = (char*)high - (char*)low;
	/* don't copy if the pointers are equal or in consecutive memory locations */
	if (size <= sizeof(void*)) {
		dbgprintf(PR_WARN, "!!! copy size is %lu for 0x%p\n", size, p1);
		/* nothing to copy, result is OK */
		return (1);
	}
	dbgprintf(PR_FORKMEM, "copying data from 0x%p len %lu\n", low, size);
	if (!WriteProcessMemory(hproc, low, low, size, &bytes)) {
		dbgprintf(PR_ERROR, "!!! error %ld writing 0x%p size %lu memory\n", GetLastError(), low, (unsigned long)size);
		return (0);
	}
	if (size != bytes) {
		dbgprintf(PR_ERROR, "!!! error writing 0x%p total size %lu, wrote %lu\n", low, size, bytes);
		return (0);
	}
	return (1);
}


/*
 * This must be called by the application as the first thing it does.
 * -amol 2/6/97
 *
 * Well, maybe not the FIRST..
 * -amol 11/10/97
 */

void fork_init(char ***cargvp)
{
	dbgprintf(PR_FORK, "%s(): environment 0x%p\n", __FUNCTION__, environ);
	if (__forked) {
		/* save child &argv, argv */
		childargvp = cargvp;
		childargv = *cargvp;
		/* Whee ! */
		longjmp(__fork_context, 1);
	}
}


int fork(void)
{
	int rc;
	size_t stacksize;
	HANDLE hProc, hThread, hArray[2];
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	SECURITY_ATTRIBUTES sa;
	DWORD dwCreationflags, object_active;
	SIZE_T written;
	int priority;
	HANDLE h64Parent, h64Child;
	void *fork_stack_end;

	__fork_stack_begin = GETSTACKBASE();
	dbgprintf(PR_FORK, "%s() stack base=0x%p\n", __FUNCTION__, __fork_stack_begin);
	/* last local stack location */
	__fork_stack_end = &fork_stack_end;

        /* only the main thread may call fork(), non-main threads have a different stack ! */
	if (GetCurrentThreadId() != mainThreadId) {
		dbgprintf(PR_ERROR, "!!! error: fork() called outside main thread %ld\n", GetCurrentThreadId());
		errno = EAGAIN;
		if (IsDebuggerPresent())
			DebugBreak();
		return (-1);
	}

	h64Parent = h64Child = NULL;
	//
	// Create two inheritable events
	//
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = 0;
	sa.bInheritHandle = TRUE;
	if (!__hforkchild) {
		__hforkchild = CreateEvent(&sa, TRUE, FALSE, NULL);
	}
	if (!__hforkparent)
		__hforkparent = CreateEvent(&sa, TRUE, FALSE, NULL);
	if (__hforkchild == NULL || __hforkparent == NULL) {
		/* does not catch the "Already exists" case */
		dbgprintf(PR_ERROR, "!!! error %ld creating events\n", GetLastError());
		/* no really meaningful errno .. */
		errno = EAGAIN;
		return (-1);
	}

	if (setjmp(__fork_context)) { // child
		if (!SetEvent(__hforkchild)) {
			dbgprintf(PR_ERROR, "!!! error %ld resuming parent. Exit\n", GetLastError());
			/* child prematurely exits */
			ExitProcess(0xFFFF);
		}
		if (WaitForSingleObject(__hforkparent, FORK_TIMEOUT) != WAIT_OBJECT_0) {
			dbgprintf(PR_ERROR, "!!! error waiting for parent. Exit\n");
			ExitProcess(0xFFFF);
		}
		CloseHandle(__hforkchild);
		CloseHandle(__hforkparent);
		__hforkchild = __hforkparent = 0;
		/* restore child argv */
		if (childargvp && childargv)
			*childargvp = childargv;
		restore_fds();
		dbgprintf(PR_FORK, "returning from fork()\n");
#if defined(DEBUGWAIT)
		/* wait forever for a debugger */
		for (;;) {
			if (IsDebuggerPresent()) {
				DebugBreak();
				break;
			}
			Sleep(1000);
		}
#endif
		return 0;
	}

	copy_fds();

	init_startupinfo(&si);

	dwCreationflags = GetPriorityClass(GetCurrentProcess());
	priority = GetThreadPriority(GetCurrentThread());
	if (!CreateProcess(gModuleName,
			   GetCommandLine(),
			   NULL,        /* security attributes */
			   NULL,        /* thread secirity attributes */
			   TRUE,        /* inherit handles */
			   CREATE_SUSPENDED | dwCreationflags,
			   NULL,        /* new env block */
			   NULL,        /* current dir */
			   &si,
			   &pi)) {
		dbgprintf(PR_ERROR, "fork(): !!! error %ld creating process\n", GetLastError());
		/* @@@@ should close_copied_fds() ? */
		close_si_handles();
		return -1;
	}
	dbgprintf(PR_FORK, "fork() created %ld with cmdline [%s]\n", pi.dwProcessId, GetCommandLine());

	ResetEvent(__hforkchild);
	ResetEvent(__hforkparent);

	hProc = pi.hProcess;
	hThread = pi.hThread;

	__forked = 1;
	/*
	 * Usage of events in the wow64 case:
	 *
	 * h64Parent : initially non-signalled
	 * h64Child  : initially non-signalled
	 *
	 *    1. Create the events, resume the child thread.
	 *    2. Child opens h64Parent to see if it is a child process in wow64
	 *    3. Child opens and sets h64Child to tell parent it's running. (This
	 *       step is needed because we can't copy to a process created in the
	 *       suspended state on wow64.)
	 *    4. Copy gForkData and then set h64Parent. This tells the child
	 *       that the parameters in the structure are trustworthy.
	 *    5. Wait for h64Child so that we know the child has created the stack
	 *       in dynamic memory.
	 *
	 *   The rest of the fork hack should now proceed as in x86
	 *
	 */
	if (bIsWow64Process) {

		// allocate the heap for the child. this can be done even when
		// the child is suspended.
		// avoids inexplicable allocation failures in the child.
		if (VirtualAllocEx(hProc, __heap_base, __heap_size, MEM_RESERVE, PAGE_READWRITE) == NULL) {
			dbgprintf(PR_ERROR, "!!! virtual allocex RESERVE failed %ld\n", GetLastError());
			goto error;
		}
		if (VirtualAllocEx(hProc, __heap_base, __heap_size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
			dbgprintf(PR_ERROR, "!!! virtual allocex COMMIT failed %ld\n", GetLastError());
			goto error;
		}

		// Do NOT expect existing events
		if (!CreateWow64Events(pi.dwProcessId, &h64Parent, &h64Child, FALSE)) {
			goto error;
		}
		ResumeThread(hThread);

		hArray[0] = h64Child;
		hArray[1] = hProc;
		if (WaitForMultipleObjects(LENGTH_OF(hArray), hArray, FALSE, FORK_TIMEOUT) != WAIT_OBJECT_0) {
			rc = GetLastError();
			goto error;
		}

	}
	//
	// Copy all the shared data
	//
	dbgprintf(PR_FORKMEM, "copying shared data from 0x%p len %u\n", &gForkData, sizeof(ForkData));
	if (!WriteProcessMemory(hProc, &gForkData, &gForkData, sizeof(ForkData), &written)) {
		dbgprintf(PR_ERROR, "!!! error %ld writing shared data\n", GetLastError());
		goto error;
	}
	if (written != sizeof(ForkData)) {
		dbgprintf(PR_ERROR, "!!! error: shared data len was %u, written %lu\n", sizeof(ForkData), written);
		goto error;
	}

	if (!bIsWow64Process) {
		if (ResumeThread(hThread) == 0xffffffff) {
			dbgprintf(PR_ERROR, "!!! error %ld resuming child\n", GetLastError());
			goto error;
		}
		/* speed optimization: run the child. When the parent reaches the
		   WaitForMultipleObjects() below there will be no waiting.
		   Sleep(0) or Sleep(1) does not make any visible difference */
		Sleep(0);
	}
	// in the wow64 case, the child will be waiting  on h64parent again.
	// set it, and then wait for h64child. This will mean the child has
	// a stack set up at the right location.
	else {
		SetEvent(h64Parent);
		hArray[0] = h64Child;
		hArray[1] = hProc;
		if (WaitForMultipleObjects(LENGTH_OF(hArray), hArray, FALSE, FORK_TIMEOUT) != WAIT_OBJECT_0) {
			rc = GetLastError();
			goto error;
		}
		CloseHandle(h64Parent);
		CloseHandle(h64Child);
		h64Parent = h64Child = NULL;
	}

	/* Wait for the child to start and init itself */
	hArray[0] = __hforkchild;
	hArray[1] = hProc;
	/* waiting for hProc should catch children that don't reach SetEvent(__hforkchild)
	   because of early termination */
	object_active = WaitForMultipleObjects(LENGTH_OF(hArray), hArray, FALSE, FORK_TIMEOUT);
	if (object_active != WAIT_OBJECT_0) {
		dbgprintf(PR_ERROR, "!!! premature exit of child %ld\n", pi.dwProcessId);
		goto error;
	}

	/* Stop the child again and copy the stack and heap */
	SuspendThread(hThread);

	if (!SetThreadPriority(hThread, priority) ) {
		dbgprintf(PR_ERROR, "!!! error %ld setting child thread priority\n", GetLastError());
		/* non fatal error */
	}

	/* copy the stack */
	stacksize = (char *)__fork_stack_begin - (char *)__fork_stack_end;
	dbgprintf(PR_FORKMEM, "copying stack (0x%p .. 0x%p) size %u\n", __fork_stack_end, __fork_stack_begin, stacksize);
	if (!WriteProcessMemory(hProc, __fork_stack_end, __fork_stack_end, stacksize, &written)) {
		dbgprintf(PR_ERROR, "!!! error %ld copying the stack (0x%p .. 0x%p) size %u\n", GetLastError(), __fork_stack_end, __fork_stack_begin, stacksize);
		goto error;
	}

	/* copy heap, common, data and bss */
	dbgprintf(PR_FORKMEM, "heap: ");
	if (!copymem(hProc, __heap_base, __heap_top))
		goto error;
	dbgprintf(PR_FORKMEM, "common: ");
	if (!copymem(hProc, &bookcommon1, &bookcommon2))
		goto error;
	dbgprintf(PR_FORKMEM, "data: ");
	if (!copymem(hProc, &bookdata1, &bookdata2))
		goto error;
	/* if no data section the .bss must be copied */
	dbgprintf(PR_FORKMEM, "bss: ");
	if (!copymem(hProc, &bookbss1, &bookbss2))
		goto error;

        /* start the waiting thread before releasing the child */
	if (!start_sigchild_thread(hProc, pi.dwProcessId)) {
		dbgprintf(PR_ERROR, "!!! error starting sigchild thread\n");
		/* release the child anyway */
	}

	/* Release the child */
	SetEvent(__hforkparent);
	rc = ResumeThread(hThread);
	if (rc == -1)
		/* child suspend count is normally 1 */
		dbgprintf(PR_ERROR, "!!! child suspend count is %d (Error ? %ld)\n", rc, GetLastError());

	__forked = 0;
	dbgprintf(PR_FORK, "%s(): returning %ld\n", __FUNCTION__, pi.dwProcessId);

	close_copied_fds();
	close_si_handles();
	CloseHandle(hThread);
	/* return process id to parent */
	return pi.dwProcessId;

error:
	/* @@@@ fds copied in copy_fds() ARE closed ??? */
	__forked = 0;
	close_si_handles();
	dbgprintf(PR_ERROR, "!!! fork() error exit\n");
	TerminateProcess(hProc, 254);
	CloseHandle(hProc);
	CloseHandle(hThread);
	if (h64Parent) {
		SetEvent(h64Parent); // don't let child block forever
		CloseHandle(h64Parent);
	}
	if (h64Child)
		CloseHandle(h64Child);
	return -1;
}


/*
 * Semantics of CreateWow64Events
 *
 * Try to open the events even if bOpenExisting is FALSE. This will help
 * us detect name duplication.
 *
 *       1. If OpenEvent succeeds,and bOpenExisting is FALSE,  fail.
 *
 *       2. If OpenEvent failed,and bOpenExisting is TRUE fail
 *
 *       3. else create the events anew
 *
 */
#define TCSH_WOW64_PARENT_EVENT_NAME "tcsh-wow64-parent-event"
#define TCSH_WOW64_CHILD_EVENT_NAME  "tcsh-wow64-child-event"
BOOL CreateWow64Events(DWORD pid, HANDLE *hParent, HANDLE *hChild, BOOL bOpenExisting)
{

	SECURITY_ATTRIBUTES sa;
	char parentname[256],childname[256];

	*hParent = *hChild = NULL;

	// make darn sure they're not inherited
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor =0;
	sa.bInheritHandle = FALSE;
	//
#if defined(_MSC_VER)
#pragma warning(disable:4995)
#endif
	// This event tells the child to hold for gForkData to be copied
	wsprintfA(parentname, "Local\\%d-%s",pid, TCSH_WOW64_PARENT_EVENT_NAME);

	wsprintfA(childname, "Local\\%d-%s",pid, TCSH_WOW64_CHILD_EVENT_NAME );
#if defined(_MSC_VER)
#pragma warning(default:4995)
#endif
	*hParent = OpenEvent(EVENT_ALL_ACCESS,FALSE, parentname);

	if(*hParent) {
		if (bOpenExisting == FALSE) { // didn't expect to be a child process
			CloseHandle(*hParent);
			*hParent = NULL;
			return FALSE;
		}

		*hChild = OpenEvent(EVENT_ALL_ACCESS,FALSE, childname);
		if (!*hChild) {
			CloseHandle(*hParent);
			*hParent = NULL;
			return FALSE;
		}

		return TRUE;
	}
	else { //event does not exist
		if (bOpenExisting == TRUE)
			return FALSE;
	}

	*hParent = CreateEvent(&sa,FALSE,FALSE,parentname);
	if (!*hParent)
		return FALSE;

	*hChild = CreateEvent(&sa,FALSE,FALSE,childname);
	if (!*hChild){
		CloseHandle(*hParent);
		*hParent = NULL;
		return FALSE;
	}
	return TRUE;
}
