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
 * $Header: /p/tcsh/cvsroot/tcsh/win32/stdio.c,v 1.9 2006/03/11 01:47:40 amold Exp $
 *
 * stdio.c Implement a whole load of i/o functions.
 *         This makes it much easier to keep track of inherited handles and
 *         also makes us reasonably vendor crt-independent.
 * -amol
 *
 * oldfaber:
 *      separated into io.c and stdio.c
 *      only used if defined(LIB_HAS_STDIO)
 */


#if defined(LIB_HAS_STDIO)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#include "ntdbg.h"
#include "forklib.h"
#include "forksect.h"

/* beware: this CRT function returns -1 if buffer too small ! */
#define XVSNPRINTF(b,l,f,v) _vsnprintf(putbuf, sizeof(putbuf)-1, format, vl);

#define MAX_OPEN_FILES (int)LENGTH_OF(__filevect)
#define BAD_FDNO(fd)		((fd < 0) || (fd >= MAX_OPEN_FILES))

#if defined(__MINGW32__)
# undef ferror
# undef feof
#endif

#if defined(_MSC_VER)
# define inline __inline
#endif

/* defined in io.c */
extern FILE __filevect[__MAX_OPEN_FILES];

#define GET_FILEHANDLE(s)	((HANDLE)(s->_base))
#define GET_HANDLE(fd)		(__filevect[fd]._base)
#if !defined(W32DEBUG)
# define GET_FD(s)		(s->_file)
#else
# define GET_FD(s)		get_fd_checked(s)
static int get_fd_checked(FILE *s)
{
	int fdno = (int)(s - &__filevect[0]);

	if (!s)
		dbgprintf(PR_ERROR, "!!! GET_FD() error NULL stream\n");
	else if (fdno == s->_file)
		return fdno;
	/* the FILE* is NOT from our vector ! */
	dbgprintf(PR_ERROR, "!!! GET_FD() error stream 0x%p fd=%d _file=%d\n", s, fdno, s->_file);
	/* but maybe the fd _is_ usable */
	if (BAD_FDNO(s->_file))
		return (-1);
	return(s->_file);
}
#endif


NCGLOBAL FILE *nt_stdin, *nt_stdout, *nt_stderr;


void nt_init_stdio(void)
{
	nt_stdin = &__filevect[STDIN_FILENO];
	nt_stdout = &__filevect[STDOUT_FILENO];
	nt_stderr = &__filevect[STDERR_FILENO];
}


void clearerr(FILE* ignore)
{
	UNREFERENCED_PARAMETER(ignore);
	return;
}


FILE* fdopen(int fd, const char *mode)
{
	HANDLE h;

	/* use the mode the fd was opened with, ignore mode */
	UNREFERENCED_PARAMETER(mode);
	if (BAD_FDNO(fd) || ((h = GET_HANDLE(fd)) == INVALID_HANDLE_VALUE))
		return NULL;
	dbgprintf(PR_IO, "fdopen(%d) handle 0x%p\n", fd, h);
	return (&__filevect[fd]);
}


int fflush(FILE *stream)
{
	UNREFERENCED_PARAMETER(stream);
	return 0;
}


int _fcloseall(void)
{
	int ii;

	/* skip stdin, stdout, stderr */
	for (ii = 3; ii < MAX_OPEN_FILES; ii++) {
		if (__filevect[ii]._file >= 0)
			close(__filevect[ii]._file);
	}
	return ii;
}


int _flush(void)
{
	/* no error */
	return 0;
}


int _flushall(void)
{
	/* no error */
	return 0;
}


int ferror(FILE *stream)
{
	UNREFERENCED_PARAMETER(stream);
	return 0;
}


int feof(FILE *stream)
{
	UNREFERENCED_PARAMETER(stream);
	/* nor really valid, but feof is not really used */
	return (GetLastError() == ERROR_HANDLE_EOF ? 1 : 0);
}


int fputc(int c, FILE *stream)
{
	/* write returns -1 == EOF on error */
	return write(GET_FD(stream), &c, 1);
}


