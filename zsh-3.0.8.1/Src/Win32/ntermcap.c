/*
 * Copyright (c) 1983 The Regents of the University of California.
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
 * $OpenBSD: advcap.c,v 1.13 2008/04/21 20:40:55 rainer Exp $
 * $KAME: advcap.c,v 1.9 2002/05/29 14:28:35 itojun Exp $
 * remcap - routines for dealing with the remote host data base
 *
 * oldfaber: modified, modifications are public domain
 *      - converted to ANSI C
 *      - tgetnum() from int64_t to int
 *      - constification
 *      - removed getent(), tnchktc(), tnamatch()
 *      - added tgoto()
 *      - added a small test main()
 */


#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
# include <io.h>
#endif

#include <termcap.h>


/*
 * termcap - routines for dealing with the terminal capability data base
 *
 * BUG:         Should use a "last" pointer in tbuf, so that searching
 *              for capabilities alphabetically would not be a n**2/2
 *              process when large numbers of capabilities are given.
 * Note:        If we add a last pointer now we will screw up the
 *              tc capability. We really should compile termcap.
 *
 * Essentially all the work here is scanning and decoding escapes
 * in string capabilities.  We don't use stdio because the editor
 * doesn't, and because living w/o it is not hard.
 */

static const char *tdecode(const char *, char **);


/*
 Windows has a termcap entry in %SYSTEMROOT%\system32\termcap.
 the capabilities strings must match the tputs() implementation
 Used:
   -- for colors
        max_colors              Co      maximum numbers of colors on screen
        max_pairs               pa      maximum number of color-pairs on the screen
        orig_pair               op      Set default pair to its original value
        orig_colors             oc      Set all color pairs to the original ones
        no_color_video          NC      video attributes that can't be used with colors
   -- standard
        cursor_address          cm      move to row #1 columns #2
        key_right               kr      right-arrow key (NOT used)
        key_up                  ku      up-arrow key (NOT used)
        key_left                kl      left-arrow key (NOT used)
        key_down                kd      down-arrow key (NOT used)
        leave stdout mode       se      reset to white on black
        enter stdout mode       so      uses bold!
*/
static const char termstr[] =
	"ansi-pc-color:"
	":Co#8:NC#3:pa#64:"
	":op=\\E[37;40m:"
	":am:bs:mi:ms:pt:"
	":co#80:it#8:li#24:"
	":cm=5\\E[%i%d;%dH:"
	":al=\\E[1L:bl=^G:cd=\\E[0J:ce=\\E[0K:cl=\\E[2J:cr=^M:"
	":dl=\\E[1M:do=\\E[1B:ho=\\E[0;0H:kb=^H:kh=\\E[h:le=\\E[1D:"
	":nd=\\E[1C:se=\\E[0m:sf=^J:so=\\E[1m:ta=^I:up=\\E[1A:"
	":LE=\\E[%dD:RI=\\E[%dC:UP=\\E[%dA:DO=\\E[%dB:";


static char const *tbuf;



/*
 * Dummy tgetent. Fails only if name is NULL.
 */
int tgetent(char *__UNUSED_PARAM(bp), const char *name)
{
	if (!name)
		return 0;
	tbuf = termstr;
	return 1;
}


/*
 * Skip to the next field.  Notice that this is very dumb, not
 * knowing about \: escapes or any such.  If necessary, :'s can be put
 * into the termcap file in octal.
 */
static const char *tskip(const char *bp)
{
	int dquote;

	dquote = 0;
	while (*bp) {
		switch (*bp) {
		case ':':
			if (!dquote)
				goto breakbreak;
			else
				bp++;
			break;
		case '\\':
			bp++;
			if (isdigit(*bp)) {
				while (isdigit(*bp++))
					;
			} else
				bp++;
		case '"':
			dquote = (dquote ? 1 : 0);
			bp++;
			break;
		default:
			bp++;
			break;
		}
	}
breakbreak:
	if (*bp == ':')
		bp++;
	return (bp);
}


/*
 * Return the (numeric) option id.
 * Numeric options look like
 *	li#80
 * i.e. the option string is separated from the numeric value by
 * a # character.  If the option is not found we return -1.
 * Note that we handle octal numbers beginning with 0.
 */
int tgetnum(const char *id)
{
	int i;
	int base;
	const char *bp = tbuf;

	for (;;) {
		bp = tskip(bp);
		if (*bp == 0)
			return (-1);
		if (strncmp(bp, id, strlen(id)) != 0)
			continue;
		bp += strlen(id);
		if (*bp == '@')
			return (-1);
		if (*bp != '#')
			continue;
		bp++;
		base = 10;
		if (*bp == '0')
			base = 8;
		i = 0;
		while (isdigit(*bp))
			i *= base, i += *bp++ - '0';
		return (i);
	}
}


/*
 * Handle a flag option.
 * Flag options are given "naked", i.e. followed by a : or the end
 * of the buffer.  Return 1 if we find the option, or 0 if it is
 * not given.
 */
