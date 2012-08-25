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
 * signal.c: Signal emulation hacks.
 * -amol
 *
 */
/*
 NOTES
        sigsusp event in tcsh/win32 is created with CreateEvent(NULL,FALSE,FALSE,NULL)
        i.e. bManualReset is FALSE (auto-reset)
        The original zsh sigsusp was created with CreateEvent(NULL,TRUE,FALSE,NULL)
        This code (as in tcsh/win32) uses the auto reset feature and a
        WaitForSingelObject() loop in sigsuspend() using the __is_suspended counter
        Other changes:
            The thread stack is a (small) fixed value, no need for gdwStackSize
            No __h_con_hup HANDLE
            nt_cleanup_signals() removed __forked check => MUST be called ONLY for the parent
            non inherited variables are NCSTATIC
            tcsh/win32 has a bug in add_to_child_list() -??- clist_h->exitcode = exitcode;
            deliver_pending() is static
            add_to_child_list() is static
            sig_child_callback() is static
            add NULL check to sigaddset(),sigdelset(),sigmember()
            the child_list is allocated/freed with heap_alloc/heap_free
                It is a separate thread, and concurrent calls may corrupt the
                fmalloc() heap ! This is a BUG in tcsh/win32
                (patch sent to mailing list)
            sigprocmask() checks "how" and returns EINVAL if not valid
                ignores "how" if "set" is NULL
            added __delayed_handler[]. Some signal handler may end up callink fork()
                in the execption handler context, and child creation SIGSEVs
            removed the DeleteCriticalSection(&sigcritter) from nt_cleanup_signals()
                the critical section will be removed anyway at program exit
            made the handlers[] array static
            removed the inc_pending macro, gPending is already explicitly used
            nice() and kill() moved to another file
            whitespace cleanup
        OutputDebugString called from dbgprinbtf() deadlocks easily. Beware of
        changing PR_NEVER with PR_SIGNAL
*/


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <stdlib.h>

#include "ntdbg.h"
#include "forklib.h"
#include "forksect.h"
#include "signal.h"

#define SMALL_STACK     65536  /* signal threads stack */
#define SIGBAD(signo) 	     ((signo) <=0 || (signo) >=NSIG)
#define fast_sigmember(a,b)  ((*(a) & (1 << (b-1))))

typedef struct _child_list {
	DWORD dwProcessId;
	DWORD exitcode;
	struct _child_list *next;
} ChildListNode;


NCGLOBAL HANDLE __h_con_alarm, __h_con_int;

/* fork() requires that the signal block mask and actions are inherited */
static Sigfunc *handlers[NSIG];
/* mask of blocked signals, inherited across fork() */
static sigset_t gBlockMask;

/* BUT fork() requires that the pending signals are cleared */
NCSTATIC unsigned long gPending[NSIG];

NCSTATIC ChildListNode *clist_h; /* head of list */
NCSTATIC ChildListNode *clist_t; /* tail of list */

NCSTATIC CRITICAL_SECTION sigcritter;
NCSTATIC HANDLE hmainthr;
NCSTATIC HANDLE hsigsusp;
NCSTATIC int __is_suspended;
/* delayed handler calls */
NCSTATIC int __delayed_handler[NSIG];
/* why was this HANDLE shared (it was explicitly set to 0) ?
   it is created with a securityattribute of NULL, and MSDN says that in this case
   it cannot be inherited. To make it not shared use NCSTATIC
*/
static HANDLE __halarm;

static unsigned int __alarm_set;


/* must be done once before calling fork() */
void nt_init_signals(void)
{
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrl_handler, TRUE);
	InitializeCriticalSection(&sigcritter);

	clist_t = clist_h = NULL;

	/* initialize hmainthr as a duplicate of the current (main) thread handle */
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
			     &hmainthr, 0, FALSE, DUPLICATE_SAME_ACCESS)){
		dbgprintf(PR_ERROR, "!!! DuplicateHandle() error %ld in %s\n", GetLastError(), __FUNCTION__);
		ExitProcess(0xFFFF);
	}
	/* Manual reset, initially non signaled */
	hsigsusp = CreateEvent(NULL, FALSE, FALSE, NULL);
	__h_con_alarm = CreateEvent(NULL, FALSE, FALSE, NULL);
	__h_con_int = CreateEvent(NULL, FALSE, FALSE, NULL);
	/* only hsigsusp tested .. */
	if (!hsigsusp) {
		dbgprintf(PR_ERROR, "!!! CreateEvent() error %ld\n", GetLastError());
		ExitProcess(0xFFFF);
	}
}


