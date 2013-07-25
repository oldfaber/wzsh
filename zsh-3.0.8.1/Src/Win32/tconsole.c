/*
 *  Copyright (c) 2005-2011 Jason Hood
 *  Copyright (c) 2012-2013 oldfaber _at_ gmail.com
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

/*
 * This file is the CONOUT$ handler, and replaces console.c and tparse.c.
 * All console output is driven by "termcap style escape sequences".
 * The interpreter of the escape sequences, implemented in
 * InterpretEscSeq() and tputs(), comes from
 *      ANSICON
 *      Copyright 2005-2011 Jason Hood
 *      Version 1.50.  Freeware
 *      (who got it from Jean-Louis Morel)
 * but has been simplified for zsh use. 
 * This file also includes
 *      nt_init_term()/nt_cleanup_term(): setup the console
 *      nt_setcursor(): change cursor shape
 * NOTES
 *      if standard output has been redirected GetConsoleScreenBufferInfo() fails for the
 *      redirected handle, but an handle to the console buffer can be obtained with a
 *      CreateFile("CONOUT$", ...). Removed the handle duplication and if we have
 *      a console use CreateFIle()
 *      if hConOut is invalid the Win32 call failure will be reported
 * TODO
 *      add logging and check WIN32 calls for failure
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ctype.h>

#include "forksect.h"
#include "ntdbg.h"
#include <termcap.h>


#define ESC     '\x1B'
/* color constants */
#define FOREGROUND_BLACK 0
#define FOREGROUND_WHITE FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE
#define BACKGROUND_BLACK 0
#define BACKGROUND_WHITE BACKGROUND_RED|BACKGROUND_GREEN|BACKGROUND_BLUE


NCSTATIC HANDLE hConOut;

#define MAX_ARG 16	      /* max number of args in an escape sequence */
NCSTATIC enum {initial, gotesc, gotprefix, gotarg} state;
NCSTATIC int prefix;			/* escape sequence prefix '[' */
NCSTATIC int suffix;			/* escape sequence suffix */
NCSTATIC int es_argc;			/* escape sequence args count */
NCSTATIC int es_argv[MAX_ARG];		/* escape sequence args */
NCSTATIC int glines, gcolumns;

/* screen attributes */
NCSTATIC WORD foreground = FOREGROUND_WHITE;
NCSTATIC WORD background = BACKGROUND_BLACK;
NCSTATIC WORD bold;
NCSTATIC WORD underline;
NCSTATIC WORD rvideo;
NCSTATIC WORD concealed;
/* saved cursor position and information */
NCSTATIC COORD SavePos;
NCSTATIC BOOL cursinfo_valid;
NCSTATIC CONSOLE_CURSOR_INFO cursinfo;

/* print buffer */
NCSTATIC int nCharInBuffer;
NCSTATIC char ChBuffer[256];


NCSTATIC WORD foregroundcolor[8] = {
	FOREGROUND_BLACK,	// black foreground
	FOREGROUND_RED,		// red foreground
	FOREGROUND_GREEN,	// green foreground
	FOREGROUND_RED | FOREGROUND_GREEN,	// yellow foreground
	FOREGROUND_BLUE,	// blue foreground
	FOREGROUND_BLUE | FOREGROUND_RED,	// magenta foreground
	FOREGROUND_BLUE | FOREGROUND_GREEN,	// cyan foreground
	FOREGROUND_WHITE	// white foreground
};

NCSTATIC WORD backgroundcolor[8] = {
	BACKGROUND_BLACK,	// black background
	BACKGROUND_RED,		// red background
	BACKGROUND_GREEN,	// green background
	BACKGROUND_RED | BACKGROUND_GREEN,	// yellow background
	BACKGROUND_BLUE,	// blue background
	BACKGROUND_BLUE | BACKGROUND_RED,	// magenta background
	BACKGROUND_BLUE | BACKGROUND_GREEN,	// cyan background
	BACKGROUND_WHITE,	// white background
};