int tgetflag(const char *id)
{
	const char *bp = tbuf;

	for (;;) {
		bp = tskip(bp);
		if (!*bp)
			return (0);
		if (strncmp(bp, id, strlen(id)) == 0) {
			bp += strlen(id);
			if (!*bp || *bp == ':')
				return (1);
			else if (*bp == '@')
				return (0);
		}
	}
}


/*
 * Get a string valued option.
 * These are given as
 *	cl=^Z
 * Much decoding is done on the strings, and the strings are
 * placed in area, which is a ref parameter which is updated.
 * No checking on area overflow.
 */
char *tgetstr(const char *id, char **area)
{
	const char *bp = tbuf;

	for (;;) {
		bp = tskip(bp);
		if (!*bp)
			return (0);
		if (strncmp(bp, id, strlen(id)) != 0)
			continue;
		bp += strlen(id);
		if (*bp == '@')
			return (0);
		if (*bp != '=')
			continue;
		bp++;
		return ((char *)tdecode(bp, area));
	}
}


/*
 * Tdecode does the grung work to decode the
 * string capability escapes.
 */
static const char *tdecode(const char *str, char **area)
{
	char *cp;
	int c;
	const char *dp;
	int i;
	char term;

	term = ':';
	cp = *area;
again:
	if (*str == '"') {
		term = '"';
		str++;
	}
	/* skip leading padding count */
	while (isdigit(*str))
		str++;
	while ((c = *str++) && c != term) {
		switch (c) {

		case '^':
			c = *str++ & 037;
			break;

		case '\\':
			dp = "E\033^^\\\\::n\nr\rt\tb\bf\f\"\"";
			c = *str++;
nextc:
			if (*dp++ == c) {
				c = *dp++;
				break;
			}
			dp++;
			if (*dp)
				goto nextc;
			if (isdigit(c)) {
				c -= '0', i = 2;
				do
					c <<= 3, c |= *str++ - '0';
				while (--i && isdigit(*str));
			}
			break;
		}
		*cp++ = (char)c;
	}
	if (c == term && term != ':') {
		term = ':';
		goto again;
	}
	*cp++ = 0;
	str = *area;
	*area = cp;
	return (str);
}


/* reverse src into dst */
static int revs(char *dst, const char *src)
{
	int n = 0;
	char tail = *src;

	if (tail) {
		n = revs(dst, src + 1);
		dst[n++] = tail;
	}
	dst[n] = '\0';
	return n;
}


/* convert val to ascii string */
static int uitoa(char *p, unsigned int val)
{
	char *psave = p;

	while (val) {
		*p++ = (char)(val % 10U + '0');
		val = val / 10U;
	}
	*p = '\0';
	/* revert in place, return the length of the converted string */
	return revs(psave, psave);
}


/*
 DESCRIPTION
        Very simple tgoto().
        The terminal definition used under win32 only uses %d
        and this code correctly encodes these strings
*/     

char *tgoto(const char *cm, int hpos, int vpos)
{
	static char tgotobuf[256];
	char *dstp = tgotobuf;
	int len;
	int incr = 0;
	int hdone = 0;
	
	if (!cm)
		return NULL;
	while (*cm) {
		if (*cm == '%') {
			cm++;
			switch (*cm++) {
			case 'd':
				if (hdone) {
					if (incr)
						hpos++;
					len = uitoa(dstp, (unsigned int)hpos);
				} else {
                                        hdone++;
					if (incr)
						vpos++;
					len = uitoa(dstp, (unsigned int)vpos);
				}
				dstp += len;
				continue;
			case 'i':
				incr++;
				continue;
			default:
				/* ignores incorrect format !!! */
				*dstp++ = *cm++;
			}
		} else
			*dstp++ = *cm++;
	}
	*dstp = '\0';
	return tgotobuf;
}


#if defined(__TESTING__)
/* see http://www.gnu.org/software/termutils/manual/termcap-1.3/html_node/termcap_44.html#SEC44 */

void nt_init_term(void);
void nt_cleanup_term(void);

int main(void)
{
	int cols;
	int fl;
	char sLE[128];
	char *u;
	char *rv;

	tbuf = termstr;
	tgetent(NULL, "dummy");
	fl = tgetflag("mi");
	printf("tgetflag(\"mi\") returned %d\n", fl);
	cols = tgetnum("co");
	printf("tgetnum(\"co\") returned %d\n", cols);
	u = sLE;
	rv = tgetstr("LE", &u);
	printf("tgetstr(\"LE\") returned %s\n", rv);

	/* test tgoto() */
	rv = tgetstr("cm", &u);
	printf("result=\"%s\"\n", tgoto(rv, 5, 1));
	nt_init_term();
	rv = tgetstr("cl", &u);
	printf("tgetstr(\"cl\") returned %s\n", rv);
	tputs(rv, 1, NULL);
	nt_cleanup_term();

	return (0);
}

#endif
