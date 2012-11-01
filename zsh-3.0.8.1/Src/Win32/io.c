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
 * $Header: /p/tcsh/cvsroot/tcsh/win32/io.c,v 1.9 2006/04/13 00:59:02 amold Exp $
 *
 * io.c
 * wrapper functions for some i/o routines.
 * -amol
 *
 * oldfaber:
 *      separated in io.c/stdio.c, heavily modified
 *      - stdio.c is an optional component
 *      - moved here all the non-stdio functions
 *      - moved here the restore_fds()/close_copied_fds()/copy_fds, needed for fork()
 *      - added f_read(), winwrite(), winread()
 *      - fixed the write buffer problem for FILE_TYPE_CHAR
 *      - uses long seek
 *      - removed the MY_FILE trick, (ab)uses the FILE struct
 */


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#include "signal.h"
#include "ntdbg.h"
#include "forklib.h"
#include "forksect.h"
#if defined(HAVE_CLIPBOARD)
#include "clipboard.h"
#endif


#define MAX_OPEN_FILES (int)LENGTH_OF(__filevect)

/* this file (ab)uses the following fiels of the _iobuf structure to hold
	char*	_base;         // file HANDLE
	int	_flag;         // file flags
	int	_file;         // file fd - the same as the index in the array
	int	_charbuf;      // saved char for '\r' handling
*/

#define GET_HANDLE(fd)		(__filevect[fd]._base)
#define GET_FD(fd)		(__filevect[fd]._file)
#define GET_FLAGS(fd)		(__filevect[fd]._flag)
#define GET_SAVEDCH(fd)       	(__filevect[fd]._charbuf)

#define INVALID_HANDLE(fd)  (GET_HANDLE(fd) == INVALID_HANDLE_VALUE)
#define BAD_FD(fd) ((fd < 0) || (fd >= (int)LENGTH_OF(__filevect)) || INVALID_HANDLE(fd))
#define SET_FLAGS(fd,f)  (GET_FLAGS(fd) = f)
#define SET_SAVEDCH(fd,v)  GET_SAVEDCH(fd) = v;

#define INVALID_CHANDLE(idx)  (__gOpenFilesCopy[idx].handle == INVALID_HANDLE_VALUE)
#define GET_CHANDLE(fd)  (__gOpenFilesCopy[fd].handle)
#define GET_CFLAGS(fd)  (__gOpenFilesCopy[fd].flags)
#define SET_CHANDLE(fd,h)  (__gOpenFilesCopy[fd].handle = h)
#define SET_CFLAGS(fd,f)  (__gOpenFilesCopy[fd].flags = f)

/* consoleread() #defines and vars */
/* base key mappings + ctrl-key mappings + alt-key mappings */
/* see tcsh nt.bind.c  to figure these out */
/*  256 +
	4*24 (fkeys) +
	4*4 (arrow) +
	4*2 (pgup/dn) +
	4*2 (home/end) +
	4*2 (ins/del)
*/
#define NT_NUM_KEYS                392 /* *NOT USED* */
#define NT_SPECIFIC_BINDING_OFFSET 256 /* where our bindings start */
#define KEYPAD_MAPPING_BEGIN       24 /* offset from NT_SPECIFIC where keypad mappings begin */
#define INS_DEL_MAPPING_BEGIN      32
#define SINGLE_KEY_OFFSET          0  /*if no ctrl or alt pressed */
#define CTRL_KEY_OFFSET            34
#define ALT_KEY_OFFSET             (34*2)
#define SHIFT_KEY_OFFSET           (34*3)


/* NCGLOBAL(s) defined in signal.c */
extern HANDLE __h_con_alarm, __h_con_int;

NCGLOBAL FILE __filevect[__MAX_OPEN_FILES];

/* @@@@ why not NCGLOBAL ? */
unsigned short __nt_want_vcode, __nt_vcode;

/*
 * this structure keeps the HANDLES duplicated in copy_fds()
 * and the file flags for the child.
 */
static struct {
	HANDLE  handle;
	int flags;
} __gOpenFilesCopy[__MAX_OPEN_FILES];


NCSTATIC INPUT_RECORD girec[2048];


