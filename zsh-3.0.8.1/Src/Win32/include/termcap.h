/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* termcap.h -- from public domain curses (pdcurses) term.h */


#ifndef _MINI_TERM_H
#define _MINI_TERM_H 1

#if !(defined(__MINGW32__)||defined(__MINGW64__))
/* mingw headers #define this macro */
# define __UNUSED_PARAM(x) x
#endif

#ifdef	__cplusplus
extern "C" {
#endif

int tgetent (char *buffer, const char *termtype);
int tgetnum (const char *name);
int tgetflag (const char *name);
char *tgetstr (const char *name, char **area);
void tputs (const char *string, int nlines, int (*outfun) (int));
char *tparam (const char *ctlstring, char *buffer, int size, ...);
char *tgoto (const char *cstring, int hpos, int vpos);

#ifdef	__cplusplus
}
#endif

#endif /* _MINI_TERM_H */
