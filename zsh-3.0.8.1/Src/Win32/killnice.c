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
 * $Header: /p/tcsh/cvsroot/tcsh/win32/signal.c,v 1.11 2006/08/25 17:49:57 christos Exp $
 *
 * oldfaber:
 *      extracted kill() and nice() from signal.c and moved here
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include "ntdbg.h"
#include "signal.h"

/* from
 * $Header: /p/tcsh/cvsroot/tcsh/win32/ps.c,v 1.9 2006/03/14 01:22:58 mitr Exp $
 */

static HWND ghwndtokillbywm_close;

static BOOL CALLBACK enum_wincb2(HWND hwnd, LPARAM pidtokill)
{
	DWORD pid = 0;

	if (!GetWindowThreadProcessId(hwnd, &pid))
		return TRUE;
	if (pid == (DWORD)pidtokill) {
		ghwndtokillbywm_close = hwnd;
		PostMessage( hwnd, WM_CLOSE, 0, 0);
		return TRUE;
	}
	return TRUE;
}

static int kill_by_wm_close(int pid)
{
	EnumWindows(enum_wincb2, (LPARAM)pid);
	if (!ghwndtokillbywm_close)
		return -1;
	ghwndtokillbywm_close = NULL;
	return 0;
}

int kill(pid_t pid, int sig)
{
	HANDLE hproc;
	int ret = 0;

	if (pid < 0) {
		if (pid == -1)
			return -1;
		pid = -pid; //no groups that we can actually do anything with.
	}

	switch(sig) {
	case 0:
	case SIGKILL:
		/* using the Windows SDK 6.1 or newer PROCESS_ALL_ACCESS has changed size
		   and Windows XP fails the call. Use only what is needed !
		   see http://msdn.microsoft.com/en-us/library/windows/desktop/ms684880%28v=vs.85%29.aspx
		*/
		hproc = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_TERMINATE, FALSE, pid);
		if (hproc == NULL) {
			errno = ESRCH;
			return -1;
		}
		if (sig && !TerminateProcess(hproc, 0xC000013AL)) {
			errno = EPERM;
			ret = -1;
		}
		CloseHandle(hproc);
		break;
	case SIGINT:
		if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid)) {
			dbgprintf(PR_ERROR, "!!! GenerateConsoleCtrlEvent(%d) for pid %d error %ld\n", sig, (int)pid, GetLastError());
			ret = -1;
		}
		break;
	case SIGBREAK:
		if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)) {
			dbgprintf(PR_ERROR, "!!! GenerateConsoleCtrlEvent(%d) for pid %d error %ld\n", sig, (int)pid, GetLastError());
			ret = -1;
		}
		break;
	case SIGHUP:
		if (kill_by_wm_close(pid) < 0) {
			errno = ESRCH;
			ret = -1;
		}
		/* follow thru */
	default:
		/* kill(pid, SIGCONT) will return 0 */
                /* should return EINVAL if the signal is not valid */
		break;
	}
	return ret;
}


/*
 oldfaber:
        modified the return value in case of success from -1 to 0
*/

//
// nice(niceness)
//
// where niceness is an integer in the range -6 to +7
//
// A usual foreground process starts at level 9 in the chart below
//
// the range -6 to +7 takes it from Base priority 15 down to 2. 
//
// Note that level 1 or > 15 are not allowed.
//
// Priority Level 11 (niceness -2) or greater affects system performance, 
//	so use with care.
//
// niceness defaults to  +4, which is lowest for background normal class.
// As in unix, +ve niceness indicates lower priorities.

/***************************************************************************
Niceness    Base    Priority class/thread priority

            1    Idle, normal, or high class,    THREAD_PRIORITY_IDLE

+7          2    Idle class,                     THREAD_PRIORITY_LOWEST
+6          3    Idle class,                     THREAD_PRIORITY_BELOW_NORMAL
+5          4    Idle class,                     THREAD_PRIORITY_NORMAL
+4          5    Background normal class,        THREAD_PRIORITY_LOWEST
                    Idle class,                  THREAD_PRIORITY_ABOVE_NORMAL
+3          6    Background normal class,        THREAD_PRIORITY_BELOW_NORMAL
                    Idle class,                  THREAD_PRIORITY_HIGHEST
+2          7    Foreground normal class,        THREAD_PRIORITY_LOWEST
                    Background normal class,     THREAD_PRIORITY_NORMAL
+1          8    Foreground normal class,        THREAD_PRIORITY_BELOW_NORMAL
                    Background normal class,     THREAD_PRIORITY_ABOVE_NORMAL
 0          9    Foreground normal class,        THREAD_PRIORITY_NORMAL
                    Background normal class,     THREAD_PRIORITY_HIGHEST
-1          10   Foreground normal class,        THREAD_PRIORITY_ABOVE_NORMAL
-2          11    High class,                    THREAD_PRIORITY_LOWEST
                    Foreground normal class,     THREAD_PRIORITY_HIGHEST
-3          12    High class,                    THREAD_PRIORITY_BELOW_NORMAL
-4          13    High class,                    THREAD_PRIORITY_NORMAL
-5          14    High class,                    THREAD_PRIORITY_ABOVE_NORMAL
-6          15    Idle, normal, or high class,   THREAD_PRIORITY_TIME_CRITICAL 
                  High class,                    THREAD_PRIORITY_HIGHEST


    16    Real-time class, THREAD_PRIORITY_IDLE
    22    Real-time class, THREAD_PRIORITY_LOWEST
    23    Real-time class, THREAD_PRIORITY_BELOW_NORMAL
    24    Real-time class, THREAD_PRIORITY_NORMAL
    25    Real-time class, THREAD_PRIORITY_ABOVE_NORMAL
    26    Real-time class, THREAD_PRIORITY_HIGHEST
    31    Real-time class, THREAD_PRIORITY_TIME_CRITICAL
****************************************************************************/

int nice(int niceness)
{
	DWORD pclass = IDLE_PRIORITY_CLASS;
	int priority = THREAD_PRIORITY_NORMAL;

	if (niceness < -6 || niceness > 7) {
		errno = EPERM;
		return -1;
	}
	switch (niceness) {
	case 7:
		pclass = IDLE_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_LOWEST;
		break;
	case 6:
		pclass = IDLE_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_BELOW_NORMAL;
		break;
	case 5:
		pclass = IDLE_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_NORMAL;
		break;
	case 4:
		pclass = IDLE_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_ABOVE_NORMAL;
		break;
	case 3:
		pclass = IDLE_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_HIGHEST;
		break;
	case 2:
		pclass = NORMAL_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_LOWEST;
		break;
	case 1:
		pclass = NORMAL_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_BELOW_NORMAL;
		break;
	case (-1):
		pclass = NORMAL_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_ABOVE_NORMAL;
		break;
	case (-2):
		pclass = NORMAL_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_HIGHEST;
		break;
	case (-3):
		pclass = HIGH_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_BELOW_NORMAL;
		break;
	case (-4):
		pclass = HIGH_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_NORMAL;
		break;
	case (-5):
		pclass = HIGH_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_ABOVE_NORMAL;
		break;
	case (-6):
		pclass = HIGH_PRIORITY_CLASS;
		priority = THREAD_PRIORITY_HIGHEST;
		break;
	default:
		break;
	}

	if (!SetPriorityClass(GetCurrentProcess(),pclass)){
		errno = EPERM;
		return -1;
	}
	if (!SetThreadPriority(GetCurrentThread(),priority)){
		errno = EPERM;
		return -1;
	}
	return 0;
}