/* reset the copy struct */
static void initOpenFileCopy(int fd)
{
	GET_CHANDLE(fd) = INVALID_HANDLE_VALUE;
	GET_CFLAGS(fd) = 0;
}


/* reset our FILE struct to 'closed' */
static void closeFILE(int fd)
{
	GET_HANDLE(fd) = (char *)INVALID_HANDLE_VALUE;
	SET_FLAGS(fd, 0);
	GET_FD(fd) = -1;
	GET_SAVEDCH(fd) = -1;
}


/* initialize a FILE struct to 'opened' */
static void openFILE(int fd, HANDLE h, int flags)
{
	GET_HANDLE(fd) = (char *)h;
        if (flags == -1)
		/* default, at open time we are only interested in FILE_TYPE_CHAR */
		SET_FLAGS(fd, GetFileType(h) & FILE_TYPE_CHAR);
        else
		SET_FLAGS(fd, flags);
	GET_FD(fd) = fd;
	GET_SAVEDCH(fd) = -1;
}


/* MSDN says it returns a long but ../include/io.h says intptr_t */
intptr_t _nt_get_osfhandle(int fd)
{
	if (fd < 0 || fd >= MAX_OPEN_FILES) {
		dbgprintf(PR_ERROR, "!!! %s(%d): invalid fd\n", __FUNCTION__, fd);
		return (intptr_t)INVALID_HANDLE_VALUE;
	}
	return (intptr_t)(GET_HANDLE(fd));
}


/* returns the first free handle */
static int freehandle(void)
{
	int ii;

	for (ii = 0; ii < MAX_OPEN_FILES; ii++)
		if (INVALID_HANDLE(ii))
			return (ii);
	return (-1);
}


/*
 * find a free __filevect slot and initialize it, return the slot number as the fd, -1 on error
 */
int _nt_open_osfhandle(intptr_t h1, int mode)
{
	int fd = freehandle();

	if (fd < 0) {
		errno = EMFILE;
		return (-1);
	}
	openFILE(fd, (HANDLE)h1, -1);
	if (mode & _O_APPEND)
		SET_FLAGS(fd, GET_FLAGS(fd) | FILE_O_APPEND);
	dbgprintf(PR_IOVERB, "_nt_open_osfhandle(0x%p, ..), fd=%d flags=%d\n", (void *)h1, fd, GET_FLAGS(fd));
	return (fd);
}


/* was init_stdio in stdio.c */
void nt_init_io(void)
{
	int ii;

	/* init ALL the arrays to invalid */
	for (ii = 0; ii < MAX_OPEN_FILES; ii++) {
		closeFILE(ii);
		initOpenFileCopy(ii);
	}
	openFILE(STDIN_FILENO, GetStdHandle(STD_INPUT_HANDLE), -1);
	openFILE(STDOUT_FILENO, GetStdHandle(STD_OUTPUT_HANDLE), -1);
	openFILE(STDERR_FILENO, GetStdHandle(STD_ERROR_HANDLE), -1);
	dbgprintf(PR_IOVERB, "nt_init_io() parent STD handles: 0=0x%p 1=0x%p 2=0x%p\n", GET_HANDLE(STDIN_FILENO), GET_HANDLE(STDOUT_FILENO), GET_HANDLE(STDERR_FILENO));
#if defined(LIB_HAS_STDIO)
	nt_init_stdio();
#endif
}


/* was in in stdio.c, called from child before returning from fork() */
void restore_fds(void)
{
	int ii;

	for (ii = 3; ii < MAX_OPEN_FILES; ii++) {
		if (INVALID_CHANDLE(ii))
			continue;
		openFILE(ii, GET_CHANDLE(ii), GET_CFLAGS(ii));
		dbgprintf(PR_IOVERB, "restore_fds() fd %d <= handle 0x%p\n", ii, GET_HANDLE(ii));
	}
}


/* was in stdio.c, called by the parent at fork() exit */
void close_copied_fds(void)
{
	int ii;

	for (ii = 3; ii < MAX_OPEN_FILES; ii++) {
		if (INVALID_CHANDLE(ii))
			continue;
		CloseHandle(GET_CHANDLE(ii));
		dbgprintf(PR_IOVERB, "close_copied_fds() close handle 0x%p for fd %d\n", GET_CHANDLE(ii), ii);
		initOpenFileCopy(ii);
	}
}