void nt_cleanup_signals(void)
{
	/* @@@@ appverifier gives error on this ..
	DeleteCriticalSection(&sigcritter);
	   verify ! */
	CloseHandle(hmainthr);
	CloseHandle(hsigsusp);
	CloseHandle(__h_con_alarm);
	CloseHandle(__h_con_int);
	/* __halarm MAY be NULL if alarm() not called */
	if (__halarm) {
		CloseHandle(__halarm);
	}
}


/* add signo to set */

int sigaddset(sigset_t *set, int signo)
{
	if (!set || SIGBAD(signo)) {
		errno = EINVAL;
		return -1;
	}
	*set |= 1 << (signo-1);
	return 0;
}


/* remove signo from set */

int sigdelset(sigset_t *set, int signo)
{
	if (!set || SIGBAD(signo)) {
		errno = EINVAL;
		return -1;
	}
	*set &= ~(1 << (signo-1));

	return 0;
}


/* return 1 if signo is in set, 0 if not, -1 on error */

int sigismember(const sigset_t *set, int signo)
{
	if (!set || SIGBAD(signo)) {
		errno = EINVAL;
		return -1;
	}

	return ((*set & (1 <<(signo-1)) ) != 0);
}


static void deliver_pending(void)
{
	sigset_t temp;
	unsigned int sig = 1;

	temp = ~gBlockMask;
	while (temp && (sig < NSIG)) {
		if (temp & 0x01){
			if (gPending[sig]) {
				do {
					dbgprintf(PR_SIGNAL, "deliver_pending for signal %d count=%lu\n", sig, gPending[sig]);
					gPending[sig]--;
					generic_handler(sig);
				} while (gPending[sig]);
			}
		}
		temp >>= 1;
		sig++;
	}
}


int sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	if (oset)
		*oset = gBlockMask;
	if (set) {
		switch (how) {
			case SIG_BLOCK:
				gBlockMask |= *set;
				break;
			case SIG_UNBLOCK:
				gBlockMask &= (~(*set));
				break;
			case SIG_SETMASK:
				gBlockMask = *set;
				break;
			default:
				errno = EINVAL;
				return (-1);
		}
	} else  {
		/* ignore how if set is NULL */
		return 0;
	}
	if (how != SIG_BLOCK)
		deliver_pending();

	return 0;
}


int sigsuspend(const sigset_t *mask)
{
	sigset_t omask;
	int ii;

	dbgprintf(PR_NEVER, "%s(): mask is %#lx\n", __FUNCTION__, (long)(*mask));
	EnterCriticalSection(&sigcritter);
	__is_suspended++;
	LeaveCriticalSection(&sigcritter);

	sigprocmask(SIG_SETMASK, mask, &omask);
	do {
		if (WaitForSingleObject(hsigsusp, INFINITE) != WAIT_OBJECT_0)
			dbgprintf(PR_ERROR, "!!! sigsuspend() WaitForSingleObject failed\n");
	} while (__is_suspended > 0);
	/* this signal handler execution has been delayed, execute now, in main thread */
	for (ii = 1; ii < NSIG; ++ii) {
		if (!fast_sigmember(&gBlockMask, ii)) {
			if (__delayed_handler[ii]) {
				dbgprintf(PR_NEVER, "delayed signal %d handler 0x%p count %d\n", ii, handlers[ii], __delayed_handler[ii]);
				handlers[ii](ii);
				__delayed_handler[ii] = 0;
			}
		}
	}
	sigprocmask(SIG_SETMASK, &omask, 0);
	errno = EINTR;
	return -1;
}


