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
 *
 * oldfaber:
 *      this is a new file.
 *      dbgprintf() was dprintf() from winzsh .../winnt/globals.c
 *      the name change is due to a clash with glibc dprintf()
 *
 * NOTE
 *      all the functions in this file may be called _BEFORE_ the C runtime
 *      is setup, therefore they _CANNOT_ use the C runtime library
 */


#if defined(W32DEBUG)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>

#include "ntdbg.h"


static DWORD debugflags;
/* try harder to avoid OutputDebugString lockup */
static volatile int locked;


void dbgprintf(int flags, const char *format, ...)
{
	va_list vl;
	int len;
	/* wvsprintf has a 1024 byte limit and no floating point support */
	char putbuf[1025];

	if (!(flags & (debugflags | PR_ERROR)))
		return;
	putbuf[1024] = 0;
	va_start(vl, format);
	/* ASCII only */
	len = wvsprintfA(putbuf, format, vl);
	va_end(vl);
	if (locked) {
		Sleep(0);
		return;
	}
	locked = 1;
	if (len >= 1024)
		OutputDebugStringA("!!! wvsprintfA() OVEFLOW\n");
	else
		OutputDebugStringA(putbuf);
	locked = 0;
}


/* convert an ASCII hex digit to integer */
static int char2int(const char *s)
{
	int digit = *s;

	if (*s >= 'A' && *s <= 'F')
		digit = (*s - '\x7');
	else if (*s >= 'a' && *s <= 'f')
		digit = (*s - '\x27');
	return (digit - '0');
}


/*
 DESCRIPTION
        initialize the debugflags variable. basename is cut to 5 char if
        longer, uppercased and "_DFLAGS" is appended to form the environment
        variable name.
        5 chars is arbitrary, but help remind the program name
*/

void init_dbgprintf(const char *basename)
{
	/* could also use heap_alloc (and heap_realloc if too small) */
	char temp[MAX_PATH + 5];
	char envvar[MAX_PATH];
	int ii;
	DWORD rc;

	locked = 0;
	/* if envvar is NULL enable nothing */
	if (!basename) {
		debugflags = 0;
		return;
	}
	ii = 0;
	/* copy up to 5 chars, stop at the first dot to avoid extensions */
	while (*basename && (*basename != '.') && (ii < 5)) {
		/* toupper should work even if the CRT is not initialized */
		envvar[ii++] = toupper(*basename++);
	}
	memcpy(envvar + ii, "_DFLAGS", 8);
	/* the CRT is not initialized yet, old-stype conversion */
	memset(temp, 0, sizeof(temp));
	rc = GetEnvironmentVariable(envvar, temp, sizeof(temp));
	if (rc && (rc < sizeof(temp))) {
		DWORD f = 0;
		char *s = temp;
		int dv;
		int factor;
		/* hex numbers are prefixed by "0x" */
		if (*s == '0' && *(s+1) == 'x') {
			s += 2;
				factor = 16;
		} else
			factor = 10;
		/* convert a digit at a time, add to f */
		while (*s) {
			dv = char2int(s);
			if ((dv >= 0) && (dv < factor)) {
					f *= factor;
					f += dv;
					++s;
			} else
				break;
		}
		debugflags |= f;
	}
}


/* this function sets the debugflags. It's useful if the environment variable
   is not present, or to dynamically change the debugflags */
void set_dbgflags(int flags)
{
	debugflags = flags;
}

#endif