/* was in stdio.c, called from parent fork() before creating child process */
void copy_fds(void)
{
	int ii;

	for (ii = 3; ii < MAX_OPEN_FILES; ii++) {
		if (INVALID_HANDLE(ii)) {
			initOpenFileCopy(ii);
			continue;
		}
		if (!DuplicateHandle(GetCurrentProcess(), GET_HANDLE(ii),
				     GetCurrentProcess(), &GET_CHANDLE(ii),
				     0, TRUE, DUPLICATE_SAME_ACCESS)) {
			dbgprintf(PR_ERROR, "!!! DuplicateHandle(0x%p, ..) error %ld in %s\n", GET_HANDLE(ii), GetLastError(), __FUNCTION__);
			initOpenFileCopy(ii);
			continue;
		}
		SET_CFLAGS(ii, GET_FLAGS(ii));
	}
}


int fd_isset(int fd, int flags)
{
	if (BAD_FD(fd))
		return (0);
	return (GET_FLAGS(fd) & flags);
}


int close(int fd)
{
	int retval = 0;

	if (BAD_FD(fd)) {
		if (fd < 0)
			dbgprintf(PR_ERROR, "!!! close(%d): invalid fd\n", fd);
                else
			dbgprintf(PR_ERROR, "!!! close(%d): invalid fd handle 0x%p\n", fd, GET_HANDLE(fd));
		errno = EBADF;
		return (-1);
	}
	if (!CloseHandle(GET_HANDLE(fd))) {
		dbgprintf(PR_ERROR, "!!! close(%d): error %ld for handle 0x%p\n", fd, GetLastError(), GET_HANDLE(fd));
		errno = EBADF;
		/* prepare return failure */
		retval = -1;
	}
	dbgprintf(PR_IO, "close(%d): handle 0x%p\n", fd, GET_HANDLE(fd));
	closeFILE(fd);
	return (retval);
}


int nt_seek(HANDLE h1, long offset, int how)
{
	DWORD dwmove;
	LARGE_INTEGER lloffset;

	switch (how) {
		case SEEK_CUR:
			dwmove = FILE_CURRENT;
			break;
		case SEEK_END:
			dwmove = FILE_END;
			break;
		case SEEK_SET:
			dwmove = FILE_BEGIN;
			break;
		default:
			errno = EINVAL;
			return (-1);
	}
	lloffset.QuadPart = (LONGLONG)offset;
	if (!SetFilePointerEx(h1, lloffset, NULL, dwmove)) {
		errno = EBADF;
		return (-1);
	}
	return 0;
}


long lseek(int fd, long offset, int how)
{
	if (BAD_FD(fd)) {
		dbgprintf(PR_ERROR, "!!! lseek(%d): invalid fd\n", fd);
		return (-1L);
	}
	return nt_seek(GET_HANDLE(fd), offset, how);
}


/* cannot substitute the MSVCRT function */
int nt_isatty(int fd)
{
	if (BAD_FD(fd)) {
		dbgprintf(PR_ERROR, "!!! isatty(%d): invalid fd\n", fd);
		return (0);
	}
	return (GET_FLAGS(fd) & FILE_TYPE_CHAR);
}


int dup(int fdin)
{
	HANDLE hdup;
	HANDLE horig;
	int newfd;

	if (BAD_FD(fdin)) {
		dbgprintf(PR_ERROR, "!!! dup(%d): invalid fd\n", fdin);
		errno = EBADF;
		return (-1);
	}
	horig = GET_HANDLE(fdin);
	if (!DuplicateHandle(GetCurrentProcess(), horig,
			     GetCurrentProcess(), &hdup,
			     0, FALSE, DUPLICATE_SAME_ACCESS)) {
		dbgprintf(PR_ERROR, "!!! DuplicateHandle(0x%p, ..) error %ld in %s\n", horig, GetLastError(), __FUNCTION__);
		errno = EBADF;
		return (-1);
	}
	newfd = _nt_open_osfhandle((intptr_t)hdup, 0);
	if (newfd < 0)
                return (-1);
	SET_FLAGS(newfd, GET_FLAGS(fdin));
	dbgprintf(PR_IO, "dup(%d) handle 0x%p => fd %d handle 0x%p\n", fdin, horig, newfd, hdup);
	return newfd;
}


