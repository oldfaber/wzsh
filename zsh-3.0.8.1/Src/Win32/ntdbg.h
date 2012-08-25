/*
 *  Copyright (c) 2012, oldfaber _at_ gmail.com
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 *  SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 *  RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *  NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
 *  USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* debug/log interface */

#if !defined(NTDBG_H)
#define NTDBG_H

#if defined(__GNUC__)
# define FORMAT_TYPE __attribute__ ((format(__printf__,2,3)))
#else
# define FORMAT_TYPE
#endif

#define PR_ERROR        0x0001
#define PR_ALL          0x0002
#define PR_IO           0x0004
#define PR_EXEC         0x0008
#define PR_SIGNAL       0x0010
#define PR_FORK         0x0020
#define PR_IOVERB       0x0040  /* more I/O logging */
#define PR_ARGS         0x0080  /* debug argument expansion */
#define PR_WARN         0x0100  /* below 0xff is only for important messages */
#define PR_IOR          0x0200  /* ALL IO reads */
#define PR_IOW          0x0400  /* ALL IO writes */
#define PR_IOWC         0x0800  /* console output */
#define PR_FORKMEM      0x1000  /* fork() memory handling */
#define PR_ALLOC        0x2000  /* ALL fmalloc()/ffree()/morecore() calls */
#define PR_VERBOSE      0x4000  /* execve() verbose log */
#define PR_NEVER        0x0000

#if defined(W32DEBUG)
# ifdef __cplusplus
extern "C" {
# endif
/* avoid pulling windows.h */
void __declspec(dllimport) __stdcall DebugBreak(void);
void dbgprintf(int flags, const char *fmt, ...) FORMAT_TYPE;
void init_dbgprintf(const char *basename);
void set_dbgflags(int flags);
# ifdef __cplusplus
}
# endif
# if defined(_MSC_VER)
/* __asm {int 3} is accepted, but the result is not much better */
#  define DBreak()    DebugBreak()
#  define GET_ESP(x)  __asm {mov x,esp}
#  define GET_EBP(x)  __asm {mov x,ebp}
#  define GETREG(r,v) __asm {mov v,r}
#  pragma warning(disable:4995)
# elif defined(__MINGW32__)
#  define DBreak()    __asm__("int3")
#  define GET_ESP(x)  __asm__("movl %%esp,%0": "=r" (x))
#  define GET_EBP(x)  __asm__("movl %%ebp,%0": "=r" (x))
#  define GETREG(r,v) __asm__("movl %%" #r ",%0" : "=r" (v) );
# endif
#else
# if defined(__GNUC__)
/* this silences gcc complaints about dbgprintf() */
static __inline__ void dbgprintf(int flags __attribute__((unused)), const char *fmt __attribute__((unused)), ...) {}
# else
#  define dbgprintf (void)
# endif
# define DBreak()
# define GETREG(r,v)
#endif

#endif /* NTDBG_H */
