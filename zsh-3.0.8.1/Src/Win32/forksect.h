/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* forksect.h -- fork library data sections.
                 Variables defined in 00_entry.c and zz_ntb2.c */


#if !defined(FORKSECT_H)
#define FORKSECT_H

#if defined(_MSC_VER)
# pragma section(".nocopy")
#endif

extern unsigned long bookbss1,bookbss2;
extern unsigned long bookcommon1,bookcommon2;
extern unsigned long bookdata1,bookdata2;

/* static and global variables not copied in a fork() */
#if defined(_MSC_VER)
# define NCSTATIC static __declspec(allocate(".nocopy"))
# define NCGLOBAL __declspec(allocate(".nocopy"))
#endif
#if defined(__MINGW32__)||defined(__TINYC__)
# define NCSTATIC static __attribute__((section(".nocopy")))
# define NCGLOBAL __attribute__((section(".nocopy")))
#endif

#endif /* FORKSECT_H */