int dup2(int fdorig, int fdcopy)
{
	HANDLE hdup;
	HANDLE horig;

	if (BAD_FD(fdorig)) {
		dbgprintf(PR_ERROR, "!!! dup2(%d, ): invalid fd\n", fdorig);
		errno = EBADF;
		return (-1);
	}
	horig = GET_HANDLE(fdorig);
	if (fdcopy < 0 || fdcopy >= MAX_OPEN_FILES) {
		errno = EBADF;
		return (-1);
	}
	if (GET_HANDLE(fdcopy) != INVALID_HANDLE_VALUE) {
		CloseHandle(GET_HANDLE(fdcopy));
		closeFILE(fdcopy);
	}
	if (!DuplicateHandle(GetCurrentProcess(), horig,
			     GetCurrentProcess(), &hdup,
			     0, fdcopy<3 ? TRUE:FALSE, DUPLICATE_SAME_ACCESS)) {
		dbgprintf(PR_ERROR, "!!! DuplicateHandle(0x%p, ..) error %ld in %s\n", horig, GetLastError(), __FUNCTION__);
		errno = EBADF;
		return (-1);
	}
	openFILE(fdcopy, hdup, GET_FLAGS(fdorig));
	switch(fdcopy) {
	case 0:
		SetStdHandle(STD_INPUT_HANDLE, hdup);
		break;
	case 1:
		SetStdHandle(STD_OUTPUT_HANDLE, hdup);
		break;
	case 2:
		SetStdHandle(STD_ERROR_HANDLE, hdup);
		break;
	}
	dbgprintf(PR_IO, "dup2(%d, %d) handle 0x%p flags %d => handle 0x%p flags %d\n", fdorig, fdcopy, horig, GET_FLAGS(fdorig), hdup, GET_FLAGS(fdcopy));
	return (0);
}


/*
 DESCRTPTION
	This is the core of
	int fe = fcntl(fd, F_DUPFD, nextfd);
*/

int iodupfd(int fd, int nextfd)
{
	HANDLE hdup;
	HANDLE horig;
	int ii;

	if (BAD_FD(fd)) {
		dbgprintf(PR_ERROR, "!!! iodupfd(%d, %d): invalid fd\n", fd, nextfd);
		errno = EBADF;
		return (-1);
	}
	if ((nextfd < 0) || (nextfd >= MAX_OPEN_FILES)) {
		dbgprintf(PR_ERROR, "!!! iodupfd(%d, %d): invalid nextfd\n", fd, nextfd);
		errno = EINVAL;
		return (-1);
	}
	for (ii = nextfd; ii < MAX_OPEN_FILES; ii++) {
		if (INVALID_HANDLE(ii)) {
			/* found the slot */
			horig = GET_HANDLE(fd);
			if (!DuplicateHandle(GetCurrentProcess(), horig,
					     GetCurrentProcess(), &hdup,
					     0, FALSE, DUPLICATE_SAME_ACCESS)) {
				dbgprintf(PR_ERROR, "!!! DuplicateHandle(0x%p, ..) error %ld in %s\n", horig, GetLastError(), __FUNCTION__);
				errno = EBADF;
				return (-1);
			}
			openFILE(ii, hdup, GET_FLAGS(fd));
			dbgprintf(PR_IO, "%d = iodupfd(%d, %d), handle 0x%p => handle 0x%p\n", ii, fd, nextfd, horig, hdup);
			return (ii);
		}
	}
	/* no more files */
	errno = EMFILE;
	return (-1);
}


int pipe(int pipefd[2])
{
	HANDLE hpipe[2];
	SECURITY_ATTRIBUTES secd;

	if (!pipefd) {
		errno = EFAULT;
		return (-1);
	}
	secd.nLength = sizeof(secd);
	secd.lpSecurityDescriptor = NULL;
	secd.bInheritHandle = FALSE;

	if (!CreatePipe(&hpipe[0], &hpipe[1], &secd, 0)) {
		dbgprintf(PR_ERROR, "!!! CreatePipe() error %ld\n", GetLastError());
		/* no error is really appropriate */
		errno = EMFILE;
		return (-1);
	}
	pipefd[0] = _nt_open_osfhandle((intptr_t)hpipe[0], 0);
	pipefd[1] = _nt_open_osfhandle((intptr_t)hpipe[1], 0);
	dbgprintf(PR_IO, "%s(): fd0=%d(handle 0x%p) fd1=%d(handle 0x%p)\n", __FUNCTION__, pipefd[0], hpipe[0], pipefd[1], hpipe[1]);
	return 0;
}


