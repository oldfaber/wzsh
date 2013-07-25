/*
 *  Copyright (c) 2012 oldfaber _at_ gmail.com
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
 access() function
 this file does NOT depends on the forklib library
*/

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_DEPRECATE  1
#define _CRT_NONSTDC_NO_DEPRECATE 1
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>

#include "unistd.h"


#define ISPATHSEP(p) ((p == '/')||(p == '\\'))

static char exts[MAX_PATH];


/*
 DESCRIPTION
	exts is a PATHEXT like array, beg and end points into this array.
	extract "next" element and return it in ebuf, if size ebuflen.
	Return the length of the extracted "extension". Il ebuf is NULL
	simply return the next "extension". If ebuf is too short return 0
*/

static size_t pgetext(const char **beg, const char **end, char ebuf[], size_t ebuflen)
{
	size_t elen;

	while(**beg && (**beg != '.'))
		(*beg)++;
	while(**end && (**end != ';'))
		(*end)++;
	elen = (size_t)(*end - *beg);
	if (!ebuf)
		return elen;
	if (elen < ebuflen) {
		memcpy(ebuf, *beg, elen);
		ebuf[elen] = '\0';
		return elen;
	}
	return (0);
}


/*
 DESCRIPTION
	advance beg and end to the "next" entry in a PATHEXT-like array,
*/

static const char *pgetnext(const char **beg, const char **end)
{
	*beg = *end;
	while (**end && **end == ';')
		(*end)++;
	return *beg;
}


/*
 DESCRIPTION
	returns 1 if extension is in the PATHEXT environment variable
	0 if error or PATHEXT not found
 NOTES
	called from hashtable.c
*/

int is_pathext(const char *extension)
{
	const char *begin, *end;
	size_t extlen;
	DWORD rc;

	if (!*exts) {
		/* not initialized */
		rc = GetEnvironmentVariable("PATHEXT", exts, sizeof(exts));
		if ((rc == 0) || (rc >= sizeof(exts))) {
			*exts = 0;
			return 0;
		}
	}
	if (!extension || (*extension == 0))
		return 0;
	begin = end = exts;
	while (*begin) {
		extlen = pgetext(&begin, &end, NULL, 0);
		if (!*begin)
			break;
		if (extlen && !strnicmp(begin, extension, extlen))
			return 1;
		begin = pgetnext(&begin, &end);
	}
	return 0;
}


/*
 DESCRIPTION
	get file attributes of filename. If hasext is true extension is appended to
	the filename. If the requested mode is X_OK the filename extension is checked
	using is_pathext
*/

static DWORD get_file_attributes(const char *filename, const char *extension, int hasext, int mode, int *isx)
{
	char *buf;
	size_t filenamelen;
	DWORD attribs, bintype;

	filenamelen = strlen(filename);
	if (hasext) {
		buf = (char *)_alloca(filenamelen + 1);
		strcpy(buf, filename);
	} else {
		buf = (char *)_alloca(filenamelen + strlen(extension) + 1);
		strcpy(buf, filename);
		strcpy(buf + filenamelen, extension);
	}
	attribs = GetFileAttributes(buf);
	if ((attribs != (DWORD)(-1)) && (mode & X_OK) &&
	    (GetBinaryType(buf, &bintype) || is_pathext(extension)))
		/* found, and executable */
		*isx = 1;
	else
		*isx = 0;
	return attribs;
}


/*
 NOTES
	If PATHEXT environment variable does not exist or if it is too long or
	for any other error in GetEnvironmentVariable() then PATHEXT is
	considered empty
*/

int access(const char *filename, int mode)
{
	char extension[_MAX_FNAME];
	DWORD attribs;
	size_t extlen;
	const char *extptr;
	const char *begin, *end;
	int isx, hasext, trypathext = 0;

	/* once: get default PATHEXT or use empty exts */
	if (!*exts) {
		DWORD rc;
		/* not initialized */
		rc = GetEnvironmentVariable("PATHEXT", exts, sizeof(exts));
		if ((rc == 0) || (rc >= sizeof(exts)))
			*exts = 0;
	}

	if (!filename) {
		errno = ENOENT;
		return (-1);
	}
	/* search for the extension starting at the end */
	extptr = filename + strlen(filename) - 1;
	hasext = 0;
	while (extptr > filename && !ISPATHSEP(*extptr)) {
		if (*extptr == '.' && *(extptr - 1) != ':' && !ISPATHSEP(*(extptr - 1))) {
			hasext++;
			break;
		}
		extptr--;
	}

	if (hasext) 
		attribs = get_file_attributes(filename, extptr, hasext, mode, &isx);
	else
		attribs = get_file_attributes(filename, "", hasext, mode, &isx);

	/* if mode != X_OK or file exists or filename already has an extension ignore PATHEXT */
	if ((mode != X_OK) || (attribs != (DWORD)-1) || hasext) {
		begin = ".";
		end = "";
	} else {
		/* dir/file name not found and no extension */
		begin = exts;
		end = exts;
		trypathext = 1;
	}

	while (*begin) {
		if (trypathext) {
			extlen = pgetext(&begin, &end, extension, sizeof(extension));
			if (!*begin)
				break;
			if (extlen)
				attribs = get_file_attributes(filename, extension, hasext, mode, &isx);
			else
				attribs = (DWORD)(-1);
		}
		if (attribs != (DWORD)(-1)) {
			/* file or directory found */
			if (mode & X_OK) {
				if (attribs & FILE_ATTRIBUTE_DIRECTORY)
					break;
				/* appending pathext may find a directory ! */
				if (trypathext || isx)
					return (0);
				break;
			} else if ((mode & W_OK) && (attribs & FILE_ATTRIBUTE_READONLY)) {
				break;
			}
			/* R_OK is always OK */
			return (0);
		}
		begin = pgetnext(&begin, &end);
	}

	if (attribs == (DWORD)(-1))
		errno = ENOENT;
	else
		errno = EACCES;
	return (-1);
}
