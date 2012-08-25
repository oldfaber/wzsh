/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* forklib.h -- win32 fork library global functions and variables */


#if !defined(FORKLIB_H)
#define FORKLIB_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if defined(__MINGW32__)
#include <stdint.h>
#endif

#if !defined(STDIN_FILENO)
#define STDIN_FILENO 0
#endif
#if !defined(STDOUT_FILENO)
#define STDOUT_FILENO 1
#endif
#if !defined(STDERR_FILENO)
#define STDERR_FILENO 2
#endif

#define LENGTH_OF(x) (sizeof(x)/sizeof(*x))
#define ISPATHSEP(p) ((p == '/')||(p == '\\'))

#if defined(_MSC_VER)
# include <string.h>
# pragma intrinsic(strcmp, strcpy, strcat, strlen, strset, memcpy, memset)
/* Visual Studio 2005+ has this defined */
# if !defined(_INTPTR_T_DEFINED)
#  define _INTPTR_T_DEFINED
#  define intptr_t long
# endif
#endif

/* this flag it is set in FILE struct flags
 and Windows already defines
 FILE_TYPE_UNKNOWN==0x0000 FILE_TYPE_DISK==0x0001
 FILE_TYPE_CHAR==0x0002    FILE_TYPE_PIPE==0x0003
 FILE_TYPE_REMOTE==0x8000 (unused!)
*/
#define FILE_O_APPEND 	0x0008

#define __MAX_OPEN_FILES 128

#ifdef	__cplusplus
extern "C" {
#endif

/* signal.c */
void nt_init_signals(void);
void nt_cleanup_signals(void);
BOOL generic_handler(int signo);
int start_sigchild_thread(HANDLE hproc, DWORD pid);
BOOL ctrl_handler(DWORD);

/* fork.c */
void fork_init(char ***cargvp);
void init_startupinfo(STARTUPINFO *si);
void close_si_handles(void);
BOOL CreateWow64Events(DWORD pid, HANDLE *hParent, HANDLE *hChild, BOOL bOpenExisting);

/* io.c/stdio.c */
int consoleread(HANDLE hInput, unsigned char *buf, unsigned int howmany);
void nt_init_io(void);
void copy_fds(void);
void restore_fds(void);
void close_copied_fds(void);
intptr_t _nt_get_osfhandle(int fd);
int _nt_open_osfhandle(intptr_t h1, int mode);
int nt_isatty(int fd);
int fd_isset(int fd, int flags);
int translate_perms(int perms, SECURITY_ATTRIBUTES *sa, DWORD *dwAccess, DWORD *dwCreateDist);
int iodupfd(int fd, int nextfd);
BOOL winwrite(HANDLE hh, const void *buffer, unsigned int blen, unsigned int *written);
int nt_seek(HANDLE h1, long offset, int how);
void nt_init_stdio(void);

/* tconsole.c */
void nt_cleanup_term(void);

/* 00_entry.c */
extern BOOL bIsWow64Process;
extern char gModuleName[4096];
extern DWORD mainThreadId;

#ifdef	__cplusplus
}
#endif

/* windows heap allocations */
#define heap_alloc(s) HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(s))
#define heap_free(p) HeapFree(GetProcessHeap(),0,(p))
#define heap_realloc(p,s) HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(p),(s))

#endif /* FORKLIB_H */