/* also used int stdio.c::nt_open2() */
int translate_perms(int perms, SECURITY_ATTRIBUTES *sa, DWORD *dwAccess, DWORD *dwCreateDist)
{
	sa->nLength = sizeof(SECURITY_ATTRIBUTES);
	sa->lpSecurityDescriptor = NULL;
	sa->bInheritHandle = FALSE;

	*dwAccess = 0;
	*dwCreateDist = 0;
	switch (perms & (_O_RDONLY | _O_WRONLY | _O_RDWR)) {
		case _O_RDONLY:
			*dwAccess = GENERIC_READ;
			break;
		case _O_WRONLY:
			*dwAccess = GENERIC_WRITE;
			break;
		case _O_RDWR:
			*dwAccess = GENERIC_READ | GENERIC_WRITE;
			break;
		default:
			errno = EINVAL;
			return (0);
	}
	switch (perms & (_O_CREAT | _O_TRUNC | _O_EXCL)) {
		case 0:
			*dwCreateDist = OPEN_EXISTING;
			break;
		case _O_CREAT:
			*dwCreateDist = OPEN_ALWAYS;
			break;
		case _O_TRUNC:
			*dwCreateDist = TRUNCATE_EXISTING;
			break;
		case _O_CREAT | _O_TRUNC:
			*dwCreateDist = CREATE_ALWAYS;
			break;
		case _O_CREAT | _O_EXCL:
		case _O_CREAT | _O_TRUNC | _O_EXCL:
			*dwCreateDist = CREATE_NEW;
			break;
		default:
			errno = EINVAL;
			return (0);
	}
	return (1);
}


/*
 NOTES
	http://support.microsoft.com/kb/90088/en-us says
	CONIN$ must have FILE_SHARE_READ an
	CONOUT$ must have FILE_SHARE_WRITE
	we open with (FILE_SHARE_READ|FILE_SHARE_WRITE) and all looks ok, but ...
	/dev/tty are usuallu opened to get/set the terminal physical interface
	like speed,
*/

int open(const char *filename, int perms, ...)
{
	// ignore the bloody mode

	int fd, mode;
	HANDLE hfile;
	SECURITY_ATTRIBUTES security;
	DWORD dwAccess, dwFlags, dwCreateDist;
	va_list ap;

	/* probably faster than creating the file and deleting it */
	if (freehandle() < 0) {
		errno = EMFILE;
		return (-1);
	}

	va_start(ap, perms);
	mode = va_arg(ap, int);
	va_end(ap);

	/* @@@@ unify with stdio.c::nt_open2() */
	if (!lstrcmp(filename,"/dev/tty")) {
		if (perms == O_RDONLY) 
			filename = "CONIN$";
		else if (perms & O_WRONLY)
			filename = "CONOUT$";
		else if (perms & O_RDWR)
			filename = "CONIN$";
		else {
			dbgprintf(PR_IO, "%s(%s, ..): bad permissions %d\n", __FUNCTION__, filename, perms);
			errno = EACCES;
			return (-1);
		}
	} else if (!lstrcmp(filename, "/dev/null")) {
		filename = "NUL";
#if defined(HAVE_CLIPBOARD)
	} else if (!lstrcmp(filename, "/dev/clipboard")) {
		if (perms & O_WRONLY)
			hfile = create_clip_writer_thread();
		else
			hfile = create_clip_reader_thread();
		if (hfile != INVALID_HANDLE_VALUE)
			goto get_fd;
		dbgprintf(PR_ERROR, "!!! %s(%s, ..): open error\n", __FUNCTION__, filename);
		errno = ENOENT;
		return (-1);
#endif
	}
	if (!translate_perms(perms, &security, &dwAccess, &dwCreateDist)) {
		dbgprintf(PR_IO, "%s(%s, %#x, ..): translate permission error\n", __FUNCTION__, filename, perms);
		errno = EPERM;
		return (-1);
	}
	dwFlags = 0;
	if (perms & O_TEMPORARY)
		dwFlags = FILE_FLAG_DELETE_ON_CLOSE;
	hfile = CreateFile(filename, dwAccess, FILE_SHARE_READ | FILE_SHARE_WRITE,
			   &security, dwCreateDist, dwFlags,  NULL);

	if (hfile == INVALID_HANDLE_VALUE) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			errno = ENOENT;
		else
			errno = EACCES;
		return (-1);
	}
	if (perms & _O_APPEND) {
		LARGE_INTEGER zloffset; zloffset.QuadPart = 0;
		if (!SetFilePointerEx(hfile, zloffset, NULL, FILE_END))
			dbgprintf(PR_ERROR, "!!! SetFilePointerEx(0x%p, ..) error %ld in %s\n", hfile, GetLastError(), __FUNCTION__);
	}