void nt_init_term(void)
{
	DWORD dwmode;
	CONSOLE_SCREEN_BUFFER_INFO scrbuf;
	HANDLE hinput = GetStdHandle(STD_INPUT_HANDLE);

	if (GetConsoleMode(hinput, &dwmode)) {
		if (!SetConsoleMode(hinput, dwmode | ENABLE_WINDOW_INPUT))
			dbgprintf(PR_ERROR, "!!! %s(): SetConsoleMode(0x%p, ..) error %ld\n", __FUNCTION__, hinput, GetLastError());
	}
	/* defaults */
	glines = 25;
	gcolumns = 80;
	hConOut = CreateFile("CONOUT$", GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hConOut == INVALID_HANDLE_VALUE) {
		dbgprintf(PR_ERROR, "!!! %s(): CreateFile(\"CONOUT$\", ..) error %ld\n", __FUNCTION__, GetLastError());
		return;
	}
	if (!GetConsoleScreenBufferInfo(hConOut, &scrbuf)) {
		dbgprintf(PR_ERROR, "!!! %s(): GetConsoleScreenBufferInfo(0x%p, ..) error %ld\n", __FUNCTION__, hConOut, GetLastError());
		/* fail all remaining calls */
		hConOut = INVALID_HANDLE_VALUE;
		return;
	}
	if (!GetConsoleCursorInfo(hConOut, &cursinfo))
		dbgprintf(PR_ERROR, "!!! %s(): GetConsoleCursorInfo(0x%p, ..) error %ld\n", __FUNCTION__, hConOut, GetLastError());
        cursinfo_valid = TRUE;
	glines = scrbuf.srWindow.Bottom - scrbuf.srWindow.Top + 1;
	gcolumns = scrbuf.srWindow.Right - scrbuf.srWindow.Left + 1;
}


/* close the console output handle */
void nt_cleanup_term(void)
{
	if (hConOut && hConOut != INVALID_HANDLE_VALUE) {
		if (cursinfo_valid)
			SetConsoleCursorInfo(hConOut, &cursinfo);
		CloseHandle(hConOut);
	}
}

int nt_getlines(void)
{
	return glines;
}


int nt_getcolumns(void)
{
	return gcolumns;
}


/*
 FlushBuffer()
        write to the console.
*/

static void FlushBuffer(void)
{
	DWORD nWritten;

	if (nCharInBuffer <= 0)
		return;
	WriteConsole(hConOut, ChBuffer, nCharInBuffer, &nWritten, NULL);
	nCharInBuffer = 0;
}


/*
 InterpretEscSeq()

        Interprets the last escape sequence scanned by ParseAndPrintString
        prefix             escape sequence prefix
        es_argc            escape sequence args count
        es_argv[]          escape sequence args array
        suffix             escape sequence suffix

        for instance, with \e[33;45;1m we have
        prefix = '[',
        es_argc = 3, es_argv[0] = 33, es_argv[1] = 45, es_argv[2] = 1
        suffix = 'm'
*/