int __cdecl fprintf(FILE *stream, const char *format,...)
{
	va_list vl;
	int rv;
	char putbuf[4096];

	va_start(vl, format);
	rv = XVSNPRINTF(putbuf, sizeof(putbuf)-1, format, vl);
	va_end(vl);
	if (rv < 0)
		return (-1);
	return write(GET_FD(stream), putbuf, rv);
}


#if defined(_MSC_VER)
/* needed to ovverride libcmt printf() */
int _get_printf_count_output(void)
{
	return (0);
}
#endif

int __cdecl printf(const char *format, ...)
{
	va_list vl;
	int rv;
	char putbuf[4096];

	va_start(vl, format);
	rv = XVSNPRINTF(putbuf, sizeof(putbuf)-1, format, vl);
	va_end(vl);
	if (rv < 0)
		return (-1);
	return write(GET_FD(nt_stdout), putbuf, rv);
}


int puts(const char *str)
{
	static const char nl = '\n';
	int rc;
	int wfd;

	wfd = GET_FD(nt_stdout);
	rc = write(wfd, str, (unsigned int)strlen(str));
	write(wfd, &nl, 1);
	return rc;
}


int putchar(int c)
{
	return fputc(c, nt_stdout);
}


int fclose(FILE *fp)
{
	int fd = GET_FD(fp);

	dbgprintf(PR_IO, "fclose(0x%p)\n", fp);
	close(fd);
	return 0;
}


#undef fgetc
int fgetc(FILE *fp)
{
	unsigned char ch;
	int numread = read(GET_FD(fp), &ch, 1);

	return (numread > 0 ? ch : EOF);
}


#undef getc
/* CRT fgetc/getc in the same object */
int getc(FILE *fp)
{
	unsigned char ch;
	int numread = read(GET_FD(fp), &ch, 1);

	return (numread > 0 ? ch : EOF);
}


int getchar(void)
{
	return fgetc(stdin);
}


int fputs(const char *str, FILE *outstream)
{
	return write(GET_FD(outstream), str, (unsigned int)strlen(str));
}


int putc(int ch, FILE *outstream)
{
	return write(GET_FD(outstream), (char*)&ch, 1);
}


FILE *fopen(const char *filename, const char *opentype)
{
	int mode, fd;

	if (!filename || !opentype) {
		errno = EINVAL;
		return NULL;
	}
	switch (*opentype) {
		case 'r':
			mode = _O_RDONLY;
			break;
		case 'w':
			mode = _O_WRONLY | _O_CREAT | _O_TRUNC;
			break;
		case 'a':
			mode = _O_WRONLY | _O_CREAT | _O_APPEND;
			break;
		default:
			errno = EINVAL;
		return NULL;
	}

	if (opentype[1] && opentype[1] == '+' ) {
		mode |= _O_RDWR;
		mode &= ~(_O_RDONLY | _O_WRONLY);
	}
	fd = open(filename, mode);
	if (fd < 0) {
		return NULL;
	}
	dbgprintf(PR_IO, "fopen(\"%s\", \"%s\") fd %d handle 0x%p\n", filename, opentype, fd, GET_HANDLE(fd));
	return (&__filevect[fd]);
}


int fseek(FILE *stream, long offset, int how)
{
	return nt_seek(GET_FILEHANDLE(stream), offset, how);
}


/*
 NOTES
	returns ONLY a 32 bit offset !
	ftello() should be better !
*/

long ftell(FILE* stream)
{
	LARGE_INTEGER where;
	LARGE_INTEGER zloffset;

	zloffset.QuadPart = 0;
	if (!SetFilePointerEx(GET_FILEHANDLE(stream), zloffset, &where, SEEK_CUR)) {
		dbgprintf(PR_ERROR, "!!! SetFilePointerEx(0x%p, ..) error %ld in %s\n", GET_FILEHANDLE(stream), GetLastError(), __FUNCTION__);
		return (-1L);
	}
	return (long)where.u.LowPart;
}