get_fd:
	fd = _nt_open_osfhandle((intptr_t)hfile, 0);
	if (fd < 0) {
		/* should not happen, freehandle() was OK, in any case the EMFILE errno
		   comes from _nt_open_osfhandle */
		CloseHandle(hfile);
		return (-1);
	}
	dbgprintf(PR_IO, "%s(%s, %#x, ..): fd=%d handle 0x=%p\n", __FUNCTION__, filename, perms, fd, hfile);
	return fd;
}


/* 
 * this is from the old zsh code
 * the "new" tcsh consoleread does not work yet
 */
#if defined(SHELL)
extern
#endif
#if defined(ZSHV4)
# define COLS zterm_columns
# define LINS zterm_lines
#else
# define COLS columns
# define LINS lines
#endif
int COLS, LINS;

int consoleread(HANDLE hInput, unsigned char *buf, unsigned int howmany)
{
	INPUT_RECORD *irec;
	DWORD numread,controlkey,i;
	WORD vcode;
	unsigned char ch;
	int rc;
	unsigned int where = 0;
	int alt_pressed = 0, memfree = 0;
	HANDLE hevents[3];

// This function is called very frequently. So, we don't:
// 1. Declare large arrays on the stack (use girec)
// 2. Allocate any memory unless we really need to.
//
// This gives me the illusion of speedups, so there.
//
// -amol
//
	if (howmany > 0) {
		if (howmany > 2048){
			irec = (INPUT_RECORD *)heap_alloc(howmany*sizeof(INPUT_RECORD));
			memfree = 1;
		}
		else
			irec = &(girec[0]);
		if (!irec){
			errno = ENOMEM;
			return -1;
		}
	}

	while (1) {
		hevents[0] = __h_con_alarm;
		hevents[1] = __h_con_int;
		hevents[2] = hInput;
		rc = WaitForMultipleObjects(sizeof(hevents)/sizeof(hevents[0]), hevents, FALSE, INFINITE);
		if (rc == WAIT_OBJECT_0) {
                        /* process the async signal and loop again */
			generic_handler(SIGALRM);
		}
		if (rc == (WAIT_OBJECT_0 + 1)) {
                        /* @@@@ should we call the SIGINT handler ? */
			errno = EINTR;
			return -1;
		}
		if (!ReadConsoleInput(hInput, irec, (DWORD)howmany, &numread)) {
			dbgprintf(PR_ERROR, "!!! %s(): ReadConsoleInput(0x%p, ..) error %ld\n", __FUNCTION__, hInput, GetLastError());
			errno = EBADF;
			if (memfree)
				heap_free(irec);
			return -1;
		}
		for (i = 0; i < numread; i++) {
			switch (irec[i].EventType) {
			case WINDOW_BUFFER_SIZE_EVENT:
				/* resizing the comsole window adjusts $LINES and $COLUMNS */
				COLS = irec[i].Event.WindowBufferSizeEvent.dwSize.X;
				LINS = irec[i].Event.WindowBufferSizeEvent.dwSize.Y;
				break;
			case KEY_EVENT:
				if (irec[i].Event.KeyEvent.bKeyDown) {
					vcode = (irec[i].Event.KeyEvent.wVirtualKeyCode);
					ch = (irec[i].Event.KeyEvent.uChar.AsciiChar);
					controlkey = (irec[i].Event.KeyEvent.dwControlKeyState);
					if (controlkey & LEFT_ALT_PRESSED)
						alt_pressed = 1;
					else if (controlkey & RIGHT_ALT_PRESSED)
						alt_pressed = 2;

					/* This hack for arrow keys -amol 9/28/96 */

					if (!(__nt_want_vcode & 0x01))
						goto skippy;
					if (vcode>= VK_F1 && vcode <= VK_F24){
						buf[where++]='\033';
						__nt_want_vcode = vcode - VK_F1;
						__nt_want_vcode <<= 8;
						__nt_want_vcode |= 2;
						return 1;
					}
					else if (vcode>= VK_PRIOR && vcode <= VK_DOWN) {
						buf[where++] = '\033';
						__nt_want_vcode = 24 + (vcode - VK_PRIOR);
						__nt_want_vcode <<= 8;
						__nt_want_vcode |= 2;
						return 1;
					}
					else if (vcode == VK_INSERT) {
						buf[where++] = '\033';
						__nt_want_vcode= 24 + 8;
						__nt_want_vcode <<= 8;
						__nt_want_vcode |= 2;
						return 1;
					}
					else if (vcode == VK_DELETE) {
						buf[where++] = '\033';
						__nt_want_vcode = 24 + 9;
						__nt_want_vcode <<= 8;
						__nt_want_vcode |= 2;
						return 1;
					}
				skippy:
					if (vcode == VK_ESCAPE) {
						buf[where++]='\033';
					} else if (ch) {
						/* @@@@ only left alt considered ! */
						if (1 == alt_pressed) {
							ch += 128;
						}
						if (ch == '\r') {
							ch = '\n';
						}
						buf[where++] = ch;
					}
					alt_pressed = 0;
				}
				break;
			default:
				break;
			}
		}
		if (where == 0)
			continue;
		if (where < howmany)
			buf[where] = 0;
		break;
	}
	if (memfree)
		heap_free(irec);
	if (!where)
		return -1;
	return ((int)where);
}