static void InterpretEscSeq(void)
{
	int i;
	WORD attribut;
	CONSOLE_SCREEN_BUFFER_INFO Info;
	DWORD NumberOfCharsWritten;
	COORD Pos;
	SMALL_RECT Rect;
	CHAR_INFO CharInfo;

	if (prefix != '[')
		return;
	GetConsoleScreenBufferInfo(hConOut, &Info);
	switch (suffix) {
	case 'm':
		if (es_argc == 0)
			es_argv[es_argc++] = 0;
		for (i = 0; i < es_argc; i++) {
			switch (es_argv[i]) {
			case 0:
				foreground = FOREGROUND_WHITE;
				background = BACKGROUND_BLACK;
				bold = 0;
				underline = 0;
				rvideo = 0;
				concealed = 0;
				break;
			case 1:
				bold = 1;
				break;
			case 21:
				bold = 0;
				break;
			case 4:
				underline = 1;
				break;
			case 24:
				underline = 0;
				break;
			case 7:
				rvideo = 1;
				break;
			case 27:
				rvideo = 0;
				break;
			case 8:
				concealed = 1;
				break;
			case 28:
				concealed = 0;
				break;
			}
			if ((30 <= es_argv[i]) && (es_argv[i] <= 37))
				foreground = (WORD)(es_argv[i] - 30);
			if ((40 <= es_argv[i]) && (es_argv[i] <= 47))
				background = (WORD)(es_argv[i] - 40);
		}
		if (rvideo)
			attribut = foregroundcolor[background] | backgroundcolor[foreground];
		else
			attribut = foregroundcolor[foreground] | backgroundcolor[background];
		if (bold)
			attribut |= FOREGROUND_INTENSITY;
		if (underline)
			attribut |= BACKGROUND_INTENSITY;
		SetConsoleTextAttribute(hConOut, attribut);
		return;

	case 'J':
		if (es_argc == 0)
			es_argv[es_argc++] = 0;	// ESC[J == ESC[0J
		if (es_argc != 1)
			return;
		switch (es_argv[0]) {
		case 0:	// ESC[0J erase from cursor to end of display
			FillConsoleOutputCharacter(hConOut, ' ', (Info.dwSize.Y - Info.dwCursorPosition.Y - 1) * Info.dwSize.X + Info.dwSize.X - Info.dwCursorPosition.X - 1, Info.dwCursorPosition, &NumberOfCharsWritten);
			FillConsoleOutputAttribute(hConOut, Info.wAttributes, (Info.dwSize.Y - Info.dwCursorPosition.Y - 1) * Info.dwSize.X + Info.dwSize.X - Info.dwCursorPosition.X - 1, Info.dwCursorPosition, &NumberOfCharsWritten);
			return;

		case 1:	// ESC[1J erase from start to cursor.
			Pos.X = 0;
			Pos.Y = 0;
			FillConsoleOutputCharacter(hConOut, ' ', Info.dwCursorPosition.Y * Info.dwSize.X + Info.dwCursorPosition.X + 1, Pos, &NumberOfCharsWritten);
			FillConsoleOutputAttribute(hConOut, Info.wAttributes, Info.dwCursorPosition.Y * Info.dwSize.X + Info.dwCursorPosition.X + 1, Pos, &NumberOfCharsWritten);
			return;

		case 2:	// ESC[2J Clear screen and home cursor
			Pos.X = 0;
			Pos.Y = 0;
			FillConsoleOutputCharacter(hConOut, ' ', Info.dwSize.X * Info.dwSize.Y, Pos, &NumberOfCharsWritten);
			FillConsoleOutputAttribute(hConOut, Info.wAttributes, Info.dwSize.X * Info.dwSize.Y, Pos, &NumberOfCharsWritten);
			SetConsoleCursorPosition(hConOut, Pos);
			return;

		default:
			return;
		}

	case 'K':
		if (es_argc == 0)
			es_argv[es_argc++] = 0;	// ESC[K == ESC[0K
		if (es_argc != 1)
			return;
		switch (es_argv[0]) {
		case 0:	// ESC[0K Clear to end of line
			FillConsoleOutputCharacter(hConOut, ' ', Info.srWindow.Right - Info.dwCursorPosition.X + 1, Info.dwCursorPosition, &NumberOfCharsWritten);
			FillConsoleOutputAttribute(hConOut, Info.wAttributes, Info.srWindow.Right - Info.dwCursorPosition.X + 1, Info.dwCursorPosition, &NumberOfCharsWritten);
			return;

		case 1:	// ESC[1K Clear from start of line to cursor
			Pos.X = 0;
			Pos.Y = Info.dwCursorPosition.Y;
			FillConsoleOutputCharacter(hConOut, ' ', Info.dwCursorPosition.X + 1, Pos, &NumberOfCharsWritten);
			FillConsoleOutputAttribute(hConOut, Info.wAttributes, Info.dwCursorPosition.X + 1, Pos, &NumberOfCharsWritten);
			return;

		case 2:	// ESC[2K Clear whole line.
			Pos.X = 0;
			Pos.Y = Info.dwCursorPosition.Y;
			FillConsoleOutputCharacter(hConOut, ' ', Info.dwSize.X, Pos, &NumberOfCharsWritten);
			FillConsoleOutputAttribute(hConOut, Info.wAttributes, Info.dwSize.X, Pos, &NumberOfCharsWritten);
			return;

		default:
			return;
		}

	case 'L':	// ESC[#L Insert # blank lines.
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[L == ESC[1L
		if (es_argc != 1)
			return;
			Rect.Left = 0;
			Rect.Top = Info.dwCursorPosition.Y;
			Rect.Right = Info.dwSize.X - 1;
			Rect.Bottom = Info.dwSize.Y - 1;
			Pos.X = 0;
			Pos.Y = (SHORT)(Info.dwCursorPosition.Y + es_argv[0]);
			CharInfo.Char.AsciiChar = ' ';
			CharInfo.Attributes = Info.wAttributes;
			ScrollConsoleScreenBuffer(hConOut, &Rect, NULL, Pos, &CharInfo);
			Pos.X = 0;
			Pos.Y = Info.dwCursorPosition.Y;
			FillConsoleOutputCharacter(hConOut, ' ', Info.dwSize.X * es_argv[0], Pos, &NumberOfCharsWritten);
			FillConsoleOutputAttribute(hConOut, Info.wAttributes, Info.dwSize.X * es_argv[0], Pos, &NumberOfCharsWritten);
			return;

	case 'M':	// ESC[#M Delete # line.
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[M == ESC[1M
		if (es_argc != 1)
			return;
		if (es_argv[0] > Info.dwSize.Y - Info.dwCursorPosition.Y)
			es_argv[0] = Info.dwSize.Y - Info.dwCursorPosition.Y;
		Rect.Left = 0;
		Rect.Top = (SHORT)(Info.dwCursorPosition.Y + es_argv[0]);
		Rect.Right = Info.dwSize.X - 1;
		Rect.Bottom = Info.dwSize.Y - 1;
		Pos.X = 0;
		Pos.Y = Info.dwCursorPosition.Y;
		CharInfo.Char.AsciiChar = ' ';
		CharInfo.Attributes = Info.wAttributes;
		ScrollConsoleScreenBuffer(hConOut, &Rect, NULL, Pos, &CharInfo);
		Pos.Y = (SHORT)(Info.dwSize.Y - es_argv[0]);
		FillConsoleOutputCharacter(hConOut, ' ', Info.dwSize.X * es_argv[0], Pos, &NumberOfCharsWritten);
		FillConsoleOutputAttribute(hConOut, Info.wAttributes, Info.dwSize.X * es_argv[0], Pos, &NumberOfCharsWritten);
		return;

	case 'P':	// ESC[#P Delete # characters.
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[P == ESC[1P
		if (es_argc != 1)
			return;
		if (Info.dwCursorPosition.X + es_argv[0] > Info.dwSize.X - 1)
			es_argv[0] = Info.dwSize.X - Info.dwCursorPosition.X;

		Rect.Left = (SHORT)(Info.dwCursorPosition.X + es_argv[0]);
		Rect.Top = Info.dwCursorPosition.Y;
		Rect.Right = Info.dwSize.X - 1;
		Rect.Bottom = Info.dwCursorPosition.Y;
		CharInfo.Char.AsciiChar = ' ';
		CharInfo.Attributes = Info.wAttributes;
		ScrollConsoleScreenBuffer(hConOut, &Rect, NULL, Info.dwCursorPosition, &CharInfo);
		Pos.X = (SHORT)(Info.dwSize.X - es_argv[0]);
		Pos.Y = Info.dwCursorPosition.Y;
		FillConsoleOutputCharacter(hConOut, ' ', es_argv[0], Pos, &NumberOfCharsWritten);
		return;

	case '@':	// ESC[#@ Insert # blank characters.
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[@ == ESC[1@
		if (es_argc != 1)
			return;
		if (Info.dwCursorPosition.X + es_argv[0] > Info.dwSize.X - 1)
			es_argv[0] = Info.dwSize.X - Info.dwCursorPosition.X;
		Rect.Left = Info.dwCursorPosition.X;
		Rect.Top = Info.dwCursorPosition.Y;
		Rect.Right = Info.dwSize.X - 1 - es_argv[0];
		Rect.Bottom = Info.dwCursorPosition.Y;
		Pos.X = Info.dwCursorPosition.X + es_argv[0];
		Pos.Y = Info.dwCursorPosition.Y;
		CharInfo.Char.AsciiChar = ' ';
		CharInfo.Attributes = Info.wAttributes;
		ScrollConsoleScreenBuffer(hConOut, &Rect, NULL, Pos, &CharInfo);
		FillConsoleOutputCharacter(hConOut, ' ', es_argv[0], Info.dwCursorPosition, &NumberOfCharsWritten);
		FillConsoleOutputAttribute(hConOut, Info.wAttributes, es_argv[0], Info.dwCursorPosition, &NumberOfCharsWritten);
		return;

	case 'A':	// ESC[#A Moves cursor up # lines
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[A == ESC[1A
		if (es_argc != 1)
			return;
		Pos.X = Info.dwCursorPosition.X;
		Pos.Y = Info.dwCursorPosition.Y - es_argv[0];
		if (Pos.Y < 0)
			Pos.Y = 0;
		SetConsoleCursorPosition(hConOut, Pos);
		return;

	case 'B':	// ESC[#B Moves cursor down # lines
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[B == ESC[1B
		if (es_argc != 1)
			return;
		Pos.X = Info.dwCursorPosition.X;
		Pos.Y = Info.dwCursorPosition.Y + es_argv[0];
		if (Pos.Y >= Info.dwSize.Y)
			Pos.Y = Info.dwSize.Y - 1;
		SetConsoleCursorPosition(hConOut, Pos);
		return;

	case 'C':	// ESC[#C Moves cursor forward # spaces
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[C == ESC[1C
		if (es_argc != 1)
			return;
		Pos.X = Info.dwCursorPosition.X + es_argv[0];
		if (Pos.X >= Info.dwSize.X)
			Pos.X = Info.dwSize.X - 1;
		Pos.Y = Info.dwCursorPosition.Y;
		SetConsoleCursorPosition(hConOut, Pos);
		return;

	case 'D':	// ESC[#D Moves cursor back # spaces
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[D == ESC[1D
		if (es_argc != 1)
			return;
		Pos.X = Info.dwCursorPosition.X - es_argv[0];
		if (Pos.X < 0)
			Pos.X = 0;
		Pos.Y = Info.dwCursorPosition.Y;
		SetConsoleCursorPosition(hConOut, Pos);
		return;

	case 'E':	// ESC[#E Moves cursor down # lines, column 1.
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[E == ESC[1E
		if (es_argc != 1)
			return;
		Pos.X = 0;
		Pos.Y = Info.dwCursorPosition.Y + es_argv[0];
		if (Pos.Y >= Info.dwSize.Y)
			Pos.Y = Info.dwSize.Y - 1;
		SetConsoleCursorPosition(hConOut, Pos);
		return;

	case 'F':	// ESC[#F Moves cursor up # lines, column 1.
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[F == ESC[1F
		if (es_argc != 1)
			return;
		Pos.X = 0;
		Pos.Y = Info.dwCursorPosition.Y - es_argv[0];
		if (Pos.Y < 0)
			Pos.Y = 0;
		SetConsoleCursorPosition(hConOut, Pos);
		return;

	case 'G':	// ESC[#G Moves cursor column # in current row.
		if (es_argc == 0)
			es_argv[es_argc++] = 1;	// ESC[G == ESC[1G
		if (es_argc != 1)
			return;
		Pos.X = es_argv[0] - 1;
		if (Pos.X >= Info.dwSize.X)
			Pos.X = Info.dwSize.X - 1;
		if (Pos.X < 0)
			Pos.X = 0;
		Pos.Y = Info.dwCursorPosition.Y;
		SetConsoleCursorPosition(hConOut, Pos);
		return;

	case 'f':
	case 'H':	// ESC[#;#H or ESC[#;#f Moves cursor to line #, column #
		if (es_argc == 0) {
			es_argv[es_argc++] = 1;	// ESC[G == ESC[1;1G
			es_argv[es_argc++] = 1;
		}
		if (es_argc == 1) {
			es_argv[es_argc++] = 1;	// ESC[nG == ESC[n;1G
		}
		if (es_argc > 2)
			return;
		Pos.X = es_argv[1] - 1;
		if (Pos.X < 0)
			Pos.X = 0;
		if (Pos.X >= Info.dwSize.X)
			Pos.X = Info.dwSize.X - 1;
		Pos.Y = es_argv[0] - 1;
		if (Pos.Y < 0)
			Pos.Y = 0;
		if (Pos.Y >= Info.dwSize.Y)
			Pos.Y = Info.dwSize.Y - 1;
		SetConsoleCursorPosition(hConOut, Pos);
		return;

	case 's':	// ESC[s Saves cursor position for recall later
		if (es_argc != 0)
			return;
		SavePos.X = Info.dwCursorPosition.X;
		SavePos.Y = Info.dwCursorPosition.Y;
		return;

	case 'u':	// ESC[u Return to saved cursor position
		if (es_argc != 0)
			return;
		SetConsoleCursorPosition(hConOut, SavePos);
		return;
	}
}


/*
 DESCRIPTION
        Output the string to the console. This function is a four states automata.
        If the number of arguments es_argc > MAX_ARG, only arguments 0..MAX_ARG-1
        are processed (no es_argv[] overflow).
 NOTES
        The output function outfun is ignored.
        The original name of this function was ParseAndPrintString()
*/

void tputs(const char *str, int __UNUSED_PARAM(nlines), int __UNUSED_PARAM((*outfun)(int)))
{

	if (!str) {
		dbgprintf(PR_ERROR, "!!! %s(): NULL string\n", __FUNCTION__);
		return;
	}

	dbgprintf(PR_IOWC, "%s(\"%s\", ..)\n", __FUNCTION__, str);
	while (*str) {
		if (state == initial) {
			if (*str == ESC)
				state = gotesc;
			else {
				ChBuffer[nCharInBuffer++] = concealed ? ' ' : *str;
				if (nCharInBuffer >= (int)sizeof(ChBuffer))
					FlushBuffer();
			}
		} else if (state == gotesc) {
			if (*str == ESC)
				;	// \e\e...\e == \e
			else if (*str == '[') {
				FlushBuffer();
				prefix = *str;
				state = gotprefix;
			} else
				state = initial;
		} else if (state == gotprefix) {
			if (isdigit(*str)) {
				es_argc = 0;
				es_argv[0] = *str - '0';
				state = gotarg;
			} else if (*str == ';') {
				es_argc = 1;
				es_argv[0] = 0;
				es_argv[es_argc] = 0;
				state = gotarg;
			} else {
				es_argc = 0;
				suffix = *str;
				InterpretEscSeq();
				state = initial;
			}
		} else if (state == gotarg) {
			if (isdigit(*str)) {
				es_argv[es_argc] = 10 * es_argv[es_argc] + (*str - '0');
			} else if (*str == ';') {
				if (es_argc < MAX_ARG - 1)
					es_argc++;
				es_argv[es_argc] = 0;
			} else {
				if (es_argc < MAX_ARG - 1)
					es_argc++;
				suffix = *str;
				InterpretEscSeq();
				state = initial;
			}
		}
		str++;
	}
	FlushBuffer();
}


/* toggle cursor for insert mode */
void nt_setcursor(int mode)
{
	CONSOLE_CURSOR_INFO newinfo;

	if (!cursinfo_valid)
		return;
	if (!GetConsoleCursorInfo(hConOut, &newinfo)) {
		dbgprintf(PR_ERROR, "!!! %s(): GetConsoleCursorInfo(0x%p, ..) error %ld\n", __FUNCTION__, hConOut, GetLastError());
		return;
	}
	if (mode & 1)
		newinfo.dwSize = cursinfo.dwSize;
	else
		newinfo.dwSize = cursinfo.dwSize * 3;
	SetConsoleCursorInfo(hConOut, &newinfo);
}
