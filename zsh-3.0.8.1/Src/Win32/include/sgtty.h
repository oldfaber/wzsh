/*
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *
 *	@(#)ioctl_compat.h	8.4 (Berkeley) 1/21/94
 */
/* oldfaber: simplified, used only for compiling */

struct tchars
{
	char t_intrc;		/* interrupt */
	char t_quitc;		/* quit */
	char t_startc;		/* start output */
	char t_stopc;		/* stop output */
	char t_eofc;		/* end-of-file */
	char t_brkc;		/* input delimiter (like nl) */
};

struct ltchars
{
	char t_suspc;		/* stop process signal */
	char t_dsuspc;		/* delayed stop process signal */
	char t_rprntc;		/* reprint line */
	char t_flushc;		/* flush output (toggles) */
	char t_werasc;		/* word erase */
	char t_lnextc;		/* literal next character */
};

/*
 * Structure for TIOCGETP and TIOCSETP ioctls.
 */
#ifndef _SGTTYB_
#define	_SGTTYB_
struct sgttyb
{
	char sg_ispeed;		/* input speed */
	char sg_ospeed;		/* output speed */
	char sg_erase;		/* erase character */
	char sg_kill;		/* kill character */
	short int sg_flags;	/* mode flags */
};
#endif
