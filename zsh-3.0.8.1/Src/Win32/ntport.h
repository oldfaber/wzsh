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
 * ntport.h
 * the main header.
 * -amol
 *
 * oldfaber: cleaned up
 */


#ifndef NTPORT_H
#define NTPORT_H

/* avoid using __mingw_ printf functions from libmingwex.a */
#undef _GNU_SOURCE

#include <stdio.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <process.h>

#include <uni_types.h>
#include "fmalloc.h"


#if !defined(UNREFERENCED_PARAMETER)
# define UNREFERENCED_PARAMETER(x) {(x)=(x);}
#endif

#if defined(_MSC_VER)
# include <string.h>
# define strcasecmp(a, b) _stricmp(a, b)
# pragma intrinsic(strcmp, strcpy, strcat, strlen, strset, memcpy, memset)
/* signed/unsigned mismatch: in zle_tricky.c(3768): t2 should be a size_t */
# pragma warning(disable:4018)
/* conversion, mainly zlog/int, disable until solved */
# pragma warning(disable:4244)
/* truncation from 64bit size_t if -Wp64: never in 32 bit mode */
# pragma warning(disable:4311)
/* Visual Studio 2005+ has this defined */
# if !defined(_INTPTR_T_DEFINED)
#  define _INTPTR_T_DEFINED
#  define intptr_t long
# endif
#endif

#if defined(LIB_HAS_STDIO)
extern FILE* nt_stdin, *nt_stdout, *nt_stderr;
# if defined(stdin)
#  undef stdin
# endif
# if defined(stdout)
#  undef stdout
# endif
# if defined(stderr)
#  undef stderr
# endif
# define stdin (nt_stdin)
# define stdout (nt_stdout)
# define stderr (nt_stderr)
#endif

#define malloc fmalloc
#define free   ffree
#define realloc frealloc
#define calloc fcalloc

/* fstat has a body in stdio.h for MS compiler */
#define fstat(a,b)  nt_fstat(a,b)
#define stat(a,b)   nt_stat(a,b)
#define lstat(a,b)  nt_stat(a,b)
#define isatty      nt_isatty
#define execve(f,a,e) nt_execve(f,a,e)

#ifdef	__cplusplus
extern "C" {
#endif

/* 00_entry.c */
void nt_init(char ***argvp);

/* nt_execve.c */
int nt_execve(const char *prog, char **args, char **envp);

/* io.c */
int nt_isatty(int fd);
int readfile(int fd, void *buf, unsigned int howmany);

/* fstat.c */
int nt_fstat(int fd, struct stat *fs);
int nt_stat(const char *filename, struct stat *fs);

/* console.c */
int nt_getlines(void);
int nt_getcolumns(void);
int nt_setcursor(int mode);

/* zsh_support.c */
int is_win32abspath(const char p[]);
char *fmt_pwd_for_prompt(const char *dir);
char *xgetenvpath(const char *envstring, char *buf, size_t bufsize);
void caseify_pwd(char *curwd);
int waitkey(int fd, int howlong);
void win32beep(void);
char *get_os_dir(const char *dirtype);

/* access.c, used in hashtable.c */
int is_pathext(const char *extension);

/* global vars */
extern unsigned short __nt_want_vcode;
extern int ntvirtualbind[];

#ifdef	__cplusplus
}
#endif

#endif /* NTPORT_H */