/*
 * basic ReadFile wrapper
 */
static int winread(HANDLE hread, void *buf, unsigned int howmany)
{
	DWORD err, numread = 0;

	if (!ReadFile(hread, buf, (DWORD)howmany, &numread, NULL)) {
		err = GetLastError();
		switch (err) {
		case ERROR_HANDLE_EOF:
		case ERROR_BROKEN_PIPE:
			errno = 0;
			return 0;
		case ERROR_ACCESS_DENIED:
		case ERROR_INVALID_HANDLE:
		default:
			dbgprintf(PR_ERROR, "!!! ReadFile(0x%p, ..) error %ld\n", hread, err);
			errno = EBADF;
			return -1;
		}
	}
	return ((int)numread);
}


/*
 NOTE
	crude hack to skip '\r' when followed by a '\n', ONLY when
	reading 1 byte at a time
*/
static int f_read(int fd, void *buf, unsigned int howmany)
{
	int numread;
	char nextch;
	HANDLE hread;

	hread = (HANDLE)_nt_get_osfhandle(fd);
	if (hread == INVALID_HANDLE_VALUE)
		return 0;
	/* if available return the saved char */
	if (GET_SAVEDCH(fd) != -1) {
		if (howmany == 1) {
			*(char *)buf = GET_SAVEDCH(fd);
			SET_SAVEDCH(fd, -1);
			dbgprintf(PR_IOR, "%s(): returning '%#2.2x'\n", __FUNCTION__, *(char *)buf);
			return 1;
		} else {
			/* a different handle or a longer read */
			dbgprintf(PR_IOR, "%s(0x%p, .., %u): discarding '%#2.2x'\n", __FUNCTION__, hread, howmany, GET_SAVEDCH(fd) & 0xff);
			SET_SAVEDCH(fd, -1);
		}
	}
	numread = winread(hread, buf, howmany);
	if (numread <= 0)
		return (numread);
	if ((numread == 1) && (*(char *)buf == '\r')) {
		numread = winread(hread, &nextch, 1);
		if ((numread == 1) && (nextch != '\n')) {
			SET_SAVEDCH(fd, nextch & 0xff);
		} else {
			/* replace '\r' with '\n', returns '\n' */
			*(char *)buf = nextch;
		}
	}
#if defined(W32DEBUG)
	if (numread == 1)
		dbgprintf(PR_IOR, "%s(0x%p): read %#2.2x='%c'\n", __FUNCTION__, hread, *((char *)buf) & 0xff, isprint(*((char *)buf) & 0xff) ? *((char *)buf) & 0xff : ' ');
	else
		dbgprintf(PR_IOR, "%s(0x%p): read %d characters\n", __FUNCTION__, hread, numread);
#endif
	return ((int)numread);
}