int sigaction(int signo, const struct sigaction *act, struct sigaction *oact)
{
	/* POSIX says that signo must not be SIGKILL or SIGSTOP */
	/* ignored here ... */
	if (SIGBAD(signo)) {
		errno = EINVAL;
		return -1;
	}
	if (oact) {
		oact->sa_handler = handlers[signo];
		oact->sa_mask = 0;
		oact->sa_flags = 0;
	}
	if (act)
		handlers[signo]=act->sa_handler;
	return 0;
}


/* NOTE: signal numbers differ from the CRT signal names ! */
BOOL generic_handler(int signo)
{
	int blocked = 0;
	DWORD tid = GetCurrentThreadId();

	if (SIGBAD(signo)) {
		dbgprintf(PR_SIGNAL, "  [%ld] bad signal\n", tid);
		return FALSE;
	}
	switch (signo) {
		case SIGINT: /* CTRL-C */
			if (handlers[signo] != SIG_IGN) {
				if (fast_sigmember(&gBlockMask, signo)) {
					gPending[signo]++;
					blocked = 1;
				} else if (handlers[signo] == SIG_DFL) {
					dbgprintf(PR_ALL, "--- default SIGINT handler. Exit\n");
					ExitProcess(0xC000013AL);
				} else {
					if (__is_suspended) {
						__delayed_handler[signo]++;
						dbgprintf(PR_NEVER, "  [%ld] signal %d delay handler count=%d\n", tid, signo, __delayed_handler[signo]);
					} else {
						handlers[signo](signo);
						SetEvent(__h_con_int);
					}
				}
			}
			break;
		case SIGBREAK: /* CTRL-BREAK */
			if (handlers[signo] != SIG_IGN) {
				if (fast_sigmember(&gBlockMask, signo)) {
					gPending[signo]++;
					blocked = 1;
				} else if (handlers[signo] == SIG_DFL) {
					dbgprintf(PR_ALL, "--- default SIGBREAK handler. Exit\n");
					ExitProcess(0xC000013AL);
				} else {
					if (__is_suspended) {
						__delayed_handler[signo]++;
						dbgprintf(PR_NEVER, "  [%ld] signal %d delay handler count=%d\n", tid, signo, __delayed_handler[signo]);
					} else {
						handlers[signo](signo);
					}
				}
			}
			break;
		case SIGHUP: /* CTRL_CLOSE_EVENT */
			if (handlers[signo] != SIG_IGN) {
				if (fast_sigmember(&gBlockMask, signo)) {
					gPending[signo]++;
					blocked = 1;
				} else if (handlers[signo] == SIG_DFL) {
					dbgprintf(PR_ALL, "--- default SIGHUP handler. Exit\n");
					ExitProcess(604);
				} else {
					handlers[signo](signo);
				}
			}
			break;
		case SIGTERM: /* CTRL_LOGOFF_EVENT */
			if (handlers[signo] != SIG_IGN) {
				if (fast_sigmember(&gBlockMask, signo)) {
					gPending[signo]++;
					blocked = 1;
				} else if (handlers[signo] == SIG_DFL) {
					dbgprintf(PR_ALL, "--- default SIGTERM handler. Exit\n");
					ExitProcess(604);
				} else
					handlers[signo](signo);
			} else {
				dbgprintf(PR_ALL, "--- SIGTERM. Exit\n");
				ExitProcess(604);
			}
			break;
		case SIGKILL: /* CTRL_SHUTDOWN_EVENT */
			/* @@@@ oldfaber: is this possible ? */
                        /* see http://msdn.microsoft.com/en-us/library/ms683242%28v=vs.85%29.aspx */
			if (handlers[signo] != SIG_IGN){
				if (fast_sigmember(&gBlockMask, signo)) {
					gPending[signo]++;
					blocked = 1;
				} else if (handlers[signo] == SIG_DFL) {
					dbgprintf(PR_ALL, "--- default SIGKILL handler. Exit\n");
					ExitProcess(604);
				} else
					handlers[signo](signo);
			} else {
				dbgprintf(PR_ALL, "--- SIGKILL. Exit\n");
				ExitProcess(604);
			}
			break;
		case SIGALRM:
			if (handlers[signo] != SIG_IGN) {
				if (fast_sigmember(&gBlockMask, signo)) {
					gPending[signo]++;
					blocked = 1;
				} else if (handlers[signo] == SIG_DFL) {
					dbgprintf(PR_ALL, "--- default SIGALRM handler. Exit\n");
					ExitProcess(604);
				} else
					handlers[signo](signo);
			}
			break;
		case SIGCHLD:
			if (handlers[signo] != SIG_IGN) {
				if (fast_sigmember(&gBlockMask, signo)) {
					gPending[signo]++;
					blocked = 1;
				} else if (handlers[signo] != SIG_DFL) {
					if (__is_suspended) {
						__delayed_handler[signo]++;
						dbgprintf(PR_NEVER, "  [%ld] signal %d delay handler count=%d\n", tid, signo, __delayed_handler[signo]);
					} else {
						handlers[signo](signo);
					}
				}
			}
			/* default action is to quietly ignore this signal */
			break;
		default:
			dbgprintf(PR_ALL, "--- generic_handler(%d). Exit\n", signo);
			ExitProcess(604);
	}
	/* if the signal is NOT blocked and the main thread is suspended signal the main thread */
	if (!blocked && __is_suspended) {
		EnterCriticalSection(&sigcritter);
		__is_suspended--;
		LeaveCriticalSection(&sigcritter);
		if (!SetEvent(hsigsusp))
			dbgprintf(PR_ERROR, "!!! SetEvent(0x%p) error %ld\n", hsigsusp, GetLastError());
	}
	return TRUE;
}


