/*
 * Copyright (c) 1997-2002, 2010, Amol Deshpande and contributors
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
 * signal.h
 * signal emulation things
 * -amol
 */


#ifndef SIGNAL_H
#define SIGNAL_H

#include "uni_types.h"

#define NSIG 23     

#if !defined(_SIG_ATOMIC_T_DEFINED)
typedef int sig_atomic_t;
#define _SIG_ATOMIC_T_DEFINED
#endif

/* ctr_handler() maps CTRL_xxx_EVENT to the following: (see wincon.h) */
#define SIGINT		1 
#define SIGBREAK 	2
#define SIGHUP		3  /* CTRL_CLOSE_EVENT */
/* 3 and 4 are reserved. hence we can't use 4 and 5 */
#define	SIGTERM		6 /* ctrl_logoff */
#define SIGKILL		7 /* ctrl_shutdown */

#define SIGILL		8 
#define SIGFPE		9	
#define SIGALRM		10
#define SIGWINCH	11
#define SIGSEGV 	12	
#define SIGSTOP 	13
#define SIGPIPE 	14
#define SIGCHLD 	15
#define SIGCONT		16 
#define SIGTSTP 	18
#define SIGTTOU 	19
#define SIGTTIN 	20
#define SIGABRT 	22	

#define SIGQUIT SIGBREAK

/* signal action codes */

#define SIG_DFL (void (*)(int))0	   /* default signal action */
#define SIG_IGN (void (*)(int))1	   /* ignore signal */
#define SIG_SGE (void (*)(int))3	   /* signal gets error */
#define SIG_ACK (void (*)(int))4	   /* acknowledge */


/* signal error value (returned by signal call on error) */

#define SIG_ERR (void (*)(int))-1	   /* signal error value */


#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#if defined(signal)
# undef signal
#endif
#define signal(a,b) _nt_signal((a), (b))

/* only defined for __MINGW32__ */
#if defined(__MINGW64__) || defined(_MSC_VER)
typedef unsigned long sigset_t;
#endif
#if defined(__MINGW32__) && !defined(__MINGW64__)
typedef int sigset_t;
#endif
typedef void Sigfunc (int);

struct sigaction {
	Sigfunc *sa_handler;
	sigset_t sa_mask;
	int sa_flags;
};


#define sigemptyset(ptr) (*(ptr) = 0)
#define sigfillset(ptr)  ( *(ptr) = ~(sigset_t)0,0)

/* Function prototypes */

void (* _nt_signal(int, void (*)(int)))(int);

int sigaddset(sigset_t*, int);
int sigdelset(sigset_t*,int);
unsigned int alarm(unsigned int);

int sigismember(const sigset_t *set, int);
int sigprocmask(int, const sigset_t *, sigset_t *);
int sigaction(int, const struct sigaction *, struct sigaction *);
int sigsuspend(const sigset_t *sigmask);

int kill(pid_t pid, int sig);
  
#endif /* SIGNAL_H */