/*
 * readfile: (ex force_read) Forces a ReadFile, instead of ReadConsole
 */
int readfile(int fd, void *buf, unsigned int howmany)
{
	return f_read(fd, buf, howmany);
}


/*
 * Read from console or from file. Returns number of bytes read, 0 if end of
 * file, -1 on error
 */
int read(int fd, void *buf, unsigned int howmany)
{
        HANDLE hread;

        hread = (HANDLE)_nt_get_osfhandle(fd);
	if (hread == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return (-1);
	}
	if (GetFileType(hread) == FILE_TYPE_CHAR)
		return consoleread(hread, (unsigned char *)buf, howmany);
	return f_read(fd, buf, howmany);
}


/*
 * This is a wrapper around WriteFile() to workaround the 64k write limit
 * on consoles (and network pipes!)
 */
BOOL winwrite(HANDLE hh, const void *buffer, unsigned int bufferlen, unsigned int *written)
{
	DWORD filetype, bw;
	size_t bsize;
	ULONGLONG lsize;

	bsize = bufferlen;
	filetype = GetFileType(hh);
	if (filetype == FILE_TYPE_UNKNOWN) {
		if (GetLastError() != NO_ERROR)
			return FALSE;
	} else if ((filetype == FILE_TYPE_CHAR) && (bufferlen > 4096)) {
		bsize = 4096; /* write 4k at a time */
	}
	lsize = bufferlen;
	*written = 0;
	/* if filetype is not FILE_TYPE_CHAR or bufferlen is small this loop is
	   executed only once, bsize is the full buffer size */
	do {
		if (!WriteFile(hh, buffer, (DWORD)bsize, (DWORD*)&bw, NULL))
			return FALSE;
		buffer = (char *)buffer + bsize;
		lsize = lsize - bsize;
		if (lsize < bsize)
			bsize = (size_t)lsize;
		*written = *written + bw;
	} while (lsize > 0);
	return TRUE;
}


int write(int fd, const void *buffer, unsigned int howmany)
{
	unsigned int written;
	HANDLE hout;

	hout = (HANDLE)_nt_get_osfhandle(fd);
	if (hout == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return (-1);
	}
	if (fd_isset(fd, FILE_O_APPEND)) {
		LARGE_INTEGER loffset; loffset.QuadPart = 0;
		if (!SetFilePointerEx(hout, loffset, NULL, FILE_END))
			dbgprintf(PR_ERROR, "!!! SetFilePointerEx(0x%p, ..) error %ld in %s\n", hout, GetLastError(), __FUNCTION__);
	}
	if (!winwrite(hout, buffer, howmany, &written)) {
		DWORD err = GetLastError();
		dbgprintf(PR_ERROR, "!!! %s(%d): handle 0x%p error %ld\n", __FUNCTION__, fd, hout, err);
		switch (err) {
		case ERROR_BROKEN_PIPE:
			errno = EPIPE;
		case ERROR_ACCESS_DENIED:
		case ERROR_INVALID_HANDLE:
		default:
			errno = EBADF;
		}
		return (-1);
	}
#if defined(W32DEBUG)
	if (written == 1)
		dbgprintf(PR_IOW, "%s(%d, ..): handle 0x%p wrote %#2.2x='%c'\n", __FUNCTION__, fd, hout, *((char *)buffer) & 0xff, isprint(*((char *)buffer) & 0xff) ? *((char *)buffer) & 0xff : ' ');
	else
		dbgprintf(PR_IOW, "%s(%d, ..): handle 0x%p written %u\n", __FUNCTION__, fd, hout, written);
#endif
	return (written ? (int)written : -1);
}