/* used outside this file for clipboard handling */
BOOL ctrl_handler(DWORD event)
{
	/* this handler is called in a NEW THREAD */
	switch (event) {
	case CTRL_C_EVENT:
		return generic_handler(SIGINT);
	case CTRL_BREAK_EVENT:
		return generic_handler(SIGBREAK);
	/* clicking on the "x" button of the console window generates
	   this event */
	case CTRL_CLOSE_EVENT:
		return generic_handler(SIGHUP);
	case CTRL_LOGOFF_EVENT:
		return generic_handler(SIGTERM);
	case CTRL_SHUTDOWN_EVENT:
		return generic_handler(SIGKILL);
	}
        /* no other event value is documented, returning FALSE should be "impossible"
           returning FALSE causes the "next" handler to be invoked */
        return FALSE;
}


Sigfunc *_nt_signal(int signal, Sigfunc *handler)
{
	Sigfunc *old;

	if (SIGBAD(signal)) {
		errno = EINVAL;
		return SIG_ERR;
	}

	old = handlers[signal];
	handlers[signal] = handler;

	return old;
}


int waitpid(pid_t pid, int *statloc, int options)
{
	ChildListNode *temp;
	int retcode;

	UNREFERENCED_PARAMETER(options);
	if (pid != -1) {
		errno = EINVAL;
		return -1;
	}

	if (!clist_h) {
		dbgprintf(PR_SIGNAL,  "  [%ld] %s(%d, ..): no child\n", GetCurrentThreadId(), __FUNCTION__, (int)pid);
                errno = ECHILD;
                return 0;
	}

	EnterCriticalSection(&sigcritter);
	retcode = (int)clist_h->dwProcessId;
	if (statloc)
		/* see the Wxx macros in sys/wait.h on how to extract the process
		 status and exit code */
		*statloc = (clist_h->exitcode & 0x00FF);
	temp = clist_h;
	clist_h = clist_h->next;
	/* the list item is allocated with heap_alloc */
	LeaveCriticalSection(&sigcritter);

	heap_free(temp);
	dbgprintf(PR_SIGNAL, "  [%ld] %s(%d, ..): exitcode %d for pid %d\n", GetCurrentThreadId(), __FUNCTION__, (int)pid, *statloc, retcode);
	return (int)retcode;
}


void CALLBACK alarm_callback(unsigned int interval)
{
	int rc;

	rc = WaitForSingleObject(__halarm, interval*1000U);
	if (rc != WAIT_TIMEOUT)
		return;

	SetEvent(__h_con_alarm);
	__alarm_set = 0;
	return;

        /*
	// consoleread() now waits for above event, and calls generic_handler to
	// handle SIGALRM in the main thread. That helps me avoid
	// problems with  fork() when we are in a secondary thread.
	//
	// This means sched, periodic etc will not be signalled unless consoleread
	// is called, but that's a reasonable risk, i think.
	// -amol 4/10/97
        */
}