size_t fread(void *buffer, size_t size, size_t count, FILE* stream)
{
	DWORD read = 0;

	if (size == 0 || count == 0)
		return (0);

	/* (size * count) MAY_OVERFLOW */
	if (!ReadFile(GET_FILEHANDLE(stream), buffer, (DWORD)(size * count), &read, NULL)) {
		dbgprintf(PR_ERROR, "!!! %s(): error %ld for handle 0x%p\n", __FUNCTION__, GetLastError(), GET_FILEHANDLE(stream));
		errno = EBADF;
		return (0);
	}
	dbgprintf(PR_IOR, "%s(): handle 0x%p read %ld\n", __FUNCTION__, GET_FILEHANDLE(stream), read);
	/* partial read, leave the file pointer at end */
	if (!read || (read < size))
		return (0);
	return (read/size);
}


size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
	HANDLE hfile;
	unsigned int written = 0;

	if (size == 0 || count == 0)
		return (0);

	hfile = GET_FILEHANDLE(stream);
	if (hfile == INVALID_HANDLE_VALUE) {
		dbgprintf(PR_ERROR, "!!! %s(): FILE has invalid handle\n", __FUNCTION__);
		return (0);
	}
	if (fd_isset(GET_FD(stream), FILE_O_APPEND)) {
		LARGE_INTEGER zloffset;
		zloffset.QuadPart = 0;
		if (!SetFilePointerEx(hfile, zloffset, NULL, FILE_END)) {
			dbgprintf(PR_ERROR, "!!! SetFilePointerEx(0x%p, ..) error %ld in %s\n", hfile, GetLastError(), __FUNCTION__);
			return (0);
		}
	}
        /* (size * count) MAY_OVERFLOW */
	if (!winwrite(hfile, buffer, (DWORD)(size * count), &written)) {
		DWORD err = GetLastError();
		dbgprintf(PR_ERROR, "!!! %s(): handle 0x%p error %ld\n", __FUNCTION__, hfile, err);
		switch (err) {
		case ERROR_BROKEN_PIPE:
			errno = EPIPE;
		case ERROR_ACCESS_DENIED:
		case ERROR_INVALID_HANDLE:
		default:
			errno = EBADF;
		}
		return (0);
	}
	if (!written) {
		errno = EBADF;
		return (0);
	}
#if defined(W32DEBUG)
	if (written/size == 1)
		dbgprintf(PR_IOW, "%s(): handle 0x%p wrote %#2.2x='%c'\n", __FUNCTION__, hfile, *((char *)buffer) & 0xff, isprint(*((char *)buffer) & 0xff) ? *((char *)buffer) & 0xff : ' ');
	else
		dbgprintf(PR_IOW, "%s(): handle 0x%p written %u\n", __FUNCTION__, hfile, written);
#endif
	return (written/size);
}


char *fgets(char *string, int num, FILE* stream)
{
	DWORD bread = 0;
	HANDLE hfile;
	int i,j;
	LARGE_INTEGER lloffset;

	hfile = GET_FILEHANDLE(stream);
	if (!ReadFile(hfile, string, num-1, &bread, NULL)) {
		dbgprintf(PR_ERROR, "!!! fgets(): error %ld reading from handle 0x%p\n", GetLastError(), hfile);
		errno = EBADF;
		return NULL;
	}
	if (bread == 0)
		return NULL;
	string[bread] = 0;
	for (i=0; i < (int)bread; i++) {
		/* ignore CR */
		if (string[i] == '\n') {
			i++;
			break;
		}
	}
	j = bread - i; // - 1; // leftover characters

	lloffset.QuadPart = (LONGLONG)(-j);
	if (!SetFilePointerEx(hfile, lloffset, NULL, SEEK_CUR)) {
		dbgprintf(PR_ERROR, "!!! SetFilePointerEx(0x%p, ..) error %ld in %s\n", hfile, GetLastError(), __FUNCTION__);
		return NULL;
	}
	dbgprintf(PR_IOR, "%s(): handle 0x%p read %ld\n", __FUNCTION__, hfile, bread);
	string[i] = 0;

	return string;
}


int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
	UNREFERENCED_PARAMETER(stream);
	UNREFERENCED_PARAMETER(buf);
	UNREFERENCED_PARAMETER(mode);
	UNREFERENCED_PARAMETER(size);
	return (0);
}

#endif