unsigned int alarm(unsigned int seconds)
{
	unsigned int temp;
	static unsigned int prev_val = 0;
	HANDLE ht;
	DWORD tid;


	if (!__halarm)
		__halarm = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (__alarm_set )
		SetEvent(__halarm);

	if (!seconds){
		__alarm_set = 0;
		return 0;
	}
	__alarm_set = 1;

	ht = CreateThread(NULL, SMALL_STACK, (LPTHREAD_START_ROUTINE)alarm_callback,
			  UIntToPtr(seconds), 0, &tid);
	if (ht)
		CloseHandle(ht);

	temp = prev_val;
	prev_val = seconds * 1000U;

	return temp;
}


/* already protected with sigcritter from caller */
static int add_to_child_list(DWORD dwpid, DWORD exitcode)
{
	if (clist_h == NULL) {
		clist_h = (ChildListNode *)heap_alloc(sizeof(ChildListNode));
		if (!clist_h)
			goto end;
		clist_h->dwProcessId = dwpid;
		clist_h->exitcode = exitcode;
		clist_h->next= NULL;
		clist_t = clist_h;
	} else {
		clist_t->next = (ChildListNode *)heap_alloc(sizeof(ChildListNode));
		if (!clist_t->next)
			goto end;
		clist_t = clist_t->next;
		clist_t->dwProcessId= dwpid;
		clist_t->exitcode = exitcode;
		clist_t->next = NULL;
	}
	dbgprintf(PR_NEVER, "  [%ld]  adding child pid: %ld  exitcode: %ld\n", GetCurrentThreadId(), dwpid, exitcode);
	return 1;

end:
	dbgprintf(PR_ERROR, "!!! out of memory in %s at %d\n", __FILE__, __LINE__);
        return 0;
}


static void sig_child_callback(DWORD pid, DWORD exitcode)
{
	DWORD suspcount;

	EnterCriticalSection(&sigcritter);
	if (add_to_child_list(pid, exitcode)) {
		suspcount = SuspendThread(hmainthr);
		if (suspcount == (DWORD)(-1))
			dbgprintf(PR_ERROR, "!!! SuspendThread(0x%p) failed, error %ld\n", hmainthr, GetLastError());
		else
			generic_handler(SIGCHLD);
		ResumeThread(hmainthr);
	}
	LeaveCriticalSection(&sigcritter);
}


struct thread_args {
	DWORD pid;
	HANDLE hproc;
};


/*
 body of the exception thread function
*/

void sigchild_thread(struct thread_args *args)
{
	DWORD exitcode;

	WaitForSingleObject(args->hproc, INFINITE);
	if (!GetExitCodeProcess(args->hproc, &exitcode)) {
		dbgprintf(PR_ERROR, "!!! GetExitCodeProcess(0x%p, ..) error %ld in %s\n", args->hproc, GetLastError(), __FUNCTION__);
		CloseHandle(args->hproc);
		heap_free(args);
		return;
	}
	CloseHandle(args->hproc);
	sig_child_callback(args->pid, exitcode);
	dbgprintf(PR_FORK, "- [%ld] End of sigchild_thread for pid %ld\n", GetCurrentThreadId(), args->pid);
	heap_free(args);
}


/*
 NOTES
	just before fork() returns the parent calls start_sigchild_thread()
	to create a new thread waiting for child termination.
 TODO
        check heap_alloc() return value
*/

int start_sigchild_thread(HANDLE hproc, DWORD pid)
{
	struct thread_args *args = (struct thread_args *)heap_alloc(sizeof(struct thread_args));
	DWORD tid;
	HANDLE hthr;
	args->hproc = hproc;
	args->pid = pid;

	hthr = CreateThread(NULL, SMALL_STACK, (LPTHREAD_START_ROUTINE)sigchild_thread, args, 0, &tid);
	dbgprintf(PR_FORK, "+ [%ld] Start sigchild thread for pid %ld\n", tid, pid);
	if (!hthr) {
		dbgprintf(PR_ERROR, "!!! CreateThread() error %ld\n", GetLastError());
		return (0);
	}
	CloseHandle(hthr);
	return (1);
}
